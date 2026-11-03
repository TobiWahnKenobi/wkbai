// wkbai - the client's day and night, seasons and weather: the sun and the scene's grade from
// the server's state, the HUD's readout, the rain, snow and lightning drawn (Environment.h)
// Licensed under the GNU General Public License 3

#include "EnvironmentView.h"
#include "../client.h"
#include "../terrain.h"
#include "../scene.h"
#include "../tags.h"
#include "../gameset/GameObjBlueprint.h"
#include "../Camera.h"
#include "../gfx/renderer.h"
#include "../imgui/imgui.h"
#include <SDL3/SDL_timer.h>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <memory>

namespace {
	// the map's own sun and fog, put back when the environment goes
	struct MapLook { const Terrain* terrain = nullptr; Vector3 sun; uint32_t sunColor = 0, fogColor = 0; bool changed = false; };
	MapLook g_mapLook;
	uint32_t g_lightningSeen = 0;
	bool g_lightningKnown = false;
	uint64_t g_flashStart = 0;
	Vector3 g_boltPos;
	uint32_t g_boltSeed = 0;

	uint32_t Scale(uint32_t colour, const float* by) {
		uint32_t out = 0;
		for (int c = 0; c < 3; c++) {
			const float v = ((colour >> (16 - 8 * c)) & 255) * by[c];
			out |= (uint32_t)std::clamp((int)std::lround(v), 0, 255) << (16 - 8 * c);
		}
		return out;
	}
	float Hash(uint32_t n) {
		n = (n << 13) ^ n;
		n = n * (n * n * 15731u + 789221u) + 1376312589u;
		return (float)(n & 0x7fffffffu) / (float)0x7fffffff;
	}
}

