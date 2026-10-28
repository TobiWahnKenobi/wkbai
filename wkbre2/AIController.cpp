#include "AIController.h"
#include "server.h"
#include "gameset/ScriptContext.h"
#include "gameset/Plan.h"
#include "gameset/gameset.h"
#include "util/GSFileParser.h"
#include "gameset/finder.h"
#include "Order.h"

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
	unitWorkOrder.clear();
	for (size_t wi = 0; wi < workOrderInstances.size(); wi++) {
		auto& list = unitLists[wi];
		list.resize(std::remove_if(list.begin(), list.end(), [&](ServerGameObject* unit) { return owner[unit->id].first != wi; }) - list.begin());
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
	int cid = -1;
	for (auto& cominst : commissionInstances) {
		++cid;
		const GSCommission* gscomm = cominst.blueprint;
		ServerGameObject* obj = cominst.handlerObject.get();
		if (!obj) continue;
		SrvScriptContext ctx{ Server::instance, obj };
		if (gscomm->suspensionCondition && gscomm->suspensionCondition->booleval(&ctx))
			continue;

		size_t completeCount = 0;

		// Character Requirements
		for (size_t ri = 0; ri < cominst.characterReqInsts.size(); ++ri) {
			auto& gsreq = gscomm->characterRequirements[ri];
			auto& reqinst = cominst.characterReqInsts[ri];
			size_t requiredCount = (size_t)std::ceil(gsreq.vdCountNeeded->eval(&ctx));
			size_t existingCount = gsreq.fdAlreadyHaving->eval(&ctx).size();
			//size_t inCtrCount = reqinst.foundations.size();
			if (existingCount < requiredCount) {
				// The ladder's rungs best first, as WKB.exe (AI_Character_Requirement 0x4c7da0, which
				// tries each rung in order and stops at the first that spawns): a rung is spawned at
				// the idle buildings of its finder where its "Spawn <type>" command can be executed
				// now (tech level, alignment, cost, population); a rung already being spawned
				// counts too, so a busy building doesn't make the AI fall back to a worse rung
				const GameSet* gs = Server::instance->gameSet.get();
				for (auto& rung : gsreq.ladder->entries) {
					const std::string spawnName = "Spawn " + rung.type->name;
					const int ordid = gs->orders.names.getIndex(spawnName);
					if (ordid < 0)
						continue;
					const OrderBlueprint* orderBp = &gs->orders[ordid];
					const int cmdid = gs->commands.names.getIndex(spawnName);
					const Command* command = cmdid >= 0 ? &gs->commands[cmdid] : nullptr;
					bool served = false;
					for (ServerGameObject* building : rung.finder->eval(&ctx)) {
						if (building->blueprint->bpClass != Tags::GAMEOBJCLASS_BUILDING)
							continue;
						if (Order* current = building->orderConfig.getCurrentOrder()) {
							if (current->blueprint == orderBp)
								served = true;
							continue;
						}
						if ((command && !command->canBeExecuted(building)) || !this->gameObj->canAffordObject(rung.type))
							continue;
						Order* order = building->orderConfig.addOrder(orderBp, Tags::ORDERASSIGNMODE_DO_FIRST, nullptr, {-1,-1,-1}, false);
						auto* task = (SpawnTask*)(order->tasks[0].get());
						task->setSpawnBlueprint(rung.type);
						task->aiCommissioner = obj;
						served = true;
					}
					if (served)
						break;
				}
			}
			else {
				completeCount += 1;
			}
		}

		// Building Requirements
		int isFoundationItem = Server::instance->gameSet->items.names.getIndex("Is a Foundation");
		for (size_t ri = 0; ri < cominst.buildingReqInsts.size(); ++ri) {
			auto& gsreq = gscomm->buildingRequirements[ri];
			auto& reqinst = cominst.buildingReqInsts[ri];
			auto it = std::remove_if(reqinst.foundations.begin(), reqinst.foundations.end(),
				[isFoundationItem](SrvGORef& ref) { return !(ref && ref->getItem(isFoundationItem) > 0.0f); }
			);
			reqinst.foundations.erase(it, reqinst.foundations.end());
			size_t requiredCount = (size_t)gsreq.vdCountNeeded->eval(&ctx);
			size_t existingCount = gsreq.fdAlreadyHaving->eval(&ctx).size();
			size_t inCtrCount = reqinst.foundations.size();
			while (existingCount + inCtrCount < requiredCount) {
				auto posori = gsreq.pdBuildPosition->eval(&ctx);
				const GameObjBlueprint* bpFoundation = Server::instance->gameSet->findBlueprint(Tags::GAMEOBJCLASS_BUILDING, gsreq.bpBuilding->name + " Foundation");
				ServerGameObject* foundation = Server::instance->stampdownObject(bpFoundation, obj->getPlayer(), posori.position, Vector3(0,0,0), false, true);
				if (!foundation)
					break;
				obj->sendEvent(Tags::PDEVENT_ON_COMMISSIONED, foundation);
				reqinst.foundations.emplace_back(foundation);
				inCtrCount = reqinst.foundations.size();
			}
			if (existingCount >= requiredCount) {
				completeCount += 1;
			}
		}

		// Upgrade Requirements
		for (size_t ri = 0; ri < cominst.upgradeReqInsts.size(); ++ri) {
			auto& gsreq = gscomm->upgradeRequirements[ri];
			auto& reqinst = cominst.upgradeReqInsts[ri];
			size_t requiredCount = (size_t)gsreq.vdCountNeeded->eval(&ctx);
			if (!reqinst.done && requiredCount > 0) {
				// find supporting buildings and launch upgrades
				// TODO: Prevent upgrades from happening multiple times (check preconditions)
				// might need to use COMMAND instead of ORDER
				SrvFinderResult buildingList = gsreq.fdSupportedBuildings->eval(&ctx);
				for (ServerGameObject* building : buildingList) {
					if (building->blueprint->bpClass == Tags::GAMEOBJCLASS_BUILDING) {
						if (building->orderConfig.getCurrentOrder() == nullptr) {
							OrderBlueprint* orderBp = gsreq.order;
							Order* order = building->orderConfig.addOrder(orderBp, Tags::ORDERASSIGNMODE_DO_FIRST);
							reqinst.done = true;
							break;
						}
					}
				}
			}
			else if (reqinst.done) {
				completeCount += 1;
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

void AIController::activateCommission(const GSCommission* commission, ServerGameObject* handler)
{
	deactivateCommission(commission, handler);
	CommissionInstance& ci = commissionInstances.emplace_back();
	ci.blueprint = commission;
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
