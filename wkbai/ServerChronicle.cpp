// wkbai - the timeline and the statistics kept by the server (the RE repo's
// docs/engine/unit-history.md, section 1)

#include "ServerChronicle.h"
#include "server.h"
#include "network.h"
#include "tags.h"
#include "gameset/gameset.h"
#include <algorithm>
#include <cstring>

namespace {
constexpr float AREA = 100.0f; // m: a fight's area around where it was at its last scan
}

GameChronicle::Event* ServerChronicle::eventById(uint32_t id)
{
	for (auto& e : chron.events)
		if (e.id == id)
			return &e;
	return nullptr;
}

GameChronicle::Event* ServerChronicle::fightAt(const Vector3& at, float now, float within)
{
	const MapRegions::Region* region = chron.regions.at(at);
	GameChronicle::Event* best = nullptr;
	for (auto& e : chron.events) {
		if (!e.fight() || now - e.endTime > within)
			continue;
		const bool close = (e.lastPosition - at).sqlen2xz() < AREA * AREA;
		const bool sameRegion = region && !e.place.empty() && region->name == e.place && (e.lastPosition - at).sqlen2xz() < 3.0f * AREA * 3.0f * AREA;
		if (close || sameRegion)
			best = &e; // the latest
	}
	return best;
}

void ServerChronicle::noteHit(uint32_t attacker, uint32_t victim, const Vector3& at)
{
	if (hits.size() < 50000)
		hits.push_back({ attacker, victim, at });
}

void ServerChronicle::noteKill(uint32_t killer, const Vector3& at)
{
	if (kills.size() < 20000)
		kills.push_back({ killer, at, 0.0f });
}

std::string ServerChronicle::label(Server& server, uint32_t unit, bool withPlayer)
{
	const UnitHistory::Record* rec = server.unitHistory.find(unit);
	if (rec && !rec->displayName().empty())
		return rec->displayName();
	std::string type = rec ? rec->type : std::string();
	uint32_t player = rec ? rec->player : 0;
	if (ServerGameObject* obj = server.findObject(unit)) {
		type = obj->blueprint->name;
		if (ServerGameObject* p = obj->getPlayer())
			player = p->id;
	}
	auto it = chron.playerNames.find(player);
	if (withPlayer && it != chron.playerNames.end() && !it->second.empty() && !type.empty())
		return it->second + "'s " + type;
	return type;
}

void ServerChronicle::addParticipant(Server& server, GameChronicle::Event& e, uint32_t unit, uint8_t flag, int kill, float now)
{
	const UnitHistory::Record* rec = server.unitHistory.find(unit);
	if (!rec)
		return;
	Participant& p = participants[e.id][unit];
	const bool isNew = p.flags == 0 && p.kills == 0;
	p.player = rec->player;
	if (ServerGameObject* obj = server.findObject(unit))
		if (ServerGameObject* pl = obj->getPlayer())
			p.player = pl->id;
	p.flags |= flag;
	if (kill) {
		p.kills += kill;
		p.reached = now;
	}
	if (isNew) {
		eventsOfUnit[unit].insert(e.id);
		server.unitHistory.records[unit].participated = true;
	}
	dirty.insert(e.id);
}

