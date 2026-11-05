// wkbai - the AI and the day and night, seasons and weather ("aiWeather"; wkbai's own, not in
// WKB.exe). The weather's effects are in ServerEnvironment.cpp; this is how an AI player answers
// them. Every 2 s, only while a setting is on, with no random numbers:
// - Cold and heat: a living unit out of shelter (45 m from its own or an ally's village, manor,
//   keep, castle or palace) whose exposure would pass 90 before it could walk back goes to the
//   nearest shelter, and idle ones go at once; they stay there while the cold or heat harms. No
//   new wave sets out into harmful cold or heat unless its target is near enough.
// - Ice: while the temperature is near the thaw, the AI's units on the frozen deep water walk to
//   the nearest land, and no wave sets out over the ice.
// - Floods: units standing in water over 0.7 m deep (0.3 m: the undead), counted half a metre
//   ahead while it rises, walk to dry land; no foundation is laid on land the flood there is or
//   coming would cover.
// - Fires: units within 9 m of a burning patch step 12 m away from it; up to 3 peasants put out
//   each of the AI's burning buildings (the scripts' "Extinguish", then the repair it goes on to),
//   and a burning building's garrison is let out once it is half burnt down.
// - Day and night: a wave ready by day waits (up to 40% of a day, 6 minutes at most) so that it
//   arrives after dark, when the defenders' sight is short; at night a third of the soldiers at
//   home (at least 10) stay home as a guard.
// A unit the weather moves is held (weatherHeld): the work orders, the war layer and focus fire
// leave it alone, and it is taken out of the scripts' armies (whose muster calls would walk it
// back out) until it is released, then given back.
#include "AIController.h"
#include "server.h"
#include "terrain.h"
#include "gameset/ScriptContext.h"
#include "gameset/gameset.h"
#include "Order.h"
#include "gameset/command.h"
#include "NNSearch.h"
#include "tags.h"
#include <algorithm>
#include <cmath>
#include <set>

namespace {
	constexpr float WALK_SPEED = 3.5f; // m/s: a unit on foot, for the times to walk somewhere
	constexpr float NIGHT = 0.3f;      // daylight under which it counts as night (sight 0.6 or less)
	enum HoldWhy : uint8_t { HOLD_COLD = 1, HOLD_HEAT, HOLD_ICE, HOLD_FLOOD, HOLD_FIRE, HOLD_EXTINGUISH };

	struct WItems {
		const GameSet* of = nullptr;
		int moveCommand = -1, buildOrder = -1, buildWallOrder = -1, extinguishOrder = -1, repairOrder = -1, ungarrison = -1, peasant = -1, si = -1, siCap = -1, burning = -1;
		void update(const GameSet* gs) {
			if (of == gs)
				return;
			of = gs;
			moveCommand = gs->commands.names.getIndex("Move");
			extinguishOrder = gs->orders.names.getIndex("Extinguish");
			repairOrder = gs->orders.names.getIndex("Repair");
			buildOrder = gs->orders.names.getIndex("Build");
			buildWallOrder = gs->orders.names.getIndex("Build Wall");
			ungarrison = gs->orderAssignments.names.getIndex("UnGarrison");
			peasant = gs->items.names.getIndex("I Am A Peasant");
			si = gs->items.names.getIndex("Structural Integrity");
			siCap = gs->items.names.getIndex("Structural Integrity Capacity");
			burning = gs->aliases.names.getIndex("Burning Buildings");
		}
	} g_w;

	// the player's characters out in the open (not garrisoned: those under a building), in the
	// tree's order
	void CollectCharacters(ServerGameObject* o, std::vector<ServerGameObject*>& out)
	{
		for (const auto& [type, children] : o->children)
			for (CommonGameObject* c : children) {
				auto* u = (ServerGameObject*)c;
				if (u->deleted)
					continue;
				if (u->blueprint->bpClass == Tags::GAMEOBJCLASS_CHARACTER) {
					if (!(u->flags & CommonGameObject::fTerminated) && !u->blueprint->canWalkOnWater())
						out.push_back(u);
				}
				else if (u->blueprint->bpClass != Tags::GAMEOBJCLASS_BUILDING)
					CollectCharacters(u, out);
			}
	}

