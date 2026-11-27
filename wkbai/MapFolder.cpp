// wkbai - wkbai's own map format, its compiler and the import of original maps (MapFolder.h)

#include "MapFolder.h"
#include "MapAuthoring.h"
#include "MapTerrain.h"
#include "MapMaterials.h"
#include "terrain.h"
#include "server.h"
#include "file.h"
#include "SaveFile.h"
#include "WaterDesign.h"
#include "MapWater.h"
#include "MapObjects.h"
#include "gameset/gameset.h"
#include "gameset/GameObjBlueprint.h"
#include "Environment.h"
#include "TrnTextureDb.h"
#include "settings.h"
#include "tags.h"
#include "util/StriCompare.h"
#include <nlohmann/json.hpp>
#include <stb_image.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#include <stb_image_write.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>

// the mods' folder, as file.cpp's (its own is file-local): "modsPath" in wkconfig.json, else "mods"
static std::filesystem::path ModsPath()
{
	return PathFromUtf8(g_settings.value<std::string>("modsPath", "mods"));
}
std::string portablePath(std::string p); // file.cpp

namespace MapFolder {
namespace {
	namespace fs = std::filesystem;
	using json = nlohmann::ordered_json;

	bool ReadAll(const fs::path& p, std::string& out)
	{
		std::ifstream f(p, std::ios::binary);
		if (!f)
			return false;
		out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
		return true;
	}
	bool WriteAll(const fs::path& p, const std::string& data)
	{
		std::error_code ec;
		fs::create_directories(p.parent_path(), ec);
		std::ofstream f(p, std::ios::binary);
		f.write(data.data(), (std::streamsize)data.size());
		return (bool)f;
	}
	uint64_t Fnv(uint64_t h, const void* data, size_t n)
	{
		for (size_t i = 0; i < n; i++) { h ^= ((const uint8_t*)data)[i]; h *= 1099511628211ull; }
		return h;
	}
	std::string Hex64(uint64_t v)
	{
		char buf[24];
		snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)v);
		return buf;
	}
	std::string StripSlash(std::string p)
	{
		while (!p.empty() && (p.back() == '\\' || p.back() == '/'))
			p.pop_back();
		return p;
	}
	bool StartsWithCI(const std::string& s, const char* prefix)
	{
		const size_t n = strlen(prefix);
		return s.size() >= n && StrCICompare(std::string_view(s).substr(0, n), std::string_view(prefix, n)) == 0;
	}
	std::string Backslashes(std::string s)
	{
		for (char& c : s)
			if (c == '/')
				c = '\\';
		return s;
	}
	std::string Narrow(const std::wstring& w)
	{
		// (the BCM's strings end with a NUL, left out of map.json; BuildTerrain adds it back)
		std::string s;
		for (wchar_t c : w)
			s += (char)c;
		while (!s.empty() && s.back() == 0)
			s.pop_back();
		return s;
	}

	// a tile's random numbers from the seed, the same whatever the order tiles are made in
	uint32_t TileRandom(uint32_t seed, int x, int z)
	{
		uint64_t v = ((uint64_t)seed << 32) ^ ((uint64_t)(uint32_t)x << 16) ^ (uint32_t)z ^ 0x9E3779B97F4A7C15ull;
		v ^= v >> 30; v *= 0xBF58476D1CE4E5B9ull;
		v ^= v >> 27; v *= 0x94D049BB133111EBull;
		v ^= v >> 31;
		return (uint32_t)v;
	}

	// ---- map.json ----

	struct MapPlayer {
		std::string name;
		int colour = 1;
		bool hasStart = false;
		float x = 0, z = 0;
		std::string base = "Manor";
		int peasants = 4;
		bool autoStart = false;          // "start": "auto": the compiler chooses it (MapObjects.h)
		json buildings;                  // the base's buildings (null: bases.buildings)
	};
	struct MapJson {
		int width = 0, height = 0, edge = 30;
		float scale = 1.0f;
		std::string name, textureDb, translation, sky, gameSet = "Warrior Kings Game Set\\multi-player extensions.cpp";
		uint32_t sunColour = 0xFFFFFF, fogColour = 0x9FC5E6;
		Vector3 sunVector = Vector3(1, 1, 1);
		uint32_t seed = 1;
		std::string fill; // the ground's material (a texture group) where no hand tile is
		std::vector<MapPlayer> players;
		json raw;
	};

	uint32_t ColourFrom(const json& j, uint32_t def)
	{
		if (!j.is_array() || j.size() < 3)
			return def;
		return ((uint32_t)j[0].get<int>() & 255) << 16 | ((uint32_t)j[1].get<int>() & 255) << 8 | ((uint32_t)j[2].get<int>() & 255);
	}
	json ColourTo(uint32_t c) { return json::array({ (c >> 16) & 255, (c >> 8) & 255, c & 255 }); }

	bool ReadMapJson(const fs::path& folder, MapJson& m, std::string& error)
	{
		std::string text;
		if (!ReadAll(folder / "map.json", text)) {
			error = "no map.json";
			return false;
		}
		json j = json::parse(text, nullptr, false);
		if (j.is_discarded() || !j.is_object()) {
			error = "map.json is not JSON";
			return false;
		}
		if (j.value("format", "") != "wkbai map") {
			error = "map.json: not a wkbai map (\"format\": \"wkbai map\")";
			return false;
		}
		if (j.value("version", 1) > FORMAT_VERSION) {
			error = "map.json is version " + std::to_string(j.value("version", 1)) + ", this build reads " + std::to_string(FORMAT_VERSION);
			return false;
		}
		try {
			m.raw = j;
			m.name = j.value("name", PathToUtf8(folder.stem()));
			const json size = j.value("size", json::object());
			m.width = size.at("tiles").at(0).get<int>();
			m.height = size.at("tiles").at(1).get<int>();
			m.edge = size.value("edge", 30);
			const json heights = j.value("heights", json::object());
			m.scale = heights.value("scale", 1.0f);
			const json tex = j.value("textures", json::object());
			m.textureDb = tex.value("database", "");
			m.translation = tex.value("translation", "");
			m.sky = j.value("sky", "");
			const json sun = j.value("sun", json::object());
			m.sunColour = ColourFrom(sun.value("colour", json()), m.sunColour);
			if (sun.contains("direction"))
				m.sunVector = Vector3(sun["direction"][0].get<float>(), sun["direction"][1].get<float>(), sun["direction"][2].get<float>());
			m.fogColour = ColourFrom(j.value("fog", json()), m.fogColour);
			m.seed = j.value("seed", 1u);
			m.gameSet = j.value("gameSet", m.gameSet);
			m.fill = j.value("ground", json::object()).value("fill", "");
			for (const json& p : j.value("players", json::array())) {
				MapPlayer mp;
				mp.name = p.value("name", "Player " + std::to_string(m.players.size() + 1));
				mp.colour = p.value("colour", (int)m.players.size() + 1);
				if (p.contains("start") && p["start"].is_string())
					mp.autoStart = p["start"].get<std::string>() == "auto";
				else if (p.contains("start")) {
					mp.hasStart = true;
					mp.x = p["start"][0].get<float>();
					mp.z = p["start"][1].get<float>();
				}
				mp.base = p.value("base", mp.base);
				mp.peasants = p.value("peasants", mp.peasants);
				if (p.contains("buildings"))
					mp.buildings = p["buildings"];
				m.players.push_back(mp);
			}
		}
		catch (const std::exception& e) {
			error = std::string("map.json: ") + e.what();
			return false;
		}
		if (m.width < 2 * m.edge + 16 || m.height < 2 * m.edge + 16 || m.width > 1024 + 2 * m.edge || m.height > 1024 + 2 * m.edge) {
			error = "map.json: size.tiles must be 16-1024 playable tiles a side plus the edge on both sides";
			return false;
		}
		if (m.textureDb.empty()) {
			error = "map.json: textures.database is missing";
			return false;
		}
		return true;
	}

	// ---- heights ----

	// heights.png on the vertex grid, in 256ths of a step (row 0 of the picture the north edge)
	bool ReadBaseHeights(const fs::path& folder, const MapJson& m, std::vector<uint16_t>& out, std::vector<std::string>& warnings, std::string& error)
	{
		const int gw = m.width + 1, gh = m.height + 1;
		int w = 0, h = 0;
		std::vector<uint16_t> px;
		if (!ReadPng16(folder / "heights.png", w, h, px)) {
			error = "heights.png can't be read";
			return false;
		}
		out.assign((size_t)gw * gh, 0);
		if (w != gw || h != gh)
			warnings.push_back("heights.png is " + std::to_string(w) + " x " + std::to_string(h) + ", resampled to the " + std::to_string(gw) + " x "
				+ std::to_string(gh) + " vertices");
		for (int r = 0; r < gh; r++)
			for (int x = 0; x < gw; x++) {
				float v;
				if (w == gw && h == gh)
					v = px[(size_t)r * w + x];
				else {
					const float fx = gw > 1 ? x * (float)(w - 1) / (gw - 1) : 0, fy = gh > 1 ? r * (float)(h - 1) / (gh - 1) : 0;
					const int x0 = std::min((int)fx, w - 1), y0 = std::min((int)fy, h - 1);
					const int x1 = std::min(x0 + 1, w - 1), y1 = std::min(y0 + 1, h - 1);
					const float tx = fx - x0, ty = fy - y0;
					auto at = [&](int xx, int yy) { return (float)px[(size_t)yy * w + xx]; };
					v = (at(x0, y0) * (1 - tx) + at(x1, y0) * tx) * (1 - ty) + (at(x0, y1) * (1 - tx) + at(x1, y1) * tx) * ty;
				}
				// the picture's row r is the vertex row z = height - r
				out[(size_t)(m.height - r) * gw + x] = (uint16_t)std::clamp((int)std::lround(v), 0, 65535);
			}
		return true;
	}
	// heights.hand.png as offsets (empty: none)
	bool ReadHandHeights(const fs::path& folder, const MapJson& m, std::vector<int>& out, std::vector<std::string>& warnings)
	{
		out.clear();
		const int gw = m.width + 1, gh = m.height + 1;
		if (!fs::exists(folder / "heights.hand.png"))
			return true;
		int w = 0, h = 0;
		std::vector<uint16_t> px;
		if (!ReadPng16(folder / "heights.hand.png", w, h, px) || w != gw || h != gh) {
			warnings.push_back("heights.hand.png can't be read or isn't " + std::to_string(gw) + " x " + std::to_string(gh) + ": left out");
			return true;
		}
		out.assign((size_t)gw * gh, 0);
		for (int r = 0; r < gh; r++)
			for (int x = 0; x < gw; x++)
				out[(size_t)(m.height - r) * gw + x] = (int)px[(size_t)r * w + x] - HAND_ZERO;
		return true;
	}
	// a terrain's heights in 256ths of a step, per vertex in memory order
	uint16_t TerrainHeight256(const Terrain& t, int i)
	{
		return (uint16_t)(t.vertices[i] * 256 + (t.heightFraction.empty() ? 0 : t.heightFraction[i]));
	}
	bool WriteGridPng(const fs::path& path, const MapJson& m, const std::vector<uint16_t>& grid)
	{
		const int gw = m.width + 1, gh = m.height + 1;
		std::vector<uint16_t> px((size_t)gw * gh);
		for (int r = 0; r < gh; r++)
			for (int x = 0; x < gw; x++)
				px[(size_t)r * gw + x] = grid[(size_t)(m.height - r) * gw + x];
		return WritePng16(path, gw, gh, px);
	}

	// ---- tiles ----

	struct HandTile {
		std::string group;
		int id = 0;
		int rot = 0, xflip = 0, zflip = 0;
	};
	// tiles.hand.json: { "format": "wkbai hand tiles", "version": 1, "tiles": [w, h],
	//   "textures": [ [group, id], ... ],
	//   "rows": [ "<row z = 0>", ... ] }: per row tokens "<texture>.<turn><flips>" (flips: 0 none,
	//   1 x, 2 z, 3 both), "-" a tile the rules make, each optionally "*<count>" for a run
	bool ReadHandTiles(const fs::path& folder, const MapJson& m, std::map<int, HandTile>& out, std::vector<std::string>& warnings,
		const fs::path& file = "tiles.hand.json")
	{
		out.clear();
		std::string text;
		if (!ReadAll(folder / file, text))
			return true;
		json j = json::parse(text, nullptr, false);
		if (j.is_discarded() || !j.is_object() || j.value("format", "") != "wkbai hand tiles") {
			warnings.push_back("tiles.hand.json is not a wkbai hand tiles file: left out");
			return true;
		}
		try {
			std::vector<std::pair<std::string, int>> textures;
			for (const json& t : j.at("textures"))
				textures.emplace_back(t.at(0).get<std::string>(), t.at(1).get<int>());
			const json& rows = j.at("rows");
			for (int z = 0; z < m.height && z < (int)rows.size(); z++) {
				std::istringstream in(rows[z].get<std::string>());
				std::string tok;
				int x = 0;
				while (in >> tok && x < m.width) {
					int count = 1;
					if (const size_t star = tok.find('*'); star != std::string::npos) {
						count = std::max(1, atoi(tok.c_str() + star + 1));
						tok.resize(star);
					}
					if (tok == "-") {
						x += count;
						continue;
					}
					const size_t dot = tok.find('.');
					const int ti = atoi(tok.c_str());
					int rot = 0, flips = 0;
					if (dot != std::string::npos && dot + 1 < tok.size()) {
						rot = (tok[dot + 1] - '0') & 3;
						if (dot + 2 < tok.size())
							flips = (tok[dot + 2] - '0') & 3;
					}
					if (ti < 0 || ti >= (int)textures.size()) {
						warnings.push_back("tiles.hand.json row " + std::to_string(z) + ": texture " + std::to_string(ti) + " isn't listed");
						x += count;
						continue;
					}
					for (int k = 0; k < count && x < m.width; k++, x++)
						out[z * m.width + x] = HandTile{ textures[ti].first, textures[ti].second, rot, flips & 1, (flips >> 1) & 1 };
				}
			}
		}
		catch (const std::exception& e) {
			warnings.push_back(std::string("tiles.hand.json: ") + e.what() + ": left out");
			out.clear();
		}
		return true;
	}
	bool WriteHandTiles(const fs::path& path, const MapJson& m, const std::map<int, HandTile>& tiles)
	{
		std::vector<std::pair<std::string, int>> textures;
		std::map<std::pair<std::string, int>, int> index;
		auto texOf = [&](const HandTile& t) {
			const auto key = std::make_pair(t.group, t.id);
			auto it = index.find(key);
			if (it != index.end())
				return it->second;
			index[key] = (int)textures.size();
			textures.push_back(key);
			return (int)textures.size() - 1;
		};
		std::vector<std::string> rows;
		for (int z = 0; z < m.height; z++) {
			std::string row, last;
			int run = 0;
			auto flush = [&]() {
				if (!run)
					return;
				if (!row.empty())
					row += ' ';
				row += last;
				if (run > 1)
					row += "*" + std::to_string(run);
			};
			for (int x = 0; x < m.width; x++) {
				std::string tok = "-";
				if (auto it = tiles.find(z * m.width + x); it != tiles.end()) {
					const HandTile& t = it->second;
					tok = std::to_string(texOf(t)) + "." + std::to_string(t.rot) + std::to_string(t.xflip | (t.zflip << 1));
				}
				if (tok == last)
					run++;
				else {
					flush();
					last = tok;
					run = 1;
				}
			}
			flush();
			rows.push_back(row);
		}
		json j;
		j["format"] = "wkbai hand tiles";
		j["version"] = 1;
		j["tiles"] = { m.width, m.height };
		json tex = json::array();
		for (const auto& [g, id] : textures)
			tex.push_back({ g, id });
		j["textures"] = tex;
		j["rows"] = rows;
		return WriteAll(path, j.dump(1) + "\n");
	}

	// the rules' tile: the ground's material, a random texture of the group, a random turn (from
	// the seed); null when the map has no material
	TerrainTexture* RuleTile(const MapJson& m, const TerrainTextureDatabase& db, int x, int z, int& rot)
	{
		rot = 0;
		if (m.fill.empty())
			return nullptr;
		auto g = db.groups.find(m.fill);
		if (g == db.groups.end() || !g->second || g->second->textures.empty())
			return nullptr;
		const uint32_t r = TileRandom(m.seed, x, z);
		auto it = g->second->textures.begin();
		std::advance(it, r % g->second->textures.size());
		rot = (r >> 16) & 3;
		return it->second.get();
	}

	// ---- water.json ----
	// the water design's JSON (WaterDesign.h) with "lakes": [[x, level, z, bits], ...] (whole-map
	// world units, the edge included, as the BCM's lake points); "tiles" may be left out when there
	// is no design
	bool ReadWater(const fs::path& folder, const MapJson& m, std::vector<Vector3>& lakes, std::vector<uint8_t>& bits,
		std::shared_ptr<WaterDesign>& design, std::vector<std::string>& warnings)
	{
		lakes.clear();
		bits.clear();
		design.reset();
		std::string text;
		if (!ReadAll(folder / "water.json", text))
			return true;
		json j = json::parse(text, nullptr, false);
		if (j.is_discarded() || !j.is_object()) {
			warnings.push_back("water.json is not JSON: left out");
			return true;
		}
		try {
			for (const json& l : j.value("lakes", json::array())) {
				lakes.emplace_back(l.at(0).get<float>(), l.at(1).get<float>(), l.at(2).get<float>());
				bits.push_back(l.size() > 3 ? (uint8_t)(l[3].get<int>() & 3) : 0);
			}
		}
		catch (const std::exception& e) {
			warnings.push_back(std::string("water.json lakes: ") + e.what());
		}
		if (j.contains("tiles")) {
			auto d = std::make_shared<WaterDesign>();
			std::string error;
			if (!d->fromText(text, error))
				warnings.push_back("water.json design: " + error + ": left out");
			else if (d->width != m.width || d->height != m.height)
				warnings.push_back("water.json design is for " + std::to_string(d->width) + " x " + std::to_string(d->height) + " tiles: left out");
			else if (!d->isEmpty())
				design = d;
		}
		return true;
	}
	bool WriteWater(const fs::path& path, const std::vector<Vector3>& lakes, const std::vector<uint8_t>& bits, const WaterDesign* design, int width, int height)
	{
		json j;
		if (design) {
			j = json::parse(design->toText());
		}
		else {
			j["format"] = "wkbai water";
			j["version"] = 1;
		}
		json l = json::array();
		for (size_t i = 0; i < lakes.size(); i++)
			l.push_back({ lakes[i].x, lakes[i].y, lakes[i].z, i < bits.size() ? bits[i] : 0 });
		j["lakes"] = l;
		(void)width; (void)height;
		return WriteAll(path, j.dump(1) + "\n");
	}

	// ---- the minimap ----
	bool ReadMinimap(const fs::path& folder, std::vector<uint8_t>& rgb)
	{
		std::string data;
		if (!ReadAll(folder / "minimap.png", data))
			return false;
		int w, h, n;
		stbi_uc* px = stbi_load_from_memory((const stbi_uc*)data.data(), (int)data.size(), &w, &h, &n, 3);
		if (!px)
			return false;
		const bool ok = w == 128 && h == 128;
		if (ok)
			rgb.assign(px, px + 128 * 128 * 3);
		stbi_image_free(px);
		return ok;
	}
	void PngToString(void* ctx, void* data, int size) { ((std::string*)ctx)->append((const char*)data, size); }
	bool WriteMinimap(const fs::path& path, const std::vector<uint8_t>& rgb)
	{
		std::string out;
		if (rgb.size() != 128 * 128 * 3 || !stbi_write_png_to_func(PngToString, &out, 128, 128, 3, rgb.data(), 128 * 3))
			return false;
		return WriteAll(path, out);
	}

	// ---- the BCM the cache holds ----
	// (MapFolder only: Terrain::writeBCM needs the BCM it was read from)
	bool WriteCacheBCM(const fs::path& path, const Terrain& t, const std::vector<uint8_t>& minimapRGB)
	{
		std::vector<uint8_t> out;
		const char magic[12] = { 'W', 'K', 'B', 'A', 'I', ' ', 'C', 'A', 'C', 'H', 'E', 0 };
		out.insert(out.end(), magic, magic + 12);
		auto u32 = [&](uint32_t v) { out.insert(out.end(), (const uint8_t*)&v, (const uint8_t*)&v + 4); };
		auto f32 = [&](float v) { out.insert(out.end(), (const uint8_t*)&v, (const uint8_t*)&v + 4); };
		auto wstr = [&](const std::wstring& s) {
			const uint16_t bytes = (uint16_t)(s.size() * 2);
			out.insert(out.end(), (const uint8_t*)&bytes, (const uint8_t*)&bytes + 2);
			for (wchar_t c : s) {
				const uint16_t u = (uint16_t)c;
				out.insert(out.end(), (const uint8_t*)&u, (const uint8_t*)&u + 2);
			}
		};
		u32((uint32_t)t.width); u32((uint32_t)t.height); u32((uint32_t)t.edge);
		wstr(t.texDbPath);
		f32(t.scale);
		u32((t.sunColor >> 16) & 255); u32((t.sunColor >> 8) & 255); u32(t.sunColor & 255);
		f32(t.sunVector.x); f32(t.sunVector.y); f32(t.sunVector.z);
		u32((t.fogColor >> 16) & 255); u32((t.fogColor >> 8) & 255); u32(t.fogColor & 255);
		wstr(t.skyTextureDirectory);
		if (minimapRGB.size() == 128 * 128 * 3)
			out.insert(out.end(), minimapRGB.begin(), minimapRGB.end());
		else
			out.resize(out.size() + 128 * 128 * 3, 128);
		uint64_t bit = (uint64_t)out.size() * 8;
		auto put = [&](uint32_t value, int bits) {
			for (int i = 0; i < bits; i++, bit++) {
				const size_t byte = (size_t)(bit / 8);
				if (byte >= out.size())
					out.resize(byte + 1, 0);
				if ((value >> i) & 1)
					out[byte] |= (uint8_t)(1 << (bit % 8));
			}
		};
		const size_t lakeCount = std::min<size_t>(t.lakes.size(), 63);
		put((uint32_t)lakeCount, 6);
		for (size_t i = 0; i < lakeCount; i++) {
			for (float f : { t.lakes[i].x, t.lakes[i].y, t.lakes[i].z }) {
				uint32_t u;
				memcpy(&u, &f, 4);
				put(u, 32);
			}
			put(i < t.lakeExtraBits.size() ? t.lakeExtraBits[i] : 0, 2);
		}
		std::vector<TerrainTextureGroup*> groups;
		std::vector<int> ids;
		std::vector<std::pair<TerrainTextureGroup*, int>> used;
		auto indexOf = [](auto& list, auto value) {
			const auto it = std::find(list.begin(), list.end(), value);
			if (it != list.end())
				return (uint32_t)(it - list.begin());
			list.push_back(value);
			return (uint32_t)(list.size() - 1);
		};
		for (int i = 0; i < t.width * t.height; i++) {
			const TerrainTexture* tex = t.tiles[i].texture;
			indexOf(groups, tex->grp);
			indexOf(ids, tex->id);
			const auto pair = std::make_pair(tex->grp, tex->id);
			if (std::find(used.begin(), used.end(), pair) == used.end())
				used.push_back(pair);
		}
		put((uint32_t)groups.size(), 16);
		for (TerrainTextureGroup* g : groups) {
			put((uint32_t)g->name.size(), 8);
			for (char c : g->name)
				put((uint8_t)c, 8);
		}
		put((uint32_t)ids.size(), 16);
		for (int id : ids)
			put((uint32_t)id, 32);
		const int groupBits = Terrain::GetMaxBits((int)groups.size()), idBits = Terrain::GetMaxBits((int)ids.size());
		for (int z = t.height - 1; z >= 0; z--)
			for (int x = 0; x < t.width; x++) {
				const TerrainTile& tile = t.tiles[z * t.width + x];
				put(indexOf(groups, tile.texture->grp), groupBits);
				put(indexOf(ids, tile.texture->id), idBits);
				put(tile.rot, 2);
				put(tile.xflip, 1);
				put(tile.zflip, 1);
			}
		put((uint32_t)used.size(), 32);
		put(0x62, 32);
		for (const auto& [g, id] : used) {
			put(indexOf(groups, g), groupBits);
			put(indexOf(ids, id), idBits);
		}
		for (int y = 0; y <= t.height; y++)
			for (int x = 0; x <= t.width; x++)
				put(t.vertices[(t.height - y) * (t.width + 1) + x], 8);
		std::string s((const char*)out.data(), out.size());
		return WriteAll(path, s);
	}

	// ---- the level ----

	struct Authoring {
		std::vector<uint32_t> players;              // map.json's players, by index: their PLAYER ids
		std::map<uint32_t, std::string> objects;    // object id -> the rule that made it ("base:0")
		std::map<uint32_t, std::pair<float, float>> positions; // where the rule put it (x, z)
		std::set<std::string> suppressed;           // rules a hand edit took over
		json source;                                // where an imported map came from
	};
	Authoring ParseAuthoring(const std::string& text)
	{
		Authoring a;
		json j = json::parse(text, nullptr, false);
		if (j.is_discarded() || !j.is_object())
			return a;
		for (const json& p : j.value("players", json::array()))
			a.players.push_back(p.get<uint32_t>());
		// (a named copy: items() of the temporary j.value() returned dangled, and the objects came
		// back empty, so a hand-edited level kept every rule's objects and got them made again)
		const json objects = j.value("objects", json::object());
		for (const auto& [id, rule] : objects.items()) {
			const uint32_t oid = (uint32_t)strtoul(id.c_str(), nullptr, 10);
			if (rule.is_object()) {
				a.objects[oid] = rule.value("by", "");
				if (rule.contains("at"))
					a.positions[oid] = { rule["at"][0].get<float>(), rule["at"][1].get<float>() };
			}
			else if (rule.is_string())
				a.objects[oid] = rule.get<std::string>();
		}
		for (const json& s : j.value("suppressed", json::array()))
			a.suppressed.insert(s.get<std::string>());
		a.source = j.value("source", json());
		return a;
	}
	std::string AuthoringText(const Authoring& a)
	{
		json j;
		j["format"] = "wkbai authoring";
		j["version"] = 1;
		j["note"] = "objects not listed are hand-made";
		if (!a.source.is_null())
			j["source"] = a.source;
		j["players"] = a.players;
		json objs = json::object();
		for (const auto& [id, rule] : a.objects) {
			json o;
			o["by"] = rule;
			if (auto p = a.positions.find(id); p != a.positions.end())
				o["at"] = { p->second.first, p->second.second };
			objs[std::to_string(id)] = o;
		}
		j["objects"] = objs;
		j["suppressed"] = json(std::vector<std::string>(a.suppressed.begin(), a.suppressed.end()));
		return j.dump(1);
	}

	// the lines of a text, with their line breaks kept
	std::vector<std::string> SplitLines(const std::string& text)
	{
		std::vector<std::string> lines;
		size_t start = 0;
		while (start < text.size()) {
			size_t end = text.find('\n', start);
			if (end == std::string::npos)
				end = text.size() - 1;
			lines.push_back(text.substr(start, end - start + 1));
			start = end + 1;
		}
		return lines;
	}
	std::string Trimmed(const std::string& line)
	{
		const size_t a = line.find_first_not_of(" \t\r\n");
		if (a == std::string::npos)
			return "";
		const size_t b = line.find_last_not_of(" \t\r\n");
		return line.substr(a, b - a + 1);
	}
	// an object's line without its blueprint, "<CLASS> <id>", as the original's levels write many
	// (the blueprint is in PREDEC): the class and id
	bool BareObjectLine(const std::string& line, std::string& cls, uint32_t& id)
	{
		const std::string t = Trimmed(line);
		const size_t sp = t.find(' ');
		if (sp == std::string::npos)
			return false;
		cls = t.substr(0, sp);
		if (Tags::GAMEOBJCLASS_tagDict.getTagID(cls.c_str()) < 0)
			return false;
		char* end = nullptr;
		const unsigned long v = strtoul(t.c_str() + sp + 1, &end, 10);
		if (!end || end == t.c_str() + sp + 1 || *end != '\0')
			return false;
		id = (uint32_t)v;
		return true;
	}
	// an object's line "<CLASS> <id> "<blueprint>"": the class, id and blueprint
	bool ObjectLine(const std::string& line, std::string& cls, uint32_t& id, std::string& bp)
	{
		const std::string t = Trimmed(line);
		const size_t sp = t.find(' ');
		if (sp == std::string::npos)
			return false;
		cls = t.substr(0, sp);
		if (Tags::GAMEOBJCLASS_tagDict.getTagID(cls.c_str()) < 0)
			return false;
		char* end = nullptr;
		const unsigned long v = strtoul(t.c_str() + sp + 1, &end, 10);
		if (!end || end == t.c_str() + sp + 1 || *end != ' ')
			return false;
		const char* q = strchr(end, '"');
		if (!q)
			return false;
		const char* q2 = strchr(q + 1, '"');
		if (!q2)
			return false;
		id = (uint32_t)v;
		bp.assign(q + 1, q2);
		return true;
	}
	// removes an object's block (and what it holds), by id; false if it isn't there
	bool RemoveObject(std::vector<std::string>& lines, uint32_t id)
	{
		for (size_t i = 0; i < lines.size(); i++) {
			std::string cls, bp;
			uint32_t oid;
			if (!ObjectLine(lines[i], cls, oid, bp) || oid != id)
				continue;
			const std::string endTag = "END_" + cls;
			const size_t indent = lines[i].find_first_not_of('\t');
			for (size_t k = i + 1; k < lines.size(); k++)
				if (Trimmed(lines[k]) == endTag && lines[k].find_first_not_of('\t') == indent) {
					size_t last = k;
					if (last + 1 < lines.size() && Trimmed(lines[last + 1]).empty())
						last++;
					lines.erase(lines.begin() + i, lines.begin() + last + 1);
					return true;
				}
			return false;
		}
		return false;
	}
	// where a player's block ends (its END_PLAYER line), npos if not found
	size_t PlayerEnd(const std::vector<std::string>& lines, uint32_t player)
	{
		for (size_t i = 0; i < lines.size(); i++) {
			std::string cls, bp;
			uint32_t oid;
			if (!ObjectLine(lines[i], cls, oid, bp) || cls != "PLAYER" || oid != player)
				continue;
			const size_t indent = lines[i].find_first_not_of('\t');
			for (size_t k = i + 1; k < lines.size(); k++)
				if (Trimmed(lines[k]) == "END_PLAYER" && lines[k].find_first_not_of('\t') == indent)
					return k;
		}
		return std::string::npos;
	}
	// PREDEC made again from the objects' lines (as Server::saveGame writes it; the old PREDEC for lines
	// without a blueprint); NEXT_UNIQUE_ID raised to the highest id when below it
	void RebuildPredec(std::vector<std::string>& lines)
	{
		std::map<std::pair<int, std::string>, std::vector<uint32_t>> byType;
		uint32_t levelId = 0, landscapeId = 0, maxId = 0;
		std::string levelBp = "Standard";
		size_t predecStart = std::string::npos, predecEnd = std::string::npos;
		bool inPredec = false;
		// the old PREDEC's blueprint per id: the original's levels name an object's blueprint only
		// there ("PLAYER 1026" in the world); rebuilt from the named lines alone, their PREDEC came
		// out empty and the level didn't load (test 99: 10 original maps crashed when imported)
		std::map<uint32_t, std::pair<int, std::string>> declared;
		std::pair<int, std::string> current{ -1, "" };
		std::vector<std::pair<std::string, uint32_t>> bare; // class, id
		for (size_t i = 0; i < lines.size(); i++) {
			const std::string t = Trimmed(lines[i]);
			if (t == "PREDEC") { predecStart = i; inPredec = true; continue; }
			if (t == "END_PREDEC") { predecEnd = i; inPredec = false; continue; }
			if (inPredec) {
				if (t.rfind("LEVEL ", 0) == 0) {
					const size_t q = t.find('"'), q2 = t.find('"', q + 1);
					if (q != std::string::npos && q2 != std::string::npos)
						levelBp = t.substr(q + 1, q2 - q - 1);
					continue;
				}
				// <CLASS> "<blueprint>" <count> <ids...>, the ids maybe running on over more lines
				std::istringstream in(t);
				std::string first;
				in >> first;
				const size_t q = t.find('"'), q2 = q == std::string::npos ? q : t.find('"', q + 1);
				const int cls = Tags::GAMEOBJCLASS_tagDict.getTagID(first.c_str());
				if (cls >= 0 && q2 != std::string::npos) {
					current = { cls, t.substr(q + 1, q2 - q - 1) };
					std::istringstream r(t.substr(q2 + 1));
					uint32_t count, id;
					r >> count;
					while (r >> id) declared[id] = current;
				}
				else if (current.first >= 0) {
					std::istringstream r(t);
					uint32_t id;
					while (r >> id) declared[id] = current;
				}
				continue;
			}
			if (t.rfind("LEVEL ", 0) == 0 && !levelId)
				levelId = (uint32_t)strtoul(t.c_str() + 6, nullptr, 10);
			else if (t.rfind("LANDSCAPE ", 0) == 0 && !landscapeId)
				landscapeId = (uint32_t)strtoul(t.c_str() + 10, nullptr, 10);
			std::string cls, bp;
			uint32_t id;
			if (ObjectLine(lines[i], cls, id, bp) && cls != "LEVEL") {
				byType[{ Tags::GAMEOBJCLASS_tagDict.getTagID(cls.c_str()), bp }].push_back(id);
				maxId = std::max(maxId, id);
			}
			else if (BareObjectLine(lines[i], cls, id) && cls != "LEVEL")
				bare.push_back({ cls, id });
		}
		for (const auto& [cls, id] : bare) {
			auto it = declared.find(id);
			if (it == declared.end())
				continue; // no blueprint known: left out, as the server would
			byType[it->second].push_back(id);
			maxId = std::max(maxId, id);
		}
		maxId = std::max({ maxId, levelId, landscapeId });
		if (predecStart == std::string::npos || predecEnd == std::string::npos)
			return;
		std::vector<std::string> predec;
		predec.push_back("\tLEVEL \"" + levelBp + "\" 1 " + std::to_string(levelId) + "\n");
		if (landscapeId)
			predec.push_back("\tLANDSCAPE \"Standard\" 1 " + std::to_string(landscapeId) + "\n");
		for (auto& [type, ids] : byType) {
			std::sort(ids.begin(), ids.end());
			std::string text = std::string("\t ") + Tags::GAMEOBJCLASS_tagDict.getStringFromID(type.first) + " \"" + type.second + "\" " + std::to_string(ids.size()) + " ";
			for (size_t k = 0; k < ids.size(); k++) {
				text += std::to_string(ids[k]) + " ";
				if (text.size() > 1800 && k + 1 < ids.size()) {
					predec.push_back(text + "\n");
					text = "\t\t ";
				}
			}
			predec.push_back(text + "\n");
		}
		lines.erase(lines.begin() + predecStart + 1, lines.begin() + predecEnd);
		lines.insert(lines.begin() + predecStart + 1, predec.begin(), predec.end());
		for (std::string& l : lines)
			if (Trimmed(l).rfind("NEXT_UNIQUE_ID ", 0) == 0) {
				// the last id handed out: the server's allocator goes past it (++, skipping ids in use,
				// Server::createObject), so a level may well hold an object with that id. Raised only
				// when an object's id lies above it, and then to that id: "past the highest" (maxId + 1)
				// moved every id made in play by one on original maps whose NEXT_UNIQUE_ID equals an
				// object's id (Tribes Unite: 6044), and test 99's imports played otherwise (18 of 54)
				const uint32_t next = (uint32_t)strtoul(Trimmed(l).c_str() + 15, nullptr, 10);
				if (next < maxId)
					l = "NEXT_UNIQUE_ID " + std::to_string(maxId) + "\n";
				break;
			}
	}
	uint32_t NextUniqueId(const std::vector<std::string>& lines)
	{
		uint32_t maxId = 0;
		for (const std::string& l : lines) {
			const std::string t = Trimmed(l);
			if (t.rfind("NEXT_UNIQUE_ID ", 0) == 0)
				maxId = std::max(maxId, (uint32_t)strtoul(t.c_str() + 15, nullptr, 10) - 1);
			std::string cls, bp;
			uint32_t id;
			if (ObjectLine(l, cls, id, bp) || (BareObjectLine(l, cls, id) && cls != "LEVEL"))
				maxId = std::max(maxId, id);
		}
		return maxId + 1;
	}
	// the MAP line's file replaced
	bool SetMapLine(std::vector<std::string>& lines, const std::string& map)
	{
		for (std::string& l : lines) {
			const size_t first = l.find_first_not_of('\t');
			if (first == std::string::npos || l.compare(first, 5, "MAP \"") != 0)
				continue;
			l = l.substr(0, first) + "MAP \"" + map + "\" \n";
			return true;
		}
		return false;
	}
	std::string NameCodes(const std::string& name)
	{
		std::string s = "NAME " + std::to_string(name.size());
		for (unsigned char c : name)
			s += " " + std::to_string((int)c);
		return s;
	}
	std::string Num(float v)
	{
		char buf[32];
		snprintf(buf, sizeof(buf), "%.2f", v);
		return buf;
	}

	// A level for a map without level.txt: the Neutral Player, map.json's players and the
	// Baresark Player (no objects yet; the rules add them), the players all at war with each other.
	// Numbered as every original map is (LEVEL 1050, LANDSCAPE 1032, the Neutral Player 1026, the
	// players 1027, 1030, 1033 ... 1048, the Baresark Player 1037), since the game set names those
	// ids (FINDER_SPECIFIC_ID 1026, 1027, 1030 ... in Diplomacy, Cart Death, the churches, the Flag
	// Positioner's 1037). With 3, 10, 11, ... a generated map's scripts found no CONTROLLER (the
	// user's log on 8 Player Lake District). The rules' objects start at 1100.
	constexpr uint32_t ORIGINAL_NEUTRAL = 1026, ORIGINAL_FIRST_PLAYER = 1027, ORIGINAL_PLAYER_STEP = 3, ORIGINAL_BARESARK = 1037;
	std::string SkeletonWorld(const MapJson& m, const std::string& mapPath, std::vector<uint32_t>& playerIds)
	{
		const uint32_t levelId = 1050, landscapeId = 1032, neutral = ORIGINAL_NEUTRAL, baresark = ORIGINAL_BARESARK;
		playerIds.clear();
		for (size_t i = 0; i < m.players.size(); i++) {
			uint32_t id = ORIGINAL_FIRST_PLAYER + ORIGINAL_PLAYER_STEP * (uint32_t)i;
			// past the eighth player (1048), steps of 3 from 1100 (1050 is the level)
			if (i >= 8)
				id = 1100 + ORIGINAL_PLAYER_STEP * (uint32_t)(i - 8);
			playerIds.push_back(id);
		}
		std::string w;
		auto line = [&](const std::string& s) { w += s + "\n"; };
		line("FORMAT SCENARIO");
		line("VERSION 1.0");
		line("MODE TEXT");
		line("");
		line("GAME_SET \"" + m.gameSet + "\"");
		line("");
		line("GAME_TYPE 4");
		std::string humans;
		for (uint32_t id : playerIds)
			humans += " " + std::to_string(id);
		line("NUM_HUMAN_PLAYERS " + std::to_string(playerIds.size()) + humans);
		line("");
		line("UPDATE_ID 0");
		line("NEXT_UPDATE_TIME_STAMP 0.00");
		line("");
		line("RANDOM_SEED " + std::to_string(m.seed));
		line("");
		line("TIME_MANAGER_STATE");
		line("\tCURRENT_TIME 0.00");
		line("\tPREVIOUS_TIME 0.00");
		line("\tELAPSED_TIME 0.00");
		line("\tPAUSED 1");
		line("\tLOCK_COUNT 1");
		line("END_TIME_MANAGER_STATE");
		line("");
		uint32_t lastId = 1099; // the last id handed out: the rules' objects from 1100
		for (uint32_t id : playerIds)
			lastId = std::max(lastId, id);
		line("NEXT_UNIQUE_ID " + std::to_string(lastId));
		line("");
		line("PREDEC");
		line("\tLEVEL \"Standard\" 1 " + std::to_string(levelId));
		line("END_PREDEC");
		line("");
		line("LEVEL " + std::to_string(levelId));
		line("");
		line("\tNAME \"" + m.name + "\"");
		line("");
		line("\tLANDSCAPE " + std::to_string(landscapeId));
		line("");
		line("\t\tMAP \"" + mapPath + "\"");
		line("");
		line("\tEND_LANDSCAPE");
		line("");
		auto player = [&](uint32_t id, const char* bp, const std::string& name, int colour) {
			line("\tPLAYER " + std::to_string(id) + " \"" + bp + "\"");
			line("");
			line("\t\t" + NameCodes(name));
			line("\t\tCOLOUR_INDEX " + std::to_string(colour));
			line("\t\tSTART_CAMERA_POS 0.00 0.00 0.00");
			line("\t\tSTART_CAMERA_ORIENTATION 0.00 0.00");
			line("\t\tRECONNAISSANCE 0");
			line("");
			line("\tEND_PLAYER");
			line("");
		};
		player(neutral, "Neutral Player", "Neutral Player", 0);
		for (size_t i = 0; i < m.players.size(); i++)
			player(playerIds[i], "Human Player", m.players[i].name, m.players[i].colour);
		player(baresark, "Baresark Player", "Baresark Player", 0);
		for (size_t i = 0; i < m.players.size(); i++)
			line("\tDIPLOMATIC_STATUS_BETWEEN " + std::to_string(neutral) + " " + std::to_string(playerIds[i]) + " \"Neutral\"");
		line("\tDIPLOMATIC_STATUS_BETWEEN " + std::to_string(neutral) + " " + std::to_string(baresark) + " \"Neutral\"");
		for (size_t i = 0; i < m.players.size(); i++)
			line("\tDIPLOMATIC_STATUS_BETWEEN " + std::to_string(std::min(playerIds[i], baresark)) + " " + std::to_string(std::max(playerIds[i], baresark)) + " \"Enemy\"");
		for (size_t i = 0; i < m.players.size(); i++)
			for (size_t k = i + 1; k < m.players.size(); k++)
				line("\tDIPLOMATIC_STATUS_BETWEEN " + std::to_string(playerIds[i]) + " " + std::to_string(playerIds[k]) + " \"Enemy\"");
		line("");
		line("END_LEVEL " + std::to_string(levelId));
		line("");
		return w;
	}

	// sets a player's start camera (its line in the player's block)
	void SetStartCamera(std::vector<std::string>& lines, uint32_t player, const Vector3& pos, float pitch, float yaw)
	{
		const size_t end = PlayerEnd(lines, player);
		if (end == std::string::npos)
			return;
		for (size_t i = end; i-- > 0;) {
			std::string cls, bp;
			uint32_t oid;
			if (ObjectLine(lines[i], cls, oid, bp) && cls == "PLAYER")
				break;
			const std::string t = Trimmed(lines[i]);
			if (t.rfind("START_CAMERA_POS ", 0) == 0)
				lines[i] = "\t\tSTART_CAMERA_POS " + Num(pos.x) + " " + Num(pos.y) + " " + Num(pos.z) + " \n";
			else if (t.rfind("START_CAMERA_ORIENTATION ", 0) == 0)
				lines[i] = "\t\tSTART_CAMERA_ORIENTATION " + Num(pitch) + " " + Num(yaw) + " \n";
		}
	}

	// The level for the cache: level.txt (or a new one from map.json), the rules' objects made again,
	// ---- scatter: objects by rule (map.json "scatter"), the Neutral Player's ----
	// [{ "id": "forest", "objects": [["Coniferous Tree", ["Pine01", "Pine02"]], ["Deciduous Tree", []]],
	//    "on": ["forest floor"] (materials, or texture groups), "density": 30 (per hectare),
	//    "spacing": 6 (m), "clump": 0-1, "maxSlopeDeg": 32, "awayFromBases": 45 (m) },
	//  { "id": "gold", "near": "bases", "objects": [["Gold Deposit Big", []], ...], "groups": 2 (per base),
	//    "distance": [70, 160], "size": 3 (objects a group), "spread": 9 (m), "maxSlopeDeg": 14 },
	//  { "id": "stone", "count": 10 (groups over the map), ... }]
	// A second list, "objectsHigh", is picked at a share rising with the ground's height: highMin
	// at or below highFrom (m), highMax at or above highTo, in patches (conifers up the hills,
	// broadleaves in the valleys)
	// Deterministic from the map's seed; objects never on water, steep ground or one another
	uint32_t ScatterHash(uint32_t a, uint32_t b, uint32_t c, uint32_t d)
	{
		uint64_t v = ((uint64_t)a << 32 | b) ^ ((uint64_t)c << 17) ^ ((uint64_t)d << 3) ^ 0x9E3779B97F4A7C15ull;
		v ^= v >> 30; v *= 0xBF58476D1CE4E5B9ull;
		v ^= v >> 27; v *= 0x94D049BB133111EBull;
		v ^= v >> 31;
		return (uint32_t)v;
	}
	float ScatterUnit(uint32_t h) { return (h >> 8) * (1.0f / 16777216.0f); }
	float ScatterNoise(float x, float z, uint32_t s)
	{
		const float fx = std::floor(x), fz = std::floor(z);
		const int ix = (int)fx, iz = (int)fz;
		float u = x - fx, v = z - fz;
		u = u * u * (3 - 2 * u); v = v * v * (3 - 2 * v);
		auto at = [&](int a, int b) { return ScatterUnit(ScatterHash(s, (uint32_t)(ix + a), (uint32_t)(iz + b), 7)); };
		const float a = at(0, 0) + (at(1, 0) - at(0, 0)) * u, b = at(0, 1) + (at(1, 1) - at(0, 1)) * u;
		return a + (b - a) * v;
	}

	int Scatter(const fs::path& folder, const MapJson& m, const Terrain& terrain, std::vector<std::string>& lines, Authoring& auth,
		std::map<std::string, std::vector<uint32_t>>& idsOf, uint32_t& next, std::vector<std::string>& warnings, json& report,
		MapObjects::PlacementState* ps, const std::vector<std::pair<float, float>>& basesAt)
	{
		const json rules = m.raw.value("scatter", json::array());
		if (!rules.is_array() || rules.empty())
			return 0;
		// the Neutral Player's block
		uint32_t neutral = 0;
		for (const std::string& l : lines) {
			std::string cls, bp;
			uint32_t id;
			if (ObjectLine(l, cls, id, bp) && cls == "PLAYER" && bp == "Neutral Player") { neutral = id; break; }
		}
		const size_t end = neutral ? PlayerEnd(lines, neutral) : std::string::npos;
		if (end == std::string::npos) {
			warnings.push_back("scatter: the level has no Neutral Player");
			return 0;
		}
		CommonGameObject* neutralObj = nullptr;
		if (ps)
			if (auto it = ps->players.find(neutral); it != ps->players.end())
				neutralObj = it->second;
		std::vector<std::string> materialWarnings;
		const json data = MapMaterials::LoadMaterials(m.textureDb, m.raw, materialWarnings);
		const int W = terrain.width, H = terrain.height, E = terrain.edge;
		const float pw = (W - 2 * E) * 5.0f, ph = (H - 2 * E) * 5.0f;
		const int ptw = W - 2 * E, pth = H - 2 * E;
		// taken places, on a 4 m grid
		std::set<std::pair<int, int>> taken;
		auto free = [&](float x, float z, float r) {
			const int c = (int)std::ceil(r / 4.0f);
			const int gx = (int)(x / 4), gz = (int)(z / 4);
			for (int dz = -c; dz <= c; dz++)
				for (int dx = -c; dx <= c; dx++)
					if (taken.count({ gx + dx, gz + dz })) return false;
			return true;
		};
		auto take = [&](float x, float z) { taken.insert({ (int)(x / 4), (int)(z / 4) }); };
		// the bases (the civic centres placed, else map.json's starts), kept clear
		std::vector<std::pair<float, float>> bases = basesAt;
		if (bases.empty())
			for (const MapPlayer& p : m.players)
				if (p.hasStart) bases.push_back({ p.x, p.z });
		// with the placement state: no object on a building's tile or beside it, none in a town (its
		// city rectangles and two tiles round them)
		std::vector<uint8_t> blockedTile;
		if (ps) {
			blockedTile.assign((size_t)ptw * pth, 0);
			for (int z = 0; z < pth; z++)
				for (int x = 0; x < ptw; x++)
					if (ps->tiles[(size_t)z * ptw + x].building.getFrom(ps))
						for (int dz = -1; dz <= 1; dz++)
							for (int dx = -1; dx <= 1; dx++)
								if (x + dx >= 0 && z + dz >= 0 && x + dx < ptw && z + dz < pth) blockedTile[(size_t)(z + dz) * ptw + x + dx] = 1;
			for (const auto& o : ps->owned)
				if (o->blueprint->bpClass == Tags::GAMEOBJCLASS_CITY)
					for (const auto& r : o->cityRectangles)
						for (int z = std::max(0, r.yStart - 2); z <= std::min(pth - 1, r.yEnd + 2); z++)
							for (int x = std::max(0, r.xStart - 2); x <= std::min(ptw - 1, r.xEnd + 2); x++)
								blockedTile[(size_t)z * ptw + x] = 1;
		}
		auto tileAt = [&](float x, float z) { return terrain.getTile((int)(x / 5) + E, (int)(z / 5) + E); };
		auto slopeDeg = [&](float x, float z) {
			const int tx = (int)(x / 5) + E, tz = (int)(z / 5) + E;
			const float a = terrain.getVertex(tx, tz), b = terrain.getVertex(tx + 1, tz), c = terrain.getVertex(tx, tz + 1), d = terrain.getVertex(tx + 1, tz + 1);
			return std::atan((std::max({ a, b, c, d }) - std::min({ a, b, c, d })) / 5.0f) * 57.2958f;
		};
		auto ok = [&](float x, float z, float maxSlope) {
			if (x < 2 || z < 2 || x > pw - 2 || z > ph - 2) return false;
			const TerrainTile* t = tileAt(x, z);
			if (!t || t->fullOfWater) return false;
			if (!blockedTile.empty() && blockedTile[(size_t)(int)(z / 5) * ptw + (int)(x / 5)]) return false;
			// not beside water either (a tree in the river's edge)
			for (int dz = -1; dz <= 1; dz++)
				for (int dx = -1; dx <= 1; dx++) {
					const TerrainTile* n = tileAt(x + dx * 5.0f, z + dz * 5.0f);
					if (n && n->fullOfWater) return false;
				}
			return slopeDeg(x, z) <= maxSlope;
		};
		// the moisture (MapMaterials::Moisture), made when a rule asks for it
		std::vector<float> moisture;
		auto moistureAt = [&](float x, float z) {
			if (moisture.empty())
				moisture = MapMaterials::Moisture(folder, m.raw, m.seed, terrain);
			const int vx = std::clamp((int)std::lround(x / 5) + E, 0, W), vz = std::clamp((int)std::lround(z / 5) + E, 0, H);
			return moisture[(size_t)vz * (W + 1) + vx];
		};
		std::vector<std::string> block;
		int made = 0;
		json counts = json::object(), nearBases = json::object(), moistureRep = json::object(), habitatRep = json::object();
		std::set<std::string> unknownBp;
		// the bases' own groups first (gold, stone, woodlots by every base), then the rest in their
		// order: a forest laid first had taken the ground a base's gold needed
		std::vector<size_t> order;
		auto byBases = [&](const json& r) {
			return r.is_object() && r.value("near", "") == "bases" && !r.contains("on") && !r.contains("moisture") && !r.contains("habitat") && !r.contains("densityMask");
		};
		for (size_t ri = 0; ri < rules.size(); ri++) if (byBases(rules[ri])) order.push_back(ri);
		for (size_t ri = 0; ri < rules.size(); ri++) if (!byBases(rules[ri])) order.push_back(ri);
		for (const size_t ri : order) {
			const json& r = rules[ri];
			if (!r.is_object()) continue;
			const std::string id = r.value("id", "scatter" + std::to_string(ri));
			const std::string rule = "scatter:" + id;
			if (auth.suppressed.count(rule)) continue;
			// the objects: [blueprint, [subtypes]]
			auto readObjects = [&](const char* key) {
				std::vector<std::pair<std::string, std::vector<std::string>>> list;
				for (const json& o : r.value(key, json::array())) {
					if (o.is_string()) list.push_back({ o.get<std::string>(), {} });
					else if (o.is_array() && !o.empty() && o[0].is_string()) {
						std::vector<std::string> sub;
						if (o.size() > 1 && o[1].is_array()) for (const json& st : o[1]) if (st.is_string()) sub.push_back(st.get<std::string>());
						list.push_back({ o[0].get<std::string>(), sub });
					}
				}
				return list;
			};
			const auto objects = readObjects("objects");
			if (objects.empty()) { warnings.push_back(rule + ": no objects"); continue; }
			const auto objectsHigh = readObjects("objectsHigh");
			const float highFrom = r.value("highFrom", 0.0f), highTo = r.value("highTo", 1.0f), highMin = r.value("highMin", 0.0f), highMax = r.value("highMax", 1.0f);
			// "highMask": a layer (layers\<name>.png) giving the share of objectsHigh instead of the
			// height (a real map's conifer share by wood, from a forest inventory)
			std::vector<float> highMask;
			if (r.contains("highMask") && r["highMask"].is_string()) {
				const std::string mk = r["highMask"].get<std::string>();
				if (!MapMaterials::ReadMaskLayer(folder / "layers" / (mk + ".png"), W + 1, H + 1, highMask))
					warnings.push_back(rule + ": no mask layers/" + mk + ".png (the height decides)");
			}
			// "densityMask": a layer (0-1) the density is multiplied by
			std::vector<float> densityMask;
			if (r.contains("densityMask") && r["densityMask"].is_string()) {
				const std::string mk = r["densityMask"].get<std::string>();
				if (!MapMaterials::ReadMaskLayer(folder / "layers" / (mk + ".png"), W + 1, H + 1, densityMask))
					warnings.push_back(rule + ": no mask layers/" + mk + ".png (left out)");
			}
			// "moisture": [from, to]: the density rises from nothing at `from` to full at `to` (a
			// forest by moisture; from > to: a dry-ground stand)
			const bool byMoisture = r.contains("moisture") && r["moisture"].is_array() && r["moisture"].size() == 2;
			const float mFrom = byMoisture ? r["moisture"][0].get<float>() : 0, mTo = byMoisture ? r["moisture"][1].get<float>() : 1;
			std::vector<uint32_t>& reuse = idsOf[rule];
			const uint32_t rs = ScatterHash(m.seed, (uint32_t)ri, 0x5ca7, 1);
			const float maxSlope = r.value("maxSlopeDeg", 32.0f), away = r.value("awayFromBases", 45.0f);
			// "habitat": a forest where the land suits trees, in patches, not by one field's contour
			// lines (the user's review of the first, moisture-only forests: a ring round the lake,
			// bands along the river, far too many trees; "there can be a forest where no big river
			// is just because the soil is good"; near rivers more likely but not guaranteed; the
			// highlands mostly bare). Per vertex a suitability: deep soil (a warped noise at
			// soilScaleM, thinner on steep and convex ground), a mild boost from the moisture, the
			// aspect (north-facing slopes keep their water, south-facing ones dry out), nothing on
			// waterlogged banks (waterGapM), a treeline on the high ground, clearings. The forest is
			// the best `cover` share of the land, shrunk to keep the trees within maxPerKm2 (700: about
			// 4 Peaks' own), so the woods stay dense rather than thinned
			const bool byHabitat = r.contains("habitat") && r["habitat"].is_object();
			std::vector<float> habitat, waterDistV;
			json habRep;
			int budget = std::numeric_limits<int>::max();
			int nearWaterTrees = 0, farTrees = 0;
			if (byHabitat) {
				const json& hb = r["habitat"];
				const float cover = hb.value("cover", 0.2f), soilScale = std::max(50.0f, hb.value("soilScaleM", 350.0f)), wMoist = hb.value("moisture", 0.2f),
					wAspect = hb.value("aspect", 0.12f), treeline = hb.value("treelineRel", 0.72f), waterGap = hb.value("waterGapM", 12.0f),
					clearings = hb.value("clearings", 0.15f), perKm2 = r.value("maxPerKm2", 700.0f), density = r.value("density", 20.0f);
				const int gw = W + 1, gh = H + 1;
				const size_t NV = (size_t)gw * gh;
				std::vector<float> hv(NV);
				std::vector<uint8_t> wet(NV, 0);
				for (int z = 0; z < gh; z++)
					for (int x = 0; x < gw; x++) hv[(size_t)z * gw + x] = terrain.getVertex(x, z);
				for (int z = 0; z < H; z++)
					for (int x = 0; x < W; x++)
						if (terrain.tiles[(size_t)z * W + x].fullOfWater)
							for (int k = 0; k < 4; k++) wet[(size_t)(z + (k >> 1)) * gw + x + (k & 1)] = 1;
				// the distance to water in metres (a 3-4 chamfer, 5 m a step)
				waterDistV.assign(NV, 1e9f);
				for (size_t i = 0; i < NV; i++) if (wet[i]) waterDistV[i] = 0;
				for (int pass = 0; pass < 2; pass++)
					for (int k = 0; k < gh; k++) {
						const int z = pass ? gh - 1 - k : k;
						for (int j = 0; j < gw; j++) {
							const int x = pass ? gw - 1 - j : j;
							float& d = waterDistV[(size_t)z * gw + x];
							const int s = pass ? 1 : -1;
							const int nb[4][2] = { { s, 0 }, { 0, s }, { s, s }, { -s, s } };
							for (const auto& o : nb) {
								const int nx = x + o[0], nz = z + o[1];
								if (nx < 0 || nz < 0 || nx >= gw || nz >= gh) continue;
								d = std::min(d, waterDistV[(size_t)nz * gw + nx] + ((o[0] && o[1]) ? 7.07f : 5.0f));
							}
						}
					}
				float lo = 1e30f, hi = -1e30f;
				for (int z = E; z <= H - E; z++)
					for (int x = E; x <= W - E; x++)
						if (!wet[(size_t)z * gw + x]) lo = std::min(lo, hv[(size_t)z * gw + x]), hi = std::max(hi, hv[(size_t)z * gw + x]);
				const float range = std::max(hi - lo, 1.0f);
				auto Hv = [&](int x, int z) { return hv[(size_t)std::clamp(z, 0, gh - 1) * gw + std::clamp(x, 0, gw - 1)]; };
				auto smooth = [](float a, float b, float v) { const float t = std::clamp((v - a) / (b - a), 0.0f, 1.0f); return t * t * (3 - 2 * t); };
				auto fbm = [&](float x, float z, uint32_t s) {
					return 0.55f * ScatterNoise(x, z, s) + 0.3f * ScatterNoise(x * 2.03f, z * 2.03f, s ^ 0x9e37u) + 0.15f * ScatterNoise(x * 4.1f, z * 4.1f, s ^ 0x7f4au);
				};
				// the soil's patches: a warped noise, ranked over the land so it runs evenly from 0
				// to 1 (a noise's own spread is narrow; ranked, the weights below mean what they say)
				std::vector<float> soilN(NV, 0.0f), soilSorted;
				for (int z = E; z <= H - E; z++)
					for (int x = E; x <= W - E; x++) {
						const size_t i = (size_t)z * gw + x;
						if (wet[i]) continue;
						const float px = (x - E) * 5.0f, pz = (z - E) * 5.0f;
						const float wx = (ScatterNoise(px / soilScale * 0.7f, pz / soilScale * 0.7f, rs ^ 0x51a1u) - 0.5f) * 1.6f;
						const float wz = (ScatterNoise(px / soilScale * 0.7f + 17.3f, pz / soilScale * 0.7f, rs ^ 0x51a2u) - 0.5f) * 1.6f;
						soilN[i] = fbm(px / soilScale + wx, pz / soilScale + wz, rs ^ 0x5011u);
						soilSorted.push_back(soilN[i]);
					}
				std::sort(soilSorted.begin(), soilSorted.end());
				auto soilRank = [&](float v) {
					return soilSorted.empty() ? 0.5f : (float)(std::lower_bound(soilSorted.begin(), soilSorted.end(), v) - soilSorted.begin()) / soilSorted.size();
				};
				std::vector<float> S(NV, -10.0f);
				std::vector<float> landS;
				for (int z = E; z <= H - E; z++)
					for (int x = E; x <= W - E; x++) {
						const size_t i = (size_t)z * gw + x;
						if (wet[i]) continue;
						const float px = (x - E) * 5.0f, pz = (z - E) * 5.0f;
						const float gx = (Hv(x + 1, z) - Hv(x - 1, z)) / 10.0f, gz = (Hv(x, z + 1) - Hv(x, z - 1)) / 10.0f;
						const float glen = std::sqrt(gx * gx + gz * gz), slope = std::atan(glen) * 57.2958f;
						float mean = 0;
						for (int oz = -2; oz <= 2; oz++) for (int ox = -2; ox <= 2; ox++) mean += Hv(x + ox, z + oz);
						const float curv = hv[i] - mean / 25.0f;
						// the soil: its patch (ranked), thinner on steep and convex ground
						float soil = soilRank(soilN[i]);
						soil *= 1.0f - smooth(12.0f, 35.0f, slope);
						soil -= 0.1f * std::clamp(curv / 3.0f, -1.0f, 1.0f);
						const float moist = moistureAt(px, pz);
						float s = soil + wMoist * std::clamp((moist - 0.35f) / 0.35f, 0.0f, 1.0f);
						// north (+z) facing: the land falls towards +z
						if (glen > 1e-3f) s += wAspect * std::clamp(-gz / glen, -1.0f, 1.0f) * smooth(3.0f, 15.0f, slope);
						s -= smooth(treeline, treeline + 0.15f, (hv[i] - lo) / range);
						s -= clearings * smooth(0.62f, 0.75f, fbm(px / 150.0f, pz / 150.0f, rs ^ 0xc1eau));
						if (waterDistV[i] < waterGap || moist > 0.95f) s = -10.0f; // waterlogged banks
						S[i] = s;
						landS.push_back(s);
					}
				const float landHa = landS.size() * 25.0f / 10000.0f;
				const float km2 = pw * ph / 1e6f;
				budget = (int)(perKm2 * km2);
				// the cover the budget allows at the rule's density (85 % of the cells fill)
				const float coverEff = std::min(cover, landHa > 0 ? budget / (density * 0.85f * landHa) : cover);
				std::vector<float> sorted = landS;
				std::sort(sorted.begin(), sorted.end());
				const float thr = sorted.empty() ? 0.0f : sorted[std::clamp((size_t)((1.0f - coverEff) * sorted.size()), (size_t)0, sorted.size() - 1)];
				habitat.assign(NV, 0.0f);
				int forested = 0;
				for (size_t i = 0; i < NV; i++) {
					habitat[i] = smooth(thr - 0.03f, thr + 0.03f, S[i]); // a soft edge, centred on the threshold
					forested += habitat[i] >= 0.5f;
				}
				// the patches (connected forest, 0.1 ha or more)
				std::vector<uint8_t> seen(NV, 0);
				int patches = 0, largest = 0;
				std::vector<size_t> stack;
				for (size_t i = 0; i < NV; i++) {
					if (seen[i] || habitat[i] < 0.5f) continue;
					int size = 0;
					stack.assign(1, i);
					seen[i] = 1;
					while (!stack.empty()) {
						const size_t c = stack.back();
						stack.pop_back();
						size++;
						const int cx = (int)(c % gw), cz = (int)(c / gw);
						const int n4[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
						for (const auto& o : n4) {
							const int nx = cx + o[0], nz = cz + o[1];
							if (nx < 0 || nz < 0 || nx >= gw || nz >= gh) continue;
							const size_t ni = (size_t)nz * gw + nx;
							if (!seen[ni] && habitat[ni] >= 0.5f) { seen[ni] = 1; stack.push_back(ni); }
						}
					}
					if (size >= 40) patches++;
					largest = std::max(largest, size);
				}
				habRep = { { "coverAsked", cover }, { "coverForBudget", std::lround(coverEff * 1000) / 1000.0 },
					{ "coverMade", landS.empty() ? 0.0 : std::lround(forested * 1000.0 / landS.size()) / 1000.0 }, { "budget", budget },
					{ "patches", patches }, { "largestPatchHa", std::lround(largest * 25 / 1000.0) / 10.0 } };
			}
			int n = 0;
			double moistSum = 0;
			auto place = [&](float x, float z, uint32_t h) {
				bool high = false;
				if (!objectsHigh.empty()) {
					float share, standAmp = 0.66f;
					if (!highMask.empty()) {
						const int vx = std::clamp((int)std::lround(x / 5) + E, 0, W), vz = std::clamp((int)std::lround(z / 5) + E, 0, H);
						share = highMask[(size_t)vz * (W + 1) + vx];
						standAmp = 0.3f; // the mask is the wood's own mix: stands within it only
					}
					else {
						const float y = terrain.getHeight(x, z);
						const float t = std::clamp((y - highFrom) / std::max(highTo - highFrom, 0.01f), 0.0f, 1.0f);
						share = highMin + (highMax - highMin) * t * t * (3 - 2 * t);
					}
					// in stands: a slow noise shifts the share by up to a third
					const float stand = (ScatterNoise(x / 45.0f, z / 45.0f, rs ^ 0x51u) - 0.5f) * standAmp;
					high = ScatterUnit(ScatterHash(h, 17, 17, 17)) < std::clamp(share + stand, 0.0f, 1.0f);
				}
				const auto& list = high ? objectsHigh : objects;
				const auto& [bp, sub] = list[h % list.size()];
				uint32_t oid;
				if (!reuse.empty()) { oid = reuse.front(); reuse.erase(reuse.begin()); }
				else oid = next++;
				const float yaw = ScatterUnit(ScatterHash(h, 3, 3, 3)) * 6.283f;
				const float y = terrain.getHeight(x, z);
				block.push_back("\t\tCONTAINER " + std::to_string(oid) + " \"" + bp + "\" \n");
				block.push_back("\t\t\tPOSITION " + Num(x) + " " + Num(y) + " " + Num(z) + " \n");
				block.push_back("\t\t\tORIENTATION 0.00 " + Num(yaw) + " 0.00 \n");
				if (!sub.empty())
					block.push_back("\t\t\tSUBTYPE \"" + sub[(h >> 12) % sub.size()] + "\" \n");
				block.push_back("\t\tEND_CONTAINER \n");
				block.push_back("\n");
				auth.objects[oid] = rule;
				auth.positions[oid] = { x, z };
				if (ps) {
					if (const GameObjBlueprint* b = ps->gameSet->findBlueprint(Tags::GAMEOBJCLASS_CONTAINER, bp))
						ps->add(oid, b, neutralObj, Vector3(x, y, z), Vector3(0, yaw, 0));
					else if (unknownBp.insert(bp).second)
						warnings.push_back(rule + ": the game set has no container \"" + bp + "\" (written as asked)");
				}
				if (byMoisture) moistSum += moistureAt(x, z);
				if (byHabitat) {
					const float wd = waterDistV[(size_t)std::clamp((int)std::lround(z / 5) + E, 0, H) * (W + 1) + std::clamp((int)std::lround(x / 5) + E, 0, W)];
					nearWaterTrees += wd <= 25.0f;
					farTrees += wd > 150.0f;
				}
				take(x, z);
				n++;
			};
			auto nearBase = [&](float x, float z) {
				for (auto& [bx, bz] : bases) if ((x - bx) * (x - bx) + (z - bz) * (z - bz) < away * away) return true;
				return false;
			};
			if (r.contains("on") || byMoisture || byHabitat || !densityMask.empty()) {
				// by ground: the groups of the materials (or the groups) named, a transition counting
				// half; without "on" any ground. The density times the moisture's ramp and the mask
				std::set<std::string> groups;
				for (const json& o : r.value("on", json::array())) {
					if (!o.is_string()) continue;
					const std::string name = o.get<std::string>();
					const json& mats = data["materials"];
					if (mats.contains(name) && mats[name].is_object()) {
						for (const json& gj : mats[name].value("groups", json::array()))
							if (gj.is_array() && !gj.empty() && gj[0].is_string()) groups.insert(gj[0].get<std::string>());
							else if (gj.is_string()) groups.insert(gj.get<std::string>());
					}
					else groups.insert(name);
				}
				const bool anyGround = !r.contains("on");
				const float density = r.value("density", 20.0f), spacing = std::max(2.0f, r.value("spacing", 6.0f)), clump = r.value("clump", 0.5f);
				const int limit = std::min(r.value("max", 30000), r.contains("maxPerKm2") || byHabitat ? (byHabitat ? budget : (int)(r["maxPerKm2"].get<float>() * pw * ph / 1e6f)) : 30000);
				// a jittered grid of cells of `spacing`; a cell's chance is density x its area. First
				// every place that would take one; over the limit, all of them thinned evenly (a cut in
				// the order of the grid had left one side of the map bare)
				const float chance = density * spacing * spacing / 10000.0f;
				struct Spot { float x, z; uint32_t h; };
				std::vector<Spot> spots;
				for (float z = spacing / 2; z < ph; z += spacing)
					for (float x = spacing / 2; x < pw; x += spacing) {
						const uint32_t h = ScatterHash(rs, (uint32_t)(x * 4), (uint32_t)(z * 4), 11);
						const float jx = x + (ScatterUnit(h) - 0.5f) * spacing * 0.8f, jz = z + (ScatterUnit(ScatterHash(h, 1, 2, 3)) - 0.5f) * spacing * 0.8f;
						const TerrainTile* t = tileAt(jx, jz);
						if (!t || !t->texture || !t->texture->grp) continue;
						float w = 1.0f;
						if (!anyGround) {
							const std::string& gname = t->texture->grp->name;
							w = groups.count(gname) ? 1.0f : 0.0f;
							if (w == 0 && gname.find('-') != std::string::npos)
								for (const std::string& g : groups)
									if (gname.find(g) != std::string::npos) { w = 0.5f; break; }
						}
						if (w == 0) continue;
						if (byMoisture) {
							const float span = std::abs(mTo - mFrom) > 1e-4f ? mTo - mFrom : 1e-4f;
							const float u = std::clamp((moistureAt(jx, jz) - mFrom) / span, 0.0f, 1.0f);
							w *= u * u * (3 - 2 * u);
							if (w <= 0) continue;
						}
						if (byHabitat) {
							const int vx = std::clamp((int)std::lround(jx / 5) + E, 0, W), vz = std::clamp((int)std::lround(jz / 5) + E, 0, H);
							w *= habitat[(size_t)vz * (W + 1) + vx];
							if (w <= 0) continue;
						}
						if (!densityMask.empty()) {
							const int vx = std::clamp((int)std::lround(jx / 5) + E, 0, W), vz = std::clamp((int)std::lround(jz / 5) + E, 0, H);
							w *= std::clamp(densityMask[(size_t)vz * (W + 1) + vx], 0.0f, 1.0f);
							if (w <= 0) continue;
						}
						// clumps: a slow noise thins the trees into stands and glades
						const float cl = 1.0f - clump + clump * 2.0f * ScatterNoise(jx / 60.0f, jz / 60.0f, rs);
						if (ScatterUnit(ScatterHash(h, 9, 9, 9)) >= chance * w * std::max(0.0f, cl)) continue;
						if (nearBase(jx, jz) || !ok(jx, jz, maxSlope)) continue;
						spots.push_back({ jx, jz, h });
					}
				const float keep = spots.size() > (size_t)limit ? (float)limit / spots.size() : 1.0f;
				for (const Spot& sp : spots) {
					if (n >= limit) break;
					if (keep < 1 && ScatterUnit(ScatterHash(sp.h, 41, 41, 41)) >= keep) continue;
					if (!free(sp.x, sp.z, spacing * 0.45f)) continue;
					place(sp.x, sp.z, sp.h);
				}
				if (byMoisture && n > 0) {
					// the land's mean moisture, for the report: the forest's against it
					double land = 0;
					int cnt = 0;
					for (float z = 10; z < ph; z += 20)
						for (float x = 10; x < pw; x += 20)
							if (const TerrainTile* t = tileAt(x, z); t && !t->fullOfWater) { land += moistureAt(x, z); cnt++; }
					moistureRep[id] = { { "underObjects", std::lround(moistSum / n * 1000) / 1000.0 }, { "land", cnt ? std::lround(land / cnt * 1000) / 1000.0 : 0.0 },
						{ "ramp", { mFrom, mTo } } };
				}
			}
			else {
				// groups of objects: near each base, or over the map
				const int perBase = r.value("groups", 2), count = r.value("count", 0), size = std::max(1, r.value("size", 3));
				const float spread = r.value("spread", 9.0f);
				json dist = r.value("distance", json::array({ 70, 160 }));
				const float dmin = dist.is_array() && dist.size() > 1 ? dist[0].get<float>() : 70, dmax = dist.is_array() && dist.size() > 1 ? dist[1].get<float>() : 160;
				uint32_t h = rs;
				// a group's places round (cx, cz), each free and on good ground (none placed yet)
				auto groupSpots = [&](float cx, float cz, uint32_t hs) {
					std::vector<std::pair<float, float>> out;
					for (int k = 0; k < size * 8 && (int)out.size() < size; k++) {
						hs = ScatterHash(hs, (uint32_t)k, 5, 5);
						const float a = ScatterUnit(hs) * 6.283f, d = ScatterUnit(ScatterHash(hs, 2, 2, 2)) * spread;
						const float x = cx + std::cos(a) * d, z = cz + std::sin(a) * d;
						if (!ok(x, z, maxSlope) || !free(x, z, 3.5f)) continue;
						bool clash = false;
						for (auto& [ox, oz] : out) clash |= (ox - x) * (ox - x) + (oz - z) * (oz - z) < 3.5f * 3.5f;
						if (!clash) out.push_back({ x, z });
					}
					return out;
				};
				auto groupAt = [&](float cx, float cz) {
					int placed = 0;
					for (int k = 0; k < size * 6 && placed < size; k++) {
						h = ScatterHash(h, (uint32_t)k, 5, 5);
						const float a = ScatterUnit(h) * 6.283f, d = ScatterUnit(ScatterHash(h, 2, 2, 2)) * spread;
						const float x = cx + std::cos(a) * d, z = cz + std::sin(a) * d;
						if (!ok(x, z, maxSlope) || !free(x, z, 3.5f)) continue;
						place(x, z, h);
						placed++;
					}
					return placed;
				};
				if (r.value("near", "") == "bases") {
					// fair (the default): every base gets its groups at the same distances (the k-th
					// group at the k-th share of the distance band), each of `size` objects, nearer to
					// its own base than to any other; only the side differs
					const bool fair = r.value("fair", true);
					json per = json::array();
					for (size_t b = 0; b < bases.size(); b++) per.push_back(0);
					for (int gI = 0; gI < perBase; gI++)
						for (size_t b = 0; b < bases.size(); b++) {
							const float dk = dmin + (gI + 0.5f) / std::max(perBase, 1) * (dmax - dmin);
							const float a0 = ScatterUnit(ScatterHash(rs, (uint32_t)b, (uint32_t)gI, 21)) * 6.283f;
							bool done = false;
							for (int tries = 0; tries < 216 && !done; tries++) {
								const int ring = tries / 72;
								const float a = a0 + (tries % 72) * (6.283f / 72);
								float d;
								if (!fair) d = dmin + ScatterUnit(ScatterHash(rs, (uint32_t)b, (uint32_t)tries, 23)) * (dmax - dmin);
								else d = ring == 0 ? dk : ring == 1 ? dk * ((tries & 1) ? 1.12f : 0.88f) : dmin + ScatterUnit(ScatterHash(rs, (uint32_t)b, (uint32_t)tries, 23)) * (dmax - dmin);
								const float cx = bases[b].first + std::cos(a) * d, cz = bases[b].second + std::sin(a) * d;
								if (!ok(cx, cz, maxSlope) || !free(cx, cz, spread + 6)) continue;
								bool nearer = true;
								for (size_t o = 0; o < bases.size(); o++)
									if (o != b && std::hypot(cx - bases[o].first, cz - bases[o].second) < d + spread) nearer = false;
								if (!nearer) continue;
								const auto spots = groupSpots(cx, cz, ScatterHash(rs, (uint32_t)b, (uint32_t)(gI * 1000 + tries), 27));
								if (spots.empty() || (fair && (int)spots.size() < size)) continue;
								for (size_t k = 0; k < spots.size(); k++)
									place(spots[k].first, spots[k].second, ScatterHash(rs, (uint32_t)(b * 64 + gI), (uint32_t)k, 29));
								per[b] = per[b].get<int>() + (int)spots.size();
								done = true;
							}
							if (!done)
								warnings.push_back(rule + ": base " + std::to_string(b) + " has no room for group " + std::to_string(gI + 1) + " (" + Num(dk) + " m out)");
						}
					nearBases[id] = per;
				}
				for (int gI = 0, tries = 0; gI < count && tries < count * 60; tries++) {
					h = ScatterHash(rs, (uint32_t)tries, 31, 31);
					const float cx = 30 + ScatterUnit(h) * (pw - 60), cz = 30 + ScatterUnit(ScatterHash(h, 6, 6, 6)) * (ph - 60);
					if (nearBase(cx, cz) || !ok(cx, cz, maxSlope) || !free(cx, cz, spread + 30)) continue;
					if (groupAt(cx, cz) > 0) gI++;
				}
			}
			if (byHabitat) {
				// the trees by the water against the land's share there (a forest isn't a riverside line)
				int landNear = 0, landAll = 0, landFar = 0;
				for (int z = E; z <= H - E; z++)
					for (int x = E; x <= W - E; x++) {
						const float wd = waterDistV[(size_t)z * (W + 1) + x];
						if (wd <= 0) continue;
						landAll++;
						landNear += wd <= 25.0f;
						landFar += wd > 150.0f;
					}
				habRep["trees"] = n;
				habRep["treesWithin25mOfWater"] = n ? std::lround(nearWaterTrees * 1000.0 / n) / 1000.0 : 0.0;
				habRep["landWithin25mOfWater"] = landAll ? std::lround(landNear * 1000.0 / landAll) / 1000.0 : 0.0;
				habRep["treesBeyond150mOfWater"] = n ? std::lround(farTrees * 1000.0 / n) / 1000.0 : 0.0;
				habRep["landBeyond150mOfWater"] = landAll ? std::lround(landFar * 1000.0 / landAll) / 1000.0 : 0.0;
				habitatRep[id] = habRep;
			}
			counts[id] = n;
			made += n;
		}
		lines.insert(lines.begin() + end, block.begin(), block.end());
		report["scatter"] = counts;
		if (!nearBases.empty()) report["scatterNearBases"] = nearBases;
		if (!moistureRep.empty()) report["scatterMoisture"] = moistureRep;
		if (!habitatRep.empty()) report["scatterHabitat"] = habitatRep;
		return made;
	}

	// The bases by intent (MapObjects.h): the starts (map.json's, the rest chosen), per player its
	// civic centre on the nearest site the server's rules accept near the start (facing the map's
	// middle), the base's buildings round it (each on a site the rules accept, civic ones in its
	// city), the peasants before its door, the start camera on it
	int PlaceBases(const MapJson& m, Terrain& terrain, std::vector<std::string>& lines, Authoring& auth,
		std::map<std::string, std::vector<uint32_t>>& idsOf, uint32_t& next, std::vector<std::string>& warnings, json& report,
		MapObjects::PlacementState& ps, std::vector<std::pair<float, float>>& basesAt)
	{
		const json bj = m.raw.value("bases", json::object());
		const GameSet& gs = *ps.gameSet;
		const auto [aw, ad] = terrain.getPlayableArea();
		const auto [ptw, pth] = terrain.getNumPlayableTiles();
		json rep = json::object(), list = json::array();
		const size_t np = m.players.size();
		std::vector<float> sx(np), sz(np);
		std::vector<bool> has(np, false);
		std::vector<std::pair<float, float>> fixed;
		std::vector<size_t> want;
		for (size_t i = 0; i < np; i++) {
			const MapPlayer& p = m.players[i];
			if (p.hasStart) {
				sx[i] = p.x, sz[i] = p.z, has[i] = true;
				fixed.push_back({ p.x, p.z });
			}
			else if (p.autoStart || bj.value("auto", false))
				want.push_back(i);
		}
		auto playerObj = [&](size_t i) -> CommonGameObject* {
			if (i >= auth.players.size()) return nullptr;
			auto it = ps.players.find(auth.players[i]);
			return it == ps.players.end() ? nullptr : it->second;
		};
		if (!want.empty()) {
			const GameObjBlueprint* c = gs.findBlueprint(Tags::GAMEOBJCLASS_BUILDING, m.players[want[0]].base);
			json sr;
			const auto chosen = MapObjects::ChooseStarts(ps, playerObj(want[0]), c, fixed, (int)want.size(), bj, sr, warnings);
			json cj = json::array();
			for (size_t k = 0; k < chosen.size(); k++) {
				const size_t i = want[k];
				sx[i] = chosen[k].first, sz[i] = chosen[k].second, has[i] = true;
				cj.push_back({ { "player", i }, { "start", { chosen[k].first, chosen[k].second } } });
			}
			sr["chosen"] = cj;
			rep["autoStarts"] = sr;
		}
		const float searchM = bj.value("searchM", 40.0f);
		int made = 0;
		for (size_t i = 0; i < np && i < auth.players.size(); i++) {
			const MapPlayer& p = m.players[i];
			const std::string rule = "base:" + std::to_string(i);
			if (!has[i] || p.base.empty() || auth.suppressed.count(rule))
				continue;
			CommonGameObject* player = playerObj(i);
			const size_t end = PlayerEnd(lines, auth.players[i]);
			if (!player || end == std::string::npos) {
				warnings.push_back(rule + ": player " + std::to_string(auth.players[i]) + " isn't in the level");
				continue;
			}
			const float px = sx[i], pz = sz[i];
			if (px < 0 || pz < 0 || px > aw || pz > ad) {
				warnings.push_back(rule + ": the start (" + Num(px) + ", " + Num(pz) + ") is off the map");
				continue;
			}
			if (const TerrainTile* t = terrain.getTile((int)(px / 5) + terrain.edge, (int)(pz / 5) + terrain.edge); t && t->fullOfWater)
				warnings.push_back(rule + ": the start (" + Num(px) + ", " + Num(pz) + ") is under water");
			const GameObjBlueprint* cbp = gs.findBlueprint(Tags::GAMEOBJCLASS_BUILDING, p.base);
			if (!cbp) {
				warnings.push_back(rule + ": the game set has no building \"" + p.base + "\"");
				continue;
			}
			std::vector<uint32_t>& reuse = idsOf[rule];
			const std::function<uint32_t()> takeId = [&]() {
				if (!reuse.empty()) {
					const uint32_t id = reuse.front();
					reuse.erase(reuse.begin());
					return id;
				}
				return next++;
			};
			std::vector<CommonGameObject*> madeObjs;
			MapObjects::SiteQuery q;
			q.x = px, q.z = pz, q.rMax = searchM, q.faceX = aw / 2, q.faceZ = ad / 2;
			MapObjects::Site site = MapObjects::FindSite(ps, player, cbp, q);
			json pj = { { "player", i }, { "start", { px, pz } } };
			if (!site.found) {
				warnings.push_back(rule + ": no site for the " + p.base + " within " + Num(searchM) + " m of the start (" + Num(px) + ", " + Num(pz)
					+ ") passes the server's rules (" + MapObjects::StatusName(site.firstRefusal) + "); put at the start, the placement check reports it");
				site.ori = Vector3(0, 3.1415927f, 0);
				site.pos = Vector3(px, terrain.getHeight(px, pz), pz);
				site.plan = StampdownPlan::getStampdownPlan(&ps, player, cbp, site.pos, site.ori);
			}
			CommonGameObject* centre = MapObjects::PutBuilding(ps, player, cbp, site, takeId, madeObjs);
			const float cx = centre->position.x, cz = centre->position.z;
			pj["centre"] = { { "blueprint", p.base }, { "at", { cx, cz } }, { "movedM", std::lround(std::hypot(cx - px, cz - pz) * 10) / 10.0 },
				{ "quarter", (int)std::lround(centre->orientation.y / 1.5707964f) % 4 }, { "passed", site.found } };
			// the door's side (the model's +z turned: (-sin y, cos y)) and the half size
			const float dx = -std::sin(centre->orientation.y), dz = std::cos(centre->orientation.y);
			float half = 7.5f;
			if (cbp->footprint && !cbp->footprint->tiles.empty()) {
				int x0 = 1 << 20, x1 = -(1 << 20), z0 = 1 << 20, z1 = -(1 << 20);
				for (const auto& to : cbp->footprint->tiles)
					x0 = std::min(x0, to.offsetX), x1 = std::max(x1, to.offsetX), z0 = std::min(z0, to.offsetZ), z1 = std::max(z1, to.offsetZ);
				half = std::max(x1 - x0 + 1, z1 - z0 + 1) * 2.5f;
			}
			// the peasants' place before the door: kept free of the base's buildings
			std::vector<uint8_t> keep((size_t)ptw * pth, 0);
			for (float along = half; along <= half + 18; along += 2.5f)
				for (float across = -9; across <= 9; across += 2.5f) {
					const float x = cx + dx * along + dz * across, z = cz + dz * along - dx * across;
					const int tx = (int)(x / 5), tz = (int)(z / 5);
					if (x >= 0 && z >= 0 && tx < ptw && tz < pth) keep[(size_t)tz * ptw + tx] = 1;
				}
			// the base's buildings
			const json intents = p.buildings.is_array() ? p.buildings : bj.value("buildings", json::array());
			json placedJ = json::array();
			int asked = 0, placedN = 0;
			for (const json& in : intents) {
				std::string name;
				int count = 1;
				float d0 = 12, d1 = 45, angle = 1e30f;
				if (in.is_string()) name = in.get<std::string>();
				else if (in.is_object()) {
					name = in.value("blueprint", "");
					count = in.value("count", 1);
					if (in.contains("distance") && in["distance"].is_array() && in["distance"].size() == 2)
						d0 = in["distance"][0].get<float>(), d1 = in["distance"][1].get<float>();
					if (in.contains("toward")) {
						const json& tw = in["toward"];
						if (tw.is_number()) angle = tw.get<float>() * 3.14159265f / 180.0f;
						else if (tw.is_string() && (tw.get<std::string>() == "centre" || tw.get<std::string>() == "away")) {
							angle = std::atan2(ad / 2 - cz, aw / 2 - cx);
							if (tw.get<std::string>() == "away") angle += 3.14159265f;
						}
					}
				}
				const GameObjBlueprint* bp = gs.findBlueprint(Tags::GAMEOBJCLASS_BUILDING, name);
				if (!bp) {
					warnings.push_back(rule + ": the game set has no building \"" + name + "\"");
					continue;
				}
				for (int c = 0; c < count; c++) {
					asked++;
					MapObjects::SiteQuery q2;
					q2.x = cx, q2.z = cz, q2.rMin = d0, q2.rMax = d1, q2.prefDist = d0, q2.prefAngle = angle;
					q2.faceX = cx, q2.faceZ = cz, q2.keepFree = &keep;
					const MapObjects::Site s2 = MapObjects::FindSite(ps, player, bp, q2);
					if (!s2.found) {
						warnings.push_back(rule + ": no site for " + name + " " + Num(d0) + "-" + Num(d1) + " m from the " + p.base
							+ " passes the server's rules (the first refusal: " + MapObjects::StatusName(s2.firstRefusal) + ", " + std::to_string(s2.tried) + " tried)");
						continue;
					}
					const CommonGameObject* b = MapObjects::PutBuilding(ps, player, bp, s2, takeId, madeObjs);
					placedJ.push_back({ { "blueprint", name }, { "id", b->id }, { "at", { b->position.x, b->position.z } },
						{ "fromCentreM", std::lround(std::hypot(b->position.x - cx, b->position.z - cz)) } });
					placedN++;
				}
			}
			// the peasants, in rows before the door, each on the nearest free walkable spot
			int peasantsPlaced = 0;
			if (const GameObjBlueprint* peasant = gs.findBlueprint(Tags::GAMEOBJCLASS_CHARACTER, "Peasant")) {
				std::vector<std::pair<float, float>> stood;
				auto freeSpot = [&](float x, float z) {
					const int tx = (int)(x / 5), tz = (int)(z / 5);
					if (x < 2 || z < 2 || tx >= ptw || tz >= pth) return false;
					if (ps.tiles[(size_t)tz * ptw + tx].building.getFrom(&ps)) return false;
					const TerrainTile* t = terrain.getTile(tx + terrain.edge, tz + terrain.edge);
					if (!t || t->fullOfWater) return false;
					const float a = terrain.getVertex(tx + terrain.edge, tz + terrain.edge), b = terrain.getVertex(tx + terrain.edge + 1, tz + terrain.edge),
						c = terrain.getVertex(tx + terrain.edge, tz + terrain.edge + 1), d = terrain.getVertex(tx + terrain.edge + 1, tz + terrain.edge + 1);
					if (std::max({ a, b, c, d }) - std::min({ a, b, c, d }) > 4.0f) return false;
					for (auto& [ox, oz] : stood)
						if ((ox - x) * (ox - x) + (oz - z) * (oz - z) < 2.0f * 2.0f) return false;
					return true;
				};
				for (int k = 0; k < p.peasants; k++) {
					const float along = half + 6.0f + 4.0f * (k / 4), across = -6.0f + 4.0f * (k % 4);
					const float tx = cx + dx * along + dz * across, tz = cz + dz * along - dx * across;
					bool done = false;
					for (int r = 0; r <= 24 && !done; r++)
						for (int a = 0; a < std::max(1, r * 6) && !done; a++) {
							const float an = a * 6.2831853f / std::max(1, r * 6);
							const float x = tx + std::cos(an) * r, z = tz + std::sin(an) * r;
							if (!freeSpot(x, z)) continue;
							madeObjs.push_back(ps.add(takeId(), peasant, player, Vector3(x, terrain.getHeight(x, z), z), centre->orientation));
							stood.push_back({ x, z });
							peasantsPlaced++;
							done = true;
						}
					if (!done)
						warnings.push_back(rule + ": no free ground for peasant " + std::to_string(k + 1) + " before the " + p.base);
				}
			}
			else if (p.peasants > 0)
				warnings.push_back(rule + ": the game set has no character \"Peasant\"");
			// the world's lines: what stands under the player (a city with what is in it)
			std::vector<std::string> block;
			for (CommonGameObject* o : madeObjs)
				if (o->parent == player)
					MapObjects::ObjectText(o, 2, block);
			for (CommonGameObject* o : madeObjs) {
				auth.objects[o->id] = rule;
				auth.positions[o->id] = { o->position.x, o->position.z };
				made++;
			}
			lines.insert(lines.begin() + end, block.begin(), block.end());
			SetStartCamera(lines, auth.players[i], Vector3(cx, terrain.getHeight(cx, cz) + 45.0f, cz + 70.0f), -0.67f, 3.15f);
			basesAt.push_back({ cx, cz });
			pj["buildings"] = placedJ;
			pj["asked"] = asked;
			pj["placed"] = placedN;
			pj["peasants"] = peasantsPlaced;
			list.push_back(pj);
		}
		rep["players"] = list;
		report["bases"] = rep;
		return made;
	}

	// the MAP line naming the folder
	bool BuildLevel(const fs::path& folder, const MapJson& m, Terrain& terrain, std::string& out, std::vector<std::string>& warnings,
		json& report, std::string& error)
	{
		std::string world;
		Authoring auth;
		std::string source;
		const std::string mapPath = GamePathOf(folder);
		bool changed = false;
		if (ReadAll(folder / "level.txt", source)) {
			if (SaveFile::IsSaveFile(source)) {
				SaveFile file;
				std::string err;
				if (!file.read(source, err))
					warnings.push_back("level.txt: " + err);
				if (const SaveSection* s = file.find("world"))
					world = s->text;
				if (const SaveSection* s = file.find("authoring"))
					auth = ParseAuthoring(s->text);
			}
			else
				world = source;
			if (world.empty()) {
				error = "level.txt has no world section";
				return false;
			}
		}
		else {
			world = SkeletonWorld(m, mapPath, auth.players);
			changed = true;
		}
		std::vector<std::string> lines = SplitLines(world);
		if (!SetMapLine(lines, mapPath))
			warnings.push_back("level.txt has no MAP line");
		// map.json's players: by index, their ids (as recorded, else the Human Players in id order)
		if (auth.players.size() < m.players.size()) {
			std::vector<uint32_t> humans;
			for (const std::string& l : lines) {
				std::string cls, bp;
				uint32_t id;
				if (ObjectLine(l, cls, id, bp) && cls == "PLAYER" && bp == "Human Player")
					humans.push_back(id);
			}
			std::sort(humans.begin(), humans.end());
			for (size_t i = auth.players.size(); i < m.players.size() && i < humans.size(); i++)
				auth.players.push_back(humans[i]);
		}
		// the rules' objects: removed (each rule makes its own again, with the same ids)
		std::map<std::string, std::vector<uint32_t>> idsOf;
		for (const auto& [id, rule] : auth.objects)
			idsOf[rule].push_back(id);
		for (const auto& [id, rule] : auth.objects)
			if (RemoveObject(lines, id))
				changed = true;
		auth.objects.clear();
		auth.positions.clear();
		uint32_t next = NextUniqueId(lines);
		int made = 0;
		// The objects go through the server's own placement rules on a light game state
		// (MapObjects.h): the game set, this terrain, the level's objects as they are
		std::unique_ptr<MapObjects::PlacementState> ps;
		if (!m.players.empty() || m.raw.contains("scatter")) {
			if (auto gs = MapObjects::LoadGameSet(m.gameSet))
				ps = std::make_unique<MapObjects::PlacementState>(gs, &terrain), ps->readWorld(lines);
			else
				warnings.push_back("objects: the game set \"" + m.gameSet + "\" can't be loaded: bases placed without the server's rules, no placement checks");
		}
		std::vector<std::pair<float, float>> basesAt;
		if (ps) {
			made += PlaceBases(m, terrain, lines, auth, idsOf, next, warnings, report, *ps, basesAt);
			changed = true;
		}
		else
		// a base per player (map.json "players": [{ "start": [x, z], "base": "Manor", "peasants": 4 }]):
		// its Manor at the start, peasants in front of it, the start camera looking at it
		for (size_t i = 0; i < m.players.size() && i < auth.players.size(); i++) {
			const MapPlayer& p = m.players[i];
			const std::string rule = "base:" + std::to_string(i);
			if (!p.hasStart || p.base.empty() || auth.suppressed.count(rule))
				continue;
			const size_t end = PlayerEnd(lines, auth.players[i]);
			if (end == std::string::npos) {
				warnings.push_back(rule + ": player " + std::to_string(auth.players[i]) + " isn't in the level");
				continue;
			}
			const auto [aw, ad] = terrain.getPlayableArea();
			if (p.x < 0 || p.z < 0 || p.x > aw || p.z > ad) {
				warnings.push_back(rule + ": the start (" + Num(p.x) + ", " + Num(p.z) + ") is off the map");
				continue;
			}
			std::vector<uint32_t>& reuse = idsOf[rule];
			auto takeId = [&]() {
				if (!reuse.empty()) {
					const uint32_t id = reuse.front();
					reuse.erase(reuse.begin());
					return id;
				}
				return next++;
			};
			const int tx = (int)(p.x / 5) + terrain.edge, tz = (int)(p.z / 5) + terrain.edge;
			if (const TerrainTile* t = terrain.getTile(tx, tz); t && t->fullOfWater)
				warnings.push_back(rule + ": the start (" + Num(p.x) + ", " + Num(p.z) + ") is under water");
			std::vector<std::string> block;
			auto object = [&](const char* cls, const std::string& bp, float x, float z, float yaw) {
				const uint32_t id = takeId();
				block.push_back("\t\t" + std::string(cls) + " " + std::to_string(id) + " \"" + bp + "\" \n");
				block.push_back("\t\t\tPOSITION " + Num(x) + " " + Num(terrain.getHeight(x, z)) + " " + Num(z) + " \n");
				block.push_back("\t\t\tORIENTATION 0.00 " + Num(yaw) + " 0.00 \n");
				block.push_back("\t\tEND_" + std::string(cls) + " \n");
				block.push_back("\n");
				auth.objects[id] = rule;
				auth.positions[id] = { x, z };
				made++;
			};
			object("BUILDING", p.base, p.x, p.z, 3.14f);
			for (int k = 0; k < p.peasants; k++)
				object("CHARACTER", "Peasant", p.x - 6.0f + 4.0f * (k % 4), p.z + 18.0f + 4.0f * (k / 4), 3.14f);
			lines.insert(lines.begin() + end, block.begin(), block.end());
			SetStartCamera(lines, auth.players[i], Vector3(p.x, terrain.getHeight(p.x, p.z) + 45.0f, p.z + 70.0f), -0.67f, 3.15f);
			changed = true;
		}
		made += Scatter(folder, m, terrain, lines, auth, idsOf, next, warnings, report, ps.get(), basesAt);
		// the placement checks (every building by the server's rules, as it stands) and the bases'
		// fairness
		if (ps) {
			report["placement"] = MapObjects::CheckPlacement(ps.get(), &warnings);
			report["fairness"] = MapObjects::Fairness(ps.get(), m.raw.value("bases", json::object()).value("fairnessRadiusM", 200.0f), &warnings);
		}
		changed = true;
		if (changed)
			RebuildPredec(lines);
		report["objectsByRules"] = made;
		std::string text;
		for (const std::string& l : lines)
			text += l;
		SaveFile file;
		file.add("world", 1, text);
		file.add("authoring", 1, AuthoringText(auth));
		out = file.write();
		return true;
	}

	// The terrain from the folder (heights + hand, the rules' tiles + hand tiles, the lakes, the
	// design); false with an error when the folder can't be read
	bool BuildTerrain(const fs::path& folder, const MapJson& m, Terrain& t, std::vector<uint8_t>& minimapRGB, std::vector<std::string>& warnings,
		json& report, std::string& error, std::map<int, HandTile>* ruleTilesOut = nullptr)
	{
		t.width = m.width;
		t.height = m.height;
		t.edge = m.edge;
		t.scale = m.scale;
		t.sunColor = m.sunColour;
		t.sunVector = m.sunVector;
		t.fogColor = m.fogColour;
		// with the NUL the original's BCMs end their strings with
		t.texDbPath = std::wstring(m.textureDb.begin(), m.textureDb.end()) + L'\0';
		t.skyTextureDirectory = std::wstring(m.sky.begin(), m.sky.end()) + L'\0';
		if (!FileExists(m.textureDb.c_str())) {
			error = "the texture database " + m.textureDb + " isn't there";
			return false;
		}
		t.texDb.load(m.textureDb.c_str());
		if (!m.translation.empty())
			t.texDb.translate(m.translation.c_str());
		std::vector<uint16_t> base;
		if (!ReadBaseHeights(folder, m, base, warnings, error))
			return false;
		std::vector<int> hand;
		ReadHandHeights(folder, m, hand, warnings);
		const size_t nv = (size_t)(m.width + 1) * (m.height + 1);
		t.vertices.resize(nv);
		t.heightFraction.assign(nv, 0);
		int handVertices = 0;
		bool anyFraction = false;
		for (size_t i = 0; i < nv; i++) {
			int v = base[i];
			if (!hand.empty() && hand[i]) {
				v += hand[i];
				handVertices++;
			}
			v = std::clamp(v, 0, 65535);
			t.vertices[i] = (uint8_t)(v >> 8);
			t.heightFraction[i] = (uint8_t)(v & 255);
			anyFraction |= (v & 255) != 0;
		}
		if (!anyFraction)
			t.heightFraction.clear();
		report["handHeightVertices"] = handVertices;
		// tiles
		std::map<int, HandTile> handTiles;
		ReadHandTiles(folder, m, handTiles, warnings);
		t.tiles = new TerrainTile[(size_t)m.width * m.height];
		TerrainTexture* fallback = nullptr;
		for (const auto& [name, g] : t.texDb.groups)
			if (g && !g->textures.empty()) {
				fallback = g->textures.begin()->second.get();
				break;
			}
		if (!fallback) {
			error = "the texture database " + m.textureDb + " has no textures";
			return false;
		}
		if (!m.fill.empty() && !t.texDb.groups.count(m.fill))
			warnings.push_back("ground.fill: no texture group " + m.fill + " in " + m.textureDb);
		int byRule = 0, byHand = 0, unset = 0;
		std::vector<uint8_t> isHand((size_t)m.width * m.height, 0);
		const bool byMaterials = MapMaterials::HasRules(m.raw);
		for (int z = 0; z < m.height; z++)
			for (int x = 0; x < m.width; x++) {
				TerrainTile& tile = t.tiles[z * m.width + x];
				tile.x = x;
				tile.z = z;
				tile.rot = tile.xflip = tile.zflip = 0;
				int rot = 0;
				TerrainTexture* tex = nullptr;
				if (auto it = handTiles.find(z * m.width + x); it != handTiles.end()) {
					const HandTile& h = it->second;
					auto g = t.texDb.groups.find(h.group);
					if (g != t.texDb.groups.end() && g->second)
						if (auto tt = g->second->textures.find(h.id); tt != g->second->textures.end())
							tex = tt->second.get();
					if (tex) {
						tile.rot = h.rot;
						tile.xflip = h.xflip;
						tile.zflip = h.zflip;
						byHand++;
						isHand[(size_t)z * m.width + x] = 1;
					}
					else
						warnings.push_back("hand tile (" + std::to_string(x) + ", " + std::to_string(z) + "): no texture " + h.group + " " + std::to_string(h.id));
				}
				if (!tex) {
					tex = RuleTile(m, t.texDb, x, z, rot);
					if (tex) {
						tile.rot = rot;
						byRule++;
					}
				}
				if (!tex) {
					tex = fallback;
					unset++;
				}
				tile.texture = tex;
			}
		report["tilesByHand"] = byHand;
		report["tilesByRule"] = byRule;
		if (unset && !byMaterials)
			warnings.push_back(std::to_string(unset) + " tiles have no hand tile and no material (ground.fill): " + fallback->grp->name + " " + std::to_string(fallback->id));
		// water
		std::shared_ptr<WaterDesign> design;
		ReportProgress("the water", 0.01f);
		ReadWater(folder, m, t.lakes, t.lakeExtraBits, design, warnings);
		t.waterDesign = design;
		t.floodfillWater();
		int flooded = 0;
		for (int i = 0; i < m.width * m.height; i++)
			flooded += t.tiles[i].fullOfWater;
		report["lakes"] = t.lakes.size();
		report["tilesUnderWater"] = flooded;
		for (size_t i = 0; i < t.lakes.size(); i++) {
			const int sx = (int)(t.lakes[i].x / 5), sz = (int)(t.lakes[i].z / 5);
			const TerrainTile* seed = t.getTile(sx, sz);
			if (!seed)
				warnings.push_back("lake " + std::to_string(i) + " is off the map");
			else if (!seed->fullOfWater)
				warnings.push_back("lake " + std::to_string(i) + " at (" + std::to_string(sx) + ", " + std::to_string(sz) + ") floods nothing: the ground there is above its level");
		}
		// the water by intent (MapWater.h): leaks, puddles, rising and dry rivers, the falls drawn
		report["water"] = MapWater::Check(t, MapWater::SeaLevelOf(m.raw), warnings);
		// the ground by rule (MapMaterials.h): materials per vertex, then the edge-rule solver; the
		// hand tiles stay
		if (byMaterials) {
			ReportProgress("the ground", 0.02f);
			std::vector<uint8_t> ruleTile;
			if (!MapMaterials::Paint(folder, m.raw, m.seed, t, isHand, report, warnings, ruleTile, error))
				return false;
			int n = 0;
			for (size_t i = 0; i < ruleTile.size(); i++)
				if (ruleTile[i]) {
					n++;
					if (ruleTilesOut) {
						const TerrainTile& tile = t.tiles[i];
						(*ruleTilesOut)[(int)i] = HandTile{ tile.texture->grp->name, tile.texture->id, (int)tile.rot, (int)tile.xflip, (int)tile.zflip };
					}
				}
			report["tilesByRule"] = n;
		}
		if (!ReadMinimap(folder, minimapRGB))
			minimapRGB = t.drawMinimap();
		return true;
	}

	fs::path TempSave(const fs::path& folder)
	{
		return folder / "build" / "editor-level.tmp";
	}
}

