// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#include "DiligentGI.h"
#include "../util/LoadingProgress.h"
#include "DiligentClouds.h"
#include "renderer_diligent.h"
#include "RenderSettings.h"
#include "../util/util.h"
#include "../Environment.h"
#include "../util/FrameStats.h"
#include <algorithm>
#include <cstring>
#include <iterator>
#include <string>
#include <vector>

using namespace Diligent;

// The radiance pass: per texel of the bounce map, the light leaving the ground or what stands on it
static const char giShaderCode[] = R"---(
cbuffer GIBuffer
{
	float4 GIAlbedoMap;  // the terrain's albedo map: its corner x, z, 1 / its width, 1 / its depth (world units)
	float4 GIWeather;    // snow cover, wetness (wkbai's weather; 0 without), the second bounce on, the bounce's strength
	float4 GIPrevRegion; // the last frame's bounce map (mip 2, giPrevTexture): its corner x, z, 1 / its size, on
	float4 GIFireCount;  // x: how many of GIFires are in use
	float4 GIFires[32];  // per fire: position and radius, colour
};
Texture2D<float> heightTexture; // the terrain's vertex heights
Texture2D terrainAlbedo;        // the terrain's colour from above, the whole map (the terrain renderer's)
Texture2D modelCapture;         // the models from above: albedo, the sun's n.l (alpha below 0: none)
Texture2D<float> modelDepth;    // and their depth
Texture2D modelEmission;        // and the light they give off (their emissive maps', at this hour)
Texture2D giPrevTexture;       // the last frame's bounce map at mip 2
SamplerState heightSampler;     // clamped, linear
SamplerState pointSampler;      // clamped, point

struct VS_OUTPUT
{
	float4 Pos : SV_POSITION;
};

VS_OUTPUT VS(uint id : SV_VertexID)
{
	VS_OUTPUT o;
	float2 p = float2((id << 1) & 2, id & 2) * 2 - 1;
	o.Pos = float4(p, 0, 1);
	return o;
}

float GroundHeight(float2 xz)
{
	float2 g = xz / 5 + SkyOccTerrain.z;
	return heightTexture.SampleLevel(heightSampler, (g + 0.5) / SkyOccTerrain.xy, 0);
}

// The sun's (or the moon's) light reaching p past the terrain and the buildings: its ray marched
// through the sky light's map of their tops, in 13 steps growing by 55% out to ~400 units (long
// evening shadows), softened with the distance; beyond the map, lit
float SunVisible(float3 p, float3 l)
{
	if (SkyOccRegion.w == 0 || l.y <= 0.01)
		return 1;
	float vis = 1;
	float t = 1.0;
	[loop] for (int i = 0; i < 13; i++) {
		float3 q = p + l * t;
		float2 ouv = (q.xz - SkyOccRegion.xy) * SkyOccRegion.z;
		if (any(ouv < 0) || any(ouv > 1) || q.y > SkyOccTerrain.w)
			break;
		float top = skyOccTexture.SampleLevel(skyOccSampler, ouv, 0).b;
		vis = min(vis, saturate(1 + (q.y - top) / (0.06 * t + 0.4)));
		if (vis <= 0)
			break;
		t *= 1.55;
	}
	return vis;
}

float3 GIFireLight(float3 pos, float3 n)
{
	float3 sum = float3(0, 0, 0);
	for (int i = 0; i < (int)GIFireCount.x; i++) {
		float4 at = GIFires[2 * i];
		float3 d = at.xyz - pos;
		float dist = length(d);
		float fade = saturate(1 - dist / at.w);
		float wrap = saturate(dot(n, d / max(dist, 1e-3))) * 0.7 + 0.3;
		sum += GIFires[2 * i + 1].rgb * (fade * fade * wrap);
	}
	return sum;
}

