// wkbai - unit history: who killed whom (the RE repo's docs/engine/unit-history.md)

#include "UnitHistory.h"
#include "server.h"
#include "network.h"
#include "tags.h"
#include "gameset/gameset.h"
#include "settings.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstdio>
#include <cstdlib>

void UnitHistory::setup(const GameSet* set)
{
	if (set == gs)
		return;
	gs = set;
	attackEvents.assign(gs ? gs->events.names.size() : 0, false);
	if (!gs)
		return;
	// the packages that hurt (Mechanics/Death.cpp: what triggers "Being Killed" and "Building
	// Destroyed"). A game set without them counts no kills.
	for (const char* name : { "Injured By Attack", "Missile Attack", "Non Specific Attack", "Spew Attack" }) {
		const int evt = gs->events.names.getIndex(name);
		if (evt >= 0)
			attackEvents[evt] = true;
	}
	hitPoints = gs->items.names.getIndex("Hit Points");
	structuralIntegrity = gs->items.names.getIndex("Structural Integrity");
	gunnerItem = gs->items.names.getIndex("I Am A Gunner");
	lightInfantryItem = gs->items.names.getIndex("I'm an LI unit");
	garrisonCategory = gs->associations.names.getIndex("Garrison");
	trace = getenv("WKB_TRACE_KILLS") != nullptr || g_settings.value("unitHistoryTrace", false);
}

void UnitHistory::onEvent(Server& server, ServerGameObject* victim, int evt, ServerGameObject* sender)
{
	if (!sender)
		return;
	setup(server.gameSet.get());
	if (isAttack(evt))
		hit(server, victim, sender);
}

uint32_t UnitHistory::resolve(Server& server, ServerGameObject* sender) const
{
	ServerGameObject* obj = sender;
	for (int step = 0; obj && step < 5; step++) {
		if (obj->creditId)
			return obj->creditId;
		const int cls = obj->blueprint->bpClass;
		if (cls == Tags::GAMEOBJCLASS_CHARACTER || cls == Tags::GAMEOBJCLASS_BUILDING)
			return obj->id;
		if (!obj->creatorId)
			return 0;
		ServerGameObject* creator = server.findObject(obj->creatorId);
		// a firer gone since (deleted): it still gets the kill
		if (!creator)
			return obj->creatorId;
		obj = creator;
	}
	return 0;
}

void UnitHistory::hit(Server& server, ServerGameObject* victim, ServerGameObject* sender)
{
	if (!sender || sender == victim)
		return;
	const uint32_t by = resolve(server, sender);
	if (!by || by == victim->id)
		return;
	victim->lastHitBy = by;
	victim->lastHitTime = server.timeManager.currentTime;
	if (trace)
		printf("UNITHISTORY t=%.2f hit %u %s by %u (sent by %u %s)\n", server.timeManager.currentTime, victim->id, victim->blueprint->name.c_str(),
			by, sender->id, sender->blueprint->name.c_str());
}

uint32_t UnitHistory::garrisonShooter(Server& server, ServerGameObject* building, ServerGameObject* missile) const
{
	if (garrisonCategory < 0)
		return 0;
	auto it = building->associates.find(garrisonCategory);
	if (it == building->associates.end() || it->second.empty())
		return 0;
	// Mechanics/Fort Attack.cpp: one arrow per light infantry unit that is no gunner, one bullet
	// per gunner ("Fire Bullet From Slit")
	const bool bullet = missile->blueprint->name.find("Bullet") != std::string::npos;
	std::vector<uint32_t> shooters; // by id (the set is ordered by id)
	for (const SrvGORef& ref : it->second) {
		ServerGameObject* t = ref.get();
		if (!t || t->deleted || (t->flags & ServerGameObject::fTerminated))
			continue;
		const bool gunner = gunnerItem >= 0 && t->getItem(gunnerItem) > 0.0f;
		const bool archer = !gunner && lightInfantryItem >= 0 && t->getItem(lightInfantryItem) > 0.0f;
		if (bullet ? gunner : archer)
			shooters.push_back(t->id);
	}
	if (shooters.empty())
		return 0;
	// in turn: the first after the last one credited
	for (uint32_t id : shooters)
		if (id > building->garrisonCredit)
			return building->garrisonCredit = id;
	return building->garrisonCredit = shooters.front();
}

void UnitHistory::created(Server& server, ServerGameObject* obj, ServerGameObject* creator)
{
	if (!obj || !creator)
		return;
	setup(server.gameSet.get());
	obj->creatorId = creator->id;
	// a missile a garrisoned building fires: its kills go to a troop inside (the user: buildings
	// can't become heroes, the troops inside them can)
	if (obj->blueprint->bpClass == Tags::GAMEOBJCLASS_MISSILE && creator->blueprint->bpClass == Tags::GAMEOBJCLASS_BUILDING)
		obj->creditId = garrisonShooter(server, creator, obj);
	// a missile made by a missile (a splitting shot) keeps its firer's credit
	else if (creator->creditId)
		obj->creditId = creator->creditId;
}

