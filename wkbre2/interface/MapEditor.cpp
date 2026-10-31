// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

// The map editor: terrain heights (raise, lower, smooth, flatten), placing objects of any type
// for any player, and saving the result as a map (.bcm, the original's format: everything but the
// heights stays as in the map it came from) and a level (.lvl) in the user folder, where the
// skirmish menu finds it. F8 switches it on and off in single player.

#include "ClientInterface.h"
#include "../MovementController.h"
#include "../client.h"
#include "../server.h"
#include "../terrain.h"
#include "../NNSearch.h"
#include "../file.h"
#include "../window.h"
#include "../tags.h"
#include "../gameset/gameset.h"
#include "../gfx/renderer.h"
#include "../gfx/TerrainRenderer.h"
#include "../gfx/DefaultTerrainRenderer.h"
#include "../gfx/DiligentEnhancedTerrainRenderer.h"
#include "../imgui/imgui.h"
#include "../util/StriCompare.h"
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_scancode.h>
#include <SDL3/SDL_timer.h>
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>
#include <fstream>
#include <cctype>

bool UseEnhancedRenderers(IRenderer* gfx);
std::string portablePath(std::string p);

namespace {
	// case-insensitive "contains" (the type filter)
	bool ContainsNoCase(const std::string& text, const std::string& part)
	{
		auto lower = [](std::string s) { for (char& c : s) c = (char)tolower((unsigned char)c); return s; };
		return lower(text).find(lower(part)) != std::string::npos;
	}
// the tiles' terrain types are looked up again (MovementController::isTileBlocked) after painting
static void ResetTileTerrainTypes(Server& server)
{
	if (!server.terrain || !server.tiles)
		return;
	const auto area = server.terrain->getNumPlayableTiles();
	for (int i = 0; i < area.first * area.second; i++)
		server.tiles[i].terrainType = -2;
}


	// The texture of a group and id in this terrain's database (client and server each have one)
	TerrainTexture* FindTexture(Terrain* trn, const std::string& group, int id)
	{
		auto g = trn->texDb.groups.find(group);
		if (g == trn->texDb.groups.end() || !g->second)
			return nullptr;
		auto t = g->second->textures.find(id);
		return t != g->second->textures.end() ? t->second.get() : nullptr;
	}
}

