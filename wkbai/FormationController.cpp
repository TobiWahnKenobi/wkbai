#include "FormationController.h"

#define _USE_MATH_DEFINES
#include <cmath>
#include <utility>
#include <vector>
#include <algorithm>
#include <map>

#include "server.h"
#include "gameset/GameObjBlueprint.h"
#include "terrain.h"
#include "gameset/gameset.h"
#include "GameObjectRef.h"
#include "MovementController.h"

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

static FormationType formationTypeOf(const ServerGameObject* formation)
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
	return type;
}

namespace {
	constexpr float PI_F = 3.14159265f;
	constexpr float TWO_PI_F = 6.2831855f;
	constexpr int MIN_MEMBERS = 8; // WKB.exe 0x54f0d0: a formation needs more than 7

	float wrapAngle(float a)
	{
		a = std::fmod(a, TWO_PI_F);
		if (a > PI_F) a -= TWO_PI_F;
		if (a < -PI_F) a += TWO_PI_F;
		return a;
	}

	// the formation's items (Formations.cpp), with WKB.exe's defaults when a formation has none
	struct FormationItems { int lead = -1, maxRel = -1, minRel = -1; };
	const FormationItems& formationItems()
	{
		static const GameSet* of = nullptr;
		static FormationItems items;
		if (Server::instance && of != Server::instance->gameSet.get()) {
			of = Server::instance->gameSet.get();
			items.lead = of->items.names.getIndex("Lead Unit Relative Speed");
			items.maxRel = of->items.names.getIndex("Movement Maximum Relative Speed");
			items.minRel = of->items.names.getIndex("Movement Minimum Relative Speed");
		}
		return items;
	}
	float itemOr(const ServerGameObject* obj, int item, float fallback)
	{
		if (item < 0)
			return fallback;
		const float v = const_cast<ServerGameObject*>(obj)->getItem(item);
		return v > 0.0f ? v : fallback;
	}

	// the living members, in id order (the same on every machine)
	std::vector<ServerGameObject*> liveMembers(ServerGameObject* formation)
	{
		std::vector<ServerGameObject*> members;
		for (auto& [childType, childList] : formation->children)
			if (childType.bpClass() == Tags::GAMEOBJCLASS_CHARACTER)
				for (CommonGameObject* c : childList)
					if (!(c->flags & CommonGameObject::fTerminated) && !c->dyncast<ServerGameObject>()->deleted)
						members.push_back(c->dyncast<ServerGameObject>());
		std::sort(members.begin(), members.end(), [](auto* a, auto* b) { return a->id < b->id; });
		return members;
	}

	// a place in the world: the layout turned to the formation's heading at its position, kept 1 m
	// inside the map (WKB.exe clamps form-up places so) and on the ground
	Vector3 worldPlace(const ServerGameObject* formation, const std::pair<float, float>& place, const ServerGameObject* member)
	{
		Vector3 v = Vector3(place.first, 0, place.second).transform(const_cast<ServerGameObject*>(formation)->getWorldMatrix());
		if (Server* server = Server::instance; server && server->terrain) {
			const auto [w, h] = server->terrain->getPlayableArea();
			v.x = std::clamp(v.x, 1.0f, std::max(1.0f, w - 1.0f));
			v.z = std::clamp(v.z, 1.0f, std::max(1.0f, h - 1.0f));
			v.y = server->terrain->getHeightEx(v.x, v.z, member && member->blueprint->canWalkOnWater());
		}
		return v;
	}

	std::vector<SrvGORef> pendingDisbands;
}

static std::vector<std::pair<float, float>> placesOf(const ServerGameObject* formation, int count)
{
	// as the original, the separations of one member's type (the first one's)
	const GameObjBlueprint* first = nullptr;
	for (auto& [childType, childList] : formation->children)
		if (childType.bpClass() == Tags::GAMEOBJCLASS_CHARACTER && !childList.empty()) {
			first = childList.front()->blueprint;
			break;
		}
	if (!first)
		return {};
	return formationPlaces(formationTypeOf(formation), count, first->formationSeparationX, first->formationSeparationY);
}

bool FormationController::memberPlace(const ServerGameObject* formation, const ServerGameObject* member, Vector3& place)
{
	const FormationController& fc = formation->formationController;
	auto it = fc.slotOf.find(member->id);
	if (it == fc.slotOf.end() || member->getParent() != formation)
		return false;
	const auto places = placesOf(formation, fc.slotCount);
	if (it->second < 0 || it->second >= (int)places.size())
		return false;
	place = worldPlace(formation, places[it->second], member);
	return true;
}

float FormationController::marchSpeed(float slowestMember) const
{
	// WKB.exe 0x5503d0: the leader walks at "Lead Unit Relative Speed" (0.75) of its own speed; the
	// others catch up (update)
	return slowestMember * itemOr(formation, formationItems().lead, 0.75f);
}

