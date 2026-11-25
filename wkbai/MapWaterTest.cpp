// wkbai - the headless test of water by intent (map authoring step 17.6; map-authoring.md section
// 5 "Water"): the water design's version 2 (per-point speeds, far-land hints; version 1 files as
// before), a river with a gorge and a fall (its designed current in the renderer's maps and the
// gameplay's, the fall the finder draws, no faults), lakes (one that holds, one that leaks off the
// map and drowns the next valley's, a puddle on flat ground, a river that rises and runs dry, painted
// river tiles left dry), a sea on two winds, Extend map following its hints (a sea to the east, a
// range to the north, a river turned south-east and north-east), and a map folder compiled with
// all of it (report.json "water", the plan's river speed, map.water version 2).
// Test "Water by intent" in test.cpp.
// Licensed under the GNU General Public License 3

#include "terrain.h"
#include "WaterDesign.h"
#include "MapWater.h"
#include "MapTerrain.h"
#include "MapFolder.h"
#include "ExtendMap.h"
#include "file.h"
#include "settings.h"
#include "gfx/EnhancedTerrainData.h"
#include "gfx/DiligentWaterfalls.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
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
	// a made-up map without an edge: W x H tiles, its ground from a function of world x, z
	void MakeTerrain(Terrain& t, int W, int H, const std::function<float(float, float)>& ground)
	{
		t.createEmpty(W, H);
		t.scale = 0.25f;
		t.vertices.resize((size_t)(W + 1) * (H + 1));
		t.heightFraction.assign((size_t)(W + 1) * (H + 1), 0);
		t.tiles = new Terrain::Tile[(size_t)W * H];
		for (int z = 0; z <= H; z++)
			for (int x = 0; x <= W; x++) {
				const float steps = std::clamp(ground(x * 5.0f, z * 5.0f) / t.scale, 0.0f, 255.99f);
				const size_t i = (size_t)z * (W + 1) + x;
				t.vertices[i] = (uint8_t)std::floor(steps);
				t.heightFraction[i] = (uint8_t)std::clamp((int)std::lround((steps - std::floor(steps)) * 256.0f), 0, 255);
			}
		for (int z = 0; z < H; z++)
			for (int x = 0; x < W; x++) {
				Terrain::Tile& tile = t.tiles[(size_t)z * W + x];
				tile.x = x;
				tile.z = z;
				tile.texture = nullptr;
				tile.rot = tile.xflip = tile.zflip = 0;
			}
	}
	bool Has(const std::vector<std::string>& w, const std::string& a, const std::string& b = "")
	{
		for (const std::string& s : w)
			if (s.find(a) != std::string::npos && (b.empty() || s.find(b) != std::string::npos))
				return true;
		return false;
	}
	std::string Join(const std::vector<std::string>& w)
	{
		std::string s;
		for (const std::string& x : w)
			s += (s.empty() ? "" : " | ") + x;
		return s.empty() ? "none" : s;
	}

	// ---- the gorge: 64 x 40 tiles; a stream along z = 100 from x = 20 m: a plain (1 in 25), a
	// gorge (1 in 9, walls 18 m), a 10 m fall at x = 175 m, a plain again into a lake from 260 m
	float GorgeBed(float x)
	{
		float h = 70.0f;
		h -= std::clamp(x, 0.0f, 100.0f) * 0.04f;
		h -= std::clamp(x - 100.0f, 0.0f, 75.0f) * 0.11f;
		h -= std::clamp(x - 175.0f, 0.0f, 5.0f) * 2.0f;
		h -= std::clamp(x - 180.0f, 0.0f, 80.0f) * 0.025f;
		return h;
	}
	constexpr float GORGE_LAKE_BOTTOM = 40.0f;
	float GorgeLake() { return GorgeBed(260.0f) + 0.3f; }
	float GorgeGround(float x, float z)
	{
		const float d = std::abs(z - 100.0f);
		const bool gorge = x >= 100.0f && x <= 172.0f;
		// (the channel ends in the lake: beyond it, carried on to the map's edge, the lake ran out)
		float g = GorgeBed(x) + (d < 6.0f && x < 262.0f ? 0.0f : gorge ? 3.0f + std::min((d - 6.0f) * 1.5f, 18.0f) : 3.0f + (d - 6.0f) * 0.1f);
		if (x >= 258.0f && x <= 300.0f && d < 45.0f)
			g = std::min(g, GORGE_LAKE_BOTTOM + (x < 266.0f ? (266.0f - x) * 0.6f : 0.0f));
		return g;
	}
	float GorgeSpeed(float x) { return x < 100.0f ? 0.6f : x < 178.0f ? 3.5f : 0.8f; }
	std::string GorgeDesign(bool speeds)
	{
		oj pts = oj::array(), sp = oj::array();
		for (float x = 20.0f; x <= 275.0f; x += 5.0f) {
			const float steep = std::abs(GorgeBed(x + 2.5f) - GorgeBed(x - 2.5f)) / 5.0f;
			pts.push_back({ x, 100.0f, GorgeBed(x) + (steep > 0.15f ? 0.2f : 0.4f) });
			sp.push_back(GorgeSpeed(x));
		}
		oj way = { { "points", pts }, { "width", 12 } };
		if (speeds)
			way["speeds"] = sp;
		return oj{ { "format", "wkbai water" }, { "version", speeds ? 2 : 1 }, { "tiles", { 64, 40 } }, { "waterways", oj::array({ way }) } }.dump();
	}
}

