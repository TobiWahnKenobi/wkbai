// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#pragma once

#include "ClientDebugger.h"
#include "../scene.h"
#include <ctime>
#include "../GameObjectRef.h"
#include "../Language.h"
#include <deque>
#include <unordered_set>

struct Client;
struct TerrainRenderer;
struct IRenderer;
struct SceneRenderer;
struct ClientGameObject;
struct GameObjBlueprint;
struct ParticleContainer;
struct PSCache;
struct ParticleRenderer;
struct TerrainSpriteContainer;
struct TerrainSpriteRenderer;

struct ClientInterface {
	Client *client;
	IRenderer *gfx;
	TerrainRenderer *terrainRenderer;
	SceneRenderer *sceneRenderer;
	struct DiligentSkyRenderer* skyRenderer = nullptr;
	ClientDebugger debugger;
	Scene *scene;
	ParticleContainer* particlesContainer;
	PSCache* psCache;
	ParticleRenderer* particleRenderer;
	TerrainSpriteContainer* terrainSpriteContainer;
	TerrainSpriteRenderer* terrainSpriteRenderer;

	void iter();
	void render();

	void updateTerrain();

	ClientInterface(Client *client, IRenderer *gfx) : client(client), gfx(gfx), terrainRenderer(nullptr),
		sceneRenderer(nullptr), debugger(client, this), scene(nullptr), particlesContainer(nullptr), psCache(nullptr),
		particleRenderer(nullptr), uiTexCache(gfx), terrainSpriteContainer(nullptr), terrainSpriteRenderer(nullptr) {}

	const GameObjBlueprint *stampdownBlueprint = nullptr;
	CliGORef stampdownPlayer;
	bool sendStampdownEvent = false;
	bool stampdownFromCommand = false;
	std::unordered_set<CliGORef> selection;
	TextureCache uiTexCache;
private:
	int camrotoffx, camrotoffy;
	float camrotorix, camrotoriy;
	bool camroton = false;
	int numObjectsDrawn = 0;
	int framesPerSecond = 0, nextFps = 0;
	time_t lastfpscheck = 0;
	Vector3 rayDirection;
	CliGORef nextSelectedObject;
	CliGORef nextSelectedObjectByMesh, nextSelectedObjectBySphere;
	float nextSelObjDistanceByMesh = 0.0f, nextSelObjDistanceBySphere = 0.0f;
	Language lang;
	std::deque<SceneEntity> attachSceneEntities;
	bool selBoxOn = false;
	int selBoxStartX, selBoxStartY, selBoxEndX, selBoxEndY;
	std::vector<CliGORef> nextSelFromBox;
	bool devMode = false;
	std::unordered_set<ClientGameObject*> highlightedObjects;

	// Minimap: terrain image of the current map, created on first use
	void* minimapTexture = nullptr;
	const void* minimapTerrain = nullptr;
	// Draws the minimap into the given screen rectangle (called from the HUD script);
	// returns true while the mouse is over it, a left click/drag moves the camera there
	bool drawMinimap(int x, int y, int w, int h);
	void drawSettingsWindow(bool* open);
	void applyCameraLimits();
	// The four mode buttons around the minimap, at the top-left corner of Bottom_Right_Panel.tga
	bool drawMinimapButtons(int panelX, int panelY);
	uint8_t minimapHiddenCategories = 0; // GameObjBlueprint::MINIMAP_* toggled off

	void drawObject(ClientGameObject* obj);
	void drawAttachmentPoints(SceneEntity* sceneEntity, uint32_t objid = 0);
};