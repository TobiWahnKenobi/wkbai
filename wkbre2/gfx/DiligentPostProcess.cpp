// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

// Post-processing of the 3D scene: ambient occlusion and bloom (see DiligentPostProcess.h).
// Passes, each a full-screen triangle:
//  1. AO, half size: the view-space position from the depth, its normal from the neighbouring
//     positions, 12 samples on a spiral within the radius, rotated per pixel; fades out with the
//     distance (the far view is fogged anyway) and is 1 on the sky.
//  2. Bright pass, half size: what is brighter than the threshold.
//  3. and 4. A 9-tap Gaussian blur of it, across and down.
//  5. Composite into the back buffer: the scene times the AO (blurred by four bilinear taps), plus
//     the bloom.
//  6. With FXAA, the composite goes into a target first, and FXAA (after Lottes' FXAA 3.11
//     quality: the edge's direction from the luma around, a search along it for its ends,
//     the pixel blended across it by its place on the edge, plus a subpixel blend) writes it
//     into the back buffer.
//  7. With TAA (the camera jittered by a sub-pixel Halton (2, 3) offset every frame, 8 steps),
//     the composite is blended into the previous frames' history, found by reprojecting the
//     pixel through its depth (the nearest depth around it, so edges move with the nearer
//     object); the history is clipped to the 3 x 3 neighbourhood's colour spread in YCoCg
//     (mean +- 1.25 sigma), which keeps moving units and particles from leaving trails.
//  8. With FSR (AMD FidelityFX Super Resolution 1, after ffx_fsr1.h, MIT licence), the scene is
//     drawn smaller and EASU scales the anti-aliased result up to the back buffer's size: a
//     12-tap filter around the pixel, a Lanczos-like kernel stretched along the local edge's
//     direction, clamped to the 4 nearest texels. RCAS then sharpens it (also after TAA without
//     FSR): the most sharpening that doesn't clip the 4 neighbours, less where it looks like noise.
//  9. With FSR 3.1 (instead of 6. to 8.), the motion vectors: the moving objects' as the scene
//     renderer drew them, elsewhere the camera's motion through the depth (where the point was
//     in the last frame). The reactive mask, where FSR trusts the current frame over its
//     history: 0.6 where the water changed the scene (since copyOpaque: it animates, but less
//     makes its surface shimmer with the jitter), 0.9 where the waterfalls and particles did
//     (since copyAfterWater; faint sparks would leave trails). Then DiligentFsr3 anti-aliases
//     and upscales the composite, and a copy puts its result into the back buffer.

#include "DiligentPostProcess.h"
#include "DiligentFsr3.h"
#include <memory>
#include "../util/util.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>
#include <string>
#include <vector>

using namespace Diligent;

static const char postShaderCode[] = R"---(
cbuffer PostBuffer
{
	float4 Proj;  // P11, P22, P33, P43
	float4 Texel; // 1 / full width, 1 / full height, 1 / half width, 1 / half height
	float4 Ao;    // radius, strength
	float4 Bloom; // threshold, intensity, blur direction (texels)
	float4 FogX;  // fog of war: world x of a view-space point (the inverse view's first column)
	float4 FogZ;  // and its world z
	float4 Fog;   // 1 / (5 m x tiles across), 1 / (5 m x tiles down), brightness where unseen, on
	float4 Easu;  // FSR: input size / output size, 0.5 x that - 0.5
	float4 Sizes; // the scene's width and height, the output's
	float4 Rcas;  // sharpening (FSR's exp2(-stops)), the sharpened image's width and height
	float4 Taa;   // history valid, the current frame's weight
	matrix TaaInverseView;  // view space -> world (row vectors)
	matrix TaaPrevViewProj; // world -> the previous frame's clip space, unjittered
	float4 Motion; // FSR 3: the jitter in UV, the object motion's "none" (NO_OBJECT_MOTION)
};
Texture2D tColor;
Texture2D tDepth;
Texture2D tAo;
Texture2D tBloom;
Texture2D tResolved; // the composite, for FXAA
Texture2D tFog;      // fog of war: per 5 m tile, 1 where the player's units see
Texture2D tHistory;  // TAA: the previous frames
Texture2D tEasu;     // FSR: the image to scale up
Texture2D tSharpen;  // RCAS: the image to sharpen
Texture2D tObjectMotion; // FSR 3: the moving objects' motion vectors
Texture2D tCopy;     // FSR 3's result, for the back buffer
Texture2D tOpaque;   // FSR 3: the scene before its water
Texture2D tWater;    // and before its other transparent parts
SamplerState sLinear;
SamplerState sPoint;

struct VS_OUTPUT
{
	float4 Pos : SV_POSITION;
	float2 Uv : TEXCOORD0;
};

VS_OUTPUT VS(uint id : SV_VertexID)
{
	VS_OUTPUT o;
	float2 p = float2((id << 1) & 2, id & 2) * 2 - 1;
	o.Pos = float4(p, 0, 1);
	o.Uv = float2(p.x * 0.5 + 0.5, 0.5 - p.y * 0.5);
	return o;
}

float3 ViewPosAt(float2 uv, float d)
{
	float z = Proj.w / (d - Proj.z);
	float2 ndc = float2(uv.x * 2 - 1, 1 - uv.y * 2);
	return float3(ndc.x * z / Proj.x, ndc.y * z / Proj.y, z);
}

float3 ViewPos(float2 uv)
{
	float d = tDepth.SampleLevel(sPoint, uv, 0).r;
	float z = Proj.w / (d - Proj.z);
	float2 ndc = float2(uv.x * 2 - 1, 1 - uv.y * 2);
	return float3(ndc.x * z / Proj.x, ndc.y * z / Proj.y, z);
}

