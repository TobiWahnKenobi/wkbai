// wkbai - fire effects (FireEffects.h)
// Licensed under the GNU General Public License 3
//
// Techniques (none of them code from elsewhere): value-noise fBm and domain warping as Inigo
// Quilez describes them (iquilezles.org, "warp"); the flame's temperature through Planck's law;
// the noise anchored in the world and scrolled upwards, so the flames of one fire read as one;
// the soft fade against the depth pre-pass as the waterfalls' particles. The CS184 fire report
// (cs184-firesim.github.io) simulates the fire on a 128^3 grid; an RTS with dozens of burning
// buildings has no frame time for that, so its idea used here is the colour ramp along the
// reaction (here: the temperature), with the noise standing in for the advection.

#include "FireEffects.h"
#include "ShaderCache.h"
#include "../util/LoadingProgress.h"
#include "renderer_diligent.h"
#include "RenderSettings.h"
#include "GpuProfiler.h"
#include "../Camera.h"
#include "../Environment.h"
#include "../ParticleContainer.h"
#include "../ParticleSystem.h"
#include "../util/util.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>
#include <unordered_map>

using namespace Diligent;

static const char fireShaderCode[] = R"---(
// (the fog, the grade and the noise as the waterfalls' shaders)
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

// The live sky along a reflected ray (FogSky, DiligentSkyRenderer): the physical sky as drawn
// (by the clock, the clouds' grey, the night's air), between its rings by the bearing and the
// height; below the horizon into the ground's colour. Only with FogSky[48].w (else the callers
// keep the sky box's light).
float3 SkyRing(int ring, float b)
{
	int i0 = (int)b % 16, i1 = (i0 + 1) % 16;
	return lerp(FogSky[ring * 16 + i0].rgb, FogSky[ring * 16 + i1].rgb, frac(b));
}
float3 SkyReflect(float3 d)
{
	d = normalize(d);
	float b = frac(atan2(d.z, d.x) / 6.2831853 + 1) * 16;
	float3 low = SkyRing(0, b);
	if (d.y <= 0.0698) // (4 degrees)
		return lerp(low, FogSky[49].rgb, saturate(-d.y * 3));
	float e = asin(d.y) * 57.29578;
	if (e < 22)
		return lerp(low, SkyRing(1, b), (e - 4) / 18);
	if (e < 50)
		return lerp(SkyRing(1, b), SkyRing(2, b), (e - 22) / 28);
	return lerp(SkyRing(2, b), FogSky[48].rgb, (e - 50) / 40);
}

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

cbuffer FireBuffer
{
	float4 Params;   // the game time (seconds), the level (1 Low, 2 High), the depth on, the emission's exposure
	float4 CamRight; // the camera's right in the world; w: 1 / the target's width
	float4 CamUp;    // its up; w: 1 / the target's height
	float4 Screen;   // the projection's P33 and P43, 0, 0
	float4 Light;    // the light on the smoke (rgb)
};

Texture2D<float> depthTexture; // the depth pre-pass's (soft edges)
Texture2D sceneTexture;        // the frame behind the fires (High's shimmer)
SamplerState sLinear;

struct FIRE_OUTPUT
{
	float4 Pos : SV_POSITION;
	float3 World : TEXCOORD0;
	float2 UV : TEXCOORD1;   // 0..1 across, 0 at the bottom, 1 at the top
	float4 Look : TEXCOORD2; // alpha, life, seed, heat
	float4 More : TEXCOORD3; // kind, width, extra, 0
	float ViewZ : TEXCOORD4;
	float Fog : FOG;
};

FIRE_OUTPUT VS_Fire(float4 A : ATTRIB0, float4 B : ATTRIB1, float4 C : ATTRIB2, uint id : SV_VertexID)
{
	float2 c = float2((id & 1) ? 1 : -1, (id & 2) ? 1 : -1);
	float w = A.w * 0.5, h = w * C.y;
	float3 centre = A.xyz + CamUp.xyz * h * C.z;
	float3 world = centre + CamRight.xyz * c.x * w + CamUp.xyz * c.y * h;
	FIRE_OUTPUT o;
	o.Pos = mul(float4(world, 1), Transform);
	o.World = world;
	o.UV = c * 0.5 + 0.5;
	o.Look = B;
	o.More = float4(C.x, A.w, C.w, 0);
	o.ViewZ = o.Pos.w;
	o.Fog = FogAmount(world, distance(world, FogCamera.xyz));
	return o;
}

