// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#pragma once

#include "SceneRenderer.h"

// D3D11EnhancedSceneRenderer on Diligent (Direct3D 12 / Vulkan): same look, same batching.
struct DiligentEnhancedSceneRenderer : SceneRenderer
{
	DiligentEnhancedSceneRenderer(IRenderer* gfx, Scene* scene, Camera* camera) : SceneRenderer(gfx, scene, camera) { init(); }
	~DiligentEnhancedSceneRenderer();
	void init();
	void render() override;
private:
	struct Impl;
	Impl* _impl;
};
