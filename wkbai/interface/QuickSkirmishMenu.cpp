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
			ImGui::Checkbox("Aggressive AI", &aggressiveAI);
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

std::string QuickSkirmishMenu::setupJson(const std::string& level, uint32_t seed) const
{
	nlohmann::json j;
	j["level"] = level;
	j["seed"] = seed;
	nlohmann::json settings = nlohmann::json::array();
	for (const auto& [name, value] : settingLog)
		settings.push_back({ name, value });
	j["settings"] = settings;
	nlohmann::json list = nlohmann::json::array();
	for (const Slot& s : slots)
		list.push_back({ { "type", (int)s.type }, { "general", s.general }, { "client", s.client }, { "name", s.name }, { "colour", s.colour }, { "team", s.team }, { "aiScript", s.aiScript } });
	j["slots"] = list;
	return j.dump(1);
}

bool QuickSkirmishMenu::readSetupJson(const std::string& text, std::string& level, uint32_t& seed,
	std::vector<std::pair<std::string, int>>& settings, std::vector<Slot>& slots)
{
	const nlohmann::json j = nlohmann::json::parse(text, nullptr, false);
	if (j.is_discarded() || !j.is_object())
		return false;
	level = j.value("level", std::string());
	seed = j.value("seed", 0u);
	settings.clear();
	for (const auto& s : j.value("settings", nlohmann::json::array()))
		if (s.is_array() && s.size() == 2)
			settings.emplace_back(s[0].get<std::string>(), s[1].get<int>());
	slots.clear();
	for (const auto& s : j.value("slots", nlohmann::json::array())) {
		Slot slot;
		slot.type = (Slot::Type)s.value("type", 1);
		slot.general = s.value("general", std::string());
		slot.aiScript = s.value("aiScript", std::string());
		slot.client = s.value("client", 0);
		slot.name = s.value("name", std::string());
		slot.colour = s.value("colour", 0);
		slot.team = s.value("team", 0);
		slots.push_back(std::move(slot));
	}
	return !level.empty();
}

