#pragma once

struct ServerGameObject;

enum class FormationType {
	Line,
	Column,
	Wedge,
	Orb
};

class FormationController
{
public:
	FormationController(ServerGameObject* formation) : formation(formation) {}
	void update();
	// a member's place in its formation now (false when it isn't a member)
	static bool memberPlace(const ServerGameObject* formation, const ServerGameObject* member, struct Vector3& place);
private:
	// standing without an order: turn to face an attack from the flank or rear
	void faceAttackers();
	ServerGameObject* formation;
	//FormationType type = FormationType::Line;
};