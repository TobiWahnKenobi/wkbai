// wkbai - a map's named regions (forests, river banks, plains, mountains, ...), for the timeline

#include "MapRegions.h"
#include "../client.h"
#include "../terrain.h"
#include "../TrnTextureDb.h"
#include "../tags.h"
#include "../file.h"
#include "../gameset/gameset.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <map>
#include <set>

namespace {
	// Names per kind of region: Tolkien and Warhammer sounding, Norse, Anglo-Saxon, Celtic and
	// Pictish. Single words, so that an event reads "Battle of Ergor".
	const std::vector<const char*> g_names[MapRegions::KindCount] = {
		// Plains
		{ "Ergor", "Harrowmere", "Eastmarch", "Calenhad", "Thornfield", "Wyrmsend", "Anfalas", "Duncraig", "Ostrava",
		  "Aldmoor", "Kerneth", "Brannoch", "Edoras", "Middlewold", "Sigmarsfeld", "Oxenford", "Lughmoor", "Talorcan" },
		// Meadows
		{ "Eofordun", "Greenhollow", "Lindon", "Maelor", "Fennwic", "Arnoch", "Bryngold", "Haldmeadow", "Sunlea",
		  "Ithilmead", "Cynewald", "Ethelmere", "Brightwater", "Talamh", "Aelfric", "Merrowvale" },
		// Forest
		{ "Mirkwold", "Drakwald", "Fangorn", "Laurelind", "Myrkvid", "Ironwood", "Elderholt", "Coedwig", "Darkhollow",
		  "Taur-nu-Fuin", "Skogheim", "Gloomwood", "Weirwold", "Athelney", "Caledon", "Ravenholt", "Grimwald", "Ashenhurst" },
		// Hills
		{ "Weathertop", "Barrowdown", "Hrothgar", "Carn Dûm", "Emyn Arnen", "Bryn Celli", "Haldorn", "Stonehelm",
		  "Craigmore", "Tor Sigurd", "Brannagh", "Ettenmoor", "Wulfhill", "Dunadd", "Ironbrow", "Ashdown" },
		// Mountains
		{ "Karak Ungor", "Caradhras", "Jotunheim", "Grimspire", "Ered Luin", "Ben Nevin", "Hammerfell", "Fenrisfang",
		  "Zirakzigil", "Drakenhorn", "Thrymheim", "Cairn Gorm", "Skarrnfell", "Ironcrown", "Gunnarstind", "Drumalban" },
		// Marsh
		{ "Nindalf", "Fenmoor", "Grendelmere", "Dagorlad", "Myrrhmarsh", "Saltmere", "Bogmoor", "Ealdfen", "Glenfenn",
		  "Midgewater", "Wormfen", "Hollowfen", "Morass of Crai", "Blackmere", "Swartmyr", "Lochan Dubh" },
		// Desert
		{ "Harad", "Khemri", "Ashlands", "Zandri", "Sunscar", "Brennholt", "Dustmarch", "Askerin", "Gorgoroth",
		  "Ka-Sabar", "Sandmere", "Emberwaste", "Lybaras", "Rasetra" },
		// Shore
		{ "Anduin's Reach", "Belfalas", "Strandmere", "Wyrmbank", "Merecliff", "Aberhafen", "Inverlochy", "Sealand",
		  "Brimwic", "Tiderun", "Lochside", "Ravenshore", "Eastbank", "Hythe", "Dunholme", "Ninian's Strand" },
		// Water
		{ "Nen Hithoel", "Lake Evendim", "Sigurd's Mere", "the Greyflood", "the Bruinen", "Loch Talorc", "Ravenwater",
		  "the Isen", "Wyrmwater", "the Afon Du", "Glassmere", "the Lune", "Mere of Dead Faces", "Silverlode" },
		// Sea
		{ "the Sea of Argoth", "the Sea of Claws", "Belegaer", "the Sea of Rhûn", "the Ymir Deep", "the Whale Road",
		  "Muir Mhanainn", "the Sea of Nurn", "the Grey Haven Sound", "the Saltmere Deep", "the Sea of Dreams", "Hafsbotn" },
	};

	uint64_t Fnv(const void* data, size_t n, uint64_t h)
	{
		for (size_t i = 0; i < n; i++) { h ^= ((const uint8_t*)data)[i]; h *= 1099511628211ull; }
		return h;
	}

