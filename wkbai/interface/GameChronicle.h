// wkbai - statistics and a timeline of a game's events, as a client sees them

#pragma once

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>
#include "../util/vecmat.h"
#include "MapRegions.h"

struct Client;
struct FogOfWar;
struct CommonGameState;

// Kept by the client interface from the objects the client has (all of them: the fog of war is
// the client's own), so it works the same in single player, for a multiplayer guest and in a
// replay. Statistics: each player's resources and counts every 10 game seconds. Events: battles
// (10 or more fighters of 2 or more sides attacking within 60 m), sieges (battles at buildings),
// raids (fewer than 10 attackers at peasants or farms), settlements founded and destroyed. Each
// event remembers whether this client's player saw it (its place in sight, or its own units in
// it): in a game only those are listed, in a replay all.
struct GameChronicle {
	struct Sample {
		float time;
		float food, wood, gold;
		int peasants, soldiers, buildings;
		int unitsLost, buildingsLost;
	};
	std::map<uint32_t, std::vector<Sample>> samples; // by player id
	std::map<uint32_t, std::string> playerNames;
	std::map<uint32_t, int> playerColours;

	// Fallen: a hero slain outside any fight (wkbai's unit history)
	enum class Kind { Battle, Siege, Raid, Founded, Destroyed, Fallen };
	// what the server names in an event (wkbai's unit history): each side's unit with the most
	// kills there, the heroes who fought there, the heroes who fell there
	enum MentionKind { MENTION_TOP_KILLER = 0, MENTION_PRESENT = 1, MENTION_FALLEN = 2 };
	struct Mention {
		int kind = 0;
		uint32_t unit = 0, player = 0;
		int kills = 0; // in the event (top killer); in all (present, fallen)
		bool dead = false; // since (present: "the late ...")
		std::string name; // as named: "Sigurd the Valiant", "Saint Jane's Trebuchet"
		std::string extra; // fallen: "slain by Guillaume le Hardi", "fell"
	};
	struct Event {
		Kind kind;
		float time, endTime;
		Vector3 position;
		std::set<uint32_t> players;
		int size = 0; // the most fighters seen at once; settlements: 0
		std::string what; // a settlement's blueprint
		std::string place; // the map region's name (MapRegions)
		bool seen = false;
		// per player: the most of its fighters in it at once, and the units it lost there (those
		// gone within 100 m while it went on), by type
		std::map<uint32_t, int> strength;
		std::map<uint32_t, std::map<std::string, int>> lost;
		Vector3 lastPosition; // a fight: where it was at the last scan (position: where it began)
		uint32_t id = 0; // its number (from 1), the same on the server and the clients
		std::vector<Mention> mentions; // from the server
		std::string title() const; // "Battle of Ergor", "Village founded at Fangorn"
		bool fight() const { return kind == Kind::Battle || kind == Kind::Siege || kind == Kind::Raid; }
		// one line of text (the network, the save game) and back
		std::string toText() const;
		bool fromText(const std::string& text);
	};
	MapRegions regions;
	int hostRegionsSerial = 0; // the host's regions taken (Client::hostRegionsSerial)
	std::vector<Event> events;

	// once a frame; the scans themselves run every few game seconds. fog: this client's, or
	// nullptr when everything is seen (no fog of war, a replay). With a server that keeps the
	// chronicle (wkbai's unit history: NETCLIMSG_CHRONICLE), the client takes its events and
	// samples instead, and only works out which events it saw.
	void update(Client* client, const FogOfWar* fog);
	static const char* kindName(Kind kind);

	// the scans and samples on their clocks, for any game state (the server's: ServerChronicle);
	// true when it scanned now (the events it made or went on with: touched)
	bool step(CommonGameState* state, uint32_t ownId, const FogOfWar* fog, float now);
	std::vector<size_t> touched;
	uint32_t nextId = 1;
	// the server's: kept by it, the client only receives (NETCLIMSG_CHRONICLE)
	bool serverDriven = false;
	// a sample as one line of text and back ("SAMPLE <player> <time> ... <colour> "<name>"")
	std::string sampleText(uint32_t player, const Sample& s) const;
	bool sampleFromText(const std::string& text);
	// the clock, the losses so far (the save game's side file)
	std::string stateText() const;
	bool stateFromText(const std::string& text);
	// new heroes fallen (a client: announced), their texts
	std::vector<std::pair<uint32_t, std::string>> fallenAnnouncements;

private:
	float nextSample = 0.0f, nextScan = 0.0f;
	bool first = true;
	const void* level = nullptr; // the level it is about
	struct Known { uint32_t player; bool building, settlement, foundation; Vector3 position; std::string name; bool fighter = false; };
	std::map<uint32_t, Known> known; // the objects at the last scan
	std::map<uint32_t, int> unitsLost, buildingsLost;
	void scan(CommonGameState* state, uint32_t ownId, const FogOfWar* fog, float now);
	void sample(CommonGameState* state, float now);
	// a line from the server (EVENT, SAMPLE, RESET)
	void receive(Client* client, const FogOfWar* fog, const std::string& line);
};
