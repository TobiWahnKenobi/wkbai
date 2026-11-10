// wkbai - Extend map: the land continued beyond a map's edge (a graphics setting)
// Licensed under the GNU General Public License 3

#pragma once

#include <cstdint>
#include <memory>
#include <vector>

struct Terrain;

// The extension is purely visual. The terrain renderer draws a client-only, bigger copy of the
// map: extendedTiles more tiles on every side and an edge that much wider, so every world
// coordinate stays where it was (a tile's place is (x - edge) * 5). The map's own Terrain, which
// the client and a local server share, is never written; the simulation, the path finding, the
// minimap and the replays never see the copy. Plan: RE repo docs/plans/extend-map.md.
//
// Phase 1 (the near ring): the heights continue the mesh edge's own outward slope, fading to
// level, with a gentle noise seeded by the map file's hash; the water at the edge (seas and
// rivers) carries on at its level; the tiles repeat the edge's (a material's tiles turned and
// varied at random, transitions as they are).

// the tiles added on each side at RenderSettings::extendMap (0: off), and the far rings
// beyond them (0: none; Far 4, out to about 3.6 km beyond the near ring; Horizon 7, about 30 km)
int ExtendMapTiles(int setting);
int ExtendMapFarRings(int setting);

// The far rings: coarse land around the near ring, each ring twice as coarse as the one inside it
// (10, 20, 40 ... units), stitched to it without cracks; drawn by the terrain renderer's far
// pipeline (PS_Far: the terrain's light and haze on a colour per vertex, the map's materials'
// mean colours). Vertices in world units; normal R10G10B10A2 (n * 0.5 + 0.5); colour RGBA8, A 0
// on water. Blocks: index ranges with their bounding boxes, for culling.
struct FarVertex { float x, y, z; uint32_t normal, colour; };
struct FarBlock { uint32_t first, count; float lo[3], hi[3]; };
struct FarTerrain {
	std::vector<FarVertex> vertices;
	std::vector<uint32_t> indices;
	std::vector<FarBlock> blocks;
	float reach = 0; // world units from the near ring's edge to the last ring's
	// The ground beyond the last ring, out to the true horizon (drawn by the sky under the horizon,
	// DiligentSkyRenderer; the last ring's outer part goes over into it): flat at outerLevel, sea
	// (outerSea) or land of outerColour (0-1, as the vertex colours)
	float outerLevel = 0;
	bool outerSea = false;
	float outerColour[3] = { 0.3f, 0.35f, 0.2f };
};

struct ExtendedTerrainDeleter { void operator()(Terrain* t) const; };
using ExtendedTerrain = std::unique_ptr<Terrain, ExtendedTerrainDeleter>;

// A tree of the extension (drawn by the client with one of the level's own tree models): its
// root in world units, its turn, its size against the model's, a random number to pick the
// model by, and whether it stands in a forest
struct ExtendedTree { float x, y, z, yaw, scale; uint32_t variant; bool forest; };

// The copy of map with n tiles more on each side (deterministic: the same map, the same copy),
// and its trees (if wanted)
ExtendedTerrain BuildExtendedTerrain(const Terrain& map, int n, std::vector<ExtendedTree>* trees = nullptr, int farRings = 0, FarTerrain* far = nullptr);

// The map's changes at run time carried into the copy: its vertices (levelled building sites,
// farms, the editor's heights), its tiles, and its water (lakes, the water design, ice)
void SyncExtendedHeights(const Terrain& map, Terrain& ext);
void SyncExtendedTiles(const Terrain& map, Terrain& ext);
void SyncExtendedWater(const Terrain& map, Terrain& ext);