float4 PS_Ao(VS_OUTPUT input) : SV_Target
{
	float d = tDepth.SampleLevel(sPoint, input.Uv, 0).r;
	if (d >= 0.99999)
		return float4(1, 1, 1, 1);
	float3 p = ViewPos(input.Uv);
	float3 px = ViewPos(input.Uv + float2(Texel.z, 0)) - p;
	float3 py = ViewPos(input.Uv + float2(0, Texel.w)) - p;
	float3 n = normalize(cross(py, px));
	if (dot(n, p) > 0)
		n = -n;
	float radius = Ao.x;
	// the radius on the screen, as a part of the screen's height (uv)
	float screenRadius = radius * Proj.y / p.z * 0.5;
	float noise = frac(52.9829189 * frac(dot(input.Pos.xy, float2(0.06711056, 0.00583715))));
	float occlusion = 0;
	const int SAMPLES = 12;
	[unroll] for (int i = 0; i < SAMPLES; i++) {
		float t = (i + 0.5) / SAMPLES;
		float angle = (i * 2.39996 + noise * 6.28318);
		float2 offset = float2(cos(angle), sin(angle)) * t * screenRadius;
		offset.x *= Proj.x / Proj.y;
		float3 s = ViewPos(input.Uv + offset);
		float3 v = s - p;
		float dist = length(v);
		float range = saturate(1 - (dist - radius) / radius);
		occlusion += max(0, dot(v, n) / max(dist, 1e-4) - 0.22) * range;
	}
	float ao = saturate(1 - occlusion / SAMPLES * 1.6);
	ao = lerp(ao, 1, saturate((p.z - 120) / 180));
	return float4(ao, ao, ao, 1);
}

float4 PS_Bright(VS_OUTPUT input) : SV_Target
{
	float3 c = tColor.SampleLevel(sLinear, input.Uv, 0).rgb;
	float bright = max(c.r, max(c.g, c.b));
	float w = saturate((bright - Bloom.x) / max(1 - Bloom.x, 1e-3));
	return float4(c * w, 1);
}

float4 PS_Blur(VS_OUTPUT input) : SV_Target
{
	static const float weights[5] = { 0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216 };
	float2 step = Bloom.zw * Texel.zw;
	float3 sum = tBloom.SampleLevel(sLinear, input.Uv, 0).rgb * weights[0];
	[unroll] for (int i = 1; i < 5; i++) {
		sum += tBloom.SampleLevel(sLinear, input.Uv + step * i, 0).rgb * weights[i];
		sum += tBloom.SampleLevel(sLinear, input.Uv - step * i, 0).rgb * weights[i];
	}
	return float4(sum, 1);
}

float FxaaLuma(float2 uv)
{
	return dot(tResolved.SampleLevel(sLinear, uv, 0).rgb, float3(0.299, 0.587, 0.114));
}

float4 PS_Fxaa(VS_OUTPUT input) : SV_Target
{
	float2 uv = input.Uv, px = Texel.xy;
	float3 rgbM = tResolved.SampleLevel(sLinear, uv, 0).rgb;
	float lM = dot(rgbM, float3(0.299, 0.587, 0.114));
	float lN = FxaaLuma(uv + float2(0, -px.y)), lS = FxaaLuma(uv + float2(0, px.y));
	float lW = FxaaLuma(uv + float2(-px.x, 0)), lE = FxaaLuma(uv + float2(px.x, 0));
	float lMax = max(lM, max(max(lN, lS), max(lW, lE))), lMin = min(lM, min(min(lN, lS), min(lW, lE)));
	float range = lMax - lMin;
	if (range < max(0.0312, lMax * 0.125))
		return float4(rgbM, 1);
	float lNW = FxaaLuma(uv + float2(-px.x, -px.y)), lNE = FxaaLuma(uv + float2(px.x, -px.y));
	float lSW = FxaaLuma(uv + float2(-px.x, px.y)), lSE = FxaaLuma(uv + float2(px.x, px.y));
	float edgeH = abs(-2 * lW + lNW + lSW) + 2 * abs(-2 * lM + lN + lS) + abs(-2 * lE + lNE + lSE);
	float edgeV = abs(-2 * lN + lNW + lNE) + 2 * abs(-2 * lM + lW + lE) + abs(-2 * lS + lSW + lSE);
	bool horz = edgeH >= edgeV;
	float l1 = horz ? lN : lW, l2 = horz ? lS : lE;
	float g1 = l1 - lM, g2 = l2 - lM;
	bool steepest1 = abs(g1) >= abs(g2);
	float gScaled = 0.25 * max(abs(g1), abs(g2));
	float stepLen = horz ? px.y : px.x;
	float lLocal = 0.5 * ((steepest1 ? l1 : l2) + lM);
	if (steepest1)
		stepLen = -stepLen;
	float2 cur = uv + (horz ? float2(0, stepLen * 0.5) : float2(stepLen * 0.5, 0));
	float2 off = horz ? float2(px.x, 0) : float2(0, px.y);
	float2 uv1 = cur - off, uv2 = cur + off;
	float e1 = FxaaLuma(uv1) - lLocal, e2 = FxaaLuma(uv2) - lLocal;
	bool done1 = abs(e1) >= gScaled, done2 = abs(e2) >= gScaled;
	static const float steps[10] = { 1, 1, 1, 1, 1.5, 2, 2, 2, 4, 8 };
	[loop] for (int i = 0; i < 10 && !(done1 && done2); i++) {
		if (!done1) { uv1 -= off * steps[i]; e1 = FxaaLuma(uv1) - lLocal; done1 = abs(e1) >= gScaled; }
		if (!done2) { uv2 += off * steps[i]; e2 = FxaaLuma(uv2) - lLocal; done2 = abs(e2) >= gScaled; }
	}
	float d1 = horz ? uv.x - uv1.x : uv.y - uv1.y, d2 = horz ? uv2.x - uv.x : uv2.y - uv.y;
	bool nearer1 = d1 < d2;
	float dist = min(d1, d2), edgeLength = d1 + d2;
	float pixelOffset = 0.5 - dist / edgeLength;
	bool centreSmaller = lM < lLocal;
	bool correct = ((nearer1 ? e1 : e2) < 0) != centreSmaller;
	float offset = correct ? pixelOffset : 0;
	float lAvg = (2 * (lN + lS + lW + lE) + lNW + lNE + lSW + lSE) / 12;
	float sub = saturate(abs(lAvg - lM) / range);
	sub = (-2 * sub + 3) * sub * sub;
	offset = max(offset, sub * sub * 0.75);
	float2 fuv = uv + (horz ? float2(0, offset * stepLen) : float2(offset * stepLen, 0));
	return float4(tResolved.SampleLevel(sLinear, fuv, 0).rgb, 1);
}

float3 ToYCoCg(float3 c)
{
	return float3(dot(c, float3(0.25, 0.5, 0.25)), dot(c, float3(0.5, 0, -0.5)), dot(c, float3(-0.25, 0.5, -0.25)));
}

float3 FromYCoCg(float3 c)
{
	return float3(c.x + c.y - c.z, c.x + c.z, c.x - c.y - c.z);
}

