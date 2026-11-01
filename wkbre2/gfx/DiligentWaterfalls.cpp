// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

// Automatic waterfalls (see DiligentWaterfalls.h). Each waterfall is a run of tile edges; per
// corner of the run its profile is traced outwards from the edge: a short stretch on the upper
// water up to the rim, then a parabola (the water leaving the rim at 2.5 units a second) that
// keeps 0.25 units above the ground, down to the lower water. Three things are drawn after the
// water, half transparent, depth tested but not written:
//  1. the sheet: the runs' profiles joined into strips, streaked by noise that scrolls down
//     (slowly over the rim, fast lower down), foamy as it falls and white at its foot;
//  2. the foam on the lower water around the foot, churned by noise drifting away from the
//     fall, ringed by ripples spreading out (Ultra);
//  3. mist and spray: billboards animated entirely on the GPU from a seed per particle (no
//     simulation on the CPU), cut at the lower water's surface and softened against the ground.
// The cascades of painted rivers are drawn first, as a film over the ground (PS_Cascade), with
// the same foam and particles at their foot.

#include "DiligentWaterfalls.h"
#include "renderer_diligent.h"
#include "RenderSettings.h"
#include "../terrain.h"
#include "../Camera.h"
#include "../util/util.h"
#include "../TrnTextureDb.h"
#include <SDL3/SDL_timer.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <map>
#include <random>
#include <string>
#include <tuple>

using namespace Diligent;

namespace {
	// the rim may stand this much above the upper water, and the ground beyond it rise to a ridge
	// this much above it, and the water still pours over (where it is lowest: within SPILLWAY of
	// the water's lowest such ridge). Map makers put mountain tarns behind a low ridge on a cliff's
	// edge (6 Player Tribes Unite: 4 units above the tarn, 35 above the lake below).
	constexpr float MAX_LIP = 4.0f;
	constexpr float MAX_RIDGE = 6.0f;
	constexpr float SPILLWAY = 0.6f;
	// the lower water within this many tiles beyond the edge
	constexpr int MAX_REACH = 5;
	// at least this much drop per unit of horizontal distance (else it is a slope, not a fall)
	constexpr float MIN_STEEPNESS = 0.8f;
	constexpr float TILE = 5.0f;
	// the water leaves the rim at 2.5 units a second: y = top - g / (2 v^2) t^2
	constexpr float FALL_CURVE = 9.8f / (2.0f * 2.5f * 2.5f);
	constexpr float CLEARANCE = 0.25f; // above the ground it runs down
	constexpr float APPROACH = 2.5f;   // the stretch on the upper water before the rim
	constexpr int FALL_SEGMENTS = 18;  // the sheet's pieces from the rim down
	// a painted river tile dropping this much (from its highest to its lowest corner) cascades
	constexpr float CASCADE_DROP = 2.5f;
}

float WaterLevelAtCorner(const Terrain* terrain, int x, int z, float level)
{
	float sum = 0;
	int n = 0;
	for (int dz = -1; dz <= 0; dz++)
		for (int dx = -1; dx <= 0; dx++) {
			const auto* t = terrain->getTile(x + dx, z + dz);
			if (t && t->fullOfWater && std::abs(t->waterLevel - level) < WATERFALL_MIN_DROP) {
				sum += t->waterLevel;
				n++;
			}
		}
	return n ? sum / n : level;
}

// The ground's top at a point: the highest of the flat tile's two triangles (either diagonal),
// its bilinear surface and the tessellated terrain's smooth surface, so that what is drawn just
// above it stays above the ground however the terrain is drawn
static float GroundTop(const Terrain* terrain, float wx, float wz)
{
	const int W = terrain->width, H = terrain->height;
	const float fx = wx / TILE + terrain->edge, fz = wz / TILE + terrain->edge;
	const int x = std::clamp((int)std::floor(fx), 0, W - 1), z = std::clamp((int)std::floor(fz), 0, H - 1);
	const float u = std::clamp(fx - x, 0.0f, 1.0f), v = std::clamp(fz - z, 0.0f, 1.0f);
	const float a = terrain->getVertex(x, z), b = terrain->getVertex(x + 1, z), c = terrain->getVertex(x, z + 1), d = terrain->getVertex(x + 1, z + 1);
	const float diag1 = u > v ? a + (b - a) * u + (d - b) * v : a + (c - a) * v + (d - c) * u;
	const float diag2 = u + v < 1 ? a + (b - a) * u + (c - a) * v : d + (c - d) * (1 - u) + (b - d) * (1 - v);
	return std::max({ diag1, diag2, terrain->getHeight(wx, wz), terrain->getSmoothHeight(wx, wz) });
}

