// wkbai - the land of a map folder: the feature plan, the generator, heightmap and elevation
// import (MapTerrain.h; the RE repo's docs/engine/map-authoring.md, section 4 and step 17.4)
//
// The generator, in order:
//   1. the base: terrain.base, or the imported source layer (layers/source.png) blended by weight
//   2. the large forms added: islands, hills, ranges (with their passes lowered)
//   3. noise for the middle scale: warped fractal noise, ridged noise along the ranges
//   4. the forms set or cut: plateaus, valleys, basins, coasts, standalone passes, the rivers'
//      broad troughs, terraces, the shore flattened around the sea level
//   5. erosion: thermal (slopes settle to a talus angle), hydraulic (water drops carving gullies
//      and laying sediment in the hollows), thermal again lightly
//   6. the plan's details carved last: plateau tops and flat sites flattened again, ramps and
//      passes cut and filled at a walkable slope, river channels whose bed never rises
// Everything comes from the map's seed: the same plan gives the same heights.
//
// The hydraulic erosion is the particle ("droplet") method as described by Hans Theobald Beyer,
// "Implementation of a method for hydraulic erosion" (TU Munich, 2015), written here from that
// description; no outside code. The noise is gradient noise over a hash of our own.

#include "MapTerrain.h"
#include "MapFolder.h"
#include "file.h"
#include <stb_image.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <sstream>
#include <unordered_set>
#include <cstring>
#include <fstream>
#include <map>

namespace MapTerrain {
namespace {
	namespace fs = std::filesystem;
	constexpr float TILE = 5.0f;
	constexpr float PI = 3.14159265358979f;

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
	bool ReadJson(const fs::path& p, json& j)
	{
		std::string text;
		if (!ReadAll(p, text))
			return false;
		j = json::parse(text, nullptr, false);
		return !j.is_discarded();
	}
	double Ms(std::chrono::steady_clock::time_point t0)
	{
		return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
	}
	float Smooth(float e0, float e1, float x)
	{
		if (e0 == e1)
			return x < e0 ? 0.0f : 1.0f;
		const float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
		return t * t * (3 - 2 * t);
	}
	float Lerp(float a, float b, float t) { return a + (b - a) * t; }

	struct V2 { float x = 0, z = 0; };
	V2 operator+(V2 a, V2 b) { return { a.x + b.x, a.z + b.z }; }
	V2 operator-(V2 a, V2 b) { return { a.x - b.x, a.z - b.z }; }
	V2 operator*(V2 a, float k) { return { a.x * k, a.z * k }; }
	float Dot(V2 a, V2 b) { return a.x * b.x + a.z * b.z; }
	float Len(V2 a) { return std::sqrt(a.x * a.x + a.z * a.z); }
	V2 Norm(V2 a) { const float l = Len(a); return l > 1e-9f ? a * (1.0f / l) : V2{ 1, 0 }; }
	V2 Perp(V2 a) { return { -a.z, a.x }; }

	// ---- the vertex grid, in metres ----
	struct Grid {
		int w = 0, h = 0;
		std::vector<float> v;
		void init(int w_, int h_, float f) { w = w_; h = h_; v.assign((size_t)w * h, f); }
		float& at(int x, int z) { return v[(size_t)z * w + x]; }
		float at(int x, int z) const { return v[(size_t)z * w + x]; }
		float sample(float fx, float fz) const
		{
			fx = std::clamp(fx, 0.0f, (float)(w - 1));
			fz = std::clamp(fz, 0.0f, (float)(h - 1));
			const int x0 = std::min((int)fx, w - 2), z0 = std::min((int)fz, h - 2);
			const float tx = fx - x0, tz = fz - z0;
			return (at(x0, z0) * (1 - tx) + at(x0 + 1, z0) * tx) * (1 - tz) + (at(x0, z0 + 1) * (1 - tx) + at(x0 + 1, z0 + 1) * tx) * tz;
		}
	};

	// ---- the map ----
	struct Map {
		int W = 0, H = 0, E = 30; // tiles of the whole map, and the edge band
		float scale = 1.0f;
		uint32_t seed = 1;
		json raw;
		int gw() const { return W + 1; }
		int gh() const { return H + 1; }
		// a vertex's position in playable metres (x east, z north), and back
		V2 pos(int x, int z) const { return { (x - E) * TILE, (z - E) * TILE }; }
		float vx(float px) const { return px / TILE + E; }
		float vz(float pz) const { return pz / TILE + E; }
		float playW() const { return (W - 2 * E) * TILE; }
		float playH() const { return (H - 2 * E) * TILE; }
	};
	bool ReadMap(const fs::path& folder, Map& m, std::string& error)
	{
		if (!ReadJson(folder / "map.json", m.raw) || !m.raw.is_object()) {
			error = "map.json can't be read";
			return false;
		}
		if (m.raw.value("format", "") != "wkbai map") {
			error = "map.json: not a wkbai map";
			return false;
		}
		try {
			const json& size = m.raw.at("size");
			m.W = size.at("tiles").at(0).get<int>();
			m.H = size.at("tiles").at(1).get<int>();
			m.E = size.value("edge", 30);
			m.scale = m.raw.value("heights", json::object()).value("scale", 1.0f);
			m.seed = m.raw.value("seed", 1u);
		}
		catch (const std::exception& e) {
			error = std::string("map.json: ") + e.what();
			return false;
		}
		if (m.W < 2 * m.E + 16 || m.H < 2 * m.E + 16 || m.W > 1024 + 2 * m.E || m.H > 1024 + 2 * m.E || m.scale <= 0) {
			error = "map.json: size.tiles must be 16-1024 playable tiles a side plus the edge, heights.scale above 0";
			return false;
		}
		return true;
	}
	bool WriteMapJson(const fs::path& folder, const json& j)
	{
		return WriteAll(folder / "map.json", j.dump(1) + "\n");
	}

	// heights.png (or another picture of the grid's size) into metres; row 0 is the north edge
	bool ReadHeights(const fs::path& file, const Map& m, float scale, Grid& g)
	{
		int w = 0, h = 0;
		std::vector<uint16_t> px;
		if (!MapFolder::ReadPng16(file, w, h, px) || w < 2 || h < 2)
			return false;
		g.init(m.gw(), m.gh(), 0);
		for (int r = 0; r < m.gh(); r++)
			for (int x = 0; x < m.gw(); x++) {
				const float fx = x * (float)(w - 1) / (m.gw() - 1), fy = r * (float)(h - 1) / (m.gh() - 1);
				const int x0 = std::min((int)fx, w - 2), y0 = std::min((int)fy, h - 2);
				const float tx = fx - x0, ty = fy - y0;
				auto at = [&](int xx, int yy) { return (float)px[(size_t)yy * w + xx]; };
				const float v = (at(x0, y0) * (1 - tx) + at(x0 + 1, y0) * tx) * (1 - ty) + (at(x0, y0 + 1) * (1 - tx) + at(x0 + 1, y0 + 1) * tx) * ty;
				g.at(x, m.H - r) = v / 256.0f * scale;
			}
		return true;
	}
	// heights.hand.png as metres on top (zero where there is none); any: a vertex with a hand edit
	void ReadHand(const fs::path& folder, const Map& m, Grid& g, bool& any)
	{
		g.init(m.gw(), m.gh(), 0);
		any = false;
		int w = 0, h = 0;
		std::vector<uint16_t> px;
		if (!fs::exists(folder / "heights.hand.png") || !MapFolder::ReadPng16(folder / "heights.hand.png", w, h, px) || w != m.gw() || h != m.gh())
			return;
		for (int r = 0; r < h; r++)
			for (int x = 0; x < w; x++) {
				const int d = (int)px[(size_t)r * w + x] - MapFolder::HAND_ZERO;
				if (d) {
					any = true;
					g.at(x, m.H - r) = d / 256.0f * m.scale;
				}
			}
	}
	bool WriteHeights(const fs::path& file, const Map& m, const Grid& g, float scale)
	{
		std::vector<uint16_t> px((size_t)m.gw() * m.gh());
		for (int r = 0; r < m.gh(); r++)
			for (int x = 0; x < m.gw(); x++)
				px[(size_t)r * m.gw() + x] = (uint16_t)std::clamp((int)std::lround(g.at(x, m.H - r) / scale * 256.0f), 0, 65535);
		return MapFolder::WritePng16(file, m.gw(), m.gh(), px);
	}
	uint64_t HashGrid(const Grid& g)
	{
		uint64_t h = 1469598103934665603ull;
		for (float f : g.v) {
			const int32_t q = (int32_t)std::lround(f * 256.0f);
			for (int i = 0; i < 4; i++) { h ^= (uint8_t)(q >> (i * 8)); h *= 1099511628211ull; }
		}
		return h;
	}

	// ---- noise ----
	uint32_t Hash3(int32_t x, int32_t z, uint32_t s)
	{
		uint32_t h = s * 0x9E3779B1u + 0x7F4A7C15u;
		h ^= (uint32_t)x * 0x85EBCA77u; h = (h << 13) | (h >> 19); h *= 0xC2B2AE3Du;
		h ^= (uint32_t)z * 0x27D4EB2Fu; h = (h << 17) | (h >> 15); h *= 0x165667B1u;
		h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12; h *= 0x297A2D39u; h ^= h >> 15;
		return h;
	}
	struct GradTable {
		float x[16], z[16];
		GradTable() { for (int i = 0; i < 16; i++) { x[i] = std::cos((i + 0.5f) * PI / 8); z[i] = std::sin((i + 0.5f) * PI / 8); } }
	};
	const GradTable g_grad;
	// gradient noise, about -1..1
	float Noise(float x, float z, uint32_t s)
	{
		const float flx = std::floor(x), flz = std::floor(z);
		const int ix = (int)flx, iz = (int)flz;
		const float fx = x - flx, fz = z - flz;
		auto g = [&](int dx, int dz) {
			const uint32_t k = Hash3(ix + dx, iz + dz, s) & 15;
			return g_grad.x[k] * (fx - dx) + g_grad.z[k] * (fz - dz);
		};
		const float ux = fx * fx * fx * (fx * (fx * 6 - 15) + 10), uz = fz * fz * fz * (fz * (fz * 6 - 15) + 10);
		return Lerp(Lerp(g(0, 0), g(1, 0), ux), Lerp(g(0, 1), g(1, 1), ux), uz) * 1.5f;
	}
	// fractal noise, about -1..1; each octave twice as fine and turned
	float Fbm(float x, float z, int octaves, uint32_t s, float gain = 0.5f)
	{
		float sum = 0, amp = 1, norm = 0;
		for (int i = 0; i < octaves; i++) {
			sum += amp * Noise(x, z, s + (uint32_t)i * 1013u);
			norm += amp;
			const float nx = 1.6f * x - 1.2f * z + 17.3f, nz = 1.2f * x + 1.6f * z - 9.1f;
			x = nx; z = nz;
			amp *= gain;
		}
		return sum / norm;
	}
	// ridged fractal noise, 0..1: sharp crests, the finer octaves mostly on the crests
	float Ridged(float x, float z, int octaves, uint32_t s)
	{
		float sum = 0, amp = 1, norm = 0, prev = 1;
		for (int i = 0; i < octaves; i++) {
			float n = 1 - std::abs(Noise(x, z, s + (uint32_t)i * 7919u));
			n *= n;
			sum += n * amp * prev;
			norm += amp;
			prev = std::clamp(n * 1.5f, 0.0f, 1.0f);
			const float nx = 1.6f * x - 1.2f * z + 5.7f, nz = 1.2f * x + 1.6f * z + 3.3f;
			x = nx; z = nz;
			amp *= 0.5f;
		}
		return sum / norm;
	}
	V2 Warp(V2 p, float amp, float scale, uint32_t s)
	{
		if (amp <= 0 || scale <= 0)
			return p;
		return { p.x + amp * Fbm(p.x / scale, p.z / scale, 3, s), p.z + amp * Fbm(p.x / scale + 31.7f, p.z / scale - 12.9f, 3, s + 77) };
	}

	// ---- shapes ----
	struct Shape {
		enum Kind { NONE, ELLIPSE, POLYGON, LINE } kind = NONE;
		V2 c;
		float rx = 0, rz = 0, cosA = 1, sinA = 0;
		std::vector<V2> pts;
		std::vector<float> cum; // a line's length up to each point
		float length = 0;
		float size() const
		{
			if (kind == ELLIPSE)
				return std::min(rx, rz);
			float x0 = 1e30f, x1 = -1e30f, z0 = 1e30f, z1 = -1e30f;
			for (V2 p : pts) { x0 = std::min(x0, p.x); x1 = std::max(x1, p.x); z0 = std::min(z0, p.z); z1 = std::max(z1, p.z); }
			return kind == POLYGON ? 0.5f * std::min(x1 - x0, z1 - z0) : length;
		}
		V2 centre() const
		{
			if (kind == ELLIPSE)
				return c;
			V2 s;
			for (V2 p : pts) s = s + p;
			return pts.empty() ? s : s * (1.0f / pts.size());
		}
	};
	bool ReadPoint(const json& j, V2& p)
	{
		if (!j.is_array() || j.size() < 2 || !j[0].is_number() || !j[1].is_number())
			return false;
		p = { j[0].get<float>(), j[1].get<float>() };
		return true;
	}
	bool ReadPoints(const json& j, std::vector<V2>& out)
	{
		out.clear();
		if (!j.is_array())
			return false;
		for (const json& e : j) {
			V2 p;
			if (!ReadPoint(e, p))
				return false;
			out.push_back(p);
		}
		return true;
	}
	void MakeLine(Shape& s)
	{
		s.cum.assign(s.pts.size(), 0.0f);
		for (size_t i = 1; i < s.pts.size(); i++)
			s.cum[i] = s.cum[i - 1] + Len(s.pts[i] - s.pts[i - 1]);
		s.length = s.cum.empty() ? 0 : s.cum.back();
	}
	float Num(const json& j, const char* k, float def)
	{
		auto it = j.find(k);
		return it != j.end() && it->is_number() ? it->get<float>() : def;
	}
	// a number, or [from, to] along a line (u 0-1)
	float NumAlong(const json& j, const char* k, float def, float u)
	{
		auto it = j.find(k);
		if (it == j.end())
			return def;
		if (it->is_number())
			return it->get<float>();
		if (it->is_array() && it->size() >= 2 && (*it)[0].is_number() && (*it)[1].is_number())
			return Lerp((*it)[0].get<float>(), (*it)[1].get<float>(), u);
		return def;
	}
	Shape ShapeOf(const json& f, const Map& m)
	{
		Shape s;
		if (f.contains("line") && ReadPoints(f["line"], s.pts) && s.pts.size() >= 2) {
			s.kind = Shape::LINE;
			MakeLine(s);
			return s;
		}
		if (f.contains("from") && f.contains("to")) {
			V2 a, b;
			if (ReadPoint(f["from"], a) && ReadPoint(f["to"], b)) {
				s.kind = Shape::LINE;
				s.pts = { a, b };
				MakeLine(s);
				return s;
			}
		}
		if (f.contains("at")) {
			const json& at = f["at"];
			if (at.is_array() && !at.empty() && at[0].is_array()) {
				if (ReadPoints(at, s.pts) && s.pts.size() >= 3)
					s.kind = Shape::POLYGON;
				return s;
			}
			if (ReadPoint(at, s.c)) {
				s.kind = Shape::ELLIPSE;
				const json r = f.value("radius", json(150.0));
				if (r.is_array() && r.size() >= 2) { s.rx = r[0].get<float>(); s.rz = r[1].get<float>(); }
				else if (r.is_number()) s.rx = s.rz = r.get<float>();
				s.rx = std::max(s.rx, 5.0f);
				s.rz = std::max(s.rz, 5.0f);
				const float a = Num(f, "angle", 0) * PI / 180;
				s.cosA = std::cos(a);
				s.sinA = std::sin(a);
			}
			return s;
		}
		// a side of the map ("side": "east"): a line along it, inside the map
		(void)m;
		return s;
	}
	struct LineHit { float d = 1e30f, t = 0; V2 point, tangent{ 1, 0 }; };
	LineHit Nearest(const Shape& s, V2 p)
	{
		LineHit best;
		for (size_t i = 0; i + 1 < s.pts.size(); i++) {
			const V2 a = s.pts[i], ab = s.pts[i + 1] - a;
			const float l2 = std::max(Dot(ab, ab), 1e-9f);
			const float t = std::clamp(Dot(p - a, ab) / l2, 0.0f, 1.0f);
			const V2 q = a + ab * t;
			const float d = Len(p - q);
			if (d < best.d) {
				best.d = d;
				best.t = s.cum[i] + t * std::sqrt(l2);
				best.point = q;
				best.tangent = Norm(ab);
			}
		}
		return best;
	}
	V2 LineAt(const Shape& s, float t, V2* tangent = nullptr)
	{
		for (size_t i = 0; i + 1 < s.pts.size(); i++)
			if (t <= s.cum[i + 1] || i + 2 == s.pts.size()) {
				const float seg = std::max(s.cum[i + 1] - s.cum[i], 1e-6f);
				const float u = std::clamp((t - s.cum[i]) / seg, 0.0f, 1.0f);
				if (tangent)
					*tangent = Norm(s.pts[i + 1] - s.pts[i]);
				return s.pts[i] + (s.pts[i + 1] - s.pts[i]) * u;
			}
		return s.pts.empty() ? V2{} : s.pts[0];
	}
	// signed distance to an area, negative inside (an ellipse approximately); a line: its distance
	float AreaSd(const Shape& s, V2 p)
	{
		switch (s.kind) {
		case Shape::ELLIPSE: {
			const V2 d = p - s.c;
			const V2 q = { d.x * s.cosA + d.z * s.sinA, -d.x * s.sinA + d.z * s.cosA };
			const float k0 = Len({ q.x / s.rx, q.z / s.rz }), k1 = Len({ q.x / (s.rx * s.rx), q.z / (s.rz * s.rz) });
			return k1 > 1e-9f ? k0 * (k0 - 1) / k1 : -std::min(s.rx, s.rz);
		}
		case Shape::POLYGON: {
			float d = 1e30f;
			bool inside = false;
			for (size_t i = 0, j = s.pts.size() - 1; i < s.pts.size(); j = i++) {
				const V2 a = s.pts[j], b = s.pts[i], ab = b - a;
				const float t = std::clamp(Dot(p - a, ab) / std::max(Dot(ab, ab), 1e-9f), 0.0f, 1.0f);
				d = std::min(d, Len(p - (a + ab * t)));
				if ((a.z > p.z) != (b.z > p.z) && p.x < a.x + (p.z - a.z) / (b.z - a.z) * (b.x - a.x))
					inside = !inside;
			}
			return inside ? -d : d;
		}
		case Shape::LINE:
			return Nearest(s, p).d;
		default:
			return 1e30f;
		}
	}

	// ---- the plan ----
	struct Feature {
		int index = 0;
		std::string kind, id, role;
		json j;
		Shape shape;
		uint32_t seed = 0;
		float warpAmp = 0, warpScale = 1;
		// the point a feature's mask is computed at: the position warped, so outlines aren't geometric
		V2 warped(V2 p) const { return Warp(p, warpAmp, warpScale, seed + 101); }
	};
	struct PassSite {
		V2 p, across;
		float height = 0, width = 40, maxDeg = 14, reach = 150;
		int feature = 0;
	};
	struct Site { // plateaus and flat sites, flattened again after the erosion
		int feature = 0;
		float height = 0, rim = 25;
		bool level = false; // height not given: the mean of what is there
	};

	const char* KINDS[] = { "hill", "range", "ridge", "plateau", "valley", "basin", "coast", "island", "river", "pass", "terraces", "flat" };

	// the side of a map as a direction from the centre: east +x, north +z
	bool SideDir(const std::string& side, V2& out)
	{
		if (side == "east") out = { 1, 0 };
		else if (side == "west") out = { -1, 0 };
		else if (side == "north") out = { 0, 1 };
		else if (side == "south") out = { 0, -1 };
		else return false;
		return true;
	}