void Test_MapWater()
{
	bool all = true;
	auto check = [&](const char* what, bool ok, const std::string& detail) {
		printf("WATERINTENT %s %d %s\n", what, (int)ok, detail.c_str());
		fflush(stdout);
		all = all && ok;
	};
	char b[1024];

	// ---- 1. the format: version 1 as before; speeds and hints round trip as version 2
	{
		WaterDesign v1, v2, back, bad;
		std::string e1, e2, e3, e4;
		const bool r1 = v1.fromText(R"({"format":"wkbai water","version":1,"tiles":[10,10],"waterways":[{"points":[[0,0],[50,0]],"width":10,"speed":1.5}]})", e1);
		const bool r2 = v2.fromText(R"({"format":"wkbai water","version":2,"tiles":[10,10],"waterways":[{"points":[[0,0],[50,0],[100,0]],"width":10,"speeds":[0.5,0,3]}],
			"farland":[{"kind":"sea","toward":"east","spread":120},{"kind":"river","toward":"south-west","at":[0,25],"weight":0.5},{"kind":"mountains","toward":90}]})", e2);
		const bool r3 = r2 && back.fromText(v2.toText(), e3);
		const bool badKind = !bad.fromText(R"({"format":"wkbai water","tiles":[10,10],"farland":[{"kind":"volcano","toward":"east"}]})", e4);
		const oj t1 = oj::parse(v1.toText()), t2 = oj::parse(v2.toText());
		const bool v1Same = r1 && t1.value("version", 0) == 1 && v1.waterways[0].speeds.empty() && v1.designedSpeed(25, 0) == 1.5f;
		// the speeds: 0.5 at the first point, the middle one unset (the next's: none of its own),
		// 3 at the last; between them along the course
		const float s0 = v2.designedSpeed(0.1f, 0), s25 = v2.designedSpeed(75, 0);
		const bool hints = r3 && back.farland.size() == 3 && back.farland[0].kind == WaterDesign::FarHint::SEA && back.farland[0].degrees == 0
			&& back.farland[1].kind == WaterDesign::FarHint::RIVER && back.farland[1].hasAt && back.farland[1].degrees == 225 && back.farland[1].weight == 0.5f
			&& back.farland[2].degrees == 90 && back.waterways[0].speeds.size() == 3;
		snprintf(b, sizeof(b), "v1 read %d written v%d, speed %.2f; v2 read %d written v%d, round trip %d, speed at 0 %.2f, at 75 m %.2f; unknown kind refused %d (%s)",
			(int)r1, t1.value("version", 0), v1.designedSpeed(25, 0), (int)r2, t2.value("version", 0), (int)hints, s0, s25, (int)badKind, e4.c_str());
		check("format", v1Same && r2 && t2.value("version", 0) == 2 && hints && std::abs(s0 - 0.5f) < 0.05f && std::abs(s25 - 3.0f) < 0.05f && badKind, b);
	}

	// ---- 2. a river with a gorge and a fall
	{
		Terrain t;
		MakeTerrain(t, 64, 40, GorgeGround);
		t.lakes = { Vector3(285.0f, GorgeLake(), 100.0f) };
		t.waterDesign = std::make_shared<WaterDesign>();
		std::string e;
		t.waterDesign->fromText(GorgeDesign(true), e);
		t.floodfillWater();
		std::vector<std::string> warns;
		const nlohmann::json rep = MapWater::Check(t, -1e30f, warns);
		const auto& way = rep["waterways"][0];
		int fallsHere = 0;
		std::string falls;
		for (const auto& f : rep["waterfalls"]) {
			const float fx = f["at"][0].get<float>();
			falls += " (" + std::to_string((int)fx) + ", " + std::to_string((int)f["at"][1].get<float>()) + ") " + f["falls"].get<std::string>() + " " + std::to_string(f["dropM"].get<float>()).substr(0, 4) + " m";
			fallsHere += fx >= 165 && fx <= 190 && f["falls"] == "east" && f["dropM"].get<float>() > 4.0f;
		}
		snprintf(b, sizeof(b), "wet %.2f, rises %s, lake %s with %d tiles and %zu leaks; falls:%s; warnings: %s", way["wetShare"].get<float>(),
			way.contains("rises") ? way["rises"].dump().c_str() : "none", rep["lakes"][0]["kind"].get<std::string>().c_str(), rep["lakes"][0]["tiles"].get<int>(),
			rep["lakes"][0]["leaks"].size(), falls.c_str(), Join(warns).c_str());
		// (and the gorge's steep stretch as rapids)
		int rapidsHere = 0;
		for (const auto& r : rep["rapids"])
			rapidsHere += r["at"][0].get<float>() >= 165 && r["at"][0].get<float>() <= 185;
		const size_t rapidsAll = rep["rapids"].size();
		check("gorge: course wet, never rising, the fall found, no faults", way["wetShare"].get<float>() > 0.9f && !way.contains("rises") && !way.contains("dry")
			&& fallsHere >= 1 && rapidsHere >= 1 && rapidsAll == (size_t)rapidsHere && rep["lakes"][0]["leaks"].empty() && warns.empty(), b + std::string("; rapids ") + rep["rapids"].dump());
		// the designed current: the renderer's (rapids map g, the flow map's speed) and the
		// gameplay's, by the points' speeds; none without them
		const std::vector<uint8_t> cur = BuildDesignedCurrent(&t);
		const int W = 64, row = 20;
		const int plain = cur[(size_t)row * W + 10], gorge = cur[(size_t)row * W + 27], below = cur[(size_t)row * W + 44], land = cur[(size_t)4 * W + 10];
		std::vector<uint8_t> sea;
		const std::vector<uint8_t> flow = BuildWaterFlow(&t, &sea);
		const int fPlain = flow[((size_t)row * W + 10) * 4 + 2], fGorge = flow[((size_t)row * W + 27) * 4 + 2];
		const float sPlain = t.waterDesign->designedSpeed(52, 101), sGorge = t.waterDesign->designedSpeed(137, 101);
		Terrain old;
		MakeTerrain(old, 64, 40, GorgeGround);
		old.lakes = t.lakes;
		old.waterDesign = std::make_shared<WaterDesign>();
		old.waterDesign->fromText(GorgeDesign(false), e);
		old.floodfillWater();
		const std::vector<uint8_t> curOld = BuildDesignedCurrent(&old);
		const bool oldNone = std::all_of(curOld.begin(), curOld.end(), [](uint8_t v) { return v == 0; });
		snprintf(b, sizeof(b), "rapids map g: plain %d, gorge %d, below the fall %d, dry land %d (m/s x 20); flow map speed plain %d, gorge %d; gameplay %.2f / %.2f m/s; without speeds all 0: %d",
			plain, gorge, below, land, fPlain, fGorge, sPlain, sGorge, (int)oldNone);
		check("gorge: the designed current drives the renderer and the gameplay", std::abs(plain - 12) <= 2 && std::abs(gorge - 70) <= 3 && std::abs(below - 16) <= 2
			&& land == 0 && fGorge > fPlain + 100 && std::abs(sPlain - 0.6f) < 0.05f && std::abs(sGorge - 3.5f) < 0.05f && oldNone, b);
	}

	// ---- 3. lakes: one that holds, one that leaks, a puddle; rivers that rise and run dry
	{
		// a basin round (120, 120): 10 m at its bottom, rising to 16 at 60 m and on to the edge
		auto basin = [](float x, float z) {
			const float r = std::hypot(x - 120.0f, z - 120.0f);
			return r < 60.0f ? 10.0f + r * 0.1f : 16.0f + (r - 60.0f) * 0.15f;
		};
		Terrain good;
		MakeTerrain(good, 48, 48, basin);
		good.lakes = { Vector3(120.0f, 18.0f, 120.0f) };
		good.waterDesign = std::make_shared<WaterDesign>(WaterDesign::blank(48, 48));
		for (int z = 0; z < 48; z++)
			for (int x = 0; x < 48; x++)
				if (std::hypot(x * 5 + 2.5f - 120.0f, z * 5 + 2.5f - 120.0f) < 82.0f)
					good.waterDesign->surface[(size_t)z * 48 + x] = WaterDesign::LAKE;
		good.floodfillWater();
		std::vector<std::string> wGood;
		const nlohmann::json rGood = MapWater::Check(good, -1e30f, wGood);
		snprintf(b, sizeof(b), "%s, %d tiles, %.2f m deep at most, leaks %s; warnings: %s", rGood["lakes"][0]["kind"].get<std::string>().c_str(),
			rGood["lakes"][0]["tiles"].get<int>(), rGood["lakes"][0]["maxDepthM"].get<float>(), rGood["lakes"][0]["leaks"].dump().c_str(), Join(wGood).c_str());
		check("a lake that doesn't leak", rGood["lakes"][0]["kind"] == "lake" && rGood["lakes"][0]["tiles"].get<int>() > 500 && rGood["lakes"][0]["leaks"].empty() && wGood.empty(), b);

		// the same with a notch cut west through its rim to the map's edge, and a pool in the notch
		// (the next valley); a river that climbs out of the lake east over dry land; river paint
		// on dry ground
		Terrain leak;
		MakeTerrain(leak, 48, 48, [&](float x, float z) {
			const float g = basin(x, z);
			return (x < 120.0f && std::abs(z - 120.0f) < 7.0f) ? std::min(g, 16.5f - (120.0f - x) * 0.02f) : g;
		});
		leak.lakes = { Vector3(120.0f, 18.0f, 120.0f), Vector3(22.0f, 15.5f, 120.0f) };
		leak.waterDesign = std::make_shared<WaterDesign>(*good.waterDesign);
		WaterDesign::Waterway up;
		for (float x = 185.0f; x <= 235.0f; x += 5.0f)
			up.points.emplace_back(x, 0.0f, 122.5f);
		up.width = 10;
		leak.waterDesign->waterways.push_back(up);
		for (int x = 4; x < 12; x++)
			leak.waterDesign->surface[(size_t)40 * 48 + x] = WaterDesign::RIVER;
		leak.floodfillWater();
		std::vector<std::string> wLeak;
		const nlohmann::json rLeak = MapWater::Check(leak, -1e30f, wLeak);
		const bool offMap = Has(wLeak, "lake 0", "leaks off the map"), drowned = Has(wLeak, "lake 1", "drowned by lake 0");
		const bool rises = Has(wLeak, "waterway 0", "rises"), dry = Has(wLeak, "waterway 0", "stays dry"), paint = Has(wLeak, "paints as river stay dry");
		snprintf(b, sizeof(b), "off the map %d, the next valley's pool drowned %d, the river rises %d (%s), runs dry %d, river paint dry %d; warnings: %s",
			(int)offMap, (int)drowned, (int)rises, rLeak["waterways"][0].value("rises", nlohmann::json()).dump().c_str(), (int)dry, (int)paint, Join(wLeak).c_str());
		check("a lake that leaks, rivers that rise and run dry: each reported with its place", offMap && drowned && rises && dry && paint
			&& rLeak["summary"]["leakingLakes"].get<int>() >= 1, b);

		// a puddle: flat ground at 10.3 m with a 0.6 m dip, the water at 10.2
		Terrain flat;
		MakeTerrain(flat, 32, 32, [](float x, float z) { return 10.3f - 0.6f * std::max(0.0f, 1.0f - std::hypot(x - 80.0f, z - 80.0f) / 50.0f); });
		flat.lakes = { Vector3(80.0f, 10.2f, 80.0f) };
		flat.floodfillWater();
		std::vector<std::string> wFlat;
		const nlohmann::json rFlat = MapWater::Check(flat, -1e30f, wFlat);
		snprintf(b, sizeof(b), "%d tiles, mean %.2f, max %.2f m; warnings: %s", rFlat["lakes"][0]["tiles"].get<int>(), rFlat["lakes"][0]["meanDepthM"].get<float>(),
			rFlat["lakes"][0]["maxDepthM"].get<float>(), Join(wFlat).c_str());
		check("a lake on flat ground: a puddle", rFlat["lakes"][0].value("puddle", false) && Has(wFlat, "puddle on flat ground"), b);
	}

	// ---- 4. a sea on two winds: the west half region A (wind east), the east half B (wind north,
	// waves twice as high); the sea reaching the map's edge is no leak
	{
		Terrain t;
		MakeTerrain(t, 64, 48, [](float x, float) { return x < 200.0f ? 4.0f : 12.0f + (x - 200.0f) * 0.1f; });
		t.lakes = { Vector3(40.0f, 8.0f, 120.0f) };
		auto d = std::make_shared<WaterDesign>(WaterDesign::blank(64, 48));
		d->seas = { { "West", 0.0f, 1.0f }, { "East", 90.0f, 2.0f } };
		for (int z = 0; z < 48; z++)
			for (int x = 0; x < 40; x++)
				d->surface[(size_t)z * 64 + x] = x < 20 ? 'A' : 'B';
		WaterDesign back;
		std::string e;
		const bool rt = back.fromText(d->toText(), e) && back.seas.size() == 2 && back.seas[1].windDegrees == 90.0f && back.seas[1].waves == 2.0f && back.at(25, 10) == 'B';
		t.waterDesign = d;
		t.floodfillWater();
		std::vector<std::string> warns;
		const nlohmann::json rep = MapWater::Check(t, -1e30f, warns);
		std::vector<uint8_t> sea;
		BuildWaterFlow(&t, &sea);
		const std::vector<uint8_t> regions = BuildWaveRegions(&t, sea);
		auto share = [&](int x, int z, int r) { return (int)regions[((size_t)z * 64 + x) * 8 + r]; };
		int seaTiles = 0;
		for (uint8_t s : sea)
			seaTiles += s != 0;
		snprintf(b, sizeof(b), "round trip %d; %s, %d tiles; open sea %d tiles; region shares west (5, 24) %d/%d, east (32, 24) %d/%d, where they meet (20, 24) %d/%d; warnings: %s",
			(int)rt, rep["lakes"][0]["kind"].get<std::string>().c_str(), rep["lakes"][0]["tiles"].get<int>(), seaTiles, share(5, 24, 0), share(5, 24, 1),
			share(32, 24, 0), share(32, 24, 1), share(20, 24, 0), share(20, 24, 1), Join(warns).c_str());
		check("a sea on two winds", rt && rep["lakes"][0]["kind"] == "sea" && warns.empty() && seaTiles > 500 && share(5, 24, 0) >= 250 && share(32, 24, 1) >= 250
			&& share(20, 24, 0) > 60 && share(20, 24, 1) > 60, b);
	}

	// ---- 5. Extend map follows the hints: 4 Peaks flattened (120 height steps) with a river
	// channel (16 steps deep) out through its east edge
	{
		LoadBCP("data.bcp");
		Terrain map;
		map.readBCM("Maps\\128_4Peaks\\128_4Peaks.bcm");
		const int w = map.width, h = map.height, zc = h / 2;
		for (int z = 0; z <= h; z++)
			for (int x = 0; x <= w; x++) {
				const bool channel = x >= w / 2 && (z == zc || z == zc + 1);
				map.vertices[(size_t)z * (w + 1) + x] = (uint8_t)(channel ? 104 : 120);
				if (!map.heightFraction.empty()) // (a BCM without a .wkbmap has none)
					map.heightFraction[(size_t)z * (w + 1) + x] = 0;
			}
		map.lakes = { Vector3((w * 3 / 4 + 0.5f) * 5.0f, 112.0f * map.scale, (zc + 0.5f) * 5.0f) };
		map.lakeExtraBits = { 0 };
		map.farLand.reset();
		auto run = [&](const std::vector<WaterDesign::FarHint>& hints) {
			for (int i = 0; i < w * h; i++) {
				map.tiles[i].fullOfWater = false;
				map.tiles[i].slopedWater = false;
				map.tiles[i].waterLevel = -1.0f;
			}
			map.waterDesign.reset();
			if (!hints.empty()) {
				map.waterDesign = std::make_shared<WaterDesign>(WaterDesign::blank(w, h));
				map.waterDesign->farland = hints;
			}
			map.floodfillWater();
			return BuildExtendedTerrain(map, 60, nullptr, 0, nullptr, 1, false);
		};
		const int n = 60;
		struct Bands { double eastWater = 0, westWater = 0, northMean = 0, southMean = 0; int riverSouth = 0, riverNorth = 0; };
		auto measure = [&](const Terrain& e) {
			Bands B;
			int east = 0, west = 0, north = 0, south = 0;
			for (int Z = n; Z < n + h; Z++)
				for (int X = 0; X < (int)e.width; X++) {
					const bool wet = e.getTile(X, Z)->fullOfWater;
					if (X >= n + w + 20) { east++; B.eastWater += wet; }
					if (X < n - 20) { west++; B.westWater += wet; }
				}
			for (int X = n; X <= n + w; X++)
				for (int Z = 0; Z <= (int)e.height; Z++) {
					if (Z >= n + h + 20) { north++; B.northMean += e.getVertex(X, Z); }
					if (Z <= n - 20) { south++; B.southMean += e.getVertex(X, Z); }
				}
			B.eastWater /= std::max(east, 1);
			B.westWater /= std::max(west, 1);
			B.northMean /= std::max(north, 1);
			B.southMean /= std::max(south, 1);
			// the river far beyond the map's east edge (30 tiles and more): its water south and north of the exit's row
			for (int Z = 0; Z < (int)e.height; Z++)
				for (int X = n + w + 30; X < (int)e.width; X++)
					if (e.getTile(X, Z)->fullOfWater) {
						if (Z < n + zc - 3) B.riverSouth++;
						if (Z > n + zc + 3) B.riverNorth++;
					}
			return B;
		};
		auto hint = [](WaterDesign::FarHint::Kind k, float deg, float spread, float from) {
			WaterDesign::FarHint hh;
			hh.kind = k;
			hh.degrees = deg;
			hh.spread = spread;
			hh.from = from;
			return hh;
		};
		const Bands none = measure(*run({}));
		const Bands hinted = measure(*run({ hint(WaterDesign::FarHint::SEA, 0, 100, 15), hint(WaterDesign::FarHint::MOUNTAINS, 90, 100, 15) }));
		snprintf(b, sizeof(b), "water east of the map %.0f%% (no hints %.0f%%), west %.0f%% (%.0f%%); north %.1f m (no hints %.1f), south %.1f m (%.1f)",
			100 * hinted.eastWater, 100 * none.eastWater, 100 * hinted.westWater, 100 * none.westWater, hinted.northMean, none.northMean, hinted.southMean, none.southMean);
		check("Extend map: the sea to the east and the range to the north as hinted, the rest as without", hinted.eastWater > 0.3 && none.eastWater < 0.1
			&& std::abs(hinted.westWater - none.westWater) < 0.03 && hinted.northMean > none.northMean + 15 && std::abs(hinted.southMean - none.southMean) < 5, b);
		WaterDesign::FarHint se = hint(WaterDesign::FarHint::RIVER, 315, 90, 0), ne = hint(WaterDesign::FarHint::RIVER, 45, 90, 0);
		se.hasAt = ne.hasAt = true;
		se.at = ne.at = Vector3((w - map.edge) * 5.0f, 0, (zc + 0.5f - map.edge) * 5.0f);
		const Bands toSE = measure(*run({ se })), toNE = measure(*run({ ne }));
		snprintf(b, sizeof(b), "river tiles 30+ tiles beyond the east edge south/north of its exit: no hint %d/%d, hinted south-east %d/%d, north-east %d/%d",
			none.riverSouth, none.riverNorth, toSE.riverSouth, toSE.riverNorth, toNE.riverSouth, toNE.riverNorth);
		check("Extend map: the river leaves the way hinted", toSE.riverSouth > 3 * std::max(toSE.riverNorth, 5) && toNE.riverNorth > 3 * std::max(toNE.riverSouth, 5), b);
	}

	// ---- 6. a map folder compiled with it: the plan's river with water and a speed, a far-land
	// hint; report.json's water checks, map.water version 2
	{
		const fs::path folder = g_userPath / "saved" / "Maps" / "Water Intent Test.wkbai";
		std::error_code ec;
		fs::remove_all(folder, ec);
		fs::create_directories(folder, ec);
		Terrain ref;
		ref.readBCM("Maps\\128_4Peaks\\128_4Peaks.bcm");
		std::string db, sky;
		for (wchar_t c : ref.texDbPath) if (c) db += (char)c;
		for (wchar_t c : ref.skyTextureDirectory) if (c) sky += (char)c;
		const int playable = 128, edge = 30, w = playable + 2 * edge;
		oj mj;
		mj["format"] = "wkbai map";
		mj["version"] = 1;
		mj["name"] = "Water Intent Test";
		mj["size"] = { { "tiles", { w, w } }, { "edge", edge } };
		mj["heights"] = { { "scale", 1.0 } };
		mj["textures"] = { { "database", db }, { "translation", "Maps\\Map_Textures\\128_4Peaks.lttt" } };
		mj["sky"] = sky;
		mj["seed"] = 61;
		mj["players"] = oj::array({ { { "name", "West" }, { "colour", 1 }, { "start", { 120, 320 } }, { "base", "Manor" }, { "peasants", 2 } },
			{ { "name", "East" }, { "colour", 2 }, { "start", { 520, 320 } }, { "base", "Manor" }, { "peasants", 2 } } });
		mj["terrain"] = { { "base", 30 }, { "noise", { { "amplitude", 3 }, { "scale", 300 } } } };
		mj["features"] = oj::array({
			{ { "kind", "valley" }, { "id", "dale" }, { "line", { { 320, 660 }, { 330, 320 }, { 320, -20 } } }, { "width", 240 }, { "depth", 6 }, { "floorHeight", { 40, 14 } } },
			{ { "kind", "river" }, { "id", "beck" }, { "line", { { 320, 630 }, { 330, 320 }, { 320, 10 } } }, { "width", 12 }, { "depth", 2.0 }, { "water", true }, { "speed", 2.2 } } });
		std::vector<uint16_t> flat((size_t)(w + 1) * (w + 1), 20 * 256);
		MapFolder::WritePng16(folder / "heights.png", w + 1, w + 1, flat);
		std::ofstream(folder / "map.json", std::ios::binary) << mj.dump(1) << "\n";
		oj st;
		std::string err;
		const bool gen = MapTerrain::Generate(folder, st, err);
		// the hint, added to the water.json the generator wrote
		oj wj = oj::parse(Text(folder / "water.json"), nullptr, false);
		float planSpeed = 0;
		for (const oj& way : wj.value("waterways", oj::array()))
			if (way.value("by", "") == "terrain:beck")
				planSpeed = way.value("speed", 0.0f);
		wj["farland"] = oj::array({ { { "kind", "river" }, { "toward", "south-east" }, { "at", { 320, 0 } } } });
		if (!wj.contains("tiles"))
			wj["tiles"] = { w, w };
		std::ofstream(folder / "water.json", std::ios::binary) << wj.dump(1) << "\n";
		const MapFolder::Result r = gen ? MapFolder::Compile(folder, true) : MapFolder::Result{};
		const oj rep = r.ok ? oj::parse(Text(folder / "build" / "report.json"), nullptr, false) : oj();
		const oj water = rep.is_object() ? rep.value("water", oj()) : oj();
		WaterDesign built;
		std::string e;
		const std::string mw = Text(folder / "build" / "map.water");
		const bool read = !mw.empty() && built.fromText(mw, e);
		const oj mwj = oj::parse(mw, nullptr, false);
		float waySpeed = 0;
		for (const auto& way : water.value("waterways", oj::array()))
			if (way.value("by", "") == "terrain:beck" && way["speedMs"].is_object())
				waySpeed = way["speedMs"].value("from", 0.0f);
		snprintf(b, sizeof(b), "generated %d (%s), compiled %d (%s); plan speed %.1f in water.json, %.1f in the report; report water %s; map.water v%d, %zu hints; summary %s",
			(int)gen, err.c_str(), (int)r.ok, r.error.c_str(), planSpeed, waySpeed, water.is_object() ? "yes" : "no",
			mwj.is_object() ? mwj.value("version", 0) : 0, read ? built.farland.size() : 0, water.is_object() ? water.value("summary", oj()).dump().c_str() : "-");
		check("compiled: report.json's water checks, the plan's speed, map.water version 2", gen && r.ok && water.is_object() && std::abs(planSpeed - 2.2f) < 0.01f
			&& std::abs(waySpeed - 2.2f) < 0.01f && read && built.farland.size() == 1 && mwj.value("version", 0) == 2
			&& water["summary"].value("waterways", 0) >= 1, b);
	}
	printf("WATERINTENT all %d\n", (int)all);
	fflush(stdout);
}
