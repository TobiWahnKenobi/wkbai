#include "StampdownPlan.h"
#include "common.h"
#include "gameset/gameset.h"
#include "gameset/GameObjBlueprint.h"
#include "tags.h"
#include "terrain.h"
#include "gameset/Footprint.h"
#include <algorithm>
#include <iterator>
#include <map>
#include <cmath>

// Walls don't count as neighbours (WKB.exe leaves out four building types, read here as the
// wall pieces)
static bool isWallPiece(int buildingType)
{
	return buildingType == Tags::BUILDINGTYPE_WALL || buildingType == Tags::BUILDINGTYPE_WALL_CORNER_IN
		|| buildingType == Tags::BUILDINGTYPE_WALL_CORNER_OUT || buildingType == Tags::BUILDINGTYPE_WALL_CROSSROADS;
}

bool StampdownPlan::groundAllows(CommonGameState* gameState, const Footprint* fp, int ox, int oz)
{
	const Terrain* trn = gameState->terrain;
	if (!trn || !gameState->tiles)
		return true;
	const auto [tileWidth, tileHeight] = trn->getNumPlayableTiles();
	std::vector<std::pair<int, int>> taken;
	for (const auto& to : fp->tiles)
		taken.emplace_back(ox + to.offsetX, oz + to.offsetZ);
	float lowest = 1e30f, highest = -1e30f;
	for (size_t t = 0; t < fp->tiles.size(); t++) {
		const auto [px, pz] = taken[t];
		if (px < 0 || pz < 0 || px >= tileWidth || pz >= tileHeight)
			return false;
		for (int dz = 0; dz <= 1; dz++)
			for (int dx = 0; dx <= 1; dx++) {
				const float h = trn->getVertex(px + trn->edge + dx, pz + trn->edge + dz);
				lowest = std::min(lowest, h);
				highest = std::max(highest, h);
			}
		const float range = highest - lowest;
		if (fp->maxHeightDifferential > 0.0f && range > fp->maxHeightDifferential)
			return false;
		if (range >= 2.5f && fp->tiles[t].flatten)
			for (int nz = -1; nz <= 1; nz++)
				for (int nx = -1; nx <= 1; nx++) {
					const int qx = px + nx, qz = pz + nz;
					if ((nx == 0 && nz == 0) || qx < 0 || qz < 0 || qx >= tileWidth || qz >= tileHeight
						|| std::find(taken.begin(), taken.end(), std::make_pair(qx, qz)) != taken.end())
						continue;
					const CommonGameObject* neighbour = gameState->tiles[qz * tileWidth + qx].building.getFrom(gameState);
					if (neighbour && !isWallPiece(neighbour->blueprint->buildingType))
						return false;
				}
	}
	return true;
}

// A tile's terrain is that of its texture's group (ASSOCIATE_TILE_TEXTURE_GROUP); a group no
// terrain names is in no list
bool StampdownPlan::tileTerrainAllowed(const CommonGameState* gameState, const Footprint::TileOffset& to, int tx, int tz)
{
	if (!to.terrains || to.terrains->empty())
		return true;
	const auto* tile = gameState->terrain->getPlayableTile(tx, tz);
	if (!tile || !tile->texture || !tile->texture->grp)
		return false;
	const auto& groups = gameState->gameSet->associatedTileTexGroups;
	const auto it = groups.find(tile->texture->grp->name);
	if (it == groups.end())
		return false;
	const int index = gameState->gameSet->terrains.getIndex(it->second);
	return index >= 0 && index < (int)to.terrains->size() && (*to.terrains)[index];
}

