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
#include <algorithm>
#include <cmath>
#include <map>
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
	float4 WaterParams; // on, UV per world unit, ripple UV per world unit
	float4 BlobParams; // on, 1 / map width, 1 / map depth, map edge in world units
	// RenderSettings::waterDetail: the sky box's colour above (w: on) and at the horizon, for
	// the water's reflection (ShadowEnvironment::skyLight), else the fog colour
	float4 WaterSkyUp;
	float4 WaterSkyHorizon;
	float4 FireLightCount; // x: how many of FireLights are in use
	float4 FireLights[32]; // per light: position and radius, colour
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

struct LAKE_OUTPUT
{
	float4 Pos : SV_POSITION;
	float3 World : COLOR1;
	float Depth : TEXCOORD0;
	float Fog : FOG;
};

LAKE_OUTPUT VS_Lake(float3 Pos : ATTRIB0, float Depth : ATTRIB1)
{
	LAKE_OUTPUT output;
	output.Pos = mul(float4(Pos, 1), Transform);
	output.World = Pos;
	output.Depth = Depth;
	output.Fog = FogAmount(Pos, distance(Pos, FogCamera.xyz));
	return output;
}

// The water surface (docs/re/water.md). The original scrolls its texture layers along two wind
// vectors and lights each tile corner with a sun glint off a scrolling field of normals. Here
// per pixel: the map's Water_Surface.tga in two scrolling layers, two layers of a ripple normal
// map, a sun glint, the sky (fog) colour reflected at grazing angles, and more transparency
// where the water is shallow. With RenderSettings::waterDetail also: the sky box's colours
// reflected along the view, a darker colour with depth, cloud shadows (on all but the glint)
// and foam along the shore, broken up by the ripples.
float4 PS_Lake(LAKE_OUTPUT input) : SV_Target
{
	if (WaterParams.x == 0)
		return float4(GradeColour(lerp(LakeColour.rgb, FogColor.rgb, input.Fog)), LakeColour.a);
	float2 uv = input.World.xz * WaterParams.y;
	float3 tex = 0.5 * (inpTexture.Sample(cloudSampler, uv + WaterLayers.xy).rgb + inpTexture.Sample(cloudSampler, uv + WaterLayers.zw).rgb);
	float2 ruv = input.World.xz * WaterParams.z;
	float2 slope = normalTexture.Sample(cloudSampler, ruv + WaterRipples.xy).xy + normalTexture.Sample(cloudSampler, ruv * 1.7 + WaterRipples.zw).yx;
	float3 n = normalize(float3(slope.x - 1, 1, slope.y - 1));
	float3 v = normalize(FogCamera.xyz - input.World);
	float3 r = reflect(-v, n);
	float glint = pow(saturate(dot(r, SunDirection)), 64) * 0.7 + pow(saturate(dot(r, SunDirection)), 3) * 0.06;
	float fresnel = 0.02 + 0.98 * pow(1 - saturate(dot(n, v)), 5);
	float3 sky = FogColor.rgb;
	float foam = 0;
	if (WaterSkyUp.w != 0) {
		sky = lerp(WaterSkyHorizon.rgb, WaterSkyUp.rgb, saturate(r.y));
		tex *= lerp(1.0, float3(0.55, 0.7, 0.8), saturate(input.Depth / 6));
		float cloud = CloudLight(input.World);
		tex *= cloud;
		sky *= lerp(1, cloud, 0.5);
		// foam: a band where the water is shallower than 0.8 units, its edge moved by the ripples
		float edge = input.Depth + (slope.x + slope.y - 2) * 0.35;
		foam = saturate(1 - edge / 0.8) * saturate(input.Depth * 4) * 0.55;
	}
	float3 c = lerp(lerp(tex, sky, fresnel * 0.6), float3(0.92, 0.95, 0.96), foam) + glint;
	float alpha = saturate(lerp(0.3, 0.8, saturate(input.Depth / 3)) + fresnel * 0.3 + glint + foam);
	return float4(GradeColour(lerp(c, FogColor.rgb, input.Fog)), alpha);
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
	float ha = 0.5 / (256 >> (int)ceil(lod));

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

// A material layer at a ground position (p in world units, tile-aligned): one of its four tiles
// (the layer's quadrants), picked per 5-unit cell by a hash, with the gradients of the continuous
// position so the mip level doesn't jump at the cells' borders
float4 SampleVariant(Texture2DArray<float4> layers, float2 p, uint layer)
{
	float2 cell = floor(p / 5);
	uint q = (uint)(Hash2(cell + 17.0).x * 4) & 3;
	float2 uv = (float2(q & 1, q >> 1) + (p / 5 - cell)) * 0.5;
	float2 g = p / 10;
	return layers.SampleGrad(splatSampler, float3(uv, layer), ddx(g), ddy(g));
}

uint4 CornerLayers(int2 c0)
{
	return uint4(splatCorners.Load(int3(c0, 0)), splatCorners.Load(int3(c0 + int2(1, 0), 0)),
		splatCorners.Load(int3(c0 + int2(0, 1), 0)), splatCorners.Load(int3(c0 + int2(1, 1), 0)));
}

float4 PS_Splat(VS_OUTPUT input) : SV_Target
{
	// the tile's own corners decide whether it keeps its picture; the blend looks the corners up
	// at a position warped by noise, so that the materials' borders don't follow the tile grid
	uint4 own4 = CornerLayers((int2)floor((input.FragPos.xz + BlobParams.w) / 5));
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
	// the tile's own picture where its corners agree
	float2 fuv = AtlasUV(input.Texcoord);
	float4 own = inpTexture.Sample(inpSampler, fuv);
	float3 ownNrm = normalTexture.Sample(inpSampler, fuv).xyz * 2 - 1;
	bool oneMaterial = all(own4 == own4.xxxx);
	if (oneMaterial && steep == 0)
		return ShadeTerrain(input, own, ownNrm, input.Tangent, input.Bitangent);
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
	if (oneMaterial) { // the tile's own picture on gentler ground, the projection on cliffs
		tex = lerp(own, tex, steep);
		nrm = normalize(lerp(ownNrm, nrm, steep));
		return ShadeTerrain(input, tex, nrm, input.Tangent, input.Bitangent);
	}
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
		std::vector<std::pair<int, std::vector<uint32_t>>> indicesPerTexture; // texture pair index
		std::vector<uint32_t> lakeIndices;
	};
	std::vector<Chunk> chunks;
	std::vector<TexturePair> texturePairs;
	std::vector<std::vector<uint32_t>> frameIndices; // per texture pair, this frame
	std::vector<uint32_t> frameLakeIndices;
	RefCntAutoPtr<IBuffer> lakeVertexBuffer;
	RefCntAutoPtr<IPipelineState> lakePipeline;
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
};

