// wkbai - the loading screen's progress (LoadingProgress.h)

#include "LoadingProgress.h"
#include "../file.h"
#include <SDL3/SDL_timer.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

namespace {

const char* const STEP_LABELS[LOADING_NUM_STEPS] = {
	"Reading the game set",
	"Reading the map and the level",
	"Building the terrain",
	"Compiling the model shaders",
	"Loading the models",
	"Preparing the textures",
	"Building the GPU buffers",
	"Starting the game",
};

struct Checkpoint { std::string key; double ms; };

// Without a last load: a load on the test machine (12 cores, RX 6900 XT; the 6-player map Tribes
// Unite, the game set's 2549 models; the game set's files left out), ms from its start
const Checkpoint DEFAULT_CHECKPOINTS[] = {
	{ "Reading the game set", 13 },
	{ "Reading the map and the level", 707 },
	{ "Reading the map and the level/the map", 709 },
	{ "Reading the map and the level/the level's objects", 743 },
	{ "Building the terrain", 1915 },
	{ "Building the terrain/the map", 1916 },
	{ "Building the terrain/shader VS", 2007 },
	{ "Building the terrain/shader PS", 2020 },
	{ "Building the terrain/shader PS_Clouds", 2928 },
	{ "Building the terrain/tile textures", 3380 },
	{ "Building the terrain/water maps", 4426 },
	{ "Building the terrain/shader VS#2", 4710 },
	{ "Building the terrain/shader PS#2", 4731 },
	{ "Building the terrain/shader PS_Albedo", 5128 },
	{ "Building the terrain/shader VS_Tess", 5154 },
	{ "Building the terrain/shader HS", 5186 },
	{ "Building the terrain/shader DS", 5217 },
	{ "Building the terrain/shader VS_Lake", 5504 },
	{ "Building the terrain/shader PS_Lake", 5522 },
	{ "Building the terrain/shader VS_LakeTess", 6016 },
	{ "Building the terrain/shader HS_Lake", 6037 },
	{ "Building the terrain/shader DS_Lake", 6068 },
	{ "Building the terrain/shader PS_LakeHigh", 7458 },
	{ "Building the terrain/shader PS_LakeHigh#2", 10159 },
	{ "Building the terrain/shader VS_Grass", 12917 },
	{ "Building the terrain/shader PS_Grass", 13000 },
	{ "Building the terrain/shader VS_Blob", 13203 },
	{ "Building the terrain/shader PS_Blob", 13220 },
	{ "Building the terrain/grass", 13258 },
	{ "Building the terrain/shader VS_Fall", 14532 },
	{ "Building the terrain/shader PS_Fall", 14549 },
	{ "Building the terrain/shader VS_Foam", 14653 },
	{ "Building the terrain/shader PS_Foam", 14661 },
	{ "Building the terrain/shader PS_Cascade", 14707 },
	{ "Building the terrain/shader VS_Particle", 14849 },
	{ "Building the terrain/shader PS_Particle", 14870 },
	{ "Compiling the model shaders", 14912 },
	{ "Compiling the model shaders/shader VS", 14915 },
	{ "Compiling the model shaders/shader VS_Anim", 15054 },
	{ "Compiling the model shaders/shader PS", 15435 },
	{ "Compiling the model shaders/shader PS_Alpha", 15697 },
	{ "Compiling the model shaders/shader PS_ShadowAlpha", 15954 },
	{ "Compiling the model shaders/shader PS_DepthAlpha", 15962 },
	{ "Compiling the model shaders/shader PS_GI", 15994 },
	{ "Compiling the model shaders/shader PS_GIAlpha", 16025 },
	{ "Compiling the model shaders/shader PS_GIEmit", 16056 },
	{ "Compiling the model shaders/shader PS_GIEmitAlpha", 16149 },
	{ "Compiling the model shaders/shader VS_Motion", 16260 },
	{ "Compiling the model shaders/shader VS_AnimMotion", 16280 },
	{ "Compiling the model shaders/shader PS_Motion", 16513 },
	{ "Compiling the model shaders/shader PS_MotionAlpha", 16523 },
	{ "Loading the models", 16745 },
	{ "Preparing the textures", 17172 },
	{ "Building the GPU buffers", 17991 },
	{ "Starting the game", 22379 },
	{ "Starting the game/post-processing", 22379 },
	{ "Starting the game/shader VS", 22379 },
	{ "Starting the game/shader PS_Ao", 22390 },
	{ "Starting the game/shader PS_Ao#2", 22496 },
	{ "Starting the game/shader PS_Contact", 22608 },
	{ "Starting the game/shader PS_Bright", 22638 },
	{ "Starting the game/shader PS_Blur", 22649 },
	{ "Starting the game/shader PS_Blur#2", 22669 },
	{ "Starting the game/shader PS_Composite", 22693 },
	{ "Starting the game/shader PS_Shafts", 22976 },
	{ "Starting the game/shader PS_Fxaa", 23162 },
	{ "Starting the game/shader PS_Taa", 23214 },
	{ "Starting the game/shader PS_Taa#2", 23298 },
	{ "Starting the game/shader PS_Easu", 23373 },
	{ "Starting the game/shader PS_Easu#2", 23499 },
	{ "Starting the game/shader PS_Easu#3", 23624 },
	{ "Starting the game/shader PS_Easu#4", 23742 },
	{ "Starting the game/shader PS_Rcas", 23860 },
	{ "Starting the game/shader PS_Rcas#2", 23883 },
	{ "Starting the game/shader PS_Rcas#3", 23910 },
	{ "Starting the game/shader PS_Motion", 23943 },
	{ "Starting the game/shader PS_Reactive", 23958 },
	{ "Starting the game/shader PS_Reactive#2", 23971 },
	{ "Starting the game/shader PS_Copy", 23985 },
	{ "Starting the game/ray tracing and the first frame", 23994 },
	{ "Starting the game/shader VS#2", 24285 },
	{ "Starting the game/shader PS", 24342 },
	{ "Starting the game/shader VS#3", 24401 },
	{ "Starting the game/shader PS#2", 24413 },
	{ "Starting the game/first frames", 24529 },
};
const double DEFAULT_TOTAL = 25039;

std::mutex g_mutex;
bool g_active = false;
uint64_t g_startNs = 0;
int g_step = -1;
std::string g_detail;
size_t g_done = 0, g_total = 0;
double g_stepStartMs[LOADING_NUM_STEPS];
std::vector<Checkpoint> g_current;      // this load's checkpoints
std::map<std::string, int> g_keyUses;   // (a key met again: "<key>#2")
// the last load's
std::vector<Checkpoint> g_history;      // in time order
std::map<std::string, double> g_historyAt;
double g_historyTotal = 0;
bool g_historyIsDefault = false; // (DEFAULT_CHECKPOINTS)
// where the bar is: the last checkpoint met that the last load had, its time there, and when here
double g_histMs = 0, g_reachedMs = 0;
float g_shown = 0;

std::function<void()> g_pump;
std::thread::id g_mainThread;
bool g_inFrame = false, g_pumping = false;
uint64_t g_lastPumpNs = 0;

double nowMs() { return (SDL_GetTicksNS() - g_startNs) / 1e6; }

std::filesystem::path historyPath() { return g_userPath / "loading-times.json"; }

void loadHistory()
{
	g_historyIsDefault = false;
	g_history.clear();
	g_historyAt.clear();
	g_historyTotal = 0;
	try {
		if (std::ifstream in(historyPath()); in) {
			const nlohmann::json j = nlohmann::json::parse(in);
			g_historyTotal = j.value("total", 0.0);
			for (const auto& c : j.at("checkpoints")) {
				g_history.push_back({ c.at(0).get<std::string>(), c.at(1).get<double>() });
				g_historyAt.emplace(g_history.back().key, g_history.back().ms);
			}
		}
	}
	catch (const std::exception&) {
		g_history.clear();
		g_historyAt.clear();
		g_historyTotal = 0;
	}
	if (g_historyTotal <= 0 || g_history.empty()) {
		g_history.assign(std::begin(DEFAULT_CHECKPOINTS), std::end(DEFAULT_CHECKPOINTS));
		g_historyAt.clear();
		for (const Checkpoint& c : g_history)
			g_historyAt.emplace(c.key, c.ms);
		g_historyTotal = DEFAULT_TOTAL;
		g_historyIsDefault = true;
	}
}

// (locked)
void checkpoint(const std::string& key)
{
	const int use = ++g_keyUses[key];
	const std::string unique = use > 1 ? key + "#" + std::to_string(use) : key;
	const double now = nowMs();
	g_current.push_back({ unique, now });
	if (auto it = g_historyAt.find(unique); it != g_historyAt.end() && it->second >= g_histMs) {
		g_histMs = it->second;
		g_reachedMs = now;
	}
}

std::string stepKey(int step) { return STEP_LABELS[step]; }

// (locked) the bar's place, 0 to 1
float computeProgress()
{
	if (g_step < 0)
		return 0.0f;
	const double now = nowMs();
	// the next checkpoint the last load had: a count goes there by its count, else the clock
	// moves the bar on toward it, not past it
	double next = g_historyTotal;
	for (const Checkpoint& c : g_history)
		if (c.ms > g_histMs) {
			next = c.ms;
			break;
		}
	const double x = g_total > 0 ? g_histMs + (next - g_histMs) * std::min(1.0, (double)g_done / g_total)
		: g_histMs + std::min(now - g_reachedMs, (next - g_histMs) * 0.95);
	const double p = x / g_historyTotal;
	g_shown = std::max(g_shown, (float)std::clamp(p, 0.0, 0.995));
	return g_shown;
}

} // namespace

