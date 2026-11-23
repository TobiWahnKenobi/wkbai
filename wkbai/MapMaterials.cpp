// wkbai - the ground of a map folder by rule: materials, rules and the edge-rule solver
// (MapMaterials.h; the RE repo's docs/engine/map-authoring.md, step 17.5)
// Licensed under the GNU General Public License 3

#include "MapMaterials.h"
#include "MapFolder.h"
#include "terrain.h"
#include "file.h"
#include "tags.h"
#include "TrnTextureDb.h"
#include "util/StriCompare.h"
#include <stb_image.h>
// (static, as MapFolder.cpp and MapAuthoring.cpp have their own)
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#include <stb_image_write.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstring>
#include <deque>
#include <fstream>
#include <functional>
#include <queue>
#include <set>
#include <sstream>
#include <unordered_set>

namespace MapMaterials {
namespace {
	namespace fs = std::filesystem;
	constexpr float TILE = 5.0f;

	bool ReadAll(const fs::path& p, std::string& out)
	{
		std::ifstream f(p, std::ios::binary);
		if (!f)
			return false;
		std::ostringstream s;
		s << f.rdbuf();
		out = s.str();
		return true;
	}
	bool WriteAll(const fs::path& p, const std::string& data)
	{
		std::error_code ec;
		fs::create_directories(p.parent_path(), ec);
		std::ofstream f(p, std::ios::binary);
		f.write(data.data(), (std::streamsize)data.size());
		return (bool)f;
	}
	// a game file (data.bcp, mods, saved, redata) as text; false when there is none
	bool ReadGameFile(const std::string& path, std::string& out)
	{
		if (!FileExists(path.c_str()))
			return false;
		char* data = nullptr;
		int size = 0;
		LoadFile(path.c_str(), &data, &size, 1);
		if (!data)
			return false;
		out.assign(data, size);
		free(data);
		return true;
	}
	std::string StemOf(const std::string& path)
	{
		size_t a = path.find_last_of("\\/");
		a = a == std::string::npos ? 0 : a + 1;
		const size_t b = path.rfind('.');
		return path.substr(a, b == std::string::npos || b < a ? std::string::npos : b - a);
	}
	std::string DataFileOf(const std::string& textureDb) { return "Maps\\Materials\\" + StemOf(textureDb) + ".json"; }

	uint32_t Hash(uint32_t a, uint32_t b = 0, uint32_t c = 0, uint32_t d = 0)
	{
		uint64_t v = ((uint64_t)a << 32 | b) ^ ((uint64_t)c << 21) ^ ((uint64_t)d << 7) ^ 0x9E3779B97F4A7C15ull;
		v ^= v >> 30; v *= 0xBF58476D1CE4E5B9ull;
		v ^= v >> 27; v *= 0x94D049BB133111EBull;
		v ^= v >> 31;
		return (uint32_t)v;
	}
	// value noise with a smooth fade, -1..1, and its fractal sum
	float Noise(float x, float z, uint32_t s)
	{
		const float fx = std::floor(x), fz = std::floor(z);
		const int ix = (int)fx, iz = (int)fz;
		const float u = x - fx, v = z - fz;
		auto at = [&](int a, int b) { return (Hash(s, (uint32_t)(ix + a), (uint32_t)(iz + b)) >> 8) * (2.0f / 16777216.0f) - 1.0f; };
		const float su = u * u * (3 - 2 * u), sv = v * v * (3 - 2 * v);
		const float a = at(0, 0) + (at(1, 0) - at(0, 0)) * su;
		const float b = at(0, 1) + (at(1, 1) - at(0, 1)) * su;
		return a + (b - a) * sv;
	}
	float Fbm(float x, float z, int octaves, uint32_t s)
	{
		float sum = 0, amp = 1, norm = 0;
		for (int i = 0; i < octaves; i++) {
			// the octaves turned against each other, so the grid doesn't show
			const float c = 0.8f, sn = 0.6f;
			const float rx = x * c - z * sn, rz = x * sn + z * c;
			sum += amp * Noise(rx + i * 17.3f, rz - i * 9.1f, s + (uint32_t)i * 1013u);
			norm += amp;
			amp *= 0.5f;
			x = rx * 2.03f;
			z = rz * 2.03f;
		}
		return sum / norm;
	}
	// a fractal noise spread to about 0-1 evenly
	float Unit(float x, float z, uint32_t s) { return std::clamp(0.5f + 1.25f * Fbm(x, z, 3, s), 0.0f, 1.0f); }

	// ---- orientations: a tile's turn and flips as the corners of its picture at its world corners ----
	// world corners 0 (x, z), 1 (x + 1, z), 2 (x + 1, z + 1), 3 (x, z + 1); picture corners 0 (u0, v0),
	// 1 (u1, v0), 2 (u1, v1), 3 (u0, v1), as the renderers' TileCornerUVs (ExtendMap's LocalCorners)
	using Perm = std::array<int, 4>;
	Perm CornersOf(int rot, bool xf, bool zf)
	{
		Perm c = { 0, 1, 2, 3 };
		const bool xflip = xf != (bool)(rot & 2), zflip = zf != (bool)(rot & 2);
		if (!xflip) { std::swap(c[0], c[3]); std::swap(c[1], c[2]); }
		if (zflip) { std::swap(c[0], c[1]); std::swap(c[2], c[3]); }
		if (rot & 1) { const int t = c[0]; c[0] = c[1]; c[1] = c[2]; c[2] = c[3]; c[3] = t; }
		return c;
	}
	int PermKey(const Perm& p) { return p[0] | p[1] << 2 | p[2] << 4 | p[3] << 6; }
	// the 8 orientations (the square's symmetries); the 16 codes (turn | xflip << 2 | zflip << 3) map onto them
	struct Orientations {
		Perm perm[8];
		int code[8];          // a code for each
		int ofCode[16];       // code -> orientation
		int ofKey[256];
		// for a tile A in orientation a: h, the world's symmetry that takes A to orientation 0 (turn 0,
		// no flips: the rules' frame); rel[a][b]: B's orientation in that frame; side[a][w]: the side
		// of A's picture at world side w
		int rel[8][8];
		int side[8][4];
		Orientations()
		{
			std::fill(std::begin(ofKey), std::end(ofKey), -1);
			int n = 0;
			for (int c = 0; c < 16; c++) {
				const Perm p = CornersOf(c & 3, (c >> 2) & 1, (c >> 3) & 1);
				const int k = PermKey(p);
				if (ofKey[k] < 0) { ofKey[k] = n; perm[n] = p; code[n] = c; n++; }
				ofCode[c] = ofKey[k];
			}
			// the world's symmetries: turns i -> i + k, mirrors i -> k - i
			std::vector<Perm> syms;
			for (int k = 0; k < 4; k++) {
				Perm r, m;
				for (int i = 0; i < 4; i++) { r[i] = (i + k) % 4; m[i] = ((k - i) % 4 + 4) % 4; }
				syms.push_back(r);
				syms.push_back(m);
			}
			const Perm& L0 = perm[ofCode[0]];
			// world sides (Tags::MAPTEXDIR_*: east 0, north 1, south 2, west 3) by their corners
			const int WS[4][2] = { { 1, 2 }, { 2, 3 }, { 0, 1 }, { 3, 0 } };
			// picture sides by the picture's corners (fitted to the original maps' tiles: north v0)
			const int PS[4][2] = { { 1, 2 }, { 0, 1 }, { 2, 3 }, { 3, 0 } };
			for (int a = 0; a < 8; a++) {
				Perm h{};
				for (const Perm& s : syms) {
					Perm q;
					for (int i = 0; i < 4; i++) q[s[i]] = perm[a][i];
					if (q == L0) { h = s; break; }
				}
				for (int b = 0; b < 8; b++) {
					Perm q;
					for (int i = 0; i < 4; i++) q[h[i]] = perm[b][i];
					rel[a][b] = ofKey[PermKey(q)];
				}
				for (int w = 0; w < 4; w++) {
					const int p0 = L0[h[WS[w][0]]], p1 = L0[h[WS[w][1]]];
					side[a][w] = -1;
					for (int s = 0; s < 4; s++)
						if ((PS[s][0] == p0 && PS[s][1] == p1) || (PS[s][0] == p1 && PS[s][1] == p0))
							side[a][w] = s;
				}
			}
		}
	};
	const Orientations& OR()
	{
		static const Orientations o;
		return o;
	}
	const int OPPOSITE[4] = { 3, 2, 1, 0 }; // east-west, north-south
	const int DX[4] = { 1, 0, 0, -1 }, DZ[4] = { 0, 1, -1, 0 };
	const char* SIDE_NAMES[4] = { "east", "north", "south", "west" };

	// ---- the texture set as the solver sees it ----
	struct Placed { TerrainTexture* tex; uint8_t orient; };
	struct Library {
		std::vector<std::string> groups;            // the base groups (the materials' groups)
		std::map<std::string, int> groupIndex;
		std::map<const TerrainTextureGroup*, int> baseOf; // a base group's TerrainTextureGroup -> index
		std::map<const TerrainTexture*, int> rid;   // a texture -> its rules' id (its TEXTURE's, for a MATCHING one)
		std::map<const TerrainTexture*, std::array<int, 4>> corners; // per picture corner, a base group (-1 unknown)
		std::unordered_set<uint64_t> compat;        // (rid A, side of A, rid B, B's orientation in A's frame)
		std::map<std::pair<int, int>, std::array<std::vector<Placed>, 16>> fits; // pair (a < b), world corners that are b -> tiles
		std::vector<std::vector<Placed>> pure;      // per base group: its textures in every orientation
		std::vector<std::vector<int>> next;         // next[a][b]: the next group on the shortest way of transitions from a to b (-1 none)
		std::vector<std::vector<int>> dist;         // dist[a][b]: transitions on that way (99 none)
		size_t ruleCount = 0;
		bool hasPair(int a, int b) const { return a == b || fits.count({ std::min(a, b), std::max(a, b) }) != 0; }
		// the ways between groups over the transitions (breadth first, from each); a group marked
		// noBridge is never a step between two others (snow between a cliff and grass), only an end
		void ways(const std::vector<uint8_t>& noBridge)
		{
			const int G = (int)groups.size();
			next.assign(G, std::vector<int>(G, -1));
			dist.assign(G, std::vector<int>(G, 99));
			for (int s = 0; s < G; s++) {
				std::vector<int> first(G, -1), d(G, -1);
				std::queue<int> q;
				d[s] = 0;
				q.push(s);
				while (!q.empty()) {
					const int u = q.front();
					q.pop();
					if (u != s && (size_t)u < noBridge.size() && noBridge[u]) continue; // an end, not a way through
					for (int v = 0; v < G; v++)
						if (d[v] < 0 && u != v && hasPair(u, v)) {
							d[v] = d[u] + 1;
							first[v] = u == s ? v : first[u];
							q.push(v);
						}
				}
				for (int v = 0; v < G; v++) {
					next[s][v] = v == s ? s : first[v];
					dist[s][v] = d[v] < 0 ? 99 : d[v];
				}
			}
		}
		static uint64_t Key(int ra, int s, int rb, int o) { return (uint64_t)(uint32_t)ra << 32 | (uint64_t)(uint32_t)rb << 8 | (uint64_t)s << 4 | (uint64_t)o; }
		// whether b may lie beyond world side w of a, by either's rules
		bool fit(const Placed& a, int w, const Placed& b) const
		{
			const Orientations& o = OR();
			auto ia = rid.find(a.tex), ib = rid.find(b.tex);
			if (ia == rid.end() || ib == rid.end())
				return true; // a texture without rules: nothing to keep to
			if (compat.count(Key(ia->second, o.side[a.orient][w], ib->second, o.rel[a.orient][b.orient])))
				return true;
			return compat.count(Key(ib->second, o.side[b.orient][OPPOSITE[w]], ia->second, o.rel[b.orient][a.orient])) != 0;
		}
		// a placed texture's world corners' groups
		std::array<int, 4> worldCorners(const Placed& p) const
		{
			std::array<int, 4> out = { -1, -1, -1, -1 };
			auto c = corners.find(p.tex);
			if (c == corners.end())
				return out;
			for (int i = 0; i < 4; i++)
				out[i] = c->second[OR().perm[p.orient][i]];
			return out;
		}
	};

	bool IsTransitionName(const std::string& n, std::string& a, std::string& b)
	{
		size_t d = n.find('-');
		size_t len = 1;
		if (d == std::string::npos) {
			d = n.find("_TO_");
			len = 4;
		}
		if (d == std::string::npos)
			return false;
		a = n.substr(0, d);
		b = n.substr(d + len);
		return true;
	}

