# CMakeList.txt : projet CMake pour wkbre2, incluez la source et définissez
# la logique spécifique au projet ici.
#
cmake_minimum_required (VERSION 3.8)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory ("imgui")

find_package (BZip2 REQUIRED)
find_package (SDL3 CONFIG REQUIRED)
find_package (nlohmann_json CONFIG REQUIRED)
find_package(OpenAL CONFIG REQUIRED)
find_package(mpg123 CONFIG REQUIRED)
find_package(sol2 CONFIG REQUIRED)
find_package(Lua REQUIRED)

# Find enet
find_path(ENET_INCLUDE_DIR enet/enet.h)
if(DEFINED VCPKG_INSTALLED_DIR)
  # vcpkg's enet has no CMake config, and a plain find_library can pick the debug
  # build for release configurations, so look for both and select per configuration.
  find_library(ENET_LIBRARY_RELEASE enet PATHS "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib" NO_DEFAULT_PATH)
  find_library(ENET_LIBRARY_DEBUG enet PATHS "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/lib" NO_DEFAULT_PATH)
  include(SelectLibraryConfigurations)
  select_library_configurations(ENET)
else()
  find_library(ENET_LIBRARY enet)
endif()
message("${ENET_LIBRARY}")
message(${ENET_INCLUDE_DIR})

# Tracy profiler client (https://github.com/wolfpld/tracy). Built from source rather than vcpkg
# so the client can be limited to localhost (no firewall prompt) and only collect while a
# profiler is connected (on-demand), which keeps the overhead negligible otherwise.
option(WKBRE2_PROFILING "Build with the Tracy profiler client" ON)
include(FetchContent)
set(TRACY_STATIC ON CACHE BOOL "" FORCE)
set(TRACY_ENABLE ${WKBRE2_PROFILING} CACHE BOOL "" FORCE)
set(TRACY_ON_DEMAND ON CACHE BOOL "" FORCE)
set(TRACY_ONLY_LOCALHOST ON CACHE BOOL "" FORCE)
set(TRACY_NO_BROADCAST ON CACHE BOOL "" FORCE)
set(TRACY_NO_CRASH_HANDLER ON CACHE BOOL "" FORCE)
FetchContent_Declare(tracy
  URL https://github.com/wolfpld/tracy/archive/refs/tags/v0.14.1.tar.gz
  URL_HASH SHA256=bf4af567e9c7524d07f3caa745fad02fb33bd5694f11910750382d1efbb251c1
  EXCLUDE_FROM_ALL)
FetchContent_MakeAvailable(tracy)

# Diligent Engine (https://github.com/DiligentGraphics/DiligentCore), the render hardware
# interface of the renderer, with only the Direct3D 12 and Vulkan backends.
# Not in vcpkg, so it is fetched at a pinned commit. To reuse a local clone, configure with
# -DFETCHCONTENT_SOURCE_DIR_DILIGENTCORE=<path>.
# The renderer only gets its window from Win32 so far (the old OpenGL renderer, the only one
# for other systems, was removed).
if(NOT WIN32)
  message(FATAL_ERROR "wkbai currently builds on Windows only: the Diligent renderer needs a Linux (X11/Wayland) window path first.")