// ---------------------------------------------------------------------------------------------

bool WritePng16(const std::filesystem::path& path, int width, int height, const std::vector<uint16_t>& pixels)
{
	// the rows, each after its filter byte (0), big-endian samples, then a zlib stream
	std::vector<uint8_t> raw;
	raw.reserve((size_t)height * (1 + width * 2));
	for (int y = 0; y < height; y++) {
		raw.push_back(0);
		for (int x = 0; x < width; x++) {
			const uint16_t v = pixels[(size_t)y * width + x];
			raw.push_back((uint8_t)(v >> 8));
			raw.push_back((uint8_t)(v & 255));
		}
	}
	int zlen = 0;
	unsigned char* z = stbi_zlib_compress(raw.data(), (int)raw.size(), &zlen, 8);
	if (!z)
		return false;
	static uint32_t crcTable[256];
	static bool tableMade = false;
	if (!tableMade) {
		for (uint32_t n = 0; n < 256; n++) {
			uint32_t c = n;
			for (int k = 0; k < 8; k++)
				c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
			crcTable[n] = c;
		}
		tableMade = true;
	}
	std::string out("\x89PNG\r\n\x1a\n", 8);
	auto be32 = [](std::string& s, uint32_t v) { for (int i = 3; i >= 0; i--) s += (char)((v >> (i * 8)) & 255); };
	auto chunk = [&](const char* tag, const uint8_t* data, size_t n) {
		be32(out, (uint32_t)n);
		std::string body(tag, 4);
		body.append((const char*)data, n);
		uint32_t c = 0xFFFFFFFFu;
		for (unsigned char ch : body)
			c = crcTable[(c ^ ch) & 255] ^ (c >> 8);
		out += body;
		be32(out, c ^ 0xFFFFFFFFu);
	};
	std::string ihdr;
	be32(ihdr, (uint32_t)width);
	be32(ihdr, (uint32_t)height);
	ihdr += (char)16; // bit depth
	ihdr += (char)0;  // greyscale
	ihdr += (char)0; ihdr += (char)0; ihdr += (char)0;
	chunk("IHDR", (const uint8_t*)ihdr.data(), ihdr.size());
	chunk("IDAT", z, (size_t)zlen);
	STBIW_FREE(z);
	chunk("IEND", nullptr, 0);
	return WriteAll(path, out);
}

