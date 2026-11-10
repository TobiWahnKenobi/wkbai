// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#define _USE_MATH_DEFINES
#include "client.h"
#include "util/LoadingProgress.h"
#include "WaterDesign.h"
#include <filesystem>
#include <fstream>
#include "file.h"
#include "server.h"
#include "network.h"
#include "gameset/gameset.h"
#include <cassert>
#include "terrain.h"
#include "interface/ClientInterface.h"
#include <SDL3/SDL_timer.h>
#include "gameset/ScriptContext.h"
#include <cmath>
#include "SoundPlayer.h"
#include <tracy/Tracy.hpp>

Client * Client::instance = nullptr;

void Client::loadFromServerObject(Server & server)
{
}

void Client::tick()
{
	ZoneScoped;
	timeManager.tick();

	const auto walkObj = [this](ClientGameObject *obj, auto &rec) -> void {
		if (obj->movement.isMoving()) {
			Vector3 newPosition = obj->movement.getNewPosition(timeManager.currentTime);
			newPosition.y = terrain->getHeightEx(newPosition.x, newPosition.z, obj->blueprint->canWalkOnWater());
			obj->updatePosition(newPosition);
			Vector3 dir = obj->movement.getDirection();
			obj->orientation.y = atan2f(dir.x, -dir.z);
		}
		if (obj->trajectory.isMoving()) {
			obj->updatePosition(obj->trajectory.getPosition(timeManager.currentTime));
			obj->orientation = obj->trajectory.getRotationAngles(timeManager.currentTime);
		}
		for (auto &it : obj->children)
			for (CommonGameObject *obj : it.second)
				rec((ClientGameObject*)obj, rec);
	};
	if(getLevel())
		walkObj(getLevel(), walkObj);

	// Camera paths
	auto resetCameraMode = [this](bool skipActions = false) {
		if (cameraCurrentPath && !skipActions) {
			sendCameraPathEnded(cameraPathIndex);
		}
		cameraMode = 0;
		cameraCurrentPath = nullptr;
	};
	if (cameraMode == 1) {
		float camTime = (float)(SDL_GetTicks() - cameraStartTime) / 1000.0f;
		float prevTime = 0.0f, nextTime = 0.0f;
		size_t nextNode;
		for (nextNode = 1; nextNode < cameraPathPos.size(); nextNode++) {
			prevTime = nextTime;
			nextTime += cameraPathDur[nextNode];
			if (camTime < nextTime)
				break;
		}
		if (nextNode == cameraPathPos.size()) {
			resetCameraMode();
		}
		else {
			auto p0 = cameraPathPos[nextNode - 1];
			auto p1 = cameraPathPos[nextNode];
			// Limit angles to [0;2pi)
			constexpr float pi = (float)M_PI;
			for (auto& p : { &p0, &p1 }) {
				for (float& c : p->second) {
					c = fmodf(c, 2*pi);
					if (c < 0.0f) c += 2*pi;
				}
			}
			// Try to make distances between angles as small as possible (< pi radians)
			for (int i = 0; i < 3; i++) {
				float d = p1.second.coord[i] - p0.second.coord[i];
				if (fabs(d) > pi) {
					if (d > 0)
						p1.second.coord[i] -= 2 * pi;
					else
						p1.second.coord[i] += 2 * pi;
				}
			}
			float dt = (camTime - prevTime) / (nextTime - prevTime);
			Vector3 pos = (p1.first - p0.first)*dt + p0.first;
			Vector3 ori = (p1.second - p0.second)*dt + p0.second;
			camera.position = pos;
			camera.orientation = ori;
		}
	}

	// Sound & music
	SoundPlayer* soundPlayer = SoundPlayer::getSoundPlayer();
	soundPlayer->setListenerPosition(camera.position, camera.direction.normal());
	if (isMusicPlaying) {
		if (!soundPlayer->isMusicPlaying()) {
			isMusicPlaying = false;
			NetPacketWriter msg{ NETSRVMSG_MUSIC_COMPLETED };
			serverLink->send(msg);
		}
	}

	// Special effects
	specialEffects.remove_if([this](SpecialEffect& sfx) {return timeManager.currentTime >= sfx.startTime + sfx.entity.model->getDuration(); });
	for (auto nextIt = loopingSpecialEffects.begin(); nextIt != loopingSpecialEffects.end();) {
		auto it = nextIt++;
		auto& lsfx = *it;
		if (!lsfx.first.first.get())
			loopingSpecialEffects.erase(it);
	}

	if (serverLink) {
		uint32_t pstime = SDL_GetTicks();
		if ((pstime - dbgLastMessageCountTime) >= 1000) {
			dbgLastMessageCountTime = pstime;
			dbgNumMessagesPerSec = dbgNumMessagesInCurrentSec;
			dbgNumMessagesInCurrentSec = 0;
		}
		dbgNumMessagesPerTick = 0;

		int pcnt = 1000000; //100;
		while (serverLink->available() && (pcnt--)) {
			NetPacket packet = serverLink->receive();
			BinaryReader br(packet.data.c_str());
			uint8_t type = br.readUint8();
			++dbgNumMessagesPerTick;

			switch (type) {
			case NETCLIMSG_TEST: {
				std::string msg = br.readStringZ();
				chatMessages.push_back(msg);
				info("Client got message: %s\n", msg.c_str());
				break;
			}
			case NETCLIMSG_GAME_SET:
			{
				std::string gsFileName = br.readStringZ();
				// a new game: no day and night or weather until the server says so
				environmentActive = false;
				environmentSkyKnown = false;
				environment = EnvironmentState();
				nightSight = NightSight();
				unitInfo.clear();
				newHeroes.clear();
				chronicleIn.clear();
				chronicleParts.clear();
				heroThreshold = 0;
				int gsVersion = br.readUint8();
				if (localServer) {
					info("Client takes gameset from local server.\n");
					gameSet = Server::instance->gameSet;
				}
				else {
					info("Loading game set: %s\n", gsFileName.c_str());
					// a guest's load starts here (a host's before its server's thread)
					LoadingBegin();
					LoadingEnterStep(LOADING_GAME_SET);
					gameSet = std::make_shared<GameSet>(gsFileName.c_str(), gsVersion);
					LoadingEnterStep(LOADING_LEVEL);
				}
				break;
			}
			case NETCLIMSG_CONTROL_PLAYER: {
				const uint32_t playerId = br.readUint32();
				clientPlayer = playerId;
				awaitingSeat = false;
				printf("Client: controlling player %u\n", playerId);
				break;
			}
			case NETCLIMSG_OBJECT_CREATED: {
				uint32_t objid = br.readUint32();
				uint32_t bpid = br.readUint32();
				uint32_t subtype = br.readUint32();
				uint32_t appear = br.readUint32();
				const GameObjBlueprint *blueprint = gameSet->getBlueprint(bpid);
				info("Object %u of type %s is created.\n", objid, blueprint->getFullName().c_str());
				ClientGameObject *obj = createObject(blueprint, objid);
				obj->subtype = subtype;
				obj->appearance = appear;
				if (blueprint->bpClass == Tags::GAMEOBJCLASS_LEVEL)
					level = obj;
				break;
			}
			case NETCLIMSG_OBJECT_ITEM_SET: {
				uint32_t objid = br.readUint32();
				uint32_t index = br.readUint32();
				float value = br.readFloat();
				info("Object %u changed its item \"%s\" to the value %f.\n", objid, gameSet->items.names.getString(index).c_str(), value);
				auto* obj = findObject(objid);
				if (!obj) printf("CLI-WARN: Obj %i from message does not exist\n", objid);
				if (obj)
					obj->items[index] = value;
				break;
			}
			case NETCLIMSG_OBJECT_PARENT_SET: {
				uint32_t objid = br.readUint32();
				uint32_t parentid = br.readUint32();
				info("Object %u is parented to object %u.\n", objid, parentid);
				ClientGameObject *obj = findObject(objid), *newParent = findObject(parentid);
				if (obj->parent) {
					auto &vec = obj->parent->children[obj->blueprint];
					vec.erase(std::remove(vec.begin(), vec.end(), obj), vec.end()); // is order important?
				}
				obj->parent = newParent;
				if (newParent)
					newParent->children[obj->blueprint].push_back(obj);
				break;
			}
			case NETCLIMSG_TERRAIN_SET: {
				std::string mapfp = br.readStringZ();
				LoadingEnterStep(LOADING_TERRAIN);
				LoadingDetail("the map");
				terrain = new Terrain;
				terrain->readFromFile(mapfp.c_str());
				if (cliInterface)
					cliInterface->updateTerrain();
				auto area = this->terrain->getNumPlayableTiles();
				this->tiles = std::make_unique<Tile[]>(area.first * area.second);
				break;
			}
			case NETCLIMSG_TERRAIN_HEIGHTS: {
				const uint32_t count = br.readUint32();
				const float steps = br.readFloat();
				for (uint32_t k = 0; k < count; k++) {
					const uint32_t i = br.readUint32();
					// a local server's terrain is the client's own only if it is the same object
					if (terrain && i < terrain->vertices.size() && !(Server::instance && Server::instance->terrain == terrain))
						terrain->setHeightSteps((int)i, steps);
				}
				if (cliInterface)
					cliInterface->terrainHeightsChanged = true;
				break;
			}
			case NETCLIMSG_OBJECT_POSITION_SET: {
				uint32_t objid = br.readUint32();
				Vector3 pos = br.readVector3();
				info("Object %u teleported to (%f,%f,%f)\n", objid, pos.x, pos.y, pos.z);
				if (ClientGameObject *obj = findObject(objid)) {
					obj->updatePosition(pos);
				}
				break;
			}
			case NETCLIMSG_OBJECT_ORIENTATION_SET: {
				uint32_t objid = br.readUint32();
				Vector3 ori = br.readVector3();
				info("Object %u oriented to (%f,%f,%f)\n", objid, ori.x, ori.y, ori.z);
				if (ClientGameObject *obj = findObject(objid)) {
					obj->orientation = ori;
				}
				break;
			}
			case NETCLIMSG_OBJECT_SUBTYPE_APPEARANCE_SET: {
				uint32_t objid = br.readUint32();
				uint32_t subtype = br.readUint32();
				uint32_t appear = br.readUint32();
				if (ClientGameObject *obj = findObject(objid)) {
					obj->subtype = subtype;
					obj->appearance = appear;
				}
				break;
			}
			case NETCLIMSG_OBJECT_COLOR_SET: {
				uint32_t objid = br.readUint32();
				uint8_t color = br.readUint8();
				info("Object %u changed its color to %u\n", objid, color);
				if (ClientGameObject *obj = findObject(objid)) {
					obj->color = color;
				}
				break;
			}
			case NETCLIMSG_TIME_SYNC: {
				timeManager.psCurrentTime = br.readUint32();
				timeManager.currentTime = (float)timeManager.psCurrentTime / 1000.0f;
				timeManager.paused = br.readUint8();
				break;
			}
			case NETCLIMSG_OBJECT_ANIM_SET: {
				uint32_t objid = br.readUint32();
				if (ClientGameObject *obj = findObject(objid)) {
					obj->animationIndex = br.readUint32();
					obj->animationVariant = br.readUint32();
					obj->animStartTime = br.readFloat();
					obj->animClamped = br.readUint8();
					obj->animSynchronizedTask = br.readUint32();
					obj->animSpeed = br.readFloat();
				}
				break;
			}
			case NETCLIMSG_OBJECT_MOVEMENT_STARTED: {
				uint32_t objid = br.readUint32();
				Vector3 src = br.readVector3();
				Vector3 dst = br.readVector3();
				float startTime = br.readFloat();
				float speed = br.readFloat();
				if (ClientGameObject *obj = findObject(objid)) {
					obj->movement.startMovement(src, dst, startTime, speed);
				}
				break;
			}
			case NETCLIMSG_OBJECT_MOVEMENT_STOPPED: {
				uint32_t objid = br.readUint32();
				if (ClientGameObject *obj = findObject(objid)) {
					obj->movement.stopMovement();
				}
				break;
			}
			case NETCLIMSG_OBJECT_REMOVED: {
				uint32_t objid = br.readUint32();
				if (ClientGameObject *obj = findObject(objid)) {
					// remove from parent's children
					auto &vec = obj->parent->children.at(obj->blueprint);
					vec.erase(std::find(vec.begin(), vec.end(), obj));
					// remove from ID map
					idmap.erase(obj->id);
					// delete the object, bye!
					delete obj;
				}
				break;
			}
			case NETCLIMSG_OBJECT_CONVERTED: {
				uint32_t objid = br.readUint32();
				uint32_t postbpid = br.readUint32();
				if (ClientGameObject *obj = findObject(objid)) {
					const GameObjBlueprint *postbp = gameSet->getBlueprint(postbpid);
					// remove from parent's children
					auto &vec = obj->parent->children.at(obj->blueprint);
					vec.erase(std::find(vec.begin(), vec.end(), obj));
					// add it back at the correct blueprint key
					obj->parent->children[postbp].push_back(obj);
					// now converted!
					obj->blueprint = postbp;
				}
				break;
			}
			case NETCLIMSG_DIPLOMATIC_STATUS_SET: {
				uint32_t id1 = br.readUint32();
				uint32_t id2 = br.readUint32();
				uint8_t status = br.readUint8();
				ClientGameObject *a = findObject(id1), *b = findObject(id2);
				if (a && b) {
					int &ref = (a->id <= b->id) ? diplomaticStatuses[{ a->id, b->id }] : diplomaticStatuses[{ b->id, a->id }];
					ref = status;
				}
				break;
			}
			case NETCLIMSG_SHOW_GAME_TEXT_WINDOW: {
				int gtwIndex = br.readUint32();
				gtwStates[gtwIndex] = 0;
				const auto& snd = gameSet->gameTextWindows[gtwIndex].pages[0].activationSound;
				if(!snd.empty())
					SoundPlayer::getSoundPlayer()->playSound(snd);
				break;
			}
			case NETCLIMSG_OBJECT_SCALE_SET: {
				auto id = br.readUint32();
				Vector3 scale = br.readVector3();
				if (ClientGameObject* obj = findObject(id))
					obj->scale = scale;
				break;
			}
			case NETCLIMSG_HIDE_GAME_TEXT_WINDOW: {
				const int gtwIndex = br.readUint32();
				gtwStates.erase(gtwIndex);
				break;
			}
			case NETCLIMSG_HIDE_CURRENT_GAME_TEXT_WINDOW: {
				gtwStates.clear();
				break;
			}
			case NETCLIMSG_OBJECT_FLAGS_SET: {
				uint32_t objid = br.readUint32();
				uint8_t flags = br.readUint8();
				if (ClientGameObject* obj = findObject(objid)) {
					obj->flags = flags;
				}
				break;
			}
			case NETCLIMSG_SNAP_CAMERA_POSITION: {
				resetCameraMode();
				camera.position = br.readVector3();
				camera.orientation = br.readVector3();
				break;
			}
			case NETCLIMSG_STORE_CAMERA_POSITION: {
				storedCameraPosition = camera.position;
				storedCameraOrientation = camera.orientation;
				break;
			}
			case NETCLIMSG_RESTORE_CAMERA_POSITION: {
				resetCameraMode();
				camera.position = storedCameraPosition;
				camera.orientation = storedCameraOrientation;
				break;
			}
			case NETCLIMSG_PLAY_CAMERA_PATH: {
				resetCameraMode();
				int camPathIndex = br.readUint32();
				const CameraPath& gspath = gameSet->cameraPaths[camPathIndex]; // TODO: boundary check
				cameraMode = 1;
				cameraCurrentPath = &gspath;
				cameraPathIndex = camPathIndex;
				cameraStartTime = SDL_GetTicks();
				cameraPathPos.clear();
				cameraPathDur.clear();
				if (gspath.startAtCurrentCameraPosition) {
					cameraPathPos.push_back({ camera.position, camera.orientation });
					cameraPathDur.push_back(1.0f);
				}
				for (auto& node : gspath.pathNodes) {
					CliScriptContext ctx(this);
					OrientedPosition op = node.position->eval(&ctx);
					cameraPathPos.push_back({ op.position, op.rotation });
					cameraPathDur.push_back(node.duration->eval(&ctx));
				}
				if (!cameraPathPos.empty()) {
					std::tie(camera.position, camera.orientation) = cameraPathPos.front();
				}
				break;
			}
			case NETCLIMSG_STOP_CAMERA_PATH: {
				bool skipActions = br.readUint8();
				resetCameraMode(skipActions);
				break;
			}
			case NETCLIMSG_OBJECT_TRAJECTORY_STARTED: {
				uint32_t objid; Vector3 initPos, initVel; float startTime;
				br.readTo(objid, initPos, initVel, startTime);
				ClientGameObject* obj = findObject(objid);
				obj->trajectory.start(initPos, initVel, startTime);
				break;
			}
			case NETCLIMSG_GAME_SPEED_CHANGED: {
				timeManager.setSpeed(br.readFloat());
				break;
			}
			case NETCLIMSG_SOUND_AT_OBJECT: {
				uint32_t objid; int soundTag; uint32_t targetid; uint8_t randval;
				br.readTo(objid, soundTag, targetid, randval);
				if (ClientGameObject* from = findObject(objid)) {
					if (ClientGameObject* to = findObject(targetid)) {
						std::string path; float refDist, maxDist;
						std::tie(path, refDist, maxDist) = from->blueprint->getSound(soundTag, from->subtype);
						if (!path.empty()) {
							std::string gfspath = "Warrior Kings Game Set\\Sounds\\" + path;
							if (to->blueprint->bpClass == Tags::GAMEOBJCLASS_PLAYER)
								SoundPlayer::getSoundPlayer()->playSound(gfspath);
							else {
								//float rolloff = (refDist / 0.1f - refDist) / (maxDist - refDist);
								SoundPlayer::getSoundPlayer()->playSound3D(gfspath, to->position, refDist, maxDist);
							}
						}
					}
				}
				break;
			}
			case NETCLIMSG_SOUND_AT_POSITION: {
				uint32_t objid; int soundTag; Vector3 targetPos; uint8_t randval;
				br.readTo(objid, soundTag, targetPos, randval);
				if (ClientGameObject* from = findObject(objid)) {
					std::string path; float refDist, maxDist;
					std::tie(path, refDist, maxDist) = from->blueprint->getSound(soundTag, from->subtype);
					if (!path.empty()) {
						std::string gfspath = "Warrior Kings Game Set\\Sounds\\" + path;
						SoundPlayer::getSoundPlayer()->playSound3D(gfspath, targetPos, refDist, maxDist);
					}
				}
				break;
			}
			case NETCLIMSG_MUSIC_CHANGED: {
				int musicTag;
				br.readTo(musicTag);
				if (ClientGameObject* player = clientPlayer) {
					auto it = player->blueprint->musicMap.find(musicTag);
					if (it != player->blueprint->musicMap.end()) {
						size_t var = (size_t)rand() % it->second.size();
						SoundPlayer::getSoundPlayer()->playMusic("Warrior Kings Game Set\\Sounds\\" + it->second[var]);
						isMusicPlaying = true;
					}
				}
				break;
			}
			case NETCLIMSG_SPECIAL_EFFECT_PLAYED_AT_POS: {
				uint32_t objid; int sfxTag; Vector3 pos;
				br.readTo(objid, sfxTag, pos);
				if (auto* obj = findObject(objid)) {
					if (Model* sfxmodel = obj->blueprint->getSpecialEffect(sfxTag)) {
						specialEffects.emplace_back();
						auto& sfx = specialEffects.back();
						sfx.entity.transform = Matrix::getTranslationMatrix(pos);
						sfx.entity.model = sfxmodel;
						sfx.entity.color = obj->color;
						sfx.startTime = timeManager.currentTime; // problem: client might start at different time...
					}
				}
				break;
			}
			case NETCLIMSG_SPECIAL_EFFECT_PLAYED_BETWEEN: {
				uint32_t objid; int sfxTag; Vector3 pos1, pos2;
				br.readTo(objid, sfxTag, pos1, pos2);
				if (auto* obj = findObject(objid)) {
					if (Model* sfxmodel = obj->blueprint->getSpecialEffect(sfxTag)) {
						Vector3 dir = pos2 - pos1;
						float dist = dir.len3();
						float ang = std::atan2(dir.x, -dir.z);

						specialEffects.emplace_back();
						auto& sfx = specialEffects.back();
						sfx.entity.transform = Matrix::getScaleMatrix({ dist,dist,dist }) * Matrix::getRotationYMatrix(-ang) * Matrix::getTranslationMatrix(pos1);
						sfx.entity.model = sfxmodel;
						sfx.entity.color = obj->color;
						sfx.startTime = timeManager.currentTime; // problem: client might start at different time...
					}
				}
				break;
			}
			case NETCLIMSG_ATTACHED_SPECIAL_EFFECT_PLAYED: {
				uint32_t objid; int sfxTag; uint32_t targetid;
				br.readTo(objid, sfxTag, targetid);
				if (auto* obj = findObject(objid)) {
					if (auto* target = findObject(targetid)) {
						if (Model* sfxmodel = obj->blueprint->getSpecialEffect(sfxTag)) {
							specialEffects.emplace_back();
							auto& sfx = specialEffects.back();
							sfx.entity.model = sfxmodel;
							sfx.entity.color = obj->color;
							sfx.attachedObj = target;
							sfx.startTime = timeManager.currentTime; // problem: client might start at different time...
						}
					}
				}
				break;
			}
			case NETCLIMSG_OBJECT_NAME_SET: {
				uint32_t objid = br.readUint32();
				int numChars = br.readUint8();
				if (ClientGameObject* obj = findObject(objid))
					obj->name = br.readString(numChars);
				break;
			}
			case NETCLIMSG_UNIT_KILLS: {
				const uint32_t objid = br.readUint32();
				br.readUint32(); // the victim (for the timeline, later)
				const int kills = (int)br.readUint32();
				const int buildings = (int)br.readUint32();
				if (ClientGameObject* obj = findObject(objid)) {
					obj->kills = kills;
					obj->buildingsDestroyed = buildings;
				}
				break;
			}
			case NETCLIMSG_UNIT_HISTORY: {
				const uint32_t objid = br.readUint32();
				UnitInfo& info = unitInfo[objid];
				info.birth = br.readFloat();
				info.kind = br.readUint8();
				const uint8_t flags = br.readUint8();
				info.flags = flags & ~4; // F_NEW_HERO: an event, not a state
				info.name = br.readStringZ();
				info.birthName = br.readStringZ();
				if (flags & 4)
					newHeroes.push_back(objid);
				break;
			}
			case NETCLIMSG_UNIT_UNTRACKED: {
				unitInfo.erase(br.readUint32());
				break;
			}
			case NETCLIMSG_HERO_THRESHOLD: {
				heroThreshold = (int)br.readUint32();
				break;
			}
			case NETCLIMSG_CHRONICLE: {
				const uint16_t part = br.readUint16(), parts = br.readUint16();
				const uint8_t length = br.readUint8();
				if (part == 0)
					chronicleParts.clear();
				chronicleParts += br.readString(length);
				if (part + 1 == parts) {
					chronicleIn.push_back(std::move(chronicleParts));
					chronicleParts.clear();
				}
				break;
			}
			case NETCLIMSG_CURRENT_ORDER_REPORT: {
				uint32_t objid = br.readUint32();
				if (ClientGameObject* obj = findObject(objid))
					obj->reportedCurrentOrder = (int)br.readUint32();
				break;
			}
			case NETCLIMSG_LOOPING_SPECIAL_EFFECT_ATTACHED: {
				uint32_t objid; int sfxTag; Vector3 pos;
				br.readTo(objid, sfxTag, pos);
				if (ClientGameObject* obj = findObject(objid)) {
					if (Model* sfxmodel = obj->blueprint->getSpecialEffect(sfxTag)) {
						std::pair<std::pair<CliGORef, int>, SceneEntity> p;
						p.first = { objid, sfxTag };
						p.second.transform = Matrix::getTranslationMatrix(pos);
						p.second.model = sfxmodel;
						p.second.color = obj->color;
						loopingSpecialEffects.insert(std::move(p));
					}
				}
				break;
			}
			case NETCLIMSG_LOOPING_SPECIAL_EFFECT_DETACHED: {
				uint32_t objid; int sfxTag;
				br.readTo(objid, sfxTag);
				if (ClientGameObject* obj = findObject(objid)) {
					auto it = loopingSpecialEffects.find({ objid, sfxTag });
					if (it != loopingSpecialEffects.end()) {
						loopingSpecialEffects.erase(it);
					}
				}
				break;
			}
			case NETCLIMSG_SKIP_CAMERA_PATH: {
				if (cameraCurrentPath && !cameraPathPos.empty()) {
					std::tie(camera.position, camera.orientation) = cameraPathPos.back();
				}
				resetCameraMode();
				break;
			}
			case NETCLIMSG_ALIAS_ASSIGNED: {
				auto [aliasIndex, objectId] = br.readValues<int, uint32_t>();
				aliases[aliasIndex].insert(objectId);
				break;
			}
			case NETCLIMSG_ALIAS_UNASSIGNED: {
				auto [aliasIndex, objectId] = br.readValues<int, uint32_t>();
				aliases[aliasIndex].erase(objectId);
				break;
			}
			case NETCLIMSG_ALIAS_CLEARED: {
				auto [aliasIndex] = br.readValues<int>();
				aliases[aliasIndex].clear();
				break;
			}
			case NETCLIMSG_INTERPOLATE_CAMERA_TO_POSITION: {
				resetCameraMode();
				auto [targetPosition, targetOrientation, duration] = br.readValues<Vector3, Vector3, float>();
				cameraMode = 1;
				cameraCurrentPath = nullptr;
				cameraPathIndex = -1;
				cameraStartTime = SDL_GetTicks();
				cameraPathPos = { {camera.position, camera.orientation}, {targetPosition, targetOrientation} };
				cameraPathDur = { 1.0f, duration };
				break;
			}
			case NETCLIMSG_INTERPOLATE_CAMERA_TO_STORED_POSITION: {
				resetCameraMode();
				auto [duration] = br.readValues<float>();
				cameraMode = 1;
				cameraCurrentPath = nullptr;
				cameraPathIndex = -1;
				cameraStartTime = SDL_GetTicks();
				cameraPathPos = { {camera.position, camera.orientation}, {storedCameraPosition, storedCameraOrientation} };
				cameraPathDur = { 1.0f, duration };
				break;
			}
			case NETCLIMSG_UPDATE_BUILDING_ORDER_COUNT_MAP: {
				auto [objectId, orderBlueprintId, updatedCount] = br.readValues<uint32_t, int, int>();
				if (ClientGameObject* obj = findObject(objectId))
					obj->buildingOrderCountMap[orderBlueprintId] = updatedCount;
				break;
			}
			case NETCLIMSG_CITY_RECTANGLE_ADDED: {
				auto [objectId, xStart, yStart, xEnd, yEnd] = br.readValues<uint32_t, int, int, int, int>();
				ClientGameObject::CityRectangle rect;
				rect.xStart = xStart;
				rect.yStart = yStart;
				rect.xEnd = xEnd;
				rect.yEnd = yEnd;
				if (ClientGameObject* obj = findObject(objectId))
					obj->cityRectangles.push_back(rect);
				break;
			}
			case NETCLIMSG_GAME_SET_FILE: {
				const uint16_t file = br.readUint16(), files = br.readUint16();
				const uint8_t part = br.readUint8(), parts = br.readUint8();
				const std::string name = br.readStringZ();
				std::string text = br.readStringZ();
				if (file == 0 && part == 0) {
					hostFiles.clear();
					hostFilesComplete.clear();
				}
				if (files > 0) {
					if (part == 0)
						hostFiles[name].clear();
					hostFiles[name] += text;
					if (part + 1 == parts)
						hostFilesComplete.insert(name);
				}
				if (hostFilesComplete.size() == files) {
					// all of them: the game set will take these instead of this computer's own
					auto& folder = g_gameSetFolderOverrides["Warrior Kings Game Set\\AI Generals"];
					folder.clear();
					for (const auto& [n, t] : hostFiles)
						folder[n] = t;
					printf("Client: %u AI General files from the host\n", (unsigned)files);
				}
				break;
			}
			case NETCLIMSG_LOADING_STAGES: {
				readLoadingRows(br);
				break;
			}
			case NETCLIMSG_ENVIRONMENT: {
				const uint8_t flags = br.readUint8();
				EnvironmentSettings& es = environmentSettings;
				es.dayNight = (flags & 1) != 0;
				es.seasons = (flags & 2) != 0;
				es.dayLengthMin = br.readUint16();
				es.startHour = br.readUint8();
				es.seasonLengthMin = br.readUint16();
				es.startSeason = br.readUint8();
				es.tempScale = br.readUint8();
				es.dayShift = br.readFloat();
				es.yearShift = br.readFloat();
				{
					const uint8_t held = br.readUint8();
					environmentWeatherHeld = held == 255 ? -1 : held;
				}
				for (int s = 0; s < 4; s++)
					es.severity[s] = br.readUint8();
				es.skyWeather = br.readUint8();
				es.skyCover = br.readUint8();
				es.weatherStability = br.readUint8();
				es.weatherMinChange = br.readUint8();
				es.weatherMaxChange = br.readUint8();
				es.weatherTypes = br.readUint16();
				environmentWeatherEnds = br.readFloat();
				es.moonPhase = br.readUint8();
				es.moonStart = br.readFloat();
				clearFogOfWar = br.readUint8() != 0;
				{
					const uint8_t cover = br.readUint8();
					es.weatherCover = cover == 255 ? -1 : cover;
				}
				environmentSkyKnown = true;
				environment.read(br);
				environmentActive = es.active();
				nightSight.factor = environment.sightFactor;
				if (terrain) {
					const bool frozen = environmentActive && environment.ice >= ICE_WALKABLE;
					if (frozen != terrain->waterFrozen) {
						terrain->waterFrozen = frozen;
						terrain->revision++;
					}
				}
				break;
			}
			case NETCLIMSG_CHARRED_TREES: {
				if (br.readUint8())
					charredTrees.clear();
				const uint8_t n = br.readUint8();
				for (uint8_t i = 0; i < n; i++)
					charredTrees.insert(br.readUint32());
				break;
			}
			case NETCLIMSG_HOST_DATA: {
				const uint8_t kind = br.readUint8();
				const uint16_t part = br.readUint16(), parts = br.readUint16();
				const uint8_t length = br.readUint8();
				std::string& text = hostDataParts[kind];
				if (part == 0)
					text.clear();
				text += br.readString(length);
				if (part + 1 != parts)
					break;
				if (kind == 1) {
					hostRegions = std::move(text);
					hostRegionsSerial++;
				}
				else if (kind == 4) {
					// one of the host's mod files: under HostMods\<mod index>\, with its timestamp
					const size_t a = text.find('\n'), b = a == std::string::npos ? a : text.find('\n', a + 1), c = b == std::string::npos ? b : text.find('\n', b + 1);
					if (c != std::string::npos) {
						const std::string index = text.substr(0, a), path = text.substr(a + 1, b - a - 1);
						const uint64_t time = std::strtoull(text.substr(b + 1, c - b - 1).c_str(), nullptr, 10);
						std::string rel = path;
						std::replace(rel.begin(), rel.end(), '\\', '/');
						if (rel.find("..") == std::string::npos && !rel.empty() && rel[0] != '/' && rel.find(':') == std::string::npos
							&& index.find_first_not_of("0123456789") == std::string::npos) {
							const std::filesystem::path file = g_userPath / "HostMods" / PathFromUtf8(index) / PathFromUtf8(rel);
							std::error_code ec;
							std::filesystem::create_directories(file.parent_path(), ec);
							std::ofstream(file, std::ios::binary).write(text.data() + c + 1, (std::streamsize)(text.size() - c - 1));
							SetFileTimeOf(file, time);
						}
					}
				}
				else if (kind == 3) {
					sessionMods = std::move(text);
					sessionModsReady = true;
				}
				else if (kind == 2 && terrain) {
					// the host's water design: the renderers make the water again if it differs
					const std::string own = terrain->waterDesign ? terrain->waterDesign->toText() : std::string();
					if (text != own) {
						std::string error;
						auto design = std::make_shared<WaterDesign>();
						if (text.empty())
							terrain->waterDesign.reset();
						else if (design->fromText(text, error))
							terrain->waterDesign = design;
						else
							printf("Client: the host's water design can't be read: %s\n", error.c_str());
						if (cliInterface)
							cliInterface->waterDesignChanged = true;
						printf("Client: the host's water design differs from ours, taken (%zu bytes)\n", text.size());
					}
				}
				hostDataParts.erase(kind);
				break;
			}
			case NETCLIMSG_JOIN_REFUSED: {
				refusedReason = br.readStringZ();
				printf("Client: the host turned us away: %s\n", refusedReason.c_str());
				break;
			}
			case NETCLIMSG_SEATS: {
				const uint8_t part = br.readUint8(), parts = br.readUint8();
				if (part == 0)
					seatsParts.clear();
				seatsParts += br.readStringZ();
				if (part + 1 == parts) {
					seats = std::move(seatsParts);
					awaitingSeat = true;
				}
				break;
			}
			case NETCLIMSG_SPECTATOR: {
				spectator = br.readUint8() != 0;
				awaitingSeat = false;
				printf("Client: watching the game\n");
				break;
			}
			case NETCLIMSG_LOBBY_STATE: {
				const uint8_t part = br.readUint8(), parts = br.readUint8();
				if (part == 0)
					lobbyStateParts.clear();
				lobbyStateParts += br.readStringZ();
				if (part + 1 == parts)
					lobbyState = std::move(lobbyStateParts);
				break;
			}
			case NETCLIMSG_BUILDING_SPAWNED_UNIT_ORDER_CHANGED: {
				auto [objectId, commandIndex, targetId, destination, faceTo] = br.readValues<uint32_t, uint32_t, uint32_t, Vector3, Vector3>();
				ClientGameObject* obj = findObject(objectId);
				if (!obj)
					break;
				if (!(commandIndex >= 0 && commandIndex < gameSet->commands.size()))
					break;
				obj->spawnedUnitCommand = commandIndex;
				obj->spawnedUnitTarget = targetId;
				obj->spawnedUnitDestination = destination;
				obj->spawnedUnitFaceTo = faceTo;
				break;
			}
			}
		}
		dbgNumMessagesInCurrentSec += dbgNumMessagesPerTick;
	}
}