endif()
set(DILIGENT_NO_DIRECT3D11 ON CACHE BOOL "" FORCE)
set(DILIGENT_NO_OPENGL ON CACHE BOOL "" FORCE)
set(DILIGENT_NO_METAL ON CACHE BOOL "" FORCE)
set(DILIGENT_NO_WEBGPU ON CACHE BOOL "" FORCE)
set(DILIGENT_NO_ARCHIVER ON CACHE BOOL "" FORCE)
set(DILIGENT_NO_SUPER_RESOLUTION ON CACHE BOOL "" FORCE)
set(DILIGENT_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(DILIGENT_INSTALL_CORE OFF CACHE BOOL "" FORCE)
FetchContent_Declare(DiligentCore
  GIT_REPOSITORY https://github.com/DiligentGraphics/DiligentCore.git
  GIT_TAG cd2329b912aa8add6a4cac538424756534266104
  GIT_SUBMODULES_RECURSE ON
  EXCLUDE_FROM_ALL)
FetchContent_MakeAvailable(DiligentCore)

# Find Discord-RPC
option(USE_DISCORD "Enables the most useless feature of all time. Setting it up and using it will waste tons of time. Not recommended if you want to keep your precious time." OFF)
if(${USE_DISCORD})
  find_path(DISCORDRPC_INCLUDE_DIR discord_rpc.h)
  find_library(DISCORDRPC_LIBRARY discord-rpc)
  add_compile_definitions(HAS_DISCORD)
endif()

# Ajoutez une source à l'exécutable de ce projet.
add_executable (wkbre2 "wkbre2.cpp" "wkbre2.h" "file.cpp" "file.h" "lzrw3.c" "lzrw_headers.h" "util/util.cpp" "util/util.h" "util/GSFileParser.cpp" "util/GSFileParser.h"
  "gameset/gameset.cpp" "gameset/gameset.h" "tags.cpp" "tags.h" "util/TagDict.h" "gameset/GameObjBlueprint.cpp" "gameset/GameObjBlueprint.h"
"util/IndexedStringList.h" "gameset/values.cpp" "gameset/values.h" "test.cpp" "server.cpp" "server.h" "client.cpp" "client.h" "common.h" "gameset/actions.cpp" "gameset/actions.h"
"gameset/finder.cpp" "gameset/finder.h" "window.cpp" "window.h" "util/vecmat.cpp" "util/vecmat.h" "gfx/bitmap.cpp" "gfx/bitmap.h" "gfx/renderer.h" "gfx/renderer_null.cpp"
"imguiimpl.cpp" "imguiimpl.h" "terrain.cpp" "terrain.h" "TrnTextureDb.cpp" "TrnTextureDb.h" "gfx/TextureCache.cpp" "gfx/TextureCache.h" "mesh.cpp" "mesh.h" "gltf.cpp" "network.cpp" "network.h"
"util/DynArray.h" "netenetlink.cpp" "netenetlink.h" "gfx/SceneRenderer.h" "gfx/DefaultSceneRenderer.cpp" "gfx/DefaultSceneRenderer.h" "gameset/command.cpp" "gameset/command.h"
"interface/ServerDebugger.cpp" "interface/ServerDebugger.h" "interface/ClientDebugger.cpp" "interface/ClientDebugger.h" "anim.cpp" "anim.h" "gfx/TerrainRenderer.h"
"gfx/DefaultTerrainRenderer.cpp" "gfx/DefaultTerrainRenderer.h" "Camera.cpp" "Camera.h" "interface/ClientInterface.cpp" "interface/ClientInterface.h" "interface/GameChronicle.cpp" "interface/GameChronicle.h" "interface/MapRegions.cpp" "interface/MapRegions.h" "Model.cpp" "Model.h"
"scene.cpp" "scene.h" "TimeManager.cpp" "TimeManager.h" "gameset/OrderBlueprint.cpp" "gameset/OrderBlueprint.h" "Order.cpp" "Order.h" "Movement.cpp" "Movement.h" "GameObjectRef.h"
"gameset/CommonEval.h" "gameset/position.cpp" "gameset/position.h" "gameset/reaction.cpp" "gameset/reaction.h" "gameset/ObjectCreation.cpp" "gameset/ObjectCreation.h"
"NNSearch.cpp" "NNSearch.h" "gameset/ScriptContext.cpp" "gameset/ScriptContext.h" "gameset/condition.cpp" "gameset/condition.h" "gameset/GameTextWindow.h" "gameset/GameTextWindow.cpp"
"gameset/Package.h" "gameset/Package.cpp" "gameset/3DClip.h" "gameset/3DClip.cpp" "settings.cpp" "settings.h" "gameset/cameraPath.h" "gameset/cameraPath.cpp" "Language.h" "Language.cpp"
"Trajectory.h" "Trajectory.cpp" "interface/GameSetDebugger.h" "interface/GameSetDebugger.cpp" "SoundPlayer.h" "SoundPlayer.cpp" "WavDocument.h" "WavDocument.cpp" "gameset/Sound.h"
"gameset/Sound.cpp" "interface/QuickStartMenu.h" "interface/QuickStartMenu.cpp" "resources.rc" "gameset/Footprint.h" "gameset/Footprint.cpp" "gameset/GSTerrain.h" "gameset/GSTerrain.cpp"
"gfx/renderer.cpp" "Pathfinding.h" "MovementController.h" "MovementController.cpp" "ParticleSystem.h" "ParticleSystem.cpp" "ParticleContainer.h"
"ParticleContainer.cpp" "gfx/ParticleRenderer.h" "gfx/DefaultParticleRenderer.h" "gfx/DefaultParticleRenderer.cpp"
"gameset/Plan.cpp" "gameset/Plan.h"  "AIController.h" "AIController.cpp"
"gameset/ArmyCreationSchedule.h" "gameset/ArmyCreationSchedule.cpp" "gameset/WorkOrder.h" "gameset/WorkOrder.cpp" "common.cpp" "gameset/Commission.h" "gameset/Commission.cpp"
"FormationController.h" "FormationController.cpp" "StampdownPlan.h" "StampdownPlan.cpp" "BreakpointManager.h" "BreakpointManager.cpp" "interface/QuickSkirmishMenu.h" "interface/QuickSkirmishMenu.cpp" "interface/ModMenu.h" "interface/ModMenu.cpp" "interface/MapEditor.cpp" "interface/FogOfWar.h" "interface/FogOfWar.cpp"
"platform.cpp" "gfx/TerrainSpriteContainer.cpp" "gfx/TerrainSpriteContainer.h" "gfx/TerrainSpriteRenderer.h" "gfx/TerrainSpriteRenderer.cpp" "util/FrameStats.h" "util/FrameStats.cpp" "gfx/renderer_diligent.cpp" "gfx/renderer_diligent.h"
"gfx/DiligentEnhancedSceneRenderer.h" "gfx/DiligentEnhancedSceneRenderer.cpp" "gfx/DiligentEnhancedTerrainRenderer.h" "gfx/DiligentEnhancedTerrainRenderer.cpp" "gfx/DiligentSkyRenderer.h" "gfx/DiligentSkyRenderer.cpp" "gfx/DiligentPostProcess.h" "gfx/DiligentPostProcess.cpp" "gfx/DiligentFsr3.h" "gfx/DiligentFsr3.cpp" "gfx/DiligentWaterfalls.h" "gfx/DiligentWaterfalls.cpp" "gfx/RenderSettings.h" "gfx/RenderSettings.cpp" "gfx/BitmapFont.h" "gfx/BitmapFont.cpp" "interface/FrontEnd.h" "interface/FrontEnd.cpp" "interface/MenuOptions.h" "interface/MenuOptions.cpp" "ServerSave.cpp" "interface/CampaignProfile.h" "interface/CampaignProfile.cpp"
"gfx/EnhancedTerrainData.h" "gfx/EnhancedTerrainData.cpp")
target_link_libraries (wkbre2
  imgui
  SDL3::SDL3
  ${ENET_LIBRARY}
  BZip2::BZip2
  nlohmann_json::nlohmann_json
  OpenAL::OpenAL
  MPG123::libmpg123
  $<$<BOOL:${USE_DISCORD}>:${DISCORDRPC_LIBRARY}>
  sol2::sol2
  ${LUA_LIBRARIES}
  Tracy::TracyClient
  Diligent-GraphicsEngineD3D12-static
  Diligent-GraphicsEngineVk-static
  Diligent-Common
  Diligent-GraphicsTools
  Vulkan::Headers
  ws2_32
  winmm
)

# AMD FidelityFX FSR 3.1's upscaler shaders (FidelityFX/: the SDK 1.1.4's passes and the headers
# they include, MIT licence), embedded in the exe as byte arrays (FidelityFXShaders.inc), each
# under the names the includes use: its path in gpu/ and its file name. Regenerated when one
# changes.
file(GLOB_RECURSE FFX_SHADER_FILES RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}/FidelityFX" CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/FidelityFX/gpu/*.h" "${CMAKE_CURRENT_SOURCE_DIR}/FidelityFX/passes/*.hlsl")
set(FFX_BYTES_PER_LINE "")
foreach(i RANGE 31)
  string(APPEND FFX_BYTES_PER_LINE "0x[0-9a-f][0-9a-f],")
