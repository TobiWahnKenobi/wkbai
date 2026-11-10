// wkbai - unit history: who killed whom, names, ages and heroes (the RE repo's
// docs/engine/unit-history.md)

#include "UnitHistory.h"
#include "server.h"
#include "network.h"
#include "tags.h"
#include "file.h"
#include "gameset/gameset.h"
#include "settings.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// ---- the name lists (redata/names, read once) ----

namespace {

struct Culture {
	std::string id;
	std::vector<std::string> male, female, byname, bynameMale, bynameFemale, ships;
	std::vector<std::pair<std::string, std::string>> epithets; // male, female form
};

struct NameLists {
	bool loaded = false;
	std::vector<Culture> cultures;
	// faction (0 imperial, 1 celestial, 2 pagan) -> (culture, weight)
	std::vector<std::pair<int, int>> factions[3];
	// units.txt: (pattern, bits)
	std::vector<std::pair<std::string, int>> units;
	std::unordered_map<const GameObjBlueprint*, int> bitsCache;
};

NameLists g_names;

std::vector<std::string> ReadLines(const char* path)
{
	std::vector<std::string> lines;
	if (!FileExists(path))
		return lines;
	char* text = nullptr;
	int size = 0;
	LoadFile(path, &text, &size);
	std::string all(text, size);
	free(text);
	size_t at = 0;
	while (at <= all.size()) {
		size_t end = all.find('\n', at);
		if (end == std::string::npos)
			end = all.size();
		std::string line = all.substr(at, end - at);
		at = end + 1;
		while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
			line.pop_back();
		size_t start = 0;
		while (start < line.size() && (line[start] == ' ' || line[start] == '\t'))
			start++;
		line = line.substr(start);
		// a byte order mark
		if (line.size() >= 3 && (unsigned char)line[0] == 0xEF && (unsigned char)line[1] == 0xBB && (unsigned char)line[2] == 0xBF)
			line = line.substr(3);
		if (line.empty() || line[0] == '#')
			continue;
		lines.push_back(line);
	}
	return lines;
}

int CultureIndex(const std::string& id)
{
	for (size_t i = 0; i < g_names.cultures.size(); i++)
		if (g_names.cultures[i].id == id)
			return (int)i;
	return -1;
}

void LoadNameLists()
{
	if (g_names.loaded)
		return;
	g_names.loaded = true;
	for (const char* id : { "english", "french", "norse", "nordic" }) {
		Culture c;
		c.id = id;
		std::string section;
		for (const std::string& line : ReadLines((std::string("names/") + id + ".txt").c_str())) {
			if (line.front() == '[' && line.back() == ']') {
				section = line.substr(1, line.size() - 2);
				continue;
			}
			if (section == "male") c.male.push_back(line);
			else if (section == "female") c.female.push_back(line);
			else if (section == "byname") c.byname.push_back(line);
			else if (section == "byname-male") c.bynameMale.push_back(line);
			else if (section == "byname-female") c.bynameFemale.push_back(line);
			else if (section == "ship") c.ships.push_back(line);
			else if (section == "epithet") {
				const size_t bar = line.find('|');
				if (bar == std::string::npos)
					c.epithets.emplace_back(line, line);
				else
					c.epithets.emplace_back(line.substr(0, bar), line.substr(bar + 1));
			}
		}
		if (!c.male.empty())
			g_names.cultures.push_back(std::move(c));
	}
	for (const std::string& line : ReadLines("names/factions.txt")) {
		char faction[32] = {};
		int n = 0;
		if (sscanf(line.c_str(), "%31s%n", faction, &n) != 1)
			continue;
		const int f = !strcmp(faction, "imperial") ? 0 : !strcmp(faction, "celestial") ? 1 : !strcmp(faction, "pagan") ? 2 : -1;
		if (f < 0)
			continue;
		const char* p = line.c_str() + n;
		char culture[32];
		int weight = 0, m = 0;
		while (sscanf(p, "%31s %d%n", culture, &weight, &m) == 2) {
			const int c = CultureIndex(culture);
			if (c >= 0 && weight > 0)
				g_names.factions[f].emplace_back(c, weight);
			p += m;
		}
	}
	// without factions.txt: every culture alike
	for (auto& f : g_names.factions)
		if (f.empty())
			for (size_t c = 0; c < g_names.cultures.size(); c++)
				f.emplace_back((int)c, 1);
	std::string section;
	for (const std::string& line : ReadLines("names/units.txt")) {
		if (line.front() == '[' && line.back() == ']') {
			section = line.substr(1, line.size() - 2);
			continue;
		}
		const int bits = section == "untracked" ? UnitHistory::U_UNTRACKED : section == "peasant" ? UnitHistory::U_PEASANT
			: section == "conscript" ? UnitHistory::U_CONSCRIPT : section == "female" ? UnitHistory::U_FEMALE
			: section == "ship" ? UnitHistory::U_SHIP : section == "noname" ? UnitHistory::U_NONAME
			: section == "ownname" ? UnitHistory::U_OWNNAME : section == "pagan" ? UnitHistory::U_PAGAN
			: section == "celestial" ? UnitHistory::U_CELESTIAL : 0;
		if (bits)
			g_names.units.emplace_back(line, bits);
	}
}

bool PatternMatches(const std::string& pattern, const std::string& name)
{
	if (pattern.empty())
		return false;
	if (pattern.back() == '*')
		return name.compare(0, pattern.size() - 1, pattern, 0, pattern.size() - 1) == 0;
	if (pattern.front() == '*') {
		const size_t len = pattern.size() - 1;
		return name.size() >= len && name.compare(name.size() - len, len, pattern, 1, len) == 0;
	}
	return pattern == name;
}

// the names' own random numbers (splitmix64): no SimRand
struct NameRandom {
	uint64_t state;
	NameRandom(uint64_t seed, uint32_t player, uint64_t n, uint64_t salt)
		: state(seed ^ (player * 0xD1B54A32D192ED03ull) ^ ((n + 1) * 0x8CB92BA72F3D8DD7ull) ^ salt) {}
	uint64_t next()
	{
		uint64_t z = (state += 0x9E3779B97F4A7C15ull);
		z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
		z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
		return z ^ (z >> 31);
	}
	size_t below(size_t n) { return n ? (size_t)(next() % n) : 0; }
	template<typename T> const T& pick(const std::vector<T>& v) { return v[below(v.size())]; }
};

constexpr uint64_t SALT_NAME = 0x6E616D65ull, SALT_HERO = 0x6865726Full;

int PickCulture(NameRandom& rnd, int faction)
{
	const auto& weights = g_names.factions[std::clamp(faction, 0, 2)];
	int total = 0;
	for (auto& [c, w] : weights)
		total += w;
	if (total <= 0)
		return 0;
	int r = (int)rnd.below((size_t)total);
	for (auto& [c, w] : weights) {
		if (r < w)
			return c;
		r -= w;
	}
	return weights.front().first;
}

// "{father}s" is the father's name in the genitive (Norse: Helgi -> Helga, Björn -> Björns;
// Nils -> Nils); "{father}" alone is the name as it is (Godwinson)
std::string FillByname(const std::string& pattern, const std::string& father)
{
	std::string out = pattern;
	size_t at = out.find("{father}s");
	if (at != std::string::npos) {
		std::string gen = father;
		if (!gen.empty() && gen.back() == 'i')
			gen.back() = 'a';
		else if (!gen.empty() && gen.back() != 's')
			gen += 's';
		out.replace(at, 9, gen);
		return out;
	}
	at = out.find("{father}");
	if (at != std::string::npos)
		out.replace(at, 8, father);
	return out;
}

const char* Ordinal(int n)
{
	static const char* roman[] = { "", "", " II", " III", " IV", " V", " VI", " VII", " VIII", " IX", " X" };
	return n >= 2 && n <= 10 ? roman[n] : "";
}

std::string Quote(const std::string& s)
{
	std::string out = "\"";
	for (char c : s)
		if (c != '"' && c != '\n' && c != '\r')
			out += c;
	return out + "\"";
}

// the line's words, "quoted" ones whole
std::vector<std::string> Words(const std::string& line)
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

bool IsFormation(const ServerGameObject* obj)
{
	return obj && obj->blueprint->bpClass == Tags::GAMEOBJCLASS_FORMATION;
}

} // namespace