void Client::sendLobbyHello(const std::string& name, const std::string& password) {
	NetPacketWriter packet(NETSRVMSG_LOBBY_HELLO);
	packet.writeStringZ(name.substr(0, 100));
	if (!password.empty())
		packet.writeStringZ(password.substr(0, 100));
	serverLink->send(packet);
}

void Client::sendModManifest() {
	const std::string text = ModManifest();
	const size_t PART = 240;
	const size_t parts = std::max<size_t>(1, (text.size() + PART - 1) / PART);
	for (size_t i = 0; i < parts && i < 65535; i++) {
		const std::string piece = text.substr(i * PART, PART);
		NetPacketWriter packet(NETSRVMSG_MOD_MANIFEST);
		packet.writeUint16((uint16_t)i);
		packet.writeUint16((uint16_t)parts);
		packet.writeUint8((uint8_t)piece.size());
		packet.writeString(piece);
		serverLink->send(packet);
	}
}

void Client::sendTakeSeat(uint32_t playerId) {
	NetPacketWriter packet(NETSRVMSG_TAKE_SEAT);
	packet.writeUint32(playerId);
	serverLink->send(packet);
}

void Client::sendLobbyRequest(const std::string& request) {
	NetPacketWriter packet(NETSRVMSG_LOBBY_REQUEST);
	packet.writeStringZ(request);
	serverLink->send(packet);
}

