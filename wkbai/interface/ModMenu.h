// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#pragma once

#include <vector>
#include "../file.h"

// Window to enable/disable mods and set their load order
struct ModMenu {
	void open();
	// Returns true when the user applied a new configuration (the file system was remounted)
	bool draw();
	bool isOpen() const { return visible; }

private:
	bool visible = false;
	bool customOrder = false;
	bool dirty = false;
	std::vector<ModInfo> mods; // edited copy, lowest priority first
};
