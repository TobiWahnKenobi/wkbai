// wkbai - the peasants' lodging (PeasantLodging.h; docs/engine/peasant-lodging.md)
// Licensed under the GNU General Public License 3

#include "PeasantLodging.h"
#include "Environment.h"
#include "server.h"
#include "terrain.h"
#include "tags.h"
#include "NNSearch.h"
#include "Order.h"
#include "gameset/gameset.h"
#include "gameset/command.h"
#include "gameset/Footprint.h"
#include "gameset/GameObjBlueprint.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <set>
#include <sstream>
#include <vector>

bool g_lodgingOrders = false;

namespace {
	constexpr float WALK_SPEED = 3.5f;   // m/s: a peasant's walk, for the time a commute takes
	constexpr float DARK = 0.5f;         // daylight under which it is night (the sun just set)
	constexpr float EVICT_SHARE = 0.1f;  // a shelter below this share of its structural integrity lets its sleepers out
	constexpr float BONUS_EACH = 0.002f, BONUS_MOST = 0.05f; // the winter's crafts: per farmer indoors, at most

	bool ContainsCI(const std::string& s, const char* what)
	{
		const size_t n = strlen(what);
		if (n > s.size())
			return false;
		for (size_t i = 0; i + n <= s.size(); i++) {
			size_t k = 0;
			while (k < n && std::tolower((unsigned char)s[i + k]) == std::tolower((unsigned char)what[k]))
				k++;
			if (k == n)
				return true;
		}
		return false;
	}

	// the game set's indices, looked up once per game set
	struct LItems {
		const GameSet* of = nullptr;
		int moveCommand = -1, peasant = -1, si = -1, siCap = -1, bonus = -1, farming = -1;
		std::vector<int> exempt; // orders a peasant goes on with at night: building, repairing, fire, militia
		void update(const GameSet* gs) {
			if (of == gs)
				return;
			of = gs;
			moveCommand = gs->commands.names.getIndex("Move");
			peasant = gs->items.names.getIndex("I Am A Peasant");
			si = gs->items.names.getIndex("Structural Integrity");
			siCap = gs->items.names.getIndex("Structural Integrity Capacity");
			bonus = gs->items.names.getIndex("Winter Crafts Bonus");
			farming = gs->orderCategories.names.getIndex("Order Farming");
			exempt.clear();
			for (const char* o : { "Build", "Build Wall", "Repair", "Extinguish", "Go to Associate and Convert to Militia", "Convert to Militia" })
				if (int i = gs->orders.names.getIndex(o); i >= 0)
					exempt.push_back(i);
		}
	} g_l;

	// a peasant's place in the departures after a change: 0..1 by its id (no random numbers)
	float Stagger(uint32_t id)
	{
		uint32_t h = id * 0x9E3779B1u;
		h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12;
		return (h & 0xFFFF) / 65536.0f;
	}

	// a house, village, manor, keep, castle or palace (built: foundations are blueprints of their own)
	bool IsShelterBp(const GameObjBlueprint* bp)
	{
		if (bp->bpClass != Tags::GAMEOBJCLASS_BUILDING)
			return false;
		const std::string& n = bp->name;
		if (ContainsCI(n, "foundation") || ContainsCI(n, "barbarian"))
			return false;
		return ContainsCI(n, "houses") || ContainsCI(n, "village") || ContainsCI(n, "manor") || ContainsCI(n, "keep")
			|| ContainsCI(n, "castle") || ContainsCI(n, "palace");
	}

	bool ShelterUsable(ServerGameObject* b)
	{
		if (!b || b->deleted || (b->flags & CommonGameObject::fTerminated))
			return false;
		if (g_l.si >= 0 && g_l.siCap >= 0) {
			const float cap = b->getItem(g_l.siCap);
			if (cap > 0.0f && b->getItem(g_l.si) < EVICT_SHARE * cap)
				return false;
		}
		return true;
	}

