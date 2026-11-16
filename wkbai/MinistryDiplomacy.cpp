// wkbai - the ministry AI's Diplomacy: grudges and gratitude, and what they make of the
// statuses (the RE repo's docs/engine/ministry-ai.md, step 7; ai-ministries.md section 8)
//
// - The ledger (Government::ledger): every unit or building lost to another player (killed by its
//   unit, UnitHistory::terminated) adds its cost, a peasant half again for the work lost; every
//   tribute from it adds its amount. Both fade: a half-life of 10 minutes x (1.5 - friendliness).
//   A loss to a player trusted at the time counts double (a betrayal).
// - Trust = gratitude x (0.5 + greed) - grudge x (0.5 + pride), reported as "trust.<id>"; the
//   Strategist's target scores use it (a grudge draws attacks, trust holds them back unless the
//   General is treacherous).
// - Every 10 s, with diplomacy allowed (the level's "Disallow Diplomacy" 0): war on a deep grudge;
//   a peace offer to an enemy it has come to trust, an alliance to a trusted neutral; offers made
//   to it accepted when trusted enough, refused after a minute otherwise; a treacherous General
//   breaks an alliance with one it doesn't trust and outmatches. Each with the General's message
//   (SEND_CHAT_MESSAGE's displays: "Declare War", "Request Alliance", "AI Accepts Player", ...).

#include "MinistryDepartments.h"
#include "AIUtil.h"
#include "server.h"
#include "gameset/gameset.h"
#include <algorithm>
#include <cmath>

namespace Ministry {

Ledger& Government::ledgerOf(uint32_t other)
{
	Ledger& l = ledger[other];
	const float now = this->now();
	if (now > l.at) {
		const float halfLife = 600.0f * (1.5f - personality.friendliness);
		const double f = std::pow(0.5, (now - l.at) / std::max(60.0f, halfLife));
		l.lost *= f;
		l.received *= f;
		l.at = now;
	}
	return l;
}

float Government::trust(uint32_t other)
{
	const Ledger& l = ledgerOf(other);
	return (float)(l.received * (0.5 + personality.greed) - l.lost * (0.5 + personality.pride));
}

void Ministries::onLoss(ServerGameObject* victimPlayer, ServerGameObject* killerPlayer, const ServerGameObject* victim)
{
	if (!victimPlayer || !killerPlayer || victimPlayer == killerPlayer || !victim)
		return;
	auto it = governments.find(victimPlayer->id);
	if (it == governments.end())
		return;
	Government& g = *it->second;
	float value = std::max(20.0f, AIUtil::TypeCost(victim->blueprint));
	if (AIUtil::IsPeasant(victim))
		value *= 1.5f;
	const bool betrayal = g.trust(killerPlayer->id) > 0.0f;
	Ledger& l = g.ledgerOf(killerPlayer->id);
	l.lost += betrayal ? 2.0 * value : value;
	l.kills++;
	if (betrayal) {
		l.betrayals++;
		g.log(DIPLOMACY, "betrayed by player %u: %s %u lost to it while trusted", killerPlayer->id, victim->blueprint->name.c_str(), victim->id);
	}
}

void Ministries::onGift(ServerGameObject* from, ServerGameObject* to, float amount)
{
	if (!from || !to || from == to || !(amount > 0.0f))
		return;
	auto it = governments.find(to->id);
	if (it == governments.end())
		return;
	Government& g = *it->second;
	g.ledgerOf(from->id).received += amount;
	g.log(DIPLOMACY, "a tribute of %.0f from player %u: trust now %.0f", amount, from->id, g.trust(from->id));
}

namespace {
	struct Diplomacy : Department {
		uint8_t id() const override { return DIPLOMACY; }
		float period() const override { return 10.0f; }
		std::map<uint32_t, float> offerSeen; // an offer to me, by its maker: when it was first seen
		std::map<uint32_t, float> lastAct; // when I last acted towards the player

