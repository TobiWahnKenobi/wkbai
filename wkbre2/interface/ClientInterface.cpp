// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#include "ClientInterface.h"
#include "../gfx/TerrainRenderer.h"
#include "../gfx/DefaultTerrainRenderer.h"
#include "../client.h"
#include "../gfx/renderer.h"
#include "ClientDebugger.h"
#include "../imguiimpl.h"
#include "../window.h"
#include <SDL3/SDL_scancode.h>
#include <SDL3/SDL_mouse.h>
#include "../tags.h"
#include "../gameset/gameset.h"
#include "../gfx/SceneRenderer.h"
#include "../gfx/DefaultSceneRenderer.h"
#include "../imgui/imgui.h"
#include <ctime>
#include "../terrain.h"
#include <SDL3/SDL_timer.h>
#include "../gameset/ScriptContext.h"
#include <stdexcept>
#include "../ParticleContainer.h"
#include "../ParticleSystem.h"
#include "../gfx/DefaultParticleRenderer.h"
#include "../settings.h"
#include <nlohmann/json.hpp>
#include "../gfx/renderer_diligent.h"
#include "../gfx/DiligentEnhancedSceneRenderer.h"
#include "../gfx/DiligentEnhancedTerrainRenderer.h"
#include "../gfx/DiligentSkyRenderer.h"
#include "../gfx/RenderSettings.h"
#include "../StampdownPlan.h"
#include "../gfx/TerrainSpriteContainer.h"
#include "../gfx/TerrainSpriteRenderer.h"
#include "../gfx/bitmap.h"
#include "../Mesh.h"
#include "../file.h"

#define SOL_NO_CHECK_NUMBER_PRECISION 1
#include <sol/sol.hpp>
#include "FrontEnd.h"
#include "LuaScripts.h"
#include "../gameset/finder.h"
#include "../gameset/Footprint.h"
#include "../server.h"
#include "../gameset/OrderBlueprint.h"
#include <tracy/Tracy.hpp>
#include <tracy/TracyC.h>
#include "../util/FrameStats.h"

namespace {
	Vector3 getRay(const Camera &cam) {
		//float ay = (1 - g_mouseX * 2.0f / g_windowWidth) * 0.45 * g_windowWidth / g_windowHeight;
		//float ax = (1 - g_mouseY * 2.0f / g_windowHeight) * 0.45;
		//Vector3 direction = Vector3(
		//	-sin(cam.orientation.y + ay)*cos(cam.orientation.x + ax),
		//	sin(cam.orientation.x + ax),
		//	cos(cam.orientation.y + ay)*cos(cam.orientation.x + ax)
		//);
		//return direction;
		const float zNear = 0.1f;
		Vector3 xvec = cam.direction.normal().cross(Vector3(0, 1, 0)).normal();
		Vector3 yvec = cam.direction.normal().cross(xvec).normal();
		//Vector3 nearori = cam.position + cam.direction.normal() * zNear;
		float ys = tan(cam.fovY * 0.5f) * zNear; // half the vertical field of view
		yvec *= ys;
		xvec *= ys * g_windowWidth / g_windowHeight;
		yvec *= (1 - g_mouseY * 2.0f / g_windowHeight);
		xvec *= (1 - g_mouseX * 2.0f / g_windowWidth);
		//Vector3 posonnear = nearori + xvec - yvec;
		//return posonnear - cam.position;
		return cam.direction.normal() * zNear + xvec - yvec;
	}

	int InMap(const Vector3 &v, const Terrain &map)
	{
		//printf("%f < %f < %f\n", (float)(-(int)map.edge * 5), v.x, (float)((map.width - map.edge) * 5));
		if ((v.x >= -(map.edge * 5.0f)) && (v.x < ((map.width - map.edge) * 5.0f)))
			if ((v.z >= -(map.edge * 5.0f)) && (v.z < ((map.height - map.edge) * 5.0f)))
				return 1;
		return 0;
	}
	
	Vector3 CalcStampdownPos(const Vector3 &raystart, const Vector3 &raydir, const Terrain &terrain)
	{
		float lv = 4;

		Vector3 ptt = raystart, vta = raydir.normal() * lv;

		float h;
		const float farzvalue = 250.0f;
		int nlp = static_cast<int>(farzvalue * 1.5f / lv);
		int m = (ptt.y < terrain.getHeight(ptt.x, ptt.z)) ? 0 : 1;

		for (int i = 0; i < nlp; i++) {
			ptt += vta;
			if (!InMap(ptt, terrain)) continue;
			h = terrain.getHeight(ptt.x, ptt.z);

			if (ptt.y == h)
				break;

			if (m ^ ((ptt.y > h) ? 1 : 0)) {
				vta *= -0.5f; m = 1 - m;
			}
		}

		if (InMap(ptt, terrain)) {
			Vector3 mapstdownpos = ptt; mapstdownpos.y = h; //mapstdownvalid = 1;
			return mapstdownpos;
		}
		else
			return Vector3(0, 0, 0);
		//if (mapstdownvalid && InLevel(mapstdownpos)) {
		//	stdownpos = mapstdownpos; stdownvalid = 1;
		//}
		//else	stdownvalid = 0;
	}

	std::optional<Vector3> RayIntersectsSphere(const Vector3 &raystart, const Vector3 &raydir, const Vector3 &center, float radius)
	{
		const Vector3 rdnorm = raydir.normal();
		const Vector3 smc = raystart - center;
		const float ddt = rdnorm.dot(smc);
		
		const float delta = ddt * ddt - (smc.sqlen3() - radius * radius);
		if (delta < 0.0f)
			return std::nullopt;

		const float k1 = -ddt + std::sqrt(delta);
		const float k2 = -ddt - std::sqrt(delta);
		if (k1 < 0.0f && k2 < 0.0f)
			return std::nullopt;
		if (k1 >= 0.0f && k2 >= 0.0f)
			return raystart + raydir * std::min(k1, k2);
		return raystart + raydir * std::max(k1, k2);
	}

	std::optional<Vector3> getRayTriangleIntersection(const Vector3& rayStart, const Vector3& _rayDir, const Vector3& p1, const Vector3& p2, const Vector3& p3) {
		Vector3 rayDir = _rayDir.normal();
		Vector3 v2 = p2 - p1, v3 = p3 - p1;
		Vector3 trinorm = v2.cross(v3).normal(); // order?
		if (trinorm == Vector3(0, 0, 0))
			return std::nullopt;
		float rayDir_dot_trinorm = rayDir.dot(trinorm);
		if (rayDir_dot_trinorm < 0.0f)
			return std::nullopt;
		float p = p1.dot(trinorm);
		float alpha = (p - rayStart.dot(trinorm)) / rayDir_dot_trinorm;
		if (alpha < 0.0f)
			return std::nullopt;
		Vector3 intersectingPoint = rayStart + rayDir * alpha;

		Vector3 c = intersectingPoint - p1;
		float d = v2.sqlen3() * v3.sqlen3() - v2.dot(v3) * v2.dot(v3);
		//assert(d != 0.0f);
		float a = (c.dot(v2) * v3.sqlen3() - c.dot(v3) * v2.dot(v3)) / d;
		float b = (c.dot(v3) * v2.sqlen3() - c.dot(v2) * v3.dot(v2)) / d;
		if (a >= 0.0f && b >= 0.0f && (a + b) <= 1.0f)
			return intersectingPoint;
		else
			return std::nullopt;
	}
}

bool ClientInterface::drawMinimap(int x, int y, int w, int h)
{
	const Terrain* terrain = client->terrain;
	if (!terrain || w <= 0 || h <= 0)
		return false;

	if (minimapTerrain != terrain) {
		if (minimapTexture)
			gfx->FreeTexture(minimapTexture);
		minimapTexture = nullptr;
		if (!terrain->minimapRGBA.empty()) {
			Bitmap bmp;
			bmp.width = terrain->minimapWidth;
			bmp.height = terrain->minimapHeight;
			bmp.format = BMFORMAT_R8G8B8A8;
			bmp.pixels.resize(terrain->minimapRGBA.size());
			memcpy(bmp.pixels.data(), terrain->minimapRGBA.data(), terrain->minimapRGBA.size());
			minimapTexture = gfx->CreateTexture(bmp, 1);
		}
		minimapTerrain = terrain;
	}

	ImDrawList* drawList = ImGui::GetBackgroundDrawList();
	const ImVec2 p0((float)x, (float)y), p1((float)(x + w), (float)(y + h));
	// Image row 0 is the north edge (largest z) of the playable area
	const auto [areaWidth, areaDepth] = terrain->getPlayableArea();
	if (minimapTexture)
		drawList->AddImage(minimapTexture, p0, p1);
	else
		drawList->AddRectFilled(p0, p1, IM_COL32(0, 0, 0, 255));

	// A non-square map is letterboxed in the square image, centred with dark bars (e.g.
	// Sutherlands Siege, 150x250 tiles), and the original shows it that way: the map part is
	// this rectangle of the widget.
	float mapX = (float)x, mapY = (float)y, mapW = (float)w, mapH = (float)h;
	if (areaWidth < areaDepth) {
		mapW = w * areaWidth / areaDepth;
		mapX = x + (w - mapW) * 0.5f;
	}
	else if (areaDepth < areaWidth) {
		mapH = h * areaDepth / areaWidth;
		mapY = y + (h - mapH) * 0.5f;
	}
	auto toMinimap = [&](float px, float pz) {
		return ImVec2(mapX + std::clamp(px / areaWidth, 0.0f, 1.0f) * mapW, mapY + std::clamp(1.0f - pz / areaDepth, 0.0f, 1.0f) * mapH);
	};

	// Objects whose blueprint has DISPLAY_ON_MINIMAP, in their player's colour
	// (colours taken from the HUD's Interface/InGame/Top Panel/Player<n>.tga)
	static const ImU32 playerColors[9] = {
		IM_COL32(200, 200, 200, 255), IM_COL32(114, 97, 187, 255), IM_COL32(212, 198, 74, 255),
		IM_COL32(226, 50, 55, 255), IM_COL32(93, 155, 57, 255), IM_COL32(212, 91, 173, 255),
		IM_COL32(204, 99, 55, 255), IM_COL32(103, 160, 182, 255), IM_COL32(180, 74, 32, 255),
	};
	static const ImU32 treeColor = IM_COL32(28, 72, 24, 255);
	auto drawDots = [&](ClientGameObject* obj, const auto& self) -> void {
		for (const auto& [type, children] : obj->children)
			for (CommonGameObject* child : children)
				self((ClientGameObject*)child, self);
		if (!obj->blueprint || !obj->blueprint->displayOnMinimap || (obj->flags & ClientGameObject::fTerminated))
			return;
		// Hidden when all its display categories are toggled off
		const uint8_t categories = obj->blueprint->minimapCategories;
		if (categories && !(categories & ~minimapHiddenCategories))
			return;
		CommonGameObject* player = obj->getPlayer();
		int color = player ? player->color : 0;
		ImU32 dotColor = playerColors[(color >= 0 && color < 9) ? color : 0];
		if (color == 0 && obj->blueprint->bpClass == Tags::GAMEOBJCLASS_CONTAINER)
			dotColor = treeColor; // neutral resources such as trees
		const float size = (obj->blueprint->bpClass == Tags::GAMEOBJCLASS_BUILDING) ? 2.5f : 1.5f;
		ImVec2 c = toMinimap(obj->position.x, obj->position.z);
		drawList->AddRectFilled(ImVec2(c.x - size, c.y - size), ImVec2(c.x + size, c.y + size), dotColor);
	};
	if (client->getLevel())
		drawDots(client->getLevel(), drawDots);

	// Where the camera looks: its view direction hits the ground at about the camera's height
	const Camera& cam = client->camera;
	Vector3 lookAt = cam.position;
	if (cam.direction.y < -0.01f) {
		float groundY = terrain->getHeight(cam.position.x, cam.position.z);
		float t = (cam.position.y - groundY) / -cam.direction.y;
		lookAt = cam.position + cam.direction * t;
	}
	// The camera marker of the original: the flat mesh Interface\Mini_Map_Arrow4.mesh3 (tip at
	// +y), placed at the camera's position and pointing along its view. Its size follows the
	// minimap's (one mesh unit ≈ 0.16 of the side, measured on the original's frame).
	static const std::vector<Vector3> arrowTriangles = [] {
		std::vector<Vector3> tris;
		const char* path = "Interface\\Mini_Map_Arrow4.mesh3";
		if (!FileExists(path))
			return tris;
		Mesh mesh(path);
		for (size_t g = 0; g < mesh.polyLists[0].groups.size() && g < mesh.groupIndices.size(); g++)
			for (const auto& tri : mesh.polyLists[0].groups[g].tupleIndex)
				for (uint16_t t : tri)
					tris.push_back(*(const Vector3*)(mesh.vertices.data() + 3 * mesh.groupIndices[g][t].vertex));
		return tris;
	}();
	float forwardX = cam.direction.x, forwardZ = cam.direction.z;
	const float forwardLen = std::sqrt(forwardX * forwardX + forwardZ * forwardZ);
	if (forwardLen > 1e-4f && !arrowTriangles.empty()) {
		forwardX /= forwardLen; forwardZ /= forwardLen;
		const float unit = 0.16f * std::min(w, h);
		const ImVec2 origin = toMinimap(cam.position.x, cam.position.z);
		// Screen axes: forward is the view direction (minimap y grows southwards), right is
		// 90° clockwise from it.
		const ImVec2 sf(forwardX * unit, -forwardZ * unit), sr(forwardZ * unit, forwardX * unit);
		auto toScreen = [&](const Vector3& v) { return ImVec2(origin.x + v.x * sr.x + v.y * sf.x, origin.y + v.x * sr.y + v.y * sf.y); };
		drawList->PushClipRect(p0, p1, true);
		for (size_t i = 0; i + 2 < arrowTriangles.size(); i += 3)
			drawList->AddTriangleFilled(toScreen(arrowTriangles[i]), toScreen(arrowTriangles[i + 1]), toScreen(arrowTriangles[i + 2]), IM_COL32(255, 255, 255, 255));
		drawList->PopClipRect();
	}

	// Click or drag on the minimap: move the camera so it looks at that point
	const bool hovered = g_mouseX >= x && g_mouseX < x + w && g_mouseY >= y && g_mouseY < y + h;
	if (hovered && g_mouseDown[SDL_BUTTON_LEFT]) {
		// in the map part; a click on the bars goes to the nearest map edge
		float targetX = std::clamp((g_mouseX - mapX) / mapW, 0.0f, 1.0f) * areaWidth;
		float targetZ = (1.0f - std::clamp((g_mouseY - mapY) / mapH, 0.0f, 1.0f)) * areaDepth;
		client->camera.position.x += targetX - lookAt.x;
		client->camera.position.z += targetZ - lookAt.z;
	}
	return hovered;
}

