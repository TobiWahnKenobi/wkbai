// wkbai - unit history: who killed whom, names, ages and heroes (the RE repo's
// docs/engine/unit-history.md)

#pragma once

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>
#include "ServerChronicle.h"

struct Server;
struct ServerGameObject;
struct GameSet;
struct GameObjBlueprint;
struct NetLink;

// The kills, worked out on the server. The WKB game set's killer is the sender of the attack
// package (Injured By Attack, Missile Attack, Non Specific Attack, Spew Attack) that takes the
// hit points to 0 (Mechanics/Death.cpp "Being Killed" -> Scoring.cpp "I Have Killed Sequence
// Executor" FINDER_PACKAGE_SENDER). So each attack package notes its sender on the victim
// (followed from a missile to whoever fired it), and a terminated character or building with no
// hit points left, hit within the last 15 s, is that unit's kill.
//
// The tracked units (every character but peasants, animals and helpers; conscripts too) get a
// record: a name from redata/names by their faction, drawn from a stream of its own (no SimRand
// number: the game and its recordings stay as they are), their birth, their kills. One that kills
// as many enemies as the threshold (the players' population limits added up / 10, at least 50)
// becomes a hero with an epithet ("Sigurd the Valiant"). In a formation at most one hero is made;
// a member that is no hero and leaves keeps half the kills it made in it.
struct UnitHistory {
	// what the history does with a unit type (redata/names/units.txt)
	enum Kind : uint8_t { KIND_PERSON = 0, KIND_SHIP = 1, KIND_NONAME = 2, KIND_OWNNAME = 3 };
	// a record's flags (also sent to the clients, NETCLIMSG_UNIT_HISTORY)
	enum Flags : uint8_t { F_HERO = 1, F_AGE_UNKNOWN = 2, F_NEW_HERO = 4, F_DEAD = 8, F_FEMALE = 16 };

	struct Record {
		uint32_t id = 0, player = 0;
		std::string type; // the blueprint's name (for the dead)
		std::string name; // the birth name: "Sigurd Ironside", a ship's, the type's own (Michael); none: siege engines
		std::string given; // what the epithet follows: "Sigurd" (a map's or the type's name: all of it)
		std::string epithet; // a hero's: "the Valiant"
		int culture = -1;
		bool female = false;
		uint8_t kind = KIND_PERSON;
		float birth = 0.0f, death = -1.0f;
		bool ageUnknown = false, hero = false, heroPending = false, conscript = false, participated = false;
		int kills = 0, buildings = 0; // at its death (alive: the object's)
		uint32_t formation = 0; // the formation it is in (0: none)
		int formationKills = 0; // its kills since it joined that formation
		uint32_t slainBy = 0; // its killer (0: none)
		std::string slainByText; // the killer as named at the time
		std::string cause; // a death without a killer: "the cold", ...
		std::vector<uint32_t> slew; // the heroes it slew
		// "Sigurd the Valiant" for a hero, else the birth name (none: siege engines)
		std::string displayName() const;
		uint8_t flags() const;
	};

	// the attack events and the items read by name, for this game set (again when it changes)
	void setup(const GameSet* gs);
	bool isAttack(int evt) const { return evt >= 0 && evt < (int)attackEvents.size() && attackEvents[evt]; }

	// every package (ServerGameObject::sendEvent): an attack one notes its sender on the victim
	void onEvent(Server& server, ServerGameObject* victim, int evt, ServerGameObject* sender);
	// an attack package reached the victim from the sender
	void hit(Server& server, ServerGameObject* victim, ServerGameObject* sender);
	// obj was made by an OBJECT_CREATION of creator
	void created(Server& server, ServerGameObject* obj, ServerGameObject* creator);
	// obj was spawned (Server::spawnObject), under its player: tracked if it is to be
	void spawned(Server& server, ServerGameObject* obj);
	// obj was converted from prevbp (whose subtype was prevSubtype): conscripted, sent back to
	// the fields, upgraded
	void converted(Server& server, ServerGameObject* obj, const GameObjBlueprint* prevbp, const std::string& prevSubtype);
	// obj's parent changed (setParent): it joined or left a formation
	void parentChanged(Server& server, ServerGameObject* obj, ServerGameObject* oldParent, ServerGameObject* newParent);
	// obj is being terminated
	void terminated(Server& server, ServerGameObject* obj);
	// obj is being deleted (Server::deleteObject; a removal need not terminate it first): a living
	// record keeps its kills as they are
	void deleted(ServerGameObject* obj);
	// a game starts (Server::startLevel): the characters on the map are tracked, born at 0
	void levelStarted(Server& server);
	// a save game was loaded: one without the history (WKB.exe's, older) names its units now,
	// their ages unknown
	void loaded(Server& server);
	// once a simulation step: the threshold once the game runs
	void update(Server& server);
	// a unit dealt or took a hit inside a fight's area (ServerChronicle's scan): one waiting to
	// become a hero (it left a formation, its formation's hero went) becomes one now
	void reachedFight(Server& server, uint32_t id);
	// what harms the unit now without a killer (the weather: "froze to death", "drowned",
	// "struck by lightning", ...): its death soon after, unless a unit is credited, has that cause
	void noteCause(uint32_t id, const char* cause, float now);
	static constexpr float causeWindow = 5.0f; // s
	// the character or building credited with a package from sender (0: none): a missile's (or
	// a fire's, a mine's) creator, a garrisoned building's missile its troop
	uint32_t resolve(Server& server, ServerGameObject* sender) const;

