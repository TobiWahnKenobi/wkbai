// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#pragma once

#include "gameset/Plan.h"
#include "GameObjectRef.h"
#include "util/vecmat.h"
#include <map>

struct ServerGameObject;
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
