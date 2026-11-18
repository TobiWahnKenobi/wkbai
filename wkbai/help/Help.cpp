// wkbai - the in-game help: the window, and opening it for whatever is on the screen
// Licensed under the GNU General Public License 3

#include "Help.h"
#include "HelpInternal.h"
#include "../AgentUi.h"
#include "../window.h"
#include "../settings.h"
#include <nlohmann/json.hpp>
#include "../Language.h"
#include "../interface/KeyBindings.h"
#include "../imgui/imgui.h"
#include "../imgui/imgui_internal.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <fstream>
#include <list>
#include <set>

using namespace Help::detail;

namespace {

bool s_open = false, s_wantFocus = false, s_scrollTop = false;
ViewState s_view;
std::vector<std::pair<std::string, std::string>> s_back, s_forward; // path, anchor
std::string s_screen, s_lastScreen, s_hovered;
const GameSet* s_gameSet = nullptr;
const Language* s_game = nullptr;
bool s_namesDirty = false;
std::string s_loadedLanguage = "?";

char s_search[128] = {};
std::vector<Hit> s_hits;
std::string s_hitsFor;

// the right-click menu
bool s_menuPending = false;
std::string s_menuIds;
// "What's this?"
bool s_whatsThis = false;
// clicks the help took: their release is taken too
bool s_swallow[16] = {};
std::string s_pendingOpen;
bool s_helpFocused = false, s_helpHovered = false;

// "Show me": outlines on the screen for a while
struct Flash { float x, y, w, h; double until; };
std::vector<Flash> s_flashes;
std::string s_note;
double s_noteUntil = 0;
std::string s_askWeb;

// the contents tree
struct Node {
	std::string path, title, folder;
	double order = 1000;
	std::list<Node> kids; // a list: the folders' pointers stay valid as it grows
};
Node s_tree;
std::string s_treeLanguage;
std::string s_expandFor;

std::string lowerAscii(std::string s)
{
	for (char& c : s)
		if (c >= 'A' && c <= 'Z')
			c = (char)(c - 'A' + 'a');
	return s;
}

bool enabled() { return g_settings.value("help", true); }

std::string language() { return g_settings.value("helpLanguage", std::string("auto")); }

void ensureLoaded()
{
	if (s_loadedLanguage != language() || !Lib().loaded()) {
		s_loadedLanguage = language();
		Lib().load(s_loadedLanguage, s_game);
		ClearViewCache();
		s_namesDirty = true;
		s_hitsFor.clear();
	}
	if (s_namesDirty) {
		Lib().setGameNames(s_gameSet, s_game);
		s_namesDirty = false;
	}
}

// the title shown for a page: the game's own name of the thing when a game set is loaded
std::string titleOf(const Page& p) { return p.gameName.empty() ? p.title : p.gameName; }

// The heading a label names on the screen's page or the pages under it (its tabs), as
// "page:<path>#<anchor>"; "" none. A heading named as the label wins, then the longest heading
// the label starts with ("Lua AI: none (...)": "Lua AI"), then one that starts with the label.
std::string labelHeading(const std::string& label)
{
	const std::string slug = Slug(label);
	if (slug.empty() || s_lastScreen.empty())
		return {};
	std::string screenPath, a;
	bool exact = false;
	if (!Lib().resolve(s_lastScreen, screenPath, a, &exact) || !exact)
		return {};
	std::string dir = screenPath;
	if (dir.size() > 6 && dir.compare(dir.size() - 6, 6, "/index") == 0)
		dir.resize(dir.size() - 6);
	int bestScore = 0;
	size_t bestLen = 0;
	std::string best;
	for (const auto& [path, page] : Lib().all()) {
		if (path != screenPath && path.compare(0, dir.size() + 1, dir + "/") != 0)
			continue;
		for (const std::string& anchor : page.anchors) {
			int score = 0;
			if (anchor == slug) score = 3;
			else if (slug.compare(0, anchor.size() + 1, anchor + "-") == 0) score = 2;
			else if (anchor.compare(0, slug.size() + 1, slug + "-") == 0) score = 1;
			if (!score) continue;
			// the better match; the longer heading; the screen's own page
			if (score > bestScore || (score == bestScore && (anchor.size() > bestLen || (anchor.size() == bestLen && path == screenPath)))) {
				bestScore = score;
				bestLen = anchor.size();
				best = "page:" + path + "#" + anchor;
			}
		}
	}
	return best;
}

// The help ids of an element of the UI registry, the screen's after them (withScreen)
std::string idsOf(const AgentUi::Element& e, bool withScreen = true)
{
	std::string id = e.id;
	if (const size_t hash = id.rfind('#'); hash != std::string::npos && id.find_first_not_of("0123456789", hash + 1) == std::string::npos)
		id.resize(hash);
	std::string ids = e.help;
	auto after = [&](const char* prefix) -> std::string {
		const size_t n = strlen(prefix);
		return id.compare(0, n, prefix) == 0 ? id.substr(n) : std::string();
	};
	if (ids.empty()) {
		const bool inGame = s_lastScreen == "ui/hud";
		if (std::string x = after("cmd:"); !x.empty()) ids = "command/" + x;
		else if (std::string x = after("top:"); !x.empty()) ids = "ui/hud/top-bar/" + Slug(x);
		else if (std::string x = after("tab:"); !x.empty() && inGame) ids = "ui/hud/build-menu/" + Slug(x);
		else if (after("activity:").size()) ids = "ui/hud/peasant-activity";
		else if (after("formation:").size()) ids = "ui/hud/formation-buttons";
		else if (std::string x = after("minimapButton:"); !x.empty()) ids = "ui/hud/minimap/" + Slug(x);
		else if (id == "minimap") ids = "ui/hud/minimap";
		else if (std::string x = after("key:"); !x.empty()) ids = "key/" + x.substr(0, x.find(':'));
	}
	// no page or heading of its own: the heading of its screen's pages that its label names
	// ("Ministry AI" on the lobby: the Settings tab's "Ministry AI"), else the screen
	if (!e.label.empty() && Lib().loaded()) {
		std::string p, a;
		bool exact = false;
		if (ids.empty() || (Lib().resolve(ids, p, a, &exact), !exact))
			if (std::string h = labelHeading(e.label); !h.empty())
				ids += (ids.empty() ? "" : "|") + h;
	}
	if (withScreen && !s_lastScreen.empty())
		ids += (ids.empty() ? "" : "|") + s_lastScreen;
	return ids;
}

// The coverage report ("helpTest": { "coverage": "<file>" }): every element drawn that has no
// page of its own (it falls back to its screen's), once each, as screen, id, label, ids
void reportCoverage(const std::string& file)
{
	static std::set<std::string> seen;
	static std::ofstream out;
	if (!out.is_open()) out.open(file, std::ios::app);
	for (const AgentUi::Element& e : AgentUi::Elements()) {
		std::string id = e.id;
		if (const size_t hash = id.rfind('#'); hash != std::string::npos && id.find_first_not_of("0123456789", hash + 1) == std::string::npos)
			id.resize(hash);
		const std::string own = idsOf(e, false);
		std::string p, a;
		bool exact = false;
		if (!own.empty()) Lib().resolve(own, p, a, &exact);
		const std::string key = s_lastScreen + '\t' + id;
		if (exact || !seen.insert(key).second) continue;
		out << s_lastScreen << '\t' << id << '\t' << e.label << '\t' << own << '\n';
	}
	out.flush();
}

// what is under the cursor, as help ids: an element of the interface, else the 3D world's object,
// else the screen
std::string idsAtMouse(bool* found = nullptr)
{
	if (const AgentUi::Element* e = AgentUi::At(g_mouseX, g_mouseY)) {
		if (found) *found = true;
		return idsOf(*e);
	}
	if (found) *found = !s_hovered.empty();
	if (!s_hovered.empty())
		return s_hovered + (s_lastScreen.empty() ? "" : "|" + s_lastScreen);
	return s_lastScreen.empty() ? "index" : s_lastScreen;
}

// a right-click that does nothing else on these (Alt+right-click works everywhere)
bool plainRightClick(const AgentUi::Element& e)
{
	// the game's panels: a right-click there may give an order (the gaps between their parts are
	// the battlefield), so only Alt+right-click
	if (e.id.compare(0, 5, "help:") == 0 && s_lastScreen == "ui/hud")
		return false;
	for (const char* p : { "button:", "tab:", "item:", "switch:", "stepper:", "slider:", "key:", "help:", "top:", "imgui:", "minimapButton:" })
		if (e.id.compare(0, strlen(p), p) == 0)
			return true;
	return false;
}

// a click taken by the help: gone for the game and for ImGui (also its release)
void takeClick(int sdlButton)
{
	g_mousePressed[sdlButton] = false;
	g_mouseDown[sdlButton] = false;
	s_swallow[sdlButton] = true;
	if (ImGuiContext* ctx = ImGui::GetCurrentContext()) {
		const int ib = sdlButton == SDL_BUTTON_LEFT ? 0 : sdlButton == SDL_BUTTON_RIGHT ? 1 : sdlButton == SDL_BUTTON_MIDDLE ? 2 : -1;
		auto& q = ctx->InputEventsQueue;
		for (int i = q.Size - 1; i >= 0; i--)
			if (q[i].Type == ImGuiInputEventType_MouseButton && q[i].MouseButton.Button == ib)
				q.erase(q.Data + i);
	}
}

void navigate(const std::string& path, const std::string& anchor, bool history = true)
{
	if (history && !s_view.path.empty() && (s_view.path != path || !anchor.empty())) {
		s_back.push_back({ s_view.path, "" });
		s_forward.clear();
	}
	s_view.path = path;
	s_view.anchor = anchor;
	s_scrollTop = anchor.empty();
	s_open = true;
	s_wantFocus = true;
	s_expandFor = path;
}

void openIds(const std::string& ids)
{
	ensureLoaded();
	std::string path, anchor;
	Lib().resolve(ids, path, anchor);
	navigate(path, anchor);
}

// "Show me": outline every element whose help is that page
void showOnScreen(const std::string& id)
{
	std::string want, wantAnchor;
	Lib().resolve(id, want, wantAnchor);
	int n = 0;
	for (const AgentUi::Element& e : AgentUi::Elements()) {
		std::string p, a;
		bool exact = false;
		Lib().resolve(idsOf(e), p, a, &exact);
		if (exact && p == want && (wantAnchor.empty() || a == wantAnchor)) {
			s_flashes.push_back({ e.x / g_uiScale, e.y / g_uiScale, e.w / g_uiScale, e.h / g_uiScale, ImGui::GetTime() + 2.5 });
			n++;
		}
	}
	if (!n) {
		const Page* p = Lib().page(want);
		s_note = Lib().ui("showMeNotOnScreen", p ? titleOf(*p) : id);
		s_noteUntil = ImGui::GetTime() + 4;
	}
	else
		s_helpFocused = false;
}

// ---- the contents tree ----

void buildTree()
{
	s_tree = Node();
	s_treeLanguage = Lib().language();
	std::map<std::string, Node*> folders;
	std::function<Node*(const std::string&)> folder = [&](const std::string& f) -> Node* {
		if (f.empty())
			return &s_tree;
		auto it = folders.find(f);
		if (it != folders.end())
			return it->second;
		const size_t slash = f.rfind('/');
		Node* parent = folder(slash == std::string::npos ? "" : f.substr(0, slash));
		Node n;
		n.folder = f;
		const std::string name = f.substr(slash == std::string::npos ? 0 : slash + 1);
		const std::string key = "folder_" + name;
		n.title = Lib().ui(key);
		if (n.title == key) {
			n.title = name;
			if (!n.title.empty()) n.title[0] = (char)toupper((unsigned char)n.title[0]);
		}
		parent->kids.push_back(std::move(n));
		return folders[f] = &parent->kids.back();
	};
	for (const auto& [path, p] : Lib().all()) {
		if (path == "index")
			continue;
		const size_t slash = path.rfind('/');
		const std::string dir = slash == std::string::npos ? "" : path.substr(0, slash);
		const std::string name = path.substr(slash == std::string::npos ? 0 : slash + 1);
		if (name == "index") {
			Node* f = folder(dir);
			f->path = path;
			f->title = titleOf(p);
			f->order = p.order;
			continue;
		}
		Node* f = folder(p.parent.empty() ? dir : p.parent);
		Node n;
		n.path = path;
		n.title = titleOf(p);
		n.order = p.order;
		f->kids.push_back(std::move(n));
	}
	std::function<void(Node&)> sortNode = [&](Node& n) {
		n.kids.sort([](const Node& a, const Node& b) {
			return a.order != b.order ? a.order < b.order : lowerAscii(a.title) < lowerAscii(b.title);
		});
		for (Node& k : n.kids) sortNode(k);
	};
	sortNode(s_tree);
}

bool contains(const Node& n, const std::string& path)
{
	if (n.path == path) return true;
	for (const Node& k : n.kids) if (contains(k, path)) return true;
	return false;
}

void drawTree(const Node& n)
{
	for (const Node& k : n.kids) {
		const bool here = k.path == s_view.path;
		if (k.kids.empty()) {
			if (ImGui::Selectable((k.title + "##" + k.path).c_str(), here))
				navigate(k.path, "");
			if (here && !s_expandFor.empty())
				ImGui::SetScrollHereY(0.3f);
			continue;
		}
		if (!s_expandFor.empty() && contains(k, s_expandFor))
			ImGui::SetNextItemOpen(true);
		ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
		if (here) flags |= ImGuiTreeNodeFlags_Selected;
		const bool open = ImGui::TreeNodeEx((k.title + "##" + k.folder).c_str(), flags);
		if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen() && !k.path.empty())
			navigate(k.path, "");
		if (open) {
			drawTree(k);
			ImGui::TreePop();
		}
	}
}

