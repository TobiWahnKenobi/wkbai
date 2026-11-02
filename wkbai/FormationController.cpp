#include "FormationController.h"

#define _USE_MATH_DEFINES
#include <cmath>
#include <utility>
#include <vector>
#include <algorithm>

#include "server.h"
#include "gameset/GameObjBlueprint.h"
#include "terrain.h"

// The member places of the original's formations (WKB.exe: Line 0x64a7a0, Column 0x64a900,
// Wedge 0x64aa70, Orb 0x64ad50), chosen by the formation type at 0x550c60. x is across, z
// backwards from the front row at 0; sepX between neighbours, sepY between rows (rings).
static std::vector<std::pair<float, float>> formationPlaces(FormationType type, int numMembers, float sepX, float sepY)
{
	std::vector<std::pair<float, float>> places;
	if (numMembers <= 0)
		return places;
	places.reserve(numMembers);
	// centred rows of `perRow` (or row k of k for the wedge)
	auto rows = [&](int perRow, bool wedge) {
		int remaining = numMembers;
		for (int row = 0; remaining > 0; row++) {
			const int count = std::min(remaining, wedge ? row + 1 : perRow);
			for (int i = 0; i < count; i++)
				places.emplace_back(i * sepX - (count - 1) * sepX * 0.5f, row * sepY);
			remaining -= count;
		}
	};
	if (type == FormationType::Line) {
		const int numRows = std::max(2, (numMembers + 11) / 12);
		rows((numRows - 1 + numMembers) / numRows, false);
	}
	else if (type == FormationType::Column) {
		const int a = std::max(2, numMembers / 10);
		const int b = std::max(1, (numMembers + 1) / a);
		rows((b - 1 + numMembers) / b, false);
	}
	else if (type == FormationType::Wedge)
		rows(0, true);
	else if (type == FormationType::Orb) {
		// rings from the outside in, sepY apart; the outer ring starts at 12 m and grows by sepX
		// until everyone fits. The front of the outer ring is at 0.
		const float TWO_PI = 6.2831855f;
		for (float outer = 12.0f; places.size() < (size_t)numMembers; outer += sepX) {
			places.clear();
			int remaining = numMembers;
			float offset = 0.0f;
			for (float r = outer; r >= 0.0f && remaining > 0; r -= sepY) {
				r = std::min(r, remaining * sepX / TWO_PI);
				if (offset == 0.0f)
					offset = r;
				const int count = std::min(remaining, (int)(r * TWO_PI / sepX));
				for (int i = 0; i < count; i++) {
					const float angle = (float)(count - i) / count * TWO_PI;
					places.emplace_back(std::sin(angle) * r, offset - std::cos(angle) * r);
				}
				remaining -= count;
			}
			if (remaining > 0)
				places.clear();
			if (outer > 1000.0f)
				break; // not with sensible separations
		}
	}
	while (places.size() < (size_t)numMembers)
		places.emplace_back(0.0f, 0.0f);
	return places;
}

void FormationController::update()
{
	FormationType type = FormationType::Line;
	if (formation->blueprint->name == "Line")
		type = FormationType::Line;
	else if (formation->blueprint->name == "Column")
		type = FormationType::Column;
	else if (formation->blueprint->name == "Wedge")
		type = FormationType::Wedge;
	else if (formation->blueprint->name == "Orb")
		type = FormationType::Orb;

	Server* server = Server::instance;
	Matrix transformMatrix = formation->getWorldMatrix();

	int numMembers = 0;
	const GameObjBlueprint* first = nullptr;
	for (auto& [childType, childList] : formation->children) {
		if (childType.bpClass() == Tags::GAMEOBJCLASS_CHARACTER) {
			numMembers += childList.size();
			if (!first && !childList.empty())
				first = childList.front()->blueprint;
		}
	}
	if (!first)
		return;
	// as the original, the separations of one member's type
	const auto places = formationPlaces(type, numMembers, first->formationSeparationX, first->formationSeparationY);

	int memberIndex = 0;
	for (auto& [childType, childList] : formation->children) {
		if (childType.bpClass() == Tags::GAMEOBJCLASS_CHARACTER) {
			for (CommonGameObject* character : childList) {
				ServerGameObject* srvCharacter = character->dyncast<ServerGameObject>();
				if (srvCharacter->orderConfig.getCurrentOrder() == nullptr) {
					auto [posX, posZ] = places[memberIndex];
					Vector3 slot = Vector3(posX, 0, posZ).transform(transformMatrix);
					if (server->terrain)
						slot.y = server->terrain->getHeightEx(slot.x, slot.z, srvCharacter->blueprint->canWalkOnWater());
					// The members walk to their places (with their walking animation, around
					// obstacles) instead of being put there: into formation, and along with it
					// while it moves. A new walk starts only when the place has moved on from
					// where the member is heading, so that the paths aren't recomputed every tick.
					constexpr float ARRIVED = 0.6f, RETARGET = 1.5f;
					const bool moving = srvCharacter->movementController.isMoving();
					if ((srvCharacter->position - slot).sqlen2xz() > ARRIVED * ARRIVED) {
						if (!moving || (srvCharacter->movementController.getDestination() - slot).sqlen2xz() > RETARGET * RETARGET)
							srvCharacter->movementController.startMovement(slot);
					}
					else if (!formation->movement.isMoving()) {
						// in place, and the formation stands: stop, facing its way (while it
						// marches the member walks on rather than stop and start at every step)
						if (moving)
							srvCharacter->movementController.stopMovement();
						if (srvCharacter->orientation.y != formation->orientation.y)
							srvCharacter->setOrientation(Vector3(0, formation->orientation.y, 0));
					}
				}
				memberIndex += 1;
			}
		}
	}
}
