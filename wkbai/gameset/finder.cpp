#include <SDL3/SDL_timer.h>
#include <map>
#include <algorithm>
#include <cstdlib>
#include <string>
// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#include "finder.h"
#include <unordered_set>
#include "../util/GSFileParser.h"
#include "gameset.h"
#include "../util/util.h"
#include "../server.h"
#include "../client.h"
#include "CommonEval.h"
#include "../NNSearch.h"
#include "ScriptContext.h"
#include <cassert>
#include <iterator>
#include "KeywordStubs.h"
#include "Package.h"
#include "../util/SimRandom.h"

ObjectFinderResult ObjectFinder::fail(ScriptContext* ctx) {
	ferr("Invalid object finder call from %s", ctx->gameState->getProgramName());
	return {};
}

// FINDER_ASSOCIATES and FINDER_ASSOCIATORS on the client: the server's associations as mirrored
// (NETCLIMSG_OBJECT_ASSOCIATION, ClientGameObject::associates). A command button's condition is
// drawn on the client: right-clicking with the Druid ended the game with "Invalid object finder
// call from Client" (the Dark Portal's "Don't Already have a portal", the AI play test of
// 2026-11-18).
static ObjectFinderResult ClientAssociations(ScriptContext* ctx, int category, bool associators) {
	CliScriptContext* cctx = (CliScriptContext*)ctx;
	ClientGameObject* self = cctx->getSelf();
	ObjectFinderResult vec;
	if (!self)
		return vec;
	const auto& map = associators ? self->associators : self->associates;
	if (auto it = map.find(category); it != map.end())
		for (const CliGORef& ref : it->second)
			if (ClientGameObject* o = ref.get(); o && o->isInteractable())
				vec.push_back(o);
	return vec;
}

// FINDER_REFERENCERS on the client: the referencers' current tasks live on the server only, so none
// (said once a game run, not fatal)
static ObjectFinderResult ClientHasNoReferencers(ScriptContext* ctx) {
	static bool said = false;
	if (!said) {
		said = true;
		printf("(FINDER_REFERENCERS on the %s: none there, the tasks are the server's)\n", ctx->gameState->getProgramName());
	}
	return {};
}

struct FinderUnknown : ObjectFinder {
	std::string name;
	virtual ObjectFinderResult eval(ScriptContext* ctx) override {
		ferr("Unknown object finder %s called from the %s!", name.c_str(), ctx->gameState->getProgramName());
		return {};
	}
	virtual void parse(GSFileParser &gsf, const GameSet &gs) override {
	}
	FinderUnknown(const std::string& name) : name(name) {}
};

// Known keyword without an implementation yet, see KeywordStubs.h
struct FinderStub : ObjectFinder {
	KeywordStubArgs args;
	virtual ObjectFinderResult eval(ScriptContext* ctx) override { args.warnOnce(); return {}; }
	virtual void parse(GSFileParser &gsf, const GameSet &gs) override { args.parse(gsf, gs); }
	FinderStub(const std::string& name, const char* signature) : args(name, signature) {}
};

struct FinderSelf : ObjectFinder {
	virtual ObjectFinderResult eval(ScriptContext* ctx) override {
		auto obj = ctx->getSelf();
		if (obj) return { obj };
		else return {};
	}
	virtual void parse(GSFileParser &gsf, const GameSet &gs) override {
	}
};

struct FinderSpecificId : ObjectFinder {
	uint32_t objid;
	virtual ObjectFinderResult eval(ScriptContext* ctx) override {
		auto obj = ctx->gameState->findObject(objid);
		if (obj) return { obj };
		else return {};
	}
	virtual void parse(GSFileParser &gsf, const GameSet &gs) override {
		objid = gsf.nextInt();
	}
	FinderSpecificId() {}
	FinderSpecificId(uint32_t objid) : objid(objid) {}
};

struct FinderPlayer : ObjectFinder {
	virtual ObjectFinderResult eval(ScriptContext* ctx) override {
		auto obj = ctx->getSelf()->getPlayer();
		if (obj) return { obj };
		else return {};
	}
	virtual void parse(GSFileParser &gsf, const GameSet &gs) override {
	}
};

struct FinderAlias : ObjectFinder {
	int aliasIndex;
	virtual ObjectFinderResult eval(ScriptContext* ctx) override {
		auto &alias = ctx->gameState->aliases[aliasIndex];
		ObjectFinderResult res;
		for (auto &ref : alias)
			if (auto* obj = ref.getFrom(ctx->gameState); obj && obj->isInteractable())
				res.push_back(obj);
		return res;
	}
	virtual void parse(GSFileParser &gsf, const GameSet &gs) override {
		aliasIndex = gs.aliases.readIndex(gsf);
	}
	FinderAlias() {}
	FinderAlias(int aliasIndex) : aliasIndex(aliasIndex) {}
};

struct FinderController : ObjectFinder {
	virtual ObjectFinderResult eval(ScriptContext* ctx) override {
		return { ctx->getSelf()->getParent() };
	}
	virtual void parse(GSFileParser &gsf, const GameSet &gs) override {
	}
};

// WKB.exe's Object_Finder_Target (0x450880): the context's target, else the unit's current one
// (message 0x7d4); with neither the finder fails (returns false), it doesn't find an empty set
// (FinderFailsWhenEmpty, below)
struct FinderTarget : ObjectFinder {
	virtual ObjectFinderResult eval(ScriptContext* ctx) override {
		if (ctx->isServer()) {
			SrvScriptContext* sctx = (SrvScriptContext*)ctx;
			if (auto obj = sctx->get(ctx->target))
				return { obj };
			if (Order* order = sctx->getSelf()->orderConfig.getCurrentOrder())
				if (order->getCurrentTask()->isWorking()) // if the task is terminated/cancelled or not started, no target should be returned
					if (CommonGameObject* target = order->getCurrentTask()->target.get())
						return { target };
			return {};
		}
		else if(ctx->isClient()) {
			auto obj = ctx->get(ctx->target);
			if (obj) return { obj };
			else return {};
		}
		return {};
	}
	virtual void parse(GSFileParser &gsf, const GameSet &gs) override {
	}
};

