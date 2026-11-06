// wkbai - the server's day and night, seasons and weather (wkbai's own; off by default)
// Licensed under the GNU General Public License 3

#pragma once

#include <array>
#include <map>
#include <set>
#include <string>
#include <vector>
#include "Environment.h"
#include "GameObjectRef.h"

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
	bool skyToSend() const { return !skySent; } // (with every setting off: the game's sky not sent yet)

	// once a game second (and at once at the start)
	void update(Server& server);
	// the state to every client (or one: a guest joining the running game)
	void send(Server& server, NetLink* only = nullptr);
	// the ground's speed factor for a unit (1 when off; boats keep theirs)
	float speedFactorFor(const ServerGameObject* obj) const;
	// winter: the fields give no food (the farming equation, values.cpp ValueEquationResult)
	bool fieldsFallow() const { return settings.seasons && state.season == 3; }
	// the farmers' food support in winter ("Support by Total Farmers", Economic Manager.cpp: what
	// the people eat beyond it comes out of the store): half when gentle, a quarter normally,
	// none when harsh; 1 the rest of the year
	static float WinterSupport(int severity) { return severity <= 0 ? 0.5f : severity == 1 ? 0.25f : 0.0f; }
	float supportScale() const { return fieldsFallow() ? WinterSupport(state.severity) : 1.0f; }
	// for the AI's winter store: the seconds of winter still to come once it is near (from the
	// middle of autumn: all of it), and that winter's severity; 0 the rest of the year
	float winterAhead(float now, int* severity) const;
	// wildfires (heatwaves and droughts): how likely any fire is to spread to the land (0 the rest
	// of the time); a flaming missile came down here (MissileTask: on a unit, a building or the
	// ground): in the heat it may set the land alight, a large fire at once
	float fireRisk() const;
	void fireLanded(Server& server, const Vector3& pos, bool sure = false);

	// ---- for the AI (AIWeather.cpp): what the weather does, asked without random numbers ----
	// the felt temperatures (the exposure's): below 2 C the cold exposure grows, below -4 C it
	// harms; above 30 C the heat exposure grows, above 34 C it harms
	float coldFeel() const;
	float heatFeel() const;
	// a unit's cold and heat exposure (0-200; over 100 it is harmed)
	float coldExposure(uint32_t id) const { auto it = exposure.find(id); return it != exposure.end() ? it->second.cold : 0.0f; }
	float heatExposure(uint32_t id) const { auto it = exposure.find(id); return it != exposure.end() ? it->second.heat : 0.0f; }
	// the shelters (villages, manors, keeps, castles, palaces, fully built) as of the last update:
	// their player's id and place; within SHELTER_RADIUS of one's own or an ally's no one freezes
	static constexpr float SHELTER_RADIUS = 45.0f;
	struct ShelterSpot { uint32_t player; Vector3 pos; };
	const std::vector<ShelterSpot>& shelterSpots() const { return shelters; }
	// whether the cold and the heat touch this blueprint (the living, not machines or the undead)
	bool feelsWeather(const GameObjBlueprint* bp) const { return kindOf(bp) == K_LIVING; }
	bool isUndead(const GameObjBlueprint* bp) const { return kindOf(bp) == K_UNDEAD; }
	// water too deep to wade at a place (the ice over it breaks below ICE_BREAKS)
	bool onDeepWater(const Vector3& p) const;
	// how deep the flood water would stand at a place with the water risen by `rise` metres (0: no
	// land that floods there)
	float floodDepthAt(const Vector3& p, float rise) const;
	// where the water's level heads now (the weather's, metres)
	float waterTarget() const;
	// the burning land's patches
	std::vector<Vector3> firePositions() const;

	// a save game's side file (<save>.env.txt): the settings and the state, read back as is
	std::string saveText() const;
	void loadText(const std::string& text);
	void reset() { *this = ServerEnvironment(); }

	// for the tests: what happened ("AITEST environment" lines)
	struct Stats { int frozen = 0, thirst = 0, drowned = 0, lightningUnits = 0, lightningBuildings = 0, lightningGround = 0, floodedBuildings = 0; float floodDamage = 0;
		size_t floodTiles = 0, floodedTiles = 0; int onIceMost = 0, onIceSeconds = 0, fallowHarvests = 0;
		int wildfires = 0, fireCells = 0, fireCellsMost = 0, fireDeaths = 0, buildingsFired = 0, fromMissiles = 0, fromBuildings = 0, spontaneous = 0;
		// the deaths by player id: 0 frozen, 1 thirst, 2 drowned (floods and the ice), 3 fire, 4 lightning
		std::map<uint32_t, std::array<int, 5>> deathsBy; } stats;
	// what happens: frozen units, strikes (for the tests' census)
	bool trace = false;
	// tests ("environmentForce": { "weather": <EnvWeather>, "waterRise": <m> }): that weather
	// always, the water's level heading there; -1 / a NaN: none
	int forceWeather = -1;
	float forceWaterRise = -1000.0f;
	// tests ("environmentForce": { "fireAt": [x, z, after s] }): a flaming missile comes down there
	// once that many seconds in (the screenshots' fire)
	float forceFireX = -1, forceFireZ = -1, forceFireAt = -1;
	size_t charredCount() const { return charred.size(); }
	// the debug panel (F9, NETSRVMSG_ENVIRONMENT_DEBUG): a setting (EnvironmentSettings::apply's
	// names), or "hour" (0-24: the clock jumps there), "season" (0-4, spring 0: the year jumps
	// there), "weather" (an EnvWeather held from now on, -1: chosen again as usual); the clients
	// are told at once
	void debugSet(Server& server, const std::string& name, float value);

