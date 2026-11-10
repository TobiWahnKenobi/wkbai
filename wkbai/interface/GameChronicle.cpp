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
	case Kind::Fallen: return "Fallen";
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
	case Kind::Fallen: return "The fall of " + what + " at " + at;
	}
	return at;
}

namespace {
std::string Quoted(const std::string& s)
{
	std::string out = "\"";
	for (char c : s)
		if (c != '"' && c != '\n' && c != '\r')
			out += c;
	return out + "\"";
}

// the line's words, "quoted" ones whole
std::vector<std::string> SplitWords(const std::string& line)
{
	std::vector<std::string> words;
	size_t i = 0;
	while (i < line.size()) {
		while (i < line.size() && line[i] == ' ')
			i++;
		if (i >= line.size())
			break;
		if (line[i] == '"') {
			const size_t end = line.find('"', i + 1);
			words.push_back(line.substr(i + 1, (end == std::string::npos ? line.size() : end) - i - 1));
			i = end == std::string::npos ? line.size() : end + 1;
		}
		else {
			size_t end = line.find(' ', i);
			if (end == std::string::npos)
				end = line.size();
			words.push_back(line.substr(i, end - i));
			i = end;
		}
	}
	return words;
}

std::string Num(float f)
{
	char b[32];
	snprintf(b, sizeof(b), "%.9g", f);
	return b;
}
}

std::string GameChronicle::Event::toText() const
{
	std::string t = "EVENT " + std::to_string(id) + " " + std::to_string((int)kind) + " " + Num(time) + " " + Num(endTime) + " "
		+ Num(position.x) + " " + Num(position.y) + " " + Num(position.z) + " " + Num(lastPosition.x) + " " + Num(lastPosition.y) + " "
		+ Num(lastPosition.z) + " " + std::to_string(size) + " " + Quoted(what) + " " + Quoted(place);
	t += " P " + std::to_string(players.size());
	for (uint32_t p : players) {
		auto it = strength.find(p);
		t += " " + std::to_string(p) + " " + std::to_string(it != strength.end() ? it->second : -1);
	}
	size_t lostCount = 0;
	for (const auto& [p, types] : lost)
		lostCount += types.size();
	t += " L " + std::to_string(lostCount);
	for (const auto& [p, types] : lost)
		for (const auto& [type, n] : types)
			t += " " + std::to_string(p) + " " + Quoted(type) + " " + std::to_string(n);
	t += " M " + std::to_string(mentions.size());
	for (const Mention& m : mentions)
		t += " " + std::to_string(m.kind) + " " + std::to_string(m.unit) + " " + std::to_string(m.player) + " " + std::to_string(m.kills) + " "
			+ (m.dead ? "1" : "0") + " " + Quoted(m.name) + " " + Quoted(m.extra);
	return t;
}

bool GameChronicle::Event::fromText(const std::string& text)
{
	const std::vector<std::string> w = SplitWords(text);
	if (w.size() < 14 || w[0] != "EVENT")
		return false;
	size_t i = 1;
	auto next = [&]() -> const std::string& { static const std::string none; return i < w.size() ? w[i++] : none; };
	auto u = [&]() { return (uint32_t)strtoul(next().c_str(), nullptr, 10); };
	auto f = [&]() { return (float)atof(next().c_str()); };
	auto n = [&]() { return atoi(next().c_str()); };
	id = u();
	kind = (Kind)std::clamp(n(), 0, (int)Kind::Fallen);
	time = f(); endTime = f();
	position.x = f(); position.y = f(); position.z = f();
	lastPosition.x = f(); lastPosition.y = f(); lastPosition.z = f();
	size = n();
	what = next();
	place = next();
	players.clear(); strength.clear(); lost.clear(); mentions.clear();
	if (next() != "P")
		return false;
	for (int k = n(); k > 0 && i < w.size(); k--) {
		const uint32_t p = u();
		const int s = n();
		players.insert(p);
		if (s >= 0)
			strength[p] = s;
	}
	if (next() != "L")
		return false;
	for (int k = n(); k > 0 && i < w.size(); k--) {
		const uint32_t p = u();
		const std::string type = next();
		lost[p][type] = n();
	}
	if (next() != "M")
		return false;
	for (int k = n(); k > 0 && i < w.size(); k--) {
		Mention m;
		m.kind = n(); m.unit = u(); m.player = u(); m.kills = n(); m.dead = n() != 0;
		m.name = next(); m.extra = next();
		mentions.push_back(std::move(m));
	}
	return true;
}

std::string GameChronicle::sampleText(uint32_t player, const Sample& s) const
{
	auto nit = playerNames.find(player);
	auto cit = playerColours.find(player);
	return "SAMPLE " + std::to_string(player) + " " + Num(s.time) + " " + Num(s.food) + " " + Num(s.wood) + " " + Num(s.gold) + " "
		+ std::to_string(s.peasants) + " " + std::to_string(s.soldiers) + " " + std::to_string(s.buildings) + " " + std::to_string(s.unitsLost)
		+ " " + std::to_string(s.buildingsLost) + " " + std::to_string(cit != playerColours.end() ? cit->second : 0) + " "
		+ Quoted(nit != playerNames.end() ? nit->second : std::string());
}