	// half the building's width in metres, from its footprint (5 m tiles)
	float ShelterRadius(const ServerGameObject* b)
	{
		int most = 1;
		if (const Footprint* f = b->blueprint->footprint)
			for (const auto& t : f->tiles)
				most = std::max({ most, std::abs(t.offsetX), std::abs(t.offsetZ) });
		return (most + 0.5f) * 5.0f;
	}

	// a player's peasants (garrisoned ones, under a building, left alone) and shelters, in the tree's order
	void Collect(ServerGameObject* o, std::vector<ServerGameObject*>& peasants, std::vector<ServerGameObject*>& shelters)
	{
		for (const auto& [type, children] : o->children)
			for (CommonGameObject* c : children) {
				auto* u = (ServerGameObject*)c;
				if (u->deleted)
					continue;
				if (u->blueprint->bpClass == Tags::GAMEOBJCLASS_CHARACTER) {
					if (!(u->flags & CommonGameObject::fTerminated) && g_l.peasant >= 0 && u->getItem(g_l.peasant) > 0.0f)
						peasants.push_back(u);
				}
				else if (u->blueprint->bpClass == Tags::GAMEOBJCLASS_BUILDING) {
					if (IsShelterBp(u->blueprint))
						shelters.push_back(u);
				}
				else
					Collect(u, peasants, shelters);
			}
	}

	// a building the peasants deliver to (Resource Delivery.cpp: villages, manors, the keep and up,
	// warehouses; the mills and docks)
	bool DeliveryPoint(const ServerGameObject* b)
	{
		if (b->blueprint->bpClass != Tags::GAMEOBJCLASS_BUILDING)
			return false;
		const std::string& n = b->blueprint->name;
		return IsShelterBp(b->blueprint) || ContainsCI(n, "warehouse") || ContainsCI(n, "windmill") || ContainsCI(n, "sawmill")
			|| ContainsCI(n, "dock");
	}

	// the work's target: the tree, mine, farm or shop of the order's tasks, not the building it was
	// on its way to deliver to. ("AI Chop Tree" given back on its delivery task's Manor had the
	// peasants chop their own Manor down at dawn, and the player was gone: AIWeather.cpp's lesson.)
	ServerGameObject* WorkTarget(ServerGameObject* u)
	{
		Order* o = u->orderConfig.getCurrentOrder();
		if (!o)
			return nullptr;
		for (const auto& t : o->tasks) {
			ServerGameObject* tg = t ? t->target.get() : nullptr;
			if (tg && !tg->deleted && !(tg->flags & CommonGameObject::fTerminated) && !DeliveryPoint(tg))
				return tg;
		}
		return nullptr;
	}

	bool Farming(ServerGameObject* u)
	{
		const Order* o = u->orderConfig.getCurrentOrder();
		return o && g_l.farming >= 0 && o->blueprint->category == g_l.farming;
	}

	bool Exempt(ServerGameObject* u)
	{
		const Order* o = u->orderConfig.getCurrentOrder();
		if (!o)
			return false;
		const GameSet* gs = Server::instance->gameSet.get();
		for (int i : g_l.exempt)
			if (o->blueprint == &gs->orders[i])
				return true;
		return false;
	}

	void MoveTo(Server& server, ServerGameObject* u, const Vector3& to)
	{
		if (g_l.moveCommand < 0)
			return;
		g_lodgingOrders = true;
		server.gameSet->commands[g_l.moveCommand].execute(u, nullptr, Tags::ORDERASSIGNMODE_FORGET_EVERYTHING_ELSE, to);
		g_lodgingOrders = false;
	}

	void SetSelectable(ServerGameObject* u, bool on)
	{
		const int f = on ? (u->flags | CommonGameObject::fSelectable) : (u->flags & ~CommonGameObject::fSelectable);
		if (f != u->flags)
			u->updateFlags(f);
	}
}

float PeasantLodging::commuteSpeed(float distance) const
{
	// at most 5% of the day on the way: faster when the days are short, never more than twice
	if (!dayNight)
		return 1.0f;
	const float window = 0.05f * daySeconds;
	return std::clamp(std::round(distance / (WALK_SPEED * window) * 20.0f) / 20.0f, 1.0f, 2.0f);
}