void Client::sendMessage(const std::string &msg) {
	NetPacketWriter packet(NETSRVMSG_TEST);
	packet.writeStringZ(msg);
	serverLink->send(packet);
}

void Client::sendCommand(ClientGameObject * obj, const Command * cmd, int assignmentMode, ClientGameObject *target, const Vector3 & destination, const Vector3& faceTo)
{
	NetPacketWriter packet(NETSRVMSG_COMMAND);
	packet.writeUint32(cmd->id);
	packet.writeUint32(obj->id);
	packet.writeUint8(assignmentMode);
	packet.writeUint32(target ? target->id : 0);
	packet.writeVector3(destination);
	packet.writeVector3(faceTo);
	serverLink->send(packet);
}

// NETCLIMSG_LOADING_STAGES after its type: the count (its top bit: the host waits for the players'
// loads), then each client's percentage, colour, whether it is this one, name
void Client::readLoadingRows(BinaryReader& br)
{
	const uint8_t countByte = br.readUint8();
	const uint8_t count = countByte & 0x7f;
	loadingWaitForOthers = (countByte & 0x80) != 0;
	loadingRows.clear();
	for (uint8_t k = 0; k < count; k++) {
		LoadingRow row;
		row.stage = br.readUint8();
		row.colour = br.readUint8();
		row.own = br.readUint8() != 0;
		row.name = br.readStringZ();
		loadingRows.push_back(std::move(row));
	}
	// (each row's percentage: printed when one crosses a quarter, or the rows change)
	std::string rows, quarters;
	for (const LoadingRow& r : loadingRows) {
		rows += " [" + r.name + (r.own ? " (own)" : "") + " colour " + std::to_string(r.colour) + " percent " + std::to_string(r.stage) + "]";
		quarters += r.name + std::to_string(r.colour) + ":" + std::to_string(r.stage / 25) + ";";
	}
	static std::string printedQuarters;
	if (quarters != printedQuarters) {
		printedQuarters = quarters;
		printf("LOADING rows:%s\n", rows.c_str());
	}
}

