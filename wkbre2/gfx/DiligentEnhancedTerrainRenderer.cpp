// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

// Port of the former D3D11EnhancedTerrainRenderer to Diligent: same shader, same culling of tiles, tiles
// drawn grouped by texture from one vertex buffer of the whole map, then lakes through
// IRenderer. The packed normals/tangents are unpacked in the vertex shader.

#include "DiligentEnhancedTerrainRenderer.h"
#include <SDL3/SDL_timer.h>
#include "EnhancedTerrainData.h"
#include "renderer_diligent.h"
#include "TextureCache.h"
#include "../terrain.h"
#include "../Camera.h"
#include "../util/util.h"
#include "../file.h"
#include "../settings.h"
#include <nlohmann/json.hpp>
#include "bitmap.h"
#include "RenderSettings.h"
#include "DiligentWaterfalls.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <tuple>
#include <utility>
#include <vector>

using namespace Diligent;

static const char tshaderCode[] = R"---(
Texture2D inpTexture;
Texture2D normalTexture;
SamplerState inpSampler;
Texture2D cloudTexture;
SamplerState cloudSampler;
Texture2D<float> shadowTexture;
SamplerComparisonState shadowSampler;
Texture2D<float> blobTexture;
Texture2D flowTexture; // the water's flow per tile (BuildWaterFlow)
Texture2D riverTexture; // the rivers' coordinates, 4 texels per tile (BuildRiverField)
Texture2D<float> riverWidth; // and their half width
Texture2D sceneTexture; // the frame behind the water, for its refraction and reflections (from Low)
Texture2D<float> depthTexture; // its depth, for the reflections' rays
Texture2D skyFront; // the sky box (ShadowEnvironment::skyFaces), reflected from Low
Texture2D skyRight;
Texture2D skyBack;
Texture2D skyLeft;
Texture2D skyTop;
Texture2DArray splatDiffuse;
Texture2DArray splatNormal;
Texture2D<uint> splatCorners;
SamplerState splatSampler;

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
	matrix ShadowMatrix; // world -> static shadow map clip space
	float4 CloudParams; // UV offset, UV per world unit, on
	float4 ShadowParams; // on, light left in the shadow
	float4 LakeColour; // water surface: the map's fog colour, half transparent (as in wkbre2 before)
	float4 WaterLayers; // UV offsets of the two texture layers
	float4 WaterRipples; // UV offsets of the two ripple layers
	float4 WaterParams; // on, UV per world unit, ripple UV per world unit, refraction on
	float4 BlobParams; // on, 1 / map width, 1 / map depth, map edge in world units
	// from RenderSettings::waterQuality 2 (Low): the sky box's colour above (w: on) and at the horizon, for
	// the water's reflection (ShadowEnvironment::skyLight), else the fog colour
	float4 WaterSkyUp;
	float4 WaterSkyHorizon;
	float4 FireLightCount; // x: how many of FireLights are in use
	float4 FireLights[32]; // per light: position and radius, colour
	float4 GrassParams; // grass: on, distance, a steady clock in seconds (the waves', flow's and sway's)
	float4 WaterFlowParams; // on, speed in units per second at full flow; High water: most pieces per tile edge, distance of full detail
	float4 GrassLook; // grass: the share of the blades drawn (density / maximum), height factor
	float4 TessParams; // terrain tessellation: on, most pieces per tile edge, distance of full detail, map edge in tiles
	float4 TessGrid;   // the height grid's last indices (width, height)
	// the water's reflections (from Low): steps along the ray (0: none), the projection's z terms
	// (depth = A + B / view z), the ray's longest reach in world units
	float4 ReflectParams;
	float4 WaterMode; // refinement steps of a hit, x; y: Medium (High's look on the flat surface); z: the sky box is bound
};

Texture2D<float> heightTexture; // the terrain's vertex heights, (width + 1) x (height + 1)

// The tessellated terrain's surface: a Catmull-Rom spline through the vertex heights in both
// directions (exact at the vertices); Terrain::getSmoothHeight is the same on the CPU
float GridHeight(int2 g)
{
	return heightTexture.Load(int3(clamp(g, int2(0, 0), (int2)TessGrid.xy), 0));
}

float CatmullRom(float a, float b, float c, float d, float t)
{
	return 0.5 * (2 * b + (c - a) * t + (2 * a - 5 * b + 4 * c - d) * t * t + (3 * b - a - 3 * c + d) * t * t * t);
}

float SmoothHeight(float2 xz)
{
	float2 g = xz / 5 + TessParams.w;
	int2 i = (int2)floor(g);
	float2 f = g - i;
	float row[4];
	[unroll] for (int k = -1; k <= 2; k++)
		row[k + 1] = CatmullRom(GridHeight(i + int2(-1, k)), GridHeight(i + int2(0, k)), GridHeight(i + int2(1, k)), GridHeight(i + int2(2, k)), f.x);
	return CatmullRom(row[0], row[1], row[2], row[3], f.y);
}

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


// The unit blob shadows, drawn into a map-sized texture of the light they leave
float BlobLight(float3 pos)
{
	float light = 1;
	if (BlobParams.x != 0)
		light = blobTexture.Sample(inpSampler, (pos.xz + BlobParams.w) * BlobParams.yz);
	return light;
}

// Blob shadows: one quad per blob, from its corners p1, p2 = p1 + u, p3 = p1 + v in map space,
// into the blob texture (u = 0 at the left, v = 0 at the top). Each multiplies the light by
// 1 - its alpha, as the original's blobs alpha-blend black onto the ground.
cbuffer BlobBuffer
{
	float4 BlobScale; // 1 / map width, 1 / map depth
};

struct BLOB_OUTPUT
{
	float4 Pos : SV_POSITION;
	float2 Uv : TEXCOORD0;
};

BLOB_OUTPUT VS_Blob(float2 P1 : ATTRIB0, float2 P2 : ATTRIB1, float2 P3 : ATTRIB2, uint id : SV_VertexID)
{
	BLOB_OUTPUT output;
	output.Uv = float2(id & 1, id >> 1);
	float2 m = P1 + output.Uv.x * (P2 - P1) + output.Uv.y * (P3 - P1);
	output.Pos = float4(m.x * BlobScale.x * 2 - 1, 1 - m.y * BlobScale.y * 2, 0, 1);
	return output;
}

float4 PS_Blob(BLOB_OUTPUT input) : SV_Target
{
	return float4(0, 0, 0, inpTexture.Sample(inpSampler, input.Uv).r);
}

// The original's terrain shadows (docs/re/shadows.md): the light is scaled by the scrolling
// sky_shadow_map.pcx, and buildings with HAS_STATIC_SHADOW darken the ground under them to
// 0.706 with soft edges. wkbre2 draws the static shadows from a sun shadow map.
float CloudLight(float3 pos)
{
	float light = 1;
	if (CloudParams.w != 0)
		light = cloudTexture.Sample(cloudSampler, pos.xz * CloudParams.z + CloudParams.xy).r;
	return light;
}

// One static shadow texel of the original: in or out of shadow at its sample point, seen along
// the sun from the ground (here: the depth test against the sun shadow map).
float StaticShadowSample(float2 xz, float y)
{
	float4 sp = mul(float4(xz.x, y, xz.y, 1), ShadowMatrix);
	float2 uv = sp.xy * float2(0.5, -0.5) + 0.5;
	float lit = 1;
	if (all(uv >= 0) && all(uv <= 1) && sp.z <= 1)
		lit = shadowTexture.SampleCmpLevelZero(shadowSampler, uv, sp.z);
	return lit;
}

// The original ray-casts 4x4 points per 5-unit tile, 1 unit apart starting 1 unit in, to the
// sun against the casters' triangles, stores each as fully lit or shadowed in a texture of 4
// texels per tile (1.25 units), and draws that bilinearly filtered: the soft edge is that filter.
float StaticShadowLight(float3 pos)
{
	float light = 1;
	if (ShadowParams.x != 0) {
		// n points per tile side (ShadowParams.z: 4 as the original, more with
		// RenderSettings::shadowResolution), 5 / (n + 1) apart starting that far in
		float n = ShadowParams.z;
		float2 t = pos.xz * (n / 5) - 0.5; // texel space, texel centres at whole numbers
		float2 t0 = floor(t), f = t - t0;
		float lit[4];
		for (int i = 0; i < 4; i++) {
			float2 k = t0 + float2(i & 1, i >> 1);
			float2 tile = floor(k / n);
			lit[i] = StaticShadowSample(tile * 5 + (k - tile * n + 1) * (5 / (n + 1)), pos.y);
		}
		float l = lerp(lerp(lit[0], lit[1], f.x), lerp(lit[2], lit[3], f.x), f.y);
		light = lerp(ShadowParams.y, 1, l);
	}
	return light;
}

struct VS_OUTPUT
{
	float4 Pos : SV_POSITION;
	float3 FragPos : COLOR1;
	float3 Norm: NORMAL;
	float4 Color : COLOR0;
	float2 Texcoord : TEXCOORD0;
	float Fog : FOG;
	float3 Tangent : TANGENT0;
	float3 Bitangent : TANGENT1;
};

// R10G10B10A2_UNORM
float4 Unpack1010102(uint p)
{
	return float4(float3(p & 1023, (p >> 10) & 1023, (p >> 20) & 1023) / 1023.0, (p >> 30) / 3.0);
}

VS_OUTPUT VS(float3 Pos3 : ATTRIB0, uint NormalPacked : ATTRIB1, uint TangentPacked : ATTRIB2, uint BitangentPacked : ATTRIB3, float2 Texcoord : ATTRIB4)
{
	float4 Pos = float4(Pos3, 1);
	float4 Normal = Unpack1010102(NormalPacked);
	float3 Tangent = Unpack1010102(TangentPacked).xyz;
	float3 Bitangent = Unpack1010102(BitangentPacked).xyz;
	VS_OUTPUT output = (VS_OUTPUT)0;
	output.Pos = mul(Pos, Transform);
	output.FragPos = Pos.xyz;
	output.Norm = Normal.xyz * 2 - float3(1,1,1);
	output.Color = float4((Normal.w * 0.5 + 0.5).xxx, 1);
	output.Texcoord = Texcoord;
	output.Fog = FogAmount(Pos.xyz, distance(Pos.xyz, FogCamera.xyz));
	output.Tangent = Tangent * 2 - float3(1,1,1);
	output.Bitangent = Bitangent * 2 - float3(1,1,1);
	return output;
}

// Terrain tessellation (RenderSettings::terrainTessellation): each triangle is cut into up to
// TessParams.y pieces per edge near the camera, fewer further out (an edge's factor from its
// midpoint, so neighbours always agree and no cracks open), and every new point is put on the
// smooth surface, with the normal from its slope
VS_OUTPUT VS_Tess(float3 Pos3 : ATTRIB0, uint NormalPacked : ATTRIB1, uint TangentPacked : ATTRIB2, uint BitangentPacked : ATTRIB3, float2 Texcoord : ATTRIB4)
{
	return VS(Pos3, NormalPacked, TangentPacked, BitangentPacked, Texcoord);
}

struct TESS_FACTORS
{
	float Edges[3] : SV_TessFactor;
	float Inside : SV_InsideTessFactor;
};

float TessFactorAt(float3 p)
{
	float d = distance(p, FogCamera.xyz);
	return clamp(TessParams.y * saturate(1 - (d - TessParams.z * 0.25) / TessParams.z), 1, TessParams.y);
}

TESS_FACTORS HS_Const(InputPatch<VS_OUTPUT, 3> p)
{
	TESS_FACTORS o;
	o.Edges[0] = TessFactorAt(0.5 * (p[1].FragPos + p[2].FragPos));
	o.Edges[1] = TessFactorAt(0.5 * (p[2].FragPos + p[0].FragPos));
	o.Edges[2] = TessFactorAt(0.5 * (p[0].FragPos + p[1].FragPos));
	o.Inside = max(o.Edges[0], max(o.Edges[1], o.Edges[2]));
	return o;
}

[domain("tri")]
[partitioning("fractional_odd")]
[outputtopology("triangle_cw")]
[outputcontrolpoints(3)]
[patchconstantfunc("HS_Const")]
VS_OUTPUT HS(InputPatch<VS_OUTPUT, 3> p, uint i : SV_OutputControlPointID)
{
	return p[i];
}

[domain("tri")]
VS_OUTPUT DS(TESS_FACTORS factors, float3 b : SV_DomainLocation, const OutputPatch<VS_OUTPUT, 3> p)
{
	VS_OUTPUT o;
	float3 pos = p[0].FragPos * b.x + p[1].FragPos * b.y + p[2].FragPos * b.z;
	pos.y = SmoothHeight(pos.xz);
	const float e = 0.6;
	float3 n = normalize(float3(SmoothHeight(pos.xz - float2(e, 0)) - SmoothHeight(pos.xz + float2(e, 0)), 2 * e,
		SmoothHeight(pos.xz - float2(0, e)) - SmoothHeight(pos.xz + float2(0, e))));
	float3 t = p[0].Tangent * b.x + p[1].Tangent * b.y + p[2].Tangent * b.z;
	float3 bt = p[0].Bitangent * b.x + p[1].Bitangent * b.y + p[2].Bitangent * b.z;
	o.Pos = mul(float4(pos, 1), Transform);
	o.FragPos = pos;
	o.Norm = n;
	o.Color = p[0].Color * b.x + p[1].Color * b.y + p[2].Color * b.z;
	o.Texcoord = p[0].Texcoord * b.x + p[1].Texcoord * b.y + p[2].Texcoord * b.z;
	o.Fog = FogAmount(pos, distance(pos, FogCamera.xyz));
	o.Tangent = normalize(t - n * dot(t, n));
	o.Bitangent = normalize(bt - n * dot(bt, n));
	return o;
}

struct LAKE_OUTPUT
{
	float4 Pos : SV_POSITION;
	float3 World : COLOR1;
	float Depth : TEXCOORD0;
	float Fog : FOG;
	float3 Normal : NORMAL;  // High water: the waves' normal (else 0)
	float Crest : TEXCOORD1; // High water: how near the top of a wave (0-1)
	float2 Base : TEXCOORD2; // the point on the still water (High water's waves move it)
};

LAKE_OUTPUT VS_Lake(float3 Pos : ATTRIB0, float Depth : ATTRIB1)
{
	LAKE_OUTPUT output;
	output.Pos = mul(float4(Pos, 1), Transform);
	output.World = Pos;
	output.Depth = Depth;
	output.Fog = FogAmount(Pos, distance(Pos, FogCamera.xyz));
	output.Normal = float3(0, 0, 0);
	output.Crest = 0;
	output.Base = Pos.xz;
	return output;
}

// High water (RenderSettings::waterQuality 4 and 5; Medium, 3, draws PS_LakeHigh on the flat
// surface without the tessellation). The water's tiles are patches cut into up to
// WaterFlowParams.z pieces per edge near the camera and moved by six gentle Gerstner waves
// (27 to 2.6 units long): on seas and lakes they roll in towards the shore along the flow map
// (0.3 units high in deep water, less in the shallows), on rivers they are small and follow
// the current. The pixel shader adds finer ripples, foam on the waves' crests and the surf.
float4 FlowAtTexel(float2 xz)
{
	// the flow map by texel fetches (no sampler in the domain shader), bilinear between tiles
	float2 g = xz / 5 + TessParams.w - 0.5;
	int2 i = (int2)floor(g);
	float2 f = g - i;
	int2 last = (int2)TessGrid.xy - 1;
	float4 a = flowTexture.Load(int3(clamp(i, 0, last), 0)), b = flowTexture.Load(int3(clamp(i + int2(1, 0), 0, last), 0));
	float4 c = flowTexture.Load(int3(clamp(i + int2(0, 1), 0, last), 0)), d = flowTexture.Load(int3(clamp(i + int2(1, 1), 0, last), 0));
	return lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y);
}

// lod: 1 where the water is tessellated finest, falling to 0 with the detail (WaterLod)
// Smooth value noise in 0..1, two channels
float2 Hash2(float2 p)
{
	p = float2(dot(p, float2(127.1, 311.7)), dot(p, float2(269.5, 183.3)));
	return frac(sin(p) * 43758.5453);
}
float2 ValueNoise2(float2 p)
{
	float2 i = floor(p), f = p - i;
	float2 u = f * f * (3 - 2 * f);
	return lerp(lerp(Hash2(i), Hash2(i + float2(1, 0)), u.x), lerp(Hash2(i + float2(0, 1)), Hash2(i + float2(1, 1)), u.x), u.y);
}

