// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#include "Order.h"
#include <set>
#include <map>
#include <algorithm>
#include <cctype>
#include <cstring>
#include "util/SimRandom.h"
#include "tags.h"
#include "gameset/OrderBlueprint.h"
#include "gameset/values.h"
#include "server.h"
#include "util/GSFileParser.h"
#include "gameset/finder.h"
#include "gameset/ScriptContext.h"
#include "gameset/gameset.h"
#include "terrain.h"
#include "gameset/Footprint.h"
#include "gameset/GameObjBlueprint.h"
#include "NNSearch.h"
#include "util/StriCompare.h"
#include "Model.h"
#include <map>
#include <cstdio>

static float FootprintDistance(const Vector3& point, const ServerGameObject* building);

// A task's PROXIMITY_DISSATISFIED_SEQUENCE, if its condition (when it has one) holds
static void RunProximityDissatisfied(const TaskBlueprint* bp, ServerGameObject* self)
{
	if (bp->proximityDissatisfiedEquation >= 0) {
		SrvScriptContext ctx(Server::instance, self);
		CommonGameObject* obj = bp->proximityDissatisfiedFinder ? bp->proximityDissatisfiedFinder->getFirst(&ctx) : self;
		if (!obj)
			return;
		auto _ = ctx.changeSelf(obj);
		if (!Server::instance->gameSet->equations[bp->proximityDissatisfiedEquation]->booleval(&ctx))
			return;
	}
	bp->proximityDissatisfiedSequence.run(self);
}

void Order::init()
{
	for (auto& task : this->tasks)
		task->init();
	this->currentTask = 0;
}

void Order::start()
{
	if (isWorking()) return;
	this->state = OTS_PROCESSING;
	this->currentTask = 0;
	this->blueprint->initSequence.run(this->gameObject);
	this->getCurrentTask()->start();
	this->blueprint->startSequence.run(this->gameObject);
}

void Order::suspend()
{
	if (!isWorking()) return;
	this->state = OTS_SUSPENDED;
	this->tasks[this->currentTask]->suspend();
	//this->blueprint->suspensionSequence.run(this->gameObject);
}

void Order::resume()
{
	this->state = OTS_PROCESSING;
	this->tasks[this->currentTask]->resume();
	this->blueprint->resumptionSequence.run(this->gameObject);
}

void Order::cancel()
{
	if (isDone()) return;
	this->state = OTS_CANCELLED;
	this->tasks[this->currentTask]->cancel();
	this->blueprint->cancellationSequence.run(this->gameObject);
	this->gameObject->updateBuildingOrderCount(this->blueprint);
}

void Order::terminate()
{
	if (isDone()) return;
	this->state = OTS_TERMINATED;
	this->tasks[this->currentTask]->terminate();
	this->blueprint->terminationSequence.run(this->gameObject);
	this->gameObject->updateBuildingOrderCount(this->blueprint);
}

void Order::process()
{
	if (this->state == OTS_SUSPENDED)
		this->resume();
	else if (this->state != OTS_PROCESSING)
		this->start();
	if (this->state == OTS_PROCESSING) {
		Task *task = this->getCurrentTask();
		task->process();
	}
}

Task * Order::getCurrentTask()
{
	if (currentTask == -1) return nullptr;
	return this->tasks[this->currentTask].get();
}

void Order::advanceToNextTask()
{
	currentTask = (currentTask + 1) % tasks.size();
	// a patrol: round again, to its next point
	if (currentTask == 0 && patrolPoints.size() >= 2) {
		patrolNext = (patrolNext + 1) % patrolPoints.size();
		for (auto& task : tasks)
			task->destination = patrolPoints[patrolNext];
	}
	// (a patrol cycles whatever order carries it)
	if (currentTask == 0 && !blueprint->cycleOrder && patrolPoints.size() < 2) {
		terminate();
	}
}

Task::Task(int id, const TaskBlueprint * blueprint, Order * order) : order(order), id(id), blueprint(blueprint)
{
	this->triggers.resize(blueprint->triggers.size());
	for (size_t i = 0; i < this->triggers.size(); i++) {
		auto &trigbp = blueprint->triggers[i];
		std::unique_ptr<Trigger> trigger;
		if (trigbp.type == Tags::TASKTRIGGER_TIMER)
			trigger = std::make_unique<TimerTrigger>(this, &trigbp);
		else if (trigbp.type == Tags::TASKTRIGGER_ANIMATION_LOOP)
			trigger = std::make_unique<AnimationLoopTrigger>(this, &trigbp);
		else if (trigbp.type == Tags::TASKTRIGGER_UNINTERRUPTIBLE_ANIMATION_LOOP)
			trigger = std::make_unique<AnimationLoopTrigger>(this, &trigbp);
		else if (trigbp.type == Tags::TASKTRIGGER_ATTACHMENT_POINT)
			trigger = std::make_unique<AttachmentPointTrigger>(this, &trigbp);
		else
			trigger = std::make_unique<Trigger>(this, &trigbp);
		this->triggers[i] = std::move(trigger);
	}
}

Task::~Task()
{
	setTarget(nullptr);
}

void Task::init()
{
	this->blueprint->initSequence.run(this->order->gameObject);
}

static void TraceTask(const Task* task, const char* what);

// PLAY_ANIMATION_TO_FIT: the playback rate that makes the task's animation last the given time
// (1 without it). The original saves it as ANIMATION_STATE's fourth value: Chop_Log 1.10 and
// Chop_Tree 0.92 for a "Chopper Time" of 2 s.
float Task::AnimationSpeedToFit(ServerGameObject* go, int anim) const
{
	if (!blueprint->playAnimationToFit)
		return 1.0f;
	SrvScriptContext ctx(Server::instance, go);
	const float fit = blueprint->playAnimationToFit->eval(&ctx);
	Model* model = go->blueprint->getModel(go->subtype, go->appearance, anim, go->animationVariant);
	const float duration = model ? model->getDuration() : 0.0f;
	if (fit <= 0.01f || duration <= 0.0f)
		return 1.0f;
	return duration / fit;
}

