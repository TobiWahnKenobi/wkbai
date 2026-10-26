// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#include "FrontEnd.h"
#include "MenuOptions.h"
#include "../gfx/BitmapFont.h"
#include "../gfx/bitmap.h"
#include "../gfx/renderer.h"
#include "../imgui/imgui.h"
#include "../file.h"
#include "../window.h"
#include "../Language.h"
#include "../SoundPlayer.h"
#include "../util/StriCompare.h"
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_scancode.h>
#include <SDL3/SDL_timer.h>
#include <sol/sol.hpp>
#include <cmath>
#include <cstdio>
#include <map>
#include <algorithm>
#include <memory>
#include <sstream>

namespace {
	FrontEnd* g_frontEnd = nullptr;

	struct Image {
		texture tex = nullptr;
		int width = 0, height = 0;
	};

	// An AI General of the original (a PLAYER blueprint in AI_Personalities.gsf)
	struct General {
		std::string blueprint, nameTag, portrait, background, category;
	};

	// Read from the game set file itself, as the menus have no game set loaded
	std::vector<General> LoadGenerals()
	{
		static const char* path = "Warrior Kings Game Set\\AI\\Kingmaker AI\\Personalities\\AI_Personalities.gsf";
		std::vector<General> generals;
		if (!FileExists(path))
			return generals;
		char* data = nullptr;
		int size = 0;
		LoadFile(path, &data, &size, 1);
		data[size] = 0;
		std::istringstream in(data);
		free(data);
		auto quoted = [](const std::string& line) {
			const size_t a = line.find('"'), b = line.rfind('"');
			return (a != std::string::npos && b > a) ? line.substr(a + 1, b - a - 1) : std::string();
		};
		auto word = [](std::istringstream& ls) { std::string w; ls >> w; return w; };
		std::string line;
		while (std::getline(in, line)) {
			if (!line.empty() && line.back() == '\r')
				line.pop_back();
			std::istringstream ls(line);
			const std::string key = word(ls);
			if (key == "PLAYER") {
				const std::string name = quoted(line);
				if (name.size() > 10 && name.compare(name.size() - 10, 10, " Kingmaker") == 0)
					generals.push_back({ name });
			}
			else if (generals.empty() || key.empty())
				continue;
			else if (key == "END_PLAYER" || key == "END_GAME_OBJECT")
				continue;
			else if (key == "STARTS_WITH_NAME_TAG")
				generals.back().nameTag = word(ls);
			else if (key == "GENERAL_PORTRAIT")
				generals.back().portrait = quoted(line);
			else if (key == "GENERAL_BACKGROUND")
				generals.back().background = word(ls);
			else if (key == "GENERAL_CATEGORY")
				generals.back().category = word(ls);
		}
		// only the ones the original lists (with a category)
		generals.erase(std::remove_if(generals.begin(), generals.end(), [](const General& g) { return g.category.empty(); }), generals.end());
		return generals;
	}
}

struct FrontEnd::Impl {
	IRenderer* gfx;
	sol::state lua;
	bool scriptOk = false;
	Language lang;
	std::map<std::string, Image, StrCICompareClass> images;
	std::map<std::string, std::unique_ptr<BitmapFont>, StrCICompareClass> fonts;
	std::vector<Request> requests;
	std::vector<std::string> saveGames;
	bool singlePlayer = true;
	Place place = Place::MAIN_MENU;
	ImDrawList* drawList = nullptr;

	explicit Impl(IRenderer* gfx) : gfx(gfx) {}

	const Image& image(const std::string& path)
	{
		auto [it, isNew] = images.try_emplace(path);
		if (isNew) {
			if (FileExists(path.c_str())) {
				Bitmap bmp = Bitmap::loadBitmap(path.c_str());
				it->second.width = bmp.width;
				it->second.height = bmp.height;
				it->second.tex = gfx->CreateTexture(bmp, 1);
			}
			else
				printf("WARNING: menu image \"%s\" not found\n", path.c_str());
		}
		return it->second;
	}

