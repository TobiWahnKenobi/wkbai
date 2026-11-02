// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#include "CampaignProfile.h"
#include "../file.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <filesystem>
#include <fstream>

namespace {
	std::filesystem::path folder() { return g_userPath / "saved" / "Campaigns"; }
	std::filesystem::path fileOf(const std::string& name) { return folder() / PathFromUtf8(name + ".json"); }
}

bool CampaignProfile::validName(const std::string& name)
{
	return !name.empty() && name.find_first_of("\\/:*?\"<>|") == std::string::npos && name.find_first_not_of(" .") != std::string::npos;
}

std::vector<std::string> CampaignProfile::list()
{
	std::vector<std::string> names;
	std::error_code ec;
	for (const auto& entry : std::filesystem::directory_iterator(folder(), ec))
		if (entry.is_regular_file() && entry.path().extension() == ".json")
			names.push_back(PathToUtf8(entry.path().stem()));
	std::sort(names.begin(), names.end());
	return names;
}

bool CampaignProfile::load(const std::string& name, CampaignProfile& out)
{
	if (!validName(name))
		return false;
	std::ifstream in(fileOf(name));
	if (!in)
		return false;
	const nlohmann::json j = nlohmann::json::parse(in, nullptr, false);
	if (!j.is_object())
		return false;
	out = {};
	out.name = name;
	const nlohmann::json conquered = j.value("conquered", nlohmann::json::array());
	for (const auto& p : conquered)
		if (p.is_string())
			out.conquered.push_back(p.get<std::string>());
	const nlohmann::json items = j.value("items", nlohmann::json::object());
	for (const auto& [item, value] : items.items())
		if (value.is_number())
			out.items[item] = value.get<float>();
	return true;
}

bool CampaignProfile::save() const
{
	if (!validName(name))
		return false;
	std::error_code ec;
	std::filesystem::create_directories(folder(), ec);
	nlohmann::json j = { { "campaign", "Kingmaker" }, { "conquered", conquered }, { "items", items } };
	std::ofstream out(fileOf(name));
	out << j.dump(1);
	return (bool)out;
}

bool CampaignProfile::remove(const std::string& name)
{
	std::error_code ec;
	return validName(name) && std::filesystem::remove(fileOf(name), ec);
}
