// wkbai - swim animations made from a unit's walk (SwimAnim.h)
// Licensed under the GNU General Public License 3

#include "SwimAnim.h"
#include "anim.h"
#include "mesh.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <vector>

namespace {
	// how a style moves the walk's vertices (heights as fractions of the model's height H, from
	// its lowest point)
	struct SwimShape {
		float pitch;      // the body tipped forward from the hips, degrees
		float headBack;   // the head (over the neck) held up against that, degrees
		float hip, neck;  // the hips' and the neck's height
		float legTop;     // the legs below this
		float legSwing;   // the legs' fore and aft swing kept (a kick once tipped), 0..1
		float legLift;    // their up and down kept
		float armSwing;   // the upper body's fore and aft swing (a stroke once tipped)
		float hipDepth;   // where the hips end up against the water line (the origin's height 0)
		float back;       // moved back (+z, units face -z) so the body lies over the origin
		float bob;        // up and down, twice a loop
	};

	SwimShape ShapeOf(SwimStyle style)
	{
		switch (style) {
		case SwimStyle::Robed:
			return { 50.0f, 25.0f, 0.55f, 0.84f, 0.50f, 0.45f, 0.5f, 1.1f, -0.20f, -0.06f, 0.012f };
		case SwimStyle::Mounted:
			// (no tip: the horse swims level, its legs below its belly paddling, down to its back)
			return { 0.0f, 0.0f, 0.40f, 1.0f, 0.38f, 0.55f, 0.6f, 1.0f, -0.12f, 0.0f, 0.010f };
		case SwimStyle::Swarm:
			return { 0.0f, 0.0f, 0.40f, 1.0f, 0.40f, 0.40f, 0.4f, 1.0f, -0.35f, 0.0f, 0.020f };
		case SwimStyle::Upright:
		default:
			// (as the Scout's own SWIM1: nearly flat, the head at the water line, the hips and feet
			// a little under it)
			return { 80.0f, 40.0f, 0.55f, 0.84f, 0.50f, 0.55f, 0.6f, 1.2f, -0.08f, -0.12f, 0.012f };
		}
	}

	float Smoothstep(float a, float b, float x)
	{
		const float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
		return t * t * (3.0f - 2.0f * t);
	}

	// turned about the x axis through a point: by a positive angle the up direction leans to -z
	// (forward)
	Vector3 TurnX(const Vector3& p, const Vector3& pivot, float radians)
	{
		const float c = std::cos(radians), s = std::sin(radians);
		const Vector3 q = p - pivot;
		return pivot + Vector3(q.x, q.y * c + q.z * s, -q.y * s + q.z * c);
	}

	// the axis' value of a vertex at a time, as Anim::interpolate (in the file's vertex order)
	float Sample(const Anim::AnimPosCoord& ac, uint32_t time, size_t i)
	{
		auto at = [&](size_t f) {
			const uint32_t q = (ac.verts[f][i / 3] >> (11 * (i % 3))) & 1023;
			return ac.vertadd[f] + ac.vertmul[f] * (q / 1023.0f);
		};
		if (ac.numFrames == 0)
			return 0.0f;
		if (ac.numFrames == 1 || time <= ac.frameTimes[0])
			return at(0);
		size_t f = 0;
		while (f + 1 < ac.numFrames && ac.frameTimes[f + 1] < time)
			f++;
		if (f + 1 >= ac.numFrames)
			return at(ac.numFrames - 1);
		const uint32_t t0 = ac.frameTimes[f], t1 = ac.frameTimes[f + 1];
		const float k = t1 > t0 ? (float)(time - t0) / (float)(t1 - t0) : 0.0f;
		return at(f) + (at(f + 1) - at(f)) * k;
	}

	// a vertex's (or an attachment point's) swimming place: its mean place over the walk, the
	// walk's move from there, the time through the loop (0..1)
	struct Mover {
		SwimShape s;
		float floor, height;
		Vector3 apply(const Vector3& rest, Vector3 d, float phase) const {
			const float h = (rest.y - floor) / height;
			const float leg = 1.0f - Smoothstep(s.legTop - 0.15f, s.legTop + 0.05f, h);
			const float swing = s.armSwing + (s.legSwing - s.armSwing) * leg;
			d.x *= swing; d.z *= swing;
			d.y *= 1.0f + (s.legLift - 1.0f) * leg;
			Vector3 p = rest + d;
			// (the head only: a pike or a staff held up over it stays with the body, not bent back)
			const float head = Smoothstep(s.neck - 0.04f, s.neck + 0.04f, h) * (1.0f - Smoothstep(1.02f, 1.12f, h));
			if (head > 0.0f && s.headBack != 0.0f)
				p = TurnX(p, Vector3(0, floor + s.neck * height, 0), -s.headBack * head * 0.0174533f);
			const Vector3 pivot(0, floor + s.hip * height, 0);
			if (s.pitch != 0.0f)
				p = TurnX(p, pivot, s.pitch * 0.0174533f);
			p.y += s.hipDepth * height - pivot.y + s.bob * height * std::sin(phase * 2.0f * 6.2831853f);
			p.z -= s.back * height;
			return p;
		}
	};

