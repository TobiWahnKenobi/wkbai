// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#include "renderer.h"
#include "../settings.h"
#include <nlohmann/json.hpp>
#include <cstdio>
#include <string>

// "renderer": "diligent" (Direct3D 12 or Vulkan, see "diligentBackend") or "null" (draws
// nothing). The old values "d3d9", "d3d11", "ogl1" and "ogl3" select Diligent: those
// renderers were removed.
IRenderer* CreateRenderer() {
	std::string setting = g_settings.value("renderer", "diligent");
	if (setting == "null")
		return CreateNULLRenderer();
	if (setting != "diligent")
		printf("Renderer \"%s\" no longer exists, using \"diligent\".\n", setting.c_str());
	return CreateDiligentRenderer();
}