// the history pulled towards the box's centre until it is inside the box
float3 ClipToBox(float3 h, float3 lo, float3 hi)
{
	float3 centre = 0.5 * (lo + hi), extent = 0.5 * (hi - lo) + 1e-4;
	float3 v = h - centre;
	float3 a = abs(v / extent);
	float m = max(a.x, max(a.y, a.z));
	return m > 1 ? centre + v / m : h;
}

float4 PS_Taa(VS_OUTPUT input) : SV_Target
{
	int2 p = int2(input.Pos.xy), last = int2(Sizes.xy) - 1;
	float3 m1 = 0, m2 = 0, cur = 0;
	float closest = 1;
	int2 cp = p;
	[unroll] for (int y = -1; y <= 1; y++)
		[unroll] for (int x = -1; x <= 1; x++) {
			int2 q = clamp(p + int2(x, y), 0, last);
			float3 c = ToYCoCg(tResolved.Load(int3(q, 0)).rgb);
			if (x == 0 && y == 0)
				cur = c;
			m1 += c;
			m2 += c * c;
			float d = tDepth.Load(int3(q, 0)).r;
			if (d < closest) {
				closest = d;
				cp = q;
			}
		}
	if (Taa.x == 0)
		return float4(FromYCoCg(cur), 1);
	// where the nearest point around was in the previous frame
	float2 cuv = (float2(cp) + 0.5) * Texel.xy;
	float3 world = mul(float4(ViewPosAt(cuv, closest), 1), TaaInverseView).xyz;
	float4 prev = mul(float4(world, 1), TaaPrevViewProj);
	float2 puvc = float2(prev.x / prev.w * 0.5 + 0.5, 0.5 - prev.y / prev.w * 0.5);
	float2 motion = puvc - cuv;
	float2 puv = input.Uv + motion;
	if (prev.w <= 0 || any(puv != saturate(puv)))
		return float4(FromYCoCg(cur), 1);
	float3 hist = ToYCoCg(tHistory.SampleLevel(sLinear, puv, 0).rgb);
	float3 mean = m1 / 9, sigma = sqrt(max(m2 / 9 - mean * mean, 0));
	hist = ClipToBox(hist, mean - 1.25 * sigma, mean + 1.25 * sigma);
	// more of the current frame where the picture moves fast (less smearing)
	float speed = length(motion * Sizes.xy);
	float w = lerp(Taa.y, 0.35, saturate(speed / 24));
	return float4(saturate(FromYCoCg(lerp(hist, cur, w))), 1);
}

// FSR 1 EASU (ffx_fsr1.h FsrEasuF), with texel loads instead of gathers
float3 EasuLoad(int2 p)
{
	return tEasu.Load(int3(clamp(p, int2(0, 0), int2(Sizes.xy) - 1), 0)).rgb;
}

float EasuLuma(float3 c)
{
	return c.b * 0.5 + (c.r * 0.5 + c.g);
}

// the direction and length of the edge from one 5-texel cross (a above, b left, c centre, d right,
// e below), weighted by the bilinear weight w of its centre
void EasuSet(inout float2 dir, inout float len, float w, float lA, float lB, float lC, float lD, float lE)
{
	float dc = lD - lC, cb = lC - lB;
	float lenX = max(abs(dc), abs(cb));
	float dirX = lD - lB;
	dir.x += dirX * w;
	lenX = saturate(abs(dirX) / max(lenX, 1e-6));
	len += lenX * lenX * w;
	float ec = lE - lC, ca = lC - lA;
	float lenY = max(abs(ec), abs(ca));
	float dirY = lE - lA;
	dir.y += dirY * w;
	lenY = saturate(abs(dirY) / max(lenY, 1e-6));
	len += lenY * lenY * w;
}

void EasuTap(inout float3 aC, inout float aW, float2 off, float2 dir, float2 len, float lob, float clp, float3 c)
{
	float2 v;
	v.x = off.x * dir.x + off.y * dir.y;
	v.y = off.x * -dir.y + off.y * dir.x;
	v *= len;
	float d2 = min(v.x * v.x + v.y * v.y, clp);
	float wB = 2.0 / 5.0 * d2 - 1;
	float wA = lob * d2 - 1;
	wB *= wB;
	wA *= wA;
	wB = 25.0 / 16.0 * wB - (25.0 / 16.0 - 1.0);
	float w = wB * wA;
	aC += c * w;
	aW += w;
}

float4 PS_Easu(VS_OUTPUT input) : SV_Target
{
	float2 pp = floor(input.Pos.xy) * Easu.xy + Easu.zw;
	float2 fp = floor(pp);
	pp -= fp;
	int2 f = int2(fp);
	//    b c
	//  e f g h
	//  i j k l
	//    n o
	float3 bC = EasuLoad(f + int2(0, -1)), cC = EasuLoad(f + int2(1, -1));
	float3 eC = EasuLoad(f + int2(-1, 0)), fC = EasuLoad(f), gC = EasuLoad(f + int2(1, 0)), hC = EasuLoad(f + int2(2, 0));
	float3 iC = EasuLoad(f + int2(-1, 1)), jC = EasuLoad(f + int2(0, 1)), kC = EasuLoad(f + int2(1, 1)), lC = EasuLoad(f + int2(2, 1));
	float3 nC = EasuLoad(f + int2(0, 2)), oC = EasuLoad(f + int2(1, 2));
	float bL = EasuLuma(bC), cL = EasuLuma(cC), eL = EasuLuma(eC), fL = EasuLuma(fC), gL = EasuLuma(gC), hL = EasuLuma(hC);
	float iL = EasuLuma(iC), jL = EasuLuma(jC), kL = EasuLuma(kC), lL = EasuLuma(lC), nL = EasuLuma(nC), oL = EasuLuma(oC);
	float2 dir = 0;
	float len = 0;
	EasuSet(dir, len, (1 - pp.x) * (1 - pp.y), bL, eL, fL, gL, jL);
	EasuSet(dir, len, pp.x * (1 - pp.y), cL, fL, gL, hL, kL);
	EasuSet(dir, len, (1 - pp.x) * pp.y, fL, iL, jL, kL, nL);
	EasuSet(dir, len, pp.x * pp.y, gL, jL, kL, lL, oL);
	float2 dir2 = dir * dir;
	float dirR = dir2.x + dir2.y;
	bool zro = dirR < 1.0 / 32768.0;
	dirR = zro ? 1 : rsqrt(max(dirR, 1e-20));
	dir.x = zro ? 1 : dir.x;
	dir *= dirR;
	len = len * 0.5;
	len *= len;
	float stretch = (dir.x * dir.x + dir.y * dir.y) / max(abs(dir.x), abs(dir.y));
	float2 len2 = float2(1 + (stretch - 1) * len, 1 - 0.5 * len);
	float lob = 0.5 + ((1.0 / 4.0 - 0.04) - 0.5) * len;
	float clp = 1.0 / lob;
	float3 min4 = min(min(fC, gC), min(jC, kC)), max4 = max(max(fC, gC), max(jC, kC));
	float3 aC = 0;
	float aW = 0;
	EasuTap(aC, aW, float2(0, -1) - pp, dir, len2, lob, clp, bC);
	EasuTap(aC, aW, float2(1, -1) - pp, dir, len2, lob, clp, cC);
	EasuTap(aC, aW, float2(-1, 1) - pp, dir, len2, lob, clp, iC);
	EasuTap(aC, aW, float2(0, 1) - pp, dir, len2, lob, clp, jC);
	EasuTap(aC, aW, float2(0, 0) - pp, dir, len2, lob, clp, fC);
	EasuTap(aC, aW, float2(-1, 0) - pp, dir, len2, lob, clp, eC);
	EasuTap(aC, aW, float2(1, 1) - pp, dir, len2, lob, clp, kC);
	EasuTap(aC, aW, float2(2, 1) - pp, dir, len2, lob, clp, lC);
	EasuTap(aC, aW, float2(2, 0) - pp, dir, len2, lob, clp, hC);
	EasuTap(aC, aW, float2(1, 0) - pp, dir, len2, lob, clp, gC);
	EasuTap(aC, aW, float2(1, 2) - pp, dir, len2, lob, clp, oC);
	EasuTap(aC, aW, float2(0, 2) - pp, dir, len2, lob, clp, nC);
	return float4(min(max4, max(min4, aC / aW)), 1);
}