bool GameChronicle::sampleFromText(const std::string& text)
{
	const std::vector<std::string> w = SplitWords(text);
	if (w.size() < 13 || w[0] != "SAMPLE")
		return false;
	const uint32_t player = (uint32_t)strtoul(w[1].c_str(), nullptr, 10);
	Sample s{ (float)atof(w[2].c_str()), (float)atof(w[3].c_str()), (float)atof(w[4].c_str()), (float)atof(w[5].c_str()),
		atoi(w[6].c_str()), atoi(w[7].c_str()), atoi(w[8].c_str()), atoi(w[9].c_str()), atoi(w[10].c_str()) };
	samples[player].push_back(s);
	playerColours[player] = atoi(w[11].c_str());
	playerNames[player] = w[12];
	return true;
}

std::string GameChronicle::stateText() const
{
	std::string t = "STATE " + std::to_string(nextId) + " " + Num(nextSample) + " " + Num(nextScan) + " " + (first ? "1" : "0") + " " + std::to_string(unitsLost.size());
	for (const auto& [p, n] : unitsLost)
		t += " " + std::to_string(p) + " " + std::to_string(n);
	t += " " + std::to_string(buildingsLost.size());
	for (const auto& [p, n] : buildingsLost)
		t += " " + std::to_string(p) + " " + std::to_string(n);
	return t;
}

bool GameChronicle::stateFromText(const std::string& text)
{
	const std::vector<std::string> w = SplitWords(text);
	if (w.size() < 6 || w[0] != "STATE")
		return false;
	nextId = (uint32_t)strtoul(w[1].c_str(), nullptr, 10);
	nextSample = (float)atof(w[2].c_str());
	nextScan = (float)atof(w[3].c_str());
	first = true; // what was there at the save isn't known: the first scan only notes it
	size_t i = 5;
	int count = atoi(w[i++].c_str());
	for (; count > 0 && i + 1 < w.size(); count--, i += 2)
		unitsLost[(uint32_t)strtoul(w[i].c_str(), nullptr, 10)] = atoi(w[i + 1].c_str());
	if (i < w.size()) {
		count = atoi(w[i++].c_str());
		for (; count > 0 && i + 1 < w.size(); count--, i += 2)
			buildingsLost[(uint32_t)strtoul(w[i].c_str(), nullptr, 10)] = atoi(w[i + 1].c_str());
	}
	return true;
}

void GameChronicle::receive(Client* client, const FogOfWar* fog, const std::string& line)
{
	const uint32_t ownId = client->clientPlayer ? client->clientPlayer->id : 0;
	if (line.compare(0, 5, "RESET") == 0) {
		events.clear();
		samples.clear();
		playerNames.clear();
		playerColours.clear();
		serverDriven = true;
		return;
	}
	if (line.compare(0, 6, "SAMPLE") == 0) {
		sampleFromText(line);
		return;
	}
	Event e;
	if (!e.fromText(line))
		return;
	// which this client's player saw: its own in it, or its place in sight (its own fog of war)
	auto seenNow = [&](const Event& ev) {
		if (!fog || ev.players.count(ownId))
			return true;
		return fog->isSeen(ev.fight() ? ev.lastPosition : ev.position);
	};
	auto it = std::find_if(events.begin(), events.end(), [&](const Event& o) { return o.id == e.id; });
	int fallenBefore = 0;
	if (it != events.end()) {
		for (const Mention& m : it->mentions)
			fallenBefore += m.kind == MENTION_FALLEN;
		e.seen = it->seen || seenNow(e);
		*it = std::move(e);
	}
	else {
		e.seen = seenNow(e);
		events.push_back(std::move(e));
		it = events.end() - 1;
	}
	// heroes fallen since: announced
	int fallen = 0;
	for (const Mention& m : it->mentions)
		if (m.kind == MENTION_FALLEN && ++fallen > fallenBefore)
			fallenAnnouncements.push_back({ m.player, m.name + (m.kills ? " (" + std::to_string(m.kills) + " kills)" : std::string()) + " has fallen"
				+ (it->place.empty() ? std::string() : " at " + it->place) + (m.extra.empty() ? std::string() : ", " + m.extra) });
}

