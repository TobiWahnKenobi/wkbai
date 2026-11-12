// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#include "window.h"
#include "interface/KeyBindings.h"
#include "AgentInput.h"
#include "AgentServer.h"
#include <cstdio>
#include <algorithm>
#include <string>
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
#include <tracy/Tracy.hpp>
#include "settings.h"
#include "gfx/RenderSettings.h"
#include <nlohmann/json.hpp>

SDL_Window *g_sdlWindow = nullptr;
bool g_windowQuit = false;
int g_windowWidth = 1366, g_windowHeight = 768;
bool g_keyDown[SDL_SCANCODE_COUNT], g_keyPressed[SDL_SCANCODE_COUNT];
bool g_modCtrl = false, g_modShift = false, g_modAlt = false;
IRenderer *g_gfxRenderer = nullptr;
bool g_mouseDown[16], g_mousePressed[16], g_mouseReleased[16];
int g_mouseX, g_mouseY, g_mouseWheel;
int g_mouseDoubleClicked = 0;
float g_uiScale = 1.0f;
std::string g_textTyped;

void InitWindow()
{
#ifdef _WIN32
	// SDL3 always makes the process per-monitor DPI aware, which renders the game and its
	// pixel-sized UI at native resolution, i.e. smaller on scaled displays. Keep SDL2's
	// behaviour (Windows scales the window) until the renderer and UI handle DPI scaling.
	// The awareness can only be set once per process, so SDL's own attempt is then ignored.
	// "dpiAware": true renders at the display's real resolution, with the interface scaled.
	if (!g_settings.value("dpiAware", false))
		SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_UNAWARE);
#endif
	SDL_SetMainReady();
	SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
	auto flags = SDL_WINDOW_RESIZABLE;
	// Initial size of the window's drawing area (the render resolution)
	g_windowWidth = g_settings.value<int>("windowWidth", g_windowWidth);
	g_windowHeight = g_settings.value<int>("windowHeight", g_windowHeight);
	g_sdlWindow = SDL_CreateWindow("wkbai", g_windowWidth, g_windowHeight, flags); // SDL3 centers new windows
	SDL_StartTextInput(g_sdlWindow); // text input events are opt-in in SDL3 (ImGui text fields need them)
	const float uiScale = g_settings.value("uiScale", 0.0f);
	g_uiScale = uiScale > 0.0f ? uiScale : g_settings.value("dpiAware", false) ? std::max(1.0f, SDL_GetWindowDisplayScale(g_sdlWindow)) : 1.0f;
	printf("Interface scale %.2f\n", g_uiScale);
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
		igio.MousePos = ImVec2(event.motion.x / g_uiScale, event.motion.y / g_uiScale);
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

// The window as RenderSettings::displayMode has it: windowed, borderless fullscreen (a desktop-
// sized window without a mode change) or fullscreen (the display switched to its desktop mode);
// done whenever the setting changes, the new size arriving as a resize event
static int g_appliedDisplayMode = 0;

static void ApplyDisplayMode(int mode)
{
	if (mode == 0)
		SDL_SetWindowFullscreen(g_sdlWindow, false);
	else {
		const SDL_DisplayMode* desktop = SDL_GetDesktopDisplayMode(SDL_GetDisplayForWindow(g_sdlWindow));
		SDL_SetWindowFullscreenMode(g_sdlWindow, mode == 2 ? desktop : nullptr);
		SDL_SetWindowFullscreen(g_sdlWindow, true);
	}
	SDL_SyncWindow(g_sdlWindow);
	g_appliedDisplayMode = mode;
	static const char* const names[3] = { "windowed", "borderless fullscreen", "fullscreen" };
	int w = 0, h = 0;
	SDL_GetWindowSizeInPixels(g_sdlWindow, &w, &h);
	printf("Display mode: %s, %i x %i\n", names[mode], w, h);
}

// The game's input state from one event: the operating system's and the agent's (AgentInput)
// alike, so the interface can't tell them apart
static void ProcessInputEvent(const SDL_Event& event, bool imguion, bool igWantsMouse)
{
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
	case SDL_EVENT_TEXT_INPUT:
		g_textTyped += event.text.text;
		break;
	case SDL_EVENT_MOUSE_WHEEL:
		if(!igWantsMouse)
			g_mouseWheel += event.wheel.integer_y;
		//printf("mouse wheel %i\n", g_mouseWheel);
		break;
	}
}

// a person's (or the agent's) hand on the mouse or keyboard
static bool IsInputEvent(const SDL_Event& event)
{
	switch (event.type) {
	case SDL_EVENT_KEY_DOWN: case SDL_EVENT_KEY_UP: case SDL_EVENT_TEXT_INPUT: case SDL_EVENT_TEXT_EDITING:
	case SDL_EVENT_MOUSE_MOTION: case SDL_EVENT_MOUSE_BUTTON_DOWN: case SDL_EVENT_MOUSE_BUTTON_UP: case SDL_EVENT_MOUSE_WHEEL:
		return true;
	default:
		return false;
	}
}

