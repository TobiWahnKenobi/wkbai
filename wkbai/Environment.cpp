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
	if (dayShift != 0.0f || yearShift != 0.0f) {
		snprintf(buf, sizeof(buf), "dayShift=%.9g\nyearShift=%.9g\n", dayShift, yearShift);
		out += buf;
	}
	return out;
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
	else if (name == "dayShift") dayShift = v;
	else if (name == "yearShift") yearShift = v;
	else return false;
	return true;
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

SunState ComputeSun(float dayFraction, float yearFraction, bool seasons, float cloudCover)
{
	SunState sun;
	// at 47 degrees north; the sun's declination over the year: the equinoxes in the middle of
	// spring and autumn, the solstices in the middle of summer and winter (with the seasons'
	// temperatures), so the noon sun stands 66 degrees high at midsummer and 20 at midwinter and
	// the days last 15.7 and 8.3 hours of the day's length; a mild 10 degrees without seasons
	const float lat = 47.0f * PI / 180.0f;
	const float decl = (seasons ? 23.4f * std::sin(2.0f * PI * (yearFraction - 0.125f)) : 10.0f) * PI / 180.0f;
	const float h = (dayFraction - 0.5f) * 2.0f * PI;
	// in a frame of east, south, up
	auto at = [&](float hourAngle) {
		const float up = std::sin(lat) * std::sin(decl) + std::cos(lat) * std::cos(decl) * std::cos(hourAngle);
		const float east = -std::cos(decl) * std::sin(hourAngle);
		const float south = -(std::cos(lat) * std::sin(decl) - std::sin(lat) * std::cos(decl) * std::cos(hourAngle));
		return Vector3(east, up, south);
	};
	const Vector3 s = at(h);
	sun.elevation = s.y;
	sun.sunTrue = s.normal();
	sun.moonTrue = at(h + PI).normal();
	sun.daylight = Smoothstep(-0.10f, 0.20f, s.y);
	// the moon: opposite the sun, at least a little up, for the night's shadows
	Vector3 dir = s;
	if (sun.daylight < 0.5f) {
		Vector3 m = at(h + PI);
		m.y = std::max(m.y, 0.25f);
		dir = m;
	}
	dir.y = std::max(dir.y, 0.05f);
	sun.direction = dir.normal();
	// colours: white high up, warm low down, a pale blue moon
	const float warm = Smoothstep(0.0f, 0.45f, s.y);
	const float dayCol[3] = { 1.0f, 0.62f + 0.38f * warm, 0.38f + 0.62f * warm };
	const float moonCol[3] = { 0.20f, 0.25f, 0.38f };
	const float ambDay[3] = { 1.0f, 0.92f + 0.08f * warm, 0.85f + 0.15f * warm };
	const float ambNight[3] = { 0.30f, 0.35f, 0.50f };
	sun.moon = 1.0f - sun.daylight;
	const float sunDim = 1.0f - 0.6f * cloudCover, ambDim = 1.0f - 0.2f * cloudCover;
	for (int c = 0; c < 3; c++) {
		sun.colour[c] = (moonCol[c] + (dayCol[c] - moonCol[c]) * sun.daylight) * sunDim;
		sun.ambient[c] = (ambNight[c] + (ambDay[c] - ambNight[c]) * sun.daylight) * ambDim;
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