// The original's minimap panel (CMinimapPanel) has four buttons on the arcs around the map:
// military units (left), civilian units (bottom), resources (right), each toggling that
// MINIMAP_DISPLAY_CATEGORY on the map, and the peasant activity interface (top, not
// implemented yet, drawn greyed out). A toggled-off category shows the "Depressed" image.
// Positions in Bottom_Right_Panel.tga found by matching the images against the panel.
bool ClientInterface::drawMinimapButtons(int panelX, int panelY)
{
	struct MinimapButton {
		const char* image;
		int x, y;
		uint8_t category;
		Bitmap mask; // alpha of the image, for hit tests on the arc
		texture tex = nullptr;
	};
	static MinimapButton buttons[] = {
		{ "Interface\\InGame\\Military Mini Map Button Depressed.tga", 7, 67, GameObjBlueprint::MINIMAP_MILITARY },
		{ "Interface\\InGame\\Economic Mini Map Button Depressed.tga", 72, 294, GameObjBlueprint::MINIMAP_ECONOMIC },
		{ "Interface\\InGame\\Resource Mini Map Button Depressed.tga", 294, 70, GameObjBlueprint::MINIMAP_RESOURCE },
		{ "Interface\\InGame\\Statistics Mini Map Button Grey.tga", 71, 7, 0 },
	};
	ImDrawList* drawList = ImGui::GetBackgroundDrawList();
	bool hoveredAny = false;
	for (MinimapButton& button : buttons) {
		if (!button.tex) {
			if (!FileExists(button.image))
				continue;
			button.mask = Bitmap::loadBitmap(button.image).convertToR8G8B8A8();
			button.tex = gfx->CreateTexture(button.mask, 1);
		}
		const int lx = g_mouseX - (panelX + button.x), ly = g_mouseY - (panelY + button.y);
		const bool hovered = lx >= 0 && ly >= 0 && lx < button.mask.width && ly < button.mask.height
			&& button.mask.pixels[4 * (ly * button.mask.width + lx) + 3] >= 128;
		hoveredAny |= hovered;
		if (hovered && button.category && g_mousePressed[SDL_BUTTON_LEFT])
			minimapHiddenCategories ^= button.category;
		const bool depressed = button.category ? (minimapHiddenCategories & button.category) != 0 : true;
		if (depressed) {
			const ImVec2 p0((float)(panelX + button.x), (float)(panelY + button.y));
			drawList->AddImage(button.tex, p0, ImVec2(p0.x + button.mask.width, p0.y + button.mask.height));
		}
	}
	return hoveredAny;
}

// The enhanced scene and terrain renderers draw with Diligent directly, so they need the
// Diligent renderer (not the null renderer).
static bool UseEnhancedRenderers(IRenderer* gfx)
{
	return g_settings.value<bool>("enhancedGraphics", false) && dynamic_cast<DiligentGfx*>(gfx) != nullptr;
}

// The original's interface creates these markers (table at 0x8B5704, used at 0x64CA40):
// Flags\Way_Point1 at the ground target of a unit order, the buoy (Resources\Bouy) when that
// target is on water, and Flags\Land_Spawn_Rally_Point for a building's spawn rally point.
// Here a unit order's flag stays until the units sent there have arrived (at least 1 s, at
// most 30 s), a rally point's for 3 s, with their animations.
bool ClientInterface::orderFlagDone(float age) const
{
	if (orderFlagRally || orderFlagUnits.empty())
		return age >= 3.0f;
	if (age < 1.0f)
		return false;
	if (age >= 30.0f)
		return true;
	for (const CliGORef& ref : orderFlagUnits) {
		const ClientGameObject* unit = ref.get();
		if (unit && !(unit->flags & ClientGameObject::fTerminated)
			&& (unit->position - orderFlagPosition).sqlen2xz() > 3.0f * 3.0f)
			return false;
	}
	return true;
}

void ClientInterface::sendRightClickCommand(Command* rightClickCommand, ClientGameObject* nextSelectedObject, const Vector3& peapos, const Vector3& faceTo)
{
	int orderFlag = 0; // 1: units sent to the ground position, 2: a building's rally point
	Vector3 destPos = peapos;
	const int NUM_DIRECTIONS = 8;
	const float SEPARATION = 3.0f;
	const float TWOPI = 2.0f * 3.1415f;
	float nextPosDir = 0.0f;
	float nextPosRadius = SEPARATION;
	for (ClientGameObject *sel : selection) {
		if (sel) {
			if (std::find(sel->blueprint->offeredCommands.begin(), sel->blueprint->offeredCommands.end(), rightClickCommand) != sel->blueprint->offeredCommands.end()) {
				// if object offers this command, send the command
				int assignmentMode = g_modCtrl ? Tags::ORDERASSIGNMODE_DO_FIRST : (g_modShift ? Tags::ORDERASSIGNMODE_DO_LAST : Tags::ORDERASSIGNMODE_FORGET_EVERYTHING_ELSE);
				client->sendCommand(sel, rightClickCommand, assignmentMode, nextSelectedObject, destPos, faceTo);
				if (!nextSelectedObject) {
					if (orderFlag == 0)
						orderFlagUnits.clear();
					orderFlag = 1;
					orderFlagUnits.push_back(sel);
				}
				// calc destination pos for next selected object
				destPos = peapos + Vector3(std::cos(nextPosDir), 0.0f, std::sin(nextPosDir)) * nextPosRadius;
				nextPosDir += SEPARATION / nextPosRadius;
				if (nextPosDir >= TWOPI) {
					nextPosDir = 0.0f;
					nextPosRadius += SEPARATION;
				}
			}
			else if (sel->blueprint->bpClass == Tags::GAMEOBJCLASS_BUILDING) {
				for (const auto* scmd : sel->blueprint->offeredCommands) {
					const std::string_view name = client->gameSet->commands.getString(scmd);
					if (name.substr(0, 6) == "Spawn ") {
						auto unitName = std::string(name.substr(6));
						int x = client->gameSet->objBlueprints[Tags::GAMEOBJCLASS_CHARACTER].names.getIndex(unitName);
						if (x >= 0) {
							const GameObjBlueprint* unitType = &client->gameSet->objBlueprints[Tags::GAMEOBJCLASS_CHARACTER][x];
							for (const auto* cmd : unitType->offeredCommands) {
								if (cmd == rightClickCommand && cmd->canBeAssignedToSpawnedUnits) {
									printf("Order for spawned unit changed to: %s\n", client->gameSet->commands.getString(cmd).c_str());
									if (cmd->order && cmd->order->classType == Tags::ORDTSKTYPE_MOVE) {
										client->sendSetBuildingSpawnedUnitOrderToDestination(sel, client->gameSet->commands.getIndex(cmd), peapos);
										orderFlag = std::max(orderFlag, 2);
									}
									else {
										client->sendSetBuildingSpawnedUnitOrderToTarget(sel, client->gameSet->commands.getIndex(cmd), nextSelectedObject);
									}
								}
							}
						}
					}
				}
			}
		}
	}
	if (orderFlag)
		showOrderFlag(peapos, orderFlag == 2);
}

void ClientInterface::showOrderFlag(const Vector3& position, bool rallyPoint)
{
	if (!client->terrain || !client->gameSet)
		return;
	const float ground = client->terrain->getHeight(position.x, position.z);
	const float surface = client->terrain->getHeightEx(position.x, position.z, true);
	const bool onWater = surface > ground + 0.01f;
	const char* path = rallyPoint ? "Warrior Kings Game Set\\Flags\\Land_Spawn_Rally_Point.ANIM3"
		: onWater ? "Warrior Kings Game Set\\Resources\\Bouy\\DEFAULT.ANIM3"
		: "Warrior Kings Game Set\\Flags\\Way_Point1.ANIM3";
	if (!FileExists(path))
		return;
	orderFlagEntity.model = client->gameSet->modelCache.getModel(path);
	orderFlagEntity.transform = Matrix::getTranslationMatrix(Vector3(position.x, surface, position.z));
	orderFlagEntity.color = client->clientPlayer ? client->clientPlayer->color : 0;
	orderFlagEntity.flags = 0;
	orderFlagStartTicks = SDL_GetTicks();
	orderFlagActive = true;
	orderFlagRally = rallyPoint;
	orderFlagPosition = position;
	if (rallyPoint)
		orderFlagUnits.clear();
}

// The range circles (DISPLAY_SPRITE_WHEN_SELECTED / _STAMPING_DOWN): the "Border Bands" textures
// are strips (arrows, dots, links) that repeat around the circle: the texture's width runs along
// the circle, stretched to 5 band widths per repeat, its height across the band (top outwards).
// Drawn on the ground with the particles' state (alpha blended, depth tested); each repeat has
// its own texture coordinates 0..1 (the batch sampler clamps).
void ClientInterface::drawRangeBands()
{
	if (rangeBands.empty() || !client->terrain)
		return;
	constexpr int TILES_PER_BATCH = 256, SEGMENTS_PER_TILE = 4;
	// one texture repeat along the circle per 5 band widths (6 m): the strips' narrow marks (three
	// ovals or arrows per repeat) are stretched along the circle, as in the original
	constexpr float TILE_LENGTH = 5.0f, BAND_WIDTH = 1.2f;
	if (!rangeBandBatch)
		rangeBandBatch = gfx->CreateBatch(TILES_PER_BATCH * SEGMENTS_PER_TILE * 4, TILES_PER_BATCH * SEGMENTS_PER_TILE * 12);
	gfx->BeginParticles();
	gfx->BeginBatchDrawing();
	gfx->SetBlendColor(-1);
	const Terrain* terrain = client->terrain;
	auto ground = [terrain](float x, float z) { return terrain->getHeightEx(x, z, true) + 0.15f; };
	for (const RangeBand& band : rangeBands) {
		gfx->SetTexture(0, (texture)band.tex);
		rangeBandBatch->begin();
		// the original's marks keep their size whatever the radius: about 1 m long and 0.6 m
		// across, one every 2 m (three per repeat)
		const float width = std::min(BAND_WIDTH, band.radius);
		const float outer = band.radius, inner = std::max(0.0f, band.radius - width);
		const float circumference = 2.0f * 3.14159265f * outer;
		const int tiles = std::max(4, (int)std::round(circumference / (width * TILE_LENGTH)));
		for (int t = 0; t < tiles; t++) {
			for (int s = 0; s < SEGMENTS_PER_TILE; s++) {
				const float u0 = (float)s / SEGMENTS_PER_TILE, u1 = (float)(s + 1) / SEGMENTS_PER_TILE;
				const float a0 = (t + u0) / tiles * 2.0f * 3.14159265f, a1 = (t + u1) / tiles * 2.0f * 3.14159265f;
				batchVertex* v;
				uint16_t* ix;
				uint32_t first;
				rangeBandBatch->next(4, 12, &v, &ix, &first);
				const float c0 = std::cos(a0), s0 = std::sin(a0), c1 = std::cos(a1), s1 = std::sin(a1);
				const Vector3 p[4] = {
					band.centre + Vector3(c0, 0, s0) * outer, band.centre + Vector3(c1, 0, s1) * outer,
					band.centre + Vector3(c1, 0, s1) * inner, band.centre + Vector3(c0, 0, s0) * inner };
				const float uv[4][2] = { { u0, 0 }, { u1, 0 }, { u1, 1 }, { u0, 1 } };
				for (int i = 0; i < 4; i++) {
					v[i].x = p[i].x;
					v[i].y = ground(p[i].x, p[i].z);
					v[i].z = p[i].z;
					v[i].u = uv[i][0];
					v[i].v = uv[i][1];
					v[i].color = 0xFFFFFFFF;
				}
				// both windings: seen from above whatever the batch pipeline culls
				for (const int& c : { 0, 1, 2, 0, 2, 3, 0, 2, 1, 0, 3, 2 })
					*(ix++) = (uint16_t)(first + c);
			}
		}
		rangeBandBatch->flush();
		rangeBandBatch->end();
	}
}

// The camera stays between a little above the ground under it and a high ceiling (for an
// overview of the map); the pitch limit is applied where the camera turns.
void ClientInterface::applyCameraLimits()
{
	if (!client->terrain)
		return;
	Vector3& pos = client->camera.position;
	const float ground = client->terrain->getHeight(pos.x, pos.z);
	pos.y = std::max(pos.y, ground + g_renderSettings.minHeightAboveGround);
	pos.y = std::min(pos.y, std::max(g_renderSettings.maxHeight, ground + g_renderSettings.minHeightAboveGround));
}

