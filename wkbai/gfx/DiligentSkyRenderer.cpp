// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

// The original's sky (traced, see docs/re/sky-and-fog.md): each side of the cube is drawn as a
// grid whose rows lie at constant elevation, tan(elevation) = (k/9)^2 for k = 0..8, plus a row
// at the bottom edge, with UVs that are the plain cube projection of those directions. Each row
// gets a fog factor, so the sky fades into the fog colour over the lowest ~11 degrees, and below
// the horizon it is all fog. Here the cube is looked up per pixel with the same projection, and
// the fog follows the rows' values, interpolated in between as the original's vertex fog is.

#include "DiligentSkyRenderer.h"
#include <chrono>
#include "../util/LoadingProgress.h"
#include "renderer_diligent.h"
#include "DiligentClouds.h"
#include "bitmap.h"
#include "RenderSettings.h"
#include "../terrain.h"
#include "../Camera.h"
#include "../file.h"
#include "../util/util.h"
#include "../Environment.h"
#include <algorithm>
#include <cmath>
#include <iterator>
#include <string>

using namespace Diligent;

static const char skyShaderCode[] = R"---(
Texture2D skyFront;
Texture2D skyRight;
Texture2D skyBack;
Texture2D skyLeft;
Texture2D skyTop;
Texture2D skyBottom; // only in a full six-face sky (no shipped sky has one)
Texture2D tClouds;   // the dynamic clouds (PS_Clouds): their light, the sky's transmittance
SamplerState skySampler;

cbuffer FogBuffer
{
	float4 FogColor;
	float FogStartDist;
	float FogEndDist;
	float4 FogCamera;
	float4 Grade; // exposure, brightness, contrast, saturation
	float4 EdgeFog;
	float4 LightMasks;
	float4 FogSun;        // the haze's tint (DiligentSkyRenderer): towards the true sun (unit), its weight (0 off)
	float4 FogSunColour;  // the sun's disc through the air (rgb, the sky's tone), its glow's strength
	float4 FogHorizon[32]; // the sky's colour 2 degrees up, by the bearing (atan2(z, x), 32 steps from 0)
	float4 FogSky[50];     // the live sky for reflections (SkyReflect): 16 bearings at 4, 22, 50 degrees up, the zenith (w: 1 on), the ground below
};

// The haze's colour along a direction (the distance and edge fog's, the sky's horizon band): at
// dawn and dusk the physical sky's own horizon there, warm and bright towards the low sun,
// dimmer away from it, instead of the haze's one grey all round, dimmed with the night's ambient
// (the sun set in smog, the play test of 2026-11-09); FogColor by day
float3 FogTintDir(float3 d)
{
	if (FogSun.w == 0 || dot(d, d) < 1e-10)
		return FogColor.rgb;
	d = normalize(d);
	// the sky's own horizon along the bearing (its band and the fogged land meet it without a seam)
	float b = frac(atan2(d.z, d.x) / 6.2831853 + 1) * 32;
	int i0 = (int)b % 32, i1 = (i0 + 1) % 32;
	float3 horizon = lerp(FogHorizon[i0].rgb, FogHorizon[i1].rgb, frac(b));
	float3 c = lerp(FogColor.rgb, horizon, FogSun.w);
	// the sun over the haze, cut by the true horizon: it rises out of the fogged sea and land as
	// out of the sky (the band hid it high above the horizon, the play test of 2026-11-09)
	float mu = dot(d, FogSun.xyz);
	if (mu > 0.99 && d.y > -0.004) {
		float disc = smoothstep(0.99985, 0.99996, mu);
		float glow = pow(saturate(mu), 2500) * 0.5 + pow(saturate(mu), 300) * 0.04;
		c += FogSunColour.rgb * (disc + glow * FogSunColour.w) * FogSun.w * saturate((d.y + 0.004) / 0.002);
	}
	return c;
}
float3 FogRatioDir(float3 d)
{
	return min(FogTintDir(d) / max(FogColor.rgb, 1e-3), 2.5);
}
float3 FogTint(float3 worldPos)
{
	return FogTintDir(worldPos - FogCamera.xyz);
}


float3 GradeColour(float3 c)
{
	c *= Grade.x;
	c = lerp(dot(c, float3(0.299, 0.587, 0.114)).xxx, c, Grade.w);
	return saturate((c - 0.5) * Grade.z + 0.5 + Grade.y);
}

cbuffer SkyBuffer
{
	float4 CamForward; // w: tan(fov y / 2) * aspect
	float4 CamRight;   // w: tan(fov y / 2)
	float4 CamUp;
	float4 SkyFront;   // world direction of sky_front.tga
	float4 SkyRight;   // world direction of sky_right.tga; w: the physical sky model (RenderSettings::skyModel 1)
	float4 SkyFlags;   // x: the sky has all six faces, y: the dynamic clouds' layer (tClouds), z: the frame (its jitter),
	                   // w: the clouds' quality (RenderSettings::cloudQuality, 0-3)
	// wkbai's day and night (g_environment; NightSky): the sun's own direction (below the horizon
	// at night), w: how much night (0 by day or without the clock); the moon's direction, w: how
	// much of it shows (its height, the night)
	float4 SkySun;
	float4 SkyMoon;
	// the stars' turn: the world's direction to a star's place on the sky (rows of a rotation);
	// SkyStarX.w: the moon's lit part (0 new .. 1 full: its glow, its light on the air)
	float4 SkyStarX;
	float4 SkyStarY;   // w: the clouds' detail (RenderSettings::cloudDetail, 0-2)
	float4 SkyStarZ;
	// the sky box under the weather's clouds (dynamicClouds below 3: no volumetric clouds over
	// it): the overcast's colour, w how much (0: the box as it is)
	float4 SkyOvercast;
};

// a hash of a 3D cell (0-1, three values): David Hoskins' "Hash without Sine" (hash33), under
// its MIT licence, not the GPL:
//   Copyright (c) 2014 David Hoskins.
//   Permission is hereby granted, free of charge, to any person obtaining a copy of this software
//   and associated documentation files (the "Software"), to deal in the Software without
//   restriction, including without limitation the rights to use, copy, modify, merge, publish,
//   distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the
//   Software is furnished to do so, subject to the following conditions: The above copyright
//   notice and this permission notice shall be included in all copies or substantial portions of
//   the Software. THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
//   PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
//   LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
//   OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
//   DEALINGS IN THE SOFTWARE.
float3 Hash33(float3 p)
{
	p = frac(p * float3(0.1031, 0.1030, 0.0973));
	p += dot(p, p.yxz + 33.33);
	return frac((p.xxy + p.yxx) * p.zyx);
}

// a dither of the 8-bit output (triangular, +-1 level): no bands in the sky's smooth gradients
float3 Dither(float2 pos)
{
	float3 a = Hash33(float3(pos, SkyFlags.z));
	float3 b = Hash33(float3(pos + 37.7, SkyFlags.z + 11));
	return (a + b - 1) / 255;
}

struct VS_OUTPUT
{
	float4 Pos : SV_POSITION;
	float2 Ndc : TEXCOORD0;
};

VS_OUTPUT VS(uint id : SV_VertexID)
{
	VS_OUTPUT output;
	float2 p = float2((id << 1) & 2, id & 2) * 2 - 1; // full-screen triangle
	output.Pos = float4(p, 1, 1);
	output.Ndc = p;
	return output;
}

// The original's fog per grid row: rows at tan(elevation) = 0, 1/81, 4/81, 9/81, 16/81 have
// fog alpha 0, 0, 40, 180, 255 (255 = no fog).
float SkyFog(float t)
{
	if (t <= 1.0 / 81) return 1;
	// (eased in: the haze's flat band under it and this ramp met in a kink, a faint line just over
	// the horizon, the play test of 2026-11-09; at 4/81 the slopes now nearly meet the next ramp's)
	if (t <= 4.0 / 81) { float x = (t - 1.0 / 81) / (3.0 / 81); return lerp(1, 1 - 40.0 / 255, x * x); }
	if (t <= 9.0 / 81) return lerp(1 - 40.0 / 255, 1 - 180.0 / 255, (t - 4.0 / 81) / (5.0 / 81));
	if (t <= 16.0 / 81) return lerp(1 - 180.0 / 255, 0, (t - 9.0 / 81) / (7.0 / 81));
	return 0;
}

Texture3D cloudShape;  // r: Perlin-Worley, g/b/a: Worley at 4, 8, 16 cells (tiling, 64^3)
Texture3D cloudDetail; // r/g/b: Worley at 4, 8, 16 cells (tiling, 32^3)
SamplerState cloud3DSampler;

static const float SHAPE_PERIOD = 480, DETAIL_PERIOD = 120; // world units per tile

float Remap(float x, float a, float b, float c, float d)
{
	return c + (x - a) / max(b - a, 1e-4) * (d - c);
}

// Cloud detail High (SkyStarY.w 2): the cumulonimbus as real ones are (the play test of
// 2026-11-09: cones going up into the sky): straight towers on the deck's flat base, their sides
// billowing out in lumps (a 3D noise of a few hundred units, changing up the tower), to an anvil
// that spreads out from about three quarters of the layer, downwind and wider the higher (several
// times the tower across), flat under the layer's top. x: the towers, y: the anvils.
float2 StormCellsHigh(float3 p, float h, float lod)
{
	float n = StormCell(p.xz, lod);
	float4 s = cloudShape.SampleLevel(cloud3DSampler, p / 1900 + float3(0.31, 0.17, 0.67), max(lod - 1, 0));
	float billow = s.g * 0.55 + s.b * 0.3 + s.a * 0.15 - 0.5;
	float th = 0.66 - 0.035 * smoothstep(0.55, 0.8, h) + billow * 0.09;
	float tower = saturate((n - th) / 0.05);
	float spread = smoothstep(0.72, 0.97, h);
	float a = StormCell(p.xz - float2(0.948683, 0.316228) * (spread * 3200), lod);
	float anvil = saturate((a - lerp(0.645, 0.56, spread) - billow * 0.04) / 0.05) * smoothstep(0.72, 0.8, h);
	return float2(tower, anvil);
}

// Cloud detail Medium and High (SkyStarY.w 1 and 2): how dense the layer is by region (the play
// test of 2026-11-09: one uniform deck): two octaves of the smooth noise (about 2.6 km and 900
// units across), drifting with the wind; 0.35 to 1.5 times.
float DensityRegions(float2 xz, float footprint)
{
	float2 dp = xz - float2(6, 2) * CloudScale.w;
	float r = cloudNoise.SampleLevel(cloudNoiseSampler, dp / 2600 + float2(0.43, 0.17), max(log2(footprint * 256 / 2600), 0)).a * 0.65
		+ cloudNoise.SampleLevel(cloudNoiseSampler, dp / 900 + float2(0.29, 0.61), max(log2(footprint * 256 / 900), 0)).a * 0.35;
	return lerp(0.35, 1.5, smoothstep(0.3, 0.72, r));
}

// The cloud layer's density at a point (Schneider, "The Real-time Volumetric Cloudscapes of
// Horizon Zero Dawn"): the 3D Perlin-Worley shape within a rounded height profile, cut by the
// cover over the map (the same as the ground shadows'), its edges eroded by 3D Worley detail,
// wispy towards the base and billowy towards the top. The cloud tops vary by region through
// their own noise (not the cover: tops following the cover made a cone of every peak of it), and
// the shape is 3D (a 2D shape stretched up the layer gave vertical streaks). footprint: the
// pixel's size there in world units, for the noise's mip levels; detail: with the erosion (the
// light's steps go without).
// the density's regions at the view's step (DensityRegions, Medium and High; 1 on Low): the
// march sets it once per step, the light's steps reuse it (sampling it again in each of them
// cost half a millisecond at Ultra)
static float cRegion = 1;
// and the 2D fields' offset there (CloudWarp; its light steps reuse it, the noise is broad)
static float2 cWarp = 0;

// High: the offset's direction at a point (-0.5 to 0.5; changing with the height h)
float2 CloudWarp(float2 xz, float h, float lod)
{
	return cloudNoise.SampleLevel(cloudNoiseSampler, xz / 1100 + float2(h * 0.43, h * 0.29) + float2(0.07, 0.53), max(lod - 1, 0)).rg - 0.5;
}

