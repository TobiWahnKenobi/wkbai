// wkbai - the ministry AI's Intelligence: the picture of the others, from what was seen, and the
// threat map (Ministries.h, the RE repo's docs/engine/ministry-intelligence.md)
//
// - Seen: an enemy object within the sight of one of the player's characters or buildings (their
//   Actual Sight Range, in 5 m tiles as the fog of war's), on a grid of 10 m cells. "aiVision": 1
//   sees everything (a difficulty setting); 0, the default, only what was seen.
// - Remembered: buildings until their place is seen again without them; characters for 3 minutes.
// - The threat map from the sightings as they were at the last think, worked out on a worker
//   thread meanwhile and taken at this think: the same map whatever the threads' speed.

#include "MinistryDepartments.h"
#include "Naval.h"
#include "AIUtil.h"
#include "server.h"
#include "terrain.h"
#include "gameset/gameset.h"
#include "settings.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <future>
#include <set>

namespace Ministry {

float Picture::threatAt(float x, float z) const
{
	if (threat.empty())
		return 0.0f;
	const int cx = (int)std::floor(x / CELL), cz = (int)std::floor(z / CELL);
	if (cx < 0 || cz < 0 || cx >= width || cz >= height)
		return 0.0f;
	return threat[cz * width + cx];
}

float Picture::threatNear(float x, float z, float radius) const
{
	if (threat.empty())
		return 0.0f;
	const int r = (int)std::ceil(radius / CELL);
	const int cx = (int)std::floor(x / CELL), cz = (int)std::floor(z / CELL);
	float best = 0.0f;
	for (int dz = -r; dz <= r; dz++)
		for (int dx = -r; dx <= r; dx++) {
			const int ux = cx + dx, uz = cz + dz;
			if (ux >= 0 && uz >= 0 && ux < width && uz < height)
				best = std::max(best, threat[uz * width + ux]);
		}
	return best;
}

std::vector<const Sighting*> Picture::of(uint32_t player) const
{
	std::vector<const Sighting*> list;
	for (const auto& [id, s] : sightings)
		if (s.player == player)
			list.push_back(&s);
	return list;
}

namespace {
	constexpr float SEEN_CELL = 10.0f;
	constexpr float FORGET_CHARACTERS = 180.0f;

	struct ThreatInput {
		std::vector<Sighting> sightings;
		float now = 0.0f;
		int width = 0, height = 0;
	};

	std::vector<float> ThreatMap(const ThreatInput& in)
	{
		std::vector<float> raw((size_t)in.width * in.height, 0.0f), out(raw.size(), 0.0f);
		for (const Sighting& s : in.sightings) {
			if (!s.fighter && !s.tower)
				continue;
			const int cx = (int)std::floor(s.x / Picture::CELL), cz = (int)std::floor(s.z / Picture::CELL);
			if (cx < 0 || cz < 0 || cx >= in.width || cz >= in.height)
				continue;
			const float age = std::max(0.0f, in.now - s.seen);
			raw[cz * in.width + cx] += s.power * std::exp(-age / 80.0f);
		}
		// each cell with half of its neighbours' (an army reaches a cell further in a few seconds)
		for (int z = 0; z < in.height; z++)
			for (int x = 0; x < in.width; x++) {
				float v = raw[z * in.width + x];
				for (int dz = -1; dz <= 1; dz++)
					for (int dx = -1; dx <= 1; dx++)
						if ((dx || dz) && x + dx >= 0 && z + dz >= 0 && x + dx < in.width && z + dz < in.height)
							v += 0.5f * raw[(z + dz) * in.width + x + dx];
				out[z * in.width + x] = v;
			}
		return out;
	}

	struct Intelligence : Department {
		uint8_t id() const override { return INTELLIGENCE; }
		float period() const override { return 2.0f; }
		std::future<std::vector<float>> pending;
		int pendingWidth = 0, pendingHeight = 0;

