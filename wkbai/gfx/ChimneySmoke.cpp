// wkbai - chimney smoke (ChimneySmoke.h; docs/engine/gltf.md "Chimney smoke" in the RE repo)
// Licensed under the GNU General Public License 3

#include "ChimneySmoke.h"
#include "renderer.h"
#include "bitmap.h"
#include "../Model.h"
#include "../Camera.h"
#include "../Environment.h"
#include "../settings.h"
#include <nlohmann/json.hpp>
#include "../util/StriCompare.h"
#include <cstring>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <unordered_map>

namespace ChimneySmoke {
namespace {
	std::string Lower(std::string s)
	{
		for (char& c : s)
			c = c == '/' ? '\\' : (char)std::tolower((unsigned char)c);
		return s;
	}
	bool EndsWith(const std::string& s, const char* tail)
	{
		const size_t n = strlen(tail);
		return s.size() >= n && s.compare(s.size() - n, n, tail) == 0;
	}

	// a hash to 0..1 (the same on every machine)
	float Hash01(uint32_t x)
	{
		x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
		return (x & 0xFFFFFF) / 16777216.0f;
	}
	uint32_t Mix(uint32_t a, uint32_t b) { return a * 0x9E3779B1U ^ (b + 0x7F4A7C15U + (a << 6) + (a >> 2)); }

	float Saturate(float x) { return x < 0.0f ? 0.0f : x > 1.0f ? 1.0f : x; }

	// per model (its file kept: a model freed with its game set and another made at its address)
	struct Cached { std::string file; std::vector<SmokeEmitter> list; };
	std::unordered_map<Model*, Cached> s_cache;

	struct Pending { Vector3 world; uint32_t seed; SmokeEmitter e; };
	std::vector<Pending> s_pending;

