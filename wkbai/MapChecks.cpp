// wkbai - the map check pass and the previews (step 17.8; MapChecks.h)
// Licensed under the GNU General Public License 3

#include "MapChecks.h"
#include "MapAuthoring.h"
#include "MapFolder.h"
#include "MapMaterials.h"
#include "MapObjects.h"
#include "MapWater.h"
#include "MovementController.h"
#include "WaterDesign.h"
#include "server.h"
#include "terrain.h"
#include "file.h"
#include "settings.h"
#include "tags.h"
#include "gameset/gameset.h"
#include "gameset/GameObjBlueprint.h"
#include <stb_image.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#include <stb_image_write.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <queue>
#include <regex>
#include <set>
#include <string>
#include <vector>

namespace MapChecks {

namespace fs = std::filesystem;

namespace {
	constexpr float TILE = 5.0f;
	std::string Num(double v)
	{
		char b[32];
		snprintf(b, sizeof(b), "%.0f", v);
		return b;
	}
	std::string At(float x, float z) { return "(" + Num(x) + ", " + Num(z) + ")"; }
	json Pt(float x, float z) { return json::array({ std::round(x * 10) / 10, std::round(z * 10) / 10 }); }
	std::string Lower(std::string s)
	{
		for (char& c : s) c = (char)tolower((unsigned char)c);
		return s;
	}
	bool Has(const std::string& s, const char* part) { return Lower(s).find(part) != std::string::npos; }
	// the first "(x, z)" in a warning: its place
	bool PlaceIn(const std::string& s, float& x, float& z)
	{
		static const std::regex re("\\(\\s*(-?[0-9]+(?:\\.[0-9]+)?)\\s*,\\s*(-?[0-9]+(?:\\.[0-9]+)?)\\s*\\)");
		std::smatch m;
		if (!std::regex_search(s, m, re))
			return false;
		x = std::stof(m[1].str());
		z = std::stof(m[2].str());
		return true;
	}

	struct Problems {
		json list = json::array();
		std::map<std::string, std::map<std::string, int>> counts; // check -> severity -> n
		int edge = 0, max = 400, perCheck = 60;
		void add(const std::string& check, const std::string& severity, const std::string& what, bool hasPlace, float x, float z,
			const std::string& fix, const json& fixOp = json())
		{
			int& n = counts[check][severity];
			n++;
			// at most perCheck of one check and severity listed (all counted): an original map's
			// hundreds of seams don't hide the rest
			if ((int)list.size() >= max || n > perCheck)
				return;
			json p = { { "id", (int)list.size() + 1 }, { "check", check }, { "severity", severity }, { "what", what } };
			if (hasPlace) {
				p["at"] = Pt(x, z);
				p["tile"] = { (int)std::floor(x / TILE) + edge, (int)std::floor(z / TILE) + edge };
			}
			else
				p["at"] = nullptr;
			p["fix"] = fix;
			if (!fixOp.is_null())
				p["fixOp"] = fixOp;
			list.push_back(p);
		}
	};

	// the players the game plays (the neutral and the barbarians' are left out)
	bool IsPlayingPlayer(const CommonGameObject* o)
	{
		return o->blueprint->bpClass == Tags::GAMEOBJCLASS_PLAYER && o->blueprint->name != "Neutral Player" && o->blueprint->name != "Baresark Player";
	}
	template <typename F> void Walk(CommonGameObject* o, const F& f)
	{
		for (const auto& [type, children] : o->children)
			for (CommonGameObject* c : children) {
				f(c);
				Walk(c, f);
			}
	}

	// a fix suggested by a placement status
	std::string PlacementFix(const std::string& status, const std::string& bp, float x, float z)
	{
		if (status == "InvalidTerrainType")
			return "the ground under the " + bp + " is a terrain its footprint can't stand on: move it (author.move) to ground it allows, "
				"or paint its tiles with a buildable ground (author.paint, e.g. GRASS1)";
		if (Has(status, "height") || Has(status, "slope") || Has(status, "steep") || Has(status, "differential"))
			return "the ground under it is too uneven: flatten it (author.heights flatten, radius 15 m at " + At(x, z) + ") or move it";
		if (Has(status, "occupied") || Has(status, "building") || Has(status, "blocked") || Has(status, "tile"))
			return "its footprint overlaps another building, a tree or a deposit: move one of them (author.move) or remove what is in the way";
		if (Has(status, "city") || Has(status, "town") || Has(status, "civic") || Has(status, "connected"))
			return "the city rules refuse it: a civic building must touch its city (place it within the city's rectangles), a civic centre must stand apart, no farm in a town";
		if (Has(status, "map") || Has(status, "outside"))
			return "its footprint leaves the map: move it inwards";
		return "move it (author.move) to a site the server accepts (the compiler's bases find one: map.json players / bases)";
	}

	// a water warning's fix
	std::string WaterFix(const std::string& w)
	{
		if (Has(w, "leak") || Has(w, "edge") || Has(w, "beyond") || Has(w, "drown"))
			return "the lake runs over its rim: raise the rim where it runs out (author.heights raise at the place) or lower the lake's level (author.water)";
		if (Has(w, "puddle") || Has(w, "flat ground") || Has(w, "shallow"))
			return "a puddle on flat ground: remove the lake point (author.water remove) or dig a basin (author.heights lower, radius 40 m)";
		if (Has(w, "rise") || Has(w, "climb") || Has(w, "dam"))
			return "the river's water climbs downstream: lower the bed there (author.heights lower along the course) or move the waterway's points";
		if (Has(w, "dry"))
			return "a painted river with no water: carve its bed lower than its source, or add a lake point upstream (author.water)";
		if (Has(w, "hint"))
			return "a far-land river hint with no river leaving the map there: carve a river to that edge or drop the hint (set_water farland)";
		if (Has(w, "fall"))
			return "a placed fall that draws nothing: put it where the water drops (a step in the bed) or remove it";
		return "see the water report (water_report) for the lake or waterway named";
	}

