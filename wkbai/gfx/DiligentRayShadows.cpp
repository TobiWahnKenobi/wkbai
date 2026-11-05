// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#include "DiligentRayShadows.h"
#include "../util/util.h"
#include "../terrain.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iterator>

using namespace Diligent;

// The alpha test of a candidate hit on non-opaque geometry (shared by the shaders)
#define ALPHA_TEST_CODE \
"StructuredBuffer<uint2> rtGeometries;\n" \
"StructuredBuffer<float2> rtUVs;\n" \
"Texture2D alphaTextures[128];\n" \
"SamplerState sAlpha;\n" \
"bool AlphaSolid(uint geometry, uint prim, float2 bary)\n" \
"{\n" \
"	uint2 g = rtGeometries[geometry];\n" \
"	if (g.y == 0)\n" \
"		return true;\n" \
"	uint b = g.x + prim * 3;\n" \
"	float2 uv = rtUVs[b] * (1 - bary.x - bary.y) + rtUVs[b + 1] * bary.x + rtUVs[b + 2] * bary.y;\n" \
"	return alphaTextures[NonUniformResourceIndex(g.y - 1)].SampleLevel(sAlpha, uv, 0).a >= 0.94;\n" \
"}\n"

// a ray query's loop: the alpha test on its non-opaque candidates
#define ALPHA_TEST_LOOP(rq) \
	"	while (" #rq ".Proceed()) {\n" \
	"		if (" #rq ".CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE && AlphaSolid(" #rq ".CandidateInstanceID() + " #rq ".CandidateGeometryIndex(),\n" \
	"				" #rq ".CandidatePrimitiveIndex(), " #rq ".CandidateTriangleBarycentrics()))\n" \
	"			" #rq ".CommitNonOpaqueTriangleHit();\n" \
	"	}\n"

// The shadow mask: per pixel the world position from the depth, its normal from the
// neighbours' positions, one ray from just off the surface towards a point of the sun's disc
static const char maskShaderCode[] = ALPHA_TEST_CODE R"---(
RaytracingAccelerationStructure tlas;
Texture2D<float> tDepth;

cbuffer RtBuffer
{
	float4 Proj;         // P11, P22, P33, P43
	float4 Texel;        // 1 / width, 1 / height, the frame (the jitter), the sun's angular radius
	float4 SunDir;       // towards the sun
	float4x4 InverseView; // view -> world (row vectors)
};

// the renderers' fog (renderer_diligent.cpp writeFogBuffer): the shadows fade with it, as the
// composite darkens the fogged colour (the terrain far beyond the map's edge is all fog)
cbuffer FogBuffer
{
	float4 FogColor;
	float FogStartDist;
	float FogEndDist;
	float4 FogCamera;
	float4 Grade;
	float4 EdgeFog; // playable area width and depth, edge fog start and opaque distance outside it
};

float FogAmount(float3 worldPos, float distanceToCamera)
{
	float distanceFog = clamp((distanceToCamera - FogStartDist) / (FogEndDist - FogStartDist), 0, 1);
	float2 outside = max(-worldPos.xz, worldPos.xz - EdgeFog.xy);
	float edgeFog = saturate((max(outside.x, outside.y) - EdgeFog.z) / max(EdgeFog.w - EdgeFog.z, 1e-3));
	return max(distanceFog, edgeFog);
}

struct VS_OUTPUT
{
	float4 Pos : SV_POSITION;
	float2 Uv : TEXCOORD0;
};

VS_OUTPUT VS(uint id : SV_VertexID)
{
	VS_OUTPUT o;
	float2 p = float2((id << 1) & 2, id & 2) * 2 - 1;
	o.Pos = float4(p, 0, 1);
	o.Uv = float2(p.x * 0.5 + 0.5, 0.5 - p.y * 0.5);
	return o;
}

float3 WorldAt(float2 uv, float d)
{
	float z = Proj.w / (d - Proj.z);
	float2 ndc = float2(uv.x * 2 - 1, 1 - uv.y * 2);
	return mul(float4(ndc.x * z / Proj.x, ndc.y * z / Proj.y, z, 1), InverseView).xyz;
}

