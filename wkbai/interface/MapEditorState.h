// wkbai - the map editor's state (interface/MapEditor.cpp), shared with its live author ops
// (interface/MapEditorAuthor.cpp, step 17.8)
// Licensed under the GNU General Public License 3

#pragma once

#include "ClientInterface.h"
#include "../server.h"
#include "../terrain.h"
#include "../tags.h"
#include "../WaterDesign.h"
#include "../Environment.h"
#include "../gameset/gameset.h"
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

struct ClientInterface::EditorState {
	enum Mode { TERRAIN, TEXTURES, WATER, OBJECTS, PLAYERS, ZONES, SCRIPTS, REGIONS };
	// regions (the timeline's named map regions, MapRegions): the one chosen, the tool (paint its
	// cells, or pick the region under the cursor) and the kind a new one gets
	int regionId = 0, regionTool = 0, newRegionKind = 0;
	bool regionStroke = false;
	// scripts: the alias chosen (the game set's DECLARE_ALIAS names, which a level's scripts
	// find objects by), its filter, and the game set file being typed
	int alias = -1;
	char aliasFilter[64] = {};
	char gameSetBuf[260] = {};
	bool gameSetRead = false;
	// zones (TERRAIN_ZONE objects, which triggers and finders use): the one being painted, by
	// id, and whether the brush adds tiles to it or takes them away
	uint32_t zoneId = 0;
	enum ZoneTool { ADD_TILES, REMOVE_TILES };
	int zoneTool = ADD_TILES, zoneType = 0;
	// water: a click adds a lake (its point, the water level that much above the ground there) or
	// removes the lake whose water is under the cursor; or the water design's tools (the
	// surface brush, the waterways, the placed waterfalls)
	enum WaterTool { ADD_LAKE, REMOVE_LAKE, PAINT_SURFACE, DRAW_WATERWAY, PLACE_WATERFALL };
	int waterTool = ADD_LAKE;
	float waterDepth = 3.0f; // metres
	// the surface brush: 0 found automatically, 1 river, 2 lake, 3 sea of surfaceRegion
	int surfaceKind = 3, surfaceRegion = 0;
	// the waterway points are added to (-1: a new one), a waterfall's first point
	int waterway = -1;
	bool fallStarted = false;
	Vector3 fallFrom;
	bool designStroke = false;
	enum Tool { RAISE, LOWER, SMOOTH, FLATTEN };
	int mode = TERRAIN, tool = RAISE;
	// texture painting: the texture (group and id), its turn (0-3, quarter turns) and flips;
	// "random turn" gives each tile its own, which hides the repeats
	std::string paintGroup;
	int paintId = -1, paintTurn = 0;
	bool paintFlipX = false, paintFlipZ = false, paintRandomTurn = false;
	// the map's latitude (map.json environment.latitude): the map's own when it has one, else
	// Britain's; set on the server (the sun and the day's length) and written into map.json on Save
	float latitude = MapLatitudeBritain;
	bool latitudeRead = false;
	// the sun's preview: day and night and the seasons on while the editor shows the latitude's
	// day (the settings before it put back when it ends); its hour and season
	bool sunPreview = false, previewDayNight = false, previewSeasons = false;
	float previewHour = 12.0f;
	int previewSeason = 1;
	float radius = 20.0f;   // metres
	float strength = 20.0f; // height steps (map units) per second at the centre
	float flattenTo = 0.0f; // the height a flatten stroke keeps (from where it began)
	// heights finer than the BCM's whole steps, kept in the map's .wkbmap (the original sees the
	// whole steps)
	bool fineHeights = true;
	bool stroke = false;
	Vector3 strokeMin, strokeMax; // the area the stroke changed (objects there get the new heights)
	std::vector<float> pending; // per vertex, the part of a step not applied yet
	uint32_t lastApply = 0, lastRebuild = 0;
	bool terrainChanged = false;

	char filter[64] = {};
	int placeClass = Tags::GAMEOBJCLASS_BUILDING;
	int placeType = -1;
	uint32_t placePlayer = 0;
	float placeYaw = 0.0f;

	char saveName[64] = "My map";
	// a new map of another size: its playable tiles (from the open map's at first)
	int newWidth = 128, newHeight = 128;
	bool newSizeSet = false;
	// the export for the original: the built-in skirmish levels ("N Player ...") and the one the
	// exported level replaces (0: none)
	std::vector<std::string> builtinLevels;
	int exportReplace = 0;
	std::string status;
	std::vector<std::string> warnings;

	// the Objects mode's tool: place new ones, or select (the game's clicks) and move or turn them
	enum ObjectTool { PLACE, SELECT };
	int objectTool = PLACE;
	// moving the selection (G): it follows the cursor from where the grab began until a left
	// click keeps it there or a right click puts it back
	bool grabbing = false;
	Vector3 grabFrom;
	uint32_t lastGrabMove = 0;

	// undo and redo: a stroke's heights or tiles before it, the lakes, objects placed (undone by
	// removing them), objects removed (undone by placing them again) or objects moved or turned
	struct TileState {
		TerrainTexture* texture;
		uint8_t rot, xflip, zflip;
	};
	struct Removed {
		const GameObjBlueprint* type;
		uint32_t id, parent;
		Vector3 position, orientation;
	};
	struct Moved {
		uint32_t id;
		Vector3 position, orientation;
	};
	// an object's name, owner and items, as the properties panel found them before an edit
	struct Props {
		uint32_t id = 0, parent = 0;
		std::string name;
		std::map<int, float> items;
	};
	struct Undo {
		std::vector<uint8_t> heights, fractions; // fractions: as Terrain::heightFraction (empty: none)
		std::vector<uint32_t> placed;
		std::vector<TileState> tiles;
		bool hasLakes = false;
		std::vector<Vector3> lakes;
		std::vector<uint8_t> lakeBits;
		// the water design before a change (null: none)
		bool hasDesign = false;
		std::shared_ptr<WaterDesign> design;
		// the map's regions before a change
		bool hasRegions = false;
		std::shared_ptr<MapRegions> regions;
		std::vector<Removed> removed;
		std::vector<Moved> moved;
		std::vector<Props> props;
		// an alias's members before a change (alias index, object ids)
		std::vector<std::pair<int, std::vector<uint32_t>>> aliasMembers;
	};
	// the properties panel: the object's state when an edit began, the name being typed, and
	// the item chosen to add
	Props editStart;
	uint32_t nameOf = 0;
	char nameBuf[128] = {};
	char itemFilter[64] = {};
	int addItem = -1;
	std::vector<Undo> undo, redo;
	static constexpr size_t MAX_UNDO = 30;
	void pushUndo(Undo u) {
		undo.push_back(std::move(u));
		if (undo.size() > MAX_UNDO)
			undo.erase(undo.begin());
		redo.clear();
	}
	std::vector<Moved> grabStart;
	// an object removed and placed again by undo or redo gets a new id: steps that name the old
	// one find the new one through this
	std::unordered_map<uint32_t, uint32_t> respawned;
	static Props readProps(ServerGameObject* obj);
	void applyProps(Server& server, ServerGameObject* obj, const Props& p) const;
	uint32_t resolve(uint32_t id) const {
		for (auto it = respawned.find(id); it != respawned.end(); it = respawned.find(id))
			id = it->second;
		return id;
	}
};