// (ARE_ASSOCIATED, values.cpp: a FINDER_TARGET that found nothing failed, as in WKB.exe)
bool FinderFailsWhenEmpty(const ObjectFinder* finder)
{
	return dynamic_cast<const FinderTarget*>(finder) != nullptr;
}

// the simulation profiling test ("detail", g_actionProfile): the time per named finder
// (FINDER_RESULTS), with the finders it uses, ms
std::map<std::string, double> g_finderMs;
// the scripts' work, counted (not timed: the same on every computer): each candidate a finder
// evaluates a condition or grade for, each action run (ActionSequence::run). The players'
// commands are spread over steps by it (Server::issueCommand).
uint64_t g_scriptWork = 0;

struct FinderResults : ObjectFinder {
	int ofd;
	virtual ObjectFinderResult eval(ScriptContext* ctx) override {
		if (ofd == -1) return {}; // TODO: Remove this after implementing exception handling
		extern bool g_actionProfile;
		if (g_actionProfile && ctx->isServer()) {
			const uint64_t start = SDL_GetPerformanceCounter();
			ObjectFinderResult r = ctx->gameState->gameSet->objectFinderDefinitions[ofd]->eval(ctx);
			g_finderMs[ctx->gameState->gameSet->objectFinderDefinitions.names.getString(ofd)] +=
				(double)(SDL_GetPerformanceCounter() - start) * 1000.0 / (double)SDL_GetPerformanceFrequency();
			return r;
		}
		return ctx->gameState->gameSet->objectFinderDefinitions[ofd]->eval(ctx);
	}
	virtual void parse(GSFileParser &gsf, const GameSet &gs) override {
		ofd = gs.objectFinderDefinitions.readIndex(gsf);
	}
};

struct FinderAssociates : ObjectFinder {
	int category;
	virtual ObjectFinderResult eval(ScriptContext* ctx) override {
		if (!ctx->isServer()) return ClientAssociations(ctx, category, false);
		SrvScriptContext* sctx = (SrvScriptContext*)ctx;
		const auto &set = sctx->getSelf()->associates[category];
		ObjectFinderResult vec;
		for (auto& ref : set)
			if (ref && ref->isInteractable())
				vec.push_back(ref);
		return vec;
	}
	virtual void parse(GSFileParser &gsf, const GameSet &gs) override {
		category = gs.associations.readIndex(gsf);
	}
};