bool UnitHistory::isEnemy(Server& server, ServerGameObject* killer, ServerGameObject* victim) const
{
	ServerGameObject* kp = killer->getPlayer();
	ServerGameObject* vp = victim->getPlayer();
	if (!kp || !vp || kp == vp)
		return false;
	// the Neutral Player's animals (horses, camels, sheep, chickens) give no kills (the user)
	if (vp->blueprint->name == "Neutral Player")
		return false;
	return server.getDiplomaticStatus(kp, vp) >= 2; // ENEMY_UNITS (finder.cpp)
}

void UnitHistory::terminated(Server& server, ServerGameObject* obj)
{
	const int cls = obj->blueprint->bpClass;
	if (cls != Tags::GAMEOBJCLASS_CHARACTER && cls != Tags::GAMEOBJCLASS_BUILDING)
		return;
	if (!obj->lastHitBy || server.timeManager.currentTime - obj->lastHitTime > creditWindow)
		return;
	setup(server.gameSet.get());
	// a death, not a removal (going into a building, a conversion, REMOVE): no hit points left
	const int item = cls == Tags::GAMEOBJCLASS_CHARACTER ? hitPoints : structuralIntegrity;
	if (item < 0 || obj->getItem(item) > 0.0f)
		return;
	// a foundation pulled down is no building destroyed
	static const std::string suffix = " Foundation";
	const std::string& name = obj->blueprint->name;
	if (cls == Tags::GAMEOBJCLASS_BUILDING && name.size() > suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0)
		return;
	const uint32_t victimId = obj->id;
	const uint32_t killerId = obj->lastHitBy;
	obj->lastHitBy = 0;
	ServerGameObject* killer = server.findObject(killerId);
	if (!killer || killer->deleted || !isEnemy(server, killer, obj))
		return;
	if (cls == Tags::GAMEOBJCLASS_CHARACTER)
		killer->kills++;
	else
		killer->buildingsDestroyed++;
	if (trace)
		printf("UNITHISTORY t=%.2f %s %u %s by %u %s: kills %d, buildings %d\n", server.timeManager.currentTime,
			cls == Tags::GAMEOBJCLASS_CHARACTER ? "killed" : "destroyed", victimId, name.c_str(), killerId, killer->blueprint->name.c_str(),
			killer->kills, killer->buildingsDestroyed);
	sendKills(server, killer, victimId);
}

std::string UnitHistory::saveText(Server& server)
{
	std::string text;
	std::vector<uint32_t> ids;
	for (const auto& [id, cobj] : server.idmap)
		ids.push_back(id);
	std::sort(ids.begin(), ids.end());
	for (uint32_t id : ids) {
		const ServerGameObject* obj = server.findObject(id);
		if (!obj || obj->deleted)
			continue;
		if (!obj->kills && !obj->buildingsDestroyed && !obj->creatorId && !obj->creditId && !obj->lastHitBy && !obj->garrisonCredit)
			continue;
		char line[160];
		snprintf(line, sizeof(line), "UNIT %u %d %d %u %u %u %.9g %u\n", obj->id, obj->kills, obj->buildingsDestroyed, obj->creatorId,
			obj->creditId, obj->lastHitBy, obj->lastHitTime, obj->garrisonCredit);
		text += line;
	}
	return text;
}

void UnitHistory::loadText(Server& server, const std::string& all)
{
	size_t at = 0;
	while (at < all.size()) {
		size_t end = all.find('\n', at);
		if (end == std::string::npos)
			end = all.size();
		const std::string line = all.substr(at, end - at);
		at = end + 1;
		unsigned id = 0, creator = 0, credit = 0, lastBy = 0, garrison = 0;
		int kills = 0, buildings = 0;
		float lastTime = -1.0f;
		if (sscanf(line.c_str(), "UNIT %u %d %d %u %u %u %f %u", &id, &kills, &buildings, &creator, &credit, &lastBy, &lastTime, &garrison) != 8)
			continue;
		if (ServerGameObject* obj = server.findObject(id)) {
			obj->kills = kills;
			obj->buildingsDestroyed = buildings;
			obj->creatorId = creator;
			obj->creditId = credit;
			obj->lastHitBy = lastBy;
			obj->lastHitTime = lastTime;
			obj->garrisonCredit = garrison;
			if (kills || buildings)
				sendKills(server, obj, 0);
		}
	}
}

void UnitHistory::sendKills(Server& server, ServerGameObject* killer, uint32_t victim)
{
	NetPacketWriter npw{ NETCLIMSG_UNIT_KILLS };
	npw.writeUint32(killer->id);
	npw.writeUint32(victim);
	npw.writeUint32((uint32_t)killer->kills);
	npw.writeUint32((uint32_t)killer->buildingsDestroyed);
	server.sendToAll(npw);
}