const char* StampdownPlan::statusTextKey(Status status)
{
	switch (status) {
	case Status::InvalidOccupied: return "BUILDING_PLACEMENT_CANNOT_BUILD_HERE";
	case Status::InvalidTerrainType: return "BUILDING_PLACEMENT_INVALID_TERRAIN_TYPE";
	case Status::InvalidTooSteep: return "BUILDING_PLACEMENT_TOO_STEEP";
	case Status::InvalidNeighbour: return "BUILDING_PLACEMENT_OVERLAPS_NEIGHBOURING_BUILDING";
	case Status::InvalidNotConnected: return "BUILDING_PLACEMENT_THIS_BUILDING_MUST_BE_CONNECTED_TO_A_CITY";
	case Status::InvalidNotDetached: return "BUILDING_PLACEMENT_THIS_BUILDING_MUST_BE_DETACHED_FROM_ALL_CITIES";
	case Status::InvalidGatehouseOverlap: return "BUILDING_PLACEMENT_A_WALL_IS_OVERLAPPING_A_GATEHOUSE";
	case Status::InvalidRuralInside: return "BUILDING_PLACEMENT_THERE_IS_A_RURAL_BUILDING_INSIDE_THIS_PERIMETER";
	case Status::InvalidTerrain:
	case Status::InvalidNotInCity:
	case Status::InvalidNoExplanation: return "BUILDING_PLACEMENT_CANNOT_BUILD_HERE";
	default: return nullptr;
	}
}