struct FinderAssociators : ObjectFinder {
	int category;
	virtual ObjectFinderResult eval(ScriptContext* ctx) override {
		if (!ctx->isServer()) return ClientAssociations(ctx, category, true);
		SrvScriptContext* sctx = (SrvScriptContext*)ctx;
		ServerGameObject* self = sctx->getSelf();
		const auto &set = self->associators[category];
		// in the order they registered, as WKB.exe lists them (ServerGameObject::associatorSeq)
		std::vector<std::pair<uint64_t, ServerGameObject*>> found;
		for (auto& ref : set)
			if (ref && ref->isInteractable()) {
				auto it = self->associatorSeq.find({ category, ref.objid });
				found.emplace_back(it != self->associatorSeq.end() ? it->second : 0, ref.get());
			}
		static const bool byId = getenv("WKB_ASSOCIATORS_BY_ID") != nullptr; // (the old order: test 124's before)
		if (!byId)
			std::stable_sort(found.begin(), found.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
		ObjectFinderResult vec;
		for (const auto& f : found)
			vec.push_back(f.second);
		return vec;
	}
	virtual void parse(GSFileParser &gsf, const GameSet &gs) override {
		category = gs.associations.readIndex(gsf);
	}
};

struct FinderPlayers : ObjectFinder {
	int equation;
	virtual ObjectFinderResult eval(ScriptContext* ctx) override {
		Server *server = Server::instance;
		ObjectFinderResult res;
		for (auto &it : server->getLevel()->children) {
			if (it.first.bpClass() == Tags::GAMEOBJCLASS_PLAYER)
				for (CommonGameObject *player : it.second)
					if (auto _ = ctx->change(ctx->candidate, player->dyncast<CommonGameObject>()))
						if (server->gameSet->equations[equation]->eval(ctx))
							res.push_back(player->dyncast<CommonGameObject>());
		}
		return res;
	}
	virtual void parse(GSFileParser &gsf, const GameSet &gs) override {
		equation = gs.equations.readIndex(gsf);
	}
};

struct FinderCandidate : ObjectFinder {
	virtual ObjectFinderResult eval(ScriptContext* ctx) override {
		auto obj = ctx->get(ctx->candidate);
		if (obj) return { obj };
		else return {};
	}
	virtual void parse(GSFileParser &gsf, const GameSet &gs) override {}
};

struct FinderCreator : ObjectFinder {
	virtual ObjectFinderResult eval(ScriptContext* ctx) override {
		auto obj = ctx->get(ctx->creator);
		if (obj) return { obj };
		else return {};
	}
	virtual void parse(GSFileParser &gsf, const GameSet &gs) override {}
};

struct FinderPackageSender : ObjectFinder {
	virtual ObjectFinderResult eval(ScriptContext* ctx) override {
		auto obj = ctx->get(ctx->packageSender);
		if (obj) return { obj };
		else return {};
	}
	virtual void parse(GSFileParser &gsf, const GameSet &gs) override {}
};

// FINDER_PACKAGE_RELATED_PARTY: the related party of the package being received
struct FinderPackageRelatedParty : ObjectFinder {
	virtual ObjectFinderResult eval(ScriptContext* ctx) override {
		ObjectFinderResult result;
		for (ServerGameObject* obj : GSPackage::currentRelatedParty)
			if (obj && !obj->deleted)
				result.push_back(obj);
		return result;
	}
	virtual void parse(GSFileParser &gsf, const GameSet &gs) override {}
};

struct FinderSequenceExecutor : ObjectFinder {
	virtual ObjectFinderResult eval(ScriptContext* ctx) override {
		auto obj = ctx->get(ctx->sequenceExecutor);
		if (obj) return { obj };
		else return {};
	}
	virtual void parse(GSFileParser &gsf, const GameSet &gs) override {}
};

struct FinderNearestToSatisfy : ObjectFinder {
	std::unique_ptr<ValueDeterminer> vdcond, vdradius;
	// The first way: every object on the tiles round it, in the tiles' order, the condition
	// evaluated for each one nearer than the nearest found so far (kept to check the one below)
	ObjectFinderResult evalAll(ScriptContext* ctx) {
		using AnyGO = CommonGameObject;
		float radius = vdradius->eval(ctx);
		const Vector3 center = ctx->getSelf()->position;
		NNSearch search;
		search.start(ctx->gameState, center, radius);
		AnyGO* nearest = nullptr;
		float nearestDist = radius * radius;
		const float now = ctx->isServer() && Server::instance ? Server::instance->timeManager.currentTime : 0.0f;
		while (AnyGO* obj = (AnyGO*)search.next()) {
			if (!obj->isInteractable()) continue;
			// (one no unit could walk to: Order.cpp, failedSearches)
			if (obj->unreachableUntil > now) continue;
			const float d = (obj->position - center).sqlen2xz();
			if (d > nearestDist || (nearest && d == nearestDist)) continue;
			auto _ = ctx->change(ctx->candidate, obj);
			if (vdcond->eval(ctx) > 0.0f) {
				nearest = obj;
				nearestDist = d;
			}
		}
		if (nearest)
			return { nearest };
		return {};
	}
	// The nearest object within the radius that satisfies the condition (only that one): the
	// objects within it sorted by distance (ties in the tiles' order, as the first way kept the
	// first one found), the condition evaluated from the nearest out until one satisfies it. The
	// same object as evalAll's, with fewer evaluations (each one evalAll made too): an army's
	// "Most appropriate Victim" evaluated it for every unit within 10 m of each soldier.
	// WKB_NEAREST_CHECK: each answer and the random numbers' state after it compared with evalAll's.
	virtual ObjectFinderResult eval(ScriptContext* ctx) override {
		static const bool check = getenv("WKB_NEAREST_CHECK") != nullptr;
		uint32_t randBefore = 0;
		if (check)
			randBefore = g_simRandom.state;
		using AnyGO = CommonGameObject;
		const float radius = vdradius->eval(ctx);
		const Vector3 center = ctx->getSelf()->position;
		struct Candidate { float d; uint32_t order; AnyGO* obj; };
		static thread_local std::vector<Candidate> near;
		const size_t base = near.size(); // (a condition may use this finder again: its own part)
		NNSearch search;
		search.start(ctx->gameState, center, radius);
		uint32_t order = 0;
		const float r2 = radius * radius;
		while (AnyGO* obj = (AnyGO*)search.next()) {
			if (!obj->isInteractable()) continue;
			const float d = (obj->position - center).sqlen2xz();
			if (d > r2) continue;
			near.push_back({ d, order++, obj });
		}
		std::sort(near.begin() + base, near.end(), [](const Candidate& a, const Candidate& b) { return a.d != b.d ? a.d < b.d : a.order < b.order; });
		AnyGO* found = nullptr;
		for (size_t i = base; i < near.size(); i++) {
			g_scriptWork++;
			auto _ = ctx->change(ctx->candidate, near[i].obj);
			if (vdcond->eval(ctx) > 0.0f) {
				found = near[i].obj;
				break;
			}
		}
		near.resize(base);
		if (check && ctx->isServer()) {
			static uint64_t calls = 0, wrong = 0;
			calls++;
			const uint32_t randNew = g_simRandom.state;
			g_simRandom.state = randBefore;
			ObjectFinderResult all = evalAll(ctx);
			AnyGO* old = all.empty() ? nullptr : all[0];
			if (old != found || g_simRandom.state != randNew) {
				if (wrong++ < 20)
					printf("NEAREST MISMATCH (call %llu): all %u, sorted %u; random state %s\n", (unsigned long long)calls,
						old ? old->id : 0u, found ? found->id : 0u, g_simRandom.state != randNew ? "different" : "the same");
			}
			if (calls % 10000 == 0)
				printf("NEAREST checked %llu, %llu different\n", (unsigned long long)calls, (unsigned long long)wrong);
			// (evalAll's state stands: the game as before)
			found = old;
		}
		if (found)
			return { found };
		return {};
	}
	virtual void parse(GSFileParser &gsf, const GameSet &gs) override {
		vdcond.reset(ReadValueDeterminer(gsf, gs));
		vdradius.reset(ReadValueDeterminer(gsf, gs));
	}
};

struct FinderLevel : ObjectFinder {
	virtual ObjectFinderResult eval(ScriptContext* ctx) override {
		return { ctx->gameState->getLevel() };
	}
	virtual void parse(GSFileParser &gsf, const GameSet &gs) override {}
};

struct FinderDisabledAssociates : ObjectFinder {
	int category;
	virtual ObjectFinderResult eval(ScriptContext* ctx) override {
		if (!ctx->isServer()) return {}; // TODO: Communicate disable count
		SrvScriptContext* sctx = (SrvScriptContext*)ctx;
		const auto& set = sctx->getSelf()->associates[category];
		ObjectFinderResult vec;
		for (auto& ref : set)
			if (ref && ref->disableCount > 0) // terminated?
				vec.push_back(ref);
		return vec;
	}
	virtual void parse(GSFileParser& gsf, const GameSet& gs) override {
		category = gs.associations.readIndex(gsf);
	}
};

struct FinderReferencers : ObjectFinder {
	int category;
	virtual ObjectFinderResult eval(ScriptContext* ctx) override {
		if (!ctx->isServer()) return ClientHasNoReferencers(ctx);
		SrvScriptContext* sctx = (SrvScriptContext*)ctx;
		// in the referencers' order, each once (a set of pointers came out in the order of their
		// addresses, which differ from run to run: the same game played differently)
		ObjectFinderResult vec;
		// (each once: looked for in the few found so far, a hash set only past 32. A set made for
		// every call, by millions in a battle of 10,000 and with nothing in it most times, took
		// most of the time: MSVC's allocates when made)
		std::unique_ptr<std::unordered_set<CommonGameObject*>> added;
		for (CommonGameObject* obj : sctx->getSelf()->referencers) {
			if (obj) {
				Order* order = ((ServerGameObject*)obj)->orderConfig.getCurrentOrder();
				if (!order) continue;
				Task* task = order->getCurrentTask();
				if (!task) continue;
				//assert(task->target.get() == ctx->getSelf());
				if (task->target != ctx->getSelf()) continue;
				if (task->blueprint->category != category) continue;
				if (vec.size() < 32) {
					if (std::find(vec.begin(), vec.end(), obj) == vec.end())
						vec.push_back(obj);
				}
				else {
					if (!added)
						added = std::make_unique<std::unordered_set<CommonGameObject*>>(vec.begin(), vec.end());
					if (added->insert(obj).second)
						vec.push_back(obj);
				}
			}
		}
		return vec;
	}
	virtual void parse(GSFileParser& gsf, const GameSet& gs) override {
		category = gs.taskCategories.readIndex(gsf);
	}
};

struct FinderOrderGiver : ObjectFinder {
	virtual ObjectFinderResult eval(ScriptContext* ctx) override {
		auto obj = ctx->get(ctx->orderGiver);
		if (obj) return { obj };
		else return {};
	}
	virtual void parse(GSFileParser& gsf, const GameSet& gs) override {}
};

struct FinderBeingTransferredToMe : ObjectFinder {
	std::unique_ptr<ObjectFinder> finder;
	virtual ObjectFinderResult eval(ScriptContext* ctx) override {
		return {};
	}
	virtual void parse(GSFileParser& gsf, const GameSet& gs) override {
		finder.reset(ReadFinder(gsf, gs));
	}
};

struct FinderCollisionSubject : ObjectFinder {
	virtual ObjectFinderResult eval(ScriptContext* ctx) override {
		auto obj = ctx->get(ctx->collisionSubject);
		if (obj) return { obj };
		else return {};
	}
	virtual void parse(GSFileParser& gsf, const GameSet& gs) override {}
};

struct FinderUser : ObjectFinder {
	virtual ObjectFinderResult eval(ScriptContext* ctx) override {
		auto* firstPlayerObj = ctx->gameState->findObject(1027);
		if (firstPlayerObj) {
			for (auto& [typeIndex, childList] : firstPlayerObj->children) {
				if (typeIndex.bpClass() == Tags::GAMEOBJCLASS_USER) {
					if (!childList.empty()) {
						return { childList[0] };
					}
				}
			}
		}
		return {};
	}
	virtual void parse(GSFileParser& gsf, const GameSet& gs) override {}
};

struct FinderSelectedObject : ObjectFinder {
	virtual ObjectFinderResult eval(ScriptContext* ctx) override {
		auto obj = ctx->get(ctx->selectedObject);
		if (obj) return { obj };
		else return {};
	}
	virtual void parse(GSFileParser& gsf, const GameSet& gs) override {}
};

struct FinderAgAllOfType : ObjectFinder {
	const GameObjBlueprint* blueprint;
	virtual ObjectFinderResult eval(ScriptContext* ctx) override {
		const auto bpIndex = GameObjBlueprintIndex(blueprint);
		ObjectFinderResult vec;
		auto walk = [bpIndex, &vec](CommonGameObject* obj, auto& rec) -> void {
			for (const auto& subords : obj->children) {
				if (subords.first == bpIndex) {
					for (CommonGameObject* sub : subords.second) {
						vec.push_back((CommonGameObject*)sub);
					}
				}
				else {
					for (CommonGameObject* sub : subords.second) {
						rec(sub, rec);
					}
				}
			}
		};
		walk(Server::instance->getLevel(), walk);
		return vec;
	}
	virtual void parse(GSFileParser& gsf, const GameSet& gs) override {
		blueprint = gs.readObjBlueprintPtr(gsf);
	}
};

struct FinderAgAsObj : ObjectFinder {
	int objId;
	std::unique_ptr<ObjectFinder> finder;

