# wkbai's Epic Online Services matchmaking plugin (wkbai_eos.dll), loaded at run time from the
# "plugins" folder beside wkbai.exe. Separate from the exe because the EOS SDK is closed and
# wkbai is GPL-3: this plugin's code and the interface it implements are MIT (see eos_plugin.cpp).
#
# Without the SDK it builds as a stub that says so in the game list. With it:
#   cmake -DEOS_SDK_DIR=C:/path/to/EOS-SDK/SDK ...
# (the folder with Include/, Lib/ and Bin/ of Epic's "EOS SDK for C" download).
# Built from wkbai's own CMakeLists (WKBAI_EOS_PLUGIN) or on its own.

cmake_minimum_required(VERSION 3.8)
if(NOT DEFINED PROJECT_NAME)
  project(wkbai_eos CXX)
endif()
set(CMAKE_CXX_STANDARD 20)

find_package(nlohmann_json CONFIG REQUIRED)

set(EOS_SDK_DIR "" CACHE PATH "Epic Online Services SDK folder (with Include, Lib, Bin); empty: the plugin is a stub")

add_library(wkbai_eos SHARED eos_plugin.cpp "${CMAKE_CURRENT_SOURCE_DIR}/../../wkbai/matchmaking/wkbai_mm_plugin.h")
target_include_directories(wkbai_eos PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../wkbai/matchmaking")
target_link_libraries(wkbai_eos PRIVATE nlohmann_json::nlohmann_json)

if(EOS_SDK_DIR AND EXISTS "${EOS_SDK_DIR}/Include/eos_sdk.h")
  message(STATUS "wkbai_eos: with the EOS SDK in ${EOS_SDK_DIR}")
  target_compile_definitions(wkbai_eos PRIVATE WKBAI_HAVE_EOS_SDK)
  target_include_directories(wkbai_eos PRIVATE "${EOS_SDK_DIR}/Include")
  if(WIN32)
    target_link_libraries(wkbai_eos PRIVATE "${EOS_SDK_DIR}/Lib/EOSSDK-Win64-Shipping.lib")
    # the SDK's own DLL goes beside the plugin
    add_custom_command(TARGET wkbai_eos POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "${EOS_SDK_DIR}/Bin/EOSSDK-Win64-Shipping.dll" "$<TARGET_FILE_DIR:wkbai_eos>")
  elseif(APPLE)
    target_link_libraries(wkbai_eos PRIVATE "${EOS_SDK_DIR}/Bin/libEOSSDK-Mac-Shipping.dylib")
  else()
    target_link_libraries(wkbai_eos PRIVATE "${EOS_SDK_DIR}/Bin/libEOSSDK-Linux-Shipping.so")
  endif()
else()
  message(STATUS "wkbai_eos: no EOS SDK (EOS_SDK_DIR): built as a stub")
endif()
