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
#include <unordered_map>
#include "tags.h"
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>

// profiling: A* searches (not the straight walks), the tiles they visited, their time
uint64_t g_astarSearches = 0, g_astarTiles = 0;
// the searches a group field answered, the fields made, the goals no search could reach
uint64_t g_fieldPaths = 0, g_fieldBuilds = 0, g_unreachableGoals = 0;
double g_astarMs = 0.0;

namespace {
	// WKB_PATHDUMP=<folder> WKB_PATHDUMP_FROM=<game s> WKB_PATHDUMP_TO=<game s>: every A* search in that
	// time to <folder>/queries.txt, and each movement type's blocked tiles (when first searched in it)
	// to <folder>/grid_<n>.bin, for the GPU path finding spike to replay offline
	void dumpSearch(const GameObjBlueprint* blueprint, Pathfinding::PFPos s, Pathfinding::PFPos e, int w, int h, double ms, int tiles, bool cut, size_t len) {
		static const char* dir = getenv("WKB_PATHDUMP");
		if (!dir) return;
		static const float from = getenv("WKB_PATHDUMP_FROM") ? (float)atof(getenv("WKB_PATHDUMP_FROM")) : 0.0f;
		static const float to = getenv("WKB_PATHDUMP_TO") ? (float)atof(getenv("WKB_PATHDUMP_TO")) : 1e9f;
		const float t = Server::instance->timeManager.currentTime;
		if (t < from || t > to) return;
		static FILE* q = fopen((std::string(dir) + "/queries.txt").c_str(), "w");
		static std::map<const GameObjBlueprint*, int> grids;
		auto it = grids.find(blueprint);
		if (it == grids.end()) {
			it = grids.emplace(blueprint, (int)grids.size()).first;
			std::vector<uint8_t> g((size_t)w * h);
			for (int z = 0; z < h; z++) for (int x = 0; x < w; x++) g[(size_t)z * w + x] = MovementController::isTileBlocked(blueprint, x, z) ? 1 : 0;
			if (FILE* f = fopen((std::string(dir) + "/grid_" + std::to_string(it->second) + ".bin").c_str(), "wb")) {
				fwrite(&w, 4, 1, f); fwrite(&h, 4, 1, f); fwrite(g.data(), 1, g.size(), f); fclose(f);
			}
			fprintf(q, "grid %d %s\n", it->second, blueprint->name.c_str());
		}
		fprintf(q, "q %.2f %d %d %d %d %d %.3f %d %d %zu\n", t, it->second, s.x, s.z, e.x, e.z, ms, tiles, cut ? 1 : 0, len);
	}
}

namespace {
	// The cached ground test per movement class (MovementController::movementClass)
	struct ClassGrid {
		bool water = false;
		const std::vector<bool>* passable = nullptr;
		const GameObjBlueprint* sample = nullptr;
		std::vector<uint8_t> state; // per tile: 0 not looked at, 1 free, 2 blocked (the ground alone)
		std::vector<int> region;    // terrainRegion, valid with regionValid
		bool regionValid = false;
	};
	struct Grids {
		uint32_t terrainId = 0, revision = 0;
		size_t logged = 0;
		int w = 0, h = 0;
		std::unordered_map<const GameObjBlueprint*, int> classOf;
		std::vector<ClassGrid> classes;
	} g_grids;
	// test runs: WKB_PATH_NOCACHE=1 the ground looked up on every test (and no regions), as before;
	// WKB_PATH_NOFIELDS=1 no group fields
	const bool g_noCache = getenv("WKB_PATH_NOCACHE") != nullptr;
	const bool g_noFields = getenv("WKB_PATH_NOFIELDS") != nullptr;
	// changes whenever a cached tile may have changed (the group fields are dropped then)
	uint32_t g_gridsVersion = 1;

	bool waterUnit(const GameObjBlueprint* bp) { return bp->canWalkOnWater() && bp->bpClass != Tags::GAMEOBJCLASS_FORMATION; }