void PeasantLodging::depart(Server& server, ServerGameObject* p, Lodger& l, const std::vector<ServerGameObject*>& shelters, float now, bool keepWork)
{
	if (!keepWork) {
		l.farmer = Farming(p);
		const Order* o = p->orderConfig.getCurrentOrder();
		l.order = o ? o->blueprint : nullptr;
		ServerGameObject* t = WorkTarget(p);
		l.target = t ? t->id : 0;
		l.targetBp = t ? t->blueprint : nullptr;
		l.work = t ? t->position : p->position;
		stats.departures++;
	}
	ServerGameObject* best = nullptr;
	float bestD = 1e30f;
	for (ServerGameObject* b : shelters)
		if (ShelterUsable(b)) {
			const float d = (b->position - p->position).sqlen2xz();
			if (d < bestD) {
				bestD = d;
				best = b;
			}
		}
	SetSelectable(p, false);
	l.since = now;
	l.tries = 0;
	if (!best) {
		// no shelter of its own: it stops and waits where it is until morning (or a shelter is built)
		if (l.state != LODGE_WAITING)
			stats.waited++;
		l.state = LODGE_WAITING;
		l.shelter = 0;
		p->lodging = LODGE_WAITING;
		p->lodgeSpeed = 1.0f;
		g_lodgingOrders = true;
		p->orderConfig.cancelAllOrders();
		g_lodgingOrders = false;
		return;
	}
	l.state = LODGE_GOING;
	l.shelter = best->id;
	p->lodging = LODGE_GOING;
	const float r = ShelterRadius(best);
	Vector3 dir = p->position - best->position;
	dir.y = 0.0f;
	const float len = std::sqrt(dir.sqlen2xz());
	dir = len > 0.1f ? dir * (1.0f / len) : Vector3(0, 0, 1);
	Vector3 to = best->position + dir * (r + 1.5f);
	to.y = server.terrain->getHeight(to.x, to.z);
	p->lodgeSpeed = commuteSpeed(std::max(0.0f, len - r));
	MoveTo(server, p, to);
	if (trace)
		printf("LODGING t=%.0f %u goes to %s %u (%.0f m, speed x%.2f)%s\n", now, p->id, best->blueprint->name.c_str(), best->id, len, p->lodgeSpeed, l.farmer ? " farmer" : "");
}

void PeasantLodging::lodge(ServerGameObject* p, Lodger& l, float now)
{
	l.state = LODGE_INSIDE;
	l.since = now;
	p->lodging = LODGE_INSIDE;
	p->lodgeSpeed = 1.0f;
	g_lodgingOrders = true;
	p->disable(); // (hidden, not selectable or targetable, its orders cancelled)
	g_lodgingOrders = false;
	stats.lodged++;
}

void PeasantLodging::wake(Server& server, ServerGameObject* p, Lodger& l, float now)
{
	if (l.state == LODGE_INSIDE)
		p->enable();
	SetSelectable(p, true);
	stats.woken++;
	// the work it left: the same target, else the nearest of its kind near where it worked
	// (a felled tree's neighbour, a rebuilt farm), else nothing (idle)
	ServerGameObject* target = l.target ? server.findObject(l.target) : nullptr;
	if (target && (target->deleted || (target->flags & CommonGameObject::fTerminated)))
		target = nullptr;
	if (!target && l.targetBp) {
		NNSearch search;
		search.start(&server, l.work, 40.0f);
		float bestD = 1e30f;
		while (CommonGameObject* c = search.next()) {
			auto* o = (ServerGameObject*)c;
			if (o->deleted || o->blueprint != l.targetBp || (o->flags & CommonGameObject::fTerminated) || !o->isInteractable())
				continue;
			const float d = (o->position - l.work).sqlen2xz();
			if (d < bestD) {
				bestD = d;
				target = o;
			}
		}
	}
	const float dist = std::sqrt(((target ? target->position : l.work) - p->position).sqlen2xz());
	p->lodging = LODGE_RETURNING;
	p->lodgeSpeed = commuteSpeed(dist);
	l.state = LODGE_RETURNING;
	l.since = now;
	l.until = now + dist / (WALK_SPEED * p->lodgeSpeed) + 10.0f;
	// (without a target of its work: idle, for the player, the AI or the scripts to set to work; an
	// order given back without its target could go anywhere)
	if (l.order && target && !DeliveryPoint(target)) {
		g_lodgingOrders = true;
		p->orderConfig.addOrder(l.order, Tags::ORDERASSIGNMODE_FORGET_EVERYTHING_ELSE, target);
		g_lodgingOrders = false;
	}
	if (trace)
		printf("LODGING t=%.0f %u back to %s on %s (%.0f m)\n", now, p->id, l.order ? server.gameSet->orders.getString(l.order->bpid).c_str() : "nothing",
			target ? target->blueprint->name.c_str() : "-", dist);
}

