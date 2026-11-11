// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#pragma once
#include <atomic>
#include <filesystem>
#include <memory>

#include <cstdint>
#include <map>
#include <vector>
#include "common.h"
#include "TimeManager.h"
#include "Order.h"
#include "GameObjectRef.h"
#include <unordered_set>
#include <unordered_map>
#include <set>
#include <atomic>
#include <string>
#include "MovementController.h"
#include "AIController.h"
#include "FormationController.h"
#include "ServerEnvironment.h"
#include "UnitHistory.h"

struct GameSet;
struct GSFileParser;
struct GameObjBlueprint;
struct NetLink;
struct NetPacket;
struct NetPacketWriter;
struct Terrain;
struct ActionSequence;
struct Server;
struct Reaction;

struct ServerGameObject : SpecificGameObject<Server, ServerGameObject> {
	using Program = Server;

	bool deleted = false; ServerGameObject* nextDeleted = nullptr;

	OrderConfiguration orderConfig;
	std::unordered_set<const Reaction*> individualReactions;
	// ordered by object id (see CommonGameState::aliases)
	std::map<int, std::set<SrvGORef>> associates, associators;
	std::vector<SrvGORef> referencers;
	std::vector<uint32_t> seenObjects; // ids of the objects in sight range, sorted
	// a fingerprint of seenObjects (two sums of mixed ids): the same objects in range again are
	// seen as such without sorting them (lookForSightRangeEvents)
	uint64_t seenHashA = 0, seenHashB = 0;
	int sightBlock = -1; // its block in Server::sightBlocks, if it generates sight range events
	int sightSlot = -1; // its place in that block's list
	// loaded from a save: the first sight update only notes what is in range, without events
	// (WKB.exe's saves don't store what each object sees, and its units don't react to the
	// objects already around them after loading; wkbre2's did: on the Marjoria's Keep save the
	// human archers newly "saw" everyone nearby and formed an army against a spy)
	bool sightFromSave = false;
	std::set<std::pair<int, int>> zoneTiles;
	int clientIndex = -1;
	bool isMusicPlaying = false;
	float currentSpeed = 0.0f;
	MovementController movementController{ this };
	AIController aiController{ this };
	FormationController formationController{ this };
	Vector3 playerStartCameraPosition, playerStartCameraOrientation;
	// wkbai's unit history (UnitHistory; kills and buildingsDestroyed are CommonGameObject's):
	// who made it (OBJECT_CREATION's creator; a missile: who fired it), the troop credited with a
	// garrisoned building's missile, the unit that sent the last attack package and when, and a
	// garrisoned building's last troop credited (its shooters take turns)
	uint32_t creatorId = 0, creditId = 0, lastHitBy = 0, garrisonCredit = 0;
	float lastHitTime = -1.0f;

	ServerGameObject(uint32_t id, const GameObjBlueprint *blueprint) : SpecificGameObject<Server, ServerGameObject>(id, blueprint), orderConfig(this) {}