void UpdateEnvironmentView(Client* client, Scene* scene)
{
	Terrain* terrain = client->terrain;
	if (g_mapLook.terrain != terrain) {
		g_mapLook = MapLook();
		g_mapLook.terrain = terrain;
		if (terrain) {
			g_mapLook.sun = terrain->sunVector;
			g_mapLook.sunColor = terrain->sunColor;
			g_mapLook.fogColor = terrain->fogColor;
		}
		g_lightningKnown = false;
	}
	if (!client->environmentActive || !terrain) {
		if (g_mapLook.changed && terrain) {
			terrain->sunVector = g_mapLook.sun;
			terrain->sunColor = g_mapLook.sunColor;
			terrain->fogColor = g_mapLook.fogColor;
			g_mapLook.changed = false;
		}
		g_environment = EnvironmentView();
		return;
	}
	const EnvironmentSettings& es = client->environmentSettings;
	const EnvironmentState& st = client->environment;
	EnvironmentView& v = g_environment;
	v.active = true;
	// the clock from the client's own time (smooth between the server's updates)
	const float t = client->timeManager.currentTime;
	v.dayFraction = EnvDayFraction(es, t);
	v.yearFraction = EnvYearFraction(es, t);
	// the rest eased towards the server's state (it comes once a second)
	auto ease = [](float& x, float target) { x += (target - x) * 0.05f; };
	if (!g_lightningKnown) {
		v.cloudCover = st.cloudCover; v.rain = st.rain; v.snowfall = st.snowfall; v.fog = st.fog;
		v.wetness = st.wetness; v.snowCover = st.snowCover; v.ice = st.ice; v.waterRise = st.waterRise;
	}
	ease(v.cloudCover, st.cloudCover);
	ease(v.rain, st.rain);
	ease(v.snowfall, st.snowfall);
	ease(v.fog, st.fog);
	ease(v.wetness, st.wetness);
	ease(v.snowCover, st.snowCover);
	ease(v.ice, st.ice);
	ease(v.waterRise, st.waterRise);
	v.temperatureC = st.temperatureC;
	v.sun = ComputeSun(v.dayFraction, v.yearFraction, es.seasons, v.cloudCover);
	// the sun's frame: south is where the map's own sun stands (its art was lit from there)
	Vector3 south = Vector3(g_mapLook.sun.x, 0.0f, g_mapLook.sun.z);
	south = south.sqlen3() > 1e-6f ? south.normal() : Vector3(0, 0, 1);
	const Vector3 east(south.z, 0.0f, -south.x);
	const Vector3& d = v.sun.direction; // (east, up, south)
	terrain->sunVector = (east * d.x + Vector3(0, 1, 0) * d.y + south * d.z).normal();
	terrain->sunColor = Scale(g_mapLook.sunColor, v.sun.colour);
	{
		// the fog: the map's, in the sky's light, greyer in fog and under clouds
		float grey[3] = { 0.62f, 0.64f, 0.68f };
		const float toGrey = std::clamp(v.fog * 0.8f + v.cloudCover * 0.35f + std::max(v.rain, v.snowfall) * 0.3f, 0.0f, 1.0f);
		uint32_t fog = 0;
		for (int c = 0; c < 3; c++) {
			const float map = ((g_mapLook.fogColor >> (16 - 8 * c)) & 255) / 255.0f;
			const float value = (map + (grey[c] - map) * toGrey) * v.sun.ambient[c];
			fog |= (uint32_t)std::clamp((int)std::lround(value * 255.0f), 0, 255) << (16 - 8 * c);
		}
		terrain->fogColor = fog | (g_mapLook.fogColor & 0xFF000000u);
	}
	g_mapLook.changed = true;
	// lightning: a flash and a bolt for each new strike (none for the ones before this client knew)
	if (!g_lightningKnown || st.lightningCount < g_lightningSeen) {
		g_lightningSeen = st.lightningCount;
		g_lightningKnown = true;
	}
	else if (st.lightningCount != g_lightningSeen) {
		g_lightningSeen = st.lightningCount;
		g_flashStart = SDL_GetTicks();
		g_boltPos = st.lightningPos;
		g_boltSeed = st.lightningCount * 7919u;
	}
	const float since = (SDL_GetTicks() - g_flashStart) / 1000.0f;
	v.lightningFlash = g_flashStart && since < 0.35f ? (since < 0.08f ? 1.0f : since < 0.16f ? 0.3f : since < 0.22f ? 0.8f : 1.0f - since / 0.35f) : 0.0f;
	v.lightningPos = g_boltPos;
	// the grade: darker and paler at night and under heavy cloud, brighter in a flash
	v.exposure = (0.42f + 0.58f * v.sun.daylight) * (1.0f - 0.18f * v.cloudCover) * (1.0f + 1.2f * v.lightningFlash);
	v.saturation = (0.50f + 0.50f * v.sun.daylight) * (1.0f - 0.2f * v.fog);
	v.fogDistance = std::clamp(1.0f - 0.8f * v.fog - 0.3f * std::max(v.rain, v.snowfall * 1.3f), 0.15f, 1.0f);
	if (scene) {
		scene->sunDirection = terrain->sunVector;
		scene->sunColor = terrain->sunColor;
	}
}

void DrawEnvironmentHud(Client* client, float screenWidth)
{
	if (!client->environmentActive || !g_environment.active)
		return;
	const EnvironmentSettings& es = client->environmentSettings;
	const EnvironmentState& st = client->environment;
	char text[160];
	int n = 0;
	if (es.dayNight) {
		const int minutes = (int)(g_environment.dayFraction * 1440.0f) % 1440;
		n += snprintf(text + n, sizeof(text) - n, "%02d:%02d", minutes / 60, minutes % 60);
	}
	if (es.seasons) {
		static const char* severity[3] = { "gentle", "normal", "harsh" };
		n += snprintf(text + n, sizeof(text) - n, "%s%s (%s)   %s   %s", n ? "   " : "", SeasonName(st.season), severity[std::clamp(st.severity, 0, 2)],
			WeatherName(st.weather), FormatTemperature(st.temperatureC, es.tempScale).c_str());
		if (st.waterRise > 0.3f)
			n += snprintf(text + n, sizeof(text) - n, "   flood %+.1f m", st.waterRise);
		else if (st.waterRise < -0.3f)
			n += snprintf(text + n, sizeof(text) - n, "   low water %.1f m", st.waterRise);
		if (st.ice > 0.6f)
			n += snprintf(text + n, sizeof(text) - n, "   frozen");
	}
	// under the top bar, centred; larger on larger screens
	ImDrawList* dl = ImGui::GetBackgroundDrawList();
	ImFont* font = ImGui::GetFont();
	const float px = std::max(16.0f, screenWidth / 1920.0f * 22.0f);
	const ImVec2 size = font->CalcTextSizeA(px, FLT_MAX, 0.0f, text);
	const ImVec2 pos(screenWidth * 0.5f - size.x * 0.5f, screenWidth / 1920.0f * 40.0f);
	dl->AddRectFilled(ImVec2(pos.x - 10, pos.y - 4), ImVec2(pos.x + size.x + 10, pos.y + size.y + 4), IM_COL32(0, 0, 0, 150), 5.0f);
	dl->AddText(font, px, pos, IM_COL32(240, 232, 200, 255), text);
}

