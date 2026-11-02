// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#pragma once

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <sol/sol.hpp>
#include "../file.h"
#include "../settings.h"
#include "../util/StriCompare.h"
#include <nlohmann/json.hpp>

// The scripts a UI ran and their timestamps, to see when one is edited, added or removed (hot reload)
struct UIScriptFiles {
	std::string extensionDir;
	std::vector<std::pair<std::string, uint64_t>> files;
	std::vector<std::string> extensions;

	static std::vector<std::string> listExtensions(const std::string& dir)
	{
		std::unique_ptr<std::vector<std::string>> all(ListFiles(dir.c_str()));
		std::vector<std::string> lua;
		for (const std::string& name : *all)
			if (name.size() > 4 && !StrCICompare(name.substr(name.size() - 4), ".lua"))
				lua.push_back(name);
		std::sort(lua.begin(), lua.end(), [](const std::string& a, const std::string& b) { return StrCICompare(a, b) < 0; });
		return lua;
	}
	bool changed() const
	{
		for (const auto& [fn, time] : files)
			if (FileModificationTime(fn.c_str()) != time)
				return true;
		return !extensionDir.empty() && listExtensions(extensionDir) != extensions;
	}
};

// Hot reload of the UI scripts and textures: on unless "hotReload": false
inline bool HotReloadEnabled() { return g_settings.value("hotReload", true); }

// The Lua UI scripts through the file system: a mod's copy of a script replaces redata's (mods
// are searched before the redata folder), and every "<extensionDir>\*.lua" that the mods add runs
// after it, in name order, to add screens or change functions. Returns whether the main script ran.
inline bool RunUIScripts(sol::state& lua, const char* mainScript, const char* extensionDir, const char* what,
	UIScriptFiles* record = nullptr)
{
	if (record) {
		record->extensionDir = extensionDir;
		record->files.clear();
	}
	auto run = [&](const std::string& fn) {
		if (!FileExists(fn.c_str())) {
			printf("%s script %s not found\n", what, fn.c_str());
			return false;
		}
		if (record)
			record->files.emplace_back(fn, FileModificationTime(fn.c_str()));
		char* text; int size;
		LoadFile(fn.c_str(), &text, &size);
		const std::string code(text, size);
		free(text);
		sol::protected_function_result r = lua.safe_script(code, sol::script_pass_on_error, "@" + fn);
		if (!r.valid()) {
			sol::error err = r;
			printf("%s script error in %s:\n%s\n", what, fn.c_str(), err.what());
		}
		return r.valid();
	};
	const bool ok = run(mainScript);
	const std::vector<std::string> extensions = UIScriptFiles::listExtensions(extensionDir);
	if (record)
		record->extensions = extensions;
	if (!ok)
		return false;
	for (const std::string& name : extensions) {
		printf("%s script extension %s\\%s\n", what, extensionDir, name.c_str());
		run(std::string(extensionDir) + "\\" + name);
	}
	return true;
}