// The newest rows waiting in the queue, read now (the loading frames, while this client is busy
// loading and doesn't read its messages); the queue keeps them, read again in order later
void Client::peekLoadingRows()
{
	std::string data;
	if (!serverLink || !serverLink->peekLast(NETCLIMSG_LOADING_STAGES, data))
		return;
	BinaryReader br(data.c_str());
	br.readUint8(); // (the type)
	readLoadingRows(br);
}

void Client::sendLoadingStage(int stage)
{
	if (!serverLink)
		return;
	NetPacketWriter packet(NETSRVMSG_LOADING_STAGE);
	packet.writeUint8((uint8_t)stage);
	serverLink->send(packet);
}

void Client::sendPauseRequest(uint8_t pauseState)
{
	NetPacketWriter packet(NETSRVMSG_PAUSE);
	packet.writeUint8(pauseState);
	serverLink->send(packet);
}

void Client::sendStampdown(const GameObjBlueprint * blueprint, ClientGameObject * player, const Vector3 & position, bool sendEvent, bool inGameplay,
	const Vector3 & orientation)
{
	NetPacketWriter msg(NETSRVMSG_STAMPDOWN);
	msg.writeUint32(blueprint->getFullId());
	msg.writeUint32(player->id);
	msg.writeVector3(position);
	uint8_t flags = 0;
	flags |= sendEvent ? 1 : 0;
	flags |= inGameplay ? 2 : 0;
	msg.writeUint8(flags);
	// last, so that recordings from before (without it) still read
	msg.writeVector3(orientation);
	serverLink->send(msg);
}

