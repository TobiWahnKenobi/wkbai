// wkbai - headless test of the map pieces made for real-place maps: rivers with water (the
// generator's pools and sills: water in the channel, none spilling over the land), scatter rules
// (forests and resources: objects placed, none on water, the broadleaf/conifer mix rising with the
// height), and a map's stored far land (farland.png into the cache, loaded with the map).
// Test "Map water, objects and far land" in test.cpp.
// Licensed under the GNU General Public License 3

#include "MapTerrain.h"
#include "MapFolder.h"
#include "terrain.h"
#include "file.h"
#include "settings.h"
#include <nlohmann/json.hpp>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {
	using oj = nlohmann::ordered_json;
	namespace fs = std::filesystem;
	std::string Text(const fs::path& p)
	{
		std::ifstream f(p, std::ios::binary);
		return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
	}
}

void Test_MapWaterObjects()
{
	LoadBCP("data.bcp");
	bool all = true;
	auto check = [&](const char* what, bool ok, const std::string& detail) {
		printf("WATEROBJ %s %d %s\n", what, (int)ok, detail.c_str());
		fflush(stdout);
		all = all && ok;
	};
	const fs::path folder = g_userPath / "saved" / "Maps" / "Water Objects Test.wkbai";
	std::error_code ec;
	fs::remove_all(folder, ec);
	fs::create_directories(folder, ec);
	Terrain ref;
	ref.readBCM("Maps\\128_4Peaks\\128_4Peaks.bcm");
	std::string db, sky;
	for (wchar_t c : ref.texDbPath) if (c) db += (char)c;
	for (wchar_t c : ref.skyTextureDirectory) if (c) sky += (char)c;
	const int playable = 192, edge = 30, w = playable + 2 * edge;
	oj mj;
	mj["format"] = "wkbai map";
	mj["version"] = 1;
	mj["name"] = "Water Objects Test";
	mj["size"] = { { "tiles", { w, w } }, { "edge", edge } };
	mj["heights"] = { { "scale", 1.0 } };
	mj["textures"] = { { "database", db }, { "translation", "Maps\\Map_Textures\\128_4Peaks.lttt" } };
	mj["sky"] = sky;
	mj["seed"] = 77;
	mj["players"] = oj::array({ { { "name", "West" }, { "colour", 1 }, { "start", { 150, 480 } }, { "base", "Manor" }, { "peasants", 2 } },
		{ { "name", "East" }, { "colour", 2 }, { "start", { 810, 480 } }, { "base", "Manor" }, { "peasants", 2 } } });
	// a valley falling north to south with a stream down it (water), a hill with a wood on it
	mj["terrain"] = { { "base", 30 }, { "noise", { { "amplitude", 4 }, { "scale", 300 } } } };
	mj["features"] = oj::array({
		{ { "kind", "valley" }, { "id", "dale" }, { "line", { { 480, 980 }, { 470, 500 }, { 490, -20 } } }, { "width", 300 }, { "depth", 6 }, { "floorHeight", { 40, 12 } } },
		{ { "kind", "river" }, { "id", "beck" }, { "line", { { 480, 950 }, { 470, 500 }, { 490, 10 } } }, { "width", 12 }, { "depth", 2.0 }, { "meander", 0.3 }, { "water", true } },
		{ { "kind", "hill" }, { "id", "wood hill" }, { "at", { 250, 200 } }, { "radius", 150 }, { "height", 70 } } });
	mj["ground"] = { { "preset", "temperate" }, { "rules", oj::array({ { { "material", "forest floor" }, { "heightRel", { 0.35, nullptr } }, { "slope", { nullptr, 25 } } } }) } };
	const oj oak = oj::array({ "Deciduous Tree", oj::array({ "Oak01", "Oak02" }) }), pine = oj::array({ "Coniferous Tree", oj::array({ "Pine01", "Pine02" }) });
	mj["scatter"] = oj::array({
		{ { "id", "forest" }, { "objects", oj::array({ oak }) }, { "objectsHigh", oj::array({ pine }) }, { "highFrom", 28 }, { "highTo", 42 }, { "highMin", 0.1 }, { "highMax", 0.95 },
			{ "on", oj::array({ "forest floor" }) }, { "density", 60 }, { "spacing", 6 }, { "max", 4000 } },
		{ { "id", "gold" }, { "near", "bases" }, { "objects", oj::array({ "Gold Deposit Big", "Gold Deposit Small" }) }, { "groups", 2 }, { "size", 3 } } });
	std::vector<uint16_t> flat((size_t)(w + 1) * (w + 1), 20 * 256);
	MapFolder::WritePng16(folder / "heights.png", w + 1, w + 1, flat);
	// a far land: a ring of hills 2 km round the map (heights 20-120 m, 40 m cells)
	{
		const int n = 160;
		std::vector<uint16_t> px((size_t)n * n);
		for (int r = 0; r < n; r++)
			for (int x = 0; x < n; x++) {
				const float fx = x - n / 2.0f, fz = r - n / 2.0f;
				const float d = std::sqrt(fx * fx + fz * fz) / (n / 2.0f);
				px[(size_t)r * n + x] = (uint16_t)std::clamp((int)(65535 * (0.5f + 0.5f * std::sin(d * 9.0f))), 0, 65535);
			}
		MapFolder::WritePng16(folder / "farland.png", n, n, px);
		std::ofstream(folder / "farland.json", std::ios::binary) << oj{ { "x0", -2700 }, { "z0", -2700 }, { "cell", 40 }, { "min", 20 }, { "max", 120 } }.dump(1);
	}
	std::ofstream(folder / "map.json", std::ios::binary) << mj.dump(1) << "\n";

	oj st;
	std::string err;
	const bool gen = MapTerrain::Generate(folder, st, err);
	int pools = -1;
	for (const oj& fr : st.value("features", oj::array()))
		if (fr.value("kind", "") == "river") pools = fr.value("pools", -1);
	check("river pools", gen && pools > 3, gen ? std::to_string(pools) + " pools" : err);
	const MapFolder::Result r = MapFolder::Compile(folder, true);
	if (!r.ok) { check("compiled", false, r.error); printf("WATEROBJ all 0\n"); return; }
	const oj rep = oj::parse(Text(folder / "build" / "report.json"));
	// the water stays in the channel: under water at most the channel's area (course x width x 2)
	const int wet = rep.value("tilesUnderWater", 0);
	check("water in the channel", wet > 20 && wet < 940 / 5 * 12 / 5 * 2 + 200, std::to_string(wet) + " tiles under water");
	const oj sc = rep.value("scatter", oj::object());
	check("scatter placed", sc.value("forest", 0) > 100 && sc.value("gold", 0) >= 4, sc.dump());
	// the objects: none on water, and more pines high up than low down
	Terrain t;
	t.readFromFile(MapFolder::TerrainFileOf(MapFolder::GamePathOf(folder)).c_str());
	const std::string level = Text(folder / "build" / "level.txt");
	int onWater = 0, pineHigh = 0, oakHigh = 0, pineLow = 0, oakLow = 0;
	for (size_t at = level.find("CONTAINER "); at != std::string::npos; at = level.find("CONTAINER ", at + 10)) {
		const size_t q = level.find('"', at), q2 = level.find('"', q + 1);
		const std::string bp = level.substr(q + 1, q2 - q - 1);
		const size_t p = level.find("POSITION ", q2);
		float x = 0, y = 0, z = 0;
		if (p == std::string::npos || sscanf(level.c_str() + p + 9, "%f %f %f", &x, &y, &z) != 3) continue;
		const TerrainTile* tile = t.getPlayableTile((int)(x / 5), (int)(z / 5));
		if (tile && tile->fullOfWater) onWater++;
		const bool pine2 = bp == "Coniferous Tree", oak2 = bp == "Deciduous Tree";
		if (y > 38) { pineHigh += pine2; oakHigh += oak2; }
		else if (y < 30) { pineLow += pine2; oakLow += oak2; }
	}
	check("nothing on water", onWater == 0, std::to_string(onWater) + " objects on water");
	const float shareHigh = pineHigh / std::max(1.0f, (float)(pineHigh + oakHigh)), shareLow = pineLow / std::max(1.0f, (float)(pineLow + oakLow));
	check("conifers higher up", pineHigh + oakHigh > 15 && shareHigh > shareLow + 0.3f,
		"pine share " + std::to_string(shareHigh) + " of " + std::to_string(pineHigh + oakHigh) + " above 38 m, " + std::to_string(shareLow) + " of " + std::to_string(pineLow + oakLow) + " below 30 m");
	// the far land: in the cache and loaded with the map
	check("far land loaded", t.farLand && t.farLand->w == 160 && std::abs(t.farLand->x0 - (-2700 + edge * 5)) < 0.01f,
		t.farLand ? std::to_string(t.farLand->w) + " x " + std::to_string(t.farLand->h) : "none");
	float wgt = 0;
	const float hv = t.farLand ? t.farLand->sample(-1000 + edge * 5.0f, 500 + edge * 5.0f, wgt) : 0;
	check("far land sampled", t.farLand && hv >= 20 && hv <= 120 && wgt > 0.99f, std::to_string(hv) + " m, weight " + std::to_string(wgt));
	printf("WATEROBJ all %d\n", (int)all);
	fflush(stdout);
}
