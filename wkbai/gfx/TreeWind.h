// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#pragma once

#include "../util/vecmat.h"
#include <cmath>
#include <cstdint>

// The trees in the wind, with shadows that sway with them. A tree bends in its own model space
// (WindDeform below): the trunk leans downwind and swings, its branches rise, fall and twist,
// its leaves flutter. To keep the ray-traced shadows true without a structure per tree (30,000
// trees: a refit each per frame), the trees are put in wind groups: WIND_YAWS directions they
// face times WIND_PHASES phases of their swing. The trees of a group move exactly alike in their
// own model space, so a tree type has one bent shadow structure per group (DiligentRayShadows::
// windBlas: WIND_GROUPS refits per tree type and frame, whatever the number of trees), and the
// vertex shader bends a tree exactly as its group's structure is bent. A tree's lean is worked out
// for the direction its group faces: off its own by up to half a group's width (22.5 degrees),
// which reads as trees leaning a little differently, as real ones do.
static constexpr uint32_t WIND_YAWS = 8, WIND_PHASES = 4, WIND_GROUPS = WIND_YAWS * WIND_PHASES;
// the group in the instance flags (SceneEntity's free bits 11-15)
static constexpr uint32_t WIND_GROUP_SHIFT = 11;

// A tree's group by its transform (row vectors): the direction its model's x axis faces, the
// nearest of WIND_YAWS, and its phase by where it stands
inline uint32_t WindGroupOf(const Matrix& m)
{
	// (the nearest of 8 directions without atan2, which cost 6 % of a frame with every far tree
	// asking in every pass: the x axis turned on by half a sector, then its octant)
	static_assert(WIND_YAWS == 8, "WindGroupOf works out octants");
	const float x0 = m.m[0][0], z0 = m.m[0][2];
	const float x = x0 * 0.92387953f - z0 * 0.38268343f, z = x0 * 0.38268343f + z0 * 0.92387953f;
	uint32_t y;
	if (z >= 0)
		y = x > 0 ? (z < x ? 0 : 1) : (z > -x ? 2 : 3);
	else
		y = x < 0 ? (-z < -x ? 4 : 5) : (-z > x ? 6 : 7);
	uint32_t h = (uint32_t)(int32_t)std::floor(m.m[3][0] * 0.5f) * 73856093u ^ (uint32_t)(int32_t)std::floor(m.m[3][2] * 0.5f) * 19349663u;
	h ^= h >> 13;
	h *= 0x5bd1e995u;
	h ^= h >> 15;
	return y * WIND_PHASES + h % WIND_PHASES;
}

// HLSL, in the scene's shaders and in the shadow structures' bending pass alike
static const char g_treeWindHlsl[] = R"---(
static const uint WIND_YAWS = 8;
static const uint WIND_PHASES = 4;

// A tree bent in the wind (TreeWind.h), in its model space: a skeleton made on the fly, without
// bones in the mesh. wb: xy the lean in the world (the way it blows times how far a crown 10 m up
// moves, m), z the sway clock (s), w 0 off, 1 sway, 2 sway and the leaves. g: the tree's wind
// group, its lean worked out for the direction the group faces. The trunk is one bone from the
// foot (the model's origin): every vertex moves downwind by the square of its height (a
// cantilever's curve), held to its length (it sinks a little as it leans), swinging on the lean
// at the tree's pace (a tall tree slower; the group's phase) and circling a little across it.
// The branches (the vertices out from the trunk) rise and fall, bend downwind and twist a little,
// each by where it sits around the trunk and how high (neighbouring vertices of a branch alike, so
// it moves as one), the more the stronger the wind. Sway and leaves: the crown's vertices also
// flutter, quickly, each by its own place. Never more than 1 m + 20 % of the height off (the
// shadow structures' bounds count on it).
float3 WindDeform(float3 p, uint g, float4 wb)
{
	if (wb.w <= 0)
		wb = float4(0, 0, 0, 0);
	uint yawIndex = (g / WIND_PHASES) % WIND_YAWS;
	float yaw = yawIndex * (6.2831853 / WIND_YAWS);
	float2 ax = float2(cos(yaw), sin(yaw));
	float2 az = float2(-ax.y, ax.x);
	float2 w = float2(dot(wb.xy, ax), dot(wb.xy, az));
	float seed = frac(((g % WIND_PHASES) + 0.5) / WIND_PHASES + yawIndex * 0.618034);
	float h = max(p.y, 0);
	float hn = h * 0.1;
	float amp = length(w);
	float t = wb.z;
	float pace = 1 / (0.8 + 0.25 * hn);
	float swing = sin(t * 1.7 * pace + seed * 6.283) * 0.4 + sin(t * 2.9 * pace + seed * 17.0) * 0.15;
	float2 off = w * ((1 + swing) * hn * hn);
	off += float2(-w.y, w.x) * (sin(t * 1.3 * pace + seed * 9.1) * 0.15 * hn * hn);
	float len = length(off);
	off *= min(1, 0.18 * h / max(len, 1e-4)); // (no tree leans more than about 10 degrees)
	float3 q = p;
	q.xz += off;
	q.y -= dot(off, off) / (2 * max(h, 0.5));
	float r = length(p.xz);
	if (amp > 1e-4 && r > 0.4) {
		float2 dir = p.xz / r;
		float2 down = w / amp;
		float bph = dot(dir, float2(2.3, 1.7)) * 1.9 + h * 0.45 + seed * 6.283;
		float bw = saturate((r - 0.4) * 0.4) * saturate(hn * 1.5);
		float a = (0.03 + 0.22 * amp) * bw;
		float sway = sin(t * 3.1 + bph) * 0.6 + sin(t * 4.7 + bph * 1.3) * 0.25;
		q.y += sway * a;
		q.xz += down * ((0.5 + 0.5 * sway) * a * 0.8);
		q.xz += float2(-dir.y, dir.x) * (sin(t * 2.3 + bph * 0.7) * a * 0.3);
	}
	if (wb.w > 1.5 && amp > 1e-4) {
		float f = sin(t * 11 + dot(p, float3(1.7, 2.3, 1.3)) + seed * 40) * (0.02 + 0.1 * amp) * saturate(r * 0.4) * saturate(hn * 2);
		q += float3(w.x / amp * 0.5, 1, w.y / amp * 0.5) * f;
	}
	float3 d = q - p;
	float dl = length(d), lim = 1 + 0.2 * h;
	if (dl > lim)
		q = p + d * (lim / dl);
	return q;
}
)---";
