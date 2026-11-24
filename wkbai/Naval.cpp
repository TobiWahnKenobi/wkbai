// wkbai - the AI at sea (Naval.h; the RE repo's docs/engine/naval-ai.md)
//
// WKB.exe's game set has the whole of it for a human: the "Dock Wood" (a Village unlocks it), the
// Cog (a transport for 40 "Weight" of passengers: infantry 1, cavalry 2, a Bombard 5), the Arrow,
// Fire, Mangonel and Bombard Boats, and Mechanics\Passenger.cpp's boarding: "Board Vessel" on a
// land unit registers it with the ship, the ship sails to the shore nearest it (PATHFIND_TO_SHORE)
// and waits, the unit walks to that landing point and boards (it is disabled, one of the ship's
// "Passengers"); "Unload Passengers" on the ship teleports them to the nearest place they can
// stand. No AI script builds a dock or a ship (only the Kingmaker Player's "Can Spawn Cog" and
// "Can Spawn Arrow Boat" items exist), and "Build Dock Wood" is hidden while a Kingmaker AI plays.
// This layer drives those same commands for the AI.
#include "Naval.h"
#include "AIController.h"
#include "AIUtil.h"
#include "MovementController.h"
#include "NNSearch.h"
#include "Order.h"
#include "StampdownPlan.h"
#include "server.h"
#include "terrain.h"
#include "tags.h"
#include "gameset/ScriptContext.h"
#include "gameset/command.h"
#include "gameset/gameset.h"
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstring>
#include <queue>
#include <set>
#include <sstream>

void AIAttack(ServerGameObject* unit, ServerGameObject* target); // AIController.cpp
bool OutOfReach(ServerGameObject* unit, const Vector3& point, float reach); // Order.cpp

namespace {
	struct Ids {
		// (by the game set's serial, not its address: the next game's game set came at the same
		// address and the blueprints cached here were the old one's)
		uint64_t of = 0;
		int boat = -1, maxWeight = -1, curWeight = -1, weight = -1, cantBoard = -1, canBuildDock = -1, range = -1, aggression = -1;
		int passengers = -1;          // the association
		int board = -1, unload = -1;  // the commands
		int readyToBoard = -1;        // the event "I'm ready to Board"
		int buildOrder = -1;
		const GameObjBlueprint* dock = nullptr, * dockFoundation = nullptr, * cog = nullptr, * land = nullptr, * villageFoundation = nullptr;
		// the colonies: the Manor, the village, the cart, the route's commands and associations
		const GameObjBlueprint* manorFoundation = nullptr, * village = nullptr, * cart = nullptr, * peasant = nullptr;
		int canBuildManor = -1, palaceBuilding = -1;            // items
		int cogDropOff = -1, cartTransport = -1, chop = -1, mineGold = -1; // commands
		int overland = -1, oversea = -1;                         // associations
		// the way to a Castle from a Keep (Mechanics: "Convert Keep to Castle from Cathedral", "... from
		// Blackmass", "... from Observatory"): the Keep's alignment, the building that converts it, its
		// foundation and its build command; a Castle gives "Can Build Manor" ("Allow Manor")
		struct CastlePath { const char* keep; const char* building; const GameObjBlueprint* bp = nullptr, * foundation = nullptr; int command = -1; };
		CastlePath castlePath[3] = { { "Keep_Celestial", "Cathedral Stone" }, { "Keep_Pagan", "Black Mass" }, { "Keep_Tech", "Observatory" } };
		// and from the Manor to a Keep ("Convert Manor to Keep from Monastery", "... from Temple", "...
		// from Windmill"), the alignment chosen by which of them the game allows
		CastlePath keepPath[3] = { { "Manor", "Monastery" }, { "Manor", "Temple" }, { "Manor", "Windmill" } };
		void update(const GameSet* gs) {
			if (of == gs->serial)
				return;
			of = gs->serial;
			boat = gs->items.names.getIndex("I am a boat");
			maxWeight = gs->items.names.getIndex("Maximum Passenger Weight");
			curWeight = gs->items.names.getIndex("Current Passenger Weight");
			weight = gs->items.names.getIndex("Weight");
			cantBoard = gs->items.names.getIndex("I can't get into boats");
			canBuildDock = gs->items.names.getIndex("Can Build Dock Wood");
			range = gs->items.names.getIndex("Maximum Missile Range");
			aggression = gs->items.names.getIndex("Aggression");
			passengers = gs->associations.names.getIndex("Passengers");
			board = gs->commands.names.getIndex("Board Vessel");
			unload = gs->commands.names.getIndex("Unload Passengers");
			readyToBoard = gs->events.names.getIndex("I'm ready to Board");
			buildOrder = gs->orders.names.getIndex("Build");
			dock = gs->findBlueprint(Tags::GAMEOBJCLASS_BUILDING, "Dock Wood");
			dockFoundation = gs->findBlueprint(Tags::GAMEOBJCLASS_BUILDING, "Dock Wood Foundation");
			villageFoundation = gs->findBlueprint(Tags::GAMEOBJCLASS_BUILDING, "Village Wood Foundation");
			cog = gs->findBlueprint(Tags::GAMEOBJCLASS_CHARACTER, "Cog");
			land = gs->findBlueprint(Tags::GAMEOBJCLASS_CHARACTER, "Spearman");
			if (!land)
				land = gs->findBlueprint(Tags::GAMEOBJCLASS_CHARACTER, "Peasant");
			manorFoundation = gs->findBlueprint(Tags::GAMEOBJCLASS_BUILDING, "Manor Foundation");
			village = gs->findBlueprint(Tags::GAMEOBJCLASS_BUILDING, "Village Wood");
			cart = gs->findBlueprint(Tags::GAMEOBJCLASS_CHARACTER, "Cart");
			peasant = gs->findBlueprint(Tags::GAMEOBJCLASS_CHARACTER, "Peasant");
			canBuildManor = gs->items.names.getIndex("Can Build Manor");
			palaceBuilding = gs->items.names.getIndex("Palace Building");
			cogDropOff = gs->commands.names.getIndex("Cog Set Up Drop Off Dock");
			cartTransport = gs->commands.names.getIndex("Transport Resources from Village to Global Stocks");
			chop = gs->commands.names.getIndex("Chop Tree");
			mineGold = gs->commands.names.getIndex("Mine Gold");
			overland = gs->associations.names.getIndex("Overland Supply Depot");
			oversea = gs->associations.names.getIndex("Oversea Supply Depot");
			for (CastlePath* list : { castlePath, keepPath })
			for (int k = 0; k < 3; k++) {
				CastlePath& c = list[k];
				c.bp = gs->findBlueprint(Tags::GAMEOBJCLASS_BUILDING, c.building);
				c.foundation = gs->findBlueprint(Tags::GAMEOBJCLASS_BUILDING, std::string(c.building) + " Foundation");
				c.command = gs->commands.names.getIndex(std::string("Build ") + c.building);
			}
		}
		bool castleFoundation(const GameObjBlueprint* bp) const {
			for (const CastlePath* list : { castlePath, keepPath })
				for (int k = 0; k < 3; k++)
					if (bp && bp == list[k].foundation)
						return true;
			return false;
		}
	} g_ids;

	Server* Srv() { return Server::instance; }
	float Now() { return Srv()->timeManager.currentTime; }
	bool Trace() { static const bool t = getenv("WKB_TRACE_NAVAL") != nullptr; return t; }

	int TileX(float x) { return (int)std::floor(x / 5.0f); }
	Vector3 TileCentre(int tx, int tz)
	{
		Vector3 p(tx * 5.0f + 2.5f, 0.0f, tz * 5.0f + 2.5f);
		if (const Terrain* t = Srv()->terrain)
			p.y = t->getHeightEx(p.x, p.z, true);
		return p;
	}
	bool InMap(int tx, int tz)
	{
		const auto [w, h] = Srv()->terrain->getNumPlayableTiles();
		return tx >= 0 && tz >= 0 && tx < w && tz < h;
	}
	// a class's region at a place, or within 4 tiles of it (a palace's own tile may be a slope)
	int RegionNear(int cls, const Vector3& p, int* ox = nullptr, int* oz = nullptr)
	{
		const int cx = TileX(p.x), cz = TileX(p.z);
		for (int r = 0; r <= 4; r++)
			for (int dz = -r; dz <= r; dz++)
				for (int dx = -r; dx <= r; dx++) {
					if (std::max(std::abs(dx), std::abs(dz)) != r || !InMap(cx + dx, cz + dz))
						continue;
					const int reg = MovementController::terrainRegion(cls, cx + dx, cz + dz);
					if (reg >= 0) {
						if (ox) *ox = cx + dx;
						if (oz) *oz = cz + dz;
						return reg;
					}
				}
		return -1;
	}

	// A shore: a tile the boat class can stand on (in boatRegion, any if < 0) next to one of the
	// land class's region. The nearest to `from` within maxR tiles.
	struct Shore { bool ok = false; int wx = 0, wz = 0, lx = 0, lz = 0, boatRegion = -1; Vector3 water, land; };
	bool ShoreTile(int boatCls, int boatRegion, int landCls, int landRegion, int x, int z, Shore& s)
	{
		if (!InMap(x, z) || MovementController::isTileBlockedFast(boatCls, x, z))
			return false;
		const int br = MovementController::terrainRegion(boatCls, x, z);
		if (br < 0 || (boatRegion >= 0 && br != boatRegion))
			return false;
		for (int dz = -1; dz <= 1; dz++)
			for (int dx = -1; dx <= 1; dx++)
				if ((dx || dz) && InMap(x + dx, z + dz) && MovementController::terrainRegion(landCls, x + dx, z + dz) == landRegion) {
					s.ok = true;
					s.wx = x; s.wz = z; s.lx = x + dx; s.lz = z + dz;
					s.boatRegion = br;
					s.water = TileCentre(x, z);
					s.land = TileCentre(x + dx, z + dz);
					return true;
				}
		return false;
	}
	Shore ShoreNear(int boatCls, int boatRegion, int landCls, int landRegion, const Vector3& from, int maxR)
	{
		Shore best;
		if (landRegion < 0)
			return best;
		const int fx = TileX(from.x), fz = TileX(from.z);
		int bd = -1, lastRing = maxR;
		for (int r = 0; r <= lastRing; r++) {
			for (int z = fz - r; z <= fz + r; z++)
				for (int x = fx - r; x <= fx + r; x++) {
					if (std::max(std::abs(x - fx), std::abs(z - fz)) != r)
						continue;
					Shore s;
					if (!ShoreTile(boatCls, boatRegion, landCls, landRegion, x, z, s))
						continue;
					const int d = (x - fx) * (x - fx) + (z - fz) * (z - fz);
					if (bd < 0 || d < bd) { bd = d; best = s; }
				}
			if (bd >= 0 && lastRing == maxR)
				lastRing = std::min(maxR, (int)std::ceil(std::sqrt((float)bd)));
		}
		return best;
	}

	// the walk's length in metres over the land class's region from a to b (8-connected), -1 when
	// b isn't in a's region
	float LandPathLength(int cls, const Vector3& a, const Vector3& b)
	{
		int ax, az, bx, bz;
		const int ra = RegionNear(cls, a, &ax, &az), rb = RegionNear(cls, b, &bx, &bz);
		if (ra < 0 || ra != rb)
			return -1.0f;
		const auto [w, h] = Srv()->terrain->getNumPlayableTiles();
		std::vector<int> dist((size_t)w * h, INT32_MAX);
		using Node = std::pair<int, int>;
		std::priority_queue<Node, std::vector<Node>, std::greater<Node>> open;
		dist[(size_t)az * w + ax] = 0;
		open.push({ 0, az * w + ax });
		const int goal = bz * w + bx;
		while (!open.empty()) {
			const auto [d, i] = open.top();
			open.pop();
			if (d != dist[i])
				continue;
			if (i == goal)
				return d * 0.5f; // (10 a tile = 5 m)
			const int x = i % w, z = i / w;
			for (int dz = -1; dz <= 1; dz++)
				for (int dx = -1; dx <= 1; dx++) {
					if (!dx && !dz)
						continue;
					const int nx = x + dx, nz = z + dz;
					if (nx < 0 || nz < 0 || nx >= w || nz >= h || MovementController::terrainRegion(cls, nx, nz) != ra)
						continue;
					const int nd = d + (dx && dz ? 14 : 10);
					if (nd < dist[(size_t)nz * w + nx]) {
						dist[(size_t)nz * w + nx] = nd;
						open.push({ nd, nz * w + nx });
					}
				}
		}
		return -1.0f;
	}

	std::vector<ServerGameObject*> Players()
	{
		std::vector<ServerGameObject*> list;
		if (CommonGameObject* level = Srv()->getLevel())
			for (const auto& [type, children] : level->children)
				if (type.bpClass() == Tags::GAMEOBJCLASS_PLAYER)
					for (CommonGameObject* c : children)
						if (!((ServerGameObject*)c)->deleted)
							list.push_back((ServerGameObject*)c);
		std::sort(list.begin(), list.end(), [](ServerGameObject* a, ServerGameObject* b) { return a->id < b->id; });
		return list;
	}

	// everything under the player (through armies, cities, towns, formations), by id
	void Walk(ServerGameObject* o, std::vector<ServerGameObject*>& out)
	{
		for (const auto& [type, children] : o->children)
			for (CommonGameObject* c : children) {
				auto* u = (ServerGameObject*)c;
				if (u->deleted)
					continue;
				out.push_back(u);
				if (u->blueprint->bpClass != Tags::GAMEOBJCLASS_CHARACTER && u->blueprint->bpClass != Tags::GAMEOBJCLASS_BUILDING)
					Walk(u, out);
			}
	}

	ServerGameObject* Obj(uint32_t id);
	ServerGameObject* Home(ServerGameObject* player)
	{
		// the first palace the layer saw (a colony's Manor, found first by the scripts' finder, would
		// have moved the home, and the navy's shore with it, to the island)
		Naval::State& st = player->aiController.naval;
		if (ServerGameObject* h = Obj(st.homeId); h && h->getPlayer() == player)
			return h;
		if (ServerGameObject* p = AIUtil::Palace(player)) {
			st.homeId = p->id;
			return p;
		}
		std::vector<ServerGameObject*> all;
		Walk(player, all);
		ServerGameObject* best = nullptr;
		for (ServerGameObject* o : all)
			if (o->blueprint->bpClass == Tags::GAMEOBJCLASS_BUILDING && (!best || o->id < best->id))
				best = o;
		return best;
	}

	bool IsDock(const ServerGameObject* o)
	{
		return o && !o->deleted && (o->blueprint == g_ids.dock || o->blueprint == g_ids.dockFoundation);
	}

	// the Co-ordinator, the handler of the player's commissions that place buildings (its reaction
	// to a commissioned foundation sends builders), as the first barracks has it
	ServerGameObject* Coordinator(ServerGameObject* player)
	{
		for (const CommissionInstance& ci : player->aiController.commissionInstances)
			if (ci.handlerObject && !ci.handlerObject->deleted && !ci.blueprint->buildingRequirements.empty())
				return ci.handlerObject.get();
		return nullptr;
	}

	// the "Spawn <type>" the building offers, its conditions met, and the player can pay
	bool CanTrain(ServerGameObject* player, ServerGameObject* b, const std::string& typeName)
	{
		const GameSet* gs = Srv()->gameSet.get();
		const GameObjBlueprint* type = gs->findBlueprint(Tags::GAMEOBJCLASS_CHARACTER, typeName);
		if (!type || !b || b->deleted || b->orderConfig.getCurrentOrder() || !player->canAffordObject(type))
			return false;
		for (const Command* cmd : b->blueprint->offeredCommands) {
			if (gs->commands.getString(cmd) != "Spawn " + typeName)
				continue;
			SrvScriptContext cctx(Srv(), b);
			auto _s = cctx.selectedObject.change(b);
			auto holds = [&](GSCondition* cond) { return cond->test->booleval(&cctx); };
			return std::all_of(cmd->conditionsImpossible.begin(), cmd->conditionsImpossible.end(), holds)
				&& std::all_of(cmd->conditionsWait.begin(), cmd->conditionsWait.end(), holds) && cmd->canBeExecuted(b);
		}
		return false;
	}

