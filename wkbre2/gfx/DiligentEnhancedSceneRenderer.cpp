// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

// Port of the former D3D11EnhancedSceneRenderer to Diligent. The shader and the batching (instances of
// one model and material, up to 16 per draw, split on animation frame, colour and flags) are
// the same. Normals are still packed as 10:10:10:2, but unpacked in the vertex shader because
// Diligent has no such vertex format.

#include "DiligentEnhancedSceneRenderer.h"
#include "renderer_diligent.h"
#include "../Model.h"
#include "../scene.h"
#include "../Camera.h"
#include "../util/util.h"
#include <algorithm>
#include <iterator>
#include <map>
#include <set>
#include <tuple>
#include <vector>

using namespace Diligent;

static constexpr uint32_t Vec3ToR10G10B10A2(const Vector3& vec) {
	uint32_t res = (uint32_t)(vec.x * 1023.0f);
	res |= (uint32_t)(vec.y * 1023.0f) << 10;
	res |= (uint32_t)(vec.z * 1023.0f) << 20;
	return res;
}
static constexpr uint32_t NormalToR10G10B10A2(const Vector3& vec) {
	return Vec3ToR10G10B10A2((vec + Vector3(1, 1, 1)) * 0.5f);
}

static const char esShaderCode[] = R"---(
Texture2D inpTexture;
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

cbuffer SceneBuffer
{
	int EntityFlags;
	float EntityTime;
};

struct VS_OUTPUT
{
	float4 Pos : SV_POSITION;
	float3 Norm: NORMAL;
	float4 Color : COLOR0;
	float2 Texcoord : TEXCOORD0;
	float Fog : FOG;
};

// R10G10B10A2_UNORM
float3 UnpackNormal(uint p)
{
	return float3(p & 1023, (p >> 10) & 1023, (p >> 20) & 1023) / 1023.0;
}

// The instance transform comes in as its four columns (the D3D11 input layout had a
// float4x4 TRANSFORM input, which column-major packing fills by columns).
VS_OUTPUT Shade(float3 Pos, float3 Normal, float2 Texcoord, float4 T0, float4 T1, float4 T2, float4 T3)
{
	float4x4 EntityTransform = transpose(float4x4(T0, T1, T2, T3));
	VS_OUTPUT output = (VS_OUTPUT)0;
	float4x4 trans = mul(EntityTransform, Transform);
	output.Pos = mul(float4(Pos, 1), trans);
	if(EntityFlags & 1) {
		output.Norm = float3(0,0,0);
		output.Color = float4(1,1,1,1);
	}
	else {
		output.Norm = normalize(mul(float4(Normal * 2 - float3(1,1,1), 0), EntityTransform).xyz);
		output.Color = float4( clamp((dot(output.Norm, normalize(SunDirection)) + 1.0) * 0.5, 160.0/255.0, 1.0) * float3(1,1,1), 1 );
	}
	output.Texcoord = Texcoord;
	output.Fog = clamp((output.Pos.w-FogStartDist) / (FogEndDist-FogStartDist), 0, 1);
	return output;
}

VS_OUTPUT VS(float3 Pos : ATTRIB0, uint Normal : ATTRIB1, float2 Texcoord : ATTRIB2,
	float4 T0 : ATTRIB3, float4 T1 : ATTRIB4, float4 T2 : ATTRIB5, float4 T3 : ATTRIB6)
{
	return Shade(Pos, UnpackNormal(Normal), Texcoord, T0, T1, T2, T3);
}

VS_OUTPUT VS_Anim(float3 Pos1 : ATTRIB0, uint Normal1 : ATTRIB1, float2 Texcoord : ATTRIB2, float3 Pos2 : ATTRIB3, uint Normal2 : ATTRIB4,
	float4 T0 : ATTRIB5, float4 T1 : ATTRIB6, float4 T2 : ATTRIB7, float4 T3 : ATTRIB8)
{
	return Shade(lerp(Pos1, Pos2, EntityTime), lerp(UnpackNormal(Normal1), UnpackNormal(Normal2), EntityTime), Texcoord, T0, T1, T2, T3);
}

