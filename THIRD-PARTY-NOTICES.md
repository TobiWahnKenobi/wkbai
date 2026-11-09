# Licences and third-party notices

## wkbai itself

wkbai is licensed under the **GNU General Public License, version 3** ([LICENSE](LICENSE)).

wkbai is a **modified version** of [wkbre2](https://github.com/AdrienTD/wkbre2) by AdrienTD
(GPL-3.0), forked from upstream commit `1b39df6` and changed since 2026-10-25 (renamed to wkbai on
2026-10-31). The git history records every change. Files that still carry AdrienTD's header
("(C) AdrienTD, Licensed under the GNU General Public License 3") are wkbre2's files, many of them
since modified; files without a header, or with a wkbai header, were added by the fork. Both are
under the GPL-3.0 unless this file names another licence for them below.

**Source code:** <https://github.com/TobiWahnKenobi/wkbai>. A release zip names its commit in
`SOURCE.txt`; the source of that commit, with the dependency versions below, is its Corresponding
Source.

Every component below is under a licence compatible with the GPL-3.0. Their full texts are in the
files named here, and a release zip carries them in its `licenses/` folder (`tools/package.py`).

## Code inside this repository under other licences

| Path | Component | Licence | Full text |
|---|---|---|---|
| `wkbai/imgui/` | Dear ImGui 1.92.4, (c) 2014-2025 Omar Cornut | MIT | `wkbai/imgui/LICENSE.txt` |
| `wkbai/imgui/imstb_*.h` | stb_rectpack, stb_textedit, stb_truetype (Sean Barrett), as bundled with Dear ImGui | Public domain or MIT, at your choice | at the end of each file |
| `wkbai/FidelityFX/` | AMD FidelityFX SDK 1.1.4 shaders (FSR 3.1 upscaler, FSR 1, SPD), (c) 2024 Advanced Micro Devices, Inc. | MIT | `wkbai/FidelityFX/LICENSE.txt` |
| `wkbai/gfx/DiligentPostProcess.cpp`: `EasuSet`, `EasuTap`, `PS_Easu`, `PS_Rcas` and their helpers | port of FidelityFX FSR 1 (`ffx_fsr1.h`), (c) 2024 Advanced Micro Devices, Inc. | MIT | `wkbai/FidelityFX/LICENSE.txt` |
| `wkbai/gfx/DiligentSkyRenderer.cpp`: `Hash33` | "Hash without Sine", (c) 2014 David Hoskins | MIT | in the file, above the function |
| `wkbai/util/bcdec.h` | bcdec (BC1-BC7 decoder), (c) 2022 Sergii Kudlai | MIT or the Unlicense, at your choice | at the end of the file |
| `wkbai/lzrw3.c`, `wkbai/lzrw_headers.h` | LZRW3, Ross Williams (1991) | Public domain | in the files |
| `wkbai/matchmaking/wkbai_mm_plugin.h`, `plugins/eos/` | wkbai's matchmaking plugin interface and its EOS plugin (wkbai's own code) | MIT (so a plugin may use a closed SDK) | in the files |

## Libraries built into or shipped with wkbai.exe

Fetched by CMake (`wkbai/CMakeLists.txt`), linked statically into `wkbai.exe`:

| Component | Version | Licence | Licence file in its source |
|---|---|---|---|
| [Diligent Engine Core](https://github.com/DiligentGraphics/DiligentCore) | commit `cd2329b` | Apache-2.0 | `License.txt` |
| [SPIRV-Tools](https://github.com/KhronosGroup/SPIRV-Tools) (via Diligent) | as pinned by that commit | Apache-2.0 | `ThirdParty/SPIRV-Tools/LICENSE` |
| [SPIRV-Cross](https://github.com/KhronosGroup/SPIRV-Cross) (via Diligent) | as pinned | Apache-2.0 | `ThirdParty/SPIRV-Cross/LICENSE` |
| [SPIRV-Headers](https://github.com/KhronosGroup/SPIRV-Headers) (via Diligent) | as pinned | MIT-style (Khronos) | `ThirdParty/SPIRV-Headers/LICENSE` |
| [glslang](https://github.com/KhronosGroup/glslang) (via Diligent) | as pinned | BSD-3-Clause, BSD-2-Clause, MIT, Apache-2.0, GPL-3.0-or-later with the Bison exception (per file) | `ThirdParty/glslang/LICENSE.txt` |
| [Vulkan-Headers](https://github.com/KhronosGroup/Vulkan-Headers) (via Diligent) | as pinned | Apache-2.0 or MIT | `ThirdParty/Vulkan-Headers/LICENSE.md` |
| [volk](https://github.com/zeux/volk) (via Diligent) | as pinned | MIT | `ThirdParty/volk/LICENSE.md` |
| [xxHash](https://github.com/Cyan4973/xxHash) (via Diligent) | as pinned | BSD-2-Clause | `ThirdParty/xxHash/LICENSE` |
| GPUOpen Shader Utils, `DXBCChecksum` (via Diligent) | as pinned | MIT | `ThirdParty/GPUOpenShaderUtils/License.txt` |
| [NVIDIA NVAPI](https://github.com/NVIDIA/nvapi) (via Diligent, `nvapi64.lib`) | as fetched by Diligent | MIT | `License.txt` |
| [Tracy Profiler](https://github.com/wolfpld/tracy) client | v0.14.1 | BSD-3-Clause | `LICENSE` |

From vcpkg (`vcpkg.json`, baseline `817892b`); each port's licence is installed as
`vcpkg_installed/<triplet>/share/<port>/copyright`:

| Component | Version | Licence | How wkbai uses it |
|---|---|---|---|
| [SDL](https://libsdl.org) | 3.4.18 | Zlib | `SDL3.dll` |
| [OpenAL Soft](https://github.com/kcat/openal-soft) | 1.25.2 | LGPL-2.0-or-later | `OpenAL32.dll`, dynamically linked |
| [mpg123](https://mpg123.org) | 1.33.7 | LGPL-2.1 | `mpg123.dll`, dynamically linked |
| [{fmt}](https://github.com/fmtlib/fmt) | 12.2.0 | MIT | `fmt.dll` (OpenAL Soft's dependency) |
| [Lua](https://www.lua.org) | 5.5.1 | MIT | `lua.dll` |
| [bzip2](https://sourceware.org/bzip2/) | 1.0.8 | bzip2 licence (BSD-style) | `bz2.dll` |
| [ENet](https://github.com/lsalzman/enet) | 1.3.18 | MIT | static |
| [nlohmann/json](https://github.com/nlohmann/json) | 3.12.0 | MIT | header-only |
| [sol2](https://github.com/ThePhD/sol2) | 3.5.0 | MIT | header-only |
| [stb](https://github.com/nothings/stb) | 2026-08-02 | Public domain or MIT, at your choice | header-only (`gfx/bitmap.cpp`) |

The two LGPL libraries stay separate DLLs, so they can be replaced by other builds of the same
version. Their sources are at the links above (the vcpkg ports name the exact archives).

## Not part of wkbai

- **Epic Online Services SDK**: closed, not included and not shipped. `plugins/eos/` builds a stub
  without it; a build with the SDK is for the builder's own use (see the note in `plugins/eos/`).
- **Nakama** (`server/`): a separate server program (Apache-2.0) run from its own Docker image; the
  repository holds only its configuration.
- **Warrior Kings: Battles** game data and executable: never included. Players need their own copy.