	// a terrain type's kind of region (Terrains.cpp's TERRAIN blueprints)
	MapRegions::Kind KindOfTerrain(const std::string& name)
	{
		static const std::map<std::string, MapRegions::Kind> kinds = {
			{ "Water", MapRegions::Water }, { "Shallow Water", MapRegions::Water }, { "River Bed", MapRegions::Water },
			{ "Ford", MapRegions::Water },
			{ "Shore", MapRegions::Shore }, { "Coastline", MapRegions::Shore }, { "Grass 'n' Water", MapRegions::Shore },
			{ "Mountains", MapRegions::Mountains }, { "Rock", MapRegions::Mountains }, { "Desert Mountain", MapRegions::Mountains },
			{ "Snow", MapRegions::Mountains },
			{ "Hills", MapRegions::Hills }, { "Rocky Grass", MapRegions::Hills }, { "Earth 'n' Rock", MapRegions::Hills },
			{ "Rocky Grass 'n' Earth", MapRegions::Hills }, { "Grass 'n' Rocky Grass", MapRegions::Hills },
			{ "Forest", MapRegions::Forest }, { "Grass 'n' Forest", MapRegions::Forest },
			{ "Sand", MapRegions::Desert }, { "Desert Rock", MapRegions::Desert },
			{ "Swamp", MapRegions::Marsh }, { "Mud", MapRegions::Marsh },
			{ "Fertile Grasses", MapRegions::Meadows },
		};
		auto it = kinds.find(name);
		return it != kinds.end() ? it->second : MapRegions::Plains;
	}
}

const char* MapRegions::kindName(Kind kind)
{
	static const char* names[KindCount] = { "plains", "meadows", "forest", "hills", "mountains", "marsh", "desert", "shore", "water", "sea" };
	return (kind >= 0 && kind < KindCount) ? names[kind] : "?";
}

const MapRegions::Region* MapRegions::at(const Vector3& position) const
{
	const int cx = (int)(position.x / (5.0f * CELL)), cz = (int)(position.z / (5.0f * CELL));
	if (cx < 0 || cz < 0 || cx >= width || cz >= height)
		return nullptr;
	const int r = cells[cz * width + cx];
	return (r >= 0 && r < (int)regions.size()) ? &regions[r] : nullptr;
}