float4 PS(VS_OUTPUT input) : SV_Target
{
	float4 tex = inpTexture.Sample(inpSampler, input.Texcoord);
	return lerp(input.Color * tex, FogColor, input.Fog);
}

static const float ALPHA_REF = 0.94;
float4 PS_Alpha(VS_OUTPUT input) : SV_Target
{
	float4 tex = inpTexture.Sample(inpSampler, input.Texcoord);
	if(tex.a < ALPHA_REF) discard;
	return lerp(input.Color * tex, FogColor, input.Fog);
}
)---";

static RefCntAutoPtr<IBuffer> CreateImmutableBuffer(IRenderDevice* device, const char* name, BIND_FLAGS bind, const void* data, uint64_t size)
{
	BufferDesc desc;
	desc.Name = name;
	desc.Size = std::max<uint64_t>(size, 4);
	desc.BindFlags = bind;
	desc.Usage = USAGE_IMMUTABLE;
	BufferData init{ data, size };
	RefCntAutoPtr<IBuffer> buf;
	device->CreateBuffer(desc, size ? &init : nullptr, &buf);
	if (!buf)
		ferr("Diligent: could not create buffer \"%s\"", name);
	return buf;
}

struct DilMesh
{
	RefCntAutoPtr<IBuffer> vertices;
	RefCntAutoPtr<IBuffer> normals;
	std::vector<RefCntAutoPtr<IBuffer>> texcoords;
	RefCntAutoPtr<IBuffer> indices;
	uint32_t totVertices = 0, totIndices = 0;
	static DilMesh create(StaticModel* model, IRenderDevice* device) {
		DilMesh enh;
		model->prepare();
		const Mesh& mesh = model->mesh;
		for (auto& mat : mesh.groupIndices)
			enh.totVertices += mat.size();

		std::vector<Vector3> pVertices;
		pVertices.reserve(enh.totVertices);
		for (auto& mat : mesh.groupIndices)
			for (auto& grp : mat)
				pVertices.push_back(*(const Vector3*)(mesh.vertices.data() + 3 * grp.vertex));
		enh.vertices = CreateImmutableBuffer(device, "Mesh positions", BIND_VERTEX_BUFFER, pVertices.data(), pVertices.size() * 12);

		std::vector<uint32_t> pNormals;
		pNormals.reserve(enh.totVertices);
		for (auto& mat : mesh.groupIndices)
			for (auto& grp : mat)
				pNormals.push_back(NormalToR10G10B10A2(Mesh::s_normalTable[mesh.normals[grp.normal]].normal()));
		enh.normals = CreateImmutableBuffer(device, "Mesh normals", BIND_VERTEX_BUFFER, pNormals.data(), pNormals.size() * 4);

		enh.texcoords.resize(mesh.uvLists.size());
		std::vector<float> pUvs;
		for (size_t i = 0; i < mesh.uvLists.size(); i++) {
			auto& uvlist = mesh.uvLists[i];
			pUvs.clear();
			pUvs.reserve(enh.totVertices * 2);
			for (auto& mat : mesh.groupIndices)
				for (auto& grp : mat)
					pUvs.insert(pUvs.end(), { uvlist[2 * grp.uv], uvlist[2 * grp.uv + 1] });
			enh.texcoords[i] = CreateImmutableBuffer(device, "Mesh UVs", BIND_VERTEX_BUFFER, pUvs.data(), pUvs.size() * 4);
		}

		std::vector<uint16_t> pIndices;
		for (auto& mat : mesh.polyLists[0].groups) {
			for (auto& tri : mat.tupleIndex)
				pIndices.insert(pIndices.end(), tri.begin(), tri.end());
			enh.totIndices += 3 * mat.tupleIndex.size();
		}
		enh.indices = CreateImmutableBuffer(device, "Mesh indices", BIND_INDEX_BUFFER, pIndices.data(), pIndices.size() * 2);
		return enh;
	}
};

