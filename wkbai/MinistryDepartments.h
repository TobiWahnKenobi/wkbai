// wkbai - the ministry AI's departments (Ministries.h): each made by its file
#pragma once

#include <memory>
#include "Ministries.h"

#include <cstdlib>
namespace Ministry {
	// (experiments: parts switched off by WKB_MINOFF bits)
	inline int MinOff() { static const int v = getenv("WKB_MINOFF") ? atoi(getenv("WKB_MINOFF")) : 0; return v; }
	// (the tuning, improvement 8: hand-set numbers a tuning run may change, WKB_MINTUNE="name=value,...";
	// the same for the whole process, so a game stays deterministic)
	inline float Tune(const char* name, float otherwise) {
		static const std::map<std::string, float> values = [] {
			std::map<std::string, float> m;
			if (const char* e = getenv("WKB_MINTUNE")) {
				std::string s(e);
				size_t at = 0;
				while (at < s.size()) {
					size_t end = s.find(',', at);
					if (end == std::string::npos) end = s.size();
					const std::string item = s.substr(at, end - at);
					const size_t eq = item.find('=');
					if (eq != std::string::npos)
						m[item.substr(0, eq)] = (float)atof(item.c_str() + eq + 1);
					at = end + 1;
				}
			}
			return m;
		}();
		auto it = values.find(name);
		return it == values.end() ? otherwise : it->second;
	}
	// a siege engine, by its type's name (Tactics trains a few, War sends them with the attacks on buildings)
	inline bool IsSiegeName(const std::string& n) {
		for (const char* s : { "Mangonel", "Trebuchet", "Ballista", "Battering Ram", "Rock Thrower", "Catapult", "Cannon" })
			if (n.find(s) != std::string::npos)
				return true;
		return false;
	}
	std::unique_ptr<Department> MakeEconomy();
	std::unique_ptr<Department> MakeLabour();
	std::unique_ptr<Department> MakeIntelligence();   // MinistryIntelligence.cpp
	std::unique_ptr<Department> MakeStrategist();     // MinistryWar.cpp
	std::unique_ptr<Department> MakeWar();
	std::unique_ptr<Department> MakeGenerals();
	std::unique_ptr<Department> MakeAdmiralty();      // (the navy, Naval.h)
	std::unique_ptr<Department> MakeDiplomacy();      // MinistryDiplomacy.cpp
	std::unique_ptr<Department> MakeBuilders();       // MinistryEconomy.cpp
	std::unique_ptr<Department> MakeResearch();
	// Economy's asks (from Economy::think) and Labour & Population's acts (from Labour::think; its
	// gold miners: peasant id -> since when)
	void EconomyNeeds(Government& g);
	void LabourActs(Government& g, std::map<uint32_t, float>& goldMiners);
	// the General's character from its items, and the departments' weights from it, once when the
	// government is formed (MinistryWar.cpp)
	void ReadPersonality(Government& g);
	void SetWeightsFromCharacter(Government& g);
	// MinistryTactics.cpp: the units' abilities by the rule table (cooldowns: unit id << 8 | rule ->
	// the game time it may again), how many used; fighters trained by counters, how many ordered
	int UseAbilities(Government& g, const std::vector<ServerGameObject*>& units, std::map<uint64_t, float>& ready);
	int TrainByCounters(Government& g);
	// the opening (MinistryTactics.cpp, WKB_MINOFF 1024 off): how many fighters short of the home
	// guard the first 20 minutes want (0: none short, or the opening over)
	int OpeningShort(Government& g);
	// the warning (MinistryTactics.cpp, WKB_MINOFF 2048 off): how many fighters short of 1.2 times
	// the enemy fighters Intelligence saw within 500 m of the palace in the last minute
	int DangerShort(Government& g);
	// a mission given (by the Strategist, or a Lua AI's ai.giveMission): its id
	uint32_t GiveMission(Government& g, Mission& m, const char* why);
}
