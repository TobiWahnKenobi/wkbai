// wkbai - the ministry AI's tactics (RE repo docs/engine/ministry-ai.md, step 6): training by
// counters for War, and the units' abilities for the Generals, from a rule table.
//
// - Training: with food to spare (over 500, after 2 minutes) every idle building that can train a
//   fighter trains one: the type whose class fares best against the main enemy's known army
//   (AIUtil::ClassFactor over its classes' share of power), less a class the army already has too
//   much of (for a General who likes combined arms), and the big summons (Abaddon, the Archangel)
//   when the General likes them and they are paid for.
// - Abilities: each rule names a command, what it aims at (a wounded friend, an enemy fighter, an
//   enemy building, the unit itself in a fight) and its reach. A unit of a mission (or at home with
//   a threat there) uses one it offers when the command's own conditions allow it for that target
//   (Command::canBeExecuted), at most every 8 s.

#include "MinistryDepartments.h"
#include "AIUtil.h"
#include "Naval.h"
#include "server.h"
#include "gameset/gameset.h"
#include "gameset/command.h"
#include "NNSearch.h"
#include "StampdownPlan.h"
#include <algorithm>
#include <cmath>

namespace Ministry {

namespace {
	enum Aim : uint8_t { HURT_FRIEND, ENEMY_FIGHTER, ENEMY_BUILDING, SELF_IN_FIGHT, ENEMY_PEASANT, SELF_AT_BUILDING };
	// (extra: the specialists' rules, WKB_MINOFF 524288 off: the powers the alignments' units
	// offer that no rule used, from the game set's command lists)
	struct Rule { const char* command; Aim aim; float reach; bool extra = false; };
	const Rule RULES[] = {
		{ "Possess", ENEMY_PEASANT, 30, true }, { "Desecrate", ENEMY_BUILDING, 30, true }, { "Plunder", ENEMY_BUILDING, 20, true },
		{ "Burn Heretic", ENEMY_FIGHTER, 40, true }, { "Burn Unbeliever", ENEMY_FIGHTER, 40, true },
		{ "Inflict Plague", ENEMY_FIGHTER, 60, true }, { "Inflict Lightning", ENEMY_FIGHTER, 60, true }, { "Inflict Firestorm", ENEMY_FIGHTER, 60, true },
		{ "Inflict Wormwood", ENEMY_FIGHTER, 60, true }, { "Inflict Fimblewinter", ENEMY_FIGHTER, 60, true }, { "Inflict Earthquake", ENEMY_BUILDING, 60, true },
		{ "Stamp!", SELF_IN_FIGHT, 25, true }, { "Shock!", SELF_IN_FIGHT, 25, true }, { "Summon Gibbering Horde", SELF_IN_FIGHT, 40, true },
		{ "Ghost Army", SELF_IN_FIGHT, 40, true }, { "Pray for the Imperial", SELF_IN_FIGHT, 40, true },
		{ "Trigger Explosives", SELF_AT_BUILDING, 12, true },
		{ "Artos Heal", HURT_FRIEND, 30 }, { "Monastic Knight Heal", HURT_FRIEND, 30 }, { "Repair Character", HURT_FRIEND, 20 },
		{ "Bless", HURT_FRIEND, 30 }, { "Barkskin", HURT_FRIEND, 30 },
		{ "Curse", ENEMY_FIGHTER, 40 }, { "Entangle", ENEMY_FIGHTER, 40 }, { "Inflict Blindness", ENEMY_FIGHTER, 40 },
		{ "Charm", ENEMY_FIGHTER, 40 }, { "Charm of Madness", ENEMY_FIGHTER, 40 }, { "Banish", ENEMY_FIGHTER, 40 },
		{ "Holy Fire", ENEMY_FIGHTER, 40 }, { "Exorcism", ENEMY_FIGHTER, 40 }, { "Spew Acid", ENEMY_FIGHTER, 30 },
		{ "Poison Bile", ENEMY_FIGHTER, 30 }, { "Squirt Ichor", ENEMY_FIGHTER, 30 }, { "Throw Rock", ENEMY_FIGHTER, 40 },
		{ "Arson", ENEMY_BUILDING, 30 }, { "Blow Open Gates", ENEMY_BUILDING, 20 },
		{ "Pray for Righteousness", SELF_IN_FIGHT, 40 }, { "Make Baresark", SELF_IN_FIGHT, 40 }, { "Swashbuckle", SELF_IN_FIGHT, 30 },
	};
	constexpr int RULE_COUNT = (int)(sizeof(RULES) / sizeof(RULES[0]));