	Library BuildLibrary(const TerrainTextureDatabase& db, const EdgeRules& er)
	{
		Library L;
		const Orientations& o = OR();
		for (const auto& [name, g] : db.groups) {
			std::string a, b;
			if (!g || g->textures.empty() || IsTransitionName(name, a, b))
				continue;
			L.groupIndex[name] = (int)L.groups.size();
			L.baseOf[g.get()] = (int)L.groups.size();
			L.groups.push_back(name);
		}
		// rules' ids: a texture's own id, or its TEXTURE's for a MATCHING alternative
		std::map<std::pair<std::string, int>, int> ridOf;
		auto ridFor = [&](const std::string& group, int id) {
			auto m = er.matchingOf.find(group);
			if (m != er.matchingOf.end())
				if (auto t = m->second.find(id); t != m->second.end())
					id = t->second;
			auto it = ridOf.find({ group, id });
			if (it != ridOf.end())
				return it->second;
			const int r = (int)ridOf.size();
			ridOf[{ group, id }] = r;
			return r;
		};
		auto isMatching = [&](const std::string& group, int id) {
			auto m = er.matchingOf.find(group);
			return m != er.matchingOf.end() && m->second.count(id) != 0;
		};
		for (const auto& [name, g] : db.groups)
			if (g)
				for (const auto& [id, t] : g->textures)
					if (t->genCorners[0].empty()) // (a generated picture has no rules: it fits any neighbour)
						L.rid[t.get()] = ridFor(name, id);
		for (const auto& [group, byTex] : er.rules)
			for (const auto& [id, list] : byTex) {
				const int ra = ridFor(group, id);
				for (const EdgeRule& r : list) {
					if (r.side < 0)
						continue;
					const int rb = ridFor(r.group, r.id);
					L.compat.insert(Library::Key(ra, r.side, rb, o.ofCode[(r.rot & 3) | (r.xflip & 1) << 2 | (r.zflip & 1) << 3]));
					L.ruleCount++;
				}
			}
		// corners: a base group's pictures are all of it; a transition's from the rules, by vote: a
		// rule that puts a picture of known corners beyond a side tells the two corners on that side
		std::map<int, std::array<int, 4>> ridCorners;
		for (const auto& [name, g] : db.groups) {
			auto bi = L.groupIndex.find(name);
			if (bi == L.groupIndex.end())
				continue;
			for (const auto& [id, t] : g->textures)
				ridCorners[L.rid[t.get()]] = { bi->second, bi->second, bi->second, bi->second };
		}
		// world corner of a tile -> the corner of its neighbour beyond side w that is the same point
		const int SHARED[4][2][2] = { { { 1, 0 }, { 2, 3 } }, { { 2, 1 }, { 3, 0 } }, { { 0, 3 }, { 1, 2 } }, { { 0, 1 }, { 3, 2 } } };
		const Perm& L0 = o.perm[o.ofCode[0]];
		struct Tr { int rid; int a, b; std::string group; int id; };
		std::vector<Tr> transitions;
		for (const auto& [name, g] : db.groups) {
			std::string an, bn;
			if (!g || !IsTransitionName(name, an, bn) || !L.groupIndex.count(an) || !L.groupIndex.count(bn))
				continue;
			for (const auto& [id, t] : g->textures)
				if (!isMatching(name, id) && t->genCorners[0].empty()) // the TEXTUREs: a MATCHING one shares its rules and corners (a generated one has neither)
					transitions.push_back({ L.rid[t.get()], L.groupIndex[an], L.groupIndex[bn], name, id });
		}
		for (int pass = 0; pass < 4; pass++) {
			std::map<int, std::array<int, 4>> found;
			for (const Tr& tr : transitions) {
				int votes[4][2] = {};
				auto g = er.rules.find(tr.group);
				if (g == er.rules.end())
					continue;
				auto list = g->second.find(tr.id);
				if (list == g->second.end())
					continue;
				for (const EdgeRule& r : list->second) {
					if (r.side < 0)
						continue;
					auto uc = ridCorners.find(ridFor(r.group, r.id));
					if (uc == ridCorners.end())
						continue;
					// the world side of this picture's side r.side at orientation 0
					int w = -1;
					for (int s = 0; s < 4; s++)
						if (o.side[o.ofCode[0]][s] == r.side)
							w = s;
					if (w < 0)
						continue;
					const Perm& pu = o.perm[o.ofCode[(r.rot & 3) | (r.xflip & 1) << 2 | (r.zflip & 1) << 3]];
					for (int k = 0; k < 2; k++) {
						const int mine = SHARED[w][k][0], theirs = SHARED[w][k][1];
						const int m = uc->second[pu[theirs]];
						const int pc = L0[mine];
						if (m == tr.a) votes[pc][0]++;
						else if (m == tr.b) votes[pc][1]++;
					}
				}
				std::array<int, 4> c;
				bool all = true;
				for (int k = 0; k < 4; k++) {
					c[k] = votes[k][0] > votes[k][1] ? tr.a : votes[k][1] > votes[k][0] ? tr.b : -1;
					all = all && c[k] >= 0;
				}
				if (all)
					found[tr.rid] = c;
			}
			for (auto& [r, c] : found)
				ridCorners[r] = c;
		}
		// the solver's tables
		L.pure.resize(L.groups.size());
		for (const auto& [name, g] : db.groups)
			for (const auto& [id, t] : g->textures) {
				std::array<int, 4> gen;
				const std::array<int, 4>* found = nullptr;
				if (!t->genCorners[0].empty()) {
					// a generated transition (TransitionGen.h): its corners as it was made
					bool ok = true;
					for (int k = 0; k < 4; k++) {
						auto gi = L.groupIndex.find(t->genCorners[k]);
						ok = ok && gi != L.groupIndex.end();
						gen[k] = ok ? gi->second : -1;
					}
					if (!ok)
						continue;
					found = &gen;
				}
				else {
					auto c = ridCorners.find(L.rid[t.get()]);
					if (c == ridCorners.end())
						continue;
					found = &c->second;
				}
				L.corners[t.get()] = *found;
				const std::array<int, 4>& cc = *found;
				if (cc[0] == cc[1] && cc[1] == cc[2] && cc[2] == cc[3]) {
					if (L.baseOf.count(g.get()))
						for (int k = 0; k < 8; k++)
							L.pure[cc[0]].push_back({ t.get(), (uint8_t)k });
					continue;
				}
				const int lo = *std::min_element(cc.begin(), cc.end()), hi = *std::max_element(cc.begin(), cc.end());
				bool two = true;
				for (int k = 0; k < 4; k++)
					two = two && (cc[k] == lo || cc[k] == hi);
				if (!two)
					continue;
				auto& table = L.fits[{ lo, hi }];
				for (int k = 0; k < 8; k++) {
					int bits = 0;
					for (int i = 0; i < 4; i++)
						if (cc[o.perm[k][i]] == hi)
							bits |= 1 << i;
					table[bits].push_back({ t.get(), (uint8_t)k });
				}
			}
		L.ways({});
		return L;
	}

	// ---- materials (data) ----
	struct Material {
		std::string name, description;
		std::vector<std::pair<int, float>> groups; // base group, weight
		float patchScale = 200;
		std::array<float, 3> colour = { 128, 128, 128 };
	};
	struct Range { bool hasLo = false, hasHi = false; float lo = 0, hi = 0; };
	struct Cond { int what; Range r; float jitter; };
	enum { C_HEIGHT, C_HEIGHTREL, C_ABOVESEA, C_SLOPE, C_CURVATURE, C_MOISTURE, C_DRAINAGE, C_RIVER, C_SEADIST, C_NOISE, C_MASK, C_COUNT };
	const char* COND_NAMES[C_COUNT] = { "height", "heightRel", "aboveSea", "slope", "curvature", "moisture", "drainage", "river", "seaDistance", "noise", "mask" };
	const float COND_JITTER[C_COUNT] = { 4.0f, 0.03f, 0.6f, 3.0f, 0.15f, 0.06f, 0.05f, 0.12f, 8.0f, 0.0f, 0.06f };
	struct Rule {
		int material = -1;
		std::vector<Cond> conds;
		int water = -1; // -1 any, 0 dry, 1 at water
		bool keepThin = false; // a line (a stream's bed, a track): kept by the clean-ups that remove slivers
		float noiseScale = 350, jitterScale = 60;
		std::string mask;
		int index = 0;
		json source;
	};

	Range RangeOf(const json& j)
	{
		Range r;
		if (j.is_array() && j.size() == 2) {
			if (j[0].is_number()) { r.hasLo = true; r.lo = j[0].get<float>(); }
			if (j[1].is_number()) { r.hasHi = true; r.hi = j[1].get<float>(); }
		}
		else if (j.is_number()) {
			r.hasLo = true;
			r.lo = j.get<float>();
		}
		return r;
	}
}

// ---------------------------------------------------------------------------------------------

EdgeRules ReadEdgeRules(const std::string& dbFile)
{
	EdgeRules er;
	std::string text;
	if (!ReadGameFile(dbFile, text))
		return er;
	auto sideOf = [](const std::string& tag) {
		for (int s = 0; s < 4; s++)
			if (StrCICompare(tag, Tags::MAPTEXDIR_tagDict.tags[s]) == 0)
				return s;
		return -1;
	};
	std::istringstream in(text);
	std::string line, group;
	int texture = -1, side = -1;
	while (std::getline(in, line)) {
		if (const size_t c = line.find("//"); c != std::string::npos)
			line.resize(c);
		std::istringstream ls(line);
		std::vector<std::string> tok;
		for (std::string t; ls >> t;) {
			if (t.size() >= 2 && t.front() == '"' && t.back() == '"')
				t = t.substr(1, t.size() - 2);
			tok.push_back(t);
		}
		if (tok.empty())
			continue;
		const std::string& k = tok[0];
		if (k == "TEXTURE_GROUP" && tok.size() >= 2)
			group = tok[1], texture = -1, side = -1;
		else if (k == "TEXTURE" && tok.size() >= 7)
			texture = atoi(tok[6].c_str()), side = -1;
		else if (k == "MATCHING" && tok.size() >= 7 && texture >= 0)
			er.matchingOf[group][atoi(tok[6].c_str())] = texture;
		else if (k == "EDGE" && tok.size() >= 7 && side >= 0 && texture >= 0) {
			EdgeRule r{ side, sideOf(tok[1]), tok[2], atoi(tok[3].c_str()), atoi(tok[4].c_str()), atoi(tok[5].c_str()), atoi(tok[6].c_str()) };
			er.rules[group][texture].push_back(r);
			er.count++;
		}
		else if (tok.size() == 1 && sideOf(k) >= 0)
			side = sideOf(k);
		else if (k == "END_TEXTURE")
			texture = -1, side = -1;
	}
	return er;
}

bool HasRules(const json& mapJson)
{
	if (!mapJson.is_object() || !mapJson.contains("ground") || !mapJson["ground"].is_object())
		return false;
	const json& g = mapJson["ground"];
	return (g.contains("rules") && g["rules"].is_array()) || (g.contains("preset") && g["preset"].is_string());
}

json LoadMaterials(const std::string& textureDb, const json& mapJson, std::vector<std::string>& warnings)
{
	json data = json::object();
	std::string text;
	const std::string file = DataFileOf(textureDb);
	if (ReadGameFile(file, text)) {
		data = json::parse(text, nullptr, false);
		if (data.is_discarded() || !data.is_object()) {
			warnings.push_back(file + " is not JSON: left out");
			data = json::object();
		}
		else
			data["source"] = file;
	}
	for (const char* k : { "materials", "presets", "groups" })
		if (!data.contains(k) || !data[k].is_object())
			data[k] = json::object();
	if (mapJson.is_object() && mapJson.contains("ground") && mapJson["ground"].is_object() && mapJson["ground"].contains("materials")
		&& mapJson["ground"]["materials"].is_object())
		for (const auto& [name, m] : mapJson["ground"]["materials"].items()) {
			if (m.is_null())
				data["materials"].erase(name);
			else if (data["materials"].contains(name) && data["materials"][name].is_object() && m.is_object())
				data["materials"][name].merge_patch(m);
			else
				data["materials"][name] = m;
		}
	return data;
}

std::string Fingerprint(const std::filesystem::path& folder)
{
	std::string text, out;
	if (!ReadAll(folder / "map.json", text))
		return out;
	const json j = json::parse(text, nullptr, false);
	if (j.is_discarded() || !HasRules(j))
		return out;
	const std::string db = j.value("textures", json::object()).value("database", "");
	ReadGameFile(DataFileOf(db), out);
	out += "|solver " + std::to_string(SOLVER_VERSION);
	// the masks the rules may read
	std::error_code ec;
	for (const auto& e : fs::directory_iterator(folder / "layers", ec)) {
		std::string d;
		if (e.path().extension() == ".png" && ReadAll(e.path(), d))
			out += "|" + PathToUtf8(e.path().filename()) + ":" + std::to_string(d.size()) + ":" + std::to_string(std::hash<std::string>()(d));
	}
	return out;
}

namespace {
	// a mask picture (8 or 16 bits, any size, row 0 the north edge) sampled per vertex, 0-1
	bool ReadMask(const fs::path& file, int gw, int gh, std::vector<float>& out)
	{
		std::string data;
		if (!ReadAll(file, data))
			return false;
		int w = 0, h = 0, n = 0;
		std::vector<float> px;
		if (stbi_is_16_bit_from_memory((const stbi_uc*)data.data(), (int)data.size())) {
			stbi_us* p = stbi_load_16_from_memory((const stbi_uc*)data.data(), (int)data.size(), &w, &h, &n, 1);
			if (!p) return false;
			px.resize((size_t)w * h);
			for (size_t i = 0; i < px.size(); i++) px[i] = p[i] / 65535.0f;
			stbi_image_free(p);
		}
		else {
			stbi_uc* p = stbi_load_from_memory((const stbi_uc*)data.data(), (int)data.size(), &w, &h, &n, 1);
			if (!p) return false;
			px.resize((size_t)w * h);
			for (size_t i = 0; i < px.size(); i++) px[i] = p[i] / 255.0f;
			stbi_image_free(p);
		}
		out.assign((size_t)gw * gh, 0.0f);
		for (int z = 0; z < gh; z++)
			for (int x = 0; x < gw; x++) {
				const float fx = gw > 1 ? x * (float)(w - 1) / (gw - 1) : 0;
				const float fy = gh > 1 ? (gh - 1 - z) * (float)(h - 1) / (gh - 1) : 0;
				const int x0 = (int)fx, y0 = (int)fy, x1 = std::min(x0 + 1, w - 1), y1 = std::min(y0 + 1, h - 1);
				const float u = fx - x0, v = fy - y0;
				const float a = px[(size_t)y0 * w + x0] + (px[(size_t)y0 * w + x1] - px[(size_t)y0 * w + x0]) * u;
				const float b = px[(size_t)y1 * w + x0] + (px[(size_t)y1 * w + x1] - px[(size_t)y1 * w + x0]) * u;
				out[(size_t)z * gw + x] = a + (b - a) * v;
			}
		return true;
	}
}

bool ReadMaskLayer(const std::filesystem::path& file, int gw, int gh, std::vector<float>& out) { return ReadMask(file, gw, gh, out); }

namespace {
	bool WritePngRgb(const fs::path& file, int w, int h, const std::vector<uint8_t>& rgb, int channels = 3)
	{
		std::string out;
		auto write = [](void* ctx, void* data, int size) { ((std::string*)ctx)->append((const char*)data, size); };
		if (!stbi_write_png_to_func(write, &out, w, h, channels, rgb.data(), w * channels))
			return false;
		return WriteAll(file, out);
	}

