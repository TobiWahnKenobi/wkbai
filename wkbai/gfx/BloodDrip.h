// wkbai - drips from HD models (wkbai's own; docs/engine/gltf.md "Drips" in the RE repo)
// Licensed under the GNU General Public License 3
//
// Drops that form at a point of a model, fall under gravity and splash on the surface below,
// leaving a small glossy pool that fades: the Black Mass's blood from the funnel under its body
// onto the altar. From a glTF's "wkDrip" nodes (Mesh::dripEmitters). A slow irregular rhythm
// (each gap between drops 0.5 to 1.5 times 1 / rate), day and night.
//
// Visual only, like the chimney smoke: each frame's drops are worked out from the game time, the
// emitter and a hash (no state, no rand()), so nothing reaches the simulation and a replay looks
// the same every time. A handful of sprites per drip: the forming drop, the falling one, a few
// splash droplets and the pools. Drawn in the particle pass without the soft-particle fade (the
// drops are a few centimetres: the fade over metres would hide them).

#pragma once

#include <cstdint>
#include <vector>
#include "../mesh.h"

struct Model;
struct Matrix;
struct Camera;
struct IRenderer;

namespace BloodDrip {
	// one sprite this frame
	struct Sprite {
		enum Kind : uint8_t { FORMING, FALLING, SPLASH, POOL };
		Vector3 position;
		Vector3 axis;   // FALLING: its direction of travel (the sprite stretched along it)
		float width;    // across, metres (POOL: the pool's diameter)
		float length;   // along the axis (FALLING), else the same as width
		float alpha;    // 0..1
		Kind kind;
	};

	// gravity, metres per second squared
	constexpr float GRAVITY = 9.81f;
	// how long a pool lasts, seconds
	constexpr float POOL_LIFE = 4.0f;

	// the drip's emitters of a model, in its space (a glTF's wkDrip nodes; none for a Mesh3)
	const std::vector<DripEmitter>& EmittersOf(Model* model);
	// the sprites of a drip in world space (from, to: where the drops form and land) at a game
	// time (seed: the object and emitter)
	void Sprites(const DripEmitter& e, const Vector3& from, const Vector3& to, uint32_t seed, float time,
		std::vector<Sprite>& out);
	// the birth time of drop i of a drip (tests: the gaps)
	float BirthOf(const DripEmitter& e, uint32_t seed, long long i);

	// this frame's: a drawn object's model at its transform (buildings only)
	void Add(Model* model, const Matrix& transform, uint32_t objid);
	// in the particle pass (BeginParticles and the batch on, the soft fade off): draws this
	// frame's and forgets them
	void Draw(IRenderer* gfx, const Camera& camera, float time);
	// the drips added this frame (tests)
	size_t PendingCount();
	void Clear();
}
