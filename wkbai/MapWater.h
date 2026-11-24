// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

struct Terrain;

// The checks of a map's water by intent (wkbai's map authoring, step 17.6; map-authoring.md
// section 5 "Water"): what the compiler (MapFolder's BuildTerrain, report.json "water") and
// describe_map report, on a terrain whose water has been flooded (Terrain::floodfillWater):
//  - each lake point's flood: its tiles, depth, whether it is the sea; a lake that leaks (off the
//    map's edge, past the lake or sea the design paints, over half the map, into another lake);
//    a lake on flat ground (a puddle: shallow all over)
//  - each waterway: how much of its course is under water (a painted river, dry, else), whether
//    its water rises downstream (a dam across it, the water climbing), its designed speed
//  - the design's painted river tiles that stay dry
//  - the falls the renderer's finder will draw (FindWaterfalls: found and placed), and each
//    placed fall that draws nothing
//  - the rivers leaving the map (what Extend map carries on) and the far-land hints: a river hint
//    with no river leaving the map there
// Coordinates are playable metres, as describe_map's. warnings gets one line per fault, each with
// its place.
namespace MapWater {
	// seaLevel: the map's sea (map.json terrain.generated.seaLevel or terrain.seaLevel; < -1e29
	// none known)
	nlohmann::json Check(const Terrain& t, float seaLevel, std::vector<std::string>& warnings);
	// the sea level map.json gives (as above), -1e30 where it gives none
	float SeaLevelOf(const nlohmann::json& mapJson);
}