	virtual ObjectFinderResult eval(ScriptContext* ctx) override {
		ServerGameObject* obj = Server::instance->findObject(objId);
		if (!obj) return {};
		auto _ = ctx->changeSelf(obj);
		return finder->eval(ctx);
	}
	virtual void parse(GSFileParser& gsf, const GameSet& gs) override {
		objId = gsf.nextInt();
		finder.reset(ReadFinder(gsf, gs));
	}
};

// CHAIN_ORIGINAL_SELF (WKB.exe 0x448890, mods): the self the enclosing FINDER_CHAIN started from,
// else self
struct FinderChainOriginalSelf : ObjectFinder {
	virtual ObjectFinderResult eval(ScriptContext* ctx) override {
		CommonGameObject* obj = ctx->get(ctx->chainOriginalSelf);
		if (!obj)
			obj = ctx->getSelf();
		if (obj) return { obj };
		return {};
	}
	virtual void parse(GSFileParser& gsf, const GameSet& gs) override {}
};

// WITHIN_HORIZ_DISTANCE <distance> (mods): the objects within that distance of self on the ground,
// heights left out, but self and armies. WKB.exe's (0x451e50) compares only the x coordinates
// and so finds a strip across the map; wkbai does what the name says.
struct FinderWithinHorizDistance : ObjectFinder {
	std::unique_ptr<ValueDeterminer> vdDistance;
	virtual ObjectFinderResult eval(ScriptContext* ctx) override {
		CommonGameObject* self = ctx->getSelf();
		const float d = vdDistance->eval(ctx);
		if (!self || d <= 0.0f)
			return {};
		NNSearch search;
		search.start(ctx->gameState, self->position, d);
		ObjectFinderResult res;
		while (CommonGameObject* obj = (CommonGameObject*)search.next())
			if (obj != self && obj->isInteractable() && obj->blueprint->bpClass != Tags::GAMEOBJCLASS_ARMY
				&& (obj->position - self->position).sqlen2xz() <= d * d)
				res.push_back(obj);
		return res;
	}
	virtual void parse(GSFileParser& gsf, const GameSet& gs) override { vdDistance.reset(ReadValueDeterminer(gsf, gs)); }
};

ObjectFinder *ReadFinder(GSFileParser &gsf, const GameSet &gs)
{
	std::string strtag = gsf.nextString();
	if (strtag.substr(0, 7) != "FINDER_") {
		printf("WARNING: Invalid finder %s at %s\n", strtag.c_str(), gsf.locate().c_str());
		return new FinderUnknown(strtag); // no need to call parse for unknown finders
	}
	std::string findername = strtag.substr(7);
	ObjectFinder *finder;
	switch (Tags::FINDER_tagDict.getTagID(findername.c_str())) {
	case Tags::FINDER_SELF: finder = new FinderSelf; break;
	case Tags::FINDER_SPECIFIC_ID: finder = new FinderSpecificId; break;
	case Tags::FINDER_PLAYER: finder = new FinderPlayer; break;
	case Tags::FINDER_ALIAS: finder = new FinderAlias; break;
	case Tags::FINDER_CONTROLLER: finder = new FinderController; break;
	case Tags::FINDER_TARGET: finder = new FinderTarget; break;
	case Tags::FINDER_RESULTS: finder = new FinderResults; break;
	case Tags::FINDER_ASSOCIATES: finder = new FinderAssociates; break;
	case Tags::FINDER_ASSOCIATORS: finder = new FinderAssociators; break;
	case Tags::FINDER_PLAYERS: finder = new FinderPlayers; break;
	case Tags::FINDER_CANDIDATE: finder = new FinderCandidate; break;
	case Tags::FINDER_CREATOR: finder = new FinderCreator; break;
	case Tags::FINDER_PACKAGE_SENDER: finder = new FinderPackageSender; break;
	case Tags::FINDER_PACKAGE_RELATED_PARTY: finder = new FinderPackageRelatedParty; break;
	case Tags::FINDER_SEQUENCE_EXECUTOR: finder = new FinderSequenceExecutor; break;
	case Tags::FINDER_NEAREST_TO_SATISFY: finder = new FinderNearestToSatisfy; break;
	case Tags::FINDER_LEVEL: finder = new FinderLevel; break;
	case Tags::FINDER_DISABLED_ASSOCIATES: finder = new FinderDisabledAssociates; break;
	case Tags::FINDER_REFERENCERS: finder = new FinderReferencers; break;
	case Tags::FINDER_ORDER_GIVER: finder = new FinderOrderGiver; break;
	case Tags::FINDER_BEING_TRANSFERRED_TO_ME: finder = new FinderBeingTransferredToMe; break;
	case Tags::FINDER_COLLISION_SUBJECT: finder = new FinderCollisionSubject; break;
	case Tags::FINDER_USER: finder = new FinderUser; break;
	case Tags::FINDER_SELECTED_OBJECT: finder = new FinderSelectedObject; break;
	default:
		if (findername == "AG_ALL_OF_TYPE") { finder = new FinderAgAllOfType; break; }
		if (findername == "AG_AS_OBJ") { finder = new FinderAgAsObj; break; }
		if (findername == "CHAIN_ORIGINAL_SELF") { finder = new FinderChainOriginalSelf; break; }
		if (findername == "WITHIN_HORIZ_DISTANCE") { finder = new FinderWithinHorizDistance; break; }
		if (const char* signature = FindStubSignature(finderStubSignatures, findername)) {
			finder = new FinderStub(strtag, signature);
			printf("WARNING: Finder %s is not implemented (arguments read, finds nothing) at %s\n", strtag.c_str(), gsf.locate().c_str());
			break;
		}
		finder = new FinderUnknown(strtag);
		printf("WARNING: Unknown finder %s at %s\n", strtag.c_str(), gsf.locate().c_str());
		break;
	}
	finder->parse(gsf, gs);
	return finder;
}

struct FinderSubordinates : ObjectFinder {
	std::unique_ptr<ObjectFinder> finder;
	int equation = -1;
	int bpclass = -1;
	const GameObjBlueprint *objbp = nullptr;
	bool immediateLevel = false;
	static thread_local ObjectFinderResult results;
	bool eligible(CommonGameObject *obj, ScriptContext *ctx) {
		if (!obj->isInteractable()) return false;
		if (!objbp || (objbp == obj->blueprint)) {
			if ((bpclass == -1) || (bpclass == obj->blueprint->bpClass)) {
				auto _ = ctx->change(ctx->candidate, obj);
				if ((equation == -1) || (Server::instance->gameSet->equations[equation]->eval(ctx) > 0.0f))
					return true;
			}
		}
		return false;
	}
	void walk(CommonGameObject *obj, ScriptContext* ctx) {
		for (auto &it : obj->children) {
			for (CommonGameObject* cchild : it.second) {
				CommonGameObject* child = cchild->dyncast<CommonGameObject>();
				if (eligible(child, ctx))
					results.push_back(child);
				if (!immediateLevel)
					walk(child, ctx);
			}
		}
	}
	virtual ObjectFinderResult eval(ScriptContext* ctx) override {
		auto vec = finder->eval(ctx);
		results.clear();
		for (CommonGameObject *par : vec) {
			static const auto scl = { Tags::GAMEOBJCLASS_BUILDING, Tags::GAMEOBJCLASS_CHARACTER, Tags::GAMEOBJCLASS_CONTAINER, Tags::GAMEOBJCLASS_MARKER, Tags::GAMEOBJCLASS_PROP };
			if (std::find(scl.begin(), scl.end(), par->blueprint->bpClass) != scl.end()) {
				if (eligible(par, ctx))
					results.push_back(par);
			}
			walk(par, ctx);
		}
		return std::move(results);
	}
	virtual void parse(GSFileParser &gsf, const GameSet &gs) override {
		std::string bpname;
		while (!gsf.eol) {
			auto arg = gsf.nextString();
			if (arg == "BY_BLUEPRINT")
				bpname = gsf.nextString(true);
			else if (arg == "BY_EQUATION")
				equation = gs.equations.readIndex(gsf);
			else if (arg == "OF_CLASS")
				bpclass = Tags::GAMEOBJCLASS_tagDict.getTagID(gsf.nextString().c_str());
			else if (arg == "IMMEDIATE_LEVEL")
				immediateLevel = true;
		}
		if (bpname != "" && bpclass != -1)
			objbp = gs.findBlueprint(bpclass, bpname);
		finder.reset(ReadFinderNode(gsf, gs));
	}
};

thread_local decltype(FinderSubordinates::results) FinderSubordinates::results; // this could be avoided using "inline" static members from C++17

struct FinderNSubs : ObjectFinder {
	DynArray<std::unique_ptr<ObjectFinder>> finders;
	virtual void parse(GSFileParser &gsf, const GameSet &gs) override {
		finders.resize(gsf.nextInt());
		for (auto &finder : finders)
			finder.reset(ReadFinderNode(gsf, gs));
	}
};

struct FinderUnion : FinderNSubs {
	virtual ObjectFinderResult eval(ScriptContext* ctx) override {
		// in the order found, each once (not a hash set's order of addresses: see FinderReferencers)
		ObjectFinderResult result;
		std::unordered_set<CommonGameObject*> set;
		for (auto &finder : finders)
			for (CommonGameObject* obj : finder->eval(ctx))
				if (set.insert(obj).second)
					result.push_back(obj);
		return result;
	}
};

struct FinderIntersection : FinderNSubs {
	virtual ObjectFinderResult eval(ScriptContext* ctx) override {
		// in the first finder's order (sorted by address, it differed from run to run)
		auto objs = finders[0]->eval(ctx);
		for (size_t f = 1; f < finders.size() && !objs.empty(); ++f) {
			auto next = finders[f]->eval(ctx);
			const std::unordered_set<CommonGameObject*> in(next.begin(), next.end());
			ObjectFinderResult kept;
			for (CommonGameObject* o : objs)
				if (in.count(o))
					kept.push_back(o);
			objs = std::move(kept);
		}
		return objs;
	}
};

struct FinderChain : FinderNSubs {
	virtual ObjectFinderResult eval(ScriptContext* ctx) override {
		auto _ = ctx->change(ctx->chainOriginalSelf, ctx->getSelf());
		ObjectFinderResult vec = { ctx->getSelf() };
		for (auto &finder : finders) {
			auto _ = ctx->changeSelf(vec[0]);
			vec = finder->eval(ctx);
			if (vec.empty())
				break;
		}
		return vec;
	}
};

struct FinderAlternative : FinderNSubs {
	virtual ObjectFinderResult eval(ScriptContext* ctx) override {
		for (auto &finder : finders) {
			auto vec = finder->eval(ctx);
			if (!vec.empty())
				return vec;
		}
		return {};
	}
};

struct FinderFilterFirst : ObjectFinder {
	int equation;
	std::unique_ptr<ValueDeterminer> count;
	std::unique_ptr<ObjectFinder> finder;
	virtual ObjectFinderResult eval(ScriptContext* ctx) override {
		auto vec = finder->eval(ctx);
		int limit = (int)count->eval(ctx), num = 0;
		decltype(vec) res;
		for (CommonGameObject *obj : vec) {
			auto _ = ctx->change(ctx->candidate, obj);
			if (ctx->gameState->gameSet->equations[equation]->eval(ctx) > 0.0f) {
				res.push_back(obj);
				if (++num >= limit)
					break;
			}
		}
		return res;
	}
	virtual void parse(GSFileParser &gsf, const GameSet &gs) override {
		equation = gs.equations.readIndex(gsf);
		count.reset(ReadValueDeterminer(gsf, gs));
		finder.reset(ReadFinderNode(gsf, gs));
	}
};

struct FinderFilter : ObjectFinder {
	int equation;
	std::unique_ptr<ObjectFinder> finder;
	virtual ObjectFinderResult eval(ScriptContext* ctx) override {
		auto vec = finder->eval(ctx);
		g_scriptWork += vec.size();
		decltype(vec) res;
		for (CommonGameObject *obj : vec) {
			auto _ = ctx->change(ctx->candidate, obj);
			if (ctx->gameState->gameSet->equations[equation]->eval(ctx) > 0.0f) {
				res.push_back(obj);
			}
		}
		return res;
	}
	virtual void parse(GSFileParser &gsf, const GameSet &gs) override {
		equation = gs.equations.readIndex(gsf);
		finder.reset(ReadFinderNode(gsf, gs));
	}
};

struct FinderMetreRadius : ObjectFinder {
	std::unique_ptr<ValueDeterminer> vdradius;
	// FINDER_METRE_RADIUS <radius> [<finder>] <terms>: the search centre, else self ("... CONSTANT 10
	// FINDER_SELF ALLIED_UNITS" in Guard.cpp and Skirmish.cpp)
	std::unique_ptr<ObjectFinder> fCentre;
	bool useOriginalSelf = false;
	int relationship = 0;
	int classFilter = 0;

