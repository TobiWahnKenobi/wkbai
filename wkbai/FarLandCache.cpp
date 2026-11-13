// wkbai - Extend map's far land kept on disk (FarLandCache.h)
// Licensed under the GNU General Public License 3

#include "FarLandCache.h"
#include "ExtendMap.h"
#include "file.h"
#include "gfx/RenderSettings.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <mutex>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

namespace FarLandCache {

namespace {

constexpr uint32_t ENTRY_MAGIC = 0x4C46574B; // "KWFL"
constexpr const char* ENTRY_EXT = ".wkf";
constexpr auto STALE_AFTER = std::chrono::hours(24 * 90);

#pragma pack(push, 1)
struct EntryHeader {
	uint32_t magic;
	uint32_t format;
	uint64_t key;      // the file's name
	uint64_t check;    // the key's second hash
	float buildMs;     // how long it took to make
	uint32_t reserved;
	uint64_t vertices, indices, blocks, nearColour, nearHorizon0, nearHorizon1, heights;
	float reach, outerLevel, outerColour[3];
	uint32_t outerSea;
	uint64_t size;     // of the data after this header
	uint64_t dataHash; // of the data
};
#pragma pack(pop)

struct State {
	std::mutex lock;
	std::filesystem::path testDir;
	uint64_t salt = 0;
	bool pruned = false;
	Stats stats;
	std::atomic<uint32_t> tmpCounter{ 0 };
};
State& S()
{
	static State s;
	return s;
}

uint64_t HashBytes(const void* data, size_t size)
{
	Hasher h;
	h.bytes(data, size);
	return h.b;
}

double Ms(std::chrono::steady_clock::time_point t)
{
	return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count();
}

bool WriteAtomic(const std::filesystem::path& p, const void* a, size_t aSize, const void* b, size_t bSize)
{
	std::error_code ec;
	std::filesystem::create_directories(p.parent_path(), ec);
	char suffix[64];
	snprintf(suffix, sizeof(suffix), ".%lu.%u.tmp", GetCurrentProcessId(), S().tmpCounter++);
	std::filesystem::path tmp = p;
	tmp += suffix;
	{
		std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
		if (!out)
			return false;
		out.write((const char*)a, (std::streamsize)aSize);
		if (bSize)
			out.write((const char*)b, (std::streamsize)bSize);
		out.flush();
		if (!out) {
			out.close();
			std::filesystem::remove(tmp, ec);
			return false;
		}
	}
	if (!MoveFileExW(tmp.c_str(), p.c_str(), MOVEFILE_REPLACE_EXISTING)) {
		std::filesystem::remove(tmp, ec);
		return false;
	}
	return true;
}

uint64_t CapBytes()
{
	return (uint64_t)std::max(0, g_renderSettings.farLandCacheMB) << 20; // (the settings keep it at 64 MB or more; tests go lower)
}

// Removes temporary files, entries unused for 90 days, then the least recently used entries until
// the folder is under the cap; returns the entries left
int Prune(const std::filesystem::path& dir)
{
	std::error_code ec;
	struct Entry { std::filesystem::path path; std::filesystem::file_time_type time; uint64_t size; };
	std::vector<Entry> entries;
	const auto now = std::filesystem::file_time_type::clock::now();
	for (auto it = std::filesystem::directory_iterator(dir, ec); !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
		const std::filesystem::path& p = it->path();
		if (p.extension() == ".tmp") {
			std::filesystem::remove(p, ec);
			continue;
		}
		if (p.extension() != ENTRY_EXT)
			continue;
		const auto t = std::filesystem::last_write_time(p, ec);
		const uint64_t size = std::filesystem::file_size(p, ec);
		if (now - t > STALE_AFTER) {
			std::filesystem::remove(p, ec);
			continue;
		}
		entries.push_back({ p, t, size });
	}
	std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) { return a.time > b.time; });
	uint64_t total = 0;
	const uint64_t cap = CapBytes();
	size_t kept = 0;
	for (const Entry& e : entries) {
		total += e.size;
		if (total > cap && kept > 0) // (the newest is always kept)
			std::filesystem::remove(e.path, ec);
		else
			kept++;
	}
	return (int)kept;
}

void PruneOnce(const std::filesystem::path& dir)
{
	State& s = S();
	if (s.pruned)
		return;
	s.pruned = true;
	Prune(dir);
}

}

bool Enabled()
{
	return g_renderSettings.farLandCache && !Directory().empty();
}

std::filesystem::path Directory()
{
	State& s = S();
	if (!s.testDir.empty())
		return s.testDir;
	if (g_userPath.empty())
		return {};
	return g_userPath / "farlandcache";
}

std::filesystem::path EntryPath(const Key& key)
{
	char name[32];
	snprintf(name, sizeof(name), "%016llx", (unsigned long long)key.a);
	return Directory() / (std::string(name) + ENTRY_EXT);
}