float CloudDensity(float3 p, float lod, float footprint, bool detail)
{
	CloudAt(p.xz);
	const int detailLevel = (int)SkyStarY.w; // (the cloudDetail texture is the erosion's)
	float h = saturate((p.y - cShape.z) / cShape.w);
	// the coverage (CloudCover, as the ground shadows')
	const float stratus = cWeather.x, storm = cWeather.y;
	// High: the map's 2D fields (the cover, the storm cells, the tops) looked up a
	// little aside, by an offset that changes with the height, more higher up: the 2D cover alone,
	// the same all up the layer, made the clouds' sides straight walls, seen from below as
	// streaks and spikes rising to the sky (the play test of 2026-11-09, most while they formed);
	// now they bulge and lean (the base keeps its place, over the ground's shadows)
	float2 xz = p.xz;
	if (detailLevel >= 2)
		xz += cWarp * (lerp(110, 260, storm) * smoothstep(0.05, 0.9, h));
	float cover = CloudCover(xz, lod + 1);
	// a sheet closes up: a stratus deck is one layer of varying thickness, not broken cloud
	cover = lerp(cover, saturate(cover * 1.3 + 0.12), stratus * smoothstep(0.5, 0.8, cShape.x));
	float stormCore = 0, deck = 0;
	if (storm > 0) {
		// cumulonimbus: towers on the storm cells' cores up to the layer's top, their anvils
		// spreading out wide under it, over a dark low deck of the ordinary cover all around
		// (the lowest sixth of the layer: a few hundred units, as a rain's)
		float2 cb = detailLevel >= 2 ? StormCellsHigh(float3(xz.x, p.y, xz.y), h, lod) : StormCells(xz, h, lod);
		float anvil = detailLevel >= 2 ? cb.y : cb.y * smoothstep(0.8, 0.9, h);
		float low = cover * (1 - smoothstep(0.1, 0.17, h));
		cover = lerp(cover, max(max(cb.x, anvil), low), storm);
		stormCore = cb.x * storm;
		deck = storm * (1 - cb.x) * (1 - smoothstep(0.1, 0.2, h));
	}
	if (cover <= 0.002)
		return 0;
	// the tops: by region; a stratus sheet's level; a storm's flat, at the layer's top (the anvils)
	float topVar = cloudNoise.SampleLevel(cloudNoiseSampler, xz * (CloudScale.x * 0.7) + float2(0.37, 0.71), lod + 1).g;
	float top = lerp(0.5, 1.0, saturate(topVar * 1.6 - 0.3));
	top = lerp(lerp(top, 0.85, stratus), 1.0, storm);
	// the base: a 10-40 unit ramp (not a share of the layer: a tall storm layer's base went soft),
	// raised and lowered a little by region (a ragged base)
	float baseH = (topVar - 0.5) * 30 / cShape.w;
	const float topStart = lerp(top * lerp(0.45, 0.7, stratus), 0.93, storm);
	float profile = saturate(Remap(h, baseH, baseH + lerp(10, 40, stratus) / cShape.w, 0, 1)) *saturate(Remap(h, topStart, top, 1, 0));
	if (profile <= 0)
		return 0;
	float3 drift = float3(6, 0, 2) * CloudScale.w; // as the cover's base layer (6 and 2 units a second)
	float3 q = p - drift - float3(0, cShape.z, 0);
	float4 s = cloudShape.SampleLevel(cloud3DSampler, q / SHAPE_PERIOD, max(log2(footprint * 64 / SHAPE_PERIOD), 0));
	if (detailLevel >= 1) {
		// Medium and High: the tops rounded, a dome over the height profile's ramp instead of its
		// straight line, and lowered a little by the 3D noise (the play test of 2026-11-09: with the
		// cover low, as while clouds form, the profile before the coverage's cut narrowed every
		// bump of the 2D cover to a point at the top: spikes going up into the sky)
		float x = saturate(Remap(h, topStart, top - 0.08 * (top - topStart) * s.a, 0, 1));
		profile = saturate(Remap(h, baseH, baseH + lerp(10, 40, stratus) / cShape.w, 0, 1)) * sqrt(saturate(1 - x * x));
		if (profile <= 0)
			return 0;
	}
	float fbm = s.g * 0.625 + s.b * 0.25 + s.a * 0.125;
	float shape = saturate(Remap(s.r, fbm - 1, 1, 0, 1));
	shape = lerp(shape, 0.45 + 0.55 * shape, max(stratus, deck * 0.35)); // sheets (and a storm's deck): the billows flattened
	if (detail && stratus > 0.05 && SkyFlags.w >= 1) { // (not on Low)
		// a sheet's underside mottled at three scales (about 180, 65 and 25 units: the lumps
		// and rolls of a real stratus deck; the billows' shape alone left it a smooth blur);
		// the view's steps only (the light's would cost three samples more each)
		float2 sp = p.xz - float2(6, 2) * CloudScale.w;
		float m = cloudNoise.SampleLevel(cloudNoiseSampler, sp / 1400 + float2(0.21, 0.47), max(log2(footprint * 256 / 1400), 0)).g * 0.5
			+ cloudNoise.SampleLevel(cloudNoiseSampler, sp / 520 + float2(0.63, 0.12), max(log2(footprint * 256 / 520), 0)).g * 0.3
			+ cloudNoise.SampleLevel(cloudNoiseSampler, sp / 200 + float2(0.11, 0.83), max(log2(footprint * 256 / 200), 0)).b * 0.2;
		shape *= lerp(1, 0.45 + 1.1 * m, stratus); // (thinner and thicker, few holes)
	}
	shape = lerp(shape, 0.3 + 0.7 * shape, stormCore * stormCore) * profile; // the towers' middles solid
	float c = saturate(Remap(shape, 1 - cover, 1, 0, 1)) * cover;
	// (Medium and High: thick and thin clouds by region; a storm's towers keep theirs)
	if (detailLevel >= 1 && c > 0)
		c *= lerp(cRegion, 1, stormCore);
	if (!detail || c <= 0)
		return c * 1.6 * cWeather.z;
	float detailLod = log2(footprint * 32 / DETAIL_PERIOD);
	float3 d = cloudDetail.SampleLevel(cloud3DSampler, (q - drift * 0.3) / DETAIL_PERIOD, max(detailLod, 0)).rgb;
	float hf = d.r * 0.625 + d.g * 0.25 + d.b * 0.125;
	float erosion = 0.4 * saturate(2 - detailLod * 0.5) * (1 - 0.6 * stratus);
	if (detailLevel >= 2 && detailLod < 3.5) {
		// High: a finer octave of billows (about 40 units) on the cumulus' tops and a storm tower's
		// sides, and the tops' cauliflower relief cut a little deeper
		float3 d2 = cloudDetail.SampleLevel(cloud3DSampler, (q + drift * 0.2) / (DETAIL_PERIOD / 3), max(detailLod + 1.6, 0)).rgb;
		float fine = d2.r * 0.5 + d2.g * 0.3 + d2.b * 0.2;
		hf = lerp(hf, fine, 0.4 * saturate(h * 2 + stormCore));
		erosion *= lerp(1, 1.3, saturate(h * 1.5) * (1 - stratus));
	}
	hf = lerp(1 - hf, hf, saturate(h * 3));
	return saturate(Remap(c, hf * erosion, 1, 0, 1)) * 1.6 * cWeather.z;
}

float HenyeyGreenstein(float mu, float g)
{
	float g2 = g * g;
	return (1 - g2) / (12.566371 * pow(1 + g2 - 2 * g * mu, 1.5));
}

// The view ray through the cloud layer, 48 to 128 steps by the quality (jittered per pixel; at most
// 1800 units of it), each lit by the sun through 3 to 6 steps of cloud towards it over 108 units
// (Beer's law with the powder term away from the sun) and
// a strong forward scattering lobe (the silver linings), plus the sky's light from above.
// Returns the light scattered towards the camera and the sky's transmittance.
// Dawn and dusk: the colour the sunlight has on the clouds after its long way through the air
// at the sun's height (its air mass, Rayleigh and Mie as PhysicalSky's), 1 by day: golden, then
// orange and red; against the sky's blue light in the shade, pink and violet. lift: how far
// below the horizon the sun still lights them (the higher clouds longer).
float3 DuskTint(float3 sun, float lift)
{
	float air = min(1 / max(sun.y + lift, 0.025), 40);
	float3 tr = exp(-(float3(5.8e-3, 13.5e-3, 33.1e-3) * 8 + 21e-3 * 1.2 * 1.1) * air);
	float3 tint = tr / max(max(tr.r, tr.g), max(tr.b, 1e-4));
	float dusk = smoothstep(0.55, 0.05, sun.y);
	// the Physical sky model (SkyRight.w): orange, gold and pink rather than a deep red (the
	// clouds' many scatterings and the sky's light soften it; under the day's grade at sunset the
	// pure air-mass colour read as red paint, the play test of 2026-11-09)
	if (SkyRight.w != 0)
		return lerp(float3(1, 1, 1), pow(tint, 0.45) * 1.15, dusk);
	return lerp(float3(1, 1, 1), tint * 1.5, dusk);
}

// skyThere: the physical sky's colour along the view (PS_Clouds; 0 without it)
float4 MarchClouds(float3 ro, float3 rd, float2 pixel, float3 skyThere)
{
	// (during a front the layer between both weathers' lowest base and highest top)
	float base = CloudShape.z, top = CloudShape.z + CloudShape.w;
	if (CloudFront.w > 0) {
		base = min(base, CloudFromShape.y);
		top = max(top, CloudFromShape.y + CloudFromShape.z);
	}
	CloudAt(ro.xz);
	if (ro.y > base)
		return float4(0, 0, 0, 1); // above the base (zoomed out): the shadows only
	// beyond the march (at the horizon): the haze the far clouds fade into, as much as the sky is
	// cloudy (a storm as an overcast), so the clouds blend into the horizon without a line
	const float hazeCover = saturate(max(cShape.x, cWeather.y * 0.85) * 1.15);
	const float4 beyond = float4(FogTintDir(rd) * hazeCover, 1 - hazeCover);
	if (rd.y <= 0.002)
		return beyond;
	// at most 1800 units of the ray in the layer; a storm's 6000, so that the cumulonimbus'
	// towers show to their anvils towards the horizon
	float t0 = (base - ro.y) / rd.y, t1 = min((top - ro.y) / rd.y, t0 + lerp(1800, 6000, cWeather.y));
	if (t1 <= t0 || t0 > 9000)
		return beyond;
	// Low: a quarter of the size, Medium: half, High and Ultra: the full size
	const int q = (int)SkyFlags.w;
	const int STEPS = q <= 0 ? 48 : q == 1 ? 64 : q == 2 ? 96 : 128;
	const int LIGHT_STEPS = q + 3;
	// towards the sun over 108 units, further the cloudier (an overcast deck's light shows its
	// thickness: thin parts brighter, its relief)
	const float lightStep = 108.0 * (1 + 2.5 * cShape.x * cShape.x) / LIGHT_STEPS;
	const float pixelScale = q <= 0 ? 1 : q == 1 ? 0.5 : 0.25; // of a quarter-size pixel
	float dt = (t1 - t0) / STEPS;
	// interleaved gradient noise, moved every frame: the temporal anti-aliasing (TAA, FSR 3.1)
	// averages the steps' banding away
	pixel += 5.588238 * fmod(SkyFlags.z, 64);
	float jitter = frac(52.9829189 * frac(dot(pixel, float2(0.06711056, 0.00583715))));
	float3 sun = normalize(CloudLightDir.xyz); // (the clouds' own light: the sun lights them past the ground's dusk)
	float mu = dot(rd, sun);
	// (g 0.8 at a 0.55 share peaked at 25 times the side's light towards the sun: the clouds near
	// it blew out to flat white, the play test of 2026-11-07; now about 5 times, and the light
	// rolled off below white at the end)
	// (and again too bright beside a clear sun under a high cover, the play test of 2026-11-08:
	// now a quarter's share at g 0.6, about 3 times)
	float phase = lerp(HenyeyGreenstein(mu, -0.2), HenyeyGreenstein(mu, 0.6), 0.25) * 12.566371;
	// a sheet scatters the light many times over: the sun shows as a diffuse bright patch, not
	// a cumulus' silver lining (a hot white blot overhead)
	const float sheetiness = cWeather.x;
	phase = lerp(phase, 1, 0.75 * sheetiness);
	float towardSun = saturate(mu * 0.5 + 0.5);
	const float3 sunTint = CloudLightColour.rgb * DuskTint(sun, 0.012); // (at dawn and dusk)
	// at dawn and dusk the sky's light is weak beside the sun's on the clouds: their lit sides glow,
	// their shaded ones are blue-grey
	const float duskK = smoothstep(0.55, 0.05, sun.y) * smoothstep(-0.08, -0.01, sun.y);
	float duskSun = lerp(1, SkyRight.w != 0 ? 1.25 : 1.7, duskK), duskSky = lerp(1, SkyRight.w != 0 ? 0.7 : 0.55, duskK);
	// (the Physical model: at dusk the clouds towards the sun glow, those opposite it are darker,
	// lit by the low sun's light from behind them)
	if (SkyRight.w != 0)
		duskSun *= lerp(1, lerp(0.45, 1.15, towardSun), duskK);
	// Away from the sun at dawn and dusk the clouds take the sky's colour there (the play test of
	// 2026-11-09: mostly a structureless grey): the sky's own colour along the view (skyThere) as
	// their sky light's tint, of mean brightness 1, a little more saturated; the sunlight on them
	// pinker (its red against the sky's blue), less of it scattered many times; the haze the far
	// ones go into tinted the same (below)
	const float away = SkyRight.w != 0 && FogSun.w > 0 ? duskK * smoothstep(0.62, 0.25, towardSun) : 0; // (the half of the sky away from it)
	float3 awaySky = 1;
	float3 sunTintAway = sunTint;
	if (away > 0) {
		// (the physical sky along the view: the belt's pink and the earth's shadow low, the
		// twilight blue higher up; else the haze's horizon there towards the sky's colour above)
		awaySky = dot(skyThere, 1) > 1e-5 ? skyThere : lerp(FogTintDir(float3(rd.x, 0, rd.z)), CloudSkyUp.rgb, saturate(rd.y * 2.5));
		awaySky = clamp(awaySky / max(dot(awaySky, float3(0.3, 0.59, 0.11)), 1e-3), 0, 2.2);
		awaySky = lerp(dot(awaySky, float3(0.3, 0.59, 0.11)).xxx, awaySky, 1.6); // (more saturated)
		sunTintAway = lerp(sunTint, dot(sunTint, float3(0.3, 0.59, 0.11)) * float3(1.2, 0.66, 0.9), away * 0.85);
	}
	float3 light = float3(0, 0, 0);
	float T = 1;
	const float SIGMA = 0.045;
	[loop] for (int i = 0; i < STEPS && T > 0.02; i++) {
		float t = t0 + (i + jitter) * dt;
		float3 p = ro + rd * t;
		// the noise's mip level by the pixel's footprint there (the detail layer's texels per
		// unit), so the distant clouds don't alias
		float lod = log2(1 + t * 0.0035 * pixelScale * 256 * CloudScale.y);
		float footprint = t * 0.0035 * pixelScale;
		cRegion = 1;
		cWarp = 0;
		if (SkyStarY.w >= 1) {
			CloudAt(p.xz);
			cRegion = DensityRegions(p.xz, footprint);
			if (SkyStarY.w >= 2)
				cWarp = CloudWarp(p.xz, saturate((p.y - cShape.z) / cShape.w), lod);
		}
		float dens = CloudDensity(p, lod, footprint, true);
		if (dens <= 0.002)
			continue;
		float od = 0;
		[loop] for (int j = 0; j < LIGHT_STEPS; j++)
			od += CloudDensity(p + sun * ((j + 0.5) * lightStep), lod + 0.5, footprint + (j + 1) * lightStep * 0.15, false) * lightStep;
		float toSun = exp(-SIGMA * od);
		float powder = 1 - exp(-2 * SIGMA * od - 0.15);
		// (a sheet's direct light dimmed by the deck around it: the light steps leave a thin sheet
		// within a few units, which lit an overcast as bright as a fair-weather cloud)
		float energy = toSun * lerp(powder * 1.6, 1, towardSun) * lerp(1, 0.5, sheetiness);
		CloudAt(p.xz); // (the light's steps moved it)
		float h = saturate((p.y - cShape.z) / cShape.w);
		float3 ambient = CloudSkyUp.rgb * (0.45 + 0.55 * h) + CloudSkyHorizon.rgb * 0.15;
		// light scattered many times inside the cloud (the light that a single scattering misses
		// in thick clouds: an overcast sheet is grey, not black), less absorbed and isotropic
		// (away from the sun at dawn and dusk less of it: its orange against the sky's blue made grey)
		energy += lerp(0.35, 0.18, away) * exp(-SIGMA * 0.2 * od);
		ambient = lerp(ambient, dot(ambient, float3(0.3, 0.59, 0.11)).xxx, 0.4 * (1 - away)) * 1.3;
		// (away from the sun at dawn and dusk: the sky's own colour there, its pink and violet
		// belt low and its blue higher up, at the ambient's brightness; their bases darker, their
		// tops lit: relief instead of one grey)
		ambient = lerp(ambient, awaySky * dot(ambient, float3(0.3, 0.59, 0.11)) * lerp(0.6, 1.25, h), away);
		// the weather's light (storm and rain clouds darker, their bases most), and lightning
		// lighting the cloud from inside
		// (and the denser parts of a cloud darker than its thin ones: an overcast sheet's relief)
		float shade = cWeather.w * lerp(1, 0.6 + 0.4 * h, cWeather.y)
			* lerp(1.12, 0.72, saturate(dens / (1.6 * max(cWeather.z, 0.1))));
		float3 flash = float3(0.75, 0.8, 1.0) * (CloudFlash.w * 9 * exp(-length(p - CloudFlash.xyz) / 260));
		float3 s = (sunTintAway * (energy * phase * 1.3 * duskSun) + ambient * (0.85 * duskSky)) * shade + flash;
		float dT = exp(-SIGMA * dens * dt);
		light += T * (1 - dT) * s;
		T *= dT;
	}
	// far clouds fade into the horizon's haze (FogColor, the weather's with the physical sky): as
	// much of it as the sky is cloudy, so an overcast horizon is grey, not the clear sky behind
	float fade = exp(-t0 / 5000);
	// (reaching beyond's at the march's end, 9000 units: fade over the last stretch to 0)
	fade *= saturate((9000 - t0) / 2500);
	// and by the elevation too, over the lowest 9 degrees: by the distance alone the change from
	// the clouds to the haze took a fraction of a degree, a line along the horizon
	fade = min(fade, smoothstep(0.004, 0.16, rd.y));
	float far = (1 - fade) * hazeCover;
	// a soft shoulder: the light above 0.5 rolled off towards 0.82 (the brightest parts keep
	// their relief instead of clipping to one white after the grade and the bloom)
	float lum = max(light.r, max(light.g, light.b));
	if (lum > 0.5)
		light *= (0.5 + (lum - 0.5) / (1 + (lum - 0.5) / 0.32)) / lum;
	// (away from the sun at dawn and dusk the haze the far clouds go into takes the colour of the
	// sky behind them, at its own brightness, higher up: the horizon's dim violet all the way up
	// veiled their pink and blue in grey)
	float3 haze = FogTintDir(rd);
	if (away > 0 && dot(skyThere, 1) > 1e-5) {
		const float3 L = float3(0.3, 0.59, 0.11);
		float3 skyTint = clamp(skyThere / max(dot(skyThere, L), 1e-4), 0, 2.5);
		haze = lerp(haze, dot(haze, L) * lerp(dot(skyTint, L).xxx, skyTint, 1.5), away * 0.85 * smoothstep(0.02, 0.2, rd.y));
	}
	return float4(light * fade + haze * far, lerp(1, T, fade) * (1 - far));
}