void LoadingBegin()
{
	std::lock_guard<std::mutex> lock(g_mutex);
	g_active = true;
	g_startNs = SDL_GetTicksNS();
	g_step = -1;
	g_detail.clear();
	g_done = g_total = 0;
	std::fill(std::begin(g_stepStartMs), std::end(g_stepStartMs), -1.0);
	g_current.clear();
	g_keyUses.clear();
	g_histMs = g_reachedMs = 0;
	g_shown = 0;
	loadHistory();
	printf("Loading: started (paced by %s)\n", g_historyIsDefault ? "the typical times" : "the last load's times");
}

void LoadingBeginIfIdle()
{
	bool active;
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		active = g_active;
	}
	if (!active)
		LoadingBegin();
}

bool LoadingActive()
{
	std::lock_guard<std::mutex> lock(g_mutex);
	return g_active;
}

void LoadingEnterStep(LoadingStep step)
{
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		if (!g_active || step <= g_step)
			return;
		const double now = nowMs();
		const float shown = computeProgress();
		if (g_step >= 0)
			printf("Loading: %-30s %6.0f ms (bar at %2.0f%%)\n", STEP_LABELS[g_step], now - g_stepStartMs[g_step], shown * 100.0f);
		g_step = step;
		g_stepStartMs[step] = now;
		g_detail.clear();
		g_done = g_total = 0;
		checkpoint(stepKey(step));
		fflush(stdout);
	}
	LoadingTick();
}