float PS(VS_OUTPUT input) : SV_Target
{
	int2 px = int2(input.Pos.xy);
	float d = tDepth.Load(int3(px, 0));
	if (d >= 0.99999)
		return 1;
	float3 p = WorldAt(input.Uv, d);
	// the normal from the nearer neighbour on each axis (no smearing over silhouettes)
	float3 px1 = WorldAt(input.Uv + float2(Texel.x, 0), tDepth.Load(int3(px + int2(1, 0), 0)));
	float3 px0 = WorldAt(input.Uv - float2(Texel.x, 0), tDepth.Load(int3(px - int2(1, 0), 0)));
	float3 py1 = WorldAt(input.Uv + float2(0, Texel.y), tDepth.Load(int3(px + int2(0, 1), 0)));
	float3 py0 = WorldAt(input.Uv - float2(0, Texel.y), tDepth.Load(int3(px - int2(0, 1), 0)));
	float3 dx = distance(px1, p) < distance(px0, p) ? px1 - p : p - px0;
	float3 dy = distance(py1, p) < distance(py0, p) ? py1 - p : p - py0;
	float3 n = normalize(cross(dy, dx));
	float3 camPos = InverseView[3].xyz;
	if (dot(n, camPos - p) < 0)
		n = -n;
	float3 l = normalize(SunDir.xyz);
	if (dot(n, l) <= 0.01)
		return 1; // turned away from the sun: its shading is dark already
	float fog = FogAmount(p, distance(p, FogCamera.xyz));
	if (fog >= 0.999)
		return 1;
	// a point of the sun's disc, moved every frame (interleaved gradient noise)
	float2 q = input.Pos.xy + 5.588238 * fmod(Texel.z, 64);
	float r1 = frac(52.9829189 * frac(dot(q, float2(0.06711056, 0.00583715))));
	float r2 = frac(52.9829189 * frac(dot(q.yx + 17.3, float2(0.06711056, 0.00583715))));
	float3 t = normalize(cross(l, abs(l.y) < 0.9 ? float3(0, 1, 0) : float3(1, 0, 0)));
	float3 b = cross(l, t);
	float a = r1 * 6.2831853, rr = sqrt(r2) * Texel.w;
	float3 dir = normalize(l + (t * cos(a) + b * sin(a)) * rr);
	float dist = length(p - camPos);
	RayDesc ray;
	ray.Origin = p + n * (0.05 + dist * 0.0008);
	ray.TMin = 0.01;
	ray.Direction = dir;
	ray.TMax = 2000; // the terrain's long shadows at a low sun
	// (back faces culled: only the terrain's, the models' instances turn the culling off)
	RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES | RAY_FLAG_CULL_BACK_FACING_TRIANGLES> rq;
	rq.TraceRayInline(tlas, RAY_FLAG_NONE, 0xFF, ray);
)---" ALPHA_TEST_LOOP(rq) R"---(
	return rq.CommittedStatus() == COMMITTED_TRIANGLE_HIT ? fog : 1;
}
)---";

// The water's reflections: per pixel of the flat water surface the mirror ray (the view
// reflected about the up axis), the nearest building, tree or unit it hits. Its colour: the
// frame before the water where the hit point is on the screen (the same surface where its
// depth agrees; else mostly the dark silhouette, as the side facing the water is turned away
// from the camera and the sun); off the screen the silhouette alone. Faded out towards the
// ray's reach and the screen's edges.
static const char reflectShaderCode[] = ALPHA_TEST_CODE R"---(
RaytracingAccelerationStructure tlas;
Texture2D tScene;
Texture2D<float> tDepth;
SamplerState sLinear;

cbuffer TransformBuffer
{
	matrix Transform;
};

cbuffer ReflectBuffer
{
	float4 Camera;   // position; w: the rays' reach
	float4 ZTerms;   // the projection's m22, m32 (view z = m32 / (depth - m22))
	float4 Fallback; // the silhouette's colour
};

struct VS_OUTPUT
{
	float4 Pos : SV_POSITION;
	float3 World : TEXCOORD0;
};

VS_OUTPUT VS(float3 Pos : ATTRIB0, float Depth : ATTRIB1)
{
	VS_OUTPUT o;
	o.Pos = mul(float4(Pos, 1), Transform);
	o.World = Pos;
	return o;
}

float4 PS(VS_OUTPUT input) : SV_Target
{
	float3 v = normalize(input.World - Camera.xyz);
	float3 r = float3(v.x, abs(v.y), v.z);
	RayDesc ray;
	ray.Origin = input.World + float3(0, 0.05, 0);
	ray.TMin = 0.02;
	ray.Direction = r;
	ray.TMax = Camera.w;
	RayQuery<RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> rq;
	rq.TraceRayInline(tlas, RAY_FLAG_NONE, 0x01, ray); // the models only, not the terrain
)---" ALPHA_TEST_LOOP(rq) R"---(
	if (rq.CommittedStatus() != COMMITTED_TRIANGLE_HIT)
		return float4(0, 0, 0, 0);
	float t = rq.CommittedRayT();
	float3 hit = ray.Origin + r * t;
	float3 colour = Fallback.rgb;
	float4 clip = mul(float4(hit, 1), Transform);
	if (clip.w > 0.05) {
		float2 uv = clip.xy / clip.w * float2(0.5, -0.5) + 0.5;
		if (all(uv >= 0) && all(uv <= 1)) {
			float2 size;
			tDepth.GetDimensions(size.x, size.y);
			float sceneZ = ZTerms.y / (tDepth.Load(int3(uv * size, 0)) - ZTerms.x);
			float same = abs(sceneZ - clip.w) < 1.5 + clip.w * 0.03 ? 1 : 0.35;
			float2 edge = min(uv, 1 - uv);
			colour = lerp(Fallback.rgb, tScene.SampleLevel(sLinear, uv, 0).rgb, same * saturate(min(edge.x, edge.y) * 16));
		}
	}
	return float4(colour, saturate((Camera.w - t) / (Camera.w * 0.3)));
}
)---";

bool DiligentRayShadows::Supported(IRenderDevice* device)
{
	const auto& info = device->GetDeviceInfo();
	const auto& rt = device->GetAdapterInfo().RayTracing;
	return info.Features.RayTracing == DEVICE_FEATURE_STATE_ENABLED && (rt.CapFlags & RAY_TRACING_CAP_FLAG_INLINE_RAY_TRACING) != 0;
}

