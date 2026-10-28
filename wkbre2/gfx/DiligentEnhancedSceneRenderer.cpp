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
#include "../settings.h"
#include "RenderSettings.h"
#include <nlohmann/json.hpp>
#include <cmath>
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
Texture2D cloudTexture;
SamplerState cloudSampler;

cbuffer TransformBuffer
{
	matrix Transform;
};

cbuffer FogBuffer
{
	float4 FogColor;
	float FogStartDist;
	float FogEndDist;
	float4 FogCamera; // camera position: the fog grows with the distance to it, as in the original
	float4 Grade; // exposure, brightness, contrast, saturation
	float4 EdgeFog; // playable area width and depth, edge fog start and opaque distance outside it
};

// Fog of a world position: the distance fog, or the map-edge fog where that is stronger. The edge
// fog follows the signed distance to the playable area's border (negative inside; the larger of x
// and z, so that it follows the square band of the map's edge): it may start inside the playable
// area and is opaque before the map mesh ends.
float FogAmount(float3 worldPos, float distanceToCamera)
{
	float distanceFog = clamp((distanceToCamera - FogStartDist) / (FogEndDist - FogStartDist), 0, 1);
	float2 outside = max(-worldPos.xz, worldPos.xz - EdgeFog.xy);
	float edgeFog = saturate((max(outside.x, outside.y) - EdgeFog.z) / max(EdgeFog.w - EdgeFog.z, 1e-3));
	return max(distanceFog, edgeFog);
}

// Colour grading of the 3D scene (the settings window's exposure/brightness/contrast/saturation)
float3 GradeColour(float3 c)
{
	c *= Grade.x;
	c = lerp(dot(c, float3(0.299, 0.587, 0.114)).xxx, c, Grade.w);
	return saturate((c - 0.5) * Grade.z + 0.5 + Grade.y);
}

cbuffer SunBuffer
{
	float3 SunDirection;
	int BumpOn;
	float4 SunColour;
	float4 CloudParams; // UV offset, UV per world unit, on
	float4 MeshLighting; // ambient, diffuse factor
};

cbuffer SceneBuffer
{
	uint VertexBase; // animated meshes: the material group's first vertex in a frame
};

// Animated meshes: every key frame's positions (3 floats each) and packed normals, one frame
// after the other (float arrays, as float3 arrays have a different stride on Vulkan)
StructuredBuffer<float> AnimPositions;
StructuredBuffer<uint> AnimNormals;

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
// float4x4 TRANSFORM input, which column-major packing fills by columns). Anim: the instance's
// first vertex in its key frame and in the next one, the blend between them, and its flags.
VS_OUTPUT Shade(float3 Pos, float3 Normal, float2 Texcoord, float4 T0, float4 T1, float4 T2, float4 T3, uint EntityFlags)
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
		// The original's mesh lighting (docs/re/lighting.md): per vertex,
		// ambient + diffuse * max(N.L, 0), clamped, times the cloud shadow at the object's
		// position (which scales both terms), then multiplied with the texture.
		output.Norm = normalize(mul(float4(Normal * 2 - float3(1,1,1), 0), EntityTransform).xyz);
		float lambert = max(dot(output.Norm, normalize(SunDirection)), 0);
		float cloud = 1;
		if (CloudParams.w != 0)
			cloud = cloudTexture.SampleLevel(cloudSampler, EntityTransform[3].xz * CloudParams.z + CloudParams.xy, 0).r;
		output.Color = float4(cloud * min(MeshLighting.x + MeshLighting.y * SunColour.rgb * lambert, 1), 1);
	}
	if (EntityFlags & 8) // SEFLAG_BLOCKED
		output.Color.rgb *= float3(1.0, 0.25, 0.25);
	if (EntityFlags & 16) // SEFLAG_PLACEABLE
		output.Color.rgb *= float3(0.35, 1.0, 0.35);
	output.Texcoord = Texcoord;
	float3 worldPos = mul(float4(Pos, 1), EntityTransform).xyz;
	output.Fog = FogAmount(worldPos, distance(worldPos, FogCamera.xyz));
	return output;
}

VS_OUTPUT VS(float3 Pos : ATTRIB0, uint Normal : ATTRIB1, float2 Texcoord : ATTRIB2,
	float4 T0 : ATTRIB3, float4 T1 : ATTRIB4, float4 T2 : ATTRIB5, float4 T3 : ATTRIB6, uint4 Anim : ATTRIB7)
{
	return Shade(Pos, UnpackNormal(Normal), Texcoord, T0, T1, T2, T3, Anim.w);
}