void openWeb(const std::string& url) { SDL_OpenURL(url.c_str()); }

void drawWindow(IRenderer* gfx)
{
	ImGuiIO& io = ImGui::GetIO();
	const ImVec2 display = io.DisplaySize;
	ImGui::SetNextWindowSize(ImVec2(display.x * 0.62f, display.y * 0.76f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowPos(ImVec2(display.x * 0.5f, display.y * 0.5f), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
	if (s_wantFocus) {
		ImGui::SetNextWindowFocus();
		ImGui::SetNextWindowCollapsed(false);
		s_wantFocus = false;
	}
	ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(30, 25, 20, 248));
	ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));
	ImGui::PushStyleColor(ImGuiCol_TitleBg, IM_COL32(62, 44, 28, 255));
	ImGui::PushStyleColor(ImGuiCol_TitleBgActive, IM_COL32(96, 64, 34, 255));
	ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(236, 226, 208, 255));
	ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32(110, 74, 38, 200));
	ImGui::PushStyleColor(ImGuiCol_HeaderHovered, IM_COL32(130, 90, 48, 200));
	ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(80, 58, 36, 255));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(110, 78, 44, 255));
	ImGui::PushFont(Font(F_BODY), Font(F_BODY) ? Font(F_BODY)->LegacySize : 0.0f);
	const std::string title = Lib().ui("windowTitle") + "###wkbaiHelp";
	if (ImGui::Begin(title.c_str(), &s_open, ImGuiWindowFlags_NoCollapse)) {
		s_helpHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);
		s_helpFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
		// the bar: back, forward, start, search, language
		auto goBack = [&]() {
			if (s_back.empty()) return;
			s_forward.push_back({ s_view.path, "" });
			s_view.path = s_back.back().first;
			s_view.anchor.clear();
			s_scrollTop = true;
			s_back.pop_back();
			s_expandFor = s_view.path;
		};
		auto goForward = [&]() {
			if (s_forward.empty()) return;
			s_back.push_back({ s_view.path, "" });
			s_view.path = s_forward.back().first;
			s_view.anchor.clear();
			s_scrollTop = true;
			s_forward.pop_back();
			s_expandFor = s_view.path;
		};
		ImGui::BeginDisabled(s_back.empty());
		if (ImGui::Button(("<  " + Lib().ui("back")).c_str())) goBack();
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::BeginDisabled(s_forward.empty());
		if (ImGui::Button((Lib().ui("forward") + "  >").c_str())) goForward();
		ImGui::EndDisabled();
		ImGui::SameLine();
		if (ImGui::Button(Lib().ui("home").c_str())) navigate("index", "");
		if (s_helpHovered && ImGui::IsMouseClicked(3)) goBack();
		if (s_helpHovered && ImGui::IsMouseClicked(4)) goForward();
		ImGui::SameLine();
		ImGui::SetNextItemWidth(std::max(160.0f, ImGui::GetContentRegionAvail().x - 210));
		ImGui::InputTextWithHint("##search", Lib().ui("search").c_str(), s_search, sizeof(s_search));
		ImGui::SameLine();
		ImGui::SetNextItemWidth(200);
		std::string current = language() == "auto" ? Lib().ui("languageAuto") : language();
		for (const auto& [code, name] : Lib().languages())
			if (code == language()) current = name;
		if (ImGui::BeginCombo("##language", current.c_str())) {
			if (ImGui::Selectable(Lib().ui("languageAuto").c_str(), language() == "auto")) {
				g_settings["helpLanguage"] = "auto";
				SaveSettings();
			}
			for (const auto& [code, name] : Lib().languages())
				if (ImGui::Selectable((name + "##" + code).c_str(), language() == code)) {
					g_settings["helpLanguage"] = code;
					SaveSettings();
				}
			ImGui::EndCombo();
		}
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("%s", Lib().ui("language").c_str());
		ImGui::Separator();

		// the contents or the search results on the left, the page on the right
		ImGui::BeginChild("##contents", ImVec2(std::min(300.0f, ImGui::GetContentRegionAvail().x * 0.3f), 0), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);
		const std::string query = s_search;
		if (!query.empty()) {
			if (query != s_hitsFor) {
				s_hits = Lib().search(query);
				s_hitsFor = query;
			}
			if (s_hits.empty())
				ImGui::TextWrapped("%s", Lib().ui("searchNoResults", query).c_str());
			for (const Hit& h : s_hits) {
				const Page* p = Lib().page(h.path);
				if (!p) continue;
				std::string label = titleOf(*p);
				if (p->state == Page::ENGLISH) label += "  (English)";
				if (ImGui::Selectable((label + "##hit" + h.path).c_str(), h.path == s_view.path))
					navigate(h.path, "");
				if (!h.snippet.empty()) {
					ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(170, 160, 145, 255));
					ImGui::PushTextWrapPos();
					ImGui::TextUnformatted(h.snippet.c_str());
					ImGui::PopTextWrapPos();
					ImGui::PopStyleColor();
				}
				ImGui::Spacing();
			}
		}
		else {
			if (s_treeLanguage != Lib().language() || s_tree.kids.empty())
				buildTree();
			if (const Page* start = Lib().page("index"))
				if (ImGui::Selectable((titleOf(*start) + "##start").c_str(), s_view.path == "index"))
					navigate("index", "");
			drawTree(s_tree);
			s_expandFor.clear();
		}
		ImGui::EndChild();
		ImGui::SameLine();
		ImGui::BeginChild("##page", ImVec2(0, 0), ImGuiChildFlags_None);
		if (s_scrollTop) {
			ImGui::SetScrollY(0);
			s_scrollTop = false;
		}
		LinkAction act;
		if (const Page* page = Lib().page(s_view.path))
			act = DrawPage(gfx, *page, s_view, s_gameSet, s_game);
		ImGui::EndChild();
		switch (act.kind) {
		case LinkAction::PAGE: if (Lib().page(act.a)) navigate(act.a, act.b); break;
		case LinkAction::ID: openIds(act.a); break;
		case LinkAction::SHOW: showOnScreen(act.a); break;
		case LinkAction::WEB: s_askWeb = act.a; ImGui::OpenPopup("##web"); break;
		default: break;
		}
		if (ImGui::BeginPopupModal("##web", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
			ImGui::TextUnformatted(Lib().ui("openLinkQuestion", s_askWeb).c_str());
			if (ImGui::Button("OK")) { openWeb(s_askWeb); ImGui::CloseCurrentPopup(); }
			ImGui::SameLine();
			if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}
	}
	else {
		s_helpHovered = s_helpFocused = false;
	}
	ImGui::End();
	ImGui::PopFont();
	ImGui::PopStyleColor(9);
}

} // namespace

