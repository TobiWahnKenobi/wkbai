// wkbai - the advanced AI: what units are worth in a fight, defence of every settlement from the
// start, and attacks chosen by strength (the aggressive AI's operations; not in WKB.exe)
//
// The Kingmaker scripts plan the economy and the armies' types; the aggressive AI (AIController)
// sends attack waves of a fixed size at the nearest palace. The advanced AI ("aiAdvanced") adds:
// - Unit power from each unit's own items (its damage, hit points, armour, range: what the game
//   set's equations made of it), and a force's strength by Lanchester's square law (the sum of
//   the units' power, squared), weighed by how each unit's class fares against the other side's
//   (spears against horse, archers against foot, horse against archers).
// - Defence of every settlement and of the peasants out working, from the start: enemy soldiers
//   near one are met by enough of the defensive army's soldiers to outmatch them (1.5 times their
//   strength), the nearest first; peasants with no help coming walk home. The scripts' own
//   defence of the palace (250 m) is left to them.
// - Attacks chosen by strength: a wave goes when it outmatches what guards its target (1.4
//   times); the target is the enemy settlement worth most for what guards it (a village with few
//   soldiers before a walled palace), nearer ones first; a wave that is losing (under half the
//   strength around it) comes home instead of dying, and one whose target fell goes on to the
//   next building there.
#include "AIController.h"
#include "server.h"
#include "gameset/ScriptContext.h"
#include "gameset/gameset.h"
#include "gameset/finder.h"
#include "Order.h"
#include "NNSearch.h"
#include "tags.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <sol/sol.hpp>
#include <filesystem>
#include <fstream>
#include "file.h"
#include "util/SimRandom.h"
#include "util/StriCompare.h"

namespace {
	struct Items {
		const GameSet* of = nullptr;
		int hp = -1, hpCap = -1, h2h = -1, missile = -1, range = -1, armour = -1, military = -1, peasant = -1, village = -1, defensiveMode = -1;
		int enemyStatus = -1, attackOrder = -1, moveHome = -1;
		void update(const GameSet* gs) {
			if (of == gs)
				return;
			of = gs;
			hp = gs->items.names.getIndex("Hit Points");
			hpCap = gs->items.names.getIndex("Hit Point Capacity");
			h2h = gs->items.names.getIndex("H2H Damage");
			missile = gs->items.names.getIndex("Missile Damage");
			range = gs->items.names.getIndex("Maximum Missile Range");
			armour = gs->items.names.getIndex("Front Armour");
			military = gs->items.names.getIndex("I'm A Military Unit");
			peasant = gs->items.names.getIndex("I Am A Peasant");
			village = gs->items.names.getIndex("I Am A Village");
			defensiveMode = gs->items.names.getIndex("Defensive Mode");
			enemyStatus = gs->diplomaticStatuses.names.getIndex("Enemy");
			attackOrder = gs->orders.names.getIndex("Attack");
			moveHome = gs->orderAssignments.names.getIndex("Move to Manor");
		}
	} g_items;

	float Item(const ServerGameObject* o, int item) { return item >= 0 ? o->getItem(item) : 0.0f; }

	// how a class (1 heavy infantry, 2 light infantry: archers, 3 heavy cavalry, 4 light cavalry)
	// fares against another, as a factor on its power
	const float CLASS_FACTOR[5][5] = {
		{ 1.0f, 1.0f, 1.0f, 1.0f, 1.0f },
		{ 1.0f, 1.0f, 1.1f, 1.3f, 1.2f }, // heavy infantry: spears and pikes hold horse
		{ 1.0f, 1.2f, 1.0f, 0.7f, 0.8f }, // archers: good against foot, ridden down by horse
		{ 1.0f, 1.0f, 1.4f, 1.0f, 1.1f }, // heavy cavalry: rides down archers
		{ 1.0f, 0.7f, 1.3f, 0.8f, 1.0f }, // light cavalry: harasses archers, loses to spears
	};

	int ClassOf(const ServerGameObject* o)
	{
		const int c = (int)Item(o, g_items.military);
		return c >= 1 && c <= 4 ? c : 0;
	}

	bool IsSoldier(const ServerGameObject* o)
	{
		return o->blueprint->bpClass == Tags::GAMEOBJCLASS_CHARACTER && ClassOf(o) > 0;
	}

	bool IsPeasant(const ServerGameObject* o)
	{
		return o->blueprint->bpClass == Tags::GAMEOBJCLASS_CHARACTER && Item(o, g_items.peasant) > 0.0f;
	}

	bool IsEnemy(ServerGameObject* me, ServerGameObject* other)
	{
		ServerGameObject* owner = other->getPlayer();
		if (!owner || owner == me || owner->blueprint->bpClass != Tags::GAMEOBJCLASS_PLAYER)
			return false;
		return g_items.enemyStatus >= 0 && Server::instance->getDiplomaticStatus(owner, me) == g_items.enemyStatus;
	}

	// a unit's (or a tower's) power: the square root of what it deals and what it takes, its
	// damage per hit (melee, or missiles with their range counting) times its hit points now,
	// armour adding a tenth per point
	float UnitPower(const ServerGameObject* o)
	{
		const float hit = std::max(Item(o, g_items.h2h), Item(o, g_items.missile) * (1.0f + std::min(Item(o, g_items.range), 80.0f) / 80.0f));
		const float hp = std::max(Item(o, g_items.hp), 0.0f);
		if (hit <= 0.0f || hp <= 0.0f)
			return 0.0f;
		return std::sqrt(hit * hp * (1.0f + 0.1f * std::max(Item(o, g_items.armour), 0.0f)));
	}

