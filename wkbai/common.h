// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#pragma once

#include <cmath>
#include <cstdint>
#include <map>
#include <set>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include <algorithm>
#include "util/vecmat.h"
#include "Movement.h"
#include "GameObjectRef.h"
#include "Trajectory.h"
#include "tags.h"
#include "gameset/GameObjBlueprint.h"
#include "Environment.h"

struct GameObjBlueprint;
struct Model;
struct GameSet;
struct Terrain;

enum class ProgramType {
	SERVER = 1,
	CLIENT = 2
};

struct Footprint;

struct CommonGameObject {
	uint32_t id;
	const GameObjBlueprint* blueprint;

	CommonGameObject* parent;
	std::map<GameObjBlueprintIndex, std::vector<CommonGameObject*>> children;

	std::map<int, float> items;
	std::map<std::pair<int, int>, float> indexedItems;

	Vector3 position, orientation, scale = Vector3(1.0f, 1.0f, 1.0f);
	// SWITCH_FOOTPRINT (mods): the building's footprint instead of its type's (server)
	const Footprint* footprintOverride = nullptr;
	// PERPENDICULAR_TO_LANDSCAPE types (siege engines, carts, the Behemoth, corpses): the ground's
	// up there (the client sets it as they move; the server's objects stay upright)
	Vector3 groundUp = Vector3(0.0f, 1.0f, 0.0f);
	bool tilted = false;

	int subtype = 0, appearance = 0;
	int color = 0;

	float animStartTime = 0.0f; // should be game_time_t?
	float animSpeed = 1.0f; // playback rate (PLAY_ANIMATION_TO_FIT), times the animation's own
	int animationIndex = 0, animationVariant = 0;
	bool animClamped = false;
	int animSynchronizedTask = -1;

	Movement movement;
	Trajectory trajectory;

	enum Flags {
		fSelectable = 1,
		fTargetable = 2,
		fRenderable = 4,
		fTerminated = 8,
		fPlayerTerminated = 16,
	};
	int flags = fSelectable | fTargetable | fRenderable;
	int disableCount = 0;

	std::string name;

	// wkbai's unit history: the enemy characters it killed and the enemy buildings it destroyed
	// (worked out by the server's UnitHistory, sent to the clients)
	int kills = 0, buildingsDestroyed = 0;

	int reportedCurrentOrder = -1;

	int tileIndex = -1;

	// City
	struct CityRectangle {
		int xStart, yStart, xEnd, yEnd;
	};
	std::vector<CityRectangle> cityRectangles;

	// Building: Spawn order
	int spawnedUnitCommand = -1;
	CmnGORef spawnedUnitTarget = -1;
	Vector3 spawnedUnitDestination;
	Vector3 spawnedUnitFaceTo;

	float getItem(int item) const;
	float getIndexedItem(int item, int index) const;

	CommonGameObject* getParent() const { return parent; }
	CommonGameObject* getPlayer() const;

	Matrix getWorldMatrix() const {
		if (tilted && orientation.x == 0.0f && orientation.z == 0.0f) {
			// turned around y, then laid on the ground: the rows are the model's axes in the world
			// (as below: x = up x z, z = x x up), up the ground's
			const float c = std::cos(-orientation.y), s = std::sin(-orientation.y);
			const Vector3 z0(s, 0.0f, c), up = groundUp;
			Vector3 x(up.y * z0.z - up.z * z0.y, up.z * z0.x - up.x * z0.z, up.x * z0.y - up.y * z0.x);
			const float xl = std::sqrt(x.x * x.x + x.y * x.y + x.z * x.z);
			if (xl > 1e-4f) {
				x = x * (1.0f / xl);
				const Vector3 z(x.y * up.z - x.z * up.y, x.z * up.x - x.x * up.z, x.x * up.y - x.y * up.x);
				Matrix m = Matrix::getZeroMatrix();
				m.m[0][0] = scale.x * x.x; m.m[0][1] = scale.x * x.y; m.m[0][2] = scale.x * x.z;
				m.m[1][0] = scale.y * up.x; m.m[1][1] = scale.y * up.y; m.m[1][2] = scale.y * up.z;
				m.m[2][0] = scale.z * z.x; m.m[2][1] = scale.z * z.y; m.m[2][2] = scale.z * z.z;
				m.m[3][0] = position.x; m.m[3][1] = position.y; m.m[3][2] = position.z; m.m[3][3] = 1.0f;
				return m;
			}
		}
		if (orientation.x == 0.0f && orientation.z == 0.0f) {
			// Most objects only turn around y: scale * rotation y * translation, written out
			// (drawn objects call this every frame)
			const float c = std::cos(-orientation.y), s = std::sin(-orientation.y);
			Matrix m = Matrix::getZeroMatrix();
			m.m[0][0] = scale.x * c; m.m[0][2] = -scale.x * s;
			m.m[1][1] = scale.y;
			m.m[2][0] = scale.z * s; m.m[2][2] = scale.z * c;
			m.m[3][0] = position.x; m.m[3][1] = position.y; m.m[3][2] = position.z; m.m[3][3] = 1.0f;
			return m;
		}
		return Matrix::getScaleMatrix(scale)
			* Matrix::getRotationXMatrix(-orientation.x)
			* Matrix::getRotationYMatrix(-orientation.y)
			* Matrix::getRotationZMatrix(-orientation.z)
			* Matrix::getTranslationMatrix(position);
	}

