// wkbai - leafless trees made in code (wkbai's seasons: winter, wildfires)
// Licensed under the GNU General Public License 3

#include "TreeModels.h"
#include "Model.h"
#include <cmath>
#include <string>
#include <vector>

namespace {
	struct TreeBuilder {
		std::vector<float> pos, uv;
		std::vector<Vector3> nrm;
		std::vector<uint32_t> tri;
		uint32_t rng;
		bool burnt;
		float rand01() { rng = rng * 1664525u + 1013904223u; return (rng >> 8) / 16777216.0f; }
		float range(float a, float b) { return a + (b - a) * rand01(); }

		static Vector3 cross(const Vector3& a, const Vector3& b) { return Vector3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
		static Vector3 unit(const Vector3& v) { const float l = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); return l > 1e-6f ? v * (1.0f / l) : Vector3(0, 1, 0); }
		void vertex(const Vector3& p, const Vector3& n, float u, float v) {
			pos.insert(pos.end(), { p.x, p.y, p.z });
			nrm.push_back(n);
			uv.insert(uv.end(), { u, v });
		}

		// a bent, tapering tube of bark from base along dir; its rings' centres returned (for the
		// branches growing from it)
		std::vector<std::pair<Vector3, float>> limb(const Vector3& base, const Vector3& dir, const Vector3& bend, float len, float r0, float r1, int sides, int rings, bool cap)
		{
			const Vector3 side = unit(std::abs(dir.y) < 0.95f ? cross(dir, Vector3(0, 1, 0)) : cross(dir, Vector3(1, 0, 0)));
			const Vector3 other = unit(cross(side, dir));
			std::vector<std::pair<Vector3, float>> centres;
			const uint32_t first = (uint32_t)(pos.size() / 3);
			for (int r = 0; r <= rings; r++) {
				const float t = (float)r / rings;
				const Vector3 c = base + dir * (len * t) + bend * (len * t * t);
				const float rad = r0 + (r1 - r0) * t;
				centres.push_back({ c, rad });
				for (int s = 0; s <= sides; s++) {
					const float a = 6.2831853f * s / sides;
					const Vector3 n = side * std::cos(a) + other * std::sin(a);
					vertex(c + n * rad, n, (float)s / sides, 0.02f + 0.96f * t);
				}
			}
			for (int r = 0; r < rings; r++)
				for (int s = 0; s < sides; s++) {
					const uint32_t a = first + r * (sides + 1) + s, b = a + 1, c = a + (sides + 1), d = c + 1;
					// outward: (b - a) x (c - a) along the normal (side then other turns about dir)
					addTriangle(a, c, b);
					addTriangle(b, c, d);
				}
			if (cap) {
				// a broken end: a flat disc, a little ragged
				const Vector3 c = centres.back().first, n = unit(dir + bend * 2.0f);
				const uint32_t mid = (uint32_t)(pos.size() / 3);
				vertex(c + n * (r1 * 0.3f), n, 0.5f, 0.5f);
				for (int s = 0; s <= sides; s++) {
					const float a = 6.2831853f * s / sides;
					const Vector3 e = side * std::cos(a) + other * std::sin(a);
					vertex(c + e * r1 + n * (r1 * range(-0.4f, 0.4f)), n, 0.5f + 0.5f * std::cos(a), 0.5f + 0.5f * std::sin(a));
				}
				for (int s = 0; s < sides; s++)
					addTriangle(mid, mid + 1 + s, mid + 2 + s);
			}
			return centres;
		}
		// the triangle facing the way its vertices' normals point
		void addTriangle(uint32_t a, uint32_t b, uint32_t c)
		{
			auto P = [&](uint32_t i) { return Vector3(pos[i * 3], pos[i * 3 + 1], pos[i * 3 + 2]); };
			const Vector3 n = cross(P(b) - P(a), P(c) - P(a));
			const Vector3 avg = nrm[a] + nrm[b] + nrm[c];
			if (n.x * avg.x + n.y * avg.y + n.z * avg.z < 0)
				std::swap(b, c);
			tri.insert(tri.end(), { a, b, c });
		}

