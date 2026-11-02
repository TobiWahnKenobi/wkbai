#pragma once

#include "common.h"
#include "GameObjectRef.h"
#include "util/vecmat.h"
#include "gameset/Footprint.h"

struct GameObjBlueprint;
struct CommonGameState;

struct StampdownPlan {
	enum struct Status {
		Valid = 0,
		InvalidNoExplanation,
		InvalidTerrain,
		InvalidNotInCity,
		InvalidOccupied,
		// WKB.exe's BUILDING_PLACEMENT_INVALID_TERRAIN_TYPE, _TOO_STEEP and
		// _OVERLAPS_NEIGHBOURING_BUILDING (see getStampdownPlan)
		InvalidTerrainType,
		InvalidTooSteep,
		InvalidNeighbour
	};
	struct StampdownCreateAction {
		const GameObjBlueprint* blueprint;
		Vector3 position;
		Vector3 orientation;
	};
	struct StampdownRemoveAction {
		GameObjectRef object;
	};

	Status status = Status::InvalidNoExplanation;
	std::vector<StampdownCreateAction> toCreate;
	std::vector<StampdownRemoveAction> toRemove;

	GameObjectRef ownerObject;
	const GameObjBlueprint* newOwnerBlueprint = nullptr;
	CommonGameObject::CityRectangle cityRectangle;

	static StampdownPlan getStampdownPlan(
		CommonGameState* gameState, CommonGameObject* playerObj, const GameObjBlueprint* blueprint, Vector3 position, Vector3 orientation);
	// players the slope limit (MAX_FOUNDATION_HEIGHT_DIFFERENTIAL) doesn't apply to: the server
	// sets it to exempt its AI players (see Server::Server)
	static bool (*exemptFromSlopeLimit)(CommonGameState* gameState, CommonGameObject* playerObj);
	// whether the footprint tile may stand on playable tile (tx, tz) by its terrain
	// (USE_TERRAIN_BLUEPRINT_LIST)
	static bool tileTerrainAllowed(const CommonGameState* gameState, const Footprint::TileOffset& to, int tx, int tz);
	// WKB.exe's text key for a refused placement (BUILDING_PLACEMENT_...), null if none
	static const char* statusTextKey(Status status);
};
