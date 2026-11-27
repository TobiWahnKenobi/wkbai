// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#include "DiligentRayShadows.h"
#include "Forest.h"
#include "TreeWind.h"
#include "../scene.h"
#include "ShaderCache.h"
#include "../util/LoadingProgress.h"
#include "../util/util.h"
#include "../terrain.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

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
RaytracingAccelerationStructure tlas2; // the still casters: the forest, the terrain (DiligentRayShadows::setForest)
Texture2D<float> tDepth;
Texture2D<float> tHeights; // the terrain's vertex heights

cbuffer RtBuffer
{
	float4 Proj;         // P11, P22, P33, P43
	float4 Texel;        // 1 / width, 1 / height, the frame (the jitter), the sun's angular radius
	float4 SunDir;       // towards the sun, w: the map edge in tiles + 1 (0: no terrain heights)
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
	// the haze thickening towards the horizon (the play test of 2026-11-09: with a long view the far
	// sea and land went from the edge fog's full haze to hardly any within a few pixels, its ramp
	// squeezed together at the grazing view, a line under the sky): what is fogged at all fogs
	// faster the nearer its direction lies to the horizontal (within about 2 degrees); the views
	// down onto the map keep their fog
	float f = max(distanceFog, edgeFog);
	float level = 1 - smoothstep(0, 0.035, abs(FogCamera.y - worldPos.y) / max(distanceToCamera, 1));
	return 1 - pow(1 - f, 1 + 6 * level);
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

// the terrain's height between its vertices (bilinear: the grass's ground, for its normal)
float GroundHeight(float2 xz)
{
	uint w, h;
	tHeights.GetDimensions(w, h);
	float2 g = xz / 5 + (SunDir.w - 1);
	int2 i = (int2)floor(g);
	float2 f = g - i;
	int2 top = int2(w, h) - 1;
	float h00 = tHeights.Load(int3(clamp(i, 0, top), 0)), h10 = tHeights.Load(int3(clamp(i + int2(1, 0), 0, top), 0));
	float h01 = tHeights.Load(int3(clamp(i + int2(0, 1), 0, top), 0)), h11 = tHeights.Load(int3(clamp(i + int2(1, 1), 0, top), 0));
	return lerp(lerp(h00, h10, f.x), lerp(h01, h11, f.x), f.y);
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
	float fog = FogAmount(p, distance(p, FogCamera.xyz));
	if (fog >= 0.999)
		return 1;
	float dist = length(p - camPos);
	float off = 0.05 + dist * 0.0008;
	// turned away from the sun: in its own shadow, as deep as the shadows it casts (the terrain's
	// sky light is brightest on the slopes away from the sun, which would otherwise stay lighter
	// than the shadow falling from them); faded in over the last few degrees before the terminator
	float nl = dot(n, l);
	if (nl < 0.15) {
		// only on a surface the rays know (the terrain, the models): a short probe into it. The
		// grass isn't in the structure, and its depth's normals (thin blades, each pixel another
		// blade) face every way: its blades were half dark all over the meadow (the play test).
		// There the ground under it decides, its normal from the terrain's heights.
		RayDesc probe;
		probe.Origin = p + n * off;
		probe.TMin = 0;
		probe.Direction = -n;
		probe.TMax = off * 2.5;
		RayQuery<RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> pq;
		pq.TraceRayInline(tlas, RAY_FLAG_NONE, 0xFF, probe);
)---" ALPHA_TEST_LOOP(pq) R"---(
		RayQuery<RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> pq2;
		pq2.TraceRayInline(tlas2, RAY_FLAG_NONE, 0xFF, probe);
)---" ALPHA_TEST_LOOP(pq2) R"---(
		if (pq.CommittedStatus() != COMMITTED_TRIANGLE_HIT && pq2.CommittedStatus() != COMMITTED_TRIANGLE_HIT) {
			// no terrain heights: lit by its own light alone (as before the self shadow)
			n = SunDir.w > 0 ? normalize(float3(GroundHeight(p.xz - float2(1.5, 0)) - GroundHeight(p.xz + float2(1.5, 0)), 3.0,
				GroundHeight(p.xz - float2(0, 1.5)) - GroundHeight(p.xz + float2(0, 1.5)))) : l;
			nl = dot(n, l);
		}
	}
	float self = lerp(fog, 1, smoothstep(-0.06, 0.15, nl));
	if (nl <= -0.06)
		return self;
	// a point of the sun's disc, moved every frame (interleaved gradient noise)
	float2 q = input.Pos.xy + 5.588238 * fmod(Texel.z, 64);
	float r1 = frac(52.9829189 * frac(dot(q, float2(0.06711056, 0.00583715))));
	float r2 = frac(52.9829189 * frac(dot(q.yx + 17.3, float2(0.06711056, 0.00583715))));
	float3 t = normalize(cross(l, abs(l.y) < 0.9 ? float3(0, 1, 0) : float3(1, 0, 0)));
	float3 b = cross(l, t);
	float a = r1 * 6.2831853, rr = sqrt(r2) * Texel.w;
	float3 dir = normalize(l + (t * cos(a) + b * sin(a)) * rr);
	RayDesc ray;
	ray.Origin = p + n * off;
	ray.TMin = 0.01;
	ray.Direction = dir;
	ray.TMax = 2000; // the terrain's long shadows at a low sun
	// (back faces culled: only the terrain's, the models' instances turn the culling off)
	RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES | RAY_FLAG_CULL_BACK_FACING_TRIANGLES> rq;
	rq.TraceRayInline(tlas, RAY_FLAG_NONE, 0xFF, ray);
)---" ALPHA_TEST_LOOP(rq) R"---(
	if (rq.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
		return fog;
	RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES | RAY_FLAG_CULL_BACK_FACING_TRIANGLES> rq2;
	rq2.TraceRayInline(tlas2, RAY_FLAG_NONE, 0xFF, ray);
)---" ALPHA_TEST_LOOP(rq2) R"---(
	return rq2.CommittedStatus() == COMMITTED_TRIANGLE_HIT ? fog : self;
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
RaytracingAccelerationStructure tlas2; // the still casters: the forest, the terrain (DiligentRayShadows::setForest)
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
	bool any = rq.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
	float t = any ? rq.CommittedRayT() : ray.TMax;
	RayDesc ray2 = ray;
	ray2.TMax = t;
	RayQuery<RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> rq2;
	rq2.TraceRayInline(tlas2, RAY_FLAG_NONE, 0x01, ray2);
)---" ALPHA_TEST_LOOP(rq2) R"---(
	if (rq2.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
		any = true;
		t = rq2.CommittedRayT();
	}
	if (!any)
		return float4(0, 0, 0, 0);
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

// The worker thread making the structures (the GPU allocations, IRenderDevice being free-threaded)
struct DiligentRayShadows::Maker {
	struct Job {
		const void* key;
		std::vector<std::string> names;
		std::vector<BLASTriangleDesc> descs;
	};
	IRenderDevice* device;
	std::mutex mutex;
	std::condition_variable wake;
	std::deque<Job> jobs;
	std::vector<std::pair<const void*, RefCntAutoPtr<IBottomLevelAS>>> made;
	bool quit = false;
	std::thread thread;

	explicit Maker(IRenderDevice* d) : device(d), thread([this] { run(); }) {}
	~Maker()
	{
		{
			std::lock_guard<std::mutex> lock(mutex);
			quit = true;
		}
		wake.notify_all();
		thread.join();
	}
	void run()
	{
		for (;;) {
			Job job;
			{
				std::unique_lock<std::mutex> lock(mutex);
				wake.wait(lock, [this] { return quit || !jobs.empty(); });
				if (quit)
					return;
				job = std::move(jobs.front());
				jobs.pop_front();
			}
			for (size_t i = 0; i < job.descs.size(); i++)
				job.descs[i].GeometryName = job.names[i].c_str();
			BottomLevelASDesc desc;
			desc.Name = "Shadow caster";
			desc.pTriangles = job.descs.data();
			desc.TriangleCount = (Uint32)job.descs.size();
			desc.Flags = RAYTRACING_BUILD_AS_PREFER_FAST_TRACE;
			RefCntAutoPtr<IBottomLevelAS> blas;
			device->CreateBLAS(desc, &blas);
			std::lock_guard<std::mutex> lock(mutex);
			made.emplace_back(job.key, std::move(blas));
		}
	}
};

DiligentRayShadows::DiligentRayShadows(IRenderDevice* device) : device(device)
{
	maker = new Maker(device);
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
		{ SHADER_TYPE_PIXEL, "tHeights", 1, SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
		{ SHADER_TYPE_PIXEL, "tlas", 1, SHADER_RESOURCE_TYPE_ACCEL_STRUCT, SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
		{ SHADER_TYPE_PIXEL, "tlas2", 1, SHADER_RESOURCE_TYPE_ACCEL_STRUCT, SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
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
		LoadingCompilingShader(entry); // (the loading screen)
		ShaderCache::CreateShader(device, sci, &s);
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
			LoadingCompilingShader(entry); // (the loading screen)
			ShaderCache::CreateShader(device, sci, &s);
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
	ShaderCache::CreateGraphicsPipeline(device, ci, &pipeline);
	ok = pipeline != nullptr;
	printf("Ray-traced shadows: %s\n", ok ? "ready" : "could not create the pipeline: off");
}

static double MsSince(std::chrono::steady_clock::time_point t0)
{
	return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

void DiligentRayShadows::beginFrame()
{
	stats = {};
	const auto t0 = std::chrono::steady_clock::now();
	instances.clear();
	frameCounter++;
	lastAdded = nullptr;
	// the worker's new structures into their pools
	{
		std::vector<std::pair<const void*, RefCntAutoPtr<IBottomLevelAS>>> made;
		{
			std::lock_guard<std::mutex> lock(maker->mutex);
			made.swap(maker->made);
		}
		for (auto& m : made) {
			Pool& pool = pools[m.first];
			pool.pending = false;
			if (m.second)
				pool.spare.push_back(std::move(m.second));
			else
				pool.owned--; // (could not be made: asked again when next wanted)
		}
	}
	// the poses unused for a while back into their pools (their structures kept for the
	// animation's next poses: no GPU allocation freed or made)
	for (auto it = animatedLastUse.begin(); it != animatedLastUse.end(); ) {
		if (frameCounter - it->second > ANIMATED_KEEP) {
			auto b = blases.find(it->first);
			if (b != blases.end()) {
				blasGeometryBase.erase(b->second.RawPtr());
				Pool& pool = pools[it->first.first];
				if (b->second)
					pool.spare.push_back(std::move(b->second));
				else
					pool.owned--;
				blases.erase(b);
				structuresChanged = true;
			}
			it = animatedLastUse.erase(it);
			stats.evicted++;
		}
		else
			++it;
	}
	// the pools of the animations unused that long: their spare structures freed, a few a frame
	// (a burst of frees is a hitch too)
	if (frameCounter % 30 == 0) {
		int freed = 0;
		for (auto& [key, pool] : pools) {
			if (frameCounter - pool.lastUse <= ANIMATED_KEEP || pool.spare.empty())
				continue;
			while (!pool.spare.empty() && freed < MAX_FREES_PER_FRAME) {
				pool.spare.pop_back();
				pool.owned--;
				freed++;
			}
			if (freed >= MAX_FREES_PER_FRAME)
				break;
		}
	}
	stats.evictMs = MsSince(t0);
}

DiligentRayShadows::~DiligentRayShadows()
{
	delete maker;
}

void DiligentRayShadows::request(const void* key, Pool& pool, const std::vector<Group>& groups)
{
	if (pool.pending)
		return;
	Maker::Job job;
	job.key = key;
	for (size_t g = 0; g < groups.size(); g++) {
		const Group& gr = groups[g];
		if (gr.indexCount < 3 || gr.vertexCount == 0)
			continue;
		job.names.push_back("g" + std::to_string(g));
		BLASTriangleDesc d;
		d.MaxVertexCount = gr.vertexCount;
		d.VertexValueType = VT_FLOAT32;
		d.VertexComponentCount = 3;
		d.MaxPrimitiveCount = gr.indexCount / 3;
		d.IndexType = VT_UINT16;
		job.descs.push_back(d);
	}
	if (job.descs.empty())
		return;
	pool.pending = true;
	pool.owned++;
	{
		std::lock_guard<std::mutex> lock(maker->mutex);
		maker->jobs.push_back(std::move(job));
	}
	maker->wake.notify_one();
}

RefCntAutoPtr<IBottomLevelAS> DiligentRayShadows::take(const void* key, const std::vector<Group>& groups, bool animated, bool peek)
{
	Pool& pool = pools[key];
	pool.lastUse = frameCounter;
	RefCntAutoPtr<IBottomLevelAS> got;
	if (!pool.spare.empty()) {
		if (peek)
			return pool.spare.back();
		got = std::move(pool.spare.back());
		pool.spare.pop_back();
		// (one more made ahead while the animation's pool may grow)
		if (animated && pool.spare.empty() && pool.owned < POSES_PER_KEY)
			request(key, pool, groups);
		return got;
	}
	if (!animated || pool.owned < POSES_PER_KEY) {
		request(key, pool, groups);
		return got;
	}
	// at the limit: the animation's pose unused longest, if no frame in flight can still use it
	auto first = animatedLastUse.lower_bound({ key, 0u });
	auto oldest = animatedLastUse.end();
	for (auto it = first; it != animatedLastUse.end() && it->first.first == key; ++it)
		if (oldest == animatedLastUse.end() || it->second < oldest->second)
			oldest = it;
	if (oldest == animatedLastUse.end() || oldest->second + IN_FLIGHT > frameCounter)
		return got;
	auto b = blases.find(oldest->first);
	if (b != blases.end()) {
		if (peek)
			return b->second;
		got = std::move(b->second);
		blasGeometryBase.erase(got.RawPtr());
		blases.erase(b);
	}
	animatedLastUse.erase(oldest);
	return got;
}

bool DiligentRayShadows::poseSlot(const void* key, const std::vector<Group>& groups)
{
	return take(key, groups, true, true) != nullptr;
}

IBottomLevelAS* DiligentRayShadows::cached(const void* key, uint32_t frame)
{
	auto it = blases.find({ key, frame });
	if (it == blases.end() || !it->second)
		return nullptr;
	auto used = animatedLastUse.find({ key, frame });
	if (used != animatedLastUse.end()) {
		used->second = frameCounter;
		auto pool = pools.find(key);
		if (pool != pools.end())
			pool->second.lastUse = frameCounter;
	}
	return it->second;
}

IBottomLevelAS* DiligentRayShadows::cachedNear(const void* key, uint32_t frame, uint32_t range)
{
	// (the key's poses are next to each other in the map, by pose)
	auto after = blases.lower_bound({ key, frame });
	uint32_t best = ~0u, bestDistance = ~0u;
	if (after != blases.end() && after->first.first == key && after->second && after->first.second - frame <= range)
		best = after->first.second, bestDistance = after->first.second - frame;
	for (auto it = after; it != blases.begin(); ) {
		--it;
		if (it->first.first != key)
			break;
		if (it->second) {
			if (frame - it->first.second <= range && frame - it->first.second < bestDistance)
				best = it->first.second;
			break;
		}
	}
	return best != ~0u ? cached(key, best) : nullptr;
}

IBottomLevelAS* DiligentRayShadows::pose(IDeviceContext* ctx, const void* key, uint32_t frame, const float* positions,
	uint32_t vertexCount, IBuffer* indices, const std::vector<Group>& groups)
{
	if (IBottomLevelAS* b = cached(key, frame))
		return b;
	if (frameCounter != poseFrame) {
		poseFrame = frameCounter;
		poseOffset = 0;
	}
	const uint64_t bytes = std::max<uint64_t>((uint64_t)vertexCount * 12, 12);
	const uint64_t start = (poseOffset + 255) & ~255ull;
	if (!poseBuffer || start + bytes > poseBuffer->GetDesc().Size) {
		// (the old one is freed once the GPU has done with this frame's copies into it)
		BufferDesc pd;
		pd.Name = "RT poses";
		pd.Size = std::max<uint64_t>({ bytes * 4, poseBuffer ? poseBuffer->GetDesc().Size * 2 : 0, 4ull << 20 });
		pd.BindFlags = BIND_RAY_TRACING;
		pd.Usage = USAGE_DEFAULT;
		poseBuffer.Release();
		device->CreateBuffer(pd, nullptr, &poseBuffer);
		if (!poseBuffer)
			return nullptr;
		poseOffset = 0;
	}
	const uint64_t offset = (poseOffset + 255) & ~255ull;
	ctx->UpdateBuffer(poseBuffer, offset, bytes, positions, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	poseOffset = offset + bytes;
	return blas(ctx, key, frame, poseBuffer, offset, vertexCount, indices, groups, true);
}

// the mesh's geometries for the alpha test, once per mesh (all its key frames share them)
uint32_t DiligentRayShadows::geometryBaseOf(const std::vector<Group>& groups)
{
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
	return base->second;
}

IBottomLevelAS* DiligentRayShadows::blas(IDeviceContext* ctx, const void* key, uint32_t frame, IBuffer* positions,
	uint64_t positionOffset, uint32_t vertexCount, IBuffer* indices, const std::vector<Group>& groups, bool animated)
{
	{
		auto it = blases.find({ key, frame });
		if (it != blases.end() && it->second) {
			if (animated)
				animatedLastUse[{ key, frame }] = frameCounter;
			return it->second;
		}
	}
	const uint32_t geometryBase = geometryBaseOf(groups);
	RefCntAutoPtr<IBottomLevelAS> slot = take(key, groups, animated);
	if (!slot)
		return nullptr;
	const auto t0 = std::chrono::steady_clock::now();
	struct Done { FrameStats& s; std::chrono::steady_clock::time_point t0; ~Done() { s.created++; s.createMs += MsSince(t0); } } done{ stats, t0 };
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
	// (a pooled structure built again for this pose keeps its address: build() told here)
	structuresChanged = true;
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
	if (animated)
		animatedLastUse[{ key, frame }] = frameCounter;
	auto& stored = blases[{ key, frame }];
	stored = std::move(slot);
	return stored;
}

// The trees' bend for their shadow structures (TreeWind.h): a mesh's rest positions in, per wind
// group (the dispatch's y) its bent ones out, the group's region in the bent buffer
static const char windComputeCode[] = R"---(
cbuffer WindJob
{
	float4 Wind;  // WindBend (TreeWind.h)
	uint4 Job;    // x the vertices, y the bent buffer's stride per group (vertices)
};
ByteAddressBuffer Rest;
RWByteAddressBuffer Bent;

[numthreads(64, 1, 1)]
void CSWind(uint3 id : SV_DispatchThreadID)
{
	if (id.x >= Job.x)
		return;
	float3 p = asfloat(Rest.Load3(id.x * 12));
	Bent.Store3((id.y * Job.y + id.x) * 12, asuint(WindDeform(p, id.y, Wind)));
}
)---";

void DiligentRayShadows::windDispatch(IDeviceContext* ctx, WindMesh& wm)
{
	void* mapped = nullptr;
	ctx->MapBuffer(windConstants, MAP_WRITE, MAP_FLAG_DISCARD, mapped);
	uint32_t* c = (uint32_t*)mapped;
	memcpy(c, windNow, 16);
	c[4] = wm.vertexCount;
	c[5] = wm.stride;
	c[6] = c[7] = 0;
	ctx->UnmapBuffer(windConstants, MAP_WRITE);
	ctx->SetPipelineState(windPipeline);
	ctx->CommitShaderResources(wm.srb, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	DispatchComputeAttribs da;
	da.ThreadGroupCountX = (wm.vertexCount + 63) / 64;
	da.ThreadGroupCountY = WIND_GROUPS;
	ctx->DispatchCompute(da);
}

void DiligentRayShadows::windBegin(IDeviceContext* ctx, const float wind[4])
{
	const bool wasOn = windNow[3] > 0;
	std::copy_n(wind, 4, windNow);
	if (!windTried) {
		windTried = true;
		std::string source = std::string(g_treeWindHlsl) + windComputeCode;
		ShaderCreateInfo sci;
		sci.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
		sci.Source = source.c_str();
		sci.SourceLength = source.size();
		sci.EntryPoint = "CSWind";
		sci.Desc.ShaderType = SHADER_TYPE_COMPUTE;
		sci.Desc.Name = "Tree wind (shadows)";
		RefCntAutoPtr<IShader> shader;
		ShaderCache::CreateShader(device, sci, &shader);
		if (shader) {
			ComputePipelineStateCreateInfo ci;
			ci.PSODesc.Name = "Tree wind (shadows)";
			ci.PSODesc.PipelineType = PIPELINE_TYPE_COMPUTE;
			ci.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;
			ci.pCS = shader;
			ShaderCache::CreateComputePipeline(device, ci, &windPipeline);
		}
		BufferDesc cb;
		cb.Name = "Tree wind constants";
		cb.Size = 32;
		cb.BindFlags = BIND_UNIFORM_BUFFER;
		cb.Usage = USAGE_DYNAMIC;
		cb.CPUAccessFlags = CPU_ACCESS_WRITE;
		device->CreateBuffer(cb, nullptr, &windConstants);
		const uint16_t corners[6] = { 0, 1, 2, 3, 4, 5 };
		BufferDesc ib;
		ib.Name = "Tree wind corners";
		ib.Size = 16;
		ib.BindFlags = BIND_RAY_TRACING;
		ib.Usage = USAGE_IMMUTABLE;
		uint16_t padded[8] = {};
		std::copy_n(corners, 6, padded);
		BufferData init{ padded, sizeof(padded) };
		device->CreateBuffer(ib, &init, &windCorners);
		if (!windPipeline || !windConstants || !windCorners) {
			windPipeline.Release();
			printf("Ray-traced shadows: the trees' wind pass could not be made: shadows of the trees stand still\n");
		}
	}
	if (!windPipeline || windNow[3] <= 0) {
		if (wasOn)
			structuresChanged = true; // (back to the still meshes)
		return;
	}
	if (!wasOn)
		structuresChanged = true;
	// the meshes in the still structure or used of late: bent and refitted, every group, every
	// frame. The refits without a barrier between them (each its own structure, its own scratch
	// region): the structures and the bent buffers are moved to their states once, together
	std::vector<WindMesh*> live;
	for (auto& it : windMeshes)
		if (it.second.inStill || it.second.lastUse + 2 >= frameCounter)
			live.push_back(&it.second);
	if (live.empty())
		return;
	for (WindMesh* wm : live)
		windDispatch(ctx, *wm);
	std::vector<StateTransitionDesc> barriers;
	barriers.reserve(live.size() * (WIND_GROUPS + 1) + 1);
	for (WindMesh* wm : live) {
		barriers.push_back(StateTransitionDesc(wm->bent, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_BUILD_AS_READ, STATE_TRANSITION_FLAG_UPDATE_STATE));
		for (auto& b : wm->blas)
			barriers.push_back(StateTransitionDesc(b, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_BUILD_AS_WRITE, STATE_TRANSITION_FLAG_UPDATE_STATE));
	}
	barriers.push_back(StateTransitionDesc(windScratch, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_BUILD_AS_WRITE, STATE_TRANSITION_FLAG_UPDATE_STATE));
	ctx->TransitionResourceStates((Uint32)barriers.size(), barriers.data());
	std::vector<BLASBuildTriangleData> data;
	for (WindMesh* wm : live) {
		for (uint32_t g = 0; g < WIND_GROUPS; g++) {
			data = wm->data;
			for (auto& d : data)
				d.VertexOffset += (uint64_t)g * wm->stride * 12;
			BuildBLASAttribs ba;
			ba.pBLAS = wm->blas[g];
			ba.BLASTransitionMode = RESOURCE_STATE_TRANSITION_MODE_NONE;
			ba.GeometryTransitionMode = RESOURCE_STATE_TRANSITION_MODE_NONE;
			ba.pTriangleData = data.data();
			ba.TriangleDataCount = (Uint32)data.size();
			ba.pScratchBuffer = windScratch;
			ba.ScratchBufferOffset = wm->scratchOffset[g];
			ba.ScratchBufferTransitionMode = RESOURCE_STATE_TRANSITION_MODE_NONE;
			ba.Update = true;
			ctx->BuildBLAS(ba);
		}
	}
	barriers.clear();
	for (WindMesh* wm : live)
		for (auto& b : wm->blas)
			barriers.push_back(StateTransitionDesc(b, RESOURCE_STATE_BUILD_AS_WRITE, RESOURCE_STATE_BUILD_AS_READ, STATE_TRANSITION_FLAG_UPDATE_STATE));
	ctx->TransitionResourceStates((Uint32)barriers.size(), barriers.data());
}

IBottomLevelAS* DiligentRayShadows::windBlas(IDeviceContext* ctx, const void* key, const std::function<std::vector<Vector3>()>& restOf,
	IBuffer* indices, const std::vector<Group>& groups, uint32_t group)
{
	if (!windOn())
		return nullptr;
	auto it = windMeshes.find(key);
	if (it == windMeshes.end()) {
		const std::vector<Vector3> rest = restOf();
		if (rest.empty())
			return nullptr;
		WindMesh& wm = windMeshes[key];
		wm.vertexCount = (uint32_t)rest.size();
		wm.stride = wm.vertexCount + 6;
		// the farthest a bend reaches (WindDeform: 1 m + 20 % of the height), at its corners two
		// triangles of a millimetre (the bounds then stay as they are, however the tree bends)
		Vector3 lo = rest[0], hi = rest[0];
		for (const Vector3& v : rest) {
			lo = Vector3(std::min(lo.x, v.x), std::min(lo.y, v.y), std::min(lo.z, v.z));
			hi = Vector3(std::max(hi.x, v.x), std::max(hi.y, v.y), std::max(hi.z, v.z));
		}
		const float reach = 1.05f + 0.2f * std::max(hi.y, 0.0f);
		lo = lo - Vector3(reach, reach, reach);
		hi = hi + Vector3(reach, reach, reach);
		std::vector<Vector3> init;
		init.reserve((size_t)wm.stride * WIND_GROUPS);
		for (uint32_t g = 0; g < WIND_GROUPS; g++) {
			init.insert(init.end(), rest.begin(), rest.end());
			init.insert(init.end(), { lo, lo + Vector3(0.001f, 0, 0), lo + Vector3(0, 0, 0.001f),
				hi, hi - Vector3(0.001f, 0, 0), hi - Vector3(0, 0, 0.001f) });
		}
		BufferDesc rd;
		rd.Name = "Tree wind rest";
		rd.Size = (uint64_t)rest.size() * 12;
		rd.BindFlags = BIND_SHADER_RESOURCE;
		rd.Usage = USAGE_IMMUTABLE;
		rd.Mode = BUFFER_MODE_RAW;
		BufferData rdata{ rest.data(), rd.Size };
		device->CreateBuffer(rd, &rdata, &wm.rest);
		BufferDesc bd;
		bd.Name = "Tree wind bent";
		bd.Size = (uint64_t)init.size() * 12;
		bd.BindFlags = BIND_UNORDERED_ACCESS | BIND_RAY_TRACING;
		bd.Usage = USAGE_DEFAULT;
		bd.Mode = BUFFER_MODE_RAW;
		BufferData bdata{ init.data(), bd.Size };
		device->CreateBuffer(bd, &bdata, &wm.bent);
		if (!wm.rest || !wm.bent) {
			windMeshes.erase(key);
			return nullptr;
		}
		windPipeline->CreateShaderResourceBinding(&wm.srb, true);
		wm.srb->GetVariableByName(SHADER_TYPE_COMPUTE, "WindJob")->Set(windConstants);
		wm.srb->GetVariableByName(SHADER_TYPE_COMPUTE, "Rest")->Set(wm.rest->GetDefaultView(BUFFER_VIEW_SHADER_RESOURCE));
		wm.srb->GetVariableByName(SHADER_TYPE_COMPUTE, "Bent")->Set(wm.bent->GetDefaultView(BUFFER_VIEW_UNORDERED_ACCESS));
		// the geometries: the mesh's (their alpha test records copied) and the corners' (opaque)
		const uint32_t meshBase = geometryBaseOf(groups);
		const uint32_t base = (uint32_t)(geometryRecords.size() / 2);
		std::vector<BLASTriangleDesc> descs;
		wm.names.reserve(groups.size() + 1);
		for (size_t g = 0; g < groups.size(); g++) {
			const Group& gr = groups[g];
			if (gr.indexCount < 3 || gr.vertexCount == 0)
				continue;
			const uint32_t rec = meshBase + (uint32_t)descs.size();
			geometryRecords.push_back(geometryRecords[2 * rec]);
			geometryRecords.push_back(geometryRecords[2 * rec + 1]);
			wm.names.push_back("g" + std::to_string(g));
			BLASTriangleDesc d;
			d.MaxVertexCount = gr.vertexCount;
			d.VertexValueType = VT_FLOAT32;
			d.VertexComponentCount = 3;
			d.MaxPrimitiveCount = gr.indexCount / 3;
			d.IndexType = VT_UINT16;
			descs.push_back(d);
			BLASBuildTriangleData b;
			b.pVertexBuffer = wm.bent;
			b.VertexOffset = (uint64_t)gr.firstVertex * 12;
			b.VertexStride = 12;
			b.VertexCount = gr.vertexCount;
			b.VertexValueType = VT_FLOAT32;
			b.VertexComponentCount = 3;
			b.PrimitiveCount = gr.indexCount / 3;
			b.pIndexBuffer = indices;
			b.IndexOffset = (uint64_t)gr.firstIndex * 2;
			b.IndexType = VT_UINT16;
			b.Flags = geometryRecords[2 * rec + 1] != 0 ? RAYTRACING_GEOMETRY_FLAG_NONE : RAYTRACING_GEOMETRY_FLAG_OPAQUE;
			wm.data.push_back(b);
		}
		if (descs.empty()) {
			windMeshes.erase(key);
			return nullptr;
		}
		geometryRecords.push_back(0);
		geometryRecords.push_back(0);
		alphaDirty = true;
		wm.names.push_back("corners");
		{
			BLASTriangleDesc d;
			d.MaxVertexCount = 6;
			d.VertexValueType = VT_FLOAT32;
			d.VertexComponentCount = 3;
			d.MaxPrimitiveCount = 2;
			d.IndexType = VT_UINT16;
			descs.push_back(d);
			BLASBuildTriangleData b;
			b.pVertexBuffer = wm.bent;
			b.VertexOffset = (uint64_t)wm.vertexCount * 12;
			b.VertexStride = 12;
			b.VertexCount = 6;
			b.VertexValueType = VT_FLOAT32;
			b.VertexComponentCount = 3;
			b.PrimitiveCount = 2;
			b.pIndexBuffer = windCorners;
			b.IndexOffset = 0;
			b.IndexType = VT_UINT16;
			b.Flags = RAYTRACING_GEOMETRY_FLAG_OPAQUE;
			wm.data.push_back(b);
		}
		for (size_t k = 0; k < descs.size(); k++) {
			descs[k].GeometryName = wm.names[k].c_str();
			wm.data[k].GeometryName = wm.names[k].c_str();
		}
		BottomLevelASDesc desc;
		desc.Name = "Tree in the wind";
		desc.pTriangles = descs.data();
		desc.TriangleCount = (Uint32)descs.size();
		desc.Flags = RAYTRACING_BUILD_AS_PREFER_FAST_TRACE | RAYTRACING_BUILD_AS_ALLOW_UPDATE;
		wm.blas.resize(WIND_GROUPS);
		wm.scratchOffset.resize(WIND_GROUPS);
		for (uint32_t g = 0; g < WIND_GROUPS; g++) {
			device->CreateBLAS(desc, &wm.blas[g]);
			if (!wm.blas[g]) {
				windMeshes.erase(key);
				return nullptr;
			}
		}
		// the refits' scratch: a region per structure (a bigger buffer when it doesn't fit)
		for (uint32_t g = 0; g < WIND_GROUPS; g++) {
			wm.scratchOffset[g] = windScratchNeed;
			windScratchNeed += (wm.blas[g]->GetScratchBufferSizes().Update + 255) & ~255ull;
		}
		if (!windScratch || windScratch->GetDesc().Size < windScratchNeed) {
			BufferDesc sd;
			sd.Name = "Tree wind scratch";
			sd.Size = std::max<uint64_t>(windScratchNeed + windScratchNeed / 2, 1 << 20);
			sd.BindFlags = BIND_RAY_TRACING;
			windScratch.Release();
			device->CreateBuffer(sd, nullptr, &windScratch);
		}
		// bent as now, built in full
		windDispatch(ctx, wm);
		const Uint64 need = wm.blas[0]->GetScratchBufferSizes().Build;
		if (!scratch || scratch->GetDesc().Size < need) {
			BufferDesc sd;
			sd.Name = "BLAS scratch";
			sd.Size = std::max<Uint64>(need, 1 << 20);
			sd.BindFlags = BIND_RAY_TRACING;
			scratch.Release();
			device->CreateBuffer(sd, nullptr, &scratch);
		}
		std::vector<BLASBuildTriangleData> data;
		for (uint32_t g = 0; g < WIND_GROUPS; g++) {
			data = wm.data;
			for (auto& d : data)
				d.VertexOffset += (uint64_t)g * wm.stride * 12;
			BuildBLASAttribs ba;
			ba.pBLAS = wm.blas[g];
			ba.BLASTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
			ba.GeometryTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
			ba.pTriangleData = data.data();
			ba.TriangleDataCount = (Uint32)data.size();
			ba.pScratchBuffer = scratch;
			ba.ScratchBufferTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
			ctx->BuildBLAS(ba);
			blasGeometryBase[wm.blas[g].RawPtr()] = base;
		}
		std::vector<StateTransitionDesc> barriers;
		for (auto& b : wm.blas)
			barriers.push_back(StateTransitionDesc(b, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_BUILD_AS_READ, STATE_TRANSITION_FLAG_UPDATE_STATE));
		ctx->TransitionResourceStates((Uint32)barriers.size(), barriers.data());
		structuresChanged = true;
		printf("Tree wind: a mesh of %u vertices bent for its shadows (%zu meshes, %.1f MiB of refit scratch)\n", wm.vertexCount, windMeshes.size(), windScratchNeed / 1048576.0);
		it = windMeshes.find(key);
	}
	WindMesh& wm = it->second;
	wm.lastUse = frameCounter;
	if (collectingStill)
		wm.inStill = true;
	return wm.blas[group % WIND_GROUPS].RawPtr();
}

void DiligentRayShadows::add(IBottomLevelAS* b, const Matrix& transform)
{
	if (!b)
		return;
	if (b != lastAdded) {
		lastAdded = b;
		lastAddedBase = blasGeometryBase[b];
	}
	instances.push_back({ b, transform, lastAddedBase });
}

void DiligentRayShadows::build(IDeviceContext* ctx)
{
	const auto t0 = std::chrono::steady_clock::now();
	struct Done { FrameStats& s; std::chrono::steady_clock::time_point t0; ~Done() { s.tlasMs += MsSince(t0); } } done{ stats, t0 };
	lastInstanceCount = instances.size() + stillCasters;
	const bool withTerrain = false; // (in the still structure: setForest)
	// the same casters as the last build: its structure stays (with 30,000 trees the build took
	// 10-20 ms a frame on the CPU; a forest doesn't move)
	if (built && !structuresChanged && builtTerrain == terrainBlas.RawPtr() && instances.size() == builtInstances.size()) {
		bool same = true;
		for (size_t i = 0; i < instances.size() && same; i++) {
			const Instance& a = instances[i];
			const Instance& b = builtInstances[i];
			same = a.blas == b.blas && a.geometryBase == b.geometryBase && memcmp(&a.transform, &b.transform, sizeof(Matrix)) == 0;
		}
		if (same) {
			uploadAlpha(ctx);
			built = movingBuilt || stillBuilt;
			return;
		}
	}
	movingBuilt = false;
	built = stillBuilt;
	structuresChanged = false;
	builtInstances = instances;
	builtTerrain = terrainBlas.RawPtr();
	if (instances.empty() && !withTerrain) {
		uploadAlpha(ctx);
		return;
	}
	const Uint32 count = (Uint32)instances.size() + (withTerrain ? 1 : 0);
	if (count > tlasCapacity) {
		tlasCapacity = std::max<Uint32>(count * 2, 1024);
		stats.tlasGrown++;
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
	movingBuilt = true;
	built = true;
}

void DiligentRayShadows::setForest(IDeviceContext* ctx, const Forest* forest, bool rebuilt, const std::vector<uint32_t>& changed,
	const std::function<IBottomLevelAS*(const ForestTree&)>& blasOf)
{
	const uint32_t slots = forest ? (uint32_t)forest->trees.size() : 0;
	stillAge++;
	if (!changed.empty())
		stillPending = true;
	bool full = rebuilt || slots != stillSlots || stillTerrain != terrainBlas.RawPtr() || !stillTlas || stillWind != windOn();
	// the slots changed are taken in at most every 30 frames (with 30,000 trees an update costs a
	// few ms): a tree felled keeps its shadow for up to half a second; a tree's model not ready
	// (its leaves' texture not uploaded yet) is tried again as often
	if (!full && !((stillPending || stillMissing) && stillAge >= 30))
		return;
	if (stillMissing)
		full = true;
	stillAge = 0;
	stillPending = false;
	stillMissing = 0;
	stillSlots = slots;
	stillTerrain = terrainBlas.RawPtr();
	stillWind = windOn();
	// (the trees' bent structures in it: bent every frame)
	for (auto& wm : windMeshes)
		wm.second.inStill = false;
	collectingStill = true;
	// the instances: a slot each (a tree gone or without a shadow: masked out), then the terrain
	IBottomLevelAS* any = terrainBlas.RawPtr();
	std::vector<IBottomLevelAS*> blasOfSlot(slots, nullptr);
	stillCasters = 0;
	for (uint32_t i = 0; i < slots; i++) {
		const ForestTree& t = forest->trees[i];
		const bool gone = !t.model || (t.hidden && !t.lifted);
		if (gone)
			continue;
		// (no shadow: no structure looked up, Extend map's tens of thousands of trees)
		if (!(t.flags & (SceneEntity::SEFLAG_STATIC_SHADOW | SceneEntity::SEFLAG_DYNAMIC_SHADOW)))
			continue;
		IBottomLevelAS* b = blasOf(t);
		if (!b) {
			stillMissing++;
			continue;
		}
		blasOfSlot[i] = b;
		any = any ? any : b;
		stillCasters++;
	}
	collectingStill = false;
	const bool withTerrain = terrainBlas != nullptr;
	const Uint32 count = slots + (withTerrain ? 1 : 0);
	if (!any || count == 0 || (stillCasters == 0 && !withTerrain)) {
		stillBuilt = false;
		built = movingBuilt;
		return;
	}
	// a full build when the slots or the terrain changed, else an update (the same instances,
	// their structures, places and masks changed)
	if (stillBuilt && stillCount != count)
		full = true;
	if (full && count > stillCapacity) {
		stillCapacity = count + count / 4 + 64;
		TopLevelASDesc td;
		td.Name = "Still shadow casters";
		td.MaxInstanceCount = stillCapacity;
		td.Flags = RAYTRACING_BUILD_AS_PREFER_FAST_TRACE | RAYTRACING_BUILD_AS_ALLOW_UPDATE;
		stillTlas.Release();
		device->CreateTLAS(td, &stillTlas);
		if (!stillTlas) {
			stillBuilt = false;
			built = movingBuilt;
			return;
		}
		BufferDesc ib;
		ib.Name = "Still TLAS instances";
		ib.Size = (Uint64)TLAS_INSTANCE_DATA_SIZE * stillCapacity;
		ib.BindFlags = BIND_RAY_TRACING;
		stillInstanceBuffer.Release();
		device->CreateBuffer(ib, nullptr, &stillInstanceBuffer);
		BufferDesc sd;
		sd.Name = "Still TLAS scratch";
		sd.Size = std::max(stillTlas->GetScratchBufferSizes().Build, stillTlas->GetScratchBufferSizes().Update);
		sd.BindFlags = BIND_RAY_TRACING;
		stillScratch.Release();
		device->CreateBuffer(sd, nullptr, &stillScratch);
	}
	while (stillNames.size() < count)
		stillNames.push_back("s" + std::to_string(stillNames.size()));
	std::vector<TLASBuildInstanceData> data(count);
	for (uint32_t i = 0; i < slots; i++) {
		TLASBuildInstanceData& d = data[i];
		d.InstanceName = stillNames[i].c_str();
		IBottomLevelAS* b = blasOfSlot[i];
		d.pBLAS = b ? b : any;
		const Matrix& m = forest->trees[i].transform;
		if (b) {
			for (int r = 0; r < 3; r++) {
				for (int c = 0; c < 3; c++)
					d.Transform.data[r][c] = m.m[c][r];
				d.Transform.data[r][3] = m.m[3][r];
			}
		}
		else {
			// a slot without a shadow: masked out, and shrunk to a point far below the world.
			// (At the identity it had the terrain's box, the whole map: with Extend map's 57,000
			// trees in the forest every shadow ray went through 57,000 of them, 900 ms a frame.)
			for (int r = 0; r < 3; r++) {
				for (int c = 0; c < 3; c++)
					d.Transform.data[r][c] = 0.0f;
				d.Transform.data[r][3] = -100000.0f;
			}
		}
		d.Mask = b ? 0x01 : 0x00;
		d.CustomId = b ? blasGeometryBase[b] : 0;
		d.Flags = RAYTRACING_INSTANCE_TRIANGLE_FACING_CULL_DISABLE; // leaves seen from either side
	}
	if (withTerrain) {
		// in world space already; opaque, so its CustomId isn't read
		TLASBuildInstanceData& d = data[count - 1];
		d.InstanceName = stillNames[count - 1].c_str();
		d.pBLAS = terrainBlas;
		d.Transform = InstanceMatrix{};
		d.Mask = 0x02;
		d.CustomId = 0;
		d.Flags = RAYTRACING_INSTANCE_NONE;
	}
	BuildTLASAttribs ta;
	ta.pTLAS = stillTlas;
	ta.TLASTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
	ta.BLASTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
	ta.pInstances = data.data();
	ta.InstanceCount = count;
	ta.pInstanceBuffer = stillInstanceBuffer;
	ta.InstanceBufferTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
	ta.pScratchBuffer = stillScratch;
	ta.ScratchBufferTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
	ta.BindingMode = HIT_GROUP_BINDING_MODE_PER_TLAS;
	ta.Update = !full && stillBuilt;
	ctx->BuildTLAS(ta);
	stillCount = count;
	stillBuilt = true;
	built = true;
	uploadAlpha(ctx);
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
	// only the heights changed (a building levelled its ground): the positions copied into the
	// buffer, the structure built again in place (none of the allocations, nor the indices)
	const bool inPlace = terrain == terrainKey && terrainBlas && terrainVertices && terrainIndices
		&& terrain->width == terrainW && terrain->height == terrainH;
	const auto t0 = std::chrono::steady_clock::now();
	struct Done { FrameStats& s; std::chrono::steady_clock::time_point t0; ~Done() { s.terrain++; s.terrainMs += MsSince(t0); } } done{ stats, t0 };
	terrainKey = terrain;
	terrainVersion = version;
	if (!inPlace)
		terrainBlas.Release();
	structuresChanged = true;
	// (in place it keeps its address: the still structure told to build again with it)
	stillTerrain = nullptr;
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
	const uint32_t primitives = (uint32_t)w * h * 2;
	auto buildTerrain = [&]() {
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
		b.GeometryName = "terrain";
		b.pVertexBuffer = terrainVertices;
		b.VertexStride = 12;
		b.VertexCount = vertexCount;
		b.VertexValueType = VT_FLOAT32;
		b.VertexComponentCount = 3;
		b.PrimitiveCount = primitives;
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
	};
	if (inPlace) {
		ctx->UpdateBuffer(terrainVertices, 0, positions.size() * 4, positions.data(), RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
		buildTerrain();
		return;
	}
	terrainW = w;
	terrainH = h;
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
	auto make = [&](const char* name, const void* data, uint64_t bytes, RefCntAutoPtr<IBuffer>& buffer, USAGE usage) {
		BufferDesc bd;
		bd.Name = name;
		bd.Size = bytes;
		bd.BindFlags = BIND_RAY_TRACING;
		bd.Usage = usage;
		BufferData init{ data, bytes };
		buffer.Release();
		device->CreateBuffer(bd, &init, &buffer);
	};
	// (the vertices updatable: the heights change in play)
	make("Ray tracing: terrain vertices", positions.data(), positions.size() * 4, terrainVertices, USAGE_DEFAULT);
	make("Ray tracing: terrain indices", indices.data(), indices.size() * 4, terrainIndices, USAGE_IMMUTABLE);
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
	buildTerrain();
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
	const Matrix& inverseView, const Vector3& sunDirection, uint64_t frameIndex, IBuffer* fogBuffer,
	ITextureView* heights, int terrainEdge)
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
	if (!srb || boundDepth != depth || boundTlas != primary() || boundTlas2 != secondary() || maskAlphaVersion != alphaVersion || boundFog != fogBuffer
		|| boundHeights != heights) {
		srb.Release();
		signature->CreateShaderResourceBinding(&srb, true);
		srb->GetVariableByName(SHADER_TYPE_PIXEL, "FogBuffer")->Set(fogBuffer);
		boundFog = fogBuffer;
		srb->GetVariableByName(SHADER_TYPE_PIXEL, "tDepth")->Set(depth->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
		// (without the terrain's heights the depth stands in, unread: SunDir.w is 0)
		srb->GetVariableByName(SHADER_TYPE_PIXEL, "tHeights")->Set(heights ? heights : depth->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
		boundHeights = heights;
		srb->GetVariableByName(SHADER_TYPE_PIXEL, "tlas")->Set(primary());
		srb->GetVariableByName(SHADER_TYPE_PIXEL, "tlas2")->Set(secondary());
		bindAlpha(srb);
		maskAlphaVersion = alphaVersion;
		boundDepth = depth;
		boundTlas = primary();
		boundTlas2 = secondary();
	}
	void* mapped = nullptr;
	ctx->MapBuffer(constants, MAP_WRITE, MAP_FLAG_DISCARD, mapped);
	const Vector3 sun = sunDirection.normal();
	float* f = (float*)mapped;
	const float head[12] = {
		projection[0], projection[1], projection[2], projection[3],
		1.0f / dd.Width, 1.0f / dd.Height, (float)(frameIndex % 64), 0.0093f, // the sun's disc: 0.53 degrees across
		sun.x, sun.y, sun.z, heights ? terrainEdge + 1.0f : 0.0f,
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
			{ SHADER_TYPE_PIXEL, "tlas2", 1, SHADER_RESOURCE_TYPE_ACCEL_STRUCT, SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
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
		ShaderCache::CreateGraphicsPipeline(device, ci, &reflectPipeline);
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
	if (!reflectSrb || reflectBound[0] != scene || reflectBound[1] != sceneDepth || reflectBoundTlas != primary() || reflectBoundTlas2 != secondary() || reflectAlphaVersion != alphaVersion) {
		reflectSrb.Release();
		reflectSignature->CreateShaderResourceBinding(&reflectSrb, true);
		reflectSrb->GetVariableByName(SHADER_TYPE_PIXEL, "tScene")->Set(scene->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
		reflectSrb->GetVariableByName(SHADER_TYPE_PIXEL, "tDepth")->Set(sceneDepth->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
		reflectSrb->GetVariableByName(SHADER_TYPE_PIXEL, "tlas")->Set(primary());
		reflectSrb->GetVariableByName(SHADER_TYPE_PIXEL, "tlas2")->Set(secondary());
		bindAlpha(reflectSrb);
		reflectAlphaVersion = alphaVersion;
		reflectBound[0] = scene;
		reflectBound[1] = sceneDepth;
		reflectBoundTlas = primary();
		reflectBoundTlas2 = secondary();
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

// The reflections on buildings and units, Ultra (RenderSettings::modelReflections): per pixel of
// the depth pre-pass's depth (at half its size) the surface's mirror ray, the nearest model it
// hits; its colour from the frame before where the hit was on the screen and is in sight now
// (the screen-space reflections' frame), else marked unknown (the model shader darkens its own
// reflection there: a wall stands in the way). The models only, not the terrain: past them the
// shaders' sky and ground. Without the normal maps (the shape's normal from the depth).
static const char modelReflShaderCode[] = ALPHA_TEST_CODE R"---(
RaytracingAccelerationStructure tlas;
RaytracingAccelerationStructure tlas2; // the still casters: the forest, the terrain (DiligentRayShadows::setForest)
Texture2D<float> tDepth;
Texture2D tScene;
SamplerState sLinear;

cbuffer ModelReflBuffer
{
	float4 Proj;          // P11, P22, P33, P43
	float4 Texel;         // 1 / the depth's width, height; the rays' reach
	float4x4 InverseView; // view -> world (row vectors)
	float4x4 ViewProj;    // this frame's
	float4x4 PrevViewProj; // the frame before's
};

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

float4 PS(VS_OUTPUT input) : SV_Target
{
	float2 size = 1 / Texel.xy;
	int2 px = int2(input.Uv * size);
	float2 uv = (px + 0.5) * Texel.xy;
	float d = tDepth.Load(int3(px, 0));
	if (d >= 0.99999)
		return float4(0, 0, 0, 0);
	float3 p = WorldAt(uv, d);
	float3 px1 = WorldAt(uv + float2(Texel.x, 0), tDepth.Load(int3(px + int2(1, 0), 0)));
	float3 px0 = WorldAt(uv - float2(Texel.x, 0), tDepth.Load(int3(px - int2(1, 0), 0)));
	float3 py1 = WorldAt(uv + float2(0, Texel.y), tDepth.Load(int3(px + int2(0, 1), 0)));
	float3 py0 = WorldAt(uv - float2(0, Texel.y), tDepth.Load(int3(px - int2(0, 1), 0)));
	float3 dx = distance(px1, p) < distance(px0, p) ? px1 - p : p - px0;
	float3 dy = distance(py1, p) < distance(py0, p) ? py1 - p : p - py0;
	float3 n = normalize(cross(dy, dx));
	float3 camPos = InverseView[3].xyz;
	float3 v = normalize(camPos - p);
	if (dot(n, v) < 0)
		n = -n;
	float3 r = reflect(-v, n);
	float dist = length(camPos - p);
	RayDesc ray;
	ray.Origin = p + n * (0.03 + dist * 0.0006);
	ray.TMin = 0.02;
	ray.Direction = r;
	ray.TMax = Texel.z;
	RayQuery<RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> rq;
	rq.TraceRayInline(tlas, RAY_FLAG_NONE, 0x01, ray); // the models only, not the terrain
)---" ALPHA_TEST_LOOP(rq) R"---(
	bool any = rq.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
	float t = any ? rq.CommittedRayT() : ray.TMax;
	RayDesc ray2 = ray;
	ray2.TMax = t;
	RayQuery<RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> rq2;
	rq2.TraceRayInline(tlas2, RAY_FLAG_NONE, 0x01, ray2);
)---" ALPHA_TEST_LOOP(rq2) R"---(
	if (rq2.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
		any = true;
		t = rq2.CommittedRayT();
	}
	if (!any)
		return float4(0, 0, 0, 0);
	float3 hit = ray.Origin + r * t;
	// in sight now: the hit's depth against the pre-pass's where it is on the screen
	float seen = 0.35;
	float4 c = mul(float4(hit, 1), ViewProj);
	if (c.w > 0.05) {
		float2 cuv = c.xy / c.w * float2(0.5, -0.5) + 0.5;
		if (all(cuv >= 0) && all(cuv <= 1)) {
			float sceneZ = Proj.w / (tDepth.Load(int3(cuv * size, 0)) - Proj.z);
			seen = abs(sceneZ - c.w) < 0.5 + c.w * 0.02 ? 1 : 0.35;
		}
	}
	// its colour in the frame before
	float4 q = mul(float4(hit, 1), PrevViewProj);
	if (q.w <= 0.05)
		return float4(0, 0, 0, 1);
	float2 quv = q.xy / q.w * float2(0.5, -0.5) + 0.5;
	if (any(quv < 0) || any(quv > 1))
		return float4(0, 0, 0, 1);
	float2 edge = min(quv, 1 - quv);
	float known = seen * saturate(min(edge.x, edge.y) * 20);
	return float4(tScene.SampleLevel(sLinear, quv, 0).rgb, 1 + known);
}
)---";

ITextureView* DiligentRayShadows::renderModelReflections(IDeviceContext* ctx, ITexture* depth, const float projection[4],
	const Matrix& inverseView, const Matrix& viewProj, ITexture* prevScene, const Matrix& prevViewProj)
{
	if (!built || !depth || !prevScene)
		return nullptr;
	if (!modelReflTried) {
		modelReflTried = true;
		const PipelineResourceDesc resources[] = {
			{ SHADER_TYPE_PIXEL, "ModelReflBuffer", 1, SHADER_RESOURCE_TYPE_CONSTANT_BUFFER, SHADER_RESOURCE_VARIABLE_TYPE_STATIC },
			{ SHADER_TYPE_PIXEL, "tDepth", 1, SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
			{ SHADER_TYPE_PIXEL, "tScene", 1, SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
			{ SHADER_TYPE_PIXEL, "tlas", 1, SHADER_RESOURCE_TYPE_ACCEL_STRUCT, SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
			{ SHADER_TYPE_PIXEL, "tlas2", 1, SHADER_RESOURCE_TYPE_ACCEL_STRUCT, SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
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
		sig.Name = "Ray-traced model reflections";
		sig.Resources = resources;
		sig.NumResources = (Uint32)std::size(resources);
		sig.ImmutableSamplers = samplers;
		sig.NumImmutableSamplers = (Uint32)std::size(samplers);
		device->CreatePipelineResourceSignature(sig, &modelReflSignature);
		BufferDesc cb;
		cb.Name = "Ray-traced model reflections";
		cb.Size = 224;
		cb.BindFlags = BIND_UNIFORM_BUFFER;
		cb.Usage = USAGE_DYNAMIC;
		cb.CPUAccessFlags = CPU_ACCESS_WRITE;
		device->CreateBuffer(cb, nullptr, &modelReflConstants);
		if (!modelReflSignature || !modelReflConstants)
			return nullptr;
		modelReflSignature->GetStaticVariableByName(SHADER_TYPE_PIXEL, "ModelReflBuffer")->Set(modelReflConstants);
		auto shader = [&](SHADER_TYPE type, const char* entry) {
			ShaderCreateInfo sci;
			sci.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
			sci.ShaderCompiler = SHADER_COMPILER_DXC;
			sci.HLSLVersion = ShaderVersion{ 6, 5 };
			sci.Source = modelReflShaderCode;
			sci.SourceLength = sizeof(modelReflShaderCode) - 1;
			sci.EntryPoint = entry;
			sci.Desc.ShaderType = type;
			sci.Desc.Name = "Ray-traced model reflections";
			RefCntAutoPtr<IShader> s;
			ShaderCache::CreateShader(device, sci, &s);
			if (!s)
				printf("Ray-traced model reflections: could not compile %s (DXC, shader model 6.5): off\n", entry);
			return s;
		};
		RefCntAutoPtr<IShader> vs = shader(SHADER_TYPE_VERTEX, "VS"), ps = shader(SHADER_TYPE_PIXEL, "PS");
		if (!vs || !ps)
			return nullptr;
		GraphicsPipelineStateCreateInfo ci;
		ci.PSODesc.Name = "Ray-traced model reflections";
		ci.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;
		auto& gp = ci.GraphicsPipeline;
		gp.NumRenderTargets = 1;
		gp.RTVFormats[0] = TEX_FORMAT_RGBA16_FLOAT;
		gp.DSVFormat = TEX_FORMAT_UNKNOWN;
		gp.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		gp.RasterizerDesc.CullMode = CULL_MODE_NONE;
		gp.DepthStencilDesc.DepthEnable = False;
		gp.DepthStencilDesc.DepthWriteEnable = False;
		ci.pVS = vs;
		ci.pPS = ps;
		IPipelineResourceSignature* sigs[] = { modelReflSignature };
		ci.ppResourceSignatures = sigs;
		ci.ResourceSignaturesCount = 1;
		ShaderCache::CreateGraphicsPipeline(device, ci, &modelReflPipeline);
		printf("Ray-traced model reflections: %s\n", modelReflPipeline ? "ready" : "could not create the pipeline: off");
	}
	if (!modelReflPipeline)
		return nullptr;
	const TextureDesc& dd = depth->GetDesc();
	const Uint32 w = std::max<Uint32>(dd.Width / 2, 1), h = std::max<Uint32>(dd.Height / 2, 1);
	if (!modelRefl || modelRefl->GetDesc().Width != w || modelRefl->GetDesc().Height != h) {
		TextureDesc td;
		td.Name = "Ray-traced model reflections";
		td.Type = RESOURCE_DIM_TEX_2D;
		td.Width = w;
		td.Height = h;
		td.Format = TEX_FORMAT_RGBA16_FLOAT;
		td.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;
		modelRefl.Release();
		device->CreateTexture(td, nullptr, &modelRefl);
		if (!modelRefl)
			return nullptr;
	}
	if (!modelReflSrb || modelReflBound[0] != depth || modelReflBound[1] != prevScene || modelReflBoundTlas != primary() || modelReflBoundTlas2 != secondary()
		|| modelReflAlphaVersion != alphaVersion) {
		modelReflSrb.Release();
		modelReflSignature->CreateShaderResourceBinding(&modelReflSrb, true);
		modelReflSrb->GetVariableByName(SHADER_TYPE_PIXEL, "tDepth")->Set(depth->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
		modelReflSrb->GetVariableByName(SHADER_TYPE_PIXEL, "tScene")->Set(prevScene->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
		modelReflSrb->GetVariableByName(SHADER_TYPE_PIXEL, "tlas")->Set(primary());
		modelReflSrb->GetVariableByName(SHADER_TYPE_PIXEL, "tlas2")->Set(secondary());
		bindAlpha(modelReflSrb);
		modelReflAlphaVersion = alphaVersion;
		modelReflBound[0] = depth;
		modelReflBound[1] = prevScene;
		modelReflBoundTlas = primary();
		modelReflBoundTlas2 = secondary();
	}
	void* mapped = nullptr;
	ctx->MapBuffer(modelReflConstants, MAP_WRITE, MAP_FLAG_DISCARD, mapped);
	float* f = (float*)mapped;
	const float head[8] = { projection[0], projection[1], projection[2], projection[3], 1.0f / dd.Width, 1.0f / dd.Height, 150.0f, 0 };
	memcpy(f, head, sizeof(head));
	const Matrix iv = inverseView.getTranspose();
	memcpy(f + 8, &iv, sizeof(Matrix));
	memcpy(f + 24, &viewProj, sizeof(Matrix));
	memcpy(f + 40, &prevViewProj, sizeof(Matrix));
	ctx->UnmapBuffer(modelReflConstants, MAP_WRITE);
	ITextureView* rtv = modelRefl->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
	ctx->SetRenderTargets(1, &rtv, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	ctx->SetViewports(1, nullptr, 0, 0);
	ctx->SetPipelineState(modelReflPipeline);
	ctx->CommitShaderResources(modelReflSrb, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	ctx->Draw(DrawAttribs{ 3, DRAW_FLAG_VERIFY_ALL });
	return modelRefl->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
}
