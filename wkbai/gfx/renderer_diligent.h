// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#pragma once

#include "renderer.h"

#include "RenderDevice.h"
#include "DeviceContext.h"
#include "SwapChain.h"
#include "RefCntAutoPtr.hpp"

struct Camera;
// buffers a ray-tracing structure is built from get BIND_RAY_TRACING (the device can)
extern bool g_rayTracingBuffers;

// The Diligent IRenderer, as seen by the renderers that draw with Diligent directly
// (DiligentEnhancedSceneRenderer, DiligentEnhancedTerrainRenderer).
struct DiligentGfx : IRenderer
{
	Diligent::RefCntAutoPtr<Diligent::IRenderDevice> device;
	Diligent::RefCntAutoPtr<Diligent::IDeviceContext> ctx;
	Diligent::RefCntAutoPtr<Diligent::ISwapChain> swapChain;

	// Dynamic constant buffers kept up to date by SetTransformMatrix and SetFog:
	// cbuffer TransformBuffer { matrix Transform; } and
	// cbuffer FogBuffer { float4 FogColor; float FogStartDist; float FogEndDist; float4 FogCamera; float4 Grade; float4 EdgeFog;
	//   float4 LightMasks; float4 FogSun; float4 FogToward; float4 FogAway; } (LightMasks: the depth pre-pass's masks in
	//   this frame's shading, FinishLightingPrepass; FogSun, FogToward, FogAway: the haze's tint, shadows.fogTint)
	Diligent::RefCntAutoPtr<Diligent::IBuffer> transformBuffer, fogBuffer;

	// Shader resource view of a texture from CreateTexture (a white texture for nullptr).
	virtual Diligent::ITextureView* GetTextureView(texture t) = 0;

	// Copy data into the current frame's stream buffers, returning the buffer and the byte
	// offset to bind it at. The data stays valid until the end of the frame.
	virtual Diligent::IBuffer* StreamVertexData(const void* data, uint64_t size, uint64_t& offset) = 0;
	virtual Diligent::IBuffer* StreamIndexData(const void* data, uint64_t size, uint64_t& offset) = 0;

	// Camera position for the enhanced renderers' fog, which like the original's grows with the
	// distance to the camera rather than with the view depth (FogBuffer.FogCamera).
	virtual void SetFogCamera(const Vector3& position) = 0;
	// Map-edge fog: the playable area's size, and the distances outside it where the edge fog
	// starts and becomes opaque (FogBuffer.EdgeFog).
	virtual void SetFogEdge(float playableWidth, float playableDepth, float start, float opaque) = 0;

	// Binds the swap chain's targets and the full viewport again after drawing offscreen (the
	// post-processing scene targets instead while those are on).
	virtual void RestoreRenderTargets() = 0;
	// A part of the window (pixels) to draw into, until RestoreRenderTargets; and the depth
	// cleared, the colour kept (the selection window's unit portrait over the HUD)
	virtual void SetViewportPixels(int x, int y, int width, int height) = 0;
	virtual void ClearDepthOnly() = 0;
	// The colour target the 3D scene is being drawn into (the back buffer, or the
	// post-processing scene target while that is on): the water copies it for its refraction
	virtual Diligent::ITextureView* CurrentColorTarget() = 0;
	virtual Diligent::ITextureView* CurrentDepthTarget() = 0;

