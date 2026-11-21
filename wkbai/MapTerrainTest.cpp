// wkbai - step 17.4's headless test (MapTerrain.h): three maps from three feature plans, one from a
// PNG heightmap, one from elevation files (a made-up SRTM .hgt and GeoTIFFs written here); the
// stats, determinism, the hand layer, compiled, loaded and played. Test "Map terrain" in test.cpp.
// Licensed under the GNU General Public License 3

#include "MapTerrain.h"
#include "MapFolder.h"
#include "server.h"
#include "terrain.h"
#include "file.h"
#include "settings.h"
#include "gameset/gameset.h"
#include "tags.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>



namespace {
	void StepServer(Server& server, int steps)
	{
		server.timeManager.stepMs = 20;
		for (int i = 0; i < steps; i++) {
			server.timeManager.pendingMs = server.timeManager.stepMs; // a step without the real clock
			if (server.timeManager.step())
				server.simulate();
		}
	}
}

// ---- Step 17.4: the land from a feature plan, a heightmap picture and elevation files ----

namespace {
	// a map folder for the terrain test: 4 Peaks' textures and sky, one material, the plan
	std::filesystem::path TerrainTestMap(const std::string& name, int playable, const nlohmann::ordered_json& terrain,
		const nlohmann::ordered_json& features, const nlohmann::ordered_json& players)
	{
		const std::filesystem::path folder = g_userPath / "saved" / "Maps" / PathFromUtf8(name + ".wkbai");
		std::error_code ec;
		std::filesystem::remove_all(folder, ec);
		std::filesystem::create_directories(folder, ec);
		Terrain ref;
		ref.readBCM("Maps\\128_4Peaks\\128_4Peaks.bcm");
		const int edge = 30, w = playable + 2 * edge;
		std::string db, sky;
		for (wchar_t c : ref.texDbPath) if (c) db += (char)c;
		for (wchar_t c : ref.skyTextureDirectory) if (c) sky += (char)c;
		nlohmann::ordered_json mj;
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
		mj["seed"] = 1704;
		mj["ground"] = { { "fill", "GRASS1" } };
		mj["players"] = players;
		mj["terrain"] = terrain;
		mj["features"] = features;
		std::vector<uint16_t> flat((size_t)(w + 1) * (w + 1), 20 * 256);
		MapFolder::WritePng16(folder / "heights.png", w + 1, w + 1, flat);
		std::ofstream(folder / "map.json", std::ios::binary) << mj.dump(1) << "\n";
		return folder;
	}
	nlohmann::ordered_json TerrainPlayers(float x1, float z1, float x2, float z2)
	{
		return nlohmann::ordered_json::array({
			{ { "name", "West" }, { "colour", 1 }, { "start", { x1, z1 } }, { "base", "Manor" }, { "peasants", 4 } },
			{ { "name", "East" }, { "colour", 2 }, { "start", { x2, z2 } }, { "base", "Manor" }, { "peasants", 4 } } });
	}
	std::string FileBytes(const std::filesystem::path& p)
	{
		std::ifstream f(p, std::ios::binary);
		return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
	}
	// a made-up elevation: a mountain massif with a valley, the sea in the south-west (metres, at a latitude and longitude)
	float FakeElevation(double lat, double lon)
	{
		const double x = (lon - 10.0) * 77.0, y = (lat - 46.0) * 111.0; // km east and north of 46 N 10 E
		double h = 2400 * std::exp(-((x - 21) * (x - 21) + (y - 25) * (y - 25)) / 40.0) + 1500 * std::exp(-((x - 30) * (x - 30) + (y - 18) * (y - 18)) / 25.0);
		h += 300 + 180 * std::sin(x * 0.9) * std::cos(y * 0.7);
		h -= 700 * std::exp(-((x - 24) * 0.6 - (y - 20)) * ((x - 24) * 0.6 - (y - 20)) / 3.0);
		if (x + y < 32) h -= (32 - x - y) * 220;
		return (float)std::max(0.0, h);
	}
	// TIFF's LZW for the test's GeoTIFF (MSB-first, early change), a table reset at 4093
	std::vector<uint8_t> LzwEncode(const std::vector<uint8_t>& in)
	{
		std::vector<uint8_t> out;
		uint32_t acc = 0;
		int nbits = 0, width = 9, next = 258;
		auto put = [&](int code) {
			acc = (acc << width) | (uint32_t)code;
			nbits += width;
			while (nbits >= 8) { out.push_back((uint8_t)(acc >> (nbits - 8))); nbits -= 8; }
		};
		std::map<std::pair<int, uint8_t>, int> table;
		put(256);
		int cur = -1;
		for (uint8_t b : in) {
			if (cur < 0) { cur = b; continue; }
			auto it = table.find({ cur, b });
			if (it != table.end()) { cur = it->second; continue; }
			put(cur);
			table[{ cur, b }] = next++;
			if (next >= (1 << width) && width < 12) width++;
			if (next >= 4093) { put(256); table.clear(); next = 258; width = 9; }
			cur = b;
		}
		if (cur >= 0) { put(cur); next++; if (next >= (1 << width) && width < 12) width++; }
		put(257);
		if (nbits > 0) out.push_back((uint8_t)(acc << (8 - nbits)));
		return out;
	}
	// a zlib stream of stored blocks (no compression) for the deflate path
	std::vector<uint8_t> ZlibStored(const std::vector<uint8_t>& in)
	{
		std::vector<uint8_t> out = { 0x78, 0x01 };
		size_t at = 0;
		do {
			const size_t n = std::min<size_t>(65535, in.size() - at);
			out.push_back(at + n >= in.size() ? 1 : 0);
			out.push_back((uint8_t)n); out.push_back((uint8_t)(n >> 8));
			out.push_back((uint8_t)~n); out.push_back((uint8_t)(~n >> 8));
			out.insert(out.end(), in.begin() + at, in.begin() + at + n);
			at += n;
		} while (at < in.size());
		uint32_t a = 1, b = 0;
		for (uint8_t c : in) { a = (a + c) % 65521; b = (b + a) % 65521; }
		const uint32_t ad = b << 16 | a;
		for (int i = 3; i >= 0; i--) out.push_back((uint8_t)(ad >> (i * 8)));
		return out;
	}
	// A Copernicus-like GeoTIFF: 32-bit float, tiled 256 x 256, the floating point predictor,
	// LZW or deflate, pixel-is-area, one degree from (lat, lon) at n pixels a side
	bool WriteFakeGeoTiff(const std::filesystem::path& p, int lat, int lon, int n, bool lzw)
	{
		const int tw = 256, across = (n + tw - 1) / tw;
		std::vector<std::vector<uint8_t>> tiles;
		for (int ty = 0; ty < across; ty++)
			for (int tx = 0; tx < across; tx++) {
				std::vector<uint8_t> raw((size_t)tw * tw * 4);
				for (int y = 0; y < tw; y++) {
					std::vector<uint8_t> row((size_t)tw * 4);
					for (int x = 0; x < tw; x++) {
						const int px = tx * tw + x, py = ty * tw + y;
						const float v = px < n && py < n ? FakeElevation(lat + 1 - (py + 0.5) / n, lon + (px + 0.5) / n) : 0.0f;
						uint32_t u;
						memcpy(&u, &v, 4);
						for (int k = 0; k < 4; k++) row[(size_t)k * tw + x] = (uint8_t)(u >> (24 - 8 * k)); // planes, the most significant first
					}
					for (size_t i = row.size() - 1; i > 0; i--) row[i] = (uint8_t)(row[i] - row[i - 1]);
					memcpy(raw.data() + (size_t)y * tw * 4, row.data(), row.size());
				}
				tiles.push_back(lzw ? LzwEncode(raw) : ZlibStored(raw));
			}
		// the layout: the header, the IFD (15 entries), the tiles' offsets and lengths, the pixel size, the tie point, the tiles
		const int entries = 15;
		const size_t offsArr = 8 + 2 + entries * 12 + 4, lensArr = offsArr + tiles.size() * 4, scaleArr = lensArr + tiles.size() * 4, tieArr = scaleArr + 24;
		size_t data = tieArr + 48;
		std::vector<uint32_t> offs, lens;
		for (auto& t : tiles) { offs.push_back((uint32_t)data); lens.push_back((uint32_t)t.size()); data += t.size(); }
		std::string f("II*\0", 4);
		auto u16 = [&](uint32_t v) { f += (char)(v & 255); f += (char)((v >> 8) & 255); };
		auto u32 = [&](uint32_t v) { for (int i = 0; i < 4; i++) f += (char)((v >> (i * 8)) & 255); };
		auto f64 = [&](double d) { uint64_t u; memcpy(&u, &d, 8); for (int i = 0; i < 8; i++) f += (char)((u >> (i * 8)) & 255); };
		u32(8);
		u16(entries);
		auto entry = [&](int tag, int type, uint32_t count, uint32_t value) { u16(tag); u16(type); u32(count); u32(value); };
		const bool one = tiles.size() == 1;
		entry(256, 3, 1, n); entry(257, 3, 1, n); entry(258, 3, 1, 32); entry(259, 3, 1, lzw ? 5 : 8); entry(262, 3, 1, 1); entry(277, 3, 1, 1);
		entry(284, 3, 1, 1); entry(317, 3, 1, 3); entry(322, 3, 1, tw); entry(323, 3, 1, tw);
		entry(324, 4, (uint32_t)tiles.size(), one ? offs[0] : (uint32_t)offsArr);
		entry(325, 4, (uint32_t)tiles.size(), one ? lens[0] : (uint32_t)lensArr);
		entry(339, 3, 1, 3); entry(33550, 12, 3, (uint32_t)scaleArr); entry(33922, 12, 6, (uint32_t)tieArr);
		u32(0);
		for (uint32_t o : offs) u32(o);
		for (uint32_t l : lens) u32(l);
		f64(1.0 / n); f64(1.0 / n); f64(0);
		f64(0); f64(0); f64(0); f64(lon); f64(lat + 1); f64(0);
		for (auto& t : tiles) f.append((const char*)t.data(), t.size());
		std::ofstream o(p, std::ios::binary);
		o.write(f.data(), (std::streamsize)f.size());
		return (bool)o;
	}
}