	// a force's strength against another's, by Lanchester's square law: the units' power (each
	// weighed by how its class fares against the other side's classes), summed, squared
	float Strength(const std::vector<ServerGameObject*>& units, const std::vector<ServerGameObject*>& against)
	{
		float classes[5] = {};
		float total = 0.0f;
		for (ServerGameObject* e : against) {
			const float p = UnitPower(e);
			classes[ClassOf(e)] += p;
			total += p;
		}
		float sum = 0.0f;
		for (ServerGameObject* u : units) {
			const int c = ClassOf(u);
			float factor = 1.0f;
			if (total > 0.0f) {
				factor = 0.0f;
				for (int k = 0; k < 5; k++)
					factor += classes[k] / total * CLASS_FACTOR[c][k];
			}
			sum += UnitPower(u) * factor;
		}
		return sum * sum;
	}

	// the enemy soldiers and towers (buildings that shoot) near a place
	std::vector<ServerGameObject*> EnemiesNear(ServerGameObject* me, const Vector3& centre, float radius, bool towers)
	{
		std::vector<ServerGameObject*> found;
		NNSearch search;
		search.start(Server::instance, centre, radius);
		while (ServerGameObject* o = (ServerGameObject*)search.next()) {
			if (o->deleted || (o->position - centre).sqlen2xz() > radius * radius)
				continue;
			const bool soldier = IsSoldier(o);
			const bool tower = towers && o->blueprint->bpClass == Tags::GAMEOBJCLASS_BUILDING && Item(o, g_items.missile) > 0.0f;
			if ((soldier || tower) && IsEnemy(me, o))
				found.push_back(o);
		}
		return found;
	}

	// the characters under an object (through formations), and the armies under the player
	void CollectUnits(ServerGameObject* o, std::vector<ServerGameObject*>& units)
	{
		for (const auto& [type, children] : o->children)
			for (CommonGameObject* c : children) {
				auto* u = (ServerGameObject*)c;
				if (u->deleted)
					continue;
				if (u->blueprint->bpClass == Tags::GAMEOBJCLASS_CHARACTER)
					units.push_back(u);
				else if (u->blueprint->bpClass == Tags::GAMEOBJCLASS_FORMATION)
					CollectUnits(u, units);
			}
	}

	void CollectArmies(ServerGameObject* player, std::vector<ServerGameObject*>& armies)
	{
		std::vector<ServerGameObject*> todo{ player };
		while (!todo.empty()) {
			ServerGameObject* o = todo.back();
			todo.pop_back();
			for (const auto& [type, children] : o->children)
				for (CommonGameObject* c : children) {
					auto* a = (ServerGameObject*)c;
					if (a->deleted || a->blueprint->bpClass != Tags::GAMEOBJCLASS_ARMY)
						continue;
					armies.push_back(a);
					todo.push_back(a);
				}
		}
	}

	// a player's settlements: its towns (each the centre of its buildings), with their buildings
	void CollectTowns(ServerGameObject* o, std::vector<ServerGameObject*>& towns)
	{
		for (const auto& [type, children] : o->children)
			for (CommonGameObject* c : children) {
				auto* t = (ServerGameObject*)c;
				if (t->deleted)
					continue;
				if (t->blueprint->bpClass == Tags::GAMEOBJCLASS_TOWN)
					towns.push_back(t);
				else if (t->blueprint->bpClass == Tags::GAMEOBJCLASS_CITY)
					CollectTowns(t, towns);
			}
	}

	std::vector<ServerGameObject*> BuildingsOf(ServerGameObject* town)
	{
		std::vector<ServerGameObject*> list;
		for (const auto& [type, children] : town->children)
			for (CommonGameObject* c : children)
				if (!((ServerGameObject*)c)->deleted && c->blueprint->bpClass == Tags::GAMEOBJCLASS_BUILDING)
					list.push_back((ServerGameObject*)c);
		return list;
	}

	// Where a player's soldiers wait at home: its Kingmaker Defensive Army, or (AI Generals whose
	// scripts make none: the Duke of the Dead's soldiers stood straight under it) its Kingmaker
	// Co-ordinator
	ServerGameObject* HomeArmy(ServerGameObject* player)
	{
		std::vector<ServerGameObject*> armies;
		CollectArmies(player, armies);
		ServerGameObject* coordinator = nullptr;
		for (ServerGameObject* a : armies) {
			if (a->blueprint->name == "Kingmaker Defensive Army")
				return a;
			if (a->blueprint->name == "Kingmaker Co-ordinator")
				coordinator = a;
		}
		return coordinator;
	}

	// the soldiers at home: those in the defensive army, and those straight under the co-ordinator
	std::vector<ServerGameObject*> HomeSoldiers(ServerGameObject* player)
	{
		std::vector<ServerGameObject*> list, armies;
		CollectArmies(player, armies);
		for (ServerGameObject* a : armies) {
			if (a->blueprint->name == "Kingmaker Defensive Army") {
				std::vector<ServerGameObject*> units;
				CollectUnits(a, units);
				for (ServerGameObject* u : units)
					if (IsSoldier(u))
						list.push_back(u);
			}
			else if (a->blueprint->name == "Kingmaker Co-ordinator")
				for (const auto& [type, children] : a->children)
					for (CommonGameObject* c : children)
						if (!((ServerGameObject*)c)->deleted && IsSoldier((ServerGameObject*)c))
							list.push_back((ServerGameObject*)c);
		}
		return list;
	}

	ServerGameObject* NearestEnemy(const std::vector<ServerGameObject*>& enemies, const Vector3& from)
	{
		ServerGameObject* best = nullptr;
		float bestD = 0.0f;
		for (ServerGameObject* e : enemies) {
			const float d = (e->position - from).sqlen2xz();
			if (!best || d < bestD) {
				best = e;
				bestD = d;
			}
		}
		return best;
	}
}