	// Post-processing (ambient occlusion, bloom): BeginPostProcessing(true) before ClearFrame sends
	// the frame's 3D drawing into offscreen targets; ApplyPostProcessing, after the 3D scene and
	// before the HUD, writes the processed scene into the back buffer and binds that again.
	// effects: SSAO, bloom, anti-aliasing and FSR upscaling as set (else only the fog of war:
	// classic graphics). With TAA or FSR 3.1, BeginPostProcessing sets the frame's
	// g_projectionJitter, so it comes before the camera's updateMatrix.
	virtual void BeginPostProcessing(bool on, bool effects = true) = 0;
	virtual void ApplyPostProcessing(const Camera& camera) = 0;
	// The fog of war for the next ApplyPostProcessing: per 5 m tile of the playable area (row z,
	// column x), 255 where the player's units see; tiles nullptr: none. The data must stay valid
	// until then; version changes with it.
	virtual void SetFogOfWar(const uint8_t* tiles, uint32_t width, uint32_t height, uint32_t version, const Matrix& inverseView, float unseenBrightness) = 0;
	// FSR 3.1 in this frame (RenderSettings::fsr3): the target the scene renderer draws its
	// moving objects' motion vectors into, with the scene's depth (RG16F, UV: where the pixel
	// was in the last frame minus where it is), else null; and the points where the opaque
	// scene is done (before the water) and the water is (before the waterfalls and the
	// particles), for FSR's reactive mask (each binds the scene's targets again)
	virtual Diligent::ITextureView* MotionVectorTarget() = 0;
	virtual void MarkOpaqueSceneDone() = 0;
	virtual void MarkWaterDone() = 0;
	// The depth pre-pass (the enhanced renderers, with the ray-traced shadows or the ambient
	// occlusion on): the opaque scene's depth first, into a lighting depth of its own, the
	// ray-traced shadows' mask and the ambient occlusion from it, then the scene drawn and shaded
	// with them (the mask on the sun's light, the occlusion on the ambient: not over the whole
	// picture afterwards). LightingPrepassWanted: whether it runs in this frame;
	// BeginLightingPrepass: the lighting depth's target, cleared (the models, the ground and the
	// water's surfaces are drawn into it), or null; FinishLightingPrepass: the masks
	// (shadows.lightRtMask and lightAo, FogBuffer.LightMasks) and the scene's targets bound again
	virtual bool LightingPrepassWanted() = 0;
	// The post-processing's shaders and pipelines made now (else in the first frame that uses
	// it): before a level's first frame, with the loading screen moving
	virtual void PreparePostProcessing() = 0;
	virtual Diligent::ITextureView* BeginLightingPrepass() = 0;
	virtual void FinishLightingPrepass(const Camera& camera) = 0;
	// In the depth pre-pass, after the ground's and the water's depth and before the models': the
	// surface depth kept (the projected decals lie on it)
	virtual void SnapshotSurfaceDepth() = 0;
	// Decals projected from the depth pre-pass onto the ground and the water (the selected
	// objects' range bands, the footprint squares of a building being placed): each a box from
	// yMin to yMax over a ring (outer and inner radius around the centre; tiles: the texture's
	// repeats around it, its width along the ring, its height across, top outwards) or a square
	// (outer: half its side), coloured (ARGB); drawn wherever the surface lies inside, over the
	// grass, hidden by what stands in front (the pre-pass's models). Available when the pre-pass
	// ran in this frame (else the caller draws them as before).
	struct ProjectedDecal {
		float centre[2] = {}, outer = 1, inner = 0, yMin = -1e4f, yMax = 1e4f, tiles = 1;
		bool square = false;
		uint32_t colour = 0xFFFFFFFF;
	};
	virtual bool ProjectedDecalsAvailable() = 0;
	virtual void DrawProjectedDecals(texture tex, const ProjectedDecal* decals, size_t count) = 0;
	// The particles between SetSoftParticles(true) and (false) fade out where they meet the
	// ground, the water and the models (the depth pre-pass's depth; off without it)
	virtual void SetSoftParticles(bool on) = 0;
	// The frames presented so far: which drawing is in the same frame, or the next
	virtual uint64_t FrameIndex() = 0;

	// The original's terrain shadows, shared by the enhanced renderers (see docs/re/shadows.md
	// in the RE repository). The terrain renderer creates the maps, the scene renderer draws
	// the static shadow casters into the shadow map before the terrain samples it.
	struct ShadowEnvironment {
		float time = 0.0f; // game time in seconds, scrolls the cloud shadows
		bool cloudShadows = true, staticShadows = true;
		Diligent::RefCntAutoPtr<Diligent::ITextureView> cloudMap; // sky_shadow_map.pcx, or null
		Diligent::RefCntAutoPtr<Diligent::ITextureView> staticShadowDSV, staticShadowSRV;
		Matrix staticShadowMatrix; // world → shadow map clip space, row vectors
		bool staticShadowValid = false; // drawn in this frame
		// The frame's unit blob shadows (Shadow.pcx), each as its sprite's corners p1, p2, p3
		// (x, z, in map space: world plus the map edge). The terrain renderer draws them into a
		// map-sized shadow texture that the terrain multiplies in, and clears the list.
		std::vector<float> blobs;
		// The frame's fire lights (RenderSettings::fireLights), at most MAX_FIRE_LIGHTS, nearest
		// to the camera first: each as position x, y, z, radius, then colour r, g, b (0-1, times
		// the strength). Filled by the client interface from the fire particle systems.
		static constexpr int MAX_FIRE_LIGHTS = 16;
		std::vector<float> fireLights;
		// The sky's mean colours above, at the horizon and below (0-1, from the sky box's faces,
		// DiligentSkyRenderer), and their mean brightness: the models' image-based light
		// (per-pixel lighting); invalid without a sky
		float skyLight[3][3] = {};
		float skyLightLuminance = 1.0f;
		bool skyLightValid = false;
		// the sky box's faces front (+z), right (+x), back, left and top (DiligentSkyRenderer),
		// for the water's reflection; null without a sky
		texture skyFaces[5] = {};
		// the physical sky is drawn instead of the sky box (with the volumetric clouds): skyLight
		// is its, and the water reflects its colours, not the box's faces
		bool physicalSky = false;
		// the haze with the physical sky (DiligentSkyRenderer): the fog's colour instead of the
		// map's, following the weather's clouds
		bool hazeOn = false;
		float hazeColour[3] = {};