void Task::start()
{
	if (isWorking()) return;
	TraceTask(this, "start");
	this->state = OTS_PROCESSING;
	// a fresh start: no saved arrival, and the triggers start anew on arriving (a task loaded
	// with TRIGGERS_STARTED and a reference time from long before had its animation-loop
	// trigger fire at once: the cart collected in the same update as it arrived)
	this->loadedArrived = false;
	this->stopTriggers();
	if (this->blueprint->usePreviousTaskTarget) {
		if (this->id > 0)
			this->setTarget(this->order->tasks[this->id - 1]->target.get());
	}
	else if ((!this->target || blueprint->identifyTargetEachCycle) && blueprint->taskTarget) {
		SrvScriptContext ctx(Server::instance, this->order->gameObject);
		this->setTarget(blueprint->taskTarget->getFirst(&ctx)); // FIXME: that would override the order's target!!!
	}
	this->startSequenceExecuted = false; // is this correct?

	// Check the filter conditions
	// Task is instantly terminated if any condition turns false (termination sequence is executed).
	// The one in TASK "Spawn" needs to be ignored as it is bugged.
	if (this->blueprint->classType != Tags::ORDTSKTYPE_SPAWN) {
		for (int filterConditionEquation : this->blueprint->filterConditionEquations) {
			auto* condition = Server::instance->gameSet->equations[filterConditionEquation];
			SrvScriptContext ctx(Server::instance, this->order->gameObject);
			if (!condition->booleval(&ctx)) {
				this->terminate();
				return;
			}
		}
	}

	if (this->target) {
		if (!this->startSequenceExecuted) {
			if (this->blueprint->proximityRequirement) {
				SrvScriptContext ctx(Server::instance, this->order->gameObject);
				this->proximity = this->blueprint->proximityRequirement->eval(&ctx); // should this be here?
			}
			// A target out of the proximity runs PROXIMITY_DISSATISFIED_SEQUENCE first, and the
			// start sequence only if the task still runs after it. The missile tasks (Fire
			// Javelin, Fire Arrow, ...) hand a unit that may not move back to its player there
			// and end the whole order; their start sequence does the same for a target out of
			// missile range but only ends the task, which a cycling order restarts at once (the
			// AI's ranged units went round 130-580 times in 30 s, 3,874 transfers in all).
			// (a proximity of 0, as TASK "Follow" has, is no requirement here)
			bool walkIntoRange = false;
			if (this->blueprint->proximityRequirement && this->proximity > 0.0f) {
				ServerGameObject* go = this->order->gameObject;
				const float edge = FootprintDistance(go->position, target.get());
				const float dist = edge >= 0.0f ? edge : (target->position - go->position).len2xz();
				// not again for a unit whose dissatisfied sequence is running: one that assigns the
				// unit an order (a SEND_EVENT, then ASSIGN_ORDER_VIA) whose task starts out of range
				// too ran it again and again until the stack ran out (a 25-minute AI game, seed 2)
				static std::set<const ServerGameObject*> running;
				if (dist >= this->proximity && running.count(go)) {
					static std::set<const TaskBlueprint*> warned;
					if (warned.insert(this->blueprint).second)
						printf("WARNING: task \"%s\" started again from its own proximity dissatisfied sequence (object %u); not run again\n",
							Server::instance->gameSet->tasks.names.getString(this->blueprint->bpid).c_str(), go->id);
				}
				// A sequence that does nothing but end the order (TASK "Fire Trebuchet": a mangonel,
				// trebuchet or ship told to fire at a target out of range) is for a target that
				// moves out of range: at the start the unit walks into range first (the play test:
				// it never moved, or, sent with "Attack", walked right up to the Manor, too close
				// to fire)
				else if (dist >= this->proximity && blueprint->proximityDissatisfiedEquation < 0
					&& blueprint->proximityDissatisfiedSequence.actionList.size() == 1
					&& blueprint->proximityDissatisfiedSequence.actionList[0]->endsOrder()) {
					walkIntoRange = true;
				}
				else if (dist >= this->proximity) {
					running.insert(go);
					RunProximityDissatisfied(this->blueprint, go);
					running.erase(go);
					if (!this->isWorking() || !this->order->isWorking())
						return;
				}
			}
			this->blueprint->startSequence.run(order->gameObject);
			this->startSequenceExecuted = true;
			// one that may not walk there (the trebuchet's start sequence ends the task for a
			// target beyond its range): the dissatisfied sequence's end of the order after all, or
			// the cycling order would start the task again at every step
			if (walkIntoRange && !this->isWorking() && this->order->isWorking()) {
				this->order->terminate();
				return;
			}
		}
	}
	this->onStart();
}


// debugging: "WKB_TRACE_TASKS_ID=<object id>" prints that object's task starts, ends,
// proximity changes and trigger firings
static void TraceTask(const Task* task, const char* what)
{
	static const char* env = getenv("WKB_TRACE_TASKS_ID");
	if (!env)
		return;
	static const unsigned id = (unsigned)strtoul(env, nullptr, 10);
	if (!task->order || !task->order->gameObject || task->order->gameObject->id != id)
		return;
	printf("TASK t=%.2f %u \"%s\": %s\n", Server::instance->timeManager.currentTime, id,
		Server::instance->gameSet->tasks.names.getString(task->blueprint->bpid).c_str(), what);
}

void Task::suspend()
{
	if (!isWorking()) return;
	this->state = OTS_SUSPENDED;
	this->stopTriggers();
	ServerGameObject *go = this->order->gameObject;
	if(go->movementController.isMoving())
		go->movementController.stopMovement();
	go->setAnimation(0);
	//this->blueprint->suspensionSequence.run(this->order->gameObject);
}

void Task::resume()
{
	TraceTask(this, "resume");
	this->state = OTS_PROCESSING;
	this->blueprint->resumptionSequence.run(this->order->gameObject);
	this->proximitySatisfied = false; // TODO: put this in "onResume"
}

void Task::cancel()
{
	if (isDone()) return;
	this->state = OTS_CANCELLED;
	this->stopTriggers();
	ServerGameObject *go = this->order->gameObject;
	if (go->movementController.isMoving())
		go->movementController.stopMovement();
	go->setAnimation(0);
	this->releaseAttackPath();
	this->blueprint->cancellationSequence.run(this->order->gameObject);
}

void Task::terminate()
{
	if (isDone()) return;
	TraceTask(this, "terminate");
	this->state = OTS_TERMINATED;
	this->stopTriggers();
	ServerGameObject *go = this->order->gameObject;
	if (go->movementController.isMoving())
		go->movementController.stopMovement();
	go->setAnimation(0);
	this->releaseAttackPath();
	this->blueprint->terminationSequence.run(this->order->gameObject);
	this->order->advanceToNextTask();
}

void Task::process()
{
	if (this->state == OTS_SUSPENDED)
		this->resume();
	else if (this->state != OTS_PROCESSING)
		this->start();
	if (this->state == OTS_PROCESSING) {
		this->onUpdate();
	}
}

void Task::startTriggers()
{
	if (triggersStarted)
		return;
	triggersStarted = true;
	for (auto& trigger : triggers)
		trigger->init();
}

void Task::stopTriggers()
{
	triggersStarted = false;
}

void Task::setTarget(ServerGameObject* newtarget)
{
	ServerGameObject* oldtarget = target.get();
	if (oldtarget && oldtarget != newtarget) // (loading sets the saved target on an empty one)
		loadedArrived = false;
	if (attackPathOf && (!newtarget || newtarget->id != attackPathOf))
		releaseAttackPath();
	//if (oldtarget == newtarget)
	//	return;
	// potential problem: if oldtarget was on deleted obj, and newtarget is null, the equality above will also turn true, leaving oldtarget on deleted obj
	// only becomes problem when suddenly new object is created with the deleted object's ID, which should "rarely" happen
	if (oldtarget) {
		std::swap(*std::find(oldtarget->referencers.begin(), oldtarget->referencers.end(), this->order->gameObject), oldtarget->referencers.back());
		oldtarget->referencers.pop_back();
	}
	target = newtarget;
	if (newtarget)
		newtarget->referencers.push_back(this->order->gameObject);
}

void Task::reevaluateTarget()
{
	suspend();
	if (blueprint->taskTarget) {
		SrvScriptContext ctx(Server::instance, this->order->gameObject);
		this->setTarget(blueprint->taskTarget->getFirst(&ctx));
	}
}

std::unique_ptr<Task> Task::create(int id, const TaskBlueprint* blueprint, Order* order)
{
	std::unique_ptr<Task> task;
	switch (blueprint->classType) {
	default:
	case Tags::ORDTSKTYPE_OBJECT_REFERENCE: task = std::make_unique<ObjectReferenceTask>(id, blueprint, order); break;
	case Tags::ORDTSKTYPE_MOVE: task = std::make_unique<MoveTask>(id, blueprint, order); break;
	case Tags::ORDTSKTYPE_MISSILE: task = std::make_unique<MissileTask>(id, blueprint, order); break;
	case Tags::ORDTSKTYPE_FACE_TOWARDS: task = std::make_unique<FaceTowardsTask>(id, blueprint, order); break;
	case Tags::ORDTSKTYPE_SPAWN: task = std::make_unique<SpawnTask>(id, blueprint, order); break;
	}
	return task;
}



Order* OrderConfiguration::addOrder(const OrderBlueprint* orderBlueprint, int assignMode, ServerGameObject *target, const Vector3 &destination, bool startNow)
{
	if (gameobj->flags & CommonGameObject::fTerminated)
		return nullptr;
	return assignOrder(orderBlueprint, assignMode, target, destination, startNow);
}