	void setItem(int index, float value);
	void setParent(ServerGameObject *newParent);
	void setPosition(const Vector3 &position);
	void setOrientation(const Vector3 &orientation);
	void setSubtypeAndAppearance(int new_subtype, int new_appearance);
	void setColor(int color);
	// startTime: when the animation began (< 0: now)
	void setAnimation(int animationIndex, bool isClamped = false, int synchronizedTask = -1, float startTime = -1.0f, float speed = 1.0f);
	void startMovement(const Vector3 &destination);
	void stopMovement();
	// the walk animation of the last movement, to go on with it when the next movement follows
	// at once (a formation member reaching its moving place stops and starts again)
	int lastWalkAnim = -1;
	float lastWalkAnimStart = 0.0f, lastWalkStop = -1.0f;
	void sendEvent(int evt, ServerGameObject *sender = nullptr);
	void associateObject(int category, ServerGameObject *associated);
	void dissociateObject(int category, ServerGameObject *associated);
	void clearAssociates(int category);
	void convertTo(const GameObjBlueprint *postbp);
	// removes what can be built upon (CAN_BE_BUILT_UPON: tree stumps) from the footprint's tiles
	void removeBuiltUponUnderFootprint(bool propsOnly = false);
	void setScale(const Vector3& scale);
	void terminate();
	void destroy();
	void updateFlags(int value);
	void disable();
	void enable();
	void setIndexedItem(int item, int index, float value);
	void startTrajectory(const Vector3& initPos, const Vector3& initVel, float startTime);
	void playSoundAtObject(int soundTag, ServerGameObject* target);
	void playSoundAtPosition(int soundTag, const Vector3& target);
	void playSpecialEffectAt(int sfxTag, const Vector3& position);
	void playSpecialEffectBetween(int sfxTag, const Vector3& position1, const Vector3& position2);
	void playAttachedSpecialEffect(int sfxTag, ServerGameObject* target);
	void setName(const std::string& name);
	void reportCurrentOrder(const OrderBlueprint* orderBp);
	void attachLoopingSpecialEffect(int sfxTag, const Vector3& position);
	void detachLoopingSpecialEffect(int sfxTag);
	void updateBuildingOrderCount(const OrderBlueprint* orderBp);
	void addCityRectangle(const CityRectangle& rectangle);
	void setBuildingSpawnedUnitOrderToTarget(int orderIndex, ServerGameObject* target);
	void setBuildingSpawnedUnitOrderToDestination(int orderIndex, Vector3 destination, Vector3 positionToFace = Vector3{ -1.0f, 0.0f, 0.0f });
	// the client message of the building's order for its spawned units (its owner's)
	void sendSpawnedUnitOrder(NetLink* link = nullptr) const; // to the owner, or to that link
	// where the spawned units' target was last (a rally point on a tree): when it is gone (felled),
	// the next spawned unit takes the nearest object there that the command accepts
	// (x < 0: none)
	Vector3 spawnedUnitTargetPlace = Vector3(-1.0f, 0.0f, 0.0f);

	void updatePosition(const Vector3 &newposition, bool events = false);
	void lookForSightRangeEvents();
	void addZoneTile(int tx, int tz);
	void removeZoneTile(int tx, int tz);
	//void removeZoneTile(int tx, int tz);
	void updateSightRange();
	void updateOccupiedTiles(const Vector3& oldposition, const Vector3& oldorientation, const Vector3& newposition, const Vector3& neworientation);
	void removeIfNotReferenced();
	float computeSpeed();
	void notifySubordinateRemoved();

	bool canAffordObject(const GameObjBlueprint* blueprint);
	void payObjectCost(const GameObjBlueprint* blueprint);
	void reclaimObjectCost(const GameObjBlueprint* blueprint);
};

struct Server : SpecificGameState<ServerGameObject, ProgramType::SERVER>
{
	using GameObject = ServerGameObject;
	static Server *instance;

	//GameSet *gameSet;

	uint32_t randomSeed;
	TimeManager timeManager;
	uint32_t nextUniqueId;

	//ServerGameObject *level;
	//std::map<uint32_t, ServerGameObject*> idmap;

	struct DelayedSequence {
		const ActionSequence* actionSequence;
		SrvGORef executor;
		std::vector<SrvGORef> selfs;
	};
	struct OverPeriodSequence {
		const ActionSequence* actionSequence;
		SrvGORef executor;
		float startTime, period;
		int numTotalExecutions, numExecutionsDone;
		std::vector<SrvGORef> remainingObjects;
	};
	std::multimap<game_time_t, DelayedSequence> delayedSequences;
	std::vector<OverPeriodSequence> overPeriodSequences;
	std::vector<OverPeriodSequence> repeatOverPeriodSequences;

	std::vector<std::string> chatMessages;

	std::vector<NetLink*> clientLinks;
	std::vector<SrvGORef> clientPlayerObjects;

