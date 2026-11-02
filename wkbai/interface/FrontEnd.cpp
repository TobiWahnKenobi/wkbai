// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#include "FrontEnd.h"
#include "../terrain.h"
#include "LuaScripts.h"
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
#include "../matchmaking/Matchmaking.h"
#include "../AIController.h"
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
#include <chrono>
#include <ctime>

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

	// What the Replays list shows of a recording: the name the player gave it (name.txt), its map
	// (setup.json's level, else the level's NAME in start.sav) and its length (events.bin's
	// stepped calls, as Server::startReplay reads them). Kept per folder until a file changes.
	struct RecordingInfo {
		std::string name, map;
		uint64_t lengthMs = 0;
	};

	std::string ReadSmallFile(const std::filesystem::path& path, size_t limit)
	{
		std::string text;
		if (FILE* f = _wfopen(path.wstring().c_str(), L"rb")) {
			char buf[4096];
			size_t n;
			while (text.size() < limit && (n = fread(buf, 1, std::min(sizeof(buf), limit - text.size()), f)) > 0)
				text.append(buf, n);
			fclose(f);
		}
		return text;
	}

	RecordingInfo recordingInfo(const std::filesystem::path& folder)
	{
		struct Cached { std::filesystem::file_time_type events, name; RecordingInfo info; };
		static std::map<std::filesystem::path, Cached> cache;
		std::error_code ec;
		const auto eventsTime = std::filesystem::last_write_time(folder / "events.bin", ec);
		const auto nameTime = std::filesystem::last_write_time(folder / "name.txt", ec);
		if (auto it = cache.find(folder); it != cache.end() && it->second.events == eventsTime && it->second.name == nameTime)
			return it->second.info;
		RecordingInfo info;
		info.name = ReadSmallFile(folder / "name.txt", 200);
		while (!info.name.empty() && (info.name.back() == '\n' || info.name.back() == '\r' || info.name.back() == ' '))
			info.name.pop_back();
		const nlohmann::json setup = nlohmann::json::parse(ReadSmallFile(folder / "setup.json", 1 << 20), nullptr, false);
		if (setup.is_object())
			info.map = setup.value("level", std::string());
		if (info.map.empty()) {
			// the level's NAME "<file>.lvl", near the start of the save game
			const std::string sav = ReadSmallFile(folder / "start.sav", 64 << 10);
			for (size_t at = sav.find("\nLEVEL "); at != std::string::npos;) {
				const size_t q = sav.find("NAME \"", at);
				const size_t e = q == std::string::npos ? q : sav.find('"', q + 6);
				if (e != std::string::npos)
					info.map = sav.substr(q + 6, e - q - 6);
				break;
			}
		}
		if (const size_t dot = info.map.rfind('.'); dot != std::string::npos)
			info.map.resize(dot);
		if (FILE* f = _wfopen((folder / "events.bin").wstring().c_str(), L"rb")) {
			char magic[7] = {};
			uint8_t clients = 0;
			if (fread(magic, 1, 7, f) == 7 && memcmp(magic, "WKBREC1", 7) == 0 && fread(&clients, 1, 1, f) == 1
				&& fseek(f, 4L * clients, SEEK_CUR) == 0) {
				int type;
				while ((type = fgetc(f)) != EOF) {
					if (type == 'S') {
						uint32_t ms = 0, count = 0;
						uint8_t paused = 0;
						if (fread(&ms, 4, 1, f) != 1 || fread(&paused, 1, 1, f) != 1 || fread(&count, 4, 1, f) != 1)
							break;
						info.lengthMs += (uint64_t)ms * count;
					}
					else if (type == 'M') {
						uint8_t client = 0;
						uint16_t length = 0;
						if (fread(&client, 1, 1, f) != 1 || fread(&length, 2, 1, f) != 1 || fseek(f, length, SEEK_CUR) != 0)
							break;
					}
					else if (type == 'R') {
						if (fseek(f, 10, SEEK_CUR) != 0)
							break;
					}
					else
						break;
				}
			}
			fclose(f);
		}
		cache[folder] = { eventsTime, nameTime, info };
		return info;
	}
}

struct FrontEnd::Impl {
	IRenderer* gfx;
	sol::state lua;
	bool scriptOk = false;
	UIScriptFiles scriptFiles; // what loadScripts ran (hot reload)
	uint64_t scriptCheckTime = 0;
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

