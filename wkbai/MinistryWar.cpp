// wkbai - the ministry AI's Strategist, War department and Generals (Ministries.h, the RE repo's
// docs/engine/ministry-ai.md, step 5)
//
// - The Strategist (every 5 s) reads the General's character, sets the departments' weights and
//   War's share, and gives missions: a scout while no enemy building is known; then attacks by
//   what Intelligence knows, each a goal (raze buildings, destroy the palace, kill peasants), sized
//   to outmatch what guards the target on the threat map, with a decoy before a big one if the
//   General is sneaky.
// - War (every 3 s): the soldiers at home, a home guard kept back, the others given to the
//   missions that ask, those that counter the target's known army first. With the ministries the
//   war layer (AIController::warUpdate) keeps defending but sends no waves of its own.
// - The Generals (every second): a mission assembles, goes out (a wave in formations for an
//   attack; a walk for a scout or a decoy), carries on to the next building, chases peasants,
//   gives up when it has lost its budget and is outmatched where it stands, and comes home.

#include "MinistryDepartments.h"
#include "AIUtil.h"
#include "server.h"
#include "gameset/gameset.h"
#include "settings.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <set>

namespace Ministry {

const char* Mission::KindName(int k)
{
	static const char* const names[KIND_COUNT] = { "scout", "raze", "palace", "raid", "kill", "decoy" };
	return k >= 0 && k < KIND_COUNT ? names[k] : "?";
}

std::vector<ServerGameObject*> Government::alive(Mission& m) const
{
	std::vector<ServerGameObject*> list;
	for (uint32_t id : m.units) {
		ServerGameObject* u = server.findObject(id);
		if (u && !u->deleted && !(u->flags & ServerGameObject::fTerminated))
			list.push_back(u);
	}
	return list;
}

namespace {
	float Unit01(float v) { return std::clamp(v > 1.5f ? v / 100.0f : v, 0.0f, 1.0f); }

	// the kills of a unit, alive or dead (the unit history keeps the dead's)
	int KillsOf(Server& srv, uint32_t id)
	{
		if (ServerGameObject* u = srv.findObject(id); u && !u->deleted)
			return u->kills;
		const UnitHistory::Record* r = srv.unitHistory.find(id);
		return r ? r->kills : 0;
	}
	int KillsOf(Server& srv, const Mission& m)
	{
		int k = 0;
		for (uint32_t id : m.units)
			k += KillsOf(srv, id);
		return k;
	}

	std::vector<ServerGameObject*> EnemyPlayers(Server& srv, ServerGameObject* me)
	{
		std::vector<ServerGameObject*> list;
		ServerGameObject* level = (ServerGameObject*)srv.getLevel();
		if (!level || !me)
			return list;
		for (const auto& [type, children] : level->children)
			for (CommonGameObject* c : children) {
				auto* p = (ServerGameObject*)c;
				if (p != me && !p->deleted && p->blueprint->bpClass == Tags::GAMEOBJCLASS_PLAYER && p->blueprint->name != "Neutral Player"
					&& !(p->flags & ServerGameObject::fTerminated) && AIUtil::IsEnemy(me, p))
					list.push_back(p);
			}
		std::sort(list.begin(), list.end(), [](auto* a, auto* b) { return a->id < b->id; });
		return list;
	}