void Client::sendStartLevelRequest()
{
	serverLink->send(NetPacketWriter(NETSRVMSG_START_LEVEL));
}

void Client::sendGameTextWindowButtonClicked(int gtwIndex, int buttonIndex)
{
	NetPacketWriter msg(NETSRVMSG_GAME_TEXT_WINDOW_BUTTON_CLICKED);
	msg.writeUint32(gtwIndex);
	msg.writeUint32(buttonIndex);
	serverLink->send(msg);
}

void Client::sendCameraPathEnded(int camPathIndex)
{
	NetPacketWriter msg{ NETSRVMSG_CAMERA_PATH_ENDED };
	msg.writeUint32(camPathIndex);
	serverLink->send(msg);
}

void Client::sendGameSpeedChange(float nextSpeed)
{
	NetPacketWriter msg{ NETSRVMSG_CHANGE_GAME_SPEED };
	msg.writeFloat(nextSpeed);
	serverLink->send(msg);
}

void Client::sendTerminateObject(ClientGameObject* obj)
{
	NetPacketWriter msg{ NETSRVMSG_TERMINATE_OBJECT };
	msg.writeUint32(obj->id);
	serverLink->send(msg);
}

void Client::sendBuildLastStampdownedObject(ClientGameObject* obj, int assignmentMode)
{
	NetPacketWriter msg{ NETSRVMSG_BUILD_LAST_STAMPDOWNED_OBJECT };
	msg.writeUint32(obj->id);
	msg.writeUint32(assignmentMode);
	serverLink->send(msg);
}

