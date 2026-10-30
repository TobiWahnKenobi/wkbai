// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

// IRenderer on Diligent Engine (Direct3D 12 or Vulkan).
// It reproduces the former D3D11 renderer (renderer_d3d11.cpp): same shaders and the same state
// behaviour. IRenderer's state toggles select one of a few pipeline states, which are created
// on first use; every pipeline shares one resource signature, so each texture has its own
// shader resource binding that works with all of them.

#include "renderer_diligent.h"
#include "DiligentPostProcess.h"
#include "bitmap.h"
#include "../window.h"
#include "../settings.h"
#include "RenderSettings.h"
#include "../util/util.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

#include "EngineFactoryD3D12.h"
#include "EngineFactoryVk.h"

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

using namespace Diligent;

// Picks the Vulkan physical device: discrete over integrated, then the newest API and driver
// version. The same GPU can be listed twice when two GPUs have different drivers (seen with a
// Boot Camp laptop GPU plus an eGPU: the eGPU also appears through the laptop GPU's older
// driver, which fails to allocate buffers). Diligent alone keeps the first discrete GPU.
// Returns DEFAULT_ADAPTER_ID if Vulkan can't be queried.
static Uint32 PickVulkanAdapter()
{
	HMODULE lib = LoadLibraryA("vulkan-1.dll");
	if (!lib)
		return DEFAULT_ADAPTER_ID;
	auto getProc = (PFN_vkGetInstanceProcAddr)GetProcAddress(lib, "vkGetInstanceProcAddr");
	auto createInstance = getProc ? (PFN_vkCreateInstance)getProc(nullptr, "vkCreateInstance") : nullptr;
	Uint32 best = DEFAULT_ADAPTER_ID;
	VkInstance instance = VK_NULL_HANDLE;
	VkInstanceCreateInfo ici{ VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
	if (createInstance && createInstance(&ici, nullptr, &instance) == VK_SUCCESS) {
		auto enumerate = (PFN_vkEnumeratePhysicalDevices)getProc(instance, "vkEnumeratePhysicalDevices");
		auto getProps = (PFN_vkGetPhysicalDeviceProperties)getProc(instance, "vkGetPhysicalDeviceProperties");
		auto destroyInstance = (PFN_vkDestroyInstance)getProc(instance, "vkDestroyInstance");
		uint32_t count = 0;
		enumerate(instance, &count, nullptr);
		std::vector<VkPhysicalDevice> devices(count);
		enumerate(instance, &count, devices.data());
		auto typeRank = [](VkPhysicalDeviceType t) {
			return t == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 3 : t == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 2 : t == VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU ? 1 : 0;
		};
		VkPhysicalDeviceProperties bestProps{};
		for (uint32_t i = 0; i < count; i++) {
			VkPhysicalDeviceProperties props;
			getProps(devices[i], &props);
			printf("Vulkan device %u: %s, API %u.%u.%u, driver 0x%08X\n", i, props.deviceName, VK_API_VERSION_MAJOR(props.apiVersion),
				VK_API_VERSION_MINOR(props.apiVersion), VK_API_VERSION_PATCH(props.apiVersion), props.driverVersion);
			bool better = best == DEFAULT_ADAPTER_ID;
			if (!better) {
				int a = typeRank(props.deviceType), b = typeRank(bestProps.deviceType);
				better = (a != b) ? (a > b) : (props.apiVersion != bestProps.apiVersion) ? (props.apiVersion > bestProps.apiVersion) : (props.driverVersion > bestProps.driverVersion);
			}
			if (better) {
				best = i;
				bestProps = props;
			}
		}
		destroyInstance(instance, nullptr);
	}
	FreeLibrary(lib);
	return best;
}

static const char diligentShaderCode[] = R"---(
Texture2D inpTexture;
SamplerState inpSampler;

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

struct VS_OUTPUT
{
	float4 Pos : SV_POSITION;
	float4 Color : COLOR0;
	float2 Texcoord : TEXCOORD0;
	float Fog : FOG;
};

static const float ALPHA_REF = 0.94;

VS_OUTPUT VS(float3 Pos : ATTRIB0, float4 Color : ATTRIB1, float2 Texcoord : ATTRIB2)
{
	VS_OUTPUT output = (VS_OUTPUT)0;
	output.Pos = mul(float4(Pos, 1.0), Transform);
	output.Color = Color.bgra;
	output.Texcoord = Texcoord;
	output.Fog = 0.0;
	return output;
}

VS_OUTPUT VS_Fog(float3 Pos : ATTRIB0, float4 Color : ATTRIB1, float2 Texcoord : ATTRIB2)
{
	VS_OUTPUT output = (VS_OUTPUT)0;
	output.Pos = mul(float4(Pos, 1.0), Transform);
	output.Color = Color.bgra;
	output.Texcoord = Texcoord;
	output.Fog = FogAmount(Pos, output.Pos.w);
	return output;
}

float4 ApplyFog(float4 color, float fog)
{
	return float4(lerp(color.rgb, FogColor.rgb, fog), color.a);
}

float4 PS(VS_OUTPUT input) : SV_Target
{
	float4 tex = inpTexture.Sample(inpSampler, input.Texcoord);
	return ApplyFog(input.Color * tex, input.Fog);
}

float4 PS_AlphaTest(VS_OUTPUT input) : SV_Target
{
	float4 tex = inpTexture.Sample(inpSampler, input.Texcoord);
	if(tex.a < ALPHA_REF) discard;
	return ApplyFog(input.Color * tex, input.Fog);
}

float4 PS_Map(VS_OUTPUT input) : SV_Target
{
	// Compute the length of a half pixel in the current mipmap
	float lod = inpTexture.CalculateLevelOfDetail(inpSampler, input.Texcoord);
	uint aw, ah, levels; // the atlas: 256 texels, or more in a high-resolution mod
	inpTexture.GetDimensions(0, aw, ah, levels);
	float ha = 0.5 / max(aw >> (int)ceil(lod), 1u);

	float2 stc = clamp(input.Texcoord, float2(ha,ha), float2(1.0-ha,1.0-ha));
	float2 tile = floor(stc / float2(0.25,0.25)); // tile index in the 4x4 atlas
	float2 subuv = fmod(stc, float2(0.25,0.25)); // coords inside the used tile

	float2 fuv = clamp(subuv, float2(ha,ha), float2(0.25-ha,0.25-ha)) + tile*0.25;
	float4 tex = inpTexture.Sample(inpSampler, fuv);
	return ApplyFog(input.Color * tex, input.Fog);
}

float4 PS_Lake(VS_OUTPUT input) : SV_Target
{
	return ApplyFog(input.Color, input.Fog);
}
)---";

