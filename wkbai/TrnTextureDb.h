// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#pragma once

#include <vector>
#include <string>
#include <utility>
#include <map>
#include <memory>
#include <array>

struct TerrainTextureGroup;
struct TerrainTexture;

struct TerrainTextureEdge
{
	TerrainTexture *tex;
	unsigned int rot : 2, xflip : 1, zflip : 1;
	// NOTE: Using memcmp with "boolean" types is dangerous!
	bool operator==(TerrainTextureEdge &b) { return tex == b.tex && rot == b.rot && xflip == b.xflip && zflip == b.zflip; }
};

struct TerrainTexture
{
	int id;
	int startx, starty, width, height;
	//texture t;
	//int tfid;
	std::string file;
	TerrainTextureGroup *grp;
	std::vector<TerrainTextureEdge> edges[4];
	// a generated transition's picture (TransitionGen.h): the base group at each picture corner
	// (0 (u0, v0), 1 (u1, v0), 2 (u1, v1), 3 (u0, v1)); empty for the game's own
	std::array<std::string, 4> genCorners;
};

struct TerrainTextureGroup
{
	std::string name;
	std::map<int, std::unique_ptr<TerrainTexture>> textures;
};

struct TerrainTextureDatabase
{
	std::map<std::string, std::unique_ptr<TerrainTextureGroup>> groups;
	std::vector<std::string> textureFilenames;

	void load(const char *filename);
	void translate(const char* ltttFilePath);
	TerrainTextureDatabase() {}
	TerrainTextureDatabase(const char *filename) { load(filename); }

	static std::string getLTTTPath(const std::string& terrainFilePath);
};
