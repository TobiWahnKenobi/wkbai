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
	float4 SkyFlags;   // x: the sky has all six faces, y: the dynamic clouds' layer (tClouds), z: the frame (its jitter)
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

// The cloud layer's density at a point: the cover there, its tops rising where it is denser
// (flat bases), the edges worn by the noise's cells
float CloudDensity(float3 p, float lod)
{
	float h = saturate((p.y - CloudShape.z) / CloudShape.w);
	float cover = CloudCover(p.xz, lod);
	// rounded tops: denser cover reaches higher; a soft flat base
	float d = smoothstep(0, 0.3, cover - h * h * 0.95) * smoothstep(0, 0.12, h);
	float cells = cloudNoise.SampleLevel(cloudNoiseSampler, p.xz * (CloudScale.y * 1.3) + CloudWind.zw * 1.3, lod).b;
	return saturate(d - (1 - cells) * 0.35 * (1 - d)) * 1.4;
}

float HenyeyGreenstein(float mu, float g)
{
	float g2 = g * g;
	return (1 - g2) / (12.566371 * pow(1 + g2 - 2 * g * mu, 1.5));
}

// The view ray through the cloud layer, 64 steps (jittered per pixel; at most 1800 units of it),
// each lit by the sun through 3 steps of cloud towards it (Beer's law with the powder term away from the sun) and
// a strong forward scattering lobe (the silver linings), plus the sky's light from above.
// Returns the light scattered towards the camera and the sky's transmittance.
float4 MarchClouds(float3 ro, float3 rd, float2 pixel)
{
	float base = CloudShape.z, top = CloudShape.z + CloudShape.w;
	if (ro.y > base || rd.y <= 0.002)
		return float4(0, 0, 0, 1); // above the base (zoomed out): the shadows only
	float t0 = (base - ro.y) / rd.y, t1 = min((top - ro.y) / rd.y, t0 + 1800);
	if (t1 <= t0 || t0 > 9000)
		return float4(0, 0, 0, 1);
	const int STEPS = 64;
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
		// the noise's mip level by the quarter-size pixel's footprint there (the detail layer's
		// texels per unit), so the distant clouds don't alias
		float lod = log2(1 + t * 0.0035 * 256 * CloudScale.y);
		float dens = CloudDensity(p, lod);
		if (dens <= 0.002)
			continue;
		float od = 0;
		[unroll] for (int j = 0; j < 3; j++)
			od += CloudDensity(p + sun * ((j + 0.5) * 36), lod + 0.5) * 36;
		float toSun = exp(-SIGMA * od);
		float powder = 1 - exp(-2 * SIGMA * od - 0.15);
		float energy = toSun * lerp(powder * 1.6, 1, towardSun);
		float h = saturate((p.y - base) / CloudShape.w);
		float3 ambient = CloudSkyUp.rgb * (0.45 + 0.55 * h) + CloudSkyHorizon.rgb * 0.15;
		float3 s = CloudSunColour.rgb * (energy * phase * 1.3) + ambient * 0.85;
		float dT = exp(-SIGMA * dens * dt);
		light += T * (1 - dT) * s;
		T *= dT;
	}
	// far clouds fade into the horizon's haze
	float fade = exp(-t0 / 5000);
	return float4(light * fade, lerp(1, T, fade));
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
		colour = lerp(tex, FogColor.rgb, fog);
	}
	// the dynamic clouds' layer (RenderSettings::dynamicClouds 3) in front of the sky box,
	// marched at a quarter of the size (PS_Clouds)
	if (SkyFlags.y != 0 && c > 0) {
		float2 uv = float2(input.Ndc.x * 0.5 + 0.5, 0.5 - input.Ndc.y * 0.5);
		float cw, ch;
		tClouds.GetDimensions(cw, ch);
		float2 o = 0.75 / float2(cw, ch); // a small blur over the march's jitter
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

struct DiligentSkyRenderer::Impl {
	DiligentGfx* dgfx;
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
	};
	for (const char* name : texNames)
		resources.push_back({ SHADER_TYPE_PIXEL, name, 1, SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_STATIC });
	resources.push_back({ SHADER_TYPE_PIXEL, "tClouds", 1, SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE });
	SamplerDesc sampler;
	sampler.AddressU = sampler.AddressV = sampler.AddressW = TEXTURE_ADDRESS_CLAMP;
	SamplerDesc noiseSampler;
	noiseSampler.AddressU = noiseSampler.AddressV = noiseSampler.AddressW = TEXTURE_ADDRESS_WRAP;
	const ImmutableSamplerDesc samplers[] = { { SHADER_TYPE_PIXEL, "skySampler", sampler }, { SHADER_TYPE_PIXEL, "cloudNoiseSampler", noiseSampler },
		{ SHADER_TYPE_PIXEL, "skyOccSampler", sampler } };
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
	// the clouds' layer, a quarter of the scene's size
	const bool clouds = _impl->dgfx->shadows.cloudShadows && g_renderSettings.cloudShadows && g_renderSettings.dynamicClouds >= 3;
	const float data[24] = {
		forward.x, forward.y, forward.z, tanY * cam.aspect,
		right.x, right.y, right.z, tanY,
		up.x, up.y, up.z, 0,
		// sky_front.tga faces +z (north) and sky_right.tga +x, as seen in the original
		0, 0, 1, 0,
		1, 0, 0, 0,
		_impl->sixFaces ? 1.0f : 0.0f, clouds ? 1.0f : 0.0f, (float)(_impl->dgfx->FrameIndex() % 64), 0,
	};
	memcpy(f, data, sizeof(data));
	ctx->UnmapBuffer(_impl->skyBuffer, MAP_WRITE);

	if (clouds) {
		const TextureDesc& scene = _impl->dgfx->CurrentColorTarget()->GetTexture()->GetDesc();
		const Uint32 w = std::max(1u, (scene.Width + 3) / 4), h = std::max(1u, (scene.Height + 3) / 4);
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