// ---- records ----

std::string UnitHistory::Record::displayName() const
{
	if (hero && !epithet.empty())
		return (given.empty() ? name : given) + " " + epithet;
	return name;
}

uint8_t UnitHistory::Record::flags() const
{
	return (hero ? F_HERO : 0) | (ageUnknown ? F_AGE_UNKNOWN : 0) | (death >= 0.0f ? F_DEAD : 0) | (female ? F_FEMALE : 0);
}

int UnitHistory::unitBits(const GameObjBlueprint* bp)
{
	LoadNameLists();
	auto it = g_names.bitsCache.find(bp);
	if (it != g_names.bitsCache.end())
		return it->second;
	int bits = 0;
	for (const auto& [pattern, b] : g_names.units)
		if (PatternMatches(pattern, bp->name))
			bits |= b;
	g_names.bitsCache[bp] = bits;
	return bits;
}

bool UnitHistory::trackable(const GameObjBlueprint* bp)
{
	if (!bp || bp->bpClass != Tags::GAMEOBJCLASS_CHARACTER)
		return false;
	return !(unitBits(bp) & (U_UNTRACKED | U_PEASANT));
}

int UnitHistory::factionOf(const GameObjBlueprint* bp, uint32_t player)
{
	const int bits = unitBits(bp);
	if (bits & U_PAGAN)
		return 2;
	if (bits & U_CELESTIAL)
		return 1;
	// the player's: the faction whose units it has more of
	const PlayerNames& p = players[player];
	if (p.pagan > p.celestial)
		return 2;
	if (p.celestial > p.pagan)
		return 1;
	return 0;
}

