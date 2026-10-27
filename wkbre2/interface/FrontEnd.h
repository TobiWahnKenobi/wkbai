// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct IRenderer;

// The menus: the main menu and its screens, and the in-game menu (Esc). The screens are a Lua
// script (redata/FrontEndUI.lua) drawn with the original's interface art and bitmap fonts; this
// host gives the script drawing, text, input, the save games and the options (MenuOptions.h),
// and collects what the player asked for (start a game, quit, ...) as requests.
struct FrontEnd {
	enum class Place { MAIN_MENU, IN_GAME };
	struct Request {
		std::string action, arg;
	};

	explicit FrontEnd(IRenderer* gfx);
	~FrontEnd();

	// Runs the menu script for this frame: call between ImGuiImpl_NewFrame and the ImGui rendering
	void frame(Place place);
	// In game: whether the in-game menu is showing (then the game gets no input)
	bool isOpen() const;
	void open(const std::string& screen);
	void close();
	std::vector<Request> takeRequests();

	void setSaveGames(std::vector<std::string> saves);
	void setSinglePlayer(bool singlePlayer);
	// The save game the game in progress was loaded from or last saved to (no extension), or empty:
	// "Save Game" saves there without asking
	void setCurrentSave(const std::string& name);
	// The game in progress can be saved (single player, a multiplayer host)
	void setCanSave(bool canSave);
	// The result of a "save" request, shown by the menu
	void saved(bool ok, const std::string& name);

	// The multiplayer lobby: the guests connected to this host ({ id, name, the requests they
	// sent since the last frame }), the chat, the host's lobby as text (for a guest) and the
	// connection ("connecting", "connected", "lost")
	struct Guest {
		int id;
		std::string name;
		std::vector<std::string> requests;
	};
	// A human player of a save game: player id, its NAME and colour
	struct SaveGameHuman {
		uint32_t id = 0;
		std::string name;
		int colour = 0;
	};
	static std::vector<SaveGameHuman> ReadSaveGameHumans(const std::string& path);
	void setLobby(std::vector<Guest> guests, const std::vector<std::string>& chat, const std::string& state, const std::string& status);

	// The one front end of the program, or null (tests)
	static FrontEnd* get();

private:
	struct Impl;
	std::unique_ptr<Impl> impl;
};
