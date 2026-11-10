// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

// Port of the former D3D11EnhancedSceneRenderer to Diligent. The shader and the batching (instances of
// one model and material, up to 16 per draw, split on animation frame, colour and flags) are
// the same. Normals are still packed as 10:10:10:2, but unpacked in the vertex shader because
// Diligent has no such vertex format.

#include "DiligentEnhancedSceneRenderer.h"
#include "../util/LoadingProgress.h"
#include "../util/FrameStats.h"
#include <thread>
#include <array>
#include <mutex>
#include <atomic>
#include "renderer_diligent.h"
#include "DiligentClouds.h"
#include "DiligentGI.h"
#include "DiligentRayShadows.h"
#include "GpuProfiler.h"
#include <unordered_set>
#include "../Model.h"
#include "../scene.h"
#include "../file.h"
#include "bitmap.h"
#include "TextureCache.h"
#include "../Camera.h"
#include "../util/util.h"
#include "../settings.h"
#include "RenderSettings.h"
#include "../Environment.h"
#include <nlohmann/json.hpp>
#include <cmath>
#include <cstring>
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
Texture2D normalMap; // override packs: <texture>_normal (tangent space, +z out), else flat with alpha 0 (derive)
Texture2D ormMap;    // override packs: <texture>_orm (occlusion, roughness, metalness), else 1, 0.65, 0
// override packs: <texture>_emissive, the light the surface gives off (any model: buildings,
// units, trees, props): rgb its colour and strength, alpha when: 0 always (embers, runes),
// 1 only at night (windows, lanterns), between them both; else black
Texture2D emissiveMap;
// override packs: <texture>_height, the relief for parallax occlusion mapping: 1 is the surface,
// 0 lies POM_RANGE (metres) below it; else a 1x1 default, which turns it off
Texture2D heightMap;
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
	float4 LightMasks; // the depth pre-pass's: ray-traced shadows on, ambient occlusion strength, a pixel's size
	float4 FogSun;        // the haze's tint (DiligentSkyRenderer): towards the true sun (unit), its weight (0 off)
	float4 FogSunColour;  // the sun's disc through the air (rgb, the sky's tone), its glow's strength
	float4 FogHorizon[32]; // the sky's colour 2 degrees up, by the bearing (atan2(z, x), 32 steps from 0)
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


// The depth pre-pass's masks at a pixel (DiligentGfx::FinishLightingPrepass): the ray-traced
// shadows' (on the sun's light; 1 sunlit) and the ambient occlusion (on the ambient light; half
// size, four bilinear taps as the composite took them; 1 open)
Texture2D<float> lightRtMask;
Texture2D<float> lightAoMask;
SamplerState lightMaskSampler;
float RtSunMask(float4 pos)
{
	return LightMasks.x != 0 ? lightRtMask.Load(int3(pos.xy, 0)) : 1;
}
float AoMask(float4 pos)
{
	if (LightMasks.y <= 0)
		return 1;
	float2 uv = pos.xy * LightMasks.zw, h = LightMasks.zw * 2;
	float ao = (lightAoMask.SampleLevel(lightMaskSampler, uv + float2(-h.x, -h.y), 0) + lightAoMask.SampleLevel(lightMaskSampler, uv + float2(h.x, -h.y), 0)
		+ lightAoMask.SampleLevel(lightMaskSampler, uv + float2(-h.x, h.y), 0) + lightAoMask.SampleLevel(lightMaskSampler, uv + float2(h.x, h.y), 0)) * 0.25;
	return lerp(1, ao, LightMasks.y);
}

// Fog of a world position: the distance fog, or the map-edge fog where that is stronger. The edge
// fog follows the signed distance to the playable area's border (negative inside; the larger of x
// and z, so that it follows the square band of the map's edge): it may start inside the playable
// area and is opaque before the map mesh ends.
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
	float4 MeshLighting; // ambient, diffuse factor, per-pixel lighting on, specular strength
	// the sky light (ShadowEnvironment::skyLight): above (w: on), at the horizon (w: the mean
	// brightness, which the ambient is divided by, so it keeps its strength), below
	float4 SkyUp;
	float4 SkyHorizon;
	float4 SkyDown;
	// x: the strength of the normals derived from the albedo's brightness, for materials
	// without a <texture>_normal map (RenderSettings::modelDetailNormals; 0 = flat);
	// y: the snow lying on roofs and models, z: how wet they are, w: how dark the night is
	// (wkbai's weather and day and night, 0 when off: the emissive maps' night lights)
	float4 Material;
	float4 FireLightCount; // x: how many of FireLights are in use
	float4 FireLights[32]; // per light: position and radius, colour
	float4 Season; // wkbai's seasons: x autumn (the broadleaf trees' colours), y dryness (heat); 0 without
};

// The frame's fire lights (ShadowEnvironment::fireLights): each a warm point light that fades
// out over its radius, half wrapped around the surface so that fires also light the ground
// beside them
float3 FireLight(float3 pos, float3 n)
{
	float3 sum = float3(0, 0, 0);
	for (int i = 0; i < (int)FireLightCount.x; i++) {
		float4 at = FireLights[2 * i];
		float3 d = at.xyz - pos;
		float dist = length(d);
		float fade = saturate(1 - dist / at.w);
		float wrap = saturate(dot(n, d / max(dist, 1e-3))) * 0.7 + 0.3;
		sum += FireLights[2 * i + 1].rgb * (fade * fade * wrap);
	}
	return sum;
}


cbuffer SceneBuffer
{
	uint VertexBase; // animated meshes: the material group's first vertex in a frame
};

// Animated meshes (DilAnim): the animation's packed key frames, the mesh's binding of its draw
// vertices to the animation's vertices, and the original's table of 256 normals
StructuredBuffer<uint> AnimKeys;
StructuredBuffer<uint> AnimBind;
StructuredBuffer<float> NormalTable;

// one axis of an animated vertex at a key
float AnimAxis(uint c, uint key, uint v)
{
	uint base = AnimKeys[1 + c] + key * AnimKeys[0];
	uint w = AnimKeys[base + 2 + v / 3];
	return asfloat(AnimKeys[base]) + asfloat(AnimKeys[base + 1]) * (((w >> (11 * (v % 3))) & 1023) / 1023.0);
}

// the instance's keys and blends (DilAnim::instanceWords)
struct AnimState
{
	uint3 key;
	float3 blend;
	uint normalKey;
	float normalBlend;
};

AnimState DecodeAnimState(uint4 anim)
{
	AnimState s;
	s.key = anim.xyz & 1023;
	s.blend = float3((anim.xyz >> 10) & 65535) / 65535.0;
	s.normalKey = (anim.x >> 26) | (((anim.y >> 26) & 15) << 6);
	s.normalBlend = (anim.z >> 26) / 63.0;
	return s;
}

// an animated vertex of the file at the instance's time
float3 AnimVertex(uint v, AnimState s)
{
	float3 p;
	[unroll] for (uint c = 0; c < 3; c++) {
		uint k2 = min(s.key[c] + 1, max(AnimKeys[4 + c], 1) - 1);
		p[c] = lerp(AnimAxis(c, s.key[c], v), AnimAxis(c, k2, v), s.blend[c]);
	}
	return p;
}

float3 TableNormal(uint key, uint n)
{
	uint w = AnimKeys[AnimKeys[7] + key * AnimKeys[8] + n / 4];
	uint i = (w >> (8 * (n % 4))) & 255;
	return float3(NormalTable[3 * i], NormalTable[3 * i + 1], NormalTable[3 * i + 2]);
}

// a bound position or normal (AnimBind's three words from b): directly the animation's, or a
// place in the frame of its triangle; rest when it has none (and when the model stands still)
float3 BoundAnim(uint b, AnimState s, bool normal, float3 rest)
{
	uint w0 = AnimBind[b], w1 = AnimBind[b + 1], w2 = AnimBind[b + 2];
	uint v0 = w0 & 0xFFFF, v1 = w0 >> 16, v2 = w1 & 0xFFFF;
	if ((AnimKeys[10] & 1) != 0 || v0 == 0xFFFF)
		return rest;
	if (v1 == 0xFFFF) {
		if (!normal)
			return v0 < AnimKeys[11] ? AnimVertex(v0, s) : rest;
		if (v0 >= AnimKeys[12] || AnimKeys[9] < 2)
			return rest;
		uint k2 = min(s.normalKey + 1, AnimKeys[9] - 1);
		return normalize(lerp(TableNormal(s.normalKey, v0), TableNormal(k2, v0), s.normalBlend));
	}
	if (max(v0, max(v1, v2)) >= AnimKeys[11])
		return rest;
	float3 p0 = AnimVertex(v0, s), e1 = AnimVertex(v1, s) - p0, e2 = AnimVertex(v2, s) - p0;
	float3 n = cross(e1, e2);
	n = dot(n, n) > 1e-20 ? normalize(n) : float3(0, 1, 0);
	float a = f16tof32(w1 >> 16), bb = f16tof32(w2 & 0xFFFF), c = f16tof32(w2 >> 16);
	float3 r = e1 * a + e2 * bb + n * c;
	return normal ? normalize(r) : p0 + r;
}

struct VS_OUTPUT
{
	float4 Pos : SV_POSITION;
	float3 Norm: NORMAL;
	float4 Color : COLOR0;
	float2 Texcoord : TEXCOORD0;
	float Fog : FOG;
	float3 WorldPos : TEXCOORD1;
	float4 Tint : TEXCOORD2; // rgb: the blocked/placeable tint, a: the cloud shadow
	// x 1: a field in winter (SEFLAG_FALLOW), drawn as bare soil; y 1: charred (SEFLAG_CHARRED);
	// z: how much the surface faces up (the world normal's y, for the snow: also on an unlit,
	// highlighted model, whose Norm is 0)
	float3 Fallow : TEXCOORD3;
	float2 Leaves : TEXCOORD4; // x 1: broadleaf (SEFLAG_LEAVES); y: the tree's own seed (from where it stands)
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
		if (CloudSun.w > 0) // the dynamic clouds' (RenderSettings::dynamicClouds)
			cloud = CloudShadow(EntityTransform[3].xyz);
		else if (CloudParams.w != 0)
			cloud = cloudTexture.SampleLevel(cloudSampler, EntityTransform[3].xz * CloudParams.z + CloudParams.xy, 0).r;
		if (SkyOccRegion.w != 0) {
			// the sky light (RenderSettings::skyGI): the ambient from the sky as much as the vertex
			// sees of it, the clouds only on the sun's light
			float3 wp = mul(float4(Pos, 1), EntityTransform).xyz;
			float vis = SkyVisibility(wp, true);
			// (and the light bounced off the ground and the roofs around, with the dynamic GI)
			output.Color = float4(min(MeshLighting.x * 1.15 * vis * SkyAmbient(output.Norm) + cloud * MeshLighting.y * SunColour.rgb * lambert
				+ BounceLight(wp, output.Norm, vis) + DirectWindowLight(wp, output.Norm), 1), 1);
		}
		else
			output.Color = float4(cloud * min(MeshLighting.x + MeshLighting.y * SunColour.rgb * lambert, 1), 1);
		output.Tint.a = cloud;
	}
	output.Tint.rgb = float3(1, 1, 1);
	output.Fallow = float3((EntityFlags & 64) ? 1 : 0, (EntityFlags & 256) ? 1 : 0,
		normalize(mul(float4(Normal * 2 - float3(1,1,1), 0), EntityTransform).xyz).y);
	output.Leaves = float2((EntityFlags & 128) ? 1 : 0, frac(sin(dot(EntityTransform[3].xz, float2(12.9898, 78.233))) * 43758.5453));
	if (EntityFlags & 8) // SEFLAG_BLOCKED
		output.Tint.rgb *= float3(1.0, 0.25, 0.25);
	if (EntityFlags & 16) // SEFLAG_PLACEABLE
		output.Tint.rgb *= float3(0.35, 1.0, 0.35);
	output.Color.rgb *= output.Tint.rgb;
	output.Texcoord = Texcoord;
	float3 worldPos = mul(float4(Pos, 1), EntityTransform).xyz;
	output.WorldPos = worldPos;
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
// (the same inputs as a static mesh's: the rest pose's position and normal for the vertices the
// animation doesn't move)
VS_OUTPUT VS_Anim(float3 Pos : ATTRIB0, uint Normal : ATTRIB1, float2 Texcoord : ATTRIB2,
	float4 T0 : ATTRIB3, float4 T1 : ATTRIB4, float4 T2 : ATTRIB5, float4 T3 : ATTRIB6, uint4 Anim : ATTRIB7, uint VertexId : SV_VertexID)
{
	AnimState s = DecodeAnimState(Anim);
	uint b = (VertexBase + VertexId) * 6;
	float3 pos = BoundAnim(b, s, false, Pos);
	float3 nrm = BoundAnim(b + 3, s, true, UnpackNormal(Normal) * 2 - 1);
	return Shade(pos, nrm * 0.5 + 0.5, Texcoord, T0, T1, T2, T3, Anim.w);
}

// The light on a model: the original's per vertex (Color), or with per-pixel lighting on
// (RenderSettings::modelLighting, the legacy adapter of PLAN.md's PBR step) the same ambient and
// sun term per pixel plus the sun's highlight of a dielectric surface (GGX, Schlick's Fresnel
// with F0 0.04, Smith's visibility). The original's textures have no roughness: a rough 0.65.
// The sky's colour towards a direction: between the horizon's and the zenith's (or the ground's)
float3 SkyColour(float3 d)
{
	return d.y >= 0 ? lerp(SkyHorizon.rgb, SkyUp.rgb, saturate(d.y)) : lerp(SkyHorizon.rgb, SkyDown.rgb, saturate(-d.y));
}

// The surface a pixel shows: its UV (moved by the parallax), the UV derivatives of the triangle
// itself (for mip selection and the tangent frame, so the relief's steps make no seams), and
// the relief's own shadow on the sun's light
static float2 SurfUV;
static float2 SurfDx;
static float2 SurfDy;
static float PomShadow;
static const float POM_RANGE = 0.06;

float PomDepth(float2 uv)
{
	return (1 - heightMap.SampleGrad(inpSampler, uv, SurfDx, SurfDy).r) * POM_RANGE;
}

