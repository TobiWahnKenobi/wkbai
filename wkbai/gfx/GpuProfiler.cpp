// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#include "GpuProfiler.h"
#include <algorithm>
#include <chrono>
#include <map>

namespace {
	using Clock = std::chrono::steady_clock;

	// a scope as recorded in a frame: its path, depth, the stamps' indices, the CPU's time
	struct Record {
		std::string path;
		int depth;
		uint32_t begin, end;
		double cpuMs;
	};
	struct FrameRecords {
		std::vector<Record> records;
		uint32_t stamps = 0;
		uint64_t frame = 0;
		bool pending = false;
	};

	GpuTimestampSource* g_source = nullptr;
	FrameRecords g_frames[PROFILER_FRAMES];
	uint64_t g_frame = 0;
	FrameRecords* g_current = nullptr;
	struct Open { size_t record; Clock::time_point start; };
	std::vector<Open> g_stack;
	std::string g_path;

	std::vector<ProfileEntry> g_entries;
	std::map<std::string, size_t> g_index;
	std::vector<float> g_cpuHistory, g_gpuHistory;
	ProfileFrame g_slowest;
	constexpr size_t HISTORY = 300;

	ProfileEntry& entry(const std::string& path, int depth)
	{
		auto it = g_index.find(path);
		if (it != g_index.end())
			return g_entries[it->second];
		// in the order first seen (the frame's order), a part after its parent
		g_index[path] = g_entries.size();
		g_entries.push_back({});
		g_entries.back().name = path;
		g_entries.back().depth = depth;
		return g_entries.back();
	}

	void push(std::vector<float>& h, float v)
	{
		h.push_back(v);
		if (h.size() > HISTORY)
			h.erase(h.begin(), h.begin() + (h.size() - HISTORY));
	}

	// a finished frame's times into the entries (GPU: from the stamps, or unknown)
	void accumulate(FrameRecords& f, const std::vector<double>* stamps)
	{
		std::map<std::string, std::pair<double, double>> sums; // path -> gpu, cpu
		std::map<std::string, int> calls, depths;
		for (const Record& r : f.records) {
			double gpu = -1.0;
			if (stamps && r.end < stamps->size() && r.begin < stamps->size())
				gpu = std::max(0.0, (*stamps)[r.end] - (*stamps)[r.begin]);
			auto& s = sums[r.path];
			s.first += gpu < 0 ? 0 : gpu;
			s.second += r.cpuMs;
			calls[r.path]++;
			depths[r.path] = r.depth;
		}
		double gpuFrame = 0.0;
		for (auto& [path, s] : sums) {
			ProfileEntry& e = entry(path, depths[path]);
			e.gpuMs = stamps ? s.first : -1.0;
			e.cpuMs = s.second;
			e.calls = calls[path];
			const double k = 0.08;
			if (stamps)
				e.gpuAvg += (s.first - e.gpuAvg) * k;
			e.cpuAvg += (s.second - e.cpuAvg) * k;
			if (stamps) {
				if (f.frame - e.lastFrame > 120 || s.first > e.gpuPeak)
					e.gpuPeak = s.first;
				else
					e.gpuPeak = std::max(s.first, e.gpuPeak * 0.995);
				e.gpuTotal += s.first;
				e.gpuFrames++;
			}
			e.cpuTotal += s.second;
			e.frames++;
			e.lastFrame = f.frame;
			if (e.depth == 0 && stamps)
				gpuFrame += s.first;
		}
		// the slowest frame on the CPU, its passes
		double cpuFrame = 0.0;
		for (auto& [path, s] : sums)
			if (depths[path] == 0)
				cpuFrame += s.second;
		if (cpuFrame > g_slowest.cpuMs) {
			g_slowest.frame = f.frame;
			g_slowest.cpuMs = cpuFrame;
			g_slowest.passes.clear();
			for (auto& [path, s] : sums)
				g_slowest.passes.push_back({ path, { s.second, stamps ? s.first : -1.0 } });
		}
		// entries not in this frame: their last times stay, marked by lastFrame
		if (stamps)
			push(g_gpuHistory, (float)gpuFrame);
	}
}

void ProfilerSetGpuSource(GpuTimestampSource* source)
{
	g_source = source;
}

void ProfilerBeginFrame(uint64_t frame, double frameCpuMs)
{
	if (frameCpuMs > 0)
		push(g_cpuHistory, (float)frameCpuMs);
	// the frames whose stamps are in (each slot is read before it is used again)
	std::vector<double> stamps;
	for (uint32_t i = 0; i < PROFILER_FRAMES; i++) {
		FrameRecords& f = g_frames[i];
		if (!f.pending)
			continue;
		const bool reuse = (frame % PROFILER_FRAMES) == i;
		if (g_source && f.stamps && g_source->read(i, f.stamps, stamps)) {
			accumulate(f, &stamps);
			f.pending = false;
		}
		else if (reuse || !g_source || !f.stamps) {
			accumulate(f, nullptr); // (the GPU's not in time: the CPU's alone)
			f.pending = false;
		}
	}
	g_frame = frame;
	g_current = &g_frames[frame % PROFILER_FRAMES];
	g_current->records.clear();
	g_current->stamps = 0;
	g_current->frame = frame;
	g_current->pending = true;
	g_stack.clear();
	g_path.clear();
}

void ProfilerBegin(const char* name)
{
	if (!g_current)
		return;
	Record r;
	r.depth = (int)g_stack.size();
	r.path = g_path.empty() ? std::string(name) : g_path + "/" + name;
	r.begin = g_current->stamps++;
	r.end = 0;
	r.cpuMs = 0.0;
	if (g_source)
		g_source->stamp(r.begin);
	g_current->records.push_back(r);
	g_stack.push_back({ g_current->records.size() - 1, Clock::now() });
	g_path = r.path;
}

void ProfilerEnd()
{
	if (!g_current || g_stack.empty())
		return;
	const Open o = g_stack.back();
	g_stack.pop_back();
	Record& r = g_current->records[o.record];
	r.end = g_current->stamps++;
	if (g_source)
		g_source->stamp(r.end);
	r.cpuMs = std::chrono::duration<double, std::milli>(Clock::now() - o.start).count();
	const size_t slash = r.path.find_last_of('/');
	g_path = slash == std::string::npos ? std::string() : r.path.substr(0, slash);
}

const std::vector<ProfileEntry>& ProfilerEntries() { return g_entries; }
const std::vector<float>& ProfilerCpuHistory() { return g_cpuHistory; }
const std::vector<float>& ProfilerGpuHistory() { return g_gpuHistory; }
const ProfileFrame& ProfilerSlowestFrame() { return g_slowest; }

void ProfilerResetTotals()
{
	for (ProfileEntry& e : g_entries) {
		e.gpuTotal = e.cpuTotal = 0.0;
		e.frames = e.gpuFrames = 0;
	}
	g_slowest = ProfileFrame();
}
