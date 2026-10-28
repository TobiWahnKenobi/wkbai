// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#include "values.h"
#include "gameset.h"
#include "../util/GSFileParser.h"
#include "../tags.h"
#include "../util/util.h"
#include <string>
#include "finder.h"
#include <algorithm>
#include "../server.h"
#include "../client.h"
#include "CommonEval.h"
#include "position.h"
#include "ScriptContext.h"
#include "../terrain.h"
#include "../Pathfinding.h"
#include "KeywordStubs.h"
#include "Commission.h"
#include "../AIController.h"
#include "../MovementController.h"
#include <unordered_set>
#include "../util/SimRandom.h"

namespace {
	float RandomFromZeroToOne(ScriptContext* ctx) { return (float)(ScriptRand(ctx) & 0xFFFF) / 32768.0f; }
}

//namespace Script
//{

float ValueDeterminer::fail(ScriptContext* ctx) {
	ferr("Invalid value determiner call from %s", ctx->gameState->getProgramName());
	return 0.0f;
}

struct ValueUnknown : ValueDeterminer {
	std::string name;
	virtual float eval(ScriptContext* ctx) override { ferr("Unknown value determiner %s called from the %s!", name.c_str(), ctx->gameState->getProgramName()); return 0.0f; }
	virtual void parse(GSFileParser &gsf, const GameSet &gs) override {}
	ValueUnknown(const std::string& name) : name(name) {}
};

// Known keyword without an implementation yet, see KeywordStubs.h
struct ValueStub : ValueDeterminer {
	KeywordStubArgs args;
	virtual float eval(ScriptContext* ctx) override { args.warnOnce(); return 0.0f; }
	virtual void parse(GSFileParser &gsf, const GameSet &gs) override { args.parse(gsf, gs); }
	ValueStub(const std::string& name, const char* signature) : args(name, signature) {}
};

struct ValueConstant : ValueDeterminer {
	float value;
	virtual float eval(ScriptContext* ctx) override { return value; }
	virtual void parse(GSFileParser &gsf, const GameSet &gs) override { value = gsf.nextFloat(); }
	ValueConstant() {}
	ValueConstant(float value) : value(value) {}
};

struct ValueItemValue : ValueDeterminer {
	int item;
	std::unique_ptr<ObjectFinder> finder;
	float eval(ScriptContext *ctx) override {
		if (CommonGameObject* obj = finder->getFirst(ctx))
			return obj->getItem(item);
		return 0.0f;
	}
	virtual void parse(GSFileParser &gsf, const GameSet &gs) override {
		item = gs.items.readIndex(gsf);
		finder.reset(ReadFinder(gsf, gs));
	}
	ValueItemValue() {}
	ValueItemValue(int item, ObjectFinder *finder) : item(item), finder(finder) {}
};

struct ValueObjectId_WKO : ValueDeterminer {
	std::unique_ptr<ObjectFinder> finder1, finder2;
	virtual float eval(ScriptContext* ctx) override {
		CommonGameObject* obj1 = finder1->getFirst(ctx);
		CommonGameObject* obj2 = finder2->getFirst(ctx);
		if (obj1 && obj2)
			return (obj1 == obj2) ? 1.0f : 0.0f;
		return 0.0f;
	}
	virtual void parse(GSFileParser& gsf, const GameSet& gs) override {
		finder1.reset(ReadFinder(gsf, gs));
		finder2.reset(ReadFinder(gsf, gs));
	}
};

struct ValueObjectId_Battles : ValueDeterminer {
	std::unique_ptr<ObjectFinder> finder;
	virtual float eval(ScriptContext* ctx) override {
		if (CommonGameObject *obj = finder->getFirst(ctx))
			return (float)obj->id;
		return 0.0f;
	}
	virtual void parse(GSFileParser &gsf, const GameSet &gs) override {
		finder.reset(ReadFinder(gsf, gs));
	}
	ValueObjectId_Battles() {}
	ValueObjectId_Battles(ObjectFinder *finder) : finder(finder) {}
};

struct ValueObjectClass : ValueDeterminer {
	int objclass;
	std::unique_ptr<ObjectFinder> finder;
	virtual float eval(ScriptContext* ctx) override {
		auto vec = finder->eval(ctx);
		return !vec.empty() && std::all_of(vec.begin(), vec.end(), [this](CommonGameObject *obj) {return obj->blueprint->bpClass == objclass; });
	}
	virtual void parse(GSFileParser &gsf, const GameSet &gs) override {
		objclass = Tags::GAMEOBJCLASS_tagDict.getTagID(gsf.nextString().c_str());
		finder.reset(ReadFinder(gsf, gs));
	}
	ValueObjectClass() {}
	ValueObjectClass(int objclass, ObjectFinder *finder) : objclass(objclass), finder(finder) {}
};

struct ValueEquationResult : ValueDeterminer {
	int equation;
	std::unique_ptr<ObjectFinder> finder;
	virtual float eval(ScriptContext* ctx) override {
		CommonGameObject *obj = finder->getFirst(ctx);
		if (!obj) return 0.0f;
		auto _ = ctx->changeSelf(obj);
		return ctx->gameState->gameSet->equations[equation]->eval(ctx);
	}
	virtual void parse(GSFileParser &gsf, const GameSet &gs) override {
		equation = gs.equations.readIndex(gsf);
		finder.reset(ReadFinder(gsf, gs));
	}
	//ValueEquationResult() {}
	//ValueEquationResult(int equation, ObjectFinder *finder) : equation(equation), finder(finder) {}
};

struct ValueIsSubsetOf : ValueDeterminer {
	std::unique_ptr<ObjectFinder> fnd_sub, fnd_super;
	virtual float eval(ScriptContext* ctx) override {
		auto sub = fnd_sub->eval(ctx);
		auto super = fnd_super->eval(ctx);
		if (sub.empty() || super.empty())
			return 0.0f;
		for (auto *obj : sub)
			if (std::find(super.begin(), super.end(), obj) == super.end())
				return 0.0f;
		return 1.0f;
	}
	virtual void parse(GSFileParser &gsf, const GameSet &gs) override {
		fnd_sub.reset(ReadFinder(gsf, gs));
		fnd_super.reset(ReadFinder(gsf, gs));
	}
};

struct ValueNumObjects : ValueDeterminer {
	std::unique_ptr<ObjectFinder> finder;
	virtual float eval(ScriptContext* ctx) override {
		return (float)finder->eval(ctx).size();
	}
	virtual void parse(GSFileParser &gsf, const GameSet &gs) override {
		finder.reset(ReadFinder(gsf, gs));
	}
};

struct ValueCanReach : ValueDeterminer {
	std::unique_ptr<ObjectFinder> finder;
	std::unique_ptr<PositionDeterminer> source;
	virtual float eval(ScriptContext* ctx) override {
		// everything is reachable from anywhere for now
		return 1.0f;
	}
	virtual void parse(GSFileParser &gsf, const GameSet &gs) override {
		finder.reset(ReadFinder(gsf, gs));
		source.reset(PositionDeterminer::createFrom(gsf, gs));
	}
};

struct ValueIsAccessible : ValueDeterminer {
	std::unique_ptr<ObjectFinder> finder1, finder2;
	virtual float eval(ScriptContext* ctx) override {
		// everything is reachable from anywhere for now
		return 1.0f;
	}
	virtual void parse(GSFileParser& gsf, const GameSet& gs) override {
		finder1.reset(ReadFinder(gsf, gs));
		finder2.reset(ReadFinder(gsf, gs));
	}
};

struct ValueHasAppearance : ValueDeterminer {
	std::unique_ptr<ObjectFinder> finder;
	int appear;
	virtual float eval(ScriptContext* ctx) override {
		for (CommonGameObject *obj : finder->eval(ctx))
			if (obj->appearance != appear)
				return 0.0f;
		return 1.0f;
	}
	virtual void parse(GSFileParser &gsf, const GameSet &gs) override {
		finder.reset(ReadFinder(gsf, gs));
		appear = gs.appearances.readIndex(gsf);
	}
};

struct ValueSamePlayer : ValueDeterminer {
	std::unique_ptr<ObjectFinder> fnd1, fnd2;
	virtual float eval(ScriptContext* ctx) override {
		auto vec1 = fnd1->eval(ctx);
		auto vec2 = fnd2->eval(ctx);
		if (vec1.empty() || vec2.empty()) return 0.0f;
		for (CommonGameObject *b : vec2) {
			CommonGameObject *bp = b->getPlayer();
			if (std::find_if(vec1.begin(), vec1.end(), [&bp](CommonGameObject * a) {return a->getPlayer() == bp;}) == vec1.end())
				return 0.0f;
		}
		return 1.0f;
	}
	virtual void parse(GSFileParser &gsf, const GameSet &gs) override {
		fnd1.reset(ReadFinder(gsf, gs));
		fnd2.reset(ReadFinder(gsf, gs));
	}
};

