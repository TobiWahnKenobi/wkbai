// wkbai - the ministry AI's framework (Ministries.h)

#include "Ministries.h"
#include "MinistryDepartments.h"
#include "server.h"
#include "gameset/gameset.h"
#include "settings.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>

namespace Ministry {

double g_deptMs[DEPT_COUNT] = {};
uint64_t g_deptThinks[DEPT_COUNT] = {};

namespace {
	const char* const NAMES[DEPT_COUNT] = { "strategist", "economy", "labour", "builders", "research", "war", "generals", "intelligence", "diplomacy" };

	// the game set's items the departments read, looked up once per game set
	struct Items {
		const GameSet* of = nullptr;
		int resources[4] = { -1, -1, -1, -1 };
		int peasant = -1, military = -1;
		void update(const GameSet* gs) {
			if (of == gs)
				return;
			of = gs;
			const char* names[4] = { "Food", "Wood", "Gold", "Stone" };
			for (int i = 0; i < 4; i++)
				resources[i] = gs->items.names.getIndex(names[i]);
			peasant = gs->items.names.getIndex("I Am A Peasant");
			military = gs->items.names.getIndex("I'm A Military Unit");
		}
	} g_items;
	const char* const RESOURCE_KEYS[4] = { "food", "wood", "gold", "stone" };

	float Item(const ServerGameObject* o, int item) { return item >= 0 ? o->getItem(item) : 0.0f; }

	std::string Num(double v) { char b[40]; snprintf(b, sizeof(b), "%.9g", v); return b; }

	// ---- Economy: the stock of each resource, and its income per minute ----
	struct Economy : Department {
		uint8_t id() const override { return ECONOMY; }
		float period() const override { return 2.0f; }
		// per resource: the stock at the last think, and the income per minute (an average that
		// follows the last half minute or so)
		float last[4] = { -1, -1, -1, -1 }, income[4] = {};
		float lastTime = -1.0f;
		void think(Government& g) override {
			ServerGameObject* player = g.player();
			if (!player)
				return;
			g_items.update(g.server.gameSet.get());
			const float now = g.now();
			for (int r = 0; r < 4; r++) {
				const float stock = Item(player, g_items.resources[r]);
				if (last[r] >= 0.0f && lastTime >= 0.0f && now > lastTime) {
					const float rate = (stock - last[r]) / (now - lastTime) * 60.0f;
					income[r] = income[r] * 0.85f + rate * 0.15f;
				}
				last[r] = stock;
				g.board.report(std::string("economy.") + RESOURCE_KEYS[r], stock);
				g.board.report(std::string("economy.") + RESOURCE_KEYS[r] + ".income", income[r]);
			}
			lastTime = now;
			EconomyNeeds(g);
			if (thinks % 15 == 0) // (every 30 s)
				g.log(ECONOMY, "stock food %.0f wood %.0f gold %.0f stone %.0f; per minute %+.0f %+.0f %+.0f %+.0f",
					last[0], last[1], last[2], last[3], income[0], income[1], income[2], income[3]);
		}
		void save(const Government&, std::vector<std::string>& lines) const override {
			std::string s = "samples " + Num(lastTime);
			for (int r = 0; r < 4; r++)
				s += " " + Num(last[r]) + " " + Num(income[r]);
			lines.push_back(s);
		}
		void load(Government&, const std::string& line) override {
			float v[9];
			if (sscanf(line.c_str(), "samples %f %f %f %f %f %f %f %f %f", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7], &v[8]) == 9) {
				lastTime = v[0];
				for (int r = 0; r < 4; r++) { last[r] = v[1 + 2 * r]; income[r] = v[2 + 2 * r]; }
			}
		}
	};

