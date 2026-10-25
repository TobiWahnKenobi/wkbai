// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

// Frame times of the last frames (CPU wall time between two presented frames),
// used for the debug overlay and the benchmark report.
struct FrameStats {
	struct Summary {
		size_t frames = 0;
		double avgMs = 0, p50Ms = 0, p95Ms = 0, p99Ms = 0, maxMs = 0;
	};

	explicit FrameStats(size_t window = 300) : window(window) {}
	void frameEnd();
	void reset();
	Summary summarize() const;
	const std::vector<float>& frameTimes() const { return times; }

	size_t window; // 0 = keep every frame
private:
	std::vector<float> times;
	size_t next = 0;
	uint64_t lastTicksNS = 0;
};

extern FrameStats g_frameStats;

// Benchmark mode, enabled by the "benchmark" object in wkconfig.json:
//   "benchmark": { "saveGame": "<file in Save_Games>", "warmupFrames": 120, "frames": 600,
//                  "report": "benchmark.json" }
// The game loads the save game, skips the warm-up frames (counted once the map is loaded),
// records the frame times and writes the report, then quits. "frameBudgetMs" (default
// 16.667) sets the budget the report is checked against. With "paused": true the game clock
// stays at the save game's start, so every run renders the same frame (for comparing renderers).
bool BenchmarkEnabled();
bool BenchmarkPaused();
const char* BenchmarkSaveGame();
void BenchmarkFrameEnd(bool worldLoaded);