	Vector3 Centre(const std::vector<ServerGameObject*>& units)
	{
		Vector3 c(0, 0, 0);
		for (ServerGameObject* u : units)
			c += u->position;
		return units.empty() ? c : c / (float)units.size();
	}
}

void ReadPersonality(Government& g)
{
	ServerGameObject* p = g.player();
	if (!p || !g.server.gameSet)
		return;
	const GameSet* gs = g.server.gameSet.get();
	auto item = [&](const char* name, float otherwise) {
		const int i = gs->items.names.getIndex(name);
		const float v = i >= 0 ? p->getItem(i) : 0.0f;
		return i >= 0 && v != 0.0f ? Unit01(v) : otherwise;
	};
	Personality& c = g.personality;
	c.aggression = item("Aggression", 0.5f);
	c.impatience = item("Impatience", 0.5f);
	c.treachery = item("Treachery", 0.3f);
	c.sneakiness = item("Sneakiness", 0.3f);
	c.raiding = item("My Raiding Party Preference", 0.4f);
	c.siege = item("My Siege Weapon Preference", 0.4f);
	c.combined = item("My Combined Arms Preference", 0.5f);
	// an editor-made General's stats, where it has them
	auto stat = [&](const char* name) { const int i = gs->items.names.getIndex(name); return i >= 0 ? p->getItem(i) : 0.0f; };
	if (stat("I Am A Player Designed AI General") > 0.0f) {
		c.aggression = Unit01(stat("Economic - Military AI Stat"));
		c.siege = Unit01(stat("Raiding - Siege AI Stat"));
		c.raiding = 1.0f - c.siege;
		c.treachery = Unit01(stat("Treachery AI Stat"));
		c.pride = Unit01(stat("Pride AI Stat"));
		c.greed = Unit01(stat("Greediness AI Stat"));
		c.friendliness = Unit01(stat("Friendliness AI Stat"));
	}
	c.read = true;
	g.log(STRATEGIST, "character: aggression %.2f, impatience %.2f, treachery %.2f, sneakiness %.2f, raiding %.2f, siege %.2f",
		c.aggression, c.impatience, c.treachery, c.sneakiness, c.raiding, c.siege);
}

namespace {
	// ---- The Strategist ----
	struct Strategist : Department {
		uint8_t id() const override { return STRATEGIST; }
		float period() const override { return 5.0f; }
		float lastAttack = -1000.0f;

