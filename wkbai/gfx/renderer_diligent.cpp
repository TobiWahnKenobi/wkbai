// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

// IRenderer on Diligent Engine (Direct3D 12 or Vulkan).
// It reproduces the former D3D11 renderer (renderer_d3d11.cpp): same shaders and the same state
// behaviour. IRenderer's state toggles select one of a few pipeline states, which are created
// on first use; every pipeline shares one resource signature, so each texture has its own
// shader resource binding that works with all of them.

#include "renderer_diligent.h"
#include "../util/LoadingProgress.h"
#include "DiligentPostProcess.h"
#include "DiligentFsr3.h"
#include "DiligentClouds.h"
#include "DiligentGI.h"
#include "DiligentRayShadows.h"

bool g_rayTracingBuffers = false;
#include "bitmap.h"
#include "../window.h"
#include "../settings.h"
#include "RenderSettings.h"
#include "../Environment.h"
#include "../util/util.h"
#include "../Camera.h"
#include "../util/FrameStats.h"
#include "GpuProfiler.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

#include "EngineFactoryD3D12.h"
#include <deque>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include "EngineFactoryVk.h"

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <dxgi1_4.h>
#pragma comment(lib, "dxgi.lib")

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
	float4 LightMasks;
	float4 FogSun;        // the haze's tint (DiligentSkyRenderer): towards the true sun (unit), its weight (0 off)
	float4 FogSunColour;  // the sun's disc through the air (rgb, the sky's tone), its glow's strength
	float4 FogHorizon[32]; // the sky's colour 2 degrees up, by the bearing (atan2(z, x), 32 steps from 0)
	float4 FogSky[50];     // the live sky for reflections (SkyReflect): 16 bearings at 4, 22, 50 degrees up, the zenith (w: 1 on), the ground below
};

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

// Fog of a world position: the distance fog, or the map-edge fog where that is stronger. The edge
// fog follows the signed distance to the playable area's border (negative inside; the larger of x
// and z, so that it follows the square band of the map's edge): it may start inside the playable
// area and is opaque before the map mesh ends.
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
	float3 Haze : COLOR1; // the fog's colour along the view (the haze's tint, FogTint: per vertex)
};

static const float ALPHA_REF = 0.94;

VS_OUTPUT VS(float3 Pos : ATTRIB0, float4 Color : ATTRIB1, float2 Texcoord : ATTRIB2)
{
	VS_OUTPUT output = (VS_OUTPUT)0;
	output.Pos = mul(float4(Pos, 1.0), Transform);
	output.Color = Color.bgra;
	output.Texcoord = Texcoord;
	output.Fog = 0.0;
	output.Haze = FogColor.rgb;
	return output;
}

VS_OUTPUT VS_Fog(float3 Pos : ATTRIB0, float4 Color : ATTRIB1, float2 Texcoord : ATTRIB2)
{
	VS_OUTPUT output = (VS_OUTPUT)0;
	output.Pos = mul(float4(Pos, 1.0), Transform);
	output.Color = Color.bgra;
	output.Texcoord = Texcoord;
	output.Fog = FogAmount(Pos, output.Pos.w);
	output.Haze = FogTint(Pos);
	return output;
}

float4 ApplyFog(float4 color, float fog, float3 haze)
{
	return float4(lerp(color.rgb, haze, fog), color.a);
}

float4 PS(VS_OUTPUT input) : SV_Target
{
	float4 tex = inpTexture.Sample(inpSampler, input.Texcoord);
	return ApplyFog(input.Color * tex, input.Fog, input.Haze);
}

float4 PS_AlphaTest(VS_OUTPUT input) : SV_Target
{
	float4 tex = inpTexture.Sample(inpSampler, input.Texcoord);
	if(tex.a < ALPHA_REF) discard;
	return ApplyFog(input.Color * tex, input.Fog, input.Haze);
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
	return ApplyFog(input.Color * tex, input.Fog, input.Haze);
}

float4 PS_Lake(VS_OUTPUT input) : SV_Target
{
	return ApplyFog(input.Color, input.Fog, input.Haze);
}

// Soft particles (SetSoftParticles): faded out over the last metres in front of the depth
// pre-pass's depth (the ground, the water, the models), instead of cut off in a hard line where
// they meet them
Texture2D<float> softDepth;
cbuffer SoftBuffer
{
	float4 Soft; // the projection's m22 and m32, 1 / the fade's distance, on
};
float4 PS_Soft(VS_OUTPUT input) : SV_Target
{
	float4 c = PS(input);
	if (Soft.w != 0) {
		float d = softDepth.Load(int3(input.Pos.xy, 0));
		float z = Soft.y / (d - Soft.x);
		c.a *= saturate((z - input.Pos.w) * Soft.z);
	}
	return c;
}
)---";

// The projected decals (DiligentGfx::DrawProjectedDecals): a box per decal (its far faces), each
// pixel's point from the depth pre-pass's surface depth (the ground and the water), the decal
// where it lies inside the ring or the square and nothing nearer stands in front (its lighting
// depth: the models too)
static const char decalShaderCode[] = R"---(
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
	float4 LightMasks; // zw: a pixel's size
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

cbuffer DecalBuffer
{
	float4 DProj;   // the projection's m11, m22, m33, m43
	float4 DJitter; // its m31, m32 (the jitter)
	matrix DInvView;
};
Texture2D<float> surfaceDepth;
Texture2D<float> sceneDepth;
Texture2D bandTexture;
SamplerState bandSampler;

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

struct DECAL_OUTPUT
{
	float4 Pos : SV_POSITION;
	nointerpolation float4 Shape : TEXCOORD0; // centre x, z, outer, inner
	nointerpolation float4 Range : TEXCOORD1; // y from, to, tiles, square
	nointerpolation float4 Colour : COLOR0;
};

static const uint BOX[36] = { 0,6,2,0,4,6, 1,3,7,1,7,5, 0,1,5,0,5,4, 2,7,3,2,6,7, 0,3,1,0,2,3, 4,5,7,4,7,6 };

DECAL_OUTPUT VS_Decal(float4 Shape : ATTRIB0, float4 Range : ATTRIB1, float4 Colour : ATTRIB2, uint id : SV_VertexID)
{
	uint c = BOX[id];
	float3 p = float3(Shape.x + ((c & 1) ? Shape.z : -Shape.z), ((c >> 1) & 1) ? Range.y : Range.x, Shape.y + ((c >> 2) & 1 ? Shape.z : -Shape.z));
	DECAL_OUTPUT o;
	o.Pos = mul(float4(p, 1), Transform);
	o.Shape = Shape;
	o.Range = Range;
	o.Colour = Colour;
	return o;
}

float ViewZ(float d)
{
	return DProj.w / (d - DProj.z);
}

