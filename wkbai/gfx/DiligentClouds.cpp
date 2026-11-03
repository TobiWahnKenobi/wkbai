// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#include "DiligentClouds.h"
#include "renderer_diligent.h"
#include "RenderSettings.h"
#include "../Camera.h"
#include "../util/util.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>
#include <string>
#include <vector>

using namespace Diligent;

const char* const g_cloudShaderCode = R"---(
cbuffer CloudBuffer
{
	float4 CloudWind;       // the base layer's UV offset (xy), the detail layer's (zw)
	float4 CloudShape;      // cover 0-1, edge softness, the layer's base altitude, its thickness
	float4 CloudSun;        // the direction to the sun, w: mode (0 off, 1 shadows, 2 light shafts, 3 the clouds in the sky)
	float4 CloudSunColour;  // the sun's colour, w: the share of the sunlight a full cloud takes
	float4 CloudScale;      // UV per world unit of the base and the detail layer, light shaft strength, a clock (s)
	float4 CloudSkyUp;      // the sky's colour above (the clouds' ambient light and the sky light)
	float4 CloudSkyHorizon; // and at the horizon; w: their mean brightness
	float4 SkyOccRegion;    // the sky light's map: its corner x, z, 1 / its size, w: on
	float4 SkyOccLight;     // the sky light's share of the light, the bounce's strength, the sun's light left in a static shadow, the top-down depth's range
	float4 SkyOccTerrain;   // the height grid's vertices across and down, the map edge in tiles, the top-down view's height
};
SamplerState cloudNoiseSampler; // wrapping, linear
SamplerState skyOccSampler;     // clamped, linear
Texture2D cloudNoise;   // r: the base layer, g: the detail, b: cells for the clouds' edges (tiling)
Texture2D skyOccTexture; // r: how much of the sky the ground sees, g: the ground's height, b: the casters' top

// How cloudy the sky is above a point of the map (0-1): two noise layers drifting with the wind,
// cut at the cover
float CloudCover(float2 xz, float lod)
{
	float b = cloudNoise.SampleLevel(cloudNoiseSampler, xz * CloudScale.x + CloudWind.xy, lod).r;
	float d = cloudNoise.SampleLevel(cloudNoiseSampler, xz * CloudScale.y + CloudWind.zw, lod).g;
	float n = b * 0.7 + d * 0.3;
	float from = 1 - CloudShape.x;
	return smoothstep(from, from + CloudShape.y, n);
}

// The light the clouds leave at a point: the sun's ray through it crosses the cloud layer's
// middle, a little blurred (the clouds' depth softens their shadows' edges)
float CloudShadow(float3 p)
{
	float h = CloudShape.z + CloudShape.w * 0.35;
	float t = max(h - p.y, 0) / max(CloudSun.y, 0.2);
	return 1 - CloudSunColour.w * CloudCover(p.xz + CloudSun.xz * t, 1.5);
}

// How much of the sky a point sees (0-1). Ground: the map's value. Models (model true): from
// the ground's value at their foot up to all of the sky at the top of what stands there, or 8
// units above the ground. Outside the map (beyond the camera's surroundings) a typical open value.
float SkyVisibility(float3 p, bool model)
{
	if (SkyOccRegion.w == 0)
		return 1;
	float2 uv = (p.xz - SkyOccRegion.xy) * SkyOccRegion.z;
	float4 s = skyOccTexture.SampleLevel(skyOccSampler, uv, 0);
	float vis = s.r;
	if (model)
		vis = lerp(s.r, 1, saturate((p.y - s.g) / max(s.b - s.g, 8)));
	float2 e = abs(uv - 0.5) * 2;
	return lerp(vis, 0.9, saturate((max(e.x, e.y) - 0.8) / 0.2));
}

