// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#include "DiligentRayShadows.h"
#include "../util/util.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iterator>

using namespace Diligent;

// The shadow mask: per pixel the world position from the depth, its normal from the
// neighbours' positions, one ray from just off the surface towards a point of the sun's disc
static const char maskShaderCode[] = R"---(
RaytracingAccelerationStructure tlas;
Texture2D<float> tDepth;

cbuffer RtBuffer
{
	float4 Proj;         // P11, P22, P33, P43
	float4 Texel;        // 1 / width, 1 / height, the frame (the jitter), the sun's angular radius
	float4 SunDir;       // towards the sun
	float4x4 InverseView; // view -> world (row vectors)
};

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

float3 WorldAt(float2 uv, float d)
{
	float z = Proj.w / (d - Proj.z);
	float2 ndc = float2(uv.x * 2 - 1, 1 - uv.y * 2);
	return mul(float4(ndc.x * z / Proj.x, ndc.y * z / Proj.y, z, 1), InverseView).xyz;
}

float PS(VS_OUTPUT input) : SV_Target
{
	int2 px = int2(input.Pos.xy);
	float d = tDepth.Load(int3(px, 0));
	if (d >= 0.99999)
		return 1;
	float3 p = WorldAt(input.Uv, d);
	// the normal from the nearer neighbour on each axis (no smearing over silhouettes)
	float3 px1 = WorldAt(input.Uv + float2(Texel.x, 0), tDepth.Load(int3(px + int2(1, 0), 0)));
	float3 px0 = WorldAt(input.Uv - float2(Texel.x, 0), tDepth.Load(int3(px - int2(1, 0), 0)));
	float3 py1 = WorldAt(input.Uv + float2(0, Texel.y), tDepth.Load(int3(px + int2(0, 1), 0)));
	float3 py0 = WorldAt(input.Uv - float2(0, Texel.y), tDepth.Load(int3(px - int2(0, 1), 0)));
	float3 dx = distance(px1, p) < distance(px0, p) ? px1 - p : p - px0;
	float3 dy = distance(py1, p) < distance(py0, p) ? py1 - p : p - py0;
	float3 n = normalize(cross(dy, dx));
	float3 camPos = InverseView[3].xyz;
	if (dot(n, camPos - p) < 0)
		n = -n;
	float3 l = normalize(SunDir.xyz);
	if (dot(n, l) <= 0.01)
		return 1; // turned away from the sun: its shading is dark already
	// a point of the sun's disc, moved every frame (interleaved gradient noise)
	float2 q = input.Pos.xy + 5.588238 * fmod(Texel.z, 64);
	float r1 = frac(52.9829189 * frac(dot(q, float2(0.06711056, 0.00583715))));
	float r2 = frac(52.9829189 * frac(dot(q.yx + 17.3, float2(0.06711056, 0.00583715))));
	float3 t = normalize(cross(l, abs(l.y) < 0.9 ? float3(0, 1, 0) : float3(1, 0, 0)));
	float3 b = cross(l, t);
	float a = r1 * 6.2831853, rr = sqrt(r2) * Texel.w;
	float3 dir = normalize(l + (t * cos(a) + b * sin(a)) * rr);
	float dist = length(p - camPos);
	RayDesc ray;
	ray.Origin = p + n * (0.05 + dist * 0.0008);
	ray.TMin = 0.01;
	ray.Direction = dir;
	ray.TMax = 500;
	RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES | RAY_FLAG_FORCE_OPAQUE> rq;
	rq.TraceRayInline(tlas, RAY_FLAG_NONE, 0xFF, ray);
	rq.Proceed();
	return rq.CommittedStatus() == COMMITTED_TRIANGLE_HIT ? 0 : 1;
}
)---";

