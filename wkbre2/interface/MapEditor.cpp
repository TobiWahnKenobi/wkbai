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
}

struct ClientInterface::EditorState {
	enum Mode { TERRAIN, OBJECTS };
	enum Tool { RAISE, LOWER, SMOOTH, FLATTEN };
	int mode = TERRAIN, tool = RAISE;
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
};

void ClientInterface::rebuildTerrainRenderer()
{
	if (terrainRenderer && terrainRenderer->updateHeights())
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

	if (ed.mode == EditorState::OBJECTS) {
		if (overUi || !g_mousePressed[SDL_BUTTON_LEFT] || ed.placeType < 0)
			return false;
		const GameObjBlueprint* type = &client->gameSet->objBlueprints[ed.placeClass][ed.placeType];
		const uint32_t playerId = ed.placePlayer;
		const Vector3 at = cursor;
		const float yaw = ed.placeYaw;
		withServer([&](Server& server) {
			ServerGameObject* player = server.findObject(playerId);
			if (!player)
				return;
			Vector3 pos = at;
			pos.y = server.terrain->getHeight(pos.x, pos.z);
			server.spawnObject(type, player, pos, Vector3(0, yaw, 0));
		});
		ed.status = "Placed " + type->name;
		return true;
	}

	// terrain brush: while the left button is held
	const bool down = g_mouseDown[SDL_BUTTON_LEFT] && (ed.stroke || !overUi);
	if (down && !ed.stroke) {
		ed.stroke = true;
		ed.lastApply = now;
		ed.flattenTo = trn->getHeight(cursor.x, cursor.z) / trn->scale;
		ed.strokeMin = ed.strokeMax = cursor;
		ed.pending.assign(trn->vertices.size(), 0.0f);
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
		ImGui::RadioButton("Objects", &ed.mode, EditorState::OBJECTS);
		ImGui::Separator();
		if (ed.mode == EditorState::TERRAIN) {
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
		if (ImGui::Button("Save map"))
			editorSave(ed.saveName);
		if (!ed.status.empty())
			ImGui::TextWrapped("%s", ed.status.c_str());
	}
	ImGui::End();
	if (!open)
		editorOn = false;
}

void ClientInterface::editorSave(const std::string& name)
{
	EditorState& ed = *editor;
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
				bool ok = server.terrain->writeBCM(saved / PathFromUtf8(portablePath(bcm)));
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
				printf("Map editor: %s %s\n", lvl.c_str(), ok ? "saved" : "failed");
			});
	
}

// Test runs ("benchmark": { ..., "editorTest": true }): a raise stroke where the camera looks,
// then a building beside it, then the map saved as "Editor Test", by the same input as a player's
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
	if (frame == 160) {
		editorSave("Editor Test");
		printf("EDITORTEST %s\n", ed.status.c_str());
	}
}