Order* OrderConfiguration::assignOrder(const OrderBlueprint* orderBlueprint, int assignMode, ServerGameObject *target, const Vector3 &destination, bool startNow)
{
	if (Order* currentOrder = getCurrentOrder()) {
		if (currentOrder->blueprint->cannotInterruptOrder && assignMode != Tags::ORDERASSIGNMODE_DO_LAST)
			return nullptr;
	}

	// Check preconditions
	for (int preConditionEquationIndex : orderBlueprint->preConditions) {
		if (preConditionEquationIndex != -1) {
			auto* equation = Server::instance->gameSet->equations[preConditionEquationIndex];
			SrvScriptContext ctx(Server::instance, this->gameobj);
			// FINDER_TARGET is the new order's target: without it the finder fell back to the
			// unit's current task's target. Train's "No Target specified or Target Training Dummy
			// has free slots" then passed for a fifth unit at a full dummy (its order started,
			// its start sequence ended it, and the termination sequence freed a slot it never had:
			// any number trained), and failed for one sent on to a free dummy from a full one.
			auto _ = ctx.change(ctx.target, target);
			if (!equation->booleval(&ctx)) {
				return nullptr;
			}
		}
	}

	Order* neworder;
	bool orderInFront = false;
	switch (assignMode) {
	case Tags::ORDERASSIGNMODE_DO_FIRST:
		// the same order again in front replaces it instead of piling up: a formation's army
		// under attack inserts "Quick Formation Army Attack" at every hit, and had a dozen and
		// more stacked; the Move command ends only the front one (TERMINATE_ORDER
		// FINDER_CONTROLLER), the next resumed, and its timer ended the formation's move
		// (TERMINATE_TASK "My Formation") every 2.5 s: the formation stood after a fight
		// (cancelled, not removed: it may be running now; process() drops it once in front)
		if (!this->orders.empty() && this->orders.front().blueprint == orderBlueprint && !this->orders.front().isDone())
			this->orders.front().cancel();
		if (!this->orders.empty()) {
			this->orders.front().suspend();
		}
		this->orders.emplace_front(this->nextOrderId++, orderBlueprint, gameobj);
		neworder = &this->orders.front();
		orderInFront = true;
		break;
	case Tags::ORDERASSIGNMODE_FORGET_EVERYTHING_ELSE:
		this->cancelAllOrders();
		// and the walk that no order made: a formation member's to its place (FormationController).
		// A member killed on the way got "Die" with no order to cancel, walked on and, made its
		// corpse, slid after the formation.
		if (gameobj->movementController.isMoving())
			gameobj->movementController.stopMovement();
		this->orders.emplace_front(this->nextOrderId++, orderBlueprint, gameobj);
		neworder = &this->orders.front();
		orderInFront = true;
		break;
	default:
	case Tags::ORDERASSIGNMODE_DO_LAST:
		// should orderInFront be set to true if no other orders present?
		// if DO_LAST is used, it should be expected that the order cannot start immediately
		// so let's keep it false for now
		this->orders.emplace_back(this->nextOrderId++, orderBlueprint, gameobj);
		neworder = &this->orders.back();
		break;
	}
	for (TaskBlueprint* taskBp : orderBlueprint->tasks) {
		int id = neworder->nextTaskId++;
		neworder->tasks.push_back(Task::create(id, taskBp, neworder));
	}
	if (target) {
		neworder->tasks.at(0)->setTarget(target);
	}
	if (destination.x >= 0.0f) {
		neworder->tasks.at(0)->destination = destination;
	}
	neworder->init();
	if (orderInFront && startNow) // start order (and first task) immediately if no other working order behind
		neworder->start();
	this->gameobj->updateBuildingOrderCount(orderBlueprint);
	return neworder;
}

void OrderConfiguration::cancelAllOrders()
{
	// we cannot delete the orders yet, they might still be referenced
	size_t count = orders.size(); // copy of the size, so that if new orders are created during the cancelling sequence, it won't cancel the new ones
	for (size_t i = 0; i < count; ++i) {
		orders[i].cancel();
	}
	gameobj->reportCurrentOrder(nullptr);
}

void OrderConfiguration::process()
{
	while (!orders.empty() && orders.front().isDone()) {
		orders.pop_front();
	}
	if (!orders.empty()) {
		orders.front().process();
	}
	bool nextBusy = !orders.empty();
	if (busy && !nextBusy)
		gameobj->sendEvent(Tags::PDEVENT_ON_IDLE);
	busy = nextBusy;

	Order* curorder = getCurrentOrder();
	gameobj->reportCurrentOrder(curorder ? curorder->blueprint : nullptr);
}

void Trigger::parse(GSFileParser &gsf, const GameSet &gs)
{
	gsf.advanceLine();
	while (!gsf.eof) {
		std::string word = gsf.nextTag();
		if (word == "END_TRIGGER")
			break;
		gsf.advanceLine();
	}
}

void TimerTrigger::init()
{
	SrvScriptContext ctx(Server::instance, this->task->order->gameObject);
	period = this->blueprint->period->eval(&ctx);
	referenceTime = Server::instance->timeManager.currentTime;
}

void TimerTrigger::update()
{
	if (Server::instance->timeManager.currentTime >= referenceTime + period) {
		TraceTask(this->task, "timer");
		this->blueprint->actions.run(this->task->order->gameObject);
		//TimerTrigger::init();
		this->referenceTime += this->period;
	}
}

void TimerTrigger::parse(GSFileParser & gsf, const GameSet & gs)
{
	gsf.advanceLine();
	while (!gsf.eof) {
		std::string word = gsf.nextTag();
		if (word == "PERIOD")
			period = gsf.nextFloat();
		else if (word == "REFERENCE_TIME")
			referenceTime = gsf.nextFloat();
		else if (word == "END_TRIGGER")
			break;
		gsf.advanceLine();
	}
}

void AnimationLoopTrigger::init()
{
	referenceTime = Server::instance->timeManager.currentTime;
}

void AnimationLoopTrigger::update()
{
	ServerGameObject* obj = this->task->order->gameObject;
	Model* model = obj->blueprint->getModel(obj->subtype, obj->appearance, obj->animationIndex, obj->animationVariant);
	if (obj->animSynchronizedTask != -1) {
		// TODO: We might need to prevent the trigger to happen twice if the task is not terminated in the trigger sequence
		// The Construction Animation tasks do terminate the order, so it's not "that" important.
		SrvScriptContext ctx{ Server::instance, obj };
		float frac = Server::instance->gameSet->tasks[obj->animSynchronizedTask].synchAnimationToFraction->eval(&ctx);
		if (frac >= 1.0f) {
			this->blueprint->actions.run(this->task->order->gameObject);
		}
	}
	else {
		float period = (model ? model->getDuration() : 1.5f) / obj->animSpeed;
		if (Server::instance->timeManager.currentTime >= referenceTime + period) {
			this->blueprint->actions.run(this->task->order->gameObject);
			this->referenceTime += period;
			// a fit time that varies (attacks' "Random Missile Attack Time"): evaluated again
			// for the next loop, the animation restarted at its rate
			if (task->blueprint->playAnimationToFit && !task->isDone() && obj->animationIndex != 0) {
				const float speed = task->AnimationSpeedToFit(obj, obj->animationIndex);
				if (std::abs(speed - obj->animSpeed) > 1e-3f) {
					obj->setAnimation(obj->animationIndex, obj->animClamped, obj->animSynchronizedTask, -1.0f, speed);
					this->referenceTime = Server::instance->timeManager.currentTime;
				}
			}
		}
	}
}

void AnimationLoopTrigger::parse(GSFileParser & gsf, const GameSet & gs)
{
	gsf.advanceLine();
	while (!gsf.eof) {
		std::string word = gsf.nextTag();
		if (word == "REFERENCE_TIME")
			referenceTime = (float)gsf.nextInt() / 1000.0f;
		else if (word == "END_TRIGGER")
			break;
		gsf.advanceLine();
	}
}

void AttachmentPointTrigger::init()
{
	referenceTime = (uint32_t)(Server::instance->timeManager.currentTime * 1000.0f);
}

void AttachmentPointTrigger::update()
{
	ServerGameObject* obj = this->task->order->gameObject;
	Model* model = obj->blueprint->getModel(obj->subtype, obj->appearance, obj->animationIndex, obj->animationVariant);
	if (model) {
		// in the animation's own time (its rate: PLAY_ANIMATION_TO_FIT)
		const float prevT = std::max(0.0f, Server::instance->timeManager.previousTime - obj->animStartTime);
		const float nextT = std::max(0.0f, Server::instance->timeManager.currentTime - obj->animStartTime);
		uint32_t prevTime = (uint32_t)(prevT * obj->animSpeed * 1000.0f);
		uint32_t nextTime = (uint32_t)(nextT * obj->animSpeed * 1000.0f);
		size_t numAPs = model->getNumAPs();
		for (size_t i = 0; i < numAPs; i++) {
			if (model->getAPInfo(i).tag.substr(0, 3) == "SP_") {
				auto p = model->hasAPFlagSwitched(i, prevTime, nextTime);
				if (p.first && p.second) {
					blueprint->actions.run(obj);
					break;
				}
			}
		}
	}
}