	float Daylight(const ServerEnvironment& env, float t)
	{
		return ComputeSun(EnvDayFraction(env.settings, t), EnvYearFraction(env.settings, t), env.settings.seasons, env.state.cloudCover).daylight;
	}

	bool OrderIs(ServerGameObject* u, int order)
	{
		const Order* o = u->orderConfig.getCurrentOrder();
		return order >= 0 && o && o->blueprint == &Server::instance->gameSet->orders[order];
	}

	ServerGameObject* OrderTarget(ServerGameObject* u)
	{
		Order* o = u->orderConfig.getCurrentOrder();
		Task* t = o ? o->getCurrentTask() : nullptr;
		ServerGameObject* tg = t ? t->target.get() : nullptr;
		return tg && !tg->deleted ? tg : nullptr;
	}

	// the middle of the nearest playable tile, from `from` outwards (up to 30 tiles), that `ok` takes
	template <typename F> bool NearestTile(const Vector3& from, F&& ok, Vector3& found)
	{
		Terrain* t = Server::instance->terrain;
		const auto [w, h] = t->getNumPlayableTiles();
		const int cx = (int)(from.x / 5.0f), cz = (int)(from.z / 5.0f);
		for (int r = 1; r <= 30; r++) {
			bool any = false;
			float best = 1e30f;
			for (int dz = -r; dz <= r; dz++)
				for (int dx = -r; dx <= r; dx++) {
					if (std::max(std::abs(dx), std::abs(dz)) != r)
						continue;
					const int x = cx + dx, z = cz + dz;
					if (x < 0 || z < 0 || x >= w || z >= h)
						continue;
					const Vector3 p(x * 5.0f + 2.5f, 0.0f, z * 5.0f + 2.5f);
					const TerrainTile* tile = t->getPlayableTile(x, z);
					if (!tile || tile->fullOfWater || !ok(p))
						continue;
					const float d = (p - from).sqlen2xz();
					if (d < best) {
						best = d;
						found = p;
						any = true;
					}
				}
			if (any) {
				found.y = t->getHeight(found.x, found.z);
				return true;
			}
		}
		return false;
	}
}

bool AIController::weatherActive()
{
	Server* srv = Server::instance;
	if (!srv->environment.enabled() || srv->aiWeather == 0 || gameObj->blueprint->bpClass != Tags::GAMEOBJCLASS_PLAYER)
		return false;
	if (weatherState < 0) {
		// 1: all AIs; 2 and 3: every other one (by player id), as aiAdvanced, for comparisons
		int rank = 0;
		if (srv->getLevel())
			for (const auto& [type, children] : srv->getLevel()->children)
				if (type.bpClass() == Tags::GAMEOBJCLASS_PLAYER)
					for (CommonGameObject* p : children)
						if (p->id < gameObj->id)
							rank++;
		weatherState = srv->aiWeather == 1 || (srv->aiWeather == 2 && rank % 2 == 1) || (srv->aiWeather == 3 && rank % 2 == 0) ? 1 : 0;
	}
	return weatherState == 1;
}