endforeach()
set(FFX_ARRAYS "")
set(FFX_ENTRIES "")
set(FFX_INDEX 0)
foreach(FFX_FILE ${FFX_SHADER_FILES})
  file(READ "${CMAKE_CURRENT_SOURCE_DIR}/FidelityFX/${FFX_FILE}" FFX_HEX HEX)
  string(LENGTH "${FFX_HEX}" FFX_LENGTH)
  math(EXPR FFX_LENGTH "${FFX_LENGTH} / 2")
  string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," FFX_HEX "${FFX_HEX}")
  string(REGEX REPLACE "(${FFX_BYTES_PER_LINE})" "\\1\n" FFX_HEX "${FFX_HEX}")
  string(APPEND FFX_ARRAYS "static const unsigned char ffxShader${FFX_INDEX}[] = {\n${FFX_HEX}0 };\n")
  string(REGEX REPLACE "^(gpu|passes)/" "" FFX_NAME "${FFX_FILE}")
  get_filename_component(FFX_BARE "${FFX_FILE}" NAME)
  string(APPEND FFX_ENTRIES "\t{ \"${FFX_NAME}\", ffxShader${FFX_INDEX}, ${FFX_LENGTH} },\n")
  if(NOT FFX_BARE STREQUAL FFX_NAME)
    string(APPEND FFX_ENTRIES "\t{ \"${FFX_BARE}\", ffxShader${FFX_INDEX}, ${FFX_LENGTH} },\n")
  endif()
  math(EXPR FFX_INDEX "${FFX_INDEX} + 1")