struct ValueObjectType : ValueDeterminer {
	const GameObjBlueprint *type;
	std::unique_ptr<ObjectFinder> finder;
	virtual float eval(ScriptContext* ctx) override {
		auto vec = finder->eval(ctx);
		if (vec.empty()) return 0.0f;
		for (auto *obj : vec)
			if (obj->blueprint != type)
				return 0.0f;
		return 1.0f;
	}
	virtual void parse(GSFileParser &gsf, const GameSet &gs) override {
		type = gs.readObjBlueprintPtr(gsf);
		finder.reset(ReadFinder(gsf, gs));
	}
};

struct ValueDistanceBetween : ValueDeterminer {
	bool takeHorizontal, takeVertical;
	std::unique_ptr<ObjectFinder> fnd1, fnd2;
	Vector3 centre(const ObjectFinderResult& vec) {
		if (vec.empty()) return {};
		Vector3 sum;
		for (CommonGameObject* obj : vec)
			sum += obj->position;
		return sum / (float)vec.size();
	}
	virtual float eval(ScriptContext* ctx) override {
		Vector3 v = centre(fnd1->eval(ctx)) - centre(fnd2->eval(ctx));
		float dist = 0.0f;
		if (takeHorizontal) dist += v.x * v.x + v.z * v.z;
		if (takeVertical) dist += v.y * v.y;
		return std::sqrt(dist);
	}
	virtual void parse(GSFileParser &gsf, const GameSet &gs) override {
		auto tag = gsf.nextString();
		if (tag == "HORIZONTAL")	{ takeHorizontal = true;  takeVertical = false; }
		else if (tag == "3D")		{ takeHorizontal = true;  takeVertical = true; }
		else if (tag == "VERTICAL")	{ takeHorizontal = false; takeVertical = true; }
		fnd1.reset(ReadFinder(gsf, gs));
		fnd2.reset(ReadFinder(gsf, gs));
	}
};

struct ValueValueTagInterpretation : ValueDeterminer {
	int valueTag;
	std::unique_ptr<ObjectFinder> fnd;
	virtual float eval(ScriptContext* ctx) override {
		CommonGameObject *obj = fnd->getFirst(ctx);
		if (!obj) return 0.0f;
		// TODO: Type specific interpretations
		ValueDeterminer* vd = nullptr;
		auto it = obj->blueprint->mappedValueTags.find(valueTag);
		if (it != obj->blueprint->mappedValueTags.end())
			vd = it->second;
		else
			vd = ctx->gameState->gameSet->valueTags[valueTag].get();
		if (!vd) return 0.0f;
		auto _ = ctx->changeSelf(obj);
		return vd->eval(ctx);
	}
	virtual void parse(GSFileParser &gsf, const GameSet &gs) override {
		valueTag = gs.valueTags.readIndex(gsf);
		fnd.reset(ReadFinder(gsf, gs));
	}
};

struct ValueDiplomaticStatusAtLeast : ValueDeterminer {
	int status;
	std::unique_ptr<ObjectFinder> a, b;
	virtual float eval(ScriptContext* ctx) override {
		auto objlist1 = a->eval(ctx);
		auto objlist2 = b->eval(ctx);
		if (objlist1.empty() || objlist2.empty())
			return 0.0f; // TODO: WARNING
		for (auto* obj1 : objlist1) {
			for (auto* obj2 : objlist2) {
				if (ctx->gameState->getDiplomaticStatus(obj1->getPlayer(), obj2->getPlayer()) > status)
					return 0.0f;
			}
		}
		return 1.0f;
	}
	virtual void parse(GSFileParser &gsf, const GameSet &gs) override {
		status = gs.diplomaticStatuses.readIndex(gsf);
		a.reset(ReadFinder(gsf, gs));
		b.reset(ReadFinder(gsf, gs));
	}
};

// ARE_ASSOCIATED <a> <category> <b>: whether every a is an associate of every b in that category
// (b did REGISTER_ASSOCIATES b <category> a), as WKB.exe's VD_Are_Associated (0x454900): each a's
// associators must include each b; an a without any is 0, no a at all is 1. wkbre2 checked the
// other way round, so "Not the Village I Collect From" let carts deliver to their own village.
struct ValueAreAssociated : ValueDeterminer {
	int category;
	std::unique_ptr<ObjectFinder> a, b;
	virtual float eval(ScriptContext* ctx) override {
		if (!ctx->isServer()) return fail(ctx);
		const auto as = a->eval(ctx), bs = b->eval(ctx);
		for (CommonGameObject* x : as) {
			auto& associators = ((ServerGameObject*)x)->associators;
			auto it = associators.find(category);
			if (it == associators.end())
				return 0.0f;
			for (CommonGameObject* y : bs)
				if (!it->second.count((ServerGameObject*)y))
					return 0.0f;
		}
		return 1.0f;
	}
	virtual void parse(GSFileParser &gsf, const GameSet &gs) override {
		a.reset(ReadFinder(gsf, gs));
		category = gs.associations.readIndex(gsf);
		b.reset(ReadFinder(gsf, gs));
	}
};

struct ValueBlueprintItemValue : ValueDeterminer {
	int item;
	const GameObjBlueprint *type;
	virtual float eval(ScriptContext* ctx) override {
		auto it = type->startItemValues.find(item);
		if (it != type->startItemValues.end())
			return it->second;
		else return 0.0f;
	}
	virtual void parse(GSFileParser &gsf, const GameSet &gs) override {
		item = gs.items.readIndex(gsf);
		type = gs.readObjBlueprintPtr(gsf);
	}
};

struct ValueTotalItemValue : ValueDeterminer {
	int item;
	std::unique_ptr<ObjectFinder> finder;
	virtual float eval(ScriptContext* ctx) override {
		float sum = 0.0f;
		for (CommonGameObject* obj : finder->eval(ctx))
			sum += obj->getItem(item);
		return sum;
	}
	virtual void parse(GSFileParser &gsf, const GameSet &gs) override {
		item = gs.items.readIndex(gsf);
		finder.reset(ReadFinder(gsf, gs));
	}
};

struct ValueIsIdle : ValueDeterminer {
	std::unique_ptr<ObjectFinder> finder;
	virtual float eval(ScriptContext* ctx) override {
		if (!ctx->isServer()) return fail(ctx);
		auto vec = finder->eval(ctx);
		return std::all_of(vec.begin(), vec.end(), [](CommonGameObject* obj) {return ((ServerGameObject*)obj)->orderConfig.orders.empty(); });
	}
	virtual void parse(GSFileParser& gsf, const GameSet& gs) override {
		finder.reset(ReadFinder(gsf, gs));
	}
};

struct ValueFinderResultsCount : ValueDeterminer {
	int ofd;
	std::unique_ptr<ObjectFinder> finder;
	virtual float eval(ScriptContext* ctx) override {
		CommonGameObject* obj = finder->getFirst(ctx);
		if (!obj) return 0.0f;
		auto _ = ctx->changeSelf(obj);
		return (float)Server::instance->gameSet->objectFinderDefinitions[ofd]->eval(ctx).size();
	}
	virtual void parse(GSFileParser& gsf, const GameSet& gs) override {
		ofd = gs.objectFinderDefinitions.readIndex(gsf);
		finder.reset(ReadFinder(gsf, gs));
	}
};

struct ValueHasDirectLineOfSightTo : ValueDeterminer {
	std::unique_ptr<ObjectFinder> finder1;
	std::unique_ptr<PositionDeterminer> pos;
	std::unique_ptr<ObjectFinder> finder2;

	virtual float eval(ScriptContext* ctx) override {
		auto* sourceObj = finder1->getFirst(ctx);
		auto* targetObj = finder2->getFirst(ctx);
		if (!targetObj)
			return 0.0f;

		Vector3 startPos = pos->eval(ctx).position;
		Vector3 endPos = targetObj->position + Vector3(0, 2, 0);
		Vector3 dir = (endPos - startPos).normal();
		
		auto* terrain = ctx->gameState->terrain;
		auto [trnWidth, trnHeight] = terrain->getNumPlayableTiles();

		auto pred = [&](Pathfinding::PFPos pos) {
			if (!(pos.x >= 0 && pos.x < trnWidth && pos.z >= 0 && pos.z < trnHeight))
				return true;

			const auto& tile = ctx->gameState->tiles[pos.z * trnWidth + pos.x];
			const auto* owningBuilding = tile.building.getFrom(ctx->gameState);
			if (owningBuilding && owningBuilding != sourceObj && owningBuilding != targetObj && !tile.buildingPassable)
				return true;

			const float h1 = terrain->getVertex(pos.x + terrain->edge, pos.z + terrain->edge);
			const float h2 = terrain->getVertex(pos.x + terrain->edge + 1, pos.z + terrain->edge);
			const float h3 = terrain->getVertex(pos.x + terrain->edge + 1, pos.z + terrain->edge + 1);
			const float h4 = terrain->getVertex(pos.x + terrain->edge, pos.z + terrain->edge + 1);
			const float tileHeight = std::min({ h1,h2,h3,h4 });
			Vector3 testPos = startPos + dir * dir.dot(Vector3(pos.x * 5.0f + 2.5f, tileHeight, pos.z * 5.0f + 2.5f) - startPos);
			if (testPos.y < tileHeight)
				return true;

			return false;
			};

		auto intersectingTile = Pathfinding::SegmentTraversal(startPos.x / 5.0f, startPos.z / 5.0f, endPos.x / 5.0f, endPos.z / 5.0f, pred);
		return !intersectingTile.has_value();
	}
	virtual void parse(GSFileParser& gsf, const GameSet& gs) override {
		finder1.reset(ReadFinder(gsf, gs));
		pos.reset(PositionDeterminer::createFrom(gsf, gs));
		finder2.reset(ReadFinder(gsf, gs));
	}
};

