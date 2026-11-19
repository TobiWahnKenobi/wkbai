// wkbai - the land of a map folder made by an agent (step 17.4 of the RE repo's
// docs/engine/map-authoring.md): the feature plan in map.json ("terrain" and "features"), the
// generator that turns it into heights.png (shapes, noise, erosion, terraces, carving), and the
// import of a heightmap picture or of real elevation data (SRTM .hgt, GeoTIFF such as Copernicus
// GLO-30) as the plan's base. heights.hand.png (what a person changed by hand) is never touched.
//
//   wkbai --author terrain <folder> [--seed <n>] [--out <file.json>]
//   wkbai --author terrain-stats <folder> [--out <file.json>]
//   wkbai --author heightmap <folder> --options <file.json> [--out <file.json>]
//   wkbai --author elevation <folder> --options <file.json> [--out <file.json>]
//   wkbai --author elevation-tiles --options <file.json> [--out <file.json>]
//
// Nothing here downloads: elevation-tiles names the tiles an area needs (source, URL, size,
// licence) so the agent can ask the user, and elevation takes the files already on disk.
//
// map.json's plan (positions in playable metres, as objects' positions; heights in metres):
//   "terrain": { "base": 20, "seaLevel": 8, "beach": 2.5,
//                "noise": { "amplitude": 6, "scale": 500, "octaves": 6, "ridged": 0.3, "warp": 0.5 },
//                "erosion": { "hydraulic": 1, "thermal": 1, "talusDeg": 38 },
//                "source": { "file": "layers/source.png", "min": 2, "max": 60, "weight": 1 } }
//   "features": [ { "kind": "range", "line": [[x, z], ...], "width": 220, "height": [70, 95], "passes": 1 }, ... ]
// Kinds: hill, range (ridge), plateau, valley, basin, coast, island, river, pass, terraces, flat.
// Each takes an optional "id" and "role" (what it is for: "player 1 base", "the ford").

#pragma once

#include <nlohmann/json.hpp>
#include <filesystem>
#include <string>
#include <vector>

namespace MapTerrain {
	constexpr int GENERATOR_VERSION = 1;
	using json = nlohmann::ordered_json;

	// The plan in the folder's map.json into heights.png (map.json's heights.scale and
	// terrain.generated updated; the sea's lake points into water.json). out: the stats (Stats)
	// plus each feature's own checks and the time taken. seed: the map's seed when 0.
	bool Generate(const std::filesystem::path& folder, json& out, std::string& error, uint32_t seed = 0);
	// A heightmap picture (PNG 8/16-bit, raw, .hgt, GeoTIFF) as the plan's base layer
	// (layers/source.png), then generated. Options: "file", "raw" {width, height, bits, bigEndian,
	// signed}, "crop" [x0, y0, x1, y1] pixels, "cover" "playable"|"whole", "range" [lo, hi] metres
	// (or "verticalScale" and "offset"), "weight" (0-1, blend with the plan), "licence",
	// "attribution", "generate" (default true).
	bool ImportHeightmap(const std::filesystem::path& folder, const json& options, json& out, std::string& error);
	// Real elevation data from files on disk. Options: "files" [...], "source" "copernicus"|"srtm",
	// "bbox" [south, west, north, east] or "centre" [lat, lon] + "sizeKm" [east-west, north-south],
	// "verticalScale" (number, or "keep-slopes", the default), "maxSlopeDeg", "maxSteepShare",
	// "floor" (m), "sea" (auto/true/false), "seaDepth" (m), "keepAspect", "weight", "generate".
	// Missing tiles: false, with out["needed"] (as TilesFor) naming them.
	bool ImportElevation(const std::filesystem::path& folder, const json& options, json& out, std::string& error);
	// The tiles an area needs, each with its source, URL, approximate size and licence, and
	// whether one of options["files"] is it; map (optional) sizes the edge band's margin
	json TilesFor(const json& options, const std::filesystem::path& map = {});
	// The folder's land as it is now (heights.png plus heights.hand.png): height range, slope
	// histogram, walkable shares (the game's own test: a tile is too steep for a unit when its
	// corners differ by more than 5 m x tan(MAX_GRADIENT); most units have 45 degrees), the sea
	bool Stats(const std::filesystem::path& folder, json& out, std::string& error);

	// "--author terrain|terrain-stats|heightmap|elevation|elevation-tiles ..." (args from that
	// word on); the exit code
	int RunCommandLine(const std::vector<std::string>& args);
}
