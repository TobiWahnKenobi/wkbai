// wkbai - wkbai's own map format, its compiler and the import of original maps (MapFolder.h)

#include "MapFolder.h"
#include "MapAuthoring.h"
#include "terrain.h"
#include "server.h"
#include "file.h"
#include "SaveFile.h"
#include "WaterDesign.h"
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
#include <map>
#include <regex>
#include <set>
#include <sstream>

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
				if (p.contains("start")) {
					mp.hasStart = true;
					mp.x = p["start"][0].get<float>();
					mp.z = p["start"][1].get<float>();
				}
				mp.base = p.value("base", mp.base);
				mp.peasants = p.value("peasants", mp.peasants);
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
	bool ReadHandTiles(const fs::path& folder, const MapJson& m, std::map<int, HandTile>& out, std::vector<std::string>& warnings)
	{
		out.clear();
		std::string text;
		if (!ReadAll(folder / "tiles.hand.json", text))
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
		for (const auto& [id, rule] : j.value("objects", json::object()).items()) {
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
	// PREDEC made again from the objects' lines (as Server::saveGame writes it); NEXT_UNIQUE_ID past
	// the highest id
	void RebuildPredec(std::vector<std::string>& lines)
	{
		std::map<std::pair<int, std::string>, std::vector<uint32_t>> byType;
		uint32_t levelId = 0, landscapeId = 0, maxId = 0;
		std::string levelBp = "Standard";
		size_t predecStart = std::string::npos, predecEnd = std::string::npos;
		bool inPredec = false;
		for (size_t i = 0; i < lines.size(); i++) {
			const std::string t = Trimmed(lines[i]);
			if (t == "PREDEC") { predecStart = i; inPredec = true; continue; }
			if (t == "END_PREDEC") { predecEnd = i; inPredec = false; continue; }
			if (inPredec) {
				if (t.rfind("LEVEL ", 0) == 0) {
					const size_t q = t.find('"'), q2 = t.find('"', q + 1);
					if (q != std::string::npos && q2 != std::string::npos)
						levelBp = t.substr(q + 1, q2 - q - 1);
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
				const uint32_t next = (uint32_t)strtoul(Trimmed(l).c_str() + 15, nullptr, 10);
				if (next <= maxId)
					l = "NEXT_UNIQUE_ID " + std::to_string(maxId + 1) + "\n";
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
			if (ObjectLine(l, cls, id, bp))
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

	// A level for a map without level.txt: the Neutral Player and map.json's players (no objects
	// yet; the rules add them), all at war with each other
	std::string SkeletonWorld(const MapJson& m, const std::string& mapPath, std::vector<uint32_t>& playerIds)
	{
		const uint32_t levelId = 1, landscapeId = 2, neutral = 3;
		playerIds.clear();
		for (size_t i = 0; i < m.players.size(); i++)
			playerIds.push_back(10 + (uint32_t)i);
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
		line("NEXT_UNIQUE_ID 100");
		line("");
		line("PREDEC");
		line("\tLEVEL \"Standard\" 1 1");
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
		for (size_t i = 0; i < m.players.size(); i++)
			line("\tDIPLOMATIC_STATUS_BETWEEN " + std::to_string(neutral) + " " + std::to_string(playerIds[i]) + " \"Neutral\"");
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
	// the MAP line naming the folder
	bool BuildLevel(const fs::path& folder, const MapJson& m, const Terrain& terrain, std::string& out, std::vector<std::string>& warnings,
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
		json& report, std::string& error)
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
		if (unset)
			warnings.push_back(std::to_string(unset) + " tiles have no hand tile and no material (ground.fill): " + fallback->grp->name + " " + std::to_string(fallback->id));
		// water
		std::shared_ptr<WaterDesign> design;
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
	for (const char* name : { "map.json", "heights.png", "heights.hand.png", "tiles.hand.json", "water.json", "level.txt", "minimap.png" }) {
		std::string data;
		const bool there = ReadAll(folder / name, data);
		h = Fnv(h, name, strlen(name));
		h = Fnv(h, &there, 1);
		h = Fnv(h, data.data(), data.size());
	}
	return Hex64(h);
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
	if (!force && IsFresh(folder)) {
		r.ok = true;
		r.reportPath = PathToUtf8(folder / "build" / "report.json");
		return r;
	}
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
	if (!BuildTerrain(folder, m, t, minimap, r.warnings, report, error))
		return fail(error);
	std::string level;
	if (!BuildLevel(folder, m, t, level, r.warnings, report, error))
		return fail(error);
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
	std::map<int, HandTile> oldHand, newHand;
	ReadHandTiles(folder, m, oldHand, r.warnings);
	for (int z = 0; z < t->height; z++)
		for (int x = 0; x < t->width; x++) {
			const int i = z * t->width + x;
			const TerrainTile& tile = t->tiles[i];
			int rot = 0;
			const TerrainTexture* rule = RuleTile(m, t->texDb, x, z, rot);
			const bool differs = !rule || rule->grp->name != tile.texture->grp->name || rule->id != tile.texture->id || (int)tile.rot != rot || tile.xflip || tile.zflip;
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
			"       wkbai --author describe <folder.wkbai | level> [--mod <name>|none] [--png <file.png>] [--out <file.json>]\n");
		return 2;
	}
	// what can go on a map, and an existing map in numbers and a picture (MapAuthoring.h)
	if (args[0] == "catalog")
		return MapAuthoring::RunCatalog(std::vector<std::string>(args.begin() + 1, args.end()));
	if (args[0] == "describe")
		return MapAuthoring::RunDescribe(std::vector<std::string>(args.begin() + 1, args.end()));
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