// WKB.exe 0x551710: the places of the layout at the formation's position and heading; each member
// takes the nearest free one, the member whose nearest free place is farthest choosing first (as
// the original's assignment 0x6ee110 looks); then the members' speeds are set so that all arrive at
// once (the slowest needed time, everyone else slower)
void FormationController::formUp()
{
	std::vector<ServerGameObject*> members = liveMembers(formation);
	slotOf.clear();
	slotCount = (int)members.size();
	slotHeading = formation->orientation.y;
	if (members.empty())
		return;
	const auto places = placesOf(formation, slotCount);
	// WKB.exe 0x551710 / 0x55f430: a place on ground the members can't stand on (a palisade, a
	// building, water) makes the formation look for another spot nearby, up to 25 m away, where
	// every place is free; not while it marches (it stays on its path)
	auto allFree = [&]() {
		for (const auto& p : places) {
			const Vector3 v = worldPlace(formation, p, members[0]);
			if (MovementController::isTileBlocked(members[0]->blueprint, (int)(v.x / 5.0f), (int)(v.z / 5.0f)))
				return false;
		}
		return true;
	};
	if (!(formation->movement.isMoving() && !holding) && !allFree()) {
		const Vector3 from = formation->position;
		Vector3 to = from;
		bool found = false;
		for (float r = 2.5f; r <= 25.0f && !found; r += 2.5f)
			for (int k = 0; k < 16 && !found; k++) {
				const float a = k * TWO_PI_F / 16.0f;
				formation->position = from + Vector3(std::sin(a) * r, 0.0f, std::cos(a) * r);
				if (allFree()) {
					to = formation->position;
					found = true;
				}
			}
		formation->position = from;
		if (found)
			formation->setPosition(to);
	}
	std::vector<Vector3> world(places.size());
	for (size_t i = 0; i < places.size(); i++)
		world[i] = worldPlace(formation, places[i], members[0]);
	std::vector<bool> taken(places.size(), false), placed(members.size(), false);
	std::vector<float> dist(members.size(), 0.0f);
	for (size_t round = 0; round < members.size(); round++) {
		int bestMember = -1, bestSlot = -1;
		float bestNear = -1.0f;
		for (size_t m = 0; m < members.size(); m++) {
			if (placed[m])
				continue;
			int nearSlot = -1;
			float near2 = 0.0f;
			for (size_t s = 0; s < world.size(); s++) {
				if (taken[s])
					continue;
				const float d2 = (members[m]->position - world[s]).sqlen2xz();
				if (nearSlot < 0 || d2 < near2) {
					nearSlot = (int)s;
					near2 = d2;
				}
			}
			if (nearSlot >= 0 && near2 > bestNear) {
				bestNear = near2;
				bestMember = (int)m;
				bestSlot = nearSlot;
			}
		}
		if (bestMember < 0)
			break;
		placed[bestMember] = true;
		taken[bestSlot] = true;
		slotOf[members[bestMember]->id] = bestSlot;
		dist[bestMember] = std::sqrt(bestNear);
	}
	// arriving together: T = the longest walk's time; each member at dist / T (not below 30 % of
	// its speed, so that nobody crawls)
	float longest = 0.0f;
	for (size_t m = 0; m < members.size(); m++) {
		const float v = members[m]->baseSpeed();
		if (v > 0.0f)
			longest = std::max(longest, dist[m] / v);
	}
	for (size_t m = 0; m < members.size(); m++) {
		const float v = members[m]->baseSpeed();
		float pace = (longest > 0.0f && v > 0.0f) ? dist[m] / (longest * v) : 1.0f;
		members[m]->formationPace = std::clamp(std::round(pace * 20.0f) / 20.0f, 0.3f, 1.0f);
	}
	formed = true;
	layoutOf = formation->blueprint;
	if (Server::instance && Server::instance->gameSet) {
		static const GameSet* of = nullptr;
		static int formUpEvent = -1;
		if (of != Server::instance->gameSet.get()) {
			of = Server::instance->gameSet.get();
			formUpEvent = of->events.names.getIndex("On Formation Form Up");
		}
		if (formUpEvent >= 0)
			formation->sendEvent(formUpEvent);
	}
}

