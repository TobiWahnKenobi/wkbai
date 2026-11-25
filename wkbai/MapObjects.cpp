// wkbai - a map folder's objects by intent: bases and the placement checks (MapObjects.h)
// Licensed under the GNU General Public License 3

#include "MapObjects.h"
#include "gameset/gameset.h"
#include "gameset/GameObjBlueprint.h"
#include "gameset/Footprint.h"
#include "terrain.h"
#include "settings.h"
#include "tags.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <set>

namespace MapObjects {
namespace {
	constexpr float PI = 3.14159265f;
	std::string Trim(const std::string& line)
	{
		const size_t a = line.find_first_not_of(" \t\r\n");
		if (a == std::string::npos)
			return "";
		const size_t b = line.find_last_not_of(" \t\r\n");
		return line.substr(a, b - a + 1);
	}
	// "<CLASS> <id> "<blueprint>"" (as MapFolder's reader)
	bool ObjectHead(const std::string& t, int& cls, std::string& clsName, uint32_t& id, std::string& bp)
	{
		const size_t sp = t.find(' ');
		if (sp == std::string::npos)
			return false;
		clsName = t.substr(0, sp);
		cls = Tags::GAMEOBJCLASS_tagDict.getTagID(clsName.c_str());
		if (cls < 0)
			return false;
		char* end = nullptr;
		const unsigned long v = strtoul(t.c_str() + sp + 1, &end, 10);
		if (!end || end == t.c_str() + sp + 1 || *end != ' ')
			return false;
		const char* q = strchr(end, '"');
		const char* q2 = q ? strchr(q + 1, '"') : nullptr;
		if (!q2)
			return false;
		id = (uint32_t)v;
		bp.assign(q + 1, q2);
		return true;
	}
	std::string Num(float v)
	{
		char buf[32];
		snprintf(buf, sizeof(buf), "%.2f", v);
		return buf;
	}
	uint32_t Hash(uint32_t a, uint32_t b, uint32_t c)
	{
		uint64_t v = ((uint64_t)a << 32 | b) ^ ((uint64_t)c << 21) ^ 0x9E3779B97F4A7C15ull;
		v ^= v >> 30; v *= 0xBF58476D1CE4E5B9ull;
		v ^= v >> 27; v *= 0x94D049BB133111EBull;
		v ^= v >> 31;
		return (uint32_t)v;
	}
	bool IsCivic(const GameObjBlueprint* bp)
	{
		return bp->buildingType == Tags::BUILDINGTYPE_CIVIC || bp->buildingType == Tags::BUILDINGTYPE_CIVIC_CENTRE;
	}
	// the playable tiles a building's footprint takes
	std::vector<std::pair<int, int>> FootprintTiles(const GameObjBlueprint* bp, const Vector3& pos, const Vector3& ori)
	{
		std::vector<std::pair<int, int>> out;
		const Footprint* fp = bp->footprint;
		if (!fp)
			return out;
		const auto ro = fp->rotateOrigin(ori.y);
		const int ox = (int)((pos.x - ro.first) / 5.0f), oz = (int)((pos.z - ro.second) / 5.0f);
		for (const auto& to : fp->tiles) {
			const auto [rx, rz] = to.rotate(ori.y);
			out.emplace_back(ox + rx, oz + rz);
		}
		return out;
	}
}

const char* StatusName(StampdownPlan::Status s)
{
	using S = StampdownPlan::Status;
	switch (s) {
	case S::Valid: return "Valid";
	case S::InvalidNoExplanation: return "InvalidNoExplanation";
	case S::InvalidTerrain: return "InvalidTerrain (off the map)";
	case S::InvalidNotInCity: return "InvalidNotInCity";
	case S::InvalidOccupied: return "InvalidOccupied";
	case S::InvalidTerrainType: return "InvalidTerrainType";
	case S::InvalidTooSteep: return "InvalidTooSteep";
	case S::InvalidNeighbour: return "InvalidNeighbour";
	case S::InvalidNotConnected: return "InvalidNotConnected";
	case S::InvalidNotDetached: return "InvalidNotDetached";
	case S::InvalidGatehouseOverlap: return "InvalidGatehouseOverlap";
	case S::InvalidRuralInside: return "InvalidRuralInside";
	}
	return "?";
}

std::shared_ptr<const GameSet> LoadGameSet(const std::string& file)
{
	static std::mutex mutex;
	static std::map<std::pair<std::string, int>, std::shared_ptr<const GameSet>> cache;
	const int version = g_settings.value<int>("gameVersion", 0);
	std::lock_guard<std::mutex> lock(mutex);
	auto& slot = cache[{ file, version }];
	if (!slot) {
		try {
			slot = std::make_shared<GameSet>(file.c_str(), version);
		}
		catch (...) {
			slot = nullptr;
		}
	}
	return slot;
}

// ---- the light game state ----

PlacementState::PlacementState(std::shared_ptr<const GameSet> gs, Terrain* t) : CommonGameState(ProgramType::SERVER)
{
	gameSet = gs;
	terrain = t;
	const auto [pw, ph] = t->getNumPlayableTiles();
	tiles = std::make_unique<Tile[]>((size_t)pw * ph);
	const GameObjBlueprint* lbp = gs->findBlueprint(Tags::GAMEOBJCLASS_LEVEL, "Standard");
	if (!lbp && gs->objBlueprints[Tags::GAMEOBJCLASS_LEVEL].size() > 0)
		lbp = &gs->objBlueprints[Tags::GAMEOBJCLASS_LEVEL][0];
	if (lbp) {
		owned.push_back(std::make_unique<CommonGameObject>(ID_ARRAY_LIMIT + 1, lbp));
		level = owned.back().get();
		idmapSet(level->id, level);
	}
}

CommonGameObject* PlacementState::add(uint32_t id, const GameObjBlueprint* bp, CommonGameObject* parent, const Vector3& pos, const Vector3& ori)
{
	owned.push_back(std::make_unique<CommonGameObject>(id, bp));
	CommonGameObject* o = owned.back().get();
	o->parent = parent ? parent : level;
	if (o->parent)
		o->parent->children[bp].push_back(o);
	o->position = pos;
	o->orientation = ori;
	idmapSet(id, o);
	if (bp->bpClass == Tags::GAMEOBJCLASS_PLAYER)
		players[id] = o;
	const auto [pw, ph] = terrain->getNumPlayableTiles();
	const bool placed = bp->bpClass == Tags::GAMEOBJCLASS_BUILDING || bp->bpClass == Tags::GAMEOBJCLASS_CHARACTER
		|| bp->bpClass == Tags::GAMEOBJCLASS_CONTAINER || bp->bpClass == Tags::GAMEOBJCLASS_PROP || bp->bpClass == Tags::GAMEOBJCLASS_MARKER;
	if (placed) {
		const int tx = (int)(pos.x / 5.0f), tz = (int)(pos.z / 5.0f);
		if (pos.x >= 0 && pos.z >= 0 && tx < pw && tz < ph) {
			o->tileIndex = tz * pw + tx;
			tiles[o->tileIndex].objList.push_back(CmnGORef(id));
			tiles[o->tileIndex].objects.push_back(o);
		}
	}
	if (bp->bpClass == Tags::GAMEOBJCLASS_BUILDING)
		updateOccupiedTiles(o, pos, ori, pos, ori);
	return o;
}

void PlacementState::readWorld(const std::vector<std::string>& lines)
{
	struct Open { CommonGameObject* obj; std::string end; Vector3 pos, ori; std::vector<CommonGameObject::CityRectangle> rects; const GameObjBlueprint* bp; uint32_t id; std::string subtype; };
	std::vector<Open> stack;
	// objects are made at their end (their place is known then); children after their parent's
	// head, so a parent is made at its head with a place set later
	for (const std::string& raw : lines) {
		const std::string t = Trim(raw);
		if (t.empty())
			continue;
		int cls;
		std::string clsName, bp;
		uint32_t id;
		if (ObjectHead(t, cls, clsName, id, bp)) {
			const GameObjBlueprint* b = gameSet->findBlueprint(cls, bp);
			CommonGameObject* parent = level;
			for (auto it = stack.rbegin(); it != stack.rend(); ++it)
				if (it->obj) { parent = it->obj; break; }
			Open o{ nullptr, "END_" + clsName, Vector3(0, 0, 0), Vector3(0, 0, 0), {}, b, id, "" };
			// players and cities hold others: made now (no place needed)
			if (b && (cls == Tags::GAMEOBJCLASS_PLAYER || cls == Tags::GAMEOBJCLASS_CITY || cls == Tags::GAMEOBJCLASS_TOWN || cls == Tags::GAMEOBJCLASS_ARMY
				|| cls == Tags::GAMEOBJCLASS_FORMATION))
				o.obj = add(id, b, parent, Vector3(0, 0, 0), Vector3(0, 0, 0));
			else if (b) {
				// made at its end; a placeholder keeps the nesting
				o.obj = nullptr;
				o.bp = b;
			}
			stack.push_back(o);
			continue;
		}
		if (stack.empty())
			continue;
		Open& top = stack.back();
		if (t == top.end) {
			if (!top.obj && top.bp) {
				CommonGameObject* parent = level;
				for (auto it = stack.rbegin() + 1; it != stack.rend(); ++it)
					if (it->obj) { parent = it->obj; break; }
				add(top.id, top.bp, parent, top.pos, top.ori);
			}
			stack.pop_back();
			continue;
		}
		float a, b, c;
		int r0, r1, r2, r3;
		if (t.rfind("POSITION ", 0) == 0 && sscanf(t.c_str() + 9, "%f %f %f", &a, &b, &c) == 3)
			top.pos = Vector3(a, b, c);
		else if (t.rfind("ORIENTATION ", 0) == 0 && sscanf(t.c_str() + 12, "%f %f %f", &a, &b, &c) == 3)
			top.ori = Vector3(a, b, c);
		else if (t.rfind("RECTANGLE ", 0) == 0 && sscanf(t.c_str() + 10, "%d %d %d %d", &r0, &r1, &r2, &r3) == 4 && top.obj)
			top.obj->cityRectangles.push_back({ r0, r1, r2, r3 });
	}
}

// ---- sites ----

Site FindSite(PlacementState& s, CommonGameObject* player, const GameObjBlueprint* bp, const SiteQuery& q)
{
	Site best;
	const Footprint* fp = bp->footprint;
	if (!fp || !s.terrain)
		return best;
	const auto [pw, ph] = s.terrain->getNumPlayableTiles();
	struct Cand { int tx, tz; float score; };
	std::vector<Cand> cands;
	const int R = (int)std::ceil(q.rMax / 5.0f) + 1;
	const int cx = (int)(q.x / 5.0f), cz = (int)(q.z / 5.0f);
	for (int dz = -R; dz <= R; dz++)
		for (int dx = -R; dx <= R; dx++) {
			const int tx = cx + dx, tz = cz + dz;
			if (tx < 0 || tz < 0 || tx >= pw || tz >= ph)
				continue;
			const float x = tx * 5.0f + 2.5f, z = tz * 5.0f + 2.5f;
			const float d = std::sqrt((x - q.x) * (x - q.x) + (z - q.z) * (z - q.z));
			if (d < q.rMin || d > q.rMax)
				continue;
			float score = std::abs(d - q.prefDist);
			if (q.prefAngle < 1e29f && d > 1.0f) {
				float da = std::atan2(z - q.z, x - q.x) - q.prefAngle;
				while (da > PI) da -= 2 * PI;
				while (da < -PI) da += 2 * PI;
				score += std::abs(da) * std::max(d, 10.0f) * 0.5f;
			}
			score += (Hash((uint32_t)tx, (uint32_t)tz, 7) >> 8) * (1e-3f / 16777216.0f);
			cands.push_back({ tx, tz, score });
		}
	std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.score < b.score; });
	for (const Cand& c : cands) {
		const float x = c.tx * 5.0f + 2.5f, z = c.tz * 5.0f + 2.5f;
		// the quarter facing (faceX, faceZ) first: the model's +z turned by y is (-sin y, cos y)
		int q0 = 0;
		if (q.face) {
			const float fx = q.faceX - x, fz = q.faceZ - z;
			if (fx * fx + fz * fz > 1.0f)
				q0 = ((int)std::lround(std::atan2(-fx, fz) / (PI / 2)) % 4 + 4) % 4;
		}
		for (int k = 0; k < 4; k++) {
			const int qt = (q0 + (k == 0 ? 0 : k == 1 ? 1 : k == 2 ? 3 : 2)) % 4;
			const Vector3 ori = StampdownPlan::quarterTurn(Vector3(0, qt * (PI / 2), 0));
			const auto ro = fp->rotateOrigin(ori.y);
			// the footprint's corner on the tile grid, the building's place at its origin
			const float px = (int)((x - ro.first) / 5.0f) * 5.0f + ro.first, pz = (int)((z - ro.second) / 5.0f) * 5.0f + ro.second;
			const Vector3 pos(px, s.terrain->getHeight(px, pz), pz);
			if (q.keepFree) {
				bool clash = false;
				for (const auto& [ftx, ftz] : FootprintTiles(bp, pos, ori))
					if (ftx >= 0 && ftz >= 0 && ftx < pw && ftz < ph && (*q.keepFree)[(size_t)ftz * pw + ftx])
						clash = true;
				if (clash)
					continue;
			}
			StampdownPlan plan = StampdownPlan::getStampdownPlan(&s, player, bp, pos, ori);
			best.tried++;
			if (plan.status == StampdownPlan::Status::Valid) {
				best.found = true;
				best.pos = pos;
				best.ori = ori;
				best.plan = std::move(plan);
				return best;
			}
			if (best.firstRefusal == StampdownPlan::Status::Valid)
				best.firstRefusal = plan.status;
		}
	}
	return best;
}