// Every instance at its own key frame: drawn with base vertex 0, the vertex id counts within the
// material group (VertexBase is where the group starts in a frame)
VS_OUTPUT VS_Anim(float2 Texcoord : ATTRIB0, float4 T0 : ATTRIB1, float4 T1 : ATTRIB2, float4 T2 : ATTRIB3, float4 T3 : ATTRIB4,
	uint4 Anim : ATTRIB5, uint VertexId : SV_VertexID)
{
	uint v1 = Anim.x + VertexBase + VertexId, v2 = Anim.y + VertexBase + VertexId;
	float3 pos1 = float3(AnimPositions[3 * v1], AnimPositions[3 * v1 + 1], AnimPositions[3 * v1 + 2]);
	float3 pos2 = float3(AnimPositions[3 * v2], AnimPositions[3 * v2 + 1], AnimPositions[3 * v2 + 2]);
	float t = asfloat(Anim.z);
	return Shade(lerp(pos1, pos2, t), lerp(UnpackNormal(AnimNormals[v1]), UnpackNormal(AnimNormals[v2]), t), Texcoord, T0, T1, T2, T3, Anim.w);
}

float4 PS(VS_OUTPUT input) : SV_Target
{
	float4 tex = inpTexture.Sample(inpSampler, input.Texcoord);
	float4 c = lerp(input.Color * tex, FogColor, input.Fog);
	return float4(GradeColour(c.rgb), c.a);
}

static const float ALPHA_REF = 0.94;
float4 PS_Alpha(VS_OUTPUT input) : SV_Target
{
	float4 tex = inpTexture.Sample(inpSampler, input.Texcoord);
	if(tex.a < ALPHA_REF) discard;
	float4 c = lerp(input.Color * tex, FogColor, input.Fog);
	return float4(GradeColour(c.rgb), c.a);
}

