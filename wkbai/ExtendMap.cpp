// wkbai - Extend map: the land continued beyond a map's edge (a graphics setting)
// Licensed under the GNU General Public License 3

#include "ExtendMap.h"
#include "terrain.h"
#include "WaterDesign.h"
#include "gfx/bitmap.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <iterator>
#include <map>
#include <queue>
#include <string>
#include <vector>
#include <thread>
#include <atomic>

namespace {

// bumped whenever the generator's output changes (the cache's key, phase 5)
constexpr uint64_t GENERATOR_VERSION = 2;

// the near ring's reach at "Near"
constexpr int NEAR_TILES = 64;

// ---------------------------------------------------------------------------------------------
// Hashing and noise: every random choice from the map's seed and a place, never a std::
// distribution, so the same map gives the same land on every load

// f(i) for i in 0..n-1 on all the processor's cores (the far rings' heights and normals: every
// call independent, the land's functions only read)
template <class F> void ParallelFor(int n, F&& f)
{
	const int T = std::max(1, std::min((int)std::thread::hardware_concurrency(), n));
	std::atomic<int> next{ 0 };
	std::vector<std::thread> threads;
	for (int t = 0; t < T; t++)
		threads.emplace_back([&]() {
			for (int i; (i = next++) < n;)
				f(i);
		});
	for (std::thread& t : threads)
		t.join();
}

uint64_t Fnv1a(uint64_t h, const void* data, size_t size)
{
	const uint8_t* p = (const uint8_t*)data;
	for (size_t i = 0; i < size; i++) {
		h ^= p[i];
		h *= 0x100000001B3ull;
	}
	return h;
}
uint64_t Mix(uint64_t x)
{
	x += 0x9E3779B97F4A7C15ull;
	x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
	x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
	return x ^ (x >> 31);
}
uint64_t HashAt(uint64_t seed, int x, int z, int salt)
{
	return Mix(seed ^ Mix(((uint64_t)(uint32_t)x << 32) ^ (uint32_t)z ^ ((uint64_t)salt << 56)));
}
float Unit(uint64_t h) { return (float)(h >> 40) * (1.0f / 16777216.0f); } // 0 to 1

// gradient noise with its derivatives (quintic fade), about -1 to 1, a cell per unit of (x, z):
// a random direction at each lattice point, so unlike value noise it shows no squares
struct Noise { float v, dx, dz; };
Noise ValueNoiseD(uint64_t seed, float x, float z, int salt)
{
	const int ix = (int)std::floor(x), iz = (int)std::floor(z);
	const float fx = x - ix, fz = z - iz;
	const float ux = fx * fx * fx * (fx * (fx * 6 - 15) + 10), uz = fz * fz * fz * (fz * (fz * 6 - 15) + 10);
	const float dux = 30 * fx * fx * (fx * (fx - 2) + 1), duz = 30 * fz * fz * (fz * (fz - 2) + 1);
	auto grad = [&](int a, int b, float& gx, float& gz) {
		const float ang = Unit(HashAt(seed, ix + a, iz + b, salt)) * 6.2831853f;
		gx = std::cos(ang);
		gz = std::sin(ang);
	};
	float gax, gaz, gbx, gbz, gcx, gcz, gdx, gdz;
	grad(0, 0, gax, gaz);
	grad(1, 0, gbx, gbz);
	grad(0, 1, gcx, gcz);
	grad(1, 1, gdx, gdz);
	const float va = gax * fx + gaz * fz, vb = gbx * (fx - 1) + gbz * fz;
	const float vc = gcx * fx + gcz * (fz - 1), vd = gdx * (fx - 1) + gdz * (fz - 1);
	const float k = va - vb - vc + vd;
	const float v = va + ux * (vb - va) + uz * (vc - va) + ux * uz * k;
	const float dx = gax + ux * (gbx - gax) + uz * (gcx - gax) + ux * uz * (gax - gbx - gcx + gdx) + dux * (uz * k + vb - va);
	const float dz = gaz + ux * (gbz - gaz) + uz * (gcz - gaz) + ux * uz * (gaz - gbz - gcz + gdz) + duz * (ux * k + vc - va);
	constexpr float S = 1.4f;
	return { v * S, dx * S, dz * S };
}
float ValueNoise(uint64_t seed, float x, float z, int salt) { return ValueNoiseD(seed, x, z, salt).v; }

// each octave turned against the one before, so no direction lines up
constexpr float OCT_C = 0.8525f, OCT_S = 0.5227f; // cos, sin of 0.55 rad
inline void Turn(float& x, float& z) { const float t = OCT_C * x - OCT_S * z; z = OCT_S * x + OCT_C * z; x = t; }

// An octave's share at a sample spacing `foot` (tiles): all of it while its cells are at least
// twice the spacing, none at the spacing (the far rings' coarse grids: their octaves finer than the
// grid aliased into spikes, the play test of 2026-11-10); 0 keeps every octave
inline float OctaveShare(float wavelength, float foot)
{
	return foot <= 0 ? 1.0f : std::clamp(wavelength / foot - 1.0f, 0.0f, 1.0f);
}

// fBm, -1 to 1 (about), the first octave's cells `cell` tiles wide
float Fbm(uint64_t seed, float x, float z, float cell, int octaves, int salt, float foot = 0)
{
	float s = 0, a = 1, t = 0, qx = x / cell, qz = z / cell, l = cell;
	for (int o = 0; o < octaves; o++) {
		const float k = OctaveShare(l, foot);
		if (k > 0)
			s += k * a * ValueNoise(seed, qx + o * 17.3f, qz - o * 9.1f, salt + o);
		t += a;
		a *= 0.5f;
		qx *= 2.03f;
		qz *= 2.03f;
		l /= 2.03f;
		Turn(qx, qz);
	}
	return s / t;
}

// Hills that look worn: each octave damped where the octaves before it are steep, so slopes
// stay smooth and the detail gathers in the valleys and on the tops (as erosion leaves them)
float ErodedFbm(uint64_t seed, float x, float z, float cell, int octaves, int salt, float foot = 0)
{
	float s = 0, a = 1, t = 0, qx = x / cell, qz = z / cell, gx = 0, gz = 0, l = cell;
	float rc = 1, rs = 0; // the octave's turn so far, to bring its slope back to the first's frame
	for (int o = 0; o < octaves; o++) {
		const Noise n = ValueNoiseD(seed, qx + o * 31.7f, qz + o * 11.9f, salt + o);
		const float k = OctaveShare(l, foot);
		gx += k * (rc * n.dx + rs * n.dz);
		gz += k * (-rs * n.dx + rc * n.dz);
		s += k * a * n.v / (1.0f + 0.6f * (gx * gx + gz * gz));
		t += a;
		a *= 0.5f;
		qx *= 2.0f;
		qz *= 2.0f;
		l *= 0.5f;
		Turn(qx, qz);
		const float nc = rc * OCT_C - rs * OCT_S;
		rs = rs * OCT_C + rc * OCT_S;
		rc = nc;
	}
	return s / t;
}

// Mountain ranges: ridged multifractal, 0 (valleys) to about 1 (crests); each octave weighted
// by the one before, so the ridges branch and sharpen at the top
float Ridged(uint64_t seed, float x, float z, float cell, int octaves, int salt, float foot = 0)
{
	float s = 0, a = 1, t = 0, qx = x / cell, qz = z / cell, w = 1, l = cell;
	for (int o = 0; o < octaves; o++) {
		const float k = OctaveShare(l, foot);
		// (an octave left out counts at its mean, a third of its weight: the ranges keep their height)
		float n = k > 0 ? 1.0f - std::abs(ValueNoise(seed, qx + o * 5.3f, qz + o * 23.1f, salt + o)) : 0.0f;
		n = (n * n * k + (1.0f - k) / 3.0f) * w;
		w = std::clamp(n * 1.8f, 0.0f, 1.0f);
		s += a * n;
		t += a;
		a *= 0.5f;
		qx *= 2.1f;
		qz *= 2.1f;
		l /= 2.1f;
		Turn(qx, qz);
	}
	return s / t;
}

uint64_t MapSeed(const Terrain& map)
{
	uint64_t h = Fnv1a(0xCBF29CE484222325ull, &GENERATOR_VERSION, sizeof(GENERATOR_VERSION));
	if (!map.bcmData.empty())
		h = Fnv1a(h, map.bcmData.data(), map.bcmData.size());
	else
		h = Fnv1a(h, map.mapFile.data(), map.mapFile.size());
	h = Fnv1a(h, map.vertices.data(), map.vertices.size());
	if (!map.heightFraction.empty())
		h = Fnv1a(h, map.heightFraction.data(), map.heightFraction.size());
	return h;
}

std::shared_ptr<WaterDesign> ShiftedDesign(const std::shared_ptr<WaterDesign>& design, int W, int H, int n)
{
	if (!design)
		return nullptr;
	auto d = std::make_shared<WaterDesign>(*design); // the waterways and placed falls are in world units
	d->width = W;
	d->height = H;
	d->surface.assign((size_t)W * H, (char)WaterDesign::AUTO);
	for (int z = 0; z < design->height; z++)
		for (int x = 0; x < design->width; x++)
			d->surface[(size_t)(z + n) * W + (x + n)] = design->at(x, z);
	return d;
}

void SetSteps(Terrain& ext, size_t i, float height)
{
	const float steps = std::clamp(height / ext.scale, 0.0f, 255.0f);
	const int whole = std::min((int)steps, 255);
	ext.vertices[i] = (uint8_t)whole;
	ext.heightFraction[i] = (uint8_t)std::clamp((int)((steps - whole) * 256.0f), 0, 255);
}

// ---------------------------------------------------------------------------------------------
// The map's materials and its transition tiles

enum Kind { K_GRASS, K_ROCK, K_SNOW, K_SAND, K_FOREST, K_DIRT, K_OTHER };
Kind KindOf(const std::string& name)
{
	std::string n = name;
	for (char& c : n)
		c = (char)toupper((unsigned char)c);
	auto has = [&](const char* s) { return n.find(s) != std::string::npos; };
	if (has("SNOW") || has("ICE")) return K_SNOW;
	if (has("ROCK") && !has("GRASS")) return K_ROCK;
	if (has("MOUNTAIN") || has("CLIFF") || has("SCREE")) return K_ROCK;
	if (has("SAND") || has("BEACH") || has("SHALLOW")) return K_SAND;
	if (has("FOREST")) return K_FOREST;
	if (has("EARTH") || has("DIRT") || has("MUD") || has("FIELD")) return K_DIRT;
	if (has("GRASS") || has("HILL") || has("SHRUB")) return K_GRASS;
	return K_OTHER;
}

// A tile's corners in the order the renderer gives its vertices: 0 (x, z), 1 (x + 1, z),
// 2 (x + 1, z + 1), 3 (x, z + 1). localCorner[i]: which corner of the texture's own picture lies
// at corner i, for the tile's turn and flips (as TileCornerUVs in EnhancedTerrainData.cpp:
// picture corners 0 (u0, v0), 1 (u1, v0), 2 (u1, v1), 3 (u0, v1))
std::array<int, 4> LocalCorners(int rotBits, bool xf, bool zf)
{
	std::array<int, 4> c = { 0, 1, 2, 3 };
	const bool xflip = xf != (bool)(rotBits & 2);
	const bool zflip = zf != (bool)(rotBits & 2);
	if (!xflip) {
		std::swap(c[0], c[3]);
		std::swap(c[1], c[2]);
	}
	if (zflip) {
		std::swap(c[0], c[1]);
		std::swap(c[2], c[3]);
	}
	if (rotBits & 1) {
		const int t = c[0];
		c[0] = c[1];
		c[1] = c[2];
		c[2] = c[3];
		c[3] = t;
	}
	return c;
}

struct Oriented { TerrainTexture* tex; uint8_t rot, xf, zf; };

struct Materials {
	std::vector<TerrainTextureGroup*> groups; // the materials (groups without a '-')
	std::vector<Kind> kinds;
	std::map<const TerrainTextureGroup*, int> index;
	// per transition texture: its picture's corners' materials (indices)
	std::map<const TerrainTexture*, std::array<int, 4>> transitionCorners;
	// per pair of materials (a < b) and pattern (bit i: corner i is b): the tiles that fit
	std::map<std::pair<int, int>, std::array<std::vector<Oriented>, 16>> fits;
	std::vector<std::array<float, 3>> colour; // each material's mean colour (0-255)
	bool hasPair(int a, int b) const { return fits.count({ std::min(a, b), std::max(a, b) }) != 0; }
	int find(Kind k, const std::vector<int>& counts) const
	{
		int best = -1;
		for (size_t i = 0; i < groups.size(); i++)
			if (kinds[i] == k && (best < 0 || counts[i] > counts[best]))
				best = (int)i;
		return best;
	}
};

struct Rgb { double r = 0, g = 0, b = 0, n = 0; };

Materials AnalyseMaterials(const Terrain& map)
{
	Materials m;
	const TerrainTextureDatabase& db = map.textures();
	for (const auto& [name, grp] : db.groups)
		if (name.find('-') == std::string::npos && !grp->textures.empty()) {
			m.index[grp.get()] = (int)m.groups.size();
			m.groups.push_back(grp.get());
			m.kinds.push_back(KindOf(name));
		}
	// the pictures' pixels (the atlases, at whatever size they are)
	std::map<std::string, Bitmap> atlases;
	auto atlas = [&](const std::string& file) -> const Bitmap& {
		auto it = atlases.find(file);
		if (it == atlases.end())
			it = atlases.emplace(file, Bitmap::loadBitmap(("Maps\\Map_Textures\\" + file).c_str()).convertToR8G8B8A8()).first;
		return it->second;
	};
	// a picture's mean colour over a part of it (fractions of the piece, u0..u1 x v0..v1)
	auto mean = [&](const TerrainTexture* t, float u0, float v0, float u1, float v1) {
		Rgb c;
		const Bitmap& a = atlas(t->file);
		if (a.width <= 0 || a.height <= 0)
			return c;
		const float sx = a.width / 256.0f, sy = a.height / 256.0f;
		const int x0 = (int)((t->startx + u0 * t->width) * sx), x1 = std::max(x0 + 1, (int)((t->startx + u1 * t->width) * sx));
		const int y0 = (int)((t->starty + v0 * t->height) * sy), y1 = std::max(y0 + 1, (int)((t->starty + v1 * t->height) * sy));
		for (int y = std::max(y0, 0); y < std::min(y1, a.height); y++)
			for (int x = std::max(x0, 0); x < std::min(x1, a.width); x++) {
				const uint8_t* p = &a.pixels[((size_t)y * a.width + x) * 4];
				c.r += p[0]; c.g += p[1]; c.b += p[2]; c.n += 1;
			}
		if (c.n > 0) { c.r /= c.n; c.g /= c.n; c.b /= c.n; }
		return c;
	};
	std::vector<Rgb> matColour(m.groups.size());
	std::vector<bool> matDone(m.groups.size(), false);
	auto materialColour = [&](int i) -> const Rgb& {
		if (!matDone[i]) {
			Rgb s;
			for (const auto& [id, t] : m.groups[i]->textures) {
				const Rgb c = mean(t.get(), 0, 0, 1, 1);
				s.r += c.r; s.g += c.g; s.b += c.b; s.n += 1;
			}
			if (s.n > 0) { s.r /= s.n; s.g /= s.n; s.b /= s.n; }
			matColour[i] = s;
			matDone[i] = true;
		}
		return matColour[i];
	};
	auto dist = [](const Rgb& a, const Rgb& b) { return (a.r - b.r) * (a.r - b.r) + (a.g - b.g) * (a.g - b.g) + (a.b - b.b) * (a.b - b.b); };
	// The transitions: "A-B" between two materials. Each corner of each picture is the material
	// its colour is closer to (a fifth of the piece at the corner, as the splat terrain did)
	for (const auto& [name, grp] : db.groups) {
		const size_t dash = name.find('-');
		if (dash == std::string::npos)
			continue;
		auto ga = db.groups.find(name.substr(0, dash)), gb = db.groups.find(name.substr(dash + 1));
		if (ga == db.groups.end() || gb == db.groups.end() || !m.index.count(ga->second.get()) || !m.index.count(gb->second.get()))
			continue;
		const int a = m.index[ga->second.get()], b = m.index[gb->second.get()];
		if (a == b)
			continue;
		const Rgb& ca = materialColour(a);
		const Rgb& cb = materialColour(b);
		if (dist(ca, cb) < 25.0)
			continue; // too alike to tell the corners apart
		const std::pair<int, int> key{ std::min(a, b), std::max(a, b) };
		for (const auto& [id, t] : grp->textures) {
			constexpr float K = 0.2f;
			const float box[4][4] = { { 0, 0, K, K }, { 1 - K, 0, 1, K }, { 1 - K, 1 - K, 1, 1 }, { 0, 1 - K, K, 1 } };
			std::array<int, 4> corners;
			int bs = 0;
			for (int k = 0; k < 4; k++) {
				const Rgb c = mean(t.get(), box[k][0], box[k][1], box[k][2], box[k][3]);
				corners[k] = dist(c, ca) <= dist(c, cb) ? a : b;
				bs += corners[k] == b;
			}
			if (bs == 0 || bs == 4)
				continue; // a picture all of one: no use as a transition
			m.transitionCorners[t.get()] = corners;
			auto& table = m.fits[key];
			for (int rot = 0; rot < 4; rot++)
				for (int xf = 0; xf < 2; xf++)
					for (int zf = 0; zf < 2; zf++) {
						const std::array<int, 4> lc = LocalCorners(rot, xf != 0, zf != 0);
						int bits = 0;
						for (int i = 0; i < 4; i++)
							if (corners[lc[i]] == key.second)
								bits |= 1 << i;
						table[bits].push_back({ t.get(), (uint8_t)rot, (uint8_t)xf, (uint8_t)zf });
					}
		}
	}
	m.colour.resize(m.groups.size());
	for (size_t i = 0; i < m.groups.size(); i++) {
		const Rgb& c = materialColour((int)i);
		m.colour[i] = { (float)c.r, (float)c.g, (float)c.b };
	}
	// a pair with none that fits is no pair
	for (auto it = m.fits.begin(); it != m.fits.end();) {
		bool any = false;
		for (int p = 1; p < 15; p++)
			any = any || !it->second[p].empty();
		it = any ? std::next(it) : m.fits.erase(it);
	}
	return m;
}

// A map tile's corners' materials (-1: unknown)
std::array<int, 4> TileCornerMaterials(const Materials& m, const Terrain::Tile* t)
{
	std::array<int, 4> out = { -1, -1, -1, -1 };
	if (!t || !t->texture)
		return out;
	auto mi = m.index.find(t->texture->grp);
	if (mi != m.index.end()) {
		out.fill(mi->second);
		return out;
	}
	auto tc = m.transitionCorners.find(t->texture);
	if (tc == m.transitionCorners.end())
		return out;
	const std::array<int, 4> lc = LocalCorners(t->rot, t->xflip, t->zflip);
	for (int i = 0; i < 4; i++)
		out[i] = tc->second[lc[i]];
	return out;
}

// ---------------------------------------------------------------------------------------------
// The smooth surface between fixed values: Laplace's equation, by Gauss-Seidel from coarse to
// fine (each level starts from the one below, so a wide grid settles in a few sweeps)
void SolveMembrane(std::vector<float>& v, const std::vector<uint8_t>& fixed, int W, int H)
{
	if (W <= 48 || H <= 48) {
		for (int it = 0; it < 400; it++)
			for (int z = 0; z < H; z++)
				for (int x = 0; x < W; x++) {
					const size_t i = (size_t)z * W + x;
					if (fixed[i])
						continue;
					float s = 0;
					int n = 0;
					if (x > 0) { s += v[i - 1]; n++; }
					if (x < W - 1) { s += v[i + 1]; n++; }
					if (z > 0) { s += v[i - W]; n++; }
					if (z < H - 1) { s += v[i + W]; n++; }
					v[i] = s / n;
				}
		return;
	}
	const int cw = (W + 1) / 2, ch = (H + 1) / 2;
	std::vector<float> cv((size_t)cw * ch, 0.0f);
	std::vector<uint8_t> cf((size_t)cw * ch, 0);
	for (int z = 0; z < ch; z++)
		for (int x = 0; x < cw; x++) {
			float s = 0, sa = 0;
			int nf = 0, na = 0;
			for (int dz = 0; dz < 2; dz++)
				for (int dx = 0; dx < 2; dx++) {
					const int fx = std::min(2 * x + dx, W - 1), fz = std::min(2 * z + dz, H - 1);
					const size_t i = (size_t)fz * W + fx;
					sa += v[i];
					na++;
					if (fixed[i]) { s += v[i]; nf++; }
				}
			cv[(size_t)z * cw + x] = nf ? s / nf : sa / na;
			cf[(size_t)z * cw + x] = nf > 0;
		}
	SolveMembrane(cv, cf, cw, ch);
	for (int z = 0; z < H; z++)
		for (int x = 0; x < W; x++) {
			const size_t i = (size_t)z * W + x;
			if (!fixed[i])
				v[i] = cv[(size_t)std::min(z / 2, ch - 1) * cw + std::min(x / 2, cw - 1)];
		}
	for (int it = 0; it < 24; it++)
		for (int z = 0; z < H; z++)
			for (int x = 0; x < W; x++) {
				const size_t i = (size_t)z * W + x;
				if (fixed[i])
					continue;
				float s = 0;
				int n = 0;
				if (x > 0) { s += v[i - 1]; n++; }
				if (x < W - 1) { s += v[i + 1]; n++; }
				if (z > 0) { s += v[i - W]; n++; }
				if (z < H - 1) { s += v[i + W]; n++; }
				v[i] = s / n;
			}
}

// the point of the rectangle [0, w] x [0, h]'s border nearest to (x, z)
void NearestOnBorder(float x, float z, float w, float h, float& bx, float& bz)
{
	bx = std::clamp(x, 0.0f, w);
	bz = std::clamp(z, 0.0f, h);
	if (bx > 0 && bx < w && bz > 0 && bz < h) {
		const float d[4] = { bx, w - bx, bz, h - bz };
		const int k = (int)(std::min_element(d, d + 4) - d);
		if (k == 0) bx = 0; else if (k == 1) bx = w; else if (k == 2) bz = 0; else bz = h;
	}
}

} // namespace

