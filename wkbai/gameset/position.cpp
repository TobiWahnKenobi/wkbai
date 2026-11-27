// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#define _USE_MATH_DEFINES
#include "position.h"
#include "../util/vecmat.h"
#include "../server.h"
#include "finder.h"
#include <memory>
#include "../util/util.h"
#include "../util/GSFileParser.h"
#include "../tags.h"
#include "../terrain.h"
#include <tuple>
#include "values.h"
#include <cmath>
#include "ScriptContext.h"
#include "GameObjBlueprint.h"
#include "../Model.h"
#include "gameset.h"
#include "KeywordStubs.h"
#include "../util/SimRandom.h"
#include "../StampdownPlan.h"
#include "../MovementController.h"
#include "../Naval.h"
#include "../AIUtil.h"
#include <algorithm>
#include <map>
#include <numeric>

namespace {
	OrientedPosition PosFromObjVec(const ObjectFinderResult &vec) {
		OrientedPosition sum;
		for (CommonGameObject* obj : vec)
			sum += {obj->position, obj->orientation};
		return vec.empty() ? OrientedPosition() : (sum / (float)vec.size());
	}
}

struct PDUnknown : PositionDeterminer {
	std::string name;
	virtual OrientedPosition eval(ScriptContext* ctx) override
	{
		ferr("Unknown position determiner %s called from %s!", name.c_str(), ctx->gameState->getProgramName());
		return OrientedPosition();
	}
	virtual void parse(GSFileParser & gsf, const GameSet & gs) override
	{
	}
	PDUnknown(const std::string& name) : name(name) {}
};

// Known keyword without an implementation yet, see KeywordStubs.h.
// Falls back to its first position argument, or the location of its first finder
// argument, rather than the map origin.
struct PDStub : PositionDeterminer {
	KeywordStubArgs args;
	virtual OrientedPosition eval(ScriptContext* ctx) override
	{
		args.warnOnce();
		if (!args.positions.empty())
			return args.positions[0]->eval(ctx);
		if (!args.finders.empty())
			return PosFromObjVec(args.finders[0]->eval(ctx));
		return OrientedPosition();
	}
	virtual void parse(GSFileParser & gsf, const GameSet & gs) override { args.parse(gsf, gs); }
	PDStub(const std::string& name, const char* signature) : args(name, signature) {}
};

struct PDLocationOf : PositionDeterminer {
	std::unique_ptr<ObjectFinder> finder;
	virtual OrientedPosition eval(ScriptContext* ctx) override
	{
		return PosFromObjVec(finder->eval(ctx));
	}
	virtual void parse(GSFileParser &gsf, const GameSet &gs) override
	{
		finder.reset(ReadFinder(gsf, gs));
	}
};

struct PDAbsolutePosition : PositionDeterminer {
	OrientedPosition opos;
	virtual OrientedPosition eval(ScriptContext* ctx) override
	{
		return opos;
	}
	virtual void parse(GSFileParser & gsf, const GameSet & gs) override
	{
		float x = gsf.nextFloat();
		float y = gsf.nextFloat();
		float z = gsf.nextFloat();
		float a = gsf.nextFloat();
		float b = gsf.nextFloat();
		opos = { Vector3(x, y, z), Vector3(a, b, 0.0f) };
	}
};

struct PDCentreOfMap : PositionDeterminer {
	virtual OrientedPosition eval(ScriptContext* ctx) override {
		Vector3 pos;
		std::tie(pos.x, pos.z) = Server::instance->terrain->getPlayableArea();
		// the middle of the playable area (it was its far corner, off the map)
		pos.x *= 0.5f;
		pos.z *= 0.5f;
		pos.y =Server::instance->terrain->getHeight(pos.x, pos.z);
		return { pos };
	}
	virtual void parse(GSFileParser & gsf, const GameSet & gs) override {
	}
};

struct PDThisSideOf : PositionDeterminer {
	std::unique_ptr<ObjectFinder> a, b;
	std::unique_ptr<ValueDeterminer> v;
	virtual OrientedPosition eval(ScriptContext* ctx) override {
		OrientedPosition o = PosFromObjVec(a->eval(ctx)), p = PosFromObjVec(b->eval(ctx));
		Vector3 d = (o.position - p.position).normal();
		return { p.position + d * v->eval(ctx), atan2f(d.x, -d.z) };
	}
	virtual void parse(GSFileParser & gsf, const GameSet & gs) override {
		a.reset(ReadFinder(gsf, gs));
		b.reset(ReadFinder(gsf, gs));
		v.reset(ReadValueDeterminer(gsf, gs));
	}
};

struct PDThisOtherSideOf : PositionDeterminer {
	std::unique_ptr<ObjectFinder> a, b;
	std::unique_ptr<ValueDeterminer> v;
	virtual OrientedPosition eval(ScriptContext* ctx) override {
		OrientedPosition o = PosFromObjVec(a->eval(ctx)), p = PosFromObjVec(b->eval(ctx));
		Vector3 d = (p.position - o.position).normal();
		return { p.position + d * v->eval(ctx), atan2f(d.x, -d.z) };
	}
	virtual void parse(GSFileParser & gsf, const GameSet & gs) override {
		a.reset(ReadFinder(gsf, gs));
		b.reset(ReadFinder(gsf, gs));
		v.reset(ReadValueDeterminer(gsf, gs));
	}
};

// NEAREST_VALID_POSITION_FOR <object> <position>: the nearest place to the position where the
// object can stand (MovementController::isTileBlocked)
struct PDNearestValidPositionFor : PositionDeterminer {
	std::unique_ptr<ObjectFinder> a;
	std::unique_ptr<PositionDeterminer> p;
	virtual OrientedPosition eval(ScriptContext* ctx) override {
		OrientedPosition po = p->eval(ctx);
		if (!ctx->isServer())
			return po;
		if (CommonGameObject* obj = a->getFirst(ctx))
			po.position = MovementController::nearestValidSpot((ServerGameObject*)obj, po.position, po.rotation.y);
		return po;
	}
	virtual void parse(GSFileParser & gsf, const GameSet & gs) override {
		a.reset(ReadFinder(gsf, gs));
		p.reset(PositionDeterminer::createFrom(gsf, gs));
	}
};

