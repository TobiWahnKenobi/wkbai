// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

// Port of the former D3D11EnhancedTerrainRenderer to Diligent: same shader, same culling of tiles, tiles
// drawn grouped by texture from one vertex buffer of the whole map, then lakes through
// IRenderer. The packed normals/tangents are unpacked in the vertex shader.

#include "DiligentEnhancedTerrainRenderer.h"
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
};

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
		float2 t = pos.xz * 0.8 - 0.5; // texel space, texel centres at whole numbers
		float2 t0 = floor(t), f = t - t0;
		float lit[4];
		for (int i = 0; i < 4; i++) {
			float2 k = t0 + float2(i & 1, i >> 1);
			float2 tile = floor(k / 4);
			lit[i] = StaticShadowSample(tile * 5 + (k - tile * 4) + 1, pos.y);
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
// where the water is shallow.
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
	float3 c = lerp(tex, FogColor.rgb, fresnel * 0.6) + glint;
	float alpha = saturate(lerp(0.3, 0.8, saturate(input.Depth / 3)) + fresnel * 0.3 + glint);
	return float4(GradeColour(lerp(c, FogColor.rgb, input.Fog)), alpha);
}

float4 PS(VS_OUTPUT input) : SV_Target
{
	// Compute the length of a half pixel in the current mipmap
	float lod = inpTexture.CalculateLevelOfDetail(inpSampler, input.Texcoord);
	float ha = 0.5 / (256 >> (int)ceil(lod));

	float2 stc = clamp(input.Texcoord, float2(ha,ha), float2(1.0-ha,1.0-ha));
	float2 tile = floor(stc / float2(0.25,0.25)); // tile index in the 4x4 atlas
	float2 subuv = fmod(stc, float2(0.25,0.25)); // coords inside the used tile

	float2 fuv = clamp(subuv, float2(ha,ha), float2(0.25-ha,0.25-ha)) + tile*0.25;

	float4 tex = inpTexture.Sample(inpSampler, fuv);

	// The original lights the terrain with a Dot3 bump-mapping pass and no ambient term: the
	// per-vertex light vector (the sun direction in tangent space, shortened by the cloud
	// shadows) dotted with the normal map, clamped, times the texture.
	// See docs/re/lighting.md.
	float lum = 0;

	if(BumpOn) {
		float3 nrm = normalTexture.Sample(inpSampler, fuv).xyz * 2 - 1;

		float3 m1 = input.Tangent;
		float3 m2 = input.Norm;
		float3 m3 = input.Bitangent;
		float3 actnorm = nrm.xxx * m1 + nrm.yyy * m2 + nrm.zzz * m3;
		lum += clamp(dot(normalize(actnorm), SunDirection), 0, 1);
		lum = clamp(lum, 0, 1);
	}
	else {
		lum += clamp(dot(input.Norm, SunDirection), 0, 1);
		lum = clamp(lum, 0, 1);
	}
	lum *= CloudLight(input.FragPos) * StaticShadowLight(input.FragPos);
	float4 c = lerp(float4(lum.xxx, 1) * input.Color * tex, FogColor, input.Fog);
	return float4(GradeColour(c.rgb), c.a);
}
)---";

struct DiligentEnhancedTerrainRenderer::Impl {
	RefCntAutoPtr<IPipelineResourceSignature> signature;
	RefCntAutoPtr<IPipelineState> pipeline;
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
};

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

// The static shadow map: 2048² texels over the square drawn by the scene renderer
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
	vbDesc.Usage = USAGE_IMMUTABLE;
	BufferData vbData{ trnVertices.data(), vbDesc.Size };
	device->CreateBuffer(vbDesc, &vbData, &_impl->trnVertexBuffer);
	if (!_impl->trnVertexBuffer)
		ferr("Diligent: could not create the terrain vertex buffer.");

	BufferDesc cbDesc;
	cbDesc.Name = "Terrain sun";
	cbDesc.Size = 176;
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
	};
	SamplerDesc sampler; // D3D11's default sampler: trilinear, clamped
	sampler.AddressU = sampler.AddressV = sampler.AddressW = TEXTURE_ADDRESS_CLAMP;
	SamplerDesc cloudSampler; // the cloud map tiles the map
	cloudSampler.AddressU = cloudSampler.AddressV = cloudSampler.AddressW = TEXTURE_ADDRESS_WRAP;
	SamplerDesc shadowSampler;
	shadowSampler.MinFilter = shadowSampler.MagFilter = shadowSampler.MipFilter = FILTER_TYPE_COMPARISON_POINT;
	shadowSampler.ComparisonFunc = COMPARISON_FUNC_LESS_EQUAL;
	shadowSampler.AddressU = shadowSampler.AddressV = shadowSampler.AddressW = TEXTURE_ADDRESS_CLAMP;
	const ImmutableSamplerDesc samplers[] = { { SHADER_TYPE_PIXEL, "inpSampler", sampler },
		{ SHADER_TYPE_PIXEL, "cloudSampler", cloudSampler }, { SHADER_TYPE_PIXEL, "shadowSampler", shadowSampler } };
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
	smDesc.Width = smDesc.Height = STATIC_SHADOW_MAP_SIZE;
	smDesc.Format = TEX_FORMAT_D32_FLOAT;
	smDesc.BindFlags = BIND_DEPTH_STENCIL | BIND_SHADER_RESOURCE;
	device->CreateTexture(smDesc, nullptr, &_impl->shadowMap);
	if (!_impl->shadowMap)
		ferr("Diligent: could not create the static shadow map.");
	shadows.staticShadowDSV = _impl->shadowMap->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL);
	shadows.staticShadowSRV = _impl->shadowMap->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
	shadows.staticShadowValid = false;
	_impl->signature->GetStaticVariableByName(SHADER_TYPE_PIXEL, "shadowTexture")->Set(shadows.staticShadowSRV);

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
	}
	delete texcache;
	delete _impl;
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
	*(int*)(sbdata + 12) = m_bumpOn ? 1 : 0;
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
	shadowParams[2] = 0.0f;
	shadowParams[3] = 0.0f;
	float* lakeColour = (float*)(sbdata + 112);
	lakeColour[0] = ((terrain->fogColor >> 16) & 255) / 255.0f;
	lakeColour[1] = ((terrain->fogColor >> 8) & 255) / 255.0f;
	lakeColour[2] = (terrain->fogColor & 255) / 255.0f;
	lakeColour[3] = 0x80 / 255.0f;
	// The original's texture scroll (0x5F3830): the offset moves along the wind vector by
	// (ms >> 4) / 128 and surges with a 4.1 s sine, ((sin + 0.8) * 0.3)^2; it wraps at 0.5.
	const uint32_t ms = (uint32_t)(shadows.time * 1000.0f);
	const float surgePhase = ((ms >> 2) & 1023) / 1024.0f * 6.2831853f;
	const float surge = (std::sin(surgePhase) + 0.8f) * 0.3f;
	auto scroll = [&](float wx, float wy, float* out) {
		const float k = (ms >> 4) * 0.0078125f + surge * surge;
		out[0] = std::fmod(wx * k, 0.5f);
		out[1] = std::fmod(wy * k, 0.5f);
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
	ctx->UnmapBuffer(_impl->sunBuffer, MAP_WRITE);

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

	ctx->SetPipelineState(_impl->pipeline);
	IBuffer* vb = _impl->trnVertexBuffer;
	const Uint64 vbOffset = 0;
	ctx->SetVertexBuffers(0, 1, &vb, &vbOffset, RESOURCE_STATE_TRANSITION_MODE_TRANSITION, SET_VERTEX_BUFFERS_FLAG_RESET);
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

