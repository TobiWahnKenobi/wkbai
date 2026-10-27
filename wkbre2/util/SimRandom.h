// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#pragma once

#include <cstdint>
#include <cstdlib>

// The simulation's random numbers: seeded from the game (RANDOM_SEED in a save) and saved with
// it, so that a game goes on the same from the same state (replays, lockstep, AI tests). MSVC's
// rand() generator and range (0..SIM_RAND_MAX), so the scripts' arithmetic stays as it was.
// Only the server's simulation uses it; sounds, particles and menus keep using rand().
constexpr int SIM_RAND_MAX = 0x7FFF;

struct SimRandom {
	uint32_t state = 1;
	void seed(uint32_t s) { state = s; }
	int next() {
		state = state * 214013u + 2531011u;
		return (int)((state >> 16) & 0x7FFF);
	}
};

extern SimRandom g_simRandom;

inline int SimRand() { return g_simRandom.next(); }

// For script values that also run on clients: the simulation's numbers only on the server
template <typename Context> int ScriptRand(Context* ctx) { return ctx->isServer() ? SimRand() : rand(); }