void AttachmentPointTrigger::parse(GSFileParser& gsf, const GameSet& gs)
{
	gsf.advanceLine();
	while (!gsf.eof) {
		std::string word = gsf.nextTag();
		if (word == "REFERENCE_TIME")
			referenceTime = gsf.nextInt();
		else if (word == "END_TRIGGER")
			break;
		gsf.advanceLine();
	}
}

void MissileTask::onStart()
{
	// remove the missile object if it has no target
	if (!this->target) {
		Server::instance->deleteObject(this->order->gameObject);
		return;
	}

	SrvScriptContext ssc{ Server::instance, this->order->gameObject };
	float speed = this->order->gameObject->blueprint->missileSpeed->eval(&ssc);

	// missile destination to hit
	Vector3 dest = this->target->position;
	if (Model* model = this->target->blueprint->getModel(target->subtype, target->appearance, target->animationIndex, target->animationVariant)) {
		Vector3 center = model->getSphereCenter().transform(target->getWorldMatrix());
		if (center.y < dest.y)
			center.y = dest.y;
		dest = center;
	}
	// the aim, as WKB.exe's Task_Missile (0x5094b0): a building is hit where it is, unless the
	// missile has APPLY_INACCURACY_TO_BUILDING_TARGETS; anything else is led and scattered
	const GameObjBlueprint* mbp = this->order->gameObject->blueprint;
	const bool exact = this->target->blueprint->bpClass == Tags::GAMEOBJCLASS_BUILDING && !mbp->inaccuracyOnBuildings;
	const float terrainOffset = dest.y - Server::instance->terrain->getHeightEx(dest.x, dest.z, this->target->blueprint->floatsOnWater);
	auto random01 = []() { return (float)SimRand() / (float)(SIM_RAND_MAX + 1); };
	bool moved = false;
	// a moving target: led by where it will be when the missile gets there (the flight time over
	// the horizontal distance at the missile's speed). LEAD_ACCURACY L in (0, 1] (else 1): with
	// L = 1 exactly, below that the lead is scaled by a random factor in [L, 2 - L].
	if (!exact && this->target->movement.isMoving()) {
		float lead = mbp->leadAccuracy ? mbp->leadAccuracy->eval(&ssc) : 1.0f;
		if (!(lead > 0.0f && lead <= 1.0f))
			lead = 1.0f;
		Vector3 hvec = (dest - this->order->gameObject->position);
		float b = hvec.len2xz() / speed; // the flight time to the target's position now
		Vector3 ahead = this->target->movement.getNewPosition(Server::instance->timeManager.currentTime + b) - this->target->position;
		if (lead < 1.0f)
			ahead *= lead + random01() * (2.0f - 2.0f * lead);
		dest.x += ahead.x;
		dest.z += ahead.z;
		moved = true;
	}
	// SPATIAL_INACCURACY S > 0: a random offset in [-S, S] in x and in z
	if (!exact && mbp->spatialInaccuracy) {
		const float s = mbp->spatialInaccuracy->eval(&ssc);
		if (s > 0.0f) {
			dest.x += -s + random01() * 2.0f * s;
			dest.z += -s + random01() * 2.0f * s;
			moved = true;
		}
	}
	if (moved) {
		const auto area = Server::instance->terrain->getPlayableArea();
		dest.x = std::clamp(dest.x, 0.0f, (float)area.first);
		dest.z = std::clamp(dest.z, 0.0f, (float)area.second);
		dest.y = Server::instance->terrain->getHeightEx(dest.x, dest.z, this->target->blueprint->floatsOnWater) + terrainOffset;
	}

	// compute initial vertical velocity such that the missile hits the target in its trajectory
	Vector3 hvec = (dest - this->order->gameObject->position);
	hvec.y = 0.0f;
	const Vector3& pos_i = this->order->gameObject->position;
	float y_i = pos_i.y, y_b = dest.y;
	float b = hvec.len2xz() / speed; // time when missile hits target on XZ coordinates
	float vy = (y_b - y_i - 0.5f * (-9.81f) * b * b) / b;

	this->msInitialVelocity = hvec.normal2xz() * speed + Vector3(0, vy, 0);
	this->msInitialPosition = pos_i;
	this->msStartTime = Server::instance->timeManager.currentTime;
	this->order->gameObject->startTrajectory(msInitialPosition, msInitialVelocity, msStartTime);
	this->msPrevPosition = this->msInitialPosition;
}

static bool LineSegmentIntersectsSphere(const Vector3& lineStart, const Vector3& lineEnd, const Vector3& center, float radius)
{
	float lineLength = (lineEnd - lineStart).len3();
	Vector3 lineDir = (lineEnd - lineStart).normal();
	Vector3 smc = lineStart - center;
	float ddt = lineDir.dot(smc);
	float delta = ddt * ddt - (smc.sqlen3() - radius * radius);
	if (delta < 0.0f)
		return false;
	float k1 = -ddt + sqrtf(delta),
		k2 = -ddt - sqrtf(delta);
	if ((k1 >= 0.0f && k1 <= lineLength) || (k2 >= 0.0f && k2 <= lineLength))
		return true;
	return false;
}

// wkbai's wildfires: a flaming missile (flaming arrows, Greek fire, fire trebuchets) coming down
// in a heatwave or drought may set the land alight (ServerEnvironment::fireLanded)
static void FlamingMissileLanded(ServerGameObject* missile)
{
	Server* server = Server::instance;
	if (!server->environment.enabled() || server->environment.fireRisk() <= 0.0f)
		return;
	const std::string& n = missile->blueprint->name;
	auto has = [&](const char* s) {
		return std::search(n.begin(), n.end(), s, s + strlen(s), [](char a, char b) { return std::tolower((unsigned char)a) == std::tolower((unsigned char)b); }) != n.end();
	};
	if (has("flaming") || has("fire") || has("napatha") || has("burning"))
		server->environment.fireLanded(*server, missile->position);
}

void MissileTask::onUpdate()
{
	ServerGameObject* go = this->order->gameObject;

	Vector3 prevPosition = msPrevPosition;
	msPrevPosition = go->position;

	// unit collision
	NNSearch nns;
	nns.start(Server::instance, go->position, 15.0f);
	while (ServerGameObject* col = (ServerGameObject*)nns.next()) {
		if (col->blueprint->bpClass == Tags::GAMEOBJCLASS_BUILDING || col->blueprint->bpClass == Tags::GAMEOBJCLASS_CHARACTER) {
			if (col->getPlayer() != go->getPlayer() && col->isInteractable()) {
				if (Model* model = col->blueprint->getModel(col->subtype, col->appearance, col->animationIndex, col->animationVariant)) {
					Vector3 sCenter = model->getSphereCenter().transform(col->getWorldMatrix());
					float sRadius = model->getSphereRadius();
					if (LineSegmentIntersectsSphere(prevPosition, go->position, sCenter, sRadius)) {
						SrvScriptContext ctx(Server::instance, go);
						auto _ = ctx.change(ctx.collisionSubject, col);
						FlamingMissileLanded(go);
						this->blueprint->collisionTrigger.run(&ctx);
						return;
					}
				}
			}
		}
	}

	// floor collision
	float theight = Server::instance->terrain->getHeight(go->position.x, go->position.z);
	if (go->position.y < theight) {
		// TODO: we could play sound directly on client without sending packet
		// (which is probably why strikeFloorSound was made for WKB in the first place)
		if (go->blueprint->strikeFloorSound != -1) {
			go->playSoundAtObject(go->blueprint->strikeFloorSound, go);
		}
		FlamingMissileLanded(go);
		this->blueprint->struckFloorTrigger.run(go);
	}
}

void MoveTask::onStart()
{
}

void MoveTask::onUpdate()
{
	ServerGameObject* go = this->order->gameObject;
	const Vector3& dest = this->target ? this->target->position : this->destination;
	if ((go->position - dest).sqlen2xz() < 0.1f) {
		terminate();
	}
	// (a second between searches that found no way, as ObjectReferenceTask's; a search runs at
	// the end of the step, and its unit is moving, pending, until then)
	if (go->movementController.takeSearchFailed())
		nextMoveAttempt = Server::instance->timeManager.currentTime + 1.0f;
	if (!go->movementController.isMoving() && Server::instance->timeManager.currentTime >= nextMoveAttempt) {
		destination = go->movementController.startMovement(dest);
		if (!go->movementController.isMoving())
			nextMoveAttempt = Server::instance->timeManager.currentTime + 1.0f;
	}
}