// the colour of a black body at a temperature (kelvin): Planck's law at 610, 550 and 465 nm (in
// micrometres; c2 = hc/k = 14388 um K), relative to the red
float3 Blackbody(float T)
{
	const float3 lambda = float3(0.610, 0.550, 0.465);
	float3 l5 = lambda * lambda * lambda * lambda * lambda;
	float3 b = 1 / (l5 * (exp(14388.0 / (lambda * T)) - 1));
	return b / b.r;
}

// soft where the fire meets the ground and the models (the depth pre-pass's depth)
float SoftEdge(FIRE_OUTPUT i, float reach)
{
	if (Params.z == 0)
		return 1;
	float depth = depthTexture.Load(int3(i.Pos.xy, 0));
	float sceneZ = Screen.y / (depth - Screen.x);
	return saturate((sceneZ - i.ViewZ) / max(reach, 0.2));
}

// a flame: its temperature from a teardrop (hottest a third up, its base round and short), eaten
// at the sides and the top by noise rising through the world; cooler as the particle ages
float4 Flame(FIRE_OUTPUT i)
{
	float x = i.UV.x * 2 - 1, y = i.UV.y;
	float alpha = i.Look.x, life = i.Look.y, seed = i.Look.z, heat = i.Look.w;
	float size = i.More.y;
	float t = Params.x;
	// the teardrop first: where even the noise's most can't make it burn, nothing to work out
	float across = x / max(1 - 0.7 * saturate(y), 0.1);
	float up = (y - 0.3) * (y < 0.3 ? 3.0 : 1.4);
	float shape = 1 - length(float2(across, up));
	if (shape * 1.55 + 0.5 * (0.9 + 1.6 * y) <= 0.05)
		discard;
	// the noise in the world (across the screen and up), about three cells across a flame:
	// the flames of one fire move as one; it rises
	float2 q = float2(dot(i.World, CamRight.xyz), i.World.y) / max(size * 0.3, 0.05);
	// (rising slowly, churning more slowly still: a fire's tongues swell and fade over seconds)
	q.y -= t * 0.12;
	q.x += seed * 0.37;
	float n;
	if (Params.y >= 2) {
		// High: domain-warped (the noise displaced by noise: tongues that curl and split)
		float2 warp = float2(Fbm(q * 0.55 + float2(0, t * 0.05), 2), Fbm(q * 0.55 + float2(5.2, 1.3 + t * 0.05), 2));
		n = Fbm(q + (warp - 0.5) * 0.9, 4);
		// (four octaves average out: their contrast back to two's, else the flames merge into sheets)
		n = saturate(0.5 + (n - 0.5) * 1.45);
	}
	else
		n = Fbm(q, 2);
	float temp = shape * 1.55 + (n - 0.5) * (0.9 + 1.6 * y);
	float body = saturate(temp);
	// the particle cools as it rises (the original's sprite turns black after two thirds of its
	// life): flame young, soot old
	float cool = saturate(1 - life * 1.5);
	temp = saturate(body * cool * (0.8 + 0.3 * (heat - 1) / 0.6));
	float soot = smoothstep(0.25, 0.75, life) * smoothstep(0.05, 0.5, body) * (0.35 + 0.65 * n);
	if (temp <= 0.02 && soot <= 0.01)
		discard;
	// 800 K (a dull red at the edges and the tips) to 2600 K (the core's yellow-white)
	float3 colour = Blackbody(800 + 1800 * temp);
	float bright = temp * temp * (3 - 2 * temp) * 1.3;
	float soft = alpha * SoftEdge(i, size * 0.25);
	float cover = smoothstep(0.03, 0.35, temp) * soft;
	float3 rgb = colour * bright * cover * Params.w * (1 - i.Fog);
	// the soot darkens what is behind it (fogged like the rest)
	float a = saturate(cover * 0.25 + soot * soft * 0.55) * (1 - i.Fog * 0.7);
	return float4(rgb, a);
}

// High's glow: a warm haze of light around the fire (light only, no cover)
float4 Glow(FIRE_OUTPUT i)
{
	float2 c = i.UV * 2 - 1;
	float r2 = dot(c, c);
	if (r2 >= 1)
		discard;
	float g = (exp(-3.5 * r2) - exp(-3.5)) / (1 - exp(-3.5));
	g *= i.More.z * i.Look.x * SoftEdge(i, i.More.y * 0.3);
	return float4(Blackbody(1300) * g * 0.08 * Params.w * (1 - i.Fog), 0);
}

