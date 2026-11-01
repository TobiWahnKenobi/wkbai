// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#pragma once

#include <vector>
#include "../util/vecmat.h"

struct Terrain;
struct Camera;
struct DiligentGfx;
namespace Diligent { struct ITextureView; }

// Waterfalls found in the map, with no map editing (the original has none). Two kinds:
//  - falls: where a lake or tarn ends at a low rim (at most a few units above it, at its lowest
//    point) and beyond it the ground drops steeply, within 5 tiles, to water at least
//    WATERFALL_MIN_DROP lower, the water pours over, arcs out from the crest and runs down into
//    the lower water (6 Player Tribes Unite's tarn over its cliff);
//  - cascades: where a painted river (the map's river textures, which most of the original's
//    rivers only are) runs down steep ground, white water over it, with mist and spray at its
//    foot and foam where it runs into a lake (2 Player 4 Peaks).

// a drop of at least this much between two waters is a waterfall (smaller steps are smoothed)
constexpr float WATERFALL_MIN_DROP = 1.5f;

// The water surface's height at a tile corner for a tile at this level: the mean level of the
// water tiles around the corner at about the same level (within WATERFALL_MIN_DROP), so that
// waters at different heights keep a clean step where they meet (the waterfall's)
float WaterLevelAtCorner(const Terrain* terrain, int x, int z, float level);

// One fall: a run of tile edges along a line, falling the same way
struct Waterfall {
	int dx = 0, dz = 0;          // the direction the water falls towards (one axis)
	float x0 = 0, z0 = 0;        // the run's first corner (world x, z, on the tile edges)
	int edges = 0;               // its length in tile edges (5 units each), along +x or +z
	std::vector<float> top;      // the upper water's level at each of the edges + 1 corners
	std::vector<float> lip;      // the ground at each corner (the rim the water pours over)
	float bottom = 0;            // the lower water's level
	float reach = 0;             // how far from the edge the lower water starts (world units)
};

std::vector<Waterfall> FindWaterfalls(const Terrain* terrain);

// The waterfalls drawn after the water (DiligentEnhancedTerrainRenderer), by
// RenderSettings::waterQuality: none on Off and Original; Low a falling sheet of streaked water
// with foam at its foot, and the cascades; Medium also a foam patch on the lower water and a
// little mist; High the sheet refracting what is behind it, tearing into strands, finer
// streaks, more mist and some spray; Ultra the finest noise, the sheet swaying, the foam ringed
// by spreading ripples, full mist and spray.
struct DiligentWaterfalls {
	DiligentWaterfalls(DiligentGfx* gfx, const Terrain* terrain);
	~DiligentWaterfalls();
	// the waterfalls found again (the water or the heights changed)
	void rebuild();
	size_t count() const;
	// scene, depth: the frame behind the water and its depth (null where there are none)
	void render(const Camera* camera, Diligent::ITextureView* scene, Diligent::ITextureView* depth);

private:
	struct Impl;
	Impl* _impl;
};
