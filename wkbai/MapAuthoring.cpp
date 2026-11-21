// wkbai - the catalog and the description of a map for an agent designing maps (MapAuthoring.h)
// Licensed under the GNU General Public License 3

#include "MapAuthoring.h"
#include "MapFolder.h"
#include "MapMaterials.h"
#include "terrain.h"
#include "server.h"
#include "file.h"
#include "settings.h"
#include "tags.h"
#include "Environment.h"
#include "TrnTextureDb.h"
#include "WaterDesign.h"
#include "gfx/bitmap.h"
#include "gameset/gameset.h"
#include "gameset/Footprint.h"
#include "gameset/GSTerrain.h"
#include "util/StriCompare.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#include <stb_image_write.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <sstream>

namespace MapAuthoring {
namespace {
	namespace fs = std::filesystem;
	using json = nlohmann::ordered_json;

	const char* SIDES[4] = { "east", "north", "south", "west" }; // Tags::MAPTEXDIR_*

	std::string Lower(std::string s)
	{
		for (char& c : s)
			c = (char)tolower((unsigned char)c);
		return s;
	}
	bool EndsWithCI(const std::string& s, const char* end)
	{
		const size_t n = strlen(end);
		return s.size() >= n && StrCICompare(std::string_view(s).substr(s.size() - n), end) == 0;
	}
	std::string ClassName(int c) { return Lower(Tags::GAMEOBJCLASS_tagDict.tags[c]); }
	// the classes whose objects stand on the map at a place, as a level's world places them
	bool Placeable(int c)
	{
		return c == Tags::GAMEOBJCLASS_BUILDING || c == Tags::GAMEOBJCLASS_CHARACTER || c == Tags::GAMEOBJCLASS_CONTAINER
			|| c == Tags::GAMEOBJCLASS_MARKER || c == Tags::GAMEOBJCLASS_PROP;
	}
	// the classes a level has as its structure (no place of their own): players, armies, cities, towns, zones
	bool Structural(int c)
	{
		return c == Tags::GAMEOBJCLASS_PLAYER || c == Tags::GAMEOBJCLASS_ARMY || c == Tags::GAMEOBJCLASS_CITY
			|| c == Tags::GAMEOBJCLASS_TOWN || c == Tags::GAMEOBJCLASS_TERRAIN_ZONE || c == Tags::GAMEOBJCLASS_FORMATION;
	}
	// the resource items: the tribute items and the four the game's economy runs on
	std::set<int> ResourceItems(const GameSet& gs)
	{
		std::set<int> r;
		for (int i : gs.tributeItems)
			if (i >= 0 && i < (int)gs.items.size())
				r.insert(i);
		for (const char* name : { "FOOD", "WOOD", "GOLD", "STONE" })
			if (const int i = gs.items.names.getIndex(name); i >= 0)
				r.insert(i);
		return r;
	}
	json Rgb(double r, double g, double b) { return json::array({ (int)std::lround(r), (int)std::lround(g), (int)std::lround(b) }); }

	// the mean colours of the textures' pictures (the atlases, at whatever size they are)
	struct Colours {
		std::map<std::string, Bitmap> atlases;
		// the mean colour of a texture's piece; false when its picture isn't there
		bool mean(const TerrainTexture* t, double rgb[3])
		{
			auto it = atlases.find(t->file);
			if (it == atlases.end()) {
				const std::string path = "Maps\\Map_Textures\\" + t->file;
				Bitmap bm;
				if (FileExists(path.c_str()))
					bm = Bitmap::loadBitmap(path.c_str()).convertToR8G8B8A8();
				it = atlases.emplace(t->file, std::move(bm)).first;
			}
			const Bitmap& a = it->second;
			if (a.width <= 0 || a.height <= 0)
				return false;
			const double sx = a.width / 256.0, sy = a.height / 256.0;
			const int x0 = (int)(t->startx * sx), x1 = std::max(x0 + 1, (int)((t->startx + t->width) * sx));
			const int y0 = (int)(t->starty * sy), y1 = std::max(y0 + 1, (int)((t->starty + t->height) * sy));
			double s[3] = {};
			size_t n = 0;
			for (int y = std::max(y0, 0); y < std::min(y1, a.height); y++)
				for (int x = std::max(x0, 0); x < std::min(x1, a.width); x++) {
					const uint8_t* p = &a.pixels[((size_t)y * a.width + x) * 4];
					s[0] += p[0], s[1] += p[1], s[2] += p[2];
					n++;
				}
			if (!n)
				return false;
			for (int c = 0; c < 3; c++)
				rgb[c] = s[c] / n;
			return true;
		}
		// a group's: the mean of its textures'
		bool mean(const TerrainTextureGroup* g, double rgb[3])
		{
			double s[3] = {};
			int n = 0;
			for (const auto& [id, t] : g->textures) {
				double one[3];
				if (mean(t.get(), one)) {
					for (int c = 0; c < 3; c++)
						s[c] += one[c];
					n++;
				}
			}
			if (!n)
				return false;
			for (int c = 0; c < 3; c++)
				rgb[c] = s[c] / n;
			return true;
		}
	};

	// the edge rules as the database's file has them (MapMaterials.h, shared with the solver)
	using MapMaterials::EdgeRule;
	using MapMaterials::EdgeRules;
	using MapMaterials::ReadEdgeRules;

	json NamesOf(const std::vector<bool>* mask, const GameSet& gs)
	{
		json a = json::array();
		if (mask)
			for (size_t t = 0; t < mask->size() && t < gs.terrains.size(); t++)
				if ((*mask)[t])
					a.push_back(gs.terrains.getString((int)t));
		return a;
	}