static void DILIGENT_CALL_TYPE DiligentMessage(DEBUG_MESSAGE_SEVERITY severity, const Char* message, const Char* function, const Char* file, int line)
{
	static const char* const names[] = { "info", "warning", "error", "fatal error" };
	printf("Diligent %s: %s\n", names[std::min((int)severity, 3)], message ? message : "");
}

struct DiligentTexture
{
	RefCntAutoPtr<ITexture> tex;
	RefCntAutoPtr<IShaderResourceBinding> srb;
};

// Vertex/index buffers (ImGui creates and fills them every frame). They are dynamic buffers,
// which are cheap to create, but whose GPU memory only lives for one frame, so the contents are
// kept on the CPU and copied again if the buffer is drawn in a later frame.
template <typename T> struct DiligentStreamBuffer
{
	IDeviceContext* ctx;
	const uint64_t* currentFrame;
	RefCntAutoPtr<IBuffer> buf;
	std::vector<T> data;
	uint64_t uploadedFrame = ~0ull;

	void upload()
	{
		void* mapped = nullptr;
		ctx->MapBuffer(buf, MAP_WRITE, MAP_FLAG_DISCARD, mapped);
		memcpy(mapped, data.data(), data.size() * sizeof(T));
		ctx->UnmapBuffer(buf, MAP_WRITE);
		uploadedFrame = *currentFrame;
	}
	IBuffer* get()
	{
		if (uploadedFrame != *currentFrame)
			upload();
		return buf;
	}
};

struct RVertexBufferDiligent : public RVertexBuffer
{
	DiligentStreamBuffer<batchVertex> stream;
	batchVertex* lock() override { return stream.data.data(); }
	void unlock() override { stream.upload(); }
};

struct RIndexBufferDiligent : public RIndexBuffer
{
	DiligentStreamBuffer<uint16_t> stream;
	uint16_t* lock() override { return stream.data.data(); }
	void unlock() override { stream.upload(); }
};

struct DiligentRenderer;

// Batches are filled on the CPU and copied into the renderer's stream buffers when flushed, so
// a batch may stay partly filled across a frame boundary (dynamic memory lives for one frame).
struct RBatchDiligent : public RBatch
{
	DiligentRenderer* gfx;
	std::vector<batchVertex> verts;
	std::vector<uint16_t> indis;

	void begin() override {}
	void end() override {}
	void next(uint32_t nverts, uint32_t nindis, batchVertex** vpnt, uint16_t** ipnt, uint32_t* fi) override
	{
		if (nverts > maxverts) ferr("Too many vertices to fit in the batch.");
		if (nindis > maxindis) ferr("Too many indices to fit in the batch.");
		if ((curverts + nverts > maxverts) || (curindis + nindis > maxindis))
			flush();
		*vpnt = verts.data() + curverts;
		*ipnt = indis.data() + curindis;
		*fi = curverts;
		curverts += nverts; curindis += nindis;
	}
	void flush() override;
};

struct DiligentRenderer : DiligentGfx
{
	// Pipeline state selected by the IRenderer calls
	enum VSKind : uint8_t { VS_PLAIN, VS_FOG };
	enum PSKind : uint8_t { PS_PLAIN, PS_ALPHATEST, PS_MAP, PS_LAKE };
	enum DepthKind : uint8_t { DEPTH_DEFAULT, DEPTH_ON, DEPTH_OFF, DEPTH_TESTONLY };
	enum TopologyKind : uint8_t { TOPO_TRIANGLES, TOPO_LINES };
	struct State {
		VSKind vs = VS_PLAIN;
		PSKind ps = PS_PLAIN;
		bool alphaBlend = false;
		DepthKind depth = DEPTH_DEFAULT;
		bool scissor = false;
		TopologyKind topology = TOPO_TRIANGLES;
		uint32_t key() const { return vs | (ps << 1) | (alphaBlend << 3) | (depth << 4) | (scissor << 6) | (topology << 7); }
	};

	RefCntAutoPtr<IPipelineResourceSignature> signature;
	RefCntAutoPtr<IShader> vertexShaders[2], pixelShaders[4];
	std::map<uint32_t, RefCntAutoPtr<IPipelineState>> pipelines;

	// Dynamic buffers that batches and shapes are appended to: the first write in a frame (or
	// when full) discards, later writes add behind the previous ones, so each draw only takes
	// the bytes it uses from the per-frame dynamic heap.
	struct StreamRing {
		RefCntAutoPtr<IBuffer> buf;
		uint64_t capacity = 0, cursor = 0, mappedFrame = ~0ull;
	};
	StreamRing vertexRing, indexRing;

	// post-processing: made when first used; on in this frame (the 3D scene drawn offscreen)
	std::unique_ptr<DiligentPostProcess> post;
	bool postActive = false;
	texture whiteTexture = nullptr;
	texture currentTexture = nullptr;
	State state;
	Matrix transform = Matrix::getIdentity();
	uint32_t fogColor = 0;
	float fogFar = 250.0f;
	Vector3 fogCamera;
	float fogEdge[4] = { 1e9f, 1e9f, 1e9f, 2e9f }; // no edge fog until a map sets it
	bool fogEnabled = false;
	bool isVulkan = false;
	uint64_t frame = 0; // presented frames, for buffers whose dynamic memory expires