	// distances in metres from the marked vertices (two-pass chamfer, 5 and 7.07 m steps)
	std::vector<float> Distance(const std::vector<uint8_t>& src, int gw, int gh)
	{
		const float INF = 1e9f, D1 = TILE, D2 = TILE * 1.41421356f;
		std::vector<float> d(src.size());
		for (size_t i = 0; i < src.size(); i++) d[i] = src[i] ? 0 : INF;
		for (int z = 0; z < gh; z++)
			for (int x = 0; x < gw; x++) {
				float& v = d[(size_t)z * gw + x];
				if (x > 0) v = std::min(v, d[(size_t)z * gw + x - 1] + D1);
				if (z > 0) {
					v = std::min(v, d[(size_t)(z - 1) * gw + x] + D1);
					if (x > 0) v = std::min(v, d[(size_t)(z - 1) * gw + x - 1] + D2);
					if (x < gw - 1) v = std::min(v, d[(size_t)(z - 1) * gw + x + 1] + D2);
				}
			}
		for (int z = gh - 1; z >= 0; z--)
			for (int x = gw - 1; x >= 0; x--) {
				float& v = d[(size_t)z * gw + x];
				if (x < gw - 1) v = std::min(v, d[(size_t)z * gw + x + 1] + D1);
				if (z < gh - 1) {
					v = std::min(v, d[(size_t)(z + 1) * gw + x] + D1);
					if (x < gw - 1) v = std::min(v, d[(size_t)(z + 1) * gw + x + 1] + D2);
					if (x > 0) v = std::min(v, d[(size_t)(z + 1) * gw + x - 1] + D2);
				}
			}
		return d;
	}