		void think(Government& g) override {
			ServerGameObject* me = g.player();
			if (!me)
				return;
			if (!g.personality.read)
				ReadPersonality(g);
			const Personality& c = g.personality;
			const float now = g.now();
			Blackboard& b = g.board;
			b.weights[WAR] = 0.5f + c.aggression;
			b.weights[GENERALS] = b.weights[WAR];
			b.weights[ECONOMY] = 1.5f - c.aggression;
			b.budgets["war.income"] = 0.25f + 0.45f * c.aggression;

			const int maxMissions = std::clamp(g.server.aiGenerals, 1, 8);
			int attacks = 0;
			bool scouting = false;
			for (const Mission& m : g.missions) {
				attacks += m.kind != Mission::SCOUT && m.kind != Mission::DECOY;
				scouting |= m.kind == Mission::SCOUT;
			}
			const std::vector<ServerGameObject*> enemies = EnemyPlayers(g.server, me);
			if (enemies.empty() || (int)g.missions.size() >= maxMissions)
				return;
			ServerGameObject* palace = AIUtil::Palace(me);
			const Vector3 home = palace ? palace->position : Vector3(0, 0, 0);

			// a scout while nothing of the enemies' buildings is known: towards the nearest enemy's start
			bool anyKnown = false;
			for (const auto& [id, s] : g.picture.sightings)
				anyKnown |= s.building;
			if (!anyKnown && !scouting && b.get("war.available") >= 1) {
				ServerGameObject* nearest = nullptr;
				float best = 0.0f;
				for (ServerGameObject* e : enemies) {
					const Vector3 at = e->playerStartCameraPosition;
					const float d = (at - home).sqlen2xz();
					if (!nearest || d < best) { nearest = e; best = d; }
				}
				if (nearest) {
					Mission m;
					m.kind = Mission::SCOUT;
					m.enemy = nearest->id;
					m.tx = nearest->playerStartCameraPosition.x;
					m.tz = nearest->playerStartCameraPosition.z;
					m.want = 1;
					m.maxLoss = 1.0f;
					give(g, m, "nothing of the enemies' buildings known");
				}
				return;
			}

			// an attack: not too soon after the last one (the more impatient, the sooner)
			const float cooldown = 120.0f * (1.6f - c.impatience - 0.4f * c.aggression);
			if (attacks >= 1 + (int)(c.aggression * 2.0f) || now - lastAttack < cooldown)
				return;
			const float poolPower = (float)b.get("war.availablePower");
			const int pool = (int)b.get("war.available");
			if (pool < 6)
				return;
			// candidate targets: every known building (and peasant groups for raids), scored by what it
			// is worth against what guards it, nearer ones first
			struct Candidate { const Sighting* s; uint8_t kind; float defence, score; };
			std::vector<Candidate> cands;
			for (const auto& [id, s] : g.picture.sightings) {
				if (!s.building && !s.peasant)
					continue;
				const float defence = g.picture.threatNear(s.x, s.z, 80.0f);
				const float dist = std::hypot(s.x - home.x, s.z - home.z);
				const float trust = (float)b.get("trust." + std::to_string(s.player), 0.0); // (Diplomacy's: below 0 a grudge)
				const float grudge = 1.0f + std::max(0.0f, -trust) / 500.0f;
				const float peace = 1.0f / (1.0f + std::max(0.0f, trust) / 500.0f * (1.0f - c.treachery));
				float worth;
				uint8_t kind;
				if (s.palace) { worth = 3.0f + 2.0f * c.siege; kind = Mission::PALACE; }
				else if (s.building) { worth = 1.0f; kind = Mission::RAZE; }
				else { worth = 0.6f + 1.4f * c.raiding; kind = Mission::RAID; }
				const float score = worth * grudge * peace / ((defence + 30.0f) * (1.0f + dist / 600.0f));
				cands.push_back({ &s, kind, defence, score });
			}
			std::sort(cands.begin(), cands.end(), [](const Candidate& a, const Candidate& b) {
				return a.score != b.score ? a.score > b.score : a.s->id < b.s->id; });
			// the first that the pool outmatches (the more aggressive, the smaller the margin)
			const float margin = 1.6f - 0.6f * c.aggression;
			for (const Candidate& cand : cands) {
				// what guards it, and a share of the enemy's whole known army (which can come)
				const float known = (float)b.get("intel." + std::to_string(cand.s->player) + ".power");
				const float need = std::sqrt(margin) * std::max(cand.defence, 0.35f * known);
				if (poolPower < need)
					continue;
				Mission m;
				m.kind = cand.kind;
				m.enemy = cand.s->player;
				m.target = cand.s->id;
				m.tx = cand.s->x;
				m.tz = cand.s->z;
				// enough to outmatch it, a raid small and quick
				const float avg = poolPower / std::max(1, pool);
				int want = (int)std::ceil((need + 40.0f) / std::max(avg, 1.0f));
				want = std::max(want, m.kind == Mission::RAID ? 6 : 10);
				m.want = std::min(want, pool);
				m.goal = m.kind == Mission::RAID ? 8 : m.kind == Mission::RAZE ? 3 : 1;
				m.maxLoss = 0.5f + 0.3f * c.aggression;
				// a decoy first, the other way, if the General is sneaky and there are soldiers to spare
				if (c.sneakiness > 0.5f && m.kind != Mission::RAID && pool - m.want >= 6 && (int)g.missions.size() + 2 <= maxMissions) {
					for (const Candidate& other : cands)
						if (other.s->building && other.s->id != cand.s->id && std::hypot(other.s->x - cand.s->x, other.s->z - cand.s->z) > 200.0f) {
							Mission d;
							d.kind = Mission::DECOY;
							d.enemy = other.s->player;
							d.target = other.s->id;
							d.tx = other.s->x;
							d.tz = other.s->z;
							d.want = std::min(6, pool - m.want);
							d.maxLoss = 1.0f; // (it may die doing it)
							d.duration = 60.0f;
							give(g, d, "to draw the defenders away");
							break;
						}
				}
				char why[160];
				snprintf(why, sizeof(why), "it guards with %.0f, we have %.0f at home (margin %.2f)", cand.defence, poolPower, margin);
				give(g, m, why);
				lastAttack = now;
				return;
			}
		}

		void give(Government& g, Mission& m, const char* why) {
			m.id = g.nextMission++;
			m.since = m.stateSince = g.now();
			g.log(STRATEGIST, "mission %u: %s %u of player %u at (%.0f, %.0f), %d units, goal %d: %s", m.id, Mission::KindName(m.kind), m.target, m.enemy,
				m.tx, m.tz, m.want, m.goal, why);
			g.board.report("missions.given", g.board.get("missions.given") + 1);
			g.missions.push_back(m);
		}

		void save(const Government&, std::vector<std::string>& lines) const override {
			char buf[40];
			snprintf(buf, sizeof(buf), "lastAttack %.9g", lastAttack);
			lines.push_back(buf);
		}
		void load(Government&, const std::string& line) override {
			sscanf(line.c_str(), "lastAttack %f", &lastAttack);
		}
	};

	// ---- War ----
	struct War : Department {
		uint8_t id() const override { return WAR; }
		float period() const override { return 3.0f; }

