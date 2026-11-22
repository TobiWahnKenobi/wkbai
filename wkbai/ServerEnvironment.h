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
#include "PeasantLodging.h"
#include "Wind.h"

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

	// the wind (Wind.h): simulated once a game second while the lobby's wind is on (whatever the
	// day and night and the seasons), sent to the clients (NETCLIMSG_WIND), saved
	WindState wind;
	bool windOn() const { return settings.wind != 0; }
	// how much it fans the fires: 0 (off, no effects, still air) .. 2.5 (a gale), 1 at 8 m/s
	float windFireFactor() const;
	// the felt cold's and heat's drop from the wind (degrees; 0 without its effects)
	float windChill() const;
	// a boat's speed factor on a leg heading that way (xz): with the wind faster, against it slower,
	// by the angle and the wind's speed (WindBoatFactor; 1 without its effects)
	float windBoatFactor(const Vector3& heading) const;
	void sendWind(Server& server, NetLink* only = nullptr);

	// ---- for the AI (AIWeather.cpp): what the weather does, asked without random numbers ----
	// the felt temperatures (the exposure's): below 2 C the cold exposure grows, below -4 C it
	// harms; above 30 C the heat exposure grows, above 34 C it harms
	float coldFeel() const;
	float heatFeel() const;
	// the day's mean temperature (the season's and the weather's, without the day's swing:
	// EnvironmentState::temperatureC has it), and what the ice and the snow cover answer to: the
	// mean with a quarter (the ice) or half (the snow's thaw) of the swing, as the water and the
	// ground are slow to warm and cool, so a frozen lake does not thaw every sunny afternoon
	float dailyMeanTemperature() const { return temperatureBase; }
	float iceTemperature() const { return temperatureBase + 0.25f * (state.temperatureC - temperatureBase); }
	float meltTemperature() const { return temperatureBase + 0.5f * (state.temperatureC - temperatureBase); }
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

	// ---- the weather on burning buildings (updateBuildingFires) ----
	// The game set's fire (Mechanics\Fire.cpp) burns a building by its "Fire Strength (SI)": the
	// Structural Integrity it loses every 2.9 s (3 when lit, up to 27; it grows by 1.5 whenever the
	// building's Fire Proofing fails a roll, a house's 1 in 5, a castle's 1 in 200). Once a second:
	//  - Rain puts it out: RAIN_DOUSE * rain^1.5 off the strength a second (EnvironmentState::rain:
	//    rain 0.5 0.18/s, a thunderstorm 0.9 0.43/s, heavy rain 1.0 0.5/s): a full fire (27) on a
	//    barracks goes out in about 2.5 minutes of rain and about a minute of heavy rain (test 96:
	//    150 s and 53 s; longer while the rain sets in, and for a house, whose fire grows fastest). Its flames shrink as it does
	//    (one "Building Fire" effect fewer below 19 and 9, the clients' flames by the share left,
	//    NETCLIMSG_FIRE_SHARES), and at 0.5 it is out ("Stop Building Burning", as the peasants').
	//  - Snow on a roof (the snow cover, less what a fire on it melted) slows a fire: at full
	//    cover the strength is held to half the most (SNOW_CAP: half the harm), and the building's
	//    Fire Proofing counts SNOW_PROOFING more (the item "Snow Fire Proofing", redata's "wkbai
	//    patches\Snow and Fire.cpp"): a house's fire grows and spreads to it 4 times less often.
	//    The fire melts its roof's snow: ROOF_MELT + ROOF_MELT_HOT * its strength / 27 a second (a
	//    new fire's roof bare in about 4 minutes, a full fire's in about 1); snow falling covers
	//    it again (ROOF_COVER * the snowfall a second).
	// Lightning still sets buildings alight in a storm; the storm's rain then works against it.
	static constexpr float RAIN_DOUSE = 0.5f, SNOW_CAP = 0.5f, SNOW_PROOFING = 30.0f;
	static constexpr float ROOF_MELT = 0.003f, ROOF_MELT_HOT = 0.012f, ROOF_COVER = 0.01f;
	// a building's roof snow (0..1) and its fire's share left by the weather (1: as it burned, or
	// not burning); tests
	float roofSnow(uint32_t id) const;
	float fireShare(uint32_t id) const;

	// ---- river currents (settings.riverCurrents, with the seasons; applyCurrents) ----
	// The water's flow (BuildWaterFlow: the map's water design, else the shores' shape; its speed
	// faster in floods and rain, EnvWaterFlowFactor) acts on the units in it once a second:
	//  - the water's depth: shallow (up to 0.75 m), normal (0.75-1.5 m: deeper is no land unit's),
	//    flood (all the water and the flooded land while it is risen FLOOD_RISE or more);
	//  - its speed (the flow map's, 0-1, times EnvWaterFlowFactor): slow below FLOW_SLOW, normal
	//    below FLOW_NORMAL, fast. The maps' water flows at 0.2-0.3 where units can wade (its banks
	//    and fords; at most about 0.45 in a river's middle): slow in fair weather, normal in rain
	//    and heavy rain (x1.15, x1.3, more as the water rises), fast in the faster spots then;
	//  - CURRENT_DRAG: how fast (m/s) the water carries them along the flow; CURRENT_HARM: the
	//    share of their health it takes a second (the undead twice that; flooded land deeper than
	//    applyFloods' drowning depth drowns them as before, not on top of it). Shallow water that
	//    flows slowly or normally does nothing.
	//  - Carried, they swim (at SWIM_SHARE of their speed) towards their path's next point, or to
	//    the nearest shore when they have nowhere to go; not in shallow water (they walk on), and
	//    machines don't swim. Each second they walk a leg of 1.5 s with the current and the swim
	//    together (one NETCLIMSG_OBJECT_MOVEMENT_STARTED); out of the water they go on their way.
	//  - A waterway of the map's water design with its own "speed" (WaterDesign::Waterway, m/s):
	//    its water's speed is that, the classes slow below 0.5 m/s, normal below 1.2 m/s, fast
	//    (as flow 0.3, 0.45: designedFlow), the weather's factor on it as on the rest.
	//  - Carried and swimming, a unit plays its "Swim" animation: its model's own (the Scout's, a
	//    mod's) or one made from its walk (SwimAnim.h; the client draws it at the water's surface),
	//    else (no walk) its walk, slower; out of the water an idle one stops, its default again.
	//  - waterHardiness: the war machines (rams, bombards, trebuchets, mangonels, siege towers,
	//    carts, the Infernal Engine ...) sink where the water would carry them (normal depth, a
	//    flood: MACHINE_SINK of their health a second), dragged MACHINE_DRAG of the current's way
	//    as they go under; in shallow water only dragged that much. The huge creatures (the
	//    Behemoth, the large Elementals, the Horde Spewer) drift with the current but can't swim,
	//    and are hurt as any. The otherworldly (the Archangel Michael, Abaddon) walk the bed as
	//    ever: neither carried nor hurt by water (nor drowned in floods or through the ice,
	//    applyFloods, whether or not the currents are on).
	//  - Formations' members, boats, buildings: not carried (the members are hurt, though).
	//  - The path finding (currentsPathCost, MovementController's searches) goes round the water
	//    that harms or carries, CURRENT_PATH_COST more a tile (a straight step 100), for land
	//    units whose way comes within 8 tiles of it; the AI (AIWeather.cpp) walks its units out of
	//    harmful water to the nearest dry land.
	// With the setting off, or without the seasons, nothing of this runs.
	enum WaterDepth : uint8_t { DEPTH_NONE = 0, DEPTH_SHALLOW = 1, DEPTH_NORMAL = 2, DEPTH_FLOOD = 3 };
	static constexpr float FLOOD_RISE = 0.3f;
	static constexpr float FLOW_SLOW = 0.3f, FLOW_NORMAL = 0.45f;
	static constexpr float SWIM_SHARE = 0.5f;
	//                                               slow  normal fast
	static constexpr float CURRENT_DRAG[3][3] = { { 0.0f, 0.0f, 0.4f },     // shallow
	                                              { 0.5f, 1.0f, 1.8f },     // normal
	                                              { 0.6f, 1.2f, 2.0f } };   // flood
	static constexpr float CURRENT_HARM[3][3] = { { 0.0f, 0.0f, 0.0f },
	                                              { 0.0f, 0.005f, 0.015f }, // about 3 min, 1 min
	                                              { 0.03f, 0.03f, 0.03f } };// about 30 s
	static constexpr int CURRENT_PATH_COST[3][3] = { { 0, 0, 60 },
	                                                 { 60, 300, 800 },
	                                                 { 800, 800, 800 } };
	// a waterway's speed set by the map's maker (m/s) as the flow map's scale: 0.5 m/s 0.3 (slow
	// below), 1.2 m/s 0.45 (normal below), on from there 0.2 a m/s
	static float designedFlow(float metresPerSecond);
	// how a unit fares in water: as any (swims), a war machine (sinks, dragged a little), a huge
	// creature (drifts, can't swim), otherworldly (untouched: Michael, Abaddon)
	enum : uint8_t { WATER_ANY = 0, WATER_MACHINE = 1, WATER_HUGE = 2, WATER_UNHARMED = 3 };
	static constexpr float MACHINE_SINK = 0.15f, MACHINE_DRAG = 0.35f;
	uint8_t waterHardiness(const GameObjBlueprint* bp) const;
	// what the water does at a place now (DEPTH_NONE: no water there); tests, the AI
	struct CurrentAt { int depth = DEPTH_NONE, speed = 0; float metres = 0, flow = 0, drag = 0, harm = 0; bool water = false; Vector3 dir, shore; };
	CurrentAt currentAt(Server& server, const Vector3& p);
	// the same without working out the flow first (the AI's: none until the currents ran once)
	CurrentAt currentHere(const Vector3& p) const;
	// the path finding's extra cost a tile for the water the currents make dangerous (null: none
	// now); around: whether the way between two points comes within margin tiles of any of it
	struct PathCost {
		int w = 0, h = 0;
		std::vector<int32_t> cost;
		std::vector<int32_t> sum; // (w + 1) x (h + 1): the tiles with a cost, summed (a box's count)
		std::vector<std::pair<int, int32_t>> tiles; // (the costly tiles: rebuilt only when they change)
		bool around(const Vector3& a, const Vector3& b, int margin = 8) const;
	};
	const PathCost* currentsPathCost() const { return pathCost.cost.empty() ? nullptr : &pathCost; }
	// the flow worked out again (tests: a water design changed)
	void forgetCurrents() { currentsPrepared = false; }
	// the playable tiles of water units can stand in (tests)
	const std::vector<int>& wadeableTiles(Server& server);
	size_t swimmerCount() const { return swimmers.size(); }

	// ---- artillery breaks the ice (settings.breakingIce, with the seasons; iceStruck) ----
	// A heavy shot (ShotHoleRadius: a bombard's ball, a trebuchet's, a mangonel's, a rocket)
	// coming down on frozen water (Terrain::frozenAt: walkable ice), on the ice or on a unit
	// standing on it, breaks a hole in it that wide (Order.cpp, the missile's landing). A shot
	// into a hole freezing over again breaks the new ice as far as it reaches. The holes are the
	// terrain's (Terrain::iceHoles: its tiles open water, not frozenAt, for the path finding, the
	// heights, the boats), sent to the clients (NETCLIMSG_ICE_HOLES), drawn there, saved.
	//  - Who stands on deep water (applyFloods' deepTiles) where it opens falls in: ICE_FALL_HARM
	//    of their health at once, then ICE_WATER_HARM a second in the icy water (the undead twice
	//    that), while they swim (the Swim animation, SWIM_SHARE of their speed) to the nearest
	//    ice or shore they can walk on. War machines sink (ICE_MACHINE_SINK a second), the huge
	//    creatures can't swim out, the otherworldly walk the bed untouched. A death in a hole is
	//    the shooter's kill (UnitHistory: the hole's shooter is the victim's last hit, each
	//    second); deaths are counted as drowned (stats.iceDrowned too).
	//  - In the frost (iceTemperature below 0) a hole closes: its radius shrinks by
	//    ICE_CLOSE_RATE m a second a degree of frost (at least ICE_CLOSE_MIN, at most
	//    ICE_CLOSE_MAX: about 2 minutes for a bombard's hole at -10 C), the new ice drawn thinner
	//    out to where it was widest; under Terrain::ICE_HOLE_MIN wide it is ice again, the thin
	//    ice fading out after. Not in the frost it stays open; the ice gone (below ICE_WALKABLE),
	//    the holes are too.
	// With the setting off, without the seasons or without ice, nothing of this runs.
	static constexpr float ICE_FALL_HARM = 0.1f, ICE_WATER_HARM = 0.06f, ICE_MACHINE_SINK = 0.25f;
	static constexpr float ICE_CLOSE_RATE = 0.004f, ICE_CLOSE_MIN = 0.005f, ICE_CLOSE_MAX = 0.05f;
	static constexpr size_t ICE_HOLES_MOST = 128;
	struct IceHole { float x = 0, z = 0, radius = 0, widest = 0; uint32_t shooter = 0; float born = 0; };
	// the hole a missile of this blueprint breaks (radius, m; 0: none, not artillery)
	static float ShotHoleRadius(const std::string& missileName);
	// a missile came down at a place (Order.cpp): breaks the ice there if it is artillery
	void iceStruck(Server& server, ServerGameObject* missile, const Vector3& at);
	// a hole that wide broken at a place by that shooter (its unit's id; 0 none); false: no ice
	// there to break (or the setting off)
	bool breakIce(Server& server, const Vector3& at, float radius, uint32_t shooter);
	const std::vector<IceHole>& iceHoles() const { return holes; }
	size_t iceSwimmerCount() const { return iceSwimmers.size(); }
	// tests: the ice's temperature held there (below -999: not)
	float forceIceTemperature = -1000.0f;

	// the peasants' lodging at night and in winter (PeasantLodging.h)
	PeasantLodging lodging;

	// the save file's environment section (an older save's side file <save>.env.txt): the settings
	// and the state, read back as is; the lodging's units after the world is loaded
	std::string saveText() const;
	void loadText(const std::string& text);
	void afterWorldLoaded(Server& server) { lodging.afterLoad(server); }
	void reset() { *this = ServerEnvironment(); }

	// for the tests: what happened ("AITEST environment" lines)
	struct Stats { int carriedSeconds = 0, carriedMost = 0, sweptAway = 0, rainedOut = 0, firesShrunk = 0, frozen = 0, thirst = 0, drowned = 0, lightningUnits = 0, lightningBuildings = 0, lightningGround = 0, floodedBuildings = 0; float floodDamage = 0;
		size_t floodTiles = 0, floodedTiles = 0; int onIceMost = 0, onIceSeconds = 0, fallowHarvests = 0;
		int wildfires = 0, fireCells = 0, fireCellsMost = 0, fireDeaths = 0, buildingsFired = 0, fromMissiles = 0, fromBuildings = 0, spontaneous = 0;
		int iceHolesBroken = 0, iceFallen = 0, iceDrowned = 0, iceSwamOut = 0, iceSwimSeconds = 0;
		// the deaths by player id: 0 frozen, 1 thirst, 2 drowned (floods and the ice), 3 fire, 4 lightning
		std::map<uint32_t, std::array<int, 5>> deathsBy; } stats;
	// what happens: frozen units, strikes (for the tests' census)
	bool trace = false;
	// tests ("environmentForce": { "weather": <EnvWeather>, "waterRise": <m> }): that weather
	// always, the water's level heading there; -1 / a NaN: none
	int forceWeather = -1;
	float forceWaterRise = -1000.0f;
	bool forceWaterRiseNow = false; // (the forced level at once, not risen to)
	float forceSnowCover = -1.0f, forceIce = -1.0f; // test runs: held (below 0: not)
	// tests ("environmentForce": { "fireAt": [x, z, after s] }): a flaming missile comes down there
	// once that many seconds in (the screenshots' fire)
	float forceFireX = -1, forceFireZ = -1, forceFireAt = -1;
	// tests ("environmentForce": { "lightningAt": [x, z, after s] }): a bolt strikes the building
	// nearest there once that many seconds in (the screenshot of a building struck)
	float forceLightningX = -1, forceLightningZ = -1, forceLightningAt = -1;
	size_t charredCount() const { return charred.size(); }
	// the debug panel (F9, NETSRVMSG_ENVIRONMENT_DEBUG): a setting (EnvironmentSettings::apply's
	// names), or "hour" (0-24: the clock jumps there), "season" (0-4, spring 0: the year jumps
	// there), "weather" (an EnvWeather held from now on, -1: chosen again as usual); the clients
	// are told at once
	void debugSet(Server& server, const std::string& name, float value);
	// a new game's clock put at the start hour and season, whatever the map's game time: once,
	// by the first update or by a debug jump of the clock before it
	void startClock(float now);