// FSR 1 RCAS (ffx_fsr1.h FsrRcasF), with its noise removal
float3 RcasLoad(int2 p)
{
	return tSharpen.Load(int3(clamp(p, int2(0, 0), int2(Rcas.yz) - 1), 0)).rgb;
}

float4 PS_Rcas(VS_OUTPUT input) : SV_Target
{
	int2 p = int2(input.Pos.xy);
	//   b
	// d e f
	//   h
	float3 b = RcasLoad(p + int2(0, -1)), d = RcasLoad(p + int2(-1, 0)), e = RcasLoad(p);
	float3 f = RcasLoad(p + int2(1, 0)), h = RcasLoad(p + int2(0, 1));
	float bL = EasuLuma(b), dL = EasuLuma(d), eL = EasuLuma(e), fL = EasuLuma(f), hL = EasuLuma(h);
	float nz = 0.25 * (bL + dL + fL + hL) - eL;
	float range = max(max(max(bL, dL), max(eL, fL)), hL) - min(min(min(bL, dL), min(eL, fL)), hL);
	nz = saturate(abs(nz) / max(range, 1e-5));
	nz = -0.5 * nz + 1.0;
	float3 mn4 = min(min(b, d), min(f, h)), mx4 = max(max(b, d), max(f, h));
	float3 hitMin = mn4 / max(4.0 * mx4, 1e-5);
	float3 hitMax = (1.0 - mx4) / min(4.0 * mn4 - 4.0, -1e-5);
	float3 lobeRGB = max(-hitMin, hitMax);
	float lobe = max(-(0.25 - 1.0 / 16.0), min(max(lobeRGB.r, max(lobeRGB.g, lobeRGB.b)), 0)) * Rcas.x;
	lobe *= nz;
	float3 c = (lobe * (b + d + f + h) + e) / (4.0 * lobe + 1.0);
	return float4(saturate(c), 1);
}

float4 PS_Composite(VS_OUTPUT input) : SV_Target
{
	float3 c = tColor.SampleLevel(sPoint, input.Uv, 0).rgb;
	float2 h = Texel.zw;
	float ao = (tAo.SampleLevel(sLinear, input.Uv + float2(-h.x, -h.y), 0).r + tAo.SampleLevel(sLinear, input.Uv + float2(h.x, -h.y), 0).r
		+ tAo.SampleLevel(sLinear, input.Uv + float2(-h.x, h.y), 0).r + tAo.SampleLevel(sLinear, input.Uv + float2(h.x, h.y), 0).r) * 0.25;
	c *= lerp(1, ao, Ao.y);
	c += tBloom.SampleLevel(sLinear, input.Uv, 0).rgb * Bloom.y;
	// fog of war: what none of the player's units sees is darker (not on the sky)
	if (Fog.w > 0 && tDepth.SampleLevel(sPoint, input.Uv, 0).r < 0.99999) {
		float4 p = float4(ViewPos(input.Uv), 1);
		float2 fuv = float2(dot(p, FogX), dot(p, FogZ)) * Fog.xy;
		float seen = tFog.SampleLevel(sLinear, fuv, 0).r;
		c *= lerp(Fog.z, 1, seen);
	}
	return float4(saturate(c), 1);
}

// FSR 3's motion vectors (UV, the last frame's position minus the current): a moving object's,
// else the camera's, through the depth. The pixel's point is where the unjittered projection
// puts it (the jitter taken off), so a still view has none.
float2 PS_Motion(VS_OUTPUT input) : SV_Target
{
	int2 p = int2(input.Pos.xy);
	float2 object = tObjectMotion.Load(int3(p, 0)).xy;
	if (object.x < Motion.z * 0.5)
		return object;
	float2 uv = input.Uv - Motion.xy;
	float3 world = mul(float4(ViewPosAt(uv, tDepth.Load(int3(p, 0)).r), 1), TaaInverseView).xyz;
	float4 prev = mul(float4(world, 1), TaaPrevViewProj);
	if (prev.w <= 1e-4)
		return float2(0, 0);
	return float2(prev.x / prev.w * 0.5 + 0.5, 0.5 - prev.y / prev.w * 0.5) - uv;
}

float4 PS_Copy(VS_OUTPUT input) : SV_Target
{
	return float4(saturate(tCopy.Load(int3(input.Pos.xy, 0)).rgb), 1);
}

// FSR 3's reactive mask (after the SDK's generated one: the tone-mapped colours' largest
// channel changed by the threshold or more; the opaque scene is the same in every copy)
static const float REACTIVE_THRESHOLD = 0.04, REACTIVE_WATER = 0.6, REACTIVE_TRANSPARENT = 0.9;