// ---------------------------------------------------------------------------------------------

void Help::SetScreen(const std::string& id) { s_screen = id; }
void Help::SetHovered(const std::string& ids) { s_hovered = ids; }
bool Help::IsOpen() { return s_open; }
void Help::Open(const std::string& ids) { openIds(ids); }

void Help::SetGame(const GameSet* gameSet, const Language* language)
{
	if (gameSet != s_gameSet || language != s_game) {
		s_gameSet = gameSet;
		s_game = language;
		s_namesDirty = true;
		s_tree.kids.clear();
	}
	if (!gameSet)
		s_hovered.clear();
}

void Help::AfterInput()
{
	const bool on = enabled();
	AgentUi::SetHelpRecording(on);
	for (int b = 0; b < 16; b++)
		if (s_swallow[b]) {
			if (g_mouseReleased[b]) { g_mouseReleased[b] = false; s_swallow[b] = false; }
			g_mouseDown[b] = false;
		}
	if (!on || !ImGui::GetCurrentContext())
		return;
	ImGuiIO& io = ImGui::GetIO();
	// typing in the help's search: no game keys
	if (s_helpFocused && io.WantTextInput)
		for (int i = 0; i < 512; i++)
			g_keyPressed[i] = false;
	if (g_keyPressed[SDL_SCANCODE_ESCAPE]) {
		if (s_whatsThis) { s_whatsThis = false; g_keyPressed[SDL_SCANCODE_ESCAPE] = false; }
		else if (s_open && s_helpFocused) { s_open = false; g_keyPressed[SDL_SCANCODE_ESCAPE] = false; }
	}
	if (KeyPressed(KeyAction::WhatsThis))
		s_whatsThis = !s_whatsThis;
	else if (KeyPressed(KeyAction::Help)) {
		bool found = false;
		const std::string ids = idsAtMouse(&found);
		// F1 over the help itself: its own page
		s_pendingOpen = s_helpHovered ? "help/viewer" : ids;
	}
	const bool overHelp = s_helpHovered;
	if (s_whatsThis && g_mousePressed[SDL_BUTTON_LEFT] && !overHelp) {
		s_pendingOpen = idsAtMouse();
		s_whatsThis = false;
		takeClick(SDL_BUTTON_LEFT);
	}
	if (g_mousePressed[SDL_BUTTON_RIGHT] && !overHelp) {
		const AgentUi::Element* e = AgentUi::At(g_mouseX, g_mouseY);
		if (g_modAlt || (e && plainRightClick(*e))) {
			s_menuIds = idsAtMouse();
			s_menuPending = true;
			takeClick(SDL_BUTTON_RIGHT);
		}
	}
}