void ServerChronicle::makeMentions(Server& server, GameChronicle::Event& e)
{
	e.mentions.clear();
	auto pit = participants.find(e.id);
	if (pit != participants.end()) {
		// each side's unit with the most kills here (equal: the one that got there first)
		std::map<uint32_t, std::pair<uint32_t, const Participant*>> top;
		for (const auto& [unit, p] : pit->second) {
			if (p.kills <= 0)
				continue;
			auto& t = top[p.player];
			if (!t.second || p.kills > t.second->kills || (p.kills == t.second->kills && p.reached < t.second->reached))
				t = { unit, &p };
		}
		for (const auto& [player, t] : top) {
			GameChronicle::Mention m;
			m.kind = GameChronicle::MENTION_TOP_KILLER;
			m.unit = t.first;
			m.player = player;
			m.kills = t.second->kills;
			const UnitHistory::Record* rec = server.unitHistory.find(t.first);
			m.dead = rec && rec->death >= 0.0f;
			m.name = label(server, t.first, false); // (the timeline puts the player's name before it)
			e.mentions.push_back(std::move(m));
		}
		// the heroes who fought here, with their kills now (heroes since too)
		for (const auto& [unit, p] : pit->second) {
			const UnitHistory::Record* rec = server.unitHistory.find(unit);
			if (!rec || !rec->hero)
				continue;
			GameChronicle::Mention m;
			m.kind = GameChronicle::MENTION_PRESENT;
			m.unit = unit;
			m.player = rec->player;
			ServerGameObject* obj = server.findObject(unit);
			m.kills = rec->death >= 0.0f || !obj ? rec->kills : obj->kills;
			m.dead = rec->death >= 0.0f;
			m.name = rec->displayName();
			e.mentions.push_back(std::move(m));
		}
	}
	if (auto fit = fallen.find(e.id); fit != fallen.end())
		e.mentions.insert(e.mentions.end(), fit->second.begin(), fit->second.end());
}

void ServerChronicle::heroFell(Server& server, uint32_t hero, const Vector3& at, uint32_t killer)
{
	const UnitHistory::Record* rec = server.unitHistory.find(hero);
	if (!rec)
		return;
	const float now = (float)server.timeManager.currentTime;
	GameChronicle::Mention m;
	m.kind = GameChronicle::MENTION_FALLEN;
	m.unit = hero;
	m.player = rec->player;
	m.kills = rec->kills;
	m.dead = true;
	m.name = rec->displayName();
	if (killer)
		m.extra = "slain by " + label(server, killer);
	else
		m.extra = rec->cause.empty() ? std::string("fell") : "died of " + rec->cause;
	// in a fight going on there (or ended less than 30 s ago): noted in it
	if (GameChronicle::Event* e = fightAt(at, now, 30.0f)) {
		fallen[e->id].push_back(m);
		addParticipant(server, *e, hero, 2, 0, now);
		dirty.insert(e->id);
		return;
	}
	// else an event of its own: "The fall of Sigurd the Valiant at Ergor"
	GameChronicle::Event e{ GameChronicle::Kind::Fallen, now, now, at, { rec->player }, 0, rec->displayName(), std::string() };
	if (const MapRegions::Region* r = chron.regions.at(at))
		e.place = r->name;
	e.lastPosition = at;
	e.seen = true;
	e.id = chron.nextId++;
	fallen[e.id].push_back(m);
	if (killer)
		if (ServerGameObject* k = server.findObject(killer))
			if (ServerGameObject* kp = k->getPlayer())
				e.players.insert(kp->id);
	chron.events.push_back(std::move(e));
	dirty.insert(chron.events.back().id);
}

void ServerChronicle::unitChanged(uint32_t unit)
{
	auto it = eventsOfUnit.find(unit);
	if (it != eventsOfUnit.end())
		dirty.insert(it->second.begin(), it->second.end());
}

void ServerChronicle::update(Server& server)
{
	if (!server.getLevel() || !server.gameSet)
		return;
	if (level != server.getLevel()) {
		// a new game (a test making another server keeps the same object: no matter)
		if (level)
			*this = ServerChronicle();
		level = server.getLevel();
	}
	const float now = (float)server.timeManager.currentTime;
	if (!started) {
		started = true;
		sendLine(server, "RESET", nullptr);
	}
	// the map regions: the host's (the same names on every client), else worked out here
	if (!chron.regions.ready()) {
		if (!server.hostRegionsText.empty())
			chron.regions.fromText(server.hostRegionsText);
		else if (now > 5.0f && server.terrain)
			chron.regions.build(server.terrain, *server.gameSet, server.getLevel());
	}
	if (chron.step(&server, 0, nullptr, now)) {
		// the hits since the last scan: who fought in which fight going on
		std::set<uint32_t> active;
		for (size_t i : chron.touched)
			if (i < chron.events.size() && chron.events[i].fight())
				active.insert(chron.events[i].id);
		for (const Hit& h : hits) {
			GameChronicle::Event* e = fightAt(h.at, now, 0.01f);
			if (!e || !active.count(e->id))
				continue;
			addParticipant(server, *e, h.attacker, 1, 0, now);
			addParticipant(server, *e, h.victim, 2, 0, now);
		}
		hits.clear();
		for (const KillNote& k : kills)
			if (GameChronicle::Event* e = fightAt(k.at, now, 30.0f))
				addParticipant(server, *e, k.killer, 1, 1, now);
		kills.clear();
		for (size_t i : chron.touched)
			if (i < chron.events.size())
				dirty.insert(chron.events[i].id);
	}
	// the events changed, to the clients
	for (uint32_t id : dirty)
		if (GameChronicle::Event* e = eventById(id)) {
			makeMentions(server, *e);
			sendLine(server, e->toText(), nullptr);
		}
	dirty.clear();
	// the new samples
	for (const auto& [player, list] : chron.samples) {
		size_t& sent = samplesSent[player];
		for (; sent < list.size(); sent++)
			sendLine(server, chron.sampleText(player, list[sent]), nullptr);
	}
}

