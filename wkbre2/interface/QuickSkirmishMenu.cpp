#include "QuickSkirmishMenu.h"
#include "../server.h"
#include "../imgui/imgui.h"
#include "../gameset/gameset.h"
#include "../util/StriCompare.h"

#include "../settings.h"
#include <nlohmann/json.hpp>

#include <algorithm>
#include <regex>

void QuickSkirmishMenu::imguiWindow()
{
	const bool isWKBattles = g_settings.value<int>("gameVersion", 1) == 2;

	ImGui::SeparatorText("Gameplay mode");
	int modeSelection = (int)mode;
	ImGui::RadioButton("Dev mode", &modeSelection, (int)DevMode);
	ImGui::RadioButton("Standard mode", &modeSelection, (int)StandardMode);
	if (isWKBattles) {
		ImGui::RadioButton("Valhalla mode", &modeSelection, (int)ValhallaMode);
	}
	ImGui::RadioButton("Campaign mode", &modeSelection, (int)CampaignMode);
	mode = (GameMode)modeSelection;

	ImGui::PushItemWidth(200.0f);

	if (mode == StandardMode) {
		ImGui::SeparatorText("Skirmish settings");
		ImGui::InputInt("Start Food", &startFood, 50, 100);
		ImGui::InputInt("Start Wood", &startWood, 50, 100);
		ImGui::InputInt("Start Gold", &startGold, 50, 100);
		ImGui::InputInt("Population Limit", &populationLimit, 10, 50);

		if (isWKBattles) {
			static const char* randomResourceValueNames[4] = {
				"Off", "Low", "Medium", "High"
			};
			ImGui::Combo("Randomise Resources", &randomiseResources, randomResourceValueNames, 4);
			
			ImGui::Checkbox("King Piece", &kingPiece);
			ImGui::Checkbox("Clear Fog of War", &clearFogOfWar);
			ImGui::Checkbox("Allow Diplomacy", &allowDiplomacy);
			ImGui::Checkbox("Team Reconnaissance", &teamReconnaissance);
			ImGui::Checkbox("Barbarian Tribes", &barbarianTribes);
		}
		else {
			ImGui::SeparatorText("Victory Condition");
			ImGui::RadioButton("Destroy Manor", &loseOnManorDestroyed, 1);
			ImGui::RadioButton("Destroy All Enemy Units", &loseOnManorDestroyed, 0);
		}
	}
	else if (mode == ValhallaMode) {
		ImGui::SeparatorText("Valhalla settings");
		ImGui::InputInt("Resource Points", &resourcePoints, 100, 200);
		ImGui::InputInt("Victory Point Limit", &victoryPointLimit, 500, 1000);
		ImGui::InputInt("Valhalla Mode Food", &valhallaModeFood, 500, 1000);
		ImGui::InputInt("Unit Lives", &unitLives);
		ImGui::InputInt("Capturable Flags", &capturableFlags);
		ImGui::Checkbox("Clear Fog of War", &clearFogOfWar);
	}
	else if (mode == CampaignMode) {
		if (isWKBattles) {
			ImGui::SeparatorText("User progress");
			ImGui::InputInt("Start Food", &startFood, 50, 100);
			ImGui::InputInt("Start Wood", &startWood, 50, 100);
			ImGui::InputInt("Start Gold", &startGold, 50, 100);
			ImGui::InputInt("Population Limit", &populationLimit, 10, 50);
			ImGui::InputInt("Pagan Tech Level", &battlesCampaignPaganTechLevel);
			ImGui::InputInt("Imperial Tech Level", &battlesCampaignImperialTechLevel);
			ImGui::InputInt("Renaissance Tech Level", &battlesCampaignRenaissanceTechLevel);
		}
		else {
			ImGui::SeparatorText("Campaign Path");
			ImGui::Text("Side alignment for Renaissance missions:");
			ImGui::RadioButton("Pagan", &originalCampaignCelestialPath, 0);
			ImGui::RadioButton("Imperial", &originalCampaignCelestialPath, 1);
		}
	}

	ImGui::PopItemWidth();
}

