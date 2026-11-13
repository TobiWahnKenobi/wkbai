// wkbai - Extend map's far land kept on disk (<user folder>\farlandcache), so it is generated
// again only when the map, the generator or a setting that changes it changed.
// Licensed under the GNU General Public License 3
//
// What is kept: the far rings' mesh and fields (FarTerrain: vertices, indices, blocks, the ground
// beyond, the near ring's colour field and horizons) and the copy's heights as they stand after
// the far rings were stitched to it: everything BuildExtendedTerrain's far part makes. The near
// ring itself (about a fifth of the time) is made as before.
//
// Each entry is kept by a hash (BuildExtendedTerrain, FarLandKey) of everything the far part
// depends on: the map's own inputs (heights and their fractions, tiles with their textures,
// turns and water, lakes, the water design), the materials' colours and kinds as read from the
// textures, the copy's heights before the far part, the seed, the settings that change the far
// land (the extension's tiles, the rings, their detail, the shadows), the generator's version
// (EXTEND_MAP_ALGORITHM, raised with every change of the generator, and the build time of
// ExtendMap.cpp, so any rebuild of it counts as a change) and the cache's format. Every quality
// setting has its own entries: going back to one already made loads it.
//
// Robustness: an entry is written to a temporary name and renamed over; one that is short, has the
// wrong format, key or contents hash is a miss (made again and rewritten). The folder is kept under
// RenderSettings::farLandCacheMB, least recently used entries first, and entries unused for 90 days
// are removed. RenderSettings::farLandCache off: no reading, no writing.

#pragma once

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

struct FarTerrain;

namespace FarLandCache {

// raised whenever the entry layout or what goes into a key changes: every older entry is a miss
constexpr uint32_t FORMAT_VERSION = 1;

// Two independent 64-bit hashes of the same bytes: the first names the entry's file, the second is
// kept inside it and checked, so a collision of one alone is still a miss
struct Hasher {
	uint64_t a = 1469598103934665603ull; // FNV-1a
	uint64_t b = 0x6A09E667F3BCC909ull;
	void bytes(const void* data, size_t size)
	{
		const uint8_t* p = (const uint8_t*)data;
		for (size_t i = 0; i < size; i++) {
			a = (a ^ p[i]) * 1099511628211ull;
			b = (b + p[i] + 1) * 0x9E3779B97F4A7C15ull;
			b ^= b >> 29;
		}
	}
	template <class T> void value(const T& v) { bytes(&v, sizeof(v)); }
	// (any container with size() and data(): std::vector, DynArray)
	template <class C> void array(const C& v) { value((uint64_t)v.size()); if (v.size()) bytes(v.data(), v.size() * sizeof(*v.data())); }
	void text(const char* s) { const uint64_t n = s ? strlen(s) : ~0ull; value(n); if (s) bytes(s, (size_t)n); }
};
struct Key { uint64_t a = 0, b = 0; };

// on (RenderSettings::farLandCache, and a folder to keep it in)
bool Enabled();
// The far part from the cache: its FarTerrain and the copy's heights after it (height: as many as
// the copy has vertices, which it must match). false: a miss (none, damaged, or off).
bool Load(const Key& key, FarTerrain& ft, std::vector<float>& height);
// and kept (buildMs: how long it took to make, for the time a hit saves)
void Store(const Key& key, const FarTerrain& ft, const std::vector<float>& height, double buildMs);

struct Stats {
	int hits = 0, misses = 0;
	int corrupt = 0;       // of the misses: an entry was there but unreadable
	int stored = 0;
	double loadMs = 0;     // reading the hits
	double savedMs = 0;    // the hits' build times less their loading
	double buildMs = 0;    // making the misses
};
Stats GetStats();
void ResetStats();
std::filesystem::path Directory();
// the entry file of a key (tests)
std::filesystem::path EntryPath(const Key& key);

// tests: the folder (empty: the user folder's farlandcache) and a salt mixed into every key (a
// pretend change of the generator)
void SetDirectoryForTest(const std::filesystem::path& dir);
void SetSaltForTest(uint64_t salt);
uint64_t Salt();
// prunes the folder now (stale entries, the size cap); returns the entries left
int PruneNow();

}
