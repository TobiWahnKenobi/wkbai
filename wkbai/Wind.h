// wkbai - the wind (wkbai's own; off by default)
// Licensed under the GNU General Public License 3
//
// The server simulates the wind once a game second (ServerEnvironment::updateWind) from the
// lobby's settings (EnvironmentSettings::wind...), the weather and the clock, without the
// simulation's random numbers: the prevailing direction comes from the game's wind seed (chosen
// when the game starts, saved and recorded with the settings), its veering, its strength's
// swings and its gusts from smooth noise of the game time. So with the wind off nothing changes,
// and with it on every client and every replay has the same wind.
//
// It is game state, not only a look: it fans the wildfires of heatwaves and droughts (they spread
// faster and further downwind, burn hotter and out sooner, throw embers ahead) and it chills
// (the felt cold is lower in a strong wind, the felt heat a little lower too). The clients draw
// it at no cost of their own: the clouds' drift, the rain's and the snow's slant, the smoke and
// the flames, the sea's waves, the grass, the banners' flapping; and, by the graphics setting
// RenderSettings::windTrees, the trees bending in it.
//
// Its compass is the minimap's: north +z, east +x. "From" is where it blows from (a north wind
// blows towards the south). Without the lobby's wind (off, the campaign, a game from before) the
// look is the fixed one from before: from the west-south-west at the reference speed, steady.

#pragma once

#include <cstdint>
#include <vector>
#include "util/vecmat.h"

struct NetPacketWriter;
struct BinaryReader;
struct EnvironmentSettings;
struct CommonGameState;

// the fixed wind from before (the clouds' drift (6, 2), the rain's (0.3, 0.1)): the way it blows
// and its speed in metres a second; the clouds drift 6.3 units a second at this speed
constexpr float WIND_DEFAULT_X = 0.948683f, WIND_DEFAULT_Z = 0.316228f, WIND_REFERENCE_SPEED = 5.0f;

// What the server simulates and sends (NETCLIMSG_WIND), once a game second
struct WindState {
	float dirX = WIND_DEFAULT_X, dirZ = WIND_DEFAULT_Z; // the way it blows (unit, x east, z north)
	float speed = WIND_REFERENCE_SPEED;  // the mean in metres a second (the gusts come on top: WindGust)
	float gustiness = 0.3f;              // 0 steady .. 1 squalls
	Vector3 direction() const { return Vector3(dirX, 0.0f, dirZ); }
	Vector3 velocity() const { return Vector3(dirX * speed, 0.0f, dirZ * speed); }
	void write(NetPacketWriter& w) const;
	void read(BinaryReader& r);
};

// the lobby's choices (EnvironmentSettings::windFrom, windStrength), for the menus and the HUD
const char* WindFromName(int from);          // 0 "Random", 1-8 North .. North-west
const char* WindStrengthName(int strength);  // 0 "By the weather", 1-5 Calm .. Gale
// the Beaufort-like name of a speed (m/s): "calm", "light air", ... "storm"
const char* WindSpeedName(float speed);
// the compass point a wind blows from, as text ("WSW"), from the way it blows
const char* WindCompass(float dirX, float dirZ);

// The wind's target at a game time (before the server eases its speed towards it): the way it
// blows (the prevailing direction, veering slowly when the settings let it), its speed and
// gustiness for the weather (an EnvWeather) now
WindState WindTarget(const EnvironmentSettings& s, int weather, float time);
// the gusts: the speed's factor at a time (seconds, any clock), 1 on average; smooth, from about
// 0.5 in a lull to 1.6-2.2 in a squall by the gustiness; seed: the game's wind seed
float WindGust(float time, float gustiness, uint32_t seed);
// the prevailing direction it blows (unit), before the veering
Vector3 WindPrevailing(const EnvironmentSettings& s);
// A boat's speed factor on a heading (xz) in a wind: the square sails and oars of the game's
// ships run best before the wind and worst into it. With c the cosine between the heading and
// the way the wind blows (1 running before it, 0 across it, -1 into it) and k its speed / 8 m/s
// (at most 2): 1 + k (0.12 + 0.30 c - 0.17 c^2), held to 0.35 .. 1.6. A moderate breeze: +25 %
// before it, +12 % across, -35 % into it; a gale twice that (into it down to 0.35).
float WindBoatFactor(const Vector3& heading, const WindState& wind);
// a smooth noise of one variable, 0..1, the same everywhere for a seed
float WindNoise(float x, uint32_t seed);

