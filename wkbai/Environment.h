// wkbai - day and night, seasons and weather (wkbai's own; off by default)
// Licensed under the GNU General Public License 3
//
// The server simulates the clock, the temperature and the weather (ServerEnvironment.cpp) and
// sends the state to its clients once a game second (NETCLIMSG_ENVIRONMENT). Everything here is
// shared by both: the settings, the state, the sun worked out from the clock, the temperature
// scales, and the night sight (both sides shorten sight ranges by the same rule, so the client's
// fog of war matches what the server's units see).

#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include "util/vecmat.h"

struct NetPacketWriter;
struct BinaryReader;
struct CommonGameState;
struct Terrain;

// The skirmish settings ("dayNight", "dayLength", ... QuickSkirmishMenu::setSetting)
struct EnvironmentSettings {
	bool dayNight = false;
	int dayLengthMin = 20;     // a whole day and night, in game minutes
	int startHour = 8;         // the game starts at this hour
	bool seasons = false;
	int seasonLengthMin = 30;  // one season, in game minutes (never shorter than a day with dayNight)
	int startSeason = 0;       // 0 spring, 1 summer, 2 autumn, 3 winter
	int tempScale = 0;         // the HUD's: 0 Celsius, 1 Fahrenheit, 2 Kelvin
	// each season's weather: 0 gentle, 1 normal, 2 harsh, 3 random (chosen anew every year)
	int severity[4] = { 1, 1, 1, 1 };
	// the debug panel's (F9) moves of the clock: added to the day's fraction (startHour's) and to
	// the year's (startSeason's), so the time of day and the season jump there and go on from it
	float dayShift = 0.0f, yearShift = 0.0f;
	bool active() const { return dayNight || seasons; }
	float dayLengthS() const { return dayLengthMin * 60.0f; }
	// the seasons since the game began (with the shift), as a count: its floor + startSeason is
	// the season, counted on over the years
	float seasonsElapsed(float time) const { return time / seasonLengthS() + yearShift * 4.0f; }
	// a season never shorter than one day while days pass
	float seasonLengthS() const;
	// "name=value" lines, as Server::aiSettingsText (recordings); read by apply
	std::string text() const;
	bool apply(const std::string& name, float value);
};

enum EnvWeather : uint8_t {
	WEATHER_CLEAR, WEATHER_CLOUDY, WEATHER_RAIN, WEATHER_HEAVY_RAIN, WEATHER_THUNDERSTORM, WEATHER_FOG,
	WEATHER_SNOW, WEATHER_BLIZZARD, WEATHER_HEATWAVE, WEATHER_DROUGHT, WEATHER_COUNT
};
const char* WeatherName(int weather);
const char* SeasonName(int season);

// What the server simulates; the clients get it every game second
struct EnvironmentState {
	float time = 0.0f;          // the game time it is for
	float dayFraction = 0.0f;   // 0 midnight, 0.5 noon
	float yearFraction = 0.0f;  // 0..1 over the four seasons, 0: the start of spring
	int season = 0;
	int severity = 1;           // the current season's (random resolved)
	int weather = WEATHER_CLEAR;
	float temperatureC = 12.0f;
	float cloudCover = 0.0f, rain = 0.0f, snowfall = 0.0f, fog = 0.0f; // 0..1, as they are drawn
	float wetness = 0.0f;       // the ground: 0 dry, 1 soaked
	float snowCover = 0.0f;     // 0 none, 1 deep
	float ice = 0.0f;           // the rivers' and lakes' ice, 0 open, 1 frozen over
	float waterRise = 0.0f;     // metres above (flood) or below (drought) the water's normal level
	float sightFactor = 1.0f;   // the sight ranges away from light (night, fog, rain)
	float speedFactor = 1.0f;   // the ground's: wet, snowy
	uint32_t lightningCount = 0; // strikes so far (the client flashes for each new one)
	Vector3 lightningPos;       // the last strike
	void write(NetPacketWriter& w) const;
	void read(BinaryReader& r);
};

// The ice (EnvironmentState::ice): land units walk over the frozen rivers and lakes from
// ICE_WALKABLE (boats are held fast); below it nobody sets out on it, and below ICE_BREAKS the
// ones still out on deep water go through it and drown
constexpr float ICE_WALKABLE = 0.85f, ICE_BREAKS = 0.5f;

// The land that floods (the server's floods and the client's picture of them, the same tiles):
// spread out from the shores, tile by tile (4 neighbours), up to 10 tiles from the water, over
// the land less than 4 m above the water it spreads from and not more than 1.5 m below it (so not
// down the cliff below a lake that ends in a waterfall); a tile reached from several waters takes
// the highest. Playable tile coordinates, the water's level, the ground at the tile's middle and
// at its lowest corner; in row order.
struct FloodLand { int x, z; float water, ground, low; };
std::vector<FloodLand> FindFloodLand(const Terrain* t);

