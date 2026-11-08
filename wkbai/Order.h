// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#pragma once

#include <vector>
#include <queue>
#include <deque>
#include "gameset/actions.h"
#include "GameObjectRef.h"
#include "util/vecmat.h"

struct OrderBlueprint;
struct TaskBlueprint;
struct TriggerBlueprint;

struct Order;
struct Task;
struct Trigger;

struct ServerGameObject;
struct GameSet;
struct GSFileParser;
struct GameObjBlueprint;

enum OrderTaskState {
	OTS_UNINITIALISED = 0,
	OTS_PROCESSING,
	OTS_SUSPENDED,
	OTS_ABORTED,
	OTS_CANCELLED,
	OTS_TERMINATED
};

struct Order {
	ServerGameObject *gameObject;
	int id, state = OTS_UNINITIALISED;
	const OrderBlueprint *blueprint;
	std::vector<std::unique_ptr<Task>> tasks;
	int nextTaskId = 0;
	int currentTask = -1;

	Order(int id, const OrderBlueprint *blueprint, ServerGameObject *gameObject) : gameObject(gameObject), id(id), blueprint(blueprint) {}

	bool isDone() const { return state >= OTS_ABORTED; }
	bool isWorking() const { return (state == OTS_PROCESSING) || (state == OTS_SUSPENDED); }

	void init();
	void start();
	void suspend();
	void resume();
	void cancel();
	void terminate();

	void process();
	Task *getCurrentTask();
	void advanceToNextTask();
};

struct Task {
	Order* order;
	int id, state = OTS_UNINITIALISED;
	const TaskBlueprint* blueprint;
	bool firstExecution = true, triggersStarted = false;
	std::vector<std::unique_ptr<Trigger>> triggers;
	bool startSequenceExecuted = false;
	SrvGORef target;
	Vector3 destination;
	float proximity = -1.0f; bool proximitySatisfied = false, lastDestinationValid = false;
	// loaded from a save as having reached its target (PROXIMITY_SATISFIED 1): kept so until the
	// task starts again or its target changes. The original's test 36 save has a cart "arrived"
	// 28 m from its village's centre (15 m from its nearest delivery point), which the
	// footprint-edge test here doesn't count as arrived: the cart drove up and collected 4 s
	// early, and the Kingmaker's food came 8 s early.
	bool loadedArrived = false;
	// no new path search before this game time: one that found no way left the unit standing,
	// and searching again every step for an unreachable target (woodcutters at a tree up a
	// cliff) was most of the simulation's time 30 minutes into a 6-AI game
	float nextMoveAttempt = 0.0f;
	// the attack path of its target this task holds (ATTACHMENT_PATH_CATEGORY ATTACK, Order.cpp
	// AttackPathSpot): the path's number, and the target's id (0: none)
	int attackPath = -1;
	uint32_t attackPathOf = 0;
	void releaseAttackPath();
	// The target's attachment path this task's unit walks (ATTACHMENT_PATH_CATEGORY ACTIVITY_PATH
	// or DELIVERY: the target model's AC_<path>_<point> or DE_<path>_<point> points), as WKB.exe
	// gives one out (0x568370, 0x6343e0): the path with the fewest units on it, the nearest first
	// point among those. The unit walks its points in order and works at the last one.
	// pathNo -1: none (the target has no such points); chosen again on each start.
	bool pathChosen = false;
	int pathNo = -1;
	uint32_t pathTargetId = 0;
	std::vector<Vector3> pathPoints;
	size_t pathStep = 0;
	// a walk to the current point was asked for (a stop near it then counts as there)
	bool pathStepTried = false;
	float AnimationSpeedToFit(ServerGameObject* go, int anim) const;

	Task(int id, const TaskBlueprint* blueprint, Order* order);
	virtual ~Task();

	bool isDone() const { return state >= OTS_ABORTED; }
	bool isWorking() const { return (state == OTS_PROCESSING) || (state == OTS_SUSPENDED); }

	void init();
	void start();
	void suspend();
	void resume();
	void cancel();
	void terminate();

	void process();
	void startTriggers();
	void stopTriggers();

	void setTarget(ServerGameObject* obj);
	void reevaluateTarget();

	virtual void onStart() {}
	virtual void onUpdate() {}

	static std::unique_ptr<Task> create(int id, const TaskBlueprint* blueprint, Order* order);
};