// out of the scripts' armies and formations (the war layer's waves keep theirs: it skips held
// units), the order given, remembered with why and until when
void AIController::weatherHold(ServerGameObject* u, int why, float until, const Vector3* moveTo, ServerGameObject* target, int order)
{
	auto [it, added] = weatherHeld.try_emplace(u->id);
	WeatherHold& h = it->second;
	if (added) {
		h.parent = 0;
		if (const Order* o = u->orderConfig.getCurrentOrder()) {
			h.order = o->blueprint;
			Task* t = ((Order*)o)->getCurrentTask();
			h.target = t && t->target.get() ? t->target.get()->id : 0;
		}
		ServerGameObject* parent = u->getParent();
		bool waveArmy = false;
		if (parent)
			for (const WarWave& w : warWaves)
				waveArmy |= w.army == parent->id;
		if (parent && !waveArmy && (parent->blueprint->bpClass == Tags::GAMEOBJCLASS_ARMY || parent->blueprint->bpClass == Tags::GAMEOBJCLASS_FORMATION)) {
			h.parent = parent->id;
			u->setParent(gameObj);
		}
	}
	h.why = (uint8_t)why;
	h.until = until;
	const GameSet* gs = Server::instance->gameSet.get();
	if (moveTo && g_w.moveCommand >= 0) {
		// (the player's "Move" command: the game set has no "Move To" order)
		gs->commands[g_w.moveCommand].execute(u, nullptr, Tags::ORDERASSIGNMODE_FORGET_EVERYTHING_ELSE, *moveTo);
		h.sent = Server::instance->timeManager.currentTime;
	}
	else if (target && order >= 0)
		u->orderConfig.addOrder(&gs->orders[order], Tags::ORDERASSIGNMODE_FORGET_EVERYTHING_ELSE, target);
}

void AIController::weatherRelease(uint32_t id)
{
	auto it = weatherHeld.find(id);
	if (it == weatherHeld.end())
		return;
	ServerGameObject* u = Server::instance->findObject(id);
	if (u && !u->deleted && it->second.parent && u->getParent() == gameObj)
		if (ServerGameObject* p = Server::instance->findObject(it->second.parent); p && !p->deleted)
			u->setParent(p);
	// a builder's order back, on its foundation if that is still there (the scripts give the first
	// builders theirs, not a work order; the work orders reassign the rest. Only Build: "AI Chop
	// Tree" given back on the target of the moment, the Manor its wood went to, had the peasants
	// chop the Manor down, and the player was gone)
	if (u && !u->deleted && it->second.order && it->second.why != HOLD_EXTINGUISH && g_w.buildOrder >= 0
		&& it->second.order == &Server::instance->gameSet->orders[g_w.buildOrder]) {
		ServerGameObject* target = it->second.target ? Server::instance->findObject(it->second.target) : nullptr;
		if (getenv("WKB_TRACE_WEATHER"))
			printf("WEATHER %u t=%.0f release %s %u (why %d): order %s on %s\n", gameObj->id, Server::instance->timeManager.currentTime, u->blueprint->name.c_str(), u->id, it->second.why,
				Server::instance->gameSet->orders.getString(it->second.order->bpid).c_str(), target ? target->blueprint->name.c_str() : "-");
		if (!it->second.target || (target && !target->deleted))
			u->orderConfig.addOrder(it->second.order, Tags::ORDERASSIGNMODE_FORGET_EVERYTHING_ELSE, target);
	}
	weatherHeld.erase(it);
}

