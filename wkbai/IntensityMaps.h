// wkbai - intensity maps as WKB.exe's (the RE repo's docs/re/combat-smaller-items.md)

#pragma once

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

struct Server;
struct ServerGameObject;

// A player's intensity maps: an int per tile each. In the WKB game set only the Kingmaker AI's
// player creates any (CREATE_INTENSITY_MAP "Dangerous Buildings" / "Scout Scared Markers"); the
// human players have none, and nobody creates "Threat".
//
// - Writers: an object with UPDATE_INTENSITY_MAP <map> ON_SIGHTING <radius m> <strength> (towers,
//   gatehouses, forts: their sight range, strength 10) is stamped into the map of every player but
//   its own that has the map, once that player has seen it (WKB.exe 0x544d70: the object's
//   "discovered" bits): a filled disc of the radius, the strength added on every tile, no falloff
//   (0x61e580). Here the stamps are kept while the object lives.
// - Users: START_APPLY_MY_PLAYERS_INTENSITY_MAP_TO_PATHFINDING <map> applies its player's map
//   always (the scouts), APPLY_INTERMITTANT_MY_PLAYERS_... only once SWITCH_ON_INTENSITY_MAP turned
//   it on for the unit (the Kingmaker armies do, for their subordinates).
// - The path search adds the summed value of the tiles to their cost (0x5473d0 / 0x548400 /
//   0x577b10); its scale there isn't traced, here a value of 10 costs one straight step.
struct IntensityMaps {
	// once a second: what each player has seen of the writers, and the maps again
	void update(Server* server);
	// SWITCH_ON_INTENSITY_MAP
	void switchOn(ServerGameObject* obj, const std::string& map);
	// SWITCH_OFF_INTENSITY_MAP (mods; WKB.exe 0x5485a0)
	void switchOff(ServerGameObject* obj, const std::string& map);
	// The extra cost per tile (in the search's units: 100 a straight step) for this unit's paths,
	// or null if it applies no map or its maps are empty. Valid until the next update.
	const std::vector<int32_t>* costFor(ServerGameObject* obj);

	// the save game's side file (<save>.intensity.txt)
	std::string saveText() const;
	void loadText(Server* server, const std::string& text);
	void clear();

private:
	struct PlayerMap {
		std::set<uint32_t> discovered; // writers this player has seen
		std::vector<int32_t> grid;      // the map (empty: nothing on it)
	};
	// player id -> map name -> map
	std::map<uint32_t, std::map<std::string, PlayerMap>> players;
	// object id -> the maps switched on for it
	std::map<uint32_t, std::set<std::string>> switchedOn;
	// player id + the maps a unit applies -> their sum as costs, built when first asked for
	std::map<std::pair<uint32_t, std::string>, std::vector<int32_t>> costCache;
	int width = 0, height = 0;
};
