// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include "util/vecmat.h"
#include "TrnTextureDb.h"
#include "util/DynArray.h"
#include <filesystem>

struct BitReader
{
	uint8_t *bytepos;
	unsigned int bitpos;

	int readbit()
	{
		int a;
		if(bitpos >= 8) {bitpos = 0; bytepos++;}
		a = ((*bytepos) & (1<<bitpos))?1:0;
		bitpos++;
		return a;
	}
	int readnb(int b)
	{
		int a = 0;
		for(int i = 0; i < b; i++)
			a |= readbit() << i;
		return a;
	}

	BitReader(void *data) : bytepos((uint8_t*)data), bitpos(0) {}
};

struct TerrainTexture;

struct TerrainTile {
	TerrainTexture *texture;
	unsigned int rot : 2, xflip : 1, zflip : 1;
	unsigned int x, z;
	float waterLevel = -1.0f; bool fullOfWater = false;
	//unsigned int lakeId; // when removing lake, do a full autofill again.
};

struct Terrain
{
	typedef TerrainTile Tile;

	int width, height, edge;
	float scale;
	uint32_t fogColor, sunColor;
	Vector3 sunVector;
	std::wstring texDbPath, skyTextureDirectory, filePath;
	std::vector<Vector3> lakes;

	TerrainTextureDatabase texDb;
	DynArray<uint8_t> vertices;
	Tile *tiles;

	// Minimap image of the playable area (row 0 = north edge), RGBA; empty if the map has none
	std::vector<uint8_t> minimapRGBA;
	int minimapWidth = 0, minimapHeight = 0;
	// A 128 x 128 RGB minimap of the playable area drawn from the terrain, as the BCM's: each
	// tile's texture's mean colour, lit by the sun on its slope, water tinted. flipX/flipZ: the
	// orientation (test 33 found the BCM's by comparing with the maps' own minimaps).
	std::vector<uint8_t> drawMinimap(bool flipX = false, bool flipZ = true) const;

	std::pair<int, int> getNumPlayableTiles() const { return { width - 2 * edge, height - 2 * edge }; }
	std::pair<float, float> getPlayableArea() const { return { 5.0f * (width - 2 * edge), 5.0f * (height - 2 * edge) }; }

	inline float getVertex(int x, int z) const
	{
		return heightSteps(z * (width + 1) + x) * scale;
	}
	// a vertex's height in steps, with its fraction (wkbai maps: finer than the BCM's whole steps)
	inline float heightSteps(int i) const
	{
		return heightFraction.empty() ? (float)vertices[i] : vertices[i] + heightFraction[i] * (1.0f / 256.0f);
	}
	void setHeightSteps(int i, float steps);
	// per vertex, the fraction of a step above `vertices` in 256ths; empty: none (as every BCM)
	std::vector<uint8_t> heightFraction;
	// <map>.wkbmap, what the BCM can't hold: finer heights, more than 63 lakes
	void readMapExtras(const char* filename);
	bool needsMapExtras() const;
	bool writeMapExtras(const std::filesystem::path& path) const;

	float getHeight(float x, float z) const;
	// the smooth surface the tessellated terrain is drawn with: a Catmull-Rom spline through the
	// vertex heights (exact at the vertices), as the terrain renderer's SmoothHeight
	float getSmoothHeight(float x, float z) const;
	float getHeightEx(float x, float z, bool floatOnWater) const;

	Vector3 getNormal(int x, int z) const;

	const Tile* getTile(int tx, int tz) const { return (tx >= 0 && tx < width && tz >= 0 && tz < height) ? &tiles[tz * width + tx] : nullptr; }
	const Tile* getPlayableTile(int tx, int tz) const { return getTile(tx + edge, tz + edge); }

	void freeArrays() {
		vertices.resize(0);
		if (tiles) delete[] tiles;
	}

	void createEmpty(int newWidth, int newHeight);

	static int GetMaxBits(int x);

	void readFromFile(const char* filename);
	void readBCM(const char *filename);
	// The BCM as read, for the map editor: writeBCM writes that file again with the current tiles
	// and heights (everything else unchanged). Its texture group and id tables, where they start
	// (bits from the file's start), the (group, id) list after the tiles, and where the heights
	// start; textures painted that the tables lack are appended to them
	std::vector<uint8_t> bcmData;
	uint64_t bcmLakesBitOffset = 0, bcmTablesBitOffset = 0, bcmHeightBitOffset = 0;
	size_t bcmMinimapOffset = 0; // the 128 x 128 RGB minimap's first byte
	std::vector<uint8_t> lakeExtraBits; // per lake, the 2 bits after its point (unknown; 0 for new lakes)
	std::vector<TerrainTextureGroup*> bcmGroups;
	std::vector<int> bcmIds;
	uint32_t bcmExtraConstant = 0x62;
	std::vector<std::pair<TerrainTextureGroup*, int>> bcmExtra;
	Tile* getTileMutable(int tx, int tz) { return (tx >= 0 && tx < width && tz >= 0 && tz < height) ? &tiles[tz * width + tx] : nullptr; }
	// newMinimap: the minimap drawn again from the terrain (the map editor), else as read
	bool writeBCM(const std::filesystem::path& path, bool newMinimap = false) const;
	// a new map of another size: playable tiles (plus this map's edge on each side), flat at
	// height steps, one texture, no lakes; header (texture database, scale, sun, fog, sky) as this
	// map's, the minimap one colour (this map's minimap's mean)
	bool writeBlankBCM(const std::filesystem::path& path, int playableWidth, int playableHeight, const TerrainTexture* texture, uint8_t heightSteps) const;
	void readSNR(const char* filename);
	void readTRN(const char* filename);

	void floodfillWater();
	// the water again from the lakes (the map editor added, moved or removed one)
	void refillWater();
};
		