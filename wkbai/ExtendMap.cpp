// wkbai - Extend map: the land continued beyond a map's edge (a graphics setting)
// Licensed under the GNU General Public License 3

#include "ExtendMap.h"
#include "terrain.h"
#include "WaterDesign.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <vector>

namespace {

// bumped whenever the generator's output changes (the cache's key, phase 5)
constexpr uint64_t GENERATOR_VERSION = 1;

// the near ring's reach at "Near"
constexpr int NEAR_TILES = 64;

uint64_t Fnv1a(uint64_t h, const void* data, size_t size)
{
	const uint8_t* p = (const uint8_t*)data;
	for (size_t i = 0; i < size; i++) {
		h ^= p[i];
		h *= 0x100000001B3ull;
	}
	return h;
}

// SplitMix64: every random choice from the map's seed and a place, never a std:: distribution
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

// smooth value noise, -1 to 1, cells of the given size (tiles)
float ValueNoise(uint64_t seed, float x, float z, float cell, int salt)
{
	const float fx = x / cell, fz = z / cell;
	const int ix = (int)std::floor(fx), iz = (int)std::floor(fz);
	float tx = fx - ix, tz = fz - iz;
	tx = tx * tx * (3 - 2 * tx);
	tz = tz * tz * (3 - 2 * tz);
	auto v = [&](int a, int b) { return Unit(HashAt(seed, ix + a, iz + b, salt)) * 2 - 1; };
	const float a = v(0, 0) + (v(1, 0) - v(0, 0)) * tx, b = v(0, 1) + (v(1, 1) - v(0, 1)) * tx;
	return a + (b - a) * tz;
}

uint64_t MapSeed(const Terrain& map)
{
	uint64_t h = Fnv1a(0xCBF29CE484222325ull, &GENERATOR_VERSION, sizeof(GENERATOR_VERSION));
	if (!map.bcmData.empty())
		h = Fnv1a(h, map.bcmData.data(), map.bcmData.size());
	else
		h = Fnv1a(h, map.mapFile.data(), map.mapFile.size());
	// the heights as loaded (a .wkbmap's fractions, a map made in the editor)
	h = Fnv1a(h, map.vertices.data(), map.vertices.size());
	if (!map.heightFraction.empty())
		h = Fnv1a(h, map.heightFraction.data(), map.heightFraction.size());
	return h;
}

bool IsMaterial(const TerrainTexture* tex)
{
	return tex && tex->grp && tex->grp->name.find('-') == std::string::npos;
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

// The map's tile that an outside tile of the copy continues: the nearest one on the mesh edge
const Terrain::Tile* SourceTile(const Terrain& map, int n, int X, int Z)
{
	return map.getTile(std::clamp(X - n, 0, map.width - 1), std::clamp(Z - n, 0, map.height - 1));
}

bool Inside(const Terrain& map, int n, int X, int Z)
{
	return X >= n && Z >= n && X < n + map.width && Z < n + map.height;
}

// The water of the copy's outside tiles: a tile whose source on the mesh edge is under water
// carries that water on, at its level
void ExtendWater(const Terrain& map, Terrain& ext)
{
	const int n = ext.extendedTiles;
	for (int Z = 0; Z < ext.height; Z++)
		for (int X = 0; X < ext.width; X++) {
			Terrain::Tile& t = ext.tiles[(size_t)Z * ext.width + X];
			const Terrain::Tile* s = Inside(map, n, X, Z) ? map.getTile(X - n, Z - n) : SourceTile(map, n, X, Z);
			t.fullOfWater = s->fullOfWater;
			t.waterLevel = s->waterLevel;
		}
}

void SetSteps(Terrain& ext, size_t i, float height)
{
	const float steps = std::clamp(height / ext.scale, 0.0f, 255.0f);
	const int whole = std::min((int)steps, 255);
	ext.vertices[i] = (uint8_t)whole;
	ext.heightFraction[i] = (uint8_t)std::clamp((int)((steps - whole) * 256.0f), 0, 255);
}

} // namespace

int ExtendMapTiles(int setting)
{
	return setting >= 1 ? NEAR_TILES : 0;
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

ExtendedTerrain BuildExtendedTerrain(const Terrain& map, int n)
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

	// Tiles: the map's in the middle; outside, the edge tile's (a material's varied: another of
	// its group's tiles, turned and flipped at random; a transition kept as it is)
	e.tiles = new Terrain::Tile[(size_t)W * H];
	for (int Z = 0; Z < H; Z++)
		for (int X = 0; X < W; X++) {
			Terrain::Tile& t = e.tiles[(size_t)Z * W + X];
			const bool inside = Inside(map, n, X, Z);
			const Terrain::Tile* s = inside ? map.getTile(X - n, Z - n) : SourceTile(map, n, X, Z);
			t = *s;
			t.x = X;
			t.z = Z;
			if (!inside && IsMaterial(s->texture)) {
				const uint64_t r = HashAt(seed, X, Z, 1);
				const auto& group = s->texture->grp->textures;
				auto it = group.begin();
				std::advance(it, (size_t)(r % group.size()));
				if (it->second)
					t.texture = it->second.get();
				t.rot = (r >> 8) & 3;
				t.xflip = (r >> 10) & 1;
				t.zflip = (r >> 11) & 1;
			}
		}
	ExtendWater(map, e);

	// Heights. The map's own vertices are copied exactly. Outside, each vertex continues its
	// nearest point on the mesh edge: that point's height, smoothed more the farther out (so the
	// edge's small bumps don't run on as stripes), plus the edge's outward slope over its last 8
	// vertices, fading to level over about 12 tiles, plus a gentle noise growing in from the edge.
	const int VW = w + 1, VH = h + 1;
	std::vector<float> src((size_t)VW * VH), blur((size_t)VW * VH), tmp((size_t)VW * VH);
	float lo = 1e30f, hi = -1e30f;
	for (int z = 0; z < VH; z++)
		for (int x = 0; x < VW; x++) {
			const float v = map.getVertex(x, z);
			src[(size_t)z * VW + x] = v;
			lo = std::min(lo, v);
			hi = std::max(hi, v);
		}
	constexpr int R = 6; // the smoothing's box radius (vertices), twice
	auto boxBlur = [&](const std::vector<float>& in, std::vector<float>& out) {
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
	};
	boxBlur(src, blur);
	std::vector<float> blur2((size_t)VW * VH);
	boxBlur(blur, blur2);
	auto S = [&](int x, int z) { return src[(size_t)std::clamp(z, 0, VH - 1) * VW + std::clamp(x, 0, VW - 1)]; };
	auto B = [&](int x, int z) { return blur2[(size_t)std::clamp(z, 0, VH - 1) * VW + std::clamp(x, 0, VW - 1)]; };
	const float relief = std::max(hi - lo, 1.0f);
	constexpr float FADE = 12.0f;        // tiles over which the edge's slope levels off
	constexpr float MAX_SLOPE = 2.0f;    // units per tile (0.4)
	const size_t NV = (size_t)(W + 1) * (H + 1);
	e.vertices.resize(NV);
	e.heightFraction.assign(NV, 0);
	std::vector<float> height(NV);
	for (int Z = 0; Z <= H; Z++)
		for (int X = 0; X <= W; X++) {
			const size_t i = (size_t)Z * (W + 1) + X;
			const int sx = X - n, sz = Z - n;
			if (sx >= 0 && sz >= 0 && sx <= w && sz <= h) {
				height[i] = S(sx, sz);
				continue;
			}
			const int cx = std::clamp(sx, 0, w), cz = std::clamp(sz, 0, h);
			const int ox = sx - cx, oz = sz - cz;
			const float d = std::sqrt((float)(ox * ox + oz * oz));
			const float smooth = std::min(d / 8.0f, 1.0f);
			float v = S(cx, cz) + (B(cx, cz) - S(cx, cz)) * smooth;
			auto continueAlong = [&](int o, int ix, int iz) {
				if (o == 0)
					return 0.0f;
				const float g = std::clamp((B(cx, cz) - B(ix, iz)) / 8.0f, -MAX_SLOPE, MAX_SLOPE);
				return g * FADE * (1.0f - std::exp(-std::abs((float)o) / FADE));
			};
			v += continueAlong(ox, cx - 8 * (ox > 0 ? 1 : -1), cz);
			v += continueAlong(oz, cx, cz - 8 * (oz > 0 ? 1 : -1));
			const float ramp = std::min(d / 16.0f, 1.0f);
			v += ramp * relief * (0.035f * ValueNoise(seed, (float)X, (float)Z, 24.0f, 2) + 0.012f * ValueNoise(seed, (float)X, (float)Z, 7.0f, 3));
			height[i] = v;
		}
	// Water carried on: its bed under its level, and its shores (vertices of water and land
	// tiles both) close to it, so neither a sheet of water hangs over lower land nor land walls
	// rise out of the water at the mesh edge's line. The map's own vertices stay as they are.
	for (int Z = 0; Z <= H; Z++)
		for (int X = 0; X <= W; X++) {
			if (X >= n && Z >= n && X <= n + w && Z <= n + h)
				continue;
			int wet = 0, dry = 0;
			float lowLevel = 1e30f, highLevel = -1e30f;
			for (int dz = -1; dz <= 0; dz++)
				for (int dx = -1; dx <= 0; dx++) {
					const Terrain::Tile* t = e.getTile(X + dx, Z + dz);
					if (!t)
						continue;
					if (t->fullOfWater) {
						wet++;
						lowLevel = std::min(lowLevel, t->waterLevel);
						highLevel = std::max(highLevel, t->waterLevel);
					}
					else
						dry++;
				}
			float& v = height[(size_t)Z * (W + 1) + X];
			if (wet && !dry)
				v = std::min(v, lowLevel - 1.0f);
			else if (wet && dry)
				v = std::clamp(v, highLevel - 0.3f, highLevel + 0.6f);
		}
	for (size_t i = 0; i < NV; i++)
		SetSteps(e, i, height[i]);
	SyncExtendedHeights(map, e); // the middle exactly as the map's
	printf("Extend map: %d x %d tiles (+%d on each side), seed %016llx\n", W, H, n, (unsigned long long)seed);
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
	const int n = ext.extendedTiles;
	ext.lakes.clear();
	for (Vector3 lake : map.lakes) {
		lake.x += 5.0f * n;
		lake.z += 5.0f * n;
		ext.lakes.push_back(lake);
	}
	ext.waterDesign = ShiftedDesign(map.waterDesign, ext.width, ext.height, n);
	ext.waterFrozen = map.waterFrozen;
	ExtendWater(map, ext);
}
