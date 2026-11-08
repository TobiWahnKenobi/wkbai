// wkbre2 - WK Engine Reimplementation
// (C) 2021AdrienTD
// Licensed under the GNU General Public License 3

#include "command.h"
#include "gameset.h"
#include "../util/util.h"
#include "../server.h"
#include "../window.h"
#include "../gameset/ScriptContext.h"
#include <algorithm>

void Command::parse(GSFileParser &gsf, GameSet &gs) {
	gsf.advanceLine();
	while (!gsf.eof) {
		std::string strtag = gsf.nextTag();

		if (strtag == "START_SEQUENCE")
			startSequence.init(gsf, gs, "END_START_SEQUENCE");
		else if (strtag == "POST_ORDER_SEQUENCE")
			postOrderSequence.init(gsf, gs, "END_POST_ORDER_SEQUENCE");
		else if (strtag == "USE_ORDER")
			order = gs.orders.readPtr(gsf);
		else if (strtag == "CURSOR") {
			auto path = gsf.nextString(true);
			if (path == "CURSOR") // needs to bypass one oddity
				path = gsf.nextString(true);
			cursor = WndCreateCursor(path.c_str());
		}
		else if (strtag == "CURSOR_CONDITION")
			cursorConditions.push_back(gs.equations.readIndex(gsf));
		else if (strtag == "ICON_CONDITION")
			iconConditions.push_back(gs.equations.readIndex(gsf));
		else if (strtag == "CURSOR_AVAILABLE") {
			GSCondition* cond = gs.conditions.readPtr(gsf);
			WndCursor* cursor = WndCreateCursor(gsf.nextString(true).c_str());
			cursorAvailable.push_back({ cond, cursor });
		}
		else if (strtag == "BUTTON_ENABLED")
			buttonEnabled = gsf.nextString(true);
		else if (strtag == "BUTTON_WAIT")
			buttonWait = gsf.nextString(true);
		else if (strtag == "BUTTON_DEPRESSED")
			buttonDepressed = gsf.nextString(true);
		else if (strtag == "BUTTON_HIGHLIGHTED")
			buttonHighlighted = gsf.nextString(true);
		else if (strtag == "BUTTON_AVAILABLE")
			buttonAvailable = gsf.nextString(true);
		else if (strtag == "BUTTON_IMPOSSIBLE")
			buttonImpossible = gsf.nextString(true);
		else if (strtag == "CONDITION_IMPOSSIBLE")
			conditionsImpossible.push_back(gs.conditions.readPtr(gsf));
		else if (strtag == "CONDITION_WAIT")
			conditionsWait.push_back(gs.conditions.readPtr(gsf));
		else if (strtag == "CONDITION_WARNING")
			conditionsWarning.push_back(gs.conditions.readPtr(gsf));
		else if (strtag == "STAMPDOWN_OBJECT")
			stampdownObject = gs.readObjBlueprintPtr(gsf);
		else if (strtag == "DEFAULT_HINT") {
			defaultHint = gsf.nextString(false);
			GSCondition::readHintValues(gsf, gs, defaultHintValues);
		}
		else if (strtag == "FLAG") {
			auto flagName = gsf.nextString();
			if (flagName == "CAN_BE_ASSIGNED_TO_SPAWNED_UNITS") {
				canBeAssignedToSpawnedUnits = true;
			}
			else if (flagName == "ALWAYS_OFFERED_BY_FORMATION")
				alwaysOfferedByFormation = true;
		}
		else if (strtag == "END_COMMAND")
			return;
		gsf.advanceLine();
	}
	ferr("Command reached end of file without END_COMMAND!");
}

bool Command::canBeExecuted(ServerGameObject* self, ServerGameObject* target) const
{
	SrvScriptContext ctx(Server::instance, self);
	auto _1 = ctx.target.change(target);
	auto _2 = ctx.selectedObject.change(self);
	auto testCondition = [&ctx](GSCondition* cond) -> bool {return cond->test->booleval(&ctx); };
	auto testEquation = [&ctx](int equ) -> bool { return equ != -1 ? Server::instance->gameSet->equations[equ]->booleval(&ctx) : true; };

	return std::all_of(this->conditionsImpossible.begin(), this->conditionsImpossible.end(), testCondition)
		&& std::all_of(this->conditionsWait.begin(), this->conditionsWait.end(), testCondition)
		&& (std::all_of(this->iconConditions.begin(), this->iconConditions.end(), testEquation)
			|| std::all_of(this->cursorConditions.begin(), this->cursorConditions.end(), testEquation));
}

void Command::execute(ServerGameObject *self, ServerGameObject *target, int assignmentMode, const Vector3 &destination, const Vector3& faceTo) const
{
	if (!canBeExecuted(self, target))
		return;

	SrvScriptContext ctx(Server::instance, self);
	auto _ = ctx.change(ctx.target, target);
	this->startSequence.run(&ctx);
	// TODO: order (what to do first?)
	Order* neworder = nullptr;
	if (this->order) {
		neworder = self->orderConfig.addOrder(this->order, assignmentMode, target, destination, false);
		if (neworder && faceTo.x >= 0.0f)
			for (auto& task : neworder->tasks)
				if (auto* face = dynamic_cast<FaceTowardsTask*>(task.get())) {
					face->faceTo = faceTo;
					face->hasFaceTo = true;
				}
		// Give blueprint for spawn tasks
		if (neworder && this->order->classType == Tags::ORDTSKTYPE_SPAWN) {
			auto& gs = *ctx.server->gameSet;
			auto name = gs.commands.getString(this);
			if (name.substr(0, 6) == "Spawn ") {
				int x = gs.objBlueprints[Tags::GAMEOBJCLASS_CHARACTER].names.getIndex(name.substr(6));
				if (x != -1) {
					const GameObjBlueprint* bp = &gs.objBlueprints[Tags::GAMEOBJCLASS_CHARACTER][x];
					((SpawnTask*)neworder->tasks[0].get())->setSpawnBlueprint(bp);
				}
			}
		}
		// Started here, once set up (a Spawn task needs its blueprint first, a Face Towards task
		// its point), as addOrder starts one in front: the post-order sequence below looks at
		// what the order's start made of the unit. Farm's "I didn't get the farmer job" tests "I
		// am a Farmer", set by ORDER "Farm"'s start sequence; with the order starting only at the
		// unit's next update, a second peasant sent to a farmed field stood idle by it (its order
		// refused at the next update) instead of going to the nearest free farm.
		if (neworder && neworder == self->orderConfig.getCurrentOrder() && !neworder->isWorking() && !neworder->isDone()
			&& (assignmentMode == Tags::ORDERASSIGNMODE_DO_FIRST || assignmentMode == Tags::ORDERASSIGNMODE_FORGET_EVERYTHING_ELSE))
			neworder->start();
	}
	// The post-order sequence (Train's "I didn't get to Train", Farm's and Shop's "I didn't get
	// the ... job", Siege Tower's docking) runs once the order has had its start, or was refused
	// (its PRE_CONDITION: a farmed field, a full dummy; that is the case it is for); not for an
	// order queued behind others (DO_LAST), which hasn't started yet
	if (!this->postOrderSequence.actionList.empty() && (!this->order || !neworder || neworder->state != OTS_UNINITIALISED))
		this->postOrderSequence.run(&ctx);
}