void Client::sendCancelCommand(ClientGameObject* obj, const Command* cmd)
{
	NetPacketWriter msg{ NETSRVMSG_CANCEL_COMMAND };
	msg.writeValues(obj->id, cmd->id);
	serverLink->send(msg);
}

void Client::sendEnvironmentDebug(const std::string& name, float value)
{
	NetPacketWriter msg{ NETSRVMSG_ENVIRONMENT_DEBUG };
	msg.writeFloat(value);
	msg.writeStringZ(name);
	serverLink->send(msg);
}

void Client::sendPutUnitIntoNewFormation(ClientGameObject* obj)
{
	NetPacketWriter msg{ NETSRVMSG_PUT_UNIT_INTO_NEW_FORMATION };
	msg.writeValues(obj->id);
	serverLink->send(msg);
}

void Client::sendCreateNewFormation()
{
	NetPacketWriter msg{ NETSRVMSG_CREATE_NEW_FORMATION };
	serverLink->send(msg);
}

void Client::sendSetBuildingSpawnedUnitOrderToTarget(ClientGameObject* obj, int commandIndex, ClientGameObject* target)
{
	NetPacketWriter msg{ NETSRVMSG_SET_BUILDING_SPAWNED_UNIT_ORDER_TO_TARGET };
	msg.writeValues(obj->id, commandIndex, target->id);
	serverLink->send(msg);
}

