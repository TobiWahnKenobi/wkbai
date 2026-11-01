// wkbai - statistics and a timeline of a game's events, as a client sees them

#include "GameChronicle.h"
#include "FogOfWar.h"
#include "../client.h"
#include "../tags.h"
#include "../gameset/gameset.h"
#include "../gameset/OrderBlueprint.h"
#include <algorithm>
#include <cmath>

const char* GameChronicle::kindName(Kind kind)
{
	switch (kind) {
	case Kind::Battle: return "Battle";
	case Kind::Siege: return "Siege";
	case Kind::Raid: return "Raid";
	case Kind::Founded: return "Founded";
	case Kind::Destroyed: return "Destroyed";
	}
	return "?";
}

std::string GameChronicle::Event::title() const
{
	const std::string at = place.empty() ? std::string("the wilds") : place;
	switch (kind) {
	case Kind::Battle: return "Battle of " + at;
	case Kind::Siege: return "Siege of " + at;
	case Kind::Raid: return "Raid on " + at;
	case Kind::Founded: return what + " founded at " + at;
	case Kind::Destroyed: return what + " destroyed at " + at;
	}
	return at;
}

void GameChronicle::update(Client* client, const FogOfWar* fog)
{
	if (!client || !client->getLevel() || !client->gameSet)
		return;
	const float now = client->timeManager.currentTime;
	// a game loaded again or a new one: start over
	if (now + 1.0f < nextSample - 10.0f) {
		*this = GameChronicle();
	}
	if (!regions.ready())
		regions.build(client);
	if (now >= nextScan) {
		nextScan = now + 5.0f;
		scan(client, fog, now);
	}
	if (now >= nextSample) {
		nextSample = now + 10.0f;
		sample(client, now);
	}
}

void GameChronicle::sample(Client* client, float now)
{
	const GameSet& gs = *client->gameSet;
	const int peasantItem = gs.items.names.getIndex("I Am A Peasant");
	const int militaryItem = gs.items.names.getIndex("I'm A Military Unit");
	for (const auto& [type, children] : client->getLevel()->children)
		for (CommonGameObject* p : children) {
			if (p->blueprint->bpClass != Tags::GAMEOBJCLASS_PLAYER || p->blueprint->name == "Neutral Player")
				continue;
			Sample s{ now, p->getItem(Tags::PDITEM_FOOD), p->getItem(Tags::PDITEM_WOOD), p->getItem(Tags::PDITEM_GOLD), 0, 0, 0, unitsLost[p->id], buildingsLost[p->id] };
			auto walk = [&](CommonGameObject* obj, auto& self) -> void {
				for (const auto& [t, ch] : obj->children)
					for (CommonGameObject* o : ch) {
						if (!(o->flags & CommonGameObject::fTerminated)) {
							if (o->blueprint->bpClass == Tags::GAMEOBJCLASS_CHARACTER) {
								if (peasantItem >= 0 && o->getItem(peasantItem) > 0.0f)
									s.peasants++;
								else if (militaryItem >= 0 && o->getItem(militaryItem) > 0.0f)
									s.soldiers++;
							}
							else if (o->blueprint->bpClass == Tags::GAMEOBJCLASS_BUILDING)
								s.buildings++;
						}
						self(o, self);
					}
			};
			walk(p, walk);
			// a player with nothing left and nothing ever: not listed
			if (s.peasants + s.soldiers + s.buildings == 0 && samples.find(p->id) == samples.end())
				continue;
			samples[p->id].push_back(s);
			// an AI's general ("Saint Jane Kingmaker" -> "Saint Jane") before the map's "Player 5"
			std::string name = p->name;
			std::string general = p->blueprint->name;
			if (general.size() > 10 && general.compare(general.size() - 10, 10, " Kingmaker") == 0)
				general.resize(general.size() - 10);
			if (name.empty() || (name.rfind("Player ", 0) == 0 && general != "Human Player"))
				name = general;
			playerNames[p->id] = name;
			playerColours[p->id] = p->color;
		}
}