// Texels of the blob shadow texture per world unit (at most 4096 per side)
static constexpr float BLOB_TEXELS_PER_UNIT = 2.0f;

// A tileable map of ripple slopes for the water: smoothed random heights, their slope in x and
// z in red and green (0.5 = flat). It stands in for the original's grid of random normals.
static texture CreateRippleTexture(IRenderer* gfx)
{
	constexpr int N = 64;
	std::vector<float> h(N * N);
	uint32_t seed = 12345;
	for (float& v : h) {
		seed = seed * 1664525u + 1013904223u;
		v = (seed >> 8) / 16777216.0f;
	}
	for (int pass = 0; pass < 3; pass++) { // box blur with wrapping
		std::vector<float> o(N * N);
		for (int y = 0; y < N; y++)
			for (int x = 0; x < N; x++) {
				float sum = 0;
				for (int dy = -1; dy <= 1; dy++)
					for (int dx = -1; dx <= 1; dx++)
						sum += h[((y + dy + N) % N) * N + (x + dx + N) % N];
				o[y * N + x] = sum / 9;
			}
		h = std::move(o);
	}
	Bitmap bmp;
	bmp.width = bmp.height = N;
	bmp.format = BMFORMAT_R8G8B8A8;
	bmp.pixels.resize(N * N * 4);
	for (int y = 0; y < N; y++)
		for (int x = 0; x < N; x++) {
			const float sx = h[y * N + (x + 1) % N] - h[y * N + (x + N - 1) % N];
			const float sz = h[((y + 1) % N) * N + x] - h[((y + N - 1) % N) * N + x];
			uint8_t* px = &bmp.pixels[(y * N + x) * 4];
			px[0] = (uint8_t)std::clamp(128.0f + sx * 1200.0f, 0.0f, 255.0f);
			px[1] = (uint8_t)std::clamp(128.0f + sz * 1200.0f, 0.0f, 255.0f);
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
	cbDesc.Size = 752;
	cbDesc.BindFlags = BIND_UNIFORM_BUFFER;
	cbDesc.Usage = USAGE_DYNAMIC;
	cbDesc.CPUAccessFlags = CPU_ACCESS_WRITE;
	device->CreateBuffer(cbDesc, nullptr, &_impl->sunBuffer);

	const PipelineResourceDesc resources[] = {
		{ SHADER_TYPE_VERTEX, "TransformBuffer", 1, SHADER_RESOURCE_TYPE_CONSTANT_BUFFER, SHADER_RESOURCE_VARIABLE_TYPE_STATIC },
		{ SHADER_TYPE_VERTEX | SHADER_TYPE_PIXEL, "FogBuffer", 1, SHADER_RESOURCE_TYPE_CONSTANT_BUFFER, SHADER_RESOURCE_VARIABLE_TYPE_STATIC },
		{ SHADER_TYPE_PIXEL, "SunBuffer", 1, SHADER_RESOURCE_TYPE_CONSTANT_BUFFER, SHADER_RESOURCE_VARIABLE_TYPE_STATIC },
		{ SHADER_TYPE_PIXEL, "inpTexture", 1, SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
		{ SHADER_TYPE_PIXEL, "normalTexture", 1, SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
		{ SHADER_TYPE_PIXEL, "cloudTexture", 1, SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_STATIC },
		{ SHADER_TYPE_PIXEL, "shadowTexture", 1, SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_STATIC },
		{ SHADER_TYPE_PIXEL, "blobTexture", 1, SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_STATIC },
		{ SHADER_TYPE_PIXEL, "splatDiffuse", 1, SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_STATIC },
		{ SHADER_TYPE_PIXEL, "splatNormal", 1, SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_STATIC },
		{ SHADER_TYPE_PIXEL, "splatCorners", 1, SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_STATIC },
	};
	SamplerDesc sampler; // D3D11's default sampler: trilinear, clamped
	sampler.AddressU = sampler.AddressV = sampler.AddressW = TEXTURE_ADDRESS_CLAMP;
	SamplerDesc cloudSampler; // the cloud map tiles the map
	cloudSampler.AddressU = cloudSampler.AddressV = cloudSampler.AddressW = TEXTURE_ADDRESS_WRAP;
	SamplerDesc shadowSampler;
	shadowSampler.MinFilter = shadowSampler.MagFilter = shadowSampler.MipFilter = FILTER_TYPE_COMPARISON_POINT;
	shadowSampler.ComparisonFunc = COMPARISON_FUNC_LESS_EQUAL;
	shadowSampler.AddressU = shadowSampler.AddressV = shadowSampler.AddressW = TEXTURE_ADDRESS_CLAMP;
	SamplerDesc splatSampler; // the material layers repeat; anisotropic, as they are seen at low angles
	splatSampler.AddressU = splatSampler.AddressV = TEXTURE_ADDRESS_WRAP;
	splatSampler.AddressW = TEXTURE_ADDRESS_CLAMP;
	splatSampler.MinFilter = splatSampler.MagFilter = splatSampler.MipFilter = FILTER_TYPE_ANISOTROPIC;
	splatSampler.MaxAnisotropy = 8;
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
	_impl->signature->GetStaticVariableByName(SHADER_TYPE_VERTEX, "FogBuffer")->Set(dgfx->fogBuffer);
	_impl->signature->GetStaticVariableByName(SHADER_TYPE_PIXEL, "FogBuffer")->Set(dgfx->fogBuffer);
	_impl->signature->GetStaticVariableByName(SHADER_TYPE_PIXEL, "SunBuffer")->Set(_impl->sunBuffer);

	// The splat-map terrain's textures: the layers with their mipmaps (box filtered), and the
	// corners' materials
	{
		const TerrainSplatData splat = BuildTerrainSplat(terrain);
		constexpr Uint32 L = TerrainSplatData::LAYER_SIZE;
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
						const float wl = tile->waterLevel;
						lakeVertices.insert(lakeVertices.end(), {
							LakeVertex{ Vector3(lx, wl, lz), wl - terrain->getVertex(x, z) },
							LakeVertex{ Vector3(lx + tilesize, wl, lz), wl - terrain->getVertex(x + 1, z) },
							LakeVertex{ Vector3(lx + tilesize, wl, lz + tilesize), wl - terrain->getVertex(x + 1, z + 1) },
							LakeVertex{ Vector3(lx, wl, lz + tilesize), wl - terrain->getVertex(x, z + 1) } });
						for (const int& c : { 0,3,1,1,3,2 })
							chunk.lakeIndices.push_back(v0 + c);
						top = std::max(top, tile->waterLevel);
					}
					chunk.low = Vector3(std::min(chunk.low.x, lx), std::min(chunk.low.y, bottom), std::min(chunk.low.z, lz));
					chunk.high = Vector3(std::max(chunk.high.x, lx + tilesize), std::max(chunk.high.y, top), std::max(chunk.high.z, lz + tilesize));
				}
			}
			for (auto& [index, list] : perTexture)
				chunk.indicesPerTexture.emplace_back(index, std::move(list));
			_impl->chunks.push_back(std::move(chunk));
		}
	}
	_impl->frameIndices.resize(_impl->texturePairs.size());
	if (!lakeVertices.empty()) {
		BufferDesc lakeDesc;
		lakeDesc.Name = "Terrain water vertices";
		lakeDesc.Size = lakeVertices.size() * sizeof(LakeVertex);
		lakeDesc.BindFlags = BIND_VERTEX_BUFFER;
		lakeDesc.Usage = USAGE_IMMUTABLE;
		BufferData lakeData{ lakeVertices.data(), lakeDesc.Size };
		device->CreateBuffer(lakeDesc, &lakeData, &_impl->lakeVertexBuffer);
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
	// the chunks' boxes: every height the map can have (the edited ones aren't measured again)
	for (Impl::Chunk& chunk : _impl->chunks) {
		chunk.low.y = std::min(chunk.low.y, 0.0f);
		chunk.high.y = std::max(chunk.high.y, 255.0f * terrain->scale);
	}
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
					lakeVertices.insert(lakeVertices.end(), {
						LakeVertex{ Vector3(lx, wl, lz), wl - terrain->getVertex(x, z) },
						LakeVertex{ Vector3(lx + tilesize, wl, lz), wl - terrain->getVertex(x + 1, z) },
						LakeVertex{ Vector3(lx + tilesize, wl, lz + tilesize), wl - terrain->getVertex(x + 1, z + 1) },
						LakeVertex{ Vector3(lx, wl, lz + tilesize), wl - terrain->getVertex(x, z + 1) } });
					for (const int& k : { 0,3,1,1,3,2 })
						chunk.lakeIndices.push_back(v0 + k);
					chunk.high.y = std::max(chunk.high.y, wl);
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
	water[8] = (g_renderSettings.waterSurface && _impl->waterTexture) ? 1.0f : 0.0f;
	water[9] = 0.1f;  // one texture repeat per two tiles
	water[10] = 0.04f;
	water[11] = 0.0f;
	const bool blobsOn = !shadows.blobs.empty();
	float* blobParams = (float*)(sbdata + 176);
	blobParams[0] = blobsOn ? 1.0f : 0.0f;
	blobParams[1] = 1.0f / (terrain->width * tilesize);
	blobParams[2] = 1.0f / (terrain->height * tilesize);
	blobParams[3] = terrain->edge * tilesize;
	float* waterSky = (float*)(sbdata + 192);
	if (g_renderSettings.waterDetail) {
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
	for (const Impl::Chunk& chunk : _impl->chunks) {
		if (!visible(chunk.low, chunk.high))
			continue;
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
		}
		ctx->CommitShaderResources(srb, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
		drawIndices(_impl->frameIndices[t]);
	}

	// ----- Water -----
	if (_impl->lakeVertexBuffer && !_impl->frameLakeIndices.empty()) {
		ctx->SetPipelineState(_impl->lakePipeline);
		IBuffer* lvb = _impl->lakeVertexBuffer;
		ctx->SetVertexBuffers(0, 1, &lvb, &vbOffset, RESOURCE_STATE_TRANSITION_MODE_TRANSITION, SET_VERTEX_BUFFERS_FLAG_RESET);
		ctx->CommitShaderResources(_impl->lakeBinding, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
		drawIndices(_impl->frameLakeIndices);
	}

	shadows.staticShadowValid = false;
}

