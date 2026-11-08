// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#pragma once

#include <vector>
#include "util/vecmat.h"
#include "Movement.h"

struct ServerGameObject;
struct GameObjBlueprint;

// test runs: a unit's last path search (MovementController.cpp's g_lastSearches, with g_recordSearches)
struct LastPathSearch {
	float time = 0.0f;
	bool straight = false, fromField = false, cut = false, unreachable = false, moving = false;
	int tiles = 0, steps = 0, count = 0;
	Vector3 destination;
};

struct MovementController {
	// Sends the unit towards destination; returns the nearest place it can stand to it (worked
	// out at once). The rest is queued (pending): the straight line's test and the path search
	// run at the end of the server's step with the step's other requests, on worker threads
	// (runPathSearches). The walk then starts at the step's game time, as it did when it started
	// here.
	Vector3 startMovement(const Vector3& destination);
	// stops the walk, and drops a pending search
	void stopMovement();
	void updateMovement();
	void walkToNextNode();
	//Vector3 getPosition(float time) const;
	// walking, or waiting for its path (a pending search counts as moving)
	bool isMoving() const { return m_started || m_pending; }
	bool isPending() const { return m_pending; }
	// whether the last pending search ended with the unit not moving (no way, or already there);
	// cleared by reading it, by a new startMovement and by stopMovement. The tasks wait a
	// second before asking again then, as they did when startMovement searched at once.
	bool takeSearchFailed() { const bool f = m_searchFailed; m_searchFailed = false; return f; }
	//Vector3 getDirection() const { return (m_pathNodes[m_pathNodes.size()-1] - m_pathNodes.back()).normal2xz(); }
	Vector3 getDestination() const { return m_pending || m_pathNodes.empty() ? m_pendingDestination : m_pathNodes.front(); }
	// where startMovement was asked to go: getDestination is the nearest place the unit can
	// stand to it, which for a goal on blocked ground (a target in a building) can be far from it
	const Vector3& getRequestedDestination() const { return m_requested; }

	MovementController(ServerGameObject* object) : m_object(object) {}

	// Whether an object of this type can't stand on the playable tile (tx, tz): off the map, a
	// building that isn't passable, water too deep for a land unit or land for a boat, a slope
	// steeper than 45 degrees. The same test as the pathfinding.
	static bool isTileBlocked(const GameObjBlueprint* blueprint, int tx, int tz);
	// the same without the buildings: whether the ground itself keeps the type off the tile
	static bool isTerrainBlocked(const GameObjBlueprint* blueprint, int tx, int tz);
	// The server's movement classes: the types whose ground test is alike (water or land, the same
	// passability fields) share one, and its tiles' isTerrainBlocked is cached (one byte per tile,
	// looked up once, again where the terrain changes). Server thread only.
	static int movementClass(const GameObjBlueprint* blueprint);
	// isTileBlocked for a class, with the cache
	static bool isTileBlockedFast(int cls, int tx, int tz);
	// the class's terrain region of a tile: the tiles the ground lets it stand on, 8-connected as
	// the A*'s moves; -1 where it can't stand. Another region can't be reached (buildings only
	// block more).
	static int terrainRegion(int cls, int tx, int tz);
	// The nearest tile centre around a position where the object can stand (searching up to
	// maxRadius tiles out); the position itself when none is found
	static Vector3 nearestValidPosition(const GameObjBlueprint* blueprint, const Vector3& position, int maxRadius = 32);

	// Runs the step's queued searches (Server::simulate, at its end; server thread): the ground
	// caches filled for every class first, then the searches on worker threads ("pathThreads" in
	// wkconfig.json, WKB_PATH_THREADS; 0 = the cores, 1 = none), then their paths applied in the
	// order they were asked for: the same game with any number of threads.
	static void runPathSearches();

	bool m_started = false;
	bool m_pending = false;
	bool m_searchFailed = false;
	// a pending search is applied only if this hasn't changed since (a new walk or a stop)
	uint32_t m_ticket = 0;
	std::vector<Vector3> m_pathNodes;
	Vector3 m_requested;
	Vector3 m_pendingDestination;
	ServerGameObject* m_object;

private:
	// the walk's end (path done, or none found): stops, and leaves a pending search alone
	void endWalk();
	friend struct PathSearchRunner;
};