// The high clouds above the cumulus layer, as thin textured layers (cheap: a few dozen samples
// a pixel at most, in the sky's own pass at the full size). Both lie in wide organic regions (the
// smooth noise in cloudNoise.a, domain-warped, at tens of kilometres), so they cover large parts
// of the sky with ragged, thinning edges instead of patches.
// - Cirrus at 1600 units, of four kinds mixed by region (HighTypes; the sky's style, CloudHigh.w,
//   shifts which are where, so each sky differs):
//   streaks (fibratus): fine fibres combed along the wind over a milky veil, bent by a two-scale
//   warp, in bundles that fray along their length;
//   mares' tails (uncinus): separate tufts with a hooked head and a long thinning tail, in lanes
//   along the wind;
//   bands (spissatus): broad, denser bands along the wind with feathered edges;
//   ripples (cirrocumulus, undulatus, billows): waves across the wind broken into small cells
//   along their crests.
// - Altocumulus at 800: rounded cells (the shape noise's Worley) with soft edges, smaller and
//   thinner towards the region's edge, shaded by their own thickness; the sheet (stratiformis)
//   closes them into rows across the wind, the patches (floccus) leave them apart. Lenticular
//   clouds now and then: smooth stacked lenses lying across the wind, standing still (they stand
//   over the hills in the wind, the others drift).
// - At dawn and dusk the sunlight on them is reddened by its long way through the air
//   (Rayleigh and Mie, as PhysicalSky's): golden, then orange, red and, against the blue sky's
//   light, pink.
// colour: the sky behind; ro, rd: the camera and the view (up).
float HighNoise(float2 p, float period, float fp)
{
	return cloudNoise.SampleLevel(cloudNoiseSampler, p / period, log2(max(fp * 256 / period, 1))).a;
}

// a high layer's regions: 0-1, how much of the layer is there (cover 0-1 widens them)
float HighRegions(float2 p, float fp, float cover, float scale, float2 seed)
{
	float2 w = float2(HighNoise(p + seed * 9000, 30000 * scale, fp), HighNoise(p.yx + seed * 7000, 30000 * scale, fp)) - 0.5;
	p += w * 9000 * scale;
	float n = HighNoise(p + seed * 5000, 21000 * scale, fp) * 0.6 + HighNoise(p + seed * 3000, 8000 * scale, fp) * 0.3
		+ HighNoise(p + seed * 1000, 2600 * scale, fp) * 0.1;
	float th = lerp(0.78, 0.3, cover);
	return smoothstep(th, th + 0.22, n) * saturate(cover * 20); // (none at 0)
}

// an altocumulus cell's density at q (the shape noise's Worley, about 80 units across)
float HighCell(float2 q, float evolve, float fp)
{
	float4 s = cloudShape.SampleLevel(cloud3DSampler, float3(q / 640, evolve), max(log2(fp * 64 / 640), 0));
	return s.b * 0.62 + s.a * 0.26 + s.g * 0.12;
}

float HighHash(float2 c)
{
	return frac(sin(dot(c, float2(127.1, 311.7))) * 43758.5453);
}

// the cirrus kinds' weights at p: x mares' tails, y bands, z ripples (the rest streaks); style
// 0-1 moves their regions
float3 HighTypes(float2 p, float fp, float style)
{
	float a = HighNoise(p + float2(17000, 3000) + style * 41000, 46000, fp);
	float b = HighNoise(p.yx + float2(9000, 21000) + style * 23000, 33000, fp);
	float s1 = sin(style * 6.2831853), s2 = cos(style * 6.2831853);
	float tails = smoothstep(0.56, 0.68, a + 0.12 * s1);
	float ripples = smoothstep(0.44, 0.32, a - 0.12 * s2);
	float bands = smoothstep(0.58, 0.7, b + 0.1 * s2) * (1 - ripples);
	return float3(tails, bands, ripples);
}

float3 HighClouds(float3 ro, float3 rd, float3 colour)
{
	if (rd.y <= 0.01 || max(CloudHigh.x, CloudFromHigh.x * step(0.001, CloudFront.w)) + max(CloudHigh.y, CloudFromHigh.y * step(0.001, CloudFront.w)) <= 0.001)
		return colour;
	float3 sun = normalize(CloudLightDir.xyz);
	float forward = HenyeyGreenstein(dot(rd, sun), 0.6) * 12.566371; // 1: as much as to the side
	float3 ambient = lerp(CloudSkyUp.rgb, CloudSkyHorizon.rgb, 0.3);
	CloudAt(ro.xz);
	float3 sunLight = CloudLightColour.rgb * cWeather.w * DuskTint(sun, 0.035);
	{
		float duskK = smoothstep(0.55, 0.05, sun.y) * smoothstep(-0.1, -0.02, sun.y);
		sunLight *= lerp(1, 1.6, duskK);
		ambient *= lerp(1, 0.6, duskK);
	}
	const float2 wind = float2(0.948683, 0.316228); // as the cumulus' drift (6, 2)
	const float2 across = float2(-wind.y, wind.x);
	float grazing = 1 / max(rd.y, 0.08); // the pixel's footprint stretched towards the horizon
	// towards the horizon the layers are seen edge on: denser, then lost in the haze
	float edgeOn = lerp(1.5, 1, saturate(rd.y * 3)) * saturate(rd.y * 8);
	{
		float t = (1600 - ro.y) / rd.y;
		CloudAt(ro.xz + rd.xz * t);
		float2 p = ro.xz + rd.xz * t - wind * CloudScale.w * 9;
		float fp = t * 0.0012 * grazing; // the pixel's size there
		float2 styleShift = cHigh.w * float2(61000, 37000); // each sky its own regions
		float regions = HighRegions(p + styleShift, fp, cHigh.x, 1, float2(0.31, 0.77));
		if (regions > 0.001) {
			const int q = (int)SkyFlags.w; // the clouds' quality: Low 0 to Ultra 3
			float3 types = HighTypes(p, fp, cHigh.w);
			if (q <= 0)
				types = float3(0, types.y, 0); // Low: streaks and bands only
			// the wind's direction turning slowly over the sky (the streaks' bend)
			float ang = (HighNoise(p + styleShift, 16000, fp) - 0.5) * 1.8;
			float2 dir = float2(wind.x * cos(ang) - wind.y * sin(ang), wind.x * sin(ang) + wind.y * cos(ang));
			float2 perp = float2(-dir.y, dir.x);
			// the smear's form by the kinds there: its length, how one-sided (a mares' tail: the
			// head downwind, the tail trailing upwind), its hook, the tufts' size
			float streakW = saturate(1 - types.x - types.y);
			float L = lerp(lerp(700, 1000, types.x), 1500, types.y); // (a tuft 1-2 km at 8 km up: here 1600)
			float asym = types.x;
			float hookSide = HighHash(floor((p + styleShift) / 9000)) > 0.5 ? 1 : -1;
			float hook = types.x * 0.3 * hookSide;
			float tuftScale = lerp(1, 2.2, types.y);
			const int N = q <= 0 ? 3 : q == 1 ? 5 : 8;
			float sum = 0, wsum = 0;
			[loop] for (int i = 0; i < N; i++) {
				float s = (i + 0.5) / N;
				float along = lerp(s - 0.5, -s, asym) * L;
				float w = lerp(1, pow(1 - s, 1.5) * 2, asym);
				float2 o = p + dir * along + perp * (hook * s * s * L * 0.3);
				float n = HighNoise(o + 1300, 560 * tuftScale, fp) * 0.65 + HighNoise(o + 700, 200 * tuftScale, fp) * 0.35;
				sum += w * smoothstep(0.48, 0.78, n);
				wsum += w;
			}
			float smear = sum / wsum;
			float d = smoothstep(0.04, lerp(0.85, 0.6, types.y), smear);
			// the fine fibres in it, along the wind
			float2 f = float2(dot(p, wind), dot(p, across) + (HighNoise(p, 14000, fp) - 0.5) * 2600);
			float fine = HighNoise(f * float2(1.0 / 3000, 1.0 / 60) + 0.53, 1, fp / 60) * 0.6
				+ HighNoise(f * float2(1.0 / 9000, 1.0 / 190) + 0.21, 1, fp / 190) * 0.4;
			d *= lerp(0.3, 1.25, fine);
			float opacity = lerp(lerp(0.55, 0.65, types.x), 0.8, types.y);
			// ripples (cirrocumulus, undulatus, billows): waves across the wind (140-320 units
			// apart, bent), broken along their crests into small cells
			if (types.z > 0.01) {
				float lambda = lerp(140, 320, HighNoise(p + 8100, 20000, fp));
				float phase = f.x / lambda + (HighNoise(p + 500, 3000, fp) - 0.5) * 6;
				float wave = 0.5 + 0.5 * sin(phase * 6.2831853);
				float cell = HighCell(p * 1.3, 0.61 + CloudScale.w * 0.0006, fp * 1.3);
				float rip = smoothstep(0.35, 0.95, wave * 0.5 + cell * 0.6) * lerp(0.7, 1, fine);
				// (where the waves are only a few pixels apart, a soft sheet instead: no moirÃ©)
				float resolved = saturate(1.5 - fp / (lambda * 0.035));
				rip = lerp(0.3, rip, resolved);
				d = lerp(d, rip * smoothstep(0.02, 0.3, smear + 0.15), types.z);
				opacity = lerp(opacity, 0.7, types.z);
			}
			d = saturate(d + 0.08) * regions; // (with the veil)
			float a = saturate(d * opacity * edgeOn) * exp(-t / 16000);
			// thin ice: white, lit through towards the sun (a halo's glow), little shade
			float3 c = sunLight * (0.66 + 0.4 * forward) * lerp(1, 0.9, d) + ambient * 0.6;
			colour = lerp(colour, c, a);
		}
	}
	{
		float t = (800 - ro.y) / rd.y;
		CloudAt(ro.xz + rd.xz * t);
		float2 still = ro.xz + rd.xz * t; // (where the lenticulars stand)
		float2 p = still - wind * CloudScale.w * 7;
		float fp = t * 0.0012 * grazing;
		float sheet = cHigh.z;
		float regions = HighRegions(p + cHigh.w * float2(43000, 71000), fp, cHigh.y, lerp(0.55, 0.8, sheet), float2(0.13, 0.42));
		if (regions > 0.001) {
			// the cells (about 80 units across, slowly changing), ragged by smaller ones; in a
			// sheet set in rows across the wind
			float evolve = 0.37 + CloudScale.w * 0.0008;
			float2 rowP = p + across * (HighNoise(p, 5000, fp) - 0.5) * 900 + wind * (HighNoise(p + 1700, 1800, fp) - 0.5) * 160;
			float rows = 0.5 + 0.5 * sin(dot(rowP, wind) * (6.2831853 / 150));
			float rowsHere = smoothstep(0.35, 0.7, HighNoise(p + 4100, 3200, fp)); // rows in places, not everywhere
			float cell = HighCell(p, evolve, fp) + (rows - 0.5) * 0.2 * sheet * rowsHere;
			// ragged, cauliflower edges: eaten by the 3D detail noise (about 20 units)
			if (SkyFlags.w >= 2) { // (High and Ultra)
				float3 dn = cloudDetail.SampleLevel(cloud3DSampler, float3(p / 160, evolve * 3), max(log2(fp * 32 / 160), 0)).rgb;
				cell -= (1 - (dn.r * 0.6 + dn.g * 0.4)) * 0.14;
			}
			else
				cell -= 0.06;
			// smaller and thinner towards the region's edge; a sheet's cells join
			float th = lerp(0.6, 0.44, regions) - 0.08 * sheet;
			float d = smoothstep(th, th + 0.3, cell) * regions;
			// the cell's own shadow: as thick as it is towards the sun (its middle grey from below,
			// its thin rims lit through)
			float dSun = SkyFlags.w >= 1 ? smoothstep(th, th + 0.3, HighCell(p + sun.xz / max(sun.y, 0.25) * 18, evolve, fp)) * regions : d; // (Low: its own thickness)
			float lit = 1 - saturate(smoothstep(0.3, 1, d) * 0.6 + dSun * 0.35);
			float a = saturate(d * lerp(0.8, 1.15, sheet) * edgeOn) * 0.88 * exp(-t / 9000);
			// the shaded parts are lit by the sky (blue-grey), the sunlit ones by the sun
			float3 shadowed = lerp(ambient, dot(ambient, float3(0.3, 0.59, 0.11)).xxx, 0.5) * 1.05;
			float3 sunlit = sunLight * (0.62 + 0.4 * (1 - d) * forward) + ambient * 0.45;
			float3 c = lerp(shadowed, sunlit, lit);
			colour = lerp(colour, c, a);
		}
	}
	// lenticular: in some places of some skies, while there is high cloud at all; smooth lenses
	// 1-2 km long lying across the wind, two or three stacked a little apart along it, their
	// edges finely streaked
	{
		float amount = step(1, SkyFlags.w) * saturate(max(cHigh.x, cHigh.y) * 3) * smoothstep(0.62, 0.72, HighNoise(float2(cHigh.w * 37000, 5300), 52000, 0));
		float t = (900 - ro.y) / rd.y;
		float2 q = ro.xz + rd.xz * t;
		float fp = t * 0.0012 * grazing;
		float place = smoothstep(0.6, 0.75, HighNoise(q + 13000 + cHigh.w * 29000, 24000, fp)) * amount;
		if (place > 0.001) {
			float2 g = float2(dot(q, wind) / 1500, dot(q, across) / 2800);
			float2 gi = floor(g), gf = frac(g) - 0.5;
			float2 jit = float2(HighHash(gi), HighHash(gi + 9.7)) - 0.5;
			float present = step(0.55, HighHash(gi + 4.3));
			float stacks = 1 + floor(HighHash(gi + 2.1) * 3); // 1-3
			float len = lerp(0.22, 0.34, HighHash(gi + 5.5)); // half its length, in cells across
			float lens = 0;
			[unroll] for (int k = 0; k < 3; k++) {
				if (k >= stacks)
					break;
				float2 o = gf - jit * float2(0.2, 0.2) - float2((k - (stacks - 1) * 0.5) * 0.17, k * 0.025);
				float2 e = o / float2(0.06 * (1 - 0.18 * k), len * (1 - 0.15 * k));
				float r = dot(e, e);
				lens = max(lens, smoothstep(1, 0.25, r) * lerp(0.55, 1, HighNoise(float2(dot(q, across), dot(q, wind) * 8) / 3000 + k * 0.3, 1, fp / 375)));
			}
			float d = lens * present * place;
			float a = saturate(d * 0.85 * edgeOn) * exp(-t / 12000);
			float3 c = sunLight * (0.7 + 0.45 * forward) * lerp(1.05, 0.85, d) + ambient * 0.55;
			colour = lerp(colour, c, a);
		}
	}
	return colour;
}