void PeasantLodging::update(Server& server, const EnvironmentSettings& settings, const EnvironmentState& state, float daylight)
{
	const bool sleepOn = settings.dayNight && settings.peasantsSleep;
	const bool winterOn = settings.seasons && settings.farmersWinter;
	if (!sleepOn && !winterOn && lodgers.empty() && !night && !winter)
		return;
	if (!server.getLevel() || !server.gameSet)
		return;
	g_l.update(server.gameSet.get());
	const float now = server.timeManager.currentTime;
	dayNight = settings.dayNight;
	daySeconds = settings.dayLengthS();

	// night: from dusk to dawn; winter (the farmers'): from winter's first dusk to spring's first dawn
	const bool dark = settings.dayNight && daylight < DARK;
	const bool nightNow = sleepOn && dark;
	if (nightNow != night) {
		night = nightNow;
		lastChange = now;
		if (trace)
			printf("LODGING t=%.0f %s\n", now, night ? "dusk: the peasants go in" : "dawn: back to work");
	}
	bool winterNow = winter;
	if (!winterOn)
		winterNow = false;
	else if (state.season == 3 && !winter && (!settings.dayNight || dark))
		winterNow = true;
	else if (state.season != 3 && winter && (!settings.dayNight || !dark))
		winterNow = false;
	if (winterNow != winter) {
		winter = winterNow;
		lastChange = now;
		if (trace)
			printf("LODGING t=%.0f %s\n", now, winter ? "winter: the farmers go in" : "spring: the farmers go out");
	}
	// the departures and the wakings spread over half a minute (3% of a short day)
	const float spread = settings.dayNight ? std::min(30.0f, 0.03f * daySeconds) : 20.0f;

	std::set<uint32_t> seen;
	farmers.clear();
	for (const auto& [type, children] : server.getLevel()->children) {
		if (type.bpClass() != Tags::GAMEOBJCLASS_PLAYER)
			continue;
		for (CommonGameObject* pc : children) {
			auto* player = (ServerGameObject*)pc;
			if (player->blueprint->name == "Neutral Player")
				continue;
			std::vector<ServerGameObject*> peasants, shelters;
			Collect(player, peasants, shelters);
			int farming = 0, wintering = 0;
			for (ServerGameObject* p : peasants) {
				auto it = lodgers.find(p->id);
				if (it == lodgers.end()) {
					// (a lodging state without its record: cleared, the unit free again)
					if (p->lodging != LODGE_NONE) {
						p->lodging = LODGE_NONE;
						p->lodgeSpeed = 1.0f;
						SetSelectable(p, p->disableCount <= 0);
					}
					// garrisoned or otherwise disabled by the scripts: left alone
					if (p->disableCount > 0)
						continue;
					const bool want = night || (winter && Farming(p));
					if (!want || Exempt(p) || now < lastChange + spread * Stagger(p->id))
						continue;
					it = lodgers.emplace(p->id, Lodger()).first;
					depart(server, p, it->second, shelters, now, false);
				}
				Lodger& l = it->second;
				seen.insert(p->id);
				const bool want = night || (winter && l.farmer);
				const bool due = now >= lastChange + spread * Stagger(p->id);
				switch (l.state) {
				case LODGE_GOING: {
					ServerGameObject* s = server.findObject(l.shelter);
					if (!want && due) {
						wake(server, p, l, now);
						break;
					}
					if (!ShelterUsable(s)) {
						depart(server, p, l, shelters, now, true);
						break;
					}
					const float d = std::sqrt((s->position - p->position).sqlen2xz());
					if (d <= ShelterRadius(s) + 8.0f)
						lodge(p, l, now);
					else if (!p->movement.isMoving() && now - l.since >= 2.0f) {
						// stopped short (blocked, or an order it could not refuse): again, twice, then in anyway
						const uint8_t tries = l.tries + 1;
						if (tries > 2) {
							stats.stuck++;
							lodge(p, l, now);
						}
						else {
							depart(server, p, l, shelters, now, true);
							l.tries = tries; // (depart starts the count again)
						}
					}
					break;
				}
				case LODGE_INSIDE: {
					ServerGameObject* s = server.findObject(l.shelter);
					if (!ShelterUsable(s)) {
						// burnt down or wrecked: out, to the next one (or to work, by day)
						stats.evicted++;
						p->enable();
						if (want) {
							depart(server, p, l, shelters, now, true);
							if (trace)
								printf("LODGING t=%.0f %u driven out of %u\n", now, p->id, l.shelter);
						}
						else
							wake(server, p, l, now);
						break;
					}
					if (!want && due)
						wake(server, p, l, now);
					break;
				}
				case LODGE_WAITING:
					if (!want && due)
						wake(server, p, l, now);
					else if (now - l.since >= 10.0f && std::any_of(shelters.begin(), shelters.end(), ShelterUsable))
						depart(server, p, l, shelters, now, true);
					break;
				case LODGE_RETURNING: {
					const Order* o = p->orderConfig.getCurrentOrder();
					if ((night || (winter && Farming(p))) && !Exempt(p)) {
						// night again before it got back (or winter came for a farmer): in once more
						depart(server, p, l, shelters, now, false);
						break;
					}
					if (now >= l.until || (o ? o->blueprint : nullptr) != l.order) {
						p->lodging = LODGE_NONE;
						p->lodgeSpeed = 1.0f;
						lodgers.erase(it);
					}
					break;
				}
				default:
					break;
				}
				if (auto f = lodgers.find(p->id); f != lodgers.end() && f->second.farmer
					&& (f->second.state == LODGE_GOING || f->second.state == LODGE_INSIDE || f->second.state == LODGE_WAITING)) {
					farming++;
					wintering += winter && f->second.state == LODGE_INSIDE;
				}
			}
			if (farming)
				farmers[player->id] = farming;
			// the winter's crafts: the farmers indoors make a little of everything (wood and gold delivered)
			if (g_l.bonus >= 0) {
				const float bonus = winter ? std::min(BONUS_MOST, BONUS_EACH * wintering) : 0.0f;
				if (player->getItem(g_l.bonus) != bonus)
					player->setItem(g_l.bonus, bonus);
			}
		}
	}
	// the lodgers gone (dead, deleted, converted into something else)
	for (auto it = lodgers.begin(); it != lodgers.end();) {
		if (seen.count(it->first)) {
			++it;
			continue;
		}
		if (ServerGameObject* u = server.findObject(it->first); u && !u->deleted) {
			if (it->second.state == LODGE_INSIDE && u->disableCount > 0)
				u->enable();
			SetSelectable(u, !(u->flags & CommonGameObject::fTerminated) && u->disableCount <= 0);
			u->lodging = LODGE_NONE;
			u->lodgeSpeed = 1.0f;
		}
		it = lodgers.erase(it);
	}
}