	// the cache, brought up to date with the server's terrain
	Grids& grids() {
		Grids& g = g_grids;
		const Server* server = Server::instance;
		Terrain* trn = server ? server->terrain : nullptr;
		if (!trn)
			return g;
		const auto [w, h] = trn->getNumPlayableTiles();
		if (g.terrainId != trn->instanceId || g.w != w || g.h != h) {
			g = Grids();
			g.terrainId = trn->instanceId;
			g.w = w; g.h = h;
			g.revision = trn->revision;
			g.logged = trn->changedVertices.size();
			g_gridsVersion++;
		}
		else if (g.revision != trn->revision) {
			for (ClassGrid& c : g.classes) {
				c.state.assign((size_t)w * h, 0);
				c.regionValid = false;
			}
			g.revision = trn->revision;
			g.logged = trn->changedVertices.size();
			g_gridsVersion++;
		}
		else if (g.logged != trn->changedVertices.size()) {
			// a vertex moved: its four tiles looked at again (a building levelling its ground
			// seldom changes whether a tile is blocked; the regions only when one does)
			const int stride = trn->width + 1;
			bool changed = false;
			for (size_t k = g.logged; k < trn->changedVertices.size(); k++) {
				const int v = trn->changedVertices[k];
				const int vx = v % stride - trn->edge, vz = v / stride - trn->edge;
				for (int tz = vz - 1; tz <= vz; tz++)
					for (int tx = vx - 1; tx <= vx; tx++) {
						if (tx < 0 || tz < 0 || tx >= w || tz >= h)
							continue;
						const size_t i = (size_t)tz * w + tx;
						for (ClassGrid& c : g.classes) {
							if (!c.state[i])
								continue;
							const uint8_t now = MovementController::isTerrainBlocked(c.sample, tx, tz) ? 2 : 1;
							if (now != c.state[i]) {
								c.state[i] = now;
								c.regionValid = false;
								changed = true;
							}
						}
					}
			}
			g.logged = trn->changedVertices.size();
			if (changed)
				g_gridsVersion++;
		}
		return g;
	}

	inline bool groundBlocked(ClassGrid& c, int w, int tx, int tz) {
		if (g_noCache)
			return MovementController::isTerrainBlocked(c.sample, tx, tz);
		uint8_t& s = c.state[(size_t)tz * w + tx];
		if (!s)
			s = MovementController::isTerrainBlocked(c.sample, tx, tz) ? 2 : 1;
		// test runs: WKB_PATH_CHECKCACHE=1 checks every cached answer against a fresh one
		static const bool check = getenv("WKB_PATH_CHECKCACHE") != nullptr;
		if (check) {
			static uint64_t checked = 0, wrong = 0;
			checked++;
			if ((s == 2) != MovementController::isTerrainBlocked(c.sample, tx, tz) && ++wrong <= 20)
				printf("PATHCACHE wrong t=%.2f tile (%d,%d) %s: cached %s\n", Server::instance->timeManager.currentTime, tx, tz, c.sample->name.c_str(), s == 2 ? "blocked" : "free");
			if ((checked & ((1ull << 24) - 1)) == 0)
				printf("PATHCACHE %llu checked, %llu wrong\n", (unsigned long long)checked, (unsigned long long)wrong);
		}
		return s == 2;
	}

	// isTileBlocked for a class: off the map, a building that isn't passable, the cached ground
	inline bool tileBlocked(const Server* server, ClassGrid& c, int w, int h, int tx, int tz) {
		if (!(tx >= 0 && tx < w && tz >= 0 && tz < h))
			return true;
		const auto& tile = server->tiles[(size_t)tz * w + tx];
		if (!tile.buildingPassable && tile.building.getFrom<Server>())
			return true;
		return groundBlocked(c, w, tx, tz);
	}

	void computeRegions(ClassGrid& c, int w, int h) {
		c.region.assign((size_t)w * h, -1);
		std::vector<int> stack;
		int next = 0;
		for (int start = 0; start < w * h; start++) {
			if (c.region[start] >= 0 || groundBlocked(c, w, start % w, start / w))
				continue;
			c.region[start] = next;
			stack.push_back(start);
			while (!stack.empty()) {
				const int i = stack.back();
				stack.pop_back();
				const int x = i % w, z = i / w;
				for (int dz = -1; dz <= 1; dz++)
					for (int dx = -1; dx <= 1; dx++) {
						const int nx = x + dx, nz = z + dz;
						if ((dx | dz) == 0 || nx < 0 || nz < 0 || nx >= w || nz >= h)
							continue;
						const int ni = nz * w + nx;
						if (c.region[ni] < 0 && !groundBlocked(c, w, nx, nz)) {
							c.region[ni] = next;
							stack.push_back(ni);
						}
					}
			}
			next++;
		}
		c.regionValid = true;
	}
}