// The sun worked out from the clock: towards it, how strong, its colour, the sky's
struct SunState {
	Vector3 direction{ 0.5f, 1.0f, 0.3f }; // towards the sun (or the moon at night), unit
	float elevation = 0.8f;   // sine of the sun's height over the horizon
	float daylight = 1.0f;    // 0 night .. 1 full day
	float colour[3] = { 1, 1, 1 }, ambient[3] = { 1, 1, 1 }; // multipliers of the map's own
	float moon = 0.0f;
};
// dayFraction 0..1, yearFraction 0..1 (the sun higher in summer); clouds dim it
SunState ComputeSun(float dayFraction, float yearFraction, bool seasons, float cloudCover);
// the day fraction / year fraction at a game time
float EnvDayFraction(const EnvironmentSettings& s, float time);
float EnvYearFraction(const EnvironmentSettings& s, float time);

// the temperature in the chosen scale, with its unit ("12 °C" in plain ASCII: "12 C")
float ConvertTemperature(float celsius, int scale);
std::string FormatTemperature(float celsius, int scale);

// The client's view of it, worked out every frame (interface/EnvironmentView.cpp) for the
// renderers: the shared interface for lighting and weather. active false: none (the map's own
// sun, no weather, every field at its neutral value).
struct EnvironmentView {
	bool active = false;
	float dayFraction = 0.5f, yearFraction = 0.125f;
	SunState sun;                 // also written into Terrain::sunVector/sunColor and the scene's sun
	float temperatureC = 12.0f;
	int weather = -1;             // EnvWeather now (-1: inactive): the dynamic clouds' preset
	float cloudCover = 0.0f, rain = 0.0f, snowfall = 0.0f, fog = 0.0f;
	float wetness = 0.0f, snowCover = 0.0f, ice = 0.0f, waterRise = 0.0f;
	float lightningFlash = 0.0f;  // 0..1, this frame
	Vector3 lightningPos;
	Vector3 wind{ 0.3f, 0.0f, 0.1f }; // metres per second of the falling rain and snow
	// the 3D scene's grade: night darker and paler, fog nearer (the renderers' FogBuffer)
	float exposure = 1.0f, saturation = 1.0f, fogDistance = 1.0f;
	// the land's look by the season: dryness 0..1 (summer a little, heatwave and drought much:
	// green grass and ground turn straw-yellow), autumn 0..1 (the broadleaf trees' colours)
	float dryness = 0.0f, autumn = 0.0f;
	// floods: mud 0..1 (the rivers greenish-brown), flow (how fast they run, 1 normal)
	float mud = 0.0f, flow = 1.0f;
	// a harsh winter's cold: the waterfalls frozen, 0..1
	float fallsFrozen = 0.0f;
	// clocks that run with the water (seconds, wrapped at 3600): the rivers' (faster in a flood)
	// and the waterfalls' (stopped when frozen), so a change of speed never jumps the patterns
	float riverClock = 0.0f, fallClock = 0.0f;
};
extern EnvironmentView g_environment;
// how much water the waterfalls carry (their look and their sound): 1 as the map has it (and
// with the weather off), up to 2 in a flood with heavy rain, down to 0.1 in a drought
inline float WaterfallFlow(const EnvironmentView& v)
{
	if (!v.active)
		return 1.0f;
	float f = 1.0f + 0.6f * v.waterRise + 0.4f * v.rain;
	f = f < 0.1f ? 0.1f : f > 2.0f ? 2.0f : f;
	return f * (1.0f - v.fallsFrozen); // frozen: no water falls (and none is heard)
}

// The night's shorter sight: away from light, sight ranges are cut to `factor` of themselves; a
// target in a lit block (20 m, as Server::sightBlocks) is seen at the full range. Each game state
// (server and client) keeps its own and builds the lit blocks from its own objects.
struct NightSight {
	static constexpr float BLOCK = 20.0f;
	float factor = 1.0f;
	std::vector<uint8_t> lit;
	int width = 0, height = 0;
	bool active() const { return factor < 0.999f; }
	bool isLit(const Vector3& p) const {
		const int bx = (int)(p.x / BLOCK), bz = (int)(p.z / BLOCK);
		return p.x >= 0 && p.z >= 0 && bx < width && bz < height && lit[bz * width + bx];
	}
	// the sight radius (metres) at which `target` is seen by a viewer whose full radius is `full`
	float radiusFor(float full, const Vector3& target) const {
		return (!active() || isLit(target)) ? full : full * factor;
	}
	// the lit blocks from the game state's buildings (fully built, any player's: windows, torches)
	// and the burning ones (alias "Burning Buildings": further)
	void rebuild(CommonGameState* gs);
};
