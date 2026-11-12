// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#include "KeyBindings.h"
#include "../window.h"
#include "../settings.h"
#include "../util/FrameStats.h"
#include <SDL3/SDL.h>
#include <nlohmann/json.hpp>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
	using K = KeyChord;
	constexpr K key(SDL_Scancode sc, int mods = 0) { return K{ (int)sc, mods }; }
	constexpr K none() { return K{}; }

	const std::vector<KeyActionInfo>& table()
	{
		using A = KeyAction;
		using P = KeyPlace;
		static const char* const CAMERA = "Camera";
		static const char* const ORDERS = "Selection and orders";
		static const char* const GROUPS = "Control groups";
		static const char* const GAME = "Game";
		static const char* const WINDOWS = "Windows and tools";
		static const char* const EDITOR = "Map editor (F8)";
		static const char* const REPLAY = "Replays";
		static const char* const CHEATS = "Cheat keys (cheat mode only)";
		static const std::vector<KeyActionInfo> list = [] {
			std::vector<KeyActionInfo> l = {
				{ A::CameraForward, "cameraForward", "Scroll forward", CAMERA, P::ALWAYS, true, false, { key(SDL_SCANCODE_UP), key(SDL_SCANCODE_W) } },
				{ A::CameraBack, "cameraBack", "Scroll back", CAMERA, P::ALWAYS, true, false, { key(SDL_SCANCODE_DOWN), key(SDL_SCANCODE_S) } },
				{ A::CameraLeft, "cameraLeft", "Scroll left", CAMERA, P::ALWAYS, true, false, { key(SDL_SCANCODE_LEFT), key(SDL_SCANCODE_A) } },
				{ A::CameraRight, "cameraRight", "Scroll right", CAMERA, P::ALWAYS, true, false, { key(SDL_SCANCODE_RIGHT), key(SDL_SCANCODE_D) } },
				{ A::CameraFast, "cameraFast", "Scroll faster (hold)", CAMERA, P::ALWAYS, true, false, { key(SDL_SCANCODE_LSHIFT), key(SDL_SCANCODE_RSHIFT) } },
				{ A::CameraRotate, "cameraRotate", "Turn the camera (hold, move the mouse)", CAMERA, P::ALWAYS, true, false, { key(SDL_SCANCODE_KP_0), none() } },
				{ A::CameraToSelection, "cameraToSelection", "Jump to the selection (again: the next unit)", CAMERA, P::GAME, false, false, { key(SDL_SCANCODE_SPACE), none() } },

				{ A::NextCommand, "nextCommand", "Next command at the cursor", ORDERS, P::GAME, false, false, { key(SDL_SCANCODE_E), none() } },
				{ A::DeleteSelected, "deleteSelected", "Destroy the selection", ORDERS, P::GAME, false, false, { key(SDL_SCANCODE_DELETE), none() } },
				{ A::TurnBuildingLeft, "turnBuildingLeft", "Turn the building being placed (left)", ORDERS, P::GAME, false, false, { key(SDL_SCANCODE_LEFTBRACKET), none() } },
				{ A::TurnBuildingRight, "turnBuildingRight", "Turn the building being placed (right)", ORDERS, P::GAME, false, false, { key(SDL_SCANCODE_RIGHTBRACKET), none() } },
			};
			// the control groups: Ctrl+1-9, 0 (and the keypad's 1-9; keypad 0 turns the camera)
			static const char* const storeIds[10] = { "groupStore1", "groupStore2", "groupStore3", "groupStore4", "groupStore5", "groupStore6", "groupStore7", "groupStore8", "groupStore9", "groupStore0" };
			static const char* const selectIds[10] = { "groupSelect1", "groupSelect2", "groupSelect3", "groupSelect4", "groupSelect5", "groupSelect6", "groupSelect7", "groupSelect8", "groupSelect9", "groupSelect0" };
			static const char* const storeNames[10] = { "Store control group 1", "Store control group 2", "Store control group 3", "Store control group 4", "Store control group 5", "Store control group 6", "Store control group 7", "Store control group 8", "Store control group 9", "Store control group 0" };
			static const char* const selectNames[10] = { "Select control group 1 (twice: go there)", "Select control group 2 (twice: go there)", "Select control group 3 (twice: go there)", "Select control group 4 (twice: go there)", "Select control group 5 (twice: go there)", "Select control group 6 (twice: go there)", "Select control group 7 (twice: go there)", "Select control group 8 (twice: go there)", "Select control group 9 (twice: go there)", "Select control group 0 (twice: go there)" };
			for (int store = 1; store >= 0; store--)
				for (int i = 0; i < 10; i++) {
					const SDL_Scancode top = (SDL_Scancode)(SDL_SCANCODE_1 + i); // SDL_SCANCODE_1 .. 9, then 0
					const K pad = i < 9 ? key((SDL_Scancode)(SDL_SCANCODE_KP_1 + i), store ? K::CTRL : 0) : none();
					const A a = (A)((int)(store ? A::GroupStore1 : A::GroupSelect1) + i);
					l.push_back({ a, store ? storeIds[i] : selectIds[i], store ? storeNames[i] : selectNames[i], GROUPS, P::GAME, false, false,
						{ key(top, store ? K::CTRL : 0), pad } });
				}
			const std::vector<KeyActionInfo> rest = {
				{ A::OpenMenu, "openMenu", "Game menu", GAME, P::ALWAYS, false, false, { key(SDL_SCANCODE_ESCAPE), none() } },
				{ A::OpenOptions, "openOptions", "Options", GAME, P::ALWAYS, false, false, { key(SDL_SCANCODE_F10), none() } },
				{ A::Chat, "chat", "Chat (multiplayer)", GAME, P::ALWAYS, false, false, { key(SDL_SCANCODE_TAB), key(SDL_SCANCODE_RETURN) } },
				{ A::Pause, "pause", "Pause", GAME, P::GAME, false, false, { key(SDL_SCANCODE_P), none() } },
				{ A::SpeedUp, "speedUp", "Game faster", GAME, P::GAME, false, false, { key(SDL_SCANCODE_KP_PLUS), none() } },
				{ A::SlowDown, "slowDown", "Game slower", GAME, P::GAME, false, false, { key(SDL_SCANCODE_KP_MINUS), none() } },
				{ A::Objectives, "objectives", "Mission objectives (the original's O)", GAME, P::GAME, false, false, { key(SDL_SCANCODE_O), none() } },
				{ A::SelectVillages, "selectVillages", "Select a Village (again: the next)", ORDERS, P::GAME, false, false, { key(SDL_SCANCODE_V), none() } },
				{ A::SelectIdlePeasants, "selectIdlePeasants", "Select an idle Peasant (again: the next)", ORDERS, P::GAME, false, false, { key(SDL_SCANCODE_I), none() } },
				{ A::SelectSpecialUnits, "selectSpecialUnits", "Select a priest, druid, succubus or sapper (again: the next)", ORDERS, P::GAME, false, false, { key(SDL_SCANCODE_X), none() } },
				{ A::SelectBarracks, "selectBarracks", "Select a Barracks (again: the next)", ORDERS, P::GAME, false, false, { key(SDL_SCANCODE_B), none() } },
				{ A::SelectStables, "selectStables", "Select a Stables (again: the next)", ORDERS, P::GAME, false, false, { key(SDL_SCANCODE_L), none() } },
				{ A::SelectEconomicBuildings, "selectEconomicBuildings", "Select an economic building (again: the next)", ORDERS, P::GAME, false, false, { key(SDL_SCANCODE_K), none() } },
				{ A::SelectPalace, "selectPalace", "Select the Manor, Keep, Castle or Palace (again: the next)", ORDERS, P::GAME, false, false, { key(SDL_SCANCODE_M), none() } },
				{ A::SelectBurningBuildings, "selectBurningBuildings", "Select a burning building (again: the next)", ORDERS, P::GAME, false, false, { key(SDL_SCANCODE_Z), none() } },
				{ A::SelectScouts, "selectScouts", "Select a Scout or Spy (again: the next)", ORDERS, P::GAME, false, false, { key(SDL_SCANCODE_N), none() } },
				{ A::SelectSupplyWagons, "selectSupplyWagons", "Select a Supply Wagon (again: the next)", ORDERS, P::GAME, false, false, { key(SDL_SCANCODE_C), none() } },
				{ A::SelectTradingPosts, "selectTradingPosts", "Select a Trading Post (again: the next)", ORDERS, P::GAME, false, false, { key(SDL_SCANCODE_T), none() } },
				{ A::SelectTowers, "selectTowers", "Select a tower or fort (again: the next)", ORDERS, P::GAME, false, false, { key(SDL_SCANCODE_Y), none() } },
				{ A::SelectDocks, "selectDocks", "Select a Dock (again: the next)", ORDERS, P::GAME, false, false, { key(SDL_SCANCODE_U), none() } },
				{ A::SelectPlacesOfInterest, "selectPlacesOfInterest", "Show a place of interest (again: the next)", ORDERS, P::GAME, false, false, { key(SDL_SCANCODE_J), none() } },
				{ A::Follow, "follow", "Follow (hold, right-click a unit)", ORDERS, P::GAME, true, false, { key(SDL_SCANCODE_F), none() } },
				{ A::Guard, "guard", "Guard (hold, right-click a unit or building)", ORDERS, P::GAME, true, false, { key(SDL_SCANCODE_G), none() } },
				{ A::Halt, "halt", "Halt", ORDERS, P::GAME, false, false, { key(SDL_SCANCODE_H), none() } },
				{ A::Patrol, "patrol", "Patrol (hold, right-click; with Shift: one more point)", ORDERS, P::GAME, true, false, { key(SDL_SCANCODE_R), none() } },
				{ A::LastWarning, "lastWarning", "To the last warning (again: the one before)", ORDERS, P::GAME, false, false, { key(SDL_SCANCODE_HOME), none() } },
				{ A::AgentTakeOver, "agentTakeOver", "Agent control: take the input over (again: give it back)", GAME, P::ALWAYS, false, false, { key(SDL_SCANCODE_F12, K::CTRL | K::ALT), none() } },

				{ A::HideInterface, "hideInterface", "Show or hide the interface", WINDOWS, P::ALWAYS, false, false, { key(SDL_SCANCODE_F3), none() } },
				{ A::Statistics, "statistics", "Statistics", WINDOWS, P::ALWAYS, false, false, { key(SDL_SCANCODE_F6), none() } },
				{ A::Timeline, "timeline", "Timeline", WINDOWS, P::ALWAYS, false, false, { key(SDL_SCANCODE_F7), none() } },
				{ A::FrameTimings, "frameTimings", "Frame timings", WINDOWS, P::ALWAYS, false, false, { key(SDL_SCANCODE_F12, K::CTRL), none() } },
				{ A::MapEditor, "mapEditor", "Map editor (single player)", WINDOWS, P::GAME, false, false, { key(SDL_SCANCODE_F8), none() } },

				{ A::EditorUndo, "editorUndo", "Undo", EDITOR, P::EDITOR, false, false, { key(SDL_SCANCODE_Z, K::CTRL), none() } },
				{ A::EditorRedo, "editorRedo", "Redo", EDITOR, P::EDITOR, false, false, { key(SDL_SCANCODE_Y, K::CTRL), key(SDL_SCANCODE_Z, K::CTRL | K::SHIFT) } },
				{ A::EditorGrab, "editorGrab", "Move the selected objects", EDITOR, P::EDITOR, false, false, { key(SDL_SCANCODE_G), none() } },
				{ A::EditorTurn, "editorTurn", "Turn the selected objects", EDITOR, P::EDITOR, false, false, { key(SDL_SCANCODE_R), none() } },
				{ A::EditorTurnBack, "editorTurnBack", "Turn the selected objects back", EDITOR, P::EDITOR, false, false, { key(SDL_SCANCODE_R, K::SHIFT), none() } },

				{ A::ReplayPause, "replayPause", "Pause or play", REPLAY, P::REPLAY, false, false, { key(SDL_SCANCODE_P), none() } },
				{ A::ReplayLog, "replayLog", "Show the log", REPLAY, P::REPLAY, false, false, { key(SDL_SCANCODE_L), none() } },
				{ A::ReplayFaster, "replayFaster", "Faster", REPLAY, P::REPLAY, false, false, { key(SDL_SCANCODE_KP_PLUS), none() } },
				{ A::ReplaySlower, "replaySlower", "Slower", REPLAY, P::REPLAY, false, false, { key(SDL_SCANCODE_KP_MINUS), none() } },

				{ A::ServerWindow, "serverWindow", "Server window (the whole game's state)", CHEATS, P::GAME, false, true, { key(SDL_SCANCODE_F1), none() } },
				{ A::Debugger, "debugger", "Debugger", CHEATS, P::GAME, false, true, { key(SDL_SCANCODE_F2), none() } },
				{ A::EnvironmentPanel, "environmentPanel", "Environment panel (sky, weather)", CHEATS, P::GAME, false, true, { key(SDL_SCANCODE_F9), none() } },
			};
			l.insert(l.end(), rest.begin(), rest.end());
			return l;
		}();
		return list;
	}

	// the bindings, by action
	std::array<std::array<KeyChord, KEY_SLOTS>, (size_t)KeyAction::COUNT>& bindings()
	{
		static std::array<std::array<KeyChord, KEY_SLOTS>, (size_t)KeyAction::COUNT> b = [] {
			std::array<std::array<KeyChord, KEY_SLOTS>, (size_t)KeyAction::COUNT> d{};
			for (const KeyActionInfo& info : table())
				for (int s = 0; s < KEY_SLOTS; s++)
					d[(size_t)info.action][s] = info.defaults[s];
			return d;
		}();
		return b;
	}

	std::function<bool()> g_cheatGate;
	uint64_t g_frame = 0;
	std::array<uint64_t, (size_t)KeyAction::COUNT> g_printedFrame{};

	bool isModifier(int sc)
	{
		return sc == SDL_SCANCODE_LCTRL || sc == SDL_SCANCODE_RCTRL || sc == SDL_SCANCODE_LSHIFT || sc == SDL_SCANCODE_RSHIFT
			|| sc == SDL_SCANCODE_LALT || sc == SDL_SCANCODE_RALT || sc == SDL_SCANCODE_LGUI || sc == SDL_SCANCODE_RGUI;
	}

	int currentMods()
	{
		return (g_modCtrl ? KeyChord::CTRL : 0) | (g_modShift ? KeyChord::SHIFT : 0) | (g_modAlt ? KeyChord::ALT : 0);
	}

	bool validKey(int sc) { return sc > 0 && sc < (int)std::size(g_keyPressed); }

	bool placesOverlap(KeyPlace a, KeyPlace b)
	{
		if (a == KeyPlace::ALWAYS || b == KeyPlace::ALWAYS || a == b)
			return true;
		// the map editor runs within a game; a replay is neither
		return (a == KeyPlace::GAME && b == KeyPlace::EDITOR) || (a == KeyPlace::EDITOR && b == KeyPlace::GAME);
	}

	void trace(KeyAction a)
	{
		if (!BenchmarkEnabled() || g_printedFrame[(size_t)a] == g_frame + 1)
			return;
		g_printedFrame[(size_t)a] = g_frame + 1;
		printf("KEYACTION %s\n", KeyActionOf(a).id);
		fflush(stdout);
	}
}

