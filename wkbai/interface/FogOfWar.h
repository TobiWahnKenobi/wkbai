// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <vector>
#include "../client.h"

// The fog of war, as the original shows it: the ground and what stands on it are darker where
// none of the player's (or its allies') characters and buildings sees, and enemy characters
// there are hidden. Per 5 m tile of the playable area, from each viewer's Actual Sight Range
// (with the original's height term, CommonGameObject::getItem) rounded to whole tiles, as
// WKB.exe's sighting (0x566D70); the edge fades over one tile.
// Enemy buildings are shown only as the player last saw them: never-seen ones not at all, and
// seen ones out of sight as they were then (built, a foundation, or since destroyed), until
// their ground comes into sight again. Each client keeps this for its own player, so a guest
// sees no more than the host.
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
	// whether a building there is in sight: any tile around its centre, as a unit seeing the
	// edge of a manor sees the manor
	bool isBuildingSeen(const Vector3& position) const;
	// an enemy building (not the neutral player's), whose live object is hidden out of sight
	bool isRemembered(Client* client, CommonGameObject* obj) const;

	// The enemy buildings as last seen, by object id: copies outside the client's object tree,
	// with their look then (position, appearance, animation, colour) and no parent, never
	// selectable. Drawn and put on the minimap where their ground is out of sight.
	std::map<uint32_t, std::unique_ptr<ClientGameObject>> remembered;
	// test runs: more circles seen, as (centre, radius in metres)
	std::vector<std::pair<Vector3, float>> testViewers;

private:
	uint64_t lastUpdate = 0;
	// the level and player the memory is for; another game starts it afresh
	const void* memoryLevel = nullptr;
	const void* memoryPlayer = nullptr;
	void updateMemory(Client* client);
};