// What the client draws, worked out every frame (interface/EnvironmentView.cpp, UpdateWindView):
// the server's wind eased between its messages, the gusts on it, and clocks that run with it so
// that a change of speed never makes anything jump
struct WindView {
	bool on = false;               // the lobby's wind (off: the fixed wind from before)
	Vector3 dir{ WIND_DEFAULT_X, 0.0f, WIND_DEFAULT_Z }; // the way it blows, unit
	float speed = WIND_REFERENCE_SPEED; // the mean, m/s
	float gust = 1.0f;             // this frame's gust factor (WindGust)
	float gustiness = 0.3f;
	// the speed with the gust (m/s), and 0..1 of it against a gale (20 m/s): the trees' bend
	float now() const { return speed * gust; }
	float strength() const { float s = now() / 20.0f; return s < 0.0f ? 0.0f : s > 1.0f ? 1.0f : s; }
	// metres the air has moved since the start (x, z; wrapped at 100 km): what drifts with the wind
	// (the rain's and snow's boxes, the clouds' offsets)
	double driftX = 0.0, driftZ = 0.0;
	// metres along its way, whatever the way (the grass's gusts; wrapped at 100 km)
	double along = 0.0;
	// the banners' clock (seconds): their flapping animation runs at 0.5 times its own speed in a
	// calm, 1 at the reference speed, up to 2.2 in a gale
	double flagClock = 0.0;
	// the trees' sway clock (seconds; faster in a strong wind) and the gusts' clock
	double swayClock = 0.0;
	float flagRate() const;
	// what the falling rain and snow, the smoke and the flames drift at (m/s, horizontal): 0.6 of
	// the mean wind (no gusts: the smoke's puffs lie along it by their age), the fixed (0.3, 0.1)
	// without the lobby's wind; and the same integrated over the time (m, the rain's boxes)
	Vector3 particle() const;
	// the game's prevailing wind, without its veering (WindPrevailing): the sea's waves run along
	// it (their phase must not turn with the veering)
	Vector3 prevailing{ WIND_DEFAULT_X, 0.0f, WIND_DEFAULT_Z };
	double particleX = 0.0, particleZ = 0.0;
};
extern WindView g_wind;

// The wind near the ground (the wind field's still part; the grass's, later the fires' shelter):
// per tile of the playable map (5 m), a factor on the wind's speed. Below 1 in the lee of what
// stands upwind higher than the tile (hills, woods, buildings, walls: the steeper it rises, the
// calmer, down to 0.25 right behind a wall or a wood's edge, and under the trees themselves),
// above 1 on crests standing over the ground upwind (up to 1.35). Worked out for one direction:
// the client's again as the wind turns, and every few seconds (buildings come and go).
struct WindShelterMap {
	int width = 0, height = 0;   // playable tiles
	std::vector<uint8_t> value;  // the factor * 170 (0 .. 1.5), row order
	Vector3 dir{ WIND_DEFAULT_X, 0.0f, WIND_DEFAULT_Z }; // what it was worked out for
	uint32_t revision = 0;       // counts up with each build
	// at a world position of the playable area, bilinear; 1 outside it or before the first build
	float at(float x, float z) const;
};
// for a game state's terrain and objects, the wind blowing along dir (unit, xz)
void BuildWindShelter(const CommonGameState* gs, const Vector3& dir, WindShelterMap& out);
// the same in two steps: what it needs from the game state (quick, on the thread that owns it:
// the ground's height and what stands on each tile), then the work (on any thread)
struct WindShelterInput {
	int width = 0, height = 0;
	std::vector<float> ground, above;
	Vector3 dir;
};
WindShelterInput GatherWindShelter(const CommonGameState* gs, const Vector3& dir);
void ComputeWindShelter(const WindShelterInput& in, WindShelterMap& out);
// the client's (UpdateWindView), for the grass (DiligentEnhancedTerrainRenderer)
extern WindShelterMap g_windShelter;