// Parallax occlusion mapping with self-shadowing (with per-pixel lighting, for materials with a
// height map). The relief is metric: the UV's world gradients come from the screen-space
// derivatives, so a ray through the relief moves the UV by the material's own scale. The view
// ray steps down until it meets the relief (8-24 steps, more at grazing angles), refined between
// the last two; from there a ray towards the sun finds whether relief above it shades the point.
// Skipped where the relief is under a third of a pixel deep, where the mips average it out.
void PrepareSurface(VS_OUTPUT input)
{
	SurfUV = input.Texcoord;
	SurfDx = ddx(input.Texcoord);
	SurfDy = ddy(input.Texcoord);
	PomShadow = 1;
	if (MeshLighting.z == 0 || dot(input.Norm, input.Norm) == 0)
		return;
	float2 hsize;
	heightMap.GetDimensions(hsize.x, hsize.y);
	if (hsize.x < 2)
		return;
	float3 n = normalize(input.Norm);
	float3 dp1 = ddx(input.WorldPos), dp2 = ddy(input.WorldPos);
	float footprint = max(length(dp1), length(dp2));
	if (POM_RANGE < footprint * 0.33)
		return;
	float3 dp2perp = cross(dp2, n), dp1perp = cross(n, dp1);
	float det = dot(dp1, dp2perp);
	if (abs(det) < 1e-14)
		return;
	float3 gu = (dp2perp * SurfDx.x + dp1perp * SurfDy.x) / det;   // du per metre, in world space
	float3 gv = (dp2perp * SurfDx.y + dp1perp * SurfDy.y) / det;
	float3 e = normalize(input.WorldPos - FogCamera.xyz);          // from the eye
	float en = max(-dot(e, n), 0.15);
	float3 lat = (e + n * en) / en;                                // sideways per metre of depth
	float2 uvPerDepth = float2(dot(gu, lat), dot(gv, lat));
	int steps = (int)lerp(24.0, 8.0, en);
	float stepDepth = POM_RANGE / steps;
	float2 uv = SurfUV;
	float d = 0;
	float gap = PomDepth(uv);                                      // > 0: the ray is above the relief
	[loop] for (int i = 1; i <= steps && gap > 0; i++) {
		float nd = stepDepth * i;
		float2 nuv = SurfUV + uvPerDepth * nd;
		float ngap = PomDepth(nuv) - nd;
		if (ngap <= 0) {
			float t = gap / (gap - ngap);
			uv = lerp(uv, nuv, t);
			d = lerp(d, nd, t);
			gap = 0;
			break;
		}
		uv = nuv; d = nd; gap = ngap;
	}
	SurfUV = uv;
	float3 l = normalize(SunDirection);
	float ln = dot(l, n);
	if (ln > 0.02 && d > 1e-4) {
		float3 latl = (l - n * ln) / ln;                           // sideways per metre of rise
		float2 uvPerRise = float2(dot(gu, latl), dot(gv, latl));
		const int sh = 10;
		float occl = 0;
		[loop] for (int j = 1; j <= sh; j++) {
			float rise = d * j / sh;
			float above = (d - rise) - PomDepth(uv + uvPerRise * rise);  // > 0: relief above the ray
			occl = max(occl, saturate(above / POM_RANGE * 6 * (1 - (float)j / (sh + 1))));
		}
		PomShadow = 1 - occl;
	}
}

float3 ModelLight(VS_OUTPUT input, float3 albedo)
{
	// the depth pre-pass's masks: the ray-traced shadow on the sun's light, the occlusion on the
	// ambient (the vertex-lit paths, sun and ambient in one colour: the shadow keeps 55%)
	const float rtSun = RtSunMask(input.Pos), occ = AoMask(input.Pos);
	if (dot(input.Norm, input.Norm) == 0)
		return input.Color.rgb * albedo * lerp(0.55, 1, rtSun) * occ;
	if (MeshLighting.z == 0)
		return (input.Color.rgb * lerp(0.55, 1, rtSun) * occ + FireLight(input.WorldPos, normalize(input.Norm)) * input.Tint.rgb) * albedo;
	float3 n = normalize(input.Norm);
	// the normal map in a tangent frame from the screen-space derivatives (the meshes have
	// no tangents)
	float4 ns = normalMap.SampleGrad(inpSampler, SurfUV, SurfDx, SurfDy);
	float3 tn = ns.xyz * 2 - 1;
	// z from x and y (a BC5 normal map has only those; the same for a full one)
	tn.z = sqrt(saturate(1 - dot(tn.xy, tn.xy)));
	// the legacy adapter's normals: without a normal map, the albedo's brightness as a height
	// (bright = raised), its slope over one texel of the current mip level
	if (ns.a < 0.5 && Material.x > 0) {
		float2 size;
		inpTexture.GetDimensions(size.x, size.y);
		float2 duv = max(fwidth(input.Texcoord), 1 / size);
		const float3 lw = float3(0.299, 0.587, 0.114);
		float hx = dot(inpTexture.Sample(inpSampler, input.Texcoord + float2(duv.x, 0)).rgb, lw)
			- dot(inpTexture.Sample(inpSampler, input.Texcoord - float2(duv.x, 0)).rgb, lw);
		float hy = dot(inpTexture.Sample(inpSampler, input.Texcoord + float2(0, duv.y)).rgb, lw)
			- dot(inpTexture.Sample(inpSampler, input.Texcoord - float2(0, duv.y)).rgb, lw);
		tn = normalize(float3(-hx * Material.x, -hy * Material.x, 1));
	}
	if (abs(tn.z - 1) > 1e-3) {
		float3 dp1 = ddx(input.WorldPos), dp2 = ddy(input.WorldPos);
		float2 duv1 = SurfDx, duv2 = SurfDy;
		float3 dp2perp = cross(dp2, n), dp1perp = cross(n, dp1);
		float3 t = dp2perp * duv1.x + dp1perp * duv2.x;
		float3 b = dp2perp * duv1.y + dp1perp * duv2.y;
		float invmax = rsqrt(max(max(dot(t, t), dot(b, b)), 1e-12));
		n = normalize(t * invmax * tn.x + b * invmax * tn.y + n * tn.z);
	}
	float3 orm = ormMap.SampleGrad(inpSampler, SurfUV, SurfDx, SurfDy).rgb;
	float3 l = normalize(SunDirection);
	float3 v = normalize(FogCamera.xyz - input.WorldPos);
	float3 h = normalize(l + v);
	float nl = saturate(dot(n, l)), nv = max(dot(n, v), 1e-3), nh = saturate(dot(n, h)), vh = saturate(dot(v, h));
	// image-based light from the sky box (a stand-in for a filtered cube map): the ambient takes
	// the sky's colour where the surface faces, at the same mean strength, and reflections the
	// sky's colour along the reflected view. Without a sky, the map's fog colour.
	float3 ambientTint = float3(1, 1, 1), skyReflected = FogColor.rgb;
	if (SkyUp.w != 0) {
		ambientTint = SkyColour(n) / SkyHorizon.w;
		skyReflected = SkyColour(reflect(-v, n));
	}
	// the sky light (RenderSettings::skyGI): the ambient and the sky's reflection as much as the
	// point sees of the sky, the sun's light bounced off the ground onto faces turned down, the
	// clouds' shadow on the sun's light only
	const bool gi = SkyOccRegion.w != 0;
	float vis = 1, sunShade = 1, allShade = input.Tint.a;
	float3 bounce = float3(0, 0, 0);
	if (gi) {
		vis = SkyVisibility(input.WorldPos, true);
		sunShade = input.Tint.a;
		allShade = 1;
		// the dynamic GI's (RenderSettings::dynamicGI): the light of the ground and roofs around,
		// in their colours, instead of a warm guess
		if (GIRegion.w != 0)
			bounce = BounceLight(input.WorldPos, n, vis);
		else
			bounce = SunColour.rgb * (saturate(l.y) * SkyOccLight.y * 0.35 * saturate(0.5 - 0.5 * n.y)) * float3(0.9, 0.85, 0.7);
		ambientTint *= 1.15 * vis;
	}
	sunShade *= PomShadow * rtSun;
	float3 diffuse = min((MeshLighting.x * orm.r * ambientTint + bounce * orm.r) * occ + MeshLighting.y * SunColour.rgb * nl * sunShade, 1);
	float roughness = clamp(orm.g, 0.05, 1), metal = orm.b;
	float a = roughness * roughness, a2 = a * a;
	float d = a2 / (3.14159265 * pow(nh * nh * (a2 - 1) + 1, 2));
	float k = a / 2;
	float gvis = 1 / ((nl * (1 - k) + k) * (nv * (1 - k) + k));
	float3 f0 = lerp(float3(0.04, 0.04, 0.04), albedo, metal);
	float3 f = f0 + (1 - f0) * pow(1 - vh, 5);
	float3 specular = SunColour.rgb * (d * gvis * f * nl / 4) * MeshLighting.w * sunShade;
	// the sky's light reflected, by the Fresnel of the view and the surface's occlusion; rough
	// surfaces get less. Without it a metal lit by the sun alone is black away from the highlight.
	float3 fv = f0 + (max(1 - roughness, f0) - f0) * pow(1 - nv, 5);
	float3 sky = skyReflected * fv * orm.r * (1 - 0.5 * roughness) * vis * occ;
	float3 fire = FireLight(input.WorldPos, n) * orm.r;
	// Ultra's lamps and windows: direct light, without the occlusion (DirectWindowLight)
	if (gi && GIRegion.w != 0)
		fire += DirectWindowLight(input.WorldPos, n);
	return ((diffuse * albedo * (1 - metal) + specular + sky) * allShade + fire * albedo * (1 - metal)) * input.Tint.rgb;
}

// wkbai's weather on a model: snow lying where the surface faces up (roofs, ledges, the tops of
// trees and rocks), in patches; wet surfaces darker
// the material's colour (Material::colour: glTF's baseColorFactor, a texture name's tint)
cbuffer MaterialBuffer
{
	float4 BaseColour;
};

float3 WeatherAlbedo(VS_OUTPUT input, float3 albedo)
{
	albedo = min(albedo * BaseColour.rgb, 1);
	// a field in winter: the crops gone, furrowed soil (the texture's pattern kept as its light
	// and dark)
	if (input.Fallow.x > 0.5)
		albedo = float3(0.34, 0.27, 0.2) * (0.5 + 0.9 * dot(albedo, float3(0.299, 0.587, 0.114)));
	// burnt by a wildfire: charcoal, the bark's grain kept, ash-grey where it was light
	if (input.Fallow.y > 0.5) {
		float lum = dot(albedo, float3(0.299, 0.587, 0.114));
		albedo = lerp(float3(0.035, 0.03, 0.028), float3(0.3, 0.29, 0.27), saturate(lum * lum * 2.5));
	}
	// a broadleaf tree's green leaves: in autumn yellow, orange, red and brown, each tree its own
	// mix (and patches of it), the trunk and branches as they are; in heat a little dried
	if (input.Leaves.x > 0.5 && Season.x + Season.y > 0) {
		float green = saturate((albedo.g - max(albedo.r, albedo.b)) * 14 + 0.2) * saturate(albedo.g * 6);
		float lum = dot(albedo, float3(0.299, 0.587, 0.114));
		float pick = frac(input.Leaves.y + (frac(sin(dot(floor(input.Texcoord * 6), float2(39.3, 11.7))) * 917.31) - 0.5) * 0.35);
		// vibrant: saturated hues, brightened (the leaves' texture is dark), the texture's light and
		// dark kept; brown only for a few
		float3 gold = float3(1.0, 0.78, 0.06), orange = float3(1.0, 0.45, 0.02), red = float3(0.9, 0.1, 0.03), brown = float3(0.6, 0.3, 0.08);
		float3 hue = pick < 0.4 ? lerp(gold, orange, pick / 0.4) : pick < 0.85 ? lerp(orange, red, (pick - 0.4) / 0.45) : lerp(red, brown, (pick - 0.85) / 0.15);
		albedo = lerp(albedo, hue * clamp(lum * 3.4, 0.45, 1.15), Season.x * green);
		albedo = lerp(albedo, float3(1.25, 1.05, 0.5) * lum, Season.y * 0.45 * green);
	}
	if (Material.y + Material.z <= 0)
		return albedo;
	float up = input.Fallow.z;
	float2 cell = floor(input.WorldPos.xz * 1.7);
	float patch = frac(sin(dot(cell, float2(12.9898, 78.233))) * 43758.5453);
	float snow = saturate((up - 0.14) / 0.3) * // (steep thatch holds some too)
		 saturate(Material.y * 1.8 - patch * 0.45);
	albedo *= 1 - 0.3 * Material.z * (1 - snow);
	return lerp(albedo, float3(0.9, 0.92, 0.97), snow);
}

// the emissive map's light: the always-on part, the night part by the night's darkness; kept as
// bright under the night's darker grade (divided by its exposure), so it blooms, and as
// coloured under its paler one (its colour pushed out by as much as the grade takes away:
// candle light stays orange)
float3 Emission(VS_OUTPUT input)
{
	float4 e = emissiveMap.SampleGrad(inpSampler, SurfUV, SurfDx, SurfDy);
	float3 c = e.rgb * lerp(1, Material.w, e.a) / max(Grade.x, 0.35);
	float lum = dot(c, float3(0.299, 0.587, 0.114));
	return max(lum + (c - lum) / max(Grade.w, 0.4), 0);
}

float4 PS(VS_OUTPUT input) : SV_Target
{
	PrepareSurface(input);
	float4 tex = inpTexture.SampleGrad(inpSampler, SurfUV, SurfDx, SurfDy);
	tex.rgb = WeatherAlbedo(input, tex.rgb);
	float4 c = lerp(float4(ModelLight(input, tex.rgb) + Emission(input), input.Color.a * tex.a), float4(FogTint(input.WorldPos), FogColor.a), input.Fog);
	return float4(GradeColour(c.rgb), c.a);
}

static const float ALPHA_REF = 0.94;
float4 PS_Alpha(VS_OUTPUT input) : SV_Target
{
	PrepareSurface(input);
	float4 tex = inpTexture.SampleGrad(inpSampler, SurfUV, SurfDx, SurfDy);
	if(tex.a < ALPHA_REF) discard;
	tex.rgb = WeatherAlbedo(input, tex.rgb);
	float4 c = lerp(float4(ModelLight(input, tex.rgb) + Emission(input), input.Color.a * tex.a), float4(FogTint(input.WorldPos), FogColor.a), input.Fog);
	return float4(GradeColour(c.rgb), c.a);
}

// The depth pre-pass's alpha-tested materials: the same test on the same surface as PS_Alpha's
// (its relief's UV), so that the shading covers exactly the depth
void PS_DepthAlpha(VS_OUTPUT input)
{
	PrepareSurface(input);
	if (inpTexture.SampleGrad(inpSampler, SurfUV, SurfDx, SurfDy).a < ALPHA_REF) discard;
}

// Static shadow casters with alpha-tested materials
void PS_ShadowAlpha(VS_OUTPUT input)
{
	if(inpTexture.Sample(inpSampler, input.Texcoord).a < ALPHA_REF) discard;
}

// The dynamic GI's models from above (DiligentGI): their albedo as drawn (the weather's, the
// seasons'), and the sun's n.l, for the bounce map's light
float4 GICapture(VS_OUTPUT input, float3 albedo)
{
	float3 n = dot(input.Norm, input.Norm) > 0 ? normalize(input.Norm) : float3(0, 1, 0);
	return float4(albedo, saturate(dot(n, normalize(SunDirection))));
}

float4 PS_GI(VS_OUTPUT input) : SV_Target
{
	float4 tex = inpTexture.Sample(inpSampler, input.Texcoord);
	return GICapture(input, WeatherAlbedo(input, tex.rgb));
}

float4 PS_GIAlpha(VS_OUTPUT input) : SV_Target
{
	float4 tex = inpTexture.Sample(inpSampler, input.Texcoord);
	if (tex.a < ALPHA_REF) discard;
	return GICapture(input, WeatherAlbedo(input, tex.rgb));
}