bool DiligentRayShadows::Supported(IRenderDevice* device)
{
	const auto& info = device->GetDeviceInfo();
	const auto& rt = device->GetAdapterInfo().RayTracing;
	return info.Features.RayTracing == DEVICE_FEATURE_STATE_ENABLED && (rt.CapFlags & RAY_TRACING_CAP_FLAG_INLINE_RAY_TRACING) != 0;
}

DiligentRayShadows::DiligentRayShadows(IRenderDevice* device) : device(device)
{
	BufferDesc cb;
	cb.Name = "Ray-traced shadows";
	cb.Size = 112;
	cb.BindFlags = BIND_UNIFORM_BUFFER;
	cb.Usage = USAGE_DYNAMIC;
	cb.CPUAccessFlags = CPU_ACCESS_WRITE;
	device->CreateBuffer(cb, nullptr, &constants);

	const PipelineResourceDesc resources[] = {
		{ SHADER_TYPE_PIXEL, "RtBuffer", 1, SHADER_RESOURCE_TYPE_CONSTANT_BUFFER, SHADER_RESOURCE_VARIABLE_TYPE_STATIC },
		{ SHADER_TYPE_PIXEL, "tDepth", 1, SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
		{ SHADER_TYPE_PIXEL, "tlas", 1, SHADER_RESOURCE_TYPE_ACCEL_STRUCT, SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
	};
	PipelineResourceSignatureDesc sig;
	sig.Name = "Ray-traced shadows";
	sig.Resources = resources;
	sig.NumResources = (Uint32)std::size(resources);
	device->CreatePipelineResourceSignature(sig, &signature);
	if (!signature)
		ferr("Diligent: could not create the ray-traced shadow signature.");
	signature->GetStaticVariableByName(SHADER_TYPE_PIXEL, "RtBuffer")->Set(constants);
	auto shader = [&](SHADER_TYPE type, const char* entry) {
		ShaderCreateInfo sci;
		sci.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
		sci.ShaderCompiler = SHADER_COMPILER_DXC;
		sci.HLSLVersion = ShaderVersion{ 6, 5 };
		sci.Source = maskShaderCode;
		sci.SourceLength = sizeof(maskShaderCode) - 1;
		sci.EntryPoint = entry;
		sci.Desc.ShaderType = type;
		sci.Desc.Name = "Ray-traced shadows";
		RefCntAutoPtr<IShader> s;
		device->CreateShader(sci, &s);
		if (!s)
			printf("Ray-traced shadows: could not compile %s (DXC, shader model 6.5): off\n", entry);
		return s;
	};
	RefCntAutoPtr<IShader> vs = shader(SHADER_TYPE_VERTEX, "VS"), ps = shader(SHADER_TYPE_PIXEL, "PS");
	if (!vs || !ps)
		return;
	GraphicsPipelineStateCreateInfo ci;
	ci.PSODesc.Name = "Ray-traced shadows";
	ci.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;
	auto& gp = ci.GraphicsPipeline;
	gp.NumRenderTargets = 1;
	gp.RTVFormats[0] = TEX_FORMAT_R8_UNORM;
	gp.DSVFormat = TEX_FORMAT_UNKNOWN;
	gp.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	gp.RasterizerDesc.CullMode = CULL_MODE_NONE;
	gp.DepthStencilDesc.DepthEnable = False;
	gp.DepthStencilDesc.DepthWriteEnable = False;
	ci.pVS = vs;
	ci.pPS = ps;
	IPipelineResourceSignature* sigs[] = { signature };
	ci.ppResourceSignatures = sigs;
	ci.ResourceSignaturesCount = 1;
	device->CreateGraphicsPipelineState(ci, &pipeline);
	ok = pipeline != nullptr;
	printf("Ray-traced shadows: %s\n", ok ? "ready" : "could not create the pipeline: off");
}

IBottomLevelAS* DiligentRayShadows::blas(IDeviceContext* ctx, const void* key, uint32_t frame, IBuffer* positions,
	uint64_t positionOffset, uint32_t vertexCount, IBuffer* indices, const std::vector<Group>& groups)
{
	auto& slot = blases[{ key, frame }];
	if (slot)
		return slot;
	std::vector<BLASTriangleDesc> descs;
	std::vector<BLASBuildTriangleData> data;
	std::vector<std::string> geometryNames;
	geometryNames.reserve(groups.size());
	for (size_t g = 0; g < groups.size(); g++) {
		const Group& gr = groups[g];
		if (gr.indexCount < 3 || gr.vertexCount == 0)
			continue;
		geometryNames.push_back("g" + std::to_string(g));
		BLASTriangleDesc d;
		d.GeometryName = geometryNames.back().c_str();
		d.MaxVertexCount = gr.vertexCount;
		d.VertexValueType = VT_FLOAT32;
		d.VertexComponentCount = 3;
		d.MaxPrimitiveCount = gr.indexCount / 3;
		d.IndexType = VT_UINT16;
		descs.push_back(d);
		BLASBuildTriangleData b;
		b.GeometryName = d.GeometryName;
		b.pVertexBuffer = positions;
		b.VertexOffset = positionOffset + (uint64_t)gr.firstVertex * 12;
		b.VertexStride = 12;
		b.VertexCount = gr.vertexCount;
		b.VertexValueType = VT_FLOAT32;
		b.VertexComponentCount = 3;
		b.PrimitiveCount = gr.indexCount / 3;
		b.pIndexBuffer = indices;
		b.IndexOffset = (uint64_t)gr.firstIndex * 2;
		b.IndexType = VT_UINT16;
		b.Flags = RAYTRACING_GEOMETRY_FLAG_OPAQUE;
		data.push_back(b);
	}
	if (descs.empty())
		return nullptr;
	BottomLevelASDesc desc;
	desc.Name = "Shadow caster";
	desc.pTriangles = descs.data();
	desc.TriangleCount = (Uint32)descs.size();
	desc.Flags = RAYTRACING_BUILD_AS_PREFER_FAST_TRACE;
	device->CreateBLAS(desc, &slot);
	if (!slot)
		return nullptr;
	const Uint64 need = slot->GetScratchBufferSizes().Build;
	if (!scratch || scratch->GetDesc().Size < need) {
		BufferDesc sd;
		sd.Name = "BLAS scratch";
		sd.Size = std::max<Uint64>(need, 1 << 20);
		sd.BindFlags = BIND_RAY_TRACING;
		scratch.Release();
		device->CreateBuffer(sd, nullptr, &scratch);
	}
	BuildBLASAttribs ba;
	ba.pBLAS = slot;
	ba.BLASTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
	ba.GeometryTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
	ba.pTriangleData = data.data();
	ba.TriangleDataCount = (Uint32)data.size();
	ba.pScratchBuffer = scratch;
	ba.ScratchBufferTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
	ctx->BuildBLAS(ba);
	return slot;
}

void DiligentRayShadows::add(IBottomLevelAS* b, const Matrix& transform)
{
	if (b)
		instances.push_back({ b, transform });
}

void DiligentRayShadows::build(IDeviceContext* ctx)
{
	lastInstanceCount = instances.size();
	built = false;
	if (instances.empty())
		return;
	const Uint32 count = (Uint32)instances.size();
	if (count > tlasCapacity) {
		tlasCapacity = std::max<Uint32>(count * 2, 1024);
		TopLevelASDesc td;
		td.Name = "Shadow casters";
		td.MaxInstanceCount = tlasCapacity;
		td.Flags = RAYTRACING_BUILD_AS_PREFER_FAST_BUILD;
		tlas.Release();
		device->CreateTLAS(td, &tlas);
		BufferDesc ib;
		ib.Name = "TLAS instances";
		ib.Size = (Uint64)TLAS_INSTANCE_DATA_SIZE * tlasCapacity;
		ib.BindFlags = BIND_RAY_TRACING;
		instanceBuffer.Release();
		device->CreateBuffer(ib, nullptr, &instanceBuffer);
		BufferDesc sd;
		sd.Name = "TLAS scratch";
		sd.Size = tlas->GetScratchBufferSizes().Build;
		sd.BindFlags = BIND_RAY_TRACING;
		tlasScratch.Release();
		device->CreateBuffer(sd, nullptr, &tlasScratch);
		boundTlas = nullptr;
	}
	while (names.size() < count)
		names.push_back("i" + std::to_string(names.size()));
	std::vector<TLASBuildInstanceData> data(count);
	for (Uint32 i = 0; i < count; i++) {
		TLASBuildInstanceData& d = data[i];
		d.InstanceName = names[i].c_str();
		d.pBLAS = instances[i].blas;
		const Matrix& m = instances[i].transform;
		for (int r = 0; r < 3; r++) {
			for (int c = 0; c < 3; c++)
				d.Transform.data[r][c] = m.m[c][r];
			d.Transform.data[r][3] = m.m[3][r];
		}
		d.Mask = 0xFF;
	}
	BuildTLASAttribs ta;
	ta.pTLAS = tlas;
	ta.TLASTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
	ta.BLASTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
	ta.pInstances = data.data();
	ta.InstanceCount = count;
	ta.pInstanceBuffer = instanceBuffer;
	ta.InstanceBufferTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
	ta.pScratchBuffer = tlasScratch;
	ta.ScratchBufferTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
	ta.BindingMode = HIT_GROUP_BINDING_MODE_PER_TLAS;
	ctx->BuildTLAS(ta);
	built = true;
}

ITextureView* DiligentRayShadows::renderMask(IDeviceContext* ctx, ITexture* depth, const float projection[4],
	const Matrix& inverseView, const Vector3& sunDirection, uint64_t frameIndex)
{
	if (!built || !depth)
		return nullptr;
	const TextureDesc& dd = depth->GetDesc();
	if (!mask || mask->GetDesc().Width != dd.Width || mask->GetDesc().Height != dd.Height) {
		TextureDesc td;
		td.Name = "Ray-traced shadow mask";
		td.Type = RESOURCE_DIM_TEX_2D;
		td.Width = dd.Width;
		td.Height = dd.Height;
		td.Format = TEX_FORMAT_R8_UNORM;
		td.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;
		mask.Release();
		device->CreateTexture(td, nullptr, &mask);
	}
	if (!srb || boundDepth != depth || boundTlas != tlas.RawPtr()) {
		srb.Release();
		signature->CreateShaderResourceBinding(&srb, true);
		srb->GetVariableByName(SHADER_TYPE_PIXEL, "tDepth")->Set(depth->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
		srb->GetVariableByName(SHADER_TYPE_PIXEL, "tlas")->Set(tlas);
		boundDepth = depth;
		boundTlas = tlas;
	}
	void* mapped = nullptr;
	ctx->MapBuffer(constants, MAP_WRITE, MAP_FLAG_DISCARD, mapped);
	const Vector3 sun = sunDirection.normal();
	float* f = (float*)mapped;
	const float head[12] = {
		projection[0], projection[1], projection[2], projection[3],
		1.0f / dd.Width, 1.0f / dd.Height, (float)(frameIndex % 64), 0.0093f, // the sun's disc: 0.53 degrees across
		sun.x, sun.y, sun.z, 0,
	};
	memcpy(f, head, sizeof(head));
	const Matrix t = inverseView.getTranspose();
	memcpy(f + 12, &t, sizeof(Matrix));
	ctx->UnmapBuffer(constants, MAP_WRITE);
	ITextureView* rtv = mask->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
	ctx->SetRenderTargets(1, &rtv, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	ctx->SetViewports(1, nullptr, 0, 0);
	ctx->SetPipelineState(pipeline);
	ctx->CommitShaderResources(srb, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	ctx->Draw(DrawAttribs{ 3, DRAW_FLAG_VERIFY_ALL });
	return mask->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
}
