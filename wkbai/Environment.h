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
	int severity[4] = { 3, 3, 3, 3 };
	// the debug panel's (F9) moves of the clock: added to the day's fraction (startHour's) and to
	// the year's (startSeason's), so the time of day and the season jump there and go on from it
	float dayShift = 0.0f, yearShift = 0.0f;
	// the sky without the seasons' weather (the game settings "cloudWeather": 0 by the cloud
	// cover, 1-10 an EnvWeather + 1; "cloudCover" in percent); the clients draw it
	int skyWeather = 0, skyCover = 45;
	// the seasons' weather's cloud cover ("weatherCover", the debug panel's, F9): below 0 random
	// for each spell of weather around its kind's own (the clients draw it; WeatherCoverFor),
	// 0-100 that cover in percent whatever the weather
	int weatherCover = -1;
	// how often the seasons' weather changes ("weatherStability"): 0 frequent (1-4 min between
	// changes), 1 normal (3-10 min), 2 seldom (8-20 min), 3 random (the default: as before, 10-35%
	// of a season, from 45 s to 10 min), 4 custom: between weatherMinChange and weatherMaxChange
	// minutes, and only the weathers in weatherTypes (bit n: EnvWeather n) as far as the season
	// has them (none of them in a season: clear then)
	int weatherStability = 3;
	int weatherMinChange = 3, weatherMaxChange = 10;
	int weatherTypes = 1023;
	// the moon's phase ("moonPhase"): 0 natural (a 29.53-day month over the game's days, from
	// moonStart, the moon's phase on the first day of startSeason in the year 1000), 1-8 fixed:
	// new, waxing crescent, first quarter, waxing gibbous, full, waning gibbous, third quarter,
	// waning crescent; moonStart 0..1 (0 new, 0.5 full), saved with the game ("moonStart";
	// "moonDay": the same in whole days of the month, 0-29, for the menu's integer settings and
	// tests); below 0 not chosen yet: the year 1000's for startSeason (HistoricMoonAge)
	int moonPhase = 0;
	float moonStart = -1.0f;
	// the phase at a game time, 0..1 (0 new, 0.25 first quarter, 0.5 full, 0.75 third quarter)
	float moonAge(float time) const;
	// the time between changes in seconds (lo, hi); false: random, the season's length has it
	bool changeRange(float& lo, float& hi) const;
	bool weatherAllowed(int w) const { return weatherStability != 4 || ((weatherTypes >> w) & 1) != 0; }
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
// a spell of weather's cloud cover (0-1), random around its kind's within the kind's range (a
// cloudy day from a few broken clouds to nearly overcast), the same on every client: from the
// spell's seed (its end time, as the server sends it); centre: the middle of the kind's range
float WeatherCoverFor(int weather, float spellSeed, float* centre = nullptr);
const char* SeasonName(int season);
const char* WeatherStabilityName(int stability);
const char* MoonPhaseName(int setting); // EnvironmentSettings::moonPhase's (0 "Natural")
// the moon's phase (0 new, 0.5 full) at midnight starting the first day of a season (0 spring ..
// 3 winter, from its equinox or solstice) in the year 1000: the natural moon's start
float HistoricMoonAge(int season);
// the lit part of the moon's disc (0 new .. 1 full) at a phase (moonAge)
float MoonLitFraction(float age);

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
	float moonAge = 0.5f;     // its phase (0 new, 0.5 full)
	float moonLit = 1.0f;     // the lit part of its disc (MoonLitFraction)
	float moonLight = 1.0f;   // its direct light: lit part times how far it is up (0 set)
	// the sky light's brightness on the scene (SkyAmbient) against the day's: 1 by day, at night
	// by the moon (0.36 moonless .. 0.92 a full moon high up)
	float skyLightScale = 1.0f;
	float shadowFade = 1.0f; // 0 where the light jumps between the sun and the moon (dusk, dawn)
	float sunrise = 6.0f, sunset = 18.0f; // the day's hours (by the season)
	// the clouds' light: the true sun until it is 4 degrees below the horizon (the clouds stay lit
	// after the ground; no shadow fade: theirs is the dusk's best light), then the moon; east, up,
	// south, and its colour (DuskTint reddens it in the shaders)
	Vector3 cloudDirection{ 0.5f, 1.0f, 0.3f };
	float cloudColour[3] = { 1, 1, 1 };
	// the sun's and the moon's own directions (east, up, south; unit), even below the horizon:
	// the night sky's (direction is the light's, the moon's at night, kept a little up)
	Vector3 sunTrue{ 0.5f, 1.0f, 0.3f }, moonTrue{ -0.5f, -1.0f, -0.3f };
};
// dayFraction 0..1, yearFraction 0..1 (the sun higher in summer); clouds dim it
// moonAge: the moon's phase (0 new, 0.5 full: opposite the sun, as without phases), its place
// on the sky, its lit part and its light by it
SunState ComputeSun(float dayFraction, float yearFraction, bool seasons, float cloudCover, float moonAge = 0.5f);

// how far the volumetric clouds' weather front has come over the camera (0: the old weather's
// clouds overhead, 1: the new one's), set by the cloud renderer each frame (ticks: SDL_GetTicksNS
// of that frame): the rain, snow and lightning drawn wait for their clouds
extern float g_cloudFrontArrival;
// the clouds' light in the world's frame (SunState::cloudDirection, cloudColour times the map's
// sun colour), set by the environment's view each frame; g_cloudSunValid false: the scene's sun
extern Vector3 g_cloudSunDirection;
extern uint32_t g_cloudSunColor;
extern bool g_cloudSunValid;
extern uint64_t g_cloudFrontArrivalTicks;
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
	Vector3 sunWorld{ 0, -1, 0 }, moonWorld{ 0, 1, 0 }; // sun.sunTrue and .moonTrue in the world's frame
	// the stars' frame (the night sky): a world direction's coordinates on the turning sky are
	// its dot products with these (they turn about the pole once a day)
	Vector3 starX{ 1, 0, 0 }, starY{ 0, 1, 0 }, starZ{ 0, 0, 1 };
	float temperatureC = 12.0f;
	int weather = -1;             // EnvWeather now (-1: inactive): the dynamic clouds' preset
	// its cloud cover (0-1; EnvironmentSettings::weatherCover or WeatherCoverFor): the dynamic
	// clouds' instead of their preset's; below 0 the preset's
	float weatherCover = -1.0f;
	// the air's humidity (0-1) from the weather, the season, the time of day (dew in the
	// morning) and the wet ground: the light shafts of the sun and the moon are stronger in moist
	// air (1 without the seasons' weather: the plain strength)
	float humidity = 0.5f;
	// the game's sky without the seasons (EnvironmentSettings::skyWeather, its cover 0..1): -1
	// outside a game (the menus, the editor, a game from before): the lighting options' clouds
	int skyWeather = -1;
	float skyCover = 0.45f;
	float cloudCover = 0.0f, rain = 0.0f, snowfall = 0.0f, fog = 0.0f;
	// as drawn: rain and snowfall while their clouds are overhead (PrecipitationShown)
	float rainShown = 0.0f, snowShown = 0.0f;
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