std::string KeyChord::text() const
{
	if (key == 0)
		return std::string();
	std::string t;
	if (!isModifier(key)) {
		if (mods & CTRL) t += "Ctrl+";
		if (mods & SHIFT) t += "Shift+";
		if (mods & ALT) t += "Alt+";
	}
	const char* name = SDL_GetScancodeName((SDL_Scancode)key);
	t += (name && *name) ? name : ("#" + std::to_string(key));
	return t;
}

KeyChord KeyChord::parse(const std::string& text)
{
	KeyChord c;
	std::string rest = text;
	for (;;) {
		auto take = [&](const char* prefix, int mod) {
			const size_t n = strlen(prefix);
			if (rest.size() > n && rest.compare(0, n, prefix) == 0) {
				c.mods |= mod;
				rest = rest.substr(n);
				return true;
			}
			return false;
		};
		if (!take("Ctrl+", CTRL) && !take("Shift+", SHIFT) && !take("Alt+", ALT))
			break;
	}
	if (rest.empty())
		return KeyChord();
	if (rest[0] == '#')
		c.key = atoi(rest.c_str() + 1);
	else
		c.key = (int)SDL_GetScancodeFromName(rest.c_str());
	if (!validKey(c.key))
		return KeyChord();
	if (isModifier(c.key))
		c.mods = 0;
	return c;
}