// A physical sky (with the volumetric clouds, instead of the sky box, whose painted clouds
// would show through and disagree with the real ones): single scattering in a planet's
// atmosphere, Rayleigh and Mie with exponential densities (8 and 1.2 km scale heights), 8 steps
// along the view and 4 towards the sun, tone-mapped to 0-1; the sun's disc through the air.
// SkyLightFromPhysical (DiligentSkyRenderer.cpp) is the same on the CPU, for the sky light.
static const float ATM_EARTH = 6360, ATM_TOP = 6420;
static const float3 ATM_RAYLEIGH = float3(5.8e-3, 13.5e-3, 33.1e-3);
static const float ATM_MIE = 21e-3;
// ozone's absorption (its Chappuis band: orange and red), in a layer round 25 km: the deep blue
// overhead at sunset and in the twilight
static const float3 ATM_OZONE = float3(0.65e-3, 1.881e-3, 0.085e-3);
float OzoneDensity(float h)
{
	return max(0, 1 - abs(h - 25) / 15);
}

float AtmTop(float3 o, float3 d)
{
	float b = dot(o, d), c = dot(o, o) - ATM_TOP * ATM_TOP;
	return -b + sqrt(max(b * b - c, 0));
}

float3 PhysicalSky(float3 rd, float3 sun)
{
	rd = normalize(float3(rd.x, max(rd.y, 0.0), rd.z));
	float3 o = float3(0, ATM_EARTH + 0.3, 0);
	float len = AtmTop(o, rd);
	float odR = 0, odM = 0, odO = 0;
	float3 sumR = 0, sumM = 0;
	// RenderSettings::skyModel 1 (Physical): ozone, the earth's shadow, the samples denser near
	// the eye; 0 (Normal) as before: evenly spaced, neither
	const bool physical = SkyRight.w != 0;
	[loop] for (int i = 0; i < 8; i++) {
		// the samples denser near the eye (squared spacing): the low, dense air along the
		// horizon's long path, where the sunset's colours come from
		float u0 = i / 8.0, u1 = (i + 1) / 8.0;
		if (!physical) {
			u0 = sqrt(u0);
			u1 = sqrt(u1);
		}
		float ds = len * (u1 * u1 - u0 * u0);
		float3 p = o + rd * (len * (u0 * u0 + u1 * u1) * 0.5);
		float h = length(p) - ATM_EARTH;
		float dR = exp(-h / 8) * ds, dM = exp(-h / 1.2) * ds, dO = physical ? OzoneDensity(h) * ds : 0;
		odR += dR;
		odM += dM;
		odO += dO;
		// the earth's shadow: no sunlight where the ground stands between the air and the sun
		// (after sunset the shadow rises opposite it, the pink band of the lit air above)
		float b = dot(p, sun), cc = dot(p, p) - ATM_EARTH * ATM_EARTH;
		if (physical && b < 0 && b * b > cc)
			continue;
		float dl = AtmTop(p, sun) / 4, lR = 0, lM = 0, lO = 0;
		[unroll] for (int j = 0; j < 4; j++) {
			float hl = length(p + sun * ((j + 0.5) * dl)) - ATM_EARTH;
			lR += exp(-hl / 8) * dl;
			lM += exp(-hl / 1.2) * dl;
			lO += physical ? OzoneDensity(hl) * dl : 0;
		}
		float3 att = exp(-(ATM_RAYLEIGH * (odR + lR) + ATM_MIE * 1.1 * (odM + lM) + ATM_OZONE * (odO + lO)));
		sumR += dR * att;
		sumM += dM * att;
	}
	// (Mie at g 0.76 and its full weight lit a white aureole some 30 degrees across, which the
	// grade and the bloom blew out over a quarter of the sky, the play test of 2026-11-08: now a
	// narrower lobe at a third of the weight, the glow close round the sun)
	float mu = dot(rd, sun), g = 0.83;
	float pR = 0.0596831 * (1 + mu * mu);
	float pM = 0.35 * 0.1193662 * ((1 - g * g) * (1 + mu * mu)) / ((2 + g * g) * pow(1 + g * g - 2 * g * mu, 1.5));
	float3 c = 20 * (sumR * ATM_RAYLEIGH * pR + sumM * ATM_MIE * pM);
	float3 trans = exp(-(ATM_RAYLEIGH * odR + ATM_MIE * 1.1 * odM + ATM_OZONE * odO));
	c += trans * 30 * smoothstep(0.99985, 0.99996, mu);
	// the twilight's blue (the Physical model): the light scattered more than once, which the
	// single scattering above leaves out (in the earth's shadow the sky went black at once, a night
	// sky with the sun just under the horizon, the play test of 2026-11-09): azure on the sun's
	// side, dimmer away from it, from the sun 6 degrees up until it is 11 under the horizon
	if (physical) {
		float tw = saturate((sun.y + 0.2) / 0.18) * (1 - smoothstep(-0.02, 0.05, sun.y));
		float2 hd = rd.xz, hs = sun.xz;
		float side = (dot(hd, hd) > 1e-8 && dot(hs, hs) > 1e-8) ? dot(normalize(hd), normalize(hs)) * 0.5 + 0.5 : 0.5;
		// (towards the sun the low sky keeps its warm glow: the blue mostly higher up there; the
		// grey-lavender sunrise, the play test of 2026-11-09)
		float up = lerp(1, smoothstep(0.0, 0.45, rd.y), side * side);
		c += float3(0.1, 0.24, 0.52) * tw * lerp(0.6, 1, side) * up * (1.2 - 0.4 * rd.y);
		// dawn's and dusk's glow on the horizon round the sun's bearing: the lit high air beyond
		// the earth's shadow, which the few samples miss (before sunrise the sky towards the sun
		// stood dark navy, the SUNRISE save of 2026-11-09): from the sun 8 degrees under the
		// horizon, gone by 7 up
		float dawn = saturate((sun.y + 0.21) / 0.14) * (1 - smoothstep(0.03, 0.12, sun.y));
		c += float3(0.9, 0.42, 0.12) * dawn * pow(side, 4) * exp(-rd.y * 9) * 1.1;
	}
	return 1 - exp(-c * 1.2);
}

// The night sky, added over the graded sky as much as the clouds let it through: the
// moon's disc (its seas from the cloud noise, its limb darker) and the glow around it, and the
// stars, a few thousand over the sky, turning about the pole with the clock, the faint ones
// most numerous. Brightness on the screen (after the grade); px: a pixel's angle (radians).
float3 NightSky(float3 rd, float px)
{
	float3 c = 0;
	// the moon: 1.5 degrees across (three times the real one's, as games draw it)
	float mu = dot(rd, SkyMoon.xyz);
	float ang = acos(clamp(mu, -1, 1));
	float radius = max(0.0130, px * 2.5);
	float disc = 0; // how much the disc covers (the stars behind it hidden)
	if (SkyMoon.w > 0) {
		const float3 moonCol = float3(0.93, 0.95, 1.0);
		const float lit = SkyStarX.w;
		// the glow: a bright aureole close by, a broad faint halo, as bright as its lit part
		c = moonCol * (0.22 * exp(-max(ang - radius, 0) / 0.02) + 0.05 * exp(-ang / 0.25)) * lit;
		if (ang < radius * 1.5) {
			// the disc's own frame: its seas, a fixed pattern of the noise
			float3 mx = normalize(cross(SkyMoon.xyz, float3(0.001, 1, 0)));
			float3 my = cross(mx, SkyMoon.xyz);
			float2 q = float2(dot(rd, mx), dot(rd, my)) / radius; // -1..1 over the disc
			float r = length(q);
			float seas = cloudNoise.SampleLevel(cloudNoiseSampler, q * 0.2 + float2(0.37, 0.61), 0).r;
			float albedo = lerp(1.0, 0.72, smoothstep(0.45, 0.65, seas));
			float limb = sqrt(saturate(1 - r * r)) * 0.3 + 0.7;
			float edge = saturate((1 - r) * radius / max(px, 1e-5) + 0.5); // antialiased
			// the phase: the ball's side towards the sun lit (the sun's direction from the moon as
			// from here), a soft terminator; the dark side faintly in the earth's light, most
			// in a thin crescent (the earth near full as seen from it)
			float3 n = mx * q.x + my * q.y - SkyMoon.xyz * sqrt(saturate(1 - r * r));
			float sunSide = smoothstep(-0.04, 0.10, dot(n, SkySun.xyz));
			float earthshine = 0.035 * (1 - lit) * (1 - lit);
			// (the disc in front of its glow; the sky behind it, outc, about 0.1)
			c = lerp(c, moonCol * (albedo * limb * 1.02) * max(sunSide, earthshine), edge);
			disc = edge;
		}
		c *= SkyMoon.w;
	}
	// the stars: a point in some cells of a 3D grid through the sphere, a pixel or two across,
	// fainter towards the horizon (the air) and in the moon's glow
	float3 sd = float3(dot(rd, SkyStarX.xyz), dot(rd, SkyStarY.xyz), dot(rd, SkyStarZ.xyz));
	const float GRID = 110;
	float3 g = sd * GRID;
	float3 cell = floor(g);
	float3 f = frac(g) - 0.5;
	float3 side = abs(f.x) > abs(f.y) ? (abs(f.x) > abs(f.z) ? float3(sign(f.x), 0, 0) : float3(0, 0, sign(f.z)))
		: (abs(f.y) > abs(f.z) ? float3(0, sign(f.y), 0) : float3(0, 0, sign(f.z)));
	float stars = 0;
	[unroll] for (int k = 0; k < 2; k++) { // the cell and its neighbour across its nearest face
		float3 cc = cell + side * k;
		float3 h = Hash33(cc);
		float3 h2 = Hash33(cc + 17.3);
		if (h2.x < 0.085) {
			float3 sp = normalize((cc + 0.2 + 0.6 * h) / GRID);
			float3 dv = sd - sp;
			float d2 = dot(dv, dv);
			float mag = pow(h2.y, 5); // most faint, a few bright
			float size = px * (0.75 + 0.6 * mag);
			float tw = 0.75 + 0.25 * sin(CloudScale.w * 3 + h2.z * 60); // a little twinkle
			stars += exp(-d2 / (size * size)) * (0.16 + 1.25 * mag) * tw;
		}
	}
	c += float3(0.86, 0.9, 1.0) * stars * saturate(rd.y * 5) * saturate(1 - SkyMoon.w * SkyStarX.w * 0.7 * exp(-ang / 0.3)) * (1 - disc * SkyMoon.w);
	return c * SkySun.w;
}

