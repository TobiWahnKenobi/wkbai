// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#pragma once

#include <cstdint>
#include <list>
#include <map>
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

	std::map<int, int> gtwStates;
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
