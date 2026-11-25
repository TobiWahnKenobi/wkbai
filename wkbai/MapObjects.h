// wkbai - a map folder's objects by intent (step 17.7 of the RE repo's docs/engine/map-authoring.md):
// player bases (the starts, chosen when a map asks for them; the base's buildings laid out round
// its civic centre), and the placement checks. Every building the compiler places goes through
// the server's own placement rules (StampdownPlan::getStampdownPlan: the footprint's tiles, the
// terrain lists, the slope limit, the neighbours, the city rules) on a light game state of its
// own (the game set, the compiled terrain, the level's objects), so that no Server is made inside
// the compiler; the same check runs on a loaded Server for describe_map.
//
// map.json (players and "bases"):
//   "players": [{ "name", "colour", "start": [x, z] | "auto", "base": "Manor", "peasants": 4,
//                 "buildings": [...] (else bases.buildings) }]
//   "bases": { "auto": false,            // every player without a start gets one chosen
//              "edgeM": 80, "waterM": 40, "maxRiseM": 6, "room": 0.6, "minApartM": 0,
//              "searchM": 40,            // how far from its start the civic centre may move
//              "buildings": [ { "blueprint": "City Houses", "count": 2, "distance": [12, 40],
//                               "toward": "centre" | "away" | degrees } ] }
// A building intent's distance is from the civic centre; "toward" prefers a side (degrees from
// +x towards +z). Each is placed on the nearest site the server's rules accept, turned to face
// the civic centre where a turn fits; civic buildings join its city (the plan's own city
// rectangles), as a peasant's stampdown would. Wall foundations the plan would lay are left out.
// Licensed under the GNU General Public License 3

#pragma once

#include "common.h"
#include "StampdownPlan.h"
#include <nlohmann/json.hpp>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

struct GameSet;
struct Terrain;
struct GameObjBlueprint;

namespace MapObjects {
	using json = nlohmann::ordered_json;

	// the game set by its file (as a level's GAME_SET names it), loaded once per file and game
	// version and kept; null when it can't be loaded
	std::shared_ptr<const GameSet> LoadGameSet(const std::string& file);

	// A game state for placing objects with the server's rules, without a Server: the game set,
	// a terrain (not owned), the tiles, and objects (owned)
	struct PlacementState : CommonGameState {
		PlacementState(std::shared_ptr<const GameSet> gs, Terrain* t);
		std::vector<std::unique_ptr<CommonGameObject>> owned;
		std::map<uint32_t, CommonGameObject*> players;
		// adds an object under parent (null: the level), on its tiles (a building takes its
		// footprint's)
		CommonGameObject* add(uint32_t id, const GameObjBlueprint* bp, CommonGameObject* parent, const Vector3& pos, const Vector3& ori);
		// reads a level's world (the original's grammar, as lines): players, cities with their
		// rectangles, buildings, characters, containers; objects whose blueprint the game set lacks
		// are left out
		void readWorld(const std::vector<std::string>& lines);
	};

	// A site for a building: the nearest to the query's preferred distance and side whose plan
	// is Valid, turned to face (faceX, faceZ) where that turn fits (else another quarter)
	struct SiteQuery {
		float x = 0, z = 0;           // the search's centre
		float rMin = 0, rMax = 40, prefDist = 0;
		float prefAngle = 1e30f;      // radians from +x towards +z; 1e30: none
		bool face = true;             // face (faceX, faceZ)
		float faceX = 0, faceZ = 0;
		// tiles kept free (playable tile index), e.g. the peasants' place before the door
		const std::vector<uint8_t>* keepFree = nullptr;
	};
	struct Site {
		bool found = false;
		Vector3 pos, ori;
		StampdownPlan plan;
		StampdownPlan::Status firstRefusal = StampdownPlan::Status::Valid;
		int tried = 0;
	};
	Site FindSite(PlacementState& s, CommonGameObject* player, const GameObjBlueprint* bp, const SiteQuery& q);
	// the building put down by its plan: into the plan's city (a new one when the plan says so,
	// its id from newId), the city's rectangle added, the ground under it levelled as the server's
	// stampdown does (Server::levelTerrainUnder: every FLATTEN tile's corners at the building's
	// height); the wall foundations the plan would lay are not made. made: the objects it made
	// (the city first when new)
	CommonGameObject* PutBuilding(PlacementState& s, CommonGameObject* player, const GameObjBlueprint* bp, const Site& site, const std::function<uint32_t()>& newId,
		std::vector<CommonGameObject*>& made);

	// the starts chosen for players without one: flat land away from the map's edge and its water,
	// with room round it, a civic centre's plan valid there, each the farthest from those already
	// set or chosen
	std::vector<std::pair<float, float>> ChooseStarts(PlacementState& s, CommonGameObject* player, const GameObjBlueprint* centre,
		const std::vector<std::pair<float, float>>& fixed, int count, const json& opts, json& report, std::vector<std::string>& warnings);

	// an object as world lines (the original's grammar): the object, its place, its rectangles
	// and subtype, what it holds, at the given depth of tabs
	void ObjectText(const CommonGameObject* o, int depth, std::vector<std::string>& out);

	// The placement checks (the compiler's report, describe_map), on any game state (the light
	// one or a loaded Server): every building against the server's rules as it stands (its own
	// tiles freed for the check), characters standing on a building's blocked tile or in water,
	// containers that can't be built upon standing on a building's tile. Faults go to warnings
	// with their place in playable metres.
	json CheckPlacement(CommonGameState* gs, std::vector<std::string>* warnings);
	// Fairness between the bases (each player's first civic centre): per base within radius m
	// the resources the containers hold (wood, gold, stone, food) and how many, the farmable
	// tiles (a farm's terrain), the nearest other base; per resource the smallest over the largest
	json Fairness(CommonGameState* gs, float radius, std::vector<std::string>* warnings);

	// the status's name (Valid, InvalidTerrainType, ...)
	const char* StatusName(StampdownPlan::Status s);
}
