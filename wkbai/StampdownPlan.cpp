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

bool (*StampdownPlan::exemptFromSlopeLimit)(CommonGameState*, CommonGameObject*) = nullptr;

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
		// The slope and the neighbours are for human players only: an AI's starting village moved
		// a tile by the limit left its cart with no way out (8 Player Continent, seed 1: no wood,
		// no farm, 15 characters all game), so the AIs place as before and their levelling is
		// capped instead (Server::levelTerrainUnder). The terrain types hold for them too, and
		// their search for a site (NEAREST_VALID_STAMPDOWN_POS) looks for them.
		const Terrain* trn = gameState->terrain;
		const bool slopeExempt = exemptFromSlopeLimit && exemptFromSlopeLimit(gameState, playerObj);
		std::vector<std::pair<int, int>> taken;
		for (const auto& to : fp->tiles) {
			const auto [rx, rz] = to.rotate(orientation.y);
			taken.emplace_back(ox + rx, oz + rz);
		}
		float lowest = 1e30f, highest = -1e30f;
		for (size_t t = 0; t < fp->tiles.size(); t++) {
			const auto& to = fp->tiles[t];
			const auto [px, pz] = taken[t];
			if (px < 0 || pz < 0 || px >= tileWidth || pz >= tileHeight) {
				plan.status = Status::InvalidTerrain;
				return plan;
			}
			const auto& tile = gameState->tiles[pz * tileWidth + px];
			if (const CommonGameObject* other = tile.building.getFrom(gameState)) {
				const bool onWall = civic && std::find(std::begin(wallBuildingTypes), std::end(wallBuildingTypes), other->blueprint->buildingType) != std::end(wallBuildingTypes);
				const bool shared = to.mode && tile.buildingPassable && (isWallLike(blueprint->buildingType) || isWallLike(other->blueprint->buildingType));
				if (!onWall && !shared) {
					plan.status = Status::InvalidOccupied;
					return plan;
				}
			}
			if (!tileTerrainAllowed(gameState, to, px, pz)) {
				plan.status = Status::InvalidTerrainType;
				return plan;
			}
			if (slopeExempt)
				continue;
			for (int dz = 0; dz <= 1; dz++)
				for (int dx = 0; dx <= 1; dx++) {
					const float h = trn->getVertex(px + trn->edge + dx, pz + trn->edge + dz);
					lowest = std::min(lowest, h);
					highest = std::max(highest, h);
				}
			const float range = highest - lowest;
			if (fp->maxHeightDifferential > 0.0f && range > fp->maxHeightDifferential) {
				plan.status = Status::InvalidTooSteep;
				return plan;
			}
			if (range >= 2.5f && to.flatten)
				for (int nz = -1; nz <= 1; nz++)
					for (int nx = -1; nx <= 1; nx++) {
						const int qx = px + nx, qz = pz + nz;
						if ((nx == 0 && nz == 0) || qx < 0 || qz < 0 || qx >= tileWidth || qz >= tileHeight
							|| std::find(taken.begin(), taken.end(), std::make_pair(qx, qz)) != taken.end())
							continue;
						const CommonGameObject* neighbour = gameState->tiles[qz * tileWidth + qx].building.getFrom(gameState);
						if (neighbour && std::find(std::begin(wallBuildingTypes), std::end(wallBuildingTypes), neighbour->blueprint->buildingType) == std::end(wallBuildingTypes)) {
							plan.status = Status::InvalidNeighbour;
							return plan;
						}
					}
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
		const auto cityIntersectsWithRect = [](const std::vector<CityRectangle>& cityRectangles, const CityRectangle& rect) -> bool {
			for (const auto& cityRect : cityRectangles) {
				bool intersects_x = (cityRect.xStart <= rect.xStart && rect.xStart <= cityRect.xEnd)
					|| (cityRect.xStart <= rect.xEnd && rect.xEnd <= cityRect.xEnd);
				bool intersects_y = (cityRect.yStart <= rect.yStart && rect.yStart <= cityRect.yEnd)
					|| (cityRect.yStart <= rect.yEnd && rect.yEnd <= cityRect.yEnd);
				if (intersects_x && intersects_y) {
					return true;
				}
			}
			return false;
			};

		const GameSet* gameSet = gameState->gameSet.get();
		const GameObjBlueprint* bpWall = gameSet->findBlueprint(Tags::GAMEOBJCLASS_BUILDING, "Stockade Foundation");
		const GameObjBlueprint* bpWallCornerIn = gameSet->findBlueprint(Tags::GAMEOBJCLASS_BUILDING, "Stockade Corner In Foundation");
		const GameObjBlueprint* bpWallCornerOut = gameSet->findBlueprint(Tags::GAMEOBJCLASS_BUILDING, "Stockade Corner Out Foundation");
		const GameObjBlueprint* bpWallCrossPiece = gameSet->findBlueprint(Tags::GAMEOBJCLASS_BUILDING, "Stockade Cross Piece Foundation");
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

			cityRectangles.push_back(plan.cityRectangle);

			for (int wall_x = footprint->wallOriginDefaultMinX; wall_x <= footprint->wallOriginDefaultMaxX; ++wall_x) {
				for (int wall_y = footprint->wallOriginDefaultMinY; wall_y <= footprint->wallOriginDefaultMaxY; ++wall_y) {
					Vector3 wallPos = position + Vector3(wall_x, 0, wall_y) * 5.0f;
					int wx = static_cast<int>(position.x / 5.0f) + wall_x;
					int wz = static_cast<int>(position.z / 5.0f) + wall_y;

					if (!(0 <= wx && wx < tileWidth && 0 <= wz && wz < tileHeight))
						continue;

					// are we on the border of the new rectangle?
					if (wall_x == footprint->wallOriginDefaultMinX || wall_x == footprint->wallOriginDefaultMaxX
						|| wall_y == footprint->wallOriginDefaultMinY || wall_y == footprint->wallOriginDefaultMaxY) {
						enum TileType
						{
							TOutside,
							TInside,
							TWall
						};
						auto tileType = [&, tileWidth = tileWidth](int tx, int tz) {
							if (cityContainsTile(cityRectangles, tx, tz)) {
								if (!gameState->tiles)
									return TInside;
								//auto* tileBuilding = tiles[tz * tileWidth + tx].building.getFrom<Server>();
								//if (tileBuilding) {
								for (const auto& tileObjRef : gameState->tiles[tz * tileWidth + tx].objList) {
									if (CommonGameObject* tileBuilding = tileObjRef.getFrom(gameState)) {
										auto it = std::find(std::begin(wallBuildingTypes), std::end(wallBuildingTypes), tileBuilding->blueprint->buildingType);
										if (it != std::end(wallBuildingTypes)) {
											return TWall;
										}
									}
								}
								return TInside;
							}
							return TOutside;
							};

						const GameObjBlueprint* needWall = nullptr;
						float needAngle = 0.0f;

						TileType leftType = tileType(wx - 1, wz);
						TileType rightType = tileType(wx + 1, wz);
						TileType bottomType = tileType(wx, wz - 1);
						TileType topType = tileType(wx, wz + 1);
						//if (leftType != TOutside && rightType != TOutside && bottomType != TOutside && topType != TOutside) {
						//	needWall = bpWallCrossPiece;
						//	needAngle = 0.0f;
						//}
						if (leftType == TOutside && rightType != TOutside && topType != TOutside && bottomType == TOutside) {
							// |.
							needWall = bpWallCornerOut;
							needAngle = 0.5f;
						}
						else if (leftType != TOutside && rightType == TOutside && topType != TOutside && bottomType == TOutside) {
							// .|
							needWall = bpWallCornerOut;
							needAngle = 0.75f;
						}
						else if (leftType != TOutside && rightType == TOutside && topType == TOutside && bottomType != TOutside) {
							// '|
							needWall = bpWallCornerOut;
							needAngle = 0.0f;
						}
						else if (leftType == TOutside && rightType != TOutside && topType == TOutside && bottomType != TOutside) {
							// |'
							needWall = bpWallCornerOut;
							needAngle = 0.25f;
						}
						else if (leftType != TOutside && rightType == TOutside) {
							needWall = bpWall;
							needAngle = 0.25f;
						}
						else if (bottomType != TOutside && topType == TOutside) {
							needWall = bpWall;
							needAngle = 0.5f;
						}
						else if (leftType == TOutside && rightType != TOutside) {
							needWall = bpWall;
							needAngle = 0.75f;
						}
						else if (bottomType == TOutside && topType != TOutside) {
							needWall = bpWall;
							needAngle = 0.0f;
						}

						if (needWall) {
							StampdownCreateAction& creation = plan.toCreate.emplace_back();
							creation.blueprint = needWall;
							creation.position = wallPos;
							creation.orientation = Vector3(0.0f, needAngle * 3.1415f * 2.0f, 0.0f);
						}
					}
					else {
						if (!gameState->tiles)
							continue;
						for (const auto& tileObjRef : gameState->tiles[wz * tileWidth + wx].objList) {
							if (CommonGameObject* tileBuilding = tileObjRef.getFrom(gameState)) {
								auto it = std::find(std::begin(wallBuildingTypes), std::end(wallBuildingTypes), tileBuilding->blueprint->buildingType);
								if (it != std::end(wallBuildingTypes)) {
									// wall now inside city, remove it
									StampdownRemoveAction& removal = plan.toRemove.emplace_back();
									removal.object = tileBuilding->id;
								}
							}
						}
					}
				}
			}
		}
	}

	// CREATE_GATEHOUSE_WHEN_AUTOWALLING (Manor, towns): a gatehouse in place of four straight wall
	// pieces in the middle of the side the building's door faces (the model's +z, turned with the
	// building; the Manor's door faces its palisade's opening on 4 Peaks), so that the ring has a
	// way out. That the original takes the door's side is from playing it, not traced.
	if (civic && blueprint->createGatehouseWhenAutowalling && gameState->terrain && !plan.toCreate.empty()) {
		const GameSet* gameSet = gameState->gameSet.get();
		const GameObjBlueprint* bpWall = gameSet->findBlueprint(Tags::GAMEOBJCLASS_BUILDING, "Stockade Foundation");
		const GameObjBlueprint* bpGate = gameSet->findBlueprint(Tags::GAMEOBJCLASS_BUILDING, "Stockade Gatehouse Foundation");
		const Vector3 door(std::sin(orientation.y), 0, std::cos(orientation.y));
		// the straight pieces by side (their angle)
		std::map<int, std::vector<size_t>> sides;
		for (size_t i = 0; i < plan.toCreate.size(); i++)
			if (plan.toCreate[i].blueprint == bpWall)
				sides[(int)std::lround(plan.toCreate[i].orientation.y * 100.0f)].push_back(i);
		int bestSide = 0;
		float bestAhead = -1e30f;
		for (const auto& [side, pieces] : sides) {
			if (pieces.size() < 4)
				continue;
			Vector3 mid(0, 0, 0);
			for (size_t i : pieces)
				mid += plan.toCreate[i].position;
			mid *= 1.0f / pieces.size();
			const float ahead = (mid.x - position.x) * door.x + (mid.z - position.z) * door.z;
			if (ahead > bestAhead) { bestAhead = ahead; bestSide = side; }
		}
		if (bpGate && bestAhead > -1e30f) {
			std::vector<size_t> pieces = sides[bestSide];
			// in order along the side
			std::sort(pieces.begin(), pieces.end(), [&](size_t a, size_t b) {
				const Vector3& pa = plan.toCreate[a].position;
				const Vector3& pb = plan.toCreate[b].position;
				return pa.x + pa.z < pb.x + pb.z; });
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

	plan.status = Status::Valid; // temporary
	return plan;
}
