// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#include "MovementController.h"
#include "server.h"
#include "Pathfinding.h"
#include "terrain.h"
#include "gameset/gameset.h"
#include "TrnTextureDb.h"
#include <cassert>
#include <SDL3/SDL_timer.h>

namespace {
	template<typename Predicate> Pathfinding::PFPos findNearestUnblockedPos(Pathfinding::PFPos pfpos, Predicate pred, int maxr) {
		int ox = pfpos.x, oz = pfpos.z;
		if (!pred(pfpos))
			return { ox, oz };
		for (int r = 1; r < maxr; r++) {
			int x = ox + r;
			int z = oz;
			for (int i = 0; i < r; i++) {
				if (!pred({ x,z }))
					return { x, z };
				x--; z++;
			}
			for (int i = 0; i < r; i++) {
				if (!pred({ x,z }))
					return { x, z };
				x--; z--;
			}
			for (int i = 0; i < r; i++) {
				if (!pred({ x,z }))
					return { x, z };
				x++; z--;
			}
			for (int i = 0; i < r; i++) {
				if (!pred({ x,z }))
					return { x, z };
				x++; z++;
			}
		}
		return { -1, -1 };
	}
}

bool MovementController::isTileBlocked(const GameObjBlueprint* blueprint, int tx, int tz)
{
	const Server* server = Server::instance;
	const Terrain* trn = server ? server->terrain : nullptr;
	if (!trn || !server->tiles)
		return false;
	const auto trnsize = trn->getNumPlayableTiles();
	if (!(tx >= 0 && tx < trnsize.first && tz >= 0 && tz < trnsize.second))
		return true;

	const auto& tile = server->tiles[tz * trnsize.first + tx];
	if (tile.building.getFrom<Server>() && !tile.buildingPassable)
		return true;
	return isTerrainBlocked(blueprint, tx, tz);
}

bool MovementController::isTerrainBlocked(const GameObjBlueprint* blueprint, int tx, int tz)
{
	const Server* server = Server::instance;
	const Terrain* trn = server ? server->terrain : nullptr;
	if (!trn || !server->tiles)
		return false;
	const auto trnsize = trn->getNumPlayableTiles();
	if (!(tx >= 0 && tx < trnsize.first && tz >= 0 && tz < trnsize.second))
		return true;

	const auto* trnTile = trn->getPlayableTile(tx, tz);
	if (trnTile) {
		const float h[4] = {
			trn->getVertex(trn->edge + tx, trn->edge + tz),
			trn->getVertex(trn->edge + tx + 1, trn->edge + tz),
			trn->getVertex(trn->edge + tx, trn->edge + tz + 1),
			trn->getVertex(trn->edge + tx + 1, trn->edge + tz + 1)
		};
		auto [minH, maxH] = std::minmax_element(std::begin(h), std::end(h));

		const bool waterUnit = blueprint->canWalkOnWater() && blueprint->bpClass != Tags::GAMEOBJCLASS_FORMATION;
		if (waterUnit && !trnTile->fullOfWater)
			return true;

		// the type's passability fields (USE_PASSABILITY_FIELDS, Passability_Fields.cpp; WKB.exe
		// reads them, its pathfinder's use of them isn't traced): a land unit can't
		// stand on water, river beds or mountains (by the tile's texture), however flat
		if (const std::vector<bool>* passable = blueprint->getPassableTerrains()) {
			auto& tile = server->tiles[tz * trnsize.first + tx];
			if (tile.terrainType == -2) {
				tile.terrainType = -1;
				const auto& groups = server->gameSet->associatedTileTexGroups;
				auto it = trnTile->texture && trnTile->texture->grp ? groups.find(trnTile->texture->grp->name) : groups.end();
				if (it != groups.end())
					tile.terrainType = (short)(it->second - &server->gameSet->terrains[0]);
			}
			if (tile.terrainType >= 0 && tile.terrainType < (int)passable->size() && !(*passable)[tile.terrainType])
				return true;
		}

		if (!waterUnit && trnTile->fullOfWater) {
			if (trnTile->waterLevel - *minH > 1.5f)
				return true;
		}

		// no gradients above 45 degrees
		if (!waterUnit && *maxH - *minH > 5.0f)
			return true;
	}

	return false;
}

Vector3 MovementController::nearestValidPosition(const GameObjBlueprint* blueprint, const Vector3& position, int maxRadius)
{
	const Pathfinding::PFPos start{ (int)(position.x / 5.0f), (int)(position.z / 5.0f) };
	if (!isTileBlocked(blueprint, start.x, start.z))
		return position;
	const auto found = findNearestUnblockedPos(start, [blueprint](Pathfinding::PFPos p) { return isTileBlocked(blueprint, p.x, p.z); }, maxRadius);
	if (found.x < 0)
		return position;
	Vector3 result(found.x * 5.0f + 2.5f, 0.0f, found.z * 5.0f + 2.5f);
	if (const Terrain* trn = Server::instance ? Server::instance->terrain : nullptr)
		result.y = trn->getHeightEx(result.x, result.z, blueprint->canWalkOnWater());
	return result;
}

// the time spent finding paths, for the server's "profileSim" report (ms)
double g_pathfindingMs = 0.0;
uint64_t g_pathfindingSearches = 0;