float4 PS_Decal(DECAL_OUTPUT input) : SV_Target
{
	int3 px = int3(input.Pos.xy, 0);
	float ds = surfaceDepth.Load(px);
	if (ds >= 0.99999)
		discard;
	float z = ViewZ(ds);
	// hidden by what stands in front of the surface (a building, a unit, a tree; not the grass)
	if (ViewZ(sceneDepth.Load(px)) < z - 0.4 - z * 0.002)
		discard;
	float2 ndc = float2(input.Pos.x * LightMasks.z * 2 - 1, 1 - input.Pos.y * LightMasks.w * 2);
	float3 v = float3((ndc.x - DJitter.x) * z / DProj.x, (ndc.y - DJitter.y) * z / DProj.y, z);
	float3 w = mul(float4(v, 1), DInvView).xyz;
	if (w.y < input.Range.x || w.y > input.Range.y)
		discard;
	float2 d = w.xz - input.Shape.xy;
	float4 c = input.Colour;
	if (input.Range.w < 0.5) {
		float r = length(d);
		if (r > input.Shape.z || r < input.Shape.w)
			discard;
		float across = (input.Shape.z - r) / max(input.Shape.z - input.Shape.w, 1e-3);
		float along = frac(atan2(d.y, d.x) / 6.2831853 * input.Range.z);
		c *= bandTexture.SampleLevel(bandSampler, float2(along, across), 0);
	}
	else if (any(abs(d) > input.Shape.z))
		discard;
	c.rgb = lerp(c.rgb, FogTint(w), FogAmount(w, distance(w, FogCamera.xyz)));
	return c;
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
	RefCntAutoPtr<ITextureView> view; // a swizzled view (BC4), else the default one
	RefCntAutoPtr<IShaderResourceBinding> srb;
	// the GPU memory budget (CreateTextureLazy): its pixels kept, the GPU texture made when drawn
	bool lazy = false;
	Bitmap cpu;
	int mipmaps = 0;
	uint64_t lastUsed = 0, bytes = 0;
	uint32_t generation = 0;
	// an uncompressed one's RGBA levels (made on a worker thread: TexturePrep; prep 0 queued,
	// 1 being made, 2 ready), whether a draw has asked for it (TouchTexture)
	std::vector<Bitmap> levels;
	std::atomic<int> prep{ 2 };
	bool requested = false;
};
// An uncompressed texture's levels as the D3D11 renderer makes them: RGBA, halved down to above
// 8 x 8 (no mipmaps of 8x8 or below: tiles merged in the terrain atlas), one level if mipmaps is 1
static std::vector<Bitmap> MakeMipChain(const Bitmap& bm, int mipmaps)
{
	std::vector<Bitmap> levels;
	levels.push_back(bm.format == BMFORMAT_R8G8B8A8 ? bm : bm.convertToR8G8B8A8());
	if (mipmaps <= 0) {
		const Bitmap& top = levels[0];
		int mmwidth = top.width / 2, mmheight = top.height / 2;
		while (mmwidth && mmheight) {
			levels.push_back(levels[0].resize(mmwidth, mmheight));
			mmwidth /= 2;
			mmheight /= 2;
			if (mmwidth <= 8 || mmheight <= 8)
				break;
		}
	}
	return levels;
}

// The lazy textures' preparation (their RGBA levels from the file's picture) on worker threads,
// so that the draw that needs one only uploads it
struct TexturePrep {
	std::mutex m;
	std::condition_variable cv;
	std::deque<DiligentTexture*> queue;
	bool started = false;

