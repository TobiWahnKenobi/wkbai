// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#pragma once

struct IRenderer;

extern char *imguiFontFile;
extern float imguiFontSize;

void ImGuiImpl_Init();
void ImGuiImpl_CreateFontsTexture(IRenderer *gfx);
void ImGuiImpl_NewFrame();
// agentShot: the agent's screenshot taken here (false: the caller takes it after drawing more,
// as the game view does after the selection window's portrait)
void ImGuiImpl_Render(IRenderer *gfx, bool agentShot = true);