bool ReadPng16(const std::filesystem::path& path, int& width, int& height, std::vector<uint16_t>& pixels)
{
	std::string data;
	if (!ReadAll(path, data))
		return false;
	int n = 0;
	if (stbi_is_16_bit_from_memory((const stbi_uc*)data.data(), (int)data.size())) {
		stbi_us* px = stbi_load_16_from_memory((const stbi_uc*)data.data(), (int)data.size(), &width, &height, &n, 1);
		if (!px)
			return false;
		pixels.assign(px, px + (size_t)width * height);
		stbi_image_free(px);
		return true;
	}
	// an 8-bit picture: a grey level is a whole height step
	stbi_uc* px = stbi_load_from_memory((const stbi_uc*)data.data(), (int)data.size(), &width, &height, &n, 1);
	if (!px)
		return false;
	pixels.resize((size_t)width * height);
	for (size_t i = 0; i < pixels.size(); i++)
		pixels[i] = (uint16_t)(px[i] * 256);
	stbi_image_free(px);
	return true;
}

bool IsMapPath(const std::string& path)
{
	const std::string p = StripSlash(path);
	return p.size() > 6 && StrCICompare(std::string_view(p).substr(p.size() - 6), ".wkbai") == 0;
}

std::filesystem::path Resolve(const std::string& path)
{
	std::string p = StripSlash(path);
	std::error_code ec;
	const fs::path asIs = PathFromUtf8(p);
	if (asIs.is_absolute())
		return fs::is_directory(asIs, ec) ? asIs : fs::path();
	if (StartsWithCI(p, "Save_Games\\") || StartsWithCI(p, "Save_Games/"))
		p = "Maps\\" + p.substr(11);
	const fs::path rel = PathFromUtf8(portablePath(p));
	std::vector<fs::path> candidates{ g_userPath / "saved" / rel };
	const auto& mods = GetMods();
	for (auto it = mods.rbegin(); it != mods.rend(); ++it)
		if (it->enabled && it->id.rfind("mods/", 0) == 0)
			candidates.push_back(ModsPath() / PathFromUtf8(it->id.substr(5)) / rel);
	candidates.push_back(g_gamePath / "saved" / rel);
	for (const fs::path& c : candidates)
		if (fs::is_directory(c, ec))
			return fs::absolute(c, ec);
	return {};
}