void DrawWeather(IRenderer* gfx, Client* client, const Camera& camera)
{
	const EnvironmentView& v = g_environment;
	if (!v.active || !client->terrain)
		return;
	const bool raining = v.rain > 0.02f, snowing = v.snowfall > 0.02f, bolt = v.lightningFlash > 0.0f;
	if (!raining && !snowing && !bolt)
		return;
	static std::unique_ptr<RBatch> batch;
	if (!batch)
		batch.reset(gfx->CreateBatch(4 * 1024, 6 * 1024));
	gfx->NoTexture(0);
	gfx->SetTransformMatrix(&camera.sceneMatrix);
	const float time = SDL_GetTicks() / 1000.0f;
	// the box of falling drops: around the point the camera looks at, its grid fixed in the world
	const Vector3 dir = camera.direction.normal();
	const float reach = std::clamp(dir.y < -0.05f ? (camera.position.y - client->terrain->getHeight(camera.position.x, camera.position.z)) / -dir.y : 60.0f, 20.0f, 90.0f);
	const Vector3 centre = camera.position + dir * (reach * 0.6f);
	const float box = 70.0f, height = 40.0f;
	const Vector3 camside = camera.direction.cross(Vector3(0, 1, 0)).normal();
	auto quad = [&](const Vector3& a, const Vector3& b, const Vector3& c, const Vector3& d, uint32_t colour) {
		batchVertex* verts; uint16_t* indices; uint32_t first;
		batch->next(4, 6, &verts, &indices, &first);
		const Vector3* p[4] = { &a, &b, &c, &d };
		for (int i = 0; i < 4; i++) {
			verts[i].x = p[i]->x; verts[i].y = p[i]->y; verts[i].z = p[i]->z;
			verts[i].color = colour; verts[i].u = (i == 1 || i == 2) ? 1.0f : 0.0f; verts[i].v = (i >= 2) ? 1.0f : 0.0f;
		}
		const uint16_t order[6] = { 0, 3, 1, 1, 3, 2 };
		for (int i = 0; i < 6; i++)
			indices[i] = (uint16_t)(first + order[i]);
	};
	auto wrap = [](float x, float origin, float size) { return origin + x - std::floor((x) / size) * size; };
	auto place = [&](int i, float fallSpeed, float sway, Vector3& out) {
		const float hx = Hash(i * 3 + 1) * box, hz = Hash(i * 3 + 2) * box, hy = Hash(i * 3 + 3);
		const float fall = std::fmod(hy * height + time * fallSpeed, height);
		const float x = hx + v.wind.x * time - (centre.x - box * 0.5f);
		const float z = hz + v.wind.z * time - (centre.z - box * 0.5f);
		out.x = wrap(x, centre.x - box * 0.5f, box) + std::sin(time * 1.3f + i) * sway;
		out.z = wrap(z, centre.z - box * 0.5f, box) + std::cos(time * 1.1f + i * 0.7f) * sway;
		const float ground = client->terrain->getHeight(out.x, out.z);
		out.y = ground + height - fall;
		return out.y > ground;
	};
	if (raining) {
		const int drops = (int)(2600 * std::min(1.0f, v.rain));
		const Vector3 vel = Vector3(v.wind.x, -14.0f, v.wind.z);
		const Vector3 along = vel.normal() * 1.1f;
		const uint32_t colour = (uint32_t)(70 + 60 * v.sun.daylight) << 24 | 0xB4BED2;
		for (int i = 0; i < drops; i++) {
			Vector3 p;
			if (!place(i, 14.0f, 0.0f, p))
				continue;
			Vector3 across = along.cross(camera.direction);
			across = across.sqlen3() > 1e-8f ? across.normal() * 0.025f : camside * 0.025f;
			quad(p + across, p - across, p - along - across, p - along + across, colour);
		}
	}
	if (snowing) {
		const int flakes = (int)(2200 * std::min(1.0f, v.snowfall));
		const Vector3 up = camside.cross(camera.direction).normal();
		const uint32_t colour = (uint32_t)(150 + 80 * v.sun.daylight) << 24 | 0xF2F4FA;
		for (int i = 0; i < flakes; i++) {
			Vector3 p;
			if (!place(i + 50000, 1.4f, 0.6f, p))
				continue;
			const float s = 0.06f + 0.05f * Hash(i + 777);
			quad(p + (camside + up) * s, p + (camside - up) * s, p + (-camside - up) * s, p + (-camside + up) * s, colour);
		}
	}
	if (bolt) {
		// a jagged bolt from the clouds to the strike, a little wider near the top
		const uint32_t colour = (uint32_t)(255 * std::min(1.0f, v.lightningFlash * 1.5f)) << 24 | 0xE8EEFF;
		Vector3 prev = v.lightningPos + Vector3((Hash(g_boltSeed) - 0.5f) * 30.0f, 90.0f, (Hash(g_boltSeed + 1) - 0.5f) * 30.0f);
		for (int s = 1; s <= 14; s++) {
			const float f = s / 14.0f;
			Vector3 next = prev + (v.lightningPos - prev) * (1.0f / (15 - s));
			if (s < 14)
				next += Vector3((Hash(g_boltSeed + s * 2) - 0.5f) * 6.0f, 0.0f, (Hash(g_boltSeed + s * 2 + 1) - 0.5f) * 6.0f);
			Vector3 across = (next - prev).cross(camera.direction);
			across = across.sqlen3() > 1e-8f ? across.normal() * (0.6f - 0.35f * f) : camside * 0.4f;
			quad(prev + across, prev - across, next - across, next + across, colour);
			prev = next;
		}
	}
	batch->flush();
}

