// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#pragma once

#include <vector>
#include "../ExtendMap.h"

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
	// The water changed (lakes added, moved or removed), as updateHeights
	virtual bool updateWater() { return true; }
	// wkbai's Extend map: the tiles the drawn terrain adds on each side of the map (0: none)
	virtual int extendedTiles() const { return 0; }
	// and the trees it grows there (null: none)
	virtual const std::vector<ExtendedTree>* extendedTrees() const { return nullptr; }
	// the far rings beyond (ExtendMapFarRings), and how far they reach beyond it (world units)
	virtual int extendedFarRings() const { return 0; }
	virtual float extendedFarReach() const { return 0.0f; }
	// and the far rings themselves (their reach and the ground beyond them; null: none)
	virtual const FarTerrain* extendedFar() const { return nullptr; }
};