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

	// In game: the game text windows showing (the level scripts' briefings, tutorial and quest
	// texts: GAME_TEXT_WINDOW), oldest first, drawn by the menu script in the original's frame.
	// A button pressed comes back as the request "gtw_button" "<id>,<button index>".
	struct GameWindow {
		struct Button {
			std::string text;
			float x = 0, y = 0, width = 75, height = 25; // pixels from the window's bottom left
			bool enabled = true;
		};
		int id = 0;
		std::string text;            // the page's text, translated, "\n" as new lines
		int page = 0, pages = 1;
		float x = 0.2f, y = 0.4f;    // bottom-left corner, fractions of the screen (y from the top)
		float width = 550;           // pixels
		bool atBottom = false, modal = false;
		std::vector<Button> buttons;
	};
	void setGameWindows(std::vector<GameWindow> windows);
	// The mission objectives (MISSION_OBJECTIVES): the active set's title and its shown entries
	// (inactive: shown greyed, SHOW_MISSION_OBJECTIVES_ENTRY_INACTIVE); empty title: none
	struct Objectives {
		struct Entry {
			std::string text;
			bool inactive = false;
		};
		std::string title;
		std::vector<Entry> entries;
		uint32_t changed = 0;        // grows when an entry is shown or hidden (the button flashes)
	};
	void setObjectives(Objectives objectives);
	// The objectives window opened or closed (the top bar's button, the Objectives key), and
	// whether it has news not seen yet
	void toggleObjectives();
	bool objectivesNews() const;
	// Whether the game windows want the mouse at this point (one covered it last frame, or one
	// is modal): the game gets no mouse input then
	bool gameWindowsWantMouse(float x, float y) const;

	// The one front end of the program, or null (tests)
	static FrontEnd* get();

private:
	struct Impl;
	std::unique_ptr<Impl> impl;
	void loadScripts();
	void reloadScriptsIfChanged();
};
