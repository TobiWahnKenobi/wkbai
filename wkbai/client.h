// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#pragma once

#include <cstdint>
#include <list>
#include <map>
#include <deque>
#include <vector>
#include <map>
#include <set>
#include "common.h"
#include "util/vecmat.h"
#include "Camera.h"
#include "scene.h"
#include <cstdarg>
#include "TimeManager.h"
#include "GameObjectRef.h"

struct GameSet;
struct GSFileParser;
struct GameObjBlueprint;
struct Server;
struct NetLink;
struct Command;
struct Terrain;
struct ClientInterface;
struct Client;
struct CameraPath;

struct ClientGameObject : SpecificGameObject<Client, ClientGameObject> {
	using Program = Client;
	ClientGameObject(uint32_t id, const GameObjBlueprint *blueprint) : SpecificGameObject<Client, ClientGameObject>(id, blueprint) {}
	SceneEntity sceneEntity;
	std::unordered_map<int, int> buildingOrderCountMap;

	void updatePosition(const Vector3& newposition);
	void updateOccupiedTiles(const Vector3& oldposition, const Vector3& oldorientation, const Vector3& newposition, const Vector3& neworientation);
};

struct Client : SpecificGameState<ClientGameObject, ProgramType::CLIENT>
{
	using GameObject = ClientGameObject;
	static Client *instance;

	//GameSet *gameSet;

	TimeManager timeManager;

	//ClientGameObject *level;
	//std::map<uint32_t, ClientGameObject*> idmap;

	std::vector<std::string> chatMessages;
	// Multiplayer guests: the lobby as the host shows it (NETCLIMSG_LOBBY_STATE), and its parts so far
	std::string lobbyState, lobbyStateParts;
	// Agent control: the players an agent plays, by name (NETCLIMSG_AGENT_PLAYERS); and what this
	// client told its server last (NETSRVMSG_AGENT_STATE; -1: not yet, on this link)
	std::vector<std::string> agentPlayers;
	int agentStateSent = -1;
	NetLink* agentStateLink = nullptr;
	// a running game's places for this guest (NETCLIMSG_SEATS), as lobbyState; awaitingSeat until
	// it takes one or watches
	std::string seats, seatsParts;
	bool awaitingSeat = false;
	// it watches the game (NETCLIMSG_SPECTATOR)
	bool spectator = false;
	// the host turned it away (NETCLIMSG_JOIN_REFUSED): why
	std::string refusedReason;
	// the host's map regions (NETCLIMSG_HOST_DATA), counted up with each that arrives; the parts
	// of the ones arriving
	std::string hostRegions;
	int hostRegionsSerial = 0;
	// wkbai's day and night, seasons and weather, as the server last sent them (NETCLIMSG_ENVIRONMENT);
	// environmentActive false: none (the map's own sun, no weather)
	bool environmentActive = false;
	bool environmentSkyKnown = false; // the game's sky (environmentSettings.skyWeather, skyCover) came
	EnvironmentSettings environmentSettings;
	EnvironmentState environment;
	int environmentWeatherHeld = -1; // the debug panel's weather held on the server (-1: none)
	float environmentWeatherEnds = 0.0f; // the game time the weather changes next (1e30: never)
	// the lobby's Clear Fog Of War (Server::clearFogOfWar, with NETCLIMSG_ENVIRONMENT): on, no fog
	// of war (everything on the map shown); off until the server says
	bool clearFogOfWar = false;
	// the players whose game is over (NETCLIMSG_PLAYER_FINISHED, Server::finishedPlayers): 1 won,
	// 2 lost, by id. The client's own in it: no fog of war, every player's statistics.
	std::map<uint32_t, uint8_t> finishedPlayers;
	// the client's player's outcome: 0 still playing, 1 won, 2 lost
	int ownOutcome() const;
	// wkbai's unit history (NETCLIMSG_UNIT_HISTORY): the tracked units' names, births, kinds
	// (UnitHistory::Kind) and flags (UnitHistory::Flags), by id; the kills to become a hero; the
	// heroes made since the interface last looked (their ids)
	struct UnitInfo { std::string name, birthName; float birth = 0.0f; uint8_t kind = 0, flags = 0; };
	std::unordered_map<uint32_t, UnitInfo> unitInfo;
	int heroThreshold = 0;
	std::vector<uint32_t> newHeroes;
	// the server's timeline lines (NETCLIMSG_CHRONICLE), for the interface's GameChronicle, and
	// the parts of the one arriving
	std::vector<std::string> chronicleIn;
	std::string chronicleParts;
	// the trees and bushes a wildfire burned (their ids): drawn charred, bare of leaves
	std::set<uint32_t> charredTrees;
	std::map<int, std::string> hostDataParts;
	// the host's mods for this game (NETCLIMSG_HOST_DATA 3): the session to mount, JSON; the
	// folder its sent files went to
	std::string sessionMods;
	bool sessionModsReady = false;
	// this computer's mods (ModManifest), to the host before the hello
	void sendModManifest();
	// The loading screen's rows (NETCLIMSG_LOADING_STAGES): stage, colour index, this client's own
	struct LoadingRow { int stage, colour; bool own; std::string name; }; // (stage: wkbai's loading percentage, 0-100)
	std::vector<LoadingRow> loadingRows;
	// the host waits for the players' loads (the game's clock stands): the loading screen stays
	bool loadingWaitForOthers = false;
	// The host's AI General files being received (NETCLIMSG_GAME_SET_FILE)
	std::map<std::string, std::string> hostFiles;
	std::set<std::string> hostFilesComplete;

