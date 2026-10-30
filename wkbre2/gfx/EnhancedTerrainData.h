// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#pragma once

#include "renderer.h"
#include "../util/vecmat.h"
#include "bitmap.h"
#include <cstdint>
#include <map>
#include <string>
#include <vector>

struct Terrain;
struct TerrainTexture;
struct TextureCache;

// CPU-side data of the enhanced terrain renderer (shared with the former D3D11 version).

// Terrain vertex: normal, tangent and bitangent packed as R10G10B10A2 (n * 0.5 + 0.5); the
// normal's 2-bit alpha is 3 inside the playable area and 0 on the map edge (drawn darker).
struct EnhancedTerrainVertex {
	Vector3 pos;
	uint32_t normal, tangent, bitangent;
	float u, v;
};

struct EnhancedTerrainTexture {
	texture diffuseMap;
	texture normalMap;
};

// Diffuse and normal map of every terrain texture. Warrior Kings has normal maps; Battles has
// bump (height) maps, which are turned into normal maps here.
void LoadEnhancedTerrainTextures(Terrain* terrain, TextureCache* texcache, std::map<TerrainTexture*, EnhancedTerrainTexture>& textures);

// Four vertices per tile, for the whole map, tile (x, z) at 4 * (z * width + x).
std::vector<EnhancedTerrainVertex> BuildEnhancedTerrainVertices(Terrain* terrain);

// The splat-map terrain (RenderSettings::terrainSplat). The original paints each tile with a
// 64-texel piece of a texture atlas, and the texture database's groups are materials ("SAND1")
// and the transitions between two ("SAND1-SAND2"). Here every material becomes one layer of a
// texture array: its four most used tiles on the map in 2 x 2 quadrants (128 texels), of which
// the shader shows one per tile of the ground, picked by a hash of the tile (a material's tiles
// fit side by side, as the maps use them; repeating them 2 x 2 showed as a checkerboard).
// Every tile corner gets a material: a material's tile gives its own to its corners, a
// transition tile gives each corner the one of its two that the texels at that corner are
// closer to in colour (with the tile's turn and flips). Where a tile's four corners agree the
// shader keeps the tile's own picture, so the ground keeps the original's variety; elsewhere it
// blends the corners' materials across the tile, so materials meet smoothly instead of along
// the painted transition tiles.
struct TerrainSplatData {
	static constexpr int LAYER_SIZE = 128;
	std::vector<std::string> names;              // per layer
	std::vector<std::vector<uint8_t>> diffuse;   // per layer, LAYER_SIZE² RGBA8
	std::vector<std::vector<uint8_t>> normal;    // per layer, LAYER_SIZE² RGBA8, as the tiles' normal maps
	std::vector<uint8_t> corners;                // layer per tile corner, (x, z) at z * (width + 1) + x
};
TerrainSplatData BuildTerrainSplat(Terrain* terrain);

// Grass (RenderSettings::grass): short blades scattered over a tile where its picture is green,
// coloured from the picture under them (with the tile's turn and flips), on gentle dry ground
struct GrassBlade {
	float x, y, z;     // root, in world units
	uint32_t colour;   // RGBA8, R in the low byte
	float yaw, height; // the direction it leans in (radians), its height in world units
};
struct TerrainGrassBuilder {
	std::map<std::string, Bitmap> atlases; // the tile pictures, by texture file
	// adds tile (x, z)'s blades, about density per square unit
	void addTile(Terrain* terrain, int x, int z, float density, std::vector<GrassBlade>& out);
};
