// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#pragma once

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
};
