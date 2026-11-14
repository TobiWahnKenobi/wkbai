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
	static const char* const names[KIND_COUNT] = { "scout", "raze", "palace", "raid", "kill", "decoy", "defend" };
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
		double lastSucceeded = 0.0; // the successful missions when the last attack was given

		void think(Government& g) override {
			if (MinOff() & 64) return;
			ServerGameObject* me = g.player();
			if (!me)
				return;
			if (!g.personality.read)
				ReadPersonality(g);
			const Personality& c = g.personality;
			const float now = g.now();
			Blackboard& b = g.board;
			// (the weights come from the character when the government is formed, SetWeightsFromCharacter;
			// a Lua AI may change them later: ai.setWeight)

			const int maxMissions = std::clamp(g.server.aiGenerals, 1, 8);
			int attacks = 0, given = 0;
			bool scouting = false;
			for (const Mission& m : g.missions) {
				if (m.kind == Mission::DEFEND)
					continue; // (War's own, not the Strategist's)
				given++;
				attacks += m.kind != Mission::SCOUT && m.kind != Mission::DECOY;
				scouting |= m.kind == Mission::SCOUT;
			}
			const std::vector<ServerGameObject*> enemies = EnemyPlayers(g.server, me);
			if (enemies.empty() || given >= maxMissions)
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

			// an attack: not too soon after the last one (the more impatient, the sooner; half as long
			// after a mission that did what it set out to do)
			const double succeeded = b.get("missions.raze.done") + b.get("missions.palace.done") + b.get("missions.raid.done");
			const float cooldown = 120.0f * (1.6f - c.impatience - 0.4f * c.aggression) * (succeeded > lastSucceeded ? 0.5f : 1.0f);
			// (one more attack for every 30 free soldiers: in a stage-1 game 114 waited at home while two
			// small missions were out)
			const int freeNow = (int)b.get("war.available");
			if (attacks >= 1 + (int)(c.aggression * 2.0f) + freeNow / 30 || now - lastAttack < cooldown)
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
				const float need = std::sqrt(margin) * std::max(cand.defence, 0.2f * known);
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
				// and a share of the free soldiers (35-70 % by aggression): the war layer sent 15 % of the
				// population limit a wave, and did four times the damage in the stage-1 games
				if (m.kind != Mission::RAID)
					want = std::max(want, (int)(pool * (0.35f + 0.35f * c.aggression)));
				// the palace is decisive: most of what is free
				if (m.kind == Mission::PALACE)
					want = std::max(want, (int)(pool * 0.8f));
				m.want = std::min(want, pool);
				m.goal = m.kind == Mission::RAID ? 8 : m.kind == Mission::RAZE ? 3 : 1;
				m.maxLoss = 0.5f + 0.3f * c.aggression;
				// a decoy first, the other way, if the General is sneaky and there are soldiers to spare
				if (c.sneakiness > 0.5f && m.kind != Mission::RAID && pool - m.want >= 6 && given + 2 <= maxMissions) {
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
				lastSucceeded = succeeded;
				return;
			}
			// none outmatched, but an army of a wave's size waiting does nothing: as the war layer, a wave
			// of 15 % of the population limit (at least 12) goes at the best target anyway
			const int popItem = g.server.gameSet->items.names.getIndex("Total Population Count");
			const float popLimit = popItem >= 0 ? me->getItem(popItem) : 200.0f;
			const int waveSize = std::max(12, (int)(popLimit * 0.15f));
			if (pool >= waveSize && !cands.empty()) {
				const Candidate& cand = cands.front();
				Mission m;
				m.kind = cand.kind == Mission::RAID ? Mission::RAZE : cand.kind;
				m.enemy = cand.s->player;
				m.target = cand.s->id;
				m.tx = cand.s->x;
				m.tz = cand.s->z;
				m.want = std::max(waveSize, (int)(pool * (0.5f + 0.3f * c.aggression)));
				m.want = std::min(m.want, pool);
				m.goal = m.kind == Mission::RAZE ? 3 : 1;
				m.maxLoss = 0.6f;
				char why[160];
				snprintf(why, sizeof(why), "a wave's worth waiting (%d free), its guard %.0f", pool, cand.defence);
				give(g, m, why);
				lastAttack = now;
				lastSucceeded = succeeded;
			}
		}

		void give(Government& g, Mission& m, const char* why) { GiveMission(g, m, why); }

		void save(const Government&, std::vector<std::string>& lines) const override {
			char buf[96];
			snprintf(buf, sizeof(buf), "lastAttack %.9g %.17g", lastAttack, lastSucceeded);
			lines.push_back(buf);
		}
		void load(Government&, const std::string& line) override {
			sscanf(line.c_str(), "lastAttack %f %lf", &lastAttack, &lastSucceeded);
		}
	};

	// ---- The defence (War's; the war layer's before) ----
	// The places: the palace (250 m), each town away from it (150 m), the peasants out working
	// (40 m, a sample of 30). At each with enemy soldiers, the defence mission there (one per place)
	// gets soldiers from home, the nearest first, until it is 1.5 times the enemies' strength; at
	// the palace any soldier, elsewhere only those within 500 m. Away from the palace, when no help
	// would win (under 0.8 times), the peasants there walk home instead. The soldiers sent leave
	// `home`. Returns whether the palace's attackers outmatch all that could be sent (the Generals
	// then recall the attacks).
	bool Defend(Government& g, ServerGameObject* me, std::vector<ServerGameObject*>& home)
	{
		Server& srv = g.server;
		ServerGameObject* palace = AIUtil::Palace(me);
		struct Place { Vector3 at; float radius; bool palace; };
		std::vector<Place> places;
		if (palace)
			places.push_back({ palace->position, 250.0f, true });
		std::vector<ServerGameObject*> towns;
		AIUtil::CollectTowns(me, towns);
		std::sort(towns.begin(), towns.end(), [](auto* a, auto* b) { return a->id < b->id; });
		for (ServerGameObject* t : towns)
			if (!palace || (t->position - palace->position).sqlen2xz() > 200.0f * 200.0f)
				places.push_back({ t->position, 150.0f, false });
		const std::vector<ServerGameObject*> peasants = AIUtil::Peasants(me);
		for (size_t i = 0; i < peasants.size(); i += std::max<size_t>(1, peasants.size() / 30))
			places.push_back({ peasants[i]->position, 40.0f, false });
		std::set<ServerGameObject*> seen;
		struct Threat { Place place; std::vector<ServerGameObject*> enemies; };
		std::vector<Threat> threats;
		for (const Place& p : places) {
			std::vector<ServerGameObject*> enemies;
			for (ServerGameObject* e : AIUtil::EnemiesNear(me, p.at, p.radius, false))
				if (seen.insert(e).second)
					enemies.push_back(e);
			if (!enemies.empty())
				threats.push_back({ p, std::move(enemies) });
		}
		// the palace first, then the biggest
		std::stable_sort(threats.begin(), threats.end(), [](const Threat& a, const Threat& b) {
			return a.place.palace != b.place.palace ? a.place.palace : a.enemies.size() > b.enemies.size(); });
		bool palaceUnmet = false;
		int sentNow = 0;
		for (Threat& t : threats) {
			int mi = -1;
			for (size_t i = 0; i < g.missions.size(); i++)
				if (g.missions[i].kind == Mission::DEFEND && std::hypot(g.missions[i].tx - t.place.at.x, g.missions[i].tz - t.place.at.z) < 100.0f) {
					mi = (int)i;
					break;
				}
			std::vector<ServerGameObject*> meeting = mi >= 0 ? g.alive(g.missions[mi]) : std::vector<ServerGameObject*>();
			if (AIUtil::Strength(meeting, t.enemies) >= 1.5f * AIUtil::Strength(t.enemies, meeting))
				continue;
			std::stable_sort(home.begin(), home.end(), [&](ServerGameObject* a, ServerGameObject* b) {
				return (a->position - t.place.at).sqlen2xz() < (b->position - t.place.at).sqlen2xz(); });
			std::vector<ServerGameObject*> sent = meeting;
			size_t used = 0;
			while (used < home.size() && AIUtil::Strength(sent, t.enemies) < 1.5f * AIUtil::Strength(t.enemies, sent)) {
				if (!t.place.palace && (home[used]->position - t.place.at).sqlen2xz() > 500.0f * 500.0f)
					break;
				sent.push_back(home[used++]);
			}
			const float ratio = AIUtil::Strength(sent, t.enemies) / std::max(1.0f, AIUtil::Strength(t.enemies, sent));
			if (!t.place.palace && ratio < 0.8f) {
				// no help that would win: the peasants there walk home
				std::vector<ServerGameObject*> flee;
				for (ServerGameObject* p : peasants)
					if ((p->position - t.place.at).sqlen2xz() < 50.0f * 50.0f)
						flee.push_back(p);
				AIUtil::SendHome(me, flee);
				g.board.report("war.peasantsFled", g.board.get("war.peasantsFled") + (double)flee.size());
				continue;
			}
			if (t.place.palace && ratio < 1.0f)
				palaceUnmet = true;
			if (!used)
				continue;
			if (mi < 0) {
				Mission m;
				m.kind = Mission::DEFEND;
				ServerGameObject* owner = t.enemies.front()->getPlayer();
				m.enemy = owner ? owner->id : 0;
				m.tx = t.place.at.x;
				m.tz = t.place.at.z;
				m.radius = t.place.radius;
				m.state = Mission::OUT;
				m.maxLoss = 1.0f; // (a defence doesn't give up: if it can't win, it isn't sent)
				char why[120];
				snprintf(why, sizeof(why), "%zu enemies at the %s", t.enemies.size(), t.place.palace ? "palace" : t.place.radius > 100.0f ? "town" : "peasants");
				GiveMission(g, m, why);
				mi = (int)g.missions.size() - 1;
			}
			Mission& m = g.missions[mi];
			const std::vector<ServerGameObject*> given(home.begin(), home.begin() + used);
			AIUtil::Hold(me, given, t.place.at, nullptr);
			for (ServerGameObject* u : given) {
				m.units.push_back(u->id);
				me->aiController.ministryUnits.insert(u->id);
				ServerGameObject* best = t.enemies.front();
				for (ServerGameObject* e : t.enemies)
					if ((e->position - u->position).sqlen2xz() < (best->position - u->position).sqlen2xz())
						best = e;
				AIUtil::Attack({ u }, best);
			}
			m.startUnits += (int)used;
			home.erase(home.begin(), home.begin() + used);
			sentNow += (int)used;
			g.log(WAR, "%zu soldiers to defend at (%.0f, %.0f) against %zu (mission %u, %.2f times their strength)", used, t.place.at.x, t.place.at.z,
				t.enemies.size(), m.id, ratio);
		}
		g.board.report("war.threats", (double)threats.size());
		g.board.report("war.defenders", g.board.get("war.defenders") + sentNow);
		// (the rest stays sorted by id, as the guard below takes it)
		std::sort(home.begin(), home.end(), [](auto* a, auto* b) { return a->id < b->id; });
		return palaceUnmet;
	}

	// ---- War ----
	struct War : Department {
		uint8_t id() const override { return WAR; }
		float period() const override { return 3.0f; }
		std::map<uint64_t, float> ready; // the abilities' cooldowns
		void save(const Government&, std::vector<std::string>& lines) const override {
			for (const auto& [key, until] : ready) {
				char buf[64];
				snprintf(buf, sizeof(buf), "ready %llu %.9g", (unsigned long long)key, until);
				lines.push_back(buf);
			}
		}
		void load(Government&, const std::string& line) override {
			unsigned long long key = 0;
			float until = 0.0f;
			if (sscanf(line.c_str(), "ready %llu %f", &key, &until) == 2)
				ready[key] = until;
		}

		void think(Government& g) override {
			ServerGameObject* me = g.player();
			if (!me)
				return;
			AIController& ai = me->aiController;
			ai.ministryWaves = !(MinOff() & 128);
			// (the budget "war.defence" 0 leaves the defence to the war layer again: a test's, or a script's)
			{
				auto it = g.board.budgets.find("war.defence");
				ai.ministryDefence = !(MinOff() & 256) && (it == g.board.budgets.end() || it->second != 0.0);
			}
			ai.ministryTraining = !(MinOff() & 32);
			// the surplus into fighters, by counters
			if (!(MinOff() & 16))
				TrainByCounters(g);
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
			// the defence first: soldiers from home to every threat, as many as it needs (they leave the pool)
			const bool palaceUnmet = ai.ministryDefence ? Defend(g, me, home) : false;
			g.board.report("war.palaceUnmet", palaceUnmet ? 1.0 : 0.0);
			// a home guard: more for a careful General, more with a threat at home, and at home at least
			// as much power as (1.2 - 0.4 x aggression) times the strongest enemy army Intelligence knows
			// (one against one, missions with most of the soldiers left the peasants to the enemy's waves:
			// the ministry side ended with half the characters of the same General without it)
			const float threat = (float)g.board.get("intel.threat.home");
			int guard = std::max(6, (int)std::ceil(home.size() * (0.15f + 0.25f * (1.0f - g.personality.aggression))));
			if (threat > 0.0f)
				guard = std::max(guard, (int)home.size() / 2);
			{
				float strongest = 0.0f;
				for (const auto& [key, v] : g.board.reports)
					if (key.rfind("intel.", 0) == 0 && key.size() > 6 && key.compare(key.size() - 6, 6, ".power") == 0)
						strongest = std::max(strongest, (float)v);
				// (0.6-1.0 x, and at most 60 % of the soldiers at home: with all of it kept, a General facing a
				// big army never attacked at all and lost to the war layer's waves)
				const float keep = strongest * (1.0f - 0.4f * g.personality.aggression);
				// the strongest stay home first
				std::vector<ServerGameObject*> byPower = home;
				std::stable_sort(byPower.begin(), byPower.end(), [](ServerGameObject* a, ServerGameObject* b) { return AIUtil::UnitPower(a) > AIUtil::UnitPower(b); });
				float kept = 0.0f;
				int n = 0;
				while (n < (int)byPower.size() && kept < keep)
					kept += AIUtil::UnitPower(byPower[n++]);
				guard = std::max(guard, std::min(n, (int)(home.size() * 0.6f)));
				g.board.report("war.guardPower", keep);
			}
			guard = std::min(guard, (int)home.size());
			const int available = std::max(0, (int)home.size() - guard);
			float power = 0.0f;
			for (int i = guard; i < (int)home.size(); i++)
				power += AIUtil::UnitPower(home[i]);
			g.board.report("war.home", (double)home.size());
			g.board.report("war.guard", guard);
			// the recall (the Generals'): a threat at home over 1.2 times what is there to meet it
			{
				float homePower = 0.0f;
				for (ServerGameObject* u : home)
					homePower += AIUtil::UnitPower(u);
				// (without War's defence: the threat map's threat over what is at home)
				const bool recall = ai.ministryDefence ? palaceUnmet : threat > 1.2f * homePower + 50.0f;
				if (recall && g.board.get("war.recall") == 0.0)
					g.log(WAR, "the missions come home: %s", palaceUnmet ? "the palace's attackers outmatch what is at home" : "a threat at home");
				g.board.report("war.recall", recall ? 1.0 : 0.0);
			}
			g.board.report("war.available", available);
			g.board.report("war.availablePower", power);
			g.board.report("war.inMissions", (double)ai.ministryUnits.size());
			if (thinks % 10 == 0) // (every 30 s)
				g.log(WAR, "%zu fighters free at home (guard %d, %d may go, power %.0f), %zu in missions; the war layer's defenders %zu",
					home.size(), guard, available, power, ai.ministryUnits.size(), ai.warDefences.size());
			// abilities at home, with enemies there
			if (threat > 0.0f)
				UseAbilities(g, home, ready);
			if (available <= 0)
				return;
			// the soldiers that may go: the guard stays (the first, by id: the oldest)
			std::vector<ServerGameObject*> free(home.begin() + guard, home.end());
			for (Mission& m : g.missions) {
				const std::vector<ServerGameObject*> have = g.alive(m);
				const int need = m.want - (int)have.size();
				// (reinforcements in groups of 4 at least, or all that is needed: one at a time, they
				// walked to the target alone and died there)
				if (m.kind == Mission::DEFEND || need <= 0 || free.empty() || m.state == Mission::RETURNING || (m.state == Mission::OUT && (int)free.size() < std::min(need, 4)))
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
		std::map<uint64_t, float> ready; // the abilities' cooldowns
		void save(const Government&, std::vector<std::string>& lines) const override {
			for (const auto& [key, until] : ready) {
				char buf[64];
				snprintf(buf, sizeof(buf), "ready %llu %.9g", (unsigned long long)key, until);
				lines.push_back(buf);
			}
		}
		void load(Government&, const std::string& line) override {
			unsigned long long key = 0;
			float until = 0.0f;
			if (sscanf(line.c_str(), "ready %llu %f", &key, &until) == 2)
				ready[key] = until;
		}

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
			// the units' abilities, where there is something to use them on
			UseAbilities(g, units, ready);
			// a defence: each fights the nearest enemy at the place; done 6 s after the last has gone
			if (m.kind == Mission::DEFEND) {
				const Vector3 place(m.tx, 0, m.tz);
				const std::vector<ServerGameObject*> enemies = AIUtil::EnemiesNear(me, place, m.radius + 50.0f, false);
				if (enemies.empty()) {
					if (m.clearSince < 0.0f)
						m.clearSince = now;
					return now - m.clearSince > 6.0f ? "done" : nullptr;
				}
				m.clearSince = -1.0f;
				// (only those inside the place are attacked, and a defender doesn't chase one out of it: the
				// test's raiders walked home as fast as the Spearmen, who followed them across the map)
				const std::vector<ServerGameObject*> inside = AIUtil::EnemiesNear(me, place, m.radius, false);
				std::vector<ServerGameObject*> back;
				for (ServerGameObject* u : units) {
					Order* o = u->orderConfig.getCurrentOrder();
					Task* t = o ? o->getCurrentTask() : nullptr;
					ServerGameObject* tg = t ? t->target.get() : nullptr;
					const bool tgInside = tg && !tg->deleted && (tg->position - place).sqlen2xz() <= (m.radius + 20.0f) * (m.radius + 20.0f);
					if ((u->position - place).sqlen2xz() > (m.radius + 60.0f) * (m.radius + 60.0f) && !tgInside) {
						back.push_back(u);
						continue;
					}
					if (tg && !tg->deleted && AIUtil::IsSoldier(tg) && tgInside)
						continue;
					if (inside.empty())
						continue;
					ServerGameObject* best = inside.front();
					for (ServerGameObject* e : inside)
						if ((e->position - u->position).sqlen2xz() < (best->position - u->position).sqlen2xz())
							best = e;
					AIUtil::Attack({ u }, best);
				}
				if (!back.empty())
					AIUtil::Hold(me, back, place, nullptr);
				return nullptr;
			}
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
			// (8 minutes out, 15 while its target is below half its strength: winning, slowly)
			const bool winning = target && target->blueprint->bpClass == Tags::GAMEOBJCLASS_BUILDING && [&] {
				const int si = srv.gameSet->items.names.getIndex("Structural Integrity"), cap = srv.gameSet->items.names.getIndex("Structural Integrity Capacity");
				return si >= 0 && cap >= 0 && target->getItem(cap) > 0.0f && target->getItem(si) < 0.5f * target->getItem(cap);
			}();
			if (now - m.stateSince > (winning ? 900.0f : 480.0f))
				return "too long";
			// home in danger: the threat there over what guards it, the attacks come back to defend it
			// (not a decoy, nor one at its last building)
			if (attacking && g.board.get("war.recall") > 0.0 && !winning)
				return "recalled";
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
					if (m.progress >= m.goal) {
						// winning: no one defends there and few lost: three more (the stage-1 games: a
						// mission that came home after three buildings left the war layer's waves to do
						// four times the damage)
						if (losses < 0.3f && AIUtil::EnemiesNear(me, at, 100.0f, true).empty() && m.goal < 30) {
							m.goal += 3;
							g.log(GENERALS, "mission %u is winning: %d more buildings", m.id, 3);
						}
						else
							return "done";
					}
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

void SetWeightsFromCharacter(Government& g)
{
	if (!g.personality.read)
		ReadPersonality(g);
	const Personality& c = g.personality;
	Blackboard& b = g.board;
	b.weights[WAR] = 0.5f + c.aggression;
	b.weights[GENERALS] = b.weights[WAR];
	b.weights[ECONOMY] = 1.5f - c.aggression;
	b.budgets["war.income"] = 0.25f + 0.45f * c.aggression;
}

uint32_t GiveMission(Government& g, Mission& m, const char* why)
{
	m.id = g.nextMission++;
	m.since = m.stateSince = g.now();
	g.log(STRATEGIST, "mission %u: %s %u of player %u at (%.0f, %.0f), %d units, goal %d: %s", m.id, Mission::KindName(m.kind), m.target, m.enemy,
		m.tx, m.tz, m.want, m.goal, why);
	g.board.report("missions.given", g.board.get("missions.given") + 1);
	g.missions.push_back(m);
	return m.id;
}

std::unique_ptr<Department> MakeStrategist() { return std::make_unique<Strategist>(); }
std::unique_ptr<Department> MakeWar() { return std::make_unique<War>(); }
std::unique_ptr<Department> MakeGenerals() { return std::make_unique<Generals>(); }

}
