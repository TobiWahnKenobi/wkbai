// wkbai - the server's day and night, seasons and weather (see ServerEnvironment.h)
// Licensed under the GNU General Public License 3

#include "ServerEnvironment.h"
#include "server.h"
#include "network.h"
#include "terrain.h"
#include "TrnTextureDb.h"
#include "tags.h"
#include "util/SimRandom.h"
#include "gameset/gameset.h"
#include "gameset/GameObjBlueprint.h"
#include "gameset/ScriptContext.h"
#include "gameset/actions.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <sstream>

namespace {
	constexpr float PI = 3.14159265358979f;
	float Rand01() { return SimRand() / (float)SIM_RAND_MAX; }
	float Approach(float x, float target, float up, float down) {
		return x < target ? std::min(target, x + up) : std::max(target, x - down);
	}
	float Quantize(float x, float step) { return std::round(x / step) * step; }
	// gentle, normal, harsh: how far a season strays from the year's mean, and how its extreme
	// weather comes
	const float SEVERITY_SCALE[3] = { 0.6f, 1.0f, 1.45f };
	bool IsHarshWeather(int w) {
		return w == WEATHER_HEAVY_RAIN || w == WEATHER_THUNDERSTORM || w == WEATHER_BLIZZARD || w == WEATHER_HEATWAVE || w == WEATHER_DROUGHT;
	}
	// each season's weather, by weight (spring, summer, autumn, winter)
	const float WEATHER_WEIGHTS[4][WEATHER_COUNT] = {
		//  clear cloudy rain  heavy storm fog   snow  blizz heat  drought
		{ 3.0f, 3.0f, 3.0f, 1.2f, 1.0f, 1.5f, 0.4f, 0.0f, 0.0f, 0.0f },
		{ 4.0f, 2.0f, 1.0f, 0.5f, 2.0f, 0.3f, 0.0f, 0.0f, 1.5f, 0.8f },
		{ 2.0f, 3.0f, 3.0f, 2.0f, 1.0f, 3.0f, 0.3f, 0.0f, 0.0f, 0.0f },
		{ 2.0f, 3.0f, 0.6f, 0.0f, 0.0f, 2.0f, 4.0f, 1.2f, 0.0f, 0.0f },
	};
	// what each weather looks like: clouds, precipitation, fog
	struct WeatherLook { float cloud, precip, fog; };
	const WeatherLook WEATHER_LOOK[WEATHER_COUNT] = {
		{ 0.05f, 0.0f, 0.0f }, { 0.6f, 0.0f, 0.0f }, { 0.85f, 0.5f, 0.0f }, { 0.95f, 1.0f, 0.1f }, { 1.0f, 0.9f, 0.0f },
		{ 0.5f, 0.0f, 0.85f }, { 0.85f, 0.6f, 0.1f }, { 1.0f, 1.0f, 0.4f }, { 0.0f, 0.0f, 0.0f }, { 0.05f, 0.0f, 0.0f },
	};
	// the weather's pull on the temperature (degrees C)
	const float WEATHER_TEMPERATURE[WEATHER_COUNT] = { 0.0f, -1.0f, -2.0f, -3.0f, -3.0f, -1.0f, -2.0f, -7.0f, 8.0f, 5.0f };
	// the water's level, metres: floods in heavy rain, low water in drought
	const float WEATHER_WATER[WEATHER_COUNT] = { 0.0f, 0.0f, 0.5f, 1.5f, 1.0f, 0.0f, 0.0f, 0.0f, -0.7f, -1.5f };

	bool ContainsCI(const std::string& s, const char* what) {
		std::string a = s;
		for (char& c : a) c = (char)std::tolower((unsigned char)c);
		return a.find(what) != std::string::npos;
	}
}

uint8_t ServerEnvironment::kindOf(const GameObjBlueprint* bp) const
{
	auto it = kinds.find(bp);
	if (it != kinds.end())
		return it->second;
	uint8_t k = K_OTHER;
	const std::string& n = bp->name;
	if (bp->bpClass == Tags::GAMEOBJCLASS_BUILDING) {
		if (n.size() >= 10 && n.compare(n.size() - 10, 10, "Foundation") == 0)
			k = K_FOUNDATION;
		else if (ContainsCI(n, "manor") || ContainsCI(n, "village") || ContainsCI(n, "keep") || ContainsCI(n, "castle") || ContainsCI(n, "palace"))
			k = K_SHELTER;
	}
	else if (bp->bpClass == Tags::GAMEOBJCLASS_CHARACTER) {
		static const char* machines[] = { "cart", "boat", "ram", "bombard", "trebuchet", "catapult", "cannon", "siege", "elemental",
			"ship", "cog", "infernal", "mangonel", "ballista", "tower", "rocket", "organ", "engine" };
		if (n.rfind("Dead ", 0) == 0)
			k = K_OTHER;
		else if (ContainsCI(n, "undead") || ContainsCI(n, "gibbering horde"))
			k = K_UNDEAD;
		else {
			k = K_LIVING;
			for (const char* m : machines)
				if (ContainsCI(n, m))
					k = K_MACHINE;
		}
	}
	kinds[bp] = k;
	return k;
}

float ServerEnvironment::speedFactorFor(const ServerGameObject* obj) const
{
	if (!enabled() || obj->blueprint->canWalkOnWater())
		return 1.0f;
	// the undead: the mud drags at them far more (to a quarter of their speed on soaked ground)
	if (kindOf(obj->blueprint) == K_UNDEAD)
		return std::clamp(Quantize(1.0f - 0.75f * state.wetness - 0.35f * state.snowCover, 0.05f), 0.25f, 1.0f);
	return state.speedFactor;
}

float ServerEnvironment::fireRisk() const
{
	if (!settings.seasons || (state.weather != WEATHER_HEATWAVE && state.weather != WEATHER_DROUGHT))
		return 0.0f;
	const float base = state.weather == WEATHER_HEATWAVE ? 1.0f : 0.75f;
	return std::clamp(base * SEVERITY_SCALE[std::clamp(state.severity, 0, 2)] - state.wetness - state.rain, 0.0f, 1.5f);
}