void UnitHistory::ensureSeed(Server& server)
{
	if (seeded)
		return;
	seeded = true;
	// a new game: the seed startLevel gave it (setup.json records it). A save loaded without the
	// history: what the save restored (the next id, the clock), so that the same save names alike.
	// A recording replays with the same names, as it loads them from its start.sav's side file.
	const uint64_t base = startedLevel ? (uint64_t)server.lastSeed
		: ((uint64_t)server.nextUniqueId << 20) ^ (uint64_t)(server.timeManager.currentTime * 1000.0f);
	nameSeed = (base << 32) ^ 0x756E697468697374ull ^ base;
}

std::string UnitHistory::drawName(uint32_t player, int faction, bool female, bool ship, int* cultureOut, std::string* givenOut)
{
	LoadNameLists();
	if (g_names.cultures.empty())
		return std::string();
	PlayerNames& p = players[player];
	NameRandom rnd(nameSeed, player, p.named++, SALT_NAME);
	const int culture = PickCulture(rnd, faction);
	const Culture& c = g_names.cultures[culture];
	if (cultureOut)
		*cultureOut = culture;
	std::string name, given;
	for (int attempt = 0; attempt < 9; attempt++) {
		if (ship) {
			if (c.ships.empty())
				return std::string();
			name = given = rnd.pick(c.ships);
		}
		else {
			const auto& givens = female && !c.female.empty() ? c.female : c.male;
			given = rnd.pick(givens);
			std::vector<const std::string*> bynames;
			for (const auto& b : c.byname)
				bynames.push_back(&b);
			for (const auto& b : female ? c.bynameFemale : c.bynameMale)
				bynames.push_back(&b);
			name = given;
			if (!bynames.empty()) {
				const std::string& pattern = *bynames[rnd.below(bynames.size())];
				name += " " + FillByname(pattern, rnd.pick(c.male));
			}
		}
		if (!p.used.count(name))
			break;
	}
	// still taken after 9 draws: an ordinal (Sigurd Ironside II)
	if (p.used.count(name)) {
		for (int n = 2; n <= 10; n++)
			if (!p.used.count(name + Ordinal(n))) {
				name += Ordinal(n);
				break;
			}
	}
	p.used.insert(name);
	if (givenOut)
		*givenOut = ship ? name : given;
	return name;
}

UnitHistory::Record* UnitHistory::track(Server& server, ServerGameObject* obj, float birth, bool ageUnknown, bool female)
{
	if (auto it = records.find(obj->id); it != records.end())
		return &it->second;
	ensureSeed(server);
	ServerGameObject* player = obj->getPlayer();
	Record rec;
	rec.id = obj->id;
	rec.player = player ? player->id : 0;
	rec.type = obj->blueprint->name;
	rec.birth = birth;
	rec.ageUnknown = ageUnknown;
	const int bits = unitBits(obj->blueprint);
	rec.female = female || (bits & U_FEMALE);
	rec.kind = (bits & U_SHIP) ? KIND_SHIP : (bits & U_NONAME) ? KIND_NONAME : (bits & U_OWNNAME) ? KIND_OWNNAME : KIND_PERSON;
	PlayerNames& p = players[rec.player];
	if (bits & U_PAGAN)
		p.pagan++;
	if (bits & U_CELESTIAL)
		p.celestial++;
	if (!obj->name.empty()) {
		// named by the map or a script: it keeps it (obj->name itself is never touched)
		rec.name = rec.given = obj->name;
		rec.kind = rec.kind == KIND_PERSON ? KIND_OWNNAME : rec.kind;
	}
	else if (rec.kind == KIND_OWNNAME)
		rec.name = rec.given = obj->blueprint->name;
	else if (rec.kind == KIND_PERSON || rec.kind == KIND_SHIP)
		rec.name = drawName(rec.player, factionOf(obj->blueprint, rec.player), rec.female, rec.kind == KIND_SHIP, &rec.culture, &rec.given);
	if (ServerGameObject* parent = obj->getParent(); IsFormation(parent))
		rec.formation = parent->id;
	Record& stored = records[obj->id] = std::move(rec);
	sendRecord(server, stored);
	if (trace)
		printf("UNITHISTORY t=%.2f tracked %u %s: \"%s\"\n", (float)server.timeManager.currentTime, obj->id, obj->blueprint->name.c_str(), stored.name.c_str());
	return &stored;
}