	// A walkable way along control points: a path wandering a little sideways, its heights the
	// land's own limited to tanMax along it (the mean of the cut-only and the fill-only limits, so
	// as much is cut as filled), put on the land as a change that fades out to the sides: the land's
	// shape across the way stays, half of its cross-slope taken off in the middle. The path's points
	// (every 5 m) are returned.
	std::vector<V2> Way(const Map& m, Grid& g, const std::vector<V2>& ctrl, float tanMax, float width, float blend, uint32_t seed, float wander)
	{
		Shape s;
		s.kind = Shape::LINE;
		s.pts = ctrl;
		MakeLine(s);
		if (s.length < 5)
			return {};
		const int n = std::max(2, (int)std::ceil(s.length / 5.0f) + 1);
		std::vector<V2> pts(n);
		std::vector<float> h(n), lo(n), hi(n), pr(n);
		for (int i = 0; i < n; i++) {
			const float t = s.length * i / (n - 1), u = t / s.length;
			V2 tan;
			const V2 p = LineAt(s, t, &tan);
			pts[i] = p + Perp(tan) * (wander * width * 1.5f * Fbm(t / 160.0f, 0.5f, 2, seed) * std::sin(PI * u));
			h[i] = g.sample(m.vx(pts[i].x), m.vz(pts[i].z));
		}
		std::vector<float> step(n, 5.0f);
		for (int i = 1; i < n; i++) step[i] = std::max(1.0f, Len(pts[i] - pts[i - 1]));
		lo = h; hi = h;
		for (int i = 1; i < n; i++) { lo[i] = std::min(lo[i], lo[i - 1] + tanMax * step[i] * 0.92f); hi[i] = std::max(hi[i], hi[i - 1] - tanMax * step[i] * 0.92f); }
		for (int i = n - 2; i >= 0; i--) { lo[i] = std::min(lo[i], lo[i + 1] + tanMax * step[i + 1] * 0.92f); hi[i] = std::max(hi[i], hi[i + 1] - tanMax * step[i + 1] * 0.92f); }
		for (int i = 0; i < n; i++) pr[i] = (lo[i] + hi[i]) * 0.5f;
		for (int pass = 0; pass < 2; pass++) {
			std::vector<float> q = pr;
			for (int i = 1; i + 1 < n; i++) q[i] = (pr[i - 1] + pr[i] + pr[i + 1]) / 3;
			pr = q;
		}
		float x0 = 1e30f, x1 = -1e30f, z0 = 1e30f, z1 = -1e30f;
		for (V2 p : pts) { x0 = std::min(x0, p.x); x1 = std::max(x1, p.x); z0 = std::min(z0, p.z); z1 = std::max(z1, p.z); }
		const float reach = width / 2 + blend;
		const int vx0 = std::max(0, (int)std::floor(m.vx(x0 - reach))), vx1 = std::min(m.gw() - 1, (int)std::ceil(m.vx(x1 + reach)));
		const int vz0 = std::max(0, (int)std::floor(m.vz(z0 - reach))), vz1 = std::min(m.gh() - 1, (int)std::ceil(m.vz(z1 + reach)));
		for (int z = vz0; z <= vz1; z++)
			for (int x = vx0; x <= vx1; x++) {
				const V2 p = m.pos(x, z);
				float bd = 1e30f, delta = 0, prof = 0, beyond = 0;
				for (int i = 0; i + 1 < n; i++) {
					const V2 a = pts[i], ab = pts[i + 1] - a;
					const float l2 = std::max(Dot(ab, ab), 1e-6f);
					const float along = Dot(p - a, ab) / l2;
					const float t = std::clamp(along, 0.0f, 1.0f);
					const float d = Len(p - (a + ab * t));
					if (d < bd) {
						bd = d;
						delta = Lerp(pr[i] - h[i], pr[i + 1] - h[i + 1], t);
						prof = Lerp(pr[i], pr[i + 1], t);
						beyond = (i == 0 && along < 0) || (i + 2 == n && along > 1) ? d : 0.0f;
					}
				}
				float w = 1 - Smooth(width / 2, width / 2 + blend, bd);
				if (beyond > 0) w = 1 - Smooth(0, blend, beyond);
				if (w <= 0) continue;
				float v = g.at(x, z) + delta * w;
				const float core = 1 - Smooth(width * 0.2f, width * 0.55f, bd);
				v = Lerp(v, prof, 0.5f * core * w);
				g.at(x, z) = v;
			}
		return pts;
	}
	json WayJson(const std::vector<V2>& pts)
	{
		json j = json::array();
		for (size_t i = 0; i < pts.size(); i += 4) j.push_back({ std::round(pts[i].x * 10) / 10, std::round(pts[i].z * 10) / 10 });
		if (!pts.empty() && (pts.size() - 1) % 4) j.push_back({ std::round(pts.back().x * 10) / 10, std::round(pts.back().z * 10) / 10 });
		return j;
	}

	// a river's course: its line every 5 m, wandering sideways by "meander" (the same in both carvings)
	std::vector<V2> Course(const Feature& f, float width)
	{
		std::vector<V2> c;
		const Shape& s = f.shape;
		const float meander = Num(f.j, "meander", 0.35f);
		const int n = std::max(2, (int)std::ceil(s.length / 5.0f) + 1);
		for (int i = 0; i < n; i++) {
			const float t = s.length * i / (n - 1);
			V2 tan;
			const V2 p = LineAt(s, t, &tan);
			// no wandering at the ends, so the river leaves where the plan says
			const float ends = Smooth(0, 80, t) * Smooth(0, 80, s.length - t);
			// bends of about ten widths, and a slower wander
			const float wb = std::max(width, 12.0f);
			const float off = meander * (wb * 4.0f * Fbm(t / (wb * 9.0f), 0.37f, 2, f.seed + 5) + wb * 6.0f * Fbm(t / 600.0f, 4.1f, 2, f.seed + 6)) * ends;
			c.push_back(p + Perp(tan) * off);
		}
		return c;
	}
	// carves a channel along the course, its bed never rising from the first point to the last;
	// only ever lowers the land. Returns the bed's heights.
	// lakeAt (optional, per point): the level of the lake the course is on there (else below
	// -1e29): the bed is held a little under the lake's level there instead of following its
	// floor, and falls on from that beyond it (a river through a lake no longer cuts a trench
	// through its rim at the floor's depth, which drained it)
	std::vector<float> CarveChannel(const Map& m, Grid& g, const std::vector<V2>& c, float width, float depth, float bank, float bankDeg,
		const std::vector<float>& fordWeight, uint32_t seed, float minBed = -1e30f, const std::vector<float>* lakeAt = nullptr, float gradeM = 0)
	{
		std::vector<float> bed(c.size());
		float prev = 1e30f;
		auto onLake = [&](size_t i) { return lakeAt && i < lakeAt->size() && (*lakeAt)[i] > -1e29f; };
		for (size_t i = 0; i < c.size(); i++) {
			if (onLake(i)) {
				bed[i] = prev = (*lakeAt)[i] - depth * 0.5f;
				continue;
			}
			// the ground: the lowest of the course point and its neighbours across (so a bank doesn't lift the bed)
			float ground = g.sample(m.vx(c[i].x), m.vz(c[i].z));
			const V2 tan = Norm(c[std::min(i + 1, c.size() - 1)] - c[i > 0 ? i - 1 : 0]);
			for (float o : { -0.3f, 0.3f }) {
				const V2 q = c[i] + Perp(tan) * (o * width);
				ground = std::min(ground, g.sample(m.vx(q.x), m.vz(q.z)));
			}
			float b = ground - depth;
			if (i > 0)
				b = std::min(b, prev - 0.01f);
			bed[i] = prev = b;
		}
		// a gentle smoothing of the bed, kept falling
		std::vector<float> sm = bed;
		for (size_t i = 0; i < bed.size(); i++) {
			float s = 0; int n = 0;
			for (int k = -3; k <= 3; k++) {
				const long j = (long)i + k;
				if (j >= 0 && j < (long)bed.size()) { s += bed[j]; n++; }
			}
			sm[i] = s / n;
		}
		for (size_t i = 0; i < sm.size(); i++) {
			if (onLake(i))
				sm[i] = bed[i];
			else if (i > 0)
				sm[i] = std::min(sm[i], sm[i - 1] - 0.005f);
		}
		// gradeM: the bed smoothed over that distance into an even fall (land from contours, as OS
		// Terrain 50, has terraced valley floors: the bed followed them as flats and sudden drops,
		// a fall at every terrace); kept falling, a lake's stretch kept as it is
		if (gradeM > 5.0f) {
			const int k = std::max(1, (int)std::lround(gradeM / 5.0f));
			std::vector<float> gs = sm;
			for (int pass = 0; pass < 2; pass++) {
				for (size_t i = 0; i < sm.size(); i++) {
					if (onLake(i)) { gs[i] = sm[i]; continue; }
					float s = 0; int n = 0;
					// (the window kept even about the point: near the head, one looking only
					// downstream had dragged a mountain stream's top far below the ground)
					const int kk = std::min({ k, (int)i, (int)(sm.size() - 1 - i) });
					for (int j = -kk; j <= kk; j++) {
						const long q = (long)i + j;
						if (q >= 0 && q < (long)sm.size() && !onLake((size_t)q)) { s += sm[q]; n++; }
					}
					gs[i] = s / n;
				}
				for (size_t i = 1; i < gs.size(); i++)
					if (!onLake(i)) gs[i] = std::min(gs[i], gs[i - 1] - 0.005f);
				sm = gs;
			}
		}
		// never below minBed (a river reaching the sea: its bed stays above the sea, which would
		// otherwise run up the channel into the valley)
		for (float& b : sm) b = std::max(b, minBed);
		bed = sm;

		float x0 = 1e30f, x1 = -1e30f, z0 = 1e30f, z1 = -1e30f;
		for (V2 p : c) { x0 = std::min(x0, p.x); x1 = std::max(x1, p.x); z0 = std::min(z0, p.z); z1 = std::max(z1, p.z); }
		const float reach = width * 2 + bank + 160;
		const int vx0 = std::max(0, (int)m.vx(x0 - reach)), vx1 = std::min(m.gw() - 1, (int)m.vx(x1 + reach) + 1);
		const int vz0 = std::max(0, (int)m.vz(z0 - reach)), vz1 = std::min(m.gh() - 1, (int)m.vz(z1 + reach) + 1);
		const float tanBank = std::tan(bankDeg * PI / 180), tanFord = std::tan(7.0f * PI / 180);
		// the nearest point of the course per vertex within reach of it: each piece of the course
		// looks only at the vertices near it (a long course on a large map: millions of vertices
		// times thousands of pieces otherwise)
		const int bw = vx1 - vx0 + 1, bh = vz1 - vz0 + 1;
		std::vector<float> nearD((size_t)bw * bh, 1e30f), nearB((size_t)bw * bh, 0), nearF((size_t)bw * bh, 0);
		for (size_t i = 0; i + 1 < c.size(); i++) {
			const V2 a = c[i], ab = c[i + 1] - a;
			const int sx0 = std::max(vx0, (int)m.vx(std::min(a.x, c[i + 1].x) - reach)), sx1 = std::min(vx1, (int)m.vx(std::max(a.x, c[i + 1].x) + reach) + 1);
			const int sz0 = std::max(vz0, (int)m.vz(std::min(a.z, c[i + 1].z) - reach)), sz1 = std::min(vz1, (int)m.vz(std::max(a.z, c[i + 1].z) + reach) + 1);
			for (int z = sz0; z <= sz1; z++)
				for (int x = sx0; x <= sx1; x++) {
					const V2 p = m.pos(x, z);
					const float t = std::clamp(Dot(p - a, ab) / std::max(Dot(ab, ab), 1e-9f), 0.0f, 1.0f);
					const float d = Len(p - (a + ab * t));
					const size_t k = (size_t)(z - vz0) * bw + (x - vx0);
					if (d < nearD[k]) {
						nearD[k] = d;
						nearB[k] = Lerp(bed[i], bed[i + 1], t);
						nearF[k] = fordWeight.empty() ? 0 : Lerp(fordWeight[i], fordWeight[i + 1], t);
					}
				}
		}
		for (int z = vz0; z <= vz1; z++)
			for (int x = vx0; x <= vx1; x++) {
				const V2 p = m.pos(x, z);
				const size_t k = (size_t)(z - vz0) * bw + (x - vx0);
				if (nearD[k] > reach) continue;
				const float bd = nearD[k], bb = nearB[k], bf = nearF[k];
				float hw = width / 2 * (1 + 0.2f * Noise(p.x / 60.0f, p.z / 60.0f, seed + 9) + 0.35f * Noise(p.x / 170.0f, p.z / 170.0f, seed + 10));
				// a river with water (lakeAt given): its floor at least a tile wide, so that a row of
				// vertices always lies under its water (narrow stretches, the width's noise down to
				// half, had left the drawn ground closing over the water: the stream in pieces)
				if (lakeAt)
					hw = std::max(hw, 5.0f);
				float prof;
				if (bd < hw)
					prof = bb + depth * 0.3f * (bd / hw) * (bd / hw);
				else {
					// the banks rough: their width and height wander
					const float bk = bank * (0.7f + 0.6f * std::clamp(0.5f + Noise(p.x / 45.0f, p.z / 45.0f, seed + 11), 0.0f, 1.0f));
					prof = bb + depth * 0.3f + depth * 0.7f * Smooth(hw, hw + bk, bd) + std::max(0.0f, bd - hw - bk) * tanBank
						+ depth * 0.35f * Fbm(p.x / 22.0f, p.z / 22.0f, 2, seed + 12) * Smooth(hw * 0.8f, hw + bk, bd);
				}
				if (bf > 0) {
					// a ford: shallow, wide, with gentle banks
					const float fw = hw * 2;
					const float pf = bb + 0.8f * std::min(1.0f, bd / fw) + std::max(0.0f, bd - fw) * tanFord;
					prof = Lerp(prof, std::min(prof, pf), bf * (1 - Smooth(fw + 15, fw + 60, bd)));
				}
				// faded out towards the reach (the banks rise at bankDeg out to it: on land steeper than
				// that, a mountainside next to a glen, the cut had stopped dead at the reach, a cliff)
				const float fade = 1 - Smooth(reach * 0.6f, reach, bd);
				if (prof < g.at(x, z))
					g.at(x, z) += (prof - g.at(x, z)) * fade;
			}
		return bed;
	}

	// ---- erosion ----
	void Thermal(Grid& g, int iterations, float talusDeg, float rate)
	{
		const float t1 = std::tan(talusDeg * PI / 180) * TILE, t2 = t1 * 1.41421356f;
		const int w = g.w, h = g.h;
		std::vector<float> d(g.v.size());
		const int dx[8] = { 1, -1, 0, 0, 1, 1, -1, -1 }, dz[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };
		for (int it = 0; it < iterations; it++) {
			std::fill(d.begin(), d.end(), 0.0f);
			for (int z = 0; z < h; z++)
				for (int x = 0; x < w; x++) {
					const float c = g.at(x, z);
					float diff[8], sum = 0, maxd = 0;
					for (int k = 0; k < 8; k++) {
						const int nx = x + dx[k], nz = z + dz[k];
						diff[k] = 0;
						if (nx < 0 || nz < 0 || nx >= w || nz >= h)
							continue;
						const float e = c - g.at(nx, nz) - (k < 4 ? t1 : t2);
						if (e > 0) { diff[k] = e; sum += e; maxd = std::max(maxd, e); }
					}
					if (maxd <= 0)
						continue;
					const float move = rate * maxd * 0.5f;
					for (int k = 0; k < 8; k++)
						if (diff[k] > 0) {
							const float share = move * diff[k] / sum;
							d[(size_t)(z + dz[k]) * w + x + dx[k]] += share;
							d[(size_t)z * w + x] -= share;
						}
				}
			for (size_t i = 0; i < d.size(); i++)
				g.v[i] += d[i];
		}
	}
	struct XorShift {
		uint32_t s;
		explicit XorShift(uint32_t seed) : s(seed ? seed : 0x1234567u) {}
		uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
		float unit() { return (next() >> 8) * (1.0f / 16777216.0f); }
	};
	void Hydraulic(Grid& g, int drops, uint32_t seed, float strength, float maxCutM, float maxFillM, float seaM)
	{
		const float seaCells = seaM / TILE;
		const int w = g.w, h = g.h;
		// in cell units (a height of 1 = 5 m), so the slopes are true gradients
		std::vector<float> hm(g.v.size());
		for (size_t i = 0; i < hm.size(); i++)
			hm[i] = g.v[i] / TILE;
		const int R = 2;
		std::vector<int> bx, bz;
		std::vector<float> bw;
		float bsum = 0;
		for (int oz = -R; oz <= R; oz++)
			for (int ox = -R; ox <= R; ox++) {
				const float d = std::sqrt((float)(ox * ox + oz * oz));
				if (d <= R) { bx.push_back(ox); bz.push_back(oz); bw.push_back(R + 0.5f - d); bsum += R + 0.5f - d; }
			}
		for (float& v : bw) v /= bsum;
		const float inertia = 0.05f, capacityK = 4.0f, minCap = 0.01f, erode = 0.3f * std::clamp(strength, 0.0f, 3.0f), deposit = 0.3f,
			evaporate = 0.015f, gravity = 4.0f;
		const int life = 64;
		// the land is lowered by at most maxCutM and raised by at most maxFillM: water leaving the
		// map takes its sediment along, and without a base level a long channel would deepen without end
		std::vector<float> lowest(hm.size()), highest(hm.size());
		for (size_t i = 0; i < hm.size(); i++) { lowest[i] = hm[i] - maxCutM / TILE; highest[i] = hm[i] + maxFillM / TILE; }
		XorShift rng(seed * 2654435761u + 17);
		auto heightGrad = [&](float x, float z, float& gx, float& gz) {
			const int ix = (int)x, iz = (int)z;
			const float fx = x - ix, fz = z - iz;
			const size_t i = (size_t)iz * w + ix;
			const float h00 = hm[i], h10 = hm[i + 1], h01 = hm[i + w], h11 = hm[i + w + 1];
			gx = (h10 - h00) * (1 - fz) + (h11 - h01) * fz;
			gz = (h01 - h00) * (1 - fx) + (h11 - h10) * fx;
			return h00 * (1 - fx) * (1 - fz) + h10 * fx * (1 - fz) + h01 * (1 - fx) * fz + h11 * fx * fz;
		};
		for (int d = 0; d < drops; d++) {
			float x = rng.unit() * (w - 1.001f), z = rng.unit() * (h - 1.001f);
			float dirX = 0, dirZ = 0, speed = 1, water = 1, sed = 0;
			for (int step = 0; step < life; step++) {
				const int ix = (int)x, iz = (int)z;
				const float fx = x - ix, fz = z - iz;
				float gx, gz;
				const float hh = heightGrad(x, z, gx, gz);
				// the drop reaches the sea: what it carries is spread out by the sea (nothing below
				// the sea level is eroded, and a delta piled on one vertex looks like a fringe)
				if (hh < seaCells)
					break;
				dirX = dirX * inertia - gx * (1 - inertia);
				dirZ = dirZ * inertia - gz * (1 - inertia);
				const float l = std::sqrt(dirX * dirX + dirZ * dirZ);
				if (l < 1e-7f)
					break;
				dirX /= l; dirZ /= l;
				const float nx = x + dirX, nz = z + dirZ;
				if (nx < 0 || nz < 0 || nx >= w - 1 || nz >= h - 1)
					break;
				float ngx, ngz;
				const float dh = heightGrad(nx, nz, ngx, ngz) - hh;
				const float cap = std::max(-dh * speed * water * capacityK, minCap);
				const size_t i = (size_t)iz * w + ix;
				if (sed > cap || dh > 0) {
					const float amt = dh > 0 ? std::min(dh, sed) : (sed - cap) * deposit;
					sed -= amt;
					const float wt[4] = { (1 - fx) * (1 - fz), fx * (1 - fz), (1 - fx) * fz, fx * fz };
					const size_t at[4] = { i, i + 1, i + w, i + w + 1 };
					for (int k = 0; k < 4; k++)
						hm[at[k]] = std::min(hm[at[k]] + amt * wt[k], std::max(hm[at[k]], highest[at[k]]));
				}
				else {
					const float amt = std::min((cap - sed) * erode, -dh);
					for (size_t k = 0; k < bw.size(); k++) {
						const int cx = ix + bx[k], cz = iz + bz[k];
						if (cx < 0 || cz < 0 || cx >= w || cz >= h)
							continue;
						const size_t c = (size_t)cz * w + cx;
						const float e = std::min(amt * bw[k], std::max(0.0f, hm[c] - lowest[c]));
						hm[c] -= e;
						sed += e;
					}
				}
				speed = std::min(3.0f, std::sqrt(std::max(0.0f, speed * speed - dh * gravity)));
				water *= 1 - evaporate;
				x = nx; z = nz;
			}
		}
		// the change smoothed a little (drops leave pits and spikes a vertex wide)
		std::vector<float> delta(hm.size()), blur(hm.size());
		for (size_t i = 0; i < hm.size(); i++)
			delta[i] = hm[i] * TILE - g.v[i];
		for (int z = 0; z < h; z++)
			for (int x = 0; x < w; x++) {
				float s = 0, n = 0;
				for (int oz = -1; oz <= 1; oz++)
					for (int ox = -1; ox <= 1; ox++) {
						const int cx = x + ox, cz = z + oz;
						if (cx < 0 || cz < 0 || cx >= w || cz >= h) continue;
						const float k = (ox == 0 && oz == 0) ? 4.0f : (ox == 0 || oz == 0) ? 2.0f : 1.0f;
						s += delta[(size_t)cz * w + cx] * k;
						n += k;
					}
				blur[(size_t)z * w + x] = s / n;
			}
		for (size_t i = 0; i < hm.size(); i++)
			g.v[i] += Lerp(delta[i], blur[i], 0.6f);
	}