std::vector<std::string> ListFolders()
{
	std::vector<std::string> names;
	std::vector<fs::path> roots{ g_userPath / "saved" / "Maps" };
	const auto& mods = GetMods();
	for (auto it = mods.rbegin(); it != mods.rend(); ++it)
		if (it->enabled && it->id.rfind("mods/", 0) == 0)
			roots.push_back(ModsPath() / PathFromUtf8(it->id.substr(5)) / "Maps");
	roots.push_back(g_gamePath / "saved" / "Maps");
	std::error_code ec;
	for (const fs::path& root : roots)
		for (const auto& entry : fs::directory_iterator(root, ec)) {
			const std::string name = PathToUtf8(entry.path().filename());
			if (entry.is_directory(ec) && IsMapPath(name) && std::find(names.begin(), names.end(), name) == names.end())
				names.push_back(name);
		}
	std::sort(names.begin(), names.end());
	return names;
}

std::string GamePathOf(const std::filesystem::path& folder)
{
	std::error_code ec;
	const fs::path abs = fs::weakly_canonical(fs::absolute(folder, ec), ec);
	std::vector<fs::path> roots{ g_userPath / "saved", g_gamePath / "saved" };
	for (const ModInfo& mod : GetMods())
		if (mod.id.rfind("mods/", 0) == 0)
			roots.push_back(ModsPath() / PathFromUtf8(mod.id.substr(5)));
	for (const fs::path& r : roots) {
		const fs::path root = fs::weakly_canonical(fs::absolute(r, ec), ec);
		const fs::path rel = abs.lexically_relative(root);
		if (!rel.empty() && rel.native()[0] != '.')
			return Backslashes(PathToUtf8(rel));
	}
	return PathToUtf8(abs);
}