const std::vector<KeyActionInfo>& GetKeyActions() { return table(); }

const KeyActionInfo* FindKeyAction(const std::string& id)
{
	for (const KeyActionInfo& info : table())
		if (id == info.id)
			return &info;
	return nullptr;
}

const KeyActionInfo& KeyActionOf(KeyAction a)
{
	static const std::array<const KeyActionInfo*, (size_t)KeyAction::COUNT> byAction = [] {
		std::array<const KeyActionInfo*, (size_t)KeyAction::COUNT> m{};
		for (const KeyActionInfo& info : table())
			m[(size_t)info.action] = &info;
		return m;
	}();
	return *byAction[(size_t)a];
}

bool KeyPressedUngated(KeyAction a)
{
	const int mods = currentMods();
	for (const KeyChord& c : bindings()[(size_t)a]) {
		if (!validKey(c.key) || !g_keyPressed[c.key])
			continue;
		// a modifier key alone: whatever else is held; any other key: with exactly its modifiers
		if (isModifier(c.key) || c.mods == mods) {
			trace(a);
			return true;
		}
	}
	return false;
}

bool KeyPressed(KeyAction a)
{
	if (KeyActionOf(a).cheat && !KeyCheatsAllowed())
		return false;
	return KeyPressedUngated(a);
}