// The air's own colour at night, before the grade (dark blue, lighter towards the horizon and
// around the moon: its light scattered)
float3 NightAir(float3 rd)
{
	float mu = dot(rd, SkyMoon.xyz);
	float h = saturate(rd.y);
	float3 c = lerp(float3(0.070, 0.095, 0.17), float3(0.028, 0.045, 0.105), sqrt(h));
	c += float3(0.06, 0.08, 0.12) * SkyMoon.w * SkyStarX.w * (0.35 + 0.65 * saturate(mu) * saturate(mu)) * (0.6 + 0.4 * (1 - h));
	return c;
}

float4 PS(VS_OUTPUT input) : SV_Target
{
	float3 d = CamForward.xyz + input.Ndc.x * CamForward.w * CamRight.xyz + input.Ndc.y * CamRight.w * CamUp.xyz;
	float a = dot(d, SkyFront.xyz), b = dot(d, SkyRight.xyz), c = d.y;
	float horizontal = sqrt(a * a + b * b);
	float3 rd = normalize(d);
	// The original's five-face sky always fades into the fog colour at the horizon and below,
	// where it has no texture. Only a full six-face sky may drop the fade, when the fog is off
	// (fog start beyond any distance).
	bool sixFaces = SkyFlags.x != 0;
	float fog = (sixFaces && FogStartDist > 1e8) ? 0 : (c < 0 ? 1 : SkyFog(c / max(horizontal, 1e-6)));
	float3 colour = FogTintDir(rd);
	float night = SkySun.w;
	float overcast = 0; // how much the weather's clouds over the sky box hide (the stars, the moon)
	float3 highAdded = 0; // what the high clouds changed of the sky (hidden behind the cumulus below)
	if (fog < 1) {
		float3 tex;
		if (SkyFlags.y != 0) { // the volumetric clouds: the physical sky
			// lit by the sun itself (with the clock: below the horizon at night, the twilight
			// after it; not by the moon, the night's light), the night's air under it
			float3 sun = dot(CloudSun.xyz, CloudSun.xyz) > 0.01 ? normalize(CloudSun.xyz) : float3(0.5, 0.8, 0.3);
			if (night > 0)
				sun = SkySun.xyz;
			tex = sun.y > -0.25 ? PhysicalSky(rd, sun) : 0;

			tex = max(tex, NightAir(rd) * night);
			float3 bare = tex;
			tex = HighClouds(FogCamera.xyz, rd, tex);
			highAdded = (tex - bare) * (1 - fog);
		}
		else {
			if (c >= max(abs(a), abs(b)))
				tex = skyTop.SampleLevel(skySampler, float2(0.5 + 0.5 * b / c, 0.5 + 0.5 * a / c), 0).rgb;
			else if (-c >= max(abs(a), abs(b)))
				tex = skyBottom.SampleLevel(skySampler, float2(0.5 - 0.5 * b / c, 0.5 + 0.5 * a / c), 0).rgb;
			else if (abs(a) >= abs(b)) {
				if (a > 0) tex = skyFront.SampleLevel(skySampler, float2(0.5 + 0.5 * b / a, 0.5 - 0.5 * c / a), 0).rgb;
				else       tex = skyBack.SampleLevel(skySampler, float2(0.5 + 0.5 * b / a, 0.5 + 0.5 * c / a), 0).rgb;
			}
			else {
				if (b > 0) tex = skyRight.SampleLevel(skySampler, float2(0.5 - 0.5 * a / b, 0.5 - 0.5 * c / b), 0).rgb;
				else       tex = skyLeft.SampleLevel(skySampler, float2(0.5 - 0.5 * a / b, 0.5 + 0.5 * c / b), 0).rgb;
			}
			// the painted day sky gives way to the night's air
			tex = lerp(tex, NightAir(rd), night * 0.9);
			// the weather's clouds over the sky box (no volumetric ones): a grey deck with the
			// ground shadows' cover (CloudCover at the layer's middle), its thicker parts darker,
			// even towards the horizon
			if (SkyOvercast.w > 0 && c > 0) {
				float t = (CloudShape.z + CloudShape.w * 0.5 - FogCamera.y) / max(rd.y, 0.02);
				float2 xz = FogCamera.xz + rd.xz * t;
				CloudAt(xz);
				float lod = log2(max(t * 0.0012 / max(rd.y, 0.05) * 256 * CloudScale.x, 1));
				float cc = lerp(0.65, CloudCover(xz, lod), exp(-t / 7000));
				// how much: by the weather's cover there (a storm's as an overcast); broken
				// clouds in patches, an overcast all over with its relief
				float amount = saturate((max(cShape.x, cWeather.y * 0.85) - 0.3) / 0.45) * 0.96;
				float k = SkyOvercast.w * amount * lerp(smoothstep(0.25, 0.75, cc), lerp(0.75, 1.0, cc), amount);
				float3 deck = SkyOvercast.rgb * lerp(1.12, 0.78, cc) * lerp(1.0, 1.15, saturate(1 - rd.y * 3));
				deck = lerp(deck, FogTintDir(rd), saturate(1 - rd.y * 5) * 0.7); // into the haze at the horizon
				tex = lerp(tex, deck, k);
				overcast = k;
			}
		}
		colour = lerp(tex, FogTintDir(rd), fog);
	}
	// the dynamic clouds' layer (RenderSettings::dynamicClouds 3) in front of the sky box,
	// marched at a quarter, half or the full size (PS_Clouds)
	float visible = 1 - overcast; // of the stars and the moon
	if (SkyFlags.y != 0 && c > 0) {
		float2 uv = float2(input.Ndc.x * 0.5 + 0.5, 0.5 - input.Ndc.y * 0.5);
		float cw, ch;
		tClouds.GetDimensions(cw, ch);
		float2 o = (SkyFlags.w >= 2 ? 0.5 : 0.75) / float2(cw, ch); // a small blur over the march's jitter
		float4 cl = (tClouds.SampleLevel(skySampler, uv + float2(o.x, o.y), 0) + tClouds.SampleLevel(skySampler, uv + float2(-o.x, o.y), 0)
			+ tClouds.SampleLevel(skySampler, uv + float2(o.x, -o.y), 0) + tClouds.SampleLevel(skySampler, uv - o, 0)) * 0.25;
		// the high clouds far behind a cumulus: hidden wherever it has some substance (its
		// transmittance alone let them show through its thin parts, the play test of 2026-11-07)
		colour -= highAdded * (1 - cl.a * cl.a * cl.a);
		colour = colour * cl.a + cl.rgb;
		visible = cl.a;
	}
	// at night the low sky goes into the fog's colour over its lowest 17 degrees or so, the stars
	// with it (the haze near the horizon): the map edge's fog is that colour, and the mountains
	// past it stood dark against the stars (the play test of 2026-11-07)
	// (the Physical sky model: only once the sun is 2 degrees under the horizon, all of it from
	// 8, in the haze's colours along the horizon: with the night's darkness from the clock, 0.6
	// with the sun on the horizon, the sunset stood behind a dark wall, the play test of 2026-11-09)
	const float lowNight = SkyRight.w != 0 ? night * saturate((-SkySun.y - 0.035) / 0.1) : night;
	float lowHaze = lowNight * (1 - smoothstep(0.0, 0.3, rd.y));
	colour = lerp(colour, SkyRight.w != 0 ? FogTintDir(rd) : FogColor.rgb, lowHaze);
	float3 outc = GradeColour(colour);
	// the moon and the stars over the graded sky (the night's grade would dim them to grey)
	if (night > 0 && c > 0 && fog < 1)
	{
		// (the Physical model: in the twilight the stars come out away from the sun first)
		float starSide = 1;
		if (SkyRight.w != 0 && dot(rd.xz, rd.xz) > 1e-8 && dot(SkySun.xz, SkySun.xz) > 1e-8)
			starSide = lerp(saturate(0.5 - 0.5 * dot(normalize(rd.xz), normalize(SkySun.xz))), 1, night * night);
		outc += NightSky(rd, CamRight.w * 2 / max(CamUp.w, 1)) * visible * (1 - fog) * (1 - lowHaze) * starSide;
	}
	return float4(outc + Dither(input.Pos.xy), 1);
}

float4 PS_Clouds(VS_OUTPUT input) : SV_Target
{
	float3 d = CamForward.xyz + input.Ndc.x * CamForward.w * CamRight.xyz + input.Ndc.y * CamRight.w * CamUp.xyz;
	float3 rd = normalize(d);
	// the sky behind the clouds (their colour away from the sun at dawn and dusk; the Physical
	// model only, the sun near the horizon: 8 x 4 steps of air once per pixel)
	float3 skyThere = 0;
	float3 sun = SkySun.w > 0 ? SkySun.xyz : (dot(CloudSun.xyz, CloudSun.xyz) > 0.01 ? normalize(CloudSun.xyz) : float3(0.5, 0.8, 0.3));
	if (SkyRight.w != 0 && sun.y < 0.55 && sun.y > -0.1 && rd.y > 0.002 && dot(rd, sun) < 0.3) // (where MarchClouds' away is)
		skyThere = PhysicalSky(rd, sun);
	return MarchClouds(FogCamera.xyz, rd, input.Pos.xy, skyThere);
}
)---";

// The sun's light through the air from the eye towards it (the Physical model's: ozone too)
static void SunTransmittanceCpu(const Vector3& sun, float out[3])
{
	const float EARTH = 6360, TOP = 6420, RAY[3] = { 5.8e-3f, 13.5e-3f, 33.1e-3f }, MIE = 21e-3f, OZONE[3] = { 0.65e-3f, 1.881e-3f, 0.085e-3f };
	const Vector3 d = Vector3(sun.x, std::max(sun.y, 0.0f), sun.z).normal();
	const Vector3 o(0, EARTH + 0.3f, 0);
	const float b = o.dot(d), c = o.dot(o) - TOP * TOP;
	const float len = -b + std::sqrt(std::max(b * b - c, 0.0f));
	float odR = 0, odM = 0, odO = 0;
	for (int i = 0; i < 16; i++) {
		const float u0 = i / 16.0f, u1 = (i + 1) / 16.0f;
		const float ds = len * (u1 * u1 - u0 * u0);
		const float h = (o + d * (len * (u0 * u0 + u1 * u1) * 0.5f)).len3() - EARTH;
		odR += std::exp(-h / 8) * ds;
		odM += std::exp(-h / 1.2f) * ds;
		odO += std::max(0.0f, 1 - std::abs(h - 25) / 15) * ds;
	}
	for (int i = 0; i < 3; i++)
		out[i] = std::exp(-(RAY[i] * odR + MIE * 1.1f * odM + OZONE[i] * odO));
}

// The physical sky's colour towards d (unit, y up) with the sun towards sun: PhysicalSky of the
// sky shader on the CPU
static void PhysicalSkyCpu(const Vector3& dIn, const Vector3& sun, float out[3])
{
	const bool physical = g_renderSettings.skyModel >= 1; // (as the shader's SkyRight.w)
	const float EARTH = 6360, TOP = 6420, RAY[3] = { 5.8e-3f, 13.5e-3f, 33.1e-3f }, MIE = 21e-3f, OZONE[3] = { 0.65e-3f, 1.881e-3f, 0.085e-3f };
	auto ozone = [](float h) { return std::max(0.0f, 1 - std::abs(h - 25) / 15); };
	const Vector3 d = Vector3(dIn.x, std::max(dIn.y, 0.0f), dIn.z).normal();
	auto top = [&](const Vector3& o, const Vector3& v) {
		const float b = o.dot(v), c = o.dot(o) - TOP * TOP;
		return -b + std::sqrt(std::max(b * b - c, 0.0f));
	};
	const Vector3 o(0, EARTH + 0.3f, 0);
	const float len = top(o, d);
	float odR = 0, odM = 0, odO = 0, sumR[3] = {}, sumM[3] = {};
	for (int i = 0; i < 8; i++) {
		const float u0 = physical ? i / 8.0f : std::sqrt(i / 8.0f), u1 = physical ? (i + 1) / 8.0f : std::sqrt((i + 1) / 8.0f);
		const float ds = len * (u1 * u1 - u0 * u0);
		const Vector3 p = o + d * (len * (u0 * u0 + u1 * u1) * 0.5f);
		const float h = p.len3() - EARTH;
		const float dR = std::exp(-h / 8) * ds, dM = std::exp(-h / 1.2f) * ds;
		odR += dR;
		odM += dM;
		odO += physical ? ozone(h) * ds : 0.0f;
		const float b = p.dot(sun), cc = p.dot(p) - EARTH * EARTH;
		if (physical && b < 0 && b * b > cc)
			continue; // in the earth's shadow
		const float dl = top(p, sun) / 4;
		float lR = 0, lM = 0, lO = 0;
		for (int j = 0; j < 4; j++) {
			const float hl = (p + sun * ((j + 0.5f) * dl)).len3() - EARTH;
			lR += std::exp(-hl / 8) * dl;
			lM += std::exp(-hl / 1.2f) * dl;
			lO += physical ? ozone(hl) * dl : 0.0f;
		}
		for (int c = 0; c < 3; c++) {
			const float att = std::exp(-(RAY[c] * (odR + lR) + MIE * 1.1f * (odM + lM) + OZONE[c] * (odO + lO)));
			sumR[c] += dR * att;
			sumM[c] += dM * att;
		}
	}
	const float mu = d.dot(sun), g = 0.83f; // (as PhysicalSky's)
	const float pR = 0.0596831f * (1 + mu * mu);
	const float pM = 0.35f * 0.1193662f * ((1 - g * g) * (1 + mu * mu)) / ((2 + g * g) * std::pow(1 + g * g - 2 * g * mu, 1.5f));
	// the twilight's blue (as the shader's)
	float twilight[3] = {};
	if (physical) {
		const float rise = std::clamp((sun.y + 0.02f) / 0.07f, 0.0f, 1.0f);
		const float tw = std::clamp((sun.y + 0.2f) / 0.18f, 0.0f, 1.0f) * (1 - rise * rise * (3 - 2 * rise));
		const float hd = std::sqrt(d.x * d.x + d.z * d.z), hs = std::sqrt(sun.x * sun.x + sun.z * sun.z);
		const float side = (hd > 1e-4f && hs > 1e-4f) ? (d.x * sun.x + d.z * sun.z) / (hd * hs) * 0.5f + 0.5f : 0.5f;
		const float t = std::clamp(d.y / 0.45f, 0.0f, 1.0f), up = 1 + (t * t * (3 - 2 * t) - 1) * side * side;
		const float k = tw * (0.6f + 0.4f * side) * up * (1.2f - 0.4f * d.y);
		const float blue[3] = { 0.1f, 0.24f, 0.52f }, warm[3] = { 0.9f, 0.42f, 0.12f };
		const float r = std::clamp((sun.y - 0.03f) / 0.09f, 0.0f, 1.0f);
		const float dawn = std::clamp((sun.y + 0.21f) / 0.14f, 0.0f, 1.0f) * (1 - r * r * (3 - 2 * r));
		const float g = dawn * side * side * side * side * std::exp(-d.y * 9) * 1.1f;
		for (int c = 0; c < 3; c++)
			twilight[c] = blue[c] * k + warm[c] * g;
	}
	for (int c = 0; c < 3; c++)
		out[c] = 1 - std::exp(-(20 * (sumR[c] * RAY[c] * pR + sumM[c] * MIE * pM) + twilight[c]) * 1.2f);
}