std::string InputsHash(const std::filesystem::path& folder)
{
	uint64_t h = 1469598103934665603ull;
	const int version = COMPILER_VERSION;
	h = Fnv(h, &version, sizeof(version));
	const std::string game = GamePathOf(folder);
	h = Fnv(h, game.data(), game.size());
	for (const char* name : { "map.json", "heights.png", "heights.hand.png", "tiles.hand.json", "water.json", "level.txt", "minimap.png", "farland.png", "farland.json" }) {
		std::string data;
		const bool there = ReadAll(folder / name, data);
		h = Fnv(h, name, strlen(name));
		h = Fnv(h, &there, 1);
		h = Fnv(h, data.data(), data.size());
	}
	// the ground rules' data file and masks (MapMaterials.h)
	const std::string mat = MapMaterials::Fingerprint(folder);
	h = Fnv(h, mat.data(), mat.size());
	return Hex64(h);
}

namespace {
	// one compile at a time (the menus' background one, the server's, the editor's): the second
	// waits, then finds the cache fresh
	std::recursive_mutex g_compileMutex;
	std::mutex g_progressMutex;
	Progress g_progress;
	thread_local bool g_compilingHere = false; // this thread is in Compile (CompilingOnThisThread)
	// the share of the bar the ground transitions took in this compile (ReportTransitions): the
	// compile's own stages fill the rest
	float g_transitionsShare = 0;
	std::chrono::steady_clock::time_point g_stageStart;
	std::string g_stageName;
	std::thread g_background;
	std::atomic<bool> g_backgroundBusy{ false };
	std::atomic<bool> g_cancel{ false }; // the game is closing: the background compile stops at its next stage
	std::thread::id g_backgroundId;
	struct Cancelled {};
	std::set<std::string> g_failed; // "<folder>|<inputs hash>" of background compiles that failed
	struct JoinAtExit {
		~JoinAtExit()
		{
			g_cancel = true;
			if (g_background.joinable())
				g_background.join();
		}
	} g_joinAtExit; // (after g_background: destroyed before it)
}

