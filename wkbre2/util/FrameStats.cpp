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

static int s_benchmarkFrame = 0; // frames since the map was loaded

bool BenchmarkCamera(Vector3& position, Vector3& orientation)
{
	const auto* bm = BenchmarkSettings();
	if (!bm || !bm->contains("camera"))
		return false;
	const auto& c = bm->at("camera");
	if (!c.is_array() || c.size() != 5)
		return false;
	position = Vector3(c[0].get<float>(), c[1].get<float>(), c[2].get<float>());
	orientation = Vector3(c[3].get<float>(), c[4].get<float>(), 0.0f);
	// a camera moving by this much per frame (x, y, z, pitch, yaw): motion for the temporal
	// anti-aliasing and upscaling
	if (bm->contains("cameraMove")) {
		const auto& m = bm->at("cameraMove");
		if (m.is_array() && m.size() == 5) {
			const float f = (float)s_benchmarkFrame;
			position += Vector3(m[0].get<float>(), m[1].get<float>(), m[2].get<float>()) * f;
			orientation += Vector3(m[3].get<float>(), m[4].get<float>(), 0.0f) * f;
		}
	}
	return true;
}

const char* BenchmarkScreenshotDue()
{
	// after the warm-up, one screenshot, or screenshotCount of the frames in a row (name_0.bmp,
	// name_1.bmp, ...: the frame-to-frame flicker of the temporal anti-aliasing)
	static int taken = 0;
	static std::string path;
	const auto* bm = BenchmarkSettings();
	const int count = bm ? bm->value("screenshotCount", 1) : 1;
	if (taken >= count || !bm || !bm->contains("screenshot") || s_benchmarkFrame < bm->value("warmupFrames", 120))
		return nullptr;
	path = bm->value("screenshot", "");
	if (count > 1) {
		const size_t dot = path.find_last_of('.');
		path.insert(dot == std::string::npos ? path.size() : dot, "_" + std::to_string(taken));
	}
	taken++;
	return path.c_str();
}

void BenchmarkFrameEnd(bool worldLoaded)
{
	const auto* bm = BenchmarkSettings();
	if (!bm || !worldLoaded)
		return;
	int& frame = s_benchmarkFrame;
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
	report["diligentBackend"] = g_settings.value("diligentBackend", "d3d12");
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
