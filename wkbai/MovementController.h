// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#pragma once

#include <vector>
#include "util/vecmat.h"
#include "Movement.h"

struct ServerGameObject;
struct GameObjBlueprint;

struct MovementController {
	Vector3 startMovement(const Vector3& destination);
	void stopMovement();
	void updateMovement();
	//Vector3 getPosition(float time) const;
	bool isMoving() const { return m_started; }
	//Vector3 getDirection() const { return (m_pathNodes[m_pathNodes.size()-1] - m_pathNodes.back()).normal2xz(); }
	Vector3 getDestination() const { return m_pathNodes.front(); }

	MovementController(ServerGameObject* object) : m_object(object) {}

	// Whether an object of this type can't stand on the playable tile (tx, tz): off the map, a
	// building that isn't passable, water too deep for a land unit or land for a boat, a slope
	// steeper than 45 degrees. The same test as the pathfinding.
	static bool isTileBlocked(const GameObjBlueprint* blueprint, int tx, int tz);
	// the same without the buildings: whether the ground itself keeps the type off the tile
	static bool isTerrainBlocked(const GameObjBlueprint* blueprint, int tx, int tz);
	// The nearest tile centre around a position where the object can stand (searching up to
	// maxRadius tiles out); the position itself when none is found
	static Vector3 nearestValidPosition(const GameObjBlueprint* blueprint, const Vector3& position, int maxRadius = 32);

	bool m_started = false;
	std::vector<Vector3> m_pathNodes;
	ServerGameObject* m_object;
};