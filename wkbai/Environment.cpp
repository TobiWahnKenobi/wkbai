// wkbai - day and night, seasons and weather (see Environment.h)
// Licensed under the GNU General Public License 3

#include "Environment.h"
#include "common.h"
#include "network.h"
#include "terrain.h"
#include "tags.h"
#include "util/BinaryReader.h"
#include "gameset/gameset.h"
#include "gameset/GameObjBlueprint.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <deque>

namespace {
	constexpr float PI = 3.14159265358979f;
	float Smoothstep(float a, float b, float x) {
		const float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
		return t * t * (3.0f - 2.0f * t);
	}
	float Frac(float x) { return x - std::floor(x); }
}

EnvironmentView g_environment;
float g_cloudFrontArrival = 1.0f;
Vector3 g_cloudSunDirection(0, 1, 0);
uint32_t g_cloudSunColor = 0xFFFFFFFF;
bool g_cloudSunValid = false;
uint64_t g_cloudFrontArrivalTicks = 0;

float EnvironmentSettings::seasonLengthS() const
{
	float s = std::max(1, seasonLengthMin) * 60.0f;
	if (dayNight)
		s = std::max(s, dayLengthS());
	return s;
}

std::string EnvironmentSettings::text() const
{
	char buf[256];
	snprintf(buf, sizeof(buf), "dayNight=%d\ndayLength=%d\nstartHour=%d\nseasons=%d\nseasonLength=%d\nstartSeason=%d\ntempScale=%d\n"
		"weatherSpring=%d\nweatherSummer=%d\nweatherAutumn=%d\nweatherWinter=%d\n",
		(int)dayNight, dayLengthMin, startHour, (int)seasons, seasonLengthMin, startSeason, tempScale,
		severity[0], severity[1], severity[2], severity[3]);
	std::string out = buf;
	// (only once moved: the text of a game without them, a recording's, stays as it was)
	if (skyWeather != 0 || skyCover != 45) {
		snprintf(buf, sizeof(buf), "cloudWeather=%d\ncloudCover=%d\n", skyWeather, skyCover);
		out += buf;
	}
	if (weatherStability != 3 || weatherMinChange != 3 || weatherMaxChange != 10 || weatherTypes != 1023) {
		snprintf(buf, sizeof(buf), "weatherStability=%d\nweatherMinChange=%d\nweatherMaxChange=%d\nweatherTypes=%d\n",
			weatherStability, weatherMinChange, weatherMaxChange, weatherTypes);
		out += buf;
	}
	if (moonPhase != 0 || moonStart >= 0.0f) {
		snprintf(buf, sizeof(buf), "moonPhase=%d\nmoonStart=%.9g\n", moonPhase, moonStart);
		out += buf;
	}
	if (weatherCover >= 0) {
		snprintf(buf, sizeof(buf), "weatherCover=%d\n", weatherCover);
		out += buf;
	}
	if (dayShift != 0.0f || yearShift != 0.0f) {
		snprintf(buf, sizeof(buf), "dayShift=%.9g\nyearShift=%.9g\n", dayShift, yearShift);
		out += buf;
	}
	return out;
}

float WeatherCoverFor(int weather, float spellSeed, float* centre)
{
	// each kind's range: from the clouds' preset's (DiligentClouds g_cloudPresets) out to both sides
	static const float range[WEATHER_COUNT][2] = {
		{ 0.0f, 0.0f },   // clear
		{ 0.30f, 0.85f }, // cloudy: a few cumulus to a nearly closed deck
		{ 0.70f, 0.90f }, // rain
		{ 0.84f, 0.96f }, // heavy rain
		{ 0.50f, 0.72f }, // thunderstorm (its cells besides)
		{ 0.84f, 0.95f }, // fog
		{ 0.75f, 0.92f }, // snow
		{ 0.88f, 0.97f }, // blizzard
		{ 0.0f, 0.0f },   // heatwave
		{ 0.0f, 0.0f },   // drought
	};
	if (weather < 0 || weather >= WEATHER_COUNT)
		return 0.0f;
	if (centre)
		*centre = 0.5f * (range[weather][0] + range[weather][1]);
	uint32_t h;
	memcpy(&h, &spellSeed, 4);
	h ^= (uint32_t)weather * 0x9E3779B9u;
	h ^= h >> 16; h *= 0x7FEB352Du; h ^= h >> 15; h *= 0x846CA68Bu; h ^= h >> 16;
	const float r = (h & 0xFFFFFF) / 16777216.0f;
	return range[weather][0] + (range[weather][1] - range[weather][0]) * r;
}