// how well the land burns: grass, fields, meadows and forest floor well, bare ground less, rock,
// sand, snow, roads and water not; trees around it more
float ServerEnvironment::fuelAt(Server& server, const Vector3& pos)
{
	Terrain* t = server.terrain;
	const auto [w, h] = t->getNumPlayableTiles();
	const int tx = (int)(pos.x / 5.0f), tz = (int)(pos.z / 5.0f);
	if (tx < 0 || tz < 0 || tx >= w || tz >= h)
		return 0.0f;
	const TerrainTile* tile = t->getPlayableTile(tx, tz);
	if (!tile || tile->fullOfWater || state.snowCover > 0.2f)
		return 0.0f;
	float fuel = 0.6f;
	if (tile->texture && tile->texture->grp) {
		const std::string& g = tile->texture->grp->name;
		if (ContainsCI(g, "grass") || ContainsCI(g, "field") || ContainsCI(g, "meadow") || ContainsCI(g, "forest") || ContainsCI(g, "moss") || ContainsCI(g, "green"))
			fuel = 1.0f;
		else if (ContainsCI(g, "rock") || ContainsCI(g, "sand") || ContainsCI(g, "snow") || ContainsCI(g, "road") || ContainsCI(g, "river") || ContainsCI(g, "beach")
			|| ContainsCI(g, "stone") || ContainsCI(g, "cliff") || ContainsCI(g, "water") || ContainsCI(g, "cobble") || ContainsCI(g, "path"))
			fuel = 0.05f;
	}
	if (server.tiles)
		for (int dz = -1; dz <= 1; dz++)
			for (int dx = -1; dx <= 1; dx++) {
				if (tx + dx < 0 || tz + dz < 0 || tx + dx >= w || tz + dz >= h)
					continue;
				for (const auto& ref : server.tiles[(tz + dz) * w + tx + dx].objList)
					if (ServerGameObject* o = ref.getFrom<Server>(); o && !o->deleted && ContainsCI(o->blueprint->name, "tree"))
						fuel += 0.25f;
			}
	return std::min(fuel, 2.0f);
}

bool ServerEnvironment::igniteAt(Server& server, const Vector3& at, float life, int gen)
{
	if (fires.size() >= 60)
		return false;
	const auto [tw, th] = server.terrain->getNumPlayableTiles();
	const int tile = (int)(at.z / 5.0f) * tw + (int)(at.x / 5.0f);
	if (at.x < 0 || at.z < 0 || at.x >= tw * 5.0f || at.z >= th * 5.0f || burnt.count(tile))
		return false;
	for (const Wildfire& f : fires)
		if ((f.pos - at).sqlen2xz() < 4.0f * 4.0f)
			return false;
	if (fuelAt(server, at) < 0.3f)
		return false;
	Wildfire f;
	f.pos = Vector3(at.x, server.terrain->getHeight(at.x, at.z), at.z);
	f.born = state.time;
	f.life = life;
	f.gen = gen;
	burnt.insert(tile);
	charTrees(server, f.pos);
	static const GameObjBlueprint* burning = nullptr;
	static const GameSet* of = nullptr;
	if (of != server.gameSet.get()) {
		of = server.gameSet.get();
		burning = of->findBlueprint(Tags::GAMEOBJCLASS_CONTAINER, "Big Burning Object");
		if (!burning)
			burning = of->findBlueprint(Tags::GAMEOBJCLASS_CONTAINER, "Burning Object");
	}
	// owned by a player, as the Firestorm's flames are (the client draws them in its colour): the
	// neutral player where the map has one, else the first
	ServerGameObject* owner = nullptr;
	if (ServerGameObject* level = server.getLevel())
		for (const auto& [type, children] : level->children)
			for (CommonGameObject* c : children)
				if (c->blueprint->bpClass == Tags::GAMEOBJCLASS_PLAYER && !((ServerGameObject*)c)->deleted
					&& (!owner || c->blueprint->name == "Neutral Player") && (!owner || owner->blueprint->name != "Neutral Player"))
					owner = (ServerGameObject*)c;
	if (burning && owner)
		if (ServerGameObject* v = server.spawnObject(burning, owner, f.pos, Vector3(0, Rand01() * 6.28f, 0)))
			f.visual = v;
	fires.push_back(f);
	stats.fireCells++;
	return true;
}

// the trees and bushes within 6 m of a burning patch are charred (only how they are drawn: no
// random numbers, the wood they hold as it was)
void ServerEnvironment::charTrees(Server& server, const Vector3& at)
{
	if (!server.tiles)
		return;
	const auto [w, h] = server.terrain->getNumPlayableTiles();
	const int tx = (int)(at.x / 5.0f), tz = (int)(at.z / 5.0f);
	for (int dz = -2; dz <= 2; dz++)
		for (int dx = -2; dx <= 2; dx++) {
			if (tx + dx < 0 || tz + dz < 0 || tx + dx >= w || tz + dz >= h)
				continue;
			for (const auto& ref : server.tiles[(tz + dz) * w + tx + dx].objList)
				if (ServerGameObject* o = ref.getFrom<Server>(); o && !o->deleted && (o->position - at).sqlen2xz() < 6.0f * 6.0f
					&& (ContainsCI(o->blueprint->name, "tree") || ContainsCI(o->blueprint->name, "bush")) && !ContainsCI(o->blueprint->name, "bird"))
					if (charred.insert(o->id).second)
						charredOrder.push_back(o->id);
		}
}

void ServerEnvironment::fireLanded(Server& server, const Vector3& pos, bool sure)
{
	const float risk = fireRisk();
	if (risk <= 0.0f || (!sure && Rand01() >= 0.35f * risk))
		return;
	// a large fire at once: the patch and a ring around it
	bool any = igniteAt(server, pos, 40.0f + 20.0f * Rand01());
	for (int i = 0; i < 4; i++) {
		const float a = Rand01() * 6.28f, d = 5.0f + 3.0f * Rand01();
		any |= igniteAt(server, pos + Vector3(std::cos(a) * d, 0, std::sin(a) * d), 30.0f + 20.0f * Rand01());
	}
	if (any) {
		stats.wildfires++;
		stats.fromMissiles++;
		if (trace)
			printf("ENVIRONMENT t=%.0f a flaming missile sets the land alight at (%.0f, %.0f)\n", state.time, pos.x, pos.z);
	}
}

