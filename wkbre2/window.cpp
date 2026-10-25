// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#include "window.h"
#include <cstdio>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif
#include <SDL3/SDL.h>
#define SDL_MAIN_HANDLED // wkbre2 has its own main()
#include <SDL3/SDL_main.h>
#include "imgui/imgui.h"
#include "gfx/renderer.h"
#include "gfx/bitmap.h"

SDL_Window *g_sdlWindow = nullptr;
bool g_windowQuit = false;
int g_windowWidth = 1366, g_windowHeight = 768;
bool g_keyDown[SDL_SCANCODE_COUNT], g_keyPressed[SDL_SCANCODE_COUNT];
bool g_modCtrl = false, g_modShift = false, g_modAlt = false;
IRenderer *g_gfxRenderer = nullptr;
bool g_mouseDown[16], g_mousePressed[16], g_mouseReleased[16];
int g_mouseX, g_mouseY, g_mouseWheel;
int g_mouseDoubleClicked = 0;

void InitWindow()
{
#ifdef _WIN32
	// SDL3 always makes the process per-monitor DPI aware, which renders the game and its
	// pixel-sized UI at native resolution, i.e. smaller on scaled displays. Keep SDL2's
	// behaviour (Windows scales the window) until the renderer and UI handle DPI scaling.
	// The awareness can only be set once per process, so SDL's own attempt is then ignored.
	SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_UNAWARE);
#endif
	SDL_SetMainReady();
	SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
#ifndef _WIN32
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
	auto flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL;
#else
	auto flags = SDL_WINDOW_RESIZABLE;
#endif
	g_sdlWindow = SDL_CreateWindow("wkbre2 \"Next\"", g_windowWidth, g_windowHeight, flags); // SDL3 centers new windows
	SDL_StartTextInput(g_sdlWindow); // text input events are opt-in in SDL3 (ImGui text fields need them)
}