		void think(Government& g) override {
			Server& srv = g.server;
			ServerGameObject* me = g.player();
			if (!me || !srv.terrain || !srv.getLevel())
				return;
			const float now = g.now();
			Picture& pic = g.picture;
			const auto [areaW, areaH] = srv.terrain->getPlayableArea();
			// the threat map from the last think's sightings (or, the first think after loading, from
			// them as loaded: the same)
			{
				const int w = std::max(1, (int)std::ceil(areaW / Picture::CELL)), h = std::max(1, (int)std::ceil(areaH / Picture::CELL));
				if (pending.valid()) {
					pic.threat = pending.get();
					pic.width = pendingWidth;
					pic.height = pendingHeight;
				}
				else if (!pic.sightings.empty()) {
					ThreatInput in{ {}, lastThink, w, h };
					for (const auto& [id, s] : pic.sightings)
						in.sightings.push_back(s);
					pic.threat = ThreatMap(in);
					pic.width = w;
					pic.height = h;
				}
			}

			// what the player's characters and buildings see now
			const bool all = srv.aiVision == 1;
			const int gw = std::max(1, (int)std::ceil(areaW / SEEN_CELL)), gh = std::max(1, (int)std::ceil(areaH / SEEN_CELL));
			std::vector<uint8_t> seen;
			if (!all) {
				seen.assign((size_t)gw * gh, 0);
				auto stamp = [&](const ServerGameObject* o) {
					const float r = std::floor(o->getItem(Tags::PDITEM_ACTUAL_SIGHT_RANGE) + 0.5f) * 5.0f;
					if (r <= 0.0f)
						return;
					const int x0 = std::max(0, (int)std::floor((o->position.x - r) / SEEN_CELL)), x1 = std::min(gw - 1, (int)std::floor((o->position.x + r) / SEEN_CELL));
					const int z0 = std::max(0, (int)std::floor((o->position.z - r) / SEEN_CELL)), z1 = std::min(gh - 1, (int)std::floor((o->position.z + r) / SEEN_CELL));
					for (int z = z0; z <= z1; z++)
						for (int x = x0; x <= x1; x++) {
							const float dx = (x + 0.5f) * SEEN_CELL - o->position.x, dz = (z + 0.5f) * SEEN_CELL - o->position.z;
							if (dx * dx + dz * dz <= r * r)
								seen[z * gw + x] = 1;
						}
				};
				std::vector<ServerGameObject*> todo{ me };
				while (!todo.empty()) {
					ServerGameObject* o = todo.back();
					todo.pop_back();
					for (const auto& [type, children] : o->children)
						for (CommonGameObject* c : children) {
							auto* u = (ServerGameObject*)c;
							if (u->deleted)
								continue;
							if (u->blueprint->bpClass == Tags::GAMEOBJCLASS_CHARACTER || u->blueprint->bpClass == Tags::GAMEOBJCLASS_BUILDING)
								stamp(u);
							todo.push_back(u);
						}
				}
			}
			auto isSeen = [&](float x, float z) {
				if (all)
					return true;
				const int cx = (int)std::floor(x / SEEN_CELL), cz = (int)std::floor(z / SEEN_CELL);
				return cx >= 0 && cz >= 0 && cx < gw && cz < gh && seen[cz * gw + cx] != 0;
			};

			// the enemy players' objects in sight
			ServerGameObject* level = (ServerGameObject*)srv.getLevel();
			std::vector<ServerGameObject*> enemies;
			for (const auto& [type, children] : level->children)
				for (CommonGameObject* c : children) {
					auto* p = (ServerGameObject*)c;
					if (p != me && !p->deleted && p->blueprint->bpClass == Tags::GAMEOBJCLASS_PLAYER && p->blueprint->name != "Neutral Player" && AIUtil::IsEnemy(me, p))
						enemies.push_back(p);
				}
			std::sort(enemies.begin(), enemies.end(), [](auto* a, auto* b) { return a->id < b->id; });
			ServerGameObject* myPalace = AIUtil::Palace(me);
			std::map<uint32_t, ServerGameObject*> live; // the enemies' objects in sight now
			for (ServerGameObject* p : enemies) {
				ServerGameObject* palace = AIUtil::Palace(p);
				std::vector<ServerGameObject*> todo{ p };
				while (!todo.empty()) {
					ServerGameObject* o = todo.back();
					todo.pop_back();
					for (const auto& [type, children] : o->children)
						for (CommonGameObject* c : children) {
							auto* u = (ServerGameObject*)c;
							if (u->deleted)
								continue;
							todo.push_back(u);
							const bool building = u->blueprint->bpClass == Tags::GAMEOBJCLASS_BUILDING;
							if ((!building && u->blueprint->bpClass != Tags::GAMEOBJCLASS_CHARACTER) || !isSeen(u->position.x, u->position.z))
								continue;
							live[u->id] = u;
							Sighting& s = pic.sightings[u->id];
							s.id = u->id;
							s.player = p->id;
							s.building = building;
							s.peasant = !building && AIUtil::IsPeasant(u);
							s.fighter = !building && AIUtil::IsFighter(u);
							s.tower = building && AIUtil::UnitPower(u) > 0.0f;
							s.palace = u == palace;
							s.cls = (uint8_t)AIUtil::ClassOf(u);
							s.power = (s.fighter || s.tower) ? AIUtil::UnitPower(u) : 0.0f;
							s.x = u->position.x;
							s.z = u->position.z;
							s.seen = now;
							s.type = u->blueprint->name;
						}
				}
			}
			// forgotten: what is in sight but gone, characters seen no more for 3 minutes, players no
			// longer enemies
			std::set<uint32_t> enemyIds;
			for (ServerGameObject* p : enemies)
				enemyIds.insert(p->id);
			for (auto it = pic.sightings.begin(); it != pic.sightings.end();) {
				const Sighting& s = it->second;
				const bool gone = !live.count(s.id) && isSeen(s.x, s.z);
				const bool old = !s.building && now - s.seen > FORGET_CHARACTERS;
				if (gone || old || !enemyIds.count(s.player))
					it = pic.sightings.erase(it);
				else
					++it;
			}

			// the reports: per enemy player, what is known of it
			int total = 0;
			for (ServerGameObject* p : enemies) {
				int fighters = 0, buildings = 0, peasants = 0, towers = 0;
				float power = 0.0f, nearest = -1.0f;
				bool palaceKnown = false;
				for (const Sighting* s : pic.of(p->id)) {
					total++;
					fighters += s->fighter;
					buildings += s->building;
					peasants += s->peasant;
					towers += s->tower;
					if (s->fighter)
						power += s->power;
					palaceKnown |= s->palace;
					if (s->building && myPalace) {
						const float d = std::hypot(s->x - myPalace->position.x, s->z - myPalace->position.z);
						if (nearest < 0.0f || d < nearest)
							nearest = d;
					}
				}
				const std::string k = "intel." + std::to_string(p->id) + ".";
				g.board.report(k + "fighters", fighters);
				g.board.report(k + "power", power);
				g.board.report(k + "buildings", buildings);
				g.board.report(k + "peasants", peasants);
				g.board.report(k + "towers", towers);
				g.board.report(k + "palace", palaceKnown ? 1 : 0);
				g.board.report(k + "distance", nearest);
			}
			g.board.report("intel.sightings", total);
			g.board.report("intel.enemies", (double)enemies.size());
			// (those whose palace stands: War defends against one alone, MinistryWar.cpp)
			g.board.report("intel.rivals", (double)std::count_if(enemies.begin(), enemies.end(), [](ServerGameObject* p) { return AIUtil::Palace(p) != nullptr; }));
			if (myPalace)
				g.board.report("intel.threat.home", pic.threatNear(myPalace->position.x, myPalace->position.z, 250.0f));
			// the map (improvement 6): how far the nearest enemy starts from the palace (their start
			// cameras, known from the start as in a skirmish's lobby), and each enemy's
			if (myPalace && !(MinOff() & 16384)) {
				float nearest = -1.0f;
				for (ServerGameObject* p : enemies) {
					const Vector3 at = p->playerStartCameraPosition;
					if (at.x == 0.0f && at.z == 0.0f)
						continue;
					const float d = std::hypot(at.x - myPalace->position.x, at.z - myPalace->position.z);
					g.board.report("map." + std::to_string(p->id) + ".start", d);
					if (nearest < 0.0f || d < nearest)
						nearest = d;
				}
				g.board.report("map.nearestEnemy", nearest);
			}
			// the warning (improvement 2): enemy fighters seen within 500 m of the palace in the last
			// minute, before they are at it (four players, plain WKB, seed 7: the threat map's 250 m saw the
			// wave of minute 16 when it was in the town, and it killed all 27 peasants)
			{
				int n = 0;
				float power = 0.0f;
				if (myPalace && !(MinOff() & 2048))
					for (const auto& [id, s] : pic.sightings)
						if (s.fighter && now - s.seen < 60.0f && std::hypot(s.x - myPalace->position.x, s.z - myPalace->position.z) < Tune("warnRadius", 500.0f)) {
							n++;
							power += s.power;
						}
				if (n > 0 && g.board.get("intel.approach") == 0.0)
					g.log(INTELLIGENCE, "warning: %d enemy fighters within 500 m (power %.0f)", n, power);
				g.board.report("intel.approach", n);
				g.board.report("intel.approachPower", power);
			}
			// the landings: enemy fighters seen in the last minute anywhere on the home's land, where no
			// rival's palace stands on it (an island: they came by sea). The warning's 500 m missed them
			// (3 Isles seed 7: the aggressive AI's 37 landed at the villages and the Stockade, the missions
			// stayed out, and about 120 of the ministry's peasants died)
			{
				int n = 0;
				float power = 0.0f;
				int cls = -1;
				const int region = myPalace && !(MinOff() & 2097152) ? Naval::HomeLandRegion(me, &cls) : -1;
				bool shared = region < 0;
				for (ServerGameObject* p : enemies)
					if (ServerGameObject* palace = AIUtil::Palace(p); palace && !shared)
						shared = Naval::LandRegionAt(cls, palace->position) == region;
				if (!shared)
					for (const auto& [id, s] : pic.sightings)
						if (s.fighter && now - s.seen < 60.0f && Naval::LandRegionAt(cls, Vector3(s.x, 0.0f, s.z)) == region) {
							n++;
							power += s.power;
						}
				if (n >= 3 && g.board.get("intel.landed") < 3.0)
					g.log(INTELLIGENCE, "a landing: %d enemy fighters on the home's island (power %.0f)", n, power);
				g.board.report("intel.landed", n);
				g.board.report("intel.landedPower", power);
			}
			if (thinks % 15 == 0)
				g.log(INTELLIGENCE, "%d sightings of %zu enemies; the threat at home %.0f", total, enemies.size(),
					myPalace ? pic.threatNear(myPalace->position.x, myPalace->position.z, 250.0f) : 0.0f);

			// the next threat map, from these sightings, meanwhile on a worker thread
			ThreatInput in;
			in.now = now;
			in.width = std::max(1, (int)std::ceil(areaW / Picture::CELL));
			in.height = std::max(1, (int)std::ceil(areaH / Picture::CELL));
			for (const auto& [id, s] : pic.sightings)
				in.sightings.push_back(s);
			pendingWidth = in.width;
			pendingHeight = in.height;
			pending = std::async(std::launch::async, [in = std::move(in)]() { return ThreatMap(in); });
			lastThink = now;
		}
		float lastThink = 0.0f;

