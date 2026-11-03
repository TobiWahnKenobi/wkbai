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
	bool active() const { return dayNight || seasons; }
	float dayLengthS() const { return dayLengthMin * 60.0f; }
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
	float cloudCover = 0.0f, rain = 0.0f, snowfall = 0.0f, fog = 0.0f;
	float wetness = 0.0f, snowCover = 0.0f, ice = 0.0f, waterRise = 0.0f;
	float lightningFlash = 0.0f;  // 0..1, this frame
	Vector3 lightningPos;
	Vector3 wind{ 0.3f, 0.0f, 0.1f }; // metres per second of the falling rain and snow
	// the 3D scene's grade: night darker and paler, fog nearer (the renderers' FogBuffer)
	float exposure = 1.0f, saturation = 1.0f, fogDistance = 1.0f;
};
extern EnvironmentView g_environment;

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