void *WndGetNativeWindowHandle()
{
#ifdef _WIN32
	return SDL_GetPointerProperty(SDL_GetWindowProperties(g_sdlWindow), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
#else
	return nullptr;
#endif
}

static ImGuiKey SdlToImGuiKey(SDL_Keycode code)
{
	switch (code) {
	case SDLK_TAB: return ImGuiKey_Tab;
	case SDLK_LEFT: return ImGuiKey_LeftArrow;
	case SDLK_RIGHT: return ImGuiKey_RightArrow;
	case SDLK_UP: return ImGuiKey_UpArrow;
	case SDLK_DOWN: return ImGuiKey_DownArrow;
	case SDLK_PAGEUP: return ImGuiKey_PageUp;
	case SDLK_PAGEDOWN: return ImGuiKey_PageDown;
	case SDLK_HOME: return ImGuiKey_Home;
	case SDLK_END: return ImGuiKey_End;
	case SDLK_DELETE: return ImGuiKey_Delete;
	case SDLK_BACKSPACE: return ImGuiKey_Backspace;
	case SDLK_SPACE: return ImGuiKey_Space;
	case SDLK_RETURN: return ImGuiKey_Enter;
	case SDLK_ESCAPE: return ImGuiKey_Escape;
	case SDLK_A: return ImGuiKey_A;
	case SDLK_C: return ImGuiKey_C;
	case SDLK_V: return ImGuiKey_V;
	case SDLK_X: return ImGuiKey_X;
	case SDLK_Y: return ImGuiKey_Y;
	case SDLK_Z: return ImGuiKey_Z;
	}
	return ImGuiKey_None;
}

void HandleImGui(const SDL_Event &event)
{
	ImGuiIO &igio = ImGui::GetIO();
	switch (event.type) {
	case SDL_EVENT_MOUSE_MOTION:
		igio.MousePos = ImVec2(event.motion.x, event.motion.y);
		break;
	case SDL_EVENT_MOUSE_BUTTON_DOWN:
		igio.MouseDown[0] = true;
		break;
	case SDL_EVENT_MOUSE_BUTTON_UP:
		igio.MouseDown[0] = false;
		break;
	case SDL_EVENT_MOUSE_WHEEL:
		igio.MouseWheel += event.wheel.y;
		break;
	case SDL_EVENT_KEY_DOWN:
	case SDL_EVENT_KEY_UP: {
		auto kmod = event.key.mod;
		igio.AddKeyEvent(ImGuiKey_ModCtrl, kmod & SDL_KMOD_CTRL);
		igio.AddKeyEvent(ImGuiKey_ModShift, kmod & SDL_KMOD_SHIFT);
		igio.AddKeyEvent(ImGuiKey_ModAlt, kmod & SDL_KMOD_ALT);
		igio.AddKeyEvent(SdlToImGuiKey(event.key.key), event.type == SDL_EVENT_KEY_DOWN);
		break;
	}
	case SDL_EVENT_TEXT_INPUT:
		igio.AddInputCharactersUTF8(event.text.text);
		break;
	}
}

void HandleWindow()
{
	SDL_Event event;
	bool imguion = ImGui::GetCurrentContext() != nullptr;
	for (int i = 0; i < SDL_SCANCODE_COUNT; i++)
		g_keyPressed[i] = false;
	for (bool& b : g_mousePressed)
		b = false;
	for (bool& b : g_mouseReleased)
		b = false;
	g_mouseWheel = 0;
	g_mouseDoubleClicked = 0;

	bool igWantsMouse = false;
	if (imguion) {
		ImGuiIO &io = ImGui::GetIO();
		igWantsMouse = io.WantCaptureMouse;
	}

	SDL_Keymod kmod = SDL_GetModState();
	g_modCtrl = kmod & SDL_KMOD_CTRL;
	g_modShift = kmod & SDL_KMOD_SHIFT;
	g_modAlt = kmod & SDL_KMOD_ALT;

	while (SDL_PollEvent(&event)) {
		if (imguion)
			HandleImGui(event);
		switch (event.type) {
		case SDL_EVENT_QUIT:
			g_windowQuit = true;
			break;
		case SDL_EVENT_KEY_DOWN:
			g_keyDown[event.key.scancode] = true;
			g_keyPressed[event.key.scancode] = true;
			break;
		case SDL_EVENT_KEY_UP:
			g_keyDown[event.key.scancode] = false; break;
		case SDL_EVENT_WINDOW_RESIZED:
			g_windowWidth = event.window.data1;
			g_windowHeight = event.window.data2;
			printf("Resized! %i %i\n", g_windowWidth, g_windowHeight);
			if (g_gfxRenderer)
				g_gfxRenderer->Reset();
			break;
		case SDL_EVENT_MOUSE_BUTTON_DOWN:
			if (!igWantsMouse) {
				g_mouseDown[event.button.button] = true;
				g_mousePressed[event.button.button] = true;
				if (event.button.clicks == 2)
					g_mouseDoubleClicked = event.button.button;
			}
			break;
		case SDL_EVENT_MOUSE_BUTTON_UP:
			if (!igWantsMouse) {
				g_mouseDown[event.button.button] = false;
				g_mouseReleased[event.button.button] = true;
			}
			break;
		case SDL_EVENT_MOUSE_MOTION:
			g_mouseX = (int)event.motion.x;
			g_mouseY = (int)event.motion.y;
			break;
		case SDL_EVENT_MOUSE_WHEEL:
			if(!igWantsMouse)
				g_mouseWheel += event.wheel.integer_y;
			//printf("mouse wheel %i\n", g_mouseWheel);
			break;
		}
	}
}

void SetRenderer(IRenderer *gfx)
{
	g_gfxRenderer = gfx;
}

struct WndCursor
{
	SDL_Cursor *sdlCursor = nullptr;
	int w = 0, h = 0;
};

WndCursor *WndCreateCursor(const char *path) {
	Bitmap cvtbmp = Bitmap::loadBitmap(path).convertToR8G8B8A8();
	int width = cvtbmp.width, height = cvtbmp.height;
	SDL_Surface *surface = SDL_CreateSurfaceFrom(cvtbmp.width, cvtbmp.height, SDL_PIXELFORMAT_RGBA32, cvtbmp.pixels.data(), cvtbmp.width * 4);
	SDL_Cursor *cursor = SDL_CreateColorCursor(surface, 0, 0);
	SDL_DestroySurface(surface);
	return new WndCursor { cursor, width, height };
}

WndCursor *WndCreateSystemCursor(int id) {
	return new WndCursor { SDL_CreateSystemCursor((SDL_SystemCursor)id), 24, 24 };
}

void WndSetCursor(WndCursor *cursor) {
	SDL_SetCursor(cursor->sdlCursor);
}