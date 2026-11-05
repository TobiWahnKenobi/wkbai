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
#include <functional>
#include <thread>
#include <cstdio>
#include <SDL3/SDL_timer.h>

using namespace Diligent;

const char* const g_cloudShaderCode = R"---(
cbuffer CloudBuffer
{
	float4 CloudWind;       // the base layer's UV offset (xy), the detail layer's (zw)
	float4 CloudShape;      // cover 0-1, edge softness, the layer's base altitude, its thickness
	float4 CloudSun;        // the direction to the sun, w: mode (0 off, 1 shadows, 2 light shafts, 3 the clouds in the sky)
	float4 CloudSunColour;  // the sun's colour, w: the share of the sunlight a full cloud takes
	float4 CloudScale;      // UV per world unit of the base and the detail layer, light shaft strength, a clock (s)
	float4 CloudSkyUp;      // the sky's colour above (the clouds' ambient light and the sky light); w: the ground's Fresnel
	float4 CloudSkyHorizon; // and at the horizon; w: their mean brightness
	float4 SkyOccRegion;    // the sky light's map: its corner x, z, 1 / its size, w: on
	float4 SkyOccLight;     // the sky light's share of the light, the bounce's strength, the sun's light left in a static shadow, the top-down depth's range
	float4 SkyOccTerrain;   // the height grid's vertices across and down, the map edge in tiles, the top-down view's height
	// the weather's cloud preset (UpdateCloudBuffer): x stratus (flat sheets), y storm (towers,
	// anvils, dark bases), z density, w the light on the clouds
	float4 CloudWeather;
	float4 CloudFlash;      // lightning inside the clouds: its position, w its strength now
	// the high clouds (the sky's pass, HighClouds): x cirrus, y altocumulus, z the altocumulus'
	// form (0 floccus: scattered tufts, 1 stratiformis: a sheet of cells)
	float4 CloudHigh;
};
SamplerState cloudNoiseSampler; // wrapping, linear
SamplerState skyOccSampler;     // clamped, linear
Texture2D cloudNoise;   // r: the base layer, g: the detail, b: cells for the clouds' edges (tiling)
Texture2D skyOccTexture; // r: how much of the sky the ground sees, g: the ground's height, b: the casters' top

// How cloudy the sky is above a point of the map (0-1): two noise layers drifting with the wind
// (the broad one mostly), ramped softly around the cover. The volumetric clouds take it as their
// coverage, the 3D shape giving the detail (a sharp, detailed cover made thin columns as tall as
// the layer); the ground shadows and the light shafts take the same, so they match the clouds.
float CloudCover(float2 xz, float lod)
{
	float b = cloudNoise.SampleLevel(cloudNoiseSampler, xz * CloudScale.x + CloudWind.xy, lod).r;
	float d = cloudNoise.SampleLevel(cloudNoiseSampler, xz * CloudScale.y + CloudWind.zw, lod).g;
	float n = b * 0.85 + d * 0.15;
	// no cover: none at all (the soft ramp would leave the noise's highest peaks)
	return saturate((n - (1 - CloudShape.x) + CloudShape.y) / (2.5 * CloudShape.y)) * saturate(CloudShape.x * 12);
}

// A storm's cumulonimbus (CloudWeather.y): a few huge cells of a much broader noise of their
// own, about one per 3 km square, each about a kilometre across; the noise's value there, 0.65
// and over a tower's core. lod: the cumulus cover's mip level (this noise is coarser).
float StormCell(float2 xz, float lod)
{
	return cloudNoise.SampleLevel(cloudNoiseSampler, xz * (CloudScale.x * 0.12) + CloudWind.xy + float2(0.53, 0.19), max(lod - 2, 0)).r;
}

// x: the towers (h: the height in the layer, 0-1; a little wider at the middle, narrower up,
// their sides bulging by a noise that changes with the height: billows, not a cylinder), y: the
// anvils' reach (wider than the towers, carried downwind with height)
float2 StormCells(float2 xz, float h, float lod)
{
	float n = StormCell(xz, lod);
	float bulge = cloudNoise.SampleLevel(cloudNoiseSampler, xz / 3000 + float2(h * 0.09, h * 0.05), max(lod - 1, 0)).g - 0.5;
	float tower = saturate((n - (0.65 + 0.015 * h - 0.02 * sin(saturate(h) * 3.14159) + bulge * 0.06)) / 0.12);
	float a = StormCell(xz - float2(0.948683, 0.316228) * (saturate(h - 0.7) * 2500), lod);
	return float2(tower, saturate((a - 0.625) / 0.05)); // only the strong cells' anvils
}

