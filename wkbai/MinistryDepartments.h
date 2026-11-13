// wkbai - the ministry AI's departments (Ministries.h): each made by its file
#pragma once

#include <memory>
#include "Ministries.h"

namespace Ministry {
	std::unique_ptr<Department> MakeEconomy();
	std::unique_ptr<Department> MakeLabour();
	std::unique_ptr<Department> MakeIntelligence();   // MinistryIntelligence.cpp
}
