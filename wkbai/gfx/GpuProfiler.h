// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#pragma once

// Per-pass frame timings: named scopes around the frame's passes (nested scopes are their
// parts), timed on the CPU and, with a renderer that has timestamp queries (the Diligent one),
// on the GPU. The GPU's times come back a few frames later, without waiting. Shown in the
// frame-time window (Ctrl+F12) and in the benchmark report ("gpuPassesMs", "cpuPassesMs").

#include <cstdint>
#include <string>
#include <vector>

struct ProfileEntry {
	std::string name;     // the scope's path ("Terrain/Water")
	int depth = 0;        // how deep it is nested (0: a pass of the frame)
	double gpuMs = 0.0;   // the last frame's (all its calls), -1 unknown
	double cpuMs = 0.0;
	double gpuAvg = 0.0, cpuAvg = 0.0; // smoothed over about a second
	double gpuPeak = 0.0;              // the highest of the last 2 seconds
	int calls = 0;
	// the benchmark's: summed over the frames recorded since ProfilerResetTotals
	double gpuTotal = 0.0, cpuTotal = 0.0;
	int frames = 0, gpuFrames = 0;
	uint64_t lastFrame = 0;
};

// The renderer's GPU timestamps (set by the Diligent renderer): begin and end write a timestamp
// for the scope's slot in this frame; the profiler reads them back with readGpu.
struct GpuTimestampSource {
	virtual ~GpuTimestampSource() {}
	// a timestamp now for this frame's query `index` (it makes more as needed)
	virtual void stamp(uint32_t index) = 0;
	// the frame `frameSlot` (frame % ProfilerFrames) stamps, if the GPU has them: in ms from the
	// first; false while not ready
	virtual bool read(uint32_t frameSlot, uint32_t count, std::vector<double>& ms) = 0;
};

constexpr uint32_t PROFILER_FRAMES = 5; // how many frames the GPU's stamps may lag

void ProfilerSetGpuSource(GpuTimestampSource* source);
// once a frame, before its first scope and after the last frame's present: reads the frames
// whose stamps are in, starts this frame's (frame: the renderer's frame index)
void ProfilerBeginFrame(uint64_t frame, double frameCpuMs);
void ProfilerBegin(const char* name);
void ProfilerEnd();
const std::vector<ProfileEntry>& ProfilerEntries();
// the frame times of the last frames (ms): the whole frame's on the CPU, and the GPU's sum of
// the passes (and the renderer's own whole-frame time, g_gpuFrameMs)
const std::vector<float>& ProfilerCpuHistory();
const std::vector<float>& ProfilerGpuHistory();
void ProfilerResetTotals();

struct ProfileScope {
	explicit ProfileScope(const char* name) { ProfilerBegin(name); }
	~ProfileScope() { ProfilerEnd(); }
	ProfileScope(const ProfileScope&) = delete;
	ProfileScope& operator=(const ProfileScope&) = delete;
};
#define PROFILE_CONCAT2(a, b) a##b
#define PROFILE_CONCAT(a, b) PROFILE_CONCAT2(a, b)
#define PROFILE_SCOPE(name) ProfileScope PROFILE_CONCAT(profileScope_, __LINE__)(name)