	float PassengerWeight(const ServerGameObject* u) { return g_ids.weight >= 0 ? std::max(u->getItem(g_ids.weight), 1.0f) : 1.0f; }
	bool CanBoard(const ServerGameObject* u)
	{
		return u && !u->deleted && u->isInteractable() && u->blueprint->bpClass == Tags::GAMEOBJCLASS_CHARACTER && !Naval::IsBoat(u)
			&& !(g_ids.cantBoard >= 0 && u->getItem(g_ids.cantBoard) > 0.0f);
	}
	bool Aboard(ServerGameObject* ship, ServerGameObject* u)
	{
		if (!ship || !u || g_ids.passengers < 0 || u->disableCount <= 0)
			return false;
		auto it = ship->associates.find(g_ids.passengers);
		return it != ship->associates.end() && it->second.count(SrvGORef(u));
	}
	int PassengersOf(ServerGameObject* ship)
	{
		if (!ship || g_ids.passengers < 0)
			return 0;
		auto it = ship->associates.find(g_ids.passengers);
		return it == ship->associates.end() ? 0 : (int)it->second.size();
	}

	float Aggression(ServerGameObject* player)
	{
		return g_ids.aggression >= 0 ? std::clamp(player->getItem(g_ids.aggression) / 100.0f, 0.0f, 1.0f) : 0.5f;
	}

	// the safe shore to land at for a target: in the target's land region, in the sea the ship
	// sails, within 350 m of it; the nearest, but away from enemies and towers (a tower or soldier
	// within 60 m costs as much as 40 m more of walk) and not under the target's own walls
	Naval::Ferry LandingFor(ServerGameObject* player, int boatRegion, const Vector3& at, Naval::Ferry f, bool& ok, const Vector3* avoid = nullptr)
	{
		ok = false;
		const int boatCls = MovementController::movementClass(g_ids.cog), landCls = MovementController::movementClass(g_ids.land);
		const int landRegion = RegionNear(landCls, at);
		if (landRegion < 0)
			return f;
		const int fx = TileX(at.x), fz = TileX(at.z);
		std::vector<Shore> found;
		for (int r = 0; r <= 70 && found.size() < 400; r++)
			for (int z = fz - r; z <= fz + r; z++)
				for (int x = fx - r; x <= fx + r; x++) {
					if (std::max(std::abs(x - fx), std::abs(z - fz)) != r)
						continue;
					Shore s;
					if (ShoreTile(boatCls, boatRegion, landCls, landRegion, x, z, s))
						found.push_back(s);
				}
		if (found.empty())
			return f;
		// a sample 15 m apart at least, nearest first (not by a landing that failed)
		std::vector<Shore> sample;
		for (const Shore& s : found) {
			if (avoid && (s.water - *avoid).sqlen2xz() < 25.0f * 25.0f)
				continue;
			if (sample.size() >= 40)
				break;
			if (std::none_of(sample.begin(), sample.end(), [&](const Shore& o) { return (o.water - s.water).sqlen2xz() < 15.0f * 15.0f; }))
				sample.push_back(s);
		}
		float best = 1e30f;
		for (const Shore& s : sample) {
			const float d = (s.land - at).len2xz();
			const std::vector<ServerGameObject*> enemies = AIUtil::EnemiesNear(player, s.land, 60.0f, true);
			float danger = 0.0f;
			for (ServerGameObject* e : enemies)
				danger += e->blueprint->bpClass == Tags::GAMEOBJCLASS_BUILDING ? 120.0f : 40.0f;
			const float score = d + danger + (d < 50.0f ? (50.0f - d) * 2.0f : 0.0f);
			if (score < best) {
				best = score;
				f.landing = s.water;
				ok = true;
			}
		}
		return f;
	}

	void Log(ServerGameObject* player, const char* fmt, ...)
	{
		if (!Trace())
			return;
		char buf[512];
		va_list ap;
		va_start(ap, fmt);
		vsnprintf(buf, sizeof(buf), fmt, ap);
		va_end(ap);
		printf("NAVAL t=%.0f player %u: %s\n", Now(), player->id, buf);
	}

	ServerGameObject* Obj(uint32_t id)
	{
		ServerGameObject* o = id ? Srv()->findObject(id) : nullptr;
		return o && !o->deleted && !(o->flags & ServerGameObject::fTerminated) ? o : nullptr;
	}

	// ---- the map ----
	void ReadMap(ServerGameObject* player, Naval::State& st)
	{
		const Naval::Need was = st.need;
		st.need = Naval::NEED_NONE;
		st.enemy = 0;
		ServerGameObject* home = Home(player);
		if (!home || !g_ids.cog || !g_ids.land) {
			if (was == Naval::NEED_UNKNOWN)
				Log(player, "the map: no home");
			return;
		}
		st.home = home->position;
		const int boatCls = MovementController::movementClass(g_ids.cog), landCls = MovementController::movementClass(g_ids.land);
		const int homeRegion = RegionNear(landCls, st.home);
		if (homeRegion < 0) {
			if (was == Naval::NEED_UNKNOWN)
				Log(player, "the map: the home (%.0f, %.0f) on no land region", st.home.x, st.home.z);
			return;
		}
		const Shore hs = ShoreNear(boatCls, -1, landCls, homeRegion, st.home, 100);
		if (!hs.ok) {
			if (was == Naval::NEED_UNKNOWN) {
				// (why: how many tiles round home a boat could stand on at all)
				int boatTiles = 0, wet = 0;
				const int cx = TileX(st.home.x), cz = TileX(st.home.z);
				for (int z = cz - 100; z <= cz + 100; z++)
					for (int x = cx - 100; x <= cx + 100; x++)
						if (InMap(x, z)) {
							boatTiles += !MovementController::isTileBlockedFast(boatCls, x, z);
							const TerrainTile* tt = Srv()->terrain->getPlayableTile(x, z);
							wet += tt && tt->fullOfWater;
						}
				std::map<std::string, int> groups;
				for (int z = cz - 100; z <= cz + 100; z++)
					for (int x = cx - 100; x <= cx + 100; x++)
						if (InMap(x, z))
							if (const TerrainTile* tt = Srv()->terrain->getPlayableTile(x, z); tt && tt->fullOfWater && tt->texture && tt->texture->grp) {
								const auto& gs = Srv()->gameSet->associatedTileTexGroups;
								auto it = gs.find(tt->texture->grp->name);
								groups[tt->texture->grp->name + "=" + (it == gs.end() ? std::string("none") : Srv()->gameSet->terrains.getString(it->second))]++;
							}
				std::string list;
				for (const auto& [g, n] : groups)
					list += " " + g + ":" + std::to_string(n);
				Log(player, "the map: no shore within 500 m of home (%.0f, %.0f): %d tiles under water, %d a boat can stand on; under water:%s", st.home.x, st.home.z, wet, boatTiles, list.c_str());
			}
			return; // no sea by this home
		}
		float bestScore = 1e30f;
		for (ServerGameObject* e : Players()) {
			if (e == player || !AIUtil::IsEnemy(player, e) || (e->flags & ServerGameObject::fTerminated))
				continue;
			ServerGameObject* eh = Home(e);
			if (!eh)
				continue;
			const int er = RegionNear(landCls, eh->position);
			if (er < 0)
				continue;
			const Shore es = ShoreNear(boatCls, hs.boatRegion, landCls, er, eh->position, 100);
			if (!es.ok)
				continue; // that sea doesn't reach it
			const float voyage = (hs.land - st.home).len2xz() + (es.water - hs.water).len2xz() * 1.2f + (eh->position - es.land).len2xz();
			Naval::Need need = Naval::NEED_NONE;
			if (er != homeRegion)
				need = Naval::NEED_ONLY;
			else if ((es.water - hs.water).len2xz() >= 150.0f) {
				const float walk = LandPathLength(landCls, st.home, eh->position);
				if (walk > 2.0f * voyage)
					need = Naval::NEED_SHORTER;
			}
			if (need == Naval::NEED_NONE)
				continue;
			const float score = (need == Naval::NEED_ONLY ? 0.0f : 1e6f) + voyage;
			if (score < bestScore) {
				bestScore = score;
				st.need = need;
				st.enemy = e->id;
				st.homeShore = hs.water;
				st.enemyShore = es.water;
			}
		}
		if (st.need != was)
			Log(player, "the map: %s (enemy %u, home shore (%.0f, %.0f), theirs (%.0f, %.0f))",
				st.need == Naval::NEED_ONLY ? "the sea is the only way" : st.need == Naval::NEED_SHORTER ? "the sea is the shorter way" : "no sea between",
				st.enemy, st.homeShore.x, st.homeShore.z, st.enemyShore.x, st.enemyShore.z);
	}

	// a foundation stamped down for the player and its builders sent: through the Co-ordinator
	// (its reaction to a commissioned foundation), else the two nearest peasants
	// the peasants building a foundation now
	int BuildersOf(ServerGameObject* player, ServerGameObject* foundation)
	{
		int n = 0;
		for (ServerGameObject* u : AIUtil::Peasants(player)) {
			Order* o = u->orderConfig.getCurrentOrder();
			Task* t = o ? o->getCurrentTask() : nullptr;
			n += t && t->target.get() == foundation;
		}
		return n;
	}
	// the two nearest peasants to a foundation, with the "Build" order (the Co-ordinator's reaction
	// to a commissioned foundation only sends builders for its own commissions: the dock laid by
	// this layer stood unbuilt for 15 minutes)
	void SendBuilders(ServerGameObject* player, ServerGameObject* foundation, int want)
	{
		Naval::State& st = player->aiController.naval;
		if (g_ids.buildOrder < 0 || !foundation)
			return;
		std::vector<ServerGameObject*> peasants = AIUtil::Peasants(player);
		const Vector3 p = foundation->position;
		std::sort(peasants.begin(), peasants.end(), [&](ServerGameObject* a, ServerGameObject* b) {
			const float da = (a->position - p).sqlen2xz(), db = (b->position - p).sqlen2xz();
			return da != db ? da < db : a->id < b->id; });
		int sent = 0;
		for (ServerGameObject* u : peasants) {
			if (sent >= want)
				break;
			if (!u->isInteractable() || player->aiController.navalUnits.count(u->id))
				continue;
			if (u->orderConfig.addOrder(&Srv()->gameSet->orders[g_ids.buildOrder], Tags::ORDERASSIGNMODE_FORGET_EVERYTHING_ELSE, foundation)) {
				// (out of the work orders' hands until it is built: the scripts took Belisar's
				// builders off his village again and again, the play of 2026-11-19)
				player->aiController.ministryHeld.insert(u->id);
				st.builders[u->id] = { foundation->id, Now() };
				sent++;
			}
		}
		Log(player, "%d builders sent to %s %u", sent, foundation->blueprint->name.c_str(), foundation->id);
	}

	ServerGameObject* LayFoundation(ServerGameObject* player, const GameObjBlueprint* bp, const Vector3& p, float yaw)
	{
		Server* srv = Srv();
		ServerGameObject* foundation = srv->stampdownObject(bp, player, p, Vector3(0, yaw, 0), false, true);
		if (!foundation)
			return nullptr;
		if (ServerGameObject* co = Coordinator(player))
			co->sendEvent(Tags::PDEVENT_ON_COMMISSIONED, foundation);
		SendBuilders(player, foundation, 2);
		return foundation;
	}

	// the layer's foundations (the dock, a village) that nobody builds: builders again, each minute
	void KeepBuilding(ServerGameObject* player, Naval::State& st)
	{
		// the builders held: back to the scripts once their foundation is built or gone, or after 4 minutes
		for (auto it = st.builders.begin(); it != st.builders.end();) {
			ServerGameObject* f = Obj(it->second.first);
			const bool done = !f || (f->blueprint != g_ids.dockFoundation && f->blueprint != g_ids.villageFoundation && !g_ids.castleFoundation(f->blueprint))
				|| Now() - it->second.second > 240.0f;
			if (done) {
				// (a colony's settler stays held: the colony works it)
				bool settler = false;
				for (const Naval::Colony& c : st.colonies)
					settler = settler || std::find(c.settlers.begin(), c.settlers.end(), it->first) != c.settlers.end();
				if (!settler)
					player->aiController.ministryHeld.erase(it->first);
				it = st.builders.erase(it);
			}
			else
				++it;
		}
		if (Now() < st.nextBuildersCheck)
			return;
		st.nextBuildersCheck = Now() + 60.0f;
		std::vector<ServerGameObject*> all;
		Walk(player, all);
		for (ServerGameObject* o : all)
			if ((o->blueprint == g_ids.dockFoundation || (o->blueprint == g_ids.villageFoundation && st.villagesLaid > 0)) && BuildersOf(player, o) == 0)
				SendBuilders(player, o, 2);
			else if (o->id == st.castleFoundation && g_ids.castleFoundation(o->blueprint) && BuildersOf(player, o) < 3)
				SendBuilders(player, o, 3 - BuildersOf(player, o));
	}

	// A village where the sea is the only way and the scripts have none 4 minutes in (it unlocks
	// the dock; on the carved 4 Peaks one General's scripts never laid one in 20 minutes): rings
	// round the palace 40 to 150 m out, by the game's placement rule
	void LayVillage(ServerGameObject* player, Naval::State& st)
	{
		if (!g_ids.villageFoundation || st.need != Naval::NEED_ONLY || Now() - st.start < 240.0f || Now() < st.nextVillageTry)
			return;
		if (g_ids.canBuildDock < 0 || player->getItem(g_ids.canBuildDock) > 0.0f)
			return;
		st.nextVillageTry = Now() + 30.0f;
		std::vector<ServerGameObject*> all;
		Walk(player, all);
		for (ServerGameObject* o : all)
			if (o->blueprint == g_ids.villageFoundation) {
				// (one under way, the scripts' or ours: builders if nobody builds it)
				if (BuildersOf(player, o) == 0) {
					Log(player, "the village foundation %u stands unbuilt", o->id);
					SendBuilders(player, o, 2);
				}
				return;
			}
		if (!player->canAffordObject(g_ids.villageFoundation)) {
			Log(player, "no village yet: can't afford it (%.0f wood)", player->getItem(Tags::PDITEM_WOOD));
			return;
		}
		int refused[16] = {}, tried = 0;
		const int landCls = MovementController::movementClass(g_ids.land);
		const int homeRegion = RegionNear(landCls, st.home);
		for (float r = 40.0f; r <= 150.0f; r += 10.0f)
			for (int k = 0; k < 16; k++) {
				const float a = k * 6.2831853f / 16.0f;
				Vector3 p(st.home.x + r * std::sin(a), 0.0f, st.home.z + r * std::cos(a));
				p = TileCentre(TileX(p.x), TileX(p.z));
				if (!InMap(TileX(p.x), TileX(p.z)) || MovementController::terrainRegion(landCls, TileX(p.x), TileX(p.z)) != homeRegion)
					continue;
				const StampdownPlan plan = StampdownPlan::getStampdownPlan(Srv(), player, g_ids.villageFoundation, p, Vector3(0, 0, 0));
				tried++;
				if (plan.status != StampdownPlan::Status::Valid) {
					refused[std::clamp((int)plan.status, 0, 15)]++;
					continue;
				}
				if (LayFoundation(player, g_ids.villageFoundation, p, 0.0f)) {
					st.villagesLaid++;
					printf("AI %u: a village laid down at t=%.0f (%.0f, %.0f), for a dock\n", player->id, Now(), p.x, p.z);
				}
				return;
			}
		std::string why;
		for (int i = 0; i < 16; i++)
			if (refused[i])
				why += " " + std::to_string(i) + ":" + std::to_string(refused[i]);
		Log(player, "no site for a village among %d (refused by status%s)", tried, why.c_str());
	}

	// ---- the dock ----
	ServerGameObject* FindDock(ServerGameObject* player, Naval::State& st, bool builtOnly)
	{
		if (ServerGameObject* d = Obj(st.dock); d && IsDock(d) && d->getPlayer() == player && (!builtOnly || d->blueprint == g_ids.dock))
			return d;
		std::vector<ServerGameObject*> all;
		Walk(player, all);
		ServerGameObject* best = nullptr;
		for (ServerGameObject* o : all)
			if (IsDock(o) && (!builtOnly || o->blueprint == g_ids.dock)
				&& (!best || (best->blueprint != g_ids.dock && o->blueprint == g_ids.dock) || (o->blueprint == best->blueprint && o->id < best->id)))
				best = o;
		if (best && (!builtOnly || !Obj(st.dock)))
			st.dock = best->id;
		return best;
	}