// NEAREST_VALID_POSITION_TOWARDS <object> <from> <towards>: as far as the object can go from the
// first position in a straight line towards the second ("Move to Nearest Valid Position Towards
// Target"); from a place it can't stand on, the nearest place it can
struct PDNearestValidPositionTowards : PositionDeterminer {
	std::unique_ptr<ObjectFinder> a;
	std::unique_ptr<PositionDeterminer> from, towards;
	virtual OrientedPosition eval(ScriptContext* ctx) override {
		OrientedPosition po = from->eval(ctx);
		const Vector3 goal = towards->eval(ctx).position;
		CommonGameObject* obj = a->getFirst(ctx);
		if (!ctx->isServer() || !obj)
			return po;
		const GameObjBlueprint* bp = obj->blueprint;
		auto blocked = [bp](const Vector3& v) { return MovementController::isTileBlocked(bp, (int)(v.x / 5.0f), (int)(v.z / 5.0f)); };
		if (blocked(po.position)) {
			po.position = MovementController::nearestValidPosition(bp, po.position);
			return po;
		}
		const Vector3 start = po.position;
		const float length = (goal - start).len2xz();
		const int steps = (int)(length / 2.5f);
		Vector3 last = start;
		for (int i = 1; i <= steps; i++) {
			const Vector3 v = start + (goal - start) * ((float)i / steps);
			if (blocked(v))
				break;
			last = v;
		}
		if (steps > 0 && !blocked(goal) && (last - start).sqlen2xz() >= (goal - start).sqlen2xz() * 0.98f)
			last = goal;
		po.position = last;
		if (Server::instance && Server::instance->terrain)
			po.position.y = Server::instance->terrain->getHeightEx(last.x, last.z, bp->canWalkOnWater());
		return po;
	}
	virtual void parse(GSFileParser& gsf, const GameSet& gs) override {
		a.reset(ReadFinder(gsf, gs));
		from.reset(PositionDeterminer::createFrom(gsf, gs));
		towards.reset(PositionDeterminer::createFrom(gsf, gs));
	}
};

// DESTINATION_OF <object>: where the object is moving to, or where it is
struct PDDestinationOf : PositionDeterminer {
	std::unique_ptr<ObjectFinder> f;
	virtual OrientedPosition eval(ScriptContext* ctx) override {
		CommonGameObject* obj = f->getFirst(ctx);
		if (!obj)
			return {};
		OrientedPosition po{ obj->position, obj->orientation };
		if (ctx->isServer()) {
			auto* sobj = (ServerGameObject*)obj;
			if (sobj->movementController.isMoving())
				po.position = sobj->movementController.getDestination();
			else if (sobj->movement.isMoving())
				po.position = sobj->movement.getDestination();
		}
		return po;
	}
	virtual void parse(GSFileParser& gsf, const GameSet& gs) override {
		f.reset(ReadFinder(gsf, gs));
	}
};

// POSITION_BETWEEN <a> <b> <fraction>: the point that fraction of the way from a to b
struct PDPositionBetween : PositionDeterminer {
	std::unique_ptr<ObjectFinder> fa, fb;
	std::unique_ptr<ValueDeterminer> fraction;
	virtual OrientedPosition eval(ScriptContext* ctx) override {
		OrientedPosition a = PosFromObjVec(fa->eval(ctx));
		const OrientedPosition b = PosFromObjVec(fb->eval(ctx));
		const float t = fraction->eval(ctx);
		a.position = a.position + (b.position - a.position) * t;
		return a;
	}
	virtual void parse(GSFileParser& gsf, const GameSet& gs) override {
		fa.reset(ReadFinder(gsf, gs));
		fb.reset(ReadFinder(gsf, gs));
		fraction.reset(ReadValueDeterminer(gsf, gs));
	}
};

struct PDOutAtAngle : public PositionDeterminer
{
	std::unique_ptr<ObjectFinder> f;
	std::unique_ptr<ValueDeterminer> u, v;
	virtual OrientedPosition eval(ScriptContext* ctx) override {
		OrientedPosition po = PosFromObjVec(f->eval(ctx));
		po.rotation.y -= u->eval(ctx) * (float)M_PI / 180;
		float l = v->eval(ctx);
		po.position.x += l * sin(po.rotation.y);
		po.position.z -= l * cos(po.rotation.y);
		po.rotation.y += (float)M_PI;
		return po;
	}
	virtual void parse(GSFileParser& gsf, const GameSet& gs) override {
		f.reset(ReadFinder(gsf, gs));
		u.reset(ReadValueDeterminer(gsf, gs));
		v.reset(ReadValueDeterminer(gsf, gs));
	}
};

struct PDTowards : public PositionDeterminer {
	std::unique_ptr<ObjectFinder> a, b;
	std::unique_ptr<ValueDeterminer> v;
	virtual OrientedPosition eval(ScriptContext* ctx) override {
		OrientedPosition o = PosFromObjVec(a->eval(ctx));
		OrientedPosition p = PosFromObjVec(b->eval(ctx));
		Vector3 d = (p.position - o.position).normal2xz();
		float m = v->eval(ctx);
		return { o.position + d * m, atan2f(d.x, -d.z) };
	}
	virtual void parse(GSFileParser& gsf, const GameSet& gs) override {
		a.reset(ReadFinder(gsf, gs));
		b.reset(ReadFinder(gsf, gs));
		v.reset(ReadValueDeterminer(gsf, gs));
	}
};

