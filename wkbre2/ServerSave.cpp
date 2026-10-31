// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

// Writing save games and levels, in the original's text format (the "FORMAT SCENARIO" files of
// Save_Games): what Server::loadSaveGame reads back, and what the original game loads (checked
// with its Load Game: it needs GAME_TYPE, PREDEC wrapped as it wraps it, the LANDSCAPE object and
// every pair's diplomacy). The client state's camera is the player's start camera.

#include "server.h"
#include "Order.h"
#include "AIController.h"
#include "gameset/gameset.h"
#include "gameset/OrderBlueprint.h"
#include "gameset/WorkOrder.h"
#include "file.h"
#include <cstdarg>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>
#include <algorithm>
#include "util/SimRandom.h"

namespace {
	struct SaveWriter {
		FILE* f;
		const GameSet& gs;
		int depth = 0;

		void line(const char* fmt, ...) {
			for (int i = 0; i < depth; i++)
				fputc('\t', f);
			va_list args;
			va_start(args, fmt);
			vfprintf(f, fmt, args);
			va_end(args);
			fputc('\n', f);
		}
	};

	std::string inQuotes(const std::string& s) { return "\"" + s + "\""; }

	// "<count> <id>...", the ids sorted so that the same game gives the same file (they come from hash sets)
	std::string idList(std::vector<uint32_t>& ids)
	{
		std::sort(ids.begin(), ids.end());
		std::string out = std::to_string(ids.size());
		for (uint32_t id : ids)
			out += " " + std::to_string(id);
		return out;
	}

	void writeTrigger(SaveWriter& w, size_t index, const Trigger* trigger)
	{
		w.line("TRIGGER %zu", index);
		w.depth++;
		if (auto* t = dynamic_cast<const TimerTrigger*>(trigger)) {
			w.line("PERIOD %.9g", t->period);
			w.line("REFERENCE_TIME %.9g", t->referenceTime);
		}
		else if (auto* t = dynamic_cast<const AnimationLoopTrigger*>(trigger))
			w.line("REFERENCE_TIME %d", (int)(t->referenceTime * 1000.0f));
		else if (auto* t = dynamic_cast<const AttachmentPointTrigger*>(trigger))
			w.line("REFERENCE_TIME %d", (int)t->referenceTime); // read back as a signed int
		w.depth--;
		w.line("END_TRIGGER");
	}

	void writeOrders(SaveWriter& w, const ServerGameObject* obj)
	{
		const OrderConfiguration& oc = obj->orderConfig;
		if (oc.orders.empty() && oc.nextOrderId == 0)
			return;
		w.line("ORDER_CONFIGURATION");
		w.depth++;
		w.line("UNIQUE_ORDER_ID %d", oc.nextOrderId);
		for (const Order& order : oc.orders) {
			w.line("ORDER %s", inQuotes(w.gs.orders.getString(order.blueprint)).c_str());
			w.depth++;
			w.line("PROCESS_STATE %d", order.state);
			w.line("ORDER_ID %d", order.id);
			w.line("UNIQUE_TASK_ID %d", order.nextTaskId);
			w.line("CURRENT_TASK %d", order.currentTask);
			for (const auto& task : order.tasks) {
				w.line("TASK %s", inQuotes(w.gs.tasks.getString(task->blueprint)).c_str());
				w.depth++;
				w.line("TASK_ID %d", task->id);
				w.line("PROCESS_STATE %d", task->state);
				if (const ServerGameObject* target = task->target.get())
					w.line("TARGET %u", target->id);
				w.line("PROXIMITY %.9g", task->proximity);
				w.line("PROXIMITY_SATISFIED %d", (int)task->proximitySatisfied);
				w.line("TRIGGERS_STARTED %d", (int)task->triggersStarted);
				w.line("FIRST_EXECUTION %d", (int)task->firstExecution);
				w.line("START_SEQUENCE_EXECUTED %d", (int)task->startSequenceExecuted);
				w.line("DESTINATION %.9g %.9g", task->destination.x, task->destination.z);
				w.line("LAST_DESTINATION_VALID %d", (int)task->lastDestinationValid);
				if (auto* spawn = dynamic_cast<const SpawnTask*>(task.get()); spawn && spawn->toSpawn)
					w.line("SPAWN_BLUEPRINT %s", spawn->toSpawn->getFullName().c_str());
				for (size_t i = 0; i < task->triggers.size(); i++)
					writeTrigger(w, i, task->triggers[i].get());
				w.depth--;
				w.line("END_TASK");
			}
			w.depth--;
			w.line("END_ORDER");
		}
		w.depth--;
		w.line("END_ORDER_CONFIGURATION");
	}

