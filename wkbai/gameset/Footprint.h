// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#pragma once

#include <vector>

struct GameSet;
struct GSFileParser;

struct Footprint {
	float originX = 0.0f, originZ = 0.0f;
	// MAX_FOUNDATION_HEIGHT_DIFFERENTIAL, metres; 0: no limit
	float maxHeightDifferential = 0.0f;
	struct TileOffset {
		int offsetX, offsetZ;
		int mode = 0;
		// DEFORM_TERRAIN: FLATTEN (the default) or LEAVE_AS_IS
		bool flatten = true;
		// USE_TERRAIN_BLUEPRINT_LIST: per terrain (index in GameSet::terrains), whether the tile
		// may stand on it (GameSet::terrainBlueprintLists); null: on any
		const std::vector<bool>* terrains = nullptr;
		TileOffset(int offsetX, int offsetZ) : offsetX(offsetX), offsetZ(offsetZ) {}
		std::pair<int, int> rotate(float angle) const;
	};
	std::vector<TileOffset> tiles;

	int wallOriginDefaultMinX = 0, wallOriginDefaultMinY = 0;
	int wallOriginDefaultMaxX = 0, wallOriginDefaultMaxY = 0;

	void parse(GSFileParser& gsf, GameSet& gs);
	std::pair<float, float> rotateOrigin(float angle) const;
};