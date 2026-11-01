// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
#include "util/vecmat.h"

struct Terrain;

// A map's water, as its maker designed it (wkbai only; the original has none): which water is a
// river, a lake or a sea, the seas' wind regions, the rivers' courses and where waterfalls are.
// Without one (or where it says "auto"), the renderers find all of this from the map's water and
// heights. It is <map>.water next to the map's BCM (Maps\<name>\<name>.water), found like the
// map's other files: a mod's, else saved\ (where the map editor writes it, also for the game's
// own maps). The file is JSON:
//   {
//     "format": "wkbai water", "version": 1,
//     "tiles": [width, height],        the whole map's tiles (with the edge), as the BCM's
//     "surface": [ "..rrr..AAB", ... ], per tile row (z from 0), a character per tile (x from 0):
//                                        . found automatically, r river, l lake,
//                                        A-Z sea, of that wind region (A the first)
//     "seas": [ { "name": "West", "wind": 37, "waves": 1.0 }, ... ],
//                                        the wind regions: the way the waves run (degrees from +x
//                                        towards +z) and their height (1 the default); the first
//                                        is also the wind of the seas found automatically and of
//                                        the lakes. Where regions meet, their waves blend over a
//                                        few tiles.
//     "waterways": [ { "points": [[x, z], ...], "width": 25 }, ... ],
//                                        rivers' courses (world units, the playable area's), each
//                                        flowing from its first point to its last: the water
//                                        within width of it is a river flowing along it, its
//                                        mouth at the last point
//     "waterfalls": [ { "from": [x, z], "to": [x, z] }, ... ],
//                                        falls placed by hand: the lip they pour over, along the
//                                        tile edges (x or z, whichever the line runs along more),
//                                        falling to its lower side
//     "autoWaterfalls": true             also the falls and cascades found in the map
//   }
struct WaterDesign {
	enum Surface : char { AUTO = '.', RIVER = 'r', LAKE = 'l' }; // or 'A' + a sea region
	static constexpr int MAX_SEA_REGIONS = 8;
	struct SeaRegion {
		std::string name;
		float windDegrees = 36.87f; // the waves' way: (0.8, 0.6), as without a design
		float waves = 1.0f;
	};
	struct Waterway {
		std::vector<Vector3> points; // x, z (y unused)
		float width = 25.0f;
	};
	struct PlacedFall {
		Vector3 from, to; // x, z
	};

	int width = 0, height = 0;
	std::vector<char> surface; // per tile, z * width + x
	std::vector<SeaRegion> seas = { SeaRegion{ "Sea" } };
	std::vector<Waterway> waterways;
	std::vector<PlacedFall> waterfalls;
	bool autoWaterfalls = true;

	// an empty design for a map of this many tiles (all automatic)
	static WaterDesign blank(int width, int height);
	char at(int x, int z) const { return (x >= 0 && z >= 0 && x < width && z < height) ? surface[(size_t)z * width + x] : (char)AUTO; }
	// the sea region of a surface character, -1 if it is no sea
	static int seaRegionOf(char c) { return c >= 'A' && c < 'A' + MAX_SEA_REGIONS ? c - 'A' : -1; }
	// whether it changes anything (else the file isn't needed)
	bool isEmpty() const;

	// The nearest waterway within its width of a point (world x, z): its index (-1: none), the
	// way it flows there (unit x, z), the distance to it and how far along it is still to its
	// mouth (world units)
	int nearestWaterway(float x, float z, float& dirX, float& dirZ, float& distance, float& toMouth) const;

	// <map>.bcm -> <map>.water
	static std::string pathFor(const std::string& mapFile);
	// false (and an error) when the file is there but can't be read; nothing when it isn't there
	bool load(const std::string& mapFile, std::string& error);
	bool save(const std::filesystem::path& path) const;
	// the file's JSON as text, and read from it (a multiplayer host's design for its guests)
	std::string toText() const;
	bool fromText(const std::string& text, std::string& error);
};