	void LayDock(ServerGameObject* player, Naval::State& st)
	{
		if (!g_ids.dockFoundation || FindDock(player, st, false))
			return;
		if (Now() < st.nextDockTry)
			return;
		st.nextDockTry = Now() + 30.0f;
		if (g_ids.canBuildDock >= 0 && player->getItem(g_ids.canBuildDock) <= 0.0f) {
			Log(player, "no dock yet: no village");
			return;
		}
		if (!player->canAffordObject(g_ids.dockFoundation)) {
			Log(player, "no dock yet: %.0f wood", player->getItem(Tags::PDITEM_WOOD));
			return;
		}
		Server* srv = Srv();
		const int landCls = MovementController::movementClass(g_ids.land);
		const int homeRegion = RegionNear(landCls, st.home);
		const int boatCls = MovementController::movementClass(g_ids.cog);
		const int seaRegion = MovementController::terrainRegion(boatCls, TileX(st.homeShore.x), TileX(st.homeShore.z));
		// the sites: water tiles of the home sea within 12 tiles of the home shore that have the
		// home's land within 2 tiles; each turned four ways; the nearest to home that may be built
		const int cx = TileX(st.homeShore.x), cz = TileX(st.homeShore.z);
		struct Site { float d; Vector3 p; float yaw; };
		std::vector<Site> sites;
		for (int dz = -12; dz <= 12; dz++)
			for (int dx = -12; dx <= 12; dx++) {
				const int x = cx + dx, z = cz + dz;
				if (!InMap(x, z) || MovementController::terrainRegion(boatCls, x, z) != seaRegion)
					continue;
				bool nearLand = false;
				for (int k = -2; k <= 2 && !nearLand; k++)
					for (int l = -2; l <= 2 && !nearLand; l++)
						nearLand = InMap(x + l, z + k) && MovementController::terrainRegion(landCls, x + l, z + k) == homeRegion;
				if (!nearLand)
					continue;
				const Vector3 p = TileCentre(x, z);
				sites.push_back({ (p - st.home).len2xz(), p, 0.0f });
			}
		std::sort(sites.begin(), sites.end(), [](const Site& a, const Site& b) { return a.d != b.d ? a.d < b.d : (a.p.x != b.p.x ? a.p.x < b.p.x : a.p.z < b.p.z); });
		int refused[16] = {};
		for (const Site& s : sites) {
			for (int q = 0; q < 4; q++) {
				const float yaw = q * 1.57079633f;
				const StampdownPlan plan = StampdownPlan::getStampdownPlan(srv, player, g_ids.dockFoundation, s.p, Vector3(0, yaw, 0));
				if (plan.status != StampdownPlan::Status::Valid) {
					refused[std::clamp((int)plan.status, 0, 15)]++;
					if (Trace() && refused[std::clamp((int)plan.status, 0, 15)] == 1) {
						std::string bad;
						for (const StampdownPlan::TileVerdict& v : plan.tiles)
							if (!v.ok) {
								const TerrainTile* tt = srv->terrain->getPlayableTile(v.x, v.z);
								bad += " (" + std::to_string(v.x - TileX(s.p.x)) + "," + std::to_string(v.z - TileX(s.p.z)) + ")"
									+ (tt && tt->texture && tt->texture->grp ? tt->texture->grp->name : std::string("?")) + (tt && tt->fullOfWater ? "~" : "");
							}
						Log(player, "dock at (%.0f, %.0f) turned %d refused (%d):%s", s.p.x, s.p.z, q, (int)plan.status, bad.c_str());
					}
					continue;
				}
				ServerGameObject* foundation = LayFoundation(player, g_ids.dockFoundation, s.p, yaw);
				if (!foundation)
					return;
				st.dock = foundation->id;
				st.docksLaid++;
				printf("AI %u: a dock laid down at t=%.0f (%.0f, %.0f), the sea %s\n", player->id, Now(), s.p.x, s.p.z,
					st.need == Naval::NEED_ONLY ? "the only way to the enemy" : "the shorter way");
				return;
			}
		}
		std::string why;
		for (int i = 0; i < 16; i++)
			if (refused[i])
				why += " " + std::to_string(i) + ":" + std::to_string(refused[i]);
		Log(player, "no site for a dock near (%.0f, %.0f) among %zu (refused by status%s)", st.homeShore.x, st.homeShore.z, sites.size(), why.c_str());
	}

	// ---- the ships ----
	void TrainShips(ServerGameObject* player, Naval::State& st, int transports, int warships, bool aggressive)
	{
		ServerGameObject* dock = FindDock(player, st, true);
		if (!dock || dock->orderConfig.getCurrentOrder())
			return;
		const std::vector<ServerGameObject*> ships = Naval::Ships(player);
		int haveT = 0, haveW = 0, mangonels = 0;
		for (ServerGameObject* s : ships) {
			if (Naval::IsTransport(s)) haveT++;
			else haveW++;
			mangonels += s->blueprint->name == "Mangonel Boat";
		}
		const char* type = nullptr;
		if (haveT < transports && CanTrain(player, dock, "Cog"))
			type = "Cog";
		else if (haveW < warships) {
			// a Mangonel Boat (the coast's buildings from 90 m) once there are war boats, if it may be
			// built; else Arrow Boats (they shoot what is on the shore, and other boats)
			if (haveW >= 1 && mangonels == 0 && (aggressive || haveW >= 2) && CanTrain(player, dock, "Mangonel Boat"))
				type = "Mangonel Boat";
			else if (CanTrain(player, dock, "Arrow Boat"))
				type = "Arrow Boat";
			else if (CanTrain(player, dock, "Fire Boat"))
				type = "Fire Boat";
		}
		st.lastTrainWant = (haveT < transports || haveW < warships) ? (type ? 1 : 2) : 0;
		if (type && AIUtil::Train(player, dock, type)) {
			st.shipsOrdered++;
			if (strcmp(type, "Cog") != 0)
				st.warshipsOrdered++;
			Log(player, "a %s ordered at the dock %u (%d transports, %d war boats)", type, dock->id, haveT, haveW);
		}
	}

	bool InFerry(const Naval::State& st, uint32_t ship)
	{
		return std::any_of(st.ferries.begin(), st.ferries.end(), [&](const Naval::Ferry& f) { return f.ship == ship && f.state != Naval::Ferry::DONE; });
	}

	// ---- the ferries ----
	void Release(ServerGameObject* player, Naval::State& st, const Naval::Ferry& f)
	{
		for (uint32_t id : f.passengers)
			player->aiController.navalUnits.erase(id);
	}

	void StepFerry(ServerGameObject* player, Naval::State& st, Naval::Ferry& f)
	{
		Server* srv = Srv();
		const GameSet* gs = srv->gameSet.get();
		const float now = Now();
		ServerGameObject* ship = Obj(f.ship);
		std::vector<ServerGameObject*> alive;
		for (uint32_t id : f.passengers)
			if (ServerGameObject* u = Obj(id))
				alive.push_back(u);
		auto setState = [&](Naval::Ferry::State s) { f.state = s; f.stateSince = now; };
		if (!ship && f.state <= Naval::Ferry::LANDING) {
			// sunk (its passengers at sea drowned with it), or lost before it sailed: the rest home
			Log(player, "ferry %u lost its ship (%zu passengers left)", f.id, alive.size());
			AIUtil::SendHome(player, alive);
			Release(player, st, f);
			setState(Naval::Ferry::DONE);
			return;
		}
		switch (f.state) {
		case Naval::Ferry::BOARDING: {
			int aboard = 0, waiting = 0;
			for (ServerGameObject* u : alive) {
				if (Aboard(ship, u))
					aboard++;
				else {
					waiting++;
					// A boarder standing still within 15 m of its ship once the ship has stopped at the
					// shore: it reports ready to board itself (the game set's "I'm ready to Board", which
					// its "Move To" sends only once at its marker; the group's arrival spread stopped
					// five of six spearmen 9 m short of theirs and they stood there)
					// (35 m: a cog stops 30-32 m off some shores, and its peasants waited there until the
					// ferry gave up, 3 Isles 2026-11-22)
					if (g_ids.readyToBoard >= 0 && !ship->movementController.isMoving() && !u->movementController.isMoving()
						&& (u->position - ship->position).sqlen2xz() < 35.0f * 35.0f && now - f.stateSince > 3.0f)
						ship->sendEvent(g_ids.readyToBoard, u);
				}
			}
			// all aboard; or after 90 s those who are (the rest home)
			const bool timeUp = now - f.stateSince > 90.0f;
			if (timeUp && Trace())
				for (ServerGameObject* u : alive)
					if (!Aboard(ship, u)) {
						Order* o = u->orderConfig.getCurrentOrder();
						Log(player, "  not aboard: %u %s at %.0f m from the ship (moving %d): %s", u->id, u->blueprint->name.c_str(), (u->position - ship->position).len2xz(),
							(int)u->movementController.isMoving(), o ? gs->orders.getString(o->blueprint->bpid).c_str() : "idle");
					}
			if ((waiting == 0 && aboard > 0) || (timeUp && aboard > 0)) {
				std::vector<ServerGameObject*> left;
				std::vector<uint32_t> kept;
				for (ServerGameObject* u : alive) {
					if (Aboard(ship, u))
						kept.push_back(u->id);
					else {
						left.push_back(u);
						player->aiController.navalUnits.erase(u->id);
					}
				}
				if (!left.empty())
					AIUtil::SendHome(player, left);
				f.passengers = kept;
				bool ok = false;
				ServerGameObject* target = Obj(f.target);
				if (target)
					f.targetAt = target->position;
				const int boatCls = MovementController::movementClass(ship->blueprint);
				const int sea = MovementController::terrainRegion(boatCls, TileX(ship->position.x), TileX(ship->position.z));
				f = LandingFor(player, sea, f.targetAt, f, ok);
				if (!ok) {
					Log(player, "ferry %u: no shore to land at near (%.0f, %.0f)", f.id, f.targetAt.x, f.targetAt.z);
					if (g_ids.unload >= 0)
						gs->commands[g_ids.unload].execute(ship, nullptr, Tags::ORDERASSIGNMODE_FORGET_EVERYTHING_ELSE);
					Release(player, st, f);
					setState(Naval::Ferry::DONE);
					return;
				}
				AIController::moveUnitsTo({ ship }, player, f.landing);
				setState(Naval::Ferry::SAILING);
				st.ferriesSent++;
				printf("AI %u: ferry %u sails with %d aboard at t=%.0f, to land at (%.0f, %.0f) for %s %u\n", player->id, f.id, aboard, now,
					f.landing.x, f.landing.z, target ? target->blueprint->name.c_str() : "a place", f.target);
			}
			else if (timeUp || alive.empty()) {
				Log(player, "ferry %u: nobody boarded", f.id);
				if (Trace())
					for (ServerGameObject* u : alive) {
						Order* o = u->orderConfig.getCurrentOrder();
						Log(player, "  %u %s at %.0f m from the ship: %s", u->id, u->blueprint->name.c_str(), (u->position - ship->position).len2xz(),
							o ? gs->orders.getString(o->blueprint->bpid).c_str() : "idle");
					}
				AIUtil::SendHome(player, alive);
				Release(player, st, f);
				setState(Naval::Ferry::DONE);
			}
			else if (now - f.stateSince > 20.0f && now >= f.nextNudge) {
				// (those still idle are told again, every 20 s: an order lost to another's)
				f.nextNudge = now + 20.0f;
				for (ServerGameObject* u : alive)
					if (!Aboard(ship, u) && !u->orderConfig.getCurrentOrder() && g_ids.board >= 0)
						gs->commands[g_ids.board].execute(u, ship, Tags::ORDERASSIGNMODE_FORGET_EVERYTHING_ELSE);
			}
			// The boarders wait at their landing point on the shore ("Move to Landing Point and Await
			// Vessel", then "Wait for Instructions from Vessel") for the ship, which stayed where it
			// was: peasants 150-190 m off stood there until the ferry gave up (3 Isles, 2026-11-22).
			// The ship sails to the water nearest them (every 10 s while it stands 25 m or more away).
			if (waiting > 0 && !ship->movementController.isMoving() && now - f.stateSince > 6.0f && now >= f.nextSail) {
				f.nextSail = now + 10.0f;
				Vector3 c;
				int n = 0;
				for (ServerGameObject* u : alive)
					if (!Aboard(ship, u) && !u->movementController.isMoving()) {
						c += u->position;
						n++;
					}
				if (n > 0) {
					c = c * (1.0f / n);
					if ((c - ship->position).len2xz() > 25.0f) {
						// (the water by their shore in the ship's own sea: the nearest valid spot for a
						// boat was in water it can't reach, and the cog stood 150-185 m off while it was
						// sent there every 10 s, 3 Isles 2026-11-22)
						const int boatCls = MovementController::movementClass(ship->blueprint);
						const int sea = RegionNear(boatCls, ship->position);
						int landCls = -1;
						for (ServerGameObject* u : alive)
							if (!Aboard(ship, u)) {
								landCls = MovementController::movementClass(u->blueprint);
								break;
							}
						const int land = landCls >= 0 ? RegionNear(landCls, c) : -1;
						const Shore near = ShoreNear(boatCls, sea, landCls, land, c, 16);
						if (f.sails < 3 && near.ok && (near.water - ship->position).len2xz() > 8.0f) {
							f.sails++;
							AIController::moveUnitsTo({ ship }, player, near.water);
							Log(player, "ferry %u: the ship sails to its %d boarders at (%.0f, %.0f)", f.id, n, near.water.x, near.water.z);
						}
						else if ((c - ship->position).len2xz() > 40.0f) {
							// the ship can't come to them: they walk to the shore nearest the ship
							const Shore by = ShoreNear(boatCls, sea, landCls, land, ship->position, 30);
							if (by.ok) {
								std::vector<ServerGameObject*> walk;
								for (ServerGameObject* u : alive)
									if (!Aboard(ship, u) && !u->movementController.isMoving() && (u->position - by.land).len2xz() > 15.0f)
										walk.push_back(u);
								if (!walk.empty()) {
									AIController::moveUnitsTo(walk, player, by.land);
									Log(player, "ferry %u: %zu boarders walk to the shore by the ship at (%.0f, %.0f)", f.id, walk.size(), by.land.x, by.land.z);
								}
								if ((by.water - ship->position).len2xz() > 8.0f)
									AIController::moveUnitsTo({ ship }, player, by.water);
							}
						}
					}
				}
			}
			return;
		}
		case Naval::Ferry::SAILING: {
			const float d = (ship->position - f.landing).len2xz();
			const bool moving = ship->movementController.isMoving();
			if (d < 9.0f || (!moving && d < 30.0f && now - f.stateSince > 3.0f)) {
				if (g_ids.unload >= 0)
					gs->commands[g_ids.unload].execute(ship, nullptr, Tags::ORDERASSIGNMODE_FORGET_EVERYTHING_ELSE);
				setState(Naval::Ferry::LANDING);
				return;
			}
			if (!moving && now - f.stateSince > 5.0f && now >= f.nextNudge) {
				// stopped short (another ship, a sandbank): again (every 3 s), and after 4 minutes another shore
				f.nextNudge = now + 3.0f;
				if (now - f.stateSince > 240.0f) {
					bool ok = false;
					const int sea = MovementController::terrainRegion(MovementController::movementClass(ship->blueprint), TileX(ship->position.x), TileX(ship->position.z));
					const Vector3 failed = f.landing;
					Naval::Ferry g = LandingFor(player, sea, f.targetAt, f, ok, &failed);
					if (ok)
						f.landing = g.landing;
					f.stateSince = now;
				}
				AIController::moveUnitsTo({ ship }, player, f.landing);
			}
			return;
		}
		case Naval::Ferry::LANDING: {
			int ashore = 0, still = 0;
			for (ServerGameObject* u : alive) {
				if (u->isInteractable())
					ashore++;
				else
					still++;
			}
			if (still == 0) {
				f.landed = ashore;
				st.ferriesLanded++;
				st.unitsLanded += ashore;
				printf("AI %u: ferry %u landed %d at t=%.0f near (%.0f, %.0f)\n", player->id, f.id, ashore, now, f.landing.x, f.landing.z);
				// regroup at the landing: they gather at the first one ashore
				if (!alive.empty())
					AIController::moveUnitsTo(alive, player, alive.front()->position);
				setState(Naval::Ferry::ASHORE);
				return;
			}
			if (now - f.stateSince > 4.0f && now >= f.nextNudge) {
				// (the passengers can't stand next to the ship: again every 2 s, then a shore a little further)
				f.nextNudge = now + 2.0f;
				if (g_ids.unload >= 0)
					gs->commands[g_ids.unload].execute(ship, nullptr, Tags::ORDERASSIGNMODE_FORGET_EVERYTHING_ELSE);
				if (now - f.stateSince > 12.0f) {
					bool ok = false;
					const int sea = MovementController::terrainRegion(MovementController::movementClass(ship->blueprint), TileX(ship->position.x), TileX(ship->position.z));
					const Vector3 failed = f.landing;
					Naval::Ferry g = LandingFor(player, sea, f.targetAt, f, ok, &failed);
					if (ok && (g.landing - f.landing).sqlen2xz() > 25.0f) {
						f.landing = g.landing;
						AIController::moveUnitsTo({ ship }, player, f.landing);
						setState(Naval::Ferry::SAILING);
					}
					else
						f.stateSince = now;
				}
			}
			return;
		}
		case Naval::Ferry::ASHORE: {
			// 8 s to gather, then to the attack: the layer's own as one of the scripts' waves; a
			// mission's or a Lua AI's are theirs again
			if (now - f.stateSince < 8.0f && f.owner == Naval::Ferry::LAYER)
				return;
			if (f.owner == Naval::Ferry::LAYER && !alive.empty()) {
				ServerGameObject* target = Obj(f.target);
				if (!target || !AIUtil::IsEnemy(player, target)) {
					const std::vector<ServerGameObject*> near = AIUtil::EnemyBuildingsNear(player, alive.front()->position, 400.0f);
					target = near.empty() ? nullptr : near.front();
				}
				ServerGameObject* home = AIUtil::HomeArmy(player);
				if (target && player->aiController.launchWave(alive, target, home ? home : player))
					printf("AI %u: ferry %u's %zu attack %s %u at t=%.0f\n", player->id, f.id, alive.size(), target->blueprint->name.c_str(), target->id, now);
				else if (target)
					AIUtil::Attack(alive, target);
			}
			Release(player, st, f);
			// the ship home to the shore it left from
			if (ship)
				AIController::moveUnitsTo({ ship }, player, f.embark);
			setState(ship ? Naval::Ferry::RETURNING : Naval::Ferry::DONE);
			return;
		}
		case Naval::Ferry::RETURNING:
			if (!ship || (ship->position - f.embark).len2xz() < 20.0f || now - f.stateSince > 180.0f
				|| (!ship->movementController.isMoving() && now - f.stateSince > 5.0f))
				setState(Naval::Ferry::DONE);
			return;
		case Naval::Ferry::DONE:
			return;
		}
	}

