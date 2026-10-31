#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <map>

class Server;

class QuickSkirmishMenu
{
public:
	QuickSkirmishMenu() = default;

	void imguiWindow();
	void changeSettingsForLevel(std::string_view levelName);
	void applySettings(Server& server);

	// Who plays at each start position of the map (the level's players in the order of their
	// ids), as chosen in the skirmish menu
	struct Slot {
		enum Type { HUMAN, AI, EMPTY, GUEST } type = AI;
		std::string general; // AI: an AI General (PLAYER blueprint); empty: a random one
		int client = 0;      // GUEST: the server's client index of the multiplayer guest
		std::string name;    // multiplayer: the human's player name (kept in save games)
		int colour = 0;      // player colour index (1 blue ... 8 brown); 0: the map's
		int team = 0;        // 0: no team
	};
	void setSlots(std::vector<Slot> newSlots) { slots = std::move(newSlots); }
	// Kingmaker campaign: the USER object's items from the player's profile (none: a new one)
	bool campaignProfile = false;
	std::map<std::string, float> campaignUserItems;
	// A game setting by name, as the skirmish menu sends them ("startFood", "mode", ...)
	void setSetting(const std::string& name, int value);

private:
	enum GameMode {
		DevMode,
		StandardMode,
		ValhallaMode,
		CampaignMode
	};
	GameMode mode = DevMode;

	// the original's skirmish defaults, as the front end's (FrontEndUI.lua): the headless AI test
	// plays with these, and with 800 population the Kingmaker AI wanted 320 peasants before it would
	// turn to war
	int startFood = 500;
	int startWood = 500;
	int startGold = 200;
	int populationLimit = 200;
	int randomiseResources = 2; // Medium, the front end's default (0: the map's own resources only)
	int loseOnManorDestroyed = 1;
	bool kingPiece = false;
	bool clearFogOfWar = false;
	bool allowDiplomacy = false;
	bool teamReconnaissance = false;
	bool barbarianTribes = false;
	bool aggressiveAI = true; // Server::aggressiveAI
	bool aiGoldExpansion = false; // Server::aiGoldExpansion
	bool aiFocusAttacks = true; // Server::aiFocusAttacks
	int aiEarlyFarms = 0; // Server::aiEarlyFarms
	int aiMinPeasants = 40; // Server::aiMinPeasants
	int aiSurplusBuildings = 3, aiSurplusPerBuilding = 800; // Server::aiSurplusBuildings, aiSurplusPerBuilding

	int resourcePoints = 2500;
	int victoryPointLimit = 10000;
	int valhallaModeFood = 20000;
	int unitLives = 0;
	int capturableFlags = 4;

	int originalCampaignCelestialPath = 0;
	int battlesCampaignImperialTechLevel = 0;
	int battlesCampaignPaganTechLevel = 0;
	int battlesCampaignRenaissanceTechLevel = 0;

	// none chosen: player 1 is the human, the others random AI Generals
	std::vector<Slot> slots;
};