	bool isInteractable() const { return (disableCount <= 0) && !(flags & fTerminated); }

	Model* getModel() const;

	size_t countChildren() const { size_t count = 0; for (auto& [_, vec] : children) count += vec.size(); return count; }

	template<typename AnyGameObject> AnyGameObject* dyncast() { return (AnyGameObject*)this; }
	template<typename AnyGameObject> const AnyGameObject* dyncast() const { return (const AnyGameObject*)this; }

	CommonGameObject(uint32_t id, const GameObjBlueprint *blueprint) : id(id), blueprint(blueprint), parent(nullptr),
		flags(blueprint->getStartingFlags()) {}
};

template<typename Program, typename AnyGameObject> struct SpecificGameObject : CommonGameObject {
	using CommonGameObject::CommonGameObject;
	AnyGameObject* getParent() const { return (AnyGameObject*)CommonGameObject::getParent(); }
	AnyGameObject* getPlayer() const { return (AnyGameObject*)CommonGameObject::getPlayer(); }
};

struct CommonGameState {
	using GameObject = CommonGameObject;

	ProgramType programType;
	std::shared_ptr<const GameSet> gameSet = nullptr;

	CommonGameObject* level = nullptr;
	// a hash map: every object reference looks its object up here, and the ordered map's tree
	// (with the trees and props, tens of thousands of objects) was a cost of its own. Nothing
	// may depend on its order: what walks it sorts by id.
	std::unordered_map<uint32_t, CommonGameObject*> idmap;
	// the same objects in an array by id (ids below ID_ARRAY_LIMIT; nullptr where none): every
	// reference followed, by millions a step with 10,000 units, without the hash map's lookup.
	// Written with idmap only through idmapSet and idmapErase.
	static constexpr uint32_t ID_ARRAY_LIMIT = 1u << 22;
	std::vector<CommonGameObject*> idArray;
	void idmapSet(uint32_t id, CommonGameObject* obj) {
		idmap[id] = obj;
		if (id < ID_ARRAY_LIMIT) {
			if (id >= idArray.size())
				idArray.resize(std::max<size_t>(id + 1, idArray.size() * 2), nullptr);
			idArray[id] = obj;
		}
	}
	void idmapErase(uint32_t id) {
		idmap.erase(id);
		if (id < idArray.size())
			idArray[id] = nullptr;
	}

	// ordered by object id: the scripts walk them, and a set ordered by its insertions walked
	// differently after a save game was loaded (a replay played another game)
	std::map<int, std::set<CmnGORef>> aliases;
	std::map<std::pair<int, int>, int> diplomaticStatuses;

	struct Tile {
		std::vector<CmnGORef> objList;
		// (server) the objects on the tile now, as pointers: objList's, in its order, without the
		// destroyed ones (objList keeps their ids), for the searches that would look each id up
		// (FreeStandingSpot). Kept by ServerGameObject::updatePosition and Server::destroyObject.
		std::vector<CommonGameObject*> objects;
		//std::vector<CmnGORef> zoneList;
		CmnGORef zone;
		CmnGORef building;
		bool buildingPassable;
		// the tile's TERRAIN (GameSet::terrains index, -1 none), from its texture group; -2 not
		// looked up yet (MovementController::isTileBlocked; reset when the texture changes)
		short terrainType = -2;
		// SET_TILE_STATUS (mods; WKB.exe 0x496f70): 0 BLOCKED, 1 CANNOT_BE_CONSTRUCTED_UPON, 2
		// BLOCKED_BY_CITY_PERIMETER, 3 CONTAINS_WALL_SECTION, 4 HAS_FORMATION, 5 PASSABLE (as the
		// map has it). 0 and 3 block the way, 0 to 3 keep buildings off.
		uint8_t scriptStatus = 5;
	};
	std::unique_ptr<Tile[]> tiles;
	Terrain* terrain = nullptr;
	// the night's shorter sight (Environment.h): factor 1 unless wkbai's day and night is on
	NightSight nightSight;

	CommonGameObject* getLevel() const { return level; }
	CommonGameObject* findObject(uint32_t id) {
		if (id < idArray.size())
			return idArray[id];
		if (id < ID_ARRAY_LIMIT)
			return nullptr;
		auto it = idmap.find(id);
		return (it != idmap.end()) ? it->second : nullptr;
	}

	int getDiplomaticStatus(CommonGameObject* a, CommonGameObject* b) const;

	bool isServer() const { return programType == ProgramType::SERVER; }
	bool isClient() const { return programType == ProgramType::CLIENT; }
	const char* getProgramName() const { return isServer() ? "Server" : isClient() ? "Client" : "?"; }

	CommonGameState(ProgramType programType) : programType(programType) {}

	// oldBlueprint: the footprint freed (a conversion: the old one; null: the object's own)
	void updateOccupiedTiles(CommonGameObject* object, const Vector3& oldposition, const Vector3& oldorientation, const Vector3& newposition, const Vector3& neworientation, const GameObjBlueprint* oldBlueprint = nullptr);
};

template<typename AnyGameObject, ProgramType PROGTYPE> struct SpecificGameState : CommonGameState {
	AnyGameObject* getLevel() const { return (AnyGameObject*)level; }
	AnyGameObject* findObject(uint32_t id) { return (AnyGameObject*)CommonGameState::findObject(id); }
	SpecificGameState() : CommonGameState(PROGTYPE) {}
};