struct DilAnim
{
	std::vector<uint32_t> animTimes;
	std::vector<RefCntAutoPtr<IBuffer>> animVertices;
	std::vector<RefCntAutoPtr<IBuffer>> animNormals;
	static DilAnim create(AnimatedModel* model, IRenderDevice* device) {
		DilAnim enh;
		model->prepare();
		const auto& anim = model->anim;
		const auto staticModel = model->getStaticModel();
		staticModel->prepare();
		const auto& mesh = staticModel->mesh;
		std::set<uint32_t> animTimesSet;
		for (int c = 0; c < 3; ++c)
			animTimesSet.insert(anim.coords[c].frameTimes.begin(), anim.coords[c].frameTimes.end());
		enh.animTimes.assign(animTimesSet.begin(), animTimesSet.end());
		enh.animTimes.back() -= 1; // prevents last frame from becoming first frame
		enh.animVertices.resize(enh.animTimes.size());
		enh.animNormals.resize(enh.animTimes.size());
		for (size_t frame = 0; frame < enh.animTimes.size(); ++frame) {
			const Vector3* intVertices = (const Vector3*)model->interpolate(enh.animTimes[frame]);
			const Vector3* intNormals = model->interpolateNormals(enh.animTimes[frame]);
			std::vector<Vector3> pVertices; std::vector<uint32_t> pNormals;
			for (auto& mat : mesh.groupIndices) {
				for (auto& grp : mat) {
					pVertices.push_back(intVertices[grp.vertex]);
					pNormals.push_back(NormalToR10G10B10A2(intNormals[grp.normal].normal()));
				}
			}
			enh.animVertices[frame] = CreateImmutableBuffer(device, "Anim positions", BIND_VERTEX_BUFFER, pVertices.data(), pVertices.size() * 12);
			enh.animNormals[frame] = CreateImmutableBuffer(device, "Anim normals", BIND_VERTEX_BUFFER, pNormals.data(), pNormals.size() * 4);
		}
		return enh;
	}
};

struct DiligentEnhancedSceneRenderer::Impl {
	std::map<StaticModel*, DilMesh> meshMap;
	std::map<AnimatedModel*, DilAnim> animMap;
	RefCntAutoPtr<IPipelineResourceSignature> signature;
	// [animated][alpha test]
	RefCntAutoPtr<IPipelineState> pipelines[2][2];
	std::map<texture, RefCntAutoPtr<IShaderResourceBinding>> textureBindings;
	RefCntAutoPtr<IBuffer> sceneConstBuffer, sunConstBuffer;
	static constexpr size_t MAX_INSTANCES = 16;
};

DiligentEnhancedSceneRenderer::~DiligentEnhancedSceneRenderer()
{
	delete _impl;
}

