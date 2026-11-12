// wkbai - the test of the shader cache (testList: "Shader cache")
//
// A cold start (the cache folder emptied) through the renderer's start-up and the
// post-processing and FSR 3 shaders, which need no map: every shader is compiled and stored.
// Then every request of that start made again: from the cache (0 compiled); with one shader's
// source changed (only that one compiled); an included file changed (only its includer);
// damaged entries (only those compiled again); a pretend driver update (everything compiled
// again); the size cap. The time saved, cold against warm, at the end.

#include "ShaderCache.h"
#include "renderer.h"
#include "renderer_diligent.h"
#include "DiligentFsr3.h"
#include "../window.h"
#include "../file.h"

#include "Shader.h"
#include "ShaderSourceFactoryUtils.h"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

using namespace Diligent;

namespace {

int CountEntries()
{
	int n = 0;
	std::error_code ec;
	for (auto it = std::filesystem::directory_iterator(ShaderCache::Directory(), ec); !ec && it != std::filesystem::directory_iterator(); it.increment(ec))
		n += it->path().extension() == ".wks";
	return n;
}

std::vector<std::filesystem::path> Entries()
{
	std::vector<std::filesystem::path> v;
	std::error_code ec;
	for (auto it = std::filesystem::directory_iterator(ShaderCache::Directory(), ec); !ec && it != std::filesystem::directory_iterator(); it.increment(ec))
		if (it->path().extension() == ".wks")
			v.push_back(it->path());
	return v;
}

double Ms(std::chrono::steady_clock::time_point t)
{
	return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count();
}

} // namespace