	struct ClientInfo {
		std::string name; // the player name a guest gave in the lobby
		std::vector<std::string> lobbyRequests; // NETSRVMSG_LOBBY_REQUEST, for the lobby to handle
		SrvGORef lastStampdown;
		std::vector<SrvGORef> unitsForNewFormation;
		int loadingStage = 0; // NETSRVMSG_LOADING_STAGE, 0: none reported
		bool spectator = false; // it watches (NETCLIMSG_SPECTATOR): its orders are ignored
		bool refused = false; // turned away (NETCLIMSG_JOIN_REFUSED): the host disconnects it
		bool joinedRunning = false; // it joined the running game: sent its state and the places
		bool awaitingState = false; // it joined the running game: no broadcasts before its state
		std::string modManifest, modManifestParts; // its mods (NETSRVMSG_MOD_MANIFEST)
		// the build check: the challenge sent, the answer (NETSRVMSG_BUILD_ANSWER), a hello held
		// until the answer came (buildPolicy IDENTICAL)
		std::string buildChallenge, buildAnswer, pendingHello;
		bool buildAnswered = false;
		bool agent = false; // agent control on at this client (NETSRVMSG_AGENT_STATE)
	};
	std::vector<ClientInfo> clientInfos;

	time_t lastSync;

	std::vector<std::tuple<ServerGameObject*, int, int>> postAssociations;
	// task targets read from a save game, set once all objects are loaded (a target can come later in the file)
	std::vector<std::pair<Task*, int>> postTaskTargets;
	// a save's AI work orders, registered in their order once all objects are loaded (their
	// cities come after the players): player, city id, unit finder, work order
	std::vector<std::tuple<ServerGameObject*, uint32_t, ObjectFinder*, const WorkOrder*>> postWorkOrders;

	ServerGameObject* objToDelete = nullptr, * objToDeleteLast = nullptr;

	Server();