	// ---- Labour & Population: the peasants (and those without work), the soldiers, the buildings ----
	struct Labour : Department {
		uint8_t id() const override { return LABOUR; }
		float period() const override { return 2.0f; }
		int lastPeasants = -1, lastIdle = -1, lastSoldiers = -1;
		void think(Government& g) override {
			ServerGameObject* player = g.player();
			if (!player)
				return;
			g_items.update(g.server.gameSet.get());
			int peasants = 0, idle = 0, soldiers = 0, characters = 0, buildings = 0;
			auto walk = [&](CommonGameObject* obj, auto& self) -> void {
				for (const auto& [type, children] : obj->children)
					for (CommonGameObject* child : children) {
						ServerGameObject* o = (ServerGameObject*)child;
						if (o->deleted)
							continue;
						if (o->blueprint->bpClass == Tags::GAMEOBJCLASS_CHARACTER) {
							characters++;
							if (Item(o, g_items.peasant) > 0.0f) {
								peasants++;
								if (o->orderConfig.orders.empty())
									idle++;
							}
							else if (Item(o, g_items.military) > 0.0f)
								soldiers++;
						}
						else if (o->blueprint->bpClass == Tags::GAMEOBJCLASS_BUILDING)
							buildings++;
						self(o, self);
					}
			};
			walk(player, walk);
			g.board.report("labour.peasants", peasants);
			g.board.report("labour.idle", idle);
			g.board.report("labour.soldiers", soldiers);
			g.board.report("labour.characters", characters);
			g.board.report("labour.buildings", buildings);
			if (peasants != lastPeasants || idle != lastIdle || soldiers != lastSoldiers)
				g.log(LABOUR, "%d peasants (%d without work), %d soldiers, %d characters, %d buildings", peasants, idle, soldiers, characters, buildings);
			lastPeasants = peasants;
			lastIdle = idle;
			lastSoldiers = soldiers;
			// the cap, the idle, the gold miners Economy asks for (MinistryEconomy.cpp)
			LabourActs(g, goldMiners);
		}
		std::map<uint32_t, float> goldMiners; // peasant id -> since when
		void save(const Government&, std::vector<std::string>& lines) const override {
			lines.push_back("last " + std::to_string(lastPeasants) + " " + std::to_string(lastIdle) + " " + std::to_string(lastSoldiers));
			for (const auto& [id, since] : goldMiners)
				lines.push_back("gold " + std::to_string(id) + " " + Num(since));
		}
		void load(Government& g, const std::string& line) override {
			unsigned id = 0;
			float since = 0.0f;
			if (sscanf(line.c_str(), "gold %u %f", &id, &since) == 2) {
				goldMiners[id] = since;
				if (ServerGameObject* p = g.player())
					p->aiController.ministryHeld.insert(id);
			}
			else
				sscanf(line.c_str(), "last %d %d %d", &lastPeasants, &lastIdle, &lastSoldiers);
		}
	};

	void MakeDepartments(Government& g)
	{
		// (in Dept order)
		g.departments.push_back(MakeStrategist());
		g.departments.push_back(MakeEconomy());
		g.departments.push_back(MakeLabour());
		g.departments.push_back(MakeBuilders());
		g.departments.push_back(MakeResearch());
		g.departments.push_back(MakeWar());
		g.departments.push_back(MakeGenerals());
		g.departments.push_back(MakeIntelligence());
		g.departments.push_back(MakeDiplomacy());
	}

	// "aiTrace" / WKB_TRACE_AI: 0 off, 1 every AI, else a player's id
	uint32_t TraceSetting()
	{
		if (const char* env = getenv("WKB_TRACE_AI"))
			return (uint32_t)strtoul(env, nullptr, 10);
		return g_settings.value("aiTrace", 0u);
	}
}

std::unique_ptr<Department> MakeEconomy() { return std::make_unique<Economy>(); }
std::unique_ptr<Department> MakeLabour() { return std::make_unique<Labour>(); }

const char* DeptName(int dept) { return dept >= 0 && dept < DEPT_COUNT ? NAMES[dept] : "?"; }

int DeptFromName(const std::string& name)
{
	for (int i = 0; i < DEPT_COUNT; i++)
		if (name == NAMES[i])
			return i;
	return -1;
}

// ---- Blackboard ----

double Blackboard::get(const std::string& key, double otherwise) const
{
	auto it = reports.find(key);
	return it == reports.end() ? otherwise : it->second;
}

uint32_t Blackboard::ask(uint8_t from, const std::string& what, float amount, float value, float urgency, bool emergency, float now)
{
	for (Request& r : requests)
		if (r.from == from && r.what == what) {
			r.amount = amount; r.value = value; r.urgency = urgency; r.emergency = emergency;
			return r.id;
		}
	Request r;
	r.id = nextRequestId++;
	r.from = from; r.what = what; r.amount = amount; r.value = value; r.urgency = urgency; r.emergency = emergency; r.since = now;
	requests.push_back(r);
	return r.id;
}

void Blackboard::withdraw(uint8_t from, const std::string& what)
{
	requests.erase(std::remove_if(requests.begin(), requests.end(), [&](const Request& r) { return r.from == from && r.what == what; }), requests.end());
}

const Request* Blackboard::find(uint8_t from, const std::string& what) const
{
	for (const Request& r : requests)
		if (r.from == from && r.what == what)
			return &r;
	return nullptr;
}