	// ---- the fleet: war boats ----
	void Fleet(ServerGameObject* player, Naval::State& st)
	{
		const float now = Now();
		std::vector<ServerGameObject*> warships;
		for (ServerGameObject* s : Naval::Ships(player))
			if (!Naval::IsTransport(s))
				warships.push_back(s);
		if (warships.empty())
			return;
		// where the fleet is wanted: a ferry's landing (it clears the shore before the ferry lands), else
		// the enemy's shore
		const Naval::Ferry* escort = nullptr;
		for (const Naval::Ferry& f : st.ferries)
			if (f.state == Naval::Ferry::SAILING || f.state == Naval::Ferry::LANDING)
				escort = &f;
		// settlers at sea: the war boats sail with their cog, not ahead to the landing (the enemy's
		// war boats sank ten settler ferries in a row on their way to 3 Isles' contested island)
		ServerGameObject* settlerShip = nullptr;
		for (const Naval::Ferry& f : st.ferries)
			if (f.owner == Naval::Ferry::COLONY && (f.state == Naval::Ferry::BOARDING || f.state == Naval::Ferry::SAILING || f.state == Naval::Ferry::LANDING))
				if (ServerGameObject* s = Obj(f.ship)) {
					settlerShip = s;
					break;
				}
		for (ServerGameObject* w : warships) {
			// what it is shooting at now (a boat on its target is left to it: a new order every 2 s
			// and the Arrow Boat's 4 s between shots never came)
			Order* o = w->orderConfig.getCurrentOrder();
			Task* cur = o ? o->getCurrentTask() : nullptr;
			ServerGameObject* curTarget = cur ? cur->target.get() : nullptr;
			const bool attacking = curTarget && !curTarget->deleted && curTarget->isInteractable() && AIUtil::IsEnemy(player, curTarget);
			// enemy boats near: the nearest first
			ServerGameObject* foe = nullptr;
			float fd = 0.0f;
			NNSearch search;
			search.start(Srv(), w->position, 120.0f);
			while (ServerGameObject* e = (ServerGameObject*)search.next()) {
				if (e->deleted || !Naval::IsBoat(e) || !e->isInteractable() || !AIUtil::IsEnemy(player, e))
					continue;
				const float d = (e->position - w->position).sqlen2xz();
				if (d < 120.0f * 120.0f && (!foe || d < fd)) { foe = e; fd = d; }
			}
			if (foe) {
				if (!(attacking && Naval::IsBoat(curTarget))) {
					AIAttack(w, foe);
					st.shipAttacks++;
				}
				continue;
			}
			if (attacking)
				continue;
			// what is on the shore within its reach (its missiles' range, the reach rule's 60 m at most)
			const float range = g_ids.range >= 0 ? std::clamp(w->getItem(g_ids.range), 15.0f, 60.0f) : 30.0f;
			std::vector<ServerGameObject*> shore = AIUtil::EnemiesNear(player, w->position, range + 40.0f, true);
			for (ServerGameObject* b : AIUtil::EnemyBuildingsNear(player, w->position, range + 40.0f))
				shore.push_back(b);
			ServerGameObject* best = nullptr;
			float bd = 0.0f;
			for (ServerGameObject* e : shore) {
				if (Naval::IsBoat(e) || !e->isInteractable())
					continue;
				const float d = (e->position - w->position).sqlen2xz();
				if ((!best || d < bd) && !OutOfReach(w, e->position, range)) { best = e; bd = d; }
			}
			if (best) {
				AIAttack(w, best);
				st.coastAttacks++;
				continue;
			}
			// the enemies' docks (huntDocks, 3 war boats or more): the nearest, shot at from within reach
			if (st.huntDocks && warships.size() >= 3) {
				ServerGameObject* dock = nullptr;
				float dd = 0.0f;
				for (ServerGameObject* e : Players()) {
					if (e == player || !AIUtil::IsEnemy(player, e))
						continue;
					std::vector<ServerGameObject*> all;
					Walk(e, all);
					for (ServerGameObject* o : all)
						if (IsDock(o) && o->isInteractable()) {
							const float d = (o->position - w->position).sqlen2xz();
							if (!dock || d < dd) { dock = o; dd = d; }
						}
				}
				if (dock) {
					if (!OutOfReach(w, dock->position, range + 20.0f)) {
						AIAttack(w, dock);
						st.dockAttacks++;
					}
					else if (!w->movementController.isMoving()) {
						const Vector3 at = MovementController::nearestValidPosition(w->blueprint, dock->position, 10);
						AIController::moveUnitsTo({ w }, player, at);
					}
					continue;
				}
			}
			if (settlerShip) {
				// (alongside: once it is more than 35 m off, after it again)
				const int k = (int)(w->id % 4);
				Vector3 to = settlerShip->position;
				to.x += (k - 1.5f) * 10.0f;
				to.z += ((k * 3) % 4 - 1.5f) * 10.0f;
				if ((settlerShip->position - w->position).sqlen2xz() > 35.0f * 35.0f || !w->movementController.isMoving()) {
					const Vector3 at = MovementController::nearestValidPosition(w->blueprint, to, 8);
					if ((at - w->position).sqlen2xz() > 15.0f * 15.0f)
						AIController::moveUnitsTo({ w }, player, at);
				}
				continue;
			}
			if (w->movementController.isMoving())
				continue;
			// on to where it is wanted (a little off: the boats spread around the place); with no ferry
			// to clear the way for, every other one guards a supply route's cog (cogs on the route were
			// sunk one after another, 3 Isles 2026-11-22)
			Vector3 to = escort ? escort->landing : (st.need == Naval::NEED_NONE ? st.homeShore : st.enemyShore);
			if (!escort && (w->id % 2) == 0)
				for (const Naval::Colony& c : st.colonies)
					if (ServerGameObject* cog = Obj(c.cog)) {
						to = cog->position;
						break;
					}
			const int k = (int)(w->id % 5);
			to.x += (k - 2) * 8.0f;
			to.z += ((k * 3) % 5 - 2) * 8.0f;
			const Vector3 at = MovementController::nearestValidPosition(w->blueprint, to, 8);
			if ((at - w->position).sqlen2xz() > 30.0f * 30.0f)
				AIController::moveUnitsTo({ w }, player, at);
			(void)now;
		}
	}

	// the layer's own ferries (the Kingmaker's and the aggressive AI's, a Lua AI that doesn't steer
	// its navy): the free fighters at home over a guard, when a transport is free
	void LayerFerries(ServerGameObject* player, Naval::State& st, bool aggressive)
	{
		if (st.manual || st.need == Naval::NEED_NONE)
			return;
		const float now = Now();
		AIController& ai = player->aiController;
		ServerGameObject* transport = nullptr;
		for (ServerGameObject* s : Naval::Ships(player))
			if (Naval::IsTransport(s) && !InFerry(st, s->id) && !Naval::OnRoute(player, s->id) && PassengersOf(s) == 0 && (!transport || s->id < transport->id))
				transport = s;
		if (!transport)
			return;
		std::vector<ServerGameObject*> pool;
		for (ServerGameObject* u : ai.freeFighters(st.home))
			if (CanBoard(u) && !ai.navalUnits.count(u->id) && !ai.weatherHolds(u->id))
				pool.push_back(u);
		std::sort(pool.begin(), pool.end(), [](ServerGameObject* a, ServerGameObject* b) { return a->id < b->id; });
		// a guard stays home: a third (at least 4) where the sea is the only way in; else half
		// (where the sea is the only way in, an enemy comes by ship too, and the war boats and towers
		// see it: a quarter, at least 1; the Kingmaker kept 4 soldiers in all and sent no ferry in 30
		// minutes with a third)
		const size_t guard = st.need == Naval::NEED_ONLY ? std::max<size_t>(1, pool.size() / 4) : std::max<size_t>(6, pool.size() / 2);
		st.lastPool = (int)pool.size();
		if (pool.size() <= guard)
			return;
		const size_t free = pool.size() - guard;
		const size_t minWave = aggressive ? 8 : 10;
		const bool long_ = st.lastFerry < 0.0f ? now - st.start > 600.0f : now - st.lastFerry > 360.0f;
		if (free < minWave && !(long_ && free >= 3))
			return;
		// the youngest go (the guard keeps the oldest), as many as the transport holds
		const float room = g_ids.maxWeight >= 0 ? transport->getItem(g_ids.maxWeight) : 20.0f;
		std::vector<ServerGameObject*> go;
		float w = 0.0f;
		for (size_t i = pool.size(); i-- > guard;) {
			const float uw = PassengerWeight(pool[i]);
			if (w + uw > room)
				continue;
			w += uw;
			go.push_back(pool[i]);
		}
		// the target: the enemy's building nearest its shore (a foothold), else its palace
		ServerGameObject* target = nullptr;
		const int landCls = MovementController::movementClass(g_ids.land);
		if (ServerGameObject* e = Obj(st.enemy)) {
			const std::vector<ServerGameObject*> near = AIUtil::EnemyBuildingsNear(player, st.enemyShore, 300.0f);
			// (one on land: a bridge or a dock in the water has no shore to land at)
			for (ServerGameObject* b : near)
				if (b->getPlayer() == e && !IsDock(b) && RegionNear(landCls, b->position) >= 0) { target = b; break; }
			if (!target)
				target = AIUtil::Palace(e);
		}
		if (!target || go.size() < 3)
			return;
		if (Naval::SendFerry(player, go, target, target->position, Naval::Ferry::LAYER))
			st.lastFerry = now;
	}

	// ---- the colonies (Naval.h, Colony) ----

	bool IsRouteShip(const Naval::State& st, uint32_t ship)
	{
		return ship && std::any_of(st.colonies.begin(), st.colonies.end(), [&](const Naval::Colony& c) { return c.cog == ship; });
	}
	bool IsSettler(const Naval::State& st, uint32_t id)
	{
		for (const Naval::Colony& c : st.colonies)
			if (std::find(c.settlers.begin(), c.settlers.end(), id) != c.settlers.end())
				return true;
		return false;
	}
	bool IsPalace(const ServerGameObject* o)
	{
		return o && o->blueprint->bpClass == Tags::GAMEOBJCLASS_BUILDING && g_ids.palaceBuilding >= 0
			&& const_cast<ServerGameObject*>(o)->getItem(g_ids.palaceBuilding) > 0.0f;
	}
	int PeasantClass() { return g_ids.peasant ? MovementController::movementClass(g_ids.peasant) : -1; }
	int TileRegion(int cls, const Vector3& p) { return RegionNear(cls, p); }

	// a resource on the map: a tree (its wood) or a gold deposit (its gold)
	struct Resource { Vector3 p; int region; float wood, gold; uint32_t id; };
	std::vector<Resource> Resources(int cls)
	{
		std::vector<Resource> out;
		std::vector<ServerGameObject*> todo;
		if (CommonGameObject* level = Srv()->getLevel())
			todo.push_back((ServerGameObject*)level);
		while (!todo.empty()) {
			ServerGameObject* o = todo.back();
			todo.pop_back();
			for (const auto& [type, children] : o->children)
				for (CommonGameObject* c : children) {
					auto* u = (ServerGameObject*)c;
					if (u->deleted)
						continue;
					const int bc = u->blueprint->bpClass;
					if (bc == Tags::GAMEOBJCLASS_CONTAINER) {
						const std::string& n = u->blueprint->name;
						const bool tree = n.find("Tree") != std::string::npos && n.find("Fallen") == std::string::npos;
						const bool gold = n.find("Gold Deposit") != std::string::npos;
						if (!tree && !gold)
							continue;
						Resource r{ u->position, RegionNear(cls, u->position), 0.0f, 0.0f, u->id };
						if (r.region < 0)
							continue;
						// (a tree whose wood the game set keeps elsewhere: 100, a tree's usual)
						if (tree)
							r.wood = u->getItem(Tags::PDITEM_WOOD) > 0.0f ? u->getItem(Tags::PDITEM_WOOD) : 100.0f;
						else
							r.gold = std::max(u->getItem(Tags::PDITEM_GOLD), 0.0f);
						if (r.wood + r.gold > 0.0f)
							out.push_back(r);
					}
					else if (bc == Tags::GAMEOBJCLASS_PLAYER || bc == Tags::GAMEOBJCLASS_LEVEL)
						todo.push_back(u);
				}
		}
		std::sort(out.begin(), out.end(), [](const Resource& a, const Resource& b) { return a.id < b.id; });
		return out;
	}
	// what lies within r metres of a place, in a region (gold counts twice: it is the scarcer)
	float ValueNear(const std::vector<Resource>& res, int region, const Vector3& at, float r)
	{
		float v = 0.0f;
		for (const Resource& x : res)
			if (x.region == region && (x.p - at).sqlen2xz() <= r * r)
				v += x.wood + 2.0f * x.gold;
		return v;
	}