void GerstnerWaves(float2 xz, float depth, float4 flow, float lod, out float3 offset, out float3 normal, out float crest)
{
	// seas and lakes only (rivers get flowing ripples): five long, smooth waves along one steady
	// wind, fading in from the shore over 6 units of depth and out with the distance (lod).
	// The direction must not vary over the map: the phase is direction . position, hundreds of
	// units out, so turning it with the flow map shifted the waves by whole wavelengths from tile
	// to tile (terraced chevrons). Near the shore the surf's bands run in along the depth.
	// Their sideways movement is shared out so that the surface never folds (sum of
	// k * a * q 0.6): the folds had drawn spikes.
	const float2 dir = float2(0.8, 0.6);
	float river = flow.a;
	float amp = 0.55 * smoothstep(0.5, 6, depth) * (1 - river) * lod;
	const float lengths[5] = { 34, 21, 13, 8.5, 5.6 };
	const float share[5] = { 1, 0.55, 0.33, 0.2, 0.12 };
	const float turn[5] = { 0, 0.5, -0.6, 1.0, -1.3 };
	offset = float3(0, 0, 0);
	float3 n = float3(0, 1, 0);
	crest = 0;
	float t = GrassParams.z;
	[unroll] for (int i = 0; i < 5; i++) {
		float cs = cos(turn[i]), sn = sin(turn[i]);
		float2 d = float2(dir.x * cs - dir.y * sn, dir.x * sn + dir.y * cs);
		float k = 6.2831853 / lengths[i];
		float w = sqrt(9.8 * k) * 0.6;
		// wave groups: each wave's height rises and falls over 60-110 units by its own noise,
		// drifting with the wind, so the sea never shows a repeating pattern
		float group = ValueNoise2(xz / (60 + 12 * i) - dir * (t * 0.02) + i * 3.1).x;
		float a = amp * share[i] * (0.4 + 1.2 * group);
		float q = a > 1e-4 ? min(0.6 / (k * a * 5), 1.0) : 0;
		float phase = k * dot(d, xz) - w * t + i * 1.7;
		float sp = sin(phase), cp = cos(phase);
		offset += float3(q * a * d.x * cp, a * sp, q * a * d.y * cp);
		n -= float3(d.x * k * a * cp, q * k * a * sp, d.y * k * a * cp);
		crest += share[i] * sp;
	}
	offset.y = max(offset.y, -0.7 * depth);
	normal = normalize(n);
	// 0 in the troughs, 1 at the highest crests (where the waves add up), none without waves
	crest = amp > 1e-3 ? saturate(0.5 + 0.5 * crest / 1.4) * saturate(amp / 0.25) : 0;
}

// How much of the tessellation's detail is left at a point (as HS_LakeConst's factors): the
// waves shrink with it, so that the coarser pieces far out don't show them as jagged triangles
float WaterLod(float3 p)
{
	float d = distance(p, FogCamera.xyz);
	return saturate(1 - (d - WaterFlowParams.w * 0.25) / WaterFlowParams.w);
}

LAKE_OUTPUT VS_LakeTess(float3 Pos : ATTRIB0, float Depth : ATTRIB1)
{
	return VS_Lake(Pos, Depth);
}

TESS_FACTORS HS_LakeConst(InputPatch<LAKE_OUTPUT, 3> p)
{
	TESS_FACTORS o;
	float3 m[3] = { 0.5 * (p[1].World + p[2].World), 0.5 * (p[2].World + p[0].World), 0.5 * (p[0].World + p[1].World) };
	[unroll] for (int i = 0; i < 3; i++) {
		o.Edges[i] = clamp(WaterFlowParams.z * WaterLod(m[i]), 1, WaterFlowParams.z);
	}
	o.Inside = max(o.Edges[0], max(o.Edges[1], o.Edges[2]));
	return o;
}

[domain("tri")]
[partitioning("fractional_odd")]
[outputtopology("triangle_cw")]
[outputcontrolpoints(3)]
[patchconstantfunc("HS_LakeConst")]
LAKE_OUTPUT HS_Lake(InputPatch<LAKE_OUTPUT, 3> p, uint i : SV_OutputControlPointID)
{
	return p[i];
}

[domain("tri")]
LAKE_OUTPUT DS_Lake(TESS_FACTORS factors, float3 b : SV_DomainLocation, const OutputPatch<LAKE_OUTPUT, 3> p)
{
	LAKE_OUTPUT o;
	float3 pos = p[0].World * b.x + p[1].World * b.y + p[2].World * b.z;
	// the depth from the water level and the terrain's smooth surface: from the tiles' corners
	// it bent at every triangle's edge, and the waves, surf and foam drew a sawtooth along them
	float depth = max(pos.y - SmoothHeight(pos.xz), 0);
	float3 offset, n;
	float crest;
	o.Base = pos.xz;
	GerstnerWaves(pos.xz, depth, FlowAtTexel(pos.xz), WaterLod(pos), offset, n, crest);
	pos += offset;
	o.Pos = mul(float4(pos, 1), Transform);
	o.World = pos;
	o.Depth = depth;
	o.Fog = FogAmount(pos, distance(pos, FogCamera.xyz));
	o.Normal = n;
	o.Crest = crest;
	return o;
}

// The water surface (docs/re/water.md). The original scrolls its texture layers along two wind
// vectors and lights each tile corner with a sun glint off a scrolling field of normals. Here
// per pixel: the map's Water_Surface.tga in two scrolling layers, two layers of a ripple normal
// map, a sun glint, the sky (fog) colour reflected at grazing angles, and more transparency
// where the water is shallow. From RenderSettings::waterQuality 2 (Low) also: the sky box's colours
// reflected along the view, a darker colour with depth, cloud shadows (on all but the glint)
// and foam along the shore, broken up by the ripples. And with RenderSettings::waterFlow: the
// ripples in three octaves, moving along the map's flow map (rivers downstream into their lake,
// sea or off the map), in two phases half a cycle apart, blended so that neither stretches for
// long; white water where narrow rivers run fast; a sharper sun sparkle.
float2 FlowSlope(float2 ruv, float2 offset)
{
	return (normalTexture.Sample(cloudSampler, ruv + WaterRipples.xy + offset).xy - 0.5)
		+ (normalTexture.Sample(cloudSampler, ruv * 2.3 + WaterRipples.zw + offset * 2.3).yx - 0.5) * 0.6
		+ (normalTexture.Sample(cloudSampler, ruv * 0.43 - WaterRipples.yx + offset * 0.43).xy - 0.5) * 0.35;
}

// Slight refraction (Low and High): the ground under the water seen through it, shifted by the
// surface's slope, more in deeper water. The water's own colour goes over it by its alpha;
// both are graded and fogged (the frame behind it already is), so the result is opaque.
float4 WithRefraction(float4 svpos, float3 n, float depth, float3 waterGraded, float alpha)
{
	if (WaterParams.w == 0)
		return float4(waterGraded, alpha);
	float2 size;
	sceneTexture.GetDimensions(size.x, size.y);
	float2 suv = svpos.xy / size + n.xz * (0.03 * saturate(depth / 1.5));
	// blurred with the depth, as water scatters the light from the bed (up to 6 pixels; a
	// sharp bed showed its tiles' edges through clear rivers)
	float2 r = saturate(depth / 3) * 6 / size;
	float3 behind = (sceneTexture.Sample(inpSampler, saturate(suv)).rgb * 2
		+ sceneTexture.Sample(inpSampler, saturate(suv + float2(r.x, r.y * 0.4))).rgb
		+ sceneTexture.Sample(inpSampler, saturate(suv + float2(-r.x * 0.4, r.y))).rgb
		+ sceneTexture.Sample(inpSampler, saturate(suv + float2(-r.x, -r.y * 0.4))).rgb
		+ sceneTexture.Sample(inpSampler, saturate(suv + float2(r.x * 0.4, -r.y))).rgb) / 6;
	return float4(lerp(behind, waterGraded, alpha), 1);
}

// The sky seen along a reflected ray (from Low): the sky box as DiligentSkyRenderer draws it
// (sky_front.tga towards +z, sky_right.tga towards +x), fading into the fog colour over its
// lowest rows as the original's; without the sky box its mean colours above and at the horizon
float3 SkyAlong(float3 r)
{
	if (WaterMode.z == 0)
		return WaterSkyUp.w != 0 ? lerp(WaterSkyHorizon.rgb, WaterSkyUp.rgb, saturate(r.y)) : FogColor.rgb;
	float a = r.z, b = r.x, c = max(r.y, 0);
	float horizontal = sqrt(a * a + b * b);
	float t = c / max(horizontal, 1e-6);
	float fog = t <= 1.0 / 81 ? 1 : t <= 4.0 / 81 ? lerp(1, 1 - 40.0 / 255, (t - 1.0 / 81) / (3.0 / 81))
		: t <= 9.0 / 81 ? lerp(1 - 40.0 / 255, 1 - 180.0 / 255, (t - 4.0 / 81) / (5.0 / 81))
		: t <= 16.0 / 81 ? lerp(1 - 180.0 / 255, 0, (t - 9.0 / 81) / (7.0 / 81)) : 0;
	float3 tex;
	if (c >= max(abs(a), abs(b)))
		tex = skyTop.SampleLevel(cloudSampler, float2(0.5 + 0.5 * b / c, 0.5 + 0.5 * a / c), 0).rgb;
	else if (abs(a) >= abs(b)) {
		if (a > 0) tex = skyFront.SampleLevel(inpSampler, float2(0.5 + 0.5 * b / a, 0.5 - 0.5 * c / a), 0).rgb;
		else       tex = skyBack.SampleLevel(inpSampler, float2(0.5 + 0.5 * b / a, 0.5 + 0.5 * c / a), 0).rgb;
	}
	else {
		if (b > 0) tex = skyRight.SampleLevel(inpSampler, float2(0.5 - 0.5 * a / b, 0.5 - 0.5 * c / b), 0).rgb;
		else       tex = skyLeft.SampleLevel(inpSampler, float2(0.5 - 0.5 * a / b, 0.5 + 0.5 * c / b), 0).rgb;
	}
	return lerp(tex, FogColor.rgb, fog);
}

// Reflections (from Low): the frame behind the water seen along the reflected ray. The ray
// is marched in world units, the steps growing with the distance (jittered per pixel), and
// projected; it hits where it passes behind the depth in the frame (by less than its step).
// Medium and up refine a hit by halving the last step. rgb: what is seen, a: how sure (fading
// towards the screen's edges and the ray's end, none for the sky or off the screen).
float4 ScreenReflection(float4 svpos, float3 world, float3 r)
{
	if (ReflectParams.x == 0 || WaterParams.w == 0 || r.y < -0.2)
		return float4(0, 0, 0, 0);
	float2 size;
	sceneTexture.GetDimensions(size.x, size.y);
	const int steps = (int)ReflectParams.x;
	const float reach = ReflectParams.w;
	float jitter = frac(52.9829189 * frac(dot(svpos.xy, float2(0.06711056, 0.00583715))));
	float tPrev = 0;
	[loop] for (int i = 1; i <= steps; i++) {
		float k = (i - 1 + jitter) / steps;
		float t = 0.4 + reach * k * k;
		float3 q = world + r * t;
		float4 clip = mul(float4(q, 1), Transform);
		if (clip.w <= 0.05)
			break;
		float2 uv = clip.xy / clip.w * float2(0.5, -0.5) + 0.5;
		if (any(uv < 0) || any(uv > 1))
			break;
		float d = depthTexture.Load(int3(uv * size, 0));
		float sceneZ = ReflectParams.z / (d - ReflectParams.y);
		float behind = clip.w - sceneZ;
		if (d < 1 && behind > 0 && behind < max(1.5, (t - tPrev) * 2)) {
			// refine between the last two points
			float lo = tPrev, hi = t;
			[loop] for (int j = 0; j < (int)WaterMode.x; j++) {
				float m = 0.5 * (lo + hi);
				float4 c = mul(float4(world + r * m, 1), Transform);
				float2 u = c.xy / c.w * float2(0.5, -0.5) + 0.5;
				float dm = depthTexture.Load(int3(saturate(u) * size, 0));
				if (c.w > ReflectParams.z / (dm - ReflectParams.y))
					hi = m;
				else
					lo = m;
			}
			float4 c = mul(float4(world + r * hi, 1), Transform);
			uv = saturate(c.xy / c.w * float2(0.5, -0.5) + 0.5);
			float2 edge = min(uv, 1 - uv);
			float sure = saturate(min(edge.x, edge.y) * 12) * saturate((1 - hi / reach) * 4);
			return float4(sceneTexture.SampleLevel(inpSampler, uv, 0).rgb, sure);
		}
		tPrev = t;
	}
	return float4(0, 0, 0, 0);
}

float4 PS_Lake(LAKE_OUTPUT input) : SV_Target
{
	// from Low up, the depth from the terrain's smooth surface (not bent at the triangles' edges)
	if (WaterSkyUp.w != 0)
		input.Depth = max(input.World.y - SmoothHeight(input.World.xz), 0);
	if (WaterParams.x == 0)
		return float4(GradeColour(lerp(LakeColour.rgb, FogColor.rgb, input.Fog)), LakeColour.a);
	float2 uv = input.World.xz * WaterParams.y;
	float3 tex = 0.5 * (inpTexture.Sample(cloudSampler, uv + WaterLayers.xy).rgb + inpTexture.Sample(cloudSampler, uv + WaterLayers.zw).rgb);
	float2 ruv = input.World.xz * WaterParams.z;
	float2 slope = normalTexture.Sample(cloudSampler, ruv + WaterRipples.xy).xy + normalTexture.Sample(cloudSampler, ruv * 1.7 + WaterRipples.zw).yx;
	float rush = 0, river = 0, swellPhase = 0, swell = 0;
	if (WaterFlowParams.x != 0 && WaterSkyUp.w != 0) {
		float4 f = flowTexture.Sample(inpSampler, (input.World.xz + BlobParams.w) * BlobParams.yz);
		float2 fdir = f.xy * 2 - 1;
		float2 flow = fdir * f.z * WaterFlowParams.y; // units per second
		river = f.a;
		rush = f.z * saturate(length(fdir) * 1.5) * river;
		const float cycle = 2.0;
		float p0 = frac(GrassParams.z / cycle), p1 = frac(GrassParams.z / cycle + 0.5);
		float w0 = 1 - abs(2 * p0 - 1);
		float2 off0 = -flow * (p0 * cycle) * WaterParams.z, off1 = -flow * (p1 * cycle) * WaterParams.z;
		// the river look: finer, smaller ripples carried by the current. (Not turned along the
		// flow: turning the world-scale ripple coordinates tore the texture wherever the direction
		// changed, and stretching them drew streaks.)
		float2 along = normalize(fdir + float2(1e-4, 0));
		float2 sRiver = (FlowSlope(ruv * 1.8, off0 * 1.8) * w0 + FlowSlope(ruv * 1.8, off1 * 1.8) * (1 - w0)) * 0.55;
		// the sea's (and a lake's) look: larger waves, and a swell rolling in over the shallows
		// towards the shore (bands along the depth's contours, as waves follow the bed)
		float2 sSea = (FlowSlope(ruv * 0.7, off0 * 0.7) * w0 + FlowSlope(ruv * 0.7, off1 * 0.7) * (1 - w0)) * 0.8;
		swellPhase = input.Depth * 2.2 + GrassParams.z * 1.3;
		swell = saturate(1 - input.Depth / 5) * saturate(input.Depth * 3) * (1 - river);
		sSea -= along * cos(swellPhase) * swell * 0.55;
		float2 s = lerp(sSea, sRiver, river);
		slope = s * 0.9 + 1; // as the two-sample sum: centred on 1
		// the texture moves half as far: at its larger scale the full shift smeared it
		float2 tuv0 = uv - flow * (p0 * cycle) * WaterParams.y * 0.5, tuv1 = uv - flow * (p1 * cycle) * WaterParams.y * 0.5;
		tex = lerp(inpTexture.Sample(cloudSampler, tuv1 + WaterLayers.xy).rgb, inpTexture.Sample(cloudSampler, tuv0 + WaterLayers.xy).rgb, w0) * 0.5
			+ 0.5 * inpTexture.Sample(cloudSampler, uv + WaterLayers.zw).rgb;
	}
	float3 n = normalize(float3(slope.x - 1, 1, slope.y - 1));
	float3 v = normalize(FogCamera.xyz - input.World);
	float3 r = reflect(-v, n);
	float glint = pow(saturate(dot(r, SunDirection)), 64) * 0.7 + pow(saturate(dot(r, SunDirection)), 3) * 0.06;
	if (WaterFlowParams.x != 0 && WaterSkyUp.w != 0)
		glint += pow(saturate(dot(r, SunDirection)), 600) * 1.5;
	float fresnel = 0.02 + 0.98 * pow(1 - saturate(dot(n, v)), 5);
	float3 sky = FogColor.rgb;
	float foam = 0;
	if (WaterSkyUp.w != 0) {
		sky = SkyAlong(r);
		tex *= lerp(1.0, float3(0.55, 0.7, 0.8), saturate(input.Depth / 6));
		float cloud = CloudLight(input.World);
		tex *= cloud;
		sky *= lerp(1, cloud, 0.5);
		// foam: a band where the water is shallower than 0.8 units, its edge moved by the ripples
		// (with the flow map, in the sea only where it is shallower than 0.5)
		float edge = input.Depth + (slope.x + slope.y - 2) * 0.35;
		foam = saturate(1 - edge / 0.8) * saturate(input.Depth * 4) * 0.55;
		if (WaterFlowParams.x != 0) // the sea's broad shallows: foam only right at the waterline
			foam *= lerp(saturate(1 - input.Depth / 0.5), 1, river);
		// white water where a narrow river runs fast, on the ripples' crests
		foam = max(foam, saturate((slope.x + slope.y - 2) * 3 - 0.6) * rush * rush * 0.35);
		// the swell breaking: foam on its crests in the last 1.2 units of depth
		foam = max(foam, saturate((sin(swellPhase) - 0.7) * 4) * saturate(1 - input.Depth / 0.8) * swell * 0.7);
		// open water darker and bluer farther out
		tex *= lerp(float3(1, 1, 1), float3(0.8, 0.9, 1.0), (1 - river) * saturate(input.Depth / 10) * WaterFlowParams.x);
	}
	float3 c = lerp(lerp(tex, sky, fresnel * 0.6), float3(0.92, 0.95, 0.96), foam) + glint;
	float alpha = saturate(lerp(0.3, 0.8, saturate(input.Depth / 3)) + fresnel * 0.3 + glint + foam);
	if (WaterSkyUp.w != 0) {
		float4 seen = ScreenReflection(input.Pos, input.World, r);
		float share = seen.a * max(fresnel * 0.6, 0.3) * (1 - foam);
		c = lerp(c, seen.rgb, share);
		alpha = max(alpha, share);
	}
	if (WaterSkyUp.w != 0)
		return WithRefraction(input.Pos, n, input.Depth, GradeColour(lerp(c, FogColor.rgb, input.Fog)), alpha);
	return float4(GradeColour(lerp(c, FogColor.rgb, input.Fog)), alpha);
}

