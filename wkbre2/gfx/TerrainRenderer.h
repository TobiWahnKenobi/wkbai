// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#pragma once

struct IRenderer;
struct Terrain;
struct Camera;

struct TerrainRenderer {
	IRenderer *gfx;
	Terrain *terrain;
	Camera *camera;
	TerrainRenderer(IRenderer *gfx, Terrain *terrain, Camera *camera) : gfx(gfx), terrain(terrain), camera(camera) {}
	virtual ~TerrainRenderer() {}
	virtual void render() = 0;
	// Bump mapping switch of the renderers that have one (shown in the debug window)
	virtual bool* bumpMapping() { return nullptr; }
	// The terrain's heights changed (the map editor): the renderers that keep their own copy of
	// the mesh update it; returns false when the renderer has to be made again instead
	virtual bool updateHeights() { return true; }
	// The tiles' textures, turns or flips changed (the map editor), as updateHeights
	virtual bool updateTiles() { return true; }
};