int MovementController::movementClass(const GameObjBlueprint* blueprint)
{
	Grids& g = grids();
	static const GameObjBlueprint* lastBp = nullptr;
	static int lastCls = -1;
	static uint32_t lastTerrain = 0;
	if (blueprint == lastBp && lastTerrain == g.terrainId)
		return lastCls;
	auto it = g.classOf.find(blueprint);
	int cls;
	if (it != g.classOf.end())
		cls = it->second;
	else {
		const bool water = waterUnit(blueprint);
		const std::vector<bool>* passable = blueprint->getPassableTerrains();
		cls = -1;
		for (size_t i = 0; i < g.classes.size() && cls < 0; i++) {
			const ClassGrid& c = g.classes[i];
			if (c.water == water && (c.passable == passable || (c.passable && passable && *c.passable == *passable)))
				cls = (int)i;
		}
		if (cls < 0) {
			cls = (int)g.classes.size();
			ClassGrid& c = g.classes.emplace_back();
			c.water = water;
			c.passable = passable;
			c.sample = blueprint;
			c.state.assign((size_t)g.w * g.h, 0);
		}
		g.classOf[blueprint] = cls;
	}
	lastBp = blueprint;
	lastCls = cls;
	lastTerrain = g.terrainId;
	return cls;
}

bool MovementController::isTileBlockedFast(int cls, int tx, int tz)
{
	const Server* server = Server::instance;
	if (!server || !server->terrain || !server->tiles)
		return false;
	Grids& g = grids();
	return tileBlocked(server, g.classes[cls], g.w, g.h, tx, tz);
}

int MovementController::terrainRegion(int cls, int tx, int tz)
{
	Grids& g = grids();
	if (!(tx >= 0 && tx < g.w && tz >= 0 && tz < g.h) || cls < 0 || cls >= (int)g.classes.size())
		return -1;
	ClassGrid& c = g.classes[cls];
	if (!c.regionValid)
		computeRegions(c, g.w, g.h);
	return c.region[(size_t)tz * g.w + tx];
}

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

	return isTileBlockedFast(movementClass(blueprint), tx, tz);
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

namespace {
	// Group flow fields: when the third search of a step goes to within GROUP_RADIUS tiles of the
	// same goal (a group's move order, a formation's slots), a Dijkstra field is made from that
	// goal over the whole map (Dial's buckets: the costs are the A*'s 100 and 141), and those
	// searches and the next ones near it, for a second, walk down it instead: from the unit's
	// tile to the goal, until a straight line to its own destination is free. Fields are dropped
	// after a second, or when the cached ground changes. Same moves as the A* (8 neighbours,
	// corners may be cut), integer costs: the same in every run (record and replay).
	constexpr int GROUP_RADIUS = 6, GROUP_SEARCHES = 3, MAX_FIELDS = 4;
	constexpr uint64_t FIELD_STEPS = 50;
	constexpr uint32_t UNREACHED = 0xFFFFFFFFu;
	struct GroupField {
		int cls;
		Pathfinding::PFPos origin;
		uint64_t step;
		uint32_t version;
		std::vector<uint32_t> dist;
	};
	std::vector<GroupField> g_fields;
	struct GroupRequest { int cls; Pathfinding::PFPos goal; int count; };
	std::vector<GroupRequest> g_requests;
	uint64_t g_requestStep = ~0ull;
	const void* g_fieldsServer = nullptr;

	int chebyshev(Pathfinding::PFPos a, Pathfinding::PFPos b) { return std::max(std::abs(a.x - b.x), std::abs(a.z - b.z)); }

	template<typename Predicate> void buildField(GroupField& f, int w, int h, Predicate pred) {
		std::vector<uint8_t> blocked((size_t)w * h);
		for (int z = 0; z < h; z++)
			for (int x = 0; x < w; x++)
				blocked[(size_t)z * w + x] = pred(Pathfinding::PFPos{ x, z }) ? 1 : 0;
		f.dist.assign((size_t)w * h, UNREACHED);
		static std::vector<int> ring[142];
		for (auto& b : ring) b.clear();
		const int gi = f.origin.z * w + f.origin.x;
		f.dist[gi] = 0;
		ring[0].push_back(gi);
		size_t pending = 1;
		static const int dx[8] = { 1, -1, 0, 0, 1, -1, 1, -1 }, dz[8] = { 0, 0, 1, -1, 1, 1, -1, -1 };
		static const uint32_t cost[8] = { 100, 100, 100, 100, 141, 141, 141, 141 };
		for (uint32_t d = 0; pending; d++) {
			auto& b = ring[d % 142];
			for (size_t k0 = 0; k0 < b.size(); k0++) {
				const int i = b[k0];
				pending--;
				if (f.dist[i] != d) continue;
				const int x = i % w, z = i / w;
				for (int k = 0; k < 8; k++) {
					const int nx = x + dx[k], nz = z + dz[k];
					if (nx < 0 || nz < 0 || nx >= w || nz >= h) continue;
					const int ni = nz * w + nx;
					if (blocked[ni]) continue;
					const uint32_t nd = d + cost[k];
					if (nd < f.dist[ni]) { f.dist[ni] = nd; ring[nd % 142].push_back(ni); pending++; }
				}
			}
			b.clear();
		}
	}

