// wkbai - generated ground transitions (TransitionGen.h)
// Licensed under the GNU General Public License 3

#include "TransitionGen.h"
#include "TrnTextureDb.h"
#include "MapMaterials.h"
#include "file.h"
#include "gfx/bitmap.h"
#include "gfx/Bc7Encode.h"
#include "util/StriCompare.h"
#include <nlohmann/json.hpp>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#include <stb_image_write.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {
	std::mutex g_mutex; // (the background map compile and the game may load a database at once)

	uint32_t Hash(uint32_t a, uint32_t b, uint32_t c)
	{
		uint32_t h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u) * 0x85EBCA77u ^ (c + 0x165667B1u) * 0xC2B2AE3Du;
		h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12; h *= 0x297A2D39u; h ^= h >> 15;
		return h;
	}
	float Unit(uint32_t h) { return (h >> 8) * (1.0f / 16777216.0f); }
	// value noise on a lattice, smooth between
	float Noise(float x, float y, uint32_t s)
	{
		const int ix = (int)std::floor(x), iy = (int)std::floor(y);
		const float fx = x - ix, fy = y - iy;
		const float u = fx * fx * (3 - 2 * fx), v = fy * fy * (3 - 2 * fy);
		auto at = [&](int a, int b) { return Unit(Hash(s, (uint32_t)(ix + a), (uint32_t)(iy + b))); };
		const float p = at(0, 0) + (at(1, 0) - at(0, 0)) * u, q = at(0, 1) + (at(1, 1) - at(0, 1)) * u;
		return p + (q - p) * v;
	}
	float Fbm(float x, float y, uint32_t s)
	{
		float sum = 0, amp = 0.5f, norm = 0;
		for (int i = 0; i < 4; i++) {
			sum += amp * Noise(x, y, s + (uint32_t)i * 977u);
			norm += amp;
			amp *= 0.5f;
			x *= 2.07f;
			y *= 2.07f;
		}
		return sum / norm;
	}
	float SmoothStep(float a, float b, float x)
	{
		const float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
		return t * t * (3 - 2 * t);
	}

	// a pure group's pictures: (file, the 64-pixel cell's corner in the original 256-pixel atlas)
	struct Cell { std::string file; int x, y; };
	std::vector<Cell> CellsOf(const TerrainTextureGroup& g)
	{
		std::vector<Cell> out;
		for (const auto& [id, t] : g.textures)
			if (t && t->width == 64 && t->height == 64)
				out.push_back({ t->file, t->startx, t->starty });
		return out;
	}

	// A pair's 8 pictures: the corners that are B (picture corners 0 (u0, v0), 1 (u1, v0), 2 (u1, v1),
	// 3 (u0, v1), as the solver's), two variants of each
	const int PATTERNS[8] = { 0b0001, 0b0011, 0b0101, 0b0111, 0b0001, 0b0011, 0b0101, 0b0111 };
	// A triple's corner layouts: every way three groups share four corners up to the solver's turns
	// and flips (one group on two corners, side by side or across, each of the three): 6, two
	// variants of each = 12 pictures. Indices into the triple's groups per picture corner
	const int TRIPLE_LAYOUTS[6][4] = {
		{ 0, 0, 1, 2 }, { 1, 1, 2, 0 }, { 2, 2, 0, 1 }, // two corners side by side
		{ 0, 1, 0, 2 }, { 1, 2, 1, 0 }, { 2, 0, 2, 1 }, // two corners across
	};

	std::string Lower(std::string s)
	{
		for (char& c : s) c = (char)tolower((unsigned char)c);
		return s;
	}

	// the atlas's pixels (RGBA), its file as found (an HD replacement or the original)
	struct Atlas { Bitmap bmp; std::string source; };
	bool LoadAtlas(const std::string& file, Atlas& a)
	{
		const std::string path = "Maps\\Map_Textures\\" + file;
		a.source = Bitmap::replacementFile(path);
		if (!FileExists(a.source.c_str()))
			return false;
		a.bmp = Bitmap::loadBitmapExact(a.source.c_str()).convertToR8G8B8A8();
		return a.bmp.width >= 256 && a.bmp.height >= 64;
	}
	bool LoadBump(const std::string& file, Bitmap& b)
	{
		const size_t dot = file.rfind('.');
		const std::string path = "Maps\\Map_Textures\\" + (dot == std::string::npos ? file : file.substr(0, dot)) + "_BumpMap.pcx";
		if (!FileExists(path.c_str()))
			return false;
		b = Bitmap::loadBitmapExact(path.c_str());
		return b.format == BMFORMAT_P8 && b.width >= 256;
	}

	// a cell's pixel (RGBA) at (u, v) 0-1 in the cell, bilinear
	void Sample(const Bitmap& b, const Cell& c, float u, float v, float out[4])
	{
		const float s = b.width / 256.0f; // an HD atlas is the original's size times s
		const float cs = 64 * s;
		const float fx = c.x * s + std::clamp(u * cs - 0.5f, 0.0f, cs - 1.001f), fy = c.y * s + std::clamp(v * cs - 0.5f, 0.0f, cs - 1.001f);
		const int x0 = (int)fx, y0 = (int)fy;
		const int x1 = std::min(x0 + 1, b.width - 1), y1 = std::min(y0 + 1, b.height - 1);
		const float tx = fx - x0, ty = fy - y0;
		const uint8_t* p = b.pixels.data();
		for (int k = 0; k < 4; k++) {
			const float a = p[((size_t)y0 * b.width + x0) * 4 + k] + (p[((size_t)y0 * b.width + x1) * 4 + k] - p[((size_t)y0 * b.width + x0) * 4 + k]) * tx;
			const float d = p[((size_t)y1 * b.width + x0) * 4 + k] + (p[((size_t)y1 * b.width + x1) * 4 + k] - p[((size_t)y1 * b.width + x0) * 4 + k]) * tx;
			out[k] = a + (d - a) * ty;
		}
	}

	// The groups' shares at (u, v) of a picture whose corner k is group corner[k] (of n groups):
	// per group its corners' mix, a ragged edge by noise that fades out close to the picture's
	// border (so a side is the same function of its two corners on every picture: neighbours meet
	// without a seam, a triple's side exactly as the pair's picture of those two), a fine grain on the
	// edge (as the game's own fringes), then a soft ramp. With three groups the shares are sharpened
	// inside (the border kept), so the middle shows one ground rather than a mix of three.
	// (Version 1: a hard ramp 0.4-0.6 and the noise faded over a fifth of the picture: the borders
	// followed the tile grid in rounded squares.)
	void Shares(const int corner[4], int n, float u, float v, uint32_t seed, float out[3])
	{
		const float bw[4] = { (1 - u) * (1 - v), u * (1 - v), u * v, (1 - u) * v };
		const float border = std::min({ u, 1 - u, v, 1 - v });
		const float e = SmoothStep(0.0f, 0.10f, border);
		if (n == 2) {
			// (the pair's form, version 2's pictures unchanged: B's share by one noise)
			float f = 0;
			for (int k = 0; k < 4; k++) f += corner[k] == 1 ? bw[k] : 0;
			const float nz = Fbm(u * 3.1f + 11.0f, v * 3.1f + 5.0f, seed) - 0.5f;
			const float grain = Noise(u * 22.0f + 3.0f, v * 22.0f + 7.0f, seed ^ 0x9e37u) - 0.5f;
			out[1] = SmoothStep(0.30f, 0.70f, f + e * (0.65f * nz + 0.22f * grain));
			out[0] = 1 - out[1];
			return;
		}
		float sum = 0;
		const float p = 1 + 2.0f * e;
		for (int g = 0; g < n; g++) {
			float f = 0;
			for (int k = 0; k < 4; k++) f += corner[k] == g ? bw[k] : 0;
			const uint32_t sg = seed + (uint32_t)g * 0x51ed27u;
			const float nz = Fbm(u * 3.1f + 11.0f, v * 3.1f + 5.0f, sg) - 0.5f;
			const float grain = Noise(u * 22.0f + 3.0f, v * 22.0f + 7.0f, sg ^ 0x9e37u) - 0.5f;
			const float w = SmoothStep(0.30f, 0.70f, f + e * (0.65f * nz + 0.22f * grain)) + 1e-4f * f;
			out[g] = std::pow(w, p);
			sum += out[g];
		}
		for (int g = 0; g < n; g++) out[g] /= sum;
	}

	bool WriteStamp(const fs::path& p, const json& j)
	{
		std::ofstream f(p, std::ios::binary);
		f << j.dump(1);
		return (bool)f;
	}

	// a generated group's pictures: per picture its corners (indices into the groups) and the
	// variant (which of each group's own pictures it takes)
	struct Layout { int corner[4]; int variant; };
	std::vector<Layout> LayoutsFor(int n)
	{
		std::vector<Layout> out;
		if (n == 2)
			for (int k = 0; k < 8; k++) {
				Layout l;
				for (int c = 0; c < 4; c++) l.corner[c] = (PATTERNS[k] >> c) & 1;
				l.variant = k / 4;
				out.push_back(l);
			}
		else
			for (int v = 0; v < 2; v++)
				for (const auto& t : TRIPLE_LAYOUTS) {
					Layout l;
					std::memcpy(l.corner, t, sizeof(l.corner));
					l.variant = v;
					out.push_back(l);
				}
		return out;
	}

	// makes Generated\<name>.dds (or .png) and _BumpMap.png for the groups (false: a source is missing)
	bool Generate(const std::string& name, const std::vector<const TerrainTextureGroup*>& groups, int pairsVersion, const fs::path& dir)
	{
		const int n = (int)groups.size();
		std::vector<std::vector<Cell>> cells;
		for (const TerrainTextureGroup* g : groups) {
			cells.push_back(CellsOf(*g));
			if (cells.back().empty())
				return false;
		}
		// the sources: each variant takes the groups' first and second pictures
		std::map<std::string, Atlas> atlases;
		std::map<std::string, Bitmap> bumps;
		json sources = json::array();
		for (const std::vector<Cell>& list : cells)
			for (const Cell& c : list) {
				if (atlases.count(c.file)) continue;
				Atlas a;
				if (!LoadAtlas(c.file, a)) return false;
				Bitmap bm;
				if (!LoadBump(c.file, bm)) return false;
				sources.push_back({ { "file", a.source }, { "size", a.bmp.width } });
				atlases.emplace(c.file, std::move(a));
				bumps.emplace(c.file, std::move(bm));
			}
		const fs::path stampPath = dir / (name + ".json");
		const json stamp = { { "generator", TransitionGen::VERSION }, { "pairs", pairsVersion }, { "sources", sources } };
		{
			std::ifstream f(stampPath, std::ios::binary);
			std::stringstream ss;
			ss << f.rdbuf();
			const json old = json::parse(ss.str(), nullptr, false);
			if (f && !old.is_discarded() && old == stamp && (fs::exists(dir / (name + ".dds")) || fs::exists(dir / (name + ".png")))
				&& fs::exists(dir / (name + "_BumpMap.png")))
				return true; // made already from the same sources
		}
		// the cell size: the largest source's (an HD pack's 256 pixels, else 64)
		int cs = 64;
		for (const auto& [f, a] : atlases) cs = std::max(cs, a.bmp.width / 4);
		const int W = cs * 4;
		std::vector<uint8_t> rgba((size_t)W * W * 4, 255);
		std::vector<uint8_t> bump(256 * 256, 128);
		const std::vector<Layout> layouts = LayoutsFor(n);
		for (int k = 0; k < (int)layouts.size(); k++) {
			const Layout& l = layouts[k];
			std::vector<const Cell*> src;
			for (int g = 0; g < n; g++) src.push_back(&cells[g][l.variant % cells[g].size()]);
			const uint32_t seed = Hash(0x7a11u, (uint32_t)k, (uint32_t)std::hash<std::string>()(Lower(name)) & 0xffffffu);
			const int ox = (k % 4) * cs, oy = (k / 4) * cs;
			float w[3], px[3][4];
			for (int y = 0; y < cs; y++)
				for (int x = 0; x < cs; x++) {
					const float u = (x + 0.5f) / cs, v = (y + 0.5f) / cs;
					Shares(l.corner, n, u, v, seed, w);
					float o[3] = { 0, 0, 0 };
					for (int g = 0; g < n; g++) {
						Sample(atlases[src[g]->file].bmp, *src[g], u, v, px[g]);
						for (int c = 0; c < 3; c++) o[c] += px[g][c] * w[g];
					}
					uint8_t* d = &rgba[((size_t)(oy + y) * W + ox + x) * 4];
					for (int c = 0; c < 3; c++)
						d[c] = (uint8_t)std::clamp(o[c] + 0.5f, 0.0f, 255.0f);
				}
			// the bump map at the original size (64 a cell): the heights mixed alike
			for (int y = 0; y < 64; y++)
				for (int x = 0; x < 64; x++) {
					const float u = (x + 0.5f) / 64, v = (y + 0.5f) / 64;
					Shares(l.corner, n, u, v, seed, w);
					float h = 0;
					for (int g = 0; g < n; g++) {
						const Bitmap& b = bumps[src[g]->file];
						h += b.pixels[(size_t)(src[g]->y + y) * b.width + src[g]->x + x] * w[g];
					}
					bump[(size_t)((k / 4) * 64 + y) * 256 + (k % 4) * 64 + x] = (uint8_t)std::clamp(h + 0.5f, 0.0f, 255.0f);
				}
		}
		std::error_code ec;
		fs::create_directories(dir, ec);
		// the colour as BC7 with every mip level (the renderer uploads it as it is); a PNG when the
		// DDS can't be written. The heights as an 8-bit grey PNG (the renderer makes the normals)
		fs::remove(dir / (name + ".dds"), ec);
		fs::remove(dir / (name + ".png"), ec);
		if (!Bc7::WriteDds(PathToUtf8(dir / (name + ".dds")), rgba.data(), W, W)) {
			fs::remove(dir / (name + ".dds"), ec);
			if (!stbi_write_png(PathToUtf8(dir / (name + ".png")).c_str(), W, W, 4, rgba.data(), W * 4))
				return false;
		}
		if (!stbi_write_png(PathToUtf8(dir / (name + "_BumpMap.png")).c_str(), 256, 256, 1, bump.data(), 256))
			return false;
		WriteStamp(stampPath, stamp);
		printf("Generated transition %s: %d x %d pixels a picture (from %zu sources)\n", name.c_str(), cs, cs, sources.size());
		return true;
	}

	// adds the generated group of these base groups (2 or 3) to db, its pictures made first where
	// the cache lacks them; false: a group db doesn't have, a transition the game has, a source missing
	bool AddGroup(TerrainTextureDatabase& db, const std::vector<std::string>& names, int pairsVersion, const fs::path& dir)
	{
		std::vector<const TerrainTextureGroup*> groups;
		std::string name;
		for (const std::string& g : names) {
			auto it = db.groups.find(g);
			if (it == db.groups.end() || !it->second)
				return false;
			groups.push_back(it->second.get());
			name += (name.empty() ? "" : "-") + g;
		}
		if (db.groups.count(name) || (names.size() == 2 && db.groups.count(names[1] + "-" + names[0])))
			return false; // the game has this transition
		if (!Generate(name, groups, pairsVersion, dir)) {
			printf("Generated transition %s: a source picture is missing, left out\n", name.c_str());
			return false;
		}
		auto g = std::make_unique<TerrainTextureGroup>();
		g->name = name;
		const std::vector<Layout> layouts = LayoutsFor((int)names.size());
		for (int k = 0; k < (int)layouts.size(); k++) {
			auto t = std::make_unique<TerrainTexture>();
			t->id = k;
			t->grp = g.get();
			// (a .pcx that isn't there: the loaders take its .dds or .png replacement, Bitmap::replacementFile)
			t->file = "Generated\\" + name + ".pcx";
			t->startx = (k % 4) * 64;
			t->starty = (k / 4) * 64;
			t->width = t->height = 64;
			for (int c = 0; c < 4; c++)
				t->genCorners[c] = names[layouts[k].corner[c]];
			g->textures.emplace(k, std::move(t));
		}
		db.groups.emplace(name, std::move(g));
		return true;
	}
}

int TransitionGen::Add(TerrainTextureDatabase& db, const std::string& dbFile)
{
	std::lock_guard<std::mutex> lock(g_mutex);
	std::vector<std::string> warnings;
	const json data = MapMaterials::LoadMaterials(dbFile, json::object(), warnings);
	if (!data.contains("generatedTransitions") || !data["generatedTransitions"].is_object())
		return 0;
	const json& gt = data["generatedTransitions"];
	const int pairsVersion = gt.value("version", 1);
	const fs::path dir = g_userPath / "saved" / "Maps" / "Map_Textures" / "Generated";
	int added = 0;
	for (const char* list : { "pairs", "triples" }) {
		const size_t want = list[0] == 'p' ? 2 : 3;
		for (const json& p : gt.value(list, json::array())) {
			if (!p.is_array() || p.size() != want)
				continue;
			std::vector<std::string> names;
			for (const json& e : p)
				if (e.is_string()) names.push_back(e.get<std::string>());
			if (names.size() == want && AddGroup(db, names, pairsVersion, dir))
				added++;
		}
	}
	return added;
}
