// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#include "ClientInterface.h"
#include "ChronicleNarrator.h"
#include "../gfx/GpuProfiler.h"
#include "../gfx/TerrainRenderer.h"
#include "../gfx/DefaultTerrainRenderer.h"
#include "../client.h"
#include "../gfx/renderer.h"
#include "ClientDebugger.h"
#include "../imguiimpl.h"
#include "../window.h"
#include <SDL3/SDL_scancode.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_mouse.h>
#include "../tags.h"
#include "../gameset/gameset.h"
#include "../gfx/SceneRenderer.h"
#include "../gfx/DefaultSceneRenderer.h"
#include "../imgui/imgui.h"
#include <ctime>
#include <map>
#include <optional>
#include <random>
#include "../terrain.h"
#include <SDL3/SDL_timer.h>
#include "../gameset/ScriptContext.h"
#include <stdexcept>
#include "../ParticleContainer.h"
#include "../ParticleSystem.h"
#include "../gfx/DefaultParticleRenderer.h"
#include "EnvironmentView.h"
#include "../TreeModels.h"
#include "../settings.h"
#include <nlohmann/json.hpp>
#include "../gfx/renderer_diligent.h"
#include "../gfx/DiligentEnhancedSceneRenderer.h"
#include "../gfx/DiligentEnhancedTerrainRenderer.h"
#include "../gfx/DiligentSkyRenderer.h"
#include "../gfx/RenderSettings.h"
#include "../StampdownPlan.h"
#include "../gfx/BitmapFont.h"
#include "../gfx/TerrainSpriteContainer.h"
#include "../gfx/TerrainSpriteRenderer.h"
#include "../gfx/bitmap.h"
#include "../Mesh.h"
#include "../file.h"

#define SOL_NO_CHECK_NUMBER_PRECISION 1
#include <sol/sol.hpp>
#include "FrontEnd.h"
#include "KeyBindings.h"
#include "LuaScripts.h"
#include "../gameset/finder.h"
#include "../gameset/Footprint.h"
#include "../server.h"
#include "../gameset/OrderBlueprint.h"
#include <tracy/Tracy.hpp>
#include <tracy/TracyC.h>
#include "../util/FrameStats.h"
#include "../util/LoadingProgress.h"

// The players' colours by index, taken from the HUD's Interface/InGame/Top Panel/Player<n>.tga
static const ImU32 playerColors[9] = {
	IM_COL32(200, 200, 200, 255), IM_COL32(114, 97, 187, 255), IM_COL32(212, 198, 74, 255),
	IM_COL32(226, 50, 55, 255), IM_COL32(93, 155, 57, 255), IM_COL32(212, 91, 173, 255),
	IM_COL32(204, 99, 55, 255), IM_COL32(103, 160, 182, 255), IM_COL32(180, 74, 32, 255),
};

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

ClientInterface::~ClientInterface()
{
	SetKeyCheatGate(nullptr);
}

bool ClientInterface::cheatKeysAllowed() const
{
	if (!withServer || !singlePlayer || replayMode || !Server::instance)
		return false;
	return Server::instance->cheatMode || editorAllowed || BenchmarkEnabled();
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

	// wkbai's weather: the snow and the ice
	DrawMinimapWeather(gfx, client, drawList, mapX, mapY, mapX + mapW, mapY + mapH);
	// the fog of war, darker where the player's units don't see (as on the original's minimap)
	const bool fogOn = fogOfWarOn() && !fogOfWar.tiles.empty();
	if (fogOn && fogOfWar.version != minimapFogVersion) {
		Bitmap bmp;
		bmp.width = (int)fogOfWar.width;
		bmp.height = (int)fogOfWar.height;
		bmp.format = BMFORMAT_R8G8B8A8;
		bmp.pixels.resize((size_t)bmp.width * bmp.height * 4);
		const float dark = 1.0f - g_renderSettings.fogOfWarBrightness;
		for (int z = 0; z < bmp.height; z++)
			for (int x = 0; x < bmp.width; x++) {
				// image row 0 is the north edge (largest z)
				uint8_t* px = &bmp.pixels[((size_t)(bmp.height - 1 - z) * bmp.width + x) * 4];
				px[0] = px[1] = px[2] = 0;
				px[3] = (uint8_t)((255 - fogOfWar.tiles[z * fogOfWar.width + x]) * dark);
			}
		if (minimapFogTexture && (minimapFogWidth != fogOfWar.width || minimapFogHeight != fogOfWar.height)) {
			gfx->FreeTexture(minimapFogTexture);
			minimapFogTexture = nullptr;
		}
		if (minimapFogTexture)
			gfx->UpdateTexture(minimapFogTexture, bmp);
		else
			minimapFogTexture = gfx->CreateTexture(bmp, 1);
		minimapFogWidth = fogOfWar.width;
		minimapFogHeight = fogOfWar.height;
		minimapFogVersion = fogOfWar.version;
	}
	if (fogOn && minimapFogTexture)
		drawList->AddImage(minimapFogTexture, ImVec2(mapX, mapY), ImVec2(mapX + mapW, mapY + mapH));

	// Objects whose blueprint has DISPLAY_ON_MINIMAP, in their player's colour
	static const ImU32 treeColor = IM_COL32(28, 72, 24, 255);
	auto drawDot = [&](ClientGameObject* obj, bool remembered) {
		if (!obj->blueprint || !obj->blueprint->displayOnMinimap || (obj->flags & ClientGameObject::fTerminated))
			return;
		// enemy characters in the fog of war aren't shown, and enemy buildings only as last seen
		if (fogOn && (obj->blueprint->bpClass == Tags::GAMEOBJCLASS_CHARACTER || obj->blueprint->bpClass == Tags::GAMEOBJCLASS_MISSILE)
			&& !fogOfWar.isSeen(obj->position) && !fogOfWar.isFriendly(client, obj))
			return;
		if (fogOn && !remembered && obj->blueprint->bpClass == Tags::GAMEOBJCLASS_BUILDING && !fogOfWar.isBuildingSeen(obj->position) && fogOfWar.isRemembered(client, obj))
			return;
		// Hidden when all its display categories are toggled off
		const uint8_t categories = obj->blueprint->minimapCategories;
		if (categories && !(categories & ~minimapHiddenCategories))
			return;
		CommonGameObject* player = obj->getPlayer();
		int color = remembered ? obj->color : player ? player->color : 0;
		ImU32 dotColor = playerColors[(color >= 0 && color < 9) ? color : 0];
		if (color == 0 && obj->blueprint->bpClass == Tags::GAMEOBJCLASS_CONTAINER)
			dotColor = treeColor; // neutral resources such as trees
		const float size = (obj->blueprint->bpClass == Tags::GAMEOBJCLASS_BUILDING) ? 2.5f : 1.5f;
		ImVec2 c = toMinimap(obj->position.x, obj->position.z);
		drawList->AddRectFilled(ImVec2(c.x - size, c.y - size), ImVec2(c.x + size, c.y + size), dotColor);
	};
	auto drawDots = [&](ClientGameObject* obj, const auto& self) -> void {
		for (const auto& [type, children] : obj->children)
			for (CommonGameObject* child : children)
				self((ClientGameObject*)child, self);
		drawDot(obj, false);
	};
	if (client->getLevel())
		drawDots(client->getLevel(), drawDots);
	if (fogOn)
		for (const auto& [id, copy] : fogOfWar.remembered)
			if (!fogOfWar.isBuildingSeen(copy->position))
				drawDot(copy.get(), true);
	// a replay's events, marked for a while
	if (replayMode) {
		drawList->PushClipRect(p0, p1, true);
		replayView.drawMinimapMarkers(drawList, uiTexCache, [&](float px, float pz) { return toMinimap(px, pz); }, (float)std::min(w, h));
		drawList->PopClipRect();
	}

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
	const int mx = UiMouseX(), my = UiMouseY(); // the minimap is in points
	const bool hovered = mx >= x && mx < x + w && my >= y && my < y + h;
	if (hovered && g_mouseDown[SDL_BUTTON_LEFT]) {
		// in the map part; a click on the bars goes to the nearest map edge
		float targetX = std::clamp((mx - mapX) / mapW, 0.0f, 1.0f) * areaWidth;
		float targetZ = (1.0f - std::clamp((my - mapY) / mapH, 0.0f, 1.0f)) * areaDepth;
		client->camera.position.x += targetX - lookAt.x;
		client->camera.position.z += targetZ - lookAt.z;
	}
	return hovered;
}

// The original's minimap panel (CMinimapPanel) has four buttons on the arcs around the map:
// military units (left), civilian units (bottom), resources (right), each toggling that
// MINIMAP_DISPLAY_CATEGORY on the map, and the peasant activity interface (top). A category
// shown on the map shows the "Depressed" image (its green rim), a toggled-off one the panel's.
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
		{ "Interface\\InGame\\Statistics Mini Map Button Depressed.tga", 71, 7, 0 },
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
		const int lx = UiMouseX() - (panelX + button.x), ly = UiMouseY() - (panelY + button.y);
		const bool hovered = lx >= 0 && ly >= 0 && lx < button.mask.width && ly < button.mask.height
			&& button.mask.pixels[4 * (ly * button.mask.width + lx) + 3] >= 128;
		hoveredAny |= hovered;
		if (hovered && button.category && g_mousePressed[SDL_BUTTON_LEFT])
			minimapHiddenCategories ^= button.category;
		// the top arc: the peasant activity interface (WKB.exe's CEconomicStatsWnd), each click
		// the next of closed, the idle peasants' bar, all five activities' bars
		if (hovered && !button.category) {
			ImGui::SetTooltip("Display Peasant Activity Interface");
			if (g_mousePressed[SDL_BUTTON_LEFT])
				peasantActivityMode = peasantActivityMode == -1 ? 0 : peasantActivityMode == 0 ? 1 : -1;
		}
		// the "Depressed" image (green-rimmed) shows what is on: a category shown on the map, the
		// peasant activity interface open
		const bool depressed = button.category ? (minimapHiddenCategories & button.category) == 0 : peasantActivityMode >= 0;
		if (depressed) {
			const ImVec2 p0((float)(panelX + button.x), (float)(panelY + button.y));
			drawList->AddImage(button.tex, p0, ImVec2(p0.x + button.mask.width, p0.y + button.mask.height));
		}
	}
	return hoveredAny;
}

// A peasant activity row's icon clicked: the player's peasants doing that activity (by the
// order the server reports for each, as Server::updatePeasantActivity counts them) become the
// selection (shift: are added); rows 1-5: shop keepers and gold miners, farmers, wood choppers
// and stone miners, builders, idle peasants. Returns how many.
int ClientInterface::selectPeasantsByActivity(int row)
{
		const GameSet* gs = client->gameSet.get();
		ClientGameObject* player = client->clientPlayer.get();
		if (!gs || !player)
			return 0;
		static const char* rowCategories[5][2] = { { "Order Shop Keeping", "Order Gold Mining" }, { "Order Farming", nullptr },
			{ "Order Wood Chopping", "Order Mining" }, { "Order Build", "Order Repair" }, { nullptr, nullptr } };
		if (row < 1 || row > 5)
			return 0;
		std::set<int> categories;
		for (const char* name : rowCategories[row - 1])
			if (name)
				if (int c = gs->orderCategories.names.getIndex(name); c >= 0)
					categories.insert(c);
		const int isPeasant = gs->items.names.getIndex("I Am A Peasant");
		if (isPeasant < 0)
			return 0;
		std::vector<ClientGameObject*> found;
		auto walk = [&](ClientGameObject* obj, auto& self) -> void {
			for (const auto& [type, children] : obj->children)
				for (CommonGameObject* c : children) {
					auto* o = (ClientGameObject*)c;
					if (o->blueprint->bpClass == Tags::GAMEOBJCLASS_CHARACTER && !(o->flags & ClientGameObject::fTerminated) && o->getItem(isPeasant) > 0.0f) {
						const int order = o->reportedCurrentOrder;
						const bool match = row == 5 ? order < 0
							: order >= 0 && order < (int)gs->orders.size() && categories.count(gs->orders[order].category);
						if (match)
							found.push_back(o);
					}
					self(o, self);
				}
		};
		walk(player, walk);
		if (!found.empty()) {
			if (!g_modShift)
				selection.clear();
			for (ClientGameObject* o : found)
				selection.insert(o);
		}
		return (int)found.size();
}

// The enhanced scene and terrain renderers draw with Diligent directly, so they need the
// Diligent renderer (not the null renderer).
bool UseEnhancedRenderers(IRenderer* gfx)
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
	// a right-drag's flag stands until the button is released
	if (formationDrag.active)
		return false;
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