		void think(Government& g) override {
			ServerGameObject* me = g.player();
			if (!me)
				return;
			AIController& ai = me->aiController;
			ai.ministryWaves = true;
			// the missions' units, for the war layer to leave alone
			ai.ministryUnits.clear();
			for (Mission& m : g.missions)
				for (ServerGameObject* u : g.alive(m))
					ai.ministryUnits.insert(u->id);
			// the fighters free at home: the war layer's pool (the defensive army, the scripts' idle
			// or not-fighting attackers near the palace), not its own defenders and waves nor the missions'
			ServerGameObject* myPalace = AIUtil::Palace(me);
			std::vector<ServerGameObject*> home = ai.freeFighters(myPalace ? myPalace->position : me->playerStartCameraPosition);
			std::sort(home.begin(), home.end(), [](auto* a, auto* b) { return a->id < b->id; });
			// a home guard: more for a careful General, more with a threat at home
			const float threat = (float)g.board.get("intel.threat.home");
			int guard = std::max(6, (int)std::ceil(home.size() * (0.15f + 0.25f * (1.0f - g.personality.aggression))));
			if (threat > 0.0f)
				guard = std::max(guard, (int)home.size() / 2);
			const int available = std::max(0, (int)home.size() - guard);
			float power = 0.0f;
			for (int i = guard; i < (int)home.size(); i++)
				power += AIUtil::UnitPower(home[i]);
			g.board.report("war.home", (double)home.size());
			g.board.report("war.guard", guard);
			g.board.report("war.available", available);
			g.board.report("war.availablePower", power);
			g.board.report("war.inMissions", (double)ai.ministryUnits.size());
			if (available <= 0)
				return;
			// the soldiers that may go: the guard stays (the first, by id: the oldest)
			std::vector<ServerGameObject*> free(home.begin() + guard, home.end());
			for (Mission& m : g.missions) {
				const std::vector<ServerGameObject*> have = g.alive(m);
				const int need = m.want - (int)have.size();
				if (need <= 0 || free.empty() || m.state == Mission::RETURNING)
					continue;
				// those that counter the target's known army first (its classes' shares), then the strongest
				float shares[5] = {};
				float total = 0.0f;
				for (const Sighting* s : g.picture.of(m.enemy))
					if (s->fighter) { shares[s->cls] += s->power; total += s->power; }
				auto score = [&](ServerGameObject* u) {
					const int cls = AIUtil::ClassOf(u);
					float f = 1.0f;
					if (total > 0.0f) {
						f = 0.0f;
						for (int k = 0; k < 5; k++)
							f += shares[k] / total * AIUtil::ClassFactor(cls, k);
					}
					// a scout: the fastest (light cavalry first)
					if (m.kind == Mission::SCOUT)
						return (cls == 4 ? 1000.0f : cls == 3 ? 500.0f : 0.0f) - AIUtil::UnitPower(u);
					return f * 100.0f + AIUtil::UnitPower(u) * 0.01f;
				};
				std::stable_sort(free.begin(), free.end(), [&](ServerGameObject* a, ServerGameObject* b) {
					const float sa = score(a), sb = score(b);
					return sa != sb ? sa > sb : a->id < b->id; });
				std::vector<ServerGameObject*> given(free.begin(), free.begin() + std::min<size_t>(need, free.size()));
				free.erase(free.begin(), free.begin() + given.size());
				for (ServerGameObject* u : given) {
					m.units.push_back(u->id);
					ai.ministryUnits.insert(u->id);
				}
				// assembling: they gather on the way out; out already (reinforcements): to the mission
				ServerGameObject* palace = AIUtil::Palace(me);
				const Vector3 from = palace ? palace->position : Centre(given);
				Vector3 to(m.tx, 0, m.tz);
				if (m.state == Mission::ASSEMBLING) {
					Vector3 dir = to - from;
					dir.y = 0;
					const float len = dir.len2xz();
					const Vector3 rally = len > 1.0f ? from + dir * (std::min(60.0f, len * 0.5f) / len) : from;
					AIUtil::Hold(me, given, rally, nullptr);
				}
				else if (ServerGameObject* t = g.server.findObject(m.target); t && !t->deleted)
					AIUtil::Attack(given, t);
				else
					AIUtil::Hold(me, given, Centre(g.alive(m)), nullptr);
				g.log(WAR, "%zu soldiers to mission %u (%s)", given.size(), m.id, Mission::KindName(m.kind));
			}
		}
	};

