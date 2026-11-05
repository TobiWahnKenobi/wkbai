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
#include "renderer_diligent.h"
#include "DiligentClouds.h"
#include "bitmap.h"
#include "RenderSettings.h"
#include "../terrain.h"
#include "../Camera.h"
#include "../file.h"
#include "../util/util.h"
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
};

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
	float4 SkyRight;   // world direction of sky_right.tga
	float4 SkyFlags;   // x: the sky has all six faces, y: the dynamic clouds' layer (tClouds), z: the frame (its jitter),
	                   // w: the clouds' quality (RenderSettings::cloudQuality, 0-3)
};

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
	if (t <= 4.0 / 81) return lerp(1, 1 - 40.0 / 255, (t - 1.0 / 81) / (3.0 / 81));
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

// The cloud layer's density at a point (Schneider, "The Real-time Volumetric Cloudscapes of
// Horizon Zero Dawn"): the 3D Perlin-Worley shape within a rounded height profile, cut by the
// cover over the map (the same as the ground shadows'), its edges eroded by 3D Worley detail,
// wispy towards the base and billowy towards the top. The cloud tops vary by region through
// their own noise (not the cover: tops following the cover made a cone of every peak of it), and
// the shape is 3D (a 2D shape stretched up the layer gave vertical streaks). footprint: the
// pixel's size there in world units, for the noise's mip levels; detail: with the erosion (the
// light's steps go without).
float CloudDensity(float3 p, float lod, float footprint, bool detail)
{
	float h = saturate((p.y - CloudShape.z) / CloudShape.w);
	// the coverage (CloudCover, as the ground shadows')
	const float stratus = CloudWeather.x, storm = CloudWeather.y;
	float cover = CloudCover(p.xz, lod + 1);
	float stormCore = 0;
	if (storm > 0) {
		// cumulonimbus: towers on the storm cells' cores up to the layer's top, their anvils
		// spreading out wide under it; the ordinary cover only in the lowest third and around the
		// cells (the ragged clouds under them)
		float2 cb = StormCells(p.xz, h, max(lod - 1, 0)); // their noise is broader: a finer mip
		float anvil = cb.y * smoothstep(0.8, 0.9, h);
		float low = cover * (1 - smoothstep(0.2, 0.36, h)) * saturate(StormCell(p.xz, max(lod - 1, 0)) * 8 - 4.4); // near the cells only
		cover = lerp(cover, max(max(cb.x, anvil), low), storm);
		stormCore = cb.x * storm;
	}
	if (cover <= 0.002)
		return 0;
	// the tops: by region; a stratus sheet's level; a storm's flat, at the layer's top (the anvils)
	float topVar = cloudNoise.SampleLevel(cloudNoiseSampler, p.xz * (CloudScale.x * 0.7) + float2(0.37, 0.71), lod + 1).g;
	float top = lerp(0.5, 1.0, saturate(topVar * 1.6 - 0.3));
	top = lerp(lerp(top, 0.85, stratus), 1.0, storm);
	float profile = saturate(Remap(h, 0, lerp(0.07, 0.2, stratus), 0, 1)) * saturate(Remap(h, lerp(top * lerp(0.45, 0.7, stratus), 0.93, storm), top, 1, 0));
	if (profile <= 0)
		return 0;
	float3 drift = float3(6, 0, 2) * CloudScale.w; // as the cover's base layer (6 and 2 units a second)
	float3 q = p - drift - float3(0, CloudShape.z, 0);
	float4 s = cloudShape.SampleLevel(cloud3DSampler, q / SHAPE_PERIOD, max(log2(footprint * 64 / SHAPE_PERIOD), 0));
	float fbm = s.g * 0.625 + s.b * 0.25 + s.a * 0.125;
	float shape = saturate(Remap(s.r, fbm - 1, 1, 0, 1));
	shape = lerp(shape, 0.45 + 0.55 * shape, stratus); // sheets: the billows flattened
	shape = lerp(shape, 0.3 + 0.7 * shape, stormCore * stormCore) * profile; // the towers' middles solid
	float c = saturate(Remap(shape, 1 - cover, 1, 0, 1)) * cover;
	if (!detail || c <= 0)
		return c * 1.6 * CloudWeather.z;
	float detailLod = log2(footprint * 32 / DETAIL_PERIOD);
	float3 d = cloudDetail.SampleLevel(cloud3DSampler, (q - drift * 0.3) / DETAIL_PERIOD, max(detailLod, 0)).rgb;
	float hf = d.r * 0.625 + d.g * 0.25 + d.b * 0.125;
	hf = lerp(1 - hf, hf, saturate(h * 3));
	float erosion = 0.4 * saturate(2 - detailLod * 0.5) * (1 - 0.6 * stratus);
	return saturate(Remap(c, hf * erosion, 1, 0, 1)) * 1.6 * CloudWeather.z;
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
float4 MarchClouds(float3 ro, float3 rd, float2 pixel)
{
	float base = CloudShape.z, top = CloudShape.z + CloudShape.w;
	if (ro.y > base || rd.y <= 0.002)
		return float4(0, 0, 0, 1); // above the base (zoomed out): the shadows only
	// at most 1800 units of the ray in the layer; a storm's 6000, so that the cumulonimbus'
	// towers show to their anvils towards the horizon
	float t0 = (base - ro.y) / rd.y, t1 = min((top - ro.y) / rd.y, t0 + lerp(1800, 6000, CloudWeather.y));
	if (t1 <= t0 || t0 > 9000)
		return float4(0, 0, 0, 1);
	// Low: a quarter of the size, Medium: half, High and Ultra: the full size
	const int q = (int)SkyFlags.w;
	const int STEPS = q <= 0 ? 48 : q == 1 ? 64 : q == 2 ? 96 : 128;
	const int LIGHT_STEPS = q + 3;
	const float lightStep = 108.0 / LIGHT_STEPS;
	const float pixelScale = q <= 0 ? 1 : q == 1 ? 0.5 : 0.25; // of a quarter-size pixel
	float dt = (t1 - t0) / STEPS;
	// interleaved gradient noise, moved every frame: the temporal anti-aliasing (TAA, FSR 3.1)
	// averages the steps' banding away
	pixel += 5.588238 * fmod(SkyFlags.z, 64);
	float jitter = frac(52.9829189 * frac(dot(pixel, float2(0.06711056, 0.00583715))));
	float3 sun = normalize(CloudSun.xyz);
	float mu = dot(rd, sun);
	float phase = lerp(HenyeyGreenstein(mu, -0.2), HenyeyGreenstein(mu, 0.8), 0.55) * 12.566371;
	float towardSun = saturate(mu * 0.5 + 0.5);
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
		float dens = CloudDensity(p, lod, footprint, true);
		if (dens <= 0.002)
			continue;
		float od = 0;
		[loop] for (int j = 0; j < LIGHT_STEPS; j++)
			od += CloudDensity(p + sun * ((j + 0.5) * lightStep), lod + 0.5, footprint + (j + 1) * lightStep * 0.15, false) * lightStep;
		float toSun = exp(-SIGMA * od);
		float powder = 1 - exp(-2 * SIGMA * od - 0.15);
		float energy = toSun * lerp(powder * 1.6, 1, towardSun);
		float h = saturate((p.y - base) / CloudShape.w);
		float3 ambient = CloudSkyUp.rgb * (0.45 + 0.55 * h) + CloudSkyHorizon.rgb * 0.15;
		// light scattered many times inside the cloud (the light that a single scattering misses
		// in thick clouds: an overcast sheet is grey, not black), less absorbed and isotropic
		energy += 0.35 * exp(-SIGMA * 0.2 * od);
		ambient = lerp(ambient, dot(ambient, float3(0.3, 0.59, 0.11)).xxx, 0.4) * 1.3;
		// the weather's light (storm and rain clouds darker, their bases most), and lightning
		// lighting the cloud from inside
		// (and the denser parts of a cloud darker than its thin ones: an overcast sheet's relief)
		float shade = CloudWeather.w * lerp(1, 0.6 + 0.4 * h, CloudWeather.y)
			* lerp(1.12, 0.72, saturate(dens / (1.6 * max(CloudWeather.z, 0.1))));
		float3 flash = float3(0.75, 0.8, 1.0) * (CloudFlash.w * 9 * exp(-length(p - CloudFlash.xyz) / 260));
		float3 s = (CloudSunColour.rgb * (energy * phase * 1.3) + ambient * 0.85) * shade + flash;
		float dT = exp(-SIGMA * dens * dt);
		light += T * (1 - dT) * s;
		T *= dT;
	}
	// far clouds fade into the horizon's haze
	float fade = exp(-t0 / 5000);
	return float4(light * fade, lerp(1, T, fade));
}