// As WKB.exe: on a selection change it lists the selected objects' offered commands that have a
// cursor, in the order offered, without the CURSOR_NOT_OFFERED_BY_DEFAULT ones and with "Move"
// held back to the end (0x78ef40, the list at +0x408). Each frame the first of them whose cursor
// shows is the cursor and the right-click (0x655aa3), Tab steps to the next (0x655bf0). A command's
// cursor shows when one of the selected objects offering it passes its cursor conditions with the
// hovered object as target (0x652f10, 0x4ffd60).
std::vector<std::pair<WndCursor*, Command*>> FindCursorCommands(Client* client, const std::vector<ClientGameObject*>& selection, ClientGameObject* target)
{
	const GameSet* gs = client->gameSet.get();
	if (!gs || selection.empty())
		return {};
	// the commands each selected object offers (a building: also those its units are sent off with)
	std::vector<std::vector<Command*>> offered(selection.size());
	std::vector<Command*> list;
	Command* move = nullptr;
	if (const int m = gs->commands.names.getIndex("Move"); m >= 0)
		move = const_cast<Command*>(&gs->commands[m]);
	// the commands some character type offers
	static const GameSet* characterCommandsOf = nullptr;
	static std::unordered_set<const Command*> characterCommands;
	if (characterCommandsOf != gs) {
		characterCommandsOf = gs;
		characterCommands.clear();
		const auto& characters = gs->objBlueprints[Tags::GAMEOBJCLASS_CHARACTER];
		for (size_t i = 0; i < characters.size(); i++)
			characterCommands.insert(characters[i].offeredCommands.begin(), characters[i].offeredCommands.end());
	}
	for (size_t s = 0; s < selection.size(); s++) {
		const ClientGameObject* sel = selection[s];
		std::vector<Command*>& cmds = offered[s];
		for (Command* scmd : sel->blueprint->offeredCommands) {
			// a formation offers a command only if one of its members does (Formations.cpp, as its
			// buttons), except its own ones that no character offers ("Formation Army Attack")
			if (sel->blueprint->bpClass == Tags::GAMEOBJCLASS_FORMATION && !scmd->alwaysOfferedByFormation
				&& characterCommands.count(scmd)) {
				bool byMember = false;
				for (const auto& [type, members] : sel->children)
					if (!members.empty()) {
						const auto& memberCommands = members[0]->blueprint->offeredCommands;
						byMember = byMember || std::find(memberCommands.begin(), memberCommands.end(), scmd) != memberCommands.end();
					}
				if (!byMember)
					continue;
			}
			cmds.push_back(scmd);
			// commands for auto ordering spawned characters from building
			if (sel->blueprint->bpClass == Tags::GAMEOBJCLASS_BUILDING) {
				const std::string_view name = gs->commands.getString(scmd);
				if (name.substr(0, 6) == "Spawn ") {
					const int x = gs->objBlueprints[Tags::GAMEOBJCLASS_CHARACTER].names.getIndex(std::string(name.substr(6)));
					if (x >= 0)
						for (Command* cmd : gs->objBlueprints[Tags::GAMEOBJCLASS_CHARACTER][x].offeredCommands)
							if (cmd->canBeAssignedToSpawnedUnits)
								cmds.push_back(cmd);
				}
			}
		}
		for (Command* cmd : cmds)
			if ((cmd->cursor || !cmd->cursorAvailable.empty()) && !cmd->cursorNotOfferedByDefault && cmd != move
				&& std::find(list.begin(), list.end(), cmd) == list.end())
				list.push_back(cmd);
	}
	if (move && std::any_of(offered.begin(), offered.end(), [&](const auto& cmds) { return std::find(cmds.begin(), cmds.end(), move) != cmds.end(); }))
		list.push_back(move);

	std::vector<std::pair<WndCursor*, Command*>> found;
	for (Command* cmd : list) {
		// nothing pointed at: only the commands with a button (or the over-landscape ones)
		if (!target && !cmd->hasButton() && !cmd->evaluateConditionsOverLandscape)
			continue;
		for (size_t s = 0; s < selection.size(); s++) {
			if (std::find(offered[s].begin(), offered[s].end(), cmd) == offered[s].end())
				continue;
			CliScriptContext ctx(client, selection[s]);
			auto _ = ctx.change(ctx.target, target);
			auto _sel = ctx.change(ctx.selectedObject, selection[s]);
			// the unit's cursor test (0x4ffd60): with CURSOR_AVAILABLE, the first of those whose
			// condition holds (none: no cursor); otherwise CURSOR, if all CURSOR_CONDITIONs hold
			WndCursor* cmdCursor = nullptr;
			if (!cmd->cursorAvailable.empty()) {
				for (auto& avcond : cmd->cursorAvailable) {
					if (avcond.first->test->eval(&ctx)) {
						cmdCursor = avcond.second;
						break;
					}
				}
			}
			else if (std::all_of(cmd->cursorConditions.begin(), cmd->cursorConditions.end(), [&](int eq) {return gs->equations[eq]->eval(&ctx) > 0.0f; }))
				cmdCursor = cmd->cursor;
			if (!cmdCursor)
				continue;
			found.emplace_back(cmdCursor, cmd);
			break;
		}
	}
	return found;
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
				// a group dragged: each unit faces the same way from its own spot (not all at one point)
				const Vector3 unitFaceTo = faceTo.x >= 0.0f ? destPos + (faceTo - peapos) : faceTo;
				client->sendCommand(sel, rightClickCommand, assignmentMode, nextSelectedObject, destPos, unitFaceTo);
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
	// with the depth pre-pass: projected onto the ground and the water (whole on slopes and the
	// tessellated relief, over the grass; strips through the ground cut them)
	auto* dgfx = dynamic_cast<DiligentGfx*>(gfx);
	if (dgfx && dgfx->ProjectedDecalsAvailable()) {
		const Terrain* terrain = client->terrain;
		for (const RangeBand& band : rangeBands) {
			const float width = std::min(BAND_WIDTH, band.radius);
			DiligentGfx::ProjectedDecal d;
			d.centre[0] = band.centre.x;
			d.centre[1] = band.centre.z;
			d.outer = band.radius;
			d.inner = std::max(0.0f, band.radius - width);
			d.tiles = (float)std::max(4, (int)std::round(2.0f * 3.14159265f * d.outer / (width * TILE_LENGTH)));
			// the box's height: the ground (and water) around the ring, with room
			float lo = 1e9f, hi = -1e9f;
			for (int i = 0; i <= 32; i++) {
				const float a = i * 2.0f * 3.14159265f / 32;
				const float r = i == 32 ? 0.0f : d.outer;
				const float y = terrain->getHeightEx(band.centre.x + std::cos(a) * r, band.centre.z + std::sin(a) * r, true);
				lo = std::min(lo, y);
				hi = std::max(hi, y);
			}
			d.yMin = lo - 3.0f;
			d.yMax = hi + 3.0f;
			dgfx->DrawProjectedDecals((texture)band.tex, &d, 1);
		}
		return;
	}
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

// The footprint squares of the building being placed, as WKB.exe draws them: one per tile, a
// little smaller than the tile so that they read as a grid, translucent red or green, lying on
// the ground (split in 2 x 2 so that they follow the smoothed terrain). An untextured look: a
// white texel tinted by the vertex colour, with the range bands' state.
void ClientInterface::drawFootprintTiles()
{
	if (footprintTiles.empty() || !client->terrain)
		return;
	constexpr int SPLIT = 2;
	constexpr float TILE = 5.0f, INSET = 0.3f;
	static std::optional<texture> white;
	if (!white) {
		Bitmap bmp;
		bmp.width = bmp.height = 1;
		bmp.format = BMFORMAT_R8G8B8A8;
		bmp.pixels.resize(4);
		for (int i = 0; i < 4; i++)
			bmp.pixels[i] = 255;
		white = gfx->CreateTexture(bmp, 1);
	}
	// with the depth pre-pass: projected onto the ground (whole on slopes, over the grass)
	auto* dgfx = dynamic_cast<DiligentGfx*>(gfx);
	if (dgfx && dgfx->ProjectedDecalsAvailable()) {
		std::vector<DiligentGfx::ProjectedDecal> squares;
		squares.reserve(footprintTiles.size());
		for (const FootprintTile& tile : footprintTiles) {
			DiligentGfx::ProjectedDecal d;
			d.centre[0] = tile.x * TILE + TILE * 0.5f;
			d.centre[1] = tile.z * TILE + TILE * 0.5f;
			d.outer = TILE * 0.5f - INSET;
			d.square = true;
			d.colour = tile.ok ? 0x8000E040 : 0x90F02020;
			float lo = 1e9f, hi = -1e9f;
			for (int i = 0; i < 9; i++) {
				const float y = client->terrain->getHeightEx(tile.x * TILE + (i % 3) * TILE * 0.5f, tile.z * TILE + (i / 3) * TILE * 0.5f, true);
				lo = std::min(lo, y);
				hi = std::max(hi, y);
			}
			d.yMin = lo - 3.0f;
			d.yMax = hi + 3.0f;
			squares.push_back(d);
		}
		dgfx->DrawProjectedDecals(*white, squares.data(), squares.size());
		return;
	}
	if (!footprintBatch)
		footprintBatch = gfx->CreateBatch(256 * SPLIT * SPLIT * 4, 256 * SPLIT * SPLIT * 6); // (a Manor's ring and footprint)
	gfx->BeginParticles();
	gfx->BeginBatchDrawing();
	gfx->SetBlendColor(-1);
	gfx->SetTexture(0, *white);
	const Terrain* terrain = client->terrain;
	// over the grass's blades (0.4 to 0.85 m at height 1), else they cover the squares in patches
	const float lift = 0.15f + (g_renderSettings.grass && UseEnhancedRenderers(gfx) ? 0.9f * std::max(0.0f, g_renderSettings.grassHeight) : 0.0f);
	footprintBatch->begin();
	for (const FootprintTile& tile : footprintTiles) {
		const uint32_t color = tile.ok ? 0x8000E040 : 0x90F02020;
		const float x0 = tile.x * TILE + INSET, z0 = tile.z * TILE + INSET, step = (TILE - 2 * INSET) / SPLIT;
		for (int sz = 0; sz < SPLIT; sz++)
			for (int sx = 0; sx < SPLIT; sx++) {
				batchVertex* v;
				uint16_t* ix;
				uint32_t first;
				footprintBatch->next(4, 6, &v, &ix, &first);
				const float corners[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
				for (int i = 0; i < 4; i++) {
					v[i].x = x0 + (sx + corners[i][0]) * step;
					v[i].z = z0 + (sz + corners[i][1]) * step;
					v[i].y = terrain->getHeightEx(v[i].x, v[i].z, true) + lift;
					v[i].u = v[i].v = 0.5f;
					v[i].color = color;
				}
				for (const int& c : { 0, 2, 1, 0, 3, 2 })
					*(ix++) = (uint16_t)(first + c);
			}
	}
	footprintBatch->flush();
	footprintBatch->end();
}

// The refusal while placing a building, as WKB.exe shows it: "Cannot build here : ..." in its
// in-game font (Fonts\Drop_Shadow_White) on a dark red box with a lighter red edge, under the
// cursor (the look from the user's screenshots of the original at 1920 x 1080)
Vector3 ClientInterface::stampdownOrientation() const
{
	return StampdownPlan::quarterTurn(Vector3(0.0f, stampdownQuarter * 1.5707964f, 0.0f));
}

void ClientInterface::drawStampdownRefusal()
{
	if (stampdownRefusal.empty())
		return;
	static std::unique_ptr<BitmapFont> font;
	static bool tried = false;
	if (!tried) {
		tried = true;
		const char* dir = "Fonts\\Drop_Shadow_White";
		std::unique_ptr<std::vector<std::string>> files(ListFiles(dir));
		if (files)
			for (const std::string& f : *files)
				if (!font && f.size() > 5 && StrCICompare(std::string_view(f).substr(f.size() - 5), ".font") == 0) {
					auto candidate = std::make_unique<BitmapFont>();
					if (candidate->load(gfx, std::string(dir) + "\\" + f))
						font = std::move(candidate);
				}
	}
	ImDrawList* dl = ImGui::GetForegroundDrawList();
	const float s = g_uiScale;
	const float textW = font ? font->measure(stampdownRefusal) * s : ImGui::CalcTextSize(stampdownRefusal.c_str()).x;
	const float textH = font ? font->height * s : ImGui::GetFontSize();
	const float padX = 3.0f * s, padY = 4.0f * s;
	float x = g_mouseX + 4.0f * s, y = g_mouseY + 30.0f * s;
	const float w = textW + 2 * padX, h = textH + 2 * padY;
	x = std::clamp(x, 0.0f, std::max(0.0f, g_windowWidth - w));
	y = std::clamp(y, 0.0f, std::max(0.0f, g_windowHeight - h));
	dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), IM_COL32(140, 8, 8, 255));
	dl->AddRect(ImVec2(x, y), ImVec2(x + w, y + h), IM_COL32(228, 96, 96, 255), 0.0f, 0, std::max(1.0f, std::round(s)));
	if (font)
		font->draw(dl, std::round(x + padX), std::round(y + padY), stampdownRefusal, 0xFFFFFFFF, s);
	else
		dl->AddText(ImVec2(x + padX, y + padY), IM_COL32(255, 255, 255, 255), stampdownRefusal.c_str());
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
	}
	if (ImGui::CollapsingHeader("Picture")) {
		ImGui::SliderFloat("Exposure", &s.exposure, 0.25f, 4.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
		ImGui::SliderFloat("Brightness", &s.brightness, -0.5f, 0.5f, "%.2f");
		ImGui::SliderFloat("Contrast", &s.contrast, 0.25f, 2.0f, "%.2f");
		ImGui::SliderFloat("Saturation", &s.saturation, 0.0f, 2.0f, "%.2f");
		ImGui::TextDisabled("Applies to the 3D scene, not the HUD");
	}
	// (grouped as the Options menu's graphics pages, MenuOptions.cpp)
	if (ImGui::CollapsingHeader("Sky and clouds")) {
		ImGui::Checkbox("Sky", &s.sky);
		{
			static const char* const cloudLevels[] = { "Original (scrolling map)", "Moving cloud shadows", "+ light shafts", "+ volumetric clouds in the sky" };
			ImGui::Combo("Clouds", &s.dynamicClouds, cloudLevels, 4);
			static const char* const skyModels[] = { "Normal", "Physical (earth's shadow, ozone, sunset haze)" };
			ImGui::BeginDisabled(!s.sky || s.dynamicClouds < 3);
			ImGui::Combo("Sky model (volumetric clouds' sky)", &s.skyModel, skyModels, 2);
			ImGui::EndDisabled();
			static const char* const cloudQualities[] = { "Low (quarter size)", "Medium (half size)", "High (full size)", "Ultra (full size, finer)" };
			ImGui::Combo("Volumetric cloud quality", &s.cloudQuality, cloudQualities, 4);
			static const char* const cloudDetails[] = { "Low (one density)", "Medium (varying density)", "High (+ billows, cumulonimbus)", "Very high (+ self-shadowed billows)", "Ultra (+ more steps, finer)" };
			ImGui::Combo("Cloud detail", &s.cloudDetail, cloudDetails, 5);
		}
		ImGui::Checkbox("Cloud shadows", &s.cloudShadows);
		ImGui::SliderFloat("Light shafts", &s.lightShaftStrength, 0.0f, 4.0f, "%.2f");
		{
			static const char* const cloudWeathers[] = { "By the cloud cover", "Clear", "Cloudy", "Rain", "Heavy rain", "Thunderstorm", "Fog", "Snow", "Blizzard", "Heatwave", "Drought" };
			// (outside a game only: in one, its settings set the clouds)
			ImGui::BeginDisabled(g_environment.active || g_environment.skyWeather >= 0);
			ImGui::Combo("Cloud weather", &s.cloudWeather, cloudWeathers, 11);
			ImGui::SliderFloat("Cloud cover", &s.cloudCover, 0.0f, 1.0f, "%.2f");
			ImGui::EndDisabled();
		}
		{
			static const char* const weatherLevels[] = { "Low", "Medium (snow on roofs and models)", "High (footprints)", "Ultra" };
			ImGui::Combo("Weather and season effects", &s.weatherEffects, weatherLevels, 4);
		}
	}
	if (ImGui::CollapsingHeader("Lights and shadows")) {
		ImGui::SeparatorText("Shadows");
		ImGui::Checkbox("Static shadows (buildings, trees)", &s.staticShadows);
		ImGui::BeginDisabled(!s.staticShadows);
		ImGui::Checkbox("Ray-traced shadows (buildings, trees, units; needs DXR 1.1)", &s.rtShadows);
		ImGui::SliderFloat("Light in static shadows", &s.staticShadowLight, 0.0f, 1.0f, "%.3f");
		{
			static const char* const levels[] = { "Original (4 x 4 per tile)", "High (8 x 8)", "Very high (16 x 16)" };
			int level = s.shadowResolution == 4 ? 2 : s.shadowResolution == 2 ? 1 : 0;
			if (ImGui::Combo("Static shadow detail", &level, levels, 3))
				s.shadowResolution = level == 2 ? 4 : level == 1 ? 2 : 1;
		}
		ImGui::Checkbox("Wall shadows", &s.wallShadows);
		ImGui::EndDisabled();
		ImGui::Checkbox("Character shadows", &s.characterShadows);
		ImGui::Checkbox("Real character shadows (shadow map)", &s.realCharacterShadows);
		ImGui::Checkbox("Contact shadows (where feet, fences and walls meet the ground)", &s.contactShadows);
		ImGui::SeparatorText("Light");
		ImGui::Checkbox("Light from the sky (sky occlusion, bounce)", &s.skyGI);
		{
			static const char* const giLevels[] = { "Off", "Low (buildings, one bounce)", "High (units too, two bounces)", "Ultra (High, light from the glowing texels)" };
			ImGui::BeginDisabled(!s.skyGI);
			ImGui::Combo("Bounced light (dynamic GI)", &s.dynamicGI, giLevels, 4);
			ImGui::SliderFloat("Bounced light strength", &s.dynamicGIStrength, 0.0f, 3.0f, "%.2f");
			ImGui::BeginDisabled(s.dynamicGI < 3);
			ImGui::SliderFloat("Window light strength (Ultra)", &s.emissiveLightStrength, 0.0f, 3.0f, "%.2f");
			ImGui::EndDisabled();
			ImGui::EndDisabled();
		}
		ImGui::Checkbox("Fires light their surroundings", &s.fireLights);
		ImGui::Checkbox("Ambient occlusion", &s.ssao);
		ImGui::BeginDisabled(!s.ssao);
		ImGui::SliderFloat("AO radius", &s.ssaoRadius, 0.5f, 10.0f, "%.1f");
		ImGui::SliderFloat("AO strength", &s.ssaoStrength, 0.0f, 1.0f, "%.2f");
		ImGui::EndDisabled();
	}
	if (ImGui::CollapsingHeader("Water")) {
		{
			static const char* const waterLevels[RenderSettings::WATER_LEVELS] = { "Original (scrolling texture)", "Off (plain colour)",
				"Very low (bump-mapped ripples)", "Low", "Medium", "High (tessellated waves)", "Very high", "Ultra" };
			ImGui::Combo("Water", &s.waterQuality, waterLevels, RenderSettings::WATER_LEVELS);
		}
		ImGui::Checkbox("Rivers flow downstream", &s.waterFlow);
		ImGui::Checkbox("Ray-traced water reflections (needs DXR 1.1, water Low or better)", &s.rtReflections);
	}
	if (ImGui::CollapsingHeader("Terrain and grass")) {
		{
			static const char* const tessLevels[] = { "Off", "Low (4 per tile edge)", "Medium (8)", "High (16)" };
			ImGui::Combo("Terrain tessellation", &s.terrainTessellation, tessLevels, 4);
		}
		ImGui::Checkbox("Terrain bump mapping", &s.bumpMapping);
		ImGui::Checkbox("Terrain lit as the original (light kept at least 1/3 up)", &s.terrainOriginalLight);
		ImGui::SliderFloat("Ground Fresnel (sky sheen at grazing angles; 0 off)", &s.terrainFresnel, 0.0f, 1.0f);
		ImGui::Checkbox("Grass", &s.grass);
		ImGui::BeginDisabled(!s.grass);
		ImGui::SliderFloat("Grass density (blades per m²)", &s.grassDensity, 0.0f, 24.0f, "%.1f");
		ImGui::SliderFloat("Grass height", &s.grassHeight, 0.2f, 3.0f, "%.2f");
		ImGui::SliderFloat("Grass distance", &s.grassDistance, 10.0f, 250.0f, "%.0f");
		ImGui::EndDisabled();
	}
	if (ImGui::CollapsingHeader("Models")) {
		ImGui::Checkbox("Models lit per pixel, with highlights", &s.modelLighting);
		ImGui::BeginDisabled(!s.modelLighting);
		ImGui::SliderFloat("Model highlight strength", &s.modelSpecular, 0.0f, 3.0f, "%.2f");
		ImGui::SliderFloat("Model detail from textures", &s.modelDetailNormals, 0.0f, 8.0f, "%.1f");
		ImGui::Checkbox("Sky light on models", &s.skyLight);
		ImGui::EndDisabled();
		ImGui::SliderFloat("Model ambient", &s.meshAmbient, 0.0f, 1.0f, "%.3f");
		ImGui::SliderFloat("Model sun factor", &s.meshDiffuse, 0.0f, 1.5f, "%.3f");
	}
	if (ImGui::CollapsingHeader("Anti-aliasing and post effects")) {
		{
			static const char* const aaLevels[] = { "Off", "FXAA", "TAA", "FSR 3.1" };
			ImGui::Combo("Anti-aliasing", &s.antiAliasing, aaLevels, 4);
		}
		ImGui::SliderInt("Anisotropic filtering (next map)", &s.anisotropy, 1, 16);
		ImGui::Checkbox("Bloom", &s.bloom);
		ImGui::BeginDisabled(!s.bloom);
		ImGui::SliderFloat("Bloom threshold", &s.bloomThreshold, 0.3f, 1.0f, "%.2f");
		ImGui::SliderFloat("Bloom intensity", &s.bloomIntensity, 0.0f, 1.5f, "%.2f");
		ImGui::EndDisabled();
		ImGui::SliderFloat("Camera motion blur", &s.motionBlur, 0.0f, 1.0f, "%.2f");
		ImGui::SliderFloat("Tilt-shift", &s.tiltShift, 0.0f, 1.0f, "%.2f");
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
// The ground's height where the terrain is drawn: the tessellated terrain's smooth surface, or
// the game's own heights
static float VisualGroundHeight(const Terrain* terrain, float x, float z)
{
	return g_terrainTessellationActive ? terrain->getSmoothHeight(x, z) : terrain->getHeight(x, z);
}

static Matrix GetShearToGroundMatrix(const Terrain* terrain, Model* model, const Matrix& world, const Vector3& position)
{
	const auto [low, high] = model->interpolateBoundingBox(0);
	const Vector3 centre = (low + high) * 0.5f;
	struct AxisSlope { Vector3 dir; float slope, length, midHeight; };
	auto axisSlope = [&](const Vector3& a, const Vector3& b) {
		const Vector3 wa = a.transform(world), wb = b.transform(world);
		Vector3 d(wb.x - wa.x, 0.0f, wb.z - wa.z);
		const float len = d.len2xz();
		const float ha = VisualGroundHeight(terrain, wa.x, wa.z), hb = VisualGroundHeight(terrain, wb.x, wb.z);
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
	const bool newLevel = prewarmedLevel != client->getLevel();
	if (newLevel)
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
	// When the game starts, besides: every building, character and missile the game set has, all
	// their appearances and animations (a building placed, a unit trained, an arrow shot for the
	// first time loaded its mesh and textures then, a stall in the game). Files missing (game-set
	// entries no map uses) are left out: loading them stops the game.
	const bool starting = newLevel && client->gameSet && g_settings.value<bool>("prewarmAllBlueprints", true);
	size_t fromLevel = models.size();
	if (starting) {
		auto meshExists = [](const std::string& file) {
			if (FileExists(file.c_str()))
				return true;
			const std::string base = file.substr(0, file.find_last_of('.'));
			return FileExists((base + ".glb").c_str()) || FileExists((base + ".gltf").c_str());
		};
		auto loadable = [&](Model* m) {
			if (auto* anim = dynamic_cast<AnimatedModel*>(m)) {
				if (!anim->ready && !FileExists(anim->fileName.c_str()))
					return false;
				StaticModel* mesh = anim->getStaticModel();
				return mesh && (mesh->ready || mesh->generator || meshExists(mesh->fileName));
			}
			auto* mesh = (StaticModel*)m;
			return mesh->ready || mesh->generator || meshExists(mesh->fileName);
		};
		// ("prewarmClasses": which, a bit per GAMEOBJCLASS_; all three about 5 s on 12 cores and 0.7 GB of
		// the GPU's memory with the game set's 2500 models, the buildings alone 2 s)
		static const int classMask = g_settings.value<int>("prewarmClasses",
			(1 << Tags::GAMEOBJCLASS_BUILDING) | (1 << Tags::GAMEOBJCLASS_CHARACTER) | (1 << Tags::GAMEOBJCLASS_MISSILE));
		for (int cls : { Tags::GAMEOBJCLASS_BUILDING, Tags::GAMEOBJCLASS_CHARACTER, Tags::GAMEOBJCLASS_MISSILE })
			if (classMask & (1 << cls))
			for (size_t b = 0; b < client->gameSet->objBlueprints[cls].size(); b++) {
				const GameObjBlueprint& bp = client->gameSet->objBlueprints[cls][b];
				for (const auto& [subId, subtype] : bp.subtypes)
					for (const auto& [appId, appear] : subtype.appearances)
						for (const auto& [index, variants] : appear.animations)
							for (Model* m : variants)
								if (m && prewarmedModels.insert(m).second && loadable(m))
									models.push_back(m);
			}
	}
	// the files and textures here, then the GPU's buffers (the animations baked on all cores)
	const uint64_t t0 = SDL_GetTicksNS();
	// (the loading screen: a step each, counted per model)
	if (newLevel)
		LoadingEnterStep(LOADING_MODELS);
	for (size_t i = 0; i < models.size(); i++) {
		models[i]->prepare();
		if (StaticModel* sm = models[i]->getStaticModel())
			sm->prepare();
		if (newLevel)
			LoadingCount(i + 1, models.size());
	}
	const uint64_t t1 = SDL_GetTicksNS();
	if (newLevel)
		LoadingEnterStep(LOADING_TEXTURES);
	// (the level's own models' textures on the GPU at once when the game starts, the game set's
	// others into RAM, to the GPU when drawn or from the renderer's background queue: the GPU
	// memory budget; the objects that appear later (a building's new state) the same as those:
	// uploaded at once there, they stalled the frame for up to a second and a half, the HD play test)
	for (size_t i = 0; i < models.size(); i++) {
		scene->prewarm(models[i], newLevel && i < fromLevel);
		if (newLevel)
			LoadingCount(i + 1, models.size());
	}
	const uint64_t t2 = SDL_GetTicksNS();
	if (newLevel)
		LoadingEnterStep(LOADING_GPU_BUFFERS);
	sceneRenderer->prewarm(models);
	const uint64_t t3 = SDL_GetTicksNS();
	const uint64_t tFiles = t1 - t0, tTex = t2 - t1, tGpu = t3 - t2;
	if (!models.empty())
		printf("Prewarmed %zu models (%zu of the game set's besides the level's) in %llu ms (files %llu, textures %llu, GPU %llu)\n", models.size(),
			models.size() - fromLevel, (unsigned long long)(SDL_GetTicks() - start),
			(unsigned long long)(tFiles / 1000000), (unsigned long long)(tTex / 1000000), (unsigned long long)(tGpu / 1000000));
}

bool ClientInterface::showsEveryone() const
{
	return replayMode || client->spectator || client->ownOutcome() != 0;
}

bool ClientInterface::fogOfWarOn() const
{
	// not in the map editor, nor for an observer without a player
	// only the game option decides it: the lobby's Clear Fog Of War on, none (Client::clearFogOfWar)
	// and once the player's game is over (won or lost, still in the game: it watches, everything
	// shown; Client::finishedPlayers)
	return !client->clearFogOfWar && !editorOn && !replayMode && !client->spectator && client->clientPlayer && client->getLevel()
		&& !client->ownOutcome();
}

// The loading screen, measured on the original (1920 x 1080, from the screen's centre): the
// player's row 138 above it, name at -227 (in the player's colour), "stage/8" at +73, the bar from +130 to +225 (14 high,
// dark red, filling bright red); "Words of Wisdom" 38 below, over a dark box from -260 to +260,
// 50 to 230 below, with one of the FRONT_END_TIP_n texts. wkbai's bar is LoadingProgress's: the
// percentage where the original has the stage, and under the rows what is being done (the step,
// the shader or file, a count such as "Loading the models  812 / 2549").
void ClientInterface::drawLoadingScreen(bool countFrame)
{
	// (the map editor shows none: no loading frames for it either)
	if (editorOn)
		LoadingAbort();
	if (loadingDone || editorOn)
		return;
	if (loadingScreenIndex == 0) {
		std::random_device rd;
		loadingScreenIndex = (int)(rd() % 9) + 1;
		// a tip that the language file has (it numbers them 1-45 with gaps)
		for (int tries = 0; tries < 50 && !loadingTip; tries++) {
			const int n = (int)(rd() % 45) + 1;
			const std::string key = "FRONT_END_TIP_" + std::to_string(n);
			if (lang.getText(key) != key)
				loadingTip = n;
		}
	}
	// (the local server's state not read while its thread loads the game: the loading frames
	// are drawn meanwhile)
	const int loadingStep = LoadingGet().step;
	const bool serverLoading = client->localServer && loadingStep >= 0 && loadingStep < LOADING_TERRAIN;
	const Server* srv = client->localServer && !serverLoading ? Server::instance : nullptr;
	int stage = 1;
	if (serverLoading) stage = loadingStep == LOADING_GAME_SET ? 1 : 2;
	else if (srv && !srv->gameSet) stage = 1;
	else if (srv && !srv->terrain) stage = 2;
	else if (srv && !srv->getLevel()) stage = 3;
	else if (!client->gameSet) stage = 4;
	else if (!client->terrain) stage = 5;
	else if (!client->getLevel()) stage = 6;
	else if (framesWithLevel < 10) stage = 7;
	else stage = 8;
	// the game frames with the level, the last step (LoadingProgress's "Starting the game")
	// Then, in a multiplayer game, the loading screen stays until every player has loaded (the
	// host's clock stands until then: Server::waitForLoads), this one at 100%
	static constexpr int FIRST_FRAMES = 30;
	const bool ownLoadDone = framesWithLevel >= FIRST_FRAMES;
	if (countFrame && client->getLevel() && client->terrain && !ownLoadDone) {
		if (++framesWithLevel >= FIRST_FRAMES) {
			LoadingFinish();
			client->sendLoadingStage(100);
			loadingPercentSent = 100;
		}
		else {
			LoadingEnterStep(LOADING_FIRST_FRAMES);
			if (framesWithLevel == 1)
				LoadingDetail("first frames");
			LoadingCount(framesWithLevel, FIRST_FRAMES);
		}
	}
	const bool waitingForOthers = framesWithLevel >= FIRST_FRAMES && client->loadingRows.size() > 1 && client->loadingWaitForOthers;
	if (framesWithLevel >= FIRST_FRAMES && !waitingForOthers) {
		if (loadingWaitStarted)
			printf("Loading: waited %llu ms for the other players\n", (unsigned long long)(SDL_GetTicks() - loadingWaitStarted));
		loadingDone = true;
		return;
	}
	if (waitingForOthers && !loadingWaitStarted) {
		loadingWaitStarted = SDL_GetTicks();
		printf("Loading: loaded, waiting for the other players\n");
	}
	// (the original's eight stages, for the test screenshot's moment)
	loadingStage = stage;
	// how far it is (LoadingProgress: what the load does and the last load's times), to the
	// other players as a percentage (100 once loaded)
	LoadingView view = LoadingGet();
	if (waitingForOthers)
		view.label = "Waiting for the other players";
	const float progress = waitingForOthers ? 1.0f : view.step >= 0 ? view.progress : std::clamp((stage - 1) / 8.0f, 0.0f, 1.0f);
	const int percent = waitingForOthers ? 100 : std::clamp((int)(progress * 100.0f), 0, 99);
	if (percent != loadingPercentSent) {
		loadingPercentSent = percent;
		client->sendLoadingStage(percent);
	}

	ImDrawList* dl = ImGui::GetForegroundDrawList();
	const ImVec2 size = ImGui::GetIO().DisplaySize;
	const std::string bg = "Interface\\Loading Screen " + std::to_string(loadingScreenIndex) + ".TGA";
	if (texture tex = uiTexCache.getTexture(bg.c_str(), false))
		dl->AddImage(tex, ImVec2(0, 0), size);
	else
		dl->AddRectFilled(ImVec2(0, 0), size, IM_COL32(0, 0, 0, 255));
	const float cx = size.x * 0.5f, cy = size.y * 0.5f;
	auto outlined = [dl](ImVec2 p, ImU32 colour, const std::string& text) {
		for (const ImVec2 d : { ImVec2(-1, 0), ImVec2(1, 0), ImVec2(0, -1), ImVec2(0, 1) })
			dl->AddText(ImVec2(p.x + d.x, p.y + d.y), IM_COL32(0, 0, 0, 220), text.c_str());
		dl->AddText(p, colour, text.c_str());
	};
	// the player's row, the name in the player's colour: the client's player once its level is in,
	// before that the local server's player for client 0
	const CommonGameObject* player = client->clientPlayer.get();
	if (!player && srv && srv->getLevel() && !srv->clientPlayerObjects.empty())
		player = srv->clientPlayerObjects[0].get();
	const std::string name = g_settings.value("playerName", lang.getText("DEFAULT_PLAYER_PROFILE"));
	const int colour = player ? player->color : 1;
	// (wkbai: the percentage where the original has "stage/8")
	auto row = [&](float y, int rowColour, const std::string& rowName, int rowPercent, float rowProgress) {
		outlined(ImVec2(cx - 227, y), playerColors[(rowColour >= 0 && rowColour < 9) ? rowColour : 1], rowName);
		outlined(ImVec2(cx + 73, y), IM_COL32(232, 214, 176, 255), std::to_string(rowPercent) + "%");
		dl->AddRectFilled(ImVec2(cx + 130, y + 2), ImVec2(cx + 225, y + 16), IM_COL32(96, 0, 0, 255));
		dl->AddRectFilled(ImVec2(cx + 130, y + 2), ImVec2(cx + 130 + 95 * rowProgress, y + 16), IM_COL32(210, 0, 0, 255));
	};
	// a multiplayer game: a row per human player, as the server reports them, 22 px apart: this
	// client's on top (its own progress), then the others' as they reported it; else this
	// player's alone
	// (more than six rows start higher, above the tip)
	const int rowCount = std::max<int>(1, (int)client->loadingRows.size());
	float y = cy - 140 - 22.0f * std::max(0, rowCount - 6);
	int ownColour = colour;
	if (!player)
		for (const Client::LoadingRow& r : client->loadingRows)
			if (r.own)
				ownColour = r.colour; // (as the server knows it, before the client's player is in)
	row(y, ownColour, name, percent, progress);
	y += 22;
	if (client->loadingRows.size() > 1)
		for (const Client::LoadingRow& r : client->loadingRows)
			if (!r.own) {
				row(y, r.colour, r.name, std::clamp(r.stage, 0, 100), std::clamp(r.stage / 100.0f, 0.0f, 1.0f));
				y += 22;
			}
	// what this computer is doing (wkbai's): the step, its detail or count, under the rows
	if (!view.label.empty()) {
		dl->PushClipRect(ImVec2(cx - 230, y - 2), ImVec2(cx + 228, y + 20), true);
		outlined(ImVec2(cx - 227, y), IM_COL32(232, 214, 176, 255), view.label);
		dl->PopClipRect();
	}
	// the tip
	if (loadingTip) {
		const std::string title = lang.getText("FRONT_END_TITLE_TIP_OF_THE_DAY");
		outlined(ImVec2(cx - ImGui::CalcTextSize(title.c_str()).x * 0.5f, cy + 30), IM_COL32(232, 214, 176, 255), title);
		dl->AddRectFilled(ImVec2(cx - 260, cy + 50), ImVec2(cx + 260, cy + 230), IM_COL32(0, 0, 0, 110));
		const std::string tip = lang.getText("FRONT_END_TIP_" + std::to_string(loadingTip));
		dl->AddText(nullptr, 0.0f, ImVec2(cx - 254, cy + 56), IM_COL32(235, 235, 235, 255), tip.c_str(), nullptr, 508.0f);
	}
}

// A frame with the loading screen alone, between the game's frames: LoadingProgress's pump (the
// terrain renderer, the shaders and the models made on the main thread) and the frames while the
// local server loads (QuickStartMenu)
void ClientInterface::loadingFrame()
{
	ZoneScopedN("Loading frame");
	if (!languageLoaded) {
		lang.load("Languages\\Language.txt");
		languageLoaded = true;
	}
	// the other players' progress as it came, though this client reads no messages meanwhile
	client->peekLoadingRows();
	ImGuiImpl_NewFrame();
	// (as a frame without a level: no post-processing)
	if (auto* postGfx = dynamic_cast<DiligentGfx*>(gfx))
		postGfx->BeginPostProcessing(false, UseEnhancedRenderers(gfx));
	gfx->ClearFrame(true, true, 0);
	gfx->BeginDrawing();
	drawLoadingScreen(false);
	gfx->InitImGuiDrawing();
	ImGuiImpl_Render(gfx);
	loadingScreenshots();
	gfx->EndDrawing();
	HandleWindow();
}

// Test runs: "benchmark": { ..., "loadingScreenshot": "<path>.bmp", "loadingScreenshots": [percent, ...] }
// saves <path>_<percent>.bmp once the bar is past each of those percentages
void ClientInterface::loadingScreenshots()
{
	if (!BenchmarkEnabled())
		return;
	const nlohmann::json& bench = g_settings["benchmark"];
	if (!bench.is_object() || !bench.contains("loadingScreenshots") || !bench["loadingScreenshots"].is_array())
		return;
	const nlohmann::json& list = bench["loadingScreenshots"];
	const std::string base = bench.value("loadingScreenshot", std::string());
	// (and <path>_wait.bmp a second into the wait for the other players)
	static bool waitShot = false;
	if (!base.empty() && !waitShot && !loadingDone && loadingWaitStarted && SDL_GetTicks() - loadingWaitStarted > 1000) {
		waitShot = true;
		const size_t dot = base.rfind('.');
		const std::string path = dot == std::string::npos ? base + "_wait" : base.substr(0, dot) + "_wait" + base.substr(dot);
		printf("Loading screenshot %s (waiting for the other players): %s\n", path.c_str(), gfx->SaveScreenshot(path.c_str()) ? "saved" : "failed");
	}
	if (base.empty() || loadingShotsTaken >= list.size())
		return;
	const LoadingView view = LoadingGet();
	if (view.step < 0 || view.progress * 100.0f < list[loadingShotsTaken].get<float>())
		return;
	const int at = list[loadingShotsTaken].get<int>();
	loadingShotsTaken++;
	const size_t dot = base.rfind('.');
	const std::string path = dot == std::string::npos ? base + "_" + std::to_string(at)
		: base.substr(0, dot) + "_" + std::to_string(at) + base.substr(dot);
	printf("Loading screenshot %s at %.1f%%, %.0f ms (%s): %s\n", path.c_str(), view.progress * 100.0f, view.elapsedMs,
		view.label.c_str(), gfx->SaveScreenshot(path.c_str()) ? "saved" : "failed");
	fflush(stdout);
}

// The level's models made before its first frame, with the loading screen moving (LoadingTick
// draws loading frames between the pieces; in the frame it could draw none): the models'
// renderer and its shaders, then the game set's models, their textures and GPU buffers
// (prewarmLevel)
void ClientInterface::prepareLevelBeforeFrame()
{
	static const bool prewarm = g_settings.value<bool>("prewarmModels", true);
	if (!prewarm || loadingDone || editorOn || !client->getLevel() || !client->terrain || !client->gameSet
		|| prewarmedLevel == client->getLevel())
		return;
	ZoneScopedN("Level preparation");
	// Test runs: "benchmark": { ..., "loadingDelayMs": n } a slower computer (the other players
	// wait for this one)
	if (BenchmarkEnabled())
		if (const int delay = g_settings["benchmark"].value("loadingDelayMs", 0); delay > 0) {
			LoadingDetail("test delay");
			const uint64_t until = SDL_GetTicks() + (uint64_t)delay;
			while (SDL_GetTicks() < until) {
				SDL_Delay(10);
				LoadingTick();
			}
		}
	LoadingEnterStep(LOADING_MODEL_SHADERS);
	if (!scene)
		scene = new Scene(gfx, &client->gameSet->modelCache);
	if (!sceneRenderer) {
		if (UseEnhancedRenderers(gfx))
			sceneRenderer = new DiligentEnhancedSceneRenderer(gfx, scene, &client->camera);
		else
			sceneRenderer = new DefaultSceneRenderer(gfx, scene, &client->camera);
	}
	prewarmLevel();
	LoadingEnterStep(LOADING_FIRST_FRAMES);
	// the post-processing's shaders, if the frames will use it (as in iter: the effects or the fog
	// of war on)
	if (auto* dgfx = dynamic_cast<DiligentGfx*>(gfx)) {
		const bool effects = UseEnhancedRenderers(gfx) && (g_renderSettings.ssao || g_renderSettings.bloom
			|| g_renderSettings.antiAliasing > 0 || g_renderSettings.upscaling > 0);
		if (effects || fogOfWarOn()) {
			LoadingDetail("post-processing");
			dgfx->PreparePostProcessing();
		}
	}
	// the first frame next: it sets up the ray tracing's shaders and buffers (in the frame: no
	// loading frames meanwhile)
	LoadingDetail("ray tracing and the first frame");
}

// wkbai's unit history: a unit's age as the panel shows it (the user): with the seasons on,
// 25 + the game years since its birth (a year: four seasons); with only the day and night, the
// days; else m:ss (h:mm:ss past an hour). "" for an untracked unit.
std::string UnitAgeText(Client* client, uint32_t id)
{
	auto it = client->unitInfo.find(id);
	if (it == client->unitInfo.end())
		return std::string();
	const float age = std::max(0.0f, (float)client->timeManager.currentTime - it->second.birth);
	const EnvironmentSettings& env = client->environmentSettings;
	char text[64];
	if (client->environmentActive && env.seasons && env.seasonLengthMin > 0) {
		const int years = (int)(age / (4.0f * env.seasonLengthMin * 60.0f));
		snprintf(text, sizeof(text), "Age %d", 25 + years);
	}
	else if (client->environmentActive && env.dayNight && env.dayLengthMin > 0) {
		const int days = (int)(age / (env.dayLengthMin * 60.0f));
		if (days == 0)
			snprintf(text, sizeof(text), "less than a day");
		else
			snprintf(text, sizeof(text), days == 1 ? "%d day" : "%d days", days);
	}
	else {
		const int s = (int)age;
		if (s >= 3600)
			snprintf(text, sizeof(text), "Age %d:%02d:%02d", s / 3600, (s / 60) % 60, s % 60);
		else
			snprintf(text, sizeof(text), "Age %d:%02d", s / 60, s % 60);
	}
	std::string out = text;
	// a save from before the history: its age counted from the load
	if (it->second.flags & 2)
		out += " (since loading)";
	return out;
}

void ClientInterface::drawUnitHistoryOverlay()
{
	ImDrawList* dl = ImGui::GetBackgroundDrawList();
	auto toScreen = [this](const Vector3& p, bool& inFront) {
		const Vector3 t = p.transformScreenCoords(client->camera.sceneMatrix);
		inFront = t.z > -1.0f && t.z < 1.0f;
		return ImVec2((t.x + 1) * 0.5f * g_windowWidth / g_uiScale, (1 - t.y) * 0.5f * g_windowHeight / g_uiScale);
	};
	// the heroes' names over their heads, in their player's colour, fading out from 120 to 220 m
	if (g_settings.value("showHeroNames", true)) {
		for (const HeroLabel& label : heroLabels) {
			auto it = client->unitInfo.find(label.id);
			ClientGameObject* obj = client->findObject(label.id);
			if (it == client->unitInfo.end() || !obj)
				continue;
			const float dist = (label.top - client->camera.position).len3();
			const float alpha = std::clamp((220.0f - dist) / 100.0f, 0.0f, 1.0f);
			if (alpha <= 0.0f)
				continue;
			bool inFront = false;
			const ImVec2 at = toScreen(label.top + Vector3(0.0f, 0.3f, 0.0f), inFront);
			if (!inFront)
				continue;
			const CommonGameObject* player = obj->getPlayer();
			const int c = player ? player->color : 0;
			// the player's colour lightened a third towards white: dark blue was hard to read on grass
			const ImU32 pc = playerColors[(c >= 0 && c < 9) ? c : 0];
			auto lighten = [](ImU32 v) { return (v + (255 - v) / 3) & 255; };
			const ImU32 colour = lighten(pc & 255) | (lighten((pc >> 8) & 255) << 8) | (lighten((pc >> 16) & 255) << 16) | ((ImU32)(alpha * 255.0f) << 24);
			const std::string& name = it->second.name;
			const ImVec2 size = ImGui::CalcTextSize(name.c_str());
			const ImVec2 p(at.x - size.x * 0.5f, at.y - size.y);
			for (const ImVec2 d : { ImVec2(-1, 0), ImVec2(1, 0), ImVec2(0, -1), ImVec2(0, 1) })
				dl->AddText(ImVec2(p.x + d.x, p.y + d.y), IM_COL32(0, 0, 0, (int)(alpha * 200.0f)), name.c_str());
			dl->AddText(p, colour, name.c_str());
		}
	}
	heroLabels.clear();
	// the unit under the mouse: "Guillaume le Hardi · Knight · 9 kills" (the setting
	// "showUnitNamesOnHover")
	if (g_settings.value("showUnitNamesOnHover", true) && !ImGui::GetIO().WantCaptureMouse) {
		if (ClientGameObject* obj = nextSelectedObject.get()) {
			auto it = client->unitInfo.find(obj->id);
			const bool seen = !fogOfWarOn() || fogOfWar.isSeen(obj->position) || fogOfWar.isFriendly(client, obj);
			if (it != client->unitInfo.end() && seen) {
				std::string line = it->second.name.empty() ? obj->blueprint->name : it->second.name + " \xC2\xB7 " + obj->blueprint->name;
				if (obj->kills > 0)
					line += " \xC2\xB7 " + std::to_string(obj->kills) + (obj->kills == 1 ? " kill" : " kills");
				ImGui::SetTooltip("%s", line.c_str());
			}
		}
	}
}

// The statistics (F6) and the timeline (F7), from GameChronicle. In a game the statistics show
// this player and its allies only, and the timeline what it saw; in a replay, everyone and
// everything. A timeline entry clicked moves the camera there.
void ClientInterface::drawChronicle()
{
	// a replay shows everyone and everything; a game the player's own statistics and its allies',
	// and the events it saw (with fog of war; without, all events); once the player has won or
	// lost (still in the game), everyone and everything too
	const int outcome = client->ownOutcome();
	const bool all = showsEveryone();
	chronicle.update(client, (all || !fogOfWarOn()) ? nullptr : &fogOfWar);
	// a replay: its controls (not over the in-game menu), and its events
	if (replayMode) {
		replayView.update(client, replayServer, chronicle, playerColors);
		FrontEnd* frontEnd = FrontEnd::get();
		if (!(frontEnd && frontEnd->isOpen())) {
			replayView.drawControls(client, replayServer, gfx);
			replayView.drawLog(client, uiTexCache);
		}
	}
	// wkbai's unit history: the units that became heroes (a replay: all; a game: the player's
	// own and its allies')
	if (!client->newHeroes.empty()) {
		CommonGameObject* me = client->clientPlayer.get();
		for (uint32_t id : client->newHeroes) {
			ClientGameObject* obj = client->findObject(id);
			CommonGameObject* p = obj ? obj->getPlayer() : nullptr;
			const int allied = client->gameSet ? client->gameSet->diplomaticStatuses.names.getIndex("Allied") : -1;
			if (all || (p && me && (p == me || (allied >= 0 && client->getDiplomaticStatus(me, p) == allied))))
				replayView.announceHero(client, id, playerColors);
		}
		client->newHeroes.clear();
	}
	// heroes fallen (a replay: all; a game: the player's own and its allies')
	if (!chronicle.fallenAnnouncements.empty()) {
		CommonGameObject* me = client->clientPlayer.get();
		const int allied = client->gameSet ? client->gameSet->diplomaticStatuses.names.getIndex("Allied") : -1;
		for (const auto& [player, text] : chronicle.fallenAnnouncements) {
			CommonGameObject* p = client->findObject(player);
			if (all || (p && me && (p == me || (allied >= 0 && client->getDiplomaticStatus(me, p) == allied)))) {
				auto it = chronicle.playerColours.find(player);
				const int c = it != chronicle.playerColours.end() ? it->second : (p ? p->color : 0);
				replayView.announceFallen(text, playerColors[(c >= 0 && c < 9) ? c : 0], Vector3());
			}
		}
		chronicle.fallenAnnouncements.clear();
	}
	replayView.drawAnnouncements(uiTexCache, gfx);
	const uint32_t own = client->clientPlayer ? client->clientPlayer->id : 0;
	const int alliedStatus = client->gameSet ? client->gameSet->diplomaticStatuses.names.getIndex("Allied") : -1;
	auto shownPlayer = [&](uint32_t id) {
		if (all || id == own)
			return true;
		CommonGameObject* me = client->clientPlayer.get();
		CommonGameObject* other = client->findObject(id);
		return me && other && alliedStatus >= 0 && client->getDiplomaticStatus(me, other) == alliedStatus;
	};
	auto colourOf = [&](uint32_t id) {
		auto it = chronicle.playerColours.find(id);
		const int c = it != chronicle.playerColours.end() ? it->second : 0;
		return playerColors[(c >= 0 && c < 9) ? c : 0];
	};
	auto nameOf = [&](uint32_t id) {
		auto it = chronicle.playerNames.find(id);
		return it != chronicle.playerNames.end() ? it->second : std::to_string(id);
	};
	auto clock = [](float t) {
		char buf[16];
		snprintf(buf, sizeof(buf), "%d:%02d", (int)t / 60, (int)t % 60);
		return std::string(buf);
	};
	if (statisticsOpen) {
		ImGui::SetNextWindowPos(ImVec2(20, 110), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSize(ImVec2(640, 400), ImGuiCond_FirstUseEver);
		if (ImGui::Begin("Statistics (F6)", &statisticsOpen)) {
			if (outcome != 0 && !replayMode)
				ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f), "%s: every player shown", outcome == 1 ? "You have won" : "You have been defeated");
			if (ImGui::BeginTable("stats", 9, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
				for (const char* h : { "Player", "Food", "Wood", "Gold", "Peasants", "Soldiers", "Buildings", "Units lost", "Buildings lost" })
					ImGui::TableSetupColumn(h);
				ImGui::TableHeadersRow();
				for (const auto& [id, list] : chronicle.samples) {
					if (list.empty() || !shownPlayer(id))
						continue;
					const GameChronicle::Sample& s = list.back();
					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(colourOf(id)), "%s", nameOf(id).c_str());
					for (float v : { s.food, s.wood, s.gold, (float)s.peasants, (float)s.soldiers, (float)s.buildings, (float)s.unitsLost, (float)s.buildingsLost }) {
						ImGui::TableNextColumn();
						ImGui::Text("%.0f", v);
					}
				}
				ImGui::EndTable();
			}
			static const char* shown[6] = { "Food", "Wood", "Gold", "Peasants", "Soldiers", "Buildings" };
			ImGui::Combo("Graph", &statisticsShown, shown, 6);
			// the graph: each player's line in its colour, over the game so far
			const ImVec2 p0 = ImGui::GetCursorScreenPos();
			const ImVec2 size(std::max(ImGui::GetContentRegionAvail().x, 100.0f), std::max(ImGui::GetContentRegionAvail().y - 4.0f, 80.0f));
			ImDrawList* dl = ImGui::GetWindowDrawList();
			dl->AddRectFilled(p0, ImVec2(p0.x + size.x, p0.y + size.y), IM_COL32(0, 0, 0, 120));
			auto value = [&](const GameChronicle::Sample& s) {
				switch (statisticsShown) {
				case 0: return s.food; case 1: return s.wood; case 2: return s.gold;
				case 3: return (float)s.peasants; case 4: return (float)s.soldiers; default: return (float)s.buildings;
				}
			};
			float tMax = 1.0f, vMax = 1.0f;
			for (const auto& [id, list] : chronicle.samples)
				if (shownPlayer(id))
					for (const auto& s : list) {
						tMax = std::max(tMax, s.time);
						vMax = std::max(vMax, value(s));
					}
			for (const auto& [id, list] : chronicle.samples) {
				if (!shownPlayer(id))
					continue;
				for (size_t i = 1; i < list.size(); i++) {
					const ImVec2 a(p0.x + size.x * list[i - 1].time / tMax, p0.y + size.y * (1.0f - value(list[i - 1]) / vMax));
					const ImVec2 b(p0.x + size.x * list[i].time / tMax, p0.y + size.y * (1.0f - value(list[i]) / vMax));
					dl->AddLine(a, b, colourOf(id), 2.0f);
				}
			}
			char label[32];
			snprintf(label, sizeof(label), "%.0f", vMax);
			dl->AddText(ImVec2(p0.x + 4, p0.y + 2), IM_COL32(255, 255, 255, 200), label);
			dl->AddText(ImVec2(p0.x + size.x - 40, p0.y + size.y - 16), IM_COL32(255, 255, 255, 200), clock(tMax).c_str());
			ImGui::Dummy(size);
		}
		ImGui::End();
	}
	if (timelineOpen) {
		ImGui::SetNextWindowPos(ImVec2(680, 110), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSize(ImVec2(520, 400), ImGuiCond_FirstUseEver);
		if (ImGui::Begin("Timeline (F7)", &timelineOpen)) {
			// two views of the same events: the chronicle (a historian's paragraphs, oldest first,
			// like a journal) and the statistics (the numbers, newest first)
			const bool story = g_settings.value("timelineChronicle", true);
			if (ImGui::RadioButton("Chronicle", story) && !story) {
				g_settings["timelineChronicle"] = true;
				SaveSettings();
			}
			ImGui::SameLine();
			if (ImGui::RadioButton("Statistics", !story) && story) {
				g_settings["timelineChronicle"] = false;
				SaveSettings();
			}
			ImGui::Separator();
			ImGui::BeginChild("##entries");
			ChronicleNarrator::Context storyContext;
			storyContext.fromSettings(client->environmentSettings, client->environmentActive);
			storyContext.playerName = nameOf;
			int shownCount = 0;
			const size_t eventCount = chronicle.events.size();
			for (size_t k = 0; k < eventCount; k++) {
				const size_t i = story ? k : eventCount - 1 - k;
				const GameChronicle::Event& e = chronicle.events[i];
				if (!all && !e.seen)
					continue;
				shownCount++;
				std::string text = clock(e.time) + "  " + e.title();
				if (e.size)
					text += " (" + std::to_string(e.size) + " fighters, " + clock(e.endTime - e.time) + ")";
				ImGui::PushID((int)i);
				// the camera looks at a place from where it is now, as high up
				auto lookAt = [&](const Vector3& at) {
					Camera& cam = client->camera;
					const Vector3 dir = cam.direction;
					const float down = std::max(-dir.y, 0.2f);
					const float reach = (cam.position.y - at.y) / down;
					cam.position.x = at.x - dir.x * reach;
					cam.position.z = at.z - dir.z * reach;
				};
				if (!story && ImGui::Selectable(text.c_str()))
					lookAt(e.position);
				// each side: its name, how many it brought, what it lost
				for (uint32_t p : story ? std::set<uint32_t>() : e.players) {
					std::string side = "    " + nameOf(p);
					if (auto it = e.strength.find(p); it != e.strength.end())
						side += ": " + std::to_string(it->second) + " fighting";
					if (auto it = e.lost.find(p); it != e.lost.end()) {
						int total = 0;
						std::string types;
						for (const auto& [type, n] : it->second) {
							total += n;
							types += (types.empty() ? "" : ", ") + std::to_string(n) + " " + type;
						}
						side += ", lost " + std::to_string(total) + " (" + types + ")";
					}
					ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(colourOf(p)), "%s", side.c_str());
				}
				// wkbai's unit history: each side's top killer here, the heroes who fought here,
				// the heroes who fell here, who razed the settlement. A name is a link: one's own
				// unit alive is selected and looked at; another's alive and in sight is looked at;
				// else the event's place.
				auto mentionClicked = [&](const GameChronicle::Mention& m) {
					ClientGameObject* o = m.dead ? nullptr : (ClientGameObject*)client->findObject(m.unit);
					if (o && (o->flags & ClientGameObject::fTerminated))
						o = nullptr;
					const bool own = o && client->clientPlayer && o->getPlayer() == client->clientPlayer;
					const bool inSight = o && (own || !fogOfWarOn() || fogOfWar.tiles.empty() || fogOfWar.isSeen(o->position) || fogOfWar.isFriendly(client, o));
					if (own) {
						selection.clear();
						selection.insert(o);
					}
					lookAt(inSight ? o->position : e.position);
				};
				auto mentionLine = [&](int kind, const char* label, const ImVec4& colour) {
					std::vector<size_t> list;
					for (size_t j = 0; j < e.mentions.size(); j++)
						if (e.mentions[j].kind == kind)
							list.push_back(j);
					if (list.empty())
						return;
					ImGui::Indent(24.0f);
					ImGui::TextColored(colour, "%s:", label);
					for (size_t n = 0; n < list.size(); n++) {
						const GameChronicle::Mention& m = e.mentions[list[n]];
						const std::string kills = std::to_string(m.kills) + (m.kills == 1 ? " kill" : " kills");
						std::string before, after;
						if (kind == GameChronicle::MENTION_TOP_KILLER) {
							before = nameOf(m.player) + "'s ";
							after = " (" + std::to_string(m.kills) + ")";
						}
						else if (kind == GameChronicle::MENTION_PRESENT) {
							before = m.dead ? "the late " : "";
							after = " (" + kills + ")";
						}
						else if (kind == GameChronicle::MENTION_FALLEN)
							after = " (" + kills + ")" + (m.extra.empty() ? std::string() : ", " + m.extra);
						else if (kind == GameChronicle::MENTION_RAZED_BY) {
							before = nameOf(m.player) + "'s ";
							if (m.dead)
								before = "the late " + before;
						}
						if (n + 1 < list.size())
							after += kind == GameChronicle::MENTION_FALLEN ? ";" : ",";
						// on this line if it fits, else the next
						const float width = ImGui::CalcTextSize((before + m.name + after).c_str()).x;
						ImGui::SameLine();
						if (ImGui::GetContentRegionAvail().x < width)
							ImGui::NewLine();
						if (!before.empty()) {
							ImGui::TextColored(colour, "%s", before.c_str());
							ImGui::SameLine(0.0f, 0.0f);
						}
						ImGui::PushID((int)list[n]);
						if (ImGui::TextLink(m.name.empty() ? "?" : m.name.c_str()))
							mentionClicked(m);
						if (ImGui::IsItemHovered())
							ImGui::SetTooltip(m.dead ? "Where it happened" : "Look at it");
						ImGui::PopID();
						if (!after.empty()) {
							ImGui::SameLine(0.0f, 0.0f);
							ImGui::TextColored(colour, "%s", after.c_str());
						}
					}
					ImGui::Unindent(24.0f);
				};
				if (story) {
					// the date, the title (a link to the place), the paragraph: word by word, wrapped
					// to the window, the players in their colours, the units named as links
					if (shownCount > 1)
						ImGui::Spacing();
					ImGui::TextDisabled("%s", ChronicleNarrator::dateLine(e.time, storyContext).c_str());
					ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.93f, 0.82f, 0.55f, 1.0f));
					if (ImGui::Selectable(e.title().c_str()))
						lookAt(e.position);
					ImGui::PopStyleColor();
					ImGui::Indent(10.0f);
					bool lineStart = true;
					int link = 0;
					for (const ChronicleNarrator::Piece& piece : ChronicleNarrator::narrate(e, storyContext)) {
						// the words of plain text (each with its space); a name is one piece
						std::vector<std::string> tokens;
						if (piece.mention >= 0 || piece.player)
							tokens.push_back(piece.text);
						else
							for (size_t a = 0; a < piece.text.size();) {
								size_t b = piece.text.find(' ', a);
								b = b == std::string::npos ? piece.text.size() : piece.text.find_first_not_of(' ', b);
								b = b == std::string::npos ? piece.text.size() : b;
								tokens.push_back(piece.text.substr(a, b - a));
								a = b;
							}
						for (const std::string& token : tokens) {
							if (token.empty())
								continue;
							const bool blank = token.find_first_not_of(' ') == std::string::npos;
							const float width = ImGui::CalcTextSize(token.c_str()).x;
							if (!lineStart) {
								ImGui::SameLine(0.0f, 0.0f);
								if (ImGui::GetContentRegionAvail().x < width) {
									if (blank)
										continue; // no space at a line's start
									ImGui::NewLine();
								}
							}
							else if (blank)
								continue;
							lineStart = false;
							if (piece.mention >= 0 && piece.mention < (int)e.mentions.size()) {
								ImGui::PushID(link++);
								if (ImGui::TextLink(token.c_str()))
									mentionClicked(e.mentions[piece.mention]);
								if (ImGui::IsItemHovered())
									ImGui::SetTooltip(e.mentions[piece.mention].dead ? "Where it happened" : "Look at it");
								ImGui::PopID();
							}
							else if (piece.player)
								ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(colourOf(piece.player)), "%s", token.c_str());
							else
								ImGui::TextUnformatted(token.c_str());
						}
					}
					ImGui::Unindent(10.0f);
				}
				else {
					mentionLine(GameChronicle::MENTION_TOP_KILLER, "Most kills", ImVec4(0.92f, 0.92f, 0.92f, 1.0f));
					mentionLine(GameChronicle::MENTION_RAZED_BY, "Razed by", ImVec4(0.9f, 0.6f, 0.3f, 1.0f));
					mentionLine(GameChronicle::MENTION_PRESENT, "Present", ImVec4(0.9f, 0.78f, 0.27f, 1.0f));
					mentionLine(GameChronicle::MENTION_FALLEN, "Fallen", ImVec4(0.85f, 0.45f, 0.4f, 1.0f));
				}
				ImGui::PopID();
			}
			if (!shownCount)
				ImGui::TextUnformatted(all ? "Nothing has happened yet." : "Nothing seen yet.");
			// the chronicle follows its newest entry while one reads at its end
			if (story && shownCount != timelineShownCount && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 40.0f)
				ImGui::SetScrollHereY(1.0f);
			timelineShownCount = shownCount;
			ImGui::EndChild();
		}
		ImGui::End();
	}
	drawEnvironmentDebug();
	drawFrameTimings();
}