float4 PS(VS_OUTPUT input) : SV_Target
{
	float2 uv = input.Pos.xy / 256; // ShadowEnvironment::GI_SIZE
	float2 xz = GIRegion.xy + uv / GIRegion.z;
	float ground = GroundHeight(xz);
	const float d = 2.0;
	float3 n = normalize(float3(GroundHeight(xz - float2(d, 0)) - GroundHeight(xz + float2(d, 0)), 2 * d,
		GroundHeight(xz - float2(0, d)) - GroundHeight(xz + float2(0, d))));
	// the ground: its albedo from above, in the weather (wet darker, snow on the level ground)
	float2 auv = (xz - GIAlbedoMap.xy) * GIAlbedoMap.zw;
	auv.y = 1 - auv.y;
	float3 albedo = terrainAlbedo.SampleLevel(heightSampler, auv, 0).rgb;
	albedo *= 1 - 0.3 * GIWeather.y;
	albedo = lerp(albedo, float3(0.85, 0.87, 0.9), saturate(GIWeather.x * 1.5) * saturate(n.y * 4 - 3));
	float nl = saturate(dot(n, CloudSun.xyz));
	float top = ground;
	// a roof, a tree or a unit on it (their albedo with the weather, as drawn)
	float2 cuv = float2(uv.x, 1 - uv.y);
	float4 cap = modelCapture.SampleLevel(pointSampler, cuv, 0);
	float3 emitted = float3(0, 0, 0);
	if (cap.a >= 0) {
		emitted = modelEmission.SampleLevel(pointSampler, cuv, 0).rgb;
		float dep = modelDepth.SampleLevel(pointSampler, cuv, 0);
		top = max(ground, SkyOccTerrain.w - dep * SkyOccLight.w);
		albedo = cap.rgb;
		nl = cap.a;
		n = float3(0, 1, 0);
	}
	float3 p = float3(xz, top + 0.3);
	// as the terrain's sky light: the sun's share with its shadows (some light left in them), the
	// sky's as much as the point sees of it, the fires'
	float sunVis = nl > 0 ? SunVisible(p, CloudSun.xyz) : 0;
	float cloud = CloudSun.w > 0 ? CloudShadow(p) : 1;
	float share = SkyOccLight.x;
	float vis = SkyVisibility(p, false);
	float3 light = (1 - share) * 1.05 * CloudSunColour.rgb * nl * lerp(SkyOccLight.z, 1, sunVis) * cloud
		+ share * SkyAmbient(n) * vis + GIFireLight(p, n);
	// High: the second bounce, the last frame's light around it onto the hidden part of its sky
	if (GIWeather.z > 0 && GIPrevRegion.w > 0) {
		float2 puv = (xz - GIPrevRegion.xy) * GIPrevRegion.z;
		if (all(puv >= 0) && all(puv <= 1))
			light += giPrevTexture.SampleLevel(heightSampler, puv, 0).rgb * ((1 - vis) * 0.8 + 0.1) * GIWeather.w;
	}
	// the light it gives off (lit windows at night) on top of what it reflects: a quarter, for
	// the light that comes back off the walls and the ground about it (the windows' own light
	// on them is WindowLight's)
	return float4(albedo * light + emitted * 0.25, top);
}
)---";