struct PDAwayFrom : public PositionDeterminer {
	std::unique_ptr<ObjectFinder> a, b;
	std::unique_ptr<ValueDeterminer> v;
	virtual OrientedPosition eval(ScriptContext* ctx) override {
		OrientedPosition o = PosFromObjVec(a->eval(ctx));
		OrientedPosition p = PosFromObjVec(b->eval(ctx));
		Vector3 d = (o.position - p.position).normal2xz();
		float m = v->eval(ctx);
		return { o.position + d * m, atan2f(d.x, -d.z) };
	}
	virtual void parse(GSFileParser& gsf, const GameSet& gs) override {
		a.reset(ReadFinder(gsf, gs));
		b.reset(ReadFinder(gsf, gs));
		v.reset(ReadValueDeterminer(gsf, gs));
	}
};

struct PDInFrontOf : public PositionDeterminer {
	std::unique_ptr<ObjectFinder> f;
	std::unique_ptr<ValueDeterminer> v;
	virtual OrientedPosition eval(ScriptContext* ctx) override {
		OrientedPosition op = PosFromObjVec(f->eval(ctx));
		Vector3 d{ sinf(op.rotation.y), 0.0f, -cosf(op.rotation.y) };
		op.position += d * v->eval(ctx);
		return op;
	}
	virtual void parse(GSFileParser& gsf, const GameSet& gs) override {
		f.reset(ReadFinder(gsf, gs));
		v.reset(ReadValueDeterminer(gsf, gs));
	}
};

struct PDBehind : PositionDeterminer {
	std::unique_ptr<ObjectFinder> f;
	std::unique_ptr<ValueDeterminer> v;
	virtual OrientedPosition eval(ScriptContext* ctx) override {
		OrientedPosition op = PosFromObjVec(f->eval(ctx));
		Vector3 d{ sinf(op.rotation.y), 0.0f, -cosf(op.rotation.y) };
		op.position -= d * v->eval(ctx);
		return op;
	}
	virtual void parse(GSFileParser& gsf, const GameSet& gs) override {
		f.reset(ReadFinder(gsf, gs));
		v.reset(ReadValueDeterminer(gsf, gs));
	}
};

struct PDOffsetFrom : public PositionDeterminer {
	std::unique_ptr<ObjectFinder> f;
	std::unique_ptr<ValueDeterminer> x, y, z;
	virtual OrientedPosition eval(ScriptContext* ctx) override {
		return PosFromObjVec(f->eval(ctx)) + OrientedPosition(Vector3(x->eval(ctx), y->eval(ctx), z->eval(ctx)));
	}
	virtual void parse(GSFileParser& gsf, const GameSet& gs) override {
		f.reset(ReadFinder(gsf, gs));
		x.reset(ReadValueDeterminer(gsf, gs));
		y.reset(ReadValueDeterminer(gsf, gs));
		z.reset(ReadValueDeterminer(gsf, gs));
	}
};

struct PDNearestAttachmentPoint : public PositionDeterminer {
	//int attachTag;
	std::string sAttachTag;
	std::unique_ptr<ObjectFinder> finder;
	std::unique_ptr<PositionDeterminer> pos;
	std::unique_ptr<ValueDeterminer> tochoose;
	virtual OrientedPosition eval(ScriptContext* ctx) override {
		CommonGameObject* obj = finder->getFirst(ctx);
		if (Model* model = obj->getModel()) {
			auto target = pos->eval(ctx);
			size_t numAPs = model->getNumAPs();
			size_t bestAP = -1;
			Vector3 bestPos;
			float bestDist = std::numeric_limits<float>::infinity();
			for (size_t i = 0; i < numAPs; i++) {
				const auto& apinfo = model->getAPInfo(i);
				if (apinfo.tag.substr(0, sAttachTag.size()) == sAttachTag) {
					Vector3 appos = apinfo.staticState.position.transform(obj->getWorldMatrix());
					float dist = (target.position - appos).sqlen3();
					if (dist < bestDist) {
						bestDist = dist;
						bestAP = i;
						bestPos = appos;
					}
				}
			}
			if (bestAP != -1)
				return { bestPos };
		}
		return obj->position + Vector3(0.0f, 1.0f, 0.0f);
	}
	virtual void parse(GSFileParser& gsf, const GameSet& gs) override {
		sAttachTag = gsf.nextString(true);
		finder.reset(ReadFinder(gsf, gs));
		pos.reset(PositionDeterminer::createFrom(gsf, gs));
		const char* prevcur = gsf.cursor;
		if (!gsf.eol) {
			if (gsf.nextString() == "CHOOSE_FROM_NEAREST")
				tochoose.reset(ReadValueDeterminer(gsf, gs));
			else
				gsf.cursor = prevcur;
		}
	}
};

struct PDSpawnTilePosition : PositionDeterminer {
	std::unique_ptr<ObjectFinder> finder;
	virtual OrientedPosition eval(ScriptContext* ctx) override {
		// TODO
		return PosFromObjVec(finder->eval(ctx));
	}
	virtual void parse(GSFileParser& gsf, const GameSet& gs) override {
		finder.reset(ReadFinder(gsf, gs));
	}
};

struct PDMatchingOffset : PositionDeterminer {
	std::unique_ptr<PositionDeterminer> cp;
	std::unique_ptr<PositionDeterminer> cr;
	std::unique_ptr<PositionDeterminer> co;

	virtual OrientedPosition eval(ScriptContext* ctx) override {
		OrientedPosition pp = cp->eval(ctx);
		OrientedPosition pr = cr->eval(ctx);
		OrientedPosition po = co->eval(ctx);

		Vector3 v = po.position - pr.position;
		float vo = atan2f(v.x, -v.z);
		float ro = vo - pr.rotation.y;
		float rl = v.len2xz();

		ro += pp.rotation.y;
		Vector3 nv = Vector3(sinf(ro) * rl, 0.0f, cosf(ro) * rl);
		return { pp.position + nv };
	}
	virtual void parse(GSFileParser& gsf, const GameSet& gs) override {
		cp.reset(PositionDeterminer::createFrom(gsf, gs));
		cr.reset(PositionDeterminer::createFrom(gsf, gs));
		co.reset(PositionDeterminer::createFrom(gsf, gs));
	}
};