// And the light they give off (their emissive maps', now), for the light out of the windows
// onto the ground, the walls and the units about them: drawn from above too, added up without
// the depth test (the walls under their roofs, which are often another material), each face
// as the material's mean over its whole map (8 x 8 bilinear taps of its last mip: each 2 x 2
// texels of a 16 x 16 one) times how much more of it there is than shows from above (1 / |n.y|,
// up to 10: a wall stands for its height), at the windows' strength all round (GI_EMISSION)
static const float GI_EMISSION = 150;
float3 MeanEmission()
{
	uint w, h, levels;
	emissiveMap.GetDimensions(0, w, h, levels);
	float lod = levels - 1;
	float4 sum = float4(0, 0, 0, 0);
	[unroll] for (int y = 0; y < 8; y++)
		[unroll] for (int x = 0; x < 8; x++)
			sum += emissiveMap.SampleLevel(inpSampler, (float2(x, y) + 0.5) / 8, lod);
	sum /= 64;
	// as Emission: the always-on part and the night part (the mean alpha, of the lit texels, a
	// mix of them; the unlit texels' alpha does not matter)
	float lit = max(sum.r, max(sum.g, sum.b));
	float a = lit > 0 ? saturate(sum.a) : 0;
	return sum.rgb * lerp(1, Material.w, a) * GI_EMISSION;
}

float4 GIEmission(VS_OUTPUT input)
{
	float ny = dot(input.Norm, input.Norm) > 0 ? abs(normalize(input.Norm).y) : 1;
	return float4(MeanEmission() * min(1 / max(ny, 0.1), 10), 0);
}

float4 PS_GIEmit(VS_OUTPUT input) : SV_Target
{
	return GIEmission(input);
}

float4 PS_GIEmitAlpha(VS_OUTPUT input) : SV_Target
{
	if (inpTexture.Sample(inpSampler, input.Texcoord).a < ALPHA_REF) discard;
	return GIEmission(input);
}

// FSR 3.1's motion vectors of what moved since the last frame: drawn over the scene's depth at the
// same (jittered) position, each vertex also where the unjittered camera sees it now and saw it in
// the last frame, with the instance's transform and key frames then (the P and PrevAnim inputs)
cbuffer MotionBuffer
{
	matrix MotionViewProj;     // this frame's view-projection, unjittered
	matrix MotionPrevViewProj; // the last frame's
};

struct MOTION_OUTPUT
{
	float4 Pos : SV_POSITION;
	float2 Texcoord : TEXCOORD0;
	float4 Cur : TEXCOORD1;
	float4 Prev : TEXCOORD2;
};

MOTION_OUTPUT Motion(float3 pos, float3 prevPos, float2 texcoord, float4 T0, float4 T1, float4 T2, float4 T3, float4 P0, float4 P1, float4 P2, float4 P3)
{
	float4x4 cur = transpose(float4x4(T0, T1, T2, T3)), prev = transpose(float4x4(P0, P1, P2, P3));
	MOTION_OUTPUT o;
	o.Pos = mul(float4(pos, 1), mul(cur, Transform));
	o.Cur = mul(mul(float4(pos, 1), cur), MotionViewProj);
	o.Prev = mul(mul(float4(prevPos, 1), prev), MotionPrevViewProj);
	o.Texcoord = texcoord;
	return o;
}

MOTION_OUTPUT VS_Motion(float3 Pos : ATTRIB0, float2 Texcoord : ATTRIB2,
	float4 T0 : ATTRIB3, float4 T1 : ATTRIB4, float4 T2 : ATTRIB5, float4 T3 : ATTRIB6, uint4 Anim : ATTRIB7,
	float4 P0 : ATTRIB8, float4 P1 : ATTRIB9, float4 P2 : ATTRIB10, float4 P3 : ATTRIB11, uint4 PrevAnim : ATTRIB12)
{
	return Motion(Pos, Pos, Texcoord, T0, T1, T2, T3, P0, P1, P2, P3);
}

float3 AnimPosition(uint4 anim, uint vertexId, float3 rest)
{
	return BoundAnim((VertexBase + vertexId) * 6, DecodeAnimState(anim), false, rest);
}

MOTION_OUTPUT VS_AnimMotion(float3 Pos : ATTRIB0, float2 Texcoord : ATTRIB2,
	float4 T0 : ATTRIB3, float4 T1 : ATTRIB4, float4 T2 : ATTRIB5, float4 T3 : ATTRIB6, uint4 Anim : ATTRIB7,
	float4 P0 : ATTRIB8, float4 P1 : ATTRIB9, float4 P2 : ATTRIB10, float4 P3 : ATTRIB11, uint4 PrevAnim : ATTRIB12,
	uint VertexId : SV_VertexID)
{
	return Motion(AnimPosition(Anim, VertexId, Pos), AnimPosition(PrevAnim, VertexId, Pos), Texcoord, T0, T1, T2, T3, P0, P1, P2, P3);
}

// the last frame's position minus the current, in UV
float2 MotionVector(MOTION_OUTPUT input)
{
	return (input.Prev.xy / input.Prev.w - input.Cur.xy / input.Cur.w) * float2(0.5, -0.5);
}

float2 PS_Motion(MOTION_OUTPUT input) : SV_Target
{
	return MotionVector(input);
}

float2 PS_MotionAlpha(MOTION_OUTPUT input) : SV_Target
{
	if(inpTexture.Sample(inpSampler, input.Texcoord).a < ALPHA_REF) discard;
	return MotionVector(input);
}
)---";

// While prewarming (DiligentEnhancedSceneRenderer::prewarm of many models), the buffers are made
// on several threads without their data, which goes up afterwards on this thread (UpdateBuffer):
// on Direct3D 12 each buffer made with its data took about 2 ms, the game set's buildings 10 s.
struct PendingUpload {
	RefCntAutoPtr<IBuffer> buffer;
	std::vector<uint8_t> data;
};
static std::atomic<bool> g_deferUploads = false;
static std::mutex g_pendingMutex;
static std::vector<PendingUpload> g_pendingUploads;

static RefCntAutoPtr<IBuffer> CreateBufferWithData(IRenderDevice* device, BufferDesc desc, const void* data, uint64_t size)
{
	RefCntAutoPtr<IBuffer> buf;
	if (g_deferUploads && size) {
		desc.Usage = USAGE_DEFAULT;
		device->CreateBuffer(desc, nullptr, &buf);
		if (buf) {
			PendingUpload up{ buf, std::vector<uint8_t>((const uint8_t*)data, (const uint8_t*)data + size) };
			std::lock_guard<std::mutex> lock(g_pendingMutex);
			g_pendingUploads.push_back(std::move(up));
		}
	}
	else {
		BufferData init{ data, size };
		device->CreateBuffer(desc, size ? &init : nullptr, &buf);
	}
	if (!buf)
		ferr("Diligent: could not create buffer \"%s\"", desc.Name);
	return buf;
}

static RefCntAutoPtr<IBuffer> CreateImmutableBuffer(IRenderDevice* device, const char* name, BIND_FLAGS bind, const void* data, uint64_t size)
{
	BufferDesc desc;
	desc.Name = name;
	desc.Size = std::max<uint64_t>(size, 4);
	desc.BindFlags = bind;
	desc.Usage = USAGE_IMMUTABLE;
	return CreateBufferWithData(device, desc, data, size);
}

// A buffer the vertex shader reads as a StructuredBuffer of 4-byte elements
static RefCntAutoPtr<IBuffer> CreateStructuredBuffer(IRenderDevice* device, const char* name, const void* data, uint64_t size)
{
	BufferDesc desc;
	desc.Name = name;
	desc.Size = std::max<uint64_t>(size, 4);
	desc.BindFlags = BIND_SHADER_RESOURCE | (g_rayTracingBuffers ? BIND_RAY_TRACING : BIND_NONE);
	desc.Usage = USAGE_IMMUTABLE;
	desc.Mode = BUFFER_MODE_STRUCTURED;
	desc.ElementByteStride = 4;
	return CreateBufferWithData(device, desc, data, size);
}

struct DilMesh
{
	RefCntAutoPtr<IBuffer> vertices;
	RefCntAutoPtr<IBuffer> normals;
	std::vector<RefCntAutoPtr<IBuffer>> texcoords;
	RefCntAutoPtr<IBuffer> indices;
	uint32_t totVertices = 0, totIndices = 0;
	std::vector<DiligentRayShadows::Group> rtGroups;
	bool rtAlphaResolved = false; // rtGroups' alpha-tested textures and UVs
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
		const BIND_FLAGS rt = g_rayTracingBuffers ? BIND_RAY_TRACING : BIND_NONE;
		enh.vertices = CreateImmutableBuffer(device, "Mesh positions", BIND_VERTEX_BUFFER | rt, pVertices.data(), pVertices.size() * 12);

		std::vector<uint32_t> pNormals;
		pNormals.reserve(enh.totVertices);
		for (auto& mat : mesh.groupIndices)
			for (auto& grp : mat)
				pNormals.push_back(NormalToR10G10B10A2((grp.normal < mesh.exactNormals.size() ? mesh.exactNormals[grp.normal] : Mesh::s_normalTable[mesh.normals[grp.normal]]).normal()));
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
		enh.indices = CreateImmutableBuffer(device, "Mesh indices", BIND_INDEX_BUFFER | rt, pIndices.data(), pIndices.size() * 2);
		// the groups as the draws take them, for the ray-traced shadows' structures
		uint32_t startIndex = 0, startVertex = 0;
		const PolygonList& polylist = mesh.polyLists[0];
		for (size_t g = 0; g < polylist.groups.size(); g++) {
			const uint32_t numIndices = 3 * (uint32_t)polylist.groups[g].tupleIndex.size();
			const uint32_t numVertices = (uint32_t)mesh.groupIndices[g].size();
			enh.rtGroups.push_back({ startIndex, numIndices, startVertex, numVertices });
			startIndex += numIndices;
			startVertex += numVertices;
		}
		return enh;
	}
};

// A float as a 16-bit half (round to nearest; the binding's frame coordinates)
static uint16_t FloatToHalf(float f)
{
	uint32_t x;
	memcpy(&x, &f, 4);
	const uint32_t sign = (x >> 16) & 0x8000;
	int32_t exp = (int32_t)((x >> 23) & 255) - 127 + 15;
	uint32_t mant = x & 0x7FFFFF;
	if (exp <= 0) {
		if (exp < -10)
			return (uint16_t)sign;
		mant |= 0x800000;
		const uint32_t shift = (uint32_t)(14 - exp);
		uint32_t h = mant >> shift;
		if ((mant >> (shift - 1)) & 1)
			h++;
		return (uint16_t)(sign | h);
	}
	if (exp >= 31)
		return (uint16_t)(sign | 0x7C00);
	uint32_t h = sign | ((uint32_t)exp << 10) | (mant >> 13);
	if (mant & 0x1000)
		h++;
	return (uint16_t)h;
}

// GPU vertex animation (docs/formats/anim3.md "For the renderer"): an animation's key frames stay
// packed as the file has them (per axis its own key frames, 10 bits per vertex and axis, three
// vertices to a word) in one buffer, AnimKeys; the vertex shader decodes the two keys around each
// instance's time on each axis and interpolates (instead of every key frame baked unpacked, 16
// bytes per vertex, ~4 GB with the HD models). Header (words): 0 the words per axis key (2 +
// ceil(vertices / 3)), 1-3 each axis' first key, 4-6 each axis' key count, 7 the normals' first
// key, 8 the words per normal key (ceil(normals / 4), a byte each: an index into the normal
// table), 9 the normal key count, 10 flags (1: the model stands still: an HD model whose
// original doesn't fit the animation), 11 the animation's vertex count, 12 its normal count.
// A key: the axis' add and mul (floats), then the packed words.
//
// Each draw vertex of the mesh has a binding (AnimBind, per mesh, DilAnimBind): its position and
// its normal, each either an animated vertex (normal) of the file directly, or (HD models,
// ReplacementBinding) a place in the frame of a triangle of three animated vertices; with
// neither (a vertex no key frame moves) its rest position and normal from the mesh's own vertex
// buffers. So every animated model, original or HD, takes the one path.
static constexpr uint32_t ANIM_HEADER_WORDS = 13;
static constexpr uint32_t ANIM_MAX_KEYS = 1023; // 10 bits in the instance

struct DilAnim
{
	std::vector<uint32_t> animTimes; // every axis' key frame times together (the RT poses)
	std::array<std::vector<uint32_t>, 3> axisTimes;
	std::vector<uint32_t> normalTimes;
	uint32_t duration = 1;
	RefCntAutoPtr<IBuffer> keys;
	IBuffer* bind = nullptr; // the mesh's (Impl::animBinds)
	RefCntAutoPtr<IShaderResourceBinding> binding; // the keys and the binding, made at the first draw
	// the key frames packed (CPU only; thread-safe for different models, once prepared)
	struct Baked {
		std::vector<uint32_t> animTimes, normalTimes, words;
		std::array<std::vector<uint32_t>, 3> axisTimes;
		uint32_t duration = 1;
	};
	static DilAnim create(AnimatedModel* model, IRenderDevice* device) {
		return upload(bake(model), device);
	}
	static DilAnim upload(Baked&& b, IRenderDevice* device) {
		DilAnim enh;
		enh.animTimes = std::move(b.animTimes);
		enh.axisTimes = std::move(b.axisTimes);
		enh.normalTimes = std::move(b.normalTimes);
		enh.duration = b.duration;
		enh.keys = CreateStructuredBuffer(device, "Anim keys", b.words.data(), b.words.size() * 4);
		return enh;
	}
	static Baked bake(AnimatedModel* model) {
		Baked enh;
		model->prepare();
		const Anim& anim = model->anim;
		StaticModel* staticModel = model->getStaticModel();
		staticModel->prepare();
		enh.duration = std::max(anim.duration, 1u);
		std::set<uint32_t> animTimesSet;
		for (int c = 0; c < 3; ++c) {
			animTimesSet.insert(anim.coords[c].frameTimes.begin(), anim.coords[c].frameTimes.end());
			enh.axisTimes[c].assign(anim.coords[c].frameTimes.begin(), anim.coords[c].frameTimes.end());
		}
		enh.animTimes.assign(animTimesSet.begin(), animTimesSet.end());
		if (!enh.animTimes.empty())
			enh.animTimes.back() -= 1; // prevents last frame from becoming first frame
		const auto& nf = anim.normalFrames;
		enh.normalTimes.assign(nf.frameTimes.begin(), nf.frameTimes.end());
		const uint32_t nv = anim.numVertices, perKey = 2 + (nv + 2) / 3;
		const uint32_t nn = nf.numFrames >= 2 ? nf.numNormals : 0, perNormalKey = (nn + 3) / 4;
		std::vector<uint32_t>& w = enh.words;
		w.assign(ANIM_HEADER_WORDS, 0);
		w[0] = perKey;
		for (int c = 0; c < 3; c++) {
			const auto& ac = anim.coords[c];
			w[1 + c] = (uint32_t)w.size();
			w[4 + c] = std::min<uint32_t>(ac.numFrames, ANIM_MAX_KEYS);
			for (uint32_t k = 0; k < w[4 + c]; k++) {
				uint32_t add, mul;
				memcpy(&add, &ac.vertadd[k], 4);
				memcpy(&mul, &ac.vertmul[k], 4);
				w.push_back(add);
				w.push_back(mul);
				const auto& packed = ac.verts[k];
				for (uint32_t i = 0; i < perKey - 2; i++)
					w.push_back(i < packed.size() ? packed[i] : 0);
			}
		}
		w[7] = (uint32_t)w.size();
		w[8] = perNormalKey;
		w[9] = nn ? std::min<uint32_t>(nf.numFrames, ANIM_MAX_KEYS) : 0;
		for (uint32_t k = 0; k < w[9]; k++)
			for (uint32_t i = 0; i < perNormalKey; i++) {
				uint32_t word = 0;
				for (uint32_t j = 0; j < 4 && 4 * i + j < nn; j++)
					word |= (uint32_t)nf.norms[k][4 * i + j] << (8 * j);
				w.push_back(word);
			}
		// an HD model whose original Mesh3 doesn't fit the animation stands still (as before)
		if (const ReplacementBinding* rb = staticModel->getReplacementBinding())
			if (!rb->usable || rb->original.numVertices != anim.numVertices)
				w[10] |= 1;
		w[11] = nv;
		w[12] = nn;
		return enh;
	}
	// the packed key and blend of an instance at an animation time, as Anim::interpolate picks them
	static uint32_t keyAt(const std::vector<uint32_t>& times, uint32_t animtime, float& blend) {
		const size_t n = std::min<size_t>(times.size(), ANIM_MAX_KEYS);
		blend = 0.0f;
		if (n < 2)
			return 0;
		size_t frame = 0;
		for (; frame < n - 1; frame++)
			if (times[frame + 1] >= animtime)
				break;
		frame = std::min(frame, n - 2);
		blend = std::clamp((float)((int64_t)animtime - times[frame]) / (float)std::max<int64_t>((int64_t)times[frame + 1] - times[frame], 1), 0.0f, 1.0f);
		return (uint32_t)frame;
	}
	// the instance's three words: per axis its key (10 bits) and blend (16 bits); in their top 6
	// bits the normals' key (6 + 4 bits) and blend (6 bits)
	void instanceWords(uint32_t time, uint32_t out[3]) const {
		const uint32_t animtime = ((int32_t)time < 0 ? 0u : time) % duration;
		for (int c = 0; c < 3; c++) {
			float t;
			const uint32_t k = keyAt(axisTimes[c], animtime, t);
			out[c] = k | (uint32_t)std::lround(t * 65535.0f) << 10;
		}
		float nt;
		const uint32_t nk = keyAt(normalTimes, animtime, nt);
		out[0] |= (nk & 63) << 26;
		out[1] |= ((nk >> 6) & 15) << 26;
		out[2] |= (uint32_t)std::lround(nt * 63.0f) << 26;
	}
};