void AIController::weatherUpdate()
{
	Server* srv = Server::instance;
	const float now = srv->timeManager.currentTime;
	if (now < nextWeatherCheck)
		return;
	nextWeatherCheck = now + 2.0f;
	const ServerEnvironment& env = srv->environment;
	const GameSet* gs = srv->gameSet.get();
	g_w.update(gs);

	std::vector<ServerGameObject*> units;
	CollectCharacters(gameObj, units);
	// the held ones that are gone
	for (auto it = weatherHeld.begin(); it != weatherHeld.end();) {
		ServerGameObject* u = srv->findObject(it->first);
		if (!u || u->deleted)
			it = weatherHeld.erase(it);
		else
			++it;
	}

	const bool seasons = env.settings.seasons;
	// ---- Cold and heat ----
	{
		const float cold = env.coldFeel(), heat = env.heatFeel();
		const float snow = 1.0f + 0.5f * env.state.snowCover;
		// exposure a second, marching
		const float coldRate = seasons && cold < 2.0f ? (2.0f - cold) / 6.0f * snow * 1.5f : 0.0f;
		const float heatRate = seasons && heat > 30.0f ? (heat - 30.0f) / 4.0f * 1.5f : 0.0f;
		// harming now, or near it (the exposure only harms below -4 C or above 34 C)
		const bool coldHarms = seasons && cold < -3.0f, heatHarms = seasons && heat > 33.0f;
		// (kept home until it is a degree milder)
		const bool coldStays = seasons && cold < -2.0f, heatStays = seasons && heat > 32.0f;
		std::vector<const ServerEnvironment::ShelterSpot*> spots;
		if (coldRate > 0.0f || heatRate > 0.0f || !weatherHeld.empty())
			for (const auto& s : env.shelterSpots()) {
				if (s.player != gameObj->id) {
					ServerGameObject* p = srv->findObject(s.player);
					if (!p || srv->getDiplomaticStatus(p, gameObj) != 0)
						continue;
				}
				spots.push_back(&s);
			}
		auto nearestSpot = [&](const Vector3& at, float& d) -> const ServerEnvironment::ShelterSpot* {
			const ServerEnvironment::ShelterSpot* best = nullptr;
			float bd = 1e30f;
			for (const auto* s : spots) {
				const float dd = (s->pos - at).sqlen2xz();
				if (dd < bd) {
					bd = dd;
					best = s;
				}
			}
			d = best ? std::sqrt(bd) : 1e9f;
			return best;
		};
		for (ServerGameObject* u : units) {
			if (!env.feelsWeather(u->blueprint))
				continue;
			float d = 0.0f;
			const auto* spot = nearestSpot(u->position, d);
			const bool sheltered = spot && d <= ServerEnvironment::SHELTER_RADIUS - 3.0f;
			auto held = weatherHeld.find(u->id);
			const bool heldWarm = held != weatherHeld.end() && (held->second.why == HOLD_COLD || held->second.why == HOLD_HEAT);
			const float ec = env.coldExposure(u->id), eh = env.heatExposure(u->id);
			// peasants at work go back out once warm (short trips: their work goes on near home,
			// and further out as far as the cold lets them); the others stay home while it harms
			const bool worker = g_w.peasant >= 0 && u->getItem(g_w.peasant) > 0.0f;
			if (heldWarm) {
				// home while it harms; then once the exposure is down
				const bool isCold = held->second.why == HOLD_COLD;
				const bool harms = isCold ? coldStays : heatStays;
				const float e = isCold ? ec : eh;
				if ((!harms && (!sheltered || e < 30.0f || now > held->second.until)) || (harms && worker && sheltered && e < 5.0f)) {
					weatherRelease(u->id);
					continue;
				}
				// still out (stopped short, or a new order): sent again
				if (!sheltered && spot && (!u->movement.isMoving() || now - held->second.sent > 30.0f)) {
					if (getenv("WKB_TRACE_WEATHER"))
						printf("WEATHER %u t=%.0f resend %s %u: %.0f m from shelter, cold %.0f, moving %d, at (%.0f,%.0f) spot (%.0f,%.0f)\n", gameObj->id, now, u->blueprint->name.c_str(), u->id, d, ec,
							(int)u->movement.isMoving(), u->position.x, u->position.z, spot->pos.x, spot->pos.z);
					const Vector3 to = spot->pos + (u->position - spot->pos).normal2xz() * 25.0f;
					weatherHold(u, held->second.why, now + 120.0f, &to);
				}
				continue;
			}
			if (held != weatherHeld.end() || sheltered || !spot)
				continue;
			// builders build on through it: taken off their foundations, the Kingmaker's opening never
			// got its first village (a harsh winter from the start: 31 buildings at 16 minutes against
			// 80, the walk to the site costing half the exposure that harms), which cost more than the
			// few builders the cold takes
			if (OrderIs(u, g_w.buildOrder) || OrderIs(u, g_w.buildWallOrder) || OrderIs(u, g_w.repairOrder) || OrderIs(u, g_w.extinguishOrder))
				continue;
			// would it get back before the exposure passed 90? (idle ones go at once)
			const float walk = std::max(0.0f, d - 30.0f) / WALK_SPEED;
			const bool idle = !u->orderConfig.getCurrentOrder();
			int why = 0;
			// (a worker until the exposure would pass 100, where the harm begins)
			const float limit = worker && !idle ? 100.0f : 90.0f;
			if (coldHarms && coldRate > 0.0f && (ec + coldRate * walk > limit || idle))
				why = HOLD_COLD;
			else if (heatHarms && heatRate > 0.0f && (eh + heatRate * walk > limit || idle))
				why = HOLD_HEAT;
			if (!why)
				continue;
			const Vector3 to = spot->pos + (u->position - spot->pos).normal2xz() * 25.0f;
			if (getenv("WKB_TRACE_WEATHER")) {
				const Order* o = u->orderConfig.getCurrentOrder();
				printf("WEATHER %u t=%.0f shelter %s %u: %.0f m, cold %.0f, rate %.2f, order %s, parent %s\n", gameObj->id, now, u->blueprint->name.c_str(), u->id, d, ec, coldRate,
					o ? gs->orders.getString(o->blueprint->bpid).c_str() : "idle", u->getParent() ? u->getParent()->blueprint->name.c_str() : "-");
			}
			weatherHold(u, why, now + 120.0f, &to);
			weatherSheltered++;
		}
	}

	// ---- The ice: off the deep water before it thaws ----
	if (seasons && env.state.ice > 0.05f && env.state.temperatureC > -1.0f) {
		for (ServerGameObject* u : units) {
			if (!env.onDeepWater(u->position))
				continue;
			auto held = weatherHeld.find(u->id);
			if (held != weatherHeld.end() && held->second.why == HOLD_ICE && u->movement.isMoving())
				continue;
			Vector3 to;
			if (NearestTile(u->position, [&](const Vector3& p) { return !env.onDeepWater(p); }, to)) {
				weatherHold(u, HOLD_ICE, now + 30.0f, &to);
				weatherIceRetreats++;
			}
		}
	}
	// released once off it
	for (auto it = weatherHeld.begin(); it != weatherHeld.end();) {
		auto next = std::next(it);
		if (it->second.why == HOLD_ICE) {
			ServerGameObject* u = srv->findObject(it->first);
			if (!u || (!env.onDeepWater(u->position) && (!u->movement.isMoving() || now > it->second.until)))
				weatherRelease(it->first);
		}
		it = next;
	}

	// ---- Floods ----
	{
		const float target = env.waterTarget();
		const bool flooding = seasons && (env.state.waterRise > 0.1f || target > 0.3f);
		if (flooding) {
			// half a metre ahead while it rises
			const float rise = std::max(env.state.waterRise, std::min(target, env.state.waterRise + 0.5f));
			for (ServerGameObject* u : units) {
				const float limit = env.isUndead(u->blueprint) ? 0.3f : 0.7f;
				if (env.floodDepthAt(u->position, rise) <= limit)
					continue;
				auto held = weatherHeld.find(u->id);
				if (held != weatherHeld.end() && held->second.why == HOLD_FLOOD && u->movement.isMoving())
					continue;
				const float high = std::max(rise, target);
				Vector3 to;
				if (NearestTile(u->position, [&](const Vector3& p) { return env.floodDepthAt(p, high) <= 0.1f; }, to)) {
					weatherHold(u, HOLD_FLOOD, now + 40.0f, &to);
					weatherFloodRetreats++;
				}
			}
		}
		for (auto it = weatherHeld.begin(); it != weatherHeld.end();) {
			auto next = std::next(it);
			if (it->second.why == HOLD_FLOOD && (now > it->second.until || !flooding))
				weatherRelease(it->first);
			it = next;
		}
	}

	// ---- Fires ----
	{
		const std::vector<Vector3> fires = seasons ? env.firePositions() : std::vector<Vector3>();
		std::set<ServerGameObject*> moved;
		for (const Vector3& f : fires) {
			NNSearch search;
			search.start(srv, f, 9.0f);
			while (ServerGameObject* o = (ServerGameObject*)search.next()) {
				if (o->deleted || o->blueprint->bpClass != Tags::GAMEOBJCLASS_CHARACTER || o->getPlayer() != gameObj || o->blueprint->canWalkOnWater()
					|| (o->position - f).sqlen2xz() > 9.0f * 9.0f || !moved.insert(o).second)
					continue;
				Vector3 away = (o->position - f).normal2xz();
				if (away.x == 0.0f && away.z == 0.0f)
					away = Vector3(1, 0, 0);
				const Vector3 to = o->position + away * 12.0f;
				weatherHold(o, HOLD_FIRE, now + 12.0f, &to);
				weatherFireRetreats++;
			}
		}
		// the AI's burning buildings (lightning, wildfires, enemies): put out by up to 3 peasants
		// each, the nearest within 150 m; a garrison let out once it is half burnt down
		std::vector<ServerGameObject*> burning;
		if (g_w.burning >= 0)
			for (const auto& ref : srv->aliases[g_w.burning])
				if (ServerGameObject* b = ref.getFrom<Server>(); b && !b->deleted && b->getPlayer() == gameObj)
					burning.push_back(b);
		std::sort(burning.begin(), burning.end(), [](ServerGameObject* a, ServerGameObject* b) { return a->id < b->id; });
		if (!burning.empty() && g_w.extinguishOrder >= 0) {
			std::vector<ServerGameObject*> peasants;
			std::map<ServerGameObject*, int> working;
			for (ServerGameObject* u : units) {
				if (g_w.peasant < 0 || u->getItem(g_w.peasant) <= 0.0f || !u->isInteractable())
					continue;
				if (OrderIs(u, g_w.extinguishOrder) || OrderIs(u, g_w.repairOrder)) {
					if (ServerGameObject* tg = OrderTarget(u))
						working[tg]++;
				}
				else if (!weatherHeld.count(u->id))
					peasants.push_back(u);
			}
			for (ServerGameObject* b : burning) {
				int have = working.count(b) ? working[b] : 0;
				std::sort(peasants.begin(), peasants.end(), [&](ServerGameObject* x, ServerGameObject* y) {
					const float dx = (x->position - b->position).sqlen2xz(), dy = (y->position - b->position).sqlen2xz();
					return dx != dy ? dx < dy : x->id < y->id; });
				while (have < 3 && !peasants.empty() && (peasants.front()->position - b->position).sqlen2xz() < 150.0f * 150.0f) {
					weatherHold(peasants.front(), HOLD_EXTINGUISH, now + 60.0f, nullptr, b, g_w.extinguishOrder);
					peasants.erase(peasants.begin());
					have++;
					weatherExtinguish++;
				}
				// the garrison out
				if (g_w.ungarrison >= 0 && g_w.si >= 0 && g_w.siCap >= 0 && b->getItem(g_w.siCap) > 0.0f && b->getItem(g_w.si) < 0.5f * b->getItem(g_w.siCap)) {
					bool garrisoned = false;
					for (const auto& [type, children] : b->children)
						for (CommonGameObject* c : children)
							garrisoned |= c->blueprint->bpClass == Tags::GAMEOBJCLASS_CHARACTER && !((ServerGameObject*)c)->deleted;
					auto last = weatherUngarrisoned.find(b->id);
					if (garrisoned && (last == weatherUngarrisoned.end() || now - last->second > 20.0f)) {
						weatherUngarrisoned[b->id] = now;
						SrvScriptContext ctx{ srv, b };
						gs->orderAssignments[g_w.ungarrison].assignTo(b, &ctx, gameObj);
						weatherUngarrisons++;
					}
				}
			}
		}
		for (auto it = weatherHeld.begin(); it != weatherHeld.end();) {
			auto next = std::next(it);
			if (it->second.why == HOLD_FIRE && now > it->second.until)
				weatherRelease(it->first);
			else if (it->second.why == HOLD_EXTINGUISH) {
				ServerGameObject* u = srv->findObject(it->first);
				if (!u || now > it->second.until || !(OrderIs(u, g_w.extinguishOrder) || OrderIs(u, g_w.repairOrder)))
					weatherRelease(it->first);
			}
			it = next;
		}
		for (auto it = weatherUngarrisoned.begin(); it != weatherUngarrisoned.end();)
			if (now - it->second > 60.0f)
				it = weatherUngarrisoned.erase(it);
			else
				++it;
	}
}