	// ---- The Generals ----
	struct Generals : Department {
		uint8_t id() const override { return GENERALS; }
		float period() const override { return 1.0f; }

		void think(Government& g) override {
			ServerGameObject* me = g.player();
			if (!me)
				return;
			Server& srv = g.server;
			const float now = g.now();
			for (size_t i = 0; i < g.missions.size();) {
				Mission& m = g.missions[i];
				const char* end = command(g, me, m, now);
				if (!end) {
					i++;
					continue;
				}
				const std::vector<ServerGameObject*> units = g.alive(m);
				AIUtil::SendHome(me, units);
				for (uint32_t id : m.units)
					me->aiController.ministryUnits.erase(id);
				const int kills = KillsOf(srv, m) - m.kills0;
				g.log(GENERALS, "mission %u (%s) ends: %s; %zu of %d back, %d kills, %d buildings razed, %.0f s", m.id, Mission::KindName(m.kind), end,
					units.size(), std::max(m.startUnits, (int)m.units.size()), kills, m.progress, now - m.since);
				const std::string k = std::string("missions.") + Mission::KindName(m.kind) + "." + (strcmp(end, "done") == 0 ? "done" : "failed");
				g.board.report(k, g.board.get(k) + 1);
				g.missions.erase(g.missions.begin() + i);
			}
		}