// The high clouds above the cumulus layer, as thin textured layers (cheap: a few samples a pixel
// in the sky's own pass, at the full size): cirrus at 1600 units, fibres stretched along the wind
// and curled by a slow warp; altocumulus at 800, small rounded tufts (floccus) or a sheet of
// cells (stratiformis), lit brighter at their thin edges and towards the sun.
// colour: the sky behind; ro, rd: the camera and the view (up).
float3 HighClouds(float3 ro, float3 rd, float3 colour)
{
	if (rd.y <= 0.01 || CloudHigh.x + CloudHigh.y <= 0.001)
		return colour;
	float3 sun = normalize(CloudSun.xyz);
	float forward = HenyeyGreenstein(dot(rd, sun), 0.6) * 12.566371; // 1: as much as to the side
	float3 ambient = lerp(CloudSkyUp.rgb, CloudSkyHorizon.rgb, 0.3);
	float3 sunLight = CloudSunColour.rgb * CloudWeather.w;
	const float2 wind = float2(0.948683, 0.316228); // as the cumulus' drift (6, 2)
	const float2 across = float2(-wind.y, wind.x);
	float grazing = 1 / max(rd.y, 0.08); // the pixel's footprint stretched towards the horizon
	if (CloudHigh.x > 0) {
		float t = (1600 - ro.y) / rd.y;
		float2 p = ro.xz + rd.xz * t - wind * CloudScale.w * 9;
		float fp = t * 0.0012 * grazing; // the pixel's size there
		float mask = cloudNoise.SampleLevel(cloudNoiseSampler, p / 7000 + float2(0.31, 0.77), log2(max(fp * 256 / 7000, 1))).r;
		float warp = cloudNoise.SampleLevel(cloudNoiseSampler, p / 9000 + float2(0.6, 0.1), log2(max(fp * 256 / 9000, 1))).g - 0.5;
		float2 f = float2(dot(p, wind), dot(p, across) + warp * 400);
		// the fibres: ridges of noise stretched along the wind (thin bright lines where it crosses
		// its middle), a finer set over them
		float n1 = cloudNoise.SampleLevel(cloudNoiseSampler, f / float2(9000, 500), log2(max(fp * 256 / 500, 1))).g;
		float n2 = cloudNoise.SampleLevel(cloudNoiseSampler, f / float2(3500, 160) + 0.5, log2(max(fp * 256 / 160, 1))).g;
		float fib = pow(saturate(1 - abs(n1 - 0.5) * 5), 2) * 0.7 + pow(saturate(1 - abs(n2 - 0.5) * 4), 3) * 0.45;
		float d = saturate((mask - (0.72 - CloudHigh.x * 0.4)) * 6) * saturate(fib);
		float a = d * 0.6 * saturate(rd.y * 8) * exp(-t / 14000);
		colour = lerp(colour, sunLight * (0.7 + 0.45 * forward) + ambient * 0.55, a);
	}
	if (CloudHigh.y > 0) {
		float t = (800 - ro.y) / rd.y;
		float2 p = ro.xz + rd.xz * t - wind * CloudScale.w * 7;
		float fp = t * 0.0012 * grazing;
		float sheet = CloudHigh.z;
		// where: patches (floccus) or a broad sheet (stratiformis)
		float cov = cloudNoise.SampleLevel(cloudNoiseSampler, p / 4200 + float2(0.13, 0.42), log2(max(fp * 256 / 4200, 1))).r;
		float mask = saturate((cov - (0.72 - CloudHigh.y * 0.4)) * lerp(8, 5, sheet));
		// the elements: Worley cells of the shape noise (about 80 units across), ragged by smaller ones
		float4 s = cloudShape.SampleLevel(cloud3DSampler, float3(p / 640, 0.37), max(log2(fp * 64 / 640), 0));
		float cell = s.b * 0.7 + s.a * 0.3;
		float d = saturate((cell - lerp(0.5, 0.4, sheet) - 0.12 * (1 - mask)) * 4) * mask; // smaller towards the edges
		float a = saturate(d * 1.4) * 0.9 * saturate(rd.y * 8) * exp(-t / 9000);
		float3 c = sunLight * (0.5 + 0.5 * (1 - d) * forward) + ambient * lerp(0.75, 0.45, d);
		colour = lerp(colour, c, a);
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

float AtmTop(float3 o, float3 d)
{
	float b = dot(o, d), c = dot(o, o) - ATM_TOP * ATM_TOP;
	return -b + sqrt(max(b * b - c, 0));
}

float3 PhysicalSky(float3 rd, float3 sun)
{
	rd = normalize(float3(rd.x, max(rd.y, 0.0), rd.z));
	float3 o = float3(0, ATM_EARTH + 0.3, 0);
	float ds = AtmTop(o, rd) / 8;
	float odR = 0, odM = 0;
	float3 sumR = 0, sumM = 0;
	[loop] for (int i = 0; i < 8; i++) {
		float3 p = o + rd * ((i + 0.5) * ds);
		float h = length(p) - ATM_EARTH;
		float dR = exp(-h / 8) * ds, dM = exp(-h / 1.2) * ds;
		odR += dR;
		odM += dM;
		float dl = AtmTop(p, sun) / 4, lR = 0, lM = 0;
		[unroll] for (int j = 0; j < 4; j++) {
			float hl = length(p + sun * ((j + 0.5) * dl)) - ATM_EARTH;
			lR += exp(-hl / 8) * dl;
			lM += exp(-hl / 1.2) * dl;
		}
		float3 att = exp(-(ATM_RAYLEIGH * (odR + lR) + ATM_MIE * 1.1 * (odM + lM)));
		sumR += dR * att;
		sumM += dM * att;
	}
	float mu = dot(rd, sun), g = 0.76;
	float pR = 0.0596831 * (1 + mu * mu);
	float pM = 0.1193662 * ((1 - g * g) * (1 + mu * mu)) / ((2 + g * g) * pow(1 + g * g - 2 * g * mu, 1.5));
	float3 c = 20 * (sumR * ATM_RAYLEIGH * pR + sumM * ATM_MIE * pM);
	float3 trans = exp(-(ATM_RAYLEIGH * odR + ATM_MIE * 1.1 * odM));
	c += trans * 30 * smoothstep(0.99985, 0.99996, mu);
	return 1 - exp(-c * 1.2);
}

float4 PS(VS_OUTPUT input) : SV_Target
{
	float3 d = CamForward.xyz + input.Ndc.x * CamForward.w * CamRight.xyz + input.Ndc.y * CamRight.w * CamUp.xyz;
	float a = dot(d, SkyFront.xyz), b = dot(d, SkyRight.xyz), c = d.y;
	float horizontal = sqrt(a * a + b * b);
	// The original's five-face sky always fades into the fog colour at the horizon and below,
	// where it has no texture. Only a full six-face sky may drop the fade, when the fog is off
	// (fog start beyond any distance).
	bool sixFaces = SkyFlags.x != 0;
	float fog = (sixFaces && FogStartDist > 1e8) ? 0 : (c < 0 ? 1 : SkyFog(c / max(horizontal, 1e-6)));
	float3 colour = FogColor.rgb;
	if (fog < 1) {
		float3 tex;
		if (SkyFlags.y != 0) { // the volumetric clouds: the physical sky
			float3 sun = dot(CloudSun.xyz, CloudSun.xyz) > 0.01 ? normalize(CloudSun.xyz) : float3(0.5, 0.8, 0.3);
			tex = PhysicalSky(normalize(d), sun);
			tex = HighClouds(FogCamera.xyz, normalize(d), tex);
		}
		else if (c >= max(abs(a), abs(b)))
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
		colour = lerp(tex, FogColor.rgb, fog);
	}
	// the dynamic clouds' layer (RenderSettings::dynamicClouds 3) in front of the sky box,
	// marched at a quarter, half or the full size (PS_Clouds)
	if (SkyFlags.y != 0 && c > 0) {
		float2 uv = float2(input.Ndc.x * 0.5 + 0.5, 0.5 - input.Ndc.y * 0.5);
		float cw, ch;
		tClouds.GetDimensions(cw, ch);
		float2 o = (SkyFlags.w >= 2 ? 0.5 : 0.75) / float2(cw, ch); // a small blur over the march's jitter
		float4 cl = (tClouds.SampleLevel(skySampler, uv + float2(o.x, o.y), 0) + tClouds.SampleLevel(skySampler, uv + float2(-o.x, o.y), 0)
			+ tClouds.SampleLevel(skySampler, uv + float2(o.x, -o.y), 0) + tClouds.SampleLevel(skySampler, uv - o, 0)) * 0.25;
		colour = colour * cl.a + cl.rgb;
	}
	return float4(GradeColour(colour), 1);
}

float4 PS_Clouds(VS_OUTPUT input) : SV_Target
{
	float3 d = CamForward.xyz + input.Ndc.x * CamForward.w * CamRight.xyz + input.Ndc.y * CamRight.w * CamUp.xyz;
	return MarchClouds(FogCamera.xyz, normalize(d), input.Pos.xy);
}
)---";

// The physical sky's colour towards d (unit, y up) with the sun towards sun: PhysicalSky of the
// sky shader on the CPU
static void PhysicalSkyCpu(const Vector3& dIn, const Vector3& sun, float out[3])
{
	const float EARTH = 6360, TOP = 6420, RAY[3] = { 5.8e-3f, 13.5e-3f, 33.1e-3f }, MIE = 21e-3f;
	const Vector3 d = Vector3(dIn.x, std::max(dIn.y, 0.0f), dIn.z).normal();
	auto top = [&](const Vector3& o, const Vector3& v) {
		const float b = o.dot(v), c = o.dot(o) - TOP * TOP;
		return -b + std::sqrt(std::max(b * b - c, 0.0f));
	};
	const Vector3 o(0, EARTH + 0.3f, 0);
	const float ds = top(o, d) / 8;
	float odR = 0, odM = 0, sumR[3] = {}, sumM[3] = {};
	for (int i = 0; i < 8; i++) {
		const Vector3 p = o + d * ((i + 0.5f) * ds);
		const float h = p.len3() - EARTH;
		const float dR = std::exp(-h / 8) * ds, dM = std::exp(-h / 1.2f) * ds;
		odR += dR;
		odM += dM;
		const float dl = top(p, sun) / 4;
		float lR = 0, lM = 0;
		for (int j = 0; j < 4; j++) {
			const float hl = (p + sun * ((j + 0.5f) * dl)).len3() - EARTH;
			lR += std::exp(-hl / 8) * dl;
			lM += std::exp(-hl / 1.2f) * dl;
		}
		for (int c = 0; c < 3; c++) {
			const float att = std::exp(-(RAY[c] * (odR + lR) + MIE * 1.1f * (odM + lM)));
			sumR[c] += dR * att;
			sumM[c] += dM * att;
		}
	}
	const float mu = d.dot(sun), g = 0.76f;
	const float pR = 0.0596831f * (1 + mu * mu);
	const float pM = 0.1193662f * ((1 - g * g) * (1 + mu * mu)) / ((2 + g * g) * std::pow(1 + g * g - 2 * g * mu, 1.5f));
	for (int c = 0; c < 3; c++)
		out[c] = 1 - std::exp(-20 * (sumR[c] * RAY[c] * pR + sumM[c] * MIE * pM) * 1.2f);
}

struct DiligentSkyRenderer::Impl {
	DiligentGfx* dgfx;
	// the sky box's own light (ShadowEnvironment::skyLight), for when the physical sky is off
	float boxLight[3][3] = {};
	float boxLuminance = 1;
	bool boxLightValid = false;
	Vector3 physicalSun = Vector3(0, -2, 0); // the sun the physical sky light was worked out for
	float physicalCover = -1; // and the cloud cover
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
		if (bmp.format != BMFORMAT_BC7)
			meanColour(bmp, i == 4 ? 0 : bmp.height * 3 / 4, sums[i == 4 ? 0 : 1]);
		_impl->faces[i] = gfx->CreateTexture(bmp, 1);
	}
	if (!_impl->loaded)
		return;
	const std::string bottomPath = dir + faceNames[5];
	bool hasBottom = false;
	if (FileExists(bottomPath.c_str())) {
		const Bitmap bmp = Bitmap::loadBitmap(bottomPath.c_str());
		if (bmp.format != BMFORMAT_BC7) {
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
	cbDesc.Size = 96;
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
		const float cover = std::clamp(env.cloudPreset.cover, 0.0f, 1.0f);
		if ((sun - _impl->physicalSun).sqlen3() > 1e-6f || std::abs(cover - _impl->physicalCover) > 0.01f) {
			_impl->physicalSun = sun;
			_impl->physicalCover = cover;
			float up[3] = {}, horizon[3] = {}, c[3];
			for (int k = 0; k < 8; k++) {
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
			for (int i = 0; i < 3; i++) {
				env.skyLight[0][i] = up[i];
				env.skyLight[1][i] = horizon[i];
				env.skyLight[2][i] = horizon[i] * 0.5f;
			}
			auto lum = [](const float* v) { return 0.299f * v[0] + 0.587f * v[1] + 0.114f * v[2]; };
			env.skyLightLuminance = std::max(0.05f, (lum(env.skyLight[0]) + 2 * lum(env.skyLight[1]) + lum(env.skyLight[2])) / 4);
			env.skyLightValid = true;
		}
	}
	else if (_impl->boxLightValid && _impl->physicalSun.y > -1.5f) {
		memcpy(env.skyLight, _impl->boxLight, sizeof(_impl->boxLight));
		env.skyLightLuminance = _impl->boxLuminance;
		_impl->physicalSun = Vector3(0, -2, 0);
	}
	const float data[24] = {
		forward.x, forward.y, forward.z, tanY * cam.aspect,
		right.x, right.y, right.z, tanY,
		up.x, up.y, up.z, 0,
		// sky_front.tga faces +z (north) and sky_right.tga +x, as seen in the original
		0, 0, 1, 0,
		1, 0, 0, 0,
		_impl->sixFaces ? 1.0f : 0.0f, clouds ? 1.0f : 0.0f, (float)(_impl->dgfx->FrameIndex() % 64), (float)cloudQuality,
	};
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
			_impl->srb->GetVariableByName(SHADER_TYPE_PIXEL, "tClouds")->Set(_impl->cloudTarget->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
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
		_impl->srb->GetVariableByName(SHADER_TYPE_PIXEL, "tClouds")->Set(_impl->cloudTarget->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
	}
	ctx->SetPipelineState(_impl->pipeline);
	ctx->CommitShaderResources(_impl->srb, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	ctx->Draw(DrawAttribs{ 3, DRAW_FLAG_VERIFY_ALL });
}