		// a direction turned from dir by angle (radians), about a random way round
		Vector3 turn(const Vector3& dir, float angle)
		{
			const Vector3 side = unit(std::abs(dir.y) < 0.95f ? cross(dir, Vector3(0, 1, 0)) : cross(dir, Vector3(1, 0, 0)));
			const Vector3 other = unit(cross(side, dir));
			const float az = range(0.0f, 6.2831853f);
			const Vector3 away = side * std::cos(az) + other * std::sin(az);
			return unit(dir * std::cos(angle) + away * std::sin(angle));
		}

		void branch(const Vector3& base, const Vector3& dir, float len, float rad, int depth, int maxDepth)
		{
			static const int sidesAt[6] = { 9, 7, 6, 5, 4, 3 };
			const int sides = sidesAt[std::min(depth, 5)];
			const int rings = depth == 0 ? 5 : depth == 1 ? 4 : 3;
			const bool last = depth >= maxDepth;
			// a burnt tree's limbs end broken off; a winter tree's twigs thin out to a point
			const float r1 = burnt ? rad * range(0.55f, 0.75f) : last ? rad * 0.15f : rad * 0.62f;
			const Vector3 bend = turn(Vector3(0, 1, 0), 1.2f) * range(0.05f, 0.22f) + Vector3(0, depth == 0 ? 0.0f : -0.06f * depth, 0);
			const auto centres = limb(base, dir, bend, len, rad, r1, sides, rings, burnt);
			if (last)
				return;
			const Vector3 tipDir = unit(centres.back().first - centres[centres.size() - 2].first);
			// the leader, on from the tip
			if (!burnt || depth == 0)
				branch(centres.back().first, turn(tipDir, range(0.1f, 0.35f)), len * range(0.62f, 0.78f), r1 * 0.95f, depth + 1, maxDepth);
			// the side branches, from the limb's upper part
			const int n = burnt ? (depth == 0 ? 3 + (rand01() < 0.5f ? 1 : 0) : 1) : depth == 0 ? 4 : 2 + (rand01() < 0.6f ? 1 : 0);
			for (int i = 0; i < n; i++) {
				if (burnt && depth > 0 && rand01() < 0.4f)
					continue; // burnt away
				const float at = depth == 0 ? range(0.45f, 0.95f) : range(0.35f, 0.95f);
				const size_t ri = std::min(centres.size() - 1, (size_t)std::lround(at * (centres.size() - 1)));
				Vector3 d = turn(tipDir, range(0.55f, 1.0f));
				if (depth == 0)
					d = unit(d + Vector3(0, 0.35f, 0));
				const float childLen = len * range(0.55f, 0.75f) * (burnt ? 0.6f : 1.0f);
				branch(centres[ri].first, d, childLen, centres[ri].second * range(0.5f, 0.68f), depth + 1, maxDepth);
			}
		}
	};
}

Model* LeaflessTreeModel(ModelCache& cache, int variant, bool burnt)
{
	variant &= 3;
	const std::string name = std::string("wkbai:leafless tree ") + (burnt ? "burnt " : "") + std::to_string(variant);
	return cache.getGeneratedModel(name, [variant, burnt](Mesh& mesh) {
		TreeBuilder b;
		b.rng = 0x9E3779B9u * (variant + 1) + (burnt ? 77u : 0u);
		b.burnt = burnt;
		// a trunk about half the tree's height to the crown, the tree 1 high
		const float trunk = burnt ? b.range(0.38f, 0.5f) : b.range(0.32f, 0.42f);
		b.branch(Vector3(0, -0.02f, 0), b.turn(Vector3(0, 1, 0), b.range(0.0f, 0.08f)), trunk, burnt ? 0.036f : 0.045f, 0, burnt ? 2 : 4);
		// 1 high, or 0.7 burnt (the caller scales it to the tree it stands for)
		float top = 0.01f;
		for (size_t i = 1; i < b.pos.size(); i += 3)
			top = std::max(top, b.pos[i]);
		// a burnt tree is what is left of one: about 0.7 of the height (not stretched up, which would
		// make its trunk thick)
		const float scale = burnt ? 0.7f / top : 1.0f / top;
		for (float& p : b.pos)
			p *= scale;
		std::vector<Mesh::BuildGroup> groups(1);
		groups[0].mat.alphaTest = false;
		groups[0].mat.textureFilename = "Tree Bark.tga";
		groups[0].triangles = std::move(b.tri);
		mesh.build(b.pos, b.nrm, b.uv, groups);
	});
}
