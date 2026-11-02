// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#pragma once

#include "SceneRenderer.h"

// Lit, instanced meshes on Diligent (Direct3D 12 / Vulkan); a port of the former
// D3D11EnhancedSceneRenderer with the same look and batching.
struct DiligentEnhancedSceneRenderer : SceneRenderer
{
	DiligentEnhancedSceneRenderer(IRenderer* gfx, Scene* scene, Camera* camera) : SceneRenderer(gfx, scene, camera) { init(); }
	~DiligentEnhancedSceneRenderer();
	void init();
	void render() override;
	void prewarm(Model* model) override;
private:
	struct Impl;
	Impl* _impl;
};