std::vector<Grant> Blackboard::allocate(const std::string& what, float available) const
{
	std::vector<const Request*> asking;
	for (const Request& r : requests)
		if (r.what == what && r.amount > 0.0f)
			asking.push_back(&r);
	std::sort(asking.begin(), asking.end(), [this](const Request* a, const Request* b) {
		if (a->emergency != b->emergency)
			return a->emergency;
		const float pa = priority(*a), pb = priority(*b);
		if (pa != pb)
			return pa > pb;
		if (a->from != b->from)
			return a->from < b->from;
		return a->id < b->id;
	});
	std::vector<Grant> grants;
	for (const Request* r : asking) {
		if (available <= 0.0f)
			break;
		const float amount = std::min(r->amount, available);
		grants.push_back({ r->id, r->from, amount });
		available -= amount;
	}
	return grants;
}

bool Blackboard::assign(uint32_t worker, uint8_t dept, float now, float minHold, bool emergency)
{
	auto it = assignments.find(worker);
	if (it != assignments.end()) {
		if (it->second.dept == dept)
			return true;
		if (!emergency && now - it->second.since < minHold)
			return false;
	}
	assignments[worker] = { dept, now };
	return true;
}

int Blackboard::assignedTo(uint32_t worker) const
{
	auto it = assignments.find(worker);
	return it == assignments.end() ? -1 : it->second.dept;
}

// ---- Government ----

Government::Government(Server& server, uint32_t playerId) : server(server), playerId(playerId)
{
	MakeDepartments(*this);
	const uint32_t t = TraceSetting();
	trace = t == 1 || (t != 0 && t == playerId);
}

ServerGameObject* Government::player() const
{
	ServerGameObject* p = server.findObject(playerId);
	return p && !p->deleted ? p : nullptr;
}

Department* Government::department(uint8_t id) const
{
	for (const auto& d : departments)
		if (d->id() == id)
			return d.get();
	return nullptr;
}

float Government::now() const { return server.timeManager.currentTime; }

void Government::log(uint8_t dept, const char* fmt, ...)
{
	if (!trace)
		return;
	char text[600];
	va_list args;
	va_start(args, fmt);
	vsnprintf(text, sizeof(text), fmt, args);
	va_end(args);
	printf("AITRACE player %u t=%.1f %s: %s\n", playerId, now(), DeptName(dept), text);
}

void Government::step()
{
	const float t = now();
	const float stepSec = server.timeManager.stepMs > 0 ? server.timeManager.stepMs / 1000.0f : 0.02f;
	// the difficulty's reaction time ("aiReaction", percent)
	const float scale = std::clamp(server.aiReaction, 25, 400) / 100.0f;
	for (const auto& d : departments) {
		const float period = d->period() * scale;
		if (d->nextAt < 0.0f) {
			// the first think: staggered by player and department over one period, in whole steps,
			// so that the governments' departments don't all think at the same step
			const int steps = std::max(1, (int)std::lround(period / stepSec));
			d->nextAt = t + (float)((playerId * 7u + d->id() * 3u) % (uint32_t)steps) * stepSec;
		}
		if (t + 1e-4f < d->nextAt)
			continue;
		const auto start = std::chrono::steady_clock::now();
		d->think(*this);
		g_deptMs[d->id()] += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
		g_deptThinks[d->id()]++;
		d->thinks++;
		d->nextAt += period;
		if (d->nextAt <= t) // (behind: a jump in time)
			d->nextAt = t + period;
	}
}

// ---- Ministries ----

void Ministries::update(Server& server)
{
	ServerGameObject* level = (ServerGameObject*)server.getLevel();
	if (!level)
		return;
	std::vector<ServerGameObject*> players;
	for (const auto& [type, children] : level->children)
		for (CommonGameObject* child : children) {
			ServerGameObject* p = (ServerGameObject*)child;
			if (p->blueprint->bpClass == Tags::GAMEOBJCLASS_PLAYER && !p->deleted && !(p->flags & ServerGameObject::fTerminated)
				&& p->aiController.isAI())
				players.push_back(p);
		}
	std::sort(players.begin(), players.end(), [](auto* a, auto* b) { return a->id < b->id; });
	for (ServerGameObject* p : players) {
		auto& g = governments[p->id];
		if (!g) {
			g = std::make_unique<Government>(server, p->id);
			g->log(STRATEGIST, "a government of %zu departments", g->departments.size());
			SetWeightsFromCharacter(*g);
		}
		g->step();
	}
}

