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

#include "DiligentPostProcess.h"
#include "../util/util.h"
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
};
Texture2D tColor;
Texture2D tDepth;
Texture2D tAo;
Texture2D tBloom;
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

float4 PS_Composite(VS_OUTPUT input) : SV_Target
{
	float3 c = tColor.SampleLevel(sPoint, input.Uv, 0).rgb;
	float2 h = Texel.zw;
	float ao = (tAo.SampleLevel(sLinear, input.Uv + float2(-h.x, -h.y), 0).r + tAo.SampleLevel(sLinear, input.Uv + float2(h.x, -h.y), 0).r
		+ tAo.SampleLevel(sLinear, input.Uv + float2(-h.x, h.y), 0).r + tAo.SampleLevel(sLinear, input.Uv + float2(h.x, h.y), 0).r) * 0.25;
	c *= lerp(1, ao, Ao.y);
	c += tBloom.SampleLevel(sLinear, input.Uv, 0).rgb * Bloom.y;
	return float4(saturate(c), 1);
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
	uint32_t width = 0, height = 0;
	RefCntAutoPtr<ITexture> color, depth, ao, bloomA, bloomB;
	RefCntAutoPtr<IBuffer> buffer;
	RefCntAutoPtr<IShader> vs;
	Pass aoPass, brightPass, blurPass[2], compositePass;

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

	void run(IDeviceContext* ctx, Pass& pass, ITexture* into)
	{
		ITextureView* rtv = into->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
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
	cb.Size = 64;
	cb.BindFlags = BIND_UNIFORM_BUFFER;
	cb.Usage = USAGE_DYNAMIC;
	cb.CPUAccessFlags = CPU_ACCESS_WRITE;
	device->CreateBuffer(cb, nullptr, &_impl->buffer);
	_impl->vs = _impl->shader(SHADER_TYPE_VERTEX, "VS");
	_impl->makePass(_impl->aoPass, "PS_Ao", { "tDepth" }, TEX_FORMAT_R8_UNORM);
	_impl->makePass(_impl->brightPass, "PS_Bright", { "tColor" }, TEX_FORMAT_RGBA8_UNORM);
	_impl->makePass(_impl->blurPass[0], "PS_Blur", { "tBloom" }, TEX_FORMAT_RGBA8_UNORM);
	_impl->makePass(_impl->blurPass[1], "PS_Blur", { "tBloom" }, TEX_FORMAT_RGBA8_UNORM);
	_impl->makePass(_impl->compositePass, "PS_Composite", { "tColor", "tAo", "tBloom" }, colorFormat);
}

DiligentPostProcess::~DiligentPostProcess()
{
	delete _impl;
}

void DiligentPostProcess::resize(uint32_t width, uint32_t height)
{
	if (width == _impl->width && height == _impl->height && _impl->color)
		return;
	_impl->width = width;
	_impl->height = height;
	const uint32_t hw = (width + 1) / 2, hh = (height + 1) / 2;
	_impl->color = _impl->target("Scene colour", width, height, _impl->colorFormat, BIND_RENDER_TARGET);
	_impl->depth = _impl->target("Scene depth", width, height, _impl->depthFormat, BIND_DEPTH_STENCIL);
	_impl->ao = _impl->target("Ambient occlusion", hw, hh, TEX_FORMAT_R8_UNORM, BIND_RENDER_TARGET);
	_impl->bloomA = _impl->target("Bloom A", hw, hh, TEX_FORMAT_RGBA8_UNORM, BIND_RENDER_TARGET);
	_impl->bloomB = _impl->target("Bloom B", hw, hh, TEX_FORMAT_RGBA8_UNORM, BIND_RENDER_TARGET);
	_impl->bind(_impl->aoPass, { _impl->depth });
	_impl->bind(_impl->brightPass, { _impl->color });
	_impl->bind(_impl->blurPass[0], { _impl->bloomA });
	_impl->bind(_impl->blurPass[1], { _impl->bloomB });
	_impl->bind(_impl->compositePass, { _impl->color, _impl->ao, _impl->bloomA });
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
	auto write = [&](float blurX, float blurY) {
		void* mapped = nullptr;
		ctx->MapBuffer(_impl->buffer, MAP_WRITE, MAP_FLAG_DISCARD, mapped);
		const float data[16] = {
			projection._11, projection._22, projection._33, projection._43,
			1.0f / _impl->width, 1.0f / _impl->height, 1.0f / hw, 1.0f / hh,
			settings.ssaoRadius, settings.ssao ? settings.ssaoStrength : 0.0f, 0, 0,
			settings.bloomThreshold, settings.bloom ? settings.bloomIntensity : 0.0f, blurX, blurY,
		};
		memcpy(mapped, data, sizeof(data));
		ctx->UnmapBuffer(_impl->buffer, MAP_WRITE);
	};
	write(0, 0);
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
		write(1.5f, 0);
		_impl->run(ctx, _impl->blurPass[0], _impl->bloomB);
		write(0, 1.5f);
		_impl->run(ctx, _impl->blurPass[1], _impl->bloomA);
	}
	else {
		ITextureView* rtv = _impl->bloomA->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
		ctx->SetRenderTargets(1, &rtv, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
		ctx->ClearRenderTarget(rtv, black, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	}
	ctx->SetRenderTargets(1, &backBufferRTV, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	ctx->SetViewports(1, nullptr, 0, 0);
	ctx->SetPipelineState(_impl->compositePass.pipeline);
	ctx->CommitShaderResources(_impl->compositePass.srb, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	ctx->Draw(DrawAttribs{ 3, DRAW_FLAG_VERIFY_ALL });
}