	// the player's palaces (built or foundations) and villages, by land region
	void Holdings(ServerGameObject* player, int cls, std::map<int, int>& palaces, std::map<int, std::vector<ServerGameObject*>>& villages)
	{
		std::vector<ServerGameObject*> all;
		Walk(player, all);
		for (ServerGameObject* o : all) {
			if (o->blueprint->bpClass != Tags::GAMEOBJCLASS_BUILDING || (o->flags & ServerGameObject::fTerminated))
				continue;
			const int r = RegionNear(cls, o->position);
			if (r < 0)
				continue;
			if (IsPalace(o))
				palaces[r]++;
			else if (o->blueprint == g_ids.village || o->blueprint == g_ids.villageFoundation)
				villages[r].push_back(o);
		}
	}

	// a valid site for a foundation in the region, rings 10 to 90 m round a place; (0, 0, 0) for none
	Vector3 SiteNear(ServerGameObject* player, const GameObjBlueprint* bp, int cls, int region, const Vector3& at)
	{
		for (float r = 10.0f; r <= 90.0f; r += 8.0f)
			for (int k = 0; k < 16; k++) {
				const float a = k * 6.2831853f / 16.0f + r * 0.05f;
				Vector3 p(at.x + r * std::sin(a), 0.0f, at.z + r * std::cos(a));
				const int tx = TileX(p.x), tz = TileX(p.z);
				if (!InMap(tx, tz) || MovementController::terrainRegion(cls, tx, tz) != region)
					continue;
				p = TileCentre(tx, tz);
				if (StampdownPlan::getStampdownPlan(Srv(), player, bp, p, Vector3(0, 0, 0)).status == StampdownPlan::Status::Valid)
					return p;
			}
		return Vector3(0, 0, 0);
	}

	// a dock site on the region's shore, in the home sea, the nearest to a place
	bool DockSite(ServerGameObject* player, int region, int seaRegion, const Vector3& at, Vector3& p, float& yaw)
	{
		const int landCls = PeasantClass(), boatCls = MovementController::movementClass(g_ids.cog);
		const Shore s = ShoreNear(boatCls, seaRegion, landCls, region, at, 80);
		if (!s.ok)
			return false;
		struct Site { float d; Vector3 p; };
		std::vector<Site> sites;
		for (int dz = -12; dz <= 12; dz++)
			for (int dx = -12; dx <= 12; dx++) {
				const int x = s.wx + dx, z = s.wz + dz;
				if (!InMap(x, z) || MovementController::terrainRegion(boatCls, x, z) != seaRegion)
					continue;
				bool nearLand = false;
				for (int k = -2; k <= 2 && !nearLand; k++)
					for (int l = -2; l <= 2 && !nearLand; l++)
						nearLand = InMap(x + l, z + k) && MovementController::terrainRegion(landCls, x + l, z + k) == region;
				if (nearLand) {
					const Vector3 q = TileCentre(x, z);
					sites.push_back({ (q - at).len2xz(), q });
				}
			}
		std::sort(sites.begin(), sites.end(), [](const Site& a, const Site& b) { return a.d != b.d ? a.d < b.d : (a.p.x != b.p.x ? a.p.x < b.p.x : a.p.z < b.p.z); });
		for (const Site& site : sites)
			for (int q = 0; q < 4; q++) {
				const float y = q * 1.57079633f;
				if (StampdownPlan::getStampdownPlan(Srv(), player, g_ids.dockFoundation, site.p, Vector3(0, y, 0)).status == StampdownPlan::Status::Valid) {
					p = site.p;
					yaw = y;
					return true;
				}
			}
		return false;
	}

	// the settlers build a foundation: those not already at it, two at most
	void SettlersBuild(ServerGameObject* player, Naval::Colony& c, ServerGameObject* foundation)
	{
		if (!foundation || g_ids.buildOrder < 0)
			return;
		int n = 0;
		for (uint32_t id : c.settlers)
			if (ServerGameObject* u = Obj(id)) {
				Order* o = u->orderConfig.getCurrentOrder();
				Task* t = o ? o->getCurrentTask() : nullptr;
				if (t && t->target.get() == foundation)
					n++;
			}
		for (uint32_t id : c.settlers) {
			if (n >= 2)
				break;
			ServerGameObject* u = Obj(id);
			if (!u || !u->isInteractable() || player->aiController.navalUnits.count(id))
				continue;
			Order* o = u->orderConfig.getCurrentOrder();
			Task* t = o ? o->getCurrentTask() : nullptr;
			if (t && t->target.get() && t->target.get()->blueprint->bpClass == Tags::GAMEOBJCLASS_BUILDING
				&& t->target.get()->blueprint->name.find("Foundation") != std::string::npos)
				continue; // (building another)
			if (u->orderConfig.addOrder(&Srv()->gameSet->orders[g_ids.buildOrder], Tags::ORDERASSIGNMODE_FORGET_EVERYTHING_ELSE, foundation))
				n++;
		}
	}

	// the idle settlers to work: the nearest tree or gold deposit on the island, round the colony's seat
	void SettlersWork(ServerGameObject* player, Naval::Colony& c, const std::vector<Resource>& res, const Vector3& seat)
	{
		const GameSet* gs = Srv()->gameSet.get();
		int k = 0;
		for (uint32_t id : c.settlers) {
			ServerGameObject* u = Obj(id);
			if (!u || !u->isInteractable() || u->orderConfig.getCurrentOrder() || player->aiController.navalUnits.count(id))
				continue;
			// every third to gold where there is some (it counts twice), the rest to the trees
			const bool wantGold = (k++ % 3) == 2;
			const Resource* best = nullptr;
			float bd = 0.0f;
			for (int pass = 0; pass < 2 && !best; pass++)
				for (const Resource& r : res) {
					if (r.region != c.region || ((r.gold > 0.0f) != (pass == 0 ? wantGold : !wantGold)))
						continue;
					const float d = (r.p - seat).sqlen2xz() + (r.p - u->position).sqlen2xz();
					if (!best || d < bd) { best = &r; bd = d; }
				}
			ServerGameObject* target = best ? Obj(best->id) : nullptr;
			const int cmd = best && best->gold > 0.0f ? g_ids.mineGold : g_ids.chop;
			if (target && cmd >= 0)
				gs->commands[cmd].execute(u, target, Tags::ORDERASSIGNMODE_FORGET_EVERYTHING_ELSE);
		}
	}

	// Manor or route (Naval.h, Colony): the Manor where the player may build one ("Can Build Manor"),
	// can pay it with a reserve left, there is a site, and the island still holds 2,500 or more to
	// repay its 900 (a route costs a dock, a cog's time and two cart trips each load, and the stock
	// waits on the sea); the route otherwise. Undecided a while (3 minutes) when the Manor is allowed
	// and half paid for: the wood may come.
	void WeighColony(ServerGameObject* player, Naval::State& st, Naval::Colony& c, const std::vector<Resource>& res)
	{
		const Vector3 seat = Obj(c.village) ? Obj(c.village)->position : c.site;
		c.value = ValueNear(res, c.region, seat, 120.0f);
		const bool allowed = g_ids.manorFoundation && g_ids.canBuildManor >= 0 && player->getItem(g_ids.canBuildManor) > 0.0f;
		const float woodCost = g_ids.manorFoundation ? g_ids.manorFoundation->getStartingItemValue(Tags::PDITEM_WOOD_COST) : 750.0f;
		const float goldCost = g_ids.manorFoundation ? g_ids.manorFoundation->getStartingItemValue(Tags::PDITEM_GOLD_COST) : 150.0f;
		const float wood = player->getItem(Tags::PDITEM_WOOD), gold = player->getItem(Tags::PDITEM_GOLD);
		const bool affordable = wood >= woodCost + 150.0f && gold >= goldCost;
		const bool worth = c.value >= 2500.0f;
		bool site = false;
		if (allowed && worth)
			site = SiteNear(player, g_ids.manorFoundation, PeasantClass(), c.region, seat).x != 0.0f;
		Naval::Colony::Plan plan = Naval::Colony::ROUTE;
		const char* why = "";
		if (!allowed)
			why = "no Manor allowed (no Castle)";
		else if (!worth)
			why = "too little left on the island for a Manor";
		else if (!site)
			why = "no site for a Manor";
		else if (affordable) {
			plan = Naval::Colony::MANOR;
			why = "a Manor allowed, paid for, worth it";
		}
		else if (c.plan == Naval::Colony::UNDECIDED && Now() - c.since < 180.0f && wood >= woodCost * 0.5f) {
			plan = Naval::Colony::UNDECIDED;
			why = "a Manor allowed, half paid for: waiting for the wood";
		}
		else
			why = "a Manor allowed but not paid for";
		// (a route once running turns into a Manor only for one paid for now)
		if (plan != c.plan) {
			printf("AI %u: colony %u at (%.0f, %.0f): %s (%s; %.0f on the island, %.0f wood %.0f gold)\n", player->id, c.id, seat.x, seat.z,
				plan == Naval::Colony::MANOR ? "a Manor of its own" : plan == Naval::Colony::ROUTE ? "the supply route by sea" : "undecided", why, c.value, wood, gold);
			c.plan = plan;
		}
	}

	// the route's associations (Cog Delivery.cpp, Cart Delivery.cpp): the village delivers to the
	// island dock, the island dock's oversea depot is the home dock
	void SetRoute(ServerGameObject* player, Naval::Colony& c, ServerGameObject* village, ServerGameObject* dock, ServerGameObject* homeDock)
	{
		if (g_ids.overland < 0 || g_ids.oversea < 0)
			return;
		village->clearAssociates(g_ids.overland);
		village->associateObject(g_ids.overland, dock);
		dock->clearAssociates(g_ids.oversea);
		dock->associateObject(g_ids.oversea, homeDock);
		c.routeSet = true;
	}

	void EndRoute(ServerGameObject* player, Naval::State& st, Naval::Colony& c)
	{
		if (ServerGameObject* cog = Obj(c.cog))
			cog->orderConfig.cancelAllOrders();
		if (ServerGameObject* v = Obj(c.village); v && g_ids.overland >= 0)
			v->clearAssociates(g_ids.overland);
		c.cog = 0;
		c.routeSet = false;
	}

	// whether anyone else (an enemy, an ally) has a building within r metres
	bool OthersBuildingNear(ServerGameObject* player, const Vector3& at, float r)
	{
		NNSearch search;
		search.start(Srv(), at, r);
		while (ServerGameObject* o = (ServerGameObject*)search.next())
			if (!o->deleted && o->blueprint->bpClass == Tags::GAMEOBJCLASS_BUILDING && o->getPlayer() && o->getPlayer() != player
				&& (o->position - at).sqlen2xz() <= r * r && o->getPlayer()->blueprint->name.find("Neutral") == std::string::npos)
				return true;
		return false;
	}

	// n settlers to a colony: free peasants at home (8 or more free kept back), the nearest to the
	// home dock, by ferry to the colony's site; held from the scripts from now on. Whether a ferry went.
	bool SendSettlers(ServerGameObject* player, Naval::State& st, Naval::Colony& c, int n)
	{
		ServerGameObject* home = Home(player);
		ServerGameObject* homeDock = FindDock(player, st, true);
		const int cls = PeasantClass();
		if (!home || !homeDock || cls < 0)
			return false;
		const int homeRegion = RegionNear(cls, home->position);
		// (a ferry of settlers ever lost to that island: not again without two war boats to go with it)
		if (auto fail = st.colonyFails.find(c.region); fail != st.colonyFails.end()) {
			int warships = 0;
			for (ServerGameObject* s : Naval::Ships(player))
				warships += !Naval::IsTransport(s);
			if (warships < 2) {
				Log(player, "colony %u: settlers wait for an escort (%d war boats)", c.id, warships);
				return false;
			}
		}
		std::vector<ServerGameObject*> free;
		for (ServerGameObject* u : AIUtil::Peasants(player))
			if (u->isInteractable() && !player->aiController.ministryHeld.count(u->id) && !player->aiController.navalUnits.count(u->id)
				&& RegionNear(cls, u->position) == homeRegion)
				free.push_back(u);
		if ((int)free.size() < n + 8)
			return false;
		const Vector3 d = homeDock->position;
		std::sort(free.begin(), free.end(), [&](ServerGameObject* a, ServerGameObject* b) {
			const float da = (a->position - d).sqlen2xz(), db = (b->position - d).sqlen2xz();
			return da != db ? da < db : a->id < b->id; });
		free.resize(n);
		const Vector3 to = Obj(c.village) ? Obj(c.village)->position : c.site;
		const uint32_t ferry = Naval::SendFerry(player, free, nullptr, to, Naval::Ferry::COLONY);
		if (!ferry)
			return false;
		c.ferry = ferry;
		if (const Naval::Ferry* f = Naval::FindFerry(player, ferry))
			for (uint32_t id : f->passengers) {
				if (std::find(c.settlers.begin(), c.settlers.end(), id) == c.settlers.end())
					c.settlers.push_back(id);
				player->aiController.ministryHeld.insert(id);
			}
		printf("AI %u: colony %u: %d settlers sent by ferry %u to (%.0f, %.0f) at t=%.0f\n", player->id, c.id, n, ferry, to.x, to.z, Now());
		return true;
	}

	// A new colony: once 10 minutes in, with a built dock at home, 12 peasants or more and nothing
	// being founded; the richest spot (resources within 60 m) on an island of the home sea without a
	// palace of anyone's and without enemy buildings within 200 m, worth 3,000 or more, taken when
	// home holds less than twice as much round the palace or 20 minutes are past. Four settlers.
	void FoundColony(ServerGameObject* player, Naval::State& st, const std::vector<Resource>& res)
	{
		const float now = Now();
		if (now < st.nextFounding || now - st.start < 600.0f || st.colonies.size() >= 2)
			return;
		st.nextFounding = now + 60.0f;
		for (const Naval::Colony& c : st.colonies)
			if (c.stage != Naval::Colony::WORKING)
				return;
		ServerGameObject* homeDock = FindDock(player, st, true);
		ServerGameObject* home = Home(player);
		const int cls = PeasantClass();
		if (!homeDock || !home || cls < 0)
			return;
		const int homeRegion = RegionNear(cls, home->position);
		const int boatCls = MovementController::movementClass(g_ids.cog);
		const int sea = MovementController::terrainRegion(boatCls, TileX(st.homeShore.x), TileX(st.homeShore.z));
		std::vector<ServerGameObject*> peasants = AIUtil::Peasants(player);
		if (peasants.size() < 12 || sea < 0 || homeRegion < 0)
			return;
		// the regions taken: any palace (anyone's), a colony of ours
		std::set<int> taken;
		for (ServerGameObject* p : Players()) {
			std::map<int, int> palaces;
			std::map<int, std::vector<ServerGameObject*>> villages;
			Holdings(p, cls, palaces, villages);
			for (const auto& [r, n] : palaces)
				taken.insert(r);
		}
		for (const Naval::Colony& c : st.colonies)
			taken.insert(c.region);
		// (and the islands another player is settling: two AIs sailed for the same spot, 3 Isles)
		for (ServerGameObject* p : Players())
			if (p != player)
				for (const Naval::Colony& c : p->aiController.naval.colonies)
					taken.insert(c.region);
		taken.insert(homeRegion);
		const Resource* best = nullptr;
		float bestValue = 0.0f;
		for (const Resource& r : res) {
			if (taken.count(r.region) || (st.colonyFails.count(r.region) && now < st.colonyFails[r.region].second))
				continue;
			const float v = ValueNear(res, r.region, r.p, 60.0f);
			if (v <= bestValue)
				continue;
			if (!ShoreNear(boatCls, sea, cls, r.region, r.p, 60).ok || OthersBuildingNear(player, r.p, 200.0f))
				continue;
			best = &r;
			bestValue = v;
		}
		const float homeValue = ValueNear(res, homeRegion, home->position, 250.0f);
		if (Trace())
			Log(player, "colony: the best island spot %s worth %.0f, home %.0f round the palace", best ? "found" : "none", bestValue, homeValue);
		if (!best || bestValue < 3000.0f || (homeValue >= 2.0f * bestValue && now - st.start < 1200.0f))
			return;
		Naval::Colony c;
		c.id = st.nextColony++;
		c.region = best->region;
		c.site = best->p;
		c.since = now;
		if (!SendSettlers(player, st, c, 4)) {
			Log(player, "colony: no settlers or no free transport for them");
			return;
		}
		st.colonies.push_back(c);
		st.coloniesFounded++;
		printf("AI %u: colony %u: %zu settlers sail for (%.0f, %.0f), worth %.0f (home %.0f) at t=%.0f\n", player->id, c.id, c.settlers.size(), best->p.x, best->p.z,
			bestValue, homeValue, now);
	}

