// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#include "FrameStats.h"
#include "../settings.h"
#include "../window.h"
#include "../file.h"
#include <SDL3/SDL_timer.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <string>

FrameStats g_frameStats;

void FrameStats::frameEnd()
{
	uint64_t now = SDL_GetTicksNS();
	if (lastTicksNS != 0) {
		float ms = (float)((double)(now - lastTicksNS) / 1.0e6);
		if (window == 0 || times.size() < window)
			times.push_back(ms);
		else
			times[next] = ms;
		if (window != 0)
			next = (next + 1) % window;
	}
	lastTicksNS = now;
}

void FrameStats::reset()
{
	times.clear();
	next = 0;
	lastTicksNS = 0;
}

FrameStats::Summary FrameStats::summarize() const
{
	Summary s;
	s.frames = times.size();
	if (times.empty())
		return s;
	std::vector<float> sorted = times;
	std::sort(sorted.begin(), sorted.end());
	double sum = 0;
	for (float t : sorted)
		sum += t;
	auto percentile = [&sorted](double p) {
		size_t i = (size_t)(p * (double)(sorted.size() - 1) + 0.5);
		return (double)sorted[std::min(i, sorted.size() - 1)];
	};
	s.avgMs = sum / (double)sorted.size();
	s.p50Ms = percentile(0.50);
	s.p95Ms = percentile(0.95);
	s.p99Ms = percentile(0.99);
	s.maxMs = sorted.back();
	return s;
}

static const nlohmann::json* BenchmarkSettings()
{
	auto it = g_settings.find("benchmark");
	if (it == g_settings.end() || !it->is_object())
		return nullptr;
	return &*it;
}

bool BenchmarkEnabled()
{
	return BenchmarkSettings() != nullptr;
}

bool BenchmarkPaused()
{
	const auto* bm = BenchmarkSettings();
	return bm && bm->value("paused", false);
}

const char* BenchmarkSaveGame()
{
	static std::string saveGame;
	if (const auto* bm = BenchmarkSettings())
		saveGame = bm->value("saveGame", "");
	return saveGame.c_str();
}

void BenchmarkFrameEnd(bool worldLoaded)
{
	const auto* bm = BenchmarkSettings();
	if (!bm || !worldLoaded)
		return;
	static int frame = 0;
	static FrameStats recorded(0);
	const int warmup = bm->value("warmupFrames", 120);
	const int frames = bm->value("frames", 600);
	++frame;
	if (frame <= warmup)
		return;
	recorded.frameEnd();
	if ((int)recorded.frameTimes().size() < frames)
		return;

	const FrameStats::Summary s = recorded.summarize();
	const double budget = g_settings.value("frameBudgetMs", 1000.0 / 60.0);
	nlohmann::json report;
	report["saveGame"] = bm->value("saveGame", "");
	report["renderer"] = g_settings.value("renderer", "");
	report["enhancedGraphics"] = g_settings.value("enhancedGraphics", false);
	report["msaaNumSamples"] = g_settings.value("msaaNumSamples", 1);
	report["vsync"] = g_settings.value("vsync", true);
	report["windowWidth"] = g_windowWidth;
	report["windowHeight"] = g_windowHeight;
	report["warmupFrames"] = warmup;
	report["frames"] = s.frames;
	report["avgMs"] = s.avgMs;
	report["p50Ms"] = s.p50Ms;
	report["p95Ms"] = s.p95Ms;
	report["p99Ms"] = s.p99Ms;
	report["maxMs"] = s.maxMs;
	report["avgFps"] = s.avgMs > 0 ? 1000.0 / s.avgMs : 0.0;
	report["frameBudgetMs"] = budget;
	report["p99WithinBudget"] = s.p99Ms <= budget;
	report["frameTimesMs"] = recorded.frameTimes();

	const std::string path = bm->value("report", "benchmark.json");
	std::ofstream out(PathFromUtf8(path));
	out << report.dump(1) << '\n';
	printf("Benchmark: %zu frames, avg %.2f ms, p50 %.2f, p95 %.2f, p99 %.2f, max %.2f; report written to %s\n",
		s.frames, s.avgMs, s.p50Ms, s.p95Ms, s.p99Ms, s.maxMs, path.c_str());
	g_windowQuit = true;
}
