// wkbai - drips from HD models (BloodDrip.h; docs/engine/gltf.md "Drips" in the RE repo)
// Licensed under the GNU General Public License 3

#include "BloodDrip.h"
#include "renderer.h"
#include "bitmap.h"
#include "../Model.h"
#include "../Camera.h"
#include "../Environment.h"
#include "../settings.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <unordered_map>

namespace BloodDrip {
namespace {
	// a hash to 0..1 (the same on every machine)
	float Hash01(uint32_t x)
	{
		x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
		return (x & 0xFFFFFF) / 16777216.0f;
	}
	uint32_t Mix(uint32_t a, uint32_t b) { return a * 0x9E3779B1U ^ (b + 0x7F4A7C15U + (a << 6) + (a >> 2)); }
	float Saturate(float x) { return x < 0.0f ? 0.0f : x > 1.0f ? 1.0f : x; }
	float Smooth(float x) { x = Saturate(x); return x * x * (3 - 2 * x); }

	// a drop about 6 cm across at size 1
	constexpr float DROP = 0.06f;
	constexpr int SPLASH_DROPLETS = 5;

	struct Cached { std::string file; std::vector<DripEmitter> list; };
	std::unordered_map<Model*, Cached> s_cache;

	struct Pending { Vector3 from, to; uint32_t seed; DripEmitter e; };
	std::vector<Pending> s_pending;

	// a drop: a round disc, soft at the edge, a little darker towards the rim (rounded)
	texture DropTexture(IRenderer* gfx)
	{
		static texture tex = nullptr;
		static IRenderer* owner = nullptr;
		if (tex && owner == gfx)
			return tex;
		constexpr int N = 32;
		Bitmap bmp;
		bmp.width = bmp.height = N;
		bmp.format = BMFORMAT_R8G8B8A8;
		bmp.pixels.resize(N * N * 4);
		for (int y = 0; y < N; y++)
			for (int x = 0; x < N; x++) {
				const float dx = (x + 0.5f) / N * 2 - 1, dy = (y + 0.5f) / N * 2 - 1;
				const float r = std::sqrt(dx * dx + dy * dy);
				const float a = Smooth((1.0f - r) * 5.0f);
				const uint8_t shade = (uint8_t)std::lround(255.0f * (1.0f - 0.45f * Saturate(r * r)));
				uint8_t* p = &bmp.pixels[(y * N + x) * 4];
				p[0] = p[1] = p[2] = shade;
				p[3] = (uint8_t)std::lround(a * 255.0f);
			}
		tex = gfx->CreateTexture(bmp, 0);
		owner = gfx;
		return tex;
	}