std::string Ministries::saveText() const
{
	std::string out;
	for (const auto& [id, g] : governments) {
		const Blackboard& b = g->board;
		out += "GOVERNMENT " + std::to_string(id) + "\n";
		std::string w = "WEIGHTS";
		for (float x : b.weights)
			w += " " + Num(x);
		out += w + "\n";
		out += "NEXT_REQUEST " + std::to_string(b.nextRequestId) + "\n";
		for (const auto& [key, v] : b.reports) {
			char buf[64];
			snprintf(buf, sizeof(buf), " %.17g\n", v);
			out += "REPORT " + key + buf;
		}
		for (const auto& [key, v] : b.budgets) {
			char buf[64];
			snprintf(buf, sizeof(buf), " %.17g\n", v);
			out += "BUDGET " + key + buf;
		}
		for (const Request& r : b.requests)
			out += "REQUEST " + std::to_string(r.id) + " " + DeptName(r.from) + " " + r.what + " " + Num(r.amount) + " " + Num(r.value) + " "
				+ Num(r.urgency) + " " + (r.emergency ? "1" : "0") + " " + Num(r.since) + "\n";
		for (const auto& [worker, a] : b.assignments)
			out += "ASSIGN " + std::to_string(worker) + " " + DeptName(a.dept) + " " + Num(a.since) + "\n";
		// the missions: "MISSION <id> <kind> <state> <enemy> <target> <x> <z> <want> <goal> <progress>
		// <max loss> <duration> <since> <state since> <start units> <kills then> <reinforced> <n> <unit ids...>"
		out += "NEXT_MISSION " + std::to_string(g->nextMission) + "\n";
		for (const Mission& m : g->missions) {
			out += "MISSION " + std::to_string(m.id) + " " + Mission::KindName(m.kind) + " " + std::to_string(m.state) + " " + std::to_string(m.enemy) + " "
				+ std::to_string(m.target) + " " + Num(m.tx) + " " + Num(m.tz) + " " + std::to_string(m.want) + " " + std::to_string(m.goal) + " "
				+ std::to_string(m.progress) + " " + Num(m.maxLoss) + " " + Num(m.duration) + " " + Num(m.since) + " " + Num(m.stateSince) + " "
				+ std::to_string(m.startUnits) + " " + std::to_string(m.kills0) + " " + (m.reinforced ? "1" : "0") + " " + std::to_string(m.units.size());
			for (uint32_t u : m.units)
				out += " " + std::to_string(u);
			out += "\n";
		}
		for (const auto& d : g->departments) {
			out += "DEPT " + std::string(DeptName(d->id())) + " " + Num(d->nextAt) + " " + std::to_string(d->thinks) + "\n";
			std::vector<std::string> lines;
			d->save(*g, lines);
			for (const std::string& l : lines)
				out += "STATE " + std::string(DeptName(d->id())) + " " + l + "\n";
		}
		out += "END_GOVERNMENT\n";
	}
	return out;
}