	void writeObject(SaveWriter& w, Server& server, const ServerGameObject* obj)
	{
		const char* cls = Tags::GAMEOBJCLASS_tagDict.getStringFromID(obj->blueprint->bpClass);
		const bool isLevel = obj->blueprint->bpClass == Tags::GAMEOBJCLASS_LEVEL;
		// the level without its type, as the original (PREDEC names it)
		if (isLevel)
			w.line("%s %u", cls, obj->id);
		else
			w.line("%s %u %s", cls, obj->id, inQuotes(obj->blueprint->name).c_str());
		w.depth++;
		if (obj->blueprint->subtypeNames.size() > 0) {
			const std::string& subtype = obj->blueprint->subtypeNames.getString(obj->subtype);
			if (obj->appearance >= 0 && obj->appearance < (int)w.gs.appearances.names.size())
				w.line("APPEARANCE %s %s", inQuotes(subtype).c_str(), inQuotes(w.gs.appearances.names.getString(obj->appearance)).c_str());
			else
				w.line("SUBTYPE %s", inQuotes(subtype).c_str());
		}
		const bool isPlayer = obj->blueprint->bpClass == Tags::GAMEOBJCLASS_PLAYER;
		if (!isLevel && !isPlayer) { // as the original: levels and players have no place
			w.line("POSITION %.9g %.9g %.9g", obj->position.x, obj->position.y, obj->position.z);
			w.line("ORIENTATION %.9g %.9g %.9g", obj->orientation.x, obj->orientation.y, obj->orientation.z);
			if (obj->animationIndex > 0 && obj->animationIndex < (int)w.gs.animations.names.size())
				w.line("ANIMATION_STATE %s %d %d %.9g 0", inQuotes(w.gs.animations.names.getString(obj->animationIndex)).c_str(),
					obj->animationVariant, (int)(obj->animStartTime * 1000.0f), obj->animSpeed);
		}
		if (isPlayer) {
			w.line("COLOUR_INDEX %d", obj->color);
			const Vector3& cp = obj->playerStartCameraPosition;
			const Vector3& co = obj->playerStartCameraOrientation;
			w.line("START_CAMERA_POS %.9g %.9g %.9g", cp.x, cp.y, cp.z);
			w.line("START_CAMERA_ORIENTATION %.9g %.9g", co.x, co.y);
		}
		if (!obj->name.empty()) {
			if (w.gs.version >= GameSet::GSVERSION_WKBATTLES && isPlayer) {
				// the Battles player name: its length and character codes (the name is UTF-8 here)
				const std::wstring wname = PathFromUtf8(obj->name).wstring();
				std::string codes;
				for (wchar_t c : wname)
					codes += " " + std::to_string((int)c);
				w.line("NAME %zu%s", wname.size(), codes.c_str());
			}
			else
				w.line("NAME %s", inQuotes(obj->name).c_str());
		}
		for (const auto& [item, value] : obj->items)
			if (value != 0.0f) // a missing item reads as 0
				w.line("ITEM %s %.9g", inQuotes(w.gs.items.names.getString(item)).c_str(), value);
		if (obj->disableCount > 0)
			w.line("DISABLE_COUNT %d", obj->disableCount);
		if (obj->flags & ServerGameObject::fTerminated)
			w.line("TERMINATED");
		if (obj->flags & ServerGameObject::fPlayerTerminated)
			w.line("PLAYER_TERMINATED");
		for (const auto& rect : obj->cityRectangles)
			w.line("RECTANGLE %d %d %d %d", rect.xStart, rect.yStart, rect.xEnd, rect.yEnd);
		if (!obj->zoneTiles.empty()) {
			std::string tiles;
			for (const auto& [x, z] : obj->zoneTiles)
				tiles += " " + std::to_string(x) + " " + std::to_string(z);
			w.line("TILES %zu%s", obj->zoneTiles.size(), tiles.c_str());
		}
		std::vector<std::string> reactions;
		for (const Reaction* r : obj->individualReactions)
			reactions.push_back(w.gs.reactions.getString(r));
		std::sort(reactions.begin(), reactions.end());
		for (const std::string& r : reactions)
			w.line("INDIVIDUAL_REACTION %s", inQuotes(r).c_str());
		std::vector<std::pair<std::string, std::string>> associates; // sorted by category, a hash map too
		for (const auto& [category, set] : obj->associates) {
			std::vector<uint32_t> ids;
			for (const SrvGORef& ref : set)
				if (const ServerGameObject* other = ref.get())
					ids.push_back(other->id);
			if (!ids.empty())
				associates.emplace_back(w.gs.associations.names.getString(category), idList(ids));
		}
		std::sort(associates.begin(), associates.end());
		for (const auto& [category, ids] : associates)
			w.line("ASSOCIATE %s %s", inQuotes(category).c_str(), ids.c_str());
		writeOrders(w, obj);

		// the level's terrain, as the original: a LANDSCAPE object holding the MAP, before the players
		if (isLevel && !server.mapFile.empty()) {
			w.line("LANDSCAPE %u", server.landscapeId);
			w.depth++;
			w.line("MAP %s", inQuotes(server.mapFile).c_str());
			w.depth--;
			w.line("END_LANDSCAPE");
		}
		for (const auto& [type, children] : obj->children)
			for (const CommonGameObject* child : children) {
				const ServerGameObject* c = child->dyncast<ServerGameObject>();
				if (!c->deleted)
					writeObject(w, server, c);
			}
		// after the children, as the original: the work orders name cities
		if (obj->aiController.planBlueprint || !obj->aiController.workOrderInstances.empty() || !obj->aiController.commissionInstances.empty()) {
			w.line("AI_CONTROLLER");
			w.depth++;
			if (obj->aiController.planBlueprint) {
				// the plan restarts from its beginning: its progress isn't read back
				w.line("MASTER_PLAN %s", inQuotes(w.gs.plans.getString(obj->aiController.planBlueprint)).c_str());
				w.line("END_MASTER_PLAN");
			}
			// the running commissions, as the original: their requirements by name in the
			// blueprint's order, with the foundations under construction
			for (const auto& ci : obj->aiController.commissionInstances) {
				const ServerGameObject* handler = ci.handlerObject.get();
				if (!handler || !ci.blueprint)
					continue;
				w.line("COMMISSION %s %u", inQuotes(w.gs.commissions.getString(ci.blueprint)).c_str(), handler->id);
				w.depth++;
				if (ci.forceBalance)
					w.line("FORCE_BALANCE");
				for (const auto& req : ci.blueprint->characterRequirements) {
					w.line("REQUIREMENT CHARACTER_REQUIREMENT %s", inQuotes(w.gs.characterLadders.getString(req.ladder)).c_str());
					w.line("END_REQUIREMENT");
				}
				for (size_t r = 0; r < ci.blueprint->buildingRequirements.size(); r++) {
					const auto* bp = ci.blueprint->buildingRequirements[r].bpBuilding;
					w.line("REQUIREMENT BUILDING_REQUIREMENT %s", inQuotes(bp ? bp->name : std::string()).c_str());
					w.depth++;
					if (r < ci.buildingReqInsts.size())
						for (const SrvGORef& f : ci.buildingReqInsts[r].foundations)
							if (f.get())
								w.line("FOUNDATION %u", f.get()->id);
					w.depth--;
					w.line("END_REQUIREMENT");
				}
				w.depth--;
				w.line("END_COMMISSION");
			}
			for (const auto& woi : obj->aiController.workOrderInstances) {
				const ServerGameObject* city = woi.city.get();
				int finder = -1;
				for (size_t i = 0; i < w.gs.objectFinderDefinitions.size(); i++)
					if (w.gs.objectFinderDefinitions[i] == woi.unitFinder) { finder = (int)i; break; }
				if (city && finder >= 0 && woi.workOrder) {
					w.line("WORK_ORDER %u %s %s", city->id, inQuotes(w.gs.objectFinderDefinitions.getString(finder)).c_str(),
						inQuotes(w.gs.workOrders.getString(woi.workOrder)).c_str());
					w.line("END_WORK_ORDER");
				}
			}
			w.depth--;
			w.line("END_AI_CONTROLLER");
		}
		if (isLevel) {
			// the game state kept at the level: aliases and diplomacy, after the objects they name
			for (const auto& [alias, set] : server.aliases) {
				std::vector<uint32_t> ids;
				for (const CmnGORef& ref : set)
					if (const CommonGameObject* other = ref.getFrom(&server))
						ids.push_back(other->id);
				if (!ids.empty())
					w.line("ALIAS %s %s", inQuotes(w.gs.aliases.names.getString(alias)).c_str(), idList(ids).c_str());
			}
			// every pair of players, as the original writes them (the default status too: with
			// only the pairs set, its loader stayed on its loading screen)
			std::vector<const ServerGameObject*> players;
			for (const auto& [type, children] : obj->children)
				for (const CommonGameObject* child : children)
					if (child->blueprint->bpClass == Tags::GAMEOBJCLASS_PLAYER && !child->dyncast<ServerGameObject>()->deleted)
						players.push_back(child->dyncast<ServerGameObject>());
			std::sort(players.begin(), players.end(), [](auto* a, auto* b) { return a->id < b->id; });
			for (size_t i = 0; i < players.size(); i++)
				for (size_t j = i + 1; j < players.size(); j++) {
					const int status = server.getDiplomaticStatus((CommonGameObject*)players[i], (CommonGameObject*)players[j]);
					w.line("DIPLOMATIC_STATUS_BETWEEN %u %u %s", players[i]->id, players[j]->id, inQuotes(w.gs.diplomaticStatuses.names.getString(status)).c_str());
				}
		}
		w.depth--;
		if (isLevel) // as the original: with its id
			w.line("END_%s %u", cls, obj->id);
		else
			w.line("END_%s", cls);
	}
}