void CreateGIResources(DiligentGfx* gfx)
{
	IRenderDevice* device = gfx->device;
	auto& env = gfx->shadows;
	using Env = DiligentGfx::ShadowEnvironment;

	auto texture = [&](const char* name, int size, TEXTURE_FORMAT format, BIND_FLAGS bind, bool mips, RefCntAutoPtr<ITexture>& out) {
		TextureDesc desc;
		desc.Name = name;
		desc.Type = RESOURCE_DIM_TEX_2D;
		desc.Width = desc.Height = size;
		desc.Format = format;
		desc.BindFlags = bind;
		desc.MipLevels = mips ? 0 : 1;
		if (mips)
			desc.MiscFlags = MISC_TEXTURE_FLAG_GENERATE_MIPS;
		device->CreateTexture(desc, nullptr, &out);
		if (!out)
			ferr("Diligent: could not create the dynamic GI's %s.", name);
	};
	texture("bounce map", Env::GI_SIZE, TEX_FORMAT_RGBA16_FLOAT, BIND_SHADER_RESOURCE | BIND_RENDER_TARGET, true, env.giRadiance);
	texture("last bounce map", Env::GI_PREV_SIZE, TEX_FORMAT_RGBA16_FLOAT, BIND_SHADER_RESOURCE, false, env.giPrev);
	texture("models from above", Env::GI_SIZE, TEX_FORMAT_RGBA16_FLOAT, BIND_SHADER_RESOURCE | BIND_RENDER_TARGET, false, env.giCapture);
	texture("models' light from above", Env::GI_SIZE, TEX_FORMAT_RGBA16_FLOAT, BIND_SHADER_RESOURCE | BIND_RENDER_TARGET, false, env.giCaptureEmission);
	texture("models' light, mips", Env::GI_SIZE, TEX_FORMAT_RGBA16_FLOAT, BIND_SHADER_RESOURCE | BIND_RENDER_TARGET, true, env.giEmission);
	texture("models' depth from above",Env::GI_SIZE, TEX_FORMAT_D32_FLOAT, BIND_SHADER_RESOURCE | BIND_DEPTH_STENCIL, false, env.giCaptureDepth);
	{
		// the terrain's albedo until the terrain renderer bakes it: a mid grey-green
		const uint8_t px[4] = { 90, 100, 70, 255 };
		TextureDesc desc;
		desc.Name = "Terrain albedo placeholder";
		desc.Type = RESOURCE_DIM_TEX_2D;
		desc.Width = desc.Height = 1;
		desc.Format = TEX_FORMAT_RGBA8_UNORM;
		desc.BindFlags = BIND_SHADER_RESOURCE;
		TextureSubResData sub{ px, 4 };
		TextureData data{ &sub, 1 };
		device->CreateTexture(desc, &data, &env.giAlbedo);
	}
	// cleared: black, at the ground (the bounce map); no light given off
	for (ITexture* t : { env.giRadiance.RawPtr(), env.giEmission.RawPtr() }) {
		const float zero[4] = { 0, 0, 0, 0 };
		ITextureView* rtv = t->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
		gfx->ctx->SetRenderTargets(1, &rtv, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
		gfx->ctx->ClearRenderTarget(rtv, zero, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
		gfx->ctx->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
		gfx->ctx->GenerateMips(t->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
	}

	BufferDesc cb;
	cb.Name = "Dynamic GI";
	cb.Size = 64 + 32 * 16;
	cb.BindFlags = BIND_UNIFORM_BUFFER;
	cb.Usage = USAGE_DYNAMIC;
	cb.CPUAccessFlags = CPU_ACCESS_WRITE;
	device->CreateBuffer(cb, nullptr, &env.giBuffer);
	if (!env.giBuffer)
		ferr("Diligent: could not create the dynamic GI's buffer.");

	const PipelineResourceDesc resources[] = {
		{ SHADER_TYPE_PIXEL, "CloudBuffer", 1, SHADER_RESOURCE_TYPE_CONSTANT_BUFFER, SHADER_RESOURCE_VARIABLE_TYPE_STATIC },
		{ SHADER_TYPE_PIXEL, "GIBuffer", 1, SHADER_RESOURCE_TYPE_CONSTANT_BUFFER, SHADER_RESOURCE_VARIABLE_TYPE_STATIC },
		{ SHADER_TYPE_PIXEL, "cloudNoise", 1, SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_STATIC },
		{ SHADER_TYPE_PIXEL, "skyOccTexture", 1, SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_STATIC },
		{ SHADER_TYPE_PIXEL, "modelCapture", 1, SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_STATIC },
		{ SHADER_TYPE_PIXEL, "modelDepth", 1, SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_STATIC },
		{ SHADER_TYPE_PIXEL, "modelEmission", 1, SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_STATIC },
		{ SHADER_TYPE_PIXEL, "giPrevTexture", 1, SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_STATIC },
		{ SHADER_TYPE_PIXEL, "heightTexture", 1, SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
		{ SHADER_TYPE_PIXEL, "terrainAlbedo", 1, SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
	};
	SamplerDesc linear;
	linear.AddressU = linear.AddressV = linear.AddressW = TEXTURE_ADDRESS_CLAMP;
	SamplerDesc point = linear;
	point.MinFilter = point.MagFilter = point.MipFilter = FILTER_TYPE_POINT;
	SamplerDesc wrap;
	wrap.AddressU = wrap.AddressV = wrap.AddressW = TEXTURE_ADDRESS_WRAP;
	const ImmutableSamplerDesc samplers[] = { { SHADER_TYPE_PIXEL, "heightSampler", linear }, { SHADER_TYPE_PIXEL, "pointSampler", point },
		{ SHADER_TYPE_PIXEL, "cloudNoiseSampler", wrap }, { SHADER_TYPE_PIXEL, "skyOccSampler", linear } };
	PipelineResourceSignatureDesc sig;
	sig.Name = "Dynamic GI";
	sig.Resources = resources;
	sig.NumResources = (Uint32)std::size(resources);
	sig.ImmutableSamplers = samplers;
	sig.NumImmutableSamplers = (Uint32)std::size(samplers);
	device->CreatePipelineResourceSignature(sig, &env.giSignature);
	if (!env.giSignature)
		ferr("Diligent: could not create the dynamic GI's signature.");
	env.giSignature->GetStaticVariableByName(SHADER_TYPE_PIXEL, "CloudBuffer")->Set(env.cloudBuffer);
	env.giSignature->GetStaticVariableByName(SHADER_TYPE_PIXEL, "GIBuffer")->Set(env.giBuffer);
	env.giSignature->GetStaticVariableByName(SHADER_TYPE_PIXEL, "cloudNoise")->Set(env.cloudNoise->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
	env.giSignature->GetStaticVariableByName(SHADER_TYPE_PIXEL, "skyOccTexture")->Set(env.skyOcc->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
	env.giSignature->GetStaticVariableByName(SHADER_TYPE_PIXEL, "modelCapture")->Set(env.giCapture->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
	env.giSignature->GetStaticVariableByName(SHADER_TYPE_PIXEL, "modelDepth")->Set(env.giCaptureDepth->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
	env.giSignature->GetStaticVariableByName(SHADER_TYPE_PIXEL, "modelEmission")->Set(env.giCaptureEmission->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
	env.giSignature->GetStaticVariableByName(SHADER_TYPE_PIXEL, "giPrevTexture")->Set(env.giPrev->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));

	const std::string source = std::string(g_cloudShaderCode) + giShaderCode;
	auto shader = [&](SHADER_TYPE type, const char* entry) {
		ShaderCreateInfo sci;
		sci.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
		sci.Source = source.c_str();
		sci.SourceLength = source.size();
		sci.EntryPoint = entry;
		sci.Desc.ShaderType = type;
		sci.Desc.Name = "Dynamic GI";
		RefCntAutoPtr<IShader> s;
		LoadingCompilingShader(entry); // (the loading screen)
		device->CreateShader(sci, &s);
		if (!s)
			ferr("Diligent: failed to compile the dynamic GI shader %s", entry);
		return s;
	};
	RefCntAutoPtr<IShader> vs = shader(SHADER_TYPE_VERTEX, "VS"), ps = shader(SHADER_TYPE_PIXEL, "PS");
	GraphicsPipelineStateCreateInfo ci;
	ci.PSODesc.Name = "Dynamic GI radiance";
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
	IPipelineResourceSignature* sigs[] = { env.giSignature };
	ci.ppResourceSignatures = sigs;
	ci.ResourceSignaturesCount = 1;
	device->CreateGraphicsPipelineState(ci, &env.giPipeline);
	if (!env.giPipeline)
		ferr("Diligent: could not create the dynamic GI pipeline.");
}

namespace {
constexpr int GI_QUERY_FRAMES = 4;
RefCntAutoPtr<IQuery> g_giStart[GI_QUERY_FRAMES], g_giEnd[GI_QUERY_FRAMES];
bool g_giIssued[GI_QUERY_FRAMES] = {};
int g_giSlot = 0;
bool g_giQueriesTried = false;
double g_giLastMs = -1.0;
}

void GITimerSkip()
{
	if (g_giStart[0])
		g_giGpuMs = 0.0; // a frame without the GI passes (the map kept)
}

void GITimerBegin(DiligentGfx* gfx)
{
	if (!g_giQueriesTried) {
		g_giQueriesTried = true;
		if (BenchmarkEnabled() && gfx->device->GetDeviceInfo().Features.TimestampQueries == DEVICE_FEATURE_STATE_ENABLED) {
			QueryDesc qd;
			qd.Type = QUERY_TYPE_TIMESTAMP;
			for (int i = 0; i < GI_QUERY_FRAMES; i++) {
				qd.Name = "GI start";
				gfx->device->CreateQuery(qd, &g_giStart[i]);
				qd.Name = "GI end";
				gfx->device->CreateQuery(qd, &g_giEnd[i]);
			}
		}
	}
	if (!g_giStart[0])
		return;
	g_giSlot = (g_giSlot + 1) % GI_QUERY_FRAMES;
	const int q = g_giSlot;
	if (g_giIssued[q]) {
		QueryDataTimestamp a, b;
		if (g_giStart[q]->GetData(&a, sizeof(a), false) && g_giEnd[q]->GetData(&b, sizeof(b), false) && a.Frequency)
			g_giLastMs = (double)(b.Counter - a.Counter) * 1000.0 / (double)a.Frequency;
	}
	g_giGpuMs = g_giLastMs;
	gfx->ctx->EndQuery(g_giStart[q]);
}

void GITimerEnd(DiligentGfx* gfx)
{
	if (!g_giStart[0])
		return;
	gfx->ctx->EndQuery(g_giEnd[g_giSlot]);
	g_giIssued[g_giSlot] = true;
}

void RenderGIRadiance(DiligentGfx* gfx)
{
	auto& env = gfx->shadows;
	IDeviceContext* ctx = gfx->ctx;
	using Env = DiligentGfx::ShadowEnvironment;
	if (!env.giPipeline || !env.terrainHeights)
		return;
	const bool second = env.giMode >= 2;
	// the last frame's map at mip 2 (64 x 64) for the second bounce, with where it lay
	if (second && env.giPrevValid) {
		CopyTextureAttribs copy(env.giRadiance, RESOURCE_STATE_TRANSITION_MODE_TRANSITION, env.giPrev, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
		copy.SrcMipLevel = 2;
		ctx->CopyTexture(copy);
	}

	void* mapped = nullptr;
	ctx->MapBuffer(env.giBuffer, MAP_WRITE, MAP_FLAG_DISCARD, mapped);
	float* f = (float*)mapped;
	std::memset(f, 0, 64 + 32 * 16);
	std::copy_n(env.giAlbedoRegion, 4, f);
	if (g_environment.active && g_renderSettings.weatherEffects >= 1) {
		f[4] = g_environment.snowCover;
		f[5] = g_environment.wetness;
	}
	f[6] = second ? 1.0f : 0.0f;
	f[7] = std::clamp(g_renderSettings.dynamicGIStrength, 0.0f, 3.0f);
	f[8] = env.giPrevOrigin[0];
	f[9] = env.giPrevOrigin[1];
	f[10] = 1.0f / Env::GI_EXTENT;
	f[11] = (second && env.giPrevValid) ? 1.0f : 0.0f;
	const std::vector<float>& fires = env.fireLights;
	const int numFires = (int)std::min(fires.size() / 7, (size_t)Env::MAX_FIRE_LIGHTS);
	f[12] = (float)numFires;
	for (int i = 0; i < numFires; i++) {
		std::copy_n(&fires[i * 7], 4, f + 16 + i * 8);
		std::copy_n(&fires[i * 7 + 4], 3, f + 20 + i * 8);
	}
	ctx->UnmapBuffer(env.giBuffer, MAP_WRITE);

	if (!env.giBinding || env.giBoundHeights != env.terrainHeights || env.giBoundAlbedo != env.giAlbedo.RawPtr()) {
		env.giBinding.Release();
		env.giSignature->CreateShaderResourceBinding(&env.giBinding, true);
		env.giBinding->GetVariableByName(SHADER_TYPE_PIXEL, "heightTexture")->Set(env.terrainHeights);
		env.giBinding->GetVariableByName(SHADER_TYPE_PIXEL, "terrainAlbedo")->Set(env.giAlbedo->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
		env.giBoundHeights = env.terrainHeights;
		env.giBoundAlbedo = env.giAlbedo.RawPtr();
	}
	ITextureView* rtv = env.giRadiance->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
	ctx->SetRenderTargets(1, &rtv, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	ctx->SetViewports(1, nullptr, 0, 0);
	ctx->SetPipelineState(env.giPipeline);
	ctx->CommitShaderResources(env.giBinding, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	ctx->Draw(DrawAttribs{ 3, DRAW_FLAG_VERIFY_ALL });
	gfx->RestoreRenderTargets();
	ctx->GenerateMips(env.giRadiance->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
	// the light given off, with mips for WindowLight (a copy: the capture's target is in the
	// scene's bindings while it is drawn)
	CopyTextureAttribs copy{ env.giCaptureEmission, RESOURCE_STATE_TRANSITION_MODE_TRANSITION, env.giEmission, RESOURCE_STATE_TRANSITION_MODE_TRANSITION };
	ctx->CopyTexture(copy);
	ctx->GenerateMips(env.giEmission->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
	env.giPrevOrigin[0] = env.giOrigin[0];
	env.giPrevOrigin[1] = env.giOrigin[1];
	env.giPrevValid = true;
}
