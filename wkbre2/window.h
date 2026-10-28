// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#pragma once

#include <string>

struct SDL_Window;
struct IRenderer;

extern SDL_Window *g_sdlWindow;
extern bool g_windowQuit;
extern int g_windowWidth, g_windowHeight;
extern bool g_keyDown[512], g_keyPressed[512];
extern bool g_modCtrl, g_modShift, g_modAlt;
// The interface's scale (menus, HUD, ImGui windows): they lay out in points, the window is in
// pixels, points = pixels / g_uiScale. "uiScale" in wkconfig.json, else the display's scale when
// "dpiAware" is on (the window then has the display's real pixels), else 1.
extern float g_uiScale;
extern IRenderer *g_gfxRenderer;
extern bool g_mouseDown[16], g_mousePressed[16], g_mouseReleased[16];
extern int g_mouseX, g_mouseY, g_mouseWheel;
extern int g_mouseDoubleClicked;
// the mouse in points (window.h, g_uiScale)
inline int UiMouseX() { return (int)(g_mouseX / g_uiScale); }
inline int UiMouseY() { return (int)(g_mouseY / g_uiScale); }
// The text typed since the last frame (UTF-8)
extern std::string g_textTyped;

void InitWindow();
void HandleWindow();
void SetRenderer(IRenderer *gfx);
// Native handle of the game window (HWND on Windows, nullptr elsewhere)
void *WndGetNativeWindowHandle();

struct WndCursor;

WndCursor *WndCreateCursor(const char *path);
WndCursor *WndCreateSystemCursor(int id);
void WndSetCursor(WndCursor *cursor);
