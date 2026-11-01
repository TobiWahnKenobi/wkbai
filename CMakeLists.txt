# CMakeList.txt : fichier projet CMake de niveau supérieur, effectuez une configuration globale
# et incluez les sous-projets ici.
#
cmake_minimum_required (VERSION 3.8)

project ("wkbai")

# Incluez les sous-projets.
add_subdirectory ("wkbre2")

# Matchmaking plugins (wkbre2/matchmaking/wkbai_mm_plugin.h), built into a "plugins" folder
# beside the exe. Epic Online Services: a stub unless EOS_SDK_DIR names Epic's SDK.
option(WKBAI_EOS_PLUGIN "Build the Epic Online Services matchmaking plugin (wkbai_eos)" ON)
if(WKBAI_EOS_PLUGIN)
  add_subdirectory ("plugins/eos")
  set_target_properties(wkbai_eos PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/wkbre2/$<CONFIG>/plugins")
  add_dependencies(wkbre2 wkbai_eos)
endif()
