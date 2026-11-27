// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#pragma once

// Where the simulation's time goes in the game set's scripts (the profiling tests: test 112's
// "scripts": true, test 30's "scriptSample"). The sampler (platform.cpp SamplerStart) stops the
// simulation thread about 1000 times a second and notes its stack; ScriptSampleReport sorts the
// samples by the script code on them (gameset/values.cpp, finder.cpp, actions.cpp, ...): the
// nodes' own code (the tree walk), the heap under a script (temporary lists) and the engine code a
// script calls. While sampling, the named definitions a script runs through (an equation, an
// object finder definition, a condition, a value tag, an action sequence) are kept on a small
// stack of marks the sampler copies, so the samples also go to the definitions.
//
// Off by default: a mark is a test of one bool. The marks only note pointers: they draw no random
// numbers and change no evaluation (determinism, test 27's hash).

#include <atomic>
#include <string>

struct ScriptMarks {
	enum Kind { SEQUENCE = 0, VALUE = 1, FINDER = 2 };
	struct Entry { const void* p; int kind; };
	static constexpr int MAX = 64;
	Entry e[MAX];
	int depth = 0;
};
extern ScriptMarks g_scriptMarks;
// on while the sampler samples scripts, for the sampled thread only
extern bool g_scriptMarksOn;
bool ScriptMarksThisThread();

struct ScriptMark {
	bool on;
	ScriptMark(int kind, const void* p) : on(g_scriptMarksOn && ScriptMarksThisThread()) {
		if (!on) return;
		const int d = g_scriptMarks.depth;
		if (d < ScriptMarks::MAX) g_scriptMarks.e[d] = { p, kind };
		// (the entry before the depth: the sampler stops the thread between any two stores)
		std::atomic_signal_fence(std::memory_order_seq_cst);
		g_scriptMarks.depth = d + 1;
	}
	~ScriptMark() {
		if (!on) return;
		std::atomic_signal_fence(std::memory_order_seq_cst);
		g_scriptMarks.depth = g_scriptMarks.depth - 1;
	}
	ScriptMark(const ScriptMark&) = delete;
	ScriptMark& operator=(const ScriptMark&) = delete;
};

struct GameSet;
// After SamplerStop (or instead of it: it stops the sampler too). The samples of the simulation
// (Server::simulate on the stack) sorted by scripts, as JSON text: shares of the simulation's
// samples, per sim phase bucket (g_simPhase), the top node types (own and inclusive), the engine
// functions scripts call, the engine callers of scripts and the top definitions. msPerStep: the
// phase's average step, to turn the shares into ms per step. Printed lines go to *text.
std::string ScriptSampleReport(const GameSet* gs, double msPerStep, int top, std::string* text);
