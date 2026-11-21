// wkbai - the headless test of the sloped (tilted) rivers (Terrain::applySlopedWater,
// WaterDesign::Waterway::levels): a stream down a made-up hillside into a lake (the water design's
// format old and new, the surface never rising downstream, the lake met at its level without a
// seam, floating along it without steps, its rapids and its fall found, a map without sloped water
// as before), then a generated map folder whose river has water ("water": true), compiled, loaded
// as the server and as a client loads it (the same water), and played. Test "Tilted water" in test.cpp.
// Licensed under the GNU General Public License 3

#include "terrain.h"
#include "WaterDesign.h"
#include "MapTerrain.h"
#include "MapFolder.h"
#include "server.h"
#include "file.h"
#include "settings.h"
#include "gfx/DiligentWaterfalls.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {
	// the made-up hillside: 40 x 40 tiles, no edge; a stream along z = 100 from x = 10 to 165 into
	// a lake from x = 150: gently (1 in 20), rapids (1 in 5), an 8 m step, gently again (1 in 33)
	constexpr int SIDE = 40;
	float Bed(float x)
	{
		float h = 70.0f;
		h -= std::clamp(x, 0.0f, 60.0f) * 0.05f;
		h -= std::clamp(x - 60.0f, 0.0f, 40.0f) * 0.2f;
		h -= std::clamp(x - 100.0f, 0.0f, 5.0f) * 1.6f;
		h -= std::clamp(x - 105.0f, 0.0f, 45.0f) * 0.03f;
		return h;
	}
	constexpr float LAKE_BOTTOM = 46.0f;
	float LakeLevel() { return Bed(150.0f) + 0.3f; }
	float Ground(float x, float z)
	{
		const float d = std::abs(z - 100.0f);
		float g = Bed(x) + (d < 6.0f ? 0.0f : 3.0f + (d - 6.0f) * 0.1f);
		if (x >= 150.0f && d < 45.0f)
			g = std::min(g, LAKE_BOTTOM + (x < 158.0f ? (158.0f - x) * 0.6f : 0.0f));
		return g;
	}
	void MakeHillside(Terrain& t)
	{
		t.createEmpty(SIDE, SIDE);
		t.scale = 0.5f;
		t.vertices.resize((SIDE + 1) * (SIDE + 1));
		t.heightFraction.assign((size_t)(SIDE + 1) * (SIDE + 1), 0);
		t.tiles = new Terrain::Tile[SIDE * SIDE];
		for (int z = 0; z <= SIDE; z++)
			for (int x = 0; x <= SIDE; x++) {
				const float steps = Ground(x * 5.0f, z * 5.0f) / t.scale;
				const int i = z * (SIDE + 1) + x;
				t.vertices[i] = (uint8_t)std::floor(steps);
				t.heightFraction[i] = (uint8_t)std::clamp((int)std::lround((steps - std::floor(steps)) * 256.0f), 0, 255);
			}
		for (int z = 0; z < SIDE; z++)
			for (int x = 0; x < SIDE; x++) {
				t.tiles[z * SIDE + x].x = x;
				t.tiles[z * SIDE + x].z = z;
				t.tiles[z * SIDE + x].texture = nullptr;
			}
		t.lakes = { Vector3(185.0f, LakeLevel(), 100.0f) };
	}
	// the stream's design: a point every 5 m, the water 0.4 m deep, 0.2 m on the steep stretches
	std::string HillsideDesign(bool sloped)
	{
		nlohmann::ordered_json pts = nlohmann::ordered_json::array();
		for (float x = 10.0f; x <= 165.0f; x += 5.0f) {
			const float steep = std::abs(Bed(x + 2.5f) - Bed(x - 2.5f)) / 5.0f;
			const float level = Bed(x) + (steep > 0.15f ? 0.2f : 0.4f);
			if (sloped)
				pts.push_back({ x, 100.0f, level });
			else
				pts.push_back({ x, 100.0f });
		}
		nlohmann::ordered_json j = { { "format", "wkbai water" }, { "version", 1 }, { "tiles", { SIDE, SIDE } },
			{ "waterways", nlohmann::ordered_json::array({ { { "points", pts }, { "width", 12 } } }) } };
		return j.dump();
	}
	void StepServer(Server& server, int steps)
	{
		server.timeManager.stepMs = 20;
		for (int i = 0; i < steps; i++) {
			server.timeManager.pendingMs = server.timeManager.stepMs;
			if (server.timeManager.step())
				server.simulate();
		}
	}
}

