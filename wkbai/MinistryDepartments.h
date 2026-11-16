// wkbai - the ministry AI's departments (Ministries.h): each made by its file
#pragma once

#include <memory>
#include "Ministries.h"

#include <cstdlib>
namespace Ministry {
	// (experiments: parts switched off by WKB_MINOFF bits)
	inline int MinOff() { static const int v = getenv("WKB_MINOFF") ? atoi(getenv("WKB_MINOFF")) : 0; return v; }
	std::unique_ptr<Department> MakeEconomy();
	std::unique_ptr<Department> MakeLabour();
	std::unique_ptr<Department> MakeIntelligence();   // MinistryIntelligence.cpp
	std::unique_ptr<Department> MakeStrategist();     // MinistryWar.cpp
	std::unique_ptr<Department> MakeWar();
	std::unique_ptr<Department> MakeGenerals();
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
	// a mission given (by the Strategist, or a Lua AI's ai.giveMission): its id
	uint32_t GiveMission(Government& g, Mission& m, const char* why);
}