// the smoke (Low and High): soft puffs, darker and denser young, lit by the sky and from below by the fire
float4 Smoke(FIRE_OUTPUT i)
{
	float2 c = i.UV * 2 - 1;
	float r2 = dot(c, c);
	if (r2 >= 1)
		discard;
	float seed = i.Look.z;
	float n = Fbm(i.UV * 2.4 + seed * 7.3 + float2(0, -Params.x * 0.07), 2);
	float a = i.Look.x * (1 - r2) * (1 - r2) * saturate(0.25 + n * 1.3) * SoftEdge(i, i.More.y * 0.4);
	float grey = i.Look.w;
	float3 col = grey * Light.rgb + Blackbody(1200) * i.More.z * 0.45 * saturate(1 - i.UV.y * 0.8);
	col = lerp(col, FogTint(i.World), i.Fog);
	return float4(GradeColour(col) * a, a);
}

// High's shimmer: the frame behind displaced by rising noise, most just above the flames
float4 Shimmer(FIRE_OUTPUT i)
{
	float x = i.UV.x * 2 - 1, y = i.UV.y;
	float m = saturate(1 - x * x) * smoothstep(0.0, 0.2, y) * saturate(1 - y) * i.More.z * i.Look.x;
	if (m <= 0.001)
		discard;
	float2 q = float2(dot(i.World, CamRight.xyz), i.World.y) / max(i.More.y * 0.12, 0.05);
	q.y -= Params.x * 0.4;
	float2 d = float2(Noise(q), Noise(q + float2(7.7, 3.1))) - 0.5;
	float2 uv = i.Pos.xy * float2(CamRight.w, CamUp.w) + d * m * 0.012 * saturate(60 / max(i.ViewZ, 1));
	float3 behind = sceneTexture.SampleLevel(sLinear, uv, 0).rgb;
	m *= SoftEdge(i, i.More.y * 0.2);
	return float4(behind * m, m);
}

float4 PS_Fire(FIRE_OUTPUT i) : SV_Target
{
	if (i.More.x < 0.5)
		return Flame(i);
	if (i.More.x < 1.5)
		return Glow(i);
	if (i.More.x < 2.5)
		return Smoke(i);
	return Shimmer(i);
}
)---";

namespace FireFx {
bool s_forceAvailable = false;
namespace {
	int s_level = ORIGINAL;
	std::unordered_map<uint32_t, float> s_damage;
	const std::unordered_map<uint32_t, float>* s_shares = nullptr;
	std::vector<Flame> s_pending;

	float Saturate(float x) { return x < 0.0f ? 0.0f : x > 1.0f ? 1.0f : x; }
	uint32_t Mix(uint32_t x)
	{
		x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
		return x;
	}
	float Hash01(uint32_t x) { return (Mix(x) & 0xFFFFFF) / 16777216.0f; }
	uint32_t FloatBits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }

	// the shader's resources, made at the first frame that draws fire (per renderer)
	struct Resources {
		DiligentGfx* owner = nullptr;
		RefCntAutoPtr<IPipelineResourceSignature> signature;
		RefCntAutoPtr<IShaderResourceBinding> binding;
		RefCntAutoPtr<IPipelineState> pipeline;
		RefCntAutoPtr<IBuffer> buffer;
		RefCntAutoPtr<ITexture> sceneCopy;
		ITextureView* boundScene = nullptr;
		ITextureView* boundDepth = nullptr;
		bool failed = false;
	};
	Resources s_res;