void QuickSkirmishMenu::setSetting(const std::string& name, int value)
{
	// skirmish maps only: other levels keep the mode chosen for them
	if (name == "mode") {
		if (mode == StandardMode || mode == ValhallaMode)
			mode = (value == 1) ? ValhallaMode : StandardMode;
	}
	else if (name == "startFood") startFood = value;
	else if (name == "startWood") startWood = value;
	else if (name == "startGold") startGold = value;
	else if (name == "populationLimit") populationLimit = value;
	else if (name == "randomiseResources") randomiseResources = value;
	else if (name == "kingPiece") kingPiece = value != 0;
	else if (name == "clearFogOfWar") clearFogOfWar = value != 0;
	else if (name == "allowDiplomacy") allowDiplomacy = value != 0;
	else if (name == "teamReconnaissance") teamReconnaissance = value != 0;
	else if (name == "barbarianTribes") barbarianTribes = value != 0;
	else if (name == "resourcePoints") resourcePoints = value;
	else if (name == "victoryPointLimit") victoryPointLimit = value;
	else if (name == "valhallaModeFood") valhallaModeFood = value;
	else if (name == "unitLives") unitLives = value;
	else if (name == "capturableFlags") capturableFlags = value;
}

void QuickSkirmishMenu::changeSettingsForLevel(std::string_view levelName)
{
	// If it is not a LVL (such as SAV), then always use DevMode (to not restart the level).
	const auto dotPosition = levelName.rfind('.');
	if (dotPosition == levelName.npos || StrCICompare(levelName.substr(dotPosition + 1), "lvl") != 0) {
		mode = DevMode;
		return;
	}

	// Tutorial
	const char* tutorialNames[] = {
		"Advanced Economy.lvl",
		"Advanced Military.lvl",
		"Basic Economy.lvl",
		"Basic Military.lvl",
		"Interactive Tutorial.lvl"
	};
	for (const auto& tutoName : tutorialNames) {
		if (StrCICompare(levelName, tutoName) == 0) {
			mode = CampaignMode;
			return;
		}
	}

	// Original Campaign level
	if (StrCICompare(levelName, "Prologue.lvl") == 0) {
		mode = CampaignMode;
		return;
	}
	if (std::regex_search(levelName.begin(), levelName.end(), std::regex("Level \\d+ - ", std::regex_constants::icase))) {
		mode = CampaignMode;
		return;
	}

	// Battles Campaign level
	// They can generally be used in Standard, Valhalla and Campaign mode.
	// But prefer Campaign mode for now.
	if (StrCICompare(levelName.substr(0, 4), "CH2_") == 0) {
		const bool isMulti = StrCICompare(levelName.substr(std::max(0, (int)dotPosition - 6), 6), "_Multi") == 0;
		if (!isMulti) {
			mode = CampaignMode;
			return;
		}
	}

	// Otherwise it is a regular skirmish map level
	if (mode != ValhallaMode) {
		mode = StandardMode;
	}
}

