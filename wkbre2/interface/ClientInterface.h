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
#include <functional>
#include <memory>

struct Server;
#include <unordered_set>

struct Client;
struct TerrainRenderer;
struct IRenderer;
struct SceneRenderer;
struct ClientGameObject;
struct GameObjBlueprint;
struct Command;
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

	// Set by the game's owner: the in-game menu pauses a single-player game
	bool singlePlayer = false;
	// The player chose to leave the game from the in-game menu: "exit_to_menu", "restart" or
	// "start" (load the save game leaveArg); the game loop then ends
	std::string leaveAction, leaveArg;
	bool wantsToLeave() const { return !leaveAction.empty(); }
	// Set by the game's owner when it can save the game (single player): saves it under a name
	// ("Save_Games\\<name>.sav" in the user folder), returns whether it worked
	std::function<bool(const std::string& name)> saveGame;
	// The map editor (interface/MapEditor.cpp): F8 in single player, or on from the start when the
	// menu's Map Editor opens a level
	bool editorOn = false;
	// Runs code on the server while its thread is held (set by the game's owner; without it, e.g.
	// as a multiplayer guest, there is no editor)
	std::function<void(const std::function<void(Server&)>&)> withServer;
private:
	bool firstFrame = true;
	// the map editor: its state, its mouse input (before the game's; true when it used the
	// click), its window, and the terrain shown again after its heights changed
	struct EditorState;
	std::shared_ptr<EditorState> editor;
	bool editorInput(const Vector3& cursor);
	// test runs: "benchmark": { ..., "editorTest": true } paints, places and saves by itself
	void editorTestStep(Vector3& cursor);
	// "benchmark": { "editorMode": "players" }: the editor open in that mode (for screenshots)
	void editorOpenMode(const std::string& mode);
	bool editorSave(const std::string& name);
	// for the original game: the map saved, then saved\Export\<name>.bcp (the map, and the level
	// as replaceLevel, a built-in skirmish level's file, unless empty) and <name>.sav (Load Game)
	bool editorExport(const std::string& name, const std::string& replaceLevel);
	void editorUndo();
	void editorRedo();
	// one step taken back (or done again): the state it replaces goes onto the other list
	void editorStep(bool redo);
	// the Objects mode's Select tool: the selection grabbed to follow the cursor, turned, or
	// removed (undoably)
	void editorGrab();
	void editorTurn(float degrees);
	void editorDeleteSelection();
	// one selected object's name, owner and items, changed on the server (undoably)
	void editorProperties();
	// the Scripts mode: the level's game set file (its scripts) and the objects in each alias
	void editorScripts();
	// what may keep the level from playing: players without a start camera or without objects
	std::vector<std::string> editorCheck();
	void editorWindow();
	void editorZones();
	void editorBlankMap();
	// a new map of another size (playable tiles), saved under the name and opened in the editor
	bool editorNewMap(const std::string& name, int width, int height);
	// the terrain renderer updated after an edit of the heights (or of the tiles), or made again
	void rebuildTerrainRenderer(bool tiles = false);
	// the lakes set on the client and the server, their water filled again and shown (the map editor)
	void setLakes(const std::vector<Vector3>& lakes, const std::vector<uint8_t>& bits);
	// The menu's "save" request: saves (saveGame) and tells the menu
	void saveRequested(const std::string& name);
	// A right-click command to the selection (units spread around the place, buildings' rally
	// points); faceTo: where the units face at the end (x < 0: nowhere)
	void sendRightClickCommand(Command* rightClickCommand, ClientGameObject* nextSelectedObject, const Vector3& peapos, const Vector3& faceTo);
	// a formation's right-drag: the move waits for the button's release, facing the drag
	struct FormationDrag {
		bool active = false;
		Vector3 start;
		Command* command = nullptr;
	} formationDrag;
	static constexpr float FORMATION_DRAG_MIN = 3.0f;
	// Multiplayer chat: Enter opens a line to type, Enter sends it to everyone, Esc closes it; the
	// messages show for a while at the top left
	void chatFrame(); // the keys and the messages
	void chatDraw();
	bool chatOpen = false, chatFocus = false;
	char chatInput[200] = {};
	size_t chatSeen = (size_t)-1; // client->chatMessages shown so far (at first: the lobby's, not shown)
	std::vector<std::pair<std::string, uint32_t>> chatLines; // a message, when it came (SDL ticks)
	bool menuPausedGame = false;
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
	std::unordered_map<const AttachmentPoint*, Model*> attachmentModels; // the models of attachment points with a path
	std::vector<float>* gpuBlobShadows = nullptr; // this frame's blob shadows for the enhanced terrain renderer
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
	// The original's marker at the ground target of a right-click order
	void showOrderFlag(const Vector3& position, bool rallyPoint);
	bool orderFlagDone(float age) const;
	SceneEntity orderFlagEntity;
	std::deque<SceneEntity> rallyFlagEntities; // of the selected buildings, this frame
	// This frame's range circles: a band texture repeated around a circle on the ground
	struct RangeBand {
		void* tex;
		Vector3 centre;
		float radius;
	};
	std::vector<RangeBand> rangeBands;
	RBatch* rangeBandBatch = nullptr;
	void drawRangeBands();
	uint64_t orderFlagStartTicks = 0; // SDL ticks (ms): a UI marker, independent of the game clock
	bool orderFlagActive = false;
	bool orderFlagRally = false;
	Vector3 orderFlagPosition;
	std::vector<CliGORef> orderFlagUnits; // the units sent there; the flag goes when they arrive
	void applyCameraLimits();
	// The four mode buttons around the minimap, at the top-left corner of Bottom_Right_Panel.tga
	bool drawMinimapButtons(int panelX, int panelY);
	uint8_t minimapHiddenCategories = 0; // GameObjBlueprint::MINIMAP_* toggled off

	void drawObject(ClientGameObject* obj);
	// Loads every object's models and textures and creates their GPU resources once per level,
	// so that the first look at a part of the map doesn't stutter.
	void prewarmLevel();
	const void* prewarmedLevel = nullptr;
	std::unordered_set<Model*> prewarmedModels;
	int prewarmCountdown = 0;
	void drawAttachmentPoints(SceneEntity* sceneEntity, uint32_t objid = 0);
};