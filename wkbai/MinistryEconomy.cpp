// wkbai - the ministry AI's economy (the RE repo's docs/engine/ministry-ai.md, step 8): what
// Economy asks for, what Labour & Population does with the peasants, the Builders' Guild's
// repairs and Research. The Kingmaker scripts keep their build orders, commissions and work
// orders; the departments correct what they leave undone:
// - Labour & Population caps the peasants (a share of the population limit, smaller for an
//   aggressive General: room for soldiers; the scripts' spawns pass AISpawnAllowed, which refuses
//   peasants at the cap), puts idle peasants to work, and gives Economy the gold miners it asks
//   for, taken from the wood (the scripts' woodcutters), held out of the work orders' hands for at
//   least 2 minutes and let go when the gold is plenty.
// - Economy asks for gold miners while gold doesn't come in (in the stage-1 baseline Belisar the
//   Hammer had 200 gold from start to end of 30 minutes).
// - The Builders' Guild sends a peasant to repair a damaged building (under 70 %) no enemy is at.
// - Research spends a surplus on the "Research ..." commands the idle buildings offer: military
//   first for an aggressive General, the economy first for a careful one.

#include "MinistryDepartments.h"
#include "AIUtil.h"
#include "server.h"
#include "gameset/gameset.h"
#include "gameset/command.h"
#include "gameset/ScriptContext.h"
#include "gameset/condition.h"
#include "NNSearch.h"
#include <algorithm>
#include <cmath>

namespace Ministry {

namespace {
	struct Items {
		const GameSet* of = nullptr;
		int popLimit = -1, si = -1, siCap = -1;
		int mineGold = -1, chop = -1, repair = -1;
		void update(const GameSet* gs) {
			if (of == gs)
				return;
			of = gs;
			popLimit = gs->items.names.getIndex("Total Population Count");
			si = gs->items.names.getIndex("Structural Integrity");
			siCap = gs->items.names.getIndex("Structural Integrity Capacity");
			mineGold = gs->commands.names.getIndex("Mine Gold");
			chop = gs->commands.names.getIndex("Chop Tree");
			repair = gs->commands.names.getIndex("Repair");
		}
	} g_items;

	const char* CurrentOrder(Server& srv, ServerGameObject* u)
	{
		Order* o = u->orderConfig.getCurrentOrder();
		return o ? srv.gameSet->orders.getString(o->blueprint->bpid).c_str() : "";
	}

	// the nearest object within the radius the command can be given at for the peasant, among those
	// whose type's name has the word (gold: "Gold", trees: "Tree"); null if none
	ServerGameObject* FindWork(Server& srv, ServerGameObject* peasant, int command, const char* word, const Vector3& around, float radius)
	{
		if (command < 0)
			return nullptr;
		const Command& cmd = srv.gameSet->commands[command];
		std::vector<ServerGameObject*> found;
		NNSearch search;
		search.start(&srv, around, radius);
		while (ServerGameObject* o = (ServerGameObject*)search.next())
			if (!o->deleted && o->blueprint->bpClass != Tags::GAMEOBJCLASS_CHARACTER && o->blueprint->name.find(word) != std::string::npos
				&& (o->position - around).sqlen2xz() <= radius * radius)
				found.push_back(o);
		std::sort(found.begin(), found.end(), [&](ServerGameObject* a, ServerGameObject* b) {
			const float da = (a->position - peasant->position).sqlen2xz(), db = (b->position - peasant->position).sqlen2xz();
			return da != db ? da < db : a->id < b->id; });
		for (size_t i = 0; i < found.size() && i < 8; i++)
			if (cmd.canBeExecuted(peasant, found[i]))
				return found[i];
		return nullptr;
	}

	// ---- Builders' Guild ----
	struct Builders : Department {
		uint8_t id() const override { return BUILDERS; }
		float period() const override { return 10.0f; }
		std::map<uint32_t, std::pair<uint32_t, float>> repairing; // building -> (peasant, since)

