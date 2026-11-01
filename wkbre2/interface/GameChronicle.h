// wkbai - statistics and a timeline of a game's events, as a client sees them

#pragma once

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>
#include "../util/vecmat.h"
#include "MapRegions.h"

struct Client;
struct FogOfWar;

// Kept by the client interface from the objects the client has (all of them: the fog of war is
// the client's own), so it works the same in single player, for a multiplayer guest and in a
// replay. Statistics: each player's resources and counts every 10 game seconds. Events: battles
// (10 or more fighters of 2 or more sides attacking within 60 m), sieges (battles at buildings),
// raids (fewer than 10 attackers at peasants or farms), settlements founded and destroyed. Each
// event remembers whether this client's player saw it (its place in sight, or its own units in
// it): in a game only those are listed, in a replay all.
struct GameChronicle {
	struct Sample {
		float time;
		float food, wood, gold;
		int peasants, soldiers, buildings;
		int unitsLost, buildingsLost;
	};
	std::map<uint32_t, std::vector<Sample>> samples; // by player id
	std::map<uint32_t, std::string> playerNames;
	std::map<uint32_t, int> playerColours;

	enum class Kind { Battle, Siege, Raid, Founded, Destroyed };
	struct Event {
		Kind kind;
		float time, endTime;
		Vector3 position;
		std::set<uint32_t> players;
		int size = 0; // the most fighters seen at once; settlements: 0
		std::string what; // a settlement's blueprint
		std::string place; // the map region's name (MapRegions)
		bool seen = false;
		// per player: the most of its fighters in it at once, and the units it lost there (those
		// gone within 100 m while it went on), by type
		std::map<uint32_t, int> strength;
		std::map<uint32_t, std::map<std::string, int>> lost;
		std::string title() const; // "Battle of Ergor", "Village founded at Fangorn"
	};
	MapRegions regions;
	std::vector<Event> events;

	// once a frame; the scans themselves run every few game seconds. fog: this client's, or
	// nullptr when everything is seen (no fog of war, a replay)
	void update(Client* client, const FogOfWar* fog);
	static const char* kindName(Kind kind);

private:
	float nextSample = 0.0f, nextScan = 0.0f;
	bool first = true;
	struct Known { uint32_t player; bool building, settlement, foundation; Vector3 position; std::string name; bool fighter = false; };
	std::map<uint32_t, Known> known; // the objects at the last scan
	std::map<uint32_t, int> unitsLost, buildingsLost;
	void scan(Client* client, const FogOfWar* fog, float now);
	void sample(Client* client, float now);
};
