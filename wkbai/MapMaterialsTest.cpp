// wkbai - step 17.5's headless test (MapMaterials.h): the ground by rule on three generated maps
// (an island, a river valley, a highland with passes), the edge-rule solver's unmatched tiles, hand
// tiles kept through a rule change, determinism, a mask layer, the presets; the solver's reading of
// the EDGE rules against an original map; compiled, loaded and played. Test "Map materials" in test.cpp.
// Licensed under the GNU General Public License 3

#include "MapMaterials.h"
#include "MapTerrain.h"
#include "MapFolder.h"
#include "server.h"
#include "terrain.h"
#include "file.h"
#include "settings.h"
#include "gameset/gameset.h"
#include <nlohmann/json.hpp>
// (static, as MapFolder.cpp and MapAuthoring.cpp have their own)
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#include <stb_image_write.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace {
	using oj = nlohmann::ordered_json;
	namespace fs = std::filesystem;

	std::string Bytes(const fs::path& p)
	{
		std::ifstream f(p, std::ios::binary);
		return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
	}
	void Put(const fs::path& p, const std::string& s) { std::ofstream(p, std::ios::binary) << s; }
	void Step(Server& server, int steps)
	{
		server.timeManager.stepMs = 20;
		for (int i = 0; i < steps; i++) {
			server.timeManager.pendingMs = server.timeManager.stepMs;
			if (server.timeManager.step())
				server.simulate();
		}
	}
	oj Players(float x1, float z1, float x2, float z2)
	{
		return oj::array({ { { "name", "West" }, { "colour", 1 }, { "start", { x1, z1 } }, { "base", "Manor" }, { "peasants", 4 } },
			{ { "name", "East" }, { "colour", 2 }, { "start", { x2, z2 } }, { "base", "Manor" }, { "peasants", 4 } } });
	}
	// a map folder as step 17.4's test makes them (4 Peaks' textures and sky), with ground rules
	fs::path MakeMap(const std::string& name, const oj& terrain, const oj& features, const oj& players, const oj& ground)
	{
		const fs::path folder = g_userPath / "saved" / "Maps" / PathFromUtf8(name + ".wkbai");
		std::error_code ec;
		fs::remove_all(folder, ec);
		fs::create_directories(folder, ec);
		Terrain ref;
		ref.readBCM("Maps\\128_4Peaks\\128_4Peaks.bcm");
		const int edge = 30, w = 256 + 2 * edge;
		std::string db, sky;
		for (wchar_t c : ref.texDbPath) if (c) db += (char)c;
		for (wchar_t c : ref.skyTextureDirectory) if (c) sky += (char)c;
		oj mj;
		mj["format"] = "wkbai map";
		mj["version"] = 1;
		mj["name"] = name;
		mj["size"] = { { "tiles", { w, w } }, { "edge", edge } };
		mj["heights"] = { { "scale", 1.0 } };
		mj["textures"] = { { "database", db }, { "translation", "Maps\\Map_Textures\\128_4Peaks.lttt" } };
		mj["sky"] = sky;
		mj["sun"] = { { "colour", { (ref.sunColor >> 16) & 255, (ref.sunColor >> 8) & 255, ref.sunColor & 255 } },
			{ "direction", { ref.sunVector.x, ref.sunVector.y, ref.sunVector.z } } };
		mj["fog"] = { (ref.fogColor >> 16) & 255, (ref.fogColor >> 8) & 255, ref.fogColor & 255 };
		mj["seed"] = 1705;
		mj["ground"] = ground;
		mj["players"] = players;
		mj["terrain"] = terrain;
		mj["features"] = features;
		std::vector<uint16_t> flat((size_t)(w + 1) * (w + 1), 20 * 256);
		MapFolder::WritePng16(folder / "heights.png", w + 1, w + 1, flat);
		Put(folder / "map.json", mj.dump(1) + "\n");
		return folder;
	}
	// the compiled terrain's tiles as text (group id turn flips), for comparing compiles
	std::vector<std::string> TilesOf(const fs::path& folder)
	{
		Terrain t;
		t.readFromFile(MapFolder::TerrainFileOf(MapFolder::GamePathOf(folder)).c_str());
		std::vector<std::string> out((size_t)t.width * t.height);
		for (int i = 0; i < t.width * t.height; i++) {
			const TerrainTile& tile = t.tiles[i];
			out[i] = tile.texture->grp->name + " " + std::to_string(tile.texture->id) + " " + std::to_string(tile.rot) + std::to_string(tile.xflip) + std::to_string(tile.zflip);
		}
		return out;
	}
}