private:
	float nextUpdate = -1.0f;
	float weatherEnds = 0.0f, nextLightning = 0.0f;
	int resolvedYear = -1;
	int resolvedSeverity[4] = { 1, 1, 1, 1 };
	float temperatureTarget = 12.0f;
	bool started = false;
	bool snapTemperature = false; // the debug panel's jump: the temperature at its target at once
	bool skySent = false; // (without day and night and the seasons: the clients sent the sky)
	// each unit's cold and heat exposure (by id: in order, saved)
	struct Exposure { float cold = 0.0f, heat = 0.0f; };
	std::map<uint32_t, Exposure> exposure;
	// the land tiles near water that can flood: playable tile index, the water's level, the ground's
	struct FloodTile { int tile; float water, ground; };
	std::vector<FloodTile> floodTiles;
	// the water too deep to wade (MovementController::isTerrainBlocked): who is on it when the
	// ice breaks goes under
	std::vector<int> deepTiles;
	// the same as a grid (playable tiles, row order), and each tile's index in floodTiles (-1: none)
	int gridW = 0, gridH = 0;
	std::vector<uint8_t> deepGrid;
	std::vector<int> floodIndex;
	std::vector<ShelterSpot> shelters;
	void countDeath(ServerGameObject* unit, int cause);
	bool floodPrepared = false;
	int simTraceCountdown = 0;
	// blueprints by kind, worked out once
	mutable std::map<const GameObjBlueprint*, uint8_t> kinds;
	// K_UNDEAD: the Undead Legion, the Risen Undead Legion, the Gibbering Horde: no cold or heat,
	// but mud slows them to a crawl and water takes them sooner
	enum Kind : uint8_t { K_OTHER = 0, K_SHELTER = 1, K_LIVING = 2, K_MACHINE = 3, K_FOUNDATION = 4, K_UNDEAD = 5 };
	uint8_t kindOf(const GameObjBlueprint* bp) const;

	// the burning land: each patch a "Burning Object" (the Firestorm's flames and sound), spreading
	// over dry ground, hurting who is in it, setting buildings alight
	// gen: steps from where the fire started (it spreads no further than about 12 * the risk);
	// burnt: the tiles that burned (they don't burn again until the heat is over)
	struct Wildfire { Vector3 pos; float born = 0, life = 0; SrvGORef visual; int gen = 0; };
	std::vector<Wildfire> fires;
	std::set<int> burnt;
	// the trees and bushes a fire burned (by id; for good, saved): the client draws them charred,
	// bare of leaves; charredSent: how many the clients were last sent
	std::set<uint32_t> charred;
	std::vector<uint32_t> charredOrder; // the same, in the order they burned
	size_t charredSent = 0;
	void sendCharred(Server& server, NetLink* only);
	void charTrees(Server& server, const Vector3& at);
	void updateFires(Server& server);
	bool igniteAt(Server& server, const Vector3& pos, float life, int gen = 0);
	float fuelAt(Server& server, const Vector3& pos);
	void pickWeather(Server& server, float now);
	void strikeLightning(Server& server);
	void applyExposure(Server& server);
	void applyFloods(Server& server);
	void prepareFloods(Server& server);
	void damage(Server& server, ServerGameObject* obj, float fraction);
};