// The frame-time window (Ctrl+F12): the whole frame on the CPU and the GPU over the last
// seconds, and per pass of the frame (GpuProfiler's scopes: their parts indented) the GPU's and
// the CPU's time, smoothed, the last frame's and the GPU's peak, each pass's share of the GPU
// frame as a bar. The GPU's times come from timestamp queries (the Diligent renderer).
void ClientInterface::drawFrameTimings()
{
	if (!frameTimingsOpen)
		return;
	ImGui::SetNextWindowPos(ImVec2(40, 110), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(640, 720), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Frame timings (Ctrl+F12)", &frameTimingsOpen)) {
		ImGui::End();
		return;
	}
	const auto& cpu = ProfilerCpuHistory();
	const auto& gpu = ProfilerGpuHistory();
	auto stats = [](const std::vector<float>& v, float& avg, float& p99, float& mx) {
		avg = p99 = mx = 0.0f;
		if (v.empty())
			return;
		std::vector<float> sorted(v);
		std::sort(sorted.begin(), sorted.end());
		double sum = 0;
		for (float f : v)
			sum += f;
		avg = (float)(sum / v.size());
		p99 = sorted[std::min(sorted.size() - 1, (size_t)(sorted.size() * 0.99))];
		mx = sorted.back();
	};
	float cAvg, cP99, cMax, gAvg, gP99, gMax;
	stats(cpu, cAvg, cP99, cMax);
	stats(gpu, gAvg, gP99, gMax);
	ImGui::Text("Frame (CPU): %.2f ms avg (%.0f fps), p99 %.2f, max %.2f", cAvg, cAvg > 0 ? 1000.0f / cAvg : 0.0f, cP99, cMax);
	if (gpu.empty())
		ImGui::TextDisabled("GPU: no timestamp queries on this renderer");
	else
		ImGui::Text("GPU (the passes): %.2f ms avg, p99 %.2f, max %.2f; whole frame %.2f ms", gAvg, gP99, gMax, g_gpuFrameMs);
	const float graphMax = std::max({ 1000.0f / 30.0f, cP99 * 1.2f, gP99 * 1.2f });
	if (!cpu.empty())
		ImGui::PlotLines("##cpu", cpu.data(), (int)cpu.size(), 0, "CPU frame", 0.0f, graphMax, ImVec2(-1, 60));
	if (!gpu.empty())
		ImGui::PlotLines("##gpu", gpu.data(), (int)gpu.size(), 0, "GPU passes", 0.0f, graphMax, ImVec2(-1, 60));
	ImGui::TextDisabled("Graphs from 0 to %.1f ms (16.7 ms: 60 fps)", graphMax);
	ImGui::Separator();
	static bool showCpu = true, hideIdle = true;
	ImGui::Checkbox("CPU times", &showCpu);
	ImGui::SameLine();
	ImGui::Checkbox("Hide passes not drawn", &hideIdle);
	const auto& entries = ProfilerEntries();
	uint64_t newest = 0;
	for (const ProfileEntry& e : entries)
		newest = std::max(newest, e.lastFrame);
	const int columns = showCpu ? 6 : 4;
	if (ImGui::BeginTable("passes", columns, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp)) {
		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableSetupColumn("Pass", ImGuiTableColumnFlags_WidthStretch, 3.0f);
		ImGui::TableSetupColumn("GPU ms", ImGuiTableColumnFlags_WidthStretch, 1.0f);
		ImGui::TableSetupColumn("GPU peak", ImGuiTableColumnFlags_WidthStretch, 1.0f);
		ImGui::TableSetupColumn("Share", ImGuiTableColumnFlags_WidthStretch, 2.0f);
		if (showCpu) {
			ImGui::TableSetupColumn("CPU ms", ImGuiTableColumnFlags_WidthStretch, 1.0f);
			ImGui::TableSetupColumn("Calls", ImGuiTableColumnFlags_WidthStretch, 0.7f);
		}
		ImGui::TableHeadersRow();
		// as a tree: each pass, then its parts (in the order first seen)
		std::vector<const ProfileEntry*> order;
		auto addChildren = [&](const std::string& parent, int depth, auto& self) -> void {
			for (const ProfileEntry& c : entries)
				if (c.depth == depth && (depth == 0 || (c.name.size() > parent.size() + 1 && c.name.compare(0, parent.size(), parent) == 0 && c.name[parent.size()] == '/'))) {
					order.push_back(&c);
					self(c.name, depth + 1, self);
				}
		};
		addChildren(std::string(), 0, addChildren);
		for (const ProfileEntry* ep : order) {
			const ProfileEntry& e = *ep;
			if (hideIdle && newest > e.lastFrame + 30)
				continue;
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			const size_t slash = e.name.find_last_of('/');
			ImGui::Indent(14.0f * e.depth + 0.001f);
			ImGui::TextUnformatted(slash == std::string::npos ? e.name.c_str() : e.name.c_str() + slash + 1);
			ImGui::Unindent(14.0f * e.depth + 0.001f);
			ImGui::TableNextColumn();
			if (gpu.empty())
				ImGui::TextDisabled("-");
			else
				ImGui::Text("%.3f", e.gpuAvg);
			ImGui::TableNextColumn();
			if (!gpu.empty())
				ImGui::Text("%.3f", e.gpuPeak);
			ImGui::TableNextColumn();
			if (gAvg > 0) {
				const float share = (float)std::clamp(e.gpuAvg / gAvg, 0.0, 1.0);
				char label[16];
				snprintf(label, sizeof(label), "%.0f%%", share * 100.0f);
				ImGui::ProgressBar(share, ImVec2(-1, 0), label);
			}
			if (showCpu) {
				ImGui::TableNextColumn();
				ImGui::Text("%.3f", e.cpuAvg);
				ImGui::TableNextColumn();
				ImGui::Text("%d", e.calls);
			}
		}
		ImGui::EndTable();
	}
	ImGui::End();
}