private:
	float nextUpdate = -1.0f;
	float nextWindUpdate = -1.0f; // (saved on the "wind" line; below 0: not started)
	bool windSent = false; // the clients were sent the wind on (not saved: a loaded game sends it again)
	void updateWind(Server& server);
	float weatherEnds = 0.0f, nextLightning = 0.0f;
	int resolvedYear = -1;
	int resolvedSeverity[4] = { 1, 1, 1, 1 };
	float temperatureTarget = 12.0f;
	// the day's mean, eased towards temperatureTarget (saved: "temperature"); a save from before
	// it had none: worked out again from its temperature at the first update (baseKnown false)
	float temperatureBase = 12.0f;
	bool baseKnown = true;
	// how far the heatwave's warm nights have come (0-1, eased; saved on the "temperature" line)
	float heatNights = 0.0f;
	bool started = false;
	// startClock done (not saved: a saved game has started)
	bool clockStarted = false;
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
	// the burning and snowed-on buildings (by id; saved, "roof" lines): the strength their fire
	// had at most while the weather was on it (0: not burning), the share of the snow cover a fire
	// melted off the roof, the share left this update (not saved: the next update's)
	struct Roof { float peak = 0.0f, melt = 0.0f, share = 1.0f; };
	std::map<uint32_t, Roof> roofs;
	bool snowProofed = false; // (some building's "Snow Fire Proofing" may be set; saved)
	std::vector<std::pair<uint32_t, uint8_t>> sharesSent; // what the clients were last sent
	void updateBuildingFires(Server& server);
	// the river currents: the flow per playable tile (worked out once: the direction, its speed
	// 0-255, the water's depth in 2 cm, the way to the nearest shore; on flooded land the nearest
	// water's flow and the way away from it), the tiles of water units can stand in, and the units
	// being carried (by id; saved, "swim" lines: on their path or not, its point they make for,
	// the leg they walk)
	struct FlowCell { int8_t dx = 0, dz = 0, sx = 0, sz = 0; uint8_t speed = 0, depth = 0, designed = 0; bool water = false; }; // (designed: 0.1 m/s)
	PathCost pathCost;
	void buildPathCost();
	mutable std::map<const GameObjBlueprint*, uint8_t> hardiness;
	std::vector<FlowCell> flowGrid;
	std::vector<int> currentTiles;
	bool currentsPrepared = false;
	struct Swimmer { bool pathing = false; Vector3 node, leg; };
	std::map<uint32_t, Swimmer> swimmers;
	void prepareCurrents(Server& server);
	void applyCurrents(Server& server);
	// the holes in the ice (saved, "icehole" lines), the units in them swimming (by id; saved,
	// "iceswim" lines), whether the terrain has them (not after a load until the next update)
	// and whether the clients do
	std::vector<IceHole> holes;
	std::set<uint32_t> iceSwimmers;
	bool holesOnTerrain = true, holesSent = true;
	void updateIceHoles(Server& server);
	void holesToTerrain(Server& server);
	void sendIceHoles(Server& server, NetLink* only);
	// the units in the holes' open water (all holes, or the one at index `only`): fallen in now
	// (the harm of the fall) or for a second already; each swims out, or sinks
	void iceHoleUnits(Server& server, int only, bool fell);
	CurrentAt currentAtTile(int ti) const;
	void sendFireShares(Server& server, NetLink* only);
	void charTrees(Server& server, const Vector3& at);
	void updateFires(Server& server);
	bool igniteAt(Server& server, const Vector3& pos, float life, int gen = 0);
	float fuelAt(Server& server, const Vector3& pos);
	void pickWeather(Server& server, float now);
	// a thunderstorm's strike (near: the building nearest that spot, the tests' hook)
	void strikeLightning(Server& server, const Vector3* near = nullptr);
	void applyExposure(Server& server);
	void applyFloods(Server& server);
	void prepareFloods(Server& server);
	void damage(Server& server, ServerGameObject* obj, float fraction, const char* cause = nullptr);
};
