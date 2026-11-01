// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#pragma once

#include "RenderDevice.h"
#include "DeviceContext.h"
#include "RefCntAutoPtr.hpp"

// AMD FidelityFX Super Resolution 3.1's temporal upscaler (FidelityFX/, the SDK 1.1.4's shaders,
// MIT licence) on Diligent: the SDK's compute passes, run in the order and with the constants of
// its host code (ffx_fsr3upscaler.cpp), which this replaces. It anti-aliases and upscales in one:
// from the jittered scene, its depth and motion vectors to the output's size.
struct DiligentFsr3 {
	struct Frame {
		Diligent::ITexture* color = nullptr;         // the scene, jittered (render size)
		Diligent::ITexture* depth = nullptr;         // its depth (D3D style: 0 near, 1 far)
		Diligent::ITexture* motionVectors = nullptr; // RG: where each pixel was in the last frame, minus where it is (UV)
		Diligent::ITexture* reactive = nullptr;      // R 0-1: how much to trust the current frame over the history, or null
		uint32_t renderWidth = 0, renderHeight = 0, outputWidth = 0, outputHeight = 0;
		float jitterX = 0, jitterY = 0;  // the projection's jitter in render pixels (+y down; jitterOffset)
		float cameraNear = 1, cameraFar = 1000, fovY = 1; // the projection's (radians)
		float frameTimeMs = 16.7f;
		float sharpness = 0.8f;          // RCAS after the upscale: 0 soft to 1 sharpest
		bool reset = false;              // a new view: no history
	};

	explicit DiligentFsr3(Diligent::IRenderDevice* device);
	~DiligentFsr3();
	// The upscaled frame (output size, RGBA16F), anti-aliased and sharpened
	Diligent::ITexture* dispatch(Diligent::IDeviceContext* ctx, const Frame& frame);

	// The jitter sequence for an upscale from render to output width: its length and the
	// frame's offset (Halton 2, 3, in pixels, -0.5 to 0.5)
	static int jitterPhaseCount(uint32_t renderWidth, uint32_t outputWidth);
	static void jitterOffset(int index, int phaseCount, float& x, float& y);

private:
	struct Impl;
	Impl* _impl;
};
