# wkbai

> **wkbai is an independent fan project. It is in no way affiliated with, endorsed by or connected
> to Black Cactus, the makers, publishers or rights holders of Warrior Kings and Warrior Kings:
> Battles, nor to wkbre2 or its author AdrienTD.** Warrior Kings is a trademark of its owners.

> **wkbai is a fork of [AdrienTD/wkbre2](https://github.com/AdrienTD/wkbre2)** (renamed on
> 2026-10-31, as it has grown far from it), the open-source engine
> reimplementation of Warrior Kings and Warrior Kings: Battles (Black Cactus, 2002/2003).
> This fork is developed **with AI assistance (Claude) and reverse engineering of the original game**.
> The original executable is analysed (Ghidra, disassembly, Direct3D call traces, side-by-side captures)
> to find out how the original renders and behaves, and the engine is then changed to match it or to improve on it.
> All credit for wkbre2 itself goes to AdrienTD and its contributors.
>
> The work happens on the `modernization` branch. No code or assets of the original game are included:
> you need your own copy of the game.

![A town in a campaign save, rendered by wkbai](docs/screenshot-town.jpg)

*The stress-test scene: a campaign save with about 4,850 objects in view, drawn with Direct3D 12. Grass, fire lights, real unit shadows, terrain tessellation, FXAA and 16x anisotropic filtering are on.*

![The same save from above without fog, with the High water](docs/screenshot-map.jpg)

*The same save from above, without fog: the High water, with tessellated waves and whitecaps.*

## What wkbai changes

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
- **AI**
  - **Kingmaker AI:** stalls fixed (deleted objects, work orders, farms at the map's corner, an upgrade waited for forever), and towns get their walls and gatehouse.
    - **Commissions:** saved and loaded. `FORCE_BALANCE` serves the least filled requirement first, as the original does, and a commission waiting for a busy building keeps its resources.
    - **Sight:** the AI sees by sight (`IS_VISIBLE`, `IS_DISCOVERED`).
  - **Differential tests against the original:** wkbai plays 30 game seconds on from a save of the original's (Marjoria's Keep) and is compared object by object with the original's own save after the same time. 135 of 138 unit and building counts now match.
  - **Aggressive AI** (skirmish setting, on by default): at most 80 peasants, barracks first, armies launched as soon as they can be paid, food-only fighters when wood or gold is short, and all of an AI's armies sent against its nearest enemy (an AI destroyed per 25-minute 8-player game, none before).
  - **Diplomacy:** AIs' diplomatic offers reach the players offered to, whose scripts answer them.
  - **Headless AI-vs-AI test** (test 30) with a census per minute, and a tournament tool over several seeds.
- **Pathfinding:** units keep to the terrain their passability fields allow, as the original's per-tile test does (no walking over water, river beds or mountains).
- **Graphics options** (enhanced renderer)
  - **Post-processing:** ambient occlusion, bloom, and **FXAA** anti-aliasing (on by default).
  - **Anisotropic filtering:** up to 16x for the terrain, the water and the models.
  - **Real unit shadows** from the sun's shadow map, and wall shadows (higher-resolution shadow maps optional).
  - **Fire lights:** burning buildings, flaming arrows and fire spells light the terrain and the models around them.
  - **Water, in six levels:**
    - **Off:** a plain colour, as the original without its water surface.
    - **Original:** the original's textured water.
    - **Low:** sky and screen-space reflections, refraction, depth colour, shore foam and flowing rivers.
    - **Medium:** the ocean look on the flat surface.
    - **High:** tessellated ocean waves with crest foam and surf.
    - **Ultra:** finer waves out to the far distance.
  - **Ocean:** long, smooth waves in groups that never repeat, deep teal water with light through the crests, sun sparkles and patchy foam.
  - **Rivers:** they follow the geography, each with its own coordinates along and across its channel (from where the water meets the terrain). Small ripples are carried downstream by a current that is faster where the river is narrow, with foam along the banks and short-lived crests where it runs tight. They drain into the largest water they meet or off the map, and blend smoothly into the sea.
  - **Reflections:** of what is on screen (from Low up) and of the sky box itself.
  - **Terrain tessellation:** a smooth surface on the GPU, with objects placed exactly on it. The game's own heights are unchanged.
  - **Grass:** PBR grass blades on green ground, with density (up to 24 per square metre) and height sliders, thinning out with the distance.
  - **Blended terrain:** materials blended at the tile corners, each from several of its tiles, triplanar on cliffs (off by default).
  - **Model highlights:** per-pixel lighting with GGX sun highlights, light from the sky box's colours, and override packs (`<texture>_normal`, `<texture>_orm`) for modders (off by default).
  - **Particles:** sprites, and streaks (wood chips, sparks) drawn as thin lines.
