// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#pragma once

#include <string>
#include <unordered_map>

struct Language {
	std::unordered_map<std::string, std::string> text;

	void load(const char* path);
	std::string getText(const std::string& key) const;

	// Language.txt writes a line break as a backslash and n: made a real one at load, so every
	// reader of the text (menus, dialogs, tooltips) gets it. "\\" and other escapes ("\c", the
	// advisor's marker) are kept for the readers that interpret them.
	static std::string decodeLineBreaks(const std::string& str);

	Language() = default;
	//Language(const char* path) { load(path); }
};