// The light the clouds leave at a point: the sun's ray through it crosses the cloud layer's
// middle, a little blurred (the clouds' depth softens their shadows' edges)
float CloudShadow(float3 p)
{
	float h = CloudShape.z + CloudShape.w * 0.35;
	float t = max(h - p.y, 0) / max(CloudSun.y, 0.2);
	float2 xz = p.xz + CloudSun.xz * t;
	float c = CloudCover(xz, 1.5);
	if (CloudWeather.y > 0) { // the storm's towers and anvils over its low deck
		float2 cb = StormCells(xz, 0.9, 1.5);
		c = lerp(c, max(max(cb.x, cb.y * 0.75), saturate(c * 1.4)), CloudWeather.y); // the deck's thin parts shade too
	}
	return 1 - CloudSunColour.w * c;
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

// 3D tiling noise for the volumetric clouds' shape (Schneider, "The Real-time Volumetric
// Cloudscapes of Horizon Zero Dawn", SIGGRAPH 2015): gradient noise and Worley cells of
// period p lattice cells in each axis
uint32_t Hash3(int x, int y, int z, int p, uint32_t seed)
{
	x = ((x % p) + p) % p;
	y = ((y % p) + p) % p;
	z = ((z % p) + p) % p;
	uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + (uint32_t)z * 3266489917u + seed * 2246822519u;
	h = (h ^ (h >> 13)) * 1274126177u;
	return h ^ (h >> 16);
}

float Gradient3(float x, float y, float z, int p, uint32_t seed)
{
	static const float g[12][3] = { {1,1,0},{-1,1,0},{1,-1,0},{-1,-1,0},{1,0,1},{-1,0,1},{1,0,-1},{-1,0,-1},{0,1,1},{0,-1,1},{0,1,-1},{0,-1,-1} };
	const int x0 = (int)std::floor(x), y0 = (int)std::floor(y), z0 = (int)std::floor(z);
	const float fx = x - x0, fy = y - y0, fz = z - z0;
	auto fade = [](float t) { return t * t * t * (t * (t * 6 - 15) + 10); };
	const float u = fade(fx), v = fade(fy), w = fade(fz);
	float c[2][2][2];
	for (int k = 0; k < 2; k++)
		for (int j = 0; j < 2; j++)
			for (int i = 0; i < 2; i++) {
				const float* gr = g[Hash3(x0 + i, y0 + j, z0 + k, p, seed) % 12];
				c[k][j][i] = gr[0] * (fx - i) + gr[1] * (fy - j) + gr[2] * (fz - k);
			}
	auto lerp = [](float a, float b, float t) { return a + (b - a) * t; };
	const float a = lerp(lerp(c[0][0][0], c[0][0][1], u), lerp(c[0][1][0], c[0][1][1], u), v);
	const float b = lerp(lerp(c[1][0][0], c[1][0][1], u), lerp(c[1][1][0], c[1][1][1], u), v);
	return lerp(a, b, w); // about -1..1
}

// 1 - the distance to the nearest of one random point per cell, 0-1 (billows: high in a cell's middle)
float Worley3(float x, float y, float z, int p, uint32_t seed)
{
	const int cx = (int)std::floor(x), cy = (int)std::floor(y), cz = (int)std::floor(z);
	float best = 3.0f;
	for (int k = -1; k <= 1; k++)
		for (int j = -1; j <= 1; j++)
			for (int i = -1; i <= 1; i++) {
				const uint32_t h = Hash3(cx + i, cy + j, cz + k, p, seed);
				const float px = cx + i + (h & 1023) / 1023.0f, py = cy + j + ((h >> 10) & 1023) / 1023.0f, pz = cz + k + ((h >> 20) & 1023) / 1023.0f;
				best = std::min(best, (px - x) * (px - x) + (py - y) * (py - y) + (pz - z) * (pz - z));
			}
	return std::clamp(1.0f - std::sqrt(best), 0.0f, 1.0f);
}

// three octaves of Worley cells from f cells across, 0.625 / 0.25 / 0.125
float WorleyFbm(float u, float v, float w, int f, uint32_t seed)
{
	return Worley3(u * f, v * f, w * f, f, seed) * 0.625f + Worley3(u * f * 2, v * f * 2, w * f * 2, f * 2, seed + 1) * 0.25f
		+ Worley3(u * f * 4, v * f * 4, w * f * 4, f * 4, seed + 2) * 0.125f;
}

// a 3D RGBA8 texture of n^3 texels with its mip chain (box filtered), from texel(u, v, w, out[4]),
// the slices made on all cores
RefCntAutoPtr<ITexture> Make3D(DiligentGfx* gfx, const char* name, int n, const std::function<void(float, float, float, float*)>& texel)
{
	std::vector<uint8_t> level((size_t)n * n * n * 4);
	const unsigned threads = std::max(1u, std::min(16u, std::thread::hardware_concurrency()));
	std::vector<std::thread> pool;
	for (unsigned t = 0; t < threads; t++)
		pool.emplace_back([&, t]() {
			for (int z = (int)t; z < n; z += (int)threads)
				for (int y = 0; y < n; y++)
					for (int x = 0; x < n; x++) {
						float out[4];
						texel((x + 0.5f) / n, (y + 0.5f) / n, (z + 0.5f) / n, out);
						uint8_t* p = &level[(((size_t)z * n + y) * n + x) * 4];
						for (int c = 0; c < 4; c++)
							p[c] = (uint8_t)std::lround(std::clamp(out[c], 0.0f, 1.0f) * 255);
					}
		});
	for (auto& th : pool)
		th.join();
	int mips = 1;
	while ((n >> mips) >= 1)
		mips++;
	TextureDesc desc;
	desc.Name = name;
	desc.Type = RESOURCE_DIM_TEX_3D;
	desc.Width = desc.Height = desc.Depth = n;
	desc.MipLevels = mips;
	desc.Format = TEX_FORMAT_RGBA8_UNORM;
	desc.BindFlags = BIND_SHADER_RESOURCE;
	desc.Usage = USAGE_IMMUTABLE;
	std::vector<std::vector<uint8_t>> levels;
	levels.push_back(std::move(level));
	for (int m = 1, s = n / 2; m < mips; m++, s /= 2) {
		const std::vector<uint8_t>& src = levels.back();
		const int ss = s * 2;
		std::vector<uint8_t> dst((size_t)s * s * s * 4);
		for (int z = 0; z < s; z++)
			for (int y = 0; y < s; y++)
				for (int x = 0; x < s; x++)
					for (int c = 0; c < 4; c++) {
						int sum = 0;
						for (int k = 0; k < 8; k++)
							sum += src[((((size_t)(2 * z + (k >> 2))) * ss + (2 * y + ((k >> 1) & 1))) * ss + (2 * x + (k & 1))) * 4 + c];
						dst[(((size_t)z * s + y) * s + x) * 4 + c] = (uint8_t)((sum + 4) / 8);
					}
		levels.push_back(std::move(dst));
	}
	std::vector<TextureSubResData> subs(mips);
	for (int m = 0; m < mips; m++) {
		const Uint64 s = std::max(1, n >> m);
		subs[m] = TextureSubResData{ levels[m].data(), s * 4, s * s * 4 };
	}
	TextureData init{ subs.data(), (Uint32)mips };
	RefCntAutoPtr<ITexture> tex;
	gfx->device->CreateTexture(desc, &init, &tex);
	if (!tex)
		ferr("Diligent: could not create the %s.", name);
	return tex;
}
}