// Static shadow casters with alpha-tested materials
void PS_ShadowAlpha(VS_OUTPUT input)
{
	if(inpTexture.Sample(inpSampler, input.Texcoord).a < ALPHA_REF) discard;
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

// A buffer the vertex shader reads as a StructuredBuffer of 4-byte elements
static RefCntAutoPtr<IBuffer> CreateStructuredBuffer(IRenderDevice* device, const char* name, const void* data, uint64_t size)
{
	BufferDesc desc;
	desc.Name = name;
	desc.Size = std::max<uint64_t>(size, 4);
	desc.BindFlags = BIND_SHADER_RESOURCE;
	desc.Usage = USAGE_IMMUTABLE;
	desc.Mode = BUFFER_MODE_STRUCTURED;
	desc.ElementByteStride = 4;
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

// All keyframes of an animation in one position and one normal buffer, one frame after the
// other: creating a buffer per keyframe made the first view of a model cost up to ~100 ms. The
// vertex shader reads them (AnimPositions, AnimNormals), so that every instance of a draw can be
// at its own key frame.
struct DilAnim
{
	std::vector<uint32_t> animTimes;
	RefCntAutoPtr<IBuffer> animVertices, animNormals;
	RefCntAutoPtr<IShaderResourceBinding> binding; // the two buffers, made at the first draw
	uint64_t frameVertexBytes = 0, frameNormalBytes = 0;
	uint32_t frameVertices() const { return (uint32_t)(frameVertexBytes / 12); }
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
		std::vector<Vector3> pVertices; std::vector<uint32_t> pNormals;
		size_t perFrame = 0;
		for (auto& mat : mesh.groupIndices)
			perFrame += mat.size();
		pVertices.reserve(perFrame * enh.animTimes.size());
		pNormals.reserve(perFrame * enh.animTimes.size());
		for (size_t frame = 0; frame < enh.animTimes.size(); ++frame) {
			const Vector3* intVertices = (const Vector3*)model->interpolate(enh.animTimes[frame]);
			const Vector3* intNormals = model->interpolateNormals(enh.animTimes[frame]);
			for (auto& mat : mesh.groupIndices) {
				for (auto& grp : mat) {
					pVertices.push_back(intVertices[grp.vertex]);
					pNormals.push_back(NormalToR10G10B10A2(intNormals[grp.normal].normal()));
				}
			}
		}
		enh.frameVertexBytes = perFrame * 12;
		enh.frameNormalBytes = perFrame * 4;
		enh.animVertices = CreateStructuredBuffer(device, "Anim positions", pVertices.data(), pVertices.size() * 12);
		enh.animNormals = CreateStructuredBuffer(device, "Anim normals", pNormals.data(), pNormals.size() * 4);
		return enh;
	}
};


// One instance of a draw (the instance vertex buffer)
struct Instance {
	Matrix transform; // transposed: its columns
	uint32_t frameStart = 0, nextFrameStart = 0; // the key frames' first vertex in the animation buffers
	float blend = 0.0f;
	uint32_t flags = 0;
};
static_assert(sizeof(Instance) == 80);

// Half the side of the square around the camera's view centre covered by the static shadow map
static constexpr float STATIC_SHADOW_RADIUS = 200.0f;
static constexpr float STATIC_SHADOW_DEPTH = 1000.0f;

struct DiligentEnhancedSceneRenderer::Impl {
	std::map<StaticModel*, DilMesh> meshMap;
	std::map<AnimatedModel*, DilAnim> animMap;
	// Textures (one binding per texture), and the frame's environment (the cloud map)
	RefCntAutoPtr<IPipelineResourceSignature> signature, envSignature, animSignature;
	// [animated][alpha test]
	RefCntAutoPtr<IPipelineState> pipelines[2][2], shadowPipelines[2][2];
	std::map<texture, RefCntAutoPtr<IShaderResourceBinding>> textureBindings;
	RefCntAutoPtr<IShaderResourceBinding> envBinding;
	ITextureView* envCloudMap = nullptr;
	RefCntAutoPtr<IBuffer> sceneConstBuffer, sunConstBuffer;
	// instances per draw: every instance has its own key frame, so a draw takes all the instances
	// of a model, material and team colour (80 bytes each, streamed)
	static constexpr size_t MAX_INSTANCES = 1024;
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
	cbDesc.Size = 32;
	cbDesc.BindFlags = BIND_UNIFORM_BUFFER;
	cbDesc.Usage = USAGE_DYNAMIC;
	cbDesc.CPUAccessFlags = CPU_ACCESS_WRITE;
	cbDesc.Name = "Scene constants";
	device->CreateBuffer(cbDesc, nullptr, &_impl->sceneConstBuffer);
	cbDesc.Name = "Scene sun";
	cbDesc.Size = 64;
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

	const PipelineResourceDesc envResources[] = {
		{ SHADER_TYPE_VERTEX, "cloudTexture", 1, SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
	};
	SamplerDesc cloudSampler; // the cloud map tiles the map
	cloudSampler.AddressU = cloudSampler.AddressV = cloudSampler.AddressW = TEXTURE_ADDRESS_WRAP;
	const ImmutableSamplerDesc envSamplers[] = { { SHADER_TYPE_VERTEX, "cloudSampler", cloudSampler } };
	PipelineResourceSignatureDesc envSigDesc;
	envSigDesc.Name = "Enhanced scene environment";
	envSigDesc.Resources = envResources;
	envSigDesc.NumResources = (Uint32)std::size(envResources);
	envSigDesc.ImmutableSamplers = envSamplers;
	envSigDesc.NumImmutableSamplers = (Uint32)std::size(envSamplers);
	envSigDesc.BindingIndex = 1;
	device->CreatePipelineResourceSignature(envSigDesc, &_impl->envSignature);
	if (!_impl->envSignature)
		ferr("Diligent: could not create the enhanced scene environment signature.");

	const PipelineResourceDesc animResources[] = {
		{ SHADER_TYPE_VERTEX, "AnimPositions", 1, SHADER_RESOURCE_TYPE_BUFFER_SRV, SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
		{ SHADER_TYPE_VERTEX, "AnimNormals", 1, SHADER_RESOURCE_TYPE_BUFFER_SRV, SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
	};
	PipelineResourceSignatureDesc animSigDesc;
	animSigDesc.Name = "Enhanced scene animation";
	animSigDesc.Resources = animResources;
	animSigDesc.NumResources = (Uint32)std::size(animResources);
	animSigDesc.BindingIndex = 2;
	device->CreatePipelineResourceSignature(animSigDesc, &_impl->animSignature);
	if (!_impl->animSignature)
		ferr("Diligent: could not create the enhanced scene animation signature.");

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
	RefCntAutoPtr<IShader> shadowAlphaShader = createShader(SHADER_TYPE_PIXEL, "PS_ShadowAlpha");

	// Buffer slots: static mesh = positions, normals, UVs, instances; animated mesh = UVs,
	// instances (its positions and normals come from the animation buffers). An instance is its
	// transform (four columns) and four uints: key frame start, next key frame start, blend, flags.
	auto instanceElements = [](std::vector<LayoutElement>& layout, Uint32 firstInput, Uint32 slot) {
		for (Uint32 i = 0; i < 4; i++)
			layout.push_back(LayoutElement{ firstInput + i, slot, 4, VT_FLOAT32, False, i * 16, sizeof(Instance), INPUT_ELEMENT_FREQUENCY_PER_INSTANCE, 1 });
		layout.push_back(LayoutElement{ firstInput + 4, slot, 4, VT_UINT32, False, 64, sizeof(Instance), INPUT_ELEMENT_FREQUENCY_PER_INSTANCE, 1 });
	};
	std::vector<LayoutElement> layouts[2];
	layouts[0] = { LayoutElement{0, 0, 3, VT_FLOAT32, False}, LayoutElement{1, 1, 1, VT_UINT32, False}, LayoutElement{2, 2, 2, VT_FLOAT32, False} };
	instanceElements(layouts[0], 3, 3);
	layouts[1] = { LayoutElement{0, 0, 2, VT_FLOAT32, False} };
	instanceElements(layouts[1], 1, 1);

	for (int shadow = 0; shadow < 2; shadow++) {
		for (int anim = 0; anim < 2; anim++) {
			for (int alpha = 0; alpha < 2; alpha++) {
				GraphicsPipelineStateCreateInfo ci;
				ci.PSODesc.Name = shadow ? "Enhanced scene static shadow" : "Enhanced scene";
				ci.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;
				auto& gp = ci.GraphicsPipeline;
				if (shadow) {
					// Depth only, into the static shadow map; both faces, as the meshes
					// aren't all closed.
					gp.NumRenderTargets = 0;
					gp.DSVFormat = TEX_FORMAT_D32_FLOAT;
					gp.RasterizerDesc.CullMode = CULL_MODE_NONE;
				}
				else {
					gp.NumRenderTargets = 1;
					gp.RTVFormats[0] = dgfx->swapChain->GetDesc().ColorBufferFormat;
					gp.DSVFormat = dgfx->swapChain->GetDesc().DepthBufferFormat;
					// As after IRenderer::BeginMeshDrawing() on D3D11: no blending, depth test
					// LESS_EQUAL with writes, default rasterizer (back faces culled).
					gp.RasterizerDesc.CullMode = CULL_MODE_BACK;
				}
				gp.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
				gp.DepthStencilDesc.DepthEnable = True;
				gp.DepthStencilDesc.DepthWriteEnable = True;
				gp.DepthStencilDesc.DepthFunc = COMPARISON_FUNC_LESS_EQUAL;
				gp.BlendDesc.RenderTargets[0].BlendEnable = False;
				gp.InputLayout.LayoutElements = layouts[anim].data();
				gp.InputLayout.NumElements = (Uint32)layouts[anim].size();
				ci.pVS = vertexShaders[anim];
				ci.pPS = shadow ? (alpha ? shadowAlphaShader.RawPtr() : nullptr) : pixelShaders[alpha].RawPtr();
				IPipelineResourceSignature* signatures[] = { _impl->signature, _impl->envSignature, _impl->animSignature };
				ci.ppResourceSignatures = signatures;
				ci.ResourceSignaturesCount = anim ? 3 : 2;
				auto& pso = shadow ? _impl->shadowPipelines[anim][alpha] : _impl->pipelines[anim][alpha];
				device->CreateGraphicsPipelineState(ci, &pso);
				if (!pso)
					ferr("Diligent: could not create the enhanced scene pipeline.");
			}
		}
	}
}

void DiligentEnhancedSceneRenderer::prewarm(Model* model)
{
	IRenderDevice* device = ((DiligentGfx*)gfx)->device;
	StaticModel* staticModel = model->getStaticModel();
	if (_impl->meshMap.find(staticModel) == _impl->meshMap.end())
		_impl->meshMap.emplace(staticModel, DilMesh::create(staticModel, device));
	if (model != staticModel) {
		auto* animModel = (AnimatedModel*)model;
		if (_impl->animMap.find(animModel) == _impl->animMap.end())
			_impl->animMap.emplace(animModel, DilAnim::create(animModel, device));
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

	auto& shadows = dgfx->shadows;
	void* mapped = nullptr;
	ctx->MapBuffer(_impl->sunConstBuffer, MAP_WRITE, MAP_FLAG_DISCARD, mapped);
	memset(mapped, 0, 64);
	*(Vector3*)mapped = scene->sunDirection.normal();
	float* sunColour = (float*)mapped + 4;
	sunColour[0] = ((scene->sunColor >> 16) & 255) / 255.0f;
	sunColour[1] = ((scene->sunColor >> 8) & 255) / 255.0f;
	sunColour[2] = (scene->sunColor & 255) / 255.0f;
	sunColour[3] = 1.0f;
	float* cloudParams = (float*)mapped + 8;
	cloudParams[0] = std::fmod(shadows.time * CLOUD_SCROLL_U, 1.0f);
	cloudParams[1] = std::fmod(shadows.time * CLOUD_SCROLL_V, 1.0f);
	cloudParams[2] = CLOUD_UV_PER_UNIT;
	cloudParams[3] = (shadows.cloudShadows && shadows.cloudMap) ? 1.0f : 0.0f;
	float* meshLighting = (float*)mapped + 12;
	meshLighting[0] = g_renderSettings.meshAmbient;
	meshLighting[1] = g_renderSettings.meshDiffuse;
	ctx->UnmapBuffer(_impl->sunConstBuffer, MAP_WRITE);

	if (!_impl->envBinding || _impl->envCloudMap != shadows.cloudMap.RawPtr()) {
		_impl->envBinding.Release();
		_impl->envSignature->CreateShaderResourceBinding(&_impl->envBinding, true);
		_impl->envCloudMap = shadows.cloudMap.RawPtr();
		_impl->envBinding->GetVariableByName(SHADER_TYPE_VERTEX, "cloudTexture")->Set(shadows.cloudMap ? shadows.cloudMap.RawPtr() : dgfx->GetTextureView(nullptr));
	}

	auto getBinding = [&](texture tex) -> IShaderResourceBinding* {
		auto& srb = _impl->textureBindings[tex];
		if (!srb) {
			_impl->signature->CreateShaderResourceBinding(&srb, true);
			srb->GetVariableByName(SHADER_TYPE_PIXEL, "inpTexture")->Set(dgfx->GetTextureView(tex));
		}
		return srb;
	};

	std::vector<Instance> instances;
	// Draws the scene's meshes, or only the static shadow casters into the bound shadow map.
	// Returns whether anything was drawn.
	auto drawMeshes = [&](bool shadowPass) {
		bool drawn = false;
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

			if (enhAnim && !enhAnim->binding) {
				DilAnim* a = const_cast<DilAnim*>(enhAnim);
				_impl->animSignature->CreateShaderResourceBinding(&a->binding, true);
				a->binding->GetVariableByName(SHADER_TYPE_VERTEX, "AnimPositions")->Set(a->animVertices->GetDefaultView(BUFFER_VIEW_SHADER_RESOURCE));
				a->binding->GetVariableByName(SHADER_TYPE_VERTEX, "AnimNormals")->Set(a->animNormals->GetDefaultView(BUFFER_VIEW_SHADER_RESOURCE));
			}

			// one draw per team colour (the UV set): the instances keep their own key frame and flags
			int prevColor = -1;
			instances.clear();

			auto flush = [&]() {
				if (instances.empty()) return;
				uint64_t instanceOffset = 0;
				IBuffer* instanceBuffer = dgfx->StreamVertexData(instances.data(), instances.size() * sizeof(Instance), instanceOffset);

				ctx->SetPipelineState((shadowPass ? _impl->shadowPipelines : _impl->pipelines)[enhAnim ? 1 : 0][alpha]);
				drawn = true;
				ctx->CommitShaderResources(srb, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
				ctx->CommitShaderResources(_impl->envBinding, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
				if (enhAnim)
					ctx->CommitShaderResources(enhAnim->binding, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
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
					if (staticModel->matIds[g] == it.first.first && numIndices > 0) {
						if (enhAnim) {
							// base vertex 0 (whether SV_VertexID includes it differs between APIs): the
							// group's start goes in VertexBase, and its UVs from their offset
							void* mapped = nullptr;
							ctx->MapBuffer(_impl->sceneConstBuffer, MAP_WRITE, MAP_FLAG_DISCARD, mapped);
							memset(mapped, 0, 16);
							*(uint32_t*)mapped = startVertex;
							ctx->UnmapBuffer(_impl->sceneConstBuffer, MAP_WRITE);
							IBuffer* buffers[2] = { enhMesh->texcoords[prevColor], instanceBuffer };
							const Uint64 offsets[2] = { (Uint64)startVertex * 8, instanceOffset };
							ctx->SetVertexBuffers(0, 2, buffers, offsets, RESOURCE_STATE_TRANSITION_MODE_TRANSITION, SET_VERTEX_BUFFERS_FLAG_RESET);
							ctx->DrawIndexed(DrawIndexedAttribs{ numIndices, VT_UINT16, DRAW_FLAG_VERIFY_ALL, (Uint32)instances.size(), startIndex, 0 });
						}
						else
							ctx->DrawIndexed(DrawIndexedAttribs{ numIndices, VT_UINT16, DRAW_FLAG_VERIFY_ALL, (Uint32)instances.size(), startIndex, startVertex });
					}
					startIndex += numIndices;
					startVertex += numVertices;
				}

				instances.clear();
			};

			for (const SceneEntity* ent : it.second.list) {
				if (shadowPass && !(ent->flags & (SceneEntity::SEFLAG_STATIC_SHADOW | SceneEntity::SEFLAG_DYNAMIC_SHADOW)))
					continue;
				Instance inst;
				inst.transform = ent->transform.getTranspose();
				inst.flags = ent->flags;
				if (enhAnim && enhAnim->animTimes.size() >= 2) {
					auto* animModel = (AnimatedModel*)model;
					uint32_t animtime = ent->animTime % animModel->anim.duration;
					size_t frame = 0;
					for (; frame < enhAnim->animTimes.size() - 1; frame++)
						if (enhAnim->animTimes[frame + 1] >= animtime)
							break;
					frame = std::min(frame, enhAnim->animTimes.size() - 2);
					inst.blend = std::clamp((float)((int64_t)animtime - enhAnim->animTimes[frame]) / (float)(enhAnim->animTimes[frame + 1] - enhAnim->animTimes[frame]), 0.0f, 1.0f);
					inst.frameStart = (uint32_t)frame * enhAnim->frameVertices();
					inst.nextFrameStart = (uint32_t)(frame + 1) * enhAnim->frameVertices();
				}
				const int actualColor = ent->color % enhMesh->texcoords.size();
				if (instances.size() >= _impl->MAX_INSTANCES || actualColor != prevColor)
					flush();
				prevColor = actualColor;
				instances.push_back(inst);
			}
			flush();
		}
		return drawn;
	};

	// Static shadows: the casters seen from the sun, orthographic over a square around the
	// point the camera looks at, snapped to whole texels so that the edges don't shimmer.
	shadows.staticShadowValid = false;
	if (shadows.staticShadows && shadows.staticShadowDSV) {
		const Vector3 sun = scene->sunDirection.normal();
		const Vector3 target = camera->position + camera->direction.normal() * STATIC_SHADOW_RADIUS * 0.75f;
		const Vector3 up = std::abs(sun.y) > 0.99f ? Vector3(0, 0, 1) : Vector3(0, 1, 0);
		Matrix view = Matrix::getLHLookAtViewMatrix(target + sun * (STATIC_SHADOW_DEPTH * 0.5f), target, up);
		const float texel = 2.0f * STATIC_SHADOW_RADIUS / shadows.staticShadowDSV->GetTexture()->GetDesc().Width;
		view._41 = std::round(view._41 / texel) * texel;
		view._42 = std::round(view._42 / texel) * texel;
		Matrix ortho = Matrix::getZeroMatrix();
		ortho._11 = ortho._22 = 1.0f / STATIC_SHADOW_RADIUS;
		ortho._33 = 1.0f / STATIC_SHADOW_DEPTH;
		ortho._44 = 1.0f;
		const Matrix lightMatrix = view * ortho;

		ITextureView* dsv = shadows.staticShadowDSV;
		ctx->SetRenderTargets(0, nullptr, dsv, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
		ctx->SetViewports(1, nullptr, 0, 0);
		ctx->ClearDepthStencil(dsv, CLEAR_DEPTH_FLAG, 1.0f, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
		gfx->SetTransformMatrix(&lightMatrix);
		shadows.staticShadowValid = drawMeshes(true);
		shadows.staticShadowMatrix = lightMatrix;
		gfx->SetTransformMatrix(&camera->sceneMatrix);
		dgfx->RestoreRenderTargets();
	}

	drawMeshes(false);
}