float ReactiveChange(float3 a, float3 b)
{
	float3 d = abs(a / (max(a.r, max(a.g, a.b)) + 1) - b / (max(b.r, max(b.g, b.b)) + 1));
	return max(d.r, max(d.g, d.b));
}

float PS_Reactive(VS_OUTPUT input) : SV_Target
{
	int3 p = int3(input.Pos.xy, 0);
	float3 opaque = tOpaque.Load(p).rgb, water = tWater.Load(p).rgb, final = tColor.Load(p).rgb;
	if (ReactiveChange(water, final) >= REACTIVE_THRESHOLD)
		return REACTIVE_TRANSPARENT;
	return ReactiveChange(opaque, water) >= REACTIVE_THRESHOLD ? REACTIVE_WATER : 0;
}
)---";

namespace {
	struct Pass {
		RefCntAutoPtr<IPipelineResourceSignature> signature;
		RefCntAutoPtr<IPipelineState> pipeline;
		RefCntAutoPtr<IShaderResourceBinding> srb;
		std::vector<std::string> textures;
	};
}

struct DiligentPostProcess::Impl {
	IRenderDevice* device;
	TEXTURE_FORMAT colorFormat, depthFormat;
	uint32_t width = 0, height = 0;             // the scene's
	uint32_t outputWidth = 0, outputHeight = 0; // the back buffer's
	RefCntAutoPtr<ITexture> color, depth, ao, bloomA, bloomB, resolved;
	// FXAA's result when FSR scales it up; TAA's history (the frame written and the one before
	// swap); FSR's upscaled image, before RCAS
	RefCntAutoPtr<ITexture> aaOut, history[2], upscaled;
	int historyCurrent = 0;
	bool historyValid = false;
	Matrix prevViewProj;
	// FSR 3: the moving objects' motion vectors and all of them, the scene before its
	// transparent parts and the reactive mask from it; whether this frame uses FSR 3, has the
	// opaque copy, and whether the last frame used it (else its history is reset)
	RefCntAutoPtr<ITexture> objectMotion, motion, opaque, afterWater, reactive;
	std::unique_ptr<DiligentFsr3> fsr3;
	ITexture* copySource = nullptr;
	bool fsr3Frame = false, opaqueCopied = false, waterCopied = false, fsr3LastFrame = false;
	// fog of war: the tiles' texture, its size, and the version of the data in it
	RefCntAutoPtr<ITexture> fog;
	uint32_t fogWidth = 0, fogHeight = 0, fogVersion = ~0u;
	RefCntAutoPtr<IBuffer> buffer;
	RefCntAutoPtr<IShader> vs;
	Pass aoPass, brightPass, blurPass[2], compositePass, fxaaPass;
	// TAA into history[i]; EASU from aaOut, history[0], history[1], resolved; RCAS from
	// upscaled, history[0], history[1]
	Pass taaPass[2], easuPass[4], rcasPass[3];
	// FSR 3: the motion vectors; the reactive mask after copyOpaque, with copyAfterWater [1] or
	// without [0]; the result into the back buffer
	Pass motionPass, reactivePass[2], copyPass;

	RefCntAutoPtr<IShader> shader(SHADER_TYPE type, const char* entry)
	{
		ShaderCreateInfo sci;
		sci.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
		sci.Source = postShaderCode;
		sci.SourceLength = sizeof(postShaderCode) - 1;
		sci.EntryPoint = entry;
		sci.Desc.ShaderType = type;
		sci.Desc.Name = entry;
		RefCntAutoPtr<IShader> s;
		device->CreateShader(sci, &s);
		if (!s)
			ferr("Diligent: failed to compile post-processing shader %s", entry);
		return s;
	}

	// one pass: its own signature with only its textures (a target never bound as a texture at
	// the same time), drawing into a target of the format
	void makePass(Pass& pass, const char* entry, std::vector<std::string> textures, TEXTURE_FORMAT target)
	{
		pass.textures = std::move(textures);
		std::vector<PipelineResourceDesc> resources = { { SHADER_TYPE_PIXEL, "PostBuffer", 1, SHADER_RESOURCE_TYPE_CONSTANT_BUFFER, SHADER_RESOURCE_VARIABLE_TYPE_STATIC } };
		for (const std::string& t : pass.textures)
			resources.push_back({ SHADER_TYPE_PIXEL, t.c_str(), 1, SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE });
		SamplerDesc linear;
		linear.AddressU = linear.AddressV = linear.AddressW = TEXTURE_ADDRESS_CLAMP;
		SamplerDesc point = linear;
		point.MinFilter = point.MagFilter = point.MipFilter = FILTER_TYPE_POINT;
		const ImmutableSamplerDesc samplers[] = { { SHADER_TYPE_PIXEL, "sLinear", linear }, { SHADER_TYPE_PIXEL, "sPoint", point } };
		PipelineResourceSignatureDesc sigDesc;
		sigDesc.Name = entry;
		sigDesc.Resources = resources.data();
		sigDesc.NumResources = (Uint32)resources.size();
		sigDesc.ImmutableSamplers = samplers;
		sigDesc.NumImmutableSamplers = (Uint32)std::size(samplers);
		device->CreatePipelineResourceSignature(sigDesc, &pass.signature);
		if (!pass.signature)
			ferr("Diligent: could not create the %s signature.", entry);
		pass.signature->GetStaticVariableByName(SHADER_TYPE_PIXEL, "PostBuffer")->Set(buffer);

		RefCntAutoPtr<IShader> ps = shader(SHADER_TYPE_PIXEL, entry);
		GraphicsPipelineStateCreateInfo ci;
		ci.PSODesc.Name = entry;
		ci.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;
		auto& gp = ci.GraphicsPipeline;
		gp.NumRenderTargets = 1;
		gp.RTVFormats[0] = target;
		gp.DSVFormat = TEX_FORMAT_UNKNOWN;
		gp.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		gp.RasterizerDesc.CullMode = CULL_MODE_NONE;
		gp.DepthStencilDesc.DepthEnable = False;
		gp.DepthStencilDesc.DepthWriteEnable = False;
		ci.pVS = vs;
		ci.pPS = ps;
		IPipelineResourceSignature* signatures[] = { pass.signature };
		ci.ppResourceSignatures = signatures;
		ci.ResourceSignaturesCount = 1;
		device->CreateGraphicsPipelineState(ci, &pass.pipeline);
		if (!pass.pipeline)
			ferr("Diligent: could not create the %s pipeline.", entry);
	}

