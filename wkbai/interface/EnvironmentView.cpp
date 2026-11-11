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
#include "../gfx/bitmap.h"
#include "../gfx/RenderSettings.h"
#include "../gfx/DiligentWaterfalls.h"
#include "../SoundPlayer.h"
#include "../imgui/imgui.h"
#include <SDL3/SDL_timer.h>
#include <algorithm>
#include <cfloat>
#include <cstdlib>
#include <cmath>
#include <cstdio>
#include <memory>
#include <unordered_map>

namespace {
	// the map's own sun and fog, put back when the environment goes
	struct MapLook { const Terrain* terrain = nullptr; Vector3 sun; uint32_t sunColor = 0, fogColor = 0; bool changed = false; };
	MapLook g_mapLook;
	uint32_t g_lightningSeen = 0;
	bool g_lightningKnown = false;
	uint64_t g_flashStart = 0;
	// the share of the precipitation and lightning drawn: the front's arrival, while the cloud
	// renderer sets it (else all of it)
	float PrecipitationShown()
	{
		if (g_cloudFrontArrivalTicks == 0 || SDL_GetTicksNS() - g_cloudFrontArrivalTicks > 500000000ull)
			return 1.0f;
		return std::clamp(g_cloudFrontArrival, 0.0f, 1.0f);
	}
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
		if (client->environmentSkyKnown && terrain) {
			g_environment.skyWeather = client->environmentSettings.skyWeather;
			g_environment.skyCover = client->environmentSettings.skyCover / 100.0f;
		}
		return;
	}
	const EnvironmentSettings& es = client->environmentSettings;
	const EnvironmentState& st = client->environment;
	EnvironmentView& v = g_environment;
	v.active = true;
	// day and night without the seasons: the game's sky (the server's weather stays clear)
	v.skyWeather = es.seasons ? -1 : es.skyWeather;
	v.skyCover = es.skyCover / 100.0f;
	// the clock from the client's own time (smooth between the server's updates)
	const float t = client->timeManager.currentTime;
	v.dayFraction = EnvDayFraction(es, t);
	v.yearFraction = EnvYearFraction(es, t);
	// the rest eased towards the server's state (it comes once a second)
	auto ease = [](float& x, float target) { x += (target - x) * 0.05f; };
	v.weather = es.seasons ? st.weather : -1;
	// the spell's cloud cover; the sun dimmed by it (the server's cover the kind's, more or less
	// as far as this one is from the middle of the kind's range)
	float coverShift = 0.0f;
	if (v.weather >= 0) {
		float centre = 0.0f;
		const float random = WeatherCoverFor(v.weather, client->environmentWeatherEnds, &centre);
		v.weatherCover = es.weatherCover >= 0 ? es.weatherCover / 100.0f : random;
		coverShift = v.weatherCover - centre;
	}
	else
		v.weatherCover = -1.0f;
	const float stCloud = std::clamp(st.cloudCover + coverShift, 0.0f, 1.0f);
	// the air's humidity: the weather's, the season's shift, dew from night to mid-morning, the
	// wet ground; eased like the rest
	{
		static const float byWeather[WEATHER_COUNT] = { 0.40f, 0.55f, 0.85f, 0.95f, 0.80f, 1.0f, 0.70f, 0.75f, 0.15f, 0.10f };
		static const float bySeason[4] = { 0.05f, -0.10f, 0.10f, 0.0f }; // spring, summer, autumn, winter
		float h = v.weather >= 0 ? byWeather[v.weather] : 0.45f;
		// while a weather front passes (the volumetric clouds' front, g_cloudFrontArrival below
		// 1 at the camera) the more humid of both weathers: rain coming in or clearing leaves
		// moist air under the broken clouds between them (the server's weather changes at once,
		// the clouds come later)
		static int weatherNow = -1, weatherBefore = -1;
		if (v.weather != weatherNow) {
			weatherBefore = weatherNow;
			weatherNow = v.weather;
		}
		if (weatherBefore >= 0 && v.weather >= 0 && g_cloudFrontArrival < 0.999f)
			h = std::max(h, byWeather[weatherBefore]);
		if (es.seasons)
			h += bySeason[std::clamp((int)st.season, 0, 3)];
		const float hour = EnvDayFraction(es, client->timeManager.currentTime) * 24.0f;
		h += 0.18f * (hour < 5.0f ? 0.6f : hour < 7.0f ? 0.6f + 0.4f * (hour - 5.0f) / 2.0f : 1.0f - std::clamp((hour - 7.0f) / 4.0f, 0.0f, 1.0f))
			+ 0.08f * std::clamp((hour - 19.0f) / 3.0f, 0.0f, 1.0f); // dew: before dawn, the morning, the evening
		h += 0.12f * st.wetness;
		h = std::clamp(h, 0.0f, 1.0f);
		static bool humidityKnown = false;
		if (!humidityKnown || !g_lightningKnown)
			v.humidity = h;
		else
			v.humidity += (h - v.humidity) * 0.01f;
		humidityKnown = true;
	}
	if (!g_lightningKnown) {
		v.cloudCover = stCloud; v.rain = st.rain; v.snowfall = st.snowfall; v.fog = st.fog;
		v.wetness = st.wetness; v.snowCover = st.snowCover; v.ice = st.ice; v.waterRise = st.waterRise;
	}
	ease(v.cloudCover, stCloud);
	ease(v.rain, st.rain);
	ease(v.snowfall, st.snowfall);
	const float shown = PrecipitationShown();
	v.rainShown = v.rain * shown;
	v.snowShown = v.snowfall * shown;
	ease(v.fog, st.fog);
	ease(v.wetness, st.wetness);
	ease(v.snowCover, st.snowCover);
	static const char* testSnow = getenv("WKB_TEST_SNOWCOVER"); // (a test hook: the snow lying, 0-1)
	if (testSnow)
		v.snowCover = (float)atof(testSnow);
	ease(v.ice, st.ice);
	ease(v.waterRise, st.waterRise);
	v.temperatureC = st.temperatureC;
	static const char* testMoon = getenv("WKB_TEST_MOONAGE"); // (a test hook: the moon's phase, 0-1)
	const float moonAge = testMoon ? (float)atof(testMoon) : es.moonAge(t);
	v.sun = ComputeSun(v.dayFraction, v.yearFraction, es.seasons, v.cloudCover, moonAge);
	// the sun's frame: the compass of the minimap (north up: +z, east to the right: +x), so the sun
	// rises in the east, stands in the south at noon and sets in the west, at 47 degrees north
	// as in central Europe (ComputeSun: north-east to south-east by the season). It had followed
	// the map's own sun as south, a different compass on every map (on the continent map it rose
	// in the south, the play test of 2026-11-09).
	const Vector3 south(0.0f, 0.0f, -1.0f), east(1.0f, 0.0f, 0.0f);
	const Vector3& d = v.sun.direction; // (east, up, south)
	terrain->sunVector = (east * d.x + Vector3(0, 1, 0) * d.y + south * d.z).normal();
	auto toWorld = [&](const Vector3& e) { return (east * e.x + Vector3(0, 1, 0) * e.y + south * e.z).normal(); };
	v.sunWorld = toWorld(v.sun.sunTrue);
	v.moonWorld = toWorld(v.sun.moonTrue);
	{
		// the celestial pole: north, 47 degrees up (ComputeSun's latitude); the sky turns about it
		// with the sun, once a day
		const float lat = 47.0f * 3.14159265f / 180.0f;
		const Vector3 pole = (south * -std::cos(lat) + Vector3(0, 1, 0) * std::sin(lat)).normal();
		const Vector3 e1 = east, e2 = pole.cross(e1).normal();
		const float a = v.dayFraction * 2.0f * 3.14159265f;
		v.starX = e1 * std::cos(a) + e2 * std::sin(a);
		v.starY = e2 * std::cos(a) - e1 * std::sin(a);
		v.starZ = pole;
	}
	terrain->sunColor = Scale(g_mapLook.sunColor, v.sun.colour);
	g_cloudSunDirection = toWorld(v.sun.cloudDirection);
	g_cloudSunColor = Scale(g_mapLook.sunColor, v.sun.cloudColour);
	// With the Physical sky model the clouds see a low sun from below: high above the ground they
	// see it longer, and once it is under their own horizon its light comes up at their bases
	// (lit from beneath at dawn and dusk, their tops in shade; the play test of 2026-11-09). The
	// light about 2 degrees lower while the sun is low; its strength kept until the sun is 1 degree
	// under the horizon, gone 6 under.
	if (g_renderSettings.skyModel >= 1 && g_renderSettings.sky && g_renderSettings.dynamicClouds >= 3) {
		const float y = v.sun.sunTrue.y;
		if (y > -0.105f && y < 0.25f) {
			Vector3 d = toWorld(v.sun.sunTrue);
			const float low = 1.0f - std::clamp((y - 0.08f) / 0.17f, 0.0f, 1.0f);
			d.y -= 0.035f * low;
			g_cloudSunDirection = d.normal();
			const float t = std::clamp((y + 0.105f) / 0.087f, 0.0f, 1.0f), up = t * t * (3.0f - 2.0f * t);
			const float dayCol[3] = { 0.71f, 0.71f, 0.71f }; // (ComputeSun's cloud light with the sun low: neutral, DuskTint reddens it)
			float col[3];
			for (int c = 0; c < 3; c++)
				col[c] = std::max(v.sun.cloudColour[c], dayCol[c] * up);
			g_cloudSunColor = Scale(g_mapLook.sunColor, col);
		}
	}
	g_cloudSunValid = true;
	{
		// the fog: the map's, in the sky's light, greyer in fog and under clouds
		float grey[3] = { 0.62f, 0.64f, 0.68f };
		const float toGrey = std::clamp(v.fog * 0.8f + v.cloudCover * 0.35f + std::max(v.rainShown, v.snowShown) * 0.3f, 0.0f, 1.0f);
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
	v.lightningFlash *= shown; // (no strike before the storm's clouds)
	v.lightningPos = g_boltPos;
	// tests (environment variable WKB_TEST_LIGHTNING): a bolt held 40 m in front of the camera
	static const bool testBolt = getenv("WKB_TEST_LIGHTNING") != nullptr;
	if (testBolt) {
		const Vector3 ahead = client->camera.position + Vector3(client->camera.direction.x, 0, client->camera.direction.z).normal() * 75.0f;
		v.lightningPos = Vector3(ahead.x, terrain->getHeight(ahead.x, ahead.z), ahead.z);
		v.lightningFlash = 0.35f;
		g_boltSeed = 12345;
	}
	// the grade: darker and paler at night and under heavy cloud, brighter in a flash. With the
	// Physical sky model by the true sun: the day's grade until it reaches the horizon, the night's
	// from 8 degrees under it (by the daylight, 0.38 with the sun on the horizon, a sunset was
	// graded as the early night: dark and grey, the play test of 2026-11-09)
	const bool physicalSky = g_renderSettings.skyModel >= 1 && g_renderSettings.sky && g_renderSettings.dynamicClouds >= 3;
	const float gradeT = std::clamp((v.sun.sunTrue.y + 0.14f) / 0.16f, 0.0f, 1.0f);
	const float gradeLight = physicalSky ? gradeT * gradeT * (3.0f - 2.0f * gradeT) : v.sun.daylight;
	v.exposure = (0.42f + 0.58f * gradeLight) * (1.0f - 0.18f * v.cloudCover) * (1.0f + 1.2f * v.lightningFlash);
	v.saturation = (0.50f + 0.50f * gradeLight) * (1.0f - 0.2f * v.fog);
	v.fogDistance = std::clamp(1.0f - 0.95f * v.fog - 0.35f * std::max(v.rainShown, v.snowShown * 1.3f), 0.08f, 1.0f);
	// the land's look by the season, the rivers in floods, the frozen falls: slow changes (a few
	// seconds to a minute), by the real time between frames
	{
		static uint64_t lastTicks = 0;
		const uint64_t ticks = SDL_GetTicks();
		const float dt = lastTicks ? std::min(0.25f, (ticks - lastTicks) / 1000.0f) : 0.0f;
		lastTicks = ticks;
		// a new map or a loaded game: straight to the state (no slow change from nothing)
		static const Terrain* settled = nullptr;
		const bool snap = settled != terrain;
		settled = terrain;
		auto approach = [&](float& x, float target, float perSecond) {
			x = snap ? target : x + std::clamp(target - x, -perSecond * dt, perSecond * dt);
		};
		const int season = es.seasons ? st.season : -1;
		float dry = season == 1 ? 0.25f + 0.08f * st.severity : season == 0 || season == 2 ? 0.05f : 0.0f;
		if (st.weather == WEATHER_HEATWAVE) dry = std::max(dry, 0.85f);
		if (st.weather == WEATHER_DROUGHT) dry = std::max(dry, 0.7f);
		dry = std::clamp(dry - 0.6f * st.wetness, 0.0f, 1.0f);
		approach(v.dryness, dry, 0.02f);
		// autumn: the leaves turn over the first third of autumn and stay, fading in early winter
		float autumn = 0.0f;
		if (es.seasons) {
			const float into = v.yearFraction * 4.0f - std::floor(v.yearFraction * 4.0f);
			const int s = (int)std::floor(v.yearFraction * 4.0f) % 4;
			// in winter the trees still in leaf keep their colours until they drop them (TreeBareInWinter)
			autumn = s == 2 ? std::clamp(into / 0.35f, 0.0f, 1.0f) : s == 3 && into < 0.15f ? 1.0f : 0.0f;
		}
		approach(v.autumn, autumn, 0.05f);
		approach(v.mud, std::clamp(st.waterRise / 1.2f, 0.0f, 1.0f) * 0.85f + 0.1f * std::min(st.rain, 1.0f) * (st.waterRise > 0.2f ? 1.0f : 0.0f), 0.02f);
		approach(v.flow, std::clamp(1.0f + 0.55f * std::max(st.waterRise, 0.0f) + 0.3f * st.rain - 0.3f * std::max(-st.waterRise, 0.0f), 0.4f, 2.0f), 0.05f);
		// the falls freeze in a harsh winter's hard cold (below -5 degrees), and thaw above -1
		const bool freezing = es.seasons && st.season == 3 && st.severity >= 2 && st.temperatureC < -5.0f;
		const bool thawing = st.temperatureC > -1.0f || st.season != 3;
		if (freezing)
			approach(v.fallsFrozen, 1.0f, 0.03f);
		else if (thawing || snap)
			approach(v.fallsFrozen, 0.0f, 0.05f);
		v.riverClock = std::fmod(v.riverClock + dt * v.flow, 3600.0f);
		v.fallClock = std::fmod(v.fallClock + dt * (1.0f - v.fallsFrozen) * (0.7f + 0.3f * std::min(v.flow, 2.0f)), 3600.0f);
	}
	if (scene) {
		scene->sunDirection = terrain->sunVector;
		scene->sunColor = terrain->sunColor;
	}
}