	// a puff: a soft round blob with a cloudy edge (value noise), white, its shape in the alpha
	texture PuffTexture(IRenderer* gfx)
	{
		static texture tex = nullptr;
		static IRenderer* owner = nullptr;
		if (tex && owner == gfx)
			return tex;
		constexpr int N = 64, G = 8;
		float grid[G + 1][G + 1];
		for (int y = 0; y <= G; y++)
			for (int x = 0; x <= G; x++)
				grid[y][x] = Hash01(Mix(x % G, y % G + 101));
		auto noise = [&](float u, float v, int octave) {
			const int g = G >> (2 - octave);
			const float fx = u * g, fy = v * g;
			const int ix = std::min((int)fx, g - 1), iy = std::min((int)fy, g - 1);
			const float tx = fx - ix, ty = fy - iy;
			const float sx = tx * tx * (3 - 2 * tx), sy = ty * ty * (3 - 2 * ty);
			const int k = G / g;
			auto at = [&](int x, int y) { return grid[(y % g) * k][(x % g) * k]; };
			const float a = at(ix, iy) + (at(ix + 1, iy) - at(ix, iy)) * sx;
			const float b = at(ix, iy + 1) + (at(ix + 1, iy + 1) - at(ix, iy + 1)) * sx;
			return a + (b - a) * sy;
		};
		Bitmap bmp;
		bmp.width = bmp.height = N;
		bmp.format = BMFORMAT_R8G8B8A8;
		bmp.pixels.resize(N * N * 4);
		for (int y = 0; y < N; y++)
			for (int x = 0; x < N; x++) {
				const float u = (x + 0.5f) / N, v = (y + 0.5f) / N;
				const float dx = u * 2 - 1, dy = v * 2 - 1;
				const float r = std::sqrt(dx * dx + dy * dy);
				const float n = 0.5f * noise(u, v, 0) + 0.3f * noise(u, v, 1) + 0.2f * noise(u, v, 2);
				// round and soft, its edge eaten by the noise
				const float edge = Saturate((1.0f - r) * 1.6f - (1.0f - n) * 0.55f);
				const float a = edge * edge * (3 - 2 * edge) * (0.75f + 0.25f * n);
				uint8_t* p = &bmp.pixels[(y * N + x) * 4];
				const uint8_t shade = (uint8_t)std::lround(215 + 40 * n); // a little relief
				p[0] = p[1] = p[2] = shade;
				p[3] = (uint8_t)std::lround(Saturate(a) * 255.0f);
			}
		tex = gfx->CreateTexture(bmp, 0);
		owner = gfx;
		return tex;
	}
}

SmokeEmitter::When DefaultWhen(const std::string& modelFile)
{
	// the forge burns day and night
	if (Lower(modelFile).find("\\blacksmith\\") != std::string::npos)
		return SmokeEmitter::ALWAYS;
	return SmokeEmitter::NIGHT_WINTER;
}

std::vector<SmokeEmitter> TableEmitters(const std::string& modelFile)
{
	// the houses have no PS_Chimney point: their chimney tops (the top of each chimney stack's
	// square in the Mesh3, a little above it)
	struct Row { const char* file; Vector3 at; };
	static const Row rows[] = {
		{ "buildings\\houses\\house01\\default.mesh3", Vector3(-0.22f, 7.0f, 2.14f) },
		{ "buildings\\houses\\house02\\default.mesh3", Vector3(0.31f, 7.0f, -0.72f) },
		{ "buildings\\houses\\house03\\default.mesh3", Vector3(-1.10f, 9.0f, -0.81f) },
	};
	const std::string f = Lower(modelFile);
	std::vector<SmokeEmitter> out;
	for (const Row& r : rows)
		if (EndsWith(f, r.file)) {
			SmokeEmitter e;
			e.position = r.at;
			e.size = 0.8f; // a cottage's
			e.rate = 0.9f;
			out.push_back(e);
		}
	return out;
}

const std::vector<SmokeEmitter>& EmittersOf(Model* model)
{
	static const std::vector<SmokeEmitter> none;
	// (an animated building's mesh: its static model, its file the mesh's)
	StaticModel* sm = model->getStaticModel();
	if (!sm)
		return none;
	auto [it, isNew] = s_cache.try_emplace(model);
	if (!isNew && it->second.file == sm->fileName)
		return it->second.list;
	it->second.file = sm->fileName;
	std::vector<SmokeEmitter>& out = it->second.list;
	out.clear();
	Mesh& mesh = sm->getMesh();
	if (mesh.smokeDeclared) {
		out = mesh.smokeEmitters;
		return out;
	}
	const SmokeEmitter::When when = DefaultWhen(sm->fileName);
	for (size_t i = 0; i < mesh.attachPoints.size(); i++) {
		const AttachmentPoint& ap = mesh.attachPoints[i];
		if (StrCICompare(ap.tag, "PS_Chimney") != 0)
			continue;
		SmokeEmitter e;
		e.position = ap.staticState.position + Vector3(0, 0.15f, 0); // (just under the stack's top)
		e.when = when;
		out.push_back(e);
	}
	if (out.empty())
		out = TableEmitters(sm->fileName);
	return out;
}

float Strength(SmokeEmitter::When when, const EnvironmentView& v)
{
	if (!v.active || when == SmokeEmitter::ALWAYS)
		return 1.0f;
	// night: from the daylight at half (dusk) to full at 0.15
	const float night = Saturate((0.5f - v.sun.daylight) / 0.35f);
	if (when == SmokeEmitter::DAY)
		return 1.0f - night;
	// winter (the last quarter of the year, faded in and out over a few days' worth), or cold
	const float yf = v.yearFraction - std::floor(v.yearFraction);
	const float winter = Saturate((yf - 0.75f) / 0.015f) * Saturate((1.0f - yf) / 0.015f);
	const float cold = Saturate((4.0f - v.temperatureC) / 8.0f);
	return std::max(night, std::max(winter, cold));
}

void Puffs(const SmokeEmitter& e, const Vector3& world, uint32_t seed, float time, float strength,
	const Vector3& wind, std::vector<Puff>& out)
{
	if (strength <= 0.0f)
		return;
	const float rate = std::clamp(e.rate, 0.1f, 10.0f) * 1.3f;
	const float interval = 1.0f / rate;
	const float phase = Hash01(seed);
	// puff i is born at (i + phase) * interval
	const long long last = (long long)std::floor(time / interval - phase);
	const long long first = (long long)std::floor((time - LIFE) / interval - phase) + 1;
	const Vector3 flatWind(wind.x, 0.0f, wind.z);
	for (long long i = first; i <= last; i++) {
		const float age = time - ((float)i + phase) * interval;
		if (age < 0.0f || age >= LIFE)
			continue;
		const uint32_t h = Mix(seed, (uint32_t)i);
		const float h1 = Hash01(h), h2 = Hash01(h + 1), h3 = Hash01(h + 2), h4 = Hash01(h + 3), h5 = Hash01(h + 4);
		// thinned as the rule fades (dusk, dawn, the season's turn): fewer puffs, fainter
		if (h5 > strength * 1.25f)
			continue;
		const float t = age / LIFE;
		// rises fast out of the chimney, slowing (warm air cooling), a little buoyancy left
		const float tau = 2.5f;
		const float up = (1.0f + 0.5f * h1) * tau * (1.0f - std::exp(-age / tau)) + 0.12f * age;
		// taken by the wind once out of the stack's shelter
		const float lag = 1.2f;
		const float drift = 0.85f * (age - lag * (1.0f - std::exp(-age / lag)));
		// and wandering
		const Vector3 wander((h2 - 0.5f) * 0.45f * age + 0.25f * std::sin(age * 0.9f + h3 * 6.283f),
			0.0f, (h3 - 0.5f) * 0.45f * age + 0.25f * std::cos(age * 0.8f + h2 * 6.283f));
		Puff p;
		p.position = world + Vector3(0, up, 0) + flatWind * drift + wander;
		p.size = std::max(e.size, 0.1f) * (1.0f + 0.55f * age) * (0.85f + 0.3f * h4);
		const float fadeIn = Saturate(age / 0.7f);
		const float fadeOut = std::pow(1.0f - t, 1.6f);
		p.alpha = 0.8f * fadeIn * fadeOut * (0.55f + 0.45f * Saturate(strength));
		p.age = age;
		p.angle = h1 * 6.283f + age * (h2 - 0.5f) * 0.4f;
		out.push_back(p);
	}
}

void Add(Model* model, const Matrix& transform, uint32_t objid)
{
	if (!model || s_pending.size() > 20000) // (never drawn: no particle pass)
		return;
	const std::vector<SmokeEmitter>& list = EmittersOf(model);
	for (size_t i = 0; i < list.size(); i++)
		s_pending.push_back({ list[i].position.transform(transform), Mix(objid, (uint32_t)i + 1), list[i] });
}

size_t PendingCount() { return s_pending.size(); }
void Clear() { s_pending.clear(); }

void Draw(IRenderer* gfx, const Camera& camera, float time)
{
	std::vector<Pending> pending;
	pending.swap(s_pending);
	static const bool enabled = g_settings.value<bool>("chimneySmoke", true);
	if (!enabled || pending.empty())
		return;
	const EnvironmentView& v = g_environment;
	float strength[3];
	for (int w = 0; w < 3; w++)
		strength[w] = Strength((SmokeEmitter::When)w, v);
	if (strength[0] <= 0.0f && strength[1] <= 0.0f && strength[2] <= 0.0f)
		return;

	// the light on the smoke: the sun's and the sky's by day, the moon's dim blue at night
	float light[3] = { 1.0f, 1.0f, 1.0f };
	float night = 0.0f;
	if (v.active) {
		const float day = Saturate(v.sun.daylight);
		night = 1.0f - day;
		static const float moon[3] = { 0.75f, 0.85f, 1.0f };
		const float moonLevel = 0.4f + 0.15f * Saturate(v.sun.moonLight);
		for (int c = 0; c < 3; c++)
			light[c] = day * (0.55f * v.sun.colour[c] + 0.45f * v.sun.ambient[c]) + night * moonLevel * moon[c];
		// overcast: flatter, a little darker
		for (int c = 0; c < 3; c++)
			light[c] *= 1.0f - 0.15f * Saturate(v.cloudCover);
	}

	static std::unique_ptr<RBatch> batch;
	if (!batch)
		batch.reset(gfx->CreateBatch(4 * 512, 6 * 512));
	gfx->SetTransformMatrix(&camera.sceneMatrix);
	gfx->SetTexture(0, PuffTexture(gfx));

	const Vector3 dir = camera.direction.normal();
	const Vector3 camside = dir.cross(Vector3(0, 1, 0)).normal();
	const Vector3 camup = camside.cross(dir).normal();
	const float reach = std::max(camera.farDist, 50.0f) + 20.0f;
	std::vector<Puff> puffs;
	// WKB_TRACE_SMOKE: the emitters and puffs drawn, every 300 frames
	static const bool trace = getenv("WKB_TRACE_SMOKE") != nullptr;
	static int frame = 0;
	size_t drawn = 0;
	for (const Pending& p : pending) {
		const float s = strength[p.e.when];
		if (s <= 0.0f)
			continue;
		const Vector3 rel = p.world - camera.position;
		const float along = rel.dot(dir);
		if (along < -15.0f || along > reach)
			continue;
		puffs.clear();
		Puffs(p.e, p.world, p.seed, time, s, v.wind, puffs);
		for (const Puff& f : puffs) {
			// young smoke greyer, thinning to white; the forge's lit orange from below at night
			const float t = Saturate(f.age / 3.0f);
			const float grey = 0.62f + 0.3f * t;
			float rgb[3];
			for (int c = 0; c < 3; c++)
				rgb[c] = grey * light[c];
			if (p.e.when == SmokeEmitter::ALWAYS && night > 0.0f) {
				const float glow = night * Saturate(1.0f - f.age / 1.6f) * 0.55f;
				rgb[0] += glow * 1.0f; rgb[1] += glow * 0.45f; rgb[2] += glow * 0.15f;
			}
			auto byte = [](float x) { return (uint32_t)std::lround(Saturate(x) * 255.0f); };
			const uint32_t colour = byte(f.alpha) << 24 | byte(rgb[0]) << 16 | byte(rgb[1]) << 8 | byte(rgb[2]);
			const float ca = std::cos(f.angle) * f.size * 0.5f, sa = std::sin(f.angle) * f.size * 0.5f;
			const Vector3 a = camside * ca + camup * sa, b = camup * ca - camside * sa;
			// (the winding DefaultParticleRenderer's: the other way round is culled)
			const Vector3 corner[4] = { f.position + a + b, f.position + a - b, f.position - a - b, f.position - a + b };
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
		}
		drawn += puffs.size();
	}
	if (trace && frame++ % 300 == 0)
		printf("SMOKE drawn: %zu emitters, %zu puffs, strength %.2f %.2f %.2f (night_winter always day)\n",
			pending.size(), drawn, strength[0], strength[1], strength[2]);
	batch->flush();
	gfx->NoTexture(0);
}
}