	// The start positions of a level's playable players ("Human Player"s, in the order of their
	// ids, as the skirmish slots), as fractions of its map's playable area with north up (as the
	// map's image): each player's Manor (Keep, Castle, Palace), else its start camera. Cached.
	std::map<std::string, std::vector<std::pair<float, float>>> levelStarts;
	const std::vector<std::pair<float, float>>& startsOf(const std::string& level)
	{
		auto [it, isNew] = levelStarts.try_emplace(level);
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
		// the players' blocks: "PLAYER <id> \"<blueprint>\"" (one tab in), their buildings and the
		// POSITION after each, their START_CAMERA_POS
		struct Start { uint32_t id; bool human; bool haveBase = false; float x = 0, z = 0, camX = 0, camZ = 0; };
		std::vector<Start> players;
		Start* cur = nullptr;
		bool baseNext = false;
		std::istringstream in(data);
		std::string line;
		while (std::getline(in, line)) {
			const size_t first = line.find_first_not_of(" \t");
			if (first == std::string::npos)
				continue;
			const std::string_view word = std::string_view(line).substr(first, line.find_first_of(" \t", first) - first);
			if (word == "PLAYER" && first == 1 && line.find('"') != std::string::npos) {
				Start s{ (uint32_t)strtoul(line.c_str() + first + 7, nullptr, 10), line.find("\"Human Player\"") != std::string::npos };
				players.push_back(s);
				cur = &players.back();
				baseNext = false;
			}
			else if (cur && word == "BUILDING" && !cur->haveBase) {
				for (const char* base : { "\"Manor\"", "\"Keep", "\"Castle", "\"Palace" })
					if (line.find(base) != std::string::npos)
						baseNext = true;
			}
			else if (cur && word == "POSITION" && baseNext) {
				sscanf(line.c_str() + first + 8, "%f %*f %f", &cur->x, &cur->z);
				cur->haveBase = true;
				baseNext = false;
			}
			else if (cur && word == "START_CAMERA_POS")
				sscanf(line.c_str() + first + 16, "%f %*f %f", &cur->camX, &cur->camZ);
		}
		free(data);
		if (map.empty() || !FileExists(map.c_str()))
			return it->second;
		Terrain trn;
		trn.readFromFile(map.c_str());
		const auto [w, d] = trn.getPlayableArea();
		std::sort(players.begin(), players.end(), [](const Start& a, const Start& b) { return a.id < b.id; });
		for (const Start& s : players) {
			if (!s.human)
				continue;
			const float x = s.haveBase ? s.x : s.camX, z = s.haveBase ? s.z : s.camZ;
			it->second.emplace_back(std::clamp(x / std::max(w, 1.0f), 0.0f, 1.0f), std::clamp(1.0f - z / std::max(d, 1.0f), 0.0f, 1.0f));
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
		// in points (window.h)
		lua.set_function("getWindowWidth", []() { return (int)(g_windowWidth / g_uiScale); });
		lua.set_function("getWindowHeight", []() { return (int)(g_windowHeight / g_uiScale); });
		lua.set_function("getTime", []() { return SDL_GetTicks() / 1000.0; });
		lua.set_function("isShiftHeld", []() { return g_modShift; });
		lua.set_function("getMouseX", []() { return UiMouseX(); });
		lua.set_function("getMouseY", []() { return UiMouseY(); });
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
		// the game recordings (folders in saved\Recordings with a start.sav), newest first (by
		// their start.sav: a rename doesn't move one up): { folder, name (name.txt, else the
		// folder's), map, date, length (seconds of game time) }
		lua.set_function("getRecordings", [this]() {
			struct Found { std::filesystem::file_time_type time; std::string folder; };
			std::vector<Found> found;
			std::error_code ec;
			const std::filesystem::path root = g_userPath / "saved" / "Recordings";
			for (const auto& entry : std::filesystem::directory_iterator(root, ec))
				if (entry.is_directory(ec) && std::filesystem::exists(entry.path() / "start.sav", ec))
					found.push_back({ std::filesystem::last_write_time(entry.path() / "start.sav", ec), PathToUtf8(entry.path().filename()) });
			std::sort(found.begin(), found.end(), [](const Found& a, const Found& b) { return a.time > b.time; });
			sol::table list = lua.create_table();
			int n = 1;
			for (const Found& f : found) {
				const RecordingInfo info = recordingInfo(root / PathFromUtf8(f.folder));
				char date[32] = {};
				const std::time_t t = std::chrono::system_clock::to_time_t(std::chrono::clock_cast<std::chrono::system_clock>(f.time));
				if (const std::tm* tm = std::localtime(&t))
					std::strftime(date, sizeof(date), "%Y-%m-%d %H:%M", tm);
				list[n++] = lua.create_table_with("folder", f.folder, "name", info.name.empty() ? f.folder : info.name,
					"map", info.map, "date", std::string(date), "length", info.lengthMs / 1000.0);
			}
			return list;
		});
		// a recording's own name (its name.txt; the folder stays): empty goes back to the folder's
		lua.set_function("renameRecording", [](const std::string& folder, const std::string& name) {
			if (folder.empty() || folder.find_first_of("\/") != std::string::npos || folder.find("..") != std::string::npos)
				return false;
			const std::filesystem::path dir = g_userPath / "saved" / "Recordings" / PathFromUtf8(folder);
			std::error_code ec;
			if (!std::filesystem::is_directory(dir, ec))
				return false;
			const std::filesystem::path file = dir / "name.txt";
			if (name.find_first_not_of(" ") == std::string::npos)
				return std::filesystem::remove(file, ec) || !ec;
			FILE* f = _wfopen(file.wstring().c_str(), L"wb");
			if (!f)
				return false;
			const bool ok = fwrite(name.data(), 1, name.size(), f) == name.size();
			fclose(f);
			return ok;
		});
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
			fprintf(f, "// Made with the wkbai AI General Editor\n\n");
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

		// Matchmaking: the games every service found, a group per service (the local network
		// first): { id, title, state ("ready", "off", "signing in", "failed"), text, games = {
		// { id, name, host, map, players, places, spectators, maxSpectators, allowSpectators,
		// password, running, versionOk, mods ("match", "host sends", "differ", "no mods", "?"),
		// modsOk, modsWhy, ping (ms, -1 unknown) }, ... } }
		lua.set_function("getGameList", [this]() {
			sol::table groups = lua.create_table();
			const std::string mine = ModManifest();
			const nlohmann::json mineJson = nlohmann::json::parse(mine, nullptr, false);
			const bool haveMods = mineJson.is_object() && !mineJson.value("mods", nlohmann::json::array()).empty();
			int g = 1;
			for (const auto& s : Matchmaking::get().services()) {
				static const char* states[] = { "off", "signing in", "ready", "failed" };
				sol::table games = lua.create_table();
				int i = 1;
				for (const GameListing& l : s->listings()) {
					if (!Matchmaking::get().filter.accepts(l))
						continue;
					std::string mods = "?", why;
					bool modsOk = true;
					if (!l.modManifest.empty()) {
						why = CompareModLogic(l.modManifest, mine);
						if (l.modPolicy == 2 && haveMods) // Server::MODS_NONE
							mods = "no mods", modsOk = false, why = "the host allows no mods";
						else if (why.empty())
							mods = "match";
						else if (l.modPolicy == 1) // Server::MODS_SEND
							mods = "host sends";
						else
							mods = "differ", modsOk = false;
					}
					sol::table t = lua.create_table();
					t["id"] = l.id;
					t["name"] = l.name;
					t["host"] = l.hostName;
					t["map"] = l.map;
					t["players"] = l.players;
					t["places"] = l.places;
					t["spectators"] = l.spectators;
					t["maxSpectators"] = l.maxSpectators;
					t["allowSpectators"] = l.allowSpectators;
					t["password"] = l.password;
					t["running"] = l.running;
					t["versionOk"] = l.version == MatchmakingGameVersion();
					t["mods"] = mods;
					t["modsOk"] = modsOk;
					t["modsWhy"] = why;
					t["ping"] = MatchmakingPing(l);
					games[i++] = t;
				}
				groups[g++] = lua.create_table_with("id", s->id(), "title", s->title(), "state", states[(int)s->state()],
					"text", s->stateText(), "icon", s->icon(), "games", games);
			}
			return groups;
		});
		lua.set_function("refreshGameList", []() { Matchmaking::get().refresh(); });
		// the list's filter: { hideFull, hidePassword, sameVersionOnly, text }
		lua.set_function("setGameListFilter", [](sol::table f) {
			ListFilter& filter = Matchmaking::get().filter;
			filter.hideFull = f.get_or("hideFull", false);
			filter.hidePassword = f.get_or("hidePassword", false);
			filter.sameVersionOnly = f.get_or("sameVersionOnly", false);
			filter.text = f.get_or("text", std::string());
		});
		// the services a host can offer its game on: { id, title, state, text, offered }
		lua.set_function("getMatchmakingServices", [this]() {
			sol::table list = lua.create_table();
			int i = 1;
			static const char* states[] = { "off", "signing in", "ready", "failed" };
			for (const auto& s : Matchmaking::get().services())
				list[i++] = lua.create_table_with("id", s->id(), "title", s->title(), "state", states[(int)s->state()],
					"text", s->stateText(), "offered", Matchmaking::get().hostedOn().count(s->id()) > 0,
					"chosen", Matchmaking::get().offerOn.count(s->id()) > 0);
			return list;
		});
		// Draws a level's map image into a box; returns false when the level has none
		lua.set_function("drawLevelPreview", [this](const std::string& level, float x, float y, float w, float h) {
			const Image& img = levelPreview(level);
			if (img.tex)
				drawList->AddImage((ImTextureID)img.tex, ImVec2(x, y), ImVec2(x + w, y + h));
			return img.tex != nullptr;
		});
		// A level's start positions as { x, y } fractions of its map image (startsOf), in the order
		// of the skirmish slots
		// the Lua AIs an AI position can have (LuaAI\*.lua: redata's and the mods')
		lua.set_function("getLuaAIs", [this]() { return sol::as_table(ListLuaAIs()); });
		lua.set_function("getLevelStarts", [this](const std::string& level) {
			sol::table list = lua.create_table();
			int n = 1;
			for (const auto& [x, y] : startsOf(level))
				list[n++] = lua.create_table_with("x", x, "y", y);
			return list;
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

		// Mods (file.h): the mods in load order, { id, name, description, kind, version, enabled,
		// problem }, whether the order is the player's own, and applying a new list
		lua.set_function("getMods", [this]() {
			sol::table list = lua.create_table();
			int i = 1;
			for (const ModInfo& m : GetMods())
				list[i++] = lua.create_table_with("id", m.id, "name", m.name, "description", m.description, "kind", m.kind,
					"version", m.version, "enabled", m.enabled, "problem", m.problem);
			return list;
		});
		lua.set_function("isCustomModOrder", []() { return IsCustomModLoadOrder(); });
		// mods: { { id, enabled }, ... } in the new order; custom: keep that order (else newest file wins)
		lua.set_function("applyMods", [](sol::table list, bool custom) {
			const std::vector<ModInfo>& current = GetMods();
			std::vector<ModInfo> mods;
			for (size_t i = 1; i <= list.size(); i++) {
				sol::table entry = list[i];
				const std::string id = entry.get_or("id", std::string());
				auto it = std::find_if(current.begin(), current.end(), [&](const ModInfo& m) { return m.id == id; });
				if (it == current.end())
					continue;
				ModInfo mod = *it;
				mod.enabled = entry.get_or("enabled", true);
				mods.push_back(std::move(mod));
			}
			ApplyModConfiguration(mods, custom);
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
				t["type"] = o.type == MenuOption::TOGGLE ? "toggle" : o.type == MenuOption::SLIDER ? "slider" : o.type == MenuOption::TEXT ? "text" : "choice";
				t["hint"] = o.hint;
				t["maxLength"] = o.maxLength;
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
			return o && o->get ? o->get() : 0.0f;
		});
		lua.set_function("setOption", [](const std::string& id, float v) {
			if (const MenuOption* o = FindMenuOption(id); o && o->set)
				o->set(v);
		});
		lua.set_function("getOptionText", [](const std::string& id) {
			const MenuOption* o = FindMenuOption(id);
			return o && o->getText ? o->getText() : std::string();
		});
		lua.set_function("setOptionText", [](const std::string& id, const std::string& text) {
			if (const MenuOption* o = FindMenuOption(id); o && o->setText)
				o->setText(text);
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
	loadScripts();
}

void FrontEnd::loadScripts()
{
	try {
		// FrontEndUI.lua (a mod's or redata's), then the mods' Lua\FrontEnd\*.lua
		impl->scriptOk = RunUIScripts(impl->lua, "FrontEndUI.lua", "Lua\\FrontEnd", "Menu", &impl->scriptFiles);
	}
	catch (const sol::error& err) {
		printf("Menu script error:\n%s\n", err.what());
	}
	impl->scriptCheckTime = SDL_GetTicks();
}

// Hot reload: a script edited, added or removed (checked every second) restarts the menu
// scripts in a new Lua state, on the screen that was open
void FrontEnd::reloadScriptsIfChanged()
{
	if (!HotReloadEnabled() || SDL_GetTicks() - impl->scriptCheckTime < 1000)
		return;
	impl->scriptCheckTime = SDL_GetTicks();
	if (!impl->scriptFiles.changed())
		return;
	std::string screen;
	try {
		sol::protected_function top = impl->lua["FE_Top"];
		if (top.valid())
			if (sol::protected_function_result r = top(); r.valid() && r.get_type() == sol::type::string)
				screen = r.get<std::string>();
	}
	catch (const sol::error&) {}
	printf("Hot reload: menu scripts (screen \"%s\")\n", screen.c_str());
	impl->lua = sol::state();
	impl->bind();
	loadScripts();
	if (!screen.empty())
		open(screen);
}

FrontEnd::~FrontEnd()
{
	if (g_frontEnd == this)
		g_frontEnd = nullptr;
}

FrontEnd* FrontEnd::get() { return g_frontEnd; }

void FrontEnd::frame(Place place)
{
	reloadScriptsIfChanged();
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