std::string EnvironmentHudText(Client* client)
{
	if (!client || !client->environmentActive || !g_environment.active)
		return std::string();
	const EnvironmentSettings& es = client->environmentSettings;
	const EnvironmentState& st = client->environment;
	std::string out;
	auto add = [&](const std::string& part) {
		if (!out.empty())
			out += "  ";
		out += part;
	};
	char buf[64];
	if (es.dayNight) {
		const int minutes = (int)(g_environment.dayFraction * 1440.0f) % 1440;
		snprintf(buf, sizeof(buf), "%02d:%02d", minutes / 60, minutes % 60);
		add(buf);
	}
	if (es.seasons) {
		// the season, with its severity only when it isn't the normal one
		std::string season = SeasonName(st.season);
		if (st.severity <= 0)
			season += " (gentle)";
		else if (st.severity >= 2)
			season += " (harsh)";
		add(season);
		add(WeatherName(st.weather));
		add(FormatTemperature(st.temperatureC, es.tempScale));
		if (st.waterRise > 0.3f) {
			snprintf(buf, sizeof(buf), "Flood +%.1f m", st.waterRise);
			add(buf);
		}
		else if (st.waterRise < -0.3f) {
			snprintf(buf, sizeof(buf), "Low water %.1f m", st.waterRise);
			add(buf);
		}
		if (st.ice > 0.6f)
			add("Frozen");
		if (st.season == 3)
			add("Fields fallow");
	}
	return out;
}

