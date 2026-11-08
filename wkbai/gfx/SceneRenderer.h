// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#pragma once

#include <vector>

struct IRenderer;
struct Scene;
struct Camera;
struct Model;

struct SceneRenderer {
	IRenderer* gfx;
	Scene* scene;
	Camera* camera;
	SceneRenderer(IRenderer* gfx, Scene* scene, Camera* camera) : gfx(gfx), scene(scene), camera(camera) {}
	virtual void render() = 0;
	// Creates the model's GPU resources ahead of its first draw (e.g. at level start)
	virtual void prewarm(Model* model) {}
	// the same for many models at once (the work spread over threads where it can be)
	virtual void prewarm(const std::vector<Model*>& models) { for (Model* m : models) prewarm(m); }
};
