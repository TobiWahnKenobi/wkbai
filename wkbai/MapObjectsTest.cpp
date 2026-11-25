// wkbai - headless test of step 17.7 (MapObjects.h): a generated map with four bases (two starts
// given, two chosen), their buildings laid out by intent, every building passing the server's
// own placement rules, fair gold and stone near every base, forests by moisture, the checks in
// the compiler's report and in describe_map, a building placed by hand where the rules refuse it
// reported with its place, the same objects from the same map twice, the city working in the
// game (a house fits by the Manor) and the map played a minute.
// Test "Map objects" in test.cpp.
// Licensed under the GNU General Public License 3

#include "MapObjects.h"
#include "MapTerrain.h"
#include "MapFolder.h"
#include "MapAuthoring.h"
#include "StampdownPlan.h"
#include "server.h"
#include "terrain.h"
#include "file.h"
#include "settings.h"
#include "gameset/gameset.h"
#include "gameset/GameObjBlueprint.h"
#include "tags.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <regex>
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
	void Step(Server& server, int steps)
	{
		server.timeManager.stepMs = 20;
		for (int i = 0; i < steps; i++) {
			server.timeManager.pendingMs = server.timeManager.stepMs;
			if (server.timeManager.step())
				server.simulate();
		}
	}
	std::string F(double v)
	{
		char b[32];
		snprintf(b, sizeof(b), "%.2f", v);
		return b;
	}
}

