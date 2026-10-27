// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#include "FrontEnd.h"
#include "MenuOptions.h"
#include "CampaignProfile.h"
#include "../gfx/BitmapFont.h"
#include "../gfx/bitmap.h"
#include "../gfx/renderer.h"
#include "../imgui/imgui.h"
#include "../file.h"
#include "../window.h"
#include "../Language.h"
#include "../SoundPlayer.h"
#include "../util/StriCompare.h"
#include "../settings.h"
#include <nlohmann/json.hpp>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_scancode.h>
#include <SDL3/SDL_timer.h>
#include <sol/sol.hpp>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <algorithm>
#include <memory>
#include <sstream>
#include <filesystem>

namespace {
	FrontEnd* g_frontEnd = nullptr;

	struct Image {
		texture tex = nullptr;
		int width = 0, height = 0;
		std::vector<uint8_t> alpha; // kept for imageAlpha (hit tests on shaped images)
	};

	// An AI General: a PLAYER blueprint "... Kingmaker" of the original's AI_Personalities.gsf, or
	// one the player made with the AI General Editor (Warrior Kings Game Set\AI Generals\*.gsf)
	struct General {
		std::string blueprint, nameTag, name, portrait, background, category, alignment;
		bool custom = false;
		std::map<std::string, float> stats; // STARTS_WITH_ITEM of a custom one
	};

	const char* const AI_GENERALS_DIR = "Warrior Kings Game Set\\AI Generals";

	// Read from the game set files themselves, as the menus have no game set loaded
	void ReadGenerals(const std::string& path, bool custom, std::vector<General>& generals)
	{
		if (!FileExists(path.c_str()))
			return;
		char* data = nullptr;
		int size = 0;
		LoadFile(path.c_str(), &data, &size, 1);
		std::istringstream in(std::string(data, size));
		free(data);
		auto quoted = [](const std::string& line) {
			const size_t a = line.find('"'), b = line.rfind('"');
			return (a != std::string::npos && b > a) ? line.substr(a + 1, b - a - 1) : std::string();
		};
		auto word = [](std::istringstream& ls) { std::string w; ls >> w; return w; };
		const size_t first = generals.size();
		std::string line;
		while (std::getline(in, line)) {
			if (!line.empty() && line.back() == '\r')
				line.pop_back();
			std::istringstream ls(line);
			const std::string key = word(ls);
			if (key == "PLAYER") {
				const std::string name = quoted(line);
				if (name.size() > 10 && name.compare(name.size() - 10, 10, " Kingmaker") == 0) {
					generals.push_back({ name });
					generals.back().custom = custom;
				}
			}
			else if (generals.size() == first || key.empty())
				continue;
			else if (key == "END_PLAYER" || key == "END_GAME_OBJECT")
				continue;
			else if (key == "STARTS_WITH_NAME_TAG")
				generals.back().nameTag = word(ls);
			else if (key == "STARTS_WITH_NAME")
				generals.back().name = quoted(line);
			else if (key == "GENERAL_PORTRAIT")
				generals.back().portrait = quoted(line);
			else if (key == "GENERAL_BACKGROUND")
				// a Language.txt key, or (made in the editor) the text itself
				generals.back().background = line.find('"') != std::string::npos ? "\"" + quoted(line) : word(ls);
			else if (key == "GENERAL_CATEGORY")
				generals.back().category = word(ls);
			else if (key == "GENERAL_ALIGNMENT")
				generals.back().alignment = quoted(line);
			else if (key == "STARTS_WITH_ITEM" && custom) {
				const std::string item = quoted(line);
				const size_t end = line.rfind('"');
				generals.back().stats[item] = (float)atof(line.c_str() + end + 1);
			}
		}
	}