struct PDFiringAttachmentPoint : PositionDeterminer {
	virtual OrientedPosition eval(ScriptContext* ctx) override {
		CommonGameObject* obj = ctx->getSelf();
		Model* model = obj->blueprint->getModel(obj->subtype, obj->appearance, obj->animationIndex, obj->animationVariant);
		if (model) {
			size_t numAPs = model->getNumAPs();
			for (size_t i = 0; i < numAPs; i++) {
				if (model->getAPInfo(i).tag.substr(0, 3) == "SP_") {
					Matrix mat = obj->getWorldMatrix();
					// the point where it is in the animation now (as the ATTACHMENT_POINT trigger
					// reads it); passing animStartTime itself took the frame at that many ms
					const float now = Server::instance ? Server::instance->timeManager.currentTime : obj->animStartTime;
					const uint32_t animTime = (uint32_t)(std::max(0.0f, now - obj->animStartTime) * obj->animSpeed * 1000.0f);
					return model->getAPState(i, animTime).position.transform(mat);
				}
			}
		}
		return obj->position + Vector3(0, 1, 0);
	}
	virtual void parse(GSFileParser& gsf, const GameSet& gs) override {
	}
};

struct PDNearestValidStampdownPos : PositionDeterminer {
	const GameObjBlueprint* objbp;
	std::unique_ptr<PositionDeterminer> p;
	std::unique_ptr<ObjectFinder> fdExemptTilesReservedBy;
	// GLOBAL_CAN_REACH_FOR_CONSTRUCTION "Land Unit" (Passability_Fields.cpp: "used by
	// NEAREST_VALID_STAMPDOWN_POSITION, NEAREST_CONSTRUCTION_SITE, RESERVE_SPACE_FOR_CONSTRUCTION"):
	// a site only where the player's land units can walk: a tile of the footprint in a land region
	// where it has peasants (its home's always). Not the home's alone: on 3 Isles Kolvas starts with
	// buildings and peasants on a second landmass too, and his farms there were refused. wkbre2 had none: on 3 Isles the Kingmaker's first village went to
	// another player's island, its builder waited there for the whole game and its economy never
	// started (WKB.exe refuses AI Generals on the island maps). -1: no limit (the client, or a
	// start no land unit can stand near).
	// whether the ministry AI plays this player (as Ministries::update picks them: the level's
	// players by id, the lobby's aiMinistriesSlots): known from the start, before the first village
	// is laid and before the government's first step
	static bool MinistryPlayer(Server* srv, ServerGameObject* player) {
		if (!srv->aiMinistries || !player->aiController.isAI())
			return false;
		if (player->aiController.ministryWaves || srv->aiMinistriesSlots == 0)
			return true;
		ServerGameObject* level = (ServerGameObject*)srv->getLevel();
		if (!level)
			return false;
		std::vector<uint32_t> ids;
		for (const auto& [type, children] : level->children)
			for (CommonGameObject* child : children) {
				ServerGameObject* p = (ServerGameObject*)child;
				if (p->blueprint->bpClass == Tags::GAMEOBJCLASS_PLAYER && p->blueprint->name != "Neutral Player" && !p->deleted)
					ids.push_back(p->id);
			}
		std::sort(ids.begin(), ids.end());
		const size_t i = std::find(ids.begin(), ids.end(), player->id) - ids.begin();
		return i < ids.size() && i < 31 && ((srv->aiMinistriesSlots >> i) & 1);
	}
	int reachClass = -1, reachRegion = -1;
	const std::vector<int>* reachRegions = nullptr; // where the player has peasants
	void setReach(ScriptContext* ctx, Vector3& from) {
		reachClass = reachRegion = -1;
		Server* srv = Server::instance;
		// (the villages' rule for every player: 3 Isles' position 0 laid its first village on another
		// island and built nothing more all game, whatever AI played it but the ministry, 31 buildings
		// in 45 minutes; with it 85-118 in 30, and the Lua AI's Kolvas still builds, 82-93. WKB_REACH=1
		// every building for every player, 3 the ministry's players only (as before 2026-11-22), 0 off)
		static const char* const env = getenv("WKB_REACH");
		static const int mode = env ? atoi(env) : -1;
		if (mode == 0 || !srv || ctx->gameState != srv || !srv->terrain)
			return;
		CommonGameObject* self = ctx->getSelf();
		ServerGameObject* player = self ? (ServerGameObject*)self->getPlayer() : nullptr;
		if (!player || player->blueprint->bpClass != Tags::GAMEOBJCLASS_PLAYER)
			return;
		if (mode == 3 && !MinistryPlayer(srv, player))
			return;
		// (villages only, unless WKB_REACH=1: the first village is what went to another island; with
		// the farms in the rule too, Kolvas's farm search was refused and his commissions stalled)
		if (mode != 1 && objbp->name.find("Village") == std::string::npos)
			return;
		int cls = -1;
		reachRegions = &Naval::PeasantRegions(player, &cls);
		// (a search from land without the player's peasants, e.g. 3 Isles' start buildings on the
		// central landmass, from home: else no site at all, and Kolvas had no farm)
		Naval::StartAtHomeIfUnreachable(player, from);
		reachRegion = reachRegions->empty() ? -1 : reachRegions->front();
		const auto [w, h] = srv->terrain->getNumPlayableTiles();
		if (reachRegion >= 0)
			reachClass = cls;
		if (const char* e = getenv("WKB_TRACE_REACH"); e && reachRegion >= 0 && atoi(e) == reachRegion) {
			static int shown = 0;
			if (shown++ < 30) {
				std::string regs;
				for (int r : *reachRegions)
					regs += " " + std::to_string(r);
				printf("REACH search t=%.0f %s by %s #%u from (%.0f, %.0f), regions%s\n", srv->timeManager.currentTime, objbp->name.c_str(),
					self->blueprint->name.c_str(), self->id, from.x, from.z, regs.c_str());
			}
		}
	}
	// The enemies' doorsteps, for the ministry AI's players (WKB_MINOFF 131072 off): no site in the square
	// of 275 m round an enemy's palace (FINDER_METRE_RADIUS searches a square of tiles, NNSearch, so its
	// "250m" reaches 257 m along the axes and 364 m at the corners). The Kingmaker scripts laid a wood village and 14-19 farms within 250 m of
	// the enemy's Manor at dawn ("Enemies Within 250m of My Manor"); that set off the enemy's War Mode,
	// some 35 of its peasants took up arms as militia and wiped out the town (4 Peaks, slot 1, docs
	// engine/ministry-ai.md). At most 0.75 of the palaces' distance along the farther axis, so a close
	// start keeps its own ground.
	std::vector<std::pair<Vector3, float>> doorsteps;
	void setDoorsteps(ScriptContext* ctx) {
		doorsteps.clear();
		Server* srv = Server::instance;
		static const bool off = getenv("WKB_MINOFF") && (atoi(getenv("WKB_MINOFF")) & 131072);
		if (off || !srv || ctx->gameState != srv)
			return;
		CommonGameObject* self = ctx->getSelf();
		ServerGameObject* player = self ? (ServerGameObject*)self->getPlayer() : nullptr;
		if (!player || player->blueprint->bpClass != Tags::GAMEOBJCLASS_PLAYER || !MinistryPlayer(srv, player))
			return;
		ServerGameObject* level = (ServerGameObject*)srv->getLevel();
		if (!level)
			return;
		ServerGameObject* mine = AIUtil::Palace(player);
		for (const auto& [type, children] : level->children)
			for (CommonGameObject* child : children) {
				ServerGameObject* p = (ServerGameObject*)child;
				if (p == player || p->deleted || p->blueprint->bpClass != Tags::GAMEOBJCLASS_PLAYER || !AIUtil::IsEnemy(player, p))
					continue;
				if (ServerGameObject* palace = AIUtil::Palace(p)) {
					float r = 275.0f;
					if (mine)
						r = std::min(r, 0.75f * std::max(std::abs(mine->position.x - palace->position.x), std::abs(mine->position.z - palace->position.z)));
					doorsteps.emplace_back(palace->position, r);
				}
			}
		if (getenv("WKB_TRACE_DOOR")) {
			static int shown = 0;
			if (shown++ < 40)
				for (const auto& [at, r] : doorsteps)
					printf("DOOR t=%.0f player %u %s: keep %.0f m off (%.0f, %.0f)\n", srv->timeManager.currentTime, player->id, objbp->name.c_str(), r, at.x, at.z);
		}
	}
	bool reachable(int tx, int tz) const {
		if (reachRegion < 0)
			return true;
		for (const auto& to : objbp->footprint->tiles) {
			const int r = MovementController::terrainRegion(reachClass, tx + to.offsetX, tz + to.offsetZ);
			if (r >= 0 && reachRegions && std::binary_search(reachRegions->begin(), reachRegions->end(), r))
				return true;
		}
		if (const char* e = getenv("WKB_TRACE_REACH"); e && (atoi(e) == 1 || atoi(e) == reachRegion)) {
			static int shown = 0;
			if (shown++ < 12) {
				std::string regs;
				for (const auto& to : objbp->footprint->tiles)
					regs += " " + std::to_string(MovementController::terrainRegion(reachClass, tx + to.offsetX, tz + to.offsetZ));
				printf("REACH refused %s at tile (%d, %d), home region %d, the footprint's:%s\n", objbp->name.c_str(), tx, tz, reachRegion, regs.c_str());
			}
		}
		return false;
	}
	bool isTileFree(CommonGameState* server, int tx, int tz) {
		int sx, sz;
		std::tie(sx, sz) = server->terrain->getNumPlayableTiles();
		if (!(tx >= 0 && tx < sx && tz >= 0 && tz < sz))
			return false;
		const auto& occupiedBuilding = server->tiles[tz * sx + tx].building.getFrom<Server>();
		if (!occupiedBuilding)
			return true;
		if (objbp->buildingType == Tags::BUILDINGTYPE_CIVIC || objbp->buildingType == Tags::BUILDINGTYPE_CIVIC_CENTRE) {
			const auto occupiedBuildingType = occupiedBuilding->blueprint->buildingType;
			if (occupiedBuildingType == Tags::BUILDINGTYPE_WALL
				|| occupiedBuildingType == Tags::BUILDINGTYPE_WALL_CORNER_IN
				|| occupiedBuildingType == Tags::BUILDINGTYPE_WALL_CORNER_OUT
				|| occupiedBuildingType == Tags::BUILDINGTYPE_WALL_CROSSROADS)
				return true;
		}
		return false;
	}
	std::vector<CommonGameObject::CityRectangle> cityRects; // (eval: gathered for its search)
	bool canBuildOn(CommonGameState* server, int tx, int tz) {
		const int bt = objbp->buildingType;
		const bool walls = bt == Tags::BUILDINGTYPE_WALL || bt == Tags::BUILDINGTYPE_WALL_CORNER_IN || bt == Tags::BUILDINGTYPE_WALL_CORNER_OUT
			|| bt == Tags::BUILDINGTYPE_WALL_CROSSROADS || objbp->name.find("Gate") != std::string::npos;
		for (auto& to : objbp->footprint->tiles) {
			if (true /*!to.mode*/) {
				int px = tx + to.offsetX, pz = tz + to.offsetZ;
				if (!isTileFree(server, px, pz))
					return false;
				// as the placement itself (StampdownPlan): no farm on a hill, no house on sand
				if (!StampdownPlan::tileTerrainAllowed(server, to, px, pz))
					return false;
				// no farm in a city, past the walls (the play test: a farmer's farm inside them)
				if (StampdownPlan::isRural(objbp) && StampdownPlan::tileInRectangles(cityRects, px, pz))
					return false;
				// not across a gate's way out (a blocking tile of the footprint; StampdownPlan.h)
				// (walls and gates join there)
				if (!to.mode && !walls && StampdownPlan::tileBesideGate(server, px, pz))
					return false;
			}
		}
		// where a land unit can get to from the start (GLOBAL_CAN_REACH_FOR_CONSTRUCTION)
		if (!reachable(tx, tz))
			return false;
		// not on an enemy's doorstep (setDoorsteps)
		if (!doorsteps.empty()) {
			const float x = tx * 5.0f + objbp->footprint->originX + 2.5f, z = tz * 5.0f + objbp->footprint->originZ + 2.5f;
			for (const auto& [at, r] : doorsteps)
				if (std::abs(x - at.x) < r && std::abs(z - at.z) < r)
					return false;
		}
		// and the slope limit and the neighbour rule, as WKB.exe's site checks (0x59b020)
		return StampdownPlan::groundAllows(server, objbp->footprint, tx, tz);
	}
	std::pair<int, int> findPos(CommonGameState* server, int ox, int oz) {
		if (canBuildOn(server, ox, oz))
			return { ox, oz };
		auto area = server->terrain->getNumPlayableTiles();
		int maxr = std::max(area.first, area.second);
		for (int r = 1; r < maxr; r++) {
			int x = ox + r;
			int z = oz;
			for (int i = 0; i < r; i++) {
				if (canBuildOn(server, x, z))
					return { x, z };
				x--; z++;
			}
			for (int i = 0; i < r; i++) {
				if (canBuildOn(server, x, z))
					return { x, z };
				x--; z--;
			}
			for (int i = 0; i < r; i++) {
				if (canBuildOn(server, x, z))
					return { x, z };
				x++; z--;
			}
			for (int i = 0; i < r; i++) {
				if (canBuildOn(server, x, z))
					return { x, z };
				x++; z++;
			}
		}
		return { ox, oz }; // in the unfortunate case where no free tile was found
	}
	virtual OrientedPosition eval(ScriptContext* ctx) override {
		auto po = p->eval(ctx);
		// no place to start from (LOCATION_OF a finder that found nothing): none, not the map's
		// corner. The Kingmaker AI's farms went to the far side of the map when it had no village
		// for them, and its builders spent the game walking there.
		if (!objbp->footprint || (po.position.x == 0.0f && po.position.z == 0.0f))
			return po;
		const Vector3 asked = po.position;
		setReach(ctx, po.position);
		setDoorsteps(ctx);
		// (the cities' rectangles once for the whole search: canBuildOn's farm-in-a-city test)
		cityRects = StampdownPlan::allCityRectangles(ctx->gameState);
		int ox = (int)((po.position.x - objbp->footprint->originX) / 5.0f);
		int oz = (int)((po.position.z - objbp->footprint->originZ) / 5.0f);
		const int sx = ox, sz = oz;
		std::tie(ox, oz) = findPos(ctx->gameState, ox, oz);
		// no reachable site at all: the search as without the rule (on 3 Isles Kolvas's farms found
		// farmland only on another island; refused, his commission stopped at its farms and laid
		// nothing else for 25 minutes. A site that can't be reached costs the commission less.)
		// (and none off the doorsteps: the search as without them)
		if (!doorsteps.empty() && !canBuildOn(ctx->gameState, ox, oz)) {
			doorsteps.clear();
			ox = (int)((po.position.x - objbp->footprint->originX) / 5.0f);
			oz = (int)((po.position.z - objbp->footprint->originZ) / 5.0f);
			std::tie(ox, oz) = findPos(ctx->gameState, ox, oz);
		}
		if (reachRegion >= 0 && !canBuildOn(ctx->gameState, ox, oz)) {
			reachRegion = -1;
			ox = (int)((asked.x - objbp->footprint->originX) / 5.0f);
			oz = (int)((asked.z - objbp->footprint->originZ) / 5.0f);
			std::tie(ox, oz) = findPos(ctx->gameState, ox, oz);
		}
		if (const char* e = getenv("WKB_TRACE_REACH"); e && reachRegion >= 0 && atoi(e) == reachRegion)
			printf("REACH result %s: from tile (%d, %d) to (%d, %d), %s\n", objbp->name.c_str(), sx, sz, ox, oz,
				canBuildOn(ctx->gameState, ox, oz) ? "a site" : "NO SITE (the start returned)");
		float fx = ox * 5 + objbp->footprint->originX + 2.5f;
		float fz = oz * 5 + objbp->footprint->originZ + 2.5f;
		return { Vector3(fx, ctx->gameState->terrain->getHeight(fx, fz), fz) };
	}
	virtual void parse(GSFileParser& gsf, const GameSet& gs) override {
		auto cursor = gsf.cursor;
		objbp = gs.readObjBlueprintPtr(gsf);
		if (!objbp) {
			gsf.cursor = cursor;
			objbp = gs.objBlueprints[Tags::GAMEOBJCLASS_BUILDING].readPtr(gsf);
		}
		p.reset(PositionDeterminer::createFrom(gsf, gs));
		cursor = gsf.cursor;
		if (gsf.nextString(false) == "EXEMPT_TILES_RESERVED_BY") {
			fdExemptTilesReservedBy.reset(ReadFinder(gsf, gs));
		}
		else {
			gsf.cursor = cursor;
		}
	}
};