bool AIController::weatherHoldsWave(const Vector3& base, ServerGameObject* target, bool striking)
{
	if (!weatherActive() || !target)
		return false;
	const ServerEnvironment& env = Server::instance->environment;
	const float now = Server::instance->timeManager.currentTime;
	const float dist = (target->position - base).len2xz();
	const float walk = dist / WALK_SPEED;
	auto hold = [&](int& counter) {
		if (waveHeldSince < 0.0f) {
			waveHeldSince = now;
			counter++;
		}
		return true;
	};
	if (env.settings.seasons) {
		// harmful cold or heat: only near targets (whose walk out costs less than 60 exposure)
		const float cold = env.coldFeel(), heat = env.heatFeel();
		const float coldRate = cold < 2.0f ? (2.0f - cold) / 6.0f * (1.0f + 0.5f * env.state.snowCover) * 1.5f : 0.0f;
		const float heatRate = heat > 30.0f ? (heat - 30.0f) / 4.0f * 1.5f : 0.0f;
		if ((cold < -3.0f && coldRate * walk > 60.0f) || (heat > 33.0f && heatRate * walk > 60.0f))
			return hold(wavesHeldCold);
		// over the ice near the thaw: the straight way crosses deep water
		if (env.state.ice > 0.05f && env.state.temperatureC > -2.0f) {
			const int steps = (int)(dist / 5.0f);
			for (int i = 1; i < steps; i++)
				if (env.onDeepWater(base + (target->position - base) * ((float)i / steps)))
					return hold(wavesHeldIce);
		}
	}
	if (env.settings.dayNight && !striking) {
		if (Daylight(env, now) < NIGHT)
			return false;
		// by day: wait for the dark, so that the wave arrives after nightfall (not past maxWait)
		const float day = env.settings.dayLengthS();
		const float maxWait = std::min(0.4f * day, 360.0f);
		if (waveHeldSince >= 0.0f && now - waveHeldSince > maxWait)
			return false;
		for (float t = 10.0f; t < day; t += 10.0f)
			if (Daylight(env, now + t) < NIGHT) {
				if (t > walk && t - walk < maxWait)
					return hold(wavesHeldNight);
				break;
			}
	}
	return false;
}

