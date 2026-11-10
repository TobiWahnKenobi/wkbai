// wkbai - unit history: who killed whom (the RE repo's docs/engine/unit-history.md)

#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct Server;
struct ServerGameObject;
struct GameSet;

// The kills, worked out on the server. The WKB game set's killer is the sender of the attack
// package (Injured By Attack, Missile Attack, Non Specific Attack, Spew Attack) that takes the
// hit points to 0 (Mechanics/Death.cpp "Being Killed" -> Scoring.cpp "I Have Killed Sequence
// Executor" FINDER_PACKAGE_SENDER). So each attack package notes its sender on the victim
// (followed from a missile to whoever fired it), and a terminated character or building with no
// hit points left, hit within the last 15 s, is that unit's kill.
struct UnitHistory {
	// the attack events and the items read by name, for this game set (again when it changes)
	void setup(const GameSet* gs);
	bool isAttack(int evt) const { return evt >= 0 && evt < (int)attackEvents.size() && attackEvents[evt]; }

	// every package (ServerGameObject::sendEvent): an attack one notes its sender on the victim
	void onEvent(Server& server, ServerGameObject* victim, int evt, ServerGameObject* sender);
	// an attack package reached the victim from the sender
	void hit(Server& server, ServerGameObject* victim, ServerGameObject* sender);
	// obj was made by an OBJECT_CREATION of creator
	void created(Server& server, ServerGameObject* obj, ServerGameObject* creator);
	// obj is being terminated
	void terminated(Server& server, ServerGameObject* obj);
	// the character or building credited with a package from sender (0: none): a missile's (or
	// a fire's, a mine's) creator, a garrisoned building's missile its troop
	uint32_t resolve(Server& server, ServerGameObject* sender) const;

	// the kill counts changed (killer, kills, buildings destroyed) to the clients
	static void sendKills(Server& server, ServerGameObject* killer, uint32_t victim);

	// the save game's side file <save>.history.txt: a line per object with any of it
	// ("UNIT <id> <kills> <buildings> <creator> <credit> <last hit by> <last hit time> <garrison credit>")
	static std::string saveText(Server& server);
	static void loadText(Server& server, const std::string& text);

	static constexpr float creditWindow = 15.0f; // s: a hit older than this doesn't kill

	// "unitHistoryTrace" (WKB_TRACE_KILLS): each kill and hit printed
	bool trace = false;

private:
	const GameSet* gs = nullptr;
	std::vector<bool> attackEvents;
	int hitPoints = -1, structuralIntegrity = -1, gunnerItem = -1, lightInfantryItem = -1, garrisonCategory = -1;
	bool isEnemy(Server& server, ServerGameObject* killer, ServerGameObject* victim) const;
	// a garrisoned building's next shooter for this missile (0: none inside)
	uint32_t garrisonShooter(Server& server, ServerGameObject* building, ServerGameObject* missile) const;
};