// A mesh's AnimBind (see DilAnim): per draw vertex 6 words, its position and its normal, each
// [v0 | v1 << 16, v2 | half(a) << 16, half(b) | half(c) << 16]: v1 0xFFFF the animated vertex
// (normal) v0 itself (v0 0xFFFF: none, the rest pose), else the triangle v0 v1 v2 and the place
// a, b, c in its frame (the two edges from v0 and the normal). Indices into the animation's
// vertices (normals), the files' order (the inverse of the mesh's remappers).
static std::vector<uint32_t> BakeAnimBind(StaticModel* staticModel)
{
	staticModel->prepare();
	const Mesh& mesh = staticModel->mesh;
	const ReplacementBinding* rb = staticModel->getReplacementBinding();
	auto inverse = [](const DynArray<uint16_t>& remap, size_t count) {
		std::vector<uint32_t> inv(count, 0xFFFF);
		for (size_t i = 0; i < remap.size(); i++)
			if (remap[i] < count && i < 0xFFFF)
				inv[remap[i]] = (uint32_t)i;
		return inv;
	};
	std::vector<uint32_t> words;
	auto direct = [&](uint32_t index) {
		words.push_back((index & 0xFFFF) | 0xFFFF0000u);
		words.push_back(0);
		words.push_back(0);
	};
	auto triangle = [&](const ReplacementBinding::Bind& b, const std::vector<uint32_t>& inv) {
		uint32_t v[3];
		for (int j = 0; j < 3; j++) {
			v[j] = b.v[j] < inv.size() ? inv[b.v[j]] : 0xFFFF;
			if (v[j] == 0xFFFF) { direct(0xFFFF); return; }
		}
		words.push_back(v[0] | v[1] << 16);
		words.push_back(v[2] | (uint32_t)FloatToHalf(b.a) << 16);
		words.push_back(FloatToHalf(b.b) | (uint32_t)FloatToHalf(b.c) << 16);
	};
	if (rb && rb->usable) {
		const Mesh& om = rb->original;
		const std::vector<uint32_t> invV = inverse(om.vertexRemapper, om.vertices.size() / 3);
		for (auto& grp : mesh.groupIndices)
			for (auto& t : grp) {
				if (t.vertex < rb->vertices.size()) triangle(rb->vertices[t.vertex], invV); else direct(0xFFFF);
				if (t.normal < rb->normals.size()) triangle(rb->normals[t.normal], invV); else direct(0xFFFF);
			}
	}
	else if (rb) { // an HD model that can't follow: the rest pose
		for (auto& grp : mesh.groupIndices)
			for (size_t i = 0; i < grp.size(); i++) { direct(0xFFFF); direct(0xFFFF); }
	}
	else {
		const std::vector<uint32_t> invV = inverse(mesh.vertexRemapper, mesh.vertices.size() / 3);
		const std::vector<uint32_t> invN = inverse(mesh.normalRemapper, mesh.normals.size());
		for (auto& grp : mesh.groupIndices)
			for (auto& t : grp) {
				direct(t.vertex < invV.size() ? invV[t.vertex] : 0xFFFF);
				direct(t.normal < invN.size() ? invN[t.normal] : 0xFFFF);
			}
	}
	return words;
}


// One instance of a draw (the instance vertex buffer)
struct Instance {
	Matrix transform; // transposed: its columns
	uint32_t anim[3] = {}; // animated: its keys and blends (DilAnim::instanceWords)
	uint32_t flags = 0;
};
static_assert(sizeof(Instance) == 80);

// An instance of the motion vectors' draw: now and in the last frame
struct MotionInstance {
	Instance current, previous;
};
static_assert(sizeof(MotionInstance) == 160);

// Half the side of the square around the camera's view centre covered by the static shadow map
static constexpr float STATIC_SHADOW_RADIUS = 200.0f;
static constexpr float STATIC_SHADOW_DEPTH = 1000.0f;

struct DiligentEnhancedSceneRenderer::Impl {
	std::map<StaticModel*, DilMesh> meshMap;
	std::map<StaticModel*, bool> hasBark; // a tree's model with bark (SEFLAG_BARE draws only that)
	std::map<AnimatedModel*, DilAnim> animMap;
	std::map<StaticModel*, RefCntAutoPtr<IBuffer>> animBinds; // per animated mesh (BakeAnimBind)
	RefCntAutoPtr<IBuffer> normalTable;
	// the ray-traced shadows' animated poses built this frame (a few a frame, the rest later)
	uint64_t poseFrame = ~0ull;
	int posesBuilt = 0;
	IBuffer* animBind(StaticModel* staticModel, IRenderDevice* device) {
		auto& b = animBinds[staticModel];
		if (!b) {
			const std::vector<uint32_t> words = BakeAnimBind(staticModel);
			b = CreateStructuredBuffer(device, "Anim binding", words.data(), words.size() * 4);
		}
		return b;
	}
	// Textures (one binding per texture), and the frame's environment (the cloud map)
	RefCntAutoPtr<IPipelineResourceSignature> signature, envSignature, animSignature;
	// [animated][alpha test]
	RefCntAutoPtr<IPipelineState> pipelines[2][2], shadowPipelines[2][2], motionPipelines[2][2];
	// the depth pre-pass's (renderDepth): depthOnly while it draws; prepared after it, until the
	// shading's render()
	RefCntAutoPtr<IPipelineState> depthPipelines[2][2];
	bool depthOnly = false, prepared = false;
	ITextureView* depthTarget = nullptr; // renderDepth's
	// the dynamic GI's models from above (DiligentGI): into ShadowEnvironment::giCapture
	RefCntAutoPtr<IPipelineState> giPipelines[2][2];
	// and the light they give off (added up, no depth), drawn only once a material has an
	// emissive map (anyEmissive)
	RefCntAutoPtr<IPipelineState> giEmitPipelines[2][2];
	bool anyEmissive = false;
	// per texture and material colour (Material::colour, its MaterialBuffer in the binding): the
	// binding, its textures (albedo, normal, ORM, emissive, height; null: the default) and their
	// generations when it was made (TouchTexture), the frame they were last checked
	struct TextureBinding {
		RefCntAutoPtr<IShaderResourceBinding> srb;
		RefCntAutoPtr<IBuffer> colour;
		texture maps[5] = {};
		uint32_t gens[5] = {};
		bool mapsKnown = false;
		uint64_t checked = ~0ull;
	};
	std::map<std::pair<texture, std::array<float, 3>>, TextureBinding> textureBindings;
	// a material binding's maps (the override pack's beside its texture: <texture>_albedo,
	// _normal, _orm, _emissive, _height; .png, .tga, .dds or .ktx2) and its colour buffer; the
	// maps' files are loaded here (the preload does it for every material: at the first draw a
	// HD model's four 4096 x 4096 maps were read and decoded then, a stall of up to 1.4 s)
	void resolveMaps(TextureBinding& e, texture tex, const Material& mat, IRenderDevice* device, Scene* scene) {
		e.mapsKnown = true;
		const std::string& filename = mat.textureFilename;
		const float colour[4] = { mat.colour[0], mat.colour[1], mat.colour[2], 1.0f };
		BufferDesc bd;
		bd.Name = "Material colour";
		bd.Size = sizeof(colour);
		bd.BindFlags = BIND_UNIFORM_BUFFER;
		bd.Usage = USAGE_IMMUTABLE;
		BufferData init{ colour, sizeof(colour) };
		device->CreateBuffer(bd, &init, &e.colour);
		auto overrideMap = [&](const char* suffix) -> texture {
			const std::string base = filename.substr(0, filename.find_last_of('.'));
			for (const char* ext : { ".png", ".tga", ".dds", ".ktx2" }) {
				const std::string name = base + suffix + ext;
				if (FileExists((scene->texCache.directory + name).c_str()))
					return scene->texCache.getTexture(name.c_str());
			}
			return nullptr;
		};
		texture albedo = overrideMap("_albedo");
		e.maps[0] = albedo ? albedo : tex;
		e.maps[1] = overrideMap("_normal");
		e.maps[2] = overrideMap("_orm");
		e.maps[3] = overrideMap("_emissive");
		e.maps[4] = overrideMap("_height");
		anyEmissive |= e.maps[3] != nullptr;
	}
	texture deriveNormal = nullptr, defaultOrm = nullptr, noEmission = nullptr, noHeight = nullptr; // the override maps' defaults