// Step 17.4 (needs the game data): three maps from three feature plans (an island, a river valley
// between ridges, a highland with passes), one from a 16-bit PNG heightmap with a river carved into
// it, and one from elevation files (a made-up SRTM .hgt and a GeoTIFF of the same land, LZW and
// deflate); each generated twice (the same heights), the hand layer untouched, the stats checked,
// compiled, loaded and played a minute. "TERRAIN <check> <0|1> ..." and "TERRAIN all <0|1>"
void Test_MapTerrain()
{
	LoadBCP("data.bcp");
	using oj = nlohmann::ordered_json;
	bool all = true;
	auto check = [&](const char* what, bool ok, const std::string& detail) {
		printf("TERRAIN %s %d %s\n", what, (int)ok, detail.c_str());
		fflush(stdout);
		all = all && ok;
	};
	auto num = [](const oj& j, std::initializer_list<const char*> path) {
		const oj* p = &j;
		for (const char* k : path) { if (!p->contains(k)) return std::nan(""); p = &(*p)[k]; }
		return p->is_number() ? p->get<double>() : std::nan("");
	};
	struct Made { std::string name; std::filesystem::path folder; };
	std::vector<Made> made;
	auto generate = [&](const std::string& name, const std::filesystem::path& folder, oj& st) {
		std::string error;
		const bool ok = MapTerrain::Generate(folder, st, error);
		if (!ok) { check((name + " generated").c_str(), false, error); return false; }
		const oj& w = st["walkable"];
		const oj& sl = st["slopes"]["tileShare"];
		std::string hist;
		for (const auto& v : sl) hist += (hist.empty() ? "" : " ") + std::to_string((int)std::lround(v.get<double>() * 1000));
		char b[512];
		snprintf(b, sizeof(b), "walkable %.3f (30deg %.3f, largest region %.3f), heights %.1f-%.1f m, slopes per mille by 5 deg [%s], %.0f ms",
			w["share"].get<double>(), w["share30deg"].get<double>(), w["largestRegionShare"].get<double>(), st["heights"]["minM"].get<double>(),
			st["heights"]["maxM"].get<double>(), hist.c_str(), st["timeMs"]["totalMs"].get<double>());
		printf("TERRAINSTATS %s %s\n", name.c_str(), b);
		for (const std::string& wn : st["warnings"]) printf("TERRAINSTATS %s warning: %s\n", name.c_str(), wn.c_str());
		fflush(stdout);
		return true;
	};

	// 1. an island: a large one with a ridge and a river, an islet, the sea all round
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
		const std::filesystem::path f = TerrainTestMap("Terrain Island", 256, terrain, features, TerrainPlayers(420, 360, 900, 520));
		oj st;
		if (generate("island", f, st)) {
			made.push_back({ "island", f });
			const double seaShare = num(st, { "sea", "playableShare" });
			check("island sea", seaShare > 0.25 && seaShare < 0.75 && st["lakes"].size() >= 1, "sea " + std::to_string(seaShare) + ", " + std::to_string(st["lakes"].size()) + " lake point(s)");
			check("island land walkable", num(st, { "walkable", "ofLand" }) > 0.8, std::to_string(num(st, { "walkable", "ofLand" })));
		}
	}
	// 2. a river valley between two ridges, with a pass over each, fords and farm terraces
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
		const std::filesystem::path f = TerrainTestMap("Terrain River Valley", 256, terrain, features, TerrainPlayers(300, 480, 1000, 800));
		oj st;
		if (generate("valley", f, st)) {
			made.push_back({ "valley", f });
			bool riverOk = false, passesOk = true;
			std::string d;
			for (const oj& fr : st["features"]) {
				if (fr["kind"] == "river") {
					riverOk = fr["bedMaxRiseM"].get<double>() <= 0 && fr["fords"].size() == 2;
					d += "river bed " + std::to_string(fr["bedFromM"].get<double>()) + " -> " + std::to_string(fr["bedToM"].get<double>()) + " m, max rise "
						+ std::to_string(fr["bedMaxRiseM"].get<double>()) + "; ";
				}
				if (fr.contains("passPaths"))
					for (const oj& pp : fr["passPaths"]) {
						passesOk = passesOk && pp["maxSlopeDeg"].get<double>() < 20;
						d += "pass " + std::to_string(pp["maxSlopeDeg"].get<double>()) + " deg; ";
					}
			}
			check("valley river falls", riverOk, d);
			check("valley passes walkable", passesOk, d);
		}
	}
	// 3. a highland: two plateaus for the bases with ramps, a high ridge between them with two passes, a peak, a tarn's basin
	std::string handBefore;
	std::filesystem::path highland;
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
		highland = TerrainTestMap("Terrain Highland", 256, terrain, features, TerrainPlayers(250, 250, 1030, 1030));
		// a hand edit first: a mound the generator must leave alone
		const int gw = 256 + 61;
		std::vector<uint16_t> hand((size_t)gw * gw, (uint16_t)MapFolder::HAND_ZERO);
		for (int r = 140; r < 150; r++) for (int x = 140; x < 150; x++) hand[(size_t)r * gw + x] = MapFolder::HAND_ZERO + 2 * 256;
		MapFolder::WritePng16(highland / "heights.hand.png", gw, gw, hand);
		handBefore = FileBytes(highland / "heights.hand.png");
		oj st;
		if (generate("highland", highland, st)) {
			made.push_back({ "highland", highland });
			bool rampsOk = true, passesOk = true, flatOk = true;
			std::string d;
			for (const oj& fr : st["features"]) {
				if (fr["kind"] == "plateau") {
					flatOk = flatOk && fr["flatShare"].get<double>() > 0.8;
					d += fr["id"].get<std::string>() + " flat " + std::to_string(fr["flatShare"].get<double>()) + " ramps";
					for (const oj& r : fr["ramps"]) { rampsOk = rampsOk && r["maxSlopeDeg"].get<double>() < 25; d += " " + std::to_string((int)r["maxSlopeDeg"].get<double>()) + "deg"; }
					d += "; ";
				}
				if (fr.contains("passPaths"))
					for (const oj& pp : fr["passPaths"]) { passesOk = passesOk && pp["maxSlopeDeg"].get<double>() < 20; d += "pass " + std::to_string((int)pp["maxSlopeDeg"].get<double>()) + "deg; "; }
			}
			check("highland plateaus flat", flatOk, d);
			check("highland ramps and passes walkable", rampsOk && passesOk, d);
			check("highland connected", num(st, { "walkable", "largestRegionOfWalkable" }) > 0.9, std::to_string(num(st, { "walkable", "largestRegionOfWalkable" })));
			check("highland hand layer untouched", FileBytes(highland / "heights.hand.png") == handBefore && st["handLayer"].get<std::string>().rfind("kept", 0) == 0, st["handLayer"].get<std::string>());
		}
	}
	// the same plan, the same heights; another seed, other heights
	{
		oj a, b, c;
		std::string e;
		const bool ok = MapTerrain::Generate(highland, a, e) && MapTerrain::Generate(highland, b, e) && MapTerrain::Generate(highland, c, e, 99);
		MapTerrain::Generate(highland, a, e);
		check("deterministic", ok && a["heightsHash"] == b["heightsHash"] && a["heightsHash"] != c["heightsHash"],
			a["heightsHash"].dump() + " " + b["heightsHash"].dump() + " seed 99 " + c["heightsHash"].dump());
	}
	// 4. a 16-bit PNG heightmap (a made-up caldera with radial ridges, 513 x 513), cropped, mapped to 4-70 m, a river carved into it
	{
		const std::filesystem::path src = g_userPath / "saved" / "terrain-test-heightmap.png";
		std::vector<uint16_t> px(513 * 513);
		for (int y = 0; y < 513; y++)
			for (int x = 0; x < 513; x++) {
				const double dx = (x - 300) / 180.0, dy = (y - 240) / 180.0, r = std::sqrt(dx * dx + dy * dy), a = std::atan2(dy, dx);
				double h = 0.25 + 0.55 * std::exp(-(r - 0.75) * (r - 0.75) / 0.06) * (0.8 + 0.2 * std::cos(a * 9)) - 0.15 * std::exp(-r * r / 0.12);
				h += 0.08 * std::sin(x * 0.05) * std::cos(y * 0.043) + 0.25 * std::max(0.0, (x + y - 700) / 400.0);
				px[(size_t)y * 513 + x] = (uint16_t)std::clamp((int)(h * 50000), 0, 65535);
			}
		MapFolder::WritePng16(src, 513, 513, px);
		const oj terrain = { { "erosion", { { "hydraulic", 0.6 }, { "thermal", 0.5 } } }, { "noise", { { "amplitude", 1.5 } } } };
		const oj features = oj::array({ { { "kind", "river" }, { "id", "outflow" }, { "line", { { 560, 700 }, { 760, 420 }, { 1100, -100 } } }, { "width", 16 }, { "depth", 2.5 } } });
		const std::filesystem::path f = TerrainTestMap("Terrain PNG", 200, oj::object(), features, TerrainPlayers(250, 250, 800, 820));
		// (the plan's terrain is kept: the import adds the source, the erosion and noise above stay)
		{
			nlohmann::ordered_json mj = nlohmann::ordered_json::parse(FileBytes(f / "map.json"));
			mj["terrain"] = terrain;
			std::ofstream(f / "map.json", std::ios::binary) << mj.dump(1) << "\n";
		}
		oj o = { { "file", PathToUtf8(src) }, { "crop", { 40, 20, 500, 480 } }, { "range", { 4, 70 } }, { "licence", "made by the test (GPL-3, as wkbai)" } }, out;
		std::string e;
		const bool ok = MapTerrain::ImportHeightmap(f, o, out, e);
		if (!ok) check("png import", false, e);
		else {
			oj& st = out["generated"];
			generate("png", f, st);
			made.push_back({ "png", f });
			const oj mj = oj::parse(FileBytes(f / "map.json"));
			const bool refOk = mj.contains("references") && mj["references"].size() == 1 && mj["references"][0]["kind"] == "heightmap";
			double rise = 1;
			for (const oj& fr : st["features"]) if (fr["kind"] == "river") rise = fr["bedMaxRiseM"].get<double>();
			check("png import", refOk && rise <= 0 && std::abs(out["source"]["min"].get<double>() - 4) < 6,
				"source " + out["source"]["min"].dump() + "-" + out["source"]["max"].dump() + " m, river max rise " + std::to_string(rise) + ", reference " + (refOk ? "kept" : "missing"));
		}
	}
	// 5. elevation: no files -> the tiles named, nothing downloaded; a made-up SRTM .hgt and a
	//    GeoTIFF (LZW, then deflate) of the same land give the same map
	{
		const std::filesystem::path dir = g_userPath / "saved" / "terrain-test-elevation";
		std::error_code ec;
		std::filesystem::create_directories(dir / "deflate", ec);
		const int n = 1201;
		std::string hgt((size_t)n * n * 2, '\0');
		for (int y = 0; y < n; y++)
			for (int x = 0; x < n; x++) {
				const int16_t v = (int16_t)std::lround(FakeElevation(47.0 - (double)y / (n - 1), 10.0 + (double)x / (n - 1)));
				hgt[((size_t)y * n + x) * 2] = (char)((uint16_t)v >> 8);
				hgt[((size_t)y * n + x) * 2 + 1] = (char)(v & 255);
			}
		std::ofstream(dir / "N46E010.hgt", std::ios::binary).write(hgt.data(), (std::streamsize)hgt.size());
		WriteFakeGeoTiff(dir / "Copernicus_DSM_COG_10_N46_00_E010_00_DEM.tif", 46, 10, 1200, true);
		WriteFakeGeoTiff(dir / "deflate" / "Copernicus_DSM_COG_10_N46_00_E010_00_DEM.tif", 46, 10, 1200, false);
		const std::filesystem::path f = TerrainTestMap("Terrain Elevation", 200, oj::object(), oj::array(), TerrainPlayers(300, 300, 700, 700));
		const oj area = { { "centre", { 46.2, 10.27 } }, { "sizeKm", 12 }, { "maxSlopeDeg", 38 } };
		oj o = area, out;
		std::string e;
		const bool refused = !MapTerrain::ImportElevation(f, o, out, e) && out.contains("missing") && !out["missing"].empty();
		check("elevation names the tiles", refused && out["missing"][0]["tile"] == "Copernicus_DSM_COG_10_N46_00_E010_00_DEM"
			&& out["missing"][0]["licence"].get<std::string>().find("attribution") != std::string::npos, out.contains("missing") ? out["missing"][0]["url"].dump() : e);
		double hashes[3] = {};
		std::string d;
		const char* files[3] = { "N46E010.hgt", "Copernicus_DSM_COG_10_N46_00_E010_00_DEM.tif", "deflate/Copernicus_DSM_COG_10_N46_00_E010_00_DEM.tif" };
		float mins[3] = {}, maxs[3] = {};
		bool ok = true;
		for (int k = 0; k < 3; k++) {
			o = area;
			o["source"] = k == 0 ? "srtm" : "copernicus";
			o["files"] = { PathToUtf8(dir / PathFromUtf8(files[k])) };
			out = oj();
			const bool r = MapTerrain::ImportElevation(f, o, out, e);
			ok = ok && r;
			if (!r) { d += std::string(files[k]) + ": " + e + "; "; continue; }
			mins[k] = out["source"]["min"].get<float>();
			maxs[k] = out["source"]["max"].get<float>();
			hashes[k] = out["generated"]["walkable"]["share"].get<double>();
			d += std::string(files[k]) + " " + std::to_string(mins[k]) + "-" + std::to_string(maxs[k]) + " m walkable " + std::to_string(hashes[k])
				+ " sea " + out["sea"].dump() + " exaggeration " + std::to_string(out["source"]["exaggeration"].get<double>()) + "; ";
			if (k == 2) {
				generate("elevation", f, out["generated"]);
				made.push_back({ "elevation", f });
			}
		}
		const oj mj = oj::parse(FileBytes(f / "map.json"));
		const bool licence = mj.contains("references") && mj["references"][0].value("attribution", "").find("Copernicus") != std::string::npos;
		check("elevation import", ok && licence && std::abs(maxs[0] - maxs[1]) < 3 && std::abs(maxs[1] - maxs[2]) < 0.01f && std::abs(hashes[0] - hashes[1]) < 0.02, d);
	}
	// compiled, loaded as the skirmish list names them, played a minute: the Manors still stand
	for (const Made& mm : made) {
		const MapFolder::Result r = MapFolder::Compile(mm.folder, true);
		if (!r.ok) { check((mm.name + " compiled").c_str(), false, r.error); continue; }
		Server server;
		server.loadSaveGame(MapFolder::GamePathOf(mm.folder).c_str());
		g_settings["simSeed"] = 7u;
		server.startLevel();
		server.timeManager.unpause();
		StepServer(server, 3000);
		int manors = 0, flooded = 0;
		auto count = [&](CommonGameObject* obj, auto& self) -> void {
			for (const auto& [type, children] : obj->children)
				for (CommonGameObject* child : children) {
					if (child->blueprint->name == "Manor" && !child->dyncast<ServerGameObject>()->deleted) manors++;
					self(child, self);
				}
		};
		count(server.getLevel(), count);
		for (int i = 0; i < server.terrain->width * server.terrain->height; i++) flooded += server.terrain->tiles[i].fullOfWater;
		char b[200];
		snprintf(b, sizeof(b), "%d Manors standing after 60 s (t=%.1f), %d tiles under water, %zu lakes", manors, server.timeManager.currentTime, flooded, server.terrain->lakes.size());
		check((mm.name + " plays").c_str(), manors == 2, b);
	}
	printf("TERRAIN all %d\n", (int)all);
	fflush(stdout);
}

