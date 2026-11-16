// wkbai - the save game file: one file of named text sections, each with its own version (the RE
// repo's docs/engine/save-format.md)
//
//   WKBAI_SAVE 1
//   SECTION world 1 <bytes>
//   <the section's text, <bytes> long, ending with a line break>
//   END_SECTION world
//   SECTION settings 1 <bytes>
//   ...
//   END_SAVE
//
// The sections stay text, so that line readers of the world (the Load Game list's players, the
// replays' level name, tools/savdiff.py) read a save as before. A section a build doesn't know, or
// one of a newer version, is skipped with a warning; a missing one is empty.

#pragma once

#include <string>
#include <string_view>
#include <vector>

struct SaveSection {
	std::string name;
	int version = 1;
	std::string text;
};

struct SaveFile {
	static constexpr int FORMAT_VERSION = 1;

	std::vector<SaveSection> sections;

	// whether the data is a wkbai save file (else an original save or level: the world alone)
	static bool IsSaveFile(std::string_view data);

	// adds a section (none when the text is empty); a line break is added at the end if missing
	void add(const std::string& name, int version, std::string text);
	const SaveSection* find(const std::string& name) const;

	// the file's text
	std::string write() const;
	// reads the file's sections; false (with the reason) if it is broken. The sections read before
	// the break are kept.
	bool read(std::string_view data, std::string& error);
};
