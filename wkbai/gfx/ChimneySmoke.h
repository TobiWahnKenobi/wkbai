// wkbai - chimney smoke (wkbai's own; docs/engine/gltf.md "Chimney smoke" in the RE repo)
// Licensed under the GNU General Public License 3
//
// Smoke from the buildings' chimneys, by rules: at night and in winter (the default: houses, the
// Manor, the Village, the Tavern ...), always (the Blacksmith's forge), or only by day. Visual
// only: the puffs are worked out each frame from the game time, the emitter and a hash (no state,
// no rand()), so nothing reaches the simulation and a replay looks the same every time.
//
// Where a model's smoke comes out:
//  - a glTF's nodes with "wkSmoke" (Mesh::smokeEmitters), which replace the rest;
//  - else the Mesh3's PS_Chimney attachment points (the original's Chimney.PSystem points), their
//    rule from the building type (DefaultWhen);
//  - else the table of models without either (the houses: their chimney tops, TableEmitters).
// Drawn in the particle pass through the batch (soft particles and fog on Diligent), lit by the
// sun and the sky, drifting with g_environment.wind.

#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include "../mesh.h"

struct Model;
struct Matrix;
struct Camera;
struct IRenderer;
struct EnvironmentView;

namespace ChimneySmoke {
	// one puff this frame
	struct Puff {
		Vector3 position;
		float size;     // across, metres
		float alpha;    // 0..1
		float age;      // seconds
		float angle;    // its sprite's turn, radians
	};

	// the emitters of a model, in its space (cached per model)
	const std::vector<SmokeEmitter>& EmittersOf(Model* model);
	// the rule of a Mesh3's PS_Chimney points by the model's file (the Blacksmith's: always)
	SmokeEmitter::When DefaultWhen(const std::string& modelFile);
	// models without chimney points of their own: their emitters by file (empty: none)
	std::vector<SmokeEmitter> TableEmitters(const std::string& modelFile);
	// how much a rule smokes now, 0..1 (night: the daylight below about half; winter: the season
	// or a cold below 4 °C); the environment off: 1 (the original's chimneys always smoked)
	float Strength(SmokeEmitter::When when, const EnvironmentView& v);
	// the puffs of an emitter at a world position at a game time (seed: the object and emitter);
	// strength thins and fades them; wind in metres per second
	void Puffs(const SmokeEmitter& e, const Vector3& world, uint32_t seed, float time, float strength,
		const Vector3& wind, std::vector<Puff>& out);
	// the life of a puff, seconds
	constexpr float LIFE = 9.0f;

	// this frame's: a drawn object's model at its transform (buildings only)
	void Add(Model* model, const Matrix& transform, uint32_t objid);
	// in the particle pass (BeginParticles and the batch on): draws this frame's and forgets them
	void Draw(IRenderer* gfx, const Camera& camera, float time);
	// the emitters added this frame (tests)
	size_t PendingCount();
	void Clear();
}
