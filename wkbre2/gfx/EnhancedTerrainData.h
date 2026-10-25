// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#pragma once

#include "renderer.h"
#include "../util/vecmat.h"
#include <cstdint>
#include <map>
#include <vector>

struct Terrain;
struct TerrainTexture;
struct TextureCache;

// CPU-side data shared by the enhanced terrain renderers (Direct3D 11 and Diligent).

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