DiligentRayShadows::DiligentRayShadows(IRenderDevice* device) : device(device)
{
	BufferDesc cb;
	cb.Name = "Ray-traced shadows";
	cb.Size = 112;
	cb.BindFlags = BIND_UNIFORM_BUFFER;
	cb.Usage = USAGE_DYNAMIC;
	cb.CPUAccessFlags = CPU_ACCESS_WRITE;
	device->CreateBuffer(cb, nullptr, &constants);
	{
		// the alpha textures' unused slots
		const uint32_t white = 0xFFFFFFFFu;
		TextureDesc wd;
		wd.Name = "Ray tracing: no alpha texture";
		wd.Type = RESOURCE_DIM_TEX_2D;
		wd.Width = wd.Height = 1;
		wd.Format = TEX_FORMAT_RGBA8_UNORM;
		wd.BindFlags = BIND_SHADER_RESOURCE;
		wd.Usage = USAGE_IMMUTABLE;
		TextureSubResData sub{ &white, 4 };
		TextureData init{ &sub, 1 };
		device->CreateTexture(wd, &init, &whiteTexture);
	}

	const PipelineResourceDesc resources[] = {
		{ SHADER_TYPE_PIXEL, "RtBuffer", 1, SHADER_RESOURCE_TYPE_CONSTANT_BUFFER, SHADER_RESOURCE_VARIABLE_TYPE_STATIC },
		{ SHADER_TYPE_PIXEL, "FogBuffer", 1, SHADER_RESOURCE_TYPE_CONSTANT_BUFFER, SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
		{ SHADER_TYPE_PIXEL, "tDepth", 1, SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
		{ SHADER_TYPE_PIXEL, "tlas", 1, SHADER_RESOURCE_TYPE_ACCEL_STRUCT, SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
		{ SHADER_TYPE_PIXEL, "rtGeometries", 1, SHADER_RESOURCE_TYPE_BUFFER_SRV, SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
		{ SHADER_TYPE_PIXEL, "rtUVs", 1, SHADER_RESOURCE_TYPE_BUFFER_SRV, SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
		{ SHADER_TYPE_PIXEL, "alphaTextures", MAX_ALPHA_TEXTURES, SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
	};
	SamplerDesc alphaSampler; // the leaves' textures tile
	alphaSampler.MinFilter = alphaSampler.MagFilter = alphaSampler.MipFilter = FILTER_TYPE_POINT;
	alphaSampler.AddressU = alphaSampler.AddressV = alphaSampler.AddressW = TEXTURE_ADDRESS_WRAP;
	const ImmutableSamplerDesc maskSamplers[] = { { SHADER_TYPE_PIXEL, "sAlpha", alphaSampler } };
	PipelineResourceSignatureDesc sig;
	sig.Name = "Ray-traced shadows";
	sig.Resources = resources;
	sig.NumResources = (Uint32)std::size(resources);
	sig.ImmutableSamplers = maskSamplers;
	sig.NumImmutableSamplers = 1;
	device->CreatePipelineResourceSignature(sig, &signature);
	if (!signature)
		ferr("Diligent: could not create the ray-traced shadow signature.");
	signature->GetStaticVariableByName(SHADER_TYPE_PIXEL, "RtBuffer")->Set(constants);
	auto shader = [&](SHADER_TYPE type, const char* entry) {
		ShaderCreateInfo sci;
		sci.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
		sci.ShaderCompiler = SHADER_COMPILER_DXC;
		sci.HLSLVersion = ShaderVersion{ 6, 5 };
		sci.Source = maskShaderCode;
		sci.SourceLength = sizeof(maskShaderCode) - 1;
		sci.EntryPoint = entry;
		sci.Desc.ShaderType = type;
		sci.Desc.Name = "Ray-traced shadows";
		RefCntAutoPtr<IShader> s;
		device->CreateShader(sci, &s);
		if (!s)
			printf("Ray-traced shadows: could not compile %s (DXC, shader model 6.5): off\n", entry);
		return s;
	};
	RefCntAutoPtr<IShader> vs = shader(SHADER_TYPE_VERTEX, "VS"), ps = shader(SHADER_TYPE_PIXEL, "PS");
	// the water's reflections: their shaders now, the pipeline with the first depth target
	{
		auto rshader = [&](SHADER_TYPE type, const char* entry) {
			ShaderCreateInfo sci;
			sci.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
			sci.ShaderCompiler = SHADER_COMPILER_DXC;
			sci.HLSLVersion = ShaderVersion{ 6, 5 };
			sci.Source = reflectShaderCode;
			sci.SourceLength = sizeof(reflectShaderCode) - 1;
			sci.EntryPoint = entry;
			sci.Desc.ShaderType = type;
			sci.Desc.Name = "Ray-traced water reflections";
			RefCntAutoPtr<IShader> s;
			device->CreateShader(sci, &s);
			if (!s)
				printf("Ray-traced reflections: could not compile %s (DXC, shader model 6.5): off\n", entry);
			return s;
		};
		reflectVs = rshader(SHADER_TYPE_VERTEX, "VS");
		reflectPs = rshader(SHADER_TYPE_PIXEL, "PS");
		BufferDesc rcb;
		rcb.Name = "Ray-traced reflections";
		rcb.Size = 48;
		rcb.BindFlags = BIND_UNIFORM_BUFFER;
		rcb.Usage = USAGE_DYNAMIC;
		rcb.CPUAccessFlags = CPU_ACCESS_WRITE;
		device->CreateBuffer(rcb, nullptr, &reflectConstants);
		reflectOk = reflectVs && reflectPs && reflectConstants;
	}
	if (!vs || !ps)
		return;
	GraphicsPipelineStateCreateInfo ci;
	ci.PSODesc.Name = "Ray-traced shadows";
	ci.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;
	auto& gp = ci.GraphicsPipeline;
	gp.NumRenderTargets = 1;
	gp.RTVFormats[0] = TEX_FORMAT_R8_UNORM;
	gp.DSVFormat = TEX_FORMAT_UNKNOWN;
	gp.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	gp.RasterizerDesc.CullMode = CULL_MODE_NONE;
	gp.DepthStencilDesc.DepthEnable = False;
	gp.DepthStencilDesc.DepthWriteEnable = False;
	ci.pVS = vs;
	ci.pPS = ps;
	IPipelineResourceSignature* sigs[] = { signature };
	ci.ppResourceSignatures = sigs;
	ci.ResourceSignaturesCount = 1;
	device->CreateGraphicsPipelineState(ci, &pipeline);
	ok = pipeline != nullptr;
	printf("Ray-traced shadows: %s\n", ok ? "ready" : "could not create the pipeline: off");
}

IBottomLevelAS* DiligentRayShadows::blas(IDeviceContext* ctx, const void* key, uint32_t frame, IBuffer* positions,
	uint64_t positionOffset, uint32_t vertexCount, IBuffer* indices, const std::vector<Group>& groups)
{
	auto& slot = blases[{ key, frame }];
	if (slot)
		return slot;
	// the mesh's geometries for the alpha test, once per mesh (all its key frames share them)
	auto base = geometryBases.find(&groups);
	if (base == geometryBases.end()) {
		base = geometryBases.emplace(&groups, (uint32_t)(geometryRecords.size() / 2)).first;
		for (const Group& gr : groups) {
			if (gr.indexCount < 3 || gr.vertexCount == 0)
				continue;
			uint32_t texture = 0;
			if (gr.alphaTexture && gr.alphaUVs.size() >= (size_t)gr.indexCount * 2) {
				auto at = std::find(alphaTextures.begin(), alphaTextures.end(), gr.alphaTexture);
				if (at != alphaTextures.end())
					texture = (uint32_t)(at - alphaTextures.begin()) + 1;
				else if (alphaTextures.size() < MAX_ALPHA_TEXTURES) {
					alphaTextures.push_back(gr.alphaTexture);
					texture = (uint32_t)alphaTextures.size();
				}
			}
			geometryRecords.push_back(texture ? (uint32_t)(alphaUVs.size() / 2) : 0);
			geometryRecords.push_back(texture);
			if (texture)
				alphaUVs.insert(alphaUVs.end(), gr.alphaUVs.begin(), gr.alphaUVs.end());
		}
		alphaDirty = true;
	}
	const uint32_t geometryBase = base->second;
	std::vector<BLASTriangleDesc> descs;
	std::vector<BLASBuildTriangleData> data;
	std::vector<std::string> geometryNames;
	geometryNames.reserve(groups.size());
	for (size_t g = 0; g < groups.size(); g++) {
		const Group& gr = groups[g];
		if (gr.indexCount < 3 || gr.vertexCount == 0)
			continue;
		geometryNames.push_back("g" + std::to_string(g));
		BLASTriangleDesc d;
		d.GeometryName = geometryNames.back().c_str();
		d.MaxVertexCount = gr.vertexCount;
		d.VertexValueType = VT_FLOAT32;
		d.VertexComponentCount = 3;
		d.MaxPrimitiveCount = gr.indexCount / 3;
		d.IndexType = VT_UINT16;
		descs.push_back(d);
		BLASBuildTriangleData b;
		b.GeometryName = d.GeometryName;
		b.pVertexBuffer = positions;
		b.VertexOffset = positionOffset + (uint64_t)gr.firstVertex * 12;
		b.VertexStride = 12;
		b.VertexCount = gr.vertexCount;
		b.VertexValueType = VT_FLOAT32;
		b.VertexComponentCount = 3;
		b.PrimitiveCount = gr.indexCount / 3;
		b.pIndexBuffer = indices;
		b.IndexOffset = (uint64_t)gr.firstIndex * 2;
		b.IndexType = VT_UINT16;
		const bool alpha = geometryRecords[2 * (geometryBase + (uint32_t)data.size()) + 1] != 0;
		b.Flags = alpha ? RAYTRACING_GEOMETRY_FLAG_NONE : RAYTRACING_GEOMETRY_FLAG_OPAQUE;
		data.push_back(b);
	}
	if (descs.empty())
		return nullptr;
	BottomLevelASDesc desc;
	desc.Name = "Shadow caster";
	desc.pTriangles = descs.data();
	desc.TriangleCount = (Uint32)descs.size();
	desc.Flags = RAYTRACING_BUILD_AS_PREFER_FAST_TRACE;
	device->CreateBLAS(desc, &slot);
	if (!slot)
		return nullptr;
	const Uint64 need = slot->GetScratchBufferSizes().Build;
	if (!scratch || scratch->GetDesc().Size < need) {
		BufferDesc sd;
		sd.Name = "BLAS scratch";
		sd.Size = std::max<Uint64>(need, 1 << 20);
		sd.BindFlags = BIND_RAY_TRACING;
		scratch.Release();
		device->CreateBuffer(sd, nullptr, &scratch);
	}
	BuildBLASAttribs ba;
	ba.pBLAS = slot;
	ba.BLASTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
	ba.GeometryTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
	ba.pTriangleData = data.data();
	ba.TriangleDataCount = (Uint32)data.size();
	ba.pScratchBuffer = scratch;
	ba.ScratchBufferTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
	ctx->BuildBLAS(ba);
	blasGeometryBase[slot.RawPtr()] = geometryBase;
	return slot;
}

void DiligentRayShadows::add(IBottomLevelAS* b, const Matrix& transform)
{
	if (b)
		instances.push_back({ b, transform, blasGeometryBase[b] });
}

void DiligentRayShadows::build(IDeviceContext* ctx)
{
	lastInstanceCount = instances.size();
	built = false;
	const bool withTerrain = terrainBlas != nullptr;
	if (instances.empty() && !withTerrain)
		return;
	const Uint32 count = (Uint32)instances.size() + (withTerrain ? 1 : 0);
	if (count > tlasCapacity) {
		tlasCapacity = std::max<Uint32>(count * 2, 1024);
		TopLevelASDesc td;
		td.Name = "Shadow casters";
		td.MaxInstanceCount = tlasCapacity;
		td.Flags = RAYTRACING_BUILD_AS_PREFER_FAST_BUILD;
		tlas.Release();
		device->CreateTLAS(td, &tlas);
		BufferDesc ib;
		ib.Name = "TLAS instances";
		ib.Size = (Uint64)TLAS_INSTANCE_DATA_SIZE * tlasCapacity;
		ib.BindFlags = BIND_RAY_TRACING;
		instanceBuffer.Release();
		device->CreateBuffer(ib, nullptr, &instanceBuffer);
		BufferDesc sd;
		sd.Name = "TLAS scratch";
		sd.Size = tlas->GetScratchBufferSizes().Build;
		sd.BindFlags = BIND_RAY_TRACING;
		tlasScratch.Release();
		device->CreateBuffer(sd, nullptr, &tlasScratch);
		boundTlas = nullptr;
	}
	while (names.size() < count)
		names.push_back("i" + std::to_string(names.size()));
	std::vector<TLASBuildInstanceData> data(count);
	for (Uint32 i = 0; i < (Uint32)instances.size(); i++) {
		TLASBuildInstanceData& d = data[i];
		d.InstanceName = names[i].c_str();
		d.pBLAS = instances[i].blas;
		const Matrix& m = instances[i].transform;
		for (int r = 0; r < 3; r++) {
			for (int c = 0; c < 3; c++)
				d.Transform.data[r][c] = m.m[c][r];
			d.Transform.data[r][3] = m.m[3][r];
		}
		d.Mask = 0x01;
		d.CustomId = instances[i].geometryBase;
		d.Flags = RAYTRACING_INSTANCE_TRIANGLE_FACING_CULL_DISABLE; // leaves and walls seen from either side
	}
	if (withTerrain) {
		// in world space already; opaque, so its CustomId isn't read
		TLASBuildInstanceData& d = data[count - 1];
		d.InstanceName = names[count - 1].c_str();
		d.pBLAS = terrainBlas;
		d.Transform = InstanceMatrix{};
		d.Mask = 0x02;
		d.CustomId = 0;
		d.Flags = RAYTRACING_INSTANCE_NONE;
	}
	BuildTLASAttribs ta;
	ta.pTLAS = tlas;
	ta.TLASTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
	ta.BLASTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
	ta.pInstances = data.data();
	ta.InstanceCount = count;
	ta.pInstanceBuffer = instanceBuffer;
	ta.InstanceBufferTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
	ta.pScratchBuffer = tlasScratch;
	ta.ScratchBufferTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
	ta.BindingMode = HIT_GROUP_BINDING_MODE_PER_TLAS;
	ctx->BuildTLAS(ta);
	uploadAlpha(ctx);
	built = true;
}

void DiligentRayShadows::setTerrain(IDeviceContext* ctx, const Terrain* terrain, uint64_t version)
{
	if (!terrain) {
		terrainBlas.Release();
		terrainKey = nullptr;
		return;
	}
	if (terrain == terrainKey && version == terrainVersion && terrainBlas)
		return;
	terrainKey = terrain;
	terrainVersion = version;
	terrainBlas.Release();
	// the vertices as the terrain renderer places them: (x - edge, z - edge) tiles of 5 units
	const int w = terrain->width, h = terrain->height;
	const uint32_t vertexCount = (uint32_t)(w + 1) * (h + 1);
	std::vector<float> positions((size_t)vertexCount * 3);
	for (int z = 0; z <= h; z++)
		for (int x = 0; x <= w; x++) {
			float* p = &positions[((size_t)z * (w + 1) + x) * 3];
			p[0] = (x - terrain->edge) * 5.0f;
			p[1] = terrain->getVertex(x, z);
			p[2] = (z - terrain->edge) * 5.0f;
		}
	// per tile its corners 0 (x, z), 1 (x + 1, z), 2 (x + 1, z + 1), 3 (x, z + 1) as the terrain
	// renderer's 0 3 1, 1 3 2: clockwise seen from above, so the upper side is the front
	std::vector<uint32_t> indices;
	indices.reserve((size_t)w * h * 6);
	for (int z = 0; z < h; z++)
		for (int x = 0; x < w; x++) {
			const uint32_t c0 = z * (w + 1) + x, c1 = c0 + 1, c3 = c0 + (w + 1), c2 = c3 + 1;
			for (uint32_t c : { c0, c3, c1, c1, c3, c2 })
				indices.push_back(c);
		}
	auto make = [&](const char* name, const void* data, uint64_t bytes, RefCntAutoPtr<IBuffer>& buffer) {
		BufferDesc bd;
		bd.Name = name;
		bd.Size = bytes;
		bd.BindFlags = BIND_RAY_TRACING;
		bd.Usage = USAGE_IMMUTABLE;
		BufferData init{ data, bytes };
		buffer.Release();
		device->CreateBuffer(bd, &init, &buffer);
	};
	make("Ray tracing: terrain vertices", positions.data(), positions.size() * 4, terrainVertices);
	make("Ray tracing: terrain indices", indices.data(), indices.size() * 4, terrainIndices);
	if (!terrainVertices || !terrainIndices || indices.empty())
		return;
	BLASTriangleDesc d;
	d.GeometryName = "terrain";
	d.MaxVertexCount = vertexCount;
	d.VertexValueType = VT_FLOAT32;
	d.VertexComponentCount = 3;
	d.MaxPrimitiveCount = (Uint32)(indices.size() / 3);
	d.IndexType = VT_UINT32;
	BottomLevelASDesc desc;
	desc.Name = "Terrain shadow caster";
	desc.pTriangles = &d;
	desc.TriangleCount = 1;
	desc.Flags = RAYTRACING_BUILD_AS_PREFER_FAST_TRACE;
	device->CreateBLAS(desc, &terrainBlas);
	if (!terrainBlas)
		return;
	const Uint64 need = terrainBlas->GetScratchBufferSizes().Build;
	if (!scratch || scratch->GetDesc().Size < need) {
		BufferDesc sd;
		sd.Name = "BLAS scratch";
		sd.Size = std::max<Uint64>(need, 1 << 20);
		sd.BindFlags = BIND_RAY_TRACING;
		scratch.Release();
		device->CreateBuffer(sd, nullptr, &scratch);
	}
	BLASBuildTriangleData b;
	b.GeometryName = d.GeometryName;
	b.pVertexBuffer = terrainVertices;
	b.VertexStride = 12;
	b.VertexCount = vertexCount;
	b.VertexValueType = VT_FLOAT32;
	b.VertexComponentCount = 3;
	b.PrimitiveCount = d.MaxPrimitiveCount;
	b.pIndexBuffer = terrainIndices;
	b.IndexType = VT_UINT32;
	b.Flags = RAYTRACING_GEOMETRY_FLAG_OPAQUE;
	BuildBLASAttribs ba;
	ba.pBLAS = terrainBlas;
	ba.BLASTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
	ba.GeometryTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
	ba.pTriangleData = &b;
	ba.TriangleDataCount = 1;
	ba.pScratchBuffer = scratch;
	ba.ScratchBufferTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
	ctx->BuildBLAS(ba);
	printf("Ray-traced shadows: terrain %d x %d tiles\n", w, h);
}

void DiligentRayShadows::uploadAlpha(IDeviceContext* ctx)
{
	if (!alphaDirty)
		return;
	alphaDirty = false;
	auto make = [&](const char* name, const void* data, size_t bytes, RefCntAutoPtr<IBuffer>& buffer) {
		static const uint32_t zero[2] = {};
		BufferDesc bd;
		bd.Name = name;
		bd.Size = std::max<size_t>(bytes, 8);
		bd.BindFlags = BIND_SHADER_RESOURCE;
		bd.Usage = USAGE_IMMUTABLE;
		bd.Mode = BUFFER_MODE_STRUCTURED;
		bd.ElementByteStride = 8;
		BufferData init{ bytes ? data : zero, bd.Size };
		buffer.Release();
		device->CreateBuffer(bd, &init, &buffer);
	};
	make("Ray tracing: geometries", geometryRecords.data(), geometryRecords.size() * 4, geometryBuffer);
	make("Ray tracing: alpha-tested UVs", alphaUVs.data(), alphaUVs.size() * 4, uvBuffer);
	alphaVersion++;
}

void DiligentRayShadows::bindAlpha(IShaderResourceBinding* b)
{
	b->GetVariableByName(SHADER_TYPE_PIXEL, "rtGeometries")->Set(geometryBuffer->GetDefaultView(BUFFER_VIEW_SHADER_RESOURCE));
	b->GetVariableByName(SHADER_TYPE_PIXEL, "rtUVs")->Set(uvBuffer->GetDefaultView(BUFFER_VIEW_SHADER_RESOURCE));
	IDeviceObject* views[MAX_ALPHA_TEXTURES];
	for (uint32_t i = 0; i < MAX_ALPHA_TEXTURES; i++)
		views[i] = i < alphaTextures.size() ? (IDeviceObject*)alphaTextures[i].RawPtr()
			: (IDeviceObject*)whiteTexture->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
	b->GetVariableByName(SHADER_TYPE_PIXEL, "alphaTextures")->SetArray(views, 0, MAX_ALPHA_TEXTURES);
}

ITextureView* DiligentRayShadows::renderMask(IDeviceContext* ctx, ITexture* depth, const float projection[4],
	const Matrix& inverseView, const Vector3& sunDirection, uint64_t frameIndex, IBuffer* fogBuffer)
{
	if (!built || !depth || !fogBuffer)
		return nullptr;
	const TextureDesc& dd = depth->GetDesc();
	if (!mask || mask->GetDesc().Width != dd.Width || mask->GetDesc().Height != dd.Height) {
		TextureDesc td;
		td.Name = "Ray-traced shadow mask";
		td.Type = RESOURCE_DIM_TEX_2D;
		td.Width = dd.Width;
		td.Height = dd.Height;
		td.Format = TEX_FORMAT_R8_UNORM;
		td.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;
		mask.Release();
		device->CreateTexture(td, nullptr, &mask);
	}
	if (!srb || boundDepth != depth || boundTlas != tlas.RawPtr() || maskAlphaVersion != alphaVersion || boundFog != fogBuffer) {
		srb.Release();
		signature->CreateShaderResourceBinding(&srb, true);
		srb->GetVariableByName(SHADER_TYPE_PIXEL, "FogBuffer")->Set(fogBuffer);
		boundFog = fogBuffer;
		srb->GetVariableByName(SHADER_TYPE_PIXEL, "tDepth")->Set(depth->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
		srb->GetVariableByName(SHADER_TYPE_PIXEL, "tlas")->Set(tlas);
		bindAlpha(srb);
		maskAlphaVersion = alphaVersion;
		boundDepth = depth;
		boundTlas = tlas;
	}
	void* mapped = nullptr;
	ctx->MapBuffer(constants, MAP_WRITE, MAP_FLAG_DISCARD, mapped);
	const Vector3 sun = sunDirection.normal();
	float* f = (float*)mapped;
	const float head[12] = {
		projection[0], projection[1], projection[2], projection[3],
		1.0f / dd.Width, 1.0f / dd.Height, (float)(frameIndex % 64), 0.0093f, // the sun's disc: 0.53 degrees across
		sun.x, sun.y, sun.z, 0,
	};
	memcpy(f, head, sizeof(head));
	const Matrix t = inverseView.getTranspose();
	memcpy(f + 12, &t, sizeof(Matrix));
	ctx->UnmapBuffer(constants, MAP_WRITE);
	ITextureView* rtv = mask->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
	ctx->SetRenderTargets(1, &rtv, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	ctx->SetViewports(1, nullptr, 0, 0);
	ctx->SetPipelineState(pipeline);
	ctx->CommitShaderResources(srb, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	ctx->Draw(DrawAttribs{ 3, DRAW_FLAG_VERIFY_ALL });
	return mask->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
}

ITextureView* DiligentRayShadows::beginWaterReflections(IDeviceContext* ctx, IBuffer* transformBuffer, ITexture* scene,
	ITexture* sceneDepth, ITextureView* dsv, const Vector3& camera, float projZ22, float projZ32, const float fallback[3])
{
	if (!built || !reflectOk || !scene || !sceneDepth || !dsv)
		return nullptr;
	const TEXTURE_FORMAT dsvFormat = dsv->GetDesc().Format;
	if (!reflectSignature) {
		const PipelineResourceDesc resources[] = {
			{ SHADER_TYPE_VERTEX | SHADER_TYPE_PIXEL, "TransformBuffer", 1, SHADER_RESOURCE_TYPE_CONSTANT_BUFFER, SHADER_RESOURCE_VARIABLE_TYPE_STATIC },
			{ SHADER_TYPE_PIXEL, "ReflectBuffer", 1, SHADER_RESOURCE_TYPE_CONSTANT_BUFFER, SHADER_RESOURCE_VARIABLE_TYPE_STATIC },
			{ SHADER_TYPE_PIXEL, "tScene", 1, SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
			{ SHADER_TYPE_PIXEL, "tDepth", 1, SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
			{ SHADER_TYPE_PIXEL, "tlas", 1, SHADER_RESOURCE_TYPE_ACCEL_STRUCT, SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
			{ SHADER_TYPE_PIXEL, "rtGeometries", 1, SHADER_RESOURCE_TYPE_BUFFER_SRV, SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
			{ SHADER_TYPE_PIXEL, "rtUVs", 1, SHADER_RESOURCE_TYPE_BUFFER_SRV, SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
			{ SHADER_TYPE_PIXEL, "alphaTextures", MAX_ALPHA_TEXTURES, SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
		};
		SamplerDesc linear;
		linear.AddressU = linear.AddressV = linear.AddressW = TEXTURE_ADDRESS_CLAMP;
		SamplerDesc alphaSampler;
		alphaSampler.MinFilter = alphaSampler.MagFilter = alphaSampler.MipFilter = FILTER_TYPE_POINT;
		alphaSampler.AddressU = alphaSampler.AddressV = alphaSampler.AddressW = TEXTURE_ADDRESS_WRAP;
		const ImmutableSamplerDesc samplers[] = { { SHADER_TYPE_PIXEL, "sLinear", linear }, { SHADER_TYPE_PIXEL, "sAlpha", alphaSampler } };
		PipelineResourceSignatureDesc sig;
		sig.Name = "Ray-traced reflections";
		sig.Resources = resources;
		sig.NumResources = (Uint32)std::size(resources);
		sig.ImmutableSamplers = samplers;
		sig.NumImmutableSamplers = (Uint32)std::size(samplers);
		device->CreatePipelineResourceSignature(sig, &reflectSignature);
		if (!reflectSignature) {
			reflectOk = false;
			return nullptr;
		}
		reflectSignature->GetStaticVariableByName(SHADER_TYPE_VERTEX, "TransformBuffer")->Set(transformBuffer);
		reflectSignature->GetStaticVariableByName(SHADER_TYPE_PIXEL, "TransformBuffer")->Set(transformBuffer);
		reflectSignature->GetStaticVariableByName(SHADER_TYPE_PIXEL, "ReflectBuffer")->Set(reflectConstants);
	}
	if (!reflectPipeline || reflectDsvFormat != dsvFormat) {
		static const LayoutElement layout[] = { LayoutElement{ 0, 0, 3, VT_FLOAT32, False }, LayoutElement{ 1, 0, 1, VT_FLOAT32, False } };
		GraphicsPipelineStateCreateInfo ci;
		ci.PSODesc.Name = "Ray-traced water reflections";
		ci.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;
		auto& gp = ci.GraphicsPipeline;
		gp.NumRenderTargets = 1;
		gp.RTVFormats[0] = TEX_FORMAT_RGBA16_FLOAT;
		gp.DSVFormat = dsvFormat;
		gp.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		gp.RasterizerDesc.CullMode = CULL_MODE_NONE;
		gp.DepthStencilDesc.DepthEnable = True;
		gp.DepthStencilDesc.DepthWriteEnable = False;
		gp.DepthStencilDesc.DepthFunc = COMPARISON_FUNC_LESS_EQUAL;
		gp.InputLayout.LayoutElements = layout;
		gp.InputLayout.NumElements = (Uint32)std::size(layout);
		ci.pVS = reflectVs;
		ci.pPS = reflectPs;
		IPipelineResourceSignature* sigs[] = { reflectSignature };
		ci.ppResourceSignatures = sigs;
		ci.ResourceSignaturesCount = 1;
		reflectPipeline.Release();
		device->CreateGraphicsPipelineState(ci, &reflectPipeline);
		reflectDsvFormat = dsvFormat;
		if (!reflectPipeline) {
			printf("Ray-traced reflections: could not create the pipeline: off\n");
			reflectOk = false;
			return nullptr;
		}
		printf("Ray-traced reflections: ready\n");
	}
	const TextureDesc& sd = scene->GetDesc();
	if (!reflections || reflections->GetDesc().Width != sd.Width || reflections->GetDesc().Height != sd.Height) {
		TextureDesc td;
		td.Name = "Ray-traced water reflections";
		td.Type = RESOURCE_DIM_TEX_2D;
		td.Width = sd.Width;
		td.Height = sd.Height;
		td.Format = TEX_FORMAT_RGBA16_FLOAT;
		td.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;
		reflections.Release();
		device->CreateTexture(td, nullptr, &reflections);
		if (!reflections)
			return nullptr;
	}
	if (!reflectSrb || reflectBound[0] != scene || reflectBound[1] != sceneDepth || reflectBoundTlas != tlas.RawPtr() || reflectAlphaVersion != alphaVersion) {
		reflectSrb.Release();
		reflectSignature->CreateShaderResourceBinding(&reflectSrb, true);
		reflectSrb->GetVariableByName(SHADER_TYPE_PIXEL, "tScene")->Set(scene->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
		reflectSrb->GetVariableByName(SHADER_TYPE_PIXEL, "tDepth")->Set(sceneDepth->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
		reflectSrb->GetVariableByName(SHADER_TYPE_PIXEL, "tlas")->Set(tlas);
		bindAlpha(reflectSrb);
		reflectAlphaVersion = alphaVersion;
		reflectBound[0] = scene;
		reflectBound[1] = sceneDepth;
		reflectBoundTlas = tlas;
	}
	void* mapped = nullptr;
	ctx->MapBuffer(reflectConstants, MAP_WRITE, MAP_FLAG_DISCARD, mapped);
	const float data[12] = {
		camera.x, camera.y, camera.z, 300.0f,
		projZ22, projZ32, 0, 0,
		fallback[0], fallback[1], fallback[2], 0,
	};
	memcpy(mapped, data, sizeof(data));
	ctx->UnmapBuffer(reflectConstants, MAP_WRITE);
	ITextureView* rtv = reflections->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
	ctx->SetRenderTargets(1, &rtv, dsv, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	ctx->SetViewports(1, nullptr, 0, 0);
	const float clear[4] = { 0, 0, 0, 0 };
	ctx->ClearRenderTarget(rtv, clear, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	ctx->SetPipelineState(reflectPipeline);
	ctx->CommitShaderResources(reflectSrb, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	return reflections->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
}
