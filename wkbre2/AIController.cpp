#include "AIController.h"
#include "server.h"
#include "gameset/ScriptContext.h"
#include "gameset/Plan.h"
#include "gameset/gameset.h"
#include "util/GSFileParser.h"
#include "gameset/finder.h"
#include "Order.h"
#include "gameset/command.h"
#include <numeric>

// Whether the building offers "Spawn <type>" for a fighter (not a peasant, cart, merchant,
// priest, scout or wagon): the Barracks and Stables, not the Manor or a Church
static bool TrainsFighters(const GameObjBlueprint* building)
{
	static std::map<const GameObjBlueprint*, bool> known;
	if (auto it = known.find(building); it != known.end())
		return it->second;
	static const char* civilians[] = { "Peasant", "Cart", "Merchant", "Priest", "Bishop", "Inquisitor", "Scout", "Wagon" };
	const GameSet* gs = Server::instance->gameSet.get();
	bool fighters = false;
	for (const Command* cmd : building->offeredCommands) {
		const std::string& name = gs->commands.getString(cmd);
		if (name.rfind("Spawn ", 0) == 0 && std::none_of(std::begin(civilians), std::end(civilians), [&](const char* c) { return name.find(c) != std::string::npos; }))
			fighters = true;
	}
	return known[building] = fighters;
}

// a building peasants farm (the Kingmaker's "Farm Wood", the desert farm): food comes from it
static bool ProducesFood(const GameObjBlueprint* building)
{
	return building->name.rfind("Farm", 0) == 0 || building->name.find(" Farm") != std::string::npos;
}

void AIController::parse(GSFileParser& gsf, const GameSet& gs)
{
	gsf.advanceLine();
	while (!gsf.eof) {
		std::string strtag = gsf.nextTag();
		if (strtag == "END_AI_CONTROLLER")
			break;
		else if (strtag == "MASTER_PLAN") {
			activatePlan(gs.plans.readIndex(gsf));
		}
		else if (strtag == "COMMISSION") {
			// COMMISSION "<name>" <handler id>, then its state and requirements up to
			// END_COMMISSION. wkbre2 works the requirements out again each update, so only
			// FORCE_BALANCE and the building requirements' FOUNDATIONs under construction are kept
			// (building requirements in the blueprint's order). The original's saves have the AI's
			// running commissions here: without them a loaded AI spawned no soldiers until its
			// scripts commissioned new ones.
			const GSCommission* commission = gs.commissions.readPtr(gsf);
			ServerGameObject* handler = Server::instance->findObject(gsf.nextInt());
			CommissionInstance* ci = nullptr;
			if (commission && handler) {
				activateCommission(commission, handler);
				ci = &commissionInstances.back();
			}
			int buildingReq = -1;
			gsf.advanceLine();
			while (!gsf.eof) {
				const std::string tag = gsf.nextTag();
				if (tag == "END_COMMISSION")
					break;
				if (ci && tag == "FORCE_BALANCE")
					ci->forceBalance = true;
				else if (tag == "REQUIREMENT") {
					if (gsf.nextString() == "BUILDING_REQUIREMENT")
						buildingReq++;
				}
				else if (ci && tag == "FOUNDATION" && buildingReq >= 0 && buildingReq < (int)ci->buildingReqInsts.size()) {
					if (ServerGameObject* foundation = Server::instance->findObject(gsf.nextInt()))
						ci->buildingReqInsts[buildingReq].foundations.emplace_back(foundation);
				}
				gsf.advanceLine();
			}
		}
		else if (strtag == "WORK_ORDER") {
			// WKBattles work orders
			ServerGameObject* city = Server::instance->findObject(gsf.nextInt());
			ObjectFinder* unitFinder = gs.objectFinderDefinitions.readRef(gsf);
			const WorkOrder* workOrder = gs.workOrders.readPtr(gsf);
			registerWorkOrder(city, unitFinder, workOrder);
			// normally a block ending with END_WORK_ORDER comes afterwards, but apparently it is empty, no need to parse
		}
		// TODO: SETTLEMENT_CONTROLLER for WK1 work orders
		gsf.advanceLine();
	}
}

