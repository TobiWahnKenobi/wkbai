// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#include "Order.h"
#include <functional>
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
#include "AIUtil.h"
#include "Model.h"
#include "PeasantLodging.h"
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
	// (no shipped script has a SUSPENSION_SEQUENCE; mods may, WKB.exe 0x505970 runs it)
	this->blueprint->suspensionSequence.run(this->gameObject);
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
	if (this->scriptSuspended)
		return;
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
	// an order another waits behind (an uninterruptible loop's): it ends with its running task
	// instead of going on to the next or cycling (WKB.exe 0x504989)
	if (finishAfterTask) {
		terminate();
		return;
	}
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
		else if (trigbp.type == Tags::TASKTRIGGER_UNINTERRUPTIBLE_ANIMATION_LOOP) {
			auto loop = std::make_unique<AnimationLoopTrigger>(this, &trigbp);
			loop->uninterruptible = true;
			trigger = std::move(loop);
		}
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
	this->blueprint->suspensionSequence.run(this->order->gameObject); // (WKB.exe 0x5063e0; none shipped)
}

void Task::resume()
{
	TraceTask(this, "resume");
	this->state = OTS_PROCESSING;
	this->blueprint->resumptionSequence.run(this->order->gameObject);
	// only a task that was in range when it was put aside: its target has gone out of range since
	// (an archer drawn into a fight). One that hadn't reached it walks on: the Kingmaker's siege
	// formations, resumed 220 m from their target, ran their dissatisfied sequence (a Wait) again
	// at every resume and never walked.
	this->resumedUnchecked = this->proximitySatisfied;
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
	if (ServerGameObject* t = target.get(); t && attackPathOf == t->id && t->getPlayer() == go->getPlayer())
		go->orderConfig.stepAwayFrom = t->id;
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
	// a peasant on its way into its shelter for the night or winter, inside or waiting for one
	// (PeasantLodging) takes no orders but the lodging's own: not the player's, the AI's or the
	// scripts' (a dying one's "Die" still)
	if (gameobj->lodging != LODGE_NONE && gameobj->lodging != LODGE_RETURNING && !g_lodgingOrders
		&& !(gameobj->flags & CommonGameObject::fTerminated))
		return nullptr;
	// As WKB.exe's 0x562130: an order that would replace the current one (DO_FIRST, FORGET_EVERYTHING_ELSE)
	// - is refused while the current order can't be interrupted (CANNOT_INTERRUPT_ORDER, 0x53fbd0);
	// - waits behind it while one of its tasks plays an uninterruptible loop (the Rocket Launcher
	//   limbering, the siege tower's climb, the explosive cart's fuse): it is put last, and the
	//   current order ends with its running task;
	// unless it overrides those (OVERRIDE_NON_INTERRUPTIBLE_ORDERS: the deaths)
	if (Order* currentOrder = getCurrentOrder(); currentOrder && assignMode != Tags::ORDERASSIGNMODE_DO_LAST
		&& !orderBlueprint->overrideNonInterruptible) {
		if (currentOrder->blueprint->cannotInterruptOrder)
			return nullptr;
		bool held = false;
		for (const auto& task : currentOrder->tasks)
			for (const auto& trigger : task->triggers)
				if (auto* loop = dynamic_cast<const AnimationLoopTrigger*>(trigger.get()); loop && loop->uninterruptible)
					held = true;
		if (held) {
			currentOrder->finishAfterTask = true;
			assignMode = Tags::ORDERASSIGNMODE_DO_LAST;
		}
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
	// idle on the spot it trained at: a few metres out from the dummy, to a free place
	if (stepAwayFrom) {
		const uint32_t from = std::exchange(stepAwayFrom, 0u);
		ServerGameObject* t = Server::instance->findObject(from);
		const GameSet* gs = Server::instance->gameSet.get();
		const int move = gs->commands.names.getIndex("Move");
		const CommonGameObject* parent = gameobj->getParent();
		const bool inFormation = parent && parent->blueprint->bpClass == Tags::GAMEOBJCLASS_FORMATION;
		if (orders.empty() && t && !t->deleted && !(t->flags & ServerGameObject::fTerminated) && move >= 0 && !inFormation) {
			Vector3 out = gameobj->position - t->position;
			out.y = 0.0f;
			const float d = std::sqrt(out.sqlen2xz());
			if (d < 6.0f) {
				out = d > 0.01f ? out * (1.0f / d) : Vector3(1, 0, 0);
				const Vector3 dest = FreeStandingSpot(gameobj, t->position + out * 6.0f);
				gs->commands[move].execute(gameobj, nullptr, Tags::ORDERASSIGNMODE_FORGET_EVERYTHING_ELSE, dest);
			}
		}
	}
	nextBusy = !orders.empty();
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
		SrvScriptContext ctx{ Server::instance, obj };
		float frac = Server::instance->gameSet->tasks[obj->animSynchronizedTask].synchAnimationToFraction->eval(&ctx);
		if (frac < 1.0f)
			synchFired = false;
		else if (!synchFired) {
			synchFired = true;
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
	// (WKB.exe's getter 0x5a6450: a speed not above 0 is 2)
	if (!(speed > 0.0f))
		speed = 2.0f;

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
		// (1 m inside the map's edge, as WKB.exe 0x509910)
		dest.x = std::clamp(dest.x, 1.0f, std::max(1.0f, (float)area.first - 1.0f));
		dest.z = std::clamp(dest.z, 1.0f, std::max(1.0f, (float)area.second - 1.0f));
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
	// every object on the launch tile is passed through (WKB.exe 0x509734 / 0x566bd0: the firer,
	// those beside it, the tower a garrison shoots from)
	if (Server* server = Server::instance; server->tiles && server->terrain) {
		int nx, nz;
		std::tie(nx, nz) = server->terrain->getNumPlayableTiles();
		const int tx = (int)(pos_i.x / 5.0f), tz = (int)(pos_i.z / 5.0f);
		if (tx >= 0 && tz >= 0 && tx < nx && tz < nz) {
			auto& tile = server->tiles[tz * nx + tx];
			for (const auto& ref : tile.objList)
				if (CommonGameObject* o = ref.getFrom(server))
					msPassesThrough[o->id] = true;
			if (CommonGameObject* b = tile.building.getFrom(server))
				msPassesThrough[b->id] = true;
		}
	}
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

// Whether a missile passes through an object (WKB.exe 0x50a820, kept per object as the original's
// set at +0x50): its own firer's kind of things aren't spared, only
// - objects whose blueprint has IGNORE_ALL_MISSILE_COLLISION (trees, farms, shipwrecks, palms);
// - with IGNORE_FRIENDLY_MISSILE_COLLISION, the firer's own and its allies';
// Friendly fire: the missile's DO_NOT_COLLIDE_WITH ("Allied Characters") isn't an engine keyword in
// WKB.exe (docs/re/missiles-removal-siege-carts.md A), so arrows and balls hit own and allied
// units in their way.
static bool MissilePassesThrough(ServerGameObject* missile, ServerGameObject* obj)
{
	const GameObjBlueprint* bp = obj->blueprint;
	if (bp->ignoreAllMissileCollision)
		return true;
	Server* server = Server::instance;
	ServerGameObject* own = missile->getPlayer();
	ServerGameObject* theirs = obj->getPlayer();
	const bool allied = own && theirs && (own == theirs
		|| server->getDiplomaticStatus(own, theirs) <= server->gameSet->diplomaticStatuses.names.getIndex("Allied"));
	if (bp->ignoreFriendlyMissileCollision && allied)
		return true;
	return false;
}

// where along the segment a box (low, high; in the segment's frame) is entered: the fraction in
// [0, 1], or -1 if it isn't (the slab test); a segment starting inside counts at 0
static float SegmentBoxEntry(const Vector3& a, const Vector3& b, const Vector3& low, const Vector3& high)
{
	float t0 = 0.0f, t1 = 1.0f;
	for (int c = 0; c < 3; c++) {
		const float lo = std::min(low.coord[c], high.coord[c]), hi = std::max(low.coord[c], high.coord[c]);
		const float d = b.coord[c] - a.coord[c];
		if (std::abs(d) < 1e-8f) {
			if (a.coord[c] < lo || a.coord[c] > hi)
				return -1.0f;
			continue;
		}
		float ta = (lo - a.coord[c]) / d, tb = (hi - a.coord[c]) / d;
		if (ta > tb) std::swap(ta, tb);
		t0 = std::max(t0, ta);
		t1 = std::min(t1, tb);
		if (t0 > t1)
			return -1.0f;
	}
	return t0;
}

void MissileTask::onUpdate()
{
	ServerGameObject* go = this->order->gameObject;
	Server* server = Server::instance;
	if (msLanded)
		return;

	const Vector3 prevPosition = msPrevPosition;
	const Vector3 nowPosition = go->position;
	msPrevPosition = nowPosition;

	// leaving the map: removed, with no trigger (WKB.exe 0x509e20)
	const auto area = server->terrain->getPlayableArea();
	if (nowPosition.x < 0.0f || nowPosition.z < 0.0f || nowPosition.x > (float)area.first || nowPosition.z > (float)area.second) {
		server->deleteObject(go);
		return;
	}

	// The step from the last position is tested against the ground (water counts) and the objects
	// near it; the nearest hit along it wins (WKB.exe 0x60f900, 0x50a570).
	const Vector3 step = nowPosition - prevPosition;
	const float stepLen = step.len3();
	float groundAt = -1.0f;
	{
		auto below = [&](float t) {
			const Vector3 p = prevPosition + step * t;
			return p.y < server->terrain->getHeightEx(p.x, p.z, true);
		};
		const int samples = std::max(1, (int)std::ceil(stepLen / 0.5f));
		float lo = 0.0f;
		for (int i = 1; i <= samples; i++) {
			const float t = (float)i / samples;
			if (below(t)) {
				float hi = t;
				for (int k = 0; k < 8; k++) {
					const float mid = 0.5f * (lo + hi);
					(below(mid) ? hi : lo) = mid;
				}
				groundAt = hi;
				break;
			}
			lo = t;
		}
	}

	ServerGameObject* hitObject = nullptr;
	float hitAt = 2.0f;
	// The candidates are the objects on the tiles the step crosses (WKB.exe 0x60f900 / 0x50a570): a
	// building on every tile of its footprint, the others (characters, containers, props) on the
	// one tile under their centre. Missiles pass through missiles. The filter (0x50a820) reads only
	// whether it's terminated, IGNORE_ALL_MISSILE_COLLISION and IGNORE_FRIENDLY_MISSILE_COLLISION:
	// not targetable, built-upon, corpses and the missile's own target count as anything else.
	auto consider = [&](ServerGameObject* col) {
		if (!col || col == go)
			return;
		const int cls = col->blueprint->bpClass;
		if (!(cls == Tags::GAMEOBJCLASS_BUILDING || cls == Tags::GAMEOBJCLASS_CHARACTER
			|| cls == Tags::GAMEOBJCLASS_PROP || cls == Tags::GAMEOBJCLASS_CONTAINER))
			return;
		if (col->flags & CommonGameObject::fTerminated)
			return;
		auto it = msPassesThrough.find(col->id);
		if (it == msPassesThrough.end())
			it = msPassesThrough.emplace(col->id, col->id == go->creatorId || MissilePassesThrough(go, col)).first;
		if (it->second)
			return;
		Model* model = col->blueprint->getModel(col->subtype, col->appearance, col->animationIndex, col->animationVariant);
		if (!model)
			return;
		// the step against the model's bounding box at its current animation frame, in the object's
		// own frame (position, orientation, scale; 0x5c0aa0, 0x5b84f0), its faces widened by 0.1
		// (0x5cb6f0); a step starting inside counts at once
		const Matrix toLocal = col->getWorldMatrix().getInverse4x3();
		const float now = server->timeManager.currentTime;
		const uint32_t animTime = (uint32_t)std::max(0.0f, (now - col->animStartTime) * col->animSpeed * 1000.0f);
		auto box = model->interpolateBoundingBox(animTime);
		for (int c = 0; c < 3; c++) {
			const float lo = std::min(box[0].coord[c], box[1].coord[c]), hi = std::max(box[0].coord[c], box[1].coord[c]);
			box[0].coord[c] = lo - 0.1f; box[1].coord[c] = hi + 0.1f;
		}
		const float t = SegmentBoxEntry(prevPosition.transform(toLocal), nowPosition.transform(toLocal), box[0], box[1]);
		if (t >= 0.0f && t < hitAt) {
			hitAt = t;
			hitObject = col;
		}
	};
	if (server->tiles) {
		int nx, nz;
		std::tie(nx, nz) = server->terrain->getNumPlayableTiles();
		const float ax = prevPosition.x / 5.0f, az = prevPosition.z / 5.0f, bx = nowPosition.x / 5.0f, bz = nowPosition.z / 5.0f;
		auto onTile = [&](int tx, int tz) {
			if (tx < 0 || tz < 0 || tx >= nx || tz >= nz)
				return;
			auto& tile = server->tiles[(size_t)tz * nx + tx];
			consider(tile.building.getFrom<Server>());
			for (const auto& ref : tile.objList)
				if (ServerGameObject* o = ref.getFrom<Server>(); o && o->blueprint->bpClass != Tags::GAMEOBJCLASS_BUILDING)
					consider(o);
		};
		// the tiles in order along the step (a building met on several of them is tested once a
		// step: msPassesThrough doesn't hold hits, so the nearest entry wins anyway)
		int tx = (int)ax, tz = (int)az;
		const int ex = (int)bx, ez = (int)bz;
		onTile(tx, tz);
		if (tx != ex || tz != ez) {
			const float dx = bx - ax, dz = bz - az;
			const int sx = dx > 0 ? 1 : -1, sz = dz > 0 ? 1 : -1;
			const float tdx = dx != 0.0f ? std::abs(1.0f / dx) : 1e30f, tdz = dz != 0.0f ? std::abs(1.0f / dz) : 1e30f;
			float tmx = dx != 0.0f ? ((sx > 0 ? (tx + 1) - ax : ax - tx) * tdx) : 1e30f;
			float tmz = dz != 0.0f ? ((sz > 0 ? (tz + 1) - az : az - tz) * tdz) : 1e30f;
			for (int guard = 0; guard < 64 && (tx != ex || tz != ez); guard++) {
				if (tmx < tmz) { tx += sx; tmx += tdx; }
				else { tz += sz; tmz += tdz; }
				onTile(tx, tz);
			}
		}
	}

	// a tie goes to the ground (WKB.exe 0x50a570)
	// test runs: WKB_TRACE_MISSILE=1 prints what each missile hit
	static const bool traceMissile = getenv("WKB_TRACE_MISSILE") != nullptr;
	if (traceMissile && (hitObject || groundAt >= 0.0f))
		printf("MISSILE t=%.2f %u %s from %u hit %s %u %s at (%.1f,%.1f,%.1f)\n", server->timeManager.currentTime, go->id, go->blueprint->name.c_str(),
			go->creatorId, hitObject && (groundAt < 0.0f || hitAt < groundAt) ? "object" : "ground",
			hitObject ? hitObject->id : 0, hitObject ? hitObject->blueprint->name.c_str() : "-", nowPosition.x, nowPosition.y, nowPosition.z);
	if (hitObject && (groundAt < 0.0f || hitAt < groundAt)) {
		go->updatePosition(prevPosition + step * hitAt);
		SrvScriptContext ctx(server, go);
		auto _ = ctx.change(ctx.collisionSubject, hitObject);
		FlamingMissileLanded(go);
		this->blueprint->collisionTrigger.run(&ctx);
		return;
	}
	if (groundAt >= 0.0f) {
		// the ground (or water): the missile stops at the hit point, with the blueprint's sound and
		// special effect (STRIKE_FLOOR_* / STRIKE_WATER_SOUND over water)
		Vector3 at = prevPosition + step * groundAt;
		const float floor = server->terrain->getHeightEx(at.x, at.z, true);
		const bool water = floor > server->terrain->getHeight(at.x, at.z) + 0.01f;
		at.y = floor;
		msLanded = true;
		go->trajectory.stop();
		go->updatePosition(at);
		const int sound = water && go->blueprint->strikeWaterSound != -1 ? go->blueprint->strikeWaterSound : go->blueprint->strikeFloorSound;
		if (sound != -1)
			go->playSoundAtObject(sound, go);
		if (!water && go->blueprint->strikeFloorEffect != -1)
			go->playSpecialEffectAt(go->blueprint->strikeFloorEffect, at);
		if (!water)
			FlamingMissileLanded(go);
		this->blueprint->struckFloorTrigger.run(go);
	}
}

void AttackWallInTheWay(ServerGameObject* unit, ServerGameObject* target);

// A formation's first character; a character itself; else none. (Not through armies: with a
// "Formation Army" ending its attack on the scout, the Kingmaker's scripts sent its formations
// back at once and they stood on the slope, PERFORMANCE save, 2026-11-18.)
static ServerGameObject* FirstCharacter(ServerGameObject* obj, int depth = 0)
{
	if (obj->blueprint->bpClass == Tags::GAMEOBJCLASS_CHARACTER)
		return obj->deleted ? nullptr : obj;
	if (depth > 3 || obj->blueprint->bpClass != Tags::GAMEOBJCLASS_FORMATION)
		return nullptr;
	for (const auto& [type, children] : obj->children)
		for (CommonGameObject* c : children)
			if (ServerGameObject* m = FirstCharacter((ServerGameObject*)c, depth + 1))
				return m;
	return nullptr;
}

// Whether an attacker can never get within `reach` metres of a point: no tile that close lies in
// the attacker's terrain region (the ground its movement class can stand on, 8-connected; from
// the terrain alone, buildings only block more, so another region is out of reach for good). A
// formation goes by its first member. The AI play test of 2026-11-16: a scout on a hilltop no
// spearman could climb drew every army of the enemy's, which stood at the foot of the slope (or
// formed up and broke again) for the rest of the game. Ranged units find a reachable tile within
// their range, and shoot.
bool OutOfReach(ServerGameObject* unit, const Vector3& point, float reach)
{
	ServerGameObject* mover = FirstCharacter(unit);
	if (!mover)
		return false;
	const int cls = MovementController::movementClass(mover->blueprint);
	const int own = MovementController::terrainRegion(cls, (int)(mover->position.x / 5.0f), (int)(mover->position.z / 5.0f));
	if (own < 0)
		return false; // standing where its class can't (pushed, spawned): no judgement
	const int r = std::clamp((int)std::ceil(reach / 5.0f), 1, 12);
	const int cx = (int)(point.x / 5.0f), cz = (int)(point.z / 5.0f);
	for (int dz = -r; dz <= r; dz++)
		for (int dx = -r; dx <= r; dx++) {
			// the tile's nearest point within reach of the point
			const float nx = std::clamp(point.x, (cx + dx) * 5.0f, (cx + dx + 1) * 5.0f), nz = std::clamp(point.z, (cz + dz) * 5.0f, (cz + dz + 1) * 5.0f);
			if ((nx - point.x) * (nx - point.x) + (nz - point.z) * (nz - point.z) > reach * reach)
				continue;
			if (MovementController::terrainRegion(cls, cx + dx, cz + dz) == own)
				return false;
		}
	return true;
}

// (gameset/actions.cpp, an AI army's victims) whether an army's soldiers can never get within
// reach of the target: its first character, through its formations and the armies under it (an
// archer's 30 m, else 3 m). A target that isn't a character or formation (a building: walls and
// gates are attacked from beside them) is always in reach.
bool ArmyCannotReach(ServerGameObject* army, ServerGameObject* target)
{
	if (!army || !target || (target->blueprint->bpClass != Tags::GAMEOBJCLASS_CHARACTER && target->blueprint->bpClass != Tags::GAMEOBJCLASS_FORMATION))
		return false;
	std::function<ServerGameObject*(ServerGameObject*, int)> first = [&](ServerGameObject* o, int depth) -> ServerGameObject* {
		if (o->blueprint->bpClass == Tags::GAMEOBJCLASS_CHARACTER)
			return o->deleted ? nullptr : o;
		if (depth > 4 || (o->blueprint->bpClass != Tags::GAMEOBJCLASS_FORMATION && o->blueprint->bpClass != Tags::GAMEOBJCLASS_ARMY))
			return nullptr;
		for (const auto& [type, children] : o->children)
			for (CommonGameObject* c : children)
				if (ServerGameObject* m = first((ServerGameObject*)c, depth + 1))
					return m;
		return nullptr;
	};
	ServerGameObject* m = first(army, 0);
	return m && OutOfReach(m, target->position, AIUtil::ClassOf(m) == 2 ? 30.0f : 3.0f);
}

// Kiting (the AI play test of 2026-11-16: "Formation/groups from the enemy chase always the same
// nearest target", so one fast unit led them around the map). wkbai's: an AI player's unit that
// walks after a moving enemy character and hasn't come a metre nearer in 12 s gives up; for 30 s
// it, its formation and its armies leave that enemy out of their victims (actions.cpp) and the
// ministries' choices (AIUtil::CanReach). By the clock, every 2 s per unit; kept for this game
// only (not saved: a loaded game forgets a chase under way and the last 30 s of give-ups).
namespace {
	struct Chase { float checkAt = 0.0f, best = 0.0f, since = 0.0f; };
	std::map<std::pair<uint32_t, uint32_t>, Chase> s_chases;
	std::map<std::pair<uint32_t, uint32_t>, float> s_gaveUp;
	uint64_t s_chaseGame = 0;
	void ChaseTablesForThisGame()
	{
		const uint64_t serial = Server::instance && Server::instance->gameSet ? Server::instance->gameSet->serial : 0;
		if (s_chaseGame != serial) {
			s_chaseGame = serial;
			s_chases.clear();
			s_gaveUp.clear();
		}
	}
}
constexpr float CHASE_CHECK = 2.0f, CHASE_PATIENCE = 12.0f, GAVE_UP_FOR = 30.0f;

bool RecentlyGaveUp(const ServerGameObject* holder, const ServerGameObject* target)
{
	if (!holder || !target)
		return false;
	ChaseTablesForThisGame();
	auto it = s_gaveUp.find({ holder->id, target->id });
	return it != s_gaveUp.end() && Server::instance->timeManager.currentTime < it->second + GAVE_UP_FOR;
}

static bool ChaseGivesUp(ServerGameObject* go, ServerGameObject* target)
{
	ServerGameObject* player = go->getPlayer();
	if (!target || go->blueprint->bpClass != Tags::GAMEOBJCLASS_CHARACTER || target->blueprint->bpClass != Tags::GAMEOBJCLASS_CHARACTER
		|| !player || !player->aiController.isAI() || !AIUtil::IsEnemy(player, target))
		return false;
	ChaseTablesForThisGame();
	const float now = Server::instance->timeManager.currentTime;
	const std::pair<uint32_t, uint32_t> key{ go->id, target->id };
	// only while both walk: a unit at its target, or one standing, isn't chasing
	if (!go->movementController.isMoving() || !target->movementController.isMoving()) {
		s_chases.erase(key);
		return false;
	}
	const float d = (target->position - go->position).len2xz();
	auto [it, fresh] = s_chases.try_emplace(key);
	Chase& c = it->second;
	if (fresh) {
		c.checkAt = now + CHASE_CHECK;
		c.best = d;
		c.since = now;
		return false;
	}
	if (now < c.checkAt)
		return false;
	c.checkAt = now + CHASE_CHECK;
	if (d < c.best - 1.0f) {
		c.best = d;
		c.since = now;
		return false;
	}
	if (now - c.since < CHASE_PATIENCE)
		return false;
	s_chases.erase(key);
	for (ServerGameObject* o = go; o && o->blueprint->bpClass != Tags::GAMEOBJCLASS_PLAYER; o = o->getParent())
		s_gaveUp[{ o->id, target->id }] = now;
	return true;
}

// The reach an attacker needs to get within, from a task's range: a melee unit's (not an
// archer's) at most 3 m. A "Move To Attack Range" walk has a 10 m range before the attack takes
// over, and slope tiles within 10 m of a hilltop scout let a spearman stand 31 m below it for good
// (the play test's PERFORMANCE2 save, 2026-11-17).
static float MeleeReachCap(ServerGameObject* unit, float reach)
{
	ServerGameObject* m = FirstCharacter(unit);
	if (!m || AIUtil::ClassOf(m) == 2)
		return reach;
	return std::min(reach, 3.0f);
}

void MoveTask::onStart()
{
}

void MoveTask::onUpdate()
{
	ServerGameObject* go = this->order->gameObject;
	// A walk ends at a free spot (WKB.exe: the stop point needs a PREFERRED_SPACING disc clear of the
	// other units', 0x436120; the walk itself goes through them, there are no unit collisions):
	// the destination, or the nearest spot round it with room (FreeStandingSpot). Not for a
	// formation's members (their places) or a move after a target.
	Vector3 dest = this->target ? this->target->position : this->destination;
	const bool member = go->getParent() && go->getParent()->blueprint->bpClass == Tags::GAMEOBJCLASS_FORMATION;
	if (!this->target && !member && go->blueprint->bpClass == Tags::GAMEOBJCLASS_CHARACTER) {
		if (!stopChosen || (stopFor - dest).sqlen2xz() > 0.01f) {
			stopFor = dest;
			stopAt = FreeStandingSpot(go, dest);
			stopChosen = true;
		}
		dest = stopAt;
	}
	if ((go->position - dest).sqlen2xz() < 0.1f) {
		terminate();
		return;
	}
	// (a second between searches that found no way, as ObjectReferenceTask's; a search runs at
	// the end of the step, and its unit is moving, pending, until then)
	const bool searchFailed = go->movementController.takeSearchFailed();
	if (searchFailed)
		nextMoveAttempt = Server::instance->timeManager.currentTime + 1.0f;
	// A walk to an enemy (the Attack order's "Move To Attack Range") that ended short of a target
	// it can never get within reach of (OutOfReach; a melee unit's 3 m): the task ends, and the
	// whole order, as if the target were gone. A spearman stood 31 m below a hilltop scout for
	// good, searching once a second (the play test's PERFORMANCE2 save, 2026-11-17).
	if (this->target && !go->movementController.isMoving() && go->getPlayer() && AIUtil::IsEnemy(go->getPlayer(), this->target.get())
		&& (searchFailed || (destination - dest).len2xz() > 2.0f)) {
		const float need = MeleeReachCap(go, std::max(this->proximity, 1.5f) + 1.0f);
		if (ServerGameObject* fc = FirstCharacter(go); fc && (this->target->position - fc->position).len2xz() > need && OutOfReach(go, this->target->position, need)) {
			TraceTask(this, "walk ended short of a target out of reach: the order ends");
			terminate();
			order->terminate();
			return;
		}
	}
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
static uint64_t s_attackPathGame = 0;
// (kept for this game only: by object id, and a game ended with tasks running left its counts)
static std::map<uint32_t, std::map<int, int>>& AttackPathUsers()
{
	const uint64_t serial = Server::instance && Server::instance->gameSet ? Server::instance->gameSet->serial : 0;
	if (s_attackPathGame != serial) {
		s_attackPathGame = serial;
		s_attackPathUsers.clear();
	}
	return s_attackPathUsers;
}

void Task::releaseAttackPath()
{
	if (!attackPathOf)
		return;
	auto it = AttackPathUsers().find(attackPathOf);
	if (it != AttackPathUsers().end()) {
		auto p = it->second.find(attackPath);
		if (p != it->second.end() && --p->second <= 0)
			it->second.erase(p);
		if (it->second.empty())
			AttackPathUsers().erase(it);
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
		auto& users = AttackPathUsers()[target->id];
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
				AttackPathUsers().erase(target->id);
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
		// PATHFIND_TO_SHORE: to the shore nearest to the target (found again once it has moved 5 m)
		if (blueprint->pathfindToShore && !following && !onAttack && !onPath) {
			if (!shoreSet || (target->position - shoreFor).sqlen2xz() > 25.0f) {
				shoreGoal = MovementController::nearestShorePosition(go->blueprint, target->blueprint, go->position, target->position);
				shoreFor = target->position;
				shoreSet = true;
			}
			goal = shoreGoal;
		}
		float prox = this->proximity;
		// a character's or formation's size counts (WKB.exe 0x50d9d0: the proximity plus a quarter
		// of the target's box, maxX + maxZ; here half its BOUNDING_VOLUME sphere): big units (Michael,
		// Abaddon, siege engines) are reached from further out instead of crowded into
		if (prox >= 0.0f && !following && target->blueprint->bpClass != Tags::GAMEOBJCLASS_BUILDING)
			prox += 0.5f * target->blueprint->boundingRadius;
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
		// a vessel at the shore point is there (it can't come nearer to a boarder on land)
		const bool atShore = blueprint->pathfindToShore && shoreSet && !go->movementController.isMoving()
			&& (go->position - shoreGoal).sqlen2xz() < 25.0f;
		const bool arrived = loadedArrived || measured || atShore;
		// MOVEMENT_STARTED_SEQUENCE: on each walk started on a path found
		{
			const uint32_t walks = go->movementController.m_walksStarted;
			if (!walksSeenSet) {
				walksSeenSet = true;
				walksSeen = walks;
			}
			else if (walks != walksSeen) {
				walksSeen = walks;
				if (!arrived && go->movementController.isMoving())
					blueprint->movementStartedSequence.run(order->gameObject);
			}
		}
		if (arrived) {
			shortWalks = 0;
			// A formation "in range" of an enemy its members can never reach: its range counts from
			// its centre and is wide, so the walk ends at once, satisfied, while its spearmen stand
			// at the foot of a slope below a scout (the Kingmaker's "Move To Attack Range Of
			// Original Victim", started again every 4 s all game: the play test of 2026-11-16).
			// By the members' own reach (an archer's 30 m, else 3 m) the task ends as if the target
			// were gone. (Out of reach by the terrain alone: a target behind a wall stays in reach,
			// and the wall in the way is attacked, AttackWallInTheWay.)
			// A unit "there" where its walk ended short (arrival counts from the walk's end, not the
			// target): a spearman 31 m below a hilltop scout stood in "Move To Attack Range" for good
			// (the play test's PERFORMANCE2 save, 2026-11-17). Once a second, by its id, from the
			// clock alone (a loaded game checks at the same steps), out of a melee unit's 3 m or the
			// task's range for an archer: the task ends as if the target were gone.
			const int cl = go->blueprint->bpClass;
			if (cl == Tags::GAMEOBJCLASS_CHARACTER
				&& !go->movementController.isMoving() && go->getPlayer()
				&& ((uint32_t)(Server::instance->timeManager.currentTime * 20.0f + 0.5f) + go->id) % 20 == 0
				&& AIUtil::IsEnemy(go->getPlayer(), target.get())) {
				const float need = MeleeReachCap(go, std::max(this->proximity, 1.5f) + 1.0f);
				if (ServerGameObject* fc = FirstCharacter(go); fc && (target->position - fc->position).len2xz() > need && OutOfReach(go, target->position, need)) {
					TraceTask(this, "arrived short of a target out of reach: the task ends");
					terminate();
					if (blueprint->terminateEntireOrderIfNoTarget)
						order->terminate();
					return;
				}
			}
			if (go->blueprint->bpClass == Tags::GAMEOBJCLASS_FORMATION && !proximitySatisfied && go->getPlayer()
				&& AIUtil::IsEnemy(go->getPlayer(), target.get())) {
				const ServerGameObject* first = nullptr;
				for (const auto& [type, children] : go->children)
					for (CommonGameObject* c : children)
						if (!first && c->blueprint->bpClass == Tags::GAMEOBJCLASS_CHARACTER)
							first = (const ServerGameObject*)c;
				if (first && OutOfReach(go, target->position, AIUtil::ClassOf(first) == 2 ? 30.0f : 3.0f)) {
					TraceTask(this, "target out of the members' reach: the task ends");
					terminate();
					if (blueprint->terminateEntireOrderIfNoTarget)
						order->terminate();
					return;
				}
			}
			// A task loaded as satisfied whose triggers hadn't started, on a standing unit and aimed
			// at a building: WKB.exe runs its PROXIMITY_SATISFIED_SEQUENCE on the first tick
			// (0x50d1e0), before the triggers. A cart loaded in "Wait to Collect Resources from
			// Village" thus ends the wait at once when the village holds enough, and drives to its
			// point in the Collect task.
			if (proximitySatisfied && loadedArrived && !triggersStarted && !go->movementController.isMoving()
				&& target->blueprint->bpClass == Tags::GAMEOBJCLASS_BUILDING) {
				TraceTask(this, "loaded satisfied: proximity sequence");
				blueprint->proximitySatisfiedSequence.run(order->gameObject);
				if (!isWorking())
					return;
			}
			this->startTriggers();
			if (go->movementController.isMoving())
				go->movementController.stopMovement();
			// facing a target that moves (WKB.exe turns to it every tick, 0x50d900), not only on
			// arriving (below); a change under 3 degrees isn't sent
			if (proximitySatisfied && go->blueprint->bpClass == Tags::GAMEOBJCLASS_CHARACTER && target->movement.isMoving()) {
				const Vector3 dir = target->position - go->position;
				if (dir.sqlen2xz() > 0.01f) {
					const float want = std::atan2(dir.x, -dir.z);
					float off = std::fmod(want - go->orientation.y, 6.2831855f);
					if (off > 3.14159265f) off -= 6.2831855f;
					if (off < -3.14159265f) off += 6.2831855f;
					if (std::abs(off) > 0.05f)
						go->setOrientation(Vector3(0, want, 0));
				}
			}
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

				// Set orientation towards target (and while it moves, above)
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
				const bool searchFailed = go->movementController.takeSearchFailed();
				if (searchFailed)
					nextMoveAttempt = Server::instance->timeManager.currentTime + 1.0f;
				// an AI unit chasing an enemy it doesn't gain on gives up (ChaseGivesUp)
				if (ChaseGivesUp(go, target.get())) {
					TraceTask(this, "chase not gaining: the attack ends");
					terminate();
					if (blueprint->terminateEntireOrderIfNoTarget)
						order->terminate();
					return;
				}
				// An attack (or a walk to an enemy: the Kingmaker formations' "Move To Attack Range
				// Of Original Victim") whose walk ended short of a target it can never reach
				// (OutOfReach: up a slope its class can't climb, across water): the task ends as if
				// the target were gone, and the unit (or its AI's script) picks another, instead of
				// standing there or searching again every second for the rest of the game (wkbai's;
				// no WKB.exe rule was traced for it, docs/engine/ministry-ai.md step 16)
				if ((blueprint->classType == Tags::ORDTSKTYPE_ATTACK || (go->getPlayer() && AIUtil::IsEnemy(go->getPlayer(), target.get())))
					&& !go->movementController.isMoving()
					&& (searchFailed || (destination - goal).len2xz() > 2.0f)
					&& OutOfReach(go, goal, MeleeReachCap(go, std::max(this->proximity, 1.5f) + 1.0f))) {
					TraceTask(this, "target out of reach: the attack ends");
					terminate();
					if (blueprint->terminateEntireOrderIfNoTarget)
						order->terminate();
					return;
				}
				// An attack whose walk ended out of range, a building in the way (WKB.exe 0x50dc50 ->
				// 0x50e760): that building becomes the unit's "Attack Target" associate and it gets the
				// event "Attack Wall Target" (the game set's "... Respond to Attack Wall Event" then
				// attack the wall)
				if (blueprint->classType == Tags::ORDTSKTYPE_ATTACK && !go->movementController.isMoving()
					&& (searchFailed || (destination - goal).len2xz() > 2.0f)
					&& Server::instance->timeManager.currentTime >= wallCheckAt)
				{
					wallCheckAt = Server::instance->timeManager.currentTime + 2.0f;
					AttackWallInTheWay(go, target.get());
				}
				// an attack's search may end within its proximity, in whole metres (WKB.exe 0x6d937c)
				auto attackRadius = [&] {
					if (blueprint->classType == Tags::ORDTSKTYPE_ATTACK && this->proximity >= 1.0f && !onPath && !following)
						go->movementController.setGoalRadius(std::floor(this->proximity));
				};
				// out of range: the dissatisfied sequence first, before any walk (it ends a missile
				// task of a unit that may not move, "Don't Skirmish"/Hold Ground: wkbai ran it after
				// the walk was asked for, and a resumed task never, so the archers walked on).
				// A resumed task's only when it would have run at the start (not the end-the-order-only
				// sequence a trebuchet walks into range with)
				const bool endsOnly = blueprint->proximityDissatisfiedEquation < 0
					&& blueprint->proximityDissatisfiedSequence.actionList.size() == 1
					&& blueprint->proximityDissatisfiedSequence.actionList[0]->endsOrder();
				if (proximitySatisfied || (resumedUnchecked && !endsOnly && this->proximity > 0.0f)) {
					proximitySatisfied = false;
					resumedUnchecked = false;
					if (getenv("WKB_TRACE_TASKS_ID")) {
						char buf[160];
						snprintf(buf, sizeof(buf), "proximity dissatisfied (edge %.2f, proximity %.2f, distance %.2f)", edge, this->proximity, (go->position - target->position).len2xz());
						TraceTask(this, buf);
					}
					RunProximityDissatisfied(blueprint, order->gameObject);
					if (!this->isWorking() || !this->order->isWorking())
						return;
				}
				resumedUnchecked = false;
				// a building it can't reach (not an attack, which goes for the wall in its way): the
				// task ends after five walks that ended short, and with it the order, so that the
				// unit goes on to its next one
				if (!go->movementController.isMoving() && Server::instance->timeManager.currentTime >= nextMoveAttempt
					&& blueprint->classType != Tags::ORDTSKTYPE_ATTACK && target->blueprint->bpClass == Tags::GAMEOBJCLASS_BUILDING
					&& ++shortWalks > 5) {
					if (getenv("WKB_TRACE_TASKS_ID"))
						TraceTask(this, "given up: the building can't be reached");
					shortWalks = 0;
					order->terminate();
					return;
				}
				if (!go->movementController.isMoving() && Server::instance->timeManager.currentTime >= nextMoveAttempt) {
					attackRadius();
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
						// WKB.exe 0x50cf9b: again when the target is off the path's end by more than
						// 0.7 of what is left to walk (|D - T|^2 > 0.5 |U - D|^2); here not under 1 m,
						// which near the target would be a search every step
						const Vector3& asked = go->movementController.getRequestedDestination();
						const float drift2 = (goal - asked).sqlen2xz();
						if (drift2 > std::max(1.0f, 0.5f * (go->position - asked).sqlen2xz())) {
							attackRadius();
							destination = go->movementController.startMovement(goal);
						}
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

// The building of another player (not an ally) on the straight line from the unit to its target,
// nearest to the unit: registered as the unit's "Attack Target" (its others cleared), and the unit
// gets "Attack Wall Target" (WKB.exe 0x50e760)
void AttackWallInTheWay(ServerGameObject* unit, ServerGameObject* target)
{
	Server* server = Server::instance;
	if (!server || !server->terrain || !server->tiles || !target)
		return;
	const auto size = server->terrain->getNumPlayableTiles();
	const Vector3 from = unit->position, to = target->position;
	const float len = (to - from).len2xz();
	ServerGameObject* own = unit->getPlayer();
	const int allied = server->gameSet->diplomaticStatuses.names.getIndex("Allied");
	for (float d = 0.0f; d <= len; d += 2.5f) {
		const Vector3 p = from + (to - from) * (len > 0.0f ? d / len : 0.0f);
		const int tx = (int)(p.x / 5.0f), tz = (int)(p.z / 5.0f);
		if (tx < 0 || tz < 0 || tx >= size.first || tz >= size.second)
			break;
		const auto& tile = server->tiles[tz * size.first + tx];
		ServerGameObject* b = tile.building.getFrom<Server>();
		if (!b || tile.buildingPassable || b == target || (b->flags & CommonGameObject::fTerminated))
			continue;
		ServerGameObject* theirs = b->getPlayer();
		if (!theirs || theirs == own || (allied >= 0 && server->getDiplomaticStatus(own, theirs) <= allied))
			continue;
		static const GameSet* of = nullptr;
		static int category = -1, event = -1;
		if (of != server->gameSet.get()) {
			of = server->gameSet.get();
			category = of->associations.names.getIndex("Attack Target");
			event = of->events.names.getIndex("Attack Wall Target");
		}
		if (category < 0 || event < 0)
			return;
		unit->clearAssociates(category);
		unit->associateObject(category, b);
		unit->sendEvent(event, b);
		return;
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
	// the room each keeps (PREFERRED_SPACING, WKB.exe 0x436120: free where |p - q| >= r + r_q);
	// rings as far apart as two of the unit's
	const float own = unit->blueprint->preferredSpacing;
	const float SPACING = std::max(1.5f, 2.0f * own);
	constexpr int RINGS = 10;
	std::vector<std::pair<Vector3, float>> taken;
	NNSearch nns;
	nns.start(Server::instance, place, SPACING * RINGS + 40.0f);
	while (CommonGameObject* o = nns.next()) {
		ServerGameObject* other = o->dyncast<ServerGameObject>();
		// (corpses, "Dead ..." characters terminated where they fell, take no spot)
		if (other == unit || other->blueprint->bpClass != Tags::GAMEOBJCLASS_CHARACTER || !other->isInteractable())
			continue;
		if (other->movementController.isMoving())
			taken.emplace_back(other->movementController.getDestination(), other->blueprint->preferredSpacing);
		else
			taken.emplace_back(other->position, other->blueprint->preferredSpacing);
	}
	auto isFree = [&](const Vector3& spot) {
		if (MovementController::isTileBlocked(unit->blueprint, (int)(spot.x / 5.0f), (int)(spot.z / 5.0f)))
			return false;
		for (const auto& [t, r] : taken)
			if ((t - spot).sqlen2xz() < (own + r) * (own + r))
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

// Where a building puts the unit it has trained. WKB.exe's Task_Spawn (0x50eef0) asks for a path
// of the building's category 4, the model's attachment points "CR_<path>_<point>" (0x568370 with
// no unit, from the building's centre; 0x6343e0 skips paths whose first point is off the playable
// area and takes the nearest), and creates the unit at that path's last point, with the ground's
// height (0x60f460). Only a building with no such path puts it on its own centre. A building's
// spawn order starts with no command (0x544340: the null at 0x92ecf0), so with no rally point
// the unit stays there. The Manor's CR_1_2 and CR_2_2 are one point at its door, 8.8 m from its
// centre and off its footprint (CR_x_1 2.4 m further out): wkbai used the centre, so a peasant
// trained with no rally point stood inside the Manor, where a click selects the Manor (test 72).
Vector3 SpawnSpot(const ServerGameObject* building)
{
	const auto area = Server::instance->terrain ? Server::instance->terrain->getPlayableArea() : std::make_pair(1e9f, 1e9f);
	const std::vector<Vector3>* best = nullptr;
	float bestDist = 0.0f;
	const auto paths = AttachmentPaths(building, "CR_");
	for (const auto& [no, points] : paths) {
		if (points.empty())
			continue;
		const Vector3& first = points.front();
		if (first.x < 0.0f || first.z < 0.0f || first.x >= area.first || first.z >= area.second)
			continue;
		const float d = (first - building->position).sqlen2xz();
		if (!best || d < bestDist) {
			best = &points;
			bestDist = d;
		}
	}
	if (!best)
		return building->position;
	Vector3 spot = best->back();
	if (Server::instance->terrain)
		spot.y = Server::instance->terrain->getHeight(spot.x, spot.z);
	return spot;
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
			ServerGameObject* spawned = Server::instance->spawnObject(toSpawn, obj->getPlayer(), SpawnSpot(obj), Vector3(0,0,0));
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
