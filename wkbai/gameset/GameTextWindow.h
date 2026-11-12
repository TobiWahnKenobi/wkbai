// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include "actions.h"

struct GameSet;
struct GSFileParser;

struct GameTextWindow {
	struct Page {
		std::string textBody;
		std::string activationSound;
		void parse(GSFileParser& gsf, GameSet& gs);
	};
	struct Button {
		std::string text;
		int onClickWindowAction = -1;
		ActionSequence onClickSequence;
		// BOTTOMLEFT_X/Y: the button's bottom-left corner in pixels from the window's bottom-left
		// corner; PIXEL_WIDTH/HEIGHT its size
		float x = 0, y = 0, width = 75, height = 25;
		void parse(GSFileParser& gsf, GameSet& gs);
	};
	std::vector<Page> pages;
	std::vector<Button> buttons;
	// The window's layout (CGameTextWindowBlueprint, WKB.exe's parser at 0x780920): BOTTOMLEFT_X/Y,
	// its bottom-left corner in fractions of the screen (y down from the top, as the windows'
	// usual 0.8 reads: above the bottom panels); PIXEL_WIDTH, its width (the height follows the
	// text); PUT_AT_BOTTOM_OF_SCREEN (wkbai: its bottom edge at the screen's bottom); MODAL (the
	// game gets no mouse input while it shows); FORCE_READ_ALL_PAGES (it closes only from its
	// last page)
	float x = 0.2f, y = 0.4f, width = 550;
	bool atBottom = false, modal = false, forceReadAllPages = false;
	void parse(GSFileParser& gsf, GameSet& gs);
};

// MISSION_OBJECTIVES "<name>": a level's objectives (the tutorials' tasks). MISSION_NAME, the
// window's title (a Language.txt key); MISSION_OBJECTIVE <key> <HIDE|SHOW|SHOW_INACTIVE>, the
// entries in their order and their state at the start. The actions SET_ACTIVE_MISSION_OBJECTIVES,
// SHOW_MISSION_OBJECTIVES_ENTRY(_INACTIVE) and HIDE_MISSION_OBJECTIVES_ENTRY change them.
struct MissionObjectives {
	enum State : uint8_t { HIDDEN = 0, SHOWN = 1, INACTIVE = 2 };
	std::string missionName;
	std::vector<std::pair<std::string, State>> entries;
	void parse(GSFileParser& gsf, GameSet& gs);
};

// COMPANY "<name>" (Companies.cpp): a company a player can enlist for a Valhalla game against its
// Resource Points (the lobby's Army Enlistment). Its PLAYER_START_SEQUENCE runs on the player at
// the start, once per enlistment (it raises "<company> Spawn"; Valhalla Mode.cpp musters it).
struct GSCompany {
	std::string name, nameTag, descriptionTag, unitTypeTag, weaponPlatform, thumbnail;
	std::vector<std::string> alignments;
	int cost = 0;
	ActionSequence playerStartSequence;
	void parse(GSFileParser& gsf, GameSet& gs);
};