void AIController::update()
{
	// ---- Plan ----
	if (planBlueprint) {
		SrvScriptContext ctx{ Server::instance, gameObj };
		bool done = planBlueprint->execute(&planState, &ctx);
		if (done) {
			planBlueprint = nullptr;
			planState.nodeStates.clear();
		}
	}

	// ---- Work Orders ----
	// A unit can be listed by several work orders (a town's and a city's workforce, while peasants
	// move between settlements): each unit works for one, the one that had it before if that still
	// lists it, else the first, so that two work orders don't take turns restarting its order.
	std::vector<SrvFinderResult> unitLists(workOrderInstances.size());
	std::map<uint32_t, std::pair<size_t, bool>> owner; // unit id -> (work order index, kept from before)
	for (size_t wi = 0; wi < workOrderInstances.size(); wi++) {
		auto& woi = workOrderInstances[wi];
		if (!woi.city)
			continue;
		SrvScriptContext ctx{ Server::instance, woi.city };
		unitLists[wi] = woi.unitFinder->eval(&ctx);
		for (ServerGameObject* unit : unitLists[wi]) {
			auto prev = unitWorkOrder.find(unit->id);
			const bool kept = prev != unitWorkOrder.end() && prev->second == std::make_pair(woi.city->id, woi.workOrder);
			auto [it, added] = owner.try_emplace(unit->id, wi, kept);
			if (!added && kept && !it->second.second)
				it->second = { wi, true };
		}
	}
	for (size_t wi = 0; wi < workOrderInstances.size(); wi++) {
		auto& list = unitLists[wi];
		list.resize(std::remove_if(list.begin(), list.end(), [&](ServerGameObject* unit) { return owner[unit->id].first != wi; }) - list.begin());
	}
	// The aggressive AI lends farmers (not in WKB.exe): a settlement whose farms have no peasants
	// gets one per farm from the settlement with the most, which keeps at least half. The farms
	// mostly belong to towns whose workforce is empty, and the scripts' peasant transfers didn't
	// fill them: at 25 minutes Ragnar had 18 farms and 4 farmers, his city 37 peasants on wood, 17
	// food and 2,254 wood.
	if (Server::instance->aggressiveAI && Server::instance->aiLendFarmers) {
		auto farmJobs = [&](size_t wi) -> size_t {
			auto& woi = workOrderInstances[wi];
			SrvScriptContext ctx{ Server::instance, woi.city };
			size_t n = 0;
			for (const auto& asg : woi.workOrder->assignments)
				if (asg.order && Server::instance->gameSet->orders.getString(asg.order) == "Farm" && asg.distFinder)
					n += asg.distFinder->eval(&ctx).size();
			return n;
		};
		for (size_t wi = 0; wi < workOrderInstances.size(); wi++) {
			if (!workOrderInstances[wi].city || !unitLists[wi].empty())
				continue;
			const size_t farms = farmJobs(wi);
			if (!farms)
				continue;
			size_t donor = SIZE_MAX;
			for (size_t di = 0; di < workOrderInstances.size(); di++)
				if (di != wi && workOrderInstances[di].workOrder == workOrderInstances[wi].workOrder
					&& (donor == SIZE_MAX || unitLists[di].size() > unitLists[donor].size()))
					donor = di;
			if (donor == SIZE_MAX)
				continue;
			auto& from = unitLists[donor];
			// peasants by id, the highest lent, so the same ones go each update
			std::sort(from.begin(), from.end(), [](ServerGameObject* a, ServerGameObject* b) { return a->id < b->id; });
			const size_t lend = std::min(farms, from.size() / 2);
			for (size_t k = 0; k < lend; k++) {
				unitLists[wi].push_back(from.back());
				from.pop_back();
			}
		}
	}
	unitWorkOrder.clear();
	for (size_t wi = 0; wi < workOrderInstances.size(); wi++) {
		auto& list = unitLists[wi];
		for (ServerGameObject* unit : list)
			unitWorkOrder[unit->id] = { workOrderInstances[wi].city->id, workOrderInstances[wi].workOrder };
	}
	for (size_t wi = 0; wi < workOrderInstances.size(); wi++) {
		auto& woi = workOrderInstances[wi];
		if (!woi.city)
			continue;
		SrvScriptContext ctx{ Server::instance, woi.city };
		auto& units = unitLists[wi];

		//// count current orders
		//DynArray<int> unitsPerAssignment;
		//unitsPerAssignment.resize(woi.workOrder->assignments.size());
		//for (auto& val : unitsPerAssignment)
		//	val = 0;
		//for (auto& unit : units) {
		//	if (unit->blueprint->bpClass == Tags::GAMEOBJCLASS_CHARACTER) {
		//		for (size_t i = 0; i < woi.workOrder->assignments.size(); ++i) {
		//			auto& asg = woi.workOrder->assignments[i];
		//			Order* order = unit->orderConfig.getCurrentOrder();
		//			if (order && order->blueprint == asg.order) {
		//				unitsPerAssignment[i] += 1;
		//			}
		//		}
		//	}
		//}

		// The assignments, in the work order's priority order, become one job (order, target) per
		// unit. A job goes first to a unit already doing it, then to one with the same order, then
		// to the next unit by id. Handing out the units again by id alone moved them between jobs
		// whenever the job list changed (a farm or foundation more or less), and each move restarted
		// their order (FORGET_EVERYTHING_ELSE). In the 6-minute AI test this took the Duke from 37
		// to 61 characters and from 0 to 1750 wood.

		auto idComparator = [](ServerGameObject* a, ServerGameObject* b) -> bool {return a->id < b->id; };
		std::sort(units.begin(), units.end(), idComparator);
		struct Job { ServerGameObject* target; const OrderBlueprint* order; };
		std::vector<Job> jobs;
		size_t unitsLeft = units.size();
		auto popUnitAndAssignTo = [&](ServerGameObject* target, const OrderBlueprint* order) {
			if (unitsLeft == 0) return;
			jobs.push_back({ target, order });
			unitsLeft--;
		};
		for (size_t i = 0; i < woi.workOrder->assignments.size(); ++i) {
			auto& asg = woi.workOrder->assignments[i];
			int quantity = 0;
			switch (asg.quantityType) {
			case WorkOrder::Assignment::INDIVIDUALS:
				quantity = (int)asg.quantityValue->eval(&ctx); // round down or up?
				break;
			case WorkOrder::Assignment::FRACTION_DOWN:
				quantity = (int)((float)unitsLeft * asg.quantityValue->eval(&ctx)); // round down or up?
				break;
			case WorkOrder::Assignment::REMAINING_WORKERS:
				quantity = (int)unitsLeft;
				break;
			}
			switch (asg.distType) {
			case WorkOrder::Assignment::DIST_PER: {
				// for every target we assign "quantity" units
				auto targets = asg.distFinder->eval(&ctx);
				std::sort(targets.begin(), targets.end(), idComparator);
				for (auto& target : targets) {
					for (int i = 0; i < quantity; i++) {
						popUnitAndAssignTo(target, asg.order);
					}
				}
				break;
			}
			case WorkOrder::Assignment::DIST_BETWEEN: {
				// assign "quantity" units across the targets
				auto targets = asg.distFinder->eval(&ctx);
				std::sort(targets.begin(), targets.end(), idComparator);
				if (!targets.empty()) {
					for (int i = 0; i < quantity; i++) {
						int t = i % targets.size();
						popUnitAndAssignTo(targets[t], asg.order);
					}
				}
				break;
			}
			case WorkOrder::Assignment::DIST_AUTO_IDENTIFY:
				// assign "quantity" units to random objects
				for (int i = 0; i < quantity; i++) {
					popUnitAndAssignTo(nullptr, asg.order);
				}
				break;
			}
		}

		// whether the unit's current order is this one (on this target, if the job has one)
		auto doing = [](CommonGameObject* obj, const OrderBlueprint* order, ServerGameObject* target) {
			Order* current = ((ServerGameObject*)obj)->orderConfig.getCurrentOrder();
			if (!current || current->blueprint != order)
				return false;
			if (!target)
				return true;
			for (const auto& task : current->tasks)
				if (task->target.get() == target)
					return true;
			return false;
		};
		std::vector<bool> taken(units.size(), false);
		std::vector<int> unitOfJob(jobs.size(), -1);
		auto pick = [&](size_t j, auto&& accept) {
			for (int u = (int)units.size() - 1; u >= 0; u--)
				if (!taken[u] && accept(units[u])) {
					taken[u] = true;
					unitOfJob[j] = u;
					return;
				}
		};
		for (size_t j = 0; j < jobs.size(); j++)
			pick(j, [&](CommonGameObject* u) { return doing(u, jobs[j].order, jobs[j].target); });
		for (size_t j = 0; j < jobs.size(); j++)
			if (unitOfJob[j] < 0)
				pick(j, [&](CommonGameObject* u) { return doing(u, jobs[j].order, nullptr); });
		for (size_t j = 0; j < jobs.size(); j++)
			if (unitOfJob[j] < 0)
				pick(j, [](CommonGameObject*) { return true; });
		// a unit with the job's order keeps its own target (a builder stays at its foundation)
		for (size_t j = 0; j < jobs.size(); j++) {
			ServerGameObject* unit = (ServerGameObject*)units[unitOfJob[j]];
			if (!doing(unit, jobs[j].order, nullptr))
				unit->orderConfig.addOrder((OrderBlueprint*)jobs[j].order, Tags::ORDERASSIGNMODE_FORGET_EVERYTHING_ELSE, jobs[j].target);
		}
	}
	auto wit = std::remove_if(workOrderInstances.begin(), workOrderInstances.end(),
		[](auto& woi) -> bool {return !woi.city; }
	);
	workOrderInstances.erase(wit, workOrderInstances.end());

	static std::vector<std::pair<const GSCommission*, SrvGORef>> completedComInsts;
	completedComInsts.clear();

	// ---- Commissions ----
	// Highest PRIORITY first. While a commission can't afford the spawn it wants, the
	// resources are reserved for it: the ones below it spawn nothing in that update (the
	// original's saves show this: a priority-0 "Monk Spawning Army" stays UNPROCESSED while the
	// army above it is CANT_AFFORD, and its other rungs RESOURCE_REQUIRED_RESERVED). Not for the
	// aggressive AI, which spends what it has.
	std::vector<std::pair<float, size_t>> byPriority;
	for (size_t i = 0; i < commissionInstances.size(); i++) {
		ServerGameObject* handler = commissionInstances[i].handlerObject.get();
		float priority = 0.0f;
		if (handler && commissionInstances[i].blueprint->priority) {
			SrvScriptContext pctx{ Server::instance, handler };
			priority = commissionInstances[i].blueprint->priority->eval(&pctx);
		}
		byPriority.push_back({ priority, i });
	}
	std::stable_sort(byPriority.begin(), byPriority.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
	bool reserved = false;
	for (const auto& [commissionPriority, cid] : byPriority) {
		auto& cominst = commissionInstances[cid];
		const GSCommission* gscomm = cominst.blueprint;
		ServerGameObject* obj = cominst.handlerObject.get();
		if (!obj) continue;
		SrvScriptContext ctx{ Server::instance, obj };
		// A suspended building commission resumes after 90 s, for 90 s at a time. The Kingmaker
		// AI pauses its buildings to save 300 food and wood for Flaming Arrows ("Kingmaker Do I Need
		// Flaming Arrow Upgrade Pause Buildings"), but with no food income (no farms yet, its start food spent
		// on peasants) the research never came and nor did the village and farms
		// that would pay for it: the AI stood still for the whole game. Not in WKB.exe; my deadlock
		// breaker, for the user's "vastly more aggressive" AI.
		if (gscomm->suspensionCondition && gscomm->suspensionCondition->booleval(&ctx)) {
			const float now = Server::instance->timeManager.currentTime;
			if (cominst.suspendedSince < 0.0f)
				cominst.suspendedSince = now;
			const float suspended = now - cominst.suspendedSince;
			if (gscomm->buildingRequirements.empty() || std::fmod(suspended, 180.0f) < 90.0f)
				continue;
		}
		else
			cominst.suspendedSince = -1.0f;

		size_t completeCount = 0;

		// Character Requirements
		const std::vector<int> charNeeds = AICharacterNeeds(gscomm, &ctx, cominst.forceBalance);
		bool shortOfResources = false; // a requirement found nothing it could pay for
		// FORCE_BALANCE: the requirement furthest below its count first (the least filled share;
		// ties in the commission's order). The original's test 36: the defensive army at heavy
		// infantry 5 of 6 and light infantry 4 of 6 got an Archer, then the Spearman, then an
		// Archer (ids 14244, 14255, 14263); in the commission's order the Spearman came first
		// and its barracks then had no time for the archers.
		std::vector<size_t> reqOrder(cominst.characterReqInsts.size());
		std::iota(reqOrder.begin(), reqOrder.end(), 0);
		if (cominst.forceBalance) {
			std::vector<float> filled(reqOrder.size(), 1.0f);
			for (size_t ri : reqOrder)
				if (charNeeds[ri] > 0)
					filled[ri] = (float)gscomm->characterRequirements[ri].fdAlreadyHaving->eval(&ctx).size() / charNeeds[ri];
			std::stable_sort(reqOrder.begin(), reqOrder.end(), [&](size_t a, size_t b) { return filled[a] < filled[b]; });
		}
		for (size_t ri : reqOrder) {
			auto& gsreq = gscomm->characterRequirements[ri];
			auto& reqinst = cominst.characterReqInsts[ri];
			size_t requiredCount = (size_t)std::max(0, charNeeds[ri]);
			size_t existingCount = gsreq.fdAlreadyHaving->eval(&ctx).size();
			//size_t inCtrCount = reqinst.foundations.size();
			if (existingCount >= requiredCount)
				completeCount += 1;
			else if (!reserved) {
				// The ladder's rungs best first, as WKB.exe (AI_Character_Requirement 0x4c7da0, which
				// tries each rung in order and stops at the first that spawns): a rung is spawned at
				// the idle buildings of its finder where its "Spawn <type>" command can be executed
				// now (tech level, alignment, cost, population); a rung already being spawned
				// counts too, so a busy building doesn't make the AI fall back to a worse rung
				const GameSet* gs = Server::instance->gameSet.get();
				bool anyServed = false, anyUnaffordable = false, anyBusy = false;
				for (auto& rung : gsreq.ladder->entries) {
					const std::string spawnName = "Spawn " + rung.type->name;
					const int ordid = gs->orders.names.getIndex(spawnName);
					if (ordid < 0)
						continue;
					const OrderBlueprint* orderBp = &gs->orders[ordid];
					const int cmdid = gs->commands.names.getIndex(spawnName);
					const Command* command = cmdid >= 0 ? &gs->commands[cmdid] : nullptr;
					bool served = false, unaffordable = false, busy = false;
					for (ServerGameObject* building : rung.finder->eval(&ctx)) {
						if (building->blueprint->bpClass != Tags::GAMEOBJCLASS_BUILDING)
							continue;
						if (Order* current = building->orderConfig.getCurrentOrder()) {
							if (current->blueprint == orderBp)
								served = true;
							else
								busy = true;
							continue;
						}
						// impossible here (tech level, alignment): try another building; only waiting
						// (the cost, population) or short of the type's cost: the rung can't be paid
						if (command) {
							SrvScriptContext cctx(Server::instance, building);
							auto _s = cctx.selectedObject.change(building);
							auto holds = [&](GSCondition* cond) { return cond->test->booleval(&cctx); };
							if (!std::all_of(command->conditionsImpossible.begin(), command->conditionsImpossible.end(), holds))
								continue;
							if (!std::all_of(command->conditionsWait.begin(), command->conditionsWait.end(), holds)) {
								unaffordable = true;
								if (getenv("WKB_TRACE_COMMISSION_RUNGS"))
									for (GSCondition* cond : command->conditionsWait)
										if (!holds(cond))
											printf("RUNGWAIT t=%.0f %s at %u player %u food %.0f handler %u: \"%s\"\n", Server::instance->timeManager.currentTime, rung.type->name.c_str(), building->id,
												building->getPlayer() ? building->getPlayer()->id : 0, building->getPlayer() ? building->getPlayer()->getItem(gs->items.names.getIndex("Food")) : -1.0f,
												this->gameObj->id, gs->conditions.getString(cond).c_str());
								continue;
							}
							if (!command->canBeExecuted(building))
								continue;
						}
						if (!this->gameObj->canAffordObject(rung.type)) {
							unaffordable = true;
							if (getenv("WKB_TRACE_COMMISSION_RUNGS"))
								printf("RUNGWAIT %s at %u: cost (handler %u)\n", rung.type->name.c_str(), building->id, this->gameObj->id);
							continue;
						}
						if (getenv("WKB_TRACE_COMMISSION"))
							printf("COMMISSION t=%.2f \"%s\" (priority %.1f, handler %u): spawn %s at %u, required %zu, existing %zu\n", Server::instance->timeManager.currentTime,
								Server::instance->gameSet->commissions.getString(gscomm).c_str(), commissionPriority, obj->id, rung.type->name.c_str(), building->id, requiredCount, existingCount);
						Order* order = building->orderConfig.addOrder(orderBp, Tags::ORDERASSIGNMODE_DO_FIRST, nullptr, {-1,-1,-1}, false);
						auto* task = (SpawnTask*)(order->tasks[0].get());
						task->setSpawnBlueprint(rung.type);
						task->aiCommissioner = obj;
						served = true;
					}
					anyUnaffordable |= unaffordable;
					anyBusy |= busy;
					if (getenv("WKB_TRACE_COMMISSION_RUNGS"))
						printf("RUNG t=%.2f \"%s\" req %zu: %s served %d unaffordable %d (required %zu, existing %zu, reserved %d)\n", Server::instance->timeManager.currentTime,
							Server::instance->gameSet->commissions.getString(gscomm).c_str(), ri, rung.type->name.c_str(), (int)served, (int)unaffordable, requiredCount, existingCount, (int)reserved);
					if (served) {
						anyServed = true;
						break;
					}
					// a rung it can't pay for: down to the next, cheaper one (the original's save:
					// Pikeman CANT_AFFORD, then Spearman spawned)
				}
				// waiting for a building busy with another spawn holds the resources too: the
				// original spawned no Peasant or Monk while its one barracks trained the army
				if (!anyServed && (anyUnaffordable || anyBusy))
					shortOfResources = true;
			}
		}
		// a commission that couldn't pay for what it needs keeps the resources: the ones below it
		// spawn nothing in this update (not for the aggressive AI, which spends what it has)
		if (shortOfResources && !Server::instance->aggressiveAI)
			reserved = true;

		// Building Requirements
		int isFoundationItem = Server::instance->gameSet->items.names.getIndex("Is a Foundation");
		// The aggressive AI places the buildings that train fighters (Barracks, Stables) first and
		// saves for them: while one is short of its cost, the others wait. The requirements' own
		// order put farms first, which took the wood as fast as it came; the first barracks came
		// after 9 to 12 minutes.
		// Fighters first starved the food instead: with every bit of wood on the barracks and walls,
		// the AIs had no farm at 3 minutes, no food, trained no peasant, and stayed at their 15
		// starting characters for 6-9 minutes. So the first few farms come before them (a pass of
		// their own, at most AI_EARLY_FARMS), then the fighter buildings, then the rest.
		std::vector<size_t> buildOrder(cominst.buildingReqInsts.size());
		std::iota(buildOrder.begin(), buildOrder.end(), 0);
		const bool aggressive = Server::instance->aggressiveAI;
		std::vector<std::pair<size_t, size_t>> passes; // requirement, at most this many (SIZE_MAX: all)
		if (aggressive) {
			std::stable_partition(buildOrder.begin(), buildOrder.end(), [&](size_t ri) { return TrainsFighters(gscomm->buildingRequirements[ri].bpBuilding); });
			for (size_t ri : buildOrder)
				if (ProducesFood(gscomm->buildingRequirements[ri].bpBuilding))
					passes.emplace_back(ri, (size_t)Server::instance->aiEarlyFarms);
		}
		for (size_t ri : buildOrder)
			passes.emplace_back(ri, SIZE_MAX);
		bool saving = false;
		std::set<size_t> counted; // requirements counted towards completeCount
		for (const auto& [ri, cap] : passes) {
			auto& gsreq = gscomm->buildingRequirements[ri];
			auto& reqinst = cominst.buildingReqInsts[ri];
			auto it = std::remove_if(reqinst.foundations.begin(), reqinst.foundations.end(),
				[isFoundationItem](SrvGORef& ref) { return !(ref && ref->getItem(isFoundationItem) > 0.0f); }
			);
			reqinst.foundations.erase(it, reqinst.foundations.end());
			// rounded up, as the character requirements: the original's Kingmaker wants max(10,
			// peasants / 2.5) farms, 34.8 for its 87 peasants, and placed a 35th foundation for its
			// 34 farms (test 36, wood 1027 -> 972 by 1805.42); cut to 34, wkbai placed none
			const float wanted = gsreq.vdCountNeeded->eval(&ctx);
			size_t fullCount = wanted > 0.0f ? (size_t)std::ceil(wanted - 1e-4f) : 0;
			// The aggressive AI's surplus (not in WKB.exe): at 25 minutes the AIs had 1,100-2,300 food
			// and 1,000-1,400 wood unspent while their one barracks trained one unit at a time. A
			// fighter building already wanted is wanted once more per aiSurplusPerBuilding of food and
			// wood together, up to aiSurplusBuildings more.
			if (aggressive && fullCount > 0 && Server::instance->aiSurplusBuildings > 0 && TrainsFighters(gsreq.bpBuilding)) {
				static int foodItem = -1, woodItem = -1;
				static const GameSet* itemsOf = nullptr;
				if (itemsOf != Server::instance->gameSet.get()) {
					itemsOf = Server::instance->gameSet.get();
					foodItem = itemsOf->items.names.getIndex("Food");
					woodItem = itemsOf->items.names.getIndex("Wood");
				}
				ServerGameObject* player = obj->getPlayer();
				const float stock = (foodItem >= 0 ? player->getItem(foodItem) : 0.0f) + (woodItem >= 0 ? player->getItem(woodItem) : 0.0f);
				fullCount += std::min((size_t)Server::instance->aiSurplusBuildings, (size_t)(stock / std::max(1.0f, Server::instance->aiSurplusPerBuilding)));
			}
			size_t requiredCount = std::min(fullCount, cap);
			size_t existingCount = gsreq.fdAlreadyHaving->eval(&ctx).size();
			size_t inCtrCount = reqinst.foundations.size();
			while (!saving && existingCount + inCtrCount < requiredCount) {
				auto posori = gsreq.pdBuildPosition->eval(&ctx);
				// no site (the position's finder found nothing): no foundation this time
				if (posori.position.x == 0.0f && posori.position.z == 0.0f)
					break;
				const GameObjBlueprint* bpFoundation = Server::instance->gameSet->findBlueprint(Tags::GAMEOBJCLASS_BUILDING, gsreq.bpBuilding->name + " Foundation");
				ServerGameObject* foundation = Server::instance->stampdownObject(bpFoundation, obj->getPlayer(), posori.position, Vector3(0,0,0), false, true);
				if (!foundation) {
					if (aggressive && bpFoundation && TrainsFighters(gsreq.bpBuilding) && !obj->getPlayer()->canAffordObject(bpFoundation))
						saving = true;
					break;
				}
				obj->sendEvent(Tags::PDEVENT_ON_COMMISSIONED, foundation);
				reqinst.foundations.emplace_back(foundation);
				inCtrCount = reqinst.foundations.size();
			}
			if (cap == SIZE_MAX && existingCount >= requiredCount && counted.insert(ri).second) {
				completeCount += 1;
			}
		}

		// Upgrade Requirements
		for (size_t ri = 0; ri < cominst.upgradeReqInsts.size(); ++ri) {
			auto& gsreq = gscomm->upgradeRequirements[ri];
			auto& reqinst = cominst.upgradeReqInsts[ri];
			// complete once nothing is needed any more (the needed value, such as "Kingmaker Do I Need
			// Flaming Arrow Upgrade", drops to 0 when the research is done); until then, while no
			// supporting building is researching it, the upgrade's command is run on an idle one, which
			// tests its conditions and takes its cost. The research was started once and taken as
			// done for good: when it didn't happen, the commissions waiting for it ("... Pause
			// Buildings") stayed suspended for the whole game, and a requirement needing nothing
			// never counted as complete.
			const float requiredCount = gsreq.vdCountNeeded->eval(&ctx);
			if (requiredCount <= 0.0f) {
				completeCount += 1;
			}
			else if (gsreq.order) {
				SrvFinderResult buildingList = gsreq.fdSupportedBuildings->eval(&ctx);
				const bool researching = std::any_of(buildingList.begin(), buildingList.end(), [&](ServerGameObject* b) {
					Order* current = b->orderConfig.getCurrentOrder();
					return current && current->blueprint == gsreq.order; });
				const GameSet* gs = Server::instance->gameSet.get();
				const int cmdid = gs->commands.names.getIndex(gs->orders.getString(gsreq.order));
				const Command* command = cmdid >= 0 ? &gs->commands[cmdid] : nullptr;
				if (!researching)
					for (ServerGameObject* building : buildingList) {
						if (building->blueprint->bpClass != Tags::GAMEOBJCLASS_BUILDING || building->orderConfig.getCurrentOrder())
							continue;
						if (command) {
							if (!command->canBeExecuted(building))
								continue;
							command->execute(building, nullptr, Tags::ORDERASSIGNMODE_DO_FIRST);
						}
						else
							building->orderConfig.addOrder(gsreq.order, Tags::ORDERASSIGNMODE_DO_FIRST);
						reqinst.done = true;
						break;
					}
			}
		}

		if (completeCount == cominst.characterReqInsts.size() + cominst.buildingReqInsts.size() + cominst.upgradeReqInsts.size()) {
			completedComInsts.push_back({ cominst.blueprint, cominst.handlerObject });
		}
	}

	for (auto& [com, hand] : completedComInsts) {
		hand->sendEvent(com->onCompleteEvent);
	}

	auto cit = std::remove_if(commissionInstances.begin(), commissionInstances.end(),
		[](auto& ci) -> bool {return !ci.handlerObject.get(); }
	);
	commissionInstances.erase(cit, commissionInstances.end());
}

void AIController::activatePlan(int planTag)
{
	auto& plan = Server::instance->gameSet->plans[planTag];
	this->planBlueprint = &plan;
	this->planState = plan.createState();
}

void AIController::abandonPlan()
{
	planBlueprint = nullptr;
	planState = {};
	// TODO: Free the state pointers
}

void AIController::registerWorkOrder(ServerGameObject* city, ObjectFinder* unitFinder, const WorkOrder* workOrder)
{
	// the same work order registered again for a city replaces the earlier one (the scripts do
	// it again and again; kept twice, the same workers got their orders from both)
	for (auto& woi : workOrderInstances)
		if (woi.city.get() == city && woi.workOrder == workOrder) {
			woi.unitFinder = unitFinder;
			return;
		}
	WorkOrderInstance woi{ city, unitFinder, workOrder };
	workOrderInstances.push_back(std::move(woi));
}

void AIController::activateCommission(const GSCommission* commission, ServerGameObject* handler, bool forceBalance)
{
	deactivateCommission(commission, handler);
	CommissionInstance& ci = commissionInstances.emplace_back();
	ci.blueprint = commission;
	ci.forceBalance = forceBalance;
	ci.handlerObject = handler;
	ci.characterReqInsts.resize(commission->characterRequirements.size());
	ci.buildingReqInsts.resize(commission->buildingRequirements.size());
	ci.upgradeReqInsts.resize(commission->upgradeRequirements.size());
	printf("COMMISSION \"%s\" ACTIVATED\n", Server::instance->gameSet->commissions.getString(commission).c_str());
}

void AIController::deactivateCommission(const GSCommission* commission, ServerGameObject* handler)
{
	for (auto it = commissionInstances.begin(); it != commissionInstances.end(); ++it) {
		auto& ci = *it;
		if (ci.blueprint == commission && ci.handlerObject.get() == handler) {
			commissionInstances.erase(it);
			return;
		}
	}
}