struct ClientInterface::EditorState {
	enum Mode { TERRAIN, TEXTURES, WATER, OBJECTS, PLAYERS, ZONES };
	// zones (TERRAIN_ZONE objects, which triggers and finders use): the one being painted, by
	// id, and whether the brush adds tiles to it or takes them away
	uint32_t zoneId = 0;
	enum ZoneTool { ADD_TILES, REMOVE_TILES };
	int zoneTool = ADD_TILES, zoneType = 0;
	// water: a click adds a lake (its point, the water level that much above the ground there) or
	// removes the lake whose water is under the cursor
	enum WaterTool { ADD_LAKE, REMOVE_LAKE };
	int waterTool = ADD_LAKE;
	float waterDepth = 3.0f; // metres
	enum Tool { RAISE, LOWER, SMOOTH, FLATTEN };
	int mode = TERRAIN, tool = RAISE;
	// texture painting: the texture (group and id), its turn (0-3, quarter turns) and flips;
	// "random turn" gives each tile its own, which hides the repeats
	std::string paintGroup;
	int paintId = -1, paintTurn = 0;
	bool paintFlipX = false, paintFlipZ = false, paintRandomTurn = false;
	float radius = 20.0f;   // metres
	float strength = 20.0f; // height steps (map units) per second at the centre
	float flattenTo = 0.0f; // the height a flatten stroke keeps (from where it began)
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
		std::vector<uint8_t> heights;
		std::vector<uint32_t> placed;
		std::vector<TileState> tiles;
		bool hasLakes = false;
		std::vector<Vector3> lakes;
		std::vector<uint8_t> lakeBits;
		std::vector<Removed> removed;
		std::vector<Moved> moved;
		std::vector<Props> props;
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

void ClientInterface::setLakes(const std::vector<Vector3>& lakes, const std::vector<uint8_t>& bits)
{
	Terrain* trn = client->terrain;
	trn->lakes = lakes;
	trn->lakeExtraBits = bits;
	trn->refillWater();
	withServer([&](Server& server) {
		if (server.terrain && server.terrain != trn && server.terrain->width == trn->width && server.terrain->height == trn->height) {
			server.terrain->lakes = lakes;
			server.terrain->lakeExtraBits = bits;
			server.terrain->refillWater();
		}
	});
	if (!terrainRenderer || !terrainRenderer->updateWater())
		rebuildTerrainRenderer(true);
}

void ClientInterface::rebuildTerrainRenderer(bool tiles)
{
	if (terrainRenderer && (tiles ? terrainRenderer->updateTiles() : terrainRenderer->updateHeights()))
		return;
	delete terrainRenderer;
	if (UseEnhancedRenderers(gfx))
		terrainRenderer = new DiligentEnhancedTerrainRenderer(gfx, client->terrain, &client->camera);
	else
		terrainRenderer = new DefaultTerrainRenderer(gfx, client->terrain, &client->camera);
}

bool ClientInterface::editorInput(const Vector3& cursor)
{
	if (!editor)
		editor = std::make_shared<EditorState>();
	EditorState& ed = *editor;
	Terrain* trn = client->terrain;
	if (!trn || !withServer)
		return false;
	const bool overUi = ImGui::GetIO().WantCaptureMouse;
	const uint32_t now = SDL_GetTicks();
	if (g_modCtrl && g_keyPressed[SDL_SCANCODE_Z] && !ImGui::GetIO().WantTextInput)
		g_modShift ? editorRedo() : editorUndo();
	if (g_modCtrl && g_keyPressed[SDL_SCANCODE_Y] && !ImGui::GetIO().WantTextInput)
		editorRedo();

	if (ed.mode == EditorState::OBJECTS && ed.objectTool == EditorState::SELECT) {
		const bool keys = !ImGui::GetIO().WantTextInput && !g_modCtrl;
		if (!ed.grabbing && keys && g_keyPressed[SDL_SCANCODE_G])
			editorGrab();
		if (!ed.grabbing && keys && g_keyPressed[SDL_SCANCODE_R])
			editorTurn(g_modShift ? -15.0f : 15.0f);
		if (!ed.grabbing)
			return false; // the game's clicks select
		// the selection follows the cursor, each object keeping its offset from where the grab began
		const Vector3 shift = cursor - ed.grabFrom;
		if (now - ed.lastGrabMove > 30) {
			ed.lastGrabMove = now;
			const std::vector<EditorState::Moved> start = ed.grabStart;
			withServer([&](Server& server) {
				for (const auto& m : start)
					if (ServerGameObject* obj = server.findObject(m.id))
						obj->setPosition(Vector3(m.position.x + shift.x, 0, m.position.z + shift.z));
			});
		}
		if (g_mousePressed[SDL_BUTTON_LEFT]) {
			ed.grabbing = false;
			EditorState::Undo u;
			u.moved = ed.grabStart;
			ed.pushUndo(std::move(u));
			ed.status = "Moved " + std::to_string(ed.grabStart.size()) + " objects";
		}
		else if (g_mousePressed[SDL_BUTTON_RIGHT]) {
			ed.grabbing = false;
			const std::vector<EditorState::Moved> start = ed.grabStart;
			withServer([&](Server& server) {
				for (const auto& m : start)
					if (ServerGameObject* obj = server.findObject(m.id))
						obj->setPosition(m.position);
			});
			ed.status = "Move cancelled";
		}
		return true;
	}

	if (ed.mode == EditorState::OBJECTS) {
		if (overUi || !g_mousePressed[SDL_BUTTON_LEFT] || ed.placeType < 0)
			return false;
		const GameObjBlueprint* type = &client->gameSet->objBlueprints[ed.placeClass][ed.placeType];
		const uint32_t playerId = ed.placePlayer;
		const Vector3 at = cursor;
		const float yaw = ed.placeYaw;
		uint32_t placed = 0;
		withServer([&](Server& server) {
			ServerGameObject* player = server.findObject(playerId);
			if (!player)
				return;
			Vector3 pos = at;
			pos.y = server.terrain->getHeight(pos.x, pos.z);
			if (ServerGameObject* obj = server.spawnObject(type, player, pos, Vector3(0, yaw, 0)))
				placed = obj->id;
		});
		if (placed) {
			EditorState::Undo u;
			u.placed.push_back(placed);
			ed.pushUndo(std::move(u));
		}
		ed.status = "Placed " + type->name;
		return true;
	}

	// water: a click adds or removes a lake; the client's and the server's water filled again
	if (ed.mode == EditorState::WATER) {
		if (overUi || !g_mousePressed[SDL_BUTTON_LEFT])
			return false;
		std::vector<Vector3> lakes = trn->lakes;
		std::vector<uint8_t> bits = trn->lakeExtraBits;
		bits.resize(lakes.size(), 0);
		const int tx = (int)(cursor.x / 5.0f) + trn->edge, tz = (int)(cursor.z / 5.0f) + trn->edge;
		if (!trn->getTile(tx, tz))
			return false;
		if (ed.waterTool == EditorState::ADD_LAKE) {
			if (lakes.size() >= 63) {
				ed.status = "A map has at most 63 lakes";
				return true;
			}
			// the lake's point in the whole map's coordinates (with the edge), as the BCM keeps it
			lakes.emplace_back(cursor.x + trn->edge * 5.0f, trn->getHeight(cursor.x, cursor.z) + ed.waterDepth, cursor.z + trn->edge * 5.0f);
			bits.push_back(0);
			ed.status = "Lake added";
		}
		else {
			// a tile's water level is that of the highest lake reaching it: the nearest lake at
			// that level goes (where another lake's water stays, a second click removes that one)
			const TerrainTile* tile = trn->getTile(tx, tz);
			if (!tile->fullOfWater) {
				ed.status = "No water here";
				return true;
			}
			const Vector3 at(cursor.x + trn->edge * 5.0f, 0, cursor.z + trn->edge * 5.0f);
			int best = -1;
			for (size_t i = 0; i < lakes.size(); i++)
				if (std::abs(lakes[i].y - tile->waterLevel) < 0.01f
					&& (best < 0 || (lakes[i] - at).len2xz() < (lakes[best] - at).len2xz()))
					best = (int)i;
			if (best < 0) {
				ed.status = "No lake found for this water";
				return true;
			}
			lakes.erase(lakes.begin() + best);
			bits.erase(bits.begin() + best);
			ed.status = "Lake removed";
		}
		EditorState::Undo u;
		u.hasLakes = true;
		u.lakes = trn->lakes;
		u.lakeBits = trn->lakeExtraBits;
		ed.pushUndo(std::move(u));
		setLakes(lakes, bits);
		return true;
	}

	// texture brush: the tiles within the radius get the texture, on the client and the server
	if (ed.mode == EditorState::TEXTURES) {
		const bool down = g_mouseDown[SDL_BUTTON_LEFT] && (ed.stroke || !overUi);
		TerrainTexture* tex = FindTexture(trn, ed.paintGroup, ed.paintId);
		if (down && tex) {
			if (!ed.stroke) {
				ed.stroke = true;
				EditorState::Undo u;
				u.tiles.reserve((size_t)trn->width * trn->height);
				for (int i = 0; i < trn->width * trn->height; i++)
					u.tiles.push_back({ trn->tiles[i].texture, (uint8_t)trn->tiles[i].rot, (uint8_t)trn->tiles[i].xflip, (uint8_t)trn->tiles[i].zflip });
				ed.pushUndo(std::move(u));
			}
			const float r = std::max(0.5f, ed.radius / 5.0f); // in tiles
			const float cx = cursor.x / 5.0f + trn->edge, cz = cursor.z / 5.0f + trn->edge;
			struct Paint { int x, z, rot; };
			std::vector<Paint> painted;
			for (int z = (int)std::floor(cz - r); z <= (int)std::ceil(cz + r); z++)
				for (int x = (int)std::floor(cx - r); x <= (int)std::ceil(cx + r); x++) {
					const float dx = x + 0.5f - cx, dz = z + 0.5f - cz;
					TerrainTile* tile = trn->getTileMutable(x, z);
					if (!tile || dx * dx + dz * dz > r * r)
						continue;
					// a random turn per tile, the same while the stroke passes over it again
					const int rot = ed.paintRandomTurn ? (int)((x * 73856093u ^ z * 19349663u) >> 7) & 3 : ed.paintTurn;
					if (tile->texture == tex && tile->rot == (unsigned)rot && tile->xflip == (unsigned)ed.paintFlipX && tile->zflip == (unsigned)ed.paintFlipZ)
						continue;
					tile->texture = tex;
					tile->rot = rot;
					tile->xflip = ed.paintFlipX;
					tile->zflip = ed.paintFlipZ;
					painted.push_back({ x, z, rot });
				}
			if (!painted.empty()) {
				const std::string group = ed.paintGroup;
				const int id = ed.paintId;
				const bool fx = ed.paintFlipX, fz = ed.paintFlipZ;
				withServer([&](Server& server) {
					if (!server.terrain)
						return;
					TerrainTexture* stex = FindTexture(server.terrain, group, id);
					for (const Paint& p : painted)
						if (TerrainTile* tile = server.terrain->getTileMutable(p.x, p.z); tile && stex) {
							tile->texture = stex;
							tile->rot = p.rot;
							tile->xflip = fx;
							tile->zflip = fz;
						}
					ResetTileTerrainTypes(server);
				});
				ed.terrainChanged = true;
			}
			if (ed.terrainChanged && now - ed.lastRebuild > 200) {
				rebuildTerrainRenderer(true);
				ed.lastRebuild = now;
				ed.terrainChanged = false;
			}
			return true;
		}
		if (ed.stroke && !down) {
			ed.stroke = false;
			if (ed.terrainChanged)
				rebuildTerrainRenderer(true);
			ed.terrainChanged = false;
			return true;
		}
		return false;
	}
	// zone brush: the playable tiles within the radius join the zone (or leave it), on the server
	if (ed.mode == EditorState::ZONES) {
		const bool down = g_mouseDown[SDL_BUTTON_LEFT] && (ed.stroke || !overUi);
		ed.stroke = down;
		if (!down || !ed.zoneId)
			return down;
		const float r = std::max(0.5f, ed.radius / 5.0f);
		const float cx = cursor.x / 5.0f, cz = cursor.z / 5.0f;
		const uint32_t zoneId = ed.zoneId;
		const bool add = ed.zoneTool == EditorState::ADD_TILES;
		withServer([&](Server& server) {
			ServerGameObject* zone = server.findObject(zoneId);
			if (!zone || !server.terrain)
				return;
			const auto area = server.terrain->getNumPlayableTiles();
			for (int z = (int)std::floor(cz - r); z <= (int)std::ceil(cz + r); z++)
				for (int x = (int)std::floor(cx - r); x <= (int)std::ceil(cx + r); x++) {
					const float dx = x + 0.5f - cx, dz = z + 0.5f - cz;
					if (x < 0 || z < 0 || x >= area.first || z >= area.second || dx * dx + dz * dz > r * r)
						continue;
					if (add)
						zone->addZoneTile(x, z);
					else
						zone->removeZoneTile(x, z);
				}
		});
		return true;
	}

	if (ed.mode != EditorState::TERRAIN) // the Players mode has no clicks on the map
		return false;

	// terrain brush: while the left button is held
	const bool down = g_mouseDown[SDL_BUTTON_LEFT] && (ed.stroke || !overUi);
	if (down && !ed.stroke) {
		ed.stroke = true;
		ed.lastApply = now;
		ed.flattenTo = trn->getHeight(cursor.x, cursor.z) / trn->scale;
		ed.strokeMin = ed.strokeMax = cursor;
		ed.pending.assign(trn->vertices.size(), 0.0f);
		EditorState::Undo u;
		u.heights.assign(trn->vertices.begin(), trn->vertices.end());
		ed.pushUndo(std::move(u));
	}
	if (ed.stroke && down) {
		const float dt = std::min(0.1f, (now - ed.lastApply) / 1000.0f);
		ed.lastApply = now;
		const float r = ed.radius / 5.0f; // in vertices
		const float cx = cursor.x / 5.0f + trn->edge, cz = cursor.z / 5.0f + trn->edge;
		const int x0 = std::max(1, (int)std::floor(cx - r)), x1 = std::min(trn->width - 1, (int)std::ceil(cx + r));
		const int z0 = std::max(1, (int)std::floor(cz - r)), z1 = std::min(trn->height - 1, (int)std::ceil(cz + r));
		ed.strokeMin = Vector3(std::min(ed.strokeMin.x, cursor.x - ed.radius), 0, std::min(ed.strokeMin.z, cursor.z - ed.radius));
		ed.strokeMax = Vector3(std::max(ed.strokeMax.x, cursor.x + ed.radius), 0, std::max(ed.strokeMax.z, cursor.z + ed.radius));
		std::vector<std::pair<int, uint8_t>> changes;
		const int stride = trn->width + 1;
		for (int z = z0; z <= z1; z++)
			for (int x = x0; x <= x1; x++) {
				const float d = std::sqrt((x - cx) * (x - cx) + (z - cz) * (z - cz));
				if (d >= r)
					continue;
				float falloff = 1.0f - d / r;
				falloff = falloff * falloff * (3.0f - 2.0f * falloff); // smoothstep
				const int i = z * stride + x;
				const float h = trn->vertices[i];
				float delta = 0.0f;
				switch (ed.tool) {
				case EditorState::RAISE: delta = ed.strength * dt * falloff; break;
				case EditorState::LOWER: delta = -ed.strength * dt * falloff; break;
				case EditorState::SMOOTH: {
					const float avg = (trn->vertices[i - 1] + trn->vertices[i + 1] + trn->vertices[i - stride] + trn->vertices[i + stride]) / 4.0f;
					delta = (avg - h) * std::min(1.0f, ed.strength * dt * 0.25f) * falloff;
					break;
				}
				case EditorState::FLATTEN:
					delta = (ed.flattenTo - h) * std::min(1.0f, ed.strength * dt * 0.25f) * falloff;
					break;
				}
				ed.pending[i] += delta;
				const int step = (int)ed.pending[i]; // whole height steps
				if (step != 0) {
					ed.pending[i] -= step;
					const uint8_t nh = (uint8_t)std::clamp((int)h + step, 0, 255);
					if (nh != trn->vertices[i])
						changes.emplace_back(i, nh);
				}
			}
		if (!changes.empty()) {
			for (const auto& [i, h] : changes)
				trn->vertices[i] = h;
			withServer([&](Server& server) {
				if (server.terrain && server.terrain->vertices.size() == trn->vertices.size())
					for (const auto& [i, h] : changes)
						server.terrain->vertices[i] = h;
			});
			ed.terrainChanged = true;
		}
		// the terrain shown again now and then while painting (it is built in one go)
		if (ed.terrainChanged && now - ed.lastRebuild > 400) {
			rebuildTerrainRenderer();
			ed.lastRebuild = now;
			ed.terrainChanged = false;
		}
		return true;
	}
	if (ed.stroke && !down) {
		ed.stroke = false;
		if (ed.terrainChanged)
			rebuildTerrainRenderer();
		ed.terrainChanged = false;
		// the lakes fill the new ground
		if (!trn->lakes.empty())
			setLakes(trn->lakes, trn->lakeExtraBits);
		// the objects in the changed area stand on the new ground
		const Vector3 centre = (ed.strokeMin + ed.strokeMax) * 0.5f;
		const float reach = std::max(ed.strokeMax.x - ed.strokeMin.x, ed.strokeMax.z - ed.strokeMin.z) * 0.75f + 5.0f;
		withServer([&](Server& server) {
			NNSearch search;
			search.start(&server, centre, reach);
			while (ServerGameObject* obj = (ServerGameObject*)search.next())
				obj->setPosition(Vector3(obj->position.x, server.terrain->getHeight(obj->position.x, obj->position.z), obj->position.z));
		});
		return true;
	}
	return false;
}

// The Zones mode's panel: the level's terrain zones (a new one, its name, deleting it), the
// brush, and the chosen zone's tiles drawn over the map. Zones live on the server only.
void ClientInterface::editorZones()
{
	EditorState& ed = *editor;
	struct ZoneRow { uint32_t id; std::string type, name; size_t tiles; };
	std::vector<ZoneRow> zones;
	std::vector<std::pair<int, int>> selectedTiles;
	std::vector<std::string> zoneTypes;
	withServer([&](Server& server) {
		const auto& types = server.gameSet->objBlueprints[Tags::GAMEOBJCLASS_TERRAIN_ZONE];
		for (size_t i = 0; i < types.size(); i++)
			zoneTypes.push_back(types.names.getString((int)i));
		ServerGameObject* level = server.getLevel();
		if (!level)
			return;
		for (const auto& [type, children] : level->children)
			for (CommonGameObject* child : children)
				if (child->blueprint->bpClass == Tags::GAMEOBJCLASS_TERRAIN_ZONE) {
					ServerGameObject* zone = (ServerGameObject*)child;
					zones.push_back({ zone->id, zone->blueprint->name, zone->name, zone->zoneTiles.size() });
					if (zone->id == ed.zoneId)
						selectedTiles.assign(zone->zoneTiles.begin(), zone->zoneTiles.end());
				}
	});
	if (zoneTypes.empty()) {
		ImGui::TextWrapped("The game set has no TERRAIN_ZONE types.");
		return;
	}
	ed.zoneType = std::clamp(ed.zoneType, 0, (int)zoneTypes.size() - 1);
	if (ImGui::BeginCombo("Type", zoneTypes[ed.zoneType].c_str())) {
		for (int i = 0; i < (int)zoneTypes.size(); i++)
			if (ImGui::Selectable(zoneTypes[i].c_str(), i == ed.zoneType))
				ed.zoneType = i;
		ImGui::EndCombo();
	}
	ImGui::SameLine();
	if (ImGui::Button("New zone")) {
		const int type = ed.zoneType;
		const size_t count = zones.size();
		withServer([&](Server& server) {
			if (ServerGameObject* level = server.getLevel())
				if (ServerGameObject* zone = server.spawnObject(&server.gameSet->objBlueprints[Tags::GAMEOBJCLASS_TERRAIN_ZONE][type], level, Vector3(0, 0, 0), Vector3(0, 0, 0))) {
					zone->name = "Zone " + std::to_string(count + 1);
					ed.zoneId = zone->id;
				}
		});
		ed.status = "New zone: paint its tiles";
	}
	if (ImGui::BeginListBox("##zones", ImVec2(-1, 120))) {
		for (const ZoneRow& z : zones) {
			const std::string label = (z.name.empty() ? std::string("(no name)") : z.name) + " - " + z.type + ", " + std::to_string(z.tiles) + " tiles##" + std::to_string(z.id);
			if (ImGui::Selectable(label.c_str(), z.id == ed.zoneId))
				ed.zoneId = z.id;
		}
		ImGui::EndListBox();
	}
	auto selected = std::find_if(zones.begin(), zones.end(), [&](const ZoneRow& z) { return z.id == ed.zoneId; });
	if (selected == zones.end()) {
		ed.zoneId = 0;
		ImGui::TextWrapped("Make a zone or choose one, then paint its tiles on the map. Triggers and finders find a zone by its name.");
		return;
	}
	char name[64];
	snprintf(name, sizeof(name), "%s", selected->name.c_str());
	if (ImGui::InputText("Name", name, sizeof(name))) {
		const std::string newName = name;
		const uint32_t id = ed.zoneId;
		withServer([&](Server& server) {
			if (ServerGameObject* zone = server.findObject(id))
				zone->name = newName;
		});
	}
	ImGui::RadioButton("Add tiles", &ed.zoneTool, EditorState::ADD_TILES);
	ImGui::SameLine();
	ImGui::RadioButton("Remove tiles", &ed.zoneTool, EditorState::REMOVE_TILES);
	ImGui::SliderFloat("Radius (m)", &ed.radius, 2.5f, 100.0f, "%.1f");
	if (ImGui::Button("Delete zone")) {
		const uint32_t id = ed.zoneId;
		withServer([&](Server& server) {
			if (ServerGameObject* zone = server.findObject(id)) {
				for (const auto& [x, z] : std::vector<std::pair<int, int>>(zone->zoneTiles.begin(), zone->zoneTiles.end()))
					zone->removeZoneTile(x, z);
				server.deleteObject(zone);
			}
		});
		ed.zoneId = 0;
		ed.status = "Zone deleted";
		return;
	}
	// the zone's tiles over the map (at most 20000; the tile corners at ground height)
	if (client->terrain) {
		ImDrawList* dl = ImGui::GetBackgroundDrawList();
		auto toScreen = [&](float x, float z, bool& visible) {
			const Vector3 t = Vector3(x, client->terrain->getHeight(x, z) + 0.3f, z).transformScreenCoords(client->camera.sceneMatrix);
			visible = visible && t.z > 0.0f && t.z < 1.0f;
			return ImVec2((t.x + 1) * 0.5f * g_windowWidth / g_uiScale, (1 - t.y) * 0.5f * g_windowHeight / g_uiScale);
		};
		const ImU32 fill = IM_COL32(80, 200, 255, 70), edge = IM_COL32(80, 200, 255, 200);
		for (size_t i = 0; i < selectedTiles.size() && i < 20000; i++) {
			const float x0 = selectedTiles[i].first * 5.0f, z0 = selectedTiles[i].second * 5.0f;
			bool visible = true;
			const ImVec2 a = toScreen(x0, z0, visible), b = toScreen(x0 + 5, z0, visible), c = toScreen(x0 + 5, z0 + 5, visible), d = toScreen(x0, z0 + 5, visible);
			if (!visible)
				continue;
			dl->AddQuadFilled(a, b, c, d, fill);
			dl->AddQuad(a, b, c, d, edge);
		}
	}
	ImGui::TextWrapped("Hold the left button to paint. A tile belongs to one zone: adding it to this zone takes it from another. Zones are saved with the map.");
}

void ClientInterface::editorWindow()
{
	if (!editor)
		editor = std::make_shared<EditorState>();
	EditorState& ed = *editor;
	ImGui::SetNextWindowPos(ImVec2(10, 120), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(360, 520), ImGuiCond_FirstUseEver);
	bool open = true;
	if (ImGui::Begin("Map editor (F8)", &open)) {
		ImGui::RadioButton("Terrain", &ed.mode, EditorState::TERRAIN);
		ImGui::SameLine();
		ImGui::RadioButton("Textures", &ed.mode, EditorState::TEXTURES);
		ImGui::SameLine();
		ImGui::RadioButton("Water", &ed.mode, EditorState::WATER);
		ImGui::SameLine();
		ImGui::RadioButton("Objects", &ed.mode, EditorState::OBJECTS);
		ImGui::RadioButton("Players", &ed.mode, EditorState::PLAYERS);
		ImGui::SameLine();
		ImGui::RadioButton("Zones", &ed.mode, EditorState::ZONES);
		ImGui::Separator();
		if (ed.mode == EditorState::ZONES)
			editorZones();
		else if (ed.mode == EditorState::PLAYERS) {
			// each player's starting resources and its diplomacy towards the others, set on the
			// server: the level is saved from there
			std::vector<ClientGameObject*> players;
			if (ClientGameObject* level = client->getLevel())
				for (const auto& [type, children] : level->children)
					for (CommonGameObject* child : children)
						if (child->blueprint->bpClass == Tags::GAMEOBJCLASS_PLAYER && child->blueprint->name != "Neutral Player")
							players.push_back((ClientGameObject*)child);
			static const char* resourceNames[] = { "Food", "Wood", "Gold", "Stone" };
			static const char* statusNames[] = { "Allied", "Neutral", "Enemy" };
			for (ClientGameObject* player : players) {
				const std::string label = std::to_string(player->id) + " " + player->blueprint->name;
				if (!ImGui::CollapsingHeader(label.c_str(), player == players.front() ? ImGuiTreeNodeFlags_DefaultOpen : 0))
					continue;
				ImGui::PushID((int)player->id);
				for (const char* resource : resourceNames) {
					const int item = client->gameSet->items.names.getIndex(resource);
					if (item < 0)
						continue;
					int value = (int)player->getItem(item);
					if (ImGui::InputInt(resource, &value, 50, 500)) {
						const uint32_t id = player->id;
						withServer([&](Server& server) {
							if (ServerGameObject* sp = server.findObject(id))
								sp->setItem(item, (float)std::max(0, value));
						});
					}
				}
				for (ClientGameObject* other : players) {
					if (other == player)
						continue;
					int status = std::clamp(client->getDiplomaticStatus(player, other), 0, 2);
					const std::string otherLabel = "towards " + std::to_string(other->id) + " " + other->blueprint->name;
					if (ImGui::Combo(otherLabel.c_str(), &status, statusNames, 3)) {
						const uint32_t a = player->id, b = other->id;
						withServer([&](Server& server) {
							ServerGameObject* sa = server.findObject(a);
							ServerGameObject* sb = server.findObject(b);
							if (sa && sb)
								server.setDiplomaticStatus(sa, sb, status);
						});
					}
				}
				ImGui::PopID();
			}
			ImGui::TextWrapped("The resources each player starts with, and how it stands to the others (both ways). A skirmish's settings replace the resources when the map is played as a skirmish.");
		}
		else if (ed.mode == EditorState::WATER && client->terrain) {
			ImGui::RadioButton("Add a lake", &ed.waterTool, EditorState::ADD_LAKE);
			ImGui::SameLine();
			ImGui::RadioButton("Remove a lake", &ed.waterTool, EditorState::REMOVE_LAKE);
			if (ed.waterTool == EditorState::ADD_LAKE)
				ImGui::SliderFloat("Depth (m)", &ed.waterDepth, 0.5f, 30.0f, "%.1f");
			ImGui::Text("%zu lakes (at most 63)", client->terrain->lakes.size());
			ImGui::TextWrapped("A lake fills the ground around the click up to its level (the ground there plus the depth), as far as the ground stays lower. Remove takes away the lake whose water is under the cursor.");
		}
		else if (ed.mode == EditorState::TEXTURES && client->terrain) {
			// the map's texture database: a group, then one of its textures
			const auto& groups = client->terrain->texDb.groups;
			if (ed.paintGroup.empty() || !groups.count(ed.paintGroup))
				for (const auto& [name, g] : groups)
					if (g && !g->textures.empty()) {
						ed.paintGroup = name;
						ed.paintId = g->textures.begin()->first;
						break;
					}
			if (ImGui::BeginCombo("Group", ed.paintGroup.c_str())) {
				for (const auto& [name, g] : groups)
					if (g && !g->textures.empty() && ImGui::Selectable(name.c_str(), name == ed.paintGroup)) {
						ed.paintGroup = name;
						ed.paintId = g->textures.begin()->first;
					}
				ImGui::EndCombo();
			}
			if (auto g = groups.find(ed.paintGroup); g != groups.end() && g->second && ImGui::BeginListBox("##textures", ImVec2(-1, 180))) {
				for (const auto& [id, tex] : g->second->textures) {
					const std::string label = std::to_string(id) + ": " + tex->file + " (" + std::to_string(tex->startx) + ", " + std::to_string(tex->starty) + ")";
					if (ImGui::Selectable(label.c_str(), id == ed.paintId))
						ed.paintId = id;
				}
				ImGui::EndListBox();
			}
			ImGui::SliderFloat("Radius (m)", &ed.radius, 2.5f, 60.0f, "%.1f");
			ImGui::Checkbox("Random turn", &ed.paintRandomTurn);
			if (!ed.paintRandomTurn) {
				ImGui::SameLine();
				ImGui::SliderInt("Turn", &ed.paintTurn, 0, 3, "%d x 90");
			}
			ImGui::Checkbox("Flip x", &ed.paintFlipX);
			ImGui::SameLine();
			ImGui::Checkbox("Flip z", &ed.paintFlipZ);
			ImGui::TextWrapped("Hold the left mouse button on the ground to paint the tiles within the radius.");
		}
		else if (ed.mode == EditorState::TERRAIN) {
			ImGui::RadioButton("Raise", &ed.tool, EditorState::RAISE); ImGui::SameLine();
			ImGui::RadioButton("Lower", &ed.tool, EditorState::LOWER); ImGui::SameLine();
			ImGui::RadioButton("Smooth", &ed.tool, EditorState::SMOOTH); ImGui::SameLine();
			ImGui::RadioButton("Flatten", &ed.tool, EditorState::FLATTEN);
			ImGui::SliderFloat("Radius (m)", &ed.radius, 5.0f, 100.0f, "%.0f");
			ImGui::SliderFloat("Strength", &ed.strength, 1.0f, 100.0f, "%.0f");
			ImGui::TextWrapped("Hold the left mouse button on the ground to paint. Flatten keeps the height where the stroke began.");
		}
		else {
			ImGui::RadioButton("Place", &ed.objectTool, EditorState::PLACE);
			ImGui::SameLine();
			ImGui::RadioButton("Select, move, turn", &ed.objectTool, EditorState::SELECT);
			if (ed.objectTool == EditorState::SELECT) {
				ImGui::Text("%zu selected", selection.size());
				if (ImGui::Button("Move (G)"))
					editorGrab();
				ImGui::SameLine();
				if (ImGui::Button("Turn -15 (Shift+R)"))
					editorTurn(-15.0f);
				ImGui::SameLine();
				if (ImGui::Button("Turn +15 (R)"))
					editorTurn(15.0f);
				if (ImGui::Button("Turn -90"))
					editorTurn(-90.0f);
				ImGui::SameLine();
				if (ImGui::Button("Turn +90"))
					editorTurn(90.0f);
				ImGui::SameLine();
				if (ImGui::Button("Remove (Delete)"))
					editorDeleteSelection();
				ImGui::TextWrapped("Click or drag a box to select, as in the game. Move: the selection follows the cursor; a left click puts it down, a right click puts it back.");
				editorProperties();
			}
			else {
			static const int classes[] = { Tags::GAMEOBJCLASS_BUILDING, Tags::GAMEOBJCLASS_CHARACTER, Tags::GAMEOBJCLASS_CONTAINER,
				Tags::GAMEOBJCLASS_PROP, Tags::GAMEOBJCLASS_MARKER };
			static const char* const classNames[] = { "Buildings", "Characters", "Containers (trees, ...)", "Props", "Markers" };
			int ci = (int)(std::find(std::begin(classes), std::end(classes), ed.placeClass) - std::begin(classes));
			if (ImGui::Combo("Class", &ci, classNames, (int)std::size(classNames))) {
				ed.placeClass = classes[ci];
				ed.placeType = -1;
			}
			// the players, for the new object's owner
			std::vector<std::pair<uint32_t, std::string>> players;
			if (ClientGameObject* level = client->getLevel())
				for (const auto& [type, children] : level->children)
					for (CommonGameObject* child : children)
						if (child->blueprint->bpClass == Tags::GAMEOBJCLASS_PLAYER)
							players.emplace_back(child->id, std::to_string(child->id) + " " + child->blueprint->name);
			if (!players.empty() && std::none_of(players.begin(), players.end(), [&](const auto& p) { return p.first == ed.placePlayer; }))
				ed.placePlayer = players.size() > 1 ? players[1].first : players[0].first;
			std::string current;
			for (const auto& p : players)
				if (p.first == ed.placePlayer)
					current = p.second;
			if (ImGui::BeginCombo("Player", current.c_str())) {
				for (const auto& p : players)
					if (ImGui::Selectable(p.second.c_str(), p.first == ed.placePlayer))
						ed.placePlayer = p.first;
				ImGui::EndCombo();
			}
			ImGui::SliderAngle("Facing", &ed.placeYaw, -180.0f, 180.0f);
			ImGui::InputText("Filter", ed.filter, sizeof(ed.filter));
			auto& list = client->gameSet->objBlueprints[ed.placeClass];
			if (ImGui::BeginListBox("##types", ImVec2(-1, 220))) {
				for (size_t i = 0; i < list.size(); i++) {
					const std::string& name = list.names.getString((int)i);
					if (ed.filter[0] && !ContainsNoCase(name, ed.filter))
						continue;
					if (ImGui::Selectable(name.c_str(), (int)i == ed.placeType))
						ed.placeType = (int)i;
				}
				ImGui::EndListBox();
			}
			ImGui::TextWrapped("Click the ground to place one. Select objects and press Delete to remove them.");
			}
		}
		ImGui::Separator();
		ImGui::InputText("Map name", ed.saveName, sizeof(ed.saveName));
		if (ImGui::Button("Undo (Ctrl+Z)"))
			editorUndo();
		ImGui::SameLine();
		if (ImGui::Button("Redo (Ctrl+Y)"))
			editorRedo();
		ImGui::SameLine();
		ImGui::TextDisabled("%zu / %zu steps", ed.undo.size(), ed.redo.size());
		if (ImGui::Button("Save map"))
			editorSave(ed.saveName);
		ImGui::SameLine();
		// the saved level started as a game
		if (ImGui::Button("Save and play")) {
			if (editorSave(ed.saveName)) {
				leaveAction = "start";
				leaveArg = std::string(ed.saveName) + ".lvl";
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Check"))
			ed.warnings = editorCheck();
		// a new map from this one: its size, sky and texture database, nothing else
		if (ImGui::Button("New blank map..."))
			ImGui::OpenPopup("New blank map");
		if (ImGui::BeginPopupModal("New blank map", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGui::TextWrapped("Flat ground of one texture (the Textures mode's), no lakes, and no objects but the players. The map keeps its size, sky and textures. Undo can't bring the old map back; it stays as it was saved.");
			if (ImGui::Button("Make it blank")) {
				editorBlankMap();
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel"))
				ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}
		for (const std::string& w : ed.warnings)
			ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "%s", w.c_str());
		if (!ed.status.empty())
			ImGui::TextWrapped("%s", ed.status.c_str());
	}
	ImGui::End();
	if (!open)
		editorOn = false;
}

// "New blank map": the heights flat, every tile the chosen texture (turned 0, no flips), no
// lakes, and every object removed but the players (their start cameras and resources stay), on
// the client and the server. The size, the sky and the texture database are the open map's.
void ClientInterface::editorBlankMap()
{
	if (!editor)
		editor = std::make_shared<EditorState>();
	if (!client->terrain)
		return;
	EditorState& ed = *editor;
	Terrain* trn = client->terrain;
	std::string group = ed.paintGroup;
	int id = ed.paintId;
	// no texture chosen: a plain grass group's first texture (GRASS..., not a blend), else any grass, else the first
	if (!FindTexture(trn, group, id))
		for (int pass = 0; pass < 3 && !FindTexture(trn, group, id); pass++)
			for (const auto& [name, g] : trn->texDb.groups)
				if (g && !g->textures.empty() && (pass == 2 || (pass == 1 && name.find("GRASS") != std::string::npos)
					|| (name.rfind("GRASS", 0) == 0 && name.find('-') == std::string::npos))) {
					group = name;
					id = g->textures.begin()->first;
					break;
				}
	const uint8_t flatHeight = 20;
	auto blank = [&](Terrain* t, TerrainTexture* tex) {
		std::fill(t->vertices.begin(), t->vertices.end(), flatHeight);
		for (int i = 0; i < t->width * t->height; i++) {
			if (tex)
				t->tiles[i].texture = tex;
			t->tiles[i].rot = 0;
			t->tiles[i].xflip = 0;
			t->tiles[i].zflip = 0;
		}
	};
	blank(trn, FindTexture(trn, group, id));
	size_t removed = 0;
	withServer([&](Server& server) {
		if (server.terrain && server.terrain != trn)
			blank(server.terrain, FindTexture(server.terrain, group, id));
		ResetTileTerrainTypes(server);
		std::vector<ServerGameObject*> gone;
		if (ServerGameObject* level = server.getLevel())
			for (const auto& [type, children] : level->children)
				for (CommonGameObject* child : children) {
					if (child->blueprint->bpClass != Tags::GAMEOBJCLASS_PLAYER) {
						gone.push_back((ServerGameObject*)child);
						continue;
					}
					for (const auto& [subType, subs] : child->children)
						for (CommonGameObject* sub : subs)
							gone.push_back((ServerGameObject*)sub);
				}
		for (ServerGameObject* obj : gone)
			server.deleteObject(obj);
		removed = gone.size();
	});
	ed.undo.clear();
	ed.zoneId = 0;
	setLakes({}, {});
	rebuildTerrainRenderer(false);
	rebuildTerrainRenderer(true);
	// the HUD's minimap drawn again from the blank ground (as the save writes it into the BCM)
	const std::vector<uint8_t> rgb = trn->drawMinimap();
	trn->minimapWidth = trn->minimapHeight = 128;
	trn->minimapRGBA.resize(128 * 128 * 4);
	for (size_t i = 0; i < 128 * 128; i++) {
		for (int c = 0; c < 3; c++)
			trn->minimapRGBA[i * 4 + c] = rgb[i * 3 + c];
		trn->minimapRGBA[i * 4 + 3] = 255;
	}
	minimapTerrain = nullptr;
	ed.status = "Blank map: " + std::to_string(removed) + " objects removed, the ground flat with " + group + " " + std::to_string(id) + ". Save it under a new name.";
	printf("EDITORBLANK %s\n", ed.status.c_str());
}

// an object the server still has (one removed this frame is only marked, and goes at its end)
static ServerGameObject* FindLive(Server& server, uint32_t id)
{
	ServerGameObject* obj = server.findObject(id);
	return obj && !obj->deleted ? obj : nullptr;
}

ClientInterface::EditorState::Props ClientInterface::EditorState::readProps(ServerGameObject* obj)
{
	Props p;
	p.id = obj->id;
	p.parent = obj->parent ? obj->parent->id : 0;
	p.name = obj->name;
	p.items = obj->items;
	return p;
}

// an object's name, owner and items set to the ones kept (an item it has now but didn't then
// goes)
void ClientInterface::EditorState::applyProps(Server& server, ServerGameObject* obj, const Props& p) const
{
	if (obj->name != p.name)
		obj->setName(p.name);
	if (ServerGameObject* parent = FindLive(server, resolve(p.parent)); parent && obj->parent && parent != obj->parent)
		obj->setParent(parent);
	std::vector<int> gone;
	for (const auto& [item, value] : obj->items)
		if (!p.items.count(item))
			gone.push_back(item);
	for (int item : gone) {
		obj->setItem(item, 0.0f);
		obj->items.erase(item);
	}
	for (const auto& [item, value] : p.items)
		obj->setItem(item, value);
}

void ClientInterface::editorUndo() { editorStep(false); }
void ClientInterface::editorRedo() { editorStep(true); }

void ClientInterface::editorGrab()
{
	if (!editor)
		return;
	EditorState& ed = *editor;
	std::vector<uint32_t> ids;
	for (const auto& ref : selection)
		if (ClientGameObject* obj = ref.get())
			ids.push_back(obj->id);
	ed.grabStart.clear();
	withServer([&](Server& server) {
		for (uint32_t id : ids)
			if (ServerGameObject* obj = FindLive(server, id))
				ed.grabStart.push_back({ id, obj->position, obj->orientation });
	});
	if (ed.grabStart.empty()) {
		ed.status = "Select objects first";
		return;
	}
	ed.grabbing = true;
	ed.grabFrom = Vector3(0, 0, 0);
	// from where the grab began: the selection's centre, so the objects stay under the cursor
	for (const auto& m : ed.grabStart)
		ed.grabFrom += m.position;
	ed.grabFrom = ed.grabFrom * (1.0f / ed.grabStart.size());
	ed.status = "Moving: left click to put them down, right click to cancel";
}

void ClientInterface::editorTurn(float degrees)
{
	if (!editor)
		return;
	EditorState& ed = *editor;
	std::vector<uint32_t> ids;
	for (const auto& ref : selection)
		if (ClientGameObject* obj = ref.get())
			ids.push_back(obj->id);
	EditorState::Undo u;
	withServer([&](Server& server) {
		for (uint32_t id : ids)
			if (ServerGameObject* obj = FindLive(server, id)) {
				u.moved.push_back({ id, obj->position, obj->orientation });
				obj->setOrientation(Vector3(obj->orientation.x, obj->orientation.y + degrees * 3.14159265f / 180.0f, obj->orientation.z));
			}
	});
	if (u.moved.empty()) {
		ed.status = "Select objects first";
		return;
	}
	ed.status = "Turned " + std::to_string(u.moved.size()) + " objects";
	ed.pushUndo(std::move(u));
}

// the selection removed on the server; undo places each again (its type, owner, place and
// facing, not its items or orders)
void ClientInterface::editorDeleteSelection()
{
	if (!editor)
		editor = std::make_shared<EditorState>();
	EditorState& ed = *editor;
	std::vector<uint32_t> ids;
	for (const auto& ref : selection)
		if (ClientGameObject* obj = ref.get())
			ids.push_back(obj->id);
	EditorState::Undo u;
	withServer([&](Server& server) {
		for (uint32_t id : ids)
			if (ServerGameObject* obj = FindLive(server, id); obj && obj->parent && obj->blueprint->bpClass != Tags::GAMEOBJCLASS_PLAYER) {
				u.removed.push_back({ obj->blueprint, id, obj->parent->id, obj->position, obj->orientation });
				server.deleteObject(obj);
			}
	});
	if (u.removed.empty())
		return;
	ed.status = "Removed " + std::to_string(u.removed.size()) + " objects";
	ed.pushUndo(std::move(u));
}

void ClientInterface::editorProperties()
{
	if (!editor || selection.size() != 1)
		return;
	EditorState& ed = *editor;
	ClientGameObject* sel = selection.begin()->get();
	if (!sel)
		return;
	const uint32_t id = sel->id;
	EditorState::Props cur;
	std::string typeName;
	std::vector<std::pair<uint32_t, std::string>> players;
	bool found = false;
	withServer([&](Server& server) {
		ServerGameObject* obj = FindLive(server, id);
		if (!obj)
			return;
		found = true;
		cur = EditorState::readProps(obj);
		typeName = obj->blueprint->name;
		if (ServerGameObject* level = server.getLevel())
			for (const auto& [type, children] : level->children)
				for (CommonGameObject* child : children)
					if (child->blueprint->bpClass == Tags::GAMEOBJCLASS_PLAYER)
						players.emplace_back(child->id, std::to_string(child->id) + " " + child->blueprint->name);
	});
	if (!found)
		return;
	const auto& itemNames = client->gameSet->items.names;
	// each edit applies at once; a field keeps the state from when it was entered, and its
	// undo step goes in when it's left
	auto change = [&](auto&& f) {
		withServer([&](Server& server) {
			if (ServerGameObject* obj = FindLive(server, id))
				f(server, obj);
		});
	};
	auto began = [&] {
		if (ImGui::IsItemActivated())
			ed.editStart = cur;
	};
	auto ended = [&] {
		if (ImGui::IsItemDeactivatedAfterEdit()) {
			EditorState::Undo u;
			u.props.push_back(ed.editStart);
			ed.pushUndo(std::move(u));
		}
	};
	auto undoNow = [&] {
		EditorState::Undo u;
		u.props.push_back(cur);
		ed.pushUndo(std::move(u));
	};

	ImGui::Separator();
	ImGui::Text("%u %s", id, typeName.c_str());
	if (ed.nameOf != id) {
		ed.nameOf = id;
		snprintf(ed.nameBuf, sizeof(ed.nameBuf), "%s", cur.name.c_str());
	}
	if (ImGui::InputText("Name", ed.nameBuf, sizeof(ed.nameBuf))) {
		const std::string name = ed.nameBuf;
		change([&](Server&, ServerGameObject* obj) { obj->setName(name); });
	}
	began();
	ended();
	std::string owner;
	for (const auto& p : players)
		if (p.first == cur.parent)
			owner = p.second;
	if (!owner.empty() && ImGui::BeginCombo("Owner", owner.c_str())) {
		for (const auto& p : players)
			if (ImGui::Selectable(p.second.c_str(), p.first == cur.parent) && p.first != cur.parent) {
				undoNow();
				const uint32_t to = p.first;
				change([&](Server& server, ServerGameObject* obj) {
					if (ServerGameObject* player = FindLive(server, to))
						obj->setParent(player);
				});
			}
		ImGui::EndCombo();
	}
	ImGui::Text("Items (%zu)", cur.items.size());
	int removeItem = -1;
	for (const auto& [item, value] : cur.items) {
		ImGui::PushID(item);
		float v = value;
		if (ImGui::SmallButton("x"))
			removeItem = item;
		ImGui::SameLine();
		if (ImGui::InputFloat(itemNames.getString(item).c_str(), &v, 0.0f, 0.0f, "%.2f")) {
			const int it = item;
			change([&](Server&, ServerGameObject* obj) { obj->setItem(it, v); });
		}
		began();
		ended();
		ImGui::PopID();
	}
	if (removeItem >= 0) {
		undoNow();
		change([&](Server&, ServerGameObject* obj) {
			obj->setItem(removeItem, 0.0f);
			obj->items.erase(removeItem);
		});
	}
	// a new item, from the game set's list (with a filter), set to 1
	ImGui::InputText("Item filter", ed.itemFilter, sizeof(ed.itemFilter));
	const std::string addName = ed.addItem >= 0 ? itemNames.getString(ed.addItem) : std::string();
	if (ImGui::BeginCombo("New item", addName.c_str())) {
		for (size_t i = 0; i < itemNames.size(); i++) {
			const std::string& name = itemNames.getString((int)i);
			if ((ed.itemFilter[0] && !ContainsNoCase(name, ed.itemFilter)) || cur.items.count((int)i))
				continue;
			if (ImGui::Selectable(name.c_str(), (int)i == ed.addItem))
				ed.addItem = (int)i;
		}
		ImGui::EndCombo();
	}
	ImGui::SameLine();
	if (ImGui::Button("Add") && ed.addItem >= 0 && !cur.items.count(ed.addItem)) {
		undoNow();
		const int it = ed.addItem;
		change([&](Server&, ServerGameObject* obj) { obj->setItem(it, 1.0f); });
	}
}

void ClientInterface::editorStep(bool redoing)
{
	if (!editor)
		return;
	EditorState& ed = *editor;
	auto& from = redoing ? ed.redo : ed.undo;
	auto& to = redoing ? ed.undo : ed.redo;
	if (from.empty() || ed.grabbing)
		return;
	EditorState::Undo u = std::move(from.back());
	from.pop_back();
	// the state this step replaces, for the other list
	EditorState::Undo back;
	const char* what = "";
	if (!u.heights.empty() && client->terrain)
		back.heights.assign(client->terrain->vertices.begin(), client->terrain->vertices.end());
	else if (u.hasLakes && client->terrain) {
		back.hasLakes = true;
		back.lakes = client->terrain->lakes;
		back.lakeBits = client->terrain->lakeExtraBits;
	}
	else if (!u.tiles.empty() && client->terrain) {
		Terrain* trn = client->terrain;
		for (int i = 0; i < trn->width * trn->height; i++)
			back.tiles.push_back({ trn->tiles[i].texture, (uint8_t)trn->tiles[i].rot, (uint8_t)trn->tiles[i].xflip, (uint8_t)trn->tiles[i].zflip });
	}
	if (!u.placed.empty() || !u.removed.empty() || !u.moved.empty() || !u.props.empty()) {
		withServer([&](Server& server) {
			// name, owner, items: as they were
			for (const auto& p : u.props)
				if (ServerGameObject* obj = FindLive(server, ed.resolve(p.id))) {
					back.props.push_back(EditorState::readProps(obj));
					back.props.back().id = p.id;
					ed.applyProps(server, obj, p);
				}
			// placed: removed again
			for (uint32_t pid : u.placed)
				if (ServerGameObject* obj = FindLive(server, ed.resolve(pid)); obj && obj->parent) {
					back.removed.push_back({ obj->blueprint, obj->id, obj->parent->id, obj->position, obj->orientation });
					server.deleteObject(obj);
				}
			// removed: placed again (a new id, which steps naming the old one follow)
			for (const auto& r : u.removed)
				if (ServerGameObject* parent = FindLive(server, ed.resolve(r.parent)))
					if (ServerGameObject* obj = server.spawnObject(r.type, parent, r.position, r.orientation)) {
						if (obj->id != r.id)
							ed.respawned[r.id] = obj->id;
						back.placed.push_back(obj->id);
					}
			// moved or turned: back where it was
			for (const auto& m : u.moved)
				if (ServerGameObject* obj = FindLive(server, ed.resolve(m.id))) {
					back.moved.push_back({ obj->id, obj->position, obj->orientation });
					obj->setPosition(m.position);
					obj->setOrientation(m.orientation);
				}
		});
		what = !u.props.empty() ? "an object's properties" : !u.moved.empty() ? "objects moved or turned" : !u.placed.empty() ? "objects placed" : "objects removed";
	}
	to.push_back(std::move(back));
	if (to.size() > EditorState::MAX_UNDO)
		to.erase(to.begin());
	if (*what) {
		ed.status = std::string(redoing ? "Redone: " : "Undone: ") + what;
		return;
	}
	if (!u.heights.empty() && client->terrain && u.heights.size() == client->terrain->vertices.size()) {
		std::copy(u.heights.begin(), u.heights.end(), client->terrain->vertices.begin());
		withServer([&](Server& server) {
			if (server.terrain && server.terrain->vertices.size() == u.heights.size())
				std::copy(u.heights.begin(), u.heights.end(), server.terrain->vertices.begin());
			// the objects on the ground as it was
			NNSearch search;
			const auto area = server.terrain->getPlayableArea();
			search.start(&server, Vector3(area.first / 2, 0, area.second / 2), std::max(area.first, area.second));
			while (ServerGameObject* obj = (ServerGameObject*)search.next())
				obj->setPosition(Vector3(obj->position.x, server.terrain->getHeight(obj->position.x, obj->position.z), obj->position.z));
		});
		rebuildTerrainRenderer();
		if (!client->terrain->lakes.empty())
			setLakes(client->terrain->lakes, client->terrain->lakeExtraBits);
		ed.status = std::string(redoing ? "Redone" : "Undone") + ": a brush stroke";
	}
	else if (u.hasLakes && client->terrain) {
		setLakes(u.lakes, u.lakeBits);
		ed.status = std::string(redoing ? "Redone" : "Undone") + ": a lake change";
	}
	else if (!u.tiles.empty() && client->terrain && u.tiles.size() == (size_t)client->terrain->width * client->terrain->height) {
		Terrain* trn = client->terrain;
		for (size_t i = 0; i < u.tiles.size(); i++) {
			trn->tiles[i].texture = u.tiles[i].texture;
			trn->tiles[i].rot = u.tiles[i].rot;
			trn->tiles[i].xflip = u.tiles[i].xflip;
			trn->tiles[i].zflip = u.tiles[i].zflip;
		}
		withServer([&](Server& server) {
			Terrain* st = server.terrain;
			if (!st || st == trn || (size_t)st->width * st->height != u.tiles.size())
				return;
			for (size_t i = 0; i < u.tiles.size(); i++) {
				TerrainTexture* tex = u.tiles[i].texture;
				if (TerrainTexture* stex = tex ? FindTexture(st, tex->grp->name, tex->id) : nullptr)
					st->tiles[i].texture = stex;
				st->tiles[i].rot = u.tiles[i].rot;
				st->tiles[i].xflip = u.tiles[i].xflip;
				st->tiles[i].zflip = u.tiles[i].zflip;
			}
			ResetTileTerrainTypes(server);
		});
		rebuildTerrainRenderer(true);
		ed.status = std::string(redoing ? "Redone" : "Undone") + ": a texture stroke";
	}
}

std::vector<std::string> ClientInterface::editorCheck()
{
	std::vector<std::string> warnings;
	withServer([&](Server& server) {
		ServerGameObject* level = server.getLevel();
		if (!level)
			return;
		for (const auto& [type, children] : level->children)
			for (CommonGameObject* child : children) {
				ServerGameObject* player = (ServerGameObject*)child;
				// the neutral and the barbarians' (Baresark) players start empty
				if (player->blueprint->bpClass != Tags::GAMEOBJCLASS_PLAYER || player->blueprint->name == "Neutral Player" || player->blueprint->name == "Baresark Player")
					continue;
				size_t objects = 0;
				for (const auto& [t, c] : player->children)
					objects += c.size();
				const std::string who = std::to_string(player->id) + " " + player->blueprint->name;
				if (player->playerStartCameraPosition == Vector3(0, 0, 0))
					warnings.push_back(who + ": no start camera (the player starts at the map's corner)");
				if (objects == 0)
					warnings.push_back(who + ": no units or buildings");
			}
		// reachability by land: from the first player's start, over the ground a Peasant can
		// stand on (buildings and walls left out: they open), every other player's start
		const GameObjBlueprint* peasant = server.gameSet->findBlueprint(Tags::GAMEOBJCLASS_CHARACTER, "Peasant");
		if (!peasant || !server.terrain)
			return;
		const auto area = server.terrain->getNumPlayableTiles();
		struct Start { std::string who; int x, z; };
		std::vector<Start> starts;
		for (const auto& [type, children] : level->children)
			for (CommonGameObject* child : children) {
				ServerGameObject* player = (ServerGameObject*)child;
				if (player->blueprint->bpClass != Tags::GAMEOBJCLASS_PLAYER || player->blueprint->name == "Neutral Player" || player->blueprint->name == "Baresark Player")
					continue;
				// its first building, else its first unit
				CommonGameObject* home = nullptr;
				for (const auto& [t, c] : player->children)
					for (CommonGameObject* o : c)
						if (!home || (o->blueprint->bpClass == Tags::GAMEOBJCLASS_BUILDING && home->blueprint->bpClass != Tags::GAMEOBJCLASS_BUILDING))
							home = o;
				if (!home)
					continue;
				// the nearest tile a Peasant can stand on, within 10 tiles
				const int hx = (int)(home->position.x / 5), hz = (int)(home->position.z / 5);
				int bx = -1, bz = -1;
				for (int r = 0; r <= 10 && bx < 0; r++)
					for (int dz = -r; dz <= r && bx < 0; dz++)
						for (int dx = -r; dx <= r; dx++)
							if ((std::abs(dx) == r || std::abs(dz) == r) && !MovementController::isTerrainBlocked(peasant, hx + dx, hz + dz)) {
								bx = hx + dx;
								bz = hz + dz;
								break;
							}
				if (bx >= 0)
					starts.push_back({ std::to_string(player->id) + " " + player->blueprint->name, bx, bz });
			}
		if (starts.size() < 2)
			return;
		std::vector<uint8_t> reached((size_t)area.first * area.second, 0);
		std::vector<std::pair<int, int>> open{ { starts[0].x, starts[0].z } };
		reached[(size_t)starts[0].z * area.first + starts[0].x] = 1;
		while (!open.empty()) {
			const auto [x, z] = open.back();
			open.pop_back();
			static const int dirs[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
			for (const auto& d : dirs) {
				const int nx = x + d[0], nz = z + d[1];
				if (nx < 0 || nz < 0 || nx >= area.first || nz >= area.second || reached[(size_t)nz * area.first + nx])
					continue;
				if (MovementController::isTerrainBlocked(peasant, nx, nz))
					continue;
				reached[(size_t)nz * area.first + nx] = 1;
				open.push_back({ nx, nz });
			}
		}
		for (size_t i = 1; i < starts.size(); i++)
			if (!reached[(size_t)starts[i].z * area.first + starts[i].x])
				warnings.push_back(starts[i].who + ": can't be reached by land from " + starts[0].who);
	});
	if (warnings.empty())
		warnings.push_back("No problems found");
	return warnings;
}

bool ClientInterface::editorSave(const std::string& name)
{
	EditorState& ed = *editor;
	bool savedOk = false;
		if (name.empty() || name.find_first_of("\\/:*?\"<>|") != std::string::npos)
			ed.status = "Not a valid name";
		else
			withServer([&](Server& server) {
				if (!server.terrain) {
					ed.status = "No terrain";
					return;
				}
				// the map: Maps\<name>\<name>.bcm, and its texture translation under the new name
				const std::string bcm = "Maps\\" + name + "\\" + name + ".bcm";
				const std::string oldLttt = server.terrain->texDb.getLTTTPath(server.mapFile);
				const std::string newLttt = server.terrain->texDb.getLTTTPath(bcm);
				const auto saved = g_userPath / "saved";
				bool ok = server.terrain->writeBCM(saved / PathFromUtf8(portablePath(bcm)), true);
				if (ok && FileExists(oldLttt.c_str())) {
					char* data; int size;
					LoadFile(oldLttt.c_str(), &data, &size);
					const auto to = saved / PathFromUtf8(portablePath(newLttt));
					std::error_code ec;
					std::filesystem::create_directories(to.parent_path(), ec);
					std::ofstream f(to, std::ios::binary);
					f.write(data, size);
					free(data);
					ok = (bool)f;
				}
				// the level, with the new map
				const std::string lvl = PathToUtf8(saved / "Save_Games" / PathFromUtf8(name + ".lvl"));
				if (ok) {
					server.mapFile = bcm;
					ok = server.saveGame(lvl.c_str());
				}
				ed.status = ok ? "Saved " + name + ".lvl (in Skirmish)" : "Saving failed";
				savedOk = ok;
				printf("Map editor: %s %s\n", lvl.c_str(), ok ? "saved" : "failed");
			});
	return savedOk;
}

// Test runs ("benchmark": { ..., "editorTest": true }): a raise stroke where the camera looks,
// then a building beside it, then the map saved as "Editor Test", by the same input as a player's
void ClientInterface::editorOpenMode(const std::string& mode)
{
	if (!editor)
		editor = std::make_shared<EditorState>();
	static const std::pair<const char*, int> modes[] = { { "terrain", EditorState::TERRAIN }, { "textures", EditorState::TEXTURES },
		{ "water", EditorState::WATER }, { "objects", EditorState::OBJECTS }, { "players", EditorState::PLAYERS }, { "zones", EditorState::ZONES } };
	for (const auto& [name, m] : modes)
		if (mode == name)
			editor->mode = m;
}

void ClientInterface::editorTestStep(Vector3& cursor)
{
	if (!editor)
		editor = std::make_shared<EditorState>();
	EditorState& ed = *editor;
	static int frame = 0;
	static Vector3 target;
	if (!client->getLevel() || !client->terrain)
		return;
	frame++;
	if (frame == 30) {
		// 60 m ahead of the camera, on the ground
		const Vector3 ahead = Vector3(client->camera.direction.x, 0, client->camera.direction.z).normal();
		target = client->camera.position + ahead * 60.0f;
		target.y = client->terrain->getHeight(target.x, target.z);
		const int x = (int)(target.x / 5.0f) + client->terrain->edge, z = (int)(target.z / 5.0f) + client->terrain->edge;
		printf("EDITORTEST height before %d at (%.0f, %.0f)\n", client->terrain->vertices[z * (client->terrain->width + 1) + x], target.x, target.z);
		ed.mode = EditorState::TERRAIN;
		ed.tool = EditorState::RAISE;
		ed.radius = 30.0f;
		ed.strength = 60.0f;
	}
	if (frame >= 30 && frame < 120) {
		cursor = target;
		g_mouseDown[SDL_BUTTON_LEFT] = true;
	}
	if (frame == 121) {
		const int x = (int)(target.x / 5.0f) + client->terrain->edge, z = (int)(target.z / 5.0f) + client->terrain->edge;
		int serverHeight = -1;
		withServer([&](Server& server) { serverHeight = server.terrain->vertices[z * (server.terrain->width + 1) + x]; });
		printf("EDITORTEST height after %d (server %d)\n", client->terrain->vertices[z * (client->terrain->width + 1) + x], serverHeight);
		// then a texture stroke there: the first texture of another group than the tile's
		ed.mode = EditorState::TEXTURES;
		ed.radius = 10.0f;
		const TerrainTile* tile = client->terrain->getTile(x, z);
		for (const auto& [name, g] : client->terrain->texDb.groups)
			if (g && !g->textures.empty() && g.get() != tile->texture->grp) {
				ed.paintGroup = name;
				ed.paintId = g->textures.begin()->first;
				break;
			}
		printf("EDITORTEST tile before %s %d, painting %s %d\n", tile->texture->grp->name.c_str(), tile->texture->id, ed.paintGroup.c_str(), ed.paintId);
	}
	if (frame >= 122 && frame < 126) {
		cursor = target;
		g_mouseDown[SDL_BUTTON_LEFT] = true;
	}
	if (frame == 127) {
		const int x = (int)(target.x / 5.0f) + client->terrain->edge, z = (int)(target.z / 5.0f) + client->terrain->edge;
		const TerrainTile* tile = client->terrain->getTile(x, z);
		std::string serverTile;
		withServer([&](Server& server) {
			if (const TerrainTile* st = server.terrain->getTile(x, z))
				serverTile = st->texture->grp->name + " " + std::to_string(st->texture->id);
		});
		printf("EDITORTEST tile after %s %d (server %s)\n", tile->texture->grp->name.c_str(), tile->texture->id, serverTile.c_str());
		// then a lake 1 m deep, 50 m to the side
		ed.mode = EditorState::WATER;
		ed.waterTool = EditorState::ADD_LAKE;
		ed.waterDepth = 1.0f;
		cursor = target + Vector3(-50, 0, 0);
		g_mousePressed[SDL_BUTTON_LEFT] = true;
	}
	if (frame == 128) {
		const Vector3 lake = target + Vector3(-50, 0, 0);
		const int x = (int)(lake.x / 5.0f) + client->terrain->edge, z = (int)(lake.z / 5.0f) + client->terrain->edge;
		int flooded = 0, serverFlooded = 0;
		for (int i = 0; i < client->terrain->width * client->terrain->height; i++)
			flooded += client->terrain->tiles[i].fullOfWater;
		bool serverWater = false;
		withServer([&](Server& server) {
			serverWater = server.terrain->getTile(x, z)->fullOfWater;
			for (int i = 0; i < server.terrain->width * server.terrain->height; i++)
				serverFlooded += server.terrain->tiles[i].fullOfWater;
		});
		printf("EDITORTEST lake: %zu lakes, water at the click %d (server %d), %d tiles under water (server %d)\n", client->terrain->lakes.size(),
			(int)client->terrain->getTile(x, z)->fullOfWater, (int)serverWater, flooded, serverFlooded);
	}
	if (frame == 130) {
		ed.mode = EditorState::OBJECTS;
		ed.placeClass = Tags::GAMEOBJCLASS_BUILDING;
		ed.placeType = client->gameSet->objBlueprints[ed.placeClass].names.getIndex("Windmill");
		if (ClientGameObject* level = client->getLevel())
			for (const auto& [type, children] : level->children)
				for (CommonGameObject* child : children)
					if (child->blueprint->bpClass == Tags::GAMEOBJCLASS_PLAYER && child->blueprint->name != "Neutral Player" && !ed.placePlayer)
						ed.placePlayer = child->id;
		cursor = target + Vector3(40, 0, 0);
		g_mousePressed[SDL_BUTTON_LEFT] = true;
		printf("EDITORTEST placing a Windmill (type %d) for player %u\n", ed.placeType, ed.placePlayer);
	}
	if (frame == 140) {
		// then a zone, painted 10 m around the hill's top
		ed.mode = EditorState::ZONES;
		ed.zoneTool = EditorState::ADD_TILES;
		ed.radius = 10.0f;
		withServer([&](Server& server) {
			const auto& types = server.gameSet->objBlueprints[Tags::GAMEOBJCLASS_TERRAIN_ZONE];
			if (ServerGameObject* level = server.getLevel(); level && types.size())
				if (ServerGameObject* zone = server.spawnObject(&types[0], level, Vector3(0, 0, 0), Vector3(0, 0, 0))) {
					zone->name = "Editor Test Zone";
					ed.zoneId = zone->id;
				}
		});
	}
	if (frame >= 141 && frame < 145) {
		cursor = target;
		g_mouseDown[SDL_BUTTON_LEFT] = true;
	}
	if (frame == 146) {
		size_t tiles = 0;
		bool tileInZone = false;
		withServer([&](Server& server) {
			if (ServerGameObject* zone = server.findObject(ed.zoneId)) {
				tiles = zone->zoneTiles.size();
				const auto area = server.terrain->getNumPlayableTiles();
				tileInZone = server.tiles[(int)(target.z / 5.0f) * area.first + (int)(target.x / 5.0f)].zone.getFrom<Server>() == zone;
			}
		});
		printf("EDITORTEST zone: %zu tiles, the hill's tile in it %d\n", tiles, (int)tileInZone);
	}
	// the windmill selected, moved 20 m, turned 90 degrees and removed; the three undone, done
	// again and undone again: it stands where it was placed
	static uint32_t windmill = 0;
	auto showWindmill = [&](const char* when) {
		const uint32_t id = ed.resolve(windmill);
		bool there = false;
		Vector3 p, o;
		withServer([&](Server& server) {
			if (ServerGameObject* obj = FindLive(server, id)) {
				there = true;
				p = obj->position;
				o = obj->orientation;
			}
		});
		if (there)
			printf("EDITORTEST windmill %s: %u at (%.1f, %.1f), facing %.0f; %zu undo, %zu redo\n", when, id, p.x, p.z, o.y * 180.0f / 3.14159265f, ed.undo.size(), ed.redo.size());
		else
			printf("EDITORTEST windmill %s: gone; %zu undo, %zu redo\n", when, ed.undo.size(), ed.redo.size());
	};
	if (frame == 147 && !ed.undo.empty() && !ed.undo.back().placed.empty()) {
		windmill = ed.undo.back().placed[0];
		showWindmill("placed");
		ed.mode = EditorState::OBJECTS;
		ed.objectTool = EditorState::SELECT;
		selection.clear();
		if (ClientGameObject* obj = client->findObject(windmill))
			selection.insert(obj);
		editorGrab();
	}
	if (windmill && (frame == 148 || frame == 149)) {
		cursor = ed.grabFrom + Vector3(20, 0, 0);
		if (frame == 149)
			g_mousePressed[SDL_BUTTON_LEFT] = true;
	}
	if (windmill && frame == 150) {
		showWindmill("moved");
		editorTurn(90.0f);
		showWindmill("turned");
		editorDeleteSelection();
		showWindmill("removed");
		for (int i = 0; i < 3; i++)
			editorUndo();
		showWindmill("after 3 undos");
		for (int i = 0; i < 3; i++)
			editorRedo();
		showWindmill("after 3 redos");
		for (int i = 0; i < 3; i++)
			editorUndo();
		showWindmill("after 3 more undos");
		ed.objectTool = EditorState::PLACE;
	}
	// its name, owner and an item changed as the properties panel does (the state before kept
	// for undo), then undone and done again
	if (windmill && frame == 151) {
		auto showProps = [&](const char* when) {
			withServer([&](Server& server) {
				if (ServerGameObject* obj = FindLive(server, ed.resolve(windmill)))
					printf("EDITORTEST windmill %s: name \"%s\", owner %u, %zu items, \"Food Production\" %.1f\n", when, obj->name.c_str(),
						obj->parent ? obj->parent->id : 0, obj->items.size(), obj->getItem(server.gameSet->items.names.getIndex("Food Production")));
			});
		};
		showProps("before");
		EditorState::Undo u;
		withServer([&](Server& server) {
			ServerGameObject* obj = FindLive(server, ed.resolve(windmill));
			ServerGameObject* other = nullptr;
			if (ServerGameObject* level = server.getLevel())
				for (const auto& [type, children] : level->children)
					for (CommonGameObject* child : children)
						if (child->blueprint->bpClass == Tags::GAMEOBJCLASS_PLAYER && child != obj->parent && child->blueprint->name != "Neutral Player" && !other)
							other = (ServerGameObject*)child;
			u.props.push_back(EditorState::readProps(obj));
			obj->setName("Test Mill");
			obj->setItem(server.gameSet->items.names.getIndex("Food Production"), 7.0f);
			if (other)
				obj->setParent(other);
		});
		ed.pushUndo(std::move(u));
		showProps("edited");
		editorUndo();
		showProps("undone");
		editorRedo();
		showProps("redone");
		editorUndo();
		showProps("undone again");
	}
	if (frame == 160) {
		editorSave("Editor Test");
		printf("EDITORTEST %s\n", ed.status.c_str());
	}
	if (frame == 170) {
		// undo the windmill, the lake, the texture stroke, then the height stroke: the ground as before
		const size_t steps = ed.undo.size();
		for (int i = 0; i < 4; i++)
			editorUndo();
		const int x = (int)(target.x / 5.0f) + client->terrain->edge, z = (int)(target.z / 5.0f) + client->terrain->edge;
		const TerrainTile* tile = client->terrain->getTile(x, z);
		printf("EDITORTEST undo: %zu steps before, height now %d, tile %s %d, %zu lakes\n", steps, client->terrain->vertices[z * (client->terrain->width + 1) + x],
			tile->texture->grp->name.c_str(), tile->texture->id, client->terrain->lakes.size());
	}
	if (frame == 180)
		for (const std::string& w : editorCheck())
			printf("EDITORTEST check: %s\n", w.c_str());
}