bool IsFresh(const std::filesystem::path& folder)
{
	std::string stored;
	std::error_code ec;
	return ReadAll(folder / "build" / "inputs.hash", stored) && Trimmed(stored) == InputsHash(folder)
		&& fs::exists(folder / "build" / "map.bcm", ec) && fs::exists(folder / "build" / "level.txt", ec);
}

Result Compile(const std::filesystem::path& folder, bool force)
{
	Result r;
	if (folder.empty() || !fs::is_directory(folder)) {
		r.error = "no map folder " + PathToUtf8(folder);
		return r;
	}
	std::lock_guard<std::recursive_mutex> compiling(g_compileMutex);
	if (!force && IsFresh(folder)) {
		r.ok = true;
		r.reportPath = PathToUtf8(folder / "build" / "report.json");
		return r;
	}
	// the progress the menus show (CompileProgress), over whatever way this returns
	struct Busy {
		Busy(const fs::path& f)
		{
			g_compilingHere = true;
			{
				std::lock_guard<std::mutex> lock(g_progressMutex);
				g_transitionsShare = 0;
				g_progress.busy = true;
				g_progress.map = PathToUtf8(f.stem());
				g_stageName.clear();
			}
			ReportProgress("reading the map", 0);
		}
		~Busy()
		{
			g_compilingHere = false;
			ReportProgress("done", 1);
			std::lock_guard<std::mutex> lock(g_progressMutex);
			g_progress.busy = false;
		}
	} busy(folder);
	const std::string hash = InputsHash(folder);
	printf("Map folder: compiling %s\n", PathToUtf8(folder).c_str());
	MapJson m;
	json report;
	report["format"] = "wkbai map report";
	report["folder"] = PathToUtf8(folder);
	report["inputs"] = hash;
	auto fail = [&](const std::string& e) {
		r.error = e;
		report["ok"] = false;
		report["error"] = e;
		report["warnings"] = r.warnings;
		WriteAll(folder / "build" / "report.json", report.dump(1) + "\n");
		printf("Map folder: %s: %s\n", PathToUtf8(folder).c_str(), e.c_str());
		return r;
	};
	std::string error;
	if (!ReadMapJson(folder, m, error))
		return fail(error);
	report["tiles"] = { m.width, m.height };
	report["edge"] = m.edge;
	Terrain t;
	std::vector<uint8_t> minimap;
	std::map<int, HandTile> ruleTiles;
	ReportProgress("the heights", 0.005f);
	if (!BuildTerrain(folder, m, t, minimap, r.warnings, report, error, &ruleTiles))
		return fail(error);
	ReportProgress("the objects", 0.94f);
	std::string level;
	if (!BuildLevel(folder, m, t, level, r.warnings, report, error))
		return fail(error);
	ReportProgress("writing the cache", 0.955f);
	const fs::path build = folder / "build";
	std::error_code ec;
	fs::create_directories(build, ec);
	bool ok = WriteCacheBCM(build / "map.bcm", t, minimap);
	if (t.needsMapExtras())
		ok = ok && t.writeMapExtras(build / "map.wkbmap");
	else
		fs::remove(build / "map.wkbmap", ec);
	if (t.waterDesign)
		ok = ok && t.waterDesign->save(build / "map.water");
	else
		fs::remove(build / "map.water", ec);
	ok = ok && WriteAll(build / "level.txt", level);
	// the map's own far land (farland.png + farland.json): what Extend map shows round the map
	// instead of generating it. farland.json: { "x0": m, "z0": m (the picture's south-west pixel
	// in playable metres, x east, z north), "cell": m, "min": m, "max": m (the picture's 0 and
	// 65535), "seaLevel": m }; the picture's row 0 is its north edge
	{
		json fj;
		std::string ftext;
		int fw = 0, fh = 0;
		std::vector<uint16_t> fpx;
		if (ReadAll(folder / "farland.json", ftext) && !(fj = json::parse(ftext, nullptr, false)).is_discarded() && fj.is_object()
			&& ReadPng16(folder / "farland.png", fw, fh, fpx) && fw > 1 && fh > 1) {
			StoredFarLand f;
			f.x0 = fj.value("x0", 0.0f) + m.edge * 5.0f;
			f.z0 = fj.value("z0", 0.0f) + m.edge * 5.0f;
			f.cell = fj.value("cell", 40.0f);
			f.w = fw;
			f.h = fh;
			f.seaLevel = fj.contains("seaLevel") && fj["seaLevel"].is_number() ? fj["seaLevel"].get<float>() : -1e30f;
			const float lo = fj.value("min", 0.0f), hi = fj.value("max", 100.0f);
			f.v.resize((size_t)fw * fh);
			for (int r = 0; r < fh; r++)
				for (int x = 0; x < fw; x++)
					f.v[(size_t)(fh - 1 - r) * fw + x] = lo + (hi - lo) * fpx[(size_t)r * fw + x] / 65535.0f;
			ok = ok && Terrain::writeFarLand(build / "map.farland", f);
			report["farLand"] = { { "cells", { fw, fh } }, { "cellM", f.cell }, { "fromM", { fj.value("x0", 0.0f), fj.value("z0", 0.0f) } },
				{ "toM", { fj.value("x0", 0.0f) + f.cell * (fw - 1), fj.value("z0", 0.0f) + f.cell * (fh - 1) } } };
		}
		else
			fs::remove(build / "map.farland", ec);
	}
	// the tiles the rules painted, so the editor's Save knows which tiles a person changed
	if (!ruleTiles.empty())
		ok = ok && WriteHandTiles(build / "tiles.rules.json", m, ruleTiles);
	else
		fs::remove(build / "tiles.rules.json", ec);
	if (!ok)
		return fail("the cache can't be written");
	report["ok"] = true;
	report["warnings"] = r.warnings;
	ok = WriteAll(build / "report.json", report.dump(1) + "\n") && WriteAll(build / "inputs.hash", hash + "\n");
	r.ok = ok;
	r.compiled = true;
	r.reportPath = PathToUtf8(build / "report.json");
	for (const std::string& w : r.warnings)
		printf("Map folder: warning: %s\n", w.c_str());
	printf("Map folder: compiled %s (%d x %d tiles, %zu lakes, %d objects by rules)\n", PathToUtf8(folder).c_str(), m.width, m.height, t.lakes.size(),
		report.value("objectsByRules", 0));
	return r;
}

