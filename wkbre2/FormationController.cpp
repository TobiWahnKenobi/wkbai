#include "FormationController.h"

#define _USE_MATH_DEFINES
#include <cmath>
#include <utility>

#include "server.h"
#include "gameset/GameObjBlueprint.h"
#include "terrain.h"

static std::pair<float, float> formationMemberPosition(FormationType type, int numMembers, float memberDistance, int member)
{
	if (numMembers <= 1) {
		return { 0.0f, 0.0f };
	}
	if (type == FormationType::Line) {
		const int numRows = (int)std::sqrt(numMembers / 2.0f);
		const int numColumns = numMembers / numRows;
		const float rowOffset = (numColumns - 1) * memberDistance / 2.0f;
		return { (member % numColumns) * memberDistance - rowOffset, (member / numColumns) * memberDistance };
	}
	if (type == FormationType::Column) {
		const int numColumns = (int)std::sqrt(numMembers / 2.0f);
		const float rowOffset = (numColumns - 1) * memberDistance / 2.0f;
		return { (member % numColumns) * memberDistance - rowOffset, (member / numColumns) * memberDistance };
	}
	if (type == FormationType::Wedge) {
		// we solve n(n+1)/2 = member
		const int row = (-1 + (int)std::sqrt(float(1 + 8 * member))) / 2;
		const int rowFirstMember = row * (row + 1) / 2;
		const float rowOffset = std::min(row, numMembers - rowFirstMember - 1) * memberDistance / 2.0f;
		return { (member - rowFirstMember) * memberDistance - rowOffset, row * memberDistance };
	}
	if (type == FormationType::Orb) {
		if (numMembers <= 2) {
			return { 0.0f, 0.0f };
		}
		const float angleStep = 2.0f * M_PI / numMembers;
		const float formationRadius = (memberDistance / 2.0f) / std::sin(angleStep / 2.0f);
		return { std::cos(angleStep * member) * formationRadius, std::sin(angleStep * member) * formationRadius };
	}
	return { 0.0f, 0.0f };
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
	for (auto& [childType, childList] : formation->children) {
		if (childType.bpClass() == Tags::GAMEOBJCLASS_CHARACTER) {
			numMembers += childList.size();
		}
	}

	int memberIndex = 0;
	for (auto& [childType, childList] : formation->children) {
		if (childType.bpClass() == Tags::GAMEOBJCLASS_CHARACTER) {
			for (CommonGameObject* character : childList) {
				ServerGameObject* srvCharacter = character->dyncast<ServerGameObject>();
				if (srvCharacter->orderConfig.getCurrentOrder() == nullptr) {
					const float memberRadius = 1.0f;
					const float memberOutDistance = 1.0f;
					const float memberDistance = memberOutDistance + 2.0f * memberRadius;
					auto [posX, posZ] = formationMemberPosition(type, numMembers, memberDistance, memberIndex);
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