		void think(Government& g) override {
			if (MinOff() & 4) return;
			Server& srv = g.server;
			ServerGameObject* me = g.player();
			if (!me)
				return;
			g_items.update(srv.gameSet.get());
			if (g_items.si < 0 || g_items.siCap < 0 || g_items.repair < 0)
				return;
			const float now = g.now();
			AIController& ai = me->aiController;
			// the repairs done (or given up after 2 minutes): their peasants back to the scripts
			for (auto it = repairing.begin(); it != repairing.end();) {
				ServerGameObject* b = srv.findObject(it->first);
				ServerGameObject* p = srv.findObject(it->second.first);
				const bool done = !b || b->deleted || b->getItem(g_items.si) >= b->getItem(g_items.siCap) * 0.98f;
				if (done || !p || p->deleted || now - it->second.second > 120.0f) {
					ai.ministryHeld.erase(it->second.first);
					if (done && b)
						g.board.report("builders.repaired", g.board.get("builders.repaired") + 1);
					it = repairing.erase(it);
				}
				else
					++it;
			}
			if (repairing.size() >= 2)
				return;
			std::vector<ServerGameObject*> damaged;
			std::vector<ServerGameObject*> todo{ me };
			while (!todo.empty()) {
				ServerGameObject* o = todo.back();
				todo.pop_back();
				for (const auto& [t, children] : o->children)
					for (CommonGameObject* c : children) {
						auto* b = (ServerGameObject*)c;
						if (b->deleted || b->blueprint->bpClass == Tags::GAMEOBJCLASS_CHARACTER)
							continue;
						todo.push_back(b);
						const float cap = b->getItem(g_items.siCap);
						if (b->blueprint->bpClass == Tags::GAMEOBJCLASS_BUILDING && cap > 0.0f && b->getItem(g_items.si) < 0.7f * cap && !repairing.count(b->id)
							&& b->blueprint->name.find("Foundation") == std::string::npos)
							damaged.push_back(b);
					}
			}
			std::sort(damaged.begin(), damaged.end(), [](auto* a, auto* b) { return a->id < b->id; });
			const Command& cmd = srv.gameSet->commands[g_items.repair];
			std::vector<ServerGameObject*> peasants = AIUtil::Peasants(me);
			for (ServerGameObject* b : damaged) {
				if (repairing.size() >= 2)
					break;
				if (!AIUtil::EnemiesNear(me, b->position, 40.0f, false).empty())
					continue;
				ServerGameObject* best = nullptr;
				for (ServerGameObject* p : peasants)
					if (!ai.ministryHeld.count(p->id) && cmd.canBeExecuted(p, b) && (!best || (p->position - b->position).sqlen2xz() < (best->position - b->position).sqlen2xz()))
						best = p;
				if (!best)
					continue;
				cmd.execute(best, b, Tags::ORDERASSIGNMODE_FORGET_EVERYTHING_ELSE);
				ai.ministryHeld.insert(best->id);
				repairing[b->id] = { best->id, now };
				g.log(BUILDERS, "peasant %u repairs %s %u (%.0f of %.0f)", best->id, b->blueprint->name.c_str(), b->id, b->getItem(g_items.si), b->getItem(g_items.siCap));
			}
		}
		void save(const Government&, std::vector<std::string>& lines) const override {
			for (const auto& [b, r] : repairing)
				lines.push_back("repair " + std::to_string(b) + " " + std::to_string(r.first) + " " + std::to_string(r.second));
		}
		void load(Government& g, const std::string& line) override {
			unsigned b = 0, p = 0;
			float since = 0.0f;
			if (sscanf(line.c_str(), "repair %u %u %f", &b, &p, &since) == 3) {
				repairing[b] = { p, since };
				if (ServerGameObject* me = g.player())
					me->aiController.ministryHeld.insert(p);
			}
		}
	};