// The sky's light on a surface facing n, of mean brightness 1: the colour above, at the
// horizon below that
float3 SkyAmbient(float3 n)
{
	float3 c = lerp(CloudSkyHorizon.rgb, CloudSkyUp.rgb, saturate(n.y * 0.5 + 0.5));
	return c / max(CloudSkyHorizon.w, 0.05);
}
)---";

// The sky light's map from the terrain's heights and the casters seen from above
static const char skyOccShaderCode[] = R"---(
Texture2D<float> heightTexture; // the terrain's vertex heights
Texture2D<float> skyDepth;      // the static shadow casters from straight above
SamplerState heightSampler;
SamplerState depthSampler;

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

// the ground, or the top of a caster on it
float TopHeight(float2 xz)
{
	float h = GroundHeight(xz);
	float2 duv = (xz - SkyOccRegion.xy) * SkyOccRegion.z;
	duv.y = 1 - duv.y;
	if (all(duv >= 0) && all(duv <= 1)) {
		float d = skyDepth.SampleLevel(depthSampler, duv, 0);
		if (d < 1)
			h = max(h, SkyOccTerrain.w - d * SkyOccLight.w);
	}
	return h;
}

// Per texel: the horizon in 16 directions out to ~46 units (steps growing by half each time),
// the sky above it cosine-weighted for a level surface: cos^2 of the horizon's elevation
float4 PS(VS_OUTPUT input) : SV_Target
{
	float2 uv = input.Pos.xy / 512; // ShadowEnvironment::SKY_OCC_SIZE
	float2 xz = SkyOccRegion.xy + uv / SkyOccRegion.z;
	float ground = GroundHeight(xz);
	float top = TopHeight(xz);
	float h0 = ground + 0.4;
	float vis = 0;
	[loop] for (int k = 0; k < 16; k++) {
		float a = (k + 0.5) * 0.39269908;
		float2 dir = float2(cos(a), sin(a));
		float horizon = 0;
		float r = 0.7;
		[loop] for (int s = 0; s < 11; s++) {
			horizon = max(horizon, (TopHeight(xz + dir * r) - h0) / r);
			r *= 1.5;
		}
		vis += 1 / (1 + horizon * horizon);
	}
	return float4(vis / 16, ground, top, 1);
}
)---";

namespace {
// tiling gradient noise of period p (lattice cells) at x, y in cells
float Hash(int x, int y, int p, int seed)
{
	x = ((x % p) + p) % p;
	y = ((y % p) + p) % p;
	uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + (uint32_t)seed * 2246822519u;
	h = (h ^ (h >> 13)) * 1274126177u;
	h ^= h >> 16;
	return (h & 0xFFFFFF) / (float)0xFFFFFF;
}

float Gradient(float x, float y, int p, int seed)
{
	const int x0 = (int)std::floor(x), y0 = (int)std::floor(y);
	const float fx = x - x0, fy = y - y0;
	auto dotGrid = [&](int ix, int iy) {
		const float a = Hash(ix, iy, p, seed) * 6.2831853f;
		return std::cos(a) * (x - ix) + std::sin(a) * (y - iy);
	};
	auto fade = [](float t) { return t * t * t * (t * (t * 6 - 15) + 10); };
	const float u = fade(fx), v = fade(fy);
	const float a = dotGrid(x0, y0) + (dotGrid(x0 + 1, y0) - dotGrid(x0, y0)) * u;
	const float b = dotGrid(x0, y0 + 1) + (dotGrid(x0 + 1, y0 + 1) - dotGrid(x0, y0 + 1)) * u;
	return a + (b - a) * v; // about -0.7..0.7
}

float Fbm(float u, float v, int basePeriod, int octaves, int seed)
{
	float sum = 0, amp = 0.5f, norm = 0;
	int p = basePeriod;
	for (int o = 0; o < octaves; o++, p *= 2, amp *= 0.5f) {
		sum += amp * Gradient(u * p, v * p, p, seed + o);
		norm += amp;
	}
	return std::clamp(0.5f + sum / norm * 0.9f, 0.0f, 1.0f);
}

// 1 - the distance to the nearest of one random point per cell (tiling), 0-1
float Cells(float u, float v, int p, int seed)
{
	const float x = u * p, y = v * p;
	const int cx = (int)std::floor(x), cy = (int)std::floor(y);
	float best = 2.0f;
	for (int j = -1; j <= 1; j++)
		for (int i = -1; i <= 1; i++) {
			const float px = cx + i + Hash(cx + i, cy + j, p, seed), py = cy + j + Hash(cx + i, cy + j, p, seed + 17);
			best = std::min(best, (px - x) * (px - x) + (py - y) * (py - y));
		}
	return std::clamp(1.0f - std::sqrt(best), 0.0f, 1.0f);
}
}