	json FootprintOf(const GameSet& gs, const Footprint* fp)
	{
		if (!fp)
			return nullptr;
		json f;
		const int index = gs.footprints.getIndex(fp);
		f["name"] = index >= 0 && index < (int)gs.footprints.size() ? gs.footprints.getString(index) : "";
		f["origin"] = { fp->originX, fp->originZ };
		if (fp->maxHeightDifferential > 0.0f)
			f["maxHeightDifferentialM"] = fp->maxHeightDifferential;
		int x0 = 0, z0 = 0, x1 = -1, z1 = -1;
		json tiles = json::array();
		bool anyLeave = false, anyTerrains = false;
		for (const Footprint::TileOffset& t : fp->tiles) {
			tiles.push_back({ t.offsetX, t.offsetZ, t.mode });
			if (x1 < x0)
				x0 = x1 = t.offsetX, z0 = z1 = t.offsetZ;
			x0 = std::min(x0, t.offsetX), x1 = std::max(x1, t.offsetX);
			z0 = std::min(z0, t.offsetZ), z1 = std::max(z1, t.offsetZ);
			anyLeave |= !t.flatten;
			anyTerrains |= t.terrains != nullptr;
		}
		f["tileCount"] = fp->tiles.size();
		f["extent"] = fp->tiles.empty() ? json::array() : json::array({ x0, z0, x1, z1 });
		f["sizeTiles"] = fp->tiles.empty() ? json::array({ 0, 0 }) : json::array({ x1 - x0 + 1, z1 - z0 + 1 });
		f["tiles"] = tiles; // [dx, dz, mode] from the object's tile
		if (anyLeave)
			f["someTilesLeaveGround"] = true;
		if (anyTerrains) {
			// the terrains the tiles may stand on (the first tile with a list)
			for (const Footprint::TileOffset& t : fp->tiles)
				if (t.terrains) {
					f["standsOn"] = NamesOf(t.terrains, gs);
					break;
				}
		}
		return f;
	}

	json BlueprintOf(const GameSet& gs, const GameObjBlueprint& bp, const std::set<int>& resources)
	{
		json b;
		b["name"] = bp.name;
		b["id"] = bp.bpId;
		b["placeable"] = Placeable(bp.bpClass);
		if (!bp.displayCategory.empty())
			b["category"] = bp.displayCategory;
		if (!bp.modelPath.empty())
			b["model"] = bp.modelPath;
		if (bp.subtypeNames.size() > 0) {
			json st = json::array();
			for (size_t s = 0; s < bp.subtypeNames.size(); s++)
				st.push_back(bp.subtypeNames.getString((int)s));
			b["subtypes"] = st;
		}
		json items = json::object(), held = json::object();
		for (const auto& [item, value] : bp.startItemValues) {
			if (item < 0 || item >= (int)gs.items.size())
				continue;
			const std::string& name = gs.items.getString(item);
			items[name] = value;
			if (resources.count(item) && value > 0.0f)
				held[name] = value;
		}
		b["items"] = items;
		b["resource"] = held.empty() ? json(nullptr) : held;
		const Footprint* fp = bp.footprint;
		b["footprint"] = FootprintOf(gs, fp);
		json pass;
		if (!bp.canTraverseNames.empty())
			pass["canTraverse"] = bp.canTraverseNames;
		if (!bp.passabilityFieldsName.empty())
			pass["fields"] = bp.passabilityFieldsName;
		if (bp.bpClass == Tags::GAMEOBJCLASS_CHARACTER || !bp.canTraverseNames.empty() || !bp.passabilityFieldsName.empty())
			pass["terrains"] = NamesOf(bp.getPassableTerrains(), gs);
		if (bp.floatsOnWater)
			pass["floatsOnWater"] = true;
		if (bp.wades())
			pass["wades"] = true;
		if (bp.hasMaxGradient)
			pass["maxGradientDeg"] = bp.maxGradient;
		if (bp.canBeBuiltUpon)
			pass["canBeBuiltUpon"] = true;
		b["passability"] = pass.empty() ? json::object() : pass;
		if (bp.boundingRadius > 0.0f)
			b["radiusM"] = bp.boundingRadius;
		return b;
	}

