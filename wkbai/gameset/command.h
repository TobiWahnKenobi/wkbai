// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#pragma once

#include <string>
#include <vector>
#include "actions.h"
#include "../util/vecmat.h"

struct ServerGameObject;
struct OrderBlueprint;
struct WndCursor;
struct GSCondition;
struct GameObjBlueprint;
struct ValueDeterminer;

struct Command {
	int id;
	OrderBlueprint *order = nullptr;
	ActionSequence startSequence;
	// POST_ORDER_SEQUENCE: run after the start sequence and the order, with the same target: the
	// Train command's sends a unit that got no slot at the dummy clicked to the nearest one with
	// a free slot ("Train near Selected Dummy")
	ActionSequence postOrderSequence;
	WndCursor *cursor = nullptr;
	std::vector<int> cursorConditions, iconConditions;
	std::vector<std::pair<GSCondition*, WndCursor*>> cursorAvailable;
	std::string buttonEnabled, buttonWait, buttonDepressed, buttonHighlighted, buttonAvailable, buttonImpossible;
	std::vector<GSCondition*> conditionsImpossible, conditionsWait, conditionsWarning;
	const GameObjBlueprint* stampdownObject = nullptr;
	std::string defaultHint;
	std::vector<ValueDeterminer*> defaultHintValues;
	bool canBeAssignedToSpawnedUnits = false;
	// FLAG ALWAYS_OFFERED_BY_FORMATION: a formation offers it even when none of its members does
	// (the others a formation offers only when a member offers them too)
	bool alwaysOfferedByFormation = false;
	// FLAG CURSOR_NOT_OFFERED_BY_DEFAULT: never the hover cursor / right-click by itself, only once
	// chosen (its button, or Tab cycling); WKB.exe's command flag 8 (0x4ff2ea), skipped at 0x78f1f2
	bool cursorNotOfferedByDefault = false;
	// FLAG EVALUATE_CONDITIONS_OVER_LANDSCAPE (WKB.exe's flag 0x40): its cursor is tested with no
	// object pointed at too, even without a button (the others need one: flag 1, 0x653289)
	bool evaluateConditionsOverLandscape = false;
	bool hasButton() const { return !buttonEnabled.empty() || !buttonDepressed.empty() || !buttonHighlighted.empty()
		|| !buttonWait.empty() || !buttonAvailable.empty() || !buttonImpossible.empty(); }

	void parse(GSFileParser &gsf, GameSet &gs);
	bool canBeExecuted(ServerGameObject* self, ServerGameObject* target = nullptr) const;
	// faceTo: a point the order's Face Towards task faces (x < 0: none)
	void execute(ServerGameObject *self, ServerGameObject *target = nullptr, int assignmentMode = 0, const Vector3 &destination = Vector3(), const Vector3& faceTo = Vector3(-1, 0, 0)) const;
};