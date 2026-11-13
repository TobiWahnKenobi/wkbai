// wkbai - the far land cache's test (FarLandCache.h): a build without the cache as the reference;
// a cold build fills the cache and gives the same bytes; a warm build makes nothing and gives the
// same bytes; a changed map, a changed generator (a salt in the key) and another quality setting
// each make their own; a damaged entry is a miss, made again and rewritten.
// Licensed under the GNU General Public License 3

#include "FarLandCache.h"
#include "ExtendMap.h"
#include "terrain.h"
#include "file.h"
#include "gfx/RenderSettings.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {
	struct Built {
		FarTerrain far;
		std::vector<uint8_t> vertices, fractions;
		double ms = 0;
	};
	template <class T> bool Same(const std::vector<T>& a, const std::vector<T>& b)
	{
		return a.size() == b.size() && (a.empty() || memcmp(a.data(), b.data(), a.size() * sizeof(T)) == 0);
	}
	bool Same(const Built& a, const Built& b)
	{
		return Same(a.far.vertices, b.far.vertices) && Same(a.far.indices, b.far.indices) && Same(a.far.blocks, b.far.blocks)
			&& Same(a.far.nearColour, b.far.nearColour) && Same(a.far.nearHorizon[0], b.far.nearHorizon[0])
			&& Same(a.far.nearHorizon[1], b.far.nearHorizon[1]) && Same(a.vertices, b.vertices) && Same(a.fractions, b.fractions)
			&& a.far.reach == b.far.reach && a.far.outerLevel == b.far.outerLevel && a.far.outerSea == b.far.outerSea
			&& memcmp(a.far.outerColour, b.far.outerColour, sizeof(a.far.outerColour)) == 0;
	}
	int CountEntries(const std::filesystem::path& dir)
	{
		int n = 0;
		std::error_code ec;
		for (auto it = std::filesystem::directory_iterator(dir, ec); !ec && it != std::filesystem::directory_iterator(); it.increment(ec))
			n += it->path().extension() == ".wkf";
		return n;
	}
	std::filesystem::path NewestEntry(const std::filesystem::path& dir)
	{
		std::filesystem::path best;
		std::filesystem::file_time_type t{};
		std::error_code ec;
		for (auto it = std::filesystem::directory_iterator(dir, ec); !ec && it != std::filesystem::directory_iterator(); it.increment(ec))
			if (it->path().extension() == ".wkf" && (best.empty() || it->last_write_time() > t)) {
				best = it->path();
				t = it->last_write_time();
			}
		return best;
	}
}

