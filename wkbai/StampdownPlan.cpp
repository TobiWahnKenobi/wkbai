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
		// MAX_FOUNDATION_HEIGHT_DIFFERENTIAL: the lowest and highest ground under the footprint's
		// tiles that are levelled (DEFORM_TERRAIN FLATTEN; their corners) may differ by at most
		// that much, in metres. Fields and walls level nothing and are not limited. Human players
		// only: an AI's starting village moved a tile by the limit left its cart with no way out
		// (8 Player Continent, seed 1: no wood, no farm, 15 characters all game), so the AIs
		// place as before and their levelling is capped instead (Server::levelTerrainUnder).
		const Terrain* trn = gameState->terrain;
		float lowest = 1e30f, highest = -1e30f;
		for (const auto& to : fp->tiles) {
			const auto [rx, rz] = to.rotate(orientation.y);
			const int px = ox + rx, pz = oz + rz;
			if (px < 0 || pz < 0 || px >= tileWidth || pz >= tileHeight) {
				plan.status = Status::InvalidTerrain;
				return plan;
			}
			for (int dz = 0; dz <= 1 && to.flatten; dz++)
				for (int dx = 0; dx <= 1; dx++) {
					const float h = trn->getVertex(px + trn->edge + dx, pz + trn->edge + dz);
					lowest = std::min(lowest, h);
					highest = std::max(highest, h);
				}
			const auto& tile = gameState->tiles[pz * tileWidth + px];
			const CommonGameObject* other = tile.building.getFrom(gameState);
			if (!other)
				continue;
			if (civic && std::find(std::begin(wallBuildingTypes), std::end(wallBuildingTypes), other->blueprint->buildingType) != std::end(wallBuildingTypes))
				continue;
			if (to.mode && tile.buildingPassable && (isWallLike(blueprint->buildingType) || isWallLike(other->blueprint->buildingType)))
				continue;
			plan.status = Status::InvalidOccupied;
			return plan;
		}
		if (fp->maxHeightDifferential > 0.0f && highest > lowest && highest - lowest > fp->maxHeightDifferential
			&& !(exemptFromSlopeLimit && exemptFromSlopeLimit(gameState, playerObj))) {
			plan.status = Status::InvalidTerrain;
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
		const GameObjBlueprint* bpWall = gameSet->findBlueprint(Tags::GAMEOBJCLASS_BUILDING, "Stockade Foundation");
		const GameObjBlueprint* bpGate = gameSet->findBlueprint(Tags::GAMEOBJCLASS_BUILDING, "Stockade Gatehouse Foundation");
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

	plan.status = Status::Valid; // temporary
	return plan;
}
