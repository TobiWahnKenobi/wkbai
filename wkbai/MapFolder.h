// wkbai - wkbai's own map format: a map folder (Maps\<name>.wkbai\), the agent's source and what
// the map editor saves, with what a person changed by hand kept in layers of its own; the
// compiler that makes the folder's cache (build\) the game loads; and the import of an original
// map (its .bcm, .lvl, .wkbmap, .water) into a folder. The RE repo's
// docs/engine/map-authoring.md, section 3 and "Step 17.2".
//
//   <name>.wkbai/
//     map.json           format and version, size, edge, height scale, sky, sun, fog, the texture
//                        database and translation, the seed, the ground's material, the players
//     heights.png        16-bit grey, (width + 1) x (height + 1) vertices, row 0 the north edge
//                        (z = height): a height step is 256 (the value / 256 times the scale is
//                        the height). 8-bit pictures and other sizes are read too (scaled by 256,
//                        resampled to the grid).
//     heights.hand.png   16-bit, the same grid, 32768 = nothing: what the editor's brushes added
//                        on top of heights.png (value - 32768, in 256ths of a step)
//     tiles.hand.json    the tiles painted by hand, over what the rules give
//     water.json         the lakes (any number) and the water design (WaterDesign.h's JSON:
//                        surface, seas, waterways, waterfalls)
//     level.txt          the level, a wkbai save file (SaveFile.h) at time 0: its world section
//                        in the original's object grammar, and an authoring section (which rule
//                        made which object)
//     minimap.png        optional: the minimap (128 x 128); an imported map keeps the original's
//     build/             made by the compiler, never edited: map.bcm (+ map.wkbmap, map.water)
//                        and level.txt the game loads, inputs.hash, report.json
//
// A level path "Save_Games\<name>.wkbai" (the skirmish list's entry) and a map path
// "Maps\<name>.wkbai" both name the folder; a full path on disk does too.

#pragma once

#include <filesystem>
#include <string>
#include <vector>

struct Terrain;
struct EnvironmentSettings;
struct Server;

namespace MapFolder {
	constexpr int FORMAT_VERSION = 1;
	// the compiler's own version: a new one makes every cache stale
	constexpr int COMPILER_VERSION = 4; // 2: the ground by rule (MapMaterials.h); 3: patches where the solver left a tile, scatter masks; 4: objects by the server's placement rules (MapObjects.h)
	constexpr int HAND_ZERO = 32768;

	// whether a path names a map folder (ends in ".wkbai", with or without a slash)
	bool IsMapPath(const std::string& path);
	// The folder on disk: a full path as it is; "Save_Games\<name>.wkbai" or "Maps\<name>.wkbai" in
	// the user folder's saved\, the enabled mod folders, the game folder's saved\. Empty when there
	// is none.
	std::filesystem::path Resolve(const std::string& path);
	// the folder's game path: "Maps\<name>.wkbai" for one in saved\ (or a mod), else its full path
	std::string GamePathOf(const std::filesystem::path& folder);
	// the map folders there are ("<name>.wkbai"): in saved\Maps, the mods' Maps and the game
	// folder's saved\Maps, each name once
	std::vector<std::string> ListFolders();

	struct Result {
		bool ok = false;
		bool compiled = false; // false: the cache was fresh
		std::string error;
		std::vector<std::string> warnings;
		std::string reportPath;
	};
	// The cache's inputs hash (map.json, heights, hand layers, water, level, minimap, the folder's
	// game path and the compiler's version)
	std::string InputsHash(const std::filesystem::path& folder);
	bool IsFresh(const std::filesystem::path& folder);
	// Compiles the folder into build\ (force: also when it is fresh), with a report
	Result Compile(const std::filesystem::path& folder, bool force = false);
	// the cache's files (compiled first when stale); empty when the folder can't be compiled
	std::string LevelFileOf(const std::string& path);
	// The menus' way (no frozen screen while a large map compiles): the cache's level when it is
	// fresh; else "" and the folder is compiled in the background (one at a time; a folder whose
	// compile failed isn't tried again until its inputs change). Compile itself waits for a
	// background compile, so the game never reads a half-written cache.
	std::string LevelFileIfFresh(const std::string& path);
	// what a compile is doing: the menus draw it as a progress bar
	struct Progress {
		bool busy = false;       // a compile is running (any, the background one or not)
		bool background = false; // the background one
		std::string map;         // the folder's name ("Isle of Skye")
		std::string stage;       // "the ground: placing tiles"
		float fraction = 0;      // 0-1
		int finished = 0;        // background compiles finished so far (the menus drop what they cached)
	};
	Progress CompileProgress();
	// set by the compiler's stages (MapMaterials' solver too)
	void ReportProgress(const char* stage, float fraction);
	std::string TerrainFileOf(const std::string& path);
	// map.json's texture translation (.lttt) for the cache's BCM ("" when none)
	std::string TranslationOf(const std::filesystem::path& folder);

	// An original map into a folder: the level (a .lvl or save; its MAP names the .bcm) with its
	// map, every tile a hand tile, the lakes and water design into water.json. out empty: the user
	// folder's saved\Maps\<level name>.wkbai.
	Result Import(const std::string& level, std::filesystem::path out = {});

	// The map editor's Save into a folder (Server::terrain as edited, the client's water design,
	// the level saved by the server): heights beyond the folder's heights.png into
	// heights.hand.png, tiles that differ from the rules' (or were hand tiles) into
	// tiles.hand.json, the lakes and the design into water.json, the level into level.txt with the
	// objects the rules made and nobody moved still marked as theirs. The folder is made (from the
	// map that is open) when it isn't there.
	Result SaveFromEditor(Server& server, const Terrain* clientTerrain, const std::filesystem::path& folder);

	// map.json's optional "environment" block (the RE repo's docs/engine/map-environment.md,
	// section 1), kept as written (the editor's Save leaves map.json as it is). Step 17.2 applies
	// what EnvironmentSettings already has: dayNight.on, dayLengthMin, a numeric start (a clock
	// hour, 5 or "05:30"), moonPhase; seasons.on, seasonLengthMin, start, severity; weather.stability
	// (or its custom minChangeMin/maxChangeMin) and allowed (weatherTypes). The rest (climate,
	// latitude, a start by the sun, meanC, weights, the first weather, wind, height, areas, fixed)
	// is kept for step 17.13. The settings named there, as EnvironmentSettings::apply names them.
	std::vector<std::pair<std::string, float>> EnvironmentDefaults(const std::string& mapPath);
	// a new game on a map folder: each of those onto `settings` where it still has the lobby's
	// default (a setting the lobby changed stays the lobby's); how many were applied
	int ApplyEnvironmentDefaults(const std::string& mapPath, EnvironmentSettings& settings);

	// headless: "wkbai --author <folder>", "wkbai --author import <level> [<folder>]", and "catalog" and
	// "describe" (MapAuthoring.h); returns the process's exit code
	int RunCommandLine(const std::vector<std::string>& args);

	// 16-bit grey PNG (one channel), for the hand layers and tests
	bool WritePng16(const std::filesystem::path& path, int width, int height, const std::vector<uint16_t>& pixels);
	bool ReadPng16(const std::filesystem::path& path, int& width, int& height, std::vector<uint16_t>& pixels);
}