	// "Morpheus 20" -> Fonts\Morpheus\20\*.font; "Drop_Shadow_White" -> Fonts\Drop_Shadow_White\*.font
	const BitmapFont* font(const std::string& name)
	{
		auto [it, isNew] = fonts.try_emplace(name);
		if (isNew) {
			std::string dir = "Fonts\\";
			const size_t space = name.rfind(' ');
			if (space != std::string::npos)
				dir += name.substr(0, space) + "\\" + name.substr(space + 1);
			else
				dir += name;
			std::unique_ptr<std::vector<std::string>> files(ListFiles(dir.c_str()));
			std::string file;
			if (files)
				for (const std::string& f : *files)
					if (f.size() > 5 && StrCICompare(std::string_view(f).substr(f.size() - 5), ".font") == 0)
						file = dir + "\\" + f;
			auto font = std::make_unique<BitmapFont>();
			if (!file.empty() && font->load(gfx, file))
				it->second = std::move(font);
			else
				printf("WARNING: menu font \"%s\" not found\n", name.c_str());
		}
		return it->second.get();
	}

	static uint32_t colourArg(sol::optional<uint32_t> c) { return c ? *c : 0xFFFFFFFF; }

	void bind()
	{
		lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::string, sol::lib::table);

		// Drawing; colours are 0xAABBGGRR (ImGui)
		lua.set_function("drawImage", [this](const std::string& path, float x, float y, float w, float h, sol::optional<uint32_t> colour) {
			const Image& img = image(path);
			if (img.tex)
				drawList->AddImage((ImTextureID)img.tex, ImVec2(x, y), ImVec2(x + w, y + h), ImVec2(0, 0), ImVec2(1, 1), colourArg(colour));
		});
		lua.set_function("drawImageUV", [this](const std::string& path, float x, float y, float w, float h, float u0, float v0, float u1, float v1, sol::optional<uint32_t> colour) {
			const Image& img = image(path);
			if (img.tex)
				drawList->AddImage((ImTextureID)img.tex, ImVec2(x, y), ImVec2(x + w, y + h), ImVec2(u0, v0), ImVec2(u1, v1), colourArg(colour));
		});
		lua.set_function("imageSize", [this](const std::string& path) {
			const Image& img = image(path);
			return std::make_tuple(img.width, img.height);
		});
		lua.set_function("drawRect", [this](float x, float y, float w, float h, uint32_t colour) {
			drawList->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), colour);
		});
		lua.set_function("drawFrame", [this](float x, float y, float w, float h, uint32_t colour, sol::optional<float> thickness) {
			drawList->AddRect(ImVec2(x, y), ImVec2(x + w, y + h), colour, 0.0f, 0, thickness ? *thickness : 1.0f);
		});
		lua.set_function("pushClip", [this](float x, float y, float w, float h) { drawList->PushClipRect(ImVec2(x, y), ImVec2(x + w, y + h), true); });
		lua.set_function("popClip", [this]() { drawList->PopClipRect(); });
		lua.set_function("drawText", [this](const std::string& fontName, float x, float y, const std::string& text, sol::optional<uint32_t> colour, sol::optional<float> scale) {
			if (const BitmapFont* f = font(fontName))
				f->draw(drawList, x, y, text, colourArg(colour), scale ? *scale : 1.0f);
		});
		lua.set_function("textWidth", [this](const std::string& fontName, const std::string& text, sol::optional<float> scale) {
			const BitmapFont* f = font(fontName);
			return f ? f->measure(text) * (scale ? *scale : 1.0f) : 0.0f;
		});
		lua.set_function("fontHeight", [this](const std::string& fontName, sol::optional<float> scale) {
			const BitmapFont* f = font(fontName);
			return f ? f->height * (scale ? *scale : 1.0f) : 0.0f;
		});
		lua.set_function("getText", [this](const std::string& key) { return lang.getText(key); });

		// Window, time and input
		lua.set_function("getWindowWidth", []() { return g_windowWidth; });
		lua.set_function("getWindowHeight", []() { return g_windowHeight; });
		lua.set_function("getTime", []() { return SDL_GetTicks() / 1000.0; });
		lua.set_function("getMouseX", []() { return g_mouseX; });
		lua.set_function("getMouseY", []() { return g_mouseY; });
		lua.set_function("getMouseWheel", []() { return g_mouseWheel; });
		lua.set_function("isMouseDown", [](int b) { return g_mouseDown[b % std::size(g_mouseDown)]; });
		lua.set_function("isMousePressed", [](int b) { return g_mousePressed[b % std::size(g_mousePressed)]; });
		lua.set_function("isMouseReleased", [](int b) { return g_mouseReleased[b % std::size(g_mouseReleased)]; });
		// An ImGui window (debug tools, the remaining ImGui menus) is under the mouse
		lua.set_function("isMouseOverOtherUi", []() { return ImGui::GetIO().WantCaptureMouse; });
		lua.set_function("isKeyPressed", [](const std::string& name) {
			const SDL_Scancode sc = SDL_GetScancodeFromName(name.c_str());
			return sc != SDL_SCANCODE_UNKNOWN && sc < (int)std::size(g_keyPressed) && g_keyPressed[sc];
		});
		lua.set_function("playSound", [](const std::string& path) {
			if (FileExists(path.c_str()))
				SoundPlayer::getSoundPlayer()->playSound(path);
		});

		// The program
		lua.set_function("request", [this](const std::string& action, sol::optional<std::string> arg) {
			requests.push_back({ action, arg ? *arg : std::string() });
		});
		lua.set_function("getPlace", [this]() { return place == Place::IN_GAME ? "game" : "menu"; });
		lua.set_function("isSinglePlayer", [this]() { return singlePlayer; });
		lua.set_function("getSaveGames", [this]() { return sol::as_table(saveGames); });
		// The original's AI Generals: { blueprint, name, category ("CONSCRIPT", "VETERAN", "ELITE"), portrait, biography }
		lua.set_function("getGenerals", [this]() {
			static const std::vector<General> generals = LoadGenerals();
			sol::table list = lua.create_table();
			int i = 1;
			for (const General& g : generals) {
				std::string bio = g.background.empty() ? std::string() : lang.getText(g.background);
				// "|Name|%s\n..." in Language.txt: the name between bars and a placeholder line
				if (!bio.empty() && bio[0] == '|') {
					const size_t end = bio.find('|', 1);
					if (end != std::string::npos)
						bio = bio.substr(end + 1);
				}
				if (bio.rfind("%s", 0) == 0)
					bio = bio.substr(2);
				size_t pos;
				// Language.txt writes line breaks as a backslash and n
				while ((pos = bio.find("\\n")) != std::string::npos)
					bio.replace(pos, 2, "\n");
				list[i++] = lua.create_table_with("blueprint", g.blueprint,
					"name", g.nameTag.empty() ? g.blueprint : lang.getText(g.nameTag),
					"category", g.category, "portrait", g.portrait, "biography", bio);
			}
			return list;
		});

		// Options (MenuOptions.h)
		lua.set_function("getOptionPages", [this]() {
			sol::table pages = lua.create_table();
			int i = 1;
			for (const MenuPage& p : GetMenuPages())
				pages[i++] = lua.create_table_with("id", p.id, "title", p.title);
			return pages;
		});
		lua.set_function("getOptions", [this](const std::string& page) {
			sol::table list = lua.create_table();
			int i = 1;
			for (const MenuOption& o : GetMenuOptions()) {
				if (o.page != page)
					continue;
				sol::table t = lua.create_table();
				t["id"] = o.id;
				t["label"] = o.label;
				t["type"] = o.type == MenuOption::TOGGLE ? "toggle" : o.type == MenuOption::SLIDER ? "slider" : "choice";
				t["min"] = o.min;
				t["max"] = o.max;
				t["step"] = o.step;
				t["logarithmic"] = o.logarithmic;
				t["format"] = o.format;
				t["needsRestart"] = o.needsRestart;
				t["choices"] = sol::as_table(o.choices);
				list[i++] = t;
			}
			return list;
		});
		lua.set_function("getOption", [](const std::string& id) {
			const MenuOption* o = FindMenuOption(id);
			return o ? o->get() : 0.0f;
		});
		lua.set_function("setOption", [](const std::string& id, float v) {
			if (const MenuOption* o = FindMenuOption(id))
				o->set(v);
		});
		lua.set_function("isOptionEnabled", [](const std::string& id) {
			const MenuOption* o = FindMenuOption(id);
			return o && (!o->enabled || o->enabled());
		});
		lua.set_function("beginEditOptions", []() { BeginEditMenuOptions(); });
		lua.set_function("saveOptions", []() { SaveMenuOptions(); });
		lua.set_function("revertOptions", []() { RevertMenuOptions(); });
	}

	template <typename... Args> void call(const char* function, Args&&... args)
	{
		if (!scriptOk)
			return;
		try {
			sol::protected_function f = lua[function];
			if (!f.valid())
				return;
			sol::protected_function_result r = f(std::forward<Args>(args)...);
			if (!r.valid()) {
				sol::error err = r;
				printf("Menu script error in %s:\n%s\n", function, err.what());
				scriptOk = false;
			}
		}
		catch (const sol::error& err) {
			printf("Menu script error in %s:\n%s\n", function, err.what());
			scriptOk = false;
		}
	}
};