void ServerEnvironment::updateFires(Server& server)
{
	const float risk = fireRisk();
	if (risk <= 0.0f && fires.empty()) {
		burnt.clear(); // the land grows back once the heat is over
		return;
	}
	const auto [aw, ah] = server.terrain->getPlayableArea();
	// out of nowhere: once in about three minutes in a full heatwave, somewhere that burns
	if (risk > 0.0f && Rand01() < 0.006f * risk) {
		for (int tries = 0; tries < 12; tries++) {
			const Vector3 at(Rand01() * aw, 0, Rand01() * ah);
			if (fuelAt(server, at) >= 0.9f && igniteAt(server, at, 25.0f + 20.0f * Rand01())) {
				stats.wildfires++;
				stats.spontaneous++;
				if (trace)
					printf("ENVIRONMENT t=%.0f a wildfire breaks out at (%.0f, %.0f)\n", state.time, at.x, at.z);
				break;
			}
		}
	}
	// from the burning buildings (whoever set them alight: torches, flaming arrows, lightning)
	if (risk > 0.0f) {
		const int alias = server.gameSet->aliases.names.getIndex("Burning Buildings");
		if (alias >= 0)
			for (const auto& ref : server.aliases[alias])
				if (ServerGameObject* b = ref.getFrom<Server>(); b && !b->deleted && Rand01() < 0.04f * risk) {
					const float a = Rand01() * 6.28f, d = 7.0f + 6.0f * Rand01();
					if (igniteAt(server, b->position + Vector3(std::cos(a) * d, 0, std::sin(a) * d), 25.0f + 15.0f * Rand01())) {
						stats.wildfires++;
						stats.fromBuildings++;
					}
				}
	}
	// spreading, burning out, the harm done
	const int hp = server.gameSet->items.names.getIndex("Hit Points");
	const int setFire = server.gameSet->actionSequences.names.getIndex("Set Fire to Building if Fire Proofing fails");
	const auto [tw, th] = server.terrain->getNumPlayableTiles();
	std::vector<std::pair<Vector3, float>> spreadTo; // where, the generation
	std::vector<std::pair<SrvGORef, float>> hurt;
	std::set<uint32_t> buildingsDone;
	for (size_t i = 0; i < fires.size();) {
		Wildfire& f = fires[i];
		const float age = state.time - f.born;
		if (age > f.life * (state.rain > 0.3f ? 0.4f : 1.0f)) {
			if (ServerGameObject* v = f.visual.get())
				server.deleteObject(v);
			fires.erase(fires.begin() + i);
			continue;
		}
		// to the land around: much in the heat, a little once it is over
		const float spread = 0.1f * std::max(risk, 0.15f);
		if (f.gen < (int)(12.0f * std::max(risk, 0.3f)) && Rand01() < spread) {
			const float a = Rand01() * 6.28f, d = 5.0f + 4.0f * Rand01();
			spreadTo.push_back({ f.pos + Vector3(std::cos(a) * d, 0, std::sin(a) * d), (float)(f.gen + 1) });
		}
		// who is in it, what stands on it
		const int tx = (int)(f.pos.x / 5.0f), tz = (int)(f.pos.z / 5.0f);
		if (server.tiles)
			for (int dz = -1; dz <= 1; dz++)
				for (int dx = -1; dx <= 1; dx++) {
					if (tx + dx < 0 || tz + dz < 0 || tx + dx >= tw || tz + dz >= th)
						continue;
					auto& tile = server.tiles[(tz + dz) * tw + tx + dx];
					if (ServerGameObject* b = tile.building.getFrom<Server>(); b && !b->deleted && setFire >= 0
						&& buildingsDone.insert(b->id).second && Rand01() < 0.05f) {
						SrvScriptContext ctx(&server, b);
						auto _ = ctx.change(ctx.sequenceExecutor, b);
						server.gameSet->actionSequences[setFire].run(&ctx);
						stats.buildingsFired++;
					}
					for (const auto& ref : tile.objList)
						if (ServerGameObject* o = ref.getFrom<Server>(); o && !o->deleted && o->blueprint->bpClass == Tags::GAMEOBJCLASS_CHARACTER
							&& !o->blueprint->canWalkOnWater() && (o->position - f.pos).sqlen2xz() < 4.5f * 4.5f)
							hurt.emplace_back(o, 0.04f);
				}
		i++;
	}
	for (const auto& [at, gen] : spreadTo)
		igniteAt(server, at, 20.0f + 15.0f * Rand01(), (int)gen);
	for (auto& [ref, frac] : hurt)
		if (ServerGameObject* o = ref.get()) {
			const bool wasAlive = hp >= 0 && o->getItem(hp) > 0.0f;
			damage(server, o, frac);
			if (ServerGameObject* u = ref.get(); wasAlive && (!u || u->getItem(hp) <= 0.0f)) {
				stats.fireDeaths++;
				countDeath(o, 3);
			}
		}
	stats.fireCellsMost = std::max(stats.fireCellsMost, (int)fires.size());
}

float ServerEnvironment::winterAhead(float now, int* severity) const
{
	if (!settings.seasons)
		return 0.0f;
	const float length = settings.seasonLengthS();
	const float elapsed = settings.seasonsElapsed(now);
	const float into = elapsed - std::floor(elapsed); // the share of the season gone
	const int season = ((int)std::floor(elapsed) + settings.startSeason) % 4;
	*severity = settings.severity[3] == 3 ? (resolvedYear >= 0 ? resolvedSeverity[3] : 1) : settings.severity[3];
	if (season == 3)
		return (1.0f - into) * length;
	if (season == 2 && into >= 0.5f)
		return length;
	return 0.0f;
}

float ServerEnvironment::coldFeel() const
{
	return state.temperatureC - (state.weather == WEATHER_BLIZZARD ? 6.0f : state.snowfall * 3.0f) - state.wetness * 2.0f;
}

float ServerEnvironment::heatFeel() const
{
	return state.temperatureC + (state.weather == WEATHER_DROUGHT ? 3.0f : 0.0f);
}

float ServerEnvironment::waterTarget() const
{
	float target = WEATHER_WATER[state.weather] * (state.severity == 0 ? 0.5f : state.severity == 2 ? 1.6f : 1.0f);
	if (state.ice > 0.5f)
		target = std::min(target, 0.0f);
	if (forceWaterRise > -999.0f)
		target = forceWaterRise;
	return target;
}

bool ServerEnvironment::onDeepWater(const Vector3& p) const
{
	const int tx = (int)(p.x / 5.0f), tz = (int)(p.z / 5.0f);
	return p.x >= 0 && p.z >= 0 && tx < gridW && tz < gridH && deepGrid[tz * gridW + tx];
}

float ServerEnvironment::floodDepthAt(const Vector3& p, float rise) const
{
	const int tx = (int)(p.x / 5.0f), tz = (int)(p.z / 5.0f);
	if (p.x < 0 || p.z < 0 || tx >= gridW || tz >= gridH)
		return 0.0f;
	const int i = floodIndex[tz * gridW + tx];
	return i < 0 ? 0.0f : std::max(0.0f, floodTiles[i].water + rise - floodTiles[i].ground);
}

std::vector<Vector3> ServerEnvironment::firePositions() const
{
	std::vector<Vector3> list;
	for (const Wildfire& f : fires)
		list.push_back(f.pos);
	return list;
}

void ServerEnvironment::countDeath(ServerGameObject* unit, int cause)
{
	if (ServerGameObject* player = unit ? unit->getPlayer() : nullptr)
		stats.deathsBy[player->id][cause]++;
}