	static void prepare(DiligentTexture* t)
	{
		t->levels = MakeMipChain(t->cpu, t->mipmaps);
		t->cpu = Bitmap(); // (the levels hold it)
	}
	void push(DiligentTexture* t)
	{
		std::lock_guard<std::mutex> lock(m);
		if (!started) {
			started = true;
			const unsigned n = std::clamp(std::thread::hardware_concurrency(), 3u, 8u) - 2;
			for (unsigned i = 0; i < n; i++)
				std::thread([this]() { loop(); }).detach();
		}
		t->prep = 0;
		queue.push_back(t);
		cv.notify_one();
	}
	void loop()
	{
		for (;;) {
			DiligentTexture* t;
			{
				std::unique_lock<std::mutex> lock(m);
				cv.wait(lock, [&]() { return !queue.empty(); });
				t = queue.front();
				queue.pop_front();
				t->prep = 1;
			}
			prepare(t);
			t->prep = 2;
		}
	}
	// prepared now on this thread (taken from the queue), or waited for (a worker has it)
	void finishNow(DiligentTexture* t)
	{
		bool mine = false;
		{
			std::lock_guard<std::mutex> lock(m);
			auto it = std::find(queue.begin(), queue.end(), t);
			if (it != queue.end()) {
				queue.erase(it);
				t->prep = 1;
				mine = true;
			}
		}
		if (mine) {
			prepare(t);
			t->prep = 2;
			return;
		}
		while (t->prep != 2)
			std::this_thread::yield();
	}
	// before it is deleted: out of the queue, or its preparation finished
	void forget(DiligentTexture* t)
	{
		{
			std::lock_guard<std::mutex> lock(m);
			auto it = std::find(queue.begin(), queue.end(), t);
			if (it != queue.end()) {
				queue.erase(it);
				return;
			}
		}
		while (t->prep == 1)
			std::this_thread::yield();
	}
};
static TexturePrep g_texturePrep;

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
	enum PSKind : uint8_t { PS_PLAIN, PS_ALPHATEST, PS_MAP, PS_LAKE, PS_SOFT };
	enum DepthKind : uint8_t { DEPTH_DEFAULT, DEPTH_ON, DEPTH_OFF, DEPTH_TESTONLY };
	enum TopologyKind : uint8_t { TOPO_TRIANGLES, TOPO_LINES };
	struct State {
		VSKind vs = VS_PLAIN;
		PSKind ps = PS_PLAIN;
		bool alphaBlend = false;
		DepthKind depth = DEPTH_DEFAULT;
		bool scissor = false;
		TopologyKind topology = TOPO_TRIANGLES;
		uint32_t key() const { return vs | (ps << 1) | (alphaBlend << 4) | (depth << 5) | (scissor << 7) | (topology << 8); }
	};

	RefCntAutoPtr<IPipelineResourceSignature> signature;
	RefCntAutoPtr<IShader> vertexShaders[2], pixelShaders[5];
	// the soft particles' (PS_SOFT: the depth pre-pass's depth, SoftBuffer) and the projected
	// decals' resources (DrawProjectedDecals; made at the first use); the camera of the frame's
	// pre-pass (FinishLightingPrepass), for both
	RefCntAutoPtr<IPipelineResourceSignature> softSignature, decalSignature;
	RefCntAutoPtr<IShaderResourceBinding> softSrb, decalSrb;
	RefCntAutoPtr<IBuffer> softBuffer, decalBuffer;
	RefCntAutoPtr<IPipelineState> decalPipeline;
	bool softParticles = false, decalsFailed = false, prepassOrtho = false;
	float prepassProj[6] = {};
	Matrix prepassInvView;
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
	bool postActive = false, postEffects = true;
	// FSR 3.1 in this frame, its jitter in render pixels, and when the last frame was processed
	bool fsr3Active = false;
	float jitterPixels[2] = {};
	std::chrono::steady_clock::time_point lastApply;
	DiligentPostProcess::FogOfWar fog;
	texture whiteTexture = nullptr;
	texture currentTexture = nullptr;
	// the per-pass timings' GPU timestamps (GpuProfiler): a list of queries per frame slot
	struct Stamps : GpuTimestampSource {
		IRenderDevice* device = nullptr;
		IDeviceContext* ctx = nullptr;
		uint32_t slot = 0;
		std::vector<RefCntAutoPtr<IQuery>> queries[PROFILER_FRAMES];
		void stamp(uint32_t index) override {
			auto& q = queries[slot];
			while (q.size() <= index) {
				QueryDesc qd;
				qd.Type = QUERY_TYPE_TIMESTAMP;
				qd.Name = "Pass timestamp";
				q.emplace_back();
				device->CreateQuery(qd, &q.back());
			}
			if (q[index])
				ctx->EndQuery(q[index]);
		}
		bool read(uint32_t frameSlot, uint32_t count, std::vector<double>& ms) override {
			auto& q = queries[frameSlot];
			if (count > q.size())
				return false;
			ms.assign(count, 0.0);
			uint64_t first = 0, freq = 0;
			for (uint32_t i = 0; i < count; i++) {
				QueryDataTimestamp d;
				if (!q[i] || !q[i]->GetData(&d, sizeof(d), false) || !d.Frequency)
					return false;
				if (i == 0) { first = d.Counter; freq = d.Frequency; }
				ms[i] = (double)((int64_t)(d.Counter - first)) * 1000.0 / (double)freq;
			}
			return true;
		}
	} stamps;
	std::chrono::steady_clock::time_point lastBegin;
	// the GPU memory budget (manageResidency): the lazy textures, those not uploaded yet, the
	// adapter's budget (DXGI, Direct3D 12 only)
	std::vector<DiligentTexture*> lazyTextures;
	std::deque<DiligentTexture*> uploadQueue;
	std::vector<DiligentTexture*> requests; // asked for by a draw (TouchTexture), not uploaded yet
	uint64_t frameUploadBytes = 0;
	uint64_t residentLazyBytes = 0;
	IDXGIAdapter3* dxgiAdapter = nullptr;
	struct {
		bool queried = false;
		uint64_t budget = 0, usage = 0, evictedSinceQuery = 0;
		uint64_t uploads = 0, evictions = 0, evictedBytes = 0;
		uint64_t unreachableSaid = 0;
		bool finalSaid = false;
	} vram;
	State state;
	Matrix transform = Matrix::getIdentity();
	uint32_t fogColor = 0;
	float fogFar = 250.0f;
	Vector3 fogCamera;
	float fogEdge[4] = { 1e9f, 1e9f, 1e9f, 2e9f }; // no edge fog until a map sets it
	float farGround[12] = {}; // FogBuffer.FarGround, FarLand, FarArea (SetFarGround)
	float lightMasks[4] = {}; // FogBuffer.LightMasks (FinishLightingPrepass)
	bool fogEnabled = false;
	bool isVulkan = false;
	uint64_t frame = 0; // presented frames, for buffers whose dynamic memory expires

	// DXC for shader model 6 (the ray-traced shadows' ray queries): dxcompiler.dll beside the
	// game, else the newest Windows SDK's
	static std::string FindDxCompiler()
	{
		char exe[MAX_PATH] = {};
		GetModuleFileNameA(nullptr, exe, MAX_PATH);
		std::string dir = exe;
		dir = dir.substr(0, dir.find_last_of("\\/") + 1);
		if (GetFileAttributesA((dir + "dxcompiler.dll").c_str()) != INVALID_FILE_ATTRIBUTES)
			return dir + "dxcompiler.dll";
		const char* sdks[] = { "10.0.26100.0", "10.0.22621.0", "10.0.22000.0", "10.0.20348.0", "10.0.19041.0" };
		for (const char* v : sdks) {
			const std::string p = std::string("C:\\Program Files (x86)\\Windows Kits\\10\\bin\\") + v + "\\x64\\dxcompiler.dll";
			if (GetFileAttributesA(p.c_str()) != INVALID_FILE_ATTRIBUTES)
				return p;
		}
		return std::string();
	}
	// the GPU's frame time (g_gpuFrameMs): a timestamp at the start and the end of each frame,
	// read back GPU_QUERY_FRAMES later, without waiting
	static constexpr int GPU_QUERY_FRAMES = 4;
	RefCntAutoPtr<IQuery> gpuStart[GPU_QUERY_FRAMES], gpuEnd[GPU_QUERY_FRAMES];
	bool gpuQueryIssued[GPU_QUERY_FRAMES] = {};

	RefCntAutoPtr<IShader> createShader(SHADER_TYPE type, const char* entryPoint, const char* source = diligentShaderCode, size_t length = sizeof(diligentShaderCode) - 1)
	{
		ShaderCreateInfo sci;
		sci.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
		sci.Source = source;
		sci.SourceLength = length;
		sci.EntryPoint = entryPoint;
		sci.Desc.ShaderType = type;
		sci.Desc.Name = entryPoint;
		RefCntAutoPtr<IShader> shader;
		LoadingCompilingShader(entryPoint); // (the loading screen)
		device->CreateShader(sci, &shader);
		if (!shader)
			ferr("Diligent: failed to compile shader %s", entryPoint);
		return shader;
	}

	RefCntAutoPtr<IBuffer> createBuffer(const char* name, uint64_t size, Diligent::BIND_FLAGS bind, USAGE usage)
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
		IPipelineResourceSignature* signatures[] = { signature, softSignature };
		ci.ppResourceSignatures = signatures;
		ci.ResourceSignaturesCount = st.ps == PS_SOFT ? 2 : 1;

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
		ensureResident(t);
		ctx->CommitShaderResources(t->srb, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
		if (st.ps == PS_SOFT)
			ctx->CommitShaderResources(softSrb, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	}

	ITextureView* currentColorTarget() { return postActive ? post->colorRTV() : swapChain->GetCurrentBackBufferRTV(); }
	ITextureView* currentDepthTarget() { return postActive ? post->depthDSV() : swapChain->GetDepthBufferDSV(); }

	void setRenderTargets()
	{
		ITextureView* rtv = currentColorTarget();
		ctx->SetRenderTargets(1, &rtv, currentDepthTarget(), RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	}

	void BeginPostProcessing(bool on, bool effects) override
	{
		postActive = false;
		postEffects = effects;
		fsr3Active = false;
		g_projectionJitter[0] = g_projectionJitter[1] = 0.0f;
		jitterPixels[0] = jitterPixels[1] = 0.0f;
		if (!on)
			return;
		if (!post)
			post = std::make_unique<DiligentPostProcess>(device, swapChain->GetDesc().ColorBufferFormat, swapChain->GetDesc().DepthBufferFormat);
		// FSR: the scene smaller, scaled up to the back buffer in ApplyPostProcessing
		const uint32_t ow = swapChain->GetDesc().Width, oh = swapChain->GetDesc().Height;
		const float scale = effects ? g_renderSettings.renderScale() : 1.0f;
		const uint32_t w = std::max(1u, (uint32_t)std::lround(ow * scale)), h = std::max(1u, (uint32_t)std::lround(oh * scale));
		post->resize(w, h, ow, oh);
		fsr3Active = effects && g_renderSettings.fsr3() && post->fsr3Ready();
		post->beginFrame(ctx, fsr3Active);
		// FSR 3.1: its Halton sequence, longer the more it upscales; TAA: the camera moved by a
		// sub-pixel Halton (2, 3) offset, 8 steps
		if (fsr3Active) {
			const int phases = DiligentFsr3::jitterPhaseCount(w, ow);
			DiligentFsr3::jitterOffset((int)(frame % (uint64_t)phases), phases, jitterPixels[0], jitterPixels[1]);
			g_projectionJitter[0] = 2.0f * jitterPixels[0] / w;
			g_projectionJitter[1] = -2.0f * jitterPixels[1] / h;
		}
		else if (effects && g_renderSettings.antiAliasing == 2) {
			auto halton = [](int i, int base) {
				float f = 1.0f, r = 0.0f;
				for (; i > 0; i /= base) {
					f /= base;
					r += f * (i % base);
				}
				return r;
			};
			const int i = (int)(frame % 8) + 1;
			g_projectionJitter[0] = (halton(i, 2) - 0.5f) * 2.0f / w;
			g_projectionJitter[1] = -(halton(i, 3) - 0.5f) * 2.0f / h;
		}
		postActive = true;
	}

	void PreparePostProcessing() override
	{
		if (!post)
			post = std::make_unique<DiligentPostProcess>(device, swapChain->GetDesc().ColorBufferFormat, swapChain->GetDesc().DepthBufferFormat);
	}

	bool LightingPrepassWanted() override
	{
		// ("depthPrepass": false in the settings file, for comparisons: no ray-traced shadows
		// then, the ambient occlusion over the whole picture as before)
		// (with the post-processing effects always: the projected decals, the soft particles and
		// the contact shadows take its depth too)
		static const bool allowed = g_settings.value<bool>("depthPrepass", true);
		return allowed && postActive && postEffects && post && !shadows.portraitPass;
	}

	void SnapshotSurfaceDepth() override
	{
		if (post)
			post->snapshotSurfaceDepth(ctx);
	}

	bool ProjectedDecalsAvailable() override
	{
		return shadows.lightPrepass && post && !decalsFailed && !prepassOrtho;
	}

	void makeDecalResources()
	{
		RefCntAutoPtr<IShader> vs = createShader(SHADER_TYPE_VERTEX, "VS_Decal", decalShaderCode, sizeof(decalShaderCode) - 1);
		RefCntAutoPtr<IShader> ps = createShader(SHADER_TYPE_PIXEL, "PS_Decal", decalShaderCode, sizeof(decalShaderCode) - 1);
		const PipelineResourceDesc resources[] = {
			{ SHADER_TYPE_VERTEX, "TransformBuffer", 1, SHADER_RESOURCE_TYPE_CONSTANT_BUFFER, SHADER_RESOURCE_VARIABLE_TYPE_STATIC },
			{ SHADER_TYPE_PIXEL, "FogBuffer", 1, SHADER_RESOURCE_TYPE_CONSTANT_BUFFER, SHADER_RESOURCE_VARIABLE_TYPE_STATIC },
			{ SHADER_TYPE_PIXEL, "DecalBuffer", 1, SHADER_RESOURCE_TYPE_CONSTANT_BUFFER, SHADER_RESOURCE_VARIABLE_TYPE_STATIC },
			{ SHADER_TYPE_PIXEL, "surfaceDepth", 1, SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC },
			{ SHADER_TYPE_PIXEL, "sceneDepth", 1, SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC },
			{ SHADER_TYPE_PIXEL, "bandTexture", 1, SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC },
		};
		SamplerDesc sampler; // as the batches': trilinear, clamped
		sampler.AddressU = sampler.AddressV = sampler.AddressW = TEXTURE_ADDRESS_CLAMP;
		const ImmutableSamplerDesc samplers[] = { { SHADER_TYPE_PIXEL, "bandSampler", sampler } };
		PipelineResourceSignatureDesc sig;
		sig.Name = "Projected decals";
		sig.Resources = resources;
		sig.NumResources = (Uint32)std::size(resources);
		sig.ImmutableSamplers = samplers;
		sig.NumImmutableSamplers = (Uint32)std::size(samplers);
		device->CreatePipelineResourceSignature(sig, &decalSignature);
		if (!decalSignature) {
			decalsFailed = true;
			return;
		}
		decalBuffer = createBuffer("Projected decals", 96, BIND_UNIFORM_BUFFER, USAGE_DYNAMIC);
		decalSignature->GetStaticVariableByName(SHADER_TYPE_VERTEX, "TransformBuffer")->Set(transformBuffer);
		decalSignature->GetStaticVariableByName(SHADER_TYPE_PIXEL, "FogBuffer")->Set(fogBuffer);
		decalSignature->GetStaticVariableByName(SHADER_TYPE_PIXEL, "DecalBuffer")->Set(decalBuffer);
		decalSignature->CreateShaderResourceBinding(&decalSrb, true);
		GraphicsPipelineStateCreateInfo ci;
		ci.PSODesc.Name = "Projected decals";
		ci.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;
		auto& gp = ci.GraphicsPipeline;
		gp.NumRenderTargets = 1;
		gp.RTVFormats[0] = swapChain->GetDesc().ColorBufferFormat;
		gp.DSVFormat = swapChain->GetDesc().DepthBufferFormat;
		gp.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		// the box's far faces (its triangles wound outwards): each pixel once, the camera inside too
		gp.RasterizerDesc.CullMode = CULL_MODE_FRONT;
		gp.DepthStencilDesc.DepthEnable = False;
		gp.DepthStencilDesc.DepthWriteEnable = False;
		auto& rt = gp.BlendDesc.RenderTargets[0];
		rt.BlendEnable = True;
		rt.SrcBlend = BLEND_FACTOR_SRC_ALPHA;
		rt.DestBlend = BLEND_FACTOR_INV_SRC_ALPHA;
		rt.SrcBlendAlpha = BLEND_FACTOR_SRC_ALPHA;
		rt.DestBlendAlpha = BLEND_FACTOR_INV_SRC_ALPHA;
		static const LayoutElement layout[] = {
			LayoutElement{ 0, 0, 4, VT_FLOAT32, False, 0, 48, INPUT_ELEMENT_FREQUENCY_PER_INSTANCE, 1 },
			LayoutElement{ 1, 0, 4, VT_FLOAT32, False, 16, 48, INPUT_ELEMENT_FREQUENCY_PER_INSTANCE, 1 },
			LayoutElement{ 2, 0, 4, VT_FLOAT32, False, 32, 48, INPUT_ELEMENT_FREQUENCY_PER_INSTANCE, 1 },
		};
		gp.InputLayout.LayoutElements = layout;
		gp.InputLayout.NumElements = (Uint32)std::size(layout);
		ci.pVS = vs;
		ci.pPS = ps;
		IPipelineResourceSignature* signatures[] = { decalSignature };
		ci.ppResourceSignatures = signatures;
		ci.ResourceSignaturesCount = 1;
		device->CreateGraphicsPipelineState(ci, &decalPipeline);
		if (!decalPipeline)
			decalsFailed = true;
	}

	void DrawProjectedDecals(texture tex, const ProjectedDecal* decals, size_t count) override
	{
		if (!count || !ProjectedDecalsAvailable())
			return;
		if (!decalPipeline)
			makeDecalResources();
		if (decalsFailed)
			return;
		void* mapped = nullptr;
		ctx->MapBuffer(decalBuffer, MAP_WRITE, MAP_FLAG_DISCARD, mapped);
		float* f = (float*)mapped;
		std::copy_n(prepassProj, 6, f);
		f[6] = f[7] = 0.0f;
		const Matrix t = prepassInvView.getTranspose();
		memcpy(f + 8, &t, sizeof(Matrix));
		ctx->UnmapBuffer(decalBuffer, MAP_WRITE);
		std::vector<float> data;
		data.reserve(count * 12);
		for (size_t i = 0; i < count; i++) {
			const ProjectedDecal& d = decals[i];
			const uint32_t c = d.colour;
			const float v[12] = { d.centre[0], d.centre[1], d.outer, d.inner, d.yMin, d.yMax, d.tiles, d.square ? 1.0f : 0.0f,
				((c >> 16) & 255) / 255.0f, ((c >> 8) & 255) / 255.0f, (c & 255) / 255.0f, ((c >> 24) & 255) / 255.0f };
			data.insert(data.end(), v, v + 12);
		}
		uint64_t offset = 0;
		IBuffer* vb = StreamVertexData(data.data(), data.size() * sizeof(float), offset);
		decalSrb->GetVariableByName(SHADER_TYPE_PIXEL, "surfaceDepth")->Set(post->surfaceDepth()->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
		decalSrb->GetVariableByName(SHADER_TYPE_PIXEL, "sceneDepth")->Set(post->lightingDepth()->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
		decalSrb->GetVariableByName(SHADER_TYPE_PIXEL, "bandTexture")->Set(GetTextureView(tex));
		ctx->SetPipelineState(decalPipeline);
		ctx->CommitShaderResources(decalSrb, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
		ctx->SetVertexBuffers(0, 1, &vb, &offset, RESOURCE_STATE_TRANSITION_MODE_TRANSITION, SET_VERTEX_BUFFERS_FLAG_RESET);
		ctx->Draw(DrawAttribs{ 36, DRAW_FLAG_VERIFY_ALL, (Uint32)count });
	}

	void SetSoftParticles(bool on) override
	{
		softParticles = on && shadows.lightPrepass && post;
		if (softParticles)
			softSrb->GetVariableByName(SHADER_TYPE_PIXEL, "softDepth")->Set(post->lightingDepth()->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
	}

	ITextureView* BeginLightingPrepass() override
	{
		return post ? post->beginLightingDepth(ctx) : nullptr;
	}

	void FinishLightingPrepass(const Camera& camera) override
	{
		shadows.lightRtMask = shadows.lightAo = nullptr;
		if (shadows.rayShadowsOn && shadows.rayShadows && shadows.rayShadows->valid()) {
			const float proj[4] = { camera.projMatrix._11, camera.projMatrix._22, camera.projMatrix._33, camera.projMatrix._43 };
			PROFILE_SCOPE("RT shadow mask");
			shadows.lightRtMask = shadows.rayShadows->renderMask(ctx, post->lightingDepth(), proj, camera.viewMatrix.getInverse4x3(), shadows.sunDirection, frame, fogBuffer,
				shadows.rtTerrain ? shadows.terrainHeights : nullptr, shadows.terrainGrid[2]);
		}
		if (g_renderSettings.ssao) {
			PROFILE_SCOPE("Ambient occlusion");
			shadows.lightAo = post->lightingAo(ctx, camera.projMatrix, g_renderSettings.ssaoRadius);
		}
		// the contact shadows (RenderSettings::contactShadows): a short march towards the sun in the
		// lighting depth, the small shadows the ray-traced ones miss or the shadow map can't
		// resolve; into the sun's mask with the ray-traced shadows
		// (the pass also filters the ray-traced mask: with them on, it runs without contact shadows too)
		if ((g_renderSettings.contactShadows || shadows.lightRtMask) && !camera.orthoMode) {
			PROFILE_SCOPE("Contact shadows");
			if (ITextureView* m = post->contactShadows(ctx, camera.projMatrix, camera.viewMatrix, shadows.sunDirection, shadows.lightRtMask,
				g_renderSettings.contactShadows ? 0.75f : 0.0f))
				shadows.lightRtMask = m;
		}
		shadows.lightPrepass = true;
		const Matrix& pm = camera.projMatrix;
		const float proj[6] = { pm._11, pm._22, pm._33, pm._43, pm._31, pm._32 };
		std::copy_n(proj, 6, prepassProj);
		prepassInvView = camera.viewMatrix.getInverse4x3();
		prepassOrtho = camera.orthoMode;
		{
			void* m = nullptr;
			ctx->MapBuffer(softBuffer, MAP_WRITE, MAP_FLAG_DISCARD, m);
			const float soft[4] = { pm._33, pm._43, 1.0f / 1.5f, camera.orthoMode ? 0.0f : 1.0f };
			memcpy(m, soft, sizeof(soft));
			ctx->UnmapBuffer(softBuffer, MAP_WRITE);
		}
		const TextureDesc& dd = post->lightingDepth()->GetDesc();
		lightMasks[0] = shadows.lightRtMask ? 1.0f : 0.0f;
		lightMasks[1] = shadows.lightAo ? std::clamp(g_renderSettings.ssaoStrength, 0.0f, 1.0f) : 0.0f;
		lightMasks[2] = 1.0f / dd.Width;
		lightMasks[3] = 1.0f / dd.Height;
		writeFogBuffer();
		setRenderTargets();
		ctx->SetViewports(1, nullptr, 0, 0);
	}

	void SetFogOfWar(const uint8_t* tiles, uint32_t width, uint32_t height, uint32_t version, const Matrix& inverseView, float unseenBrightness, uint32_t pad) override
	{
		fog = { tiles, width, height, version, inverseView, unseenBrightness, pad };
	}

	void ApplyPostProcessing(const Camera& camera) override
	{
		const Matrix& projection = camera.projMatrix;
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
		s.antiAliasing = g_renderSettings.antiAliasing;
		s.sharpness = g_renderSettings.sharpness;
		s.motionBlur = g_renderSettings.motionBlur;
		s.tiltShift = g_renderSettings.tiltShift;
		s.view = camera.viewMatrix;
		s.projection = camera.projMatrix;
		s.jitter[0] = camera.orthoMode ? 0.0f : g_projectionJitter[0];
		s.jitter[1] = camera.orthoMode ? 0.0f : g_projectionJitter[1];
		s.ortho = camera.orthoMode;
		const auto now = std::chrono::steady_clock::now();
		s.fsr3 = fsr3Active;
		s.jitterPixels[0] = camera.orthoMode ? 0.0f : jitterPixels[0];
		s.jitterPixels[1] = camera.orthoMode ? 0.0f : jitterPixels[1];
		s.cameraNear = camera.nearDist;
		s.cameraFar = camera.farDist;
		s.fovY = camera.fovY;
		s.frameTimeMs = std::chrono::duration<float, std::milli>(now - lastApply).count();
		lastApply = now;
		if (!postEffects) {
			s.ssao = s.bloom = false;
			s.motionBlur = 0.0f;
			s.tiltShift = 0.0f;
			s.antiAliasing = 0;
			s.fsr3 = false;
		}
		s.fog = fog;
		fog.tiles = nullptr;
		s.fogEdgeStart = fogEdge[2];
		s.fogEdgeOpaque = fogEdge[3];
		// the light shafts (RenderSettings::dynamicClouds 2 and up), with the enhanced renderers;
		// the pass also runs with the shadows alone (1), for the clouds' shade on the ray-traced
		// shadows (its alpha; the shader skips the shafts below 2)
		s.lightShafts = postEffects && shadows.clouds >= 1 && shadows.cloudShadows && shadows.cloudShadowsDrawn;
		s.cloudBuffer = shadows.cloudBuffer;
		s.cloudNoise = shadows.cloudNoise ? shadows.cloudNoise->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE) : nullptr;
		shadows.cloudShadowsDrawn = false;
		// the ray-traced shadows and the ambient occlusion: in the scene's shading, from the
		// depth pre-pass (FinishLightingPrepass); the masks go out of use with the scene
		s.aoInShading = shadows.lightPrepass && shadows.lightAo;
		shadows.lightPrepass = false;
		shadows.lightRtMask = shadows.lightAo = nullptr;
		if (lightMasks[0] != 0.0f || lightMasks[1] != 0.0f) {
			std::fill(lightMasks, lightMasks + 4, 0.0f);
			writeFogBuffer();
		}
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
		DiligentTexture* dt = (DiligentTexture*)(t ? t : whiteTexture);
		ensureResident(dt);
		return dt->view ? dt->view.RawPtr() : dt->tex->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
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

	void SetFarGround(float level, float reach, bool sea, const float colour[3], float areaWidth, float areaDepth, float nearEnd, float farClip) override
	{
		const float g[12] = { level, reach, sea ? 1.0f : 0.0f, reach > 0 ? 1.0f : 0.0f, colour[0], colour[1], colour[2], FAR_AIR, areaWidth, areaDepth, nearEnd, farClip };
		if (std::equal(g, g + 12, farGround))
			return;
		std::copy_n(g, 12, farGround);
		writeFogBuffer();
	}
	// the air's length over the ground beyond (world units): how far it takes to go two thirds of
	// the way into the horizon's colour (real air, on the ground beyond the extension only)
	static constexpr float FAR_AIR = 9000.0f; // (25000 left a dark band under the horizon without the edge fog)

	void SetFogEdge(float playableWidth, float playableDepth, float start, float opaque) override
	{
		fogEdge[0] = playableWidth;
		fogEdge[1] = playableDepth;
		fogEdge[2] = start;
		fogEdge[3] = opaque;
		writeFogBuffer();
	}

	ITextureView* CurrentColorTarget() override { return currentColorTarget(); }
	ITextureView* CurrentDepthTarget() override { return currentDepthTarget(); }

	ITextureView* MotionVectorTarget() override
	{
		return postActive && fsr3Active ? post->objectMotionRTV() : nullptr;
	}

	void MarkOpaqueSceneDone() override
	{
		if (!postActive || !fsr3Active)
			return;
		post->copyOpaque(ctx);
		RestoreRenderTargets();
	}

	void MarkWaterDone() override
	{
		if (!postActive || !fsr3Active)
			return;
		post->copyAfterWater(ctx);
		RestoreRenderTargets();
	}

	uint64_t FrameIndex() override { return frame; }

	void RestoreRenderTargets() override
	{
		setRenderTargets();
		ctx->SetViewports(1, nullptr, 0, 0);
	}

	void SetViewportPixels(int x, int y, int width, int height) override
	{
		Viewport vp;
		vp.TopLeftX = (float)x;
		vp.TopLeftY = (float)y;
		vp.Width = (float)width;
		vp.Height = (float)height;
		vp.MinDepth = 0.0f;
		vp.MaxDepth = 1.0f;
		const auto& desc = swapChain->GetDesc();
		ctx->SetViewports(1, &vp, desc.Width, desc.Height);
	}

	void ClearDepthOnly() override
	{
		if (ITextureView* dsv = currentDepthTarget())
			ctx->ClearDepthStencil(dsv, CLEAR_DEPTH_FLAG, 1.0f, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
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
		if (shadows.hazeOn) // the physical sky's haze, following the weather (the last frame's)
			for (int i = 0; i < 3; i++)
				cc[i] = shadows.hazeColour[i];
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
		// wkbai's weather: fog nearer (Environment.h; 1 without)
		if (!g_renderSettings.fogOff && g_environment.fogDistance < 1.0f) {
			cc[4] *= g_environment.fogDistance * g_environment.fogDistance * g_environment.fogDistance;
			cc[5] *= g_environment.fogDistance;
		}
		cc[6] = cc[7] = 0.0f;
		cc[8] = fogCamera.x;
		cc[9] = fogCamera.y;
		cc[10] = fogCamera.z;
		cc[11] = 1.0f;
		// (wkbai's day and night: darker and paler at night, Environment.h; 1 without)
		cc[12] = g_renderSettings.exposure * g_environment.exposure;
		cc[13] = g_renderSettings.brightness;
		cc[14] = g_renderSettings.contrast;
		cc[15] = g_renderSettings.saturation * g_environment.saturation;
		cc[16] = fogEdge[0];
		cc[17] = fogEdge[1];
		cc[18] = fogEdge[2];
		cc[19] = fogEdge[3];
		// the depth pre-pass's masks: the ray-traced shadows' on, the ambient occlusion's
		// strength, a pixel's size (the frame's)
		for (int i = 0; i < 4; i++)
			cc[20 + i] = lightMasks[i];
		// the haze's tint along the horizon (the physical sky's), only with its haze
		const bool tint = shadows.hazeOn && shadows.fogTint[3] != 0.0f;
		for (int i = 0; i < 4; i++) {
			cc[24 + i] = tint ? shadows.fogTint[i] : 0.0f;
			cc[28 + i] = shadows.fogSunColour[i];
		}
		memcpy(cc + 32, shadows.fogHorizon, sizeof(shadows.fogHorizon));
		// the live sky for reflections (SkyReflect; FogSky[48].w 0: none)
		memcpy(cc + 160, shadows.fogSky, sizeof(shadows.fogSky));
		// Extend map's ground beyond (FarGround, FarLand), the sun's direction (FarSun)
		std::copy_n(farGround, 12, cc + 360);
		const Vector3 sun = shadows.sunDirection.sqlen3() > 1e-6f ? shadows.sunDirection.normal() : Vector3(0, 1, 0);
		cc[372] = sun.x;
		cc[373] = sun.y;
		cc[374] = sun.z;
		cc[375] = 0.0f;
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
			eci.Features.Tessellation = DEVICE_FEATURE_STATE_OPTIONAL; // RenderSettings::terrainTessellation
			eci.Features.TimestampQueries = DEVICE_FEATURE_STATE_OPTIONAL; // the benchmark's GPU times
			eci.Features.RayTracing = DEVICE_FEATURE_STATE_OPTIONAL; // the ray-traced shadows (DiligentRayShadows)
			const std::string dxc = FindDxCompiler();
			if (!dxc.empty())
				eci.pDxCompilerPath = dxc.c_str();
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
			eci.Features.Tessellation = DEVICE_FEATURE_STATE_OPTIONAL; // RenderSettings::terrainTessellation
			eci.Features.TimestampQueries = DEVICE_FEATURE_STATE_OPTIONAL; // the benchmark's GPU times
			eci.Features.RayTracing = DEVICE_FEATURE_STATE_OPTIONAL; // the ray-traced shadows (DiligentRayShadows)
			const std::string dxc = FindDxCompiler();
			if (!dxc.empty())
				eci.pDxCompilerPath = dxc.c_str();
			if (adapter >= 0) eci.AdapterId = (Uint32)adapter;
			factory->CreateDeviceAndContextsD3D12(eci, &device, contexts);
			ctx.Attach(contexts[0]);
			if (!device)
				ferr("Diligent: could not create the Direct3D 12 device.");
			factory->CreateSwapChainD3D12(device, ctx, scDesc, FullScreenModeDesc{}, window, &swapChain);
			// the adapter's memory budget (QueryVideoMemoryInfo): the DXGI adapter with the device's
			// vendor and device IDs (the one with the most memory of those)
			IDXGIFactory4* dxgiFactory = nullptr;
			if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory4), (void**)&dxgiFactory))) {
				const auto& ai = device->GetAdapterInfo();
				SIZE_T best = 0;
				IDXGIAdapter1* a = nullptr;
				for (UINT i = 0; dxgiFactory->EnumAdapters1(i, &a) != DXGI_ERROR_NOT_FOUND; i++) {
					DXGI_ADAPTER_DESC1 d = {};
					IDXGIAdapter3* a3 = nullptr;
					if (SUCCEEDED(a->GetDesc1(&d)) && d.VendorId == ai.VendorId && d.DeviceId == ai.DeviceId && d.DedicatedVideoMemory >= best
						&& SUCCEEDED(a->QueryInterface(__uuidof(IDXGIAdapter3), (void**)&a3))) {
						if (dxgiAdapter)
							dxgiAdapter->Release();
						dxgiAdapter = a3;
						best = d.DedicatedVideoMemory;
					}
					a->Release();
				}
				dxgiFactory->Release();
			}
		}
		if (!swapChain)
			ferr("Diligent: could not create the swap chain.");
		printf("Diligent renderer: %s, adapter \"%s\"\n", isVulkan ? "Vulkan" : "Direct3D 12", device->GetAdapterInfo().Description);
		{
			const auto& info = device->GetDeviceInfo();
			const auto& rt = device->GetAdapterInfo().RayTracing;
			printf("Diligent: ray tracing %s (caps 0x%X, max recursion %u, primitives per BLAS up to %u), timestamp queries %s\n",
				info.Features.RayTracing == DEVICE_FEATURE_STATE_ENABLED ? "available" : "not available",
				(unsigned)rt.CapFlags, rt.MaxRecursionDepth, rt.MaxPrimitivesPerBLAS, info.Features.TimestampQueries == DEVICE_FEATURE_STATE_ENABLED ? "on" : "off");
			// (always: the frame-time window, Ctrl+F12, shows them too; a few queries a frame)
			if (info.Features.TimestampQueries == DEVICE_FEATURE_STATE_ENABLED) {
				QueryDesc qd;
				qd.Type = QUERY_TYPE_TIMESTAMP;
				for (int i = 0; i < GPU_QUERY_FRAMES; i++) {
					qd.Name = "GPU frame start";
					device->CreateQuery(qd, &gpuStart[i]);
					qd.Name = "GPU frame end";
					device->CreateQuery(qd, &gpuEnd[i]);
				}
				stamps.device = device;
				stamps.ctx = ctx;
				ProfilerSetGpuSource(&stamps);
			}
		}

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
		fogBuffer = createBuffer("Fog", 1504, BIND_UNIFORM_BUFFER, USAGE_DYNAMIC);
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
		pixelShaders[PS_SOFT] = createShader(SHADER_TYPE_PIXEL, "PS_Soft");
		{
			// the soft particles' depth (the frame's lighting depth) and its constants
			const PipelineResourceDesc softResources[] = {
				{ SHADER_TYPE_PIXEL, "softDepth", 1, SHADER_RESOURCE_TYPE_TEXTURE_SRV, SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC },
				{ SHADER_TYPE_PIXEL, "SoftBuffer", 1, SHADER_RESOURCE_TYPE_CONSTANT_BUFFER, SHADER_RESOURCE_VARIABLE_TYPE_STATIC },
			};
			PipelineResourceSignatureDesc softDesc;
			softDesc.Name = "Soft particles";
			softDesc.Resources = softResources;
			softDesc.NumResources = (Uint32)std::size(softResources);
			softDesc.BindingIndex = 1;
			device->CreatePipelineResourceSignature(softDesc, &softSignature);
			if (!softSignature)
				ferr("Diligent: could not create the soft particles' signature.");
			softBuffer = createBuffer("Soft particles", 16, BIND_UNIFORM_BUFFER, USAGE_DYNAMIC);
			softSignature->GetStaticVariableByName(SHADER_TYPE_PIXEL, "SoftBuffer")->Set(softBuffer);
			softSignature->CreateShaderResourceBinding(&softSrb, true);
		}

		Bitmap whiteBmp;
		whiteBmp.width = 1;
		whiteBmp.height = 1;
		whiteBmp.format = BMFORMAT_R8G8B8A8;
		whiteBmp.pixels.resize(4);
		*(uint32_t*)whiteBmp.pixels.data() = 0xFFFFFFFF;
		whiteTexture = CreateTexture(whiteBmp, 1);
		CreateCloudResources(this);
		CreateGIResources(this);
		shadows.rayShadowsSupported = DiligentRayShadows::Supported(device);
		g_rayTracingBuffers = shadows.rayShadowsSupported;
	}

	void Reset() override
	{
		swapChain->Resize(g_windowWidth, g_windowHeight);
	}

	// Frame begin/end

	void BeginDrawing() override
	{
		shadows.rayShadowsOn = shadows.rayReflectionsOn = false; // until the client interface asks for them this frame
		{
			// the per-pass timings (GpuProfiler): the frames whose stamps are in, this frame's
			const auto now = std::chrono::steady_clock::now();
			const double cpuMs = lastBegin.time_since_epoch().count() ? std::chrono::duration<double, std::milli>(now - lastBegin).count() : 0.0;
			lastBegin = now;
			stamps.slot = (uint32_t)(frame % PROFILER_FRAMES);
			ProfilerBeginFrame(frame, cpuMs);
		}
		manageResidency();
		if (gpuStart[0]) {
			const int q = (int)(frame % GPU_QUERY_FRAMES);
			// this slot's last frame: its time, if the GPU is done with it
			if (gpuQueryIssued[q]) {
				QueryDataTimestamp a, b;
				if (gpuStart[q]->GetData(&a, sizeof(a), false) && gpuEnd[q]->GetData(&b, sizeof(b), false) && a.Frequency)
					g_gpuFrameMs = (double)(b.Counter - a.Counter) * 1000.0 / (double)a.Frequency;
			}
			ctx->EndQuery(gpuStart[q]);
		}
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
		if (gpuStart[0]) {
			const int q = (int)(frame % GPU_QUERY_FRAMES);
			ctx->EndQuery(gpuEnd[q]);
			gpuQueryIssued[q] = true;
		}
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
		DiligentTexture* t = new DiligentTexture;
		makeGpuTexture(t, bm, mipmaps);
		return (texture)t;
	}

	// The GPU's memory budget (RenderSettings-independent, "vramBudgetMB" in the settings
	// lowers it for tests): a lazy texture keeps its pixels in RAM and goes to the GPU when first
	// drawn (or from the background queue while there is room); near the budget the least
	// recently drawn ones leave the GPU again, to come back when drawn.
	texture CreateTextureLazy(const Bitmap& bm, int mipmaps) override
	{
		DiligentTexture* t = new DiligentTexture;
		t->lazy = true;
		t->cpu = bm;
		t->mipmaps = mipmaps;
		lazyTextures.push_back(t);
		uploadQueue.push_back(t);
		// an uncompressed one's RGBA and mip chain made on a worker thread (a 4096 x 4096 PNG's
		// took seconds on the draw that needed it: the HD models' play test)
		if (!bm.compressed())
			g_texturePrep.push(t);
		else
			t->prep = 2;
		return (texture)t;
	}

	void UploadTextureNow(texture t) override
	{
		if (t)
			ensureResident((DiligentTexture*)t, true);
	}

	// non-blocking: ~0u while it isn't on the GPU (still being prepared, or over this frame's
	// upload allowance: then asked for first in the next frames)
	uint32_t TouchTexture(texture t) override
	{
		if (!t)
			return 0;
		DiligentTexture* dt = (DiligentTexture*)t;
		return ensureResident(dt, false) ? dt->generation : ~0u;
	}

	// the texture on the GPU: block (GetTextureView, the legacy draws, the level's own) prepares
	// and uploads it now whatever it costs; else only when prepared and within the frame's
	// allowance (64 MB), or it is asked for (requests) and false returned
	bool ensureResident(DiligentTexture* t, bool block = true)
	{
		t->lastUsed = frame;
		if (t->tex || !t->lazy)
			return true;
		if (!block && (t->prep != 2 || frameUploadBytes > (64ull << 20))) {
			if (!t->requested) {
				t->requested = true;
				requests.push_back(t);
			}
			return false;
		}
		if (t->prep != 2)
			g_texturePrep.finishNow(t);
		makeGpuTexture(t, t->cpu, t->mipmaps);
		t->generation++;
		t->requested = false;
		residentLazyBytes += t->bytes;
		frameUploadBytes += t->bytes;
		vram.usage += t->bytes;
		vram.uploads++;
		return true;
	}

	void evictTexture(DiligentTexture* t)
	{
		if (!t->tex)
			return;
		if (currentTexture == (texture)t)
			currentTexture = nullptr;
		t->srb.Release();
		t->view.Release();
		t->tex.Release();
		t->generation++;
		residentLazyBytes -= t->bytes;
		vram.evictions++;
		vram.evictedBytes += t->bytes;
	}

	void printVram(uint64_t used)
	{
		size_t residentCount = 0;
		for (DiligentTexture* t : lazyTextures)
			residentCount += t->tex ? 1 : 0;
		printf("VRAM: %llu of %llu MB budget; model textures %zu of %zu on the GPU (%llu MB), %llu uploads, %llu evictions (%llu MB), %zu queued, %zu asked for\n",
			(unsigned long long)(used >> 20), (unsigned long long)(vram.budget >> 20), residentCount, lazyTextures.size(),
			(unsigned long long)(residentLazyBytes >> 20), (unsigned long long)vram.uploads, (unsigned long long)vram.evictions,
			(unsigned long long)(vram.evictedBytes >> 20), uploadQueue.size(), requests.size());
	}

	// Once a frame: the budget (DXGI's, every 30 frames), the textures asked for (drawn with a
	// stand-in until they are on the GPU), the evictions near the budget, the background uploads
	// below it
	void manageResidency()
	{
		frameUploadBytes = 0;
		static const uint64_t testBudget = (uint64_t)std::max(0, g_settings.value<int>("vramBudgetMB", 0)) << 20;
		if (frame % 30 == 0 || !vram.queried) {
			vram.queried = true;
			vram.budget = vram.usage = 0;
			if (dxgiAdapter) {
				DXGI_QUERY_VIDEO_MEMORY_INFO info = {};
				if (SUCCEEDED(dxgiAdapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info))) {
					vram.budget = info.Budget;
					vram.usage = info.CurrentUsage;
				}
			}
			vram.evictedSinceQuery = 0;
			if (testBudget) {
				// (without DXGI: the lazy textures alone against it)
				vram.budget = vram.budget ? std::min(vram.budget, testBudget) : testBudget;
				if (!dxgiAdapter)
					vram.usage = residentLazyBytes;
			}
		}
		// the textures asked for, the oldest first: those prepared, within the frame's allowance
		for (size_t i = 0; i < requests.size() && frameUploadBytes < (96ull << 20); ) {
			DiligentTexture* t = requests[i];
			if (t->tex) {
				t->requested = false;
				requests.erase(requests.begin() + i);
			}
			else if (t->prep == 2) {
				const uint64_t last = t->lastUsed;
				ensureResident(t, true);
				t->lastUsed = last;
				requests.erase(requests.begin() + i);
			}
			else
				i++;
		}
		if (!vram.budget)
			return;
		// what is in use now: the last reading, less what has been evicted since (freed a few
		// frames later), plus what has been uploaded since
		const uint64_t used = vram.usage > vram.evictedSinceQuery ? vram.usage - vram.evictedSinceQuery : 0;
		const uint64_t high = vram.budget / 100 * 90, low = vram.budget / 100 * 80, room = vram.budget / 100 * 75;
		// (looked at every 10th frame: over the budget with little to evict, every frame sorted
		// the lazy textures for nothing)
		if (used > high && frame % 10 == 0) {
			std::vector<DiligentTexture*> resident;
			for (DiligentTexture* t : lazyTextures)
				if (t->tex && t->lastUsed + 3 < frame)
					resident.push_back(t);
			std::sort(resident.begin(), resident.end(), [](DiligentTexture* a, DiligentTexture* b) { return a->lastUsed < b->lastUsed; });
			uint64_t now = used;
			for (DiligentTexture* t : resident) {
				if (now <= low)
					break;
				now -= std::min(now, t->bytes);
				vram.evictedSinceQuery += t->bytes;
				evictTexture(t);
			}
			// the budget below what the rest of the game takes (meshes, render targets, the
			// terrain, the textures in view): said once a minute, the setting can't be met
			if (now > high && (vram.unreachableSaid == 0 || frame >= vram.unreachableSaid + 3600)) {
				vram.unreachableSaid = frame;
				printf("VRAM: %llu MB in use stays over the %llu MB budget: the model textures out of view are all off the GPU, the rest can't be evicted\n",
					(unsigned long long)(now >> 20), (unsigned long long)(vram.budget >> 20));
			}
		}
		else if (used < room && !uploadQueue.empty()) {
			// the background queue: up to 48 MB a frame, the prepared ones
			uint64_t sent = 0;
			for (size_t n = uploadQueue.size(); n > 0 && !uploadQueue.empty() && sent < (48ull << 20) && used + sent < room; n--) {
				DiligentTexture* t = uploadQueue.front();
				uploadQueue.pop_front();
				if (t->tex)
					continue;
				if (t->prep != 2) {
					uploadQueue.push_back(t); // (not ready yet: later)
					continue;
				}
				const uint64_t last = t->lastUsed;
				ensureResident(t, true);
				t->lastUsed = last; // (not drawn: the first to go again)
				sent += t->bytes;
			}
		}
		if ((frame % 600 == 0 || g_windowQuit) && !vram.finalSaid && (vram.uploads || vram.evictions)) {
			vram.finalSaid = g_windowQuit;
			printVram(used);
		}
	}

	// The GPU texture (and its binding) from a bitmap: RGBA with the D3D11 renderer's mipmaps, or
	// block-compressed as it is
	void makeGpuTexture(DiligentTexture* t, const Bitmap& bm, int mipmaps)
	{
		if (bm.compressed()) {
			makeBCTexture(t, bm, mipmaps);
			return;
		}
		// (a lazy texture's levels prepared on a worker thread)
		std::vector<Bitmap> made;
		if (!(t->lazy && !t->levels.empty()))
			made = MakeMipChain(bm, mipmaps);
		const std::vector<Bitmap>& levels = made.empty() ? t->levels : made;
		const Bitmap& cvtbmp = levels[0];

		TextureDesc desc;
		desc.Type = RESOURCE_DIM_TEX_2D;
		desc.Width = cvtbmp.width;
		desc.Height = cvtbmp.height;
		desc.MipLevels = (Uint32)levels.size();
		desc.Format = TEX_FORMAT_RGBA8_UNORM;
		desc.BindFlags = BIND_SHADER_RESOURCE;
		desc.Usage = USAGE_DEFAULT;
		std::vector<TextureSubResData> sub(levels.size());
		t->bytes = 0;
		for (size_t i = 0; i < levels.size(); i++) {
			sub[i].pData = levels[i].pixels.data();
			sub[i].Stride = levels[i].width * 4u;
			t->bytes += (uint64_t)levels[i].width * levels[i].height * 4;
		}
		TextureData data{ sub.data(), (Uint32)sub.size() };

		device->CreateTexture(desc, &data, &t->tex);
		if (!t->tex)
			ferr("Diligent: could not create a %ix%i texture.", cvtbmp.width, cvtbmp.height);
		signature->CreateShaderResourceBinding(&t->srb, true);
		t->srb->GetVariableByName(SHADER_TYPE_PIXEL, "inpTexture")->Set(t->tex->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
	}

	// BC1-BC5 and BC7 (DDS, KTX2) as they are, with the file's mip levels (one if mipmaps is 1);
	// BC4's single channel read as grey (its view's swizzle)
	void makeBCTexture(DiligentTexture* t, const Bitmap& bm, int mipmaps)
	{
		const int bc = bm.bcKind();
		const Uint64 blockBytes = (bc == 1 || bc == 4) ? 8 : 16;
		const size_t count = mipmaps == 1 ? 1 : bm.levels.size();
		std::vector<TextureSubResData> sub(count);
		for (size_t i = 0; i < count; i++) {
			sub[i].pData = bm.pixels.data() + bm.levels[i];
			sub[i].Stride = (Uint64)std::max(1, (bm.width >> i) / 4) * blockBytes;
		}
		t->bytes = count < bm.levels.size() ? bm.levels[count] : bm.pixels.size();
		TextureDesc desc;
		desc.Type = RESOURCE_DIM_TEX_2D;
		desc.Width = bm.width;
		desc.Height = bm.height;
		desc.MipLevels = (Uint32)count;
		desc.Format = bc == 1 ? TEX_FORMAT_BC1_UNORM : bc == 2 ? TEX_FORMAT_BC2_UNORM : bc == 3 ? TEX_FORMAT_BC3_UNORM
			: bc == 4 ? TEX_FORMAT_BC4_UNORM : bc == 5 ? TEX_FORMAT_BC5_UNORM : TEX_FORMAT_BC7_UNORM;
		desc.BindFlags = BIND_SHADER_RESOURCE;
		desc.Usage = USAGE_DEFAULT;
		TextureData data{ sub.data(), (Uint32)sub.size() };
		device->CreateTexture(desc, &data, &t->tex);
		if (!t->tex)
			ferr("Diligent: could not create a %ix%i BC%d texture.", bm.width, bm.height, bc);
		if (bc == 4) {
			TextureViewDesc vd;
			vd.ViewType = TEXTURE_VIEW_SHADER_RESOURCE;
			vd.Swizzle = TextureComponentMapping{ TEXTURE_COMPONENT_SWIZZLE_R, TEXTURE_COMPONENT_SWIZZLE_R, TEXTURE_COMPONENT_SWIZZLE_R, TEXTURE_COMPONENT_SWIZZLE_ONE };
			t->tex->CreateView(vd, &t->view);
		}
		signature->CreateShaderResourceBinding(&t->srb, true);
		t->srb->GetVariableByName(SHADER_TYPE_PIXEL, "inpTexture")->Set(t->view ? t->view.RawPtr() : t->tex->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
	}

	void FreeTexture(texture t) override
	{
		if (t && t != whiteTexture) {
			if (currentTexture == t)
				currentTexture = nullptr;
			DiligentTexture* dt = (DiligentTexture*)t;
			if (dt->lazy) {
				g_texturePrep.forget(dt);
				requests.erase(std::remove(requests.begin(), requests.end(), dt), requests.end());
				if (dt->tex)
					residentLazyBytes -= dt->bytes;
				lazyTextures.erase(std::remove(lazyTextures.begin(), lazyTextures.end(), dt), lazyTextures.end());
				uploadQueue.erase(std::remove(uploadQueue.begin(), uploadQueue.end(), dt), uploadQueue.end());
			}
			delete dt;
		}
	}

	void UpdateTexture(texture t, const Bitmap& bmp) override
	{
		DiligentTexture* dt = (DiligentTexture*)t;
		if (dt->lazy) { // its pixels for its next upload, and on the GPU again now
			g_texturePrep.forget(dt);
			if (bmp.compressed()) {
				dt->cpu = bmp;
				dt->levels.clear();
			}
			else
				dt->levels = MakeMipChain(bmp, dt->mipmaps);
			dt->prep = 2;
			if (dt->tex)
				evictTexture(dt);
			ensureResident(dt);
			return;
		}
		const TextureDesc& desc = dt->tex->GetDesc();
		if (bmp.compressed() || desc.Format != TEX_FORMAT_RGBA8_UNORM) {
			printf("Hot reload: block-compressed textures are not reloaded (restart to see the change)\n");
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

	template <typename T> void initStream(DiligentStreamBuffer<T>& stream, int count, const char* name, Diligent::BIND_FLAGS bind)
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
		state.ps = softParticles ? PS_SOFT : PS_PLAIN;
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

