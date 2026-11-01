// wkbai - a map's named regions (forests, river banks, plains, mountains, ...), for the timeline

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
#include "../util/vecmat.h"

struct Client;

// The map in 40 m cells (8 x 8 tiles), each of the kind most of its tiles are, from the terrain
// type of its texture (the game set's TERRAIN blueprints: Forest, Swamp, Sand, Mountains, ...),
// the water, the height and the trees standing at the start. Neighbouring cells of a kind form a
// region; a region of fewer than 4 cells joins the neighbour it borders most. Each region gets a
// name from its kind's list (fantasy, Norse, Anglo-Saxon, Celtic, Pictish sounding), chosen by a
// hash of the map and the region: the same map always gets the same regions and names. They are
// saved in saved\Regions\<map>.json with a hash of the terrain and the trees, and worked out
// again only when that changes (the map edited) - unless they were edited by hand (the map
// editor's Regions mode: names, kinds, which cells belong where), which are kept as long as the
// map keeps its size.
struct MapRegions {
	// Water: rivers, fords, lakes; Sea: water the renderer gives its sea look (open water reaching
	// the map's edge, or painted as sea by the map's water design: EnhancedTerrainData)
	enum Kind { Plains, Meadows, Forest, Hills, Mountains, Marsh, Desert, Shore, Water, Sea, KindCount };
	struct Region { Kind kind; std::string name; Vector3 centre; int cells = 0; };
	std::vector<Region> regions;

	// for the client's map (once, at the start: the trees are those standing then); true if made
	bool build(Client* client);
	bool ready() const { return !cells.empty(); }
	// the region at a place, or nullptr
	const Region* at(const Vector3& position) const;
	static const char* kindName(Kind kind);

	// Editing (the map editor): the cells (of CELL x CELL tiles, x, z from the playable area's
	// corner) and the region each belongs to; a new region (its name the kind's next unused one,
	// returns its index); a region taken away (its cells to the neighbour it borders most). Each
	// change marks the regions edited; save writes them for the map, or under another map's name.
	static constexpr int CELL = 8; // tiles
	int cellsWide() const { return width; }
	int cellsHigh() const { return height; }
	int cellRegion(int cx, int cz) const { return (cx >= 0 && cz >= 0 && cx < width && cz < height) ? cells[cz * width + cx] : -1; }
	void setCell(int cx, int cz, int region);
	int addRegion(Kind kind);
	void removeRegion(int region);
	std::string nextName(Kind kind) const;
	bool edited = false;
	bool save(const std::string& mapName = std::string()) const;

private:
	int width = 0, height = 0; // in cells
	std::vector<int> cells; // region index per cell
	std::string mapName;
	uint64_t hash = 0;
	void updateCentres();
	static std::filesystem::path fileFor(const std::string& mapName);
	bool load(const std::filesystem::path& file, int expectedWidth, int expectedHeight);
	void save(const std::filesystem::path& file) const;
};
