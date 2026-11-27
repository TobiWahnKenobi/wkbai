// wkbai - live map authoring over the agent-control socket (step 17.8 of the RE repo's
// docs/engine/map-authoring.md, section 3 "The live ops"): an agent changes the map that is open
// (in the map editor, or a single-player game with cheats, or a test run) and sees its edit at once,
// without compiling the folder again. Each change is one undo step of the editor's, made on the
// client's terrain and the server's as the editor's own tools do.
//
// Places are in playable metres (x east, z north from the south-west corner), as everywhere in
// map authoring. A region (heights, paint) is one of:
//   "circle": { "at": [x, z], "radius": r }      "rect": [x0, z0, x1, z1]
//   "polygon": [[x, z], ...]                     "line": [[x, z], ...] with "width"
// with "falloff" (metres of soft edge inside it; heights only).
//
// Ops (the reply: "ok", what changed, and on failure "error"):
//   author.heights { region, "mode": raise | lower | set | flatten | smooth | ramp | terrace | noise,
//                    "amount" (m; raise, lower, noise), "to" (m; set, flatten: else the region's
//                    mean), "iterations" (smooth), "from"/"to" (ramp: the line's end heights, else the
//                    ground's there), "step" (m), "strength" 0-1 (terrace), "scale" (m; noise), "seed" }
//   author.paint   { region, "group": "GRASS1", "seed" }: the group's pictures and the transitions
//                    round them (MapMaterials::PaintRegion)
//   author.water   { "add": [{ "at": [x, z], "level": m | "depth": m above the ground }], "remove":
//                    [index, ...], "removeAt": [[x, z], ...] (the lake flooding that place) }
//   author.place   { "blueprint", "class" (building, character, ...; else the first that has it),
//                    "player" (id; else the first playing player), "at": [x, z], "yaw" (degrees),
//                    "count", "spacing" }
//   author.remove  { "ids": [...] } or { "at": [x, z], "radius", "class" }
//   author.move    { "object" (its id: a request's "id" is the protocol's), "to": [x, z], "yaw" }
//   author.check   { "unit", "fairnessRadiusM", "edges" }: MapChecks::Run on the running game
//   author.save    { "name" }: the editor's Save into saved\Maps\<name>.wkbai
//   author.undo, author.redo
//   author.view    { "at": [x, z] (looked at), "from": [x, z] | "distance" + "yaw" (degrees),
//                    "above" (m over the highest ground within 150 m of the eye) } or
//                  { "camera": [x, y, z, pitch, yaw] }; { "release": true } lets go
//   author.state   the map's size, the players, the undo steps, the camera, "loaded" (the loading screen gone)
// Licensed under the GNU General Public License 3