	// one axis' key frame packed as the file's: add, mul and three 10-bit values a word
	void Pack(Anim::AnimPosCoord& ac, size_t f, const std::vector<float>& values)
	{
		float lo = values.empty() ? 0.0f : values[0], hi = lo;
		for (float v : values) { lo = std::min(lo, v); hi = std::max(hi, v); }
		ac.vertadd[f] = lo;
		ac.vertmul[f] = hi - lo;
		ac.verts[f].resize((values.size() + 2) / 3);
		for (auto& w : ac.verts[f]) w = 0;
		for (size_t i = 0; i < values.size(); i++) {
			const uint32_t q = hi > lo ? (uint32_t)std::clamp((int)std::lround((values[i] - lo) / (hi - lo) * 1023.0f), 0, 1023) : 0u;
			ac.verts[f][i / 3] |= q << (11 * (i % 3));
		}
	}
}

bool SwimsByName(const std::string& blueprintName)
{
	std::string n = blueprintName;
	for (char& c : n)
		c = (char)std::tolower((unsigned char)c);
	if (n.rfind("dead ", 0) == 0)
		return false;
	static const char* skip[] = { "cart", "wagon", "boat", "ram", "bombard", "trebuchet", "catapult", "cannon", "siege", "elemental",
		"ship", "cog", "infernal", "mangonel", "ballista", "tower", "rocket", "organ", "engine", "abaddon", "michael", "behemoth",
		"horde spewer" };
	for (const char* m : skip)
		if (n.find(m) != std::string::npos)
			return false;
	return true;
}

SwimStyle SwimStyleForFolder(const std::string& folder)
{
	std::string f = folder;
	for (char& c : f)
		c = (char)std::tolower((unsigned char)c);
	auto has = [&](const char* s) { return f.find(s) != std::string::npos; };
	if (has("gibbering horde"))
		return SwimStyle::Swarm;
	if (has("knight") || has("horseman") || has("mounted") || has("bowoman") || has("cavalry") || has("rider"))
		return SwimStyle::Mounted;
	if (has("priest") || has("bishop") || has("monk") || has("inquisitor") || has("druid") || has("peasant\\female")
		|| has("peasant/female") || has("succubi"))
		return SwimStyle::Robed;
	return SwimStyle::Upright;
}