namespace {
	// the season and how far into it (0..1), -1 without seasons
	int SeasonNow(Client* client, float& into)
	{
		into = 0.0f;
		if (!client->environmentActive || !client->environmentSettings.seasons || !g_environment.active)
			return -1;
		const float y = g_environment.yearFraction * 4.0f;
		into = y - std::floor(y);
		return (int)std::floor(y) % 4;
	}
	// how many leaves fall: through autumn, more as it goes on; heavy in early winter from the
	// trees still in leaf
	float LeafFall(Client* client)
	{
		float into;
		const int s = SeasonNow(client, into);
		return s == 2 ? 0.3f + 0.7f * into : s == 3 && into < 0.15f ? 1.6f : 0.0f;
	}
	struct LeafTree { Vector3 centre; float radius; uint32_t id; };
	std::vector<LeafTree> g_leafTrees;
}

bool TreeBareInWinter(Client* client, uint32_t id)
{
	float into;
	if (SeasonNow(client, into) != 3)
		return false;
	return into >= 0.005f + 0.115f * Hash(id * 2654435761u ^ 0x5EEDu);
}

void AddLeafTree(Client* client, const Vector3& centre, float radius, uint32_t id)
{
	if (g_leafTrees.size() < 4096 && LeafFall(client) > 0.0f)
		g_leafTrees.push_back({ centre, radius, id });
}

