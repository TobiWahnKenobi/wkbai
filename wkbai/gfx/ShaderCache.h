// wkbai - the compiled shaders and the driver's pipeline cache kept on disk
// (<user folder>\shadercache), so that only shaders that changed are compiled again.
//
// Each shader is kept by a hash of everything its bytecode depends on: its source with the
// files it includes, its macros, entry point, stage, language, compiler and shader model, the
// compilers' versions (Diligent's, FXC's and DXC's DLLs) and the cache format. A shader whose
// hash is in the cache is created from the stored bytecode; any other is compiled and stored.
// The whole cache is dropped when the graphics adapter or its driver changes (adapter.txt:
// backend, vendor, device, subsystem and revision IDs, the driver's version and, with Vulkan,
// the pipeline cache UUID). With Vulkan the driver's VkPipelineCache is kept as well
// (pipelines-vk.bin) under the same adapter and driver.
//
// Robustness: every file is written to a temporary name and renamed; an entry that is short,
// has the wrong format or hash, or fails to load is a miss (compiled again and rewritten); the
// folder is kept under a size cap, least recently used entries first, and entries unused for
// 90 days are removed at the start.

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace Diligent {
struct IRenderDevice;
struct IShader;
struct IPipelineState;
struct ShaderCreateInfo;
struct GraphicsPipelineStateCreateInfo;
struct ComputePipelineStateCreateInfo;
}

namespace ShaderCache {

// Opens the cache for the device (once the device exists). dxcPath: the DXC DLL the device was
// given (its version is part of every key), or empty.
void Init(Diligent::IRenderDevice* device, const std::string& dxcPath);

// device->CreateShader through the cache. A shader that failed to compile is remembered as a
// failure too (the same source fails the same way with the same compiler and driver).
void CreateShader(Diligent::IRenderDevice* device, const Diligent::ShaderCreateInfo& sci, Diligent::IShader** shader);

// device->Create...PipelineState with the driver's pipeline cache (Vulkan), timed.
void CreateGraphicsPipeline(Diligent::IRenderDevice* device, const Diligent::GraphicsPipelineStateCreateInfo& ci, Diligent::IPipelineState** pso);
void CreateComputePipeline(Diligent::IRenderDevice* device, const Diligent::ComputePipelineStateCreateInfo& ci, Diligent::IPipelineState** pso);

// Once a frame: once the start or a map load is over (no loading screen, no shader or pipeline
// made for 3 s), logs one line (hits, misses, rebuilds, time) and saves the pipeline cache.
void OnFrame();

struct Stats {
	int requests = 0;
	int loaded = 0;     // created from the cache
	int compiled = 0;   // compiled (a miss)
	int corrupt = 0;    // of the misses: an entry was there but unreadable
	int failed = 0;     // compiled and failed, or a remembered failure
	int pipelines = 0;
	double compileMs = 0, loadMs = 0, pipelineMs = 0;
	bool rebuilt = false; // the adapter or driver changed at Init: the cache was emptied
	std::string reason;   // why it was rebuilt
};
Stats GetStats();
void ResetStats();

std::filesystem::path Directory();
// the adapter and driver of the cache (adapter.txt's text)
std::string AdapterHeader();

// Tests: opens the cache again as Init does, with this text added to the adapter header (a
// pretend driver update); "" is the real header.
void ReopenForTest(const std::string& fakeDriver);
// Tests: the size cap in bytes (default 256 MB)
void SetSizeCapForTest(uint64_t bytes);
// Tests: prunes the folder now (as at Init); returns the entries left
int PruneNow();
// Tests: keeps a copy of every shader request from now on (SetRecording(true)) ...
void SetRecording(bool on);
size_t RecordedCount();
// ... and makes them all again through the cache; the one numbered touched (if it has its
// source as text) with a comment added, as if it had been edited. Returns the shaders made.
int Replay(int touched = -1);

} // namespace ShaderCache
