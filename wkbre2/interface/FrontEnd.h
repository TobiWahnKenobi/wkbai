// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#pragma once

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

	// The one front end of the program, or null (tests)
	static FrontEnd* get();

private:
	struct Impl;
	std::unique_ptr<Impl> impl;
};