void LoadingDetail(const std::string& detail)
{
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		if (!g_active || g_step < 0)
			return;
		g_detail = detail;
		g_done = g_total = 0;
		checkpoint(stepKey(g_step) + "/" + detail);
	}
	LoadingTick();
}

void LoadingCompilingShader(const char* name)
{
	if (LoadingActive())
		LoadingDetail(std::string("shader ") + name);
}

void LoadingCount(size_t done, size_t total)
{
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		if (!g_active || g_step < 0)
			return;
		g_done = done;
		g_total = total;
	}
	LoadingTick();
}

void LoadingFinish()
{
	std::lock_guard<std::mutex> lock(g_mutex);
	if (!g_active)
		return;
	const double total = nowMs();
	if (g_step >= 0)
		printf("Loading: %-30s %6.0f ms (bar at %2.0f%%)\n", STEP_LABELS[g_step], total - g_stepStartMs[g_step], computeProgress() * 100.0f);
	printf("Loading: done in %.0f ms (the last load: %.0f ms)\n", total, g_historyTotal);
	fflush(stdout);
	g_active = false;
	nlohmann::json j;
	j["total"] = total;
	nlohmann::json list = nlohmann::json::array();
	for (const Checkpoint& c : g_current)
		list.push_back({ c.key, std::round(c.ms * 10.0) / 10.0 });
	j["checkpoints"] = list;
	try {
		std::ofstream out(historyPath());
		out << j.dump(1);
	}
	catch (const std::exception&) {
	}
}

void LoadingAbort()
{
	std::lock_guard<std::mutex> lock(g_mutex);
	g_active = false;
}

LoadingView LoadingGet()
{
	std::lock_guard<std::mutex> lock(g_mutex);
	LoadingView v;
	if (!g_active)
		return v;
	v.step = g_step;
	v.progress = computeProgress();
	v.elapsedMs = nowMs();
	if (g_step >= 0) {
		v.label = STEP_LABELS[g_step];
		if (!g_detail.empty())
			v.label += ": " + g_detail;
		if (g_total > 0)
			v.label += "  " + std::to_string(std::min(g_done, g_total)) + " / " + std::to_string(g_total);
	}
	return v;
}

void LoadingSetPump(std::function<void()> pump)
{
	std::lock_guard<std::mutex> lock(g_mutex);
	g_pump = std::move(pump);
	g_mainThread = std::this_thread::get_id();
}

void LoadingSetInFrame(bool inFrame)
{
	g_inFrame = inFrame;
}

void LoadingTick()
{
	std::function<void()> pump;
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		if (!g_active || !g_pump || std::this_thread::get_id() != g_mainThread)
			return;
		pump = g_pump;
	}
	if (g_inFrame || g_pumping)
		return;
	const uint64_t now = SDL_GetTicksNS();
	if (now - g_lastPumpNs < 33000000ull)
		return;
	g_pumping = true;
	pump();
	g_pumping = false;
	g_lastPumpNs = SDL_GetTicksNS();
}