bool Load(const Key& key, FarTerrain& ft, std::vector<float>& height)
{
	if (!Enabled())
		return false;
	State& s = S();
	std::lock_guard<std::mutex> lock(s.lock);
	const auto t0 = std::chrono::steady_clock::now();
	PruneOnce(Directory());
	const std::filesystem::path p = EntryPath(key);
	std::ifstream in(p, std::ios::binary);
	if (!in) {
		s.stats.misses++;
		return false;
	}
	auto corrupt = [&](const char* why) {
		printf("Far land cache: %s damaged (%s): made again\n", p.filename().string().c_str(), why);
		s.stats.misses++;
		s.stats.corrupt++;
		return false;
	};
	EntryHeader h{};
	if (!in.read((char*)&h, sizeof(h)))
		return corrupt("short");
	if (h.magic != ENTRY_MAGIC || h.format != FORMAT_VERSION)
		return corrupt("format");
	if (h.key != key.a || h.check != key.b)
		return corrupt("key");
	const uint64_t expect = h.vertices * sizeof(FarVertex) + h.indices * 4 + h.blocks * sizeof(FarBlock)
		+ (h.nearColour + h.nearHorizon0 + h.nearHorizon1) * 4 + h.heights * 4;
	if (h.size != expect || h.heights != height.size())
		return corrupt("size");
	std::vector<uint8_t> data((size_t)h.size);
	if (h.size && !in.read((char*)data.data(), (std::streamsize)h.size))
		return corrupt("short");
	if (HashBytes(data.data(), data.size()) != h.dataHash)
		return corrupt("contents");
	const uint8_t* q = data.data();
	auto take = [&](auto& v, uint64_t n) {
		using T = typename std::decay_t<decltype(v)>::value_type;
		v.resize((size_t)n);
		if (n)
			memcpy(v.data(), q, (size_t)n * sizeof(T));
		q += n * sizeof(T);
	};
	take(ft.vertices, h.vertices);
	take(ft.indices, h.indices);
	take(ft.blocks, h.blocks);
	take(ft.nearColour, h.nearColour);
	take(ft.nearHorizon[0], h.nearHorizon0);
	take(ft.nearHorizon[1], h.nearHorizon1);
	take(height, h.heights);
	ft.reach = h.reach;
	ft.outerLevel = h.outerLevel;
	ft.outerSea = h.outerSea != 0;
	for (int i = 0; i < 3; i++)
		ft.outerColour[i] = h.outerColour[i];
	in.close();
	// (used now: the last to go when the folder is pruned)
	std::error_code ec;
	std::filesystem::last_write_time(p, std::filesystem::file_time_type::clock::now(), ec);
	const double ms = Ms(t0);
	s.stats.hits++;
	s.stats.loadMs += ms;
	s.stats.savedMs += std::max(0.0, (double)h.buildMs - ms);
	return true;
}

void Store(const Key& key, const FarTerrain& ft, const std::vector<float>& height, double buildMs)
{
	if (!Enabled())
		return;
	State& s = S();
	std::lock_guard<std::mutex> lock(s.lock);
	s.stats.buildMs += buildMs;
	std::vector<uint8_t> data;
	auto put = [&](const auto& v) {
		using T = typename std::decay_t<decltype(v)>::value_type;
		const size_t n = v.size() * sizeof(T);
		const size_t at = data.size();
		data.resize(at + n);
		if (n)
			memcpy(data.data() + at, v.data(), n);
	};
	put(ft.vertices);
	put(ft.indices);
	put(ft.blocks);
	put(ft.nearColour);
	put(ft.nearHorizon[0]);
	put(ft.nearHorizon[1]);
	put(height);
	EntryHeader h{};
	h.magic = ENTRY_MAGIC;
	h.format = FORMAT_VERSION;
	h.key = key.a;
	h.check = key.b;
	h.buildMs = (float)buildMs;
	h.vertices = ft.vertices.size();
	h.indices = ft.indices.size();
	h.blocks = ft.blocks.size();
	h.nearColour = ft.nearColour.size();
	h.nearHorizon0 = ft.nearHorizon[0].size();
	h.nearHorizon1 = ft.nearHorizon[1].size();
	h.heights = height.size();
	h.reach = ft.reach;
	h.outerLevel = ft.outerLevel;
	h.outerSea = ft.outerSea ? 1 : 0;
	for (int i = 0; i < 3; i++)
		h.outerColour[i] = ft.outerColour[i];
	h.size = data.size();
	h.dataHash = HashBytes(data.data(), data.size());
	const std::filesystem::path p = EntryPath(key);
	if (WriteAtomic(p, &h, sizeof(h), data.data(), data.size())) {
		s.stats.stored++;
		Prune(Directory());
	}
	else
		printf("Far land cache: could not write %s\n", p.string().c_str());
}

Stats GetStats()
{
	std::lock_guard<std::mutex> lock(S().lock);
	return S().stats;
}

void ResetStats()
{
	std::lock_guard<std::mutex> lock(S().lock);
	S().stats = Stats();
}

void SetDirectoryForTest(const std::filesystem::path& dir)
{
	std::lock_guard<std::mutex> lock(S().lock);
	S().testDir = dir;
	S().pruned = false;
}

void SetSaltForTest(uint64_t salt)
{
	S().salt = salt;
}

uint64_t Salt()
{
	return S().salt;
}

int PruneNow()
{
	std::lock_guard<std::mutex> lock(S().lock);
	return Prune(Directory());
}

}
