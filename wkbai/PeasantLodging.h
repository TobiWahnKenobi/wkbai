// wkbai - the peasants' lodging (wkbai's own, not in WKB.exe; off unless the lobby's switches and
// day and night or the seasons are on). docs/engine/peasant-lodging.md has the rules.
// Licensed under the GNU General Public License 3
//
// - Night ("peasantsSleep", with day and night): at dusk every peasant of every player goes into
//   the nearest of its own houses ("City Houses"), villages, manors, keeps, castles and palaces,
//   and back to its work at dawn. Nobody gathers at night; the farmers' food support goes on
//   ("Number Of Farmers" counts them as farming).
// - Winter ("farmersWinter", with the seasons): the farmers go in at winter's first dusk (without
//   day and night: when winter comes) and stay until spring's first dawn. Their support is the
//   winter's (ServerEnvironment::supportScale), and they give the player a small bonus on the
//   wood and gold delivered ("Winter Crafts Bonus": 0.2% each, at most 5%; the game set patch
//   "wkbai patches\Winter Crafts.cpp").
// - Builders, repairers, fire-fighters and peasants called up as militia go on with their job.
// - On the way in they can't be selected (nor given orders, by the player, the AI or the scripts);
//   inside they are hidden and can't be hurt (disabled, as a garrison). They can be selected
//   again once they leave for work. A shelter falling below a tenth of its structural integrity,
//   or gone, lets its sleepers out to the next one.
// - The walks are sped up (up to 2x) so that they take at most 5% of the day.
// Once a game second from ServerEnvironment::update, without random numbers (the departures are
// spread by a hash of the unit's id), saved in the save file's environment section.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>
#include "util/vecmat.h"

struct Server;
struct ServerGameObject;
struct OrderBlueprint;
struct GameObjBlueprint;
struct EnvironmentSettings;
struct EnvironmentState;

enum LodgeState : uint8_t { LODGE_NONE = 0, LODGE_GOING = 1, LODGE_INSIDE = 2, LODGE_WAITING = 3, LODGE_RETURNING = 4 };

// set while the lodging gives a lodger its orders: OrderConfiguration::assignOrder refuses every
// other order to a peasant going in, inside or waiting (Order.cpp)
extern bool g_lodgingOrders;

struct PeasantLodging {
	// once a game second (ServerEnvironment::update), with the sun's daylight of the moment
	void update(Server& server, const EnvironmentSettings& settings, const EnvironmentState& state, float daylight);
	// the peasants of a player counted as farming while lodged (going in, inside, waiting): added
	// to the scripts' "Number Of Farmers" (server.cpp), so that the food support goes on
	int farmersOf(uint32_t player) const { auto it = farmers.find(player); return it != farmers.end() ? it->second : 0; }
	bool active() const { return night || winter || !lodgers.empty(); }
	int lodgedCount() const { int n = 0; for (const auto& [id, l] : lodgers) n += l.state == LODGE_INSIDE; return n; }
	size_t count() const { return lodgers.size(); }

	// the save file's environment section: "lodging ..." and "lodger ..." lines
	std::string saveText(Server& server) const;
	bool loadLine(const std::string& tag, const std::string& line);
	// after the world is loaded: the units' lodging states, flags and speeds as saved
	void afterLoad(Server& server);

	// for the tests (test 30's AITEST lines)
	struct Stats { int departures = 0, lodged = 0, woken = 0, waited = 0, evicted = 0, stuck = 0; } stats;
	bool trace = false;

private:
	struct Lodger {
		uint8_t state = LODGE_NONE;
		bool farmer = false;
		uint8_t tries = 0;
		uint32_t shelter = 0;
		const OrderBlueprint* order = nullptr; // the work to go back to, and its target
		uint32_t target = 0;
		const GameObjBlueprint* targetBp = nullptr;
		Vector3 work;                           // where it worked (the target's place, or its own)
		float since = 0.0f;                     // when the state began
		float until = 0.0f;                     // returning: when the sped-up walk ends at the latest
	};
	std::map<uint32_t, Lodger> lodgers;    // by unit id, in order
	std::map<uint32_t, int> farmers;       // by player id
	bool night = false, winter = false;
	float lastChange = -1e9f;              // when night or winter last began or ended
	float daySeconds = 1200.0f;
	bool dayNight = false;

	void depart(Server& server, ServerGameObject* p, Lodger& l, const std::vector<ServerGameObject*>& shelters, float now, bool keepWork);
	void lodge(ServerGameObject* p, Lodger& l, float now);
	void wake(Server& server, ServerGameObject* p, Lodger& l, float now);
	float commuteSpeed(float distance) const;
};