void Test_ShaderCache()
{
	int failures = 0;
	auto check = [&](bool ok, const std::string& what) {
		if (!ok)
			failures++;
		printf("UNITTEST %s: %s\n", ok ? "ok" : "FAILED", what.c_str());
	};
	auto stats = [](const char* what, const ShaderCache::Stats& s) {
		printf("  %s: %d requests, %d from the cache, %d compiled, %d damaged, %d failing; %.0f ms compiling, %.0f ms loading\n", what,
			s.requests, s.loaded, s.compiled, s.corrupt, s.failed, s.compileMs, s.loadMs);
	};

	// the cold start: no cache folder
	std::error_code ec;
	std::filesystem::remove_all(g_userPath / "shadercache", ec);
	ShaderCache::SetRecording(true);
	const auto coldStart = std::chrono::steady_clock::now();
	InitWindow();
	IRenderer* gfx = CreateRenderer();
	gfx->Init();
	DiligentGfx* dg = dynamic_cast<DiligentGfx*>(gfx);
	if (!dg) {
		printf("UNITTEST FAILED (not the Diligent renderer)\n");
		return;
	}
	IRenderDevice* device = dg->device;
	dg->PreparePostProcessing();
	{
		DiligentFsr3 fsr3(device);
	}
	const double coldStartMs = Ms(coldStart);
	ShaderCache::SetRecording(false);
	const ShaderCache::Stats cold = ShaderCache::GetStats();
	stats("cold start", cold);
	const int requests = (int)ShaderCache::RecordedCount();
	const int stored = CountEntries();
	check(requests > 20 && cold.requests == requests, "the start made " + std::to_string(requests) + " shaders");
	check(!cold.rebuilt, "an empty folder is not a rebuild");
	check(cold.loaded + cold.compiled == requests && cold.compiled > 0, "cold start: " + std::to_string(cold.compiled) + " compiled (the rest repeats)");
	check(stored == cold.compiled, "cold start fills the cache: " + std::to_string(stored) + " entries for " + std::to_string(cold.compiled) + " compiled");
	check(std::filesystem::exists(ShaderCache::Directory() / "adapter.txt"), "adapter.txt written");

	// a warm start: everything from the cache
	ShaderCache::ResetStats();
	auto t = std::chrono::steady_clock::now();
	int made = ShaderCache::Replay();
	const double warmMs = Ms(t);
	const ShaderCache::Stats warm = ShaderCache::GetStats();
	stats("warm start", warm);
	check(warm.compiled == 0 && warm.loaded == requests, "warm start compiles 0 shaders");
	check(made == requests - cold.failed, "warm start: every shader made (" + std::to_string(made) + ")");

	// one shader edited: only that one compiled
	int touched = 0;
	ShaderCache::ResetStats();
	ShaderCache::Replay(touched);
	const ShaderCache::Stats one = ShaderCache::GetStats();
	stats("one shader edited", one);
	check(one.compiled == 1 && one.loaded == requests - 1, "touching one shader recompiles only that shader");
	check(CountEntries() == stored + 1, "its new entry is stored beside the old one");

	// an included file changed: only the shader that includes it
	{
		const char* mainA = "#include \"common.hlsli\"\nfloat4 PS(float4 p : SV_POSITION) : SV_TARGET { return Tint(); }\n";
		const char* mainB = "float4 PS(float4 p : SV_POSITION) : SV_TARGET { return float4(0, 1, 0, 1); }\n";
		auto make = [&](const char* common) {
			const MemoryShaderSourceFileInfo files[] = { { "a.hlsl", mainA }, { "b.hlsl", mainB }, { "common.hlsli", common } };
			MemoryShaderSourceFactoryCreateInfo fci{ files, (Uint32)std::size(files), true };
			RefCntAutoPtr<IShaderSourceInputStreamFactory> factory;
			CreateMemoryShaderSourceFactory(fci, &factory);
			for (const char* file : { "a.hlsl", "b.hlsl" }) {
				ShaderCreateInfo sci;
				sci.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
				sci.FilePath = file;
				sci.pShaderSourceStreamFactory = factory;
				sci.EntryPoint = "PS";
				sci.Desc.ShaderType = SHADER_TYPE_PIXEL;
				sci.Desc.Name = file;
				RefCntAutoPtr<IShader> s;
				ShaderCache::CreateShader(device, sci, &s);
			}
		};
		make("float4 Tint() { return float4(1, 0, 0, 1); }\n");
		ShaderCache::ResetStats();
		make("float4 Tint() { return float4(0, 0, 1, 1); }\n");
		const ShaderCache::Stats inc = ShaderCache::GetStats();
		stats("an included file changed", inc);
		check(inc.compiled == 1 && inc.loaded == 1, "an included file changed recompiles only its includer");
	}

	// damaged entries: one cut short, one overwritten with rubbish
	{
		std::vector<std::filesystem::path> entries = Entries();
		std::sort(entries.begin(), entries.end());
		std::vector<std::filesystem::path> damaged;
		for (const auto& p : entries) {
			if (damaged.size() == 2)
				break;
			if (std::filesystem::file_size(p, ec) > 64)
				damaged.push_back(p);
		}
		if (damaged.size() == 2) {
			std::filesystem::resize_file(damaged[0], std::filesystem::file_size(damaged[0]) / 2, ec);
			std::ofstream(damaged[1], std::ios::binary | std::ios::trunc) << "not a shader cache entry";
		}
		ShaderCache::ResetStats();
		made = ShaderCache::Replay();
		const ShaderCache::Stats bad = ShaderCache::GetStats();
		stats("two damaged entries", bad);
		// (an entry may belong to the touched shader or the include test, which the replay doesn't make)
		check(bad.corrupt == bad.compiled && bad.compiled <= 2 && bad.loaded + bad.compiled == requests, "a damaged entry is a miss, compiled again (" + std::to_string(bad.compiled) + ")");
		check(made == requests - cold.failed, "and every shader still made");
		ShaderCache::ResetStats();
		ShaderCache::Replay();
		check(ShaderCache::GetStats().compiled == 0, "the damaged entries were rewritten");
	}

	// a pretend driver update: the whole cache dropped and rebuilt
	ShaderCache::ReopenForTest("driver 999.0.0.0 (the shader cache test)\n");
	check(ShaderCache::GetStats().rebuilt, "a changed driver is seen: " + ShaderCache::GetStats().reason);
	check(CountEntries() == 0, "every entry dropped");
	ShaderCache::ResetStats();
	t = std::chrono::steady_clock::now();
	made = ShaderCache::Replay();
	const double rebuildMs = Ms(t);
	const ShaderCache::Stats rebuilt = ShaderCache::GetStats();
	stats("after the driver update", rebuilt);
	check(rebuilt.compiled == cold.compiled && rebuilt.loaded == cold.loaded, "a faked driver change rebuilds everything");
	// and the real adapter again
	ShaderCache::ReopenForTest("");
	check(ShaderCache::GetStats().rebuilt, "the real driver again is a change too");
	ShaderCache::ResetStats();
	ShaderCache::Replay();
	check(CountEntries() == stored, "refilled: " + std::to_string(CountEntries()) + " entries");

	// the size cap: the least recently used go
	{
		uint64_t total = 0;
		for (const auto& p : Entries())
			total += std::filesystem::file_size(p, ec);
		ShaderCache::SetSizeCapForTest(total / 2);
		const int left = ShaderCache::PruneNow();
		uint64_t after = 0;
		for (const auto& p : Entries())
			after += std::filesystem::file_size(p, ec);
		check(left < stored && left > 0 && after <= total / 2, "the size cap: " + std::to_string(left) + " of " + std::to_string(stored) + " entries kept, " + std::to_string(after / 1024) + " of " + std::to_string(total / 1024) + " KB");
		ShaderCache::SetSizeCapForTest(256ull << 20);
		ShaderCache::ResetStats();
		ShaderCache::Replay();
		check(ShaderCache::GetStats().compiled == stored - left, "the pruned ones are compiled again when wanted");
	}

	printf("Shader cache: the start's shaders took %.0f ms to compile (cold, after a driver update: %.0f ms) and %.0f ms from the cache (warm): %.0f ms saved (the whole cold start: %.0f ms)\n",
		cold.compileMs + cold.loadMs, rebuildMs, warmMs, rebuildMs - warmMs, coldStartMs);
	printf(failures ? "UNITTEST FAILED (%d)\n" : "UNITTEST PASSED\n", failures);
	fflush(stdout);
}
