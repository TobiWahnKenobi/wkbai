// wkbai - the ground of a map folder by rule (step 17.5 of the RE repo's
// docs/engine/map-authoring.md): materials (named families of texture groups, from data),
// rules in map.json that choose a material per vertex from the land (height, slope, curvature,
// moisture, masks, noise), and the edge-rule solver that turns the materials into tiles whose
// four sides all match their neighbours by the texture database's EDGE rules.
// Licensed under the GNU General Public License 3
//
// map.json:
//   "ground": { "preset": "temperate",            // temperate, nordic, dry (the data file's presets)
//               "default": "grass",               // where no rule matches (else the preset's)
//               "rules": [ { "material": "snow", "heightRel": [0.85, null], "slope": [null, 40] }, ... ],
//               "rulesMode": "before",            // the map's rules before the preset's (or "replace")
//               "materials": { "heath": { "groups": [["GRASS5", 2], ["SHRUB1", 1]] } },
//               "moisture": { "waterReachM": 180, "riverReachM": 140, "drainage": 0.35, "noise": 0.08 },
//               "despeckle": 1 }
// A rule's conditions (each [min, max], null for open; all must hold; the first rule that holds
// wins): height (m), heightRel (0-1 over the playable land), aboveSea (m above the sea level),
// slope (degrees), curvature (m: the vertex above (+, a crest) or below (-, a hollow) the mean
// of the 5 x 5 vertices round it), moisture (0-1), drainage (0-1, the flow of the land's water),
// river (the distance to a carved river's middle in its half-widths), seaDistance (m to the
// nearest water), noise (0-1, a large-scale noise of the rule's own; noiseScale m), mask
// (layers/<name>.png) with maskRange (default [0.5, null]), water (true: the vertex is at water).
// Every limit wanders by a jitter (defaults per condition; "jitter": a factor or per-condition
// numbers; "jitterScale" m) so borders are organic, not contour lines.
//
// The materials come from Maps\Materials\<database name>.json (redata's for the base game; a
// mod's replaces it), plus map.json's ground.materials. Without "ground" rules or a preset the
// map keeps ground.fill (17.2): one group at random.

#pragma once

#include <nlohmann/json.hpp>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

struct Terrain;

namespace MapMaterials {
	constexpr int SOLVER_VERSION = 1;
	using json = nlohmann::ordered_json;

	// The edge rules of a texture database, as its file has them (TerrainTextureDatabase::load
	// reads the groups and textures only): per group, per texture (the TEXTURE's id; its MATCHING
	// alternatives share them), per side (Tags::MAPTEXDIR_*: east, north, south, west of the
	// texture's own picture), the textures that may lie beyond that side with their turn and flips
	// relative to it
	struct EdgeRule { int side, otherSide; std::string group; int id, rot, xflip, zflip; };
	struct EdgeRules {
		std::map<std::string, std::map<int, std::vector<EdgeRule>>> rules; // group -> texture id -> rules
		std::map<std::string, std::map<int, int>> matchingOf;               // group -> MATCHING id -> its TEXTURE's id
		size_t count = 0;
	};
	EdgeRules ReadEdgeRules(const std::string& dbFile);

	// whether map.json asks for materials by rule (ground.rules or ground.preset)
	bool HasRules(const json& mapJson);
	// the material data for a texture database with the map's additions (ground.materials):
	// { "materials", "presets", "groups", "source" }
	json LoadMaterials(const std::string& textureDb, const json& mapJson, std::vector<std::string>& warnings);
	// what the materials of a map folder depend on beyond its own files (the data file), for the
	// cache's inputs hash
	std::string Fingerprint(const std::filesystem::path& folder);
	// a mask picture of layers\ (8 or 16 bits, any size, row 0 the north edge) sampled per vertex
	// of a gw x gh grid, 0-1; false when there is none (the scatter rules read them too)
	bool ReadMaskLayer(const std::filesystem::path& file, int gw, int gh, std::vector<float>& out);

	// the moisture per vertex (0-1, (width + 1) x (height + 1), row 0 the south edge as the terrain's
	// vertices): what the ground rules read (distance to water and rivers, drainage, hollows, noise;
	// or layers/moisture.png), for t with its water flooded; the forests by moisture (scatter) read it
	std::vector<float> Moisture(const std::filesystem::path& folder, const json& mapJson, uint32_t seed, const Terrain& t);

	// Paints the rules' tiles onto t: t's heights, lakes and texture database are set, the water
	// flooded; hand[i] (per tile) marks the hand tiles, which stay as they are (the solver works
	// around them). report: the materials report (shares, passability, unmatched edges with their
	// places). Writes build/materials.png (a top-down material map) and build/moisture.png.
	// ruleTile[i]: set per tile the rules painted.
	bool Paint(const std::filesystem::path& folder, const json& mapJson, uint32_t seed, Terrain& t, const std::vector<uint8_t>& hand,
		json& report, std::vector<std::string>& warnings, std::vector<uint8_t>& ruleTile, std::string& error);

	// the share of neighbouring tiles of a map whose pictures fit by the EDGE rules (the solver's
	// reading of them), and the share of tiles whose corners the solver knows; for tests on the
	// original maps, which the original editor made with these rules
	json EdgeFitOf(const Terrain& t, const std::string& dbFile);

	// "--author materials <folder> [--out <file.json>] [--png <file.png>]": compiles the folder
	// and gives its materials report; "--author material-presets": the presets and materials
	int RunCommandLine(const std::vector<std::string>& args);
}