#include "ClientInterface.h"
#include "MapEditorState.h"
#include "../MapChecks.h"
#include "../MapMaterials.h"
#include "../MapFolder.h"
#include "../file.h"
#include "../settings.h"
#include "../NNSearch.h"
#include "../client.h"
#include "../server.h"
#include "../terrain.h"
#include "../tags.h"
#include "../gameset/gameset.h"
#include "../gameset/GameObjBlueprint.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace {
	using json = nlohmann::json;
	constexpr float TILE = 5.0f;

	// a region of the map: how far inside it a point is (metres; negative: outside)
	struct Region {
		enum Kind { NONE, CIRCLE, RECT, POLYGON, LINE } kind = NONE;
		float cx = 0, cz = 0, r = 0;
		float x0 = 0, z0 = 0, x1 = 0, z1 = 0;
		std::vector<std::pair<float, float>> pts;
		float width = 10;
		float falloff = 0;
		bool read(const json& req, std::string& error)
		{
			auto pt = [](const json& p, float& x, float& z) {
				if (!p.is_array() || p.size() != 2 || !p[0].is_number() || !p[1].is_number()) return false;
				x = p[0].get<float>(), z = p[1].get<float>();
				return true;
			};
			if (req.contains("circle")) {
				const json& c = req["circle"];
				if (!c.is_object() || !pt(c.value("at", json()), cx, cz) || !c.value("radius", json()).is_number()) { error = "circle: { \"at\": [x, z], \"radius\": r }"; return false; }
				r = c["radius"].get<float>();
				kind = CIRCLE;
			}
			else if (req.contains("rect")) {
				const json& c = req["rect"];
				if (!c.is_array() || c.size() != 4) { error = "rect: [x0, z0, x1, z1]"; return false; }
				x0 = std::min(c[0].get<float>(), c[2].get<float>()), x1 = std::max(c[0].get<float>(), c[2].get<float>());
				z0 = std::min(c[1].get<float>(), c[3].get<float>()), z1 = std::max(c[1].get<float>(), c[3].get<float>());
				kind = RECT;
			}
			else if (req.contains("polygon") || req.contains("line")) {
				const bool line = req.contains("line");
				const json& c = req[line ? "line" : "polygon"];
				if (!c.is_array() || c.size() < (line ? 2u : 3u)) { error = line ? "line: [[x, z], [x, z], ...]" : "polygon: three points or more"; return false; }
				for (const json& p : c) {
					float x, z;
					if (!pt(p, x, z)) { error = "a point is [x, z]"; return false; }
					pts.push_back({ x, z });
				}
				width = req.value("width", 10.0f);
				kind = line ? LINE : POLYGON;
			}
			else { error = "a region: circle, rect, polygon or line"; return false; }
			falloff = req.value("falloff", -1.0f);
			if (falloff < 0)
				falloff = kind == CIRCLE ? r * 0.35f : kind == LINE ? width * 0.4f : 5.0f;
			return true;
		}
		static float segDist(float px, float pz, float ax, float az, float bx, float bz, float* tOut = nullptr)
		{
			const float dx = bx - ax, dz = bz - az, l2 = dx * dx + dz * dz;
			const float t = l2 > 0 ? std::clamp(((px - ax) * dx + (pz - az) * dz) / l2, 0.0f, 1.0f) : 0.0f;
			if (tOut) *tOut = t;
			return std::hypot(px - ax - t * dx, pz - az - t * dz);
		}
		float inside(float x, float z) const
		{
			switch (kind) {
			case CIRCLE: return r - std::hypot(x - cx, z - cz);
			case RECT: return std::min({ x - x0, x1 - x, z - z0, z1 - z });
			case LINE: {
				float d = 1e30f;
				for (size_t i = 1; i < pts.size(); i++) d = std::min(d, segDist(x, z, pts[i - 1].first, pts[i - 1].second, pts[i].first, pts[i].second));
				return width / 2 - d;
			}
			case POLYGON: {
				bool in = false;
				float d = 1e30f;
				for (size_t i = 0, j = pts.size() - 1; i < pts.size(); j = i++) {
					const auto& a = pts[i]; const auto& b = pts[j];
					if ((a.second > z) != (b.second > z) && x < (b.first - a.first) * (z - a.second) / (b.second - a.second) + a.first) in = !in;
					d = std::min(d, segDist(x, z, a.first, a.second, b.first, b.second));
				}
				return in ? d : -d;
			}
			default: return -1;
			}
		}
		// 0 outside, 1 deeper inside than the falloff, smooth between
		float weight(float x, float z) const
		{
			const float d = inside(x, z);
			if (d <= 0) return 0;
			if (falloff <= 0 || d >= falloff) return 1;
			const float t = d / falloff;
			return t * t * (3 - 2 * t);
		}
		void bounds(float& bx0, float& bz0, float& bx1, float& bz1) const
		{
			switch (kind) {
			case CIRCLE: bx0 = cx - r, bz0 = cz - r, bx1 = cx + r, bz1 = cz + r; break;
			case RECT: bx0 = x0, bz0 = z0, bx1 = x1, bz1 = z1; break;
			default: {
				bx0 = bz0 = 1e30f, bx1 = bz1 = -1e30f;
				for (auto& [x, z] : pts) bx0 = std::min(bx0, x), bz0 = std::min(bz0, z), bx1 = std::max(bx1, x), bz1 = std::max(bz1, z);
				const float m = kind == LINE ? width / 2 : 0;
				bx0 -= m, bz0 -= m, bx1 += m, bz1 += m;
			}
			}
		}
	};

	float Noise(uint32_t seed, float x, float z)
	{
		auto hash = [seed](int a, int b) {
			uint32_t h = seed * 374761393u + (uint32_t)a * 668265263u + (uint32_t)b * 2147483647u;
			h = (h ^ (h >> 13)) * 1274126177u;
			return ((h ^ (h >> 16)) & 0xffffff) / (float)0xffffff * 2 - 1;
		};
		const int ix = (int)std::floor(x), iz = (int)std::floor(z);
		float fx = x - ix, fz = z - iz;
		fx = fx * fx * (3 - 2 * fx), fz = fz * fz * (3 - 2 * fz);
		const float a = hash(ix, iz), b = hash(ix + 1, iz), c = hash(ix, iz + 1), d = hash(ix + 1, iz + 1);
		return (a * (1 - fx) + b * fx) * (1 - fz) + (c * (1 - fx) + d * fx) * fz;
	}

	ServerGameObject* Live(Server& server, uint32_t id)
	{
		ServerGameObject* o = server.findObject(id);
		return o && !o->deleted ? o : nullptr;
	}
	std::string DbFile(const Terrain* t)
	{
		std::string db;
		for (wchar_t c : t->texDbPath) if (c) db += (char)c;
		return db;
	}
	const char* CLASS_NAMES[] = { "army", "building", "character", "city", "container", "formation", "level", "marker", "missile", "player", "prop", "terrain zone", "town", "user" };
}