FrontEnd::FrontEnd(IRenderer* gfx) : impl(std::make_unique<Impl>(gfx))
{
	g_frontEnd = this;
	impl->lang.load("Languages\\Language.txt");
	impl->bind();
	try {
		sol::protected_function_result r = impl->lua.safe_script_file("redata/FrontEndUI.lua", sol::script_pass_on_error);
		impl->scriptOk = r.valid();
		if (!r.valid()) {
			sol::error err = r;
			printf("Menu script error:\n%s\n", err.what());
		}
	}
	catch (const sol::error& err) {
		printf("Menu script error:\n%s\n", err.what());
	}
}

FrontEnd::~FrontEnd()
{
	if (g_frontEnd == this)
		g_frontEnd = nullptr;
}

FrontEnd* FrontEnd::get() { return g_frontEnd; }

void FrontEnd::frame(Place place)
{
	impl->place = place;
	// The main menu is the background; the in-game menu covers the game and its HUD
	impl->drawList = place == Place::MAIN_MENU ? ImGui::GetBackgroundDrawList() : ImGui::GetForegroundDrawList();
	impl->call("FE_Frame", place == Place::IN_GAME ? "game" : "menu");
}

bool FrontEnd::isOpen() const
{
	if (!impl->scriptOk)
		return false;
	try {
		sol::protected_function f = impl->lua["FE_IsOpen"];
		if (!f.valid())
			return false;
		sol::protected_function_result r = f();
		return r.valid() && r.get<bool>();
	}
	catch (const sol::error&) {
		return false;
	}
}

void FrontEnd::open(const std::string& screen) { impl->call("FE_Open", screen); }
void FrontEnd::close() { impl->call("FE_Close"); }

std::vector<FrontEnd::Request> FrontEnd::takeRequests()
{
	return std::exchange(impl->requests, {});
}

void FrontEnd::setSaveGames(std::vector<std::string> saves) { impl->saveGames = std::move(saves); }
void FrontEnd::setSinglePlayer(bool singlePlayer) { impl->singlePlayer = singlePlayer; }