	json EnvironmentNames()
	{
		json e;
		json w = json::array();
		for (int i = 0; i < WEATHER_COUNT; i++)
			w.push_back(Lower(WeatherName(i)));
		e["weathers"] = w;
		json s = json::array();
		for (int i = 0; i < 4; i++)
			s.push_back(Lower(SeasonName(i)));
		e["seasons"] = s;
		e["severities"] = { "gentle", "normal", "harsh", "random" };
		json st = json::array();
		for (int i = 0; i < 5; i++)
			st.push_back(Lower(WeatherStabilityName(i)));
		e["stabilities"] = st;
		json mp = json::array();
		for (int i = 0; i < 9; i++)
			mp.push_back(Lower(MoonPhaseName(i)));
		e["moonPhases"] = mp;
		e["dayStarts"] = { "dawn", "sunrise", "noon", "dusk", "night" };
		// the reusable climates (Environment\climates\*.json, base game and mods), none shipped yet
		json climates = json::array();
		if (std::unique_ptr<std::vector<std::string>> files(ListFiles("Environment\\climates")); files)
			for (const std::string& f : *files)
				if (EndsWithCI(f, ".json"))
					climates.push_back(f.substr(0, f.size() - 5));
		e["climates"] = climates;
		// what the game applies today (step 17.2); the rest is kept for step 17.13
		e["applied"] = { "dayNight.on", "dayNight.dayLengthMin", "dayNight.start (an hour)", "dayNight.moonPhase", "seasons.on",
			"seasons.seasonLengthMin", "seasons.start", "seasons.severity", "weather.stability", "weather.allowed" };
		e["keptForLater"] = { "climate", "latitude", "dayNight.start (by the sun)", "seasons.meanC", "seasons.dailySwingC", "weather.weights",
			"weather.start", "wind", "height", "areas", "fixed" };
		return e;
	}
}

json Catalog(const GameSet& gs, const std::string& gameSetFile, bool edges)
{
	json c;
	c["format"] = "wkbai catalog";
	c["version"] = CATALOG_VERSION;
	c["gameSet"] = gameSetFile;
	c["gameVersion"] = gs.version;
	json mods = json::array();
	for (const ModInfo& m : GetMods())
		mods.push_back({ { "id", m.id }, { "name", m.name }, { "kind", m.kind }, { "enabled", m.enabled }, { "logicFiles", m.logicFiles } });
	c["mods"] = mods;

	// the items, and the resources: what players can give each other (TRIBUTES: food, wood, gold)
	// and stone (not a tribute, but what the rock outcrops hold)
	const std::set<int> resources = ResourceItems(gs);
	json items = json::array(), res = json::array();
	for (size_t i = 0; i < gs.items.size(); i++)
		items.push_back(gs.items.getString((int)i));
	for (int i : resources)
		res.push_back(gs.items.getString(i));
	c["items"] = items;
	c["resources"] = res;

	// the terrains (Terrains.cpp) and which texture groups are of each
	json terrains = json::array();
	for (size_t t = 0; t < gs.terrains.size(); t++) {
		const GSTerrain& gt = gs.terrains[t];
		json ti = json::object();
		for (const auto& [item, v] : gt.itemValues)
			if (item >= 0 && item < (int)gs.items.size())
				ti[gs.items.getString(item)] = v;
		json groups = json::array();
		for (const auto& [g, ptr] : gs.associatedTileTexGroups)
			if (ptr == &gt)
				groups.push_back(g);
		terrains.push_back({ { "name", gs.terrains.getString((int)t) }, { "cost", gt.terrainCost }, { "items", ti }, { "textureGroups", groups } });
	}
	c["terrains"] = terrains;
	json pf = json::object(), tbl = json::object();
	for (const auto& [name, mask] : gs.passabilityFields)
		pf[name] = NamesOf(&mask, gs);
	for (const auto& [name, mask] : gs.terrainBlueprintLists)
		tbl[name] = NamesOf(&mask, gs);
	c["passabilityFields"] = pf;        // per field: the terrains passable for it
	c["terrainBlueprintLists"] = tbl;   // per list: the terrains a footprint's tile may stand on

	// every object blueprint by class
	json classes = json::object(), counts = json::object();
	size_t total = 0;
	for (int k = 0; k < Tags::GAMEOBJCLASS_COUNT; k++) {
		const auto& list = gs.objBlueprints[k];
		json a = json::array();
		for (size_t i = 0; i < list.size(); i++)
			a.push_back(BlueprintOf(gs, list[i], resources));
		counts[ClassName(k)] = a.size();
		total += a.size();
		json cls;
		cls["placeable"] = Placeable(k);
		if (Structural(k))
			cls["structural"] = true;
		cls["blueprints"] = a;
		classes[ClassName(k)] = cls;
	}
	c["classes"] = classes;

	// the trees: the blueprints holding wood whose model is a tree's, with their kinds (subtypes)
	json trees = json::array();
	for (int k : { Tags::GAMEOBJCLASS_CONTAINER, Tags::GAMEOBJCLASS_PROP, Tags::GAMEOBJCLASS_BUILDING }) {
		const auto& list = gs.objBlueprints[k];
		for (size_t i = 0; i < list.size(); i++) {
			const GameObjBlueprint& bp = list[i];
			const std::string model = Lower(bp.modelPath), name = Lower(bp.name);
			if (model.find("trees\\") == std::string::npos && model.find("tree") == std::string::npos && name.find("tree") == std::string::npos)
				continue;
			json t = { { "class", ClassName(k) }, { "name", bp.name } };
			json st = json::array();
			for (size_t s = 0; s < bp.subtypeNames.size(); s++)
				st.push_back(bp.subtypeNames.getString((int)s));
			t["subtypes"] = st;
			float wood = 0.0f;
			for (int r : gs.tributeItems)
				if (r >= 0 && r < (int)gs.items.size() && Lower(gs.items.getString(r)) == "wood")
					wood = bp.getStartingItemValue(r);
			t["wood"] = wood;
			if (!bp.modelPath.empty())
				t["model"] = bp.modelPath;
			trees.push_back(t);
		}
	}
	c["trees"] = trees;

	// the texture databases (Maps\Map_Textures\*.dat) and their groups
	json dbs = json::array();
	size_t groupCount = 0, textureCount = 0, ruleCount = 0;
	Colours colours;
	std::vector<std::string> dbFiles;
	if (std::unique_ptr<std::vector<std::string>> files(ListFiles("Maps\\Map_Textures")); files)
		for (const std::string& f : *files)
			if (EndsWithCI(f, ".dat"))
				dbFiles.push_back("Maps\\Map_Textures\\" + f);
	std::sort(dbFiles.begin(), dbFiles.end());
	for (const std::string& file : dbFiles) {
		TerrainTextureDatabase db(file.c_str());
		const EdgeRules er = ReadEdgeRules(file);
		json groups = json::array();
		for (const auto& [name, grp] : db.groups) {
			json g;
			g["name"] = name;
			auto at = gs.associatedTileTexGroups.find(name);
			const GSTerrain* terrain = at == gs.associatedTileTexGroups.end() ? nullptr : at->second;
			const int ti = terrain ? gs.terrains.getIndex(terrain) : -1;
			g["terrain"] = ti >= 0 ? json(gs.terrains.getString(ti)) : json(nullptr);
			if (terrain)
				g["cost"] = terrain->terrainCost;
			json passable = json::array();
			for (const auto& [field, mask] : gs.passabilityFields)
				if (ti >= 0 && ti < (int)mask.size() && mask[ti])
					passable.push_back(field);
			g["passableFor"] = passable;
			// a transition between two groups ("GRASS1-SAND1": GRASS1 on one side, SAND1 on the other)
			if (const size_t dash = name.find('-'); dash != std::string::npos && db.groups.count(name.substr(0, dash)) && db.groups.count(name.substr(dash + 1)))
				g["transition"] = { name.substr(0, dash), name.substr(dash + 1) };
			double rgb[3];
			g["meanColour"] = colours.mean(grp.get(), rgb) ? Rgb(rgb[0], rgb[1], rgb[2]) : json(nullptr);
			// the textures: id, picture and piece; MATCHING ids are alternatives of a TEXTURE
			const auto mo = er.matchingOf.find(name);
			json texs = json::array();
			for (const auto& [id, t] : grp->textures) {
				json tj = { { "id", id }, { "file", t->file }, { "rect", { t->startx, t->starty, t->width, t->height } } };
				if (mo != er.matchingOf.end())
					if (auto m = mo->second.find(id); m != mo->second.end())
						tj["matches"] = m->second;
				if (edges) {
					json ej = json::object();
					const auto gr = er.rules.find(name);
					if (gr != er.rules.end())
						if (auto tr = gr->second.find(id); tr != gr->second.end())
							for (const EdgeRule& r : tr->second) {
								if (!ej.contains(SIDES[r.side]))
									ej[SIDES[r.side]] = json::array();
								// [group, id, turn, xflip, zflip, its side facing this one]
								ej[SIDES[r.side]].push_back({ r.group, r.id, r.rot, r.xflip, r.zflip, r.otherSide >= 0 ? SIDES[r.otherSide] : "?" });
							}
					tj["edges"] = ej;
				}
				texs.push_back(tj);
			}
			g["textures"] = texs;
			// the edge rules in short: per side, the groups a texture of this one may border, and how many rules
			json sides = json::object();
			size_t rules = 0;
			std::set<std::string> borders;
			if (const auto gr = er.rules.find(name); gr != er.rules.end())
				for (const auto& [id, list] : gr->second)
					for (const EdgeRule& r : list) {
						json& s = sides[SIDES[r.side]];
						if (!s.is_object())
							s = json::object();
						s[r.group] = s.value(r.group, 0) + 1;
						borders.insert(r.group);
						rules++;
					}
			g["edgeRules"] = rules;
			g["borders"] = borders;
			g["edgesBySide"] = sides;
			groups.push_back(g);
			groupCount++;
			textureCount += grp->textures.size();
		}
		ruleCount += er.count;
		dbs.push_back({ { "file", file }, { "groups", groups }, { "groupCount", db.groups.size() }, { "edgeRules", er.count } });
	}
	c["textureDatabases"] = dbs;
	c["edgeRuleFormat"] = edges ? "per texture and side: [group, id, turn 0-3, xflip, zflip, the other texture's side facing this one]"
		: "per group and side the groups bordered and the number of rules; --edges (or edges: true) for every texture's rules";

	// the skies (Maps\Sky Textures\<name>, map.json "sky": "Maps\\Sky Textures\\<name>\\")
	json skies = json::array();
	if (std::unique_ptr<std::vector<std::string>> dirs(ListDirectories("Maps\\Sky Textures")); dirs) {
		std::sort(dirs->begin(), dirs->end());
		for (const std::string& d : *dirs)
			skies.push_back({ { "name", d }, { "path", "Maps\\Sky Textures\\" + d + "\\" } });
	}
	c["skies"] = skies;

	// the map's limits (MapFolder's compiler)
	c["mapSize"] = {
		{ "playableTilesMin", 16 }, { "playableTilesMax", 1024 }, { "tileM", 5 }, { "defaultEdgeTiles", 30 },
		{ "tiles", "size.tiles = playable + 2 * edge on each axis" },
		{ "heights", "16-bit, 256 per height step; a step is heights.scale metres; (w + 1) x (h + 1) vertices, row 0 the north edge" },
		{ "lakes", "any number" }, { "seaRegionsMax", WaterDesign::MAX_SEA_REGIONS } };
	c["environment"] = EnvironmentNames();

	json cnt = counts;
	cnt["blueprints"] = total;
	cnt["textureDatabases"] = dbFiles.size();
	cnt["textureGroups"] = groupCount;
	cnt["textures"] = textureCount;
	cnt["edgeRules"] = ruleCount;
	cnt["terrains"] = gs.terrains.size();
	cnt["items"] = gs.items.size();
	cnt["skies"] = skies.size();
	cnt["trees"] = trees.size();
	c["counts"] = cnt;
	return c;
}

namespace {
	// a player's colour on the picture (by the player's colour index)
	const uint8_t PLAYER_COLOURS[9][3] = { { 200, 200, 200 }, { 220, 40, 40 }, { 40, 90, 230 }, { 240, 220, 40 }, { 40, 200, 70 },
		{ 230, 130, 30 }, { 160, 60, 200 }, { 40, 210, 210 }, { 250, 120, 190 } };

