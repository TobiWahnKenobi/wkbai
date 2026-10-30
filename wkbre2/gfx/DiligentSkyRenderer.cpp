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
	float4 SkyFlags;   // x: the sky has all six faces
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
	return float4(GradeColour(colour), 1);
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
	};
	for (const char* name : texNames)
		resources.push_back({ SHADER_TYPE_PIXEL, name, 1, SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_STATIC });
	SamplerDesc sampler;
	sampler.AddressU = sampler.AddressV = sampler.AddressW = TEXTURE_ADDRESS_CLAMP;
	const ImmutableSamplerDesc samplers[] = { { SHADER_TYPE_PIXEL, "skySampler", sampler } };
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
	for (int i = 0; i < 6; i++)
		_impl->signature->GetStaticVariableByName(SHADER_TYPE_PIXEL, texNames[i])->Set(_impl->dgfx->GetTextureView(_impl->faces[i]));

	auto createShader = [&](SHADER_TYPE type, const char* entryPoint) {
		ShaderCreateInfo sci;
		sci.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
		sci.Source = skyShaderCode;
		sci.SourceLength = sizeof(skyShaderCode) - 1;
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
	const float data[24] = {
		forward.x, forward.y, forward.z, tanY * cam.aspect,
		right.x, right.y, right.z, tanY,
		up.x, up.y, up.z, 0,
		// sky_front.tga faces +z (north) and sky_right.tga +x, as seen in the original
		0, 0, 1, 0,
		1, 0, 0, 0,
		_impl->sixFaces ? 1.0f : 0.0f, 0, 0, 0,
	};
	memcpy(f, data, sizeof(data));
	ctx->UnmapBuffer(_impl->skyBuffer, MAP_WRITE);

	ctx->SetPipelineState(_impl->pipeline);
	ctx->CommitShaderResources(_impl->srb, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	ctx->Draw(DrawAttribs{ 3, DRAW_FLAG_VERIFY_ALL });
}