// Horizontal distance from a point to the nearest tile of a building's footprint (0 inside it);
// -1 when the object isn't a building with a footprint. Only the blocked tiles count, the
// building itself: the passable ones are its yard (the manor's reach 15 m out in front, to just
// inside its palisade's gate, and a cart delivering within 6.5 m of them unloaded at the gate).
// A footprint that is all passable (a field) counts whole.
static float FootprintDistance(const Vector3& point, const ServerGameObject* building)
{
	if (!building || building->blueprint->bpClass != Tags::GAMEOBJCLASS_BUILDING || !building->blueprint->footprint)
		return -1.0f;
	const Footprint* fp = building->blueprint->footprint;
	if (fp->tiles.empty())
		return -1.0f;
	const auto rotOrigin = fp->rotateOrigin(building->orientation.y);
	const int ox = (int)((building->position.x - rotOrigin.first) / 5.0f);
	const int oz = (int)((building->position.z - rotOrigin.second) / 5.0f);
	const bool anyBlocked = std::any_of(fp->tiles.begin(), fp->tiles.end(), [](const Footprint::TileOffset& to) { return !to.mode; });
	float best = 1e30f;
	for (const auto& to : fp->tiles) {
		if (anyBlocked && to.mode)
			continue;
		const auto [rx, rz] = to.rotate(building->orientation.y);
		const float x0 = (ox + rx) * 5.0f, z0 = (oz + rz) * 5.0f;
		const float dx = std::max({ x0 - point.x, 0.0f, point.x - (x0 + 5.0f) });
		const float dz = std::max({ z0 - point.z, 0.0f, point.z - (z0 + 5.0f) });
		best = std::min(best, dx * dx + dz * dz);
	}
	return std::sqrt(best);
}

// Attack paths (ATTACHMENT_PATH_CATEGORY ATTACK). The target's model has its attack points as
// attachment points "AT_<path>_<point>" (the Training Dummy: AT_1_1 .. AT_4_1, two in front of it
// and two behind, 2.2 m out). WKB.exe (0x568370 with 0x6343e0, docs/engine/training-dummy.md)
// gives a unit the path with the fewest units on it, of those the one whose point is nearest
// to the unit, skipping points outside the playable area; the target keeps how many units are
// on each of its paths, and a unit gives its path back when its task ends or changes target.
// Several units on one path share it (the original lets them; Training.cpp caps a dummy's
// trainees at 4, so each has a path of its own).
// wkbai uses the paths for a unit of the target's own player (training) only: attackers of a
// building still come at it from its footprint's edge (as deliverers do, see FootprintReach).
// The count per target and path, by target id (the server's objects; not saved: a loaded unit
// takes a path again)
static std::map<uint32_t, std::map<int, int>> s_attackPathUsers;

void Task::releaseAttackPath()
{
	if (!attackPathOf)
		return;
	auto it = s_attackPathUsers.find(attackPathOf);
	if (it != s_attackPathUsers.end()) {
		auto p = it->second.find(attackPath);
		if (p != it->second.end() && --p->second <= 0)
			it->second.erase(p);
		if (it->second.empty())
			s_attackPathUsers.erase(it);
	}
	attackPathOf = 0;
	attackPath = -1;
}

// The attack path's point a task's unit goes to (its own path, taken at the first call), or false
// where the task, target or unit has none.
static bool AttackPathSpot(Task* task, ServerGameObject* unit, ServerGameObject* target, Vector3& spot)
{
	if (!task->blueprint->attackPath || !target || target->blueprint->bpClass != Tags::GAMEOBJCLASS_BUILDING
		|| unit->getPlayer() != target->getPlayer())
		return false;
	Model* model = target->getModel();
	if (!model)
		return false;
	// the paths' first points, by path number
	std::map<int, Vector3> points;
	const Matrix world = target->getWorldMatrix();
	for (size_t i = 0; i < model->getNumAPs(); i++) {
		const std::string& tag = model->getAPInfo(i).tag;
		if (tag.size() < 4 || tag.compare(0, 3, "AT_") != 0)
			continue;
		const int path = atoi(tag.c_str() + 3);
		const size_t sep = tag.find('_', 3);
		const int point = sep == std::string::npos ? 0 : atoi(tag.c_str() + sep + 1);
		if (point > 1 && points.count(path))
			continue;
		points[path] = model->getAPInfo(i).staticState.position.transform(world);
	}
	if (points.empty())
		return false;
	if (task->attackPathOf != target->id || !points.count(task->attackPath)) {
		task->releaseAttackPath();
		auto& users = s_attackPathUsers[target->id];
		const auto area = Server::instance->terrain ? Server::instance->terrain->getPlayableArea() : std::make_pair(1e9f, 1e9f);
		int best = -1, bestUsers = 0;
		float bestDist = 0.0f;
		for (const auto& [path, at] : points) {
			if (at.x < 0.0f || at.z < 0.0f || at.x >= area.first || at.z >= area.second)
				continue;
			const auto u = users.find(path);
			const int n = u == users.end() ? 0 : u->second;
			const float d = (at - unit->position).sqlen2xz();
			if (best < 0 || n < bestUsers || (n == bestUsers && d < bestDist)) {
				best = path;
				bestUsers = n;
				bestDist = d;
			}
		}
		if (best < 0) {
			if (users.empty())
				s_attackPathUsers.erase(target->id);
			return false;
		}
		users[best]++;
		task->attackPath = best;
		task->attackPathOf = target->id;
	}
	spot = points[task->attackPath];
	if (Server::instance->terrain)
		spot.y = Server::instance->terrain->getHeight(spot.x, spot.z);
	return true;
}

// How near to a building's footprint a task's unit goes when it has no attachment path there
// (ChooseAttachmentPath: a model without points of the task's category, or a last point it
// couldn't reach). A delivery (ATTACHMENT_PATH_CATEGORY DELIVERY) to a building with a yard comes
// up to the building, 3.5 m: with the task's 6.5 m the cart stopped in the middle of the yard
// (play test 3)
static float FootprintReach(const TaskBlueprint* task, float proximity, const ServerGameObject* building)
{
	constexpr float DELIVERY_REACH = 3.5f;
	if (!task->deliveryPath || proximity <= DELIVERY_REACH || !building->blueprint->footprint)
		return proximity;
	const auto& tiles = building->blueprint->footprint->tiles;
	const bool yard = std::any_of(tiles.begin(), tiles.end(), [](const Footprint::TileOffset& to) { return to.mode; })
		&& std::any_of(tiles.begin(), tiles.end(), [](const Footprint::TileOffset& to) { return !to.mode; });
	return yard ? DELIVERY_REACH : proximity;
}

// The attachment-point prefix of a task's ATTACHMENT_PATH_CATEGORY (WKB.exe's table at 0x5696a0:
// EE_ entry/exit, DE_ delivery, AT_ attack, CR_, AC_ activity); null for the others
static const char* AttachmentPathPrefix(const TaskBlueprint* task)
{
	return task->activityPath ? "AC_" : task->deliveryPath ? "DE_" : nullptr;
}