void ServerEnvironment::damage(Server& server, ServerGameObject* obj, float fraction)
{
	const GameSet& gs = *server.gameSet;
	const bool building = obj->blueprint->bpClass == Tags::GAMEOBJCLASS_BUILDING;
	const int item = gs.items.names.getIndex(building ? "Structural Integrity" : "Hit Points");
	const int cap = gs.items.names.getIndex(building ? "Structural Integrity Capacity" : "Hit Point Capacity");
	if (item < 0)
		return;
	float capacity = cap >= 0 ? obj->getItem(cap) : 0.0f;
	if (capacity <= 0.0f)
		capacity = std::max(obj->getItem(item), 1.0f);
	obj->setItem(item, obj->getItem(item) - capacity * fraction);
	// as the scripts' own damage (Death.cpp: "Being Killed" on "Non Specific Attack")
	const int evt = gs.events.names.getIndex("Non Specific Attack");
	if (evt >= 0)
		obj->sendEvent(evt);
}

void ServerEnvironment::pickWeather(Server& server, float now)
{
	const int season = state.season;
	float weights[WEATHER_COUNT];
	float total = 0.0f;
	for (int w = 0; w < WEATHER_COUNT; w++) {
		float wt = settings.seasons ? WEATHER_WEIGHTS[season][w] : (w == WEATHER_CLEAR ? 1.0f : 0.0f);
		// (custom: only the weathers allowed in this game)
		if (!settings.weatherAllowed(w))
			wt = 0.0f;
		if (IsHarshWeather(w))
			wt *= state.severity == 0 ? 0.25f : state.severity == 2 ? 2.0f : 1.0f;
		// not the same storm twice running
		if (w == state.weather && IsHarshWeather(w))
			wt *= 0.3f;
		weights[w] = wt;
		total += wt;
	}
	float r = Rand01() * total;
	int chosen = WEATHER_CLEAR;
	for (int w = 0; w < WEATHER_COUNT; w++) {
		if (r < weights[w] || w == WEATHER_COUNT - 1) {
			chosen = w;
			if (weights[w] > 0.0f)
				break;
		}
		r -= weights[w];
	}
	// (none possible: none of the allowed weathers comes in this season, custom; clear then)
	const bool fallback = total <= 0.0f;
	if (weights[chosen] <= 0.0f)
		chosen = WEATHER_CLEAR;
	if (forceWeather >= 0 && forceWeather < WEATHER_COUNT)
		chosen = forceWeather;
	state.weather = chosen;
	float lo, hi;
	if (settings.changeRange(lo, hi))
		weatherEnds = now + lo + (hi - lo) * Rand01();
	else {
		const float length = settings.seasonLengthS() * (0.10f + 0.25f * Rand01());
		weatherEnds = now + std::clamp(length, 45.0f, 600.0f);
	}
	if (trace && fallback)
		printf("ENVIRONMENT t=%.0f no allowed weather in %s: clear\n", now, SeasonName(season));
	if (chosen == WEATHER_THUNDERSTORM)
		nextLightning = now + 4.0f + 6.0f * Rand01();
	if (trace)
		printf("ENVIRONMENT t=%.0f weather %s until %.0f (%s, %.1f C)\n", now, WeatherName(chosen), weatherEnds, SeasonName(season), state.temperatureC);
}

void ServerEnvironment::strikeLightning(Server& server)
{
	// the candidates: every player's characters and buildings, in the tree's order
	std::vector<ServerGameObject*> buildings, walls, characters;
	auto walk = [&](ServerGameObject* obj, auto& self) -> void {
		for (const auto& [type, children] : obj->children)
			for (CommonGameObject* c : children) {
				ServerGameObject* so = (ServerGameObject*)c;
				if (so->deleted || (so->flags & CommonGameObject::fTerminated))
					continue;
				const uint8_t k = kindOf(so->blueprint);
				if (so->blueprint->bpClass == Tags::GAMEOBJCLASS_BUILDING && k != K_FOUNDATION) {
					// the walls, most of the buildings, apart: lightning goes for houses and towers
					const std::string& n = so->blueprint->name;
					const bool wall = ContainsCI(n, "stockade") || ContainsCI(n, "wall") || ContainsCI(n, "paling") || ContainsCI(n, "gate");
					(wall ? walls : buildings).push_back(so);
				}
				else if (k == K_LIVING || k == K_MACHINE || k == K_UNDEAD)
					characters.push_back(so);
				self(so, self);
			}
	};
	walk(server.getLevel(), walk);
	const int r = SimRand() % 100;
	ServerGameObject* target = nullptr;
	if (r < 20 && !buildings.empty())
		target = buildings[SimRand() % buildings.size()];
	else if (r >= 20 && r < 24 && !walls.empty())
		target = walls[SimRand() % walls.size()];
	else if (r >= 24 && r < 44 && !characters.empty())
		target = characters[SimRand() % characters.size()];
	state.lightningCount++;
	if (target) {
		state.lightningPos = target->position;
		const bool isBuilding = target->blueprint->bpClass == Tags::GAMEOBJCLASS_BUILDING;
		(isBuilding ? stats.lightningBuildings : stats.lightningUnits)++;
		// the Act of God's own strike (AOG Lightning.cpp): damage, the building set on fire, the
		// characters around hurt, the effect
		const int seq = server.gameSet->actionSequences.names.getIndex("Damage Done By Direct Lightning");
		const int hpItem = server.gameSet->items.names.getIndex("Hit Points");
		const SrvGORef struck = target;
		const bool struckAlive = !isBuilding && hpItem >= 0 && target->getItem(hpItem) > 0.0f;
		ServerGameObject* struckPlayer = target->getPlayer();
		if (seq >= 0) {
			SrvScriptContext ctx(&server, target);
			auto _ = ctx.change(ctx.sequenceExecutor, target);
			server.gameSet->actionSequences[seq].run(&ctx);
		}
		else
			damage(server, target, isBuilding ? 0.3f : 1.0f);
		if (ServerGameObject* u = struck.get(); struckAlive && struckPlayer && (!u || u->getItem(hpItem) <= 0.0f))
			stats.deathsBy[struckPlayer->id][4]++;
		if (trace)
			printf("ENVIRONMENT t=%.0f lightning hits (%s) %s %u at (%.0f, %.0f)\n", state.time, seq >= 0 ? "the scripts' strike" : "own damage", target->blueprint->name.c_str(), target->id, target->position.x, target->position.z);
	}
	else {
		const auto [w, h] = server.terrain->getPlayableArea();
		const float x = Rand01() * w, z = Rand01() * h;
		state.lightningPos = Vector3(x, server.terrain->getHeight(x, z), z);
		stats.lightningGround++;
	}
	const float spacing = state.severity == 0 ? 1.8f : state.severity == 2 ? 0.6f : 1.0f;
	nextLightning = state.time + (5.0f + 15.0f * Rand01()) * spacing;
}