bool FormationController::steer(const Vector3& dir, float dt)
{
	if (dir.sqlen2xz() < 1e-6f)
		return holding;
	const float want = std::atan2(dir.x, -dir.z);
	const float off = wrapAngle(want - formation->orientation.y);
	const float now = Server::instance ? Server::instance->timeManager.currentTime : 0.0f;
	if (!wasMarching) {
		wasMarching = true;
		// setting off (WKB.exe 0x54fb90): more than 45° from the path's way, the formation first
		// forms up where it is, facing along the path, then marches
		if (std::abs(off) > PI_F / 4.0f) {
			formation->orientation.y = want;
			formUp();
			holding = true;
			holdUntil = now + 10.0f;
			return true;
		}
	}
	if (holding) {
		// held until every member is near its place (or 10 s at most)
		bool ready = true;
		for (ServerGameObject* m : liveMembers(formation)) {
			Vector3 place;
			if (!m->orderConfig.getCurrentOrder() && memberPlace(formation, m, place) && (m->position - place).sqlen2xz() > 2.0f * 2.0f) {
				ready = false;
				break;
			}
		}
		if (!ready && now < holdUntil)
			return true;
		holding = false;
		released = true;
	}
	// wheeling: the outer file walks at most the formation's speed again, so the block turns at
	// speed / half its width (at least 2 m) radians a second
	float halfWidth = 2.0f;
	for (const auto& p : placesOf(formation, slotCount))
		halfWidth = std::max(halfWidth, std::abs(p.first));
	const float rate = std::max(formation->currentSpeed, 0.5f) / halfWidth;
	const float step = rate * dt;
	formation->orientation.y = std::abs(off) <= step ? want : wrapAngle(formation->orientation.y + (off > 0 ? step : -step));
	slotHeading = formation->orientation.y;
	return false;
}

void FormationController::disband(ServerGameObject* formation)
{
	if (!formation || formation->blueprint->bpClass != Tags::GAMEOBJCLASS_FORMATION)
		return;
	ServerGameObject* parent = formation->getParent();
	for (auto& [_, subType] : formation->children) {
		auto subTypeCopy = subType;
		for (auto* sub : subTypeCopy) {
			ServerGameObject* s = (ServerGameObject*)sub;
			s->formationPace = 1.0f;
			s->setParent(parent);
		}
	}
	formation->sendEvent(Tags::PDEVENT_ON_FORMATION_DISBAND);
}

void FormationController::faceMembersWay(ServerGameObject* formation)
{
	// (headings: 0 faces -z, atan2(x, -z): the mean of the members' unit vectors)
	float sx = 0.0f, cz = 0.0f;
	for (auto& [unitType, units] : formation->children)
		for (CommonGameObject* unit : units) {
			sx += std::sin(unit->orientation.y);
			cz += std::cos(unit->orientation.y);
		}
	if (sx * sx + cz * cz > 1e-6f)
		formation->setOrientation(Vector3(0.0f, std::atan2(sx, cz), 0.0f));
}

void FormationController::flushDisbands()
{
	auto list = std::move(pendingDisbands);
	pendingDisbands.clear();
	for (const SrvGORef& ref : list)
		if (ServerGameObject* f = ref.get(); f && !f->deleted)
			disband(f);
}

// A formation standing without an order turns to face an attack from its flank or rear (WK1's
// manual: "a formation will turn to face an attack"): the members hit in the last 2 s, by enemies
// within 80 m, give the attackers' middle; more than 50° off its front, the formation turns there
// (at most every 4 s, so that it doesn't swing between two attacks). The members then walk to
// their places on the new front; those already fighting keep fighting.
void FormationController::faceAttackers()
{
	Server* server = Server::instance;
	if (!server || formation->movement.isMoving() || formation->orderConfig.getCurrentOrder())
		return;
	const float now = server->timeManager.currentTime;
	static std::map<uint32_t, float> lastTurn;
	if (auto it = lastTurn.find(formation->id); it != lastTurn.end() && now >= it->second && now - it->second < 4.0f)
		return;
	ServerGameObject* own = formation->getPlayer();
	Vector3 sum;
	int count = 0;
	for (auto& [childType, childList] : formation->children) {
		if (childType.bpClass() != Tags::GAMEOBJCLASS_CHARACTER)
			continue;
		for (CommonGameObject* c : childList) {
			ServerGameObject* member = c->dyncast<ServerGameObject>();
			if ((member->flags & CommonGameObject::fTerminated) || !member->lastHitBy || member->lastHitTime < 0.0f
				|| now - member->lastHitTime > 2.0f || now < member->lastHitTime)
				continue;
			ServerGameObject* attacker = server->findObject(member->lastHitBy);
			if (!attacker || (attacker->flags & CommonGameObject::fTerminated) || attacker->getPlayer() == own)
				continue;
			if ((attacker->position - formation->position).sqlen2xz() > 80.0f * 80.0f)
				continue;
			sum += attacker->position;
			count++;
		}
	}
	if (!count)
		return;
	const Vector3 dir = sum / (float)count - formation->position;
	if (dir.sqlen2xz() < 1.0f)
		return;
	const float want = std::atan2(dir.x, -dir.z);
	float off = std::fmod(want - formation->orientation.y, 6.2831855f);
	if (off > 3.14159265f) off -= 6.2831855f;
	if (off < -3.14159265f) off += 6.2831855f;
	if (std::abs(off) < 50.0f * 3.14159265f / 180.0f)
		return;
	formation->setOrientation(Vector3(0.0f, want, 0.0f));
	lastTurn[formation->id] = now;
}

