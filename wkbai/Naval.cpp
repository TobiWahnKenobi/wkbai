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

	ServerGameObject* Home(ServerGameObject* player)
	{
		if (ServerGameObject* p = AIUtil::Palace(player))
			return p;
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
	Naval::Ferry LandingFor(ServerGameObject* player, int boatRegion, const Vector3& at, Naval::Ferry f, bool& ok)
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
		// a sample 15 m apart at least, nearest first
		std::vector<Shore> sample;
		for (const Shore& s : found) {
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
		if (!home || !g_ids.cog || !g_ids.land)
			return;
		st.home = home->position;
		const int boatCls = MovementController::movementClass(g_ids.cog), landCls = MovementController::movementClass(g_ids.land);
		const int homeRegion = RegionNear(landCls, st.home);
		if (homeRegion < 0)
			return;
		const Shore hs = ShoreNear(boatCls, -1, landCls, homeRegion, st.home, 100);
		if (!hs.ok)
			return; // no sea by this home
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
			const bool done = !f || (f->blueprint != g_ids.dockFoundation && f->blueprint != g_ids.villageFoundation) || Now() - it->second.second > 240.0f;
			if (done) {
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
	}

	// A village where the sea is the only way and the scripts have none 4 minutes in (it unlocks
	// the dock; on the carved 4 Peaks one General's scripts never laid one in 20 minutes): rings
	// round the palace 40 to 150 m out, by the game's placement rule
	void LayVillage(ServerGameObject* player, Naval::State& st)
	{
		if (!g_ids.villageFoundation || st.need != Naval::NEED_ONLY || Now() < 240.0f || Now() < st.nextVillageTry)
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
					if (g_ids.readyToBoard >= 0 && !ship->movementController.isMoving() && !u->movementController.isMoving()
						&& (u->position - ship->position).sqlen2xz() < 15.0f * 15.0f && now - f.stateSince > 3.0f)
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
					f = LandingFor(player, sea, ship->position, f, ok);
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
					Naval::Ferry g = LandingFor(player, sea, f.targetAt + (ship->position - f.targetAt) * 0.5f, f, ok);
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
			if (w->movementController.isMoving())
				continue;
			// on to where it is wanted (a little off: the boats spread around the place)
			Vector3 to = escort ? escort->landing : (st.need == Naval::NEED_NONE ? st.homeShore : st.enemyShore);
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
			if (Naval::IsTransport(s) && !InFerry(st, s->id) && PassengersOf(s) == 0 && (!transport || s->id < transport->id))
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
		const bool long_ = st.lastFerry < 0.0f ? now > 600.0f : now - st.lastFerry > 360.0f;
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
			if (IsTransport(s) && !InFerry(st, s->id) && PassengersOf(s) == 0
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
		// the map, every 60 s (from 5 s: the regions are made then)
		if (now >= st.nextCheck && now >= 5.0f) {
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
			// cogs first; war boats once the first ferry could go (not before minute 6, or the
			// opening's wood went into them)
			TrainShips(player, st, transports, now > 360.0f || ministry ? std::max(warships, 0) : 0, aggressive);
		}
		st.hadShips = st.hadShips || !Ships(player).empty();
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
			if (IsTransport(s) && PassengersOf(s) > 0 && !InFerry(st, s->id) && st.need > NEED_NONE && g_ids.passengers >= 0) {
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

	std::string Census(ServerGameObject* player)
	{
		const State& st = player->aiController.naval;
		int transports = 0, warships = 0;
		for (ServerGameObject* s : Ships(player))
			(IsTransport(s) ? transports : warships)++;
		char buf[256];
		snprintf(buf, sizeof(buf), "need %d, docks laid %d, ships %d+%d (ordered %d), ferries %d sent, %d landed (%d units), attacks %d at boats, %d at the coast",
			(int)st.need, st.docksLaid, transports, warships, st.shipsOrdered, st.ferriesSent, st.ferriesLanded, st.unitsLanded, st.shipAttacks, st.coastAttacks);
		return buf;
	}
}
