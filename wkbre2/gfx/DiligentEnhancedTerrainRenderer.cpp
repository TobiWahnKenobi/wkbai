// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

// Port of the former D3D11EnhancedTerrainRenderer to Diligent: same shader, same culling of tiles, tiles
// drawn grouped by texture from one vertex buffer of the whole map, then lakes through
// IRenderer. The packed normals/tangents are unpacked in the vertex shader.

#include "DiligentEnhancedTerrainRenderer.h"
#include "EnhancedTerrainData.h"
#include "renderer_diligent.h"
#include "TextureCache.h"
#include "../terrain.h"
#include "../Camera.h"
#include "../util/util.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <tuple>
#include <utility>
#include <vector>

using namespace Diligent;

static const char tshaderCode[] = R"---(
Texture2D inpTexture;
Texture2D normalTexture;
SamplerState inpSampler;

cbuffer TransformBuffer
{
	matrix Transform;
};

cbuffer FogBuffer
{
	float4 FogColor;
	float FogStartDist;
	float FogEndDist;
};

cbuffer SunBuffer
{
	float3 SunDirection;
	int BumpOn;
};

struct VS_OUTPUT
{
	float4 Pos : SV_POSITION;
	float3 FragPos : COLOR1;
	float3 Norm: NORMAL;
	float4 Color : COLOR0;
	float2 Texcoord : TEXCOORD0;
	float Fog : FOG;
	float3 Tangent : TANGENT0;
	float3 Bitangent : TANGENT1;
};

// R10G10B10A2_UNORM
float4 Unpack1010102(uint p)
{
	return float4(float3(p & 1023, (p >> 10) & 1023, (p >> 20) & 1023) / 1023.0, (p >> 30) / 3.0);
}

VS_OUTPUT VS(float3 Pos3 : ATTRIB0, uint NormalPacked : ATTRIB1, uint TangentPacked : ATTRIB2, uint BitangentPacked : ATTRIB3, float2 Texcoord : ATTRIB4)
{
	float4 Pos = float4(Pos3, 1);
	float4 Normal = Unpack1010102(NormalPacked);
	float3 Tangent = Unpack1010102(TangentPacked).xyz;
	float3 Bitangent = Unpack1010102(BitangentPacked).xyz;
	VS_OUTPUT output = (VS_OUTPUT)0;
	output.Pos = mul(Pos, Transform);
	output.FragPos = Pos.xyz;
	output.Norm = Normal.xyz * 2 - float3(1,1,1);
	output.Color = float4((Normal.w * 0.5 + 0.5).xxx, 1);
	output.Texcoord = Texcoord;
	output.Fog = clamp((output.Pos.w-FogStartDist) / (FogEndDist-FogStartDist), 0, 1);
	output.Tangent = Tangent * 2 - float3(1,1,1);
	output.Bitangent = Bitangent * 2 - float3(1,1,1);
	return output;
}

float4 PS(VS_OUTPUT input) : SV_Target
{
	// Compute the length of a half pixel in the current mipmap
	float lod = inpTexture.CalculateLevelOfDetail(inpSampler, input.Texcoord);
	float ha = 0.5 / (256 >> (int)ceil(lod));

	float2 stc = clamp(input.Texcoord, float2(ha,ha), float2(1.0-ha,1.0-ha));
	float2 tile = floor(stc / float2(0.25,0.25)); // tile index in the 4x4 atlas
	float2 subuv = fmod(stc, float2(0.25,0.25)); // coords inside the used tile

	float2 fuv = clamp(subuv, float2(ha,ha), float2(0.25-ha,0.25-ha)) + tile*0.25;

	float4 tex = inpTexture.Sample(inpSampler, fuv);

	// The original lights the terrain with a Dot3 bump-mapping pass and no ambient term: the
	// per-vertex light vector (the sun direction in tangent space, shortened where static and
	// cloud shadows fall) dotted with the normal map, clamped, times the texture.
	// See docs/re/lighting.md.
	float lum = 0;

	if(BumpOn) {
		float3 nrm = normalTexture.Sample(inpSampler, fuv).xyz * 2 - 1;

		float3 m1 = input.Tangent;
		float3 m2 = input.Norm;
		float3 m3 = input.Bitangent;
		float3 actnorm = nrm.xxx * m1 + nrm.yyy * m2 + nrm.zzz * m3;
		lum += clamp(dot(normalize(actnorm), SunDirection), 0, 1);
		lum = clamp(lum, 0, 1);
	}
	else {
		lum += clamp(dot(input.Norm, SunDirection), 0, 1);
		lum = clamp(lum, 0, 1);
	}
	return lerp(float4(lum.xxx, 1) * input.Color * tex, FogColor, input.Fog);
}
)---";