// A target's attachment paths of a prefix, in path-number order, each its points in order (WKB.exe
// 0x568100 reads "AC_<path>_<point>" as path * 200 + point, so the points come in that order), in
// world coordinates
static std::vector<std::pair<int, std::vector<Vector3>>> AttachmentPaths(const ServerGameObject* target, const char* prefix)
{
	std::vector<std::pair<int, std::vector<Vector3>>> paths;
	Model* model = target->blueprint->getModel(target->subtype, target->appearance, target->animationIndex, target->animationVariant);
	if (!model)
		return paths;
	std::map<int, std::map<int, Vector3>> points;
	// placed by the object's position and turn and its type's SCALE_APPEARANCE, as its matrix
	// (getWorldMatrix) but without the random size variation: that is drawn when a level loads,
	// before the game's seed (a tree's points came out 30% apart in two games of one process,
	// test 27)
	const float c = std::cos(-target->orientation.y), sn = std::sin(-target->orientation.y);
	const Vector3& sc = target->blueprint->scaleAppearance;
	auto place = [&](const Vector3& p) {
		return Vector3(p.x * sc.x * c + p.z * sc.z * sn + target->position.x, p.y * sc.y + target->position.y,
			-p.x * sc.x * sn + p.z * sc.z * c + target->position.z);
	};
	const size_t numAPs = model->getNumAPs();
	for (size_t i = 0; i < numAPs; i++) {
		const AttachmentPoint& ap = model->getAPInfo(i);
		if (ap.tag.size() < 4 || StrCICompare(ap.tag.substr(0, 3), prefix) != 0)
			continue;
		int path = 0, point = 1;
		if (sscanf(ap.tag.c_str() + 3, "%d_%d", &path, &point) < 1)
			continue;
		points[path][point] = place(ap.staticState.position);
	}
	for (auto& [path, byPoint] : points) {
		paths.emplace_back(path, std::vector<Vector3>());
		for (auto& [point, pos] : byPoint)
			paths.back().second.push_back(pos);
	}
	return paths;
}

// Gives the task's unit one of its target's attachment paths, as WKB.exe's 0x6343e0: of the
// paths whose first point the unit can stand on (on the map, ground it may walk), the one the
// fewest other units are on (their current task holds it), and of those the one whose first
// point is nearest the unit. None when the target has no points of the task's category.
static void ChooseAttachmentPath(Task* task)
{
	task->pathChosen = true;
	task->pathNo = -1;
	task->pathPoints.clear();
	task->pathStep = 0;
	task->pathStepTried = false;
	ServerGameObject* target = task->target.get();
	const char* prefix = AttachmentPathPrefix(task->blueprint);
	// WKB_NO_ATTACHMENT_PATHS=1: none (for an A/B of the AI's economy against the old rules)
	// (=AC or =DE: only that category's)
	static const char* noPaths = getenv("WKB_NO_ATTACHMENT_PATHS");
	if (!target || !prefix || (noPaths && (!*noPaths || *noPaths == '1' || !strncmp(noPaths, prefix, 2))))
		return;
	// (not on a target that walks: its points are placed once, here)
	if (target->blueprint->bpClass == Tags::GAMEOBJCLASS_CHARACTER || target->blueprint->bpClass == Tags::GAMEOBJCLASS_FORMATION)
		return;
	task->pathTargetId = target->id;
	ServerGameObject* unit = task->order->gameObject;
	auto paths = AttachmentPaths(target, prefix);
	if (paths.empty())
		return;
	// the other units' paths on this target (each unit once, by its current task)
	std::map<int, int> users;
	std::set<const ServerGameObject*> counted;
	for (const SrvGORef& ref : target->referencers) {
		ServerGameObject* other = ref.get();
		if (!other || other == unit || !counted.insert(other).second)
			continue;
		Order* o = other->orderConfig.getCurrentOrder();
		Task* t = o ? o->getCurrentTask() : nullptr;
		if (t && t->target.get() == target && t->pathChosen && t->pathNo >= 0 && t->pathTargetId == target->id
			&& AttachmentPathPrefix(t->blueprint) == prefix)
			users[t->pathNo]++;
	}
	Terrain* trn = Server::instance->terrain;
	const auto playable = trn ? trn->getNumPlayableTiles() : std::make_pair(0, 0);
	int best = -1, bestUsers = 0;
	float bestDist = 0.0f;
	for (size_t i = 0; i < paths.size(); i++) {
		const Vector3& first = paths[i].second.front();
		if (trn) {
			const int tx = (int)std::floor(first.x / 5.0f), tz = (int)std::floor(first.z / 5.0f);
			if (tx < 0 || tz < 0 || tx >= playable.first || tz >= playable.second
				|| MovementController::isTerrainBlocked(unit->blueprint, tx, tz))
				continue;
		}
		const int n = users.count(paths[i].first) ? users[paths[i].first] : 0;
		const float d = (first - unit->position).sqlen2xz();
		if (best < 0 || n < bestUsers || (n == bestUsers && d < bestDist)) {
			best = (int)i;
			bestUsers = n;
			bestDist = d;
		}
	}
	if (best < 0)
		return;
	task->pathNo = paths[best].first;
	static const bool trace = getenv("WKB_TRACE_PATHS") != nullptr;
	if (trace)
		printf("PATH t=%.2f %u (%.2f, %.2f) takes %s%d of %s #%u (%zu paths, %d on it), first point (%.2f, %.2f), last (%.2f, %.2f)\n",
			Server::instance->timeManager.currentTime, unit->id, unit->position.x, unit->position.z, prefix, task->pathNo,
			target->blueprint->name.c_str(), target->id, paths.size(), bestUsers,
			paths[best].second.front().x, paths[best].second.front().z, paths[best].second.back().x, paths[best].second.back().z);
	task->pathPoints = std::move(paths[best].second);
}

void RestoreAttachmentPath(Task* task, uint32_t targetId, int pathNo, size_t step, bool tried)
{
	task->pathChosen = true;
	task->pathTargetId = targetId;
	task->pathNo = -1;
	task->pathPoints.clear();
	task->pathStep = 0;
	task->pathStepTried = false;
	ServerGameObject* target = task->target.get();
	const char* prefix = AttachmentPathPrefix(task->blueprint);
	if (pathNo < 0 || !target || !prefix || target->id != targetId)
		return;
	for (auto& [no, points] : AttachmentPaths(target, prefix))
		if (no == pathNo && !points.empty()) {
			task->pathNo = pathNo;
			task->pathPoints = std::move(points);
			task->pathStep = std::min(step, task->pathPoints.size() - 1);
			// (not the walk asked for: a loaded unit stands, its movement isn't saved, and standing
			// after an asked walk counts as the walk ended: it skipped the point, gave the last up)
			(void)tried;
			return;
		}
}

void ObjectReferenceTask::onStart()
{
	this->pathChosen = false;
	this->pathNo = -1;
	this->pathPoints.clear();
	this->pathStep = 0;
	this->pathStepTried = false;
	this->proximitySatisfied = false; // might need to do this on resumption too
	if (target)
		this->destination = target->position;
}