struct PDRandomAttachmentPoint : public PositionDeterminer {
	std::string sAttachTag;
	std::unique_ptr<ObjectFinder> finder;
	virtual OrientedPosition eval(ScriptContext* ctx) override {
		CommonGameObject* obj = finder->getFirst(ctx);
		if (Model* model = obj->getModel()) {
			size_t numAPs = model->getNumAPs();

			static constexpr size_t MAXAPS = 16;
			std::array<size_t, MAXAPS> validAPs;
			size_t numValidAPs = 0;
			for (size_t i = 0; i < numAPs; i++) {
				const auto& apinfo = model->getAPInfo(i);
				if (apinfo.tag.substr(0, sAttachTag.size()) == sAttachTag) {
					validAPs[numValidAPs++] = i;
					if (numValidAPs >= MAXAPS) break;
				}
			}
			if (numValidAPs > 0) {
				size_t randomAP = ScriptRand(ctx) % numValidAPs;
				return model->getAPInfo(validAPs[randomAP]).staticState.position.transform(obj->getWorldMatrix());
			}
		}
		return obj->position + Vector3(0.0f, 1.0f, 0.0f);
	}
	virtual void parse(GSFileParser& gsf, const GameSet& gs) override {
		sAttachTag = gsf.nextString(true);
		finder.reset(ReadFinder(gsf, gs));
	}
};