struct ValueWaterBeneath : ValueDeterminer {
	std::unique_ptr<ObjectFinder> finder;
	virtual float eval(ScriptContext* ctx) override {
		CommonGameObject* obj = finder->getFirst(ctx);
		int tx = (int)(obj->position.x / 5.0f), tz = (int)(obj->position.z / 5.0f);
		if (auto* tile = ctx->gameState->terrain->getPlayableTile(tx, tz)) {
			if (tile->fullOfWater)
				return 1.0f;
		}
		return 0.0f;
	}
	virtual void parse(GSFileParser& gsf, const GameSet& gs) override {
		finder.reset(ReadFinder(gsf, gs));
	}
};

// ANGLE_BETWEEN <HORIZONTAL|VERTICAL|3D> <a> <b>: the angle from a's facing to the direction
// from a to b, in half turns (-1..1), as WKB.exe's VD_Angle_Between (0x453dc0). HORIZONTAL: in
// the ground plane, signed by the side b is on; VERTICAL: a's facing's elevation less the
// direction's (each as sign(y) acos(1 - |y|)), so it is above 0 when a is higher than b (the game
// set's height bonuses); 3D: the angle between the two in space. 0 when a and b are at one place.
struct ValueAngleBetween : ValueDeterminer {
	int mode = 0; // 0: HORIZONTAL, 1: VERTICAL, 2: 3D (the original's order)
	std::unique_ptr<ObjectFinder> a, b;
	virtual float eval(ScriptContext* ctx) override {
		CommonGameObject* oa = a->getFirst(ctx);
		CommonGameObject* ob = b->getFirst(ctx);
		if (!oa || !ob)
			return 0.0f;
		Vector3 diff = ob->position - oa->position;
		if (diff.x == 0.0f && diff.y == 0.0f && diff.z == 0.0f)
			return 0.0f;
		// a's facing: the yaw as setOrientation takes it (atan2(x, -z)), the pitch from orientation.x
		const float yaw = oa->orientation.y, pitch = oa->orientation.x;
		Vector3 facing(std::sin(yaw) * std::cos(pitch), -std::sin(pitch), -std::cos(yaw) * std::cos(pitch));
		if (mode == 1) {
			const Vector3 dir = diff.normal();
			auto elevation = [](float y) { return y > 0.0f ? std::acos(1.0f - y) : -std::acos(std::min(1.0f, 1.0f + y)); };
			return (elevation(facing.y) - elevation(dir.y)) / 3.14159265f;
		}
		if (mode == 0) {
			facing.y = 0.0f;
			diff.y = 0.0f;
			if (diff.x == 0.0f && diff.z == 0.0f)
				return 0.0f;
			facing = facing.normal();
		}
		const Vector3 dir = diff.normal();
		const float dot = std::clamp(facing.x * dir.x + facing.y * dir.y + facing.z * dir.z, -1.0f, 1.0f);
		const float angle = std::acos(dot);
		// the side: the up part of facing x dir
		return ((facing.z * dir.x - facing.x * dir.z) < 0.0f ? -angle : angle) / 3.14159265f;
	}
	virtual void parse(GSFileParser& gsf, const GameSet& gs) override {
		const std::string tag = gsf.nextString();
		mode = (tag == "VERTICAL") ? 1 : (tag == "3D") ? 2 : 0;
		a.reset(ReadFinder(gsf, gs));
		b.reset(ReadFinder(gsf, gs));
	}
};

struct ValueIsMusicPlaying : ValueDeterminer {
	std::unique_ptr<ObjectFinder> finder;
	virtual float eval(ScriptContext* ctx) override {
		if (!ctx->isServer()) return fail(ctx);
		CommonGameObject* obj = finder->getFirst(ctx);
		if (!obj) return 0.0f;
		ServerGameObject* player = (ServerGameObject*)obj->getPlayer();
		return player->isMusicPlaying ? 1.0f : 0.0f;
	}
	virtual void parse(GSFileParser& gsf, const GameSet& gs) override {
		finder.reset(ReadFinder(gsf, gs));
	}
};

struct ValueCurrentlyDoingOrder : ValueDeterminer {
	int category;
	std::unique_ptr<ObjectFinder> finder;
	virtual float eval(ScriptContext* ctx) override {
		if (ctx->isServer()) {
			SrvScriptContext* sctx = (SrvScriptContext*)ctx;
			auto vec = finder->eval(sctx);
			if (vec.empty()) return 0.0f;
			return std::all_of(vec.begin(), vec.end(), [this](ServerGameObject* obj) {
				auto* order = obj->orderConfig.getCurrentOrder();
				return order && (order->blueprint->category == category);
				});
		}
		else if (ctx->isClient()) {
			CliScriptContext* cctx = (CliScriptContext*)ctx;
			auto vec = finder->eval(cctx);
			if (vec.empty()) return 0.0f;
			return std::all_of(vec.begin(), vec.end(), [this,ctx](ClientGameObject* obj) {
				return obj->reportedCurrentOrder != -1 && ctx->gameState->gameSet->orders[obj->reportedCurrentOrder].category == category;
			});
		}
		return 0.0f;
	}
	virtual void parse(GSFileParser& gsf, const GameSet& gs) override {
		category = gs.orderCategories.readIndex(gsf);
		finder.reset(ReadFinder(gsf, gs));
	}
};

struct ValueCurrentlyDoingTask : ValueDeterminer {
	int category;
	std::unique_ptr<ObjectFinder> finder;
	virtual float eval(ScriptContext* ctx) override {
		if (!ctx->isServer()) return fail(ctx);
		SrvScriptContext* sctx = (SrvScriptContext*)ctx;
		auto vec = finder->eval(sctx);
		if (vec.empty()) return 0.0f;
		return std::all_of(vec.begin(), vec.end(), [this](ServerGameObject* obj) {
			auto order = obj->orderConfig.getCurrentOrder();
			return order && (order->getCurrentTask()->blueprint->category == category);
			});
	}
	virtual void parse(GSFileParser& gsf, const GameSet& gs) override {
		category = gs.taskCategories.readIndex(gsf);
		finder.reset(ReadFinder(gsf, gs));
	}
};

struct ValueTileItem : ValueDeterminer {
	int item;
	std::unique_ptr<ObjectFinder> finder;
	virtual float eval(ScriptContext* ctx) override {
		if (auto* obj = finder->getFirst(ctx)) {
			int tx = (int)(obj->position.x / 5.0f), tz = (int)(obj->position.z / 5.0f);
			if (auto* tile = ctx->gameState->terrain->getPlayableTile(tx, tz)) {
				auto& attg = ctx->gameState->gameSet->associatedTileTexGroups;
				auto it = attg.find(tile->texture->grp->name);
				if (it != attg.end()) {
					auto& vals = it->second->itemValues;
					auto vt = vals.find(item);
					if (vt != vals.end())
						return vt->second;
				}
			}
		}
		return 0.0f;
	}
	virtual void parse(GSFileParser& gsf, const GameSet& gs) override {
		item = gs.items.readIndex(gsf);
		finder.reset(ReadFinder(gsf, gs));
	}
};

struct ValueNumAssociates : ValueDeterminer {
	int category;
	std::unique_ptr<ObjectFinder> finder;
	virtual float eval(ScriptContext* ctx) override {
		if (!ctx->isServer()) return fail(ctx);
		ServerGameObject* obj = (ServerGameObject*)finder->getFirst(ctx);
		if (!obj) return 0.0f;
		auto it = obj->associates.find(category);
		if (it == obj->associates.end())
			return 0.0f;
		return (float)it->second.size();
	}
	virtual void parse(GSFileParser& gsf, const GameSet& gs) override {
		category = gs.associations.readIndex(gsf);
		finder.reset(ReadFinder(gsf, gs));
	}
};

struct ValueNumAssociators : ValueDeterminer {
	int category;
	std::unique_ptr<ObjectFinder> finder;
	virtual float eval(ScriptContext* ctx) override {
		if (!ctx->isServer()) return fail(ctx);
		ServerGameObject* obj = (ServerGameObject*)finder->getFirst(ctx);
		if (!obj) return 0.0f;
		auto it = obj->associators.find(category);
		if (it == obj->associators.end())
			return 0.0f;
		return (float)it->second.size();
	}
	virtual void parse(GSFileParser& gsf, const GameSet& gs) override {
		category = gs.associations.readIndex(gsf);
		finder.reset(ReadFinder(gsf, gs));
	}
};