	template<typename AnyGO> bool eligible(AnyGO* obj, AnyGO* refplayer, ScriptContext* ctx) {
		if (!obj->isInteractable()) return false;
		AnyGO* objplayer = obj->getPlayer();
		// friend or foe needs both players (a skirmish's empty position: its player was removed
		// and its objects, not freed yet, have none; the crash with every AI General kicked)
		if ((relationship == 2 || relationship == 3) && (!objplayer || !refplayer))
			return false;
		switch (relationship) {
		case 1: if (objplayer != refplayer) return false; break;
		case 2: if (objplayer != refplayer && ctx->gameState->getDiplomaticStatus(objplayer, refplayer) >= 1) return false; break;
		case 3: if (objplayer == refplayer || ctx->gameState->getDiplomaticStatus(objplayer, refplayer) < 2) return false; break;
		default:;
		}
		int objclass = obj->blueprint->bpClass;
		// WKB.exe's radius search doesn't find markers (it has no MARKERS_ONLY term): in the
		// original's 30 s from its Marjoria's Keep save, "Pecked by Crow" created all 8 due crow
		// markers, so "Less than 2 Crow Markers Within 50m" never counted the ones beside it;
		// counting them held wkbre2 to 3
		if (objclass == Tags::GAMEOBJCLASS_MARKER && classFilter != 4)
			return false;
		switch (classFilter) {
		case 1: if (objclass != Tags::GAMEOBJCLASS_BUILDING) return false; break;
		case 2: if (objclass != Tags::GAMEOBJCLASS_CHARACTER) return false; break;
		case 3: if (objclass != Tags::GAMEOBJCLASS_BUILDING && objclass != Tags::GAMEOBJCLASS_CHARACTER) return false; break;
		case 4: if (objclass != Tags::GAMEOBJCLASS_MARKER) return false; break;
		case 5: if (objclass != Tags::GAMEOBJCLASS_CONTAINER) return false; break; // trees and other resources
		default:;
		}
		return true;
	}
	virtual ObjectFinderResult eval(ScriptContext* ctx) override {
		using AnyGO = CommonGameObject;
		float radius = vdradius->eval(ctx);
		AnyGO* player = (useOriginalSelf ? ctx->get(ctx->chainOriginalSelf) : ctx->getSelf())->getPlayer();
		Vector3 centre = ctx->getSelf()->position;
		if (fCentre) {
			AnyGO* first = fCentre->getFirst(ctx);
			if (!first)
				return {};
			centre = first->position;
		}
		NNSearch search;
		search.start(ctx->gameState, centre, radius);
		ObjectFinderResult res;
		while (AnyGO* obj = (AnyGO*)search.next())
			if (eligible(obj, player, ctx))
				res.push_back(obj);
		return res;
	}
	virtual void parse(GSFileParser &gsf, const GameSet &gs) override {
		vdradius.reset(ReadValueDeterminer(gsf, gs));
		// ...
		auto cursor = gsf.cursor;
		std::string word = gsf.nextString();
		if (word.rfind("FINDER_", 0) == 0) {
			gsf.cursor = cursor;
			fCentre.reset(ReadFinderNode(gsf, gs));
			word = gsf.nextString();
		}
		while (!word.empty()) {
			if (word == "SAME_PLAYER_UNITS") relationship = 1;
			else if (word == "ALLIED_UNITS") relationship = 2;
			else if (word == "ENEMY_UNITS") relationship = 3;
			else if (word == "ORIGINAL_SAME_PLAYER_UNITS") { useOriginalSelf = true; relationship = 1; }
			else if (word == "ORIGINAL_ALLIED_UNITS") { useOriginalSelf = true; relationship = 2; }
			else if (word == "ORIGINAL_ENEMY_UNITS") { useOriginalSelf = true; relationship = 3; }
			else if (word == "BUILDINGS_ONLY") classFilter = 1;
			else if (word == "CHARACTERS_ONLY") classFilter = 2;
			else if (word == "CHARACTERS_AND_BUILDINGS_ONLY") classFilter = 3;
			else if (word == "MARKERS_ONLY") classFilter = 4;
			else if (word == "CONTAINER_ONLY") classFilter = 5;
			else printf("Unknown METRE_RADIUS term %s\n", word.c_str());
			word = gsf.nextString();
		}
	}
};

struct FinderGradeSelect : ObjectFinder {
	bool byHighest;
	int equation;
	std::unique_ptr<ValueDeterminer> vdCount;
	std::unique_ptr<ObjectFinder> finder;
	virtual ObjectFinderResult eval(ScriptContext* ctx) override {
		int count = 0;
		if (vdCount)
			count = (int)vdCount->eval(ctx);
		auto vec = finder->eval(ctx);
		g_scriptWork += vec.size();
		ValueDeterminer *vd = Server::instance->gameSet->equations[equation];

		std::vector<std::pair<float,CommonGameObject*>> values(vec.size());
		bool nan = false;
		for (size_t i = 0; i < vec.size(); i++) {
			auto _ = ctx->change(ctx->candidate, vec[i]);
			values[i] = { vd->eval(ctx), vec[i] };
			nan |= values[i].first != values[i].first;
		}
		// the best one only (an army soldier's victim among hundreds): the first best, as the
		// stable sort's first (but for a NaN, which the sort may place anywhere)
		if (count == 1 && vec.size() > 1 && !nan) {
			size_t best = 0;
			for (size_t i = 1; i < values.size(); i++)
				if (byHighest ? values[i].first > values[best].first : values[i].first < values[best].first)
					best = i;
			return { values[best].second };
		}
		std::stable_sort(values.begin(), values.end(), [this](auto & a, auto & b) {return byHighest ? (a.first > b.first) : (a.first < b.first); });
		if (count <= 0 || (size_t)count > vec.size())
			count = vec.size();
		ObjectFinderResult res;
		res.reserve(count);
		for (int i = 0; i < count; i++)
			res.push_back(values[i].second);
		return res;
	}
	virtual void parse(GSFileParser &gsf, const GameSet &gs) override {
		auto arg = gsf.nextString();
		if (arg == "BY_HIGHEST")
			byHighest = true;
		else if (arg == "BY_LOWEST")
			byHighest = false;
		else
			ferr("Invalid grading %s", arg.c_str());
		equation = gs.equations.readIndex(gsf);
		if (!gsf.eol)
			vdCount.reset(ReadValueDeterminer(gsf, gs));
		finder.reset(ReadFinderNode(gsf, gs));
	}
};

struct FinderNearestCandidate : ObjectFinder {
	std::unique_ptr<ObjectFinder> finder;
	virtual ObjectFinderResult eval(ScriptContext* ctx) override {
		auto vec = finder->eval(ctx);
		float bestDist = INFINITY;
		CommonGameObject *bestObj = nullptr;
		for (CommonGameObject *obj : vec) {
			float dist = (obj->position - ctx->getSelf()->position).sqlen2xz();
			if (dist < bestDist) {
				bestDist = dist;
				bestObj = obj;
			}
		}
		if (bestObj)
			return { bestObj };
		else
			return {};
	}
	virtual void parse(GSFileParser &gsf, const GameSet &gs) override {
		finder.reset(ReadFinderNode(gsf, gs));
	}
};

struct FinderTileRadius : ObjectFinder {
	std::unique_ptr<ValueDeterminer> vdradius;
	virtual ObjectFinderResult eval(ScriptContext* ctx) override {
		using AnyGO = CommonGameObject;
		float radius = vdradius->eval(ctx);
		NNSearch search;
		search.start(ctx->gameState, ctx->getSelf()->position, radius);
		ObjectFinderResult res;
		while (AnyGO* obj = (AnyGO*)search.next())
			if (obj->isInteractable())
				res.push_back(obj);
		return res;
	}
	virtual void parse(GSFileParser& gsf, const GameSet& gs) override {
		vdradius.reset(ReadValueDeterminer(gsf, gs));
	}
};

struct FinderDiscoveredUnits : ObjectFinder {
	virtual ObjectFinderResult eval(ScriptContext* ctx) override {
		// TODO
		return {};
	}
	virtual void parse(GSFileParser& gsf, const GameSet& gs) override {}
};

struct FinderRandomSelection : ObjectFinder {
	std::unique_ptr<ValueDeterminer> vCount;
	std::unique_ptr<ObjectFinder> finder;
	virtual ObjectFinderResult eval(ScriptContext* ctx) override {
		auto vec = finder->eval(ctx);
		size_t cnt = std::min(vec.size(), (size_t)vCount->eval(ctx));
		for (size_t i = 0; i < cnt; i++) {
			size_t s = ScriptRand(ctx) % (vec.size() - i) + i;
			std::swap(vec[i], vec[s]);
		}
		vec.resize(cnt);
		return vec;
	}
	virtual void parse(GSFileParser& gsf, const GameSet& gs) override {
		vCount.reset(ReadValueDeterminer(gsf, gs));
		finder.reset(ReadFinderNode(gsf, gs));
	}
};

// FILTER_CANDIDATES <condition>, the source on the next line: the candidates for which it is
// above 0; CULL_CANDIDATES (cull = true, WKB.exe 0x448cb0, mods) keeps the others
struct FinderFilterCandidates : ObjectFinder {
	bool cull = false;
	std::unique_ptr<ValueDeterminer> condition;
	std::unique_ptr<ObjectFinder> finder;
	FinderFilterCandidates(bool cull = false) : cull(cull) {}
	virtual ObjectFinderResult eval(ScriptContext* ctx) override {
		auto vec = finder->eval(ctx);
		g_scriptWork += vec.size();
		decltype(vec) res;
		for (CommonGameObject* obj : vec) {
			auto _ = ctx->change(ctx->candidate, obj);
			if ((condition->eval(ctx) > 0.0f) != cull) {
				res.push_back(obj);
			}
		}
		return res;
	}
	virtual void parse(GSFileParser& gsf, const GameSet& gs) override {
		condition.reset(ReadValueDeterminer(gsf, gs));
		finder.reset(ReadFinderNode(gsf, gs));
	}
};

// GRADE_SELECT_CANDIDATES BY_HIGHEST|BY_LOWEST <grade value> [<count value>], the source on the
// next line (WKB.exe 0x44b0e0): the count best of the source by the grade, each graded as the
// candidate, best first (the first wins a tie); all of them, in their order, when there are no
// more than the count; none with a count of 0
struct FinderGradeSelectCandidates : ObjectFinder {
	bool byHighest = true;
	std::unique_ptr<ValueDeterminer> vdGrade, vdCount;
	std::unique_ptr<ObjectFinder> finder;
	virtual ObjectFinderResult eval(ScriptContext* ctx) override {
		auto vec = finder->eval(ctx);
		const int count = vdCount ? (int)vdCount->eval(ctx) : (int)vec.size();
		g_scriptWork += vec.size();
		if (count <= 0)
			return {};
		if ((size_t)count >= vec.size())
			return vec;
		std::vector<std::pair<float, CommonGameObject*>> graded(vec.size());
		for (size_t i = 0; i < vec.size(); i++) {
			auto _ = ctx->change(ctx->candidate, vec[i]);
			graded[i] = { vdGrade->eval(ctx), vec[i] };
		}
		std::stable_sort(graded.begin(), graded.end(), [this](auto& a, auto& b) { return byHighest ? a.first > b.first : a.first < b.first; });
		ObjectFinderResult res;
		for (int i = 0; i < count; i++)
			res.push_back(graded[i].second);
		return res;
	}
	virtual void parse(GSFileParser& gsf, const GameSet& gs) override {
		const std::string mode = gsf.nextString();
		byHighest = mode != "BY_LOWEST";
		vdGrade.reset(ReadValueDeterminer(gsf, gs));
		if (!gsf.eol)
			vdCount.reset(ReadValueDeterminer(gsf, gs));
		finder.reset(ReadFinderNode(gsf, gs));
	}
};

ObjectFinder *ReadFinderNode(::GSFileParser &gsf, const ::GameSet &gs)
{
	gsf.advanceLine();
	while (!gsf.eof) {
		const char *oldcur = gsf.cursor;
		std::string strtag = gsf.nextTag();
		if (strtag == "END_OBJECT_FINDER_DEFINITION")
			return new FinderUnknown("<empty OBJECT_FINDER_DEFINITION>");
		else if(strtag.substr(0, 7) == "FINDER_") {
			//if (strtag.substr(0, 7) != "FINDER_")
			//	return new FinderUnknown; // no need to call parse for unknown finders
			ObjectFinder *finder;
			switch (Tags::FINDER_tagDict.getTagID(strtag.substr(7).c_str())) {
			case Tags::FINDER_SUBORDINATES: finder = new FinderSubordinates; break;
			case Tags::FINDER_UNION: finder = new FinderUnion; break;
			case Tags::FINDER_INTERSECTION: finder = new FinderIntersection; break;
			case Tags::FINDER_CHAIN: finder = new FinderChain; break;
			case Tags::FINDER_ALTERNATIVE: finder = new FinderAlternative; break;
			case Tags::FINDER_FILTER_FIRST: finder = new FinderFilterFirst; break;
			case Tags::FINDER_FILTER: finder = new FinderFilter; break;
			case Tags::FINDER_METRE_RADIUS: finder = new FinderMetreRadius; break;
			case Tags::FINDER_GRADE_SELECT: finder = new FinderGradeSelect; break;
			case Tags::FINDER_NEAREST_CANDIDATE: finder = new FinderNearestCandidate; break;
			case Tags::FINDER_TILE_RADIUS: finder = new FinderTileRadius; break;
			case Tags::FINDER_DISCOVERED_UNITS: finder = new FinderDiscoveredUnits; break;
			case Tags::FINDER_RANDOM_SELECTION: finder = new FinderRandomSelection; break;
			case Tags::FINDER_FILTER_CANDIDATES: finder = new FinderFilterCandidates; break;
			default:
				if (strtag == "FINDER_GRADE_SELECT_CANDIDATES") { finder = new FinderGradeSelectCandidates; break; }
				if (strtag == "FINDER_CULL_CANDIDATES") { finder = new FinderFilterCandidates(true); break; }
				gsf.cursor = oldcur;
				return ReadFinder(gsf, gs);
			}
			finder->parse(gsf, gs);
			return finder;
		}
		gsf.advanceLine();
	}
	ferr("Object finder definition reached end of file without END_OBJECT_FINDER_DEFINITION!");
	return nullptr;
}