StampdownPlan StampdownPlan::getStampdownPlan(
	CommonGameState* gameState, CommonGameObject* playerObj, const GameObjBlueprint* blueprint, Vector3 position, Vector3 orientation)
{
	StampdownPlan plan;

	static const int wallBuildingTypes[] = {
		Tags::BUILDINGTYPE_WALL,
		Tags::BUILDINGTYPE_WALL_CORNER_IN,
		Tags::BUILDINGTYPE_WALL_CORNER_OUT,
		Tags::BUILDINGTYPE_WALL_CROSSROADS
	};

	const bool civic = blueprint->buildingType == Tags::BUILDINGTYPE_CIVIC || blueprint->buildingType == Tags::BUILDINGTYPE_CIVIC_CENTRE;
	// the city rules' refusal (WKB.exe 0x510f90), given after the walls are laid out: the preview
	// shows the ring and hides the walls it takes down, refused or not, as the original does
	Status cityRefusal = Status::Valid;
	// walls, towers and gatehouses: their passable tiles (a gateway, a wall walk) may share
	auto isWallLike = [](int type) {
		return type == Tags::BUILDINGTYPE_GATEHOUSE || (type >= Tags::BUILDINGTYPE_TOWER && type <= Tags::BUILDINGTYPE_WALL_CROSSROADS);
	};

	// The footprint's tiles must lie on the playable map and be free of other buildings, as
	// the new building would take them (CommonGameState::updateOccupiedTiles). Passable tiles
	// of a wall, tower or gatehouse (such as a gateway) may share a tile with another building's
	// passable tile (not a field's with another field's, though fields are passable); civic
	// buildings may stand on walls, which they replace.
	if (const Footprint* fp = blueprint->footprint; fp && gameState->tiles && gameState->terrain) {
		const auto [tileWidth, tileHeight] = gameState->terrain->getNumPlayableTiles();
		const auto rotOrigin = fp->rotateOrigin(orientation.y);
		const int ox = (int)((position.x - rotOrigin.first) / 5.0f);
		const int oz = (int)((position.z - rotOrigin.second) / 5.0f);
		// Then, tile by tile, WKB.exe's checks (0x59ab80):
		// - the tile's terrain must be in its USE_TERRAIN_BLUEPRINT_LIST, else
		//   BUILDING_PLACEMENT_INVALID_TERRAIN_TYPE: no building on sand, shores, mountains or
		//   water, no farm on hills, rock or mud;
		// - MAX_FOUNDATION_HEIGHT_DIFFERENTIAL: the highest and lowest corners of all the tiles so
		//   far, levelled or not, may differ by at most that much, in metres, else
		//   BUILDING_PLACEMENT_TOO_STEEP. A 3 x 3 house's 8 m over 15 m is 28 degrees, a farm's
		//   12 m is 39;
		// - once they differ by 2.5 m or more, a tile to be levelled may not border another
		//   building (any of its 8 neighbours outside the footprint), else
		//   BUILDING_PLACEMENT_OVERLAPS_NEIGHBOURING_BUILDING: levelling shares its edge's
		//   corners with the neighbour, which would be left on a step. Walls don't count
		//   (WKB.exe leaves out four building types, read here as the wall pieces).
		// They hold for every player, the AIs too, as in WKB.exe; the AIs' search for a site
		// (NEAREST_VALID_STAMPDOWN_POS) looks for them (groundAllows).
		const Terrain* trn = gameState->terrain;
		std::vector<std::pair<int, int>> taken;
		for (const auto& to : fp->tiles) {
			const auto [rx, rz] = to.rotate(orientation.y);
			taken.emplace_back(ox + rx, oz + rz);
		}
		// The first refusal is the plan's (and the text shown), as in WKB.exe; the other tiles are
		// still looked at, so that the preview shows each in red or green as the original does: a
		// tile is red for its own fault (another building, its terrain, a building beside it that
		// levelling would leave on a step), all are red once the whole is too steep.
		Status refusal = Status::Valid;
		auto refuse = [&refusal](Status s) {
			if (refusal == Status::Valid)
				refusal = s;
		};
		bool tooSteep = false;
		float lowest = 1e30f, highest = -1e30f;
		for (size_t t = 0; t < fp->tiles.size(); t++) {
			const auto& to = fp->tiles[t];
			const auto [px, pz] = taken[t];
			if (px < 0 || pz < 0 || px >= tileWidth || pz >= tileHeight) {
				refuse(Status::InvalidTerrain);
				continue;
			}
			bool ok = true;
			const auto& tile = gameState->tiles[pz * tileWidth + px];
			if (const CommonGameObject* other = tile.building.getFrom(gameState)) {
				const bool onWall = civic && std::find(std::begin(wallBuildingTypes), std::end(wallBuildingTypes), other->blueprint->buildingType) != std::end(wallBuildingTypes);
				const bool shared = to.mode && tile.buildingPassable && (isWallLike(blueprint->buildingType) || isWallLike(other->blueprint->buildingType));
				if (!onWall && !shared) {
					refuse(Status::InvalidOccupied);
					ok = false;
				}
			}
			if (ok && !tileTerrainAllowed(gameState, to, px, pz)) {
				refuse(Status::InvalidTerrainType);
				ok = false;
			}
			for (int dz = 0; dz <= 1; dz++)
				for (int dx = 0; dx <= 1; dx++) {
					const float h = trn->getVertex(px + trn->edge + dx, pz + trn->edge + dz);
					lowest = std::min(lowest, h);
					highest = std::max(highest, h);
				}
			const float range = highest - lowest;
			if (ok && fp->maxHeightDifferential > 0.0f && range > fp->maxHeightDifferential) {
				refuse(Status::InvalidTooSteep);
				tooSteep = true;
			}
			if (ok && range >= 2.5f && to.flatten)
				for (int nz = -1; nz <= 1 && ok; nz++)
					for (int nx = -1; nx <= 1 && ok; nx++) {
						const int qx = px + nx, qz = pz + nz;
						if ((nx == 0 && nz == 0) || qx < 0 || qz < 0 || qx >= tileWidth || qz >= tileHeight
							|| std::find(taken.begin(), taken.end(), std::make_pair(qx, qz)) != taken.end())
							continue;
						const CommonGameObject* neighbour = gameState->tiles[qz * tileWidth + qx].building.getFrom(gameState);
						if (neighbour && std::find(std::begin(wallBuildingTypes), std::end(wallBuildingTypes), neighbour->blueprint->buildingType) == std::end(wallBuildingTypes)) {
							refuse(Status::InvalidNeighbour);
							ok = false;
						}
					}
			plan.tiles.push_back({ px, pz, ok });
		}
		if (tooSteep)
			for (auto& verdict : plan.tiles)
				verdict.ok = false;
		if (refusal != Status::Valid) {
			plan.status = refusal;
			return plan;
		}
	}

	if (civic) {
		using CityRectangle = CommonGameObject::CityRectangle;
		
		const auto cityContainsTile = [](const std::vector<CityRectangle>& cityRectangles, int x, int z) -> bool {
			for (const auto& rect : cityRectangles) {
				if (rect.xStart <= x && x <= rect.xEnd && rect.yStart <= z && z <= rect.yEnd) {
					return true;
				}
			}
			return false;
			};
		// the rectangles share a tile; wkbre2 asked whether an end of one lay within the other,
		// which missed a rectangle reaching across a city's whole side: that started a city of
		// its own, whose ring stood across the first one's
		const auto cityIntersectsWithRect = [](const std::vector<CityRectangle>& cityRectangles, const CityRectangle& rect) -> bool {
			for (const auto& cityRect : cityRectangles) {
				const bool intersects_x = rect.xStart <= cityRect.xEnd && cityRect.xStart <= rect.xEnd;
				const bool intersects_y = rect.yStart <= cityRect.yEnd && cityRect.yStart <= rect.yEnd;
				if (intersects_x && intersects_y) {
					return true;
				}
			}
			return false;
			};

		const GameSet* gameSet = gameState->gameSet.get();
		// the player's wall type: "Wall Index" (Walls.cpp: 0/1 stockade, 2/3 unaligned wall, 4/5
		// technological, 6/7 celestial; the even ones the foundations the ring lays), set by the
		// wall research ("Stockade to Wall" sets 2); the play test: a city grown after the research
		// still got palisades. Falls back to the stockade when a set is missing.
		static const char* const wallSets[4] = { "Stockade", "Unaligned Wall", "Technological Wall", "Celestial Wall" };
		const int wallIndexItem = gameSet->items.names.getIndex("Wall Index");
		const int wallSet = wallIndexItem >= 0 ? std::clamp((int)playerObj->getItem(wallIndexItem) / 2, 0, 3) : 0;
		auto wallPiece = [&](const char* piece) {
			const std::string name = std::string(wallSets[wallSet]) + piece;
			const GameObjBlueprint* bp = gameSet->findBlueprint(Tags::GAMEOBJCLASS_BUILDING, name);
			return bp ? bp : gameSet->findBlueprint(Tags::GAMEOBJCLASS_BUILDING, std::string("Stockade") + piece);
		};
		const GameObjBlueprint* bpWall = wallPiece(" Foundation");
		const GameObjBlueprint* bpWallCornerIn = wallPiece(" Corner In Foundation");
		const GameObjBlueprint* bpWallCornerOut = wallPiece(" Corner Out Foundation");
		const GameObjBlueprint* bpWallCrossPiece = wallPiece(" Cross Piece Foundation");
		Footprint* footprint = blueprint->footprint;
		if (bpWall && footprint) {
			auto [tileWidth, tileHeight] = gameState->terrain->getNumPlayableTiles();

			// remove offset applied by building from position
			position -= Vector3(footprint->originX, 0.0f, footprint->originZ);

			plan.cityRectangle.xStart = std::clamp(footprint->wallOriginDefaultMinX + static_cast<int>(position.x / 5.0f), 0, tileWidth - 1);
			plan.cityRectangle.yStart = std::clamp(footprint->wallOriginDefaultMinY + static_cast<int>(position.z / 5.0f), 0, tileHeight - 1);
			plan.cityRectangle.xEnd = std::clamp(footprint->wallOriginDefaultMaxX + static_cast<int>(position.x / 5.0f), 0, tileWidth - 1);
			plan.cityRectangle.yEnd = std::clamp(footprint->wallOriginDefaultMaxY + static_cast<int>(position.z / 5.0f), 0, tileHeight - 1);

			CommonGameObject* city = nullptr;
			std::vector<CityRectangle> cityRectangles;
			for (const auto& [bpid, objlist] : playerObj->children) {
				for (auto* candidate : objlist) {
					if (candidate->blueprint->bpClass == Tags::GAMEOBJCLASS_CITY) {
						if (cityIntersectsWithRect(candidate->cityRectangles, plan.cityRectangle)) {
							city = candidate;
							cityRectangles = city->cityRectangles;
							plan.ownerObject = city->id;
							break;
						}
					}
				}
			}

			if (!city) {
				plan.newOwnerBlueprint = gameSet->findBlueprint(Tags::GAMEOBJCLASS_CITY, "New City");
			}

			// WKB.exe walks the new rectangle's four sides (0x510e90, 0x5897a0), each from corner
			// to corner, and its visitor (0x5180e0) sorts the tiles three ways:
			// - outside all of the player's cities (0x5104a0 for each): new wall goes there
			//   (0x8da210), and a corner that got its wall isn't visited again by the next side;
			// - inside a city and strictly within one of the cities' rectangles, off its edge
			//   (0x517e20, 0x5104f0): the city's area (0x8da20c);
			// - inside a city, on its rectangles' edge: where its walls stand (0x8da214). A
			//   corner there is counted twice, once by each side.
			// No tile outside: the building stands inside a city, which is fine. Otherwise
			// (0x510f90), three or more tiles of the city's area make it part of the city, however
			// far from the Manor; with fewer, a civic building needs at least four counts on the
			// city's walls (a corner of the new rectangle there counts twice) and a civic centre
			// (Manor, Keep, Castle, Palace) none. "Near a Manor" in the refusal's words.
			// wkbai counted every tile once, area and walls together.
			{
				const CityRectangle& r = plan.cityRectangle;
				int onWall = 0, inArea = 0, outside = 0;
				std::vector<std::pair<int, int>> walled; // corners that got a new wall
				auto count = [&](int tx, int tz) {
					if (std::find(walled.begin(), walled.end(), std::make_pair(tx, tz)) != walled.end())
						return;
					bool in = false, area = false;
					for (const auto& [bpid, objlist] : playerObj->children)
						for (auto* candidate : objlist)
							if (candidate->blueprint->bpClass == Tags::GAMEOBJCLASS_CITY)
								for (const auto& cr : candidate->cityRectangles) {
									in |= cr.xStart <= tx && tx <= cr.xEnd && cr.yStart <= tz && tz <= cr.yEnd;
									area |= cr.xStart < tx && tx < cr.xEnd && cr.yStart < tz && tz < cr.yEnd;
								}
					if (!in) {
						outside++;
						walled.emplace_back(tx, tz);
					}
					else
						(area ? inArea : onWall)++;
				};
				// the sides as 0x510e90 walks them: along +x, +z, -x, -z (0x5897a0 skips a side
				// that starts where it ends)
				auto side = [&](int x0, int z0, int x1, int z1) {
					if (x0 == x1 && z0 == z1)
						return;
					const int sx = (x1 > x0) - (x1 < x0), sz = (z1 > z0) - (z1 < z0);
					for (int tx = x0, tz = z0;; tx += sx, tz += sz) {
						count(tx, tz);
						if (tx == x1 && tz == z1)
							break;
					}
				};
				side(r.xStart, r.yStart, r.xEnd, r.yStart);
				side(r.xEnd, r.yStart, r.xEnd, r.yEnd);
				side(r.xEnd, r.yEnd, r.xStart, r.yEnd);
				side(r.xStart, r.yEnd, r.xStart, r.yStart);
				if (outside > 0 && inArea < 3) {
					if (blueprint->buildingType == Tags::BUILDINGTYPE_CIVIC_CENTRE) {
						if (onWall > 0)
							cityRefusal = Status::InvalidNotDetached;
					}
					else if (onWall < 4)
						cityRefusal = Status::InvalidNotConnected;
				}
				// no farm or village inside the new perimeter (WKB.exe 0x5113c0, after the above)
				if (cityRefusal == Status::Valid && gameState->tiles)
					for (int tz = r.yStart; tz <= r.yEnd && cityRefusal == Status::Valid; tz++)
						for (int tx = r.xStart; tx <= r.xEnd; tx++)
							if (const CommonGameObject* b = gameState->tiles[tz * tileWidth + tx].building.getFrom(gameState);
								b && (b->blueprint->buildingType == Tags::BUILDINGTYPE_RURAL || b->blueprint->buildingType == Tags::BUILDINGTYPE_RURAL_CENTRE)) {
								cityRefusal = Status::InvalidRuralInside;
								break;
							}
			}

			cityRectangles.push_back(plan.cityRectangle);

			// The ring around the city's rectangles, redone wherever the new rectangle changes it:
			// on its tiles and the tiles next to it, as no other tile's neighbours change. wkbre2
			// laid only the new rectangle's border and left the old ring as it was, so where two
			// rectangles met, old corner pieces stood in the middle of a side, walls ran on inside
			// the city, and the inner corners (no WALL_CORNER_IN was ever laid) stayed open.
			//
			// A border tile's piece follows from which of its eight neighbours lie outside the city:
			// one side outside, a straight piece along it; two sides next to each other, an outer
			// corner; no side but one diagonal, an inner corner; no side but more diagonals (two
			// rectangles touching at a corner), the cross piece. The angles come from the models:
			// the straight stockade runs along its x and the corner pieces have their two arms
			// along -x and -z (Stockade*.mesh3), turned a quarter at a time (x to z, z to -x).
			const int nx0 = std::max(plan.cityRectangle.xStart - 1, 0), nx1 = std::min(plan.cityRectangle.xEnd + 1, tileWidth - 1);
			const int nz0 = std::max(plan.cityRectangle.yStart - 1, 0), nz1 = std::min(plan.cityRectangle.yEnd + 1, tileHeight - 1);
			auto outside = [&](int tx, int tz) { return !cityContainsTile(cityRectangles, tx, tz); };
			// the walls (and gatehouses, which stand in for four of them) already on a tile: those
			// whose position is on it, and the building whose footprint takes it (a gatehouse's
			// position is on one of its four tiles only)
			auto wallsOn = [&, tileWidth = tileWidth](int tx, int tz) {
				std::vector<CommonGameObject*> walls;
				if (!gameState->tiles)
					return walls;
				const auto& tile = gameState->tiles[tz * tileWidth + tx];
				auto add = [&](CommonGameObject* tileBuilding) {
					if (tileBuilding && std::find(walls.begin(), walls.end(), tileBuilding) == walls.end()
						&& (tileBuilding->blueprint->buildingType == Tags::BUILDINGTYPE_GATEHOUSE
							|| std::find(std::begin(wallBuildingTypes), std::end(wallBuildingTypes), tileBuilding->blueprint->buildingType) != std::end(wallBuildingTypes)))
						walls.push_back(tileBuilding);
				};
				for (const auto& tileObjRef : tile.objList)
					add(tileObjRef.getFrom(gameState));
				add(tile.building.getFrom(gameState));
				return walls;
			};
			auto remove = [&](CommonGameObject* obj) {
				for (const auto& removal : plan.toRemove)
					if (removal.object == obj->id)
						return;
				plan.toRemove.emplace_back().object = obj->id;
			};
			// straight pieces a gatehouse already stands in for: (the gatehouse, the piece)
			std::vector<std::pair<GameObjectRef, StampdownCreateAction>> keptByGate;

			// the piece a tile of the city takes (no blueprint: none, it is inside)
			auto pieceFor = [&](int wx, int wz) {
				const bool left = outside(wx - 1, wz), right = outside(wx + 1, wz);
				const bool bottom = outside(wx, wz - 1), top = outside(wx, wz + 1);

				const GameObjBlueprint* needWall = nullptr;
				float needAngle = 0.0f;
				if (!left && !right && !bottom && !top) {
					const bool dl = outside(wx - 1, wz - 1), dr = outside(wx + 1, wz - 1);
					const bool ur = outside(wx + 1, wz + 1), ul = outside(wx - 1, wz + 1);
					const int diagonals = dl + dr + ur + ul;
					if (diagonals == 1) {
						needWall = bpWallCornerIn;
						needAngle = dl ? 0.0f : dr ? 0.25f : ur ? 0.5f : 0.75f;
					}
					else if (diagonals > 1)
						needWall = bpWallCrossPiece;
				}
				else if (left && !right && !top && bottom) {
					// |.
					needWall = bpWallCornerOut;
					needAngle = 0.5f;
				}
				else if (!left && right && !top && bottom) {
					// .|
					needWall = bpWallCornerOut;
					needAngle = 0.75f;
				}
				else if (!left && right && top && !bottom) {
					// '|
					needWall = bpWallCornerOut;
					needAngle = 0.0f;
				}
				else if (left && !right && top && !bottom) {
					// |'
					needWall = bpWallCornerOut;
					needAngle = 0.25f;
				}
				else if (!left && right) {
					needWall = bpWall;
					needAngle = 0.25f;
				}
				else if (!bottom && top) {
					needWall = bpWall;
					needAngle = 0.5f;
				}
				else if (left && !right) {
					needWall = bpWall;
					needAngle = 0.75f;
				}
				else if (bottom && !top) {
					needWall = bpWall;
					needAngle = 0.0f;
				}
				// a strip one tile wide: a straight piece across it
				else if (left && right && !(bottom && top)) {
					needWall = bpWall;
					needAngle = 0.25f;
				}
				else if (bottom && top && !(left && right)) {
					needWall = bpWall;
					needAngle = 0.0f;
				}
				return StampdownCreateAction{ needWall, Vector3(wx * 5.0f + 2.5f, position.y, wz * 5.0f + 2.5f),
					Vector3(0.0f, needAngle * 3.1415f * 2.0f, 0.0f) };
			};

			for (int wx = nx0; wx <= nx1; ++wx) {
				for (int wz = nz0; wz <= nz1; ++wz) {
					if (outside(wx, wz))
						continue;
					// what stands there already: kept if it is that piece (built or not, stockade
					// or stone), else taken down; a gatehouse is kept where the wall runs on straight
					const StampdownCreateAction piece = pieceFor(wx, wz);
					const int needQuarter = (int)std::lround(piece.orientation.y / (3.1415f * 0.5f)) % 4;
					bool keep = false;
					for (CommonGameObject* wall : wallsOn(wx, wz)) {
						const int type = wall->blueprint->buildingType;
						const int quarter = ((int)std::lround(wall->orientation.y / (3.1415f * 0.5f)) % 4 + 4) % 4;
						const bool same = piece.blueprint && type == piece.blueprint->buildingType && (piece.blueprint == bpWallCrossPiece || quarter == needQuarter);
						if (same)
							keep = true;
						else if (piece.blueprint == bpWall && type == Tags::BUILDINGTYPE_GATEHOUSE) {
							keptByGate.push_back({ wall->id, piece });
							keep = true;
						}
						else
							remove(wall);
					}
					if (piece.blueprint && !keep)
						plan.toCreate.push_back(piece);
				}
			}
			// a gatehouse taken down on one of its tiles (where a corner goes now, or the city has
			// grown over it) leaves its other tiles to straight pieces, also those past the tiles
			// redone above
			for (const auto& [gate, piece] : keptByGate)
				if (std::any_of(plan.toRemove.begin(), plan.toRemove.end(), [&](const StampdownRemoveAction& r) { return r.object == gate; }))
					plan.toCreate.push_back(piece);
			if (gameState->tiles) {
				for (const auto& removal : std::vector<StampdownRemoveAction>(plan.toRemove)) {
					const CommonGameObject* gate = removal.object.getFrom(gameState);
					if (!gate || gate->blueprint->buildingType != Tags::BUILDINGTYPE_GATEHOUSE)
						continue;
					const int gx = (int)(gate->position.x / 5.0f), gz = (int)(gate->position.z / 5.0f);
					for (int wx = std::max(gx - 3, 0); wx <= std::min(gx + 3, tileWidth - 1); ++wx)
						for (int wz = std::max(gz - 3, 0); wz <= std::min(gz + 3, tileHeight - 1); ++wz) {
							if (nx0 <= wx && wx <= nx1 && nz0 <= wz && wz <= nz1)
								continue;
							if (gameState->tiles[wz * tileWidth + wx].building.getFrom(gameState) != gate || outside(wx, wz))
								continue;
							if (const StampdownCreateAction piece = pieceFor(wx, wz); piece.blueprint)
								plan.toCreate.push_back(piece);
						}
				}
			}
		}
	}

	// CREATE_GATEHOUSE_WHEN_AUTOWALLING (Manor, towns): a gatehouse in place of four straight wall
	// pieces in the middle of the side the building's door faces (the model's +z, turned with the
	// building; the Manor's door faces its palisade's opening on 4 Peaks), so that the ring has a
	// way out. That the original takes the door's side is from playing it, not traced.
	// A gatehouse the city grew over is laid again in the new run of wall nearest to where it
	// stood, so that the ring keeps its way out (not traced either).
	const CommonGameObject* lostGate = nullptr;
	for (const auto& removal : plan.toRemove)
		if (const CommonGameObject* obj = removal.object.getFrom(gameState); obj && obj->blueprint->buildingType == Tags::BUILDINGTYPE_GATEHOUSE)
			lostGate = obj;
	if (civic && (blueprint->createGatehouseWhenAutowalling || lostGate) && gameState->terrain && !plan.toCreate.empty()) {
		const GameSet* gameSet = gameState->gameSet.get();
		// (the player's wall type, as the ring's: "Wall Index" / 2)
		static const char* const wallSets[4] = { "Stockade", "Unaligned Wall", "Technological Wall", "Celestial Wall" };
		static const char* const gateSets[4] = { "Stockade", "Unaligned", "Technological", "Celestial" };
		const int wallIndexItem = gameSet->items.names.getIndex("Wall Index");
		const int wallSet = wallIndexItem >= 0 ? std::clamp((int)playerObj->getItem(wallIndexItem) / 2, 0, 3) : 0;
		const GameObjBlueprint* bpWall = gameSet->findBlueprint(Tags::GAMEOBJCLASS_BUILDING, std::string(wallSets[wallSet]) + " Foundation");
		const GameObjBlueprint* bpGate = gameSet->findBlueprint(Tags::GAMEOBJCLASS_BUILDING, std::string(gateSets[wallSet]) + " Gatehouse Foundation");
		if (!bpWall || !bpGate) {
			bpWall = gameSet->findBlueprint(Tags::GAMEOBJCLASS_BUILDING, "Stockade Foundation");
			bpGate = gameSet->findBlueprint(Tags::GAMEOBJCLASS_BUILDING, "Stockade Gatehouse Foundation");
		}
		const Vector3 door(std::sin(orientation.y), 0, std::cos(orientation.y));
		// the new straight pieces by side (their angle and the line they stand on), then split
		// into unbroken runs: where the city grew, a side's new pieces can be several runs
		std::map<std::pair<int, int>, std::vector<size_t>> lines;
		for (size_t i = 0; i < plan.toCreate.size(); i++)
			if (plan.toCreate[i].blueprint == bpWall) {
				const int angle = (int)std::lround(plan.toCreate[i].orientation.y * 100.0f);
				const Vector3& p = plan.toCreate[i].position;
				const bool alongZ = angle == 157 || angle == 471;
				lines[{ angle, (int)std::floor((alongZ ? p.x : p.z) / 5.0f) }].push_back(i);
			}
		std::vector<std::vector<size_t>> sides;
		for (auto& [key, pieces] : lines) {
			std::sort(pieces.begin(), pieces.end(), [&](size_t a, size_t b) {
				const Vector3& pa = plan.toCreate[a].position;
				const Vector3& pb = plan.toCreate[b].position;
				return pa.x + pa.z < pb.x + pb.z; });
			for (size_t k = 0; k < pieces.size(); k++) {
				const Vector3& prev = plan.toCreate[pieces[k ? k - 1 : 0]].position;
				const Vector3& cur = plan.toCreate[pieces[k]].position;
				if (k == 0 || std::abs(cur.x + cur.z - prev.x - prev.z) > 5.5f)
					sides.emplace_back();
				sides.back().push_back(pieces[k]);
			}
		}
		size_t bestSide = 0;
		float bestAhead = -1e30f;
		for (size_t side = 0; side < sides.size(); side++) {
			const std::vector<size_t>& pieces = sides[side];
			if (pieces.size() < 4)
				continue;
			Vector3 mid(0, 0, 0);
			for (size_t i : pieces)
				mid += plan.toCreate[i].position;
			mid *= 1.0f / pieces.size();
			const float ahead = lostGate ? -(mid - lostGate->position).len2xz()
				: (mid.x - position.x) * door.x + (mid.z - position.z) * door.z;
			if (ahead > bestAhead) { bestAhead = ahead; bestSide = side; }
		}
		if (bpGate && bestAhead > -1e30f) {
			const std::vector<size_t> pieces = sides[bestSide];
			const size_t first = pieces.size() / 2 - 2;
			Vector3 at(0, 0, 0);
			for (size_t k = 0; k < 4; k++)
				at += plan.toCreate[pieces[first + k]].position;
			at *= 0.25f;
			const Vector3 orientation = plan.toCreate[pieces[first]].orientation;
			std::vector<size_t> drop(pieces.begin() + first, pieces.begin() + first + 4);
			std::sort(drop.rbegin(), drop.rend());
			for (size_t i : drop)
				plan.toCreate.erase(plan.toCreate.begin() + i);
			plan.toCreate.push_back({ bpGate, at, orientation });
		}
	}

	// a new wall where a gatehouse stands (not along it): WKB.exe refuses it (0x51123c)
	if (cityRefusal == Status::Valid)
		for (const auto& removal : plan.toRemove)
			if (const CommonGameObject* obj = removal.object.getFrom(gameState); obj && obj->blueprint->buildingType == Tags::BUILDINGTYPE_GATEHOUSE)
				cityRefusal = Status::InvalidGatehouseOverlap;
	plan.status = cityRefusal;
	return plan;
}