std::string PeasantLodging::saveText(Server& server) const
{
	if (!night && !winter && lodgers.empty())
		return std::string();
	std::ostringstream o;
	o.precision(9);
	o << "lodging " << (int)night << ' ' << (int)winter << ' ' << lastChange << '\n';
	for (const auto& [id, l] : lodgers) {
		o << "lodger " << id << ' ' << (int)l.state << ' ' << (int)l.farmer << ' ' << (int)l.tries << ' ' << l.shelter << ' ' << l.target << ' '
			<< l.work.x << ' ' << l.work.y << ' ' << l.work.z << ' ' << l.since << ' ' << l.until << ' ';
		ServerGameObject* u = server.findObject(id);
		o << (u ? u->lodgeSpeed : 1.0f) << ' ' << (l.targetBp ? (int)l.targetBp->bpClass : -1) << ' ';
		// the names last (they have spaces): the order, then the target's blueprint
		o << '"' << (l.order ? server.gameSet->orders.getString(l.order->bpid) : std::string()) << "\" \""
			<< (l.targetBp ? l.targetBp->name : std::string()) << "\"\n";
	}
	return o.str();
}

namespace {
	std::string Quoted(std::istream& in)
	{
		std::string s;
		char c;
		while (in.get(c) && c != '"') {}
		while (in.get(c) && c != '"')
			s += c;
		return s;
	}
	// the speeds as saved, until afterLoad gives them to the units
	std::map<uint32_t, float> g_loadedSpeeds;
	std::map<uint32_t, std::pair<std::string, std::string>> g_loadedNames;
	std::map<uint32_t, int> g_loadedClasses;
}