// ATTACHMENT_POINT_BY_INDEX "<tag>" <finder> <index> (WKB.exe 0x4a2eb0, mods): the first object's
// attachment point with that tag at the index (truncated), counted from the highest-numbered one
// as the original does; none (found = false) out of range
struct PDAttachmentPointByIndex : public PositionDeterminer {
	std::string sAttachTag;
	std::unique_ptr<ObjectFinder> finder;
	std::unique_ptr<ValueDeterminer> vdIndex;
	virtual OrientedPosition eval(ScriptContext* ctx) override {
		OrientedPosition none;
		none.found = false;
		CommonGameObject* obj = finder->getFirst(ctx);
		if (!obj)
			return none;
		Model* model = obj->getModel();
		if (!model)
			return none;
		std::vector<size_t> aps;
		for (size_t i = model->getNumAPs(); i-- > 0;)
			if (model->getAPInfo(i).tag.substr(0, sAttachTag.size()) == sAttachTag)
				aps.push_back(i);
		const int index = (int)vdIndex->eval(ctx);
		if (index < 0 || index >= (int)aps.size())
			return none;
		return model->getAPInfo(aps[index]).staticState.position.transform(obj->getWorldMatrix());
	}
	virtual void parse(GSFileParser& gsf, const GameSet& gs) override {
		sAttachTag = gsf.nextString(true);
		finder.reset(ReadFinder(gsf, gs));
		vdIndex.reset(ReadValueDeterminer(gsf, gs));
	}
};