bool AIController::advanced()
{
	Server* srv = Server::instance;
	if (!srv->aggressiveAI || srv->aiAdvanced == 0)
		return false;
	if (advancedState < 0) {
		// 1: all AIs; 2 and 3: every other one (by player id), for AI-against-AI comparisons
		int rank = 0;
		if (srv->getLevel())
			for (const auto& [type, children] : srv->getLevel()->children)
				if (type.bpClass() == Tags::GAMEOBJCLASS_PLAYER)
					for (CommonGameObject* p : children)
						if (p->id < gameObj->id)
							rank++;
		advancedState = srv->aiAdvanced == 1 || (srv->aiAdvanced == 2 && rank % 2 == 1) || (srv->aiAdvanced == 3 && rank % 2 == 0) ? 1 : 0;
	}
	return advancedState == 1;
}

float AIController::forceStrength(const std::vector<ServerGameObject*>& units, const std::vector<ServerGameObject*>& against)
{
	g_items.update(Server::instance->gameSet.get());
	return Strength(units, against);
}

void AIController::advancedUpdate()
{
	Server* srv = Server::instance;
	const GameSet* gs = srv->gameSet.get();
	g_items.update(gs);
	const float now = srv->timeManager.currentTime;
	if (now < nextAdvancedCheck)
		return;
	nextAdvancedCheck = now + 3.0f;
	if (g_items.attackOrder < 0)
		return;

	// test runs: WKB_TRACE_UNITS prints each unit type's items once
	if (getenv("WKB_TRACE_UNITS")) {
		static std::set<const GameObjBlueprint*> shown;
		std::vector<ServerGameObject*> all;
		CollectUnits(gameObj, all);
		std::vector<ServerGameObject*> armies;
		CollectArmies(gameObj, armies);
		for (ServerGameObject* a : armies)
			CollectUnits(a, all);
		for (ServerGameObject* u : all)
			if (IsSoldier(u) && shown.insert(u->blueprint).second)
				printf("UNIT %s: class %d, hit points %.0f/%.0f, h2h %.1f, missile %.1f, range %.0f, armour %.1f, power %.1f\n", u->blueprint->name.c_str(), ClassOf(u),
					Item(u, g_items.hp), Item(u, g_items.hpCap), Item(u, g_items.h2h), Item(u, g_items.missile), Item(u, g_items.range), Item(u, g_items.armour), UnitPower(u));
	}

	SrvScriptContext ctx{ srv, gameObj };
	const int palaceFinder = gs->objectFinderDefinitions.names.getIndex("Palace Building Of Player");
	ServerGameObject* palace = palaceFinder >= 0 ? (ServerGameObject*)gs->objectFinderDefinitions[palaceFinder]->getFirst(&ctx) : nullptr;
	std::vector<ServerGameObject*> armies;
	CollectArmies(gameObj, armies);
	ServerGameObject* defensive = HomeArmy(gameObj);

	// ---- Defence of every settlement and of the peasants out working ----
	// the threats: enemy soldiers near a town (other than the palace's, which the scripts defend)
	// or near a peasant, grouped by place
	std::vector<ServerGameObject*> towns;
	CollectTowns(gameObj, towns);
	std::vector<ServerGameObject*> peasants;
	for (const auto& [type, children] : gameObj->children)
		for (CommonGameObject* c : children)
			if (!((ServerGameObject*)c)->deleted && IsPeasant((ServerGameObject*)c))
				peasants.push_back((ServerGameObject*)c);
	std::set<ServerGameObject*> seen;
	struct Threat { Vector3 at; std::vector<ServerGameObject*> enemies; };
	std::vector<Threat> threats;
	auto addThreat = [&](const Vector3& at, float radius) {
		if (palace && (at - palace->position).sqlen2xz() < 250.0f * 250.0f)
			return;
		std::vector<ServerGameObject*> enemies;
		for (ServerGameObject* e : EnemiesNear(gameObj, at, radius, false))
			if (seen.insert(e).second)
				enemies.push_back(e);
		if (!enemies.empty())
			threats.push_back({ at, std::move(enemies) });
	};
	for (ServerGameObject* t : towns)
		addThreat(t->position, 100.0f);
	for (size_t i = 0; i < peasants.size(); i += std::max<size_t>(1, peasants.size() / 40))
		addThreat(peasants[i]->position, 30.0f);

	// the defenders already out: released when their threat is gone (nobody within 60 m) or after
	// 90 s, and sent home
	for (auto it = defenders.begin(); it != defenders.end();) {
		ServerGameObject* u = srv->findObject(it->first);
		if (!u || u->deleted) {
			it = defenders.erase(it);
			continue;
		}
		const bool cleared = EnemiesNear(gameObj, it->second.at, 60.0f, false).empty();
		if (cleared || now - it->second.since > 90.0f) {
			if (g_items.moveHome >= 0)
				gs->orderAssignments[g_items.moveHome].assignTo(u, &ctx, gameObj);
			it = defenders.erase(it);
			continue;
		}
		++it;
	}

	std::vector<ServerGameObject*> available;
	for (ServerGameObject* u : HomeSoldiers(gameObj))
		if (!defenders.count(u->id))
			available.push_back(u);
	for (Threat& t : threats) {
		// already being met?
		std::vector<ServerGameObject*> meeting;
		for (const auto& [id, d] : defenders)
			if ((d.at - t.at).sqlen2xz() < 80.0f * 80.0f)
				if (ServerGameObject* u = srv->findObject(id))
					meeting.push_back(u);
		const float enemy = Strength(t.enemies, meeting);
		if (Strength(meeting, t.enemies) >= 1.5f * enemy)
			continue;
		std::sort(available.begin(), available.end(), [&](ServerGameObject* a, ServerGameObject* b) {
			return (a->position - t.at).sqlen2xz() < (b->position - t.at).sqlen2xz(); });
		std::vector<ServerGameObject*> sent = meeting;
		size_t used = 0;
		while (used < available.size() && Strength(sent, t.enemies) < 1.5f * Strength(t.enemies, sent))
			sent.push_back(available[used++]);
		if (Strength(sent, t.enemies) < 0.8f * Strength(t.enemies, sent)) {
			// no help that would win: the peasants there walk home
			for (ServerGameObject* p : peasants)
				if ((p->position - t.at).sqlen2xz() < 40.0f * 40.0f && g_items.moveHome >= 0) {
					gs->orderAssignments[g_items.moveHome].assignTo(p, &ctx, gameObj);
					peasantsFled++;
				}
			continue;
		}
		for (size_t i = 0; i < used; i++) {
			ServerGameObject* u = available[i];
			if (ServerGameObject* e = NearestEnemy(t.enemies, u->position))
				u->orderConfig.addOrder(&gs->orders[g_items.attackOrder], Tags::ORDERASSIGNMODE_FORGET_EVERYTHING_ELSE, e);
			defenders[u->id] = { t.at, now };
			defendersSent++;
		}
		available.erase(available.begin(), available.begin() + used);
		threatsMet++;
		if (getenv("WKB_TRACE_ADVANCED"))
			printf("ADVANCED player %u t=%.0f: %zu enemies at (%.0f,%.0f), %zu defenders sent\n", gameObj->id, now, t.enemies.size(), t.at.x, t.at.z, used);
	}

	// ---- Waves: home when losing, on to the next building when the target fell ----
	if (now >= nextAdvancedWaveCheck) {
		nextAdvancedWaveCheck = now + 6.0f;
		for (auto it = advancedWaves.begin(); it != advancedWaves.end();) {
			ServerGameObject* army = srv->findObject(it->army);
			std::vector<ServerGameObject*> units;
			if (army && !army->deleted)
				CollectUnits(army, units);
			if (units.empty()) {
				it = advancedWaves.erase(it);
				continue;
			}
			Vector3 centre;
			for (ServerGameObject* u : units)
				centre += u->position;
			centre = centre / (float)units.size();
			const std::vector<ServerGameObject*> around = EnemiesNear(gameObj, centre, 70.0f, true);
			// losing: under half the strength around it, having lost a fifth of its units
			if (!around.empty() && units.size() < it->size * 0.8f && Strength(units, around) < 0.5f * Strength(around, units)) {
				for (ServerGameObject* u : units) {
					if (defensive)
						u->setParent(defensive);
					if (g_items.moveHome >= 0)
						gs->orderAssignments[g_items.moveHome].assignTo(u, &ctx, gameObj);
				}
				wavesRecalled++;
				if (getenv("WKB_TRACE_ADVANCED"))
					printf("ADVANCED player %u t=%.0f: wave %u recalled, %zu of %zu units left against %zu\n", gameObj->id, now, army->id, units.size(), it->size, around.size());
				it = advancedWaves.erase(it);
				continue;
			}
			// the target fell: the nearest enemy building within 150 m of it, else home
			ServerGameObject* target = srv->findObject(it->target);
			if (!target || target->deleted) {
				ServerGameObject* next = nullptr;
				float nextD = 150.0f * 150.0f;
				NNSearch search;
				search.start(srv, it->targetAt, 150.0f);
				while (ServerGameObject* b = (ServerGameObject*)search.next()) {
					if (b->deleted || b->blueprint->bpClass != Tags::GAMEOBJCLASS_BUILDING || !IsEnemy(gameObj, b))
						continue;
					const float d = (b->position - it->targetAt).sqlen2xz();
					if (d < nextD) {
						next = b;
						nextD = d;
					}
				}
				if (next) {
					for (ServerGameObject* u : units)
						u->orderConfig.addOrder(&gs->orders[g_items.attackOrder], Tags::ORDERASSIGNMODE_FORGET_EVERYTHING_ELSE, next);
					it->target = next->id;
					it->targetAt = next->position;
					wavesRetargeted++;
				}
				else {
					for (ServerGameObject* u : units) {
						if (defensive)
							u->setParent(defensive);
						if (g_items.moveHome >= 0)
							gs->orderAssignments[g_items.moveHome].assignTo(u, &ctx, gameObj);
					}
					it = advancedWaves.erase(it);
					continue;
				}
			}
			++it;
		}
	}
}