void Help::Frame(IRenderer* gfx)
{
	s_lastScreen = s_screen.empty() ? s_lastScreen : s_screen;
	s_screen.clear();
	if (!enabled() || !ImGui::GetCurrentContext())
		return;
	ImGuiIO& io = ImGui::GetIO();
	// a right-click over an ImGui window (the game never sees those clicks)
	if (io.WantCaptureMouse && !s_helpHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && !s_menuPending) {
		const int mx = (int)(io.MousePos.x * g_uiScale), my = (int)(io.MousePos.y * g_uiScale);
		if (const AgentUi::Element* e = AgentUi::At(mx, my); e && e->id.compare(0, 6, "imgui:") == 0) {
			s_menuIds = idsOf(*e);
			s_menuPending = true;
		}
	}
	// test runs: "helpTest": { "open": "<help ids>", "menu": "<help ids>", "search": "<words>",
	// "frame": n } opens the help (or its right-click menu at the cursor) that many frames in
	if (g_settings.contains("helpTest") && g_settings["helpTest"].is_object()) {
		static int frame = 0;
		const nlohmann::json& t = g_settings["helpTest"];
		if (++frame == t.value("frame", 30)) {
			if (t.contains("open")) s_pendingOpen = t.value("open", std::string());
			if (t.contains("menu")) { s_menuIds = t.value("menu", std::string()); s_menuPending = true; }
			if (t.contains("search")) snprintf(s_search, sizeof(s_search), "%s", t.value("search", std::string()).c_str());
			// "f1": true: as F1 there, the help for what is under the cursor
			if (t.value("f1", false)) {
				ensureLoaded();
				s_pendingOpen = idsAtMouse();
				printf("HELP F1 %s\n", s_pendingOpen.c_str());
			}
		}
		if (t.contains("coverage") && frame > 2) {
			ensureLoaded();
			reportCoverage(t.value("coverage", std::string()));
		}
	}
	if (!s_pendingOpen.empty() || s_menuPending || s_open)
		ensureLoaded();
	if (!s_pendingOpen.empty()) {
		openIds(s_pendingOpen);
		s_pendingOpen.clear();
	}
	if (s_menuPending) {
		s_menuPending = false;
		ImGui::OpenPopup("##helpmenu");
	}
	ImGui::PushFont(Font(F_BODY), Font(F_BODY) ? Font(F_BODY)->LegacySize : 0.0f);
	if (ImGui::BeginPopup("##helpmenu")) {
		std::string path, anchor;
		Lib().resolve(s_menuIds, path, anchor);
		const Page* p = Lib().page(path);
		std::string what = p ? titleOf(*p) : path;
		if (p && !anchor.empty()) {
			// the heading the id points at, when there is one: "Minimap: Zoom buttons"
			for (size_t i = 0; i < p->anchors.size(); i++)
				if (p->anchors[i] == anchor) {
					std::string h = anchor;
					std::replace(h.begin(), h.end(), '-', ' ');
					if (!h.empty()) h[0] = (char)toupper((unsigned char)h[0]);
					what += ": " + h;
				}
		}
		if (ImGui::MenuItem(Lib().ui("contextMenuHelp", what).c_str(), "F1"))
			openIds(s_menuIds);
		ImGui::EndPopup();
	}
	ImGui::PopFont();

	ImDrawList* fg = ImGui::GetForegroundDrawList();
	if (s_whatsThis) {
		ImFont* f = Font(F_H2);
		fg->AddText(f, f->LegacySize, ImVec2(io.MousePos.x + 14, io.MousePos.y + 6), IM_COL32(255, 210, 120, 255), "?");
		ImGui::SetTooltip("%s", Lib().ui("whatsThis").c_str());
	}
	const double now = ImGui::GetTime();
	s_flashes.erase(std::remove_if(s_flashes.begin(), s_flashes.end(), [&](const Flash& f) { return f.until < now; }), s_flashes.end());
	for (const Flash& f : s_flashes) {
		const float pulse = 0.5f + 0.5f * (float)std::sin(now * 8.0);
		fg->AddRect(ImVec2(f.x - 3, f.y - 3), ImVec2(f.x + f.w + 3, f.y + f.h + 3), IM_COL32(255, 200, 90, (int)(140 + 115 * pulse)), 4, 0, 3);
	}
	if (now < s_noteUntil) {
		ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.12f), ImGuiCond_Always, ImVec2(0.5f, 0));
		ImGui::Begin("##helpnote", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoFocusOnAppearing);
		ImGui::TextUnformatted(s_note.c_str());
		ImGui::End();
	}
	if (s_open)
		drawWindow(gfx);
	else
		s_helpHovered = s_helpFocused = false;
}

std::string Help::Check(const std::string& lang)
{
	Lib().load(lang, nullptr);
	ClearViewCache();
	s_loadedLanguage = "?";
	return Lib().check();
}
