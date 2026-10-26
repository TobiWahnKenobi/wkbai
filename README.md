# wkbre2 — modernization fork

> **This is a fork of [AdrienTD/wkbre2](https://github.com/AdrienTD/wkbre2)**, the open-source engine
> reimplementation of Warrior Kings and Warrior Kings: Battles (Black Cactus, 2002/2003).
> This fork is developed **with AI assistance (Claude) and reverse engineering of the original game**.
> The original executable is analysed (Ghidra, disassembly, Direct3D call traces, side-by-side captures)
> to find out how the original renders and behaves, and wkbre2 is then changed to match it or to improve on it.
> All credit for wkbre2 itself goes to AdrienTD and its contributors.
>
> The work happens on the `modernization` branch. No code or assets of the original game are included:
> you need your own copy of the game.

![A town in a campaign save, rendered by this fork](docs/screenshot-town.jpg)

*The stress-test scene: a campaign save with about 4,850 objects in view, drawn with Direct3D 12.*

![The whole map of the same save without fog](docs/screenshot-map.jpg)

## What this fork changes

Compared with upstream wkbre2:

- **Renderer**
  - [Diligent Engine](https://github.com/DiligentGraphics/DiligentCore) with **Direct3D 12 and Vulkan**. The old Direct3D 9/11 and OpenGL renderers are gone, and a null renderer remains for tests.
- **Faithful look, from reverse engineering the original**
  - **Lighting:** the original's model and terrain lighting.
  - **Shadows:** the original's static building shadows, scrolling cloud shadows and unit blob shadows.
  - **Sky and fog:** the sky cube with its horizon fade, the fog distances, and an edge fog that hides the map border.
  - **Water:** a water surface with scrolling texture layers, ripples and a sun glint.
  - **Models:** `SHEAR_MODEL` models adapt to sloped ground.
- **Gameplay and interface**
  - **Minimap:** player dots, the camera arrow and the four mode buttons.
  - **Camera:** limits on pitch and height, and Shift to scroll faster.
  - **Order flags:** a flag marks the target of an order.
  - **Deleting:** the Delete key destroys the selected object, with its death animation.
- **Performance**
  - **Terrain:** drawn in chunks, with water in a single draw call.
  - **Loading:** models are prepared at level start, so new parts of the map no longer stutter into view.
  - **Shadows on the GPU:** blob shadows are drawn on the GPU.
  - **Scene building:** cheaper per-object work.
  - **Result:** the stress test above runs in about 4.3 ms per frame (was 8.0 ms).
- **Engine**
  - **Toolchain:** x64, C++20 and SDL 3, built with a vcpkg manifest and Windows CI.
  - **Profiling:** a Tracy profiler client and a benchmark mode with frame-time reports.
- **Mods**
  - **File system:** a layered file system that follows the original's archive rules (the newest timestamp wins).
  - **Mod folders:** mod folders with `mod.json`, and a Mods menu to enable them and change their load order.
- **Robustness**
  - **Scripts:** the game-set parser stays in sync on unimplemented keywords.
  - **Missing files:** broken mod combinations no longer crash.
- **Settings:** a temporary settings window on **F10** for the view distance, fog, field of view, colour grading, shadows, water and camera speed.

## Status

The project follows a ten-phase plan, from setup (phase 0) to release (phase 9). **Current phase: 4, the modern renderer**, starting with a mode that looks like the original. **Next:** the main menu, the Settings menu and the in-game Esc menu, then bugs found in play-testing.

| Phase | Progress | Done | Open |
|---|---|---|---|
| 0 Setup | 4/7 | Repository, toolchain, Ghidra connected | DXVK quick win, baseline captures |
| 1 Asset formats | 2/4 | BCP archive reader/writer, round-trip tests | Format specs, asset inventory |
| 2 Binary reverse engineering (ongoing) | 4/7 | Class hierarchy (RTTI), script keyword coverage, file system | Pathfinding, combat and AI; differential tests |
| 3 Engine foundation | 6/6 | Fork, CI, x64/C++20/SDL 3, layered file system, renderer choice, profiler | — |
| **4 Modern renderer** (current) | 1/8 | Diligent (D3D12/Vulkan) | Faithful mode (in progress), GPU skinning and culling, PBR, lighting and effects, splat-map terrain, glTF/KTX2, UI scaling |
| 5 Gameplay parity | 2/7 | WKB Plus evaluated, minimap | Menus, play-test bugs, script gaps, deterministic simulation, original engine bugs |
| 6 Map editor | 0/3 | — | Port the wkbre editor, new map format, undo and play-testing |
| 7 Modding | 1/4 | Mod package format | Overrides, hot reload, modding docs |
| 8 AI overhaul | 0/4 | — | Reverse-engineer the original AI, new strategic and tactical layers |
| 9 Release | 0/1 | — | Launcher and packaging |

**Known issues from play-testing:**
- **Building placement:** buildings can be placed on top of other buildings.
- **Formations:** they move as one block, without animations.
- **Guarding:** a unit told to guard another walks into it instead of keeping its distance.
- **Range circles:** they are missing.
- **Rally flag:** selecting a building doesn't show its rally flag.

## Usage

- Build the `modernization` branch (see below). Windows only for now: the Diligent renderer still needs a Linux window path.
- Create **wkconfig.json** next to the executable, or edit it if it is already there:
  - **gamePath:** the folder where the game is installed, the one that contains data.bcp. Write each backslash twice (`\\`).
  - **gameVersion:** 1 for **Warrior Kings**, 2 for **Warrior Kings: Battles**.
  - **diligentBackend:** optionally, `"d3d12"` (the default) or `"vulkan"`.
- Launch wkbre2. Press **F10** in game for the settings window.

**Note:** if the program closes at startup without an error, check **wkconfig.json** for syntax errors (missing commas, quotes or braces).

## Compiling

You need:
- Visual Studio 2022 or later, with C++20;
- [CMake](https://cmake.org/);
- [vcpkg](https://vcpkg.io).

The dependencies come from the `vcpkg.json` manifest. Diligent Engine is fetched by CMake.

```
cmake -S . -B build -A x64 -DCMAKE_TOOLCHAIN_FILE=<vcpkg root>/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows
cmake --build build --config RelWithDebInfo
```

---

## About wkbre2 (from the upstream README)

wkbre2 is an engine recreation: it loads the original game's own asset and script files. The original
game uses a set of text-based script files, the **game set**, that define all units, buildings, game mechanics,
commands and orders. wkbre2 interprets this game set, much as ScummVM interprets the scripts of adventure games.
This way it supports nearly all of the game's mechanics without reimplementing them from scratch.

Like the original engine, wkbre2 has a **client-server architecture**:
- the **server** runs all game logic and holds the state of the game;
- each **client** shows the player a view of that state and sends the player's commands to the server.

This architecture is what makes multiplayer possible. In single player, the server and the client run on
separate threads, so the game logic and the rendering don't slow each other down.

See [AdrienTD/wkbre2](https://github.com/AdrienTD/wkbre2) for the original project, its history and its releases.

## License

GPL-3.0, as upstream: see [LICENSE](LICENSE).