	void loadSaveGame(const char *filename);
	// Writes the game to a save game file (a file system path, its folders are created) that
	// loadSaveGame reads back (ServerSave.cpp)
	bool saveGame(const char* filename);
	// what the game was loaded from, for saving
	std::string gameSetFile, mapFile;
	// GAME_TYPE: 4 in every shipped level, 1 in the original's skirmish saves (its Load Game
	// lists a file by it). Saving writes it; a game started from a level is saved as 1, and the
	// map editor saves levels as 4
	int gameType = 4;
	bool savingLevel = false;
	// the LANDSCAPE object's id (the original's level holds its MAP in one); 0: none read, a new
	// id on saving
	uint32_t landscapeId = 0;
	// NUM_HUMAN_PLAYERS as read: a level saved by the map editor lists them again
	std::vector<uint32_t> humanPlayerIds;
	// The Kingmaker campaign: CONQUER_LEVEL (the province is won, the game ends) and
	// UPDATE_USER_PROFILE (the USER object's items, kept in the campaign profile)
	std::atomic<bool> levelConquered = false;
	// EXIT_TO_MAIN_MENU and BOOT_LEVEL "<level>" (the game endings, the tutorials' next lesson):
	// the game ends; bootLevel is set before the flag
	std::string bootLevel;
	std::atomic<bool> exitToMenu = false, levelBooted = false;
	bool userProfileUpdated = false;
	// the aggressive AI (InstallAIAggressionOverrides): the skirmish setting "aggressiveAI"
	bool aggressiveAI = true;
	// the skirmish setting "cheatMode": the environment's debug panel (F9) works only with it
	// (recorded, so a replay applies the panel's changes too)
	bool cheatMode = false;
	// loadSaveGame read the game's settings from the save game (<save>.ai.txt): the lobby's
	// applySettings then keeps them (all the game's settings go with its save game)
	bool settingsFromSave = false;
	// the lobby's "clearFogOfWar" (the original's Clear Fog Of War: on, everything on the map is
	// shown; off, the default, the fog of war: FogOfWar, the clients told with
	// NETCLIMSG_ENVIRONMENT; the campaign always has it) and "teamReconnaissance" (allies share their
	// sight: applied as the players' Reconnaissance Upgrade Status), kept with the game settings
	// (kept outside the struct: a member here moved the layout that a still unfound memory bug
	// depends on, docs/engine/playtest-2026-11-07.md)
	static bool clearFogOfWar, teamReconnaissance;
	// with it, AIs found villages at gold (ValueGoldExpansion): the setting "aiGoldExpansion", off
	// (25-minute tournament: 341 soldiers raised with it, 358 without; more gold, much less wood)
	bool aiGoldExpansion = false;
	// with it, an AI's armies attack the nearest enemy (FinderFocusedEnemyPalaces): "aiFocusAttacks"
	bool aiFocusAttacks = true;
	// with it, an AI places its first this many farms before its fighter buildings (AIController):
	// "aiEarlyFarms"; 0: fighter buildings first, as before
	int aiEarlyFarms = 0;
	// with it, an AI with no building that trains fighters this many game seconds in lays down a
	// barracks itself (AIController): "aiFirstBarracks"; 0: off
	int aiFirstBarracks = 90;
	// with it, an AI keeps a model of its enemies (AIController): the soldiers it meets, by type,
	// steer its armies' make-up towards counters, and the enemy who attacked its base is the
	// one its armies go for: "aiAdapt"
	bool aiAdapt = true;
	// with it, an AI wants at least this many peasants, at war too (ValuePeasantFloor): "aiMinPeasants"
	int aiMinPeasants = 40;
	// with it, a fighter building is wanted once more per aiSurplusPerBuilding of food and wood
	// in stock, up to aiSurplusBuildings more (AIController): "aiSurplusBuildings", "aiSurplusPerBuilding"
	int aiSurplusBuildings = 3;
	float aiSurplusPerBuilding = 800.0f;
	// with it, a settlement whose farms have no peasants borrows them (AIController): "aiLendFarmers"
	bool aiLendFarmers = true;
	// with it, attackers below this share of their hit points go home to the defensive army
	// (AIController): "aiRetreatHealth" in percent; 0: they fight on
	float aiRetreatHealth = 0.0f;
	// with it, a defensive army of more than this many units sends all but 10 to attack
	// (AIController): "aiAttackWave"; 0: off
	int aiAttackWave = 50;
	// with it, a wave gathers 120 m short of its target before it attacks: "aiWaveGather";
	// off: waves stream in (they razed more buildings in the AI tests)
	bool aiWaveGather = false;
	// with it, waves take every siege engine along, never keeping one home: "aiWaveSiege";
	// off: with it on the waves razed fewer buildings (the scripts' sapper armies lose them)
	bool aiWaveSiege = false;
	// with it, a wave's unit types of 4 or more march in Column, form a Line (heavy cavalry:
	// a Wedge) 50 m short of the target and charge from there, as the original's Siege Armies:
	// "aiWaveFormations"
	bool aiWaveFormations = true;
	// with it, the ranged units of each attacking army shoot at the weakest enemy near it together:
	// "aiFocusFire"
	bool aiFocusFire = true;
	// the advanced AI (AIAdvanced.cpp): unit power, defence of every settlement, attacks chosen by
	// strength: "aiAdvanced" 1 for every AI, 2 or 3 for every other one (comparisons), 0 off
	int aiAdvanced = 0;
	// with it, an AI left with fewer than 4 peasants and under 50 food (a peasant's cost) is given
	// what is short of 50, at most every 30 s (AIController): "aiLifeline"
	bool aiLifeline = true;
	// with it, an AI keeps a home guard growing to 10 soldiers from minute 2 (AIController::homeGuard)
	bool aiHomeGuard = true;
	// the war layer (AIAdvanced.cpp, AIController::warUpdate), in place of the fixed waves above:
	// soldiers (not peasants) meet enemies at every settlement and the palace, waves sized to the
	// AI's population limit also take the scripts' idle attackers, waves fight what defends their
	// target and go on to the next building, and the attacker is attacked back: "aiWar"
	bool aiWar = true;
	// with it, a human enemy counts as this much (percent) of its distance when an AI picks whom
	// its armies attack (FinderFocusedEnemyPalaces): "aiHumanFocus"; 100: by distance alone
	int aiHumanFocus = 50;
	// with the day and night, seasons and weather on, the AIs answer them (AIWeather.cpp): shelter
	// from the cold and heat, off the ice before it thaws, out of floods and fires, fires put out,
	// waves timed for the night: "aiWeather" 1 for every AI, 2 or 3 for every other one
	// (comparisons), 0 off
	int aiWeather = 1;
	// each AI player's Lua AI (LuaAI\<name>.lua), by player id; none: the scripts and the C++ AI
	std::map<uint32_t, std::string> aiScripts;
	// per player, its buildings destroyed (removed with no hit points left): the AI tests
	std::map<uint32_t, int> buildingsDestroyed;
	// and their hit point capacities summed (with the damage to standing buildings, a steadier
	// measure of the attacks than the count)
	std::map<uint32_t, float> buildingHpDestroyed;
	// (shooting player, target's player) -> missiles buildings fired (ServerGameObject::associateObject)
	std::map<std::pair<uint32_t, uint32_t>, int> buildingShots;
	std::map<std::string, float> userProfileItems;
	// wkbai's day and night, seasons and weather (the skirmish settings "dayNight", "seasons", ...): off
	ServerEnvironment environment;
	// wkbai's unit history: who killed whom (UnitHistory.h)
	UnitHistory unitHistory;
	// The USER object (under the first player, as FINDER_USER), or null
	ServerGameObject* findUser();
	ServerGameObject *createObject(const GameObjBlueprint *blueprint, uint32_t id = 0);
	ServerGameObject* spawnObject(const GameObjBlueprint* blueprint, ServerGameObject* parent, const Vector3& initialPosition, const Vector3& initialOrientation);
	ServerGameObject* stampdownObject(const GameObjBlueprint* blueprint, ServerGameObject* player, const Vector3& position, const Vector3& orientation,
		bool sendEvent, bool inGameplay);
	void deleteObject(ServerGameObject *obj);
	void destroyObject(ServerGameObject* obj);
	void freeDeletedObjects();