std::vector<Waterfall> FindWaterfalls(const Terrain* terrain)
{
	// every tile edge that the water falls over: its direction, the line it lies on (the
	// vertex column or row), its place along the line, the two levels and the reach
	struct EdgeFall { int dir, line, along; float top, bottom, reach, lip; };
	static const int DIRS[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
	std::vector<EdgeFall> falls;
	for (int tz = 0; tz < terrain->height; tz++)
		for (int tx = 0; tx < terrain->width; tx++) {
			const TerrainTile* t = terrain->getTile(tx, tz);
			if (!t->fullOfWater)
				continue;
			const float level = t->waterLevel;
			for (int dir = 0; dir < 4; dir++) {
				const int dx = DIRS[dir][0], dz = DIRS[dir][1];
				const TerrainTile* n = terrain->getTile(tx + dx, tz + dz);
				// the same water (or a higher one) across the edge
				if (!n || (n->fullOfWater && n->waterLevel > level - WATERFALL_MIN_DROP))
					continue;
				// the edge's corners, at vertex column/row 'line'
				auto edgeCorners = [&](int k, float& h1, float& h2) {
					// the far edge of the k-th tile beyond this one (k = 0: this tile's edge)
					const int ex = tx + dx * k, ez = tz + dz * k;
					if (dx != 0) {
						const int cx = ex + (dx > 0 ? 1 : 0);
						h1 = terrain->getVertex(cx, ez);
						h2 = terrain->getVertex(cx, ez + 1);
						return cx;
					}
					const int cz = ez + (dz > 0 ? 1 : 0);
					h1 = terrain->getVertex(ex, cz);
					h2 = terrain->getVertex(ex + 1, cz);
					return cz;
				};
				float h1, h2;
				const int line = edgeCorners(0, h1, h2);
				const float lip = (h1 + h2) * 0.5f - level;
				if (lip > MAX_LIP)
					continue; // a bank, not a rim
				// beyond the edge: dry ground no higher than the rim, then the lower water
				bool found = false;
				float bottom = 0, ridge = lip;
				int k = 0;
				for (; k < MAX_REACH; k++) {
					const TerrainTile* m = terrain->getTile(tx + dx * (k + 1), tz + dz * (k + 1));
					if (!m)
						break;
					if (m->fullOfWater) {
						found = m->waterLevel <= level - WATERFALL_MIN_DROP;
						bottom = m->waterLevel;
						break;
					}
					float f1, f2;
					edgeCorners(k + 1, f1, f2);
					ridge = std::max(ridge, (f1 + f2) * 0.5f - level);
					if (ridge > MAX_RIDGE)
						break;
				}
				if (!found)
					continue;
				const float reach = k * TILE;
				if ((level - bottom) / std::max(reach, TILE * 0.5f) < MIN_STEEPNESS)
					continue;
				falls.push_back({ dir, line, dx != 0 ? tz : tx, level, bottom, reach, ridge });
			}
		}
	// only the spillways: the edges about as low as each water's lowest ridge (else a lake by a
	// cliff would pour over all of its shore)
	std::map<float, float> lowestLip; // by the upper water's level
	for (const EdgeFall& f : falls) {
		auto [it, isNew] = lowestLip.try_emplace(f.top, f.lip);
		it->second = std::min(it->second, f.lip);
	}
	falls.erase(std::remove_if(falls.begin(), falls.end(), [&](const EdgeFall& f) { return f.lip > lowestLip[f.top] + SPILLWAY; }), falls.end());

	// edges next to each other on a line, falling the same way at about the same levels, are one
	std::sort(falls.begin(), falls.end(), [](const EdgeFall& a, const EdgeFall& b) {
		return std::tie(a.dir, a.line, a.along) < std::tie(b.dir, b.line, b.along);
	});
	std::vector<Waterfall> result;
	for (size_t i = 0; i < falls.size();) {
		size_t j = i + 1;
		while (j < falls.size() && falls[j].dir == falls[i].dir && falls[j].line == falls[i].line
			&& falls[j].along == falls[j - 1].along + 1 && std::abs(falls[j].top - falls[j - 1].top) < 0.5f
			&& std::abs(falls[j].bottom - falls[j - 1].bottom) < 1.0f)
			j++;
		Waterfall w;
		w.dx = DIRS[falls[i].dir][0];
		w.dz = DIRS[falls[i].dir][1];
		w.edges = (int)(j - i);
		const int first = falls[i].along, line = falls[i].line;
		float bottom = 0, reach = 0;
		for (size_t e = i; e < j; e++) {
			bottom += falls[e].bottom;
			reach += falls[e].reach;
		}
		w.bottom = bottom / w.edges;
		w.reach = reach / w.edges;
		for (int c = 0; c <= w.edges; c++) {
			const int vx = w.dx != 0 ? line : first + c, vz = w.dx != 0 ? first + c : line;
			const float top = falls[i + std::min(c, w.edges - 1)].top;
			w.top.push_back(WaterLevelAtCorner(terrain, vx, vz, top));
			w.lip.push_back(terrain->getVertex(vx, vz));
		}
		const int vx0 = w.dx != 0 ? line : first, vz0 = w.dx != 0 ? first : line;
		w.x0 = (vx0 - terrain->edge) * TILE;
		w.z0 = (vz0 - terrain->edge) * TILE;
		result.push_back(std::move(w));
		i = j;
	}
	return result;
}

static const char fallShaderCode[] = R"---(
cbuffer TransformBuffer
{
	matrix Transform;
};

cbuffer FogBuffer
{
	float4 FogColor;
	float FogStartDist;
	float FogEndDist;
	float4 FogCamera;
	float4 Grade;
	float4 EdgeFog;
};

cbuffer FallBuffer
{
	float4 Params;   // a steady clock in seconds, the water quality (2-5), refraction on, depth on
	float4 Sun;      // towards the sun
	float4 CamRight; // the camera's right and up in the world, for the billboards
	float4 CamUp;
	float4 Screen;   // 1 / target width, 1 / height, the projection's P33 and P43
	float4 Tint;     // the falling water's own colour
};

Texture2D sceneTexture;        // the frame behind the water (refraction, from High)
Texture2D<float> depthTexture; // its depth (soft particles)
SamplerState sLinear;

float FogAmount(float3 worldPos, float distanceToCamera)
{
	float distanceFog = clamp((distanceToCamera - FogStartDist) / (FogEndDist - FogStartDist), 0, 1);
	float2 outside = max(-worldPos.xz, worldPos.xz - EdgeFog.xy);
	float edgeFog = saturate((max(outside.x, outside.y) - EdgeFog.z) / max(EdgeFog.w - EdgeFog.z, 1e-3));
	return max(distanceFog, edgeFog);
}

float3 GradeColour(float3 c)
{
	c *= Grade.x;
	c = lerp(dot(c, float3(0.299, 0.587, 0.114)).xxx, c, Grade.w);
	return saturate((c - 0.5) * Grade.z + 0.5 + Grade.y);
}

float Hash(float2 p)
{
	p = frac(p * float2(123.34, 456.21));
	p += dot(p, p + 45.32);
	return frac(p.x * p.y);
}

float Noise(float2 p)
{
	float2 i = floor(p), f = frac(p);
	float2 u = f * f * (3 - 2 * f);
	return lerp(lerp(Hash(i), Hash(i + float2(1, 0)), u.x), lerp(Hash(i + float2(0, 1)), Hash(i + float2(1, 1)), u.x), u.y);
}

float Fbm(float2 p, int octaves)
{
	float sum = 0, amp = 0.5, norm = 0;
	[loop] for (int i = 0; i < octaves; i++) {
		sum += Noise(p) * amp;
		norm += amp;
		p = p * 2.03 + float2(17.1, 5.7);
		amp *= 0.5;
	}
	return sum / norm;
}

struct FALL_OUTPUT
{
	float4 Pos : SV_POSITION;
	float3 World : TEXCOORD0;
	float3 Normal : NORMAL;
	float4 A : TEXCOORD1; // sheet: along the edge, along the fall (units), fall 0-1, distance to the run's ends
	                      // foam: along the run from its middle, out from the foot, half length, radius
	float4 B : TEXCOORD2; // the drop's height, the sheet's fade at the run's ends (foam: a seed), the fall's direction (x, z)
	float Fog : FOG;
};

FALL_OUTPUT VS_Fall(float3 Pos : ATTRIB0, float3 Normal : ATTRIB1, float4 A : ATTRIB2, float4 B : ATTRIB3)
{
	FALL_OUTPUT o;
	// Ultra: the sheet sways a little, more lower down
	if (Params.y >= 5)
		Pos += Normal * (Noise(float2(A.x * 0.45, A.y * 0.25 - Params.x * 1.8)) - 0.5) * 0.3 * A.z;
	o.Pos = mul(float4(Pos, 1), Transform);
	o.World = Pos;
	o.Normal = Normal;
	o.A = A;
	o.B = B;
	o.Fog = FogAmount(Pos, distance(Pos, FogCamera.xyz));
	return o;
}

FALL_OUTPUT VS_Foam(float3 Pos : ATTRIB0, float3 Normal : ATTRIB1, float4 A : ATTRIB2, float4 B : ATTRIB3)
{
	FALL_OUTPUT o;
	o.Pos = mul(float4(Pos, 1), Transform);
	o.World = Pos;
	o.Normal = Normal;
	o.A = A;
	o.B = B;
	o.Fog = FogAmount(Pos, distance(Pos, FogCamera.xyz));
	return o;
}

float3 FoamColour(float3 n)
{
	return float3(0.92, 0.95, 0.97) * (0.72 + 0.33 * saturate(dot(n, Sun.xyz)));
}

float4 PS_Fall(FALL_OUTPUT input) : SV_Target
{
	const float q = Params.y, t = Params.x;
	const float u = input.A.x, v = input.A.y, fall = input.A.z, side = input.A.w;
	const int octaves = q >= 5 ? 4 : q >= 4 ? 3 : 2;
	// two layers of streaks scrolling down: slowly over the rim, fast lower down
	float slow = Fbm(float2(u * 1.3, (v - t * 2.5) * 0.2), octaves);
	float fast = Fbm(float2(u * 0.9 + 3.7, (v - t * 8.0) * 0.07), octaves);
	// how far it has fallen: the water turns white within about 4 units of falling, whatever the
	// fall's height, and whitest at the foot
	const float aerated = saturate(max(fall * input.B.x / 4.0, (fall - 0.7) * 3.0));
	float n = lerp(slow, fast, aerated);
	// foam: little on the glassy rim, more as it falls, white at the foot
	float foam = saturate((n - lerp(0.66, 0.22, aerated)) * 3.0) + saturate((fall - 0.8) * 5.0) * (0.5 + n);
	foam = saturate(foam * saturate(v - 2.5));
	// thinning raggedly at the run's ends, fading in on the upper water
	float alpha = lerp(0.45, 0.85, aerated) + foam * 0.3;
	alpha *= saturate(side / input.B.y + (n - 0.5) * 1.4 - 0.1);
	alpha *= saturate((v - 0.2) / 2.0);
	// from High the falling water tears into strands lower down
	if (q >= 4)
		alpha *= lerp(1, saturate(fast * 2.4 - 0.1), saturate(aerated * 1.4 - 0.25) * (q >= 5 ? 0.5 : 0.3));
	float3 nn = normalize(input.Normal);
	float3 view = normalize(FogCamera.xyz - input.World);
	if (dot(nn, view) < 0)
		nn = -nn;
	float3 water = Tint.rgb * (0.6 + 0.45 * saturate(dot(nn, Sun.xyz)));
	if (Params.z != 0) {
		// what is behind, bent by the streaks and tinted, through the clear water
		float2 suv = input.Pos.xy * Screen.xy + (float2(slow, fast) - 0.5) * 0.035;
		water = lerp(sceneTexture.Sample(sLinear, suv).rgb * Tint.rgb * 1.8, water, 0.65);
	}
	// the sky (the fog's colour) mirrored in the glassy water, more at grazing angles
	float fresnel = 0.25 + 0.6 * pow(1 - saturate(dot(nn, view)), 3);
	water = lerp(water, FogColor.rgb, fresnel * (1 - aerated * 0.7));
	float3 c = lerp(water, FoamColour(nn), foam);
	// the sun's glint on the glassy water near the rim
	float3 h = normalize(view + Sun.xyz);
	c += pow(saturate(dot(nn, h)), 48) * 0.45 * (1 - foam) * saturate(1 - fall * 2);
	c = lerp(c, FogColor.rgb, input.Fog);
	return float4(GradeColour(c), saturate(alpha));
}

// A cascade over the ground (painted rivers): white water carried downhill. The noise moves along
// the flow in two phases blended in turn (so it never stretches), each smeared a little along the
// flow into streaks; the water is clear where it runs slowly and foams where it is steep.
float CascadeNoise(float2 p, float2 dir, float shift, int octaves)
{
	float2 q = p - dir * shift;
	return (Fbm(q, octaves) + Fbm(q - dir * 0.45, octaves) + Fbm(q - dir * 0.9, octaves)) / 3;
}

float4 PS_Cascade(FALL_OUTPUT input) : SV_Target
{
	const float q = Params.y, t = Params.x;
	const float fall = input.A.z, side = input.A.w;
	const int octaves = q >= 5 ? 4 : q >= 4 ? 3 : 2;
	float2 dir = input.B.zw;
	dir = dot(dir, dir) > 1e-4 ? normalize(dir) : float2(1, 0);
	const float speed = 2.5 + 7.0 * fall; // units per second
	const float period = 1.6;
	float phase0 = frac(t / period), phase1 = frac(t / period + 0.5);
	float w0 = 1 - abs(phase0 * 2 - 1);
	float2 p = input.World.xz * 0.9;
	float n0 = CascadeNoise(p, dir, phase0 * period * speed * 0.9, octaves);
	float n1 = CascadeNoise(p + 13.7, dir, phase1 * period * speed * 0.9, octaves);
	float n = lerp(n1, n0, w0);
	// blending two noises flattens them: stretch back around the middle
	n = saturate((n - 0.5) * 1.6 + 0.5);
	float foam = saturate((n - lerp(0.62, 0.28, fall)) * 3.0);
	float alpha = (0.3 + 0.25 * fall + foam * 0.55) * saturate(side / input.B.y + (n - 0.5) * 1.2 - 0.1);
	float3 nn = normalize(input.Normal);
	float3 view = normalize(FogCamera.xyz - input.World);
	float3 water = Tint.rgb * (0.6 + 0.45 * saturate(dot(nn, Sun.xyz)));
	if (Params.z != 0) {
		float2 suv = input.Pos.xy * Screen.xy + (n - 0.5) * 0.008;
		water = lerp(sceneTexture.Sample(sLinear, suv).rgb * Tint.rgb * 1.8, water, 0.5);
	}
	float3 c = lerp(water, FoamColour(nn), foam);
	float3 h = normalize(view + Sun.xyz);
	c += pow(saturate(dot(nn, h)), 48) * 0.3 * (1 - foam);
	c = lerp(c, FogColor.rgb, input.Fog);
	return float4(GradeColour(c), saturate(alpha));
}

float4 PS_Foam(FALL_OUTPUT input) : SV_Target
{
	const float q = Params.y, t = Params.x;
	const float s = input.A.x, w = input.A.y, halfLength = input.A.z, radius = input.A.w;
	// the distance from the foot (behind it, against the cliff, the foam ends sooner)
	float2 d = float2(max(abs(s) - halfLength, 0), w > 0 ? w : -w * 2.5);
	float dist = length(d);
	float r = dist / radius;
	if (r >= 1)
		discard;
	const int octaves = q >= 5 ? 4 : 3;
	float2 away = input.B.zw;
	float churn = Fbm(input.World.xz * 1.1 - away * t * 1.2, octaves);
	float fine = Noise(input.World.xz * 3.3 + float2(t * 0.9, -t * 0.6));
	float foam = saturate(churn * 1.9 - r * 1.3 - 0.05) * (0.55 + 0.6 * fine);
	// ripples spreading out from the foot
	if (q >= 4) {
		float ring = pow(saturate(sin(dist * 2.2 - t * 3.0) * 0.5 + 0.5), 6) * (1 - r) * (0.4 + churn);
		foam = max(foam, ring * (q >= 5 ? 0.85 : 0.5));
	}
	foam *= saturate((1 - r) * 3);
	float3 c = lerp(FoamColour(float3(0, 1, 0)), FogColor.rgb, input.Fog);
	return float4(GradeColour(c), saturate(foam * 0.8));
}

struct PART_OUTPUT
{
	float4 Pos : SV_POSITION;
	float3 World : TEXCOORD0;
	float2 Corner : TEXCOORD1;
	float4 Look : TEXCOORD2; // alpha, kind (0 mist, 1 spray), the lower water's level, size
	float ViewZ : TEXCOORD3;
	float Fog : FOG;
};

PART_OUTPUT VS_Particle(float4 Base : ATTRIB0, float4 Dir : ATTRIB1, uint id : SV_VertexID)
{
	const float seed = Base.w, kind = Dir.w, drop = Dir.z;
	const float2 d = Dir.xy, across = float2(-d.y, d.x);
	const float r1 = frac(seed * 13.17), r2 = frac(seed * 71.31), r3 = frac(seed * 191.7), r4 = frac(seed * 317.3);
	const float life = kind == 0 ? 3.0 + r1 * 2.5 : 0.7 + r1 * 0.6;
	const float age = frac(Params.x / life + seed * 7.31);
	const float tt = age * life;
	const float scale = clamp(sqrt(drop / 4), 0.6, 2.0);
	float3 pos = Base.xyz;
	float size, alpha;
	if (kind == 0) {
		// mist: rising and spreading slowly, growing, fading in and out
		float3 vel = float3(d.x, 0, d.y) * (0.4 + r3) * scale + float3(across.x, 0, across.y) * (r4 - 0.5) * 0.8
			+ float3(0, (0.35 + r2 * 0.8) * scale, 0);
		pos += vel * tt;
		size = (1.0 + age * 3.5) * scale;
		alpha = sin(age * 3.14159) * 0.2;
	}
	else {
		// spray: thrown up and out from the foot, falling back
		float3 vel = float3(d.x, 0, d.y) * (0.8 + r3 * 2.6) * scale + float3(across.x, 0, across.y) * (r4 - 0.5) * 2.5
			+ float3(0, (1.6 + r2 * 3.2) * scale, 0);
		pos += vel * tt + float3(0, -4.9, 0) * tt * tt;
		size = (0.16 + r2 * 0.22) * (1 + age) * scale;
		alpha = (1 - age) * 0.75;
	}
	float2 c = float2((id & 1) ? 1 : -1, (id & 2) ? 1 : -1);
	float3 world = pos + (CamRight.xyz * c.x + CamUp.xyz * c.y) * size;
	PART_OUTPUT o;
	o.Pos = mul(float4(world, 1), Transform);
	o.World = world;
	o.Corner = c;
	o.Look = float4(alpha, kind, Base.y, size);
	o.ViewZ = o.Pos.w;
	o.Fog = FogAmount(world, distance(world, FogCamera.xyz));
	return o;
}

float4 PS_Particle(PART_OUTPUT input) : SV_Target
{
	float r2 = dot(input.Corner, input.Corner);
	if (r2 >= 1)
		discard;
	float a = input.Look.x * (1 - r2) * (1 - r2);
	if (input.Look.y == 0) // mist: wispy
		a *= 0.4 + 1.2 * Noise(input.Corner * 1.7 + input.World.xz * 0.15 + Params.x * 0.2);
	// cut at the lower water's surface, softly
	a *= saturate((input.World.y - input.Look.z) / max(input.Look.w * 0.35, 0.15));
	// softened where it meets the ground and the models
	if (Params.w != 0) {
		float depth = depthTexture.Load(int3(input.Pos.xy, 0));
		float sceneZ = Screen.w / (depth - Screen.z);
		a *= saturate((sceneZ - input.ViewZ) / max(input.Look.w * 0.6, 0.3));
	}
	float light = 0.75 + 0.3 * saturate(Sun.y);
	float3 c = (input.Look.y == 0 ? float3(0.86, 0.9, 0.93) : float3(0.95, 0.97, 1.0)) * light;
	c = lerp(c, FogColor.rgb, input.Fog);
	return float4(GradeColour(c), saturate(a));
}
)---";

