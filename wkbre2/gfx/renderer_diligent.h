// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#pragma once

#include "renderer.h"

#include "RenderDevice.h"
#include "DeviceContext.h"
#include "SwapChain.h"
#include "RefCntAutoPtr.hpp"

// The Diligent IRenderer, as seen by the renderers that draw with Diligent directly
// (DiligentEnhancedSceneRenderer, DiligentEnhancedTerrainRenderer).
struct DiligentGfx : IRenderer
{
	Diligent::RefCntAutoPtr<Diligent::IRenderDevice> device;
	Diligent::RefCntAutoPtr<Diligent::IDeviceContext> ctx;
	Diligent::RefCntAutoPtr<Diligent::ISwapChain> swapChain;

	// Dynamic constant buffers kept up to date by SetTransformMatrix and SetFog:
	// cbuffer TransformBuffer { matrix Transform; } and
	// cbuffer FogBuffer { float4 FogColor; float FogStartDist; float FogEndDist; float4 FogCamera; float4 Grade; float4 EdgeFog; }
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
	// effects: SSAO, bloom and FXAA as set (else only the fog of war: classic graphics)
	virtual void BeginPostProcessing(bool on, bool effects = true) = 0;
	virtual void ApplyPostProcessing(const Matrix& projection) = 0;
	// The fog of war for the next ApplyPostProcessing: per 5 m tile of the playable area (row z,
	// column x), 255 where the player's units see; tiles nullptr: none. The data must stay valid
	// until then; version changes with it.
	virtual void SetFogOfWar(const uint8_t* tiles, uint32_t width, uint32_t height, uint32_t version, const Matrix& inverseView, float unseenBrightness) = 0;

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
	} shadows;
};

// The cloud shadow mapping measured in the original: one texel of the 256×256 map covers
// 4×4 world units, and the map scrolls by (4.06, 1.17) texels per second of game time.
constexpr float CLOUD_UV_PER_UNIT = 1.0f / 1024.0f;
constexpr float CLOUD_SCROLL_U = 4.06f / 256.0f, CLOUD_SCROLL_V = 1.17f / 256.0f;