	// A Castle sooner for a colony's Manor (not in WKB.exe, whose AI never settles another island): a
	// colony worth a Manor (4,000 or more round it) that has none allowed, a Keep at home and no
	// Castle building of its alignment under way: that building's foundation laid round the home (the
	// game's own command must allow it: its tech level, no building of the other alignments), with
	// wood and gold to spare, three peasants to build it. The scripts reach the Castle themselves
	// later or never in a 45-minute game; built, the Keep becomes a Castle and allows the Manor.
	void PushCastle(ServerGameObject* player, Naval::State& st, const std::vector<ServerGameObject*>& all)
	{
		const float now = Now();
		if (now < st.nextCastleTry || g_ids.canBuildManor < 0 || player->getItem(g_ids.canBuildManor) > 0.0f)
			return;
		st.nextCastleTry = now + 30.0f;
		bool wanted = false;
		for (const Naval::Colony& c : st.colonies)
			wanted = wanted || (c.plan != Naval::Colony::MANOR && c.value >= 4000.0f && !c.manor);
		ServerGameObject* home = Home(player);
		if (!wanted || !home)
			return;
		// (still a Manor: its Keep first, by a Monastery, Temple or Windmill; a Keep: its Castle)
		const bool keepStep = home->blueprint->name == "Manor";
		if (!keepStep && home->blueprint->name.rfind("Keep", 0) != 0) {
			Log(player, "a Castle for the colony's Manor: the palace is %s", home->blueprint->name.c_str());
			return;
		}
		const auto& path = keepStep ? g_ids.keepPath : g_ids.castlePath;
		// (one under way, or built: nothing more; a foundation of ours lost: tried again)
		for (ServerGameObject* o : all)
			for (const auto& c : path)
				if (o->blueprint == c.bp || o->blueprint == c.foundation) {
					if (o->blueprint == c.foundation)
						st.castleFoundation = o->id;
					return;
				}
		ServerGameObject* builder = nullptr;
		const int cls = PeasantClass();
		const int homeRegion = RegionNear(cls, home->position);
		for (ServerGameObject* u : AIUtil::Peasants(player))
			if (u->isInteractable() && RegionNear(cls, u->position) == homeRegion) {
				builder = u;
				break;
			}
		if (!builder)
			return;
		const GameSet* gs = Srv()->gameSet.get();
		for (const auto& c : path) {
			if (!c.foundation || c.command < 0)
				continue;
			// (the Keep's own alignment: an unaligned one, or the Manor, takes the first the game allows)
			const bool own = home->blueprint->name == c.keep;
			if (!own && home->blueprint->name != "Keep")
				continue;
			const float wood = c.foundation->getStartingItemValue(Tags::PDITEM_WOOD_COST), gold = c.foundation->getStartingItemValue(Tags::PDITEM_GOLD_COST);
			if (player->getItem(Tags::PDITEM_WOOD) < wood + 200.0f || player->getItem(Tags::PDITEM_GOLD) < gold + 50.0f) {
				Log(player, "a Castle for the colony's Manor: %s wants %.0f wood %.0f gold", c.building, wood, gold);
				return;
			}
			if (!gs->commands[c.command].canBeExecuted(builder)) {
				Log(player, "a Castle for the colony's Manor: %s not allowed yet", c.building);
				continue;
			}
			// (rings round the home out to 250 m, four ways turned: within 90 m of a grown town there
			// was no site for an Observatory, 3 Isles)
			Vector3 p(0, 0, 0);
			float yaw = 0.0f;
			int refused[16] = {};
			for (float r = 20.0f; r <= 250.0f && p.x == 0.0f; r += 10.0f)
				for (int k = 0; k < 24 && p.x == 0.0f; k++) {
					const float a = k * 6.2831853f / 24.0f + r * 0.03f;
					const int tx = TileX(home->position.x + r * std::sin(a)), tz = TileX(home->position.z + r * std::cos(a));
					if (!InMap(tx, tz) || MovementController::terrainRegion(cls, tx, tz) != homeRegion)
						continue;
					const Vector3 q = TileCentre(tx, tz);
					for (int t = 0; t < 4; t++) {
						const StampdownPlan plan = StampdownPlan::getStampdownPlan(Srv(), player, c.foundation, q, Vector3(0, t * 1.5707963f, 0));
						if (plan.status == StampdownPlan::Status::Valid) {
							p = q;
							yaw = t * 1.5707963f;
							break;
						}
						refused[std::clamp((int)plan.status, 0, 15)]++;
					}
				}
			if (p.x == 0.0f) {
				std::string why;
				for (int i = 0; i < 16; i++)
					if (refused[i])
						why += " " + std::to_string(i) + ":" + std::to_string(refused[i]);
				Log(player, "a Castle for the colony's Manor: no site for %s (refused by status%s)", c.building, why.c_str());
				return;
			}
			if (ServerGameObject* f = LayFoundation(player, c.foundation, p, yaw)) {
				st.castleFoundation = f->id;
				st.castlesLaid++;
				SendBuilders(player, f, 3);
				printf("AI %u: a %s laid down at (%.0f, %.0f) at t=%.0f: %s\n", player->id, c.building, p.x, p.z, now,
					keepStep ? "the Manor becomes a Keep, the way to a Castle for the colony's Manor" : "its Castle allows the colony's Manor");
			}
			return;
		}
	}

	void Colonies(ServerGameObject* player, Naval::State& st)
	{
		const float now = Now();
		if (now < st.nextColonyCheck || !g_ids.peasant || !g_ids.village)
			return;
		st.nextColonyCheck = now + 5.0f;
		const int cls = PeasantClass();
		const std::vector<Resource> res = Resources(cls);
		std::map<int, int> palaces;
		std::map<int, std::vector<ServerGameObject*>> villages;
		Holdings(player, cls, palaces, villages);
		// villages already stranded (a script's, a player's): a colony each island
		for (const auto& [r, list] : villages) {
			if (palaces.count(r) || std::any_of(st.colonies.begin(), st.colonies.end(), [&](const Naval::Colony& c) { return c.region == r; }))
				continue;
			Naval::Colony c;
			c.id = st.nextColony++;
			c.region = r;
			c.site = list.front()->position;
			c.village = list.front()->id;
			c.stage = Naval::Colony::WORKING;
			c.since = now;
			st.colonies.push_back(c);
			printf("AI %u: colony %u: the village %u at (%.0f, %.0f) has no Manor it can reach\n", player->id, c.id, c.village, c.site.x, c.site.z);
		}
		FoundColony(player, st, res);
		if (!st.colonies.empty()) {
			std::vector<ServerGameObject*> mine;
			Walk(player, mine);
			PushCastle(player, st, mine);
		}
		ServerGameObject* homeDock = FindDock(player, st, true);
		const GameSet* gs = Srv()->gameSet.get();
		for (auto it = st.colonies.begin(); it != st.colonies.end();) {
			Naval::Colony& c = *it;
			// the settlers: on its island, the player's peasants there (held from the scripts)
			if (c.stage == Naval::Colony::SETTLING) {
				const Naval::Ferry* f = Naval::FindFerry(player, c.ferry);
				const Naval::Ferry::State fs = f ? f->state : Naval::Ferry::DONE;
				if (fs == Naval::Ferry::ASHORE || fs == Naval::Ferry::RETURNING || (fs == Naval::Ferry::DONE && f && f->landed > 0))
					c.stage = Naval::Colony::FOUNDING;
				else if (fs == Naval::Ferry::DONE) {
					printf("AI %u: colony %u: the settlers never landed\n", player->id, c.id);
					// (two lost there: that island left alone for 15 minutes; the enemy's war boats sank
					// ten ferries of settlers in a row on 3 Isles' contested island)
					auto& fail = st.colonyFails[c.region];
					if (++fail.first >= 2) {
						fail.second = now + 900.0f;
						fail.first = 0;
						printf("AI %u: the island %d left alone for 15 minutes (settlers lost twice)\n", player->id, c.region);
					}
					for (uint32_t id : c.settlers)
						player->aiController.ministryHeld.erase(id);
					st.nextFounding = now + 300.0f;
					it = st.colonies.erase(it);
					continue;
				}
				else {
					++it;
					continue;
				}
			}
			// (those on their way, in a ferry still, stay held and on the list)
			std::vector<uint32_t> settlers;
			int working = 0;
			for (ServerGameObject* u : AIUtil::Peasants(player))
				if (RegionNear(cls, u->position) == c.region && !player->aiController.navalUnits.count(u->id)) {
					settlers.push_back(u->id);
					working++;
				}
			for (uint32_t id : c.settlers)
				if (std::find(settlers.begin(), settlers.end(), id) == settlers.end()) {
					if (Obj(id) && player->aiController.navalUnits.count(id))
						settlers.push_back(id);
					else
						player->aiController.ministryHeld.erase(id);
				}
			for (uint32_t id : settlers)
				player->aiController.ministryHeld.insert(id);
			c.settlers = settlers;
			// too few on the island (a stranded village nobody works, settlers lost at boarding): more,
			// once the last ferry is over, every 2 minutes at most
			const Naval::Ferry::State lastFerry = c.ferry ? Naval::FerryState(player, c.ferry) : Naval::Ferry::DONE;
			if (working < 2 && (lastFerry == Naval::Ferry::DONE || lastFerry == Naval::Ferry::RETURNING) && now >= c.nextSettlers) {
				c.nextSettlers = now + 120.0f;
				SendSettlers(player, st, c, 3);
			}
			// a colony at work (its Manor, its route open) grows to 8 settlers, 3 at a time every 3
			// minutes while home keeps 8 free peasants or more (SendSettlers): two to four settlers
			// brought 60 to 900 home in 45 minutes
			else if (c.stage == Naval::Colony::WORKING && working < 8 && (c.routeSet || c.plan == Naval::Colony::MANOR) && working >= 2
				&& (lastFerry == Naval::Ferry::DONE || lastFerry == Naval::Ferry::RETURNING) && now >= c.nextSettlers) {
				c.nextSettlers = now + 180.0f;
				SendSettlers(player, st, c, std::min(3, 8 - working));
			}
			// the colony's buildings: its village, Manor (or foundations), dock on the island
			ServerGameObject* village = nullptr, * manor = nullptr, * dock = nullptr;
			std::vector<ServerGameObject*> all;
			Walk(player, all);
			for (ServerGameObject* o : all) {
				if (o->blueprint->bpClass != Tags::GAMEOBJCLASS_BUILDING || (o->flags & ServerGameObject::fTerminated) || RegionNear(cls, o->position) != c.region) {
					if (IsDock(o) && !dock) {
						// (a dock stands in the water: its land is the region beside it)
						bool beside = false;
						const int tx = TileX(o->position.x), tz = TileX(o->position.z);
						for (int dz = -3; dz <= 3 && !beside; dz++)
							for (int dx = -3; dx <= 3 && !beside; dx++)
								beside = InMap(tx + dx, tz + dz) && MovementController::terrainRegion(cls, tx + dx, tz + dz) == c.region;
						if (beside)
							dock = o;
					}
					continue;
				}
				if ((IsPalace(o) || o->blueprint == g_ids.manorFoundation) && (!manor || o->blueprint != g_ids.manorFoundation))
					manor = o;
				else if ((o->blueprint == g_ids.village || o->blueprint == g_ids.villageFoundation) && (!village || o->blueprint == g_ids.village))
					village = o;
				else if (IsDock(o))
					dock = o;
			}
			// (the Manor laid by the colony, by its id: the walk through the player's objects missed the
			// foundation under the new settlement it makes; its conversion to the Manor keeps the place)
			if (!manor && c.manor)
				if (ServerGameObject* m = Obj(c.manor); m && m->getPlayer() == player && RegionNear(cls, m->position) == c.region)
					manor = m;
			if (!manor && c.manor) {
				// (converted: the built Manor stands where the foundation stood)
				NNSearch search;
				search.start(Srv(), c.site, 150.0f);
				while (ServerGameObject* o = (ServerGameObject*)search.next())
					if (!o->deleted && o->getPlayer() == player && IsPalace(o) && RegionNear(cls, o->position) == c.region) {
						manor = o;
						break;
					}
			}
			c.village = village ? village->id : 0;
			c.manor = manor ? manor->id : 0;
			c.dock = dock ? dock->id : 0;
			if (settlers.empty() && !village && !manor && !dock) {
				printf("AI %u: colony %u lost (no settler, no building)\n", player->id, c.id);
				EndRoute(player, st, c);
				it = st.colonies.erase(it);
				continue;
			}
			// (weighed until a Manor is laid: its cost paid, the wood left would have turned it back to the route)
			if (now >= c.nextWeigh && !manor) {
				c.nextWeigh = now + 30.0f;
				WeighColony(player, st, c, res);
			}
			const Vector3 seat = manor ? manor->position : village ? village->position : c.site;
			const bool manorBuilt = manor && manor->blueprint != g_ids.manorFoundation;
			if (manorBuilt) {
				// its own Manor: the carts and peasants deliver there; a route that ran is ended
				if (c.cog || c.routeSet) {
					printf("AI %u: colony %u: its Manor is built, the supply route ends\n", player->id, c.id);
					EndRoute(player, st, c);
				}
				if (c.plan != Naval::Colony::MANOR || c.stage != Naval::Colony::WORKING) {
					if (c.stage != Naval::Colony::WORKING)
						printf("AI %u: colony %u: its Manor %u is built at t=%.0f\n", player->id, c.id, manor->id, now);
					c.plan = Naval::Colony::MANOR;
					c.stage = Naval::Colony::WORKING;
					st.colonyManors++;
				}
			}
			// (nothing laid without a settler on the island to build it: a Lua AI that never had a ship
			// paid 900 for a Manor on its stranded village's island that nobody could reach)
			else if (working == 0) {
			}
			else if (c.plan == Naval::Colony::MANOR && !manor && now >= c.nextLay) {
				c.nextLay = now + 20.0f;
				const Vector3 p = SiteNear(player, g_ids.manorFoundation, cls, c.region, seat);
				if (p.x != 0.0f && player->canAffordObject(g_ids.manorFoundation)) {
					if (ServerGameObject* f = Srv()->stampdownObject(g_ids.manorFoundation, player, p, Vector3(0, 0, 0), false, true)) {
						printf("AI %u: colony %u: a Manor laid down at (%.0f, %.0f) at t=%.0f\n", player->id, c.id, p.x, p.z, now);
						manor = f;
						c.manor = f->id;
					}
				}
			}
			else if (c.plan == Naval::Colony::ROUTE && now >= c.nextLay) {
				c.nextLay = now + 20.0f;
				if (!village && player->canAffordObject(g_ids.villageFoundation)) {
					const Vector3 p = SiteNear(player, g_ids.villageFoundation, cls, c.region, c.site);
					if (p.x != 0.0f)
						if (ServerGameObject* f = Srv()->stampdownObject(g_ids.villageFoundation, player, p, Vector3(0, 0, 0), false, true)) {
							printf("AI %u: colony %u: a village laid down at (%.0f, %.0f) at t=%.0f\n", player->id, c.id, p.x, p.z, now);
							village = f;
							c.village = f->id;
						}
				}
				if (!dock && homeDock && player->canAffordObject(g_ids.dockFoundation)) {
					const int boatCls = MovementController::movementClass(g_ids.cog);
					const int sea = MovementController::terrainRegion(boatCls, TileX(homeDock->position.x), TileX(homeDock->position.z));
					Vector3 p;
					float yaw = 0.0f;
					int seaHere = sea;
					if (seaHere < 0) {
						// (a dock's own tile is blocked for boats: the home sea by the shore the navy uses)
						seaHere = MovementController::terrainRegion(boatCls, TileX(st.homeShore.x), TileX(st.homeShore.z));
					}
					if (seaHere >= 0 && DockSite(player, c.region, seaHere, seat, p, yaw))
						if (ServerGameObject* f = Srv()->stampdownObject(g_ids.dockFoundation, player, p, Vector3(0, yaw, 0), false, true)) {
							printf("AI %u: colony %u: a dock laid down at (%.0f, %.0f) at t=%.0f, for the supply route\n", player->id, c.id, p.x, p.z, now);
							dock = f;
							c.dock = f->id;
						}
				}
			}
			// the foundations built by the settlers
			for (ServerGameObject* f : { manor, village, dock })
				if (f && f->blueprint->name.find("Foundation") != std::string::npos)
					SettlersBuild(player, c, f);
			// the route: village and dock built, the home dock there: the associations, a cog on the
			// drop-off order, a cart at the home dock
			const bool villageBuilt = village && village->blueprint == g_ids.village;
			const bool dockBuilt = dock && dock->blueprint == g_ids.dock;
			if (c.plan == Naval::Colony::ROUTE && villageBuilt && dockBuilt && homeDock) {
				if (!c.routeSet) {
					SetRoute(player, c, village, dock, homeDock);
					c.stage = Naval::Colony::WORKING;
					st.routesOpened++;
					printf("AI %u: colony %u: the supply route opened, village %u -> dock %u -> dock %u -> the Manor, at t=%.0f\n", player->id, c.id,
						village->id, dock->id, homeDock->id, now);
				}
				ServerGameObject* cog = Obj(c.cog);
				if (!cog) {
					c.cog = 0;
					ServerGameObject* best = nullptr;
					for (ServerGameObject* s : Naval::Ships(player))
						if (Naval::IsTransport(s) && !InFerry(st, s->id) && !IsRouteShip(st, s->id) && PassengersOf(s) == 0
							&& (!best || (s->position - dock->position).sqlen2xz() < (best->position - dock->position).sqlen2xz()))
							best = s;
					if (best && g_ids.cogDropOff >= 0) {
						gs->commands[g_ids.cogDropOff].execute(best, dock, Tags::ORDERASSIGNMODE_FORGET_EVERYTHING_ELSE);
						c.cog = best->id;
						c.cogLoad = 0.0f;
						printf("AI %u: colony %u: the cog %u sails the route\n", player->id, c.id, best->id);
					}
				}
				else {
					// (its order lost: again; its load home counted)
					if (!cog->orderConfig.getCurrentOrder() && g_ids.cogDropOff >= 0)
						gs->commands[g_ids.cogDropOff].execute(cog, dock, Tags::ORDERASSIGNMODE_FORGET_EVERYTHING_ELSE);
					const float load = cog->getItem(Tags::PDITEM_WOOD) + cog->getItem(Tags::PDITEM_GOLD) + cog->getItem(Tags::PDITEM_FOOD);
					if (load <= 0.0f && c.cogLoad > 0.0f && (cog->position - homeDock->position).len2xz() < 60.0f) {
						c.shipped += (int)c.cogLoad;
						c.trips++;
					}
					c.cogLoad = load;
				}
				// a cart at the home dock, to the Manor: a new one from the palace, else the nearest at home
				ServerGameObject* cart = Obj(c.homeCart);
				Order* co = cart ? cart->orderConfig.getCurrentOrder() : nullptr;
				if (!cart || !co) {
					if (!cart) {
						ServerGameObject* home = Home(player);
						const int homeRegion = home ? RegionNear(cls, home->position) : -1;
						for (ServerGameObject* o : all)
							if (o->blueprint == g_ids.cart && o->isInteractable() && RegionNear(cls, o->position) == homeRegion
								&& !std::any_of(st.colonies.begin(), st.colonies.end(), [&](const Naval::Colony& k) { return k.homeCart == o->id; })
								&& (!cart || (o->position - homeDock->position).sqlen2xz() < (cart->position - homeDock->position).sqlen2xz()))
								cart = o;
						if (!cart && home && now >= c.nextCart && homeDock->getItem(Tags::PDITEM_WOOD) + homeDock->getItem(Tags::PDITEM_GOLD) > 0.0f) {
							c.nextCart = now + 60.0f;
							if (AIUtil::Train(player, home, "Cart"))
								printf("AI %u: colony %u: a cart ordered for the home dock\n", player->id, c.id);
						}
					}
					if (cart && g_ids.cartTransport >= 0) {
						gs->commands[g_ids.cartTransport].execute(cart, homeDock, Tags::ORDERASSIGNMODE_FORGET_EVERYTHING_ELSE);
						if (c.homeCart != cart->id)
							printf("AI %u: colony %u: the cart %u carries from the home dock %u to the Manor\n", player->id, c.id, cart->id, homeDock->id);
						c.homeCart = cart->id;
					}
				}
			}
			// the island's own carts, idle: back to their village
			if (villageBuilt && g_ids.cartTransport >= 0)
				for (ServerGameObject* o : all)
					if (o->blueprint == g_ids.cart && o->isInteractable() && !o->orderConfig.getCurrentOrder() && RegionNear(cls, o->position) == c.region)
						gs->commands[g_ids.cartTransport].execute(o, village, Tags::ORDERASSIGNMODE_FORGET_EVERYTHING_ELSE);
			// and the settlers to work
			if (now >= c.nextWork) {
				c.nextWork = now + 10.0f;
				SettlersWork(player, c, res, seat);
			}
			++it;
		}
	}
}