		void think(Government& g) override {
			Server& srv = g.server;
			ServerGameObject* me = g.player();
			ServerGameObject* level = (ServerGameObject*)srv.getLevel();
			if (!me || !level || !srv.gameSet)
				return;
			const GameSet* gs = srv.gameSet.get();
			if (!g.personality.read)
				ReadPersonality(g);
			const Personality& c = g.personality;
			const float now = g.now();
			const int allied = gs->diplomaticStatuses.names.getIndex("Allied"), neutral = gs->diplomaticStatuses.names.getIndex("Neutral"),
				enemy = gs->diplomaticStatuses.names.getIndex("Enemy");
			const int disallowItem = gs->items.names.getIndex("Disallow Diplomacy");
			const bool allowed = !(disallowItem >= 0 && level->getItem(disallowItem) > 0.0f) && allied >= 0 && neutral >= 0 && enemy >= 0;
			std::vector<ServerGameObject*> players;
			for (const auto& [type, children] : level->children)
				for (CommonGameObject* ch : children) {
					auto* p = (ServerGameObject*)ch;
					if (p != me && !p->deleted && p->blueprint->bpClass == Tags::GAMEOBJCLASS_PLAYER && p->blueprint->name != "Neutral Player"
						&& !(p->flags & ServerGameObject::fTerminated))
						players.push_back(p);
				}
			std::sort(players.begin(), players.end(), [](auto* a, auto* b) { return a->id < b->id; });
			// the lines: war below this, peace and alliance above those (a proud General holds grudges
			// longer, a friendly one makes peace sooner)
			const float warLine = -(300.0f + 700.0f * c.friendliness);
			const float peaceLine = 200.0f + 400.0f * c.pride;
			const float allyLine = 600.0f + 600.0f * c.pride;
			for (ServerGameObject* p : players) {
				const float t = g.trust(p->id);
				const Ledger& l = g.ledgerOf(p->id);
				g.board.report("trust." + std::to_string(p->id), t);
				g.board.report("grudge." + std::to_string(p->id), l.lost);
				g.board.report("gratitude." + std::to_string(p->id), l.received);
				if (!allowed)
					continue;
				const int status = srv.getDiplomaticStatus(me, p);
				// (a minute after an act; an offer again only after three)
				const float sinceAct = now - (lastAct.count(p->id) ? lastAct[p->id] : -1000.0f);
				const bool quiet = sinceAct < 60.0f;
				const bool mayOffer = sinceAct > 180.0f;
				// an offer made to me
				const int offered = srv.getDiplomaticOffer(p, me);
				if (offered >= 0) {
					if (!offerSeen.count(p->id))
						offerSeen[p->id] = now;
					const float need = offered == allied ? allyLine * 0.6f : peaceLine * 0.5f;
					if (t >= need) {
						srv.setDiplomaticStatus(me, p, offered, me);
						srv.sendChatDisplay(me, p, "AI Accepts Player");
						g.log(DIPLOMACY, "accepts player %u's offer of %s (trust %.0f)", p->id, gs->diplomaticStatuses.names.getString(offered).c_str(), t);
						offerSeen.erase(p->id);
						lastAct[p->id] = now;
						continue;
					}
					if (now - offerSeen[p->id] > 60.0f) {
						srv.makeDiplomaticOffer(p, me, -1); // (withdrawn: refused)
						srv.sendChatDisplay(me, p, "AI Refuses Player");
						g.log(DIPLOMACY, "refuses player %u's offer of %s (trust %.0f, needs %.0f)", p->id, gs->diplomaticStatuses.names.getString(offered).c_str(), t, need);
						offerSeen.erase(p->id);
						lastAct[p->id] = now;
						continue;
					}
				}
				else
					offerSeen.erase(p->id);
				if (quiet)
					continue;
				if (status != enemy && t < warLine) {
					srv.setDiplomaticStatus(me, p, enemy, me);
					srv.sendChatDisplay(me, p, status == allied ? "AI Breaks Alliance with Player" : "Declare War");
					g.log(DIPLOMACY, "declares war on player %u: a grudge of %.0f (%d lost to it, %d betrayals)", p->id, t, l.kills, l.betrayals);
					lastAct[p->id] = now;
				}
				else if (status == enemy && t > peaceLine && mayOffer && srv.getDiplomaticOffer(me, p) < 0) {
					srv.makeDiplomaticOffer(me, p, neutral);
					srv.sendChatDisplay(me, p, "Request Alliance");
					g.log(DIPLOMACY, "offers player %u peace (trust %.0f)", p->id, t);
					lastAct[p->id] = now;
				}
				else if (status == neutral && t > allyLine && mayOffer && srv.getDiplomaticOffer(me, p) < 0) {
					srv.makeDiplomaticOffer(me, p, allied);
					srv.sendChatDisplay(me, p, "Request Alliance");
					g.log(DIPLOMACY, "offers player %u an alliance (trust %.0f)", p->id, t);
					lastAct[p->id] = now;
				}
				else if (status == allied && c.treachery > 0.6f && t < 100.0f) {
					// treachery: an ally it doesn't trust and whose known army it outmatches
					const float theirs = (float)g.board.get("intel." + std::to_string(p->id) + ".power");
					const float mine = (float)g.board.get("war.availablePower");
					if (mine > 1.5f * theirs + 100.0f) {
						srv.setDiplomaticStatus(me, p, enemy, me);
						srv.sendChatDisplay(me, p, "AI Breaks Alliance with Player");
						g.log(DIPLOMACY, "breaks the alliance with player %u (trust %.0f, its known army %.0f against %.0f)", p->id, t, theirs, mine);
						lastAct[p->id] = now;
					}
				}
			}
		}

		void save(const Government& g, std::vector<std::string>& lines) const override {
			for (const auto& [id, at] : offerSeen)
				lines.push_back("offer " + std::to_string(id) + " " + std::to_string(at));
			for (const auto& [id, at] : lastAct)
				lines.push_back("act " + std::to_string(id) + " " + std::to_string(at));
			for (const auto& [id, l] : g.ledger) {
				char buf[160];
				snprintf(buf, sizeof(buf), "ledger %u %.17g %.17g %.9g %d %d", id, l.lost, l.received, l.at, l.betrayals, l.kills);
				lines.push_back(buf);
			}
		}
		void load(Government& g, const std::string& line) override {
			unsigned id = 0;
			float at = 0.0f;
			Ledger l;
			if (sscanf(line.c_str(), "offer %u %f", &id, &at) == 2)
				offerSeen[id] = at;
			else if (sscanf(line.c_str(), "act %u %f", &id, &at) == 2)
				lastAct[id] = at;
			else if (sscanf(line.c_str(), "ledger %u %lf %lf %f %d %d", &id, &l.lost, &l.received, &l.at, &l.betrayals, &l.kills) == 6)
				g.ledger[id] = l;
		}
	};
}

std::unique_ptr<Department> MakeDiplomacy() { return std::make_unique<Diplomacy>(); }

}
