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
find_package(glew CONFIG REQUIRED)
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
"gameset/finder.cpp" "gameset/finder.h" "window.cpp" "window.h" "util/vecmat.cpp" "util/vecmat.h" "gfx/bitmap.cpp" "gfx/bitmap.h" "gfx/renderer.h" "gfx/renderer_d3d9.cpp"
"imguiimpl.cpp" "imguiimpl.h" "terrain.cpp" "terrain.h" "TrnTextureDb.cpp" "TrnTextureDb.h" "gfx/TextureCache.cpp" "gfx/TextureCache.h" "mesh.cpp" "mesh.h" "network.cpp" "network.h"
"util/DynArray.h" "netenetlink.cpp" "netenetlink.h" "gfx/SceneRenderer.h" "gfx/DefaultSceneRenderer.cpp" "gfx/DefaultSceneRenderer.h" "gameset/command.cpp" "gameset/command.h"
"interface/ServerDebugger.cpp" "interface/ServerDebugger.h" "interface/ClientDebugger.cpp" "interface/ClientDebugger.h" "anim.cpp" "anim.h" "gfx/TerrainRenderer.h"
"gfx/DefaultTerrainRenderer.cpp" "gfx/DefaultTerrainRenderer.h" "Camera.cpp" "Camera.h" "interface/ClientInterface.cpp" "interface/ClientInterface.h" "Model.cpp" "Model.h"
"scene.cpp" "scene.h" "TimeManager.cpp" "TimeManager.h" "gameset/OrderBlueprint.cpp" "gameset/OrderBlueprint.h" "Order.cpp" "Order.h" "Movement.cpp" "Movement.h" "GameObjectRef.h"
"gameset/CommonEval.h" "gameset/position.cpp" "gameset/position.h" "gameset/reaction.cpp" "gameset/reaction.h" "gameset/ObjectCreation.cpp" "gameset/ObjectCreation.h"
"NNSearch.cpp" "NNSearch.h" "gameset/ScriptContext.cpp" "gameset/ScriptContext.h" "gameset/condition.cpp" "gameset/condition.h" "gameset/GameTextWindow.h" "gameset/GameTextWindow.cpp"
"gameset/Package.h" "gameset/Package.cpp" "gameset/3DClip.h" "gameset/3DClip.cpp" "settings.cpp" "settings.h" "gameset/cameraPath.h" "gameset/cameraPath.cpp" "Language.h" "Language.cpp"
"Trajectory.h" "Trajectory.cpp" "interface/GameSetDebugger.h" "interface/GameSetDebugger.cpp" "SoundPlayer.h" "SoundPlayer.cpp" "WavDocument.h" "WavDocument.cpp" "gameset/Sound.h"
"gameset/Sound.cpp" "interface/QuickStartMenu.h" "interface/QuickStartMenu.cpp" "resources.rc" "gameset/Footprint.h" "gameset/Footprint.cpp" "gameset/GSTerrain.h" "gameset/GSTerrain.cpp"
"gfx/renderer_d3d11.cpp" "gfx/renderer.cpp" "Pathfinding.h" "MovementController.h" "MovementController.cpp" "ParticleSystem.h" "ParticleSystem.cpp" "ParticleContainer.h"
"ParticleContainer.cpp" "gfx/ParticleRenderer.h" "gfx/DefaultParticleRenderer.h" "gfx/DefaultParticleRenderer.cpp" "gfx/renderer_ogl3.cpp" "gfx/D3D11EnhancedTerrainRenderer.cpp"
"gfx/D3D11EnhancedTerrainRenderer.h" "gfx/renderer_d3d11.h" "gfx/D3D11EnhancedSceneRenderer.h" "gfx/D3D11EnhancedSceneRenderer.cpp" "gameset/Plan.cpp" "gameset/Plan.h"  "AIController.h" "AIController.cpp"
"gameset/ArmyCreationSchedule.h" "gameset/ArmyCreationSchedule.cpp" "gameset/WorkOrder.h" "gameset/WorkOrder.cpp" "common.cpp" "gameset/Commission.h" "gameset/Commission.cpp"
"FormationController.h" "FormationController.cpp" "StampdownPlan.h" "StampdownPlan.cpp" "BreakpointManager.h" "BreakpointManager.cpp" "interface/QuickSkirmishMenu.h" "interface/QuickSkirmishMenu.cpp" "interface/ModMenu.h" "interface/ModMenu.cpp"
"platform.cpp" "gfx/TerrainSpriteContainer.cpp" "gfx/TerrainSpriteContainer.h" "gfx/TerrainSpriteRenderer.h" "gfx/TerrainSpriteRenderer.cpp" "util/FrameStats.h" "util/FrameStats.cpp")
target_link_libraries (wkbre2
  imgui
  SDL3::SDL3
  ${ENET_LIBRARY}
  BZip2::BZip2
  nlohmann_json::nlohmann_json
  OpenAL::OpenAL
  MPG123::libmpg123
  $<$<BOOL:${USE_DISCORD}>:${DISCORDRPC_LIBRARY}>
  GLEW::GLEW
  sol2::sol2
  ${LUA_LIBRARIES}
  Tracy::TracyClient
  $<$<PLATFORM_ID:Windows>:
    d3d9
    ws2_32
    winmm
    d3d11
    d3dcompiler
    opengl32
    dxgi
  >
)
target_include_directories(wkbre2
  PRIVATE
  ${ENET_INCLUDE_DIR}
  $<$<BOOL:${USE_DISCORD}>:${DISCORDRPC_INCLUDE_DIR}>
  ${LUA_INCLUDE_DIR}
)

# TODO: Ajoutez des tests et installez des cibles si nécessaire.
