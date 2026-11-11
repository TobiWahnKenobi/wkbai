// wkbai - agent control: what is on the screen, for aiming (the UI registry and the world)
// Licensed under the GNU General Public License 3

#include "AgentUi.h"
#include "AgentInput.h"
#include "window.h"
#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include <map>
#include <unordered_map>

namespace AgentUi {

namespace {
	std::vector<Element> building, shown;
	std::map<std::string, int> idCount;
	Minimap minimap, minimapBuilding;
	const World* world = nullptr;
	// ImGui: the item rectangles of this frame by id, until their label comes
	std::unordered_map<ImGuiID, std::pair<ImRect, bool>> imguiItems;
}

void AddPixels(const std::string& id, const std::string& label, int x, int y, int w, int h, bool enabled)
{
	if (!AgentInput::Enabled() || w <= 0 || h <= 0)
		return;
	const int n = ++idCount[id];
	building.push_back({ n > 1 ? id + "#" + std::to_string(n) : id, label, x, y, w, h, enabled });
}

void Add(const std::string& id, const std::string& label, float x, float y, float w, float h, bool enabled)
{
	if (!AgentInput::Enabled())
		return;
	AddPixels(id, label, (int)(x * g_uiScale), (int)(y * g_uiScale), (int)(w * g_uiScale + 0.5f), (int)(h * g_uiScale + 0.5f), enabled);
}

void EndFrame()
{
	if (ImGuiContext* ctx = ImGui::GetCurrentContext())
		ctx->TestEngineHookItems = AgentInput::Enabled(); // ImGui's items only while it is on
	imguiItems.clear();
	if (!AgentInput::Enabled()) {
		shown.clear();
		building.clear();
		idCount.clear();
		return;
	}
	shown = std::move(building);
	building.clear();
	idCount.clear();
	minimap = minimapBuilding;
	minimapBuilding = Minimap();
}

const std::vector<Element>& Elements() { return shown; }

const Element* Find(const std::string& id)
{
	for (const Element& e : shown)
		if (e.id == id)
			return &e;
	return nullptr;
}

const Element* At(int x, int y)
{
	for (auto it = shown.rbegin(); it != shown.rend(); ++it)
		if (x >= it->x && x < it->x + it->w && y >= it->y && y < it->y + it->h)
			return &*it;
	return nullptr;
}

void SetMinimap(const Minimap& m) { if (AgentInput::Enabled()) minimapBuilding = m; }
const Minimap& GetMinimap() { return minimap; }

void SetWorld(const World* w) { world = w; }
const World* GetWorld() { return world; }

}

// ImGui's test engine hooks (IMGUI_ENABLE_TEST_ENGINE, imgui/CMakeLists.txt): every item ImGui
// lays out, with its label, into the registry ("imgui:<label>"); called only while
// TestEngineHookItems is set (agent control on)
void ImGuiTestEngineHook_ItemAdd(ImGuiContext* ctx, ImGuiID id, const ImRect& bb, const ImGuiLastItemData* item_data)
{
	const bool disabled = item_data && (item_data->ItemFlags & ImGuiItemFlags_Disabled);
	AgentUi::imguiItems[id] = { bb, !disabled };
	(void)ctx;
}

void ImGuiTestEngineHook_ItemInfo(ImGuiContext* ctx, ImGuiID id, const char* label, ImGuiItemStatusFlags flags)
{
	(void)ctx; (void)flags;
	auto it = AgentUi::imguiItems.find(id);
	if (it == AgentUi::imguiItems.end() || !label || !*label)
		return;
	// the label as shown ("Start level##x" shows "Start level")
	std::string text = label;
	if (text == "Debug##Default")
		return; // ImGui's implicit window, never shown
	if (const size_t hash = text.find("##"); hash != std::string::npos)
		text.resize(hash);
	if (text.empty())
		return;
	const ImRect& r = it->second.first;
	AgentUi::Add("imgui:" + text, text, r.Min.x, r.Min.y, r.GetWidth(), r.GetHeight(), it->second.second);
}

void ImGuiTestEngineHook_Log(ImGuiContext*, const char*, ...) {}

const char* ImGuiTestEngine_FindItemDebugLabel(ImGuiContext*, ImGuiID) { return nullptr; }