// POINT_ALONG_PATH <finder> <position> <position> <value> (mods): read, but as in WKB.exe (its
// evaluation is the empty 0x448a40) it never gives a position
struct PDPointAlongPath : public PositionDeterminer {
	std::unique_ptr<ObjectFinder> finder;
	std::unique_ptr<PositionDeterminer> from, to;
	std::unique_ptr<ValueDeterminer> distance;
	virtual OrientedPosition eval(ScriptContext* ctx) override {
		OrientedPosition none;
		none.found = false;
		return none;
	}
	virtual void parse(GSFileParser& gsf, const GameSet& gs) override {
		finder.reset(ReadFinder(gsf, gs));
		from.reset(PositionDeterminer::createFrom(gsf, gs));
		to.reset(PositionDeterminer::createFrom(gsf, gs));
		distance.reset(ReadValueDeterminer(gsf, gs));
	}
};

// NEAREST_CONSTRUCTION_SITE <position> <building> <minimum sites> <cluster distance> <range>, as
// WKB.exe's (0x4a6a40; parser 0x4a6780): every site within <range> metres of the position
// (snapped to the tile grid) where the building fits, grouped into clusters of sites within
// <cluster distance> of each other (WKB.exe squares it), clusters smaller than <minimum sites>
// left out; the centre of the largest cluster, snapped to the grid: the middle of the biggest
// open area. None (found = false) when a value isn't positive or there is no such cluster:
// the Kingmaker's Farmland Marker (1, 15, 60) then isn't made and the AI knows it has no room
// for another farm. wkbre2 had taken the nearest free spot at any distance.
struct PDNearestConstructionSite : public PDNearestValidStampdownPos {
	std::unique_ptr<ValueDeterminer> val1, val2, val3;
	std::unique_ptr<ObjectFinder> fExemptTilesReservedBy;
	virtual OrientedPosition eval(ScriptContext* ctx) override {
		auto po = p->eval(ctx);
		OrientedPosition none = po;
		none.found = false;
		// no place to start from (LOCATION_OF a finder that found nothing): none, not the map's
		// corner. The Kingmaker AI's farms went to the far side of the map when it had no village
		// for them, and its builders spent the game walking there.
		if (!objbp->footprint || (po.position.x == 0.0f && po.position.z == 0.0f))
			return none;
		const int minSites = (int)val1->eval(ctx);
		const int join = (int)val2->eval(ctx), range = (int)val3->eval(ctx);
		if (minSites <= 0 || join <= 0 || range <= 0)
			return none;
		const Vector3 asked = po.position;
		setReach(ctx, po.position);
		const int ox = (int)((po.position.x - objbp->footprint->originX) / 5.0f);
		const int oz = (int)((po.position.z - objbp->footprint->originZ) / 5.0f);
		const int r = range / 5, jr = join / 5;
		// the sites, and their index by tile
		std::vector<std::pair<int, int>> sites;
		std::map<std::pair<int, int>, int> siteAt;
		auto collect = [&](int cx, int cz) {
			for (int dz = -r; dz <= r; dz++)
				for (int dx = -r; dx <= r; dx++)
					if ((dx * dx + dz * dz) * 25 <= range * range && canBuildOn(ctx->gameState, cx + dx, cz + dz)) {
						siteAt[{ cx + dx, cz + dz }] = (int)sites.size();
						sites.push_back({ cx + dx, cz + dz });
					}
		};
		collect(ox, oz);
		// clusters: sites within the cluster distance joined (union-find); the biggest with
		// minSites or more
		std::map<int, std::pair<int, std::pair<double, double>>> clusters; // root: count, sum of x and z
		auto biggest = [&]() -> const std::pair<int, std::pair<double, double>>* {
			clusters.clear();
			std::vector<int> parent(sites.size());
			std::iota(parent.begin(), parent.end(), 0);
			auto root = [&](int i) { while (parent[i] != i) i = parent[i] = parent[parent[i]]; return i; };
			for (size_t i = 0; i < sites.size(); i++)
				for (int dz = -jr; dz <= jr; dz++)
					for (int dx = -jr; dx <= jr; dx++) {
						if ((dx * dx + dz * dz) * 25 > join * join)
							continue;
						auto it = siteAt.find({ sites[i].first + dx, sites[i].second + dz });
						if (it != siteAt.end())
							parent[root((int)i)] = root(it->second);
					}
			for (size_t i = 0; i < sites.size(); i++) {
				auto& c = clusters[root((int)i)];
				c.first++;
				c.second.first += sites[i].first;
				c.second.second += sites[i].second;
			}
			const std::pair<int, std::pair<double, double>>* best = nullptr;
			for (const auto& [_, c] : clusters)
				if (c.first >= minSites && (!best || c.first > best->first))
					best = &c;
			return best;
		};
		const std::pair<int, std::pair<double, double>>* best = biggest();
		// none in reach: the search as without the rule (on 3 Isles Kolvas's farms found farmland
		// only on another island; refused, his commission stopped at its farms and laid nothing
		// else for 25 minutes. A site that can't be reached costs the commission less.)
		if (!best && reachRegion >= 0) {
			reachRegion = -1;
			sites.clear();
			siteAt.clear();
			collect((int)((asked.x - objbp->footprint->originX) / 5.0f), (int)((asked.z - objbp->footprint->originZ) / 5.0f));
			best = biggest();
			if (getenv("WKB_TRACE_REACH"))
				printf("REACH fallback %s from (%.0f, %.0f): %zu sites, %s\n", objbp->name.c_str(), asked.x, asked.z, sites.size(), best ? "a cluster" : "none");
		}
		if (!best)
			return none;
		const int bx = (int)std::lround(best->second.first / best->first), bz = (int)std::lround(best->second.second / best->first);
		float fx = bx * 5 + objbp->footprint->originX + 2.5f;
		float fz = bz * 5 + objbp->footprint->originZ + 2.5f;
		return { Vector3(fx, ctx->gameState->terrain->getHeight(fx, fz), fz) };
	}
	virtual void parse(GSFileParser& gsf, const GameSet& gs) override {
		p.reset(PositionDeterminer::createFrom(gsf, gs));
		objbp = gs.objBlueprints[Tags::GAMEOBJCLASS_BUILDING].readPtr(gsf);
		val1.reset(ReadValueDeterminer(gsf, gs));
		val2.reset(ReadValueDeterminer(gsf, gs));
		val3.reset(ReadValueDeterminer(gsf, gs));
		auto cursor = gsf.cursor;
		if (gsf.nextString() == "EXEMPT_TILES_RESERVED_BY") {
			fExemptTilesReservedBy.reset(ReadFinder(gsf, gs));
		}
		else {
			gsf.cursor = cursor;
		}
	}
};