nlohmann::json ClientInterface::authorOp(const nlohmann::json& req)
{
	const std::string op = req.value("op", std::string());
	json out = { { "ok", true }, { "op", op } };
	auto fail = [&](const std::string& why) { return json{ { "ok", false }, { "op", op }, { "error", why } }; };
	Terrain* trn = client->terrain;
	if (!trn || !client->getLevel())
		return fail("no map loaded yet");
	if (!editor)
		editor = std::make_shared<EditorState>();
	EditorState& ed = *editor;
	const int E = trn->edge;
	const auto [pw, ph] = trn->getNumPlayableTiles();
	auto ptOf = [](const json& p, float& x, float& z) {
		if (!p.is_array() || p.size() != 2 || !p[0].is_number() || !p[1].is_number()) return false;
		x = p[0].get<float>(), z = p[1].get<float>();
		return true;
	};
	// the objects in an area stand on the ground as it is now
	auto resettle = [&](float x0, float z0, float x1, float z1) {
		withServer([&](Server& server) {
			NNSearch search;
			const Vector3 centre((x0 + x1) / 2, 0, (z0 + z1) / 2);
			search.start(&server, centre, std::max(x1 - x0, z1 - z0) * 0.75f + 10.0f);
			while (ServerGameObject* obj = (ServerGameObject*)search.next())
				obj->setPosition(Vector3(obj->position.x, server.terrain->getHeight(obj->position.x, obj->position.z), obj->position.z));
		});
	};

	if (op == "author.heights") {
		Region rg;
		std::string error;
		if (!rg.read(req, error)) return fail(error);
		const std::string mode = req.value("mode", std::string("raise"));
		static const char* modes[] = { "raise", "lower", "set", "flatten", "smooth", "ramp", "terrace", "noise" };
		if (std::find_if(std::begin(modes), std::end(modes), [&](const char* m) { return mode == m; }) == std::end(modes))
			return fail("mode: raise, lower, set, flatten, smooth, ramp, terrace or noise");
		if (mode == "ramp" && rg.kind != Region::LINE)
			return fail("a ramp needs a line: [[x, z], [x, z]] and a width");
		float bx0, bz0, bx1, bz1;
		rg.bounds(bx0, bz0, bx1, bz1);
		const int stride = trn->width + 1;
		const int vx0 = std::max(1, (int)std::floor(bx0 / TILE) + E), vx1 = std::min(trn->width - 1, (int)std::ceil(bx1 / TILE) + E);
		const int vz0 = std::max(1, (int)std::floor(bz0 / TILE) + E), vz1 = std::min(trn->height - 1, (int)std::ceil(bz1 / TILE) + E);
		if (vx0 > vx1 || vz0 > vz1) return fail("the region is off the map");
		const float maxM = (255.0f + 255.0f / 256.0f) * trn->scale;
		auto vpos = [&](int vx, int vz, float& x, float& z) { x = (vx - E) * TILE, z = (vz - E) * TILE; };
		// the vertices and their weights
		struct V { int i; float w, x, z; };
		std::vector<V> vs;
		double meanSum = 0, meanW = 0;
		for (int vz = vz0; vz <= vz1; vz++)
			for (int vx = vx0; vx <= vx1; vx++) {
				float x, z;
				vpos(vx, vz, x, z);
				const float w = rg.weight(x, z);
				if (w <= 0) continue;
				const int i = vz * stride + vx;
				vs.push_back({ i, w, x, z });
				meanSum += trn->heightSteps(i) * trn->scale * w;
				meanW += w;
			}
		if (vs.empty()) return fail("no vertex inside the region");
		EditorState::Undo u;
		u.heights.assign(trn->vertices.begin(), trn->vertices.end());
		u.fractions = trn->heightFraction;
		std::vector<float> h(trn->vertices.size());
		for (size_t i = 0; i < h.size(); i++) h[i] = trn->heightSteps((int)i) * trn->scale;
		std::vector<float> nh = h;
		const float amount = req.value("amount", 5.0f);
		float to = req.value("to", (float)(meanSum / std::max(meanW, 1e-6)));
		const uint32_t seed = req.value("seed", 1u);
		if (mode == "smooth") {
			const int it = std::clamp(req.value("iterations", 4), 1, 50);
			for (int k = 0; k < it; k++) {
				std::vector<float> cur = nh;
				for (const V& v : vs) {
					const float avg = (cur[v.i - 1] + cur[v.i + 1] + cur[v.i - stride] + cur[v.i + stride]) / 4;
					nh[v.i] = cur[v.i] + (avg - cur[v.i]) * 0.6f * v.w;
				}
			}
		}
		else if (mode == "ramp") {
			// the line's heights from its first point to its last, along its length
			auto ground = [&](float x, float z) { return trn->getHeight(std::clamp(x, 0.0f, pw * TILE - 0.1f), std::clamp(z, 0.0f, ph * TILE - 0.1f)); };
			const float from = req.contains("from") ? req["from"].get<float>() : ground(rg.pts.front().first, rg.pts.front().second);
			const float end = req.contains("to") ? req["to"].get<float>() : ground(rg.pts.back().first, rg.pts.back().second);
			std::vector<float> acc(rg.pts.size(), 0);
			for (size_t k = 1; k < rg.pts.size(); k++)
				acc[k] = acc[k - 1] + std::hypot(rg.pts[k].first - rg.pts[k - 1].first, rg.pts[k].second - rg.pts[k - 1].second);
			const float total = std::max(acc.back(), 1.0f);
			for (const V& v : vs) {
				float best = 1e30f, along = 0;
				for (size_t k = 1; k < rg.pts.size(); k++) {
					float t;
					const float d = Region::segDist(v.x, v.z, rg.pts[k - 1].first, rg.pts[k - 1].second, rg.pts[k].first, rg.pts[k].second, &t);
					if (d < best) { best = d; along = acc[k - 1] + t * (acc[k] - acc[k - 1]); }
				}
				const float target = from + (end - from) * along / total;
				nh[v.i] = h[v.i] + (target - h[v.i]) * v.w;
			}
		}
		else
			for (const V& v : vs) {
				float& n = nh[v.i];
				if (mode == "raise") n = h[v.i] + amount * v.w;
				else if (mode == "lower") n = h[v.i] - amount * v.w;
				else if (mode == "set" || mode == "flatten") n = h[v.i] + (to - h[v.i]) * v.w;
				else if (mode == "terrace") {
					const float step = std::max(0.5f, req.value("step", 4.0f)), strength = std::clamp(req.value("strength", 0.7f), 0.0f, 1.0f);
					n = h[v.i] + (std::round(h[v.i] / step) * step - h[v.i]) * strength * v.w;
				}
				else if (mode == "noise") {
					const float scale = std::max(5.0f, req.value("scale", 40.0f));
					n = h[v.i] + amount * v.w * (Noise(seed, v.x / scale, v.z / scale) * 0.7f + Noise(seed + 7, v.x / scale * 2.1f, v.z / scale * 2.1f) * 0.3f);
				}
			}
		float lo = 1e30f, hi = -1e30f, most = 0;
		int changed = 0;
		for (const V& v : vs) {
			nh[v.i] = std::clamp(nh[v.i], 0.0f, maxM);
			if (std::abs(nh[v.i] - h[v.i]) < 1e-3f) continue;
			changed++;
			most = std::max(most, std::abs(nh[v.i] - h[v.i]));
			lo = std::min(lo, nh[v.i]), hi = std::max(hi, nh[v.i]);
			trn->setHeightSteps(v.i, nh[v.i] / trn->scale);
		}
		withServer([&](Server& server) {
			if (server.terrain && server.terrain != trn && server.terrain->vertices.size() == trn->vertices.size())
				for (const V& v : vs)
					if (std::abs(nh[v.i] - h[v.i]) >= 1e-3f)
						server.terrain->setHeightSteps(v.i, nh[v.i] / server.terrain->scale);
			if (server.terrain) server.terrain->revision++;
		});
		if (changed) {
			ed.pushUndo(std::move(u));
			rebuildTerrainRenderer(false);
			if (!trn->lakes.empty())
				setLakes(trn->lakes, trn->lakeExtraBits);
			resettle(bx0, bz0, bx1, bz1);
		}
		out["mode"] = mode;
		out["vertices"] = changed;
		out["largestChangeM"] = std::round(most * 100) / 100;
		if (changed) out["rangeM"] = { std::round(lo * 10) / 10, std::round(hi * 10) / 10 };
		if (mode == "set" || mode == "flatten") out["toM"] = std::round(to * 100) / 100;
		return out;
	}

	if (op == "author.paint") {
		Region rg;
		std::string error;
		json r2 = req;
		if (!r2.contains("falloff")) r2["falloff"] = 0;
		if (!rg.read(r2, error)) return fail(error);
		const std::string group = req.value("group", std::string());
		if (group.empty()) return fail("paint needs a group (a base texture group: GRASS1, SAND1, ...; the catalog's textureDatabases)");
		const int W = trn->width, H = trn->height;
		std::vector<uint8_t> mark((size_t)W * H, 0);
		int marked = 0;
		for (int z = 0; z < ph; z++)
			for (int x = 0; x < pw; x++)
				if (rg.inside(x * TILE + 2.5f, z * TILE + 2.5f) > 0) { mark[(size_t)(z + E) * W + x + E] = 1; marked++; }
		if (!marked) return fail("no tile's middle inside the region");
		EditorState::Undo u;
		for (int i = 0; i < W * H; i++)
			u.tiles.push_back({ trn->tiles[i].texture, (uint8_t)trn->tiles[i].rot, (uint8_t)trn->tiles[i].xflip, (uint8_t)trn->tiles[i].zflip });
		MapMaterials::json rep;
		if (!MapMaterials::PaintRegion(*trn, DbFile(trn), group, mark, req.value("seed", 1u), rep, error)) return fail(error);
		// the server's terrain: the same pictures by group and id
		withServer([&](Server& server) {
			Terrain* st = server.terrain;
			if (st && st != trn && st->width == W && st->height == H)
				for (int i = 0; i < W * H; i++) {
					const TerrainTexture* tex = trn->tiles[i].texture;
					if (tex == u.tiles[i].texture && trn->tiles[i].rot == u.tiles[i].rot && trn->tiles[i].xflip == u.tiles[i].xflip && trn->tiles[i].zflip == u.tiles[i].zflip)
						continue;
					if (tex && tex->grp) {
						auto g = st->texDb.groups.find(tex->grp->name);
						if (g != st->texDb.groups.end() && g->second) {
							auto tt = g->second->textures.find(tex->id);
							if (tt != g->second->textures.end()) st->tiles[i].texture = tt->second.get();
						}
					}
					st->tiles[i].rot = trn->tiles[i].rot;
					st->tiles[i].xflip = trn->tiles[i].xflip;
					st->tiles[i].zflip = trn->tiles[i].zflip;
				}
			if (st && server.tiles) {
				for (int i = 0; i < pw * ph; i++) server.tiles[i].terrainType = -2;
				st->revision++;
			}
		});
		ed.pushUndo(std::move(u));
		rebuildTerrainRenderer(true);
		out["paint"] = json::parse(rep.dump());
		return out;
	}

	if (op == "author.water") {
		std::vector<Vector3> lakes = trn->lakes;
		std::vector<uint8_t> bits = trn->lakeExtraBits;
		bits.resize(lakes.size(), 0);
		json added = json::array(), removed = json::array();
		std::vector<int> drop;
		for (const json& r : req.value("remove", json::array()))
			if (r.is_number_integer() && r.get<int>() >= 0 && r.get<int>() < (int)lakes.size()) drop.push_back(r.get<int>());
		for (const json& p : req.value("removeAt", json::array())) {
			float x, z;
			if (!ptOf(p, x, z)) return fail("removeAt: [[x, z], ...]");
			const TerrainTile* tile = trn->getPlayableTile(std::clamp((int)(x / TILE), 0, pw - 1), std::clamp((int)(z / TILE), 0, ph - 1));
			if (!tile || !tile->fullOfWater) continue;
			int best = -1;
			const Vector3 at(x + E * TILE, 0, z + E * TILE);
			for (size_t i = 0; i < lakes.size(); i++)
				if (std::abs(lakes[i].y - tile->waterLevel) < 0.01f && (best < 0 || (lakes[i] - at).len2xz() < (lakes[best] - at).len2xz())) best = (int)i;
			if (best >= 0) drop.push_back(best);
		}
		std::sort(drop.begin(), drop.end());
		drop.erase(std::unique(drop.begin(), drop.end()), drop.end());
		for (auto it = drop.rbegin(); it != drop.rend(); ++it) {
			removed.push_back({ { "index", *it }, { "at", { lakes[*it].x - E * TILE, lakes[*it].z - E * TILE } }, { "levelM", lakes[*it].y } });
			lakes.erase(lakes.begin() + *it);
			bits.erase(bits.begin() + *it);
		}
		for (const json& a : req.value("add", json::array())) {
			float x, z;
			if (!ptOf(a.value("at", json()), x, z)) return fail("add: [{ \"at\": [x, z], \"level\": m | \"depth\": m }]");
			if (x < 0 || z < 0 || x >= pw * TILE || z >= ph * TILE) return fail("a lake point is off the playable map");
			const float level = a.contains("level") ? a["level"].get<float>() : trn->getHeight(x, z) + a.value("depth", 3.0f);
			if (lakes.size() >= 1024) return fail("a map has at most 1024 lakes");
			lakes.emplace_back(x + E * TILE, level, z + E * TILE);
			bits.push_back(0);
			added.push_back({ { "index", lakes.size() - 1 }, { "at", { x, z } }, { "levelM", level } });
		}
		if (added.empty() && removed.empty()) return fail("nothing to add or remove (add, remove, removeAt)");
		EditorState::Undo u;
		u.hasLakes = true;
		u.lakes = trn->lakes;
		u.lakeBits = trn->lakeExtraBits;
		ed.pushUndo(std::move(u));
		setLakes(lakes, bits);
		int under = 0;
		for (int z = 0; z < ph; z++)
			for (int x = 0; x < pw; x++) under += trn->getPlayableTile(x, z)->fullOfWater;
		out["added"] = added;
		out["removed"] = removed;
		out["lakes"] = lakes.size();
		out["playableTilesUnderWater"] = under;
		return out;
	}

	if (op == "author.place") {
		const std::string name = req.value("blueprint", std::string());
		const std::string cls = req.value("class", std::string());
		float x, z;
		if (name.empty() || !ptOf(req.value("at", json()), x, z)) return fail("place needs a blueprint and at [x, z]");
		if (x < 0 || z < 0 || x >= pw * TILE || z >= ph * TILE) return fail("at is off the playable map");
		const int count = std::clamp(req.value("count", 1), 1, 500);
		const float spacing = req.value("spacing", 3.0f), yaw = req.value("yaw", 0.0f) * 3.14159265f / 180.0f;
		EditorState::Undo u;
		std::string error;
		json ids = json::array();
		std::string placedClass;
		withServer([&](Server& server) {
			const GameObjBlueprint* bp = nullptr;
			for (int c = 0; c < Tags::GAMEOBJCLASS_COUNT && !bp; c++)
				if (cls.empty() || cls == CLASS_NAMES[c])
					if ((bp = server.gameSet->findBlueprint(c, name))) placedClass = CLASS_NAMES[c];
			if (!bp) { error = "no blueprint " + name + (cls.empty() ? "" : " of class " + cls); return; }
			ServerGameObject* owner = nullptr;
			if (req.contains("player")) owner = Live(server, req["player"].get<uint32_t>());
			else {
				for (const auto& [t, ch] : server.getLevel()->children)
					for (CommonGameObject* c : ch)
						if (!owner && c->blueprint->bpClass == Tags::GAMEOBJCLASS_PLAYER && c->blueprint->name != "Neutral Player" && c->blueprint->name != "Baresark Player")
							owner = (ServerGameObject*)c;
			}
			if (!owner || owner->blueprint->bpClass != Tags::GAMEOBJCLASS_PLAYER) { error = "no such player"; return; }
			const int side = (int)std::ceil(std::sqrt((float)count));
			for (int i = 0; i < count; i++) {
				Vector3 pos(x + (i % side - side / 2) * spacing, 0, z + (i / side - side / 2) * spacing);
				pos.y = server.terrain->getHeight(pos.x, pos.z);
				if (ServerGameObject* obj = server.spawnObject(bp, owner, pos, Vector3(0, yaw, 0))) {
					u.placed.push_back(obj->id);
					ids.push_back(obj->id);
				}
			}
		});
		if (!error.empty()) return fail(error);
		if (u.placed.empty()) return fail("the server made no object");
		ed.pushUndo(std::move(u));
		out["ids"] = ids;
		out["class"] = placedClass;
		return out;
	}

	if (op == "author.remove") {
		EditorState::Undo u;
		std::vector<uint32_t> ids;
		for (const json& i : req.value("ids", json::array()))
			if (i.is_number_unsigned() || i.is_number_integer()) ids.push_back(i.get<uint32_t>());
		float x = 0, z = 0;
		const bool area = ptOf(req.value("at", json()), x, z);
		const float radius = req.value("radius", 5.0f);
		const std::string cls = req.value("class", std::string());
		if (ids.empty() && !area) return fail("remove needs ids or at [x, z] with a radius");
		json gone = json::array();
		withServer([&](Server& server) {
			if (area) {
				NNSearch search;
				search.start(&server, Vector3(x, 0, z), radius);
				while (ServerGameObject* obj = (ServerGameObject*)search.next())
					if (std::hypot(obj->position.x - x, obj->position.z - z) <= radius && (cls.empty() || cls == CLASS_NAMES[obj->blueprint->bpClass]))
						ids.push_back(obj->id);
			}
			for (uint32_t id : ids)
				if (ServerGameObject* obj = Live(server, ed.resolve(id)); obj && obj->parent && obj->blueprint->bpClass != Tags::GAMEOBJCLASS_PLAYER
					&& obj->blueprint->bpClass != Tags::GAMEOBJCLASS_LEVEL) {
					u.removed.push_back({ obj->blueprint, obj->id, obj->parent->id, obj->position, obj->orientation });
					gone.push_back({ { "id", obj->id }, { "blueprint", obj->blueprint->name } });
					server.deleteObject(obj);
				}
		});
		if (u.removed.empty()) return fail("nothing removed");
		ed.pushUndo(std::move(u));
		out["removed"] = gone;
		return out;
	}

	if (op == "author.move") {
		float x, z;
		// ("object": a request's "id" is the protocol's own)
		const uint32_t id = req.value("object", 0u);
		if (!id || !ptOf(req.value("to", json()), x, z)) return fail("move needs an object (its id) and to [x, z]");
		EditorState::Undo u;
		bool found = false;
		withServer([&](Server& server) {
			if (ServerGameObject* obj = Live(server, ed.resolve(id))) {
				found = true;
				u.moved.push_back({ obj->id, obj->position, obj->orientation });
				obj->setPosition(Vector3(x, server.terrain->getHeight(x, z), z));
				if (req.contains("yaw"))
					obj->setOrientation(Vector3(obj->orientation.x, req["yaw"].get<float>() * 3.14159265f / 180.0f, obj->orientation.z));
			}
		});
		if (!found) return fail("no object " + std::to_string(id));
		ed.pushUndo(std::move(u));
		out["object"] = id;
		out["at"] = { x, z };
		return out;
	}

	if (op == "author.check") {
		MapChecks::Options o;
		o.unit = req.value("unit", o.unit);
		o.fairnessRadiusM = req.value("fairnessRadiusM", o.fairnessRadiusM);
		o.edges = req.value("edges", true);
		o.maxProblems = req.value("maxProblems", 200);
		json r;
		withServer([&](Server& server) { r = json::parse(MapChecks::Run(server, o).dump()); });
		r["op"] = op;
		if (!r.contains("ok")) r["ok"] = true;
		// the reply's ok: the check ran; "clean": no faults
		r["clean"] = r.value("ok", false);
		r["ok"] = true;
		return r;
	}

	if (op == "author.save") {
		const std::string name = req.value("name", std::string());
		if (name.empty()) return fail("save needs a name (saved\\Maps\\<name>.wkbai)");
		const bool ok = editorSave(name);
		out["ok"] = ok;
		out["status"] = ed.status;
		out["folder"] = PathToUtf8(g_userPath / "saved" / "Maps" / PathFromUtf8(name + ".wkbai"));
		if (!ok) out["error"] = ed.status;
		return out;
	}

	if (op == "author.undo" || op == "author.redo") {
		const size_t before = op == "author.undo" ? ed.undo.size() : ed.redo.size();
		if (!before) return fail(op == "author.undo" ? "nothing to undo" : "nothing to redo");
		op == "author.undo" ? editorUndo() : editorRedo();
		out["undoSteps"] = ed.undo.size();
		out["redoSteps"] = ed.redo.size();
		return out;
	}

	if (op == "author.view") {
		if (req.value("release", false)) {
			authorViewHeld = false;
			out["released"] = true;
			return out;
		}
		if (req.contains("camera")) {
			const json& c = req["camera"];
			if (!c.is_array() || c.size() != 5) return fail("camera: [x, y, z, pitch, yaw]");
			authorViewPosition = Vector3(c[0].get<float>(), c[1].get<float>(), c[2].get<float>());
			authorViewOrientation = Vector3(c[3].get<float>(), c[4].get<float>(), 0);
		}
		else {
			float lx, lz;
			if (!ptOf(req.value("at", json()), lx, lz)) return fail("view needs at [x, z] (or camera)");
			float ex, ez;
			if (!ptOf(req.value("from", json()), ex, ez)) {
				const float d = req.value("distance", 150.0f), yawDeg = req.value("yaw", 225.0f); // from the south-west
				ex = lx + std::cos(yawDeg * 3.14159265f / 180.0f) * d;
				ez = lz + std::sin(yawDeg * 3.14159265f / 180.0f) * d;
			}
			// above the highest ground (or water) within 150 m of the eye: never inside a hill
			float top = -1e30f;
			for (float dz = -150; dz <= 150; dz += 10)
				for (float dx = -150; dx <= 150; dx += 10)
					if (dx * dx + dz * dz <= 150 * 150) {
						const int vx = std::clamp((int)std::lround((ex + dx) / TILE) + E, 0, trn->width), vz = std::clamp((int)std::lround((ez + dz) / TILE) + E, 0, trn->height);
						top = std::max(top, trn->getVertex(vx, vz));
						const TerrainTile* t = trn->getTile(std::clamp(vx, 0, trn->width - 1), std::clamp(vz, 0, trn->height - 1));
						if (t && t->fullOfWater) top = std::max(top, t->waterLevel);
					}
			const float y = top + req.value("above", 50.0f);
			const float ly = trn->getHeight(std::clamp(lx, 0.0f, pw * TILE - 0.1f), std::clamp(lz, 0.0f, ph * TILE - 0.1f));
			const float dx = lx - ex, dz = lz - ez;
			authorViewPosition = Vector3(ex, y, ez);
			authorViewOrientation = Vector3(std::atan2(ly - y, std::hypot(dx, dz)), std::atan2(-dx, dz), 0);
		}
		authorViewHeld = true;
		client->camera.position = authorViewPosition;
		client->camera.orientation = authorViewOrientation;
		out["camera"] = { authorViewPosition.x, authorViewPosition.y, authorViewPosition.z, authorViewOrientation.x, authorViewOrientation.y };
		out["hint"] = "the next screenshot shows this view; author.view { \"release\": true } gives the camera back";
		return out;
	}

	if (op == "author.state") {
		json players = json::array();
		withServer([&](Server& server) {
			for (const auto& [t, ch] : server.getLevel()->children)
				for (CommonGameObject* c : ch)
					if (c->blueprint->bpClass == Tags::GAMEOBJCLASS_PLAYER)
						players.push_back({ { "id", c->id }, { "blueprint", c->blueprint->name }, { "name", ((ServerGameObject*)c)->name } });
		});
		out["playableTiles"] = { pw, ph };
		out["playableM"] = { pw * TILE, ph * TILE };
		out["edge"] = E;
		out["heightScale"] = trn->scale;
		out["lakes"] = trn->lakes.size();
		out["players"] = players;
		out["editor"] = editorOn;
		out["loaded"] = loadingDone; // (the loading screen gone: the map drawn as the player sees it)
		out["undoSteps"] = ed.undo.size();
		out["redoSteps"] = ed.redo.size();
		out["camera"] = { client->camera.position.x, client->camera.position.y, client->camera.position.z, client->camera.orientation.x, client->camera.orientation.y };
		return out;
	}

	return fail("author ops: heights, paint, water, place, remove, move, check, save, undo, redo, view, state, quit");
}
