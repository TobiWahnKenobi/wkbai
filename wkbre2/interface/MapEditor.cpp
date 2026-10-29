// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

// The map editor: terrain heights (raise, lower, smooth, flatten), placing objects of any type
// for any player, and saving the result as a map (.bcm, the original's format: everything but the
// heights stays as in the map it came from) and a level (.lvl) in the user folder, where the
// skirmish menu finds it. F8 switches it on and off in single player.

#include "ClientInterface.h"
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

	// undo: a stroke's heights or tiles before it, or an object placed (its id)
	struct TileState {
		TerrainTexture* texture;
		uint8_t rot, xflip, zflip;
	};
	struct Undo {
		std::vector<uint8_t> heights;
		uint32_t placed = 0;
		std::vector<TileState> tiles;
		bool hasLakes = false;
		std::vector<Vector3> lakes;
		std::vector<uint8_t> lakeBits;
	};
	std::vector<Undo> undo;
	static constexpr size_t MAX_UNDO = 30;
	void pushUndo(Undo u) {
		undo.push_back(std::move(u));
		if (undo.size() > MAX_UNDO)
			undo.erase(undo.begin());
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
		editorUndo();

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
		if (placed)
			ed.pushUndo({ {}, placed });
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
		ed.pushUndo({ std::vector<uint8_t>(trn->vertices.begin(), trn->vertices.end()), 0 });
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
		ImGui::Separator();
		ImGui::InputText("Map name", ed.saveName, sizeof(ed.saveName));
		if (ImGui::Button("Undo (Ctrl+Z)"))
			editorUndo();
		ImGui::SameLine();
		ImGui::TextDisabled("%zu steps", ed.undo.size());
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
		for (const std::string& w : ed.warnings)
			ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "%s", w.c_str());
		if (!ed.status.empty())
			ImGui::TextWrapped("%s", ed.status.c_str());
	}
	ImGui::End();
	if (!open)
		editorOn = false;
}

void ClientInterface::editorUndo()
{
	if (!editor || editor->undo.empty())
		return;
	EditorState& ed = *editor;
	EditorState::Undo u = std::move(ed.undo.back());
	ed.undo.pop_back();
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
		ed.status = "Undone: a brush stroke";
	}
	else if (u.hasLakes && client->terrain) {
		setLakes(u.lakes, u.lakeBits);
		ed.status = "Undone: a lake change";
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
		ed.status = "Undone: a texture stroke";
	}
	else if (u.placed) {
		if (ClientGameObject* obj = client->findObject(u.placed))
			client->sendTerminateObject(obj);
		ed.status = "Undone: an object placed";
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