void UnitHistory::untrack(Server& server, uint32_t id)
{
	auto it = records.find(id);
	if (it == records.end())
		return;
	records.erase(it);
	NetPacketWriter npw{ NETCLIMSG_UNIT_UNTRACKED };
	npw.writeUint32(id);
	server.sendToAll(npw);
	if (trace)
		printf("UNITHISTORY t=%.2f untracked %u\n", (float)server.timeManager.currentTime, id);
}

void UnitHistory::sendRecord(Server& server, const Record& rec, bool newHero, NetLink* link) const
{
	NetPacketWriter npw{ NETCLIMSG_UNIT_HISTORY };
	npw.writeUint32(rec.id);
	npw.writeFloat(rec.birth);
	npw.writeUint8(rec.kind);
	npw.writeUint8(rec.flags() | (newHero ? F_NEW_HERO : 0));
	npw.writeStringZ(rec.displayName().substr(0, 100));
	npw.writeStringZ(rec.name.substr(0, 100));
	if (link)
		link->send(npw);
	else
		server.sendToAll(npw);
}

void UnitHistory::sendAllTo(Server& server, NetLink* link) const
{
	NetPacketWriter npw{ NETCLIMSG_HERO_THRESHOLD };
	npw.writeUint32((uint32_t)threshold);
	link->send(npw);
	for (const auto& [id, rec] : records)
		if (rec.death < 0.0f)
			sendRecord(server, rec, false, link);
}

// ---- the game's events ----

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
	LoadNameLists();
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
	// a unit waiting to become a hero (it left a formation, or its formation's hero went) becomes
	// one at its next fight
	for (uint32_t id : { by, victim->id }) {
		auto it = records.find(id);
		if (it != records.end() && it->second.heroPending && it->second.death < 0.0f)
			if (ServerGameObject* obj = server.findObject(id))
				tryHero(server, obj, it->second);
	}
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

void UnitHistory::spawned(Server& server, ServerGameObject* obj)
{
	if (!obj || !trackable(obj->blueprint))
		return;
	setup(server.gameSet.get());
	track(server, obj, (float)server.timeManager.currentTime, false, false);
}

void UnitHistory::converted(Server& server, ServerGameObject* obj, const GameObjBlueprint* prevbp, const std::string& prevSubtype)
{
	if (!obj || obj->blueprint->bpClass != Tags::GAMEOBJCLASS_CHARACTER)
		return;
	setup(server.gameSet.get());
	auto it = records.find(obj->id);
	const int bits = unitBits(obj->blueprint);
	if (it != records.end()) {
		Record& rec = it->second;
		rec.type = obj->blueprint->name;
		// a conscript back to the fields: with no kill (and no hero) it is untracked again, a
		// new name if it is conscripted again (the user: one that killed stays tracked)
		if ((bits & U_PEASANT) && rec.conscript && obj->kills == 0 && obj->buildingsDestroyed == 0 && !rec.hero)
			untrack(server, obj->id);
		return;
	}
	if (!trackable(obj->blueprint))
		return;
	// conscripted (Mechanics/Convert to Militia.cpp: CONVERT_TO "Militia"): tracked from now, its
	// age is its time under arms. The Militia has one subtype: the peasant's tells a woman.
	const bool fromPeasant = prevbp && (unitBits(prevbp) & U_PEASANT);
	Record* rec = track(server, obj, (float)server.timeManager.currentTime, false, fromPeasant && prevSubtype == "Female");
	if (rec && fromPeasant) {
		rec->conscript = true;
		// kills it made as an untracked peasant don't count
		obj->kills = 0;
		obj->buildingsDestroyed = 0;
	}
}

bool UnitHistory::formationHasHero(Server& server, ServerGameObject* formation, uint32_t except) const
{
	if (!formation)
		return false;
	for (const auto& [type, members] : formation->children)
		for (CommonGameObject* m : members) {
			if (m->id == except)
				continue;
			auto it = records.find(m->id);
			if (it != records.end() && it->second.hero && it->second.death < 0.0f)
				return true;
		}
	return false;
}