struct MissileTask : Task {
	float msStartTime; Vector3 msInitialPosition, msInitialVelocity, msPrevPosition;
	using Task::Task;
	virtual void onStart() override;
	virtual void onUpdate() override;
};

struct MoveTask : Task {
	using Task::Task;
	virtual void onStart() override;
	virtual void onUpdate() override;
};

struct ObjectReferenceTask : Task {
	using Task::Task;
	virtual void onStart() override;
	virtual void onUpdate() override;
};

struct FaceTowardsTask : Task {
	// a point to face (the end of a right-drag) when the task has no target
	Vector3 faceTo;
	bool hasFaceTo = false;
	using Task::Task;
	//virtual void onStart() override;
	virtual void onUpdate() override;
};

struct SpawnTask : Task {
	const GameObjBlueprint* toSpawn = nullptr;
	SrvGORef aiCommissioner; // temp
	bool isSpawned = false;
	using Task::Task;
	~SpawnTask();
	void setSpawnBlueprint(const GameObjBlueprint* blueprint);
	virtual void onStart() override;
	virtual void onUpdate() override;
};


struct Trigger {
	Task *task;
	const TriggerBlueprint *blueprint;
	Trigger(Task *task, const TriggerBlueprint *blueprint) : task(task), blueprint(blueprint) {}
	// (virtual: a task deletes its TimerTriggers through Trigger pointers; AddressSanitizer's
	// new-delete-type-mismatch, 2026-11-07)
	virtual ~Trigger() = default;
	virtual void init() {}
	virtual void update() {}
	virtual void parse(GSFileParser &gsf, const GameSet &gs);
};

// (the times start at 0: a task saved before its triggers started wrote whatever the memory held,
// "PERIOD 1.40129846e-45" in a play test's save)
struct TimerTrigger : Trigger {
	float period = 0.0f, referenceTime = 0.0f;
	using Trigger::Trigger;
	void init() override;
	void update() override;
	void parse(GSFileParser &gsf, const GameSet &gs) override;
};

struct AnimationLoopTrigger : Trigger {
	float referenceTime = 0.0f;
	using Trigger::Trigger;
	void init() override;
	void update() override;
	void parse(GSFileParser &gsf, const GameSet &gs) override;
};

struct AttachmentPointTrigger : Trigger {
	uint32_t referenceTime = 0;
	using Trigger::Trigger;
	void init() override;
	void update() override;
	void parse(GSFileParser& gsf, const GameSet& gs) override;
};

struct OrderConfiguration {
	ServerGameObject *gameobj;
	std::deque<Order> orders;
	int nextOrderId = 0;
	bool busy = false;

	OrderConfiguration(ServerGameObject *gameobj) : gameobj(gameobj) {}
	void process();
	// An order from the engine: a command (a player's, the AI's), wkbai's AI, the debugger. None
	// for a terminated object: a corpse is the dead unit converted in place, with its id, and was
	// still in the AI's lists ("Attack": it walked along with the living).
	Order* addOrder(const OrderBlueprint *orderBlueprint, int assignMode = 0, ServerGameObject *target = nullptr, const Vector3 &destination = Vector3(-1.0f,-1.0f,-1.0f), bool startNow = true);
	// An order from the game set's scripts (ORDER_ASSIGNMENT): also for a terminated object, as
	// "Die" is given to the dying unit
	Order* assignOrder(const OrderBlueprint *orderBlueprint, int assignMode = 0, ServerGameObject *target = nullptr, const Vector3 &destination = Vector3(-1.0f,-1.0f,-1.0f), bool startNow = true);
	void cancelAllOrders();

	Order *getCurrentOrder() { return orders.empty() ? nullptr : &orders[0]; }
};

// A task's attachment path as a save keeps it (beside it, <save>.paths.txt: WKB.exe's saves have no
// place for it): the path of its target's points, the point it walks to and whether that walk was
// asked for. Without it a loaded worker took a path again from its first point (test 43: 2 of 14
// workers carried on as in the saved run).
void RestoreAttachmentPath(Task* task, uint32_t targetId, int pathNo, size_t step, bool tried);

// The spot nearest to a place where a unit can stand clear of the other characters there or
// walking there (a rally point's units gather around it)
Vector3 FreeStandingSpot(ServerGameObject* unit, const Vector3& place);