	bool MakeResources(DiligentGfx* gfx)
	{
		if (s_res.owner == gfx)
			return !s_res.failed;
		s_res = Resources();
		s_res.owner = gfx;
		IRenderDevice* device = gfx->device;
		BufferDesc cb;
		cb.Name = "Fire";
		cb.Size = 80;
		cb.BindFlags = BIND_UNIFORM_BUFFER;
		cb.Usage = USAGE_DYNAMIC;
		cb.CPUAccessFlags = CPU_ACCESS_WRITE;
		device->CreateBuffer(cb, nullptr, &s_res.buffer);
		const SHADER_TYPE vsps = SHADER_TYPE_VERTEX | SHADER_TYPE_PIXEL;
		const PipelineResourceDesc resources[] = {
			{ vsps, "TransformBuffer", 1, SHADER_RESOURCE_TYPE_CONSTANT_BUFFER, SHADER_RESOURCE_VARIABLE_TYPE_STATIC },
			{ vsps, "FogBuffer", 1, SHADER_RESOURCE_TYPE_CONSTANT_BUFFER, SHADER_RESOURCE_VARIABLE_TYPE_STATIC },
			{ vsps, "FireBuffer", 1, SHADER_RESOURCE_TYPE_CONSTANT_BUFFER, SHADER_RESOURCE_VARIABLE_TYPE_STATIC },
			{ SHADER_TYPE_PIXEL, "depthTexture", 1, SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
			{ SHADER_TYPE_PIXEL, "sceneTexture", 1, SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
		};
		SamplerDesc linear;
		linear.AddressU = linear.AddressV = linear.AddressW = TEXTURE_ADDRESS_CLAMP;
		const ImmutableSamplerDesc samplers[] = { { SHADER_TYPE_PIXEL, "sLinear", linear } };
		PipelineResourceSignatureDesc sig;
		sig.Name = "Fire resources";
		sig.Resources = resources;
		sig.NumResources = (Uint32)std::size(resources);
		sig.ImmutableSamplers = samplers;
		sig.NumImmutableSamplers = (Uint32)std::size(samplers);
		device->CreatePipelineResourceSignature(sig, &s_res.signature);
		if (!s_res.buffer || !s_res.signature) {
			s_res.failed = true;
			return false;
		}
		for (SHADER_TYPE st : { SHADER_TYPE_VERTEX, SHADER_TYPE_PIXEL }) {
			s_res.signature->GetStaticVariableByName(st, "TransformBuffer")->Set(gfx->transformBuffer);
			s_res.signature->GetStaticVariableByName(st, "FogBuffer")->Set(gfx->fogBuffer);
			s_res.signature->GetStaticVariableByName(st, "FireBuffer")->Set(s_res.buffer);
		}
		s_res.signature->CreateShaderResourceBinding(&s_res.binding, true);
		auto shader = [&](SHADER_TYPE type, const char* entry) {
			ShaderCreateInfo sci;
			sci.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
			sci.Source = fireShaderCode;
			sci.SourceLength = sizeof(fireShaderCode) - 1;
			sci.EntryPoint = entry;
			sci.Desc.ShaderType = type;
			sci.Desc.Name = entry;
			RefCntAutoPtr<IShader> s;
			LoadingCompilingShader(entry); // (the loading screen)
			ShaderCache::CreateShader(device, sci, &s);
			return s;
		};
		RefCntAutoPtr<IShader> vs = shader(SHADER_TYPE_VERTEX, "VS_Fire"), ps = shader(SHADER_TYPE_PIXEL, "PS_Fire");
		if (!vs || !ps) {
			s_res.failed = true;
			return false;
		}
		GraphicsPipelineStateCreateInfo ci;
		ci.PSODesc.Name = "Fire";
		ci.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;
		auto& gp = ci.GraphicsPipeline;
		gp.NumRenderTargets = 1;
		gp.RTVFormats[0] = gfx->swapChain->GetDesc().ColorBufferFormat;
		gp.DSVFormat = gfx->swapChain->GetDesc().DepthBufferFormat;
		gp.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
		gp.RasterizerDesc.CullMode = CULL_MODE_NONE;
		gp.DepthStencilDesc.DepthEnable = True;
		gp.DepthStencilDesc.DepthWriteEnable = False;
		gp.DepthStencilDesc.DepthFunc = COMPARISON_FUNC_LESS_EQUAL;
		// premultiplied: the flames' and the glow's light added over what their cover leaves
		auto& blend = gp.BlendDesc.RenderTargets[0];
		blend.BlendEnable = True;
		blend.SrcBlend = BLEND_FACTOR_ONE;
		blend.DestBlend = BLEND_FACTOR_INV_SRC_ALPHA;
		blend.SrcBlendAlpha = BLEND_FACTOR_ONE;
		blend.DestBlendAlpha = BLEND_FACTOR_INV_SRC_ALPHA;
		const Uint32 stride = sizeof(Instance);
		const LayoutElement layout[] = {
			LayoutElement{ 0, 0, 4, VT_FLOAT32, False, 0, stride, INPUT_ELEMENT_FREQUENCY_PER_INSTANCE, 1 },
			LayoutElement{ 1, 0, 4, VT_FLOAT32, False, 16, stride, INPUT_ELEMENT_FREQUENCY_PER_INSTANCE, 1 },
			LayoutElement{ 2, 0, 4, VT_FLOAT32, False, 32, stride, INPUT_ELEMENT_FREQUENCY_PER_INSTANCE, 1 } };
		gp.InputLayout.LayoutElements = layout;
		gp.InputLayout.NumElements = (Uint32)std::size(layout);
		IPipelineResourceSignature* signatures[] = { s_res.signature };
		ci.ppResourceSignatures = signatures;
		ci.ResourceSignaturesCount = 1;
		ci.pVS = vs;
		ci.pPS = ps;
		ShaderCache::CreateGraphicsPipeline(device, ci, &s_res.pipeline);
		if (!s_res.pipeline) {
			s_res.failed = true;
			return false;
		}
		return true;
	}

	// one fire's flames together: a standing fire's by its emitter, a moving one's (an arrow, a
	// fireball) by its system and object
	struct Cluster {
		Vector3 base, sum;
		float size = 0, alpha = 0, heat = 1, lowest = 1e9f;
		int count = 0;
		uint32_t seed = 0;
		bool standing = false;
	};

	// a flame's height over its width
	const float FLAME_ASPECT = 1.7f;
	// how far a standing fire's flames are sunk into what burns, in their half-heights
	const float STANDING_SINK = 0.4f;
	// and how much taller they are (what's sunk still reaching out)
	const float STANDING_TALL = 1.3f;
}

const char* QualityName(int quality)
{
	static const char* const names[LEVELS] = { "Original", "Low", "High" };
	return names[std::clamp(quality, 0, (int)LEVELS - 1)];
}

void BeginFrame(int quality, bool available)
{
	s_level = (available || s_forceAvailable) ? std::clamp(quality, 0, (int)LEVELS - 1) : ORIGINAL;
	// (the damage set last frame stays: the fire lights are worked out before this frame's is set;
	// forgotten now and then, for the objects gone)
	if (s_level == ORIGINAL || s_damage.size() > 4096)
		s_damage.clear();
}

int Level() { return s_level; }

bool Takes(int level, const ParticleSystem* system)
{
	return level > ORIGINAL && system && system->flameSprite && system->Particles.type == PSType::Sprites;
}

float DamageScale(float integrity)
{
	return 1.0f + 0.6f * (1.0f - Saturate(integrity));
}

void SetDamage(uint32_t objid, float integrity)
{
	if (objid)
		s_damage[objid] = Saturate(integrity);
}

float DamageOf(uint32_t objid)
{
	auto it = s_damage.find(objid);
	return it == s_damage.end() ? 1.0f : DamageScale(it->second);
}

void SetShares(const std::unordered_map<uint32_t, float>* shares) { s_shares = shares; }

float ShareOf(uint32_t objid)
{
	if (!s_shares || !objid || s_shares->empty())
		return 1.0f;
	auto it = s_shares->find(objid);
	return it == s_shares->end() ? 1.0f : Saturate(it->second);
}

bool HasShare(uint32_t objid) { return s_shares && objid && s_shares->count(objid) != 0; }

float Flicker(float time, uint32_t seed)
{
	const float s = Hash01(seed) * 6.2831853f, s2 = Hash01(seed ^ 0x5bd1e995U) * 6.2831853f;
	// (slow swells, 0.25 to 0.8 a second, and a little faster wavering on them; no buzz)
	return 1.0f + 0.15f * std::sin(time * 1.6f + s) + 0.10f * std::sin(time * 2.9f + s2) + 0.05f * std::sin(time * 5.1f + s + s2);
}

Vector3 Sway(float time, uint32_t seed)
{
	const float s = Hash01(seed ^ 0x3c6ef372U) * 6.2831853f, s2 = Hash01(seed ^ 0xa54ff53aU) * 6.2831853f;
	return Vector3(0.35f * std::sin(time * 1.1f + s) + 0.15f * std::sin(time * 2.3f + s2),
		0.25f * std::sin(time * 1.7f + s2) + 0.1f * std::sin(time * 3.1f + s),
		0.35f * std::sin(time * 0.9f + s2 + 1.0f) + 0.15f * std::sin(time * 2.6f + s));
}

uint32_t SeedOf(const Particle& p)
{
	return Mix(FloatBits(p.startTime) ^ Mix(FloatBits(p.startPosition.x) + 0x9E3779B1U * FloatBits(p.startPosition.z)) ^ (p.objid * 0x85ebca6bU));
}

void Add(const Particle& p, float now, float size, float alpha)
{
	if (s_pending.size() >= 16384)
		return;
	Flame f;
	f.position = const_cast<Particle&>(p).getPosition(now); // (not const in ParticleContainer.h)
	f.emitter = p.startPosition;
	// (a fire the rain is putting out, or snow holding down: smaller and fainter, cooler)
	const float share = ShareOf(p.objid);
	f.size = size * ShareSize(share);
	f.alpha = Saturate(alpha * ShareAlpha(share));
	f.life = p.maxAge > 0 ? Saturate((now - p.startTime) / p.maxAge) : 0.0f;
	f.heat = 1.0f + (DamageOf(p.objid) - 1.0f) * share;
	f.seed = SeedOf(p);
	f.objid = p.objid;
	f.system = p.system;
	s_pending.push_back(f);
}

size_t PendingCount() { return s_pending.size(); }
const std::vector<Flame>& Pending() { return s_pending; }

void Clear() { s_pending.clear(); }

void BuildInstances(int level, float time, bool shimmer, std::vector<Instance>& out)
{
	out.clear();
	if (level <= ORIGINAL || s_pending.empty())
		return;
	auto push = [&](const Vector3& at, float width, float alpha, float life, float seed, float heat, int kind, float aspect, float lift, float extra) {
		out.push_back(Instance{ { at.x, at.y, at.z, width }, { alpha, life, seed, heat }, { (float)kind, aspect, lift, extra } });
	};
	// High: the fires as wholes (the glow, the shimmer and the smoke)
	// (kept between frames: no allocations once they have grown)
	static std::vector<Cluster> clusters;
	static std::unordered_map<uint64_t, size_t> index;
	clusters.clear();
	index.clear();
	{
		for (const Flame& f : s_pending) {
			const bool standing = f.system->standingFire;
			uint64_t key = (uint64_t)(uintptr_t)f.system * 0x9E3779B97F4A7C15ULL ^ ((uint64_t)f.objid << 20);
			if (standing)
				key ^= (uint64_t)Mix((uint32_t)std::lround(f.emitter.x) * 73856093U ^ (uint32_t)std::lround(f.emitter.y) * 19349663U
					^ (uint32_t)std::lround(f.emitter.z) * 83492791U) << 1;
			auto [it, isNew] = index.try_emplace(key, clusters.size());
			if (isNew) {
				clusters.emplace_back();
				clusters.back().seed = Mix((uint32_t)key ^ (uint32_t)(key >> 32));
				clusters.back().standing = standing;
				clusters.back().base = f.emitter;
			}
			Cluster& c = clusters[it->second];
			c.sum += f.position;
			c.size = std::max(c.size, f.size);
			c.alpha += f.alpha;
			c.heat = std::max(c.heat, f.heat);
			c.lowest = std::min(c.lowest, f.position.y);
			c.count++;
		}
		for (Cluster& c : clusters)
			if (!c.standing) {
				c.base = c.sum / (float)c.count;
				c.base.y = std::min(c.base.y, c.lowest + c.size * 0.25f);
			}
		// the shimmer above the flames, the frame behind displaced (first: the rest over it)
		if (shimmer && level >= HIGH)
			for (const Cluster& c : clusters)
				if (c.standing)
					push(c.base + Vector3(0, c.size * 0.35f, 0), c.size * 1.1f, Saturate(c.alpha), 0, 0, c.heat, SHIMMER, 1.9f, 0.4f, 1.0f);
		// the smoke of a standing fire, at Low and High: a column of puffs rising from its flames'
		// tops, drifting with the wind, growing and thinning (a puff every LIFE / count seconds, evenly,
		// each its own path), thicker the more the building is damaged
		// (the lobby's wind, Wind.h, also without the seasons; a gale's lean held to 6 m/s)
		Vector3 wind = g_environment.wind;
		if (wind.len2xz() > 6.0f)
			wind = wind * (6.0f / wind.len2xz());
		for (const Cluster& c : clusters) {
			if (!c.standing)
				continue;
			const float damage = (c.heat - 1.0f) / 0.6f;
			const int puffs = (level >= HIGH ? 9 : 6) + (int)std::lround((level >= HIGH ? 6.0f : 3.0f) * damage);
			const float LIFE = 10.0f, rise = 1.0f + 0.08f * c.size;
			const float strength = Saturate(c.alpha * 0.8f) * (0.5f + 0.3f * damage);
			for (int k = 0; k < puffs; k++) {
				const uint32_t ps = Mix(c.seed + (uint32_t)k * 0x9E3779B1U);
				const float offset = ((float)k + 0.6f * Hash01(ps)) / (float)puffs;
				const float age = std::fmod(time / LIFE + offset, 1.0f) * LIFE;
				const float u = age / LIFE;
				Vector3 at = c.base + Vector3(0, c.size * 0.4f + rise * age, 0) + wind * (age * 0.8f);
				at.x += (Hash01(ps ^ 0x1234567U) - 0.5f) * c.size * (0.3f + u);
				at.z += (Hash01(ps ^ 0x7654321U) - 0.5f) * c.size * (0.3f + u);
				const float width = c.size * (0.6f + 1.8f * u);
				const float a = strength * Saturate(u * 8.0f) * (1.0f - u) * std::sqrt(1.0f - u);
				if (a <= 0.01f)
					continue;
				const float grey = 0.2f + 0.3f * u + 0.06f * Hash01(ps ^ 0xabcdefU);
				push(at, width, a, u, Hash01(ps ^ 0x13579U), grey, SMOKE, 1.0f, 0.0f, Saturate(1.0f - u * 3.0f));
			}
		}
	}
	// the flames: a little narrower than the sprite and half again as tall, the base at the particle,
	// bigger the more the building is damaged. A standing fire's sunk into what burns: its points
	// (a building's MI_) are on the roof, its ridges and peaks, where the original's sprite, centred
	// on them, was half inside; the flame's root and hottest third in the building (hidden by it),
	// its tongues coming out of the roof instead of sitting on it
	for (const Flame& f : s_pending) {
		const float damage = (f.heat - 1.0f) / 0.6f;
		const float width = f.size * 0.5f * (0.9f + 0.3f * damage);
		Vector3 at = f.position;
		float aspect = FLAME_ASPECT;
		if (f.system->standingFire) {
			aspect *= STANDING_TALL;
			at.y -= width * 0.5f * aspect * STANDING_SINK;
		}
		push(at, width, f.alpha, f.life, Hash01(f.seed), f.heat, FLAME, aspect, 0.45f, 0.0f);
	}
	// the glow around each fire, flickering
	if (level >= HIGH)
		for (const Cluster& c : clusters)
			push(c.base + Vector3(0, c.size * (c.standing ? 0.1f : 0.3f), 0), c.size * 1.6f, Saturate(c.alpha * 0.7f), 0, 0, c.heat, GLOW, 1.0f, 0.0f,
				Flicker(time, c.seed) * (0.8f + 0.4f * (c.heat - 1.0f) / 0.6f));
}

void Draw(IRenderer* gfx, const Camera& camera, float time)
{
	if (s_pending.empty())
		return;
	auto* dgfx = dynamic_cast<DiligentGfx*>(gfx);
	if (s_level <= ORIGINAL || !dgfx || !MakeResources(dgfx)) {
		s_pending.clear();
		return;
	}
	PROFILE_SCOPE("Fire");
	IDeviceContext* ctx = dgfx->ctx;
	ITexture* target = dgfx->CurrentColorTarget() ? dgfx->CurrentColorTarget()->GetTexture() : nullptr;
	if (!target) {
		s_pending.clear();
		return;
	}
	const TextureDesc& td = target->GetDesc();
	// High's shimmer: the frame so far copied (not with multisampling)
	bool shimmer = s_level >= HIGH && td.SampleCount == 1;
	if (shimmer && (!s_res.sceneCopy || s_res.sceneCopy->GetDesc().Width != td.Width || s_res.sceneCopy->GetDesc().Height != td.Height
		|| s_res.sceneCopy->GetDesc().Format != td.Format)) {
		TextureDesc cd;
		cd.Name = "Scene behind the fires";
		cd.Type = RESOURCE_DIM_TEX_2D;
		cd.Width = td.Width;
		cd.Height = td.Height;
		cd.Format = td.Format;
		cd.BindFlags = BIND_SHADER_RESOURCE;
		cd.Usage = USAGE_DEFAULT;
		s_res.sceneCopy.Release();
		dgfx->device->CreateTexture(cd, nullptr, &s_res.sceneCopy);
		s_res.boundScene = nullptr;
	}
	shimmer = shimmer && s_res.sceneCopy;
	static std::vector<Instance> instances;
	{
		PROFILE_SCOPE("Instances");
		BuildInstances(s_level, time, shimmer, instances);
	}
	s_pending.clear();
	if (instances.empty())
		return;
	shimmer = shimmer && instances.front().c[0] == (float)SHIMMER;
	if (shimmer) {
		PROFILE_SCOPE("Shimmer copy");
		ctx->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
		CopyTextureAttribs copy(target, RESOURCE_STATE_TRANSITION_MODE_TRANSITION, s_res.sceneCopy, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
		ctx->CopyTexture(copy);
		dgfx->RestoreRenderTargets();
	}
	ITextureView* white = dgfx->GetTextureView(nullptr);
	ITextureView* sceneView = shimmer ? s_res.sceneCopy->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE) : white;
	const bool depthOn = dgfx->shadows.lightPrepass && dgfx->shadows.lightingDepth && !camera.orthoMode;
	ITextureView* depthView = depthOn ? dgfx->shadows.lightingDepth : white;
	if (s_res.boundScene != sceneView) {
		s_res.binding->GetVariableByName(SHADER_TYPE_PIXEL, "sceneTexture")->Set(sceneView, SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
		s_res.boundScene = sceneView;
	}
	if (s_res.boundDepth != depthView) {
		s_res.binding->GetVariableByName(SHADER_TYPE_PIXEL, "depthTexture")->Set(depthView, SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
		s_res.boundDepth = depthView;
	}

	// the light on the smoke: the sun's and the sky's by day, the moon's dim blue at night (as the
	// chimneys')
	float light[3] = { 1.0f, 1.0f, 1.0f };
	const EnvironmentView& v = g_environment;
	if (v.active) {
		const float day = Saturate(v.sun.daylight), night = 1.0f - day;
		static const float moon[3] = { 0.75f, 0.85f, 1.0f };
		const float moonLevel = 0.4f + 0.15f * Saturate(v.sun.moonLight);
		for (int c = 0; c < 3; c++)
			light[c] = (day * (0.55f * v.sun.colour[c] + 0.45f * v.sun.ambient[c]) + night * moonLevel * moon[c]) * (1.0f - 0.15f * Saturate(v.cloudCover));
	}
	const Matrix& view = camera.viewMatrix;
	const Matrix& proj = camera.projMatrix;
	const float data[20] = {
		// (the flames give light: the exposure set, not the night's darkening, g_environment.exposure)
		time, (float)s_level, depthOn ? 1.0f : 0.0f, g_renderSettings.exposure,
		view._11, view._21, view._31, 1.0f / td.Width,
		view._12, view._22, view._32, 1.0f / td.Height,
		proj._33, proj._43, 0, 0,
		light[0], light[1], light[2], 0,
	};
	void* mapped = nullptr;
	ctx->MapBuffer(s_res.buffer, MAP_WRITE, MAP_FLAG_DISCARD, mapped);
	memcpy(mapped, data, sizeof(data));
	ctx->UnmapBuffer(s_res.buffer, MAP_WRITE);
	gfx->SetTransformMatrix(&camera.sceneMatrix);

	uint64_t offset = 0;
	IBuffer* vb = dgfx->StreamVertexData(instances.data(), instances.size() * sizeof(Instance), offset);
	ctx->SetPipelineState(s_res.pipeline);
	ctx->SetVertexBuffers(0, 1, &vb, &offset, RESOURCE_STATE_TRANSITION_MODE_TRANSITION, SET_VERTEX_BUFFERS_FLAG_RESET);
	ctx->CommitShaderResources(s_res.binding, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	DrawAttribs draw;
	draw.NumVertices = 4;
	draw.NumInstances = (Uint32)instances.size();
	draw.Flags = DRAW_FLAG_VERIFY_ALL;
	ctx->Draw(draw);
}
}