namespace {
	struct FallVertex {
		Vector3 pos, normal;
		float a[4], b[4];
	};
	struct Particle {
		float base[4]; // position (at the lower water), seed
		float dir[4];  // fall direction x, z, drop, kind
	};
}

struct DiligentWaterfalls::Impl {
	DiligentGfx* gfx;
	const Terrain* terrain;
	std::vector<Waterfall> falls;
	RefCntAutoPtr<IPipelineResourceSignature> signature;
	RefCntAutoPtr<IShaderResourceBinding> binding;
	RefCntAutoPtr<IPipelineState> fallPipeline, foamPipeline, cascadePipeline, particlePipeline;
	RefCntAutoPtr<IBuffer> buffer, vertices, indices, particles;
	// the index buffer: the sheets, the foam, the cascades
	uint32_t fallIndexCount = 0, foamIndexFirst = 0, foamIndexCount = 0, cascadeIndexFirst = 0, cascadeIndexCount = 0;
	uint32_t mistCount = 0, sprayCount = 0; // the particles: mist first, then spray
	ITextureView* boundScene = nullptr;
	ITextureView* boundDepth = nullptr;

	void build();
};

void DiligentWaterfalls::Impl::build()
{
	falls = FindWaterfalls(terrain);
	std::vector<FallVertex> verts;
	std::vector<uint32_t> fallIndices, foamIndices, cascadeIndices;
	std::vector<Particle> mist, spray;
	std::mt19937 rng(12345);
	std::uniform_real_distribution<float> uni(0.0f, 1.0f);
	float totalLength = 0;
	// at the foot of a fall: the foam on the lower water (a patch around the line from start along
	// 'along' for 'length' units, 'impact' units out in the direction of the fall) and the mist and
	// spray, more for higher falls
	auto emitFoot = [&](Vector3 start, Vector3 along, float ddx, float ddz, float length, float impact, float bottom, float drop, bool foam) {
		if (foam) {
			const float radius = std::clamp(2.5f + 0.3f * drop, 3.0f, 9.0f);
			const uint32_t f0 = (uint32_t)verts.size();
			for (int k = 0; k < 4; k++) {
				const float s = (k & 1) ? length + radius : -radius, out = (k & 2) ? radius : -1.5f;
				FallVertex v;
				v.pos = Vector3(start.x + along.x * s + ddx * (impact + out), bottom + 0.05f, start.z + along.z * s + ddz * (impact + out));
				v.normal = Vector3(0, 1, 0);
				const float a[4] = { s - length * 0.5f, out, length * 0.5f, radius }, b[4] = { drop, uni(rng), ddx, ddz };
				std::copy_n(a, 4, v.a);
				std::copy_n(b, 4, v.b);
				verts.push_back(v);
			}
			foamIndices.insert(foamIndices.end(), { f0, f0 + 1, f0 + 2, f0 + 2, f0 + 1, f0 + 3 });
		}
		const float amount = length * std::clamp(drop / 5.0f, 0.5f, 2.0f);
		auto emit = [&](std::vector<Particle>& list, int count, float kind) {
			for (int i = 0; i < count; i++) {
				const float s = uni(rng) * length, out = impact + uni(rng) * 1.0f - 0.3f;
				Particle p = { { start.x + along.x * s + ddx * out, bottom, start.z + along.z * s + ddz * out, uni(rng) },
					{ ddx, ddz, drop, kind } };
				list.push_back(p);
			}
		};
		emit(mist, std::max(2, (int)(amount * 0.6f)), 0.0f);
		emit(spray, std::max(4, (int)(amount * 1.6f)), 1.0f);
	};
	for (const Waterfall& w : falls) {
		const float ddx = (float)w.dx, ddz = (float)w.dz;
		const Vector3 along = w.dx != 0 ? Vector3(0, 0, 1) : Vector3(1, 0, 0);
		const float length = w.edges * TILE;
		const float drop = std::max(w.top[0], w.lip[0]) - w.bottom;
		float impactSum = 0;
		// the sheet: per corner its profile, resampled to the same number of points
		const uint32_t firstVertex = (uint32_t)verts.size();
		// in columns half a tile apart (a fall one tile wide still has a middle where it is opaque)
		const int perCorner = FALL_SEGMENTS + 2, columns = 2 * w.edges;
		const float fadeWidth = std::min(2.5f, length * 0.4f);
		for (int c = 0; c <= columns; c++) {
			const Vector3 corner(w.x0 + along.x * c * TILE * 0.5f, 0, w.z0 + along.z * c * TILE * 0.5f);
			const int c0 = c / 2, c1 = (c + 1) / 2;
			const float level = (w.top[c0] + w.top[c1]) * 0.5f, lip = (w.lip[c0] + w.lip[c1]) * 0.5f;
			const float top = std::max(level, lip) + 0.08f;
			// over a ridge beyond the rim: the water runs up to its crest and falls from there
			float crestT = 0.0f, crestY = top;
			for (float t = 0.1f; t < w.reach + TILE * 0.5f; t += 0.1f) {
				const float y = GroundTop(terrain, corner.x + ddx * t, corner.z + ddz * t) + CLEARANCE;
				if (y > crestY) {
					crestY = y;
					crestT = t;
				}
			}
			// the fall from the rim: (distance out, height) every 0.1 units
			std::vector<std::pair<float, float>> path = { { 0.0f, top } };
			for (float t = 0.1f;; t += 0.1f) {
				const float ground = GroundTop(terrain, corner.x + ddx * t, corner.z + ddz * t);
				const float free = t < crestT ? top + (crestY - top) * t / crestT : crestY - FALL_CURVE * (t - crestT) * (t - crestT);
				float y = std::max(free, ground + CLEARANCE);
				if (y <= w.bottom - 0.3f || t > w.reach + 15.0f) {
					path.push_back({ t, w.bottom - 0.3f });
					break;
				}
				path.push_back({ t, y });
			}
			impactSum += path.back().first;
			std::vector<float> arc(path.size(), 0.0f);
			for (size_t i = 1; i < path.size(); i++)
				arc[i] = arc[i - 1] + std::hypot(path[i].first - path[i - 1].first, path[i].second - path[i - 1].second);
			auto pointAt = [&](float s, float& out, float& y, float& tdh, float& ty) {
				size_t i = 1;
				while (i + 1 < path.size() && arc[i] < s)
					i++;
				const float seg = std::max(arc[i] - arc[i - 1], 1e-5f);
				const float f = std::clamp((s - arc[i - 1]) / seg, 0.0f, 1.0f);
				out = path[i - 1].first + (path[i].first - path[i - 1].first) * f;
				y = path[i - 1].second + (path[i].second - path[i - 1].second) * f;
				tdh = (path[i].first - path[i - 1].first) / seg;
				ty = (path[i].second - path[i - 1].second) / seg;
			};
			const float u = w.dx != 0 ? corner.z : corner.x;
			const float side = std::min(c, columns - c) * TILE * 0.5f;
			// on the upper water, then the fall
			FallVertex start;
			start.pos = Vector3(corner.x - ddx * APPROACH, level + 0.03f, corner.z - ddz * APPROACH);
			start.normal = Vector3(0, 1, 0);
			const float startA[4] = { u, 0.0f, 0.0f, side }, startB[4] = { drop, fadeWidth, ddx, ddz };
			std::copy_n(startA, 4, start.a);
			std::copy_n(startB, 4, start.b);
			verts.push_back(start);
			const float approachLength = std::hypot(APPROACH, top - level - 0.03f);
			for (int i = 0; i <= FALL_SEGMENTS; i++) {
				const float s = arc.back() * i / FALL_SEGMENTS;
				float out, y, tdh, ty;
				pointAt(s, out, y, tdh, ty);
				if (i == 0) { // leaving the rim level
					tdh = 1.0f;
					ty = 0.0f;
				}
				FallVertex v;
				v.pos = Vector3(corner.x + ddx * out, y, corner.z + ddz * out);
				v.normal = Vector3(ddx * -ty, tdh, ddz * -ty);
				const float a[4] = { u, approachLength + s, std::clamp((crestY - y) / std::max(crestY - w.bottom, 0.1f), 0.0f, 1.0f), side };
				std::copy_n(a, 4, v.a);
				std::copy_n(startB, 4, v.b);
				verts.push_back(v);
			}
		}
		for (int c = 0; c < columns; c++)
			for (int i = 0; i + 1 < perCorner; i++) {
				const uint32_t a = firstVertex + c * perCorner + i, b = a + perCorner;
				fallIndices.insert(fallIndices.end(), { a, b, a + 1, a + 1, b, b + 1 });
			}

		emitFoot(Vector3(w.x0, 0, w.z0), along, ddx, ddz, length, impactSum / (columns + 1), w.bottom, drop, true);
		totalLength += length;
	}

	// Painted rivers (the map's river textures; most of the original's rivers are only these)
	// running down steep ground: cascades over the ground. The tiles drawn are the steep river
	// tiles and the river tiles around them, in 4 x 4 pieces each, a little above both the flat and
	// the tessellated (smooth) ground; opaque at the corners of steep tiles inside the river,
	// fading out towards its banks and where it runs gently again.
	size_t cascadeTiles = 0;
	{
		const int W = terrain->width, H = terrain->height;
		std::vector<int8_t> kind(W * H, 0); // 0 none, 1 river bank (a transition texture), 2 river
		std::vector<float> tileDrop(W * H, 0.0f);
		for (int z = 0; z < H; z++)
			for (int x = 0; x < W; x++) {
				const TerrainTile* t = terrain->getTile(x, z);
				const float hs[4] = { terrain->getVertex(x, z), terrain->getVertex(x + 1, z), terrain->getVertex(x, z + 1), terrain->getVertex(x + 1, z + 1) };
				const float hi = *std::max_element(hs, hs + 4), lo = *std::min_element(hs, hs + 4);
				tileDrop[z * W + x] = hi - lo;
				if (!t->texture || !t->texture->grp || (t->fullOfWater && t->waterLevel >= hi - 0.1f))
					continue; // under a lake
				std::string name = t->texture->grp->name;
				std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return (char)std::toupper(c); });
				if (name.find("RIVER") == std::string::npos && name.find("STREAM") == std::string::npos && name.find("WATERFALL") == std::string::npos)
					continue;
				kind[z * W + x] = name.find('-') == std::string::npos ? 2 : 1;
			}
		auto at = [&](const std::vector<int8_t>& v, int x, int z) { return (x < 0 || z < 0 || x >= W || z >= H) ? (int8_t)0 : v[z * W + x]; };
		std::vector<int8_t> steep(W * H, 0);
		for (int i = 0; i < W * H; i++)
			steep[i] = kind[i] && tileDrop[i] >= CASCADE_DROP;
		// a corner is opaque where all four tiles around it are river and one of them is steep
		auto cornerOpacity = [&](int cx, int cz) {
			bool anySteep = false;
			for (int dz = -1; dz <= 0; dz++)
				for (int dx = -1; dx <= 0; dx++) {
					if (!at(kind, cx + dx, cz + dz))
						return 0.0f;
					anySteep = anySteep || at(steep, cx + dx, cz + dz);
				}
			return anySteep ? 1.0f : 0.0f;
		};
		// the surface: a little above the ground, however it is drawn
		auto surface = [&](float wx, float wz) { return GroundTop(terrain, wx, wz) + 0.2f; };
		constexpr int PIECES = 4;
		for (int z = 0; z < H; z++)
			for (int x = 0; x < W; x++) {
				if (!kind[z * W + x])
					continue;
				bool near = false;
				for (int dz = -1; dz <= 1 && !near; dz++)
					for (int dx = -1; dx <= 1 && !near; dx++)
						near = at(steep, x + dx, z + dz) != 0;
				if (!near)
					continue;
				cascadeTiles++;
				const float o[4] = { cornerOpacity(x, z), cornerOpacity(x + 1, z), cornerOpacity(x, z + 1), cornerOpacity(x + 1, z + 1) };
				const float lx = (x - terrain->edge) * TILE, lz = (z - terrain->edge) * TILE;
				const uint32_t first = (uint32_t)verts.size();
				for (int j = 0; j <= PIECES; j++)
					for (int i = 0; i <= PIECES; i++) {
						const float fu = (float)i / PIECES, fv = (float)j / PIECES;
						const float wx = lx + fu * TILE, wz = lz + fv * TILE;
						const float y = surface(wx, wz);
						// downhill, and how steep
						const float gx = (terrain->getHeight(wx + 0.75f, wz) - terrain->getHeight(wx - 0.75f, wz)) / 1.5f;
						const float gz = (terrain->getHeight(wx, wz + 0.75f) - terrain->getHeight(wx, wz - 0.75f)) / 1.5f;
						const float slope = std::sqrt(gx * gx + gz * gz);
						const float dx = slope > 1e-4f ? -gx / slope : 1.0f, dz = slope > 1e-4f ? -gz / slope : 0.0f;
						const float opacity = (o[0] * (1 - fu) + o[1] * fu) * (1 - fv) + (o[2] * (1 - fu) + o[3] * fu) * fv;
						FallVertex v;
						v.pos = Vector3(wx, y, wz);
						v.normal = Vector3(-gx, 1.0f, -gz).normal();
						// across the flow, down the flow (by the height lost), how steep, opacity
						const float a[4] = { wx * -dz + wz * dx, (300.0f - y) * 1.2f + 3.0f, std::clamp((slope - 0.35f) / 0.8f, 0.0f, 1.0f), opacity * 2.5f };
						const float b[4] = { slope * TILE, 2.5f, dx, dz };
						std::copy_n(a, 4, v.a);
						std::copy_n(b, 4, v.b);
						verts.push_back(v);
					}
				for (int j = 0; j < PIECES; j++)
					for (int i = 0; i < PIECES; i++) {
						const uint32_t a = first + j * (PIECES + 1) + i, c = a + PIECES + 1;
						cascadeIndices.insert(cascadeIndices.end(), { a, c, a + 1, a + 1, c, c + 1 });
					}
			}
		// the foot of each steep tile whose downhill neighbour is gentle river or water: mist and
		// spray there, foam where it is water
		int feet = 0;
		static const int DIRS4[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
		for (int z = 0; z < H; z++)
			for (int x = 0; x < W; x++) {
				if (!steep[z * W + x])
					continue;
				// the lowest of the four neighbours across the tile's edges
				int best = -1;
				float bestH = 1e9f;
				for (int d = 0; d < 4; d++) {
					const int dx = DIRS4[d][0], dz = DIRS4[d][1];
					const float h = (dx != 0) ? (terrain->getVertex(x + (dx > 0), z) + terrain->getVertex(x + (dx > 0), z + 1))
						: (terrain->getVertex(x, z + (dz > 0)) + terrain->getVertex(x + 1, z + (dz > 0)));
					if (h < bestH) {
						bestH = h;
						best = d;
					}
				}
				const int dx = DIRS4[best][0], dz = DIRS4[best][1];
				const TerrainTile* n = terrain->getTile(x + dx, z + dz);
				if (!n || at(steep, x + dx, z + dz))
					continue;
				const bool water = n->fullOfWater && n->waterLevel > bestH * 0.5f - 0.5f;
				if (!water && !at(kind, x + dx, z + dz))
					continue;
				const float edgeX = (x - terrain->edge + (dx > 0 ? 1 : 0)) * TILE, edgeZ = (z - terrain->edge + (dz > 0 ? 1 : 0)) * TILE;
				const Vector3 start = dx != 0 ? Vector3(edgeX, 0, (z - terrain->edge) * TILE) : Vector3((x - terrain->edge) * TILE, 0, edgeZ);
				const Vector3 along = dx != 0 ? Vector3(0, 0, 1) : Vector3(1, 0, 0);
				const float footY = water ? n->waterLevel : bestH * 0.5f + 0.2f;
				emitFoot(start, along, (float)dx, (float)dz, TILE, 0.5f, footY, tileDrop[z * W + x], water);
				if (feet++ < 16)
					printf("  cascade foot at %.0f, %.0f (%.1f high), falling (%d, %d) into %s\n", start.x, start.z, footY, dx, dz, water ? "water" : "river");
			}
	}
	// at most so many: a random part of each (any first part of them is spread evenly, so the
	// lower qualities draw the first few)
	std::shuffle(mist.begin(), mist.end(), rng);
	std::shuffle(spray.begin(), spray.end(), rng);
	mist.resize(std::min(mist.size(), (size_t)4000));
	spray.resize(std::min(spray.size(), (size_t)12000));
	mistCount = (uint32_t)mist.size();
	sprayCount = (uint32_t)spray.size();
	std::vector<Particle> allParticles = std::move(mist);
	allParticles.insert(allParticles.end(), spray.begin(), spray.end());

	fallIndexCount = (uint32_t)fallIndices.size();
	foamIndexFirst = fallIndexCount;
	foamIndexCount = (uint32_t)foamIndices.size();
	fallIndices.insert(fallIndices.end(), foamIndices.begin(), foamIndices.end());
	cascadeIndexFirst = (uint32_t)fallIndices.size();
	cascadeIndexCount = (uint32_t)cascadeIndices.size();
	fallIndices.insert(fallIndices.end(), cascadeIndices.begin(), cascadeIndices.end());
	vertices.Release();
	indices.Release();
	particles.Release();
	if (verts.empty()) {
		printf("Waterfalls: none\n");
		return;
	}
	auto makeBuffer = [&](const char* name, const void* data, size_t size, BIND_FLAGS bind, RefCntAutoPtr<IBuffer>& out) {
		BufferDesc desc;
		desc.Name = name;
		desc.Size = size;
		desc.BindFlags = bind;
		desc.Usage = USAGE_IMMUTABLE;
		BufferData init{ data, desc.Size };
		gfx->device->CreateBuffer(desc, &init, &out);
	};
	makeBuffer("Waterfall vertices", verts.data(), verts.size() * sizeof(FallVertex), BIND_VERTEX_BUFFER, vertices);
	makeBuffer("Waterfall indices", fallIndices.data(), fallIndices.size() * sizeof(uint32_t), BIND_INDEX_BUFFER, indices);
	if (!allParticles.empty())
		makeBuffer("Waterfall particles", allParticles.data(), allParticles.size() * sizeof(Particle), BIND_VERTEX_BUFFER, particles);
	printf("Waterfalls: %zu, %.0f units long; cascades on %zu river tiles; %u mist and %u spray particles\n", falls.size(), totalLength,
		cascadeTiles, mistCount, sprayCount);
	for (size_t i = 0; i < std::min(falls.size(), (size_t)16); i++) {
		const Waterfall& w = falls[i];
		printf("  at %.0f, %.0f falling (%d, %d): %d tiles wide, %.1f down to %.1f\n", w.x0, w.z0, w.dx, w.dz, w.edges,
			std::max(w.top[0], w.lip[0]) - w.bottom, w.bottom);
	}
}

