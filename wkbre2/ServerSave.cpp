// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

// Writing save games, in the original's text format (the "FORMAT SCENARIO" files of
// Save_Games): what Server::loadSaveGame reads back. Not written yet: delayed and over-period
// sequences, the AI's commissions and the client state (camera), which the loader doesn't read
// either.

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
		w.line("%s %u %s", cls, obj->id, inQuotes(obj->blueprint->name).c_str());
		w.depth++;
		const bool isLevel = obj->blueprint->bpClass == Tags::GAMEOBJCLASS_LEVEL;
		if (isLevel && !server.mapFile.empty())
			w.line("MAP %s", inQuotes(server.mapFile).c_str());
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

		for (const auto& [type, children] : obj->children)
			for (const CommonGameObject* child : children) {
				const ServerGameObject* c = child->dyncast<ServerGameObject>();
				if (!c->deleted)
					writeObject(w, server, c);
			}
		// after the children, as the original: the work orders name cities
		if (obj->aiController.planBlueprint || !obj->aiController.workOrderInstances.empty()) {
			w.line("AI_CONTROLLER");
			w.depth++;
			if (obj->aiController.planBlueprint) {
				// the plan restarts from its beginning: its progress isn't read back
				w.line("MASTER_PLAN %s", inQuotes(w.gs.plans.getString(obj->aiController.planBlueprint)).c_str());
				w.line("END_MASTER_PLAN");
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
			for (const auto& [pair, status] : server.diplomaticStatuses)
				w.line("DIPLOMATIC_STATUS_BETWEEN %d %d %s", pair.first, pair.second, inQuotes(w.gs.diplomaticStatuses.names.getString(status)).c_str());
		}
		w.depth--;
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
	w.line("NUM_HUMAN_PLAYERS %zu%s", clientPlayerObjects.size(), humans.c_str());
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
	w.line("NEXT_UNIQUE_ID %u", nextUniqueId);
	w.line("");
	writeObject(w, *this, level->dyncast<ServerGameObject>());
	const bool ok = !ferror(f);
	fclose(f);
	return ok;
}