void Test_MapObjects()
{
	LoadBCP("data.bcp");
	bool all = true;
	auto check = [&](const char* what, bool ok, const std::string& detail) {
		printf("MAPOBJ %s %d %s\n", what, (int)ok, detail.c_str());
		fflush(stdout);
		all = all && ok;
	};
	const fs::path folder = g_userPath / "saved" / "Maps" / "4 Player Map Objects Test.wkbai";
	std::error_code ec;
	fs::remove_all(folder, ec);
	fs::create_directories(folder, ec);
	Terrain ref;
	ref.readBCM("Maps\\128_4Peaks\\128_4Peaks.bcm");
	std::string db, sky;
	for (wchar_t c : ref.texDbPath) if (c) db += (char)c;
	for (wchar_t c : ref.skyTextureDirectory) if (c) sky += (char)c;
	const int playable = 256, edge = 30, w = playable + 2 * edge;
	oj mj;
	mj["format"] = "wkbai map";
	mj["version"] = 1;
	mj["name"] = "Map Objects Test";
	mj["size"] = { { "tiles", { w, w } }, { "edge", edge } };
	mj["heights"] = { { "scale", 1.0 } };
	mj["textures"] = { { "database", db }, { "translation", "Maps\\Map_Textures\\128_4Peaks.lttt" } };
	mj["sky"] = sky;
	mj["seed"] = 1707;
	// two starts given (west and east), two chosen by the compiler
	mj["players"] = oj::array({
		{ { "name", "West" }, { "colour", 1 }, { "start", { 220, 640 } }, { "base", "Manor" }, { "peasants", 4 } },
		{ { "name", "East" }, { "colour", 2 }, { "start", { 1060, 640 } }, { "base", "Manor" }, { "peasants", 4 } },
		{ { "name", "North" }, { "colour", 3 }, { "start", "auto" }, { "base", "Manor" }, { "peasants", 4 } },
		{ { "name", "South" }, { "colour", 4 }, { "start", "auto" }, { "base", "Manor" }, { "peasants", 4 } } });
	mj["bases"] = { { "edgeM", 120 }, { "waterM", 50 }, { "minApartM", 400 },
		{ "buildings", oj::array({
			{ { "blueprint", "City Houses" }, { "count", 3 }, { "distance", { 12, 40 } } },
			{ { "blueprint", "Barracks Stone" }, { "count", 1 }, { "distance", { 15, 45 } }, { "toward", "centre" } },
			{ { "blueprint", "Standard Farm" }, { "count", 2 }, { "distance", { 30, 70 } }, { "toward", "away" } } }) } };
	// a river north to south through the middle (water), a lake basin in the north-east, hills
	mj["terrain"] = { { "base", 30 }, { "noise", { { "amplitude", 5 }, { "scale", 280 } } } };
	mj["features"] = oj::array({
		{ { "kind", "river" }, { "id", "midriver" }, { "line", { { 650, 1290 }, { 620, 640 }, { 660, -10 } } }, { "width", 14 }, { "depth", 2.0 }, { "meander", 0.35 },
			{ "water", true } },
		{ { "kind", "basin" }, { "id", "mere" }, { "at", { 1000, 1030 } }, { "radius", 110 }, { "depth", 9 }, { "water", 28.3 } },
		{ { "kind", "hill" }, { "id", "sw hill" }, { "at", { 300, 250 } }, { "radius", 160 }, { "height", 60 } },
		{ { "kind", "hill" }, { "id", "ne hill" }, { "at", { 1050, 300 } }, { "radius", 130 }, { "height", 55 } } });
	mj["ground"] = { { "preset", "temperate" } };
	const oj oak = oj::array({ "Deciduous Tree", oj::array({ "Oak01", "Oak02", "Oak03" }) }), pine = oj::array({ "Coniferous Tree", oj::array({ "Pine01", "Pine02" }) });
	mj["scatter"] = oj::array({
		// forests by moisture: none on dry ground, full on the wettest
		{ { "id", "forest" }, { "objects", oj::array({ oak }) }, { "objectsHigh", oj::array({ pine }) }, { "highFrom", 40 }, { "highTo", 60 }, { "highMin", 0.1 },
			{ "highMax", 0.9 }, { "moisture", { 0.55, 0.8 } }, { "density", 90 }, { "spacing", 6 }, { "clump", 0.4 }, { "max", 8000 }, { "awayFromBases", 70 } },
		// a woodlot, gold and stone by every base, the same for each
		{ { "id", "woodlot" }, { "near", "bases" }, { "objects", oj::array({ oak }) }, { "groups", 1 }, { "size", 20 }, { "spread", 16 }, { "distance", { 75, 110 } } },
		{ { "id", "gold" }, { "near", "bases" }, { "objects", oj::array({ "Gold Deposit Medium" }) }, { "groups", 2 }, { "size", 3 }, { "distance", { 70, 150 } } },
		{ { "id", "stone" }, { "near", "bases" }, { "objects", oj::array({ "Rock Outcrop Medium" }) }, { "groups", 2 }, { "size", 3 }, { "distance", { 70, 150 } } },
		// lone trees on the open grass, few
		{ { "id", "lone" }, { "objects", oj::array({ oak }) }, { "on", oj::array({ "grass", "meadow" }) }, { "density", 1.5 }, { "spacing", 10 } } });
	std::vector<uint16_t> flat((size_t)(w + 1) * (w + 1), 30 * 256);
	MapFolder::WritePng16(folder / "heights.png", w + 1, w + 1, flat);
	std::ofstream(folder / "map.json", std::ios::binary) << mj.dump(1) << "\n";
	oj st;
	std::string err;
	if (!MapTerrain::Generate(folder, st, err)) { check("terrain", false, err); printf("MAPOBJ all 0\n"); return; }
	const MapFolder::Result r = MapFolder::Compile(folder, true);
	if (!r.ok) { check("compiled", false, r.error); printf("MAPOBJ all 0\n"); return; }
	const oj rep = oj::parse(Text(folder / "build" / "report.json"));

	// 1. the bases: four civic centres, all on sites the server's rules accept, their buildings
	const oj bases = rep.value("bases", oj::object());
	int centres = 0, centresPassed = 0, asked = 0, placed = 0, peasants = 0;
	std::string bd;
	for (const oj& b : bases.value("players", oj::array())) {
		centres++;
		centresPassed += b["centre"].value("passed", false);
		asked += b.value("asked", 0);
		placed += b.value("placed", 0);
		peasants += b.value("peasants", 0);
		bd += "(" + F(b["centre"]["at"][0].get<double>()) + ", " + F(b["centre"]["at"][1].get<double>()) + ") moved " + F(b["centre"]["movedM"].get<double>()) + " m; ";
	}
	check("bases", centres == 4 && centresPassed == 4 && asked == 24 && placed == 24 && peasants == 16,
		std::to_string(centres) + " centres (" + std::to_string(centresPassed) + " on accepted sites), " + std::to_string(placed) + " of " + std::to_string(asked)
			+ " buildings, " + std::to_string(peasants) + " peasants: " + bd);
	// 2. the chosen starts: on land, away from the water, apart from the others
	const oj fair = rep.value("fairness", oj::object());
	double nearest = 1e9;
	for (const oj& b : fair.value("bases", oj::array()))
		if (b["nearestBaseM"].is_number()) nearest = std::min(nearest, b["nearestBaseM"].get<double>());
	const oj autoS = bases.value("autoStarts", oj::object());
	check("starts chosen", autoS.value("chosen", oj::array()).size() == 2 && nearest >= 400,
		autoS.value("chosen", oj::array()).dump() + ", " + std::to_string(autoS.value("candidates", 0)) + " candidates, the closest two bases " + F(nearest) + " m apart");
	// 3. every building passes the server's rules, no peasant in a building or the water, no tree
	// or deposit on a building
	const oj pl = rep.value("placement", oj::object());
	check("placement", pl.value("ok", false) && pl.value("buildings", 0) == 28 && pl.value("passed", 0) == 28,
		std::to_string(pl.value("passed", 0)) + " of " + std::to_string(pl.value("buildings", 0)) + " buildings pass, " + std::to_string(pl.value("charactersBlocked", -1))
			+ " characters blocked, " + std::to_string(pl.value("containersOnBuildings", -1)) + " containers on buildings; " + pl.value("byBlueprint", oj::object()).dump());
	// 4. fair: the same gold and stone at every base (and the woodlot's trees)
	const oj ratios = fair.value("ratios", oj::object());
	const oj nb = rep.value("scatterNearBases", oj::object());
	auto even = [&](const char* k, int each) {
		const oj a = nb.value(k, oj::array());
		if (a.size() != 4) return false;
		for (const oj& v : a) if (v.get<int>() != each) return false;
		return true;
	};
	const double goldR = ratios.contains("Gold") ? ratios["Gold"].value("ratio", 0.0) : 0, stoneR = ratios.contains("Stone") ? ratios["Stone"].value("ratio", 0.0) : 0;
	check("fair resources", even("gold", 6) && even("stone", 6) && even("woodlot", 20) && goldR > 0.99 && stoneR > 0.99,
		"near the bases " + nb.dump() + "; within 200 m gold " + (ratios.contains("Gold") ? ratios["Gold"].dump() : "-") + ", stone "
			+ (ratios.contains("Stone") ? ratios["Stone"].dump() : "-") + ", wood " + (ratios.contains("Wood") ? ratios["Wood"].dump() : "-"));
	// 5. forests by moisture: the trees stand where the land is wetter, none on water, none in a
	// town (a city rectangle and two tiles round it)
	const oj sm = rep.value("scatterMoisture", oj::object()).value("forest", oj::object());
	const int forest = rep.value("scatter", oj::object()).value("forest", 0);
	Terrain t;
	t.readFromFile(MapFolder::TerrainFileOf(MapFolder::GamePathOf(folder)).c_str());
	const std::string level = Text(folder / "build" / "level.txt");
	struct Rect { int x0, z0, x1, z1; };
	std::vector<Rect> rects;
	{
		const std::regex re("RECTANGLE (-?\\d+) (-?\\d+) (-?\\d+) (-?\\d+)");
		for (auto it = std::sregex_iterator(level.begin(), level.end(), re); it != std::sregex_iterator(); ++it)
			rects.push_back({ std::stoi((*it)[1]), std::stoi((*it)[2]), std::stoi((*it)[3]), std::stoi((*it)[4]) });
	}
	int onWater = 0, inTown = 0, trees = 0;
	{
		const std::regex re("CONTAINER \\d+ \"([^\"]+)\" \\n\\t+POSITION ([-\\d.]+) [-\\d.]+ ([-\\d.]+)");
		for (auto it = std::sregex_iterator(level.begin(), level.end(), re); it != std::sregex_iterator(); ++it) {
			const float x = std::stof((*it)[2]), z = std::stof((*it)[3]);
			trees += (*it)[1].str().find("Tree") != std::string::npos;
			const TerrainTile* tile = t.getTile((int)(x / 5) + t.edge, (int)(z / 5) + t.edge);
			onWater += tile && tile->fullOfWater;
			const int tx = (int)(x / 5), tz = (int)(z / 5);
			for (const Rect& rc : rects)
				if (tx >= rc.x0 - 2 && tx <= rc.x1 + 2 && tz >= rc.z0 - 2 && tz <= rc.z1 + 2) { inTown++; break; }
		}
	}
	check("forests by moisture", forest > 300 && sm.value("underObjects", 0.0) > sm.value("land", 1.0) + 0.1 && onWater == 0 && inTown == 0 && !rects.empty(),
		std::to_string(forest) + " forest trees (" + std::to_string(trees) + " trees in all), moisture under them " + F(sm.value("underObjects", 0.0)) + " against the land's "
			+ F(sm.value("land", 0.0)) + "; " + std::to_string(onWater) + " objects on water, " + std::to_string(inTown) + " in a town (" + std::to_string(rects.size()) + " city rectangles)");
	// 6. describe_map: the same checks on the map as the server loads it
	{
		oj d;
		std::string derr;
		const bool ok = MapAuthoring::Describe(MapFolder::GamePathOf(folder), "", d, derr);
		const oj dp = ok ? d.value("placement", oj::object()) : oj::object();
		const oj df = ok ? d.value("fairness", oj::object()) : oj::object();
		check("describe_map", ok && dp.value("ok", false) && dp.value("buildings", 0) == 28 && df.value("bases", oj::array()).size() == 4,
			ok ? std::to_string(dp.value("passed", 0)) + " of " + std::to_string(dp.value("buildings", 0)) + " pass on the loaded server; " + std::to_string(df.value("bases", oj::array()).size())
				+ " bases" : derr);
	}
	// 7. the same map twice: the same objects
	{
		const std::string before = level;
		const MapFolder::Result r2 = MapFolder::Compile(folder, true);
		const std::string after = Text(folder / "build" / "level.txt");
		check("deterministic", r2.ok && before == after, std::to_string(before.size()) + " and " + std::to_string(after.size()) + " bytes");
	}
	// 8. in the game: the Manors stand in their cities and a house fits beside each, as a peasant
	// would place it (the city's rectangles connect it); a minute played
	{
		Server server;
		server.loadSaveGame(MapFolder::GamePathOf(folder).c_str());
		const GameObjBlueprint* house = server.gameSet->findBlueprint(Tags::GAMEOBJCLASS_BUILDING, "City Houses");
		int manorsInCity = 0, houseFits = 0;
		std::vector<CommonGameObject*> manors;
		auto walk = [&](CommonGameObject* obj, auto& self) -> void {
			for (const auto& [type, children] : obj->children)
				for (CommonGameObject* child : children) {
					if (child->blueprint->name == "Manor") {
						manors.push_back(child);
						manorsInCity += child->getParent() && child->getParent()->blueprint->bpClass == Tags::GAMEOBJCLASS_CITY;
					}
					self(child, self);
				}
		};
		walk(server.getLevel(), walk);
		for (CommonGameObject* mo : manors) {
			bool fits = false;
			for (int r = 3; r <= 9 && !fits; r++)
				for (int a = 0; a < 16 && !fits; a++) {
					const float x = mo->position.x + std::cos(a * 0.3927f) * r * 5, z = mo->position.z + std::sin(a * 0.3927f) * r * 5;
					const Vector3 p(x, server.terrain->getHeight(x, z), z);
					fits = house && StampdownPlan::getStampdownPlan(&server, mo->getPlayer(), house, p, Vector3(0, 0, 0)).status == StampdownPlan::Status::Valid;
				}
			houseFits += fits;
		}
		g_settings["simSeed"] = 7u;
		server.startLevel();
		server.timeManager.unpause();
		Step(server, 3000);
		int standing = 0;
		auto count = [&](CommonGameObject* obj, auto& self) -> void {
			for (const auto& [type, children] : obj->children)
				for (CommonGameObject* child : children) {
					if (child->blueprint->bpClass == Tags::GAMEOBJCLASS_BUILDING && !child->dyncast<ServerGameObject>()->deleted) standing++;
					self(child, self);
				}
		};
		count(server.getLevel(), count);
		check("in the game", manors.size() == 4 && manorsInCity == 4 && houseFits == 4 && standing == 28,
			std::to_string(manors.size()) + " Manors, " + std::to_string(manorsInCity) + " in a city, a house fits by " + std::to_string(houseFits) + "; "
				+ std::to_string(standing) + " buildings standing after " + F(server.timeManager.currentTime) + " s");
	}
	// 9. a building placed by hand where the rules refuse it (a house in the river): reported with
	// its place; the bases are laid out round it again
	{
		float wx = -1, wz = -1;
		for (int z = 100; z < 160 && wx < 0; z++)
			for (int x = 100; x < 160; x++)
				if (const TerrainTile* tile = t.getTile(x + t.edge, z + t.edge); tile && tile->fullOfWater) { wx = x * 5 + 2.5f, wz = z * 5 + 2.5f; break; }
		std::string lv = Text(folder / "build" / "level.txt");
		// the West player's block gets the house (hand-made: not in the authoring section)
		const size_t pp = lv.find("PLAYER 10 ");
		const size_t endp = pp == std::string::npos ? pp : lv.find("\tEND_PLAYER", pp);
		bool reported = false;
		std::string detail = "no water tile found";
		if (wx >= 0 && endp != std::string::npos) {
			const std::string house = "\t\tBUILDING 900000 \"City Houses\" \n\t\t\tPOSITION " + F(wx) + " 30.00 " + F(wz) + " \n\t\t\tORIENTATION 0.00 0.00 0.00 \n\t\tEND_BUILDING \n\n";
			lv.insert(endp, house);
			std::ofstream(folder / "level.txt", std::ios::binary) << lv;
			const MapFolder::Result r3 = MapFolder::Compile(folder, true);
			const oj rep3 = oj::parse(Text(folder / "build" / "report.json"));
			const oj p3 = rep3.value("placement", oj::object());
			std::string status;
			for (const oj& f : p3.value("failed", oj::array()))
				if (f.value("id", 0) == 900000) { reported = true; status = f.value("status", "") + " at " + f["at"].dump(); }
			bool warned = false;
			for (const oj& wn : rep3.value("warnings", oj::array()))
				warned |= wn.get<std::string>().find("900000") != std::string::npos;
			detail = "a house in the river at (" + F(wx) + ", " + F(wz) + "): " + (reported ? status : "not reported") + (warned ? ", in the warnings" : "") + "; "
				+ std::to_string(p3.value("failedCount", -1)) + " failed of " + std::to_string(p3.value("buildings", 0));
			reported = reported && warned && r3.ok && p3.value("failedCount", -1) == 1 && p3.value("buildings", 0) == 29;
			fs::remove(folder / "level.txt", ec);
			MapFolder::Compile(folder, true);
		}
		check("hand building reported", reported, detail);
	}
	printf("MAPOBJ all %d\n", (int)all);
	fflush(stdout);
}
