// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#include <cstdio>
#include <charconv>
#include <chrono>
#include <thread>
#include <utility>

#ifndef _WIN32 // temp
void _sleep(int duration) {std::this_thread::sleep_for(std::chrono::milliseconds(duration));}
void Sleep(int duration) {_sleep(duration);}
void gets_s(char* destination, size_t len) {fgets(destination, len, stdin);}
void __debugbreak() {}
char* itoa(int num, char* buffer, int base) {std::to_chars(buffer, buffer+10, num, base);}
#endif

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#include <string>
#include "platform.h"
#include "util/ScriptProfile.h"

HiddenChild PopenHidden(const char* exePath)
{
	HiddenChild child;
	SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
	HANDLE readEnd = nullptr, writeEnd = nullptr;
	if (!CreatePipe(&readEnd, &writeEnd, &sa, 0))
		return child;
	SetHandleInformation(readEnd, HANDLE_FLAG_INHERIT, 0); // only the child's end is inherited
	HANDLE nul = CreateFileA("NUL", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
	STARTUPINFOA si{};
	si.cb = sizeof(si);
	si.dwFlags = STARTF_USESTDHANDLES;
	si.hStdInput = nul;
	si.hStdOutput = writeEnd;
	si.hStdError = nul;
	PROCESS_INFORMATION pi{};
	std::string commandLine = std::string("\"") + exePath + "\"";
	// the environment (WKB_TEST and the like, set with _putenv_s) inherited
	const BOOL started = CreateProcessA(exePath, commandLine.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
	CloseHandle(writeEnd); // else the read never ends
	if (nul != INVALID_HANDLE_VALUE)
		CloseHandle(nul);
	if (!started) {
		CloseHandle(readEnd);
		return child;
	}
	CloseHandle(pi.hThread);
	child.out = _fdopen(_open_osfhandle((intptr_t)readEnd, _O_RDONLY | _O_TEXT), "r");
	child.process = pi.hProcess;
	return child;
}

int PcloseHidden(HiddenChild& child)
{
	if (child.out)
		fclose(child.out); // closes the pipe's handle too
	DWORD code = (DWORD)-1;
	if (child.process) {
		WaitForSingleObject((HANDLE)child.process, INFINITE);
		GetExitCodeProcess((HANDLE)child.process, &code);
		CloseHandle((HANDLE)child.process);
	}
	child = {};
	return (int)code;
}
#endif

#ifdef _WIN32
unsigned long long ThreadCycles()
{
	ULONG64 c = 0;
	QueryThreadCycleTime(GetCurrentThread(), &c);
	return c;
}
#else
#include <ctime>
unsigned long long ThreadCycles()
{
	timespec t{};
	clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t);
	return (unsigned long long)t.tv_sec * 1000000000ull + t.tv_nsec;
}
#endif

// A sampling profiler for the tests (Windows): a thread that stops the given one about 1000 times
// a second and notes the functions on its stack (by the PDB). SamplerStop: the top functions by
// samples with them on the stack (inclusive) and on top (exclusive).
#ifdef _WIN32
#include <dbghelp.h>
#include <atomic>
#include <map>
#include <vector>
#include <algorithm>
#pragma comment(lib, "dbghelp.lib")
#pragma comment(lib, "winmm.lib")
extern "C" __declspec(dllimport) unsigned int __stdcall timeBeginPeriod(unsigned int);
extern "C" __declspec(dllimport) unsigned int __stdcall timeEndPeriod(unsigned int);
namespace {
	std::atomic<bool> g_samplerRun{ false };
	std::thread g_samplerThread;
	std::vector<SamplerSample> g_samples;
	DWORD g_samplerThreadId = 0;
	// the samples' frames (by the unwind tables; 128 deep: a script's tree nests deep)
	constexpr int SAMPLER_FRAMES = 128;
}
ScriptMarks g_scriptMarks;
bool g_scriptMarksOn = false;
bool ScriptMarksThisThread() { return GetCurrentThreadId() == g_samplerThreadId; }
const std::vector<SamplerSample>& SamplerSamples() { return g_samples; }
void SamplerStart(bool scripts)
{
	if (g_samplerRun) return;
	HANDLE target = nullptr;
	DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &target, THREAD_ALL_ACCESS, FALSE, 0);
	g_samples.clear();
	g_samplerThreadId = GetCurrentThreadId();
	g_scriptMarks.depth = 0;
	g_scriptMarksOn = scripts;
	g_samplerRun = true;
	timeBeginPeriod(1);
	g_samplerThread = std::thread([target, scripts]() {
		HANDLE process = GetCurrentProcess();
		extern std::atomic<const char*> g_simPhase;
		while (g_samplerRun) {
			Sleep(1);
			if (SuspendThread(target) == (DWORD)-1) continue;
			CONTEXT c{};
			c.ContextFlags = CONTEXT_FULL;
			// (nothing allocated while it is stopped: it may hold the heap's lock)
			DWORD64 frames[SAMPLER_FRAMES];
			ScriptMarks::Entry marks[ScriptMarks::MAX];
			int n = 0, m = 0;
			const char* phase = nullptr;
			if (GetThreadContext(target, &c)) {
				for (; n < SAMPLER_FRAMES && c.Rip; n++) {
					frames[n] = c.Rip;
					DWORD64 base = 0;
					PRUNTIME_FUNCTION f = RtlLookupFunctionEntry(c.Rip, &base, nullptr);
					if (!f) { c.Rip = *(DWORD64*)c.Rsp; c.Rsp += 8; continue; }
					void* handler = nullptr; DWORD64 frame = 0;
					RtlVirtualUnwind(UNW_FLAG_NHANDLER, base, c.Rip, f, &c, &handler, &frame, nullptr);
				}
				if (scripts) {
					phase = g_simPhase.load(std::memory_order_relaxed);
					m = std::min(g_scriptMarks.depth, ScriptMarks::MAX);
					for (int i = 0; i < m; i++) marks[i] = g_scriptMarks.e[i];
				}
			}
			ResumeThread(target);
			if (n) {
				SamplerSample s;
				s.frames.assign(frames, frames + n);
				s.phase = phase;
				s.marks.resize(m);
				for (int i = 0; i < m; i++) s.marks[i] = { marks[i].p, marks[i].kind };
				g_samples.push_back(std::move(s));
			}
		}
		CloseHandle(target);
		(void)process;
	});
}
// stops the sampling thread (once)
void SamplerHalt()
{
	if (!g_samplerRun) return;
	g_samplerRun = false;
	g_samplerThread.join();
	g_scriptMarksOn = false;
	timeEndPeriod(1);
}
std::string SamplerStop(int top)
{
	if (!g_samplerRun && g_samples.empty()) return {};
	SamplerHalt();
	HANDLE process = GetCurrentProcess();
	SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
	SymInitialize(process, nullptr, TRUE);
	std::map<DWORD64, std::string> names;
	auto nameOf = [&](DWORD64 a) -> const std::string& {
		auto it = names.find(a);
		if (it != names.end()) return it->second;
		char buf[sizeof(SYMBOL_INFO) + 512] = {};
		SYMBOL_INFO* sym = (SYMBOL_INFO*)buf;
		sym->SizeOfStruct = sizeof(SYMBOL_INFO);
		sym->MaxNameLen = 511;
		DWORD64 disp = 0;
		std::string n = SymFromAddr(process, a, &disp, sym) ? std::string(sym->Name) : "?";
		return names[a] = n;
	};
	std::map<std::string, int> incl, excl;
	for (const auto& sample : g_samples) {
		const auto& st = sample.frames;
		std::vector<const std::string*> seen;
		for (size_t i = 0; i < st.size(); i++) {
			const std::string& n = nameOf(st[i]);
			if (i == 0) excl[n]++;
			if (std::find(seen.begin(), seen.end(), &n) == seen.end()) { seen.push_back(&n); incl[n]++; }
		}
	}
	SymCleanup(process);
	auto list = [&](std::map<std::string, int>& m, const char* title) {
		std::vector<std::pair<int, std::string>> v;
		for (auto& [n, c] : m) v.emplace_back(c, n);
		std::sort(v.rbegin(), v.rend());
		std::string s = std::string(title) + ":\n";
		char buf[600];
		for (int i = 0; i < top && i < (int)v.size(); i++) {
			snprintf(buf, sizeof(buf), "  %5.1f%%  %s\n", 100.0 * v[i].first / std::max<size_t>(1, g_samples.size()), v[i].second.c_str());
			s += buf;
		}
		return s;
	};
	char head[100];
	snprintf(head, sizeof(head), "%zu samples\n", g_samples.size());
	return head + list(incl, "on the stack") + list(excl, "on top");
}
#else
ScriptMarks g_scriptMarks;
bool g_scriptMarksOn = false;
bool ScriptMarksThisThread() { return false; }
namespace { std::vector<SamplerSample> g_samples; }
const std::vector<SamplerSample>& SamplerSamples() { return g_samples; }
void SamplerStart(bool) {}
void SamplerHalt() {}
std::string SamplerStop(int) { return {}; }
#endif