struct DiligentSkyRenderer::Impl {
	DiligentGfx* dgfx;
	// the sky box's own light (ShadowEnvironment::skyLight), for when the physical sky is off
	float boxLight[3][3] = {};
	float boxLuminance = 1;
	bool boxLightValid = false;
	Vector3 physicalSun = Vector3(0, -2, 0); // the sun the physical sky light was worked out for
	float physicalCover = -1; // and the cloud cover
	int physicalModel = -1; // and RenderSettings::skyModel
	// the haze's tint (ShadowEnvironment::fogTint) as worked out for this true sun and cover
	Vector3 tintSun = Vector3(0, -2, 0);
	float tintCover = -1, horizon[32][4] = {}, sunColour[3] = {};
	Vector3 reflSun = Vector3(0, -2, 0), reflMoon = Vector3(0, -2, 0);
	float reflCover = -1, reflNight = -1, reflMoonShows = -1, reflSky[50][4] = {};
	int reflModel = -1;
	const Camera* camera;
	texture faces[6] = {};
	bool sixFaces = false;
	RefCntAutoPtr<IPipelineResourceSignature> signature;
	RefCntAutoPtr<IPipelineState> pipeline;
	RefCntAutoPtr<IShaderResourceBinding> srb;
	RefCntAutoPtr<IBuffer> skyBuffer;
	// the dynamic clouds at a quarter of the scene's size (RenderSettings::dynamicClouds 3)
	RefCntAutoPtr<IPipelineResourceSignature> cloudSignature;
	RefCntAutoPtr<IPipelineState> cloudPipeline;
	RefCntAutoPtr<IShaderResourceBinding> cloudSrb;
	RefCntAutoPtr<ITexture> cloudTarget;
	bool loaded = false;
};