	NetLink *serverLink;

	Camera camera;
	Vector3 storedCameraPosition, storedCameraOrientation;
	int cameraMode = 0;
	const CameraPath* cameraCurrentPath = nullptr;
	int cameraPathIndex = -1;
	std::vector<std::pair<Vector3, Vector3>> cameraPathPos;
	std::vector<float> cameraPathDur;
	uint32_t cameraStartTime;

	ClientInterface *cliInterface;

	bool localServer;
	CliGORef clientPlayer;

	// The game text windows showing: window index -> its page (-1: closed by its button);
	// gtwOrder: when each was shown (a serial number, the newest on top)
	std::map<int, int> gtwStates;
	std::map<int, uint32_t> gtwOrder;
	uint32_t gtwSerial = 0;
	// The player's mission objectives (NETCLIMSG_MISSION_OBJECTIVES_*): the active set (-1: none),
	// its entries' keys and states (MissionObjectives::State), a counter of the entries shown
	std::vector<std::pair<std::string, uint8_t>> objectiveEntries;
	int missionObjectives = -1;
	uint32_t objectivesChanged = 0;
	// Diplomacy (ServerDiplomacy.cpp): the offers between players ((from, to) -> status), the top
	// bar's Diplomacy, Global Diplomacy and Tributes buttons, the AI Generals' messages to show
	// one after another, and the notices (Language keys, with the player in whose colour)
	std::map<std::pair<uint32_t, uint32_t>, int> diplomaticOffers;
	// Disguises (NETCLIMSG_DISGUISE, _UNMASK): by object id, the look the player's enemies see,
	// unless unmasked for this client's player; disguiseFor: the one this client sees (none for
	// its own and its allies' objects)
	struct Disguise {
		const GameObjBlueprint* blueprint = nullptr;
		int subtype = 0, appearance = 0;
		bool unmasked = false;
	};
	std::map<uint32_t, Disguise> disguises;
	const Disguise* disguiseFor(CommonGameObject* obj);
	// REVEAL_FOG_OF_WAR for this client's player (NETCLIMSG_FOG_REVEAL): the object seen around
	// it until the game time `until` (< 0: for good), radius in 5 m tiles
	struct FogReveal { uint32_t object; float until, radius; };
	std::vector<FogReveal> fogReveals;
	// the minimap alerts going (NETCLIMSG_MINIMAP_ALERT): a blinking dot or collapsing circles at
	// (x, z), colour 0xRRGGBB or 0xFF0000nn (player colour nn), from SDL tick `start`
	struct MinimapAlert {
		bool circle = false;
		float x = 0, z = 0;
		uint32_t colour = 0xFFFFFF;
		float seconds = 1;
		int circles = 1;
		uint32_t start = 0;
	};
	std::vector<MinimapAlert> minimapAlerts;
	// the last four warnings' places (x, z), the latest first (one per place: a new alert within
	// 25 m of a kept one moves it to the front), for Home; warningCycle: Home's next pick
	std::vector<std::pair<float, float>> warningPlaces;
	// DISABLE_GAME_INTERFACE: no HUD and no orders for this player (NETCLIMSG_GAME_INTERFACE)
	bool gameInterfaceEnabled = true;
	// UNLOCK_LEVEL this session (also noted in saved\Unlocked Levels.txt)
	std::set<std::string> unlockedLevels;
	// the mission objectives entries' position markers, by entry key (kind 1: a place, 2: an
	// object, followed)
	struct ObjectiveMarker { int kind = 0; float x = 0, z = 0; uint32_t object = 0; };
	std::map<std::string, ObjectiveMarker> objectiveMarkers;
	size_t warningCycle = 0;
	struct DiplomacyButton {
		bool enabled = false, set = false; // set: by ENABLE_/DISABLE_..._WINDOW (else the default)
		std::string tooltip;
		bool news = false;                  // flashes until its window is opened
	};
	DiplomacyButton diplomacyButtons[3];
	struct ChatDisplay {
		uint32_t from = 0;
		std::string key;
		std::vector<float> values;
	};
	std::deque<ChatDisplay> chatDisplays;
	struct Notice {
		std::string key;
		uint32_t player = 0;
		uint32_t time = 0; // SDL ticks
	};
	std::vector<Notice> notices;
	void addNotice(const std::string& key, uint32_t player);
	void sendDiplomaticStatus(uint32_t player, int status);
	void sendDiplomaticOffer(uint32_t player, int status);
	void sendTribute(uint32_t player, int item, float amount);
	void sendPatrol(CommonGameObject* obj, bool more, const Vector3& point);
	// Sets a showing window's page and plays that page's ACTIVATION_SOUND
	void setGameTextWindowPage(int gtwIndex, int page);
	bool isMusicPlaying = false;