bool KeyDown(KeyAction a)
{
	if (KeyActionOf(a).cheat && !KeyCheatsAllowed())
		return false;
	const int mods = currentMods();
	for (const KeyChord& c : bindings()[(size_t)a])
		if (validKey(c.key) && g_keyDown[c.key] && (isModifier(c.key) || (c.mods & mods) == c.mods))
			return true;
	return false;
}

void SetKeyCheatGate(std::function<bool()> gate) { g_cheatGate = std::move(gate); }
bool KeyCheatsAllowed() { return g_cheatGate && g_cheatGate(); }

KeyChord GetKeyBinding(KeyAction a, int slot)
{
	return slot >= 0 && slot < KEY_SLOTS ? bindings()[(size_t)a][slot] : KeyChord();
}

void SetKeyBinding(KeyAction a, int slot, KeyChord chord)
{
	if (slot < 0 || slot >= KEY_SLOTS)
		return;
	if (isModifier(chord.key))
		chord.mods = 0;
	if (chord.key == 0)
		chord.mods = 0;
	bindings()[(size_t)a][slot] = chord;
}

void ResetKeyBinding(KeyAction a)
{
	const KeyActionInfo& info = KeyActionOf(a);
	for (int s = 0; s < KEY_SLOTS; s++)
		bindings()[(size_t)a][s] = info.defaults[s];
}

