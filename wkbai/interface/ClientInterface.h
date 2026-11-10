// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#pragma once

#include "ClientDebugger.h"
#include "../scene.h"
#include <ctime>
#include "../GameObjectRef.h"
#include "../Language.h"
#include "FogOfWar.h"
#include "GameChronicle.h"
#include "ReplayView.h"
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
struct WndCursor;

// The commands the selected objects offer on a target (the hovered object, nullptr: the ground)
// whose cursor shows, in the order they are offered, each with its cursor. The first is the
// hover cursor's and the right-click's (Tab steps through the others).
std::vector<std::pair<WndCursor*, Command*>> FindCursorCommands(Client* client, const std::vector<ClientGameObject*>& selection, ClientGameObject* target);
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
	// A frame with the loading screen alone (LoadingProgress's pump, and while the local server
	// loads): drawn from the work that blocks the main thread, between frames
	void loadingFrame();
	void render();

	void updateTerrain();
	// playing a recording: everything shown (no fog of war), the commands ignored by the server,
	// no command panels; its playback controls and its events on the screen and the minimap.
	// replayServer: the server playing it (in this program), for the controls
	bool replayMode = false;
	Server* replayServer = nullptr;
	ReplayView replayView;
	// heights changed by the server (buildings levelling the ground): the terrain renderer is
	// refreshed, at most every 250 ms
	bool terrainHeightsChanged = false;
	// the host's water design came (Client, NETCLIMSG_HOST_DATA): the water made again
	bool waterDesignChanged = false;
	uint64_t terrainHeightsRefreshed = 0;

	ClientInterface(Client *client, IRenderer *gfx) : client(client), gfx(gfx), terrainRenderer(nullptr),
		sceneRenderer(nullptr), debugger(client, this), scene(nullptr), particlesContainer(nullptr), psCache(nullptr),
		particleRenderer(nullptr), uiTexCache(gfx), terrainSpriteContainer(nullptr), terrainSpriteRenderer(nullptr) {}

	const GameObjBlueprint *stampdownBlueprint = nullptr;
	// the walls the building being placed would take down (its plan's toRemove, last frame's):
	// not drawn while it is placed, as in WKB.exe
	std::vector<uint32_t> stampdownHidden;
	// the refusal shown at the cursor while placing (the original's red box), empty: none
	std::string stampdownRefusal;
	void drawStampdownRefusal();
	CliGORef stampdownPlayer;
	// the building being placed turned by quarters (0 to 3), with [ and ] as in WKB.exe; back to
	// 0 on every pick, the same building again too, as in WKB.exe (0x6535da); stampdownTurnedFor
	// (the one it was turned for) also catches a pick made elsewhere
	int stampdownQuarter = 0;
	const GameObjBlueprint* stampdownTurnedFor = nullptr;
	Vector3 stampdownOrientation() const;
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
	// F8 turns the map editor on and off: only when the game was started as the map editor (the
	// menu's Map Editor), not in a game or a replay
	bool editorAllowed = false;
	// Runs code on the server while its thread is held (set by the game's owner; without it, e.g.
	// as a multiplayer guest, there is no editor)
	std::function<void(const std::function<void(Server&)>&)> withServer;
	// The keys that amount to cheating (F1 the server's window, F2 the client's debugger, F9 the
	// environment's panel): in single player with the game setting cheat mode on only (the lobby's,
	// kept by the save game), in the map editor, or in a test run; never in a replay
	bool cheatKeysAllowed() const;
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
	// the Water mode's water design (WaterDesign.h): its panel, and the renderers' water made
	// again from it
	void editorWaterDesign();
	void applyWaterDesign();
	// the Regions mode: the timeline's named map regions (chronicle.regions)
	void editorRegions();
	void editorBlankMap();
	// a new map of another size (playable tiles), saved under the name and opened in the editor
	bool editorNewMap(const std::string& name, int width, int height);
	// the terrain renderer updated after an edit of the heights (or of the tiles), or made again
	void rebuildTerrainRenderer(bool tiles = false);
	// a new terrain renderer for client->terrain, with Extend map as set (not in the map editor)
	TerrainRenderer* makeTerrainRenderer();
	int wantedExtendTiles() const;
	int wantedFarRings() const;
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
	// what the player's units see (darker elsewhere, enemy characters hidden there)
	FogOfWar fogOfWar;
	// the peasant activity interface over the minimap: -1 closed, 0 the idle bar, 1 all five
	// (benchmark "peasantActivity" sets it for screenshots)
	int peasantActivityMode = -1;
	// the top bar's Settings button asked for the in-game menu (opened the next frame)
	bool openMenuRequested = false;
	bool fogOfWarOn() const;
	// the original's loading screen over everything until the level has been drawn for a while:
	// one of Loading Screen 1-9 (random, as WKB.exe 0x706300), the player's stage of 8 with its
	// bar, and a tip ("Words of Wisdom")
	// (countFrame: a game frame, counted toward the first frames; not a loadingFrame)
	void drawLoadingScreen(bool countFrame = true);
	// the loading screen's test screenshots ("benchmark": { "loadingScreenshots": [percent, ...] })
	void loadingScreenshots();
	size_t loadingShotsTaken = 0;
	bool languageLoaded = false; // (the first frame's, or the first loading frame's)
	// the level's models and their renderer made before the frame (with the loading screen
	// moving): the model shaders, the game set's models, textures and GPU buffers
	void prepareLevelBeforeFrame();
	// the game's statistics (F6) and its timeline of events (F7)
	GameChronicle chronicle;
	bool statisticsOpen = false, timelineOpen = false;
	// the environment's debug panel (F9, single player): day and night, seasons, weather by hand
	bool environmentDebugOpen = false;
	void drawEnvironmentDebug();
	bool frameTimingsOpen = false; // the frame-time window (Ctrl+F12)
	void drawFrameTimings();
	int statisticsShown = 0; // the graph: food, wood, gold, peasants, soldiers, buildings
	void drawChronicle();
	int selectPeasantsByActivity(int row);
	// control groups 0-9 (Ctrl+digit stores, digit selects, twice centres the camera); per game
	std::unordered_set<CliGORef> controlGroups[10];
	int lastGroupRecalled = -1;
	uint64_t lastGroupRecallTicks = 0;
	void controlGroupKeys();
	// the selected (or hovered) character in the selection window, turning (after the HUD)
	void drawSelectionPortrait();
	// the lower left panel, its stained-glass window as refracting glass with enhanced graphics
	void drawStainedGlassPanel(int x, int y, int w, int h);
	std::vector<void*> glassFrames;
	const void* loadingFor = nullptr; // the level it was shown for (nullptr: before any)
	int loadingScreenIndex = 0, loadingTip = 0, framesWithLevel = 0;
	int loadingStage = 0;
	int loadingPercentSent = -1; // (to the server, for the other players' loading screens)
	uint64_t loadingWaitStarted = 0; // (SDL ticks: loaded, waiting for the other players)
	bool loadingDone = false;
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
	// Extend map's trees (ExtendMap.h): the level's own tree models as seen (with their size and
	// season flags), and an entity per tree, made once those are known
	struct ExtTreeModel { Model* model; float scale; uint32_t flags; };
	std::vector<ExtTreeModel> extTreeModels;
	std::vector<SceneEntity> extTreeEntities;
	const void* extTreesFor = nullptr; // the tree list and model count the entities were made for
	size_t extTreesModels = 0;
	void drawExtendedTrees();
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
	// the fog of war over the minimap, and the FogOfWar::version in it
	void* minimapFogTexture = nullptr;
	uint32_t minimapFogVersion = ~0u, minimapFogWidth = 0, minimapFogHeight = 0;
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
	// This frame's footprint of the building being placed: each tile (playable tile coordinates)
	// red where it can't be built on, green where it can, on the ground as in the original
	struct FootprintTile {
		int x, z;
		bool ok;
	};
	std::vector<FootprintTile> footprintTiles;
	RBatch* footprintBatch = nullptr;
	void drawFootprintTiles();
	uint64_t orderFlagStartTicks = 0; // SDL ticks (ms): a UI marker, independent of the game clock
	bool orderFlagActive = false;
	bool orderFlagRally = false;
	Vector3 orderFlagPosition;
	std::vector<CliGORef> orderFlagUnits; // the units sent there; the flag goes when they arrive
	void applyCameraLimits();
	// The four mode buttons around the minimap, at the top-left corner of Bottom_Right_Panel.tga
	bool drawMinimapButtons(int panelX, int panelY);
	uint8_t minimapHiddenCategories = 0; // GameObjBlueprint::MINIMAP_* toggled off

	// remembered: a copy from FogOfWar::remembered, drawn as the building was last seen
	void drawObject(ClientGameObject* obj, bool remembered = false);
	// Loads every object's models and textures and creates their GPU resources once per level,
	// so that the first look at a part of the map doesn't stutter.
	void prewarmLevel();
	const void* prewarmedLevel = nullptr;
	std::unordered_set<Model*> prewarmedModels;
	int prewarmCountdown = 0;
	void drawAttachmentPoints(SceneEntity* sceneEntity, uint32_t objid = 0);
};