void ServerEnvironment::applyExposure(Server& server)
{
	// the shelters (villages and manors, keep, castle, palace): their players and places
	struct Shelter { CommonGameObject* player; Vector3 pos; };
	std::vector<Shelter> shelters;
	this->shelters.clear();
	std::vector<ServerGameObject*> living;
	auto walk = [&](ServerGameObject* obj, auto& self) -> void {
		for (const auto& [type, children] : obj->children)
			for (CommonGameObject* c : children) {
				ServerGameObject* so = (ServerGameObject*)c;
				if (so->deleted || (so->flags & CommonGameObject::fTerminated))
					continue;
				const uint8_t k = kindOf(so->blueprint);
				if (k == K_SHELTER) {
					shelters.push_back({ so->getPlayer(), so->position });
					this->shelters.push_back({ so->getPlayer() ? so->getPlayer()->id : 0u, so->position });
				}
				else if (k == K_LIVING)
					living.push_back(so);
				self(so, self);
			}
	};
	walk(server.getLevel(), walk);
	const int hp = server.gameSet->items.names.getIndex("Hit Points");
	const float T = state.temperatureC;
	const float coldFeel = this->coldFeel();
	const float heatFeel = this->heatFeel();
	std::set<uint32_t> present;
	for (ServerGameObject* unit : living) {
		CommonGameObject* player = unit->getPlayer();
		if (!player || player->blueprint->name == "Neutral Player")
			continue;
		bool sheltered = unit->parent && unit->parent->blueprint->bpClass == Tags::GAMEOBJCLASS_BUILDING;
		for (size_t i = 0; i < shelters.size() && !sheltered; i++) {
			const Shelter& s = shelters[i];
			if (s.player != player && (!s.player || server.getDiplomaticStatus(s.player, player) != 0))
				continue;
			if ((s.pos - unit->position).sqlen2xz() <= 45.0f * 45.0f)
				sheltered = true;
		}
		const bool moving = unit->movement.isMoving();
		auto it = exposure.find(unit->id);
		Exposure e = it != exposure.end() ? it->second : Exposure();
		if (sheltered) {
			e.cold = std::max(0.0f, e.cold - 5.0f);
			e.heat = std::max(0.0f, e.heat - 5.0f);
		}
		else {
			if (coldFeel < 2.0f)
				e.cold += (2.0f - coldFeel) / 6.0f * (moving ? 1.5f : 1.0f) * (1.0f + 0.5f * state.snowCover);
			else
				e.cold = std::max(0.0f, e.cold - 2.0f);
			if (heatFeel > 30.0f)
				e.heat += (heatFeel - 30.0f) / 4.0f * (moving ? 1.5f : 1.0f);
			else
				e.heat = std::max(0.0f, e.heat - 2.0f);
			e.cold = std::min(e.cold, 200.0f);
			e.heat = std::min(e.heat, 200.0f);
		}
		float frac = 0.0f;
		bool cold = false;
		if (!sheltered && e.cold > 100.0f && coldFeel < -4.0f) {
			frac = 0.004f * (-4.0f - coldFeel) / 4.0f;
			cold = true;
		}
		else if (!sheltered && e.heat > 100.0f && heatFeel > 34.0f)
			frac = 0.004f * (heatFeel - 33.0f) / 3.0f;
		if (e.cold > 0.0f || e.heat > 0.0f) {
			exposure[unit->id] = e;
			present.insert(unit->id);
		}
		else if (it != exposure.end())
			exposure.erase(it);
		if (frac > 0.0f && hp >= 0) {
			const SrvGORef ref = unit;
			const bool wasAlive = unit->getItem(hp) > 0.0f;
			damage(server, unit, frac);
			if (ServerGameObject* u = ref.get(); wasAlive && (!u || u->getItem(hp) <= 0.0f)) {
				(cold ? stats.frozen : stats.thirst)++;
				countDeath(unit, cold ? 0 : 1);
				if (trace)
					printf("ENVIRONMENT t=%.0f %s %u died of %s (%.1f C)\n", state.time, unit->blueprint->name.c_str(), ref.objid, cold ? "the cold" : "thirst", T);
			}
		}
	}
	// the units gone
	for (auto it = exposure.begin(); it != exposure.end();)
		if (!present.count(it->first))
			it = exposure.erase(it);
		else
			++it;
}

void ServerEnvironment::prepareFloods(Server& server)
{
	floodPrepared = true;
	floodTiles.clear();
	deepTiles.clear();
	Terrain* t = server.terrain;
	if (!t)
		return;
	const auto [w, h] = t->getNumPlayableTiles();
	gridW = w;
	gridH = h;
	deepGrid.assign((size_t)w * h, 0);
	floodIndex.assign((size_t)w * h, -1);
	for (int z = 0; z < h; z++)
		for (int x = 0; x < w; x++) {
			const TerrainTile* tile = t->getPlayableTile(x, z);
			if (tile && tile->fullOfWater) {
				const float bed = std::min({ t->getVertex(t->edge + x, t->edge + z), t->getVertex(t->edge + x + 1, t->edge + z),
					t->getVertex(t->edge + x, t->edge + z + 1), t->getVertex(t->edge + x + 1, t->edge + z + 1) });
				if (tile->waterLevel - bed > 1.5f) {
					deepTiles.push_back(z * w + x);
					deepGrid[z * w + x] = 1;
				}
			}
		}
	for (const FloodLand& f : FindFloodLand(t)) {
		floodIndex[f.z * w + f.x] = (int)floodTiles.size();
		floodTiles.push_back({ f.z * w + f.x, f.water, f.ground });
	}
}

