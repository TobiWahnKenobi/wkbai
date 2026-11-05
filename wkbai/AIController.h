// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#pragma once

#include "gameset/Plan.h"
#include "GameObjectRef.h"
#include "util/vecmat.h"
#include <map>
#include <memory>
#include <string>
#include <vector>

struct ServerGameObject;
struct OrderBlueprint;
struct GSFileParser;
struct GameSet;
struct WorkOrder;
struct ObjectFinder;
struct GSCommission;
struct GameObjBlueprint;
struct SrvScriptContext;
template <typename T> struct SpecificFinderResult;

struct WorkOrderInstance {
	SrvGORef city;
	ObjectFinder* unitFinder;
	const WorkOrder* workOrder;
};

struct CommissionInstance {
	struct RequirementInstance {
		//const T* gsRequirement;
		int state = 0; // active, complete, stalled
	};
	struct CharacterRequirementInstance : RequirementInstance {

	};
	struct BuildingRequirementInstance : RequirementInstance {
		std::vector<SrvGORef> foundations;
	};
	struct UpgradeRequirementInstance : RequirementInstance {
		bool done = false;
	};

	const GSCommission* blueprint;
	bool forceBalance = false; // ACTIVATE_COMMISSION ... FORCE_BALANCE
	float suspendedSince = -1.0f; // game time the suspension condition became true, -1 when not suspended
	SrvGORef handlerObject;
	
	std::vector<CharacterRequirementInstance> characterReqInsts;
	std::vector<BuildingRequirementInstance> buildingReqInsts;
	std::vector<UpgradeRequirementInstance> upgradeReqInsts;
};

struct AIController {
	ServerGameObject* gameObj;
	const PlanNodeSequence* planBlueprint = nullptr; PlanNodeSequence::State planState;
	std::vector<WorkOrderInstance> workOrderInstances;
	// unit id -> the (city id, work order) that had the unit at the last update: a unit listed by
	// several work orders stays with that one while it still lists the unit
	std::map<uint32_t, std::pair<uint32_t, const WorkOrder*>> unitWorkOrder;
	// wounded attackers sent home: the next check (game time), how many so far
	float nextRetreatCheck = 0.0f;
	int retreated = 0;
	// attack waves sent: the next check (game time), how many, their units
	float nextWaveCheck = 0.0f;
	int waves = 0, waveUnits = 0;
	// focus fire: the next check (game time), the attack orders given so far
	float nextFocusFireCheck = 0.0f;
	int focusOrders = 0;
	// the first barracks: the next check (game time), the foundation laid down (none yet: null)
	float nextBarracksCheck = 0.0f;
	SrvGORef firstBarracks;
	bool firstBarracksDone = false;
	// the opponent model (aiAdapt): enemy soldiers met, by "I'm A Military Unit" (1 heavy
	// infantry, 2 light infantry, 3 heavy cavalry, 4 light cavalry), fading; the personality's
	// army make-up as it was set up (-1: not read yet); the enemy who attacked the base most
	// lately, and when; the next checks
	float enemySeen[5] = {};
	float baseMix[4] = { -1.0f, -1.0f, -1.0f, -1.0f };
	uint32_t grudgePlayer = 0;
	float grudgeTime = -1.0f;
	float nextAdaptScan = 0.0f, nextAdaptMix = 0.0f;
	int mixChanges = 0;
	// waves walking to their rally point before they attack together
	struct GatheringWave { uint32_t army, target; Vector3 rally; float since; };
	std::vector<GatheringWave> gatheringWaves;
	// waves' formations marching in Column to 50 m short of their target, then in Line (Wedge:
	// heavy cavalry) before they charge; lined: the game time they were put in Line, else < 0
	struct MarchingFormation { uint32_t formation, target; Vector3 point; float since, lined; };
	std::vector<MarchingFormation> marchingFormations;
	int formationsSent = 0;
	std::vector<CommissionInstance> commissionInstances;