	//ServerGameObject* findObject(uint32_t id) { auto it = idmap.find(id); return (it != idmap.end()) ? it->second : nullptr; }

	void addClient(NetLink* link);
	void removeClient(NetLink* link);

	// Multiplayer: an AI General takes over the player of a guest who leaves the game: "aiTakeOver"
	bool aiTakeOver = true;
	// guests with no position watch the game (spectators), at most maxSpectators of them
	bool allowSpectators = false;
	int maxSpectators = 4;
	// the password a guest has to give (NETSRVMSG_LOBBY_HELLO); empty: none
	std::string password;
	// Which mods guests may have ("modPolicy"): every one must have the host's game logic (the
	// same mods with logic files: ModManifest, CompareModLogic), and
	enum ModPolicy { MODS_MATCH = 0, // that is all (mods that only change looks are free)
		MODS_SEND = 1, // the host sends its logic mods to a guest who lacks them, for this game
		MODS_NONE = 2, // no mods at all, of any kind
		MODS_ALLOWED = 3 }; // only the host's own mods and those in wkconfig.json "modAllowList"
	int modPolicy = MODS_MATCH;
	// agent control's harness: the simulation steps only when told ("harness.advance")
	bool lockstep = false;
	uint32_t lockstepSteps = 0;  // steps still to run
	uint64_t lockstepDone = 0;   // steps run in lockstep so far
	// Which wkbai guests must run ("buildPolicy", BuildIdentity::Policy): 0 the same network
	// version, 1 the same version (the default), 2 an identical build (its code proved by a challenge)
	int buildPolicy = 1;
	// a guest's build against the policy ("" fits)
	std::string checkGuestBuild(int clientIndex);
	// NETSRVMSG_LOBBY_HELLO (again when a held hello's build answer comes)
	void lobbyHello(int clientIndex, const std::string& data);
	// Agent control ("allowAgents", on by default): guests whose game an agent plays may join;
	// off, a guest that says so is turned away. Everyone is told who has it on
	// (NETCLIMSG_AGENT_PLAYERS, a chat line when it changes)
	bool allowAgents = true;
	// every player an agent played at some time in this game (player id: name), kept by saves
	// ("agentPlayer.<id>=<name>"), for telling agent games from people's
	std::map<uint32_t, std::string> agentPlayersEver;
	std::string clientDisplayName(int clientIndex);
	void sendAgentPlayers();
	void noteAgentPlayer(int clientIndex);
	// a guest's mods checked at its hello: "" when it may join; a reason to turn it away; or
	// "sent" when the host sent it its mods (it says hello again with them mounted)
	std::string checkGuestMods(int clientIndex);
	void sendModsTo(int clientIndex);
	// the players a human whose client left played, before an AI General took them over
	std::map<uint32_t, const GameObjBlueprint*> humanBlueprints;
	// a running game's state, for a guest who joins it: every object with what the clients
	// know of it, the diplomacy, the aliases, the levelled ground, the time
	void sendStateTo(NetLink* link);
	// the players a guest who joins the running game can take: those an AI plays and those
	// whose human left
	std::vector<ServerGameObject*> freeSeats();
	int spectatorCount() const;
	// the running game's places to the client (NETCLIMSG_SEATS), or it is turned away when there
	// are none and it can't watch
	void offerSeats(int clientIndex);
	void refuseClient(int clientIndex, const std::string& reason);
	// the client watches the game, through the host's player's eyes
	void makeSpectator(int clientIndex);
	// an AI General plays the player from now on ("Setup My AI Player Stuff" starts its AI)
	// (general: that one, else a random one; returns the one chosen)
	const GameObjBlueprint* takeOverByAI(ServerGameObject* player, const GameObjBlueprint* general = nullptr);
	// a message to one client (none in a replay to the recorded clients beyond its viewer)
	void sendToClient(int clientIndex, const NetPacketWriter& packet);
	// a client that left a running game: its player's control goes, an AI takes it over if
	// takeOver; recorded ('R'), so that a replay does the same
	void clientLeftGame(size_t clientIndex, bool takeOver);
	// the vertices levelTerrainUnder changed (for sendStateTo)
	std::set<int> levelledVertices;
	// What the host works out from its own files (NETCLIMSG_HOST_DATA), so that every client has
	// the same: the timeline's map regions (its saved\Regions, or worked out on its map) and the
	// map's water design. To every client (only: that one); prepareHostData works out the regions
	// once the game is set up (the trees standing then) and sends them
	void sendHostData(int kind, const std::string& text, NetLink* only = nullptr);
	void prepareHostData();
	std::string hostRegionsText;

