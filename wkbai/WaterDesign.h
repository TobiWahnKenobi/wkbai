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
//     "waterways": [ { "points": [[x, z], ...], "width": 25, "speed": 1.5 }, ... ],
//                                        rivers' courses (world units, the playable area's), each
//                                        flowing from its first point to its last: the water
//                                        within width of it is a river flowing along it, its
//                                        mouth at the last point; "speed" (m/s, optional): its
//                                        current set by hand (0 or left out: found from the land).
//                                        "speeds": [m/s, ...] (optional, one per point): the
//                                        current at each point, between them along the course as
//                                        a line (a lazy river in the plain, fast in a gorge); 0 at
//                                        a point: "speed" there. The renderer's current and the
//                                        gameplay currents both take them (version 2).
//                                        A point may carry a third number, [x, z, level]: the
//                                        water's surface height there (world units). With a level
//                                        on every point the waterway is a sloped river
//                                        (Terrain::applySlopedWater): its surface runs down along
//                                        the course (never rising downstream), between the points
//                                        as a line, over the tiles within width / 2 of it whose
//                                        ground lies below it; rapids, cascades and falls where
//                                        it steepens (DiligentWaterfalls). Older readers take
//                                        the first two numbers only: a river of the level water.
//     "waterfalls": [ { "from": [x, z], "to": [x, z] }, ... ],
//                                        falls placed by hand: the lip they pour over, along the
//                                        tile edges (x or z, whichever the line runs along more),
//                                        falling to its lower side
//     "autoWaterfalls": true             also the falls and cascades found in the map
//     "farland": [ { "kind": "sea", "toward": "east", "spread": 90, "from": 10, "weight": 1 }, ... ]
//                                        what Extend map's far land does beyond the map (version
//                                        2; without any, it is guessed from the edge band alone):
//                                        "kind" sea (open sea), land (no sea), mountains (a range
//                                        rising), lowland (no ranges) or river (a river leaving the
//                                        map turns that way); "toward" a compass name (north is
//                                        +z, the top of heights.png; east +x) or degrees from +x
//                                        towards +z; "spread" the angle it covers (degrees,
//                                        seen from the map's middle); "from" how many tiles beyond
//                                        the map's edge it is full (it comes in over as many
//                                        again); "weight" 0 to 1; a river's "at": [x, z] the
//                                        point on the map's edge where it leaves (the nearest
//                                        river exit; left out: every exit on that side); a sea's
//                                        "level" (world units) where the map has no sea of its own
//   }
// Version 1 files (no "speeds", no "farland") read as before; a design without either is
// still written as version 1.
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
		float speed = 0.0f; // m/s set by the map's maker; 0: found from the land, as without it
		// a sloped river: the surface's height at each point (as many as points), else empty
		std::vector<float> levels;
		// the current at each point (m/s, as many as points; 0: speed), else empty
		std::vector<float> speeds;
		// who made it ("by": the terrain generator's own are "terrain:<river>"; kept as it is)
		std::string by;
		bool sloped() const { return levels.size() == points.size() && points.size() >= 2; }
		// the designed current at a place on its course (the segment from point i, t along it);
		// 0: none (found from the land)
		float speedAt(int i, float t) const;
		bool hasSpeed() const;
	};
	struct PlacedFall {
		Vector3 from, to; // x, z
	};
	// a far-land hint (Extend map: BuildExtendedTerrain)
	struct FarHint {
		enum Kind { SEA, LAND, MOUNTAINS, LOWLAND, RIVER };
		Kind kind = SEA;
		float degrees = 0.0f;  // the way it lies from the map's middle: from +x towards +z
		float spread = 90.0f;  // the angle it covers (degrees)
		float from = 0.0f;     // tiles beyond the map's edge where it is full
		float weight = 1.0f;   // 0 to 1
		bool hasAt = false;    // a river's exit point (playable world units)
		Vector3 at;
		float level = -1e30f;  // a sea's level where the map has none (world units), else unset
		static const char* kindName(Kind k);
		static bool kindFrom(const std::string& s, Kind& k);
		// the share (0 to 1) of a direction (degrees from +x towards +z) within its spread
		float across(float dirDegrees) const;
	};

	int width = 0, height = 0;
	std::vector<char> surface; // per tile, z * width + x
	std::vector<SeaRegion> seas = { SeaRegion{ "Sea" } };
	std::vector<Waterway> waterways;
	std::vector<PlacedFall> waterfalls;
	bool autoWaterfalls = true;
	std::vector<FarHint> farland;

	// an empty design for a map of this many tiles (all automatic)
	static WaterDesign blank(int width, int height);
	char at(int x, int z) const { return (x >= 0 && z >= 0 && x < width && z < height) ? surface[(size_t)z * width + x] : (char)AUTO; }
	// the sea region of a surface character, -1 if it is no sea
	static int seaRegionOf(char c) { return c >= 'A' && c < 'A' + MAX_SEA_REGIONS ? c - 'A' : -1; }
	// whether it changes anything (else the file isn't needed)
	bool isEmpty() const;
	bool hasSlopedWater() const;

	// The nearest waterway within its width of a point (world x, z): its index (-1: none), the
	// way it flows there (unit x, z), the distance to it and how far along it is still to its
	// mouth (world units)
	int nearestWaterway(float x, float z, float& dirX, float& dirZ, float& distance, float& toMouth, int* segment = nullptr, float* along = nullptr) const;
	// The designed current (m/s) at a point (world x, z): the nearest waterway's within its width,
	// its speed or its points' speeds there; 0 where none is set
	float designedSpeed(float x, float z) const;
	// a compass name ("north", "ne", "south-west", ...) or a number as degrees from +x towards +z
	static bool directionFrom(const std::string& s, float& degrees);

	// <map>.bcm -> <map>.water
	static std::string pathFor(const std::string& mapFile);
	// false (and an error) when the file is there but can't be read; nothing when it isn't there
	bool load(const std::string& mapFile, std::string& error);
	bool save(const std::filesystem::path& path) const;
	// the file's JSON as text, and read from it (a multiplayer host's design for its guests)
	std::string toText() const;
	bool fromText(const std::string& text, std::string& error);
};