	// Ultra GI (RenderSettings::dynamicGI 3): the light of the emissive maps from where their
	// glowing texels sit on the mesh, instead of the round pool per building. Per model and
	// material, worked out once (emittersOf): the glowing texels sampled over the triangles
	// (about one sample per 2 x 2 texels), gathered into lights by place and the way they face
	// (cells of 1.5 units, wider until at most MAX_EMITTERS remain): a lit window becomes one
	// light just in front of it, facing out, a fire one all round. Their light: the always-on
	// part and the night part (the texels' alpha). How bright a light is follows the emissive
	// texture's brightness there (the mean of its glowing texels, times EMITTER_STRENGTH, at the
	// source), how far it reaches the size of the glowing area (EMITTER_REACH x the square root
	// of its area, 3-24 units): a small dim window lights the ground below it, a big bright
	// forge a wide patch, both as bright near the source as their texels are.
	struct Emitter {
		Vector3 pos, facing; // the model's space
		float directional = 0; // 1: all of it faces one way (a window), 0: all round
		float always[3] = {}, night[3] = {}; // at the source
		float radius = 3; // its reach (from its area)
		float size2 = 1; // its core (softening): a third of the reach, squared
	};
	static constexpr size_t MAX_EMITTERS = 24;
	static constexpr float EMITTER_STRENGTH = 6.0f, EMITTER_REACH = 12.0f; // the user's pick at night (4 Peaks town)
	// the emissive maps' texels (0-1 as the shaders take them, at most 256 x 256), by file
	struct EmissiveTexels { int w = 0, h = 0; std::vector<float> rgba; };
	std::map<std::string, EmissiveTexels> emissiveTexels;
	std::map<std::pair<StaticModel*, uint32_t>, std::vector<Emitter>> emitters;
	// the emissive map's file beside the material's texture (as resolveMaps finds it), or ""
	static std::string emissivePath(const Material& mat, Scene* scene) {
		const std::string& filename = mat.textureFilename;
		const std::string base = filename.substr(0, filename.find_last_of('.'));
		for (const char* ext : { ".png", ".tga", ".dds", ".ktx2" }) {
			const std::string path = scene->texCache.directory + base + "_emissive" + ext;
			if (FileExists(path.c_str()))
				return path;
		}
		return {};
	}
	const EmissiveTexels& texelsOf(const std::string& path) {
		auto it = emissiveTexels.find(path);
		if (it != emissiveTexels.end())
			return it->second;
		EmissiveTexels& t = emissiveTexels[path];
		Bitmap bm = Bitmap::loadBitmap(path.c_str());
		if (bm.format != BMFORMAT_R8G8B8A8)
			bm = bm.convertToR8G8B8A8();
		if (bm.width <= 0 || bm.height <= 0 || bm.format != BMFORMAT_R8G8B8A8)
			return t;
		// boxed down to at most 256 on a side
		int step = 1;
		while (bm.width / step > 256 || bm.height / step > 256)
			step *= 2;
		t.w = std::max(1, bm.width / step);
		t.h = std::max(1, bm.height / step);
		t.rgba.assign((size_t)t.w * t.h * 4, 0.0f);
		const float norm = 1.0f / (255.0f * step * step);
		for (int y = 0; y < t.h * step && y < bm.height; y++)
			for (int x = 0; x < t.w * step && x < bm.width; x++) {
				const uint8_t* px = bm.pixels.data() + ((size_t)y * bm.width + x) * 4;
				float* out = &t.rgba[((size_t)(y / step) * t.w + x / step) * 4];
				for (int c = 0; c < 4; c++)
					out[c] += px[c] * norm;
			}
		return t;
	}
	const std::vector<Emitter>& emittersOf(StaticModel* model, uint32_t matId, const Material& mat, Scene* scene) {
		const auto key = std::make_pair(model, matId);
		auto found = emitters.find(key);
		if (found != emitters.end())
			return found->second;
		std::vector<Emitter>& out = emitters[key];
		const std::string path = emissivePath(mat, scene);
		if (path.empty())
			return out;
		const EmissiveTexels& tx = texelsOf(path);
		model->prepare();
		const Mesh& mesh = model->mesh;
		if (tx.w == 0 || mesh.uvLists.size() == 0 || mesh.polyLists.size() == 0)
			return out;
		struct Sample { Vector3 p, n; float always[3], night[3]; float area, lum; };
		std::vector<Sample> samples;
		const auto& uvs = mesh.uvLists[0];
		const PolygonList& polylist = mesh.polyLists[0];
		auto normalOf = [&](const IndexTuple& t) {
			return t.normal < mesh.exactNormals.size() ? mesh.exactNormals[t.normal] : Mesh::s_normalTable[mesh.normals[t.normal]];
		};
		for (size_t g = 0; g < polylist.groups.size() && g < mesh.groupIndices.size(); g++) {
			if (model->matIds[g] != matId)
				continue;
			const auto& tuples = mesh.groupIndices[g];
			for (const auto& tri : polylist.groups[g].tupleIndex) {
				Vector3 v[3], n(0, 0, 0);
				float u[3], w[3];
				for (int k = 0; k < 3; k++) {
					const IndexTuple& t = tuples[tri[k]];
					v[k] = *(const Vector3*)(mesh.vertices.data() + 3 * t.vertex);
					n += normalOf(t);
					u[k] = uvs[2 * t.uv];
					w[k] = uvs[2 * t.uv + 1];
				}
				const float area = 0.5f * (v[1] - v[0]).cross(v[2] - v[0]).len3();
				if (!(area > 1e-6f))
					continue;
				n = n.normal();
				const float uvArea = 0.5f * std::abs((u[1] - u[0]) * (w[2] - w[0]) - (u[2] - u[0]) * (w[1] - w[0])) * tx.w * tx.h;
				// a lattice inside the triangle: n (n + 1) / 2 points, about one per 2 x 2 texels
				const int steps = std::clamp((int)std::ceil(std::sqrt(uvArea * 0.5f)), 1, 64);
				const float each = area / (steps * (steps + 1) * 0.5f);
				for (int i = 0; i < steps; i++)
					for (int j = 0; j < steps - i; j++) {
						const float b1 = (i + 1.0f / 3) / steps, b2 = (j + 1.0f / 3) / steps, b0 = 1 - b1 - b2;
						const float su = b0 * u[0] + b1 * u[1] + b2 * u[2], sv = b0 * w[0] + b1 * w[1] + b2 * w[2];
						int x = (int)std::floor((su - std::floor(su)) * tx.w), y = (int)std::floor((sv - std::floor(sv)) * tx.h);
						x = std::clamp(x, 0, tx.w - 1);
						y = std::clamp(y, 0, tx.h - 1);
						const float* e = &tx.rgba[((size_t)y * tx.w + x) * 4];
						const float lum = 0.299f * e[0] + 0.587f * e[1] + 0.114f * e[2];
						if (lum < 0.03f)
							continue;
						Sample s;
						s.p = v[0] * b0 + v[1] * b1 + v[2] * b2;
						s.n = n;
						for (int c = 0; c < 3; c++) {
							s.always[c] = e[c] * (1 - e[3]) * each;
							s.night[c] = e[c] * e[3] * each;
						}
						s.area = each;
						s.lum = lum * each;
						samples.push_back(s);
					}
			}
		}
		if (samples.empty())
			return out;
		// gathered into cells by place and by the axis they face most along
		struct Acc { Vector3 p, n; float always[3] = {}, night[3] = {}; float area = 0, lum = 0; };
		std::map<std::tuple<int, int, int, int>, Acc> cells;
		for (float cell = 1.5f;; cell *= 1.6f) {
			cells.clear();
			for (const Sample& s : samples) {
				const float ax = std::abs(s.n.x), ay = std::abs(s.n.y), az = std::abs(s.n.z);
				const int facing = ax >= ay && ax >= az ? (s.n.x > 0 ? 0 : 1) : ay >= az ? (s.n.y > 0 ? 2 : 3) : (s.n.z > 0 ? 4 : 5);
				Acc& a = cells[{ (int)std::floor(s.p.x / cell), (int)std::floor(s.p.y / cell), (int)std::floor(s.p.z / cell), facing }];
				a.p += s.p * s.lum;
				a.n += s.n * s.lum;
				for (int c = 0; c < 3; c++) {
					a.always[c] += s.always[c];
					a.night[c] += s.night[c];
				}
				a.area += s.area;
				a.lum += s.lum;
			}
			if (cells.size() <= MAX_EMITTERS || cell > 64)
				break;
		}
		for (const auto& [k, a] : cells) {
			Emitter e;
			const Vector3 n = a.n * (1.0f / a.lum);
			e.directional = std::min(n.len3(), 1.0f);
			e.facing = n.normal();
			e.pos = a.p * (1.0f / a.lum) + e.facing * 0.3f; // just in front of the glowing surface
			// the texels' mean brightness (the sums are over the area), at the source
			e.radius = std::clamp(EMITTER_REACH * std::sqrt(a.area), 3.0f, 24.0f);
			e.size2 = (e.radius / 3) * (e.radius / 3);
			for (int c = 0; c < 3; c++) {
				e.always[c] = a.always[c] / a.area * EMITTER_STRENGTH;
				e.night[c] = a.night[c] / a.area * EMITTER_STRENGTH;
			}
			out.push_back(e);
		}
		return out;
	}
	// This frame's lights of the emissive maps around (ShadowEnvironment::giLights): every
	// drawn entity's emitters over the bounce map's square, the brightest first, binned into
	// giLightGrid by the square their radius covers
	std::vector<float> uploadedLights;
	std::vector<uint32_t> uploadedGrid;
	void placeEmitterLights(Scene* scene, DiligentGfx::ShadowEnvironment& shadows, IDeviceContext* ctx) {
		using Env = DiligentGfx::ShadowEnvironment;
		const float night = g_environment.active ? 1.0f - g_environment.sun.daylight : 0.0f;
		const float strength = std::clamp(g_renderSettings.emissiveLightStrength, 0.0f, 3.0f); // the window light setting
		const float ox = shadows.giMapOrigin[0], oz = shadows.giMapOrigin[1], ext = Env::GI_EXTENT;
		struct Light { float v[12]; float lum; };
		std::vector<Light> lights;
		for (const auto& it : scene->matInsts) {
			if (it.second.list.empty())
				continue;
			StaticModel* model = it.first.second->getStaticModel();
			const std::vector<Emitter>& list = emittersOf(model, it.first.first, scene->modelCache->getMaterial(it.first.first), scene);
			if (list.empty())
				continue;
			for (SceneEntity* ent : it.second.list) {
				const Matrix& m = ent->transform;
				if (m._41 < ox - 32 || m._41 > ox + ext + 32 || m._43 < oz - 32 || m._43 > oz + ext + 32)
					continue;
				for (const Emitter& e : list) {
					Light l;
					float lum = 0;
					for (int c = 0; c < 3; c++) {
						// (times its core: the shaders divide by distance squared plus it)
						l.v[4 + c] = (e.always[c] + e.night[c] * night) * e.size2 * strength;
						lum += l.v[4 + c] * (c == 0 ? 0.299f : c == 1 ? 0.587f : 0.114f);
					}
					if (lum < 1e-3f)
						continue;
					const Vector3 p = e.pos.transform(m), f = e.facing.transformNormal(m).normal();
					l.v[0] = p.x; l.v[1] = p.y; l.v[2] = p.z;
					l.v[3] = e.radius;
					l.v[7] = e.size2;
					l.v[8] = f.x; l.v[9] = f.y; l.v[10] = f.z; l.v[11] = e.directional;
					l.lum = lum;
					lights.push_back(l);
				}
			}
		}
		std::sort(lights.begin(), lights.end(), [](const Light& a, const Light& b) { return a.lum > b.lum; });
		if (lights.size() > (size_t)Env::MAX_GI_LIGHTS)
			lights.resize(Env::MAX_GI_LIGHTS);
		std::vector<float> data(lights.size() * 12);
		std::vector<uint32_t> grid((size_t)Env::GI_LIGHT_CELLS * Env::GI_LIGHT_CELLS * Env::GI_CELL_SLOTS, 0);
		const float cellSize = ext / Env::GI_LIGHT_CELLS;
		for (size_t i = 0; i < lights.size(); i++) {
			const Light& l = lights[i];
			std::copy_n(l.v, 12, &data[i * 12]);
			const int x0 = std::max(0, (int)std::floor((l.v[0] - l.v[3] - ox) / cellSize)), x1 = std::min(Env::GI_LIGHT_CELLS - 1, (int)std::floor((l.v[0] + l.v[3] - ox) / cellSize));
			const int z0 = std::max(0, (int)std::floor((l.v[2] - l.v[3] - oz) / cellSize)), z1 = std::min(Env::GI_LIGHT_CELLS - 1, (int)std::floor((l.v[2] + l.v[3] - oz) / cellSize));
			for (int z = z0; z <= z1; z++)
				for (int x = x0; x <= x1; x++) {
					uint32_t* cell = &grid[((size_t)z * Env::GI_LIGHT_CELLS + x) * Env::GI_CELL_SLOTS];
					if (cell[0] < (uint32_t)Env::GI_CELL_SLOTS - 1)
						cell[1 + cell[0]++] = (uint32_t)i;
				}
		}
		// uploaded only when they changed (the buildings' lights stay put: most frames none)
		if (data != uploadedLights) {
			if (!data.empty())
				ctx->UpdateBuffer(shadows.giLights, 0, data.size() * 4, data.data(), RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
			uploadedLights = data;
		}
		if (grid != uploadedGrid) {
			ctx->UpdateBuffer(shadows.giLightGrid, 0, grid.size() * 4, grid.data(), RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
			uploadedGrid = std::move(grid);
		}
		shadows.giLightCount = (int)lights.size();
		g_giEmitterLights = shadows.giLightCount;
	}
	texture standIn = nullptr, standInClear = nullptr;
	RefCntAutoPtr<IShaderResourceBinding> envBinding;
	ITextureView* envCloudMap = nullptr;
	ITextureView* occHeights = nullptr; // the terrain heights bound to the sky light's pass
	RefCntAutoPtr<IBuffer> sceneConstBuffer, sunConstBuffer, motionConstBuffer;
	// the motion vectors' camera (unjittered view-projection) and the frame it was drawn in
	Matrix motionViewProj;
	uint64_t motionFrame = ~0ull;
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
	cbDesc.Size = 672;
	device->CreateBuffer(cbDesc, nullptr, &_impl->sunConstBuffer);
	cbDesc.Name = "Scene motion";
	cbDesc.Size = 128;
	device->CreateBuffer(cbDesc, nullptr, &_impl->motionConstBuffer);


	const PipelineResourceDesc resources[] = {
		{ SHADER_TYPE_VERTEX, "TransformBuffer", 1, SHADER_RESOURCE_TYPE_CONSTANT_BUFFER, SHADER_RESOURCE_VARIABLE_TYPE_STATIC },
		{ SHADER_TYPE_VERTEX | SHADER_TYPE_PIXEL, "FogBuffer", 1, SHADER_RESOURCE_TYPE_CONSTANT_BUFFER, SHADER_RESOURCE_VARIABLE_TYPE_STATIC },
		{ SHADER_TYPE_VERTEX | SHADER_TYPE_PIXEL, "SunBuffer", 1, SHADER_RESOURCE_TYPE_CONSTANT_BUFFER, SHADER_RESOURCE_VARIABLE_TYPE_STATIC },
		{ SHADER_TYPE_VERTEX, "SceneBuffer", 1, SHADER_RESOURCE_TYPE_CONSTANT_BUFFER, SHADER_RESOURCE_VARIABLE_TYPE_STATIC },
		{ SHADER_TYPE_VERTEX, "MotionBuffer", 1, SHADER_RESOURCE_TYPE_CONSTANT_BUFFER, SHADER_RESOURCE_VARIABLE_TYPE_STATIC },
		{ SHADER_TYPE_PIXEL, "inpTexture", 1, SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
		{ SHADER_TYPE_PIXEL, "normalMap", 1, SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
		{ SHADER_TYPE_PIXEL, "ormMap", 1, SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
		{ SHADER_TYPE_PIXEL, "emissiveMap", 1, SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
		{ SHADER_TYPE_PIXEL, "heightMap", 1, SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
		{ SHADER_TYPE_PIXEL, "MaterialBuffer", 1, SHADER_RESOURCE_TYPE_CONSTANT_BUFFER, SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
	};
	SamplerDesc sampler; // D3D11's default sampler: trilinear, clamped
	sampler.AddressU = sampler.AddressV = sampler.AddressW = TEXTURE_ADDRESS_CLAMP;
	if (g_renderSettings.anisotropy > 1) { // RenderSettings::anisotropy
		sampler.MinFilter = sampler.MagFilter = sampler.MipFilter = FILTER_TYPE_ANISOTROPIC;
		sampler.MaxAnisotropy = std::clamp(g_renderSettings.anisotropy, 1, 16);
	}
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
	_impl->signature->GetStaticVariableByName(SHADER_TYPE_PIXEL, "SunBuffer")->Set(_impl->sunConstBuffer);
	_impl->signature->GetStaticVariableByName(SHADER_TYPE_VERTEX, "SceneBuffer")->Set(_impl->sceneConstBuffer);
	_impl->signature->GetStaticVariableByName(SHADER_TYPE_VERTEX, "MotionBuffer")->Set(_impl->motionConstBuffer);

	const PipelineResourceDesc envResources[] = {
		{ SHADER_TYPE_VERTEX, "cloudTexture", 1, SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
		// the dynamic clouds and the sky light (DiligentClouds)
		{ SHADER_TYPE_VERTEX | SHADER_TYPE_PIXEL, "CloudBuffer", 1, SHADER_RESOURCE_TYPE_CONSTANT_BUFFER, SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
		{ SHADER_TYPE_VERTEX | SHADER_TYPE_PIXEL, "cloudNoise", 1, SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
		{ SHADER_TYPE_VERTEX | SHADER_TYPE_PIXEL, "skyOccTexture", 1, SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
		{ SHADER_TYPE_VERTEX | SHADER_TYPE_PIXEL, "giTexture", 1, SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
		{ SHADER_TYPE_VERTEX | SHADER_TYPE_PIXEL, "giEmitTexture", 1, SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
		{ SHADER_TYPE_VERTEX | SHADER_TYPE_PIXEL, "giLights", 1, SHADER_RESOURCE_TYPE_BUFFER_SRV, SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
		{ SHADER_TYPE_VERTEX | SHADER_TYPE_PIXEL, "giLightGrid", 1, SHADER_RESOURCE_TYPE_BUFFER_SRV, SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
		// the depth pre-pass's masks (DiligentGfx::FinishLightingPrepass), the frame's
		{ SHADER_TYPE_PIXEL, "lightRtMask", 1, SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC },
		{ SHADER_TYPE_PIXEL, "lightAoMask", 1, SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC },
	};
	SamplerDesc cloudSampler; // the cloud map tiles the map
	cloudSampler.AddressU = cloudSampler.AddressV = cloudSampler.AddressW = TEXTURE_ADDRESS_WRAP;
	SamplerDesc occSampler;
	occSampler.AddressU = occSampler.AddressV = occSampler.AddressW = TEXTURE_ADDRESS_CLAMP;
	const ImmutableSamplerDesc envSamplers[] = { { SHADER_TYPE_VERTEX, "cloudSampler", cloudSampler },
		{ SHADER_TYPE_VERTEX | SHADER_TYPE_PIXEL, "cloudNoiseSampler", cloudSampler }, { SHADER_TYPE_VERTEX | SHADER_TYPE_PIXEL, "skyOccSampler", occSampler },
		{ SHADER_TYPE_PIXEL, "lightMaskSampler", occSampler } };
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
		{ SHADER_TYPE_VERTEX, "AnimKeys", 1, SHADER_RESOURCE_TYPE_BUFFER_SRV, SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
		{ SHADER_TYPE_VERTEX, "AnimBind", 1, SHADER_RESOURCE_TYPE_BUFFER_SRV, SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
		{ SHADER_TYPE_VERTEX, "NormalTable", 1, SHADER_RESOURCE_TYPE_BUFFER_SRV, SHADER_RESOURCE_VARIABLE_TYPE_STATIC },
	};
	PipelineResourceSignatureDesc animSigDesc;
	animSigDesc.Name = "Enhanced scene animation";
	animSigDesc.Resources = animResources;
	animSigDesc.NumResources = (Uint32)std::size(animResources);
	animSigDesc.BindingIndex = 2;
	device->CreatePipelineResourceSignature(animSigDesc, &_impl->animSignature);
	if (!_impl->animSignature)
		ferr("Diligent: could not create the enhanced scene animation signature.");
	{
		// the original's 256 normals, for the animations' normal key frames
		std::vector<float> table;
		for (const Vector3& n : Mesh::s_normalTable)
			table.insert(table.end(), { n.x, n.y, n.z });
		_impl->normalTable = CreateStructuredBuffer(device, "Normal table", table.data(), table.size() * 4);
		_impl->animSignature->GetStaticVariableByName(SHADER_TYPE_VERTEX, "NormalTable")->Set(_impl->normalTable->GetDefaultView(BUFFER_VIEW_SHADER_RESOURCE));
	}

	const std::string source = std::string(g_cloudShaderCode) + esShaderCode;
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
			ferr("Diligent: failed to compile enhanced scene shader %s", entryPoint);
		return shader;
	};
	RefCntAutoPtr<IShader> vertexShaders[2] = { createShader(SHADER_TYPE_VERTEX, "VS"), createShader(SHADER_TYPE_VERTEX, "VS_Anim") };
	RefCntAutoPtr<IShader> pixelShaders[2] = { createShader(SHADER_TYPE_PIXEL, "PS"), createShader(SHADER_TYPE_PIXEL, "PS_Alpha") };
	RefCntAutoPtr<IShader> shadowAlphaShader = createShader(SHADER_TYPE_PIXEL, "PS_ShadowAlpha");
	RefCntAutoPtr<IShader> depthAlphaShader = createShader(SHADER_TYPE_PIXEL, "PS_DepthAlpha");

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
	layouts[1] = layouts[0]; // animated: the same, the rest pose for what the animation doesn't move

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
				if (!shadow) {
					// the depth pre-pass's: into the lighting depth alone, the same vertex shader and
					// alpha test (the same depth as the shading's)
					ci.PSODesc.Name = "Enhanced scene depth pre-pass";
					gp.NumRenderTargets = 0;
					gp.RTVFormats[0] = TEX_FORMAT_UNKNOWN;
					ci.pPS = alpha ? depthAlphaShader.RawPtr() : nullptr;
					device->CreateGraphicsPipelineState(ci, &_impl->depthPipelines[anim][alpha]);
					if (!_impl->depthPipelines[anim][alpha])
						ferr("Diligent: could not create the enhanced scene depth pre-pass pipeline.");
				}
			}
		}
	}

	// The dynamic GI's capture (DiligentGI): the models from straight above into an RGBA16F
	// target (albedo, n.l) over its depth; both faces (roofs, open meshes). And the light they
	// give off: added up into another (no depth)
	{
		RefCntAutoPtr<IShader> giShaders[2][2] = {
			{ createShader(SHADER_TYPE_PIXEL, "PS_GI"), createShader(SHADER_TYPE_PIXEL, "PS_GIAlpha") },
			{ createShader(SHADER_TYPE_PIXEL, "PS_GIEmit"), createShader(SHADER_TYPE_PIXEL, "PS_GIEmitAlpha") } };
		for (int emit = 0; emit < 2; emit++)
			for (int anim = 0; anim < 2; anim++)
				for (int alpha = 0; alpha < 2; alpha++) {
					GraphicsPipelineStateCreateInfo ci;
					ci.PSODesc.Name = emit ? "Enhanced scene GI emission" : "Enhanced scene GI capture";
					ci.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;
					auto& gp = ci.GraphicsPipeline;
					gp.NumRenderTargets = 1;
					gp.RTVFormats[0] = TEX_FORMAT_RGBA16_FLOAT;
					gp.DSVFormat = emit ? TEX_FORMAT_UNKNOWN : TEX_FORMAT_D32_FLOAT;
					gp.RasterizerDesc.CullMode = CULL_MODE_NONE;
					gp.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
					gp.DepthStencilDesc.DepthEnable = emit ? False : True;
					gp.DepthStencilDesc.DepthWriteEnable = emit ? False : True;
					gp.DepthStencilDesc.DepthFunc = COMPARISON_FUNC_LESS_EQUAL;
					auto& blend = gp.BlendDesc.RenderTargets[0];
					blend.BlendEnable = emit ? True : False;
					blend.SrcBlend = blend.DestBlend = BLEND_FACTOR_ONE;
					blend.SrcBlendAlpha = blend.DestBlendAlpha = BLEND_FACTOR_ONE;
					blend.BlendOp = blend.BlendOpAlpha = BLEND_OPERATION_ADD;
					gp.InputLayout.LayoutElements = layouts[anim].data();
					gp.InputLayout.NumElements = (Uint32)layouts[anim].size();
					ci.pVS = vertexShaders[anim];
					ci.pPS = giShaders[emit][alpha];
					IPipelineResourceSignature* signatures[] = { _impl->signature, _impl->envSignature, _impl->animSignature };
					ci.ppResourceSignatures = signatures;
					ci.ResourceSignaturesCount = anim ? 3 : 2;
					auto& out = emit ? _impl->giEmitPipelines[anim][alpha] : _impl->giPipelines[anim][alpha];
					device->CreateGraphicsPipelineState(ci, &out);
					if (!out)
						ferr("Diligent: could not create the enhanced scene GI capture pipeline.");
				}
	}

	// FSR 3.1's motion vectors (MotionInstance: an instance now, then in the last frame), into
	// an RG16F target over the scene's depth: tested, not written, pulled a little towards the
	// camera so that the same surface passes
	RefCntAutoPtr<IShader> motionVertexShaders[2] = { createShader(SHADER_TYPE_VERTEX, "VS_Motion"), createShader(SHADER_TYPE_VERTEX, "VS_AnimMotion") };
	RefCntAutoPtr<IShader> motionPixelShaders[2] = { createShader(SHADER_TYPE_PIXEL, "PS_Motion"), createShader(SHADER_TYPE_PIXEL, "PS_MotionAlpha") };
	auto motionElements = [](std::vector<LayoutElement>& layout, Uint32 firstInput, Uint32 slot) {
		for (Uint32 then = 0; then < 2; then++) {
			const Uint32 base = then * (Uint32)sizeof(Instance);
			for (Uint32 i = 0; i < 4; i++)
				layout.push_back(LayoutElement{ firstInput + then * 5 + i, slot, 4, VT_FLOAT32, False, base + i * 16, sizeof(MotionInstance), INPUT_ELEMENT_FREQUENCY_PER_INSTANCE, 1 });
			layout.push_back(LayoutElement{ firstInput + then * 5 + 4, slot, 4, VT_UINT32, False, base + 64, sizeof(MotionInstance), INPUT_ELEMENT_FREQUENCY_PER_INSTANCE, 1 });
		}
	};
	std::vector<LayoutElement> motionLayouts[2];
	motionLayouts[0] = { LayoutElement{0, 0, 3, VT_FLOAT32, False}, LayoutElement{1, 1, 1, VT_UINT32, False}, LayoutElement{2, 2, 2, VT_FLOAT32, False} };
	motionElements(motionLayouts[0], 3, 3);
	motionLayouts[1] = motionLayouts[0];
	for (int anim = 0; anim < 2; anim++) {
		for (int alpha = 0; alpha < 2; alpha++) {
			GraphicsPipelineStateCreateInfo ci;
			ci.PSODesc.Name = "Enhanced scene motion vectors";
			ci.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;
			auto& gp = ci.GraphicsPipeline;
			gp.NumRenderTargets = 1;
			gp.RTVFormats[0] = TEX_FORMAT_RG16_FLOAT;
			gp.DSVFormat = dgfx->swapChain->GetDesc().DepthBufferFormat;
			gp.RasterizerDesc.CullMode = CULL_MODE_BACK;
			gp.RasterizerDesc.DepthBias = -4;
			gp.RasterizerDesc.SlopeScaledDepthBias = -1.0f;
			gp.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
			gp.DepthStencilDesc.DepthEnable = True;
			gp.DepthStencilDesc.DepthWriteEnable = False;
			gp.DepthStencilDesc.DepthFunc = COMPARISON_FUNC_LESS_EQUAL;
			gp.BlendDesc.RenderTargets[0].BlendEnable = False;
			gp.InputLayout.LayoutElements = motionLayouts[anim].data();
			gp.InputLayout.NumElements = (Uint32)motionLayouts[anim].size();
			ci.pVS = motionVertexShaders[anim];
			ci.pPS = motionPixelShaders[alpha];
			IPipelineResourceSignature* signatures[] = { _impl->signature, _impl->envSignature, _impl->animSignature };
			ci.ppResourceSignatures = signatures;
			ci.ResourceSignaturesCount = anim ? 3 : 2;
			device->CreateGraphicsPipelineState(ci, &_impl->motionPipelines[anim][alpha]);
			if (!_impl->motionPipelines[anim][alpha])
				ferr("Diligent: could not create the enhanced scene motion vector pipeline.");
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

void DiligentEnhancedSceneRenderer::prewarm(const std::vector<Model*>& models)
{
	// the meshes and the animations' key frames (their buffers) made on all cores, the buffers'
	// data put in here afterwards (CreateBufferWithData)
	DiligentGfx* dgfx = (DiligentGfx*)gfx;
	IRenderDevice* device = dgfx->device;
	const auto c0 = std::chrono::steady_clock::now();
	std::vector<StaticModel*> meshes;
	std::vector<AnimatedModel*> anims;
	std::set<Model*> seen;
	for (Model* model : models) {
		// the materials' override maps loaded now (into RAM, to the GPU when drawn), not at the
		// first draw
		{
			const uint32_t* matIds = model->getMaterialIds();
			for (size_t i = 0; i < model->getNumMaterials(); i++) {
				const Material& mat = scene->modelCache->getMaterial(matIds[i]);
				const texture tex = scene->texCache.getTexture(mat.textureFilename.c_str());
				auto& e = _impl->textureBindings[{ tex, { mat.colour[0], mat.colour[1], mat.colour[2] } }];
				if (!e.mapsKnown)
					_impl->resolveMaps(e, tex, mat, device, scene);
			}
		}
		StaticModel* staticModel = model->getStaticModel();
		if (_impl->meshMap.find(staticModel) == _impl->meshMap.end() && seen.insert(staticModel).second)
			meshes.push_back(staticModel);
		if (model != staticModel) {
			auto* animModel = (AnimatedModel*)model;
			if (_impl->animMap.find(animModel) == _impl->animMap.end() && seen.insert(animModel).second)
				anims.push_back(animModel);
		}
	}
	std::vector<DilMesh> madeMeshes(meshes.size());
	std::vector<DilAnim> madeAnims(anims.size());
	std::atomic<size_t> next = 0, finished = 0;
	const size_t total = meshes.size() + anims.size();
	auto work = [&]() {
		for (size_t i; (i = next++) < total; ) {
			if (i < meshes.size())
				madeMeshes[i] = DilMesh::create(meshes[i], device);
			else
				madeAnims[i - meshes.size()] = DilAnim::create(anims[i - meshes.size()], device);
			finished++;
		}
	};
	g_deferUploads = true;
	const unsigned threads = std::clamp(std::thread::hardware_concurrency(), 1u, 16u);
	// while the game loads, the main thread draws the loading screen (its count) instead of
	// working: the cores all work
	const bool loading = LoadingActive();
	std::vector<std::thread> pool;
	for (unsigned t = loading ? 0 : 1; t < threads && t < total; t++)
		pool.emplace_back(work);
	if (loading)
		while (finished < total) {
			LoadingCount(finished, total);
			std::this_thread::sleep_for(std::chrono::milliseconds(4));
		}
	else
		work();
	for (auto& t : pool)
		t.join();
	// the animated meshes' bindings (their HD models' bindings made on the cores above)
	for (AnimatedModel* animModel : anims)
		_impl->animBind(animModel->getStaticModel(), device);
	g_deferUploads = false;
	const auto c1 = std::chrono::steady_clock::now();
	for (size_t i = 0; i < meshes.size(); i++)
		_impl->meshMap.emplace(meshes[i], std::move(madeMeshes[i]));
	for (size_t i = 0; i < anims.size(); i++)
		_impl->animMap.emplace(anims[i], std::move(madeAnims[i]));
	size_t bytes = 0;
	{
		std::lock_guard<std::mutex> lock(g_pendingMutex);
		for (PendingUpload& up : g_pendingUploads) {
			dgfx->ctx->UpdateBuffer(up.buffer, 0, up.data.size(), up.data.data(), RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
			bytes += up.data.size();
		}
		g_pendingUploads.clear();
		g_pendingUploads.shrink_to_fit();
	}
	const auto c2 = std::chrono::steady_clock::now();
	auto ms = [](auto a, auto b) { return (long long)std::chrono::duration_cast<std::chrono::milliseconds>(b - a).count(); };
	if (total)
		printf("Prewarm: %zu meshes and %zu animations made in %lld ms on %u threads, %zu MB put in in %lld ms\n",
			meshes.size(), anims.size(), ms(c0, c1), threads, bytes >> 20, ms(c1, c2));
}

void DiligentEnhancedSceneRenderer::renderDepth(ITextureView* dsv)
{
	_impl->depthTarget = dsv;
	_impl->depthOnly = true;
	render();
	_impl->depthOnly = false;
}

void DiligentEnhancedSceneRenderer::render()
{
	DiligentGfx* dgfx = (DiligentGfx*)gfx;
	IRenderDevice* device = dgfx->device;
	IDeviceContext* ctx = dgfx->ctx;
	// after the depth pre-pass (renderDepth): the frame prepared, the models only shaded
	const bool depthOnly = _impl->depthOnly, prepared = _impl->prepared;

	// Keeps the IRenderer's state the same as after the D3D11 renderer (lakes and particles
	// are drawn through IRenderer afterwards).
	gfx->BeginMeshDrawing();
	gfx->BeginBatchDrawing();

	auto& shadows = dgfx->shadows;
	// wkbai's seasons and weather, when on, drive the clouds: their preset (EnvWeather in the
	// presets' order) and the flashes, at the strikes
	if (g_environment.active && g_environment.weather >= 0) {
		shadows.cloudWeatherKind = g_environment.weather;
		shadows.cloudFlashOverride = g_environment.lightningFlash;
		shadows.cloudFlash[0] = g_environment.lightningPos.x;
		shadows.cloudFlash[1] = shadows.cloudPreset.base + std::min(shadows.cloudPreset.thickness * 0.35f, 150.0f); // in the low deck
		shadows.cloudFlash[2] = g_environment.lightningPos.z;
	}
	else {
		shadows.cloudWeatherKind = -1;
		shadows.cloudFlashOverride = -1;
	}
	// without the seasons, in a game: its sky (the game settings' cloud weather and cover)
	shadows.cloudGameWeather = g_environment.weather < 0 ? g_environment.skyWeather : -1;
	shadows.cloudGameCover = g_environment.skyCover;
	if (!shadows.portraitPass && !prepared)
		UpdateCloudBuffer(dgfx, *camera, scene->sunDirection, scene->sunColor);
	void* mapped = nullptr;
	ctx->MapBuffer(_impl->sunConstBuffer, MAP_WRITE, MAP_FLAG_DISCARD, mapped);
	memset(mapped, 0, 672);
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
	meshLighting[2] = g_renderSettings.modelLighting ? 1.0f : 0.0f;
	meshLighting[3] = g_renderSettings.modelSpecular;
	float* sky = (float*)mapped + 16;
	if (shadows.skyLightValid && g_renderSettings.skyLight) {
		for (int k = 0; k < 3; k++)
			for (int c = 0; c < 3; c++)
				sky[k * 4 + c] = shadows.skyLight[k][c];
		sky[3] = 1.0f;
		sky[7] = shadows.skyLightLuminance;
	}
	float* material = (float*)mapped + 28;
	material[0] = g_renderSettings.modelDetailNormals;
	if (g_environment.active && g_renderSettings.weatherEffects >= 1) {
		material[1] = g_environment.snowCover;
		material[2] = g_environment.wetness;
	}
	if (g_environment.active)
		material[3] = 1.0f - g_environment.sun.daylight;
	float* season = (float*)mapped + 164;
	if (g_environment.active) {
		season[0] = g_environment.autumn;
		season[1] = g_environment.dryness;
	}
	float* fire = (float*)mapped + 32;
	const std::vector<float>& fireLights = shadows.fireLights;
	const int numFires = (int)std::min(fireLights.size() / 7, (size_t)DiligentGfx::ShadowEnvironment::MAX_FIRE_LIGHTS);
	fire[0] = (float)numFires;
	for (int i = 0; i < numFires; i++) {
		std::copy_n(&fireLights[i * 7], 4, fire + 4 + i * 8);
		std::copy_n(&fireLights[i * 7 + 4], 3, fire + 8 + i * 8);
	}
	ctx->UnmapBuffer(_impl->sunConstBuffer, MAP_WRITE);

	if (!_impl->envBinding || _impl->envCloudMap != shadows.cloudMap.RawPtr()) {
		_impl->envBinding.Release();
		_impl->envSignature->CreateShaderResourceBinding(&_impl->envBinding, true);
		_impl->envCloudMap = shadows.cloudMap.RawPtr();
		_impl->envBinding->GetVariableByName(SHADER_TYPE_VERTEX, "cloudTexture")->Set(shadows.cloudMap ? shadows.cloudMap.RawPtr() : dgfx->GetTextureView(nullptr));
		for (SHADER_TYPE stage : { SHADER_TYPE_VERTEX, SHADER_TYPE_PIXEL }) {
			if (auto* v = _impl->envBinding->GetVariableByName(stage, "CloudBuffer")) v->Set(shadows.cloudBuffer);
			if (auto* v = _impl->envBinding->GetVariableByName(stage, "cloudNoise")) v->Set(shadows.cloudNoise->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
			if (auto* v = _impl->envBinding->GetVariableByName(stage, "skyOccTexture")) v->Set(shadows.skyOcc->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
			if (auto* v = _impl->envBinding->GetVariableByName(stage, "giTexture")) v->Set(shadows.giRadiance->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
			if (auto* v = _impl->envBinding->GetVariableByName(stage, "giEmitTexture")) v->Set(shadows.giEmission->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
			if (auto* v = _impl->envBinding->GetVariableByName(stage, "giLights")) v->Set(shadows.giLights->GetDefaultView(BUFFER_VIEW_SHADER_RESOURCE));
			if (auto* v = _impl->envBinding->GetVariableByName(stage, "giLightGrid")) v->Set(shadows.giLightGrid->GetDefaultView(BUFFER_VIEW_SHADER_RESOURCE));
		}
	}
	// the depth pre-pass's masks of this frame, or white (unread: FogBuffer.LightMasks is 0)
	if (auto* v = _impl->envBinding->GetVariableByName(SHADER_TYPE_PIXEL, "lightRtMask"))
		v->Set(shadows.lightRtMask ? shadows.lightRtMask : dgfx->GetTextureView(nullptr));
	if (auto* v = _impl->envBinding->GetVariableByName(SHADER_TYPE_PIXEL, "lightAoMask"))
		v->Set(shadows.lightAo ? shadows.lightAo : dgfx->GetTextureView(nullptr));

	// a material's binding: its texture, and the override pack's albedo, normal and ORM maps
	// beside it (<texture>_albedo, _normal, _orm, _emissive, .png, .tga, .dds or .ktx2) or
	// defaults: the original texture, a flat normal marked (alpha 0) for deriving one from it,
	// 1 / 0.65 / 0, no light
	auto pixel = [&](uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
		Bitmap bmp;
		bmp.width = bmp.height = 1;
		bmp.format = BMFORMAT_R8G8B8A8;
		bmp.pixels.resize(4);
		bmp.pixels[0] = r; bmp.pixels[1] = g; bmp.pixels[2] = b; bmp.pixels[3] = a;
		return gfx->CreateTexture(bmp, 1);
	};
	if (!_impl->deriveNormal) {
		_impl->deriveNormal = pixel(128, 128, 255, 0);
		_impl->defaultOrm = pixel(255, 166, 0);
		_impl->noEmission = pixel(0, 0, 0, 0);
		_impl->noHeight = pixel(255, 255, 255);   // 1x1: no relief, the shader skips the parallax
		// the stand-ins for an albedo not on the GPU yet: grey, or invisible (alpha-tested)
		_impl->standIn = pixel(128, 128, 128, 255);
		_impl->standInClear = pixel(128, 128, 128, 0);
	}
	// a material's binding: its textures (the albedo, or its _albedo override, and the override
	// maps) and its colour. Model textures are lazy (the GPU memory budget): each frame a binding
	// is used its textures are marked as drawn (and uploaded if they had been evicted); one that
	// was made again since has a new view, and the binding is made again with it.
	auto getBinding = [&](texture tex, const Material& mat) -> IShaderResourceBinding* {
		const std::string& filename = mat.textureFilename;
		auto& e = _impl->textureBindings[{ tex, { mat.colour[0], mat.colour[1], mat.colour[2] } }];
		const uint64_t now = dgfx->FrameIndex();
		if (e.srb && e.checked != now) {
			e.checked = now;
			for (int i = 0; i < 5; i++)
				if (e.maps[i] && dgfx->TouchTexture(e.maps[i]) != e.gens[i]) {
					e.srb.Release();
					break;
				}
		}
		if (!e.srb) {
			if (!e.mapsKnown)
				_impl->resolveMaps(e, tex, mat, device, scene);
			// (a texture not on the GPU yet, TouchTexture ~0: a stand-in until it is, then the binding
			// is made again; the draw never waits for an upload)
			for (int i = 0; i < 5; i++)
				e.gens[i] = e.maps[i] ? dgfx->TouchTexture(e.maps[i]) : 0;
			auto mapOr = [&](int i, texture standIn) { return e.maps[i] && e.gens[i] != ~0u ? e.maps[i] : standIn; };
			_impl->signature->CreateShaderResourceBinding(&e.srb, true);
			e.srb->GetVariableByName(SHADER_TYPE_PIXEL, "MaterialBuffer")->Set(e.colour);
			e.srb->GetVariableByName(SHADER_TYPE_PIXEL, "inpTexture")->Set(dgfx->GetTextureView(mapOr(0, mat.alphaTest ? _impl->standInClear : _impl->standIn)));
			e.srb->GetVariableByName(SHADER_TYPE_PIXEL, "normalMap")->Set(dgfx->GetTextureView(mapOr(1, _impl->deriveNormal)));
			e.srb->GetVariableByName(SHADER_TYPE_PIXEL, "ormMap")->Set(dgfx->GetTextureView(mapOr(2, _impl->defaultOrm)));
			e.srb->GetVariableByName(SHADER_TYPE_PIXEL, "emissiveMap")->Set(dgfx->GetTextureView(mapOr(3, _impl->noEmission)));
			e.srb->GetVariableByName(SHADER_TYPE_PIXEL, "heightMap")->Set(dgfx->GetTextureView(mapOr(4, _impl->noHeight)));
			e.checked = now;
		}
		return e.srb;
	};

	std::vector<Instance> instances;
	std::vector<MotionInstance> motionInstances;
	// an instance's key frames and the blend between them at an animation time
	auto keyFrames = [](Instance& inst, const DilAnim* enhAnim, AnimatedModel* animModel, uint32_t time) {
		enhAnim->instanceWords(time, inst.anim);
	};
	// Draws the scene's meshes, only the static shadow casters into the bound shadow map, or the
	// motion vectors of the meshes that moved or animated since the last frame (FSR 3.1, into the
	// bound motion vector target; the entities remember how they were drawn). Returns whether
	// anything was drawn.
	// DRAW_SKY: the static shadow casters only (not the units), for the sky light's map;
	// DRAW_GI: the dynamic GI's models from above (the static casters, with High the units too);
	// DRAW_GI_EMIT: the same, the light they give off
	// DRAW_DEPTH: the depth pre-pass's, the same meshes as DRAW_SCENE
	enum DrawMode { DRAW_SCENE, DRAW_SHADOWS, DRAW_MOTION, DRAW_SKY, DRAW_GI, DRAW_GI_EMIT, DRAW_DEPTH };
	const uint64_t frameIndex = dgfx->FrameIndex();
	auto drawMeshes = [&](DrawMode mode) {
		const bool giPass = mode == DRAW_GI || mode == DRAW_GI_EMIT;
		const bool shadowPass = mode == DRAW_SHADOWS || mode == DRAW_SKY || giPass, motionPass = mode == DRAW_MOTION;
		const uint32_t casterFlags = (mode == DRAW_SKY || (giPass && shadows.giMode < 2)) ? SceneEntity::SEFLAG_STATIC_SHADOW
			: (SceneEntity::SEFLAG_STATIC_SHADOW | SceneEntity::SEFLAG_DYNAMIC_SHADOW);
		bool drawn = false;
		for (const auto& it : scene->matInsts) {
			const Material& mat = scene->modelCache->getMaterial(it.first.first);
			const int alpha = mat.alphaTest ? 1 : 0;
			IShaderResourceBinding* srb = getBinding(it.second.tex, mat);

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
				a->bind = _impl->animBind(staticModel, device);
				a->binding->GetVariableByName(SHADER_TYPE_VERTEX, "AnimKeys")->Set(a->keys->GetDefaultView(BUFFER_VIEW_SHADER_RESOURCE));
				a->binding->GetVariableByName(SHADER_TYPE_VERTEX, "AnimBind")->Set(a->bind->GetDefaultView(BUFFER_VIEW_SHADER_RESOURCE));
			}

			// one draw per team colour (the UV set): the instances keep their own key frame and flags
			int prevColor = -1;
			instances.clear();
			motionInstances.clear();

			auto flush = [&]() {
				const size_t count = motionPass ? motionInstances.size() : instances.size();
				if (count == 0) return;
				uint64_t instanceOffset = 0;
				IBuffer* instanceBuffer = motionPass
					? dgfx->StreamVertexData(motionInstances.data(), count * sizeof(MotionInstance), instanceOffset)
					: dgfx->StreamVertexData(instances.data(), count * sizeof(Instance), instanceOffset);

				ctx->SetPipelineState((mode == DRAW_GI_EMIT ? _impl->giEmitPipelines : giPass ? _impl->giPipelines : shadowPass ? _impl->shadowPipelines
					: motionPass ? _impl->motionPipelines : mode == DRAW_DEPTH ? _impl->depthPipelines : _impl->pipelines)[enhAnim ? 1 : 0][alpha]);
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
							IBuffer* buffers[4] = { enhMesh->vertices, enhMesh->normals, enhMesh->texcoords[prevColor], instanceBuffer };
							const Uint64 offsets[4] = { (Uint64)startVertex * 12, (Uint64)startVertex * 4, (Uint64)startVertex * 8, instanceOffset };
							ctx->SetVertexBuffers(0, 4, buffers, offsets, RESOURCE_STATE_TRANSITION_MODE_TRANSITION, SET_VERTEX_BUFFERS_FLAG_RESET);
							ctx->DrawIndexed(DrawIndexedAttribs{ numIndices, VT_UINT16, DRAW_FLAG_VERIFY_ALL, (Uint32)count, startIndex, 0 });
						}
						else
							ctx->DrawIndexed(DrawIndexedAttribs{ numIndices, VT_UINT16, DRAW_FLAG_VERIFY_ALL, (Uint32)count, startIndex, startVertex });
					}
					startIndex += numIndices;
					startVertex += numVertices;
				}

				instances.clear();
				motionInstances.clear();
			};

			const bool animated = enhAnim && enhAnim->animTimes.size() >= 2;
			// a tree bare of leaves (winter, burnt; wkbai's seasons): only its bark drawn; a model
			// without bark (a bush) not at all once burnt, as it is in winter
			auto isBark = [](const std::string& f) {
				std::string l = f;
				for (char& c : l) c = (char)tolower((unsigned char)c);
				return l.find("bark") != std::string::npos;
			};
			const bool barkMaterial = isBark(mat.textureFilename);
			bool modelHasBark = false;
			if (!barkMaterial) {
				auto bt = _impl->hasBark.find(staticModel);
				if (bt == _impl->hasBark.end()) {
					bool any = false;
					for (uint32_t id : staticModel->matIds)
						any |= isBark(scene->modelCache->getMaterial(id).textureFilename);
					bt = _impl->hasBark.emplace(staticModel, any).first;
				}
				modelHasBark = bt->second;
			}
			for (SceneEntity* ent : it.second.list) {
				if (shadowPass && !(ent->flags & casterFlags))
					continue;
				if ((ent->flags & SceneEntity::SEFLAG_BARE) && !barkMaterial && (modelHasBark || (ent->flags & SceneEntity::SEFLAG_CHARRED)))
					continue;
				if (motionPass) {
					// the first of the entity's materials in this frame: what was drawn becomes the
					// last frame's
					if (ent->drawnFrame != frameIndex) {
						ent->previousValid = ent->drawnFrame + 1 == frameIndex;
						ent->previousTransform = ent->drawnTransform;
						ent->previousAnimTime = ent->drawnAnimTime;
						ent->drawnTransform = ent->transform;
						ent->drawnAnimTime = ent->animTime;
						ent->drawnFrame = frameIndex;
					}
					// still: the camera's motion covers it
					const bool moved = memcmp(&ent->transform, &ent->previousTransform, sizeof(Matrix)) != 0
						|| (animated && ent->animTime != ent->previousAnimTime);
					if (!ent->previousValid || !moved)
						continue;
				}
				Instance inst;
				inst.transform = ent->transform.getTranspose();
				inst.flags = ent->flags;
				if (animated)
					keyFrames(inst, enhAnim, (AnimatedModel*)model, ent->animTime);
				const int actualColor = ent->color % enhMesh->texcoords.size();
				if ((motionPass ? motionInstances.size() : instances.size()) >= _impl->MAX_INSTANCES || actualColor != prevColor)
					flush();
				prevColor = actualColor;
				if (motionPass) {
					MotionInstance mi;
					mi.current = inst;
					mi.previous = inst;
					mi.previous.transform = ent->previousTransform.getTranspose();
					if (animated)
						keyFrames(mi.previous, enhAnim, (AnimatedModel*)model, ent->previousAnimTime);
					motionInstances.push_back(mi);
				}
				else
					instances.push_back(inst);
			}
			flush();
		}
		return drawn;
	};

	// Static shadows: the casters seen from the sun, orthographic over a square around the
	// point the camera looks at, snapped to whole texels so that the edges don't shimmer.
	if (!prepared)
		shadows.staticShadowValid = false;
	if (!prepared && shadows.staticShadows && shadows.staticShadowDSV) {
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
		PROFILE_SCOPE("Shadow map");
		shadows.staticShadowValid = drawMeshes(DRAW_SHADOWS);
		shadows.staticShadowMatrix = lightMatrix;
		gfx->SetTransformMatrix(&camera->sceneMatrix);
		dgfx->RestoreRenderTargets();
	}

	// The sky light's map (RenderSettings::skyGI): the static casters from straight above over its
	// square, then per texel the horizon over them and the terrain (DiligentClouds)
	if (!prepared && shadows.skyOccOn) {
		PROFILE_SCOPE("Sky light map");
		using Env = DiligentGfx::ShadowEnvironment;
		const float half = Env::SKY_OCC_RADIUS;
		const Vector3 centre(shadows.skyOccOrigin[0] + half, 0, shadows.skyOccOrigin[1] + half);
		Matrix view = Matrix::getLHLookAtViewMatrix(Vector3(centre.x, Env::SKY_OCC_TOP, centre.z), centre, Vector3(0, 0, 1));
		Matrix ortho = Matrix::getZeroMatrix();
		ortho._11 = ortho._22 = 1.0f / half;
		ortho._33 = 1.0f / Env::SKY_OCC_RANGE;
		ortho._44 = 1.0f;
		const Matrix topDown = view * ortho;
		ITextureView* dsv = shadows.skyDepth->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL);
		ctx->SetRenderTargets(0, nullptr, dsv, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
		ctx->SetViewports(1, nullptr, 0, 0);
		ctx->ClearDepthStencil(dsv, CLEAR_DEPTH_FLAG, 1.0f, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
		gfx->SetTransformMatrix(&topDown);
		drawMeshes(DRAW_SKY);
		gfx->SetTransformMatrix(&camera->sceneMatrix);
		if (!shadows.skyOccBinding || _impl->occHeights != shadows.terrainHeights) {
			shadows.skyOccBinding.Release();
			shadows.skyOccSignature->CreateShaderResourceBinding(&shadows.skyOccBinding, true);
			shadows.skyOccBinding->GetVariableByName(SHADER_TYPE_PIXEL, "heightTexture")->Set(shadows.terrainHeights);
			_impl->occHeights = shadows.terrainHeights;
		}
		ITextureView* rtv = shadows.skyOcc->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
		ctx->SetRenderTargets(1, &rtv, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
		ctx->SetViewports(1, nullptr, 0, 0);
		ctx->SetPipelineState(shadows.skyOccPipeline);
		ctx->CommitShaderResources(shadows.skyOccBinding, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
		ctx->Draw(DrawAttribs{ 3, DRAW_FLAG_VERIFY_ALL });
		dgfx->RestoreRenderTargets();
		// its means (SkyVisibility's fade towards the edge)
		ctx->GenerateMips(shadows.skyOcc->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
	}

	// The dynamic GI (RenderSettings::dynamicGI, DiligentGI): the models from straight above over
	// the bounce map's square (as the sky light's casters: the same height and depth range), then
	// the bounce map's light from them, the terrain and the sky light's map
	if (!prepared && shadows.skyOccOn && shadows.giMode > 0 && shadows.giRefresh && !shadows.portraitPass) {
		PROFILE_SCOPE("Dynamic GI");
		using Env = DiligentGfx::ShadowEnvironment;
		const float half = Env::GI_EXTENT * 0.5f;
		const Vector3 centre(shadows.giOrigin[0] + half, 0, shadows.giOrigin[1] + half);
		Matrix view = Matrix::getLHLookAtViewMatrix(Vector3(centre.x, Env::SKY_OCC_TOP, centre.z), centre, Vector3(0, 0, 1));
		Matrix ortho = Matrix::getZeroMatrix();
		ortho._11 = ortho._22 = 1.0f / half;
		ortho._33 = 1.0f / Env::SKY_OCC_RANGE;
		ortho._44 = 1.0f;
		const Matrix topDown = view * ortho;
		ITextureView* rtv = shadows.giCapture->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
		ITextureView* emitRtv = shadows.giCaptureEmission->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
		ITextureView* dsv = shadows.giCaptureDepth->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL);
		const float none[4] = { 0, 0, 0, -1 }, dark[4] = { 0, 0, 0, 0 };
		GITimerBegin(dgfx);
		ctx->SetRenderTargets(1, &rtv, dsv, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
		ctx->SetViewports(1, nullptr, 0, 0);
		ctx->ClearRenderTarget(rtv, none, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
		ctx->ClearDepthStencil(dsv, CLEAR_DEPTH_FLAG, 1.0f, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
		gfx->SetTransformMatrix(&topDown);
		drawMeshes(DRAW_GI);
		// the light they give off (none without emissive maps: only cleared)
		ctx->SetRenderTargets(1, &emitRtv, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
		ctx->SetViewports(1, nullptr, 0, 0);
		ctx->ClearRenderTarget(emitRtv, dark, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
		if (_impl->anyEmissive)
			drawMeshes(DRAW_GI_EMIT);
		gfx->SetTransformMatrix(&camera->sceneMatrix);
		dgfx->RestoreRenderTargets();
		RenderGIRadiance(dgfx);
		GITimerEnd(dgfx);
	}
	else if (!prepared && shadows.giMode > 0)
		GITimerSkip();
	// Ultra: the emissive maps' lights where their glowing texels are (every frame: units move)
	if (!prepared && shadows.giMode >= 3 && !shadows.portraitPass && shadows.giLights) {
		PROFILE_SCOPE("Emissive lights");
		_impl->placeEmitterLights(scene, shadows, ctx);
	}

	// The ray-traced shadows' casters (and what the ray-traced reflections see): every entity
	// casting a shadow, with its mesh's structure
	// (an animated one's at its current key frame), then the frame's top-level structure
	if (!prepared && (shadows.rayShadowsOn || shadows.rayReflectionsOn)) {
		PROFILE_SCOPE("RT structures");
		if (!shadows.rayShadows)
			shadows.rayShadows = new DiligentRayShadows(device);
		if (!shadows.rayShadows->usable())
			shadows.rayShadowsSupported = false;
		else {
			DiligentRayShadows* rts = shadows.rayShadows;
			rts->beginFrame();
			std::unordered_set<SceneEntity*> added;
			for (const auto& it : scene->matInsts) {
				auto* model = it.first.second;
				auto* staticModel = model->getStaticModel();
				auto mt = _impl->meshMap.find(staticModel);
				if (mt == _impl->meshMap.end())
					continue;
				DilMesh& mesh = mt->second;
				// the alpha-tested groups (leaves): their texture as drawn and per triangle its UVs
				if (!mesh.rtAlphaResolved) {
					// (only once their textures are on the GPU: the binding's stand-in would stay in
					// the structures for good; asked for meanwhile, the mesh casts no shadow until then)
					bool ready = true;
					const Mesh& m0 = staticModel->mesh;
					for (size_t g = 0; g < mesh.rtGroups.size() && g < m0.polyLists[0].groups.size(); g++) {
						const Material& gm = scene->modelCache->getMaterial(staticModel->matIds[g]);
						if (gm.alphaTest && dgfx->TouchTexture(scene->texCache.getTexture(gm.textureFilename.c_str())) == ~0u)
							ready = false;
					}
					if (!ready)
						continue;
					mesh.rtAlphaResolved = true;
					const Mesh& m = staticModel->mesh;
					const PolygonList& pl = m.polyLists[0];
					for (size_t g = 0; g < mesh.rtGroups.size() && g < pl.groups.size() && m.uvLists.size() > 0; g++) {
						const Material& gm = scene->modelCache->getMaterial(staticModel->matIds[g]);
						if (!gm.alphaTest)
							continue;
						texture tex = scene->texCache.getTexture(gm.textureFilename.c_str());
						IShaderResourceBinding* tsrb = getBinding(tex, gm);
						auto& grp = mesh.rtGroups[g];
						grp.alphaTexture = (ITextureView*)tsrb->GetVariableByName(SHADER_TYPE_PIXEL, "inpTexture")->Get();
						const auto& uvl = m.uvLists[0];
						const auto& gi = m.groupIndices[g];
						for (const auto& tri : pl.groups[g].tupleIndex)
							for (int k = 0; k < 3; k++) {
								const IndexTuple& v = gi[tri[k]];
								grp.alphaUVs.push_back(uvl[2 * v.uv]);
								grp.alphaUVs.push_back(uvl[2 * v.uv + 1]);
							}
					}
				}
				const DilAnim* anim = nullptr;
				if (model != staticModel) {
					auto at = _impl->animMap.find((AnimatedModel*)model);
					if (at != _impl->animMap.end() && at->second.animTimes.size() >= 2 && at->second.duration > 0)
						anim = &at->second;
				}
				for (SceneEntity* ent : it.second.list) {
					if (!(ent->flags & (SceneEntity::SEFLAG_STATIC_SHADOW | SceneEntity::SEFLAG_DYNAMIC_SHADOW)) || !added.insert(ent).second)
						continue;
					IBottomLevelAS* blas = nullptr;
					if (anim) {
						// the pose's structure, cached (DiligentRayShadows drops the ones unused for a
						// while); a new one's pose worked out here, at most a few a frame. The poses
						// at eighths of the way between key frames, the nearest one: the key frame's
						// alone, while the drawn mesh blends towards the next, made a moving part's
						// shadow copy a plane at another angle, which shadowed the drawn surface in
						// a straight line where it lay above it (the HD Blacksmith's bellows board,
						// reported by the HD-models session, 2026-11-09)
						constexpr uint32_t SUBSTEPS = 8;
						const uint32_t animtime = ((int32_t)ent->animTime < 0 ? 0u : ent->animTime) % anim->duration;
						float t;
						uint32_t frame = DilAnim::keyAt(anim->animTimes, animtime, t);
						uint32_t step = (uint32_t)std::lround(t * SUBSTEPS);
						if (step >= SUBSTEPS) {
							frame++; // (the next key frame itself: its own pose)
							step = 0;
						}
						const uint32_t poseId = frame * SUBSTEPS + step;
						const uint32_t t0 = anim->animTimes[frame];
						const uint32_t poseTime = step == 0 ? t0 : t0 + (uint32_t)((uint64_t)(anim->animTimes[frame + 1] - t0) * step / SUBSTEPS);
						blas = rts->cached(anim, poseId);
						if (_impl->poseFrame != frameIndex) {
							_impl->poseFrame = frameIndex;
							_impl->posesBuilt = 0;
						}
						if (!blas && _impl->posesBuilt < 24) {
							_impl->posesBuilt++;
							auto* animModel = (AnimatedModel*)model;
							const Vector3* v = (const Vector3*)animModel->interpolate(poseTime);
							std::vector<Vector3> pose;
							pose.reserve(mesh.totVertices);
							for (auto& grp : staticModel->mesh.groupIndices)
								for (auto& tup : grp)
									pose.push_back(v[tup.vertex]);
							BufferDesc pd;
							pd.Name = "RT pose";
							pd.Size = std::max<uint64_t>(pose.size() * 12, 12);
							pd.BindFlags = BIND_RAY_TRACING;
							pd.Usage = USAGE_IMMUTABLE;
							BufferData init{ pose.data(), pose.size() * 12 };
							RefCntAutoPtr<IBuffer> poseBuffer;
							device->CreateBuffer(pd, &init, &poseBuffer);
							if (poseBuffer)
								blas = rts->blas(ctx, anim, poseId, poseBuffer, 0, mesh.totVertices, mesh.indices, mesh.rtGroups, true);
						}
						if (!blas)
							continue;
					}
					else
						blas = rts->blas(ctx, &mesh, 0, mesh.vertices, 0, mesh.totVertices, mesh.indices, mesh.rtGroups);
					rts->add(blas, ent->transform);
				}
			}
			rts->setTerrain(ctx, shadows.rayShadowsOn ? shadows.rtTerrain : nullptr, shadows.rtTerrainVersion);
			rts->build(ctx);
		}
	}

	// the depth pre-pass: the models' depth alone, into the lighting depth; the shading comes in
	// the next render()
	if (depthOnly) {
		if (_impl->depthTarget) {
			PROFILE_SCOPE("Models' depth");
			ctx->SetRenderTargets(0, nullptr, _impl->depthTarget, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
			ctx->SetViewports(1, nullptr, 0, 0);
			drawMeshes(DRAW_DEPTH);
		}
		_impl->prepared = true;
		return;
	}
	_impl->prepared = false;
	{
		PROFILE_SCOPE("Models");
		drawMeshes(DRAW_SCENE);
	}

	// FSR 3.1: the motion vectors of what moved, seen by the camera without its jitter now and
	// in the last frame (none after a frame without them: no last camera)
	if (ITextureView* motion = dgfx->MotionVectorTarget()) {
		Matrix projection = camera->projMatrix;
		if (!camera->orthoMode) {
			projection._31 -= g_projectionJitter[0];
			projection._32 -= g_projectionJitter[1];
		}
		const Matrix viewProj = camera->viewMatrix * projection;
		const bool cameraValid = _impl->motionFrame + 1 == frameIndex;
		const Matrix matrices[2] = { viewProj.getTranspose(), _impl->motionViewProj.getTranspose() };
		_impl->motionViewProj = viewProj;
		_impl->motionFrame = frameIndex;
		ctx->MapBuffer(_impl->motionConstBuffer, MAP_WRITE, MAP_FLAG_DISCARD, mapped);
		memcpy(mapped, matrices, sizeof(matrices));
		ctx->UnmapBuffer(_impl->motionConstBuffer, MAP_WRITE);
		if (cameraValid) {
			PROFILE_SCOPE("Motion vectors");
			ctx->SetRenderTargets(1, &motion, dgfx->CurrentDepthTarget(), RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
			drawMeshes(DRAW_MOTION);
			dgfx->RestoreRenderTargets();
		}
	}
}