void ResetAllKeyBindings()
{
	for (const KeyActionInfo& info : table())
		ResetKeyBinding(info.action);
}

bool KeyBindingIsDefault(KeyAction a)
{
	const KeyActionInfo& info = KeyActionOf(a);
	for (int s = 0; s < KEY_SLOTS; s++)
		if (bindings()[(size_t)a][s] != info.defaults[s])
			return false;
	return true;
}

std::vector<KeyConflict> FindKeyConflicts(KeyAction a, int slot, KeyChord chord)
{
	std::vector<KeyConflict> found;
	if (chord.empty())
		return found;
	const KeyPlace place = KeyActionOf(a).place;
	for (const KeyActionInfo& info : table())
		for (int s = 0; s < KEY_SLOTS; s++) {
			if (info.action == a && s == slot)
				continue;
			if (bindings()[(size_t)info.action][s] == chord && (info.action == a || placesOverlap(place, info.place)))
				found.push_back({ info.action, s });
		}
	return found;
}

void LoadKeyBindings()
{
	ResetAllKeyBindings();
	const nlohmann::json saved = g_settings.value("keyBindings", nlohmann::json::object());
	if (!saved.is_object())
		return;
	for (const auto& [id, keys] : saved.items()) {
		const KeyActionInfo* info = FindKeyAction(id);
		if (!info || !keys.is_array())
			continue;
		for (int s = 0; s < KEY_SLOTS && s < (int)keys.size(); s++)
			if (keys[s].is_string())
				SetKeyBinding(info->action, s, KeyChord::parse(keys[s].get<std::string>()));
	}
}

