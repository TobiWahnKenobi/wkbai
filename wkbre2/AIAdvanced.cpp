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
	ServerGameObject* defensive = nullptr;
	for (ServerGameObject* a : armies)
		if (a->blueprint->name == "Kingmaker Defensive Army")
			defensive = a;

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
	if (defensive) {
		std::vector<ServerGameObject*> home;
		CollectUnits(defensive, home);
		for (ServerGameObject* u : home)
			if (IsSoldier(u) && !defenders.count(u->id))
				available.push_back(u);
	}
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