	RefCntAutoPtr<IShader> createShader(SHADER_TYPE type, const char* entryPoint)
	{
		ShaderCreateInfo sci;
		sci.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
		sci.Source = diligentShaderCode;
		sci.SourceLength = sizeof(diligentShaderCode) - 1;
		sci.EntryPoint = entryPoint;
		sci.Desc.ShaderType = type;
		sci.Desc.Name = entryPoint;
		RefCntAutoPtr<IShader> shader;
		device->CreateShader(sci, &shader);
		if (!shader)
			ferr("Diligent: failed to compile shader %s", entryPoint);
		return shader;
	}

	RefCntAutoPtr<IBuffer> createBuffer(const char* name, uint64_t size, BIND_FLAGS bind, USAGE usage)
	{
		BufferDesc desc;
		desc.Name = name;
		desc.Size = size;
		desc.BindFlags = bind;
		desc.Usage = usage;
		if (usage == USAGE_DYNAMIC)
			desc.CPUAccessFlags = CPU_ACCESS_WRITE;
		RefCntAutoPtr<IBuffer> buf;
		device->CreateBuffer(desc, nullptr, &buf);
		if (!buf)
			ferr("Diligent: could not create buffer \"%s\" (%llu bytes)", name, (unsigned long long)size);
		return buf;
	}

	IPipelineState* getPipeline(const State& st)
	{
		auto& pso = pipelines[st.key()];
		if (pso)
			return pso;

		GraphicsPipelineStateCreateInfo ci;
		std::string name = "wkbre2 PSO " + std::to_string(st.key());
		ci.PSODesc.Name = name.c_str();
		ci.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;
		auto& gp = ci.GraphicsPipeline;
		gp.NumRenderTargets = 1;
		gp.RTVFormats[0] = swapChain->GetDesc().ColorBufferFormat;
		gp.DSVFormat = swapChain->GetDesc().DepthBufferFormat;
		gp.PrimitiveTopology = (st.topology == TOPO_LINES) ? PRIMITIVE_TOPOLOGY_LINE_LIST : PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

		// Like D3D11's default rasterizer state (cull back faces), except that the D3D11
		// renderer's scissor state also disables culling.
		gp.RasterizerDesc.CullMode = st.scissor ? CULL_MODE_NONE : CULL_MODE_BACK;
		gp.RasterizerDesc.FrontCounterClockwise = False;
		gp.RasterizerDesc.ScissorEnable = st.scissor;

		auto& ds = gp.DepthStencilDesc;
		switch (st.depth) {
		case DEPTH_DEFAULT: ds.DepthEnable = True; ds.DepthWriteEnable = True; ds.DepthFunc = COMPARISON_FUNC_LESS; break;
		case DEPTH_ON: ds.DepthEnable = True; ds.DepthWriteEnable = True; ds.DepthFunc = COMPARISON_FUNC_LESS_EQUAL; break;
		case DEPTH_OFF: ds.DepthEnable = False; ds.DepthWriteEnable = False; ds.DepthFunc = COMPARISON_FUNC_ALWAYS; break;
		case DEPTH_TESTONLY: ds.DepthEnable = True; ds.DepthWriteEnable = False; ds.DepthFunc = COMPARISON_FUNC_LESS_EQUAL; break;
		}

		auto& rt = gp.BlendDesc.RenderTargets[0];
		rt.BlendEnable = st.alphaBlend;
		rt.SrcBlend = BLEND_FACTOR_SRC_ALPHA;
		rt.DestBlend = BLEND_FACTOR_INV_SRC_ALPHA;
		rt.BlendOp = BLEND_OPERATION_ADD;
		rt.SrcBlendAlpha = BLEND_FACTOR_SRC_ALPHA;
		rt.DestBlendAlpha = BLEND_FACTOR_INV_SRC_ALPHA;
		rt.BlendOpAlpha = BLEND_OPERATION_ADD;

		// batchVertex: position, colour (bytes B, G, R, A), texture coordinates
		static const LayoutElement layout[] = {
			LayoutElement{0, 0, 3, VT_FLOAT32, False},
			LayoutElement{1, 0, 4, VT_UINT8, True},
			LayoutElement{2, 0, 2, VT_FLOAT32, False},
		};
		gp.InputLayout.LayoutElements = layout;
		gp.InputLayout.NumElements = (Uint32)std::size(layout);

		ci.pVS = vertexShaders[st.vs];
		ci.pPS = pixelShaders[st.ps];
		IPipelineResourceSignature* signatures[] = { signature };
		ci.ppResourceSignatures = signatures;
		ci.ResourceSignaturesCount = 1;

		device->CreateGraphicsPipelineState(ci, &pso);
		if (!pso)
			ferr("Diligent: failed to create pipeline state %u", st.key());
		return pso;
	}