bool EnvironmentSettings::apply(const std::string& name, float v)
{
	const int i = (int)v;
	if (name == "dayNight") dayNight = i != 0;
	else if (name == "dayLength") dayLengthMin = std::clamp(i, 1, 24 * 60);
	else if (name == "startHour") startHour = std::clamp(i, 0, 23);
	else if (name == "seasons") seasons = i != 0;
	else if (name == "seasonLength") seasonLengthMin = std::clamp(i, 1, 24 * 60);
	else if (name == "startSeason") startSeason = std::clamp(i, 0, 3);
	else if (name == "tempScale") tempScale = std::clamp(i, 0, 2);
	else if (name == "weatherSpring") severity[0] = std::clamp(i, 0, 3);
	else if (name == "weatherSummer") severity[1] = std::clamp(i, 0, 3);
	else if (name == "weatherAutumn") severity[2] = std::clamp(i, 0, 3);
	else if (name == "weatherWinter") severity[3] = std::clamp(i, 0, 3);
	else if (name == "cloudWeather") skyWeather = std::clamp(i, 0, (int)WEATHER_COUNT);
	else if (name == "cloudCover") skyCover = std::clamp(i, 0, 100);
	else if (name == "weatherCover") weatherCover = v < 0.0f ? -1 : std::clamp(i, 0, 100);
	else if (name == "weatherStability") weatherStability = std::clamp(i, 0, 4);
	else if (name == "weatherMinChange") weatherMinChange = std::clamp(i, 1, 120);
	else if (name == "weatherMaxChange") weatherMaxChange = std::clamp(i, 1, 120);
	else if (name == "weatherTypes") weatherTypes = i & ((1 << WEATHER_COUNT) - 1);
	else if (name == "moonPhase") moonPhase = std::clamp(i, 0, 8);
	else if (name == "moonStart") moonStart = v < 0.0f ? -1.0f : Frac(v);
	else if (name == "moonDay") moonStart = Frac(std::clamp(v, 0.0f, 29.53f) / 29.530589f);
	else if (name == "dayShift") dayShift = v;
	else if (name == "yearShift") yearShift = v;
	else return false;
	return true;
}

bool EnvironmentSettings::changeRange(float& lo, float& hi) const
{
	static const float ranges[3][2] = { { 1, 4 }, { 3, 10 }, { 8, 20 } }; // minutes
	if (weatherStability >= 0 && weatherStability < 3) {
		lo = ranges[weatherStability][0] * 60.0f;
		hi = ranges[weatherStability][1] * 60.0f;
		return true;
	}
	if (weatherStability == 4) {
		lo = std::min(weatherMinChange, weatherMaxChange) * 60.0f;
		hi = std::max(weatherMinChange, weatherMaxChange) * 60.0f;
		return true;
	}
	return false;
}

float EnvironmentSettings::moonAge(float time) const
{
	if (moonPhase >= 1 && moonPhase <= 8)
		return (moonPhase - 1) / 8.0f;
	// the game's days so far (with the clock's own start and the debug panel's moves of it)
	const float days = startHour / 24.0f + dayShift + time / std::max(dayLengthS(), 1.0f);
	return Frac((moonStart < 0.0f ? HistoricMoonAge(startSeason) : moonStart) + days / 29.530589f);
}

const char* MoonPhaseName(int p)
{
	static const char* names[9] = { "Natural", "New moon", "Waxing crescent", "First quarter", "Waxing gibbous",
		"Full moon", "Waning gibbous", "Third quarter", "Waning crescent" };
	return (p >= 0 && p < 9) ? names[p] : "?";
}

