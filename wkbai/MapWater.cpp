// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#include "MapWater.h"
#include "terrain.h"
#include "WaterDesign.h"
#include "gfx/DiligentWaterfalls.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

using json = nlohmann::json;

namespace {
	std::string Num(float v)
	{
		char b[32];
		snprintf(b, sizeof(b), "%.0f", v);
		return b;
	}
	std::string Num1(float v)
	{
		char b[32];
		snprintf(b, sizeof(b), "%.1f", v);
		return b;
	}
	// a tile's centre in playable metres, as text and as JSON
	std::string Place(const Terrain& t, int x, int z)
	{
		return "(" + Num((x - t.edge + 0.5f) * 5.0f) + ", " + Num((z - t.edge + 0.5f) * 5.0f) + ")";
	}
	json At(const Terrain& t, int x, int z) { return { std::round((x - t.edge + 0.5f) * 50.0f) / 10.0f, std::round((z - t.edge + 0.5f) * 50.0f) / 10.0f }; }
	float Bed(const Terrain& t, int x, int z)
	{
		return std::min({ t.getVertex(x, z), t.getVertex(x + 1, z), t.getVertex(x, z + 1), t.getVertex(x + 1, z + 1) });
	}
	float Mean(const Terrain& t, int x, int z)
	{
		return 0.25f * (t.getVertex(x, z) + t.getVertex(x + 1, z) + t.getVertex(x, z + 1) + t.getVertex(x + 1, z + 1));
	}
	const char* Compass(float dx, float dz)
	{
		static const char* names[8] = { "east", "north-east", "north", "north-west", "west", "south-west", "south", "south-east" };
		float a = std::atan2(dz, dx) * 57.2957795f;
		if (a < 0)
			a += 360.0f;
		return names[(int)std::lround(a / 45.0f) % 8];
	}
}

float MapWater::SeaLevelOf(const json& mj)
{
	if (!mj.is_object() || !mj.contains("terrain") || !mj["terrain"].is_object())
		return -1e30f;
	const json& t = mj["terrain"];
	if (t.contains("generated") && t["generated"].is_object() && t["generated"].contains("seaLevel") && t["generated"]["seaLevel"].is_number())
		return t["generated"]["seaLevel"].get<float>();
	if (t.contains("seaLevel") && t["seaLevel"].is_number())
		return t["seaLevel"].get<float>();
	return -1e30f;
}