bool CompilingOnThisThread()
{
	return g_compilingHere;
}

void ReportTransitions(const std::string& stage, int done, int total)
{
	// they take most of a first compile (a minute or two against one): 60 % of the bar
	constexpr float SHARE = 0.6f;
	{
		std::lock_guard<std::mutex> lock(g_progressMutex);
		g_transitionsShare = 0;
	}
	ReportProgress(stage.c_str(), 0); // (the stage's name, the cancel check)
	std::lock_guard<std::mutex> lock(g_progressMutex);
	g_transitionsShare = SHARE * std::clamp((float)done / std::max(total, 1), 0.0f, 1.0f);
	g_progress.fraction = g_transitionsShare;
}

Progress CompileProgress()
{
	std::lock_guard<std::mutex> lock(g_progressMutex);
	return g_progress;
}

void ReportProgress(const char* stage, float fraction)
{
	if (g_cancel && std::this_thread::get_id() == g_backgroundId && fraction < 1)
		throw Cancelled{};
	std::lock_guard<std::mutex> lock(g_progressMutex);
	const auto now = std::chrono::steady_clock::now();
	if (stage && g_stageName != stage) {
		// "compileTimes": true in wkconfig.json prints how long each stage took
		if (!g_stageName.empty() && g_settings.value("compileTimes", false))
			printf("Map folder: %-40s %6.2f s\n", g_stageName.c_str(), std::chrono::duration<double>(now - g_stageStart).count());
		g_stageName = stage;
		g_stageStart = now;
		g_progress.stage = stage;
	}
	g_progress.fraction = std::clamp(g_transitionsShare + (1 - g_transitionsShare) * fraction, 0.0f, 1.0f);
}

std::string LevelFileIfFresh(const std::string& path)
{
	const fs::path folder = Resolve(path);
	if (folder.empty())
		return {};
	if (g_backgroundBusy)
		return {};
	if (IsFresh(folder))
		return PathToUtf8(folder / "build" / "level.txt");
	const std::string key = PathToUtf8(folder) + "|" + InputsHash(folder);
	{
		std::lock_guard<std::mutex> lock(g_progressMutex);
		if (g_failed.count(key))
			return {};
	}
	if (g_background.joinable())
		g_background.join();
	g_backgroundBusy = true;
	{
		std::lock_guard<std::mutex> lock(g_progressMutex);
		g_progress.background = true;
	}
	g_background = std::thread([folder, key]() {
		g_backgroundId = std::this_thread::get_id();
		Result r;
		try {
			r = Compile(folder);
		}
		catch (const Cancelled&) {
			printf("Map folder: compiling %s stopped (the game closes)\n", PathToUtf8(folder).c_str());
		}
		std::lock_guard<std::mutex> lock(g_progressMutex);
		if (!r.ok && !g_cancel)
			g_failed.insert(key);
		g_progress.background = false;
		g_progress.finished++;
		g_backgroundBusy = false;
	});
	return {};
}

std::string LevelFileOf(const std::string& path)
{
	const fs::path folder = Resolve(path);
	if (folder.empty() || !Compile(folder).ok)
		return {};
	return PathToUtf8(folder / "build" / "level.txt");
}

std::string TerrainFileOf(const std::string& path)
{
	const fs::path folder = Resolve(path);
	if (folder.empty() || !Compile(folder).ok)
		return {};
	return PathToUtf8(folder / "build" / "map.bcm");
}

std::string TranslationOf(const std::filesystem::path& folder)
{
	MapJson m;
	std::string error;
	return ReadMapJson(folder, m, error) ? m.translation : std::string();
}

namespace {
	// map.json for a terrain (an original map's, or the one open in the editor)
	json MapJsonOf(const Terrain& t, const std::string& name, const std::string& translation)
	{
		json j;
		j["format"] = "wkbai map";
		j["version"] = FORMAT_VERSION;
		j["name"] = name;
		j["size"] = { { "tiles", { t.width, t.height } }, { "edge", t.edge } };
		j["heights"] = { { "scale", t.scale }, { "file", "heights.png" }, { "unit", "1/256 of a height step; height = value / 256 * scale" } };
		j["textures"] = { { "database", Narrow(t.texDbPath) }, { "translation", translation } };
		j["sky"] = Narrow(t.skyTextureDirectory);
		j["sun"] = { { "colour", ColourTo(t.sunColor) }, { "direction", { t.sunVector.x, t.sunVector.y, t.sunVector.z } } };
		j["fog"] = ColourTo(t.fogColor);
		j["seed"] = 1;
		j["ground"] = json::object();
		j["players"] = json::array();
		return j;
	}
	// the folder's sources from a terrain as it is (every tile a hand tile)
	bool WriteSources(const fs::path& folder, const Terrain& t, const json& mapJson, MapJson& m)
	{
		bool ok = WriteAll(folder / "map.json", mapJson.dump(1) + "\n");
		std::string error;
		ok = ok && ReadMapJson(folder, m, error);
		std::vector<uint16_t> grid((size_t)(t.width + 1) * (t.height + 1));
		for (size_t i = 0; i < grid.size(); i++)
			grid[i] = TerrainHeight256(t, (int)i);
		ok = ok && WriteGridPng(folder / "heights.png", m, grid);
		std::vector<uint16_t> zero(grid.size(), HAND_ZERO);
		ok = ok && WriteGridPng(folder / "heights.hand.png", m, zero);
		std::map<int, HandTile> tiles;
		for (int i = 0; i < t.width * t.height; i++)
			tiles[i] = HandTile{ t.tiles[i].texture->grp->name, t.tiles[i].texture->id, (int)t.tiles[i].rot, (int)t.tiles[i].xflip, (int)t.tiles[i].zflip };
		ok = ok && WriteHandTiles(folder / "tiles.hand.json", m, tiles);
		ok = ok && WriteWater(folder / "water.json", t.lakes, t.lakeExtraBits, t.waterDesign.get(), t.width, t.height);
		return ok;
	}
	// the MAP line's file of a level's text
	std::string MapOfWorld(const std::string& world)
	{
		const size_t m = world.find("MAP \"");
		if (m == std::string::npos)
			return {};
		const size_t end = world.find('"', m + 5);
		return end == std::string::npos ? std::string() : world.substr(m + 5, end - m - 5);
	}
}

Result Import(const std::string& level, std::filesystem::path out)
{
	Result r;
	std::string levelPath = level;
	if (!FileExists(levelPath.c_str()) && FileExists(("Save_Games\\" + level).c_str()))
		levelPath = "Save_Games\\" + level;
	if (!FileExists(levelPath.c_str())) {
		r.error = "no level " + level;
		return r;
	}
	char* data = nullptr;
	int size = 0;
	LoadFile(levelPath.c_str(), &data, &size);
	std::string text(data, size);
	free(data);
	std::string world = text;
	if (SaveFile::IsSaveFile(text)) {
		SaveFile file;
		std::string err;
		file.read(text, err);
		const SaveSection* s = file.find("world");
		world = s ? s->text : std::string();
	}
	const std::string bcm = MapOfWorld(world);
	if (bcm.empty() || IsMapPath(bcm) || !FileExists(bcm.c_str())) {
		r.error = "the level's map " + bcm + " isn't an original map that is there";
		return r;
	}
	std::string name = PathToUtf8(PathFromUtf8(portablePath(levelPath)).stem());
	if (out.empty())
		out = g_userPath / "saved" / "Maps" / PathFromUtf8(name + ".wkbai");
	std::error_code ec;
	fs::create_directories(out, ec);
	Terrain t;
	t.readFromFile(bcm.c_str());
	const std::string lttt = t.texDb.getLTTTPath(bcm);
	json mj = MapJsonOf(t, name, FileExists(lttt.c_str()) ? lttt : std::string());
	mj["source"] = { { "level", levelPath }, { "map", bcm } };
	MapJson m;
	bool ok = WriteSources(out, t, mj, m);
	if (!t.minimapRGBA.empty()) {
		std::vector<uint8_t> rgb(128 * 128 * 3);
		for (int i = 0; i < 128 * 128; i++)
			for (int c = 0; c < 3; c++)
				rgb[i * 3 + c] = t.minimapRGBA[i * 4 + c];
		ok = ok && WriteMinimap(out / "minimap.png", rgb);
	}
	// the level: its world as it is, the MAP line naming the folder; every object hand-made
	std::vector<std::string> lines = SplitLines(world);
	SetMapLine(lines, GamePathOf(out));
	std::string w;
	for (const std::string& l : lines)
		w += l;
	Authoring auth;
	auth.source = { { "level", levelPath }, { "map", bcm } };
	SaveFile file;
	file.add("world", 1, w);
	file.add("authoring", 1, AuthoringText(auth));
	ok = ok && WriteAll(out / "level.txt", file.write());
	fs::remove_all(out / "build", ec);
	if (!ok) {
		r.error = "writing " + PathToUtf8(out) + " failed";
		return r;
	}
	r = Compile(out, true);
	printf("Map folder: imported %s (%s) into %s\n", levelPath.c_str(), bcm.c_str(), PathToUtf8(out).c_str());
	return r;
}