float HistoricMoonAge(int season)
{
	// the spring equinox of 2000 (20 March, 07:35 UTC, Julian day 2451623.816) a thousand mean
	// tropical years back, the seasons a quarter year apart (14/15 March of the Julian calendar
	// in the year 1000); the mean month from a new moon on 6 January 2000, 18:14 UTC (Julian day
	// 2451550.26). The mean month, not the true new moons: a few hours off, as "roughly" asks.
	// Spring 6.6 days old (near the first quarter), summer 9.3, autumn 12.1, winter 14.8 (full).
	const double year = 365.24219;
	const double jd = 2451623.816 - 1000.0 * year + std::clamp(season, 0, 3) * year / 4.0;
	const double age = (std::floor(jd - 0.5) + 0.5 - 2451550.26) / 29.530588853; // its midnight
	return (float)(age - std::floor(age));
}

float MoonLitFraction(float age)
{
	return 0.5f * (1.0f - std::cos(2.0f * PI * age));
}

const char* WeatherStabilityName(int s)
{
	static const char* names[5] = { "Frequent", "Normal", "Seldom", "Random", "Custom" };
	return (s >= 0 && s < 5) ? names[s] : "?";
}

const char* WeatherName(int w)
{
	static const char* names[WEATHER_COUNT] = { "Clear", "Cloudy", "Rain", "Heavy rain", "Thunderstorm", "Fog",
		"Snow", "Blizzard", "Heatwave", "Drought" };
	return (w >= 0 && w < WEATHER_COUNT) ? names[w] : "?";
}

const char* SeasonName(int s)
{
	static const char* names[4] = { "Spring", "Summer", "Autumn", "Winter" };
	return names[((s % 4) + 4) % 4];
}

void EnvironmentState::write(NetPacketWriter& w) const
{
	w.writeFloat(time);
	w.writeFloat(dayFraction);
	w.writeFloat(yearFraction);
	w.writeUint8((uint8_t)season);
	w.writeUint8((uint8_t)severity);
	w.writeUint8((uint8_t)weather);
	for (float f : { temperatureC, cloudCover, rain, snowfall, fog, wetness, snowCover, ice, waterRise, sightFactor, speedFactor })
		w.writeFloat(f);
	w.writeUint32(lightningCount);
	w.writeVector3(lightningPos);
}

void EnvironmentState::read(BinaryReader& r)
{
	time = r.readFloat();
	dayFraction = r.readFloat();
	yearFraction = r.readFloat();
	season = r.readUint8();
	severity = r.readUint8();
	weather = r.readUint8();
	for (float* f : { &temperatureC, &cloudCover, &rain, &snowfall, &fog, &wetness, &snowCover, &ice, &waterRise, &sightFactor, &speedFactor })
		*f = r.readFloat();
	lightningCount = r.readUint32();
	lightningPos = r.readVector3();
}

float EnvDayFraction(const EnvironmentSettings& s, float time)
{
	if (!s.dayNight)
		return 0.5f; // no clock: always noon (the sun as computed, with seasons)
	return Frac(s.startHour / 24.0f + s.dayShift + time / s.dayLengthS());
}

float EnvYearFraction(const EnvironmentSettings& s, float time)
{
	if (!s.seasons)
		return 0.125f; // mid spring
	return Frac(s.startSeason * 0.25f + s.yearShift + time / (4.0f * s.seasonLengthS()));
}

