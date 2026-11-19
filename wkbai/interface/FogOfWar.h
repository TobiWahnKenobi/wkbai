// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
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
	// as drawn: the same with drawPad tiles more on each side (Extend map's land: the sight
	// circles reach over the border, the land beyond unseen; the game itself only knows tiles)
	std::vector<uint8_t> drawTiles;
	uint32_t drawWidth = 0, drawHeight = 0;
	int drawPad = 0; // set by the client interface

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

	// The trees as last seen. At the start the player knows every tree as it stands; from then
	// on a tree changes for it (felled, falling, used up and gone, a new or regrown tree) only
	// where it or an ally sharing its sight sees it. Copy on write: an unchanged tree costs
	// nothing (the live object is what the player remembers); the client calls beforeTreeChange
	// before a server message changes a tree, and a tree out of sight is copied then, as it
	// looked (treeGhosts, drawn and on the minimap instead of the live tree until its ground is
	// seen). A tree that comes into being out of sight is unknown (treeUnknown: not drawn) until
	// seen. Only what the player sees: the server's game is the same.
	// set by the client interface: the fog of war in play (off: nothing kept, the live trees drawn)
	bool memoryOn = false;
	std::unordered_map<uint32_t, std::unique_ptr<ClientGameObject>> treeGhosts;
	std::unordered_set<uint32_t> treeUnknown;
	// whether the object is a tree, standing, fallen, a stump or growing (scenery a player
	// remembers): a container or prop with "Tree" in its type's name (not the birds), or the
	// game set's "I am a tree" (the fallen cedars and willows)
	bool isTree(Client* client, const CommonGameObject* obj) const;
	// the live tree is drawn as remembered instead (a copy) or not at all (unknown)
	bool treeHidden(const ClientGameObject* obj) const {
		return (!treeGhosts.empty() || !treeUnknown.empty()) && treeHiddenLookUp(obj);
	}
	bool treeHiddenLookUp(const ClientGameObject* obj) const;
	// a server message is about to change this object (its look, items, type, or remove it)
	void beforeTreeChange(Client* client, ClientGameObject* obj);
	// a new object (or one converted into another type)
	void treeCreated(Client* client, ClientGameObject* obj);
	// a sound or special effect of this object is not played: a tree out of sight
	bool hidesTreeEffect(Client* client, ClientGameObject* obj) const;
	// a new game (NETCLIMSG_GAME_SET): the memory starts afresh at its first update
	void resetTrees();
	// the memory as a save keeps it (the treeMemory section, by player: Server::treeMemory):
	// "ghost <id> <class> "<type>" <subtype> <appearance> <position> <orientation> <scale>
	// <tilted> <ground up> <animation> <variant> <clamped> <frame ms>" and "unknown <id>" lines
	std::string treeMemoryText(Client* client) const;
	void loadTreeMemory(Client* client, const std::string& text);
	// whether the tree memory has begun for this game (its first update with the fog's tiles)
	bool treesStarted() const { return treesOn; }

private:
	bool treesOn = false;
	bool treesDirty = false; // changed since sent to the local server
	int treeItem = -1, workedItem = -1; // "I am a tree", "I'm Being Worked at"
	const void* treeItemsOf = nullptr;
	void lookUpTreeItems(Client* client);
	void updateTrees(Client* client);
	// a copy of the object as it looks now, outside the object tree
	static void copyLook(Client* client, CommonGameObject* from, std::unique_ptr<ClientGameObject>& copy);

	uint64_t lastUpdate = 0;
	uint32_t nightRebuild = 0; // the night's lit blocks: built again every 4th update
	// the level and player the memory is for; another game starts it afresh
	const void* memoryLevel = nullptr;
	const void* memoryPlayer = nullptr;
	void updateMemory(Client* client);
};