bool PeasantLodging::loadLine(const std::string& tag, const std::string& line)
{
	std::istringstream l(line);
	std::string t;
	l >> t;
	if (tag == "lodging") {
		int n = 0, w = 0;
		l >> n >> w >> lastChange;
		night = n != 0;
		winter = w != 0;
		return true;
	}
	if (tag == "lodger") {
		uint32_t id = 0;
		int state = 0, farmer = 0, tries = 0;
		Lodger r;
		float speed = 1.0f;
		int cls = -1;
		l >> id >> state >> farmer >> tries >> r.shelter >> r.target >> r.work.x >> r.work.y >> r.work.z >> r.since >> r.until >> speed >> cls;
		r.state = (uint8_t)state;
		r.farmer = farmer != 0;
		r.tries = (uint8_t)tries;
		const std::string order = Quoted(l), bp = Quoted(l);
		lodgers[id] = r;
		g_loadedSpeeds[id] = speed;
		g_loadedNames[id] = { order, bp };
		g_loadedClasses[id] = cls;
		return true;
	}
	return false;
}

void PeasantLodging::afterLoad(Server& server)
{
	for (auto it = lodgers.begin(); it != lodgers.end();) {
		ServerGameObject* u = server.findObject(it->first);
		if (!u || u->deleted) {
			it = lodgers.erase(it);
			continue;
		}
		Lodger& l = it->second;
		if (auto n = g_loadedNames.find(it->first); n != g_loadedNames.end() && server.gameSet) {
			if (int o = server.gameSet->orders.names.getIndex(n->second.first); o >= 0)
				l.order = &server.gameSet->orders[o];
			const int cls = g_loadedClasses.count(it->first) ? g_loadedClasses[it->first] : -1;
			if (!n->second.second.empty() && cls >= 0 && cls < Tags::GAMEOBJCLASS_COUNT)
				l.targetBp = server.gameSet->findBlueprint(cls, n->second.second);
		}
		u->lodging = l.state;
		u->lodgeSpeed = g_loadedSpeeds.count(it->first) ? g_loadedSpeeds[it->first] : 1.0f;
		// (inside: the world's DISABLE_COUNT has hidden it already; on the way: not selectable)
		if (l.state == LODGE_GOING || l.state == LODGE_WAITING)
			SetSelectable(u, false);
		++it;
	}
	g_loadedSpeeds.clear();
	g_loadedNames.clear();
	g_loadedClasses.clear();
}
