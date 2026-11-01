// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#pragma once

#include "RenderDevice.h"
#include "DeviceContext.h"
#include "RefCntAutoPtr.hpp"
#include "../util/vecmat.h"

// Post-processing of the 3D scene (enhanced graphics on Diligent; the original has none):
// screen-space ambient occlusion from the depth buffer, bloom, anti-aliasing (FXAA or TAA) and
// AMD FSR 1 upscaling, or AMD FSR 3.1 for both (DiligentFsr3). The scene is drawn into this
// object's colour and depth targets (same formats as the swap chain, so every pipeline fits;
// smaller than the window with upscaling), then apply() writes the result into the back buffer,
// where the HUD is drawn on top.
struct DiligentPostProcess {
	// the fog of war: per 5 m tile of the playable area (row z, column x), 255 where the player's
	// units see; version: changes with the data
	struct FogOfWar {
		const uint8_t* tiles = nullptr;
		uint32_t width = 0, height = 0, version = 0;
		Matrix inverseView;
		float unseenBrightness = 0.55f;
	};
	struct Settings {
		FogOfWar fog;
		bool ssao = true, bloom = true;
		float ssaoRadius = 2.5f;    // world units
		float ssaoStrength = 0.7f;  // 0: none, 1: fully dark where occluded
		float bloomThreshold = 0.72f, bloomIntensity = 0.35f;
		int antiAliasing = 1;   // 0 off, 1 FXAA, 2 TAA (RenderSettings::antiAliasing)
		float sharpness = 0.8f; // FSR's (and TAA's) sharpening, 0-1 (RenderSettings::sharpness)
		// TAA: the camera's view and (jittered) projection, the jitter in the projection (its
		// _31 and _32) and whether it is orthographic (no TAA then)
		Matrix view, projection;
		float jitter[2] = {};
		bool ortho = false;
		// FSR 3.1 instead of the anti-aliasing and FSR 1 (not orthographic): the jitter in render
		// pixels (+y down), the camera's planes and vertical field of view, the frame's time
		bool fsr3 = false;
		float jitterPixels[2] = {};
		float cameraNear = 1.0f, cameraFar = 1000.0f, fovY = 0.9f;
		float frameTimeMs = 16.7f;
	};
	// The object motion vectors' clear value: no moving object drawn there (the camera's
	// motion is computed from the depth instead)
	static constexpr float NO_OBJECT_MOTION = 32768.0f;

	DiligentPostProcess(Diligent::IRenderDevice* device, Diligent::TEXTURE_FORMAT colorFormat, Diligent::TEXTURE_FORMAT depthFormat);
	~DiligentPostProcess();
	// the scene's targets at this size, the back buffer's size (larger: FSR upscales to it)
	void resize(uint32_t width, uint32_t height, uint32_t outputWidth, uint32_t outputHeight);
	Diligent::ITextureView* colorRTV() const;
	Diligent::ITextureView* depthDSV() const;
	// FSR 3.1's inputs from the frame's drawing: beginFrame (after resize) clears the motion
	// vectors of moving objects, which the scene renderer draws into objectMotionRTV (RG16F, UV:
	// where the pixel was in the last frame minus where it is); copyOpaque keeps the scene
	// before its water, copyAfterWater before the rest of its transparent parts (waterfalls,
	// particles), for the reactive mask
	void beginFrame(Diligent::IDeviceContext* ctx, bool fsr3);
	Diligent::ITextureView* objectMotionRTV() const;
	void copyOpaque(Diligent::IDeviceContext* ctx);
	void copyAfterWater(Diligent::IDeviceContext* ctx);
	// projection: the camera's (row vectors, D3D style: z_clip = z P33 + P43, w = z)
	void apply(Diligent::IDeviceContext* ctx, Diligent::ITextureView* backBufferRTV, const Matrix& projection, const Settings& settings);

private:
	struct Impl;
	Impl* _impl;
};