void Test_TiltedWater()
{
	bool all = true;
	auto check = [&](const char* what, bool ok, const std::string& detail) {
		printf("TILTED %s %d %s\n", what, (int)ok, detail.c_str());
		fflush(stdout);
		all = all && ok;
	};
	char b[512];

	// 1. the format: levels on every point make a sloped river; without them (an old file) a
	//    river of the level water; written again the same; a level that would rise is held
	{
		WaterDesign oldD, newD, again;
		std::string e1, e2, e3;
		const bool r1 = oldD.fromText(HillsideDesign(false), e1), r2 = newD.fromText(HillsideDesign(true), e2);
		const bool r3 = r2 && again.fromText(newD.toText(), e3);
		bool same = r3 && again.waterways.size() == 1 && again.waterways[0].levels.size() == newD.waterways[0].levels.size();
		for (size_t i = 0; same && i < newD.waterways[0].levels.size(); i++)
			same = std::abs(again.waterways[0].levels[i] - newD.waterways[0].levels[i]) < 0.002f;
		WaterDesign rising;
		std::string e4;
		rising.fromText(R"({"format":"wkbai water","tiles":[40,40],"waterways":[{"points":[[0,0,10],[10,0,12],[20,0,8]],"width":10}]})", e4);
		const bool held = rising.waterways.size() == 1 && rising.waterways[0].sloped() && rising.waterways[0].levels[1] == 10.0f;
		snprintf(b, sizeof(b), "old file %d sloped %d, new %d sloped %d, round trip %d, rising level held %d", (int)r1,
			r1 && oldD.waterways.size() == 1 && oldD.waterways[0].sloped(), (int)r2, r2 && newD.hasSlopedWater(), (int)same, (int)held);
		check("format", r1 && !oldD.hasSlopedWater() && r2 && newD.hasSlopedWater() && same && held, b);
	}

	// 2. the hillside with its stream
	Terrain level, sloped;
	MakeHillside(level);
	level.waterDesign = std::make_shared<WaterDesign>();
	{
		std::string e;
		level.waterDesign->fromText(HillsideDesign(false), e);
	}
	level.floodfillWater();
	MakeHillside(sloped);
	sloped.waterDesign = std::make_shared<WaterDesign>();
	{
		std::string e;
		sloped.waterDesign->fromText(HillsideDesign(true), e);
	}
	sloped.floodfillWater();
	const int zRow = 20; // the tile row the stream runs along (z 100-105)
	{
		int levelSloped = 0, slopedTiles = 0, rise = 0, lakeTiles = 0;
		float prev = 1e9f, maxDepth = 0, minDepth = 1e9f;
		for (int i = 0; i < SIDE * SIDE; i++) {
			levelSloped += level.tiles[i].slopedWater;
			slopedTiles += sloped.tiles[i].slopedWater;
		}
		std::string profile;
		for (int x = 2; x < SIDE; x++) {
			const Terrain::Tile* t = sloped.getTile(x, zRow);
			if (!t->fullOfWater)
				continue;
			if (!t->slopedWater) {
				lakeTiles++;
				continue;
			}
			rise += t->waterLevel > prev + 0.01f;
			prev = t->waterLevel;
			const float bed = Bed(x * 5.0f + 2.5f);
			maxDepth = std::max(maxDepth, t->waterLevel - bed);
			minDepth = std::min(minDepth, t->waterLevel - bed);
			if (x % 3 == 0)
				profile += " " + std::to_string((int)std::lround(t->waterLevel * 10) / 10.0f).substr(0, 4);
		}
		// the stream's tiles along x: every one water (from x 10 m to the lake)
		int dryOnCourse = 0;
		for (int x = 3; x < 30; x++)
			dryOnCourse += !sloped.getTile(x, zRow)->fullOfWater;
		snprintf(b, sizeof(b), "%d sloped tiles (level design: %d), %d dry on the course, %d rising steps, depth %.2f-%.2f m over the bed, lake tiles on the row %d; levels%s",
			slopedTiles, levelSloped, dryOnCourse, rise, minDepth, maxDepth, lakeTiles, profile.c_str());
		check("surface follows the bed", levelSloped == 0 && slopedTiles >= 50 && dryOnCourse == 0 && rise == 0 && minDepth > 0.0f && maxDepth < 2.0f && lakeTiles > 0, b);
	}
	// the level design as before: the stream only where the lake floods (no water up the hill)
	{
		int wetUphill = 0;
		for (int x = 0; x < 26; x++)
			wetUphill += level.getTile(x, zRow)->fullOfWater;
		snprintf(b, sizeof(b), "%d tiles wet up the hill with the old (level) waterway; sloped surface %s", wetUphill, level.slopedSurface.empty() ? "none" : "made");
		check("level design unchanged", wetUphill == 0 && level.slopedSurface.empty(), b);
	}
	// the mouth: the river meets the lake at its level, the corners where they touch the same
	{
		float worst = 0;
		int shared = 0;
		for (int z = 0; z < SIDE; z++)
			for (int x = 0; x + 1 < SIDE; x++) {
				const Terrain::Tile* a = sloped.getTile(x, z);
				const Terrain::Tile* c = sloped.getTile(x + 1, z);
				if (!a->fullOfWater || !c->fullOfWater || a->slopedWater == c->slopedWater)
					continue;
				// their shared corners, from either side
				for (int k = 0; k <= 1; k++) {
					const float fromA = TileWaterCorner(&sloped, x, z, x + 1, z + k), fromC = TileWaterCorner(&sloped, x + 1, z, x + 1, z + k);
					worst = std::max(worst, std::abs(fromA - fromC));
					shared++;
				}
			}
		const Terrain::Tile* lake = sloped.getTile(36, zRow);
		snprintf(b, sizeof(b), "lake level %.2f (tile %.2f), %d corners where the river meets it, the worst seam %.3f m", LakeLevel(), lake->waterLevel, shared, worst);
		check("into the lake at its level", shared >= 2 && worst < 0.05f && std::abs(lake->waterLevel - LakeLevel()) < 0.01f, b);
	}
	// floating down it: the surface under a boat never jumps (at most its own slope per step)
	{
		float worstJump = 0, prevH = 0;
		bool first = true, below = false;
		for (float x = 12.0f; x < 160.0f; x += 0.5f) {
			const float h = sloped.getHeightEx(x, 102.5f, true);
			if (!first)
				worstJump = std::max(worstJump, std::abs(h - prevH) - std::abs(Bed(x) - Bed(x - 0.5f)));
			below = below || h < sloped.getHeight(x, 102.5f) - 0.01f;
			prevH = h;
			first = false;
		}
		snprintf(b, sizeof(b), "worst jump beyond the bed's own fall %.3f m per half unit, below the ground %d", worstJump, (int)below);
		check("floating without steps", worstJump < 0.25f && !below, b);
	}
	// rapids where it is steep, none where it is gentle; the fall at the step; no level water's
	// falls between the sloped tiles
	{
		float gentle = 0, steep = 0;
		for (int x = 3; x < 11; x++) gentle = std::max(gentle, SlopedRapids(&sloped, x, zRow));
		for (int x = 13; x < 19; x++) steep = std::max(steep, SlopedRapids(&sloped, x, zRow));
		const std::vector<Waterfall> falls = FindWaterfalls(&sloped);
		int atStep = 0, elsewhere = 0;
		std::string where;
		for (const Waterfall& w : falls) {
			const bool here = w.x0 >= 90.0f && w.x0 <= 110.0f && w.dx == 1;
			(here ? atStep : elsewhere)++;
			char one[96];
			snprintf(one, sizeof(one), " (%.0f, %.0f) %+d %+d %.1f->%.1f", w.x0, w.z0, w.dx, w.dz, w.top.empty() ? 0.0f : w.top[0], w.bottom);
			where += one;
		}
		snprintf(b, sizeof(b), "rapids gentle %.2f steep %.2f; %zu falls, %d at the step, %d elsewhere:%s", gentle, steep, falls.size(), atStep, elsewhere, where.c_str());
		check("rapids and the fall", gentle < 0.1f && steep > 0.5f && atStep >= 1 && elsewhere == 0, b);
	}
	// a lake held at its outlet: a tarn (level 28, its floor at 20, its rim at 30) whose outlet's
	// channel is cut through the rim at the floor's depth, down a slope falling to 10: as level
	// water the tarn runs out down it and floods the low land; with the sloped river along the
	// channel (from out on the tarn, at its level there, as the generator writes it) it stays in
	// its basin, without a dry hole round the outlet, and pours down into the trench
	{
		auto ground = [](float x, float z) {
			const float r = std::hypot(x - 80.0f, z - 100.0f);
			float g = r < 40.0f ? 20.0f + std::max(0.0f, r - 30.0f) * 1.0f : 30.0f - std::max(0.0f, x - 120.0f) * 0.25f;
			g = std::max(g, 8.0f);
			if (std::abs(z - 100.0f) < 6.0f && x > 80.0f)
				g = std::min(g, std::min(20.0f, 30.0f - std::max(0.0f, x - 120.0f) * 0.25f - 1.0f));
			return g;
		};
		auto make = [&](Terrain& t, bool withRiver) {
			t.createEmpty(SIDE, SIDE);
			t.scale = 0.5f;
			t.vertices.resize((SIDE + 1) * (SIDE + 1));
			t.heightFraction.assign((size_t)(SIDE + 1) * (SIDE + 1), 0);
			t.tiles = new Terrain::Tile[SIDE * SIDE];
			for (int z = 0; z <= SIDE; z++)
				for (int x = 0; x <= SIDE; x++) {
					const float steps = ground(x * 5.0f, z * 5.0f) / t.scale;
					t.vertices[z * (SIDE + 1) + x] = (uint8_t)std::floor(steps);
					t.heightFraction[z * (SIDE + 1) + x] = (uint8_t)std::clamp((int)std::lround((steps - std::floor(steps)) * 256.0f), 0, 255);
				}
			t.lakes = { Vector3(62.5f, 28.0f, 102.5f) };
			nlohmann::ordered_json pts = nlohmann::ordered_json::array();
			for (float x = 100.0f; x <= 195.0f; x += 5.0f)
				pts.push_back({ x, 100.0f, x <= 115.0f ? 28.0f : ground(x, 100.0f) + 0.4f }); // at the tarn's level out on it, then over the trench
			t.waterDesign = std::make_shared<WaterDesign>();
			std::string e;
			t.waterDesign->fromText(nlohmann::ordered_json({ { "format", "wkbai water" }, { "tiles", { SIDE, SIDE } },
				{ "waterways", nlohmann::ordered_json::array({ { { "points", pts }, { "width", 10 } } }) } }).dump(), e);
			if (!withRiver)
				t.waterDesign->waterways[0].levels.clear();
			t.floodfillWater();
		};
		Terrain leaky, held;
		make(leaky, false);
		make(held, true);
		auto lakeBeyondRim = [](const Terrain& t) {
			int n = 0;
			for (int z = 0; z < SIDE; z++)
				for (int x = 26; x < SIDE; x++) // beyond the rim (x > 130 m)
					n += t.getTile(x, z)->fullOfWater && !t.getTile(x, z)->slopedWater;
			return n;
		};
		int riverTiles = 0, rising = 0;
		float prev = 1e9f;
		for (int x = 20; x < SIDE; x++) {
			const Terrain::Tile* t = held.getTile(x, 20);
			if (!t->slopedWater)
				continue;
			riverTiles++;
			rising += t->waterLevel > prev + 0.01f;
			prev = t->waterLevel;
		}
		const float first = held.getTile(24, 20)->waterLevel;
		// the tarn's floor round the outlet's inner end (r < 30 m) all water
		int dryInTarn = 0;
		for (int z = 0; z < SIDE; z++)
			for (int x = 0; x < SIDE; x++)
				dryInTarn += std::hypot(x * 5.0f + 2.5f - 80.0f, z * 5.0f + 2.5f - 100.0f) < 28.0f && !held.getTile(x, z)->fullOfWater;
		// (the old data's trench below the rim: the tarn pours into it, a fall at the outlet)
		int outletFalls = 0;
		for (const Waterfall& w : FindWaterfalls(&held))
			outletFalls += w.dx == 1 && w.x0 >= 105.0f && w.x0 <= 130.0f && !w.top.empty() && std::abs(w.top[0] - 28.0f) < 0.6f;
		snprintf(b, sizeof(b), "lake tiles beyond the rim: level water %d, with the sloped river %d; %d river tiles, %d rising, below the rim %.2f (lake 28), %d falls at the outlet, %d dry tiles in the tarn",
			lakeBeyondRim(leaky), lakeBeyondRim(held), riverTiles, rising, first, outletFalls, dryInTarn);
		check("a lake held at its outlet", lakeBeyondRim(leaky) > 50 && lakeBeyondRim(held) == 0 && riverTiles >= 10 && rising == 0 && first <= 28.01f && outletFalls >= 1 && dryInTarn == 0, b);
	}
	// wading: the stream is shallow water everywhere (the units wade or swim as in a river)
	{
		int deep = 0;
		for (int x = 2; x < 30; x++) {
			deep += sloped.waterDepthAt(x, zRow) > 1.5f;
		}
		snprintf(b, sizeof(b), "%d stream tiles deeper than 1.5 m (where the units that can't wade are blocked)", deep);
		check("a stream to wade", deep == 0, b);
	}

	// 3. a generated map: a hillside river with water into a basin's lake, compiled, the server's
	//    and a client's terrain the same water, played
	LoadBCP("data.bcp");
	{
		using oj = nlohmann::ordered_json;
		const std::filesystem::path folder = g_userPath / "saved" / "Maps" / "Tilted Water.wkbai";
		std::error_code ec;
		std::filesystem::remove_all(folder, ec);
		std::filesystem::create_directories(folder, ec);
		Terrain ref;
		ref.readBCM("Maps\\128_4Peaks\\128_4Peaks.bcm");
		const int edge = 30, playable = 192, w = playable + 2 * edge;
		std::string db, sky;
		for (wchar_t c : ref.texDbPath) if (c) db += (char)c;
		for (wchar_t c : ref.skyTextureDirectory) if (c) sky += (char)c;
		oj mj;
		mj["format"] = "wkbai map";
		mj["version"] = 1;
		mj["name"] = "Tilted Water";
		mj["size"] = { { "tiles", { w, w } }, { "edge", edge } };
		mj["heights"] = { { "scale", 1.0 } };
		mj["textures"] = { { "database", db }, { "translation", "Maps\\Map_Textures\\128_4Peaks.lttt" } };
		mj["sky"] = sky;
		mj["sun"] = { { "colour", { (ref.sunColor >> 16) & 255, (ref.sunColor >> 8) & 255, ref.sunColor & 255 } },
			{ "direction", { ref.sunVector.x, ref.sunVector.y, ref.sunVector.z } } };
		mj["fog"] = { (ref.fogColor >> 16) & 255, (ref.fogColor >> 8) & 255, ref.fogColor & 255 };
		mj["seed"] = 2611;
		mj["ground"] = { { "fill", "GRASS1" } };
		mj["players"] = oj::array({ { { "name", "West" }, { "colour", 1 }, { "start", { 150, 700 } }, { "base", "Manor" }, { "peasants", 4 } },
			{ { "name", "East" }, { "colour", 2 }, { "start", { 760, 230 } }, { "base", "Manor" }, { "peasants", 4 } } });
		mj["terrain"] = { { "base", 20 }, { "noise", { { "amplitude", 3 }, { "scale", 300 } } } };
		mj["features"] = oj::array({
			{ { "kind", "hill" }, { "id", "fell" }, { "at", { 230, 230 } }, { "radius", 300 }, { "height", 80 } },
			{ { "kind", "basin" }, { "id", "mere" }, { "at", { 720, 720 } }, { "radius", 140 }, { "depth", 9 }, { "water", 18.5 } },
			{ { "kind", "river" }, { "id", "beck" }, { "line", { { 250, 250 }, { 420, 470 }, { 560, 560 }, { 700, 700 } } }, { "width", 8 }, { "depth", 2 },
				{ "meander", 0.4 }, { "water", true } } });
		std::vector<uint16_t> flat((size_t)(w + 1) * (w + 1), 20 * 256);
		MapFolder::WritePng16(folder / "heights.png", w + 1, w + 1, flat);
		std::ofstream(folder / "map.json", std::ios::binary) << mj.dump(1) << "\n";
		MapTerrain::json st;
		std::string error;
		const bool gen = MapTerrain::Generate(folder, st, error);
		// the waterway written: levels on its points, never rising
		WaterDesign d;
		std::string text, e;
		{
			std::ifstream f(folder / "water.json", std::ios::binary);
			text.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
		}
		const bool read = d.fromText(text, e);
		size_t points = 0;
		float from = 0, to = 0;
		bool monotone = true, by = false;
		for (const auto& way : d.waterways)
			if (way.sloped()) {
				points += way.points.size();
				from = way.levels.front();
				to = way.levels.back();
				by = way.by == "terrain:beck";
				for (size_t i = 1; i < way.levels.size(); i++)
					monotone = monotone && way.levels[i] <= way.levels[i - 1];
			}
		snprintf(b, sizeof(b), "generated %d (%s), design read %d, %zu sloped points, levels %.1f -> %.1f m, never rising %d, by the generator %d", (int)gen, error.c_str(),
			(int)read, points, from, to, (int)monotone, (int)by);
		check("generator writes a sloped river", gen && read && points > 20 && from > to + 10 && monotone && by, b);
		// generated again: its own waterway replaced, not added
		MapTerrain::json st2;
		MapTerrain::Generate(folder, st2, error);
		WaterDesign d2;
		{
			std::ifstream f(folder / "water.json", std::ios::binary);
			text.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
		}
		d2.fromText(text, e);
		snprintf(b, sizeof(b), "%zu waterways after generating twice", d2.waterways.size());
		check("generated again: replaced", d2.waterways.size() == 1, b);

		const MapFolder::Result r = MapFolder::Compile(folder, true);
		Server server;
		server.loadSaveGame(MapFolder::GamePathOf(folder).c_str());
		int slopedTiles = 0, mismatch = 0;
		Terrain client;
		client.readFromFile(MapFolder::GamePathOf(folder).c_str());
		if (server.terrain && server.terrain->width == client.width && server.terrain->height == client.height) {
			for (int i = 0; i < client.width * client.height; i++) {
				const auto& s = server.terrain->tiles[i];
				const auto& c = client.tiles[i];
				slopedTiles += s.slopedWater;
				mismatch += s.fullOfWater != c.fullOfWater || s.slopedWater != c.slopedWater || (s.fullOfWater && std::abs(s.waterLevel - c.waterLevel) > 1e-4f);
			}
		}
		else
			mismatch = -1;
		// the mere floods (as the generator's lake map has it) and the beck runs into it at its level
		{
			int mere = 0;
			for (int i = 0; server.terrain && i < server.terrain->width * server.terrain->height; i++)
				mere += server.terrain->tiles[i].fullOfWater && !server.terrain->tiles[i].slopedWater && std::abs(server.terrain->tiles[i].waterLevel - 18.5f) < 0.01f;
			const float mouth = d2.waterways.empty() || d2.waterways[0].levels.empty() ? 0.0f : d2.waterways[0].levels.back();
			snprintf(b, sizeof(b), "%d tiles of the mere (18.5 m), the beck's last level %.2f", mere, mouth);
			check("generated river into the mere", mere > 200 && std::abs(mouth - 18.5f) < 0.05f, b);
		}
		snprintf(b, sizeof(b), "compiled %d, %d sloped tiles on the server, %d tiles whose water differs from the client's", (int)r.ok, slopedTiles, mismatch);
		check("server and client the same water", r.ok && slopedTiles > 30 && mismatch == 0, b);
		server.startLevel();
		server.timeManager.unpause();
		StepServer(server, 1500);
		snprintf(b, sizeof(b), "played to t=%.1f s", server.timeManager.currentTime);
		check("played", server.timeManager.currentTime > 25.0f, b);
	}
	printf("TILTED all %d\n", (int)all);
	fflush(stdout);
}