endforeach()
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/FidelityFXShaders.inc"
  CONTENT "// generated by CMakeLists.txt from FidelityFX/\n${FFX_ARRAYS}struct FfxShaderFile { const char* name; const unsigned char* data; unsigned length; };\nstatic const FfxShaderFile ffxShaderFiles[] = {\n${FFX_ENTRIES}};\n"
  @ONLY)

target_include_directories(wkbre2
  PRIVATE
  "${CMAKE_CURRENT_BINARY_DIR}"
  ${ENET_INCLUDE_DIR}
  $<$<BOOL:${USE_DISCORD}>:${DISCORDRPC_INCLUDE_DIR}>
  ${LUA_INCLUDE_DIR}
)

# The Lua UI scripts are read from "redata" beside the exe (the working directory): copy them on
# every build, so a run from the build folder never uses an older copy
# the exe is wkbai.exe (the fork's name since 2026-10-31; the target and folders keep wkbre2's)
set_target_properties(wkbre2 PROPERTIES OUTPUT_NAME "wkbai")

# the exe is wkbai.exe (the fork's name since 2026-10-31; the target and folders keep wkbre2's)
set_target_properties(wkbre2 PROPERTIES OUTPUT_NAME "wkbai")

# the exe is wkbai.exe (the fork's name since 2026-10-31; the target and folders keep wkbre2's)
set_target_properties(wkbre2 PROPERTIES OUTPUT_NAME "wkbai")

add_custom_command(TARGET wkbre2 POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/redata" "$<TARGET_FILE_DIR:wkbre2>/redata"
)

# TODO: Ajoutez des tests et installez des cibles si nécessaire.
