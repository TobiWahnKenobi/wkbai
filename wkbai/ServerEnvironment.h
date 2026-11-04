// wkbai - the server's day and night, seasons and weather (wkbai's own; off by default)
// Licensed under the GNU General Public License 3

#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>
#include "Environment.h"

struct Server;
struct ServerGameObject;
struct GameObjBlueprint;
struct NetLink;

// Simulated once a game second from Server::simulate, with the simulation's random numbers
// only (g_simRandom), and only while a setting is on: with every setting off nothing here runs
// and the game is the same as before.
struct ServerEnvironment {
	EnvironmentSettings settings;
	EnvironmentState state;
	bool enabled() const { return settings.active(); }

	// once a game second (and at once at the start)
	void update(Server& server);
	// the state to every client (or one: a guest joining the running game)
	void send(Server& server, NetLink* only = nullptr);
	// the ground's speed factor for a unit (1 when off; boats keep theirs)
	float speedFactorFor(const ServerGameObject* obj) const;

	// a save game's side file (<save>.env.txt): the settings and the state, read back as is
	std::string saveText() const;
	void loadText(const std::string& text);
	void reset() { *this = ServerEnvironment(); }

	// for the tests: what happened ("AITEST environment" lines)
	struct Stats { int frozen = 0, thirst = 0, drowned = 0, lightningUnits = 0, lightningBuildings = 0, lightningGround = 0, floodedBuildings = 0; float floodDamage = 0;
		size_t floodTiles = 0, floodedTiles = 0; } stats;
	// what happens: frozen units, strikes (for the tests' census)
	bool trace = false;
	// tests ("environmentForce": { "weather": <EnvWeather>, "waterRise": <m> }): that weather
	// always, the water's level heading there; -1 / a NaN: none
	int forceWeather = -1;
	float forceWaterRise = -1000.0f;

private:
	float nextUpdate = -1.0f;
	float weatherEnds = 0.0f, nextLightning = 0.0f;
	int resolvedYear = -1;
	int resolvedSeverity[4] = { 1, 1, 1, 1 };
	float temperatureTarget = 12.0f;
	bool started = false;
	// each unit's cold and heat exposure (by id: in order, saved)
	struct Exposure { float cold = 0.0f, heat = 0.0f; };
	std::map<uint32_t, Exposure> exposure;
	// the land tiles near water that can flood: playable tile index, the water's level, the ground's
	struct FloodTile { int tile; float water, ground; };
	std::vector<FloodTile> floodTiles;
	bool floodPrepared = false;
	int simTraceCountdown = 0;
	// blueprints by kind, worked out once
	std::map<const GameObjBlueprint*, uint8_t> kinds;
	enum Kind : uint8_t { K_OTHER = 0, K_SHELTER = 1, K_LIVING = 2, K_MACHINE = 3, K_FOUNDATION = 4 };
	uint8_t kindOf(const GameObjBlueprint* bp);

	void pickWeather(Server& server, float now);
	void strikeLightning(Server& server);
	void applyExposure(Server& server);
	void applyFloods(Server& server);
	void prepareFloods(Server& server);
	void damage(Server& server, ServerGameObject* obj, float fraction);
};
