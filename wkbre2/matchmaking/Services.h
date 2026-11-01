// wkbai - matchmaking: the services there are
// Licensed under the GNU General Public License 3

#pragma once

#include "Matchmaking.h"
#include <filesystem>

// The local network: hosts answer a query broadcast to the game port (EnetTransport.h)
std::unique_ptr<MatchmakingService> CreateLocalService();
// A Nakama server (wkconfig.json "matchmaking": { "nakama": { "server": "http://host:7350", ... } })
std::unique_ptr<MatchmakingService> CreateNakamaService();
// A plugin library (wkbai_mm_plugin.h), or null when it isn't one
std::unique_ptr<MatchmakingService> LoadPluginService(const std::filesystem::path& library);
