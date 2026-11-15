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
#include "server.h"
#include "gameset/gameset.h"
#include "gameset/command.h"
#include "NNSearch.h"
#include <algorithm>
#include <cmath>

namespace Ministry {

namespace {
	enum Aim : uint8_t { HURT_FRIEND, ENEMY_FIGHTER, ENEMY_BUILDING, SELF_IN_FIGHT };
	struct Rule { const char* command; Aim aim; float reach; };
	const Rule RULES[] = {
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
				if (command[i] >= 0)
					for (const Command* c : bp->offeredCommands)
						if (c == &gs->commands[command[i]]) { list.push_back(i); break; }
			return offered[bp] = list;
		}
	} g_lookup;

	float Health(const ServerGameObject* o)
	{
		if (g_lookup.hp < 0 || g_lookup.hpCap < 0)
			return 1.0f;
		const float cap = o->getItem(g_lookup.hpCap);
		return cap > 0.0f ? o->getItem(g_lookup.hp) / cap : 1.0f;
	}
}

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
// most fighters an enemy has been seen with (at most 60). While it is short, Labour holds the
// peasants at their number (at least 25) and Tactics trains whatever is affordable.
int OpeningShort(Government& g)
{
	ServerGameObject* me = g.player();
	if (!me || (MinOff() & 1024))
		return 0;
	const float now = g.now();
	if (now < 240.0f || now > 1200.0f)
		return 0;
	std::map<uint32_t, int> seen;
	for (const auto& [id, s] : g.picture.sightings)
		if (s.fighter)
			seen[s.player]++;
	int most = 0;
	for (const auto& [p, n] : seen)
		most = std::max(most, n);
	const int want = std::min(60, std::max(12, (int)std::ceil(most * 1.2f)));
	const int have = me->aiController.fighters();
	g.board.report("opening.guard", want);
	return std::max(0, want - have);
}

int TrainByCounters(Government& g)
{
	ServerGameObject* me = g.player();
	Server& srv = g.server;
	if (!me)
		return 0;
	const GameSet* gs = srv.gameSet.get();
	const float now = g.now();
	// (the opening's guard short: whatever is affordable, Train checks)
	const int short_ = OpeningShort(g);
	if (short_ == 0 && (now < 120.0f || me->getItem(Tags::PDITEM_FOOD) <= 500.0f))
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
	int ordered = 0;
	for (const auto& [b, names] : AIUtil::Trainers(me, false)) {
		const std::string* best = nullptr;
		float bestScore = -1e9f;
		for (const std::string& name : names) {
			const GameObjBlueprint* type = gs->findBlueprint(Tags::GAMEOBJCLASS_CHARACTER, name);
			const int cls = AIUtil::TypeClass(type);
			const bool big = name.find("Abaddon") != std::string::npos || name.find("Michael") != std::string::npos || name.find("Archangel") != std::string::npos;
			if (cls == 0 && !big)
				continue;
			float score = 1.0f;
			if (total > 0.0f) {
				score = 0.0f;
				for (int k = 0; k < 5; k++)
					score += shares[k] / total * AIUtil::ClassFactor(cls, k);
			}
			// combined arms: less of a class that is over 40% of the army already
			if (ownTotal >= 8 && c.combined > 0.5f && own[cls] > ownTotal * 0.4f)
				score -= 0.2f;
			if (big)
				score = summons ? 3.0f : -1e9f;
			// a little for the cheaper: more soldiers for the food
			score -= AIUtil::TypeCost(type) * 0.0002f;
			if (score > bestScore) { bestScore = score; best = &name; }
		}
		if (best && AIUtil::Train(me, b, *best)) {
			ordered++;
			own[AIUtil::TypeClass(gs->findBlueprint(Tags::GAMEOBJCLASS_CHARACTER, *best))]++;
			const std::string k = "war.trained." + *best;
			g.board.report(k, g.board.get(k) + 1);
			if (short_ > 0 ? ordered >= short_ : me->getItem(Tags::PDITEM_FOOD) <= 500.0f)
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