	// ---- Research ----
	struct Research : Department {
		uint8_t id() const override { return RESEARCH; }
		float period() const override { return 15.0f; }

		void think(Government& g) override {
			if (MinOff() & 8) return;
			Server& srv = g.server;
			ServerGameObject* me = g.player();
			if (!me || g.now() < 360.0f)
				return;
			// a surplus over what War and the reserves need
			if (me->getItem(Tags::PDITEM_FOOD) < 800.0f || me->getItem(Tags::PDITEM_WOOD) < 400.0f || me->getItem(Tags::PDITEM_GOLD) < 300.0f)
				return;
			const GameSet* gs = srv.gameSet.get();
			const bool military = g.personality.aggression >= 0.5f;
			static const char* const MILITARY[] = { "Infantry", "Cavalry", "Crossbow", "Gunpowder", "Arrow", "Blade", "Armour", "Barding", "Chivalry", "Grapeshot", "Greek Fire" };
			static const char* const ECONOMY[] = { "Sawmill", "Windmill", "Plough", "Crop", "Banking", "Harness", "Alchemy" };
			auto kindOf = [&](const std::string& name) {
				for (const char* w : MILITARY) if (name.find(w) != std::string::npos) return 1;
				for (const char* w : ECONOMY) if (name.find(w) != std::string::npos) return 2;
				return 3;
			};
			std::vector<ServerGameObject*> buildings, todo{ me };
			while (!todo.empty()) {
				ServerGameObject* o = todo.back();
				todo.pop_back();
				for (const auto& [t, children] : o->children)
					for (CommonGameObject* c : children) {
						auto* b = (ServerGameObject*)c;
						if (b->deleted || b->blueprint->bpClass == Tags::GAMEOBJCLASS_CHARACTER)
							continue;
						todo.push_back(b);
						if (b->blueprint->bpClass == Tags::GAMEOBJCLASS_BUILDING && !b->orderConfig.getCurrentOrder())
							buildings.push_back(b);
					}
			}
			std::sort(buildings.begin(), buildings.end(), [](auto* a, auto* b) { return a->id < b->id; });
			const Command* best = nullptr;
			ServerGameObject* where = nullptr;
			int bestRank = 99;
			for (ServerGameObject* b : buildings)
				for (const Command* cmd : b->blueprint->offeredCommands) {
					const std::string& name = gs->commands.getString(cmd);
					if (name.rfind("Research ", 0) != 0 || !cmd->canBeExecuted(b))
						continue;
					const int kind = kindOf(name);
					const int rank = kind == 3 ? 3 : (kind == 1) == military ? 1 : 2;
					if (rank < bestRank) { bestRank = rank; best = cmd; where = b; }
				}
			if (!best)
				return;
			best->execute(where, nullptr, Tags::ORDERASSIGNMODE_DO_FIRST);
			const std::string& name = gs->commands.getString(best);
			g.board.report("research.ordered", g.board.get("research.ordered") + 1);
			g.log(RESEARCH, "%s at %s %u", name.c_str(), where->blueprint->name.c_str(), where->id);
		}
	};
}

// Economy's asks, from its reports (Economy::think): gold miners while gold doesn't come in
void EconomyNeeds(Government& g)
{
	ServerGameObject* me = g.player();
	if (!me)
		return;
	const float now = g.now();
	const double gold = g.board.get("economy.gold"), income = g.board.get("economy.gold.income");
	// Gold only when it is in the way: a fighter its buildings can train (possible now) costs more gold
	// than there is. (Asked for whenever gold didn't come in, the miners came from the wood and cost a
	// third of the soldiers one against one, while the gold stayed unspent: 400-500 at the end.)
	double goldNeeded = 0.0;
	if (now > 180.0f && g.department(ECONOMY) && g.department(ECONOMY)->thinks % 15 == 0) {
		Server& srv = g.server;
		const GameSet* gs = srv.gameSet.get();
		std::vector<ServerGameObject*> todo{ me };
		while (!todo.empty()) {
			ServerGameObject* o = todo.back();
			todo.pop_back();
			for (const auto& [t, children] : o->children)
				for (CommonGameObject* c : children) {
					auto* b = (ServerGameObject*)c;
					if (b->deleted || b->blueprint->bpClass == Tags::GAMEOBJCLASS_CHARACTER)
						continue;
					todo.push_back(b);
					if (b->blueprint->bpClass != Tags::GAMEOBJCLASS_BUILDING)
						continue;
					for (const Command* cmd : b->blueprint->offeredCommands) {
						const std::string& name = gs->commands.getString(cmd);
						if (name.rfind("Spawn ", 0) != 0)
							continue;
						const GameObjBlueprint* type = gs->findBlueprint(Tags::GAMEOBJCLASS_CHARACTER, name.substr(6));
						const float cost = type ? type->getStartingItemValue(Tags::PDITEM_GOLD_COST) : 0.0f;
						if (!type || cost <= gold || AIUtil::TypeClass(type) == 0)
							continue;
						SrvScriptContext cctx(&srv, b);
						auto _s = cctx.selectedObject.change(b);
						if (std::all_of(cmd->conditionsImpossible.begin(), cmd->conditionsImpossible.end(), [&](GSCondition* cond) { return cond->test->booleval(&cctx); }))
							goldNeeded = std::max(goldNeeded, (double)cost);
					}
				}
		}
		g.board.report("economy.goldForFighters", goldNeeded);
	}
	else
		goldNeeded = g.board.get("economy.goldForFighters");
	// and for Research (it spends a surplus from 300 gold), with wood to spare only: alone, the gold this
	// mined gave Belisar 212 characters at 30 minutes against 135
	if (goldNeeded < 300.0 && g.board.get("economy.wood") > 600.0)
		goldNeeded = 300.0;
	g.board.report("economy.goldNeeded", goldNeeded);
	const int want = goldNeeded > gold && income < 20.0 ? 3 + (g.personality.aggression > 0.5f ? 1 : 0) : goldNeeded <= gold || gold > 1500.0 ? 0 : -1;
	if (want > 0)
		g.board.ask(ECONOMY, "workers.gold", (float)want, 2.0f, gold < 200.0 ? 2.0f : 1.0f, false, now);
	else if (want == 0)
		g.board.withdraw(ECONOMY, "workers.gold");
}

// Labour & Population's acts (Labour::think): the cap, idle peasants, the gold miners asked for
void LabourActs(Government& g, std::map<uint32_t, float>& goldMiners)
{
	Server& srv = g.server;
	ServerGameObject* me = g.player();
	if (!me)
		return;
	g_items.update(srv.gameSet.get());
	const float now = g.now();
	AIController& ai = me->aiController;
	const std::vector<ServerGameObject*> peasants = AIUtil::Peasants(me);
	// the cap: a share of the population limit (40-60 %), at least 20
	const float limit = g_items.popLimit >= 0 ? me->getItem(g_items.popLimit) : 200.0f;
	ai.ministryPeasantCap = std::max(20, (int)(limit * (0.6f - 0.2f * g.personality.aggression)));
	ai.ministryPeasants = (int)peasants.size();
	g.board.report("labour.cap", ai.ministryPeasantCap);
	ServerGameObject* palace = AIUtil::Palace(me);
	const Vector3 home = palace ? palace->position : me->playerStartCameraPosition;
	// No militia but in an emergency: the scripts' "Cannot Use Militia", which the war layer sets only
	// while 5 soldiers are at home. With the missions out, a Kingmaker's defence raised every peasant
	// as militia at once (47 in a minute, one against one) and they all died: the economy gone. Only
	// a threat at home over twice what defends it with the palace under 60 % lets them fight.
	{
		static uint64_t of = 0;
		static int noMilitia = -1, si = -1, siCap = -1;
		if (of != srv.gameSet->serial) {
			of = srv.gameSet->serial;
			noMilitia = srv.gameSet->items.names.getIndex("Cannot Use Militia");
			si = srv.gameSet->items.names.getIndex("Structural Integrity");
			siCap = srv.gameSet->items.names.getIndex("Structural Integrity Capacity");
		}
		if (noMilitia >= 0 && !(MinOff() & 512) && ai.ministryDefence) { // (else the war layer's rule, with its defence: militia while fewer than 5 soldiers)
			// (what defends: the soldiers at home and War's defences; counting only those at home, "0"
			// while all of them defended)
			float defence = 0.0f;
			for (ServerGameObject* u : ai.freeFighters(home))
				defence += AIUtil::UnitPower(u);
			for (Mission& m : g.missions)
				if (m.kind == Mission::DEFEND)
					for (ServerGameObject* u : g.alive(m))
						defence += AIUtil::UnitPower(u);
			const float threat = (float)g.board.get("intel.threat.home");
			const bool palaceFalling = palace && si >= 0 && siCap >= 0 && palace->getItem(siCap) > 0.0f && palace->getItem(si) < 0.6f * palace->getItem(siCap);
			// (the palace falling, and the threat over twice the defence or War's defence unable to meet
			// the palace's attackers with every soldier there is: four players, a Kingmaker overrun lost
			// 32 peasants who weren't allowed to fight. Not "unable" alone: an army passing 240 m from the
			// palace made all 45 peasants militia, which never went back to work, and died)
			const bool falling = palaceFalling && (threat > 2.0f * defence + 50.0f || g.board.get("war.palaceUnmet") > 0.0);
			const bool emergency = falling;
			if (emergency != (me->getItem(noMilitia) == 0.0f))
				g.log(LABOUR, emergency ? "the peasants may fight (a threat of %.0f against %.0f, the palace falling)" : "no militia (a threat of %.0f against %.0f)", threat, defence);
			me->setItem(noMilitia, emergency ? 0.0f : 1.0f);
			// (the scripts convert in one batch once allowed, 22 at once in a test, and those on their way
			// finish after it is forbidden again: unless the palace is falling, the conversions over 8
			// militia are called back, the newest first, and they go back to work)
			if (!falling) {
				std::vector<ServerGameObject*> converting;
				int armed = 0;
				for (ServerGameObject* p : peasants) {
					const char* o = CurrentOrder(srv, p);
					if (strstr(o, "Militia"))
						converting.push_back(p);
				}
				armed = (int)g.board.get("labour.militia") - (int)converting.size();
				std::sort(converting.begin(), converting.end(), [](auto* a, auto* b) { return a->id > b->id; });
				int called = 0;
				for (ServerGameObject* p : converting)
					if (armed + (int)converting.size() - called > 8) {
						p->orderConfig.cancelAllOrders();
						called++;
					}
				if (called) {
					g.log(LABOUR, "%d peasants called back from becoming militia (%d militia)", called, armed + (int)converting.size() - called);
					g.board.report("labour.militiaCalledBack", g.board.get("labour.militiaCalledBack") + called);
				}
			}
			// the emergency over: the militia with no enemy within 100 m go back to work (their own
			// "Disband Militia"; the scripts never send them back: in a four-player game 43 militia stayed
			// soldiers until they died, the economy gone)
			// (and not while the threat is more than the soldiers can meet)
			if (!emergency && threat <= defence) {
				int disbanded = 0;
				const GameSet* gs = srv.gameSet.get();
				std::vector<ServerGameObject*> militia;
				// (all of the player's, through its armies, cities and towns)
				std::vector<ServerGameObject*> todo{ me };
				while (!todo.empty()) {
					ServerGameObject* o = todo.back();
					todo.pop_back();
					for (const auto& [type, children] : o->children)
						for (CommonGameObject* c : children) {
							auto* u = (ServerGameObject*)c;
							if (u->deleted)
								continue;
							if (u->blueprint->bpClass == Tags::GAMEOBJCLASS_CHARACTER) {
								if (u->blueprint->name.find("Militia") != std::string::npos && !ai.ministryUnits.count(u->id) && !(u->flags & ServerGameObject::fTerminated))
									militia.push_back(u);
							}
							else if (u->blueprint->bpClass != Tags::GAMEOBJCLASS_BUILDING)
								todo.push_back(u);
						}
				}
				std::sort(militia.begin(), militia.end(), [](auto* a, auto* b) { return a->id < b->id; });
				for (ServerGameObject* u : militia) {
					if (!AIUtil::EnemiesNear(me, u->position, 100.0f, false).empty())
						continue;
					for (const Command* c : u->blueprint->offeredCommands) {
						const std::string& n = gs->commands.names.getString(c->id);
						if (n.find("Militia") == std::string::npos || n.find("Convert") != std::string::npos || !c->canBeExecuted(u, nullptr))
							continue;
						c->execute(u, nullptr, Tags::ORDERASSIGNMODE_FORGET_EVERYTHING_ELSE);
						disbanded++;
						break;
					}
				}
				if (disbanded) {
					g.log(LABOUR, "%d militia back to work", disbanded);
					g.board.report("labour.disbanded", g.board.get("labour.disbanded") + disbanded);
				}
			}
		}
	}
	// the gold miners: the dead and those whose gold ran out let go (another gold source first)
	for (auto it = goldMiners.begin(); it != goldMiners.end();) {
		ServerGameObject* p = srv.findObject(it->first);
		if (!p || p->deleted) {
			ai.ministryHeld.erase(it->first);
			it = goldMiners.erase(it);
			continue;
		}
		if (!p->orderConfig.getCurrentOrder()) {
			if (ServerGameObject* gold = FindWork(srv, p, g_items.mineGold, "Gold", p->position, 250.0f))
				srv.gameSet->commands[g_items.mineGold].execute(p, gold, Tags::ORDERASSIGNMODE_FORGET_EVERYTHING_ELSE);
			else {
				ai.ministryHeld.erase(it->first);
				it = goldMiners.erase(it);
				continue;
			}
		}
		++it;
	}
	const Request* ask = g.board.find(ECONOMY, "workers.gold");
	const int want = ask ? (int)ask->amount : 0;
	if (want > 0 && g.board.get("labour.goldAsked") == 0.0)
		g.log(LABOUR, "Economy asks for %d gold miners (%zu peasants, %zu held)", want, peasants.size(), ai.ministryHeld.size());
	if (want > 0)
		g.board.report("labour.goldAsked", g.board.get("labour.goldAsked") + 1);
	// enough gold: those held at least 2 minutes go back to the scripts
	if (!ask)
		for (auto it = goldMiners.begin(); it != goldMiners.end();)
			if (now - it->second > 120.0f) {
				ai.ministryHeld.erase(it->first);
				it = goldMiners.erase(it);
			}
			else
				++it;
	// more miners: the idle first, then woodcutters (with wood to spare), the nearest the gold
	if ((int)goldMiners.size() < want && !(MinOff() & 2)) {
		// (with wood to spare all of them; without, two at most: the scripts keep every peasant busy,
		// and in a stage-1 game wood stayed under 300 for 10 minutes, so no miner ever went)
		const bool woodToSpare = me->getItem(Tags::PDITEM_WOOD) > 500.0f;
		// (from the wood only with wood to spare: soldiers need it)
		const int fromWood = woodToSpare ? want : 0;
		int woodTaken = 0;
		for (const auto& [id, since] : goldMiners)
			woodTaken++;
		std::vector<ServerGameObject*> candidates;
		for (ServerGameObject* p : peasants)
			if (!ai.ministryHeld.count(p->id) && p->isInteractable()) {
				const std::string order = CurrentOrder(srv, p);
				if (order.empty() || (order == "Chop Tree" && woodTaken < fromWood))
					candidates.push_back(p);
			}
		std::stable_sort(candidates.begin(), candidates.end(), [&](ServerGameObject* a, ServerGameObject* b) {
			const bool ia = !a->orderConfig.getCurrentOrder(), ib = !b->orderConfig.getCurrentOrder();
			if (ia != ib) return ia;
			return a->id < b->id; });
		for (ServerGameObject* p : candidates) {
			if ((int)goldMiners.size() >= want)
				break;
			ServerGameObject* gold = FindWork(srv, p, g_items.mineGold, "Gold", home, 300.0f);
			if (!gold) {
				if (g.board.get("labour.goldNone") == 0.0)
					g.log(LABOUR, "gold asked for, but no gold within 300 m of home the peasants can mine (%zu candidates)", candidates.size());
				g.board.report("labour.goldNone", g.board.get("labour.goldNone") + 1);
				break; // (no gold near home)
			}
			if (!g.board.assign(p->id, ECONOMY, now, 120.0f, false))
				continue;
			srv.gameSet->commands[g_items.mineGold].execute(p, gold, Tags::ORDERASSIGNMODE_FORGET_EVERYTHING_ELSE);
			ai.ministryHeld.insert(p->id);
			goldMiners[p->id] = now;
			g.board.report("labour.goldSent", g.board.get("labour.goldSent") + 1);
			g.log(LABOUR, "peasant %u to mine gold at %s %u (%zu of %d)", p->id, gold->blueprint->name.c_str(), gold->id, goldMiners.size(), want);
		}
	}
	g.board.report("labour.goldMiners", (double)goldMiners.size());
	// idle peasants (not held), more than a few: to the wood near home (the scripts give the rest work);
	// not where enemy soldiers are (four players, seed 3: the peasants that had fled an army stood idle
	// at home and were sent to the nearest trees, back to it: 45 peasants down to 22. With this rule
	// switched off he ended with 130 characters instead of 36)
	// Only once more than two have stood idle for 10 s: the scripts hand out work a moment after a
	// building is done (four players, seed 3: 20 of 33 peasants idle for a moment were all sent to the
	// trees, the scripts' next buildings went up late, and the game went its own way: 6 living
	// characters at 30 minutes against 132 for the same General without the ministries)
	int idle = 0;
	{
		int waiting = 0;
		for (ServerGameObject* p : peasants)
			waiting += !ai.ministryHeld.count(p->id) && !p->orderConfig.getCurrentOrder() && p->isInteractable();
		// ("labour.idleFrom": when more than two became idle, plus 1; 0 while not)
		if (waiting <= 2)
			g.board.report("labour.idleFrom", 0.0);
		else if (g.board.get("labour.idleFrom") == 0.0)
			g.board.report("labour.idleFrom", now + 1.0);
	}
	const double idleFrom = g.board.get("labour.idleFrom");
	if (!(MinOff() & 1) && idleFrom > 0.0 && now - (idleFrom - 1.0) >= 10.0)
	for (ServerGameObject* p : peasants)
		if (!ai.ministryHeld.count(p->id) && !p->orderConfig.getCurrentOrder() && p->isInteractable()) {
			if (++idle <= 2)
				continue;
			if (!AIUtil::EnemiesNear(me, p->position, 200.0f, false).empty())
				continue;
			if (ServerGameObject* tree = FindWork(srv, p, g_items.chop, "Tree", p->position, 120.0f); tree && AIUtil::EnemiesNear(me, tree->position, 150.0f, false).empty()) {
				srv.gameSet->commands[g_items.chop].execute(p, tree, Tags::ORDERASSIGNMODE_FORGET_EVERYTHING_ELSE);
				g.board.report("labour.putToWork", g.board.get("labour.putToWork") + 1);
			}
		}
}

std::unique_ptr<Department> MakeBuilders() { return std::make_unique<Builders>(); }
std::unique_ptr<Department> MakeResearch() { return std::make_unique<Research>(); }

}