	void syncTime();

	void setClientPlayerControl(int clientIndex, uint32_t playerObjId);
	// a building stamped down in play levels the ground under the tiles of its footprint that
	// flatten (DEFORM_TERRAIN), and the clients are told
	void levelTerrainUnder(ServerGameObject* building);
	// each client's loading percentage, its player's colour and its name to every client (the
	// count's top bit: still waiting for the players' loads)
	void sendLoadingStages();
	// A multiplayer game starts when every human player has loaded it (its NETSRVMSG_LOADING_STAGE
	// at 100): until then the clock stands and the players' orders aren't taken, so that a faster
	// computer gets no head start; after LOAD_WAIT_MS it starts anyway
	bool waitingForLoads = false;
	uint64_t waitingForLoadsSince = 0;
	static constexpr uint64_t LOAD_WAIT_MS = 180000;
	void waitForLoads();
	void checkLoadsDone();
	void setDiplomaticStatus(ServerGameObject *a, ServerGameObject *b, int status);
	void showGameTextWindow(ServerGameObject* player, int gtwIndex);
	void hideGameTextWindow(ServerGameObject* player, int gtwIndex);
	void hideCurrentGameTextWindow(ServerGameObject* player);
	void snapCameraPosition(ServerGameObject* player, const Vector3& position, const Vector3& orientation);
	void storeCameraPosition(ServerGameObject* player);
	void restoreCameraPosition(ServerGameObject* player);
	void playCameraPath(ServerGameObject* player, int camPathIndex);
	void stopCameraPath(ServerGameObject* player, bool skipActions);
	void skipCameraPath(ServerGameObject* player);
	void interpolateCameraToPosition(ServerGameObject* player, const Vector3& position, const Vector3& orientation, float duration);
	void interpolateCameraToStoredPosition(ServerGameObject* player, float duration);
	void setGameSpeed(float nextSpeed);
	void playMusic(ServerGameObject* player, int musicTag);
	void assignAlias(int aliasIndex, ServerGameObject* object);
	void unassignAlias(int aliasIndex, ServerGameObject* object);
	void clearAlias(int aliasIndex);

	void startLevel();