std::vector<FloodLand> FindFloodLand(const Terrain* t)
{
	std::vector<FloodLand> out;
	if (!t)
		return out;
	const auto [w, h] = t->getNumPlayableTiles();
	if (w <= 0 || h <= 0)
		return out;
	const size_t n = (size_t)w * h;
	std::vector<float> ground(n), level(n, -1e30f);
	std::vector<uint8_t> water(n), steps(n, 255);
	for (int z = 0; z < h; z++)
		for (int x = 0; x < w; x++) {
			const TerrainTile* tile = t->getPlayableTile(x, z);
			water[(size_t)z * w + x] = tile && tile->fullOfWater;
			ground[(size_t)z * w + x] = t->getHeight(x * 5.0f + 2.5f, z * 5.0f + 2.5f);
		}
	const int dx[4] = { 1, -1, 0, 0 }, dz[4] = { 0, 0, 1, -1 };
	auto fits = [&](size_t i, float lvl) { return ground[i] - lvl < 4.0f && ground[i] - lvl > -1.5f; };
	std::deque<int> queue;
	// the shores: land beside water
	for (int z = 0; z < h; z++)
		for (int x = 0; x < w; x++) {
			const size_t i = (size_t)z * w + x;
			if (water[i])
				continue;
			for (int k = 0; k < 4; k++) {
				const int ax = x + dx[k], az = z + dz[k];
				if (ax < 0 || az < 0 || ax >= w || az >= h || !water[(size_t)az * w + ax])
					continue;
				const float lvl = t->getPlayableTile(ax, az)->waterLevel;
				if (fits(i, lvl) && lvl > level[i]) {
					level[i] = lvl;
					steps[i] = 1;
				}
			}
			if (steps[i] == 1)
				queue.push_back((int)i);
		}
	// outwards (a tile reached again with higher water spreads again)
	while (!queue.empty()) {
		const int i = queue.front();
		queue.pop_front();
		if (steps[i] >= 10)
			continue;
		const int x = i % w, z = i / w;
		for (int k = 0; k < 4; k++) {
			const int ax = x + dx[k], az = z + dz[k];
			if (ax < 0 || az < 0 || ax >= w || az >= h)
				continue;
			const size_t j = (size_t)az * w + ax;
			if (water[j] || !fits(j, level[i]) || level[j] >= level[i])
				continue;
			level[j] = level[i];
			steps[j] = (uint8_t)(steps[i] + 1);
			queue.push_back((int)j);
		}
	}
	for (int z = 0; z < h; z++)
		for (int x = 0; x < w; x++) {
			const size_t i = (size_t)z * w + x;
			if (level[i] < -1e29f)
				continue;
			const float low = std::min({ t->getVertex(t->edge + x, t->edge + z), t->getVertex(t->edge + x + 1, t->edge + z),
				t->getVertex(t->edge + x, t->edge + z + 1), t->getVertex(t->edge + x + 1, t->edge + z + 1) });
			out.push_back({ x, z, level[i], ground[i], low });
		}
	return out;
}