void UnitHistory::parentChanged(Server& server, ServerGameObject* obj, ServerGameObject* oldParent, ServerGameObject* newParent)
{
	auto it = records.find(obj->id);
	if (it == records.end())
		return;
	Record& rec = it->second;
	if (newParent && newParent->blueprint->bpClass == Tags::GAMEOBJCLASS_PLAYER)
		rec.player = newParent->id;
	const bool leaves = IsFormation(oldParent) && rec.formation == oldParent->id;
	const bool joins = IsFormation(newParent);
	if (leaves) {
		setup(server.gameSet.get());
		// dying is no leaving (Death.cpp hands the dying to its player): it keeps all
		const bool dying = (obj->flags & ServerGameObject::fTerminated) || (hitPoints >= 0 && obj->getItem(hitPoints) <= 0.0f);
		if (!dying && !rec.hero && rec.formationKills > 0) {
			// half the kills made in the formation stay with it (rounded down: 7 -> keeps 4)
			const int lost = rec.formationKills / 2;
			obj->kills = std::max(0, obj->kills - lost);
			sendKills(server, obj, 0);
			if (trace)
				printf("UNITHISTORY t=%.2f %u left formation %u: %d of %d formation kills lost, %d kills\n", (float)server.timeManager.currentTime,
					obj->id, oldParent->id, lost, rec.formationKills, obj->kills);
		}
		rec.formation = 0;
		rec.formationKills = 0;
		// leaving makes no one a hero: one with enough kills becomes one at its next fight
		if (!rec.hero && !dying && threshold > 0 && obj->kills >= threshold && rec.kind != KIND_SHIP && rec.kind != KIND_NONAME)
			rec.heroPending = true;
		// a hero gone: those of the formation who qualify wait for their next fight or kill
		if (rec.hero && !formationHasHero(server, oldParent, obj->id))
			for (const auto& [type, members] : oldParent->children)
				for (CommonGameObject* m : members)
					if (auto mt = records.find(m->id); mt != records.end() && !mt->second.hero && threshold > 0 && m->kills >= threshold)
						mt->second.heroPending = true;
	}
	if (joins) {
		rec.formation = newParent->id;
		rec.formationKills = 0;
	}
}

void UnitHistory::tryHero(Server& server, ServerGameObject* obj, Record& rec)
{
	if (rec.hero || threshold <= 0 || rec.kind == KIND_SHIP || rec.kind == KIND_NONAME || rec.death >= 0.0f)
		return;
	if (obj->kills < threshold) {
		rec.heroPending = false;
		return;
	}
	ServerGameObject* formation = rec.formation ? server.findObject(rec.formation) : nullptr;
	if (formation && IsFormation(formation)) {
		// one hero is made in a formation: while it has one, nobody else becomes one there
		if (formationHasHero(server, formation)) {
			rec.heroPending = true;
			return;
		}
		// none yet: the member who qualifies with the most kills (equal: the lowest id)
		ServerGameObject* best = obj;
		for (const auto& [type, members] : formation->children)
			for (CommonGameObject* m : members) {
				auto mt = records.find(m->id);
				if (mt == records.end() || mt->second.hero || mt->second.death >= 0.0f || mt->second.kind == KIND_SHIP || mt->second.kind == KIND_NONAME)
					continue;
				if (m->kills >= threshold && (m->kills > best->kills || (m->kills == best->kills && m->id < best->id)))
					best = (ServerGameObject*)m;
			}
		Record& bestRec = records[best->id];
		makeHero(server, best, bestRec);
		if (best != obj)
			rec.heroPending = true;
		return;
	}
	makeHero(server, obj, rec);
}

std::string UnitHistory::drawEpithet(const Record& rec)
{
	LoadNameLists();
	if (g_names.cultures.empty())
		return std::string();
	const int culture = rec.culture >= 0 && rec.culture < (int)g_names.cultures.size() ? rec.culture : 0;
	const Culture& c = g_names.cultures[culture];
	if (c.epithets.empty())
		return std::string();
	PlayerNames& p = players[rec.player];
	NameRandom rnd(nameSeed, rec.player, p.heroes++, SALT_HERO);
	std::string epithet;
	for (int attempt = 0; attempt < 9; attempt++) {
		const auto& e = rnd.pick(c.epithets);
		epithet = rec.female ? e.second : e.first;
		// another hero of the player with the same name and epithet: another draw
		bool taken = false;
		for (const auto& [id, other] : records)
			if (other.hero && other.player == rec.player && other.given == rec.given && other.epithet == epithet)
				taken = true;
		if (!taken)
			break;
	}
	return epithet;
}