// The environment's debug panel (F9, single player): day and night, the seasons and the weather
// set by hand. Each change goes to the server as a message (NETSRVMSG_ENVIRONMENT_DEBUG), so a
// recording replays it; the panel shows what the server last sent.
void ClientInterface::drawEnvironmentDebug()
{
	if (!environmentDebugOpen || !client || !client->getLevel())
		return;
	ImGui::SetNextWindowPos(ImVec2(40, 110), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(380, 0), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Environment debug (F9)", &environmentDebugOpen)) {
		ImGui::End();
		return;
	}
	const EnvironmentSettings& es = client->environmentSettings;
	const EnvironmentState& st = client->environment;
	const bool on = client->environmentActive;
	auto send = [&](const char* name, float value) { client->sendEnvironmentDebug(name, value); };

	// the state now
	if (on) {
		const float hour = g_environment.dayFraction * 24.0f;
		ImGui::Text("%02d:%02d  %s (%s)  %s  %s", (int)hour, (int)((hour - (int)hour) * 60.0f), SeasonName(st.season),
			st.severity <= 0 ? "gentle" : st.severity == 1 ? "normal" : "harsh", WeatherName(st.weather), FormatTemperature(st.temperatureC, es.tempScale).c_str());
		ImGui::Text("clouds %.2f (drawn %.2f)  rain %.2f  snowfall %.2f  fog %.2f", st.cloudCover, g_environment.weatherCover, st.rain, st.snowfall, st.fog);
		ImGui::Text("humidity %.0f%% (the light shafts' strength)", g_environment.humidity * 100.0f);
		ImGui::Text("wet %.2f  snow cover %.2f  ice %.2f%s  water %+.1f m", st.wetness, st.snowCover, st.ice, st.ice >= ICE_WALKABLE ? " (walkable)" : "", st.waterRise);
		ImGui::Text("sight %.2f  ground speed %.2f  daylight %.2f", st.sightFactor, st.speedFactor, g_environment.sun.daylight);
		{
			// the moon: its phase (the setting's, or natural), how much is lit, its light now
			const SunState& sun = g_environment.sun;
			const int named = (int)std::floor(sun.moonAge * 8.0f + 0.5f) % 8 + 1;
			ImGui::Text("moon: %s, day %.1f of 29.5, lit %.0f%%, light %.2f", MoonPhaseName(named), sun.moonAge * 29.53f, sun.moonLit * 100.0f, sun.moonLight);
			int phase = es.moonPhase;
			if (ImGui::BeginCombo("Moon phase", MoonPhaseName(phase))) {
				for (int p = 0; p <= 8; p++)
					if (ImGui::Selectable(MoonPhaseName(p), p == phase))
						send("moonPhase", (float)p);
				ImGui::EndCombo();
			}
		}
		// how often the weather changes, and in how long it does next
		std::string stability = WeatherStabilityName(es.weatherStability);
		float lo, hi;
		if (es.changeRange(lo, hi))
			stability += " (" + std::to_string((int)(lo / 60)) + "-" + std::to_string((int)(hi / 60)) + " min)";
		else
			stability += " (by the season's length)";
		const float left = client->environmentWeatherEnds - st.time;
		if (!es.seasons || left > 1e20f || client->environmentWeatherHeld >= 0)
			ImGui::Text("changes: %s  next: never (%s)", stability.c_str(), client->environmentWeatherHeld >= 0 ? "held" : "no seasons");
		else {
			const int sec = std::max(0, (int)(left + 0.999f));
			ImGui::Text("changes: %s  next in %d:%02d", stability.c_str(), sec / 60, sec % 60);
		}
		if (es.weatherStability == 4) {
			std::string types;
			for (int w = 0; w < WEATHER_COUNT; w++)
				if (es.weatherAllowed(w))
					types += (types.empty() ? "" : ", ") + std::string(WeatherName(w));
			ImGui::TextWrapped("allowed: %s", types.empty() ? "none (clear)" : types.c_str());
		}
	}
	else
		ImGui::TextUnformatted("Day and night and the seasons are off: the map's own look.");
	ImGui::Separator();

	// day and night
	bool dayNight = es.dayNight;
	if (ImGui::Checkbox("Day and night", &dayNight))
		send("dayNight", dayNight ? 1.0f : 0.0f);
	static int dayLength = 20;
	if (!ImGui::IsAnyItemActive())
		dayLength = es.dayLengthMin;
	ImGui::SetNextItemWidth(200);
	ImGui::SliderInt("Day length (min)", &dayLength, 1, 240);
	if (ImGui::IsItemDeactivatedAfterEdit())
		send("dayLength", (float)dayLength);
	static float hour = 12.0f;
	if (!ImGui::IsAnyItemActive())
		hour = g_environment.active ? g_environment.dayFraction * 24.0f : 12.0f;
	ImGui::BeginDisabled(!es.dayNight);
	ImGui::SetNextItemWidth(200);
	ImGui::SliderFloat("Time of day (h)", &hour, 0.0f, 24.0f, "%.2f");
	if (ImGui::IsItemDeactivatedAfterEdit())
		send("hour", hour);
	static const std::pair<const char*, float> hours[] = { { "Midnight", 0 }, { "Dawn", 6 }, { "Noon", 12 }, { "Dusk", 18.5f } };
	for (size_t i = 0; i < std::size(hours); i++) {
		if (i)
			ImGui::SameLine();
		if (ImGui::Button(hours[i].first))
			send("hour", hours[i].second);
	}
	ImGui::EndDisabled();
	ImGui::Separator();

	// the seasons
	bool seasons = es.seasons;
	if (ImGui::Checkbox("Seasons and weather", &seasons))
		send("seasons", seasons ? 1.0f : 0.0f);
	static int seasonLength = 30;
	if (!ImGui::IsAnyItemActive())
		seasonLength = es.seasonLengthMin;
	ImGui::SetNextItemWidth(200);
	ImGui::SliderInt("Season length (min)", &seasonLength, 1, 240);
	if (ImGui::IsItemDeactivatedAfterEdit())
		send("seasonLength", (float)seasonLength);
	ImGui::BeginDisabled(!es.seasons);
	static float season = 0.5f;
	if (!ImGui::IsAnyItemActive())
		season = g_environment.active ? g_environment.yearFraction * 4.0f : 0.5f;
	ImGui::SetNextItemWidth(200);
	ImGui::SliderFloat("Season (0 spring - 4)", &season, 0.0f, 3.999f, "%.2f");
	if (ImGui::IsItemDeactivatedAfterEdit())
		send("season", season);
	for (int s = 0; s < 4; s++) {
		if (s)
			ImGui::SameLine();
		if (ImGui::Button(SeasonName(s)))
			send("season", s + 0.05f); // the season's start
	}
	static const char* severities[] = { "gentle", "normal", "harsh", "random" };
	static const char* severityNames[] = { "weatherSpring", "weatherSummer", "weatherAutumn", "weatherWinter" };
	for (int s = 0; s < 4; s++) {
		int v = std::clamp(es.severity[s], 0, 3);
		ImGui::SetNextItemWidth(90);
		if (s % 2)
			ImGui::SameLine(190);
		// (an ID of its own: the season buttons above carry the same names)
		const std::string label = std::string(SeasonName(s)) + "##severity";
		if (ImGui::Combo(label.c_str(), &v, severities, 4))
			send(severityNames[s], (float)v);
	}
	ImGui::EndDisabled();
	ImGui::Separator();

	// the weather: held (also without the seasons), or chosen as usual
	int held = client->environmentWeatherHeld + 1;
	std::vector<const char*> weathers = { "Auto (as the season brings it)" };
	for (int w = 0; w < WEATHER_COUNT; w++)
		weathers.push_back(WeatherName(w));
	ImGui::BeginDisabled(!on);
	ImGui::SetNextItemWidth(220);
	if (ImGui::Combo("Weather", &held, weathers.data(), (int)weathers.size()))
		send("weather", (float)(held - 1));
	// the weather's cloud cover: random for each spell of weather (around its kind's), or held
	// at the slider's (moving it holds it; Random lets it vary again). Saved with the game.
	static int cover = 50;
	if (!ImGui::IsAnyItemActive())
		cover = (int)std::lround(std::max(g_environment.weatherCover, 0.0f) * 100.0f);
	ImGui::BeginDisabled(!es.seasons);
	ImGui::SetNextItemWidth(200);
	ImGui::SliderInt(es.weatherCover < 0 ? "Cloud cover (random)###cover" : "Cloud cover (held)###cover", &cover, 0, 100, "%d%%");
	if (ImGui::IsItemDeactivatedAfterEdit())
		send("weatherCover", (float)cover);
	ImGui::SameLine();
	ImGui::BeginDisabled(es.weatherCover < 0);
	if (ImGui::Button("Random"))
		send("weatherCover", -1.0f);
	ImGui::EndDisabled();
	ImGui::EndDisabled();
	ImGui::EndDisabled();
	// how often it changes (custom's limits and weathers: the lobby's)
	int stabilityChoice = std::clamp(es.weatherStability, 0, 4);
	ImGui::BeginDisabled(!es.seasons);
	ImGui::SetNextItemWidth(120);
	if (ImGui::Combo("Weather changes", &stabilityChoice, "Frequent\0Normal\0Seldom\0Random\0Custom\0"))
		send("weatherStability", (float)stabilityChoice);
	ImGui::EndDisabled();
	int scale = es.tempScale;
	ImGui::SetNextItemWidth(120);
	if (ImGui::Combo("Temperature", &scale, "Celsius\0Fahrenheit\0Kelvin\0"))
		send("tempScale", (float)scale);
	ImGui::TextDisabled("Changes are recorded and replay the same.");
	ImGui::End();
}

// The lower left panel's stained-glass window as glass (enhanced graphics): each pane bulges
// a little between the dark lead lines, so the painted pattern is refracted by its normal and
// a light sweeping slowly across glints on it. Precomputed on the CPU as GLASS_FRAMES frames
// of the panel (a cheap stand-in for a shader: the HUD is drawn through ImGui), cycled at 4 fps.
static constexpr int GLASS_FRAMES = 32;
void ClientInterface::drawStainedGlassPanel(int x, int y, int w, int h)
{
	static const char* path = "Interface\\InGame\\Bottom_Left_Panel.tga";
	if (!UseEnhancedRenderers(gfx) || !g_renderSettings.stainedGlass) {
		ImGui::GetBackgroundDrawList()->AddImage(uiTexCache.getTexture(path, false), ImVec2((float)x, (float)y), ImVec2((float)(x + w), (float)(y + h)));
		return;
	}
	if (glassFrames.empty()) {
		const Bitmap src = Bitmap::loadBitmap(path).convertToR8G8B8A8();
		const int W = src.width, H = src.height;
		if (W <= 0 || H <= 0)
			return;
		const unsigned char* px = src.pixels.data();
		// the window's glass: inside its circle and not the dark lead
		constexpr float CX = 128.0f, CY = 195.0f, R = 100.0f;
		std::vector<float> mask(W * H, 0.0f), tmp(W * H);
		for (int j = 0; j < H; j++)
			for (int i = 0; i < W; i++) {
				const unsigned char* p = px + (j * W + i) * 4;
				const float lum = 0.3f * p[0] + 0.59f * p[1] + 0.11f * p[2];
				const float dx = i - CX, dy = j - CY;
				if (dx * dx + dy * dy < R * R && lum >= 45.0f)
					mask[j * W + i] = 1.0f;
			}
		// the panes' height: the mask blurred (raised in the middle of each pane)
		std::vector<float> height = mask;
		for (int pass = 0; pass < 4; pass++) {
			for (int j = 0; j < H; j++)
				for (int i = 0; i < W; i++) {
					float s = 0; int n = 0;
					for (int k = -2; k <= 2; k++) { const int ii = std::clamp(i + k, 0, W - 1); s += height[j * W + ii]; n++; }
					tmp[j * W + i] = s / n;
				}
			for (int j = 0; j < H; j++)
				for (int i = 0; i < W; i++) {
					float s = 0; int n = 0;
					for (int k = -2; k <= 2; k++) { const int jj = std::clamp(j + k, 0, H - 1); s += tmp[jj * W + i]; n++; }
					height[j * W + i] = s / n * mask[j * W + i];
				}
		}
		auto sample = [&](float fx, float fy, int c) {
			fx = std::clamp(fx, 0.0f, (float)W - 1.001f); fy = std::clamp(fy, 0.0f, (float)H - 1.001f);
			const int i0 = (int)fx, j0 = (int)fy; const float ax = fx - i0, ay = fy - j0;
			auto at = [&](int i, int j) { return (float)px[(j * W + i) * 4 + c]; };
			return (at(i0, j0) * (1 - ax) + at(i0 + 1, j0) * ax) * (1 - ay) + (at(i0, j0 + 1) * (1 - ax) + at(i0 + 1, j0 + 1) * ax) * ay;
		};
		for (int f = 0; f < GLASS_FRAMES; f++) {
			// the light sweeps from the upper left to the upper right and back
			const float t = 0.5f - 0.5f * std::cos(f * 6.2831853f / GLASS_FRAMES);
			Vector3 light(-0.8f + 1.6f * t, -0.6f, 0.9f);
			light = light.normal();
			Bitmap out;
			out.width = W; out.height = H; out.format = BMFORMAT_R8G8B8A8;
			out.pixels.resize((size_t)W * H * 4);
			memcpy(out.pixels.data(), px, (size_t)W * H * 4);
			for (int j = 1; j < H - 1; j++)
				for (int i = 1; i < W - 1; i++) {
					if (mask[j * W + i] <= 0.0f)
						continue;
					const float gx = (height[j * W + i + 1] - height[j * W + i - 1]) * 0.5f;
					const float gy = (height[(j + 1) * W + i] - height[(j - 1) * W + i]) * 0.5f;
					Vector3 n(-gx * 6.0f, -gy * 6.0f, 1.0f);
					n = n.normal();
					// refraction: the pattern seen through the bulge
					const float rx = i + n.x * 2.2f, ry = j + n.y * 2.2f;
					const float diffuse = 0.88f + 0.22f * std::max(0.0f, n.dot(light));
					// the glint: the light reflected towards the viewer (0, 0, 1)
					const Vector3 refl = n * (2.0f * n.dot(light)) - light;
					const float spec = std::pow(std::max(0.0f, refl.z), 24.0f) * 110.0f;
					unsigned char* o = &out.pixels[((size_t)j * W + i) * 4];
					for (int c = 0; c < 3; c++)
						o[c] = (unsigned char)std::clamp(sample(rx, ry, c) * diffuse + spec, 0.0f, 255.0f);
				}
			glassFrames.push_back(gfx->CreateTexture(out, 1));
		}
	}
	const int f = (int)(SDL_GetTicks() / 250) % GLASS_FRAMES;
	ImGui::GetBackgroundDrawList()->AddImage(glassFrames[f], ImVec2((float)x, (float)y), ImVec2((float)(x + w), (float)(y + h)));
}