	// ---- the sea: the tiles below the sea level joined to a large body of them ----
	struct SeaBody { int tiles = 0; int deepX = 0, deepZ = 0; float deep = 1e30f; };
	// per tile 1 when under the sea; bodies of at least minTiles
	std::vector<uint8_t> SeaTiles(const Map& m, const Grid& g, float sea, int minTiles, std::vector<SeaBody>* bodies)
	{
		const int W = m.W, H = m.H;
		std::vector<int> label((size_t)W * H, -1);
		std::vector<uint8_t> below((size_t)W * H, 0), out((size_t)W * H, 0);
		for (int z = 0; z < H; z++)
			for (int x = 0; x < W; x++) {
				const float a = (g.at(x, z) + g.at(x + 1, z) + g.at(x, z + 1) + g.at(x + 1, z + 1)) * 0.25f;
				below[(size_t)z * W + x] = a < sea;
			}
		std::vector<int> stack;
		int next = 0;
		for (int s = 0; s < W * H; s++) {
			if (!below[s] || label[s] >= 0)
				continue;
			SeaBody b;
			std::vector<int> members;
			stack.push_back(s);
			label[s] = next;
			while (!stack.empty()) {
				const int t = stack.back();
				stack.pop_back();
				members.push_back(t);
				const int x = t % W, z = t / W;
				const float a = (g.at(x, z) + g.at(x + 1, z) + g.at(x, z + 1) + g.at(x + 1, z + 1)) * 0.25f;
				if (a < b.deep) { b.deep = a; b.deepX = x; b.deepZ = z; }
				const int nb[4] = { x > 0 ? t - 1 : -1, x < W - 1 ? t + 1 : -1, z > 0 ? t - W : -1, z < H - 1 ? t + W : -1 };
				for (int n : nb)
					if (n >= 0 && below[n] && label[n] < 0) { label[n] = next; stack.push_back(n); }
			}
			b.tiles = (int)members.size();
			if (b.tiles >= minTiles) {
				for (int t : members) out[t] = 1;
				if (bodies) bodies->push_back(b);
			}
			next++;
		}
		return out;
	}

	json StatsOf(const Map& m, const Grid& g, bool hasSea, float sea)
	{
		const int W = m.W, H = m.H, E = m.E;
		float lo = 1e30f, hi = -1e30f, alo = 1e30f, ahi = -1e30f;
		double sum = 0;
		size_t n = 0;
		for (int z = 0; z <= H; z++)
			for (int x = 0; x <= W; x++) {
				const float v = g.at(x, z);
				alo = std::min(alo, v); ahi = std::max(ahi, v);
				if (x >= E && x <= W - E && z >= E && z <= H - E) { lo = std::min(lo, v); hi = std::max(hi, v); sum += v; n++; }
			}
		std::vector<uint8_t> seaT;
		std::vector<SeaBody> bodies;
		if (hasSea)
			seaT = SeaTiles(m, g, sea, 100, &bodies);
		const float edges[11] = { 0, 5, 10, 15, 20, 25, 30, 35, 40, 45, 90 };
		std::vector<int> bins(10, 0);
		const float lim45 = TILE * std::tan(45.0f * PI / 180), lim30 = TILE * std::tan(30.0f * PI / 180), lim60 = TILE * std::tan(60.0f * PI / 180);
		int land = 0, w45 = 0, w30 = 0, w60 = 0, seaP = 0;
		const int pw = W - 2 * E, ph = H - 2 * E;
		std::vector<uint8_t> walk((size_t)pw * ph, 0);
		for (int z = E; z < H - E; z++)
			for (int x = E; x < W - E; x++) {
				const float v[4] = { g.at(x, z), g.at(x + 1, z), g.at(x, z + 1), g.at(x + 1, z + 1) };
				const float rise = *std::max_element(v, v + 4) - *std::min_element(v, v + 4);
				const float deg = std::atan(rise / TILE) * 180 / PI;
				int b = 0;
				while (b < 9 && deg >= edges[b + 1]) b++;
				bins[b]++;
				if (hasSea && seaT[(size_t)z * W + x]) { seaP++; continue; }
				land++;
				w45 += rise <= lim45;
				w30 += rise <= lim30;
				w60 += rise <= lim60;
				walk[(size_t)(z - E) * pw + (x - E)] = rise <= lim45;
			}
		// the largest region a 45-degree unit can walk across
		int largest = 0;
		{
			std::vector<uint8_t> seen(walk.size(), 0);
			std::vector<int> st;
			for (int s = 0; s < pw * ph; s++) {
				if (!walk[s] || seen[s]) continue;
				int count = 0;
				st.push_back(s); seen[s] = 1;
				while (!st.empty()) {
					const int t = st.back(); st.pop_back(); count++;
					const int x = t % pw, z = t / pw;
					const int nb[4] = { x > 0 ? t - 1 : -1, x < pw - 1 ? t + 1 : -1, z > 0 ? t - pw : -1, z < ph - 1 ? t + pw : -1 };
					for (int k : nb) if (k >= 0 && walk[k] && !seen[k]) { seen[k] = 1; st.push_back(k); }
				}
				largest = std::max(largest, count);
			}
		}
		const double tiles = std::max(1, pw * ph);
		json share = json::array();
		for (int b : bins) share.push_back(std::round(b / tiles * 10000) / 10000);
		json out;
		out["heights"] = { { "minM", lo }, { "maxM", hi }, { "meanM", n ? sum / n : 0.0 }, { "wholeMapMinM", alo }, { "wholeMapMaxM", ahi } };
		out["slopes"] = { { "binsDeg", json(std::vector<float>(edges, edges + 11)) }, { "tileShare", share },
			{ "measure", "per playable tile: atan(highest corner - lowest corner, 5 m), the game's own MAX_GRADIENT test" } };
		out["walkable"] = { { "share", w45 / tiles }, { "share30deg", w30 / tiles }, { "share60deg", w60 / tiles },
			{ "landShare", land / tiles }, { "ofLand", land ? (double)w45 / land : 0.0 },
			{ "largestRegionShare", largest / tiles }, { "largestRegionOfWalkable", w45 ? (double)largest / w45 : 0.0 },
			{ "note", "share: playable tiles a 45-degree unit (most units) can stand on, sea left out; 30deg/60deg for the other MAX_GRADIENTs" } };
		if (hasSea) {
			json b = json::array();
			for (const SeaBody& s : bodies) b.push_back({ { "tiles", s.tiles }, { "deepestM", s.deep } });
			out["sea"] = { { "levelM", sea }, { "playableShare", seaP / tiles }, { "bodies", b } };
		}
		return out;
	}

	// options and json files from the command line
	void OutputJson(const json& j, const std::string& out)
	{
		if (!out.empty())
			WriteAll(PathFromUtf8(out), j.dump(1) + "\n");
		else
			printf("AUTHOR-JSON-BEGIN\n%s\nAUTHOR-JSON-END\n", j.dump(1).c_str());
	}

	// ---- rasters: heightmap pictures and elevation files ----
	struct Raster {
		int w = 0, h = 0;
		std::vector<float> v;
		std::vector<uint8_t> valid;
		bool geo = false;
		bool metric = false; // lon0/lat0/dlon/dlat in a grid's metres (eastings, northings), not degrees
		double lon0 = 0, lat0 = 0, dlon = 0, dlat = 0; // pixel (0, 0)'s centre and the step to the next pixel
		std::string file, format;
		float at(int x, int y) const { return v[(size_t)y * w + x]; }
		bool ok(int x, int y) const { return valid.empty() || valid[(size_t)y * w + x]; }
		// bilinear, clamped to the picture, invalid pixels left out (NaN when none is valid)
		float sample(double x, double y) const
		{
			x = std::clamp(x, 0.0, (double)w - 1);
			y = std::clamp(y, 0.0, (double)h - 1);
			const int x0 = std::min((int)x, w - 2 < 0 ? 0 : w - 2), y0 = std::min((int)y, h - 2 < 0 ? 0 : h - 2);
			const int x1 = std::min(x0 + 1, w - 1), y1 = std::min(y0 + 1, h - 1);
			const float tx = (float)(x - x0), ty = (float)(y - y0);
			float s = 0, ws = 0;
			const int xs[4] = { x0, x1, x0, x1 }, ys[4] = { y0, y0, y1, y1 };
			const float wt[4] = { (1 - tx) * (1 - ty), tx * (1 - ty), (1 - tx) * ty, tx * ty };
			for (int k = 0; k < 4; k++)
				if (ok(xs[k], ys[k])) { s += at(xs[k], ys[k]) * wt[k]; ws += wt[k]; }
			return ws > 1e-6f ? s / ws : NAN;
		}
	};
	std::string Lower(std::string s) { for (char& c : s) c = (char)tolower((unsigned char)c); return s; }

	bool LoadPng(const fs::path& p, Raster& r, std::string& error)
	{
		std::string data;
		if (!ReadAll(p, data)) { error = "can't read " + PathToUtf8(p); return false; }
		int n = 0;
		if (stbi_is_16_bit_from_memory((const stbi_uc*)data.data(), (int)data.size())) {
			stbi_us* px = stbi_load_16_from_memory((const stbi_uc*)data.data(), (int)data.size(), &r.w, &r.h, &n, 1);
			if (!px) { error = "not a picture: " + PathToUtf8(p); return false; }
			r.v.assign(px, px + (size_t)r.w * r.h);
			stbi_image_free(px);
			r.format = "png16";
		}
		else {
			stbi_uc* px = stbi_load_from_memory((const stbi_uc*)data.data(), (int)data.size(), &r.w, &r.h, &n, 1);
			if (!px) { error = "not a picture: " + PathToUtf8(p); return false; }
			r.v.assign(px, px + (size_t)r.w * r.h);
			stbi_image_free(px);
			r.format = "png8";
		}
		return true;
	}
	bool LoadRaw(const fs::path& p, const json& o, Raster& r, std::string& error)
	{
		std::string data;
		if (!ReadAll(p, data)) { error = "can't read " + PathToUtf8(p); return false; }
		const int bits = o.value("bits", 16);
		const bool be = o.value("bigEndian", false), sgn = o.value("signed", false);
		const size_t bpp = bits / 8;
		if (bits != 8 && bits != 16) { error = "raw: bits 8 or 16"; return false; }
		r.w = o.value("width", 0);
		r.h = o.value("height", 0);
		const size_t count = data.size() / bpp;
		if (r.w <= 0 || r.h <= 0) {
			r.w = r.h = (int)std::lround(std::sqrt((double)count));
			if ((size_t)r.w * r.h != count) { error = "raw: give width and height (the file isn't square)"; return false; }
		}
		if ((size_t)r.w * r.h * bpp > data.size()) { error = "raw: the file is smaller than width x height"; return false; }
		r.v.resize((size_t)r.w * r.h);
		const uint8_t* d = (const uint8_t*)data.data();
		for (size_t i = 0; i < r.v.size(); i++) {
			if (bits == 8) { r.v[i] = sgn ? (float)(int8_t)d[i] : (float)d[i]; continue; }
			const uint16_t u = be ? (uint16_t)(d[i * 2] << 8 | d[i * 2 + 1]) : (uint16_t)(d[i * 2 + 1] << 8 | d[i * 2]);
			r.v[i] = sgn ? (float)(int16_t)u : (float)u;
		}
		r.format = "raw" + std::to_string(bits);
		return true;
	}
	// SRTM .hgt: big-endian signed 16-bit metres, square, row 0 the north edge; the name gives the
	// south-west corner (N47E011.hgt: 47-48 N, 11-12 E); -32768 is a void
	bool HgtCorner(const std::string& name, int& lat, int& lon)
	{
		const std::string n = Lower(fs::path(name).filename().string());
		for (size_t i = 0; i + 7 <= n.size(); i++)
			if ((n[i] == 'n' || n[i] == 's') && isdigit((unsigned char)n[i + 1]) && isdigit((unsigned char)n[i + 2]) && (n[i + 3] == 'e' || n[i + 3] == 'w')
				&& isdigit((unsigned char)n[i + 4]) && isdigit((unsigned char)n[i + 5]) && isdigit((unsigned char)n[i + 6])) {
				lat = (n[i + 1] - '0') * 10 + (n[i + 2] - '0');
				lon = (n[i + 4] - '0') * 100 + (n[i + 5] - '0') * 10 + (n[i + 6] - '0');
				if (n[i] == 's') lat = -lat;
				if (n[i + 3] == 'w') lon = -lon;
				return true;
			}
		return false;
	}
	bool LoadHgt(const fs::path& p, Raster& r, std::string& error)
	{
		std::string data;
		if (!ReadAll(p, data)) { error = "can't read " + PathToUtf8(p); return false; }
		const int n = (int)std::lround(std::sqrt(data.size() / 2.0));
		if ((size_t)n * n * 2 != data.size() || n < 2) { error = PathToUtf8(p) + ": not an SRTM .hgt (square, 16-bit)"; return false; }
		int lat = 0, lon = 0;
		if (!HgtCorner(PathToUtf8(p), lat, lon)) { error = PathToUtf8(p) + ": the name doesn't say the tile (N47E011.hgt)"; return false; }
		r.w = r.h = n;
		r.v.resize((size_t)n * n);
		r.valid.assign((size_t)n * n, 1);
		const uint8_t* d = (const uint8_t*)data.data();
		for (size_t i = 0; i < r.v.size(); i++) {
			const int16_t s = (int16_t)(d[i * 2] << 8 | d[i * 2 + 1]);
			r.v[i] = s;
			if (s == -32768) r.valid[i] = 0;
		}
		r.geo = true;
		r.lon0 = lon; r.lat0 = lat + 1;
		r.dlon = 1.0 / (n - 1); r.dlat = -1.0 / (n - 1);
		r.format = "srtm-hgt";
		return true;
	}