		// Dynamic clouds and the sky light (DiligentClouds; RenderSettings::dynamicClouds, skyGI):
		// the cloud noise, the cloud constant buffer (cbuffer CloudBuffer), this frame's mode
		Diligent::RefCntAutoPtr<Diligent::ITexture> cloudNoise;
		// the volumetric clouds' 3D noise (DiligentClouds): the shape and the erosion's detail
		Diligent::RefCntAutoPtr<Diligent::ITexture> cloudShape, cloudDetail;
		Diligent::RefCntAutoPtr<Diligent::IBuffer> cloudBuffer;
		int clouds = 0;
		bool cloudShadowsDrawn = false; // the enhanced scene renderer wrote the cloud buffer this frame
		// the sky light's map (RGBA16F: the sky seen, the ground's height, the casters' top) over
		// a square of 2 x SKY_OCC_RADIUS around the camera's target, from the terrain's heights
		// and the static casters from above (skyDepth, drawn by the scene renderer)
		static constexpr int SKY_OCC_SIZE = 512;
		static constexpr float SKY_OCC_RADIUS = 260.0f, SKY_OCC_TOP = 600.0f, SKY_OCC_RANGE = 1000.0f;
		Diligent::RefCntAutoPtr<Diligent::ITexture> skyOcc, skyDepth;
		Diligent::RefCntAutoPtr<Diligent::IPipelineResourceSignature> skyOccSignature;
		Diligent::RefCntAutoPtr<Diligent::IPipelineState> skyOccPipeline;
		Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> skyOccBinding;
		float skyOccOrigin[2] = {};
		bool skyOccOn = false; // this frame
		// Dynamic global illumination (DiligentGI; RenderSettings::dynamicGI): the bounce map
		// (RGBA16F with mips: the light leaving the ground, roofs and units, and its height) over
		// a square of GI_EXTENT around the camera's target, snapped to GI_SNAP; the models seen
		// from above for it (albedo, the sun's n.l; the light they give off, their emissive maps',
		// and its copy with mips, giEmission, for the shaders: the capture is drawn with them bound;
		// and their depth); the last frame's map at
		// mip 2 for the second bounce (High); the terrain's albedo over the whole map (baked by
		// the terrain renderer; giAlbedoRegion: its corner x, z and 1 / width, 1 / depth in world
		// units)
		static constexpr int GI_SIZE = 256, GI_PREV_SIZE = 64;
		static constexpr float GI_EXTENT = 512.0f, GI_SNAP = 64.0f;
		Diligent::RefCntAutoPtr<Diligent::ITexture> giRadiance, giPrev, giCapture, giCaptureEmission, giEmission, giCaptureDepth, giAlbedo;
		Diligent::RefCntAutoPtr<Diligent::IPipelineResourceSignature> giSignature;
		Diligent::RefCntAutoPtr<Diligent::IPipelineState> giPipeline;
		Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> giBinding;
		Diligent::RefCntAutoPtr<Diligent::IBuffer> giBuffer;
		Diligent::ITextureView* giBoundHeights = nullptr;
		Diligent::ITexture* giBoundAlbedo = nullptr;
		float giAlbedoRegion[4] = {};
		float giOrigin[2] = {}, giPrevOrigin[2] = {};
		bool giPrevValid = false;
		int giMode = 0; // this frame: 0 off, 1 Low, 2 High, 3 Ultra
		// Ultra's lights from the emissive maps (DiligentEnhancedSceneRenderer: worked out once
		// per model and material from where its glowing texels sit, placed per frame): giLights,
		// per light 3 float4s (position, radius; colour, softening; the way it faces, how much it
		// faces one way), and giLightGrid over the bounce map's square (GIRegion), in
		// GI_LIGHT_CELLS x GI_LIGHT_CELLS cells of GI_CELL_SLOTS uints: the count, then the lights
		// reaching the cell (the brightest first)
		static constexpr int MAX_GI_LIGHTS = 2048, GI_LIGHT_CELLS = 32, GI_CELL_SLOTS = 32;
		Diligent::RefCntAutoPtr<Diligent::IBuffer> giLights, giLightGrid;
		int giLightCount = 0; // this frame's
		// the map is drawn again every GI_REFRESH_LOW / _HIGH frames (the light changes slowly),
		// and at once when its square moves or the mode changes (giRefresh: this frame);
		// giMapOrigin: where the map in giRadiance lies (the shaders' GIRegion)
		static constexpr int GI_REFRESH_LOW = 4, GI_REFRESH_HIGH = 2;
		float giMapOrigin[2] = { 1e30f, 1e30f };
		int giMapMode = 0, giFrames = 0;
		bool giRefresh = false;
		// the clouds' weather (DiligentClouds UpdateCloudBuffer): the preset now, blending towards
		// the weather's; cloudWeatherKind: the seasons' weather kind when it drives the clouds (else
		// -1: RenderSettings::cloudWeather); its lightning (cloudFlashOverride >= 0: the weather's
		// flash strength and cloudFlash's position set by it, else the clouds' own flashes)
		struct CloudPresetState { float cover = 0.45f, base = 230, thickness = 150, stratus = 0, storm = 0, density = 1, shadow = 0.62f, light = 1, cirrus = 0, alto = 0, altoSheet = 0; } cloudPreset;
		// cloudPreset is the weather at the camera; a weather front (UpdateCloudBuffer) between
		// cloudFrom (ahead of it) and cloudTo (behind it), at frontPos along the wind
		CloudPresetState cloudFrom, cloudTo;
		int cloudKindNow = -2; // -2: no frame yet
		bool frontActive = false;
		float frontPos = 0, frontHalfWidth = 0;
		uint64_t cloudPresetTicks = 0;
		int cloudWeatherKind = -1;
		// the game's sky without the seasons (-1: none, the lighting options' cloudWeather and
		// cloudCover; 0 by cloudGameCover, 1-10 a weather)
		int cloudGameWeather = -1;
		float cloudGameCover = 0.45f;
		float cloudFlash[4] = {}, cloudFlashOverride = -1, cloudFlashStart = -100, cloudFlashNext = 0;
		// the terrain's vertex heights (the terrain renderer's), its grid's vertices across and
		// down and the map edge in tiles
		Diligent::ITextureView* terrainHeights = nullptr;
		int terrainGrid[3] = {};
		// the terrain for the ray-traced shadows (the terrain renderer's while it lives) and its
		// heights' version, raised whenever they change
		const class Terrain* rtTerrain = nullptr;
		uint64_t rtTerrainVersion = 0;
		// ray-traced shadows (DiligentRayShadows, RenderSettings::rtShadows): made at the first
		// frame that wants them; on in this frame; the sun (towards it) for the mask. The water's
		// ray-traced reflections (RenderSettings::rtReflections) use the same scene.
		class DiligentRayShadows* rayShadows = nullptr;
		bool rayShadowsSupported = false, rayShadowsOn = false, rayReflectionsOn = false;
		Vector3 sunDirection = Vector3(0, 1, 0);
		// a small extra view (the selection window's unit portrait): the clouds' update, the sky
		// light's map and the ray-traced passes belong to the world view and are left out (they
		// set their own viewports and follow the camera)
		bool portraitPass = false;
		// the depth pre-pass's masks in this frame (FinishLightingPrepass; null: none, a white
		// texture stands in): the ray-traced shadows' (1 sunlit, the frame's size) and the
		// ambient occlusion (1 open, half size)
		Diligent::ITextureView* lightRtMask = nullptr;
		Diligent::ITextureView* lightAo = nullptr;
		bool lightPrepass = false;
		// the haze's tint along the horizon (the physical sky's, DiligentSkyRenderer;
		// FogBuffer.FogSun, FogSunColour, FogHorizon): towards the true sun and the tint's weight,
		// the sun's disc through the air and its glow's strength, the sky 2 degrees up by bearing
		float fogTint[4] = {}, fogSunColour[4] = {}, fogHorizon[32][4] = {};
		// the live sky for reflections (FogBuffer.FogSky, DiligentSkyRenderer): the physical sky
		// with the clouds' grey and the night's air, 16 bearings (atan2(z, x)) at 4, 22 and 50
		// degrees up ([0..15], [16..31], [32..47]), the zenith ([48], w: 1 valid) and the ground
		// below ([49]); w 0 with the sky box (the shaders keep the box's light)
		float fogSky[50][4] = {};
	} shadows;
};

// The cloud shadow mapping measured in the original: one texel of the 256×256 map covers
// 4×4 world units, and the map scrolls by (4.06, 1.17) texels per second of game time.
constexpr float CLOUD_UV_PER_UNIT = 1.0f / 1024.0f;
constexpr float CLOUD_SCROLL_U = 4.06f / 256.0f, CLOUD_SCROLL_V = 1.17f / 256.0f;