int ExtendMapTiles(int setting)
{
	return setting >= 1 ? NEAR_TILES : 0;
}

int ExtendMapFarRings(int setting)
{
	return setting >= 3 ? 7 : setting == 2 ? 4 : 0;
}

void ExtendedTerrainDeleter::operator()(Terrain* t) const
{
	if (!t)
		return;
	if (t->tiles)
		delete[] t->tiles;
	t->tiles = nullptr;
	delete t;
}

ExtendedTerrain BuildExtendedTerrain(const Terrain& map, int n, std::vector<ExtendedTree>* trees, int farRings, FarTerrain* far, int detail)
{
	const int w = map.width, h = map.height, W = w + 2 * n, H = h + 2 * n;
	ExtendedTerrain ext(new Terrain());
	Terrain& e = *ext;
	e.tiles = nullptr;
	e.width = W;
	e.height = H;
	e.edge = map.edge + n;
	e.scale = map.scale;
	e.fogColor = map.fogColor;
	e.sunColor = map.sunColor;
	e.sunVector = map.sunVector;
	e.texDbPath = map.texDbPath;
	e.skyTextureDirectory = map.skyTextureDirectory;
	e.filePath = map.filePath;
	e.mapFile = map.mapFile;
	e.sharedTexDb = &map.textures();
	e.extendedTiles = n;
	e.waterFrozen = map.waterFrozen;
	for (Vector3 lake : map.lakes) {
		lake.x += 5.0f * n;
		lake.z += 5.0f * n;
		e.lakes.push_back(lake);
	}
	e.waterDesign = ShiftedDesign(map.waterDesign, W, H, n);
	const uint64_t seed = MapSeed(map);
	const Materials mats = AnalyseMaterials(map);
	const int M = (int)mats.groups.size();

	const int VW = w + 1, VH = h + 1, EW = W + 1, EH = H + 1;
	auto mapV = [&](int x, int z) { return map.getVertex(std::clamp(x, 0, w), std::clamp(z, 0, h)); };
	auto inMapV = [&](int X, int Z) { return X >= n && Z >= n && X <= n + w && Z <= n + h; };
	auto inMapT = [&](int X, int Z) { return X >= n && Z >= n && X < n + w && Z < n + h; };

	// ---- Analysis of the map's edge
	float lo = 1e30f, hi = -1e30f;
	for (int z = 0; z < VH; z++)
		for (int x = 0; x < VW; x++) {
			lo = std::min(lo, mapV(x, z));
			hi = std::max(hi, mapV(x, z));
		}
	const float relief = std::max(hi - lo, 4.0f);
	// per map vertex, fractions of its tiles around (blurred later): rocky or snowy, wet
	std::vector<int> matCount(M, 0);
	std::vector<float> rocky((size_t)VW * VH, 0), wetV((size_t)VW * VH, 0);
	std::vector<int> seamMat((size_t)VW * VH, -1); // the outer tiles' corner materials, on the border
	float snowLine = 1e30f, bedSum = 0, levelSum = 0;
	int bedN = 0;
	std::vector<float> snowHeights;
	std::vector<int> waterMatCount(M, 0);
	for (int z = 0; z < h; z++)
		for (int x = 0; x < w; x++) {
			const Terrain::Tile* t = map.getTile(x, z);
			const std::array<int, 4> cm = TileCornerMaterials(mats, t);
			const int cx[4] = { x, x + 1, x + 1, x }, cz[4] = { z, z, z + 1, z + 1 };
			for (int k = 0; k < 4; k++) {
				if (cm[k] < 0)
					continue;
				matCount[cm[k]]++;
				const size_t vi = (size_t)cz[k] * VW + cx[k];
				const Kind kd = mats.kinds[cm[k]];
				if (kd == K_ROCK || kd == K_SNOW)
					rocky[vi] += 0.25f;
				if (kd == K_SNOW)
					snowHeights.push_back(mapV(cx[k], cz[k]));
				if (t->fullOfWater)
					waterMatCount[cm[k]]++;
				const bool border = cx[k] == 0 || cz[k] == 0 || cx[k] == w || cz[k] == h;
				if (border && seamMat[vi] < 0)
					seamMat[vi] = cm[k];
			}
			if (t->fullOfWater) {
				for (int k = 0; k < 4; k++)
					wetV[(size_t)cz[k] * VW + cx[k]] += 0.25f;
				if (x < 4 || z < 4 || x >= w - 4 || z >= h - 4) {
					bedSum += map.getVertex(x, z);
					levelSum += t->waterLevel;
					bedN++;
				}
			}
		}
	if (!snowHeights.empty()) {
		std::sort(snowHeights.begin(), snowHeights.end());
		snowLine = snowHeights[snowHeights.size() / 5];
	}
	// border vertices with no known material: their neighbour's along the border
	for (int pass = 0; pass < 4; pass++)
		for (int z = 0; z < VH; z++)
			for (int x = 0; x < VW; x++) {
				const size_t i = (size_t)z * VW + x;
				if (seamMat[i] >= 0 || !(x == 0 || z == 0 || x == w || z == h))
					continue;
				for (const auto& [dx, dz] : { std::pair{ 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } }) {
					const int ax = x + dx, az = z + dz;
					if (ax >= 0 && az >= 0 && ax < VW && az < VH && seamMat[(size_t)az * VW + ax] >= 0) {
						seamMat[i] = seamMat[(size_t)az * VW + ax];
						break;
					}
				}
			}
	const int matGrass = std::max(mats.find(K_GRASS, matCount), 0);
	const int matRock = mats.find(K_ROCK, matCount), matSnow = mats.find(K_SNOW, matCount);
	const int matSand = mats.find(K_SAND, matCount);
	// the map's own ground materials, most used first (at least 1% of its corners)
	std::vector<int> landMats;
	{
		int total = 0;
		for (int c : matCount)
			total += c;
		for (int i = 0; i < M; i++)
			if ((mats.kinds[i] == K_GRASS || mats.kinds[i] == K_DIRT || mats.kinds[i] == K_FOREST) && matCount[i] * 200 >= total)
				landMats.push_back(i);
		std::sort(landMats.begin(), landMats.end(), [&](int a, int b) { return matCount[a] > matCount[b]; });
		// the main ground first
		if (!landMats.empty() && landMats[0] != matGrass) {
			landMats.erase(std::remove(landMats.begin(), landMats.end(), matGrass), landMats.end());
			landMats.insert(landMats.begin(), matGrass);
		}
	}
	// Where the map puts its rock (its rock and snow corners): by height and by slope. Over its dry
	// vertices, the lowest height above which much of the land is rock (the rock line: its high
	// hills brown, the play test of 2026-11-10 found the extension's green to the top), and the
	// lowest slope above which much is. Without a clear one, the extension's own defaults.
	float rockLine = 1e30f, rockSlope = 0.34f;
	{
		struct S { float h, s, r; };
		std::vector<S> pts;
		for (int z = 0; z < VH; z++)
			for (int x = 0; x < VW; x++) {
				const size_t i = (size_t)z * VW + x;
				if (wetV[i] > 0)
					continue;
				const float v = mapV(x, z);
				const float s = std::max({ std::abs(mapV(x + 1, z) - v), std::abs(mapV(x - 1, z) - v), std::abs(mapV(x, z + 1) - v), std::abs(mapV(x, z - 1) - v) }) / 5.0f;
				pts.push_back({ v, s, std::min(rocky[i], 1.0f) });
			}
		// the lowest threshold with at least 40% rock above it, over at least 1% of the land
		auto threshold = [&](auto key, float fallback) {
			std::sort(pts.begin(), pts.end(), [&](const S& a, const S& b) { return key(a) > key(b); });
			double sum = 0;
			float best = fallback;
			for (size_t k = 0; k < pts.size(); k++) {
				sum += pts[k].r;
				if ((k + 1) * 100 >= pts.size() && sum >= 0.4 * (k + 1))
					best = key(pts[k]);
			}
			return best;
		};
		double total = 0;
		for (const S& p : pts)
			total += p.r;
		if (!pts.empty() && total >= 0.01 * pts.size()) {
			rockLine = threshold([](const S& p) { return p.h; }, 1e30f);
			rockSlope = std::clamp(threshold([](const S& p) { return p.s; }, rockSlope), 0.12f, 0.45f);
		}
		printf("Extend map: rock above %.1f (heights %.1f to %.1f), or steeper than %.2f; snow above %.1f; %.0f%% of the dry land rocky\n",
			rockLine < 1e29f ? rockLine : -1.0f, lo, hi, rockSlope, snowLine < 1e29f ? snowLine : -1.0f, pts.empty() ? 0.0 : 100.0 * total / pts.size());
	}
	// a place's rock (0 to 1): steeper than the map's rock slope, or above its rock line
	auto rockAt = [&](float v, float rise, float jitter) {
		const float bySlope = std::clamp((rise - rockSlope - 0.1f * jitter) / 0.3f, 0.0f, 1.0f);
		// (high ground only where it slopes too: the high flats along a river had turned white
		// with the transition towards rock, 4 Peaks in the play test of 2026-11-10)
		const float sloped = std::clamp((rise - 0.25f * rockSlope) / (0.35f * rockSlope), 0.0f, 1.0f);
		const float byHeight = rockLine < 1e29f ? std::clamp((v - rockLine - 0.06f * relief * jitter) / (0.05f * relief + 1.0f), 0.0f, 1.0f) * sloped : 0.0f;
		return std::max(bySlope, byHeight);
	};
	int matBed = -1;
	// the ground under the map's water (its sand, its river bed), never its grass
	for (int i = 0; i < M; i++)
		if (waterMatCount[i] > 0 && mats.kinds[i] != K_GRASS && mats.kinds[i] != K_FOREST && (matBed < 0 || waterMatCount[i] > waterMatCount[matBed]))
			matBed = i;
	if (matBed < 0)
		matBed = mats.find(K_SAND, matCount);
	const float seaBed = bedN ? bedSum / bedN : lo;
	const float seaLevel = bedN ? levelSum / bedN : -1e30f;

	// blurred fields over the map's vertices, read at the border
	auto blur2D = [&](const std::vector<float>& in, int R) {
		std::vector<float> tmp(in.size()), out(in.size());
		for (int z = 0; z < VH; z++)
			for (int x = 0; x < VW; x++) {
				float s = 0;
				for (int k = -R; k <= R; k++)
					s += in[(size_t)z * VW + std::clamp(x + k, 0, VW - 1)];
				tmp[(size_t)z * VW + x] = s / (2 * R + 1);
			}
		for (int z = 0; z < VH; z++)
			for (int x = 0; x < VW; x++) {
				float s = 0;
				for (int k = -R; k <= R; k++)
					s += tmp[(size_t)std::clamp(z + k, 0, VH - 1) * VW + x];
				out[(size_t)z * VW + x] = s / (2 * R + 1);
			}
		return out;
	};
	std::vector<float> heights0((size_t)VW * VH);
	for (int z = 0; z < VH; z++)
		for (int x = 0; x < VW; x++)
			heights0[(size_t)z * VW + x] = mapV(x, z);
	const std::vector<float> wide = blur2D(blur2D(heights0, 10), 10);
	const std::vector<float> near5 = blur2D(heights0, 3);
	std::vector<float> roughIn((size_t)VW * VH);
	for (size_t i = 0; i < roughIn.size(); i++)
		roughIn[i] = std::abs(heights0[i] - near5[i]);
	const std::vector<float> rough = blur2D(roughIn, 8);
	// The map's own small relief, by scale: how far its vertices stand from the mean of their
	// neighbours (about 1, 3 and 8 tiles), over its dry land. The extension gets the same, or its
	// ground looks smoothed out next to the map's.
	const std::vector<float> b1 = blur2D(heights0, 1), b8 = blur2D(heights0, 8);
	// (medians, not means of squares: the ordinary ground's, not its cliffs')
	std::vector<float> d1, d3, d8;
	for (int z = 0; z < VH; z++)
		for (int x = 0; x < VW; x++) {
			const size_t i = (size_t)z * VW + x;
			if (wetV[i] > 0)
				continue;
			d1.push_back(std::abs(heights0[i] - b1[i]));
			d3.push_back(std::abs(b1[i] - near5[i]));
			d8.push_back(std::abs(near5[i] - b8[i]));
		}
	auto median = [](std::vector<float>& v, float fallback) {
		if (v.empty())
			return fallback;
		std::nth_element(v.begin(), v.begin() + v.size() / 2, v.end());
		return v[v.size() / 2] * 1.48f; // as a standard deviation
	};
	const float sigma1 = median(d1, 0.3f), sigma3 = median(d3, 0.5f), sigma8 = median(d8, 1.0f);
	std::vector<float> fineIn((size_t)VW * VH);
	for (size_t i = 0; i < fineIn.size(); i++)
		fineIn[i] = std::abs(heights0[i] - b1[i]) + std::abs(b1[i] - near5[i]);
	// near the seam the edge's own roughness right there; farther out each side's broader one
	// (so a rough side stays rough and a smooth one smooth, blending where they meet)
	const std::vector<float> fineLocal = blur2D(fineIn, 6);
	const std::vector<float> fineSide = blur2D(blur2D(fineIn, 12), 12);
	const float fineGlobal = std::max(0.05f, (sigma1 + sigma3) * 0.8f);
	const std::vector<float> rockyB = blur2D(rocky, 8), wetB = blur2D(wetV, 6);
	auto at = [&](const std::vector<float>& f, float x, float z) {
		const int ix = std::clamp((int)std::lround(x), 0, w), iz = std::clamp((int)std::lround(z), 0, h);
		return f[(size_t)iz * VW + ix];
	};

	// The far field's height at a place (copy tile coordinates): the edge's wide mean height at its
	// nearest point on the map's border (a rim rising to the mesh edge keeps its own), the sea
	// floor off the sea, slow undulations; farther out than the near ring, broad rises and lows
	// of the land itself. The membrane ends on it, and the far rings stand on it.
	// The sea's share at a place from its border point's, with a noise where it goes over from
	// land to sea (the coasts had run straight along the map's edge, the play test of
	// 2026-11-10): bays and headlands; none on the open sea or inland
	auto coastal = [&](float wet, float px, float pz, float foot) {
		const float band = std::min(wet * 3.0f, 1.0f) * std::min((1.0f - wet) * 3.0f, 1.0f);
		return std::clamp(wet + 0.55f * band * Fbm(seed, px, pz, 26.0f, 3, 150, foot), 0.0f, 1.0f);
	};
	auto farBase = [&](float px, float pz, float foot = 0.0f) {
		const float sx = px - n, sz = pz - n;
		float bx, bz;
		NearestOnBorder(sx, sz, (float)w, (float)h, bx, bz);
		const float wet = coastal(std::clamp(at(wetB, bx, bz) * 1.6f, 0.0f, 1.0f), px, pz, foot);
		const float dx = std::max({ -sx, sx - w, 0.0f }), dz = std::max({ -sz, sz - h, 0.0f });
		const float d = std::sqrt(dx * dx + dz * dz);
		const float far = std::clamp((d - n) / 300.0f, 0.0f, 1.0f);
		const float land = std::max(at(wide, bx, bz), 0.85f * at(near5, bx, bz) + 0.15f * at(wide, bx, bz))
			+ 0.2f * relief * Fbm(seed, px, pz, 60.0f, 2, 40, foot) + far * 0.45f * relief * Fbm(seed, px, pz, 420.0f, 3, 41, foot);
		const float sea = seaBed - 0.12f * relief - far * 0.1f * relief;
		return land + (sea - land) * wet;
	};
	// The land forms at a place: ranges where the map's edge (sampled through a warp that grows
	// with the distance) is rocky or rough, and here and there farther out; worn hills
	// elsewhere; the sea floor off the sea. Also the place's mountain and sea shares and the
	// border point it was sampled at.
	struct Land { float detail, mtn, wet, bx, bz, d; };
	auto landAt = [&](float px, float pz, float foot = 0.0f) {
		Land L;
		const float sx = px - n, sz = pz - n;
		const float dx = std::max({ -sx, sx - w, 0.0f }), dz = std::max({ -sz, sz - h, 0.0f });
		L.d = std::sqrt(dx * dx + dz * dz);
		const float warp = 6.0f + 0.7f * std::min(L.d, 200.0f);
		const float wx = sx + warp * Fbm(seed, px, pz, 22.0f, 3, 10, foot), wz = sz + warp * Fbm(seed, px, pz, 22.0f, 3, 11, foot);
		NearestOnBorder(wx, wz, (float)w, (float)h, L.bx, L.bz);
		const float rockiness = std::clamp(at(rockyB, L.bx, L.bz) * 1.5f, 0.0f, 1.0f);
		const float roughness = at(rough, L.bx, L.bz);
		L.wet = coastal(std::clamp(at(wetB, L.bx, L.bz) * 1.6f, 0.0f, 1.0f), px, pz, foot);
		// the edge's own character fades out far away, where the land goes its own way
		const float own = 1.0f - std::clamp((L.d - 2.0f * n) / 400.0f, 0.0f, 0.7f);
		float mtn = own * std::clamp(std::max(rockiness, roughness / (0.04f * relief + 0.5f) - 0.6f), 0.0f, 1.0f);
		mtn = std::max(mtn, std::clamp((Fbm(seed, px, pz, 90.0f + 0.15f * std::min(L.d, 1500.0f), 2, 50, foot) - 0.25f) * 3.0f, 0.0f, 1.0f)
			* std::clamp(L.d / 40.0f, 0.0f, 1.0f) * (1 - L.wet));
		L.mtn = mtn;
		// a broad warp first (the ranges curve), then the forms: ranges as bands of ridges with
		// valleys between them, worn hills
		const float qx = px + 22.0f * Fbm(seed, px, pz, 70.0f, 2, 20, foot) + 5.0f * Fbm(seed, px, pz, 18.0f, 2, 22, foot);
		const float qz = pz + 22.0f * Fbm(seed, px, pz, 70.0f, 2, 21, foot) + 5.0f * Fbm(seed, px, pz, 18.0f, 2, 23, foot);
		const float hills = ErodedFbm(seed, qx, qz, 46.0f, 5, 60, foot) * (0.09f * relief + 2.0f);
		const float band = std::clamp(0.5f + 1.6f * Fbm(seed, qx, qz, 85.0f, 2, 71, foot), 0.0f, 1.0f);
		const float ridges = (Ridged(seed, qx, qz, 62.0f, 5, 70, foot) * (0.35f + 0.65f * band) - 0.2f) * (0.65f * relief + 8.0f);
		const float seaFloor = ErodedFbm(seed, qx, qz, 40.0f, 4, 80, foot) * (0.03f * relief + 0.6f);
		const float detail = hills * (1 - mtn) + ridges * mtn;
		L.detail = detail * (1 - L.wet) + seaFloor * L.wet;
		return L;
	};

	// ---- The macro surface: a membrane from the map's own vertices out to the far boundary,
	// where it is the edge's wide mean (sea floor off the sea), with slow undulations
	std::vector<float> base((size_t)EW * EH, 0.0f);
	std::vector<uint8_t> fixed((size_t)EW * EH, 0);
	for (int Z = 0; Z < EH; Z++)
		for (int X = 0; X < EW; X++) {
			const size_t i = (size_t)Z * EW + X;
			if (inMapV(X, Z)) {
				base[i] = mapV(X - n, Z - n);
				fixed[i] = 1;
			}
			else if (X == 0 || Z == 0 || X == EW - 1 || Z == EH - 1) {
				base[i] = farBase((float)X, (float)Z);
				fixed[i] = 1;
			}
		}
	SolveMembrane(base, fixed, EW, EH);

	// ---- Land forms on top, fading in from the map's edge: ranges where the edge is rocky or
	// rough, worn hills elsewhere; positions warped so nothing lines up with the grid
	std::vector<float> height((size_t)EW * EH);
	std::vector<float> mountainAt((size_t)EW * EH, 0.0f);
	// the small relief per scale (calibrated below), and where the ground is dry
	std::vector<float> bump1((size_t)EW * EH, 0.0f), bump3((size_t)EW * EH, 0.0f), bump8((size_t)EW * EH, 0.0f);
	std::vector<uint8_t> dryAt((size_t)EW * EH, 0);
	for (int Z = 0; Z < EH; Z++)
		for (int X = 0; X < EW; X++) {
			const size_t i = (size_t)Z * EW + X;
			if (inMapV(X, Z)) {
				height[i] = base[i];
				continue;
			}
			const float px = (float)X, pz = (float)Z;
			const Land L = landAt(px, pz);
			const float d = L.d, wet = L.wet, bx = L.bx, bz = L.bz;
			mountainAt[i] = L.mtn;
			const float ramp = std::clamp(d / 14.0f, 0.0f, 1.0f);
			const float ramp2 = ramp * ramp * (3 - 2 * ramp);
			// the small relief, as rough as the map's own (its edge's near the seam), rougher and
			// smoother in patches; its finest scale comes in at once, so the seam shows no change
			const float patch = 0.6f + 0.6f * std::clamp(Fbm(seed, px, pz, 40.0f, 2, 76) + 0.5f, 0.0f, 1.0f);
			const float near = std::clamp(d / 20.0f, 0.0f, 1.0f);
			const float localScale = (at(fineLocal, bx, bz) * (1 - near) + at(fineSide, bx, bz) * near) * 0.8f / fineGlobal;
			const float fineRamp = std::clamp(d / 3.0f, 0.0f, 1.0f);
			const float amp = fineRamp * patch * std::clamp(localScale, 0.5f, 1.6f) * (1 - wet);
			bump1[i] = amp * sigma1 * ValueNoise(seed, px / 1.6f, pz / 1.6f, 77);
			bump3[i] = amp * sigma3 * ValueNoise(seed, px / 4.0f, pz / 4.0f, 78);
			bump8[i] = amp * sigma8 * Fbm(seed, px, pz, 11.0f, 2, 79);
			dryAt[i] = wet < 0.3f;
			height[i] = base[i] + ramp2 * L.detail + bump1[i] + bump3[i] + bump8[i];
		}
	// The small relief calibrated: measured on the map's edge band and on the extension alike
	// (medians of how far each vertex stands from its neighbours' mean at about 1, 3 and 8 tiles),
	// each scale's detail is scaled until the extension is as rough as the band beside it
	{
		auto boxE = [&](const std::vector<float>& in, int R) {
			std::vector<float> tmp(in.size()), out(in.size());
			for (int Z = 0; Z < EH; Z++)
				for (int X = 0; X < EW; X++) {
					float s = 0;
					for (int k = -R; k <= R; k++)
						s += in[(size_t)Z * EW + std::clamp(X + k, 0, EW - 1)];
					tmp[(size_t)Z * EW + X] = s / (2 * R + 1);
				}
			for (int Z = 0; Z < EH; Z++)
				for (int X = 0; X < EW; X++) {
					float s = 0;
					for (int k = -R; k <= R; k++)
						s += tmp[(size_t)std::clamp(Z + k, 0, EH - 1) * EW + X];
					out[(size_t)Z * EW + X] = s / (2 * R + 1);
				}
			return out;
		};
		auto isBand = [&](int X, int Z) {
			const int sx = X - n, sz = Z - n;
			return inMapV(X, Z) && (sx < map.edge || sz < map.edge || sx > w - map.edge || sz > h - map.edge)
				&& wetV[(size_t)sz * VW + sx] == 0;
		};
		auto isExt = [&](int X, int Z) {
			return !inMapV(X, Z) && X >= 8 && Z >= 8 && X <= W - 8 && Z <= H - 8 && dryAt[(size_t)Z * EW + X];
		};
		auto measure = [&](bool band, float out[3]) {
			const std::vector<float> c1 = boxE(height, 1), c3 = boxE(height, 3), c8 = boxE(height, 8);
			std::vector<float> v[3];
			for (int Z = 0; Z < EH; Z++)
				for (int X = 0; X < EW; X++) {
					if (band ? !isBand(X, Z) : !isExt(X, Z))
						continue;
					const size_t i = (size_t)Z * EW + X;
					v[0].push_back(std::abs(height[i] - c1[i]));
					v[1].push_back(std::abs(c1[i] - c3[i]));
					v[2].push_back(std::abs(c3[i] - c8[i]));
				}
			for (int k = 0; k < 3; k++) {
				if (v[k].empty()) { out[k] = 0; continue; }
				std::nth_element(v[k].begin(), v[k].begin() + v[k].size() / 2, v[k].end());
				out[k] = v[k][v[k].size() / 2] * 1.48f;
			}
		};
		float target[3], got[3];
		measure(true, target);
		for (int iter = 0; iter < 3; iter++) {
			measure(false, got);
			float r[3];
			for (int k = 0; k < 3; k++)
				r[k] = got[k] > 1e-3f ? std::clamp(target[k] / got[k], 0.5f, 3.0f) : 1.0f;
			// the coarser scales also carry part of the finer ones' measure: correct the finest first
			std::vector<float>* parts[3] = { &bump1, &bump3, &bump8 };
			for (int k = 0; k < 3; k++)
				for (size_t i = 0; i < height.size(); i++) {
					const float add = (r[k] - 1.0f) * (*parts[k])[i];
					height[i] += add;
					(*parts[k])[i] += add;
				}
		}
		measure(false, got);
		printf("Extend map: small relief, band %.2f / %.2f / %.2f, extension %.2f / %.2f / %.2f (1, 3, 8 tiles)\n",
			target[0], target[1], target[2], got[0], got[1], got[2]);
	}

	// ---- Tiles: the map's own in the middle (water and textures chosen below for the rest)
	e.tiles = new Terrain::Tile[(size_t)W * H];
	for (int Z = 0; Z < H; Z++)
		for (int X = 0; X < W; X++) {
			Terrain::Tile& t = e.tiles[(size_t)Z * W + X];
			if (inMapT(X, Z))
				t = *map.getTile(X - n, Z - n);
			else {
				t.texture = mats.groups.empty() ? map.getTile(0, 0)->texture : mats.groups[matGrass]->textures.begin()->second.get();
				t.rot = t.xflip = t.zflip = 0;
				t.waterLevel = -1.0f;
				t.fullOfWater = false;
			}
			t.x = X;
			t.z = Z;
		}
	auto H_ = [&](int X, int Z) -> float& { return height[(size_t)std::clamp(Z, 0, H) * EW + std::clamp(X, 0, W)]; };

	// ---- Rivers: each narrow water at the map's mesh edge carries on outwards, meandering,
	// carving its bed and stepping down with the land (cascades where it drops)
	std::vector<float> riverLevel((size_t)W * H, -1e30f);
	struct RiverPt { float x, z, level, half; };
	std::vector<RiverPt> riverPts; // every river's course, also beyond the copy (the far rings')
	std::vector<float> riverU((size_t)EW * EH, 1e30f), riverL((size_t)EW * EH, 0.0f), riverHalf((size_t)EW * EH, 1.0f);
	struct Exit { int side; float along; int width; float level; };
	std::vector<Exit> exits;
	for (int side = 0; side < 4; side++) {
		const int len = side < 2 ? w : h;
		int run = 0;
		for (int k = 0; k <= len; k++) {
			const Terrain::Tile* t = nullptr;
			if (k < len)
				t = side == 0 ? map.getTile(k, 0) : side == 1 ? map.getTile(k, h - 1) : side == 2 ? map.getTile(0, k) : map.getTile(w - 1, k);
			if (t && t->fullOfWater) {
				run++;
				continue;
			}
			if (run > 0 && run < std::min(30, len / 3)) {
				const int mid = k - run / 2 - 1;
				const Terrain::Tile* m = side == 0 ? map.getTile(mid, 0) : side == 1 ? map.getTile(mid, h - 1) : side == 2 ? map.getTile(0, mid) : map.getTile(w - 1, mid);
				exits.push_back({ side, k - run * 0.5f, run, m->waterLevel });
			}
			run = 0;
		}
	}
	for (size_t r = 0; r < exits.size(); r++) {
		const Exit& ex = exits[r];
		const float ox = ex.side == 2 ? -1.0f : ex.side == 3 ? 1.0f : 0.0f, oz = ex.side == 0 ? -1.0f : ex.side == 1 ? 1.0f : 0.0f;
		float px = ex.side < 2 ? n + ex.along : (ex.side == 2 ? (float)n : (float)(n + w));
		float pz = ex.side < 2 ? (ex.side == 0 ? (float)n : (float)(n + h)) : n + ex.along;
		const float half0 = std::clamp(ex.width * 0.5f, 1.5f, 7.0f);
		float level = ex.level;
		const float a0 = std::atan2(oz, ox);
		const float phase = Unit(HashAt(seed, (int)r, 0, 91)) * 6.2832f;
		// its valley: banks rising from the water at up to VALLEY_SLOPE units per tile, so the land
		// opens up around the river instead of standing over it as walls
		constexpr float VALLEY = 20.0f, VALLEY_SLOPE = 1.1f;
		// (with far rings it carries on through them: its points beyond the copy are kept for them)
		const int steps = farRings > 0 ? 3 * n + 700 : 3 * n;
		for (int s = 0; s < steps; s++) {
			// meanders: a slow swing and a faster one, and a wander, away from the straight line out
			// (wider and longer swings than before: the rivers ran nearly straight, the play test of
			// 2026-11-10)
			const float a = a0 + 0.85f * std::sin(s / 19.0f + phase) + 0.35f * std::sin(s / 7.3f + 2 * phase)
				+ 0.75f * Fbm(seed, (float)s, (float)r * 37.0f, 30.0f, 2, 90);
			px += std::cos(a);
			pz += std::sin(a);
			const bool inCopy = px >= 1 && pz >= 1 && px <= W - 1 && pz <= H - 1;
			if (!inCopy && farRings <= 0)
				break;
			// narrower farther from the map (a river seen from its lower course up is wider)
			const float half = std::max(1.5f, half0 * (1.0f - 0.4f * std::min(s / (3.0f * n), 1.0f)));
			// the level follows the land down (never up), so the river runs downhill
			const float ground = inCopy ? base[(size_t)std::clamp((int)pz, 0, H) * EW + std::clamp((int)px, 0, W)]
				: farBase(px, pz) + landAt(px, pz).detail;
			level = std::min(level, std::max(ground - 0.6f, seaLevel > -1e29f ? seaLevel : -1e30f));
			riverPts.push_back({ px, pz, level, half });
			if (!inCopy)
				continue;
			const int R = (int)std::ceil(half + VALLEY);
			// each vertex remembers the river point it is nearest to (beyond the channel's edge)
			for (int dz = -R; dz <= R; dz++)
				for (int dx = -R; dx <= R; dx++) {
					const int X = (int)std::lround(px) + dx, Z = (int)std::lround(pz) + dz;
					if (X < 0 || Z < 0 || X > W || Z > H)
						continue;
					const float dist = std::sqrt((X - px) * (X - px) + (Z - pz) * (Z - pz));
					const size_t vi = (size_t)Z * EW + X;
					if (dist - half < riverU[vi]) {
						riverU[vi] = dist - half;
						riverL[vi] = level;
						riverHalf[vi] = half;
					}
				}
		}
	}
	// The river's tiles: every tile the channel reaches at a corner, at its lowest level there.
	// The water covers them whole and the banks rise out of it, so the shore follows the banks
	// within the tiles (the tiles whose middle lay in the channel drew it in steps, with dry pits
	// beside it, the play test of 2026-11-10)
	for (int Z = 0; Z < H; Z++)
		for (int X = 0; X < W; X++) {
			if (inMapT(X, Z))
				continue;
			float rl = -1e30f;
			for (int c = 0; c < 4; c++) {
				const size_t vi = (size_t)(Z + (c >> 1)) * EW + X + (c & 1);
				if (riverU[vi] <= 0.6f)
					rl = rl < -1e29f ? riverL[vi] : std::min(rl, riverL[vi]);
			}
			riverLevel[(size_t)Z * W + X] = rl;
		}
	// the channels and their valleys, each vertex by its nearest river point: the bed under
	// the water, banks that hold it, and the valley's sides opening up from them
	constexpr float VALLEY_W = 20.0f, VALLEY_SLOPE_W = 1.1f;
	for (int Z = 0; Z <= H; Z++)
		for (int X = 0; X <= W; X++) {
			const size_t vi = (size_t)Z * EW + X;
			if (riverU[vi] > VALLEY_W || inMapV(X, Z))
				continue;
			const float u = riverU[vi], level = riverL[vi], half = riverHalf[vi];
			float& v = H_(X, Z);
			if (u <= 0)
				v = std::min(v, level - 2.2f - 1.6f * std::min(-u / half, 1.0f));
			else {
				v = std::min(v, level + 0.35f + VALLEY_SLOPE_W * u * (0.4f + 0.06f * u));
				if (u < 1.5f)
					v = std::max(v, level + 0.25f); // the bank itself holds the water
			}
		}

	// ---- Seas and lakes at the edge spread out over the land below their level (their shores
	// follow the land's shape); the rivers' channels take their own levels
	for (int Z = 0; Z < H; Z++)
		for (int X = 0; X < W; X++) {
			if (inMapT(X, Z) || riverLevel[(size_t)Z * W + X] < -1e29f)
				continue;
			Terrain::Tile& t = e.tiles[(size_t)Z * W + X];
			t.fullOfWater = true;
			t.waterLevel = riverLevel[(size_t)Z * W + X];
		}
	{
		std::deque<std::pair<int, int>> queue;
		for (int Z = 0; Z < H; Z++)
			for (int X = 0; X < W; X++)
				if (inMapT(X, Z) && e.tiles[(size_t)Z * W + X].fullOfWater && (X == n || Z == n || X == n + w - 1 || Z == n + h - 1))
					queue.push_back({ X, Z });
		while (!queue.empty()) {
			const auto [x, z] = queue.front();
			queue.pop_front();
			const float lvl = e.tiles[(size_t)z * W + x].waterLevel;
			const int nb[4][6] = { { x + 1, z, x + 1, z, x + 1, z + 1 }, { x - 1, z, x, z, x, z + 1 },
				{ x, z + 1, x, z + 1, x + 1, z + 1 }, { x, z - 1, x, z, x + 1, z } };
			for (const auto& q : nb) {
				if (q[0] < 0 || q[1] < 0 || q[0] >= W || q[1] >= H || inMapT(q[0], q[1]))
					continue;
				Terrain::Tile& t = e.tiles[(size_t)q[1] * W + q[0]];
				if (t.fullOfWater && t.waterLevel >= lvl)
					continue;
				if (std::min(H_(q[2], q[3]), H_(q[4], q[5])) >= lvl)
					continue;
				t.fullOfWater = true;
				t.waterLevel = lvl;
				queue.push_back({ q[0], q[1] });
			}
		}
	}

	// ---- Lakes and ponds where the land holds water: a priority flood from the copy's outer
	// edge and the map (where water drains away) fills every hollow to its spill height; the
	// hollows big and deep enough become lakes, the small ones ponds, a little under their rim
	int lakes = 0, ponds = 0;
	{
		std::vector<float> filled((size_t)EW * EH, 0.0f);
		std::vector<uint8_t> done((size_t)EW * EH, 0);
		using QE = std::pair<float, uint32_t>;
		std::priority_queue<QE, std::vector<QE>, std::greater<QE>> pq;
		for (int Z = 0; Z <= H; Z++)
			for (int X = 0; X <= W; X++) {
				const size_t vi = (size_t)Z * EW + X;
				if (X == 0 || Z == 0 || X == W || Z == H || inMapV(X, Z)) {
					filled[vi] = H_(X, Z);
					done[vi] = 1;
					pq.push({ filled[vi], (uint32_t)vi });
				}
			}
		while (!pq.empty()) {
			const auto [f, vi] = pq.top();
			pq.pop();
			const int X = (int)(vi % EW), Z = (int)(vi / EW);
			const int nb[4][2] = { { X + 1, Z }, { X - 1, Z }, { X, Z + 1 }, { X, Z - 1 } };
			for (const auto& q : nb) {
				if (q[0] < 0 || q[1] < 0 || q[0] > W || q[1] > H)
					continue;
				const size_t ni = (size_t)q[1] * EW + q[0];
				if (done[ni])
					continue;
				done[ni] = 1;
				filled[ni] = std::max(H_(q[0], q[1]), f);
				pq.push({ filled[ni], (uint32_t)ni });
			}
		}
		// the hollow tiles, grouped: a tile is in a hollow if its lowest corner lies under the
		// filled surface there
		std::vector<int> label((size_t)W * H, -1);
		struct Basin { std::vector<std::pair<int, int>> tiles; float spill; bool lake; float rank; };
		std::vector<Basin> basins;
		for (int Z = 0; Z < H; Z++)
			for (int X = 0; X < W; X++) {
				if (inMapT(X, Z) || label[(size_t)Z * W + X] >= 0 || e.tiles[(size_t)Z * W + X].fullOfWater)
					continue;
				auto hollow = [&](int x, int z) {
					const float f = std::min({ filled[(size_t)z * EW + x], filled[(size_t)z * EW + x + 1], filled[(size_t)(z + 1) * EW + x], filled[(size_t)(z + 1) * EW + x + 1] });
					const float lowCorner = std::min({ H_(x, z), H_(x + 1, z), H_(x, z + 1), H_(x + 1, z + 1) });
					return f - lowCorner > 0.35f;
				};
				if (!hollow(X, Z))
					continue;
				std::vector<std::pair<int, int>> tilesOf{ { X, Z } };
				label[(size_t)Z * W + X] = 1;
				float spill = 1e30f, deepest = 0.0f;
				for (size_t k = 0; k < tilesOf.size(); k++) {
					const auto [x, z] = tilesOf[k];
					spill = std::min(spill, filled[(size_t)z * EW + x]);
					deepest = std::max(deepest, filled[(size_t)z * EW + x] - H_(x, z));
					const int nb[4][2] = { { x + 1, z }, { x - 1, z }, { x, z + 1 }, { x, z - 1 } };
					for (const auto& q : nb) {
						if (q[0] < 0 || q[1] < 0 || q[0] >= W || q[1] >= H || inMapT(q[0], q[1]))
							continue;
						int& l = label[(size_t)q[1] * W + q[0]];
						if (l >= 0 || e.tiles[(size_t)q[1] * W + q[0]].fullOfWater || !hollow(q[0], q[1]))
							continue;
						l = 1;
						tilesOf.push_back({ q[0], q[1] });
					}
				}
				// keep the real ones: a lake from 40 tiles, a pond from 6 (and only some of the
				// small ones, chosen by the place), never a puddle under a metre deep
				const size_t area = tilesOf.size();
				const bool pond = area >= 6 && area < 40 && deepest > 0.9f;
				const bool lake = area >= 40 && deepest > 1.5f;
				if (pond || lake)
					basins.push_back({ std::move(tilesOf), spill, lake, (float)area * (0.5f + Unit(HashAt(seed, X, Z, 95))) });
			}
		// not every hollow holds water: the biggest (with a random lean), a few per area of land
		std::sort(basins.begin(), basins.end(), [](const Basin& a, const Basin& b) { return a.rank > b.rank; });
		const int outside = W * H - w * h;
		const int maxLakes = std::max(2, outside / 9000), maxPonds = std::max(3, outside / 5000);
		for (const Basin& b : basins) {
			if (b.lake ? lakes >= maxLakes : ponds >= maxPonds)
				continue;
			const float level = b.spill - 0.25f;
			for (const auto& [x, z] : b.tiles) {
				if (std::min({ H_(x, z), H_(x + 1, z), H_(x, z + 1), H_(x + 1, z + 1) }) >= level)
					continue;
				Terrain::Tile& t = e.tiles[(size_t)z * W + x];
				t.fullOfWater = true;
				t.waterLevel = level;
			}
			(b.lake ? lakes : ponds)++;
		}
	}

	// ---- Materials, per vertex. Zones (the bed under water, sand along it, rock on steep
	// slopes, snow above the map's snow line) each reach their material through a chain of the
	// map's own transitions from the ground (grass -> snow -> mountain, say): the stronger the
	// zone at a vertex, the farther along its chain, so neighbours differ by one link at most and
	// a transition tile always joins them. Elsewhere the map's ground, with patches of its others.
	std::vector<std::vector<int>> pairsOf(M);
	for (const auto& [key, table] : mats.fits) {
		pairsOf[key.first].push_back(key.second);
		pairsOf[key.second].push_back(key.first);
	}
	// the shortest chain of materials from a to b (each step a transition pair), at most 4 long,
	// through no material of the kind `avoid` on the way
	auto chain = [&](int a, int b, int avoid = -1) {
		std::vector<int> path;
		if (a < 0 || b < 0)
			return path;
		if (a == b)
			return std::vector<int>{ a };
		std::vector<int> prev(M, -2);
		std::deque<int> q{ a };
		prev[a] = -1;
		while (!q.empty() && prev[b] == -2) {
			const int c = q.front();
			q.pop_front();
			for (int nb : pairsOf[c])
				if (prev[nb] == -2 && (nb == b || (int)mats.kinds[nb] != avoid)) {
					prev[nb] = c;
					q.push_back(nb);
				}
		}
		if (prev[b] == -2)
			return path;
		for (int c = b; c != -1; c = prev[c])
			path.push_back(c);
		std::reverse(path.begin(), path.end());
		if (path.size() > 4)
			path.clear();
		return path;
	};
	// a zone's material: the most used of its kind that a chain reaches from the ground
	// (rock not by way of snow: half-strength rock came out as snow along the shores, the
	// play test of 2026-11-10; else any)
	auto zoneChain = [&](Kind k, int preferred) {
		const int avoid = k == K_ROCK ? (int)K_SNOW : -1;
		std::vector<int> best = chain(matGrass, preferred, avoid);
		if (!best.empty())
			return best;
		for (int i = 0; i < M; i++)
			if (mats.kinds[i] == k && matCount[i] > 0) {
				std::vector<int> c = chain(matGrass, i, avoid);
				if (!c.empty() && (best.empty() || c.size() < best.size() || (c.size() == best.size() && matCount[i] > matCount[best.back()])))
					best = c;
			}
		return best;
	};
	const std::vector<int> bedChain = zoneChain(K_SAND, matBed), sandChain = zoneChain(K_SAND, matSand);
	const std::vector<int> rockChain = zoneChain(K_ROCK, matRock), snowChain = zoneChain(K_SNOW, matSnow);
	printf("Extend map: zone chains from the ground: bed %zu, sand %zu, rock %zu, snow %zu materials\n", bedChain.size(), sandChain.size(), rockChain.size(), snowChain.size());
	for (const auto* zc : { &bedChain, &sandChain, &rockChain, &snowChain }) {
		std::string names;
		for (int m : *zc)
			names += " " + mats.groups[m]->name;
		printf("Extend map:  chain%s\n", names.c_str());
	}
	std::vector<int> mat((size_t)EW * EH, matGrass);
	std::vector<uint8_t> matFixed((size_t)EW * EH, 0);
	// the near ring's rock by its mountain share (tenths): the far rings' grids are too coarse
	// for their slopes, they take the share of rock the near ring's ground of their kind has
	std::array<double, 11> rockBinSum{}, rockBinN{};
	// the forest floor, if the map has one that joins its ground
	int matForestZone = mats.find(K_FOREST, matCount);
	if (matForestZone >= 0 && (matCount[matForestZone] == 0 || !mats.hasPair(matForestZone, matGrass)))
		matForestZone = -1;
	for (int Z = 0; Z < EH; Z++)
		for (int X = 0; X < EW; X++) {
			const size_t i = (size_t)Z * EW + X;
			const int sx = X - n, sz = Z - n;
			if (inMapV(X, Z)) {
				if (sx == 0 || sz == 0 || sx == w || sz == h) {
					const int sm = seamMat[(size_t)sz * VW + sx];
					mat[i] = sm >= 0 ? sm : matGrass;
					matFixed[i] = 1;
				}
				else
					matFixed[i] = 2; // inside the map: not drawn by the extension
				continue;
			}
			if (M == 0)
				continue;
			const float px = (float)X, pz = (float)Z;
			const float v = H_(X, Z);
			const float rise = std::max({ std::abs(H_(X + 1, Z) - v), std::abs(H_(X - 1, Z) - v), std::abs(H_(X, Z + 1) - v), std::abs(H_(X, Z - 1) - v) }) / 5.0f;
			bool wetHere = false, nearWater = false;
			float level = -1e30f;
			for (int tz = Z - 3; tz < Z + 3; tz++)
				for (int tx = X - 3; tx < X + 3; tx++) {
					const Terrain::Tile* t = e.getTile(tx, tz);
					if (!t || !t->fullOfWater)
						continue;
					nearWater = true;
					level = std::max(level, t->waterLevel);
					if (tx >= X - 1 && tx <= X && tz >= Z - 1 && tz <= Z)
						wetHere = true;
				}
			const float jitter = Fbm(seed, px, pz, 9.0f, 3, 33);
			// each zone's strength, 0 to 1
			const float sBed = wetHere ? std::clamp((level - v) / 1.5f, 0.0f, 1.0f) : 0.0f;
			const float sSand = nearWater && !wetHere ? std::clamp(1.0f - (v - level) / (1.8f + jitter), 0.0f, 1.0f) * std::clamp(1.0f - (rise - 0.3f) / 0.2f, 0.0f, 1.0f) : 0.0f;
			const float sSnow = snowLine < 1e29f ? std::clamp((v - snowLine - 0.06f * relief * jitter) / (0.05f * relief + 1.0f), 0.0f, 1.0f) : 0.0f;
			const float sRock = rockAt(v, rise, jitter) * (1 - sSnow);
			if (!wetHere && !nearWater) {
				const int b = std::clamp((int)std::lround(mountainAt[i] * 10.0f), 0, 10);
				rockBinSum[b] += std::max(sRock, sSnow);
				rockBinN[b] += 1;
			}
			struct Zone { float s; const std::vector<int>* c; };
			const Zone zones[4] = { { sBed, &bedChain }, { sSand, &sandChain }, { sRock, &rockChain }, { sSnow, &snowChain } };
			int m = matGrass;
			float strongest = 0.0f;
			for (const Zone& z : zones)
				if (z.s > strongest && z.c->size() > 1) {
					strongest = z.s;
					const size_t k = std::min(z.c->size() - 1, (size_t)(z.s * z.c->size()));
					m = (*z.c)[k];
				}
			// forests where the map grows them (its forest floor), in big patches on gentle ground
			if (strongest <= 0.0f && matForestZone >= 0 && rise < 0.45f) {
				const float f = Fbm(seed, px + 4.0f * jitter, pz, 38.0f, 4, 140);
				if (f > 0.18f) {
					m = matForestZone;
					strongest = 1.0f;
				}
			}
			if (strongest <= 0.0f && landMats.size() > 1) {
				// patches of the map's other ground (meadows, earth, scrub), where they join it
				// each of the map's other grounds in its own patches (its own noise), the first that
				// joins the ground wins
				for (size_t k = 1; k < landMats.size() && k < 6; k++) {
					const float p = Fbm(seed, px + 3.0f * jitter, pz, 22.0f + 6.0f * k, 4, 34 + (int)k * 7);
					const float need = 0.22f + 0.05f * k; // the map's commoner grounds in more patches
					if (p > need && mats.hasPair(landMats[k], matGrass)) {
						m = landMats[k];
						break;
					}
				}
			}
			mat[i] = m;
		}
	// A majority filter over the extension's materials (twice): a vertex takes the material most
	// of its 3 x 3 neighbourhood has, where at least 5 of the 9 have it. Zones near their
	// thresholds had flipped from one vertex to the next, a checkered pattern on the hills (the
	// play test of 2026-11-10).
	for (int pass = 0; pass < 2; pass++) {
		std::vector<int> out = mat;
		for (int Z = 1; Z + 1 < EH; Z++)
			for (int X = 1; X + 1 < EW; X++) {
				const size_t i = (size_t)Z * EW + X;
				if (matFixed[i])
					continue;
				int cand[9], cnt[9], m = 0;
				for (int dz = -1; dz <= 1; dz++)
					for (int dx = -1; dx <= 1; dx++) {
						const int v = mat[(size_t)(Z + dz) * EW + X + dx];
						int k = 0;
						while (k < m && cand[k] != v)
							k++;
						if (k == m) { cand[m] = v; cnt[m] = 0; m++; }
						cnt[k]++;
					}
				for (int k = 0; k < m; k++)
					if (cnt[k] >= 5 && cand[k] != mat[i])
						out[i] = cand[k];
			}
		mat.swap(out);
	}
	// Every tile must have a picture: one material, or two that a transition of the map's joins
	// in that pattern. A tile that has none goes back to the ground (or to its seam corner's
	// material, by the map's edge), and its neighbours are looked at again.
	auto tileOk = [&](int X, int Z) {
		const int c[4] = { mat[(size_t)Z * EW + X], mat[(size_t)Z * EW + X + 1], mat[(size_t)(Z + 1) * EW + X + 1], mat[(size_t)(Z + 1) * EW + X] };
		int a = c[0], b = -1;
		for (int k = 1; k < 4; k++)
			if (c[k] != a) {
				if (b >= 0 && c[k] != b)
					return false;
				b = c[k];
			}
		if (b < 0)
			return true;
		const int lo2 = std::min(a, b), hi2 = std::max(a, b);
		auto it = mats.fits.find({ lo2, hi2 });
		if (it == mats.fits.end())
			return false;
		int bits = 0;
		for (int k = 0; k < 4; k++)
			if (c[k] == hi2)
				bits |= 1 << k;
		return !it->second[bits].empty();
	};
	for (int pass = 0; pass < 16; pass++) {
		bool changed = false;
		for (int Z = 0; Z < H; Z++)
			for (int X = 0; X < W; X++) {
				if (inMapT(X, Z) || tileOk(X, Z))
					continue;
				const size_t ci[4] = { (size_t)Z * EW + X, (size_t)Z * EW + X + 1, (size_t)(Z + 1) * EW + X + 1, (size_t)(Z + 1) * EW + X };
				// keep the material that most of the corners can join (a seam corner's first; on a
				// tie the zone's rather than the ground, so peaks keep their snow), and bring the
				// corners that can't join it to it
				int keep = -1;
				for (size_t c : ci)
					if (matFixed[c] == 1) { keep = mat[c]; break; }
				if (keep < 0) {
					int bestScore = -1;
					for (size_t c : ci) {
						const int cand = mat[c];
						int score = 0;
						for (size_t o : ci)
							score += (mat[o] == cand || mats.hasPair(mat[o], cand)) ? 2 : 0;
						score += cand != matGrass;
						if (pass >= 10)
							score = cand == matGrass ? 100 : 0; // at last, the ground
						if (score > bestScore) { bestScore = score; keep = cand; }
					}
				}
				// the other material the tile may keep: the first corner that joins keep
				int other = -1;
				for (size_t c : ci)
					if (mat[c] != keep && mats.hasPair(mat[c], keep)) { other = mat[c]; break; }
				for (size_t c : ci)
					if (!matFixed[c] && mat[c] != keep && (mat[c] != other || pass >= 6)) {
						mat[c] = keep;
						changed = true;
					}
			}
		if (!changed)
			break;
	}
	int unmatched = 0;
	for (int Z = 0; Z < H; Z++)
		for (int X = 0; X < W; X++) {
			if (inMapT(X, Z))
				continue;
			Terrain::Tile& t = e.tiles[(size_t)Z * W + X];
			const int c[4] = { mat[(size_t)Z * EW + X], mat[(size_t)Z * EW + X + 1], mat[(size_t)(Z + 1) * EW + X + 1], mat[(size_t)(Z + 1) * EW + X] };
			const uint64_t r = HashAt(seed, X, Z, 1);
			int a = c[0], b = -1;
			for (int k = 1; k < 4; k++)
				if (c[k] != a)
					b = c[k];
			bool done = false;
			if (b >= 0) {
				const int lo2 = std::min(a, b), hi2 = std::max(a, b);
				auto it = mats.fits.find({ lo2, hi2 });
				if (it != mats.fits.end()) {
					int bits = 0;
					for (int k = 0; k < 4; k++)
						if (c[k] == hi2)
							bits |= 1 << k;
					const std::vector<Oriented>& options = it->second[bits];
					bool pure = true;
					for (int k = 0; k < 4; k++)
						pure = pure && (c[k] == lo2 || c[k] == hi2);
					if (!options.empty() && pure) {
						const Oriented& o = options[r % options.size()];
						t.texture = o.tex;
						t.rot = o.rot;
						t.xflip = o.xf;
						t.zflip = o.zf;
						done = true;
					}
				}
				if (!done) {
					// no picture joins these corners: one material, the seam's by the map's edge (so
					// the edge's row doesn't turn into battlements), else the corners' commonest
					unmatched++;
					const size_t ci[4] = { (size_t)Z * EW + X, (size_t)Z * EW + X + 1, (size_t)(Z + 1) * EW + X + 1, (size_t)(Z + 1) * EW + X };
					int best = -1, bestN = -1;
					for (int k = 0; k < 4; k++) {
						int cnt = 0;
						for (int o = 0; o < 4; o++)
							cnt += c[o] == c[k];
						cnt += matFixed[ci[k]] == 1 ? 10 : 0;
						if (cnt > bestN) { bestN = cnt; best = c[k]; }
					}
					a = best;
				}
			}
			if (!done && a >= 0 && a < M) {
				const auto& group = mats.groups[a]->textures;
				auto it = group.begin();
				std::advance(it, (size_t)((r >> 3) % group.size()));
				t.texture = it->second.get();
				t.rot = (r >> 8) & 3;
				t.xflip = (r >> 10) & 1;
				t.zflip = (r >> 11) & 1;
			}
		}

	// test runs: "WKB_EXTEND_HEIGHTS=<file>": the copy's vertex heights, (W + 1) x (H + 1) floats
	// after a header of 5 (W + 1, H + 1, n, w, h)
	if (const char* dump = getenv("WKB_EXTEND_HEIGHTS"))
		if (FILE* f = fopen(dump, "wb")) {
			const float hdr[5] = { (float)EW, (float)EH, (float)n, (float)w, (float)h };
			fwrite(hdr, sizeof(float), 5, f);
			fwrite(height.data(), sizeof(float), height.size(), f);
			fclose(f);
		}
	// test runs: "WKB_EXTEND_DUMP=<file.ppm>": the copy from above, a pixel per tile (material
	// kinds' colours, hill-shaded; water blue; the map's mesh edge marked)
	if (const char* dump = getenv("WKB_EXTEND_DUMP"))
		if (FILE* f = fopen(dump, "wb")) {
			fprintf(f, "P6\n%d %d\n255\n", W, H);
			static const uint8_t KC[7][3] = { { 110, 170, 70 }, { 130, 125, 120 }, { 245, 245, 250 }, { 220, 200, 140 }, { 40, 100, 40 }, { 140, 105, 70 }, { 200, 0, 200 } };
			for (int Z = H - 1; Z >= 0; Z--)
				for (int X = 0; X < W; X++) {
					const Terrain::Tile& t = e.tiles[(size_t)Z * W + X];
					const float v = H_(X, Z), gx = H_(X + 1, Z) - v, gz = H_(X, Z + 1) - v;
					const float shade = std::clamp(0.75f + (-gx + gz) * 0.06f, 0.3f, 1.3f) * (0.6f + 0.4f * std::clamp((v - lo) / relief, 0.0f, 1.0f));
					int k = K_OTHER;
					if (t.texture) {
						auto mi = mats.index.find(t.texture->grp);
						if (mi != mats.index.end())
							k = mats.kinds[mi->second];
						else if (auto tc = mats.transitionCorners.find(t.texture); tc != mats.transitionCorners.end())
							k = mats.kinds[tc->second[0]];
					}
					uint8_t px[3];
					for (int c = 0; c < 3; c++)
						px[c] = (uint8_t)std::clamp(KC[k][c] * shade, 0.0f, 255.0f);
					if (t.fullOfWater) {
						const float depth = std::clamp((t.waterLevel - v) / 8.0f, 0.0f, 1.0f);
						px[0] = (uint8_t)(40 - 20 * depth); px[1] = (uint8_t)(110 - 50 * depth); px[2] = (uint8_t)(210 - 60 * depth);
					}
					const bool edgeLine = ((X == n - 1 || X == n + w) && Z >= n - 1 && Z <= n + h) || ((Z == n - 1 || Z == n + h) && X >= n - 1 && X <= n + w);
					if (edgeLine) { px[0] = 255; px[1] = 220; px[2] = 0; }
					fwrite(px, 1, 3, f);
				}
			fclose(f);
		}

	{
		// how much of the extension is under water, and how deep (the log)
		int riverTiles = 0, seaTiles = 0;
		double riverDepth = 0;
		for (int Z = 0; Z < H; Z++)
			for (int X = 0; X < W; X++) {
				if (inMapT(X, Z))
					continue;
				const Terrain::Tile& t = e.tiles[(size_t)Z * W + X];
				if (!t.fullOfWater)
					continue;
				if (riverLevel[(size_t)Z * W + X] > -1e29f) {
					riverTiles++;
					riverDepth += t.waterLevel - 0.25f * (H_(X, Z) + H_(X + 1, Z) + H_(X, Z + 1) + H_(X + 1, Z + 1));
				}
				else
					seaTiles++;
			}
		printf("Extend map: water outside the map: %d river tiles (mean depth %.2f), %d sea and lake tiles\n", riverTiles, riverTiles ? riverDepth / riverTiles : 0.0, seaTiles);
	}
	// ---- Trees: forests on the forest floor (dense), copses where its patches thin out, and
	// lone trees over the meadows in loose groups; none in water, on sand, rock or snow, or on
	// steep ground. Places jittered from the tile's hash, so they keep no grid.
	if (trees) {
		trees->clear();
		const int matForest = mats.find(K_FOREST, matCount);
		auto heightAt = [&](float x, float z) {
			const int ix = std::clamp((int)x, 0, W - 1), iz = std::clamp((int)z, 0, H - 1);
			const float fx = x - ix, fz = z - iz;
			const float a = H_(ix, iz) + (H_(ix + 1, iz) - H_(ix, iz)) * fx;
			const float b = H_(ix, iz + 1) + (H_(ix + 1, iz + 1) - H_(ix, iz + 1)) * fx;
			return a + (b - a) * fz;
		};
		for (int Z = 1; Z < H - 1; Z++)
			for (int X = 1; X < W - 1; X++) {
				if (inMapT(X, Z))
					continue;
				const Terrain::Tile& t = e.tiles[(size_t)Z * W + X];
				if (t.fullOfWater)
					continue;
				const int c[4] = { mat[(size_t)Z * EW + X], mat[(size_t)Z * EW + X + 1], mat[(size_t)(Z + 1) * EW + X + 1], mat[(size_t)(Z + 1) * EW + X] };
				int forestCorners = 0, badCorners = 0;
				for (int k = 0; k < 4; k++) {
					if (c[k] < 0 || c[k] >= M)
						continue;
					const Kind kd = mats.kinds[c[k]];
					forestCorners += c[k] == matForest || kd == K_FOREST;
					badCorners += kd == K_ROCK || kd == K_SNOW || kd == K_SAND;
				}
				if (badCorners > 0)
					continue;
				const float rise = std::max({ std::abs(H_(X + 1, Z) - H_(X, Z)), std::abs(H_(X, Z + 1) - H_(X, Z)), std::abs(H_(X + 1, Z + 1) - H_(X, Z)) }) / 5.0f;
				if (rise > 0.5f)
					continue;
				// trees per tile: a forest's, its edge's, or the meadows' (grouped by a noise)
				const float groves = Fbm(seed, (float)X, (float)Z, 16.0f, 3, 120);
				// (without a forest floor of its own, the map's trees gather in copses)
				const float copse = matForest >= 0 && matCount[matForest] > 0 ? 0.12f : 0.4f;
				float density = forestCorners == 4 ? 0.8f : forestCorners > 0 ? 0.35f : (groves > 0.38f ? copse : 0.004f);
				density *= std::clamp(1.0f - (rise - 0.3f) / 0.2f, 0.0f, 1.0f);
				for (int k = 0; k < 2; k++) {
					const uint64_t r = HashAt(seed, X, Z, 130 + k);
					if (Unit(r) >= density * 0.5f)
						continue;
					const float fx = X + Unit(Mix(r)), fz = Z + Unit(Mix(r ^ 0x55));
					ExtendedTree tr;
					tr.x = (fx - e.edge) * 5.0f;
					tr.z = (fz - e.edge) * 5.0f;
					tr.y = heightAt(fx, fz);
					tr.yaw = Unit(Mix(r ^ 0x77)) * 6.2831853f;
					tr.scale = 0.8f + 0.45f * Unit(Mix(r ^ 0x99));
					tr.variant = (uint32_t)(Mix(r ^ 0xAB) >> 32);
					tr.forest = forestCorners > 0;
					trees->push_back(tr);
				}
			}
	}

	// ---- The far rings (FarTerrain): nested square rings around the copy, each twice as coarse as
	// the one inside it. Their land is the near ring's own (farBase and landAt, the same noise), the
	// rivers carry on as water along their courses, the sea is its flat surface; colours from the
	// map's materials by the same zones. Each ring's outer edge follows the next ring's inner edge,
	// and the copy's outer edge ring 0's, so no crack opens between them.
	if (far && farRings > 0) {
		far->vertices.clear();
		far->indices.clear();
		far->blocks.clear();
		// ring 0's spacing, tiles, by the detail (Low 20 units, Medium 10, High 5, Ultra 2.5)
		const float S0 = std::array<float, 4>{ 4.0f, 2.0f, 1.0f, 0.5f }[std::clamp(detail, 0, 3)];
		constexpr float B0 = 48.0f;  // ring 0's width, tiles; each ring twice the one before
		// the river points in buckets of 32 tiles, for the nearest one to a place
		constexpr float RB = 32.0f;
		std::map<std::pair<int, int>, std::vector<int>> riverBuckets;
		for (size_t k = 0; k < riverPts.size(); k++)
			riverBuckets[{ (int)std::floor(riverPts[k].x / RB), (int)std::floor(riverPts[k].z / RB) }].push_back((int)k);
		auto nearestRiver = [&](float px, float pz, float& u, float& level, float& half) {
			u = 1e30f;
			const int bx = (int)std::floor(px / RB), bz = (int)std::floor(pz / RB);
			for (int dz = -1; dz <= 1; dz++)
				for (int dx = -1; dx <= 1; dx++) {
					auto it = riverBuckets.find({ bx + dx, bz + dz });
					if (it == riverBuckets.end())
						continue;
					for (int k : it->second) {
						const RiverPt& p = riverPts[k];
						const float dd = std::sqrt((p.x - px) * (p.x - px) + (p.z - pz) * (p.z - pz)) - p.half;
						if (dd < u) { u = dd; level = p.level; half = p.half; }
					}
				}
		};
		auto colourOf = [&](int m) { return m >= 0 && m < M ? mats.colour[m] : std::array<float, 3>{ 90, 110, 60 }; };
		std::array<float, 11> rockShare{};
		for (int b = 0; b <= 10; b++)
			rockShare[b] = rockBinN[b] >= 20 ? (float)(rockBinSum[b] / rockBinN[b]) : (b > 0 ? rockShare[b - 1] : 0.0f);
		printf("Extend map: rock share by mountain share: %.2f %.2f %.2f %.2f %.2f %.2f\n", rockShare[0], rockShare[2], rockShare[4], rockShare[6], rockShare[8], rockShare[10]);
		const std::array<float, 3> cGrass = colourOf(matGrass), cRock = colourOf(matRock >= 0 ? matRock : matGrass);
		const std::array<float, 3> cSnow = matSnow >= 0 ? colourOf(matSnow) : std::array<float, 3>{ 235, 238, 242 };
		const std::array<float, 3> cSand = colourOf(matSand >= 0 ? matSand : matGrass);
		const std::array<float, 3> cForest = matForestZone >= 0 ? colourOf(matForestZone)
			: std::array<float, 3>{ cGrass[0] * 0.55f, cGrass[1] * 0.62f, cGrass[2] * 0.5f };
		auto mix3 = [](std::array<float, 3> a, const std::array<float, 3>& b, float t) {
			for (int c = 0; c < 3; c++)
				a[c] += (b[c] - a[c]) * std::clamp(t, 0.0f, 1.0f);
			return a;
		};

		struct Ring {
			std::vector<float> xs, zs;   // the grid's lines (copy tile coordinates)
			float ix0, iz0, ix1, iz1;    // its hole: the ring inside it
			float s = 1;                 // its spacing, tiles
			std::vector<float> hgt;      // per grid point: the land (under water its bed)
			std::vector<float> lvl;      // per grid point: the water's level over it (-1e30: none)
			std::vector<uint8_t> water;  // per grid point: under water (lvl over hgt)
			std::vector<uint8_t> used;   // per grid point: on a drawn cell
			std::vector<float> mtn;      // per grid point: the land's mountain share
			std::vector<float> flat;     // per grid point: how far the last ring has settled to its level
			int nx() const { return (int)xs.size(); }
			int nz() const { return (int)zs.size(); }
		};
		auto lines = [](float o0, float i0, float i1, float o1, float s) {
			std::vector<float> v;
			for (float x = o0; x < i0 - 0.5f * s; x += s)
				v.push_back(x);
			const int m = std::max(1, (int)std::lround((i1 - i0) / s));
			for (int k = 0; k <= m; k++)
				v.push_back(i0 + (i1 - i0) * k / m);
			for (float x = i1 + s; x <= o1 + 0.5f * s; x += s)
				v.push_back(x);
			return v;
		};
		std::vector<Ring> rings(farRings);
		float x0 = 0, z0 = 0, x1 = (float)W, z1 = (float)H;
		// the height of the land alone (no rivers) at a ring's grid spacing: for the normals
		auto landHeight = [&](float px, float pz, float s) { return farBase(px, pz, s) + landAt(px, pz, s).detail; };
		for (int k = 0; k < farRings; k++) {
			Ring& R = rings[k];
			const float s = S0 * (float)(1 << k), b = B0 * (float)(1 << k);
			R.s = s;
			R.ix0 = x0; R.iz0 = z0; R.ix1 = x1; R.iz1 = z1;
			R.xs = lines(x0 - b, x0, x1, x1 + b, s);
			R.zs = lines(z0 - b, z0, z1, z1 + b, s);
			x0 -= b; z0 -= b; x1 += b; z1 += b;
			const int NX = R.nx(), NZ = R.nz();
			R.hgt.assign((size_t)NX * NZ, 0.0f);
			R.lvl.assign((size_t)NX * NZ, -1e30f);
			R.water.assign((size_t)NX * NZ, 0);
			R.used.assign((size_t)NX * NZ, 0);
			R.mtn.assign((size_t)NX * NZ, 0.0f);
			R.flat.assign((size_t)NX * NZ, 0.0f);
			for (int j = 0; j + 1 < NZ; j++)
				for (int i = 0; i + 1 < NX; i++) {
					const float cx = 0.5f * (R.xs[i] + R.xs[i + 1]), cz = 0.5f * (R.zs[j] + R.zs[j + 1]);
					if (cx > R.ix0 && cx < R.ix1 && cz > R.iz0 && cz < R.iz1)
						continue; // the hole
					for (int c = 0; c < 4; c++)
						R.used[(size_t)(j + (c >> 1)) * NX + i + (c & 1)] = 1;
				}
			// The land keeps its own bed everywhere; the water is a level over it (a river's, the
			// sea's), drawn as a flat surface, so its shore is where the land rises out of it (the
			// grid points set to the water's level had torn the slopes beside them into walls and
			// holes, and drawn the shores along the grid, the play test of 2026-11-10)
			ParallelFor(NZ, [&](int j) {
				for (int i = 0; i < NX; i++) {
					const size_t gi = (size_t)j * NX + i;
					if (!R.used[gi])
						continue;
					const float px = R.xs[i], pz = R.zs[j];
					// (the noise without the octaves finer than the grid: they aliased into spikes)
					const Land L = landAt(px, pz, s);
					float v = farBase(px, pz, s) + L.detail;
					R.mtn[gi] = L.mtn;
					float lvl = -1e30f;
					// rivers: their bed under their level (a grid point within about a third of the
					// spacing of the channel: a river narrower than the grid still shows), their
					// valleys opening up from it
					float u, level = 0, half = 1;
					nearestRiver(px, pz, u, level, half);
					const float u2 = u - 0.35f * s;
					if (u2 <= 0) {
						lvl = level;
						v = std::min(v, level - 1.2f - 1.2f * std::min(-u2 / std::max(half, 1.0f), 1.0f));
					}
					else if (u2 < 20.0f + s)
						v = std::min(v, level + 0.35f + 1.1f * u2 * (0.4f + 0.06f * u2));
					// the sea's level over the land near the map's seas
					if (seaLevel > -1e29f && L.wet > 0.12f)
						lvl = std::max(lvl, seaLevel);
					R.hgt[gi] = v;
					R.lvl[gi] = lvl;
				}
			});
		}
		// The ground beyond the last ring (to the horizon, drawn by the sky): the sea if most of the
		// last ring's outer edge is, at its level; else land at the edge's mean height. Over the
		// last ring's outer part its land settles to that level, along a line wandering with a
		// noise (a straight one drew straight coasts), the sea's shores rising out of it.
		{
			Ring& R = rings.back();
			const int NX = R.nx(), NZ = R.nz();
			double wetN = 0, all = 0, hsum = 0;
			for (int j = 0; j < NZ; j++)
				for (int i = 0; i < NX; i++) {
					if (!(i == 0 || j == 0 || i == NX - 1 || j == NZ - 1))
						continue;
					const size_t gi = (size_t)j * NX + i;
					all += 1;
					const bool wet = R.lvl[gi] > R.hgt[gi];
					wetN += wet;
					if (!wet)
						hsum += R.hgt[gi];
				}
			far->outerSea = seaLevel > -1e29f && wetN > 0.5 * all;
			far->outerLevel = far->outerSea ? seaLevel : (all > wetN ? (float)(hsum / (all - wetN)) : seaLevel);
			if (!far->outerSea && seaLevel > -1e29f)
				far->outerLevel = std::max(far->outerLevel, seaLevel + 1.0f);
			const float bw = std::max(R.ix0 - R.xs.front(), 1.0f);
			for (int j = 0; j < NZ; j++)
				for (int i = 0; i < NX; i++) {
					const size_t gi = (size_t)j * NX + i;
					if (!R.used[gi])
						continue;
					const float px = R.xs[i], pz = R.zs[j];
					const float f = std::clamp(std::max({ R.ix0 - px, px - R.ix1, R.iz0 - pz, pz - R.iz1 }) / bw, 0.0f, 1.0f)
						+ 0.12f * Fbm(seed, px, pz, 90.0f, 3, 160, R.s);
					const float k = std::clamp((f - 0.7f) / 0.3f, 0.0f, 1.0f);
					const float wgt = k * k * (3 - 2 * k);
					R.flat[gi] = wgt;
					if (far->outerSea) {
						// the land sinks under the sea towards the end
						R.hgt[gi] += (far->outerLevel - 3.0f - R.hgt[gi]) * wgt;
						if (wgt > 0)
							R.lvl[gi] = std::max(R.lvl[gi], far->outerLevel);
					}
					else // the land settles to the level, the sea's floor rises out of it
						R.hgt[gi] += (far->outerLevel - R.hgt[gi]) * wgt;
				}
		}
		for (Ring& R : rings)
			for (size_t gi = 0; gi < R.hgt.size(); gi++)
				R.water[gi] = R.used[gi] && R.lvl[gi] > R.hgt[gi];
		// Detail falling off gradually: over the outer part of each ring its land is smoothed
		// towards the next ring's coarser grid (the step from one ring to the next was abrupt, the
		// play test of 2026-11-10)
		for (Ring& R : rings) {
			const int NX = R.nx(), NZ = R.nz();
			const float bw = std::max(R.ix0 - R.xs.front(), 1.0f);
			for (int pass = 0; pass < 2; pass++) {
				std::vector<float> out = R.hgt;
				for (int j = 1; j + 1 < NZ; j++)
					for (int i = 1; i + 1 < NX; i++) {
						const size_t gi = (size_t)j * NX + i;
						if (!R.used[gi] || R.water[gi])
							continue;
						const float px = R.xs[i], pz = R.zs[j];
						const float out_ = std::max({ R.ix0 - px, px - R.ix1, R.iz0 - pz, pz - R.iz1 }); // into the ring
						const float f = std::clamp(out_ / bw, 0.0f, 1.0f);
						const float wgt = std::clamp((f - 0.35f) / 0.65f, 0.0f, 1.0f);
						if (wgt <= 0)
							continue;
						float sum = 0, cnt = 0;
						for (const auto& [di, dj] : { std::pair{ 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } }) {
							const size_t g = (size_t)(j + dj) * NX + i + di;
							if (R.used[g]) { sum += R.hgt[g]; cnt += 1; }
						}
						if (cnt > 0)
							out[gi] = R.hgt[gi] + (sum / cnt - R.hgt[gi]) * 0.5f * wgt * wgt * (3 - 2 * wgt);
					}
				R.hgt.swap(out);
			}
		}
		// a ring's height along its hole's edge, between its own grid points there
		auto edgeHeight = [](const Ring& R, float px, float pz) {
			const int NX = R.nx();
			auto along = [](const std::vector<float>& ls, float p, int& a, float& t) {
				a = (int)(std::upper_bound(ls.begin(), ls.end(), p) - ls.begin()) - 1;
				a = std::clamp(a, 0, (int)ls.size() - 2);
				t = std::clamp((p - ls[a]) / std::max(ls[a + 1] - ls[a], 1e-4f), 0.0f, 1.0f);
			};
			int a, c;
			float t, q;
			along(R.xs, px, a, t);
			along(R.zs, pz, c, q);
			// on a vertical edge (x fixed): interpolate along z, and the other way
			if (std::abs(px - R.ix0) < 1e-3f || std::abs(px - R.ix1) < 1e-3f) {
				const int i = (int)(std::lower_bound(R.xs.begin(), R.xs.end(), px - 1e-3f) - R.xs.begin());
				return R.hgt[(size_t)c * NX + i] * (1 - q) + R.hgt[(size_t)(c + 1) * NX + i] * q;
			}
			const int j = (int)(std::lower_bound(R.zs.begin(), R.zs.end(), pz - 1e-3f) - R.zs.begin());
			return R.hgt[(size_t)j * NX + a] * (1 - t) + R.hgt[(size_t)j * NX + a + 1] * t;
		};
		// Stitching: ring k's outer edge on ring k + 1's inner edge (from the outside in, so each
		// ring's edge takes the next one's as that one already stands)
		for (int k = farRings - 2; k >= 0; k--) {
			Ring& R = rings[k];
			const Ring& O = rings[k + 1];
			const int NX = R.nx(), NZ = R.nz();
			for (int j = 0; j < NZ; j++)
				for (int i = 0; i < NX; i++) {
					if (!(i == 0 || j == 0 || i == NX - 1 || j == NZ - 1))
						continue;
					const size_t gi = (size_t)j * NX + i;
					R.hgt[gi] = edgeHeight(O, R.xs[i], R.zs[j]);
					R.water[gi] = R.lvl[gi] > R.hgt[gi];
				}
		}
		// and the copy's outer edge on ring 0's inner edge. Where ring 0 is water there, the copy
		// keeps its own bed if it is deeper (lifted, its last tile was shallow water over a dark bed
		// with the surf's foam, a dark line along the seam at dusk)
		auto edgeWet = [](const Ring& R, float px, float pz) {
			const int NX = R.nx();
			auto idx = [](const std::vector<float>& ls, float p) {
				int a = (int)(std::upper_bound(ls.begin(), ls.end(), p) - ls.begin()) - 1;
				return std::clamp(a, 0, (int)ls.size() - 2);
			};
			const int a = idx(R.xs, px), c = idx(R.zs, pz);
			const int i = (int)(std::lower_bound(R.xs.begin(), R.xs.end(), px - 1e-3f) - R.xs.begin());
			const int j = (int)(std::lower_bound(R.zs.begin(), R.zs.end(), pz - 1e-3f) - R.zs.begin());
			if (std::abs(px - R.ix0) < 1e-3f || std::abs(px - R.ix1) < 1e-3f)
				return R.water[(size_t)c * NX + i] && R.water[(size_t)(c + 1) * NX + i];
			return R.water[(size_t)j * NX + a] && R.water[(size_t)j * NX + a + 1];
		};
		for (int Z = 0; Z <= H; Z++)
			for (int X = 0; X <= W; X++) {
				if (!(X == 0 || Z == 0 || X == W || Z == H))
					continue;
				const float h = edgeHeight(rings[0], (float)X, (float)Z);
				if (edgeWet(rings[0], (float)X, (float)Z) && H_(X, Z) < h)
					continue;
				H_(X, Z) = h;
			}

		// The banks: a dry grid point on a cell with water stands a little over the water's level
		// there (the cell's water covers it whole; a dry corner lower than a river's level drew a
		// square of water out of its channel). The rings' edges stay as stitched.
		for (Ring& R : rings) {
			const int NX = R.nx(), NZ = R.nz();
			std::vector<float> lift = R.hgt;
			for (int j = 0; j + 1 < NZ; j++)
				for (int i = 0; i + 1 < NX; i++) {
					const size_t g[4] = { (size_t)j * NX + i, (size_t)j * NX + i + 1, (size_t)(j + 1) * NX + i, (size_t)(j + 1) * NX + i + 1 };
					float wl = -1e30f;
					for (size_t q : g)
						if (R.used[q] && R.water[q])
							wl = std::max(wl, R.lvl[q]);
					if (wl < -1e29f)
						continue;
					for (int c = 0; c < 4; c++) {
						const int ii = i + (c & 1), jj = j + (c >> 1);
						if (ii == 0 || jj == 0 || ii == NX - 1 || jj == NZ - 1 || R.water[g[c]])
							continue;
						lift[g[c]] = std::max(lift[g[c]], wl + 1.2f); // (the shore about halfway along the cell: 0.3 drew squares)
					}
				}
			R.hgt.swap(lift);
		}

		// The vertices: normals from the grid's central differences (as the mesh stands), at the
		// rings' edges, where a neighbour is missing, from the land's own height function at the
		// ring's spacing (one-sided differences there had lit the low sun's land in lines), going
		// flat where the last ring settles to its level; colours by the zones.
		// Then the cells by blocks: the land, and over every cell with water at a corner the
		// water's flat surface at its highest level there (PS_FarSea; the land rises through it).
		auto pk = [](float f) { return (uint32_t)std::clamp((int)std::lround((f * 0.5f + 0.5f) * 1023.0f), 0, 1023); };
		const uint32_t upNormal = pk(0.0f) | (pk(1.0f) << 10) | (pk(0.0f) << 20) | (3u << 30);
		double outerSum[3] = {}, outerN = 0;
		for (int k = 0; k < farRings; k++) {
			const Ring& R = rings[k];
			const int NX = R.nx(), NZ = R.nz();
			const float s = R.s, d = 0.5f * s;
			std::vector<std::array<float, 3>> grad((size_t)NX * NZ); // gx, gz, rise
			ParallelFor(NZ, [&](int j) {
				for (int i = 0; i < NX; i++) {
					const size_t gi = (size_t)j * NX + i;
					if (!R.used[gi])
						continue;
					const float px = R.xs[i], pz = R.zs[j];
					auto usedAt = [&](int ii, int jj) { return ii >= 0 && jj >= 0 && ii < NX && jj < NZ && R.used[(size_t)jj * NX + ii]; };
					float gx, gz;
					if (usedAt(i - 1, j) && usedAt(i + 1, j) && usedAt(i, j - 1) && usedAt(i, j + 1)) {
						gx = (R.hgt[gi + 1] - R.hgt[gi - 1]) / (5.0f * (R.xs[i + 1] - R.xs[i - 1]));
						gz = (R.hgt[gi + NX] - R.hgt[gi - NX]) / (5.0f * (R.zs[j + 1] - R.zs[j - 1]));
					}
					else {
						gx = (landHeight(px + d, pz, s) - landHeight(px - d, pz, s)) / (10.0f * d);
						gz = (landHeight(px, pz + d, s) - landHeight(px, pz - d, s)) / (10.0f * d);
					}
					const float keep = 1.0f - R.flat[gi];
					grad[gi] = { gx * keep, gz * keep, std::sqrt(gx * gx + gz * gz) * keep };
				}
			});
			std::vector<uint32_t> index((size_t)NX * NZ, ~0u);
			for (int j = 0; j < NZ; j++)
				for (int i = 0; i < NX; i++) {
					const size_t gi = (size_t)j * NX + i;
					if (!R.used[gi])
						continue;
					const float gx = grad[gi][0], gz = grad[gi][1], rise = grad[gi][2];
					Vector3 nrm(-gx, 1.0f, -gz);
					nrm = nrm.normal();
					const float px = R.xs[i], pz = R.zs[j], v = R.hgt[gi];
					std::array<float, 3> c = cGrass;
					// the map's other grounds in their patches, the forests in theirs (as the near ring's)
					for (size_t q = 1; q < landMats.size() && q < 6; q++) {
						const float p = Fbm(seed, px, pz, 22.0f + 6.0f * q, 4, 34 + (int)q * 7);
						if (p > 0.22f + 0.05f * q) {
							c = mix3(c, colourOf(landMats[q]), std::clamp((p - 0.22f - 0.05f * q) * 8.0f, 0.0f, 1.0f));
							break;
						}
					}
					const float fp = Fbm(seed, px, pz, 38.0f, 4, 140);
					c = mix3(c, cForest, std::clamp((fp - 0.14f) * 8.0f, 0.0f, 1.0f) * (rise < 0.45f ? 1.0f : 0.0f));
					const float jitter = Fbm(seed, px, pz, 9.0f, 3, 33);
					// sand along the water (the sea's and the rivers' shores)
					const float wl = R.lvl[gi] > -1e29f ? R.lvl[gi] : seaLevel;
					if (wl > -1e29f)
						c = mix3(c, cSand, std::clamp(1.0f - (v - wl) / 2.0f, 0.0f, 1.0f) * std::clamp(1.0f - (rise - 0.3f) / 0.2f, 0.0f, 1.0f));
					// (the land's rock: by its height and slope, or the near ring's share for its kind)
					const float mb = std::clamp(R.mtn[gi] * 10.0f, 0.0f, 10.0f);
					const int m0 = std::min((int)mb, 9);
					const float share = rockShare[m0] + (rockShare[m0 + 1] - rockShare[m0]) * (mb - m0);
					c = mix3(c, cRock, std::max(rockAt(v, rise, jitter), share) * (1.0f - R.flat[gi]));
					if (snowLine < 1e29f)
						c = mix3(c, cSnow, std::clamp((v - snowLine - 0.06f * relief * jitter) / (0.05f * relief + 1.0f), 0.0f, 1.0f));
					const float shade = 1.0f + 0.08f * Fbm(seed, px, pz, 5.0f, 2, 141);
					if (k == farRings - 1 && !R.water[gi]) {
						for (int q = 0; q < 3; q++)
							outerSum[q] += c[q];
						outerN += 1;
					}
					FarVertex fv;
					fv.x = (px - e.edge) * 5.0f;
					fv.z = (pz - e.edge) * 5.0f;
					fv.y = v;
					fv.normal = pk(nrm.x) | (pk(nrm.y) << 10) | (pk(nrm.z) << 20) | (3u << 30);
					auto b8 = [&](float f) { return (uint32_t)std::clamp((int)std::lround(f * shade), 0, 255); };
					fv.colour = b8(c[0]) | (b8(c[1]) << 8) | (b8(c[2]) << 16) | (255u << 24);
					index[gi] = (uint32_t)far->vertices.size();
					far->vertices.push_back(fv);
				}
			constexpr int BLOCK = 16;
			for (int bj = 0; bj + 1 < NZ; bj += BLOCK)
				for (int bi = 0; bi + 1 < NX; bi += BLOCK) {
					FarBlock blk;
					blk.first = (uint32_t)far->indices.size();
					float lo[3] = { 1e30f, 1e30f, 1e30f }, hi[3] = { -1e30f, -1e30f, -1e30f };
					auto grow = [&](const FarVertex& fv) {
						const float p[3] = { fv.x, fv.y, fv.z };
						for (int a = 0; a < 3; a++) {
							lo[a] = std::min(lo[a], p[a]);
							hi[a] = std::max(hi[a], p[a]);
						}
					};
					for (int j = bj; j < std::min(bj + BLOCK, NZ - 1); j++)
						for (int i = bi; i < std::min(bi + BLOCK, NX - 1); i++) {
							const size_t g0 = (size_t)j * NX + i, g1 = g0 + 1, g3 = g0 + NX, g2 = g3 + 1;
							const uint32_t c0 = index[g0], c1 = index[g1], c3 = index[g3], c2 = index[g2];
							const float cx = 0.5f * (R.xs[i] + R.xs[i + 1]), cz = 0.5f * (R.zs[j] + R.zs[j + 1]);
							if ((cx > R.ix0 && cx < R.ix1 && cz > R.iz0 && cz < R.iz1) || c0 == ~0u || c1 == ~0u || c2 == ~0u || c3 == ~0u)
								continue;
							for (uint32_t c : { c0, c3, c1, c1, c3, c2 })
								far->indices.push_back(c);
							for (uint32_t c : { c0, c1, c2, c3 })
								grow(far->vertices[c]);
							// the water's surface over the cell
							float wl = -1e30f;
							for (size_t g : { g0, g1, g2, g3 })
								if (R.water[g])
									wl = std::max(wl, R.lvl[g]);
							if (wl < -1e29f)
								continue;
							const uint32_t w0 = (uint32_t)far->vertices.size();
							for (size_t g : { g0, g1, g2, g3 }) {
								FarVertex fv = far->vertices[index[g]];
								fv.y = wl;
								fv.normal = upNormal;
								fv.colour = 0; // alpha 0: water (PS_FarSea)
								far->vertices.push_back(fv);
								grow(fv);
							}
							for (uint32_t c : { 0u, 3u, 1u, 1u, 3u, 2u })
								far->indices.push_back(w0 + c);
						}
					blk.count = (uint32_t)far->indices.size() - blk.first;
					if (blk.count == 0)
						continue;
					for (int a = 0; a < 3; a++) {
						blk.lo[a] = lo[a];
						blk.hi[a] = hi[a];
					}
					far->blocks.push_back(blk);
				}
		}
		far->reach = 5.0f * B0 * (float)((1 << farRings) - 1);
		// the land beyond: the last ring's mean colour (0-1, as PS_Far takes the vertex colours)
		for (int q = 0; q < 3; q++)
			far->outerColour[q] = std::clamp((outerN > 0 ? (float)(outerSum[q] / outerN) : cGrass[q]) / 255.0f, 0.0f, 1.0f);
		printf("Extend map: beyond the last ring %s at %.1f\n", far->outerSea ? "the sea" : "land", far->outerLevel);
		printf("Extend map: %d far rings to %.0f units beyond the near ring: %zu vertices, %zu triangles, %zu blocks\n",
			farRings, far->reach, far->vertices.size(), far->indices.size() / 3, far->blocks.size());
	}

	const size_t NV = (size_t)EW * EH;
	e.vertices.resize(NV);
	e.heightFraction.assign(NV, 0);
	for (size_t i = 0; i < NV; i++)
		SetSteps(e, i, height[i]);
	SyncExtendedHeights(map, e); // the middle exactly as the map's
	printf("Extend map: the map's small relief %.2f / %.2f / %.2f (1, 3, 8 tiles)\n", sigma1, sigma3, sigma8);
	printf("Extend map: %d x %d tiles (+%d on each side), seed %016llx; %d materials, %zu transition pairs, %zu river exits, %d lakes, %d ponds, %zu trees, %d tiles without a fitting transition\n",
		W, H, n, (unsigned long long)seed, M, mats.fits.size(), exits.size(), lakes, ponds, trees ? trees->size() : (size_t)0, unmatched);
	return ext;
}