void CreateCloudResources(DiligentGfx* gfx)
{
	auto& env = gfx->shadows;
	IRenderDevice* device = gfx->device;
	// the volumetric clouds' 3D noise: the shape (r: Perlin-Worley, g/b/a: Worley at 4, 8, 16
	// cells) and the erosion's detail (r/g/b: Worley at 4, 8, 16 cells)
	{
		const uint64_t start = SDL_GetTicksNS();
		env.cloudShape = Make3D(gfx, "Cloud shape noise", 64, [](float u, float v, float w, float* out) {
			float perlin = 0, amp = 0.5f, norm = 0;
			for (int o = 0, p = 4; o < 4; o++, p *= 2, amp *= 0.5f) {
				perlin += amp * Gradient3(u * p, v * p, w * p, p, 101 + o);
				norm += amp;
			}
			perlin = std::clamp(0.5f + perlin / norm * 0.9f, 0.0f, 1.0f);
			const float cells = WorleyFbm(u, v, w, 4, 7);
			// Perlin-Worley: the Perlin noise remapped from (cells - 1, 1): billowy, connected
			out[0] = std::clamp((perlin - (cells - 1)) / (2 - cells), 0.0f, 1.0f);
			out[1] = cells;
			out[2] = WorleyFbm(u, v, w, 8, 17);
			out[3] = WorleyFbm(u, v, w, 16, 27);
		});
		env.cloudDetail = Make3D(gfx, "Cloud detail noise", 32, [](float u, float v, float w, float* out) {
			out[0] = WorleyFbm(u, v, w, 2, 37);
			out[1] = WorleyFbm(u, v, w, 4, 47);
			out[2] = WorleyFbm(u, v, w, 8, 57);
			out[3] = 1;
		});
		printf("Cloud 3D noise made in %.0f ms\n", (SDL_GetTicksNS() - start) / 1e6);
	}
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
	cb.Size = 208;
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

// The cloud presets per weather (in the order of the seasons' weather, EnvWeather: clear,
// cloudy, rain, heavy rain, thunderstorm, fog, snow, blizzard, heatwave, drought)
const CloudPreset g_cloudPresets[CLOUD_WEATHER_COUNT] = {
	//  cover  base  thick  stratus storm density shadow light  cirrus alto  alto's sheet
	{ 0.00f, 245, 120, 0.0f, 0.0f, 1.0f, 0.55f, 1.00f, 0.0f, 0.0f, 0.0f }, // clear: none at all
	{ 0.55f, 230, 150, 0.1f, 0.0f, 1.0f, 0.62f, 1.00f, 0.0f, 0.0f, 0.0f }, // cloudy: broken cumulus
	{ 0.80f, 205, 200, 0.45f, 0.15f, 1.3f, 0.78f, 0.88f, 0.0f, 0.0f, 0.0f }, // rain: low, grey, flatter
	{ 0.90f, 195, 260, 0.55f, 0.35f, 1.5f, 0.85f, 0.76f, 0.0f, 0.0f, 0.0f }, // heavy rain: nimbostratus
	// thunderstorm: cumulonimbus (StormCells: towers to the top, flat anvils spreading under it,
	// dark bases) over ragged low clouds
	{ 0.60f, 170, 1300, 0.05f, 1.0f, 1.8f, 0.88f, 0.72f, 0.0f, 0.0f, 0.0f },
	{ 0.90f, 200, 70, 0.9f, 0.0f, 0.8f, 0.70f, 1.18f, 0.0f, 0.0f, 0.0f }, // fog: a low flat sheet
	{ 0.85f, 210, 180, 0.60f, 0.10f, 1.1f, 0.72f, 1.22f, 0.0f, 0.0f, 0.0f }, // snow: pale overcast
	{ 0.93f, 190, 240, 0.70f, 0.30f, 1.4f, 0.82f, 0.82f, 0.0f, 0.0f, 0.0f }, // blizzard
	{ 0.00f, 265, 100, 0.0f, 0.0f, 0.8f, 0.50f, 1.05f, 0.40f, 0.35f, 0.0f }, // heatwave: cirrus, altocumulus floccus
	{ 0.00f, 260, 110, 0.0f, 0.0f, 0.8f, 0.50f, 1.05f, 0.30f, 0.60f, 1.0f }, // drought: altocumulus stratiformis, some cirrus
};

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
	// the weather's preset (RenderSettings::cloudWeather: 0 the cover slider's own cumulus, else
	// a weather; with the seasons' weather, its current kind), approached over about 15 seconds
	// of real time (at once on the first frame)
	{
		CloudPreset target = g_cloudPresets[1];
		target.cover = rs.cloudCover;
		const int kind = env.cloudWeatherKind >= 0 ? env.cloudWeatherKind : rs.cloudWeather - 1;
		if (kind >= 0 && kind < CLOUD_WEATHER_COUNT)
			target = g_cloudPresets[kind];
		const uint64_t now = SDL_GetTicksNS();
		float k = 1.0f;
		if (env.cloudPresetTicks != 0)
			k = 1.0f - std::exp(-std::min((now - env.cloudPresetTicks) / 1e9f, 1.0f) / 15.0f);
		env.cloudPresetTicks = now;
		float* cur = &env.cloudPreset.cover;
		const float* to = &target.cover;
		static_assert(sizeof(CloudPreset) == 11 * sizeof(float) && sizeof(env.cloudPreset) == sizeof(CloudPreset), "the presets' fields");
		for (int i = 0; i < 11; i++)
			cur[i] += (to[i] - cur[i]) * k;
		// lightning inside the storm's clouds (with the seasons' weather, its strikes): flashes
		// every 4 to 14 seconds, flickering out over a quarter of a second
		const float stormNow = env.cloudPreset.storm;
		if (env.cloudFlashOverride >= 0)
			env.cloudFlash[3] = env.cloudFlashOverride;
		else if (stormNow > 0.5f) {
			const float t = now / 1e9f;
			if (t >= env.cloudFlashNext) {
				const uint32_t r = (uint32_t)(now >> 10) * 2654435761u;
				const float a = (r & 1023) / 1023.0f * 6.2831853f, d = 150.0f + ((r >> 10) & 1023) / 1023.0f * 650.0f;
				const Vector3 at = camera.position + camera.direction.normal() * 400.0f;
				env.cloudFlash[0] = at.x + std::cos(a) * d;
				env.cloudFlash[1] = env.cloudPreset.base + std::min(env.cloudPreset.thickness * 0.35f, 150.0f); // in the low deck
				env.cloudFlash[2] = at.z + std::sin(a) * d;
				env.cloudFlashStart = t;
				env.cloudFlashNext = t + 4.0f + ((r >> 20) & 1023) / 1023.0f * 10.0f;
			}
			const float age = t - env.cloudFlashStart;
			env.cloudFlash[3] = age < 0.25f ? (1 - age / 0.25f) * (0.6f + 0.4f * std::abs(std::sin(age * 60.0f))) * stormNow : 0.0f;
		}
		else
			env.cloudFlash[3] = 0.0f;
	}
	const DiligentGfx::ShadowEnvironment::CloudPresetState& preset = env.cloudPreset;
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
	const float data[52] = {
		wrap(time * 6.0f * BASE_SCALE), wrap(time * 2.0f * BASE_SCALE), wrap(time * 7.5f * DETAIL_SCALE), wrap(-time * 1.2f * DETAIL_SCALE),
		std::clamp(preset.cover, 0.0f, 1.0f), 0.22f, preset.base, preset.thickness,
		sun.x, sun.y, sun.z, rs.cloudShadows ? (float)rs.dynamicClouds : 0.0f,
		r, g, b, preset.shadow,
		BASE_SCALE, DETAIL_SCALE, rs.lightShaftStrength, time,
		up[0], up[1], up[2], std::clamp(rs.terrainFresnel, 0.0f, 1.0f),
		horizon[0], horizon[1], horizon[2], horizonLum,
		env.skyOccOrigin[0], env.skyOccOrigin[1], 1.0f / size, skyOn ? 1.0f : 0.0f,
		0.4f, 0.5f, 0.3f, DiligentGfx::ShadowEnvironment::SKY_OCC_RANGE,
		(float)env.terrainGrid[0], (float)env.terrainGrid[1], (float)env.terrainGrid[2], DiligentGfx::ShadowEnvironment::SKY_OCC_TOP,
		preset.stratus, preset.storm, preset.density, preset.light,
		env.cloudFlash[0], env.cloudFlash[1], env.cloudFlash[2], env.cloudFlash[3],
		preset.cirrus, preset.alto, preset.altoSheet, 0.0f,
	};
	static_assert(sizeof(data) <= 208, "the cloud buffer's size (CreateCloudResources)");
	gfx->ctx->UpdateBuffer(env.cloudBuffer, 0, sizeof(data), data, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	env.skyOccOn = skyOn;
}