	// TIFF's LZW (MSB-first codes, 9-12 bits, the early change)
	bool Lzw(const uint8_t* in, size_t n, std::vector<uint8_t>& out, size_t expect)
	{
		out.clear();
		out.reserve(expect);
		std::vector<int> prefix(4096, -1);
		std::vector<uint8_t> suffix(4096), first(4096);
		std::vector<int> length(4096, 0);
		for (int i = 0; i < 256; i++) { suffix[i] = (uint8_t)i; first[i] = (uint8_t)i; length[i] = 1; }
		int next = 258, width = 9, old = -1;
		size_t bitpos = 0;
		std::vector<uint8_t> tmp;
		auto emit = [&](int code) {
			const int L = length[code];
			const size_t at = out.size();
			out.resize(at + L);
			for (int c = code, k = L - 1; k >= 0; k--) { out[at + k] = suffix[c]; c = prefix[c]; }
		};
		while (bitpos + width <= n * 8) {
			int code = 0;
			for (int b = 0; b < width; b++, bitpos++)
				code = (code << 1) | ((in[bitpos >> 3] >> (7 - (bitpos & 7))) & 1);
			if (code == 257)
				break;
			if (code == 256) { next = 258; width = 9; old = -1; continue; }
			if (old < 0) {
				if (code > 255) return false;
				emit(code);
				old = code;
				continue;
			}
			if (code < next) {
				emit(code);
				if (next < 4096) { prefix[next] = old; suffix[next] = first[code]; first[next] = first[old]; length[next] = length[old] + 1; next++; }
			}
			else if (code == next) {
				if (next >= 4096) return false;
				prefix[next] = old; suffix[next] = first[old]; first[next] = first[old]; length[next] = length[old] + 1; next++;
				emit(code);
			}
			else
				return false;
			old = code;
			if (next + 1 >= (1 << width) && width < 12)
				width++;
		}
		return true;
	}
	bool LoadTiff(const fs::path& p, Raster& r, std::string& error)
	{
		std::string data;
		if (!ReadAll(p, data) || data.size() < 8) { error = "can't read " + PathToUtf8(p); return false; }
		const uint8_t* d = (const uint8_t*)data.data();
		const size_t size = data.size();
		const bool le = d[0] == 'I';
		if (!((d[0] == 'I' && d[1] == 'I') || (d[0] == 'M' && d[1] == 'M'))) { error = PathToUtf8(p) + ": not a TIFF"; return false; }
		auto u16 = [&](size_t o) -> uint32_t { if (o + 2 > size) return 0; return le ? (uint32_t)(d[o] | d[o + 1] << 8) : (uint32_t)(d[o] << 8 | d[o + 1]); };
		auto u32 = [&](size_t o) -> uint32_t { if (o + 4 > size) return 0; return le ? (uint32_t)d[o] | (uint32_t)d[o + 1] << 8 | (uint32_t)d[o + 2] << 16 | (uint32_t)d[o + 3] << 24
			: (uint32_t)d[o] << 24 | (uint32_t)d[o + 1] << 16 | (uint32_t)d[o + 2] << 8 | (uint32_t)d[o + 3]; };
		auto f64 = [&](size_t o) { uint64_t v = 0; for (int i = 0; i < 8; i++) v |= (uint64_t)d[o + (le ? i : 7 - i)] << (i * 8); double f; memcpy(&f, &v, 8); return f; };
		if (u16(2) == 43) { error = PathToUtf8(p) + ": BigTIFF isn't read"; return false; }
		if (u16(2) != 42) { error = PathToUtf8(p) + ": not a TIFF"; return false; }
		const size_t ifd = u32(4);
		const int count = (int)u16(ifd);
		std::map<int, std::vector<double>> tags;
		std::map<int, std::string> ascii;
		for (int i = 0; i < count; i++) {
			const size_t e = ifd + 2 + (size_t)i * 12;
			const int tag = (int)u16(e), type = (int)u16(e + 2);
			const uint32_t n = u32(e + 4);
			const int sz = type == 3 ? 2 : type == 4 ? 4 : type == 12 ? 8 : type == 11 ? 4 : type == 2 || type == 1 || type == 7 ? 1 : type == 16 ? 8 : 0;
			if (!sz) continue;
			const size_t at = (size_t)n * sz <= 4 ? e + 8 : u32(e + 8);
			if (at + (size_t)n * sz > size) continue;
			if (type == 2) { ascii[tag] = std::string((const char*)d + at, n); continue; }
			std::vector<double> vals;
			if (n > 50000000) continue;
			vals.reserve(n);
			for (uint32_t k = 0; k < n; k++) {
				const size_t o = at + (size_t)k * sz;
				if (type == 3) vals.push_back(u16(o));
				else if (type == 4) vals.push_back(u32(o));
				else if (type == 12) vals.push_back(f64(o));
				else if (type == 11) { uint32_t b = u32(o); float f; memcpy(&f, &b, 4); vals.push_back(f); }
				else if (type == 1 || type == 7) vals.push_back(d[o]);
			}
			tags[tag] = std::move(vals);
		}
		auto tagv = [&](int t, double def) { auto it = tags.find(t); return it != tags.end() && !it->second.empty() ? it->second[0] : def; };
		r.w = (int)tagv(256, 0);
		r.h = (int)tagv(257, 0);
		const int bps = (int)tagv(258, 1), comp = (int)tagv(259, 1), spp = (int)tagv(277, 1), pred = (int)tagv(317, 1), fmt = (int)tagv(339, 1);
		const int planar = (int)tagv(284, 1);
		if (r.w <= 0 || r.h <= 0 || (size_t)r.w * r.h > 400000000ull) { error = PathToUtf8(p) + ": no size"; return false; }
		if (spp != 1 && planar != 2) { error = PathToUtf8(p) + ": " + std::to_string(spp) + " samples a pixel (an elevation file has 1)"; return false; }
		if (!(bps == 8 || bps == 16 || bps == 32 || bps == 64)) { error = PathToUtf8(p) + ": " + std::to_string(bps) + " bits a sample"; return false; }
		if (!(comp == 1 || comp == 5 || comp == 8 || comp == 32946)) { error = PathToUtf8(p) + ": compression " + std::to_string(comp) + " isn't read (none, LZW, deflate are)"; return false; }
		const bool tiled = tags.count(322) && tags.count(324);
		const int bw = tiled ? (int)tagv(322, 0) : r.w, bh = tiled ? (int)tagv(323, 0) : (int)std::min<double>(tagv(278, r.h), r.h);
		const std::vector<double>& offs = tags[tiled ? 324 : 273];
		const std::vector<double>& lens = tags[tiled ? 325 : 279];
		if (bw <= 0 || bh <= 0 || offs.empty() || lens.size() < offs.size()) { error = PathToUtf8(p) + ": no strips or tiles"; return false; }
		const int across = (r.w + bw - 1) / bw, down = (r.h + bh - 1) / bh;
		const size_t bytesPer = bps / 8, blockBytes = (size_t)bw * bh * bytesPer;
		r.v.assign((size_t)r.w * r.h, 0.0f);
		std::vector<uint8_t> buf;
		for (int bi = 0; bi < across * down && bi < (int)offs.size(); bi++) {
			const size_t o = (size_t)offs[bi], n = (size_t)lens[bi];
			if (o + n > size) { error = PathToUtf8(p) + ": a block past the file's end"; return false; }
			if (comp == 1)
				buf.assign(d + o, d + o + n);
			else if (comp == 5) {
				if (!Lzw(d + o, n, buf, blockBytes)) { error = PathToUtf8(p) + ": bad LZW data"; return false; }
			}
			else {
				int outlen = 0;
				char* z = stbi_zlib_decode_malloc_guesssize_headerflag((const char*)d + o, (int)n, (int)blockBytes, &outlen, 1);
				if (!z) { error = PathToUtf8(p) + ": bad deflate data"; return false; }
				buf.assign((uint8_t*)z, (uint8_t*)z + outlen);
				free(z);
			}
			const int rows = tiled ? bh : std::min(bh, r.h - bi * bh);
			buf.resize(std::max(buf.size(), (size_t)bw * rows * bytesPer), 0);
			const size_t rowBytes = (size_t)bw * bytesPer;
			// the predictors, row by row
			if (pred == 2) {
				for (int y = 0; y < rows; y++) {
					uint8_t* row = buf.data() + y * rowBytes;
					for (int x = 1; x < bw; x++) {
						if (bytesPer == 1) row[x] = (uint8_t)(row[x] + row[x - 1]);
						else if (bytesPer == 2) {
							uint8_t* a = row + x * 2; const uint8_t* b = row + (x - 1) * 2;
							const uint16_t va = le ? (uint16_t)(a[0] | a[1] << 8) : (uint16_t)(a[0] << 8 | a[1]);
							const uint16_t vb = le ? (uint16_t)(b[0] | b[1] << 8) : (uint16_t)(b[0] << 8 | b[1]);
							const uint16_t s = (uint16_t)(va + vb);
							if (le) { a[0] = (uint8_t)s; a[1] = (uint8_t)(s >> 8); } else { a[0] = (uint8_t)(s >> 8); a[1] = (uint8_t)s; }
						}
						else if (bytesPer == 4) {
							uint8_t* a = row + x * 4; const uint8_t* b = row + (x - 1) * 4;
							uint32_t va = 0, vb = 0;
							for (int k = 0; k < 4; k++) { va |= (uint32_t)a[le ? k : 3 - k] << (k * 8); vb |= (uint32_t)b[le ? k : 3 - k] << (k * 8); }
							const uint32_t s = va + vb;
							for (int k = 0; k < 4; k++) a[le ? k : 3 - k] = (uint8_t)(s >> (k * 8));
						}
					}
				}
			}
			else if (pred == 3) {
				// floating point: the bytes differenced along the row, then each sample's bytes in
				// planes, the most significant first; put back in the file's byte order
				std::vector<uint8_t> tmp(rowBytes);
				for (int y = 0; y < rows; y++) {
					uint8_t* row = buf.data() + y * rowBytes;
					for (size_t i = 1; i < rowBytes; i++)
						row[i] = (uint8_t)(row[i] + row[i - 1]);
					for (int x = 0; x < bw; x++)
						for (size_t k = 0; k < bytesPer; k++)
							tmp[x * bytesPer + (le ? bytesPer - 1 - k : k)] = row[k * bw + x];
					memcpy(row, tmp.data(), rowBytes);
				}
			}
			const int bx0 = (bi % across) * bw, by0 = (bi / across) * bh;
			for (int y = 0; y < rows; y++)
				for (int x = 0; x < bw; x++) {
					const int px = bx0 + x, py = by0 + y;
					if (px >= r.w || py >= r.h) continue;
					const uint8_t* s = buf.data() + (size_t)y * rowBytes + (size_t)x * bytesPer;
					uint64_t u = 0;
					for (size_t k = 0; k < bytesPer; k++) u |= (uint64_t)s[le ? k : bytesPer - 1 - k] << (k * 8);
					double v;
					if (fmt == 3 && bytesPer == 4) { uint32_t b = (uint32_t)u; float f; memcpy(&f, &b, 4); v = f; }
					else if (fmt == 3 && bytesPer == 8) { memcpy(&v, &u, 8); }
					else if (fmt == 2) v = bytesPer == 1 ? (double)(int8_t)u : bytesPer == 2 ? (double)(int16_t)u : (double)(int32_t)u;
					else v = (double)u;
					r.v[(size_t)py * r.w + px] = (float)v;
				}
		}
		r.format = "tiff";
		// GDAL's nodata
		if (ascii.count(42113)) {
			const double nod = atof(ascii[42113].c_str());
			r.valid.assign(r.v.size(), 1);
			for (size_t i = 0; i < r.v.size(); i++)
				if (r.v[i] == (float)nod || std::isnan(r.v[i])) r.valid[i] = 0;
		}
		// GeoTIFF: the tie point and the pixel size (pixel is area: the tie point is pixel (0, 0)'s corner)
		if (tags.count(33922) && tags[33922].size() >= 6 && tags.count(33550) && tags[33550].size() >= 2) {
			const std::vector<double>& t = tags[33922];
			const std::vector<double>& s = tags[33550];
			bool point = false;
			if (tags.count(34735)) {
				const std::vector<double>& k = tags[34735];
				for (size_t i = 4; i + 3 < k.size(); i += 4)
					if ((int)k[i] == 1025 && (int)k[i + 1] == 0) point = (int)k[i + 3] == 2;
			}
			r.geo = true;
			r.dlon = s[0];
			r.dlat = -s[1];
			r.lon0 = t[3] - t[0] * s[0] + (point ? 0 : s[0] / 2);
			r.lat0 = t[4] + t[1] * s[1] - (point ? 0 : s[1] / 2);
		}
		return true;
	}
	// An Esri ASCII grid (OS Terrain 50's .asc tiles: ncols, nrows, xllcorner, yllcorner, cellsize,
	// optional NODATA_value, then the rows from the north): its position in the grid's own metres
	// (British National Grid for OS data), so lon0/lat0 are eastings and northings here (r.metric)
	bool LoadAsc(const fs::path& p, Raster& r, std::string& error)
	{
		std::string data;
		if (!ReadAll(p, data)) { error = "can't read " + PathToUtf8(p); return false; }
		std::istringstream in(data);
		double xll = 0, yll = 0, cs = 0, nodata = -1e30;
		bool centre = false;
		std::string key;
		for (int k = 0; k < 6; k++) {
			const std::streampos at = in.tellg();
			if (!(in >> key)) break;
			const std::string lk = Lower(key);
			if (lk == "ncols") in >> r.w;
			else if (lk == "nrows") in >> r.h;
			else if (lk == "xllcorner") in >> xll;
			else if (lk == "yllcorner") in >> yll;
			else if (lk == "xllcenter") { in >> xll; centre = true; }
			else if (lk == "yllcenter") { in >> yll; centre = true; }
			else if (lk == "cellsize") in >> cs;
			else if (lk == "nodata_value") in >> nodata;
			else { in.seekg(at); break; }
		}
		if (r.w <= 0 || r.h <= 0 || cs <= 0) { error = PathToUtf8(p) + ": not an ASCII grid"; return false; }
		r.v.resize((size_t)r.w * r.h);
		r.valid.assign(r.v.size(), 1);
		for (size_t i = 0; i < r.v.size(); i++) {
			double v = 0;
			if (!(in >> v)) { error = PathToUtf8(p) + ": the grid ends early"; return false; }
			r.v[i] = (float)v;
			if (v == nodata) r.valid[i] = 0;
		}
		const double half = centre ? 0 : cs / 2;
		r.geo = true;
		r.metric = true;
		r.lon0 = xll + half;
		r.lat0 = yll + half + (r.h - 1) * cs;
		r.dlon = cs;
		r.dlat = -cs;
		r.format = "asc";
		return true;
	}
	bool LoadRaster(const fs::path& p, const json& o, Raster& r, std::string& error)
	{
		const std::string ext = Lower(p.extension().string());
		r.file = PathToUtf8(p);
		if (!fs::exists(p)) { error = "no file " + PathToUtf8(p); return false; }
		if (ext == ".png") return LoadPng(p, r, error);
		if (ext == ".hgt") return LoadHgt(p, r, error);
		if (ext == ".asc") return LoadAsc(p, r, error);
		if (ext == ".tif" || ext == ".tiff") return LoadTiff(p, r, error);
		if (ext == ".raw" || ext == ".r16" || ext == ".r8" || ext == ".bin") {
			json ro = o.value("raw", json::object());
			if (ext == ".r8" && !ro.contains("bits")) ro["bits"] = 8;
			return LoadRaw(p, ro, r, error);
		}
		error = "unknown heightmap kind " + ext + " (.png, .raw/.r16/.r8, .hgt, .tif)";
		return false;
	}

	// The base layer: a grid of metres written as layers/source.png with its range in map.json
	bool WriteSource(const fs::path& folder, Map& m, const Grid& g, const json& meta, json& sourceOut)
	{
		float lo = 1e30f, hi = -1e30f;
		for (float v : g.v) { lo = std::min(lo, v); hi = std::max(hi, v); }
		if (hi <= lo) hi = lo + 1;
		std::vector<uint16_t> px((size_t)m.gw() * m.gh());
		for (int r = 0; r < m.gh(); r++)
			for (int x = 0; x < m.gw(); x++)
				px[(size_t)r * m.gw() + x] = (uint16_t)std::clamp((int)std::lround((g.at(x, m.H - r) - lo) / (hi - lo) * 65535.0f), 0, 65535);
		std::error_code ec;
		fs::create_directories(folder / "layers", ec);
		if (!MapFolder::WritePng16(folder / "layers" / "source.png", m.gw(), m.gh(), px))
			return false;
		sourceOut = json::object();
		sourceOut["file"] = "layers/source.png";
		sourceOut["min"] = lo;
		sourceOut["max"] = hi;
		sourceOut["weight"] = 1.0;
		for (auto it = meta.begin(); it != meta.end(); ++it)
			sourceOut[it.key()] = it.value();
		return true;
	}
	bool ReadSource(const fs::path& folder, const Map& m, const json& src, Grid& g, std::string& error)
	{
		const std::string file = src.value("file", "layers/source.png");
		int w = 0, h = 0;
		std::vector<uint16_t> px;
		if (!MapFolder::ReadPng16(folder / PathFromUtf8(file), w, h, px) || w < 2 || h < 2) {
			error = "terrain.source: can't read " + file;
			return false;
		}
		const float lo = Num(src, "min", 0), hi = Num(src, "max", 100);
		g.init(m.gw(), m.gh(), 0);
		for (int r = 0; r < m.gh(); r++)
			for (int x = 0; x < m.gw(); x++) {
				const float fx = x * (float)(w - 1) / (m.gw() - 1), fy = r * (float)(h - 1) / (m.gh() - 1);
				const int x0 = std::min((int)fx, w - 2), y0 = std::min((int)fy, h - 2);
				const float tx = fx - x0, ty = fy - y0;
				auto at = [&](int xx, int yy) { return (float)px[(size_t)yy * w + xx]; };
				const float v = (at(x0, y0) * (1 - tx) + at(x0 + 1, y0) * tx) * (1 - ty) + (at(x0, y0 + 1) * (1 - tx) + at(x0 + 1, y0 + 1) * tx) * ty;
				g.at(x, m.H - r) = lo + v / 65535.0f * (hi - lo);
			}
		return true;
	}
	// a references entry for the terrain's source, replacing the last one
	void SetReference(json& raw, const json& ref)
	{
		json refs = raw.value("references", json::array());
		if (!refs.is_array()) refs = json::array();
		json kept = json::array();
		for (const json& r : refs)
			if (!(r.is_object() && r.value("use", "") == "terrain.source"))
				kept.push_back(r);
		kept.push_back(ref);
		raw["references"] = kept;
	}
	// a terrain block for an imported base: the land as it is, until the agent adds to the plan
	void DefaultImportTerrain(json& raw, float base)
	{
		json t = raw.value("terrain", json::object());
		if (!t.is_object()) t = json::object();
		if (!t.contains("base")) t["base"] = base;
		if (!t.contains("noise")) t["noise"] = { { "amplitude", 0 } };
		if (!t.contains("erosion")) t["erosion"] = { { "hydraulic", 0 }, { "thermal", 0 } };
		raw["terrain"] = t;
	}