	struct Placed {
		const ServerGameObject* obj;
		const ServerGameObject* player; // null: none above it
	};

	bool PngTo(const fs::path& path, int w, int h, const std::vector<uint8_t>& rgb)
	{
		std::string out;
		auto write = [](void* ctx, void* data, int size) { ((std::string*)ctx)->append((const char*)data, size); };
		if (!stbi_write_png_to_func(write, &out, w, h, 3, rgb.data(), w * 3))
			return false;
		std::error_code ec;
		if (path.has_parent_path())
			fs::create_directories(path.parent_path(), ec);
		std::ofstream f(path, std::ios::binary);
		f.write(out.data(), (std::streamsize)out.size());
		return (bool)f;
	}

	// the level to load for a map: a folder as it is; a level by its game path, its full path, or its name in Save_Games
	std::string LevelPath(const std::string& map, std::string& error)
	{
		if (MapFolder::IsMapPath(map)) {
			const fs::path folder = MapFolder::Resolve(map);
			if (folder.empty()) {
				error = "no map folder " + map;
				return "";
			}
			return PathToUtf8(folder);
		}
		std::error_code ec;
		if (fs::exists(PathFromUtf8(map), ec) && fs::path(PathFromUtf8(map)).is_absolute())
			return map;
		for (const std::string& p : { map, "Save_Games\\" + map, "Save_Games\\" + map + ".lvl", map + ".lvl" })
			if (FileExists(p.c_str()))
				return p;
		error = "no level " + map + " (a .wkbai folder, Save_Games\\<level>.lvl, or a full path)";
		return "";
	}
}

bool Describe(const std::string& map, const std::string& png, json& out, std::string& error)
{
	const std::string level = LevelPath(map, error);
	if (level.empty())
		return false;
	Server server;
	server.loadSaveGame(level.c_str());
	const Terrain* t = server.terrain;
	if (!t || !server.gameSet) {
		error = "the level has no map or game set";
		return false;
	}
	const GameSet& gs = *server.gameSet;
	const int W = t->width, H = t->height, E = t->edge;
	out = json::object();
	out["format"] = "wkbai map description";
	out["version"] = DESCRIBE_VERSION;
	out["map"] = map;
	out["level"] = level;
	out["terrainFile"] = t->mapFile;
	out["gameSet"] = server.gameSetFile;
	std::string db, sky;
	for (wchar_t c : t->texDbPath) if (c) db += (char)c;
	for (wchar_t c : t->skyTextureDirectory) if (c) sky += (char)c;
	out["textureDatabase"] = db;
	out["sky"] = sky;
	const int pw = W - 2 * E, ph = H - 2 * E;
	out["size"] = { { "tiles", { W, H } }, { "edge", E }, { "playableTiles", { pw, ph } }, { "playableM", { pw * 5, ph * 5 } }, { "heightScale", t->scale } };

	// the heights over the playable vertices (metres), and the whole map's range
	float lo = 1e30f, hi = -1e30f, alo = 1e30f, ahi = -1e30f;
	double sum = 0;
	size_t n = 0;
	for (int z = 0; z <= H; z++)
		for (int x = 0; x <= W; x++) {
			const float v = t->getVertex(x, z);
			alo = std::min(alo, v), ahi = std::max(ahi, v);
			if (x >= E && x <= W - E && z >= E && z <= H - E) {
				lo = std::min(lo, v), hi = std::max(hi, v);
				sum += v;
				n++;
			}
		}
	const int BINS = 16;
	std::vector<int> hist(BINS, 0);
	for (int z = E; z <= H - E; z++)
		for (int x = E; x <= W - E; x++) {
			const float v = t->getVertex(x, z);
			const int b = hi > lo ? std::min(BINS - 1, (int)((v - lo) / (hi - lo) * BINS)) : 0;
			hist[b]++;
		}
	// the slopes: per playable tile its steepest edge or diagonal
	int steep[4] = {}; // > 10, 20, 30, 45 degrees
	for (int z = E; z < H - E; z++)
		for (int x = E; x < W - E; x++) {
			const float a = t->getVertex(x, z), b = t->getVertex(x + 1, z), c = t->getVertex(x, z + 1), d = t->getVertex(x + 1, z + 1);
			const float rise = std::max({ std::abs(a - b), std::abs(c - d), std::abs(a - c), std::abs(b - d), std::abs(a - d) / 1.41421356f, std::abs(b - c) / 1.41421356f });
			const float deg = std::atan(rise / 5.0f) * 57.2957795f;
			const float lim[4] = { 10, 20, 30, 45 };
			for (int k = 0; k < 4; k++)
				steep[k] += deg > lim[k];
		}
	const double tilesP = std::max(1, pw * ph);
	out["heights"] = { { "minM", lo }, { "maxM", hi }, { "meanM", n ? sum / n : 0.0 }, { "wholeMapMinM", alo }, { "wholeMapMaxM", ahi },
		{ "histogram", { { "fromM", lo }, { "binM", (hi - lo) / BINS }, { "vertices", hist } } },
		{ "slopeShare", { { "over10deg", steep[0] / tilesP }, { "over20deg", steep[1] / tilesP }, { "over30deg", steep[2] / tilesP }, { "over45deg", steep[3] / tilesP } } } };

	// the water: the lakes (flood-filled from their points), the tiles under water, the design's waterways and falls
	int under = 0, underP = 0;
	for (int z = 0; z < H; z++)
		for (int x = 0; x < W; x++)
			if (t->tiles[z * W + x].fullOfWater) {
				under++;
				underP += x >= E && x < W - E && z >= E && z < H - E;
			}
	json lakes = json::array();
	// (the lakes' points are in whole-map metres: here in playable metres, as every position here)
	for (const Vector3& l : t->lakes)
		lakes.push_back({ { "at", { l.x - E * 5.0f, l.z - E * 5.0f } }, { "levelM", l.y } });
	json water;
	water["coordinates"] = "playable metres (x east, z north from the playable area's south-west corner), as objects' positions";
	water["lakes"] = lakes;
	water["lakeCount"] = t->lakes.size();
	water["tilesUnderWater"] = under;
	water["playableTilesUnderWater"] = underP;
	water["playableShare"] = underP / tilesP;
	if (const WaterDesign* wd = t->waterDesign.get()) {
		json ways = json::array();
		for (const WaterDesign::Waterway& w : wd->waterways) {
			double len = 0;
			for (size_t i = 1; i < w.points.size(); i++)
				len += std::hypot(w.points[i].x - w.points[i - 1].x, w.points[i].z - w.points[i - 1].z);
			json pts = json::array();
			for (const Vector3& p : w.points)
				pts.push_back({ p.x, p.z });
			ways.push_back({ { "points", pts }, { "lengthM", len }, { "widthM", w.width }, { "speedMs", w.speed } });
		}
		json falls = json::array();
		for (const WaterDesign::PlacedFall& f : wd->waterfalls)
			falls.push_back({ { "from", { f.from.x, f.from.z } }, { "to", { f.to.x, f.to.z } } });
		int river = 0, lake = 0, sea = 0;
		for (char s : wd->surface)
			river += s == WaterDesign::RIVER, lake += s == WaterDesign::LAKE, sea += WaterDesign::seaRegionOf(s) >= 0;
		json seas = json::array();
		for (const WaterDesign::SeaRegion& s : wd->seas)
			seas.push_back({ { "name", s.name }, { "windDeg", s.windDegrees }, { "waves", s.waves } });
		water["design"] = { { "waterways", ways }, { "waterfalls", falls }, { "autoWaterfalls", wd->autoWaterfalls }, { "seas", seas },
			{ "surfaceTiles", { { "river", river }, { "lake", lake }, { "sea", sea } } } };
	}
	else
		water["design"] = nullptr;
	out["water"] = water;

	// the materials: texture groups and terrains over the playable tiles
	std::map<std::string, int> byGroup, byTerrain;
	for (int z = E; z < H - E; z++)
		for (int x = E; x < W - E; x++) {
			const TerrainTexture* tex = t->tiles[z * W + x].texture;
			const std::string g = tex && tex->grp ? tex->grp->name : "-";
			byGroup[g]++;
			auto it = gs.associatedTileTexGroups.find(g);
			byTerrain[it == gs.associatedTileTexGroups.end() ? "none" : gs.terrains.getString(it->second)]++;
		}
	auto shares = [&](const std::map<std::string, int>& m) {
		std::vector<std::pair<std::string, int>> v(m.begin(), m.end());
		std::sort(v.begin(), v.end(), [](auto& a, auto& b) { return a.second != b.second ? a.second > b.second : a.first < b.first; });
		json a = json::array();
		for (auto& [k, c] : v)
			a.push_back({ { "name", k }, { "tiles", c }, { "share", c / tilesP } });
		return a;
	};
	out["materials"] = { { "textureGroups", shares(byGroup) }, { "terrains", shares(byTerrain) } };

	// the objects, by class and by player
	std::vector<Placed> placed;
	json players = json::array();
	std::map<uint32_t, int> playerIndex;
	auto walk = [&](const CommonGameObject* obj, const ServerGameObject* player, auto& self) -> void {
		for (const auto& [type, children] : obj->children)
			for (const CommonGameObject* child : children) {
				const ServerGameObject* c = (const ServerGameObject*)child;
				if (c->deleted)
					continue;
				const ServerGameObject* p = player;
				if (c->blueprint->bpClass == Tags::GAMEOBJCLASS_PLAYER) {
					p = c;
					playerIndex[c->id] = (int)players.size();
					players.push_back({ { "id", c->id }, { "blueprint", c->blueprint->name }, { "name", c->name }, { "colour", c->color },
						{ "byClass", json::object() }, { "byBlueprint", json::object() } });
				}
				else
					placed.push_back({ c, p });
				self(child, p, self);
			}
	};
	walk(server.getLevel(), nullptr, walk);
	json byClass = json::object(), byBlueprint = json::object();
	for (const Placed& pl : placed) {
		const std::string cls = ClassName(pl.obj->blueprint->bpClass), name = pl.obj->blueprint->name;
		byClass[cls] = byClass.value(cls, 0) + 1;
		json& bb = byBlueprint[cls];
		if (!bb.is_object())
			bb = json::object();
		bb[name] = bb.value(name, 0) + 1;
		if (pl.player) {
			json& pj = players[playerIndex[pl.player->id]];
			pj["byClass"][cls] = pj["byClass"].value(cls, 0) + 1;
			pj["byBlueprint"][name] = pj["byBlueprint"].value(name, 0) + 1;
		}
	}
	size_t onMap = 0;
	for (const Placed& pl : placed)
		onMap += Placeable(pl.obj->blueprint->bpClass);
	// count: the objects standing on the map (buildings, characters, containers, markers, props);
	// byClass also has the level's structure (cities, armies, towns, zones)
	out["objects"] = { { "count", onMap }, { "byClass", byClass }, { "byBlueprint", byBlueprint } };
	out["players"] = players;

	// the picture: relief (hypsometric tint, lit from the north-west) with the water, the
	// waterways and falls, and the objects as symbols; north up
	if (!png.empty()) {
		const int ppt = std::clamp(1024 / std::max(W, H), 1, 4);
		const int iw = W * ppt, ih = H * ppt;
		std::vector<uint8_t> img((size_t)iw * ih * 3);
		auto vert = [&](int x, int z) { return t->getVertex(std::clamp(x, 0, W), std::clamp(z, 0, H)); };
		const float range = std::max(1.0f, ahi - alo);
		for (int r = 0; r < ih; r++)
			for (int cx = 0; cx < iw; cx++) {
				const float fx = (cx + 0.5f) / ppt, fz = H - (r + 0.5f) / ppt;
				const int x = std::clamp((int)fx, 0, W - 1), z = std::clamp((int)fz, 0, H - 1);
				const float u = fx - x, v = fz - z;
				const float h = (vert(x, z) * (1 - u) + vert(x + 1, z) * u) * (1 - v) + (vert(x, z + 1) * (1 - u) + vert(x + 1, z + 1) * u) * v;
				// the slope across the tile: lit from the north-west
				const float dx = (vert(x + 1, z) + vert(x + 1, z + 1) - vert(x, z) - vert(x, z + 1)) * 0.5f / 5.0f;
				const float dz = (vert(x, z + 1) + vert(x + 1, z + 1) - vert(x, z) - vert(x + 1, z)) * 0.5f / 5.0f;
				const float nx = -dx, ny = 1.0f, nz = -dz, nl = std::sqrt(nx * nx + ny * ny + nz * nz);
				const float lx = -0.6f, ly = 0.7f, lz = 0.4f; // from the north-west (north is +z)
				const float shade = std::clamp(0.35f + 0.75f * (nx * lx + ny * ly + nz * lz) / nl, 0.2f, 1.25f);
				// low green, mid brown, high grey-white
				const float k = (h - alo) / range;
				float col[3];
				const float stops[4][3] = { { 70, 120, 60 }, { 140, 150, 80 }, { 140, 110, 75 }, { 225, 225, 225 } };
				const float s = std::clamp(k, 0.0f, 1.0f) * 3.0f;
				const int si = std::min(2, (int)s);
				for (int c = 0; c < 3; c++)
					col[c] = (stops[si][c] * (1 - (s - si)) + stops[si + 1][c] * (s - si)) * shade;
				const TerrainTile& tile = t->tiles[z * W + x];
				if (tile.fullOfWater) {
					const float depth = std::clamp((tile.waterLevel - h) / 6.0f, 0.0f, 1.0f);
					const float wc[3] = { 60 - 30 * depth, 120 - 50 * depth, 200 - 40 * depth };
					for (int c = 0; c < 3; c++)
						col[c] = wc[c] * 0.85f + col[c] * 0.15f;
				}
				const bool edgeBand = x < E || x >= W - E || z < E || z >= H - E;
				uint8_t* p = &img[((size_t)r * iw + cx) * 3];
				for (int c = 0; c < 3; c++)
					p[c] = (uint8_t)std::clamp((int)(col[c] * (edgeBand ? 0.55f : 1.0f)), 0, 255);
			}
		// positions in playable metres (objects, waterways, falls), the picture the whole map
		auto toPx = [&](float wx, float wz, int& px, int& py) { px = (int)((wx / 5.0f + E) * ppt); py = (int)((H - E - wz / 5.0f) * ppt); };
		auto dot = [&](int px, int py, int rad, const uint8_t* c, bool outline) {
			for (int y = py - rad; y <= py + rad; y++)
				for (int x = px - rad; x <= px + rad; x++)
					if (x >= 0 && y >= 0 && x < iw && y < ih) {
						const bool border = outline && (y == py - rad || y == py + rad || x == px - rad || x == px + rad);
						uint8_t* p = &img[((size_t)y * iw + x) * 3];
						for (int k = 0; k < 3; k++)
							p[k] = border ? 0 : c[k];
					}
		};
		auto line = [&](float x0, float z0, float x1, float z1, const uint8_t* c, int rad) {
			int ax, ay, bx, by;
			toPx(x0, z0, ax, ay);
			toPx(x1, z1, bx, by);
			const int steps = std::max({ std::abs(bx - ax), std::abs(by - ay), 1 });
			for (int i = 0; i <= steps; i++)
				dot(ax + (bx - ax) * i / steps, ay + (by - ay) * i / steps, rad, c, false);
		};
		if (const WaterDesign* wd = t->waterDesign.get()) {
			const uint8_t wc[3] = { 120, 190, 255 }, fc[3] = { 255, 255, 255 };
			for (const WaterDesign::Waterway& w : wd->waterways)
				for (size_t i = 1; i < w.points.size(); i++)
					line(w.points[i - 1].x, w.points[i - 1].z, w.points[i].x, w.points[i].z, wc, std::max(0, ppt / 2));
			for (const WaterDesign::PlacedFall& f : wd->waterfalls)
				line(f.from.x, f.from.z, f.to.x, f.to.z, fc, std::max(1, ppt / 2));
		}
		// trees and resources first, then markers, characters, buildings on top
		const uint8_t treeC[3] = { 20, 70, 25 }, resC[3] = { 250, 210, 60 }, propC[3] = { 120, 120, 120 }, markC[3] = { 255, 0, 255 };
		const std::set<int> resourceItems = ResourceItems(gs);
		auto isTree = [](const GameObjBlueprint* bp) {
			const std::string m = Lower(bp->modelPath), nm = Lower(bp->name);
			return m.find("tree") != std::string::npos || nm.find("tree") != std::string::npos;
		};
		for (int pass = 0; pass < 3; pass++)
			for (const Placed& pl : placed) {
				const GameObjBlueprint* bp = pl.obj->blueprint;
				const int cls = bp->bpClass;
				const int order = cls == Tags::GAMEOBJCLASS_BUILDING ? 2 : cls == Tags::GAMEOBJCLASS_CHARACTER || cls == Tags::GAMEOBJCLASS_MARKER ? 1 : 0;
				if (order != pass || !Placeable(cls))
					continue;
				int px, py;
				toPx(pl.obj->position.x, pl.obj->position.z, px, py);
				const int pc = pl.player ? std::clamp(pl.player->color, 0, 8) : 0;
				if (cls == Tags::GAMEOBJCLASS_BUILDING) {
					int size = 1;
					if (bp->footprint && !bp->footprint->tiles.empty()) {
						int x0 = 1 << 20, x1 = -(1 << 20);
						for (const auto& tt : bp->footprint->tiles)
							x0 = std::min(x0, tt.offsetX), x1 = std::max(x1, tt.offsetX);
						size = x1 - x0 + 1;
					}
					dot(px, py, std::max(2, size * ppt / 2), PLAYER_COLOURS[pc], true);
				}
				else if (cls == Tags::GAMEOBJCLASS_CHARACTER)
					dot(px, py, std::max(1, ppt / 2), PLAYER_COLOURS[pc], false);
				else if (cls == Tags::GAMEOBJCLASS_MARKER) {
					for (int d = -2; d <= 2; d++) {
						dot(px + d, py, 0, markC, false);
						dot(px, py + d, 0, markC, false);
					}
				}
				else if (isTree(bp))
					dot(px, py, std::max(0, ppt / 2 - 1), treeC, false);
				else {
					bool res = false;
					for (int r : resourceItems)
						res |= bp->getStartingItemValue(r) > 0.0f;
					dot(px, py, std::max(0, ppt / 2), res ? resC : propC, false);
				}
			}
		if (!PngTo(PathFromUtf8(png), iw, ih, img)) {
			error = "can't write " + png;
			return false;
		}
		out["png"] = { { "path", png }, { "size", { iw, ih } }, { "pixelsPerTile", ppt },
			{ "legend", { { "relief", "height tint (low green, mid brown, high white), lit from the north-west; the edge band darker; north up" },
				{ "water", "blue, darker where deeper; waterways light blue lines; waterfalls white" },
				{ "buildings", "squares of the player's colour, footprint-sized, black outline" }, { "characters", "dots of the player's colour" },
				{ "trees", "dark green" }, { "resources", "yellow (holds a resource)" }, { "props", "grey" }, { "markers", "magenta crosses" },
				{ "playerColours", "1 red, 2 blue, 3 yellow, 4 green, 5 orange, 6 purple, 7 cyan, 8 pink, 0 grey" } } } };
	}
	return true;
}

namespace {
	struct Args {
		std::map<std::string, std::string> opts;
		std::set<std::string> flags;
		std::vector<std::string> positional;
	};
	Args Parse(const std::vector<std::string>& args, std::initializer_list<const char*> withValue)
	{
		Args a;
		for (size_t i = 0; i < args.size(); i++) {
			const std::string& s = args[i];
			if (s.size() > 2 && s[0] == '-' && s[1] == '-') {
				const std::string k = s.substr(2);
				bool takes = false;
				for (const char* w : withValue)
					takes |= k == w;
				if (takes && i + 1 < args.size())
					a.opts[k] = args[++i];
				else
					a.flags.insert(k);
			}
			else
				a.positional.push_back(s);
		}
		return a;
	}
	int Emit(const json& j, const Args& a, const std::string& what)
	{
		const std::string text = j.dump(a.flags.count("compact") ? -1 : 1);
		if (auto it = a.opts.find("out"); it != a.opts.end()) {
			const fs::path p = PathFromUtf8(it->second);
			std::error_code ec;
			if (p.has_parent_path())
				fs::create_directories(p.parent_path(), ec);
			std::ofstream f(p, std::ios::binary);
			f << text << "\n";
			if (!f) {
				printf("AUTHOR failed: can't write %s\n", it->second.c_str());
				return 1;
			}
			printf("AUTHOR ok, %s %s\n", what.c_str(), it->second.c_str());
		}
		else {
			printf("AUTHOR-JSON-BEGIN\n%s\nAUTHOR-JSON-END\n", text.c_str());
			printf("AUTHOR ok, %s\n", what.c_str());
		}
		fflush(stdout);
		return 0;
	}
	// --mod <name>: only that mod on top of the base game ("none" or "base": the base game alone);
	// without it the mods as wkconfig.json has them. For this run only (not saved).
	bool SelectMod(const Args& a)
	{
		LoadBCP("data.bcp");
		auto it = a.opts.find("mod");
		if (it == a.opts.end())
			return true;
		const std::string want = Lower(it->second);
		const bool none = want == "none" || want == "base";
		json disabled = json::array();
		bool found = none;
		for (const ModInfo& m : GetMods()) {
			std::string stem = Lower(m.id);
			if (const size_t slash = stem.find_last_of("/\\"); slash != std::string::npos)
				stem = stem.substr(slash + 1);
			if (EndsWithCI(stem, ".bcp"))
				stem.resize(stem.size() - 4);
			const bool match = !none && (Lower(m.id) == want || Lower(m.name) == want || Lower(m.modId) == want || stem == want);
			found |= match;
			if (!match)
				disabled.push_back(m.id);
		}
		if (!found) {
			printf("AUTHOR failed: no mod %s (mods:", it->second.c_str());
			for (const ModInfo& m : GetMods())
				printf(" \"%s\"", m.id.c_str());
			printf(")\n");
			return false;
		}
		g_settings["disabledMods"] = disabled;
		InitFileSystem();
		return true;
	}
}

int RunCatalog(const std::vector<std::string>& args)
{
	const Args a = Parse(args, { "mod", "gameset", "out" });
	if (!SelectMod(a))
		return 1;
	const std::string file = a.opts.count("gameset") ? a.opts.at("gameset") : "Warrior Kings Game Set\\multi-player extensions.cpp";
	if (!FileExists(file.c_str())) {
		printf("AUTHOR failed: no game set %s\n", file.c_str());
		return 1;
	}
	GameSet gs(file.c_str(), g_settings.value<int>("gameVersion", 0));
	const json c = Catalog(gs, file, a.flags.count("edges") > 0);
	const json& n = c["counts"];
	char what[256];
	snprintf(what, sizeof(what), "catalog: %d blueprints, %d texture groups, %d textures, %d skies", n["blueprints"].get<int>(),
		n["textureGroups"].get<int>(), n["textures"].get<int>(), n["skies"].get<int>());
	return Emit(c, a, what);
}

int RunDescribe(const std::vector<std::string>& args)
{
	const Args a = Parse(args, { "mod", "png", "out" });
	if (a.positional.empty()) {
		printf("usage: wkbai --author describe <folder.wkbai | level> [--png <file.png>] [--out <file.json>]\n");
		return 2;
	}
	if (!SelectMod(a))
		return 1;
	json d;
	std::string error;
	if (!Describe(a.positional[0], a.opts.count("png") ? a.opts.at("png") : "", d, error)) {
		printf("AUTHOR failed: %s\n", error.c_str());
		return 1;
	}
	char what[256];
	snprintf(what, sizeof(what), "description: %d x %d tiles, %d objects", d["size"]["tiles"][0].get<int>(), d["size"]["tiles"][1].get<int>(),
		d["objects"]["count"].get<int>());
	return Emit(d, a, what);
}

} // namespace MapAuthoring
