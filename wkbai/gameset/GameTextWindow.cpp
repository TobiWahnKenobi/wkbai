// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#include "GameTextWindow.h"
#include "../util/GSFileParser.h"
#include "gameset.h"

void GameTextWindow::parse(GSFileParser& gsf, GameSet& gs)
{
	gsf.advanceLine();
	while (!gsf.eof) {
		auto tag = gsf.nextTag();
		if (tag == "PAGE") {
			pages.emplace_back();
			pages.back().parse(gsf, gs);
		}
		else if (tag == "BUTTON") {
			buttons.emplace_back();
			buttons.back().parse(gsf, gs);
		}
		else if (tag == "BOTTOMLEFT_X")
			x = gsf.nextFloat();
		else if (tag == "BOTTOMLEFT_Y")
			y = gsf.nextFloat();
		else if (tag == "PIXEL_WIDTH")
			width = gsf.nextFloat();
		else if (tag == "PUT_AT_BOTTOM_OF_SCREEN")
			atBottom = true;
		else if (tag == "MODAL")
			modal = true;
		else if (tag == "FORCE_READ_ALL_PAGES")
			forceReadAllPages = true;
		else if (tag == "GAME_TEXT_WINDOW_END")
			break;
		gsf.advanceLine();
	}
}

void GameTextWindow::Page::parse(GSFileParser& gsf, GameSet& gs)
{
	gsf.advanceLine();
	while (!gsf.eof) {
		auto tag = gsf.nextTag();
		if (tag == "TEXT_BODY")
			textBody = gsf.nextString();
		else if (tag == "ACTIVATION_SOUND")
			activationSound = gsf.nextString(true);
		else if (tag == "PAGE_END")
			break;
		gsf.advanceLine();
	}
}

void GameTextWindow::Button::parse(GSFileParser& gsf, GameSet& gs)
{
	gsf.advanceLine();
	while (!gsf.eof) {
		auto tag = gsf.nextTag();
		if (tag == "BUTTON_TEXT") {
			text = gsf.nextString();
		}
		else if (tag == "ON_CLICK_WINDOW_ACTION") {
			onClickWindowAction = Tags::GTW_BUTTON_WINDOW_ACTION_tagDict.getTagID(gsf.nextString().c_str());
		}
		else if (tag == "ON_CLICK_SEQUENCE") {
			onClickSequence.init(gsf, gs, "END_ON_CLICK_SEQUENCE");
		}
		else if (tag == "BOTTOMLEFT_X")
			x = gsf.nextFloat();
		else if (tag == "BOTTOMLEFT_Y")
			y = gsf.nextFloat();
		else if (tag == "PIXEL_WIDTH")
			width = gsf.nextFloat();
		else if (tag == "PIXEL_HEIGHT")
			height = gsf.nextFloat();
		else if (tag == "BUTTON_END")
			break;
		gsf.advanceLine();
	}
}

void GSCompany::parse(GSFileParser& gsf, GameSet& gs)
{
	gsf.advanceLine();
	while (!gsf.eof) {
		auto tag = gsf.nextTag();
		if (tag == "NAME_TAG")
			nameTag = gsf.nextString();
		else if (tag == "DESCRIPTION_TAG")
			descriptionTag = gsf.nextString();
		else if (tag == "UNIT_TYPE_TAG")
			unitTypeTag = gsf.nextString();
		else if (tag == "AVAILABLE_TO_ALIGNMENT")
			alignments.push_back(gsf.nextString(true));
		else if (tag == "WEAPON_PLATFORM")
			weaponPlatform = gsf.nextString(true);
		else if (tag == "THUMBNAIL_BITMAP")
			thumbnail = gsf.nextString(true);
		else if (tag == "RESOURCE_POINT_COST")
			cost = gsf.nextInt();
		else if (tag == "PLAYER_START_SEQUENCE")
			playerStartSequence.init(gsf, gs, "END_PLAYER_START_SEQUENCE");
		else if (tag == "END_COMPANY")
			break;
		gsf.advanceLine();
	}
}

void MissionObjectives::parse(GSFileParser& gsf, GameSet& gs)
{
	gsf.advanceLine();
	while (!gsf.eof) {
		auto tag = gsf.nextTag();
		if (tag == "MISSION_NAME")
			missionName = gsf.nextString();
		else if (tag == "MISSION_OBJECTIVE") {
			std::string key = gsf.nextString();
			const std::string state = gsf.nextString();
			entries.emplace_back(std::move(key), state == "SHOW" ? SHOWN : (state == "SHOW_INACTIVE" || state == "INACTIVE") ? INACTIVE : HIDDEN);
		}
		else if (tag == "END_MISSION_OBJECTIVES")
			break;
		gsf.advanceLine();
	}
}
