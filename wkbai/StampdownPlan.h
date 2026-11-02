#pragma once

#include "common.h"
#include "GameObjectRef.h"
#include "util/vecmat.h"

struct GameObjBlueprint;
struct CommonGameState;

struct StampdownPlan {
	enum struct Status {
		Valid = 0,
		InvalidNoExplanation,
		InvalidTerrain,
		InvalidNotInCity,
		InvalidOccupied
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
};