struct ValueBuildingType : ValueDeterminer {
	int type;
	std::unique_ptr<ObjectFinder> finder;
	virtual float eval(ScriptContext* ctx) override {
		if (auto* obj = finder->getFirst(ctx)) {
			return (obj->blueprint->buildingType == type) ? 1.0f : 0.0f;
		}
		return 0.0f;
	}
	virtual void parse(GSFileParser& gsf, const GameSet& gs) override {
		type = Tags::BUILDINGTYPE_tagDict.getTagID(gsf.nextString().c_str());
		finder.reset(ReadFinder(gsf, gs));
	}
};

struct ValueNumReferencers : ValueDeterminer {
	int taskCategory;
	std::unique_ptr<ObjectFinder> finder;
	virtual float eval(ScriptContext* ctx) override {
		if (!ctx->isServer()) return fail(ctx);
		ServerGameObject* obj = (ServerGameObject*)finder->getFirst(ctx);
		if (!obj) return 0.0f;
		int count = 0;
		// FIXME risk of duplicates!!!
		for (CommonGameObject* ref : obj->referencers) {
			if (ref) {
				Order* order = ((ServerGameObject*)ref)->orderConfig.getCurrentOrder();
				if (!order) continue;
				Task* task = order->getCurrentTask();
				if (!task) continue;
				if (task->blueprint->category == taskCategory)
					count++;
			}
		}
		return (float)count;
	}
	virtual void parse(GSFileParser& gsf, const GameSet& gs) override {
		taskCategory = gs.taskCategories.readIndex(gsf);
		finder.reset(ReadFinder(gsf, gs));
	}
};

struct ValueIndexedItemValue : ValueDeterminer {
	int item;
	std::unique_ptr<ValueDeterminer> index;
	std::unique_ptr<ObjectFinder> finder;
	virtual float eval(ScriptContext* ctx) override {
		if (CommonGameObject* obj = finder->getFirst(ctx))
			return obj->getIndexedItem(item, (int)index->eval(ctx));
		return 0.0f;
	}
	virtual void parse(GSFileParser& gsf, const GameSet& gs) override {
		item = gs.items.readIndex(gsf);
		index.reset(ReadValueDeterminer(gsf, gs));
		finder.reset(ReadFinder(gsf, gs));
	}
};

struct ValueWithinForwardArc : ValueDeterminer {
	std::unique_ptr<ObjectFinder> fCenter;
	std::unique_ptr<ObjectFinder> fTarget;
	std::unique_ptr<ValueDeterminer> vArcAngle;
	std::unique_ptr<ValueDeterminer> vArcRadius;
	virtual float eval(ScriptContext* ctx) override {
		auto* oCenter = fCenter->getFirst(ctx);
		auto* oTarget = fTarget->getFirst(ctx);
		float arcAngle = vArcAngle->eval(ctx);
		float arcRadius = vArcRadius->eval(ctx);
		Vector3 centerDir{ sinf(oCenter->orientation.y), 0.0f, -cosf(oCenter->orientation.y) };
		Vector3 centerToTarget = oTarget->position - oCenter->position;
		float dist = centerToTarget.len2xz();
		if (dist > arcRadius)
			return 0.0f;
		if (dist <= 0.0f)
			return 1.0f; // :thinking:
		float alpha = std::acos(centerDir.dot(centerToTarget/dist));
		return std::abs(alpha) < arcAngle;
	}
	virtual void parse(GSFileParser& gsf, const GameSet& gs) override {
		fCenter.reset(ReadFinder(gsf, gs));
		fTarget.reset(ReadFinder(gsf, gs));
		vArcAngle.reset(ReadValueDeterminer(gsf, gs));
		vArcRadius.reset(ReadValueDeterminer(gsf, gs));
	}
};

struct ValueMapWidth : ValueDeterminer {
	virtual float eval(ScriptContext* ctx) override {
		return ctx->gameState->terrain->getPlayableArea().first;
	}
	virtual void parse(GSFileParser& gsf, const GameSet& gs) override {}
};

struct ValueMapDepth : ValueDeterminer {
	virtual float eval(ScriptContext* ctx) override {
		return ctx->gameState->terrain->getPlayableArea().second;
	}
	virtual void parse(GSFileParser& gsf, const GameSet& gs) override {}
};

struct ValueIsDisabled : ValueDeterminer {
	std::unique_ptr<ObjectFinder> finder;
	virtual float eval(ScriptContext* ctx) override {
		auto* obj = finder->getFirst(ctx);
		if (!obj) return 0.0f;
		return (obj->disableCount > 0) ? 1.0f : 0.0f;
	}
	virtual void parse(GSFileParser& gsf, const GameSet& gs) override {
		finder.reset(ReadFinder(gsf, gs));
	}
};

struct ValueIsDiscovered : ValueDeterminer {
	std::unique_ptr<ObjectFinder> fObjects, fPlayer;
	virtual float eval(ScriptContext* ctx) override {
		// TODO
		return 1.0f;
	}
	virtual void parse(GSFileParser& gsf, const GameSet& gs) override {
		fObjects.reset(ReadFinder(gsf, gs));
		fPlayer.reset(ReadFinder(gsf, gs));
	}
};
struct ValueIsVisible : ValueDeterminer {
	std::unique_ptr<ObjectFinder> fObjects, fPlayer;
	virtual float eval(ScriptContext* ctx) override {
		// TODO
		return 1.0f;
	}
	virtual void parse(GSFileParser& gsf, const GameSet& gs) override {
		fObjects.reset(ReadFinder(gsf, gs));
		fPlayer.reset(ReadFinder(gsf, gs));
	}
};
// COULD_REACH <character type> <from> <to>: whether that type could walk from one place to near
// the other (within 4 tiles, as the place is often a building's): a flood fill over the tiles it
// can stand on (MovementController::isTileBlocked). Up to 40000 tiles; beyond, reachable. The AI
// asks it before sending a battering ram at a palace behind walls.
struct ValueCouldReach : ValueDeterminer {
	const GameObjBlueprint* objtype;
	std::unique_ptr<PositionDeterminer> pStart, pEnd;
	virtual float eval(ScriptContext* ctx) override {
		if (!ctx->isServer() || !objtype)
			return 1.0f;
		// from the nearest tiles the type can stand on (the places are often a building's or a marker's)
		const Vector3 from = MovementController::nearestValidPosition(objtype, pStart->eval(ctx).position, 16);
		const Vector3 to = MovementController::nearestValidPosition(objtype, pEnd->eval(ctx).position, 16);
		const int sx = (int)(from.x / 5.0f), sz = (int)(from.z / 5.0f);
		const int ex = (int)(to.x / 5.0f), ez = (int)(to.z / 5.0f);
		constexpr int NEAR = 4, LIMIT = 40000;
		std::unordered_set<int64_t> seen;
		std::vector<std::pair<int, int>> open = { { sx, sz } };
		seen.insert(((int64_t)sx << 32) | (uint32_t)sz);
		while (!open.empty()) {
			const auto [x, z] = open.back();
			open.pop_back();
			if (std::abs(x - ex) <= NEAR && std::abs(z - ez) <= NEAR)
				return 1.0f;
			if ((int)seen.size() > LIMIT)
				return 1.0f;
			for (int dz = -1; dz <= 1; dz++)
				for (int dx = -1; dx <= 1; dx++) {
					const int nx = x + dx, nz = z + dz;
					if ((dx == 0 && dz == 0) || !seen.insert(((int64_t)nx << 32) | (uint32_t)nz).second)
						continue;
					if (!MovementController::isTileBlocked(objtype, nx, nz))
						open.push_back({ nx, nz });
				}
		}
		return 0.0f;
	}
	virtual void parse(GSFileParser& gsf, const GameSet& gs) override {
		objtype = gs.objBlueprints[Tags::GAMEOBJCLASS_CHARACTER].readPtr(gsf);
		pStart.reset(PositionDeterminer::createFrom(gsf, gs));
		pEnd.reset(PositionDeterminer::createFrom(gsf, gs));
	}
};

// AI_CONTROLLED <finder>: 1 when every object found belongs to a player run by an AI (WKB.exe's
// VD_AI_Controlled, server 0x4537c0: the player has an AI controller); on a client 0. The game set
// uses it to cancel an AI's spawning at the population limit, which would else block the building.
struct ValueAiControlled : ValueDeterminer {
	std::unique_ptr<ObjectFinder> fPlayer;
	virtual float eval(ScriptContext* ctx) override {
		if (!ctx->isServer())
			return 0.0f;
		const auto objects = fPlayer->eval(ctx);
		if (objects.empty())
			return 0.0f;
		for (CommonGameObject* obj : objects) {
			const AIController& ai = ((ServerGameObject*)obj)->getPlayer()->aiController;
			if (!(ai.planBlueprint || !ai.commissionInstances.empty() || !ai.workOrderInstances.empty()))
				return 0.0f;
		}
		return 1.0f;
	}
	virtual void parse(GSFileParser& gsf, const GameSet& gs) override {
		fPlayer.reset(ReadFinder(gsf, gs));
	}
};

struct ValueGradientInFront : ValueDeterminer {
	std::unique_ptr<ObjectFinder> fObject;
	virtual float eval(ScriptContext* ctx) override {
		// TODO
		return 0.0f;
	}
	virtual void parse(GSFileParser& gsf, const GameSet& gs) override {
		fObject.reset(ReadFinder(gsf, gs));
	}
};