void UnitHistory::makeHero(Server& server, ServerGameObject* obj, Record& rec)
{
	rec.hero = true;
	rec.heroPending = false;
	if (ServerGameObject* player = obj->getPlayer())
		rec.player = player->id;
	rec.epithet = drawEpithet(rec);
	sendRecord(server, rec, true);
	if (trace)
		printf("UNITHISTORY t=%.2f hero %u: \"%s\" is now \"%s\" (%d kills)\n", (float)server.timeManager.currentTime, obj->id, rec.name.c_str(),
			rec.displayName().c_str(), obj->kills);
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
	setup(server.gameSet.get());
	// a death, not a removal (going into a building, a conversion, REMOVE): no hit points left
	const int item = cls == Tags::GAMEOBJCLASS_CHARACTER ? hitPoints : structuralIntegrity;
	const bool died = item >= 0 && obj->getItem(item) <= 0.0f;
	auto vit = records.find(obj->id);
	Record* victimRec = vit != records.end() ? &vit->second : nullptr;
	const float now = (float)server.timeManager.currentTime;
	const bool credited = died && obj->lastHitBy && now - obj->lastHitTime <= creditWindow;
	// a foundation pulled down is no building destroyed
	static const std::string suffix = " Foundation";
	const std::string& name = obj->blueprint->name;
	const bool foundation = cls == Tags::GAMEOBJCLASS_BUILDING && name.size() > suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0;
	ServerGameObject* killer = nullptr;
	Record* killerRec = nullptr;
	if (credited && !foundation) {
		const uint32_t victimId = obj->id;
		const uint32_t killerId = obj->lastHitBy;
		obj->lastHitBy = 0;
		killer = server.findObject(killerId);
		auto kit = records.find(killerId);
		killerRec = kit != records.end() ? &kit->second : nullptr;
		if (killer && !killer->deleted && isEnemy(server, killer, obj)) {
			// a peasant that was never a conscript (untracked) gets no kill
			const bool counts = killer->blueprint->bpClass != Tags::GAMEOBJCLASS_CHARACTER || killerRec || trackable(killer->blueprint);
			if (counts) {
				if (cls == Tags::GAMEOBJCLASS_CHARACTER)
					killer->kills++;
				else
					killer->buildingsDestroyed++;
				// a tracked unit made outside spawnObject (a script's, the editor's): from now
				if (!killerRec && trackable(killer->blueprint))
					killerRec = track(server, killer, now, true, false);
				if (trace)
					printf("UNITHISTORY t=%.2f %s %u %s by %u %s: kills %d, buildings %d\n", now,
						cls == Tags::GAMEOBJCLASS_CHARACTER ? "killed" : "destroyed", victimId, name.c_str(), killerId, killer->blueprint->name.c_str(),
						killer->kills, killer->buildingsDestroyed);
				sendKills(server, killer, victimId);
				if (killerRec && cls == Tags::GAMEOBJCLASS_CHARACTER) {
					if (killerRec->formation) {
						killerRec->formationKills++;
						// the formation's tally: every kill made in it
						if (ServerGameObject* f = server.findObject(killerRec->formation); f && IsFormation(f)) {
							f->kills++;
							sendKills(server, f, victimId);
						}
					}
					if (victimRec && victimRec->hero)
						killerRec->slew.push_back(obj->id);
					tryHero(server, killer, *killerRec);
				}
			}
			else
				killer = nullptr;
		}
		else if (!killer && killerRec && killerRec->death >= 0.0f && cls == Tags::GAMEOBJCLASS_CHARACTER) {
			// a firer dead since its missile left: the kill is still its own
			ServerGameObject* kp = server.findObject(killerRec->player);
			ServerGameObject* vp = obj->getPlayer();
			if (kp && vp && kp != vp && vp->blueprint->name != "Neutral Player" && server.getDiplomaticStatus(kp, vp) >= 2) {
				killerRec->kills++;
				if (victimRec && victimRec->hero)
					killerRec->slew.push_back(obj->id);
			}
			killer = nullptr;
		}
		else
			killer = nullptr;
	}
	else
		obj->lastHitBy = 0;
	// the record of a tracked unit that died
	if (victimRec && died && cls == Tags::GAMEOBJCLASS_CHARACTER) {
		victimRec->death = now;
		victimRec->kills = obj->kills;
		victimRec->buildings = obj->buildingsDestroyed;
		victimRec->heroPending = false;
		if (ServerGameObject* player = obj->getPlayer())
			victimRec->player = player->id;
		if (killer) {
			victimRec->slainBy = killer->id;
			auto kit = records.find(killer->id);
			victimRec->slainByText = kit != records.end() && !kit->second.displayName().empty() ? kit->second.displayName() : killer->blueprint->name;
		}
		if (trace && victimRec->hero)
			printf("UNITHISTORY t=%.2f hero fallen %u \"%s\" (%d kills), slain by %u\n", now, obj->id, victimRec->displayName().c_str(),
				victimRec->kills, victimRec->slainBy);
		// the dead are kept for the timeline when they count: kills, a hero, a fight in an event
		if (!victimRec->hero && victimRec->kills == 0 && victimRec->buildings == 0 && !victimRec->participated && victimRec->slew.empty())
			untrack(server, obj->id);
		else
			sendRecord(server, *victimRec);
	}
}