DiligentWaterfalls::DiligentWaterfalls(DiligentGfx* gfx, const Terrain* terrain)
{
	_impl = new Impl;
	_impl->gfx = gfx;
	_impl->terrain = terrain;
	IRenderDevice* device = gfx->device;

	BufferDesc cb;
	cb.Name = "Waterfalls";
	cb.Size = 96;
	cb.BindFlags = BIND_UNIFORM_BUFFER;
	cb.Usage = USAGE_DYNAMIC;
	cb.CPUAccessFlags = CPU_ACCESS_WRITE;
	device->CreateBuffer(cb, nullptr, &_impl->buffer);

	const SHADER_TYPE vsps = SHADER_TYPE_VERTEX | SHADER_TYPE_PIXEL;
	const PipelineResourceDesc resources[] = {
		{ vsps, "TransformBuffer", 1, SHADER_RESOURCE_TYPE_CONSTANT_BUFFER, SHADER_RESOURCE_VARIABLE_TYPE_STATIC },
		{ vsps, "FogBuffer", 1, SHADER_RESOURCE_TYPE_CONSTANT_BUFFER, SHADER_RESOURCE_VARIABLE_TYPE_STATIC },
		{ vsps, "FallBuffer", 1, SHADER_RESOURCE_TYPE_CONSTANT_BUFFER, SHADER_RESOURCE_VARIABLE_TYPE_STATIC },
		{ SHADER_TYPE_PIXEL, "sceneTexture", 1, SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
		{ SHADER_TYPE_PIXEL, "depthTexture", 1, SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
	};
	SamplerDesc linear;
	linear.AddressU = linear.AddressV = linear.AddressW = TEXTURE_ADDRESS_CLAMP;
	const ImmutableSamplerDesc samplers[] = { { SHADER_TYPE_PIXEL, "sLinear", linear } };
	PipelineResourceSignatureDesc sigDesc;
	sigDesc.Name = "Waterfall resources";
	sigDesc.Resources = resources;
	sigDesc.NumResources = (Uint32)std::size(resources);
	sigDesc.ImmutableSamplers = samplers;
	sigDesc.NumImmutableSamplers = (Uint32)std::size(samplers);
	device->CreatePipelineResourceSignature(sigDesc, &_impl->signature);
	if (!_impl->signature)
		ferr("Diligent: could not create the waterfall signature.");
	for (SHADER_TYPE st : { SHADER_TYPE_VERTEX, SHADER_TYPE_PIXEL }) {
		_impl->signature->GetStaticVariableByName(st, "TransformBuffer")->Set(gfx->transformBuffer);
		_impl->signature->GetStaticVariableByName(st, "FogBuffer")->Set(gfx->fogBuffer);
		_impl->signature->GetStaticVariableByName(st, "FallBuffer")->Set(_impl->buffer);
	}
	_impl->signature->CreateShaderResourceBinding(&_impl->binding, true);

	auto shader = [&](SHADER_TYPE type, const char* entry) {
		ShaderCreateInfo sci;
		sci.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
		sci.Source = fallShaderCode;
		sci.SourceLength = sizeof(fallShaderCode) - 1;
		sci.EntryPoint = entry;
		sci.Desc.ShaderType = type;
		sci.Desc.Name = entry;
		RefCntAutoPtr<IShader> s;
		device->CreateShader(sci, &s);
		if (!s)
			ferr("Diligent: failed to compile waterfall shader %s", entry);
		return s;
	};
	GraphicsPipelineStateCreateInfo ci;
	ci.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;
	auto& gp = ci.GraphicsPipeline;
	gp.NumRenderTargets = 1;
	gp.RTVFormats[0] = gfx->swapChain->GetDesc().ColorBufferFormat;
	gp.DSVFormat = gfx->swapChain->GetDesc().DepthBufferFormat;
	gp.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	gp.RasterizerDesc.CullMode = CULL_MODE_NONE;
	gp.DepthStencilDesc.DepthEnable = True;
	gp.DepthStencilDesc.DepthWriteEnable = False;
	auto& blend = gp.BlendDesc.RenderTargets[0];
	blend.BlendEnable = True;
	blend.SrcBlend = BLEND_FACTOR_SRC_ALPHA;
	blend.DestBlend = BLEND_FACTOR_INV_SRC_ALPHA;
	blend.SrcBlendAlpha = BLEND_FACTOR_ONE;
	blend.DestBlendAlpha = BLEND_FACTOR_INV_SRC_ALPHA;
	IPipelineResourceSignature* signatures[] = { _impl->signature };
	ci.ppResourceSignatures = signatures;
	ci.ResourceSignaturesCount = 1;

	const Uint32 stride = sizeof(FallVertex);
	const LayoutElement meshLayout[] = {
		LayoutElement{ 0, 0, 3, VT_FLOAT32, False, 0, stride },
		LayoutElement{ 1, 0, 3, VT_FLOAT32, False, 12, stride },
		LayoutElement{ 2, 0, 4, VT_FLOAT32, False, 24, stride },
		LayoutElement{ 3, 0, 4, VT_FLOAT32, False, 40, stride } };
	gp.InputLayout.LayoutElements = meshLayout;
	gp.InputLayout.NumElements = (Uint32)std::size(meshLayout);
	RefCntAutoPtr<IShader> fallVs = shader(SHADER_TYPE_VERTEX, "VS_Fall"), fallPs = shader(SHADER_TYPE_PIXEL, "PS_Fall");
	ci.PSODesc.Name = "Waterfall sheet";
	ci.pVS = fallVs;
	ci.pPS = fallPs;
	device->CreateGraphicsPipelineState(ci, &_impl->fallPipeline);
	RefCntAutoPtr<IShader> foamVs = shader(SHADER_TYPE_VERTEX, "VS_Foam"), foamPs = shader(SHADER_TYPE_PIXEL, "PS_Foam");
	ci.PSODesc.Name = "Waterfall foam";
	ci.pVS = foamVs;
	ci.pPS = foamPs;
	device->CreateGraphicsPipelineState(ci, &_impl->foamPipeline);
	RefCntAutoPtr<IShader> cascadePs = shader(SHADER_TYPE_PIXEL, "PS_Cascade");
	ci.PSODesc.Name = "Waterfall cascades";
	ci.pPS = cascadePs;
	device->CreateGraphicsPipelineState(ci, &_impl->cascadePipeline);

	const Uint32 pstride = sizeof(Particle);
	const LayoutElement particleLayout[] = {
		LayoutElement{ 0, 0, 4, VT_FLOAT32, False, 0, pstride, INPUT_ELEMENT_FREQUENCY_PER_INSTANCE, 1 },
		LayoutElement{ 1, 0, 4, VT_FLOAT32, False, 16, pstride, INPUT_ELEMENT_FREQUENCY_PER_INSTANCE, 1 } };
	gp.InputLayout.LayoutElements = particleLayout;
	gp.InputLayout.NumElements = (Uint32)std::size(particleLayout);
	gp.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
	RefCntAutoPtr<IShader> partVs = shader(SHADER_TYPE_VERTEX, "VS_Particle"), partPs = shader(SHADER_TYPE_PIXEL, "PS_Particle");
	ci.PSODesc.Name = "Waterfall mist and spray";
	ci.pVS = partVs;
	ci.pPS = partPs;
	device->CreateGraphicsPipelineState(ci, &_impl->particlePipeline);
	if (!_impl->fallPipeline || !_impl->foamPipeline || !_impl->cascadePipeline || !_impl->particlePipeline)
		ferr("Diligent: could not create the waterfall pipelines.");

	_impl->build();
}

DiligentWaterfalls::~DiligentWaterfalls()
{
	delete _impl;
}

void DiligentWaterfalls::rebuild()
{
	_impl->build();
}

size_t DiligentWaterfalls::count() const
{
	return _impl->falls.size();
}

void DiligentWaterfalls::render(const Camera* camera, ITextureView* scene, ITextureView* depth)
{
	const int quality = g_renderSettings.waterQuality;
	if (quality < 2 || !_impl->vertices)
		return;
	DiligentGfx* gfx = _impl->gfx;
	IDeviceContext* ctx = gfx->ctx;

	// what is behind the sheet only from High; the depth for the soft particles from Medium
	ITextureView* sceneView = quality >= 4 ? scene : nullptr;
	ITextureView* depthView = quality >= 3 ? depth : nullptr;
	ITextureView* white = gfx->GetTextureView(nullptr);
	if (_impl->boundScene != sceneView) {
		_impl->binding->GetVariableByName(SHADER_TYPE_PIXEL, "sceneTexture")->Set(sceneView ? sceneView : white, SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
		_impl->boundScene = sceneView;
	}
	if (_impl->boundDepth != depthView) {
		_impl->binding->GetVariableByName(SHADER_TYPE_PIXEL, "depthTexture")->Set(depthView ? depthView : white, SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
		_impl->boundDepth = depthView;
	}

	const Matrix& view = camera->viewMatrix;
	const Matrix& proj = camera->projMatrix;
	const Vector3 sun = _impl->terrain->sunVector.normal();
	ITexture* target = gfx->CurrentColorTarget()->GetTexture();
	const float tint[3] = { 0.42f, 0.55f, 0.58f };
	void* mapped = nullptr;
	ctx->MapBuffer(_impl->buffer, MAP_WRITE, MAP_FLAG_DISCARD, mapped);
	const float data[24] = {
		(float)std::fmod(SDL_GetTicks() / 1000.0, 3600.0), (float)quality, sceneView ? 1.0f : 0.0f, (depthView && !camera->orthoMode) ? 1.0f : 0.0f,
		sun.x, sun.y, sun.z, 0,
		view._11, view._21, view._31, 0,
		view._12, view._22, view._32, 0,
		1.0f / target->GetDesc().Width, 1.0f / target->GetDesc().Height, proj._33, proj._43,
		tint[0], tint[1], tint[2], 1,
	};
	memcpy(mapped, data, sizeof(data));
	ctx->UnmapBuffer(_impl->buffer, MAP_WRITE);

	IBuffer* vb = _impl->vertices;
	const Uint64 offset = 0;
	ctx->SetVertexBuffers(0, 1, &vb, &offset, RESOURCE_STATE_TRANSITION_MODE_TRANSITION, SET_VERTEX_BUFFERS_FLAG_RESET);
	ctx->SetIndexBuffer(_impl->indices, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	// the cascades over the ground, then the foam on the lower water (from Medium), under the sheet
	if (_impl->cascadeIndexCount) {
		ctx->SetPipelineState(_impl->cascadePipeline);
		ctx->CommitShaderResources(_impl->binding, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
		DrawIndexedAttribs draw{ _impl->cascadeIndexCount, VT_UINT32, DRAW_FLAG_VERIFY_ALL };
		draw.FirstIndexLocation = _impl->cascadeIndexFirst;
		ctx->DrawIndexed(draw);
	}
	if (quality >= 3 && _impl->foamIndexCount) {
		ctx->SetPipelineState(_impl->foamPipeline);
		ctx->CommitShaderResources(_impl->binding, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
		DrawIndexedAttribs draw{ _impl->foamIndexCount, VT_UINT32, DRAW_FLAG_VERIFY_ALL };
		draw.FirstIndexLocation = _impl->foamIndexFirst;
		ctx->DrawIndexed(draw);
	}
	if (_impl->fallIndexCount) {
		ctx->SetPipelineState(_impl->fallPipeline);
		ctx->CommitShaderResources(_impl->binding, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
		ctx->DrawIndexed(DrawIndexedAttribs{ _impl->fallIndexCount, VT_UINT32, DRAW_FLAG_VERIFY_ALL });
	}

	// mist from Medium, spray from High: a part of them by the quality
	const float mistShare = quality >= 5 ? 1.0f : quality == 4 ? 0.6f : quality == 3 ? 0.3f : 0.0f;
	const float sprayShare = quality >= 5 ? 1.0f : quality == 4 ? 0.35f : 0.0f;
	const uint32_t mist = (uint32_t)(_impl->mistCount * mistShare), spray = (uint32_t)(_impl->sprayCount * sprayShare);
	if (_impl->particles && (mist || spray)) {
		ctx->SetPipelineState(_impl->particlePipeline);
		IBuffer* pb = _impl->particles;
		ctx->SetVertexBuffers(0, 1, &pb, &offset, RESOURCE_STATE_TRANSITION_MODE_TRANSITION, SET_VERTEX_BUFFERS_FLAG_RESET);
		ctx->CommitShaderResources(_impl->binding, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
		auto drawRange = [&](uint32_t first, uint32_t count) {
			if (!count)
				return;
			DrawAttribs draw;
			draw.NumVertices = 4;
			draw.NumInstances = count;
			draw.FirstInstanceLocation = first;
			draw.Flags = DRAW_FLAG_VERIFY_ALL;
			ctx->Draw(draw);
		};
		drawRange(0, mist);
		drawRange(_impl->mistCount, spray);
	}
}