void QuickSkirmishMenu::setSetting(const std::string& name, int value)
{
	settingLog.emplace_back(name, value);
	if (environment.apply(name, (float)value))
		return;
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
	else if (name == "aggressiveAI") aggressiveAI = value != 0;
	else if (name == "cheatMode") cheatMode = value != 0;
	else if (name == "aiGoldExpansion") aiGoldExpansion = value != 0;
	else if (name == "aiFocusAttacks") aiFocusAttacks = value != 0;
	else if (name == "aiEarlyFarms") aiEarlyFarms = value;
	else if (name == "aiFirstBarracks") aiFirstBarracks = value;
	else if (name == "aiAdapt") aiAdapt = value != 0;
	else if (name == "recordGame") recordGame = value != 0;
	else if (name == "aiTakeOver") aiTakeOver = value != 0;
	else if (name == "aiAdvanced") aiAdvanced = std::clamp(value, 0, 3);
	else if (name == "aiLifeline") aiLifeline = value != 0;
	else if (name == "aiHomeGuard") aiHomeGuard = value != 0;
	else if (name == "aiWar") aiWar = value != 0;
	else if (name == "aiWeather") aiWeather = std::clamp(value, 0, 3);
	else if (name == "aiHumanFocus") aiHumanFocus = std::clamp(value, 10, 100);
	else if (name == "modPolicy") modPolicy = std::clamp(value, 0, 3);
	else if (name == "allowSpectators") allowSpectators = value != 0;
	else if (name == "maxSpectators") maxSpectators = std::max(0, value);
	else if (name == "aiMinPeasants") aiMinPeasants = value;
	else if (name == "aiSurplusBuildings") aiSurplusBuildings = value;
	else if (name == "aiSurplusPerBuilding") aiSurplusPerBuilding = (float)value;
	else if (name == "aiLendFarmers") aiLendFarmers = value != 0;
	else if (name == "aiRetreatHealth") aiRetreatHealth = value;
	else if (name == "aiAttackWave") aiAttackWave = value;
	else if (name == "aiWaveGather") aiWaveGather = value != 0;
	else if (name == "aiWaveSiege") aiWaveSiege = value != 0;
	else if (name == "aiWaveFormations") aiWaveFormations = value != 0;
	else if (name == "aiFocusFire") aiFocusFire = value != 0;
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

	// Battles Campaign level (a Kingmaker province, CH2_<Province>.lvl and its _Multi variant):
	// Campaign mode only when it is played from the campaign map. The original plays the same
	// levels as skirmish and multiplayer maps (WKB.exe unlocks a conquered province's map for
	// them, 0x6f0d20), and then the level item "Skirmish Campaign Level" stays 0, so the
	// province script's campaign parts (the intro, the campaign resources, the conquest
	// ending) don't run. In Campaign mode wkbai sets it to 1.
	if (StrCICompare(levelName.substr(0, 4), "CH2_") == 0 && campaignProfile) {
		mode = CampaignMode;
		return;
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

		// no guest keeps the player the level gave it by its order (one with no position would
		// control an AI's player): the slots say who plays where
		for (size_t i = 1; i < server.clientPlayerObjects.size(); i++)
			server.setClientPlayerControl((int)i, 0);
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
			else if (slot.type == Slot::GUEST && slot.client > 0 && (replaySetup || slot.client < (int)server.clientLinks.size())) {
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
				// the one chosen, for a recording's setup
				if (general && i < slots.size())
					slots[i].general = general->name;
				if (i < slots.size() && !slots[i].aiScript.empty())
					server.aiScripts[player->id] = slots[i].aiScript;
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
		// Diplomacy as WKB.exe's lobby sets it (0x7a5f54): with Allow Diplomacy and no teams the
		// players may change their relations ("Disallow Diplomacy" 0, everyone's "Diplomacy
		// Enabled" 1); otherwise not, and with teams "Teams Selected" (tributes between team mates)
		if (isWKBattles) {
			const bool open = allowDiplomacy && !anyTeam;
			setItemByName(level, "Disallow Diplomacy", open ? 0.0f : 1.0f);
			setItemByName(level, "Teams Selected", anyTeam ? 1.0f : 0.0f);
			for (const auto& [player, team] : playing)
				setItemByName(player, "Diplomacy Enabled", open ? 1.0f : 0.0f);
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

	// Team Reconnaissance: every player starts with the Shared Vision research done ("Reconnaissance
	// Upgrade Status" 2, as Reconnaissance.cpp's research sets it), so allies see what each other
	// sees (FogOfWar); the item is the players', kept by the save game
	if (teamReconnaissance && isWKBattles && !campaignProfile)
		for (auto& [objType, objList] : level->children)
			for (auto* comObj : objList)
				if (ServerGameObject* obj = comObj->dyncast<ServerGameObject>(); obj->blueprint->bpClass == Tags::GAMEOBJCLASS_PLAYER)
					setItemByName(obj, "Reconnaissance Upgrade Status", 2.0f);
	// the game's settings: a save game keeps those it was saved with (Server::loadSaveGame,
	// <save>.ai.txt; the play test: a loaded game lost its cheat mode, so F9 stopped working)
	if (!server.settingsFromSave) {
		server.clearFogOfWar = clearFogOfWar;
		server.teamReconnaissance = teamReconnaissance;
		server.aggressiveAI = aggressiveAI;
		// cheat mode (the F9 panel): a skirmish against the AI only, not with guests, not the campaign
		server.cheatMode = cheatMode && !campaignProfile
			&& std::none_of(slots.begin(), slots.end(), [](const Slot& s) { return s.type == Slot::GUEST; });
		server.aiGoldExpansion = aiGoldExpansion;
		server.aiFocusAttacks = aiFocusAttacks;
		server.aiEarlyFarms = aiEarlyFarms;
		server.aiFirstBarracks = aiFirstBarracks;
		server.aiAdapt = aiAdapt;
		server.aiTakeOver = aiTakeOver;
		server.aiAdvanced = aiAdvanced;
		server.aiLifeline = aiLifeline;
		server.aiHomeGuard = aiHomeGuard;
		server.aiWar = aiWar;
		server.aiWeather = aiWeather;
		server.aiHumanFocus = aiHumanFocus;
		server.modPolicy = modPolicy;
		server.allowSpectators = allowSpectators;
		server.maxSpectators = maxSpectators;
		server.aiMinPeasants = aiMinPeasants;
		server.aiSurplusBuildings = aiSurplusBuildings;
		server.aiSurplusPerBuilding = (float)aiSurplusPerBuilding;
		server.aiLendFarmers = aiLendFarmers;
		server.aiRetreatHealth = aiRetreatHealth / 100.0f;
		server.aiAttackWave = aiAttackWave;
		server.aiWaveGather = aiWaveGather;
		server.aiWaveSiege = aiWaveSiege;
		server.aiWaveFormations = aiWaveFormations;
		server.aiFocusFire = aiFocusFire;
	}
	else if (server.clientLinks.size() > 1)
		server.cheatMode = false; // (never with guests)
	if (mode != DevMode) {
		// a new game: the day and night, seasons and weather as set (a save game keeps its own)
		server.environment.reset();
		// (none in the campaign: its levels play as the original's, whatever the skirmish set)
		server.environment.settings = campaignProfile ? EnvironmentSettings() : environment;
		// the natural moon starts at the phase it had on the first day of the starting season in
		// the year 1000 (unless the settings gave one), saved with the game
		if (server.environment.settings.moonStart < 0.0f)
			server.environment.settings.moonStart = HistoricMoonAge(server.environment.settings.startSeason);
		server.environment.trace = g_settings.value("environmentTrace", false);
		if (const nlohmann::json force = g_settings.value("environmentForce", nlohmann::json()); force.is_object()) {
			server.environment.forceWeather = force.value("weather", -1);
			server.environment.forceWaterRise = force.value("waterRise", -1000.0f);
			if (force.contains("fireAt") && force["fireAt"].is_array() && force["fireAt"].size() == 3) {
				server.environment.forceFireX = force["fireAt"][0].get<float>();
				server.environment.forceFireZ = force["fireAt"][1].get<float>();
				server.environment.forceFireAt = force["fireAt"][2].get<float>();
			}
			if (force.contains("lightningAt") && force["lightningAt"].is_array() && force["lightningAt"].size() == 3) {
				server.environment.forceLightningX = force["lightningAt"][0].get<float>();
				server.environment.forceLightningZ = force["lightningAt"][1].get<float>();
				server.environment.forceLightningAt = force["lightningAt"][2].get<float>();
			}
		}
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
