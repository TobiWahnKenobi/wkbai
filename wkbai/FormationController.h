#pragma once

#include <cstdint>
#include <map>
#include <vector>

#include "util/vecmat.h"

struct ServerGameObject;
struct Server;

enum class FormationType {
	Line,
	Column,
	Wedge,
	Orb
};

// A formation's members and their places, as WKB.exe's Server_Formation (docs/re/formation-movement.md):
// - every member keeps its place (slot) until the formation forms up again; a form-up (creation, a
//   member joining, a turn while standing, a march setting off the other way, an Orb losing a
//   member) gives each member the nearest free place of the new layout, at speeds that bring
//   everyone there at once, and fires "On Formation Form Up";
// - marching, the block wheels round the path's corners at a rate its width allows instead of
//   snapping round; its leader walks at "Lead Unit Relative Speed" of the slowest member, and the
//   members keep up between "Movement Minimum/Maximum Relative Speed" of that;
// - a formation that has formed up disbands when fewer than 8 members are left ("On Formation
//   Disband"); a dead member's place stays empty (but in an Orb).
class FormationController
{
public:
	FormationController(ServerGameObject* formation) : formation(formation) {}
	void update();
	// a member's place in its formation now (false when it isn't a member)
	static bool memberPlace(const ServerGameObject* formation, const ServerGameObject* member, Vector3& place);
	// the formation marching along `dir` for `dt` seconds: turns towards it (or stops to form up
	// facing it); true while the march is held for the form-up
	bool steer(const Vector3& dir, float dt);
	// true once after a held march is let go (the walk is sent to the clients again)
	bool takeReleased() { const bool r = released; released = false; return r; }
	// the formation's own speed, from its slowest member's
	float marchSpeed(float slowestMember) const;
	// disband a formation (DISBAND_FORMATION, too few members left): its subordinates back to its
	// controller, "On Formation Disband"
	static void disband(ServerGameObject* formation);
	// the disbands decided while the object tree was walked, done after it
	static void flushDisbands();
	// a new formation faces the way its members face (their mean heading), not the map's north
	static void faceMembersWay(ServerGameObject* formation);
	// form up again where it stands (CREATE_FORMATION of a formation: "Reform Yourself", the Halt
	// command's)
	void requestFormUp() { reformWanted = true; }
private:
	// standing without an order: turn to face an attack from the flank or rear
	void faceAttackers();
	// give every member the nearest free place of the layout at the formation's position and heading
	void formUp();
	// The formation's own track (WKB.exe keeps one path, F+0x300, that every member travels at its
	// row's distance behind the leader): the point s metres along it from the front row's centre,
	// ahead on the path still to walk (s > 0) or back along where it has been (s < 0)
	Vector3 trackPoint(float s) const;
	// a straight track behind the formation along its heading, as long as `length` (a march starts)
	void seedTrail(float length);
	// note where the front row's centre is now (marching), keeping `length` metres of the track
	void recordTrail(float length);
	ServerGameObject* formation;
	std::map<uint32_t, int> slotOf; // member id -> place index
	int slotCount = 0; // places in the layout (more than members after deaths)
	float slotHeading = 0.0f; // the heading the places were given at
	bool formed = false;
	bool holding = false; // the march waits for the members to form up facing its way
	float holdUntil = 0.0f;
	bool wasMarching = false;
	bool released = false;
	const void* layoutOf = nullptr; // the formation type (blueprint) the places were given for
	std::vector<Vector3> trail; // where the front row's centre was, oldest first, 1 m apart
	bool trailActive = false; // the trail belongs to the march going on
	bool reformWanted = false;
	bool distorted = false; // members fight out of place, the others make room (WKB.exe F+0x2ee)
	bool wheelingIn = false; // the last stretch: turning into the order's end facing
	float wheelHeading = 0.0f;
};
