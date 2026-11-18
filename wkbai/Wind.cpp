// wkbai - the wind (see Wind.h)
// Licensed under the GNU General Public License 3

#include "Wind.h"
#include "Environment.h"
#include "network.h"
#include "util/BinaryReader.h"
#include <algorithm>
#include <cmath>

WindView g_wind;

namespace {
	constexpr float PI = 3.14159265358979f;
	float Hash01(uint32_t n)
	{
		n ^= n >> 16; n *= 0x7feb352du; n ^= n >> 15; n *= 0x846ca68bu; n ^= n >> 16;
		return (n & 0xFFFFFFu) / 16777216.0f;
	}
	// by EnvWeather: the mean speed (m/s) and the gustiness. Fog comes with still air, a storm's
	// and a blizzard's with gales; a drought's dry wind is fresh and gusty, a heatwave's air light
	const float WEATHER_SPEED[WEATHER_COUNT] = { 3.5f, 5.5f, 7.0f, 10.0f, 14.0f, 1.2f, 5.0f, 17.0f, 3.0f, 7.5f };
	const float WEATHER_GUSTS[WEATHER_COUNT] = { 0.3f, 0.3f, 0.35f, 0.45f, 0.65f, 0.1f, 0.3f, 0.6f, 0.35f, 0.5f };
	// the lobby's fixed strengths (windStrength 1-5): calm, light, breeze, strong, gale
	const float FIXED_SPEED[5] = { 0.8f, 3.0f, 6.0f, 11.0f, 18.0f };
	const float FIXED_GUSTS[5] = { 0.1f, 0.25f, 0.3f, 0.4f, 0.55f };
	// the default's bearing (where it blows from, degrees clockwise from north): WIND_DEFAULT_X, _Z
	constexpr float DEFAULT_FROM = 251.565f;
}

void WindState::write(NetPacketWriter& w) const
{
	w.writeFloat(dirX);
	w.writeFloat(dirZ);
	w.writeFloat(speed);
	w.writeFloat(gustiness);
}

void WindState::read(BinaryReader& r)
{
	dirX = r.readFloat();
	dirZ = r.readFloat();
	speed = r.readFloat();
	gustiness = r.readFloat();
}

const char* WindFromName(int from)
{
	static const char* names[9] = { "Random (mostly westerly)", "North", "North-east", "East", "South-east", "South",
		"South-west", "West", "North-west" };
	return names[std::clamp(from, 0, 8)];
}

const char* WindStrengthName(int strength)
{
	static const char* names[6] = { "By the weather", "Calm", "Light", "Breeze", "Strong", "Gale" };
	return names[std::clamp(strength, 0, 5)];
}

const char* WindSpeedName(float v)
{
	// the Beaufort scale's
	static const float upTo[] = { 0.5f, 1.6f, 3.4f, 5.5f, 8.0f, 10.8f, 13.9f, 17.2f, 20.8f, 24.5f };
	static const char* names[] = { "calm", "light air", "light breeze", "gentle breeze", "moderate breeze", "fresh breeze",
		"strong breeze", "near gale", "gale", "strong gale", "storm" };
	int i = 0;
	while (i < 10 && v >= upTo[i])
		i++;
	return names[i];
}

const char* WindCompass(float dirX, float dirZ)
{
	static const char* points[16] = { "N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE", "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW" };
	// from: against the way it blows; the bearing clockwise from north (+z) through east (+x)
	float b = std::atan2(-dirX, -dirZ) * 180.0f / PI;
	if (b < 0.0f)
		b += 360.0f;
	return points[(int)std::floor(b / 22.5f + 0.5f) % 16];
}

float WindNoise(float x, uint32_t seed)
{
	const float i = std::floor(x), f = x - i;
	const uint32_t n = (uint32_t)(int64_t)i;
	const float a = Hash01(n * 0x9E3779B9u ^ seed), b = Hash01((n + 1) * 0x9E3779B9u ^ seed);
	const float s = f * f * (3.0f - 2.0f * f);
	return a + (b - a) * s;
}

float WindGust(float t, float g, uint32_t seed)
{
	// three octaves (7 s, 2.3 s, 0.9 s) around their mean, and now and then a squall (a minute or
	// so apart, lasting several seconds), the stronger the gustier
	const float n = 0.6f * WindNoise(t / 7.0f, seed ^ 0xA511u) + 0.3f * WindNoise(t / 2.3f, seed ^ 0x3C6Fu)
		+ 0.1f * WindNoise(t / 0.9f, seed ^ 0x77E1u);
	const float sq = WindNoise(t / 23.0f, seed ^ 0x51EDu);
	const float squall = std::clamp((sq - 0.72f) / 0.2f, 0.0f, 1.0f);
	const float f = 1.0f + g * (n - 0.5f) * 2.0f + g * 1.2f * squall * squall * (3.0f - 2.0f * squall) - g * 0.1f;
	return std::max(f, 0.3f);
}

// the prevailing direction (where it blows from, degrees): the lobby's, or by the seed (within 90
// degrees of the west-south-west, as the westerlies over Europe)
static float PrevailingFrom(const EnvironmentSettings& s, uint32_t seed)
{
	return s.windFrom >= 1 && s.windFrom <= 8 ? (s.windFrom - 1) * 45.0f : DEFAULT_FROM + (Hash01(seed) - 0.5f) * 180.0f;
}

Vector3 WindPrevailing(const EnvironmentSettings& s)
{
	const float a = PrevailingFrom(s, (uint32_t)s.windSeed * 2654435761u + 0x1234567u) * PI / 180.0f;
	return Vector3(-std::sin(a), 0.0f, -std::cos(a));
}

WindState WindTarget(const EnvironmentSettings& s, int weather, float time)
{
	WindState w;
	const uint32_t seed = (uint32_t)s.windSeed * 2654435761u + 0x1234567u;
	weather = std::clamp(weather, 0, (int)WEATHER_COUNT - 1);
	float from = PrevailingFrom(s, seed);
	if (s.windVeers) {
		// veering slowly, up to 35 degrees either way over several minutes; storms swing it faster
		from += (WindNoise(time / 420.0f, seed ^ 0xBEEFu) - 0.5f) * 70.0f;
		from += (WindNoise(time / 45.0f, seed ^ 0xCAFEu) - 0.5f) * 24.0f * WEATHER_GUSTS[weather];
	}
	const float a = from * PI / 180.0f;
	w.dirX = -std::sin(a);
	w.dirZ = -std::cos(a);
	if (s.windStrength >= 1 && s.windStrength <= 5) {
		w.speed = FIXED_SPEED[s.windStrength - 1] * (0.85f + 0.3f * WindNoise(time / 150.0f, seed ^ 0x5EEDu));
		w.gustiness = FIXED_GUSTS[s.windStrength - 1];
	}
	else {
		w.speed = WEATHER_SPEED[weather] * (0.7f + 0.6f * WindNoise(time / 150.0f, seed ^ 0x5EEDu));
		w.gustiness = WEATHER_GUSTS[weather];
	}
	return w;
}

Vector3 WindView::particle() const
{
	if (!on)
		return Vector3(0.3f, 0.0f, 0.1f);
	const float v = 0.6f * std::min(speed, 30.0f);
	return Vector3(dir.x * v, 0.0f, dir.z * v);
}

float WindView::flagRate() const
{
	return std::clamp(0.5f + 0.1f * now(), 0.45f, 2.2f);
}