void Test_MapMaterials()
{
	LoadBCP("data.bcp");
	bool all = true;
	auto check = [&](const char* what, bool ok, const std::string& detail) {
		printf("MATERIALS %s %d %s\n", what, (int)ok, detail.c_str());
		fflush(stdout);
		all = all && ok;
	};

	// 0. the solver's reading of the EDGE rules on an original map (made with them by the original
	//    editor): most neighbours fit, and the corners of neighbouring pictures agree
	{
		Terrain t;
		t.readFromFile("Maps\\128_4Peaks\\128_4Peaks.bcm");
		std::string db;
		for (wchar_t c : t.texDbPath) if (c) db += (char)c;
		const oj f = MapMaterials::EdgeFitOf(t, db);
		check("edge rules read as the original maps use them", f["fitShare"].get<double>() > 0.9 && f["cornerAgreeShare"].get<double>() > 0.9, f.dump());
	}
	// the data file: materials and the three presets
	{
		std::vector<std::string> w;
		const oj d = MapMaterials::LoadMaterials("Maps\\Map_Textures\\All_Textures.dat", oj::object(), w);
		bool ok = d["presets"].contains("temperate") && d["presets"].contains("nordic") && d["presets"].contains("dry");
		for (const char* m : { "grass", "meadow", "forest floor", "sand", "beach", "rock", "cliff", "snow", "mud", "river bed", "farmland", "road" })
			ok = ok && d["materials"].contains(m);
		check("materials and presets", ok, std::to_string(d["materials"].size()) + " materials, " + std::to_string(d["presets"].size()) + " presets from " + d.value("source", std::string("?")));
	}

	struct Made { std::string name; fs::path folder; };
	std::vector<Made> made;
	auto paint = [&](const std::string& name, const fs::path& f, oj& mr) {
		const MapFolder::Result r = MapFolder::Compile(f, true);
		if (!r.ok) { check((name + " compiled").c_str(), false, r.error); return false; }
		const oj rep = oj::parse(Bytes(f / "build" / "report.json"));
		if (!rep.contains("materials")) { check((name + " painted").c_str(), false, "no materials in the report"); return false; }
		mr = rep["materials"];
		std::string shares;
		for (const oj& m : mr["materials"])
			shares += m["material"].get<std::string>() + " " + std::to_string((int)std::lround(m["share"].get<double>() * 1000)) + "; ";
		printf("MATERIALSTATS %s %s blocked %.3f slow %.3f, %zu unmatched (%d corners, %d edges), %.0f ms (corners %d passes, %d vertices changed, %d repair passes)\n",
			name.c_str(), shares.c_str(), mr["passability"]["blockedShare"].get<double>(), mr["passability"]["slowShare"].get<double>(),
			mr["unmatched"]["count"].get<size_t>(), mr["unmatched"]["corners"].get<int>(), mr["unmatched"]["edges"].get<int>(), mr["timeMs"]["total"].get<double>(),
			mr["solver"]["legalisePasses"].get<int>(), mr["solver"]["verticesChanged"].get<int>(), mr["solver"]["repairPasses"].get<int>());
		for (size_t k = 0; k < mr["unmatched"]["list"].size() && k < 40; k++) {
			const oj& u = mr["unmatched"]["list"][k];
			printf("MATERIALSTATS %s unmatched %s at %s: %s\n", name.c_str(), u["kind"].get<std::string>().c_str(), u["atM"].dump().c_str(), u["reason"].get<std::string>().c_str());
		}
		fflush(stdout);
		// every unmatched tile reported with its place and a reason
		const size_t n = mr["unmatched"]["count"].get<size_t>();
		bool reported = mr["unmatched"]["list"].size() == std::min<size_t>(n, 2000);
		for (const oj& u : mr["unmatched"]["list"]) reported = reported && u.contains("tile") && !u["reason"].get<std::string>().empty();
		check((name + " unmatched edges none or reported").c_str(), reported && n <= 2000, std::to_string(n) + " unmatched, by groups " + mr["unmatched"]["edgesByGroups"].dump());
		return true;
	};
	auto generate = [&](const fs::path& f) {
		oj st;
		std::string e;
		if (!MapTerrain::Generate(f, st, e)) { check("generated", false, e); return false; }
		return true;
	};

	// 1. the island, temperate: beaches, sea cliffs, a ridge with rock, the river's bed
	{
		const oj terrain = { { "base", 8 }, { "seaLevel", 16 }, { "beach", 2 },
			{ "noise", { { "amplitude", 5 }, { "scale", 450 }, { "ridged", 0.3 } } }, { "erosion", { { "hydraulic", 1 }, { "thermal", 1 } } } };
		const oj features = oj::array({
			{ { "kind", "island" }, { "id", "main" }, { "at", { 640, 640 } }, { "radius", { 500, 420 } }, { "angle", 20 }, { "height", 48 }, { "irregularity", 0.8 } },
			{ { "kind", "island" }, { "id", "islet" }, { "at", { 1140, 170 } }, { "radius", 90 }, { "height", 22 } },
			{ { "kind", "range" }, { "id", "spine" }, { "line", { { 380, 560 }, { 640, 780 }, { 900, 820 } } }, { "width", 220 }, { "height", { 62, 78 } }, { "passes", 1 } },
			{ { "kind", "plateau" }, { "id", "west base" }, { "role", "player 1 base" }, { "at", { 420, 360 } }, { "radius", 95 }, { "height", 26 }, { "ramps", 2 } },
			{ { "kind", "flat" }, { "id", "east base" }, { "role", "player 2 base" }, { "at", { 900, 520 } }, { "radius", 85 } },
			{ { "kind", "river" }, { "id", "burn" }, { "line", { { 700, 790 }, { 860, 930 }, { 1000, 1120 } } }, { "width", 14 }, { "depth", 2.5 } } });
		const fs::path f = MakeMap("Materials Island", terrain, features, Players(420, 360, 900, 520), { { "preset", "temperate" } });
		oj mr;
		if (generate(f) && paint("island", f, mr)) {
			made.push_back({ "island", f });
			bool beach = false;
			for (const oj& m : mr["materials"]) beach = beach || (m["material"] == "beach" && m["share"].get<double>() > 0.01);
			check("island has beaches", beach, "");
		}
	}
	// 2. the river valley, temperate, with a track along a mask (layers/track.png)
	{
		const oj terrain = { { "base", 30 }, { "noise", { { "amplitude", 12 }, { "scale", 450 }, { "ridged", 0.3 } } },
			{ "erosion", { { "hydraulic", 1 }, { "thermal", 1 } } } };
		const oj features = oj::array({
			{ { "kind", "range" }, { "id", "north ridge" }, { "line", { { -150, 1100 }, { 400, 1140 }, { 800, 1070 }, { 1430, 1120 } } }, { "width", 320 }, { "height", { 95, 110 } }, { "passes", 1 } },
			{ { "kind", "range" }, { "id", "south ridge" }, { "line", { { -150, 180 }, { 450, 150 }, { 900, 230 }, { 1430, 170 } } }, { "width", 300 }, { "height", { 90, 100 } },
				{ "passes", oj::array({ { 640, 190 } }) } },
			{ { "kind", "valley" }, { "id", "dale" }, { "line", { { -150, 720 }, { 640, 630 }, { 1430, 560 } } }, { "width", 560 }, { "depth", 8 }, { "fall", 6 } },
			{ { "kind", "river" }, { "id", "river" }, { "line", { { -150, 730 }, { 640, 640 }, { 1430, 560 } } }, { "width", 22 }, { "depth", 3 }, { "meander", 0.5 }, { "fords", 2 } },
			{ { "kind", "terraces" }, { "id", "fields" }, { "at", { 960, 410 } }, { "radius", 190 }, { "step", 5 }, { "strength", 0.6 } },
			{ { "kind", "hill" }, { "id", "knoll" }, { "at", { 960, 410 } }, { "radius", 130 }, { "height", 42 } } });
		const oj ground = { { "preset", "temperate" },
			{ "rules", oj::array({ { { "material", "track" }, { "mask", "track" }, { "slope", { nullptr, 20 } } },
				{ { "material", "farmland" }, { "heightRel", { nullptr, 0.45 } }, { "moisture", { 0.3, 0.8 } }, { "slope", { nullptr, 6 } }, { "noise", { 0.6, nullptr } }, { "noiseScale", 260 } } }) } };
		const fs::path f = MakeMap("Materials River Valley", terrain, features, Players(300, 480, 1000, 800), ground);
		// the track: a winding line from the west base over the river to the east base (a mask picture,
		// the whole map, row 0 north)
		{
			const int n = 317;
			std::vector<uint8_t> px((size_t)n * n, 0);
			for (int i = 0; i <= 400; i++) {
				const float u = i / 400.0f;
				const float x = 300 + (1000 - 300) * u + 60 * std::sin(u * 6.28f), z = 480 + (800 - 480) * u + 50 * std::sin(u * 9.4f);
				const int cx = (int)(x / 5) + 30, cz = (int)(z / 5) + 30;
				for (int oz = -1; oz <= 1; oz++)
					for (int ox = -1; ox <= 1; ox++) {
						const int px_ = cx + ox, pr = n - 1 - (cz + oz);
						if (px_ >= 0 && pr >= 0 && px_ < n && pr < n) px[(size_t)pr * n + px_] = 255;
					}
			}
			std::error_code ec;
			fs::create_directories(f / "layers", ec);
			stbi_write_png((f / "layers" / "track.png").string().c_str(), n, n, 1, px.data(), n);
		}
		oj mr;
		if (generate(f) && paint("valley", f, mr)) {
			made.push_back({ "valley", f });
			int bed = 0, track = 0;
			for (const oj& m : mr["materials"]) {
				if (m["material"] == "river bed") bed = m["tiles"].get<int>();
				if (m["material"] == "track") track = m["tiles"].get<int>();
			}
			check("valley river bed and track", bed > 200 && track > 50, "river bed " + std::to_string(bed) + " tiles, track " + std::to_string(track));
		}
	}
	// 3. the highland, nordic, with hand tiles (a cobbled square on the west plateau) that must stay
	//    through a change of the rules
	fs::path highland;
	{
		const oj terrain = { { "base", 60 }, { "noise", { { "amplitude", 14 }, { "scale", 550 }, { "ridged", 0.45 } } },
			{ "erosion", { { "hydraulic", 1.2 }, { "thermal", 1 } } } };
		const oj features = oj::array({
			{ { "kind", "plateau" }, { "id", "west plateau" }, { "role", "player 1 base" }, { "at", { 250, 250 } }, { "radius", 170 }, { "height", 72 }, { "cliffs", 0.6 }, { "ramps", 2 } },
			{ { "kind", "plateau" }, { "id", "east plateau" }, { "role", "player 2 base" }, { "at", { 1030, 1030 } }, { "radius", 170 }, { "height", 74 }, { "cliffs", 0.6 }, { "ramps", 2 } },
			{ { "kind", "range" }, { "id", "divide" }, { "line", { { -100, 980 }, { 500, 720 }, { 780, 520 }, { 1380, 280 } } }, { "width", 340 }, { "height", { 140, 160 } },
				{ "passes", 2 }, { "passHeight", 96 } },
			{ { "kind", "range" }, { "id", "spur" }, { "line", { { 150, 1380 }, { 380, 950 } } }, { "width", 240 }, { "height", 125 } },
			{ { "kind", "hill" }, { "id", "peak" }, { "at", { 1060, 300 } }, { "radius", 190 }, { "height", 140 }, { "shape", "peak" } },
			{ { "kind", "basin" }, { "id", "tarn" }, { "at", { 620, 1030 } }, { "radius", 150 }, { "depth", 14 } } });
		highland = MakeMap("Materials Highland", terrain, features, Players(250, 250, 1030, 1030), { { "preset", "nordic" } });
		// hand tiles: COBBLES1 in a 6 x 6 square at playable tiles (40-45, 60-65) (whole map +30)
		const int W = 316;
		oj ht = { { "format", "wkbai hand tiles" }, { "version", 1 }, { "tiles", { W, W } }, { "textures", oj::array({ oj::array({ "COBBLES1", 0 }) }) } };
		oj rows = oj::array();
		for (int z = 0; z < W; z++) {
			if (z < 90 || z > 95) { rows.push_back("-*" + std::to_string(W)); continue; }
			std::string row = "-*70";
			for (int x = 70; x <= 75; x++) row += " 0." + std::to_string((x + z) & 1) + std::to_string(x & 1);
			row += " -*" + std::to_string(W - 76);
			rows.push_back(row);
		}
		ht["rows"] = rows;
		Put(highland / "tiles.hand.json", ht.dump(1) + "\n");
		const std::string handBefore = Bytes(highland / "tiles.hand.json");
		oj mr;
		if (generate(highland) && paint("highland", highland, mr)) {
			made.push_back({ "highland", highland });
			const std::vector<std::string> a = TilesOf(highland);
			// determinism: the same again
			MapFolder::Compile(highland, true);
			const std::vector<std::string> b = TilesOf(highland);
			check("deterministic", a == b, "");
			// a rule changed (snow lower, a heath material added): the hand tiles stay, the rest follows
			oj mj = oj::parse(Bytes(highland / "map.json"));
			mj["ground"]["materials"] = { { "heath", { { "description", "barren grass and shrubs" }, { "groups", oj::array({ oj::array({ "GRASS5", 2 }), oj::array({ "SHRUB1", 1 }) }) } } } };
			mj["ground"]["rules"] = oj::array({ { { "material", "snow" }, { "heightRel", { 0.5, nullptr } }, { "slope", { nullptr, 30 } } },
				{ { "material", "heath" }, { "moisture", { nullptr, 0.3 } }, { "slope", { nullptr, 15 } } } });
			Put(highland / "map.json", mj.dump(1) + "\n");
			oj mr2;
			paint("highland changed", highland, mr2);
			const std::vector<std::string> c = TilesOf(highland);
			int kept = 0, handN = 0, changed = 0;
			for (int z = 90; z <= 95; z++)
				for (int x = 70; x <= 75; x++) {
					handN++;
					const std::string want = std::string("COBBLES1 0 ") + std::to_string((x + z) & 1) + std::to_string(x & 1) + "0";
					kept += c[(size_t)z * W + x] == want && a[(size_t)z * W + x] == want;
				}
			for (size_t i = 0; i < a.size(); i++) changed += a[i] != c[i];
			check("hand tiles kept through a rule change", kept == handN && Bytes(highland / "tiles.hand.json") == handBefore && changed > 1000,
				std::to_string(kept) + " of " + std::to_string(handN) + " hand tiles, tiles.hand.json " + (Bytes(highland / "tiles.hand.json") == handBefore ? "unchanged" : "CHANGED")
				+ ", " + std::to_string(changed) + " rule tiles changed");
			// another seed: other tiles
			mj["seed"] = 99;
			Put(highland / "map.json", mj.dump(1) + "\n");
			MapFolder::Compile(highland, true);
			const std::vector<std::string> d = TilesOf(highland);
			check("another seed, other tiles", d != c, "");
			// back to the nordic preset alone for the shots
			mj["seed"] = 1705;
			mj["ground"] = { { "preset", "nordic" } };
			Put(highland / "map.json", mj.dump(1) + "\n");
			MapFolder::Compile(highland, true);
		}
	}
	// 4. the dry preset on the valley's land (a copy)
	{
		const fs::path src = g_userPath / "saved" / "Maps" / "Materials River Valley.wkbai";
		const fs::path f = g_userPath / "saved" / "Maps" / "Materials Dry Valley.wkbai";
		std::error_code ec;
		fs::remove_all(f, ec);
		fs::create_directories(f, ec);
		for (const char* n : { "map.json", "heights.png", "water.json" })
			fs::copy_file(src / n, f / n, fs::copy_options::overwrite_existing, ec);
		oj mj = oj::parse(Bytes(f / "map.json"));
		mj["name"] = "Materials Dry Valley";
		mj["ground"] = { { "preset", "dry" } };
		Put(f / "map.json", mj.dump(1) + "\n");
		oj mr;
		if (paint("dry valley", f, mr)) {
			bool sand = false;
			for (const oj& m : mr["materials"]) sand = sand || (m["material"] == "sand" && m["share"].get<double>() > 0.2);
			check("dry preset is sandy", sand, "");
		}
	}
	// compiled, loaded as the skirmish list names them, played a minute: the Manors still stand
	for (const Made& mm : made) {
		Server server;
		server.loadSaveGame(MapFolder::GamePathOf(mm.folder).c_str());
		g_settings["simSeed"] = 7u;
		server.startLevel();
		server.timeManager.unpause();
		Step(server, 3000);
		int manors = 0;
		auto count = [&](CommonGameObject* obj, auto& self) -> void {
			for (const auto& [type, children] : obj->children)
				for (CommonGameObject* child : children) {
					if (child->blueprint->name == "Manor" && !child->dyncast<ServerGameObject>()->deleted) manors++;
					self(child, self);
				}
		};
		count(server.getLevel(), count);
		check((mm.name + " plays").c_str(), manors == 2, std::to_string(manors) + " Manors standing after 60 s");
	}
	printf("MATERIALS all %d\n", (int)all);
	fflush(stdout);
}