Vector3 MovementController::startMovement(const Vector3& destination)
{
	const uint64_t profileStart = SDL_GetPerformanceCounter();
	struct ProfileEnd {
		uint64_t start;
		~ProfileEnd() {
			g_pathfindingMs += (double)(SDL_GetPerformanceCounter() - start) * 1000.0 / (double)SDL_GetPerformanceFrequency();
			g_pathfindingSearches++;
		}
	} profileEnd{ profileStart };
	//m_object->startMovement(destination);
	//m_pathNodes = { destination };

	m_requested = destination;
	using namespace Pathfinding;
	Terrain* trn = Server::instance->terrain;
	auto trnsize = trn->getNumPlayableTiles();
	const GameObjBlueprint* blueprint = m_object->blueprint;
	auto pred = [blueprint](PFPos pfp) -> bool { return isTileBlocked(blueprint, pfp.x, pfp.z); };

	PFPos posStart{ (int)(m_object->position.x / 5.0f), (int)(m_object->position.z / 5.0f) };
	PFPos posEnd{ (int)(destination.x / 5.0f), (int)(destination.z / 5.0f) };

	// if start is blocked...
	if (pred(posStart)) {
		// find "nearest" blocked tile, if unit is currently inside blocked tile
		auto area = trn->getNumPlayableTiles();
		int maxr = std::max(area.first, area.second);
		posStart = findNearestUnblockedPos(posStart, pred, maxr); // WARNING: Might cause infinite loop if there is no unblocked tile!
		if (pred(posStart)) {
			stopMovement();
			return destination;
		}
	}

	// if destination if blocked, find non-blocked tile nearest to destination
	Vector3 realDestination = destination;
	if (pred(posEnd)) {
		Vector3 dir = (m_object->position - realDestination).normal2xz();
		if (!(dir.x == 0.0f && dir.z == 0.0f)) {
			Vector3 test = realDestination;
			while (true) {
				test += dir;
				PFPos near = { (int)(test.x / 5.0f), (int)(test.z / 5.0f) };
				if (!(near.x >= 0 && near.x < trnsize.first && near.z >= 0 && near.z < trnsize.second)) {
					break;
				}
				if (!pred(near)) {
					posEnd = near;
					realDestination = test;
					break;
				}
				assert(near != posStart);
			}
		}
		// the free tile on the line towards the unit can be far beyond what is in the way: a
		// building on a steep hillside (Tribes Unite), the line crossing the hill to its other
		// side, where the unit then stood 50 m from what it was to build. A free tile much
		// nearer to the destination (on any side) is taken instead; about as near, the one on
		// the unit's side is kept
		const PFPos goal{ (int)(destination.x / 5.0f), (int)(destination.z / 5.0f) };
		const PFPos nearest = findNearestUnblockedPos(goal, pred, 24);
		if (nearest.x >= 0) {
			auto dist = [&](PFPos p) { return std::sqrt((float)((p.x - goal.x) * (p.x - goal.x) + (p.z - goal.z) * (p.z - goal.z))); };
			if (pred(posEnd) || dist(nearest) + 2.0f < dist(posEnd)) {
				posEnd = nearest;
				realDestination = Vector3(nearest.x * 5.0f + 2.5f, destination.y, nearest.z * 5.0f + 2.5f);
			}
		}
	}

	auto rtres = SegmentTraversal(m_object->position.x / 5.0f, m_object->position.z / 5.0f, realDestination.x / 5.0f, realDestination.z / 5.0f, pred);
	if (!rtres) {
		m_pathNodes = { realDestination, m_object->position };
		m_object->startMovement(m_pathNodes[0]);
		m_started = true;
	}
	else {
		auto tileList = DoPathfinding(posStart, posEnd, pred, ManhattanDiagHeuristic, trnsize.first, trnsize.second);
		// test runs: WKB_TRACE_PATH prints the searches cut short and where they send the unit
		if (g_lastSearchCut && getenv("WKB_TRACE_PATH"))
			printf("PATHCUT t=%.1f obj %u %s player %u from (%d,%d) to (%d,%d) goes to (%d,%d)\n", Server::instance->timeManager.currentTime,
				m_object->id, m_object->blueprint->name.c_str(), m_object->getPlayer() ? m_object->getPlayer()->id : 0,
				posStart.x, posStart.z, posEnd.x, posEnd.z, tileList.empty() ? -1 : tileList.front().x, tileList.empty() ? -1 : tileList.front().z);
		if (tileList.size() >= 1) {
			m_pathNodes.clear();
			// a search cut short ends at the tile nearest to the destination: the unit goes there,
			// not on in a straight line through what is in the way
			if (tileList.front() == posEnd)
				m_pathNodes.emplace_back(realDestination);
			for (PFPos& pfp : tileList) {
				m_pathNodes.emplace_back(pfp.x * 5.0f + 2.5f, m_object->position.y, pfp.z * 5.0f + 2.5f);
			}
			if (m_pathNodes.size() >= 2) {
				m_object->startMovement(m_pathNodes[m_pathNodes.size() - 2]);
				m_started = true;
			}
			else
				stopMovement(); // cut short without getting any nearer
		}
		else
			stopMovement();
	}
	return realDestination;
}

void MovementController::stopMovement()
{
	m_object->stopMovement();
	m_pathNodes.clear();
	m_started = false;
}

void MovementController::updateMovement()
{
	// check if Movement is done, if yes, go to next path node...
	if (!m_started) return;
	if ((m_object->position - m_pathNodes[m_pathNodes.size() - 2]).sqlen2xz() < 0.01f) {
		m_pathNodes.pop_back();
		if (m_pathNodes.size() >= 2) {
			m_object->startMovement(m_pathNodes[m_pathNodes.size() - 2]);
		}
		else
			stopMovement();
	}
}
