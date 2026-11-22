// wkbai - WKB.exe's water overlay terrain (the RE repo's docs/re/combat-smaller-items.md, section
// 11: 0x60b410 sorts each tile against its four corners, 0x60b580 gives it the overlay terrain).
// A tile whose water surface lies 0-1.6 m above every corner is "Shallow Water", one deeper than
// 1.6 m somewhere "Water"; a tile with a corner above the surface (or no water) keeps its texture's
// terrain. The original maps paint their sea beds with sand, cliffs and grass (3 Isles: SAND1 under
// 7,500 sea tiles), so without the overlay no ship could stand on any original sea.
//
// wkbai applies it where a type's ground test names a water terrain: the boats (Seafaring Unit:
// Shallow Water and Water only) and the footprint tiles whose terrain list has one (the dock's
// water rows). The land units keep their test (the texture, and deep water by its depth), which
// gives the same for deep water; shallow water by texture for them is left as it was.
#pragma once

#include "terrain.h"
#include <algorithm>

// 0: no overlay (the texture's terrain), 1: "Shallow Water", 2: "Water"; playable tile coordinates
inline int WaterOverlayClass(const Terrain* t, int px, int pz)
{
	const TerrainTile* tile = t ? t->getPlayableTile(px, pz) : nullptr;
	if (!tile || !tile->fullOfWater)
		return 0;
	const float h[4] = {
		t->getVertex(t->edge + px, t->edge + pz), t->getVertex(t->edge + px + 1, t->edge + pz),
		t->getVertex(t->edge + px, t->edge + pz + 1), t->getVertex(t->edge + px + 1, t->edge + pz + 1) };
	const float hi = std::max({ h[0], h[1], h[2], h[3] }), lo = std::min({ h[0], h[1], h[2], h[3] });
	if (tile->waterLevel < hi)
		return 0;
	return tile->waterLevel - lo > 1.6f ? 2 : 1;
}