	const Record* find(uint32_t id) const { auto it = records.find(id); return it != records.end() ? &it->second : nullptr; }
	bool tracked(uint32_t id) const { return records.count(id) != 0; }
	int heroThreshold() const { return threshold; }

	// the kill counts changed (killer, kills, buildings destroyed) to the clients
	static void sendKills(Server& server, ServerGameObject* killer, uint32_t victim);
	// a record to the clients (or that one link)
	void sendRecord(Server& server, const Record& rec, bool newHero = false, NetLink* link = nullptr) const;
	// every record and the threshold to one client that joins (sendStateTo)
	void sendAllTo(Server& server, NetLink* link) const;

	// the save game's side file <save>.history.txt: a line per object with any of it
	// ("UNIT <id> <kills> <buildings> <creator> <credit> <last hit by> <last hit time> <garrison credit>"),
	// the name stream and the threshold, a RECORD line per tracked unit
	static std::string saveText(Server& server);
	static void loadText(Server& server, const std::string& text);

	static constexpr float creditWindow = 15.0f; // s: a hit older than this doesn't kill

	// "unitHistoryTrace" (WKB_TRACE_KILLS): each kill and hit printed
	bool trace = false;

	// the records, alive and dead, by unit id
	std::map<uint32_t, Record> records;
	// the names' stream (the game's seed), per player the names used and the draws so far
	uint64_t nameSeed = 0;
	bool seeded = false;
	struct PlayerNames { std::set<std::string> used; uint32_t named = 0, heroes = 0; int pagan = 0, celestial = 0; };
	std::map<uint32_t, PlayerNames> players;
	// kills to become a hero (0: not worked out yet)
	int threshold = 0;
	// the timeline and the statistics, on the server
	ServerChronicle chronicle;

	// a unit type's place in redata/names/units.txt (bits: U_*)
	enum UnitBits { U_UNTRACKED = 1, U_PEASANT = 2, U_CONSCRIPT = 4, U_FEMALE = 8, U_SHIP = 16, U_NONAME = 32, U_OWNNAME = 64, U_PAGAN = 128, U_CELESTIAL = 256 };
	static int unitBits(const GameObjBlueprint* bp);
	// a character that gets a record when it is made
	static bool trackable(const GameObjBlueprint* bp);

	// the names drawn for a player (tests): a person's (female) of the faction (0 imperial,
	// 1 celestial, 2 pagan), a ship's
	std::string drawName(uint32_t player, int faction, bool female, bool ship, int* cultureOut = nullptr, std::string* givenOut = nullptr);

private:
	const GameSet* gs = nullptr;
	std::vector<bool> attackEvents;
	int hitPoints = -1, structuralIntegrity = -1, gunnerItem = -1, lightInfantryItem = -1, garrisonCategory = -1;
	bool isEnemy(Server& server, ServerGameObject* killer, ServerGameObject* victim) const;
	// a garrisoned building's next shooter for this missile (0: none inside)
	uint32_t garrisonShooter(Server& server, ServerGameObject* building, ServerGameObject* missile) const;
	Record* track(Server& server, ServerGameObject* obj, float birth, bool ageUnknown, bool female);
	void untrack(Server& server, uint32_t id);
	void ensureSeed(Server& server);
	void trackAll(Server& server, bool ageUnknown);
	bool pendingLoad = false, startedLevel = false, historyLoaded = false;
	// the causes noted (noteCause): the cause and when, by unit id (not saved: seconds long)
	std::map<uint32_t, std::pair<const char*, float>> causes;
	// the record becomes a hero if it may (the threshold, its formation's hero)
	void tryHero(Server& server, ServerGameObject* obj, Record& rec);
	void makeHero(Server& server, ServerGameObject* obj, Record& rec);
	bool formationHasHero(Server& server, ServerGameObject* formation, uint32_t except = 0) const;
	std::string drawEpithet(const Record& rec);
	int factionOf(const GameObjBlueprint* bp, uint32_t player);
};
