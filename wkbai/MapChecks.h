// wkbai - the map check pass and the previews (step 17.8 of the RE repo's
// docs/engine/map-authoring.md, sections 6 and 8): every check on one map gathered into one
// report, each problem with its place and a suggested fix; top-down pictures and known-good
// cameras for in-game shots.
//
// The checks run on a loaded Server (a map folder or an original level loaded headless, as
// describe_map loads it, or the running game for the live author.check), so an agent gets the
// same report from the compiler's output and from a map it is editing:
//  - players: without a start camera, without units or buildings
//  - terrain: the walkable share for the unit (a Peasant by default), what blocks the rest (slope,
//    terrain, water), the walkable regions no unit can reach from the main land (steep-slope
//    islands; a warning where buildings, characters, gold or stone stand on them)
//  - reachability: every start from every other by land; where two can't reach each other the
//    nearest points of their two lands (a ramp's ends)
//  - placement: every building by the server's own rules (MapObjects::CheckPlacement), characters
//    in buildings or water, containers on buildings
//  - water: MapWater::Check (leaks, puddles, rising and dry rivers, falls, hints)
//  - tile edges: the ground solver's unmatched tiles (a folder's report), else every edge where
//    two grounds meet with no transition (MapMaterials::EdgeFaults)
//  - fairness: the resources and farmland near each base (MapObjects::Fairness), the path length
//    to the nearest rival, the high ground near each start, the choke points between starts, a
//    symmetry score
//  - the compiler's other warnings (a folder's report)
// A problem: { "id", "check", "severity": "error" | "warning" | "info", "what", "at": [x, z]
// (playable metres, null: none), "tile": [x, z] (whole map), "fix", "fixOp" (an author.* op that
// would make the fix, where one can) }. Errors and warnings count as faults; info doesn't.
//
// Command line:
//   wkbai --author check <folder.wkbai | level> [--out <file.json>] [--png <file.png>]
//   wkbai --author preview <folder.wkbai | level> --dir <folder> [--out <file.json>]
// Licensed under the GNU General Public License 3

#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <utility>
#include <vector>

struct Server;
struct Terrain;

namespace MapChecks {
	using json = nlohmann::ordered_json;
	constexpr int CHECK_VERSION = 1;

	struct Options {
		float fairnessRadiusM = 200.0f;
		int maxProblems = 400;   // problems listed (all counted)
		bool edges = true;       // the tile edge check (MapMaterials::EdgeFaults) when no report lists them
		float seaLevel = -1e30f; // the map's sea for the water checks (map.json; < -1e29: none)
		std::string unit = "Peasant";
		// a map folder's build/report.json: the ground solver's unmatched tiles, the compiler's
		// warnings
		const json* compileReport = nullptr;
	};

	// what the checks found on the grid, for the pictures
	struct Grids {
		int pw = 0, ph = 0;                     // playable tiles
		std::vector<int> region;                // per playable tile: its land region, -1 blocked
		std::vector<uint8_t> blocked;           // 0 walkable, 1 slope, 2 terrain, 3 water
		int mainRegion = -1;
		struct Start { std::string who; uint32_t player = 0; int colour = 0; float x = 0, z = 0; int tx = -1, tz = -1; };
		std::vector<Start> starts;
		std::vector<std::vector<std::pair<int, int>>> paths; // between starts (tiles), by land
		std::vector<std::pair<float, float>> chokes;        // their narrowest points (metres)
		std::vector<uint8_t> minimap;                       // 128 x 128 RGB, as the BCM holds it (Check)
	};

	// every check on the server's map
	json Run(Server& server, const Options& o, Grids* grids = nullptr);
	// a map folder (compiled first when stale) or a level, loaded headless, checked
	bool Check(const std::string& map, Options o, json& out, std::string& error, Grids* grids = nullptr, json* cameras = nullptr);
	// cameras above ground: per start, per fault (the first few, 100 m apart), one wide shot per
	// side towards the far land; each [x, y, z, pitch, yaw] as the benchmark's "cameras" take them,
	// y above the highest ground within 150 m of the eye
	json Cameras(const Terrain& t, const Grids& g, const json& report);
	// the pictures (relief.png, passability.png, water.png, fairness.png, problems.png, minimap.png,
	// materials.png for a folder with ground rules) and cameras.json into dir
	bool Preview(const std::string& map, const std::string& dir, json& out, std::string& error);
	int RunCommandLine(const std::vector<std::string>& args);
}