CommonGameObject* PutBuilding(PlacementState& s, CommonGameObject* player, const GameObjBlueprint* bp, const Site& site, const std::function<uint32_t()>& newId,
	std::vector<CommonGameObject*>& made)
{
	CommonGameObject* parent = player;
	if (CommonGameObject* owner = site.plan.ownerObject.getFrom(&s))
		parent = owner;
	else if (site.plan.newOwnerBlueprint) {
		parent = s.add(newId(), site.plan.newOwnerBlueprint, player, Vector3(0, 0, 0), Vector3(0, 0, 0));
		made.push_back(parent);
	}
	if (parent->blueprint->bpClass == Tags::GAMEOBJCLASS_CITY && IsCivic(bp))
		parent->cityRectangles.push_back(site.plan.cityRectangle);
	CommonGameObject* b = s.add(newId(), bp, parent, site.pos, site.ori);
	made.push_back(b);
	// the ground levelled as Server::levelTerrainUnder does: every FLATTEN tile's four corners at
	// the building's height (its ground where it was placed)
	Terrain* t = s.terrain;
	if (const Footprint* fp = bp->footprint) {
		const auto ro = fp->rotateOrigin(site.ori.y);
		const int ox = (int)((site.pos.x - ro.first) / 5.0f), oz = (int)((site.pos.z - ro.second) / 5.0f);
		const float target = site.pos.y / t->scale;
		std::set<int> vertices;
		for (const auto& to : fp->tiles) {
			if (!to.flatten)
				continue;
			const auto [rx, rz] = to.rotate(site.ori.y);
			const int tx = ox + rx + t->edge, tz = oz + rz + t->edge;
			if (tx < 0 || tz < 0 || tx >= t->width || tz >= t->height)
				continue;
			for (int dz = 0; dz <= 1; dz++)
				for (int dx = 0; dx <= 1; dx++)
					vertices.insert((tz + dz) * (t->width + 1) + tx + dx);
		}
		for (int i : vertices)
			t->setHeightSteps(i, target);
	}
	return b;
}