void ServerEnvironment::applyFloods(Server& server)
{
	if (!floodPrepared)
		prepareFloods(server);
	stats.floodTiles = floodTiles.size();
	stats.floodedTiles = 0;
	if (!server.tiles)
		return;
	const int hp = server.gameSet->items.names.getIndex("Hit Points");
	std::set<uint32_t> buildingsDone;
	std::vector<std::pair<SrvGORef, float>> hits; // (object, fraction), applied after the walk
	// the ice: walked on from ICE_WALKABLE; who is still out on deep water once it is thinner than
	// ICE_BREAKS goes through it (a fifth of their health a second)
	// (frozen or thawed: the path finding's cached blocked tiles are looked at again)
	if (const bool frozen = state.ice >= ICE_WALKABLE; frozen != server.terrain->waterFrozen) {
		server.terrain->waterFrozen = frozen;
		server.terrain->revision++;
	}
	if (server.terrain->waterFrozen) {
		int onIce = 0;
		for (int ti : deepTiles)
			for (const auto& ref : server.tiles[ti].objList)
				if (ServerGameObject* o = ref.getFrom<Server>(); o && !o->deleted && o->blueprint->bpClass == Tags::GAMEOBJCLASS_CHARACTER && !o->blueprint->canWalkOnWater())
					onIce++;
		stats.onIceMost = std::max(stats.onIceMost, onIce);
		stats.onIceSeconds += onIce;
	}
	if (state.ice > 0.0f && state.ice < ICE_BREAKS)
		for (int ti : deepTiles)
			for (const auto& ref : server.tiles[ti].objList)
				if (ServerGameObject* o = ref.getFrom<Server>(); o && !o->deleted && o->blueprint->bpClass == Tags::GAMEOBJCLASS_CHARACTER
					&& !o->blueprint->canWalkOnWater() && (kindOf(o->blueprint) == K_LIVING || kindOf(o->blueprint) == K_MACHINE || kindOf(o->blueprint) == K_UNDEAD))
					hits.emplace_back(o, kindOf(o->blueprint) == K_UNDEAD ? 0.4f : 0.2f);
	if (state.waterRise < 0.1f && hits.empty())
		return;
	static const std::vector<FloodTile> none;
	for (const FloodTile& ft : state.waterRise < 0.1f ? none : floodTiles) {
		const float depth = ft.water + state.waterRise - ft.ground;
		if (depth <= 0.1f)
			continue;
		stats.floodedTiles++;
		auto& tile = server.tiles[ft.tile];
		if (ServerGameObject* b = tile.building.getFrom<Server>(); b && !b->deleted && buildingsDone.insert(b->id).second)
			hits.emplace_back(b, 0.0015f * std::min(depth, 2.0f));
		// drowning: in more than 1 m of water, the undead in more than 0.5 m and twice as fast
		if (depth > 0.5f && state.ice < 0.6f)
			for (const auto& ref : tile.objList)
				if (ServerGameObject* o = ref.getFrom<Server>(); o && !o->deleted && o->blueprint->bpClass == Tags::GAMEOBJCLASS_CHARACTER
					&& !o->blueprint->canWalkOnWater()) {
					const uint8_t k = kindOf(o->blueprint);
					if (k == K_UNDEAD)
						hits.emplace_back(o, 0.1f);
					else if (depth > 1.0f && (k == K_LIVING || k == K_MACHINE))
						hits.emplace_back(o, 0.05f);
				}
	}
	if (trace && simTraceCountdown-- <= 0) {
		simTraceCountdown = 30;
		size_t withBuilding = 0, withObjects = 0;
		for (const FloodTile& ft : floodTiles)
			if (ft.water + state.waterRise - ft.ground > 0.1f) {
				withBuilding += server.tiles[ft.tile].building.objid != 0;
				withObjects += !server.tiles[ft.tile].objList.empty();
			}
		printf("ENVIRONMENT t=%.0f flood: %zu tiles under water, %zu with a building, %zu with objects; %zu hits\n", state.time, stats.floodedTiles, withBuilding, withObjects, hits.size());
	}
	for (auto& [ref, frac] : hits)
		if (ServerGameObject* o = ref.get()) {
			const bool building = o->blueprint->bpClass == Tags::GAMEOBJCLASS_BUILDING;
			const bool wasAlive = building || (hp >= 0 && o->getItem(hp) > 0.0f);
			damage(server, o, frac);
			if (building) {
				stats.floodedBuildings++;
				stats.floodDamage += frac;
			}
			else if (ServerGameObject* u = ref.get(); wasAlive && (!u || u->getItem(hp) <= 0.0f)) {
				stats.drowned++;
				countDeath(o, 2);
			}
		}
}