ServerGameObject* AIController::advancedWaveTarget(const std::vector<ServerGameObject*>& wave, const Vector3& home, bool anyway)
{
	Server* srv = Server::instance;
	g_items.update(srv->gameSet.get());
	// every enemy settlement: worth (a palace 3, a village 2, others 1.5, the grudge's twice) for
	// what guards it (soldiers and towers within 150 m), nearer ones first
	ServerGameObject* best = nullptr;
	float bestScore = 0.0f;
	if (!srv->getLevel())
		return nullptr;
	for (const auto& [type, children] : srv->getLevel()->children) {
		if (type.bpClass() != Tags::GAMEOBJCLASS_PLAYER)
			continue;
		for (CommonGameObject* pc : children) {
			auto* player = (ServerGameObject*)pc;
			if (player == gameObj || player->deleted || g_items.enemyStatus < 0 || srv->getDiplomaticStatus(player, gameObj) != g_items.enemyStatus)
				continue;
			std::vector<ServerGameObject*> towns;
			CollectTowns(player, towns);
			for (ServerGameObject* town : towns) {
				std::vector<ServerGameObject*> buildings = BuildingsOf(town);
				if (buildings.empty())
					continue;
				bool isPalace = false, isVillage = false;
				for (ServerGameObject* b : buildings) {
					isPalace |= b->blueprint->name.find("Manor") != std::string::npos || b->blueprint->name.find("Palace") != std::string::npos;
					isVillage |= Item(b, g_items.village) > 0.0f;
				}
				float worth = isPalace ? 3.0f : isVillage ? 2.0f : 1.5f;
				if (player->id == grudgePlayer)
					worth *= 2.0f;
				const std::vector<ServerGameObject*> guards = EnemiesNear(gameObj, town->position, 150.0f, true);
				const float defence = Strength(guards, wave);
				if (!anyway && Strength(wave, guards) < 1.4f * defence)
					continue;
				const float distance = (town->position - home).len2xz();
				const float score = worth / ((defence + 1.0f) * (1.0f + distance / 600.0f));
				if (!best || score > bestScore) {
					// the building to attack: the town's nearest to home
					ServerGameObject* nearest = nullptr;
					for (ServerGameObject* b : buildings)
						if (!nearest || (b->position - home).sqlen2xz() < (nearest->position - home).sqlen2xz())
							nearest = b;
					best = nearest;
					bestScore = score;
				}
			}
		}
	}
	return best;
}