void Test_FarLandCache()
{
	int failures = 0;
	auto check = [&](bool ok, const std::string& what) {
		if (!ok)
			failures++;
		printf("UNITTEST %s: %s\n", ok ? "ok" : "FAILED", what.c_str());
	};
	LoadBCP("data.bcp");
	Terrain map;
	map.readBCM("Maps\\128_4Peaks\\128_4Peaks.bcm");
	const std::filesystem::path dir = std::filesystem::temp_directory_path() / "wkbai-farlandcache-test";
	std::error_code ec;
	std::filesystem::remove_all(dir, ec);
	FarLandCache::SetDirectoryForTest(dir);
	FarLandCache::SetSaltForTest(0);
	const bool wasOn = g_renderSettings.farLandCache;
	const int n = ExtendMapTiles(2, 0), rings = ExtendMapFarRings(2);
	auto build = [&](int detail) {
		Built b;
		const auto t0 = std::chrono::steady_clock::now();
		ExtendedTerrain e = BuildExtendedTerrain(map, n, nullptr, rings, &b.far, detail, true);
		b.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
		b.vertices.assign(e->vertices.begin(), e->vertices.end());
		b.fractions = e->heightFraction;
		return b;
	};
	auto stats = [](const char* what) {
		const FarLandCache::Stats s = FarLandCache::GetStats();
		printf("  %s: %d hits, %d misses (%d damaged), %d stored; %.0f ms loading, %.0f ms saved, %.0f ms building\n", what,
			s.hits, s.misses, s.corrupt, s.stored, s.loadMs, s.savedMs, s.buildMs);
		return s;
	};

	// the reference: no cache
	g_renderSettings.farLandCache = false;
	const Built ref = build(0);
	check(!ref.far.vertices.empty() && !ref.far.nearHorizon[0].empty(), "the reference build made the far land (" + std::to_string(ref.far.vertices.size()) + " vertices)");
	check(CountEntries(dir) == 0, "without the cache nothing is written");
	g_renderSettings.farLandCache = true;

	// a cold load fills the cache
	FarLandCache::ResetStats();
	const Built cold = build(0);
	FarLandCache::Stats s = stats("cold");
	check(s.misses == 1 && s.hits == 0 && s.stored == 1, "a cold load misses and stores");
	check(CountEntries(dir) == 1, "one entry in the folder");
	check(Same(cold, ref), "the cold load's far land is the reference's, byte for byte");

	// a warm load makes nothing and gives the same
	FarLandCache::ResetStats();
	const Built warm = build(0);
	s = stats("warm");
	check(s.hits == 1 && s.misses == 0 && s.stored == 0, "a warm load hits and makes nothing");
	check(Same(warm, ref), "the warm load's far land is the reference's, byte for byte");
	printf("  times: reference %.0f ms, cold %.0f ms, warm %.0f ms\n", ref.ms, cold.ms, warm.ms);
	check(warm.ms < cold.ms, "the warm load is quicker");

	// another quality setting: its own entry, both kept
	FarLandCache::ResetStats();
	const Built other = build(1);
	s = stats("detail 1");
	check(s.misses == 1 && s.stored == 1 && CountEntries(dir) == 2, "another quality setting makes and keeps its own");
	FarLandCache::ResetStats();
	build(0);
	s = stats("detail 0 again");
	check(s.hits == 1 && s.misses == 0, "going back to the first setting loads it");
	FarLandCache::ResetStats();
	const Built otherWarm = build(1);
	s = stats("detail 1 again");
	check(s.hits == 1 && Same(otherWarm, other), "and the second again, the same bytes");

	// a changed map rebuilds
	{
		const size_t i = (size_t)(map.height / 2) * (map.width + 1) + map.width / 2;
		const auto before = map.vertices[i];
		map.vertices[i] = (decltype(before))(before + 3);
		FarLandCache::ResetStats();
		build(0);
		s = stats("map changed");
		check(s.misses == 1 && s.stored == 1, "a changed map is a miss and made again");
		map.vertices[i] = before;
	}

	// a changed generator rebuilds (a salt in the key, as EXTEND_MAP_ALGORITHM raised)
	FarLandCache::SetSaltForTest(1);
	FarLandCache::ResetStats();
	build(0);
	s = stats("generator changed");
	check(s.misses == 1 && s.stored == 1, "a changed generator is a miss and made again");
	FarLandCache::SetSaltForTest(0);

	// a damaged entry is a miss, made again, rewritten
	FarLandCache::ResetStats();
	build(0); // (touches the original entry: the newest)
	const std::filesystem::path victim = NewestEntry(dir);
	const uintmax_t size = std::filesystem::file_size(victim, ec);
	std::filesystem::resize_file(victim, size / 2, ec);
	FarLandCache::ResetStats();
	const Built repaired = build(0);
	s = stats("damaged");
	check(s.misses == 1 && s.corrupt == 1 && s.stored == 1, "a truncated entry is a miss, made again and stored");
	check(Same(repaired, ref), "and the far land is the reference's");
	{
		// a flipped byte inside the data
		std::fstream f(victim, std::ios::in | std::ios::out | std::ios::binary);
		f.seekg(std::filesystem::file_size(victim, ec) - 100);
		char c = 0;
		f.read(&c, 1);
		c ^= 0x5A;
		f.seekp(std::filesystem::file_size(victim, ec) - 100);
		f.write(&c, 1);
	}
	FarLandCache::ResetStats();
	const Built flipped = build(0);
	s = stats("flipped byte");
	check(s.corrupt == 1 && Same(flipped, ref), "a changed byte is caught by the contents hash");
	FarLandCache::ResetStats();
	build(0);
	s = stats("after repair");
	check(s.hits == 1, "the rewritten entry hits again");

	// the size cap: the least recently used go first
	{
		const int before = CountEntries(dir);
		const int keep = g_renderSettings.farLandCacheMB;
		g_renderSettings.farLandCacheMB = 0; // (below any entry: only the newest kept)
		const int left = FarLandCache::PruneNow();
		check(left >= 1 && left < before, "over the size cap the oldest entries go (" + std::to_string(before) + " -> " + std::to_string(left) + ")");
		g_renderSettings.farLandCacheMB = keep;
	}

	g_renderSettings.farLandCache = wasOn;
	FarLandCache::SetDirectoryForTest({});
	std::filesystem::remove_all(dir, ec);
	printf("FARLANDCACHE failures %d\n", failures);
	printf(failures == 0 ? "UNITTEST PASSED\n" : "UNITTEST FAILED\n");
	fflush(stdout);
}
