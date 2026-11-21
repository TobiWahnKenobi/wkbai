// wkbai - what an agent designing a map needs to know (step 17.3 of the RE repo's
// docs/engine/map-authoring.md): the catalog of what can go on a map, from the loaded game set and
// texture databases (so a mod's units and textures are in it), and the description of an
// existing map (a .wkbai folder or an original level) as numbers and a top-down picture.
//
//   wkbai --author catalog [--mod <name>|none] [--gameset <file>] [--edges] [--out <file.json>]
//   wkbai --author describe <folder.wkbai | level> [--png <file.png>] [--out <file.json>]
//
// Both run headless (no window) and print one line "AUTHOR ok, ..." or "AUTHOR failed: ..."
// besides the JSON they write (to --out, else to stdout between "AUTHOR-JSON-BEGIN" and
// "AUTHOR-JSON-END" lines).

#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

struct GameSet;

namespace MapAuthoring {
	constexpr int CATALOG_VERSION = 1;
	constexpr int DESCRIBE_VERSION = 1;

	// The catalog of the game set (its file, as a level's GAME_SET names it) and of every texture
	// database in Maps\Map_Textures, with what's mounted now. edges: each texture's edge rules in
	// full (else per group and side the groups it has rules with, and how many)
	nlohmann::ordered_json Catalog(const GameSet& gameSet, const std::string& gameSetFile, bool edges);

	// The description of a map: a .wkbai folder (compiled first when its cache is stale), a level
	// ("Save_Games\<name>.lvl", a full path, or just its name) or a save. png: where the top-down
	// picture goes ("" none). False with error when it can't be loaded.
	bool Describe(const std::string& map, const std::string& png, nlohmann::ordered_json& out, std::string& error);

	// "--author catalog ..." / "--author describe ..." (args after the word); the exit code
	int RunCatalog(const std::vector<std::string>& args);
	int RunDescribe(const std::vector<std::string>& args);
}