// ---- Lua AIs ----
// An AI player can be steered by a Lua script, LuaAI\<name>.lua (redata's, or a mod's: a mod's copy
// replaces redata's, and mods add their own), chosen per AI position in the lobby
// (Server::aiScripts). The Kingmaker scripts still plan its economy, buildings and armies' types;
// the Lua AI decides what the aggressive and advanced AI do in C++: when and where to attack, how
// to defend, when to come home. Each AI player has its own Lua state; its "think(ai)" runs every
// AI_INTERVAL seconds of game time (3 if the script doesn't set it). The state has Lua's base,
// math, string and table libraries without file access; math.random is the game's random
// numbers and pairs goes through the keys in order (numbers, then strings, then the rest),
// so that a game plays the same again from its recording. The "ai" table (LuaAIApi below):
// objects are their ids; a list of them is an array.
struct LuaAIState {
	sol::state lua;
	bool ok = false;
	std::string name;
};

namespace {
	sol::table IdList(sol::state_view lua, const std::vector<ServerGameObject*>& objects)
	{
		sol::table t = lua.create_table((int)objects.size(), 0);
		int i = 1;
		for (ServerGameObject* o : objects)
			t[i++] = o->id;
		return t;
	}

	std::vector<ServerGameObject*> Objects(sol::optional<sol::table> ids)
	{
		std::vector<ServerGameObject*> list;
		if (!ids)
			return list;
		for (size_t i = 1; i <= ids->size(); i++) {
			const sol::optional<uint32_t> id = (*ids)[i];
			if (id)
				if (ServerGameObject* o = Server::instance->findObject(*id); o && !o->deleted)
					list.push_back(o);
		}
		return list;
	}

	// pairs in key order: numbers ascending, then strings, then other keys as next gives them
	const char* g_sortedPairs = R"(
local next, type, tostring, sort = next, type, tostring, table.sort
function pairs(t)
	local keys, others = {}, {}
	for k in next, t do
		local kt = type(k)
		if kt == "number" or kt == "string" then keys[#keys + 1] = k else others[#others + 1] = k end
	end
	sort(keys, function(a, b)
		local ta, tb = type(a), type(b)
		if ta ~= tb then return ta == "number" end
		return a < b
	end)
	for _, k in next, others do keys[#keys + 1] = k end
	local i = 0
	return function()
		i = i + 1
		local k = keys[i]
		if k ~= nil then return k, t[k] end
	end, t, nil
end
)";
}

std::vector<std::string> ListLuaAIs()
{
	std::vector<std::string> names;
	std::unique_ptr<std::vector<std::string>> files(ListFiles("LuaAI"));
	if (files)
		for (const std::string& f : *files)
			if (f.size() > 4 && StrCICompare(f.substr(f.size() - 4), ".lua") == 0)
				names.push_back(f.substr(0, f.size() - 4));
	std::sort(names.begin(), names.end(), [](const std::string& a, const std::string& b) { return StrCICompare(a, b) < 0; });
	return names;
}

std::string LuaAIPath(const std::string& name)
{
	return "LuaAI\\" + name + ".lua";
}

bool AIController::luaActive()
{
	Server* srv = Server::instance;
	if (!srv->aggressiveAI)
		return false;
	auto it = srv->aiScripts.find(gameObj->id);
	if (it == srv->aiScripts.end() || it->second.empty())
		return false;
	if (!luaAI || luaAI->name != it->second)
		startLuaAI(it->second);
	return luaAI && luaAI->ok;
}

void AIController::startLuaAI(const std::string& name)
{
	luaAI = std::make_shared<LuaAIState>();
	luaAI->name = name;
	sol::state& lua = luaAI->lua;
	lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::string, sol::lib::table);
	for (const char* unsafe : { "dofile", "loadfile", "load", "require", "collectgarbage" })
		lua[unsafe] = sol::lua_nil;
	lua["math"]["random"] = [](sol::optional<int> a, sol::optional<int> b) -> double {
		if (!a)
			return (double)SimRand() / (double)SIM_RAND_MAX;
		const int lo = b ? *a : 1, hi = b ? *b : *a;
		return hi < lo ? lo : lo + (int)(SimRand() % (uint32_t)(hi - lo + 1));
	};
	lua["math"]["randomseed"] = []() {};
	lua.safe_script(g_sortedPairs);