	float Num(const json& j, const char* k, float def)
	{
		return j.is_object() && j.contains(k) && j[k].is_number() ? j[k].get<float>() : def;
	}
}

bool Paint(const std::filesystem::path& folder, const json& mapJson, uint32_t seed, Terrain& t, const std::vector<uint8_t>& hand,
	json& report, std::vector<std::string>& warnings, std::vector<uint8_t>& ruleTile, std::string& error)
{
	using clock = std::chrono::steady_clock;
	const auto t0 = clock::now();
	auto ms = [](clock::time_point a) { return std::chrono::duration<double, std::milli>(clock::now() - a).count(); };
	const json ground = mapJson.value("ground", json::object());
	const std::string dbFile = mapJson.value("textures", json::object()).value("database", "");
	const int W = t.width, H = t.height, E = t.edge, gw = W + 1, gh = H + 1;
	const size_t NV = (size_t)gw * gh, NT = (size_t)W * H;
	ruleTile.assign(NT, 0);

	// ---- the texture set ----
	const EdgeRules er = ReadEdgeRules(dbFile);
	Library L = BuildLibrary(t.texDb, er);
	const int G = (int)L.groups.size();
	if (!G) {
		error = "the texture database " + dbFile + " has no groups to paint with";
		return false;
	}
	if (!er.count)
		warnings.push_back("ground: " + dbFile + " has no edge rules: tiles are chosen by their corners only");
	const auto tLib = clock::now();
	MapFolder::ReportProgress("the ground: the land", 0.03f);

	// ---- materials and rules ----
	json data = LoadMaterials(dbFile, mapJson, warnings);
	{
		std::vector<uint8_t> noBridge(G, 0);
		for (int g = 0; g < G; g++) {
			const json gj = data["groups"].value(L.groups[g], json::object());
			noBridge[g] = gj.contains("bridge") && gj["bridge"].is_boolean() && !gj["bridge"].get<bool>();
		}
		L.ways(noBridge);
	}
	std::vector<Material> mats;
	std::map<std::string, int> matIndex;
	auto addMaterial = [&](const std::string& name, const json& mj) {
		Material m;
		m.name = name;
		m.description = mj.value("description", "");
		m.patchScale = Num(mj, "patchScale", 200);
		for (const json& gj : mj.value("groups", json::array())) {
			std::string g;
			float wgt = 1;
			if (gj.is_string()) g = gj.get<std::string>();
			else if (gj.is_array() && !gj.empty() && gj[0].is_string()) {
				g = gj[0].get<std::string>();
				if (gj.size() > 1 && gj[1].is_number()) wgt = std::max(0.0f, gj[1].get<float>());
			}
			auto gi = L.groupIndex.find(g);
			if (gi == L.groupIndex.end()) {
				warnings.push_back("material " + name + ": no texture group " + g + " in " + dbFile + " (a transition or a group with no pictures can't be a material's)");
				continue;
			}
			if (wgt > 0)
				m.groups.push_back({ gi->second, wgt });
		}
		if (m.groups.empty())
			return -1;
		// its colour: the groups' mean colours, for the material map
		std::array<float, 3> c = { 0, 0, 0 };
		float wsum = 0;
		for (auto& [g, wgt] : m.groups) {
			const json gj = data["groups"].value(L.groups[g], json::object());
			if (gj.contains("colour") && gj["colour"].is_array() && gj["colour"].size() >= 3) {
				for (int k = 0; k < 3; k++) c[k] += gj["colour"][k].get<float>() * wgt;
				wsum += wgt;
			}
		}
		if (wsum > 0)
			for (int k = 0; k < 3; k++) m.colour[k] = c[k] / wsum;
		if (mj.contains("mapColour") && mj["mapColour"].is_array() && mj["mapColour"].size() >= 3)
			for (int k = 0; k < 3; k++) m.colour[k] = mj["mapColour"][k].get<float>();
		matIndex[name] = (int)mats.size();
		mats.push_back(m);
		return (int)mats.size() - 1;
	};
	for (const auto& [name, mj] : data["materials"].items())
		if (mj.is_object())
			addMaterial(name, mj);
	// a texture set without a data file: each base group is a material of its own name
	for (int g = 0; g < G; g++) {
		std::string lower = L.groups[g];
		std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return (char)std::tolower(c); });
		if (!matIndex.count(L.groups[g]) && !matIndex.count(lower))
			addMaterial(L.groups[g], json{ { "groups", json::array({ L.groups[g] }) }, { "description", "the texture group " + L.groups[g] } });
	}
	auto materialOf = [&](const json& j, const std::string& where) {
		if (!j.is_string()) return -1;
		auto it = matIndex.find(j.get<std::string>());
		if (it == matIndex.end()) {
			warnings.push_back(where + ": no material " + j.get<std::string>());
			return -1;
		}
		return it->second;
	};
	json preset = json::object();
	std::string presetName;
	if (ground.contains("preset") && ground["preset"].is_string()) {
		presetName = ground["preset"].get<std::string>();
		if (data["presets"].contains(presetName))
			preset = data["presets"][presetName];
		else
			warnings.push_back("ground.preset: no preset " + presetName + " (" + std::to_string(data["presets"].size()) + " in " + data.value("source", std::string("no data file")) + ")");
	}
	json ruleList = json::array();
	const std::string mode = ground.value("rulesMode", "before");
	if (ground.contains("rules") && ground["rules"].is_array())
		for (const json& r : ground["rules"]) ruleList.push_back(r);
	if (mode != "replace")
		for (const json& r : preset.value("rules", json::array())) ruleList.push_back(r);
	int defaultMat = -1;
	if (ground.contains("default")) defaultMat = materialOf(ground["default"], "ground.default");
	if (defaultMat < 0 && preset.contains("default")) defaultMat = materialOf(preset["default"], "preset " + presetName + " default");
	if (defaultMat < 0 && ground.contains("fill") && ground["fill"].is_string())
		for (size_t i = 0; i < mats.size(); i++)
			if (mats[i].groups.size() == 1 && L.groups[mats[i].groups[0].first] == ground["fill"].get<std::string>()) defaultMat = (int)i;
	if (defaultMat < 0) defaultMat = matIndex.count("grass") ? matIndex["grass"] : matIndex.count("GRASS1") ? matIndex["GRASS1"] : 0;
	std::vector<Rule> rules;
	for (size_t i = 0; i < ruleList.size(); i++) {
		const json& rj = ruleList[i];
		if (!rj.is_object()) continue;
		Rule r;
		r.index = (int)i;
		r.source = rj;
		r.material = materialOf(rj.value("material", json()), "ground rule " + std::to_string(i));
		if (r.material < 0) continue;
		r.noiseScale = std::max(10.0f, Num(rj, "noiseScale", 350));
		r.jitterScale = std::max(10.0f, Num(rj, "jitterScale", 60));
		const json jit = rj.value("jitter", json());
		for (int c = 0; c < C_COUNT; c++) {
			if (c == C_MASK) continue;
			if (!rj.contains(COND_NAMES[c])) continue;
			float j = COND_JITTER[c];
			if (jit.is_number()) j *= jit.get<float>();
			else if (jit.is_object() && jit.contains(COND_NAMES[c]) && jit[COND_NAMES[c]].is_number()) j = jit[COND_NAMES[c]].get<float>();
			r.conds.push_back({ c, RangeOf(rj[COND_NAMES[c]]), j });
		}
		if (rj.contains("mask") && rj["mask"].is_string()) {
			r.mask = rj["mask"].get<std::string>();
			Range mr = rj.contains("maskRange") ? RangeOf(rj["maskRange"]) : Range{ true, false, 0.5f, 0 };
			float j = COND_JITTER[C_MASK];
			if (jit.is_number()) j *= jit.get<float>();
			r.conds.push_back({ C_MASK, mr, j });
		}
		if (rj.contains("water") && rj["water"].is_boolean()) r.water = rj["water"].get<bool>() ? 1 : 0;
		r.keepThin = rj.value("keepThin", false);
		for (const auto& [k, v] : rj.items()) {
			bool known = k == "material" || k == "water" || k == "jitter" || k == "jitterScale" || k == "noiseScale" || k == "mask" || k == "maskRange"
				|| k == "id" || k == "note" || k == "role" || k == "keepThin";
			for (int c = 0; c < C_COUNT; c++) known = known || k == COND_NAMES[c];
			if (!known) warnings.push_back("ground rule " + std::to_string(i) + ": unknown key " + k + " (left out)");
		}
		rules.push_back(r);
	}

	// ---- the land, per vertex ----
	std::vector<float> h(NV), slope(NV), curv(NV);
	for (int z = 0; z < gh; z++)
		for (int x = 0; x < gw; x++)
			h[(size_t)z * gw + x] = t.getVertex(x, z);
	auto H_ = [&](int x, int z) { return h[(size_t)std::clamp(z, 0, gh - 1) * gw + std::clamp(x, 0, gw - 1)]; };
	for (int z = 0; z < gh; z++)
		for (int x = 0; x < gw; x++) {
			// the mean of the four tiles round the vertex, each as the game measures it (its corners'
			// spread over 5 m: above 45 degrees most units can't stand there)
			float steep = 0;
			for (int oz = -1; oz <= 0; oz++)
				for (int ox = -1; ox <= 0; ox++) {
					const float a = H_(x + ox, z + oz), b = H_(x + ox + 1, z + oz), c = H_(x + ox + 1, z + oz + 1), d = H_(x + ox, z + oz + 1);
					steep += std::atan((std::max({ a, b, c, d }) - std::min({ a, b, c, d })) / TILE) * 0.25f;
				}
			slope[(size_t)z * gw + x] = steep * 180.0f / 3.14159265f;
			float s = 0;
			int n = 0;
			for (int oz = -2; oz <= 2; oz++)
				for (int ox = -2; ox <= 2; ox++) { s += H_(x + ox, z + oz); n++; }
			curv[(size_t)z * gw + x] = H_(x, z) - s / n;
		}
	// water: vertices at a tile full of water
	std::vector<uint8_t> wet(NV, 0);
	for (int z = 0; z < H; z++)
		for (int x = 0; x < W; x++)
			if (t.tiles[(size_t)z * W + x].fullOfWater)
				for (int k = 0; k < 4; k++) wet[(size_t)(z + (k >> 1)) * gw + x + (k & 1)] = 1;
	// the sea level (the generator's, else the plan's)
	float seaLevel = -1e9f;
	const json terrainJ = mapJson.value("terrain", json::object());
	if (terrainJ.contains("generated") && terrainJ["generated"].is_object() && terrainJ["generated"].contains("seaLevel") && terrainJ["generated"]["seaLevel"].is_number())
		seaLevel = terrainJ["generated"]["seaLevel"].get<float>();
	else if (terrainJ.contains("seaLevel") && terrainJ["seaLevel"].is_number())
		seaLevel = terrainJ["seaLevel"].get<float>();
	// the playable land's height range (above the sea), for heightRel
	float lo = 1e9f, hi = -1e9f;
	for (int z = E; z <= H - E; z++)
		for (int x = E; x <= W - E; x++) {
			const float v = h[(size_t)z * gw + x];
			if (wet[(size_t)z * gw + x] || v < seaLevel) continue;
			lo = std::min(lo, v);
			hi = std::max(hi, v);
		}
	if (lo > hi) { lo = 0; hi = 1; }
	const float range = std::max(hi - lo, 1.0f);
	const std::vector<float> seaDist = Distance(wet, gw, gh);
	// fresh water (a lake above the sea's level) waters the land round it more than the sea
	std::vector<uint8_t> fresh(NV, 0);
	for (int z = 0; z < H; z++)
		for (int x = 0; x < W; x++) {
			const TerrainTile& tile = t.tiles[(size_t)z * W + x];
			if (tile.fullOfWater && (seaLevel < -1e8f || std::abs(tile.waterLevel - seaLevel) > 1.0f))
				for (int k = 0; k < 4; k++) fresh[(size_t)(z + (k >> 1)) * gw + x + (k & 1)] = 1;
		}
	const std::vector<float> freshDist = Distance(fresh, gw, gh);
	// the rivers the generator carved: the distance to the course in half-widths
	std::vector<float> river(NV, 1e9f);
	std::vector<uint8_t> riverSrc(NV, 0);
	int riverCount = 0;
	if (terrainJ.contains("generated") && terrainJ["generated"].contains("rivers") && terrainJ["generated"]["rivers"].is_array())
		for (const json& rv : terrainJ["generated"]["rivers"]) {
			const float hw = std::max(2.0f, Num(rv, "width", 20) / 2);
			const json& c = rv.value("course", json::array());
			if (c.size() < 2) continue;
			riverCount++;
			for (size_t i = 0; i + 1 < c.size(); i++) {
				const float ax = c[i][0].get<float>(), az = c[i][1].get<float>(), bx = c[i + 1][0].get<float>(), bz = c[i + 1][1].get<float>();
				const float reach = hw * 3 + 10;
				const int x0 = std::max(0, (int)((std::min(ax, bx) - reach) / TILE) + E), x1 = std::min(gw - 1, (int)((std::max(ax, bx) + reach) / TILE) + E + 1);
				const int z0 = std::max(0, (int)((std::min(az, bz) - reach) / TILE) + E), z1 = std::min(gh - 1, (int)((std::max(az, bz) + reach) / TILE) + E + 1);
				const float abx = bx - ax, abz = bz - az, len2 = std::max(abx * abx + abz * abz, 1e-6f);
				for (int z = z0; z <= z1; z++)
					for (int x = x0; x <= x1; x++) {
						const float px = (x - E) * TILE, pz = (z - E) * TILE;
						const float u = std::clamp(((px - ax) * abx + (pz - az) * abz) / len2, 0.0f, 1.0f);
						const float dxp = px - (ax + abx * u), dzp = pz - (az + abz * u);
						const float d = std::sqrt(dxp * dxp + dzp * dzp) / hw;
						float& rv2 = river[(size_t)z * gw + x];
						rv2 = std::min(rv2, d);
						if (d <= 1) riverSrc[(size_t)z * gw + x] = 1;
					}
			}
		}
	const std::vector<float> riverDist = Distance(riverSrc, gw, gh);
	// drainage: each vertex's water flows to its lowest neighbour (the land as the erosion left it)
	std::vector<float> acc(NV, 1.0f);
	{
		std::vector<uint32_t> order(NV);
		for (size_t i = 0; i < NV; i++) order[i] = (uint32_t)i;
		std::sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) { return h[a] > h[b] || (h[a] == h[b] && a < b); });
		for (uint32_t i : order) {
			const int x = (int)(i % gw), z = (int)(i / gw);
			float best = h[i];
			int bi = -1;
			for (int oz = -1; oz <= 1; oz++)
				for (int ox = -1; ox <= 1; ox++) {
					const int nx = x + ox, nz = z + oz;
					if ((!ox && !oz) || nx < 0 || nz < 0 || nx >= gw || nz >= gh) continue;
					const float d = (h[(size_t)nz * gw + nx] - h[i]) / ((ox && oz) ? 1.41421356f : 1.0f);
					if (h[i] + d < best) { best = h[i] + d; bi = nz * gw + nx; }
				}
			if (bi >= 0 && !wet[i]) acc[bi] += acc[i];
		}
	}
	std::vector<float> drain(NV);
	{
		float maxLog = 1;
		for (size_t i = 0; i < NV; i++) maxLog = std::max(maxLog, std::log(acc[i]));
		// a 3 x 3 blur, so a channel is a band, not a line of vertices
		for (int z = 0; z < gh; z++)
			for (int x = 0; x < gw; x++) {
				float s = 0;
				int n = 0;
				for (int oz = -1; oz <= 1; oz++)
					for (int ox = -1; ox <= 1; ox++) {
						const int nx = x + ox, nz = z + oz;
						if (nx < 0 || nz < 0 || nx >= gw || nz >= gh) continue;
						s += std::log(acc[(size_t)nz * gw + nx]);
						n++;
					}
				drain[(size_t)z * gw + x] = std::clamp(s / n / (maxLog * 0.75f), 0.0f, 1.0f);
			}
	}
	// moisture: a base, more near water and rivers, along the drainage, in valleys (below the land
	// round them, 80 m), and a slow noise; or the map's own layer
	std::vector<float> moist(NV);
	const json mo = ground.value("moisture", json::object());
	const float waterReach = Num(mo, "waterReachM", 180), riverReach = Num(mo, "riverReachM", 140), drainW = Num(mo, "drainage", 0.3f),
		moistNoise = Num(mo, "noise", 0.12f), moistBase = Num(mo, "base", 0.3f), valleyW = Num(mo, "valleys", 0.25f),
		seaWet = Num(mo, "sea", 0.45f);
	// the land round each vertex: a box blur 16 vertices out (prefix sums)
	std::vector<float> broad(NV);
	{
		const int R = 16;
		std::vector<double> sum((size_t)(gw + 1) * (gh + 1), 0.0);
		for (int z = 0; z < gh; z++)
			for (int x = 0; x < gw; x++)
				sum[(size_t)(z + 1) * (gw + 1) + x + 1] = h[(size_t)z * gw + x] + sum[(size_t)z * (gw + 1) + x + 1] + sum[(size_t)(z + 1) * (gw + 1) + x] - sum[(size_t)z * (gw + 1) + x];
		for (int z = 0; z < gh; z++)
			for (int x = 0; x < gw; x++) {
				const int x0 = std::max(0, x - R), x1 = std::min(gw - 1, x + R), z0 = std::max(0, z - R), z1 = std::min(gh - 1, z + R);
				const double s = sum[(size_t)(z1 + 1) * (gw + 1) + x1 + 1] - sum[(size_t)z0 * (gw + 1) + x1 + 1] - sum[(size_t)(z1 + 1) * (gw + 1) + x0] + sum[(size_t)z0 * (gw + 1) + x0];
				broad[(size_t)z * gw + x] = (float)(s / ((double)(x1 - x0 + 1) * (z1 - z0 + 1)));
			}
	}
	std::vector<float> moistLayer;
	const bool moistFromLayer = ReadMask(folder / "layers" / "moisture.png", gw, gh, moistLayer);
	for (int z = 0; z < gh; z++)
		for (int x = 0; x < gw; x++) {
			const size_t i = (size_t)z * gw + x;
			if (moistFromLayer) { moist[i] = moistLayer[i]; continue; }
			const float px = (x - E) * TILE, pz = (z - E) * TILE;
			const float near = std::max({ seaWet * std::exp(-seaDist[i] / waterReach), std::exp(-freshDist[i] / waterReach), std::exp(-riverDist[i] / riverReach) });
			const float valley = std::clamp((broad[i] - h[i]) / 12.0f, -1.0f, 1.0f);
			moist[i] = std::clamp(moistBase + 0.55f * near + drainW * drain[i] + valleyW * valley + moistNoise * Fbm(px / 300, pz / 300, 3, seed ^ 0x6d6f6973u), 0.0f, 1.0f);
		}
	// masks the rules name
	std::map<std::string, std::vector<float>> masks;
	for (const Rule& r : rules)
		if (!r.mask.empty() && !masks.count(r.mask)) {
			std::vector<float> m;
			if (!ReadMask(folder / "layers" / (r.mask + ".png"), gw, gh, m)) {
				warnings.push_back("ground rule " + std::to_string(r.index) + ": no mask layers/" + r.mask + ".png (the rule never holds)");
				m.assign(NV, -1.0f);
			}
			masks[r.mask] = std::move(m);
		}
	const auto tLand = clock::now();
	MapFolder::ReportProgress("the ground: materials and corners", 0.05f);

	// ---- a material per vertex: the first rule that holds ----
	std::vector<int16_t> vm(NV, (int16_t)defaultMat);
	std::vector<uint8_t> thin(NV, 0); // set by a keepThin rule: no opening, no speck removal
	std::vector<int> ruleHits(rules.size(), 0);
	for (int z = 0; z < gh; z++)
		for (int x = 0; x < gw; x++) {
			const size_t i = (size_t)z * gw + x;
			const float px = (x - E) * TILE, pz = (z - E) * TILE;
			float val[C_COUNT];
			val[C_HEIGHT] = h[i];
			val[C_HEIGHTREL] = (h[i] - lo) / range;
			val[C_ABOVESEA] = seaLevel > -1e8f ? h[i] - seaLevel : 1e6f;
			val[C_SLOPE] = slope[i];
			val[C_CURVATURE] = curv[i];
			val[C_MOISTURE] = moist[i];
			val[C_DRAINAGE] = drain[i];
			val[C_RIVER] = river[i];
			val[C_SEADIST] = seaDist[i];
			for (size_t ri = 0; ri < rules.size(); ri++) {
				const Rule& r = rules[ri];
				if (r.water >= 0 && (int)wet[i] != r.water) continue;
				bool ok = true;
				for (const Cond& c : r.conds) {
					float v;
					if (c.what == C_NOISE) v = Unit(px / r.noiseScale, pz / r.noiseScale, seed * 31u + 977u * (uint32_t)(r.index + 1));
					else if (c.what == C_MASK) v = masks[r.mask][i];
					else v = val[c.what];
					const float j = c.jitter > 0 ? c.jitter * Fbm(px / r.jitterScale, pz / r.jitterScale, 3, seed * 17u + 131u * (uint32_t)(r.index + 1) + (uint32_t)c.what) : 0;
					if ((c.r.hasLo && v < c.r.lo + j) || (c.r.hasHi && v > c.r.hi + j)) { ok = false; break; }
				}
				if (ok) {
					vm[i] = (int16_t)r.material;
					thin[i] = r.keepThin;
					ruleHits[ri]++;
					break;
				}
			}
		}
	// no slivers: a morphological opening of each material (opening passes, default 1: a vertex
	// keeps its material only within a 3 x 3 block all of it), so nothing is thinner than three
	// vertices and the bridges between materials have room; what is opened away takes the
	// material most common round it among the vertices that kept theirs
	const int opening = std::max(0, (int)Num(ground, "opening", 1));
	int opened = 0;
	for (int pass = 0; pass < opening; pass++) {
		auto at = [&](int x, int z) { return vm[(size_t)std::clamp(z, 0, gh - 1) * gw + std::clamp(x, 0, gw - 1)]; };
		std::vector<uint8_t> core(NV, 1), keep(thin);
		for (int z = 0; z < gh; z++)
			for (int x = 0; x < gw; x++)
				for (int oz = -1; oz <= 1 && core[(size_t)z * gw + x]; oz++)
					for (int ox = -1; ox <= 1; ox++)
						if (at(x + ox, z + oz) != vm[(size_t)z * gw + x]) { core[(size_t)z * gw + x] = 0; break; }
		for (int z = 0; z < gh; z++)
			for (int x = 0; x < gw; x++) {
				const size_t i = (size_t)z * gw + x;
				for (int oz = -1; oz <= 1 && !keep[i]; oz++)
					for (int ox = -1; ox <= 1; ox++) {
						const int nx = std::clamp(x + ox, 0, gw - 1), nz = std::clamp(z + oz, 0, gh - 1);
						if (core[(size_t)nz * gw + nx] && vm[(size_t)nz * gw + nx] == vm[i]) { keep[i] = 1; break; }
					}
			}
		// the vertices left: the commonest kept material round them, growing inwards
		for (int round = 0; round < 8; round++) {
			bool any = false;
			std::vector<int16_t> nv = vm;
			std::vector<uint8_t> nk = keep;
			for (int z = 0; z < gh; z++)
				for (int x = 0; x < gw; x++) {
					const size_t i = (size_t)z * gw + x;
					if (keep[i]) continue;
					std::map<int, int> count;
					for (int oz = -1; oz <= 1; oz++)
						for (int ox = -1; ox <= 1; ox++) {
							const int nx = x + ox, nz = z + oz;
							if ((!ox && !oz) || nx < 0 || nz < 0 || nx >= gw || nz >= gh) continue;
							if (keep[(size_t)nz * gw + nx]) count[vm[(size_t)nz * gw + nx]]++;
						}
					if (count.empty()) { any = true; continue; }
					int best = vm[i], bn = 0;
					for (auto& [m, n] : count) if (n > bn || (n == bn && m == vm[i])) { bn = n; best = m; }
					opened += best != vm[i];
					nv[i] = (int16_t)best;
					nk[i] = 1;
				}
			vm.swap(nv);
			keep.swap(nk);
			if (!any) break;
		}
	}
	// no specks: a material's patch smaller than minPatch vertices (4-connected) takes the material
	// most common round it (twice, as a patch absorbed can leave a smaller one beside it)
	const int minPatch = std::max(1, (int)Num(ground, "minPatch", 16));
	int absorbed = 0;
	for (int round = 0; round < 2 && minPatch > 1; round++) {
		std::vector<int> comp(NV, -1);
		std::vector<int> stack, members;
		for (size_t s0 = 0; s0 < NV; s0++) {
			if (comp[s0] >= 0) continue;
			const int m = vm[s0];
			members.clear();
			stack.assign(1, (int)s0);
			comp[s0] = (int)s0;
			std::map<int, int> around;
			while (!stack.empty()) {
				const int v = stack.back();
				stack.pop_back();
				members.push_back(v);
				const int x = v % gw, z = v / gw;
				const int nb[4][2] = { { x + 1, z }, { x - 1, z }, { x, z + 1 }, { x, z - 1 } };
				for (auto& q : nb) {
					if (q[0] < 0 || q[1] < 0 || q[0] >= gw || q[1] >= gh) continue;
					const int u = q[1] * gw + q[0];
					if (vm[u] == m) {
						if (comp[u] < 0) { comp[u] = (int)s0; stack.push_back(u); }
					}
					else around[vm[u]]++;
				}
			}
			if ((int)members.size() >= minPatch || around.empty() || thin[members[0]]) continue;
			int best = -1, bn = -1;
			for (auto& [mm, n] : around) if (n > bn) { bn = n; best = mm; }
			for (int v : members) vm[v] = (int16_t)best;
			absorbed += (int)members.size();
		}
	}
	// a material's group per vertex: large patches by its weights, its first group alone within
	// four vertices of another material (three groups can't meet in a pair of tiles: the EDGE rules
	// have no tiles for it, so the patches stay inside)
	std::vector<uint8_t> nearOther(NV, 0);
	{
		std::vector<uint8_t> border(NV, 0);
		for (int z = 0; z < gh; z++)
			for (int x = 0; x < gw; x++)
				for (int oz = -1; oz <= 1 && !border[(size_t)z * gw + x]; oz++)
					for (int ox = -1; ox <= 1; ox++) {
						const int nx = x + ox, nz = z + oz;
						if (nx >= 0 && nz >= 0 && nx < gw && nz < gh && vm[(size_t)nz * gw + nx] != vm[(size_t)z * gw + x]) { border[(size_t)z * gw + x] = 1; break; }
					}
		const std::vector<float> d = Distance(border, gw, gh);
		for (size_t i = 0; i < NV; i++) nearOther[i] = d[i] <= 4 * TILE;
	}
	std::vector<int16_t> vg(NV);
	for (int z = 0; z < gh; z++)
		for (int x = 0; x < gw; x++) {
			const size_t i = (size_t)z * gw + x;
			const Material& m = mats[vm[i]];
			if (m.groups.size() == 1 || nearOther[i]) { vg[i] = (int16_t)m.groups[0].first; continue; }
			float total = 0;
			for (auto& g : m.groups) total += g.second;
			const float px = (x - E) * TILE, pz = (z - E) * TILE;
			float u = Unit(px / m.patchScale, pz / m.patchScale, seed * 7u + 7919u * (uint32_t)(vm[i] + 1)) * total;
			int pick = m.groups.back().first;
			for (auto& g : m.groups) {
				if (u < g.second) { pick = g.first; break; }
				u -= g.second;
			}
			vg[i] = (int16_t)pick;
		}

	// ---- the hand tiles' corners stay ----
	std::vector<uint8_t> fixedV(NV, 0);
	int handFixedConflicts = 0;
	for (int z = 0; z < H; z++)
		for (int x = 0; x < W; x++) {
			const size_t ti = (size_t)z * W + x;
			if (!hand[ti]) continue;
			const TerrainTile& tile = t.tiles[ti];
			const Placed p{ tile.texture, (uint8_t)OR().ofCode[(tile.rot & 3) | (tile.xflip & 1) << 2 | (tile.zflip & 1) << 3] };
			const std::array<int, 4> c = L.worldCorners(p);
			const int vi[4] = { z * gw + x, z * gw + x + 1, (z + 1) * gw + x + 1, (z + 1) * gw + x };
			for (int k = 0; k < 4; k++) {
				if (c[k] < 0) continue;
				if (fixedV[vi[k]] && vg[vi[k]] != c[k]) { handFixedConflicts++; continue; }
				vg[vi[k]] = (int16_t)c[k];
				fixedV[vi[k]] = 1;
			}
		}

	// ---- every tile's corners made paintable: at most two groups, with a transition between them,
	// in a pattern some picture has; a group that has no transition to its neighbour's gets the
	// next group on the way to it (grass to rock: through the hillside) ----
	int legalPasses = 0, changedTotal = 0;
	auto patternOk = [&](const std::array<int, 4>& c) {
		const int a = *std::min_element(c.begin(), c.end()), b = *std::max_element(c.begin(), c.end());
		if (a == b) return !L.pure[a].empty();
		for (int k = 0; k < 4; k++) if (c[k] != a && c[k] != b) return false;
		auto it = L.fits.find({ a, b });
		if (it == L.fits.end()) return false;
		int bits = 0;
		for (int k = 0; k < 4; k++) if (c[k] == b) bits |= 1 << k;
		return !it->second[bits].empty();
	};
	// The EDGE rules let a transition tile touch only its own two groups' tiles (CLIFF1-GRASS1
	// borders CLIFF1, GRASS1 and CLIFF1-GRASS1, never GRASS1-SAND1): so any two tiles side by side
	// may hold two groups between them, and those two must have a transition. A window (two tiles
	// side by side: 3 x 2 vertices) costs by how far it is from that: per pair of its groups the
	// transitions between them beyond one, per group beyond two, and a tile whose two groups lie in a
	// pattern no picture has. The graph distance gives the search a slope: a bridging group lowers
	// the cost step by step (grass to rock: through the hillside).
	auto windowCost = [&](int x0, int z0, int wx, int wz) {
		if (x0 < 0 || z0 < 0 || x0 + wx >= gw || z0 + wz >= gh) return 0;
		int g[6], n = 0;
		for (int z = z0; z <= z0 + wz; z++)
			for (int x = x0; x <= x0 + wx; x++) {
				const int v = vg[(size_t)z * gw + x];
				bool seen = false;
				for (int k = 0; k < n; k++) seen = seen || g[k] == v;
				if (!seen) g[n++] = v;
			}
		if (n == 1) return 0;
		int c = 0;
		for (int i = 0; i < n; i++)
			for (int j = i + 1; j < n; j++) c += 4 * std::min(L.dist[g[i]][g[j]] - 1, 6);
		return c + 6 * std::max(0, n - 2);
	};
	auto tileCost = [&](int x, int z) {
		if (x < 0 || z < 0 || x >= W || z >= H || hand[(size_t)z * W + x]) return 0;
		std::array<int, 4> c = { vg[(size_t)z * gw + x], vg[(size_t)z * gw + x + 1], vg[(size_t)(z + 1) * gw + x + 1], vg[(size_t)(z + 1) * gw + x] };
		return patternOk(c) ? 0 : 2;
	};
	// the cost round a vertex: the windows and tiles it is in
	auto around = [&](int x, int z) {
		int c = 0;
		for (int tz = z - 1; tz <= z; tz++)
			for (int tx = x - 2; tx <= x; tx++) c += windowCost(tx, tz, 2, 1);
		for (int tz = z - 2; tz <= z; tz++)
			for (int tx = x - 1; tx <= x; tx++) c += windowCost(tx, tz, 1, 2);
		for (int tz = z - 1; tz <= z; tz++)
			for (int tx = x - 1; tx <= x; tx++) c += tileCost(tx, tz);
		return c;
	};
	auto bad = [&](int x, int z) { return windowCost(x, z, 2, 1) || windowCost(x, z, 1, 2) || tileCost(x, z); };
	// the groups the rules want (after the despeckle): a vertex leaves its own only to make its tiles paintable
	const std::vector<int16_t> want = vg;
	// groups that block land units (a bridge through one would block a way the land leaves open)
	std::vector<uint8_t> blocks(G, 0);
	for (int g = 0; g < G; g++) {
		const json gj = data["groups"].value(L.groups[g], json::object());
		blocks[g] = gj.contains("landPassable") && gj["landPassable"].is_boolean() && !gj["landPassable"].get<bool>();
	}
	// A constructive pass first: the vertices in the rules' order (the first rule's material first,
	// the default last; within one, row by row), each taking the group nearest its wish (by
	// transitions; a blocking bridge costs more) that fits every vertex already placed in the
	// windows round it. So a river bed or a cliff stays where its rule put it and the ground
	// round it gives way, with the bridges on the side of the later rule.
	{
		std::vector<int> matPriority(mats.size(), (int)rules.size() + 1);
		for (const Rule& r : rules) matPriority[r.material] = std::min(matPriority[r.material], r.index);
		matPriority[defaultMat] = std::min(matPriority[defaultMat], (int)rules.size());
		std::vector<uint8_t> placed(NV, 0);
		for (size_t i = 0; i < NV; i++) placed[i] = fixedV[i];
		// the order: level by level (the rules' order); within a level a wavefront outwards from what
		// is placed already, so the bridges at a border are laid before the ground beyond them
		std::vector<uint32_t> order;
		order.reserve(NV);
		{
			std::vector<int> levelOf(NV);
			int maxLevel = 0;
			for (size_t i = 0; i < NV; i++) {
				levelOf[i] = fixedV[i] ? -1 : matPriority[vm[i]];
				maxLevel = std::max(maxLevel, levelOf[i]);
			}
			std::vector<std::vector<uint32_t>> byLevel(maxLevel + 1);
			for (size_t i = 0; i < NV; i++)
				if (levelOf[i] >= 0) byLevel[levelOf[i]].push_back((uint32_t)i);
			std::vector<uint8_t> done(NV, 0);
			for (size_t i = 0; i < NV; i++) done[i] = fixedV[i];
			std::vector<uint32_t> queue;
			for (int lv = 0; lv <= maxLevel; lv++) {
				if (byLevel[lv].empty()) continue;
				auto nearDone = [&](uint32_t v) {
					const int x = v % gw, z = v / gw;
					for (int oz = -1; oz <= 1; oz++)
						for (int ox = -1; ox <= 1; ox++) {
							const int nx = x + ox, nz = z + oz;
							if ((ox || oz) && nx >= 0 && nz >= 0 && nx < gw && nz < gh && done[(size_t)nz * gw + nx] == 1) return true;
						}
					return false;
				};
				queue.clear();
				for (uint32_t v : byLevel[lv])
					if (nearDone(v)) { queue.push_back(v); done[v] = 2; }
				size_t head = 0, seedAt = 0;
				for (;;) {
					if (head == queue.size()) {
						// an area the wave doesn't reach: a new seed, row by row
						while (seedAt < byLevel[lv].size() && done[byLevel[lv][seedAt]]) seedAt++;
						if (seedAt == byLevel[lv].size()) break;
						queue.push_back(byLevel[lv][seedAt]);
						done[byLevel[lv][seedAt]] = 2;
					}
					const uint32_t v = queue[head++];
					order.push_back(v);
					const int x = v % gw, z = v / gw;
					for (int oz = -1; oz <= 1; oz++)
						for (int ox = -1; ox <= 1; ox++) {
							const int nx = x + ox, nz = z + oz;
							if ((!ox && !oz) || nx < 0 || nz < 0 || nx >= gw || nz >= gh) continue;
							const uint32_t u = (uint32_t)(nz * gw + nx);
							if (!done[u] && levelOf[u] == lv) { done[u] = 2; queue.push_back(u); }
						}
				}
				for (uint32_t v : byLevel[lv]) done[v] = 1;
			}
		}
		// whether g at v fits the placed vertices of every window round v (at most two groups, with a
		// transition between them) and of every tile whose corners are then all placed
		auto fits = [&](int v, int g, bool lookahead = true) {
			const int x = v % gw, z = v / gw;
			// room for the bridges: a placed vertex k vertices away may be at most (k + 1) / 2
			// transitions from g (a bridging group needs two vertices across, as a window is 3 wide)
			for (int oz = -6; oz <= 6; oz++)
				for (int ox = -6; ox <= 6; ox++) {
					const int k = std::max(std::abs(ox), std::abs(oz));
					if (k < 2 || !lookahead) continue;
					const int nx = x + ox, nz = z + oz;
					if (nx < 0 || nz < 0 || nx >= gw || nz >= gh) continue;
					const int u = nz * gw + nx;
					if (placed[u] && L.dist[g][vg[u]] > (k + 1) / 2) return false;
				}
			for (int dir = 0; dir < 2; dir++) {
				const int wx = dir ? 1 : 2, wz = dir ? 2 : 1;
				for (int z0 = z - wz; z0 <= z; z0++)
					for (int x0 = x - wx; x0 <= x; x0++) {
						if (x0 < 0 || z0 < 0 || x0 + wx >= gw || z0 + wz >= gh) continue;
						int other = -1;
						for (int zz = z0; zz <= z0 + wz; zz++)
							for (int xx = x0; xx <= x0 + wx; xx++) {
								const int u = zz * gw + xx;
								if (u == v || !placed[u]) continue;
								const int gu = vg[u];
								if (gu == g) continue;
								if (other >= 0 && gu != other) return false;
								if (L.dist[g][gu] > 1) return false;
								other = gu;
							}
					}
			}
			for (int tz = z - 1; tz <= z; tz++)
				for (int tx = x - 1; tx <= x; tx++) {
					if (tx < 0 || tz < 0 || tx >= W || tz >= H || hand[(size_t)tz * W + tx]) continue;
					std::array<int, 4> c;
					bool full = true;
					const int vi[4] = { tz * gw + tx, tz * gw + tx + 1, (tz + 1) * gw + tx + 1, (tz + 1) * gw + tx };
					for (int k = 0; k < 4; k++) {
						if (vi[k] == v) c[k] = g;
						else if (placed[vi[k]]) c[k] = vg[vi[k]];
						else full = false;
					}
					if (full && !patternOk(c)) return false;
				}
			return true;
		};
		std::vector<std::pair<int, int>> byCost(G);
		for (uint32_t v : order) {
			if (placed[v]) continue;
			const int w = want[v];
			if (!fits((int)v, w)) {
				for (int g = 0; g < G; g++)
					byCost[g] = { 3 * std::min(L.dist[w][g], 30) + (blocks[g] && !blocks[w] ? 2 : 0), g };
				std::sort(byCost.begin(), byCost.end());
				int pick = -1;
				for (auto& [c, g] : byCost) {
					if (c >= 90) break;
					if (fits((int)v, g)) { pick = g; break; }
				}
				if (pick < 0)
					// no room for every bridge: the nearest group that fits the windows round it as placed
					for (auto& [c, g] : byCost) {
						if (c >= 90) break;
						if (fits((int)v, g, false)) { pick = g; break; }
					}
				if (pick < 0) {
					// nothing fits: the group that breaks the fewest windows round it (the search below
					// and the report take it from there)
					int bestV = 1 << 30;
					for (auto& [c, g] : byCost) {
						if (c >= 90) break;
						const int16_t keep = vg[v];
						vg[v] = (int16_t)g;
						const int viol = around((int)v % gw, (int)v / gw);
						vg[v] = keep;
						if (viol < bestV) { bestV = viol; pick = g; }
					}
				}
				if (pick >= 0) vg[v] = (int16_t)pick;
			}
			placed[v] = 1;
		}
		// Trouble spots again: the vertices round every window that still can't be painted (3
		// vertices out) are lifted and laid again from the ground round them inwards, each now
		// wishing for the group most common among the placed vertices near it (the spot gives
		// way to its surroundings)
		for (int retry = 0; retry < 4; retry++) {
			std::vector<uint8_t> lift(NV, 0);
			int lifted = 0;
			for (int z = 0; z < H; z++)
				for (int x = 0; x < W; x++)
					if (bad(x, z))
						for (int oz = -3; oz <= 5; oz++)
							for (int ox = -3; ox <= 5; ox++) {
								const int nx = x + ox, nz = z + oz;
								if (nx < 0 || nz < 0 || nx >= gw || nz >= gh) continue;
								const size_t u = (size_t)nz * gw + nx;
								if (!fixedV[u] && !lift[u]) { lift[u] = 1; lifted++; }
							}
			if (!lifted) break;
			for (size_t i = 0; i < NV; i++) if (lift[i]) placed[i] = 0;
			// the wave from the edge of each spot inwards
			std::vector<uint32_t> wave;
			std::vector<uint8_t> inWave(NV, 0);
			for (size_t i = 0; i < NV; i++) {
				if (!lift[i]) continue;
				const int x = (int)(i % gw), z = (int)(i / gw);
				bool edge = false;
				for (int oz = -1; oz <= 1 && !edge; oz++)
					for (int ox = -1; ox <= 1; ox++) {
						const int nx = x + ox, nz = z + oz;
						if (nx >= 0 && nz >= 0 && nx < gw && nz < gh && placed[(size_t)nz * gw + nx]) { edge = true; break; }
					}
				if (edge) { wave.push_back((uint32_t)i); inWave[i] = 1; }
			}
			for (size_t head = 0; head < wave.size(); head++) {
				const uint32_t v = wave[head];
				const int x = v % gw, z = v / gw;
				for (int oz = -1; oz <= 1; oz++)
					for (int ox = -1; ox <= 1; ox++) {
						const int nx = x + ox, nz = z + oz;
						if (nx < 0 || nz < 0 || nx >= gw || nz >= gh) continue;
						const size_t u = (size_t)nz * gw + nx;
						if (lift[u] && !inWave[u]) { inWave[u] = 1; wave.push_back((uint32_t)u); }
					}
			}
			for (size_t i = 0; i < NV; i++) if (lift[i] && !inWave[i]) wave.push_back((uint32_t)i);
			for (uint32_t v : wave) {
				const int x = v % gw, z = v / gw;
				std::map<int, int> near;
				for (int oz = -3; oz <= 3; oz++)
					for (int ox = -3; ox <= 3; ox++) {
						const int nx = x + ox, nz = z + oz;
						if (nx < 0 || nz < 0 || nx >= gw || nz >= gh) continue;
						const size_t u = (size_t)nz * gw + nx;
						if (placed[u]) near[vg[u]] += 4 - std::max(std::abs(ox), std::abs(oz));
					}
				int w = want[v], bn = -1;
				for (auto& [g, n] : near) if (n > bn) { bn = n; w = g; }
				int pick = -1;
				for (int pass = 0; pass < 2 && pick < 0; pass++) {
					for (int g = 0; g < G; g++)
						byCost[g] = { 3 * std::min(L.dist[w][g], 30) + (blocks[g] && !blocks[w] ? 2 : 0) + (g == want[v] ? -1 : 0), g };
					std::sort(byCost.begin(), byCost.end());
					for (auto& [c, g] : byCost) {
						if (c >= 90) break;
						if (fits((int)v, g, pass == 0)) { pick = g; break; }
					}
				}
				vg[v] = (int16_t)(pick >= 0 ? pick : w);
				placed[v] = 1;
			}
		}
	}
	const std::vector<int16_t> constructed = vg;
	// min-conflict search over the vertices of windows that can't be painted: each takes the group
	// (its own, its wish, a neighbour's, or the next on the way to one) that leaves the cost round
	// it lowest, a step from what the rules want costing a little
	std::vector<uint8_t> queued(NV, 0);
	for (int pass = 0; pass < 24; pass++) {
		MapFolder::ReportProgress("the ground: materials and corners", 0.08f + 0.25f * pass / 24);
		std::vector<int> todo;
		for (int z = 0; z < H; z++)
			for (int x = 0; x < W; x++)
				if (bad(x, z))
					for (int oz = 0; oz <= 2; oz++)
						for (int ox = 0; ox <= 2; ox++) {
							const int vx = x + ox, vz = z + oz;
							if (vx >= gw || vz >= gh) continue;
							const int v = vz * gw + vx;
							if (!fixedV[v] && !queued[v]) { queued[v] = 1; todo.push_back(v); }
						}
		if (todo.empty()) break;
		if (pass & 1) std::reverse(todo.begin(), todo.end());
		int changed = 0;
		for (int v : todo) {
			queued[v] = 0;
			const int x = v % gw, z = v / gw;
			auto cost = [&](int g) {
				const int16_t keep = vg[v];
				vg[v] = (int16_t)g;
				const int c = 10 * around(x, z) + 3 * std::min(L.dist[g][want[v]], 6);
				vg[v] = keep;
				return c;
			};
			std::vector<int> cands = { vg[v], want[v] };
			for (int oz = -2; oz <= 2; oz++)
				for (int ox = -2; ox <= 2; ox++) {
					const int nx = x + ox, nz = z + oz;
					if ((!ox && !oz) || nx < 0 || nz < 0 || nx >= gw || nz >= gh) continue;
					const int g = vg[(size_t)nz * gw + nx];
					cands.push_back(g);
					if (L.next[vg[v]][g] >= 0) cands.push_back(L.next[vg[v]][g]);
					if (L.next[want[v]][g] >= 0) cands.push_back(L.next[want[v]][g]);
					if (L.next[g][want[v]] >= 0) cands.push_back(L.next[g][want[v]]);
				}
			std::sort(cands.begin(), cands.end());
			cands.erase(std::unique(cands.begin(), cands.end()), cands.end());
			const int now = cost(vg[v]);
			int best = vg[v], bestC = now;
			uint32_t bestH = 0;
			for (int g : cands) {
				if (g == vg[v]) continue;
				const int c = cost(g);
				const uint32_t hh = Hash(seed, (uint32_t)v, (uint32_t)g, (uint32_t)pass);
				if (c < bestC || (c == bestC && c < now && hh > bestH)) { bestC = c; best = g; bestH = hh; }
			}
			// a sideways move now and then (as good as staying), so two vertices don't hold each other
			if (best == vg[v] && pass > 6 && (Hash(seed ^ 0xabcu, (uint32_t)v, (uint32_t)pass) % 3) == 0) {
				for (int g : cands)
					if (g != vg[v] && cost(g) == now) { best = g; break; }
			}
			if (best != vg[v]) { vg[v] = (int16_t)best; changed++; }
		}
		legalPasses = pass + 1;
		changedTotal += changed;
		if (!changed) break;
	}
	// Last resort, where the search left a window it can't paint (three groups meeting where no
	// picture joins them, mostly on mountains: scree, rock, hillside and grass in a few metres): a
	// patch of one group over it, the one among those round it that leaves the least wrong round
	// the patch (a step from what the rules want breaking ties), the patch a vertex wider each round
	// until its rim fits. Only taken when it lowers the cost there.
	int patched = 0, patchRounds = 0, badBeforePatches = 0, badAfterPatches = 0;
	{
		auto regionCost = [&](int x0, int z0, int x1, int z1) {
			int c = 0;
			for (int z = z0 - 2; z <= z1; z++)
				for (int x = x0 - 2; x <= x1; x++)
					c += windowCost(x, z, 2, 1) + windowCost(x, z, 1, 2) + tileCost(x, z);
			return c;
		};
		std::vector<int16_t> keep;
		for (int r = 0; r < 8; r++) {
			std::vector<std::pair<int, int>> badTiles;
			for (int z = 0; z < H; z++)
				for (int x = 0; x < W; x++)
					if (bad(x, z)) badTiles.push_back({ x, z });
			if (r == 0) badBeforePatches = (int)badTiles.size();
			badAfterPatches = (int)badTiles.size();
			if (badTiles.empty()) break;
			patchRounds = r + 1;
			MapFolder::ReportProgress("the ground: mending where three grounds meet", 0.33f + 0.11f * r / 8);
			for (auto [x, z] : badTiles) {
				if (!bad(x, z)) continue;
				const int x0 = std::max(0, x - r), z0 = std::max(0, z - r), x1 = std::min(gw - 1, x + 2 + r), z1 = std::min(gh - 1, z + 2 + r);
				std::vector<int> cands;
				for (int vz = std::max(0, z0 - 1); vz <= std::min(gh - 1, z1 + 1); vz++)
					for (int vx = std::max(0, x0 - 1); vx <= std::min(gw - 1, x1 + 1); vx++)
						cands.push_back(vg[(size_t)vz * gw + vx]);
				std::sort(cands.begin(), cands.end());
				cands.erase(std::unique(cands.begin(), cands.end()), cands.end());
				// the patch and a ring round it: the ring's vertices may take the group on the way
				// from theirs to the patch's (a bridge), so the patch meets what lies round it
				const int rx0 = std::max(0, x0 - 1), rz0 = std::max(0, z0 - 1), rx1 = std::min(gw - 1, x1 + 1), rz1 = std::min(gh - 1, z1 + 1);
				keep.clear();
				for (int vz = rz0; vz <= rz1; vz++)
					for (int vx = rx0; vx <= rx1; vx++) keep.push_back(vg[(size_t)vz * gw + vx]);
				auto restore = [&]() {
					size_t k = 0;
					for (int vz = rz0; vz <= rz1; vz++)
						for (int vx = rx0; vx <= rx1; vx++) vg[(size_t)vz * gw + vx] = keep[k++];
				};
				const int before = regionCost(rx0, rz0, rx1, rz1);
				int best = -1, bestC = before, bestW = 1 << 30;
				bool bestRing = false;
				auto apply = [&](int g, bool ring) {
					int wdist = 0;
					for (int vz = rz0; vz <= rz1; vz++)
						for (int vx = rx0; vx <= rx1; vx++) {
							const size_t v = (size_t)vz * gw + vx;
							if (fixedV[v]) continue;
							const bool inner = vx >= x0 && vx <= x1 && vz >= z0 && vz <= z1;
							if (inner) vg[v] = (int16_t)g;
							else if (ring && L.dist[keep[(size_t)(vz - rz0) * (rx1 - rx0 + 1) + (vx - rx0)]][g] > 1) {
								const int nb = L.next[keep[(size_t)(vz - rz0) * (rx1 - rx0 + 1) + (vx - rx0)]][g];
								if (nb >= 0 && !L.pure[nb].empty()) vg[v] = (int16_t)nb;
							}
							wdist += std::min(L.dist[vg[v]][want[v]], 6);
						}
					if (blocks[g]) wdist += 1000; // (a blocking patch only when nothing else fits)
					return wdist;
				};
				for (int g : cands) {
					if (L.pure[g].empty()) continue; // a group with no pictures of its own can't fill a patch
					for (bool ring : { false, true }) {
						const int wdist = apply(g, ring);
						const int c = regionCost(rx0, rz0, rx1, rz1);
						if (c < bestC || (c == bestC && best >= 0 && wdist < bestW)) { bestC = c; best = g; bestW = wdist; bestRing = ring; }
						restore();
					}
				}
				if (best < 0) continue;
				apply(best, bestRing);
				patched++;
			}
		}
	}
	{
		int diff = 0;
		for (size_t i = 0; i < NV; i++) diff += vg[i] != want[i];
		changedTotal = diff;
	}
	const int localFixed = 0, localTried = 0; // (a local block search found nothing the passes above hadn't: removed)
	// test runs: "WKB_MATERIALS_DUMP=<file>": the groups' names, then per vertex row the wished and
	// the placed group index
	if (const char* dump = getenv("WKB_MATERIALS_DUMP")) {
		json d = { { "groups", L.groups }, { "gw", gw }, { "gh", gh } };
		json w = json::array(), g = json::array();
		for (size_t i = 0; i < NV; i++) { w.push_back(want[i]); g.push_back(vg[i]); }
		d["want"] = w;
		json c = json::array();
		for (size_t i = 0; i < NV; i++) c.push_back(constructed[i]);
		d["constructed"] = c;
		d["got"] = g;
		WriteAll(PathFromUtf8(std::string(dump) + "." + PathToUtf8(folder.filename()) + ".json"), d.dump());
	}
	const auto tLegal = clock::now();
	MapFolder::ReportProgress("the ground: placing tiles", 0.44f);

	// ---- the tiles: per tile the pictures that fit its corners; then the edges: each tile one
	// whose four sides fit its neighbours by the EDGE rules (first a scan, then repairs) ----
	struct Unmatched { int x, z; std::string kind, detail; };
	std::vector<Unmatched> unmatched;
	std::vector<const std::vector<Placed>*> cand(NT, nullptr);
	std::deque<std::vector<Placed>> fallbackCands; // (a deque: the tiles keep pointers into it)
	std::vector<Placed> cur(NT, Placed{ nullptr, 0 });
	for (int z = 0; z < H; z++)
		for (int x = 0; x < W; x++) {
			const size_t ti = (size_t)z * W + x;
			const TerrainTile& tile = t.tiles[ti];
			if (hand[ti]) {
				cur[ti] = { tile.texture, (uint8_t)OR().ofCode[(tile.rot & 3) | (tile.xflip & 1) << 2 | (tile.zflip & 1) << 3] };
				continue;
			}
			const int vi[4] = { z * gw + x, z * gw + x + 1, (z + 1) * gw + x + 1, (z + 1) * gw + x };
			std::array<int, 4> c;
			for (int k = 0; k < 4; k++) c[k] = vg[vi[k]];
			const int a = *std::min_element(c.begin(), c.end()), b = *std::max_element(c.begin(), c.end());
			if (a == b && !L.pure[a].empty()) { cand[ti] = &L.pure[a]; continue; }
			if (patternOk(c)) {
				int bits = 0;
				for (int k = 0; k < 4; k++) if (c[k] == b) bits |= 1 << k;
				cand[ti] = &L.fits.at({ a, b })[bits];
				continue;
			}
			// no picture fits these corners: the pictures nearest them (transitions too), a corner
			// of its own group counting 2, one a transition away 1, so the seam fades rather
			// than stepping as a square of one material (reported)
			auto score = [&](const std::array<int, 4>& pc) {
				int sc = 0;
				for (int k = 0; k < 4; k++) sc += pc[k] == c[k] ? 2 : (pc[k] >= 0 && L.dist[pc[k]][c[k]] <= 1 ? 1 : 0);
				return sc;
			};
			int bestScore = -1;
			std::vector<Placed> bestList;
			auto consider = [&](const std::vector<Placed>& list) {
				for (const Placed& pl : list) {
					const int sc = score(L.worldCorners(pl));
					if (sc > bestScore) { bestScore = sc; bestList.clear(); }
					if (sc == bestScore) bestList.push_back(pl);
				}
			};
			std::set<int> seen;
			for (int k = 0; k < 4; k++)
				if (seen.insert(c[k]).second) consider(L.pure[c[k]]);
			for (const auto& [pair, table] : L.fits)
				if (seen.count(pair.first) || seen.count(pair.second))
					for (const auto& list : table) consider(list);
			std::string d = "corners";
			for (int k = 0; k < 4; k++) d += " " + L.groups[c[k]];
			fallbackCands.push_back(bestList);
			const Placed& shown = bestList.empty() ? L.pure[c[0]].front() : bestList.front();
			unmatched.push_back({ x, z, "corners", d + ": no picture joins them; the nearest used (" + shown.tex->grp->name + ", " + std::to_string(bestScore) + " of 8)" });
			cand[ti] = &fallbackCands.back();
		}
	auto conflicts = [&](int x, int z, const Placed& p) {
		int n = 0;
		for (int w = 0; w < 4; w++) {
			const int nx = x + DX[w], nz = z + DZ[w];
			if (nx < 0 || nz < 0 || nx >= W || nz >= H) continue;
			const Placed& q = cur[(size_t)nz * W + nx];
			if (!q.tex) continue;
			n += !L.fit(p, w, q);
		}
		return n;
	};
	auto choose = [&](int x, int z, uint32_t salt) {
		const size_t ti = (size_t)z * W + x;
		const std::vector<Placed>& cs = *cand[ti];
		if (cs.empty()) return 0;
		int bestN = 99;
		std::vector<int> best;
		// a large list is sampled (a material of 16 pictures has 128 placings)
		const size_t step = cs.size() > 96 ? 3 : 1;
		const size_t off = step > 1 ? Hash(seed, (uint32_t)ti, salt) % step : 0;
		for (size_t k = off; k < cs.size(); k += step) {
			const int n = conflicts(x, z, cs[k]);
			if (n < bestN) { bestN = n; best.clear(); }
			if (n == bestN) best.push_back((int)k);
		}
		cur[ti] = cs[best[Hash(seed ^ 0x51u, (uint32_t)x, (uint32_t)z, salt) % best.size()]];
		return bestN;
	};
	for (int z = 0; z < H; z++) {
		if ((z & 31) == 0)
			MapFolder::ReportProgress("the ground: placing tiles", 0.44f + 0.19f * z / H);
		for (int x = 0; x < W; x++)
			if (cand[(size_t)z * W + x]) choose(x, z, 0);
	}
	int repairPasses = 0;
	for (int pass = 1; pass <= 12; pass++) {
		MapFolder::ReportProgress("the ground: fitting the edges", 0.63f + 0.25f * pass / 12);
		int bad = 0, fixed = 0;
		for (int z = 0; z < H; z++)
			for (int x = 0; x < W; x++) {
				const size_t ti = (size_t)z * W + x;
				if (!cand[ti]) continue;
				if (!conflicts(x, z, cur[ti])) continue;
				bad++;
				fixed += choose(x, z, (uint32_t)pass) == 0;
			}
		repairPasses = pass;
		if (!bad || !fixed) break;
	}
	// an edge still wrong: both its tiles chosen together (every pair of their pictures, against
	// their other neighbours too)
	for (int pass = 0; pass < 2; pass++)
		for (int z = 0; z < H; z++)
			for (int x = 0; x < W; x++)
				for (int w : { 0, 1 }) {
					const int nx = x + DX[w], nz = z + DZ[w];
					if (nx >= W || nz >= H) continue;
					const size_t a = (size_t)z * W + x, b = (size_t)nz * W + nx;
					if (!cur[a].tex || !cur[b].tex || L.fit(cur[a], w, cur[b])) continue;
					if (!cand[a] && !cand[b]) continue;
					const std::vector<Placed> onlyA{ cur[a] }, onlyB{ cur[b] };
					const std::vector<Placed>& ca = cand[a] ? *cand[a] : onlyA;
					const std::vector<Placed>& cb = cand[b] ? *cand[b] : onlyB;
					const Placed keepA = cur[a], keepB = cur[b];
					int best = conflicts(x, z, keepA) + conflicts(nx, nz, keepB);
					Placed bestA = keepA, bestB = keepB;
					for (const Placed& pa : ca) {
						cur[a] = pa;
						const int na = conflicts(x, z, pa);
						if (na >= best) continue;
						for (const Placed& pb : cb) {
							cur[b] = pb;
							const int n = conflicts(x, z, pa) + conflicts(nx, nz, pb);
							if (n < best) { best = n; bestA = pa; bestB = pb; }
						}
						cur[b] = keepB;
					}
					cur[a] = bestA;
					cur[b] = bestB;
				}
	// into the terrain
	std::map<std::string, int> groupTiles;
	for (int z = 0; z < H; z++)
		for (int x = 0; x < W; x++) {
			const size_t ti = (size_t)z * W + x;
			TerrainTile& tile = t.tiles[ti];
			if (cand[ti] && cur[ti].tex) {
				const int code = OR().code[cur[ti].orient];
				// any of the codes that give this orientation: a turn of 2 is both flips
				int c2 = code;
				if (Hash(seed, (uint32_t)x, (uint32_t)z, 0x7f) & 1) {
					for (int k = 0; k < 16; k++)
						if (k != code && OR().ofCode[k] == cur[ti].orient) { c2 = k; break; }
				}
				tile.texture = cur[ti].tex;
				tile.rot = c2 & 3;
				tile.xflip = (c2 >> 2) & 1;
				tile.zflip = (c2 >> 3) & 1;
				ruleTile[ti] = 1;
			}
			if (tile.texture && tile.texture->grp)
				groupTiles[tile.texture->grp->name]++;
		}
	// the edges that still don't fit
	int edgeBad = 0, edgeBadHand = 0;
	std::map<std::string, int> badPairs;
	for (int z = 0; z < H; z++)
		for (int x = 0; x < W; x++)
			for (int w : { 0, 1 }) {
				const int nx = x + DX[w], nz = z + DZ[w];
				if (nx >= W || nz >= H) continue;
				const size_t a = (size_t)z * W + x, b = (size_t)nz * W + nx;
				if (!cur[a].tex || !cur[b].tex || L.fit(cur[a], w, cur[b])) continue;
				if (hand[a] && hand[b]) { edgeBadHand++; continue; }
				edgeBad++;
				{
					std::string ga = cur[a].tex->grp->name, gb = cur[b].tex->grp->name;
					if (gb < ga) std::swap(ga, gb);
					badPairs[ga + " | " + gb]++;
				}
				const int ca = (int)(L.corners.count(cur[a].tex) ? 1 : 0);
				(void)ca;
				unmatched.push_back({ x, z, "edge", std::string(SIDE_NAMES[w]) + " side: " + cur[a].tex->grp->name + " " + std::to_string(cur[a].tex->id) + " next to "
					+ cur[b].tex->grp->name + " " + std::to_string(cur[b].tex->id) + ": no EDGE rule allows them in these turns" + (hand[a] || hand[b] ? " (beside a hand tile)" : "") });
			}
	const auto tSolve = clock::now();
	MapFolder::ReportProgress("the ground: the report", 0.88f);

	// ---- the report ----
	json mr = json::object();
	mr["solverVersion"] = SOLVER_VERSION;
	mr["preset"] = presetName.empty() ? json() : json(presetName);
	mr["dataFile"] = data.value("source", std::string());
	mr["default"] = mats[defaultMat].name;
	mr["rules"] = rules.size();
	{
		json hits = json::array();
		for (size_t i = 0; i < rules.size(); i++)
			hits.push_back({ { "rule", rules[i].index }, { "material", mats[rules[i].material].name }, { "vertices", ruleHits[i] } });
		mr["ruleHits"] = hits;
	}
	// per material: its vertices, and the tiles it is the commonest corner of, with their passability
	std::vector<int> vCount(mats.size(), 0), tCount(mats.size(), 0), tBlocked(mats.size(), 0), tSlow(mats.size(), 0);
	std::vector<double> slowLoss(mats.size(), 0);
	for (int z = E; z <= H - E; z++)
		for (int x = E; x <= W - E; x++) vCount[vm[(size_t)z * gw + x]]++;
	const json& gdata = data["groups"];
	int playable = 0, blocked = 0, slow = 0, water = 0, blockedWalkable = 0;
	std::vector<int> tBlockedWalkable(mats.size(), 0);
	for (int z = E; z < H - E; z++)
		for (int x = E; x < W - E; x++) {
			const size_t ti = (size_t)z * W + x;
			playable++;
			const int vi[4] = { z * gw + x, z * gw + x + 1, (z + 1) * gw + x + 1, (z + 1) * gw + x };
			int best = vm[vi[0]], bn = 0;
			for (int k = 0; k < 4; k++) {
				int n = 0;
				for (int q = 0; q < 4; q++) n += vm[vi[q]] == vm[vi[k]];
				if (n > bn) { bn = n; best = vm[vi[k]]; }
			}
			const TerrainTile& tile = t.tiles[ti];
			if (tile.fullOfWater) { water++; continue; }
			tCount[best]++;
			const json gj = tile.texture && tile.texture->grp ? gdata.value(tile.texture->grp->name, json::object()) : json::object();
			if (gj.contains("landPassable") && !gj["landPassable"].get<bool>()) {
				blocked++;
				tBlocked[best]++;
				// ground a 45-degree unit could stand on, that the material closes
				const float spread = std::max({ h[vi[0]], h[vi[1]], h[vi[2]], h[vi[3]] }) - std::min({ h[vi[0]], h[vi[1]], h[vi[2]], h[vi[3]] });
				if (spread <= TILE) { blockedWalkable++; tBlockedWalkable[best]++; }
			}
			else if (Num(gj, "cost", 1) < 0.999f) { slow++; tSlow[best]++; slowLoss[best] += 1 - Num(gj, "cost", 1); }
		}
	const int pv = (W - 2 * E + 1) * (H - 2 * E + 1);
	json shares = json::array();
	std::vector<int> order(mats.size());
	for (size_t i = 0; i < mats.size(); i++) order[i] = (int)i;
	std::sort(order.begin(), order.end(), [&](int a, int b) { return tCount[a] > tCount[b]; });
	for (int i : order) {
		if (!tCount[i]) continue;
		json g = json::array();
		for (auto& [gi, wgt] : mats[i].groups) g.push_back(L.groups[gi]);
		json pj = { { "blockedTiles", tBlocked[i] }, { "blockedOnWalkableGround", tBlockedWalkable[i] }, { "slowTiles", tSlow[i] } };
		if (tSlow[i]) pj["meanSpeed"] = std::round((1 - slowLoss[i] / tSlow[i]) * 1000) / 1000;
		pj["note"] = tBlocked[i] ? "blocks land units on " + std::to_string(tBlocked[i]) + " tiles (" + std::to_string(tBlockedWalkable[i]) + " of them walkable by slope)" : tSlow[i] ? "slows land units" : "open";
		shares.push_back({ { "material", mats[i].name }, { "share", std::round(1000.0 * tCount[i] / std::max(playable - water, 1)) / 1000 },
			{ "tiles", tCount[i] }, { "vertexShare", std::round(1000.0 * vCount[i] / std::max(pv, 1)) / 1000 }, { "groups", g }, { "passability", pj } });
	}
	mr["materials"] = shares;
	mr["sharesAbout"] = "share: of the playable land's tiles (water tiles left out), each tile counted for its corners' commonest material";
	json gt = json::object();
	for (auto& [g, n] : groupTiles) gt[g] = n;
	mr["groupTiles"] = gt;
	mr["passability"] = { { "playableTiles", playable }, { "waterTiles", water }, { "blockedTiles", blocked },
		{ "blockedShare", std::round(1000.0 * blocked / std::max(playable, 1)) / 1000 }, { "blockedOnWalkableGround", blockedWalkable }, { "slowTiles", slow },
		{ "slowShare", std::round(1000.0 * slow / std::max(playable, 1)) / 1000 },
		{ "about", "blocked: the tile's terrain lets no land unit cross (cliffs, mountains, deep river beds); blockedOnWalkableGround: of those, tiles a 45-degree unit could stand on by their slope (what the material closes); slow: its speed is below 1 (rock 0.5, snow 0.75 ...); from the data file's groups (the game set's terrains); water tiles left out" } };
	json um = json::array();
	for (size_t i = 0; i < unmatched.size() && i < 2000; i++) {
		const Unmatched& u = unmatched[i];
		um.push_back({ { "tile", { u.x, u.z } }, { "atM", { (u.x - E) * TILE + 2.5f, (u.z - E) * TILE + 2.5f } }, { "playable", u.x >= E && u.z >= E && u.x < W - E && u.z < H - E },
			{ "kind", u.kind }, { "reason", u.detail } });
	}
	int unmatchedCorners = 0;
	for (const Unmatched& u : unmatched) unmatchedCorners += u.kind == "corners";
	json bp = json::array();
	{
		std::vector<std::pair<int, std::string>> v;
		for (auto& [k, n] : badPairs) v.push_back({ -n, k });
		std::sort(v.begin(), v.end());
		for (size_t i = 0; i < v.size() && i < 20; i++) bp.push_back({ { "groups", v[i].second }, { "edges", -v[i].first } });
	}
	mr["unmatched"] = { { "count", unmatched.size() }, { "edgesByGroups", bp }, { "corners", unmatchedCorners }, { "edges", edgeBad }, { "betweenHandTiles", edgeBadHand },
		{ "handCornerConflicts", handFixedConflicts }, { "list", um },
		{ "about", "tile [x, z] counts from the whole map's south-west corner, edge band included; atM is the tile's middle in playable metres. corners: no picture joins the tile's corners' groups (a fallback was used); edges: two neighbours' pictures have no EDGE rule for their turns. Edges between two hand tiles are counted apart, not listed" } };
	{
		std::vector<float> m;
		for (int z = E; z <= H - E; z++)
			for (int x = E; x <= W - E; x++)
				if (!wet[(size_t)z * gw + x]) m.push_back(moist[(size_t)z * gw + x]);
		std::sort(m.begin(), m.end());
		json q = json::object();
		for (int p : { 10, 25, 50, 75, 90 })
			q["p" + std::to_string(p)] = m.empty() ? 0.0 : std::round(m[std::min(m.size() - 1, m.size() * p / 100)] * 1000) / 1000.0;
		mr["moistureQuantiles"] = q;
		mr["moistureAbout"] = "the moisture rules read, over the playable land (water left out): set moisture limits against these";
	}
	mr["solver"] = { { "groups", G }, { "transitions", L.fits.size() }, { "edgeRules", L.ruleCount }, { "picturesWithCorners", L.corners.size() },
		{ "legalisePasses", legalPasses }, { "localRepairs", localFixed }, { "localBlocksTried", localTried }, { "verticesAbsorbed", absorbed }, { "verticesOpened", opened }, { "verticesChanged", changedTotal }, { "patches", patched }, { "patchRounds", patchRounds }, { "badBeforePatches", badBeforePatches }, { "badLastRound", badAfterPatches }, { "repairPasses", repairPasses }, { "rivers", riverCount },
		{ "moisture", moistFromLayer ? "layers/moisture.png" : "made: distance to water and rivers, drainage, hollows" } };
	mr["timeMs"] = { { "library", std::round(std::chrono::duration<double, std::milli>(tLib - t0).count()) },
		{ "land", std::round(std::chrono::duration<double, std::milli>(tLand - tLib).count()) },
		{ "corners", std::round(std::chrono::duration<double, std::milli>(tLegal - tLand).count()) },
		{ "tiles", std::round(std::chrono::duration<double, std::milli>(tSolve - tLegal).count()) }, { "total", std::round(ms(t0)) } };
	json legend = json::array();
	for (int i : order)
		if (tCount[i]) legend.push_back({ { "material", mats[i].name }, { "colour", { (int)mats[i].colour[0], (int)mats[i].colour[1], (int)mats[i].colour[2] } } });
	mr["picture"] = { { "file", "build/materials.png" }, { "moisture", "build/moisture.png" }, { "legend", legend },
		{ "about", "per tile the mean of its corners' materials' map colours (the data's mapColour, else the mean of the groups' pictures), lit from the north-west; water blue; unmatched tiles red; the edge band darker; north up" } };
	if (unmatched.size())
		warnings.push_back("ground: " + std::to_string(unmatched.size()) + " unmatched tile(s) (" + std::to_string(unmatchedCorners) + " corners, " + std::to_string(edgeBad)
			+ " edges); report.json materials.unmatched has their places");
	report["materials"] = mr;

	// ---- the pictures: the materials top-down, the moisture ----
	{
		std::vector<uint8_t> rgb((size_t)W * H * 3);
		std::set<std::pair<int, int>> bad;
		for (const Unmatched& u : unmatched) bad.insert({ u.x, u.z });
		for (int z = 0; z < H; z++)
			for (int x = 0; x < W; x++) {
				const int vi[4] = { z * gw + x, z * gw + x + 1, (z + 1) * gw + x + 1, (z + 1) * gw + x };
				const TerrainTile& tile = t.tiles[(size_t)z * W + x];
				// the tile's own picture's corners when known (a transition shows both), else the rule's
				std::array<float, 3> c = { 0, 0, 0 };
				for (int k = 0; k < 4; k++)
					for (int q = 0; q < 3; q++) c[q] += mats[vm[vi[k]]].colour[q] / 4;
				const float nx = (h[vi[0]] + h[vi[3]] - h[vi[1]] - h[vi[2]]) / (2 * TILE), nz = (h[vi[0]] + h[vi[1]] - h[vi[2]] - h[vi[3]]) / (2 * TILE);
				const float light = std::clamp(0.85f + 0.9f * (nx * 0.7f - nz * 0.7f) * -1.0f, 0.45f, 1.25f);
				const bool edgeBand = x < E || x >= W - E || z < E || z >= H - E;
				uint8_t* p = &rgb[((size_t)(H - 1 - z) * W + x) * 3];
				if (tile.fullOfWater) { p[0] = 40; p[1] = 90; p[2] = 160; }
				else
					for (int q = 0; q < 3; q++) p[q] = (uint8_t)std::clamp((int)(c[q] * light * (edgeBand ? 0.55f : 1.0f)), 0, 255);
				if (bad.count({ x, z })) { p[0] = 255; p[1] = 0; p[2] = 0; }
			}
		WritePngRgb(folder / "build" / "materials.png", W, H, rgb);
		std::vector<uint8_t> g(NV);
		for (int z = 0; z < gh; z++)
			for (int x = 0; x < gw; x++) g[(size_t)(gh - 1 - z) * gw + x] = (uint8_t)std::clamp((int)(moist[(size_t)z * gw + x] * 255), 0, 255);
		WritePngRgb(folder / "build" / "moisture.png", gw, gh, g, 1);
	}
	return true;
}