void DiligentEnhancedSceneRenderer::init()
{
	_impl = new Impl;
	DiligentGfx* dgfx = (DiligentGfx*)gfx;
	IRenderDevice* device = dgfx->device;

	BufferDesc cbDesc;
	cbDesc.Size = 16;
	cbDesc.BindFlags = BIND_UNIFORM_BUFFER;
	cbDesc.Usage = USAGE_DYNAMIC;
	cbDesc.CPUAccessFlags = CPU_ACCESS_WRITE;
	cbDesc.Name = "Scene constants";
	device->CreateBuffer(cbDesc, nullptr, &_impl->sceneConstBuffer);
	cbDesc.Name = "Scene sun";
	device->CreateBuffer(cbDesc, nullptr, &_impl->sunConstBuffer);

	const PipelineResourceDesc resources[] = {
		{ SHADER_TYPE_VERTEX, "TransformBuffer", 1, SHADER_RESOURCE_TYPE_CONSTANT_BUFFER, SHADER_RESOURCE_VARIABLE_TYPE_STATIC },
		{ SHADER_TYPE_VERTEX | SHADER_TYPE_PIXEL, "FogBuffer", 1, SHADER_RESOURCE_TYPE_CONSTANT_BUFFER, SHADER_RESOURCE_VARIABLE_TYPE_STATIC },
		{ SHADER_TYPE_VERTEX, "SunBuffer", 1, SHADER_RESOURCE_TYPE_CONSTANT_BUFFER, SHADER_RESOURCE_VARIABLE_TYPE_STATIC },
		{ SHADER_TYPE_VERTEX, "SceneBuffer", 1, SHADER_RESOURCE_TYPE_CONSTANT_BUFFER, SHADER_RESOURCE_VARIABLE_TYPE_STATIC },
		{ SHADER_TYPE_PIXEL, "inpTexture", 1, SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
	};
	SamplerDesc sampler; // D3D11's default sampler: trilinear, clamped
	sampler.AddressU = sampler.AddressV = sampler.AddressW = TEXTURE_ADDRESS_CLAMP;
	const ImmutableSamplerDesc samplers[] = { { SHADER_TYPE_PIXEL, "inpSampler", sampler } };
	PipelineResourceSignatureDesc sigDesc;
	sigDesc.Name = "Enhanced scene resources";
	sigDesc.Resources = resources;
	sigDesc.NumResources = (Uint32)std::size(resources);
	sigDesc.ImmutableSamplers = samplers;
	sigDesc.NumImmutableSamplers = (Uint32)std::size(samplers);
	device->CreatePipelineResourceSignature(sigDesc, &_impl->signature);
	if (!_impl->signature)
		ferr("Diligent: could not create the enhanced scene resource signature.");
	_impl->signature->GetStaticVariableByName(SHADER_TYPE_VERTEX, "TransformBuffer")->Set(dgfx->transformBuffer);
	_impl->signature->GetStaticVariableByName(SHADER_TYPE_VERTEX, "FogBuffer")->Set(dgfx->fogBuffer);
	_impl->signature->GetStaticVariableByName(SHADER_TYPE_PIXEL, "FogBuffer")->Set(dgfx->fogBuffer);
	_impl->signature->GetStaticVariableByName(SHADER_TYPE_VERTEX, "SunBuffer")->Set(_impl->sunConstBuffer);
	_impl->signature->GetStaticVariableByName(SHADER_TYPE_VERTEX, "SceneBuffer")->Set(_impl->sceneConstBuffer);

	auto createShader = [&](SHADER_TYPE type, const char* entryPoint) {
		ShaderCreateInfo sci;
		sci.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
		sci.Source = esShaderCode;
		sci.SourceLength = sizeof(esShaderCode) - 1;
		sci.EntryPoint = entryPoint;
		sci.Desc.ShaderType = type;
		sci.Desc.Name = entryPoint;
		RefCntAutoPtr<IShader> shader;
		device->CreateShader(sci, &shader);
		if (!shader)
			ferr("Diligent: failed to compile enhanced scene shader %s", entryPoint);
		return shader;
	};
	RefCntAutoPtr<IShader> vertexShaders[2] = { createShader(SHADER_TYPE_VERTEX, "VS"), createShader(SHADER_TYPE_VERTEX, "VS_Anim") };
	RefCntAutoPtr<IShader> pixelShaders[2] = { createShader(SHADER_TYPE_PIXEL, "PS"), createShader(SHADER_TYPE_PIXEL, "PS_Alpha") };

	// Buffer slots: static mesh = positions, normals, UVs, instances;
	// animated mesh = positions, normals, UVs, next positions, next normals, instances.
	auto instanceElements = [](std::vector<LayoutElement>& layout, Uint32 firstInput, Uint32 slot) {
		for (Uint32 i = 0; i < 4; i++)
			layout.push_back(LayoutElement{ firstInput + i, slot, 4, VT_FLOAT32, False, i * 16, 64, INPUT_ELEMENT_FREQUENCY_PER_INSTANCE, 1 });
	};
	std::vector<LayoutElement> layouts[2];
	layouts[0] = { LayoutElement{0, 0, 3, VT_FLOAT32, False}, LayoutElement{1, 1, 1, VT_UINT32, False}, LayoutElement{2, 2, 2, VT_FLOAT32, False} };
	instanceElements(layouts[0], 3, 3);
	layouts[1] = { LayoutElement{0, 0, 3, VT_FLOAT32, False}, LayoutElement{1, 1, 1, VT_UINT32, False}, LayoutElement{2, 2, 2, VT_FLOAT32, False},
		LayoutElement{3, 3, 3, VT_FLOAT32, False}, LayoutElement{4, 4, 1, VT_UINT32, False} };
	instanceElements(layouts[1], 5, 5);

	for (int anim = 0; anim < 2; anim++) {
		for (int alpha = 0; alpha < 2; alpha++) {
			GraphicsPipelineStateCreateInfo ci;
			ci.PSODesc.Name = "Enhanced scene";
			ci.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;
			auto& gp = ci.GraphicsPipeline;
			gp.NumRenderTargets = 1;
			gp.RTVFormats[0] = dgfx->swapChain->GetDesc().ColorBufferFormat;
			gp.DSVFormat = dgfx->swapChain->GetDesc().DepthBufferFormat;
			gp.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
			// As after IRenderer::BeginMeshDrawing() on D3D11: no blending, depth test
			// LESS_EQUAL with writes, default rasterizer (back faces culled).
			gp.RasterizerDesc.CullMode = CULL_MODE_BACK;
			gp.DepthStencilDesc.DepthEnable = True;
			gp.DepthStencilDesc.DepthWriteEnable = True;
			gp.DepthStencilDesc.DepthFunc = COMPARISON_FUNC_LESS_EQUAL;
			gp.BlendDesc.RenderTargets[0].BlendEnable = False;
			gp.InputLayout.LayoutElements = layouts[anim].data();
			gp.InputLayout.NumElements = (Uint32)layouts[anim].size();
			ci.pVS = vertexShaders[anim];
			ci.pPS = pixelShaders[alpha];
			IPipelineResourceSignature* signatures[] = { _impl->signature };
			ci.ppResourceSignatures = signatures;
			ci.ResourceSignaturesCount = 1;
			device->CreateGraphicsPipelineState(ci, &_impl->pipelines[anim][alpha]);
			if (!_impl->pipelines[anim][alpha])
				ferr("Diligent: could not create the enhanced scene pipeline.");
		}
	}
}

void DiligentEnhancedSceneRenderer::render()
{
	DiligentGfx* dgfx = (DiligentGfx*)gfx;
	IRenderDevice* device = dgfx->device;
	IDeviceContext* ctx = dgfx->ctx;

	// Keeps the IRenderer's state the same as after the D3D11 renderer (lakes and particles
	// are drawn through IRenderer afterwards).
	gfx->BeginMeshDrawing();
	gfx->BeginBatchDrawing();

	void* mapped = nullptr;
	ctx->MapBuffer(_impl->sunConstBuffer, MAP_WRITE, MAP_FLAG_DISCARD, mapped);
	memset(mapped, 0, 16);
	*(Vector3*)mapped = scene->sunDirection.normal();
	ctx->UnmapBuffer(_impl->sunConstBuffer, MAP_WRITE);

	auto getBinding = [&](texture tex) -> IShaderResourceBinding* {
		auto& srb = _impl->textureBindings[tex];
		if (!srb) {
			_impl->signature->CreateShaderResourceBinding(&srb, true);
			srb->GetVariableByName(SHADER_TYPE_PIXEL, "inpTexture")->Set(dgfx->GetTextureView(tex));
		}
		return srb;
	};

	std::vector<Matrix> instTransforms;
	for (const auto& it : scene->matInsts) {
		const Material& mat = scene->modelCache->getMaterial(it.first.first);
		const int alpha = mat.alphaTest ? 1 : 0;
		IShaderResourceBinding* srb = getBinding(it.second.tex);

		auto* model = it.first.second;
		auto* staticModel = model->getStaticModel();

		auto mt = _impl->meshMap.find(staticModel);
		if (mt == _impl->meshMap.end())
			mt = _impl->meshMap.emplace(staticModel, DilMesh::create(staticModel, device)).first;
		const DilMesh* enhMesh = &mt->second;

		const DilAnim* enhAnim = nullptr;
		if (model != staticModel) {
			auto* animModel = (AnimatedModel*)model;
			auto at = _impl->animMap.find(animModel);
			if (at == _impl->animMap.end())
				at = _impl->animMap.emplace(animModel, DilAnim::create(animModel, device)).first;
			enhAnim = &at->second;
		}

		int prevFrame = -1;
		float prevAnimLerp = -1.0f;
		int prevColor = -1;
		uint32_t prevFlags = -1;
		instTransforms.clear();

		auto flush = [&]() {
			if (instTransforms.empty()) return;
			uint64_t instanceOffset = 0;
			IBuffer* instanceBuffer = dgfx->StreamVertexData(instTransforms.data(), instTransforms.size() * 64, instanceOffset);

			void* mapped = nullptr;
			ctx->MapBuffer(_impl->sceneConstBuffer, MAP_WRITE, MAP_FLAG_DISCARD, mapped);
			uint8_t* sceneBufferBytes = (uint8_t*)mapped;
			memset(sceneBufferBytes, 0, 16);
			*(uint32_t*)(sceneBufferBytes + 0) = prevFlags;
			*(float*)(sceneBufferBytes + 4) = prevAnimLerp;
			ctx->UnmapBuffer(_impl->sceneConstBuffer, MAP_WRITE);

			ctx->SetPipelineState(_impl->pipelines[enhAnim ? 1 : 0][alpha]);
			ctx->CommitShaderResources(srb, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
			if (enhAnim) {
				IBuffer* buffers[6] = { enhAnim->animVertices[prevFrame], enhAnim->animNormals[prevFrame], enhMesh->texcoords[prevColor],
					enhAnim->animVertices[prevFrame + 1], enhAnim->animNormals[prevFrame + 1], instanceBuffer };
				const Uint64 offsets[6] = { 0, 0, 0, 0, 0, instanceOffset };
				ctx->SetVertexBuffers(0, 6, buffers, offsets, RESOURCE_STATE_TRANSITION_MODE_TRANSITION, SET_VERTEX_BUFFERS_FLAG_RESET);
			}
			else {
				IBuffer* buffers[4] = { enhMesh->vertices, enhMesh->normals, enhMesh->texcoords[prevColor], instanceBuffer };
				const Uint64 offsets[4] = { 0, 0, 0, instanceOffset };
				ctx->SetVertexBuffers(0, 4, buffers, offsets, RESOURCE_STATE_TRANSITION_MODE_TRANSITION, SET_VERTEX_BUFFERS_FLAG_RESET);
			}
			ctx->SetIndexBuffer(enhMesh->indices, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

			PolygonList& polylist = staticModel->mesh.polyLists[0];
			uint32_t startIndex = 0, startVertex = 0;
			for (size_t g = 0; g < polylist.groups.size(); g++) {
				uint32_t numIndices = 3 * polylist.groups[g].tupleIndex.size();
				uint32_t numVertices = staticModel->mesh.groupIndices[g].size();
				if (staticModel->matIds[g] == it.first.first && numIndices > 0)
					ctx->DrawIndexed(DrawIndexedAttribs{ numIndices, VT_UINT16, DRAW_FLAG_VERIFY_ALL, (Uint32)instTransforms.size(), startIndex, startVertex });
				startIndex += numIndices;
				startVertex += numVertices;
			}

			instTransforms.clear();
		};

		for (const SceneEntity* ent : it.second.list) {
			size_t frame = 0;
			float animLerp = 0.0f;
			if (enhAnim) {
				auto* animModel = (AnimatedModel*)model;
				uint32_t animtime = ent->animTime % animModel->anim.duration;
				for (; frame < enhAnim->animTimes.size() - 1; frame++)
					if (enhAnim->animTimes[frame + 1] >= animtime)
						break;
				animLerp = (float)(animtime - enhAnim->animTimes[frame]) / (float)(enhAnim->animTimes[frame + 1] - enhAnim->animTimes[frame]);
			}
			int actualColor = ent->color % enhMesh->texcoords.size();

			if (instTransforms.size() >= _impl->MAX_INSTANCES || std::tie(prevFrame, prevAnimLerp, prevColor, prevFlags) != std::tie(frame, animLerp, actualColor, ent->flags)) {
				flush();
			}
			std::tie(prevFrame, prevAnimLerp, prevColor, prevFlags) = std::tie(frame, animLerp, actualColor, ent->flags);
			instTransforms.push_back(ent->transform.getTranspose());
		}
		flush();
	}
}

