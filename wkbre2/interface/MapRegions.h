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
// again only when that changes (the map edited).
struct MapRegions {
	// Water: rivers, fords, narrow lakes; Sea: water the renderer gives its sea look, wide water
	// (EnhancedTerrainData's flow map: 6 tiles or more from the shore at its widest)
	enum Kind { Plains, Meadows, Forest, Hills, Mountains, Marsh, Desert, Shore, Water, Sea, KindCount };
	struct Region { Kind kind; std::string name; Vector3 centre; int cells = 0; };
	std::vector<Region> regions;

	// for the client's map (once, at the start: the trees are those standing then); true if made
	bool build(Client* client);
	bool ready() const { return !cells.empty(); }
	// the region at a place, or nullptr
	const Region* at(const Vector3& position) const;
	static const char* kindName(Kind kind);

private:
	static constexpr int CELL = 8; // tiles
	int width = 0, height = 0; // in cells
	std::vector<int> cells; // region index per cell
	std::string mapName;
	uint64_t hash = 0;
	bool load(const std::filesystem::path& file);
	void save(const std::filesystem::path& file) const;
};
