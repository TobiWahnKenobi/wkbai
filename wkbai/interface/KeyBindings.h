// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

// wkbai's key bindings: every keyboard action of the game in one table, with an id, a name, a
// group (the Keyboard page's headings) and up to two keys each (a key with Ctrl, Shift or Alt
// held, or a modifier key alone). Code asks for an action (KeyPressed, KeyDown), never for a
// key. The bindings are the user's (wkconfig.json's "keyBindings": only the actions changed from
// their defaults), not the save game's: they belong to the hands at this keyboard, and a save
// sent to someone else, or a multiplayer save, must not remap their keys.
enum class KeyAction {
	// camera
	CameraForward, CameraBack, CameraLeft, CameraRight, CameraFast, CameraRotate, CameraToSelection,
	// selection and orders
	NextCommand, DeleteSelected, TurnBuildingLeft, TurnBuildingRight,
	// control groups: store (Ctrl+digit) and select (digit) for 1-9 and 0
	GroupStore1, GroupStore2, GroupStore3, GroupStore4, GroupStore5, GroupStore6, GroupStore7, GroupStore8, GroupStore9, GroupStore0,
	GroupSelect1, GroupSelect2, GroupSelect3, GroupSelect4, GroupSelect5, GroupSelect6, GroupSelect7, GroupSelect8, GroupSelect9, GroupSelect0,
	// the game
	OpenMenu, OpenOptions, Chat, Pause, SpeedUp, SlowDown, Objectives,
	// the original's keys (the game set's Hot Keys.cpp): selecting by kind, the nearest first
	SelectVillages, SelectIdlePeasants, SelectSpecialUnits, SelectBarracks, SelectStables, SelectEconomicBuildings,
	SelectPalace, SelectBurningBuildings, SelectScouts, SelectSupplyWagons, SelectTradingPosts, SelectTowers,
	SelectDocks, SelectPlacesOfInterest,
	// held with a right-click: Follow, Guard, Patrol (Shift: one more point); Halt at once
	Follow, Guard, Halt, Patrol,
	// Home: the last four warnings' places in turn (the minimap alerts)
	LastWarning,
	// agent control: the input to the person at the keyboard and back (window.cpp, AgentInput)
	AgentTakeOver,
	// windows and tools
	HideInterface, Statistics, Timeline, FrameTimings, MapEditor,
	// the map editor
	EditorUndo, EditorRedo, EditorGrab, EditorTurn, EditorTurnBack,
	// replays
	ReplayPause, ReplayLog, ReplayFaster, ReplaySlower,
	// cheat keys (only where cheat mode allows them: KeyCheatsAllowed)
	ServerWindow, Debugger, EnvironmentPanel,
	COUNT
};

// a key and the modifiers held with it; key 0: none (a cleared slot)
struct KeyChord {
	enum : int { CTRL = 1, SHIFT = 2, ALT = 4 };
	int key = 0;   // an SDL_Scancode
	int mods = 0;  // CTRL | SHIFT | ALT
	bool empty() const { return key == 0; }
	bool operator==(const KeyChord& o) const { return key == o.key && (key == 0 || mods == o.mods); }
	bool operator!=(const KeyChord& o) const { return !(*this == o); }
	// "Ctrl+F12", "Keypad 0", "Left Shift"; "" for none
	std::string text() const;
	static KeyChord parse(const std::string& text);
};

// where an action works: two actions sharing a key conflict when their places overlap
enum class KeyPlace { ALWAYS, GAME, EDITOR, REPLAY };

struct KeyActionInfo {
	KeyAction action;
	const char* id;     // stable name: wkconfig.json, scripts, tests
	const char* name;
	const char* group;
	KeyPlace place;
	bool held;          // a key held down (camera movement): extra modifiers don't stop it
	bool cheat;         // a cheat key
	KeyChord defaults[2];
};

constexpr int KEY_SLOTS = 2;

const std::vector<KeyActionInfo>& GetKeyActions();
const KeyActionInfo* FindKeyAction(const std::string& id);
const KeyActionInfo& KeyActionOf(KeyAction a);

// pressed this frame (with exactly those modifiers); a cheat action only when cheats are allowed
bool KeyPressed(KeyAction a);
// the same without the cheat gate (closing a cheat window always works)
bool KeyPressedUngated(KeyAction a);
// held down (with at least its modifiers)
bool KeyDown(KeyAction a);

// the cheat gate (ClientInterface::cheatKeysAllowed while a game runs); none: no cheats
void SetKeyCheatGate(std::function<bool()> gate);
bool KeyCheatsAllowed();

KeyChord GetKeyBinding(KeyAction a, int slot);
void SetKeyBinding(KeyAction a, int slot, KeyChord chord);
void ResetKeyBinding(KeyAction a);
void ResetAllKeyBindings();
bool KeyBindingIsDefault(KeyAction a);
// the other actions' slots that this chord would clash with in the action's place
struct KeyConflict { KeyAction action; int slot; };
std::vector<KeyConflict> FindKeyConflicts(KeyAction a, int slot, KeyChord chord);

// from and to g_settings["keyBindings"] (SaveKeyBindings doesn't write the file: SaveSettings does)
void LoadKeyBindings();
void SaveKeyBindings();

// The Keyboard page's key capture: begun, then polled every frame until it gives a chord (the
// first key pressed with the modifiers then held, or a modifier pressed and released alone) or
// gives up (Esc: cancelled)
void BeginKeyCapture();
enum class KeyCaptureResult { WAITING, DONE, CANCELLED };
KeyCaptureResult PollKeyCapture(KeyChord& out);

// test runs: a frame counter for the "KEYACTION <id>" lines printed in benchmark runs
void KeyBindingsNewFrame();
