#pragma once

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <set>

class BreakpointManager
{
public:
	static BreakpointManager& instance();
	// whether any breakpoint is set or a step is asked for: without, an action needs no check
	// (ActionSequence::run asked instance() and checkAndBreak, two mutexes and a set lookup, for
	// every action: 1.7% of a 9000-unit battle step, docs/engine/script-profile.md)
	static bool active() { return s_active.load(std::memory_order_relaxed); }

	void setBreakpoint(int fileIndex, int lineNumber);
	void removeBreakpoint(int fileIndex, int lineNumber);
	void clearAllBreakpoints();
	bool isBreakpointSet(int fileIndex, int lineNumber);

	void checkAndBreak(int fileIndex, int lineNumber);
	std::optional<std::pair<int, int>> getCurrentBreakLine();
	void resumeFromBreak();
	void stepInto();

private:
	BreakpointManager() = default;
	std::mutex m_accessMutex;
	std::condition_variable m_breakCondVar;
	std::set<std::pair<int, int>> m_breakpointSet;
	std::optional<std::pair<int, int>> m_currentBreak;
	bool m_pleaseUnbreak = false;
	bool m_stepOnNextAction = false;
	
	static std::unique_ptr<BreakpointManager> s_instance;
	static std::atomic<bool> s_active;
	void updateActive() { s_active.store(!m_breakpointSet.empty() || m_stepOnNextAction, std::memory_order_relaxed); }
};