	std::vector<General> LoadGenerals()
	{
		std::vector<General> generals;
		ReadGenerals("Warrior Kings Game Set\\AI\\Kingmaker AI\\Personalities\\AI_Personalities.gsf", false, generals);
		std::unique_ptr<std::vector<std::string>> files(ListFiles(AI_GENERALS_DIR));
		if (files) {
			std::sort(files->begin(), files->end());
			for (const std::string& f : *files)
				if (f.size() > 4 && StrCICompare(std::string_view(f).substr(f.size() - 4), ".gsf") == 0)
					ReadGenerals(std::string(AI_GENERALS_DIR) + "\\" + f, true, generals);
		}
		// only the ones the original lists (with a category)
		generals.erase(std::remove_if(generals.begin(), generals.end(), [](const General& g) { return g.category.empty(); }), generals.end());
		return generals;
	}

	// The file of a general made in the editor, in the user folder (never the game folder)
	std::filesystem::path CustomGeneralFile(const std::string& name)
	{
		return g_userPath / "saved" / "Warrior Kings Game Set" / "AI Generals" / PathFromUtf8(name + ".gsf");
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
	std::string currentSave;
	bool canSave = true;
	std::vector<Guest> guests;
	std::vector<std::string> chat;
	std::string lobbyState, lobbyStatus;
	bool singlePlayer = true;
	Place place = Place::MAIN_MENU;
	ImDrawList* drawList = nullptr;
	std::vector<General> generals;
	bool generalsLoaded = false;

	explicit Impl(IRenderer* gfx) : gfx(gfx) {}

	const Image& image(const std::string& path)
	{
		auto [it, isNew] = images.try_emplace(path);
		if (isNew) {
			if (FileExists(path.c_str())) {
				Bitmap bmp = Bitmap::loadBitmap(path.c_str());
				it->second.width = bmp.width;
				it->second.height = bmp.height;
				if (bmp.format == BMFORMAT_B8G8R8A8 || bmp.format == BMFORMAT_R8G8B8A8) {
					it->second.alpha.resize((size_t)bmp.width * bmp.height);
					for (size_t i = 0; i < it->second.alpha.size(); i++)
						it->second.alpha[i] = bmp.pixels[i * 4 + 3];
				}
				it->second.tex = gfx->CreateTexture(bmp, 1);
			}
			else
				printf("WARNING: menu image \"%s\" not found\n", path.c_str());
		}
		return it->second;
	}

	// The minimap image of a level's map, for a map preview: the level file names the map
	// (MAP "Maps\...\x.bcm"), and a .bcm keeps a 128x128 RGB image of its playable area right
	// after its header strings (docs/re/minimap.md). Cached under "level:<name>"; empty when the
	// map is no .bcm.
	const Image& levelPreview(const std::string& level)
	{
		auto [it, isNew] = images.try_emplace("level:" + level);
		if (!isNew)
			return it->second;
		const std::string path = "Save_Games\\" + level;
		if (!FileExists(path.c_str()))
			return it->second;
		char* data = nullptr;
		int size = 0;
		LoadFile(path.c_str(), &data, &size, 1);
		data[size] = 0;
		std::string map;
		if (const char* m = strstr(data, "MAP \"")) {
			const char* end = strchr(m + 5, '"');
			if (end)
				map.assign(m + 5, end);
		}
		free(data);
		if (map.size() < 4 || StrCICompare(std::string_view(map).substr(map.size() - 4), ".bcm") != 0 || !FileExists(map.c_str()))
			return it->second;
		LoadFile(map.c_str(), &data, &size);
		const uint8_t* fp = (const uint8_t*)data + 24;
		const uint8_t* fileEnd = (const uint8_t*)data + size;
		auto skipString = [&]() {
			if (fp + 2 > fileEnd) return false;
			const uint16_t bytes = *(const uint16_t*)fp;
			fp += 2 + bytes;
			return fp <= fileEnd;
		};
		// texture database path, scale, sun colour, sun vector, fog colour, sky directory
		bool ok = skipString();
		fp += 4 + 12 + 12 + 12;
		ok = ok && skipString() && fp + 128 * 128 * 3 <= fileEnd;
		if (ok) {
			Bitmap bmp;
			bmp.width = bmp.height = 128;
			bmp.format = BMFORMAT_R8G8B8A8;
			bmp.pixels.resize(128 * 128 * 4);
			for (int i = 0; i < 128 * 128; i++) {
				bmp.pixels[i * 4 + 0] = fp[i * 3 + 0];
				bmp.pixels[i * 4 + 1] = fp[i * 3 + 1];
				bmp.pixels[i * 4 + 2] = fp[i * 3 + 2];
				bmp.pixels[i * 4 + 3] = 255;
			}
			it->second.width = it->second.height = 128;
			it->second.tex = gfx->CreateTexture(bmp, 1);
		}
		free(data);
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
		// The alpha (0..255) of an image's pixel, for hit tests on shaped images (0 outside)
		lua.set_function("imageAlpha", [this](const std::string& path, int x, int y) {
			const Image& img = image(path);
			if (x < 0 || y < 0 || x >= img.width || y >= img.height || img.alpha.empty())
				return 0;
			return (int)img.alpha[(size_t)y * img.width + x];
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
		lua.set_function("getTypedText", []() { return g_textTyped; });
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
		lua.set_function("getCurrentSave", [this]() { return currentSave; });
		lua.set_function("canSaveGame", [this]() { return canSave; });
		// The human players of a save game ({ id, name, colour }), as NUM_HUMAN_PLAYERS lists them
		lua.set_function("getSaveGameHumans", [this](const std::string& file) {
			sol::table list = lua.create_table();
			int n = 1;
			for (const SaveGameHuman& h : ReadSaveGameHumans("Save_Games\\" + file))
				list[n++] = lua.create_table_with("id", h.id, "name", h.name, "colour", h.colour);
			return list;
		});

		// A text file of the game's data (data.bcp, mods), or nil: lets the script read game data
		// such as Campaigns\\Kingmaker.cam
		lua.set_function("readDataFile", [this](const std::string& path) -> sol::object {
			if (!FileExists(path.c_str()))
				return sol::lua_nil;
			char* data = nullptr;
			int size = 0;
			LoadFile(path.c_str(), &data, &size, 1);
			std::string text(data, size);
			free(data);
			return sol::make_object(lua, text);
		});

		// The files of a game data folder (data.bcp, mods, the user folder)
		lua.set_function("listDataFiles", [](const std::string& dir) {
			std::unique_ptr<std::vector<std::string>> files(ListFiles(dir.c_str()));
			std::vector<std::string> list = files ? *files : std::vector<std::string>();
			std::sort(list.begin(), list.end());
			return sol::as_table(list);
		});

		// The AI General Editor: { name, category, alignment, portrait, background, stats = { item = value } }
		// as a PLAYER "<name> Kingmaker" like the original's generals, in the user folder's
		// Warrior Kings Game Set\AI Generals (the game set links AI Generals\*.gsf)
		lua.set_function("saveAIGeneral", [this](sol::table t) {
			const std::string name = t.get_or("name", std::string());
			if (!CampaignProfile::validName(name))
				return false;
			const std::filesystem::path file = CustomGeneralFile(name);
			std::error_code ec;
			std::filesystem::create_directories(file.parent_path(), ec);
			FILE* f = _wfopen(file.c_str(), L"w");
			if (!f)
				return false;
			auto clean = [](std::string text) {
				for (char& c : text)
					if (c == '"' || c == '\n' || c == '\r')
						c = c == '"' ? '\'' : ' ';
				return text;
			};
			fprintf(f, "// Made with the wkbre2 AI General Editor\n\n");
			fprintf(f, "\tPLAYER \"%s Kingmaker\"\n\n", clean(name).c_str());
			fprintf(f, "\t\tINHERITS_FROM \"Kingmaker Player\"\n");
			fprintf(f, "\t\tSTARTS_WITH_NAME \"%s\"\n\n", clean(name).c_str());
			fprintf(f, "\t\tGENERAL_PORTRAIT \"%s\"\n", clean(t.get_or("portrait", std::string())).c_str());
			fprintf(f, "\t\tGENERAL_BACKGROUND \"%s\"\n", clean(t.get_or("background", std::string())).c_str());
			fprintf(f, "\t\tGENERAL_CATEGORY %s\n", clean(t.get_or("category", std::string("VETERAN"))).c_str());
			fprintf(f, "\t\tGENERAL_ALIGNMENT \"%s\"\n\n", clean(t.get_or("alignment", std::string("Renaissance"))).c_str());
			fprintf(f, "\t\tSTARTS_WITH_ITEM \"I Am A Player Designed AI General\" 1\n");
			if (sol::optional<sol::table> stats = t["stats"])
				for (const auto& [k, v] : *stats)
					if (k.is<std::string>() && v.is<double>())
						fprintf(f, "\t\tSTARTS_WITH_ITEM \"%s\" %g\n", clean(k.as<std::string>()).c_str(), v.as<double>());
			fprintf(f, "\n\tEND_PLAYER\n");
			const bool ok = !ferror(f);
			fclose(f);
			generalsLoaded = false;
			return ok;
		});
		lua.set_function("deleteAIGeneral", [this](const std::string& name) {
			std::error_code ec;
			const bool ok = CampaignProfile::validName(name) && std::filesystem::remove(CustomGeneralFile(name), ec);
			generalsLoaded = false;
			return ok;
		});

		// Kingmaker campaign profiles (CampaignProfile.h)
		lua.set_function("getCampaignProfiles", []() { return sol::as_table(CampaignProfile::list()); });
		lua.set_function("getCampaignProfile", [this](const std::string& name) -> sol::object {
			CampaignProfile p;
			if (!CampaignProfile::load(name, p))
				return sol::lua_nil;
			sol::table t = lua.create_table();
			t["name"] = p.name;
			t["conquered"] = sol::as_table(p.conquered);
			sol::table items = lua.create_table();
			for (const auto& [item, value] : p.items)
				items[item] = value;
			t["items"] = items;
			return t;
		});
		lua.set_function("createCampaignProfile", [](const std::string& name) {
			CampaignProfile p;
			if (CampaignProfile::load(name, p))
				return false; // exists
			p.name = name;
			return p.save();
		});
		lua.set_function("deleteCampaignProfile", [](const std::string& name) { return CampaignProfile::remove(name); });

		// Multiplayer lobby
		lua.set_function("getLobbyGuests", [this]() {
			sol::table list = lua.create_table();
			int i = 1;
			for (const Guest& g : guests)
				list[i++] = lua.create_table_with("id", g.id, "name", g.name, "requests", sol::as_table(g.requests));
			return list;
		});
		lua.set_function("getChat", [this]() { return sol::as_table(chat); });
		lua.set_function("getLobbyState", [this]() { return lobbyState; });
		lua.set_function("getLobbyStatus", [this]() { return lobbyStatus; });
		lua.set_function("getPlayerName", []() { return g_settings.value("playerName", std::string("Player")); });
		lua.set_function("getJoinAddress", []() { return g_settings.value("joinAddress", std::string("localhost")); });
		// Draws a level's map image into a box; returns false when the level has none
		lua.set_function("drawLevelPreview", [this](const std::string& level, float x, float y, float w, float h) {
			const Image& img = levelPreview(level);
			if (img.tex)
				drawList->AddImage((ImTextureID)img.tex, ImVec2(x, y), ImVec2(x + w, y + h));
			return img.tex != nullptr;
		});
		// The original's AI Generals: { blueprint, name, category ("CONSCRIPT", "VETERAN", "ELITE"), portrait, biography }
		lua.set_function("getGenerals", [this]() {
			if (!generalsLoaded) {
				generals = LoadGenerals();
				generalsLoaded = true;
			}
			sol::table list = lua.create_table();
			int i = 1;
			for (const General& g : generals) {
				std::string bio = g.background.empty() ? std::string()
					: g.background[0] == '"' ? g.background.substr(1) : lang.getText(g.background);
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
				sol::table stats = lua.create_table();
				for (const auto& [item, value] : g.stats)
					stats[item] = value;
				list[i++] = lua.create_table_with("blueprint", g.blueprint,
					"name", !g.nameTag.empty() ? lang.getText(g.nameTag) : !g.name.empty() ? g.name : g.blueprint,
					"category", g.category, "portrait", g.portrait, "biography", bio,
					"custom", g.custom, "alignment", g.alignment, "stats", stats);
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
void FrontEnd::setCurrentSave(const std::string& name) { impl->currentSave = name; }
void FrontEnd::setCanSave(bool canSave) { impl->canSave = canSave; }

std::vector<FrontEnd::SaveGameHuman> FrontEnd::ReadSaveGameHumans(const std::string& path)
{
	std::vector<SaveGameHuman> humans;
	if (!FileExists(path.c_str()))
		return humans;
	char* data = nullptr;
	int size = 0;
	LoadFile(path.c_str(), &data, &size, 1);
	std::istringstream in(std::string(data, size));
	free(data);
	std::string line;
	SaveGameHuman* current = nullptr;
	int depth = 0;
	while (std::getline(in, line)) {
		std::istringstream ls(line);
		std::string tag;
		ls >> tag;
		if (tag == "NUM_HUMAN_PLAYERS") {
			int count = 0;
			ls >> count;
			for (int i = 0; i < count; i++) {
				SaveGameHuman h;
				ls >> h.id;
				humans.push_back(h);
			}
		}
		else if (tag == "PLAYER") {
			uint32_t id = 0;
			ls >> id;
			current = nullptr;
			for (SaveGameHuman& h : humans)
				if (h.id == id)
					current = &h;
			depth = 0;
		}
		else if (!current)
			continue;
		else if (tag.rfind("END_", 0) == 0) {
			if (tag == "END_PLAYER" && depth == 0)
				current = nullptr;
			else
				depth--;
		}
		else if (tag == "CHARACTER" || tag == "BUILDING" || tag == "CITY" || tag == "TOWN" || tag == "ARMY" || tag == "FORMATION"
			|| tag == "MARKER" || tag == "CONTAINER" || tag == "PROP" || tag == "MISSILE" || tag == "TERRAIN_ZONE" || tag == "USER")
			depth++;
		else if (depth > 0)
			continue;
		else if (tag == "COLOUR_INDEX")
			ls >> current->colour;
		else if (tag == "NAME") {
			// Battles: a character count and the codes; otherwise a quoted string
			std::string rest;
			std::getline(ls, rest);
			const size_t q = rest.find('"');
			if (q != std::string::npos)
				current->name = rest.substr(q + 1, rest.rfind('"') - q - 1);
			else {
				std::istringstream cs(rest);
				int count = 0, code = 0;
				cs >> count;
				std::wstring w;
				for (int i = 0; i < count && cs >> code; i++)
					w += (wchar_t)code;
				current->name = PathToUtf8(std::filesystem::path(w));
			}
		}
	}
	return humans;
}
void FrontEnd::saved(bool ok, const std::string& name) { impl->call("FE_Saved", ok, name); }

void FrontEnd::setLobby(std::vector<Guest> guests, const std::vector<std::string>& chat, const std::string& state, const std::string& status)
{
	impl->guests = std::move(guests);
	if (chat.size() != impl->chat.size())
		impl->chat = chat;
	impl->lobbyState = state;
	impl->lobbyStatus = status;
}
