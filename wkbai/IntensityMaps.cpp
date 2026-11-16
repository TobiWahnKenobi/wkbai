// wkbai - intensity maps as WKB.exe's (IntensityMaps.h)

#include "IntensityMaps.h"
#include "server.h"
#include "terrain.h"
#include "tags.h"
#include "gameset/gameset.h"
#include "gameset/ScriptContext.h"
#include "gameset/values.h"
#include <algorithm>
#include <cmath>
#include <sstream>

bool ObjectSeenByPlayer(ScriptContext* ctx, CommonGameObject* obj, CommonGameObject* player); // values.cpp

void IntensityMaps::clear()
{
	players.clear();
	switchedOn.clear();
	costCache.clear();
	width = height = 0;
}

void IntensityMaps::update(Server* server)
{
	costCache.clear();
	if (!server->terrain)
		return;
	std::tie(width, height) = server->terrain->getNumPlayableTiles();
	// the players with maps, and the writers
	std::vector<ServerGameObject*> withMaps, writers;
	for (const auto& [id, cobj] : server->idmap) {
		if (cobj->flags & CommonGameObject::fTerminated)
			continue;
		const GameObjBlueprint* bp = cobj->blueprint;
		if (bp->bpClass == Tags::GAMEOBJCLASS_PLAYER && !bp->createsIntensityMaps.empty())
			withMaps.push_back((ServerGameObject*)cobj);
		else if (!bp->intensityWriters.empty())
			writers.push_back((ServerGameObject*)cobj);
	}
	if (withMaps.empty()) {
		players.clear();
		return;
	}
	SrvScriptContext ctx(server, nullptr);
	for (ServerGameObject* player : withMaps) {
		auto& maps = players[player->id];
		for (const std::string& name : player->blueprint->createsIntensityMaps)
			maps[name];
		// what it has seen (for good), then its maps from the writers seen that still stand
		for (ServerGameObject* w : writers) {
			ServerGameObject* owner = w->getPlayer();
			if (owner == player)
				continue;
			bool seen = false, looked = false;
			for (const auto& wr : w->blueprint->intensityWriters) {
				auto it = maps.find(wr.map);
				if (it == maps.end() || it->second.discovered.count(w->id))
					continue;
				if (!looked) {
					looked = true;
					seen = ObjectSeenByPlayer(&ctx, w, player);
				}
				if (seen)
					it->second.discovered.insert(w->id);
			}
		}
		for (auto& [name, map] : maps) {
			map.grid.clear();
			for (auto it = map.discovered.begin(); it != map.discovered.end();) {
				ServerGameObject* w = server->findObject(*it);
				if (!w || (w->flags & CommonGameObject::fTerminated)) {
					it = map.discovered.erase(it);
					continue;
				}
				++it;
				for (const auto& wr : w->blueprint->intensityWriters) {
					if (wr.map != name)
						continue;
					SrvScriptContext wctx(server, w);
					const float radius = wr.radius->eval(&wctx);
					const int strength = (int)wr.strength->eval(&wctx);
					if (strength == 0)
						continue;
					if (map.grid.empty())
						map.grid.assign((size_t)width * height, 0);
					// a filled disc of the radius in tiles, the strength on every tile (0x61e580)
					const int cx = (int)(w->position.x / 5.0f), cz = (int)(w->position.z / 5.0f);
					const int rt = std::max(0, (int)(radius / 5.0f));
					for (int z = std::max(0, cz - rt); z <= std::min(height - 1, cz + rt); z++)
						for (int x = std::max(0, cx - rt); x <= std::min(width - 1, cx + rt); x++)
							if ((x - cx) * (x - cx) + (z - cz) * (z - cz) <= rt * rt) {
								int32_t& v = map.grid[(size_t)z * width + x];
								v = (int32_t)std::min<int64_t>(1 << 24, (int64_t)v + strength);
							}
				}
			}
		}
	}
	// the players gone
	for (auto it = players.begin(); it != players.end();) {
		ServerGameObject* p = server->findObject(it->first);
		it = (!p || (p->flags & CommonGameObject::fTerminated)) ? players.erase(it) : std::next(it);
	}
	for (auto it = switchedOn.begin(); it != switchedOn.end();) {
		ServerGameObject* o = server->findObject(it->first);
		it = (!o || (o->flags & CommonGameObject::fTerminated)) ? switchedOn.erase(it) : std::next(it);
	}
}