void ServerEnvironment::update(Server& server)
{
	if (!server.getLevel() || !server.gameSet || !server.terrain)
		return;
	// without day and night and the seasons: only the game's sky, once, for the clients to draw
	if (!enabled()) {
		if (!skySent) {
			skySent = true;
			send(server);
		}
		return;
	}
	const float now = server.timeManager.currentTime;
	if (started && now < nextUpdate)
		return;
	const float dt = started ? 1.0f : 0.0f;
	nextUpdate = now + 1.0f;
	// a new game starts at the start hour and in the start season, whatever the clock the map was
	// saved with (some levels' game time is far from 0: a winter night instead of a spring morning)
	if (!started && now > 0.0f) {
		auto frac = [](float x) { return x - std::floor(x); };
		// (a hair past: the floor of the seasons elapsed must not round to the season before)
		settings.dayShift = frac(settings.dayShift - now / settings.dayLengthS() + 1e-5f);
		settings.yearShift = frac(settings.yearShift - now / (4.0f * settings.seasonLengthS()) + 1e-5f);
	}
	state.time = now;
	state.dayFraction = EnvDayFraction(settings, now);
	state.yearFraction = EnvYearFraction(settings, now);

	if (settings.seasons) {
		const int absSeason = (int)std::floor(settings.seasonsElapsed(now)) + settings.startSeason;
		state.season = absSeason % 4;
		if (absSeason / 4 != resolvedYear) {
			resolvedYear = absSeason / 4;
			for (int s = 0; s < 4; s++)
				resolvedSeverity[s] = settings.severity[s] == 3 ? SimRand() % 3 : settings.severity[s];
		}
		state.severity = resolvedSeverity[state.season];
	}
	else {
		state.season = 0;
		state.severity = 1;
	}

	// with a set stability (not random), a weather the new season doesn't have (snow into summer)
	// gives way at once
	float lo, hi;
	const bool outOfSeason = started && settings.seasons && settings.changeRange(lo, hi) && forceWeather < 0
		&& state.weather >= 0 && state.weather < WEATHER_COUNT && WEATHER_WEIGHTS[state.season][state.weather] <= 0.0f;
	if (outOfSeason && trace)
		printf("ENVIRONMENT t=%.0f %s ends with the season (%s)\n", now, WeatherName(state.weather), SeasonName(state.season));
	if (!started || now >= weatherEnds || outOfSeason) {
		if (settings.seasons)
			pickWeather(server, now);
		else {
			// (without seasons: clear, or the debug panel's weather)
			state.weather = forceWeather >= 0 && forceWeather < WEATHER_COUNT ? forceWeather : WEATHER_CLEAR;
			weatherEnds = 1e30f;
		}
	}

	// the temperature: the season's mean (a smooth curve through the seasons' middles), the day's
	// swing, the weather's pull, eased towards
	const SunState sun = ComputeSun(state.dayFraction, state.yearFraction, settings.seasons, state.cloudCover);
	{
		static const float means[4] = { 12.0f, 24.0f, 11.0f, -3.0f };
		static const float swing[4] = { 6.0f, 7.0f, 5.0f, 4.0f };
		const float x = state.yearFraction * 4.0f - 0.5f;
		const int i = (int)std::floor(x);
		const float f = x - i;
		const float a = means[(i % 4 + 4) % 4], b = means[((i + 1) % 4 + 4) % 4];
		float target = a + (b - a) * (1.0f - std::cos(PI * f)) * 0.5f;
		const float scale = SEVERITY_SCALE[std::clamp(state.severity, 0, 2)];
		target = 11.0f + (target - 11.0f) * scale;
		if (settings.dayNight)
			target += swing[state.season] * (1.0f - 0.5f * state.cloudCover) * std::sin(2.0f * PI * (state.dayFraction - 0.375f));
		target += WEATHER_TEMPERATURE[state.weather] * (WEATHER_TEMPERATURE[state.weather] > 0 ? scale : 1.0f);
		// no hotter than 40 degrees (a harsh summer's heatwave in the afternoon would reach 48):
		// the game's lands are temperate
		target = std::min(target, 40.0f);
		temperatureTarget = target;
		// (at once also after the debug panel moved the clock or held a weather: a jump from
		// summer to winter was 29 degrees and its blizzard rain for a minute and a half)
		if (!started || snapTemperature)
			state.temperatureC = target;
		else
			// (quicker with short seasons: a twentieth of a season, 10 to 90 s)
			state.temperatureC += (target - state.temperatureC) * (1.0f - std::exp(-dt / std::clamp(settings.seasonLengthS() / 20.0f, 10.0f, 90.0f)));
	}
	snapTemperature = false;
	const float T = state.temperatureC;

	// what is drawn: clouds, rain or snow (by the temperature), fog
	{
		const WeatherLook& look = WEATHER_LOOK[state.weather];
		// (snow and blizzards fall as snow whatever the temperature: a held weather may come
		// before the cold)
		const float snowShare = state.weather == WEATHER_SNOW || state.weather == WEATHER_BLIZZARD ? 1.0f
			: T < 0.5f ? 1.0f : T > 2.5f ? 0.0f : (2.5f - T) / 2.0f;
		const float step = started ? 0.05f : 1.0f;
		state.cloudCover = Approach(state.cloudCover, look.cloud, step, step);
		state.rain = Approach(state.rain, look.precip * (1.0f - snowShare), step, step);
		state.snowfall = Approach(state.snowfall, look.precip * snowShare, step, step);
		state.fog = Approach(state.fog, look.fog, step * 0.5f, step * 0.5f);
	}

	// the ground and the water
	if (settings.seasons && dt > 0.0f) {
		float dry = std::max(0.0f, T + 5.0f) / 3000.0f * (0.5f + sun.daylight);
		if (state.weather == WEATHER_HEATWAVE || state.weather == WEATHER_DROUGHT)
			dry *= 3.0f;
		state.wetness += state.rain / 90.0f - dry;
		if (state.snowfall > 0.05f && T < 1.0f)
			state.snowCover += state.snowfall / 200.0f * (state.weather == WEATHER_BLIZZARD ? 2.0f : 1.0f);
		if (T > 1.0f) {
			const float melt = std::min(state.snowCover, (T - 1.0f) / 400.0f + state.rain / 150.0f);
			state.snowCover -= melt;
			state.wetness += melt * 0.5f;
		}
		state.ice += T < 0.0f ? -T / 500.0f : -(T / 250.0f + 0.002f);
		state.wetness = std::clamp(state.wetness, 0.0f, 1.0f);
		state.snowCover = std::clamp(state.snowCover, 0.0f, 1.0f);
		state.ice = std::clamp(state.ice, 0.0f, 1.0f);
		state.waterRise = Approach(state.waterRise, waterTarget(), 0.008f, 0.004f);
	}

	// sight and speed, in steps (each change restarts the moving units' walks)
	{
		const float night = settings.dayNight ? 0.45f + 0.55f * sun.daylight : 1.0f;
		const float weather = (1.0f - 0.5f * state.fog) * (1.0f - 0.2f * state.rain) * (1.0f - 0.35f * state.snowfall);
		state.sightFactor = std::clamp(Quantize(night * weather, 0.05f), 0.3f, 1.0f);
		state.speedFactor = std::clamp(Quantize(1.0f - 0.25f * state.wetness - 0.35f * state.snowCover, 0.05f), 0.55f, 1.0f);
	}

	started = true;
	if (settings.seasons && dt > 0.0f) {
		if (state.weather == WEATHER_THUNDERSTORM && now >= nextLightning)
			strikeLightning(server);
		applyExposure(server);
		applyFloods(server);
		if (forceFireAt >= 0.0f && now >= forceFireAt) {
			forceFireAt = -1.0f;
			fireLanded(server, Vector3(forceFireX, 0, forceFireZ), true);
		}
		updateFires(server);
	}

	server.nightSight.factor = state.sightFactor;
	if (server.nightSight.active())
		server.nightSight.rebuild(&server);
	else
		server.nightSight.lit.clear(), server.nightSight.width = server.nightSight.height = 0;
	send(server);
}

void ServerEnvironment::send(Server& server, NetLink* only)
{
	NetPacketWriter msg(NETCLIMSG_ENVIRONMENT);
	msg.writeUint8((uint8_t)((settings.dayNight ? 1 : 0) | (settings.seasons ? 2 : 0)));
	msg.writeUint16((uint16_t)settings.dayLengthMin);
	msg.writeUint8((uint8_t)settings.startHour);
	msg.writeUint16((uint16_t)settings.seasonLengthMin);
	msg.writeUint8((uint8_t)settings.startSeason);
	msg.writeUint8((uint8_t)settings.tempScale);
	// the debug panel's (F9): the clock's shifts, the weather held (255: none), the severities
	msg.writeFloat(settings.dayShift);
	msg.writeFloat(settings.yearShift);
	msg.writeUint8(forceWeather >= 0 ? (uint8_t)forceWeather : 255);
	for (int s = 0; s < 4; s++)
		msg.writeUint8((uint8_t)settings.severity[s]);
	msg.writeUint8((uint8_t)settings.skyWeather);
	msg.writeUint8((uint8_t)settings.skyCover);
	// how often the weather changes, and when it does next (the debug panel's)
	msg.writeUint8((uint8_t)settings.weatherStability);
	msg.writeUint8((uint8_t)settings.weatherMinChange);
	msg.writeUint8((uint8_t)settings.weatherMaxChange);
	msg.writeUint16((uint16_t)settings.weatherTypes);
	msg.writeFloat(weatherEnds);
	// the moon's phase (every client draws the same one)
	msg.writeUint8((uint8_t)settings.moonPhase);
	msg.writeFloat(settings.moonStart);
	// the lobby's Clear Fog Of War (on: the clients show everything, no fog of war)
	msg.writeUint8(Server::clearFogOfWar ? 1 : 0);
	msg.writeUint8(settings.weatherCover < 0 ? 255 : (uint8_t)settings.weatherCover);
	state.write(msg);
	if (only)
		only->send(msg);
	else
		for (NetLink* link : server.clientLinks)
			link->send(msg);
	sendCharred(server, only);
}

