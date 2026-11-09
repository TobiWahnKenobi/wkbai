#pragma once

#include <cstddef>
#include <functional>
#include <string>

// The loading screen's progress: what the load is doing now (a step, a detail such as the shader
// being compiled or the file being read, a count such as "Models 812 / 2549") and how far it is.
// The server's thread (the game set, the map and the level) and the main thread (the terrain, the
// shaders, the models, the first frames) report into it.
//
// The bar moves by time, as the last load took it (<user folder>\loading-times.json): every step
// and detail is a checkpoint, and the bar stands where the last load's clock stood when it got
// there, moving on toward the next one with the clock (a counted step by its count). Without a
// last load, by a load on the test machine (LoadingProgress.cpp's DEFAULT_CHECKPOINTS).
//
// The work that blocks the main thread (the terrain renderer, the shaders, the models) calls
// LoadingTick() now and then (each detail and count does), which draws a loading frame (the pump,
// ClientInterface's) at most every 33 ms, so the screen moves while it loads.
enum LoadingStep {
	LOADING_GAME_SET,      // the server's (a guest's: its own) game set files
	LOADING_LEVEL,         // the map and the level's objects
	LOADING_TERRAIN,       // the client's terrain: textures, water, the terrain's and the water's shaders, grass
	LOADING_MODEL_SHADERS, // the models' renderer and its shaders
	LOADING_MODELS,        // the game set's buildings, characters and missiles: their files
	LOADING_TEXTURES,      // their textures
	LOADING_GPU_BUFFERS,   // their meshes' and animations' GPU buffers
	LOADING_FIRST_FRAMES,  // "Starting the game": the first frame (the post-processing's and the ray tracing's set-up), then 30 more
	LOADING_NUM_STEPS
};

// A new load (the host before its server's thread starts, a guest when the game set comes)
void LoadingBegin();
// ... unless one is on
void LoadingBeginIfIdle();
bool LoadingActive();
// The step it is at now (later steps only: an earlier one is ignored)
void LoadingEnterStep(LoadingStep step);
// What it does within the step (a checkpoint); pumps a frame on the main thread
void LoadingDetail(const std::string& detail);
// A shader about to be compiled (a detail: "Shader PS_Lake")
void LoadingCompilingShader(const char* name);
// How far the step is by a count (not a checkpoint); pumps a frame on the main thread
void LoadingCount(size_t done, size_t total);
// The game is on: the steps' times printed and kept for the next load
void LoadingFinish();
// The game left before it loaded
void LoadingAbort();

struct LoadingView {
	int step = -1;
	std::string label;  // "Loading the models  812 / 2549", "Building the terrain: shader PS_Lake"
	float progress = 0; // 0 to 1, never back
	double elapsedMs = 0;
};
LoadingView LoadingGet();

// The main thread's frame drawer for LoadingTick (none: no frames); while inFrame (a frame being
// built), LoadingTick draws none
void LoadingSetPump(std::function<void()> pump);
void LoadingSetInFrame(bool inFrame);
void LoadingTick();