// the take-over key (KeyAction::AgentTakeOver), from the operating system's keyboard
static bool IsTakeOverKey(const SDL_Event& event)
{
	if (event.type != SDL_EVENT_KEY_DOWN || event.key.repeat)
		return false;
	const int mods = ((event.key.mod & SDL_KMOD_CTRL) ? KeyChord::CTRL : 0) | ((event.key.mod & SDL_KMOD_SHIFT) ? KeyChord::SHIFT : 0)
		| ((event.key.mod & SDL_KMOD_ALT) ? KeyChord::ALT : 0);
	for (int slot = 0; slot < KEY_SLOTS; slot++) {
		const KeyChord c = GetKeyBinding(KeyAction::AgentTakeOver, slot);
		if (!c.empty() && c.key == (int)event.key.scancode && c.mods == mods)
			return true;
	}
	return false;
}

// the input changes hands: nothing held stays held (a button down when the agent took over
// would otherwise never come up)
static void LetGoOfInput(bool imguion)
{
	std::fill(std::begin(g_keyDown), std::end(g_keyDown), false);
	std::fill(std::begin(g_mouseDown), std::end(g_mouseDown), false);
	if (imguion) {
		ImGuiIO& io = ImGui::GetIO();
		io.MouseDown[0] = false;
		io.AddKeyEvent(ImGuiKey_ModCtrl, false);
		io.AddKeyEvent(ImGuiKey_ModShift, false);
		io.AddKeyEvent(ImGuiKey_ModAlt, false);
	}
}

int InputModState()
{
	return AgentInput::HasInput() ? AgentInput::Mods() : (int)SDL_GetModState();
}

void HandleWindow()
{
	FrameMark;
	ZoneScoped;
	if (g_renderSettings.displayMode != g_appliedDisplayMode)
		ApplyDisplayMode(std::clamp(g_renderSettings.displayMode, 0, 2));
	SDL_Event event;
	bool imguion = ImGui::GetCurrentContext() != nullptr;
	for (int i = 0; i < SDL_SCANCODE_COUNT; i++)
		g_keyPressed[i] = false;
	KeyBindingsNewFrame();
	for (bool& b : g_mousePressed)
		b = false;
	for (bool& b : g_mouseReleased)
		b = false;
	g_mouseWheel = 0;
	g_mouseDoubleClicked = 0;
	g_textTyped.clear();

	bool igWantsMouse = false;
	if (imguion) {
		ImGuiIO &io = ImGui::GetIO();
		igWantsMouse = io.WantCaptureMouse;
	}

	while (SDL_PollEvent(&event)) {
		if (AgentInput::Enabled() && IsTakeOverKey(event)) {
			AgentInput::SetTakenOver(!AgentInput::TakenOver());
			LetGoOfInput(imguion);
			printf("Agent control: %s\n", AgentInput::TakenOver() ? "the input taken over at the keyboard" : "the input given back to the agent");
			fflush(stdout);
			continue;
		}
		// the agent has the input: the operating system's mouse and keyboard don't add to it
		if (AgentInput::HasInput() && IsInputEvent(event))
			continue;
		ProcessInputEvent(event, imguion, igWantsMouse);
	}
	// the agent's connection (replies for the frames run, new requests), then its frame, through
	// the same function
	AgentServer::Poll();
	static std::vector<SDL_Event> agentEvents;
	if (AgentInput::TakeFrame(agentEvents))
		for (const SDL_Event& e : agentEvents)
			ProcessInputEvent(e, imguion, igWantsMouse);

	const int kmod = InputModState();
	g_modCtrl = kmod & SDL_KMOD_CTRL;
	g_modShift = kmod & SDL_KMOD_SHIFT;
	g_modAlt = kmod & SDL_KMOD_ALT;
}

void SetRenderer(IRenderer *gfx)
{
	g_gfxRenderer = gfx;
}

struct WndCursor
{
	SDL_Cursor *sdlCursor = nullptr;
	int w = 0, h = 0;
	std::string path;
};

WndCursor *WndCreateCursor(const char *path) {
	Bitmap cvtbmp = Bitmap::loadBitmap(path).convertToR8G8B8A8();
	int width = cvtbmp.width, height = cvtbmp.height;
	SDL_Surface *surface = SDL_CreateSurfaceFrom(cvtbmp.width, cvtbmp.height, SDL_PIXELFORMAT_RGBA32, cvtbmp.pixels.data(), cvtbmp.width * 4);
	SDL_Cursor *cursor = SDL_CreateColorCursor(surface, 0, 0);
	SDL_DestroySurface(surface);
	return new WndCursor { cursor, width, height, path };
}

WndCursor *WndCreateSystemCursor(int id) {
	return new WndCursor { SDL_CreateSystemCursor((SDL_SystemCursor)id), 24, 24 };
}

const char *WndCursorPath(const WndCursor *cursor) {
	return cursor ? cursor->path.c_str() : "";
}

void WndCursorSize(const WndCursor *cursor, int &width, int &height) {
	width = cursor ? cursor->w : 0;
	height = cursor ? cursor->h : 0;
}

void WndSetCursor(WndCursor *cursor) {
	SDL_SetCursor(cursor->sdlCursor);
}