struct ValueAverageEquationResult : ValueDeterminer {
	int equation;
	std::unique_ptr<ObjectFinder> finder;
	virtual float eval(ScriptContext* ctx) override {
		auto vec = finder->eval(ctx);
		if (vec.empty())
			return 0.0f;
		float sum = 0.0f;
		for (auto* obj : vec) {
			auto _ = ctx->changeSelf(obj);
			sum += ctx->gameState->gameSet->equations[equation]->eval(ctx);
		}
		return sum / (float)vec.size();
	}
	virtual void parse(GSFileParser& gsf, const GameSet& gs) override {
		equation = gs.equations.readIndex(gsf);
		finder.reset(ReadFinder(gsf, gs));
	}
};

// CAN_AFFORD_COMMISSION <commission> <handler finder> [FORCE_BALANCE] [BEST_QUALITY], as WKB.exe's
// VD_Can_Afford_Commission (server 0x457730, the check 0x4cb440): the handler's player's resources,
// less what each requirement still misses times its cost, must not go below zero. A character
// requirement takes the first entry of its ladder (best first) that has a building to spawn it
// and that it can pay for; with BEST_QUALITY only the first such entry. On a client: 0.
// Whether one of these buildings may spawn the type (WKB.exe 0x4c9af0: a building for it, and the
// type allowed): it has the "Spawn <type>" order, and the command of that name, if any, has no
// impossible condition (tech level, alignment; the wait conditions, such as the cost now, don't
// count). Counting locked rungs made every BEST_QUALITY army unaffordable: the AI never attacked.
bool AISpawnAllowed(const GameObjBlueprint* type, const SrvFinderResult& buildings)
{
	const GameSet* gs = Server::instance->gameSet.get();
	const std::string spawnName = "Spawn " + type->name;
	if (gs->orders.names.getIndex(spawnName) < 0)
		return false;
	const int cmdid = gs->commands.names.getIndex(spawnName);
	const Command* command = cmdid >= 0 ? &gs->commands[cmdid] : nullptr;
	for (ServerGameObject* building : buildings) {
		if (building->blueprint->bpClass != Tags::GAMEOBJCLASS_BUILDING)
			continue;
		if (!command)
			return true;
		SrvScriptContext ctx(Server::instance, building);
		auto _ = ctx.selectedObject.change(building);
		if (std::all_of(command->conditionsImpossible.begin(), command->conditionsImpossible.end(),
			[&](GSCondition* cond) { return cond->test->booleval(&ctx); }))
			return true;
	}
	return false;
}

// FORCE_BALANCE, my reading of the flag (not traced in WKB.exe): the Kingmaker's armies are
// combined arms, and cavalry needs Stables that an AI may not have built yet. Without the flag's
// balancing the cavalry requirements could never be met, so no army was ever affordable or
// complete and the AI never attacked; with it, their share goes to the infantry and archers.
std::vector<int> AICharacterNeeds(const GSCommission* commission, SrvScriptContext* hctx, bool forceBalance)
{
	const size_t n = commission->characterRequirements.size();
	std::vector<int> needed(n), having(n);
	std::vector<bool> spawnable(n, true);
	for (size_t i = 0; i < n; i++) {
		const auto& req = commission->characterRequirements[i];
		needed[i] = (int)std::ceil(req.vdCountNeeded->eval(hctx));
		having[i] = (int)req.fdAlreadyHaving->eval(hctx).size();
	}
	if (!forceBalance)
		return needed;
	int moved = 0;
	std::vector<size_t> takers, givers, fighters;
	for (size_t i = 0; i < n; i++) {
		const auto& entries = commission->characterRequirements[i].ladder->entries;
		spawnable[i] = std::any_of(entries.begin(), entries.end(), [&](const auto& e) { return AISpawnAllowed(e.type, e.finder->eval(hctx)); });
		// the supply wagons' requirement takes no fighters' share
		const bool wagons = std::any_of(entries.begin(), entries.end(), [](const auto& e) { return e.type->name.find("Wagon") != std::string::npos; });
		if (spawnable[i] && !wagons)
			fighters.push_back(i);
		if (needed[i] > having[i])
			(spawnable[i] ? takers : givers).push_back(i);
	}
	// only cavalry short: the infantry and archers that can be spawned take its share
	if (takers.empty())
		takers = fighters;
	// nothing that can be spawned to give it to (no barracks yet): unchanged, so the army stays
	// unaffordable rather than becoming an empty one that is "complete" at once
	if (takers.empty())
		return needed;
	for (size_t i : givers) {
		moved += needed[i] - having[i];
		needed[i] = having[i];
	}
	for (size_t k = 0; moved > 0; k++, moved--)
		needed[takers[k % takers.size()]]++;

	// The aggressive AI: a requirement whose best type the player can't pay for now (short of
	// wood or gold) gives its share to the fighters that don't cost what is short, such as the
	// Spearman (60 food). The user: "there are enough troops types that do not require gold".
	// The armies had waited for archers' wood while hundreds of food stood unused.
	if (Server::instance && Server::instance->aggressiveAI) {
		static const int costItems[3] = { Tags::PDITEM_FOOD_COST, Tags::PDITEM_WOOD_COST, Tags::PDITEM_GOLD_COST };
		static const int stockItems[3] = { Tags::PDITEM_FOOD, Tags::PDITEM_WOOD, Tags::PDITEM_GOLD };
		ServerGameObject* player = ((ServerGameObject*)hctx->getSelf())->getPlayer();
		auto bestType = [&](size_t i) -> const GameObjBlueprint* {
			for (const auto& e : commission->characterRequirements[i].ladder->entries)
				if (AISpawnAllowed(e.type, e.finder->eval(hctx)))
					return e.type;
			return nullptr;
		};
		// paid in the requirements' order from what the player has, as CAN_AFFORD_COMMISSION does
		bool shortOf[3] = {};
		float left[3];
		for (int r = 0; r < 3; r++)
			left[r] = player->getItem(stockItems[r]);
		std::vector<size_t> unpaid;
		for (size_t i = 0; i < n; i++) {
			const GameObjBlueprint* type = spawnable[i] && needed[i] > having[i] ? bestType(i) : nullptr;
			if (!type)
				continue;
			bool fits = true;
			for (int r = 0; r < 3; r++)
				if ((needed[i] - having[i]) * type->getStartingItemValue(costItems[r]) > left[r]) {
					shortOf[r] = true;
					fits = false;
				}
			if (fits)
				for (int r = 0; r < 3; r++)
					left[r] -= (needed[i] - having[i]) * type->getStartingItemValue(costItems[r]);
			else
				unpaid.push_back(i);
		}
		std::vector<size_t> payers;
		for (size_t i : fighters) {
			const GameObjBlueprint* type = bestType(i);
			bool usesShort = false;
			for (int r = 0; r < 3; r++)
				usesShort |= shortOf[r] && type->getStartingItemValue(costItems[r]) > 0.0f;
			if (!usesShort)
				payers.push_back(i);
		}
		if (!unpaid.empty() && !payers.empty()) {
			int extra = 0;
			for (size_t i : unpaid)
				if (std::find(payers.begin(), payers.end(), i) == payers.end()) {
					extra += needed[i] - having[i];
					needed[i] = having[i];
				}
			for (size_t k = 0; extra > 0; k++, extra--)
				needed[payers[k % payers.size()]]++;
		}
	}
	return needed;
}

// The aggressive AI, on unless the skirmish setting turns it off (the user: the AI should be
// "vastly more aggressive"; not in WKB.exe). With the original Kingmaker scripts an AI wants
// 0.4 x its population limit of peasants before it builds anything else of note, 400 at the
// user's 1000, and takes ten minutes or more to raise its first army; then it launches one only
// on a roll against its Impatience (15-100) each minute. The aggressive AI wants at most the
// peasants of the original's default limit of 200 (80) and always launches an army it can
// afford. Both wrap the scripts' equations, so the scripts themselves are unchanged.
struct ValueAggressionOverride : ValueDeterminer {
	ValueDeterminer* original;
	float limit;
	bool asMinimum; // the result is at least limit (else at most)
	ValueAggressionOverride(ValueDeterminer* original, float limit, bool asMinimum) : original(original), limit(limit), asMinimum(asMinimum) {}
	~ValueAggressionOverride() { delete original; }
	virtual float eval(ScriptContext* ctx) override {
		const float value = original->eval(ctx);
		if (!ctx->isServer() || !Server::instance || !Server::instance->aggressiveAI)
			return value;
		return asMinimum ? std::max(value, limit) : std::min(value, limit);
	}
	virtual void parse(GSFileParser& gsf, const GameSet& gs) override {}
};

void InstallAIAggressionOverrides(GameSet& gs)
{
	const std::pair<const char*, std::pair<float, bool>> overrides[] = {
		{ "Total Peasants Needed", { 80.0f, false } },
		{ "I Have Enough Resources For Maximum Attack Of My Next Army Type Or I Have Become Impatient", { 1.0f, true } },
	};
	for (const auto& [name, how] : overrides) {
		const int eq = gs.equations.names.getIndex(name);
		if (eq >= 0)
			gs.equations[eq] = new ValueAggressionOverride(gs.equations[eq], how.first, how.second);
	}
}