// the charred trees (NETCLIMSG_CHARRED_TREES): the new ones, or all of them to a client joining,
// 60 to a packet
void ServerEnvironment::sendCharred(Server& server, NetLink* only)
{
	size_t from = only ? 0 : charredSent;
	if (!only)
		charredSent = charredOrder.size();
	bool reset = only != nullptr;
	if (from >= charredOrder.size() && !reset)
		return;
	do {
		const size_t n = std::min<size_t>(charredOrder.size() - from, 60);
		NetPacketWriter msg(NETCLIMSG_CHARRED_TREES);
		msg.writeUint8(reset ? 1 : 0);
		msg.writeUint8((uint8_t)n);
		for (size_t i = 0; i < n; i++)
			msg.writeUint32(charredOrder[from + i]);
		if (only)
			only->send(msg);
		else
			for (NetLink* link : server.clientLinks)
				link->send(msg);
		from += n;
		reset = false;
	} while (from < charredOrder.size());
}

std::string ServerEnvironment::saveText() const
{
	std::ostringstream o;
	o.precision(9);
	o << settings.text();
	o << "state " << state.time << ' ' << state.season << ' ' << state.severity << ' ' << state.weather << ' ' << state.temperatureC << ' '
		<< state.cloudCover << ' ' << state.rain << ' ' << state.snowfall << ' ' << state.fog << ' ' << state.wetness << ' ' << state.snowCover << ' '
		<< state.ice << ' ' << state.waterRise << ' ' << state.sightFactor << ' ' << state.speedFactor << ' ' << state.lightningCount << '\n';
	for (const Wildfire& f : fires)
		o << "fire " << f.pos.x << ' ' << f.pos.y << ' ' << f.pos.z << ' ' << f.born << ' ' << f.life << ' ' << f.visual.objid << ' ' << f.gen << '\n';
	if (!burnt.empty()) {
		o << "burnt";
		for (int t : burnt)
			o << ' ' << t;
		o << '\n';
	}
	if (!charred.empty()) {
		o << "charred";
		for (uint32_t id : charredOrder)
			o << ' ' << id;
		o << '\n';
	}
	o << "timers " << nextUpdate << ' ' << weatherEnds << ' ' << nextLightning << ' ' << resolvedYear << ' ' << resolvedSeverity[0] << ' '
		<< resolvedSeverity[1] << ' ' << resolvedSeverity[2] << ' ' << resolvedSeverity[3] << ' ' << (int)started << '\n';
	for (const auto& [id, e] : exposure)
		o << "exposure " << id << ' ' << e.cold << ' ' << e.heat << '\n';
	return o.str();
}

void ServerEnvironment::loadText(const std::string& text)
{
	reset();
	std::istringstream in(text);
	std::string line;
	while (std::getline(in, line)) {
		std::istringstream l(line);
		const size_t eq = line.find('=');
		if (eq != std::string::npos) {
			settings.apply(line.substr(0, eq), (float)atof(line.c_str() + eq + 1));
			continue;
		}
		std::string tag;
		l >> tag;
		if (tag == "state")
			l >> state.time >> state.season >> state.severity >> state.weather >> state.temperatureC >> state.cloudCover >> state.rain >> state.snowfall
				>> state.fog >> state.wetness >> state.snowCover >> state.ice >> state.waterRise >> state.sightFactor >> state.speedFactor >> state.lightningCount;
		else if (tag == "timers") {
			int s = 0;
			l >> nextUpdate >> weatherEnds >> nextLightning >> resolvedYear >> resolvedSeverity[0] >> resolvedSeverity[1] >> resolvedSeverity[2] >> resolvedSeverity[3] >> s;
			started = s != 0;
		}
		else if (tag == "fire") {
			Wildfire f;
			uint32_t id = 0;
			l >> f.pos.x >> f.pos.y >> f.pos.z >> f.born >> f.life >> id >> f.gen;
			f.visual = id;
			fires.push_back(f);
		}
		else if (tag == "burnt") {
			int t;
			while (l >> t)
				burnt.insert(t);
		}
		else if (tag == "charred") {
			uint32_t id;
			while (l >> id)
				if (charred.insert(id).second)
					charredOrder.push_back(id);
		}
		else if (tag == "exposure") {
			uint32_t id = 0;
			Exposure e;
			l >> id >> e.cold >> e.heat;
			exposure[id] = e;
		}
	}
}

void ServerEnvironment::debugSet(Server& server, const std::string& name, float value)
{
	const float now = server.timeManager.currentTime;
	auto frac = [](float x) { return x - std::floor(x); };
	if (name == "sunrise" || name == "sunset") {
		// hours after this season's sunrise or sunset (before it when negative): test runs reach
		// dawn and dusk on any day of the year, where a fixed hour can land in the night
		const SunState sun = ComputeSun(EnvDayFraction(settings, now), EnvYearFraction(settings, now), settings.seasons, state.cloudCover);
		const float h = (name == "sunrise" ? sun.sunrise : sun.sunset) + value;
		printf("ENVIRONMENT debug t=%.0f: %s %+g h (sunrise %.2f, sunset %.2f)\n", now, name.c_str(), value, sun.sunrise, sun.sunset);
		debugSet(server, "hour", h - 24.0f * std::floor(h / 24.0f));
		return;
	}
	if (name == "hour") {
		// the shift that puts the clock at that hour now
		const float current = frac(settings.startHour / 24.0f + settings.dayShift + now / settings.dayLengthS());
		settings.dayShift = frac(settings.dayShift + std::clamp(value, 0.0f, 24.0f) / 24.0f - current);
	}
	else if (name == "season") {
		const float current = frac(settings.startSeason * 0.25f + settings.yearShift + now / (4.0f * settings.seasonLengthS()));
		settings.yearShift = frac(settings.yearShift + std::clamp(value, 0.0f, 4.0f) / 4.0f - current);
	}
	else if (name == "weather") {
		forceWeather = value >= 0.0f && value < WEATHER_COUNT ? (int)value : -1;
		weatherEnds = 0.0f; // chosen again at the next update: the held one, or a new one
	}
	else if (!settings.apply(name, value))
		return;
	printf("ENVIRONMENT debug t=%.0f: %s = %g\n", now, name.c_str(), value);
	if (!enabled()) {
		// all off: the map's own look, open water, full sight
		if (server.terrain && server.terrain->waterFrozen) {
			server.terrain->waterFrozen = false;
			server.terrain->revision++;
		}
		server.nightSight = NightSight();
		send(server);
		return;
	}
	nextUpdate = now; // worked out at the next step, and sent
	if (name == "hour" || name == "season" || name == "weather")
		snapTemperature = true;
	send(server);
}