void CreateCloudResources(DiligentGfx* gfx)
{
	auto& env = gfx->shadows;
	IRenderDevice* device = gfx->device;
	constexpr int N = 256;
	std::vector<uint8_t> pixels((size_t)N * N * 4);
	for (int y = 0; y < N; y++)
		for (int x = 0; x < N; x++) {
			const float u = (x + 0.5f) / N, v = (y + 0.5f) / N;
			// the base: billows of a few cells with the cells' rims eating into them
			const float base = Fbm(u, v, 3, 5, 11) * 0.75f + Cells(u, v, 6, 5) * 0.25f;
			const float detail = Fbm(u, v, 8, 4, 41);
			const float cells = Cells(u, v, 8, 77) * 0.65f + Fbm(u, v, 16, 3, 91) * 0.35f;
			uint8_t* p = &pixels[((size_t)y * N + x) * 4];
			p[0] = (uint8_t)std::lround(std::clamp(base, 0.0f, 1.0f) * 255);
			p[1] = (uint8_t)std::lround(detail * 255);
			p[2] = (uint8_t)std::lround(cells * 255);
			p[3] = 255;
		}
	TextureDesc desc;
	desc.Name = "Cloud noise";
	desc.Type = RESOURCE_DIM_TEX_2D;
	desc.Width = desc.Height = N;
	desc.MipLevels = 0;
	desc.Format = TEX_FORMAT_RGBA8_UNORM;
	desc.BindFlags = BIND_SHADER_RESOURCE | BIND_RENDER_TARGET;
	desc.MiscFlags = MISC_TEXTURE_FLAG_GENERATE_MIPS;
	device->CreateTexture(desc, nullptr, &env.cloudNoise);
	if (!env.cloudNoise)
		ferr("Diligent: could not create the cloud noise.");
	TextureSubResData sub{ pixels.data(), (Uint64)N * 4 };
	gfx->ctx->UpdateTexture(env.cloudNoise, 0, 0, Box{ 0, (Uint32)N, 0, (Uint32)N }, sub, RESOURCE_STATE_TRANSITION_MODE_TRANSITION, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	gfx->ctx->GenerateMips(env.cloudNoise->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));

	BufferDesc cb;
	cb.Name = "Clouds";
	cb.Size = 160;
	cb.BindFlags = BIND_UNIFORM_BUFFER;
	cb.Usage = USAGE_DEFAULT; // written with UpdateBuffer, kept between frames (the sky draws first)
	const std::vector<float> zeros(cb.Size / 4, 0.0f); // off until the scene renderer writes it
	BufferData cbData{ zeros.data(), cb.Size };
	device->CreateBuffer(cb, &cbData, &env.cloudBuffer);
	if (!env.cloudBuffer)
		ferr("Diligent: could not create the cloud buffer.");

	// the sky light's map, and the casters from above
	TextureDesc occ;
	occ.Name = "Sky light occlusion";
	occ.Type = RESOURCE_DIM_TEX_2D;
	occ.Width = occ.Height = DiligentGfx::ShadowEnvironment::SKY_OCC_SIZE;
	occ.Format = TEX_FORMAT_RGBA16_FLOAT;
	occ.BindFlags = BIND_SHADER_RESOURCE | BIND_RENDER_TARGET;
	device->CreateTexture(occ, nullptr, &env.skyOcc);
	TextureDesc dd;
	dd.Name = "Sky light casters from above";
	dd.Type = RESOURCE_DIM_TEX_2D;
	dd.Width = dd.Height = DiligentGfx::ShadowEnvironment::SKY_OCC_SIZE * 2;
	dd.Format = TEX_FORMAT_D32_FLOAT;
	dd.BindFlags = BIND_SHADER_RESOURCE | BIND_DEPTH_STENCIL;
	device->CreateTexture(dd, nullptr, &env.skyDepth);
	if (!env.skyOcc || !env.skyDepth)
		ferr("Diligent: could not create the sky light's maps.");
	const float white[4] = { 1, 1, 1, 1 };
	ITextureView* rtv = env.skyOcc->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
	gfx->ctx->SetRenderTargets(1, &rtv, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	gfx->ctx->ClearRenderTarget(rtv, white, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

	const PipelineResourceDesc resources[] = {
		{ SHADER_TYPE_PIXEL, "CloudBuffer", 1, SHADER_RESOURCE_TYPE_CONSTANT_BUFFER, SHADER_RESOURCE_VARIABLE_TYPE_STATIC },
		{ SHADER_TYPE_PIXEL, "skyDepth", 1, SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_STATIC },
		{ SHADER_TYPE_PIXEL, "heightTexture", 1, SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
	};
	SamplerDesc linear;
	linear.AddressU = linear.AddressV = linear.AddressW = TEXTURE_ADDRESS_CLAMP;
	SamplerDesc point = linear;
	point.MinFilter = point.MagFilter = point.MipFilter = FILTER_TYPE_POINT;
	const ImmutableSamplerDesc samplers[] = { { SHADER_TYPE_PIXEL, "heightSampler", linear }, { SHADER_TYPE_PIXEL, "depthSampler", point },
		{ SHADER_TYPE_PIXEL, "cloudNoiseSampler", linear }, { SHADER_TYPE_PIXEL, "skyOccSampler", linear } };
	PipelineResourceSignatureDesc sig;
	sig.Name = "Sky light occlusion";
	sig.Resources = resources;
	sig.NumResources = (Uint32)std::size(resources);
	sig.ImmutableSamplers = samplers;
	sig.NumImmutableSamplers = (Uint32)std::size(samplers);
	device->CreatePipelineResourceSignature(sig, &env.skyOccSignature);
	if (!env.skyOccSignature)
		ferr("Diligent: could not create the sky light occlusion signature.");
	env.skyOccSignature->GetStaticVariableByName(SHADER_TYPE_PIXEL, "CloudBuffer")->Set(env.cloudBuffer);
	env.skyOccSignature->GetStaticVariableByName(SHADER_TYPE_PIXEL, "skyDepth")->Set(env.skyDepth->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
	const std::string source = std::string(g_cloudShaderCode) + skyOccShaderCode;
	auto shader = [&](SHADER_TYPE type, const char* entry) {
		ShaderCreateInfo sci;
		sci.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
		sci.Source = source.c_str();
		sci.SourceLength = source.size();
		sci.EntryPoint = entry;
		sci.Desc.ShaderType = type;
		sci.Desc.Name = "Sky light occlusion";
		RefCntAutoPtr<IShader> s;
		device->CreateShader(sci, &s);
		if (!s)
			ferr("Diligent: failed to compile the sky light occlusion shader %s", entry);
		return s;
	};
	RefCntAutoPtr<IShader> vs = shader(SHADER_TYPE_VERTEX, "VS"), ps = shader(SHADER_TYPE_PIXEL, "PS");
	GraphicsPipelineStateCreateInfo ci;
	ci.PSODesc.Name = "Sky light occlusion";
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
	IPipelineResourceSignature* sigs[] = { env.skyOccSignature };
	ci.ppResourceSignatures = sigs;
	ci.ResourceSignaturesCount = 1;
	device->CreateGraphicsPipelineState(ci, &env.skyOccPipeline);
	if (!env.skyOccPipeline)
		ferr("Diligent: could not create the sky light occlusion pipeline.");
}

void UpdateCloudBuffer(DiligentGfx* gfx, const Camera& camera, const Vector3& sunDirection, uint32_t sunColour)
{
	auto& env = gfx->shadows;
	if (!env.cloudBuffer)
		return;
	const RenderSettings& rs = g_renderSettings;
	const float time = env.time;
	const Vector3 sun = sunDirection.normal();
	env.sunDirection = sun;
	// wind: the base layer drifts at 6 units a second, the detail a little faster and across
	constexpr float BASE_SCALE = 1.0f / 1500.0f, DETAIL_SCALE = 1.0f / 420.0f;
	auto wrap = [](float v) { return v - std::floor(v); };
	const float r = ((sunColour >> 16) & 255) / 255.0f, g = ((sunColour >> 8) & 255) / 255.0f, b = (sunColour & 255) / 255.0f;
	env.clouds = rs.dynamicClouds;
	env.cloudShadowsDrawn = true;
	// the sky light's map: around the point the camera looks at, snapped to its texels
	const float size = 2.0f * DiligentGfx::ShadowEnvironment::SKY_OCC_RADIUS;
	const float texel = size / DiligentGfx::ShadowEnvironment::SKY_OCC_SIZE;
	const Vector3 target = camera.position + camera.direction.normal() * (DiligentGfx::ShadowEnvironment::SKY_OCC_RADIUS * 0.7f);
	env.skyOccOrigin[0] = std::round((target.x - size * 0.5f) / texel) * texel;
	env.skyOccOrigin[1] = std::round((target.z - size * 0.5f) / texel) * texel;
	const bool skyOn = rs.skyGI && env.skyOcc && env.terrainHeights;
	const float* up = env.skyLightValid ? env.skyLight[0] : nullptr;
	const float* horizon = env.skyLightValid ? env.skyLight[1] : nullptr;
	const float grey[3] = { 0.62f, 0.68f, 0.75f }, greyH[3] = { 0.7f, 0.74f, 0.78f };
	if (!up) { up = grey; horizon = greyH; }
	const float horizonLum = 0.299f * (up[0] + horizon[0]) * 0.5f + 0.587f * (up[1] + horizon[1]) * 0.5f + 0.114f * (up[2] + horizon[2]) * 0.5f;
	const float data[40] = {
		wrap(time * 6.0f * BASE_SCALE), wrap(time * 2.0f * BASE_SCALE), wrap(time * 7.5f * DETAIL_SCALE), wrap(-time * 1.2f * DETAIL_SCALE),
		std::clamp(rs.cloudCover, 0.0f, 1.0f), 0.22f, 230.0f, 140.0f,
		sun.x, sun.y, sun.z, rs.cloudShadows ? (float)rs.dynamicClouds : 0.0f,
		r, g, b, 0.62f,
		BASE_SCALE, DETAIL_SCALE, rs.lightShaftStrength, time,
		up[0], up[1], up[2], 1.0f,
		horizon[0], horizon[1], horizon[2], horizonLum,
		env.skyOccOrigin[0], env.skyOccOrigin[1], 1.0f / size, skyOn ? 1.0f : 0.0f,
		0.4f, 0.5f, 0.3f, DiligentGfx::ShadowEnvironment::SKY_OCC_RANGE,
		(float)env.terrainGrid[0], (float)env.terrainGrid[1], (float)env.terrainGrid[2], DiligentGfx::ShadowEnvironment::SKY_OCC_TOP,
	};
	gfx->ctx->UpdateBuffer(env.cloudBuffer, 0, sizeof(data), data, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	env.skyOccOn = skyOn;
}