	struct Lookup {
		const GameSet* gs = nullptr;
		uint64_t of = 0; // (the game set's serial: the blueprints' addresses can be the last game's)
		int command[RULE_COUNT];
		int hp = -1, hpCap = -1;
		std::map<const GameObjBlueprint*, std::vector<int>> offered; // the rules a type offers
		void update(const GameSet* set) {
			gs = set;
			if (of == set->serial)
				return;
			of = set->serial;
			for (int i = 0; i < RULE_COUNT; i++)
				command[i] = gs->commands.names.getIndex(RULES[i].command);
			hp = gs->items.names.getIndex("Hit Points");
			hpCap = gs->items.names.getIndex("Hit Point Capacity");
			offered.clear();
		}
		const std::vector<int>& rulesOf(const GameObjBlueprint* bp) {
			auto it = offered.find(bp);
			if (it != offered.end())
				return it->second;
			std::vector<int> list;
			for (int i = 0; i < RULE_COUNT; i++)
				if (command[i] >= 0 && !(RULES[i].extra && (MinOff() & 524288)))
					for (const Command* c : bp->offeredCommands)
						if (c == &gs->commands[command[i]]) { list.push_back(i); break; }
			return offered[bp] = list;
		}
	} g_lookup;

	// A unit type's role for the training (WKB_MINOFF 524288 off). The game's "I'm A Military Unit"
	// is 1-4 for the line (heavy and light infantry, heavy and light cavalry), 5 for the scout and
	// the spy, 6 for engines and monsters (Bombard, Rocket Launcher, Infernal Engine, Behemoth, War
	// Elephant, Abaddon, Michael, ...), and nothing for the priests, monks, druids and succubi.
	// Before, only 1-4, the named siege engines and the big summons were trained.
	int RoleOf(const GameSet* gs, const GameObjBlueprint* t)
	{
		static uint64_t of = 0;
		static int military = -1, missile = -1, range = -1;
		static std::map<const GameObjBlueprint*, int> known;
		if (of != gs->serial) {
			of = gs->serial;
			military = gs->items.names.getIndex("I'm A Military Unit");
			missile = gs->items.names.getIndex("Missile Damage");
			range = gs->items.names.getIndex("Maximum Missile Range");
			known.clear();
		}
		if (!t)
			return ROLE_NONE;
		if (auto it = known.find(t); it != known.end())
			return it->second;
		const std::string& n = t->name;
		const int cls = military >= 0 ? (int)t->getStartingItemValue(military) : 0;
		int role = ROLE_NONE;
		if (n == "Sapper")
			role = ROLE_ENGINE; // (builds the Rocket Launcher, the Trebuchet and the Ram in the field)
		else if (cls >= 1 && cls <= 4)
			role = ROLE_LINE;
		else if (cls == 5)
			role = n.find("Spy") != std::string::npos ? ROLE_SABOTEUR : ROLE_NONE;
		else if (cls == 6) {
			const bool far = missile >= 0 && range >= 0 && t->getStartingItemValue(missile) > 0.0f && t->getStartingItemValue(range) >= 90.0f;
			if (n.find("Abaddon") != std::string::npos || n.find("Michael") != std::string::npos || n.find("Limbered") != std::string::npos)
				role = ROLE_NONE; // (the big summons as before; a limbered engine is the same one packed)
			else
				role = IsSiegeName(n) || far || n.find("Explosive") != std::string::npos ? ROLE_ENGINE : ROLE_MONSTER;
		}
		else
			for (const char* s : { "Monk", "Priest", "Bishop", "Inquisitor", "High Priestess", "Arch Druid", "Succubus" })
				if (n == s)
					role = ROLE_SUPPORT;
		return known[t] = role;
	}