bool Server::saveGame(const char* filename)
{
	if (!level || !gameSet)
		return false;
	const std::filesystem::path path = PathFromUtf8(filename);
	std::error_code ec;
	std::filesystem::create_directories(path.parent_path(), ec);
	FILE* f = _wfopen(path.c_str(), L"w");
	if (!f)
		return false;
	SaveWriter w{ f, *gameSet };
	w.line("FORMAT SCENARIO");
	w.line("VERSION 1.0");
	w.line("MODE TEXT");
	w.line("");
	w.line("GAME_SET %s", inQuotes(gameSetFile).c_str());
	w.line("");
	std::string humans;
	for (const SrvGORef& player : clientPlayerObjects)
		humans += " " + std::to_string(player.get() ? player.get()->id : 0);
	// a level lists every player a human can take (as read); a game, the ones playing
	if (savingLevel && !humanPlayerIds.empty()) {
		std::vector<uint32_t> listed;
		for (uint32_t id : humanPlayerIds)
			if (ServerGameObject* p = findObject(id); p && !p->deleted)
				listed.push_back(id);
		humans.clear();
		for (uint32_t id : listed)
			humans += " " + std::to_string(id);
		w.line("GAME_TYPE 4");
		w.line("NUM_HUMAN_PLAYERS %zu%s", listed.size(), humans.c_str());
	}
	else {
		w.line("GAME_TYPE %d", savingLevel ? 4 : (gameType == 4 ? 1 : gameType));
		w.line("NUM_HUMAN_PLAYERS %zu%s", clientPlayerObjects.size(), humans.c_str());
	}
	w.line("");
	// as the original's header (a level has 0 and 0.00)
	w.line("UPDATE_ID 0");
	w.line("NEXT_UPDATE_TIME_STAMP %.9g", savingLevel ? 0.0f : timeManager.currentTime);
	w.line("");
	w.line("RANDOM_SEED %u", g_simRandom.state);
	w.line("");
	w.line("TIME_MANAGER_STATE");
	w.depth++;
	w.line("CURRENT_TIME %.9g", timeManager.currentTime);
	w.line("PREVIOUS_TIME %.9g", timeManager.previousTime);
	w.line("ELAPSED_TIME %.9g", timeManager.elapsedTime);
	w.line("PAUSED %d", (int)timeManager.paused);
	w.line("LOCK_COUNT %u", timeManager.lockCount + 1); // as the original: saving holds a lock, which the loader releases
	w.depth--;
	w.line("END_TIME_MANAGER_STATE");
	w.line("");
	if (!landscapeId)
		landscapeId = nextUniqueId++;
	w.line("NEXT_UNIQUE_ID %u", nextUniqueId);
	w.line("");
	// PREDEC: every object's type and id before the tree, as the original writes them (its loader
	// makes the objects from it; without it the original crashed loading a level). One line per
	// type, wrapped as the original's (at about 1800 characters, going on after two tabs)
	{
		std::map<std::pair<int, std::string>, std::vector<uint32_t>> byType;
		std::vector<const ServerGameObject*> todo{ level->dyncast<ServerGameObject>() };
		while (!todo.empty()) {
			const ServerGameObject* o = todo.back();
			todo.pop_back();
			if (o->blueprint->bpClass != Tags::GAMEOBJCLASS_LEVEL)
				byType[{ o->blueprint->bpClass, o->blueprint->name }].push_back(o->id);
			for (const auto& [type, children] : o->children)
				for (const CommonGameObject* child : children)
					if (const ServerGameObject* c = child->dyncast<ServerGameObject>(); !c->deleted)
						todo.push_back(c);
		}
		w.line("PREDEC");
		w.depth++;
		const ServerGameObject* lvl = level->dyncast<ServerGameObject>();
		w.line("LEVEL %s 1 %u", inQuotes(lvl->blueprint->name).c_str(), lvl->id);
		if (!mapFile.empty())
			w.line("LANDSCAPE \"Standard\" 1 %u", landscapeId);
		for (auto& [type, ids] : byType) {
			std::sort(ids.begin(), ids.end());
			// as the original: each id followed by a space, and a line broken after the id that
			// takes it (with its tab) past 1800 characters. Its reader tells a line that goes on by
			// that length: wrapped shorter, it stayed on its loading screen.
			std::string text = std::string(" ") + Tags::GAMEOBJCLASS_tagDict.getStringFromID(type.first) + " " + inQuotes(type.second) + " " + std::to_string(ids.size()) + " ";
			for (size_t k = 0; k < ids.size(); k++) {
				text += std::to_string(ids[k]) + " ";
				if (1 + text.size() > 1800 && k + 1 < ids.size()) {
					w.line("%s", text.c_str());
					text = "\t ";
				}
			}
			w.line("%s", text.c_str());
		}
		w.depth--;
		w.line("END_PREDEC");
		w.line("");
	}
	writeObject(w, *this, level->dyncast<ServerGameObject>());
	// the pending scripted sequences, as the original (after the level)
	w.line("");
	auto ids = [](const std::vector<SrvGORef>& objs) {
		std::string s;
		size_t n = 0;
		for (const SrvGORef& r : objs)
			if (r.get()) { s += " " + std::to_string(r.get()->id); n++; }
		return std::to_string(n) + s;
	};
	for (const auto& [at, ds] : delayedSequences) {
		const ServerGameObject* executor = ds.executor.get();
		if (!ds.actionSequence || !executor)
			continue;
		w.line("DELAYED_SEQUENCE_EXECUTION %.9g %s %u %s 1", at, inQuotes(w.gs.actionSequences.getString(ds.actionSequence)).c_str(),
			executor->id, ids(ds.selfs).c_str());
	}
	w.line("");
	for (int repeat = 0; repeat < 2; repeat++)
		for (const auto& ops : repeat ? repeatOverPeriodSequences : overPeriodSequences) {
			const ServerGameObject* executor = ops.executor.get();
			if (!ops.actionSequence || !executor)
				continue;
			w.line("%s %s %u %.9g %.9g %d %d %s", repeat ? "REPEAT_SEQUENCE_OVER_PERIOD" : "EXECUTE_SEQUENCE_OVER_PERIOD",
				inQuotes(w.gs.actionSequences.getString(ops.actionSequence)).c_str(), executor->id, ops.startTime, ops.period,
				ops.numTotalExecutions, ops.numExecutionsDone, ids(ops.remainingObjects).c_str());
		}
	// the human's client state, as the original ends every level and save: its camera, here the
	// player's start camera (the original's loader waited forever on its loading screen without it)
	{
		std::vector<uint32_t> clients;
		if (savingLevel && !humanPlayerIds.empty())
			clients.push_back(humanPlayerIds.front());
		else
			for (const SrvGORef& player : clientPlayerObjects)
				if (player.get())
					clients.push_back(player.get()->id);
		for (uint32_t id : clients)
			if (const ServerGameObject* player = findObject(id)) {
				const Vector3& cp = player->playerStartCameraPosition;
				const Vector3& co = player->playerStartCameraOrientation;
				w.line("");
				w.line("CLIENT_STATE %u", id);
				w.line("UPDATE_ID 0");
				w.line("CAMERA_POSITION %.9g %.9g %.9g %.9g %.9g", cp.x, cp.y, cp.z, co.x, co.y);
				w.line("END_CLIENT_STATE");
			}
	}
	const bool ok = !ferror(f);
	fclose(f);
	return ok;
}