void SaveKeyBindings()
{
	nlohmann::json saved = nlohmann::json::object();
	for (const KeyActionInfo& info : table()) {
		if (KeyBindingIsDefault(info.action))
			continue;
		nlohmann::json keys = nlohmann::json::array();
		for (int s = 0; s < KEY_SLOTS; s++)
			keys.push_back(GetKeyBinding(info.action, s).text());
		saved[info.id] = keys;
	}
	if (saved.empty())
		g_settings.erase("keyBindings");
	else
		g_settings["keyBindings"] = saved;
}

namespace {
	struct Capture {
		bool active = false;
		bool otherKey = false;   // a key other than a lone modifier came
		int loneModifier = 0;    // the modifier pressed first, while no other was held
		std::array<bool, 8> modDown{};
	} g_capture;
	const int modKeys[8] = { SDL_SCANCODE_LCTRL, SDL_SCANCODE_RCTRL, SDL_SCANCODE_LSHIFT, SDL_SCANCODE_RSHIFT,
		SDL_SCANCODE_LALT, SDL_SCANCODE_RALT, SDL_SCANCODE_LGUI, SDL_SCANCODE_RGUI };
}

void BeginKeyCapture()
{
	g_capture = Capture();
	g_capture.active = true;
	for (int i = 0; i < 8; i++)
		g_capture.modDown[i] = g_keyDown[modKeys[i]];
}

KeyCaptureResult PollKeyCapture(KeyChord& out)
{
	if (!g_capture.active)
		return KeyCaptureResult::CANCELLED;
	if (g_keyPressed[SDL_SCANCODE_ESCAPE]) {
		g_capture.active = false;
		g_keyPressed[SDL_SCANCODE_ESCAPE] = false;
		return KeyCaptureResult::CANCELLED;
	}
	// the modifiers held now, by the keys themselves (SDL's state is from before this frame's events)
	int mods = 0;
	for (int i = 0; i < 8; i++)
		if (g_keyDown[modKeys[i]])
			mods |= i < 2 ? KeyChord::CTRL : i < 4 ? KeyChord::SHIFT : i < 6 ? KeyChord::ALT : 0;
	for (int sc = 1; sc < (int)std::size(g_keyPressed); sc++) {
		if (!g_keyPressed[sc] || isModifier(sc) || sc == SDL_SCANCODE_ESCAPE)
			continue;
		out = KeyChord{ sc, mods };
		g_keyPressed[sc] = false; // (not also a key for the page's other controls)
		g_capture.active = false;
		return KeyCaptureResult::DONE;
	}
	for (int i = 0; i < 8; i++) {
		const bool down = g_keyDown[modKeys[i]];
		bool othersDown = false;
		for (int j = 0; j < 8; j++)
			othersDown |= j != i && g_keyDown[modKeys[j]];
		if (down && !g_capture.modDown[i])
			g_capture.loneModifier = othersDown || g_capture.loneModifier ? -1 : modKeys[i];
		if (!down && g_capture.modDown[i] && g_capture.loneModifier == modKeys[i]) {
			out = KeyChord{ modKeys[i], 0 };
			g_capture.active = false;
			return KeyCaptureResult::DONE;
		}
		g_capture.modDown[i] = down;
	}
	return KeyCaptureResult::WAITING;
}

void KeyBindingsNewFrame() { g_frame++; }