json EdgeFitOf(const Terrain& t, const std::string& dbFile)
{
	const EdgeRules er = ReadEdgeRules(dbFile);
	const Library L = BuildLibrary(t.texDb, er);
	const int W = t.width, H = t.height;
	auto placed = [&](int x, int z) {
		const TerrainTile& tile = t.tiles[(size_t)z * W + x];
		return Placed{ tile.texture, (uint8_t)OR().ofCode[(tile.rot & 3) | (tile.xflip & 1) << 2 | (tile.zflip & 1) << 3] };
	};
	long pairs = 0, fit = 0, known = 0, cornerPairs = 0, cornerAgree = 0;
	for (int z = 0; z < H; z++)
		for (int x = 0; x < W; x++) {
			const Placed a = placed(x, z);
			known += L.corners.count(a.tex) != 0;
			for (int w : { 0, 1 }) {
				const int nx = x + DX[w], nz = z + DZ[w];
				if (nx >= W || nz >= H) continue;
				const Placed b = placed(nx, nz);
				pairs++;
				fit += L.fit(a, w, b);
				// the shared corners' groups agree
				const std::array<int, 4> ca = L.worldCorners(a), cb = L.worldCorners(b);
				const int S[2][2][2] = { { { 1, 0 }, { 2, 3 } }, { { 2, 1 }, { 3, 0 } } };
				for (int k = 0; k < 2; k++) {
					const int ga = ca[S[w][k][0]], gb = cb[S[w][k][1]];
					if (ga < 0 || gb < 0) continue;
					cornerPairs++;
					cornerAgree += ga == gb;
				}
			}
		}
	return { { "pairs", pairs }, { "fitShare", pairs ? (double)fit / pairs : 0.0 }, { "tilesWithCorners", W * H ? (double)known / ((double)W * H) : 0.0 },
		{ "cornerAgreeShare", cornerPairs ? (double)cornerAgree / cornerPairs : 0.0 }, { "edgeRules", L.ruleCount }, { "groups", L.groups.size() } };
}

