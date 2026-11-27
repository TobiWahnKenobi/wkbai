// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#include "ObjectCreation.h"
#include "../util/GSFileParser.h"
#include "gameset.h"
#include "finder.h"
#include "position.h"
#include "../server.h"
#include "ScriptContext.h"
#include "../util/util.h"
#include "../MovementController.h"
#include "../terrain.h"

// A character created LOCATION_OF a building stands on the building's own tiles. The Barbarian
// Generator's raiders (Mechanics\Barbarian Generator.cpp, "Create a Barbarian Raider") are created
// at the village's centre, which its footprint ("Barbarian Settlement") blocks, inside the ring of
// huts: they stood there and never came out to attack. Such a character comes out on the nearest
// tile it can stand on outside that building's footprint (the village's edge), as a trained unit
// comes out at its building's door (Order.cpp SpawnSpot). Anywhere else it stays where it was made.
static Vector3 OffBuildingTiles(const GameObjBlueprint* type, const Vector3& p)
{
	Server* server = Server::instance;
	if (!server || !server->terrain || !server->tiles || type->bpClass != Tags::GAMEOBJCLASS_CHARACTER)
		return p;
	const auto [w, h] = server->terrain->getNumPlayableTiles();
	const int tx = (int)(p.x / 5.0f), tz = (int)(p.z / 5.0f);
	if (tx < 0 || tz < 0 || tx >= w || tz >= h)
		return p;
	const Server::Tile& own = server->tiles[(size_t)tz * w + tx];
	const CommonGameObject* building = own.building.getFrom(server);
	if (!building || own.buildingPassable || !MovementController::isTileBlocked(type, tx, tz))
		return p;
	// square rings round the tile, the nearest free tile to the point (rings one further than the
	// first found, as a tile of the next ring can be nearer than a corner of this one)
	int bx = -1, bz = -1, lastRing = 24;
	float best = 0.0f;
	for (int r = 1; r <= lastRing; r++) {
		for (int z = tz - r; z <= tz + r; z++)
			for (int x = tx - r; x <= tx + r; x++) {
				if (std::max(std::abs(x - tx), std::abs(z - tz)) != r || x < 0 || z < 0 || x >= w || z >= h)
					continue;
				if (server->tiles[(size_t)z * w + x].building.getFrom(server) == building || MovementController::isTileBlocked(type, x, z))
					continue;
				const float dx = x * 5.0f + 2.5f - p.x, dz = z * 5.0f + 2.5f - p.z, d = dx * dx + dz * dz;
				if (bx < 0 || d < best) {
					bx = x; bz = z; best = d;
				}
			}
		if (bx >= 0 && lastRing > r + 1)
			lastRing = r + 1;
	}
	if (bx < 0)
		return MovementController::nearestValidPosition(type, p);
	Vector3 q(bx * 5.0f + 2.5f, 0.0f, bz * 5.0f + 2.5f);
	q.y = server->terrain->getHeightEx(q.x, q.z, type->canWalkOnWater());
	return q;
}

void ObjectCreation::parse(GSFileParser & gsf, const GameSet & gs)
{
	gsf.advanceLine();
	while (!gsf.eof) {
		auto tag = gsf.nextTag();
		if (tag == "TYPE_TO_CREATE") {
			typeToCreate = gs.readObjBlueprintPtr(gsf);
		}
		else if (tag == "MAPPED_TYPE_TO_CREATE") {
			mappedTypeToCreate = gs.typeTags.readIndex(gsf);
			mappedTypeFrom = ReadFinder(gsf, gs);
		}
		else if (tag == "CONTROLLER") {
			controller = ReadFinder(gsf, gs);
		}
		else if (tag == "CREATE_AT") {
			createAt = PositionDeterminer::createFrom(gsf, gs);
		}
		else if (tag == "POST_CREATION_SEQUENCE") {
			postCreationSequence.init(gsf, gs, "END_POST_CREATION_SEQUENCE");
		}
		else if (tag == "SUBORDINATES") {
			subordinates = ReadFinder(gsf, gs);
		}
		else if (tag == "END_OBJECT_CREATION")
			break;
		gsf.advanceLine();
	}
}

void ObjectCreation::run(ServerGameObject * creator, SrvScriptContext* ctx) const
{
	// a post-creation sequence that creates again (the Kingmaker AI's "Muster Marker" tries other
	// places until a battering ram could reach the palace): at most 64 deep, not a stack overflow
	static int depth = 0;
	if (depth >= 64) {
		printf("WARNING: object creations nested more than 64 deep, stopped\n");
		return;
	}
	struct DepthGuard { DepthGuard() { depth++; } ~DepthGuard() { depth--; } } depthGuard;
	auto _0 = ctx->changeSelf(creator);
	auto _1 = ctx->change(ctx->creator, creator);
	const GameObjBlueprint* type = nullptr;
	if (typeToCreate)
		type = typeToCreate;
	else if (mappedTypeFrom) {
		auto* obj = mappedTypeFrom->getFirst(ctx);
		auto it = obj->blueprint->mappedTypeTags.find(mappedTypeToCreate);
		if (it != obj->blueprint->mappedTypeTags.end())
			type = it->second;
	}
	else
		ferr("OBJECT_CREATION has no object type defined!");
	if (!type)
		return;
	ServerGameObject* ctrl = controller->getFirst(ctx);
	if (!ctrl) {
		printf("WARNING: OBJECT_CREATION failed as no CONTROLLER found.\n");
		return;
	}
	OrientedPosition opos = createAt ? createAt->eval(ctx) : OrientedPosition({ creator->position, creator->orientation });
	static const bool onBuilding = getenv("WKB_SPAWN_ON_BUILDING") != nullptr; // (the old way: test 124's before)
	if (!onBuilding)
		opos.position = OffBuildingTiles(type, opos.position);
	ServerGameObject* created = Server::instance->spawnObject(type, ctrl, opos.position, opos.rotation);
	// a building a script creates levels its ground as a stamped-down one does: WKB.exe puts every
	// building on the map through 0x513790, which collects the FLATTEN tiles' corners (flag 1 to
	// 0x514790, 0x5149e7) and levels them (0x514a20), wherever the building came from. The farmer's
	// "Auto Farm" (Peasant Unit.cpp) is one; wkbai levelled only in stampdownObject.
	Server::instance->levelTerrainUnder(created);
	// wkbai's unit history: who made it (a missile: who fired it, or the troop in a building)
	Server::instance->unitHistory.created(*Server::instance, created, creator);
	if (subordinates) {
		for (ServerGameObject* sub : subordinates->eval(ctx))
			sub->setParent(created);
	}
	if (type->bpClass == Tags::GAMEOBJCLASS_FORMATION) {
		created->sendEvent(Tags::PDEVENT_ON_SPAWN);
	}
	auto _2 = ctx->changeSelf(created);
	postCreationSequence.run(ctx);
}