// The selected (else the hovered) character standing in the middle of the lower left panel's
// stained-glass window, slowly turning, as the original's selection window shows it: its model
// with its current animation, drawn after the HUD into a square there (Diligent only).
void ClientInterface::drawSelectionPortrait()
{
	DiligentGfx* dgfx = dynamic_cast<DiligentGfx*>(gfx);
	FrontEnd* frontEnd = FrontEnd::get();
	if (!dgfx || !sceneRenderer || !scene || !client->getLevel() || !loadingDone || editorOn || (frontEnd && frontEnd->isOpen()))
		return;
	ClientGameObject* obj = selection.empty() ? nullptr : selection.begin()->get();
	if (!obj)
		obj = nextSelectedObject.get();
	if (!obj || obj->blueprint->bpClass != Tags::GAMEOBJCLASS_CHARACTER)
		return;
	Model* model = obj->getModel();
	if (!model)
		return;
	// the window's middle in the panel (330 x 326 at the bottom left), in points
	constexpr float CX = 128.0f, CY = 195.0f, SIZE = 120.0f;
	const float s = g_uiScale;
	const float panelY = g_windowHeight / s - 326.0f;
	const int px = (int)((CX - SIZE / 2) * s), py = (int)((panelY + CY - SIZE / 2) * s), size = (int)(SIZE * s);

	static SceneEntity portrait;
	const float angle = (float)(SDL_GetTicks() % 12000) / 12000.0f * 6.2831853f;
	portrait = obj->sceneEntity;
	portrait.model = model;
	portrait.transform = Matrix::getRotationYMatrix(angle);
	// lit normally (a selected unit is drawn unlit as its highlight)
	portrait.flags &= ~(SceneEntity::SEFLAG_STATIC_SHADOW | SceneEntity::SEFLAG_DYNAMIC_SHADOW | SceneEntity::SEFLAG_NOLIGHTING);
	// the camera a little above, the figure filling the square
	const Vector3 centre = model->getSphereCenter();
	const float radius = std::max(0.5f, model->getSphereRadius());
	Camera cam;
	cam.fovY = 0.6f;
	cam.aspect = 1.0f;
	cam.nearDist = 0.1f;
	cam.farDist = 100.0f;
	const float dist = radius / std::sin(cam.fovY * 0.5f) * 1.05f;
	cam.position = centre + Vector3(0.0f, radius * 0.25f, -dist);
	cam.direction = (centre - cam.position).normal();
	cam.orientation = Vector3(std::asin(cam.direction.y), 0.0f, 0.0f);
	cam.viewMatrix = Matrix::getLHLookAtViewMatrix(cam.position, centre, Vector3(0, 1, 0));
	cam.projMatrix = Matrix::getLHPerspectiveMatrix(cam.fovY, 1.0f, cam.nearDist, cam.farDist);
	cam.sceneMatrix = cam.viewMatrix * cam.projMatrix;

	scene->clear();
	scene->add(&portrait);
	Camera* mainCamera = sceneRenderer->camera;
	sceneRenderer->camera = &cam;
	// ImGui leaves its last clip rectangle as the scissor
	gfx->DisableScissor();
	gfx->DisableFog();
	// ImGui draws without the depth target: the colour and depth targets bound again
	dgfx->RestoreRenderTargets();
	dgfx->ClearDepthOnly();
	dgfx->SetViewportPixels(px, py, size, size);
	// the scene renderers draw with the transform set (the client sets the camera's for the world)
	gfx->SetTransformMatrix(&cam.sceneMatrix);
	// not the static shadow pass: it would draw the terrain's shadow map from the portrait alone
	// (and reset the viewport)
	auto& sh = dgfx->shadows;
	const bool staticShadows = sh.staticShadows, skyOcc = sh.skyOccOn, rayShadows = sh.rayShadowsOn, rayReflections = sh.rayReflectionsOn;
	sh.staticShadows = false;
	// nor the world view's sky light map and ray-traced passes (they reset the viewport to the
	// whole screen: the figure filled it)
	sh.skyOccOn = sh.rayShadowsOn = sh.rayReflectionsOn = false;
	sh.portraitPass = true;
	sceneRenderer->render();
	sh.portraitPass = false;
	sh.staticShadows = staticShadows;
	sh.skyOccOn = skyOcc;
	sh.rayShadowsOn = rayShadows;
	sh.rayReflectionsOn = rayReflections;
	dgfx->RestoreRenderTargets();
	sceneRenderer->camera = mainCamera;
	scene->clear();
}