void Client::sendSetBuildingSpawnedUnitOrderToDestination(ClientGameObject* obj, int commandIndex, Vector3 destination, Vector3 faceTo)
{
	NetPacketWriter msg{ NETSRVMSG_SET_BUILDING_SPAWNED_UNIT_ORDER_TO_DESTINATION };
	msg.writeValues(obj->id, commandIndex, destination, faceTo);
	serverLink->send(msg);
}

ClientGameObject * Client::createObject(const GameObjBlueprint * blueprint, uint32_t id)
{
	ClientGameObject *obj = new ClientGameObject(id, blueprint);
	assert(idmap[id] == nullptr);
	idmap[id] = obj;
	return obj;
}

void ClientGameObject::updatePosition(const Vector3& newposition)
{
	// TODO: Generalize with server equivalent instead of duplicating code
	Client* prog = Client::instance;
	Vector3 oldposition = position;
	position = newposition;
	if (prog->tiles) {
		int trnNumX, trnNumZ;
		std::tie(trnNumX, trnNumZ) = prog->terrain->getNumPlayableTiles();
		int tx = static_cast<int>(position.x / 5.0f), tz = static_cast<int>(position.z / 5.0f);
		int newtileIndex;
		if (tx >= 0 && tx < trnNumX && tz >= 0 && tz < trnNumZ)
			newtileIndex = tz * trnNumX + tx;
		else
			newtileIndex = -1;
		if (newtileIndex != tileIndex) {
			int prevtileIndex = tileIndex;
			tileIndex = newtileIndex;
			if (prevtileIndex != -1) {
				auto& vec = prog->tiles[prevtileIndex].objList;
				vec.erase(std::find(vec.begin(), vec.end(), this->id));
			}
			if (newtileIndex != -1) {
				prog->tiles[newtileIndex].objList.push_back(this->id);
			}
			updateOccupiedTiles(oldposition, orientation, newposition, orientation);
		}
	}
}

void ClientGameObject::updateOccupiedTiles(const Vector3& oldposition, const Vector3& oldorientation, const Vector3& newposition, const Vector3& neworientation)
{
	Client::instance->updateOccupiedTiles(this, oldposition, oldorientation, newposition, neworientation);
}