void Ministries::loadText(Server& server, const std::string& text)
{
	governments.clear();
	Government* g = nullptr;
	size_t at = 0;
	while (at < text.size()) {
		size_t end = text.find('\n', at);
		if (end == std::string::npos)
			end = text.size();
		const std::string line = text.substr(at, end - at);
		at = end + 1;
		const size_t sp = line.find(' ');
		const std::string tag = line.substr(0, sp);
		const std::string rest = sp == std::string::npos ? std::string() : line.substr(sp + 1);
		auto word = [](const std::string& s, size_t& p) {
			while (p < s.size() && s[p] == ' ') p++;
			const size_t q = s.find(' ', p);
			std::string w = s.substr(p, q == std::string::npos ? std::string::npos : q - p);
			p = q == std::string::npos ? s.size() : q;
			return w;
		};
		if (tag == "GOVERNMENT") {
			const uint32_t id = (uint32_t)strtoul(rest.c_str(), nullptr, 10);
			auto& slot = governments[id];
			slot = std::make_unique<Government>(server, id);
			g = slot.get();
			continue;
		}
		if (!g)
			continue;
		Blackboard& b = g->board;
		size_t p = 0;
		if (tag == "END_GOVERNMENT")
			g = nullptr;
		else if (tag == "WEIGHTS") {
			for (int i = 0; i < DEPT_COUNT; i++) {
				const std::string w = word(rest, p);
				if (!w.empty())
					b.weights[i] = (float)atof(w.c_str());
			}
		}
		else if (tag == "NEXT_REQUEST")
			b.nextRequestId = (uint32_t)strtoul(rest.c_str(), nullptr, 10);
		else if (tag == "REPORT" || tag == "BUDGET") {
			const std::string key = word(rest, p);
			const double v = strtod(word(rest, p).c_str(), nullptr);
			(tag == "REPORT" ? b.reports : b.budgets)[key] = v;
		}
		else if (tag == "REQUEST") {
			Request r;
			r.id = (uint32_t)strtoul(word(rest, p).c_str(), nullptr, 10);
			const int from = DeptFromName(word(rest, p));
			r.what = word(rest, p);
			r.amount = (float)atof(word(rest, p).c_str());
			r.value = (float)atof(word(rest, p).c_str());
			r.urgency = (float)atof(word(rest, p).c_str());
			r.emergency = word(rest, p) == "1";
			r.since = (float)atof(word(rest, p).c_str());
			if (from >= 0 && !r.what.empty()) {
				r.from = (uint8_t)from;
				b.requests.push_back(r);
			}
		}
		else if (tag == "ASSIGN") {
			const uint32_t worker = (uint32_t)strtoul(word(rest, p).c_str(), nullptr, 10);
			const int dept = DeptFromName(word(rest, p));
			const float since = (float)atof(word(rest, p).c_str());
			if (dept >= 0)
				b.assignments[worker] = { (uint8_t)dept, since };
		}
		else if (tag == "DEPT") {
			const int dept = DeptFromName(word(rest, p));
			if (Department* d = dept >= 0 ? g->department((uint8_t)dept) : nullptr) {
				d->nextAt = (float)atof(word(rest, p).c_str());
				d->thinks = strtoull(word(rest, p).c_str(), nullptr, 10);
			}
		}
		else if (tag == "STATE") {
			const int dept = DeptFromName(word(rest, p));
			if (Department* d = dept >= 0 ? g->department((uint8_t)dept) : nullptr)
				d->load(*g, rest.substr(std::min(p + 1, rest.size())));
		}
		else if (tag == "NEXT_MISSION")
			g->nextMission = (uint32_t)strtoul(rest.c_str(), nullptr, 10);
		else if (tag == "MISSION") {
			Mission m;
			auto u32 = [&]() { return (uint32_t)strtoul(word(rest, p).c_str(), nullptr, 10); };
			auto f32 = [&]() { return (float)atof(word(rest, p).c_str()); };
			m.id = u32();
			const std::string kind = word(rest, p);
			for (int k = 0; k < Mission::KIND_COUNT; k++)
				if (kind == Mission::KindName(k))
					m.kind = (uint8_t)k;
			m.state = (uint8_t)u32();
			m.enemy = u32();
			m.target = u32();
			m.tx = f32(); m.tz = f32();
			m.want = (int)u32(); m.goal = (int)u32(); m.progress = (int)u32();
			m.maxLoss = f32(); m.duration = f32(); m.since = f32(); m.stateSince = f32();
			m.startUnits = (int)u32(); m.kills0 = (int)u32();
			m.reinforced = word(rest, p) == "1";
			const uint32_t n = u32();
			for (uint32_t i = 0; i < n && i < 10000; i++)
				m.units.push_back(u32());
			g->missions.push_back(std::move(m));
		}
	}
	// the war layer leaves the missions' units alone from the first step
	for (auto& [id, gov] : governments)
		if (ServerGameObject* p = gov->player()) {
			p->aiController.ministryWaves = true;
			p->aiController.ministryUnits.clear();
			for (const Mission& m : gov->missions)
				p->aiController.ministryUnits.insert(m.units.begin(), m.units.end());
		}
}

std::string Ministries::profileText(uint64_t steps)
{
	std::string s;
	double total = 0.0;
	for (int i = 0; i < DEPT_COUNT; i++)
		total += g_deptMs[i];
	char buf[80];
	snprintf(buf, sizeof(buf), "%.3f ms per step", steps ? total / steps : 0.0);
	s = buf;
	for (int i = 0; i < DEPT_COUNT; i++)
		if (g_deptThinks[i]) {
			snprintf(buf, sizeof(buf), ", %s %.3f (%llu thinks, %.3f ms each)", NAMES[i], steps ? g_deptMs[i] / steps : 0.0,
				(unsigned long long)g_deptThinks[i], g_deptMs[i] / g_deptThinks[i]);
			s += buf;
		}
	for (int i = 0; i < DEPT_COUNT; i++) { g_deptMs[i] = 0.0; g_deptThinks[i] = 0; }
	return s;
}

}
