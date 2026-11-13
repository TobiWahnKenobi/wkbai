// wkbai - what the AI layers share (AIAdvanced.cpp): how units are told apart, a force's
// strength, what is near, and the orders the Lua AIs and the ministry AI give
#pragma once

#include <string>
#include <utility>
#include <vector>
#include "util/vecmat.h"

struct ServerGameObject;

namespace AIUtil {
	// "I'm A Military Unit": 1 heavy infantry, 2 light infantry (archers), 3 heavy cavalry, 4 light cavalry, 0 none
	int ClassOf(const ServerGameObject* o);
	bool IsSoldier(const ServerGameObject* o);
	bool IsPeasant(const ServerGameObject* o);
	// a character that fights (soldiers, and armed ones without a class: Monks)
	bool IsFighter(const ServerGameObject* o);
	// the object's owner is at war with me
	bool IsEnemy(ServerGameObject* me, ServerGameObject* other);
	bool IsHuman(const ServerGameObject* player);
	// a unit's power (damage per hit and hit points), and a force's strength against another
	// (weighed by the classes, squared: Lanchester)
	float UnitPower(const ServerGameObject* o);
	float Strength(const std::vector<ServerGameObject*>& units, const std::vector<ServerGameObject*>& against);
	// how a class (0-4) fares against another, as a factor on its power
	float ClassFactor(int cls, int against);
	// enemy soldiers (and towers) within the radius
	std::vector<ServerGameObject*> EnemiesNear(ServerGameObject* me, const Vector3& centre, float radius, bool towers);
	// enemy buildings within the radius, nearest first
	std::vector<ServerGameObject*> EnemyBuildingsNear(ServerGameObject* me, const Vector3& centre, float radius);
	// enemy peasants within the radius, nearest first
	std::vector<ServerGameObject*> EnemyPeasantsNear(ServerGameObject* me, const Vector3& centre, float radius);

	void CollectUnits(ServerGameObject* o, std::vector<ServerGameObject*>& units);
	void CollectArmies(ServerGameObject* player, std::vector<ServerGameObject*>& armies);
	void CollectTowns(ServerGameObject* player, std::vector<ServerGameObject*>& towns);
	std::vector<ServerGameObject*> BuildingsOf(ServerGameObject* town);
	ServerGameObject* HomeArmy(ServerGameObject* player);
	std::vector<ServerGameObject*> HomeSoldiers(ServerGameObject* player);
	// the player's palace, or null
	ServerGameObject* Palace(ServerGameObject* player);
	// the player's peasants (straight under it)
	std::vector<ServerGameObject*> Peasants(ServerGameObject* player);

	// each unit attacks the target
	void Attack(const std::vector<ServerGameObject*>& units, ServerGameObject* target);
	// soldiers back into the defensive army, all walking to the manor
	void SendHome(ServerGameObject* player, const std::vector<ServerGameObject*>& units);
	// units into an army of the AI's own (the given one, else a new one) walking to the point;
	// returns the army
	ServerGameObject* Hold(ServerGameObject* player, const std::vector<ServerGameObject*>& units, const Vector3& at, ServerGameObject* army);
	// a wave in formations at the target (AIController::launchWave); returns its army
	ServerGameObject* Launch(ServerGameObject* player, const std::vector<ServerGameObject*>& units, ServerGameObject* target);
	// the player's idle buildings and the fighter types each can train now (possible, waited for,
	// paid for), by building id
	std::vector<std::pair<ServerGameObject*, std::vector<std::string>>> Trainers(ServerGameObject* player, bool fightersOnly = true);
	// a building trains one of the type, for the defensive army; whether it was ordered
	bool Train(ServerGameObject* player, ServerGameObject* building, const std::string& typeName);
}