	const uint32_t me = gameObj->id;
	auto player = [me]() { return Server::instance->findObject(me); };
	sol::table ai = lua.create_named_table("ai");
	ai["id"] = me;
	ai.set_function("time", []() { return Server::instance->timeManager.currentTime; });
	ai.set_function("log", [me](const std::string& text) {
		if (getenv("WKB_TRACE_LUAAI"))
			printf("LUAAI player %u t=%.0f: %s\n", me, Server::instance->timeManager.currentTime, text.c_str());
	});
	ai.set_function("alive", [](uint32_t id) { ServerGameObject* o = Server::instance->findObject(id); return o && !o->deleted; });
	ai.set_function("position", [](uint32_t id) -> std::tuple<float, float> {
		ServerGameObject* o = Server::instance->findObject(id);
		return o ? std::make_tuple(o->position.x, o->position.z) : std::make_tuple(0.0f, 0.0f);
	});
	ai.set_function("unit", [](sol::this_state ts, uint32_t id) -> sol::object {
		ServerGameObject* o = Server::instance->findObject(id);
		sol::state_view lua(ts);
		if (!o || o->deleted)
			return sol::lua_nil;
		g_items.update(Server::instance->gameSet.get());
		sol::table t = lua.create_table();
		t["id"] = o->id;
		t["type"] = o->blueprint->name;
		t["class"] = ClassOf(o);
		t["soldier"] = IsSoldier(o);
		t["peasant"] = IsPeasant(o);
		t["building"] = o->blueprint->bpClass == Tags::GAMEOBJCLASS_BUILDING;
		t["hp"] = Item(o, g_items.hp);
		t["hpMax"] = Item(o, g_items.hpCap);
		t["melee"] = Item(o, g_items.h2h);
		t["missile"] = Item(o, g_items.missile);
		t["range"] = Item(o, g_items.range);
		t["armour"] = Item(o, g_items.armour);
		t["power"] = UnitPower(o);
		t["x"] = o->position.x;
		t["z"] = o->position.z;
		ServerGameObject* owner = o->getPlayer();
		t["owner"] = owner ? owner->id : 0;
		return t;
	});
	ai.set_function("item", [](uint32_t id, const std::string& name) {
		ServerGameObject* o = Server::instance->findObject(id);
		const int item = Server::instance->gameSet->items.names.getIndex(name);
		return o && item >= 0 ? o->getItem(item) : 0.0f;
	});
	// an item of this AI's player or one of its objects set (the scripts read many: "Cannot Use
	// Militia", "Defensive Mode", ...)
	ai.set_function("setItem", [player](uint32_t id, const std::string& name, float value) {
		ServerGameObject* p = player();
		ServerGameObject* o = Server::instance->findObject(id);
		const int item = Server::instance->gameSet->items.names.getIndex(name);
		if (p && o && !o->deleted && item >= 0 && (o == p || o->getPlayer() == p))
			o->setItem(item, value);
	});
	ai.set_function("enemies", [player](sol::this_state ts) {
		std::vector<ServerGameObject*> list;
		g_items.update(Server::instance->gameSet.get());
		if (ServerGameObject* p = player(); p && Server::instance->getLevel())
			for (const auto& [type, children] : Server::instance->getLevel()->children)
				if (type.bpClass() == Tags::GAMEOBJCLASS_PLAYER)
					for (CommonGameObject* c : children)
						if (c != p && !((ServerGameObject*)c)->deleted && g_items.enemyStatus >= 0 && Server::instance->getDiplomaticStatus(c, p) == g_items.enemyStatus)
							list.push_back((ServerGameObject*)c);
		return IdList(ts, list);
	});
	ai.set_function("palace", [player]() -> sol::optional<uint32_t> {
		ServerGameObject* p = player();
		const GameSet* gs = Server::instance->gameSet.get();
		const int finder = gs->objectFinderDefinitions.names.getIndex("Palace Building Of Player");
		if (!p || finder < 0)
			return sol::nullopt;
		SrvScriptContext ctx{ Server::instance, p };
		ServerGameObject* palace = (ServerGameObject*)gs->objectFinderDefinitions[finder]->getFirst(&ctx);
		return palace ? sol::optional<uint32_t>(palace->id) : sol::nullopt;
	});
	ai.set_function("towns", [player](sol::this_state ts, sol::optional<uint32_t> of) {
		sol::state_view lua(ts);
		g_items.update(Server::instance->gameSet.get());
		ServerGameObject* p = of ? Server::instance->findObject(*of) : player();
		sol::table list = lua.create_table();
		if (!p)
			return list;
		std::vector<ServerGameObject*> towns;
		CollectTowns(p, towns);
		int i = 1;
		for (ServerGameObject* town : towns) {
			const std::vector<ServerGameObject*> buildings = BuildingsOf(town);
			if (buildings.empty())
				continue;
			bool palace = false, village = false;
			for (ServerGameObject* b : buildings) {
				palace |= b->blueprint->name.find("Manor") != std::string::npos || b->blueprint->name.find("Palace") != std::string::npos;
				village |= Item(b, g_items.village) > 0.0f;
			}
			sol::table t = lua.create_table();
			t["id"] = town->id;
			t["x"] = town->position.x;
			t["z"] = town->position.z;
			t["palace"] = palace;
			t["village"] = village;
			t["buildings"] = IdList(ts, buildings);
			list[i++] = t;
		}
		return list;
	});
	ai.set_function("enemiesNear", [player](sol::this_state ts, float x, float z, float radius, sol::optional<bool> towers) {
		g_items.update(Server::instance->gameSet.get());
		ServerGameObject* p = player();
		return IdList(ts, p ? EnemiesNear(p, Vector3(x, 0, z), radius, towers.value_or(false)) : std::vector<ServerGameObject*>());
	});
	ai.set_function("buildingsNear", [player](sol::this_state ts, float x, float z, float radius) {
		std::vector<ServerGameObject*> list;
		ServerGameObject* p = player();
		g_items.update(Server::instance->gameSet.get());
		NNSearch search;
		search.start(Server::instance, Vector3(x, 0, z), radius);
		while (ServerGameObject* b = (ServerGameObject*)search.next())
			if (p && !b->deleted && b->blueprint->bpClass == Tags::GAMEOBJCLASS_BUILDING && IsEnemy(p, b)
				&& (b->position - Vector3(x, 0, z)).sqlen2xz() <= radius * radius)
				list.push_back(b);
		std::sort(list.begin(), list.end(), [x, z](ServerGameObject* a, ServerGameObject* b) {
			return (a->position - Vector3(x, 0, z)).sqlen2xz() < (b->position - Vector3(x, 0, z)).sqlen2xz(); });
		return IdList(ts, list);
	});
	ai.set_function("peasants", [player](sol::this_state ts) {
		std::vector<ServerGameObject*> list;
		g_items.update(Server::instance->gameSet.get());
		if (ServerGameObject* p = player())
			for (const auto& [type, children] : p->children)
				for (CommonGameObject* c : children)
					if (!((ServerGameObject*)c)->deleted && IsPeasant((ServerGameObject*)c))
						list.push_back((ServerGameObject*)c);
		return IdList(ts, list);
	});
	// the soldiers at home (the defensive army), and the attacking armies with their units
	ai.set_function("home", [player](sol::this_state ts) {
		g_items.update(Server::instance->gameSet.get());
		ServerGameObject* p = player();
		return IdList(ts, p ? HomeSoldiers(p) : std::vector<ServerGameObject*>());
	});
	ai.set_function("armies", [player](sol::this_state ts) {
		sol::state_view lua(ts);
		std::vector<ServerGameObject*> armies;
		if (ServerGameObject* p = player())
			CollectArmies(p, armies);
		sol::table list = lua.create_table();
		int i = 1;
		for (ServerGameObject* a : armies)
			if (a->blueprint->name == "Kingmaker Attacking Army") {
				std::vector<ServerGameObject*> units;
				CollectUnits(a, units);
				sol::table t = lua.create_table();
				t["id"] = a->id;
				t["units"] = IdList(ts, units);
				list[i++] = t;
			}
		return list;
	});
	ai.set_function("strength", [](sol::table units, sol::table against) {
		g_items.update(Server::instance->gameSet.get());
		return Strength(Objects(units), Objects(against));
	});
	ai.set_function("defensiveMode", [player]() {
		g_items.update(Server::instance->gameSet.get());
		ServerGameObject* p = player();
		return p && g_items.defensiveMode >= 0 && p->getItem(g_items.defensiveMode) > 0.0f;
	});
	// orders: each unit attacks the target; home (into the defensive army, walking to the manor);
	// a wave (a new attacking army in formations, see AIController::launchWave): its army's id
	ai.set_function("attack", [](sol::table units, uint32_t target) {
		const GameSet* gs = Server::instance->gameSet.get();
		g_items.update(gs);
		ServerGameObject* t = Server::instance->findObject(target);
		if (!t || t->deleted || g_items.attackOrder < 0)
			return;
		for (ServerGameObject* u : Objects(units))
			u->orderConfig.addOrder(&gs->orders[g_items.attackOrder], Tags::ORDERASSIGNMODE_FORGET_EVERYTHING_ELSE, t);
	});
	ai.set_function("sendHome", [player](sol::table units) {
		const GameSet* gs = Server::instance->gameSet.get();
		g_items.update(gs);
		ServerGameObject* p = player();
		if (!p)
			return;
		ServerGameObject* defensive = HomeArmy(p);
		SrvScriptContext ctx{ Server::instance, p };
		for (ServerGameObject* u : Objects(units)) {
			if (defensive && IsSoldier(u))
				u->setParent(defensive);
			if (g_items.moveHome >= 0)
				gs->orderAssignments[g_items.moveHome].assignTo(u, &ctx, p);
		}
	});
	// units held at a place: moved into an army of the AI's own (the given one if it still
	// exists, else a new Kingmaker Attacking Army), out of the scripts' hands, walking there;
	// returns the army's id
	ai.set_function("hold", [player](sol::table units, float x, float z, sol::optional<uint32_t> armyId) -> sol::optional<uint32_t> {
		ServerGameObject* p = player();
		const GameSet* gs = Server::instance->gameSet.get();
		const GameObjBlueprint* armyBp = gs->findBlueprint(Tags::GAMEOBJCLASS_ARMY, "Kingmaker Attacking Army");
		const int moveOrder = gs->orders.names.getIndex("Move To");
		if (!p || !armyBp || moveOrder < 0)
			return sol::nullopt;
		ServerGameObject* army = armyId ? Server::instance->findObject(*armyId) : nullptr;
		if (!army || army->deleted || army->getPlayer() != p || army->blueprint->bpClass != Tags::GAMEOBJCLASS_ARMY)
			army = Server::instance->spawnObject(armyBp, p, Vector3(x, 0, z), Vector3(0, 0, 0));
		if (!army)
			return sol::nullopt;
		for (ServerGameObject* u : Objects(units)) {
			if (u->getParent() != army)
				u->setParent(army);
			u->orderConfig.addOrder(&gs->orders[moveOrder], Tags::ORDERASSIGNMODE_FORGET_EVERYTHING_ELSE, nullptr, Vector3(x, 0, z));
		}
		return army->id;
	});
	ai.set_function("launch", [player](sol::table units, uint32_t target) -> sol::optional<uint32_t> {
		ServerGameObject* p = player();
		ServerGameObject* t = Server::instance->findObject(target);
		if (!p || !t || t->deleted)
			return sol::nullopt;
		ServerGameObject* defensive = HomeArmy(p);
		ServerGameObject* army = p->aiController.launchWave(Objects(units), t, defensive ? defensive : p);
		if (!army)
			return sol::nullopt;
		p->aiController.waves++;
		p->aiController.waveUnits += (int)units.size();
		return army->id;
	});
	// training: this AI's idle buildings and the fighters each can train now (its "Spawn <type>"
	// command possible and waited for, and paid for), and an order to train one there; the soldier
	// is commissioned by the defensive army, as the scripts' own are
	ai.set_function("trainers", [player](sol::this_state ts) {
		sol::state_view lua(ts);
		sol::table list = lua.create_table();
		ServerGameObject* p = player();
		if (!p)
			return list;
		const GameSet* gs = Server::instance->gameSet.get();
		g_items.update(gs);
		std::vector<ServerGameObject*> buildings, todo{ p };
		while (!todo.empty()) {
			ServerGameObject* o = todo.back();
			todo.pop_back();
			for (const auto& [type, children] : o->children)
				for (CommonGameObject* c : children) {
					auto* b = (ServerGameObject*)c;
					if (b->deleted)
						continue;
					if (b->blueprint->bpClass == Tags::GAMEOBJCLASS_BUILDING)
						buildings.push_back(b);
					else if (b->blueprint->bpClass == Tags::GAMEOBJCLASS_PLAYER || b->blueprint->bpClass == Tags::GAMEOBJCLASS_ARMY)
						todo.push_back(b);
				}
		}
		std::sort(buildings.begin(), buildings.end(), [](ServerGameObject* a, ServerGameObject* b) { return a->id < b->id; });
		int i = 1;
		for (ServerGameObject* b : buildings) {
			if (b->orderConfig.getCurrentOrder())
				continue;
			sol::table types = lua.create_table();
			int n = 1;
			for (const Command* cmd : b->blueprint->offeredCommands) {
				const std::string& name = gs->commands.getString(cmd);
				if (name.rfind("Spawn ", 0) != 0)
					continue;
				const GameObjBlueprint* type = gs->findBlueprint(Tags::GAMEOBJCLASS_CHARACTER, name.substr(6));
				if (!type || gs->orders.names.getIndex(name) < 0)
					continue;
				SrvScriptContext cctx(Server::instance, b);
				auto _s = cctx.selectedObject.change(b);
				auto holds = [&](GSCondition* cond) { return cond->test->booleval(&cctx); };
				if (!std::all_of(cmd->conditionsImpossible.begin(), cmd->conditionsImpossible.end(), holds)
					|| !std::all_of(cmd->conditionsWait.begin(), cmd->conditionsWait.end(), holds)
					|| !cmd->canBeExecuted(b) || !p->canAffordObject(type))
					continue;
				// fighters only: the soldier's item (not peasants, carts, priests, ...)
				if (g_items.military < 0 || !type->startItemValues.count(g_items.military) || type->startItemValues.at(g_items.military) <= 0.0f)
					continue;
				types[n++] = type->name;
			}
			if (n > 1) {
				sol::table t = lua.create_table();
				t["id"] = b->id;
				t["type"] = b->blueprint->name;
				t["types"] = types;
				list[i++] = t;
			}
		}
		return list;
	});
	ai.set_function("train", [player](uint32_t building, const std::string& typeName) {
		ServerGameObject* p = player();
		ServerGameObject* b = Server::instance->findObject(building);
		const GameSet* gs = Server::instance->gameSet.get();
		if (!p || !b || b->deleted || b->getPlayer() != p || b->orderConfig.getCurrentOrder())
			return false;
		const std::string spawnName = "Spawn " + typeName;
		const int ordid = gs->orders.names.getIndex(spawnName);
		const GameObjBlueprint* type = gs->findBlueprint(Tags::GAMEOBJCLASS_CHARACTER, typeName);
		if (ordid < 0 || !type || !p->canAffordObject(type))
			return false;
		Order* order = b->orderConfig.addOrder(&gs->orders[ordid], Tags::ORDERASSIGNMODE_DO_FIRST, nullptr, { -1, -1, -1 }, false);
		if (!order || order->tasks.empty())
			return false;
		auto* task = (SpawnTask*)(order->tasks[0].get());
		task->setSpawnBlueprint(type);
		if (ServerGameObject* home = HomeArmy(p))
			task->aiCommissioner = home;
		p->aiController.trained++;
		return true;
	});