	// The path down a field from start (end first, as DoPathfinding's), ending where the line to
	// the destination is free; empty when the field doesn't serve (start not reached, no free line)
	template<typename Predicate> std::vector<Pathfinding::PFPos> walkField(const GroupField& f, int w, int h, Pathfinding::PFPos start,
		Pathfinding::PFPos end, const Vector3& destination, Predicate pred) {
		using Pathfinding::PFPos;
		static const int dx[8] = { 1, -1, 0, 0, 1, -1, 1, -1 }, dz[8] = { 0, 0, 1, -1, 1, 1, -1, -1 };
		std::vector<PFPos> path;
		int i = start.z * w + start.x;
		if (f.dist[i] == UNREACHED)
			return {};
		for (;;) {
			const PFPos p{ i % w, i / w };
			path.push_back(p);
			if (chebyshev(p, end) <= 2 * GROUP_RADIUS && !Pathfinding::SegmentTraversal(p.x + 0.5f, p.z + 0.5f, destination.x / 5.0f, destination.z / 5.0f, pred))
				break;
			if (f.dist[i] == 0)
				return {};
			int best = -1;
			uint32_t bd = f.dist[i];
			for (int k = 0; k < 8; k++) {
				const int nx = p.x + dx[k], nz = p.z + dz[k];
				if (nx < 0 || nz < 0 || nx >= w || nz >= h) continue;
				const int ni = nz * w + nx;
				if (f.dist[ni] < bd) { bd = f.dist[ni]; best = ni; }
			}
			if (best < 0)
				return {};
			i = best;
		}
		std::reverse(path.begin(), path.end());
		return path;
	}