DiligentSkyRenderer::DiligentSkyRenderer(IRenderer* gfx, const Terrain* terrain, const Camera* camera)
{
	_impl = new Impl;
	_impl->dgfx = (DiligentGfx*)gfx;
	_impl->camera = camera;
	IRenderDevice* device = _impl->dgfx->device;

	std::string dir;
	for (wchar_t c : terrain->skyTextureDirectory) {
		if (!c) break; // the .bcm's string includes its terminator
		dir += (char)c;
	}
	if (!dir.empty() && dir.back() != '\\' && dir.back() != '/')
		dir += '\\';
	static const char* faceNames[6] = { "sky_front.tga", "sky_right.tga", "sky_back.tga", "sky_left.tga", "sky_top.tga", "sky_bottom.tga" };
	_impl->loaded = !terrain->skyTextureDirectory.empty();
	// the sky light (ShadowEnvironment::skyLight): the top face's mean colour above, the side
	// faces' lowest quarter (image rows at the bottom are the horizon) at the horizon, the
	// bottom face's below (or the horizon's, darker, as ground)
	double sums[3][3] = {};
	auto meanColour = [](const Bitmap& bmp, int fromRow, double out[3]) {
		Bitmap rgba = bmp.format == BMFORMAT_R8G8B8A8 ? bmp : bmp.convertToR8G8B8A8();
		double s[3] = {};
		size_t n = 0;
		for (int y = fromRow; y < rgba.height; y++)
			for (int x = 0; x < rgba.width; x++, n++)
				for (int c = 0; c < 3; c++)
					s[c] += rgba.pixels[((size_t)y * rgba.width + x) * 4 + c];
		for (int c = 0; c < 3; c++)
			out[c] += n ? s[c] / n / 255.0 : 0.0;
	};
	for (int i = 0; i < 5 && _impl->loaded; i++) {
		const std::string path = dir + faceNames[i];
		if (!FileExists(path.c_str())) {
			printf("No sky: \"%s\" not found\n", path.c_str());
			_impl->loaded = false;
			break;
		}
		const Bitmap bmp = Bitmap::loadBitmap(path.c_str());
		if (!bmp.compressed())
			meanColour(bmp, i == 4 ? 0 : bmp.height * 3 / 4, sums[i == 4 ? 0 : 1]);
		_impl->faces[i] = gfx->CreateTexture(bmp, 1);
	}
	if (!_impl->loaded)
		return;
	const std::string bottomPath = dir + faceNames[5];
	bool hasBottom = false;
	if (FileExists(bottomPath.c_str())) {
		const Bitmap bmp = Bitmap::loadBitmap(bottomPath.c_str());
		if (!bmp.compressed()) {
			meanColour(bmp, 0, sums[2]);
			hasBottom = true;
		}
		_impl->faces[5] = gfx->CreateTexture(bmp, 1);
		_impl->sixFaces = true;
	}
	auto& light = _impl->dgfx->shadows;
	for (int c = 0; c < 3; c++) {
		light.skyLight[0][c] = (float)sums[0][c];
		light.skyLight[1][c] = (float)(sums[1][c] / 4);
		light.skyLight[2][c] = hasBottom ? (float)sums[2][c] : light.skyLight[1][c] * 0.5f;
	}
	auto lum = [](const float* c) { return 0.299f * c[0] + 0.587f * c[1] + 0.114f * c[2]; };
	light.skyLightLuminance = std::max(0.05f, (lum(light.skyLight[0]) + 2 * lum(light.skyLight[1]) + lum(light.skyLight[2])) / 4);
	light.skyLightValid = true;
	memcpy(_impl->boxLight, light.skyLight, sizeof(_impl->boxLight));
	_impl->boxLuminance = light.skyLightLuminance;
	_impl->boxLightValid = true;
	printf("Sky light: above %.2f %.2f %.2f, horizon %.2f %.2f %.2f, below %.2f %.2f %.2f\n", light.skyLight[0][0], light.skyLight[0][1], light.skyLight[0][2],
		light.skyLight[1][0], light.skyLight[1][1], light.skyLight[1][2], light.skyLight[2][0], light.skyLight[2][1], light.skyLight[2][2]);

	BufferDesc cbDesc;
	cbDesc.Name = "Sky camera";
	cbDesc.Size = 192;
	cbDesc.BindFlags = BIND_UNIFORM_BUFFER;
	cbDesc.Usage = USAGE_DYNAMIC;
	cbDesc.CPUAccessFlags = CPU_ACCESS_WRITE;
	device->CreateBuffer(cbDesc, nullptr, &_impl->skyBuffer);

	static const char* texNames[6] = { "skyFront", "skyRight", "skyBack", "skyLeft", "skyTop", "skyBottom" };
	std::vector<PipelineResourceDesc> resources = {
		{ SHADER_TYPE_PIXEL, "FogBuffer", 1, SHADER_RESOURCE_TYPE_CONSTANT_BUFFER, SHADER_RESOURCE_VARIABLE_TYPE_STATIC },
		{ SHADER_TYPE_PIXEL, "SkyBuffer", 1, SHADER_RESOURCE_TYPE_CONSTANT_BUFFER, SHADER_RESOURCE_VARIABLE_TYPE_STATIC },
		{ SHADER_TYPE_PIXEL, "CloudBuffer", 1, SHADER_RESOURCE_TYPE_CONSTANT_BUFFER, SHADER_RESOURCE_VARIABLE_TYPE_STATIC },
		{ SHADER_TYPE_PIXEL, "cloudNoise", 1, SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_STATIC },
		{ SHADER_TYPE_PIXEL, "cloudShape", 1, SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_STATIC },
		{ SHADER_TYPE_PIXEL, "cloudDetail", 1, SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_STATIC },
	};
	for (const char* name : texNames)
		resources.push_back({ SHADER_TYPE_PIXEL, name, 1, SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_STATIC });
	resources.push_back({ SHADER_TYPE_PIXEL, "tClouds", 1, SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE });
	SamplerDesc sampler;
	sampler.AddressU = sampler.AddressV = sampler.AddressW = TEXTURE_ADDRESS_CLAMP;
	SamplerDesc noiseSampler;
	noiseSampler.AddressU = noiseSampler.AddressV = noiseSampler.AddressW = TEXTURE_ADDRESS_WRAP;
	const ImmutableSamplerDesc samplers[] = { { SHADER_TYPE_PIXEL, "skySampler", sampler }, { SHADER_TYPE_PIXEL, "cloudNoiseSampler", noiseSampler },
		{ SHADER_TYPE_PIXEL, "skyOccSampler", sampler }, { SHADER_TYPE_PIXEL, "cloud3DSampler", noiseSampler } };
	PipelineResourceSignatureDesc sigDesc;
	sigDesc.Name = "Sky resources";
	sigDesc.Resources = resources.data();
	sigDesc.NumResources = (Uint32)resources.size();
	sigDesc.ImmutableSamplers = samplers;
	sigDesc.NumImmutableSamplers = (Uint32)std::size(samplers);
	device->CreatePipelineResourceSignature(sigDesc, &_impl->signature);
	if (!_impl->signature)
		ferr("Diligent: could not create the sky resource signature.");
	_impl->signature->GetStaticVariableByName(SHADER_TYPE_PIXEL, "FogBuffer")->Set(_impl->dgfx->fogBuffer);
	_impl->signature->GetStaticVariableByName(SHADER_TYPE_PIXEL, "SkyBuffer")->Set(_impl->skyBuffer);
	_impl->signature->GetStaticVariableByName(SHADER_TYPE_PIXEL, "CloudBuffer")->Set(_impl->dgfx->shadows.cloudBuffer);
	_impl->signature->GetStaticVariableByName(SHADER_TYPE_PIXEL, "cloudNoise")->Set(_impl->dgfx->shadows.cloudNoise->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
	_impl->signature->GetStaticVariableByName(SHADER_TYPE_PIXEL, "cloudShape")->Set(_impl->dgfx->shadows.cloudShape->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
	_impl->signature->GetStaticVariableByName(SHADER_TYPE_PIXEL, "cloudDetail")->Set(_impl->dgfx->shadows.cloudDetail->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
	for (int i = 0; i < 6; i++)
		_impl->signature->GetStaticVariableByName(SHADER_TYPE_PIXEL, texNames[i])->Set(_impl->dgfx->GetTextureView(_impl->faces[i]));

	const std::string source = std::string(g_cloudShaderCode) + skyShaderCode;
	auto createShader = [&](SHADER_TYPE type, const char* entryPoint) {
		ShaderCreateInfo sci;
		sci.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
		sci.Source = source.c_str();
		sci.SourceLength = source.size();
		sci.EntryPoint = entryPoint;
		sci.Desc.ShaderType = type;
		sci.Desc.Name = entryPoint;
		RefCntAutoPtr<IShader> shader;
		LoadingCompilingShader(entryPoint); // (the loading screen)
		device->CreateShader(sci, &shader);
		if (!shader)
			ferr("Diligent: failed to compile sky shader %s", entryPoint);
		return shader;
	};
	RefCntAutoPtr<IShader> vs = createShader(SHADER_TYPE_VERTEX, "VS");
	RefCntAutoPtr<IShader> ps = createShader(SHADER_TYPE_PIXEL, "PS");

	{
		const PipelineResourceDesc cloudResources[] = {
			{ SHADER_TYPE_PIXEL, "FogBuffer", 1, SHADER_RESOURCE_TYPE_CONSTANT_BUFFER, SHADER_RESOURCE_VARIABLE_TYPE_STATIC },
			{ SHADER_TYPE_PIXEL, "SkyBuffer", 1, SHADER_RESOURCE_TYPE_CONSTANT_BUFFER, SHADER_RESOURCE_VARIABLE_TYPE_STATIC },
			{ SHADER_TYPE_PIXEL, "CloudBuffer", 1, SHADER_RESOURCE_TYPE_CONSTANT_BUFFER, SHADER_RESOURCE_VARIABLE_TYPE_STATIC },
			{ SHADER_TYPE_PIXEL, "cloudNoise", 1, SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_STATIC },
			{ SHADER_TYPE_PIXEL, "cloudShape", 1, SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_STATIC },
			{ SHADER_TYPE_PIXEL, "cloudDetail", 1, SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_STATIC },
		};
		PipelineResourceSignatureDesc cs;
		cs.Name = "Sky clouds";
		cs.Resources = cloudResources;
		cs.NumResources = (Uint32)std::size(cloudResources);
		cs.ImmutableSamplers = samplers;
		cs.NumImmutableSamplers = (Uint32)std::size(samplers);
		device->CreatePipelineResourceSignature(cs, &_impl->cloudSignature);
		if (!_impl->cloudSignature)
			ferr("Diligent: could not create the sky clouds signature.");
		_impl->cloudSignature->GetStaticVariableByName(SHADER_TYPE_PIXEL, "FogBuffer")->Set(_impl->dgfx->fogBuffer);
		_impl->cloudSignature->GetStaticVariableByName(SHADER_TYPE_PIXEL, "SkyBuffer")->Set(_impl->skyBuffer);
		_impl->cloudSignature->GetStaticVariableByName(SHADER_TYPE_PIXEL, "CloudBuffer")->Set(_impl->dgfx->shadows.cloudBuffer);
		_impl->cloudSignature->GetStaticVariableByName(SHADER_TYPE_PIXEL, "cloudNoise")->Set(_impl->dgfx->shadows.cloudNoise->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
		_impl->cloudSignature->GetStaticVariableByName(SHADER_TYPE_PIXEL, "cloudShape")->Set(_impl->dgfx->shadows.cloudShape->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
		_impl->cloudSignature->GetStaticVariableByName(SHADER_TYPE_PIXEL, "cloudDetail")->Set(_impl->dgfx->shadows.cloudDetail->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
		RefCntAutoPtr<IShader> cps = createShader(SHADER_TYPE_PIXEL, "PS_Clouds");
		GraphicsPipelineStateCreateInfo cci;
		cci.PSODesc.Name = "Sky clouds";
		cci.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;
		auto& cgp = cci.GraphicsPipeline;
		cgp.NumRenderTargets = 1;
		cgp.RTVFormats[0] = TEX_FORMAT_RGBA16_FLOAT;
		cgp.DSVFormat = TEX_FORMAT_UNKNOWN;
		cgp.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		cgp.RasterizerDesc.CullMode = CULL_MODE_NONE;
		cgp.DepthStencilDesc.DepthEnable = False;
		cgp.DepthStencilDesc.DepthWriteEnable = False;
		cci.pVS = vs;
		cci.pPS = cps;
		IPipelineResourceSignature* csigs[] = { _impl->cloudSignature };
		cci.ppResourceSignatures = csigs;
		cci.ResourceSignaturesCount = 1;
		device->CreateGraphicsPipelineState(cci, &_impl->cloudPipeline);
		if (!_impl->cloudPipeline)
			ferr("Diligent: could not create the sky clouds pipeline.");
		_impl->cloudSignature->CreateShaderResourceBinding(&_impl->cloudSrb, true);
	}

	GraphicsPipelineStateCreateInfo ci;
	ci.PSODesc.Name = "Sky";
	ci.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;
	auto& gp = ci.GraphicsPipeline;
	gp.NumRenderTargets = 1;
	gp.RTVFormats[0] = _impl->dgfx->swapChain->GetDesc().ColorBufferFormat;
	gp.DSVFormat = _impl->dgfx->swapChain->GetDesc().DepthBufferFormat;
	gp.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	// Like the original: behind everything, no depth test or write, no blending
	gp.RasterizerDesc.CullMode = CULL_MODE_NONE;
	gp.DepthStencilDesc.DepthEnable = False;
	gp.DepthStencilDesc.DepthWriteEnable = False;
	gp.BlendDesc.RenderTargets[0].BlendEnable = False;
	ci.pVS = vs;
	ci.pPS = ps;
	IPipelineResourceSignature* signatures[] = { _impl->signature };
	ci.ppResourceSignatures = signatures;
	ci.ResourceSignaturesCount = 1;
	device->CreateGraphicsPipelineState(ci, &_impl->pipeline);
	if (!_impl->pipeline)
		ferr("Diligent: could not create the sky pipeline.");
	_impl->signature->CreateShaderResourceBinding(&_impl->srb, true);
	for (int i = 0; i < 5; i++) // the water reflects them
		_impl->dgfx->shadows.skyFaces[i] = _impl->loaded ? _impl->faces[i] : nullptr;
}

DiligentSkyRenderer::~DiligentSkyRenderer()
{
	std::fill(std::begin(_impl->dgfx->shadows.skyFaces), std::end(_impl->dgfx->shadows.skyFaces), nullptr);
	for (texture t : _impl->faces)
		if (t)
			_impl->dgfx->FreeTexture(t);
	delete _impl;
}

void DiligentSkyRenderer::render()
{
	if (!_impl->loaded || !g_renderSettings.sky)
		return;
	IDeviceContext* ctx = _impl->dgfx->ctx;
	const Camera& cam = *_impl->camera;

	// The camera's basis as in Camera::updateMatrix (left-handed look-at with +y up, 0.9 rad fov y)
	const Vector3 forward = cam.direction.normal();
	const Vector3 right = Vector3(0, 1, 0).cross(forward).normal();
	const Vector3 up = forward.cross(right);
	const float tanY = std::tan(cam.fovY * 0.5f);
	void* mapped = nullptr;
	ctx->MapBuffer(_impl->skyBuffer, MAP_WRITE, MAP_FLAG_DISCARD, mapped);
	float* f = (float*)mapped;
	// the clouds' layer, a quarter, half or the full scene's size (RenderSettings::cloudQuality)
	const int cloudQuality = std::clamp(g_renderSettings.cloudQuality, 0, 3);
	const Uint32 cloudDivisor = cloudQuality == 0 ? 4 : cloudQuality == 1 ? 2 : 1;
	const bool clouds = _impl->dgfx->shadows.cloudShadows && g_renderSettings.cloudShadows && g_renderSettings.dynamicClouds >= 3;
	// with the volumetric clouds the physical sky, and the sky light from it (above: the mean of
	// 30, 60 and 90 degrees up; the horizon: 4 degrees up; below: the horizon's, darker, as ground),
	// worked out again when the sun moves; else the sky box's
	auto& env = _impl->dgfx->shadows;
	env.physicalSky = clouds;
	if (clouds) {
		Vector3 sun = env.sunDirection;
		sun = sun.sqlen3() > 0.01f ? sun.normal() : Vector3(0.5f, 0.8f, 0.3f).normal();
		const float cover = std::clamp(std::max(env.cloudPreset.cover, env.cloudPreset.storm * 0.85f), 0.0f, 1.0f); // a storm: as dark as an overcast
		if ((sun - _impl->physicalSun).sqlen3() > 1e-6f || std::abs(cover - _impl->physicalCover) > 0.01f || _impl->physicalModel != g_renderSettings.skyModel) {
			_impl->physicalModel = g_renderSettings.skyModel;
			_impl->physicalSun = sun;
			_impl->physicalCover = cover;
			float up[3] = {}, horizon[3] = {}, below[3] = {}, c[3];
			// The light: the sky box's own (from its painted faces, as the original's lighting: a
			// clear day's ground as in the original), only greyed by the clouds below; the physical
			// sky's from the CPU model only without a sky box.
			const bool box = _impl->boxLightValid;
			if (box) {
				memcpy(up, _impl->boxLight[0], sizeof(up));
				memcpy(horizon, _impl->boxLight[1], sizeof(horizon));
				memcpy(below, _impl->boxLight[2], sizeof(below));
			}
			else for (int k = 0; k < 8; k++) {
				const float a = k * 0.785398f;
				for (float e : { 0.5236f, 1.0472f, 1.5708f }) {
					PhysicalSkyCpu(Vector3(std::cos(a) * std::cos(e), std::sin(e), std::sin(a) * std::cos(e)), sun, c);
					for (int i = 0; i < 3; i++)
						up[i] += c[i] / 24;
				}
				PhysicalSkyCpu(Vector3(std::cos(a) * 0.9976f, 0.0698f, std::sin(a) * 0.9976f), sun, c);
				for (int i = 0; i < 3; i++)
					horizon[i] += c[i] / 8;
			}
			// under the clouds the sky's light turns grey, the more the more cloud
			auto toGrey = [&](float* v) {
				const float l = 0.3f * v[0] + 0.59f * v[1] + 0.11f * v[2];
				for (int i = 0; i < 3; i++)
					v[i] += (l * 1.1f - v[i]) * 0.8f * cover * cover;
			};
			toGrey(up);
			toGrey(horizon);
			toGrey(below);
			// (the physical sky's: half as saturated in any weather: its full blue as the ground's
			// fill light made sunlit ground look cold and sunless next to the sky box's warm light)
			if (!box) for (float* v : { up, horizon }) {
				const float l = 0.3f * v[0] + 0.59f * v[1] + 0.11f * v[2];
				for (int i = 0; i < 3; i++)
					v[i] += (l - v[i]) * 0.5f;
			}
			// and above as bright as at the horizon (their colours kept): SkyAmbient divides by
			// their mean, and the physical sky's dark zenith under its bright horizon gave level
			// ground ~0.6 of the sky box's fill light (the box is about as bright above as around)
			if (!box) {
				const float lu = std::max(0.3f * up[0] + 0.59f * up[1] + 0.11f * up[2], 1e-3f);
				const float lh = std::max(0.3f * horizon[0] + 0.59f * horizon[1] + 0.11f * horizon[2], 1e-3f);
				const float mean = (lu + lh) * 0.5f;
				for (int i = 0; i < 3; i++) {
					up[i] *= mean / lu;
					horizon[i] *= mean / lh;
				}
			}
			for (int i = 0; i < 3; i++) {
				env.skyLight[0][i] = up[i];
				env.skyLight[1][i] = horizon[i];
				env.skyLight[2][i] = box ? below[i] : horizon[i] * 0.5f;
			}
			auto lum = [](const float* v) { return 0.299f * v[0] + 0.587f * v[1] + 0.114f * v[2]; };
			env.skyLightLuminance = std::max(0.05f, (lum(env.skyLight[0]) + 2 * lum(env.skyLight[1]) + lum(env.skyLight[2])) / 4);
			env.skyLightValid = true;
		}
	}
	// The haze's tint along the horizon (FogBuffer.FogSun, FogToward, FogAway): the physical sky
	// 3 degrees up towards the true sun's bearing and away from it, over its mean all round; the
	// clouds' cover greys it, the night (the sun far under) ends it
	if (clouds && g_renderSettings.skyModel >= 1) {
		const Vector3 sunTrue = g_environment.active ? g_environment.sunWorld : env.sunDirection.normal();
		const float cover = std::clamp(std::max(env.cloudPreset.cover, env.cloudPreset.storm * 0.85f), 0.0f, 1.0f);
		const float h = std::sqrt(sunTrue.x * sunTrue.x + sunTrue.z * sunTrue.z);
		const float live = std::clamp((sunTrue.y + 0.2f) / 0.1f, 0.0f, 1.0f) * (1.0f - 0.2f * cover * cover);
		if (h > 1e-3f && live > 0.0f && sunTrue.y < 0.21f) {
			if ((sunTrue - _impl->tintSun).sqlen3() > 1e-7f || std::abs(cover - _impl->tintCover) > 0.01f) {
				_impl->tintSun = sunTrue;
				_impl->tintCover = cover;
				// the sky 2 degrees up by bearing (the same model as the sky's), the clouds' grey
				// over it as the sky light's
				const float ce = 0.99939f, se = 0.0349f;
				for (int k = 0; k < 32; k++) {
					const float a = k * 6.2831853f / 32;
					float* v = _impl->horizon[k];
					PhysicalSkyCpu(Vector3(std::cos(a) * ce, se, std::sin(a) * ce), sunTrue, v);
					// (greyed less than the sky light: the low sun lights the haze under the cloud
					// deck, the horizon glows; at 0.8 of the cover's square it read as a grey band)
					const float l = 0.3f * v[0] + 0.59f * v[1] + 0.11f * v[2];
					for (int i = 0; i < 3; i++) {
						v[i] += (l * 1.1f - v[i]) * 0.35f * cover * cover;
						// a soft knee over 0.6 (at most about 0.85): the glow round the sun not white
						if (v[i] > 0.6f)
							v[i] = 0.6f + (v[i] - 0.6f) / (1.0f + (v[i] - 0.6f) / 0.25f);
					}
					v[3] = 0.0f;
				}
				// the sun's disc through the air towards it, in the sky's tone (its 30 times the
				// transmittance, mapped as PhysicalSky maps it)
				float trans[3];
				SunTransmittanceCpu(sunTrue, trans);
				for (int i = 0; i < 3; i++)
					_impl->sunColour[i] = 1.0f - std::exp(-30.0f * trans[i] * 1.2f);
			}
			// at dawn and dusk (the sun under 12 degrees, all of it under 6): by day the haze's own
			env.fogTint[0] = sunTrue.x;
			env.fogTint[1] = sunTrue.y;
			env.fogTint[2] = sunTrue.z;
			// (all of it with the sun within 2 degrees of the horizon, none from 7 up: higher, the
			// sky's bright glow round the sun spread over the whole fogged land and sea, a white
			// sheet, the play test of 2026-11-09)
			const float low = 1.0f - std::clamp((sunTrue.y - 0.03f) / 0.1f, 0.0f, 1.0f);
			env.fogTint[3] = live * low;
			// (the disc as bright as the clouds let it: an overcast hides it)
			const float clear = 1.0f - 0.9f * cover * cover;
			for (int i = 0; i < 3; i++)
				env.fogSunColour[i] = _impl->sunColour[i] * clear;
			env.fogSunColour[3] = 1.0f;
			memcpy(env.fogHorizon, _impl->horizon, sizeof(env.fogHorizon));
		}
		else
			env.fogTint[3] = 0.0f;
	}
	else
		env.fogTint[3] = 0.0f;
	// the haze (the distance fog's colour, the horizon's, the far clouds'): the physical sky's
	// horizon, greyer and darker the more cloud (a storm's dark, a fog's pale: the clouds' light)
	// The user's own fog colour (RenderSettings::fogColourOverride) is the clear day's haze then,
	// turned grey and darker by the clouds the same way.
	env.hazeOn = clouds && env.skyLightValid;
	if (env.hazeOn) {
		const float cover = std::clamp(std::max(env.cloudPreset.cover, env.cloudPreset.storm * 0.85f), 0.0f, 1.0f); // a storm: as dark as an overcast
		const float k = (1.0f - 0.5f * cover * cover) * (1.0f + (std::min(env.cloudPreset.light, 1.2f) - 1.0f) * cover);
		float base[3];
		if (g_renderSettings.fogColourOverride) {
			memcpy(base, g_renderSettings.fogColour, sizeof(base));
			const float l = 0.3f * base[0] + 0.59f * base[1] + 0.11f * base[2];
			for (int i = 0; i < 3; i++)
				base[i] += (l - base[i]) * 0.8f * cover * cover;
		}
		else
			memcpy(base, env.skyLight[1], sizeof(base)); // already grey by the cover
		// wkbai's day and night: as dark as the night's ambient (the map's fog is dimmed the same
		// way, EnvironmentView); the sky box's light, the haze's base, doesn't change with the clock
		const float* night = g_environment.active ? g_environment.sun.ambient : nullptr;
		for (int i = 0; i < 3; i++)
			env.hazeColour[i] = std::clamp(base[i] * k * (night ? night[i] : 1.0f), 0.0f, 1.0f);
	}
	if (!clouds && _impl->boxLightValid && _impl->physicalSun.y > -1.5f) {
		memcpy(env.skyLight, _impl->boxLight, sizeof(_impl->boxLight));
		env.skyLightLuminance = _impl->boxLuminance;
		_impl->physicalSun = Vector3(0, -2, 0);
	}
	// wkbai's night (the moon, the stars) and the weather's overcast over the sky box
	const EnvironmentView& ev = g_environment;
	auto smooth = [](float a, float b, float x) { const float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f); return t * t * (3 - 2 * t); };
	// the night's sky (its air, the stars, the moon): with the Physical sky model by the true sun, 2
	// to 9 degrees under the horizon (by the clock's daylight, 0.8 with the sun 1 degree under,
	// the stars were out at sunset); else by the daylight
	const bool physicalNight = clouds && g_renderSettings.skyModel >= 1;
	const float night = !ev.active ? 0.0f : physicalNight ? smooth(-0.035f, -0.155f, ev.sunWorld.y)
		: std::clamp(1.0f - ev.sun.daylight, 0.0f, 1.0f);
	const float moonShows = night * smooth(-0.03f, 0.06f, ev.moonWorld.y);
	{
		static bool told = false;
		if (!told && moonShows > 0.5f) {
			told = true;
			printf("Night sky: the moon towards %.3f %.3f %.3f\n", ev.moonWorld.x, ev.moonWorld.y, ev.moonWorld.z);
		}
	}
	// The live sky for reflections (ShadowEnvironment::fogSky, FogBuffer.FogSky; the shaders'
	// SkyReflect): the physical sky as drawn, in 16 bearings at 4, 22 and 50 degrees up and at
	// the zenith, greyed and darkened by the clouds as the haze is, with the night's air (and the
	// moon's light in it). The Fresnel reflections of models, water and the ground had taken the
	// sky box's painted faces (a fixed blue, also at dusk and at night). 49 directions of the CPU
	// model, only when the sun, the moon, the clouds or the night changed (sky-fresnel,
	// 2026-11-10): no cube map, no ray tracing.
	env.fogSky[48][3] = 0.0f;
	if (clouds) {
		Vector3 sunTrue = ev.active ? ev.sunWorld : env.sunDirection;
		sunTrue = sunTrue.sqlen3() > 0.01f ? sunTrue.normal() : Vector3(0.5f, 0.8f, 0.3f).normal();
		const auto& p = env.cloudPreset;
		const float cover = std::clamp(std::max(p.cover, p.storm * 0.85f), 0.0f, 1.0f);
		const float moonLight = moonShows * (ev.active ? ev.sun.moonLit : 1.0f);
		Impl& m = *_impl;
		if ((sunTrue - m.reflSun).sqlen3() > 1e-6f || std::abs(cover - m.reflCover) > 0.01f || std::abs(night - m.reflNight) > 0.01f
			|| std::abs(moonLight - m.reflMoonShows) > 0.02f || (moonLight > 0 && (ev.moonWorld - m.reflMoon).sqlen3() > 1e-4f)
			|| m.reflModel != g_renderSettings.skyModel) {
			const auto t0 = std::chrono::steady_clock::now();
			m.reflSun = sunTrue;
			m.reflCover = cover;
			m.reflNight = night;
			m.reflMoonShows = moonLight;
			m.reflMoon = ev.moonWorld;
			m.reflModel = g_renderSettings.skyModel;
			const float k = (1.0f - 0.5f * cover * cover) * (1.0f + (std::min(p.light, 1.2f) - 1.0f) * cover);
			// the clouds' own light: the sun's through the air (reddened at dawn and dusk; the
			// clouds high up see it a little longer than the ground), brightest towards the sun,
			// over a grey of the sky's light inside them. A deck lit orange from below at sunset
			// is what the water mirrors then, not a grey.
			float cloudSun[3];
			{
				Vector3 up = sunTrue;
				up.y += 0.015f;
				float trans[3];
				SunTransmittanceCpu(up.normal(), trans);
				const float lit = std::clamp((sunTrue.y + 0.09f) / 0.08f, 0.0f, 1.0f);
				for (int i = 0; i < 3; i++)
					// (mapped softer than the sun's disc: the deck salmon and red at sunset, not yellow)
					cloudSun[i] = (1.0f - std::exp(-8.0f * trans[i])) * lit * (1.0f - 0.45f * cover * cover);
			}
			auto one = [&](const Vector3& d, float grey, float* v) {
				PhysicalSkyCpu(d, sunTrue, v);
				// (the sky's glow round the sun with the same soft knee as the haze's horizon)
				for (int i = 0; i < 3; i++)
					if (v[i] > 0.6f)
						v[i] = 0.6f + (v[i] - 0.6f) / (1.0f + (v[i] - 0.6f) / 0.25f);
				// the night's air (the sky shader's NightAir): blue, darker up, the moon's light
				const float h = std::max(d.y, 0.0f), sh = std::sqrt(h), mu = std::max(d.dot(ev.moonWorld), 0.0f);
				const float lo[3] = { 0.070f, 0.095f, 0.17f }, hi[3] = { 0.028f, 0.045f, 0.105f }, moon[3] = { 0.06f, 0.08f, 0.12f };
				for (int i = 0; i < 3; i++) {
					const float air = lo[i] + (hi[i] - lo[i]) * sh + moon[i] * moonLight * (0.35f + 0.65f * mu * mu) * (0.6f + 0.4f * (1 - h));
					v[i] = std::max(v[i], air * night);
				}
				// the clouds over it (more of them seen higher up): their sunlit colour (forward
				// scattering towards the sun) over the sky's grey inside them, darker the thicker
				const float l = 0.3f * v[0] + 0.59f * v[1] + 0.11f * v[2];
				// (the haze's darkening only half: the cloud shader's deck stays lit, a red and pink
				// one away from the low sun too, multiply scattered)
				const float sd = std::max(d.dot(sunTrue), 0.0f), fwd = 0.6f + 0.4f * sd * sd + 0.3f * std::pow(sd, 8.0f);
				for (int i = 0; i < 3; i++) {
					const float cloud = ((l + v[i]) * 0.45f + cloudSun[i] * fwd * 0.8f) * (0.5f + 0.5f * k);
					v[i] = std::clamp(v[i] + (cloud - v[i]) * grey * cover, 0.0f, 1.0f);
				}
				v[3] = 0.0f;
			};
			const float ce[3] = { 0.99756f, 0.92718f, 0.64279f }, se[3] = { 0.06976f, 0.37461f, 0.76604f }, grey[3] = { 0.5f, 0.7f, 0.85f };
			for (int r = 0; r < 3; r++)
				for (int b = 0; b < 16; b++) {
					const float a = b * 6.2831853f / 16;
					one(Vector3(std::cos(a) * ce[r], se[r], std::sin(a) * ce[r]), grey[r], m.reflSky[r * 16 + b]);
				}
			one(Vector3(0, 1, 0), 0.8f, m.reflSky[48]);
			static int told = 0;
			if (told < 3) {
				told++;
				const float us = std::chrono::duration<float, std::micro>(std::chrono::steady_clock::now() - t0).count();
				printf("Sky reflections: 49 directions in %.0f us; zenith %.2f %.2f %.2f, horizon %.2f %.2f %.2f (sun %.3f up, cover %.2f, night %.2f)\n",
					us, m.reflSky[48][0], m.reflSky[48][1], m.reflSky[48][2], m.reflSky[0][0], m.reflSky[0][1], m.reflSky[0][2], sunTrue.y, cover, night);
			}
		}
		memcpy(env.fogSky, m.reflSky, sizeof(m.reflSky));
		// the ground below the horizon: the sky light's (the box's below or the horizon's half),
		// as dark as the night's ambient
		for (int i = 0; i < 3; i++)
			env.fogSky[49][i] = env.skyLightValid ? env.skyLight[2][i] * (ev.active ? ev.sun.ambient[i] : 1.0f) : m.reflSky[0][i] * 0.5f;
		env.fogSky[48][3] = 1.0f;
	}
	// the overcast: with the weather's clouds (the seasons' or the cloudWeather setting's), when
	// there are no volumetric ones in the sky (dynamicClouds below 3; the shader takes how much
	// from the cover over each point, so a front shows): a grey of the sky box's own light, darker
	// the heavier the cloud (as the haze: the clouds' light), and at night
	float overcast[4] = {};
	if (!clouds && _impl->boxLightValid && (env.cloudWeatherKind >= 0 || (env.cloudGameWeather >= 0 ? env.cloudGameWeather : g_renderSettings.cloudWeather) > 0)) {
		const auto& p = env.cloudPreset;
		const float cover = std::clamp(std::max(p.cover, p.storm * 0.85f), 0.0f, 1.0f);
		const float k = (1.0f - 0.45f * cover * cover) * (1.0f + (std::min(p.light, 1.2f) - 1.0f) * cover);
		const float* bl = _impl->boxLight[0];
		const float lum = 0.299f * bl[0] + 0.587f * bl[1] + 0.114f * bl[2];
		const float tint[3] = { 0.97f, 1.0f, 1.04f };
		for (int i = 0; i < 3; i++)
			overcast[i] = std::clamp(lum * 1.05f * k * tint[i] * (ev.active ? ev.sun.ambient[i] : 1.0f), 0.0f, 1.0f);
		overcast[3] = 1.0f;
	}
	const float screenHeight = (float)_impl->dgfx->CurrentColorTarget()->GetTexture()->GetDesc().Height;
	const bool physicalModel = clouds && g_renderSettings.skyModel >= 1;
	const float data[48] = {
		forward.x, forward.y, forward.z, tanY * cam.aspect,
		right.x, right.y, right.z, tanY,
		up.x, up.y, up.z, screenHeight,
		// sky_front.tga faces +z (north) and sky_right.tga +x, as seen in the original
		0, 0, 1, 0,
		1, 0, 0, physicalModel ? 1.0f : 0.0f,
		_impl->sixFaces ? 1.0f : 0.0f, clouds ? 1.0f : 0.0f, (float)(_impl->dgfx->FrameIndex() % 64), (float)cloudQuality,
		ev.sunWorld.x, ev.sunWorld.y, ev.sunWorld.z, night,
		ev.moonWorld.x, ev.moonWorld.y, ev.moonWorld.z, moonShows,
		ev.starX.x, ev.starX.y, ev.starX.z, ev.active ? ev.sun.moonLit : 1.0f,
		ev.starY.x, ev.starY.y, ev.starY.z, (float)std::clamp(g_renderSettings.cloudDetail, 0, 2),
		ev.starZ.x, ev.starZ.y, ev.starZ.z, 0,
		overcast[0], overcast[1], overcast[2], overcast[3],
	};
	static_assert(sizeof(data) == 192, "the sky buffer's size");
	memcpy(f, data, sizeof(data));
	ctx->UnmapBuffer(_impl->skyBuffer, MAP_WRITE);

	if (clouds) {
		const TextureDesc& scene = _impl->dgfx->CurrentColorTarget()->GetTexture()->GetDesc();
		const Uint32 w = std::max(1u, (scene.Width + cloudDivisor - 1) / cloudDivisor), h = std::max(1u, (scene.Height + cloudDivisor - 1) / cloudDivisor);
		if (!_impl->cloudTarget || _impl->cloudTarget->GetDesc().Width != w || _impl->cloudTarget->GetDesc().Height != h) {
			TextureDesc td;
			td.Name = "Sky clouds";
			td.Type = RESOURCE_DIM_TEX_2D;
			td.Width = w;
			td.Height = h;
			td.Format = TEX_FORMAT_RGBA16_FLOAT;
			td.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;
			_impl->cloudTarget.Release();
			_impl->dgfx->device->CreateTexture(td, nullptr, &_impl->cloudTarget);
			// ALLOW_OVERWRITE: a mutable variable that already holds a texture ignores a new one
			// without it (Diligent), and the sky kept compositing the old target, frozen on its
			// last frame: the clouds stuck to the camera after the scene's size changed (the
			// upscaling, the cloud quality, the window) until the level was loaded again
			_impl->srb->GetVariableByName(SHADER_TYPE_PIXEL, "tClouds")->Set(_impl->cloudTarget->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE),
				SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
		}
		ITextureView* rtv = _impl->cloudTarget->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
		ctx->SetRenderTargets(1, &rtv, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
		ctx->SetViewports(1, nullptr, 0, 0);
		ctx->SetPipelineState(_impl->cloudPipeline);
		ctx->CommitShaderResources(_impl->cloudSrb, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
		ctx->Draw(DrawAttribs{ 3, DRAW_FLAG_VERIFY_ALL });
		_impl->dgfx->RestoreRenderTargets();
	}
	else if (!_impl->cloudTarget) {
		// a 1 x 1 target bound while the clouds are off
		TextureDesc td;
		td.Name = "Sky clouds";
		td.Type = RESOURCE_DIM_TEX_2D;
		td.Width = td.Height = 1;
		td.Format = TEX_FORMAT_RGBA16_FLOAT;
		td.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;
		_impl->dgfx->device->CreateTexture(td, nullptr, &_impl->cloudTarget);
		_impl->srb->GetVariableByName(SHADER_TYPE_PIXEL, "tClouds")->Set(_impl->cloudTarget->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE),
			SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
	}
	ctx->SetPipelineState(_impl->pipeline);
	ctx->CommitShaderResources(_impl->srb, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	ctx->Draw(DrawAttribs{ 3, DRAW_FLAG_VERIFY_ALL });
}