// ---- the starts ----

std::vector<std::pair<float, float>> ChooseStarts(PlacementState& s, CommonGameObject* player, const GameObjBlueprint* centre,
	const std::vector<std::pair<float, float>>& fixed, int count, const json& opts, json& report, std::vector<std::string>& warnings)
{
	std::vector<std::pair<float, float>> out;
	if (count <= 0 || !centre || !centre->footprint)
		return out;
	const Terrain* t = s.terrain;
	const auto [pw, ph] = t->getNumPlayableTiles();
	const float W = pw * 5.0f, H = ph * 5.0f;
	const float edgeM = opts.value("edgeM", 80.0f), waterM = opts.value("waterM", 40.0f), maxRise = opts.value("maxRiseM", 6.0f),
		room = opts.value("room", 0.6f), minApart = opts.value("minApartM", 0.0f);
	// the distance to water per tile (a breadth-first pass in tiles)
	std::vector<int> wd((size_t)pw * ph, 1 << 20);
	std::vector<int> queue;
	for (int z = 0; z < ph; z++)
		for (int x = 0; x < pw; x++)
			if (const auto* tile = t->getPlayableTile(x, z); tile && tile->fullOfWater) {
				wd[(size_t)z * pw + x] = 0;
				queue.push_back(z * pw + x);
			}
	for (size_t qi = 0; qi < queue.size(); qi++) {
		const int i = queue[qi], x = i % pw, z = i / pw;
		const int n4[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
		for (const auto& d : n4) {
			const int nx = x + d[0], nz = z + d[1];
			if (nx < 0 || nz < 0 || nx >= pw || nz >= ph || wd[(size_t)nz * pw + nx] <= wd[i] + 1)
				continue;
			wd[(size_t)nz * pw + nx] = wd[i] + 1;
			queue.push_back(nz * pw + nx);
		}
	}
	// the civic centre's terrains (its first footprint tile's list), for the room round a site
	const Footprint::TileOffset& probe = centre->footprint->tiles.front();
	struct Cand { float x, z, room; };
	std::vector<Cand> cands;
	const float step = 20.0f;
	for (float z = edgeM; z <= H - edgeM; z += step)
		for (float x = edgeM; x <= W - edgeM; x += step) {
			const int tx = (int)(x / 5), tz = (int)(z / 5);
			if (wd[(size_t)tz * pw + tx] * 5.0f < waterM)
				continue;
			// flat: the rise within 25 m, and the share of tiles within 30 m a building could take
			float lo = 1e30f, hi = -1e30f;
			int good = 0, all = 0;
			for (int dz = -6; dz <= 6; dz++)
				for (int dx = -6; dx <= 6; dx++) {
					if (dx * dx + dz * dz > 36) continue;
					const int qx = tx + dx, qz = tz + dz;
					if (qx < 0 || qz < 0 || qx >= pw || qz >= ph) continue;
					const float a = t->getVertex(qx + t->edge, qz + t->edge), b = t->getVertex(qx + t->edge + 1, qz + t->edge),
						c = t->getVertex(qx + t->edge, qz + t->edge + 1), d = t->getVertex(qx + t->edge + 1, qz + t->edge + 1);
					if (dx * dx + dz * dz <= 25) { lo = std::min({ lo, a, b, c, d }); hi = std::max({ hi, a, b, c, d }); }
					all++;
					const auto* tile = t->getPlayableTile(qx, qz);
					if (tile && !tile->fullOfWater && std::max({ a, b, c, d }) - std::min({ a, b, c, d }) < 2.0f
						&& StampdownPlan::tileTerrainAllowed(&s, probe, qx, qz))
						good++;
				}
			if (hi - lo > maxRise || all == 0)
				continue;
			const float r = (float)good / all;
			if (r < room)
				continue;
			cands.push_back({ x, z, r });
		}
	report["candidates"] = cands.size();
	std::vector<std::pair<float, float>> chosen = fixed;
	for (int k = 0; k < count; k++) {
		// the farthest from those chosen (none yet: the farthest from the map's middle), the roomier
		// on a tie; its civic centre must pass the server's rules there
		bool found = false;
		while (!cands.empty()) {
			size_t bi = 0;
			float bs = -1e30f;
			for (size_t i = 0; i < cands.size(); i++) {
				float d = 1e30f;
				if (chosen.empty())
					d = std::hypot(cands[i].x - W / 2, cands[i].z - H / 2);
				for (const auto& [cx, cz] : chosen)
					d = std::min(d, std::hypot(cands[i].x - cx, cands[i].z - cz));
				const float sc = d + cands[i].room * 20.0f;
				if (sc > bs) { bs = sc; bi = i; }
			}
			const Cand c = cands[bi];
			cands.erase(cands.begin() + bi);
			SiteQuery q;
			q.x = c.x; q.z = c.z; q.rMax = 10; q.faceX = W / 2; q.faceZ = H / 2;
			const Site site = FindSite(s, player, centre, q);
			if (!site.found)
				continue;
			float nearest = 1e30f;
			for (const auto& [cx, cz] : chosen)
				nearest = std::min(nearest, std::hypot(c.x - cx, c.z - cz));
			if (minApart > 0 && nearest < minApart)
				warnings.push_back("bases: a chosen start (" + Num(c.x) + ", " + Num(c.z) + ") is " + Num(nearest) + " m from the next, under minApartM "
					+ Num(minApart));
			chosen.push_back({ c.x, c.z });
			out.push_back({ c.x, c.z });
			found = true;
			break;
		}
		if (!found) {
			warnings.push_back("bases: no site left for a start (flat, " + Num(edgeM) + " m from the edge, " + Num(waterM) + " m from water, room "
				+ Num(room) + ")");
			break;
		}
	}
	return out;
}

// ---- text ----

void ObjectText(const CommonGameObject* o, int depth, std::vector<std::string>& out)
{
	const std::string tabs(depth, '\t'), cls = Tags::GAMEOBJCLASS_tagDict.getStringFromID(o->blueprint->bpClass);
	out.push_back(tabs + cls + " " + std::to_string(o->id) + " \"" + o->blueprint->name + "\" \n");
	out.push_back(tabs + "\tPOSITION " + Num(o->position.x) + " " + Num(o->position.y) + " " + Num(o->position.z) + " \n");
	out.push_back(tabs + "\tORIENTATION " + Num(o->orientation.x) + " " + Num(o->orientation.y) + " " + Num(o->orientation.z) + " \n");
	for (const auto& r : o->cityRectangles)
		out.push_back(tabs + "\tRECTANGLE " + std::to_string(r.xStart) + " " + std::to_string(r.yStart) + " " + std::to_string(r.xEnd) + " "
			+ std::to_string(r.yEnd) + " \n");
	std::vector<const CommonGameObject*> kids;
	for (const auto& [bpi, list] : o->children)
		for (const CommonGameObject* c : list)
			kids.push_back(c);
	std::sort(kids.begin(), kids.end(), [](const CommonGameObject* a, const CommonGameObject* b) { return a->id < b->id; });
	for (const CommonGameObject* c : kids)
		ObjectText(c, depth + 1, out);
	out.push_back(tabs + "END_" + cls + " \n");
	out.push_back("\n");
}

// ---- checks ----

json CheckPlacement(CommonGameState* gs, std::vector<std::string>* warnings)
{
	json out;
	if (!gs || !gs->terrain || !gs->tiles || !gs->gameSet) {
		out["skipped"] = "no game state";
		return out;
	}
	const auto [pw, ph] = gs->terrain->getNumPlayableTiles();
	std::vector<CommonGameObject*> buildings, others;
	std::vector<CommonGameObject*> all;
	for (const auto& [id, o] : gs->idmap)
		if (o && !(o->flags & CommonGameObject::fTerminated))
			all.push_back(o);
	std::sort(all.begin(), all.end(), [](const CommonGameObject* a, const CommonGameObject* b) { return a->id < b->id; });
	for (CommonGameObject* o : all) {
		if (o->blueprint->bpClass == Tags::GAMEOBJCLASS_BUILDING)
			buildings.push_back(o);
		else if (o->blueprint->bpClass == Tags::GAMEOBJCLASS_CHARACTER || o->blueprint->bpClass == Tags::GAMEOBJCLASS_CONTAINER)
			others.push_back(o);
	}
	json failed = json::array(), byStatus = json::object(), byBlueprint = json::object();
	int passed = 0, noFootprint = 0;
	auto at = [](const CommonGameObject* o) { return json::array({ std::lround(o->position.x * 10) / 10.0, std::lround(o->position.z * 10) / 10.0 }); };
	for (CommonGameObject* b : buildings) {
		if (!b->blueprint->footprint) {
			noFootprint++;
			continue;
		}
		// its own tiles freed for the check, as before it was placed
		std::vector<std::pair<size_t, bool>> freed;
		for (const auto& [tx, tz] : FootprintTiles(b->blueprint, b->position, b->orientation))
			if (tx >= 0 && tz >= 0 && tx < pw && tz < ph) {
				auto& tile = gs->tiles[(size_t)tz * pw + tx];
				if (tile.building.objid == b->id) {
					freed.push_back({ (size_t)tz * pw + tx, tile.buildingPassable });
					tile.building = nullptr;
				}
			}
		const StampdownPlan plan = StampdownPlan::getStampdownPlan(gs, b->getPlayer(), b->blueprint, b->position, b->orientation);
		for (const auto& [i, passable] : freed) {
			gs->tiles[i].building = b->id;
			gs->tiles[i].buildingPassable = passable;
		}
		const std::string st = StatusName(plan.status);
		byStatus[st] = byStatus.value(st, 0) + 1;
		json& bb = byBlueprint[b->blueprint->name];
		if (bb.is_null()) bb = { { "count", 0 }, { "passed", 0 } };
		bb["count"] = bb["count"].get<int>() + 1;
		if (plan.status == StampdownPlan::Status::Valid) {
			passed++;
			bb["passed"] = bb["passed"].get<int>() + 1;
			continue;
		}
		const CommonGameObject* p = b->getPlayer();
		json f = { { "id", b->id }, { "blueprint", b->blueprint->name }, { "player", p ? p->id : 0 }, { "at", at(b) }, { "status", st } };
		json badTiles = json::array();
		for (const auto& v : plan.tiles)
			if (!v.ok) badTiles.push_back({ v.x * 5 + 2.5, v.z * 5 + 2.5 });
		if (!badTiles.empty()) f["refusedTilesAt"] = badTiles;
		if (failed.size() < 500) failed.push_back(f);
		if (warnings)
			warnings->push_back("placement: " + b->blueprint->name + " " + std::to_string(b->id) + " at (" + Num(b->position.x) + ", " + Num(b->position.z)
				+ ") fails the server's rules: " + st);
	}
	// characters on a building's blocked tile or in water; containers that can't be built upon on
	// a building's tile
	int charactersBlocked = 0, containersOnBuildings = 0;
	json blockedAt = json::array();
	for (CommonGameObject* o : others) {
		const int tx = (int)(o->position.x / 5.0f), tz = (int)(o->position.z / 5.0f);
		if (o->position.x < 0 || o->position.z < 0 || tx >= pw || tz >= ph)
			continue;
		const auto& tile = gs->tiles[(size_t)tz * pw + tx];
		const CommonGameObject* on = tile.building.getFrom(gs);
		if (o->blueprint->bpClass == Tags::GAMEOBJCLASS_CHARACTER) {
			const auto* tt = gs->terrain->getPlayableTile(tx, tz);
			const bool wet = tt && tt->fullOfWater && tt->waterLevel - gs->terrain->getHeight(o->position.x, o->position.z) > 1.0f;
			if ((on && !tile.buildingPassable) || wet) {
				charactersBlocked++;
				if (blockedAt.size() < 50) blockedAt.push_back({ { "id", o->id }, { "blueprint", o->blueprint->name }, { "at", at(o) }, { "on", on ? on->blueprint->name : "water" } });
				if (warnings)
					warnings->push_back("placement: " + o->blueprint->name + " " + std::to_string(o->id) + " at (" + Num(o->position.x) + ", " + Num(o->position.z)
						+ ") stands " + (on ? "in " + on->blueprint->name : std::string("in water")));
			}
		}
		else if (on && !o->blueprint->canBeBuiltUpon) {
			containersOnBuildings++;
			if (blockedAt.size() < 50) blockedAt.push_back({ { "id", o->id }, { "blueprint", o->blueprint->name }, { "at", at(o) }, { "on", on->blueprint->name } });
			if (warnings)
				warnings->push_back("placement: " + o->blueprint->name + " " + std::to_string(o->id) + " at (" + Num(o->position.x) + ", " + Num(o->position.z)
					+ ") stands on " + on->blueprint->name);
		}
	}
	out["rules"] = "the server's own (StampdownPlan::getStampdownPlan): footprint on the map and free, its terrains, MAX_FOUNDATION_HEIGHT_DIFFERENTIAL, "
		"the neighbour rule, the city rules; each building as it stands, its own tiles freed";
	out["buildings"] = buildings.size() - noFootprint;
	out["passed"] = passed;
	out["failedCount"] = (int)(buildings.size() - noFootprint) - passed;
	out["failed"] = failed;
	out["byStatus"] = byStatus;
	out["byBlueprint"] = byBlueprint;
	out["withoutFootprint"] = noFootprint;
	out["charactersBlocked"] = charactersBlocked;
	out["containersOnBuildings"] = containersOnBuildings;
	if (!blockedAt.empty()) out["objectsBlockedAt"] = blockedAt;
	out["ok"] = out["failedCount"].get<int>() == 0 && charactersBlocked == 0 && containersOnBuildings == 0;
	return out;
}

json Fairness(CommonGameState* gs, float radius, std::vector<std::string>* warnings)
{
	json out;
	if (!gs || !gs->terrain || !gs->gameSet || !gs->level) {
		out["skipped"] = "no game state";
		return out;
	}
	const GameSet& set = *gs->gameSet;
	const auto [pw, ph] = gs->terrain->getNumPlayableTiles();
	// the bases: each player's first civic centre (by id)
	struct Base { const CommonGameObject* player; const CommonGameObject* centre; };
	std::vector<Base> bases;
	std::vector<const CommonGameObject*> containers;
	std::vector<std::pair<uint32_t, const CommonGameObject*>> sorted(gs->idmap.begin(), gs->idmap.end());
	std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
	std::map<const CommonGameObject*, const CommonGameObject*> centreOf;
	for (const auto& [id, o] : sorted) {
		if (!o || (o->flags & CommonGameObject::fTerminated)) continue;
		if (o->blueprint->bpClass == Tags::GAMEOBJCLASS_CONTAINER)
			containers.push_back(o);
		else if (o->blueprint->bpClass == Tags::GAMEOBJCLASS_BUILDING && o->blueprint->buildingType == Tags::BUILDINGTYPE_CIVIC_CENTRE)
			if (const CommonGameObject* p = o->getPlayer(); p && !centreOf.count(p))
				centreOf[p] = o;
	}
	for (const auto& [p, c] : centreOf)
		bases.push_back({ p, c });
	std::sort(bases.begin(), bases.end(), [](const Base& a, const Base& b) { return a.player->id < b.player->id; });
	// the resources by name
	std::vector<std::pair<std::string, int>> res;
	for (const char* name : { "Wood", "Gold", "Stone", "Food" }) {
		int i = set.items.names.getIndex(name);
		if (i < 0) {
			std::string up = name;
			for (char& ch : up) ch = (char)toupper((unsigned char)ch);
			i = set.items.names.getIndex(up);
		}
		if (i >= 0) res.push_back({ name, i });
	}
	// a farm's terrains (the first rural building with a footprint whose name has "Farm")
	const GameObjBlueprint* farm = set.findBlueprint(Tags::GAMEOBJCLASS_BUILDING, "Standard Farm");
	json list = json::array();
	std::map<std::string, std::pair<double, double>> range; // min, max
	for (const Base& b : bases) {
		const float bx = b.centre->position.x, bz = b.centre->position.z;
		json j = { { "player", b.player->id }, { "centre", b.centre->blueprint->name }, { "at", { std::lround(bx), std::lround(bz) } } };
		json amounts = json::object(), counts = json::object();
		for (const auto& [name, idx] : res) { amounts[name] = 0.0; counts[name] = 0; }
		for (const CommonGameObject* c : containers) {
			if (std::hypot(c->position.x - bx, c->position.z - bz) > radius) continue;
			for (const auto& [name, idx] : res)
				if (const float v = c->getItem(idx); v > 0) {
					amounts[name] = amounts[name].get<double>() + v;
					counts[name] = counts[name].get<int>() + 1;
				}
		}
		int farmable = 0;
		if (farm && farm->footprint && !farm->footprint->tiles.empty()) {
			const int r = (int)(radius / 5);
			const int cx = (int)(bx / 5), cz = (int)(bz / 5);
			for (int dz = -r; dz <= r; dz++)
				for (int dx = -r; dx <= r; dx++) {
					const int tx = cx + dx, tz = cz + dz;
					if (dx * dx + dz * dz > r * r || tx < 0 || tz < 0 || tx >= pw || tz >= ph) continue;
					const auto* tile = gs->terrain->getPlayableTile(tx, tz);
					if (tile && !tile->fullOfWater && !gs->tiles[(size_t)tz * pw + tx].building.getFrom(gs)
						&& StampdownPlan::tileTerrainAllowed(gs, farm->footprint->tiles.front(), tx, tz))
						farmable++;
				}
		}
		float nearest = 1e30f;
		for (const Base& o : bases)
			if (o.player != b.player)
				nearest = std::min(nearest, std::hypot(o.centre->position.x - bx, o.centre->position.z - bz));
		j["resources"] = amounts;
		j["deposits"] = counts;
		j["farmableTiles"] = farmable;
		j["nearestBaseM"] = nearest < 1e29f ? json(std::lround(nearest)) : json(nullptr);
		auto note = [&](const std::string& k, double v) {
			auto it = range.find(k);
			if (it == range.end()) range[k] = { v, v };
			else it->second = { std::min(it->second.first, v), std::max(it->second.second, v) };
		};
		for (const auto& [name, idx] : res) note(name, amounts[name].get<double>());
		note("farmableTiles", farmable);
		if (nearest < 1e29f) note("nearestBaseM", nearest);
		list.push_back(j);
	}
	json ratios = json::object();
	for (const auto& [k, mm] : range) {
		const double r = mm.second > 0 ? mm.first / mm.second : 1.0;
		ratios[k] = { { "min", mm.first }, { "max", mm.second }, { "ratio", std::lround(r * 1000) / 1000.0 } };
		if (warnings && bases.size() > 1 && mm.second > 0 && r < 0.75 && k != "Food" && k != "nearestBaseM")
			warnings->push_back("fairness: " + k + " within " + Num(radius) + " m of the bases runs from " + Num((float)mm.first) + " to " + Num((float)mm.second)
				+ " (ratio " + Num((float)r) + ")");
	}
	out["radiusM"] = radius;
	out["bases"] = list;
	out["ratios"] = ratios;
	return out;
}

} // namespace MapObjects