namespace {
	// a snowflake: six arms with a short branch each, white, its shape in the alpha (16 x 16)
	texture SnowflakeTexture(IRenderer* gfx)
	{
		static texture tex = nullptr;
		if (!tex) {
			constexpr int N = 8;
			Bitmap bmp;
			bmp.width = bmp.height = N;
			bmp.format = BMFORMAT_R8G8B8A8;
			bmp.pixels.resize(N * N * 4);
			auto segment = [](float px, float py, float ax, float ay, float bx, float by) {
				const float dx = bx - ax, dy = by - ay;
				const float t = std::clamp(((px - ax) * dx + (py - ay) * dy) / (dx * dx + dy * dy), 0.0f, 1.0f);
				const float ex = px - (ax + dx * t), ey = py - (ay + dy * t);
				return std::sqrt(ex * ex + ey * ey);
			};
			const float c = (N - 1) * 0.5f;
			for (int y = 0; y < N; y++)
				for (int x = 0; x < N; x++) {
					float d = std::sqrt((x - c) * (x - c) + (y - c) * (y - c)) - 0.6f; // the middle
					for (int k = 0; k < 6; k++) {
						const float a = k * 1.0471976f, ca = std::cos(a), sa = std::sin(a);
						d = std::min(d, segment((float)x, (float)y, c, c, c + ca * 3.5f, c + sa * 3.5f));
						// a branch off each side, two thirds out
						const float bx = c + ca * 2.3f, by = c + sa * 2.3f;
						for (const float side : { -0.7f, 0.7f })
							d = std::min(d, segment((float)x, (float)y, bx, by, bx + std::cos(a + side) * 1.1f, by + std::sin(a + side) * 1.1f));
					}
					const float alpha = std::clamp(1.2f - d / 0.6f, 0.0f, 1.0f);
					uint8_t* p = &bmp.pixels[(y * N + x) * 4];
					p[0] = p[1] = p[2] = 255;
					p[3] = (uint8_t)std::lround(alpha * 255.0f);
				}
			tex = gfx->CreateTexture(bmp, 1);
		}
		return tex;
	}
}