bool MapRegions::build(Client* client)
{
	Terrain* trn = client ? client->terrain : nullptr;
	if (!trn || !client->gameSet || !client->getLevel())
		return false;
	const GameSet& gs = *client->gameSet;
	const auto [tw, th] = trn->getNumPlayableTiles();
	// the trees standing now, per tile
	std::vector<uint8_t> trees((size_t)tw * th, 0);
	auto walk = [&](CommonGameObject* obj, auto& self) -> void {
		for (const auto& [type, children] : obj->children)
			for (CommonGameObject* o : children) {
				if (o->blueprint->bpClass == Tags::GAMEOBJCLASS_CONTAINER && o->blueprint->name.find("Tree") != std::string::npos) {
					const int tx = (int)(o->position.x / 5.0f), tz = (int)(o->position.z / 5.0f);
					if (tx >= 0 && tz >= 0 && tx < tw && tz < th && trees[tz * tw + tx] < 255)
						trees[tz * tw + tx]++;
				}
				self(o, self);
			}
	};
	walk(client->getLevel(), walk);

	// each tile's kind, and the hash of what decides it
	std::vector<Kind> kinds((size_t)tw * th, Plains);
	uint64_t h = 1469598103934665603ull;
	h = Fnv(&tw, sizeof(tw), h);
	h = Fnv(&th, sizeof(th), h);
	float lowest = 1e30f, highestH = -1e30f;
	for (int z = 0; z <= th; z++)
		for (int x = 0; x <= tw; x++) {
			const float v = trn->getVertex(x + trn->edge, z + trn->edge);
			lowest = std::min(lowest, v);
			highestH = std::max(highestH, v);
			h = Fnv(&v, sizeof(v), h);
		}
	const float mountainLine = lowest + 0.75f * (highestH - lowest);
	for (int z = 0; z < th; z++)
		for (int x = 0; x < tw; x++) {
			const TerrainTile* tile = trn->getPlayableTile(x, z);
			Kind k = Plains;
			if (tile && tile->texture && tile->texture->grp) {
				auto it = gs.associatedTileTexGroups.find(tile->texture->grp->name);
				if (it != gs.associatedTileTexGroups.end())
					k = KindOfTerrain(gs.terrains.getString(it->second));
				h = Fnv(tile->texture->grp->name.data(), tile->texture->grp->name.size(), h);
			}
			if (tile && tile->fullOfWater)
				k = Water;
			const float y = trn->getVertex(x + trn->edge, z + trn->edge);
			if (k != Water && y > mountainLine && highestH - lowest > 20.0f)
				k = Mountains;
			if ((k == Plains || k == Meadows || k == Hills) && trees[z * tw + x] >= 1)
				k = Forest;
			kinds[z * tw + x] = k;
			h = Fnv(&trees[z * tw + x], 1, h);
		}
	// land next to water (within 2 tiles) is shore
	std::vector<Kind> withShore = kinds;
	for (int z = 0; z < th; z++)
		for (int x = 0; x < tw; x++) {
			Kind& k = withShore[z * tw + x];
			if (k == Water || k == Mountains)
				continue;
			for (int dz = -2; dz <= 2 && k != Shore; dz++)
				for (int dx = -2; dx <= 2; dx++) {
					const int nx = x + dx, nz = z + dz;
					if (nx >= 0 && nz >= 0 && nx < tw && nz < th && kinds[nz * tw + nx] == Water) {
						k = Shore;
						break;
					}
				}
		}

	const std::string name = PathToUtf8(std::filesystem::path(trn->filePath).stem());
	const std::filesystem::path file = g_userPath / "saved" / "Regions" / PathFromUtf8(name + ".json");
	mapName = name;
	hash = h;
	if (load(file))
		return true;

	// the cells' kinds: the most tiles of a kind (forest and water count twice: a wood or a lake
	// in a cell makes it that)
	width = (tw + CELL - 1) / CELL;
	height = (th + CELL - 1) / CELL;
	std::vector<Kind> cellKind((size_t)width * height, Plains);
	for (int cz = 0; cz < height; cz++)
		for (int cx = 0; cx < width; cx++) {
			int count[KindCount] = {};
			for (int z = cz * CELL; z < std::min(th, (cz + 1) * CELL); z++)
				for (int x = cx * CELL; x < std::min(tw, (cx + 1) * CELL); x++) {
					const Kind k = withShore[z * tw + x];
					count[k] += (k == Forest || k == Water) ? 2 : 1;
				}
			int best = 0;
			for (int k = 1; k < KindCount; k++)
				if (count[k] > count[best])
					best = k;
			cellKind[cz * width + cx] = (Kind)best;
		}
	// regions: neighbouring cells of a kind
	cells.assign((size_t)width * height, -1);
	std::vector<Kind> regionKind;
	for (int i = 0; i < width * height; i++) {
		if (cells[i] >= 0)
			continue;
		const int r = (int)regionKind.size();
		regionKind.push_back(cellKind[i]);
		std::vector<int> todo{ i };
		cells[i] = r;
		while (!todo.empty()) {
			const int c = todo.back();
			todo.pop_back();
			const int cx = c % width, cz = c / width;
			const int nb[4][2] = { { cx - 1, cz }, { cx + 1, cz }, { cx, cz - 1 }, { cx, cz + 1 } };
			for (const auto& n : nb)
				if (n[0] >= 0 && n[1] >= 0 && n[0] < width && n[1] < height) {
					const int j = n[1] * width + n[0];
					if (cells[j] < 0 && cellKind[j] == cellKind[i]) {
						cells[j] = r;
						todo.push_back(j);
					}
				}
		}
	}
	// small regions (under 4 cells) join the neighbour they border most, smallest first, until
	// none is left
	for (bool merged = true; merged;) {
		merged = false;
		std::vector<int> size(regionKind.size(), 0);
		for (int r : cells)
			size[r]++;
		int smallest = -1;
		for (int r = 0; r < (int)size.size(); r++)
			if (size[r] > 0 && size[r] < 4 && (smallest < 0 || size[r] < size[smallest]))
				smallest = r;
		if (smallest < 0)
			break;
		std::map<int, int> border;
		for (int c = 0; c < width * height; c++) {
			if (cells[c] != smallest)
				continue;
			const int cx = c % width, cz = c / width;
			const int nb[4][2] = { { cx - 1, cz }, { cx + 1, cz }, { cx, cz - 1 }, { cx, cz + 1 } };
			for (const auto& n : nb)
				if (n[0] >= 0 && n[1] >= 0 && n[0] < width && n[1] < height) {
					const int other = cells[n[1] * width + n[0]];
					if (other != smallest)
						border[other]++;
				}
		}
		if (border.empty()) {
			size[smallest] = 4; // alone on the map: kept
			break;
		}
		const int into = std::max_element(border.begin(), border.end(), [](const auto& a, const auto& b) { return a.second < b.second; })->first;
		for (int& c : cells)
			if (c == smallest)
				c = into;
		merged = true;
	}
	// renumbered in order, with their centres and names
	std::map<int, int> renumber;
	for (int& c : cells) {
		auto it = renumber.find(c);
		if (it == renumber.end())
			it = renumber.emplace(c, (int)renumber.size()).first;
		c = it->second;
	}
	regions.assign(renumber.size(), Region{});
	for (const auto& [old, r] : renumber)
		regions[r].kind = regionKind[old];
	for (int c = 0; c < width * height; c++) {
		Region& reg = regions[cells[c]];
		reg.centre += Vector3(((c % width) + 0.5f) * CELL * 5.0f, 0.0f, ((c / width) + 0.5f) * CELL * 5.0f);
		reg.cells++;
	}
	// wide water is a sea, as the renderer draws it (its sea look where the water is 6 tiles or
	// more from the shore): each water tile's distance to land, then the regions holding such
	std::vector<int> toLand((size_t)tw * th, 1 << 20);
	std::vector<int> queue;
	for (int t = 0; t < tw * th; t++)
		if (kinds[t] != Water) {
			toLand[t] = 0;
			queue.push_back(t);
		}
	for (size_t q = 0; q < queue.size(); q++) {
		const int t = queue[q], x = t % tw, z = t / tw;
		const int nb[4][2] = { { x - 1, z }, { x + 1, z }, { x, z - 1 }, { x, z + 1 } };
		for (const auto& n : nb)
			if (n[0] >= 0 && n[1] >= 0 && n[0] < tw && n[1] < th) {
				const int j = n[1] * tw + n[0];
				if (toLand[j] > toLand[t] + 1) {
					toLand[j] = toLand[t] + 1;
					queue.push_back(j);
				}
			}
	}
	for (int t = 0; t < tw * th; t++)
		if (toLand[t] >= 6) {
			const int c = ((t / tw) / CELL) * width + (t % tw) / CELL;
			if (regions[cells[c]].kind == Water)
				regions[cells[c]].kind = Sea;
		}
	std::set<std::string> used;
	for (size_t r = 0; r < regions.size(); r++) {
		Region& reg = regions[r];
		reg.centre = reg.centre * (1.0f / (float)std::max(reg.cells, 1));
		const auto& list = g_names[reg.kind];
		uint64_t pick = Fnv(name.data(), name.size(), Fnv(&r, sizeof(r), 1469598103934665603ull));
		for (size_t tries = 0; tries < list.size() * 2; tries++) {
			const std::string candidate = list[(pick + tries) % list.size()];
			if (used.insert(candidate).second) {
				reg.name = candidate;
				break;
			}
		}
		if (reg.name.empty())
			reg.name = std::string(list[pick % list.size()]) + " " + std::to_string(r + 1);
	}
	save(file);
	printf("Map regions for %s: %zu regions, saved to %s\n", name.c_str(), regions.size(), PathToUtf8(file).c_str());
	return true;
}