	struct SpecialEffect {
		SceneEntity entity;
		CliGORef attachedObj;
		float startTime;
	};
	std::list<SpecialEffect> specialEffects;
	std::multimap<std::pair<CliGORef, int>, SceneEntity> loopingSpecialEffects;

	int dbgNumMessagesPerTick = 0, dbgNumMessagesInCurrentSec = 0, dbgNumMessagesPerSec = 0;
	uint32_t dbgLastMessageCountTime = 0;

	//void loadSaveGame(const char *filename);
	//ClientGameObject *createObject(GameObjBlueprint *blueprint, uint32_t id = 0);

	Client(bool localServer = false) : 
		serverLink(nullptr), cliInterface(nullptr), localServer(localServer) {instance = this;}

	//ClientGameObject* findObject(uint32_t id) { auto it = idmap.find(id); if (it != idmap.end()) return it->second; else return nullptr; }

	void loadFromServerObject(Server &server);

	void tick();

	void sendMessage(const std::string &msg);
	void sendLobbyHello(const std::string& name, const std::string& password = std::string());
	// a running game: the place to take (a player id from seats), 0: watch
	void sendTakeSeat(uint32_t playerId);
	void sendLobbyRequest(const std::string& request);
	// faceTo: where to face at the end (a formation's right-drag; x < 0: nowhere)
	void sendCommand(ClientGameObject *obj, const Command *cmd, int assignmentMode, ClientGameObject *target = nullptr, const Vector3 & destination = Vector3(), const Vector3& faceTo = Vector3(-1,0,0));
	void sendPauseRequest(uint8_t pauseState);
	void sendLoadingStage(int stage);
	void readLoadingRows(struct BinaryReader& br);
	void peekLoadingRows();
	void sendStampdown(const GameObjBlueprint *blueprint, ClientGameObject *player, const Vector3 &position, bool sendEvent = false, bool inGameplay = false,
		const Vector3 &orientation = Vector3(0, 0, 0));
	void sendStartLevelRequest();
	void sendGameTextWindowButtonClicked(int gtwIndex, int buttonIndex);
	void sendCameraPathEnded(int camPathIndex);
	void sendGameSpeedChange(float nextSpeed);
	void sendTerminateObject(ClientGameObject* obj);
	void sendBuildLastStampdownedObject(ClientGameObject* obj, int assignmentMode);
	void sendCancelCommand(ClientGameObject* obj, const Command* cmd);
	// the environment's debug panel (F9): "name" = value (ServerEnvironment::debugSet)
	void sendEnvironmentDebug(const std::string& name, float value);
	void sendPutUnitIntoNewFormation(ClientGameObject* obj);
	void sendCreateNewFormation();
	void sendSetBuildingSpawnedUnitOrderToTarget(ClientGameObject* obj, int commandIndex, ClientGameObject* target);
	void sendSetBuildingSpawnedUnitOrderToDestination(ClientGameObject* obj, int commandIndex, Vector3 destination, Vector3 faceTo = Vector3{ -1.0f, 0.0f, 0.0f });

	void attachInterface(ClientInterface *cliIface) { cliInterface = cliIface; }

private:
	//ClientGameObject * loadObject(GSFileParser & gsf, const std::string & clsname);
	ClientGameObject *createObject(const GameObjBlueprint *blueprint, uint32_t id = 0);
	void info(const char *fmt, ...) {
		va_list args;
		va_start(args, fmt);
		//vprintf(fmt, args);
		va_end(args);
	}
};