	void bind(Pass& pass, std::vector<ITexture*> textures)
	{
		pass.srb.Release();
		pass.signature->CreateShaderResourceBinding(&pass.srb, true);
		for (size_t i = 0; i < textures.size(); i++)
			pass.srb->GetVariableByName(SHADER_TYPE_PIXEL, pass.textures[i].c_str())->Set(textures[i]->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
	}

	RefCntAutoPtr<ITexture> target(const char* name, uint32_t w, uint32_t h, TEXTURE_FORMAT format, BIND_FLAGS bind)
	{
		TextureDesc desc;
		desc.Name = name;
		desc.Type = RESOURCE_DIM_TEX_2D;
		desc.Width = std::max(1u, w);
		desc.Height = std::max(1u, h);
		desc.Format = format;
		desc.BindFlags = bind | BIND_SHADER_RESOURCE;
		RefCntAutoPtr<ITexture> t;
		device->CreateTexture(desc, nullptr, &t);
		if (!t)
			ferr("Diligent: could not create the %s target.", name);
		return t;
	}

	// the fog of war's texture: one byte per tile (1x1 white when there is none)
	void makeFog(uint32_t w, uint32_t h, const uint8_t* data)
	{
		TextureDesc desc;
		desc.Name = "Fog of war";
		desc.Type = RESOURCE_DIM_TEX_2D;
		desc.Width = std::max(1u, w);
		desc.Height = std::max(1u, h);
		desc.Format = TEX_FORMAT_R8_UNORM;
		desc.BindFlags = BIND_SHADER_RESOURCE;
		desc.Usage = USAGE_DEFAULT;
		const uint8_t white = 255;
		TextureSubResData sub;
		sub.pData = data ? data : &white;
		sub.Stride = desc.Width;
		TextureData init{ &sub, 1 };
		fog.Release();
		device->CreateTexture(desc, &init, &fog);
		if (!fog)
			ferr("Diligent: could not create the fog of war texture.");
		fogWidth = w;
		fogHeight = h;
	}

	void bindComposite()
	{
		if (!fog)
			makeFog(0, 0, nullptr);
		bind(compositePass, { color, ao, bloomA, depth, fog });
	}

	void run(IDeviceContext* ctx, Pass& pass, ITexture* into)
	{
		runInto(ctx, pass, into->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET));
	}