struct ValueCanAffordCommission : ValueDeterminer {
	const GSCommission* commission = nullptr;
	std::unique_ptr<ObjectFinder> fHandler;
	bool bestQuality = false, forceBalance = false;
	virtual float eval(ScriptContext* ctx) override {
		if (!ctx->isServer() || !commission)
			return 0.0f;
		ServerGameObject* handler = (ServerGameObject*)fHandler->getFirst(ctx);
		if (!handler)
			return 0.0f;
		ServerGameObject* player = handler->getPlayer();
		static const std::pair<int, int> costItems[3] = { // as in server.cpp
			{Tags::PDITEM_FOOD, Tags::PDITEM_FOOD_COST},
			{Tags::PDITEM_WOOD, Tags::PDITEM_WOOD_COST},
			{Tags::PDITEM_GOLD, Tags::PDITEM_GOLD_COST},
		};
		float left[3];
		for (int r = 0; r < 3; r++)
			left[r] = player->getItem(costItems[r].first);
		// takes count times the type's cost from these resources, if they have it
		auto pay = [&](float* from, const GameObjBlueprint* type, int count) {
			float after[3];
			for (int r = 0; r < 3; r++) {
				after[r] = from[r] - count * std::max(0.0f, type->getStartingItemValue(costItems[r].second));
				if (after[r] < 0.0f)
					return false;
			}
			std::copy(std::begin(after), std::end(after), from);
			return true;
		};
		// the foundations this commission has started already, when it is active
		const CommissionInstance* active = nullptr;
		for (const CommissionInstance& ci : player->aiController.commissionInstances)
			if (ci.blueprint == commission && ci.handlerObject.get() == handler)
				active = &ci;
		SrvScriptContext hctx(Server::instance, handler);
		const std::vector<int> needs = AICharacterNeeds(commission, &hctx, forceBalance);
		for (size_t ri = 0; ri < commission->characterRequirements.size(); ri++) {
			const auto& req = commission->characterRequirements[ri];
			const int missing = needs[ri] - (int)req.fdAlreadyHaving->eval(&hctx).size();
			if (missing <= 0)
				continue;
			bool paid = false;
			for (const auto& entry : req.ladder->entries) {
				if (!AISpawnAllowed(entry.type, entry.finder->eval(&hctx)))
					continue;
				if (pay(left, entry.type, missing)) {
					paid = true;
					break;
				}
				if (bestQuality)
					break;
			}
			if (!paid)
				return 0.0f;
		}
		for (size_t i = 0; i < commission->buildingRequirements.size(); i++) {
			const auto& req = commission->buildingRequirements[i];
			int missing = (int)req.vdCountNeeded->eval(&hctx) - (int)req.fdAlreadyHaving->eval(&hctx).size();
			if (active && i < active->buildingReqInsts.size())
				for (const SrvGORef& foundation : active->buildingReqInsts[i].foundations)
					if (foundation)
						missing--;
			if (missing > 0 && !pay(left, req.bpBuilding, missing))
				return 0.0f;
		}
		return 1.0f;
	}
	virtual void parse(GSFileParser& gsf, const GameSet& gs) override {
		commission = gs.commissions.readPtr(gsf);
		fHandler.reset(ReadFinder(gsf, gs));
		while (!gsf.eol) {
			const std::string flag = gsf.nextString();
			if (flag == "BEST_QUALITY")
				bestQuality = true;
			else if (flag == "FORCE_BALANCE")
				forceBalance = true;
			else if (flag.empty())
				break;
		}
	}
};

struct ValueAverageItemValue : ValueDeterminer {
	int item;
	std::unique_ptr<ObjectFinder> finder;
	virtual float eval(ScriptContext* ctx) override {
		auto objList = finder->eval(ctx);
		if (objList.empty()) {
			return 0.0f;
		}
		float sum = 0.0f;
		for (CommonGameObject* obj : objList) {
			sum += obj->getItem(item);
		}
		return sum / (float)objList.size();
	}
	virtual void parse(GSFileParser& gsf, const GameSet& gs) override {
		item = gs.items.readIndex(gsf);
		finder.reset(ReadFinder(gsf, gs));
	}
};

// The way an object faces: orientation.y as movement sets it, atan2(dir.x, -dir.z)
static Vector3 FacingDirection(const CommonGameObject* obj)
{
	const float angle = obj->orientation.y;
	return { std::sin(angle), 0.0f, -std::cos(angle) };
}

// IS_IN_FRONT_OF / IS_BEHIND <subject> <object>: the subject is in front of / behind the
// object, as the object faces ("I'm Attacking HI Formed up From the Front":
// IS_IN_FRONT_OF FINDER_SELF FINDER_TARGET)
struct ValueIsInFrontOf : ValueDeterminer {
	std::unique_ptr<ObjectFinder> subjectFinder, objectFinder;
	bool behind = false;
	ValueIsInFrontOf(bool behind = false) : behind(behind) {}
	virtual float eval(ScriptContext* ctx) override {
		// TODO Handle multiple objects from the finder
		const auto* subject = subjectFinder->getFirst(ctx);
		const auto* object = objectFinder->getFirst(ctx);
		if (!subject || !object)
			return 0.0f;
		const float d = FacingDirection(object).dot(subject->position - object->position);
		return (behind ? d < 0.0f : d > 0.0f) ? 1.0f : 0.0f;
	}
	virtual void parse(GSFileParser& gsf, const GameSet& gs) override {
		subjectFinder.reset(ReadFinder(gsf, gs));
		objectFinder.reset(ReadFinder(gsf, gs));
	}
};

// NUM_ORDERS <finder>: the orders in the first object's queue
struct ValueNumOrders : ValueDeterminer {
	std::unique_ptr<ObjectFinder> finder;
	virtual float eval(ScriptContext* ctx) override {
		if (!ctx->isServer()) return fail(ctx);
		auto* obj = finder->getFirst(ctx);
		return obj ? (float)((ServerGameObject*)obj)->orderConfig.orders.size() : 0.0f;
	}
	virtual void parse(GSFileParser& gsf, const GameSet& gs) override {
		finder.reset(ReadFinder(gsf, gs));
	}
};

// DOING_SAME_ORDER <a> <b>: both have a current order, of the same type (double-click selects
// the units doing what the clicked one does)
struct ValueDoingSameOrder : ValueDeterminer {
	std::unique_ptr<ObjectFinder> fa, fb;
	virtual float eval(ScriptContext* ctx) override {
		if (!ctx->isServer()) return fail(ctx);
		auto* a = (ServerGameObject*)fa->getFirst(ctx);
		auto* b = (ServerGameObject*)fb->getFirst(ctx);
		if (!a || !b)
			return 0.0f;
		const Order* oa = a->orderConfig.getCurrentOrder();
		const Order* ob = b->orderConfig.getCurrentOrder();
		return (oa && ob && oa->blueprint == ob->blueprint) ? 1.0f : 0.0f;
	}
	virtual void parse(GSFileParser& gsf, const GameSet& gs) override {
		fa.reset(ReadFinder(gsf, gs));
		fb.reset(ReadFinder(gsf, gs));
	}
};

// CAN_TRAVERSE_TILE <traveller> <place>: the traveller could stand on the tile of the place
// (passengers: the vessel is not at sea). CAN_TRAVERSE_NEIGHBOURING_TILE <place> <travellers>:
// one of the 8 tiles around the place can take all the travellers (they can disembark).
struct ValueCanTraverseTile : ValueDeterminer {
	std::unique_ptr<ObjectFinder> f1, f2;
	bool neighbouring;
	ValueCanTraverseTile(bool neighbouring) : neighbouring(neighbouring) {}
	virtual float eval(ScriptContext* ctx) override {
		if (!ctx->isServer()) return fail(ctx);
		if (!neighbouring) {
			auto* traveller = f1->getFirst(ctx);
			auto* place = f2->getFirst(ctx);
			if (!traveller || !place)
				return 0.0f;
			return MovementController::isTileBlocked(traveller->blueprint, (int)(place->position.x / 5.0f), (int)(place->position.z / 5.0f)) ? 0.0f : 1.0f;
		}
		auto* place = f1->getFirst(ctx);
		auto travellers = f2->eval(ctx);
		if (!place || travellers.empty())
			return 0.0f;
		const int px = (int)(place->position.x / 5.0f), pz = (int)(place->position.z / 5.0f);
		for (int dz = -1; dz <= 1; dz++)
			for (int dx = -1; dx <= 1; dx++) {
				if (dx == 0 && dz == 0)
					continue;
				bool all = true;
				for (CommonGameObject* t : travellers)
					if (MovementController::isTileBlocked(t->blueprint, px + dx, pz + dz)) { all = false; break; }
				if (all)
					return 1.0f;
			}
		return 0.0f;
	}
	virtual void parse(GSFileParser& gsf, const GameSet& gs) override {
		f1.reset(ReadFinder(gsf, gs));
		f2.reset(ReadFinder(gsf, gs));
	}
};