int RunCommandLine(const std::vector<std::string>& args)
{
	LoadBCP("data.bcp");
	auto opt = [&](const char* k) -> std::string {
		for (size_t i = 1; i + 1 < args.size(); i++)
			if (args[i] == k) return args[i + 1];
		return "";
	};
	if (args[0] == "material-presets") {
		std::vector<std::string> w;
		const std::string db = opt("--database").empty() ? "Maps\\Map_Textures\\All_Textures.dat" : opt("--database");
		json d = LoadMaterials(db, json::object(), w);
		json out = { { "database", db }, { "source", d.value("source", std::string()) }, { "materials", d["materials"] }, { "presets", d["presets"] },
			{ "conditions", { "height", "heightRel", "aboveSea", "slope", "curvature", "moisture", "drainage", "river", "seaDistance", "noise", "mask", "water" } } };
		const std::string text = out.dump(1);
		if (!opt("--out").empty()) WriteAll(PathFromUtf8(opt("--out")), text + "\n");
		else printf("AUTHOR-JSON-BEGIN\n%s\nAUTHOR-JSON-END\n", text.c_str());
		printf("AUTHOR ok, %zu materials, %zu presets\n", d["materials"].size(), d["presets"].size());
		return 0;
	}
	if (args.size() < 2) {
		printf("usage: wkbai --author materials <folder.wkbai> [--out <file.json>] [--png <file.png>]\n       wkbai --author material-presets [--database <file>] [--out <file.json>]\n");
		return 2;
	}
	const fs::path folder = MapFolder::Resolve(args[1]);
	if (folder.empty()) {
		printf("AUTHOR no map folder %s\n", args[1].c_str());
		return 1;
	}
	const MapFolder::Result r = MapFolder::Compile(folder, true);
	for (const std::string& w : r.warnings) printf("AUTHOR warning: %s\n", w.c_str());
	if (!r.ok) {
		printf("AUTHOR failed: %s\n", r.error.c_str());
		return 1;
	}
	std::string text;
	ReadAll(folder / "build" / "report.json", text);
	json rep = json::parse(text, nullptr, false);
	json out = rep.is_object() && rep.contains("materials") ? rep["materials"] : json{ { "materials", "none: map.json has no ground rules or preset" } };
	if (!opt("--png").empty()) {
		std::error_code ec;
		fs::copy_file(folder / "build" / "materials.png", PathFromUtf8(opt("--png")), fs::copy_options::overwrite_existing, ec);
	}
	if (!opt("--out").empty()) WriteAll(PathFromUtf8(opt("--out")), out.dump(1) + "\n");
	else printf("AUTHOR-JSON-BEGIN\n%s\nAUTHOR-JSON-END\n", out.dump(1).c_str());
	const json& u = out.value("unmatched", json::object());
	printf("AUTHOR ok, materials: %d unmatched (%d corners, %d edges), report %s\n", u.value("count", 0), u.value("corners", 0), u.value("edges", 0), r.reportPath.c_str());
	fflush(stdout);
	return 0;
}

} // namespace MapMaterials