void GameChronicle::scan(Client* client, const FogOfWar* fog, float now)
{
	const GameSet& gs = *client->gameSet;
	const int peasantItem = gs.items.names.getIndex("I Am A Peasant");
	const int militaryItem = gs.items.names.getIndex("I'm A Military Unit");
	const int attackCategory = gs.orderCategories.names.getIndex("Order Attack");
	CommonGameObject* own = client->clientPlayer.get();
	const uint32_t ownId = own ? own->id : 0;
	auto placeAt = [&](const Vector3& pos) {
		const MapRegions::Region* r = regions.at(pos);
		return r ? r->name : std::string();
	};
	auto seenAt = [&](const Vector3& pos, const std::set<uint32_t>& players) {
		return !fog || players.count(ownId) || fog->isSeen(pos);
	};

	// everything now, by id; the fighters attacking; the peasants, farms and buildings by player
	std::map<uint32_t, Known> current;
	struct Fighter { Vector3 pos; uint32_t player; };
	std::vector<Fighter> fighters;
	std::vector<Fighter> targets, buildings; // peasants and farms; all buildings
	auto walk = [&](CommonGameObject* obj, uint32_t player, auto& self) -> void {
		for (const auto& [t, ch] : obj->children)
			for (CommonGameObject* o : ch) {
				uint32_t owner = player;
				if (o->blueprint->bpClass == Tags::GAMEOBJCLASS_PLAYER)
					owner = o->id;
				if (!(o->flags & CommonGameObject::fTerminated) && owner) {
					const int cls = o->blueprint->bpClass;
					if (cls == Tags::GAMEOBJCLASS_CHARACTER || cls == Tags::GAMEOBJCLASS_BUILDING) {
						const std::string& name = o->blueprint->name;
						static const std::string suffix = " Foundation";
						const bool foundation = name.size() > suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0;
						const bool settlement = cls == Tags::GAMEOBJCLASS_BUILDING
							&& (o->blueprint->buildingType == Tags::BUILDINGTYPE_CIVIC_CENTRE || o->blueprint->buildingType == Tags::BUILDINGTYPE_RURAL_CENTRE);
						current[o->id] = Known{ owner, cls == Tags::GAMEOBJCLASS_BUILDING, settlement, foundation, o->position,
							foundation ? name.substr(0, name.size() - suffix.size()) : name,
							cls == Tags::GAMEOBJCLASS_CHARACTER && militaryItem >= 0 && o->getItem(militaryItem) > 0.0f };
						if (cls == Tags::GAMEOBJCLASS_BUILDING) {
							buildings.push_back({ o->position, owner });
							if (name.find("Farm") != std::string::npos)
								targets.push_back({ o->position, owner });
						}
						else if (peasantItem >= 0 && o->getItem(peasantItem) > 0.0f)
							targets.push_back({ o->position, owner });
						else if (militaryItem >= 0 && o->getItem(militaryItem) > 0.0f && attackCategory >= 0) {
							const int order = ((ClientGameObject*)o)->reportedCurrentOrder;
							if (order >= 0 && order < (int)gs.orders.size() && gs.orders[order].category == attackCategory)
								fighters.push_back({ o->position, owner });
						}
					}
				}
				self(o, owner, self);
			}
	};
	walk(client->getLevel(), 0, walk);

	// losses: what was there and is gone (or another player's now); settlements destroyed, and
	// founded (a new foundation, or a settlement where there was none)
	for (const auto& [id, k] : known) {
		auto it = current.find(id);
		if (it != current.end() && it->second.player == k.player)
			continue;
		if (k.building)
			buildingsLost[k.player]++;
		else
			unitsLost[k.player]++;
		// a unit gone at a fight going on (within 100 m) was lost there
		if (!k.building)
			for (Event& e : events)
				if ((e.kind == Kind::Battle || e.kind == Kind::Siege || e.kind == Kind::Raid) && now - e.endTime <= 30.0f
					&& (e.position - k.position).sqlen2xz() < 100.0f * 100.0f) {
					e.lost[k.player][k.name]++;
					e.players.insert(k.player);
					break;
				}
		if (k.settlement && !first) {
			// a foundation finished: the building stands where it was
			const bool finished = std::any_of(current.begin(), current.end(), [&](const auto& c) {
				return c.second.settlement && c.second.player == k.player && (c.second.position - k.position).sqlen2xz() < 25.0f; });
			if (!finished) {
				Event e{ Kind::Destroyed, now, now, k.position, { k.player }, 0, k.name, placeAt(k.position) };
				e.seen = seenAt(k.position, e.players);
				events.push_back(std::move(e));
			}
		}
	}
	if (!first)
		for (const auto& [id, c] : current) {
			if (!c.settlement || known.count(id))
				continue;
			const bool wasThere = std::any_of(known.begin(), known.end(), [&](const auto& k) {
				return k.second.settlement && k.second.player == c.player && (k.second.position - c.position).sqlen2xz() < 25.0f; });
			if (wasThere)
				continue;
			Event e{ Kind::Founded, now, now, c.position, { c.player }, 0, c.name, placeAt(c.position) };
			e.seen = seenAt(c.position, e.players);
			events.push_back(std::move(e));
		}
	known = std::move(current);
	first = false;

	// fights: the fighters attacking, grouped in 60 m cells with their neighbours
	std::map<std::pair<int, int>, std::vector<size_t>> cells;
	for (size_t i = 0; i < fighters.size(); i++)
		cells[{ (int)std::floor(fighters[i].pos.x / 60.0f), (int)std::floor(fighters[i].pos.z / 60.0f) }].push_back(i);
	std::set<std::pair<int, int>> done;
	for (const auto& [cell, list] : cells) {
		if (done.count(cell))
			continue;
		// the cell with its neighbours that have fighters
		std::vector<size_t> group;
		for (int dz = -1; dz <= 1; dz++)
			for (int dx = -1; dx <= 1; dx++) {
				auto it = cells.find({ cell.first + dx, cell.second + dz });
				if (it == cells.end() || done.count(it->first))
					continue;
				done.insert(it->first);
				group.insert(group.end(), it->second.begin(), it->second.end());
			}
		Vector3 centre;
		std::set<uint32_t> players;
		std::map<uint32_t, int> strength;
		for (size_t i : group) {
			centre += fighters[i].pos;
			players.insert(fighters[i].player);
			strength[fighters[i].player]++;
		}
		centre = centre * (1.0f / (float)group.size());
		const int size = (int)group.size();
		Kind kind;
		std::set<uint32_t> involved = players;
		if (size >= 10 && players.size() >= 2) {
			// at buildings of a side in the fight (2 or more within 40 m): a siege
			int atBuildings = 0;
			for (const Fighter& b : buildings)
				if ((b.pos - centre).sqlen2xz() < 40.0f * 40.0f)
					atBuildings++;
			kind = atBuildings >= 2 ? Kind::Siege : Kind::Battle;
		}
		else if (size < 10) {
			// a few attackers at another player's peasants or farms: a raid
			uint32_t victim = 0;
			for (const Fighter& t : targets)
				if (!players.count(t.player) && (t.pos - centre).sqlen2xz() < 30.0f * 30.0f) {
					victim = t.player;
					break;
				}
			if (!victim)
				continue;
			kind = Kind::Raid;
			involved.insert(victim);
		}
		else
			continue;
		// an event still going on near here goes on (fights 30 s apart are one)
		Event* going = nullptr;
		for (Event& e : events)
			if ((e.kind == Kind::Battle || e.kind == Kind::Siege || e.kind == Kind::Raid) && now - e.endTime <= 30.0f
				&& (e.position - centre).sqlen2xz() < 100.0f * 100.0f)
				going = &e;
		if (going) {
			going->endTime = now;
			going->size = std::max(going->size, size);
			for (const auto& [p, n] : strength)
				going->strength[p] = std::max(going->strength[p], n);
			going->players.insert(involved.begin(), involved.end());
			// a raid that grew is a battle, a battle at buildings a siege
			if (kind == Kind::Siege || (kind == Kind::Battle && going->kind == Kind::Raid))
				going->kind = kind;
			going->seen = going->seen || seenAt(centre, going->players);
		}
		else {
			Event e{ kind, now, now, centre, involved, size, std::string(), placeAt(centre) };
			e.strength = strength;
			e.seen = seenAt(centre, e.players);
			events.push_back(std::move(e));
		}
	}
}