// RANDOM_FRACTION: a random number in [0, 1)
struct ValueRandomFraction : ValueDeterminer {
	virtual float eval(ScriptContext* ctx) override { return (float)ScriptRand(ctx) / ((float)SIM_RAND_MAX + 1.0f); }
	virtual void parse(GSFileParser& gsf, const GameSet& gs) override {}
};

ValueDeterminer *ReadValueDeterminer(::GSFileParser &gsf, const ::GameSet &gs)
{
	ValueDeterminer *vd;
	auto strtag = gsf.nextTag();
	switch (Tags::VALUE_tagDict.getTagID(strtag.c_str())) {
	case Tags::VALUE_CONSTANT: vd = new ValueConstant(gsf.nextFloat()); return vd;
	case Tags::VALUE_DEFINED_VALUE: {
		std::string name = gsf.nextString(true);
		auto it = gs.definedValues.find(name);
		if (it == gs.definedValues.end()) {
			// e.g. defined in a linked file that a mod combination is missing
			printf("WARNING: Defined value \"%s\" not found at %s, using 0\n", name.c_str(), gsf.locate().c_str());
			return new ValueConstant(0.0f);
		}
		return new ValueConstant(it->second);
	}
	case Tags::VALUE_ITEM_VALUE: vd = new ValueItemValue; break;
	case Tags::VALUE_OBJECT_ID: if (gs.version >= gs.GSVERSION_WKBATTLES) vd = new ValueObjectId_Battles; else vd = new ValueObjectId_WKO; break;
	case Tags::VALUE_OBJECT_CLASS: vd = new ValueObjectClass; break;
	case Tags::VALUE_EQUATION_RESULT: vd = new ValueEquationResult; break;
	case Tags::VALUE_IS_SUBSET_OF: vd = new ValueIsSubsetOf; break;
	case Tags::VALUE_NUM_OBJECTS: vd = new ValueNumObjects; break;
	case Tags::VALUE_CAN_REACH: vd = new ValueCanReach; break;
	case Tags::VALUE_IS_ACCESSIBLE: vd = new ValueIsAccessible; break;
	case Tags::VALUE_HAS_APPEARANCE: vd = new ValueHasAppearance; break;
	case Tags::VALUE_SAME_PLAYER: vd = new ValueSamePlayer; break;
	case Tags::VALUE_OBJECT_TYPE: vd = new ValueObjectType; break;
	case Tags::VALUE_DISTANCE_BETWEEN: vd = new ValueDistanceBetween; break;
	case Tags::VALUE_DISTANCE_BETWEEN_INCLUDING_RADIUS: vd = new ValueDistanceBetween; break; // TODO
	case Tags::VALUE_VALUE_TAG_INTERPRETATION: vd = new ValueValueTagInterpretation; break;
	case Tags::VALUE_DIPLOMATIC_STATUS_AT_LEAST: vd = new ValueDiplomaticStatusAtLeast; break;
	case Tags::VALUE_ARE_ASSOCIATED: vd = new ValueAreAssociated; break;
	case Tags::VALUE_BLUEPRINT_ITEM_VALUE: vd = new ValueBlueprintItemValue; break;
	case Tags::VALUE_TOTAL_ITEM_VALUE: vd = new ValueTotalItemValue; break;
	case Tags::VALUE_IS_IDLE: vd = new ValueIsIdle; break;
	case Tags::VALUE_FINDER_RESULTS_COUNT: vd = new ValueFinderResultsCount; break;
	case Tags::VALUE_HAS_DIRECT_LINE_OF_SIGHT_TO: vd = new ValueHasDirectLineOfSightTo; break;
	case Tags::VALUE_WATER_BENEATH: vd = new ValueWaterBeneath; break;
	case Tags::VALUE_ANGLE_BETWEEN: vd = new ValueAngleBetween; break;
	case Tags::VALUE_IS_MUSIC_PLAYING: vd = new ValueIsMusicPlaying; break;
	case Tags::VALUE_CURRENTLY_DOING_ORDER: vd = new ValueCurrentlyDoingOrder; break;
	case Tags::VALUE_CURRENTLY_DOING_TASK: vd = new ValueCurrentlyDoingTask; break;
	case Tags::VALUE_TILE_ITEM: vd = new ValueTileItem; break;
	case Tags::VALUE_NUM_ASSOCIATES: vd = new ValueNumAssociates; break;
	case Tags::VALUE_NUM_ASSOCIATORS: vd = new ValueNumAssociators; break;
	case Tags::VALUE_BUILDING_TYPE: vd = new ValueBuildingType; break;
	case Tags::VALUE_BUILDING_TYPE_OPERAND: vd = new ValueBuildingType; break;
	case Tags::VALUE_NUM_REFERENCERS: vd = new ValueNumReferencers; break;
	case Tags::VALUE_INDEXED_ITEM_VALUE: vd = new ValueIndexedItemValue; break;
	case Tags::VALUE_WITHIN_FORWARD_ARC: vd = new ValueWithinForwardArc; break;
	case Tags::VALUE_MAP_WIDTH: vd = new ValueMapWidth; break;
	case Tags::VALUE_MAP_DEPTH: vd = new ValueMapDepth; break;
	case Tags::VALUE_IS_DISABLED: vd = new ValueIsDisabled; break;
	case Tags::VALUE_IS_DISCOVERED: vd = new ValueIsDiscovered; break;
	case Tags::VALUE_IS_VISIBLE: vd = new ValueIsVisible; break;
	case Tags::VALUE_COULD_REACH: vd = new ValueCouldReach; break;
	case Tags::VALUE_AI_CONTROLLED: vd = new ValueAiControlled; break;
	case Tags::VALUE_GRADIENT_IN_FRONT: vd = new ValueGradientInFront; break;
	case Tags::VALUE_AVERAGE_EQUATION_RESULT: vd = new ValueAverageEquationResult; break;
	case Tags::VALUE_CAN_AFFORD_COMMISSION: vd = new ValueCanAffordCommission; break;
	case Tags::VALUE_AVERAGE_ITEM_VALUE: vd = new ValueAverageItemValue; break;
	case Tags::VALUE_IS_IN_FRONT_OF: vd = new ValueIsInFrontOf; break;
	default:
		// keywords of the original without a tag in wkbre2's list
		if (strtag == "IS_BEHIND") { vd = new ValueIsInFrontOf(true); break; }
		if (strtag == "NUM_ORDERS") { vd = new ValueNumOrders; break; }
		if (strtag == "DOING_SAME_ORDER") { vd = new ValueDoingSameOrder; break; }
		if (strtag == "CAN_TRAVERSE_TILE") { vd = new ValueCanTraverseTile(false); break; }
		if (strtag == "CAN_TRAVERSE_NEIGHBOURING_TILE") { vd = new ValueCanTraverseTile(true); break; }
		if (strtag == "RANDOM_FRACTION") { vd = new ValueRandomFraction; break; }
		if (const char* signature = FindStubSignature(valueStubSignatures, strtag)) {
			vd = new ValueStub(strtag, signature);
			printf("WARNING: Value determiner %s is not implemented (arguments read, evaluates to 0) at %s\n", strtag.c_str(), gsf.locate().c_str());
			break;
		}
		vd = new ValueUnknown(strtag);
		printf("WARNING: Unknown value determiner %s at %s\n", strtag.c_str(), gsf.locate().c_str());
		break;
	}
	vd->parse(gsf, gs);
	return vd;
}

// Unary equation nodes

struct UnaryEnode : ValueDeterminer {
	std::unique_ptr<ValueDeterminer> a;
	virtual void parse(GSFileParser &gsf, const GameSet &gs) override {
		a.reset(ReadEquationNode(gsf, gs));
	}
};

struct EnodeNot : UnaryEnode {
	virtual float eval(ScriptContext* ctx) override { return a->eval(ctx) <= 0.0f; }
};

struct EnodeIsZero : UnaryEnode {
	virtual float eval(ScriptContext* ctx) override { return a->eval(ctx) == 0.0f; }
};

struct EnodeIsPositive : UnaryEnode {
	virtual float eval(ScriptContext* ctx) override { return a->eval(ctx) > 0.0f; }
};

struct EnodeIsNegative : UnaryEnode {
	virtual float eval(ScriptContext* ctx) override { return a->eval(ctx) < 0.0f; }
};

struct EnodeAbsoluteValue : UnaryEnode {
	virtual float eval(ScriptContext* ctx) override { return std::abs(a->eval(ctx)); }
};

struct EnodeCeil : UnaryEnode {
	virtual float eval(ScriptContext* ctx) override { return std::ceil(a->eval(ctx)); }
};

struct EnodeNegate : UnaryEnode {
	virtual float eval(ScriptContext* ctx) override { return -a->eval(ctx); }
};

struct EnodeRandomUpTo : UnaryEnode {
	virtual float eval(ScriptContext* ctx) override { return RandomFromZeroToOne(ctx) * a->eval(ctx); }
};

struct EnodeRound : UnaryEnode {
	virtual float eval(ScriptContext* ctx) override { return std::round(a->eval(ctx)); }
};