void UnitHistory::trackAll(Server& server, bool ageUnknown)
{
	std::vector<ServerGameObject*> chars;
	for (auto& [id, cobj] : server.idmap) {
		ServerGameObject* obj = (ServerGameObject*)cobj;
		if (obj && !obj->deleted && !(obj->flags & ServerGameObject::fTerminated) && trackable(obj->blueprint) && !records.count(id))
			chars.push_back(obj);
	}
	std::sort(chars.begin(), chars.end(), [](ServerGameObject* a, ServerGameObject* b) { return a->id < b->id; });
	for (ServerGameObject* obj : chars)
		track(server, obj, ageUnknown ? (float)server.timeManager.currentTime : 0.0f, ageUnknown, false);
}

void UnitHistory::levelStarted(Server& server)
{
	setup(server.gameSet.get());
	pendingLoad = false;
	if (historyLoaded)
		return; // a save game with its history
	// the game's seed from now (a unit made before the start drew with another)
	startedLevel = true;
	seeded = false;
	ensureSeed(server);
	trackAll(server, false);
}

void UnitHistory::loaded(Server& server)
{
	// a save without the history (WKB.exe's, an older wkbai's, a level): named at the level's
	// start (a new game) or at the first step (a game going on), the ages then unknown
	if (!historyLoaded)
		pendingLoad = true;
}

void UnitHistory::update(Server& server)
{
	if (pendingLoad && server.gameSet && server.getLevel()) {
		pendingLoad = false;
		setup(server.gameSet.get());
		ensureSeed(server);
		trackAll(server, true);
	}
	// the threshold, once the lobby has set the players' population limits: the sum of all
	// players' / 10, at least 50 (the user). Fixed for the game, saved.
	if (threshold > 0 || server.timeManager.currentTime < 1.0f || !server.getLevel() || !server.gameSet)
		return;
	const int popItem = server.gameSet->items.names.getIndex("Total Population Count");
	float sum = 0.0f;
	for (auto& [type, children] : server.getLevel()->children)
		if (type.bpClass() == Tags::GAMEOBJCLASS_PLAYER)
			for (CommonGameObject* p : children)
				if (popItem >= 0)
					sum += std::max(0.0f, p->getItem(popItem));
	threshold = std::max(50, (int)(sum / 10.0f));
	if (const int forced = g_settings.value("unitHistoryThreshold", 0); forced > 0)
		threshold = forced;
	NetPacketWriter npw{ NETCLIMSG_HERO_THRESHOLD };
	npw.writeUint32((uint32_t)threshold);
	server.sendToAll(npw);
	if (trace)
		printf("UNITHISTORY t=%.2f hero threshold %d (population limits %.0f)\n", (float)server.timeManager.currentTime, threshold, sum);
	// units already past it (a save from before the history)
	for (auto& [id, rec] : records)
		if (rec.death < 0.0f)
			if (ServerGameObject* obj = server.findObject(id); obj && obj->kills >= threshold)
				tryHero(server, obj, rec);
}

// ---- save games ----