struct DiligentEnhancedTerrainRenderer::Impl {
	RefCntAutoPtr<IPipelineResourceSignature> signature;
	RefCntAutoPtr<IPipelineState> pipeline;
	RefCntAutoPtr<IBuffer> sunBuffer;
	RefCntAutoPtr<IBuffer> trnVertexBuffer;
	std::unique_ptr<RBatch> lakeBatch;

	using TexturePair = std::pair<texture, texture>; // diffuse, normal map
	std::map<TerrainTexture*, TexturePair> textures;
	std::map<TexturePair, RefCntAutoPtr<IShaderResourceBinding>> bindings;
	std::map<TexturePair, std::vector<const TerrainTile*>> tilesPerTex;
	std::vector<uint32_t> indices;
};

void DiligentEnhancedTerrainRenderer::init()
{
	DiligentGfx* dgfx = (DiligentGfx*)gfx;
	IRenderDevice* device = dgfx->device;
	_impl = new Impl;
	_impl->lakeBatch.reset(gfx->CreateBatch(4 * 256, 6 * 256));
	texcache = new TextureCache(gfx, "Maps\\Map_Textures\\");

	std::map<TerrainTexture*, EnhancedTerrainTexture> textures;
	LoadEnhancedTerrainTextures(terrain, texcache, textures);
	for (const auto& [trntex, et] : textures)
		_impl->textures[trntex] = { et.diffuseMap, et.normalMap };

	std::vector<EnhancedTerrainVertex> trnVertices = BuildEnhancedTerrainVertices(terrain);
	BufferDesc vbDesc;
	vbDesc.Name = "Terrain vertices";
	vbDesc.Size = trnVertices.size() * sizeof(EnhancedTerrainVertex);
	vbDesc.BindFlags = BIND_VERTEX_BUFFER;
	vbDesc.Usage = USAGE_IMMUTABLE;
	BufferData vbData{ trnVertices.data(), vbDesc.Size };
	device->CreateBuffer(vbDesc, &vbData, &_impl->trnVertexBuffer);
	if (!_impl->trnVertexBuffer)
		ferr("Diligent: could not create the terrain vertex buffer.");

	BufferDesc cbDesc;
	cbDesc.Name = "Terrain sun";
	cbDesc.Size = 16;
	cbDesc.BindFlags = BIND_UNIFORM_BUFFER;
	cbDesc.Usage = USAGE_DYNAMIC;
	cbDesc.CPUAccessFlags = CPU_ACCESS_WRITE;
	device->CreateBuffer(cbDesc, nullptr, &_impl->sunBuffer);

	const PipelineResourceDesc resources[] = {
		{ SHADER_TYPE_VERTEX, "TransformBuffer", 1, SHADER_RESOURCE_TYPE_CONSTANT_BUFFER, SHADER_RESOURCE_VARIABLE_TYPE_STATIC },
		{ SHADER_TYPE_VERTEX | SHADER_TYPE_PIXEL, "FogBuffer", 1, SHADER_RESOURCE_TYPE_CONSTANT_BUFFER, SHADER_RESOURCE_VARIABLE_TYPE_STATIC },
		{ SHADER_TYPE_PIXEL, "SunBuffer", 1, SHADER_RESOURCE_TYPE_CONSTANT_BUFFER, SHADER_RESOURCE_VARIABLE_TYPE_STATIC },
		{ SHADER_TYPE_PIXEL, "inpTexture", 1, SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
		{ SHADER_TYPE_PIXEL, "normalTexture", 1, SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
	};
	SamplerDesc sampler; // D3D11's default sampler: trilinear, clamped
	sampler.AddressU = sampler.AddressV = sampler.AddressW = TEXTURE_ADDRESS_CLAMP;
	const ImmutableSamplerDesc samplers[] = { { SHADER_TYPE_PIXEL, "inpSampler", sampler } };
	PipelineResourceSignatureDesc sigDesc;
	sigDesc.Name = "Enhanced terrain resources";
	sigDesc.Resources = resources;
	sigDesc.NumResources = (Uint32)std::size(resources);
	sigDesc.ImmutableSamplers = samplers;
	sigDesc.NumImmutableSamplers = (Uint32)std::size(samplers);
	device->CreatePipelineResourceSignature(sigDesc, &_impl->signature);
	if (!_impl->signature)
		ferr("Diligent: could not create the enhanced terrain resource signature.");
	_impl->signature->GetStaticVariableByName(SHADER_TYPE_VERTEX, "TransformBuffer")->Set(dgfx->transformBuffer);
	_impl->signature->GetStaticVariableByName(SHADER_TYPE_VERTEX, "FogBuffer")->Set(dgfx->fogBuffer);
	_impl->signature->GetStaticVariableByName(SHADER_TYPE_PIXEL, "FogBuffer")->Set(dgfx->fogBuffer);
	_impl->signature->GetStaticVariableByName(SHADER_TYPE_PIXEL, "SunBuffer")->Set(_impl->sunBuffer);

	auto createShader = [&](SHADER_TYPE type, const char* entryPoint) {
		ShaderCreateInfo sci;
		sci.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
		sci.Source = tshaderCode;
		sci.SourceLength = sizeof(tshaderCode) - 1;
		sci.EntryPoint = entryPoint;
		sci.Desc.ShaderType = type;
		sci.Desc.Name = entryPoint;
		RefCntAutoPtr<IShader> shader;
		device->CreateShader(sci, &shader);
		if (!shader)
			ferr("Diligent: failed to compile enhanced terrain shader %s", entryPoint);
		return shader;
	};
	RefCntAutoPtr<IShader> vs = createShader(SHADER_TYPE_VERTEX, "VS");
	RefCntAutoPtr<IShader> ps = createShader(SHADER_TYPE_PIXEL, "PS");

	// EnhancedTerrainVertex: position, normal, tangent, bitangent, UV
	static const LayoutElement layout[] = {
		LayoutElement{0, 0, 3, VT_FLOAT32, False},
		LayoutElement{1, 0, 1, VT_UINT32, False},
		LayoutElement{2, 0, 1, VT_UINT32, False},
		LayoutElement{3, 0, 1, VT_UINT32, False},
		LayoutElement{4, 0, 2, VT_FLOAT32, False},
	};
	GraphicsPipelineStateCreateInfo ci;
	ci.PSODesc.Name = "Enhanced terrain";
	ci.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;
	auto& gp = ci.GraphicsPipeline;
	gp.NumRenderTargets = 1;
	gp.RTVFormats[0] = dgfx->swapChain->GetDesc().ColorBufferFormat;
	gp.DSVFormat = dgfx->swapChain->GetDesc().DepthBufferFormat;
	gp.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	// As on D3D11, where the terrain is drawn after the scene: depth state of mesh drawing
	// (LESS_EQUAL with writes), no blending, back faces culled.
	gp.RasterizerDesc.CullMode = CULL_MODE_BACK;
	gp.DepthStencilDesc.DepthEnable = True;
	gp.DepthStencilDesc.DepthWriteEnable = True;
	gp.DepthStencilDesc.DepthFunc = COMPARISON_FUNC_LESS_EQUAL;
	gp.BlendDesc.RenderTargets[0].BlendEnable = False;
	gp.InputLayout.LayoutElements = layout;
	gp.InputLayout.NumElements = (Uint32)std::size(layout);
	ci.pVS = vs;
	ci.pPS = ps;
	IPipelineResourceSignature* signatures[] = { _impl->signature };
	ci.ppResourceSignatures = signatures;
	ci.ResourceSignaturesCount = 1;
	device->CreateGraphicsPipelineState(ci, &_impl->pipeline);
	if (!_impl->pipeline)
		ferr("Diligent: could not create the enhanced terrain pipeline.");
}

DiligentEnhancedTerrainRenderer::~DiligentEnhancedTerrainRenderer()
{
	delete texcache;
	delete _impl;
}

void DiligentEnhancedTerrainRenderer::render()
{
	DiligentGfx* dgfx = (DiligentGfx*)gfx;
	IDeviceContext* ctx = dgfx->ctx;

	gfx->BeginMapDrawing();

	constexpr float tilesize = 5.0f;
	Vector3 sunNormal = terrain->sunVector.normal();

	// camera space bounding box
	const Vector3& camstart = camera->position;
	Vector3 camend = camera->position + camera->direction * camera->farDist;
	float farheight = std::tan(0.9f) * camera->farDist;
	float farwidth = farheight * camera->aspect;
	Vector3 camside = camera->direction.cross(Vector3(0, 1, 0)).normal();
	Vector3 campup = camside.cross(camera->direction).normal();
	Vector3 farleft = camend + camside * farwidth;
	Vector3 farright = camend - camside * farwidth;
	Vector3 farup = camend + campup * farheight;
	Vector3 fardown = camend - campup * farheight;
	float bbx1, bbz1, bbx2, bbz2;
	std::tie(bbx1, bbx2) = std::minmax({ camstart.x, farleft.x, farright.x, farup.x, fardown.x });
	std::tie(bbz1, bbz2) = std::minmax({ camstart.z, farleft.z, farright.z, farup.z, fardown.z });
	auto clamp = [](auto val, auto low, auto high) {return std::max(low, std::min(high, val)); };
	int tlsx = clamp((int)std::floor(bbx1 / tilesize) + (int)terrain->edge, 0, (int)terrain->width - 1);
	int tlsz = clamp((int)std::floor(bbz1 / tilesize) + (int)terrain->edge, 0, (int)terrain->height - 1);
	int tlex = clamp((int)std::ceil(bbx2 / tilesize) + (int)terrain->edge, 0, (int)terrain->width - 1);
	int tlez = clamp((int)std::ceil(bbz2 / tilesize) + (int)terrain->edge, 0, (int)terrain->height - 1);

	gfx->BeginBatchDrawing();

	void* mapped = nullptr;
	ctx->MapBuffer(_impl->sunBuffer, MAP_WRITE, MAP_FLAG_DISCARD, mapped);
	char* sbdata = (char*)mapped;
	*(Vector3*)(sbdata + 0) = sunNormal;
	*(int*)(sbdata + 12) = m_bumpOn ? 1 : 0;
	ctx->UnmapBuffer(_impl->sunBuffer, MAP_WRITE);

	for (int z = tlsz; z <= tlez; z++) {
		for (int x = tlsx; x <= tlex; x++) {
			int lx = x - terrain->edge, lz = z - terrain->edge;
			Vector3 pp((lx + 0.5f) * tilesize, terrain->getVertex(x, z), (lz + 0.5f) * tilesize);
			Vector3 camcenter = camera->position + camera->direction * 125.0f;
			pp += (camcenter - pp).normal() * tilesize * sqrtf(2.0f);
			Vector3 ttpp = pp.transformScreenCoords(camera->sceneMatrix);
			if (ttpp.x < -1 || ttpp.x > 1 || ttpp.y < -1 || ttpp.y > 1 || ttpp.z < -1 || ttpp.z > 1)
				continue;
			const Terrain::Tile* tile = terrain->getTile(x, z);
			_impl->tilesPerTex[_impl->textures.at(tile->texture)].push_back(tile);
		}
	}

	ctx->SetPipelineState(_impl->pipeline);
	IBuffer* vb = _impl->trnVertexBuffer;
	const Uint64 vbOffset = 0;
	ctx->SetVertexBuffers(0, 1, &vb, &vbOffset, RESOURCE_STATE_TRANSITION_MODE_TRANSITION, SET_VERTEX_BUFFERS_FLAG_RESET);
	auto& indices = _impl->indices;
	for (auto& pack : _impl->tilesPerTex) {
		if (pack.second.empty())
			continue;
		auto& srb = _impl->bindings[pack.first];
		if (!srb) {
			_impl->signature->CreateShaderResourceBinding(&srb, true);
			srb->GetVariableByName(SHADER_TYPE_PIXEL, "inpTexture")->Set(dgfx->GetTextureView(pack.first.first));
			srb->GetVariableByName(SHADER_TYPE_PIXEL, "normalTexture")->Set(dgfx->GetTextureView(pack.first.second));
		}
		ctx->CommitShaderResources(srb, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
		// One draw per texture and up to 8192 tiles (the D3D11 renderer splits every 1024
		// indices), so that the indices fit in the stream buffer.
		constexpr size_t maxTilesPerDraw = 8192;
		for (size_t first = 0; first < pack.second.size(); first += maxTilesPerDraw) {
			const size_t last = std::min(pack.second.size(), first + maxTilesPerDraw);
			indices.clear();
			for (size_t t = first; t < last; t++) {
				const TerrainTile* tile = pack.second[t];
				uint32_t firstindex = 4 * (tile->z * terrain->width + tile->x);
				for (const int& c : { 0,3,1,1,3,2 })
					indices.push_back(firstindex + c);
			}
			uint64_t ibOffset = 0;
			IBuffer* ib = dgfx->StreamIndexData(indices.data(), indices.size() * sizeof(uint32_t), ibOffset);
			ctx->SetIndexBuffer(ib, ibOffset, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
			ctx->DrawIndexed(DrawIndexedAttribs{ (Uint32)indices.size(), VT_UINT32, DRAW_FLAG_VERIFY_ALL });
		}
	}

	// ----- Lakes -----
	gfx->BeginBatchDrawing();
	gfx->NoTexture(0);
	auto* lakeBatch = _impl->lakeBatch.get();
	lakeBatch->begin();
	gfx->BeginLakeDrawing();
	const uint32_t color = (terrain->fogColor & 0xFFFFFF) | 0x80000000;
	for (int z = tlsz; z <= tlez; z++) {
		for (int x = tlsx; x <= tlex; x++) {
			const TerrainTile* tile = terrain->getTile(x, z);
			if (tile && tile->fullOfWater) {
				unsigned int x = tile->x;
				unsigned int z = tile->z;
				int lx = x - terrain->edge, lz = z - terrain->edge;

				// is water tile visible on screen?
				Vector3 pp((lx + 0.5f) * tilesize, tile->waterLevel, (lz + 0.5f) * tilesize);
				Vector3 camcenter = camera->position + camera->direction * 125.0f;
				pp += (camcenter - pp).normal() * tilesize * sqrtf(2.0f);
				Vector3 ttpp = pp.transformScreenCoords(camera->sceneMatrix);
				if (ttpp.x < -1 || ttpp.x > 1 || ttpp.y < -1 || ttpp.y > 1 || ttpp.z < -1 || ttpp.z > 1)
					continue;

				batchVertex* outvert; uint16_t* outindices; unsigned int firstindex;
				lakeBatch->next(4, 6, &outvert, &outindices, &firstindex);
				outvert[0].x = lx * tilesize; outvert[0].y = tile->waterLevel; outvert[0].z = lz * tilesize;
				outvert[0].color = color; outvert[0].u = 0.0f; outvert[0].v = 0.0f;
				outvert[1].x = (lx + 1) * tilesize; outvert[1].y = tile->waterLevel; outvert[1].z = lz * tilesize;
				outvert[1].color = color; outvert[1].u = 0.0f; outvert[1].v = 0.0f;
				outvert[2].x = (lx + 1) * tilesize; outvert[2].y = tile->waterLevel; outvert[2].z = (lz + 1) * tilesize;
				outvert[2].color = color; outvert[2].u = 0.0f; outvert[2].v = 0.0f;
				outvert[3].x = lx * tilesize; outvert[3].y = tile->waterLevel; outvert[3].z = (lz + 1) * tilesize;
				outvert[3].color = color; outvert[3].u = 0.0f; outvert[3].v = 0.0f;
				uint16_t* oix = outindices;
				for (const int& c : { 0,3,1,1,3,2 })
					*(oix++) = firstindex + c;
			}
		}
	}
	lakeBatch->flush();
	lakeBatch->end();

	for (auto& pack : _impl->tilesPerTex)
		pack.second.clear();
}