// The rivers' ripples at (s, d): s the distance to the mouth along the channel, d the distance
// to the bank. Small, round waves (the same scale along and across; drawn out along the flow
// their ridges caught the sun as white lines, which no river has), in two sizes. Slopes along
// and across.
float2 RiverSlope(float s, float d)
{
	float2 c = normalTexture.Sample(cloudSampler, float2(s, d) * 0.09 + float2(0.71, 0.13)).xy - 0.5;
	float2 a = normalTexture.Sample(cloudSampler, float2(s, d) * 0.22).xy - 0.5;
	float2 b = normalTexture.Sample(cloudSampler, float2(s, d) * 0.5 + float2(0.31, 0.63)).yx - 0.5;
	return c * 0.8 + a + b * 0.5;
}

float RiverNoise(float2 uv)
{
	return saturate(length(normalTexture.Sample(cloudSampler, uv).xy - 0.5) * 2.5);
}

// High water's pixels: the waves' normal with finer ripples on it (carried by the flow), the
// sky and sun as on Low, and foam on the crests and in the surf, broken up by the ripple map
float4 PS_LakeHigh(LAKE_OUTPUT input) : SV_Target
{
	float2 base = input.Base;
	float depth = input.Depth;
	if (WaterMode.y != 0) // Medium: the flat surface's depth from the terrain's smooth surface
		depth = max(input.World.y - SmoothHeight(base), 0);
	float4 f = flowTexture.Sample(inpSampler, (base + BlobParams.w) * BlobParams.yz);
	float2 fdir = f.xy * 2 - 1;
	float river = f.a;
	// the waves' normal and crest again here, per pixel, from the same function (per vertex
	// they were faceted)
	float3 offset, wn;
	float crest;
	GerstnerWaves(base, depth, f, WaterLod(input.World), offset, wn, crest);
	// fine ripples on them, carried by the current on rivers: the sparkles and the rivers' look
	float2 flow = fdir * f.z * WaterFlowParams.y * WaterFlowParams.x * river;
	const float cycle = 2.0;
	float p0 = frac(GrassParams.z / cycle), p1 = frac(GrassParams.z / cycle + 0.5);
	float w0 = 1 - abs(2 * p0 - 1);
	float2 ruv = base * WaterParams.z * 3.0;
	float2 off0 = -flow * (p0 * cycle) * WaterParams.z * 3.0, off1 = -flow * (p1 * cycle) * WaterParams.z * 3.0;
	float2 detail = FlowSlope(ruv, off0) * w0 + FlowSlope(ruv, off1) * (1 - w0);
	float2 fine = normalTexture.Sample(cloudSampler, ruv * 3.7 + WaterRipples.zw * 4).xy - 0.5;
	// the rivers' own look (BuildRiverField): ripples laid along the channel, following its
	// bends from bank to bank, carried downstream by the current (faster where narrower), in
	// two phases half a cycle apart so that speed changes along it never stretch them for long
	float4 rf = riverTexture.Sample(inpSampler, (base + BlobParams.w) * BlobParams.yz);
	float2 along = dot(rf.zw, rf.zw) > 0.01 ? normalize(rf.zw) : float2(1, 0);
	float2 across = float2(-along.y, along.x);
	// the current by the river's width: 3.5 units a second where it is 8 wide or less,
	// slowing to 0.6 where it is 50 wide and more
	float halfWidth = max(riverWidth.Sample(inpSampler, (base + BlobParams.w) * BlobParams.yz), 1.5);
	float current = clamp(14.0 / halfWidth, 0.6, 3.5) * WaterFlowParams.x; // units per second
	float tight = saturate((17 - halfWidth) / 11); // 1 where it is 12 units wide or less, 0 from 34
	const float rcycle = 3.0;
	float q0 = frac(GrassParams.z / rcycle), q1 = frac(GrassParams.z / rcycle + 0.5);
	float v0 = 1 - abs(2 * q0 - 1);
	float s0 = rf.x + current * q0 * rcycle, s1 = rf.x + current * q1 * rcycle + 7.3;
	float2 rs = RiverSlope(s0, rf.y) * v0 + RiverSlope(s1, rf.y) * (1 - v0);
	float2 riverDetail = rs.x * along + rs.y * across;
	float2 seaDetail = detail * 0.3 + fine * 0.22;
	float2 slope = lerp(seaDetail, riverDetail * 0.5, river);
	float3 n = normalize(wn + float3(slope.x, 0, slope.y));
	float3 v = normalize(FogCamera.xyz - input.World);
	float3 l = normalize(SunDirection);
	float3 r = reflect(-v, n);
	float fresnel = 0.02 + 0.98 * pow(1 - saturate(dot(n, v)), 5);
	float cloud = CloudLight(input.World);
	// the water's own colour: turquoise in the shallows to a deep blue, a little of the map's
	// water texture, lit by the sun through the surface
	float2 uv = base * WaterParams.y;
	float3 tex = 0.5 * (inpTexture.Sample(cloudSampler, uv + WaterLayers.xy).rgb + inpTexture.Sample(cloudSampler, uv + WaterLayers.zw).rgb);
	float3 body = lerp(float3(0.07, 0.38, 0.42), float3(0.015, 0.15, 0.26), smoothstep(0.5, 8, depth));
	body = lerp(body, tex * float3(0.55, 0.75, 0.85), 0.25);
	body *= (0.6 + 0.4 * saturate(dot(wn, l))) * cloud;
	// light through the waves' faces: they glow turquoise as they rise, most against the sun
	float through = crest * saturate(dot(-v, l) * 0.4 + 0.7);
	body += float3(0.04, 0.32, 0.28) * through * 0.55 * cloud;
	float3 rr = reflect(-v, normalize(wn + (n - wn) * 0.35));
	float3 sky = SkyAlong(rr) * lerp(1, cloud, 0.5);
	// the sun: a broad highlight off the waves, many small sparkles off the ripples
	float sw = saturate(dot(reflect(-v, wn), l)), sr = saturate(dot(r, l));
	float glint = (pow(sw, 160) * 0.45 + pow(sr, 1400) * lerp(2.2, 0.5, river)) * cloud;
	// foam: streaks on the highest crests, broken up by value noise in three octaves, its
	// coordinates warped by a slower noise and drifting with the wind: nothing repeats (the
	// ripple map had, every 24 units, and its shapes showed again and again over a wide sea)
	float2 drift = float2(0.8, 0.6) * GrassParams.z * 0.6;
	float2 wp = base - drift + (ValueNoise2(base / 31 + GrassParams.z * 0.02) - 0.5) * 14;
	float fbm = ValueNoise2(wp / 7.0).x * 0.55 + ValueNoise2(wp / 3.1 + 5.3).y * 0.3 + ValueNoise2(wp / 1.3 + 9.7).x * 0.15;
	// lacy edges from two fine layers of the ripple map (too small to show a pattern)
	float lace = saturate(length(normalTexture.Sample(cloudSampler, wp * 0.21).xy - 0.5) * 3) * 0.6
		+ saturate(length(normalTexture.Sample(cloudSampler, wp * 0.47 + 0.3).xy - 0.5) * 3) * 0.4;
	float noise = saturate((fbm - 0.22) * 1.9) * lerp(0.35, 1.25, lace);
	// (more of it in some stretches than others, by a slow noise: no regular pattern)
	float foamPatch = smoothstep(0.25, 0.75, ValueNoise2(base / 45 - float2(0.8, 0.6) * GrassParams.z * 0.015 + 9.7).y);
	// (the crests foam at a height that varies by region too, so the rows of whitecaps differ)
	float crestAt = lerp(0.66, 0.82, ValueNoise2(base / 57 + 3.3).x);
	float crestFoam = smoothstep(crestAt, crestAt + 0.2, crest) * smoothstep(0.15, 0.7, noise) * lerp(0.35, 1.2, foamPatch);
	// the surf: bands that run in with the swell where the waves break, 1.2 to 0.15 units deep,
	// and the swash at the waterline
	float surf = saturate((sin(depth * 3 + GrassParams.z * 1.6) - 0.6) * 2.5) * smoothstep(1.2, 0.5, depth) * smoothstep(0.05, 0.15, depth);
	float swash = smoothstep(0.25, 0.0, depth);
	float shoreFoam = max(surf * smoothstep(0.1, 0.7, noise), swash * smoothstep(0.0, 0.6, noise)) * (1 - 0.6 * river);
	// the rivers' foam: a thin, broken edge along the banks, carried with the current
	float bankNoise = RiverNoise(float2(s0, rf.y) * 0.3) * v0 + RiverNoise(float2(s1, rf.y) * 0.3 + 0.5) * (1 - v0);
	float bankFoam = saturate(saturate(1 - rf.y / 1.2) * 1.4 - bankNoise * 1.2) * 0.55;
	// small white crests on the ripples where the river runs tight, carried with them
	// (the user: the ripple map's crests made "an ugly repeating pattern"). Each is its own: the
	// river's (along, across) coordinates in cells of 2.4 x 1.8 units, carried with the current;
	// a cell has a crest only by its hash (more of them where tighter), at a random place, size,
	// angle and time, rising and fading over a random 0.8-1.6 s of a 2.5-5 s cycle. Two such
	// layers, offset by half a cell.
	float riverCrest = 0;
	float sAdv = rf.x + current * GrassParams.z;
	[unroll] for (int layer = 0; layer < 2; layer++) {
		float2 q = float2(sAdv / 2.4, rf.y / 1.8) + layer * float2(0.5, 0.37);
		float2 cell = floor(q), fr = q - cell;
		float2 h1 = Hash2(cell + layer * 17.1), h2 = Hash2(cell + 41.7 + layer * 9.3);
		if (h1.x > tight * 0.55)
			continue;
		float period = 2.5 + 2.5 * h2.x;
		float life = 0.8 + 0.8 * h2.y;
		float at = frac(GrassParams.z / period + h1.y) * period;
		float rise = saturate(at / (life * 0.3)) * saturate((life - at) / (life * 0.7));
		float2 c = 0.25 + 0.5 * Hash2(cell + 7.7 + layer);
		float2 dq = (fr - c) * float2(2.4, 1.8);
		float ang = h2.x * 3.1;
		float2 rq = float2(dq.x * cos(ang) - dq.y * sin(ang), dq.x * sin(ang) + dq.y * cos(ang));
		float size = (0.25 + 0.35 * h1.y) * rise;
		float shape = length(rq / float2(size * 1.6 + 1e-3, size * 0.7 + 1e-3));
		riverCrest = max(riverCrest, saturate((1 - shape) * 3) * rise);
	}
	riverCrest *= tight * saturate(rf.y / 0.8);
	float riverFoam = max(bankFoam, riverCrest) * river;
	float foam = saturate(max(max(crestFoam, shoreFoam), riverFoam)) * 0.9;
	// what the waves reflect: the frame along the ray (bent by the waves, less by the ripples),
	// else the sky
	float4 seen = ScreenReflection(input.Pos, input.World, rr);
	float mirror = fresnel * 0.8;
	float3 reflected = lerp(sky, seen.rgb, seen.a);
	mirror = lerp(mirror, max(mirror, 0.4), seen.a);
	float3 c = lerp(lerp(body, reflected, mirror), float3(0.9, 0.95, 0.96) * lerp(1, cloud, 0.5), foam) + glint * (1 - foam) * (1 - seen.a * 0.7);
	float clear = lerp(lerp(0.35, 0.95, saturate(depth / 2.5)), clamp(1 - exp(-1.3 * depth), 0.2, 0.96), river);
	float alpha = saturate(clear + mirror * 0.25 + glint + foam);
	return WithRefraction(input.Pos, n, depth, GradeColour(lerp(c, FogColor.rgb, input.Fog)), alpha);
}

// Grass (RenderSettings::grass): one instance per blade, three triangles from SV_VertexID (a
// base, a narrower middle, the tip), leaning its own way and swaying in the wind, shrinking to
// nothing over the last 30% of the grass distance. A blade is drawn while its random value
// (the colour's alpha) is below the density share, so the density slider works at once.
// Shading is physically based, with values for grass leaves: a waxy dielectric cuticle
// (F0 0.04, roughness 0.6) over a diffuse, translucent blade. It gets the sun through its own
// normal (wrapped a little, as leaves scatter light inside), light through the blade where its
// other side faces the sun (about 30% transmits; more looking into the sun through it), the
// sky's colour as ambient, darker towards
// the root where the blades shade each other, a GGX highlight, the clouds, static and blob
// shadows and fires.
struct GRASS_OUTPUT
{
	float4 Pos : SV_POSITION;
	float3 World : COLOR1;
	float4 Colour : COLOR0;
	float3 Normal : NORMAL;
	float Up : TEXCOORD0; // 0 at the root, 1 at the tip
	float Fog : FOG;
};

GRASS_OUTPUT VS_Grass(float3 Root : ATTRIB0, float4 Colour : ATTRIB1, float2 YawHeight : ATTRIB2, uint VertexId : SV_VertexID)
{
	static const float2 shape[9] = { float2(-1, 0), float2(1, 0), float2(-0.7, 0.55), float2(1, 0), float2(0.7, 0.55),
		float2(-0.7, 0.55), float2(-0.7, 0.55), float2(0.7, 0.55), float2(0, 1) };
	float2 s = shape[VertexId];
	if (TessParams.x != 0)
		Root.y = SmoothHeight(Root.xz);
	float dist = distance(Root, FogCamera.xyz);
	// each blade ends at its own distance, from 55% of the grass distance to all of it, and
	// shrinks over the last 40% of its reach: the meadow thins out instead of ending in a line
	float reach = GrassParams.y * (0.55 + 0.45 * frac(sin(dot(Root.xz, float2(12.9898, 78.233))) * 43758.5453));
	float grow = saturate((reach - dist) / (reach * 0.4)) * step(Colour.a, GrassLook.x);
	float height = YawHeight.y * grow * GrassLook.y;
	float2 dir = float2(cos(YawHeight.x), sin(YawHeight.x));
	float2 side = float2(-dir.y, dir.x);
	float sway = sin(GrassParams.z * 1.7 + Root.x * 0.35 + Root.z * 0.23) * 0.5 + 0.5;
	float bend = (0.15 + 0.25 * sway) * height * s.y * s.y;
	float3 p = Root + float3(side.x, 0, side.y) * (s.x * 0.07 * grow) + float3(dir.x, 0, dir.y) * bend + float3(0, s.y * height, 0);
	GRASS_OUTPUT output;
	output.Pos = mul(float4(p, 1), Transform);
	output.World = p;
	output.Colour = float4(Colour.rgb, 1);
	// the blade's face: towards its lean, turning up as it bends, rounded across its width
	output.Normal = float3(dir.x, 0, dir.y) + float3(0, 0.5 + 0.9 * s.y, 0) + float3(side.x, 0, side.y) * (s.x * 0.35);
	output.Up = s.y;
	output.Fog = FogAmount(p, dist);
	return output;
}