SunState ComputeSun(float dayFraction, float yearFraction, bool seasons, float cloudCover, float moonAge)
{
	SunState sun;
	// at 47 degrees north; the sun's declination over the year: the equinoxes in the middle of
	// spring and autumn, the solstices in the middle of summer and winter (with the seasons'
	// temperatures), so the noon sun stands 66 degrees high at midsummer and 20 at midwinter and
	// the days last 15.7 and 8.3 hours of the day's length; a mild 10 degrees without seasons
	const float lat = 47.0f * PI / 180.0f;
	const float decl = (seasons ? 23.4f * std::sin(2.0f * PI * (yearFraction - 0.125f)) : 10.0f) * PI / 180.0f;
	const float h = (dayFraction - 0.5f) * 2.0f * PI;
	// the moon: its elongation, how far east of the sun it has moved along the ecliptic (0 new,
	// half a turn full), so it rises and sets that much later; its declination the sun's at that
	// place on the ecliptic (with the seasons: the full moon high in winter, low in summer). Its
	// hour angle lags the sun's by the elongation.
	const float elong = 2.0f * PI * moonAge;
	// (without the seasons the sun's fixed 10 degrees as a point of the ecliptic: the full moon
	// opposite it at -10, the whole disc lit)
	const float moonDecl = (seasons ? 23.4f * std::sin(2.0f * PI * (yearFraction - 0.125f) + elong) : 10.0f * std::cos(elong)) * PI / 180.0f;
	const float hm = h - elong;
	// in a frame of east, south, up
	auto atDecl = [&](float hourAngle, float decl) {
		const float up = std::sin(lat) * std::sin(decl) + std::cos(lat) * std::cos(decl) * std::cos(hourAngle);
		const float east = -std::cos(decl) * std::sin(hourAngle);
		const float south = -(std::cos(lat) * std::sin(decl) - std::sin(lat) * std::cos(decl) * std::cos(hourAngle));
		return Vector3(east, up, south);
	};
	auto at = [&](float hourAngle) { return atDecl(hourAngle, decl); };
	const Vector3 s = at(h);
	const Vector3 mTrue = atDecl(hm, moonDecl);
	sun.elevation = s.y;
	sun.sunTrue = s.normal();
	sun.moonTrue = mTrue.normal();
	// the moon's light: its lit part (none at new moon), and only while it is up
	sun.moonAge = moonAge;
	sun.moonLit = MoonLitFraction(moonAge);
	sun.moonLight = sun.moonLit * Smoothstep(-0.03f, 0.12f, mTrue.y);
	sun.daylight = Smoothstep(-0.10f, 0.20f, s.y);
	// Sunrise and sunset by the season (the hour angle where the sun crosses the horizon at this
	// declination: in winter later and earlier, in summer earlier and later), in the day's hours.
	// The shadows' light by the clock around them (the play test: they jumped at 6 in spring):
	// the moon's fades out from 2 to 1 hours before sunrise, the sun's in from 1 hour before to
	// sunrise; at dusk the sun's out from sunset to 1 hour after, the moon's in from 1 to 2 hours
	// after. The light's direction changes from one to the other while both are faded out.
	const float h0 = std::acos(std::clamp(-std::tan(lat) * std::tan(decl), -1.0f, 1.0f));
	const float hour = dayFraction * 24.0f, rise = 12.0f - h0 * 12.0f / PI, set = 12.0f + h0 * 12.0f / PI;
	auto around = [](float x) { x = std::fmod(x + 12.0f, 24.0f); if (x < 0) x += 24.0f; return x - 12.0f; }; // -12..12 hours
	const float fromRise = around(hour - rise), fromSet = around(hour - set);
	const bool sunLights = hour >= rise - 1.0f && hour <= set + 1.0f;
	sun.sunrise = rise;
	sun.sunset = set;
	// the moon (opposite the sun when full), at least a little up, for the night's shadows
	Vector3 dir = s;
	if (!sunLights) {
		Vector3 m = mTrue;
		m.y = std::max(m.y, 0.25f);
		dir = m;
	}
	dir.y = std::max(dir.y, 0.05f);
	sun.direction = dir.normal();
	// colours: white high up, warm low down, a pale blue moon
	const float warm = Smoothstep(0.0f, 0.45f, s.y);
	const float dayCol[3] = { 1.0f, 0.62f + 0.38f * warm, 0.38f + 0.62f * warm };
	// The moon's light by its phase and height, and the night sky's with it: most of the night's
	// ambient light is the moonlit sky, so a moonless night is about half as light and has no
	// direct light (the play test of 2026-11-10: the sky's unchanged ambient, stronger than the
	// moon's direct light, hid the phase; then 12% down, a little too bright, 2026-11-11; the
	// moonless night darker, 0.36 of the day's sky light instead of 0.5, the phases between by
	// their light, the full moon's kept)
	const float moonCol[3] = { 0.23f * sun.moonLight, 0.27f * sun.moonLight, 0.40f * sun.moonLight };
	const float skyMoon = 0.36f + 0.56f * sun.moonLight;
	const float ambDay[3] = { 1.0f, 0.92f + 0.08f * warm, 0.85f + 0.15f * warm };
	const float ambNight[3] = { 0.30f * skyMoon, 0.35f * skyMoon, 0.50f * skyMoon };
	sun.moon = 1.0f - sun.daylight;
	sun.skyLightScale = 1.0f + (skyMoon - 1.0f) * sun.moon;
	const float sunDim = 1.0f - 0.6f * cloudCover, ambDim = 1.0f - 0.2f * cloudCover;
	// the light's direction jumps from the sun to the moon at daylight 0.5 (and back at dawn):
	// the direct light, and with it every shadow (ray-traced, the shadow maps', the clouds'),
	// fades out before the jump and in after it, the sky's light alone in between
	sun.shadowFade = sunLights ? std::min(Smoothstep(-1.0f, 0.0f, fromRise), Smoothstep(1.0f, 0.0f, fromSet))
		: std::min(Smoothstep(-1.0f, -2.0f, fromRise), Smoothstep(1.0f, 2.0f, fromSet));
	for (int c = 0; c < 3; c++) {
		sun.colour[c] = (moonCol[c] + (dayCol[c] - moonCol[c]) * sun.daylight) * sunDim * sun.shadowFade;
		sun.ambient[c] = (ambNight[c] + (ambDay[c] - ambNight[c]) * sun.daylight) * ambDim;
	}
	if (s.y > -0.07f) {
		sun.cloudDirection = s.normal();
		const float up = Smoothstep(-0.07f, 0.0f, s.y);
		// (not dimmed by the cover: that is the ground's, under them) and not warm: the cloud
		// shaders redden it themselves by the air it passes (DuskTint); the warm day colour on top
		// of that reddened the dawn and dusk clouds twice, to a deep red (the play test of
		// 2026-11-11), so a neutral light of the warm colour's brightness
		const float grey = 0.3f * dayCol[0] + 0.59f * dayCol[1] + 0.11f * dayCol[2];
		for (int c = 0; c < 3; c++)
			sun.cloudColour[c] = grey * up;
	}
	else {
		Vector3 m = mTrue;
		m.y = std::max(m.y, 0.25f);
		sun.cloudDirection = m.normal();
		const float up = Smoothstep(-0.07f, -0.14f, s.y);
		for (int c = 0; c < 3; c++)
			sun.cloudColour[c] = moonCol[c] * sunDim * up;
	}
	return sun;
}