		void save(const Government& g, std::vector<std::string>& lines) const override {
			// (the pending map isn't saved: loading works it out from the sightings, which it was made of)
			for (const auto& [id, s] : g.picture.sightings) {
				char buf[200];
				snprintf(buf, sizeof(buf), "s %u %u %d %d %d %d %d %d %.9g %.9g %.9g %.9g ", s.id, s.player, (int)s.building, (int)s.peasant,
					(int)s.fighter, (int)s.palace, (int)s.tower, (int)s.cls, s.power, s.x, s.z, s.seen);
				lines.push_back(buf + s.type);
			}
			char buf[40];
			snprintf(buf, sizeof(buf), "last %.9g", lastThink);
			lines.push_back(buf);
		}
		void load(Government& g, const std::string& line) override {
			if (line.rfind("last ", 0) == 0) {
				lastThink = (float)atof(line.c_str() + 5);
				return;
			}
			Sighting s;
			int b, pe, f, pa, t, c, n = 0;
			if (sscanf(line.c_str(), "s %u %u %d %d %d %d %d %d %f %f %f %f %n", &s.id, &s.player, &b, &pe, &f, &pa, &t, &c, &s.power, &s.x, &s.z, &s.seen, &n) >= 12 && n > 0) {
				s.building = b; s.peasant = pe; s.fighter = f; s.palace = pa; s.tower = t; s.cls = (uint8_t)c;
				s.type = line.substr(n);
				g.picture.sightings[s.id] = s;
			}
		}
	};
}

std::unique_ptr<Department> MakeIntelligence() { return std::make_unique<Intelligence>(); }

}