// Temporary settings window (F10) for trying the render and camera settings while playing.
// "Save" writes them to wkconfig.json; the renderer section only applies after a restart.
void ClientInterface::drawSettingsWindow(bool* open)
{
	RenderSettings& s = g_renderSettings;
	ImGui::SetNextWindowSize(ImVec2(560, 0), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Settings (temporary, F10)", open)) {
		ImGui::End();
		return;
	}
	if (ImGui::CollapsingHeader("View and fog", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Checkbox("View distance: whole map", &s.farClipWholeMap);
		ImGui::BeginDisabled(s.farClipWholeMap);
		ImGui::SliderFloat("View distance (sets the fog)", &s.farClip, 100.0f, 50000.0f, "%.0f", ImGuiSliderFlags_Logarithmic);
		ImGui::EndDisabled();
		ImGui::TextDisabled("The original's far_clip: 350 (options slider 280-1000)");
		ImGui::Checkbox("Fog off", &s.fogOff);
		ImGui::BeginDisabled(s.fogOff);
		ImGui::Checkbox("Own fog distances", &s.fogCustom);
		if (s.fogCustom) {
			const float viewDistance = client->camera.farDist;
			ImGui::SliderFloat("Fog near", &s.fogNear, 0.0f, 50000.0f, "%.0f", ImGuiSliderFlags_Logarithmic);
			ImGui::SliderFloat("Fog far (opaque)", &s.fogFar, 1.0f, 50000.0f, "%.0f", ImGuiSliderFlags_Logarithmic);
			s.fogNear = std::min(s.fogNear, s.fogFar);
			if (s.fogFar > viewDistance)
				ImGui::TextDisabled("Fog far is capped at the view distance (%.0f)", viewDistance);
		}
		else
			ImGui::SliderFloat("Fog start (of fog end)", &s.fogStart, 0.0f, 0.99f, "%.2f");
		ImGui::EndDisabled();
		ImGui::Checkbox("Map-edge fog", &s.edgeFog);
		ImGui::TextDisabled("The sliders apply to long views (fog off, own fog, whole map, view distance > 525);\nwith the original fog the edge fog is gentle (0 to 0.95) to keep the distant mountains");
		ImGui::BeginDisabled(!s.edgeFog);
		ImGui::SliderFloat("Edge fog starts (of edge band)", &s.edgeFogStart, -1.0f, 0.95f, "%.2f");
		ImGui::SliderFloat("Edge fog opaque at (of edge band)", &s.edgeFogOpaque, 0.0f, 0.95f, "%.2f");
		s.edgeFogStart = std::min(s.edgeFogStart, s.edgeFogOpaque);
		ImGui::TextDisabled("Band from the playable area's edge (0) to the map mesh's edge (1); below 0 is inside the playable area");
		ImGui::EndDisabled();
		ImGui::Checkbox("Own fog colour", &s.fogColourOverride);
		ImGui::BeginDisabled(!s.fogColourOverride);
		ImGui::ColorEdit3("Fog colour", s.fogColour);
		ImGui::EndDisabled();
		ImGui::SliderFloat("Field of view (vertical)", &s.fovYDegrees, 20.0f, 100.0f, "%.1f deg");
		ImGui::Checkbox("Sky", &s.sky);
	}
	if (ImGui::CollapsingHeader("Picture", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::SliderFloat("Exposure", &s.exposure, 0.25f, 4.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
		ImGui::SliderFloat("Brightness", &s.brightness, -0.5f, 0.5f, "%.2f");
		ImGui::SliderFloat("Contrast", &s.contrast, 0.25f, 2.0f, "%.2f");
		ImGui::SliderFloat("Saturation", &s.saturation, 0.0f, 2.0f, "%.2f");
		ImGui::TextDisabled("Applies to the 3D scene, not the HUD");
	}
	if (ImGui::CollapsingHeader("Lighting and shadows", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Checkbox("Cloud shadows", &s.cloudShadows);
		ImGui::Checkbox("Water surface", &s.waterSurface);
		ImGui::Checkbox("Static shadows", &s.staticShadows);
		ImGui::SliderFloat("Light in static shadows", &s.staticShadowLight, 0.0f, 1.0f, "%.3f");
		ImGui::SliderFloat("Model ambient", &s.meshAmbient, 0.0f, 1.0f, "%.3f");
		ImGui::SliderFloat("Model sun factor", &s.meshDiffuse, 0.0f, 1.5f, "%.3f");
		ImGui::Checkbox("Terrain bump mapping", &s.bumpMapping);
		ImGui::Checkbox("Character shadows", &s.characterShadows);
	}
	if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::SliderFloat("Scroll speed", &s.scrollSpeed, 0.1f, 5.0f, "%.2f");
		ImGui::SliderFloat("Shift scroll factor", &s.shiftScrollFactor, 1.0f, 10.0f, "%.1f");
		ImGui::SliderFloat("Mouse look sensitivity", &s.mouseLookSensitivity, 0.001f, 0.05f, "%.3f");
		ImGui::SliderFloat("Lowest height above ground", &s.minHeightAboveGround, 0.5f, 50.0f, "%.1f");
		ImGui::SliderFloat("Highest camera height", &s.maxHeight, 100.0f, 3000.0f, "%.0f");
	}
	if (ImGui::CollapsingHeader("Renderer (after a restart)")) {
		static const char* backends[] = { "d3d12", "vulkan" };
		std::string backend = g_settings.value<std::string>("diligentBackend", "d3d12");
		int backendIndex = backend == "vulkan" ? 1 : 0;
		if (ImGui::Combo("Backend", &backendIndex, backends, 2))
			g_settings["diligentBackend"] = backends[backendIndex];
		bool enhanced = g_settings.value<bool>("enhancedGraphics", false);
		if (ImGui::Checkbox("Enhanced graphics", &enhanced))
			g_settings["enhancedGraphics"] = enhanced;
		bool vsync = g_settings.value<bool>("vsync", true);
		if (ImGui::Checkbox("VSync", &vsync))
			g_settings["vsync"] = vsync;
		bool validation = g_settings.value<bool>("gfxValidation", false);
		if (ImGui::Checkbox("Graphics validation (debug)", &validation))
			g_settings["gfxValidation"] = validation;
		int size[2] = { g_settings.value<int>("windowWidth", 1280), g_settings.value<int>("windowHeight", 720) };
		if (ImGui::InputInt2("Window size", size)) {
			g_settings["windowWidth"] = size[0];
			g_settings["windowHeight"] = size[1];
		}
	}
	ImGui::Checkbox("Show FPS counter", &s.showFps);
	ImGui::Separator();
	if (ImGui::Button("Save to wkconfig.json"))
		s.save();
	ImGui::SameLine();
	if (ImGui::Button("Original values"))
		s = RenderSettings();
	ImGui::SameLine();
	if (ImGui::Button("Reload saved"))
		s.load();
	ImGui::End();
}

// SHEAR_MODEL: a world-space shear that keeps the model upright but lifts each vertex by the
// ground's slope under the object, so that wall segments follow the terrain as one band. The
// slope comes from the ground heights at the ends of the model's bounding box along its own
// axes; the longer axis also sets the height, so that neighbouring segments meet at their ends.
static Matrix GetShearToGroundMatrix(const Terrain* terrain, Model* model, const Matrix& world, const Vector3& position)
{
	const auto [low, high] = model->interpolateBoundingBox(0);
	const Vector3 centre = (low + high) * 0.5f;
	struct AxisSlope { Vector3 dir; float slope, length, midHeight; };
	auto axisSlope = [&](const Vector3& a, const Vector3& b) {
		const Vector3 wa = a.transform(world), wb = b.transform(world);
		Vector3 d(wb.x - wa.x, 0.0f, wb.z - wa.z);
		const float len = d.len2xz();
		const float ha = terrain->getHeight(wa.x, wa.z), hb = terrain->getHeight(wb.x, wb.z);
		return AxisSlope{ len > 1e-3f ? d / len : Vector3(0, 0, 0), len > 1e-3f ? (hb - ha) / len : 0.0f, len, (ha + hb) * 0.5f };
	};
	const AxisSlope ax = axisSlope(Vector3(low.x, 0, centre.z), Vector3(high.x, 0, centre.z));
	const AxisSlope az = axisSlope(Vector3(centre.x, 0, low.z), Vector3(centre.x, 0, high.z));
	// Ground gradient (the two axes are perpendicular in the ground plane)
	const float gx = ax.dir.x * ax.slope + az.dir.x * az.slope;
	const float gz = ax.dir.z * ax.slope + az.dir.z * az.slope;
	const float dy = (ax.length >= az.length ? ax.midHeight : az.midHeight) - position.y;
	// Row vectors: y' = y + gx * (x - px) + gz * (z - pz) + dy
	Matrix shear = Matrix::getIdentity();
	shear.m[0][1] = gx;
	shear.m[2][1] = gz;
	shear.m[3][1] = dy - gx * position.x - gz * position.z;
	return shear;
}

void ClientInterface::prewarmLevel()
{
	ZoneScopedN("Prewarm level");
	if (prewarmedLevel != client->getLevel())
		prewarmedModels.clear();
	prewarmedLevel = client->getLevel();
	prewarmCountdown = 60;
	if (!client->getLevel() || !sceneRenderer)
		return;
	const uint64_t start = SDL_GetTicks();
	std::vector<Model*> models;
	auto collect = [&](ClientGameObject* obj, const auto& self) -> void {
		for (const auto& [type, children] : obj->children)
			for (CommonGameObject* child : children)
				self((ClientGameObject*)child, self);
		if (!obj->blueprint)
			return;
		// The animation it plays now and the default one, all variants
		if (const auto* appear = obj->blueprint->getAppearance(obj->subtype, obj->appearance)) {
			for (int index : { obj->animationIndex, 0 }) {
				auto it = appear->animations.find(index);
				if (it != appear->animations.end())
					for (Model* m : it->second)
						if (m && prewarmedModels.insert(m).second)
							models.push_back(m);
			}
		}
	};
	collect(client->getLevel(), collect);
	for (Model* model : models) {
		scene->prewarm(model);
		sceneRenderer->prewarm(model);
	}
	if (!models.empty())
		printf("Prewarmed %zu models in %llu ms\n", models.size(), (unsigned long long)(SDL_GetTicks() - start));
}

void ClientInterface::drawObject(ClientGameObject *obj)
{
	using namespace Tags;

	for (auto& typechildren : obj->children) {
		for (CommonGameObject* child : typechildren.second) {
			drawObject((ClientGameObject*)child);
		}
	}

	if (!devMode && !(obj->flags & ClientGameObject::fRenderable))
		return;

	static const float tolerance = 25.0f;
	const float dirDist = client->camera.direction.dot(obj->position - client->camera.position);
	const float nearBound = client->camera.nearDist - tolerance;
	const float farBound = client->camera.farDist + tolerance;
	if (dirDist < nearBound || dirDist > farBound)
		return;

	const Vector3 side = client->camera.direction.cross(Vector3(0, 1, 0));
	// the same for all objects of a frame
	static float lastFovX = -1.0f, tanFov = 0.0f, cosFov = 1.0f;
	const float fovX = client->camera.fovY * client->camera.aspect;
	if (fovX != lastFovX) {
		lastFovX = fovX;
		tanFov = std::tan(fovX / 2.0f);
		cosFov = std::cos(fovX / 2.0f);
	}
	const float sideDist = std::abs(side.dot(obj->position - client->camera.position));
	const float sideBound = std::max(0.0f, dirDist) * tanFov + tolerance / cosFov;
	if (sideDist > sideBound)
		return;

	Model* model = nullptr;
	if (const auto* appear = obj->blueprint->getAppearance(obj->subtype, obj->appearance)) {
		const auto& ap = *appear;
		auto it = ap.animations.find(obj->animationIndex);
		if (it == ap.animations.end())
			it = ap.animations.find(0);
		if (it != ap.animations.end()) {
			const auto& anim = it->second;
			if (!anim.empty())
				model = anim[obj->animationVariant];
		}
	}
	if (!model && devMode)
		model = obj->blueprint->representAs;
	if (!model)
		return;

	const Matrix transformMatrix = obj->getWorldMatrix();

	Vector3 sphereCenter = model->getSphereCenter().transform(transformMatrix);
	float sphereRadius = model->getSphereRadius() * std::max({ obj->scale.x, obj->scale.y, obj->scale.z });
	Vector3 ttpp = sphereCenter.transformScreenCoords(client->camera.sceneMatrix);
	bool onCam = (client->camera.position - sphereCenter).len3() < sphereRadius;
	if (!onCam)
		onCam = !(ttpp.x < -1 || ttpp.x > 1 || ttpp.y < -1 || ttpp.y > 1 || ttpp.z < -1 || ttpp.z > 1);
	if (!onCam) {
		Vector3 camdir = client->camera.direction.normal();
		Vector3 H = client->camera.position + camdir * camdir.dot(sphereCenter - client->camera.position);
		Vector3 sphereEnd = sphereCenter + (H - sphereCenter).normal() * sphereRadius;
		ttpp = sphereEnd.transformScreenCoords(client->camera.sceneMatrix);
		onCam = !(ttpp.x < -1 || ttpp.x > 1 || ttpp.y < -1 || ttpp.y > 1 || ttpp.z < -1 || ttpp.z > 1);
	}
	if (!onCam)
		return;

	obj->sceneEntity.model = model;
	obj->sceneEntity.transform = transformMatrix;
	if (obj->blueprint->shearModel && client->terrain)
		obj->sceneEntity.transform *= GetShearToGroundMatrix(client->terrain, model, transformMatrix, obj->position);
	obj->sceneEntity.color = obj->getPlayer()->color;
	if (obj->animSynchronizedTask != -1) {
		CliScriptContext ctx{ client, obj };
		float frac = client->gameSet->tasks[obj->animSynchronizedTask].synchAnimationToFraction->eval(&ctx);
		obj->sceneEntity.animTime = (uint32_t)(model->getDuration() * frac * 1000.0f);
	}
	else
		obj->sceneEntity.animTime = (uint32_t)((client->timeManager.currentTime - obj->animStartTime) * 1000.0f);
	obj->sceneEntity.flags = 0;
	if (obj->animClamped) obj->sceneEntity.flags |= SceneEntity::SEFLAG_ANIM_CLAMP_END;
	if (obj->blueprint->hasStaticShadow) obj->sceneEntity.flags |= SceneEntity::SEFLAG_STATIC_SHADOW;

	// highlight selected object
	bool highlighted = selection.count(obj) >= 1;
	if (!highlighted) {
		highlighted = obj->blueprint->bpClass == Tags::GAMEOBJCLASS_CHARACTER
			&& obj->parent && obj->parent->blueprint->bpClass == Tags::GAMEOBJCLASS_FORMATION
			&& selection.count(obj->getParent());
	}
	if (highlighted) {
		highlightedObjects.insert(obj);
	}

	scene->add(&obj->sceneEntity);
	numObjectsDrawn++;
	// Attachment points
	drawAttachmentPoints(&obj->sceneEntity, obj->id);

	// shadow
	const bool blobShadow = model && obj->blueprint->hasDynamicShadow && g_renderSettings.characterShadows;
	if (blobShadow && gpuBlobShadows) {
		// A round blob as wide as the model's bounding sphere (see below), drawn by the terrain renderer
		const Vector3 centre = sphereCenter + Vector3(1, 0, 1) * (client->terrain->edge * 5.0f);
		const float radius = model->getSphereRadius() * std::max(obj->scale.x, obj->scale.z);
		gpuBlobShadows->insert(gpuBlobShadows->end(), { centre.x - radius, centre.z - radius,
			centre.x - radius, centre.z + radius, centre.x + radius, centre.z - radius });
	}
	else if (blobShadow) {
		static std::optional<texture> shadowTexture;
		if (!shadowTexture) {
			Bitmap bmp = Bitmap::loadBitmap("Warrior Kings Game Set\\Textures\\shadow.pcx").convertToR8G8B8A8();
			uint32_t* pix = reinterpret_cast<uint32_t*>(bmp.pixels.data());
			for (int i = 0; i < bmp.width * bmp.height; ++i) {
				pix[i] = (pix[i] & 255) << 24;
			}
			shadowTexture = gfx->CreateTexture(bmp, 1);
		}
		// A round blob as wide as the model's bounding sphere: the bounding box's footprint is no
		// wider than the unit itself, so the unit hid its whole shadow.
		const Vector3 centre = sphereCenter + Vector3(1, 0, 1) * (client->terrain->edge * 5.0f);
		const float radius = model->getSphereRadius() * std::max(obj->scale.x, obj->scale.z);
		const TerrainSpriteContainer::Point p1{ centre.x - radius, centre.z - radius };
		const TerrainSpriteContainer::Point p2{ centre.x - radius, centre.z + radius };
		const TerrainSpriteContainer::Point p3{ centre.x + radius, centre.z - radius };
		terrainSpriteContainer->addSprite(*shadowTexture, p1, p2, p3);
	}

	if (devMode || (obj->flags & (ClientGameObject::fSelectable | ClientGameObject::fTargetable))) {
		// Ray collision check
		if (auto spherePoint = RayIntersectsSphere(client->camera.position, rayDirection, obj->position + obj->sceneEntity.model->getSphereCenter() * obj->scale, obj->sceneEntity.model->getSphereRadius() * std::max({ obj->scale.x, obj->scale.y, obj->scale.z }))) {
			if (obj->blueprint->bpClass == Tags::GAMEOBJCLASS_CHARACTER) {
				const float sphereDist = (client->camera.position - *spherePoint).sqlen3();
				if (sphereDist < nextSelObjDistanceBySphere) {
					nextSelectedObjectBySphere = obj;
					nextSelObjDistanceBySphere = sphereDist;
				}
			}

			const float* verts = model->interpolate(obj->sceneEntity.animTime);
			Mesh& mesh = model->getStaticModel()->getMesh();
			PolygonList& polylist = mesh.polyLists[0];
			for (size_t g = 0; g < polylist.groups.size(); g++) {
				PolyListGroup& group = polylist.groups[g];
				for (auto& tuple : group.tupleIndex) {
					Vector3 vec[3];
					for (int j = 0; j < 3; j++) {
						uint16_t i = tuple[j];
						IndexTuple& tind = mesh.groupIndices[g][i];
						const float* fl = &verts[tind.vertex * 3];
						Vector3 prever(fl[0], fl[1], fl[2]);
						vec[j] = prever.transform(obj->sceneEntity.transform);
					}
					const auto trianglePoint = getRayTriangleIntersection(client->camera.position, rayDirection, vec[0], vec[2], vec[1]);
					if (trianglePoint) {
						float dist = (client->camera.position - *trianglePoint).sqlen3();
						if (dist < nextSelObjDistanceByMesh) {
							nextSelectedObjectByMesh = obj;
							nextSelObjDistanceByMesh = dist;
						}
					}
				}
			}
		}
		// Box selection check
		if (selBoxOn) {
			int bx = static_cast<int>((ttpp.x + 1.0f) * g_windowWidth / 2.0f);
			int by = static_cast<int>((-ttpp.y + 1.0f) * g_windowHeight / 2.0f);
			bool inBox = ((selBoxStartX < bx) && (bx < selBoxEndX)) || ((selBoxEndX < bx) && (bx < selBoxStartX));
			inBox = inBox && (((selBoxStartY < by) && (by < selBoxEndY)) || ((selBoxEndY < by) && (by < selBoxStartY)));
			if (inBox) {
				nextSelFromBox.push_back(obj);
			}
		}
	}
}

void ClientInterface::drawAttachmentPoints(SceneEntity* sceneEntity, uint32_t objid)
{
	Model* model = sceneEntity->model;
	size_t numAPs = model->getNumAPs();
	for (size_t i = 0; i < numAPs; i++) {
		const AttachmentPoint& ap = model->getAPInfo(i);
		auto state = model->getAPState(i, sceneEntity->animTime);
		if (state.on) {
			if (StrCICompare(ap.tag, "PS_Trail") == 0) {
				particlesContainer->generateTrail(state.position.transform(sceneEntity->transform), objid, client->timeManager.previousTime, client->timeManager.currentTime);
			}
			else if (std::string_view(ap.tag).substr(0, 3) == "PS_") {
				particlesContainer->generate(psCache->getPS(ap.tag.substr(3)), state.position.transform(sceneEntity->transform), objid, client->timeManager.previousTime, client->timeManager.currentTime);
			}
			if (!ap.path.empty()) {
				attachSceneEntities.emplace_back();
				auto& apse = attachSceneEntities.back();
				auto [cached, isNew] = attachmentModels.try_emplace(&ap, nullptr);
				if (isNew)
					cached->second = model->cache->getModel("Warrior Kings Game Set\\" + ap.path);
				apse.model = cached->second;
				apse.transform = Matrix::getTranslationMatrix(state.position) * sceneEntity->transform;
				apse.color = sceneEntity->color;
				apse.animTime = (uint32_t)(client->timeManager.currentTime * 1000.0f);
				scene->add(&apse);
				// Recursively draw attach. points of the attach. point's model!
				drawAttachmentPoints(&apse, objid); // NOTE: Same objid kept, trail might be shared!!
			}
		}
	}
}

namespace {
	// The frame's input, set aside for the in-game menu while it is open
	struct MenuInput {
		bool keyDown[std::size(g_keyDown)], keyPressed[std::size(g_keyPressed)];
		bool mouseDown[std::size(g_mouseDown)], mousePressed[std::size(g_mousePressed)], mouseReleased[std::size(g_mouseReleased)];
		int mouseWheel = 0, mouseDoubleClicked = 0;

		template <typename T, size_t N> static void move(T (&to)[N], T (&from)[N]) {
			std::copy(std::begin(from), std::end(from), std::begin(to));
			std::fill(std::begin(from), std::end(from), T());
		}
		void take() {
			move(keyDown, g_keyDown); move(keyPressed, g_keyPressed);
			move(mouseDown, g_mouseDown); move(mousePressed, g_mousePressed); move(mouseReleased, g_mouseReleased);
			mouseWheel = std::exchange(g_mouseWheel, 0);
			mouseDoubleClicked = std::exchange(g_mouseDoubleClicked, 0);
		}
		void give() {
			move(g_keyDown, keyDown); move(g_keyPressed, keyPressed);
			move(g_mouseDown, mouseDown); move(g_mousePressed, mousePressed); move(g_mouseReleased, mouseReleased);
			g_mouseWheel = mouseWheel;
			g_mouseDoubleClicked = mouseDoubleClicked;
		}
	};
}

void ClientInterface::chatFrame()
{
	const uint32_t now = SDL_GetTicks();
	if (chatSeen == (size_t)-1)
		chatSeen = client->chatMessages.size();
	for (; chatSeen < client->chatMessages.size(); chatSeen++) {
		const std::string& msg = client->chatMessages[chatSeen];
		if (!msg.empty() && msg[0] != '!') // script lines (tests, the debugger)
			chatLines.push_back({ msg, now });
	}
	if (chatLines.size() > 50)
		chatLines.erase(chatLines.begin(), chatLines.end() - 50);
	// test runs: "lobbyTest": { ..., "gameChat": "<text>" } says it once the game has run 3 seconds
	if (static bool said = false; !said && client->timeManager.currentTime > 3.0f && client->serverLink) {
		said = true;
		if (const std::string text = g_settings.value("lobbyTest", nlohmann::json()).value("gameChat", std::string()); !text.empty())
			client->sendMessage(g_settings.value("playerName", std::string("Player")) + ": " + text);
	}

	const bool enter = g_keyPressed[SDL_SCANCODE_RETURN] || g_keyPressed[SDL_SCANCODE_KP_ENTER];
	if (chatOpen) {
		if (g_keyPressed[SDL_SCANCODE_ESCAPE])
			chatOpen = false;
		else if (enter) {
			if (chatInput[0] && client->serverLink)
				client->sendMessage(g_settings.value("playerName", std::string("Player")) + ": " + chatInput);
			chatOpen = false;
		}
		// while typing, the game gets no keys (Esc doesn't open the menu)
		std::fill(std::begin(g_keyDown), std::end(g_keyDown), false);
		std::fill(std::begin(g_keyPressed), std::end(g_keyPressed), false);
	}
	else if (enter && !ImGui::GetIO().WantTextInput) {
		chatOpen = chatFocus = true;
		chatInput[0] = 0;
		g_keyPressed[SDL_SCANCODE_RETURN] = g_keyPressed[SDL_SCANCODE_KP_ENTER] = false;
	}

}

// The chat's messages and its typing line (in the ImGui frame)
void ClientInterface::chatDraw()
{
	const uint32_t now = SDL_GetTicks();
	// the last messages (all of the last ones while typing), fading out after 15 seconds
	const ImGuiIO& io = ImGui::GetIO();
	const float size = std::max(14.0f, std::round(io.DisplaySize.y / 48.0f));
	const float x = std::round(io.DisplaySize.x * 0.01f);
	float y = std::round(io.DisplaySize.y * 0.1f);
	ImDrawList* dl = ImGui::GetForegroundDrawList();
	const size_t first = chatLines.size() > 8 ? chatLines.size() - 8 : 0;
	for (size_t i = first; i < chatLines.size(); i++) {
		const uint32_t age = now - chatLines[i].second;
		if (!chatOpen && age > 15000)
			continue;
		const int alpha = chatOpen ? 255 : (int)(255 * std::clamp((15000 - age) / 2000.0f, 0.0f, 1.0f));
		const char* text = chatLines[i].first.c_str();
		dl->AddText(ImGui::GetFont(), size, ImVec2(x + 1, y + 1), IM_COL32(0, 0, 0, alpha), text);
		dl->AddText(ImGui::GetFont(), size, ImVec2(x, y), IM_COL32(255, 230, 150, alpha), text);
		y += std::round(size * 1.15f);
	}
	if (chatOpen) {
		ImGui::SetNextWindowPos(ImVec2(x, y + size * 0.3f));
		ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x * 0.4f, 0));
		ImGui::SetNextWindowBgAlpha(0.6f);
		ImGui::Begin("##chat", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove);
		ImGui::TextUnformatted("Say:");
		ImGui::SameLine();
		if (chatFocus) {
			ImGui::SetKeyboardFocusHere();
			chatFocus = false;
		}
		ImGui::SetNextItemWidth(-1);
		ImGui::InputText("##say", chatInput, sizeof(chatInput));
		ImGui::End();
	}
}

void ClientInterface::iter()
{
	ZoneScoped;
	static uint32_t oldCliTime = SDL_GetTicks();
	uint32_t cliTime = SDL_GetTicks();
	uint32_t cliDeltaTime = cliTime - oldCliTime;
	if (cliDeltaTime > 1000u) cliDeltaTime = 100u;
	oldCliTime = cliTime;

	static Vector3 peapos(0, 0, 0);
	static bool showTerrain = true;

	static WndCursor *arrowCursor;
	static WndCursor *currentCursor = nullptr;
	if (firstFrame) {
		firstFrame = false;
		//arrowCursor = WndCreateSystemCursor(SDL_SYSTEM_CURSOR_HAND);
		arrowCursor = WndCreateCursor("Interface\\C_DEFAULT.TGA");
		WndSetCursor(arrowCursor);
		currentCursor = arrowCursor;
		lang.load("Languages\\Language.txt");
	}

	// The in-game menu (Esc): while it is open, it gets the frame's input and the game none
	FrontEnd* frontEnd = FrontEnd::get();
	if (!singlePlayer && client->getLevel() && !(frontEnd && frontEnd->isOpen()))
		chatFrame();
	if (frontEnd && !frontEnd->isOpen() && g_keyPressed[SDL_SCANCODE_ESCAPE] && client->getLevel()) {
		frontEnd->setSinglePlayer(singlePlayer);
		frontEnd->setCanSave((bool)saveGame);
		frontEnd->open("ingame");
		if (singlePlayer && !client->timeManager.paused) {
			client->sendPauseRequest(true);
			menuPausedGame = true;
		}
	}
	// Test runs: "benchmark": { ..., "openMenu": [screens] } shows the in-game menu in the screenshot
	if (frontEnd && BenchmarkEnabled() && client->getLevel() && !frontEnd->isOpen()) {
		static bool opened = false;
		const auto screens = g_settings.value("benchmark", nlohmann::json()).value("openMenu", nlohmann::json::array());
		if (!opened && !screens.empty()) {
			opened = true;
			for (const auto& screen : screens)
				frontEnd->open(screen.get<std::string>());
		}
	}
	// Test runs: "benchmark": { ..., "saveAs": "<name>" } saves the game once it has run 2 seconds
	if (frontEnd && BenchmarkEnabled() && client->getLevel() && client->timeManager.currentTime > 2.0f) {
		static bool saved = false;
		const std::string name = g_settings.value("benchmark", nlohmann::json()).value("saveAs", std::string());
		if (!saved && !name.empty()) {
			saved = true;
			saveRequested(name);
		}
	}
	// Test runs: "benchmark": { ..., "script": [seconds, "<action>"] } runs a script action (on the
	// server, through the script chat) once the game has run that long
	if (BenchmarkEnabled() && client->getLevel()) {
		static bool ran = false;
		const nlohmann::json script = g_settings.value("benchmark", nlohmann::json()).value("script", nlohmann::json());
		if (!ran && script.is_array() && script.size() == 2 && client->timeManager.currentTime > script[0].get<float>()) {
			ran = true;
			client->sendMessage("!" + script[1].get<std::string>());
		}
	}
	// Test runs: "benchmark": { ..., "select": "<blueprint>" } selects the player's first object of that type
	if (BenchmarkEnabled() && client->getLevel() && client->clientPlayer && selection.empty()) {
		const std::string name = g_settings.value("benchmark", nlohmann::json()).value("select", std::string());
		if (!name.empty()) {
			ClientGameObject* found = nullptr;
			auto search = [&](ClientGameObject* obj, auto& self) -> void {
				if (found)
					return;
				if (obj->blueprint->name == name) {
					found = obj;
					return;
				}
				for (const auto& [type, children] : obj->children)
					for (CommonGameObject* child : children)
						self(child->dyncast<ClientGameObject>(), self);
			};
			search(client->clientPlayer.get(), search);
			if (found)
				selection.insert(found);
		}
	}
	const bool menuOpen = frontEnd && frontEnd->isOpen();
	static MenuInput menuInput;
	if (menuOpen)
		menuInput.take();

	//----- Command cursors -----//
	TracyCZoneN(zoneInput, "Input and commands", 1);

	static Command* selectedCursorCommand = nullptr;
	WndCursor *nextCursor = nullptr;
	Command *rightClickCommand = nullptr;
	static std::vector<std::pair<WndCursor*, Command*>> availableCursorCommands;
	availableCursorCommands.clear();
	for (ClientGameObject *sel : selection) {
		if (sel) {
			CliScriptContext ctx(client, sel);
			auto _ = ctx.change(ctx.target, nextSelectedObject);

			static std::vector<Command*> availableCommands;
			availableCommands.clear();

			for (Command* scmd : sel->blueprint->offeredCommands) {
				availableCommands.push_back(scmd);

				// commands for auto ordering spawned characters from building
				if (sel->blueprint->bpClass == Tags::GAMEOBJCLASS_BUILDING) {
					const std::string_view name = client->gameSet->commands.getString(scmd);
					if (name.substr(0, 6) == "Spawn ") {
						auto unitName = std::string(name.substr(6));
						int x = client->gameSet->objBlueprints[Tags::GAMEOBJCLASS_CHARACTER].names.getIndex(unitName);
						if (x >= 0) {
							const GameObjBlueprint* unitType = &client->gameSet->objBlueprints[Tags::GAMEOBJCLASS_CHARACTER][x];
							for (Command* cmd : unitType->offeredCommands) {
								if (cmd->canBeAssignedToSpawnedUnits) {
									availableCommands.push_back(cmd);
								}
							}
						}
					}
				}
			}

			for (Command* cmd : availableCommands) {
				WndCursor* cmdCursor = nullptr;
				for (auto& avcond : cmd->cursorAvailable) {
					if (avcond.first->test->eval(&ctx)) {
						cmdCursor = avcond.second;
						break;
					}
				}
				if (!cmdCursor)
					cmdCursor = cmd->cursor;
				if (cmdCursor) {
					if (std::all_of(cmd->cursorConditions.begin(), cmd->cursorConditions.end(), [&](int eq) {return client->gameSet->equations[eq]->eval(&ctx) > 0.0f; })) {
						if (!nextCursor) {
							nextCursor = cmdCursor;
							rightClickCommand = cmd;
						}
						if (std::find_if(availableCursorCommands.begin(), availableCursorCommands.end(), [&](auto& pair) {return pair.second == cmd; }) == availableCursorCommands.end())
							availableCursorCommands.emplace_back(cmdCursor, cmd);
					}
				}
			}
		}
	}
	if (selectedCursorCommand) {
		// if a specfic cursor command is selected, check that it is still valid, and use it instead
		auto it = std::find_if(availableCursorCommands.begin(), availableCursorCommands.end(), [](auto& pair) {return pair.second == selectedCursorCommand; });
		if (it != availableCursorCommands.end()) {
			nextCursor = it->first;
			rightClickCommand = it->second;
		}
	}
	if (!nextCursor)
		nextCursor = arrowCursor;
	if (nextCursor) {
		if (nextCursor != currentCursor) {
			currentCursor = nextCursor;
			WndSetCursor(currentCursor);
		}
	}

	//----- Input -----//

	Vector3 forward = client->camera.direction.normal2xz();
	Vector3 strafe = forward.cross(Vector3(0, 1, 0));
	// Shift scrolls faster
	const float camSpeed = g_renderSettings.scrollSpeed * (g_modShift ? g_renderSettings.shiftScrollFactor : 1.0f) * 2.0f * cliDeltaTime / (1000.0f / 60.0f);
	forward *= camSpeed;
	strafe *= camSpeed;

	if (g_keyDown[SDL_SCANCODE_UP] || g_keyDown[SDL_SCANCODE_W])
		client->camera.position += forward;
	if (g_keyDown[SDL_SCANCODE_DOWN] || g_keyDown[SDL_SCANCODE_S])
		client->camera.position -= forward;
	if (g_keyDown[SDL_SCANCODE_RIGHT] || g_keyDown[SDL_SCANCODE_D])
		client->camera.position -= strafe;
	if (g_keyDown[SDL_SCANCODE_LEFT] || g_keyDown[SDL_SCANCODE_A])
		client->camera.position += strafe;

	if (g_keyDown[SDL_SCANCODE_KP_0] || g_mouseDown[SDL_BUTTON_MIDDLE]) {
		if (!camroton) {
			camrotoffx = g_mouseX;
			camrotoffy = g_mouseY;
			camrotorix = client->camera.orientation.x;
			camrotoriy = client->camera.orientation.y;
			camroton = true;
		}
		// Pitch stays within straight down and straight up, so the view never flips over;
		// yaw is unlimited.
		constexpr float maxPitch = 1.5707963f - 0.001f;
		const float sensitivity = g_renderSettings.mouseLookSensitivity;
		client->camera.orientation.x = std::clamp(camrotorix - (g_mouseY - camrotoffy) * sensitivity, -maxPitch, maxPitch);
		client->camera.orientation.y = camrotoriy - (g_mouseX - camrotoffx) * sensitivity;
	}
	else
		camroton = false;

	if (g_mouseWheel)
		client->camera.position.y += g_mouseWheel * (g_renderSettings.invertMouseWheel ? -2 : 2);

	static bool clickedScriptedUi = false;
	if (!clickedScriptedUi && g_mousePressed[SDL_BUTTON_LEFT]) {
		debugger.selectObject(nextSelectedObject);
		if (!g_modShift)
			selection.clear();
		if (nextSelectedObject) {
			selection.insert(nextSelectedObject);
			const bool doubleClick = g_mouseDoubleClicked == SDL_BUTTON_LEFT;
			const int singleClickSelectAllItem = client->gameSet->items.names.getIndex("Single Click Selects All Units Of This Type");
			const bool selectAllOnSingleClick = singleClickSelectAllItem >= 0 && nextSelectedObject->getItem(singleClickSelectAllItem) > 0.0f;
			if (doubleClick ^ selectAllOnSingleClick) {
				// select nearby units of same type
				auto walkObj = [&](ClientGameObject* obj, const auto& rec) -> void {
					if (obj->blueprint == nextSelectedObject->blueprint) {
						if ((obj->position - nextSelectedObject->position).len2xz() < 25.0f) {
							selection.insert(obj);
						}
					}
					for (const auto& [type, objList] : obj->children) {
						for (const auto& child : objList) {
							rec((ClientGameObject*)child, rec);
						}
					}
					};
				walkObj(nextSelectedObject->getPlayer(), walkObj);
			}
		}
		stampdownBlueprint = nullptr;
		selBoxStartX = selBoxEndX = g_mouseX;
		selBoxStartY = selBoxEndY = g_mouseY;
		selBoxOn = true;
		selectedCursorCommand = nullptr;
	}

	if (g_mouseDown[SDL_BUTTON_LEFT]) {
		selBoxEndX = g_mouseX;
		selBoxEndY = g_mouseY;
	}
	else {
		if (selBoxOn) {
			selBoxOn = false;
			//if (!g_modShift)
			//	selection.clear();
			selection.insert(nextSelFromBox.begin(), nextSelFromBox.end());
		}
	}

	if (!clickedScriptedUi && g_mousePressed[SDL_BUTTON_RIGHT]) {
		if (stampdownBlueprint && stampdownPlayer) {
			client->sendStampdown(stampdownBlueprint, stampdownPlayer, peapos, sendStampdownEvent, stampdownFromCommand);
			if (stampdownFromCommand) {
				const Command* cmd = &client->gameSet->commands[client->gameSet->commands.names.getIndex("Build")];
				for (ClientGameObject* obj : selection) {
					if (obj) {
						if (std::find(obj->blueprint->offeredCommands.begin(), obj->blueprint->offeredCommands.end(), cmd) != obj->blueprint->offeredCommands.end()) {
							int assignmentMode = g_modCtrl ? Tags::ORDERASSIGNMODE_DO_FIRST : (g_modShift ? Tags::ORDERASSIGNMODE_DO_LAST : Tags::ORDERASSIGNMODE_FORGET_EVERYTHING_ELSE);
							client->sendBuildLastStampdownedObject(obj, assignmentMode);
						}
					}
				}
				if (!g_modShift && !g_modCtrl)
					stampdownBlueprint = nullptr;
			}
		}
		else if (rightClickCommand && !nextSelectedObject && !formationDrag.active
			&& std::any_of(selection.begin(), selection.end(), [](const auto& s) { return s && s->blueprint->bpClass == Tags::GAMEOBJCLASS_FORMATION; })) {
			// a formation sent to the ground: the move goes on releasing the button, facing
			// the way the mouse was dragged meanwhile (the arrow shows it)
			formationDrag = { true, peapos, rightClickCommand };
		}
		else if (rightClickCommand)
			sendRightClickCommand(rightClickCommand, nextSelectedObject, peapos, Vector3(-1, 0, 0));
	}
	if (formationDrag.active && !g_mouseDown[SDL_BUTTON_RIGHT]) {
		formationDrag.active = false;
		const bool dragged = (peapos - formationDrag.start).sqlen2xz() > FORMATION_DRAG_MIN * FORMATION_DRAG_MIN;
		sendRightClickCommand(formationDrag.command, nullptr, formationDrag.start, dragged ? peapos : Vector3(-1, 0, 0));
	}

	clickedScriptedUi = false;

	// a formation's right-drag: an arrow from where it goes to where it will face
	if (formationDrag.active && (peapos - formationDrag.start).sqlen2xz() > FORMATION_DRAG_MIN * FORMATION_DRAG_MIN) {
		auto toScreen = [this](const Vector3& p) {
			const Vector3 t = p.transformScreenCoords(client->camera.sceneMatrix);
			return ImVec2((t.x + 1) * 0.5f * g_windowWidth, (1 - t.y) * 0.5f * g_windowHeight);
		};
		const Vector3 a = formationDrag.start, b = peapos;
		const Vector3 dir = (b - a).normal();
		const Vector3 side(-dir.z, 0, dir.x);
		const Vector3 head = b - dir * 3.0f;
		ImDrawList* dl = ImGui::GetBackgroundDrawList();
		const ImU32 colour = IM_COL32(255, 210, 60, 230), shadow = IM_COL32(0, 0, 0, 160);
		for (int pass = 0; pass < 2; pass++) {
			const ImU32 c = pass == 0 ? shadow : colour;
			const float w = pass == 0 ? 7.0f : 4.0f;
			dl->AddLine(toScreen(a), toScreen(head), c, w);
			dl->AddTriangleFilled(toScreen(b), toScreen(head + side * 1.8f), toScreen(head - side * 1.8f), c);
		}
		dl->AddCircleFilled(toScreen(a), 5.0f, colour);
	}

	if (g_keyPressed[SDL_SCANCODE_P]) {
		static bool pauseState = false;
		pauseState = !pauseState;
		client->sendPauseRequest(pauseState);
	}
	if (g_keyPressed[SDL_SCANCODE_KP_PLUS]) {
		client->sendGameSpeedChange(client->timeManager.timeSpeed * 2.0f);
	}
	if (g_keyPressed[SDL_SCANCODE_KP_MINUS]) {
		client->sendGameSpeedChange(client->timeManager.timeSpeed / 2.0f);
	}
	if (g_keyPressed[SDL_SCANCODE_DELETE]) {
		for (ClientGameObject* sel : selection)
			if (sel)
				client->sendTerminateObject(sel);
	}

	static bool debuggerOn = false;
	static bool uiScriptOn = true;
	if (g_keyPressed[SDL_SCANCODE_F2]) {
		debuggerOn = !debuggerOn;
	}
	if (g_keyPressed[SDL_SCANCODE_F3]) {
		uiScriptOn = !uiScriptOn;
	}

	static int elevatorMouseStart = 0;
	static float elevatorCamStart = 0.0f;
	if (g_mousePressed[SDL_BUTTON_X1]) {
		elevatorMouseStart = g_mouseY;
		elevatorCamStart = client->camera.position.y;
	}
	if (g_mouseDown[SDL_BUTTON_X1])
		client->camera.position.y = elevatorCamStart + (elevatorMouseStart - g_mouseY) * 0.1f;
	applyCameraLimits();

	static bool settingsOn = false;
	if (g_keyPressed[SDL_SCANCODE_F10])
		settingsOn = !settingsOn;

	if (g_keyPressed[SDL_SCANCODE_E]) {
		// select available cursor command next to the one currently shown
		auto it = std::find_if(availableCursorCommands.begin(), availableCursorCommands.end(), [&](auto& pair) { return pair.second == rightClickCommand; });
		if (it != availableCursorCommands.end()) {
			size_t index = it - availableCursorCommands.begin();
			if (index == availableCursorCommands.size() - 1)
				selectedCursorCommand = nullptr;
			else
				selectedCursorCommand = availableCursorCommands[index + 1].second;
		}
	}

	//----- Clean selection of unauthorized object -----//
	if (!devMode) {
		std::unordered_set<CliGORef> formationsSelected;
		bool hasCharacter = false;

		for (auto itSelect = selection.begin(); itSelect != selection.end();) {
			ClientGameObject* obj = itSelect->get();
			const bool ok = obj && (obj->flags & ClientGameObject::fSelectable) && (obj->getPlayer() == client->clientPlayer);
			if (!ok) {
				itSelect = selection.erase(itSelect);
				continue;
			}
			hasCharacter |= obj->blueprint->bpClass == Tags::GAMEOBJCLASS_CHARACTER;
			if (obj->blueprint->bpClass == Tags::GAMEOBJCLASS_CHARACTER && obj->getParent()->blueprint->bpClass == Tags::GAMEOBJCLASS_FORMATION) {
				formationsSelected.insert(obj->getParent());
				itSelect = selection.erase(itSelect);
				continue;
			}
			++itSelect;
		}
		if (hasCharacter) {
			for (auto itSelect = selection.begin(); itSelect != selection.end();) {
				if (itSelect->get()->blueprint->bpClass != Tags::GAMEOBJCLASS_CHARACTER && itSelect->get()->blueprint->bpClass != Tags::GAMEOBJCLASS_FORMATION) {
					itSelect = selection.erase(itSelect);
					continue;
				}
				++itSelect;
			}
		}
		selection.merge(formationsSelected);
	}

	TracyCZoneEnd(zoneInput);

	//----- ImGui -----//
	TracyCZoneN(zoneUi, "UI (ImGui and UI script)", 1);

	ImGuiImpl_NewFrame();
	if (!singlePlayer && client->getLevel() && !(frontEnd && frontEnd->isOpen()))
		chatDraw();
	if (settingsOn)
		drawSettingsWindow(&settingsOn);
	if (g_renderSettings.showFps) {
		const FrameStats::Summary ft = g_frameStats.summarize();
		char text[96];
		snprintf(text, sizeof(text), "%d FPS  %.2f ms (p99 %.2f)", framesPerSecond, ft.avgMs, ft.p99Ms);
		ImDrawList* fg = ImGui::GetForegroundDrawList();
		const ImVec2 pos(12.0f, 92.0f); // below the top panel
		const ImVec2 size = ImGui::CalcTextSize(text);
		fg->AddRectFilled(ImVec2(pos.x - 4, pos.y - 2), ImVec2(pos.x + size.x + 4, pos.y + size.y + 2), IM_COL32(0, 0, 0, 160));
		fg->AddText(pos, IM_COL32(255, 255, 255, 255), text);
	}
	if (debuggerOn) {
		debugger.draw();

		ImGui::Begin("Client Interface Debug");
		ImGui::Text("ODPF: %i", numObjectsDrawn);
		ImGui::Text("FPS: %i", framesPerSecond);
		const FrameStats::Summary ft = g_frameStats.summarize();
		ImGui::Text("Frame ms (last %zu): avg %.2f  p50 %.2f  p99 %.2f  max %.2f", ft.frames, ft.avgMs, ft.p50Ms, ft.p99Ms, ft.maxMs);
		ImGui::DragFloat3("peapos", &peapos.x);
		ImGui::Checkbox("Dev mode", &devMode);
		if (ImGui::Button("Start level"))
			client->sendStartLevelRequest();
		ImGui::Checkbox("Terrain", &showTerrain);
		if (bool* bump = terrainRenderer ? terrainRenderer->bumpMapping() : nullptr) {
			if (client->terrain)
				ImGui::DragFloat3("Sun", &client->terrain->sunVector.x, 0.1f);
			ImGui::Checkbox("Bump", bump);
		}
		ImGui::End();
	}

	for (auto& activeGtw : client->gtwStates) {
		if (activeGtw.second == -1) continue;
		auto& gtw = client->gameSet->gameTextWindows[activeGtw.first];
		auto& page = gtw.pages[activeGtw.second];
		std::string title = "[GTW] " + client->gameSet->gameTextWindows.names[activeGtw.first];
		ImGui::SetNextWindowPos(ImVec2((float)g_windowWidth/2, (float)g_windowHeight/2), 0, ImVec2(0.5f, 0.5f));
		ImGui::Begin(title.c_str(), nullptr, ImGuiWindowFlags_NoSavedSettings);
		auto loctext = lang.getText(page.textBody);
		std::string ptext;
		for (size_t i = 0; i < loctext.size(); i++) {
			if (loctext[i] == '\\') {
				char c = loctext[i + 1];
				if (c == 'n')
					ptext.push_back('\n');
				else if (c == 0)
					break;
				i++;
			}
			else
				ptext.push_back(loctext[i]);
		}
		ImGui::PushTextWrapPos(560.0f);
		ImGui::TextUnformatted(ptext.c_str());
		ImGui::PopTextWrapPos();
		for (size_t i = 0; i < gtw.buttons.size(); i++) {
			auto& button = gtw.buttons[i];
			if (ImGui::Button(lang.getText(button.text).c_str())) {
				switch (button.onClickWindowAction) {
				case Tags::GTW_BUTTON_WINDOW_ACTION_CLOSE_WINDOW:
					activeGtw.second = -1; break;
				case Tags::GTW_BUTTON_WINDOW_ACTION_MOVE_FIRST_PAGE:
					activeGtw.second = 0; break;
				case Tags::GTW_BUTTON_WINDOW_ACTION_MOVE_NEXT_PAGE:
					if (activeGtw.second < (int)gtw.pages.size() - 1) activeGtw.second++; break;
				case Tags::GTW_BUTTON_WINDOW_ACTION_MOVE_PREVIOUS_PAGE:
					if (activeGtw.second > 0) activeGtw.second--; break;
				}
				if(!button.onClickSequence.actionList.empty())
					client->sendGameTextWindowButtonClicked(activeGtw.first, i);
			}
			ImGui::SameLine();
		}
		ImGui::NewLine();
		ImGui::End();
	}

	//----- Scriptable UI -----//
	static bool luaInitialized = false;
	static sol::state lua;
	static sol::load_result luaScript;
	static TextureCache csTextureCache(gfx);
	// The script's functions capture this ClientInterface: a new one (after going back to the
	// main menu) starts a new script
	static ClientInterface* luaOwner = nullptr;
	// hot reload (every second): changed textures in place, and the script in a new state when a
	// file of it was edited, added or removed
	static UIScriptFiles luaScriptFiles;
	static uint32_t hotReloadCheck = 0;
	bool reloadScript = false;
	if (HotReloadEnabled() && SDL_GetTicks() - hotReloadCheck >= 1000) {
		hotReloadCheck = SDL_GetTicks();
		TextureCache::ReloadChangedTextures();
		// also after a script error: fixing the file brings the UI back
		if (!luaScriptFiles.files.empty() && luaScriptFiles.changed()) {
			printf("Hot reload: in-game UI script\n");
			reloadScript = true;
			uiScriptOn = true;
		}
	}
	if (luaOwner != this || reloadScript) {
		luaOwner = this;
		luaInitialized = false;
		luaScript = {};
		lua = {};
	}
	if (uiScriptOn && !luaInitialized) {
		lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::string);
		// UI functions
		lua.set_function("drawText", 
			[](int x, int y, const std::string& text) {
				ImGui::GetBackgroundDrawList()->AddText(ImVec2((float)x, (float)y), -1, text.c_str());
			});
		lua.set_function("drawRect",
			[](int x, int y, int width, int height, uint32_t color) {
				ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2((float)x, (float)y), ImVec2((float)(x + width), (float)(y + height)), color);
			});
		lua.set_function("drawImage",
			[](int x, int y, int width, int height, const std::string& path) {
				texture tex = csTextureCache.getTexture(path.c_str(), true);
				ImGui::GetBackgroundDrawList()->AddImage(tex, ImVec2((float)x, (float)y), ImVec2((float)(x + width), (float)(y + height)));
			});
		lua.set_function("getWindowWidth", []() {return g_windowWidth; });
		lua.set_function("getWindowHeight", []() {return g_windowHeight; });
		// Client object
		lua.set_function("getPlayerID",
			[this]() { ClientGameObject* obj = client->clientPlayer.get(); return obj ? obj->id : 0u; }
		);
		lua.set_function("getItem",
			[this](uint32_t id, const std::string& item) {
				int itemId = client->gameSet->items.names.getIndex(item);
				return client->findObject(id)->getItem(itemId);
			});
		lua.set_function("getFirstSelection",
			[this]() -> uint32_t {
				if (selection.empty()) return 0u;
				ClientGameObject* obj = selection.begin()->get();
				return obj ? obj->id : 0u;
			}
		);
		lua.set_function("getColor",
			[this](uint32_t id) {
				ClientGameObject* obj = client->findObject(id);
				return obj ? obj->color : 0;
			});
		lua.set_function("getObjectClass",
			[this](uint32_t id) {
				ClientGameObject* obj = client->findObject(id);
				return obj ? obj->blueprint->bpClass : -1;
			}
		);
		// Client
		lua.set_function("getWKVersion",
			[this]() {return client->gameSet ? client->gameSet->version : 0; }
		);
		lua.set("GSVERSION_WKONE", GameSet::GSVERSION_WKONE);
		lua.set("GSVERSION_WKBATTLES", GameSet::GSVERSION_WKBATTLES);
		lua.set_function("getGameTime", [this]() {return client->timeManager.currentTime; });
		lua.set_function("getSystemTime", [this]() {return (float)SDL_GetTicks() / 1000.0f; });
		// Game set
		lua.set_function("computeEquation",
			[this](const std::string& equationName, uint32_t objId) -> float {
				int equationIndex = client->gameSet->equations.names.getIndex(equationName);
				if (equationIndex == -1) return 0.0f;
				CliScriptContext ctx{ client, client->findObject(objId) };
				return client->gameSet->equations[equationIndex]->eval(&ctx);
			}
		);
		// Blueprint
		lua.set_function("getBlueprint",
			[this](uint32_t id) -> const GameObjBlueprint* {
				ClientGameObject* obj = client->findObject(id);
				return obj ? obj->blueprint : nullptr;
			}
		);
		lua.set_function("getCommand", [this](const std::string& name) -> const Command* {
			int commandIndex = client->gameSet->commands.names.getIndex(name);
			if (commandIndex == -1) return nullptr;
			return &client->gameSet->commands[commandIndex];
			});
		sol::usertype<GameObjBlueprint> blueprint_type = lua.new_usertype<GameObjBlueprint>("GameObjBlueprint");
		blueprint_type.set("name", sol::readonly(&GameObjBlueprint::name));
		blueprint_type.set("bpClass", sol::readonly(&GameObjBlueprint::bpClass));
		//AA lua.set_function("offeredCommands", &GameObjBlueprint::offeredCommands);
		lua.set_function("getOfferedCommandsCount", [](const GameObjBlueprint* bp) {return bp->offeredCommands.size(); });
		lua.set_function("getOfferedCommand", [](const GameObjBlueprint* bp, int index) -> Command* {return bp->offeredCommands.at(index); });
		// Command
		sol::usertype<Command> command_type = lua.new_usertype<Command>("Command");
		lua.set("buttonAvailable", sol::readonly(&Command::buttonAvailable));
		lua.set("buttonDepressed", sol::readonly(&Command::buttonDepressed));
		lua.set("buttonEnabled", sol::readonly(&Command::buttonEnabled));
		lua.set("buttonHighlighted", sol::readonly(&Command::buttonHighlighted));
		lua.set("buttonImpossible", sol::readonly(&Command::buttonImpossible));
		lua.set("buttonWait", sol::readonly(&Command::buttonWait));
		lua.set_function("getText", [this](const std::string& key) { return lang.getText(key); });
		// The build menu tab of a build command: its building's DISPLAY_CATEGORY ("" for none)
		lua.set_function("getCommandBuildCategory", [](const Command* cmd) -> std::string {
			return (cmd && cmd->stampdownObject) ? cmd->stampdownObject->displayCategory : std::string();
		});
		lua.set_function("getCommandHint",
			[this](const Command* cmd, uint32_t objid) -> std::string {
				ClientGameObject* obj = client->findObject(objid);
				if (!obj) return "No object, no hint";
				CliScriptContext ctx{ client, obj };
				auto _ = ctx.change(ctx.selectedObject, obj);
				auto condPred = [&ctx](GSCondition* cond) {return cond->test->booleval(&ctx); };
				auto it = std::find_if_not(cmd->conditionsImpossible.begin(), cmd->conditionsImpossible.end(), condPred);
				if (it != cmd->conditionsImpossible.end()) {
					return (*it)->getFormattedHint(lang, &ctx);
				}
				it = std::find_if_not(cmd->conditionsWait.begin(), cmd->conditionsWait.end(), condPred);
				if (it != cmd->conditionsWait.end()) {
					return (*it)->getFormattedHint(lang, &ctx);
				}
				it = std::find_if_not(cmd->conditionsWarning.begin(), cmd->conditionsWarning.end(), condPred);
				if (it != cmd->conditionsWarning.end()) {
					return (*it)->getFormattedHint(lang, &ctx);
				}
				if (!cmd->defaultHint.empty())
					return GSCondition::getFormattedHint(cmd->defaultHint, cmd->defaultHintValues, lang, &ctx);
				return client->gameSet->commands.getString(cmd);
			}
		);
		lua.set_function("getBuildingSpawnedUnitCommand", [this](uint32_t objid) -> const Command* {
			ClientGameObject* obj = client->findObject(objid);
			return client->gameSet->commands.getPointer(obj->spawnedUnitCommand);
			});
		lua.set_function("getBuildingSpawnedUnitIcon", [this](uint32_t objid) -> std::string {
			ClientGameObject* obj = client->findObject(objid);
			if (!obj) return {};
			const Command* cmd = client->gameSet->commands.getPointer(obj->spawnedUnitCommand);
			if (!cmd) return {};
			return cmd->buttonEnabled;
			});
		// Command list
		enum class CSInfo {
			ENABLED = 0,
			IMPOSSIBLE = 1,
			WAIT = 2
		};
		lua.new_enum("CSInfo",
			"ENABLED", CSInfo::ENABLED,
			"IMPOSSIBLE", CSInfo::IMPOSSIBLE,
			"WAIT", CSInfo::WAIT);
		struct CommandState {
			const Command* command;
			const std::string* texpath;
			CSInfo info;
			int count = 0;
			bool isCurrentOrder = false;
		};
		sol::usertype<CommandState> cs_type = lua.new_usertype<CommandState>("CommandState");
		//cs_type.set("command", sol::readonly(&CommandState::command));
		//cs_type.set("texpath", sol::readonly(&CommandState::texpath));
		cs_type.set("command", sol::readonly_property([](CommandState* cs) {return cs->command; }));
		cs_type.set("texpath", sol::readonly_property([](CommandState* cs) {return *cs->texpath; }));
		cs_type.set("info", sol::readonly(&CommandState::info));
		cs_type.set("count", sol::readonly(&CommandState::count));
		cs_type.set("isCurrentOrder", sol::readonly(&CommandState::isCurrentOrder));
		cs_type.set("buttonAvailable", sol::readonly_property([](CommandState* cs) {return cs->command->buttonAvailable; }));
		cs_type.set("buttonDepressed", sol::readonly_property([](CommandState* cs) {return cs->command->buttonDepressed; }));
		cs_type.set("buttonEnabled", sol::readonly_property([](CommandState* cs) {return cs->command->buttonEnabled; }));
		cs_type.set("buttonHighlighted", sol::readonly_property([](CommandState* cs) {return cs->command->buttonHighlighted; }));
		cs_type.set("buttonImpossible", sol::readonly_property([](CommandState* cs) {return cs->command->buttonImpossible; }));
		cs_type.set("buttonWait", sol::readonly_property([](CommandState* cs) {return cs->command->buttonWait; }));
		
		lua.set_function("listSelectionCommands", [this](const std::function<bool(const std::string&)>& filter) {
			std::vector<CommandState> cmdStates;
			ClientGameObject* sel = nullptr; //selectedObject;
			if (!sel && !selection.empty()) sel = *selection.begin();
			if (sel) {
				CliScriptContext ctx{ client, sel };
				auto _tv = ctx.change(ctx.selectedObject, sel);
				for (Command* cmd : sel->blueprint->offeredCommands) {
					if (cmd->buttonAvailable.empty() && cmd->buttonEnabled.empty())
						continue;
					// a formation offers a command only if one of its members does (Formations.cpp:
					// "These commands will only be offered if the units offer them")
					if (sel->blueprint->bpClass == Tags::GAMEOBJCLASS_FORMATION && !cmd->alwaysOfferedByFormation) {
						bool offered = false;
						for (const auto& [type, members] : sel->children)
							if (!members.empty()) {
								const auto& memberCommands = members[0]->blueprint->offeredCommands;
								offered = offered || std::find(memberCommands.begin(), memberCommands.end(), cmd) != memberCommands.end();
							}
						if (!offered)
							continue;
					}
					const std::string cmdname = client->gameSet->commands.names.getString(cmd->id);
					if (!filter(cmdname))
						continue;
					auto iconPred = [this, &ctx](int eq) {return client->gameSet->equations[eq]->booleval(&ctx); };
					auto condPred = [&ctx](GSCondition* cond) {return cond->test->booleval(&ctx); };
					if (std::all_of(cmd->iconConditions.begin(), cmd->iconConditions.end(), iconPred)) {
						// Icon is shown
						CommandState cs;
						cs.command = cmd;
						if (!std::all_of(cmd->conditionsImpossible.begin(), cmd->conditionsImpossible.end(), condPred)) {
							cs.texpath = &cmd->buttonImpossible;
							cs.info = CSInfo::IMPOSSIBLE;
						}
						else if (!std::all_of(cmd->conditionsWait.begin(), cmd->conditionsWait.end(), condPred)) {
							cs.texpath = &cmd->buttonWait;
							cs.info = CSInfo::WAIT;
						}
						else {
							cs.texpath = cmd->buttonEnabled.empty() ? &cmd->buttonAvailable : &cmd->buttonEnabled;
							cs.info = CSInfo::ENABLED;
						}
						if (!cs.texpath->empty()) {
							if (cmd->order) {
								cs.isCurrentOrder = cmd->order->bpid == sel->reportedCurrentOrder;
								if (auto it = sel->buildingOrderCountMap.find(cmd->order->bpid); it != sel->buildingOrderCountMap.end())
									cs.count = it->second;
							}
							cmdStates.push_back(std::move(cs));
							//cmdStates.push_back(lua.create_table_with(
							//	"command", lua.wrapp cs.command,
							//	"texpath", *cs.texpath,
							//	"info", cs.info
							//));
						}
					}
				}
			}
			return sol::as_nested(cmdStates);
			});
		// Mouse
		lua.set_function("getMouseX", []() {return g_mouseX; });
		lua.set_function("getMouseY", []() {return g_mouseY; });
		lua.set_function("isMousePressed", [](int button) {return g_mousePressed[button % std::size(g_mousePressed)]; });
		lua.set_function("isMouseDown", [](int button) {return g_mouseDown[button % std::size(g_mouseDown)]; });
		// Keyboard
		lua.set_function("isCtrlHeld", []() {return g_modCtrl; });
		lua.set_function("isShiftHeld", []() {return g_modShift; });
		lua.set_function("isAltHeld", []() {return g_modAlt; });
		// ImGui
		lua.set_function("setTooltip",[](const std::string& str) { ImGui::SetTooltip("%s", str.c_str()); });
		// Button
		// Minimap; a click on it is UI input, not a click into the world
		lua.set_function("drawMinimap",
			[this](int x, int y, int w, int h) {
				if (drawMinimap(x, y, w, h))
					clickedScriptedUi = true;
			});
		lua.set_function("drawMinimapButtons",
			[this](int panelX, int panelY) {
				if (drawMinimapButtons(panelX, panelY))
					clickedScriptedUi = true;
			});
		lua.set_function("handleButton",
			[]() -> int {
				clickedScriptedUi = true;
				if (g_mouseReleased[SDL_BUTTON_LEFT]) {
					return 1;
				}
				if (g_mouseReleased[SDL_BUTTON_RIGHT]) {
					return 3;
				}
				return 0;
			});
		lua.set_function("launchCommand",
			[this](const Command* cmd, int assignmentMode, int count) {
				if (cmd->stampdownObject) {
					stampdownBlueprint = cmd->stampdownObject;
					stampdownPlayer = (*selection.begin())->getPlayer();
					stampdownFromCommand = true;
				}
				else if (client->gameSet->commands.getString(cmd) == "Formation") {
					for (ClientGameObject* obj : selection) {
						client->sendPutUnitIntoNewFormation(obj);
					}
					client->sendCreateNewFormation();
				}
				else {
					for (ClientGameObject* obj : selection)
						if (obj)
							if (std::find(obj->blueprint->offeredCommands.begin(), obj->blueprint->offeredCommands.end(), cmd) != obj->blueprint->offeredCommands.end())
								for (int i = 0; i < count; i++)
									client->sendCommand(obj, cmd, assignmentMode);
				}
			});
		lua.set_function("forceLaunchCommand",
			[this](const Command* cmd, int assignmentMode) {
				for (ClientGameObject* obj : selection)
					if (obj)
						client->sendCommand(obj, cmd, assignmentMode);
			});
		lua.set_function("cancelCommand",
			[this](const Command* cmd, int count) {
				for (ClientGameObject* obj : selection)
					if (obj)
						for (int i = 0; i < count; ++i)
							client->sendCancelCommand(obj, cmd);
			});
		// Tags
		auto addTags = [](auto& lua, auto& tagDict, const std::string& prefix) {
			for (size_t i = 0; i < tagDict.tags.size(); ++i) {
				lua.set(prefix + tagDict.tags[i], i);
			}
		};
		addTags(lua, Tags::GAMEOBJCLASS_tagDict, "GAMEOBJCLASS_");
		addTags(lua, Tags::ORDERASSIGNMODE_tagDict, "ORDERASSIGNMODE_");
		// Misc
		//lua.set_function("sin",static_cast<double(*)(double)>(&std::sin));
		//lua.set_function("round",static_cast<double(*)(double)>(&std::round));

		//luaScript = lua.load_file("redata/ClientUIScript.lua");
		luaInitialized = true;
		try {
			// ClientUIScript.lua (a mod's or redata's), then the mods' Lua\InGame\*.lua
			if (!RunUIScripts(lua, "ClientUIScript.lua", "Lua\\InGame", "In-game UI", &luaScriptFiles))
				uiScriptOn = false;
		}
		catch (const sol::error& err) {
			printf("Sol init error:\n%s\n", err.what());
			uiScriptOn = false;
		}
	}
	if (uiScriptOn) {
		try {
			lua["CCUI_PerFrame"]();
		}
		catch (const sol::error& err) {
			printf("Sol error:\n%s\n", err.what());
			uiScriptOn = false;
		}
	}
	if (!uiScriptOn && luaInitialized) {
		luaInitialized = false;
		luaScript = {};
		lua = {};
	}

	if (frontEnd) {
		if (menuOpen)
			menuInput.give();
		frontEnd->frame(FrontEnd::Place::IN_GAME);
		for (const FrontEnd::Request& r : frontEnd->takeRequests()) {
			if (r.action == "quit")
				g_windowQuit = true;
			else if (r.action == "exit_to_menu" || r.action == "restart" || r.action == "start") {
				leaveAction = r.action;
				leaveArg = r.arg;
			}
			else if (r.action == "save")
				saveRequested(r.arg);
		}
		if (menuPausedGame && !frontEnd->isOpen()) {
			client->sendPauseRequest(false);
			menuPausedGame = false;
		}
	}

	TracyCZoneEnd(zoneUi);

	//----- Rendering -----//
	TracyCZoneN(zoneScene, "Scene building", 1);

	// Fog colour: the map's, or the settings window's override
	uint32_t fogColour = client->terrain ? client->terrain->fogColor : 0;
	if (g_renderSettings.fogColourOverride) {
		auto channel = [](float c) { return (uint32_t)std::clamp((int)std::lround(c * 255.0f), 0, 255); };
		fogColour = (channel(g_renderSettings.fogColour[0]) << 16) | (channel(g_renderSettings.fogColour[1]) << 8) | channel(g_renderSettings.fogColour[2]);
	}
	gfx->ClearFrame(true, true, fogColour);
	gfx->BeginDrawing();

	Vector3 benchmarkPosition, benchmarkOrientation;
	if (BenchmarkCamera(benchmarkPosition, benchmarkOrientation)) {
		client->camera.position = benchmarkPosition;
		client->camera.orientation = benchmarkOrientation;
	}
	// View distance (the original's far_clip, default 350, slider 280-1000; it also sets the
	// fog) or the whole map, and the field of view, from the settings
	if (g_renderSettings.farClipWholeMap && client->terrain) {
		const float mapWidth = 5.0f * client->terrain->width, mapHeight = 5.0f * client->terrain->height;
		client->camera.farDist = std::sqrt(mapWidth * mapWidth + mapHeight * mapHeight) / 0.95f + 1.0f;
	}
	else
		client->camera.farDist = g_renderSettings.farClip;
	// Near plane: the original's near_clip of 1, pushed out for very long view distances to keep
	// the depth precision
	client->camera.nearDist = std::max(1.0f, client->camera.farDist / 10000.0f);
	client->camera.fovY = g_renderSettings.fovY();
	client->camera.updateMatrix();
	gfx->SetTransformMatrix(&client->camera.sceneMatrix);

	gfx->SetFog(fogColour, client->camera.farDist);
	if (auto* dgfx = dynamic_cast<DiligentGfx*>(gfx)) {
		dgfx->SetFogCamera(client->camera.position);
		// Map-edge fog: opaque inside the band between the playable area and the mesh's edge
		if (client->terrain && g_renderSettings.edgeFog) {
			const auto [areaWidth, areaDepth] = client->terrain->getPlayableArea();
			const float band = 5.0f * client->terrain->edge;
			// With the original's fog and view distance the distance fog hides the far terrain, and
			// the edge band keeps the original's distant mountains: a gentle edge fog. Longer views
			// (fog off, own fog distances, whole map, a view distance well beyond the original's)
			// use the settings' stronger edge fog.
			const RenderSettings& rs = g_renderSettings;
			const bool longView = rs.fogOff || rs.fogCustom || rs.farClipWholeMap || rs.farClip > 1.5f * 350.0f;
			float opaque = 0.95f, start = 0.0f;
			if (longView) {
				opaque = std::clamp(rs.edgeFogOpaque, 0.0f, 0.95f);
				start = std::min(std::clamp(rs.edgeFogStart, -1.0f, 0.95f), opaque - 0.001f);
			}
			dgfx->SetFogEdge(areaWidth, areaDepth, band * start, band * opaque);
		}
		else
			dgfx->SetFogEdge(1e9f, 1e9f, 1e9f, 2e9f);
	}
	if (skyRenderer)
		skyRenderer->render();

	// Get ray
	rayDirection = getRay(client->camera).normal();
	nextSelectedObject = nullptr;
	nextSelectedObjectByMesh = nullptr;
	nextSelectedObjectBySphere = nullptr;
	nextSelObjDistanceByMesh = std::numeric_limits<float>::infinity();
	nextSelObjDistanceBySphere = std::numeric_limits<float>::infinity();
	nextSelFromBox.clear();

	if (client->gameSet) {
		if(!scene)
			scene = new Scene(gfx, &client->gameSet->modelCache);
		if (!particlesContainer)
			particlesContainer = new ParticleContainer;
		if (!psCache)
			psCache = new PSCache("Warrior Kings Game Set\\Particle_Systems");
		if (!particleRenderer)
			particleRenderer = new DefaultParticleRenderer(gfx, particlesContainer);

		numObjectsDrawn = 0;
		attachSceneEntities.clear();
		gpuBlobShadows = nullptr;
		if (auto* dgfx = dynamic_cast<DiligentGfx*>(gfx); dgfx && client->terrain && UseEnhancedRenderers(gfx)) {
			gpuBlobShadows = &dgfx->shadows.blobs;
			gpuBlobShadows->clear();
		}
		if (client->getLevel())
			drawObject(client->getLevel());
		nextSelectedObject = nextSelectedObjectByMesh ? nextSelectedObjectByMesh : nextSelectedObjectBySphere;

		// highlight hovered object
		if (nextSelectedObject) {
			highlightedObjects.insert(nextSelectedObject);
			if (nextSelectedObject->blueprint->bpClass == Tags::GAMEOBJCLASS_CHARACTER
				&& nextSelectedObject->parent && nextSelectedObject->parent->blueprint->bpClass == Tags::GAMEOBJCLASS_FORMATION) {
				for (const auto& [chType, chList] : nextSelectedObject->parent->children) {
					if (chType.bpClass() == Tags::GAMEOBJCLASS_CHARACTER) {
						for (auto* child : chList) {
							highlightedObjects.insert(child->dyncast<ClientGameObject>());
						}
					}
				}
			}
		}
		static ClientInterface* healthTexOwner = nullptr; // from this ClientInterface's uiTexCache
		static texture healthTex[8];
		if (healthTexOwner != this) {
			healthTexOwner = this;
			for (int i = 0; i < 8; ++i) {
				std::string name = "Interface\\InGame\\health" + std::to_string(i + 1) + ".tga";
				healthTex[i] = uiTexCache.getTexture(name.c_str(), false);
			}
		}
		for (const auto& obj : highlightedObjects) {
			obj->sceneEntity.flags |= SceneEntity::SEFLAG_NOLIGHTING;
			if (obj->blueprint->bpClass == Tags::GAMEOBJCLASS_CHARACTER && terrainSpriteContainer && client->terrain) {
				float health = obj->getItem(Tags::PDITEM_HIT_POINTS);
				float capacity = obj->getItem(Tags::PDITEM_HIT_POINT_CAPACITY);
				Model* model = obj->getModel();
				if (model) {
					int healthIndex = (capacity > 0.0f) ? std::clamp(static_cast<int>(health * 8.0f / capacity), 0, 7) : 0;
					const float radius = model->getStaticModel()->getSphereRadius();
					const Vector3 pos = obj->position + Vector3(1, 0, 1) * (client->terrain->edge * 5.0f - radius);
					TerrainSpriteContainer::Point p1{ pos.x, pos.z };
					TerrainSpriteContainer::Point p2{ pos.x + 2.0f * radius, pos.z };
					TerrainSpriteContainer::Point p3{ pos.x, pos.z + 2.0f * radius };
					terrainSpriteContainer->addSprite(healthTex[healthIndex], p1, p2, p3);
				}
			}
		}
		highlightedObjects.clear();

		// The range circles of the selected objects (DISPLAY_SPRITE_WHEN_SELECTED, and those of the
		// objects that DISPLAY_SPRITE_WHEN_SELECTED_FOR finds), and later of the building being
		// placed: band textures on the ground, the value being the radius in metres
		rangeBands.clear();
		auto addRangeBand = [this](const GameObjBlueprint::RangeSprite& band, const Vector3& centre, ScriptContext* ctx) {
			if (!band.radius || !client->terrain)
				return;
			const float r = band.radius->eval(ctx);
			if (!(r > 0.0f && r < 5000.0f))
				return;
			if (texture tex = uiTexCache.getTexture(band.texture.c_str(), false))
				rangeBands.push_back({ tex, centre, r });
		};
		auto addSelectedBands = [&](ClientGameObject* obj) {
			CliScriptContext ctx{ client, obj };
			for (const auto& band : obj->blueprint->spritesWhenSelected)
				addRangeBand(band, obj->position, &ctx);
		};
		for (ClientGameObject* sel : selection) {
			if (!sel)
				continue;
			addSelectedBands(sel);
			for (ObjectFinder* finder : sel->blueprint->spritesWhenSelectedFor) {
				CliScriptContext ctx{ client, sel };
				for (CommonGameObject* found : finder->eval(&ctx))
					if (found && !selection.count(found->dyncast<ClientGameObject>()))
						addSelectedBands(found->dyncast<ClientGameObject>());
			}
		}

		// test
		//Vector3 peapos = client->camera.position + getRay(client->camera).normal() * 16;
		peapos = Vector3(0, 0, 0);
		if(client->terrain)
			peapos = CalcStampdownPos(client->camera.position, rayDirection, *client->terrain);
		SceneEntity test;
		if (stampdownBlueprint && stampdownPlayer) {
			//test.model = client->gameSet->modelCache.getModel("Warrior Kings Game Set\\Characters\\Peasant\\Male\\Peasant1.MESH3");
			try {
				test.model = stampdownBlueprint->subtypes.at(0).appearances.at(0).animations.at(0).at(0);
				if (auto* fp = stampdownBlueprint->footprint) {
					peapos.x = std::floor(peapos.x / 5.0f) * 5.0f + 2.5f + fp->originX;
					peapos.z = std::floor(peapos.z / 5.0f) * 5.0f + 2.5f + fp->originZ;
					if (client->terrain)
						peapos.y = client->terrain->getHeightEx(peapos.x, peapos.z, stampdownBlueprint->canWalkOnWater());
				}
				test.transform = Matrix::getTranslationMatrix(peapos);
				test.color = stampdownPlayer->color;
				scene->add(&test);

				static std::vector<SceneEntity> planEntities;
				planEntities.clear();
				auto plan = StampdownPlan::getStampdownPlan(client, client->clientPlayer.get(), stampdownBlueprint, peapos, Vector3(0, 0, 0));
				// where it can't stand (on another building), red; the server refuses it too
				if (plan.status != StampdownPlan::Status::Valid)
					test.flags |= SceneEntity::SEFLAG_BLOCKED;
				// a field (its whole footprint passable) shows green where it can be placed
				else if (const Footprint* fp = stampdownBlueprint->footprint; fp && !fp->tiles.empty()
					&& std::all_of(fp->tiles.begin(), fp->tiles.end(), [](const auto& t) { return t.mode != 0; }))
					test.flags |= SceneEntity::SEFLAG_PLACEABLE;
				// its range circles, the values those of the player ("run on player!" in the game set)
				{
					CliScriptContext ctx{ client, stampdownPlayer.get() };
					for (const auto& band : stampdownBlueprint->spritesWhenStampingDown)
						addRangeBand(band, peapos, &ctx);
				}
				planEntities.reserve(plan.toCreate.size());
				for (auto& creation : plan.toCreate) {
					auto& entity = planEntities.emplace_back();
					entity.transform = Matrix::getRotationYMatrix(-creation.orientation.y) * Matrix::getTranslationMatrix(creation.position);
					entity.model = creation.blueprint->subtypes.at(0).appearances.at(0).animations.at(0).at(0);
					entity.color = stampdownPlayer->color;
				}
				for (auto& entity : planEntities) {
					scene->add(&entity);
				}
			}
			catch (const std::out_of_range &) {
				// nop
			}
		}
		for (auto& sfx : client->specialEffects) {
			if (sfx.attachedObj)
				sfx.entity.transform = sfx.attachedObj->getWorldMatrix();
			Vector3 ttpp = sfx.entity.transform.getTranslationVector().transformScreenCoords(client->camera.sceneMatrix);
			if (ttpp.x >= -1 && ttpp.x <= 1 && ttpp.y >= -1 && ttpp.y <= 1 && ttpp.z >= -1 && ttpp.z <= 1) {
				sfx.entity.animTime = (uint32_t)((client->timeManager.currentTime - sfx.startTime) * 1000.0f);
				scene->add(&sfx.entity);
				drawAttachmentPoints(&sfx.entity);
			}
		}
		if (orderFlagActive && orderFlagEntity.model) {
			const float age = (SDL_GetTicks() - orderFlagStartTicks) / 1000.0f;
			if (age >= 0.0f && !orderFlagDone(age)) {
				orderFlagEntity.animTime = (uint32_t)(age * 1000.0f);
				scene->add(&orderFlagEntity);
			}
			else
				orderFlagActive = false;
		}
		// The rally points of the selected buildings (where their new units go), with the
		// original's rally flag
		rallyFlagEntities.clear();
		static const char* rallyFlagPath = "Warrior Kings Game Set\\Flags\\Land_Spawn_Rally_Point.ANIM3";
		for (ClientGameObject* sel : selection) {
			if (!sel || sel->blueprint->bpClass != Tags::GAMEOBJCLASS_BUILDING || sel->spawnedUnitCommand < 0
				|| sel->spawnedUnitDestination.x < 0.0f || !client->terrain || !FileExists(rallyFlagPath))
				continue;
			const Vector3& dest = sel->spawnedUnitDestination;
			SceneEntity& flag = rallyFlagEntities.emplace_back();
			flag.model = client->gameSet->modelCache.getModel(rallyFlagPath);
			flag.transform = Matrix::getTranslationMatrix(Vector3(dest.x, client->terrain->getHeightEx(dest.x, dest.z, true), dest.z));
			flag.color = sel->getPlayer()->color;
			flag.animTime = (uint32_t)SDL_GetTicks();
			scene->add(&flag);
		}
		for (auto& lsfx : client->loopingSpecialEffects) {
			Vector3 ttpp = lsfx.second.transform.getTranslationVector().transformScreenCoords(client->camera.sceneMatrix);
			if (ttpp.x >= -1 && ttpp.x <= 1 && ttpp.y >= -1 && ttpp.y <= 1 && ttpp.z >= -1 && ttpp.z <= 1) {
				lsfx.second.animTime = (uint32_t)(client->timeManager.currentTime * 1000.0f);
				scene->add(&lsfx.second);
				drawAttachmentPoints(&lsfx.second);
			}
		}
		if (client->terrain) {
			scene->sunDirection = client->terrain->sunVector;
			scene->sunColor = client->terrain->sunColor;
		}
		if (auto* dgfx = dynamic_cast<DiligentGfx*>(gfx)) {
			dgfx->shadows.time = client->timeManager.currentTime;
			dgfx->shadows.cloudShadows = g_renderSettings.cloudShadows;
			dgfx->shadows.staticShadows = g_renderSettings.staticShadows;
		}
		if (!sceneRenderer) {
			if (UseEnhancedRenderers(gfx))
				sceneRenderer = new DiligentEnhancedSceneRenderer(gfx, scene, &client->camera);
			else
				sceneRenderer = new DefaultSceneRenderer(gfx, scene, &client->camera);
		}
		TracyCZoneEnd(zoneScene);
		// Once per level, then about once a second for objects that arrived since
		static const bool prewarm = g_settings.value<bool>("prewarmModels", true);
		if (prewarm && (prewarmedLevel != client->getLevel() || --prewarmCountdown <= 0))
			prewarmLevel();
		{
			ZoneScopedN("Scene rendering");
			sceneRenderer->render();
		}
		scene->clear();
	}
	else {
		TracyCZoneEnd(zoneScene);
	}

	if (terrainRenderer && showTerrain) {
		ZoneScopedN("Terrain rendering");
		if (bool* bump = terrainRenderer->bumpMapping())
			*bump = g_renderSettings.bumpMapping;
		terrainRenderer->render();
	}

	if (terrainSpriteRenderer && showTerrain) {
		ZoneScopedN("Terrain sprites");
		terrainSpriteRenderer->render();
	}
	if (terrainSpriteContainer)
		terrainSpriteContainer->clear();
	if (showTerrain)
		drawRangeBands();

	if (particleRenderer && particlesContainer) {
		ZoneScopedN("Particles");
		gfx->BeginParticles();
		gfx->BeginBatchDrawing();
		particleRenderer->render(client->timeManager.previousTime, client->timeManager.currentTime, client->camera);
		particlesContainer->update(client->timeManager.currentTime);
	}

	if (selBoxOn) {
		gfx->InitRectDrawing();
		gfx->NoTexture(0);
		gfx->DrawFrame(selBoxStartX, selBoxStartY, selBoxEndX - selBoxStartX, selBoxEndY - selBoxStartY, -1);
	}

	{
		ZoneScopedN("ImGui rendering");
		gfx->InitImGuiDrawing();
		ImGuiImpl_Render(gfx);
	}

	{
		ZoneScopedN("Present");
		if (const char* screenshot = BenchmarkScreenshotDue())
			printf("Screenshot %s: %s\n", screenshot, gfx->SaveScreenshot(screenshot) ? "saved" : "failed");
		gfx->EndDrawing();
	}
	g_frameStats.frameEnd();
	// Test runs: "benchmark": { ..., "census": <seconds> } prints each player's characters and
	// buildings by type every that many seconds of game time ("CENSUS t=... player ...")
	if (BenchmarkEnabled() && client->getLevel()) {
		static float nextCensus = 0.0f;
		const float every = g_settings.value("benchmark", nlohmann::json()).value("census", 0.0f);
		if (every > 0.0f && client->timeManager.currentTime >= nextCensus) {
			nextCensus = client->timeManager.currentTime + every;
			for (const auto& [type, players] : client->getLevel()->children) {
				for (CommonGameObject* player : players) {
					if (player->blueprint->bpClass != Tags::GAMEOBJCLASS_PLAYER)
						continue;
					std::map<std::string, int> counts;
					int characters = 0, buildings = 0;
					auto count = [&](CommonGameObject* obj, auto& self) -> void {
						for (const auto& [ctype, children] : obj->children)
							for (CommonGameObject* child : children) {
								const int cls = child->blueprint->bpClass;
								if (cls == Tags::GAMEOBJCLASS_CHARACTER) {
									characters++;
									counts[child->blueprint->name]++;
									// what they are doing (single player: the local server's objects)
									if (Server* server = Server::instance)
										if (ServerGameObject* sobj = server->findObject(child->id)) {
											const Order* order = sobj->orderConfig.getCurrentOrder();
											counts["order:" + (order ? server->gameSet->orders.getString(order->blueprint->bpid) : std::string("idle"))]++;
										}
								}
								else if (cls == Tags::GAMEOBJCLASS_BUILDING) {
									buildings++;
									counts[child->blueprint->name]++;
									// resources stored in buildings (villages, warehouses)
									for (int item : { Tags::PDITEM_FOOD, Tags::PDITEM_WOOD, Tags::PDITEM_GOLD })
										if (const float v = child->getItem(item); v != 0.0f)
											counts[std::string("stock:") + child->blueprint->name + ":" + Tags::PDITEM_tagDict.getStringFromID(item)] += (int)v;
								}
								self(child, self);
							}
					};
					count(player, count);
					printf("CENSUS t=%.0f player %u (%s): food %.0f wood %.0f gold %.0f, %d characters, %d buildings:", client->timeManager.currentTime,
						player->id, player->blueprint->name.c_str(), player->getItem(Tags::PDITEM_FOOD), player->getItem(Tags::PDITEM_WOOD),
						player->getItem(Tags::PDITEM_GOLD), characters, buildings);
					for (const auto& [name, n] : counts)
						printf(" %s=%d", name.c_str(), n);
					printf("\n");
				}
			}
			fflush(stdout);
		}
		// "traceOrder": "<order>": every 2 s of game time, the first unit doing that order (its
		// task, target and items), to follow a unit's work in a test run ("TRACE t=...")
		const std::string traceOrder = g_settings.value("benchmark", nlohmann::json()).value("traceOrder", std::string());
		static float nextTrace = 0.0f;
		static uint32_t tracedId = 0;
		if (!traceOrder.empty() && Server::instance && client->timeManager.currentTime >= nextTrace) {
			nextTrace = client->timeManager.currentTime + 2.0f;
			Server* server = Server::instance;
			const int orderIndex = server->gameSet->orders.names.getIndex(traceOrder);
			ServerGameObject* traced = tracedId ? server->findObject(tracedId) : nullptr;
			if (!traced && orderIndex >= 0) {
				for (const auto& [id, obj] : server->idmap) {
					auto* s = obj->dyncast<ServerGameObject>();
					const Order* o = s->orderConfig.getCurrentOrder();
					if (o && o->blueprint->bpid == orderIndex) { traced = s; tracedId = id; break; }
				}
			}
			if (traced) {
				std::string task = "-", target = "-";
				float distance = -1.0f;
				if (Order* o = traced->orderConfig.getCurrentOrder()) {
					task = server->gameSet->orders.getString(o->blueprint->bpid);
					if (Task* t = o->getCurrentTask()) {
						task += " / " + server->gameSet->tasks.getString(t->blueprint->bpid);
						if (ServerGameObject* tg = t->target.get()) {
							target = tg->blueprint->name + " #" + std::to_string(tg->id) + " at (" + std::to_string((int)tg->position.x) + ","
								+ std::to_string((int)tg->position.z) + ") footprint tiles " + std::to_string(tg->blueprint->footprint ? tg->blueprint->footprint->tiles.size() : 0);
							distance = (tg->position - traced->position).len2xz();
						}
					}
				}
				printf("TRACE t=%.0f unit %u at (%.0f,%.0f) moving %d anim %d: %s -> %s dist %.1f items:", client->timeManager.currentTime, traced->id,
					traced->position.x, traced->position.z, (int)traced->movement.isMoving(), traced->animationIndex, task.c_str(), target.c_str(), distance);
				for (const auto& [item, value] : traced->items)
					if (value != 0.0f)
						printf(" %s=%g", server->gameSet->items.names.getString(item).c_str(), value);
				printf("\n");
				fflush(stdout);
			}
		}
	}
	BenchmarkFrameEnd(client->terrain && client->gameSet);
	HandleWindow();

	nextFps++;
	time_t tim = time(NULL);
	if (tim >= lastfpscheck + 1) {
		framesPerSecond = nextFps;
		nextFps = 0;
		lastfpscheck = tim;
	}
}

void ClientInterface::render()
{
	if(this->terrainRenderer)
		this->terrainRenderer->render();
}

void ClientInterface::updateTerrain() {
	delete skyRenderer;
	skyRenderer = nullptr;
	if (UseEnhancedRenderers(gfx))
		skyRenderer = new DiligentSkyRenderer(gfx, client->terrain, &client->camera);
	if (UseEnhancedRenderers(gfx))
		this->terrainRenderer = new DiligentEnhancedTerrainRenderer(gfx, client->terrain, &client->camera);
	else
		this->terrainRenderer = new DefaultTerrainRenderer(gfx, client->terrain, &client->camera);
	this->terrainSpriteContainer = new TerrainSpriteContainer;
	this->terrainSpriteRenderer = new TerrainSpriteRenderer(gfx, client->terrain, this->terrainSpriteContainer);
}

void ClientInterface::saveRequested(const std::string& name)
{
	const bool ok = saveGame && !name.empty() && saveGame(name);
	if (FrontEnd* frontEnd = FrontEnd::get()) {
		if (ok)
			frontEnd->setCurrentSave(name);
		frontEnd->saved(ok, name);
	}
}