		// a mission's step; its end ("done", "lost", "gave up", "too long") or null while it goes on
		const char* command(Government& g, ServerGameObject* me, Mission& m, float now) {
			Server& srv = g.server;
			std::vector<ServerGameObject*> units = g.alive(m);
			ServerGameObject* target = m.target ? srv.findObject(m.target) : nullptr;
			if (target && (target->deleted || (target->flags & ServerGameObject::fTerminated)))
				target = nullptr;
			if (m.state == Mission::ASSEMBLING) {
				// all there; or after a minute 60% of them, after two half; after three it gives up
				const int have = (int)units.size();
				const bool ready = have >= m.want || (now - m.since > 60.0f && have >= std::max(1, m.want * 6 / 10))
					|| (now - m.since > 120.0f && have >= std::max(1, m.want / 2));
				if (now - m.since > 180.0f && !ready)
					return "gave up";
				if (!ready)
					return nullptr;
				m.state = Mission::OUT;
				m.stateSince = now;
				m.startUnits = (int)units.size();
				m.kills0 = KillsOf(srv, m);
				const bool attack = m.kind == Mission::PALACE || m.kind == Mission::RAZE;
				if (attack && target && AIUtil::Launch(me, units, target))
					g.log(GENERALS, "mission %u out: a wave of %zu at %s %u", m.id, units.size(), target->blueprint->name.c_str(), target->id);
				else {
					AIUtil::Hold(me, units, Vector3(m.tx, 0, m.tz), nullptr);
					g.log(GENERALS, "mission %u out: %zu walking to (%.0f, %.0f)", m.id, units.size(), m.tx, m.tz);
				}
				return nullptr;
			}
			if (units.empty())
				return "lost";
			const Vector3 at = Centre(units);
			const int kills = KillsOf(srv, m) - m.kills0;
			const float losses = m.startUnits > 0 ? 1.0f - (float)units.size() / m.startUnits : 0.0f;
			const bool attacking = m.kind == Mission::PALACE || m.kind == Mission::RAZE || m.kind == Mission::RAID || m.kind == Mission::KILL;
			// giving up: the budget lost and outmatched where it stands
			if (losses > m.maxLoss && m.maxLoss < 1.0f) {
				const std::vector<ServerGameObject*> enemies = AIUtil::EnemiesNear(me, at, 70.0f, true);
				if (AIUtil::Strength(units, enemies) < 0.5f * AIUtil::Strength(enemies, units))
					return "gave up";
			}
			if (now - m.stateSince > 480.0f)
				return "too long";
			// reinforcements once, for an attack that has lost a third and isn't winning
			if (!m.reinforced && attacking && m.kind != Mission::RAID && losses > 0.35f) {
				m.reinforced = true;
				m.want = (int)units.size() + std::max(4, m.startUnits / 2);
				g.log(GENERALS, "mission %u asks for reinforcements: %d more", m.id, m.want - (int)units.size());
			}
			switch (m.kind) {
			case Mission::SCOUT: {
				ServerGameObject* e = srv.findObject(m.enemy);
				const bool palaceKnown = g.board.get("intel." + std::to_string(m.enemy) + ".palace") > 0.0;
				bool buildings = false;
				for (const Sighting* s : g.picture.of(m.enemy))
					buildings |= s->building;
				if (palaceKnown || buildings)
					return "done";
				if ((at - Vector3(m.tx, 0, m.tz)).len2xz() < 40.0f) {
					// nothing at its start: on to another enemy's start, else home
					(void)e;
					return "done";
				}
				if (!units.front()->orderConfig.getCurrentOrder())
					AIUtil::Hold(me, units, Vector3(m.tx, 0, m.tz), nullptr);
				return nullptr;
			}
			case Mission::DECOY:
				if (now - m.stateSince > m.duration)
					return "done";
				// it stands its ground there, fighting what comes
				for (ServerGameObject* u : units)
					if (!u->orderConfig.getCurrentOrder()) {
						std::vector<ServerGameObject*> near = AIUtil::EnemiesNear(me, u->position, 40.0f, false);
						if (!near.empty())
							AIUtil::Attack({ u }, near.front());
						else if (target)
							AIUtil::Attack({ u }, target);
					}
				return nullptr;
			case Mission::PALACE:
				if (!target)
					return "done";
				break;
			case Mission::RAZE:
				if (!target) {
					m.progress++;
					if (m.progress >= m.goal)
						return "done";
					// on to the next building near where the last one stood
					std::vector<ServerGameObject*> next = AIUtil::EnemyBuildingsNear(me, Vector3(m.tx, 0, m.tz), 150.0f);
					if (next.empty())
						return "done";
					target = next.front();
					m.target = target->id;
					m.tx = target->position.x;
					m.tz = target->position.z;
					AIUtil::Attack(units, target);
					g.log(GENERALS, "mission %u: on to %s %u (%d of %d razed)", m.id, target->blueprint->name.c_str(), target->id, m.progress, m.goal);
				}
				break;
			case Mission::RAID: case Mission::KILL: {
				if (kills >= m.goal)
					return "done";
				std::vector<ServerGameObject*> prey = m.kind == Mission::RAID ? AIUtil::EnemyPeasantsNear(me, at, 120.0f) : AIUtil::EnemiesNear(me, at, 120.0f, false);
				if (prey.empty()) {
					if ((at - Vector3(m.tx, 0, m.tz)).len2xz() < 30.0f && now - m.stateSince > 30.0f) {
						// none here: the nearest peasants Intelligence knows of that player, else home
						const Sighting* best = nullptr;
						float bd = 0.0f;
						for (const Sighting* s : g.picture.of(m.enemy))
							if ((m.kind == Mission::RAID ? s->peasant : s->fighter)) {
								const float d = std::hypot(s->x - at.x, s->z - at.z);
								if (d > 40.0f && (!best || d < bd)) { best = s; bd = d; }
							}
						if (!best)
							return "done";
						m.tx = best->x;
						m.tz = best->z;
						AIUtil::Hold(me, units, Vector3(m.tx, 0, m.tz), nullptr);
					}
					else if (!units.front()->orderConfig.getCurrentOrder())
						AIUtil::Hold(me, units, Vector3(m.tx, 0, m.tz), nullptr);
					return nullptr;
				}
				// each idle raider takes the nearest
				for (ServerGameObject* u : units)
					if (!u->orderConfig.getCurrentOrder()) {
						ServerGameObject* best = prey.front();
						for (ServerGameObject* p : prey)
							if ((p->position - u->position).sqlen2xz() < (best->position - u->position).sqlen2xz())
								best = p;
						AIUtil::Attack({ u }, best);
					}
				return nullptr;
			}
			default:
				break;
			}
			// attacking a building: idle units fight what is near, else the target
			if (target)
				for (ServerGameObject* u : units)
					if (!u->orderConfig.getCurrentOrder()) {
						std::vector<ServerGameObject*> near = AIUtil::EnemiesNear(me, u->position, 40.0f, false);
						AIUtil::Attack({ u }, near.empty() ? target : near.front());
					}
			return nullptr;
		}
	};
}

std::unique_ptr<Department> MakeStrategist() { return std::make_unique<Strategist>(); }
std::unique_ptr<Department> MakeWar() { return std::make_unique<War>(); }
std::unique_ptr<Department> MakeGenerals() { return std::make_unique<Generals>(); }

}