PositionDeterminer * PositionDeterminer::createFrom(GSFileParser & gsf, const GameSet & gs)
{
	PositionDeterminer *pos;
	std::string tag = gsf.nextString();
	switch (Tags::POSITION_tagDict.getTagID(tag.c_str())) {
	case Tags::POSITION_LOCATION_OF: pos = new PDLocationOf; break;
	case Tags::POSITION_ABSOLUTE_POSITION: pos = new PDAbsolutePosition; break;
	case Tags::POSITION_CENTRE_OF_MAP: pos = new PDCentreOfMap; break;
	case Tags::POSITION_THIS_SIDE_OF: pos = new PDThisSideOf; break;
	case Tags::POSITION_THE_OTHER_SIDE_OF: pos = new PDThisOtherSideOf; break;
	case Tags::POSITION_NEAREST_VALID_POSITION_FOR: pos = new PDNearestValidPositionFor; break;
	case Tags::POSITION_DESTINATION_OF: pos = new PDDestinationOf; break;
	case Tags::POSITION_OUT_AT_ANGLE: pos = new PDOutAtAngle; break;
	case Tags::POSITION_TOWARDS: pos = new PDTowards; break;
	case Tags::POSITION_AWAY_FROM: pos = new PDAwayFrom; break;
	case Tags::POSITION_IN_FRONT_OF: pos = new PDInFrontOf; break;
	case Tags::POSITION_BEHIND: pos = new PDBehind; break;
	case Tags::POSITION_OFFSET_FROM: pos = new PDOffsetFrom; break;
	case Tags::POSITION_NEAREST_ATTACHMENT_POINT: pos = new PDNearestAttachmentPoint; break;
	case Tags::POSITION_SPAWN_TILE_POSITION: pos = new PDSpawnTilePosition; break;
	case Tags::POSITION_MATCHING_OFFSET: pos = new PDMatchingOffset; break;
	case Tags::POSITION_FIRING_ATTACHMENT_POINT: pos = new PDFiringAttachmentPoint; break;
	case Tags::POSITION_NEAREST_VALID_STAMPDOWN_POS: pos = new PDNearestValidStampdownPos; break;
	case Tags::POSITION_RANDOM_ATTACHMENT_POINT: pos = new PDRandomAttachmentPoint; break;
	case Tags::POSITION_NEAREST_CONSTRUCTION_SITE: pos = new PDNearestConstructionSite; break;
	default:
		if (tag == "NEAREST_VALID_POSITION_TOWARDS") { pos = new PDNearestValidPositionTowards; break; }
		if (tag == "POSITION_BETWEEN") { pos = new PDPositionBetween; break; }
		if (tag == "ATTACHMENT_POINT_BY_INDEX") { pos = new PDAttachmentPointByIndex; break; }
		if (tag == "POINT_ALONG_PATH") { pos = new PDPointAlongPath; break; }
		if (const char* signature = FindStubSignature(positionStubSignatures, tag)) {
			pos = new PDStub(tag, signature);
			printf("WARNING: Position determiner %s is not implemented (arguments read, uses its first argument's position) at %s\n", tag.c_str(), gsf.locate().c_str());
			break;
		}
		pos = new PDUnknown(tag);
		printf("WARNING: Unknown position determiner %s at %s\n", tag.c_str(), gsf.locate().c_str());
		break;
	}
	pos->parse(gsf, gs);
	return pos;
}