	void tick();
	// tick's parts: the game for one time step, then the clients' messages and the time sync
	void simulate();
	// the players' peasant activity counts (Idle Peasant Count, Wood Chopper Count, ...), which
	// WKB.exe's engine keeps and the peasant activity interface shows; once a game second
	void updatePeasantActivity();
	float nextPeasantActivityUpdate = 0.0f;
	// simulate() calls so far (staggers the work that doesn't need every step)
	uint64_t simStepIndex = 0;
	// The objects that generate sight range events, by blocks of SIGHT_BLOCK x SIGHT_BLOCK tiles
	// (in no order). The sight updates search these: the tiles' own lists hold every tree
	// and prop, and going through all of them, tile by tile, was a third of the simulation's
	// time late in a 6-AI game. The objects themselves, not their ids: looking each candidate
	// up by id was most of a step with 1500 units walking together (RE repo docs/engine/fast-movement.md). An
	// object leaves its block when it is destroyed (Server::destroyObject), as it leaves idmap.
	// The block keeps each one's position too (updatePosition keeps it), so the search goes
	// through one array.
	static constexpr int SIGHT_BLOCK = 4;
	struct SightEntry { float x, y, z; uint32_t id; ServerGameObject* obj; };
	std::vector<std::vector<SightEntry>> sightBlocks;
	int sightBlocksWidth = 0, sightBlocksHeight = 0;
	void updateSightBlock(ServerGameObject* obj, bool removed = false);
	void network();
	// one client message (NETSRVMSG_*), as network() receives it
	void handlePacket(int clientIndex, const std::string& data);

	// Game recordings: the game as it starts (a save game) and then, in order, each simulate()
	// with how far the clock moved and whether the game was paused, and each client message.
	// From the same start the simulation is deterministic, so a replay that feeds them in the
	// same order plays the same game (the AIs included).
	struct Recording {
		// 'S' <ms> <paused> <count>: count simulate() calls with the clock moved by ms each;
		// 'M' <client> <length> <bytes>: a client message; 'R' <client> <takeover>: a client left
		FILE* file = nullptr;
		uint32_t runMs = 0, runCount = 0;
		bool runPaused = false;
		uint32_t lastTime = 0;
		void flushRun();
	};
	std::unique_ptr<Recording> recording;
	// the AI settings above (not in a save game), for a recording: "name=value" lines
	std::string aiSettingsText() const;
	// (scriptsOnly: only the players' Lua AIs, as a save game's companion file has them)
	void applyAiSettingsText(const std::string& text, bool scriptsOnly = false);
	// a replay's copies of the Lua AIs its recording used (LuaAI\<name>.lua in its folder)
	std::filesystem::path replayScripts;
	// starts recording into the folder (start.sav, events.bin, ai.txt); false if it can't be written
	bool startRecording(const std::filesystem::path& folder, const std::string& setupJson = std::string());
	// the seed startLevel gave the random numbers (a recording's setup)
	uint32_t lastSeed = 0;
	void stopRecording();
	// a recording being played: its events, where it is, how many calls of the current run are
	// left; replayPaused: the viewer's pause (the recorded pauses are part of the events)
	struct Replay {
		struct Event { char type; uint32_t ms = 0; bool paused = false; uint32_t count = 0; int client = 0; std::string data; bool takeOver = false; };
		std::vector<Event> events;
		size_t next = 0;
		uint32_t leftInRun = 0;
		uint32_t budgetMs = 0, lastSDLTime = 0;
		std::vector<uint32_t> clientPlayers;
	};
	std::unique_ptr<Replay> replay;
	// the viewer's playback (atomic: its interface reads them, and sets replaySeekMs, from its
	// own thread): its pause, the game time played and the whole recording's (the stepped
	// calls' ms), and a jump forward to replaySeekMs of played time (0: none), run as fast as
	// it goes
	std::atomic<bool> replayPaused = false;
	std::atomic<uint32_t> replayPlayedMs = 0, replayTotalMs = 0, replaySeekMs = 0;
	// reads a recording's events (its start.sav is loaded as a save game first); false if none
	bool startReplay(const std::filesystem::path& folder);
	// one event of the replay; false at its end
	bool replayEvent();
	bool replayFinished() const { return replay && replay->next >= replay->events.size(); }

private:
	std::map<uint32_t, const GameObjBlueprint*> predec;

	ServerGameObject * loadObject(GSFileParser & gsf, const std::string & clsname);
	void loadSavePredec(GSFileParser & gsf);

	void sendToAll(const NetPacket &packet);
	void sendToAll(const NetPacketWriter &packet);
	void sendTo(ServerGameObject* player, const NetPacketWriter& packet);

	friend struct ServerGameObject;
	friend struct UnitHistory;
	friend struct ServerChronicle;
};