	// Binds the pipeline and the texture for the current state, before a draw call.
	void prepareDraw(TopologyKind topology)
	{
		State st = state;
		st.topology = topology;
		ctx->SetPipelineState(getPipeline(st));
		DiligentTexture* t = (DiligentTexture*)(currentTexture ? currentTexture : whiteTexture);
		ctx->CommitShaderResources(t->srb, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	}

	ITextureView* currentColorTarget() { return postActive ? post->colorRTV() : swapChain->GetCurrentBackBufferRTV(); }
	ITextureView* currentDepthTarget() { return postActive ? post->depthDSV() : swapChain->GetDepthBufferDSV(); }

	void setRenderTargets()
	{
		ITextureView* rtv = currentColorTarget();
		ctx->SetRenderTargets(1, &rtv, currentDepthTarget(), RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	}

	void BeginPostProcessing(bool on) override
	{
		postActive = false;
		if (!on)
			return;
		if (!post)
			post = std::make_unique<DiligentPostProcess>(device, swapChain->GetDesc().ColorBufferFormat, swapChain->GetDesc().DepthBufferFormat);
		post->resize(swapChain->GetDesc().Width, swapChain->GetDesc().Height);
		postActive = true;
	}

	void ApplyPostProcessing(const Matrix& projection) override
	{
		if (!postActive)
			return;
		postActive = false;
		DiligentPostProcess::Settings s;
		s.ssao = g_renderSettings.ssao;
		s.ssaoRadius = g_renderSettings.ssaoRadius;
		s.ssaoStrength = g_renderSettings.ssaoStrength;
		s.bloom = g_renderSettings.bloom;
		s.bloomThreshold = g_renderSettings.bloomThreshold;
		s.bloomIntensity = g_renderSettings.bloomIntensity;
		post->apply(ctx, swapChain->GetCurrentBackBufferRTV(), projection, s);
		setRenderTargets();
		ctx->SetViewports(1, nullptr, 0, 0);
	}

	// Copies data into a stream ring and returns its byte offset there.
	uint64_t streamWrite(StreamRing& ring, const void* data, uint64_t size)
	{
		if (size > ring.capacity)
			ferr("Diligent: %llu bytes don't fit in a %llu-byte stream buffer.", (unsigned long long)size, (unsigned long long)ring.capacity);
		MAP_FLAGS flags = MAP_FLAG_NO_OVERWRITE;
		if (ring.mappedFrame != frame || ring.cursor + size > ring.capacity) {
			flags = MAP_FLAG_DISCARD;
			ring.cursor = 0;
			ring.mappedFrame = frame;
		}
		void* mapped = nullptr;
		ctx->MapBuffer(ring.buf, MAP_WRITE, flags, mapped);
		const uint64_t offset = ring.cursor;
		memcpy((uint8_t*)mapped + offset, data, size);
		ctx->UnmapBuffer(ring.buf, MAP_WRITE);
		ring.cursor += (size + 15) & ~15ull;
		return offset;
	}

	ITextureView* GetTextureView(texture t) override
	{
		return ((DiligentTexture*)(t ? t : whiteTexture))->tex->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
	}

	IBuffer* StreamVertexData(const void* data, uint64_t size, uint64_t& offset) override
	{
		offset = streamWrite(vertexRing, data, size);
		return vertexRing.buf;
	}

	IBuffer* StreamIndexData(const void* data, uint64_t size, uint64_t& offset) override
	{
		offset = streamWrite(indexRing, data, size);
		return indexRing.buf;
	}

	void SetFogCamera(const Vector3& position) override
	{
		fogCamera = position;
		writeFogBuffer();
	}

	void SetFogEdge(float playableWidth, float playableDepth, float start, float opaque) override
	{
		fogEdge[0] = playableWidth;
		fogEdge[1] = playableDepth;
		fogEdge[2] = start;
		fogEdge[3] = opaque;
		writeFogBuffer();
	}

	void RestoreRenderTargets() override
	{
		setRenderTargets();
		ctx->SetViewports(1, nullptr, 0, 0);
	}

	void streamVertices(const batchVertex* verts, uint32_t count)
	{
		IBuffer* vb = vertexRing.buf;
		const Uint64 offset = streamWrite(vertexRing, verts, count * sizeof(batchVertex));
		ctx->SetVertexBuffers(0, 1, &vb, &offset, RESOURCE_STATE_TRANSITION_MODE_TRANSITION, SET_VERTEX_BUFFERS_FLAG_RESET);
	}

	void streamIndices(const uint16_t* indis, uint32_t count)
	{
		const uint64_t offset = streamWrite(indexRing, indis, count * sizeof(uint16_t));
		ctx->SetIndexBuffer(indexRing.buf, offset, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	}

	void writeTransformBuffer()
	{
		void* mapped = nullptr;
		ctx->MapBuffer(transformBuffer, MAP_WRITE, MAP_FLAG_DISCARD, mapped);
		*(Matrix*)mapped = transform.getTranspose();
		ctx->UnmapBuffer(transformBuffer, MAP_WRITE);
	}

	void writeFogBuffer()
	{
		void* mapped = nullptr;
		ctx->MapBuffer(fogBuffer, MAP_WRITE, MAP_FLAG_DISCARD, mapped);
		float* cc = (float*)mapped;
		cc[0] = ((fogColor >> 16) & 255) / 255.0f;
		cc[1] = ((fogColor >> 8) & 255) / 255.0f;
		cc[2] = (fogColor & 255) / 255.0f;
		cc[3] = ((fogColor >> 24) & 255) / 255.0f;
		// The original's fog (0x60A870): linear, ending at 0.95 x far clip - near clip (1),
		// starting halfway there ("fogStart" changes that), so that geometry has faded out
		// before it is clipped.
		const float fogEnd = 0.95f * fogFar - 1.0f;
		cc[4] = fogEnd * std::clamp(g_renderSettings.fogStart, 0.0f, 0.99f);
		cc[5] = fogEnd;
		if (g_renderSettings.fogCustom) {
			// Own near and far, the far end never beyond the view distance
			cc[5] = std::min(g_renderSettings.fogFar, fogFar);
			cc[4] = std::min(g_renderSettings.fogNear, cc[5] * 0.999f);
		}
		if (g_renderSettings.fogOff) {
			// No fog: start and end beyond any distance (the sky shader checks for this too)
			cc[4] = 1e9f;
			cc[5] = 2e9f;
		}
		cc[6] = cc[7] = 0.0f;
		cc[8] = fogCamera.x;
		cc[9] = fogCamera.y;
		cc[10] = fogCamera.z;
		cc[11] = 1.0f;
		cc[12] = g_renderSettings.exposure;
		cc[13] = g_renderSettings.brightness;
		cc[14] = g_renderSettings.contrast;
		cc[15] = g_renderSettings.saturation;
		cc[16] = fogEdge[0];
		cc[17] = fogEdge[1];
		cc[18] = fogEdge[2];
		cc[19] = fogEdge[3];
		ctx->UnmapBuffer(fogBuffer, MAP_WRITE);
	}

	// Initialisation

	void Init() override
	{
		isVulkan = g_settings.value<std::string>("diligentBackend", "d3d12") == "vulkan";
		const int adapter = g_settings.value<int>("adapterIndex", -1);
		const bool validation = g_settings.value<bool>("gfxValidation", false);

		SwapChainDesc scDesc;
		scDesc.Width = g_windowWidth;
		scDesc.Height = g_windowHeight;
		scDesc.ColorBufferFormat = TEX_FORMAT_RGBA8_UNORM;
		scDesc.DepthBufferFormat = TEX_FORMAT_D32_FLOAT;
		scDesc.Usage = SWAP_CHAIN_USAGE_RENDER_TARGET | SWAP_CHAIN_USAGE_COPY_SOURCE; // for SaveScreenshot
		Win32NativeWindow window{ WndGetNativeWindowHandle() };

		IDeviceContext* contexts[1] = {};
		if (isVulkan) {
			IEngineFactoryVk* factory = GetEngineFactoryVk();
			factory->SetMessageCallback(DiligentMessage);
			EngineVkCreateInfo eci;
			eci.EnableValidation = validation;
			eci.DynamicHeapSize = 64 << 20;
			eci.AdapterId = (adapter >= 0) ? (Uint32)adapter : PickVulkanAdapter();
			factory->CreateDeviceAndContextsVk(eci, &device, contexts);
			ctx.Attach(contexts[0]);
			if (!device)
				ferr("Diligent: could not create the Vulkan device.");
			factory->CreateSwapChainVk(device, ctx, scDesc, window, &swapChain);
		}
		else {
			IEngineFactoryD3D12* factory = GetEngineFactoryD3D12();
			factory->SetMessageCallback(DiligentMessage);
			if (!factory->LoadD3D12())
				ferr("Diligent: could not load d3d12.dll.");
			EngineD3D12CreateInfo eci;
			eci.EnableValidation = validation;
			if (adapter >= 0) eci.AdapterId = (Uint32)adapter;
			factory->CreateDeviceAndContextsD3D12(eci, &device, contexts);
			ctx.Attach(contexts[0]);
			if (!device)
				ferr("Diligent: could not create the Direct3D 12 device.");
			factory->CreateSwapChainD3D12(device, ctx, scDesc, FullScreenModeDesc{}, window, &swapChain);
		}
		if (!swapChain)
			ferr("Diligent: could not create the swap chain.");
		printf("Diligent renderer: %s, adapter \"%s\"\n", isVulkan ? "Vulkan" : "Direct3D 12", device->GetAdapterInfo().Description);

		// One resource signature for every pipeline: two constant buffers bound once, and the
		// texture, which changes per draw (one shader resource binding per texture).
		const SHADER_TYPE vsps = SHADER_TYPE_VERTEX | SHADER_TYPE_PIXEL;
		const PipelineResourceDesc resources[] = {
			{ SHADER_TYPE_VERTEX, "TransformBuffer", 1, SHADER_RESOURCE_TYPE_CONSTANT_BUFFER, SHADER_RESOURCE_VARIABLE_TYPE_STATIC },
			{ vsps, "FogBuffer", 1, SHADER_RESOURCE_TYPE_CONSTANT_BUFFER, SHADER_RESOURCE_VARIABLE_TYPE_STATIC },
			{ SHADER_TYPE_PIXEL, "inpTexture", 1, SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
		};
		// D3D11's default sampler: trilinear, clamped
		SamplerDesc sampler;
		sampler.MinFilter = sampler.MagFilter = sampler.MipFilter = FILTER_TYPE_LINEAR;
		sampler.AddressU = sampler.AddressV = sampler.AddressW = TEXTURE_ADDRESS_CLAMP;
		const ImmutableSamplerDesc samplers[] = { { SHADER_TYPE_PIXEL, "inpSampler", sampler } };
		PipelineResourceSignatureDesc sigDesc;
		sigDesc.Name = "wkbre2 resources";
		sigDesc.Resources = resources;
		sigDesc.NumResources = (Uint32)std::size(resources);
		sigDesc.ImmutableSamplers = samplers;
		sigDesc.NumImmutableSamplers = (Uint32)std::size(samplers);
		device->CreatePipelineResourceSignature(sigDesc, &signature);
		if (!signature)
			ferr("Diligent: could not create the resource signature.");

		transformBuffer = createBuffer("Transform", 64, BIND_UNIFORM_BUFFER, USAGE_DYNAMIC);
		fogBuffer = createBuffer("Fog", 80, BIND_UNIFORM_BUFFER, USAGE_DYNAMIC);
		vertexRing.capacity = 2 << 20;
		vertexRing.buf = createBuffer("Vertex stream", vertexRing.capacity, BIND_VERTEX_BUFFER, USAGE_DYNAMIC);
		indexRing.capacity = 512 << 10;
		indexRing.buf = createBuffer("Index stream", indexRing.capacity, BIND_INDEX_BUFFER, USAGE_DYNAMIC);
		signature->GetStaticVariableByName(SHADER_TYPE_VERTEX, "TransformBuffer")->Set(transformBuffer);
		signature->GetStaticVariableByName(SHADER_TYPE_VERTEX, "FogBuffer")->Set(fogBuffer);
		signature->GetStaticVariableByName(SHADER_TYPE_PIXEL, "FogBuffer")->Set(fogBuffer);

		vertexShaders[VS_PLAIN] = createShader(SHADER_TYPE_VERTEX, "VS");
		vertexShaders[VS_FOG] = createShader(SHADER_TYPE_VERTEX, "VS_Fog");
		pixelShaders[PS_PLAIN] = createShader(SHADER_TYPE_PIXEL, "PS");
		pixelShaders[PS_ALPHATEST] = createShader(SHADER_TYPE_PIXEL, "PS_AlphaTest");
		pixelShaders[PS_MAP] = createShader(SHADER_TYPE_PIXEL, "PS_Map");
		pixelShaders[PS_LAKE] = createShader(SHADER_TYPE_PIXEL, "PS_Lake");

		Bitmap whiteBmp;
		whiteBmp.width = 1;
		whiteBmp.height = 1;
		whiteBmp.format = BMFORMAT_R8G8B8A8;
		whiteBmp.pixels.resize(4);
		*(uint32_t*)whiteBmp.pixels.data() = 0xFFFFFFFF;
		whiteTexture = CreateTexture(whiteBmp, 1);
	}

	void Reset() override
	{
		swapChain->Resize(g_windowWidth, g_windowHeight);
	}

	// Frame begin/end

	void BeginDrawing() override
	{
		setRenderTargets();
		ctx->SetViewports(1, nullptr, 0, 0);
		// Like D3D11's ClearState(); the constant buffers keep their values, but dynamic
		// buffers have to be written again in every frame.
		state = State();
		currentTexture = nullptr;
		fogEnabled = false;
		writeTransformBuffer();
		writeFogBuffer();
	}

	void EndDrawing() override
	{
		swapChain->Present(g_renderSettings.vsync ? 1 : 0);
		frame++;
	}

	void ClearFrame(bool clearColors, bool clearDepth, uint32_t color) override
	{
		setRenderTargets();
		if (clearColors) {
			const float cc[4] = { ((color >> 16) & 255) / 255.0f, ((color >> 8) & 255) / 255.0f, (color & 255) / 255.0f, ((color >> 24) & 255) / 255.0f };
			ctx->ClearRenderTarget(currentColorTarget(), cc, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
		}
		if (clearDepth)
			ctx->ClearDepthStencil(currentDepthTarget(), CLEAR_DEPTH_FLAG, 1.0f, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	}

	bool SaveScreenshot(const char* path) override
	{
		ITexture* backBuffer = swapChain->GetCurrentBackBufferRTV()->GetTexture();
		TextureDesc desc = backBuffer->GetDesc();
		desc.Name = "Screenshot readback";
		desc.Usage = USAGE_STAGING;
		desc.BindFlags = BIND_NONE;
		desc.CPUAccessFlags = CPU_ACCESS_READ;
		desc.MipLevels = 1;
		RefCntAutoPtr<ITexture> staging;
		device->CreateTexture(desc, nullptr, &staging);
		if (!staging)
			return false;
		CopyTextureAttribs copy{ backBuffer, RESOURCE_STATE_TRANSITION_MODE_TRANSITION, staging, RESOURCE_STATE_TRANSITION_MODE_TRANSITION };
		ctx->CopyTexture(copy);
		ctx->WaitForIdle();
		MappedTextureSubresource mapped;
		ctx->MapTextureSubresource(staging, 0, 0, MAP_READ, MAP_FLAG_DO_NOT_WAIT, nullptr, mapped); // WaitForIdle above already synchronized
		if (!mapped.pData)
			return false;

		const uint32_t w = desc.Width, h = desc.Height, rowSize = (w * 3 + 3) & ~3u;
		std::vector<uint8_t> file(54 + (size_t)rowSize * h, 0);
		auto put32 = [&file](size_t at, uint32_t v) { memcpy(&file[at], &v, 4); };
		file[0] = 'B'; file[1] = 'M';
		put32(2, (uint32_t)file.size()); put32(10, 54); put32(14, 40);
		put32(18, w); put32(22, h);
		file[26] = 1; file[28] = 24;
		const bool bgra = desc.Format == TEX_FORMAT_BGRA8_UNORM || desc.Format == TEX_FORMAT_BGRA8_UNORM_SRGB;
		for (uint32_t y = 0; y < h; y++) {
			const uint8_t* src = (const uint8_t*)mapped.pData + (size_t)(h - 1 - y) * mapped.Stride; // BMP rows go bottom-up
			uint8_t* dst = &file[54 + (size_t)y * rowSize];
			for (uint32_t x = 0; x < w; x++) {
				dst[x * 3 + 0] = src[x * 4 + (bgra ? 0 : 2)];
				dst[x * 3 + 1] = src[x * 4 + 1];
				dst[x * 3 + 2] = src[x * 4 + (bgra ? 2 : 0)];
			}
		}
		ctx->UnmapTextureSubresource(staging, 0, 0);
		FILE* f = fopen(path, "wb");
		if (!f)
			return false;
		fwrite(file.data(), 1, file.size(), f);
		fclose(f);
		return true;
	}

	// Textures management

	texture CreateTexture(const Bitmap& bm, int mipmaps) override
	{
		if (bm.format == BMFORMAT_BC7)
			return createBC7Texture(bm, mipmaps);
		Bitmap cvtbmp = bm.convertToR8G8B8A8();

		// Same mipmap chain as the D3D11 renderer
		std::vector<Bitmap> levels;
		levels.push_back(cvtbmp);
		if (mipmaps <= 0) {
			int mmwidth = cvtbmp.width / 2, mmheight = cvtbmp.height / 2;
			while (mmwidth && mmheight) {
				levels.push_back(cvtbmp.resize(mmwidth, mmheight));
				mmwidth /= 2;
				mmheight /= 2;
				// no mipmaps of 8x8 or below (to prevent tiles from being merged in the terrain atlas!)
				if (mmwidth <= 8 || mmheight <= 8)
					break;
			}
		}

		TextureDesc desc;
		desc.Type = RESOURCE_DIM_TEX_2D;
		desc.Width = cvtbmp.width;
		desc.Height = cvtbmp.height;
		desc.MipLevels = (Uint32)levels.size();
		desc.Format = TEX_FORMAT_RGBA8_UNORM;
		desc.BindFlags = BIND_SHADER_RESOURCE;
		desc.Usage = USAGE_DEFAULT;
		std::vector<TextureSubResData> sub(levels.size());
		for (size_t i = 0; i < levels.size(); i++) {
			sub[i].pData = levels[i].pixels.data();
			sub[i].Stride = levels[i].width * 4u;
		}
		TextureData data{ sub.data(), (Uint32)sub.size() };

		DiligentTexture* t = new DiligentTexture;
		device->CreateTexture(desc, &data, &t->tex);
		if (!t->tex)
			ferr("Diligent: could not create a %ix%i texture.", cvtbmp.width, cvtbmp.height);
		signature->CreateShaderResourceBinding(&t->srb, true);
		t->srb->GetVariableByName(SHADER_TYPE_PIXEL, "inpTexture")->Set(t->tex->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
		return (texture)t;
	}

	// BC7 (DDS, KTX2) as it is, with the file's mip levels (one if mipmaps is 1)
	texture createBC7Texture(const Bitmap& bm, int mipmaps)
	{
		const size_t count = mipmaps == 1 ? 1 : bm.levels.size();
		std::vector<TextureSubResData> sub(count);
		for (size_t i = 0; i < count; i++) {
			sub[i].pData = bm.pixels.data() + bm.levels[i];
			sub[i].Stride = (Uint64)std::max(1, (bm.width >> i) / 4) * 16;
		}
		TextureDesc desc;
		desc.Type = RESOURCE_DIM_TEX_2D;
		desc.Width = bm.width;
		desc.Height = bm.height;
		desc.MipLevels = (Uint32)count;
		desc.Format = TEX_FORMAT_BC7_UNORM;
		desc.BindFlags = BIND_SHADER_RESOURCE;
		desc.Usage = USAGE_DEFAULT;
		TextureData data{ sub.data(), (Uint32)sub.size() };
		DiligentTexture* t = new DiligentTexture;
		device->CreateTexture(desc, &data, &t->tex);
		if (!t->tex)
			ferr("Diligent: could not create a %ix%i BC7 texture.", bm.width, bm.height);
		signature->CreateShaderResourceBinding(&t->srb, true);
		t->srb->GetVariableByName(SHADER_TYPE_PIXEL, "inpTexture")->Set(t->tex->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
		return (texture)t;
	}

	void FreeTexture(texture t) override
	{
		if (t && t != whiteTexture) {
			if (currentTexture == t)
				currentTexture = nullptr;
			delete (DiligentTexture*)t;
		}
	}

	void UpdateTexture(texture t, const Bitmap& bmp) override
	{
		DiligentTexture* dt = (DiligentTexture*)t;
		const TextureDesc& desc = dt->tex->GetDesc();
		if (bmp.format == BMFORMAT_BC7 || desc.Format == TEX_FORMAT_BC7_UNORM) {
			printf("Hot reload: BC7 textures are not reloaded (restart to see the change)\n");
			return;
		}
		Bitmap cvtbmp = bmp.convertToR8G8B8A8();
		if (cvtbmp.width != (int)desc.Width || cvtbmp.height != (int)desc.Height)
			return;
		TextureSubResData sub;
		sub.pData = cvtbmp.pixels.data();
		sub.Stride = cvtbmp.width * 4u;
		Box box{ 0, desc.Width, 0, desc.Height };
		ctx->UpdateTexture(dt->tex, 0, 0, box, sub, RESOURCE_STATE_TRANSITION_MODE_TRANSITION, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	}

	// State changes

	void SetTransformMatrix(const Matrix* m) override
	{
		transform = *m;
		writeTransformBuffer();
	}

	void SetTexture(uint32_t x, texture t) override
	{
		if (x == 0)
			currentTexture = t;
	}

	void NoTexture(uint32_t x) override
	{
		if (x == 0)
			currentTexture = nullptr;
	}

	void SetFog(uint32_t color, float farz) override
	{
		fogColor = color;
		fogFar = farz;
		writeFogBuffer();
		state.vs = VS_FOG;
		fogEnabled = true;
	}

	void DisableFog() override
	{
		state.vs = VS_PLAIN;
		fogEnabled = false;
	}

	void EnableAlphaTest() override { state.ps = PS_ALPHATEST; }
	void DisableAlphaTest() override { state.ps = PS_PLAIN; }
	void EnableColorBlend() override {}
	void DisableColorBlend() override {}
	void SetBlendColor(int c) override {}
	void EnableAlphaBlend() override {}
	void DisableAlphaBlend() override {}
	void EnableScissor() override { state.scissor = true; }
	void DisableScissor() override { state.scissor = false; }

	void SetScissorRect(int x, int y, int w, int h) override
	{
		Rect rect{ x, y, x + w, y + h };
		const auto& desc = swapChain->GetDesc();
		ctx->SetScissorRects(1, &rect, desc.Width, desc.Height);
		state.scissor = true;
	}

	void EnableDepth() override {}
	void DisableDepth() override {}

	// 2D Rectangles drawing

	void InitRectDrawing() override
	{
		Matrix m = Matrix::getZeroMatrix();
		m._11 = 2.0f / g_windowWidth;
		m._22 = -2.0f / g_windowHeight;
		m._41 = -1;
		m._42 = 1;
		m._44 = 1;
		SetTransformMatrix(&m);
		state.alphaBlend = true;
		state.depth = DEPTH_OFF;
		state.ps = PS_PLAIN;
		state.vs = VS_PLAIN;
	}

	void DrawRect(int x, int y, int w, int h, int c, float u, float v, float o, float p) override
	{
		// Two triangles (the D3D11 renderer uses a strip)
		auto f = [](int n) { return (float)n - 0.5f; };
		const batchVertex corners[4] = {
			{ f(x), f(y), 0.0f, c, u, v },
			{ f(x + w), f(y), 0.0f, c, u + o, v },
			{ f(x), f(y + h), 0.0f, c, u, v + p },
			{ f(x + w), f(y + h), 0.0f, c, u + o, v + p },
		};
		const batchVertex verts[6] = { corners[0], corners[1], corners[2], corners[2], corners[1], corners[3] };
		streamVertices(verts, 6);
		prepareDraw(TOPO_TRIANGLES);
		ctx->Draw(DrawAttribs{ 6, DRAW_FLAG_VERIFY_ALL });
	}

	void DrawGradientRect(int x, int y, int w, int h, int c0, int c1, int c2, int c3) override {}

	void DrawFrame(int x, int y, int w, int h, int c) override
	{
		// Four lines (the D3D11 renderer uses a line strip)
		auto f = [](int n) { return (float)n; };
		const batchVertex corners[4] = {
			{ f(x), f(y), 0.0f, c, 0.0f, 0.0f },
			{ f(x + w), f(y), 0.0f, c, 0.0f, 0.0f },
			{ f(x + w), f(y + h), 0.0f, c, 0.0f, 0.0f },
			{ f(x), f(y + h), 0.0f, c, 0.0f, 0.0f },
		};
		const batchVertex verts[8] = { corners[0], corners[1], corners[1], corners[2], corners[2], corners[3], corners[3], corners[0] };
		streamVertices(verts, 8);
		prepareDraw(TOPO_LINES);
		ctx->Draw(DrawAttribs{ 8, DRAW_FLAG_VERIFY_ALL });
	}

	// 3D Landscape/Heightmap drawing

	void BeginMapDrawing() override
	{
		state.ps = PS_MAP;
		state.alphaBlend = false;
	}

	void BeginLakeDrawing() override
	{
		state.ps = PS_LAKE;
		state.alphaBlend = true;
	}

	// 3D Mesh drawing

	void BeginMeshDrawing() override
	{
		state.alphaBlend = false;
		state.depth = DEPTH_ON;
		state.ps = PS_ALPHATEST;
	}

	// Batch drawing

	RBatch* CreateBatch(int mv, int mi) override
	{
		RBatchDiligent* batch = new RBatchDiligent;
		batch->gfx = this;
		batch->maxverts = mv; batch->maxindis = mi;
		batch->curverts = batch->curindis = 0;
		batch->verts.resize(mv);
		batch->indis.resize(mi);
		if ((uint64_t)mv * sizeof(batchVertex) > vertexRing.capacity || (uint64_t)mi * 2 > indexRing.capacity)
			ferr("Diligent: batch of %i vertices / %i indices is larger than the stream buffers.", mv, mi);
		return batch;
	}

	void BeginBatchDrawing() override { state.topology = TOPO_TRIANGLES; }
	int ConvertColor(int c) override { return c; }

	// Buffer drawing

	template <typename T> void initStream(DiligentStreamBuffer<T>& stream, int count, const char* name, BIND_FLAGS bind)
	{
		stream.ctx = ctx;
		stream.currentFrame = &frame;
		stream.data.resize(std::max(count, 1));
		stream.buf = createBuffer(name, stream.data.size() * sizeof(T), bind, USAGE_DYNAMIC);
	}

	RVertexBuffer* CreateVertexBuffer(int nv) override
	{
		RVertexBufferDiligent* vb = new RVertexBufferDiligent;
		vb->size = nv;
		initStream(vb->stream, nv, "Vertex buffer", BIND_VERTEX_BUFFER);
		return vb;
	}

	RIndexBuffer* CreateIndexBuffer(int ni) override
	{
		RIndexBufferDiligent* ib = new RIndexBufferDiligent;
		ib->size = ni;
		initStream(ib->stream, ni, "Index buffer", BIND_INDEX_BUFFER);
		return ib;
	}

	void SetVertexBuffer(RVertexBuffer* _rv) override
	{
		IBuffer* vb = ((RVertexBufferDiligent*)_rv)->stream.get();
		const Uint64 offset = 0;
		ctx->SetVertexBuffers(0, 1, &vb, &offset, RESOURCE_STATE_TRANSITION_MODE_TRANSITION, SET_VERTEX_BUFFERS_FLAG_RESET);
	}

	void SetIndexBuffer(RIndexBuffer* _ri) override
	{
		ctx->SetIndexBuffer(((RIndexBufferDiligent*)_ri)->stream.get(), 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	}

	void DrawBuffer(int first, int count) override
	{
		if (count <= 0)
			return;
		prepareDraw(state.topology);
		ctx->DrawIndexed(DrawIndexedAttribs{ (Uint32)count, VT_UINT16, DRAW_FLAG_VERIFY_ALL, 1, (Uint32)first });
	}

	// ImGui

	void InitImGuiDrawing() override
	{
		InitRectDrawing();
		state.topology = TOPO_TRIANGLES;
	}

	void BeginParticles() override
	{
		state.alphaBlend = true;
		state.depth = DEPTH_TESTONLY;
		state.ps = PS_PLAIN;
		state.vs = fogEnabled ? VS_FOG : VS_PLAIN;
	}

	void SetLineTopology() override { state.topology = TOPO_LINES; }
	void SetTriangleTopology() override { state.topology = TOPO_TRIANGLES; }
};

void RBatchDiligent::flush()
{
	if (!curverts) return;
	gfx->streamVertices(verts.data(), curverts);
	gfx->streamIndices(indis.data(), curindis);
	gfx->prepareDraw(gfx->state.topology);
	gfx->ctx->DrawIndexed(DrawIndexedAttribs{ curindis, VT_UINT16, DRAW_FLAG_VERIFY_ALL });
	curverts = curindis = 0;
}

IRenderer* CreateDiligentRenderer() { return new DiligentRenderer; }