	// heights of the playable area, metres
	float H(const Terrain& t, int x, int z) { return t.getVertex(std::clamp(x, 0, t.width) , std::clamp(z, 0, t.height)); }
}

json Run(Server& server, const Options& o, Grids* gridsOut)
{
	json out;
	Problems P;
	P.max = o.maxProblems;
	Terrain* t = server.terrain;
	ServerGameObject* level = server.getLevel();
	if (!t || !level || !server.gameSet) {
		out["ok"] = false;
		out["error"] = "no map loaded";
		return out;
	}
	const GameSet& gs = *server.gameSet;
	const int E = t->edge, W = t->width, Hh = t->height;
	P.edge = E;
	const auto [pw, ph] = t->getNumPlayableTiles();
	Grids G;
	G.pw = pw;
	G.ph = ph;
	out["format"] = "wkbai map check";
	out["version"] = CHECK_VERSION;
	out["size"] = { { "playableTiles", { pw, ph } }, { "playableM", { pw * TILE, ph * TILE } }, { "edge", E } };

	// ---- players: their start camera and objects; each one's home (its first building, else
	// its first unit)
	json players = json::array();
	struct Home { ServerGameObject* player; CommonGameObject* home; };
	std::vector<Home> homes;
	for (const auto& [type, children] : level->children)
		for (CommonGameObject* c : children) {
			if (!IsPlayingPlayer(c))
				continue;
			ServerGameObject* pl = (ServerGameObject*)c;
			size_t objects = 0;
			CommonGameObject* home = nullptr;
			Walk(pl, [&](CommonGameObject* ob) {
				if (ob->blueprint->bpClass != Tags::GAMEOBJCLASS_BUILDING && ob->blueprint->bpClass != Tags::GAMEOBJCLASS_CHARACTER)
					return;
				objects++;
				// its first building by id (a base's civic centre comes first), else its first unit
				const bool b = ob->blueprint->bpClass == Tags::GAMEOBJCLASS_BUILDING, hb = home && home->blueprint->bpClass == Tags::GAMEOBJCLASS_BUILDING;
				if (!home || (b && !hb) || (b == hb && ob->id < home->id))
					home = ob;
			});
			const std::string who = "player " + std::to_string(pl->id) + (pl->name.empty() ? "" : " (" + pl->name + ")");
			const bool camera = !(pl->playerStartCameraPosition == Vector3(0, 0, 0));
			json pj = { { "id", pl->id }, { "name", pl->name }, { "colour", pl->color }, { "objects", objects }, { "startCamera", camera } };
			if (home)
				pj["home"] = { { "blueprint", home->blueprint->name }, { "at", Pt(home->position.x, home->position.z) } };
			players.push_back(pj);
			if (!camera)
				P.add("players", "warning", who + " has no start camera: the game starts it looking at the map's corner", home != nullptr,
					home ? home->position.x : 0, home ? home->position.z : 0,
					"give it a start (map.json players[i].start: the compiler aims the camera at its base), or set it in the editor's Players mode");
			if (!objects)
				P.add("players", "error", who + " has no units or buildings: it can't play", false, 0, 0,
					"give it a base: map.json players[i].start (a Manor and peasants) or author.place a Manor for it",
					json{ { "op", "author.place" }, { "blueprint", "Manor" }, { "player", pl->id }, { "at", "[x, z]" } });
			if (home)
				homes.push_back({ pl, home });
		}
	out["players"] = players;

	// ---- the land the unit can stand on, by tile
	const GameObjBlueprint* unit = gs.findBlueprint(Tags::GAMEOBJCLASS_CHARACTER, o.unit);
	if (!unit)
		unit = gs.findBlueprint(Tags::GAMEOBJCLASS_CHARACTER, "Peasant");
	const size_t NT = (size_t)pw * ph;
	G.blocked.assign(NT, 0);
	G.region.assign(NT, -1);
	int bySlope = 0, byTerrain = 0, byWater = 0;
	const float slopeLimit = TILE; // 45 degrees: a rise of a tile's width
	for (int z = 0; z < ph; z++)
		for (int x = 0; x < pw; x++) {
			const size_t i = (size_t)z * pw + x;
			if (unit && !MovementController::isTerrainBlocked(unit, x, z))
				continue;
			if (!unit) continue;
			const TerrainTile* tile = t->getPlayableTile(x, z);
			const float a = H(*t, E + x, E + z), b = H(*t, E + x + 1, E + z), c = H(*t, E + x, E + z + 1), d = H(*t, E + x + 1, E + z + 1);
			const float lo = std::min({ a, b, c, d }), hi = std::max({ a, b, c, d });
			if (tile && tile->fullOfWater && tile->waterLevel > lo) { G.blocked[i] = 3; byWater++; }
			else if (hi - lo > slopeLimit) { G.blocked[i] = 1; bySlope++; }
			else { G.blocked[i] = 2; byTerrain++; }
		}
	// regions, 8-connected as the path finding moves
	std::vector<int> sizes;
	for (size_t s = 0; s < NT; s++) {
		if (G.blocked[s] || G.region[s] >= 0)
			continue;
		const int r = (int)sizes.size();
		sizes.push_back(0);
		std::vector<size_t> stack{ s };
		G.region[s] = r;
		while (!stack.empty()) {
			const size_t i = stack.back();
			stack.pop_back();
			sizes[r]++;
			const int x = (int)(i % pw), z = (int)(i / pw);
			for (int dz = -1; dz <= 1; dz++)
				for (int dx = -1; dx <= 1; dx++) {
					const int nx = x + dx, nz = z + dz;
					if ((!dx && !dz) || nx < 0 || nz < 0 || nx >= pw || nz >= ph) continue;
					const size_t j = (size_t)nz * pw + nx;
					if (G.blocked[j] || G.region[j] >= 0) continue;
					G.region[j] = r;
					stack.push_back(j);
				}
		}
	}
	int walkable = 0;
	for (int n : sizes) walkable += n;
	G.mainRegion = sizes.empty() ? -1 : (int)(std::max_element(sizes.begin(), sizes.end()) - sizes.begin());
	// the nearest tile of a region to another: a search over every tile from one region's tiles
	// (4-connected), giving the two ends of the shortest way between them (a ramp's)
	auto nearestBetween = [&](int from, int to, int& ax, int& az, int& bx, int& bz) {
		std::vector<int> parent(NT, -2);
		std::queue<size_t> q;
		for (size_t i = 0; i < NT; i++)
			if (G.region[i] == from) { parent[i] = -1; q.push(i); }
		while (!q.empty()) {
			const size_t i = q.front();
			q.pop();
			if (G.region[i] == to) {
				bx = (int)(i % pw), bz = (int)(i / pw);
				size_t k = i;
				while (parent[k] >= 0) k = (size_t)parent[k];
				ax = (int)(k % pw), az = (int)(k / pw);
				return true;
			}
			const int x = (int)(i % pw), z = (int)(i / pw);
			const int nb[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
			for (const auto& d : nb) {
				const int nx = x + d[0], nz = z + d[1];
				if (nx < 0 || nz < 0 || nx >= pw || nz >= ph) continue;
				const size_t j = (size_t)nz * pw + nx;
				if (parent[j] != -2) continue;
				parent[j] = (int)i;
				q.push(j);
			}
		}
		return false;
	};
	// a ramp between two lands' nearest tiles, ready to send: from 8 m inside the higher land out
	// over the lower one, long enough for about 15 degrees (at least 30 m), 16 m wide
	auto ramp = [&](int ax, int az, int bx, int bz) {
		float x0 = ax * TILE + 2.5f, z0 = az * TILE + 2.5f, x1 = bx * TILE + 2.5f, z1 = bz * TILE + 2.5f;
		if (t->getHeight(x0, z0) < t->getHeight(x1, z1)) { std::swap(x0, x1); std::swap(z0, z1); }
		const float rise = std::abs(t->getHeight(x0, z0) - t->getHeight(x1, z1));
		float dx = x1 - x0, dz = z1 - z0;
		const float n = std::max(std::hypot(dx, dz), 0.01f);
		dx /= n, dz /= n;
		const float len = std::max(30.0f, rise / 0.268f);
		auto clampX = [&](float v) { return std::clamp(v, 2.5f, pw * TILE - 2.5f); };
		auto clampZ = [&](float v) { return std::clamp(v, 2.5f, ph * TILE - 2.5f); };
		return json{ { "op", "author.heights" }, { "mode", "ramp" },
			{ "line", { Pt(clampX(x0 - dx * 8), clampZ(z0 - dz * 8)), Pt(clampX(x1 + dx * len), clampZ(z1 + dz * len)) } }, { "width", 16 }, { "falloff", 5 } };
	};
	// what stands on each region (buildings, characters, gold and stone)
	std::map<int, int> standing;
	auto itemOf = [&](const char* name, const char* upper) { const int i = gs.items.names.getIndex(name); return i >= 0 ? i : gs.items.names.getIndex(upper); };
	const int goldItem = itemOf("Gold", "GOLD"), stoneItem = itemOf("Stone", "STONE");
	Walk(level, [&](CommonGameObject* ob) {
		const int cls = ob->blueprint->bpClass;
		bool counts = cls == Tags::GAMEOBJCLASS_BUILDING || cls == Tags::GAMEOBJCLASS_CHARACTER;
		if (cls == Tags::GAMEOBJCLASS_CONTAINER)
			counts = (goldItem >= 0 && ob->getItem(goldItem) > 0) || (stoneItem >= 0 && ob->getItem(stoneItem) > 0);
		if (!counts) return;
		const int tx = (int)(ob->position.x / TILE), tz = (int)(ob->position.z / TILE);
		if (tx < 0 || tz < 0 || tx >= pw || tz >= ph) return;
		// a building's own tile may be blocked: the nearest region within 2 tiles
		for (int r = 0; r <= 2; r++)
			for (int dz = -r; dz <= r; dz++)
				for (int dx = -r; dx <= r; dx++) {
					const int nx = tx + dx, nz = tz + dz;
					if (nx < 0 || nz < 0 || nx >= pw || nz >= ph) continue;
					const int reg = G.region[(size_t)nz * pw + nx];
					if (reg >= 0 && reg != G.mainRegion) { standing[reg]++; return; }
					if (reg >= 0) return;
				}
	});
	json islands = json::array();
	int unreachedTiles = 0, islandCount = 0;
	{
		std::vector<std::pair<int, int>> order;
		for (int r = 0; r < (int)sizes.size(); r++)
			if (r != G.mainRegion) { order.push_back({ -sizes[r], r }); unreachedTiles += sizes[r]; }
		std::sort(order.begin(), order.end());
		// each region's middle tile (the one nearest its mean)
		std::vector<double> sx(sizes.size()), sz(sizes.size());
		for (size_t i = 0; i < NT; i++)
			if (G.region[i] >= 0) { sx[G.region[i]] += (double)(i % pw); sz[G.region[i]] += (double)(i / pw); }
		for (const auto& [neg, r] : order) {
			if (-neg < 6) break; // single tiles between rocks: the path finding's noise
			islandCount++;
			if (islandCount > 60) continue;
			const double mx = sx[r] / sizes[r], mz = sz[r] / sizes[r];
			int cx = -1, cz = -1;
			double best = 1e30;
			for (size_t i = 0; i < NT; i++)
				if (G.region[i] == r) {
					const double d = std::pow(i % pw - mx, 2) + std::pow(i / pw - mz, 2);
					if (d < best) { best = d; cx = (int)(i % pw); cz = (int)(i / pw); }
				}
			int ax = cx, az = cz, bx = cx, bz = cz;
			const bool way = G.mainRegion >= 0 && nearestBetween(r, G.mainRegion, ax, az, bx, bz);
			const int on = standing.count(r) ? standing[r] : 0;
			islands.push_back({ { "tiles", sizes[r] }, { "at", Pt(cx * TILE + 2.5f, cz * TILE + 2.5f) }, { "objects", on },
				{ "nearestMainLand", way ? Pt(bx * TILE + 2.5f, bz * TILE + 2.5f) : json(nullptr) } });
			const std::string what = std::to_string(sizes[r]) + " tiles of walkable ground no unit can reach from the main land" +
				(on ? " (" + std::to_string(on) + " buildings, units or deposits stand on it)" : "");
			P.add("terrain", on ? "warning" : "info", what, true, cx * TILE + 2.5f, cz * TILE + 2.5f,
				way ? "connect it with a ramp from " + At(ax * TILE + 2.5f, az * TILE + 2.5f) + " to " + At(bx * TILE + 2.5f, bz * TILE + 2.5f) +
				" (author.heights ramp: the fixOp), or leave it as scenery" : "leave it as scenery",
				way ? ramp(ax, az, bx, bz) : json());
		}
	}
	const float playableTiles = (float)std::max<size_t>(NT, 1);
	out["terrain"] = { { "unit", unit ? unit->name : "none" }, { "walkableShare", std::round(1000.0 * walkable / playableTiles) / 1000 },
		{ "blockedBySlopeShare", std::round(1000.0 * bySlope / playableTiles) / 1000 }, { "blockedByTerrainShare", std::round(1000.0 * byTerrain / playableTiles) / 1000 },
		{ "waterShare", std::round(1000.0 * byWater / playableTiles) / 1000 },
		{ "mainLandShare", G.mainRegion >= 0 ? std::round(1000.0 * sizes[G.mainRegion] / playableTiles) / 1000 : 0.0 },
		{ "regions", sizes.size() }, { "unreachedTiles", unreachedTiles }, { "islands", islandCount }, { "islandList", islands },
		{ "about", "walkable: the tiles the unit can stand on by the game's own test (MovementController::isTerrainBlocked: water, the terrain's passability, 45 degrees); regions 8-connected; islands: regions of 6 tiles or more apart from the largest (the main land)" } };
	if (sizes.empty() || walkable < (int)(0.2f * playableTiles))
		P.add("terrain", "warning", "only " + Num(100.0 * walkable / playableTiles) + " % of the map is walkable", false, 0, 0,
			"lower the steepest ground (author.heights smooth), paint passable ground over blocking terrain, or drain water");

	// ---- the starts: each player's home on the nearest walkable tile within 10 tiles
	for (const Home& h : homes) {
		Grids::Start s;
		s.who = "player " + std::to_string(h.player->id) + (h.player->name.empty() ? "" : " (" + h.player->name + ")");
		s.player = h.player->id;
		s.colour = h.player->color;
		s.x = h.home->position.x;
		s.z = h.home->position.z;
		const int hx = (int)(s.x / TILE), hz = (int)(s.z / TILE);
		for (int r = 0; r <= 10 && s.tx < 0; r++)
			for (int dz = -r; dz <= r && s.tx < 0; dz++)
				for (int dx = -r; dx <= r; dx++) {
					const int nx = hx + dx, nz = hz + dz;
					if ((std::abs(dx) == r || std::abs(dz) == r) && nx >= 0 && nz >= 0 && nx < pw && nz < ph && !G.blocked[(size_t)nz * pw + nx]) {
						s.tx = nx;
						s.tz = nz;
						break;
					}
				}
		if (s.tx < 0)
			P.add("reachability", "error", s.who + "'s base stands on no walkable ground (none within 50 m)", true, s.x, s.z,
				"move the base to open ground (author.move) or flatten round it (author.heights flatten, radius 40 m)",
				json{ { "op", "author.heights" }, { "mode", "flatten" }, { "circle", { { "at", Pt(s.x, s.z) }, { "radius", 40 } } } });
		G.starts.push_back(s);
	}
	// path lengths by land from each start (8 moves, 5 m or 7.07 m), with the way back
	const size_t S = G.starts.size();
	std::vector<std::vector<float>> dist(S);
	std::vector<std::vector<int>> from(S);
	for (size_t k = 0; k < S; k++) {
		if (G.starts[k].tx < 0) continue;
		dist[k].assign(NT, 1e30f);
		from[k].assign(NT, -1);
		using Q = std::pair<float, size_t>;
		std::priority_queue<Q, std::vector<Q>, std::greater<Q>> q;
		const size_t s0 = (size_t)G.starts[k].tz * pw + G.starts[k].tx;
		dist[k][s0] = 0;
		q.push({ 0.0f, s0 });
		while (!q.empty()) {
			const auto [d, i] = q.top();
			q.pop();
			if (d > dist[k][i]) continue;
			const int x = (int)(i % pw), z = (int)(i / pw);
			for (int dz = -1; dz <= 1; dz++)
				for (int dx = -1; dx <= 1; dx++) {
					const int nx = x + dx, nz = z + dz;
					if ((!dx && !dz) || nx < 0 || nz < 0 || nx >= pw || nz >= ph) continue;
					const size_t j = (size_t)nz * pw + nx;
					if (G.blocked[j]) continue;
					const float nd = d + (dx && dz ? 7.071f : 5.0f);
					if (nd < dist[k][j]) { dist[k][j] = nd; from[k][j] = (int)i; q.push({ nd, j }); }
				}
		}
	}
	// the clearance: tiles to the nearest blocked tile or the map's edge (chamfer 3-4), for the
	// choke points
	std::vector<int> clear(NT, 1 << 20);
	for (size_t i = 0; i < NT; i++)
		if (G.blocked[i]) clear[i] = 0;
	for (int z = 0; z < ph; z++)
		for (int x = 0; x < pw; x++) {
			int& c = clear[(size_t)z * pw + x];
			c = std::min(c, 3 * (std::min({ x, z, pw - 1 - x, ph - 1 - z }) + 1));
			if (x > 0) c = std::min(c, clear[(size_t)z * pw + x - 1] + 3);
			if (z > 0) c = std::min(c, clear[(size_t)(z - 1) * pw + x] + 3);
			if (x > 0 && z > 0) c = std::min(c, clear[(size_t)(z - 1) * pw + x - 1] + 4);
			if (x + 1 < pw && z > 0) c = std::min(c, clear[(size_t)(z - 1) * pw + x + 1] + 4);
		}
	for (int z = ph - 1; z >= 0; z--)
		for (int x = pw - 1; x >= 0; x--) {
			int& c = clear[(size_t)z * pw + x];
			if (x + 1 < pw) c = std::min(c, clear[(size_t)z * pw + x + 1] + 3);
			if (z + 1 < ph) c = std::min(c, clear[(size_t)(z + 1) * pw + x] + 3);
			if (x + 1 < pw && z + 1 < ph) c = std::min(c, clear[(size_t)(z + 1) * pw + x + 1] + 4);
			if (x > 0 && z + 1 < ph) c = std::min(c, clear[(size_t)(z + 1) * pw + x - 1] + 4);
		}
	json pairs = json::array();
	std::vector<float> nearestRival(S, 1e30f);
	for (size_t a = 0; a < S; a++)
		for (size_t b = a + 1; b < S; b++) {
			const Grids::Start &A = G.starts[a], &B = G.starts[b];
			if (A.tx < 0 || B.tx < 0) continue;
			const float straight = std::hypot(A.x - B.x, A.z - B.z);
			const size_t bi = (size_t)B.tz * pw + B.tx;
			json pj = { { "from", A.player }, { "to", B.player }, { "straightM", std::round(straight) } };
			if (dist[a][bi] < 1e29f) {
				std::vector<std::pair<int, int>> path;
				int chokeC = 1 << 20;
				size_t chokeAt = bi;
				for (int i = (int)bi; i >= 0; i = from[a][i]) {
					path.push_back({ i % pw, i / pw });
					// the narrowest point away from the two bases (the 60 m round each are theirs)
					const float da = std::hypot((i % pw - A.tx) * TILE, (i / pw - A.tz) * TILE), db = std::hypot((i % pw - B.tx) * TILE, (i / pw - B.tz) * TILE);
					if (da > 60 && db > 60 && clear[i] < chokeC) { chokeC = clear[i]; chokeAt = (size_t)i; }
				}
				G.paths.push_back(path);
				pj["pathM"] = std::round(dist[a][bi]);
				pj["detour"] = std::round(100.0 * dist[a][bi] / std::max(straight, 1.0f)) / 100;
				nearestRival[a] = std::min(nearestRival[a], dist[a][bi]);
				nearestRival[b] = std::min(nearestRival[b], dist[a][bi]);
				if (chokeC < (1 << 20)) {
					// width: twice the clearance (chamfer units of a third of a tile)
					const float width = 2.0f * chokeC / 3.0f * TILE;
					const float cx = (chokeAt % pw) * TILE + 2.5f, cz = (chokeAt / pw) * TILE + 2.5f;
					pj["choke"] = { { "at", Pt(cx, cz) }, { "widthM", std::round(width) } };
					G.chokes.push_back({ cx, cz });
					if (width < 12.0f)
						P.add("fairness", "info", "the way by land from " + A.who + " to " + B.who + " narrows to about " + Num(width) + " m", true, cx, cz,
							"a choke point (often wanted); widen it with author.heights smooth there if armies should pass in formation");
				}
			}
			else {
				pj["pathM"] = nullptr;
				int ax = A.tx, az = A.tz, bx = B.tx, bz = B.tz;
				const int ra = G.region[(size_t)A.tz * pw + A.tx], rb = G.region[bi];
				const bool way = nearestBetween(ra, rb, ax, az, bx, bz);
				P.add("reachability", "error", B.who + " can't be reached by land from " + A.who, true, B.x, B.z,
					way ? "join their lands: a ramp or pass from " + At(ax * TILE + 2.5f, az * TILE + 2.5f) + " to " + At(bx * TILE + 2.5f, bz * TILE + 2.5f) +
					" (author.heights ramp), or a ford across the water there" : "join their lands with a ramp or a ford",
					way ? ramp(ax, az, bx, bz) : json());
			}
			pairs.push_back(pj);
		}
	out["reachability"] = { { "starts", [&] {
		json s = json::array();
		for (const auto& st : G.starts)
			s.push_back({ { "player", st.player }, { "at", Pt(st.x, st.z) }, { "tile", st.tx >= 0 ? json{ st.tx + E, st.tz + E } : json(nullptr) },
				{ "region", st.tx >= 0 ? G.region[(size_t)st.tz * pw + st.tx] : -1 } });
		return s; }() }, { "pairs", pairs } };

	// ---- placement: the server's own rules on every building, units and deposits on buildings
	{
		std::vector<std::string> pw0;
		const json pl = MapObjects::CheckPlacement(&server, nullptr);
		json pc = pl;
		for (const json& f : pl.value("failed", json::array())) {
			const float x = f["at"][0].get<float>(), z = f["at"][1].get<float>();
			const std::string bp = f.value("blueprint", std::string()), st = f.value("status", std::string());
			json op = json::object();
			if (st != "InvalidTerrainType" && (Has(st, "height") || Has(st, "differential") || Has(st, "slope")))
				op = { { "op", "author.heights" }, { "mode", "flatten" }, { "circle", { { "at", Pt(x, z) }, { "radius", 15 } } } };
			else if (st == "InvalidTerrainType")
				op = { { "op", "author.paint" }, { "group", "GRASS1" }, { "circle", { { "at", Pt(x, z) }, { "radius", 15 } } } };
			P.add("placement", "error", bp + " " + std::to_string(f.value("id", 0)) + " of player " + std::to_string(f.value("player", 0)) +
				" fails the server's placement rules: " + st, true, x, z, PlacementFix(st, bp, x, z), op.empty() ? json() : op);
		}
		for (const json& b : pl.value("objectsBlockedAt", json::array())) {
			const float x = b["at"][0].get<float>(), z = b["at"][1].get<float>();
			const std::string on = b.value("on", std::string());
			P.add("placement", "warning", b.value("blueprint", std::string()) + " " + std::to_string(b.value("id", 0)) + (on == "water" ? " stands in water over a metre deep" : " stands on " + on),
				true, x, z, on == "water" ? "move it ashore (author.move) or lower the water there" : "move it off the building (author.move) or remove it (author.remove)",
				json{ { "op", "author.move" }, { "object", b.value("id", 0) }, { "to", "[x, z]" } });
		}
		out["placement"] = pc;
	}

	// ---- water
	{
		std::vector<std::string> ww;
		out["water"] = json::parse(MapWater::Check(*t, o.seaLevel, ww).dump());
		for (const std::string& w : ww) {
			float x = 0, z = 0;
			const bool place = PlaceIn(w, x, z);
			P.add("water", Has(w, "rapids") ? "info" : "warning", w, place, x, z, WaterFix(w));
		}
	}

	// ---- tile edges: the ground solver's unmatched tiles, else every seam between two grounds
	{
		const json* rep = o.compileReport;
		const bool fromReport = rep && rep->contains("materials") && (*rep)["materials"].contains("unmatched");
		if (fromReport) {
			const json& u = (*rep)["materials"]["unmatched"];
			for (const json& e : u.value("list", json::array())) {
				if (!e.value("playable", true)) continue;
				const float x = e["atM"][0].get<float>(), z = e["atM"][1].get<float>();
				P.add("edges", "warning", "tile " + e["tile"].dump() + ": " + e.value("reason", std::string()), true, x, z,
					"a transition with no matching picture: paint the tile and its neighbours with one of the two grounds (author.paint, radius 6 m), "
					"or a bridging ground between them (the materials' rules)",
					json{ { "op", "author.paint" }, { "group", "<one of the two groups>" }, { "circle", { { "at", Pt(x, z) }, { "radius", 6 } } } });
			}
			out["edges"] = { { "from", "the compiler's ground solver (report.json materials.unmatched)" }, { "count", u.value("count", 0) },
				{ "edgesByGroups", u.value("edgesByGroups", json::array()) } };
		}
		else if (o.edges) {
			std::string db;
			for (wchar_t c : t->texDbPath) if (c) db += (char)c;
			const json ef = MapMaterials::EdgeFaults(*t, db, 300);
			for (const json& e : ef["list"]) {
				const float x = e["atM"][0].get<float>(), z = e["atM"][1].get<float>();
				const std::string bridge = e["bridge"].is_string() ? e["bridge"].get<std::string>() : "";
				const std::string g = e.value("groups", std::string());
				const std::string first = g.substr(0, g.find(" | "));
				P.add("edges", "warning", "tile " + e["tile"].dump() + " " + e.value("side", std::string()) + " side: " + g + " meet with no transition tile", true, x, z,
					bridge.empty() ? "paint both tiles with one of the two grounds (author.paint, radius 6 m: it fits the transitions round it)"
					: "paint a strip of " + bridge + " between them (it has transitions to both), or one of the two grounds over both (author.paint)",
					json{ { "op", "author.paint" }, { "group", bridge.empty() ? first : bridge }, { "circle", { { "at", Pt(x, z) }, { "radius", 6 } } } });
			}
			out["edges"] = { { "from", "the tiles as they are (MapMaterials::EdgeFaults)" }, { "count", ef["count"] }, { "byGroups", ef["byGroups"] } };
		}
	}

	// ---- fairness: resources near the bases, the nearest rival by land, high ground, symmetry
	{
		std::vector<std::string> fw;
		json f = MapObjects::Fairness(&server, o.fairnessRadiusM, &fw);
		const json bases = f.value("bases", json::array());
		if (f.contains("ratios") && f["ratios"].is_object())
			for (const auto& [res, r] : f["ratios"].items()) {
				const double ratio = r.value("ratio", 1.0);
				if (ratio >= 0.75 || r.value("max", 0.0) <= 0) continue;
				// the base with the least
				float x = 0, z = 0;
				bool place = false;
				double least = 1e30;
				for (const json& b : bases) {
					const double v = res == "farmableTiles" ? b.value("farmableTiles", 0.0)
						: b.contains("resources") && b["resources"].contains(res) ? b["resources"][res].get<double>() : 1e30;
					if (v < least) { least = v; x = b["at"][0].get<float>(); z = b["at"][1].get<float>(); place = true; }
				}
				P.add("fairness", "warning", res + " within " + Num(o.fairnessRadiusM) + " m of the bases runs from " + Num(r.value("min", 0.0)) + " to " +
					Num(r.value("max", 0.0)) + " (ratio " + std::to_string(ratio).substr(0, 4) + "); the least at the base here", place, x, z,
					res == "farmableTiles" ? "give this base more open farmland: paint farmable ground (author.paint GRASS1) or clear trees near it"
					: "add " + res + " near this base: a fair scatter rule (set_scatter near bases, fair: true) or author.place a deposit within " + Num(o.fairnessRadiusM) + " m",
					res == "farmableTiles" ? json() : json{ { "op", "author.place" }, { "blueprint", "<a " + res + " deposit>" }, { "at", Pt(x, z) } });
			}
		// the nearest rival by land
		float lo = 1e30f, hi = 0;
		size_t loK = 0;
		for (size_t k = 0; k < S; k++)
			if (nearestRival[k] < 1e29f) {
				if (nearestRival[k] < lo) { lo = nearestRival[k]; loK = k; }
				hi = std::max(hi, nearestRival[k]);
			}
		if (hi > 0 && lo < 1e29f && lo / hi < 0.75f)
			P.add("fairness", "warning", G.starts[loK].who + "'s nearest rival is " + Num(lo) + " m away by land, another player's " + Num(hi) + " m (ratio " +
				std::to_string(lo / hi).substr(0, 4) + ")", true, G.starts[loK].x, G.starts[loK].z,
				"move the starts so the nearest rivals are as far for each (map.json players[i].start, or bases.auto), or lengthen the short way");
		// the high ground within the radius above each start
		json high = json::array();
		float hlo = 1e30f, hhi = -1e30f;
		for (const auto& st : G.starts) {
			const float g0 = t->getHeight(st.x, st.z);
			float top = g0;
			const int r = (int)(o.fairnessRadiusM / TILE);
			for (int dz = -r; dz <= r; dz += 2)
				for (int dx = -r; dx <= r; dx += 2)
					if (dx * dx + dz * dz <= r * r) {
						const float x = st.x + dx * TILE, z = st.z + dz * TILE;
						if (x >= 0 && z >= 0 && x < pw * TILE && z < ph * TILE) top = std::max(top, t->getHeight(x, z));
					}
			high.push_back({ { "player", st.player }, { "aboveBaseM", std::round((top - g0) * 10) / 10 } });
			hlo = std::min(hlo, top - g0);
			hhi = std::max(hhi, top - g0);
		}
		// a symmetry score: the heights against the map turned half round and mirrored both ways
		json sym = json::object();
		{
			float lo2 = 1e30f, hi2 = -1e30f;
			for (int z = E; z <= Hh - E; z++)
				for (int x = E; x <= W - E; x++) { const float v = H(*t, x, z); lo2 = std::min(lo2, v); hi2 = std::max(hi2, v); }
			const float range = std::max(hi2 - lo2, 1.0f);
			const char* names[3] = { "half turn", "mirror east-west", "mirror north-south" };
			double best = -1;
			std::string bestName;
			for (int k = 0; k < 3; k++) {
				double sum = 0;
				int n = 0;
				for (int z = 0; z <= ph; z += 2)
					for (int x = 0; x <= pw; x += 2) {
						// half turn (pw - x, ph - z); mirror east-west (pw - x, z); mirror north-south (x, ph - z)
						const int mx = k == 2 ? x : pw - x, mz = k == 1 ? z : ph - z;
						sum += std::abs(H(*t, E + x, E + z) - H(*t, E + mx, E + mz));
						n++;
					}
				const double score = 1.0 - sum / std::max(n, 1) / range;
				sym[names[k]] = std::round(score * 1000) / 1000;
				if (score > best) { best = score; bestName = names[k]; }
			}
			sym["best"] = bestName;
			sym["about"] = "1 - the mean height difference to the map so turned or mirrored, over the height range (1: the same land)";
		}
		f["nearestRivalByLandM"] = [&] { json a = json::array(); for (size_t k = 0; k < S; k++) a.push_back(nearestRival[k] < 1e29f ? json(std::round(nearestRival[k])) : json(nullptr)); return a; }();
		f["highGround"] = high;
		f["symmetry"] = sym;
		if (S >= 2 && hhi - hlo > 20.0f)
			P.add("fairness", "info", "the high ground near the starts differs: from " + Num(hlo) + " m to " + Num(hhi) + " m above the base", false, 0, 0,
				"even it out if the map should be fair (a hill near the lower ones, author.heights raise)");
		out["fairness"] = f;
	}

	// ---- the compiler's other warnings (a folder)
	if (o.compileReport && o.compileReport->contains("warnings")) {
		for (const json& wj : (*o.compileReport)["warnings"]) {
			const std::string w = wj.is_string() ? wj.get<std::string>() : wj.dump();
			// covered above, on the map as it is now
			if (w.rfind("placement:", 0) == 0 || w.rfind("water:", 0) == 0 || w.rfind("fairness:", 0) == 0 || w.rfind("ground:", 0) == 0)
				continue;
			float x = 0, z = 0;
			const bool place = PlaceIn(w, x, z);
			P.add("compiler", "warning", w, place, x, z, Has(w, "no site") ? "give the start more flat open ground (author.heights flatten, radius 40 m) or move it"
				: Has(w, "no material") || Has(w, "no hand tile") ? "give the map a ground rule (set_material_rules) or ground.fill" : "see the compiler's report");
		}
	}

	// ---- the summary
	int errors = 0, warnings = 0, infos = 0;
	json byCheck = json::object();
	for (const auto& [check, sev] : P.counts) {
		json c = json::object();
		for (const auto& [s, n] : sev) {
			c[s] = n;
			(s == "error" ? errors : s == "warning" ? warnings : infos) += n;
		}
		byCheck[check] = c;
	}
	out["summary"] = { { "errors", errors }, { "warnings", warnings }, { "info", infos }, { "faults", errors + warnings }, { "byCheck", byCheck },
		{ "listed", P.list.size() } };
	out["ok"] = errors + warnings == 0;
	out["problems"] = P.list;
	out["about"] = "each problem: check, severity (error: the map can't be played as meant; warning: should be fixed; info: worth knowing), what, at "
		"(playable metres, x east, z north from the south-west corner), tile (whole map, edge band included), fix, and fixOp: an author.* op "
		"(the live ops over the agent-control socket) that would make the fix, where the values in [ ] or < > are the agent's to choose";
	if (gridsOut)
		*gridsOut = std::move(G);
	return out;
}

bool Check(const std::string& map, Options o, json& out, std::string& error, Grids* grids, json* cameras)
{
	const std::string level = MapAuthoring::LevelOf(map, error);
	if (level.empty())
		return false;
	// a folder: its compiler report (compiled now when stale) and its sea level
	json report;
	const fs::path folder = MapFolder::IsMapPath(map) ? MapFolder::Resolve(map) : fs::path();
	if (!folder.empty()) {
		const MapFolder::Result r = MapFolder::Compile(folder, false);
		if (!r.ok) {
			error = "the map doesn't compile: " + r.error;
			return false;
		}
		std::ifstream f(folder / "build" / "report.json", std::ios::binary);
		report = json::parse(f, nullptr, false);
		if (report.is_object())
			o.compileReport = &report;
		std::ifstream m(folder / "map.json", std::ios::binary);
		const nlohmann::json mj = nlohmann::json::parse(m, nullptr, false);
		if (mj.is_object())
			o.seaLevel = MapWater::SeaLevelOf(mj);
		if (mj.is_object() && mj.contains("bases") && mj["bases"].is_object())
			o.fairnessRadiusM = mj["bases"].value("fairnessRadiusM", o.fairnessRadiusM);
	}
	Server server;
	server.loadSaveGame(level.c_str());
	if (!server.terrain || !server.gameSet || !server.getLevel()) {
		error = "the level has no map or game set";
		return false;
	}
	Grids g;
	out = Run(server, o, &g);
	out["map"] = map;
	out["level"] = level;
	if (cameras)
		*cameras = Cameras(*server.terrain, g, out);
	g.minimap = server.terrain->drawMinimap();
	if (grids)
		*grids = std::move(g);
	return true;
}

json Cameras(const Terrain& t, const Grids& g, const json& report)
{
	const int E = t.edge;
	const float pwM = g.pw * TILE, phM = g.ph * TILE;
	auto ground = [&](float x, float z) {
		float h = t.getHeight(std::clamp(x, 0.0f, pwM - 0.1f), std::clamp(z, 0.0f, phM - 0.1f));
		const TerrainTile* tile = t.getPlayableTile(std::clamp((int)(x / TILE), 0, g.pw - 1), std::clamp((int)(z / TILE), 0, g.ph - 1));
		if (tile && tile->fullOfWater) h = std::max(h, tile->waterLevel);
		return h;
	};
	// the highest ground (or water) within r of a point, on the whole map's heights
	auto highest = [&](float x, float z, float r) {
		float best = -1e30f;
		for (float dz = -r; dz <= r; dz += 10)
			for (float dx = -r; dx <= r; dx += 10)
				if (dx * dx + dz * dz <= r * r) {
					const int vx = std::clamp((int)std::lround((x + dx) / TILE) + E, 0, t.width), vz = std::clamp((int)std::lround((z + dz) / TILE) + E, 0, t.height);
					best = std::max(best, t.getVertex(vx, vz));
				}
		return std::max(best, ground(x, z));
	};
	json cams = json::array();
	auto add = [&](const std::string& label, float ex, float ez, float lx, float lz, float above) {
		ex = std::clamp(ex, -100.0f, pwM + 100), ez = std::clamp(ez, -100.0f, phM + 100);
		const float y = highest(ex, ez, 150) + above;
		const float ly = ground(std::clamp(lx, 0.0f, pwM), std::clamp(lz, 0.0f, phM));
		const float dx = lx - ex, dz = lz - ez, dy = ly - y;
		const float yaw = std::atan2(-dx, dz), pitch = std::atan2(dy, std::hypot(dx, dz));
		cams.push_back({ { "label", label }, { "camera", { std::round(ex * 10) / 10, std::round(y * 10) / 10, std::round(ez * 10) / 10,
			std::round(pitch * 1000) / 1000, std::round(yaw * 1000) / 1000 } }, { "looksAt", Pt(lx, lz) } });
	};
	const float cx = pwM / 2, cz = phM / 2;
	// each start, from 170 m behind it on the side away from the map's middle
	for (const auto& s : g.starts) {
		const float d = std::max(1.0f, std::hypot(s.x - cx, s.z - cz));
		add(s.who, s.x + (s.x - cx) / d * 170, s.z + (s.z - cz) / d * 170, s.x, s.z, 55);
	}
	// the faults with places (errors first), 100 m apart, at most 6
	std::vector<std::pair<float, float>> seen;
	for (const char* sev : { "error", "warning" })
		for (const json& p : report.value("problems", json::array())) {
			if (p.value("severity", std::string()) != sev || !p["at"].is_array() || seen.size() >= 6) continue;
			const float x = p["at"][0].get<float>(), z = p["at"][1].get<float>();
			bool near = false;
			for (auto& [sx, sz] : seen) near |= std::hypot(sx - x, sz - z) < 100;
			if (near) continue;
			seen.push_back({ x, z });
			// from 140 m towards the map's middle (so the camera stays over the map)
			const float d = std::max(1.0f, std::hypot(cx - x, cz - z));
			add("problem " + std::to_string(p.value("id", 0)) + " (" + p.value("check", std::string()) + ")", x + (cx - x) / d * 140, z + (cz - z) / d * 140, x, z, 45);
		}
	// one wide shot per side towards the far land: from a third of the way in, over the edge
	add("wide north", cx, phM * 0.62f, cx, phM + 300, 140);
	add("wide south", cx, phM * 0.38f, cx, -300, 140);
	add("wide east", pwM * 0.62f, cz, pwM + 300, cz, 140);
	add("wide west", pwM * 0.38f, cz, -300, cz, 140);
	return cams;
}

namespace {
	struct Picture {
		int w = 0, h = 0, ppt = 1, E = 0, H = 0;
		std::vector<uint8_t> rgb;
		void px(int x, int y, const uint8_t* c)
		{
			if (x < 0 || y < 0 || x >= w || y >= h) return;
			uint8_t* p = &rgb[((size_t)y * w + x) * 3];
			p[0] = c[0], p[1] = c[1], p[2] = c[2];
		}
		void toPx(float wx, float wz, int& x, int& y) const
		{
			x = (int)((wx / TILE + E) * ppt);
			y = (int)((H - E - wz / TILE) * ppt);
		}
		void dot(float wx, float wz, int r, const uint8_t* c)
		{
			int x, y;
			toPx(wx, wz, x, y);
			for (int dy = -r; dy <= r; dy++)
				for (int dx = -r; dx <= r; dx++)
					if (dx * dx + dy * dy <= r * r) px(x + dx, y + dy, c);
		}
		void ring(float wx, float wz, int r, int thick, const uint8_t* c)
		{
			int x, y;
			toPx(wx, wz, x, y);
			for (int dy = -r - thick; dy <= r + thick; dy++)
				for (int dx = -r - thick; dx <= r + thick; dx++) {
					const float d = std::sqrt((float)(dx * dx + dy * dy));
					if (d >= r - thick && d <= r + thick) px(x + dx, y + dy, c);
				}
		}
		void line(float x0, float z0, float x1, float z1, int r, const uint8_t* c)
		{
			int ax, ay, bx, by;
			toPx(x0, z0, ax, ay);
			toPx(x1, z1, bx, by);
			const int steps = std::max({ std::abs(bx - ax), std::abs(by - ay), 1 });
			for (int i = 0; i <= steps; i++) {
				const int x = ax + (bx - ax) * i / steps, y = ay + (by - ay) * i / steps;
				for (int dy = -r; dy <= r; dy++)
					for (int dx = -r; dx <= r; dx++) px(x + dx, y + dy, c);
			}
		}
		void dim(float k)
		{
			for (uint8_t& v : rgb) v = (uint8_t)(v * k + 255 * (1 - k) * 0.15f);
		}
		bool save(const fs::path& p) const
		{
			std::string out;
			auto write = [](void* ctx, void* data, int size) { ((std::string*)ctx)->append((const char*)data, size); };
			if (!stbi_write_png_to_func(write, &out, w, h, 3, rgb.data(), w * 3)) return false;
			std::ofstream f(p, std::ios::binary);
			f.write(out.data(), (std::streamsize)out.size());
			return (bool)f;
		}
	};
	const uint8_t PLAYER_COLOURS[9][3] = { { 200, 200, 200 }, { 220, 40, 40 }, { 40, 90, 230 }, { 240, 220, 40 }, { 40, 200, 70 },
		{ 230, 130, 30 }, { 160, 60, 200 }, { 40, 210, 210 }, { 250, 120, 190 } };
}

bool Preview(const std::string& map, const std::string& dir, json& out, std::string& error)
{
	const fs::path d = PathFromUtf8(dir);
	std::error_code ec;
	fs::create_directories(d, ec);
	// the relief with the objects: describe_map's picture
	json desc;
	const fs::path reliefPng = d / "relief.png";
	if (!MapAuthoring::Describe(map, PathToUtf8(reliefPng), desc, error))
		return false;
	Grids g;
	json cams;
	json report;
	if (!Check(map, Options(), report, error, &g, &cams))
		return false;
	int iw = 0, ih = 0, n = 0;
	unsigned char* base = stbi_load(PathToUtf8(reliefPng).c_str(), &iw, &ih, &n, 3);
	if (!base) {
		error = "the relief picture can't be read";
		return false;
	}
	const int W = desc["size"]["tiles"][0].get<int>(), Ht = desc["size"]["tiles"][1].get<int>(), E = desc["size"]["edge"].get<int>();
	Picture proto;
	proto.w = iw, proto.h = ih, proto.E = E, proto.H = Ht;
	proto.ppt = std::max(1, iw / std::max(W, 1));
	proto.rgb.assign(base, base + (size_t)iw * ih * 3);
	stbi_image_free(base);
	json files = json::object();
	files["relief"] = PathToUtf8(reliefPng);

	// passability: the main land light, other land orange, slopes dark grey, blocking terrain
	// brown, water blue; the starts, the ways between them and their choke points
	{
		Picture p = proto;
		const uint8_t mainC[3] = { 205, 215, 190 }, islandC[3] = { 240, 150, 40 }, slopeC[3] = { 70, 70, 70 }, terrC[3] = { 120, 80, 50 }, waterC[3] = { 60, 110, 200 };
		for (int z = 0; z < g.ph; z++)
			for (int x = 0; x < g.pw; x++) {
				const size_t i = (size_t)z * g.pw + x;
				const uint8_t* c = g.blocked[i] == 1 ? slopeC : g.blocked[i] == 2 ? terrC : g.blocked[i] == 3 ? waterC : g.region[i] == g.mainRegion ? mainC : islandC;
				for (int dy = 0; dy < p.ppt; dy++)
					for (int dx = 0; dx < p.ppt; dx++)
						p.px((x + E) * p.ppt + dx, (Ht - E - 1 - z) * p.ppt + dy, c);
			}
		const uint8_t pathC[3] = { 255, 255, 255 }, chokeC[3] = { 255, 0, 200 };
		for (const auto& path : g.paths)
			for (size_t k = 1; k < path.size(); k++)
				p.line(path[k - 1].first * TILE + 2.5f, path[k - 1].second * TILE + 2.5f, path[k].first * TILE + 2.5f, path[k].second * TILE + 2.5f, 0, pathC);
		for (const auto& [x, z] : g.chokes) p.ring(x, z, 3 * p.ppt, 1, chokeC);
		for (const auto& s : g.starts) p.dot(s.x, s.z, 3 * p.ppt, PLAYER_COLOURS[std::clamp(s.colour, 0, 8)]);
		p.save(d / "passability.png");
		files["passability"] = PathToUtf8(d / "passability.png");
	}
	// water: the relief greyed, the water by depth, the waterways with arrows downstream, the falls
	{
		Picture p = proto;
		for (size_t i = 0; i < p.rgb.size(); i += 3) {
			const uint8_t gry = (uint8_t)((p.rgb[i] + p.rgb[i + 1] + p.rgb[i + 2]) / 3);
			const bool blue = p.rgb[i + 2] > p.rgb[i] + 30 && p.rgb[i + 2] > p.rgb[i + 1];
			if (!blue) p.rgb[i] = p.rgb[i + 1] = p.rgb[i + 2] = (uint8_t)(gry * 0.8f + 30);
		}
		json design = desc.contains("water") && desc["water"].is_object() ? desc["water"].value("design", json::object()) : json::object();
		if (!design.is_object()) design = json::object();
		const uint8_t wayC[3] = { 0, 230, 255 }, arrowC[3] = { 255, 255, 255 }, fallC[3] = { 255, 60, 60 };
		for (const json& w : design.value("waterways", json::array())) {
			const json& pts = w["points"];
			for (size_t k = 1; k < pts.size(); k++) {
				const float x0 = pts[k - 1][0].get<float>(), z0 = pts[k - 1][1].get<float>(), x1 = pts[k][0].get<float>(), z1 = pts[k][1].get<float>();
				p.line(x0, z0, x1, z1, std::max(0, p.ppt / 2), wayC);
				// an arrowhead every segment's middle, pointing downstream (the points' order)
				const float mx = (x0 + x1) / 2, mz = (z0 + z1) / 2, len = std::max(1.0f, std::hypot(x1 - x0, z1 - z0));
				const float ux = (x1 - x0) / len, uz = (z1 - z0) / len, s = 12;
				p.line(mx, mz, mx - ux * s - uz * s * 0.6f, mz - uz * s + ux * s * 0.6f, 0, arrowC);
				p.line(mx, mz, mx - ux * s + uz * s * 0.6f, mz - uz * s - ux * s * 0.6f, 0, arrowC);
			}
		}
		const json falls = report.contains("water") && report["water"].is_object() ? report["water"].value("waterfalls", json::array()) : json::array();
		for (const json& f : falls)
			if (f.contains("at") && f["at"].is_array())
				p.dot(f["at"][0].get<float>(), f["at"][1].get<float>(), 2 * p.ppt, fallC);
		p.save(d / "water.png");
		files["water"] = PathToUtf8(d / "water.png");
	}
	// fairness: the radius round each base, the resources inside (from describe's picture), the
	// nearest rival's way
	{
		Picture p = proto;
		p.dim(0.75f);
		const float radius = report.contains("fairness") && report["fairness"].is_object() ? report["fairness"].value("radiusM", 200.0f) : 200.0f;
		for (const auto& s : g.starts) {
			const uint8_t* c = PLAYER_COLOURS[std::clamp(s.colour, 0, 8)];
			p.ring(s.x, s.z, (int)(radius / TILE * p.ppt), 1, c);
			p.dot(s.x, s.z, 3 * p.ppt, c);
		}
		const uint8_t pathC[3] = { 255, 255, 255 };
		for (const auto& path : g.paths)
			for (size_t k = 1; k < path.size(); k++)
				p.line(path[k - 1].first * TILE + 2.5f, path[k - 1].second * TILE + 2.5f, path[k].first * TILE + 2.5f, path[k].second * TILE + 2.5f, 0, pathC);
		p.save(d / "fairness.png");
		files["fairness"] = PathToUtf8(d / "fairness.png");
	}
	// problems: every fault with a place, red (errors), orange (warnings), blue (info)
	{
		Picture p = proto;
		p.dim(0.6f);
		const uint8_t errC[3] = { 255, 30, 30 }, warnC[3] = { 255, 160, 0 }, infoC[3] = { 80, 160, 255 };
		json legend = json::array();
		for (const char* sev : { "info", "warning", "error" })
			for (const json& pr : report["problems"]) {
				if (pr.value("severity", std::string()) != sev || !pr["at"].is_array()) continue;
				const uint8_t* c = std::string(sev) == "error" ? errC : std::string(sev) == "warning" ? warnC : infoC;
				const float x = pr["at"][0].get<float>(), z = pr["at"][1].get<float>();
				p.ring(x, z, std::max(4, 3 * p.ppt), 1, c);
				p.dot(x, z, 1, c);
				int px, py;
				p.toPx(x, z, px, py);
				legend.push_back({ { "id", pr["id"] }, { "pixel", { px, py } } });
			}
		p.save(d / "problems.png");
		files["problems"] = PathToUtf8(d / "problems.png");
		out["problemPixels"] = legend;
	}
	// the minimap as the BCM holds it (drawn from the terrain), and a folder's material map
	{
		if (MapFolder::IsMapPath(map)) {
			const fs::path folder = MapFolder::Resolve(map);
			if (fs::exists(folder / "build" / "materials.png", ec)) {
				fs::copy_file(folder / "build" / "materials.png", d / "materials.png", fs::copy_options::overwrite_existing, ec);
				files["materials"] = PathToUtf8(d / "materials.png");
			}
		}
		if (g.minimap.size() == 128 * 128 * 3) {
			Picture p;
			p.w = p.h = 128;
			p.rgb = g.minimap;
			p.save(d / "minimap.png");
			files["minimap"] = PathToUtf8(d / "minimap.png");
		}
	}
	std::ofstream cf(d / "cameras.json", std::ios::binary);
	cf << cams.dump(1) << "\n";
	files["cameras"] = PathToUtf8(d / "cameras.json");
	out["format"] = "wkbai map preview";
	out["files"] = files;
	out["cameras"] = cams;
	out["summary"] = report["summary"];
	out["legend"] = { { "relief", "describe_map's picture: relief, water, objects (buildings in the player's colour, trees dark green, resources yellow)" },
		{ "passability", "main land light, land no unit reaches from it orange, too steep dark grey, blocking terrain brown, water blue; white: the ways by land between the starts; magenta rings: their narrowest points; dots: the starts" },
		{ "water", "the land grey, the water blue; cyan lines: waterways with white arrows downstream; red dots: the falls the renderer draws" },
		{ "fairness", "a ring of the fairness radius round each base in its colour, the ways between the starts in white" },
		{ "problems", "red rings: errors, orange: warnings, blue: info (problemPixels gives each problem's pixel)" },
		{ "minimap", "the minimap as the BCM holds it" }, { "materials", "the compiler's material map (a folder with ground rules)" },
		{ "cameras", "known-good cameras (y above the highest ground within 150 m) for in-game shots: the benchmark's cameras, [x, y, z, pitch, yaw]" } };
	return true;
}

int RunCommandLine(const std::vector<std::string>& args)
{
	LoadBCP("data.bcp");
	std::string map, outFile, png, dir;
	for (size_t i = 1; i < args.size(); i++) {
		if (args[i] == "--out" && i + 1 < args.size()) outFile = args[++i];
		else if (args[i] == "--png" && i + 1 < args.size()) png = args[++i];
		else if (args[i] == "--dir" && i + 1 < args.size()) dir = args[++i];
		else if (map.empty()) map = args[i];
	}
	if (map.empty() || (args[0] == "preview" && dir.empty())) {
		printf("usage: wkbai --author check <folder.wkbai | level> [--out <file.json>] [--png <problems.png>]\n"
			"       wkbai --author preview <folder.wkbai | level> --dir <folder> [--out <file.json>]\n");
		return 2;
	}
	json out;
	std::string error;
	bool ok;
	if (args[0] == "preview" || !png.empty()) {
		const std::string pdir = !dir.empty() ? dir : PathToUtf8(fs::path(PathFromUtf8(png)).parent_path() / "preview");
		json pv;
		ok = Preview(map, pdir, pv, error);
		if (ok && args[0] == "check") {
			ok = Check(map, Options(), out, error);
			std::error_code ec;
			fs::copy_file(PathFromUtf8(pdir) / "problems.png", PathFromUtf8(png), fs::copy_options::overwrite_existing, ec);
		}
		else
			out = pv;
	}
	else
		ok = Check(map, Options(), out, error);
	if (!ok) {
		printf("AUTHOR failed: %s\n", error.c_str());
		return 1;
	}
	const std::string text = out.dump(1);
	if (!outFile.empty()) {
		std::ofstream f(PathFromUtf8(outFile), std::ios::binary);
		f << text << "\n";
	}
	else
		printf("AUTHOR-JSON-BEGIN\n%s\nAUTHOR-JSON-END\n", text.c_str());
	const json& s = out["summary"];
	printf("AUTHOR ok, %s: %d errors, %d warnings, %d info%s%s\n", args[0].c_str(), s.value("errors", 0), s.value("warnings", 0), s.value("info", 0),
		outFile.empty() ? "" : ", ", outFile.c_str());
	fflush(stdout);
	return 0;
}

} // namespace MapChecks
