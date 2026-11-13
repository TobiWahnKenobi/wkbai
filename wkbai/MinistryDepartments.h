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
	// the General's character from its items (MinistryWar.cpp)
	void ReadPersonality(Government& g);
}