void DrawWeather(IRenderer* gfx, Client* client, const Camera& camera)
{
	const EnvironmentView& v = g_environment;
	std::vector<LeafTree> trees;
	trees.swap(g_leafTrees); // this frame's (the next frame adds its own)
	if (!v.active || !client->terrain)
		return;
	const bool raining = v.rainShown > 0.02f, snowing = v.snowShown > 0.02f, bolt = v.lightningFlash > 0.0f;
	const float leafFall = trees.empty() ? 0.0f : LeafFall(client);
	if (!raining && !snowing && !bolt && leafFall <= 0.0f)
		return;
	static std::unique_ptr<RBatch> batch;
	if (!batch)
		batch.reset(gfx->CreateBatch(4 * 1024, 6 * 1024));
	gfx->NoTexture(0);
	gfx->SetTransformMatrix(&camera.sceneMatrix);
	const float time = SDL_GetTicks() / 1000.0f;
	// the box of falling drops: around the point the camera looks at, its grid fixed in the world
	const Vector3 dir = camera.direction.normal();
	// (continuous in the pitch: with a step at a level view, a degree's turn moved the boxes)
	const float aboveGround = std::max(camera.position.y - client->terrain->getHeight(camera.position.x, camera.position.z), 1.0f);
	const float reach = std::clamp(aboveGround / std::max(-dir.y, 0.05f), 20.0f, 90.0f);
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
	auto placeIn = [&](int i, float fallSpeed, float sway, const Vector3& centre, float box, float height, Vector3& out) {
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
	auto place = [&](int i, float fallSpeed, float sway, Vector3& out) { return placeIn(i, fallSpeed, sway, centre, box, height, out); };
	// The far layer: over the ground the camera sees beyond the near box (from the RTS camera's
	// height the near box is a small patch in the middle of the screen), sparser and larger so
	// that it reads at that distance: about as many again for the whole view
	// around the camera itself, 900 units across, from the ground to well above the camera: a box
	// around the point looked at ended in a line across the view, with no snow above it
	const float farReach = std::clamp(aboveGround / std::max(-dir.y, 0.05f), 30.0f, 400.0f);
	// (looking down, around the ground looked at and about as wide as the view there: around the
	// camera most of it lay outside the view, a few drops on the screen)
	const float lookDown = std::clamp((-dir.y - 0.35f) / 0.45f, 0.0f, 1.0f);
	Vector3 farCentre = camera.position;
	{
		const Vector3 flat = Vector3(dir.x, 0.0f, dir.z);
		const float ahead = aboveGround * std::sqrt(flat.sqlen3()) / std::max(-dir.y, 0.05f);
		if (flat.sqlen3() > 1e-6f)
			farCentre = farCentre + flat.normal() * (ahead * lookDown);
	}
	const float farBox = 900.0f + (std::max(aboveGround * 2.6f, 260.0f) - 900.0f) * lookDown;
	const float farHeight = std::max(aboveGround + 80.0f, 120.0f);
	auto inNear = [&](const Vector3& p) { return std::abs(p.x - centre.x) < box * 0.5f && std::abs(p.z - centre.z) < box * 0.5f; };
	auto inFront = [&](const Vector3& p) { return (p - camera.position).dot(dir) > 0.0f; };
	// how much of it, by the graphics setting (RenderSettings::weatherEffects)
	static const float amount[4] = { 0.35f, 0.65f, 1.0f, 1.5f };
	const float share = amount[std::clamp(g_renderSettings.weatherEffects, 0, 3)];
	// from high up the far layer covers more ground: more of it (the play test: from higher up
	// it did not seem to rain at all), and each drop and flake at least about 1.5 pixels across
	const float farMore = std::clamp(farHeight / 60.0f, 2.0f, 3.5f);
	const float pixel = 2.0f * std::tan(std::max(camera.fovY, 0.2f) * 0.5f) / std::max(768.0f, 1.0f); // a pixel's size a unit away (at 768 lines)
	// A drop's streak on the screen at least minPx long: seen from above a falling streak is
	// foreshortened to a dot (the play test: looking down from high up, no rain at all; looking
	// up, rain), so it is drawn out radially from the point below the camera as well, as rain
	// looks from above
	const float down = std::clamp((-dir.y - 0.3f) / 0.5f, 0.0f, 1.0f);
	auto lengthen = [&](const Vector3& p, Vector3 streak, float minPx) {
		Vector3 ray = p - camera.position;
		const float dist = std::sqrt(ray.sqlen3());
		if (dist < 1e-3f)
			return streak;
		ray = ray * (1.0f / dist);
		const Vector3 onScreen = streak - ray * streak.dot(ray);
		const float px = std::sqrt(onScreen.sqlen3()) / (dist * pixel);
		if (px >= minPx)
			return streak;
		Vector3 radial = ray - dir * ray.dot(dir);
		radial = radial.sqlen3() > 1e-8f ? radial.normal() : camside;
		return streak + radial * ((minPx - px) * dist * pixel);
	};
	if (raining) {
		// (the near box lies on the ground: from high up a dense patch around the point looked at,
		// so thinner the higher the camera; the far layer fills the view)
		const float nearShare = std::clamp(1.0f - (aboveGround - 60.0f) / 140.0f, 0.2f, 1.0f);
		const int drops = (int)(2600 * share * nearShare * std::min(1.0f, v.rainShown));
		const Vector3 vel = Vector3(v.wind.x, -14.0f, v.wind.z);
		const Vector3 along = vel.normal() * 1.1f;
		const uint32_t colour = (uint32_t)(70 + 60 * v.sun.daylight) << 24 | 0xB4BED2;
		for (int i = 0; i < drops; i++) {
			Vector3 p;
			if (!place(i, 14.0f, 0.0f, p) || (p - camera.position).sqlen3() < 8.0f * 8.0f)
				continue;
			const Vector3 streak = lengthen(p, along, 12.0f * down);
			Vector3 across = streak.cross(p - camera.position);
			across = across.sqlen3() > 1e-8f ? across.normal() * 0.025f : camside * 0.025f;
			quad(p + across, p - across, p - streak - across, p - streak + across, colour);
		}
		// far: longer, fainter streaks
		const int farDrops = (int)(1800 * share * farMore * std::min(1.0f, v.rainShown));
		const Vector3 farAlong = along * 3.0f;
		const uint32_t farColour = (uint32_t)(40 + 40 * v.sun.daylight + 35 * down) << 24 | 0xB4BED2;
		for (int i = 0; i < farDrops; i++) {
			Vector3 p;
			if (!placeIn(i + 200000, 14.0f, 0.0f, farCentre, farBox, farHeight, p) || inNear(p) || !inFront(p))
				continue;
			const float dist = std::sqrt((p - camera.position).sqlen3());
			const float width = std::max(0.08f, dist * pixel * 0.75f);
			const Vector3 streak = lengthen(p, farAlong * std::max(1.0f, width / 0.08f * 0.35f), 9.0f * down);
			Vector3 across = streak.cross(p - camera.position);
			across = across.sqlen3() > 1e-8f ? across.normal() * width : camside * width;
			quad(p + across, p - across, p - streak - across, p - streak + across, farColour);
		}
	}
	if (snowing) {
		// snowflake sprites (the rain's quads are untextured): drawn apart, then the texture off
		batch->flush();
		gfx->SetTexture(0, SnowflakeTexture(gfx));
		const int flakes = (int)(2200 * share * std::min(1.0f, v.snowShown));
		const Vector3 up = camside.cross(camera.direction).normal();
		const uint32_t colour = (uint32_t)(150 + 80 * v.sun.daylight) << 24 | 0xF2F4FA;
		for (int i = 0; i < flakes; i++) {
			Vector3 p;
			if (!place(i + 50000, 1.4f, 0.6f, p) || (p - camera.position).sqlen3() < 15.0f * 15.0f)
				continue;
			const float s = (0.06f + 0.05f * Hash(i + 777)) * 0.9f;
			quad(p + (camside + up) * s, p + (camside - up) * s, p + (-camside - up) * s, p + (-camside + up) * s, colour);
		}
		// far: larger flakes (a pixel or two at that distance), as many again over the whole view
		const int farFlakes = (int)(2400 * share * farMore * std::min(1.0f, v.snowShown));
		for (int i = 0; i < farFlakes; i++) {
			Vector3 p;
			if (!placeIn(i + 300000, 1.4f, 1.2f, farCentre, farBox, farHeight, p) || inNear(p) || !inFront(p))
				continue;
			const float dist = std::sqrt((p - camera.position).sqlen3());
			const float s = std::max((0.2f + 0.15f * Hash(i + 4321)) * std::clamp(dist / 120.0f, 0.6f, 2.0f) * 0.75f, dist * pixel * 0.8f);
			quad(p + (camside + up) * s, p + (camside - up) * s, p + (-camside - up) * s, p + (-camside + up) * s, colour);
		}
		batch->flush();
		gfx->NoTexture(0);
	}
	if (leafFall > 0.0f) {
		// the falling leaves: from the crowns of the nearest trees in leaf, drifting with the wind,
		// swaying and tumbling (each a small quad turning about), gold, orange, red and brown
		static const int perTree[4] = { 4, 8, 14, 22 };
		const int each = std::max(1, (int)std::lround(perTree[std::clamp(g_renderSettings.weatherEffects, 0, 3)] * leafFall));
		const size_t nearest = std::min<size_t>(trees.size(), 48);
		std::partial_sort(trees.begin(), trees.begin() + nearest, trees.end(), [&](const LeafTree& a, const LeafTree& b) {
			return (a.centre - centre).sqlen2xz() < (b.centre - centre).sqlen2xz();
		});
		const Vector3 up = camside.cross(camera.direction).normal();
		static const uint32_t palette[5] = { 0xF2C21A, 0xF07818, 0xD8461A, 0xB82A14, 0x9A6A26 };
		const float light = 0.35f + 0.65f * v.sun.daylight;
		for (size_t t = 0; t < nearest; t++) {
			const LeafTree& tr = trees[t];
			for (int k = 0; k < each; k++) {
				const uint32_t seed = tr.id * 97u + (uint32_t)k * 7919u;
				// from the crown's outer edge, below its middle (not inside the foliage), down at
				// 0.5-0.9 m a second, swaying; then a few seconds lying on the ground, fading
				const float around = Hash(seed + 3) * 6.2831853f, out = tr.radius * (0.45f + 0.35f * Hash(seed + 4));
				const float x0 = tr.centre.x + std::cos(around) * out, z0 = tr.centre.z + std::sin(around) * out;
				const float ground0 = client->terrain->getHeight(x0, z0);
				const float startY = std::max(tr.centre.y - tr.radius * (0.05f + 0.3f * Hash(seed + 5)), ground0 + 2.0f);
				const float speed = 0.5f + 0.4f * Hash(seed + 1);
				const float fallTime = (startY - ground0) / speed, rest = 4.0f + 3.0f * Hash(seed + 9);
				const float period = fallTime + rest;
				const float tl = std::fmod(time + Hash(seed + 2) * period, period);
				const float tf = std::min(tl, fallTime); // how long it has fallen
				Vector3 p(x0 + v.wind.x * tf * 0.3f + std::sin(tf * 1.1f + seed * 0.013f) * 0.9f, 0.0f,
					z0 + v.wind.z * tf * 0.3f + std::cos(tf * 0.9f + seed * 0.029f) * 0.9f);
				const float ground = client->terrain->getHeight(p.x, p.z);
				const bool landed = tl >= fallTime;
				p.y = landed ? ground + 0.05f : std::max(startY - tf * speed, ground + 0.05f);
				if ((p - camera.position).sqlen3() < 6.0f * 6.0f)
					continue;
				const float s = 0.25f + 0.15f * Hash(seed + 7); // larger than a leaf: seen from the RTS camera
				Vector3 a, b;
				if (landed) {
					// lying flat on the ground
					const float turnTo = seed * 0.37f;
					a = Vector3(std::cos(turnTo), 0, std::sin(turnTo)) * s;
					b = Vector3(-std::sin(turnTo), 0, std::cos(turnTo)) * (s * 0.7f);
				}
				else {
					// tumbling: a slow spin, and turning edge-on now and then
					const float spin = tf * (0.8f + 1.2f * Hash(seed + 6)) + seed * 0.01f;
					const float flip = 0.25f + 0.75f * std::abs(std::cos(tf * 1.6f + seed * 0.11f));
					a = (camside * std::cos(spin) + up * std::sin(spin)) * s;
					b = (up * std::cos(spin) - camside * std::sin(spin)) * (s * flip);
				}
				const uint32_t rgb = palette[(int)(Hash(seed + 8) * 4.999f)];
				const float shade = light * (landed ? 0.8f : 1.0f);
				const uint32_t r = (uint32_t)(((rgb >> 16) & 255) * shade), g = (uint32_t)(((rgb >> 8) & 255) * shade), bl = (uint32_t)((rgb & 255) * shade);
				const float fade = std::min(1.0f, tl * 2.0f) * std::min(1.0f, (period - tl) / 2.0f);
				quad(p + a + b, p - a + b, p - a - b, p + a - b, (uint32_t)(235 * fade) << 24 | r << 16 | g << 8 | bl);
			}
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
				next += Vector3((Hash(g_boltSeed + s * 2) - 0.5f) * 14.0f, 0.0f, (Hash(g_boltSeed + s * 2 + 1) - 0.5f) * 14.0f);
			Vector3 across = (next - prev).cross(camera.direction);
			across = across.sqlen3() > 1e-8f ? across.normal() * (1.3f - 0.8f * f) : camside * 0.8f;
			quad(next + across, next - across, prev - across, prev + across, colour);
			prev = next;
		}
	}
	batch->flush();
}

namespace {
	// The land that floods, as the server's (FindFloodLand)
	struct FloodMap { const Terrain* terrain = nullptr; std::vector<FloodLand> tiles; };
	FloodMap g_floodMap;

	void PrepareFloodMap(const Terrain* t)
	{
		g_floodMap.terrain = t;
		g_floodMap.tiles = FindFloodLand(t);
	}

	// footprints: where, which way, when, in snow (1) or mud (0)
	struct Print { float x, z, dx, dz, born; uint8_t snow; };
	std::vector<Print> g_prints;
	size_t g_printNext = 0;
	struct Walker { Vector3 last; bool left = false; float seen = 0; };
	std::unordered_map<uint32_t, Walker> g_walkers;
	const Terrain* g_printTerrain = nullptr;

	// a boot's print: the sole and the heel, soft-edged (alpha), white (tinted by the vertex colour)
	texture PrintTexture(IRenderer* gfx)
	{
		static texture tex = nullptr;
		if (tex)
			return tex;
		Bitmap bmp;
		bmp.width = 16; bmp.height = 32;
		bmp.format = BMFORMAT_R8G8B8A8;
		bmp.pixels.resize(16 * 32 * 4);
		for (int y = 0; y < 32; y++)
			for (int x = 0; x < 16; x++) {
				const float u = (x + 0.5f) / 16.0f - 0.5f, v = (y + 0.5f) / 32.0f;
				// the sole (front two thirds), the heel (the back quarter)
				const float sole = 1.0f - std::hypot(u / 0.42f, (v - 0.36f) / 0.33f);
				const float heel = 1.0f - std::hypot(u / 0.34f, (v - 0.84f) / 0.14f);
				const float a = std::clamp(std::max(sole, heel) * 4.0f, 0.0f, 1.0f);
				uint8_t* p = &bmp.pixels[(y * 16 + x) * 4];
				p[0] = p[1] = p[2] = 255;
				p[3] = (uint8_t)(a * 255.0f);
			}
		tex = gfx->CreateTexture(bmp, 1);
		return tex;
	}
	texture WhiteTexture(IRenderer* gfx)
	{
		static texture tex = nullptr;
		if (!tex) {
			Bitmap bmp;
			bmp.width = bmp.height = 1;
			bmp.format = BMFORMAT_R8G8B8A8;
			bmp.pixels.resize(4);
			for (int i = 0; i < 4; i++)
				bmp.pixels[i] = 255;
			tex = gfx->CreateTexture(bmp, 1);
		}
		return tex;
	}
}

void DrawGroundWeather(IRenderer* gfx, Client* client, const Camera& camera)
{
	const EnvironmentView& v = g_environment;
	const Terrain* terrain = client->terrain;
	if (!v.active || !terrain)
		return;
	const int quality = std::clamp(g_renderSettings.weatherEffects, 0, 3);
	// (the enhanced terrain renderer draws the flood in its water shader)
	const bool flooding = v.waterRise > 0.05f && !g_terrainDrawsFlood;
	g_terrainDrawsFlood = false;
	const bool printing = quality >= 2 && (v.snowCover > 0.12f || v.wetness > 0.35f);
	if (!flooding && !printing && g_prints.empty())
		return;
	static std::unique_ptr<RBatch> batch;
	if (!batch)
		batch.reset(gfx->CreateBatch(4 * 1024, 6 * 1024));
	gfx->BeginParticles();
	gfx->BeginBatchDrawing();
	gfx->SetBlendColor(-1);
	gfx->SetTransformMatrix(&camera.sceneMatrix);
	auto surface = [terrain](float x, float z) { return g_terrainTessellationActive ? terrain->getSmoothHeight(x, z) : terrain->getHeight(x, z); };
	// where the camera looks
	const Vector3 dir = camera.direction.normal();
	const float reach = std::clamp(dir.y < -0.05f ? (camera.position.y - terrain->getHeight(camera.position.x, camera.position.z)) / -dir.y : 150.0f, 30.0f, 400.0f);
	const Vector3 look = camera.position + dir * reach;
	const float time = SDL_GetTicks() / 1000.0f;

	// the flood: a sheet of muddy water at the risen level over the land that floods, hidden by
	// the depth test where the ground stands higher; it meets the lakes' raised surface (the
	// terrain renderer's WaterLevelPos) at the same height
	if (flooding) {
		if (g_floodMap.terrain != terrain)
			PrepareFloodMap(terrain);
		gfx->SetTexture(0, WhiteTexture(gfx));
		batch->begin();
		const float range = std::max(160.0f, reach * 1.4f);
		const float light = 0.35f + 0.65f * v.sun.daylight;
		for (const FloodLand& f : g_floodMap.tiles) {
			const float level = f.water + v.waterRise;
			if (level <= f.low + 0.02f)
				continue;
			const float cx = f.x * 5.0f + 2.5f, cz = f.z * 5.0f + 2.5f;
			if (std::abs(cx - look.x) > range || std::abs(cz - look.z) > range)
				continue;
			batchVertex* verts; uint16_t* indices; uint32_t first;
			batch->next(4, 6, &verts, &indices, &first);
			const float corners[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
			for (int i = 0; i < 4; i++) {
				const float x = (f.x + corners[i][0]) * 5.0f, z = (f.z + corners[i][1]) * 5.0f;
				// a slow shimmer, the same at a shared corner
				const float s = 0.9f + 0.1f * std::sin(time * 0.8f + x * 0.21f + z * 0.17f);
				const int r = (int)(78 * light * s), g = (int)(92 * light * s), b = (int)(96 * light * s);
				verts[i].x = x; verts[i].y = level; verts[i].z = z;
				verts[i].u = verts[i].v = 0.5f;
				verts[i].color = 0xD0000000u | (uint32_t)std::min(r, 255) << 16 | (uint32_t)std::min(g, 255) << 8 | (uint32_t)std::min(b, 255);
			}
			for (const int& c : { 0, 2, 1, 0, 3, 2 })
				*(indices++) = (uint16_t)(first + c);
		}
		batch->flush();
		batch->end();
	}

	// footprints (High and Ultra): every walking unit near the camera leaves one every 0.75 m,
	// left and right of its path, in snow or in wet ground; they fade over their life and with
	// the snow or the mud
	const size_t maxPrints = quality >= 3 ? 8192 : 2048;
	const float life = quality >= 3 ? 150.0f : 50.0f;
	const float now = client->timeManager.currentTime;
	if (g_printTerrain != terrain || quality < 2) {
		g_prints.clear(); g_printNext = 0; g_walkers.clear();
		g_printTerrain = terrain;
	}
	if (printing) {
		const bool snow = v.snowCover > 0.12f;
		const auto [tw, th] = terrain->getNumPlayableTiles();
		for (const auto& [id, obj] : client->idmap) {
			if (!obj || obj->blueprint->bpClass != Tags::GAMEOBJCLASS_CHARACTER || obj->blueprint->canWalkOnWater()
				|| (obj->flags & CommonGameObject::fTerminated))
				continue;
			const Vector3& p = obj->position;
			if (std::abs(p.x - look.x) > 160.0f || std::abs(p.z - look.z) > 160.0f)
				continue;
			Walker& w = g_walkers[id];
			w.seen = now;
			const Vector3 d = p - w.last;
			const float moved = std::sqrt(d.x * d.x + d.z * d.z);
			if (moved > 6.0f || w.last.sqlen3() == 0) { w.last = p; continue; }
			if (moved < 0.75f)
				continue;
			w.last = p;
			// not on the water (frozen or not)
			const Terrain::Tile* tile = terrain->getPlayableTile((int)(p.x / 5.0f), (int)(p.z / 5.0f));
			if (!tile || tile->fullOfWater || p.x < 0 || p.z < 0 || p.x >= tw * 5.0f || p.z >= th * 5.0f)
				continue;
			const float fx = d.x / moved, fz = d.z / moved;
			const float side = w.left ? 0.2f : -0.2f;
			w.left = !w.left;
			Print pr{ p.x - fz * side, p.z + fx * side, fx, fz, now, (uint8_t)(snow ? 1 : 0) };
			if (g_prints.size() < maxPrints)
				g_prints.push_back(pr);
			else
				g_prints[g_printNext++ % maxPrints] = pr;
		}
		// the units gone or far away for a while
		if ((int)now % 10 == 0)
			for (auto it = g_walkers.begin(); it != g_walkers.end();)
				it = now - it->second.seen > 5.0f ? g_walkers.erase(it) : std::next(it);
	}
	if (!g_prints.empty()) {
		gfx->SetTexture(0, PrintTexture(gfx));
		batch->begin();
		const float snowFade = std::clamp((v.snowCover - 0.05f) * 6.0f, 0.0f, 1.0f);
		const float mudFade = std::clamp((v.wetness - 0.2f) * 4.0f, 0.0f, 1.0f);
		size_t alive = 0;
		for (const Print& pr : g_prints) {
			const float age = now - pr.born;
			if (age > life || age < 0)
				continue;
			alive++;
			if (std::abs(pr.x - look.x) > 170.0f || std::abs(pr.z - look.z) > 170.0f)
				continue;
			const float fade = (1.0f - age / life) * (pr.snow ? snowFade : mudFade);
			if (fade <= 0.01f)
				continue;
			// snow: blue-grey hollows; mud: dark brown
			const uint32_t rgb = pr.snow ? 0x6A7488u : 0x2E2216u;
			const uint32_t colour = (uint32_t)(fade * (pr.snow ? 200.0f : 190.0f)) << 24 | rgb;
			batchVertex* verts; uint16_t* indices; uint32_t first;
			batch->next(4, 6, &verts, &indices, &first);
			// 0.2 m wide, 0.42 m long (a little larger than a boot, to read from the game's camera), heel behind
			const float ax = pr.dx * 0.21f, az = pr.dz * 0.21f, sx = -pr.dz * 0.1f, sz = pr.dx * 0.1f;
			const float px[4] = { pr.x + ax - sx, pr.x + ax + sx, pr.x - ax + sx, pr.x - ax - sx };
			const float pz[4] = { pr.z + az - sz, pr.z + az + sz, pr.z - az + sz, pr.z - az - sz };
			const float uv[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
			const float y = surface(pr.x, pr.z) + 0.07f;
			for (int i = 0; i < 4; i++) {
				verts[i].x = px[i]; verts[i].y = y; verts[i].z = pz[i];
				verts[i].u = uv[i][0]; verts[i].v = uv[i][1];
				verts[i].color = colour;
			}
			for (const int& c : { 0, 2, 1, 0, 3, 2 })
				*(indices++) = (uint16_t)(first + c);
		}
		batch->flush();
		batch->end();
		if (alive == 0)
			g_prints.clear(), g_printNext = 0;
	}
}

void DrawMinimapWeather(IRenderer* gfx, Client* client, void* drawListPtr, float x0, float y0, float x1, float y1)
{
	const EnvironmentView& v = g_environment;
	const Terrain* terrain = client->terrain;
	if (!v.active || !terrain || (v.snowCover < 0.02f && v.ice < 0.05f))
		return;
	// the masks, once per map: the snow where the land is flat enough to hold it (as the terrain
	// renderer's SnowShare, in patches), the water's tiles; row 0 is the north edge (largest z)
	static const Terrain* maskTerrain = nullptr;
	static texture snowMask = nullptr, iceMask = nullptr;
	if (maskTerrain != terrain) {
		if (snowMask) gfx->FreeTexture(snowMask);
		if (iceMask) gfx->FreeTexture(iceMask);
		snowMask = iceMask = nullptr;
		maskTerrain = terrain;
		const auto [w, h] = terrain->getNumPlayableTiles();
		if (w > 0 && h > 0) {
			Bitmap snow, ice;
			for (Bitmap* b : { &snow, &ice }) {
				b->width = w; b->height = h;
				b->format = BMFORMAT_R8G8B8A8;
				b->pixels.resize((size_t)w * h * 4);
			}
			for (int z = 0; z < h; z++)
				for (int x = 0; x < w; x++) {
					const Terrain::Tile* tile = terrain->getPlayableTile(x, z);
					const size_t at = ((size_t)(h - 1 - z) * w + x) * 4;
					const bool water = tile && tile->fullOfWater;
					const Vector3 n = terrain->getNormal(x + terrain->edge, z + terrain->edge);
					const float flat = std::clamp((n.y - 0.72f) / 0.2f, 0.0f, 1.0f) * (0.85f + 0.15f * Hash((uint32_t)(z * w + x)));
					for (int c = 0; c < 3; c++)
						snow.pixels[at + c] = ice.pixels[at + c] = 255;
					snow.pixels[at + 3] = water ? 0 : (uint8_t)(flat * 255.0f);
					ice.pixels[at + 3] = water ? 255 : 0;
				}
			snowMask = gfx->CreateTexture(snow, 1);
			iceMask = gfx->CreateTexture(ice, 1);
		}
	}
	ImDrawList* dl = (ImDrawList*)drawListPtr;
	const ImVec2 a(x0, y0), b(x1, y1);
	if (snowMask && v.snowCover >= 0.02f)
		dl->AddImage(snowMask, a, b, ImVec2(0, 0), ImVec2(1, 1), IM_COL32(242, 246, 252, (int)(std::min(1.0f, v.snowCover * 1.3f) * 235)));
	if (iceMask && v.ice >= 0.05f)
		dl->AddImage(iceMask, a, b, ImVec2(0, 0), ImVec2(1, 1), IM_COL32(214, 230, 242, (int)(std::min(1.0f, v.ice) * 210)));
}

void UpdateWaterfallSounds(Client* client)
{
	SoundPlayer* player = SoundPlayer::getSoundPlayer();
	if (!player)
		return;
	const Vector3 ear = client->camera.position;
	// the nearest (by how loud they are there)
	std::vector<std::pair<float, const WaterfallSoundSpot*>> heard;
	for (const WaterfallSoundSpot& s : g_waterfallSoundSpots) {
		const float d = std::sqrt((s.pos - ear).sqlen3());
		if (d < 320.0f)
			heard.push_back({ std::min(1.5f, s.strength) / (1.0f + d / 30.0f), &s });
	}
	std::sort(heard.begin(), heard.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
	const float flow = WaterfallFlow(g_environment);
	for (int slot = 0; slot < SoundPlayer::MAX_LOOPS; slot++) {
		if (slot >= (int)heard.size() || flow < 0.12f) {
			player->setLoop(slot, "", Vector3(), 1.0f, 0.0f, 1.0f);
			continue;
		}
		const WaterfallSoundSpot& s = *heard[slot].second;
		// falls: the river's rush, the deeper and louder the more water; cascades and trickles: the
		// stream's
		const bool small = s.cascade || flow < 0.5f || s.strength < 0.4f;
		static const char* const river = "Warrior Kings Game Set\\Sounds\\Background Sounds\\ambient_river_1.wav";
		static const char* const stream = "Warrior Kings Game Set\\Sounds\\Background Sounds\\ambient_stream_2.wav";
		const float size = std::clamp(s.strength, 0.2f, 1.5f);
		const float gain = std::clamp(0.35f + 0.45f * size, 0.3f, 1.0f) * std::clamp(flow, 0.15f, 1.6f) * 0.8f;
		const float pitch = std::clamp(1.15f - 0.12f * flow - 0.05f * size, 0.8f, 1.2f);
		player->setLoop(slot, small ? stream : river, s.pos, 8.0f + 10.0f * size * std::min(flow, 1.5f), gain, pitch);
	}
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
