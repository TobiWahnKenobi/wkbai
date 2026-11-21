// wkbai - fire effects (RenderSettings::fireQuality)
// Licensed under the GNU General Public License 3
//
// The fires' look: burning buildings (the game set's "Building Fire" effects at their MI_ points,
// more of them the more a building is damaged), flaming arrows and projectiles, the fire spells
// and the map's fires. They are the original's particle systems (Fire_Building, Fire, Mapfire,
// Flame_Arrow, Fireball ...), unchanged: where and when each particle is, its size and its alpha
// along its life come from them, so every fire source is covered and the game is untouched.
//  - Original: the original's Fire.TGA (Fireball.TGA ...) sprites, as before
//    (DefaultParticleRenderer).
//  - Low: each fire particle drawn as a procedural flame instead of its sprite: value noise
//    rising through a teardrop, its temperature (the particle's age, the noise, a building's
//    damage) coloured along the black-body curve (Planck's law at 610, 550 and 465 nm); smoke
//    rising from a standing fire's flames (a burning building's).
//  - High: finer flames (more octaves, the noise domain-warped by noise, after Quilez), a glow
//    around each fire, the air shimmering above it (the frame behind, displaced), a thicker
//    column of smoke, the fire lights (RenderSettings::fireLights) flickering with
//    them and stronger and wider the more a building is damaged.
//  Low and High: the fire lights (a burning building's light on the ground and the models) more
//  orange, flickering and swaying.
// Visual only: worked out each frame from the particles and the game time (no state, no rand()),
// so nothing reaches the simulation and replays stay the same. Low and High need the enhanced
// renderers on Diligent; without them the fires are Original's.

#pragma once

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>
#include "../util/vecmat.h"

struct IRenderer;
struct Camera;
struct Particle;
struct ParticleSystem;

namespace FireFx {
	enum Quality { ORIGINAL = 0, LOW = 1, HIGH = 2, LEVELS = 3 };
	// "Original", "Low", "High" (the options menu's choices)
	const char* QualityName(int quality);

	// at the start of a frame's scene: the level in effect (the setting, Original when the
	// renderer can't draw the others); Original forgets the damage set
	void BeginFrame(int quality, bool available);
	int Level();

	// whether a particle of this system is drawn by the fire shader at this level (Low and High:
	// the flame-sprite fires, ParticleSystem::flameSprite); Original: none
	bool Takes(int level, const ParticleSystem* system);

	// a building's damage, its structural integrity as a share of its capacity (0..1): how much
	// hotter, bigger and smokier its fires burn (1 intact .. 1.6 at none)
	float DamageScale(float integrity);
	// this frame's integrity of a burning object (the fires attached to it carry its id)
	void SetDamage(uint32_t objid, float integrity);
	float DamageOf(uint32_t objid); // DamageScale of what was set, 1 for an object without

	// the burning buildings' fires the weather is putting out (Client::fireShares: id, the share
	// of the fire left, 0..1; ServerEnvironment.h): their flames that much smaller and fainter, at
	// every level (Original: the sprites, DefaultParticleRenderer). Null: none.
	void SetShares(const std::unordered_map<uint32_t, float>* shares);
	float ShareOf(uint32_t objid); // 1 for an object without
	bool HasShare(uint32_t objid);
	// a flame's size and its alpha for a share left (1 at 1; 0.35 and 0.5 nearly out)
	inline float ShareSize(float share) { return 0.35f + 0.65f * share; }
	inline float ShareAlpha(float share) { return 0.5f + 0.5f * share; }

	// a fire's flicker about 1 (0.7..1.3) at a game time; seed: which fire (the same every time)
	float Flicker(float time, uint32_t seed);
	// where a fire's light is about its fire at a game time (metres, about 0.5 at most each way):
	// it sways with the flames
	Vector3 Sway(float time, uint32_t seed);
	// a stable seed for a particle (its start)
	uint32_t SeedOf(const Particle& p);

	// one flame this frame (Add, from the particle renderer)
	struct Flame {
		Vector3 position;  // the particle's
		Vector3 emitter;   // where it started (a standing fire's emitter)
		float size;        // the original sprite's size (its Scale_Gradient)
		float steady;      // and the fire's, for its smoke, glow and shimmer: its system's size late in a
		                   // particle's life, not the live particles' (their largest dying, the smoke
		                   // jumped narrower every second)
		float alpha;       // its colour's alpha, 0..1 (its Colour_Fn)
		float life;        // its age over its maximum age, 0..1
		float heat;        // DamageScale of its object
		uint32_t seed;
		uint32_t objid;
		const ParticleSystem* system;
	};
	void Add(const Particle& p, float now, float size, float alpha);
	size_t PendingCount();
	const std::vector<Flame>& Pending();

	// the shader's instances for the pending flames at a level (tests; Draw's): per instance three
	// float4s (position, width; alpha, life, seed, heat; kind, height / width, lift, extra),
	// kinds 0 flame, 1 glow, 2 smoke, 3 shimmer, in the order drawn
	enum Kind { FLAME = 0, GLOW = 1, SMOKE = 2, SHIMMER = 3 };
	struct Instance { float a[4], b[4], c[4]; };
	void BuildInstances(int level, float time, bool shimmer, std::vector<Instance>& out);

	// in the particle pass, after the particle renderer (soft particles on): draws this frame's
	// fires and forgets them
	void Draw(IRenderer* gfx, const Camera& camera, float time);
	void Clear();

	// tests: the renderer can draw the fire levels (else Level() is Original without Diligent)
	extern bool s_forceAvailable;
}