bool MapRegions::load(const std::filesystem::path& file)
{
	std::ifstream in(file);
	if (!in)
		return false;
	const nlohmann::json j = nlohmann::json::parse(in, nullptr, false);
	if (j.is_discarded() || j.value("hash", std::string()) != std::to_string(hash) || j.value("cell", 0) != CELL)
		return false;
	width = j.value("width", 0);
	height = j.value("height", 0);
	cells = j.value("cells", std::vector<int>());
	regions.clear();
	for (const auto& r : j.value("regions", nlohmann::json::array())) {
		Region reg;
		reg.kind = (Kind)std::clamp(r.value("kind", 0), 0, KindCount - 1);
		reg.name = r.value("name", std::string());
		reg.centre = Vector3(r.value("x", 0.0f), 0.0f, r.value("z", 0.0f));
		reg.cells = r.value("cells", 0);
		regions.push_back(std::move(reg));
	}
	if ((int)cells.size() != width * height || regions.empty()) {
		cells.clear();
		return false;
	}
	return true;
}

void MapRegions::save(const std::filesystem::path& file) const
{
	nlohmann::json j;
	j["map"] = mapName;
	j["hash"] = std::to_string(hash);
	j["cell"] = CELL;
	j["width"] = width;
	j["height"] = height;
	j["cells"] = cells;
	nlohmann::json list = nlohmann::json::array();
	for (const Region& r : regions)
		list.push_back({ { "kind", (int)r.kind }, { "kindName", kindName(r.kind) }, { "name", r.name }, { "x", r.centre.x }, { "z", r.centre.z }, { "cells", r.cells } });
	j["regions"] = list;
	std::error_code ec;
	std::filesystem::create_directories(file.parent_path(), ec);
	std::ofstream(file) << j.dump(1);
}