float4 PS_Grass(GRASS_OUTPUT input) : SV_Target
{
	// the blade's normal, bent towards the ground's (up): seen from above, a meadow's blades
	// face the sky more than their shape alone says
	float3 n = normalize(lerp(normalize(input.Normal), float3(0, 1, 0), 0.55));
	float3 v = normalize(FogCamera.xyz - input.World);
	if (dot(n, v) < 0)
		n = -n; // both sides of the blade
	float3 l = normalize(SunDirection);
	float shadow = CloudLight(input.World) * StaticShadowLight(input.World) * BlobLight(input.World);
	float3 albedo = input.Colour.rgb;
	float nl = saturate(dot(n, l)), nv = max(dot(n, v), 1e-3);
	float wrapped = saturate((dot(n, l) + 0.25) / 1.25);
	// light through the blade: about 30% of what falls on its other side, diffusely, and more
	// when looking towards the sun through it
	float transmitted = saturate(-dot(n, l)) * 0.3 + pow(saturate(dot(-v, l)), 3) * 0.2;
	// GGX highlight of the cuticle: F0 0.04, roughness 0.6
	const float a = 0.36, a2 = a * a, k = a / 2;
	float3 h = normalize(l + v);
	float nh = saturate(dot(n, h)), vh = saturate(dot(v, h));
	float d = a2 / (3.14159265 * pow(nh * nh * (a2 - 1) + 1, 2));
	float vis = 1 / ((nl * (1 - k) + k) * (nv * (1 - k) + k));
	float f = 0.04 + 0.96 * pow(1 - vh, 5);
	float specular = d * vis * f * nl / 4;
	// the sky above as ambient (the sky box's colours with the water's detail on, else the fog)
	float3 sky = WaterSkyUp.w != 0 ? lerp(WaterSkyHorizon.rgb, WaterSkyUp.rgb, saturate(n.y)) : FogColor.rgb;
	float occlusion = lerp(0.35, 1.0, input.Up);
	float3 c = albedo * ((wrapped * 0.75 + transmitted) * shadow + sky * 0.3 * occlusion) + specular * shadow
		+ albedo * FireLight(input.World, n);
	// never much darker than the ground under it: seen edge-on from low down, a blade's side away
	// from the sun still gets the light bounced up from the sunlit meadow around it
	float ground = max(SunDirection.y, 1.0 / 3.0) * shadow;
	c = max(c, albedo * ground * lerp(0.7, 0.9, input.Up));
	c = lerp(c, FogColor.rgb, input.Fog);
	return float4(GradeColour(c), 1);
}

// The original lights the terrain with a Dot3 bump-mapping pass and no ambient term: the
// per-vertex light vector (the sun direction in tangent space, shortened by the cloud
// shadows) dotted with the normal map, clamped, times the texture.
// See docs/re/lighting.md.
float4 ShadeTerrain(VS_OUTPUT input, float4 tex, float3 nrm, float3 tangent, float3 bitangent)
{
	float lum = 0;

	// BumpOn & 2 (RenderSettings::terrainOriginalLight): the original's per-vertex Dot3 light
	// vector. It is the sun in the slope's tangent frame, but its up component is at least 1/3
	// (the sides shortened to keep it unit length), so faces turned away from the sun keep a
	// third of the light (fitted to its traced vertices; RE repository,
	// docs/engine/original-comparison.md)
	float3 vertexNormal = input.Norm;
	if (BumpOn & 2) {
		float3 n = normalize(vertexNormal);
		float3 t = normalize(tangent - n * dot(tangent, n));
		float3 b = normalize(bitangent - n * dot(bitangent, n) - t * dot(bitangent, t));
		float3 l = float3(dot(SunDirection, t), dot(SunDirection, n), dot(SunDirection, b));
		if (l.y < 1.0 / 3.0) {
			l.xz *= 0.9428090 / max(length(l.xz), 1e-5); // sqrt(8/9)
			l.y = 1.0 / 3.0;
		}
		// the filtered normal as it is, as the original's DOTPRODUCT3 stage: rough bump maps
		// average to a shorter normal in the distance, which darkens them
		lum = (BumpOn & 1) ? dot(nrm, l) : l.y;
		lum = clamp(lum, 0, 1);
	}
	else if(BumpOn & 1) {
		float3 actnorm = nrm.xxx * tangent + nrm.yyy * vertexNormal + nrm.zzz * bitangent;
		lum += clamp(dot(normalize(actnorm), SunDirection), 0, 1);
		lum = clamp(lum, 0, 1);
	}
	else {
		lum += clamp(dot(vertexNormal, SunDirection), 0, 1);
		lum = clamp(lum, 0, 1);
	}
	lum *= CloudLight(input.FragPos) * StaticShadowLight(input.FragPos);
	float3 light = (lum * BlobLight(input.FragPos)).xxx + FireLight(input.FragPos, normalize(input.Norm));
	float4 c = lerp(float4(light, 1) * input.Color * tex, FogColor, input.Fog);
	return float4(GradeColour(c.rgb), c.a);
}

// The tile's own picture: its texel in the 4 x 4 atlas, not bleeding into the neighbours
float2 AtlasUV(float2 texcoord)
{
	// Compute the length of a half pixel in the current mipmap
	float lod = inpTexture.CalculateLevelOfDetail(inpSampler, texcoord);
	uint aw, ah, levels; // the atlas: 256 texels, or more in a high-resolution mod
	inpTexture.GetDimensions(0, aw, ah, levels);
	float ha = 0.5 / max(aw >> (int)ceil(lod), 1u);

	float2 stc = clamp(texcoord, float2(ha,ha), float2(1.0-ha,1.0-ha));
	float2 tile = floor(stc / float2(0.25,0.25)); // tile index in the 4x4 atlas
	float2 subuv = fmod(stc, float2(0.25,0.25)); // coords inside the used tile

	return clamp(subuv, float2(ha,ha), float2(0.25-ha,0.25-ha)) + tile*0.25;
}

float4 PS(VS_OUTPUT input) : SV_Target
{
	float2 fuv = AtlasUV(input.Texcoord);
	float4 tex = inpTexture.Sample(inpSampler, fuv);
	float3 nrm = normalTexture.Sample(inpSampler, fuv).xyz * 2 - 1;
	return ShadeTerrain(input, tex, nrm, input.Tangent, input.Bitangent);
}

// The splat-map terrain (gfx/EnhancedTerrainData.h): the materials of the four corners of the
// tile, blended by distance to the corners and sharpened by the textures' brightness (a
// height blend: the brighter material shows through first), with world-space UVs, one repeat
// per two tiles. The tangent frame follows the world's x and z.

// A material layer at a ground position (p in world units, tile-aligned): one of its four tiles
// (the layer's quadrants), picked per 5-unit cell by a hash, with the gradients of the continuous
// position so the mip level doesn't jump at the cells' borders
float4 SampleVariant(Texture2DArray<float4> layers, float2 p, uint layer)
{
	// the material's four tiles blended by smooth, low noise (about 25 units across). One tile
	// chosen outright per 5-unit cell drew a grid of differently toned squares.
	float2 a = smoothstep(0.4, 0.6, ValueNoise2(p * 0.04 + 3.7)); // mostly one tile, narrow seams between
	float2 local = frac(p / 5) * 0.5;
	float2 g = p / 10;
	float2 gx = ddx(g), gy = ddy(g);
	float4 s0 = layers.SampleGrad(splatSampler, float3(local, layer), gx, gy);
	float4 s1 = layers.SampleGrad(splatSampler, float3(local + float2(0.5, 0), layer), gx, gy);
	float4 s2 = layers.SampleGrad(splatSampler, float3(local + float2(0, 0.5), layer), gx, gy);
	float4 s3 = layers.SampleGrad(splatSampler, float3(local + float2(0.5, 0.5), layer), gx, gy);
	return lerp(lerp(s0, s1, a.x), lerp(s2, s3, a.x), a.y);
}

uint4 CornerLayers(int2 c0)
{
	return uint4(splatCorners.Load(int3(c0, 0)), splatCorners.Load(int3(c0 + int2(1, 0), 0)),
		splatCorners.Load(int3(c0 + int2(0, 1), 0)), splatCorners.Load(int3(c0 + int2(1, 1), 0)));
}

float4 PS_Splat(VS_OUTPUT input) : SV_Target
{
	// the blend looks the corners up at a position warped by noise, so that the materials'
	// borders don't follow the tile grid. (Tiles whose corners all agree had kept their own
	// picture, whose tone differs from the blended layers: every such tile showed as a square.)
	float2 wpos = input.FragPos.xz + (ValueNoise2(input.FragPos.xz * 0.15) - 0.5) * 3.5;
	float2 c = (wpos + BlobParams.w) / 5;
	int2 c0 = (int2)floor(c);
	float2 f = c - c0;
	uint4 l4 = CornerLayers(c0);
	uint l[4] = { l4.x, l4.y, l4.z, l4.w };
	float w[4] = { (1 - f.x) * (1 - f.y), f.x * (1 - f.y), (1 - f.x) * f.y, f.x * f.y };
	// steep ground (cliffs): the materials projected along the three axes (triplanar), as the
	// tiles' pictures stretch down a slope
	float3 n = normalize(input.Norm);
	float steep = saturate((0.8 - n.y) * 4);
	float2 gp = input.FragPos.xz + BlobParams.w; // on the tile grid
	float3 tw = pow(abs(n), 4);
	tw /= tw.x + tw.y + tw.z;
	float4 tex = 0;
	float3 nrm = 0;
	float4 s[4];
	float3 sn[4];
	float h[4];
	float top = 0;
	[unroll] for (int i = 0; i < 4; i++) {
		s[i] = SampleVariant(splatDiffuse, gp, l[i]);
		if (steep > 0)
			s[i] = lerp(s[i], tw.y * s[i] + tw.x * SampleVariant(splatDiffuse, input.FragPos.zy, l[i])
				+ tw.z * SampleVariant(splatDiffuse, input.FragPos.xy, l[i]), steep);
		sn[i] = SampleVariant(splatNormal, gp, l[i]).xyz * 2 - 1;
		h[i] = w[i] + dot(s[i].rgb, float3(0.299, 0.587, 0.114)) * 0.35;
		top = max(top, h[i]);
	}
	float total = 0;
	[unroll] for (int j = 0; j < 4; j++) {
		float b = max(h[j] - (top - 0.45), 0);
		tex += s[j] * b;
		nrm += sn[j] * b;
		total += b;
	}
	tex /= total;
	nrm /= total;
	// on cliffs the normal maps' detail fades: their planar projection doesn't fit there
	nrm = normalize(lerp(nrm, float3(0, 1, 0), steep));
	float3 t = normalize(float3(1, 0, 0) - n * n.x);
	float3 b = normalize(float3(0, 0, 1) - n * n.z);
	return ShadeTerrain(input, tex, nrm, t, b);
}
)---";

struct DiligentEnhancedTerrainRenderer::Impl {
	RefCntAutoPtr<IPipelineResourceSignature> signature;
	RefCntAutoPtr<IPipelineState> pipeline;
	// the splat-map terrain: its pipeline, material layers and corner materials
	RefCntAutoPtr<IPipelineState> splatPipeline;
	RefCntAutoPtr<ITexture> splatDiffuse, splatNormal, splatCorners;
	RefCntAutoPtr<ITexture> flowMap; // the water's flow, one texel per tile
	RefCntAutoPtr<ITexture> riverField; // the rivers' coordinates (BuildRiverField)
	RefCntAutoPtr<ITexture> riverWidth; // and their half width
	RefCntAutoPtr<ITexture> heightMap; // the vertex heights, for the tessellated terrain's surface
	bool tessellation = false; // the GPU can tessellate
	RefCntAutoPtr<IPipelineState> tessPipeline, tessSplatPipeline;
	RefCntAutoPtr<IPipelineState> highWaterPipeline; // RenderSettings::waterQuality 4 and 5 (High, Ultra)
	RefCntAutoPtr<IPipelineState> mediumWaterPipeline; // 3 (Medium): High's pixels on the flat surface
	RefCntAutoPtr<ITexture> sceneCopy; // the frame before the water, for its refraction and reflections
	RefCntAutoPtr<ITexture> depthCopy; // and its depth
	static constexpr const char* SKY_NAMES[5] = { "skyFront", "skyRight", "skyBack", "skyLeft", "skyTop" };
	texture boundSky[5] = {}; // ShadowEnvironment::skyFaces as bound to lakeBinding
	bool refraction = false; // this frame
	RefCntAutoPtr<IBuffer> sunBuffer;
	RefCntAutoPtr<IBuffer> trnVertexBuffer;

	using TexturePair = std::pair<texture, texture>; // diffuse, normal map
	std::map<TerrainTexture*, TexturePair> textures;
	std::map<TexturePair, RefCntAutoPtr<IShaderResourceBinding>> bindings;
	// The terrain in chunks of CHUNK x CHUNK tiles, with their index lists per texture and their
	// water quads prebuilt: each frame only the chunks' bounding boxes are tested against the
	// view, and the visible chunks' lists are concatenated (one draw per texture, one for water).
	struct Chunk {
		Vector3 low, high;
		uint32_t grassFirst = 0, grassCount = 0; // its blades in grassBuffer
		std::vector<std::pair<int, std::vector<uint32_t>>> indicesPerTexture; // texture pair index
		std::vector<uint32_t> lakeIndices;
	};
	std::vector<Chunk> chunks;
	std::vector<TexturePair> texturePairs;
	std::vector<std::vector<uint32_t>> frameIndices; // per texture pair, this frame
	std::vector<uint32_t> frameLakeIndices;
	RefCntAutoPtr<IBuffer> lakeVertexBuffer;
	RefCntAutoPtr<IPipelineState> lakePipeline;
	RefCntAutoPtr<IPipelineState> grassPipeline;
	RefCntAutoPtr<IBuffer> grassBuffer; // GrassBlade instances, chunk after chunk
	float grassBuiltDensity = 0; // the blades per square unit in grassBuffer (TerrainGrassBuilder::BuildDensity)
	void buildGrass(IRenderDevice* device, Terrain* terrain, float density);
	RefCntAutoPtr<IShaderResourceBinding> lakeBinding;
	texture cloudMap = nullptr;
	texture waterTexture = nullptr, rippleTexture = nullptr;
	RefCntAutoPtr<ITexture> shadowMap;
	// Blob shadows: their own pipeline, drawing into blobMap
	RefCntAutoPtr<IPipelineResourceSignature> blobSignature;
	RefCntAutoPtr<IPipelineState> blobPipeline;
	RefCntAutoPtr<IShaderResourceBinding> blobBinding;
	RefCntAutoPtr<IBuffer> blobBuffer;
	RefCntAutoPtr<ITexture> blobMap;
	texture blobSprite = nullptr;
	std::unique_ptr<DiligentWaterfalls> waterfalls; // where the water drops (from Low water)
};

// Texels of the blob shadow texture per world unit (at most 4096 per side)
static constexpr float BLOB_TEXELS_PER_UNIT = 2.0f;

// A tileable map of ripple slopes for the water: smoothed random heights, their slope in x and
// z in red and green (0.5 = flat). It stands in for the original's grid of random normals.
static texture CreateRippleTexture(IRenderer* gfx)
{
	// four octaves of wrapping value noise (cells of 64, 32, 16 and 8 texels, each half as
	// strong as the one before) over 256 texels; the slopes are scaled so that 99% of them fit
	constexpr int N = 256;
	std::vector<float> h(N * N, 0.0f);
	uint32_t seed = 12345;
	float amplitude = 1.0f;
	for (int cell = 64; cell >= 8; cell /= 2, amplitude *= 0.5f) {
		const int G = N / cell;
		std::vector<float> grid(G * G);
		for (float& v : grid) {
			seed = seed * 1664525u + 1013904223u;
			v = (seed >> 8) / 16777216.0f;
		}
		for (int y = 0; y < N; y++)
			for (int x = 0; x < N; x++) {
				const float fx = (float)x / cell, fy = (float)y / cell;
				const int x0 = (int)fx, y0 = (int)fy;
				float tx = fx - x0, ty = fy - y0;
				tx = tx * tx * tx * (tx * (tx * 6 - 15) + 10);
				ty = ty * ty * ty * (ty * (ty * 6 - 15) + 10);
				auto g = [&](int gx, int gy) { return grid[((gy + G) % G) * G + (gx + G) % G]; };
				const float top = g(x0, y0) + (g(x0 + 1, y0) - g(x0, y0)) * tx;
				const float bottom = g(x0, y0 + 1) + (g(x0 + 1, y0 + 1) - g(x0, y0 + 1)) * tx;
				h[y * N + x] += (top + (bottom - top) * ty) * amplitude * cell;
			}
	}
	std::vector<float> sx(N * N), sz(N * N), mags;
	for (int y = 0; y < N; y++)
		for (int x = 0; x < N; x++) {
			sx[y * N + x] = h[y * N + (x + 1) % N] - h[y * N + (x + N - 1) % N];
			sz[y * N + x] = h[((y + 1) % N) * N + x] - h[((y + N - 1) % N) * N + x];
			mags.push_back(std::max(std::abs(sx[y * N + x]), std::abs(sz[y * N + x])));
		}
	std::nth_element(mags.begin(), mags.begin() + mags.size() * 99 / 100, mags.end());
	const float scale = 110.0f / std::max(mags[mags.size() * 99 / 100], 1e-6f);
	Bitmap bmp;
	bmp.width = bmp.height = N;
	bmp.format = BMFORMAT_R8G8B8A8;
	bmp.pixels.resize(N * N * 4);
	for (int i = 0; i < N * N; i++) {
		uint8_t* px = &bmp.pixels[i * 4];
		px[0] = (uint8_t)std::clamp(128.0f + sx[i] * scale, 0.0f, 255.0f);
		px[1] = (uint8_t)std::clamp(128.0f + sz[i] * scale, 0.0f, 255.0f);
		px[2] = 128;
		px[3] = 255;
	}
	return gfx->CreateTexture(bmp, 0);
}

// The static shadow map: 2048² texels over the square drawn by the scene renderer, 4096² with
// a higher RenderSettings::shadowResolution
static constexpr Uint32 STATIC_SHADOW_MAP_SIZE = 2048;