void GameChronicle::update(Client* client, const FogOfWar* fog)
{
	if (!client || !client->getLevel() || !client->gameSet)
		return;
	const float now = client->timeManager.currentTime;
	// a game loaded again or a new one (another level, or the clock a minute back): start over.
	// Not on a few seconds back: a replay's clock runs ahead of a slow simulation and the time
	// sync puts it back, which wiped the timeline.
	if (client->getLevel() != level || (!serverDriven && now + 60.0f < nextSample - 10.0f)) {
		*this = GameChronicle();
		level = client->getLevel();
	}
	// the server's events and samples (NETCLIMSG_CHRONICLE)
	for (const std::string& line : client->chronicleIn)
		receive(client, fog, line);
	client->chronicleIn.clear();
	if (serverDriven)
		return;
	// the host's regions (every client the same names); without them (a host that sends none)
	// the client's own, once the game has run a while
	if (client->hostRegionsSerial != hostRegionsSerial) {
		hostRegionsSerial = client->hostRegionsSerial;
		MapRegions fromHost;
		if (fromHost.fromText(client->hostRegions)) {
			regions = std::move(fromHost);
			printf("Chronicle: %zu map regions from the host, the first %s\n", regions.regions.size(), regions.regions.empty() ? "-" : regions.regions[0].name.c_str());
		}
	}
	if (!regions.ready() && now > 5.0f)
		regions.build(client);
	step(client, client->clientPlayer ? client->clientPlayer->id : 0, fog, now);
}

bool GameChronicle::step(CommonGameState* state, uint32_t ownId, const FogOfWar* fog, float now)
{
	bool scanned = false;
	touched.clear();
	if (now >= nextScan) {
		nextScan = now + 5.0f;
		scan(state, ownId, fog, now);
		scanned = true;
	}
	if (now >= nextSample) {
		nextSample = now + 10.0f;
		sample(state, now);
	}
	return scanned;
}

void GameChronicle::sample(CommonGameState* state, float now)
{
	const GameSet& gs = *state->gameSet;
	const int peasantItem = gs.items.names.getIndex("I Am A Peasant");
	const int militaryItem = gs.items.names.getIndex("I'm A Military Unit");
	for (const auto& [type, children] : state->getLevel()->children)
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

void GameChronicle::scan(CommonGameState* state, uint32_t ownId, const FogOfWar* fog, float now)
{
	const GameSet& gs = *state->gameSet;
	const int peasantItem = gs.items.names.getIndex("I Am A Peasant");
	const int militaryItem = gs.items.names.getIndex("I'm A Military Unit");
	const int attackCategory = gs.orderCategories.names.getIndex("Order Attack");
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
							const int order = o->reportedCurrentOrder;
							if (order >= 0 && order < (int)gs.orders.size() && gs.orders[order].category == attackCategory)
								fighters.push_back({ o->position, owner });
						}
					}
				}
				self(o, owner, self);
			}
	};
	walk(state->getLevel(), 0, walk);

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
		// a unit gone at a fight going on (within 100 m of where it last was) was lost there
		if (!k.building)
			for (Event& e : events)
				if ((e.kind == Kind::Battle || e.kind == Kind::Siege || e.kind == Kind::Raid) && now - e.endTime <= 30.0f
					&& (e.lastPosition - k.position).sqlen2xz() < 100.0f * 100.0f) {
					e.lost[k.player][k.name]++;
					e.players.insert(k.player);
					touched.push_back(&e - events.data());
					break;
				}
		if (k.settlement && !first) {
			// a foundation finished: the building stands where it was
			const bool finished = std::any_of(current.begin(), current.end(), [&](const auto& c) {
				return c.second.settlement && c.second.player == k.player && (c.second.position - k.position).sqlen2xz() < 25.0f; });
			if (!finished) {
				Event e{ Kind::Destroyed, now, now, k.position, { k.player }, 0, k.name, placeAt(k.position) };
				e.seen = seenAt(k.position, e.players);
				e.id = nextId++;
				touched.push_back(events.size());
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
			e.id = nextId++;
			touched.push_back(events.size());
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
		// a fight in the same map region (or within 100 m) less than 6 minutes after one there
		// ended is the same one going on: one entry on the timeline, not one every few minutes
		const std::string place = placeAt(centre);
		Event* going = nullptr;
		for (Event& e : events)
			if ((e.kind == Kind::Battle || e.kind == Kind::Siege || e.kind == Kind::Raid) && now - e.endTime < 360.0f
				&& ((!place.empty() && e.place == place) || (e.lastPosition - centre).sqlen2xz() < 100.0f * 100.0f))
				going = &e;
		if (going) {
			going->endTime = now;
			going->lastPosition = centre;
			going->size = std::max(going->size, size);
			for (const auto& [p, n] : strength)
				going->strength[p] = std::max(going->strength[p], n);
			going->players.insert(involved.begin(), involved.end());
			// a raid that grew is a battle, a battle at buildings a siege
			if (kind == Kind::Siege || (kind == Kind::Battle && going->kind == Kind::Raid))
				going->kind = kind;
			going->seen = going->seen || seenAt(centre, going->players);
			touched.push_back(going - events.data());
		}
		else {
			Event e{ kind, now, now, centre, involved, size, std::string(), place };
			e.lastPosition = centre;
			e.strength = strength;
			e.seen = seenAt(centre, e.players);
			e.id = nextId++;
			touched.push_back(events.size());
			events.push_back(std::move(e));
		}
	}
}
