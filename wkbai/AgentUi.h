// wkbai - agent control: what is on the screen, for aiming (the UI registry and the world)
// Licensed under the GNU General Public License 3

#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

// Agent control's eyes for aiming (docs/engine/agent-control.md in the RE repo). Nothing here
// acts: it says where things are drawn, so that AgentServer can move the agent's cursor there
// and click with AgentInput, as a person would.
//
// The UI registry: every interactive element the screen draws this frame (the Lua HUD's
// buttons, the front end's buttons and tabs, the minimap, ImGui's items) puts itself in a list,
// and the list of the last whole frame is what the agent sees. Elements of the same id get "#2",
// "#3" in drawing order. While agent control is off, Add returns at once.
namespace AgentUi {

struct Element {
	std::string id;     // "cmd:Build Farm", "tab:ECONOMIC", "button:Options", "imgui:Start level", "minimap"
	std::string label;  // what the player reads on it (may be empty)
	int x, y, w, h;     // window pixels (g_mouseX/Y)
	bool enabled;
};

// in points (the UI's units: pixels / g_uiScale)
void Add(const std::string& id, const std::string& label, float x, float y, float w, float h, bool enabled);
// in window pixels
void AddPixels(const std::string& id, const std::string& label, int x, int y, int w, int h, bool enabled);
// the frame is drawn: its list becomes the one the agent sees (ImGuiImpl_Render)
void EndFrame();
const std::vector<Element>& Elements();
const Element* Find(const std::string& id);
// the element at a pixel, the topmost (last drawn) one; nullptr: none
const Element* At(int x, int y);

// The minimap's map part (points) and the playable area it shows, for "camera" moves
struct Minimap {
	bool valid = false;
	float x = 0, y = 0, w = 0, h = 0;
	float areaWidth = 0, areaDepth = 0;
};
void SetMinimap(const Minimap& m);
const Minimap& GetMinimap();

// The 3D world as the player's screen shows it, from the game interface (ClientInterface sets
// these while a game is on; empty outside one)
struct World {
	// an object's screen point (window pixels), false with the reason (unknown, off the screen,
	// behind the camera)
	std::function<bool(uint32_t id, int& x, int& y, std::string& why)> objectPoint;
	// where an object is on the map (x, z in metres)
	std::function<bool(uint32_t id, float& x, float& z)> objectPosition;
	// a point on the ground (x, z in metres) on the screen
	std::function<bool(float x, float z, int& sx, int& sy, std::string& why)> groundPoint;
	// the object under the cursor this frame (the hover pick a click uses), 0: none
	std::function<uint32_t()> hovered;
	// the player's own objects (a player knows where they are): id, type, the screen point if on it
	struct Seen { uint32_t id; std::string type; int x, y; bool onScreen; bool character; };
	std::function<std::vector<Seen>()> ownOnScreen;
	// the player this screen plays (0: none, it watches)
	std::function<uint32_t()> player;
	// Layer 3: what the player sees, as JSON (AgentServer's "observe"): no more than the screen
	// and the HUD show, the fog of war as the player has it
	// ({ "scenery": true }: trees and other objects on the screen too)
	std::function<nlohmann::json(const nlohmann::json& request)> observe;
	// Test probes (AgentServer's "probe"): what a person can't see (paths, orders, the game's hash,
	// every object regardless of fog); only where cheat keys work (single player in cheat mode) or
	// in a replay, else an error
	std::function<nlohmann::json(const nlohmann::json& request)> probe;
	// the selection's ids
	std::function<std::vector<uint32_t>()> selection;
};
void SetWorld(const World* world);
const World* GetWorld();

}