- **Formats:** glTF 2.0 models (import as overrides, export with `"exportGltf"`), DDS textures (BC1-BC5, BC7) and KTX2 (RGBA8, BC1-BC5, BC7; BC7 uploaded compressed).
- **Map editor** (F8): heights, texture painting, lakes, objects, the players' resources and diplomacy, terrain zones, a new blank map, undo, a new minimap on saving, a check (missing start cameras, players that can't be reached by land), and save-and-play.
- **Settings:** an options menu, and a settings window on **F10** for the view distance, fog, field of view, colour grading, shadows, water and camera speed.

## Status

The project follows a ten-phase plan, from setup (phase 0) to release (phase 9). **Current phase:** 5, gameplay parity. **Next:** Phase 5: test 36's last differences (wood after loading: saved animation-loop triggers; tree stumps; a farm foundation), the original's arrival rule for attachment paths; Phase 8: army retreat and regrouping; Phase 6: the new map format and editor features.

| Phase | Progress | Done | Open |
|---|---|---|---|
| 0 Setup & quick wins | 7/7 | Git repo, Pristine SHA-256 hashes, Toolchain installed, REA + Ghidra connected, DXVK quick win, Baseline perf captures, Identify version.dll | — |
| 1 Asset & format knowledge | 4/4 | BCP reader/extractor, Format specs, Round-trip tests, Asset inventory | — |
| 2 Binary RE (ongoing) | 7/7 | Exe in Ghidra via REA, RTTI class hierarchy, Keyword diff vs wkbre2, Original file system (BCP load rule), Renderer interface signatures, Pathfinding/combat/AI RE, Differential test harness | — |
| 3 Engine foundation (wkbre2) | 6/6 | Fork & build wkbre2, Run against game data, CI + round-trip tests, x64 · C++20 · SDL3, Layered VFS (BCP → mods), RHI decision + Tracy | — |
| 4 Modern renderer | 8/8 | Diligent integration (D3D12/Vulkan), Faithful mode, GPU skinning/instancing/culling, PBR materials, Lighting & effects, Splat-map terrain, glTF / KTX2, Resolution & UI scaling | — |
| **5 Gameplay parity** (current) | 7/8 | Evaluate WKB Plus patch, Minimap + DISPLAY_ON_MINIMAP, Main menu, Settings, in-game Esc menu, Play-test bugs: stampdown overlap, formations, guard, range circles, rally flag, Deterministic simulation, Fix original engine bugs, Population limit up to 10,000 | Close script/order gaps (in progress) |
| 6 Map editor | 1/3 | Undo / play-test / validation | Port wkbre editor features (in progress), Export original + new format (in progress) |
| 7 Modding | 4/4 | Mod package format, Asset/script/Lua overrides, Hot reload + mod manager, Modding docs | — |
| 8 AI overhaul | 2/4 | RE original AI, Headless AI-vs-AI testing | Strategic layer (in progress), Operational/tactical layers (in progress) |
| 9 Release | 1/1 | Launcher + packaging | — |

**Play-testing:** the issues found in three play tests are fixed or listed in the reverse-engineering repository's notes (building placement, formations, guarding, range circles, rally flags, AI armies and walls, particles, wall shadows, the HUD and the settings steppers).

## Usage

- Build the `modernization` branch (see below). Windows only for now: the Diligent renderer still needs a Linux window path.
- Create **wkconfig.json** next to the executable, or edit it if it is already there:
  - **gamePath:** the folder where the game is installed, the one that contains data.bcp. Write each backslash twice (`\\`).
  - **gameVersion:** 1 for **Warrior Kings**, 2 for **Warrior Kings: Battles**.
  - **diligentBackend:** optionally, `"d3d12"` (the default) or `"vulkan"`.
- Launch **wkbai.exe**. Press **F10** in game for the settings window. Settings, saves and mods go to `%APPDATA%\wkbai` (copied from `%APPDATA%\wkbre2` the first time).

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
