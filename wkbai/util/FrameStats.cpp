// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#include "FrameStats.h"
#include "../gfx/GpuProfiler.h"
#include "../settings.h"
#include "../window.h"
#include "../file.h"
#include "../server.h"
#include "../Environment.h"
#include <SDL3/SDL_timer.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
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
double g_gpuFrameMs = -1.0;
double g_giGpuMs = -1.0;

bool BenchmarkCamera(Vector3& position, Vector3& orientation)
{
	const auto* bm = BenchmarkSettings();
	// "cameras": [[x, y, z, pitch, yaw], ...], each held for "cameraHold" frames after the
	// warm-up (one screenshot each, name_0.bmp, name_1.bmp, ...: the HD model showcase)
	if (bm && bm->contains("cameras") && bm->at("cameras").is_array() && !bm->at("cameras").empty()) {
		const auto& list = bm->at("cameras");
		const int hold = std::max(1, bm->value("cameraHold", 30));
		const int k = std::clamp((s_benchmarkFrame - bm->value("warmupFrames", 120)) / hold, 0, (int)list.size() - 1);
		const auto& c = list[k];
		if (!c.is_array() || c.size() != 5)
			return false;
		position = Vector3(c[0].get<float>(), c[1].get<float>(), c[2].get<float>());
		orientation = Vector3(c[3].get<float>(), c[4].get<float>(), 0.0f);
		return true;
	}
	if (!bm || !bm->contains("camera"))
		return false;
	const auto& c = bm->at("camera");
	if (!c.is_array() || c.size() != 5)
		return false;
	position = Vector3(c[0].get<float>(), c[1].get<float>(), c[2].get<float>());
	orientation = Vector3(c[3].get<float>(), c[4].get<float>(), 0.0f);
	// "cameraAtMoon": true: the pitch and yaw towards the moon (wkbai's day and night; the
	// camera's own pitch and yaw added: the moon off the middle of the picture)
	if (bm->value("cameraAtMoon", false) && g_environment.active) {
		const Vector3& m = g_environment.moonWorld;
		orientation += Vector3(std::asin(std::clamp(m.y, -1.0f, 1.0f)), std::atan2(-m.x, m.z), 0.0f);
	}
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
	const bool list = bm && bm->contains("cameras") && bm->at("cameras").is_array();
	const int count = list ? (int)bm->at("cameras").size() : bm ? bm->value("screenshotCount", 1) : 1;
	// with a camera list: the last frame each camera is held
	const int due = !bm ? 0 : list ? bm->value("warmupFrames", 120) + (taken + 1) * std::max(1, bm->value("cameraHold", 30)) - 1
	                              : bm->value("warmupFrames", 120);
	if (taken >= count || !bm || !bm->contains("screenshot") || s_benchmarkFrame < due)
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
	// the game clock over the recorded frames, against the real one: whether the simulation (on
	// its own thread) keeps up with the game speed while this is drawn
	static float gameStart = 0.0f;
	static uint64_t realStart = 0;
	if (frame <= warmup) {
		if (frame == warmup)
			ProfilerResetTotals(); // the per-pass times over the recorded frames only
		if (frame == warmup && Server::instance) {
			gameStart = Server::instance->timeManager.currentTime;
			realStart = SDL_GetTicks();
		}
		return;
	}
	recorded.frameEnd();
	static std::vector<float> gpuTimes, giTimes;
	if (g_gpuFrameMs >= 0.0)
		gpuTimes.push_back((float)g_gpuFrameMs);
	if (g_giGpuMs >= 0.0)
		giTimes.push_back((float)g_giGpuMs);
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
	if (Server::instance && realStart) {
		const double real = (SDL_GetTicks() - realStart) / 1000.0;
		report["gameStartTime"] = gameStart;
		report["gameSeconds"] = Server::instance->timeManager.currentTime - gameStart;
		report["realSeconds"] = real;
		report["gameSpeed"] = real > 0.0 ? (Server::instance->timeManager.currentTime - gameStart) / real : 0.0;
		report["timeSpeed"] = Server::instance->timeManager.timeSpeed;
	}
	if (!giTimes.empty()) {
		double sum = 0;
		for (float t : giTimes) sum += t;
		report["giGpuAvgMs"] = sum / giTimes.size();
		printf("Benchmark: dynamic GI passes avg %.3f ms\n", sum / giTimes.size());
	}
	if (!gpuTimes.empty()) {
		std::vector<float> g = gpuTimes;
		std::sort(g.begin(), g.end());
		double sum = 0;
		for (float t : g) sum += t;
		report["gpuAvgMs"] = sum / g.size();
		report["gpuP50Ms"] = g[g.size() / 2];
		report["gpuP95Ms"] = g[std::min(g.size() - 1, (size_t)(g.size() * 0.95))];
		printf("Benchmark: GPU avg %.3f ms, p50 %.3f, p95 %.3f\n", sum / g.size(), g[g.size() / 2], g[std::min(g.size() - 1, (size_t)(g.size() * 0.95))]);
	}
	// per pass of the frame (GpuProfiler): the average over the recorded frames, GPU and CPU
	{
		nlohmann::json gpuPasses = nlohmann::json::object(), cpuPasses = nlohmann::json::object();
		for (const ProfileEntry& e : ProfilerEntries()) {
			if (e.gpuFrames > 0)
				gpuPasses[e.name] = e.gpuTotal / e.gpuFrames;
			if (e.frames > 0)
				cpuPasses[e.name] = e.cpuTotal / e.frames;
		}
		report["gpuPassesMs"] = gpuPasses;
		report["cpuPassesMs"] = cpuPasses;
	}
	report["frameTimesMs"] = recorded.frameTimes();

	const std::string path = bm->value("report", "benchmark.json");
	std::ofstream out(PathFromUtf8(path));
	out << report.dump(1) << '\n';
	printf("Benchmark: %zu frames, avg %.2f ms, p50 %.2f, p95 %.2f, p99 %.2f, max %.2f; report written to %s\n",
		s.frames, s.avgMs, s.p50Ms, s.p95Ms, s.p99Ms, s.maxMs, path.c_str());
	g_windowQuit = true;
}
