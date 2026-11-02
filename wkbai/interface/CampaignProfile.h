// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#pragma once

#include <map>
#include <string>
#include <vector>

// A player's progress through the Kingmaker campaign: the provinces conquered and the items of
// the USER object (resources, population, tech levels, "<Province> Completed", ...) that carry
// over from one province to the next. Kept as JSON in the user folder
// (saved\Campaigns\<name>.json), not the original's .usr profiles.
struct CampaignProfile {
	std::string name;
	std::vector<std::string> conquered; // province keys ("Angland")
	std::map<std::string, float> items;

	static std::vector<std::string> list();
	static bool load(const std::string& name, CampaignProfile& out);
	bool save() const;
	static bool remove(const std::string& name);
	// A file name, not a path
	static bool validName(const std::string& name);
};