	void runInto(IDeviceContext* ctx, Pass& pass, ITextureView* rtv)
	{
		ctx->SetRenderTargets(1, &rtv, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
		ctx->SetViewports(1, nullptr, 0, 0);
		ctx->SetPipelineState(pass.pipeline);
		ctx->CommitShaderResources(pass.srb, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
		ctx->Draw(DrawAttribs{ 3, DRAW_FLAG_VERIFY_ALL });
	}
};

DiligentPostProcess::DiligentPostProcess(IRenderDevice* device, TEXTURE_FORMAT colorFormat, TEXTURE_FORMAT depthFormat)
{
	_impl = new Impl;
	_impl->device = device;
	_impl->colorFormat = colorFormat;
	_impl->depthFormat = depthFormat;
	BufferDesc cb;
	cb.Name = "Post-processing";
	cb.Size = 320;
	cb.BindFlags = BIND_UNIFORM_BUFFER;
	cb.Usage = USAGE_DYNAMIC;
	cb.CPUAccessFlags = CPU_ACCESS_WRITE;
	device->CreateBuffer(cb, nullptr, &_impl->buffer);
	_impl->vs = _impl->shader(SHADER_TYPE_VERTEX, "VS");
	_impl->makePass(_impl->aoPass, "PS_Ao", { "tDepth" }, TEX_FORMAT_R8_UNORM);
	_impl->makePass(_impl->brightPass, "PS_Bright", { "tColor" }, TEX_FORMAT_RGBA8_UNORM);
	_impl->makePass(_impl->blurPass[0], "PS_Blur", { "tBloom" }, TEX_FORMAT_RGBA8_UNORM);
	_impl->makePass(_impl->blurPass[1], "PS_Blur", { "tBloom" }, TEX_FORMAT_RGBA8_UNORM);
	_impl->makePass(_impl->compositePass, "PS_Composite", { "tColor", "tAo", "tBloom", "tDepth", "tFog" }, colorFormat);
	_impl->makePass(_impl->fxaaPass, "PS_Fxaa", { "tResolved" }, colorFormat);
	for (Pass& pass : _impl->taaPass)
		_impl->makePass(pass, "PS_Taa", { "tResolved", "tHistory", "tDepth" }, TEX_FORMAT_RGBA16_FLOAT);
	for (Pass& pass : _impl->easuPass)
		_impl->makePass(pass, "PS_Easu", { "tEasu" }, colorFormat);
	for (Pass& pass : _impl->rcasPass)
		_impl->makePass(pass, "PS_Rcas", { "tSharpen" }, colorFormat);
	_impl->makePass(_impl->motionPass, "PS_Motion", { "tObjectMotion", "tDepth" }, TEX_FORMAT_RG16_FLOAT);
	for (Pass& pass : _impl->reactivePass)
		_impl->makePass(pass, "PS_Reactive", { "tColor", "tOpaque", "tWater" }, TEX_FORMAT_R8_UNORM);
	_impl->makePass(_impl->copyPass, "PS_Copy", { "tCopy" }, colorFormat);}

DiligentPostProcess::~DiligentPostProcess()
{
	delete _impl;
}

void DiligentPostProcess::resize(uint32_t width, uint32_t height, uint32_t outputWidth, uint32_t outputHeight)
{
	if (width == _impl->width && height == _impl->height && outputWidth == _impl->outputWidth && outputHeight == _impl->outputHeight && _impl->color)
		return;
	_impl->width = width;
	_impl->height = height;
	_impl->outputWidth = outputWidth;
	_impl->outputHeight = outputHeight;
	_impl->historyValid = false;
	const uint32_t hw = (width + 1) / 2, hh = (height + 1) / 2;
	_impl->color = _impl->target("Scene colour", width, height, _impl->colorFormat, BIND_RENDER_TARGET);
	_impl->depth = _impl->target("Scene depth", width, height, _impl->depthFormat, BIND_DEPTH_STENCIL);
	_impl->ao = _impl->target("Ambient occlusion", hw, hh, TEX_FORMAT_R8_UNORM, BIND_RENDER_TARGET);
	_impl->bloomA = _impl->target("Bloom A", hw, hh, TEX_FORMAT_RGBA8_UNORM, BIND_RENDER_TARGET);
	_impl->bloomB = _impl->target("Bloom B", hw, hh, TEX_FORMAT_RGBA8_UNORM, BIND_RENDER_TARGET);
	_impl->resolved = _impl->target("Scene before FXAA", width, height, _impl->colorFormat, BIND_RENDER_TARGET);
	_impl->bind(_impl->aoPass, { _impl->depth });
	_impl->bind(_impl->brightPass, { _impl->color });
	_impl->bind(_impl->blurPass[0], { _impl->bloomA });
	_impl->bind(_impl->blurPass[1], { _impl->bloomB });
	_impl->bindComposite();
	_impl->bind(_impl->fxaaPass, { _impl->resolved });
	_impl->aaOut = _impl->target("Scene after FXAA", width, height, _impl->colorFormat, BIND_RENDER_TARGET);
	for (int i = 0; i < 2; i++)
		_impl->history[i] = _impl->target("TAA history", width, height, TEX_FORMAT_RGBA16_FLOAT, BIND_RENDER_TARGET);
	_impl->upscaled = _impl->target("Scene upscaled", outputWidth, outputHeight, _impl->colorFormat, BIND_RENDER_TARGET);
	for (int i = 0; i < 2; i++)
		_impl->bind(_impl->taaPass[i], { _impl->resolved, _impl->history[1 - i], _impl->depth });
	ITexture* easuSources[4] = { _impl->aaOut, _impl->history[0], _impl->history[1], _impl->resolved };
	for (int i = 0; i < 4; i++)
		_impl->bind(_impl->easuPass[i], { easuSources[i] });
	ITexture* rcasSources[3] = { _impl->upscaled, _impl->history[0], _impl->history[1] };
	for (int i = 0; i < 3; i++)
		_impl->bind(_impl->rcasPass[i], { rcasSources[i] });
	_impl->objectMotion = _impl->target("Object motion vectors", width, height, TEX_FORMAT_RG16_FLOAT, BIND_RENDER_TARGET);
	_impl->motion = _impl->target("Motion vectors", width, height, TEX_FORMAT_RG16_FLOAT, BIND_RENDER_TARGET);
	_impl->opaque = _impl->target("Scene before water", width, height, _impl->colorFormat, BIND_NONE);
	_impl->afterWater = _impl->target("Scene before transparency", width, height, _impl->colorFormat, BIND_NONE);
	_impl->reactive = _impl->target("Reactive mask", width, height, TEX_FORMAT_R8_UNORM, BIND_RENDER_TARGET);
	_impl->bind(_impl->motionPass, { _impl->objectMotion, _impl->depth });
	_impl->bind(_impl->reactivePass[0], { _impl->color, _impl->opaque, _impl->opaque });
	_impl->bind(_impl->reactivePass[1], { _impl->color, _impl->opaque, _impl->afterWater });
	_impl->copySource = nullptr;
}

void DiligentPostProcess::beginFrame(IDeviceContext* ctx, bool fsr3)
{
	_impl->fsr3Frame = fsr3;
	_impl->opaqueCopied = _impl->waterCopied = false;
	if (!fsr3)
		return;
	ITextureView* rtv = _impl->objectMotion->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
	const float none[4] = { NO_OBJECT_MOTION, NO_OBJECT_MOTION, 0, 0 };
	ctx->SetRenderTargets(1, &rtv, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	ctx->ClearRenderTarget(rtv, none, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
}

ITextureView* DiligentPostProcess::objectMotionRTV() const
{
	return _impl->fsr3Frame ? _impl->objectMotion->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET) : nullptr;
}

void DiligentPostProcess::copyOpaque(IDeviceContext* ctx)
{
	if (!_impl->fsr3Frame || _impl->opaqueCopied)
		return;
	ctx->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
	CopyTextureAttribs copy(_impl->color, RESOURCE_STATE_TRANSITION_MODE_TRANSITION, _impl->opaque, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	ctx->CopyTexture(copy);
	_impl->opaqueCopied = true;
}

void DiligentPostProcess::copyAfterWater(IDeviceContext* ctx)
{
	if (!_impl->fsr3Frame || !_impl->opaqueCopied || _impl->waterCopied)
		return;
	ctx->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
	CopyTextureAttribs copy(_impl->color, RESOURCE_STATE_TRANSITION_MODE_TRANSITION, _impl->afterWater, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	ctx->CopyTexture(copy);
	_impl->waterCopied = true;
}

ITextureView* DiligentPostProcess::colorRTV() const
{
	return _impl->color ? _impl->color->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET) : nullptr;
}

ITextureView* DiligentPostProcess::depthDSV() const
{
	return _impl->depth ? _impl->depth->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL) : nullptr;
}

void DiligentPostProcess::apply(IDeviceContext* ctx, ITextureView* backBufferRTV, const Matrix& projection, const Settings& settings)
{
	const uint32_t hw = (_impl->width + 1) / 2, hh = (_impl->height + 1) / 2;
	// the fog of war's tiles, into its texture when they changed
	const FogOfWar& fog = settings.fog;
	const bool fogOn = fog.tiles && fog.width > 0 && fog.height > 0;
	if (fogOn && fog.version != _impl->fogVersion) {
		if (fog.width != _impl->fogWidth || fog.height != _impl->fogHeight) {
			_impl->makeFog(fog.width, fog.height, fog.tiles);
			_impl->bindComposite();
		}
		else {
			TextureSubResData sub;
			sub.pData = fog.tiles;
			sub.Stride = fog.width;
			Box box{ 0, fog.width, 0, fog.height };
			ctx->UpdateTexture(_impl->fog, 0, 0, box, sub, RESOURCE_STATE_TRANSITION_MODE_TRANSITION, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
		}
		_impl->fogVersion = fog.version;
	}
	const Matrix& iv = fog.inverseView;
	const uint32_t w = _impl->width, h = _impl->height, ow = _impl->outputWidth, oh = _impl->outputHeight;
	const bool fsr = ow != w || oh != h;
	const bool taa = settings.antiAliasing == 2;
	const bool fxaa = settings.antiAliasing == 1;
	// TAA: the history is good when TAA ran in the last frame at this size (not orthographic)
	const bool taaOn = taa && !settings.ortho;
	Matrix projUnjittered = settings.projection;
	projUnjittered._31 -= settings.jitter[0];
	projUnjittered._32 -= settings.jitter[1];
	const Matrix viewProj = settings.view * projUnjittered;
	const bool historyValid = taaOn && _impl->historyValid;
	const Matrix inverseView = settings.view.getInverse4x3().getTranspose(), prevViewProj = _impl->prevViewProj.getTranspose();
	// RCAS: FSR's sharpness of 0 (sharpest) to 2 stops, by the setting; milder after TAA alone
	const float stops = (1.0f - std::clamp(settings.sharpness, 0.0f, 1.0f)) * 2.0f + (fsr ? 0.0f : 0.5f);
	auto write = [&](float blurX, float blurY, uint32_t sharpenW, uint32_t sharpenH) {
		void* mapped = nullptr;
		ctx->MapBuffer(_impl->buffer, MAP_WRITE, MAP_FLAG_DISCARD, mapped);
		const float data[44] = {
			projection._11, projection._22, projection._33, projection._43,
			1.0f / w, 1.0f / h, 1.0f / hw, 1.0f / hh,
			settings.ssaoRadius, settings.ssao ? settings.ssaoStrength : 0.0f, 0, 0,
			settings.bloomThreshold, settings.bloom ? settings.bloomIntensity : 0.0f, blurX, blurY,
			iv._11, iv._21, iv._31, iv._41,
			iv._13, iv._23, iv._33, iv._43,
			fogOn ? 1.0f / (5.0f * fog.width) : 0.0f, fogOn ? 1.0f / (5.0f * fog.height) : 0.0f, fog.unseenBrightness, fogOn ? 1.0f : 0.0f,
			(float)w / ow, (float)h / oh, 0.5f * w / ow - 0.5f, 0.5f * h / oh - 0.5f,
			(float)w, (float)h, (float)ow, (float)oh,
			std::exp2(-stops), (float)sharpenW, (float)sharpenH, 0,
			historyValid ? 1.0f : 0.0f, 0.1f, 0, 0,
		};
		memcpy(mapped, data, sizeof(data));
		memcpy((float*)mapped + 44, &inverseView, sizeof(Matrix));
		memcpy((float*)mapped + 60, &prevViewProj, sizeof(Matrix));
		const float motion[4] = { settings.jitterPixels[0] / w, settings.jitterPixels[1] / h, NO_OBJECT_MOTION, 0 };
		memcpy((float*)mapped + 76, motion, sizeof(motion));
		ctx->UnmapBuffer(_impl->buffer, MAP_WRITE);
	};
	write(0, 0, w, h);
	const float white[4] = { 1, 1, 1, 1 }, black[4] = { 0, 0, 0, 1 };
	if (settings.ssao)
		_impl->run(ctx, _impl->aoPass, _impl->ao);
	else {
		ITextureView* rtv = _impl->ao->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
		ctx->SetRenderTargets(1, &rtv, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
		ctx->ClearRenderTarget(rtv, white, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	}
	if (settings.bloom) {
		_impl->run(ctx, _impl->brightPass, _impl->bloomA);
		write(1.5f, 0, w, h);
		_impl->run(ctx, _impl->blurPass[0], _impl->bloomB);
		write(0, 1.5f, w, h);
		_impl->run(ctx, _impl->blurPass[1], _impl->bloomA);
	}
	else {
		ITextureView* rtv = _impl->bloomA->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
		ctx->SetRenderTargets(1, &rtv, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
		ctx->ClearRenderTarget(rtv, black, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	}
	_impl->historyValid = taaOn;
	_impl->prevViewProj = viewProj;
	// FSR 3.1: the composite with its motion vectors and reactive mask, anti-aliased and
	// upscaled, copied into the back buffer
	if (settings.fsr3 && !settings.ortho && _impl->fsr3Frame) {
		_impl->run(ctx, _impl->compositePass, _impl->resolved);
		_impl->run(ctx, _impl->motionPass, _impl->motion);
		if (!_impl->fsr3)
			_impl->fsr3 = std::make_unique<DiligentFsr3>(_impl->device);
		DiligentFsr3::Frame f;
		if (_impl->opaqueCopied) {
			_impl->run(ctx, _impl->reactivePass[_impl->waterCopied ? 1 : 0], _impl->reactive);
			f.reactive = _impl->reactive;
		}
		f.color = _impl->resolved;
		f.depth = _impl->depth;
		f.motionVectors = _impl->motion;
		f.renderWidth = w;
		f.renderHeight = h;
		f.outputWidth = ow;
		f.outputHeight = oh;
		f.jitterX = settings.jitterPixels[0];
		f.jitterY = settings.jitterPixels[1];
		f.cameraNear = settings.cameraNear;
		f.cameraFar = settings.cameraFar;
		f.fovY = settings.fovY;
		f.frameTimeMs = settings.frameTimeMs;
		f.sharpness = settings.sharpness;
		f.reset = !_impl->fsr3LastFrame;
		ITexture* out = _impl->fsr3->dispatch(ctx, f);
		if (out != _impl->copySource) {
			_impl->bind(_impl->copyPass, { out });
			_impl->copySource = out;
		}
		_impl->runInto(ctx, _impl->copyPass, backBufferRTV);
		_impl->fsr3LastFrame = true;
		return;
	}
	_impl->fsr3LastFrame = false;
	// the composite straight into the back buffer, without anti-aliasing or upscaling
	if (!fxaa && !taaOn && !fsr) {
		_impl->runInto(ctx, _impl->compositePass, backBufferRTV);
		return;
	}
	_impl->run(ctx, _impl->compositePass, _impl->resolved);
	// the anti-aliased scene at its own size: [0] aaOut, [1] [2] the history, [3] resolved
	int source = 3;
	if (fxaa) {
		if (!fsr) {
			_impl->runInto(ctx, _impl->fxaaPass, backBufferRTV);
			return;
		}
		_impl->run(ctx, _impl->fxaaPass, _impl->aaOut);
		source = 0;
	}
	else if (taaOn) {
		const int cur = _impl->historyCurrent = 1 - _impl->historyCurrent;
		_impl->run(ctx, _impl->taaPass[cur], _impl->history[cur]);
		source = 1 + cur;
	}
	if (fsr) {
		_impl->run(ctx, _impl->easuPass[source], _impl->upscaled);
		write(0, 0, ow, oh);
		_impl->runInto(ctx, _impl->rcasPass[0], backBufferRTV);
	}
	else // TAA alone: sharpened a little (it softens the picture)
		_impl->runInto(ctx, _impl->rcasPass[source], backBufferRTV);
}
