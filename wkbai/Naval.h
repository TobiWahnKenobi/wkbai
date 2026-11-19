// wkbai - the AI at sea (not in WKB.exe: its game set has docks, cogs, war boats and the
// passengers' boarding and landing, but no AI script uses any of them, and the dock's button is
// hidden while a Kingmaker AI plays). One naval layer for every AI player: it reads the map (is
// the enemy across water, is water the only way), lays down a dock, trains cogs and war boats,
// ferries armies over (board, sail, land at a safe shore, regroup, attack), fights enemy ships
// and shoots at the coast. The Kingmaker and aggressive AIs use it on its own; a Lua AI through
// ai.naval*() (or on its own, unless it takes the helm); the ministry AI through its Admiralty,
// which sets what to build and hands missions to the ferries. The RE repo's docs/engine/naval-ai.md.
#pragma once

#include "util/vecmat.h"
#include <cstdint>
#include <map>
#include <string>
#include <vector>

struct ServerGameObject;
struct GameObjBlueprint;

namespace Naval {
	// what the map asks of a player: no sea between it and its enemies, a sea between them that is
	// the shorter way (land path over twice the voyage), or a sea that is the only way
	enum Need : int8_t { NEED_UNKNOWN = -1, NEED_NONE = 0, NEED_SHORTER = 1, NEED_ONLY = 2 };

	struct Ferry {
		enum State : uint8_t { BOARDING, SAILING, LANDING, ASHORE, RETURNING, DONE };
		// who sent it: the naval layer itself (the scripts' AIs), a Lua AI, or a ministry mission
		enum Owner : uint8_t { LAYER, LUA, MINISTRY };
		uint32_t id = 0, ship = 0, target = 0;
		Vector3 targetAt, embark, landing;
		std::vector<uint32_t> passengers;
		State state = BOARDING;
		Owner owner = LAYER;
		uint32_t mission = 0; // the ministry's mission
		float since = 0.0f, stateSince = 0.0f, nextNudge = 0.0f;
		int landed = 0; // passengers put ashore
	};

	// one AI player's navy (AIController::naval)
	struct State {
		Need need = NEED_UNKNOWN;
		uint32_t enemy = 0;          // the enemy the need is for (the nearest across water)
		Vector3 home, homeShore, enemyShore;
		float nextCheck = 0.0f, nextThink = 0.0f, nextDockTry = 0.0f, nextVillageTry = 0.0f, nextBuildersCheck = 0.0f;
		bool hadShips = false;
		float start = -1.0f;        // the game time the layer first ran (a map's clock may not start at 0)
		float nextTrace = 0.0f;
		int lastTrainWant = 0, lastPool = -1; // (the trace: 0 nothing wanted, 1 a ship ordered, 2 none could be)      // ships seen once (they are looked after even with no need left)
		uint32_t dock = 0;           // the dock or its foundation
		// what to keep: transports (cogs) and war boats; < 0 the layer's own numbers (by personality)
		int wantTransports = -1, wantWarships = -1;
		// the layer ferries no army of its own (a ministry's Admiralty, or a Lua AI at the helm,
		// ferries its own); it still builds and fights at sea
		bool manual = false;
		std::vector<Ferry> ferries;
		// the peasants sent to build the dock or a village (held from the work orders): id -> (foundation, since)
		std::map<uint32_t, std::pair<uint32_t, float>> builders;
		uint32_t nextFerry = 1;
		float lastFerry = -1.0f;
		// counts for the census and the tests
		int docksLaid = 0, shipsOrdered = 0, warshipsOrdered = 0, ferriesSent = 0, ferriesLanded = 0, unitsLanded = 0;
		int shipAttacks = 0, coastAttacks = 0, villagesLaid = 0;
	};

	// every AI player's step (AIController::update), with "aiNaval" on
	void Update(ServerGameObject* player);
	// whether the unit can't walk to the place (another land region, water between) but a ship of
	// this player's could take it there: the same sea touches the unit's shore and the place's
	bool Overseas(ServerGameObject* player, ServerGameObject* unit, const Vector3& place);
	// The units ferried to the target (an object; or a place, target 0), from the nearest shore to
	// them, landed at a safe shore near it: the ferry's id, 0 when none can go (no free ship, no
	// shore). A MINISTRY or LUA ferry hands the units back once ashore (FerryState ASHORE); the
	// layer's own attacks with them as a wave.
	uint32_t SendFerry(ServerGameObject* player, const std::vector<ServerGameObject*>& units, ServerGameObject* target,
		const Vector3& place, Ferry::Owner owner, uint32_t mission = 0);
	// a ferry's state (DONE for one there is no more of)
	Ferry::State FerryState(ServerGameObject* player, uint32_t ferry);
	const Ferry* FindFerry(ServerGameObject* player, uint32_t ferry);
	// the player's ships (boats: "I am a boat"), its transports (cogs, with room for passengers)
	// and war boats, alive
	std::vector<ServerGameObject*> Ships(ServerGameObject* player);
	bool IsBoat(const ServerGameObject* o);
	bool IsTransport(const ServerGameObject* o);
	// An AI's foundation site where its peasants can walk (not in WKB.exe): a site in another land
	// region than the player's home (an island across the sea) is moved to the nearest valid site
	// round the palace on the home's land, 20 to 250 m out; (0, 0, 0) when there is none. The
	// Kingmaker's first village on 3 Isles went to the "Nearest Farming Marker To My Manor" on
	// another player's island, its builder stood 253 m away on its own, and the scripts' economy
	// never started (WKB.exe refuses AI Generals on the two island maps). Unchanged elsewhere.
	Vector3 HomeSite(ServerGameObject* player, const GameObjBlueprint* foundation, const Vector3& wanted);
	// the land region of the player's home (its palace, else its first building) for a Peasant's
	// movement class (set in cls); -1 when it has none
	int HomeLandRegion(ServerGameObject* player, int* cls);
	// the land regions (a Peasant's class, set in cls) where the player has peasants (its home's
	// too), sorted; looked up again once a game second
	const std::vector<int>& PeasantRegions(ServerGameObject* player, int* cls);
	// a search for a site that starts on land where the player has no peasant starts at its home
	// instead (its palace); whether it was moved
	bool StartAtHomeIfUnreachable(ServerGameObject* player, Vector3& from);
	// whether a place is across the sea from the player's home (another land region, a sea route
	// known: need > NONE)
	bool PlaceOverseas(ServerGameObject* player, const Vector3& place);
	// for the census: one line
	std::string Census(ServerGameObject* player);
	// the type of a land unit for the map's regions (a Spearman, else a Peasant)
	const GameObjBlueprint* LandType();
	const GameObjBlueprint* BoatType();
}