bool MakeSwimAnim(const Anim& walk, SwimStyle style, Anim& out)
{
	const uint32_t nv = walk.numVertices;
	if (nv == 0 || walk.duration == 0)
		return false;
	for (const auto& ac : walk.coords)
		if (ac.numFrames == 0)
			return false;
	constexpr float SLOWER = 1.5f; // (a stroke is slower than a step)
	// the key frames: every axis' together (the body's tip mixes y and z), and the loop's end
	std::set<uint32_t> timeSet{ 0u, walk.duration };
	for (const auto& ac : walk.coords)
		for (uint32_t t : ac.frameTimes)
			if (t <= walk.duration) timeSet.insert(t);
	const std::vector<uint32_t> times(timeSet.begin(), timeSet.end());
	std::vector<std::vector<Vector3>> pose(times.size(), std::vector<Vector3>(nv));
	std::vector<Vector3> rest(nv);
	for (size_t k = 0; k < times.size(); k++)
		for (uint32_t i = 0; i < nv; i++) {
			Vector3& p = pose[k][i];
			p = Vector3(Sample(walk.coords[0], times[k], i), Sample(walk.coords[1], times[k], i), Sample(walk.coords[2], times[k], i));
			if (k + 1 < times.size()) rest[i] += p; // (the loop's end is its start again)
		}
	float floor = 1e30f, top = -1e30f;
	for (Vector3& r : rest) {
		r /= (float)std::max<size_t>(times.size() - 1, 1);
		floor = std::min(floor, r.y);
		top = std::max(top, r.y);
	}
	// (the body's height: a spear, a pike, a lance or a staff held up would count as body, and put
	// the head under water; the game's people stand about 2 m, a horse and rider 3 m)
	const float tallest = style == SwimStyle::Mounted ? 3.0f : style == SwimStyle::Swarm ? 1.5f : 2.1f;
	const Mover mover{ ShapeOf(style), floor, std::clamp(top - floor, 0.3f, tallest) };

	out = Anim();
	out.meshname = walk.meshname;
	out.duration = (uint32_t)std::lround(walk.duration * SLOWER);
	out.numVertices = nv;
	std::vector<uint32_t> outTimes(times.size());
	for (size_t k = 0; k < times.size(); k++)
		outTimes[k] = k + 1 == times.size() ? out.duration : (uint32_t)std::lround(times[k] * SLOWER);
	Vector3 lo(1e30f, 1e30f, 1e30f), hi(-1e30f, -1e30f, -1e30f);
	std::vector<std::array<std::vector<float>, 3>> axes(times.size());
	for (size_t k = 0; k < times.size(); k++) {
		const float phase = (float)times[k] / (float)walk.duration;
		for (int c = 0; c < 3; c++) axes[k][c].resize(nv);
		for (uint32_t i = 0; i < nv; i++) {
			const Vector3 p = mover.apply(rest[i], pose[k][i] - rest[i], phase);
			for (int c = 0; c < 3; c++) {
				axes[k][c][i] = p.coord[c];
				lo.coord[c] = std::min(lo.coord[c], p.coord[c]);
				hi.coord[c] = std::max(hi.coord[c], p.coord[c]);
			}
		}
	}
	for (int c = 0; c < 3; c++) {
		Anim::AnimPosCoord& ac = out.coords[c];
		ac.numFrames = (uint32_t)times.size();
		ac.frameTimes.resize(ac.numFrames);
		ac.verts.resize(ac.numFrames);
		ac.vertadd.resize(ac.numFrames);
		ac.vertmul.resize(ac.numFrames);
		for (size_t k = 0; k < times.size(); k++) {
			ac.frameTimes[k] = outTimes[k];
			Pack(ac, k, axes[k][c]);
		}
	}
	out.sphereCenter = (lo + hi) * 0.5f;
	out.sphereRadius = std::max((hi - lo).len3() * 0.5f, 0.1f);

	// the attachment points (a torch's flame, a horn): moved as the body near them
	out.attachPoints.resize(walk.attachPoints.size());
	for (size_t a = 0; a < walk.attachPoints.size(); a++) {
		const auto& src = walk.attachPoints[a];
		auto& dst = out.attachPoints[a];
		dst.numFrames = src.numFrames;
		dst.frameTimes.resize(src.numFrames);
		dst.states.resize(src.numFrames);
		Vector3 mean;
		for (uint32_t f = 0; f < src.numFrames; f++) mean += src.states[f].position;
		if (src.numFrames) mean /= (float)src.numFrames;
		for (uint32_t f = 0; f < src.numFrames; f++) {
			const uint32_t t = std::min(src.frameTimes[f], walk.duration);
			dst.frameTimes[f] = f + 1 == src.numFrames && src.frameTimes[f] >= walk.duration ? out.duration : (uint32_t)std::lround(t * SLOWER);
			dst.states[f] = src.states[f];
			dst.states[f].position = mover.apply(mean, src.states[f].position - mean, (float)t / (float)walk.duration);
		}
	}

	// the normals: the walk's, tipped with the body
	const auto& sn = walk.normalFrames;
	auto& on = out.normalFrames;
	on.numFrames = sn.numFrames;
	on.numNormals = sn.numNormals;
	on.frameTimes.resize(sn.numFrames);
	on.norms.resize(sn.numFrames);
	const float pitch = mover.s.pitch * 0.0174533f;
	for (uint32_t f = 0; f < sn.numFrames; f++) {
		on.frameTimes[f] = f + 1 == sn.numFrames && sn.frameTimes[f] >= walk.duration ? out.duration : (uint32_t)std::lround(std::min(sn.frameTimes[f], walk.duration) * SLOWER);
		on.norms[f].resize(sn.numNormals);
		for (uint32_t i = 0; i < sn.numNormals; i++)
			on.norms[f][i] = pitch == 0.0f ? sn.norms[f][i]
				: Mesh::nearestNormal(TurnX(Mesh::s_normalTable[sn.norms[f][i]], Vector3(), pitch));
	}
	return true;
}
