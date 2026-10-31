// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#pragma once

#include <cstdint>
#include <vector>
#include "../util/vecmat.h"

struct Client;
struct CommonGameObject;

// The fog of war, as the original shows it: the ground and what stands on it are darker where
// none of the player's (or its allies') characters and buildings sees, and enemy characters
// there are hidden. Per 5 m tile of the playable area, from each viewer's Actual Sight Range
// (with the original's height term, CommonGameObject::getItem) rounded to whole tiles, as
// WKB.exe's sighting (0x566D70); the edge fades over one tile.
struct FogOfWar {
	// row z, column x; 255 seen, 0 unseen
	std::vector<uint8_t> tiles;
	uint32_t width = 0, height = 0;
	// changes with the tiles (for the texture)
	uint32_t version = 0;

	// worked out again every 250 ms (real time)
	void update(Client* client);
	// whether the player sees there (without a level or fog: everywhere)
	bool isSeen(const Vector3& position) const;
	// whether the object belongs to the player or an ally
	bool isFriendly(Client* client, CommonGameObject* obj) const;

private:
	uint64_t lastUpdate = 0;
};
