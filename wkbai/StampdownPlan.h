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
	// a wall piece put on a city's ring: the piece and turn that tile takes there (a corner where
	// the ring turns; the play test: a new wall in a razed corner was a straight piece), placed
	// instead of the one asked for; null: the one asked for
	const GameObjBlueprint* placeBlueprint = nullptr;
	Vector3 placeOrientation;
	const GameObjBlueprint* newOwnerBlueprint = nullptr;
	CommonGameObject::CityRectangle cityRectangle;

	static StampdownPlan getStampdownPlan(
		CommonGameState* gameState, CommonGameObject* playerObj, const GameObjBlueprint* blueprint, Vector3 position, Vector3 orientation);
	// The placing interface's help for a gatehouse pointed at a wall piece (wkbai's own): the
	// cursor's ground point lies behind a standing wall, so the gatehouse went beside the wall.
	// When `piece` is a wall piece: `quarter` (the player's turn, 0-3) is turned along it if it lay
	// across, and `position` is set to the placement whose four tiles take the piece's tile, the
	// one the plan accepts nearest `cursorGround` (else the nearest). False: not a wall piece,
	// both left as they were.
	static bool gatehouseOntoWall(CommonGameState* gameState, CommonGameObject* playerObj, const GameObjBlueprint* blueprint,
		const CommonGameObject* piece, const Vector3& cursorGround, int& quarter, Vector3& position);
	// The wall piece the cursor points at: from the ray's ground point back towards the eye, the
	// first tile holding a wall piece while the ray is less than 10 m above the ground (a
	// standing wall's height); null if none. Not the hovered object: the preview hides the
	// pieces a gatehouse replaces, and a hidden piece is never hovered.
	static const CommonGameObject* wallOnRay(CommonGameState* gameState, const Vector3& eye, const Vector3& cursorGround);
	// the slope limit and the neighbour rule alone for the footprint, unturned, at playable tile
	// (ox, oz), as WKB.exe's yes/no check (0x59b020) the searches for a site use
	static bool groundAllows(CommonGameState* gameState, const Footprint* fp, int ox, int oz);
	// a tree, a stone or gold deposit (a container that can't be built upon) on playable tile
	// (tx, tz): no building there, and the auto-walls leave it out, as the original's
	static bool tileHasBlockingObject(CommonGameState* gameState, int tx, int tz);
	// playable tile (tx, tz) in any player's city: no farm or village there (the original's
	// placement check 6, CANNOT_BUILD_HERE; the play test: a farmer's farm inside the walls)
	static bool tileInAnyCity(CommonGameState* gameState, int tx, int tz);
	// playable tile (tx, tz) within two tiles of a gate's way through (a walkable tile of a
	// building that also has blocked tiles: a gatehouse in a stockade or wall). The AI's sites
	// leave it free (gameset/position.cpp): on 3 Isles a Kingmaker laid its Trading Post across
	// the outside of its stockade's only gate and its peasants were shut in for the game.
	static bool tileBesideGate(CommonGameState* gameState, int tx, int tz);
	static bool isRural(const GameObjBlueprint* blueprint);
	// whether the footprint tile may stand on playable tile (tx, tz) by its terrain
	// (USE_TERRAIN_BLUEPRINT_LIST)
	static bool tileTerrainAllowed(const CommonGameState* gameState, const Footprint::TileOffset& to, int tx, int tz);
	// WKB.exe's text key for a refused placement (BUILDING_PLACEMENT_...), null if none
	static const char* statusTextKey(Status status);
	// The orientation a building is placed with: a quarter turn about y (0, pi/2, pi, 3pi/2), the
	// nearest to the given one. WKB.exe turns a footprint by quarters only (0x59a420: within half
	// a radian of one, else unturned); its interface steps the stampdown's angle by a quarter
	// with [ and ] (0x64ff22).
	static Vector3 quarterTurn(const Vector3& orientation);
};
