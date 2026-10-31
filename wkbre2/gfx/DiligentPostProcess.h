// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#pragma once

#include "RenderDevice.h"
#include "DeviceContext.h"
#include "RefCntAutoPtr.hpp"
#include "../util/vecmat.h"

// Post-processing of the 3D scene (enhanced graphics on Diligent; the original has none):
// screen-space ambient occlusion from the depth buffer, and bloom. The scene is drawn into this
// object's colour and depth targets (same formats as the swap chain, so every pipeline fits), then
// apply() writes the result into the back buffer, where the HUD is drawn on top.
struct DiligentPostProcess {
	struct Settings {
		bool ssao = true, bloom = true;
		float ssaoRadius = 2.5f;    // world units
		float ssaoStrength = 0.7f;  // 0: none, 1: fully dark where occluded
		float bloomThreshold = 0.72f, bloomIntensity = 0.35f;
		bool fxaa = true;
	};

	DiligentPostProcess(Diligent::IRenderDevice* device, Diligent::TEXTURE_FORMAT colorFormat, Diligent::TEXTURE_FORMAT depthFormat);
	~DiligentPostProcess();
	// the targets at this size (made again when it changed)
	void resize(uint32_t width, uint32_t height);
	Diligent::ITextureView* colorRTV() const;
	Diligent::ITextureView* depthDSV() const;
	// projection: the camera's (row vectors, D3D style: z_clip = z P33 + P43, w = z)
	void apply(Diligent::IDeviceContext* ctx, Diligent::ITextureView* backBufferRTV, const Matrix& projection, const Settings& settings);

private:
	struct Impl;
	Impl* _impl;
};