	// the vertices of the grid a heightmap or an area covers
	void Cover(const Map& m, const std::string& cover, int& X0, int& X1, int& Z0, int& Z1)
	{
		if (cover == "whole") { X0 = 0; X1 = m.W; Z0 = 0; Z1 = m.H; }
		else { X0 = m.E; X1 = m.W - m.E; Z0 = m.E; Z1 = m.H - m.E; }
	}
	double Mirror(double u, double n)
	{
		if (n <= 1) return 0;
		const double period = 2 * (n - 1);
		u = std::fmod(u, period);
		if (u < 0) u += period;
		return u > n - 1 ? period - u : u;
	}
} // namespace

// ================= the generator =================

bool Generate(const fs::path& folder, json& out, std::string& error, uint32_t seedOverride)
{
	const auto t0 = std::chrono::steady_clock::now();
	Map m;
	if (!ReadMap(folder, m, error))
		return false;
	const uint32_t seed = seedOverride ? seedOverride : m.seed;
	json terrain = m.raw.value("terrain", json::object());
	if (!terrain.is_object()) terrain = json::object();
	const json featuresJ = m.raw.value("features", json::array());
	json warnings = json::array();
	json timing = json::object();

	const float base = Num(terrain, "base", 20.0f);
	bool hasSea = terrain.contains("seaLevel") && terrain["seaLevel"].is_number();
	float sea = hasSea ? terrain["seaLevel"].get<float>() : 0.0f;

	// the plan's features
	std::vector<Feature> feats;
	if (featuresJ.is_array())
		for (size_t i = 0; i < featuresJ.size(); i++) {
			const json& fj = featuresJ[i];
			if (!fj.is_object()) { warnings.push_back("features[" + std::to_string(i) + "]: not an object"); continue; }
			Feature f;
			f.index = (int)i;
			f.j = fj;
			f.kind = Lower(fj.value("kind", ""));
			if (f.kind == "ridge") f.kind = "range";
			f.id = fj.value("id", "");
			f.role = fj.value("role", "");
			bool known = false;
			for (const char* k : KINDS) known = known || f.kind == k;
			if (!known) { warnings.push_back("features[" + std::to_string(i) + "]: unknown kind \"" + f.kind + "\""); continue; }
			f.seed = seed * 7919u + (uint32_t)i * 104729u + 1;
			f.shape = ShapeOf(fj, m);
			const float irr = std::clamp(Num(fj, "irregularity", f.kind == "plateau" ? 0.8f : 0.5f), 0.0f, 1.5f);
			const float size = f.shape.kind == Shape::LINE ? Num(fj, "width", 200) * 0.5f : f.shape.kind == Shape::NONE ? 300.0f : f.shape.size();
			f.warpAmp = irr * size * 0.45f;
			f.warpScale = std::max(40.0f, size * 0.9f);
			const bool needsShape = f.kind != "coast" && f.kind != "terraces";
			if (needsShape && f.shape.kind == Shape::NONE) { warnings.push_back("features[" + std::to_string(i) + "] (" + f.kind + "): no \"at\" or \"line\""); continue; }
			if ((f.kind == "range" || f.kind == "valley" || f.kind == "river") && f.shape.kind != Shape::LINE) {
				warnings.push_back("features[" + std::to_string(i) + "] (" + f.kind + "): needs a \"line\" (or \"from\" and \"to\")");
				continue;
			}
			if ((f.kind == "island" || f.kind == "coast") && !hasSea) {
				// an island or a coast needs a sea: at the base level, the sea floor below it
				hasSea = true;
				sea = base;
				warnings.push_back("terrain.seaLevel not given: the sea is at the base level, " + std::to_string(base) + " m");
			}
			feats.push_back(f);
		}

	const int gw = m.gw(), gh = m.gh();
	Grid g;
	g.init(gw, gh, base);
	// 1. the base: the imported source layer, blended
	if (terrain.contains("source") && terrain["source"].is_object()) {
		Grid src;
		std::string e;
		if (!ReadSource(folder, m, terrain["source"], src, e)) { error = e; return false; }
		const float w = std::clamp(Num(terrain["source"], "weight", 1.0f), 0.0f, 1.0f);
		for (size_t i = 0; i < g.v.size(); i++) g.v[i] = Lerp(g.v[i], src.v[i], w);
	}
	// fields for the noise: its strength (1 = terrain.noise.amplitude) and the ridged relief in metres
	Grid amp, relief;
	amp.init(gw, gh, 1.0f);
	relief.init(gw, gh, 0.0f);
	std::vector<PassSite> passes;
	std::vector<Site> sites;
	json featRep = json::array();
	// the rivers as carved (their course, width and bed), for the materials (MapMaterials.h)
	json riversGen = json::array();
	// rivers with water ("water": true): their courses, carved beds and sizes, made into sloped
	// waterways (WaterDesign::Waterway::levels) once every channel is cut
	struct WetRiver { const Feature* f; std::vector<V2> c; std::vector<float> bed; float width, depth; std::vector<float> lakeAt; };
	std::vector<WetRiver> wet;
	for (const Feature& f : feats) featRep.push_back({ { "index", f.index }, { "kind", f.kind } });
	auto rep = [&](const Feature& f) -> json& {
		for (json& r : featRep) if (r["index"] == f.index) return r;
		return featRep[0];
	};
	for (const Feature& f : feats) {
		json& r = rep(f);
		if (!f.id.empty()) r["id"] = f.id;
		if (!f.role.empty()) r["role"] = f.role;
	}
	auto forEach = [&](auto fn) {
		for (int z = 0; z < gh; z++)
			for (int x = 0; x < gw; x++)
				fn(x, z, m.pos(x, z));
	};

	// 2. the large forms added
	auto t1 = std::chrono::steady_clock::now();
	for (const Feature& f : feats) {
		if (f.kind == "island") {
			const float peak = Num(f.j, "height", sea + 30), depth = Num(f.j, "seaDepth", 8), shelf = Num(f.j, "shelf", 160);
			const float r0 = f.shape.size();
			forEach([&](int x, int z, V2 p) {
				const float sd = AreaSd(f.shape, f.warped(p)) + r0 * 0.12f * Fbm(p.x / 90.0f, p.z / 90.0f, 3, f.seed + 3);
				float t;
				if (sd > 0)
					t = sea - 0.4f - depth * Smooth(0, shelf, sd);
				else {
					const float u = std::clamp(-sd / (r0 * 0.85f), 0.0f, 1.0f);
					t = sea + 0.6f + (peak - sea) * std::pow(Smooth(0, 1, u), 0.85f);
					amp.at(x, z) += 0.6f * u;
					relief.at(x, z) += (peak - sea) * 0.18f * u;
				}
				g.at(x, z) = std::max(g.at(x, z), t);
			});
		}
	}
	for (const Feature& f : feats) {
		if (f.kind == "hill") {
			const float peak = Num(f.j, "height", base + 25);
			const float r0 = f.shape.size();
			const bool sharp = f.j.value("shape", "") == "peak";
			forEach([&](int x, int z, V2 p) {
				const float sd = AreaSd(f.shape, f.warped(p));
				const float u = std::clamp(1 - (sd + r0) / r0, 0.0f, 1.0f); // 1 at the centre, 0 at the outline
				if (u <= 0) return;
				float b = Smooth(0, 1, u);
				if (sharp) b = std::pow(b, 1.6f);
				// the height is the top: it rises from whatever is there
				g.at(x, z) += std::max(0.0f, peak - g.at(x, z)) * b;
				amp.at(x, z) += 0.6f * b;
				relief.at(x, z) += std::abs(peak - base) * 0.2f * b;
			});
		}
		else if (f.kind == "range") {
			const float width = Num(f.j, "width", 240), hw = width / 2;
			const float L = f.shape.length;
			// the passes: evenly along the line, or where the plan puts them
			std::vector<std::pair<float, float>> pts; // t, saddle height
			const float peakMid = NumAlong(f.j, "height", base + 60, 0.5f);
			const float passH = Num(f.j, "passHeight", base + 0.3f * (peakMid - base));
			const float passW = Num(f.j, "passWidth", 90);
			if (f.j.contains("passes") && f.j["passes"].is_number()) {
				const int n = std::max(0, f.j["passes"].get<int>());
				XorShift rr(f.seed + 11);
				for (int k = 0; k < n; k++)
					pts.push_back({ L * ((k + 1.0f) / (n + 1) + (rr.unit() - 0.5f) * 0.12f / (n + 1)), passH });
			}
			else if (f.j.contains("passes") && f.j["passes"].is_array()) {
				for (const json& pj : f.j["passes"]) {
					V2 pp;
					if (ReadPoint(pj, pp)) pts.push_back({ Nearest(f.shape, pp).t, passH });
					else if (pj.is_object() && ReadPoint(pj.value("at", json()), pp)) pts.push_back({ Nearest(f.shape, pp).t, Num(pj, "height", passH) });
				}
			}
			json pr = json::array();
			for (auto [pt, ph] : pts) {
				V2 tan;
				const V2 at = LineAt(f.shape, pt, &tan);
				PassSite s;
				s.p = at; s.across = Perp(tan); s.height = ph; s.width = Num(f.j, "passPathWidth", 40);
				s.maxDeg = Num(f.j, "passMaxSlopeDeg", 14); s.reach = hw * 1.15f; s.feature = f.index;
				passes.push_back(s);
				pr.push_back({ { "at", { at.x, at.z } }, { "heightM", ph } });
			}
			rep(f)["passes"] = pr;
			forEach([&](int x, int z, V2 p) {
				const V2 wp = f.warped(p);
				const LineHit hit = Nearest(f.shape, wp);
				// the width varies along the ridge, and spurs come off its sides
				const float hwv = hw * (1 + 0.25f * Fbm(hit.t / 260.0f, 3.1f, 2, f.seed + 21));
				if (hit.d >= hwv * 1.3f) return;
				const float c = std::clamp(1 - hit.d / hwv, 0.0f, 1.0f);
				const float s = std::pow(Smooth(0, 1, c), 1.15f);
				const float u = L > 0 ? hit.t / L : 0;
				float peak = NumAlong(f.j, "height", base + 60, u) * (0.82f + 0.18f * Fbm(hit.t / 180.0f, 7.7f, 3, f.seed + 31));
				// the ends taper into the plain unless the line reaches the map's edge
				peak = Lerp(base, peak, Smooth(0, hw * 0.8f, hit.t) * Smooth(0, hw * 0.8f, L - hit.t) * 0.7f + 0.3f);
				float relief0 = std::max(0.0f, peak - base) * 0.38f;
				for (auto [pt, ph] : pts) {
					const float gpass = std::exp(-((hit.t - pt) * (hit.t - pt)) / (passW * passW));
					peak = Lerp(peak, std::min(peak, ph), gpass);
					relief0 *= 1 - 0.85f * gpass;
				}
				g.at(x, z) += std::max(0.0f, peak - g.at(x, z)) * s;
				amp.at(x, z) += 0.8f * s;
				relief.at(x, z) += relief0 * Smooth(0, 0.6f, c);
			});
		}
	}
	timing["forms"] = Ms(t1);

	// 3. the noise
	t1 = std::chrono::steady_clock::now();
	{
		const json nz = terrain.value("noise", json::object());
		const float A = Num(nz, "amplitude", 6), S = std::max(20.0f, Num(nz, "scale", 500)), ridgedShare = std::clamp(Num(nz, "ridged", 0.25f), 0.0f, 1.0f);
		const int oct = std::clamp((int)Num(nz, "octaves", 6), 1, 10);
		const float warp = Num(nz, "warp", 0.5f);
		const uint32_t s = seed * 31u + 7;
		forEach([&](int x, int z, V2 p) {
			const V2 q = { p.x / S, p.z / S };
			const V2 w = { warp * Fbm(q.x * 0.7f + 3.1f, q.z * 0.7f, 3, s + 1), warp * Fbm(q.x * 0.7f, q.z * 0.7f - 4.7f, 3, s + 2) };
			const V2 qw = q + w;
			const float f = Fbm(qw.x, qw.z, oct, s + 3);
			const float rd = Ridged(qw.x * 1.3f, qw.z * 1.3f, oct, s + 4);
			const float n = (1 - ridgedShare) * f + ridgedShare * (rd * 2 - 0.8f);
			float add = A * amp.at(x, z) * n;
			const float rel = relief.at(x, z);
			if (rel > 0) {
				// the mountains' own relief: ridged noise at the scale of the ridges' spurs
				const float r2 = Ridged(p.x / 170.0f + w.x, p.z / 170.0f + w.z, 6, s + 5);
				add += rel * (r2 * 2 - 0.85f);
			}
			g.at(x, z) += add;
		});
	}
	timing["noise"] = Ms(t1);

	// 4. the forms set or cut
	t1 = std::chrono::steady_clock::now();
	// what the rivers' troughs and channels took off the land, per vertex (for the lakes' rims below)
	Grid riverCut;
	riverCut.init(gw, gh, 0.0f);
	int lakeRimsMended = 0;
	// per vertex the level of the lake a kept rim holds (the lowering at the rivers' lips stops there)
	Grid rimHold;
	rimHold.init(gw, gh, -1e30f);
	for (const Feature& f : feats) {
		if (f.kind == "plateau") {
			Site st;
			st.feature = f.index;
			st.height = Num(f.j, "height", base + 25);
			st.rim = Lerp(70.0f, 12.0f, std::clamp(Num(f.j, "cliffs", 0.5f), 0.0f, 1.0f));
			sites.push_back(st);
			forEach([&](int x, int z, V2 p) {
				const float sd = AreaSd(f.shape, f.warped(p));
				// the rim: steep in places, a slope in others
				const float rim = st.rim * (0.45f + 1.1f * std::clamp(0.5f + Fbm(p.x / 110.0f, p.z / 110.0f, 2, f.seed + 4), 0.0f, 1.0f));
				const float k = Smooth(rim * 0.5f, -rim * 0.5f, sd);
				if (k <= 0) return;
				const float top = st.height + 1.2f * Fbm(p.x / 160.0f, p.z / 160.0f, 3, f.seed + 2);
				g.at(x, z) = Lerp(g.at(x, z), top, k);
			});
		}
		else if (f.kind == "valley" || f.kind == "basin") {
			const float depth = Num(f.j, "depth", 20);
			const bool line = f.shape.kind == Shape::LINE;
			const float width = line ? Num(f.j, "width", 300) : 0, hw = width / 2;
			const float r0 = line ? hw : f.shape.size();
			const float floorShare = std::clamp(Num(f.j, "floor", 0.35f), 0.0f, 0.9f);
			forEach([&](int x, int z, V2 p) {
				const V2 wp = f.warped(p);
				float u, fl;
				if (line) {
					const LineHit hit = Nearest(f.shape, wp);
					u = hit.d / hw;
					const float along = f.shape.length > 0 ? hit.t / f.shape.length : 0;
					fl = f.j.contains("floorHeight") ? NumAlong(f.j, "floorHeight", base - depth, along) : base - depth - Num(f.j, "fall", 0) * along;
				}
				else {
					u = (AreaSd(f.shape, wp) + r0) / r0;
					fl = Num(f.j, "floorHeight", base - depth);
				}
				if (u >= 1) return;
				const float s = 1 - Smooth(floorShare, 1, u);
				const float cut = std::min(g.at(x, z), fl + 0.2f * (g.at(x, z) - fl));
				g.at(x, z) = Lerp(g.at(x, z), cut, s);
			});
		}
		else if (f.kind == "coast") {
			const float seaWidth = Num(f.j, "sea", 0), depth = Num(f.j, "seaDepth", 8), shelf = Num(f.j, "shelf", 200);
			const float beach = Num(f.j, "beach", 40), cliffs = std::clamp(Num(f.j, "cliffs", 0.2f), 0.0f, 1.0f);
			V2 dir;
			const std::string side = Lower(f.j.value("side", ""));
			const bool bySide = SideDir(side, dir);
			if (!bySide && f.shape.kind != Shape::LINE) { warnings.push_back("coast: \"side\" (north/south/east/west) or a \"line\""); continue; }
			V2 seaPt;
			const bool hasSeaPt = ReadPoint(f.j.value("seaSide", json()), seaPt);
			const float span = bySide ? (dir.x != 0 ? m.playW() : m.playH()) : 0;
			const float sw = seaWidth > 0 ? seaWidth : span * 0.25f;
			const float irr = std::clamp(Num(f.j, "irregularity", 0.5f), 0.0f, 1.5f);
			forEach([&](int x, int z, V2 p) {
				float sd; // > 0 at sea
				float along;
				if (bySide) {
					// the coast line sw metres in from the side
					const float centre = dir.x != 0 ? m.playW() * 0.5f : m.playH() * 0.5f;
					const float coord = dir.x != 0 ? p.x : p.z;
					along = dir.x != 0 ? p.z : p.x;
					const float d = (dir.x + dir.z > 0) ? coord - (2 * centre - sw) : sw - coord;
					sd = d;
				}
				else {
					const LineHit hit = Nearest(f.shape, p);
					along = hit.t;
					const V2 n = Perp(hit.tangent);
					float sgn = Dot(p - hit.point, n) >= 0 ? 1.0f : -1.0f;
					if (hasSeaPt && Dot(seaPt - hit.point, n) < 0) sgn = -sgn;
					sd = hit.d * sgn;
				}
				// bays and headlands
				sd += irr * sw * 0.55f * Fbm(along / 420.0f, 1.9f, 4, f.seed + 4) + irr * 25.0f * Fbm(p.x / 60.0f, p.z / 60.0f, 3, f.seed + 6);
				const bool cliff = Fbm(along / 300.0f, 8.3f, 2, f.seed + 8) > 1 - 2 * cliffs;
				float h = g.at(x, z);
				if (sd > 0) {
					const float floorH = sea - 0.5f - depth * Smooth(0, shelf, sd);
					h = Lerp(h, std::min(h, floorH), Smooth(0, cliff ? 6.0f : 30.0f, sd));
				}
				else if (!cliff) {
					// the land comes down to a beach
					const float k = 1 - Smooth(0, beach * 3, -sd);
					h = Lerp(h, std::min(h, sea + 0.8f + (-sd) * 0.03f), k);
				}
				g.at(x, z) = h;
			});
		}
		else if (f.kind == "pass") {
			V2 at = f.shape.centre();
			const float ph = Num(f.j, "height", base + 15), width = Num(f.j, "width", 90);
			// across: the given direction, or the way down that is lowest on both sides
			V2 across{ 1, 0 };
			if (f.j.contains("direction") && f.j["direction"].is_number()) {
				const float a = f.j["direction"].get<float>() * PI / 180;
				across = { std::sin(a), std::cos(a) };
			}
			else {
				float best = 1e30f;
				for (int k = 0; k < 12; k++) {
					const float a = k * PI / 12;
					const V2 d{ std::cos(a), std::sin(a) };
					const V2 p1 = at + d * 180.0f, p2 = at - d * 180.0f;
					const float s = g.sample(m.vx(p1.x), m.vz(p1.z)) + g.sample(m.vx(p2.x), m.vz(p2.z));
					if (s < best) { best = s; across = d; }
				}
			}
			forEach([&](int x, int z, V2 p) {
				const float r = Len(p - at);
				if (r > width * 3) return;
				g.at(x, z) = std::min(g.at(x, z), ph + std::max(0.0f, r - width / 2) * 0.3f);
			});
			PassSite s;
			s.p = at; s.across = across; s.height = ph; s.width = Num(f.j, "pathWidth", 40);
			s.maxDeg = Num(f.j, "maxSlopeDeg", 14); s.reach = Num(f.j, "reach", 200); s.feature = f.index;
			passes.push_back(s);
		}
		else if (f.kind == "river" && f.j.value("trough", true)) {
			// the broad trough the river runs in, before the erosion, so the drainage finds it
			// ("trough": false for rivers on real land, which has its valleys already: the trough's
			// gentle sides had shaved the mountains beside the glens down to its own height)
			const float width = Num(f.j, "width", 20);
			const float vw = Num(f.j, "valleyWidth", std::max(width * 4, 70.0f)), vd = Num(f.j, "valleyDepth", Num(f.j, "depth", 3) * 1.5f);
			const std::vector<V2> c = Course(f, width);
			// (no deeper than the river's own lowest bed, minBed: inland, troughs leaving the map
			// had gone 10 m below the land and the whole map was raised to make up for it)
			const float minBed = f.j.contains("minBed") && f.j["minBed"].is_number() ? f.j["minBed"].get<float>() : -1e30f;
			const Grid before = g;
			CarveChannel(m, g, c, vw * 0.3f, vd, vw * 0.5f, 9, {}, f.seed + 40, minBed);
			for (size_t i = 0; i < g.v.size(); i++) riverCut.v[i] += before.v[i] - g.v[i];
		}
	}
	for (const Feature& f : feats) {
		if (f.kind != "terraces") continue;
		const float step = std::max(1.0f, Num(f.j, "step", 6)), strength = std::clamp(Num(f.j, "strength", 0.7f), 0.0f, 1.0f);
		forEach([&](int x, int z, V2 p) {
			float k = 1;
			if (f.shape.kind != Shape::NONE) {
				const float r0 = f.shape.kind == Shape::LINE ? Num(f.j, "width", 200) / 2 : f.shape.size() * 0.25f + 20;
				k = Smooth(r0, -r0 * 0.2f, AreaSd(f.shape, f.warped(p)) - (f.shape.kind == Shape::LINE ? 0 : 0));
			}
			if (k <= 0) return;
			const float h = g.at(x, z) / step;
			const float fl = std::floor(h), fr = h - fl;
			// flat treads, steep risers, the risers' line wandering
			const float edge = 0.78f + 0.12f * Noise(p.x / 50.0f, p.z / 50.0f, f.seed + 1);
			const float t = (fl + Smooth(edge, 1.0f, fr)) * step;
			g.at(x, z) = Lerp(g.at(x, z), t, k * strength);
		});
	}
	// the shore flattened around the sea level, so coasts get beaches
	if (hasSea) {
		const float band = Num(terrain, "beach", 2.5f);
		if (band > 0)
			for (float& v : g.v) {
				const float d = v - sea;
				if (std::abs(d) < band) v = sea + (d > 0 ? 1.0f : -1.0f) * band * std::pow(std::abs(d) / band, 1.7f);
			}
	}
	timing["shapes"] = Ms(t1);

	// 5. erosion
	t1 = std::chrono::steady_clock::now();
	{
		const json er = terrain.value("erosion", json::object());
		const float hyd = Num(er, "hydraulic", 1.0f), th = Num(er, "thermal", 1.0f), talus = Num(er, "talusDeg", 38);
		if (th > 0)
			Thermal(g, (int)std::lround(12 * th), talus, 0.5f);
		if (hyd > 0) {
			const int drops = (int)std::min<double>(4e6, gw * (double)gh * Num(er, "dropsPerVertex", 0.9f) * std::min(hyd, 2.0f));
			Hydraulic(g, drops, seed * 977u + 3, hyd, Num(er, "maxCutM", 10), Num(er, "maxFillM", 8), hasSea ? sea : -1e9f);
		}
		if (th > 0)
			Thermal(g, (int)std::lround(4 * th), talus + 4, 0.4f);
	}
	timing["erosion"] = Ms(t1);

	// 6. the plan's details, carved last
	t1 = std::chrono::steady_clock::now();
	for (const Feature& f : feats) {
		if (f.kind != "flat") continue;
		const float r0 = f.shape.size();
		float level = Num(f.j, "height", NAN);
		if (std::isnan(level)) {
			double s = 0; int n = 0;
			forEach([&](int x, int z, V2 p) { if (AreaSd(f.shape, p) < 0) { s += g.at(x, z); n++; } });
			level = n ? (float)(s / n) : base;
		}
		const float blend = Num(f.j, "blend", std::max(25.0f, r0 * 0.5f));
		forEach([&](int x, int z, V2 p) {
			const float sd = AreaSd(f.shape, f.warped(p));
			const float k = Smooth(blend, 0, sd);
			if (k > 0) g.at(x, z) = Lerp(g.at(x, z), level + 0.3f * Noise(p.x / 40.0f, p.z / 40.0f, f.seed), k);
		});
		rep(f)["heightM"] = level;
	}
	for (const Site& st : sites) {
		const Feature* fp = nullptr;
		for (const Feature& f : feats) if (f.index == st.feature) fp = &f;
		const Feature& f = *fp;
		forEach([&](int x, int z, V2 p) {
			const float sd = AreaSd(f.shape, f.warped(p));
			const float k = Smooth(-st.rim * 0.05f, -st.rim * 0.6f, sd);
			if (k <= 0) return;
			const float top = st.height + 1.2f * Fbm(p.x / 160.0f, p.z / 160.0f, 3, f.seed + 2);
			g.at(x, z) = Lerp(g.at(x, z), top, k * 0.8f);
		});
		// ramps down from the top
		std::vector<V2> dirs;
		if (f.j.contains("rampsAt") && f.j["rampsAt"].is_array())
			for (const json& rj : f.j["rampsAt"]) { V2 q; if (ReadPoint(rj, q)) dirs.push_back(Norm(q - f.shape.centre())); }
		else {
			const int n = std::max(0, (int)Num(f.j, "ramps", 1));
			const float a0 = f.j.contains("rampAngle") ? Num(f.j, "rampAngle", 0) * PI / 180 : XorShift(f.seed + 13).unit() * 2 * PI;
			for (int k = 0; k < n; k++) { const float a = a0 + k * 2 * PI / n; dirs.push_back({ std::sin(a), std::cos(a) }); }
		}
		const float rampDeg = Num(f.j, "rampDeg", 12), rampW = Num(f.j, "rampWidth", 26);
		const float tanR = std::tan(rampDeg * PI / 180);
		json rr = json::array();
		const V2 c = f.shape.centre();
		int k = 0;
		for (V2 d : dirs) {
			// the rim along the ray, the drop beyond it
			float t = 0;
			while (t < 3000 && AreaSd(f.shape, f.warped(c + d * t)) < 0) t += 5;
			const V2 rim = c + d * t;
			const V2 q = rim + d * (st.rim + 60);
			const float drop = std::abs(st.height - g.sample(m.vx(q.x), m.vz(q.z)));
			const float half = drop / tanR * 0.6f;
			const float in = std::min(half + 20, t * 0.8f), out = half + st.rim + 30;
			const std::vector<V2> way = Way(m, g, { rim - d * in, rim + d * out }, tanR, rampW, 22, f.seed + 50 + k++, 0.5f);
			rr.push_back({ { "way", WayJson(way) }, { "dropM", drop } });
		}
		rep(f)["ramps"] = rr;
	}
	int passesDone = 0;
	for (const PassSite& s : passes) {
		const float saddle = std::min(g.sample(m.vx(s.p.x), m.vz(s.p.z)), s.height);
		const float tanMax = std::tan(s.maxDeg * PI / 180);
		V2 ends[2];
		for (int side = 0; side < 2; side++) {
			const V2 d = s.across * (side ? -1.0f : 1.0f);
			float L = s.reach;
			for (int it = 0; it < 5; it++) {
				const V2 q = s.p + d * L;
				const float need = std::abs(g.sample(m.vx(q.x), m.vz(q.z)) - saddle) / tanMax;
				if (need <= L) break;
				L = std::min(need * 1.05f, 900.0f);
			}
			ends[side] = s.p + d * L;
		}
		const std::vector<V2> way = Way(m, g, { ends[0], s.p, ends[1] }, tanMax, s.width, 26, seed * 13u + (uint32_t)s.feature * 7u + (uint32_t)passesDone++, 0.6f);
		for (json& fr : featRep)
			if (fr["index"] == s.feature) {
				if (!fr.contains("passPaths")) fr["passPaths"] = json::array();
				fr["passPaths"].push_back({ { "way", WayJson(way) }, { "saddleM", saddle } });
			}
	}
	// The lakes on the land as the rivers find it (for the rivers with water: their beds and water
	// through a lake at its level): per vertex the highest lake over it, flooded from each lake's
	// point as the game floods (Terrain::floodfillWater: from tile to tile across an edge with a
	// corner below the level), its tiles' corners below the level (the sea, the basins' lakes,
	// water.json's own lakes: those the generator didn't make, at their level less the last
	// run's raise)
	std::vector<float> lakeOn;
	auto floodLakes = [&]() {
		lakeOn.assign(g.v.size(), -1e30f);
		std::vector<std::array<float, 3>> points; // vertex x, z, level
		if (hasSea) {
			std::vector<SeaBody> bodies;
			SeaTiles(m, g, sea, 100, &bodies);
			for (const SeaBody& b : bodies) points.push_back({ (float)b.deepX, (float)b.deepZ, sea });
		}
		for (const Feature& f : feats)
			if (f.kind == "basin" && f.j.contains("water") && f.j["water"].is_number()) {
				const V2 c = f.shape.centre();
				points.push_back({ m.vx(c.x), m.vz(c.z), f.j["water"].get<float>() });
			}
		{
			json wj0, gen0 = m.raw.value("terrain", json::object()).value("generated", json::object());
			const float raised0 = gen0.value("raisedByM", 0.0f);
			if (ReadJson(folder / "water.json", wj0) && wj0.is_object())
				for (const json& l : wj0.value("lakes", json::array())) {
					if (!l.is_array() || l.size() < 3) continue;
					bool mine = false;
					for (const json& p : gen0.value("lakes", json::array()))
						if (p.is_array() && p.size() >= 3 && std::abs(l[0].get<float>() - p[0].get<float>()) < 0.05f
							&& std::abs(l[1].get<float>() - p[1].get<float>()) < 0.05f && std::abs(l[2].get<float>() - p[2].get<float>()) < 0.05f)
							mine = true;
					if (!mine) points.push_back({ l[0].get<float>() / TILE, l[2].get<float>() / TILE, l[1].get<float>() - raised0 });
				}
		}
		std::vector<float> tileLake((size_t)m.W * m.H, -1e30f);
		for (const auto& p : points) {
			const int sx = std::clamp((int)std::floor(p[0]), 0, m.W - 1), sz = std::clamp((int)std::floor(p[1]), 0, m.H - 1);
			// (the heights as heights.png keeps them, in 256ths of the scale, as the game floods: lakes
			// from elevation data lie on the data's flat water, the ground at their level, and in the
			// unrounded heights a lake had run out over land a thousandth of a metre below it)
			const float L = p[2];
			auto q = [&](float v) { return std::round(v / m.scale * 256.0f) / 256.0f * m.scale; };
			auto lowest = [&](int x, int z) { return std::min({ q(g.at(x, z)), q(g.at(x + 1, z)), q(g.at(x, z + 1)), q(g.at(x + 1, z + 1)) }); };
			if (lowest(sx, sz) >= L || tileLake[(size_t)sz * m.W + sx] >= L) continue;
			std::vector<int> queue = { sz * m.W + sx };
			tileLake[queue[0]] = L;
			for (size_t qi = 0; qi < queue.size(); qi++) {
				const int x = queue[qi] % m.W, z = queue[qi] / m.W;
				const int nb[4][6] = { { x + 1, z, x + 1, z, x + 1, z + 1 }, { x - 1, z, x, z, x, z + 1 },
					{ x, z + 1, x, z + 1, x + 1, z + 1 }, { x, z - 1, x, z, x + 1, z } };
				for (const auto& n : nb) {
					if (n[0] < 0 || n[1] < 0 || n[0] >= m.W || n[1] >= m.H) continue;
					const size_t t = (size_t)n[1] * m.W + n[0];
					if (tileLake[t] >= L || std::min(q(g.at(n[2], n[3])), q(g.at(n[4], n[5]))) >= L) continue;
					tileLake[t] = L;
					queue.push_back((int)t);
				}
			}
			printf("Terrain: the rivers' lake at tile (%d, %d), level %.2f: %zu tiles\n", sx, sz, L, queue.size());
		}
		for (int z = 0; z < m.H; z++)
			for (int x = 0; x < m.W; x++) {
				const float L = tileLake[(size_t)z * m.W + x];
				if (L < -1e29f) continue;
				for (int c = 0; c < 4; c++) {
					const size_t k = (size_t)(z + (c >> 1)) * m.gw() + x + (c & 1);
					if (g.v[k] <= L + 0.01f) lakeOn[k] = std::max(lakeOn[k], L);
				}
			}
	};
	bool lakesFlooded = false;
	for (const Feature& f : feats) {
		if (f.kind != "river") continue;
		const float width = Num(f.j, "width", 20), depth = Num(f.j, "depth", 3);
		const std::vector<V2> c = Course(f, width);
		// a river with water: the lake under each point of its course (else none)
		std::vector<float> lakeAt;
		if (f.j.value("water", false)) {
			if (!lakesFlooded) {
				floodLakes();
				lakesFlooded = true;
			}
			lakeAt.assign(c.size(), -1e30f);
			for (size_t i = 0; i < c.size(); i++) {
				const int x = std::clamp((int)std::lround(m.vx(c[i].x)), 0, m.gw() - 1), z = std::clamp((int)std::lround(m.vz(c[i].z)), 0, m.gh() - 1);
				lakeAt[i] = lakeOn[(size_t)z * m.gw() + x];
			}
		}
		std::vector<float> fw(c.size(), 0.0f);
		const int fords = std::max(0, (int)Num(f.j, "fords", 0));
		json fr = json::array();
		for (int k = 0; k < fords; k++) {
			const size_t at = (size_t)((k + 1.0) * (c.size() - 1) / (fords + 1));
			for (size_t i = 0; i < c.size(); i++) {
				const float d = ((float)i - (float)at) * 5.0f;
				fw[i] = std::max(fw[i], std::exp(-d * d / (25.0f * 25.0f)));
			}
			fr.push_back({ c[at].x, c[at].z });
		}
		const float minBed = f.j.contains("minBed") && f.j["minBed"].is_number() ? f.j["minBed"].get<float>() : hasSea ? sea + 0.4f : -1e30f; // (above the sea: it never runs up the channel)
		const Grid beforeChannel = g;
		const std::vector<float> bed = CarveChannel(m, g, c, width, depth, Num(f.j, "bank", width * 1.5f), Num(f.j, "bankDeg", 32), fw, f.seed + 41, minBed,
			lakeAt.empty() ? nullptr : &lakeAt, Num(f.j, "grade", 0));
		for (size_t i = 0; i < g.v.size(); i++) riverCut.v[i] += beforeChannel.v[i] - g.v[i];
		float maxRise = -1e30f;
		for (size_t i = 1; i < bed.size(); i++) maxRise = std::max(maxRise, bed[i] - bed[i - 1]);
		json& r = rep(f);
		r["lengthM"] = (c.size() - 1) * 5.0f;
		r["bedFromM"] = bed.front();
		r["bedToM"] = bed.back();
		r["bedMaxRiseM"] = maxRise;
		r["fords"] = fr;
		json course = json::array();
		for (size_t i = 0; i < c.size(); i += 2)
			course.push_back({ std::round(c[i].x * 10) / 10, std::round(c[i].z * 10) / 10, std::round(bed[i] * 100) / 100 });
		riversGen.push_back({ { "id", f.j.value("id", "") }, { "width", width }, { "course", course }, { "fords", fr } });
		if (f.j.value("water", false))
			wet.push_back({ &f, c, bed, width, depth, lakeAt });
	}
	// The lakes' rims kept: water.json's own lakes (lakes found on the land, not the generator's),
	// each flooded from its point over the land as cut (8-connected below its level); where the flood
	// would run out through land a river's trough or channel cut below the level, that land gets back
	// what the river took (a lake left behind a low bar at its outlet; the river's water falls over
	// it). Done here, before the rivers' water, so their water and the lakes see the land as it stays
	// (mended afterwards, the rivers' water had stood above the land the lake no longer reached)
	{
		std::vector<std::array<float, 3>> own;
		json wj0, gen0 = m.raw.value("terrain", json::object()).value("generated", json::object());
		const float raised0 = gen0.value("raisedByM", 0.0f);
		if (ReadJson(folder / "water.json", wj0) && wj0.is_object())
			for (const json& l : wj0.value("lakes", json::array())) {
				if (!l.is_array() || l.size() < 3) continue;
				bool mine = false;
				for (const json& p : gen0.value("lakes", json::array()))
					if (p.is_array() && p.size() >= 3 && std::abs(l[0].get<float>() - p[0].get<float>()) < 0.05f
						&& std::abs(l[1].get<float>() - p[1].get<float>()) < 0.05f && std::abs(l[2].get<float>() - p[2].get<float>()) < 0.05f)
						mine = true;
				if (!mine && (!hasSea || std::abs(l[1].get<float>() - raised0 - sea) > 0.01f))
					own.push_back({ l[0].get<float>() / TILE, l[2].get<float>() / TILE, l[1].get<float>() - raised0 });
			}
		int mended = 0;
		const int gw2 = m.gw(), gh2 = m.gh();
		for (const auto& p : own) {
			const float L = p[2];
			// (in the heights as heights.png keeps them, as the game floods: a rim a hair above the
			// level could round below it; a rim the river took within 5 cm of the level is put back
			// a centimetre above it)
			auto q = [&](float v) { return std::round(v / m.scale * 256.0f) / 256.0f * m.scale; };
			for (int round = 0; round < 20; round++) {
				// the seed: the lowest of the tile's corners
				int sx = std::clamp((int)std::floor(p[0]), 0, gw2 - 2), sz = std::clamp((int)std::floor(p[1]), 0, gh2 - 2);
				int seed = -1;
				for (int c = 0; c < 4; c++) {
					const int x = sx + (c & 1), z = sz + (c >> 1);
					if (q(g.at(x, z)) < L && (seed < 0 || g.at(x, z) < g.v[seed])) seed = z * gw2 + x;
				}
				if (seed < 0) break;
				std::vector<uint8_t> seen(g.v.size(), 0);
				std::vector<int> stack = { seed }, breach;
				seen[seed] = 1;
				while (!stack.empty()) {
					const int k = stack.back();
					stack.pop_back();
					const int x = k % gw2, z = k / gw2;
					for (int dz = -1; dz <= 1; dz++)
						for (int dx = -1; dx <= 1; dx++) {
							const int nx = x + dx, nz = z + dz;
							if (nx < 0 || nz < 0 || nx >= gw2 || nz >= gh2) continue;
							const int nk = nz * gw2 + nx;
							if (seen[nk] || q(g.v[nk]) >= L) continue;
							seen[nk] = 1;
							if (riverCut.v[nk] > 0 && g.v[nk] + riverCut.v[nk] >= L - 0.05f) breach.push_back(nk);
							else stack.push_back(nk);
						}
				}
				if (breach.empty()) break;
				for (int k : breach) {
					g.v[k] = std::max(g.v[k] + riverCut.v[k], L + m.scale / 256.0f);
					riverCut.v[k] = 0;
					rimHold.v[k] = std::max(rimHold.v[k], L + m.scale / 256.0f);
					mended++;
				}
			}
		}
		if (mended) printf("Terrain: lake rims kept, %d vertices given back\n", mended);
		lakeRimsMended = mended;
	}
	// Their water: a sloped surface along the course (the game's sloped rivers), its height at
	// each point the bed there (as carved, or lower where a later channel cut it) plus the water's
	// depth ("waterDepth", m; 0.45 of the channel's depth), thinner where the bed runs steeply
	// (fast water: a third at a slope of one in two, at least 0.25 m: rapids, cascades and falls hug their bed),
	// smoothed a little and never rising downstream. Heights before the raise below.
	struct WetWay { const Feature* f; std::vector<V2> c; std::vector<float> level; float width; };
	std::vector<WetWay> wetWays;
	// the lakes and the sea as they flood the land now, every channel cut and the lakes' rims kept:
	// the banks below, and each river's stretches on a lake again (found before its channel was cut,
	// a lake had reached along troughs its kept rim now closes, and the river's water there had
	// stayed at the lake's level, metres above the land)
	if (!wet.empty()) {
		floodLakes();
		for (WetRiver& wr : wet)
			if (!wr.lakeAt.empty())
				for (size_t i = 0; i < wr.c.size() && i < wr.lakeAt.size(); i++) {
					const int x = std::clamp((int)std::lround(m.vx(wr.c[i].x)), 0, m.gw() - 1), z = std::clamp((int)std::lround(m.vz(wr.c[i].z)), 0, m.gh() - 1);
					wr.lakeAt[i] = lakeOn[(size_t)z * m.gw() + x];
				}
	}
	for (const WetRiver& wr : wet) {
		const size_t n = wr.c.size();
		if (n < 2) continue;
		const float wd = Num(wr.f->j, "waterDepth", wr.depth * 0.45f);
		std::vector<float> bed(n);
		for (size_t i = 0; i < n; i++)
			bed[i] = (i < wr.lakeAt.size() && wr.lakeAt[i] > -1e29f) ? wr.bed[i] : std::min(wr.bed[i], g.sample(m.vx(wr.c[i].x), m.vz(wr.c[i].z)));
		for (size_t i = 1; i < n; i++)
			if (!(i < wr.lakeAt.size() && wr.lakeAt[i] > -1e29f) && !(wr.lakeAt.size() > i - 1 && wr.lakeAt[i - 1] > -1e29f))
				bed[i] = std::min(bed[i], bed[i - 1]);
		std::vector<float> level(n);
		for (size_t i = 0; i < n; i++) {
			const size_t a = i >= 3 ? i - 3 : 0, b = std::min(n - 1, i + 3);
			const float slope = (bed[a] - bed[b]) / std::max((float)(b - a) * 5.0f, 1.0f);
			level[i] = bed[i] + std::max(0.25f, wd / (1.0f + 4.0f * std::max(slope, 0.0f)));
		}
		auto lakeLevel = [&](size_t i) { return i < wr.lakeAt.size() && wr.lakeAt[i] > -1e29f ? wr.lakeAt[i] : -1e30f; };
		std::vector<float> sm(n);
		for (size_t i = 0; i < n; i++) {
			float sum = 0; int k = 0;
			for (long j = (long)i - 2; j <= (long)i + 2; j++)
				if (j >= 0 && j < (long)n) { sum += level[j]; k++; }
			// (only ever shallower than before: averaged across a steep drop the water had stood
			// above the land at its foot)
			sm[i] = std::max(std::min(sum / k, level[i]), bed[i] + 0.12f);
		}
		for (size_t i = 0; i < n; i++) {
			if (lakeLevel(i) > -1e29f)
				sm[i] = lakeLevel(i); // on a lake: its level
			else if (i > 0)
				sm[i] = std::min(sm[i], sm[i - 1]);
		}
		if (hasSea)
			for (float& l : sm) l = std::max(l, sea);
		// never below the lake it runs into: the stretch above a lake (its channel cut below the
		// lake's level near the shore) at the lake's level, as the lake reaches up it
		for (size_t i = n - 1; i-- > 0;)
			sm[i] = std::max(sm[i], sm[i + 1]);
		// never above the land beside the channel (the lower bank, half the width and 2 m out, plus
		// 0.3 m; a bank under a lake or the sea: its surface, as the land now floods): the level of a
		// lake or the sea carried upstream had held rivers 2-12 m above land lower than it but cut
		// off from it (behind a lake's rim, behind the coast), a raised ribbon; then falling again
		{
			const float out = wr.width / 2 + 2;
			for (size_t i = 0; i < n; i++) {
				if (lakeLevel(i) > -1e29f) continue;
				const V2 tan = Norm(wr.c[std::min(i + 1, n - 1)] - wr.c[i > 0 ? i - 1 : 0]);
				const V2 a = wr.c[i] + Perp(tan) * out, b = wr.c[i] - Perp(tan) * out;
				// (a bank under a lake: the lake's surface)
				auto bankAt = [&](V2 p) {
					const int vx = std::clamp((int)std::lround(m.vx(p.x)), 0, m.gw() - 1), vz = std::clamp((int)std::lround(m.vz(p.z)), 0, m.gh() - 1);
					const float lk = lakeOn.empty() ? -1e30f : lakeOn[(size_t)vz * m.gw() + vx];
					return std::max(g.sample(m.vx(p.x), m.vz(p.z)), lk);
				};
				const float bankLow = std::min(bankAt(a), bankAt(b));
				sm[i] = std::min(sm[i], std::max(bankLow + 0.3f, bed[i] + 0.12f));
			}
			for (size_t i = 1; i < n; i++)
				if (lakeLevel(i) < -1e29f)
					sm[i] = std::min(sm[i], sm[i - 1]);
		}
		// The ground as the game draws it (Terrain::getSmoothHeight, a Catmull-Rom spline through the
		// vertices) bulges above the vertices where the land bends, most at a fall's lip, and rose
		// through the water there: the vertices round such a place lowered until it lies 0.15 m
		// under the water (the water wears its lip down), checked every metre of the course
		{
			auto vert = [&](int x, int z) { return g.at(std::clamp(x, 0, m.gw() - 1), std::clamp(z, 0, m.gh() - 1)); };
			auto spline = [](float a, float b, float c, float d, float t) {
				return 0.5f * (2 * b + (c - a) * t + (2 * a - 5 * b + 4 * c - d) * t * t + (3 * b - a - 3 * c + d) * t * t * t);
			};
			auto smoothAt = [&](float px, float pz) {
				const float gx = m.vx(px), gz = m.vz(pz);
				const int ix = (int)std::floor(gx), iz = (int)std::floor(gz);
				const float fx = gx - ix, fz = gz - iz;
				float row[4];
				for (int k = -1; k <= 2; k++)
					row[k + 1] = spline(vert(ix - 1, iz + k), vert(ix, iz + k), vert(ix + 1, iz + k), vert(ix + 2, iz + k), fx);
				return spline(row[0], row[1], row[2], row[3], fz);
			};
			int lowered = 0;
			for (int pass = 0; pass < 4; pass++)
				for (size_t i = 0; i + 1 < n; i++)
					for (int s = 0; s < 5; s++) {
						const float t = s / 5.0f;
						const V2 p = wr.c[i] + (wr.c[i + 1] - wr.c[i]) * t;
						const float want = sm[i] + (sm[i + 1] - sm[i]) * t - 0.15f, over = smoothAt(p.x, p.z) - want;
						if (over <= 0.0f)
							continue;
						const int x0 = (int)std::floor(m.vx(p.x)), z0 = (int)std::floor(m.vz(p.z));
						// never below half a metre under this river's own carved bed (where rivers share
						// a course, the lowering had fed on itself: a pit 11 m deep)
						const float floorM = wr.bed[i] + (wr.bed[i + 1] - wr.bed[i]) * t - 0.5f;
						for (int dz = 0; dz <= 1; dz++)
							for (int dx = 0; dx <= 1; dx++) {
								const int x = x0 + dx, z = z0 + dz;
								if (x >= 0 && z >= 0 && x < m.gw() && z < m.gh())
									g.at(x, z) = std::max(g.at(x, z) - over * 0.7f, std::min(g.at(x, z), std::max(floorM, rimHold.at(x, z))));
							}
						lowered++;
					}
			json& rr = rep(*wr.f);
			rr["waterLipsLowered"] = lowered;
		}
		wetWays.push_back({ wr.f, wr.c, sm, wr.width });
		json& r = rep(*wr.f);
		r["waterFromM"] = sm.front();
		r["waterToM"] = sm.back();
		float steepest = 0;
		for (size_t i = 1; i < n; i++) steepest = std::max(steepest, (sm[i - 1] - sm[i]) / 5.0f);
		r["waterSteepest"] = std::round(steepest * 1000) / 1000;
	}
	timing["details"] = Ms(t1);

	// no land below 0 (the format's heights start there): everything raised, the sea too
	float lowestM = 1e30f;
	for (float v : g.v) lowestM = std::min(lowestM, v);
	float raisedBy = 0;
	if (lowestM < 0.5f) {
		raisedBy = std::ceil((0.5f - lowestM) * 4) / 4;
		for (float& v : g.v) v += raisedBy;
		if (hasSea) sea += raisedBy;
		warnings.push_back("the land went " + std::to_string(-lowestM) + " m below 0: all of it raised by " + std::to_string(raisedBy) + " m (raise terrain.base to avoid it)");
	}
	// sea and basin lakes: the lake points the compiler floods from (whole-map metres)
	std::vector<std::array<float, 3>> lakes;
	if (hasSea) {
		std::vector<SeaBody> bodies;
		SeaTiles(m, g, sea, 100, &bodies);
		for (const SeaBody& b : bodies)
			lakes.push_back({ b.deepX * TILE + 2.5f, sea, b.deepZ * TILE + 2.5f });
	}
	for (const Feature& f : feats)
		if (f.kind == "basin" && f.j.contains("water") && f.j["water"].is_number()) {
			const V2 c = f.shape.centre();
			lakes.push_back({ c.x + m.E * TILE, f.j["water"].get<float>() + raisedBy, c.z + m.E * TILE });
		}

	// heights >= 0; the height scale so the highest point fits (the hand layer keeps its meaning)
	for (float& v : g.v) v = std::max(v, 0.0f);
	Grid hand;
	bool handAny = false;
	ReadHand(folder, m, hand, handAny);
	float top = 0;
	for (size_t i = 0; i < g.v.size(); i++) top = std::max(top, g.v[i] + hand.v[i]);
	const float need = top / 255.9f;
	float scale = m.scale;
	if (need > scale) {
		if (!handAny)
			scale = std::ceil(need * 4) / 4;
		else
			warnings.push_back("the land reaches " + std::to_string(top) + " m, above what heights.scale " + std::to_string(m.scale)
				+ " holds (255.9 steps); heights.hand.png has edits, so the scale is kept and the top cut off");
	}
	if (!WriteHeights(folder / "heights.png", m, g, scale)) { error = "can't write heights.png"; return false; }

	// map.json: the scale, and what the generator made (so the next run replaces its own lakes only)
	json raw = m.raw;
	json heightsJ = raw.value("heights", json::object());
	heightsJ["scale"] = scale;
	raw["heights"] = heightsJ;
	json prevGen = raw.value("terrain", json::object()).value("generated", json::object());
	json lakesJ = json::array();
	for (auto& l : lakes) lakesJ.push_back({ l[0], l[1], l[2] });
	json gen = { { "by", "wkbai --author terrain" }, { "version", GENERATOR_VERSION }, { "seed", seed } };
	{
		Grid q;
		ReadHeights(folder / "heights.png", m, scale, q);
		char hx[24];
		snprintf(hx, sizeof(hx), "%016llx", (unsigned long long)HashGrid(q));
		gen["heightsHash"] = hx;
	}
	gen["lakes"] = lakesJ;
	gen["seaLevel"] = hasSea ? json(sea) : json();
	gen["raisedByM"] = raisedBy;
	if (lakeRimsMended) gen["lakeRimsKept"] = lakeRimsMended;
	for (json& rv : riversGen)
		for (json& pt : rv["course"])
			pt[2] = pt[2].get<float>() + raisedBy;
	gen["rivers"] = riversGen;
	// the sloped waterways, every second point of the course (10 m apart), the last one too, and
	// every point where the water's fall bends (at a fall's lip and foot: a line over the next
	// points cut the corner, and the water ran under the ground at the lip)
	json riverWays = json::array();
	for (const WetWay& w : wetWays) {
		json pts = json::array();
		for (size_t i = 0; i < w.c.size(); i++)
			if (i % 2 == 0 || i + 1 == w.c.size() || std::abs(w.level[i] - (w.level[i - 1] + w.level[i + 1]) * 0.5f) > 0.03f)
				pts.push_back({ std::round(w.c[i].x * 10) / 10, std::round(w.c[i].z * 10) / 10, std::round((w.level[i] + raisedBy) * 1000) / 1000 });
		riverWays.push_back({ { "points", pts }, { "width", w.width }, { "by", "terrain:" + w.f->j.value("id", std::to_string(w.f->index)) } });
	}
	json terrainOut = raw.value("terrain", json::object());
	if (!terrainOut.is_object()) terrainOut = json::object();
	terrainOut["generated"] = gen;
	raw["terrain"] = terrainOut;
	if (!WriteMapJson(folder, raw)) { error = "can't write map.json"; return false; }

	// water.json: the generator's lakes replaced, everything else kept
	{
		json wj;
		if (!ReadJson(folder / "water.json", wj) || !wj.is_object())
			wj = { { "format", "wkbai water" }, { "version", 1 } };
		json kept = json::array();
		for (const json& l : wj.value("lakes", json::array())) {
			bool mine = false;
			for (const json& p : prevGen.value("lakes", json::array()))
				if (l.is_array() && l.size() >= 3 && p.size() >= 3 && std::abs(l[0].get<float>() - p[0].get<float>()) < 0.05f
					&& std::abs(l[1].get<float>() - p[1].get<float>()) < 0.05f && std::abs(l[2].get<float>() - p[2].get<float>()) < 0.05f)
					mine = true;
			if (!mine) kept.push_back(l);
		}
		for (auto& l : lakes) kept.push_back({ l[0], l[1], l[2], 0 });
		wj["lakes"] = kept;
		// the waterways: the generator's own replaced ("by": "terrain:<id>"), the others kept
		json ways = json::array();
		for (const json& w : wj.value("waterways", json::array()))
			if (!(w.is_object() && w.value("by", "").rfind("terrain:", 0) == 0)) ways.push_back(w);
		for (const json& w : riverWays) ways.push_back(w);
		if (!ways.empty() || wj.contains("waterways")) {
			wj["waterways"] = ways;
			if (!wj.contains("tiles")) wj["tiles"] = { m.W, m.H };
		}
		if (!lakes.empty() || !riverWays.empty() || !prevGen.value("lakes", json::array()).empty() || fs::exists(folder / "water.json"))
			WriteAll(folder / "water.json", wj.dump(1) + "\n");
	}

	// the stats of the land as it now is (with the hand layer), and each feature's checks
	Grid fin = g;
	{
		Grid q;
		if (ReadHeights(folder / "heights.png", m, scale, q)) fin = q;
		for (size_t i = 0; i < fin.v.size(); i++) fin.v[i] += hand.v[i] * (scale / m.scale);
	}
	out = StatsOf(m, fin, hasSea, sea);
	for (json& fr : featRep) {
		const int idx = fr["index"].get<int>();
		const Feature* fp = nullptr;
		for (const Feature& f : feats) if (f.index == idx) fp = &f;
		if (!fp) continue;
		const Feature& f = *fp;
		if (f.kind == "plateau" || f.kind == "flat") {
			int inside = 0, flat = 0;
			double s = 0;
			for (int z = m.E; z < m.H - m.E; z++)
				for (int x = m.E; x < m.W - m.E; x++) {
					const V2 p = m.pos(x, z) + V2{ 2.5f, 2.5f };
					if (AreaSd(f.shape, f.warped(p)) > (f.kind == "plateau" ? -Lerp(70.0f, 12.0f, std::clamp(Num(f.j, "cliffs", 0.5f), 0.0f, 1.0f)) * 0.7f : -20.0f)) continue;
					const float v[4] = { fin.at(x, z), fin.at(x + 1, z), fin.at(x, z + 1), fin.at(x + 1, z + 1) };
					const float rise = *std::max_element(v, v + 4) - *std::min_element(v, v + 4);
					inside++;
					flat += rise < TILE * std::tan(8 * PI / 180);
					s += (v[0] + v[1] + v[2] + v[3]) / 4;
				}
			fr["tiles"] = inside;
			fr["flatShare"] = inside ? (double)flat / inside : 0.0;
			fr["meanM"] = inside ? s / inside : 0.0;
		}
		// the steepest stretch along each ramp and pass path, 5 m at a time
		auto pathSlope = [&](V2 a, V2 b) {
			const float L = Len(b - a);
			float worst = 0;
			const int n = std::max(1, (int)(L / 5));
			for (int i = 0; i < n; i++) {
				const V2 p0 = a + (b - a) * ((float)i / n), p1 = a + (b - a) * ((float)(i + 1) / n);
				const float dh = std::abs(fin.sample(m.vx(p1.x), m.vz(p1.z)) - fin.sample(m.vx(p0.x), m.vz(p0.z)));
				worst = std::max(worst, std::atan(dh / (L / n)) * 180 / PI);
			}
			return worst;
		};
		auto waySlope = [&](const json& way) {
			std::vector<V2> pts;
			for (const json& p : way) pts.push_back({ p[0].get<float>(), p[1].get<float>() });
			float worst = 0;
			for (size_t i = 0; i + 1 < pts.size(); i++) worst = std::max(worst, pathSlope(pts[i], pts[i + 1]));
			return worst;
		};
		if (fr.contains("ramps"))
			for (json& r : fr["ramps"]) r["maxSlopeDeg"] = waySlope(r["way"]);
		if (fr.contains("passPaths"))
			for (json& r : fr["passPaths"]) r["maxSlopeDeg"] = waySlope(r["way"]);
		if (f.kind == "range" || f.kind == "hill" || f.kind == "island") {
			float peak = -1e30f;
			for (int z = 0; z < m.gh(); z++)
				for (int x = 0; x < m.gw(); x++)
					if (AreaSd(f.shape, f.warped(m.pos(x, z))) < (f.kind == "range" ? Num(f.j, "width", 240) / 2 : 0)) peak = std::max(peak, fin.at(x, z));
			fr["peakM"] = peak;
		}
		if (f.kind == "island" && hasSea) {
			int land = 0;
			for (int z = m.E; z < m.H - m.E; z++)
				for (int x = m.E; x < m.W - m.E; x++)
					if (AreaSd(f.shape, m.pos(x, z)) < f.shape.size() * 0.5f && fin.at(x, z) > sea) land++;
			fr["landTiles"] = land;
		}
	}
	out["features"] = featRep;
	out["plan"] = { { "features", (int)feats.size() }, { "seed", seed }, { "base", base }, { "seaLevel", hasSea ? json(sea) : json() } };
	out["heightScale"] = scale;
	out["raisedByM"] = raisedBy;
	out["lakes"] = lakesJ;
	out["handLayer"] = handAny ? "kept: heights.hand.png has edits, untouched" : "empty";
	out["warnings"] = warnings;
	timing["totalMs"] = Ms(t0);
	out["timeMs"] = timing;
	out["heightsHash"] = gen["heightsHash"];
	out["size"] = { { "tiles", { m.W, m.H } }, { "edge", m.E }, { "playableM", { m.playW(), m.playH() } } };
	return true;
}

bool Stats(const fs::path& folder, json& out, std::string& error)
{
	Map m;
	if (!ReadMap(folder, m, error))
		return false;
	Grid g, hand;
	if (!ReadHeights(folder / "heights.png", m, m.scale, g)) { error = "can't read heights.png"; return false; }
	bool any = false;
	ReadHand(folder, m, hand, any);
	for (size_t i = 0; i < g.v.size(); i++) g.v[i] += hand.v[i];
	const json t = m.raw.value("terrain", json::object());
	// the sea as the generator last made it (it may have raised the land), else the plan's
	json sl = t.is_object() ? t.value("seaLevel", json()) : json();
	if (t.is_object() && t.contains("generated") && t["generated"].is_object() && t["generated"].contains("seaLevel"))
		sl = t["generated"]["seaLevel"];
	const bool hasSea = sl.is_number();
	out = StatsOf(m, g, hasSea, hasSea ? sl.get<float>() : 0.0f);
	out["heightScale"] = m.scale;
	out["handLayer"] = any ? "has edits" : "empty";
	return true;
}

// ================= heightmap import =================

bool ImportHeightmap(const fs::path& folder, const json& o, json& out, std::string& error)
{
	Map m;
	if (!ReadMap(folder, m, error))
		return false;
	const std::string file = o.value("file", "");
	if (file.empty()) { error = "heightmap: no \"file\""; return false; }
	Raster r;
	if (!LoadRaster(PathFromUtf8(file), o, r, error))
		return false;
	// the crop of the picture (pixels), and the vertices it covers
	double cx0 = 0, cy0 = 0, cx1 = r.w, cy1 = r.h;
	if (o.contains("crop") && o["crop"].is_array() && o["crop"].size() >= 4) {
		cx0 = o["crop"][0].get<double>(); cy0 = o["crop"][1].get<double>(); cx1 = o["crop"][2].get<double>(); cy1 = o["crop"][3].get<double>();
		if (!(cx1 > cx0 && cy1 > cy0)) { error = "heightmap: crop [x0, y0, x1, y1] with x1 > x0, y1 > y0"; return false; }
	}
	int X0, X1, Z0, Z1;
	Cover(m, o.value("cover", "playable"), X0, X1, Z0, Z1);
	// pixels a vertex spans: above 1.5 several samples are averaged
	const double ppv = std::max((cx1 - cx0) / (X1 - X0 + 1), (cy1 - cy0) / (Z1 - Z0 + 1));
	const int k = std::clamp((int)std::ceil(ppv), 1, 8);
	Grid s;
	s.init(m.gw(), m.gh(), 0);
	for (int z = 0; z < m.gh(); z++)
		for (int x = 0; x < m.gw(); x++) {
			double sum = 0;
			int n = 0;
			for (int sz = 0; sz < k; sz++)
				for (int sx = 0; sx < k; sx++) {
					const double fx = x - 0.5 + (sx + 0.5) / k, fz = z - 0.5 + (sz + 0.5) / k;
					const double u = cx0 + (fx - X0 + 0.5) / (X1 - X0 + 1) * (cx1 - cx0) - 0.5;
					const double v = cy0 + ((Z1 - fz) + 0.5) / (Z1 - Z0 + 1) * (cy1 - cy0) - 0.5;
					const float val = r.sample(Mirror(u, r.w), Mirror(v, r.h));
					if (!std::isnan(val)) { sum += val; n++; }
				}
			s.at(x, z) = n ? (float)(sum / n) : 0.0f;
		}
	// the picture's values to metres
	float smin = 1e30f, smax = -1e30f;
	for (int z = Z0; z <= Z1; z++)
		for (int x = X0; x <= X1; x++) { smin = std::min(smin, s.at(x, z)); smax = std::max(smax, s.at(x, z)); }
	json meta = json::object();
	if (o.contains("verticalScale") && o["verticalScale"].is_number()) {
		const float vs = o["verticalScale"].get<float>(), off = Num(o, "offset", 0);
		for (float& v : s.v) v = v * vs + off;
		meta["verticalScale"] = vs;
		meta["offset"] = off;
	}
	else {
		float lo = 2, hi = 60;
		if (o.contains("range") && o["range"].is_array() && o["range"].size() >= 2) { lo = o["range"][0].get<float>(); hi = o["range"][1].get<float>(); }
		const float span = smax > smin ? smax - smin : 1;
		for (float& v : s.v) v = lo + (v - smin) / span * (hi - lo);
		meta["range"] = { lo, hi };
	}
	for (float& v : s.v) v = std::max(v, 0.0f);
	meta["from"] = PathToUtf8(PathFromUtf8(file).filename());
	meta["format"] = r.format;
	meta["pixels"] = { r.w, r.h };
	meta["crop"] = { cx0, cy0, cx1, cy1 };
	meta["cover"] = o.value("cover", "playable");
	json src;
	if (!WriteSource(folder, m, s, meta, src)) { error = "can't write layers/source.png"; return false; }
	src["weight"] = std::clamp(Num(o, "weight", 1.0f), 0.0f, 1.0f);
	json raw = m.raw;
	DefaultImportTerrain(raw, src["min"].get<float>());
	raw["terrain"]["source"] = src;
	json ref = { { "use", "terrain.source" }, { "kind", "heightmap" }, { "file", meta["from"] },
		{ "licence", o.value("licence", "given by the user; check before the map ships") } };
	if (o.contains("attribution")) ref["attribution"] = o["attribution"];
	SetReference(raw, ref);
	if (!WriteMapJson(folder, raw)) { error = "can't write map.json"; return false; }
	out = json::object();
	out["source"] = src;
	if (o.value("generate", true)) {
		json st;
		if (!Generate(folder, st, error))
			return false;
		out["generated"] = st;
	}
	return true;
}

// ================= elevation import =================

namespace {
	struct Area { double s = 0, w = 0, n = 0, e = 0; };
	bool AreaOf(const json& o, Area& a, std::string& error)
	{
		if (o.contains("bbox") && o["bbox"].is_array() && o["bbox"].size() >= 4) {
			a.s = o["bbox"][0].get<double>(); a.w = o["bbox"][1].get<double>(); a.n = o["bbox"][2].get<double>(); a.e = o["bbox"][3].get<double>();
		}
		else if (o.contains("centre") && o["centre"].is_array() && o["centre"].size() >= 2) {
			const double lat = o["centre"][0].get<double>(), lon = o["centre"][1].get<double>();
			double ew = 5, ns = 5;
			const json& sk = o.value("sizeKm", json(5.0));
			if (sk.is_number()) ew = ns = sk.get<double>();
			else if (sk.is_array() && sk.size() >= 2) { ew = sk[0].get<double>(); ns = sk[1].get<double>(); }
			const double dlat = ns / 110.574 / 2, dlon = ew / (111.320 * std::cos(lat * PI / 180)) / 2;
			a = { lat - dlat, lon - dlon, lat + dlat, lon + dlon };
		}
		else { error = "elevation: \"bbox\" [south, west, north, east] or \"centre\" [lat, lon] with \"sizeKm\""; return false; }
		if (!(a.n > a.s && a.e > a.w) || a.s < -90 || a.n > 90) { error = "elevation: the area is empty or off the globe"; return false; }
		return true;
	}
	double KmEW(const Area& a) { return (a.e - a.w) * 111.320 * std::cos((a.s + a.n) / 2 * PI / 180); }
	double KmNS(const Area& a) { return (a.n - a.s) * 110.574; }
	std::string TileName(const std::string& src, int lat, int lon)
	{
		char b[96];
		if (src == "srtm")
			snprintf(b, sizeof(b), "%c%02d%c%03d", lat >= 0 ? 'N' : 'S', std::abs(lat), lon >= 0 ? 'E' : 'W', std::abs(lon));
		else
			snprintf(b, sizeof(b), "Copernicus_DSM_COG_10_%c%02d_00_%c%03d_00_DEM", lat >= 0 ? 'N' : 'S', std::abs(lat), lon >= 0 ? 'E' : 'W', std::abs(lon));
		return b;
	}
	const char* COPERNICUS_LICENCE = "Copernicus DEM licence (License-COPDEM-30): free of charge for the public, with attribution; modified data credited as produced using Copernicus WorldDEM-30";
	const char* COPERNICUS_ATTRIBUTION = "produced using Copernicus WorldDEM-30 \xC2\xA9 DLR e.V. 2010-2014 and \xC2\xA9 Airbus Defence and Space GmbH 2014-2018 provided under COPERNICUS by the European Union and ESA; all rights reserved";
	const char* SRTM_LICENCE = "public domain (NASA/USGS SRTM)";
	const char* OSGB_LICENCE = "Open Government Licence v3.0 (www.nationalarchives.gov.uk/doc/open-government-licence/version/3/): free for any use, with attribution";
	const char* OSGB_ATTRIBUTION = "Contains OS data \xC2\xA9 Crown copyright and database right 2026 (OS Terrain 50)";
	const char* SRTM_ATTRIBUTION = "NASA Shuttle Radar Topography Mission (SRTM) Global 1 arc second, NASA JPL / USGS EROS";
}

json TilesFor(const json& o, const fs::path& mapFolder)
{
	json out = json::object();
	Area a;
	std::string error;
	if (!AreaOf(o, a, error)) { out["error"] = error; return out; }
	const std::string src = Lower(o.value("source", "copernicus"));
	// the edge band continues the real land: the area widened by the band's share
	double mx = 0, mz = 0;
	if (!mapFolder.empty()) {
		Map m;
		if (ReadMap(mapFolder, m, error)) {
			mx = (double)m.E / std::max(1, m.W - 2 * m.E);
			mz = (double)m.E / std::max(1, m.H - 2 * m.E);
		}
	}
	const Area wide = { a.s - (a.n - a.s) * mz, a.w - (a.e - a.w) * mx, a.n + (a.n - a.s) * mz, a.e + (a.e - a.w) * mx };
	std::vector<std::string> files;
	for (const json& f : o.value("files", json::array())) if (f.is_string()) files.push_back(f.get<std::string>());
	json tiles = json::array();
	for (int lat = (int)std::floor(wide.s); lat <= (int)std::floor(wide.n - 1e-9); lat++)
		for (int lon = (int)std::floor(wide.w); lon <= (int)std::floor(wide.e - 1e-9); lon++) {
			const std::string name = TileName(src, lat, lon);
			json t = { { "tile", name }, { "lat", lat }, { "lon", lon } };
			if (src == "srtm") {
				t["source"] = "NASA SRTM Global 1 arc second (SRTMGL1 v003), LP DAAC";
				t["url"] = "https://e4ftl01.cr.usgs.gov/MEASURES/SRTMGL1.003/2000.02.11/" + name + ".SRTMGL1.hgt.zip";
				t["download"] = "needs a free NASA Earthdata login; unzip to " + name + ".hgt";
				t["sizeMB"] = "about 10-25 zipped, 25.9 unzipped (3601 x 3601, 16-bit)";
				t["licence"] = SRTM_LICENCE;
				t["attribution"] = SRTM_ATTRIBUTION;
			}
			else {
				t["source"] = "Copernicus DEM GLO-30 (30 m), the AWS open data copy";
				t["url"] = "https://copernicus-dem-30m.s3.amazonaws.com/" + name + "/" + name + ".tif";
				t["download"] = "no login; no file exists for tiles that are all sea";
				t["sizeMB"] = "about 15-50 (a compressed 32-bit GeoTIFF, 3600 pixels a side south of 50 degrees, narrower further north)";
				t["licence"] = COPERNICUS_LICENCE;
				t["attribution"] = COPERNICUS_ATTRIBUTION;
			}
			std::string given;
			for (const std::string& f : files) {
				const std::string fl = Lower(PathToUtf8(PathFromUtf8(f).filename()));
				int la, lo;
				if (fl.find(Lower(name)) != std::string::npos || (HgtCorner(fl, la, lo) && la == lat && lo == lon && fl.find("copernicus") == std::string::npos))
					given = f;
			}
			t["given"] = given.empty() ? json(false) : json(given);
			tiles.push_back(t);
		}
	out["area"] = { { "bbox", { a.s, a.w, a.n, a.e } }, { "km", { KmEW(a), KmNS(a) } }, { "withEdgeBand", { wide.s, wide.w, wide.n, wide.e } } };
	out["tiles"] = tiles;
	out["note"] = "nothing is downloaded: ask the user before each download (source, area, size, licence), then pass the files";
	return out;
}

bool ImportElevation(const fs::path& folder, const json& o, json& out, std::string& error)
{
	Map m;
	if (!ReadMap(folder, m, error))
		return false;
	const std::string src = Lower(o.value("source", "copernicus"));
	// OS Terrain 50 (and any Esri ASCII grid in metres): the area as British National Grid metres
	// "bng": [east0, north0, east1, north1] (or "bngKm"), the tiles in "files"
	const bool metric = src == "os-terrain-50" || src == "osgb" || src == "asc";
	Area a;
	if (metric) {
		const json& b = o.contains("bng") ? o["bng"] : o.value("bngKm", json());
		const double f = o.contains("bng") ? 1.0 : 1000.0;
		if (!b.is_array() || b.size() < 4) { error = "elevation (os-terrain-50): \"bng\" [east0, north0, east1, north1] metres (or \"bngKm\") and \"files\" (the .asc tiles)"; return false; }
		a.w = b[0].get<double>() * f; a.s = b[1].get<double>() * f; a.e = b[2].get<double>() * f; a.n = b[3].get<double>() * f;
	}
	else if (!AreaOf(o, a, error))
		return false;
	auto kmEWOf = [&](const Area& q) { return metric ? (q.e - q.w) / 1000.0 : KmEW(q); };
	auto kmNSOf = [&](const Area& q) { return metric ? (q.n - q.s) / 1000.0 : KmNS(q); };
	// the map's shape: the area cut to the playable area's aspect (keepAspect), centred
	const double mapAspect = (double)(m.W - 2 * m.E) / (m.H - 2 * m.E);
	if (o.value("keepAspect", true)) {
		const double ew = kmEWOf(a), ns = kmNSOf(a);
		if (ew / ns > mapAspect) {
			const double keep = ns * mapAspect / ew, mid = (a.w + a.e) / 2, half = (a.e - a.w) / 2 * keep;
			a.w = mid - half; a.e = mid + half;
		}
		else {
			const double keep = ew / mapAspect / ns, mid = (a.s + a.n) / 2, half = (a.n - a.s) / 2 * keep;
			a.s = mid - half; a.n = mid + half;
		}
	}
	json tiles;
	json missing = json::array();
	std::vector<std::string> files;
	if (metric) {
		// the files as given (OS Terrain 50's tiles are named by the grid: SW53.asc)
		tiles = { { "tiles", json::array() } };
		for (const json& f : o.value("files", json::array()))
			if (f.is_string()) {
				files.push_back(f.get<std::string>());
				tiles["tiles"].push_back({ { "tile", PathToUtf8(PathFromUtf8(f.get<std::string>()).stem()) }, { "source", "OS Terrain 50 (Ordnance Survey, 50 m grid)" }, { "given", f } });
			}
		if (files.empty()) { error = "elevation (os-terrain-50): no \"files\" (the .asc tiles over the area)"; return false; }
	}
	else {
		json tilesOpt = o;
		tilesOpt["bbox"] = { a.s, a.w, a.n, a.e };
		tilesOpt.erase("centre");
		tiles = TilesFor(tilesOpt, folder);
		for (const json& t : tiles["tiles"]) {
			if (t["given"].is_string()) files.push_back(t["given"].get<std::string>());
			else missing.push_back(t);
		}
	}
	if (!missing.empty() && !o.value("allowMissing", false)) {
		out = { { "needed", tiles }, { "missing", missing } };
		error = std::to_string(missing.size()) + " elevation tile(s) not given: ask the user before downloading them (\"needed\" names source, URL, size, licence)";
		return false;
	}
	std::vector<Raster> rs;
	for (const std::string& f : files) {
		Raster r;
		if (!LoadRaster(PathFromUtf8(f), o, r, error)) return false;
		if (!r.geo) { error = f + ": no geographic position in the file (a GeoTIFF or an SRTM .hgt by its name)"; return false; }
		if (r.metric != metric) { error = f + (metric ? ": not a grid in metres (os-terrain-50 takes .asc tiles)" : ": a grid in metres needs \"source\": \"os-terrain-50\" and \"bng\""); return false; }
		rs.push_back(std::move(r));
	}
	size_t lastR = 0;
	auto elev = [&](double lat, double lon) -> float {
		for (size_t k = 0; k < rs.size(); k++) {
			const size_t ri = (k + lastR) % rs.size();
			const Raster& r = rs[ri];
			const double x = (lon - r.lon0) / r.dlon, y = (lat - r.lat0) / r.dlat;
			if (x >= -0.5 && y >= -0.5 && x <= r.w - 0.5 && y <= r.h - 0.5) {
				const float v = r.sample(x, y);
				if (!std::isnan(v)) { lastR = ri; return v; }
			}
		}
		return NAN;
	};
	// metres on the map: the vertex grid over the area, the edge band beyond it
	const double kmEW = kmEWOf(a), kmNS = kmNSOf(a);
	const double hRatio = (m.W - 2 * m.E) * TILE / (kmEW * 1000.0); // map metres per real metre
	const double realPerVertex = TILE / hRatio;
	const double pxM = rs.empty() ? 30 : metric ? std::abs(rs[0].dlat) : std::abs(rs[0].dlat) * 110574.0;
	const int k = std::clamp((int)std::ceil(realPerVertex / pxM), 1, 6);
	Grid e;
	e.init(m.gw(), m.gh(), 0);
	int voids = 0;
	const int pw = m.W - 2 * m.E, ph = m.H - 2 * m.E;
	for (int z = 0; z < m.gh(); z++)
		for (int x = 0; x < m.gw(); x++) {
			double sum = 0;
			int n = 0;
			for (int sz = 0; sz < k; sz++)
				for (int sx = 0; sx < k; sx++) {
					const double fx = x - 0.5 + (sx + 0.5) / k, fz = z - 0.5 + (sz + 0.5) / k;
					const double lon = a.w + (fx - m.E) / pw * (a.e - a.w), lat = a.s + (fz - m.E) / ph * (a.n - a.s);
					const float v = elev(lat, lon);
					if (!std::isnan(v)) { sum += v; n++; }
				}
			if (n) e.at(x, z) = (float)(sum / n);
			else { e.at(x, z) = NAN; voids++; }
		}
	// voids: OS Terrain 50 leaves out the tiles that are all sea, so a void there is sea
	if (voids && metric) {
		for (float& v : e.v) if (std::isnan(v)) v = -2;
	}
	// voids (no data, a missing tile): the nearest known value, row by row then column by column
	else if (voids) {
		for (int pass = 0; pass < 4; pass++)
			for (int z = 0; z < m.gh(); z++)
				for (int x = 0; x < m.gw(); x++) {
					if (!std::isnan(e.at(x, z))) continue;
					float s = 0; int n = 0;
					for (int r = 1; r < 40 && !n; r++) {
						const std::pair<int, int> dirs4[4] = { { r, 0 }, { -r, 0 }, { 0, r }, { 0, -r } };
						for (auto [dx, dz] : dirs4) {
							const int nx = x + dx, nz = z + dz;
							if (nx >= 0 && nz >= 0 && nx < m.gw() && nz < m.gh() && !std::isnan(e.at(nx, nz))) { s += e.at(nx, nz); n++; }
						}
					}
					if (n) e.at(x, z) = s / n;
				}
		for (float& v : e.v) if (std::isnan(v)) v = 0;
	}
	// the sea: real heights at or below seaBelow (0.5 m: Copernicus and SRTM give the sea as 0, OS
	// Terrain 50 about -1.7); "seaFromEdge" (default for os-terrain-50): only what joins the edge of
	// the map, so land below the sea's level behind its banks (the Fens) stays land
	const float seaBelow = Num(o, "seaBelow", 0.5f);
	std::vector<uint8_t> isSea(e.v.size(), 0);
	for (size_t i = 0; i < e.v.size(); i++) isSea[i] = e.v[i] <= seaBelow;
	if (o.value("seaFromEdge", metric)) {
		std::vector<uint8_t> keep(e.v.size(), 0);
		std::vector<int> stack;
		const int w = m.gw(), hh = m.gh();
		// the seeds: "seaAt" points known to be sea (in the area's own coordinates: BNG metres or
		// [lat, lon]), else every low vertex on the map's edge
		const json seaAt = o.value("seaAt", json::array());
		for (const json& pt : seaAt) {
			if (!pt.is_array() || pt.size() < 2) continue;
			double fx, fz;
			if (metric) { fx = m.E + (pt[0].get<double>() - a.w) / (a.e - a.w) * pw; fz = m.E + (pt[1].get<double>() - a.s) / (a.n - a.s) * ph; }
			else { fx = m.E + (pt[1].get<double>() - a.w) / (a.e - a.w) * pw; fz = m.E + (pt[0].get<double>() - a.s) / (a.n - a.s) * ph; }
			const int x = std::clamp((int)std::lround(fx), 0, w - 1), z = std::clamp((int)std::lround(fz), 0, hh - 1);
			if (isSea[(size_t)z * w + x] && !keep[(size_t)z * w + x]) { keep[(size_t)z * w + x] = 1; stack.push_back(z * w + x); }
		}
		if (seaAt.empty())
			for (int z = 0; z < hh; z++)
				for (int x = 0; x < w; x++)
					if ((x == 0 || z == 0 || x == w - 1 || z == hh - 1) && isSea[(size_t)z * w + x]) { keep[(size_t)z * w + x] = 1; stack.push_back(z * w + x); }
		while (!stack.empty()) {
			const int i = stack.back();
			stack.pop_back();
			const int x = i % w, z = i / w;
			const int nb[4][2] = { { x + 1, z }, { x - 1, z }, { x, z + 1 }, { x, z - 1 } };
			for (auto& q : nb)
				if (q[0] >= 0 && q[1] >= 0 && q[0] < w && q[1] < hh) {
					const size_t u = (size_t)q[1] * w + q[0];
					if (isSea[u] && !keep[u]) { keep[u] = 1; stack.push_back((int)u); }
				}
		}
		isSea.swap(keep);
	}
	int seaV = 0, landV = 0;
	float emin = 1e30f, emax = -1e30f;
	for (int z = m.E; z <= m.H - m.E; z++)
		for (int x = m.E; x <= m.W - m.E; x++) {
			const float v = e.at(x, z);
			if (isSea[(size_t)z * m.gw() + x]) seaV++;
			else { landV++; emin = std::min(emin, v); emax = std::max(emax, v); }
		}
	const std::string seaOpt = o.contains("sea") && o["sea"].is_boolean() ? (o["sea"].get<bool>() ? "true" : "false") : "auto";
	const bool sea = seaOpt == "true" || (seaOpt == "auto" && seaV > (pw + 1) * (ph + 1) / 200);
	if (!landV) { emin = 0; emax = 1; }
	if (sea) emin = 0;
	// the vertical scale: the real slopes kept (as the land is squeezed), times "exaggerate", or a number
	double vs = hRatio * Num(o, "exaggerate", 1.0f);
	std::string vsMode = "keep-slopes";
	if (o.contains("verticalScale") && o["verticalScale"].is_number()) { vs = o["verticalScale"].get<double>(); vsMode = "given"; }
	// the steepest: at most maxSteepShare of the land's tiles above maxSlopeDeg, by flattening
	const float maxDeg = Num(o, "maxSlopeDeg", 40), maxShare = Num(o, "maxSteepShare", 0.08f);
	std::vector<float> rises;
	for (int z = m.E; z < m.H - m.E; z++)
		for (int x = m.E; x < m.W - m.E; x++) {
			const float v[4] = { e.at(x, z), e.at(x + 1, z), e.at(x, z + 1), e.at(x + 1, z + 1) };
			if (sea && isSea[(size_t)z * m.gw() + x] && isSea[(size_t)z * m.gw() + x + 1] && isSea[(size_t)(z + 1) * m.gw() + x] && isSea[(size_t)(z + 1) * m.gw() + x + 1]) continue;
			rises.push_back(*std::max_element(v, v + 4) - *std::min_element(v, v + 4));
		}
	bool flattened = false;
	if (!rises.empty() && maxDeg > 0 && maxDeg < 89) {
		std::sort(rises.begin(), rises.end());
		const size_t q = std::min(rises.size() - 1, (size_t)std::floor((1 - std::clamp(maxShare, 0.0f, 1.0f)) * (rises.size() - 1)));
		const double rq = rises[q];
		if (rq > 0) {
			const double vsMax = TILE * std::tan(maxDeg * PI / 180) / rq;
			if (vs > vsMax) { vs = vsMax; flattened = true; }
		}
	}
	const float floorM = Num(o, "floor", 2), seaDepth = Num(o, "seaDepth", 6);
	const float seaLevel = floorM + seaDepth;
	Grid h;
	h.init(m.gw(), m.gh(), 0);
	for (size_t i = 0; i < h.v.size(); i++)
		h.v[i] = (sea ? seaLevel + 0.6f : floorM) + (float)((std::max(e.v[i], emin) - emin) * vs);
	// (land below 0 behind banks, the Fens: kept above the sea's level by the max with emin = 0)
	if (sea) {
		// below the sea level, deeper away from the land (a chamfer distance in vertices)
		std::vector<float> dist(h.v.size(), 1e9f);
		for (size_t i = 0; i < h.v.size(); i++) if (!isSea[i]) dist[i] = 0;
		const int w = m.gw(), hh = m.gh();
		for (int z = 0; z < hh; z++)
			for (int x = 0; x < w; x++) {
				float& d = dist[(size_t)z * w + x];
				if (x > 0) d = std::min(d, dist[(size_t)z * w + x - 1] + 1);
				if (z > 0) d = std::min(d, dist[(size_t)(z - 1) * w + x] + 1);
				if (x > 0 && z > 0) d = std::min(d, dist[(size_t)(z - 1) * w + x - 1] + 1.414f);
				if (x < w - 1 && z > 0) d = std::min(d, dist[(size_t)(z - 1) * w + x + 1] + 1.414f);
			}
		for (int z = hh - 1; z >= 0; z--)
			for (int x = w - 1; x >= 0; x--) {
				float& d = dist[(size_t)z * w + x];
				if (x < w - 1) d = std::min(d, dist[(size_t)z * w + x + 1] + 1);
				if (z < hh - 1) d = std::min(d, dist[(size_t)(z + 1) * w + x] + 1);
				if (x < w - 1 && z < hh - 1) d = std::min(d, dist[(size_t)(z + 1) * w + x + 1] + 1.414f);
				if (x > 0 && z < hh - 1) d = std::min(d, dist[(size_t)(z + 1) * w + x - 1] + 1.414f);
			}
		for (size_t i = 0; i < h.v.size(); i++)
			if (isSea[i]) h.v[i] = seaLevel - 0.6f - seaDepth * std::min(1.0f, dist[i] * TILE / 120.0f);
	}
	json meta = { { "from", "elevation" }, { "source", src }, { "bbox", metric ? json({ { "bng", { a.w, a.s, a.e, a.n } } }) : json({ a.s, a.w, a.n, a.e }) }, { "km", { kmEW, kmNS } },
		{ "horizontalScale", hRatio }, { "realMetresPerMapMetre", 1 / hRatio }, { "verticalScale", vs }, { "verticalMode", vsMode },
		{ "exaggeration", vs / hRatio }, { "flattenedForSlope", flattened }, { "realRangeM", { emin, emax } }, { "voidVertices", voids } };
	json srcJ;
	if (!WriteSource(folder, m, h, meta, srcJ)) { error = "can't write layers/source.png"; return false; }
	srcJ["weight"] = std::clamp(Num(o, "weight", 1.0f), 0.0f, 1.0f);
	json raw = m.raw;
	DefaultImportTerrain(raw, floorM);
	raw["terrain"]["source"] = srcJ;
	if (sea) raw["terrain"]["seaLevel"] = seaLevel;
	json tileNames = json::array();
	for (const json& t : tiles["tiles"]) if (t["given"].is_string()) tileNames.push_back(t["tile"]);
	json ref = { { "use", "terrain.source" }, { "kind", "elevation" }, { "source", tiles["tiles"].empty() ? json("") : tiles["tiles"][0]["source"] },
		{ "tiles", tileNames }, { "area", metric ? json({ { "bng", { a.w, a.s, a.e, a.n } } }) : json({ a.s, a.w, a.n, a.e }) },
		{ "licence", metric ? OSGB_LICENCE : src == "srtm" ? SRTM_LICENCE : COPERNICUS_LICENCE },
		{ "attribution", metric ? OSGB_ATTRIBUTION : src == "srtm" ? SRTM_ATTRIBUTION : COPERNICUS_ATTRIBUTION },
		{ "ships", "name the data and its attribution in THIRD-PARTY-NOTICES.md when the map ships" } };
	SetReference(raw, ref);
	if (!WriteMapJson(folder, raw)) { error = "can't write map.json"; return false; }
	out = json::object();
	out["source"] = srcJ;
	out["tiles"] = tiles;
	out["sea"] = sea ? json(seaLevel) : json(false);
	if (o.value("generate", true)) {
		json st;
		if (!Generate(folder, st, error)) return false;
		out["generated"] = st;
	}
	return true;
}

// ================= the command line =================

int RunCommandLine(const std::vector<std::string>& args)
{
	auto usage = [] {
		printf("usage: wkbai --author terrain <folder> [--seed <n>] [--out <file.json>]\n"
			"       wkbai --author terrain-stats <folder> [--out <file.json>]\n"
			"       wkbai --author heightmap <folder> --options <file.json> [--out <file.json>]\n"
			"       wkbai --author elevation <folder> --options <file.json> [--out <file.json>]\n"
			"       wkbai --author elevation-tiles --options <file.json> [--folder <folder>] [--out <file.json>]\n");
		return 2;
	};
	if (args.empty()) return usage();
	const std::string cmd = args[0];
	std::string folderArg, outFile, optFile;
	uint32_t seed = 0;
	for (size_t i = 1; i < args.size(); i++) {
		if (args[i] == "--out" && i + 1 < args.size()) outFile = args[++i];
		else if (args[i] == "--options" && i + 1 < args.size()) optFile = args[++i];
		else if (args[i] == "--seed" && i + 1 < args.size()) seed = (uint32_t)std::stoul(args[++i]);
		else if (args[i] == "--folder" && i + 1 < args.size()) folderArg = args[++i];
		else if (folderArg.empty()) folderArg = args[i];
		else return usage();
	}
	json opts = json::object();
	if (!optFile.empty() && (!ReadJson(PathFromUtf8(optFile), opts) || !opts.is_object())) {
		printf("AUTHOR failed: can't read the options %s\n", optFile.c_str());
		return 1;
	}
	if (cmd == "elevation-tiles") {
		const json t = TilesFor(opts, folderArg.empty() ? fs::path() : MapFolder::Resolve(folderArg));
		OutputJson(t, outFile);
		printf(t.contains("error") ? "AUTHOR failed: %s\n" : "AUTHOR ok, %s\n", t.contains("error") ? t["error"].get<std::string>().c_str() : "elevation tiles listed");
		return t.contains("error") ? 1 : 0;
	}
	if (folderArg.empty()) return usage();
	const fs::path folder = MapFolder::Resolve(folderArg);
	if (folder.empty()) {
		printf("AUTHOR failed: no map folder %s\n", folderArg.c_str());
		return 1;
	}
	json out;
	std::string error;
	bool ok;
	if (cmd == "terrain") ok = Generate(folder, out, error, seed);
	else if (cmd == "terrain-stats") ok = Stats(folder, out, error);
	else if (cmd == "heightmap") ok = ImportHeightmap(folder, opts, out, error);
	else if (cmd == "elevation") ok = ImportElevation(folder, opts, out, error);
	else return usage();
	if (!ok) {
		if (!out.is_null()) OutputJson(out, outFile);
		printf("AUTHOR failed: %s\n", error.c_str());
		fflush(stdout);
		return 1;
	}
	OutputJson(out, outFile);
	const json& st = out.contains("generated") ? out["generated"] : out;
	if (st.contains("walkable"))
		printf("AUTHOR ok, %s: walkable %.3f, heights %.1f-%.1f m%s\n", cmd.c_str(), st["walkable"]["share"].get<double>(),
			st["heights"]["minM"].get<double>(), st["heights"]["maxM"].get<double>(),
			st.contains("timeMs") ? (", " + std::to_string((int)st["timeMs"]["totalMs"].get<double>()) + " ms").c_str() : "");
	else
		printf("AUTHOR ok, %s\n", cmd.c_str());
	fflush(stdout);
	return 0;
}

} // namespace MapTerrain