	float FormTime(const DripEmitter& e) { return std::min(1.1f, 0.45f / std::clamp(e.rate, 0.05f, 5.0f)); }
}

float BirthOf(const DripEmitter& e, uint32_t seed, long long i)
{
	// a slot of 1 / rate per drop, the drop anywhere in its middle half: gaps 0.5 to 1.5 slots
	const float interval = 1.0f / std::clamp(e.rate, 0.05f, 5.0f);
	const float phase = Hash01(seed);
	return ((float)i + phase + 0.5f * (Hash01(Mix(seed, (uint32_t)i)) - 0.5f)) * interval;
}

const std::vector<DripEmitter>& EmittersOf(Model* model)
{
	static const std::vector<DripEmitter> none;
	StaticModel* sm = model->getStaticModel();
	if (!sm)
		return none;
	auto [it, isNew] = s_cache.try_emplace(model);
	if (!isNew && it->second.file == sm->fileName)
		return it->second.list;
	it->second.file = sm->fileName;
	it->second.list = sm->getMesh().dripEmitters;
	return it->second.list;
}

void Sprites(const DripEmitter& e, const Vector3& from, const Vector3& to, uint32_t seed, float time,
	std::vector<Sprite>& out)
{
	const float height = std::max(from.y - to.y, 0.01f);
	const float fallTime = std::sqrt(2.0f * height / GRAVITY);
	const float form = FormTime(e);
	const float life = form + fallTime + POOL_LIFE;
	const float interval = 1.0f / std::clamp(e.rate, 0.05f, 5.0f);
	const float r = DROP * std::max(e.size, 0.1f);
	const long long last = (long long)std::floor(time / interval) + 1;
	const long long first = (long long)std::floor((time - life) / interval) - 1;
	for (long long i = first; i <= last; i++) {
		const float age = time - BirthOf(e, seed, i);
		if (age < 0.0f || age >= life)
			continue;
		const uint32_t h = Mix(seed ^ 0x5bd1e995U, (uint32_t)i);
		const float big = 0.85f + 0.3f * Hash01(h);
		if (age < form) {
			// swelling at the tip, hanging lower as it grows, a neck in the last moment
			const float s = Smooth(age / form);
			const float w = r * big * (0.35f + 0.65f * s);
			const float neck = Saturate((age / form - 0.8f) / 0.2f);
			Sprite p;
			p.kind = Sprite::FORMING;
			p.position = from - Vector3(0, w * 0.45f + neck * w * 0.35f, 0);
			p.axis = Vector3(0, -1, 0);
			p.width = w;
			p.length = w * (1.0f + 0.5f * neck);
			p.alpha = 0.95f * Smooth(age / (form * 0.3f));
			out.push_back(p);
			continue;
		}
		const float t = age - form;
		if (t < fallTime) {
			// falling (the xz from the tip to where it lands), drawn stretched along its speed
			const float k = t / fallTime;
			const float speed = GRAVITY * t;
			Sprite p;
			p.kind = Sprite::FALLING;
			p.position = Vector3(from.x + (to.x - from.x) * k, from.y - 0.5f * GRAVITY * t * t, from.z + (to.z - from.z) * k);
			p.axis = (Vector3((to.x - from.x) / fallTime, -speed, (to.z - from.z) / fallTime)).normal();
			p.width = r * big * 0.85f;
			p.length = r * big * 1.2f + speed * 0.035f; // (a little motion blur)
			p.alpha = 0.95f;
			out.push_back(p);
			continue;
		}
		// landed: droplets thrown up and out, and a pool that spreads and fades
		const float since = t - fallTime;
		const float impact = std::sqrt(2.0f * GRAVITY * height);
		for (int d = 0; d < SPLASH_DROPLETS; d++) {
			const uint32_t hd = Mix(h, (uint32_t)d + 17);
			const float ang = (d + Hash01(hd)) * 6.2831853f / SPLASH_DROPLETS;
			const float out_ = (0.35f + 0.5f * Hash01(hd + 1)) * std::min(impact, 9.0f) * 0.18f;
			const float up = (0.5f + 0.6f * Hash01(hd + 2)) * std::min(impact, 9.0f) * 0.2f;
			const float y = up * since - 0.5f * GRAVITY * since * since;
			if (y < 0.0f && since > 0.02f)
				continue;
			Sprite p;
			p.kind = Sprite::SPLASH;
			p.position = to + Vector3(std::cos(ang) * out_ * since, std::max(y, 0.0f) + 0.01f, std::sin(ang) * out_ * since);
			p.axis = Vector3(0, -1, 0);
			p.width = p.length = r * (0.3f + 0.2f * Hash01(hd + 3));
			p.alpha = 0.9f;
			out.push_back(p);
		}
		Sprite pool;
		pool.kind = Sprite::POOL;
		pool.position = to + Vector3(0, 0.012f, 0);
		pool.axis = Vector3(0, 1, 0);
		pool.width = pool.length = r * big * (1.2f + 2.6f * Smooth(since / 0.35f));
		pool.alpha = 0.85f * std::pow(1.0f - Saturate(since / POOL_LIFE), 1.5f);
		out.push_back(pool);
	}
}

void Add(Model* model, const Matrix& transform, uint32_t objid)
{
	if (!model || s_pending.size() > 4000) // (never drawn: no particle pass)
		return;
	const std::vector<DripEmitter>& list = EmittersOf(model);
	for (size_t i = 0; i < list.size(); i++)
		s_pending.push_back({ list[i].position.transform(transform), list[i].landing.transform(transform),
			Mix(objid, (uint32_t)i + 101), list[i] });
}

size_t PendingCount() { return s_pending.size(); }
void Clear() { s_pending.clear(); }

void Draw(IRenderer* gfx, const Camera& camera, float time)
{
	std::vector<Pending> pending;
	pending.swap(s_pending);
	static const bool enabled = g_settings.value<bool>("drips", true);
	if (!enabled || pending.empty())
		return;

	// the light on the blood: the sun's and the sky's by day, the moon's dim blue at night
	const EnvironmentView& v = g_environment;
	float light[3] = { 1.0f, 1.0f, 1.0f }, gloss[3] = { 1.0f, 1.0f, 1.0f };
	if (v.active) {
		const float day = Saturate(v.sun.daylight), night = 1.0f - day;
		static const float moon[3] = { 0.75f, 0.85f, 1.0f };
		const float moonLevel = 0.35f + 0.15f * Saturate(v.sun.moonLight);
		for (int c = 0; c < 3; c++) {
			light[c] = day * (0.6f * v.sun.colour[c] + 0.4f * v.sun.ambient[c]) + night * moonLevel * moon[c];
			gloss[c] = day * v.sun.colour[c] + night * moonLevel * moon[c];
		}
	}

	static std::unique_ptr<RBatch> batch;
	if (!batch)
		batch.reset(gfx->CreateBatch(4 * 256, 6 * 256));
	gfx->SetTransformMatrix(&camera.sceneMatrix);
	gfx->SetTexture(0, DropTexture(gfx));

	const Vector3 dir = camera.direction.normal();
	const Vector3 camside = dir.cross(Vector3(0, 1, 0)).normal();
	const Vector3 camup = camside.cross(dir).normal();
	// the pools lie flat: the screen's up laid on the ground
	Vector3 flatUp(camup.x, 0.0f, camup.z);
	if (flatUp.sqlen3() < 1e-6f)
		flatUp = Vector3(dir.x, 0.0f, dir.z);
	flatUp = flatUp.normal();
	const Vector3 flatSide = Vector3(camside.x, 0.0f, camside.z).normal();
	auto byte = [](float x) { return (uint32_t)std::lround(Saturate(x) * 255.0f); };
	auto quad = [&](const Vector3& at, const Vector3& a, const Vector3& b, uint32_t colour) {
		// (the winding DefaultParticleRenderer's: the other way round is culled)
		const Vector3 corner[4] = { at + a + b, at + a - b, at - a - b, at - a + b };
		batchVertex* verts; uint16_t* indices; uint32_t firstIndex;
		batch->next(4, 6, &verts, &indices, &firstIndex);
		for (int k = 0; k < 4; k++) {
			verts[k].x = corner[k].x; verts[k].y = corner[k].y; verts[k].z = corner[k].z;
			verts[k].color = (int)colour;
			verts[k].u = (k == 1 || k == 2) ? 1.0f : 0.0f;
			verts[k].v = (k >= 2) ? 0.0f : 1.0f;
		}
		const uint16_t order[6] = { 0, 3, 1, 1, 3, 2 };
		for (int k = 0; k < 6; k++)
			indices[k] = (uint16_t)(firstIndex + order[k]);
	};

	const float reach = std::min(std::max(camera.farDist, 50.0f), 400.0f);
	std::vector<Sprite> sprites;
	static const bool trace = getenv("WKB_TRACE_DRIP") != nullptr;
	static int frame = 0;
	size_t drawn = 0;
	for (const Pending& p : pending) {
		const Vector3 rel = p.from - camera.position;
		const float along = rel.dot(dir);
		if (along < -10.0f || along > reach)
			continue;
		sprites.clear();
		Sprites(p.e, p.from, p.to, p.seed, time, sprites);
		for (const Sprite& s : sprites) {
			// the pools a shade darker (soaking in), the drops their colour
			const float body = s.kind == Sprite::POOL ? 0.8f : 1.0f;
			const uint32_t colour = byte(s.alpha) << 24 | byte(p.e.colour[0] * light[0] * body) << 16
				| byte(p.e.colour[1] * light[1] * body) << 8 | byte(p.e.colour[2] * light[2] * body);
			Vector3 a, b, toEye;
			if (s.kind == Sprite::POOL) {
				a = flatSide * (s.width * 0.5f);
				b = flatUp * (s.width * 0.5f);
			}
			else {
				// along the axis (up the screen), across it facing the camera
				const Vector3 up = -s.axis;
				Vector3 across = dir.cross(up);
				if (across.sqlen3() < 1e-6f)
					across = camside;
				a = across.normal() * (s.width * 0.5f);
				b = up * (s.length * 0.5f);
			}
			quad(s.position, a, b, colour);
			// the gloss: a small bright spot up and to the side, the light's colour
			const float spot = s.kind == Sprite::POOL ? 0.22f : 0.3f;
			const float ga = s.kind == Sprite::POOL ? 0.45f * s.alpha : 0.7f * s.alpha;
			const uint32_t gc = byte(ga) << 24 | byte(0.9f * gloss[0]) << 16 | byte(0.85f * gloss[1]) << 8 | byte(0.8f * gloss[2]);
			const Vector3 an = a.normal(), bn = b.normal();
			const Vector3 at = s.position - an * (s.width * 0.18f) + bn * (s.length * 0.2f) - dir * 0.002f;
			quad(at, an * (s.width * spot * 0.5f), bn * (s.width * spot * 0.5f), gc);
			drawn += 2;
		}
	}
	if (trace && frame++ % 300 == 0)
		printf("DRIP drawn: %zu drips, %zu sprites\n", pending.size(), drawn);
	batch->flush();
	gfx->NoTexture(0);
}
}