void SyncExtendedHeights(const Terrain& map, Terrain& ext)
{
	const int n = ext.extendedTiles, W = ext.width;
	for (int z = 0; z <= map.height; z++)
		for (int x = 0; x <= map.width; x++) {
			const size_t s = (size_t)z * (map.width + 1) + x, d = (size_t)(z + n) * (W + 1) + (x + n);
			ext.vertices[d] = map.vertices[s];
			ext.heightFraction[d] = map.heightFraction.empty() ? 0 : map.heightFraction[s];
		}
}

void SyncExtendedTiles(const Terrain& map, Terrain& ext)
{
	const int n = ext.extendedTiles;
	for (int z = 0; z < map.height; z++)
		for (int x = 0; x < map.width; x++) {
			const Terrain::Tile* s = map.getTile(x, z);
			Terrain::Tile& t = ext.tiles[(size_t)(z + n) * ext.width + (x + n)];
			t.texture = s->texture;
			t.rot = s->rot;
			t.xflip = s->xflip;
			t.zflip = s->zflip;
		}
}

void SyncExtendedWater(const Terrain& map, Terrain& ext)
{
	// the map's own tiles' water; the extension's (its seas and rivers) stays as it was made
	const int n = ext.extendedTiles;
	ext.lakes.clear();
	for (Vector3 lake : map.lakes) {
		lake.x += 5.0f * n;
		lake.z += 5.0f * n;
		ext.lakes.push_back(lake);
	}
	ext.waterDesign = ShiftedDesign(map.waterDesign, ext.width, ext.height, n);
	ext.waterFrozen = map.waterFrozen;
	for (int z = 0; z < map.height; z++)
		for (int x = 0; x < map.width; x++) {
			const Terrain::Tile* s = map.getTile(x, z);
			Terrain::Tile& t = ext.tiles[(size_t)(z + n) * ext.width + (x + n)];
			t.fullOfWater = s->fullOfWater;
			t.waterLevel = s->waterLevel;
		}
}
