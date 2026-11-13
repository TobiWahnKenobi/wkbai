// wkbai - the ministry AI's departments (Ministries.h): each made by its file
#pragma once

#include <memory>
#include "Ministries.h"

namespace Ministry {
	std::unique_ptr<Department> MakeEconomy();
	std::unique_ptr<Department> MakeLabour();
	std::unique_ptr<Department> MakeIntelligence();   // MinistryIntelligence.cpp
	std::unique_ptr<Department> MakeStrategist();     // MinistryWar.cpp
	std::unique_ptr<Department> MakeWar();
	std::unique_ptr<Department> MakeGenerals();
	std::unique_ptr<Department> MakeDiplomacy();      // MinistryDiplomacy.cpp
	// the General's character from its items (MinistryWar.cpp)
	void ReadPersonality(Government& g);
	// MinistryTactics.cpp: the units' abilities by the rule table (cooldowns: unit id << 8 | rule ->
	// the game time it may again), how many used; fighters trained by counters, how many ordered
	int UseAbilities(Government& g, const std::vector<ServerGameObject*>& units, std::map<uint64_t, float>& ready);
	int TrainByCounters(Government& g);
}