	// A group field's path for the search, when there is one or this search makes one
	template<typename Predicate> std::vector<Pathfinding::PFPos> groupPath(int cls, int w, int h, Pathfinding::PFPos start, Pathfinding::PFPos end,
		const Vector3& destination, Predicate pred) {
		const Server* server = Server::instance;
		const uint64_t step = server->simStepIndex;
		if (g_fieldsServer != server || g_requestStep > step + FIELD_STEPS) { // another game
			g_fields.clear();
			g_requests.clear();
			g_fieldsServer = server;
		}
		std::erase_if(g_fields, [&](const GroupField& f) { return f.version != g_gridsVersion || step > f.step + FIELD_STEPS || f.step > step; });
		for (const GroupField& f : g_fields)
			if (f.cls == cls && chebyshev(f.origin, end) <= GROUP_RADIUS) {
				auto path = walkField(f, w, h, start, end, destination, pred);
				if (!path.empty())
					g_fieldPaths++;
				return path;
			}
		if (g_requestStep != step) {
			g_requests.clear();
			g_requestStep = step;
		}
		GroupRequest* req = nullptr;
		for (GroupRequest& r : g_requests)
			if (r.cls == cls && chebyshev(r.goal, end) <= GROUP_RADIUS) { req = &r; break; }
		if (!req) {
			g_requests.push_back({ cls, end, 1 });
			return {};
		}
		if (++req->count < GROUP_SEARCHES || pred(req->goal))
			return {};
		if ((int)g_fields.size() >= MAX_FIELDS)
			g_fields.erase(g_fields.begin());
		GroupField& f = g_fields.emplace_back();
		f.cls = cls;
		f.origin = req->goal;
		f.step = step;
		f.version = g_gridsVersion;
		buildField(f, w, h, pred);
		g_fieldBuilds++;
		auto path = walkField(f, w, h, start, end, destination, pred);
		if (!path.empty())
			g_fieldPaths++;
		return path;
	}
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
	const int cls = movementClass(blueprint);
	auto pred = [cls](PFPos pfp) -> bool { return isTileBlockedFast(cls, pfp.x, pfp.z); };

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
		// already about as near to it as the free ground goes: no walk. A formation member
		// whose place was on blocked ground stood at its edge and went back and forth there in
		// walks of 30 cm, each to the free point the line from its place had found that time
		if ((m_object->position - destination).len2xz() < (realDestination - destination).len2xz() + 0.5f) {
			m_pathNodes.clear();
			walkToNextNode();
			return realDestination;
		}
	}

	auto rtres = SegmentTraversal(m_object->position.x / 5.0f, m_object->position.z / 5.0f, realDestination.x / 5.0f, realDestination.z / 5.0f, pred);
	if (!rtres) {
		m_pathNodes = { realDestination, m_object->position };
		walkToNextNode();
	}
	else {
		// a goal in another terrain region than the unit can't be reached: the search goes to the
		// tile of the unit's region nearest to it instead (it searched up to 2 x 20000 tiles, the
		// longest searches of a game, then went to the nearest tile it had seen, or stood)
		PFPos searchEnd = posEnd;
		if (const int startRegion = g_noCache ? -1 : terrainRegion(cls, posStart.x, posStart.z); startRegion >= 0 && terrainRegion(cls, posEnd.x, posEnd.z) != startRegion) {
			const int maxr = trnsize.first + trnsize.second;
			const PFPos nearest = findNearestUnblockedPos(posEnd, [&](PFPos p) { return terrainRegion(cls, p.x, p.z) != startRegion; }, maxr);
			if (nearest.x >= 0)
				searchEnd = nearest;
			g_unreachableGoals++;
		}
		const uint64_t astarStart = SDL_GetPerformanceCounter();
		std::vector<PFPos> tileList;
		bool fromField = false;
		if (searchEnd == posEnd && !g_noFields) {
			tileList = groupPath(cls, trnsize.first, trnsize.second, posStart, posEnd, realDestination, pred);
			fromField = !tileList.empty();
		}
		if (!fromField)
			tileList = DoPathfinding(posStart, searchEnd, pred, ManhattanDiagHeuristic, trnsize.first, trnsize.second);
		const double astarMs = (double)(SDL_GetPerformanceCounter() - astarStart) * 1000.0 / (double)SDL_GetPerformanceFrequency();
		if (!fromField) {
			g_astarSearches++;
			g_astarTiles += g_lastSearchSteps;
		}
		g_astarMs += astarMs;
		if (!fromField)
			dumpSearch(blueprint, posStart, searchEnd, trnsize.first, trnsize.second, astarMs, g_lastSearchSteps, g_lastSearchCut, tileList.size());
		// test runs: WKB_TRACE_PATH prints the searches cut short and where they send the unit
		if (!fromField && g_lastSearchCut && getenv("WKB_TRACE_PATH"))
			printf("PATHCUT t=%.1f obj %u %s player %u from (%d,%d) to (%d,%d) goes to (%d,%d)\n", Server::instance->timeManager.currentTime,
				m_object->id, m_object->blueprint->name.c_str(), m_object->getPlayer() ? m_object->getPlayer()->id : 0,
				posStart.x, posStart.z, posEnd.x, posEnd.z, tileList.empty() ? -1 : tileList.front().x, tileList.empty() ? -1 : tileList.front().z);
		if (tileList.size() >= 1) {
			m_pathNodes.clear();
			// a search cut short ends at the tile nearest to the destination: the unit goes there,
			// not on in a straight line through what is in the way
			// (a field's path ends where the straight line to the destination is free)
			if (tileList.front() == posEnd || fromField)
				m_pathNodes.emplace_back(realDestination);
			for (PFPos& pfp : tileList) {
				m_pathNodes.emplace_back(pfp.x * 5.0f + 2.5f, m_object->position.y, pfp.z * 5.0f + 2.5f);
			}
			walkToNextNode(); // (none when cut short without getting any nearer)
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

// Starts the walk to the next path node, passing over the ones the unit already stands on. A
// walk to where the unit stands doesn't start (Movement), and then the controller was moving
// with no walk going on, which nothing ended (updateMovement runs for objects that walk): the
// unit stood with its walking animation and its task waited for it to arrive. Already as near
// as it can get to the destination, it stops.
void MovementController::walkToNextNode()
{
	while (m_pathNodes.size() >= 2) {
		if ((m_object->position - m_pathNodes[m_pathNodes.size() - 2]).sqlen2xz() >= 0.01f) {
			m_object->startMovement(m_pathNodes[m_pathNodes.size() - 2]);
			if (m_object->movement.isMoving()) {
				m_started = true;
				return;
			}
			break;
		}
		m_pathNodes.pop_back();
	}
	// (a unit that stands already is left as it is: its task asks again every step)
	if (m_started || m_object->movement.isMoving())
		stopMovement();
	else
		m_pathNodes.clear();
}

void MovementController::updateMovement()
{
	// check if Movement is done, if yes, go to next path node...
	if (!m_started) return;
	if (!m_object->movement.isMoving() || (m_object->position - m_pathNodes[m_pathNodes.size() - 2]).sqlen2xz() < 0.01f) {
		m_pathNodes.pop_back();
		walkToNextNode();
	}
}