json MapWater::Check(const Terrain& t, float seaLevel, std::vector<std::string>& warnings)
{
	const int W = t.width, H = t.height, E = t.edge;
	const int playable = std::max(1, (W - 2 * E) * (H - 2 * E));
	const WaterDesign* d = t.waterDesign.get();
	json out;
	int faults = 0;
	auto warn = [&](const std::string& s) {
		warnings.push_back(s);
		faults++;
	};

	// ---- the lakes: each point's own flood (its level's tiles joined to its seed, not under a
	// sloped river)
	// the design's painted standing water (lakes and seas), and how far each tile is from it (to 3)
	std::vector<uint8_t> nearPaint((size_t)W * H, 255);
	bool painted = false;
	if (d && d->width == W && d->height == H) {
		std::vector<int> q;
		for (int i = 0; i < W * H; i++) {
			const char c = d->surface[i];
			if (c == WaterDesign::LAKE || WaterDesign::seaRegionOf(c) >= 0) {
				nearPaint[i] = 0;
				q.push_back(i);
				painted = true;
			}
		}
		for (size_t k = 0; k < q.size(); k++) {
			const int i = q[k], x = i % W, z = i / W;
			if (nearPaint[i] >= 3)
				continue;
			for (int dz = -1; dz <= 1; dz++)
				for (int dx = -1; dx <= 1; dx++) {
					const int ax = x + dx, az = z + dz;
					if (ax < 0 || az < 0 || ax >= W || az >= H || nearPaint[(size_t)az * W + ax] <= nearPaint[i] + 1)
						continue;
					nearPaint[(size_t)az * W + ax] = (uint8_t)(nearPaint[i] + 1);
					q.push_back(az * W + ax);
				}
		}
	}
	json lakes = json::array();
	std::vector<int> owner((size_t)W * H, -1); // the lake point whose flood a tile is
	int leaking = 0, puddles = 0;
	for (size_t li = 0; li < t.lakes.size(); li++) {
		const Vector3& L = t.lakes[li];
		const int sx = (int)(L.x / 5.0f), sz = (int)(L.z / 5.0f);
		json lj = { { "index", li }, { "at", { std::round((L.x - E * 5.0f) * 10) / 10, std::round((L.z - E * 5.0f) * 10) / 10 } }, { "levelM", L.y } };
		const TerrainTile* seed = t.getTile(sx, sz);
		if (!seed || !seed->fullOfWater || seed->slopedWater || std::abs(seed->waterLevel - L.y) > 1e-3f) {
			// (dry: the compiler's own warning; under a higher lake's water: drowned, below)
			lj["tiles"] = 0;
			lj["state"] = !seed ? "off the map" : !seed->fullOfWater ? "dry" : "under other water";
			if (seed && seed->fullOfWater && !seed->slopedWater && seed->waterLevel > L.y + 1e-3f) {
				const int o = owner[(size_t)sz * W + sx];
				lj["state"] = "drowned";
				warn("water: lake " + std::to_string(li) + " at " + Place(t, sx, sz) + " (level " + Num1(L.y) + ") is drowned by "
					+ (o >= 0 ? "lake " + std::to_string(o) : std::string("higher water")) + " at level " + Num1(seed->waterLevel)
					+ ": the higher lake leaks into its basin");
			}
			lakes.push_back(lj);
			continue;
		}
		std::vector<int> flood = { sz * W + sx };
		std::vector<uint8_t> seen((size_t)W * H, 0);
		seen[flood[0]] = 1;
		for (size_t k = 0; k < flood.size(); k++) {
			const int i = flood[k], x = i % W, z = i / W;
			for (const auto& [dx, dz] : { std::pair{ 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } }) {
				const int ax = x + dx, az = z + dz;
				if (ax < 0 || az < 0 || ax >= W || az >= H || seen[(size_t)az * W + ax])
					continue;
				const TerrainTile* n = t.getTile(ax, az);
				if (!n->fullOfWater || n->slopedWater || std::abs(n->waterLevel - L.y) > 1e-3f)
					continue;
				seen[(size_t)az * W + ax] = 1;
				flood.push_back(az * W + ax);
			}
		}
		double depthSum = 0;
		float depthMax = 0;
		int borderTile = -1, playableTiles = 0, outside = 0, firstOutside = -1, paintedSea = 0, paintedIn = 0;
		for (int i : flood) {
			const int x = i % W, z = i / W;
			const float dep = L.y - Mean(t, x, z);
			depthSum += std::max(dep, 0.0f);
			depthMax = std::max(depthMax, L.y - Bed(t, x, z));
			if (borderTile < 0 && (x == 0 || z == 0 || x == W - 1 || z == H - 1))
				borderTile = i;
			playableTiles += x >= E && z >= E && x < W - E && z < H - E;
			if (owner[i] < 0)
				owner[i] = (int)li;
			if (painted) {
				const char c = d->surface[i];
				paintedSea += WaterDesign::seaRegionOf(c) >= 0;
				paintedIn += nearPaint[i] == 0;
				if (nearPaint[i] > 2) {
					if (firstOutside < 0)
						firstOutside = i;
					outside++;
				}
			}
		}
		const int n = (int)flood.size();
		const float meanDepth = (float)(depthSum / n);
		// the sea: at the map's sea level, painted as a sea, or (without either) reaching the map's
		// edge over a fifth of it
		const bool atSea = seaLevel > -1e29f && std::abs(L.y - seaLevel) < 0.05f;
		const bool sea = atSea || paintedSea * 2 > paintedIn || (seaLevel < -1e29f && !painted && borderTile >= 0 && playableTiles * 5 > playable);
		lj["kind"] = sea ? "sea" : "lake";
		lj["tiles"] = n;
		lj["playableShare"] = std::round(1000.0 * playableTiles / playable) / 1000.0;
		lj["meanDepthM"] = std::round(meanDepth * 100) / 100;
		lj["maxDepthM"] = std::round(depthMax * 100) / 100;
		json leaks = json::array();
		const std::string name = "water: lake " + std::to_string(li) + " at " + Place(t, sx, sz) + " (level " + Num1(L.y) + ")";
		if (!sea) {
			if (borderTile >= 0) {
				leaks.push_back({ { "kind", "off the map" }, { "at", At(t, borderTile % W, borderTile / W) } });
				warn(name + " leaks off the map's edge at " + Place(t, borderTile % W, borderTile / W) + ": its rim is lower there (or make it the sea)");
			}
			if (painted && paintedIn > 0 && outside > std::max(8, n / 20)) {
				leaks.push_back({ { "kind", "past the painted lake" }, { "tiles", outside }, { "at", At(t, firstOutside % W, firstOutside / W) } });
				warn(name + " floods " + std::to_string(outside) + " tiles beyond the lake the design paints, first at "
					+ Place(t, firstOutside % W, firstOutside / W) + ": it leaks into the next valley there");
			}
			if (playableTiles * 2 > playable) {
				leaks.push_back({ { "kind", "most of the map" }, { "share", (double)playableTiles / playable } });
				warn(name + " floods " + Num(100.0f * playableTiles / playable) + "% of the map: its basin leaks (or it is meant as the sea: paint it as one)");
			}
			// a puddle: shallow all over (a lake on flat ground)
			if (n >= 12 && depthMax < 1.0f && meanDepth < 0.5f) {
				lj["puddle"] = true;
				puddles++;
				warn(name + " is a puddle on flat ground: " + std::to_string(n) + " tiles, " + Num1(meanDepth) + " m deep on the mean, "
					+ Num1(depthMax) + " m at most (dig a basin, or lower its level)");
			}
		}
		if (!leaks.empty())
			leaking++;
		lj["leaks"] = leaks;
		lakes.push_back(lj);
	}
	// lakes whose seed lies in another (higher) lake's flood were reported above; a lower lake's
	// seed covered at its own level by a higher one isn't possible (the higher wins the tile)
	out["lakes"] = lakes;

	// ---- the waterways: under water along their course, rising, speed
	json ways = json::array();
	int dryWays = 0, risingWays = 0;
	if (d) {
		for (size_t wi = 0; wi < d->waterways.size(); wi++) {
			const WaterDesign::Waterway& w = d->waterways[wi];
			int samples = 0, wet = 0, risingAt = -1;
			float worstRise = 0, lowSoFar = 1e30f, length = 0;
			int dryRun = 0, longestDry = 0, dryFrom = -1, longestDryAt = -1;
			int lastTile = -1;
			for (size_t i = 0; i + 1 < w.points.size(); i++) {
				const Vector3 a = w.points[i], b = w.points[i + 1];
				const float len = std::hypot(b.x - a.x, b.z - a.z);
				length += len;
				const int steps = std::max(1, (int)std::ceil(len / 2.5f));
				for (int s = 0; s < steps; s++) {
					const float k = (float)s / steps;
					const int tx = (int)std::floor((a.x + (b.x - a.x) * k) / 5.0f) + E, tz = (int)std::floor((a.z + (b.z - a.z) * k) / 5.0f) + E;
					const TerrainTile* tile = t.getTile(tx, tz);
					if (!tile || tz * W + tx == lastTile)
						continue;
					lastTile = tz * W + tx;
					samples++;
					const bool isWet = tile->fullOfWater;
					wet += isWet;
					// the course's surface: its water's level where it is wet, the bed where dry;
					// downstream it never rises (a dry stretch above the water before it: a dam)
					const float surface = isWet ? tile->waterLevel : Bed(t, tx, tz);
					if (lowSoFar < 1e29f && surface - lowSoFar > worstRise) {
						worstRise = surface - lowSoFar;
						risingAt = lastTile;
					}
					lowSoFar = std::min(lowSoFar, surface);
					if (!isWet) {
						if (dryRun++ == 0)
							dryFrom = lastTile;
						if (dryRun > longestDry) {
							longestDry = dryRun;
							longestDryAt = dryFrom;
						}
					}
					else
						dryRun = 0;
				}
			}
			const float wetShare = samples ? (float)wet / samples : 0.0f;
			json wj = { { "index", wi }, { "lengthM", std::round(length) }, { "widthM", w.width }, { "sloped", w.sloped() },
				{ "wetShare", std::round(wetShare * 1000) / 1000 } };
			if (!w.by.empty())
				wj["by"] = w.by;
			float smin = 1e30f, smax = 0;
			for (size_t i = 0; i < w.points.size(); i++) {
				const float s = w.speedAt((int)i, 0.0f);
				smin = std::min(smin, s);
				smax = std::max(smax, s);
			}
			wj["speedMs"] = w.hasSpeed() ? json({ { "from", smin }, { "to", smax }, { "perPoint", !w.speeds.empty() } }) : json("found from the land");
			const std::string name = "water: waterway " + std::to_string(wi) + (w.by.empty() ? "" : " (" + w.by + ")");
			if (wetShare < 0.5f) {
				wj["dry"] = true;
				dryWays++;
				warn(name + " stays dry over " + Num(100.0f * (1.0f - wetShare)) + "% of its course"
					+ (longestDryAt >= 0 ? ", the longest dry stretch from " + Place(t, longestDryAt % W, longestDryAt / W) : std::string())
					+ ": a painted river with no water (give it water: a sloped course, a lake, or \"water\": true in the terrain plan)");
			}
			else if (longestDry >= 6 && longestDryAt >= 0) {
				wj["dryStretch"] = { { "tiles", longestDry }, { "at", At(t, longestDryAt % W, longestDryAt / W) } };
				warn(name + " runs dry for " + std::to_string(longestDry) + " tiles from " + Place(t, longestDryAt % W, longestDryAt / W));
			}
			if (worstRise > 0.5f && risingAt >= 0) {
				wj["rises"] = { { "byM", std::round(worstRise * 100) / 100 }, { "at", At(t, risingAt % W, risingAt / W) } };
				risingWays++;
				warn(name + " rises " + Num1(worstRise) + " m downstream at " + Place(t, risingAt % W, risingAt / W)
					+ ": its bed climbs there (a dam across it, or it runs the wrong way)");
			}
			ways.push_back(wj);
		}
	}
	out["waterways"] = ways;

	// ---- the painted river tiles that stay dry
	int paintedRiver = 0, dryRiver = 0, dryX0 = W, dryZ0 = H, dryX1 = -1, dryZ1 = -1;
	if (d && d->width == W && d->height == H)
		for (int z = 0; z < H; z++)
			for (int x = 0; x < W; x++)
				if (d->surface[(size_t)z * W + x] == WaterDesign::RIVER) {
					paintedRiver++;
					if (!t.getTile(x, z)->fullOfWater) {
						dryRiver++;
						dryX0 = std::min(dryX0, x), dryZ0 = std::min(dryZ0, z), dryX1 = std::max(dryX1, x), dryZ1 = std::max(dryZ1, z);
					}
				}
	out["paintedRiverTiles"] = { { "tiles", paintedRiver }, { "dry", dryRiver } };
	if (dryRiver > 0) {
		out["paintedRiverTiles"]["dryFrom"] = At(t, dryX0, dryZ0);
		out["paintedRiverTiles"]["dryTo"] = At(t, dryX1, dryZ1);
		warn("water: " + std::to_string(dryRiver) + " of the " + std::to_string(paintedRiver) + " tiles the design paints as river stay dry, between "
			+ Place(t, dryX0, dryZ0) + " and " + Place(t, dryX1, dryZ1));
	}

	// ---- the falls the renderer will draw
	const std::vector<Waterfall> found = FindWaterfalls(&t);
	json falls = json::array();
	for (const Waterfall& f : found) {
		// (x0, z0 world: the playable area's)
		const float len = f.edges * 5.0f;
		const float cx = f.x0 + (f.dz != 0 ? len * 0.5f : 0.0f), cz = f.z0 + (f.dx != 0 ? len * 0.5f : 0.0f);
		float top = 0;
		for (float v : f.top)
			top += v;
		top = f.top.empty() ? 0 : top / f.top.size();
		falls.push_back({ { "at", { std::round(cx * 10) / 10, std::round(cz * 10) / 10 } }, { "falls", Compass((float)f.dx, (float)f.dz) },
			{ "widthM", len }, { "dropM", std::round((top - f.bottom) * 100) / 100 }, { "intoWater", f.intoWater } });
	}
	out["waterfalls"] = falls;
	// a sloped river's steep stretches: drawn as rapids (white water where SlopedRapids is half
	// or more, a foot with mist and spray where it eases), not as fall sheets; each stretch with
	// its place and drop
	json rapidsJ = json::array();
	if (d && d->hasSlopedWater()) {
		std::vector<float> r((size_t)W * H, 0.0f);
		for (int z = 0; z < H; z++)
			for (int x = 0; x < W; x++)
				if (t.getTile(x, z)->slopedWater) {
					// (not a channel's edge tile: its surface meets the bank there, steep across the
					// river, and the renderer's white water fades out over it)
					bool edgeTile = false;
					for (const auto& [dx, dz] : { std::pair{ 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } }) {
						const TerrainTile* n = t.getTile(x + dx, z + dz);
						edgeTile = edgeTile || !n || !n->fullOfWater;
					}
					if (!edgeTile)
						r[(size_t)z * W + x] = SlopedRapids(&t, x, z);
				}
		std::vector<uint8_t> seen((size_t)W * H, 0);
		for (int s = 0; s < W * H; s++) {
			if (seen[s] || r[s] < 0.5f)
				continue;
			std::vector<int> run = { s };
			seen[s] = 1;
			float hi = -1e30f, lo = 1e30f, most = 0;
			double sx = 0, sz = 0;
			for (size_t k = 0; k < run.size(); k++) {
				const int i = run[k], x = i % W, z = i / W;
				const float lv = t.getTile(x, z)->waterLevel;
				hi = std::max(hi, lv), lo = std::min(lo, lv), most = std::max(most, r[i]);
				sx += x, sz += z;
				for (int dz = -1; dz <= 1; dz++)
					for (int dx = -1; dx <= 1; dx++) {
						const int ax = x + dx, az = z + dz;
						if (ax < 0 || az < 0 || ax >= W || az >= H || seen[(size_t)az * W + ax] || r[(size_t)az * W + ax] < 0.5f)
							continue;
						seen[(size_t)az * W + ax] = 1;
						run.push_back(az * W + ax);
					}
			}
			const int cx = (int)std::lround(sx / run.size()), cz = (int)std::lround(sz / run.size());
			rapidsJ.push_back({ { "at", At(t, cx, cz) }, { "tiles", run.size() }, { "dropM", std::round((hi - lo) * 10) / 10 },
				{ "steepest", std::round(most * 100) / 100 } });
		}
	}
	out["rapids"] = rapidsJ;
	if (d)
		for (size_t pi = 0; pi < d->waterfalls.size(); pi++) {
			const WaterDesign::PlacedFall& p = d->waterfalls[pi];
			const float mx = (p.from.x + p.to.x) * 0.5f, mz = (p.from.z + p.to.z) * 0.5f;
			bool drawn = false;
			for (const json& f : falls)
				drawn = drawn || std::hypot(f["at"][0].get<float>() - mx, f["at"][1].get<float>() - mz) <= 12.5f + 0.5f * std::hypot(p.to.x - p.from.x, p.to.z - p.from.z);
			if (!drawn)
				warn("water: placed fall " + std::to_string(pi) + " at (" + Num(mx) + ", " + Num(mz) + ") draws nothing: no lower ground beside its lip");
		}

	// ---- the rivers leaving the map (Extend map's exits: runs of water along the map's border
	// narrower than 30 tiles and a third of the side) and the far-land hints
	json exits = json::array();
	struct Exit { float x, z; int side; };
	std::vector<Exit> exitList;
	for (int side = 0; side < 4; side++) {
		const int len = side < 2 ? W : H;
		int run = 0;
		for (int k = 0; k <= len; k++) {
			const TerrainTile* tile = nullptr;
			if (k < len)
				tile = side == 0 ? t.getTile(k, 0) : side == 1 ? t.getTile(k, H - 1) : side == 2 ? t.getTile(0, k) : t.getTile(W - 1, k);
			if (tile && tile->fullOfWater) {
				run++;
				continue;
			}
			if (run > 0 && run < std::min(30, len / 3)) {
				const float along = k - run * 0.5f;
				const float tx = side < 2 ? along : (side == 2 ? 0.0f : (float)W), tz = side < 2 ? (side == 0 ? 0.0f : (float)H) : along;
				const Exit e{ (tx - E) * 5.0f, (tz - E) * 5.0f, side };
				exitList.push_back(e);
				static const char* sides[4] = { "south", "north", "west", "east" };
				exits.push_back({ { "at", { std::round(e.x), std::round(e.z) } }, { "side", sides[side] }, { "widthTiles", run } });
			}
			run = 0;
		}
	}
	out["riverExits"] = exits;
	if (d && !d->farland.empty()) {
		json hints = json::array();
		for (size_t hi = 0; hi < d->farland.size(); hi++) {
			const WaterDesign::FarHint& h = d->farland[hi];
			json hj = { { "kind", WaterDesign::FarHint::kindName(h.kind) }, { "towardDeg", h.degrees }, { "spread", h.spread }, { "from", h.from }, { "weight", h.weight } };
			if (h.kind == WaterDesign::FarHint::RIVER) {
				// the exits it turns: the one nearest its point (within 60 m), else those on the
				// side it points to
				int turned = 0;
				const float ca = std::cos(h.degrees / 57.2957795f), sa = std::sin(h.degrees / 57.2957795f);
				for (const Exit& e : exitList) {
					static const float ox[4] = { 0, 0, -1, 1 }, oz[4] = { -1, 1, 0, 0 };
					if (h.hasAt ? std::hypot(e.x - h.at.x, e.z - h.at.z) <= 60.0f : ox[e.side] * ca + oz[e.side] * sa > 0.3f)
						turned++;
				}
				hj["exits"] = turned;
				if (!turned)
					warn("water: far-land hint " + std::to_string(hi) + " (a river " + Compass(std::cos(h.degrees / 57.2957795f), std::sin(h.degrees / 57.2957795f))
						+ (h.hasAt ? " from (" + Num(h.at.x) + ", " + Num(h.at.z) + ")" : std::string()) + ") finds no river leaving the map there");
			}
			if (h.kind == WaterDesign::FarHint::SEA)
				hj["seaLevel"] = h.level > -1e29f ? json(h.level) : seaLevel > -1e29f ? json(seaLevel) : json("the map's edge water, else below its lowest edge");
			hints.push_back(hj);
		}
		out["farland"] = hints;
	}

	out["summary"] = { { "lakes", t.lakes.size() }, { "leakingLakes", leaking }, { "puddles", puddles }, { "waterways", ways.size() },
		{ "dryWaterways", dryWays }, { "risingWaterways", risingWays }, { "dryRiverTiles", dryRiver }, { "waterfalls", falls.size() }, { "rapids", rapidsJ.size() },
		{ "riverExits", exits.size() }, { "faults", faults } };
	return out;
}
