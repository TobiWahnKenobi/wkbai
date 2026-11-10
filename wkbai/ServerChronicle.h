// wkbai - the timeline and the statistics kept by the server (the RE repo's
// docs/engine/unit-history.md, section 1)

#pragma once

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>
#include "interface/GameChronicle.h"

struct Server;
struct NetLink;

// The same scans as a client's GameChronicle (battles, sieges, raids, settlements founded and
// destroyed, the players' samples), run in the simulation on the server's objects and the game
// clock, so that a replay makes the same events, the save game keeps them, and each event knows
// who fought in it: a unit that dealt or took a hit (an attack package) in its area while it went
// on took part. Each event names each side's unit with the most kills there, the heroes who
// fought there (named later too, when one becomes a hero) and the heroes who fell there; a hero
// slain outside any fight is an event of its own (Fallen). The clients receive the events and
// samples as text (NETCLIMSG_CHRONICLE) and filter them by their own fog of war.
struct ServerChronicle {
	GameChronicle chron;

	// once a simulation step
	void update(Server& server);
	// an attack package from attacker reached victim (both: the units credited), at the victim's place
	void noteHit(uint32_t attacker, uint32_t victim, const Vector3& at);
	// a kill by killer of a unit at that place
	void noteKill(uint32_t killer, const Vector3& at);
	// a hero died there (killer: its killer, 0 none)
	void heroFell(Server& server, uint32_t hero, const Vector3& at, uint32_t killer);
	// a unit became a hero, or a hero died: the events it fought in are sent again
	void unitChanged(uint32_t unit);
	// the whole chronicle to one client (or all): RESET, the events, the samples
	void sendAll(Server& server, NetLink* link = nullptr);
	// the save game's side file: "CHRONICLE ..." lines, and back
	std::string saveText(Server& server);
	void loadLine(Server& server, const std::string& line);

	// what a unit is called in the timeline: its name, a siege engine or a building by its
	// player's name and its type ("Saint Jane's Trebuchet")
	std::string label(Server& server, uint32_t unit, bool withPlayer = true);

private:
	struct Participant { uint32_t player = 0; int kills = 0; uint8_t flags = 0; float reached = 0.0f; }; // flags: 1 dealt, 2 took
	std::map<uint32_t, std::map<uint32_t, Participant>> participants; // by event id, by unit
	std::map<uint32_t, std::set<uint32_t>> eventsOfUnit;
	std::map<uint32_t, std::vector<GameChronicle::Mention>> fallen; // by event id
	struct Hit { uint32_t attacker, victim; Vector3 at; };
	std::vector<Hit> hits; // since the last scan
	struct KillNote { uint32_t killer; Vector3 at; float time; };
	std::vector<KillNote> kills;
	std::set<uint32_t> dirty; // events to send
	std::map<uint32_t, size_t> samplesSent; // by player
	bool started = false;
	const void* level = nullptr;
	GameChronicle::Event* eventById(uint32_t id);
	// the fight going on (or ended less than 30 s ago) whose area holds the place; null: none
	GameChronicle::Event* fightAt(const Vector3& at, float now, float within);
	void addParticipant(Server& server, GameChronicle::Event& e, uint32_t unit, uint8_t flag, int kill, float now);
	void makeMentions(Server& server, GameChronicle::Event& e);
	static void sendLine(Server& server, const std::string& line, NetLink* link);
};
