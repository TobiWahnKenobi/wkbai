// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#pragma once

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
	std::unordered_map<int, std::unordered_set<SrvGORef>> associates, associators;
	std::vector<SrvGORef> referencers;
	std::unordered_set<SrvGORef> seenObjects;
	std::set<std::pair<int, int>> zoneTiles;
	int clientIndex = -1;
	bool isMusicPlaying = false;
	float currentSpeed = 0.0f;
	MovementController movementController{ this };
	AIController aiController{ this };
	FormationController formationController{ this };
	Vector3 playerStartCameraPosition, playerStartCameraOrientation;

	ServerGameObject(uint32_t id, const GameObjBlueprint *blueprint) : SpecificGameObject<Server, ServerGameObject>(id, blueprint), orderConfig(this) {}

	void setItem(int index, float value);
	void setParent(ServerGameObject *newParent);
	void setPosition(const Vector3 &position);
	void setOrientation(const Vector3 &orientation);
	void setSubtypeAndAppearance(int new_subtype, int new_appearance);
	void setColor(int color);
	// startTime: when the animation began (< 0: now)
	void setAnimation(int animationIndex, bool isClamped = false, int synchronizedTask = -1, float startTime = -1.0f);
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

	void updatePosition(const Vector3 &newposition, bool events = false);
	void lookForSightRangeEvents();
	void addZoneTile(int tx, int tz);
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
		SrvGORef lastStampdown;
		std::vector<SrvGORef> unitsForNewFormation;
	};
	std::vector<ClientInfo> clientInfos;

	time_t lastSync;

	std::vector<std::tuple<ServerGameObject*, int, int>> postAssociations;
	// task targets read from a save game, set once all objects are loaded (a target can come later in the file)
	std::vector<std::pair<Task*, int>> postTaskTargets;

	ServerGameObject* objToDelete = nullptr, * objToDeleteLast = nullptr;

	Server() { instance = this; }

	void loadSaveGame(const char *filename);
	// Writes the game to a save game file (a file system path, its folders are created) that
	// loadSaveGame reads back (ServerSave.cpp)
	bool saveGame(const char* filename);
	// what the game was loaded from, for saving
	std::string gameSetFile, mapFile;
	// The Kingmaker campaign: CONQUER_LEVEL (the province is won, the game ends) and
	// UPDATE_USER_PROFILE (the USER object's items, kept in the campaign profile)
	std::atomic<bool> levelConquered = false;
	// EXIT_TO_MAIN_MENU and BOOT_LEVEL "<level>" (the game endings, the tutorials' next lesson):
	// the game ends; bootLevel is set before the flag
	std::string bootLevel;
	std::atomic<bool> exitToMenu = false, levelBooted = false;
	bool userProfileUpdated = false;
	std::map<std::string, float> userProfileItems;
	// The USER object (under the first player, as FINDER_USER), or null
	ServerGameObject* findUser();
	ServerGameObject *createObject(const GameObjBlueprint *blueprint, uint32_t id = 0);
	ServerGameObject* spawnObject(const GameObjBlueprint* blueprint, ServerGameObject* parent, const Vector3& initialPosition, const Vector3& initialOrientation);
	ServerGameObject* stampdownObject(const GameObjBlueprint* blueprint, ServerGameObject* player, const Vector3& position, const Vector3& orientation,
		bool sendEvent, bool inGameplay);
	void deleteObject(ServerGameObject *obj);
	void destroyObject(ServerGameObject* obj);

	//ServerGameObject* findObject(uint32_t id) { auto it = idmap.find(id); return (it != idmap.end()) ? it->second : nullptr; }

	void addClient(NetLink* link);
	void removeClient(NetLink* link);

	void syncTime();

	void setClientPlayerControl(int clientIndex, uint32_t playerObjId);
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

private:
	std::map<uint32_t, const GameObjBlueprint*> predec;

	ServerGameObject * loadObject(GSFileParser & gsf, const std::string & clsname);
	void loadSavePredec(GSFileParser & gsf);

	void sendToAll(const NetPacket &packet);
	void sendToAll(const NetPacketWriter &packet);
	void sendTo(ServerGameObject* player, const NetPacketWriter& packet);

	friend struct ServerGameObject;
};