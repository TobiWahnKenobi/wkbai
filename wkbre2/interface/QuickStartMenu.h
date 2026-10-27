// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include "QuickSkirmishMenu.h"
#include "ModMenu.h"
#include "FrontEnd.h"

struct IRenderer;
struct NetEnetLink;
struct _ENetHost;
struct Server;
struct Client;
struct NetLocalBuffer;
struct NetLocalLink;

struct QuickStartMenu {
	QuickStartMenu(IRenderer* gfx);
	~QuickStartMenu();
	// The ImGui windows that the menus still open (skirmish settings, host, join, mods)
	void draw();
	// What the player chose in the menus (FrontEnd requests)
	void handleRequest(const FrontEnd::Request& request);
	void launchGame();
private:
	std::vector<std::string> savegames;
	size_t savselected = 0;
	IRenderer* gfx;

	enum Mode {
		MODE_UNDEFINED = 0,
		MODE_SINGLEPLAYER = 1,
		MODE_MULTIPLAYER_HOST,
		MODE_MULTIPLAYER_JOIN,
	};
	Mode modeChosen = MODE_UNDEFINED;

	_ENetHost* eserver = nullptr;
	std::vector<NetEnetLink*> guests;
	Server* server = nullptr;
	Client* client = nullptr;
	NetLocalBuffer* queue1 = nullptr, * queue2 = nullptr;
	NetLocalLink* srv2cli = nullptr, * cli2srv = nullptr;
	bool enetOn = false;

	bool showSkirmishWnd = false, showHostWnd = false, showJoinWnd = false;
	char joinAddress[64] = "localhost"; uint16_t joinPort = 1234;

	QuickSkirmishMenu skirmishMenu;
	ModMenu modMenu;

	// keepSelection: the selected level stays (a save game was added during the game)
	void refreshSaveGames(bool keepSelection = false);
	void selectSaveGame(const std::string& name);
	// A new server and client, for the next game
	void newSession();
	// After a game: what the player chose in the in-game menu
	void afterGame(const std::string& leaveAction, const std::string& leaveArg);
	void netInit();
	void netHandle();
	void netDeinit();
};