	// a replay: the copy its recording kept
	std::string code, path = LuaAIPath(name);
	const std::filesystem::path recorded = Server::instance->replayScripts / PathFromUtf8(name + ".lua");
	std::error_code ec;
	if (Server::instance->replay && std::filesystem::is_regular_file(recorded, ec)) {
		std::ifstream in(recorded, std::ios::binary);
		code.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
		path = PathToUtf8(recorded);
	}
	else {
		if (!FileExists(path.c_str())) {
			printf("Lua AI %s for player %u: %s not found\n", name.c_str(), me, path.c_str());
			return;
		}
		char* text = nullptr;
		int size = 0;
		LoadFile(path.c_str(), &text, &size);
		code.assign(text, size);
		free(text);
	}
	sol::protected_function_result r = lua.safe_script(code, sol::script_pass_on_error, "@" + path);
	if (!r.valid()) {
		sol::error err = r;
		printf("Lua AI %s for player %u: %s\n", name.c_str(), me, err.what());
		return;
	}
	luaAI->ok = true;
	nextLuaThink = 0.0f;
	printf("Lua AI %s for player %u\n", name.c_str(), me);
}

void AIController::luaThink()
{
	const float now = Server::instance->timeManager.currentTime;
	if (!luaAI || !luaAI->ok || now < nextLuaThink)
		return;
	sol::state& lua = luaAI->lua;
	const float interval = lua["AI_INTERVAL"].get_or(3.0f);
	nextLuaThink = now + std::clamp(interval, 0.5f, 60.0f);
	sol::protected_function think = lua["think"];
	if (!think.valid())
		return;
	sol::protected_function_result r = think(lua["ai"]);
	if (!r.valid()) {
		sol::error err = r;
		printf("Lua AI %s for player %u: %s\n", luaAI->name.c_str(), gameObj->id, err.what());
		luaErrors++;
		if (luaErrors >= 10) {
			printf("Lua AI %s for player %u: stopped after 10 errors\n", luaAI->name.c_str(), gameObj->id);
			luaAI->ok = false;
		}
	}
}

std::string AIController::luaCensus()
{
	if (!luaAI || !luaAI->ok)
		return std::string();
	sol::protected_function census = luaAI->lua["census"];
	std::string text;
	if (census.valid()) {
		sol::protected_function_result r = census();
		if (r.valid() && r.get_type() == sol::type::string)
			text = r.get<std::string>();
	}
	return luaAI->name + ": " + text;
}