struct EnodeTrunc : UnaryEnode {
	virtual float eval(ScriptContext* ctx) override { return std::trunc(a->eval(ctx)); }
};

// Binary equation nodes

struct BinaryEnode : ValueDeterminer {
	std::unique_ptr<ValueDeterminer> a, b;
	virtual void parse(GSFileParser &gsf, const GameSet &gs) override {
		a.reset(ReadEquationNode(gsf, gs));
		b.reset(ReadEquationNode(gsf, gs));
	}
};

struct EnodeAddition : BinaryEnode {
	virtual float eval(ScriptContext* ctx) override { return a->eval(ctx) + b->eval(ctx); }
};

struct EnodeSubtraction : BinaryEnode {
	virtual float eval(ScriptContext* ctx) override { return a->eval(ctx) - b->eval(ctx); }
};

struct EnodeMultiplication : BinaryEnode {
	virtual float eval(ScriptContext* ctx) override { return a->eval(ctx) * b->eval(ctx); }
};

struct EnodeDivision : BinaryEnode {
	virtual float eval(ScriptContext* ctx) override { return a->eval(ctx) / b->eval(ctx); }
};

struct EnodeAnd : BinaryEnode {
	virtual float eval(ScriptContext* ctx) override { return (a->eval(ctx) > 0.0f) && (b->eval(ctx) > 0.0f); }
};

struct EnodeOr : BinaryEnode {
	virtual float eval(ScriptContext* ctx) override { return (a->eval(ctx) > 0.0f) || (b->eval(ctx) > 0.0f); }
};

struct EnodeLessThan : BinaryEnode {
	virtual float eval(ScriptContext* ctx) override { return a->eval(ctx) < b->eval(ctx); }
};

struct EnodeLessThanOrEqualTo : BinaryEnode {
	virtual float eval(ScriptContext* ctx) override { return a->eval(ctx) <= b->eval(ctx); }
};

struct EnodeGreaterThan : BinaryEnode {
	virtual float eval(ScriptContext* ctx) override { return a->eval(ctx) > b->eval(ctx); }
};

struct EnodeGreaterThanOrEqualTo : BinaryEnode {
	virtual float eval(ScriptContext* ctx) override { return a->eval(ctx) >= b->eval(ctx); }
};

struct EnodeEquals : BinaryEnode {
	virtual float eval(ScriptContext* ctx) override { return a->eval(ctx) == b->eval(ctx); }
};

struct EnodeMax : BinaryEnode {
	virtual float eval(ScriptContext* ctx) override { return std::max(a->eval(ctx), b->eval(ctx)); }
};

struct EnodeMin : BinaryEnode {
	virtual float eval(ScriptContext* ctx) override { return std::min(a->eval(ctx), b->eval(ctx)); }
};

struct EnodeRandomInteger : BinaryEnode {
	virtual float eval(ScriptContext* ctx) override {
		int x = (int)a->eval(ctx), y = (int)b->eval(ctx);
		return (float)(ScriptRand(ctx) % (y - x + 1) + x);
	}
};

struct EnodeRandomRange : BinaryEnode {
	virtual float eval(ScriptContext* ctx) override {
		float x = a->eval(ctx), y = b->eval(ctx);
		return RandomFromZeroToOne(ctx) * (y - x) + x;
	}
};

// Ternary equation nodes

struct TernaryEnode : ValueDeterminer {
	std::unique_ptr<ValueDeterminer> a, b, c;
	virtual void parse(GSFileParser &gsf, const GameSet &gs) override {
		a.reset(ReadEquationNode(gsf, gs));
		b.reset(ReadEquationNode(gsf, gs));
		c.reset(ReadEquationNode(gsf, gs));
	}
};

struct EnodeIfThenElse : TernaryEnode {
	virtual float eval(ScriptContext* ctx) override {
		if (a->eval(ctx) > 0.0f)
			return b->eval(ctx);
		else
			return c->eval(ctx);
	}
};

struct EnodeIsBetween : TernaryEnode {
	virtual float eval(ScriptContext* ctx) override {
		float x = a->eval(ctx), y = b->eval(ctx), z = c->eval(ctx);
		return (x > y && x < z) ? 1.0f : 0.0f;
	}
};

// Quaternary equation nodes

struct QuaternaryEnode : ValueDeterminer {
	std::unique_ptr<ValueDeterminer> a, b, c, d;
	virtual void parse(GSFileParser& gsf, const GameSet& gs) override {
		a.reset(ReadEquationNode(gsf, gs));
		b.reset(ReadEquationNode(gsf, gs));
		c.reset(ReadEquationNode(gsf, gs));
		d.reset(ReadEquationNode(gsf, gs));
	}
};

struct EnodeFrontBackLeftRight : QuaternaryEnode {
	std::unique_ptr<ObjectFinder> f_unit, f_target;
	virtual float eval(ScriptContext* ctx) override {
		CommonGameObject* unit = f_unit->getFirst(ctx);
		CommonGameObject* target = f_unit->getFirst(ctx);
		if (!(unit && target))
			return 0.0f;
		Vector3 vec = unit->position - target->position;
		if (std::abs(vec.z) > std::abs(vec.x)) {
			// Front/back
			if (vec.z >= 0.0f) return a->eval(ctx);
			else return b->eval(ctx);
		}
		else {
			// Left/right
			if (vec.x < 0.0f) return c->eval(ctx);
			else return d->eval(ctx);
		}

	}
	virtual void parse(GSFileParser& gsf, const GameSet& gs) override {
		f_unit.reset(ReadFinder(gsf, gs));
		f_target.reset(ReadFinder(gsf, gs));
		QuaternaryEnode::parse(gsf, gs);
	}
};


ValueDeterminer *ReadEquationNode(::GSFileParser &gsf, const ::GameSet &gs)
{
	gsf.advanceLine();
	while (!gsf.eof) {
		std::string strtag = gsf.nextTag();
		if (strtag == "END_EQUATION")
			return new ValueUnknown("<empty EQUATION>");
		else if (strtag == "ENODE") {
			const char *oldcur = gsf.cursor;
			ValueDeterminer *vd;
			std::string nodename = gsf.nextString();
			switch (Tags::ENODE_tagDict.getTagID(nodename.c_str())) {
			case Tags::ENODE_ADDITION: vd = new EnodeAddition; break;
			case Tags::ENODE_NEGATE: vd = new EnodeNegate; break;
			case Tags::ENODE_SUBTRACTION: vd = new EnodeSubtraction; break;
			case Tags::ENODE_MULTIPLICATION: vd = new EnodeMultiplication; break;
			case Tags::ENODE_DIVISION: vd = new EnodeDivision; break;
			case Tags::ENODE_NOT: vd = new EnodeNot; break;
			case Tags::ENODE_IS_ZERO: vd = new EnodeIsZero; break;
			case Tags::ENODE_IS_POSITIVE: vd = new EnodeIsPositive; break;
			case Tags::ENODE_IS_NEGATIVE: vd = new EnodeIsNegative; break;
			case Tags::ENODE_ABSOLUTE_VALUE: vd = new EnodeAbsoluteValue; break;
			case Tags::ENODE_RANDOM_UP_TO: vd = new EnodeRandomUpTo; break;
			case Tags::ENODE_ROUND: vd = new EnodeRound; break;
			case Tags::ENODE_TRUNC: vd = new EnodeTrunc; break;
			case Tags::ENODE_AND: vd = new EnodeAnd; break;
			case Tags::ENODE_OR: vd = new EnodeOr; break;
			case Tags::ENODE_LESS_THAN: vd = new EnodeLessThan; break;
			case Tags::ENODE_LESS_THAN_OR_EQUAL_TO: vd = new EnodeLessThanOrEqualTo; break;
			case Tags::ENODE_GREATER_THAN: vd = new EnodeGreaterThan; break;
			case Tags::ENODE_GREATER_THAN_OR_EQUAL_TO: vd = new EnodeGreaterThanOrEqualTo; break;
			case Tags::ENODE_EQUALS: vd = new EnodeEquals; break;
			case Tags::ENODE_MAX: vd = new EnodeMax; break;
			case Tags::ENODE_MIN: vd = new EnodeMin; break;
			case Tags::ENODE_RANDOM_INTEGER: vd = new EnodeRandomInteger; break;
			case Tags::ENODE_RANDOM_RANGE: vd = new EnodeRandomRange; break;
			case Tags::ENODE_IF_THEN_ELSE: vd = new EnodeIfThenElse; break;
			case Tags::ENODE_IS_BETWEEN: vd = new EnodeIsBetween; break;
			case Tags::ENODE_FRONT_BACK_LEFT_RIGHT: vd = new EnodeFrontBackLeftRight; break;
			default:
				if (nodename == "CEIL") { vd = new EnodeCeil; break; }
				gsf.cursor = oldcur;
				return ReadValueDeterminer(gsf, gs);
			}
			vd->parse(gsf, gs);
			return vd;
		}
		gsf.advanceLine();
	}
	ferr("Equation reached end of file without END_EQUATION!");
	return nullptr;
}

//}