namespace Naval {
	const GameObjBlueprint* LandType() { g_ids.update(Srv()->gameSet.get()); return g_ids.land; }
	const GameObjBlueprint* BoatType() { g_ids.update(Srv()->gameSet.get()); return g_ids.cog; }

	bool IsBoat(const ServerGameObject* o)
	{
		g_ids.update(Srv()->gameSet.get());
		return o && o->blueprint->bpClass == Tags::GAMEOBJCLASS_CHARACTER && g_ids.boat >= 0 && o->getItem(g_ids.boat) > 0.0f;
	}
	bool IsTransport(const ServerGameObject* o)
	{
		return IsBoat(o) && g_ids.maxWeight >= 0 && o->getItem(g_ids.maxWeight) > 0.0f;
	}

	std::vector<ServerGameObject*> Ships(ServerGameObject* player)
	{
		g_ids.update(Srv()->gameSet.get());
		std::vector<ServerGameObject*> all, ships;
		if (!player || g_ids.boat < 0)
			return ships;
		Walk(player, all);
		for (ServerGameObject* o : all)
			if (IsBoat(o) && !(o->flags & ServerGameObject::fTerminated))
				ships.push_back(o);
		std::sort(ships.begin(), ships.end(), [](ServerGameObject* a, ServerGameObject* b) { return a->id < b->id; });
		return ships;
	}

	bool Overseas(ServerGameObject* player, ServerGameObject* unit, const Vector3& place)
	{
		g_ids.update(Srv()->gameSet.get());
		// (only where the map has a sea route to an enemy: elsewhere nothing changes)
		if (!unit || !g_ids.cog || !Srv()->terrain || !player || !Srv()->aiNaval || player->aiController.naval.need <= NEED_NONE)
			return false;
		const int cls = MovementController::movementClass(unit->blueprint);
		const int own = MovementController::terrainRegion(cls, TileX(unit->position.x), TileX(unit->position.z));
		if (own < 0)
			return false;
		const int there = RegionNear(cls, place);
		if (there < 0 || there == own)
			return false;
		const int boatCls = MovementController::movementClass(g_ids.cog);
		const Shore mine = ShoreNear(boatCls, -1, cls, own, unit->position, 100);
		if (!mine.ok)
			return false;
		return ShoreNear(boatCls, mine.boatRegion, cls, there, place, 100).ok;
	}

	uint32_t SendFerry(ServerGameObject* player, const std::vector<ServerGameObject*>& units, ServerGameObject* target,
		const Vector3& place, Ferry::Owner owner, uint32_t mission)
	{
		g_ids.update(Srv()->gameSet.get());
		if (!player || units.empty() || g_ids.board < 0)
			return 0;
		State& st = player->aiController.naval;
		// a free transport: the nearest to the units
		Vector3 centre;
		for (ServerGameObject* u : units)
			centre += u->position;
		centre = centre * (1.0f / units.size());
		ServerGameObject* ship = nullptr;
		for (ServerGameObject* s : Ships(player))
			if (IsTransport(s) && !InFerry(st, s->id) && !OnRoute(player, s->id) && PassengersOf(s) == 0
				&& (!ship || (s->position - centre).sqlen2xz() < (ship->position - centre).sqlen2xz()))
				ship = s;
		if (!ship)
			return 0;
		Ferry f;
		f.id = st.nextFerry++;
		f.ship = ship->id;
		f.target = target ? target->id : 0;
		f.targetAt = target ? target->position : place;
		f.owner = owner;
		f.mission = mission;
		f.since = f.stateSince = Now();
		f.embark = ship->position;
		const GameSet* gs = Srv()->gameSet.get();
		const float room = g_ids.maxWeight >= 0 ? ship->getItem(g_ids.maxWeight) - (g_ids.curWeight >= 0 ? ship->getItem(g_ids.curWeight) : 0.0f) : 20.0f;
		float w = 0.0f;
		for (ServerGameObject* u : units) {
			if (!CanBoard(u))
				continue;
			const float uw = PassengerWeight(u);
			if (w + uw > room)
				continue;
			w += uw;
			// out of its army or formation first (the reaction moves it to the player anyway)
			if (u->getParent() != player)
				u->setParent(player);
			f.passengers.push_back(u->id);
			player->aiController.navalUnits.insert(u->id);
			gs->commands[g_ids.board].execute(u, ship, Tags::ORDERASSIGNMODE_FORGET_EVERYTHING_ELSE);
		}
		if (f.passengers.empty())
			return 0;
		st.ferries.push_back(f);
		printf("AI %u: ferry %u boards %zu (weight %.0f of %.0f) on %s %u at t=%.0f\n", player->id, f.id, f.passengers.size(), w, room,
			ship->blueprint->name.c_str(), ship->id, Now());
		return f.id;
	}

	const Ferry* FindFerry(ServerGameObject* player, uint32_t ferry)
	{
		if (!player)
			return nullptr;
		for (const Ferry& f : player->aiController.naval.ferries)
			if (f.id == ferry)
				return &f;
		return nullptr;
	}
	Ferry::State FerryState(ServerGameObject* player, uint32_t ferry)
	{
		const Ferry* f = FindFerry(player, ferry);
		return f ? f->state : Ferry::DONE;
	}

	void Update(ServerGameObject* player)
	{
		Server* srv = Srv();
		if (!srv->aiNaval || !srv->terrain || player->blueprint->bpClass != Tags::GAMEOBJCLASS_PLAYER)
			return;
		g_ids.update(srv->gameSet.get());
		if (!g_ids.cog || !g_ids.land || !g_ids.dockFoundation || g_ids.board < 0)
			return;
		State& st = player->aiController.naval;
		const float now = Now();
		// (the game's own start: a map's clock may not start at 0, 3 Isles' at 6242 s)
		if (st.start < 0.0f) {
			st.start = now;
			// (tests: WKB_COLONY_MANOR=1 gives every AI "Can Build Manor" from the start, as a Castle
			// would, to see the colonies' Manors without playing an hour to a Castle)
			if (const char* e = getenv("WKB_COLONY_MANOR"); e && atoi(e) == 1 && g_ids.canBuildManor >= 0)
				player->setItem(g_ids.canBuildManor, 1.0f);
		}
		// the map, every 60 s (from 5 s: the regions are made then)
		if (now >= st.nextCheck && now - st.start >= 5.0f) {
			st.nextCheck = now + 60.0f;
			ReadMap(player, st);
		}
		if (st.need <= NEED_NONE && st.ferries.empty() && !st.hadShips)
			return; // (nothing at all on a map without a sea between: the game as before)
		// the ferries every step (boarding and landing want it), the rest every 2 s
		for (Ferry& f : st.ferries)
			StepFerry(player, st, f);
		// (a finished ferry is kept 30 s: a mission or a Lua AI reads how it went)
		st.ferries.erase(std::remove_if(st.ferries.begin(), st.ferries.end(), [&](const Ferry& f) { return f.state == Ferry::DONE && now - f.stateSince > 30.0f; }), st.ferries.end());
		if (now < st.nextThink)
			return;
		st.nextThink = now + 2.0f;
		const bool aggressive = srv->aggressiveAI;
		const bool ministry = player->aiController.ministryWaves;
		if (st.need > NEED_NONE) {
			LayVillage(player, st);
			LayDock(player, st);
			KeepBuilding(player, st);
			// the layer's numbers: the Kingmaker one cog and two war boats where the sea is the only
			// way (one where it is the shorter), the aggressive AI two cogs and three; by its General's
			// aggression a war boat more or less
			const float ag = Aggression(player);
			int transports = st.need == NEED_ONLY ? (aggressive ? 2 : 1) : 1;
			int warships = st.need == NEED_ONLY ? (aggressive ? 3 : 2) : 1;
			warships += ag > 0.7f ? 1 : ag < 0.3f ? -1 : 0;
			if (st.wantTransports >= 0) transports = st.wantTransports;
			if (st.wantWarships >= 0) warships = st.wantWarships;
			// a cog more for each colony's supply route (and one for the settlers' ferry while one
			// is being founded with every transport at work)
			for (const Colony& c : st.colonies)
				transports += (c.plan == Colony::ROUTE) + (c.plan == Colony::ROUTE && c.routeSet && !c.cog);
			// cogs first; war boats once the first ferry could go (not before minute 6, or the
			// opening's wood went into them)
			TrainShips(player, st, transports, now - st.start > 360.0f || ministry ? std::max(warships, 0) : 0, aggressive);
		}
		st.hadShips = st.hadShips || !Ships(player).empty();
		// the colonies: other islands' resources (a Manor there, or the supply route by sea)
		if (st.need > NEED_NONE || !st.colonies.empty())
			Colonies(player, st);
		if (Trace() && now >= st.nextTrace) {
			st.nextTrace = now + 30.0f;
			ServerGameObject* d = FindDock(player, st, false);
			Log(player, "status: %.0f wood %.0f food %.0f gold; village %d; dock %u %s; training %s; free fighters %d; %s", player->getItem(Tags::PDITEM_WOOD), player->getItem(Tags::PDITEM_FOOD),
				player->getItem(Tags::PDITEM_GOLD), g_ids.canBuildDock >= 0 ? (int)player->getItem(g_ids.canBuildDock) : -1, d ? d->id : 0, d ? d->blueprint->name.c_str() : "-",
				st.lastTrainWant == 0 ? "nothing wanted" : st.lastTrainWant == 1 ? "a ship" : "none trainable", st.lastPool, Census(player).c_str());
		}
		Fleet(player, st);
		// a ship carrying passengers no ferry knows of (a loaded game: ferries aren't saved): sailed on
		for (ServerGameObject* s : Ships(player))
			if (IsTransport(s) && PassengersOf(s) > 0 && !InFerry(st, s->id) && !IsRouteShip(st, s->id) && st.need > NEED_NONE && g_ids.passengers >= 0) {
				Ferry f;
				f.id = st.nextFerry++;
				f.ship = s->id;
				f.embark = st.homeShore;
				f.since = f.stateSince = now;
				for (const SrvGORef& r : s->associates.at(g_ids.passengers))
					if (ServerGameObject* u = r.get()) {
						f.passengers.push_back(u->id);
						player->aiController.navalUnits.insert(u->id);
					}
				ServerGameObject* e = Obj(st.enemy);
				ServerGameObject* target = e ? AIUtil::Palace(e) : nullptr;
				f.target = target ? target->id : 0;
				f.targetAt = target ? target->position : st.enemyShore;
				bool ok = false;
				const int sea = MovementController::terrainRegion(MovementController::movementClass(s->blueprint), TileX(s->position.x), TileX(s->position.z));
				f = LandingFor(player, sea, f.targetAt, f, ok);
				if (!ok)
					continue;
				f.state = Ferry::SAILING;
				AIController::moveUnitsTo({ s }, player, f.landing);
				st.ferries.push_back(f);
				Log(player, "took up ship %u with %zu passengers again", s->id, f.passengers.size());
			}
		if (!ministry)
			LayerFerries(player, st, aggressive);
	}

	int HomeLandRegion(ServerGameObject* player, int* cls)
	{
		g_ids.update(Srv()->gameSet.get());
		const GameObjBlueprint* peasant = Srv()->gameSet->findBlueprint(Tags::GAMEOBJCLASS_CHARACTER, "Peasant");
		ServerGameObject* home = player ? Home(player) : nullptr;
		if (!peasant || !home || !Srv()->terrain)
			return -1;
		const int c = MovementController::movementClass(peasant);
		if (cls)
			*cls = c;
		return RegionNear(c, home->position);
	}