void AddNightLights(Client* client, const Vector3& camera, std::vector<std::pair<float, std::array<float, 7>>>& list)
{
	const EnvironmentView& v = g_environment;
	const float dark = 1.0f - v.sun.daylight;
	if (!v.active || dark < 0.05f)
		return;
	for (const auto& [id, obj] : client->idmap) {
		if (!obj || obj->blueprint->bpClass != Tags::GAMEOBJCLASS_BUILDING || (obj->flags & CommonGameObject::fTerminated))
			continue;
		const float d2 = (obj->position - camera).sqlen3();
		if (d2 > 220.0f * 220.0f)
			continue;
		const std::string& name = obj->blueprint->name;
		if (name.size() >= 10 && name.compare(name.size() - 10, 10, "Foundation") == 0)
			continue;
		// walls and palisades: no windows
		if (name.find("Stockade") != std::string::npos || name.find("Wall") != std::string::npos || name.find("Paling") != std::string::npos
			|| name.find("Gate") != std::string::npos || name.find("Tower") != std::string::npos || name.find("Farm") != std::string::npos)
			continue;
		const float s = 1.4f * dark;
		// a little flicker, steady per building
		const float flicker = 0.9f + 0.1f * std::sin(SDL_GetTicks() / 1000.0f * 3.1f + id * 1.7f);
		std::array<float, 7> l = { obj->position.x, obj->position.y + 5.0f, obj->position.z, 30.0f,
			1.0f * s * flicker, 0.68f * s * flicker, 0.34f * s * flicker };
		list.push_back({ 1e7f + d2, l });
	}
}