Result SaveFromEditor(Server& server, const Terrain* clientTerrain, const std::filesystem::path& folder)
{
	Result r;
	Terrain* t = server.terrain;
	if (!t || !t->tiles) {
		r.error = "no terrain";
		return r;
	}
	std::error_code ec;
	fs::create_directories(folder, ec);
	const fs::path current = IsMapPath(server.mapFile) ? Resolve(server.mapFile) : fs::path();
	const bool same = !current.empty() && fs::equivalent(current, folder, ec);
	MapJson m;
	std::string error;
	const bool haveMap = ReadMapJson(folder, m, error);
	if (!haveMap || m.width != t->width || m.height != t->height) {
		// a new folder (or one of another size): its sources from the map that is open. An
		// original map's own heights are the base (the strokes made since go into the hand layer).
		if (!current.empty() && !same && fs::exists(current / "map.json", ec)) {
			for (const char* f : { "map.json", "heights.png", "minimap.png" })
				if (fs::exists(current / f, ec))
					fs::copy_file(current / f, folder / f, fs::copy_options::overwrite_existing, ec);
			fs::remove(folder / "level.txt", ec);
		}
		std::string err2;
		if (!ReadMapJson(folder, m, err2) || m.width != t->width || m.height != t->height) {
			Terrain original;
			const bool fromOriginal = current.empty() && !server.mapFile.empty() && FileExists(server.mapFile.c_str());
			if (fromOriginal)
				original.readFromFile(server.mapFile.c_str());
			const Terrain& baseTerrain = fromOriginal && original.width == t->width && original.height == t->height ? original : *t;
			std::string translation;
			if (!current.empty())
				translation = TranslationOf(current);
			else if (const std::string lttt = t->texDb.getLTTTPath(server.mapFile); FileExists(lttt.c_str()))
				translation = lttt;
			json mj = MapJsonOf(*t, PathToUtf8(folder.stem()), translation);
			if (!WriteSources(folder, baseTerrain, mj, m)) {
				r.error = "writing " + PathToUtf8(folder) + " failed";
				return r;
			}
		}
	}
	// heights: what isn't heights.png goes into the hand layer
	std::vector<uint16_t> base;
	if (!ReadBaseHeights(folder, m, base, r.warnings, error)) {
		r.error = error;
		return r;
	}
	std::vector<uint16_t> hand(base.size(), HAND_ZERO);
	int handVertices = 0;
	for (size_t i = 0; i < base.size(); i++) {
		const int d = (int)TerrainHeight256(*t, (int)i) - (int)base[i];
		hand[i] = (uint16_t)std::clamp(HAND_ZERO + d, 0, 65535);
		handVertices += d != 0;
	}
	bool ok = WriteGridPng(folder / "heights.hand.png", m, hand);
	// tiles: those that aren't the rules' (or were hand tiles) are hand tiles
	std::map<int, HandTile> oldHand, newHand, painted;
	ReadHandTiles(folder, m, oldHand, r.warnings);
	// a map with ground rules: the tiles the rules painted at the last compile (MapMaterials.h)
	const bool byMaterials = MapMaterials::HasRules(m.raw);
	if (byMaterials)
		ReadHandTiles(current.empty() ? folder : current, m, painted, r.warnings, fs::path("build") / "tiles.rules.json");
	for (int z = 0; z < t->height; z++)
		for (int x = 0; x < t->width; x++) {
			const int i = z * t->width + x;
			const TerrainTile& tile = t->tiles[i];
			int rot = 0;
			bool differs;
			if (byMaterials) {
				auto p = painted.find(i);
				differs = p == painted.end() || p->second.group != tile.texture->grp->name || p->second.id != tile.texture->id || p->second.rot != (int)tile.rot
					|| p->second.xflip != (int)tile.xflip || p->second.zflip != (int)tile.zflip;
			}
			else {
				const TerrainTexture* rule = RuleTile(m, t->texDb, x, z, rot);
				differs = !rule || rule->grp->name != tile.texture->grp->name || rule->id != tile.texture->id || (int)tile.rot != rot || tile.xflip || tile.zflip;
			}
			if (differs || oldHand.count(i))
				newHand[i] = HandTile{ tile.texture->grp->name, tile.texture->id, (int)tile.rot, (int)tile.xflip, (int)tile.zflip };
		}
	ok = ok && WriteHandTiles(folder / "tiles.hand.json", m, newHand);
	// water: the lakes (the server's) and the design (the client's, which the editor changes)
	const WaterDesign* design = clientTerrain && clientTerrain->waterDesign && !clientTerrain->waterDesign->isEmpty() ? clientTerrain->waterDesign.get() : nullptr;
	ok = ok && WriteWater(folder / "water.json", t->lakes, t->lakeExtraBits, design, t->width, t->height);
	// the level, from the server; the rules' objects that nobody moved stay the rules'
	Authoring auth;
	{
		std::string loaded;
		// the authoring the game loaded with the level (the cache's), else the folder's
		for (const fs::path& p : { (current.empty() ? folder : current) / "build" / "level.txt", folder / "level.txt" })
			if (ReadAll(p, loaded) && SaveFile::IsSaveFile(loaded)) {
				SaveFile file;
				std::string err;
				file.read(loaded, err);
				if (const SaveSection* s = file.find("authoring")) {
					auth = ParseAuthoring(s->text);
					break;
				}
			}
	}
	const std::string mapBefore = server.mapFile;
	const fs::path tmp = TempSave(folder);
	fs::create_directories(tmp.parent_path(), ec);
	server.mapFile = GamePathOf(folder);
	server.savingLevel = true;
	ok = ok && server.saveGame(PathToUtf8(tmp).c_str());
	server.savingLevel = false;
	std::string world;
	ok = ok && ReadAll(tmp, world);
	fs::remove(tmp, ec);
	if (!same)
		server.mapFile = mapBefore;
	int keptByRules = 0, takenOver = 0;
	for (auto it = auth.objects.begin(); it != auth.objects.end();) {
		ServerGameObject* obj = server.findObject(it->first);
		const auto at = auth.positions.find(it->first);
		const bool moved = !obj || obj->deleted || (at != auth.positions.end()
			&& (std::abs(obj->position.x - at->second.first) > 0.05f || std::abs(obj->position.z - at->second.second) > 0.05f));
		if (moved) {
			auth.suppressed.insert(it->second);
			auth.positions.erase(it->first);
			it = auth.objects.erase(it);
			takenOver++;
		}
		else {
			++it;
			keptByRules++;
		}
	}
	// a rule some of whose objects were taken over by hand: its others are hand-made now too
	for (auto it = auth.objects.begin(); it != auth.objects.end();)
		if (auth.suppressed.count(it->second)) {
			auth.positions.erase(it->first);
			it = auth.objects.erase(it);
		}
		else
			++it;
	SaveFile file;
	file.add("world", 1, world);
	file.add("authoring", 1, AuthoringText(auth));
	ok = ok && WriteAll(folder / "level.txt", file.write());
	if (!ok) {
		r.error = "saving into " + PathToUtf8(folder) + " failed";
		return r;
	}
	printf("Map folder: saved %s from the editor (%d vertices in the hand layer, %zu hand tiles, %zu lakes, %d objects still the rules', %d taken over by hand)\n",
		PathToUtf8(folder).c_str(), handVertices, newHand.size(), t->lakes.size(), keptByRules, takenOver);
	r = Compile(folder, true);
	return r;
}

std::vector<std::pair<std::string, float>> EnvironmentDefaults(const std::string& mapPath)
{
	std::vector<std::pair<std::string, float>> out;
	const fs::path folder = IsMapPath(mapPath) ? Resolve(mapPath) : fs::path();
	std::string text;
	if (folder.empty() || !ReadAll(folder / "map.json", text))
		return out;
	const json j = json::parse(text, nullptr, false);
	if (j.is_discarded() || !j.is_object() || !j.contains("environment") || !j["environment"].is_object())
		return out;
	const json& e = j["environment"];
	auto lower = [](std::string s) { for (char& c : s) c = (char)tolower((unsigned char)c); return s; };
	auto pick = [&](const json& v, std::initializer_list<const char*> names, int& index) {
		if (v.is_number()) {
			index = v.get<int>();
			return true;
		}
		if (!v.is_string())
			return false;
		const std::string s = lower(v.get<std::string>());
		int i = 0;
		for (const char* n : names) {
			if (s == n) {
				index = i;
				return true;
			}
			i++;
		}
		return false;
	};
	try {
		if (const json d = e.value("dayNight", json::object()); d.is_object()) {
			if (d.contains("on"))
				out.emplace_back("dayNight", d["on"].get<bool>() ? 1.0f : 0.0f);
			if (d.contains("dayLengthMin"))
				out.emplace_back("dayLength", d["dayLengthMin"].get<float>());
			// a clock hour (5, or "05:30": the hour, as the setting has whole hours); a start by the
			// sun ("dawn", ...) is step 17.13's
			if (d.contains("start")) {
				const json& s = d["start"];
				if (s.is_number())
					out.emplace_back("startHour", s.get<float>());
				else if (s.is_string() && !s.get<std::string>().empty() && isdigit((unsigned char)s.get<std::string>()[0]))
					out.emplace_back("startHour", (float)atoi(s.get<std::string>().c_str()));
			}
			int moon;
			if (d.contains("moonPhase") && pick(d["moonPhase"], { "natural", "new", "waxing crescent", "first quarter", "waxing gibbous", "full",
				"waning gibbous", "third quarter", "waning crescent" }, moon))
				out.emplace_back("moonPhase", (float)moon);
		}
		// the latitude (degrees north, negative south): the sun's path and the day's length
		if (e.contains("latitude") && e["latitude"].is_number())
			out.emplace_back("latitude", e["latitude"].get<float>());
		if (const json s = e.value("seasons", json::object()); s.is_object()) {
			if (s.contains("on"))
				out.emplace_back("seasons", s["on"].get<bool>() ? 1.0f : 0.0f);
			if (s.contains("seasonLengthMin"))
				out.emplace_back("seasonLength", s["seasonLengthMin"].get<float>());
			int season;
			if (s.contains("start") && pick(s["start"], { "spring", "summer", "autumn", "winter" }, season))
				out.emplace_back("startSeason", (float)season);
			static const char* keys[4] = { "spring", "summer", "autumn", "winter" };
			static const char* names[4] = { "weatherSpring", "weatherSummer", "weatherAutumn", "weatherWinter" };
			if (s.contains("severity")) {
				const json& sev = s["severity"];
				for (int k = 0; k < 4; k++) {
					int v;
					const json one = sev.is_object() ? sev.value(keys[k], json()) : sev;
					if (pick(one, { "gentle", "normal", "harsh", "random" }, v))
						out.emplace_back(names[k], (float)v);
				}
			}
		}
		if (const json w = e.value("weather", json::object()); w.is_object()) {
			if (w.contains("stability")) {
				const json& st = w["stability"];
				int v;
				if (st.is_object()) {
					out.emplace_back("weatherStability", 4.0f);
					if (st.contains("minChangeMin"))
						out.emplace_back("weatherMinChange", st["minChangeMin"].get<float>());
					if (st.contains("maxChangeMin"))
						out.emplace_back("weatherMaxChange", st["maxChangeMin"].get<float>());
				}
				else if (pick(st, { "frequent", "normal", "seldom", "random", "custom" }, v))
					out.emplace_back("weatherStability", (float)v);
			}
			// the weathers allowed (EnvironmentSettings::weatherTypes; today the weather keeps to
			// them with the custom stability only, as the lobby's)
			if (w.contains("allowed") && w["allowed"].is_array()) {
				static const char* weathers[WEATHER_COUNT] = { "clear", "cloudy", "rain", "heavy rain", "thunderstorm", "fog", "snow", "blizzard", "heatwave", "drought" };
				int bits = 0;
				for (const json& a : w["allowed"]) {
					if (!a.is_string())
						continue;
					std::string n = lower(a.get<std::string>());
					if (n == "heavyrain" || n == "heavy_rain")
						n = "heavy rain";
					for (int k = 0; k < WEATHER_COUNT; k++)
						if (n == weathers[k])
							bits |= 1 << k;
				}
				out.emplace_back("weatherTypes", (float)bits);
			}
		}
	}
	catch (const std::exception& ex) {
		printf("Map folder: %s: map.json's environment block: %s (what was read before is used)\n", mapPath.c_str(), ex.what());
	}
	return out;
}

bool SetLatitude(const fs::path& folder, float latitudeDeg)
{
	std::string text;
	if (!ReadAll(folder / "map.json", text))
		return false;
	json j = json::parse(text, nullptr, false);
	if (j.is_discarded() || !j.is_object())
		return false;
	if (!j.contains("environment") || !j["environment"].is_object())
		j["environment"] = json::object();
	// (to a tenth of a degree: the editor's slider)
	j["environment"]["latitude"] = std::round(std::clamp(latitudeDeg, -75.0f, 75.0f) * 10.0f) / 10.0f;
	std::ofstream f(folder / "map.json", std::ios::binary);
	f << j.dump(1) << "\n";
	return (bool)f;
}

int ApplyEnvironmentDefaults(const std::string& mapPath, EnvironmentSettings& settings)
{
	// a setting's value by its apply name (the ones EnvironmentDefaults gives)
	auto get = [](const EnvironmentSettings& s, const std::string& n) -> float {
		if (n == "dayNight") return s.dayNight;
		if (n == "dayLength") return (float)s.dayLengthMin;
		if (n == "startHour") return (float)s.startHour;
		if (n == "moonPhase") return (float)s.moonPhase;
		if (n == "seasons") return s.seasons;
		if (n == "seasonLength") return (float)s.seasonLengthMin;
		if (n == "startSeason") return (float)s.startSeason;
		if (n == "weatherSpring") return (float)s.severity[0];
		if (n == "weatherSummer") return (float)s.severity[1];
		if (n == "weatherAutumn") return (float)s.severity[2];
		if (n == "weatherWinter") return (float)s.severity[3];
		if (n == "weatherStability") return (float)s.weatherStability;
		if (n == "weatherMinChange") return (float)s.weatherMinChange;
		if (n == "weatherMaxChange") return (float)s.weatherMaxChange;
		if (n == "weatherTypes") return (float)s.weatherTypes;
		if (n == "latitude") return s.latitude;
		return -12345.0f;
	};
	const EnvironmentSettings lobbyDefault;
	int applied = 0;
	for (const auto& [name, value] : EnvironmentDefaults(mapPath))
		if (get(settings, name) == get(lobbyDefault, name) && settings.apply(name, value))
			applied++;
	if (applied)
		printf("Map folder: %s: %d of the map's environment settings applied\n", mapPath.c_str(), applied);
	return applied;
}

int RunCommandLine(const std::vector<std::string>& args)
{
	if (args.empty()) {
		printf("usage: wkbai --author <folder.wkbai>\n       wkbai --author import <level.lvl> [<folder.wkbai>]\n"
			"       wkbai --author catalog [--mod <name>|none] [--gameset <file>] [--edges] [--out <file.json>]\n"
			"       wkbai --author describe <folder.wkbai | level> [--mod <name>|none] [--png <file.png>] [--out <file.json>]\n"
			"       wkbai --author terrain|terrain-stats|heightmap|elevation|elevation-tiles ... (MapTerrain.h)\n"
			"       wkbai --author materials <folder.wkbai> [--out <file.json>] [--png <file.png>] | material-presets (MapMaterials.h)\n");
		return 2;
	}
	// what can go on a map, and an existing map in numbers and a picture (MapAuthoring.h)
	if (args[0] == "catalog")
		return MapAuthoring::RunCatalog(std::vector<std::string>(args.begin() + 1, args.end()));
	if (args[0] == "describe")
		return MapAuthoring::RunDescribe(std::vector<std::string>(args.begin() + 1, args.end()));
	// the land: the feature plan's generator, heightmap and elevation import (MapTerrain.h)
	for (const char* c : { "terrain", "terrain-stats", "heightmap", "elevation", "elevation-tiles" })
		if (args[0] == c)
			return MapTerrain::RunCommandLine(args);
	// the ground: materials by rule and the edge-rule solver (MapMaterials.h)
	if (args[0] == "materials" || args[0] == "material-presets")
		return MapMaterials::RunCommandLine(args);
	LoadBCP("data.bcp");
	Result r;
	if (args[0] == "import") {
		if (args.size() < 2) {
			printf("usage: wkbai --author import <level.lvl> [<folder.wkbai>]\n");
			return 2;
		}
		r = Import(args[1], args.size() > 2 ? PathFromUtf8(args[2]) : fs::path());
	}
	else {
		const fs::path folder = Resolve(args[0]);
		if (folder.empty()) {
			printf("AUTHOR no map folder %s\n", args[0].c_str());
			return 1;
		}
		r = Compile(folder, true);
	}
	for (const std::string& w : r.warnings)
		printf("AUTHOR warning: %s\n", w.c_str());
	if (!r.ok)
		printf("AUTHOR failed: %s\n", r.error.c_str());
	else
		printf("AUTHOR ok, report %s\n", r.reportPath.c_str());
	fflush(stdout);
	return r.ok ? 0 : 1;
}

} // namespace MapFolder
