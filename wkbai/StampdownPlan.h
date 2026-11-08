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
		InvalidNeighbour,
		// WKB.exe's city rules (0x510f90): a civic building's ring of walls on three tiles of a
		// city's area or four counts of its walls (_MUST_BE_CONNECTED_TO_A_CITY), a civic centre's
		// on none of its walls
		// (_MUST_BE_DETACHED_FROM_ALL_CITIES), no new wall over a gatehouse
		// (_A_WALL_IS_OVERLAPPING_A_GATEHOUSE)
		InvalidNotConnected,
		InvalidNotDetached,
		InvalidGatehouseOverlap,
		// a rural building (a farm, a village) inside the new rectangle (WKB.exe 0x5113c0,
		// _THERE_IS_A_RURAL_BUILDING_INSIDE_THIS_PERIMETER)
		InvalidRuralInside
	};
	struct StampdownCreateAction {
		const GameObjBlueprint* blueprint;
		Vector3 position;
		Vector3 orientation;
	};
	struct StampdownRemoveAction {
		GameObjectRef object;
	};

	// a footprint tile (playable tile coordinates) and whether it may be built on, for the
	// preview's red and green squares
	struct TileVerdict {
		int x, z;
		bool ok;
	};

	Status status = Status::InvalidNoExplanation;
	std::vector<TileVerdict> tiles;
	std::vector<StampdownCreateAction> toCreate;
	std::vector<StampdownRemoveAction> toRemove;

	GameObjectRef ownerObject;
	const GameObjBlueprint* newOwnerBlueprint = nullptr;
	CommonGameObject::CityRectangle cityRectangle;

	static StampdownPlan getStampdownPlan(
		CommonGameState* gameState, CommonGameObject* playerObj, const GameObjBlueprint* blueprint, Vector3 position, Vector3 orientation);
	// the slope limit and the neighbour rule alone for the footprint, unturned, at playable tile
	// (ox, oz), as WKB.exe's yes/no check (0x59b020) the searches for a site use
	static bool groundAllows(CommonGameState* gameState, const Footprint* fp, int ox, int oz);
	// whether the footprint tile may stand on playable tile (tx, tz) by its terrain
	// (USE_TERRAIN_BLUEPRINT_LIST)
	static bool tileTerrainAllowed(const CommonGameState* gameState, const Footprint::TileOffset& to, int tx, int tz);
	// WKB.exe's text key for a refused placement (BUILDING_PLACEMENT_...), null if none
	static const char* statusTextKey(Status status);
};