	// The advanced AI (AIAdvanced.cpp, "aiAdvanced"): whether this player has it (-1: not yet
	// decided), the defenders out (unit id: where, since when), its waves (to bring home when
	// losing, or on to the next building), and counts for the census
	int advancedState = -1;
	float nextAdvancedCheck = 0.0f, nextAdvancedWaveCheck = 0.0f;
	struct Defending { Vector3 at; float since; };
	std::map<uint32_t, Defending> defenders;
	struct AdvancedWave { uint32_t army, target; Vector3 targetAt; size_t size; };
	std::vector<AdvancedWave> advancedWaves;
	float nextLifelineCheck = 0.0f;
	float nextGuardCheck = 0.0f;
	int guardTrained = 0; // the home guard's soldiers ordered (homeGuard)
	int surplusTrained = 0; // the surplus's soldiers ordered (homeGuard, with aiWar)
	int homeGuard(float now);
	int trained = 0; // soldiers ordered by a Lua AI's ai.train
	int lifelines = 0; // the food given by the lifeline (AIController::update)
	// wkbai's seasons: the food kept back for the winter (no spending below it, canAffordObject)
	float winterReserve = 0.0f, nextWinterCheck = 0.0f;
	// wkbai's seasons and weather, the AI's answers (AIWeather.cpp, "aiWeather"): whether this
	// player has them (-1: not decided yet); the units the weather moved, out of the work orders',
	// the war layer's and focus fire's hands until released (the scripts' army or formation they
	// were taken out of, why, until when, when last sent); the burning buildings whose garrison was
	// let out; when a wave was first held; counts for the census
	// (and the order it had, given back on release: the scripts' first builders have theirs from
	// the scripts, not from a work order, and their opening waited for good when it was lost)
	struct WeatherHold { float until = 0.0f, sent = 0.0f; uint32_t parent = 0, target = 0; const OrderBlueprint* order = nullptr; uint8_t why = 0; };
	std::map<uint32_t, WeatherHold> weatherHeld;
	std::map<uint32_t, float> weatherUngarrisoned;
	int weatherState = -1;
	float nextWeatherCheck = 0.0f, waveHeldSince = -1.0f;
	int weatherSheltered = 0, weatherIceRetreats = 0, weatherFloodRetreats = 0, weatherFireRetreats = 0, weatherExtinguish = 0, weatherUngarrisons = 0;
	int wavesHeldCold = 0, wavesHeldIce = 0, wavesHeldNight = 0, nightWaves = 0;
	bool weatherActive();
	void weatherUpdate();
	bool weatherHolds(uint32_t id) const { return !weatherHeld.empty() && weatherHeld.count(id) != 0; }
	// whether the war layer may take the unit: not held, or only kept home from the cold or heat
	bool weatherPoolable(uint32_t id) const { auto it = weatherHeld.find(id); return it == weatherHeld.end() || it->second.why <= 2; }
	void weatherWaveSent();
	void weatherHold(ServerGameObject* u, int why, float until, const Vector3* moveTo, ServerGameObject* target = nullptr, int order = -1);
	void weatherRelease(uint32_t id);
	// a new wave from base at the target: whether the weather says wait (harmful cold or heat, the
	// thaw, the coming night)
	bool weatherHoldsWave(const Vector3& base, ServerGameObject* target, bool striking);
	// the soldiers kept home: more at night
	size_t weatherKeep(size_t available, size_t keep);
	// no foundation where a flood could reach
	bool weatherAvoidsSite(const Vector3& pos);
	std::string weatherCensus() const;
	int threatsMet = 0, defendersSent = 0, peasantsFled = 0, wavesHeld = 0, wavesRecalled = 0, wavesRetargeted = 0;
	bool advanced();
	// the war layer (AIAdvanced.cpp, "aiWar"): its defence forces (an army of its own at a
	// place, until no enemy has been there for 6 s), its waves (army, target, the enemy player,
	// size at launch), the enemy to strike back at and until when, and counts for the census
	struct WarDefence { uint32_t army; Vector3 at; float radius, clearSince; };
	std::vector<WarDefence> warDefences;
	struct WarWave { uint32_t army, target, enemy; size_t size; std::vector<uint32_t> members; };
	std::vector<WarWave> warWaves;
	float nextWarCheck = 0.0f, lastWarWave = -1.0f;
	bool lastWaveAtHuman = false; // every other wave goes for a human
	uint32_t counterPlayer = 0;
	float counterUntil = -1.0f;
	int warThreats = 0, warDefenders = 0, warWavesSent = 0, warWaveUnits = 0, warCounters = 0, warRetargets = 0, warRetreats = 0, warEngaged = 0, warBreaches = 0;
	void warUpdate();
	// the characters under this player that fight (soldiers, armed ones without a class: Monks)
	int fighters() const;
	// a Lua AI (AIAdvanced.cpp, LuaAI\<name>.lua) steering this player: whether it has one (made
	// the first time), its state, when it thinks next, its errors
	std::shared_ptr<struct LuaAIState> luaAI;
	float nextLuaThink = 0.0f;
	int luaErrors = 0;
	bool luaActive();
	void startLuaAI(const std::string& name);
	void luaThink();
	// the Lua AI's name and its census() text (the AI test), empty without one
	std::string luaCensus();
	ServerGameObject* launchWave(const std::vector<ServerGameObject*>& members, ServerGameObject* target, ServerGameObject* home);
	void advancedUpdate();
	// the enemy settlement worth attacking most for what guards it, among those the wave would
	// outmatch (1.4 times their strength; any, if anyway), as the building to attack; null: none
	ServerGameObject* advancedWaveTarget(const std::vector<ServerGameObject*>& wave, const Vector3& home, bool anyway);
	static float forceStrength(const std::vector<ServerGameObject*>& units, const std::vector<ServerGameObject*>& against);
	
	// whether an AI runs this player (a plan, commissions or work orders, and no client)
	bool isAI() const;
	AIController(ServerGameObject* obj) : gameObj(obj) {}
	void parse(GSFileParser& gsf, const GameSet& gs);
	void update();

	void activatePlan(int planTag);
	void abandonPlan();
	void registerWorkOrder(ServerGameObject* city, ObjectFinder* unitFinder, const WorkOrder* workOrder);
	void activateCommission(const GSCommission* commission, ServerGameObject* handler, bool forceBalance = false);
	void deactivateCommission(const GSCommission* commission, ServerGameObject* handler);
};

// Whether one of the buildings may spawn the type now (gameset/values.cpp)
bool AISpawnAllowed(const GameObjBlueprint* type, const SpecificFinderResult<ServerGameObject>& buildings);
// How many characters each character requirement of the commission needs, evaluated for its
// handler; with forceBalance, a requirement none of whose ladder's types can be spawned now
// gives what it still misses to the others that can and need some (gameset/values.cpp)
std::vector<int> AICharacterNeeds(const GSCommission* commission, SrvScriptContext* handlerCtx, bool forceBalance);
// The aggressive AI (Server::aggressiveAI): wraps the Kingmaker equations it changes (gameset/values.cpp)
void InstallAIAggressionOverrides(GameSet& gs);

// the Lua AIs there are (LuaAI\*.lua, redata's and the mods'), by name; one's game path
std::vector<std::string> ListLuaAIs();
std::string LuaAIPath(const std::string& name);
