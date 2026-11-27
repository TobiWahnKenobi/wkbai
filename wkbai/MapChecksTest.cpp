// wkbai - headless test of step 17.8 (MapChecks.h): a deliberately broken map, every planted fault
// reported by the one check pass with its place and a fix; the same map with the faults taken out
// reports none; the live paint's core (MapMaterials::PaintRegion) mends the planted seam; the
// previews (pictures and cameras above ground); the check on an original map (4 Peaks).
// Test "Map checks" in test.cpp.
// Licensed under the GNU General Public License 3

#include "MapChecks.h"
#include "MapFolder.h"
#include "MapMaterials.h"
#include "server.h"
#include "terrain.h"
#include "file.h"
#include "settings.h"
#include "TrnTextureDb.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
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
	std::string F(double v)
	{
		char b[32];
		snprintf(b, sizeof(b), "%.1f", v);
		return b;
	}
}

void Test_MapChecks()
{
	LoadBCP("data.bcp");
	bool all = true;
	auto check = [&](const char* what, bool ok, const std::string& detail) {
		printf("MAPCHECK %s %d %s\n", what, (int)ok, detail.c_str());
		fflush(stdout);
		all = all && ok;
	};
	const fs::path folder = g_userPath / "saved" / "Maps" / "3 Player Map Checks Test.wkbai";
	std::error_code ec;
	fs::remove_all(folder, ec);
	fs::create_directories(folder, ec);
	Terrain ref;
	ref.readBCM("Maps\\128_4Peaks\\128_4Peaks.bcm");
	std::string db, sky;
	for (wchar_t c : ref.texDbPath) if (c) db += (char)c;
	for (wchar_t c : ref.skyTextureDirectory) if (c) sky += (char)c;
	// a picture of SAND1 for the planted seam
	int sandId = -1;
	if (auto g = ref.texDb.groups.find("SAND1"); g != ref.texDb.groups.end() && g->second && !g->second->textures.empty())
		sandId = g->second->textures.begin()->first;
	const int playable = 192, edge = 30, w = playable + 2 * edge;
	const float base = 20.0f;

	// the planted faults (playable metres)
	const float mesaX = 720, mesaZ = 720, mesaR = 60, mesaH = 60;      // East's start on a cliff-bound mesa: unreachable
	const float islX = 220, islZ = 760, islR = 30, islH = 45;          // a second mesa with gold on it: land no unit reaches
	const float dishX = 720, dishZ = 220, dishR = 45, dishD = 0.6f;    // a lake in a shallow dish: a puddle
	const float houseX = 778, houseZ = 720;                            // a house over the mesa's cliff: refused by the server
	const int seamTx = 60, seamTz = 100;                               // a SAND1 hand tile in GRASS1: two grounds, no transition
	const float seamX = seamTx * 5 + 2.5f, seamZ = seamTz * 5 + 2.5f;

	auto writeFolder = [&](bool broken) {
		oj mj;
		mj["format"] = "wkbai map";
		mj["version"] = 1;
		mj["name"] = "Map Checks Test";
		mj["size"] = { { "tiles", { w, w } }, { "edge", edge } };
		mj["heights"] = { { "scale", 1.0 } };
		mj["textures"] = { { "database", db }, { "translation", "Maps\\Map_Textures\\128_4Peaks.lttt" } };
		mj["sky"] = sky;
		mj["seed"] = 1708;
		mj["ground"] = { { "fill", "GRASS1" } };
		oj players = oj::array({
			{ { "name", "West" }, { "colour", 1 }, { "start", { 200, 200 } }, { "base", "Manor" }, { "peasants", 4 } },
			{ { "name", "East" }, { "colour", 2 }, { "start", broken ? oj{ mesaX, mesaZ } : oj{ 760, 760 } }, { "base", "Manor" }, { "peasants", 4 } } });
		// a third player with no start: no base, no camera
		if (broken)
			players.push_back({ { "name", "North" }, { "colour", 3 } });
		mj["players"] = players;
		std::ofstream(folder / "map.json", std::ios::binary) << mj.dump(1) << "\n";
		std::vector<uint16_t> px((size_t)(w + 1) * (w + 1));
		for (int r = 0; r <= w; r++)
			for (int x = 0; x <= w; x++) {
				const int z = w - r;
				const float mx = (x - edge) * 5.0f, mz = (z - edge) * 5.0f;
				float h = base;
				if (broken) {
					if (std::hypot(mx - mesaX, mz - mesaZ) <= mesaR) h = mesaH;
					if (std::hypot(mx - islX, mz - islZ) <= islR) h = islH;
					const float d = std::hypot(mx - dishX, mz - dishZ);
					if (d < dishR) h = base - dishD * (1 - (d / dishR) * (d / dishR));
				}
				px[(size_t)r * (w + 1) + x] = (uint16_t)std::lround(h * 256);
			}
		MapFolder::WritePng16(folder / "heights.png", w + 1, w + 1, px);
		fs::remove(folder / "water.json", ec);
		fs::remove(folder / "tiles.hand.json", ec);
		fs::remove(folder / "level.txt", ec);
		if (!broken)
			return;
		// the puddle's lake point, a little below the dish's rim
		oj wj = { { "lakes", oj::array({ oj::array({ dishX + edge * 5.0f, base - 0.2f, dishZ + edge * 5.0f, 0 }) }) } };
		std::ofstream(folder / "water.json", std::ios::binary) << wj.dump(1) << "\n";
		// the seam
		oj hj = { { "format", "wkbai hand tiles" }, { "version", 1 }, { "tiles", { w, w } }, { "textures", oj::array({ oj::array({ "SAND1", sandId }) }) } };
		oj rows = oj::array();
		for (int z = 0; z < w; z++)
			rows.push_back(z == seamTz + edge ? "-*" + std::to_string(seamTx + edge) + " 0.00 -*" + std::to_string(w - seamTx - edge - 1) : "-*" + std::to_string(w));
		hj["rows"] = rows;
		std::ofstream(folder / "tiles.hand.json", std::ios::binary) << hj.dump(1) << "\n";
	};

	// 1. the broken map: compiled, then the hand objects put into its level (a house over the
	// cliff for East, West's gold and gold on the second mesa for the Neutral Player), compiled again
	writeFolder(true);
	MapFolder::Result r = MapFolder::Compile(folder, true);
	if (!r.ok) { check("compiled", false, r.error); printf("MAPCHECK all 0\n"); return; }
	{
		std::string lv = Text(folder / "build" / "level.txt");
		auto insertIn = [&](const std::string& player, const std::string& what) {
			const size_t pp = lv.find("PLAYER " + player + " ");
			const size_t endp = pp == std::string::npos ? pp : lv.find("\tEND_PLAYER", pp);
			if (endp == std::string::npos) return false;
			lv.insert(endp, what);
			return true;
		};
		auto obj = [](const char* kind, int id, const char* bp, float x, float y, float z) {
			return std::string("\t\t") + kind + " " + std::to_string(id) + " \"" + bp + "\" \n\t\t\tPOSITION " + F(x) + " " + F(y) + " " + F(z) +
				" \n\t\t\tORIENTATION 0.00 0.00 0.00 \n\t\tEND_" + kind + " \n\n";
		};
		bool ok = insertIn("1030", obj("BUILDING", 900001, "City Houses", houseX, mesaH, houseZ));
		std::string gold;
		for (int k = 0; k < 3; k++) gold += obj("CONTAINER", 900010 + k, "Gold Deposit Medium", 260.0f + k * 12, base, 150.0f);
		gold += obj("CONTAINER", 900020, "Gold Deposit Medium", islX, islH, islZ);
		ok = ok && insertIn("1026", gold);
		std::ofstream(folder / "level.txt", std::ios::binary) << lv;
		r = MapFolder::Compile(folder, true);
		check("broken map compiled", ok && r.ok, r.ok ? "with a hand house and four gold deposits in level.txt" : r.error);
	}
	const auto t0 = std::chrono::steady_clock::now();
	oj rep, cams;
	std::string error;
	MapChecks::Grids grids;
	if (!MapChecks::Check(PathToUtf8(folder), MapChecks::Options(), rep, error, &grids, &cams)) { check("check ran", false, error); printf("MAPCHECK all 0\n"); return; }
	const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
	std::ofstream(g_userPath / "map_checks_broken.json", std::ios::binary) << rep.dump(1) << "\n";
	const oj& probs = rep["problems"];
	// a planted fault: a problem of this check (and severity) within reach of its place
	auto found = [&](const char* name, const char* chk, const char* sev, bool place, float x, float z, float reach, const char* must = nullptr) {
		const oj* best = nullptr;
		float bestD = 1e30f;
		for (const oj& p : probs) {
			if (p["check"] != chk || (sev && p["severity"] != sev)) continue;
			if (must && p["what"].get<std::string>().find(must) == std::string::npos) continue;
			if (!place) { best = &p; bestD = 0; break; }
			if (!p["at"].is_array()) continue;
			const float d = std::hypot(p["at"][0].get<float>() - x, p["at"][1].get<float>() - z);
			if (d < bestD) { bestD = d; best = &p; }
		}
		const bool ok = best && bestD <= reach && !(*best)["fix"].get<std::string>().empty();
		check(name, ok, best ? (*best)["severity"].get<std::string>() + " \"" + (*best)["what"].get<std::string>() + "\" at " + (*best)["at"].dump() +
			(place ? " (" + F(bestD) + " m from the planted place)" : "") + "; fix: " + (*best)["fix"].get<std::string>().substr(0, 90) +
			((*best).contains("fixOp") ? "; fixOp " + (*best)["fixOp"].value("op", std::string()) : "") : "not reported");
	};
	found("unreachable start", "reachability", "error", true, mesaX, mesaZ, 50, "can't be reached by land");
	found("land no unit reaches (gold on it)", "terrain", "warning", true, islX, islZ, 25, "no unit can reach");
	found("refused house", "placement", "error", true, houseX, houseZ, 10, "City Houses");
	found("puddle", "water", "warning", true, dishX, dishZ, 50, "flat ground");
	found("seam", "edges", "warning", true, seamX, seamZ, 6);
	found("unfair gold", "fairness", "warning", true, mesaX, mesaZ, 50, "Gold");
	found("player without a base", "players", "error", false, 0, 0, 0, "no units or buildings");
	found("player without a camera", "players", "warning", false, 0, 0, 0, "no start camera");
	{
		const oj& s = rep["summary"];
		int placed = 0;
		for (const oj& p : probs) placed += p["at"].is_array();
		check("summary", s["errors"].get<int>() >= 3 && s["warnings"].get<int>() >= 4 && !rep["ok"].get<bool>(),
			std::to_string(s["errors"].get<int>()) + " errors, " + std::to_string(s["warnings"].get<int>()) + " warnings, " + std::to_string(s["info"].get<int>()) +
			" info; " + std::to_string(placed) + " of " + std::to_string(probs.size()) + " with a place; by check " + s["byCheck"].dump() + "; " + F(ms) + " ms");
	}
	// the ramp the reachability fault suggests joins the two lands (its ends on each)
	{
		bool ok = false;
		std::string d = "no fixOp";
		for (const oj& p : probs)
			if (p["check"] == "reachability" && p.contains("fixOp") && p["fixOp"].value("mode", std::string()) == "ramp") {
				const oj& l = p["fixOp"]["line"];
				const float ax = l[0][0].get<float>(), az = l[0][1].get<float>(), bx = l[1][0].get<float>(), bz = l[1][1].get<float>();
				const float da = std::hypot(ax - mesaX, az - mesaZ), dbb = std::hypot(bx - mesaX, bz - mesaZ);
				// from inside the mesa's rim out over the plain, long enough for about 15 degrees down
				// the 40 m cliff (149 m)
				ok = std::min(da, dbb) <= mesaR && std::min(da, dbb) > mesaR - 15 && std::max(da, dbb) > mesaR + 140;
				d = "ramp from " + l[0].dump() + " to " + l[1].dump();
			}
		check("ramp suggested down the cliff", ok, d);
	}
	// 2. the previews: every picture written, the cameras above the ground near them
	{
		const fs::path dir = g_userPath / "map_checks_preview";
		fs::remove_all(dir, ec);
		oj pv;
		const bool ok = MapChecks::Preview(PathToUtf8(folder), PathToUtf8(dir), pv, error);
		int files = 0;
		for (const char* f : { "relief.png", "passability.png", "water.png", "fairness.png", "problems.png", "minimap.png", "cameras.json" })
			files += fs::file_size(dir / f, ec) > 100;
		// every camera at least 30 m over the highest ground within 150 m (heights from the map's
		// own picture)
		Server server;
		server.loadSaveGame(PathToUtf8(folder).c_str());
		int above = 0, n = 0;
		for (const oj& c : pv.value("cameras", oj::array())) {
			const float x = c["camera"][0].get<float>(), y = c["camera"][1].get<float>(), z = c["camera"][2].get<float>();
			float top = -1e30f;
			for (float dz = -150; dz <= 150; dz += 5)
				for (float dx = -150; dx <= 150; dx += 5)
					if (dx * dx + dz * dz <= 150 * 150) {
						const float px = std::clamp(x + dx, 0.0f, playable * 5.0f - 0.1f), pz = std::clamp(z + dz, 0.0f, playable * 5.0f - 0.1f);
						top = std::max(top, server.terrain->getHeight(px, pz));
					}
			n++;
			above += y >= top + 30;
		}
		check("previews", ok && files == 7 && n >= 7 && above == n, std::to_string(files) + " of 7 files in " + PathToUtf8(dir) + ", " + std::to_string(above) + " of " +
			std::to_string(n) + " cameras 30 m or more above the ground within 150 m" + (ok ? "" : ": " + error));
	}
	// 3. the live paint's core on the loaded map: GRASS1 over the seam mends it
	{
		Server server;
		server.loadSaveGame(PathToUtf8(folder).c_str());
		Terrain& t = *server.terrain;
		const int before = MapMaterials::EdgeFaults(t, db, 50)["count"].get<int>();
		std::vector<uint8_t> mark((size_t)t.width * t.height, 0);
		for (int dz = -1; dz <= 1; dz++)
			for (int dx = -1; dx <= 1; dx++)
				mark[(size_t)(seamTz + edge + dz) * t.width + seamTx + edge + dx] = 1;
		MapMaterials::json pr;
		const bool ok = MapMaterials::PaintRegion(t, db, "GRASS1", mark, 3, pr, error);
		const int after = MapMaterials::EdgeFaults(t, db, 50)["count"].get<int>();
		check("paint mends the seam", ok && before > 0 && after == 0, std::to_string(before) + " seam edges before, " + std::to_string(after) + " after; " + (ok ? pr.dump() : error));
		// and SAND1 painted in a circle comes with its transitions: no seam round it
		std::vector<uint8_t> disc((size_t)t.width * t.height, 0);
		for (int z = 0; z < t.height; z++)
			for (int x = 0; x < t.width; x++)
				disc[(size_t)z * t.width + x] = std::hypot((x - edge) * 5 + 2.5f - 400.0f, (z - edge) * 5 + 2.5f - 400.0f) < 25;
		const bool ok2 = MapMaterials::PaintRegion(t, db, "SAND1", disc, 4, pr, error);
		const MapMaterials::json ef = MapMaterials::EdgeFaults(t, db, 50);
		check("paint fits transitions", ok2 && ef["count"].get<int>() == 0 && pr["ringTiles"].get<int>() > 0,
			(ok2 ? pr.dump() : error) + "; seams after " + std::to_string(ef["count"].get<int>()) + " " + ef["byGroups"].dump());
	}
	// (the broken map kept for the live ops' smoke test: "3 Player Map Checks Broken.wkbai")
	{
		const fs::path keep = g_userPath / "saved" / "Maps" / "3 Player Map Checks Broken.wkbai";
		fs::remove_all(keep, ec);
		fs::copy(folder, keep, fs::copy_options::recursive, ec);
	}
	// 4. the same map with the faults taken out: no errors, no warnings
	{
		writeFolder(false);
		r = MapFolder::Compile(folder, true);
		oj clean;
		const bool ok = r.ok && MapChecks::Check(PathToUtf8(folder), MapChecks::Options(), clean, error);
		std::ofstream(g_userPath / "map_checks_clean.json", std::ios::binary) << clean.dump(1) << "\n";
		std::string list;
		for (const oj& p : clean.value("problems", oj::array()))
			if (p["severity"] != "info") list += " [" + p["check"].get<std::string>() + ": " + p["what"].get<std::string>() + "]";
		check("clean map clean", ok && clean["summary"]["faults"].get<int>() == 0,
			ok ? clean["summary"].dump() + list : r.error + error);
	}
	// 5. an original map: the check runs and gives its numbers (4 Peaks is played as shipped: its
	// faults are the original's)
	{
		const auto t1 = std::chrono::steady_clock::now();
		oj op;
		const bool ok = MapChecks::Check("2 Player 4 Peaks.lvl", MapChecks::Options(), op, error);
		const double ms1 = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t1).count();
		std::ofstream(g_userPath / "map_checks_4peaks.json", std::ios::binary) << op.dump(1) << "\n";
		check("4 Peaks checked", ok && op["reachability"]["pairs"].size() >= 1 && op["reachability"]["pairs"][0]["pathM"].is_number(),
			ok ? op["summary"].dump() + "; walkable " + op["terrain"]["walkableShare"].dump() + ", starts " + op["reachability"]["pairs"].dump() + ", symmetry " +
			op["fairness"]["symmetry"].dump() + "; " + F(ms1) + " ms" : error);
	}
	printf("MAPCHECK all %d\n", (int)all);
	fflush(stdout);
}