void DiligentEnhancedTerrainRenderer::init()
{
	DiligentGfx* dgfx = (DiligentGfx*)gfx;
	IRenderDevice* device = dgfx->device;
	_impl = new Impl;
	texcache = new TextureCache(gfx, "Maps\\Map_Textures\\");

	std::map<TerrainTexture*, EnhancedTerrainTexture> textures;
	LoadEnhancedTerrainTextures(terrain, texcache, textures);
	for (const auto& [trntex, et] : textures)
		_impl->textures[trntex] = { et.diffuseMap, et.normalMap };

	std::vector<EnhancedTerrainVertex> trnVertices = BuildEnhancedTerrainVertices(terrain);
	BufferDesc vbDesc;
	vbDesc.Name = "Terrain vertices";
	vbDesc.Size = trnVertices.size() * sizeof(EnhancedTerrainVertex);
	vbDesc.BindFlags = BIND_VERTEX_BUFFER;
	vbDesc.Usage = USAGE_DEFAULT; // updateHeights writes it again
	BufferData vbData{ trnVertices.data(), vbDesc.Size };
	device->CreateBuffer(vbDesc, &vbData, &_impl->trnVertexBuffer);
	if (!_impl->trnVertexBuffer)
		ferr("Diligent: could not create the terrain vertex buffer.");

	BufferDesc cbDesc;
	cbDesc.Name = "Terrain sun";
	cbDesc.Size = 864;
	cbDesc.BindFlags = BIND_UNIFORM_BUFFER;
	cbDesc.Usage = USAGE_DYNAMIC;
	cbDesc.CPUAccessFlags = CPU_ACCESS_WRITE;
	device->CreateBuffer(cbDesc, nullptr, &_impl->sunBuffer);

	// the hull and domain shaders of the tessellated terrain, where the GPU has them
	_impl->tessellation = device->GetDeviceInfo().Features.Tessellation == DEVICE_FEATURE_STATE_ENABLED;
	const SHADER_TYPE tessStages = _impl->tessellation ? (SHADER_TYPE_HULL | SHADER_TYPE_DOMAIN) : SHADER_TYPE_UNKNOWN;
	const PipelineResourceDesc resources[] = {
		{ SHADER_TYPE_VERTEX | SHADER_TYPE_PIXEL | tessStages, "TransformBuffer", 1, SHADER_RESOURCE_TYPE_CONSTANT_BUFFER, SHADER_RESOURCE_VARIABLE_TYPE_STATIC },
		{ SHADER_TYPE_VERTEX | SHADER_TYPE_PIXEL | tessStages, "FogBuffer", 1, SHADER_RESOURCE_TYPE_CONSTANT_BUFFER, SHADER_RESOURCE_VARIABLE_TYPE_STATIC },
		{ SHADER_TYPE_VS_PS | tessStages, "SunBuffer", 1, SHADER_RESOURCE_TYPE_CONSTANT_BUFFER, SHADER_RESOURCE_VARIABLE_TYPE_STATIC },
		{ SHADER_TYPE_VERTEX | SHADER_TYPE_PIXEL | tessStages, "heightTexture", 1, SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_STATIC },
		{ SHADER_TYPE_PIXEL, "inpTexture", 1, SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
		{ SHADER_TYPE_PIXEL, "normalTexture", 1, SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
		{ SHADER_TYPE_PIXEL, "sceneTexture", 1, SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
		{ SHADER_TYPE_PIXEL, "depthTexture", 1, SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
		{ SHADER_TYPE_PIXEL, "skyFront", 1, SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
		{ SHADER_TYPE_PIXEL, "skyRight", 1, SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
		{ SHADER_TYPE_PIXEL, "skyBack", 1, SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
		{ SHADER_TYPE_PIXEL, "skyLeft", 1, SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
		{ SHADER_TYPE_PIXEL, "skyTop", 1, SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
		{ SHADER_TYPE_PIXEL, "cloudTexture", 1, SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_STATIC },
		{ SHADER_TYPE_PIXEL, "shadowTexture", 1, SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_STATIC },
		{ SHADER_TYPE_PIXEL, "blobTexture", 1, SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_STATIC },
		{ SHADER_TYPE_PIXEL | tessStages, "flowTexture", 1, SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_STATIC },
		{ SHADER_TYPE_PIXEL, "riverTexture", 1, SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_STATIC },
		{ SHADER_TYPE_PIXEL, "riverWidth", 1, SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_STATIC },
		{ SHADER_TYPE_PIXEL, "splatDiffuse", 1, SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_STATIC },
		{ SHADER_TYPE_PIXEL, "splatNormal", 1, SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_STATIC },
		{ SHADER_TYPE_PIXEL, "splatCorners", 1, SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_STATIC },
	};
	SamplerDesc sampler; // D3D11's default sampler: trilinear, clamped
	sampler.AddressU = sampler.AddressV = sampler.AddressW = TEXTURE_ADDRESS_CLAMP;
	SamplerDesc cloudSampler; // the cloud map tiles the map
	cloudSampler.AddressU = cloudSampler.AddressV = cloudSampler.AddressW = TEXTURE_ADDRESS_WRAP;
	// anisotropic (RenderSettings::anisotropy): the ground and the water are seen at low angles
	const int aniso = std::clamp(g_renderSettings.anisotropy, 1, 16);
	if (aniso > 1)
		for (SamplerDesc* d : { &sampler, &cloudSampler }) {
			d->MinFilter = d->MagFilter = d->MipFilter = FILTER_TYPE_ANISOTROPIC;
			d->MaxAnisotropy = aniso;
		}
	SamplerDesc shadowSampler;
	shadowSampler.MinFilter = shadowSampler.MagFilter = shadowSampler.MipFilter = FILTER_TYPE_COMPARISON_POINT;
	shadowSampler.ComparisonFunc = COMPARISON_FUNC_LESS_EQUAL;
	shadowSampler.AddressU = shadowSampler.AddressV = shadowSampler.AddressW = TEXTURE_ADDRESS_CLAMP;
	SamplerDesc splatSampler; // the material layers repeat; anisotropic, as they are seen at low angles
	splatSampler.AddressU = splatSampler.AddressV = TEXTURE_ADDRESS_WRAP;
	splatSampler.AddressW = TEXTURE_ADDRESS_CLAMP;
	splatSampler.MinFilter = splatSampler.MagFilter = splatSampler.MipFilter = FILTER_TYPE_ANISOTROPIC;
	splatSampler.MaxAnisotropy = std::max(aniso, 2);
	const ImmutableSamplerDesc samplers[] = { { SHADER_TYPE_PIXEL, "inpSampler", sampler },
		{ SHADER_TYPE_PIXEL, "cloudSampler", cloudSampler }, { SHADER_TYPE_PIXEL, "shadowSampler", shadowSampler },
		{ SHADER_TYPE_PIXEL, "splatSampler", splatSampler } };
	PipelineResourceSignatureDesc sigDesc;
	sigDesc.Name = "Enhanced terrain resources";
	sigDesc.Resources = resources;
	sigDesc.NumResources = (Uint32)std::size(resources);
	sigDesc.ImmutableSamplers = samplers;
	sigDesc.NumImmutableSamplers = (Uint32)std::size(samplers);
	device->CreatePipelineResourceSignature(sigDesc, &_impl->signature);
	if (!_impl->signature)
		ferr("Diligent: could not create the enhanced terrain resource signature.");
	_impl->signature->GetStaticVariableByName(SHADER_TYPE_VERTEX, "TransformBuffer")->Set(dgfx->transformBuffer);
	_impl->signature->GetStaticVariableByName(SHADER_TYPE_PIXEL, "TransformBuffer")->Set(dgfx->transformBuffer); // the reflections' rays
	_impl->signature->GetStaticVariableByName(SHADER_TYPE_VERTEX, "FogBuffer")->Set(dgfx->fogBuffer);
	_impl->signature->GetStaticVariableByName(SHADER_TYPE_PIXEL, "FogBuffer")->Set(dgfx->fogBuffer);
	_impl->signature->GetStaticVariableByName(SHADER_TYPE_PIXEL, "SunBuffer")->Set(_impl->sunBuffer);

	// The splat-map terrain's textures: the layers with their mipmaps (box filtered), and the
	// corners' materials
	{
		const TerrainSplatData splat = BuildTerrainSplat(terrain);
		const Uint32 L = (Uint32)splat.layerSize;
		Uint32 mips = 1;
		while ((L >> mips) > 0)
			mips++;
		auto createArray = [&](const std::vector<std::vector<uint8_t>>& layers, const char* name, RefCntAutoPtr<ITexture>& out) {
			std::vector<std::vector<uint8_t>> levels; // per layer, per mip
			std::vector<TextureSubResData> subres;
			levels.reserve(layers.size() * mips);
			for (const auto& base : layers) {
				levels.push_back(base);
				for (Uint32 m = 1; m < mips; m++) {
					const Uint32 ps = L >> (m - 1), s = L >> m;
					const std::vector<uint8_t>& prev = levels.back();
					std::vector<uint8_t> next(s * s * 4);
					for (Uint32 y = 0; y < s; y++)
						for (Uint32 x = 0; x < s; x++)
							for (int c = 0; c < 4; c++)
								next[(y * s + x) * 4 + c] = (uint8_t)((prev[((2 * y) * ps + 2 * x) * 4 + c] + prev[((2 * y) * ps + 2 * x + 1) * 4 + c]
									+ prev[((2 * y + 1) * ps + 2 * x) * 4 + c] + prev[((2 * y + 1) * ps + 2 * x + 1) * 4 + c] + 2) / 4);
					levels.push_back(std::move(next));
				}
			}
			for (size_t i = 0; i < levels.size(); i++)
				subres.push_back(TextureSubResData{ levels[i].data(), (Uint64)(L >> (i % mips)) * 4 });
			TextureDesc desc;
			desc.Name = name;
			desc.Type = RESOURCE_DIM_TEX_2D_ARRAY;
			desc.Width = desc.Height = L;
			desc.ArraySize = (Uint32)layers.size();
			desc.MipLevels = mips;
			desc.Format = TEX_FORMAT_RGBA8_UNORM;
			desc.BindFlags = BIND_SHADER_RESOURCE;
			desc.Usage = USAGE_IMMUTABLE;
			TextureData data{ subres.data(), (Uint32)subres.size() };
			device->CreateTexture(desc, &data, &out);
			if (!out)
				ferr("Diligent: could not create the %s.", name);
		};
		createArray(splat.diffuse, "Splat terrain materials", _impl->splatDiffuse);
		createArray(splat.normal, "Splat terrain normal maps", _impl->splatNormal);
		TextureDesc cdesc;
		cdesc.Name = "Splat terrain corners";
		cdesc.Type = RESOURCE_DIM_TEX_2D;
		cdesc.Width = terrain->width + 1;
		cdesc.Height = terrain->height + 1;
		cdesc.Format = TEX_FORMAT_R8_UINT;
		cdesc.BindFlags = BIND_SHADER_RESOURCE;
		cdesc.Usage = USAGE_IMMUTABLE;
		TextureSubResData csub{ splat.corners.data(), (Uint64)cdesc.Width };
		TextureData cdata{ &csub, 1 };
		device->CreateTexture(cdesc, &cdata, &_impl->splatCorners);
		if (!_impl->splatCorners)
			ferr("Diligent: could not create the splat terrain corners.");
		_impl->signature->GetStaticVariableByName(SHADER_TYPE_PIXEL, "splatDiffuse")->Set(_impl->splatDiffuse->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
		_impl->signature->GetStaticVariableByName(SHADER_TYPE_PIXEL, "splatNormal")->Set(_impl->splatNormal->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
		_impl->signature->GetStaticVariableByName(SHADER_TYPE_PIXEL, "splatCorners")->Set(_impl->splatCorners->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
	}

	// The vertex heights, for the tessellated terrain (updateHeights writes them again)
	{
		std::vector<float> heights((size_t)(terrain->width + 1) * (terrain->height + 1));
		for (int z = 0; z <= (int)terrain->height; z++)
			for (int x = 0; x <= (int)terrain->width; x++)
				heights[(size_t)z * (terrain->width + 1) + x] = terrain->getVertex(x, z);
		TextureDesc hdesc;
		hdesc.Name = "Terrain heights";
		hdesc.Type = RESOURCE_DIM_TEX_2D;
		hdesc.Width = terrain->width + 1;
		hdesc.Height = terrain->height + 1;
		hdesc.Format = TEX_FORMAT_R32_FLOAT;
		hdesc.BindFlags = BIND_SHADER_RESOURCE;
		hdesc.Usage = USAGE_DEFAULT;
		TextureSubResData hsub{ heights.data(), (Uint64)hdesc.Width * 4 };
		TextureData hdata{ &hsub, 1 };
		device->CreateTexture(hdesc, &hdata, &_impl->heightMap);
		if (!_impl->heightMap)
			ferr("Diligent: could not create the terrain height texture.");
		_impl->signature->GetStaticVariableByName(SHADER_TYPE_VERTEX, "heightTexture")->Set(_impl->heightMap->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
	}

	// The water's flow map: one texel per tile
	{
		const std::vector<uint8_t> flow = BuildWaterFlow(terrain);
		TextureDesc fdesc;
		fdesc.Name = "Water flow";
		fdesc.Type = RESOURCE_DIM_TEX_2D;
		fdesc.Width = terrain->width;
		fdesc.Height = terrain->height;
		fdesc.Format = TEX_FORMAT_RGBA8_UNORM;
		fdesc.BindFlags = BIND_SHADER_RESOURCE;
		fdesc.Usage = USAGE_IMMUTABLE;
		TextureSubResData fsub{ flow.data(), (Uint64)fdesc.Width * 4 };
		TextureData fdata{ &fsub, 1 };
		device->CreateTexture(fdesc, &fdata, &_impl->flowMap);
		if (!_impl->flowMap)
			ferr("Diligent: could not create the water flow map.");
		_impl->signature->GetStaticVariableByName(SHADER_TYPE_PIXEL, "flowTexture")->Set(_impl->flowMap->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
		std::vector<float> halfWidth;
		const std::vector<float> field = BuildRiverField(terrain, flow, &halfWidth);
		TextureDesc rdesc = fdesc;
		rdesc.Name = "River coordinates";
		rdesc.Width = terrain->width * RIVER_FIELD_RES;
		rdesc.Height = terrain->height * RIVER_FIELD_RES;
		rdesc.Format = TEX_FORMAT_RGBA32_FLOAT;
		TextureSubResData rsub{ field.data(), (Uint64)rdesc.Width * 16 };
		TextureData rdata{ &rsub, 1 };
		device->CreateTexture(rdesc, &rdata, &_impl->riverField);
		if (!_impl->riverField)
			ferr("Diligent: could not create the river coordinates.");
		_impl->signature->GetStaticVariableByName(SHADER_TYPE_PIXEL, "riverTexture")->Set(_impl->riverField->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
		rdesc.Name = "River half width";
		rdesc.Format = TEX_FORMAT_R32_FLOAT;
		TextureSubResData wsub{ halfWidth.data(), (Uint64)rdesc.Width * 4 };
		TextureData wdata{ &wsub, 1 };
		device->CreateTexture(rdesc, &wdata, &_impl->riverWidth);
		if (!_impl->riverWidth)
			ferr("Diligent: could not create the river width.");
		_impl->signature->GetStaticVariableByName(SHADER_TYPE_PIXEL, "riverWidth")->Set(_impl->riverWidth->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
	}

	// Cloud shadows: sky_shadow_map.pcx in the map's sky directory; the original falls back to
	// a map of full light when it is missing.
	auto& shadows = dgfx->shadows;
	std::string skyDirectory;
	for (wchar_t c : terrain->skyTextureDirectory) {
		if (!c) break; // the .bcm's string includes its terminator
		skyDirectory += (char)c;
	}
	if (!skyDirectory.empty() && skyDirectory.back() != '\\' && skyDirectory.back() != '/')
		skyDirectory += '\\';
	const std::string cloudMapPath = skyDirectory + "sky_shadow_map.pcx";
	shadows.cloudMap = nullptr;
	if (!terrain->skyTextureDirectory.empty() && FileExists(cloudMapPath.c_str())) {
		_impl->cloudMap = gfx->CreateTexture(Bitmap::loadBitmap(cloudMapPath.c_str()), 1);
		shadows.cloudMap = dgfx->GetTextureView(_impl->cloudMap);
	}
	else
		printf("No cloud shadows: \"%s\" not found\n", cloudMapPath.c_str());
	_impl->signature->GetStaticVariableByName(SHADER_TYPE_PIXEL, "cloudTexture")->Set(dgfx->GetTextureView(_impl->cloudMap));

	TextureDesc smDesc;
	smDesc.Name = "Static shadow map";
	smDesc.Type = RESOURCE_DIM_TEX_2D;
	smDesc.Width = smDesc.Height = STATIC_SHADOW_MAP_SIZE * (g_renderSettings.shadowResolution > 1 ? 2 : 1);
	smDesc.Format = TEX_FORMAT_D32_FLOAT;
	smDesc.BindFlags = BIND_DEPTH_STENCIL | BIND_SHADER_RESOURCE;
	device->CreateTexture(smDesc, nullptr, &_impl->shadowMap);
	if (!_impl->shadowMap)
		ferr("Diligent: could not create the static shadow map.");
	shadows.staticShadowDSV = _impl->shadowMap->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL);
	shadows.staticShadowSRV = _impl->shadowMap->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
	shadows.staticShadowValid = false;
	_impl->signature->GetStaticVariableByName(SHADER_TYPE_PIXEL, "shadowTexture")->Set(shadows.staticShadowSRV);

	// Blob shadow texture over the whole map mesh, edges included
	TextureDesc blobDesc;
	blobDesc.Name = "Blob shadow map";
	blobDesc.Type = RESOURCE_DIM_TEX_2D;
	blobDesc.Width = (Uint32)std::clamp((int)(terrain->width * 5.0f * BLOB_TEXELS_PER_UNIT), 16, 4096);
	blobDesc.Height = (Uint32)std::clamp((int)(terrain->height * 5.0f * BLOB_TEXELS_PER_UNIT), 16, 4096);
	blobDesc.Format = TEX_FORMAT_R8_UNORM;
	blobDesc.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;
	blobDesc.ClearValue.Format = TEX_FORMAT_R8_UNORM;
	blobDesc.ClearValue.Color[0] = 1.0f;
	device->CreateTexture(blobDesc, nullptr, &_impl->blobMap);
	if (!_impl->blobMap)
		ferr("Diligent: could not create the blob shadow map.");
	_impl->signature->GetStaticVariableByName(SHADER_TYPE_PIXEL, "blobTexture")->Set(_impl->blobMap->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
	{
		Bitmap blob = Bitmap::loadBitmap("Warrior Kings Game Set\\Textures\\shadow.pcx").convertToR8G8B8A8();
		_impl->blobSprite = gfx->CreateTexture(blob, 1);
	}
	cbDesc.Name = "Blob shadow constants";
	cbDesc.Size = 16;
	device->CreateBuffer(cbDesc, nullptr, &_impl->blobBuffer);
	const PipelineResourceDesc blobResources[] = {
		{ SHADER_TYPE_VERTEX, "BlobBuffer", 1, SHADER_RESOURCE_TYPE_CONSTANT_BUFFER, SHADER_RESOURCE_VARIABLE_TYPE_STATIC },
		{ SHADER_TYPE_PIXEL, "inpTexture", 1, SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
	};
	const ImmutableSamplerDesc blobSamplers[] = { { SHADER_TYPE_PIXEL, "inpSampler", sampler } };
	PipelineResourceSignatureDesc blobSigDesc;
	blobSigDesc.Name = "Blob shadow resources";
	blobSigDesc.Resources = blobResources;
	blobSigDesc.NumResources = (Uint32)std::size(blobResources);
	blobSigDesc.ImmutableSamplers = blobSamplers;
	blobSigDesc.NumImmutableSamplers = (Uint32)std::size(blobSamplers);
	device->CreatePipelineResourceSignature(blobSigDesc, &_impl->blobSignature);
	if (!_impl->blobSignature)
		ferr("Diligent: could not create the blob shadow resource signature.");
	_impl->blobSignature->GetStaticVariableByName(SHADER_TYPE_VERTEX, "BlobBuffer")->Set(_impl->blobBuffer);
	_impl->blobSignature->CreateShaderResourceBinding(&_impl->blobBinding, true);
	_impl->blobBinding->GetVariableByName(SHADER_TYPE_PIXEL, "inpTexture")->Set(dgfx->GetTextureView(_impl->blobSprite));

	auto createShader = [&](SHADER_TYPE type, const char* entryPoint) {
		ShaderCreateInfo sci;
		sci.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
		sci.Source = tshaderCode;
		sci.SourceLength = sizeof(tshaderCode) - 1;
		sci.EntryPoint = entryPoint;
		sci.Desc.ShaderType = type;
		sci.Desc.Name = entryPoint;
		RefCntAutoPtr<IShader> shader;
		device->CreateShader(sci, &shader);
		if (!shader)
			ferr("Diligent: failed to compile enhanced terrain shader %s", entryPoint);
		return shader;
	};
	RefCntAutoPtr<IShader> vs = createShader(SHADER_TYPE_VERTEX, "VS");
	RefCntAutoPtr<IShader> ps = createShader(SHADER_TYPE_PIXEL, "PS");

	// EnhancedTerrainVertex: position, normal, tangent, bitangent, UV
	static const LayoutElement layout[] = {
		LayoutElement{0, 0, 3, VT_FLOAT32, False},
		LayoutElement{1, 0, 1, VT_UINT32, False},
		LayoutElement{2, 0, 1, VT_UINT32, False},
		LayoutElement{3, 0, 1, VT_UINT32, False},
		LayoutElement{4, 0, 2, VT_FLOAT32, False},
	};
	GraphicsPipelineStateCreateInfo ci;
	ci.PSODesc.Name = "Enhanced terrain";
	ci.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;
	auto& gp = ci.GraphicsPipeline;
	gp.NumRenderTargets = 1;
	gp.RTVFormats[0] = dgfx->swapChain->GetDesc().ColorBufferFormat;
	gp.DSVFormat = dgfx->swapChain->GetDesc().DepthBufferFormat;
	gp.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	// As on D3D11, where the terrain is drawn after the scene: depth state of mesh drawing
	// (LESS_EQUAL with writes), no blending, back faces culled.
	gp.RasterizerDesc.CullMode = CULL_MODE_BACK;
	gp.DepthStencilDesc.DepthEnable = True;
	gp.DepthStencilDesc.DepthWriteEnable = True;
	gp.DepthStencilDesc.DepthFunc = COMPARISON_FUNC_LESS_EQUAL;
	gp.BlendDesc.RenderTargets[0].BlendEnable = False;
	gp.InputLayout.LayoutElements = layout;
	gp.InputLayout.NumElements = (Uint32)std::size(layout);
	ci.pVS = vs;
	ci.pPS = ps;
	IPipelineResourceSignature* signatures[] = { _impl->signature };
	ci.ppResourceSignatures = signatures;
	ci.ResourceSignaturesCount = 1;
	device->CreateGraphicsPipelineState(ci, &_impl->pipeline);
	if (!_impl->pipeline)
		ferr("Diligent: could not create the enhanced terrain pipeline.");
	RefCntAutoPtr<IShader> splatPs = createShader(SHADER_TYPE_PIXEL, "PS_Splat");
	ci.PSODesc.Name = "Splat terrain";
	ci.pPS = splatPs;
	device->CreateGraphicsPipelineState(ci, &_impl->splatPipeline);
	if (!_impl->splatPipeline)
		ferr("Diligent: could not create the splat terrain pipeline.");
	ci.pPS = ps;
	if (_impl->tessellation) {
		// the same with the terrain tessellated: triangles as patches, both faces (the
		// tessellator's winding isn't the index lists')
		RefCntAutoPtr<IShader> tessVs = createShader(SHADER_TYPE_VERTEX, "VS_Tess");
		RefCntAutoPtr<IShader> hs = createShader(SHADER_TYPE_HULL, "HS");
		RefCntAutoPtr<IShader> ds = createShader(SHADER_TYPE_DOMAIN, "DS");
		gp.PrimitiveTopology = PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST;
		gp.RasterizerDesc.CullMode = CULL_MODE_NONE;
		ci.pVS = tessVs;
		ci.pHS = hs;
		ci.pDS = ds;
		ci.PSODesc.Name = "Enhanced terrain, tessellated";
		device->CreateGraphicsPipelineState(ci, &_impl->tessPipeline);
		ci.PSODesc.Name = "Splat terrain, tessellated";
		ci.pPS = splatPs;
		device->CreateGraphicsPipelineState(ci, &_impl->tessSplatPipeline);
		if (!_impl->tessPipeline || !_impl->tessSplatPipeline) {
			printf("Diligent: no tessellated terrain (pipeline failed)\n");
			_impl->tessellation = false;
		}
		ci.pHS = nullptr;
		ci.pDS = nullptr;
		ci.pVS = vs;
		ci.pPS = ps;
		gp.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		gp.RasterizerDesc.CullMode = CULL_MODE_BACK;
	}

	// Water: half transparent over the terrain, depth tested but not written
	RefCntAutoPtr<IShader> lakeVs = createShader(SHADER_TYPE_VERTEX, "VS_Lake");
	RefCntAutoPtr<IShader> lakePs = createShader(SHADER_TYPE_PIXEL, "PS_Lake");
	static const LayoutElement lakeLayout[] = { LayoutElement{0, 0, 3, VT_FLOAT32, False}, LayoutElement{1, 0, 1, VT_FLOAT32, False} };
	ci.PSODesc.Name = "Enhanced terrain water";
	gp.RasterizerDesc.CullMode = CULL_MODE_NONE;
	gp.DepthStencilDesc.DepthWriteEnable = False;
	auto& blend = gp.BlendDesc.RenderTargets[0];
	blend.BlendEnable = True;
	blend.SrcBlend = BLEND_FACTOR_SRC_ALPHA;
	blend.DestBlend = BLEND_FACTOR_INV_SRC_ALPHA;
	blend.SrcBlendAlpha = BLEND_FACTOR_ONE;
	blend.DestBlendAlpha = BLEND_FACTOR_INV_SRC_ALPHA;
	gp.InputLayout.LayoutElements = lakeLayout;
	gp.InputLayout.NumElements = (Uint32)std::size(lakeLayout);
	ci.pVS = lakeVs;
	ci.pPS = lakePs;
	device->CreateGraphicsPipelineState(ci, &_impl->lakePipeline);
	if (!_impl->lakePipeline)
		ferr("Diligent: could not create the enhanced terrain water pipeline.");
	if (_impl->tessellation) {
		RefCntAutoPtr<IShader> lakeTessVs = createShader(SHADER_TYPE_VERTEX, "VS_LakeTess");
		RefCntAutoPtr<IShader> lakeHs = createShader(SHADER_TYPE_HULL, "HS_Lake");
		RefCntAutoPtr<IShader> lakeDs = createShader(SHADER_TYPE_DOMAIN, "DS_Lake");
		RefCntAutoPtr<IShader> lakeHighPs = createShader(SHADER_TYPE_PIXEL, "PS_LakeHigh");
		ci.PSODesc.Name = "Enhanced terrain water, high";
		gp.PrimitiveTopology = PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST;
		ci.pVS = lakeTessVs;
		ci.pHS = lakeHs;
		ci.pDS = lakeDs;
		ci.pPS = lakeHighPs;
		device->CreateGraphicsPipelineState(ci, &_impl->highWaterPipeline);
		if (!_impl->highWaterPipeline)
			printf("Diligent: no high water (pipeline failed)\n");
		ci.pHS = nullptr;
		ci.pDS = nullptr;
		ci.pVS = lakeVs;
		ci.pPS = lakePs;
		gp.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	}
	{
		RefCntAutoPtr<IShader> lakeHighPs = createShader(SHADER_TYPE_PIXEL, "PS_LakeHigh");
		ci.PSODesc.Name = "Enhanced terrain water, medium";
		ci.pPS = lakeHighPs;
		device->CreateGraphicsPipelineState(ci, &_impl->mediumWaterPipeline);
		if (!_impl->mediumWaterPipeline)
			printf("Diligent: no medium water (pipeline failed)\n");
		ci.pPS = lakePs;
	}

	// Grass: opaque blades, both sides, one instance per blade
	RefCntAutoPtr<IShader> grassVs = createShader(SHADER_TYPE_VERTEX, "VS_Grass");
	RefCntAutoPtr<IShader> grassPs = createShader(SHADER_TYPE_PIXEL, "PS_Grass");
	static const LayoutElement grassLayout[] = {
		LayoutElement{ 0, 0, 3, VT_FLOAT32, False, 0, sizeof(GrassBlade), INPUT_ELEMENT_FREQUENCY_PER_INSTANCE, 1 },
		LayoutElement{ 1, 0, 4, VT_UINT8, True, 12, sizeof(GrassBlade), INPUT_ELEMENT_FREQUENCY_PER_INSTANCE, 1 },
		LayoutElement{ 2, 0, 2, VT_FLOAT32, False, 16, sizeof(GrassBlade), INPUT_ELEMENT_FREQUENCY_PER_INSTANCE, 1 } };
	ci.PSODesc.Name = "Enhanced terrain grass";
	gp.RasterizerDesc.CullMode = CULL_MODE_NONE;
	gp.DepthStencilDesc.DepthWriteEnable = True;
	blend.BlendEnable = False;
	gp.InputLayout.LayoutElements = grassLayout;
	gp.InputLayout.NumElements = (Uint32)std::size(grassLayout);
	ci.pVS = grassVs;
	ci.pPS = grassPs;
	device->CreateGraphicsPipelineState(ci, &_impl->grassPipeline);
	if (!_impl->grassPipeline)
		ferr("Diligent: could not create the enhanced terrain grass pipeline.");

	{
		RefCntAutoPtr<IShader> blobVs = createShader(SHADER_TYPE_VERTEX, "VS_Blob");
		RefCntAutoPtr<IShader> blobPs = createShader(SHADER_TYPE_PIXEL, "PS_Blob");
		static const LayoutElement blobLayout[] = {
			LayoutElement{ 0, 0, 2, VT_FLOAT32, False, 0, 24, INPUT_ELEMENT_FREQUENCY_PER_INSTANCE, 1 },
			LayoutElement{ 1, 0, 2, VT_FLOAT32, False, 8, 24, INPUT_ELEMENT_FREQUENCY_PER_INSTANCE, 1 },
			LayoutElement{ 2, 0, 2, VT_FLOAT32, False, 16, 24, INPUT_ELEMENT_FREQUENCY_PER_INSTANCE, 1 },
		};
		GraphicsPipelineStateCreateInfo bci;
		bci.PSODesc.Name = "Blob shadows";
		bci.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;
		auto& bgp = bci.GraphicsPipeline;
		bgp.NumRenderTargets = 1;
		bgp.RTVFormats[0] = TEX_FORMAT_R8_UNORM;
		bgp.DSVFormat = TEX_FORMAT_UNKNOWN;
		bgp.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
		bgp.RasterizerDesc.CullMode = CULL_MODE_NONE;
		bgp.DepthStencilDesc.DepthEnable = False;
		bgp.DepthStencilDesc.DepthWriteEnable = False;
		auto& bb = bgp.BlendDesc.RenderTargets[0];
		bb.BlendEnable = True;
		bb.SrcBlend = BLEND_FACTOR_ZERO; // light * (1 - alpha)
		bb.DestBlend = BLEND_FACTOR_INV_SRC_ALPHA;
		bb.SrcBlendAlpha = BLEND_FACTOR_ZERO;
		bb.DestBlendAlpha = BLEND_FACTOR_ONE;
		bgp.InputLayout.LayoutElements = blobLayout;
		bgp.InputLayout.NumElements = (Uint32)std::size(blobLayout);
		bci.pVS = blobVs;
		bci.pPS = blobPs;
		IPipelineResourceSignature* blobSignatures[] = { _impl->blobSignature };
		bci.ppResourceSignatures = blobSignatures;
		bci.ResourceSignaturesCount = 1;
		device->CreateGraphicsPipelineState(bci, &_impl->blobPipeline);
		if (!_impl->blobPipeline)
			ferr("Diligent: could not create the blob shadow pipeline.");
	}
	_impl->signature->CreateShaderResourceBinding(&_impl->lakeBinding, true);
	const std::string waterPath = skyDirectory + "Water_Surface.tga";
	if (!skyDirectory.empty() && FileExists(waterPath.c_str()))
		_impl->waterTexture = gfx->CreateTexture(Bitmap::loadBitmap(waterPath.c_str()), 0);
	_impl->rippleTexture = CreateRippleTexture(gfx);
	_impl->lakeBinding->GetVariableByName(SHADER_TYPE_PIXEL, "inpTexture")->Set(dgfx->GetTextureView(_impl->waterTexture));
	_impl->lakeBinding->GetVariableByName(SHADER_TYPE_PIXEL, "normalTexture")->Set(dgfx->GetTextureView(_impl->rippleTexture));
	_impl->lakeBinding->GetVariableByName(SHADER_TYPE_PIXEL, "sceneTexture")->Set(dgfx->GetTextureView(nullptr));
	_impl->lakeBinding->GetVariableByName(SHADER_TYPE_PIXEL, "depthTexture")->Set(dgfx->GetTextureView(nullptr));
	for (const char* name : Impl::SKY_NAMES)
		_impl->lakeBinding->GetVariableByName(SHADER_TYPE_PIXEL, name)->Set(dgfx->GetTextureView(nullptr), SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);

	// Chunks
	constexpr int CHUNK = 16;
	constexpr float tilesize = 5.0f;
	std::map<Impl::TexturePair, int> pairIndex;
	struct LakeVertex { Vector3 pos; float depth; };
	std::vector<LakeVertex> lakeVertices;
	for (int cz = 0; cz < (int)terrain->height; cz += CHUNK) {
		for (int cx = 0; cx < (int)terrain->width; cx += CHUNK) {
			Impl::Chunk chunk;
			chunk.low = Vector3(1e9f, 1e9f, 1e9f);
			chunk.high = Vector3(-1e9f, -1e9f, -1e9f);
			std::map<int, std::vector<uint32_t>> perTexture;
			for (int z = cz; z < std::min(cz + CHUNK, (int)terrain->height); z++) {
				for (int x = cx; x < std::min(cx + CHUNK, (int)terrain->width); x++) {
					const TerrainTile* tile = terrain->getTile(x, z);
					const Impl::TexturePair pair = _impl->textures.at(tile->texture);
					auto [it, isNew] = pairIndex.try_emplace(pair, (int)_impl->texturePairs.size());
					if (isNew)
						_impl->texturePairs.push_back(pair);
					const uint32_t first = 4 * (z * terrain->width + x);
					auto& list = perTexture[it->second];
					for (const int& c : { 0,3,1,1,3,2 })
						list.push_back(first + c);
					const float lx = (x - (int)terrain->edge) * tilesize, lz = (z - (int)terrain->edge) * tilesize;
					float top = std::max({ terrain->getVertex(x, z), terrain->getVertex(x + 1, z), terrain->getVertex(x, z + 1), terrain->getVertex(x + 1, z + 1) });
					float bottom = std::min({ terrain->getVertex(x, z), terrain->getVertex(x + 1, z), terrain->getVertex(x, z + 1), terrain->getVertex(x + 1, z + 1) });
					if (tile->fullOfWater) {
						const uint32_t v0 = (uint32_t)lakeVertices.size();
						// the corners at the mean level of the water around them at about this level (a clean
						// step where waters at different heights meet: DiligentWaterfalls)
						const float lv = tile->waterLevel;
						const float w00 = WaterLevelAtCorner(terrain, x, z, lv), w10 = WaterLevelAtCorner(terrain, x + 1, z, lv);
						const float w11 = WaterLevelAtCorner(terrain, x + 1, z + 1, lv), w01 = WaterLevelAtCorner(terrain, x, z + 1, lv);
						lakeVertices.insert(lakeVertices.end(), {
							LakeVertex{ Vector3(lx, w00, lz), w00 - terrain->getVertex(x, z) },
							LakeVertex{ Vector3(lx + tilesize, w10, lz), w10 - terrain->getVertex(x + 1, z) },
							LakeVertex{ Vector3(lx + tilesize, w11, lz + tilesize), w11 - terrain->getVertex(x + 1, z + 1) },
							LakeVertex{ Vector3(lx, w01, lz + tilesize), w01 - terrain->getVertex(x, z + 1) } });
						for (const int& c : { 0,3,1,1,3,2 })
							chunk.lakeIndices.push_back(v0 + c);
						top = std::max({ top, tile->waterLevel, w00, w10, w11, w01 });
					}
					chunk.low = Vector3(std::min(chunk.low.x, lx), std::min(chunk.low.y, bottom), std::min(chunk.low.z, lz));
					chunk.high = Vector3(std::max(chunk.high.x, lx + tilesize), std::max(chunk.high.y, top), std::max(chunk.high.z, lz + tilesize));
				}
			}
			for (auto& [index, list] : perTexture)
				chunk.indicesPerTexture.emplace_back(index, std::move(list));
			chunk.high.y += 1.0f; // the blades above the ground
			_impl->chunks.push_back(std::move(chunk));
		}
	}
	_impl->frameIndices.resize(_impl->texturePairs.size());
	_impl->buildGrass(device, terrain, TerrainGrassBuilder::BuildDensity(g_renderSettings.grassDensity));
	if (!lakeVertices.empty()) {
		BufferDesc lakeDesc;
		lakeDesc.Name = "Terrain water vertices";
		lakeDesc.Size = lakeVertices.size() * sizeof(LakeVertex);
		lakeDesc.BindFlags = BIND_VERTEX_BUFFER;
		lakeDesc.Usage = USAGE_IMMUTABLE;
		BufferData lakeData{ lakeVertices.data(), lakeDesc.Size };
		device->CreateBuffer(lakeDesc, &lakeData, &_impl->lakeVertexBuffer);
	}
	_impl->waterfalls = std::make_unique<DiligentWaterfalls>(dgfx, terrain);
}

void DiligentEnhancedTerrainRenderer::Impl::buildGrass(IRenderDevice* device, Terrain* terrain, float density)
{
	constexpr int CHUNK = 16; // as the chunks were made
	TerrainGrassBuilder grassBuilder;
	std::vector<GrassBlade> grass;
	size_t c = 0;
	for (int cz = 0; cz < (int)terrain->height; cz += CHUNK) {
		for (int cx = 0; cx < (int)terrain->width; cx += CHUNK) {
			Chunk& chunk = chunks.at(c++);
			chunk.grassFirst = (uint32_t)grass.size();
			for (int z = cz; z < std::min(cz + CHUNK, (int)terrain->height); z++)
				for (int x = cx; x < std::min(cx + CHUNK, (int)terrain->width); x++)
					grassBuilder.addTile(terrain, x, z, density, grass);
			chunk.grassCount = (uint32_t)grass.size() - chunk.grassFirst;
		}
	}
	grassBuffer.Release();
	grassBuiltDensity = density;
	if (!grass.empty()) {
		BufferDesc grassDesc;
		grassDesc.Name = "Terrain grass blades";
		grassDesc.Usage = USAGE_IMMUTABLE;
		grassDesc.BindFlags = BIND_VERTEX_BUFFER;
		grassDesc.Size = grass.size() * sizeof(GrassBlade);
		BufferData grassData{ grass.data(), grassDesc.Size };
		device->CreateBuffer(grassDesc, &grassData, &grassBuffer);
		printf("Grass: %zu blades (%.0f per square unit)\n", grass.size(), density);
	}
}

DiligentEnhancedTerrainRenderer::~DiligentEnhancedTerrainRenderer()
{
	if (_impl) {
		auto& shadows = ((DiligentGfx*)gfx)->shadows;
		shadows.cloudMap = nullptr;
		shadows.staticShadowDSV = shadows.staticShadowSRV = nullptr;
		shadows.staticShadowValid = false;
		if (_impl->cloudMap)
			gfx->FreeTexture(_impl->cloudMap);
		if (_impl->waterTexture)
			gfx->FreeTexture(_impl->waterTexture);
		if (_impl->rippleTexture)
			gfx->FreeTexture(_impl->rippleTexture);
		if (_impl->blobSprite)
			gfx->FreeTexture(_impl->blobSprite);
		shadows.blobs.clear();
	}
	delete texcache;
	delete _impl;
}

bool DiligentEnhancedTerrainRenderer::updateHeights()
{
	DiligentGfx* dgfx = (DiligentGfx*)gfx;
	const std::vector<EnhancedTerrainVertex> trnVertices = BuildEnhancedTerrainVertices(terrain);
	const uint64_t size = trnVertices.size() * sizeof(EnhancedTerrainVertex);
	if (size != _impl->trnVertexBuffer->GetDesc().Size)
		return false;
	dgfx->ctx->UpdateBuffer(_impl->trnVertexBuffer, 0, size, trnVertices.data(), RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	if (_impl->heightMap) {
		std::vector<float> heights((size_t)(terrain->width + 1) * (terrain->height + 1));
		for (int z = 0; z <= (int)terrain->height; z++)
			for (int x = 0; x <= (int)terrain->width; x++)
				heights[(size_t)z * (terrain->width + 1) + x] = terrain->getVertex(x, z);
		Box box{ 0, (Uint32)(terrain->width + 1), 0, (Uint32)(terrain->height + 1) };
		TextureSubResData sub{ heights.data(), (Uint64)(terrain->width + 1) * 4 };
		dgfx->ctx->UpdateTexture(_impl->heightMap, 0, 0, box, sub, RESOURCE_STATE_TRANSITION_MODE_TRANSITION, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	}
	// the chunks' boxes: every height the map can have (the edited ones aren't measured again)
	for (Impl::Chunk& chunk : _impl->chunks) {
		chunk.low.y = std::min(chunk.low.y, 0.0f);
		chunk.high.y = std::max(chunk.high.y, 255.0f * terrain->scale);
	}
	if (_impl->waterfalls)
		_impl->waterfalls->rebuild();
	return true;
}

bool DiligentEnhancedTerrainRenderer::updateTiles()
{
	// the texture coordinates (turns, flips, the texture's part of its page) are in the vertices
	if (!updateHeights())
		return false;
	// each chunk's index lists per texture pair again, as init made them (16 × 16 tiles, in rows)
	constexpr int CHUNK = 16;
	std::map<Impl::TexturePair, int> pairIndex;
	for (size_t i = 0; i < _impl->texturePairs.size(); i++)
		pairIndex.emplace(_impl->texturePairs[i], (int)i);
	size_t c = 0;
	for (int cz = 0; cz < (int)terrain->height; cz += CHUNK)
		for (int cx = 0; cx < (int)terrain->width; cx += CHUNK, c++) {
			if (c >= _impl->chunks.size())
				return false;
			std::map<int, std::vector<uint32_t>> perTexture;
			for (int z = cz; z < std::min(cz + CHUNK, (int)terrain->height); z++)
				for (int x = cx; x < std::min(cx + CHUNK, (int)terrain->width); x++) {
					auto tex = _impl->textures.find(terrain->getTile(x, z)->texture);
					if (tex == _impl->textures.end())
						return false;
					auto [it, isNew] = pairIndex.try_emplace(tex->second, (int)_impl->texturePairs.size());
					if (isNew)
						_impl->texturePairs.push_back(tex->second);
					const uint32_t first = 4 * (z * terrain->width + x);
					auto& list = perTexture[it->second];
					for (const int& k : { 0,3,1,1,3,2 })
						list.push_back(first + k);
				}
			auto& chunk = _impl->chunks[c];
			chunk.indicesPerTexture.clear();
			for (auto& [index, list] : perTexture)
				chunk.indicesPerTexture.emplace_back(index, std::move(list));
		}
	_impl->frameIndices.resize(_impl->texturePairs.size());
	return true;
}

bool DiligentEnhancedTerrainRenderer::updateWater()
{
	// the water quads of the flooded tiles and each chunk's list of them, as init made them
	constexpr int CHUNK = 16;
	constexpr float tilesize = 5.0f;
	struct LakeVertex { Vector3 pos; float depth; };
	std::vector<LakeVertex> lakeVertices;
	size_t c = 0;
	for (int cz = 0; cz < (int)terrain->height; cz += CHUNK)
		for (int cx = 0; cx < (int)terrain->width; cx += CHUNK, c++) {
			if (c >= _impl->chunks.size())
				return false;
			auto& chunk = _impl->chunks[c];
			chunk.lakeIndices.clear();
			for (int z = cz; z < std::min(cz + CHUNK, (int)terrain->height); z++)
				for (int x = cx; x < std::min(cx + CHUNK, (int)terrain->width); x++) {
					const TerrainTile* tile = terrain->getTile(x, z);
					if (!tile->fullOfWater)
						continue;
					const float lx = (x - (int)terrain->edge) * tilesize, lz = (z - (int)terrain->edge) * tilesize;
					const uint32_t v0 = (uint32_t)lakeVertices.size();
					const float wl = tile->waterLevel;
					// the corners at the mean level of the water around them at about this level (a clean
					// step where waters at different heights meet: DiligentWaterfalls)
					const float lv = tile->waterLevel;
					const float w00 = WaterLevelAtCorner(terrain, x, z, lv), w10 = WaterLevelAtCorner(terrain, x + 1, z, lv);
					const float w11 = WaterLevelAtCorner(terrain, x + 1, z + 1, lv), w01 = WaterLevelAtCorner(terrain, x, z + 1, lv);
					lakeVertices.insert(lakeVertices.end(), {
						LakeVertex{ Vector3(lx, w00, lz), w00 - terrain->getVertex(x, z) },
						LakeVertex{ Vector3(lx + tilesize, w10, lz), w10 - terrain->getVertex(x + 1, z) },
						LakeVertex{ Vector3(lx + tilesize, w11, lz + tilesize), w11 - terrain->getVertex(x + 1, z + 1) },
						LakeVertex{ Vector3(lx, w01, lz + tilesize), w01 - terrain->getVertex(x, z + 1) } });
					for (const int& k : { 0,3,1,1,3,2 })
						chunk.lakeIndices.push_back(v0 + k);
					chunk.high.y = std::max({ chunk.high.y, wl, w00, w10, w11, w01 });
				}
		}
	_impl->lakeVertexBuffer.Release();
	if (!lakeVertices.empty()) {
		BufferDesc lakeDesc;
		lakeDesc.Name = "Terrain water vertices";
		lakeDesc.Size = lakeVertices.size() * sizeof(LakeVertex);
		lakeDesc.BindFlags = BIND_VERTEX_BUFFER;
		lakeDesc.Usage = USAGE_IMMUTABLE;
		BufferData lakeData{ lakeVertices.data(), lakeDesc.Size };
		((DiligentGfx*)gfx)->device->CreateBuffer(lakeDesc, &lakeData, &_impl->lakeVertexBuffer);
	}
	if (_impl->waterfalls)
		_impl->waterfalls->rebuild();
	return true;
}

void DiligentEnhancedTerrainRenderer::render()
{
	DiligentGfx* dgfx = (DiligentGfx*)gfx;
	IDeviceContext* ctx = dgfx->ctx;

	gfx->BeginMapDrawing();

	constexpr float tilesize = 5.0f;
	Vector3 sunNormal = terrain->sunVector.normal();

	gfx->BeginBatchDrawing();

	void* mapped = nullptr;
	ctx->MapBuffer(_impl->sunBuffer, MAP_WRITE, MAP_FLAG_DISCARD, mapped);
	char* sbdata = (char*)mapped;
	*(Vector3*)(sbdata + 0) = sunNormal;
	*(int*)(sbdata + 12) = (m_bumpOn ? 1 : 0) | (g_renderSettings.terrainOriginalLight ? 2 : 0);
	auto& shadows = dgfx->shadows;
	*(Matrix*)(sbdata + 16) = shadows.staticShadowMatrix.getTranspose();
	float* cloudParams = (float*)(sbdata + 80);
	cloudParams[0] = std::fmod(shadows.time * CLOUD_SCROLL_U, 1.0f);
	cloudParams[1] = std::fmod(shadows.time * CLOUD_SCROLL_V, 1.0f);
	cloudParams[2] = CLOUD_UV_PER_UNIT;
	cloudParams[3] = (shadows.cloudShadows && _impl->cloudMap) ? 1.0f : 0.0f;
	float* shadowParams = (float*)(sbdata + 96);
	shadowParams[0] = shadows.staticShadowValid ? 1.0f : 0.0f;
	shadowParams[1] = g_renderSettings.staticShadowLight;
	shadowParams[2] = 4.0f * g_renderSettings.shadowResolution; // sample points per tile side
	shadowParams[3] = 0.0f;
	float* lakeColour = (float*)(sbdata + 112);
	lakeColour[0] = ((terrain->fogColor >> 16) & 255) / 255.0f;
	lakeColour[1] = ((terrain->fogColor >> 8) & 255) / 255.0f;
	lakeColour[2] = (terrain->fogColor & 255) / 255.0f;
	lakeColour[3] = 0x80 / 255.0f;
	// The texture scroll: the offset moves along the wind vector at the original's speed
	// (0x5F3830: (ms >> 4) / 128 per millisecond), but steadily. The original adds a 4.1 s surge,
	// ((sin + 0.8) * 0.3)^2, which made the water speed up, slow down and swing back, and wraps
	// at 0.5, which jumped the texture by half a repeat; wrapping at 1.0 is seamless.
	// a steady clock: the game time is nudged by the server's time sync, which made the water
	// jump; the water flows on regardless of the game's speed
	const double seconds = SDL_GetTicks() / 1000.0;
	auto scroll = [&](float wx, float wy, float* out) {
		const double k = seconds * (1000.0 / 16.0 / 128.0);
		out[0] = (float)std::fmod(wx * k, 1.0);
		out[1] = (float)std::fmod(wy * k, 1.0);
	};
	float* water = (float*)(sbdata + 128);
	scroll(0.088f, -0.052f, water + 0);
	scroll(0.1f, 0.3f, water + 2);
	scroll(0.9f * 0.25f, 0.34f * 0.25f, water + 4); // the original's normal field wind, slowed for the finer ripples
	scroll(-0.34f * 0.2f, 0.9f * 0.2f, water + 6);
	water[8] = (g_renderSettings.waterQuality >= 1 && _impl->waterTexture) ? 1.0f : 0.0f;
	water[9] = 0.1f;  // one texture repeat per two tiles
	water[10] = 0.04f;
	// refraction from Low up: the scene target copied before the water (not with multisampling)
	_impl->refraction = false;
	if (g_renderSettings.waterQuality >= 2 && !_impl->frameLakeIndices.empty()) {
		ITexture* target = dgfx->CurrentColorTarget()->GetTexture();
		const TextureDesc& td = target->GetDesc();
		if (td.SampleCount == 1) {
			if (!_impl->sceneCopy || _impl->sceneCopy->GetDesc().Width != td.Width || _impl->sceneCopy->GetDesc().Height != td.Height
				|| _impl->sceneCopy->GetDesc().Format != td.Format) {
				TextureDesc cd;
				cd.Name = "Scene behind the water";
				cd.Type = RESOURCE_DIM_TEX_2D;
				cd.Width = td.Width;
				cd.Height = td.Height;
				cd.Format = td.Format;
				cd.BindFlags = BIND_SHADER_RESOURCE;
				cd.Usage = USAGE_DEFAULT;
				_impl->sceneCopy.Release();
				dgfx->device->CreateTexture(cd, nullptr, &_impl->sceneCopy);
				if (_impl->sceneCopy)
					_impl->lakeBinding->GetVariableByName(SHADER_TYPE_PIXEL, "sceneTexture")->Set(_impl->sceneCopy->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE),
						SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
				// the depth: a depth texture that can also be read
				ITexture* depth = dgfx->CurrentDepthTarget() ? dgfx->CurrentDepthTarget()->GetTexture() : nullptr;
				_impl->depthCopy.Release();
				if (depth && depth->GetDesc().SampleCount == 1) {
					TextureDesc dd = cd;
					dd.Name = "Depth behind the water";
					dd.Format = depth->GetDesc().Format;
					dd.BindFlags = BIND_DEPTH_STENCIL | BIND_SHADER_RESOURCE;
					dgfx->device->CreateTexture(dd, nullptr, &_impl->depthCopy);
				}
				if (_impl->depthCopy)
					_impl->lakeBinding->GetVariableByName(SHADER_TYPE_PIXEL, "depthTexture")->Set(_impl->depthCopy->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE),
						SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
			}
			_impl->refraction = _impl->sceneCopy != nullptr;
		}
	}
	water[11] = _impl->refraction ? 1.0f : 0.0f;
	const bool blobsOn = !shadows.blobs.empty();
	float* blobParams = (float*)(sbdata + 176);
	blobParams[0] = blobsOn ? 1.0f : 0.0f;
	blobParams[1] = 1.0f / (terrain->width * tilesize);
	blobParams[2] = 1.0f / (terrain->height * tilesize);
	blobParams[3] = terrain->edge * tilesize;
	float* waterSky = (float*)(sbdata + 192);
	if (g_renderSettings.waterQuality >= 2) {
		const bool skyOn = shadows.skyLightValid && g_renderSettings.sky;
		for (int c = 0; c < 3; c++) {
			const float fog = ((terrain->fogColor >> (16 - 8 * c)) & 255) / 255.0f;
			waterSky[c] = skyOn ? shadows.skyLight[0][c] : fog;
			waterSky[4 + c] = skyOn ? shadows.skyLight[1][c] : fog;
		}
		waterSky[3] = 1.0f;
	}
	else
		std::fill(waterSky, waterSky + 8, 0.0f);
	float* grassParams = (float*)(sbdata + 752);
	grassParams[0] = g_renderSettings.grass ? 1.0f : 0.0f;
	grassParams[1] = std::max(1.0f, g_renderSettings.grassDistance);
	// the waves, the flow and the grass's sway run on the water's steady clock (the game time is
	// nudged by the server's time sync and stops while paused, which made the waves jump or stand)
	grassParams[2] = (float)std::fmod(SDL_GetTicks() / 1000.0, 3600.0);
	grassParams[3] = 0.0f;
	const bool tessOn = _impl->tessellation && g_renderSettings.terrainTessellation > 0;
	g_terrainTessellationActive = tessOn;
	float* tessParams = (float*)(sbdata + 800);
	tessParams[0] = tessOn ? 1.0f : 0.0f;
	tessParams[1] = (float)(2 << g_renderSettings.terrainTessellation); // 4, 8, 16
	// the distance of full detail (to a quarter of it; none from 1.25 times it): longer with the level
	static const float tessReach[4] = { 160.0f, 160.0f, 360.0f, 640.0f };
	tessParams[2] = tessReach[std::clamp(g_renderSettings.terrainTessellation, 0, 3)];
	tessParams[3] = (float)terrain->edge;
	float* tessGrid = (float*)(sbdata + 816);
	tessGrid[0] = (float)terrain->width;
	tessGrid[1] = (float)terrain->height;
	tessGrid[2] = tessGrid[3] = 0.0f;
	float* grassLook = (float*)(sbdata + 784);
	// more blades than were built: build them (a moment's pause, once per step of 6)
	if (g_renderSettings.grass && g_renderSettings.grassDensity > _impl->grassBuiltDensity + 1e-3f)
		_impl->buildGrass(dgfx->device, terrain, TerrainGrassBuilder::BuildDensity(g_renderSettings.grassDensity));
	grassLook[0] = std::clamp(g_renderSettings.grassDensity / std::max(_impl->grassBuiltDensity, 1.0f), 0.0f, 1.0f);
	grassLook[1] = std::max(0.0f, g_renderSettings.grassHeight);
	grassLook[2] = grassLook[3] = 0.0f;
	float* flowParams = (float*)(sbdata + 768);
	flowParams[0] = g_renderSettings.waterFlow ? 1.0f : 0.0f;
	flowParams[1] = 0.7f; // the ripples move at most 0.7 units a second (1.3 stretched them)
	// High water: most pieces per tile edge near the camera and the fade (full detail to a
	// quarter of it, none from 1.25 times it). Ultra: the GPU's most, to 250 and 1250 units
	const bool ultra = g_renderSettings.waterQuality >= 5;
	flowParams[2] = ultra ? 64.0f : 40.0f;
	flowParams[3] = ultra ? 1000.0f : 600.0f;
	// the reflections: Low a short, coarse ray; Medium and High longer, refined; Ultra the finest
	float* reflect = (float*)(sbdata + 832);
	const int wq = g_renderSettings.waterQuality;
	const bool reflections = wq >= 2 && _impl->refraction && _impl->depthCopy && !camera->orthoMode;
	reflect[0] = !reflections ? 0.0f : wq == 2 ? 12.0f : wq <= 4 ? 24.0f : 40.0f;
	reflect[1] = camera->projMatrix.m[2][2];
	reflect[2] = camera->projMatrix.m[3][2];
	reflect[3] = wq == 2 ? 45.0f : wq <= 4 ? 90.0f : 140.0f;
	float* waterMode = (float*)(sbdata + 848);
	waterMode[0] = wq == 2 ? 0.0f : wq <= 4 ? 4.0f : 6.0f;
	const bool highOk = _impl->highWaterPipeline && _impl->waterTexture;
	waterMode[1] = (wq == 3 || (wq >= 4 && !highOk)) && _impl->mediumWaterPipeline && _impl->waterTexture ? 1.0f : 0.0f;
	// the sky box for the reflections, bound again when it changed
	bool skyBox = g_renderSettings.sky && wq >= 2;
	for (int i = 0; i < 5; i++) {
		skyBox = skyBox && shadows.skyFaces[i];
		if (_impl->boundSky[i] != shadows.skyFaces[i]) {
			_impl->boundSky[i] = shadows.skyFaces[i];
			_impl->lakeBinding->GetVariableByName(SHADER_TYPE_PIXEL, Impl::SKY_NAMES[i])->Set(dgfx->GetTextureView(shadows.skyFaces[i]),
				SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
		}
	}
	waterMode[2] = skyBox ? 1.0f : 0.0f;
	waterMode[3] = 0.0f;
	float* fire = (float*)(sbdata + 224);
	const std::vector<float>& fireLights = shadows.fireLights;
	const int numFires = (int)std::min(fireLights.size() / 7, (size_t)DiligentGfx::ShadowEnvironment::MAX_FIRE_LIGHTS);
	std::fill(fire, fire + 4 + 8 * DiligentGfx::ShadowEnvironment::MAX_FIRE_LIGHTS, 0.0f);
	fire[0] = (float)numFires;
	for (int i = 0; i < numFires; i++) {
		std::copy_n(&fireLights[i * 7], 4, fire + 4 + i * 8);
		std::copy_n(&fireLights[i * 7 + 4], 3, fire + 8 + i * 8);
	}
	ctx->UnmapBuffer(_impl->sunBuffer, MAP_WRITE);

	// Blob shadows into the blob shadow map, one instanced quad each
	if (blobsOn) {
		ctx->MapBuffer(_impl->blobBuffer, MAP_WRITE, MAP_FLAG_DISCARD, mapped);
		float* scale = (float*)mapped;
		scale[0] = blobParams[1];
		scale[1] = blobParams[2];
		scale[2] = scale[3] = 0.0f;
		ctx->UnmapBuffer(_impl->blobBuffer, MAP_WRITE);
		ITextureView* rtv = _impl->blobMap->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
		ctx->SetRenderTargets(1, &rtv, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
		ctx->SetViewports(1, nullptr, 0, 0);
		const float white[4] = { 1, 1, 1, 1 };
		ctx->ClearRenderTarget(rtv, white, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
		ctx->SetPipelineState(_impl->blobPipeline);
		ctx->CommitShaderResources(_impl->blobBinding, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
		constexpr size_t maxBlobsPerDraw = 16384; // 384 KiB of the stream buffer
		const size_t numBlobs = shadows.blobs.size() / 6;
		for (size_t first = 0; first < numBlobs; first += maxBlobsPerDraw) {
			const size_t count = std::min(numBlobs - first, maxBlobsPerDraw);
			uint64_t offset = 0;
			IBuffer* ib = dgfx->StreamVertexData(shadows.blobs.data() + first * 6, count * 24, offset);
			ctx->SetVertexBuffers(0, 1, &ib, &offset, RESOURCE_STATE_TRANSITION_MODE_TRANSITION, SET_VERTEX_BUFFERS_FLAG_RESET);
			ctx->Draw(DrawAttribs{ 4, DRAW_FLAG_VERIFY_ALL, (Uint32)count });
		}
		dgfx->RestoreRenderTargets();
		// the terrain samples the map
		StateTransitionDesc barrier{ _impl->blobMap, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE, STATE_TRANSITION_FLAG_UPDATE_STATE };
		ctx->TransitionResourceStates(1, &barrier);
	}
	shadows.blobs.clear();

	// Visible chunks: a chunk is culled when all corners of its bounding box lie outside the same
	// clip plane (row vectors: clip = (x, y, z, 1) * sceneMatrix; depth 0..w).
	const Matrix& m = camera->sceneMatrix;
	auto visible = [&](const Vector3& lo, const Vector3& hi) {
		int outside[6] = {};
		for (int i = 0; i < 8; i++) {
			const float x = (i & 1) ? hi.x : lo.x, y = (i & 2) ? hi.y : lo.y, z = (i & 4) ? hi.z : lo.z;
			const float cx = x * m._11 + y * m._21 + z * m._31 + m._41;
			const float cy = x * m._12 + y * m._22 + z * m._32 + m._42;
			const float cz = x * m._13 + y * m._23 + z * m._33 + m._43;
			const float cw = x * m._14 + y * m._24 + z * m._34 + m._44;
			outside[0] += cx < -cw; outside[1] += cx > cw;
			outside[2] += cy < -cw; outside[3] += cy > cw;
			outside[4] += cz < 0; outside[5] += cz > cw;
		}
		for (int n : outside)
			if (n == 8)
				return false;
		return true;
	};
	for (auto& list : _impl->frameIndices)
		list.clear();
	_impl->frameLakeIndices.clear();
	std::vector<const Impl::Chunk*> grassChunks;
	for (const Impl::Chunk& chunk : _impl->chunks) {
		if (!visible(chunk.low, chunk.high))
			continue;
		if (chunk.grassCount > 0) {
			// within the grass distance of the camera, by the box's nearest point
			const Vector3& cam = camera->position;
			const float dx = std::max({ chunk.low.x - cam.x, 0.0f, cam.x - chunk.high.x });
			const float dz = std::max({ chunk.low.z - cam.z, 0.0f, cam.z - chunk.high.z });
			const float dy = std::max({ chunk.low.y - cam.y, 0.0f, cam.y - chunk.high.y });
			if (dx * dx + dy * dy + dz * dz < g_renderSettings.grassDistance * g_renderSettings.grassDistance)
				grassChunks.push_back(&chunk);
		}
		for (const auto& [index, list] : chunk.indicesPerTexture)
			_impl->frameIndices[index].insert(_impl->frameIndices[index].end(), list.begin(), list.end());
		_impl->frameLakeIndices.insert(_impl->frameLakeIndices.end(), chunk.lakeIndices.begin(), chunk.lakeIndices.end());
	}

	// Draws in blocks that fit in the stream buffer
	auto drawIndices = [&](const std::vector<uint32_t>& indices) {
		constexpr size_t maxIndicesPerDraw = 6 * 8192;
		for (size_t first = 0; first < indices.size(); first += maxIndicesPerDraw) {
			const size_t count = std::min(indices.size() - first, maxIndicesPerDraw);
			uint64_t ibOffset = 0;
			IBuffer* ib = dgfx->StreamIndexData(indices.data() + first, count * sizeof(uint32_t), ibOffset);
			ctx->SetIndexBuffer(ib, ibOffset, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
			ctx->DrawIndexed(DrawIndexedAttribs{ (Uint32)count, VT_UINT32, DRAW_FLAG_VERIFY_ALL });
		}
	};

	IBuffer* vb = _impl->trnVertexBuffer;
	const Uint64 vbOffset = 0;
	ctx->SetVertexBuffers(0, 1, &vb, &vbOffset, RESOURCE_STATE_TRANSITION_MODE_TRANSITION, SET_VERTEX_BUFFERS_FLAG_RESET);
	// the splat pipeline shares the signature: the same bindings per tile texture
	if (g_terrainTessellationActive)
		ctx->SetPipelineState(g_renderSettings.terrainSplat ? _impl->tessSplatPipeline : _impl->tessPipeline);
	else
		ctx->SetPipelineState(g_renderSettings.terrainSplat ? _impl->splatPipeline : _impl->pipeline);
	for (size_t t = 0; t < _impl->texturePairs.size(); t++) {
		if (_impl->frameIndices[t].empty())
			continue;
		const Impl::TexturePair& pair = _impl->texturePairs[t];
		auto& srb = _impl->bindings[pair];
		if (!srb) {
			_impl->signature->CreateShaderResourceBinding(&srb, true);
			srb->GetVariableByName(SHADER_TYPE_PIXEL, "inpTexture")->Set(dgfx->GetTextureView(pair.first));
			srb->GetVariableByName(SHADER_TYPE_PIXEL, "normalTexture")->Set(dgfx->GetTextureView(pair.second));
			srb->GetVariableByName(SHADER_TYPE_PIXEL, "sceneTexture")->Set(dgfx->GetTextureView(nullptr));
			srb->GetVariableByName(SHADER_TYPE_PIXEL, "depthTexture")->Set(dgfx->GetTextureView(nullptr));
			for (const char* name : Impl::SKY_NAMES)
				srb->GetVariableByName(SHADER_TYPE_PIXEL, name)->Set(dgfx->GetTextureView(nullptr));
		}
		ctx->CommitShaderResources(srb, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
		drawIndices(_impl->frameIndices[t]);
	}

	// ----- Grass -----
	if (g_renderSettings.grass && _impl->grassBuffer && !grassChunks.empty()) {
		ctx->SetPipelineState(_impl->grassPipeline);
		IBuffer* gvb = _impl->grassBuffer;
		ctx->SetVertexBuffers(0, 1, &gvb, &vbOffset, RESOURCE_STATE_TRANSITION_MODE_TRANSITION, SET_VERTEX_BUFFERS_FLAG_RESET);
		ctx->CommitShaderResources(_impl->lakeBinding, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
		for (const Impl::Chunk* chunk : grassChunks) {
			DrawAttribs draw;
			draw.NumVertices = 9;
			draw.NumInstances = chunk->grassCount;
			draw.FirstInstanceLocation = chunk->grassFirst;
			draw.Flags = DRAW_FLAG_VERIFY_ALL;
			ctx->Draw(draw);
		}
	}

	// ----- Water -----
	if (_impl->lakeVertexBuffer && !_impl->frameLakeIndices.empty()) {
		if (_impl->refraction) {
			CopyTextureAttribs copy(dgfx->CurrentColorTarget()->GetTexture(), RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
				_impl->sceneCopy, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
			ctx->CopyTexture(copy);
			if (_impl->depthCopy && dgfx->CurrentDepthTarget()) {
				CopyTextureAttribs dcopy(dgfx->CurrentDepthTarget()->GetTexture(), RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
					_impl->depthCopy, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
				ctx->CopyTexture(dcopy);
			}
			dgfx->RestoreRenderTargets();
		}
		// High and Ultra tessellated, Medium (or High without tessellation) flat with High's pixels
		const int wq = g_renderSettings.waterQuality;
		const bool high = wq >= 4 && _impl->highWaterPipeline && _impl->waterTexture;
		const bool medium = !high && wq >= 3 && _impl->mediumWaterPipeline && _impl->waterTexture;
		ctx->SetPipelineState(high ? _impl->highWaterPipeline : medium ? _impl->mediumWaterPipeline : _impl->lakePipeline);
		IBuffer* lvb = _impl->lakeVertexBuffer;
		ctx->SetVertexBuffers(0, 1, &lvb, &vbOffset, RESOURCE_STATE_TRANSITION_MODE_TRANSITION, SET_VERTEX_BUFFERS_FLAG_RESET);
		ctx->CommitShaderResources(_impl->lakeBinding, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
		drawIndices(_impl->frameLakeIndices);
	}
	// ----- Waterfalls, over the water -----
	// (with the frame behind the water when it was copied in this frame)
	if (_impl->waterfalls) {
		const bool copied = _impl->refraction && _impl->lakeVertexBuffer && !_impl->frameLakeIndices.empty();
		_impl->waterfalls->render(camera, copied ? _impl->sceneCopy->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE) : nullptr,
			copied && _impl->depthCopy ? _impl->depthCopy->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE) : nullptr);
	}

	shadows.staticShadowValid = false;
}