void IntensityMaps::switchOn(ServerGameObject* obj, const std::string& map)
{
	const auto& allowed = obj->blueprint->intermittentIntensityMaps;
	if (std::find(allowed.begin(), allowed.end(), map) != allowed.end())
		switchedOn[obj->id].insert(map);
}

void IntensityMaps::switchOff(ServerGameObject* obj, const std::string& map)
{
	if (auto it = switchedOn.find(obj->id); it != switchedOn.end()) {
		it->second.erase(map);
		if (it->second.empty())
			switchedOn.erase(it);
	}
}

const std::vector<int32_t>* IntensityMaps::costFor(ServerGameObject* obj)
{
	const GameObjBlueprint* bp = obj->blueprint;
	if (players.empty() || (bp->appliesIntensityMaps.empty() && bp->intermittentIntensityMaps.empty()))
		return nullptr;
	ServerGameObject* player = obj->getPlayer();
	auto pit = player ? players.find(player->id) : players.end();
	if (pit == players.end())
		return nullptr;
	std::vector<std::string> applied = bp->appliesIntensityMaps;
	if (auto sit = switchedOn.find(obj->id); sit != switchedOn.end())
		applied.insert(applied.end(), sit->second.begin(), sit->second.end());
	if (applied.empty())
		return nullptr;
	std::sort(applied.begin(), applied.end());
	applied.erase(std::unique(applied.begin(), applied.end()), applied.end());
	std::string key;
	for (const auto& a : applied)
		key += a + '\n';
	auto [cit, added] = costCache.try_emplace({ player->id, key });
	if (added) {
		for (const auto& a : applied) {
			auto mit = pit->second.find(a);
			if (mit == pit->second.end() || mit->second.grid.empty())
				continue;
			if (cit->second.empty())
				cit->second.assign(mit->second.grid.size(), 0);
			// a value of 10 (a tower's) costs one straight step (100) more; at most 1000 steps
			for (size_t i = 0; i < mit->second.grid.size(); i++)
				cit->second[i] = (int32_t)std::min<int64_t>(100000, cit->second[i] + (int64_t)mit->second.grid[i] * 10);
		}
	}
	return cit->second.empty() ? nullptr : &cit->second;
}

std::string IntensityMaps::saveText() const
{
	std::ostringstream out;
	for (const auto& [pid, maps] : players)
		for (const auto& [name, map] : maps) {
			out << "DISCOVERED " << pid << " \"" << name << "\"";
			for (uint32_t id : map.discovered)
				out << ' ' << id;
			out << '\n';
		}
	for (const auto& [oid, maps] : switchedOn)
		for (const auto& name : maps)
			out << "SWITCHED_ON " << oid << " \"" << name << "\"\n";
	return out.str();
}

void IntensityMaps::loadText(Server* server, const std::string& text)
{
	clear();
	std::istringstream in(text);
	std::string line;
	while (std::getline(in, line)) {
		std::istringstream ls(line);
		std::string what;
		uint32_t id = 0;
		ls >> what >> id;
		const size_t q1 = line.find('"'), q2 = q1 == std::string::npos ? q1 : line.find('"', q1 + 1);
		if (q2 == std::string::npos)
			continue;
		const std::string name = line.substr(q1 + 1, q2 - q1 - 1);
		std::istringstream rest(line.substr(q2 + 1));
		if (what == "DISCOVERED") {
			auto& map = players[id][name];
			uint32_t w;
			while (rest >> w)
				map.discovered.insert(w);
		}
		else if (what == "SWITCHED_ON")
			switchedOn[id].insert(name);
	}
	update(server);
}