float ConvertTemperature(float c, int scale)
{
	return scale == 1 ? c * 9.0f / 5.0f + 32.0f : scale == 2 ? c + 273.15f : c;
}

std::string FormatTemperature(float c, int scale)
{
	char buf[32];
	snprintf(buf, sizeof(buf), scale == 2 ? "%.0f K" : scale == 1 ? "%.0f \xC2\xB0""F" : "%.0f \xC2\xB0""C", ConvertTemperature(c, scale));
	return buf;
}

void NightSight::rebuild(CommonGameState* gs)
{
	if (!gs->terrain || !gs->gameSet) {
		width = height = 0;
		lit.clear();
		return;
	}
	const auto [aw, ah] = gs->terrain->getPlayableArea();
	width = (int)std::ceil(aw / BLOCK);
	height = (int)std::ceil(ah / BLOCK);
	lit.assign((size_t)width * height, 0);
	auto mark = [&](const Vector3& p, float radius) {
		const int bx0 = std::max(0, (int)((p.x - radius) / BLOCK)), bx1 = std::min(width - 1, (int)((p.x + radius) / BLOCK));
		const int bz0 = std::max(0, (int)((p.z - radius) / BLOCK)), bz1 = std::min(height - 1, (int)((p.z + radius) / BLOCK));
		for (int bz = bz0; bz <= bz1; bz++)
			for (int bx = bx0; bx <= bx1; bx++) {
				// the block's nearest point to p within the radius
				const float nx = std::clamp(p.x, bx * BLOCK, (bx + 1) * BLOCK), nz = std::clamp(p.z, bz * BLOCK, (bz + 1) * BLOCK);
				if ((nx - p.x) * (nx - p.x) + (nz - p.z) * (nz - p.z) <= radius * radius)
					lit[bz * width + bx] = 1;
			}
	};
	for (const auto& [id, obj] : gs->idmap) {
		if (!obj || obj->blueprint->bpClass != Tags::GAMEOBJCLASS_BUILDING || (obj->flags & CommonGameObject::fTerminated))
			continue;
		const std::string& name = obj->blueprint->name;
		if (name.size() >= 10 && name.compare(name.size() - 10, 10, "Foundation") == 0)
			continue;
		mark(obj->position, 20.0f);
	}
	static thread_local const GameSet* aliasSet = nullptr;
	static thread_local int burning = -1;
	if (aliasSet != gs->gameSet.get()) {
		aliasSet = gs->gameSet.get();
		burning = gs->gameSet->aliases.names.getIndex("Burning Buildings");
	}
	if (burning >= 0)
		if (auto it = gs->aliases.find(burning); it != gs->aliases.end())
			for (const CmnGORef& ref : it->second)
				if (CommonGameObject* b = gs->findObject(ref.objid))
					mark(b->position, 40.0f);
}
