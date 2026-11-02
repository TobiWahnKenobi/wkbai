// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#pragma once

#include <functional>
#include <string>
#include <vector>

// The settings that the Options menu shows: wkbre2's own (render, camera and sound settings),
// not the original's list. Pages show their options in the order registered here, and the menu
// shows the pages in this order, so a new setting or page needs nothing but an entry here.
struct MenuOption {
	enum Type { TOGGLE, SLIDER, CHOICE };
	std::string id;      // stable name, for scripts
	std::string page;    // a MenuPage id
	std::string label;   // plain text, or a Language.txt key
	Type type = TOGGLE;
	float min = 0.0f, max = 1.0f, step = 0.0f; // sliders; step 0: continuous
	bool logarithmic = false;                   // sliders over a wide range
	std::string format = "%.2f";                // how a slider shows its value
	std::vector<std::string> choices;          // CHOICE: labels, value = index
	std::function<float()> get;
	std::function<void(float)> set;
	std::function<bool()> enabled;             // optional: greyed out when false
	bool needsRestart = false;                 // takes effect after restarting wkbre2
};

struct MenuPage {
	std::string id, title;
};

const std::vector<MenuPage>& GetMenuPages();
const std::vector<MenuOption>& GetMenuOptions();
const MenuOption* FindMenuOption(const std::string& id);
// Saves the settings to wkconfig.json and applies those that need it (sound volumes)
void SaveMenuOptions();
// Remembers the settings when the Options menu opens, for Cancel
void BeginEditMenuOptions();
// Cancel: back to the settings as they were when the Options menu opened
void RevertMenuOptions();