void ClientInterface::drawObject(ClientGameObject *obj, bool remembered)
{
	using namespace Tags;

	for (auto& typechildren : obj->children) {
		for (CommonGameObject* child : typechildren.second) {
			drawObject((ClientGameObject*)child);
		}
	}

	if (!devMode && !(obj->flags & ClientGameObject::fRenderable))
		return;
	if (!stampdownHidden.empty() && std::find(stampdownHidden.begin(), stampdownHidden.end(), obj->id) != stampdownHidden.end())
		return;
	// the fog of war hides enemy characters no unit of the player's sees, and their missiles
	// (arrows, bolts, a siege engine's shots: flying through the fog they showed what fired there)
	if ((obj->blueprint->bpClass == Tags::GAMEOBJCLASS_CHARACTER || obj->blueprint->bpClass == Tags::GAMEOBJCLASS_MISSILE)
		&& fogOfWarOn() && !fogOfWar.isSeen(obj->position) && !fogOfWar.isFriendly(client, obj))
		return;
	// and enemy buildings out of sight: drawn as last seen (FogOfWar::remembered) instead
	if (!remembered && obj->blueprint->bpClass == Tags::GAMEOBJCLASS_BUILDING && fogOfWarOn() && !fogOfWar.isBuildingSeen(obj->position) && fogOfWar.isRemembered(client, obj))
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
			// a variant beyond the list (an original save's "Marjoria's Keep (5)") read past it
			if (!anim.empty())
				model = anim[obj->animationVariant < anim.size() ? obj->animationVariant : 0];
		}
	}
	// a standing tree's default animation is its shake under the woodcutter's axe (the oaks' crowns
	// bounce): only while someone works at it ("I'm Being Worked at"), still otherwise
	if (model && obj->animationIndex == 0 && obj->blueprint->name.find("Tree") != std::string::npos
		&& dynamic_cast<AnimatedModel*>(model)) {
		static const GameSet* of = nullptr;
		static int worked = -1;
		if (of != client->gameSet.get()) {
			of = client->gameSet.get();
			worked = of->items.names.getIndex("I'm Being Worked at");
		}
		if (worked < 0 || obj->getItem(worked) <= 0.0f)
			model = model->getStaticModel();
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
	// wkbai's unit history: a hero on screen gets its name over its head
	if (obj->blueprint->bpClass == GAMEOBJCLASS_CHARACTER && !remembered && !client->unitInfo.empty()) {
		auto hit = client->unitInfo.find(obj->id);
		if (hit != client->unitInfo.end() && (hit->second.flags & 9) == 1 && !(obj->flags & ClientGameObject::fTerminated))
			heroLabels.push_back({ obj->id, sphereCenter + Vector3(0.0f, sphereRadius * 0.7f, 0.0f) });
	}

	obj->sceneEntity.model = model;
	obj->sceneEntity.transform = transformMatrix;
	// with the terrain tessellated, an object standing on the ground is drawn on its smooth
	// surface: lifted or lowered by the difference to the game's height there (the game itself
	// keeps its heights: placement, movement and multiplayer are unchanged)
	if (g_terrainTessellationActive && client->terrain && !obj->blueprint->shearModel) {
		const float ground = client->terrain->getHeight(obj->position.x, obj->position.z);
		if (std::abs(obj->position.y - ground) < 0.25f)
			obj->sceneEntity.transform._42 += client->terrain->getSmoothHeight(obj->position.x, obj->position.z) - ground;
	}
	if (obj->blueprint->shearModel && client->terrain)
		obj->sceneEntity.transform *= GetShearToGroundMatrix(client->terrain, model, transformMatrix, obj->position);
	// a remembered building has no parent: its colour and frame are those it had when last seen
	obj->sceneEntity.color = remembered ? obj->color : obj->getPlayer()->color;
	if (obj->animSynchronizedTask != -1 && !remembered) {
		CliScriptContext ctx{ client, obj };
		float frac = client->gameSet->tasks[obj->animSynchronizedTask].synchAnimationToFraction->eval(&ctx);
		obj->sceneEntity.animTime = (uint32_t)(model->getDuration() * frac * 1000.0f);
	}
	else if (!remembered)
		obj->sceneEntity.animTime = (uint32_t)((client->timeManager.currentTime - obj->animStartTime) * obj->animSpeed * 1000.0f);
	obj->sceneEntity.flags = 0;
	if (obj->animClamped) obj->sceneEntity.flags |= SceneEntity::SEFLAG_ANIM_CLAMP_END;
	// walls (RenderSettings::wallShadows): the sheared wall segments and every other piece of a
	// wall line by its building type, as the corners aren't sheared and would leave holes
	auto isWallPiece = [](const GameObjBlueprint* bp) {
		if (bp->bpClass != Tags::GAMEOBJCLASS_BUILDING)
			return false;
		switch (bp->buildingType) {
		case Tags::BUILDINGTYPE_WALL: case Tags::BUILDINGTYPE_WALL_CORNER_IN: case Tags::BUILDINGTYPE_WALL_CORNER_OUT:
		case Tags::BUILDINGTYPE_WALL_CROSSROADS: case Tags::BUILDINGTYPE_TOWER: case Tags::BUILDINGTYPE_TOWER_CORNER_IN:
		case Tags::BUILDINGTYPE_TOWER_CORNER_OUT: case Tags::BUILDINGTYPE_GATEHOUSE:
			return true;
		default:
			return bp->shearModel;
		}
	};
	if (obj->blueprint->hasStaticShadow || (g_renderSettings.wallShadows && isWallPiece(obj->blueprint)))
		obj->sceneEntity.flags |= SceneEntity::SEFLAG_STATIC_SHADOW;
	// the fields in winter (wkbai's seasons; they give no food then): bare soil
	if (client->environmentActive && client->environmentSettings.seasons && client->environment.season == 3
		&& obj->blueprint->bpClass == Tags::GAMEOBJCLASS_BUILDING) {
		const std::string& name = obj->blueprint->name;
		if (name.size() > 5 && name.compare(name.size() - 5, 5, " Farm") == 0)
			obj->sceneEntity.flags |= SceneEntity::SEFLAG_FALLOW;
	}
	// broadleaf trees and bushes (not the conifers, cedars and snowy trees): autumn colours
	if (client->environmentActive && client->environmentSettings.seasons) {
		const std::string& name = obj->blueprint->name;
		if (name.find("Deciduous") != std::string::npos || name.find("Willow") != std::string::npos
			|| name.find("City Tree") != std::string::npos || name.find("Bush") != std::string::npos)
			obj->sceneEntity.flags |= SceneEntity::SEFLAG_LEAVES;
	}
	// bare trees (wkbai's seasons): the broadleaf trees in winter (not the bushes, firs and cedars),
	// and every tree a wildfire burned (charred too). Drawn as the game set's own leafless tree
	// (two of its models, by the tree's id; the oaks' bark is only their trunk, the branches are in
	// the leaves); without it only the bark. A bush that burned is gone.
	if (client->environmentActive) {
		const std::string& name = obj->blueprint->name;
		const bool charred = !client->charredTrees.empty() && client->charredTrees.count(obj->id) != 0;
		const bool broadleaf = name.find("Deciduous") != std::string::npos || name.find("Willow") != std::string::npos;
		const bool tree = name.find("Tree") != std::string::npos && name.find("Bird") == std::string::npos;
		if ((charred && tree) || (broadleaf && TreeBareInWinter(client, obj->id))) {
			// a leafless tree made in code (TreeModels.cpp; four of each, by the tree's id), as high
			// as the tree it stands for
			Model* m = LeaflessTreeModel(client->gameSet->modelCache, (int)(obj->id * 2654435761u >> 28), charred);
			const auto box = model->interpolateBoundingBox(0);
			const float k = std::clamp(box[1].y - std::max(box[0].y, 0.0f), 0.5f, 60.0f);
			obj->sceneEntity.transform = Matrix::getScaleMatrix(Vector3(k, k, k)) * obj->sceneEntity.transform;
			obj->sceneEntity.model = m;
			obj->sceneEntity.flags &= ~SceneEntity::SEFLAG_LEAVES;
		}
		else if (charred)
			obj->sceneEntity.flags |= SceneEntity::SEFLAG_BARE;
		else if (broadleaf)
			AddLeafTree(client, sphereCenter, sphereRadius, obj->id); // its leaves falling in autumn
		if (charred)
			obj->sceneEntity.flags |= SceneEntity::SEFLAG_CHARRED;
	}

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
	bool blobShadow = model && obj->blueprint->hasDynamicShadow && g_renderSettings.characterShadows;
	// the shadow of the unit's shape from the sun's shadow map, where the renderer draws one
	if (blobShadow && gpuBlobShadows && g_renderSettings.realCharacterShadows && g_renderSettings.staticShadows) {
		obj->sceneEntity.flags |= SceneEntity::SEFLAG_DYNAMIC_SHADOW;
		blobShadow = false;
	}
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
		// the mouse only (a game text window under the cursor, or a modal one): the keys stay
		// with the game
		void takeMouse() {
			move(mouseDown, g_mouseDown); move(mousePressed, g_mousePressed); move(mouseReleased, g_mouseReleased);
			mouseWheel = std::exchange(g_mouseWheel, 0);
			mouseDoubleClicked = std::exchange(g_mouseDoubleClicked, 0);
		}
		void giveMouse() {
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
		if (const std::string text = g_settings.value("lobbyTest", nlohmann::json::object()).value("gameChat", std::string()); !text.empty())
			client->sendMessage(g_settings.value("playerName", std::string("Player")) + ": " + text);
	}

	// (while typing, Enter sends and Esc closes, whatever the chat key is)
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
	else if (KeyPressed(KeyAction::Chat) && !ImGui::GetIO().WantTextInput) {
		chatOpen = chatFocus = true;
		chatInput[0] = 0;
		for (int s = 0; s < KEY_SLOTS; s++)
			if (const KeyChord c = GetKeyBinding(KeyAction::Chat, s); !c.empty() && c.key < (int)std::size(g_keyPressed))
				g_keyPressed[c.key] = false;
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

// Control groups: Ctrl+0-9 (top row or keypad 1-9; keypad 0 turns the camera) stores the
// selection, the digit alone selects it again without the units lost since, and the same
// digit twice within 400 ms also centres the camera on them
void ClientInterface::controlGroupKeys()
{
	if (chatOpen || ImGui::GetIO().WantTextInput)
		return;
	// (the key bindings' groups 1-9 then 0; group numbers here 0-9 as the digits)
	int group = -1;
	bool store = false;
	for (int k = 0; k < 10 && group == -1; k++) {
		if (KeyPressed((KeyAction)((int)KeyAction::GroupStore1 + k)))
			group = (k + 1) % 10, store = true;
		else if (KeyPressed((KeyAction)((int)KeyAction::GroupSelect1 + k)))
			group = (k + 1) % 10;
	}
	if (group == -1)
		return;
	std::unordered_set<CliGORef>& stored = controlGroups[group];
	auto usable = [&](ClientGameObject* obj) {
		return obj && !(obj->flags & ClientGameObject::fTerminated) && (obj->flags & ClientGameObject::fSelectable)
			&& obj->getPlayer() == client->clientPlayer;
	};
	for (auto it = stored.begin(); it != stored.end();)
		it = usable(it->get()) ? std::next(it) : stored.erase(it);
	if (store) {
		stored.clear();
		for (const CliGORef& ref : selection)
			if (usable(ref.get()))
				stored.insert(ref);
		lastGroupRecalled = -1;
		if (BenchmarkEnabled())
			printf("CONTROLGROUP store %d: %zu\n", group, stored.size());
		return;
	}
	if (stored.empty())
		return;
	selection = stored;
	const uint64_t now = SDL_GetTicks();
	const bool twice = lastGroupRecalled == group && now - lastGroupRecallTicks < 400;
	lastGroupRecalled = twice ? -1 : group;
	lastGroupRecallTicks = now;
	if (BenchmarkEnabled())
		printf("CONTROLGROUP recall %d: %zu\n", group, selection.size());
	if (!twice || !client->terrain)
		return;
	// where the camera looks moved to the group's middle, as a minimap click does
	Vector3 centre(0, 0, 0);
	for (const CliGORef& ref : selection)
		centre += ref->position;
	centre = centre / (float)selection.size();
	Camera& cam = client->camera;
	Vector3 lookAt = cam.position;
	if (cam.direction.y < -0.01f) {
		const float groundY = client->terrain->getHeight(cam.position.x, cam.position.z);
		lookAt = cam.position + cam.direction * ((cam.position.y - groundY) / -cam.direction.y);
	}
	cam.position.x += centre.x - lookAt.x;
	cam.position.z += centre.z - lookAt.z;
	if (BenchmarkEnabled())
		printf("CONTROLGROUP centre %d: group at %.1f %.1f, camera looked at %.1f %.1f\n", group, centre.x, centre.z, lookAt.x, lookAt.z);
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
		if (!languageLoaded)
			lang.load("Languages\\Language.txt");
		languageLoaded = true;
	}
	// the cheat keys of the key bindings (F1, F2, F9 by default) work as this says, whatever key
	// they are bound to
	SetKeyCheatGate([this]() { return cheatKeysAllowed(); });
	prepareLevelBeforeFrame();
	// (from here a frame is being built: LoadingTick draws none)
	LoadingSetInFrame(true);

	// The in-game menu (Esc): while it is open, it gets the frame's input and the game none
	FrontEnd* frontEnd = FrontEnd::get();
	if (!singlePlayer && client->getLevel() && !(frontEnd && frontEnd->isOpen()))
		chatFrame();
	// Esc, or the top bar's Settings button (openInGameMenu from the HUD script, the frame before)
	const bool menuRequested = openMenuRequested;
	openMenuRequested = false;
	if (frontEnd && !frontEnd->isOpen() && (KeyPressed(KeyAction::OpenMenu) || menuRequested) && client->getLevel()) {
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
		const auto screens = g_settings.value("benchmark", nlohmann::json::object()).value("openMenu", nlohmann::json::array());
		if (!opened && !screens.empty()) {
			opened = true;
			for (const auto& screen : screens)
				frontEnd->open(screen.get<std::string>());
		}
	}
	// Test runs: "benchmark": { ..., "saveAs": "<name>" } saves the game once it has run 2 seconds
	// ("saveAfter": <seconds> for another time; counted from the first frame's game time, so a
	// loaded save runs that long too and isn't saved at once)
	static float benchmarkStartTime = -1.0f;
	if (BenchmarkEnabled() && client->getLevel() && benchmarkStartTime < 0.0f)
		benchmarkStartTime = client->timeManager.currentTime;
	if (frontEnd && BenchmarkEnabled() && client->getLevel() && client->timeManager.currentTime - benchmarkStartTime
		> g_settings.value("benchmark", nlohmann::json::object()).value("saveAfter", 2.0f)) {
		static bool saved = false;
		const std::string name = g_settings.value("benchmark", nlohmann::json::object()).value("saveAs", std::string());
		if (!saved && !name.empty()) {
			saved = true;
			saveRequested(name);
		}
	}
	// Test runs: "benchmark": { ..., "script": [seconds, "<action>"] } runs a script action (on the
	// server, through the script chat) once the game has run that long
	// (or a list of such pairs)
	if (BenchmarkEnabled() && client->getLevel()) {
		static size_t ran = 0;
		nlohmann::json script = g_settings.value("benchmark", nlohmann::json::object()).value("script", nlohmann::json());
		if (script.is_array() && script.size() == 2 && script[0].is_number())
			script = nlohmann::json::array({ script });
		while (script.is_array() && ran < script.size() && script[ran].is_array() && script[ran].size() == 2
			&& client->timeManager.currentTime > script[ran][0].get<float>())
			client->sendMessage("!" + script[ran++][1].get<std::string>());
	}
	// the ground a building levelled, drawn anew
	if (waterDesignChanged && terrainRenderer) {
		waterDesignChanged = false;
		applyWaterDesign();
	}
	if (terrainHeightsChanged && terrainRenderer && SDL_GetTicks() - terrainHeightsRefreshed >= 250) {
		terrainHeightsChanged = false;
		terrainHeightsRefreshed = SDL_GetTicks();
		rebuildTerrainRenderer(false);
	}
	// Test runs: "benchmark": { ..., "peasantActivity": 0 or 1 } opens the peasant activity interface
	if (BenchmarkEnabled() && peasantActivityMode == -1) {
		static bool applied = false;
		if (!applied) {
			applied = true;
			peasantActivityMode = g_settings.value("benchmark", nlohmann::json::object()).value("peasantActivity", -1);
		}
	}
	// Test runs: "benchmark": { ..., "selectActivity": <row 1-5> } clicks that peasant activity
	// row's icon once the level has been drawn 120 frames (paused runs too), and prints how many
	// it selected
	static int framesForActivity = 0;
	if (BenchmarkEnabled() && client->getLevel() && ++framesForActivity > 120) {
		static bool clicked = false;
		const int row = g_settings.value("benchmark", nlohmann::json::object()).value("selectActivity", 0);
		if (!clicked && row > 0) {
			clicked = true;
			printf("ACTIVITY row %d selected %d\n", row, selectPeasantsByActivity(row));
			fflush(stdout);
		}
	}
	// Test runs: "benchmark": { ..., "select": "<blueprint>" } selects the player's first object of
	// that type (once it is found, so that the selection can be emptied again)
	static bool benchmarkSelected = false;
	if (BenchmarkEnabled() && client->getLevel() && client->clientPlayer && selection.empty() && !benchmarkSelected) {
		const std::string name = g_settings.value("benchmark", nlohmann::json::object()).value("select", std::string());
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
			if (found) {
				selection.insert(found);
				benchmarkSelected = true;
			}
		}
	}
	const bool menuOpen = frontEnd && frontEnd->isOpen();
	static MenuInput menuInput;
	// a game text window (or the objectives) under the mouse, or a modal one: it gets the
	// mouse, the game none
	const bool windowsTakeMouse = !menuOpen && frontEnd && frontEnd->gameWindowsWantMouse(UiMouseX(), UiMouseY());
	if (menuOpen)
		menuInput.take();
	else if (windowsTakeMouse)
		menuInput.takeMouse();

	//----- Command cursors -----//
	TracyCZoneN(zoneInput, "Input and commands", 1);

	static Command* selectedCursorCommand = nullptr;
	WndCursor *nextCursor = nullptr;
	Command *rightClickCommand = nullptr;
	static std::vector<std::pair<WndCursor*, Command*>> availableCursorCommands;
	availableCursorCommands.clear();
	{
		static std::vector<ClientGameObject*> selected;
		selected.clear();
		for (ClientGameObject* sel : selection)
			if (sel)
				selected.push_back(sel);
		availableCursorCommands = FindCursorCommands(client, selected, nextSelectedObject);
		if (!availableCursorCommands.empty()) {
			nextCursor = availableCursorCommands[0].first;
			rightClickCommand = availableCursorCommands[0].second;
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
			// test runs: the hover cursor's command, each time it changes
			if (BenchmarkEnabled())
				printf("CURSOR %s %s over %s\n", rightClickCommand ? client->gameSet->commands.getString(rightClickCommand).c_str() : "-",
					WndCursorPath(currentCursor), nextSelectedObject ? nextSelectedObject->blueprint->name.c_str() : "the ground");
		}
	}

	//----- Input -----//

	Vector3 forward = client->camera.direction.normal2xz();
	Vector3 strafe = forward.cross(Vector3(0, 1, 0));
	// Shift scrolls faster
	const float camSpeed = g_renderSettings.scrollSpeed * (KeyDown(KeyAction::CameraFast) ? g_renderSettings.shiftScrollFactor : 1.0f) * 2.0f * cliDeltaTime / (1000.0f / 60.0f);
	forward *= camSpeed;
	strafe *= camSpeed;

	if (KeyDown(KeyAction::CameraForward))
		client->camera.position += forward;
	if (KeyDown(KeyAction::CameraBack))
		client->camera.position -= forward;
	if (KeyDown(KeyAction::CameraRight))
		client->camera.position -= strafe;
	if (KeyDown(KeyAction::CameraLeft))
		client->camera.position += strafe;
	// the mouse at the window's edge (the Mouse page's option): not in test runs, which would
	// read the real mouse, nor over the menu or an ImGui window
	if (g_renderSettings.edgeScroll && !BenchmarkEnabled() && SDL_GetMouseFocus() && !ImGui::GetIO().WantCaptureMouse
		&& !(frontEnd && frontEnd->isOpen())) {
		constexpr int edge = 3;
		if (g_mouseY < edge) client->camera.position += forward;
		if (g_mouseY >= g_windowHeight - edge) client->camera.position -= forward;
		if (g_mouseX >= g_windowWidth - edge) client->camera.position -= strafe;
		if (g_mouseX < edge) client->camera.position += strafe;
	}

	// moves the camera over the ground, at its height and angle, so that the middle of the view
	// falls on target (looking level or up: straight over it)
	auto centreCameraOn = [this](const Vector3& target) {
		Camera& cam = client->camera;
		const float pitch = cam.orientation.x, yaw = cam.orientation.y;
		const Vector3 dir(-std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch));
		const float dist = dir.y < -0.01f ? std::max((target.y - cam.position.y) / dir.y, 0.0f) : 0.0f;
		cam.position.x = target.x - dir.x * dist;
		cam.position.z = target.z - dir.z * dist;
	};

	// Test runs: "benchmark": { ..., "spaceTest": [ { "select": "<blueprint>", "count": n,
	// "formation": true, "presses": k }, ... ] } selects the player's first n of that type that
	// are in no formation (put into a new one first if asked), presses Space k times, 10 frames
	// apart, and prints where each press took the camera
	bool spaceTestPrint = false, spaceTestPress = false;
	if (BenchmarkEnabled() && client->getLevel() && client->clientPlayer) {
		static const nlohmann::json steps = g_settings.value("benchmark", nlohmann::json::object()).value("spaceTest", nlohmann::json::array());
		static size_t step = 0;
		static int frame = 0, wait = 0, press = 0, phase = 0;
		static std::vector<CliGORef> picked;
		if (++frame > 120 && step < steps.size() && --wait <= 0) {
			const nlohmann::json& s = steps[step];
			if (phase == 0) {
				const std::string name = s.value("select", std::string());
				const size_t count = s.value("count", 1);
				picked.clear();
				auto search = [&](ClientGameObject* obj, auto& self) -> void {
					if (picked.size() >= count)
						return;
					if (obj->blueprint->name == name && (obj->flags & ClientGameObject::fSelectable)
						&& !(obj->parent && obj->getParent()->blueprint->bpClass == Tags::GAMEOBJCLASS_FORMATION)) {
						picked.push_back(obj);
						return;
					}
					for (const auto& [type, children] : obj->children)
						for (CommonGameObject* child : children)
							self(child->dyncast<ClientGameObject>(), self);
				};
				search(client->clientPlayer.get(), search);
				if (s.value("formation", false)) {
					for (ClientGameObject* obj : picked)
						client->sendPutUnitIntoNewFormation(obj);
					client->sendCreateNewFormation();
				}
				printf("SPACE step %zu: %zu of %s picked%s\n", step, picked.size(), name.c_str(), s.value("formation", false) ? ", put into a new formation" : "");
				phase = 1;
				wait = 30;
			}
			else if (phase == 1) {
				selection.clear();
				for (const CliGORef& ref : picked)
					if (ref)
						selection.insert(ref);
				phase = 2;
				press = 0;
				wait = 5;
			}
			else if (press < s.value("presses", 1)) {
				spaceTestPress = true;
				spaceTestPrint = true;
				press++;
				wait = 10;
			}
			else {
				step++;
				phase = 0;
				if (step == steps.size())
					printf("SPACE test done\n");
			}
			fflush(stdout);
		}
	}

	// Space: the camera jumps to the selection at once, keeping its height and angle; each
	// further press goes on to the next selected unit, in the order of their ids, a formation
	// being one stop at its members' centre. A selection with anything new in it starts over at
	// the first; one that only lost units (killed, or shift-clicked off) goes on from where it was.
	static std::unordered_set<CliGORef> spaceSelection;
	static uint32_t spaceLastStop = 0;
	const bool selectionGrew = std::any_of(selection.begin(), selection.end(), [](const CliGORef& ref) { return !spaceSelection.count(ref); });
	if (selectionGrew || selection.empty())
		spaceLastStop = 0;
	if (selectionGrew || selection.size() != spaceSelection.size())
		spaceSelection = selection;
	std::optional<Vector3> cameraJump;
	size_t spaceStopCount = 0;
	ClientGameObject* spaceStop = nullptr;
	if ((KeyPressed(KeyAction::CameraToSelection) || spaceTestPress) && !ImGui::GetIO().WantTextInput) {
		std::map<uint32_t, ClientGameObject*> stops;
		for (ClientGameObject* sel : selection) {
			if (!sel)
				continue;
			ClientGameObject* stop = sel->parent && sel->getParent()->blueprint->bpClass == Tags::GAMEOBJCLASS_FORMATION ? sel->getParent() : sel;
			stops.emplace(stop->id, stop);
		}
		if (!stops.empty()) {
			auto it = stops.upper_bound(spaceLastStop);
			if (it == stops.end())
				it = stops.begin();
			spaceLastStop = it->first;
			spaceStop = it->second;
			spaceStopCount = stops.size();
			Vector3 target = spaceStop->position;
			if (spaceStop->blueprint->bpClass == Tags::GAMEOBJCLASS_FORMATION) {
				Vector3 sum;
				int members = 0;
				for (const auto& [type, children] : spaceStop->children)
					if (type.bpClass() == Tags::GAMEOBJCLASS_CHARACTER)
						for (CommonGameObject* child : children) {
							sum += child->position;
							members++;
						}
				if (members)
					target = sum / (float)members;
			}
			cameraJump = target;
			centreCameraOn(target);
		}
	}

	// The fixed camera (the original's default, without free look): a zoom from 0 (near) to 1
	// (far) sets its height above the ground, 12 to 110 m, and its pitch, 35 to 62 degrees down;
	// scrolling keeps that height over hills and valleys (eased, so it doesn't jolt); the middle
	// mouse button turns it round only; the wheel zooms.
	if (g_renderSettings.fixedCamera && client->terrain) {
		static bool fixedInit = false;
		static float fixedZoom = 0.4f, fixedGround = 0.0f;
		const Vector3& cp = client->camera.position;
		const float ground = std::max(client->terrain->getHeight(cp.x, cp.z), 0.0f);
		if (!fixedInit) {
			fixedInit = true;
			fixedGround = ground;
			fixedZoom = std::clamp((cp.y - ground - 12.0f) / 98.0f, 0.0f, 1.0f);
		}
		if (g_mouseWheel)
			fixedZoom = std::clamp(fixedZoom - g_mouseWheel * (g_renderSettings.invertMouseWheel ? -0.05f : 0.05f), 0.0f, 1.0f);
		const float ease = 1.0f - std::exp(-(float)cliDeltaTime / 250.0f);
		fixedGround += (ground - fixedGround) * ease;
		client->camera.position.y = fixedGround + 12.0f + 98.0f * fixedZoom;
		client->camera.orientation.x = -(0.61f + 0.47f * fixedZoom);
		if (cameraJump) {
			// a jump lands at once: the height over the new ground without easing, and the view
			// centred again for that height
			for (int i = 0; i < 2; i++) {
				fixedGround = std::max(client->terrain->getHeight(cp.x, cp.z), 0.0f);
				client->camera.position.y = fixedGround + 12.0f + 98.0f * fixedZoom;
				centreCameraOn(*cameraJump);
			}
		}
		if (getenv("WKB_TRACE_CAMERA")) printf("CAMERA pos (%.1f,%.1f,%.1f) pitch %.2f ground %.1f zoom %.2f\n", cp.x, cp.y, cp.z, client->camera.orientation.x, ground, fixedZoom);
		if (KeyDown(KeyAction::CameraRotate) || g_mouseDown[SDL_BUTTON_MIDDLE]) {
			if (!camroton) {
				camrotoffx = g_mouseX;
				camrotoriy = client->camera.orientation.y;
				camroton = true;
			}
			client->camera.orientation.y = camrotoriy - (g_mouseX - camrotoffx) * g_renderSettings.mouseLookSensitivity;
		}
		else
			camroton = false;
	}
	else if (KeyDown(KeyAction::CameraRotate) || g_mouseDown[SDL_BUTTON_MIDDLE]) {
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

	if (g_mouseWheel && !g_renderSettings.fixedCamera)
		client->camera.position.y += g_mouseWheel * (g_renderSettings.invertMouseWheel ? -2 : 2);

	// the map editor (F8 in single player) takes the clicks it uses
	// test runs: "benchmark": { ..., "statistics": true, "timeline": true } opens them
	if (BenchmarkEnabled()) {
		static bool opened = false;
		if (!opened) {
			opened = true;
			const nlohmann::json bench = g_settings.value("benchmark", nlohmann::json::object());
			statisticsOpen = bench.value("statistics", false);
			timelineOpen = bench.value("timeline", false);
			environmentDebugOpen = bench.value("environmentDebug", false);
		}
		// "environmentSet": [[name, value], ...]: the debug panel's changes, sent once the level runs
		static bool envSent = false;
		if (!envSent && client && client->getLevel() && withServer) {
			envSent = true;
			const nlohmann::json bench = g_settings.value("benchmark", nlohmann::json::object());
			for (const auto& e : bench.value("environmentSet", nlohmann::json::array()))
				if (e.is_array() && e.size() == 2)
					client->sendEnvironmentDebug(e[0].get<std::string>(), e[1].get<float>());
		}
	}
	if (KeyPressed(KeyAction::Statistics) && !ImGui::GetIO().WantTextInput)
		statisticsOpen = !statisticsOpen;
	if (KeyPressed(KeyAction::Timeline) && !ImGui::GetIO().WantTextInput)
		timelineOpen = !timelineOpen;
	if (KeyPressed(KeyAction::FrameTimings) && !ImGui::GetIO().WantTextInput)
		frameTimingsOpen = !frameTimingsOpen;
	// test runs: "benchmark": { ..., "frameTimings": true } opens the frame-time window
	static const bool benchTimings = BenchmarkEnabled() && g_settings.value("benchmark", nlohmann::json::object()).value("frameTimings", false);
	if (benchTimings)
		frameTimingsOpen = true;
	if (KeyPressed(KeyAction::MapEditor) && withServer && singlePlayer && editorAllowed && !replayMode)
		editorOn = !editorOn;
	// the environment's debug panel: in a skirmish with cheat mode on only (the game setting)
	const bool cheats = cheatKeysAllowed();
	if (KeyPressed(KeyAction::EnvironmentPanel) && cheats && !ImGui::GetIO().WantTextInput)
		environmentDebugOpen = !environmentDebugOpen;
	if (!cheats)
		environmentDebugOpen = false;
	if (withServer && BenchmarkEnabled() && g_settings.value("benchmark", nlohmann::json::object()).value("editorTest", false)) {
		editorOn = true;
		editorTestStep(peapos);
	}
	else if (withServer && BenchmarkEnabled() && g_settings.value("benchmark", nlohmann::json::object()).value("editorCheckTest", false)) {
		// a test run: the map editor's check printed once
		static int frame = 0;
		if (++frame == 30)
			for (const std::string& w : editorCheck())
				printf("EDITORCHECK %s\n", w.c_str());
	}
	else if (withServer && BenchmarkEnabled() && g_settings.value("benchmark", nlohmann::json::object()).value("editorBlankTest", false)) {
		// a test run: the map made blank, then saved as "Blank Test"
		editorOn = true;
		static int frame = 0;
		if (++frame == 30) {
			editorBlankMap();
			editorSave("Blank Test");
		}
	}
	else if (withServer && BenchmarkEnabled() && g_settings.value("benchmark", nlohmann::json()).contains("editorNewMapTest")) {
		// a test run: "editorNewMapTest": [width, height] makes a new map of that size, saved as
		// "Size Test", and opens it in the editor (once)
		static int frame = 0;
		static const Terrain* before = nullptr;
		static bool shown = false;
		if (++frame == 30) {
			const auto size = g_settings["benchmark"]["editorNewMapTest"];
			before = client->terrain;
			editorOn = true;
			editorNewMap("Size Test", size.at(0).get<int>(), size.at(1).get<int>());
		}
		else if (frame > 30 && client->terrain && client->terrain != before && client->getLevel() && !shown) {
			shown = true;
			const auto [w, h] = client->terrain->getNumPlayableTiles();
			printf("EDITORNEWMAP opened: %d x %d playable tiles, %d edge, editor %d\n", w, h, client->terrain->edge, (int)editorOn);
		}
	}
	else if (withServer && BenchmarkEnabled() && !g_settings.value("benchmark", nlohmann::json::object()).value("editorMode", std::string()).empty() && !editorOn) {
		editorOn = true;
		editorOpenMode(g_settings["benchmark"]["editorMode"].get<std::string>());
	}
	if (editorOn && withServer && editorInput(peapos)) {
		g_mousePressed[SDL_BUTTON_LEFT] = false;
		g_mouseDown[SDL_BUTTON_LEFT] = false;
	}

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
			client->sendStampdown(stampdownBlueprint, stampdownPlayer, peapos, sendStampdownEvent, stampdownFromCommand, stampdownOrientation());
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
			&& std::any_of(selection.begin(), selection.end(), [](const auto& s) {
				return s && (s->blueprint->bpClass == Tags::GAMEOBJCLASS_FORMATION || s->blueprint->bpClass == Tags::GAMEOBJCLASS_CHARACTER); })) {
			// units, groups and formations sent to the ground: the move goes on releasing the
			// button, facing the way the mouse was dragged meanwhile. As in the original, the
			// order's flag stands at the destination from the press on, and a line shows the facing.
			formationDrag = { true, peapos, rightClickCommand };
			showOrderFlag(peapos, false);
			orderFlagUnits.clear();
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

	// a right-drag: the original's thin white line from the destination (its flag) to where the
	// units will face
	if (formationDrag.active && (peapos - formationDrag.start).sqlen2xz() > FORMATION_DRAG_MIN * FORMATION_DRAG_MIN) {
		auto toScreen = [this](const Vector3& p) {
			const Vector3 t = p.transformScreenCoords(client->camera.sceneMatrix);
			return ImVec2((t.x + 1) * 0.5f * g_windowWidth / g_uiScale, (1 - t.y) * 0.5f * g_windowHeight / g_uiScale);
		};
		ImGui::GetBackgroundDrawList()->AddLine(toScreen(formationDrag.start), toScreen(peapos), IM_COL32(255, 255, 255, 235), 2.0f);
	}

	// Test runs: "benchmark": { ..., "keys": [[frame, "<key>"], ...] } presses each key (an SDL
	// name, "Ctrl+" in front for Ctrl held; "Deselect" empties the selection) that many frames
	// after the level is there
	static bool simulatedCtrl = false;
	if (BenchmarkEnabled() && client->getLevel()) {
		static int keyFrame = 0;
		keyFrame++;
		for (const auto& key : g_settings.value("benchmark", nlohmann::json::object()).value("keys", nlohmann::json::array())) {
			if (!key.is_array() || key.size() != 2 || key[0].get<int>() != keyFrame)
				continue;
			std::string name = key[1].get<std::string>();
			if (name == "Deselect") {
				selection.clear();
				continue;
			}
			if (name.rfind("Ctrl+", 0) == 0) {
				name = name.substr(5);
				simulatedCtrl = !g_modCtrl;
				g_modCtrl = true;
			}
			if (const SDL_Scancode sc = SDL_GetScancodeFromName(name.c_str()); sc != SDL_SCANCODE_UNKNOWN)
				g_keyPressed[sc] = true;
		}
	}
	// (a replay's controls have their own keys)
	// Test runs: "benchmark": { ..., "diplomacyWindow": [frame, window 1-3] } opens that diplomacy
	// window once, that many frames after the level is there
	if (BenchmarkEnabled() && client->getLevel()) {
		static int diplomacyFrame = 0;
		const nlohmann::json open = g_settings.value("benchmark", nlohmann::json::object()).value("diplomacyWindow", nlohmann::json());
		if (open.is_array() && open.size() == 2 && ++diplomacyFrame == open[0].get<int>())
			toggleDiplomacyRequested = std::clamp(open[1].get<int>() - 1, 0, 2);
	}
	// O: the mission objectives window (the original's key, Hot Keys.cpp)
	if (KeyPressed(KeyAction::Objectives) && client->missionObjectives >= 0)
		toggleObjectivesRequested = true;
	if (KeyPressed(KeyAction::Pause) && !replayMode) {
		static bool pauseState = false;
		pauseState = !pauseState;
		client->sendPauseRequest(pauseState);
	}
	if (KeyPressed(KeyAction::SpeedUp) && !replayMode) {
		client->sendGameSpeedChange(client->timeManager.timeSpeed * 2.0f);
	}
	if (KeyPressed(KeyAction::SlowDown) && !replayMode) {
		client->sendGameSpeedChange(client->timeManager.timeSpeed / 2.0f);
	}
	if (KeyPressed(KeyAction::DeleteSelected) && !ImGui::GetIO().WantTextInput) {
		// in the map editor, removed so that undo can place them again
		if (editorOn && withServer)
			editorDeleteSelection();
		else
			for (ClientGameObject* sel : selection)
				if (sel)
					client->sendTerminateObject(sel);
	}
	controlGroupKeys();
	if (simulatedCtrl)
		g_modCtrl = simulatedCtrl = false;

	static bool debuggerOn = false;
	static bool uiScriptOn = true;
	// F2: the client's debugger (every object, the dev mode, the sun): a cheat key
	if (KeyPressed(KeyAction::Debugger) && cheatKeysAllowed()) {
		debuggerOn = !debuggerOn;
	}
	if (debuggerOn && !cheatKeysAllowed())
		debuggerOn = false;
	if (KeyPressed(KeyAction::HideInterface) && !ImGui::GetIO().WantTextInput) {
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
	if (cameraJump)
		centreCameraOn(*cameraJump); // the limits may have lifted it
	if (spaceTestPrint) {
		const Camera& cam = client->camera;
		const float pitch = cam.orientation.x, yaw = cam.orientation.y;
		const Vector3 dir(-std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch));
		if (cameraJump) {
			const Vector3& t = *cameraJump;
			const float dist = dir.y < -0.01f ? (t.y - cam.position.y) / dir.y : 0.0f;
			printf("SPACE press: stop %u (%s) of %zu, target (%.1f,%.1f,%.1f), camera (%.1f,%.1f,%.1f) pitch %.2f yaw %.2f, view centre (%.1f,%.1f)\n",
				spaceStop->id, spaceStop->blueprint->name.c_str(), spaceStopCount, t.x, t.y, t.z,
				cam.position.x, cam.position.y, cam.position.z, pitch, yaw, cam.position.x + dir.x * dist, cam.position.z + dir.z * dist);
		}
		else
			printf("SPACE press: nothing selected, camera stays at (%.1f,%.1f,%.1f)\n", cam.position.x, cam.position.y, cam.position.z);
		fflush(stdout);
	}

	// F10: the Options screen (the in-game menu's, over the game; Back goes to the in-game menu),
	// pausing a single-player game as Esc does
	static bool settingsOn = false;
	if (KeyPressed(KeyAction::OpenOptions) && frontEnd && !frontEnd->isOpen() && client->getLevel()) {
		frontEnd->setSinglePlayer(singlePlayer);
		frontEnd->setCanSave((bool)saveGame);
		frontEnd->open("ingame");
		frontEnd->open("options");
		if (singlePlayer && !client->timeManager.paused) {
			client->sendPauseRequest(true);
			menuPausedGame = true;
		}
	}

	// [ and ]: the building being placed turned by a quarter, as in WKB.exe (0x64ff22: the keys
	// by their place on the keyboard, scan codes 0x1A and 0x1B, the two right of P; [ from 0 to
	// pi/2, ] the other way). Back to unturned for another building.
	if (stampdownBlueprint != stampdownTurnedFor) {
		stampdownTurnedFor = stampdownBlueprint;
		stampdownQuarter = 0;
	}
	if (stampdownBlueprint && !ImGui::GetIO().WantTextInput) {
		if (KeyPressed(KeyAction::TurnBuildingLeft))
			stampdownQuarter = (stampdownQuarter + 1) % 4;
		if (KeyPressed(KeyAction::TurnBuildingRight))
			stampdownQuarter = (stampdownQuarter + 3) % 4;
	}

	if (KeyPressed(KeyAction::NextCommand) && !ImGui::GetIO().WantTextInput) {
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
	if (editorOn && withServer)
		editorWindow();
	if (!singlePlayer && client->getLevel() && !(frontEnd && frontEnd->isOpen()))
		chatDraw();
	if (settingsOn)
		drawSettingsWindow(&settingsOn);
	if (g_renderSettings.showFps) {
		const FrameStats::Summary ft = g_frameStats.summarize();
		char text[96];
		snprintf(text, sizeof(text), "%d FPS  %.2f ms (p99 %.2f)", framesPerSecond, ft.avgMs, ft.p99Ms);
		ImDrawList* fg = ImGui::GetForegroundDrawList();
		// below the top panel, under the game speed's line (ClientUIScript.lua: at y 88)
		const ImVec2 pos(12.0f, 88.0f + ImGui::GetFontSize() + 10.0f);
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

	// (the game text windows: updateGameWindows, drawn by the menu script)

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
		// the HUD lays out in points (window.h)
		lua.set_function("getWindowWidth", []() {return (int)(g_windowWidth / g_uiScale); });
		lua.set_function("getWindowHeight", []() {return (int)(g_windowHeight / g_uiScale); });
		// Client object
		lua.set_function("getPlayerID",
			[this]() { ClientGameObject* obj = client->clientPlayer.get(); return obj ? obj->id : 0u; }
		);
		lua.set_function("getItem",
			[this](uint32_t id, const std::string& item) {
				int itemId = client->gameSet->items.names.getIndex(item);
				ClientGameObject* obj = client->findObject(id);
				return (obj && itemId >= 0) ? obj->getItem(itemId) : 0.0f;
			});
		// the object under the mouse (0: none), for the selection panel's stats on hover
		lua.set_function("getHoveredObject",
			[this]() -> uint32_t {
				ClientGameObject* obj = nextSelectedObject.get();
				return obj ? obj->id : 0u;
			});
		// wkbai's unit history (Client::unitInfo): the name shown ("" none), the birth name, a hero,
		// tracked at all, the age as the panel shows it, the kills and the buildings destroyed,
		// the kills to become a hero
		lua.set_function("getUnitName", [this](uint32_t id) -> std::string {
			auto it = client->unitInfo.find(id);
			return it != client->unitInfo.end() ? it->second.name : std::string(); });
		lua.set_function("getUnitBirthName", [this](uint32_t id) -> std::string {
			auto it = client->unitInfo.find(id);
			return it != client->unitInfo.end() ? it->second.birthName : std::string(); });
		lua.set_function("isUnitHero", [this](uint32_t id) {
			auto it = client->unitInfo.find(id);
			return it != client->unitInfo.end() && (it->second.flags & 1) != 0; });
		lua.set_function("isUnitTracked", [this](uint32_t id) { return client->unitInfo.count(id) != 0; });
		lua.set_function("getUnitAge", [this](uint32_t id) { return UnitAgeText(client, id); });
		lua.set_function("getUnitKills", [this](uint32_t id) {
			ClientGameObject* obj = client->findObject(id);
			return obj ? obj->kills : 0; });
		lua.set_function("getUnitBuildingsDestroyed", [this](uint32_t id) {
			ClientGameObject* obj = client->findObject(id);
			return obj ? obj->buildingsDestroyed : 0; });
		lua.set_function("getHeroThreshold", [this]() { return client->heroThreshold; });
		// a formation (or a member's formation): "Formation: 212 kills · heroes: Sigurd the
		// Valiant" ("" for none)
		lua.set_function("getFormationSummary", [this](uint32_t id) -> std::string {
			ClientGameObject* obj = client->findObject(id);
			if (obj && obj->blueprint->bpClass != Tags::GAMEOBJCLASS_FORMATION)
				obj = obj->getParent();
			if (!obj || obj->blueprint->bpClass != Tags::GAMEOBJCLASS_FORMATION)
				return std::string();
			std::string heroes;
			int count = 0;
			for (const auto& [type, members] : obj->children)
				for (CommonGameObject* m : members)
					if (auto it = client->unitInfo.find(m->id); it != client->unitInfo.end() && (it->second.flags & 1)) {
						heroes += (count++ ? ", " : "") + it->second.name;
					}
			std::string text = "Formation: " + std::to_string(obj->kills) + (obj->kills == 1 ? " kill" : " kills");
			if (count)
				text += std::string(count == 1 ? " \xC2\xB7 hero: " : " \xC2\xB7 heroes: ") + heroes;
			return text;
		});
		// several selected: the one with the most kills ("Best: Sigurd the Valiant, 64 kills"; ""
		// with one selected or none with a kill)
		lua.set_function("getSelectionBest", [this]() -> std::string {
			if (selection.size() < 2)
				return std::string();
			ClientGameObject* best = nullptr;
			for (const auto& ref : selection)
				if (ClientGameObject* o = ref.get(); o && o->kills > 0 && (!best || o->kills > best->kills))
					best = o;
			if (!best)
				return std::string();
			auto it = client->unitInfo.find(best->id);
			const std::string name = it != client->unitInfo.end() && !it->second.name.empty() ? it->second.name : best->blueprint->name;
			return "Best: " + name + ", " + std::to_string(best->kills) + (best->kills == 1 ? " kill" : " kills");
		});
		// part of an image: its rectangle u0,v0 - u1,v1 (0..1) drawn into x, y, width, height
		lua.set_function("drawImagePart",
			[](int x, int y, int width, int height, const std::string& path, float u0, float v0, float u1, float v1) {
				texture tex = csTextureCache.getTexture(path.c_str(), true);
				ImGui::GetBackgroundDrawList()->AddImage(tex, ImVec2((float)x, (float)y), ImVec2((float)(x + width), (float)(y + height)), ImVec2(u0, v0), ImVec2(u1, v1));
			});
		lua.set_function("openInGameMenu", [this]() { openMenuRequested = true; });
		// the mission objectives (the top bar's button): whether the level has some, whether
		// there is news not seen yet (the button flashes), and the window opened or closed
		lua.set_function("hasObjectives", [this]() { return client->missionObjectives >= 0; });
		lua.set_function("objectivesNews", []() { FrontEnd* fe = FrontEnd::get(); return fe && fe->objectivesNews(); });
		lua.set_function("toggleObjectives", [this]() { toggleObjectivesRequested = true; });
		// the top bar's Diplomacy (1), Global Diplomacy (2) and Tributes (3) buttons: enabled, news
		// (it flashes), its tooltip; and its window opened or closed
		lua.set_function("diplomacyButton", [this](int i) {
			FrontEnd::Diplomacy d = diplomacyView();
			const int k = std::clamp(i - 1, 0, 2);
			return std::make_tuple(d.enabled[k], d.news[k], d.tooltips[k]);
		});
		lua.set_function("toggleDiplomacyWindow", [this](int i) { toggleDiplomacyRequested = std::clamp(i - 1, 0, 2); });
		// the lower left panel, its window as glass with enhanced graphics
		lua.set_function("drawStainedGlassPanel", [this](int x, int y, int w, int h) { drawStainedGlassPanel(x, y, w, h); });
		// text in a colour (0xAABBGGRR, as drawRect)
		lua.set_function("drawTextColor",
			[](int x, int y, const std::string& text, uint32_t color) {
				ImGui::GetBackgroundDrawList()->AddText(ImVec2((float)x, (float)y), color, text.c_str());
			});
		// the peasant activity interface: -1 closed, 0 the idle bar, 1 all five bars
		lua.set_function("getPeasantActivityMode", [this]() { return peasantActivityMode; });
		lua.set_function("selectPeasantsByActivity", [this](int row) { return selectPeasantsByActivity(row); });
		lua.set_function("getTextWidth",
			[](const std::string& text) { return (int)ImGui::CalcTextSize(text.c_str()).x; });
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
		lua.set_function("getGameSpeed", [this]() { return client->timeManager.timeSpeed; });
		lua.set_function("isGamePaused", [this]() { return client->timeManager.paused; });
		// wkbai's day and night, seasons and weather, one short line ("" when off), drawn by the
		// HUD script under the resources
		lua.set_function("getEnvironmentLine", [this]() { return EnvironmentHudText(client); });
		// a recording played: no command panels (the server takes no orders), its own speed line
		lua.set_function("isReplay", [this]() { return replayMode; });
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
		lua.set_function("getMouseX", []() {return UiMouseX(); });
		lua.set_function("getMouseY", []() {return UiMouseY(); });
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
					stampdownQuarter = 0; // a new pick starts unturned, even the same building (0x6535da)
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
			// Test runs: "benchmark": { ..., "buildTab": "MILITARY" } opens that tab of the build menu
			if (BenchmarkEnabled()) {
				static bool tabSet = false;
				const std::string tab = g_settings.value("benchmark", nlohmann::json::object()).value("buildTab", std::string());
				if (!tabSet && !tab.empty()) {
					tabSet = true;
					lua["buildTab"] = tab;
				}
			}
			// wkbai's unit history: the heroes' names, under the interface's panels
			drawUnitHistoryOverlay();
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
		else if (windowsTakeMouse)
			menuInput.giveMouse();
		updateGameWindows(frontEnd);
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
			else if (r.action == "diplomacy_status" || r.action == "diplomacy_offer" || r.action == "tribute" || r.action == "diplomacy_seen")
				diplomacyRequest(r.action, r.arg);
			else if (r.action == "gtw_button") {
				const size_t comma = r.arg.find(',');
				if (comma != std::string::npos)
					gameWindowButton(atoi(r.arg.c_str()), atoi(r.arg.c_str() + comma + 1));
			}
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
	// the 3D scene post-processed (enhanced graphics on Diligent)
	DiligentGfx* postGfx = dynamic_cast<DiligentGfx*>(gfx);
	const bool fogOn = fogOfWarOn();
	// Test runs: "benchmark": { ..., "fogReveal": [x, z, radius, from, until] } lights that circle
	// (metres; seconds of game time) as a unit of the player's would, to fill the memory of the
	// enemy buildings there; the counts are printed once a second
	if (fogOn && BenchmarkEnabled()) {
		const nlohmann::json reveal = g_settings.value("benchmark", nlohmann::json::object()).value("fogReveal", nlohmann::json());
		fogOfWar.testViewers.clear();
		const float now = client->timeManager.currentTime;
		if (reveal.is_array() && reveal.size() == 5 && now >= reveal[3].get<float>() && now < reveal[4].get<float>())
			fogOfWar.testViewers.push_back({ Vector3(reveal[0].get<float>(), 0, reveal[1].get<float>()), reveal[2].get<float>() });
		static int lastPrinted = -1;
		if ((int)now != lastPrinted && client->getLevel()) {
			lastPrinted = (int)now;
			int enemy = 0, shown = 0;
			// and at the start, where each enemy's buildings stand (their mean)
			std::map<uint32_t, std::pair<Vector3, int>> towns;
			auto count = [&](CommonGameObject* obj, auto& self) -> void {
				for (const auto& [type, children] : obj->children)
					for (CommonGameObject* child : children) {
						self(child, self);
						if (fogOfWar.isRemembered(client, child) && !(child->flags & ClientGameObject::fTerminated)) {
							enemy++;
							shown += fogOfWar.isBuildingSeen(child->position);
							auto& town = towns[child->getPlayer()->id];
							town.first += child->position;
							town.second++;
						}
					}
			};
			count(client->getLevel(), count);
			if (lastPrinted == 0)
				printf("FOG camera at (%.0f, %.0f, %.0f)\n", client->camera.position.x, client->camera.position.y, client->camera.position.z);
			if (lastPrinted == 0)
				for (const auto& [player, town] : towns)
					printf("FOG player %u: %d buildings about (%.0f, %.0f)\n", player, town.second, town.first.x / town.second, town.first.z / town.second);
			int remembered = 0;
			for (const auto& [id, copy] : fogOfWar.remembered)
				remembered += !fogOfWar.isBuildingSeen(copy->position);
			printf("FOG t=%d enemy buildings %d, in sight %d, remembered out of sight %d\n", lastPrinted, enemy, shown, remembered);
			fflush(stdout);
		}
	}
	if (fogOn)
		fogOfWar.update(client);
	if (postGfx) {
		const bool effects = UseEnhancedRenderers(gfx) && (g_renderSettings.ssao || g_renderSettings.bloom || g_renderSettings.antiAliasing > 0 || g_renderSettings.upscaling > 0);
		postGfx->BeginPostProcessing(client->getLevel() && (effects || fogOn), UseEnhancedRenderers(gfx));
	}
	gfx->ClearFrame(true, true, fogColour);
	gfx->BeginDrawing();

	Vector3 benchmarkPosition, benchmarkOrientation;
	const bool benchmarkCamera = BenchmarkCamera(benchmarkPosition, benchmarkOrientation);
	if (benchmarkCamera) {
		client->camera.position = benchmarkPosition;
		client->camera.orientation = benchmarkOrientation;
	}
	// the camera's own position kept over the playable map, like a player's in a first-person
	// view: turning, tilting or raising it never moves it (not the benchmark's fixed views, nor
	// the map editor's)
	else if (client->terrain && client->getLevel() && !editorOn) {
		Camera& cam = client->camera;
		const auto [areaWidth, areaDepth] = client->terrain->getPlayableArea();
		cam.position.x = std::clamp(cam.position.x, 0.0f, areaWidth);
		cam.position.z = std::clamp(cam.position.z, 0.0f, areaDepth);
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
	if (skyRenderer) {
		PROFILE_SCOPE("Sky and clouds");
		skyRenderer->render();
	}

	// Test runs: "benchmark": { ..., "mouse": [x, y] } holds the cursor there (window pixels);
	// "stampdown": "<blueprint>" places that building of the player's (its preview, tooltips)
	if (BenchmarkEnabled()) {
		const nlohmann::json bench = g_settings.value("benchmark", nlohmann::json());
		if (const nlohmann::json mouse = bench.value("mouse", nlohmann::json()); mouse.is_array() && mouse.size() == 2) {
			g_mouseX = mouse[0].get<int>();
			g_mouseY = mouse[1].get<int>();
		}
		// "hover": "<blueprint>": the camera over the first such object (the player's own first),
		// the mouse held on it
		if (const std::string name = bench.value("hover", std::string()); !name.empty() && client->getLevel() && client->terrain) {
			ClientGameObject* found = nullptr;
			auto search = [&](ClientGameObject* obj, auto& self) -> void {
				for (const auto& [type, children] : obj->children)
					for (CommonGameObject* c : children) {
						// ("hoverHero": true: a hero of that type, wkbai's unit history)
						if (!found && c->blueprint->name == name && (!bench.value("hoverHero", false)
							|| (client->unitInfo.count(c->id) && (client->unitInfo[c->id].flags & 9) == 1)))
							found = c->dyncast<ClientGameObject>();
						self(c->dyncast<ClientGameObject>(), self);
					}
			};
			if (ClientGameObject* own = client->clientPlayer)
				search(own, search);
			if (!found)
				search(client->getLevel(), search);
			if (found) {
				Camera& cam = client->camera;
				// "hoverPitch": <radians> looks down that steeply (-1.5: from above, past trees)
				if (bench.contains("hoverPitch"))
					cam.orientation.x = bench.value("hoverPitch", -0.9f);
				if (cam.direction.y < -0.01f) {
					const float groundY = client->terrain->getHeight(cam.position.x, cam.position.z);
					const Vector3 lookAt = cam.position + cam.direction * ((cam.position.y - groundY) / -cam.direction.y);
					cam.position.x += found->position.x - lookAt.x;
					cam.position.z += found->position.z - lookAt.z;
				}
				Vector3 centre = found->position;
				if (found->sceneEntity.model)
					centre = found->position + found->sceneEntity.model->getSphereCenter() * found->scale;
				// "hoverOffset": [x, y, z] metres from the model's centre (a model with a gap there)
				if (const nlohmann::json off = bench.value("hoverOffset", nlohmann::json()); off.is_array() && off.size() == 3)
					centre += Vector3(off[0].get<float>(), off[1].get<float>(), off[2].get<float>());
				const Vector3 ndc = centre.transformScreenCoords(cam.sceneMatrix);
				g_mouseX = (int)((ndc.x + 1.0f) * g_windowWidth / 2.0f);
				g_mouseY = (int)((-ndc.y + 1.0f) * g_windowHeight / 2.0f);
			}
		}
		if (const std::string name = bench.value("stampdown", std::string()); !name.empty() && !stampdownBlueprint && client->clientPlayer) {
			stampdownBlueprint = client->gameSet->findBlueprint(Tags::GAMEOBJCLASS_BUILDING, name);
			stampdownPlayer = client->clientPlayer;
			static bool said = false;
			if (!said) {
				said = true;
				printf("BENCH stampdown \"%s\": %s\n", name.c_str(), stampdownBlueprint ? "placing it" : "no such building blueprint");
				fflush(stdout);
			}
		}
	}
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
		if (fogOfWarOn())
			for (const auto& [id, copy] : fogOfWar.remembered)
				if (!fogOfWar.isBuildingSeen(copy->position))
					drawObject(copy.get(), true);
		// Mesh particles (wood planks, bricks, coins, shrapnel): their model at the particle,
		// spinning at the system's Rotations_Per_Second about an axis of its own, at its scale.
		// The particle renderer only draws sprites; these were blank squares before.
		if (particlesContainer) {
			const float now = client->timeManager.currentTime;
			for (auto& [key, list] : particlesContainer->particles)
				for (Particle& p : list) {
					PS_Particles& def = p.system->Particles;
					if (def.type != PSType::Mesh || def.Mesh.empty())
						continue;
					const float age = now - p.startTime;
					if (age < 0 || age > p.maxAge)
						continue;
					std::string path = def.Mesh;
					std::replace(path.begin(), path.end(), '/', '\\');
					const uint32_t seed = (uint32_t)(p.startTime * 7919.0f) ^ (uint32_t)(p.startPosition.x * 104729.0f);
					const float rps = def.Rotations_Per_Second[0] + (def.Rotations_Per_Second[1] - def.Rotations_Per_Second[0]) * ((seed & 255) / 255.0f);
					const float angle = age * rps * 6.2831853f;
					const auto [s1, s2, si] = def.Scale_Gradient.getAtTime(age, 1.0f);
					const float scale = s1 + (s2 - s1) * si;
					attachSceneEntities.emplace_back();
					SceneEntity& e = attachSceneEntities.back();
					e.model = client->gameSet->modelCache.getModel(path);
					e.transform = Matrix::getScaleMatrix(Vector3(scale, scale, scale)) * Matrix::getRotationXMatrix(angle + ((seed >> 8) & 255) * 0.0246f)
						* Matrix::getRotationYMatrix(angle * 0.7f + ((seed >> 16) & 255) * 0.0246f) * Matrix::getTranslationMatrix(p.getPosition(now));
					e.color = 0;
					e.animTime = 0;
					e.flags = 0;
					scene->add(&e);
				}
		}
		// Fire lights: one per fire (system and object), at its live particles' centre, its colour
		// their mean colour, stronger with more particles; the nearest to the camera are kept
		if (auto* dgfx = dynamic_cast<DiligentGfx*>(gfx); dgfx && particlesContainer && gpuBlobShadows) {
			std::vector<float>& lights = dgfx->shadows.fireLights;
			lights.clear();
			if (g_renderSettings.fireLights) {
				struct Fire { Vector3 sum; float colour[3] = {}, alpha = 0, lowest = 1e9f; int count = 0; };
				std::map<std::pair<void*, uint32_t>, Fire> fires;
				const float now = client->timeManager.currentTime;
				for (auto& [key, list] : particlesContainer->particles)
					for (Particle& p : list) {
						if (!p.system->emitsLight)
							continue;
						const float age = now - p.startTime;
						if (age < 0 || age > p.maxAge)
							continue;
						Fire& f = fires[{ key, p.objid }];
						const Vector3 at = p.getPosition(now);
						f.sum += at;
						f.lowest = std::min(f.lowest, at.y);
						const uint32_t c = p.getColor(now);
						const float a = (c >> 24) / 255.0f;
						for (int i = 0; i < 3; i++)
							f.colour[i] += ((c >> (16 - 8 * i)) & 255) / 255.0f * a;
						f.alpha += a;
						f.count++;
					}
				std::vector<std::pair<float, std::array<float, 7>>> list;
				for (auto& [key, f] : fires) {
					// at the fire's base (its particles rise), so that it lights the ground around it
					Vector3 at = f.sum / (float)f.count;
					at.y = f.lowest + 1.5f;
					// the fire's hue at full brightness (the sprites' alpha-weighted colour, warmed:
					// their middles are near white), as strong as there are visible sprites
					if (f.alpha <= 0)
						continue;
					float hue[3] = { f.colour[0] / f.alpha, f.colour[1] / f.alpha * 0.7f, f.colour[2] / f.alpha * 0.35f };
					const float peak = std::max({ hue[0], hue[1], hue[2], 1e-3f });
					const float strength = std::min(1.0f, f.alpha / 3.0f) * 1.5f;
					std::array<float, 7> l = { at.x, at.y, at.z, 12.0f + 12.0f * std::min(1.0f, f.count / 24.0f),
						hue[0] / peak * strength, hue[1] / peak * strength, hue[2] / peak * strength };
					list.push_back({ (at - client->camera.position).sqlen3(), l });
				}
				// at night (wkbai's day and night), the buildings' windows and torches, after the fires
				AddNightLights(client, client->camera.position, list);
				std::sort(list.begin(), list.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
				for (size_t i = 0; i < list.size() && i < (size_t)DiligentGfx::ShadowEnvironment::MAX_FIRE_LIGHTS; i++)
					lights.insert(lights.end(), list[i].second.begin(), list[i].second.end());
			}
		}
		// test runs: "benchmark": { ..., "particleTest": "<particle system>" } emits it 25 m in front
		// of the camera, to see a particle system on a screenshot
		static const std::string particleTest = g_settings.value("benchmark", nlohmann::json::object()).value("particleTest", std::string());
		if (!particleTest.empty() && particlesContainer && psCache && client->terrain) {
			Vector3 at = client->camera.position + Vector3(client->camera.direction.x, 0, client->camera.direction.z).normal() * 25.0f;
			at.y = client->terrain->getHeight(at.x, at.z) + 8.0f;
			particlesContainer->generate(psCache->getPS(particleTest), at, 0, client->timeManager.previousTime, client->timeManager.currentTime);
		}
		nextSelectedObject = nextSelectedObjectByMesh ? nextSelectedObjectByMesh : nextSelectedObjectBySphere;

		// highlight hovered object; not in test runs ("benchmark"): they start detached but still read
		// the real cursor, and a building under it was drawn unshaded (SEFLAG_NOLIGHTING), so test
		// shots came out at random without its light (GI Ultra's lamps, 2026-11-11)
		if (nextSelectedObject && !BenchmarkEnabled()) {
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
		// the charge bar's arc around the feet, beside the health ring (WKB.exe 0x64D88F):
		// Power<n>.tga, n = 1 + 8 x Charge Bar / Charge Bar Capacity (at most 8), shown while it is
		// above 0. A peasant's is what it carries (the scripts add each load, delivery empties
		// it), an archer's its arrows, a cart's its load.
		static texture powerTex[8];
		static int chargeBar = -1, chargeBarCapacity = -1;
		if (healthTexOwner != this) {
			healthTexOwner = this;
			for (int i = 0; i < 8; ++i) {
				std::string name = "Interface\\InGame\\health" + std::to_string(i + 1) + ".tga";
				healthTex[i] = uiTexCache.getTexture(name.c_str(), false);
				name = "Interface\\InGame\\Power" + std::to_string(i + 1) + ".tga";
				powerTex[i] = uiTexCache.getTexture(name.c_str(), false);
			}
			chargeBar = client->gameSet->items.names.getIndex("Charge Bar");
			chargeBarCapacity = client->gameSet->items.names.getIndex("Charge Bar Capacity");
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
					if (chargeBar >= 0 && chargeBarCapacity >= 0) {
						const float charge = obj->getItem(chargeBar), chargeCap = obj->getItem(chargeBarCapacity);
						if (chargeCap > 0.0f && charge > 0.0f && powerTex[0])
							terrainSpriteContainer->addSprite(powerTex[std::min(7, (int)(charge * 8.0f / chargeCap))], p1, p2, p3);
					}
				}
			}
		}
		highlightedObjects.clear();

		// The range circles of the selected objects (DISPLAY_SPRITE_WHEN_SELECTED, and those of the
		// objects that DISPLAY_SPRITE_WHEN_SELECTED_FOR finds), and later of the building being
		// placed: band textures on the ground, the value being the radius in metres
		rangeBands.clear();
		footprintTiles.clear();
		stampdownHidden.clear();
		stampdownRefusal.clear();
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
				// The preview shows the building as it will be when finished, as WKB.exe's interface
				// does (0x64df2b): the foundation's MAP_TYPE_TAG "Fully Built Version" (the type it is
				// converted to when built); only the foundation itself is placed
				static int fullyBuiltTag = -1;
				static const GameSet* tagOf = nullptr;
				if (tagOf != client->gameSet.get()) {
					tagOf = client->gameSet.get();
					fullyBuiltTag = tagOf->typeTags.names.getIndex("Fully Built Version");
				}
				auto finishedModel = [](const GameObjBlueprint* foundation) {
					if (auto it = foundation->mappedTypeTags.find(fullyBuiltTag); it != foundation->mappedTypeTags.end() && it->second) {
						try {
							return it->second->subtypes.at(0).appearances.at(0).animations.at(0).at(0);
						}
						catch (const std::out_of_range&) {
						}
					}
					return foundation->subtypes.at(0).appearances.at(0).animations.at(0).at(0);
				};
				test.model = finishedModel(stampdownBlueprint);
				// turned with [ and ]: the model, its footprint and its origin (WKB.exe 0x59a4d0 turns
				// the origin as the tiles), the city rectangle (StampdownPlan)
				const Vector3 orientation = stampdownOrientation();
				if (auto* fp = stampdownBlueprint->footprint) {
					const auto rotOrigin = fp->rotateOrigin(orientation.y);
					peapos.x = std::floor(peapos.x / 5.0f) * 5.0f + 2.5f + rotOrigin.first;
					peapos.z = std::floor(peapos.z / 5.0f) * 5.0f + 2.5f + rotOrigin.second;
					if (client->terrain)
						peapos.y = client->terrain->getHeightEx(peapos.x, peapos.z, stampdownBlueprint->canWalkOnWater());
				}
				test.transform = Matrix::getRotationYMatrix(-orientation.y) * Matrix::getTranslationMatrix(peapos);
				test.color = stampdownPlayer->color;
				scene->add(&test);

				static std::vector<SceneEntity> planEntities;
				planEntities.clear();
				auto plan = StampdownPlan::getStampdownPlan(client, client->clientPlayer.get(), stampdownBlueprint, peapos, orientation);
				for (const auto& verdict : plan.tiles)
					footprintTiles.push_back({ verdict.x, verdict.z, verdict.ok });
				// red where it can't be placed (the server refuses it too), green where it can, as in
				// the original
				test.flags |= plan.status != StampdownPlan::Status::Valid ? SceneEntity::SEFLAG_BLOCKED : SceneEntity::SEFLAG_PLACEABLE;
				// its range circles, the values those of the player ("run on player!" in the game set)
				{
					CliScriptContext ctx{ client, stampdownPlayer.get() };
					for (const auto& band : stampdownBlueprint->spritesWhenStampingDown)
						addRangeBand(band, peapos, &ctx);
				}
				// a farm's soil, as WKB.exe's interface shows it while a "Standard Farm Foundation" is
				// placed (0x64d480): the "Fertility" of the terrain under it (Terrains.cpp: 0.5 to
				// 1.5) gives TOOLTIP_ARID_FARM up to 0.5, TOOLTIP_FERTILE_FARM from 1.5, else
				// TOOLTIP_FARM; here with the value as a percentage too. Where it is refused, why,
				// in WKB.exe's words (BUILDING_PLACEMENT_TOO_STEEP and the others)
				for (const auto& removal : plan.toRemove)
					stampdownHidden.push_back(removal.object.objid);
				// the new ring's tiles green under its poles, as the original's
				for (const auto& creation : plan.toCreate)
					footprintTiles.push_back({ (int)(creation.position.x / 5.0f), (int)(creation.position.z / 5.0f), true });
				// Test runs: "benchmark": { ..., "rangeBand": [radius, "<texture>"] } a range band around
				// the building being placed (the projected decals' screenshots)
				if (BenchmarkEnabled()) {
					const nlohmann::json rb = g_settings.value("benchmark", nlohmann::json::object()).value("rangeBand", nlohmann::json());
					if (rb.is_array() && rb.size() == 2)
						if (texture tex = uiTexCache.getTexture(rb[1].get<std::string>().c_str(), false))
							rangeBands.push_back({ tex, peapos, rb[0].get<float>() });
				}
				if (const char* refusal = StampdownPlan::statusTextKey(plan.status)) {
					stampdownRefusal = lang.getText(refusal);
					drawStampdownRefusal();
				}
				else if (stampdownBlueprint->name == "Standard Farm Foundation" && client->terrain) {
					static int fertilityItem = -1;
					static const GameSet* itemsOf = nullptr;
					if (itemsOf != client->gameSet.get()) {
						itemsOf = client->gameSet.get();
						fertilityItem = itemsOf->items.names.getIndex("Fertility");
					}
					if (const auto* tile = client->terrain->getPlayableTile((int)(peapos.x / 5.0f), (int)(peapos.z / 5.0f)); tile && fertilityItem >= 0) {
						float fertility = 0.0f;
						auto& groups = client->gameSet->associatedTileTexGroups;
						if (auto it = groups.find(tile->texture->grp->name); it != groups.end())
							if (auto vt = it->second->itemValues.find(fertilityItem); vt != it->second->itemValues.end())
								fertility = vt->second;
						const char* key = fertility <= 0.5f ? "TOOLTIP_ARID_FARM" : fertility >= 1.5f ? "TOOLTIP_FERTILE_FARM" : "TOOLTIP_FARM";
						ImGui::SetTooltip("%s\nFertility %d%%", lang.getText(key).c_str(), (int)std::lround(fertility * 100.0f));
					}
				}
				planEntities.reserve(plan.toCreate.size());
				// the ring's new pieces as the original shows them (the user's screenshots of WKB.exe):
				// the banner poles of their foundations only, untinted, refused or not, over the
				// ring's green tiles; the poles are the foundation model's attachments (a stockade's
				// Stockade\Construction\Default.MESH3: Flags\CON_FLAP), the model itself (the wall's
				// shape, which rises as it is built) is left out
				for (auto& creation : plan.toCreate) {
					auto& entity = planEntities.emplace_back();
					entity.transform = Matrix::getRotationYMatrix(-creation.orientation.y) * Matrix::getTranslationMatrix(creation.position);
					entity.model = creation.blueprint->subtypes.at(0).appearances.at(0).animations.at(0).at(0);
					entity.color = stampdownPlayer->color;
				}
				for (auto& entity : planEntities)
					drawAttachmentPoints(&entity);
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
			// a test run's sun ("benchmark": { "sunVector": [x, y, z] }), for comparing with the original
			if (BenchmarkEnabled()) {
				const auto& bench = g_settings.value("benchmark", nlohmann::json());
				if (bench.contains("sunVector") && bench["sunVector"].is_array() && bench["sunVector"].size() == 3)
					client->terrain->sunVector = Vector3(bench["sunVector"][0].get<float>(), bench["sunVector"][1].get<float>(), bench["sunVector"][2].get<float>());
			}
			scene->sunDirection = client->terrain->sunVector;
			scene->sunColor = client->terrain->sunColor;
		}
		// wkbai's day and night, seasons and weather: the sun, fog and grade (none: the map's)
		UpdateEnvironmentView(client, scene);
		UpdateWaterfallSounds(client);
		if (auto* dgfx = dynamic_cast<DiligentGfx*>(gfx)) {
			dgfx->shadows.time = client->timeManager.currentTime;
			dgfx->shadows.cloudShadows = g_renderSettings.cloudShadows;
			// ray-traced shadows instead of the shadow map, where the GPU can
			dgfx->shadows.rayShadowsOn = g_renderSettings.staticShadows && g_renderSettings.rtShadows && dgfx->shadows.rayShadowsSupported;
			dgfx->shadows.staticShadows = g_renderSettings.staticShadows && !dgfx->shadows.rayShadowsOn;
			dgfx->shadows.rayReflectionsOn = g_renderSettings.rtReflections && g_renderSettings.waterQuality >= RenderSettings::WATER_LOW && dgfx->shadows.rayShadowsSupported;
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
		// the depth pre-pass (the ray-traced shadows or the ambient occlusion on): the models',
		// the ground's and the water's surfaces' depth into the lighting depth, the masks from it;
		// then the scene is drawn and shaded with them
		if (auto* dgfx = dynamic_cast<DiligentGfx*>(gfx); dgfx && dgfx->LightingPrepassWanted()) {
			auto* esr = dynamic_cast<DiligentEnhancedSceneRenderer*>(sceneRenderer);
			auto* etr = showTerrain ? dynamic_cast<DiligentEnhancedTerrainRenderer*>(terrainRenderer) : nullptr;
			if (esr) {
				PROFILE_SCOPE("Depth pre-pass");
				if (Diligent::ITextureView* dsv = dgfx->BeginLightingPrepass()) {
					// the models' frame prepared (shadow map, GI, ray-tracing structures), the ground's
					// and the water's depth, kept as the surface depth (the projected decals'), then
					// the models' depth
					esr->renderDepth(nullptr);
					if (etr) {
						if (bool* bump = etr->bumpMapping())
							*bump = g_renderSettings.bumpMapping;
						etr->renderDepth(dsv);
						etr->renderWaterDepth();
					}
					dgfx->SnapshotSurfaceDepth();
					esr->renderDepth(dsv);
					dgfx->FinishLightingPrepass(client->camera);
				}
			}
		}
		{
			ZoneScopedN("Scene rendering");
			PROFILE_SCOPE("Scene");
			sceneRenderer->render();
		}
		scene->clear();
	}
	else {
		TracyCZoneEnd(zoneScene);
	}

	if (terrainRenderer && showTerrain) {
		ZoneScopedN("Terrain rendering");
		PROFILE_SCOPE("Terrain");
		if (bool* bump = terrainRenderer->bumpMapping())
			*bump = g_renderSettings.bumpMapping;
		terrainRenderer->render();
	}

	if (terrainSpriteRenderer && showTerrain) {
		ZoneScopedN("Terrain sprites");
		PROFILE_SCOPE("Terrain sprites");
		terrainSpriteRenderer->render();
	}
	if (terrainSpriteContainer)
		terrainSpriteContainer->clear();
	if (showTerrain) {
		drawRangeBands();
		drawFootprintTiles();
		// wkbai's weather: flood water over the land, footprints in snow and mud
		PROFILE_SCOPE("Ground weather");
		DrawGroundWeather(gfx, client, client->camera);
	}

	if (particleRenderer && particlesContainer) {
		ZoneScopedN("Particles");
		PROFILE_SCOPE("Particles and weather");
		// soft: faded where they meet the ground, the water and the models (the depth pre-pass)
		auto* dgfx = dynamic_cast<DiligentGfx*>(gfx);
		if (dgfx)
			dgfx->SetSoftParticles(true);
		gfx->BeginParticles();
		gfx->BeginBatchDrawing();
		particleRenderer->render(client->timeManager.previousTime, client->timeManager.currentTime, client->camera);
		particlesContainer->update(client->timeManager.currentTime);
		DrawWeather(gfx, client, client->camera);
		if (dgfx) {
			dgfx->SetSoftParticles(false);
			gfx->BeginParticles(); // (the state without them)
		}
	}

	if (postGfx) {
		if (fogOn && !fogOfWar.tiles.empty())
			postGfx->SetFogOfWar(fogOfWar.tiles.data(), fogOfWar.width, fogOfWar.height, fogOfWar.version,
				client->camera.viewMatrix.getInverse4x3(), g_renderSettings.fogOfWarBrightness);
		PROFILE_SCOPE("Post-processing");
		postGfx->ApplyPostProcessing(client->camera);
	}

	if (selBoxOn) {
		gfx->InitRectDrawing();
		gfx->NoTexture(0);
		gfx->DrawFrame(selBoxStartX, selBoxStartY, selBoxEndX - selBoxStartX, selBoxEndY - selBoxStartY, -1);
	}

	drawLoadingScreen();
	if (loadingDone) {
		drawChronicle();
	}

	// Test runs: "benchmark": { ..., "drawCursor": true } draws the cursor into the frame (the
	// screenshots leave out the system's cursor)
	if (BenchmarkEnabled() && currentCursor && g_settings.value("benchmark", nlohmann::json::object()).value("drawCursor", false)) {
		int cw, ch;
		WndCursorSize(currentCursor, cw, ch);
		const ImVec2 at((float)UiMouseX(), (float)UiMouseY());
		ImGui::GetForegroundDrawList()->AddImage(uiTexCache.getTexture(WndCursorPath(currentCursor), false), at,
			ImVec2(at.x + cw / g_uiScale, at.y + ch / g_uiScale));
	}
	{
		ZoneScopedN("ImGui rendering");
		PROFILE_SCOPE("UI");
		gfx->InitImGuiDrawing();
		ImGuiImpl_Render(gfx);
	}
	{
		PROFILE_SCOPE("Unit portrait");
		drawSelectionPortrait();
	}
	// Test runs: "benchmark": { ..., "loadingScreenshot": path } once the loading screen is at stage 4
	// (a guest: once the host has sent the rows of the players)
	const bool rowsIn = client->localServer || client->loadingRows.size() > 1;
	loadingScreenshots();
	if (BenchmarkEnabled() && !loadingDone && loadingStage >= 4 && rowsIn
		&& !g_settings.value("benchmark", nlohmann::json::object()).contains("loadingScreenshots")) {
		static bool shot = false;
		const std::string path = g_settings.value("benchmark", nlohmann::json::object()).value("loadingScreenshot", std::string());
		if (!shot && !path.empty()) {
			shot = true;
			printf("Loading screenshot %s: %s\n", path.c_str(), gfx->SaveScreenshot(path.c_str()) ? "saved" : "failed");
		}
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
		const float every = g_settings.value("benchmark", nlohmann::json::object()).value("census", 0.0f);
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
		const std::string traceOrder = g_settings.value("benchmark", nlohmann::json::object()).value("traceOrder", std::string());
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
	LoadingSetInFrame(false);
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

namespace {
	// A game text: "\n" a new line, "\c" (the advisor's lines) dropped, other escapes as the
	// character after the backslash
	std::string GameText(const std::string& text)
	{
		std::string out;
		out.reserve(text.size());
		for (size_t i = 0; i < text.size(); i++) {
			if (text[i] != '\\') {
				out.push_back(text[i]);
				continue;
			}
			if (i + 1 >= text.size())
				break;
			const char c = text[++i];
			if (c == 'n')
				out.push_back('\n');
			else if (c != 'c')
				out.push_back(c);
		}
		return out;
	}
}

namespace {
	// a player as the windows name it: an AI General by its blueprint ("Saint Jane Kingmaker" ->
	// "Saint Jane"), a human by its name
	std::string DiplomacyPlayerName(const CommonGameObject* p)
	{
		const std::string& general = p->blueprint->name;
		const bool isGeneral = general.size() > 10 && general.compare(general.size() - 10, 10, " Kingmaker") == 0;
		if (isGeneral)
			return general.substr(0, general.size() - 10);
		if (!p->name.empty())
			return p->name;
		return "Player " + std::to_string(p->id);
	}
}

// An AI General's message (MAP_CHAT_MESSAGE's text): %d an amount, %r a resource (1 Food, 2
// Materials, 3 Gold: WKB.exe 0x6afbc0), each taking the next value; an unknown one, or one past
// the values, gives nothing (the original hung there)
std::string ClientInterface::chatDisplayText(const std::string& key, const std::vector<float>& values)
{
	if (!languageLoaded) {
		lang.load("Languages\\Language.txt");
		languageLoaded = true;
	}
	const std::string text = lang.getText(key);
	std::string out;
	size_t next = 0;
	for (size_t i = 0; i < text.size(); i++) {
		if (text[i] != '%' || i + 1 >= text.size()) {
			out.push_back(text[i]);
			continue;
		}
		const char c = text[++i];
		const bool have = next < values.size();
		const float v = have ? values[next] : 0.0f;
		next++;
		if (!have)
			continue;
		if (c == 'd')
			out += std::to_string((int)v);
		else if (c == 'r') {
			static const char* const keys[] = { "RESOURCE_FOOD", "RESOURCE_WOOD", "RESOURCE_GOLD" };
			const int r = (int)v;
			if (r >= 1 && r <= 3)
				out += lang.getText(keys[r - 1]);
		}
	}
	return out;
}

void ClientInterface::updateGameWindows(FrontEnd* frontEnd)
{
	// the AI Generals' messages, one at a time, in the game set's "AI General Chat Window"
	// (WKB.exe 0x531134: the sender's name as the title, then the text)
	const int chatWindow = client->gameSet->gameTextWindows.names.getIndex("AI General Chat Window");
	if (chatWindow < 0)
		client->chatDisplays.clear();
	else if (!client->chatDisplays.empty()) {
		auto st = client->gtwStates.find(chatWindow);
		if (st == client->gtwStates.end() || st->second < 0) {
			client->gtwOrder[chatWindow] = ++client->gtwSerial;
			client->gtwStates[chatWindow] = 0;
		}
	}
	std::vector<std::pair<uint32_t, FrontEnd::GameWindow>> shown;
	for (const auto& [index, page] : client->gtwStates) {
		if (page < 0 || index < 0 || index >= (int)client->gameSet->gameTextWindows.size())
			continue;
		const GameTextWindow& gtw = client->gameSet->gameTextWindows[index];
		if (page >= (int)gtw.pages.size())
			continue;
		FrontEnd::GameWindow w;
		w.id = index;
		w.text = GameText(lang.getText(gtw.pages[page].textBody));
		w.page = page;
		w.pages = (int)gtw.pages.size();
		w.x = gtw.x;
		w.y = gtw.y;
		w.width = gtw.width;
		w.atBottom = gtw.atBottom;
		w.modal = gtw.modal;
		if (index == chatWindow && !client->chatDisplays.empty()) {
			const Client::ChatDisplay& chat = client->chatDisplays.front();
			w.text = GameText(chatDisplayText(chat.key, chat.values));
			if (CommonGameObject* from = client->findObject(chat.from)) {
				w.title = DiplomacyPlayerName(from);
				w.titleColour = from->color;
				w.general = from->blueprint->name;
			}
		}
		const bool lastPage = page + 1 >= (int)gtw.pages.size();
		for (const GameTextWindow::Button& b : gtw.buttons) {
			FrontEnd::GameWindow::Button button;
			button.text = GameText(lang.getText(b.text));
			button.x = b.x;
			button.y = b.y;
			button.width = b.width;
			button.height = b.height;
			// greyed where it would do nothing: Back on the first page, Next on the last, and
			// with FORCE_READ_ALL_PAGES closing before the last page has been reached
			switch (b.onClickWindowAction) {
			case Tags::GTW_BUTTON_WINDOW_ACTION_MOVE_PREVIOUS_PAGE:
			case Tags::GTW_BUTTON_WINDOW_ACTION_MOVE_FIRST_PAGE:
				button.enabled = page > 0 || !b.onClickSequence.actionList.empty();
				break;
			case Tags::GTW_BUTTON_WINDOW_ACTION_MOVE_NEXT_PAGE:
				button.enabled = !lastPage || !b.onClickSequence.actionList.empty();
				break;
			case Tags::GTW_BUTTON_WINDOW_ACTION_CLOSE_WINDOW:
				button.enabled = !gtw.forceReadAllPages || lastPage;
				break;
			}
			w.buttons.push_back(std::move(button));
		}
		const auto order = client->gtwOrder.find(index);
		shown.emplace_back(order != client->gtwOrder.end() ? order->second : 0, std::move(w));
	}
	std::stable_sort(shown.begin(), shown.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
	std::vector<FrontEnd::GameWindow> windows;
	for (auto& [order, w] : shown)
		windows.push_back(std::move(w));
	frontEnd->setGameWindows(std::move(windows));

	FrontEnd::Objectives objectives;
	if (client->missionObjectives >= 0 && client->missionObjectives < (int)client->gameSet->missionObjectives.size()) {
		const MissionObjectives& mo = client->gameSet->missionObjectives[client->missionObjectives];
		objectives.title = GameText(lang.getText(mo.missionName.empty() ? client->gameSet->missionObjectives.getString(client->missionObjectives) : mo.missionName));
		for (const auto& [key, state] : client->objectiveEntries)
			if (state != MissionObjectives::HIDDEN)
				objectives.entries.push_back({ GameText(lang.getText(key)), state == MissionObjectives::INACTIVE });
		objectives.changed = client->objectivesChanged;
	}
	frontEnd->setObjectives(std::move(objectives));
	if (toggleObjectivesRequested) {
		toggleObjectivesRequested = false;
		frontEnd->toggleObjectives();
	}
	frontEnd->setDiplomacy(diplomacyView());
	if (toggleDiplomacyRequested >= 0) {
		frontEnd->toggleDiplomacyWindow(toggleDiplomacyRequested);
		toggleDiplomacyRequested = -1;
	}
}

// What the top bar's Diplomacy, Global Diplomacy and Tributes windows show (FrontEnd::Diplomacy)
FrontEnd::Diplomacy ClientInterface::diplomacyView()
{
	FrontEnd::Diplomacy d;
	const GameSet* gs = client->gameSet.get();
	CommonGameObject* me = client->clientPlayer.get();
	CommonGameObject* level = client->getLevel();
	if (!gs || !me || !level)
		return d;
	for (size_t i = 0; i < gs->diplomaticStatuses.names.size(); i++)
		d.statuses.push_back(gs->diplomaticStatuses.names.getString((int)i));
	// the players still playing, this client's first (not the map's neutral and Baresark players)
	std::vector<CommonGameObject*> players{ me };
	for (const auto& [type, children] : level->children)
		for (CommonGameObject* p : children) {
			if (p == me || p->blueprint->bpClass != Tags::GAMEOBJCLASS_PLAYER || p->blueprint->name == "Neutral Player"
				|| p->blueprint->name == "Baresark Player" || (p->flags & ClientGameObject::fTerminated) || client->finishedPlayers.count(p->id))
				continue;
			players.push_back(p);
		}
	std::sort(players.begin() + 1, players.end(), [](auto* a, auto* b) { return a->id < b->id; });
	auto offer = [&](CommonGameObject* from, CommonGameObject* to) {
		auto it = client->diplomaticOffers.find({ from->id, to->id });
		return it != client->diplomaticOffers.end() ? it->second : -1;
	};
	for (CommonGameObject* p : players) {
		FrontEnd::Diplomacy::Player row;
		row.id = p->id;
		row.name = DiplomacyPlayerName(p);
		row.general = p->blueprint->name;
		row.colour = p->color;
		if (p != me) {
			row.status = client->getDiplomaticStatus(me, p);
			row.myOffer = offer(me, p);
			row.theirOffer = offer(p, me);
		}
		d.players.push_back(std::move(row));
	}
	const size_t n = players.size();
	d.relations.assign(n * n, -1);
	for (size_t i = 0; i < n; i++)
		for (size_t j = 0; j < n; j++)
			if (i != j)
				d.relations[i * n + j] = client->getDiplomaticStatus(players[i], players[j]);
	for (int item : gs->tributeItems)
		d.tributes.push_back({ item, gs->items.names.getString(item), me->getItem(item) });
	auto itemOf = [&](CommonGameObject* obj, const char* name) {
		const int item = gs->items.names.getIndex(name);
		return item >= 0 ? obj->getItem(item) : 0.0f;
	};
	const bool diplomacyOn = itemOf(me, "Diplomacy Enabled") != 0.0f;
	d.canChange = itemOf(level, "Disallow Diplomacy") == 0.0f;
	// the Diplomacy button: with the lobby's Allow Diplomacy (or as a script sets it); Global
	// Diplomacy: a Tavern's script; Tributes: a Trading Post's, between players who may change
	// their relations or team mates (WKB.exe's client 0xb5)
	static const char* const tooltipOn[3] = { "INGAME_DIPLOMACY", "INGAME_DIPLOMATIC_REPORT", "INGAME_TRIBUTES" };
	static const char* const tooltipOff[3] = { "INGAME_DIPLOMACY_UNAVAILABLE_SINGLE", "INGAME_DIPLOMATIC_REPORT_UNAVAILABLE_SINGLE", "INGAME_TRIBUTES_UNAVAILABLE_SINGLE" };
	for (int i = 0; i < 3; i++) {
		const Client::DiplomacyButton& b = client->diplomacyButtons[i];
		bool on = b.set ? b.enabled : (i == 0 && diplomacyOn);
		if (i == 2)
			on = on && (diplomacyOn || itemOf(level, "Teams Selected") != 0.0f);
		d.enabled[i] = on && n > 1;
		d.news[i] = b.news && d.enabled[i];
		d.tooltips[i] = GameText(lang.getText(!b.tooltip.empty() ? b.tooltip : (d.enabled[i] ? tooltipOn[i] : tooltipOff[i])));
	}
	// the notices, 10 seconds each
	const uint32_t now = (uint32_t)SDL_GetTicks();
	auto& notices = client->notices;
	notices.erase(std::remove_if(notices.begin(), notices.end(), [now](const Client::Notice& x) { return now - x.time > 10000; }), notices.end());
	for (const Client::Notice& x : notices) {
		CommonGameObject* p = client->findObject(x.player);
		d.notices.push_back({ GameText(lang.getText(x.key)), p ? p->color : 0, (now - x.time) / 1000.0f });
	}
	return d;
}

// The diplomacy windows' requests (FrontEndUI.lua): "<player id>,<status>" and
// "<player id>,<item>,<amount>"; "diplomacy_seen" "<window>" (0-2)
void ClientInterface::diplomacyRequest(const std::string& action, const std::string& arg)
{
	if (action == "diplomacy_seen") {
		const int window = atoi(arg.c_str());
		if (window >= 0 && window < 3)
			client->diplomacyButtons[window].news = false;
		return;
	}
	const uint32_t player = (uint32_t)strtoul(arg.c_str(), nullptr, 10);
	const size_t comma = arg.find(',');
	if (!player || comma == std::string::npos)
		return;
	if (action == "diplomacy_status")
		client->sendDiplomaticStatus(player, atoi(arg.c_str() + comma + 1));
	else if (action == "diplomacy_offer")
		client->sendDiplomaticOffer(player, atoi(arg.c_str() + comma + 1));
	else if (action == "tribute") {
		const size_t comma2 = arg.find(',', comma + 1);
		if (comma2 != std::string::npos)
			client->sendTribute(player, atoi(arg.c_str() + comma + 1), (float)atof(arg.c_str() + comma2 + 1));
	}
}

void ClientInterface::gameWindowButton(int gtwIndex, int buttonIndex)
{
	auto it = client->gtwStates.find(gtwIndex);
	if (it == client->gtwStates.end() || it->second < 0 || gtwIndex >= (int)client->gameSet->gameTextWindows.size())
		return;
	const GameTextWindow& gtw = client->gameSet->gameTextWindows[gtwIndex];
	if (buttonIndex < 0 || buttonIndex >= (int)gtw.buttons.size())
		return;
	const GameTextWindow::Button& button = gtw.buttons[buttonIndex];
	const int page = it->second, pages = (int)gtw.pages.size();
	switch (button.onClickWindowAction) {
	case Tags::GTW_BUTTON_WINDOW_ACTION_CLOSE_WINDOW:
		if (gtw.forceReadAllPages && page + 1 < pages)
			return;
		it->second = -1;
		// the AI General's message read: the next one, if any, shows
		if (gtwIndex == client->gameSet->gameTextWindows.names.getIndex("AI General Chat Window") && !client->chatDisplays.empty())
			client->chatDisplays.pop_front();
		break;
	case Tags::GTW_BUTTON_WINDOW_ACTION_MOVE_FIRST_PAGE:
		if (page != 0)
			client->setGameTextWindowPage(gtwIndex, 0);
		break;
	case Tags::GTW_BUTTON_WINDOW_ACTION_MOVE_NEXT_PAGE:
		if (page + 1 < pages)
			client->setGameTextWindowPage(gtwIndex, page + 1);
		break;
	case Tags::GTW_BUTTON_WINDOW_ACTION_MOVE_PREVIOUS_PAGE:
		if (page > 0)
			client->setGameTextWindowPage(gtwIndex, page - 1);
		break;
	}
	if (!button.onClickSequence.actionList.empty())
		client->sendGameTextWindowButtonClicked(gtwIndex, buttonIndex);
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