	int LandRegionAt(int cls, const Vector3& place)
	{
		return Srv()->terrain ? RegionNear(cls, place) : -1;
	}

	const std::vector<int>& PeasantRegions(ServerGameObject* player, int* cls)
	{
		struct Cache { uint64_t game = 0; float at = -1.0f; int cls = -1; std::vector<int> regions; };
		static std::map<uint32_t, Cache> caches;
		static const std::vector<int> none;
		if (!player || !Srv()->terrain)
			return none;
		Cache& c = caches[player->id];
		const uint64_t game = Srv()->gameSet->serial;
		if (c.game != game || Now() < c.at || Now() - c.at >= 1.0f) {
			c.game = game;
			c.at = Now();
			c.regions.clear();
			const int home = HomeLandRegion(player, &c.cls);
			if (home >= 0)
				c.regions.push_back(home);
			if (c.cls >= 0)
				for (ServerGameObject* u : AIUtil::Peasants(player))
					if (const int r = RegionNear(c.cls, u->position); r >= 0)
						c.regions.push_back(r);
			std::sort(c.regions.begin(), c.regions.end());
			c.regions.erase(std::unique(c.regions.begin(), c.regions.end()), c.regions.end());
		}
		if (cls)
			*cls = c.cls;
		return c.regions;
	}

	bool PlaceOverseas(ServerGameObject* player, const Vector3& place)
	{
		if (!player || player->aiController.naval.need <= NEED_NONE)
			return false;
		int cls = -1;
		const int home = HomeLandRegion(player, &cls);
		if (home < 0 || cls < 0)
			return false;
		const int there = RegionNear(cls, place);
		return there >= 0 && there != home;
	}

	bool StartAtHomeIfUnreachable(ServerGameObject* player, Vector3& from)
	{
		int cls = -1;
		const std::vector<int>& regions = PeasantRegions(player, &cls);
		if (regions.empty() || cls < 0)
			return false;
		const int r = RegionNear(cls, from);
		if (r < 0 || std::binary_search(regions.begin(), regions.end(), r))
			return false;
		ServerGameObject* home = Home(player);
		if (!home)
			return false;
		// (only to a home on the peasants' land: on 3 Isles Kolvas's home is on the central landmass
		// and his Manor's search went there, the farms all refused and his commission stalled)
		const int h = RegionNear(cls, home->position);
		if (h < 0 || !std::binary_search(regions.begin(), regions.end(), h))
			return false;
		from = home->position;
		return true;
	}

	Vector3 HomeSite(ServerGameObject* player, const GameObjBlueprint* foundation, const Vector3& wanted)
	{
		g_ids.update(Srv()->gameSet.get());
		if (!player || !foundation || !g_ids.land || !Srv()->terrain || (wanted.x == 0.0f && wanted.z == 0.0f))
			return wanted;
		ServerGameObject* home = Home(player);
		if (!home)
			return wanted;
		const int landCls = MovementController::movementClass(g_ids.land);
		const int homeRegion = RegionNear(landCls, home->position);
		const int there = RegionNear(landCls, wanted);
		if (homeRegion < 0 || there < 0 || there == homeRegion)
			return wanted;
		// the nearest to the site that was wanted, among the valid ones on the home's land
		Vector3 best;
		float bd = -1.0f;
		for (float r = 20.0f; r <= 250.0f; r += 10.0f) {
			for (int k = 0; k < 24; k++) {
				const float a = k * 6.2831853f / 24.0f;
				Vector3 p(home->position.x + r * std::sin(a), 0.0f, home->position.z + r * std::cos(a));
				const int tx = TileX(p.x), tz = TileX(p.z);
				if (!InMap(tx, tz) || MovementController::terrainRegion(landCls, tx, tz) != homeRegion)
					continue;
				p = TileCentre(tx, tz);
				const float d = (p - wanted).sqlen2xz();
				if (bd >= 0.0f && d >= bd)
					continue;
				if (StampdownPlan::getStampdownPlan(Srv(), player, foundation, p, Vector3(0, 0, 0)).status == StampdownPlan::Status::Valid) {
					best = p;
					bd = d;
				}
			}
			if (bd >= 0.0f && r >= 60.0f)
				break; // (the nearest rings found one: no need to go further out)
		}
		if (bd < 0.0f) {
			Log(player, "no site on its own land for %s (wanted at (%.0f, %.0f), across the water)", foundation->name.c_str(), wanted.x, wanted.z);
			return Vector3(0, 0, 0);
		}
		Log(player, "%s moved from (%.0f, %.0f) across the water to (%.0f, %.0f) on its own land", foundation->name.c_str(), wanted.x, wanted.z, best.x, best.z);
		return best;
	}

	bool OnRoute(ServerGameObject* player, uint32_t ship)
	{
		return player && IsRouteShip(player->aiController.naval, ship);
	}

	std::string ColonyCensus(ServerGameObject* player)
	{
		const State& st = player->aiController.naval;
		std::string s = "founded " + std::to_string(st.coloniesFounded) + ", manors " + std::to_string(st.colonyManors) + ", routes " + std::to_string(st.routesOpened)
			+ ", castles laid " + std::to_string(st.castlesLaid);
		for (const Colony& c : st.colonies) {
			char buf[256];
			snprintf(buf, sizeof(buf), "; colony %u (%s, %s): %zu settlers, village %u, manor %u, dock %u, cog %u, %d trips, %d shipped", c.id,
				c.plan == Colony::MANOR ? "manor" : c.plan == Colony::ROUTE ? "route" : "undecided",
				c.stage == Colony::SETTLING ? "settling" : c.stage == Colony::FOUNDING ? "founding" : "working",
				c.settlers.size(), c.village, c.manor, c.dock, c.cog, c.trips, c.shipped);
			s += buf;
		}
		return s;
	}

	std::string Census(ServerGameObject* player)
	{
		const State& st = player->aiController.naval;
		int transports = 0, warships = 0;
		for (ServerGameObject* s : Ships(player))
			(IsTransport(s) ? transports : warships)++;
		char buf[256];
		snprintf(buf, sizeof(buf), "need %d, docks laid %d, ships %d+%d (ordered %d), ferries %d sent, %d landed (%d units), attacks %d at boats, %d at the coast, %d at docks",
			(int)st.need, st.docksLaid, transports, warships, st.shipsOrdered, st.ferriesSent, st.ferriesLanded, st.unitsLanded, st.shipAttacks, st.coastAttacks, st.dockAttacks);
		return buf;
	}

	// The save file's "naval" section: per AI player with anything at sea "PLAYER <id>", then
	// "STATE ...", a "FERRY ...", "BUILDER ...", "COLONY ...", "FAIL ..." line each, "HELD <n> <ids>"
	// (the units the layer holds: navalUnits) and "END". Numbers by whitespace, floats at %.9g (read
	// back to the same bits), id lists as <count> <ids...>.
	namespace {
		struct Out {
			std::string s;
			void f(float v) { char b[32]; snprintf(b, sizeof(b), " %.9g", v); s += b; }
			void u(uint64_t v) { s += " " + std::to_string(v); }
			void i(int64_t v) { s += " " + std::to_string(v); }
			void v(const Vector3& p) { f(p.x); f(p.y); f(p.z); }
			template <class C> void ids(const C& a) { u(a.size()); for (uint32_t x : a) u(x); }
			void tag(const char* t) { s += t; }
			void end() { s += "\n"; }
		};
		struct In {
			std::istringstream is;
			explicit In(const std::string& line) : is(line) {}
			float f() { std::string w; is >> w; return w.empty() ? 0.0f : strtof(w.c_str(), nullptr); }
			uint32_t u() { unsigned long long x = 0; is >> x; return (uint32_t)x; }
			int i() { long long x = 0; is >> x; return (int)x; }
			Vector3 v() { const float x = f(), y = f(), z = f(); return Vector3(x, y, z); }
			std::vector<uint32_t> ids() { std::vector<uint32_t> a; const uint32_t n = u(); for (uint32_t k = 0; k < n && k < 100000 && is; k++) a.push_back(u()); return a; }
		};
		bool Worth(const State& st)
		{
			return st.need != NEED_UNKNOWN || !st.ferries.empty() || !st.colonies.empty() || st.dock || st.hadShips || st.start >= 0.0f;
		}
	}

	std::vector<ServerGameObject*> AIPlayers()
	{
		std::vector<ServerGameObject*> list;
		for (ServerGameObject* p : Players())
			if (Worth(p->aiController.naval))
				list.push_back(p);
		return list;
	}

	std::string SaveText()
	{
		Out o;
		for (ServerGameObject* p : Players()) {
			const State& st = p->aiController.naval;
			if (!Worth(st))
				continue;
			o.tag("PLAYER"); o.u(p->id); o.end();
			o.tag("STATE"); o.i(st.need); o.u(st.enemy); o.v(st.home); o.v(st.homeShore); o.v(st.enemyShore);
			o.f(st.nextCheck); o.f(st.nextThink); o.f(st.nextDockTry); o.f(st.nextVillageTry); o.f(st.nextBuildersCheck);
			o.i(st.hadShips); o.f(st.start); o.f(st.nextTrace); o.i(st.lastTrainWant); o.i(st.lastPool); o.u(st.dock);
			o.i(st.wantTransports); o.i(st.wantWarships); o.i(st.manual); o.i(st.huntDocks); o.i(st.dockAttacks);
			o.u(st.nextFerry); o.f(st.lastFerry);
			o.i(st.docksLaid); o.i(st.shipsOrdered); o.i(st.warshipsOrdered); o.i(st.ferriesSent); o.i(st.ferriesLanded); o.i(st.unitsLanded);
			o.i(st.shipAttacks); o.i(st.coastAttacks); o.i(st.villagesLaid);
			o.u(st.homeId); o.u(st.nextColony); o.f(st.nextColonyCheck); o.f(st.nextFounding);
			o.i(st.coloniesFounded); o.i(st.colonyManors); o.i(st.routesOpened);
			o.u(st.castleFoundation); o.i(st.castlesLaid); o.f(st.nextCastleTry); o.end();
			for (const Ferry& f : st.ferries) {
				o.tag("FERRY"); o.u(f.id); o.u(f.ship); o.u(f.target); o.v(f.targetAt); o.v(f.embark); o.v(f.landing);
				o.i(f.state); o.i(f.owner); o.u(f.mission); o.f(f.since); o.f(f.stateSince); o.f(f.nextNudge); o.f(f.nextSail);
				o.i(f.landed); o.ids(f.passengers); o.i(f.sails); o.end();
			}
			for (const auto& [id, b] : st.builders) {
				o.tag("BUILDER"); o.u(id); o.u(b.first); o.f(b.second); o.end();
			}
			for (const Colony& c : st.colonies) {
				o.tag("COLONY"); o.u(c.id); o.i(c.region); o.v(c.site); o.i(c.plan); o.i(c.stage);
				o.u(c.ferry); o.u(c.village); o.u(c.manor); o.u(c.dock); o.u(c.cog); o.u(c.homeCart);
				o.f(c.since); o.f(c.nextWork); o.f(c.nextWeigh); o.f(c.nextLay); o.f(c.nextCart); o.f(c.nextSettlers);
				o.f(c.value); o.f(c.cogLoad); o.i(c.routeSet); o.i(c.shipped); o.i(c.trips); o.ids(c.settlers); o.end();
			}
			for (const auto& [region, fail] : st.colonyFails) {
				o.tag("FAIL"); o.i(region); o.i(fail.first); o.f(fail.second); o.end();
			}
			o.tag("HELD"); o.ids(p->aiController.navalUnits); o.end();
			o.tag("END"); o.end();
		}
		return o.s;
	}

	void LoadText(const std::string& text)
	{
		Server* srv = Srv();
		ServerGameObject* p = nullptr;
		std::istringstream lines(text);
		std::string line;
		while (std::getline(lines, line)) {
			if (!line.empty() && line.back() == '\r')
				line.pop_back();
			In in(line);
			std::string tag;
			in.is >> tag;
			if (tag == "PLAYER") {
				p = srv->findObject(in.u());
				if (p && p->blueprint->bpClass != Tags::GAMEOBJCLASS_PLAYER)
					p = nullptr;
				if (p)
					p->aiController.naval = State();
				continue;
			}
			if (!p)
				continue;
			State& st = p->aiController.naval;
			AIController& ai = p->aiController;
			if (tag == "STATE") {
				st.need = (Need)in.i(); st.enemy = in.u(); st.home = in.v(); st.homeShore = in.v(); st.enemyShore = in.v();
				st.nextCheck = in.f(); st.nextThink = in.f(); st.nextDockTry = in.f(); st.nextVillageTry = in.f(); st.nextBuildersCheck = in.f();
				st.hadShips = in.i() != 0; st.start = in.f(); st.nextTrace = in.f(); st.lastTrainWant = in.i(); st.lastPool = in.i(); st.dock = in.u();
				st.wantTransports = in.i(); st.wantWarships = in.i(); st.manual = in.i() != 0; st.huntDocks = in.i() != 0; st.dockAttacks = in.i();
				st.nextFerry = in.u(); st.lastFerry = in.f();
				st.docksLaid = in.i(); st.shipsOrdered = in.i(); st.warshipsOrdered = in.i(); st.ferriesSent = in.i(); st.ferriesLanded = in.i(); st.unitsLanded = in.i();
				st.shipAttacks = in.i(); st.coastAttacks = in.i(); st.villagesLaid = in.i();
				st.homeId = in.u(); st.nextColony = in.u(); st.nextColonyCheck = in.f(); st.nextFounding = in.f();
				st.coloniesFounded = in.i(); st.colonyManors = in.i(); st.routesOpened = in.i();
				st.castleFoundation = in.u(); st.castlesLaid = in.i(); st.nextCastleTry = in.f();
			}
			else if (tag == "FERRY") {
				Ferry f;
				f.id = in.u(); f.ship = in.u(); f.target = in.u(); f.targetAt = in.v(); f.embark = in.v(); f.landing = in.v();
				f.state = (Ferry::State)in.i(); f.owner = (Ferry::Owner)in.i(); f.mission = in.u();
				f.since = in.f(); f.stateSince = in.f(); f.nextNudge = in.f(); f.nextSail = in.f();
				f.landed = in.i(); f.passengers = in.ids(); f.sails = in.i();
				if (f.state <= Ferry::DONE && f.owner <= Ferry::COLONY)
					st.ferries.push_back(std::move(f));
			}
			else if (tag == "BUILDER") {
				const uint32_t id = in.u(), foundation = in.u();
				const float since = in.f();
				st.builders[id] = { foundation, since };
				ai.ministryHeld.insert(id); // (SendBuilders holds them)
			}
			else if (tag == "COLONY") {
				Colony c;
				c.id = in.u(); c.region = in.i(); c.site = in.v(); c.plan = (Colony::Plan)in.i(); c.stage = (Colony::Stage)in.i();
				c.ferry = in.u(); c.village = in.u(); c.manor = in.u(); c.dock = in.u(); c.cog = in.u(); c.homeCart = in.u();
				c.since = in.f(); c.nextWork = in.f(); c.nextWeigh = in.f(); c.nextLay = in.f(); c.nextCart = in.f(); c.nextSettlers = in.f();
				c.value = in.f(); c.cogLoad = in.f(); c.routeSet = in.i() != 0; c.shipped = in.i(); c.trips = in.i(); c.settlers = in.ids();
				ai.ministryHeld.insert(c.settlers.begin(), c.settlers.end()); // (SendSettlers holds them)
				if (c.plan <= Colony::ROUTE && c.stage <= Colony::WORKING)
					st.colonies.push_back(std::move(c));
			}
			else if (tag == "FAIL") {
				const int region = in.i(), n = in.i();
				const float until = in.f();
				st.colonyFails[region] = { n, until };
			}
			else if (tag == "HELD") {
				const std::vector<uint32_t> held = in.ids();
				ai.navalUnits.insert(held.begin(), held.end());
			}
			else if (tag == "END")
				p = nullptr;
		}
	}
}