	float Health(const ServerGameObject* o)
	{
		if (g_lookup.hp < 0 || g_lookup.hpCap < 0)
			return 1.0f;
		const float cap = o->getItem(g_lookup.hpCap);
		return cap > 0.0f ? o->getItem(g_lookup.hp) / cap : 1.0f;
	}
}

int UnitRole(const GameSet* gs, const GameObjBlueprint* type) { return RoleOf(gs, type); }

int UseAbilities(Government& g, const std::vector<ServerGameObject*>& units, std::map<uint64_t, float>& ready)
{
	ServerGameObject* me = g.player();
	Server& srv = g.server;
	if (!me || units.empty())
		return 0;
	const GameSet* gs = srv.gameSet.get();
	g_lookup.update(gs);
	const float now = g.now();
	int used = 0;
	for (ServerGameObject* u : units) {
		const std::vector<int>& rules = g_lookup.rulesOf(u->blueprint);
		if (rules.empty())
			continue;
		for (int r : rules) {
			const uint64_t key = ((uint64_t)u->id << 8) | (uint64_t)r;
			if (auto it = ready.find(key); it != ready.end() && now < it->second)
				continue;
			const Rule& rule = RULES[r];
			const Command& cmd = gs->commands[g_lookup.command[r]];
			ServerGameObject* target = nullptr;
			switch (rule.aim) {
			case HURT_FRIEND: {
				float worst = 0.7f;
				for (ServerGameObject* f : units)
					if (f != u && (f->position - u->position).sqlen2xz() < rule.reach * rule.reach && Health(f) < worst && cmd.canBeExecuted(u, f)) {
						worst = Health(f);
						target = f;
					}
				break;
			}
			case ENEMY_FIGHTER: {
				float best = 0.0f;
				for (ServerGameObject* e : AIUtil::EnemiesNear(me, u->position, rule.reach, false))
					if (AIUtil::UnitPower(e) > best && cmd.canBeExecuted(u, e)) {
						best = AIUtil::UnitPower(e);
						target = e;
					}
				break;
			}
			case ENEMY_BUILDING:
				for (ServerGameObject* b : AIUtil::EnemyBuildingsNear(me, u->position, rule.reach))
					if (cmd.canBeExecuted(u, b)) { target = b; break; }
				break;
			case SELF_IN_FIGHT:
				if (!AIUtil::EnemiesNear(me, u->position, rule.reach, false).empty() && cmd.canBeExecuted(u, nullptr))
					target = u;
				break;
			case ENEMY_PEASANT: {
				NNSearch search;
				search.start(&srv, u->position, rule.reach);
				std::vector<ServerGameObject*> found;
				while (ServerGameObject* e = (ServerGameObject*)search.next())
					if (!e->deleted && AIUtil::IsPeasant(e) && AIUtil::IsEnemy(me, e) && (e->position - u->position).sqlen2xz() <= rule.reach * rule.reach)
						found.push_back(e);
				std::sort(found.begin(), found.end(), [](auto* a, auto* b) { return a->id < b->id; });
				for (ServerGameObject* e : found)
					if (cmd.canBeExecuted(u, e)) { target = e; break; }
				break;
			}
			case SELF_AT_BUILDING:
				if (!AIUtil::EnemyBuildingsNear(me, u->position, rule.reach).empty() && cmd.canBeExecuted(u, nullptr))
					target = u;
				break;
			}
			if (!target)
				continue;
			cmd.execute(u, target == u ? nullptr : target, Tags::ORDERASSIGNMODE_DO_FIRST);
			ready[key] = now + 8.0f;
			used++;
			const std::string k = std::string("tactics.") + rule.command;
			g.board.report(k, g.board.get(k) + 1);
			g.log(GENERALS, "%s %u: %s at %s %u", u->blueprint->name.c_str(), u->id, rule.command, target->blueprint->name.c_str(), target->id);
			break; // (one ability a unit a think)
		}
	}
	// (the cooldowns past are dropped)
	for (auto it = ready.begin(); it != ready.end();)
		it = now >= it->second ? ready.erase(it) : std::next(it);
	return used;
}

// The opening. One against one, 15 seeds on each of plain WKB, WKB Plus 2021 and Plus v9.5, the
// games were decided by the first wave: about 10 soldiers at minutes 15-22 (plain, 2021), 40-80 at
// minutes 7-9 (v9.5), against a side with 0-6 soldiers among 20-60 peasants. And the ministries
// changed nothing before minute 7-16: they trained only with food over 500, which the scripts'
// peasant spawns never left. So from minute 4 to 20 a home guard: 12 fighters, or 1.2 times the
// most fighters an enemy has been seen with (at most 60). While it is short, Tactics trains
// whatever is affordable. (Labour held the peasants too while it was short; four players under
// plain WKB, with food at 0-50, that stalled the economy at 27 peasants for minutes, so the hold
// now follows the warning, DangerShort.)
int OpeningShort(Government& g)
{
	ServerGameObject* me = g.player();
	if (!me || (MinOff() & 1024))
		return 0;
	const float now = g.played();
	if (now < 240.0f || now > Tune("openingEnd", 1200.0f))
		return 0;
	std::map<uint32_t, int> seen;
	for (const auto& [id, s] : g.picture.sightings)
		if (s.fighter)
			seen[s.player]++;
	int most = 0;
	for (const auto& [p, n] : seen)
		most = std::max(most, n);
	// (the floor by the map: 12 with an enemy starting within 700 m, 8 further away; improvement 6)
	const double nearest = g.board.get("map.nearestEnemy", -1.0);
	const int floor = (int)Tune("openingFloor", 12.0f) - (nearest >= 0.0 && nearest > 700.0 ? 4 : 0);
	int want = std::min((int)Tune("openingCap", 60.0f), std::max(floor, (int)std::ceil(most * Tune("openingFactor", 1.2f))));
	// (on an island, the sea the only way in: no land rush can come, so a guard of 4, and the
	// peasants aren't held for one; on 3 Isles the guard kept the economy small for 16 minutes)
	if (me->aiController.naval.need == Naval::NEED_ONLY)
		want = std::min(want, 4);
	const int have = me->aiController.fighters();
	g.board.report("opening.guard", want);
	return std::max(0, want - have);
}

// The warning: enemy fighters near the palace (Intelligence's intel.approach); short while there
// are fewer than 1.2 times as many of the own. Any time in the game: Tactics trains, Labour holds
// the peasants (their food goes into soldiers), War calls the missions home.
int DangerShort(Government& g)
{
	ServerGameObject* me = g.player();
	if (!me || (MinOff() & 2048))
		return 0;
	const int seen = (int)g.board.get("intel.approach");
	if (seen <= 0)
		return 0;
	return std::max(0, (int)std::ceil(seen * Tune("warnFactor", 1.2f)) - me->aiController.fighters());
}

int TrainByCounters(Government& g)
{
	ServerGameObject* me = g.player();
	Server& srv = g.server;
	if (!me)
		return 0;
	const GameSet* gs = srv.gameSet.get();
	const float now = g.played();
	// (the opening's guard short: whatever is affordable, Train checks)
	const int short_ = std::max(OpeningShort(g), DangerShort(g));
	const float spare = Tune("trainFood", 500.0f);
	if (short_ == 0 && (now < 120.0f || me->getItem(Tags::PDITEM_FOOD) <= spare))
		return 0;
	// the main enemy: the one whose known army is strongest; its classes' share of power
	float shares[5] = {}, total = 0.0f;
	{
		std::map<uint32_t, float> power;
		for (const auto& [id, s] : g.picture.sightings)
			if (s.fighter)
				power[s.player] += s.power;
		uint32_t main = 0;
		float best = 0.0f;
		for (const auto& [p, v] : power)
			if (v > best) { best = v; main = p; }
		for (const auto& [id, s] : g.picture.sightings)
			if (s.fighter && s.player == main) { shares[s.cls] += s.power; total += s.power; }
	}
	// the own army's classes
	int own[5] = {}, ownTotal = 0;
	for (ServerGameObject* u : me->aiController.freeFighters(me->playerStartCameraPosition)) {
		own[AIUtil::ClassOf(u)]++;
		ownTotal++;
	}
	const Personality& c = g.personality;
	const int bigGuys = gs->items.names.getIndex("Summons Big Guys");
	const bool summons = bigGuys >= 0 && me->getItem(bigGuys) > 0.0f;
	// The army's own composition (improvement 5; WKB_MINOFF 8192 off): a mix by the General's
	// character that the counters lean on, so the army isn't all one class. Heavy infantry holds,
	// archers shoot, heavy cavalry breaks archers, light cavalry raids (more of it for a raider).
	// Plus v9.5's own script armies (Siege, Sapper, Guarding, Formation) exist only under v9.5; this
	// works on every version.
	float mix[5] = { 0.0f, 0.35f, 0.30f, 0.20f, 0.15f };
	mix[4] += 0.2f * (c.raiding - 0.5f);
	mix[1] += 0.1f * (1.0f - c.aggression) - 0.05f;
	{
		float sum = 0.0f;
		for (int k = 1; k < 5; k++) { mix[k] = std::max(0.05f, mix[k]); sum += mix[k]; }
		for (int k = 1; k < 5; k++) mix[k] /= sum;
	}
	const bool composing = !(MinOff() & 8192);
	// siege engines: a General who likes siege wants two (one otherwise) once enemy towers or a
	// palace are known, after minute 12, from the surplus only
	auto isSiege = [](const std::string& n) { return IsSiegeName(n); };
	int siegeOwn = 0, siegeWant = 0;
	if (composing && short_ == 0 && now > 720.0f) {
		bool hard = false;
		for (const auto& [key, v] : g.board.reports)
			if (key.rfind("intel.", 0) == 0 && v > 0.0 && (key.size() > 7 && (key.compare(key.size() - 7, 7, ".towers") == 0 || key.compare(key.size() - 7, 7, ".palace") == 0)))
				hard = true;
		if (hard) {
			siegeWant = c.siege > 0.5f ? 2 : 1;
			for (ServerGameObject* u : me->aiController.freeFighters(me->playerStartCameraPosition))
				siegeOwn += isSiege(u->blueprint->name);
			for (Mission& m : g.missions)
				for (ServerGameObject* u : g.alive(m))
					siegeOwn += isSiege(u->blueprint->name);
		}
	}
	// The specialists (WKB_MINOFF 524288 off): besides the line, supports (monks, priests, bishops,
	// inquisitors, priestesses, druids, succubi: 1 and one more per 8 soldiers, at most 8, from
	// minute 8), monsters (Behemoth, War Elephant, Horde Spewer, ...: one per 12 soldiers, at most 4,
	// from minute 12), engines (all of them, not only the named siege engines: the siege want, and
	// from minute 15 one per 20 soldiers, at most 4) and two spies from minute 10. Not while the
	// guard is short: soldiers first.
	const bool specialists = !(MinOff() & 524288);
	int have[ROLE_COUNT] = {}, want[ROLE_COUNT] = {};
	if (specialists) {
		std::vector<ServerGameObject*> todo{ me };
		while (!todo.empty()) {
			ServerGameObject* o = todo.back();
			todo.pop_back();
			for (const auto& [t, children] : o->children)
				for (CommonGameObject* ch : children) {
					auto* u = (ServerGameObject*)ch;
					if (u->deleted)
						continue;
					if (u->blueprint->bpClass == Tags::GAMEOBJCLASS_CHARACTER) {
						if (!(u->flags & ServerGameObject::fTerminated))
							have[RoleOf(gs, u->blueprint)]++;
					}
					else if (u->blueprint->bpClass != Tags::GAMEOBJCLASS_BUILDING)
						todo.push_back(u);
				}
		}
		const int soldiers = have[ROLE_LINE];
		if (short_ == 0) {
			want[ROLE_SUPPORT] = now > 480.0f ? std::min(8, 1 + soldiers / 8) : 0;
			want[ROLE_MONSTER] = now > 720.0f ? std::min(4, soldiers / 12) : 0;
			want[ROLE_ENGINE] = std::max(siegeWant, now > 900.0f ? std::min(4, soldiers / 20) : 0);
			want[ROLE_SABOTEUR] = now > 600.0f ? 2 : 0;
		}
		siegeOwn = have[ROLE_ENGINE];
	}
	int ordered = 0;
	for (const auto& [b, names] : AIUtil::Trainers(me, false)) {
		const std::string* best = nullptr;
		float bestScore = -1e9f;
		for (const std::string& name : names) {
			const GameObjBlueprint* type = gs->findBlueprint(Tags::GAMEOBJCLASS_CHARACTER, name);
			const int cls = AIUtil::TypeClass(type);
			const bool big = name.find("Abaddon") != std::string::npos || name.find("Michael") != std::string::npos || name.find("Archangel") != std::string::npos;
			const int role = specialists ? RoleOf(gs, type) : ROLE_NONE;
			const bool special = role != ROLE_NONE && role != ROLE_LINE;
			if (specialists && special) {
				if (have[role] >= want[role])
					continue;
			}
			const bool siege = !special && cls == 0 && isSiege(name);
			if (cls == 0 && !big && !special && !(siege && siegeOwn < siegeWant))
				continue;
			float score = 1.0f;
			if (total > 0.0f) {
				score = 0.0f;
				for (int k = 0; k < 5; k++)
					score += shares[k] / total * AIUtil::ClassFactor(cls, k);
			}
			if (composing && cls > 0 && ownTotal >= 6)
				// towards the mix: up to +-0.3 for a class far under or over its share
				score += std::clamp((mix[cls] - (float)own[cls] / ownTotal) * (0.5f + c.combined), -0.3f, 0.3f);
			else if (!composing && ownTotal >= 8 && c.combined > 0.5f && own[cls] > ownTotal * 0.4f)
				// combined arms: less of a class that is over 40% of the army already
				score -= 0.2f;
			if (big)
				score = summons ? 3.0f : -1e9f;
			if (siege)
				score = 2.0f;
			// (a specialist under its want before the line: the dearest of its role the building
			// offers, the best one, by the cost term below turned round)
			if (special)
				score = 2.5f + AIUtil::TypeCost(type) * 0.0004f;
			// a little for the cheaper: more soldiers for the food; near the population limit (80 %) the
			// dearer instead, each place counts (improvement 7)
			const bool full = !(MinOff() & 32768) && g.board.get("labour.full") >= 0.8;
			score += AIUtil::TypeCost(type) * (full ? 0.0002f : -0.0002f);
			if (score > bestScore) { bestScore = score; best = &name; }
		}
		if (best && AIUtil::Train(me, b, *best)) {
			ordered++;
			own[AIUtil::TypeClass(gs->findBlueprint(Tags::GAMEOBJCLASS_CHARACTER, *best))]++;
			if (specialists)
				have[RoleOf(gs, gs->findBlueprint(Tags::GAMEOBJCLASS_CHARACTER, *best))]++;
			siegeOwn += isSiege(*best);
			const std::string k = "war.trained." + *best;
			g.board.report(k, g.board.get(k) + 1);
			if (short_ > 0 ? ordered >= short_ : me->getItem(Tags::PDITEM_FOOD) <= spare)
				break;
		}
	}
	// a sapper at home builds an engine when the engines are short: the Rocket Launcher (with
	// Rocketry), the Trebuchet, the Battering Ram, in that order, whichever its commands allow, on
	// a site round the palace (their commands stamp down a foundation the sapper then builds)
	if (specialists && have[ROLE_ENGINE] < want[ROLE_ENGINE] + 1) {
		static const char* const ENGINES[] = { "Build Rocket Launcher", "Build Trebuchet", "Build Battering Ram" };
		const int build = gs->orders.names.getIndex("Build");
		ServerGameObject* palace = AIUtil::Palace(me);
		std::vector<ServerGameObject*> sappers;
		for (ServerGameObject* u : me->aiController.freeFighters(palace ? palace->position : me->playerStartCameraPosition))
			if (u->blueprint->name == "Sapper" && !u->orderConfig.getCurrentOrder())
				sappers.push_back(u);
		for (ServerGameObject* s : sappers) {
			if (!palace || build < 0)
				break;
			bool done = false;
			for (const char* name : ENGINES) {
				const int ci = gs->commands.names.getIndex(name);
				if (ci < 0)
					continue;
				const Command& cmd = gs->commands[ci];
				if (!cmd.stampdownObject || std::find(s->blueprint->offeredCommands.begin(), s->blueprint->offeredCommands.end(), &cmd) == s->blueprint->offeredCommands.end()
					|| !cmd.canBeExecuted(s))
					continue;
				if (!me->canAffordObject(cmd.stampdownObject))
					continue;
				Vector3 site(0, 0, 0);
				for (float r = 40.0f; r <= 120.0f && site.x == 0.0f; r += 20.0f)
					for (int k = 0; k < 16 && site.x == 0.0f; k++) {
						const float a = k * 6.2831853f / 16.0f + r * 0.05f;
						const Vector3 q(palace->position.x + r * std::sin(a), palace->position.y, palace->position.z + r * std::cos(a));
						if (StampdownPlan::getStampdownPlan(&srv, me, cmd.stampdownObject, q, Vector3(0, 0, 0)).status == StampdownPlan::Status::Valid)
							site = q;
					}
				if (site.x == 0.0f)
					continue;
				ServerGameObject* f = srv.stampdownObject(cmd.stampdownObject, me, site, Vector3(0, 0, 0), false, true);
				if (!f)
					continue;
				s->orderConfig.addOrder(&gs->orders[build], Tags::ORDERASSIGNMODE_FORGET_EVERYTHING_ELSE, f);
				have[ROLE_ENGINE]++;
				g.board.report("war.enginesLaid", g.board.get("war.enginesLaid") + 1);
				g.log(WAR, "sapper %u builds %s at (%.0f, %.0f)", s->id, cmd.stampdownObject->name.c_str(), site.x, site.z);
				done = true;
				break;
			}
			if (done)
				break;
		}
	}
	if (ordered)
		g.log(WAR, "%d fighters ordered, by counters (the main enemy's known army: heavy infantry %.0f%%, archers %.0f%%, heavy cavalry %.0f%%, light cavalry %.0f%%)",
			ordered, total > 0 ? shares[1] / total * 100 : 0.0f, total > 0 ? shares[2] / total * 100 : 0.0f, total > 0 ? shares[3] / total * 100 : 0.0f,
			total > 0 ? shares[4] / total * 100 : 0.0f);
	return ordered;
}

}