void ServerChronicle::sendAll(Server& server, NetLink* link)
{
	sendLine(server, "RESET", link);
	for (auto& e : chron.events) {
		makeMentions(server, e);
		sendLine(server, e.toText(), link);
	}
	for (const auto& [player, list] : chron.samples)
		for (const auto& s : list)
			sendLine(server, chron.sampleText(player, s), link);
	if (!link) {
		started = true;
		for (const auto& [player, list] : chron.samples)
			samplesSent[player] = list.size();
	}
}

std::string ServerChronicle::saveText(Server& server)
{
	std::string text = "CHRONICLE " + chron.stateText() + "\n";
	for (auto& e : chron.events) {
		makeMentions(server, e);
		text += "CHRONICLE " + e.toText() + "\n";
	}
	for (const auto& [player, list] : chron.samples)
		for (const auto& s : list)
			text += "CHRONICLE " + chron.sampleText(player, s) + "\n";
	for (const auto& [id, units] : participants)
		for (const auto& [unit, p] : units) {
			char line[160];
			snprintf(line, sizeof(line), "CHRONICLE PART %u %u %u %d %d %.9g\n", id, unit, p.player, p.kills, (int)p.flags, p.reached);
			text += line;
		}
	return text;
}

void ServerChronicle::loadLine(Server& server, const std::string& line)
{
	level = server.getLevel();
	if (line.compare(0, 6, "STATE ") == 0)
		chron.stateFromText(line);
	else if (line.compare(0, 7, "SAMPLE ") == 0) {
		chron.sampleFromText(line);
	}
	else if (line.compare(0, 6, "EVENT ") == 0) {
		GameChronicle::Event e;
		if (e.fromText(line)) {
			e.seen = true;
			for (const auto& m : e.mentions)
				if (m.kind == GameChronicle::MENTION_FALLEN)
					fallen[e.id].push_back(m);
			chron.events.push_back(std::move(e));
		}
	}
	else if (line.compare(0, 5, "PART ") == 0) {
		unsigned id = 0, unit = 0, player = 0;
		int k = 0, flags = 0;
		float reached = 0.0f;
		if (sscanf(line.c_str(), "PART %u %u %u %d %d %f", &id, &unit, &player, &k, &flags, &reached) == 6) {
			participants[id][unit] = { player, k, (uint8_t)flags, reached };
			eventsOfUnit[unit].insert(id);
		}
	}
}

void ServerChronicle::sendLine(Server& server, const std::string& line, NetLink* link)
{
	// in parts of up to 240 bytes: part, parts (16 bits each), the bytes
	constexpr size_t PART = 240;
	const size_t parts = std::max<size_t>(1, (line.size() + PART - 1) / PART);
	for (size_t i = 0; i < parts; i++) {
		NetPacketWriter npw{ NETCLIMSG_CHRONICLE };
		npw.writeUint16((uint16_t)i);
		npw.writeUint16((uint16_t)parts);
		const size_t from = std::min(i * PART, line.size());
		const std::string bytes = line.substr(from, std::min(PART, line.size() - from));
		npw.writeUint8((uint8_t)bytes.size());
		npw.writeString(bytes);
		if (link)
			link->send(npw);
		else
			server.sendToAll(npw);
	}
}
