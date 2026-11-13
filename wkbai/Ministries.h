// wkbai - the ministry AI's framework: each AI player a government of departments that share a
// blackboard (the RE repo's docs/engine/ai-ministries.md and ministry-framework.md)
//
// - Deterministic: every department runs on the simulation thread, at its own rate in game time,
//   staggered by player and department; the governments in player id order, the departments in
//   a fixed order. Nothing depends on the computer, and a save keeps it all (the "ai" section).
// - The blackboard: what the departments report (named numbers), what they ask for (requests
//   weighed by value x urgency x the asker's weight), the budgets the strategist sets, and which
//   department each worker is lent to (with a minimum time, so they don't swing back and forth).
// - The trace: "aiTrace" (wkconfig.json, or WKB_TRACE_AI): 1 for every AI, or a player's id;
//   lines "AITRACE player <id> t=<s> <department>: <text>".
//
// The departments of this first step only report (Economy, Labour). The game plays as before.

#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

struct Server;
struct ServerGameObject;

namespace Ministry {

	// the departments, in the order they run
	enum Dept : uint8_t { STRATEGIST, ECONOMY, LABOUR, BUILDERS, RESEARCH, WAR, GENERALS, INTELLIGENCE, DIPLOMACY, DEPT_COUNT };
	const char* DeptName(int dept);
	int DeptFromName(const std::string& name); // -1 if none

	struct Request {
		uint32_t id = 0;
		uint8_t from = 0; // the department asking
		std::string what; // "peasants", "food", "wood", "gold", "stone", ... (no spaces)
		float amount = 0.0f, value = 0.0f, urgency = 0.0f;
		bool emergency = false;
		float since = 0.0f; // the game time it was asked
	};

	struct Grant {
		uint32_t request = 0;
		uint8_t to = 0;
		float amount = 0.0f;
	};

	struct Blackboard {
		std::map<std::string, double> reports; // "economy.food" -> 1200 (ordered: saved in order)
		std::vector<Request> requests; // in the order they were asked
		float weights[DEPT_COUNT] = { 1, 1, 1, 1, 1, 1, 1, 1, 1 };
		std::map<std::string, double> budgets; // "war.income" -> 0.4 (shares the strategist sets)
		struct Assignment { uint8_t dept = 0; float since = 0.0f; };
		std::map<uint32_t, Assignment> assignments; // worker id -> the department it is lent to
		uint32_t nextRequestId = 1;

		void report(const std::string& key, double value) { reports[key] = value; }
		double get(const std::string& key, double otherwise = 0.0) const;

		// a request; returns its id. A department asking again for the same thing replaces its
		// request (keeping its id and since)
		uint32_t ask(uint8_t from, const std::string& what, float amount, float value, float urgency, bool emergency, float now);
		void withdraw(uint8_t from, const std::string& what);
		const Request* find(uint8_t from, const std::string& what) const;
		// a request's weight: value x urgency x the asker's weight
		float priority(const Request& r) const { return r.value * r.urgency * weights[r.from]; }
		// Shares out an amount of a thing among its requests: emergencies first, then by priority;
		// ties by department order, then by id. A request gets what it asked for while there is
		// enough, the last one what is left.
		std::vector<Grant> allocate(const std::string& what, float available) const;

		// lends a worker to a department: refused (false) while it has been with another for less
		// than minHold seconds, unless it is an emergency
		bool assign(uint32_t worker, uint8_t dept, float now, float minHold, bool emergency);
		int assignedTo(uint32_t worker) const; // -1: none
	};

	struct Government;

	struct Department {
		virtual ~Department() = default;
		virtual uint8_t id() const = 0;
		// game seconds between two thinks
		virtual float period() const = 0;
		virtual void think(Government& g) = 0;
		// the department's own state, as "<key> <value...>" lines (no line breaks in a value)
		virtual void save(std::vector<std::string>& lines) const {}
		virtual void load(const std::string& line) {}
		float nextAt = -1.0f; // the game time of its next think (-1: not scheduled yet)
		uint64_t thinks = 0;
	};

	struct Government {
		Server& server;
		uint32_t playerId = 0;
		Blackboard board;
		std::vector<std::unique_ptr<Department>> departments; // in Dept order
		bool trace = false;

		Government(Server& server, uint32_t playerId);
		ServerGameObject* player() const;
		Department* department(uint8_t id) const;
		float now() const;
		// a trace line (only with the trace on for this player)
		void log(uint8_t dept, const char* fmt, ...);
		// runs the departments due at this step
		void step();
	};

	// the profile ("profileSim"): ms in each department, and thinks, since the last print
	extern double g_deptMs[DEPT_COUNT];
	extern uint64_t g_deptThinks[DEPT_COUNT];

	// The server's governments
	struct Ministries {
		std::map<uint32_t, std::unique_ptr<Government>> governments; // by player id

		// every step (Server::simulate, with "aiMinistries" on): a government for each AI player
		// not yet having one, then the departments due, the players by id
		void update(Server& server);
		void clear() { governments.clear(); }
		// the save's "ai" section, and back (after the world: the players exist)
		std::string saveText() const;
		void loadText(Server& server, const std::string& text);
		// the profile line's text ("economy 0.01 labour 0.02 ...", ms per step over `steps`)
		static std::string profileText(uint64_t steps);
	};
}