std::string UnitHistory::saveText(Server& server)
{
	UnitHistory& h = server.unitHistory;
	std::string text;
	char line[512];
	if (h.seeded) {
		snprintf(line, sizeof(line), "NAME_SEED %llu\nTHRESHOLD %d\n", (unsigned long long)h.nameSeed, h.threshold);
		text += line;
		for (const auto& [pid, p] : h.players) {
			snprintf(line, sizeof(line), "PLAYER %u %u %u %d %d\n", pid, p.named, p.heroes, p.pagan, p.celestial);
			text += line;
			for (const std::string& n : p.used)
				text += "USED " + std::to_string(pid) + " " + Quote(n) + "\n";
		}
	}
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
		snprintf(line, sizeof(line), "UNIT %u %d %d %u %u %u %.9g %u\n", obj->id, obj->kills, obj->buildingsDestroyed, obj->creatorId,
			obj->creditId, obj->lastHitBy, obj->lastHitTime, obj->garrisonCredit);
		text += line;
	}
	for (const auto& [id, r] : h.records) {
		const int flags = (r.hero ? 1 : 0) | (r.heroPending ? 2 : 0) | (r.ageUnknown ? 4 : 0) | (r.conscript ? 8 : 0) | (r.participated ? 16 : 0) | (r.female ? 32 : 0);
		snprintf(line, sizeof(line), "RECORD %u %u %d %d %.9g %.9g %d %d %d %u %d %u ", r.id, r.player, (int)r.kind, r.culture, r.birth, r.death,
			flags, r.kills, r.buildings, r.formation, r.formationKills, r.slainBy);
		text += line;
		text += Quote(r.type) + " " + Quote(r.name) + " " + Quote(r.given) + " " + Quote(r.epithet) + " " + Quote(r.slainByText) + " " + Quote(r.cause) + "\n";
		for (uint32_t v : r.slew)
			text += "SLEW " + std::to_string(r.id) + " " + std::to_string(v) + "\n";
	}
	return text;
}

void UnitHistory::loadText(Server& server, const std::string& all)
{
	UnitHistory& h = server.unitHistory;
	h.setup(server.gameSet.get());
	size_t at = 0;
	while (at < all.size()) {
		size_t end = all.find('\n', at);
		if (end == std::string::npos)
			end = all.size();
		std::string line = all.substr(at, end - at);
		at = end + 1;
		if (!line.empty() && line.back() == '\r')
			line.pop_back();
		if (line.compare(0, 5, "UNIT ") == 0) {
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
			continue;
		}
		const std::vector<std::string> w = Words(line);
		if (w.empty())
			continue;
		if (w[0] == "NAME_SEED" && w.size() >= 2) {
			h.nameSeed = strtoull(w[1].c_str(), nullptr, 10);
			h.seeded = h.historyLoaded = true;
		}
		else if (w[0] == "THRESHOLD" && w.size() >= 2)
			h.threshold = atoi(w[1].c_str());
		else if (w[0] == "PLAYER" && w.size() >= 6) {
			PlayerNames& p = h.players[(uint32_t)strtoul(w[1].c_str(), nullptr, 10)];
			p.named = (uint32_t)strtoul(w[2].c_str(), nullptr, 10);
			p.heroes = (uint32_t)strtoul(w[3].c_str(), nullptr, 10);
			p.pagan = atoi(w[4].c_str());
			p.celestial = atoi(w[5].c_str());
		}
		else if (w[0] == "USED" && w.size() >= 3)
			h.players[(uint32_t)strtoul(w[1].c_str(), nullptr, 10)].used.insert(w[2]);
		else if (w[0] == "RECORD" && w.size() >= 19) {
			Record r;
			r.id = (uint32_t)strtoul(w[1].c_str(), nullptr, 10);
			r.player = (uint32_t)strtoul(w[2].c_str(), nullptr, 10);
			r.kind = (uint8_t)atoi(w[3].c_str());
			r.culture = atoi(w[4].c_str());
			r.birth = (float)atof(w[5].c_str());
			r.death = (float)atof(w[6].c_str());
			const int flags = atoi(w[7].c_str());
			r.hero = flags & 1;
			r.heroPending = flags & 2;
			r.ageUnknown = flags & 4;
			r.conscript = flags & 8;
			r.participated = flags & 16;
			r.female = flags & 32;
			r.kills = atoi(w[8].c_str());
			r.buildings = atoi(w[9].c_str());
			r.formation = (uint32_t)strtoul(w[10].c_str(), nullptr, 10);
			r.formationKills = atoi(w[11].c_str());
			r.slainBy = (uint32_t)strtoul(w[12].c_str(), nullptr, 10);
			r.type = w[13];
			r.name = w[14];
			r.given = w[15];
			r.epithet = w[16];
			r.slainByText = w[17];
			r.cause = w[18];
			h.records[r.id] = std::move(r);
		}
		else if (w[0] == "SLEW" && w.size() >= 3) {
			auto it = h.records.find((uint32_t)strtoul(w[1].c_str(), nullptr, 10));
			if (it != h.records.end())
				it->second.slew.push_back((uint32_t)strtoul(w[2].c_str(), nullptr, 10));
		}
	}
	// to the clients
	if (h.threshold > 0) {
		NetPacketWriter npw{ NETCLIMSG_HERO_THRESHOLD };
		npw.writeUint32((uint32_t)h.threshold);
		server.sendToAll(npw);
	}
	for (const auto& [id, rec] : h.records)
		h.sendRecord(server, rec);
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