void FormationController::update()
{
	const FormationType type = formationTypeOf(formation);
	Server* server = Server::instance;
	std::vector<ServerGameObject*> members = liveMembers(formation);
	if (members.empty())
		return;

	// members gone (dead, left): their places stay empty, but an Orb closes up; a formation that had
	// formed up and has fewer than 8 left disbands (WKB.exe 0x54f1d0)
	bool lost = false;
	for (auto it = slotOf.begin(); it != slotOf.end();) {
		const bool here = std::any_of(members.begin(), members.end(), [&](auto* m) { return m->id == it->first; });
		if (!here) {
			it = slotOf.erase(it);
			lost = true;
		}
		else
			++it;
	}
	if (lost && formed && (int)members.size() < MIN_MEMBERS) {
		pendingDisbands.emplace_back(formation);
		formed = false;
		return;
	}
	const bool marching = formation->movement.isMoving();
	if (!marching) {
		wasMarching = false;
		holding = false;
	}
	faceAttackers();
	// a form-up: the first time, a member without a place (joined), an Orb that lost one, or a turn
	// while standing (a facing order, an attack from the flank)
	const bool joined = std::any_of(members.begin(), members.end(), [&](auto* m) { return !slotOf.count(m->id); });
	if (!formed || joined || (lost && type == FormationType::Orb) || layoutOf != formation->blueprint
		|| (!marching && std::abs(wrapAngle(formation->orientation.y - slotHeading)) > 0.35f))
		formUp();

	const auto places = placesOf(formation, slotCount);
	const FormationItems& items = formationItems();
	const float maxRel = itemOr(formation, items.maxRel, 1.0f), minRel = itemOr(formation, items.minRel, 0.0f);
	const float formSpeed = marching && !holding ? formation->currentSpeed : 0.0f;
	const Vector3 marchDir = marching ? formation->movement.getDirection() : Vector3(0, 0, 0);
	// the middle of the places (an Orb's members face away from it)
	Vector3 centre(0, 0, 0);
	for (const auto& p : places)
		centre += Vector3(p.first, 0, p.second);
	if (!places.empty())
		centre = (centre / (float)places.size()).transform(formation->getWorldMatrix());

	for (ServerGameObject* member : members) {
		if (member->orderConfig.getCurrentOrder() != nullptr)
			continue;
		auto it = slotOf.find(member->id);
		if (it == slotOf.end() || it->second >= (int)places.size())
			continue;
		const Vector3 slot = worldPlace(formation, places[it->second], member);
		const float d = std::sqrt((member->position - slot).sqlen2xz());
		// marching, the member keeps pace with its place (WKB.exe 0x5503d0): a little faster the
		// farther behind, between the formation's minimum and maximum relative speeds, never above
		// its own speed; it aims a second ahead of its place so that it walks on rather than stop
		// and start at every step
		Vector3 target = slot;
		if (formSpeed > 0.0f) {
			const float v = member->baseSpeed();
			const float want = formSpeed * std::clamp(1.0f + (d - 1.0f) / 3.0f, std::max(minRel, 0.5f), std::max(maxRel, 1.0f) + 0.5f);
			member->formationPace = v > 0.0f ? std::clamp(std::round(want / v * 20.0f) / 20.0f, 0.3f, 1.0f) : 1.0f;
			target = slot + marchDir * formSpeed;
			target.y = slot.y;
		}
		// The members walk to their places (with their walking animation, around obstacles)
		// instead of being put there. A new walk starts only when the place has moved on from where
		// the member is heading, so that the paths aren't recomputed every tick.
		constexpr float ARRIVED = 0.6f, RETARGET = 1.5f;
		const bool moving = member->movementController.isMoving();
		if (d > ARRIVED || formSpeed > 0.0f) {
			if (!moving || (member->movementController.getRequestedDestination() - target).sqlen2xz() > RETARGET * RETARGET)
				member->movementController.startMovement(target);
		}
		else if (!marching || holding) {
			// in place, and the formation stands: stop, facing its way (an Orb's outwards)
			if (moving)
				member->movementController.stopMovement();
			member->formationPace = 1.0f;
			float face = formation->orientation.y;
			if (type == FormationType::Orb && (slot - centre).sqlen2xz() > 0.25f)
				face = std::atan2(slot.x - centre.x, -(slot.z - centre.z));
			if (member->orientation.y != face)
				member->setOrientation(Vector3(0, face, 0));
		}
	}
}