void QuickSkirmishMenu::applySettings(Server& server)
{
	auto setItemByName = [&server](ServerGameObject* obj, const char* name, float value) {
		int item = server.gameSet->items.names.getIndex(name);
		if (item != -1) {
			obj->setItem(item, value);
		}
		};

	ServerGameObject* level = server.getLevel();
	const bool isWKBattles = server.gameSet->version == GameSet::GSVERSION_WKBATTLES;
	std::vector<SrvGORef> aiPlayers; // the AI players, in a game without any human

	if (mode == StandardMode) {
		for (auto& [objType, objList] : level->children) {
			for (auto* comObj : objList) {
				ServerGameObject* obj = comObj->dyncast<ServerGameObject>();
				if (obj->blueprint->bpClass == Tags::GAMEOBJCLASS_PLAYER) {
					obj->setItem(Tags::PDITEM_FOOD, startFood);
					obj->setItem(Tags::PDITEM_WOOD, startWood);
					obj->setItem(Tags::PDITEM_GOLD, startGold);
					setItemByName(obj, "Total Population Count", populationLimit);

					if (isWKBattles) {
						setItemByName(obj, "King Piece Lose Condition Set", kingPiece);
						setItemByName(obj, "Diplomacy Enabled", allowDiplomacy);
					}
					else {
						setItemByName(obj, "Lose on Manor Destroyed", loseOnManorDestroyed);
					}
				}
			}
		}

		if (isWKBattles) {
			setItemByName(level, "Amount of Resources", randomiseResources);
			setItemByName(level, "Random Barbarians Chosen", barbarianTribes);
		}
	}

	if (mode == ValhallaMode && isWKBattles) {
		setItemByName(level, "Valhalla Mode Chosen", 1.0f);
		setItemByName(level, "Number of Capturable Flags", capturableFlags);

		for (auto& [objType, objList] : level->children) {
			for (auto* comObj : objList) {
				ServerGameObject* obj = comObj->dyncast<ServerGameObject>();
				if (obj->blueprint->bpClass == Tags::GAMEOBJCLASS_PLAYER) {
					setItemByName(obj, "Valhalla Resource Points", resourcePoints);
					setItemByName(obj, "Victory Time Limit", victoryPointLimit);
					setItemByName(obj, "Food", valhallaModeFood);
					setItemByName(obj, "No. Of Lives Left", unitLives);

				}
			}
		}
	}

	if (mode == CampaignMode) {
		const GameObjBlueprint* userBlueprint = server.gameSet->findBlueprint(Tags::GAMEOBJCLASS_USER, "Default");
		ServerGameObject* firstPlayerObject = server.findObject(1027);

		if (userBlueprint && firstPlayerObject) {
			ServerGameObject* userObject = server.createObject(userBlueprint);
			userObject->setParent(firstPlayerObject);

			if (isWKBattles && campaignProfile && !campaignUserItems.empty()) {
				// carried over from the provinces conquered before
				setItemByName(level, "Skirmish Campaign Level", 1.0f);
				for (const auto& [item, value] : campaignUserItems)
					setItemByName(userObject, item.c_str(), value);
			}
			else if (isWKBattles) {
				setItemByName(level, "Skirmish Campaign Level", 1.0f);
				setItemByName(userObject, "Food", startFood);
				setItemByName(userObject, "Wood", startWood);
				setItemByName(userObject, "Gold", startGold);
				setItemByName(userObject, "Total Population Count", populationLimit);
				setItemByName(userObject, "Pagan Tech Level", battlesCampaignPaganTechLevel);
				setItemByName(userObject, "Imperial Tech Level", battlesCampaignImperialTechLevel);
				setItemByName(userObject, "Renaissance Tech Level", battlesCampaignRenaissanceTechLevel);
			}
			else {
				setItemByName(userObject, "Chosen Celestial", originalCampaignCelestialPath);
			}
		}
	}

	if (mode != DevMode && isWKBattles && !slots.empty()) {
		// The map's start positions in the order of their player ids
		auto* humanPlayer = server.gameSet->findBlueprint(Tags::GAMEOBJCLASS_PLAYER, "Human Player");
		std::vector<ServerGameObject*> players;
		if (auto it = server.getLevel()->children.find(humanPlayer); it != server.getLevel()->children.end())
			for (CommonGameObject* obj : it->second)
				players.push_back(obj->dyncast<ServerGameObject>());
		std::sort(players.begin(), players.end(), [](auto* a, auto* b) { return a->id < b->id; });

		// The AI Generals: the PLAYER blueprints "... Kingmaker" (AI_Personalities.gsf)
		std::vector<const GameObjBlueprint*> generals;
		const auto& playerTypes = server.gameSet->objBlueprints[Tags::GAMEOBJCLASS_PLAYER];
		for (size_t i = 0; i < playerTypes.size(); i++) {
			const std::string& name = playerTypes.names.getString(i);
			if (name.size() > 10 && StrCICompare(std::string_view(name).substr(name.size() - 10), " Kingmaker") == 0)
				generals.push_back(&playerTypes[i]);
		}

		bool humanPlaced = false, guestPlaced = false;
		std::vector<std::pair<ServerGameObject*, int>> playing; // with their teams
		for (size_t i = 0; i < players.size(); i++) {
			const Slot slot = i < slots.size() ? slots[i] : Slot{};
			ServerGameObject* player = players[i];
			if (slot.type == Slot::EMPTY) {
				// nobody plays here: the player goes with its units and buildings
				printf("Skirmish: position %zu (player %u): empty\n", i + 1, player->id);
				server.deleteObject(player);
				continue;
			}
			if (slot.type == Slot::HUMAN && !humanPlaced) {
				server.setClientPlayerControl(0, player->id);
				humanPlaced = true;
				if (!slot.name.empty())
					player->setName(slot.name);
				printf("Skirmish: position %zu (player %u): human\n", i + 1, player->id);
			}
			else if (slot.type == Slot::GUEST && slot.client > 0 && slot.client < (int)server.clientLinks.size()) {
				server.setClientPlayerControl(slot.client, player->id);
				guestPlaced = true;
				if (!slot.name.empty())
					player->setName(slot.name);
				printf("Skirmish: position %zu (player %u): guest %d\n", i + 1, player->id, slot.client);
			}
			else {
				aiPlayers.emplace_back(player);
				const GameObjBlueprint* general = slot.general.empty() ? nullptr
					: server.gameSet->findBlueprint(Tags::GAMEOBJCLASS_PLAYER, slot.general);
				if (!general && !generals.empty())
					general = generals[rand() % generals.size()];
				if (general) {
					player->convertTo(general);
					printf("Skirmish: position %zu (player %u): %s\n", i + 1, player->id, general->name.c_str());
				}
			}
			if (slot.colour > 0)
				player->setColor(slot.colour);
			playing.emplace_back(player, slot.team);
		}
		if (humanPlaced || guestPlaced)
			aiPlayers.clear();

		// Teams: allied within a team, enemies to everyone else (the map's own diplomacy when no
		// teams are chosen)
		const bool anyTeam = std::any_of(playing.begin(), playing.end(), [](const auto& p) { return p.second > 0; });
		const int allied = server.gameSet->diplomaticStatuses.names.getIndex("Allied");
		const int enemy = server.gameSet->diplomaticStatuses.names.getIndex("Enemy");
		if (anyTeam && allied >= 0 && enemy >= 0)
			for (size_t a = 0; a < playing.size(); a++)
				for (size_t b = a + 1; b < playing.size(); b++) {
					const bool sameTeam = playing[a].second > 0 && playing[a].second == playing[b].second;
					server.setDiplomaticStatus(playing[a].first, playing[b].first, sameTeam ? allied : enemy);
					printf("Skirmish: players %u and %u %s\n", playing[a].first->id, playing[b].first->id, sameTeam ? "allied" : "enemies");
				}
	}
	else if (mode != DevMode && isWKBattles) {
		auto* humanPlayer = server.gameSet->findBlueprint(Tags::GAMEOBJCLASS_PLAYER, "Human Player");
		const char* aiPlayerNames[] = {
			"Ignis the Lord Protector Kingmaker",
			"Xaphan the Keeper of the Furnace Kingmaker",
			"Grand Duke Guiscard Kingmaker",
			"Lukius, the Iron Priest Kingmaker",
			"Marjoria, War Queen of the Dawn Kingmaker",
			"Princess Sumarya the Vengeful Kingmaker",
			"Katkerry Gan Kingmaker",
			"Ichthyus Granitas Kingmaker",
		};
		int remainingAiTypes = std::size(aiPlayerNames);
		auto playerList = server.getLevel()->children.at(humanPlayer);
		for (auto& obj : playerList) {
			if (obj->id == 1027)
				continue;
			int aiTypeIndex = rand() % remainingAiTypes;
			const char* aiName = aiPlayerNames[aiTypeIndex];
			std::swap(aiPlayerNames[aiTypeIndex], aiPlayerNames[remainingAiTypes - 1]);
			remainingAiTypes -= 1;
			if (remainingAiTypes == 0) {
				remainingAiTypes = std::size(aiPlayerNames);
			}

			auto* aiPlayer = server.gameSet->findBlueprint(Tags::GAMEOBJCLASS_PLAYER, aiName);
			if (aiPlayer) {
				obj->dyncast<ServerGameObject>()->convertTo(aiPlayer);
			}
		}
	}

	if (mode != DevMode) {
		server.startLevel();
		// Without a human player, "Set AI Difficulty Level" (On Level Start) sets a Conscript or
		// Veteran AI's population limit to a part of the human's, i.e. to 0: that AI then wants no
		// peasants and never trains any. The original's skirmish always has a human; AI-only games
		// (wkbre2's lobby, the headless AI test) give such an AI the skirmish's limit.
		for (const SrvGORef& ref : aiPlayers)
			if (ServerGameObject* player = ref.get())
				if (const int item = server.gameSet->items.names.getIndex("Total Population Count"); item >= 0 && player->getItem(item) <= 0.0f) {
					player->setItem(item, (float)populationLimit);
					printf("Skirmish: AI-only game, player %u gets population limit %d\n", player->id, populationLimit);
				}
	}
}