void ObjectReferenceTask::onUpdate()
{
	// a terminated target is rejected first, so a new one is looked for at once: a gold or
	// stone outcropping another miner emptied and removed is still there, terminated, in this
	// miner's update, and with no new target TERMINATE_ENTIRE_ORDER_IF_NO_TARGET sent the miner
	// idle (half loaded, beside more outcroppings)
	if (blueprint->rejectTargetIfItIsTerminated) {
		if (this->target && (this->target->flags & ServerGameObject::fTerminated))
			this->setTarget(nullptr);
	}
	if (!this->target) {
		this->stopTriggers();
		ServerGameObject* go = this->order->gameObject;
		if (go->movementController.isMoving())
			go->movementController.stopMovement();
		if (blueprint->taskTarget) {
			SrvScriptContext ctx(Server::instance, this->order->gameObject);
			ServerGameObject* found = blueprint->taskTarget->getFirst(&ctx);
			if (found && blueprint->rejectTargetIfItIsTerminated && (found->flags & ServerGameObject::fTerminated))
				found = nullptr;
			this->setTarget(found);
			// reaching the new one runs the proximity satisfied sequence again (the miner's
			// "Gold Location" marker moves to it)
			this->proximitySatisfied = false;
		}
	}
	if (this->target) {
		ServerGameObject* go = this->order->gameObject;
		// Following (Follow, guarding): the game set gives the follower the engine items "Follow
		// X Offset" and "Follow Y Offset" (random, -10..10; Y -30 for sappers, behind the target),
		// so it keeps that offset to the target's side and front instead of walking into it.
		// Applied where the target is a character or formation of the same player and the
		// task wants to reach it exactly (proximity 0).
		Vector3 goal = target->position;
		const bool following = this->proximity == 0.0f && go->getPlayer() == target->getPlayer()
			&& (target->blueprint->bpClass == Tags::GAMEOBJCLASS_CHARACTER || target->blueprint->bpClass == Tags::GAMEOBJCLASS_FORMATION);
		if (following) {
			static const GameSet* itemsOf = nullptr;
			static int followX = -1, followY = -1;
			if (itemsOf != Server::instance->gameSet.get()) {
				itemsOf = Server::instance->gameSet.get();
				followX = itemsOf->items.names.getIndex("Follow X Offset");
				followY = itemsOf->items.names.getIndex("Follow Y Offset");
			}
			const float ox = followX >= 0 ? go->getItem(followX) : 0.0f;
			const float oy = followY >= 0 ? go->getItem(followY) : 0.0f;
			if (ox != 0.0f || oy != 0.0f) {
				const float yaw = target->orientation.y;
				const Vector3 forward(std::sin(yaw), 0.0f, -std::cos(yaw)), right(std::cos(yaw), 0.0f, std::sin(yaw));
				goal = target->position + right * ox + forward * oy;
				if (Server::instance->terrain)
					goal.y = Server::instance->terrain->getHeightEx(goal.x, goal.z, go->blueprint->canWalkOnWater());
			}
		}
		// An attack path (training at a dummy): its point
		Vector3 spot;
		const bool onAttack = !following && AttackPathSpot(this, go, target.get(), spot);
		if (onAttack)
			goal = spot;
		// An attachment-path task (ACTIVITY_PATH, DELIVERY) on a target with points of its category
		// walks the unit along one of the target's paths, point by point, and works at its last
		// point (WKB.exe 0x50d420 asks for the path, 0x6c83f0/0x568370; with it the distance test
		// 0x50d9d0 doesn't count). The original's deliverers stand exactly on a DE point (0.0 m off
		// in its saves); shop keepers at the end of a DE path along the shop's side, farmers at an
		// AC path's end inside the field, woodcutters 1.7 m from the tree, miners 2-3.5 m from the
		// outcropping's centre.
		if (!following && !onAttack && AttachmentPathPrefix(blueprint) && (!pathChosen || pathTargetId != target->id))
			ChooseAttachmentPath(this);
		// a last point the walk can't get within 3 m of (walls or buildings round it, ground the
		// unit can't stand on) is given up: the target is then reached by the task's proximity, as one
		// without points
		if (!following && !onAttack && pathChosen && pathNo >= 0 && !pathPoints.empty() && pathStep + 1 == pathPoints.size() && pathStepTried
			&& !go->movementController.isMoving() && (go->position - pathPoints[pathStep]).sqlen2xz() >= 9.0f) {
			static const bool trace = getenv("WKB_TRACE_PATHS") != nullptr;
			if (trace)
				printf("PATH %u gave up %s%d_%zu of %s #%u, %.1f m short\n", go->id, AttachmentPathPrefix(blueprint), pathNo, pathStep + 1,
					target->blueprint->name.c_str(), target->id, (go->position - pathPoints[pathStep]).len2xz());
			pathNo = -1;
			pathPoints.clear();
		}
		const bool onPath = !following && !onAttack && pathChosen && pathNo >= 0 && !pathPoints.empty();
		if (onPath) {
			// on to the next point once at this one, or once the walk to it has ended (its tile
			// taken by something: a field's outer point on a wall or a neighbour's footprint)
			while (pathStep + 1 < pathPoints.size()) {
				const float d2 = (go->position - pathPoints[pathStep]).sqlen2xz();
				if (d2 < 1.0f || (!go->movementController.isMoving() && pathStepTried))
					{ pathStep++; pathStepTried = false; }
				else
					break;
			}
			goal = pathPoints[pathStep];
		}
		float prox = this->proximity;
		if (prox >= 0.0f) prox = std::max(prox - (destination - goal).len2xz(), 0.1f);
		// a follower is there while it stands near the target's current goal (the last
		// movement's destination stays behind when the target walks on)
		constexpr float FOLLOW_TOLERANCE = 1.5f;
		// A building with a footprint is reached from its edge (its footprint's tiles), not its
		// centre: builders, deliverers and attackers stand around it (the original sends them to
		// its activity path) and a large building's centre can't be come near
		const float edge = FootprintDistance(go->position, target.get());
		const float reach = edge >= 0.0f ? FootprintReach(blueprint, this->proximity, target.get()) : this->proximity;
		// on a path: at its last point, where the walk there ends (or near it, when the walk ended
		// a little short: the point's tile taken); an attack path's point exactly (the trainees stand
		// where the original puts them, 2.2 m from the dummy's centre; within a metre they swung at it
		// from beside it)
		const float spotDist2 = (onPath || onAttack) ? (go->position - goal).sqlen2xz() : 0.0f;
		const bool measured = onAttack ? (spotDist2 < 0.04f || (!go->movementController.isMoving() && spotDist2 < 9.0f))
			: onPath ? (pathStep + 1 == pathPoints.size()
					&& (spotDist2 < 0.01f || (!go->movementController.isMoving() && pathStepTried && spotDist2 < 9.0f)))
			: following ? (go->position - goal).sqlen2xz() < FOLLOW_TOLERANCE * FOLLOW_TOLERANCE
			: (prox < 0.0f || (edge >= 0.0f
				// within the proximity of the edge, or, when the path ends a little further out
				// (its last tile next to the footprint was taken), within one tile more
				? (edge < reach || (!go->movementController.isMoving() && edge < reach + 5.0f))
				: (go->position - destination).sqlen2xz() < prox * prox));
		const bool arrived = loadedArrived || measured;
		if (arrived) {
			this->startTriggers();
			if (go->movementController.isMoving())
				go->movementController.stopMovement();
			if (!proximitySatisfied) {
				proximitySatisfied = true;
				TraceTask(this, "proximity satisfied");
				blueprint->proximitySatisfiedSequence.run(order->gameObject);
				blueprint->movementCompletedSequence.run(order->gameObject);

				// Set the task animation
				int animtoplay = -1;
				for (const auto& ea : blueprint->equAnims) {
					SrvScriptContext ctx(Server::instance, this->order->gameObject);
					if (Server::instance->gameSet->equations[ea.first]->booleval(&ctx)) {
						animtoplay = ea.second;
						break;
					}
				}
				if (animtoplay == -1)
					animtoplay = blueprint->defaultAnim;
				if (animtoplay != -1)
					go->setAnimation(animtoplay, blueprint->playAnimationOnce, blueprint->synchAnimationToFraction ? blueprint->bpid : -1,
						-1.0f, AnimationSpeedToFit(go, animtoplay));

				// Set orientation towards target, TODO: update when target moves (perhaps on Client?)
				if (go->blueprint->bpClass == Tags::GAMEOBJCLASS_CHARACTER) {
					Vector3 dir = (target->position - go->position).normal2xz();
					if (!(dir.x == 0.0f && dir.z == 0.0f))
						go->setOrientation(Vector3(0, std::atan2(dir.x, -dir.z), 0));
				}
			}
		}
		else {
			ServerGameObject* obj = order->gameObject;
			Model* model = obj->blueprint->getModel(obj->subtype, obj->appearance, obj->animationIndex, obj->animationVariant);
			float period = (model ? model->getDuration() : 0.2f) / obj->animSpeed;
			if (obj->animationIndex == 0 || Server::instance->timeManager.currentTime >= obj->animStartTime + period) {
				// only then the proximity can be dissatisfied

				this->stopTriggers();
				if (go->movementController.takeSearchFailed())
					nextMoveAttempt = Server::instance->timeManager.currentTime + 1.0f;
				if (!go->movementController.isMoving() && Server::instance->timeManager.currentTime >= nextMoveAttempt) {
					destination = go->movementController.startMovement(goal);
					pathStepTried = onPath;
					if (!go->movementController.isMoving())
						nextMoveAttempt = Server::instance->timeManager.currentTime + 1.0f;
				}
				// If the target moves during the movement, restart the movement. Not for every
				// 10 cm it moves: each restart is a path search, and late in a 6-AI game the
				// fights' chasers searched every step (a third of the simulation's time). Only
				// once the target is a tenth of the way off where the path goes, and at least
				// 2 m: near it that is every few steps, far off every few seconds. (Against the goal
				// asked for, not the path's end: a search cut short ends far from it, and those
				// units searched again every step whatever the target did.)
				if (target->blueprint->bpClass == Tags::GAMEOBJCLASS_CHARACTER)
					if (go->movementController.isMoving()) {
						const float off = std::max(2.0f, 0.1f * (goal - go->position).len2xz());
						if ((goal - go->movementController.getRequestedDestination()).sqlen2xz() > off * off)
							destination = go->movementController.startMovement(goal);
					}
				if (proximitySatisfied) {
					proximitySatisfied = false;
					if (getenv("WKB_TRACE_TASKS_ID")) {
						char buf[160];
						snprintf(buf, sizeof(buf), "proximity dissatisfied (edge %.2f, proximity %.2f, distance %.2f)", edge, this->proximity, (go->position - target->position).len2xz());
						TraceTask(this, buf);
					}
					//if (blueprint->defaultAnim != -1)
					//	go->setAnimation(0);
					RunProximityDissatisfied(blueprint, order->gameObject);
				}
			}
		}
		if (this->triggersStarted) {
			for (auto& trigger : this->triggers)
				trigger->update();
		}
	}
	else {
		terminate();
		if (blueprint->terminateEntireOrderIfNoTarget)
			order->terminate();
	}
}