void AIController::weatherWaveSent()
{
	waveHeldSince = -1.0f;
	if (weatherActive() && Server::instance->environment.settings.dayNight && Daylight(Server::instance->environment, Server::instance->timeManager.currentTime) < NIGHT)
		nightWaves++;
}

size_t AIController::weatherKeep(size_t available, size_t keep)
{
	if (!weatherActive() || !Server::instance->environment.settings.dayNight)
		return keep;
	const ServerEnvironment& env = Server::instance->environment;
	if (Daylight(env, Server::instance->timeManager.currentTime) >= NIGHT)
		return keep;
	return std::min(available, std::max({ keep, (size_t)10, (size_t)(available * 0.35f) }));
}

bool AIController::weatherAvoidsSite(const Vector3& pos)
{
	if (!weatherActive() || !Server::instance->environment.settings.seasons)
		return false;
	// the flood there is or coming (the water heading up), half a metre more for safety: the
	// highest flood a harsh season can bring (2.4 m) covered most of Tribes Unite's lakeside land,
	// and no foundation was laid all game
	const ServerEnvironment& env = Server::instance->environment;
	const float rise = std::max(env.state.waterRise, env.waterTarget());
	return rise > 0.1f && env.floodDepthAt(pos, rise + 0.5f) > 0.1f;
}

std::string AIController::weatherCensus() const
{
	char buf[256];
	snprintf(buf, sizeof(buf), "sheltered %d, off the ice %d, out of floods %d, away from fires %d, extinguishing %d, garrisons out %d; waves held: cold/heat %d, ice %d, for the night %d; night waves %d; held now %zu",
		weatherSheltered, weatherIceRetreats, weatherFloodRetreats, weatherFireRetreats, weatherExtinguish, weatherUngarrisons, wavesHeldCold, wavesHeldIce, wavesHeldNight, nightWaves, weatherHeld.size());
	return buf;
}
