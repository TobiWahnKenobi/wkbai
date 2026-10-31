// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#pragma once

#include "gameset/Plan.h"
#include "GameObjectRef.h"
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