void FaceTowardsTask::onUpdate()
{
	ServerGameObject* obj = this->order->gameObject;
	if (target || hasFaceTo) {
		const Vector3 dir = (target ? target->position : faceTo) - obj->position;
		if (dir.sqlen2xz() > 0.01f)
			obj->setOrientation(Vector3(0.0f, std::atan2(dir.x, -dir.z), 0.0f));
	}
	terminate();
}

SpawnTask::~SpawnTask()
{
	if (!this->isSpawned) {
		ServerGameObject* obj = this->order->gameObject;
		ServerGameObject* player = obj->getPlayer();
		player->reclaimObjectCost(toSpawn);
	}
}

void SpawnTask::setSpawnBlueprint(const GameObjBlueprint* blueprint)
{
	toSpawn = blueprint;
	ServerGameObject* obj = order->gameObject;
	ServerGameObject* player = obj->getPlayer();
	player->payObjectCost(toSpawn);
}

void SpawnTask::onStart()
{
	ServerGameObject* obj = order->gameObject;
	obj->setItem(Tags::PDITEM_HIT_POINTS_OF_OBJECT_BEING_SPAWNED, 0.0f);
	obj->setItem(Tags::PDITEM_HIT_POINT_CAPACITY_OF_OBJECT_BEING_SPAWNED, toSpawn->startItemValues.at(Tags::PDITEM_HIT_POINT_CAPACITY));
}

// The spot nearest to a place where a unit can stand clear of the other characters there or
// walking there (rings 3 m apart around it, walkable tiles only): the units a building sends
// to its rally point gather around it instead of on top of each other.
Vector3 FreeStandingSpot(ServerGameObject* unit, const Vector3& place)
{
	constexpr float SPACING = 3.0f;
	constexpr int RINGS = 10;
	std::vector<Vector3> taken;
	NNSearch nns;
	nns.start(Server::instance, place, SPACING * RINGS + 40.0f);
	while (CommonGameObject* o = nns.next()) {
		ServerGameObject* other = o->dyncast<ServerGameObject>();
		// (corpses, "Dead ..." characters terminated where they fell, take no spot)
		if (other == unit || other->blueprint->bpClass != Tags::GAMEOBJCLASS_CHARACTER || !other->isInteractable())
			continue;
		if (other->movementController.isMoving())
			taken.push_back(other->movementController.getDestination());
		else
			taken.push_back(other->position);
	}
	auto isFree = [&](const Vector3& spot) {
		if (MovementController::isTileBlocked(unit->blueprint, (int)(spot.x / 5.0f), (int)(spot.z / 5.0f)))
			return false;
		for (const Vector3& t : taken)
			if ((t - spot).sqlen2xz() < SPACING * SPACING * 0.8f)
				return false;
		return true;
	};
	for (int ring = 0; ring <= RINGS; ring++) {
		const int count = ring == 0 ? 1 : (int)(6.2831853f * ring);
		for (int i = 0; i < count; i++) {
			const float angle = 6.2831853f * i / count;
			const Vector3 spot = place + Vector3(std::cos(angle), 0, std::sin(angle)) * (ring * SPACING);
			if (isFree(spot))
				return spot;
		}
	}
	return place;
}

// The object a building's spawned units take instead of their gone target: within 70 m of where
// that was (the building, if not known), the nearest that the command's cursor conditions accept
// for the new unit, as a right-click on it would (for "Chop Tree": a tree with wood and fewer than
// 5 woodcutters). Nearest first, ties by id. None: null.
static ServerGameObject* RallyTargetInstead(ServerGameObject* building, const Command* command, ServerGameObject* unit)
{
	if (command->cursorConditions.empty())
		return nullptr; // nothing tells what fits
	const Vector3 place = building->spawnedUnitTargetPlace.x >= 0.0f ? building->spawnedUnitTargetPlace : building->position;
	constexpr float REACH = 70.0f; // as the game set's "Unoccupied Tree closest to Target"
	std::vector<std::pair<float, ServerGameObject*>> near;
	for (const auto& [id, o] : Server::instance->idmap) {
		ServerGameObject* so = (ServerGameObject*)o;
		if (!so || so == unit || so == building || so->deleted || (so->flags & ServerGameObject::fTerminated))
			continue;
		const float d = (so->position - place).sqlen2xz();
		if (d < REACH * REACH)
			near.emplace_back(d, so);
	}
	std::sort(near.begin(), near.end(), [](const auto& a, const auto& b) { return a.first != b.first ? a.first < b.first : a.second->id < b.second->id; });
	const GameSet& gs = *Server::instance->gameSet;
	for (const auto& [d, so] : near) {
		SrvScriptContext ctx(Server::instance, unit);
		auto _1 = ctx.target.change(so);
		auto _2 = ctx.selectedObject.change(unit);
		if (std::all_of(command->cursorConditions.begin(), command->cursorConditions.end(),
			[&](int eq) { return eq < 0 || gs.equations[eq]->booleval(&ctx); }))
			return so;
	}
	return nullptr;
}

void SpawnTask::onUpdate()
{
	this->startTriggers();
	if (this->triggersStarted) {
		for (auto& trigger : this->triggers)
			trigger->update();
	}
	if (this->isDone()) {
		ServerGameObject* obj = order->gameObject;
		if (obj->getItem(Tags::PDITEM_HIT_POINTS_OF_OBJECT_BEING_SPAWNED) >= obj->getItem(Tags::PDITEM_HIT_POINT_CAPACITY_OF_OBJECT_BEING_SPAWNED)) {
			ServerGameObject* spawned = Server::instance->spawnObject(toSpawn, obj->getPlayer(), obj->position, Vector3(0,0,0));
			this->isSpawned = true;
			if (obj->spawnedUnitCommand >= 0) {
				const Command* command = &Server::instance->gameSet->commands[obj->spawnedUnitCommand];
				const auto& spawnedOfferedCommands = spawned->blueprint->offeredCommands;
				const auto it = std::find(spawnedOfferedCommands.begin(), spawnedOfferedCommands.end(), command);
				if (it != spawnedOfferedCommands.end()) {
					ServerGameObject* target = obj->spawnedUnitTarget.getFrom<Server>();
					if (target && (target->deleted || (target->flags & ServerGameObject::fTerminated)))
						target = nullptr;
					// a rally point on a tree that has been felled (or any target gone): the nearest
					// one there that the command takes, which becomes the rally point (the play
					// test: the Manor's new peasants stood idle once their tree was gone)
					const uint32_t had = obj->spawnedUnitTarget.objid;
					if (!target && had != 0 && had != 0xFFFFFFFFu) {
						target = RallyTargetInstead(obj, command, spawned);
						if (target)
							obj->setBuildingSpawnedUnitOrderToTarget(obj->spawnedUnitCommand, target);
					}
					else if (target)
						obj->spawnedUnitTargetPlace = target->position;
					Vector3 destination = obj->spawnedUnitDestination;
					// to a place (the rally point): each unit to a free spot next to it, not all on it
					if (!target && destination.x >= 0.0f)
						destination = FreeStandingSpot(spawned, destination);
					command->execute(spawned, target, Tags::ORDERASSIGNMODE_FORGET_EVERYTHING_ELSE, destination);
				}
			}
			if (ServerGameObject* com = aiCommissioner.get()) {
				com->sendEvent(Tags::PDEVENT_ON_COMMISSIONED, spawned);
			}
		}
	}
}
