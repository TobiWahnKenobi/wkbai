// wkbai - the timeline told as a chronicle (ChronicleNarrator.h)

#include "ChronicleNarrator.h"
#include "../Environment.h"
#include <algorithm>
#include <cmath>
#include <map>

namespace {
using Piece = ChronicleNarrator::Piece;
using Pieces = std::vector<Piece>;
using Vars = std::map<std::string, Pieces>;

// the wording: one of n, by the event and a slot (the same everywhere for the same event)
int Pick(const GameChronicle::Event& e, uint32_t slot, int n)
{
	uint32_t h = e.id * 2654435761u ^ (uint32_t)(e.time * 10.0f) * 40503u ^ slot * 2246822519u;
	h ^= h >> 15; h *= 2654435761u; h ^= h >> 13;
	return n > 0 ? (int)(h % (uint32_t)n) : 0;
}

std::string Number(int n)
{
	static const char* words[] = { "none", "one", "two", "three", "four", "five", "six", "seven", "eight", "nine", "ten", "eleven", "twelve" };
	return n >= 0 && n <= 12 ? words[n] : std::to_string(n);
}

std::string Ordinal(int n)
{
	static const char* words[] = { "", "first", "second", "third", "fourth", "fifth", "sixth", "seventh", "eighth", "ninth", "tenth", "eleventh", "twelfth" };
	if (n >= 1 && n <= 12)
		return words[n];
	const int t = n % 100;
	const char* suffix = (t >= 11 && t <= 13) ? "th" : n % 10 == 1 ? "st" : n % 10 == 2 ? "nd" : n % 10 == 3 ? "rd" : "th";
	return std::to_string(n) + suffix;
}

// "Spearman" -> "Spearmen", "Archer" -> "Archers", "Militia" stays
std::string Plural(const std::string& type)
{
	if (type.size() >= 3 && type.compare(type.size() - 3, 3, "man") == 0)
		return type.substr(0, type.size() - 3) + "men";
	if (!type.empty() && (type.back() == 'a' || type.back() == 's'))
		return type;
	if (type.size() >= 2 && type.back() == 'y' && std::string("aeiou").find(type[type.size() - 2]) == std::string::npos)
		return type.substr(0, type.size() - 1) + "ies";
	return type + "s";
}

std::string Article(const std::string& word)
{
	return !word.empty() && std::string("AEIOUaeiou").find(word[0]) != std::string::npos ? "an " : "a ";
}

std::string Lower(std::string s)
{
	for (char& ch : s)
		ch = (char)std::tolower((unsigned char)ch);
	return s;
}

// a template: "{D}, the hosts of {L} met at {P}." with the pieces put in
void Emit(Pieces& out, const std::string& tpl, const Vars& vars)
{
	if (!out.empty())
		out.push_back({ " " });
	const size_t first = out.size();
	size_t i = 0;
	while (i < tpl.size()) {
		const size_t open = tpl.find('{', i);
		if (open == std::string::npos) {
			out.push_back({ tpl.substr(i) });
			break;
		}
		if (open > i)
			out.push_back({ tpl.substr(i, open - i) });
		const size_t close = tpl.find('}', open);
		const std::string key = tpl.substr(open + 1, close - open - 1);
		if (auto it = vars.find(key); it != vars.end())
			out.insert(out.end(), it->second.begin(), it->second.end());
		i = close + 1;
	}
	// a sentence begins with a capital
	for (size_t j = first; j < out.size(); j++)
		if (!out[j].text.empty()) {
			if (out[j].player == 0 && out[j].mention < 0)
				out[j].text[0] = (char)std::toupper((unsigned char)out[j].text[0]);
			break;
		}
}

Pieces Text(const std::string& s) { return { Piece{ s } }; }

// "A", "A and B", "A, B and C"
Pieces List(const std::vector<Pieces>& items)
{
	Pieces out;
	for (size_t i = 0; i < items.size(); i++) {
		if (i > 0)
			out.push_back({ i + 1 == items.size() ? " and " : ", " });
		out.insert(out.end(), items[i].begin(), items[i].end());
	}
	return out;
}

// how one died, after a comma: "slain by Aelfred", "taken by the cold"; "" for no cause
std::string Cause(const std::string& extra)
{
	static const std::map<std::string, std::string> causes = {
		{ "froze to death", "taken by the cold" }, { "died of thirst", "overcome by heat and thirst" },
		{ "drowned", "lost to the waters" }, { "went through the ice", "swallowed by the ice" },
		{ "burned to death", "consumed by the flames" }, { "struck by lightning", "struck down by a bolt from heaven" },
		{ "fell", "" } };
	if (auto it = causes.find(extra); it != causes.end())
		return it->second;
	return extra; // "slain by ...", and any other
}

std::string KillsPhrase(int kills)
{
	return kills == 1 ? "one kill" : Number(kills) + " kills";
}
}

void ChronicleNarrator::Context::fromSettings(const EnvironmentSettings& env, bool active)
{
	seasons = active && env.seasons && env.seasonLengthMin > 0;
	dayNight = active && env.dayNight && env.dayLengthMin > 0;
	seasonLengthS = seasons ? env.seasonLengthS() : 1800.0f;
	dayLengthS = env.dayLengthS() > 0.0f ? env.dayLengthS() : 1200.0f;
	yearShift = env.yearShift;
	startSeason = env.startSeason;
	startHour = env.startHour;
}

namespace {
const char* StorySeason(int s)
{
	static const char* names[] = { "spring", "summer", "autumn", "winter" };
	return names[((s % 4) + 4) % 4];
}

const char* TimeOfDay(float hour)
{
	if (hour >= 5.0f && hour < 7.0f) return "at dawn";
	if (hour >= 7.0f && hour < 11.0f) return "in the morning";
	if (hour >= 11.0f && hour < 14.0f) return "about noon";
	if (hour >= 14.0f && hour < 18.0f) return "in the afternoon";
	if (hour >= 18.0f && hour < 21.0f) return "at dusk";
	return "by night";
}

struct Calendar {
	int season = 0, year = 1000, day = 1;
	float hour = 12.0f;
};

Calendar CalendarAt(float t, const ChronicleNarrator::Context& c)
{
	Calendar cal;
	const float elapsed = t / std::max(c.seasonLengthS, 1.0f) + c.yearShift * 4.0f + (float)c.startSeason;
	cal.season = (int)std::floor(elapsed) % 4;
	cal.year = 1000 + (int)std::floor(elapsed / 4.0f);
	const float days = (float)c.startHour / 24.0f + t / std::max(c.dayLengthS, 1.0f);
	cal.day = 1 + (int)std::floor(days);
	cal.hour = (days - std::floor(days)) * 24.0f;
	return cal;
}

// the sentence's opening: "In the spring of the year 1000, at dusk"
std::string Opener(float t, const ChronicleNarrator::Context& c)
{
	const Calendar cal = CalendarAt(t, c);
	if (c.seasons) {
		std::string s = std::string("In the ") + StorySeason(cal.season) + " of the year " + std::to_string(cal.year);
		if (c.dayNight)
			s += std::string(", ") + TimeOfDay(cal.hour);
		return s;
	}
	if (c.dayNight)
		return "On the " + Ordinal(cal.day) + " day, " + TimeOfDay(cal.hour);
	if (t < 120.0f) return "At the very outset of the war";
	if (t < 600.0f) return "Early in the war";
	if (t < 1800.0f) return "As the war went on";
	return "Late in the long war";
}
}

std::string ChronicleNarrator::dateLine(float time, const Context& c)
{
	const Calendar cal = CalendarAt(time, c);
	char clock[16];
	snprintf(clock, sizeof(clock), "%d:%02d", (int)time / 60, (int)time % 60);
	std::string s;
	if (c.seasons) {
		s = StorySeason(cal.season);
		s[0] = (char)std::toupper((unsigned char)s[0]);
		s += ", the year " + std::to_string(cal.year);
		if (c.dayNight)
			s += std::string(", ") + TimeOfDay(cal.hour);
	}
	else if (c.dayNight)
		s = "Day " + std::to_string(cal.day) + ", " + TimeOfDay(cal.hour);
	return s.empty() ? std::string(clock) : s + "  (" + clock + ")";
}

std::string ChronicleNarrator::plain(const std::vector<Piece>& pieces)
{
	std::string s;
	for (const Piece& p : pieces)
		s += p.text;
	return s;
}

std::vector<ChronicleNarrator::Piece> ChronicleNarrator::narrate(const GameChronicle::Event& e, const Context& c)
{
	using GC = GameChronicle;
	Pieces out;
	auto player = [&](uint32_t id) { return Pieces{ Piece{ c.playerName ? c.playerName(id) : std::to_string(id), id } }; };
	// (one piece: "'s" never wraps away from the name)
	auto possessive = [&](uint32_t id) { Pieces p = player(id); p[0].text += "'s"; return p; };
	auto unit = [&](size_t i) {
		const GC::Mention& m = e.mentions[i];
		Piece p{ m.name.empty() ? std::string("one unnamed") : m.name };
		p.mention = (int)i;
		return Pieces{ p };
	};
	const std::string place = e.place.empty() ? std::string("a nameless place in the wilds") : e.place;
	Vars v;
	v["D"] = Text(Opener(e.time, c));
	v["P"] = Text(place);
	std::vector<uint32_t> sides(e.players.begin(), e.players.end());

	// the units named, by kind
	std::vector<size_t> top, present, fallen, razers;
	for (size_t i = 0; i < e.mentions.size(); i++)
		switch (e.mentions[i].kind) {
		case GC::MENTION_TOP_KILLER: top.push_back(i); break;
		case GC::MENTION_PRESENT: present.push_back(i); break;
		case GC::MENTION_FALLEN: fallen.push_back(i); break;
		case GC::MENTION_RAZED_BY: razers.push_back(i); break;
		}
	auto fellHere = [&](uint32_t id) {
		return std::any_of(fallen.begin(), fallen.end(), [&](size_t i) { return e.mentions[i].unit == id; });
	};

	// who fought there and their deeds, the dead (a fight, and the fight around a settlement)
	auto deeds = [&](uint32_t slot) {
		for (size_t n = 0; n < top.size(); n++) {
			const GC::Mention& m = e.mentions[top[n]];
			Vars w = v;
			w["N"] = unit(top[n]);
			if (m.dead && !fellHere(m.unit))
				w["N"].insert(w["N"].begin(), Piece{ "the late " });
			w["A"] = possessive(m.player);
			w["K"] = Text(Number(m.kills));
			static const char* forms[] = {
				"Of {A} warriors, none fought harder than {N}, who slew {K} that day.",
				"{N} of {A} host slew {K} there, more than any other of them.",
				"Foremost among {A} fighters was {N}, with {K} slain.",
			};
			Emit(out, m.kills == 1 && Pick(e, slot + (uint32_t)n, 3) == 0 ? "Of {A} warriors, {N} alone struck down a foe."
				: forms[Pick(e, slot + (uint32_t)n, 3)], w);
		}
		// the heroes there who weren't named for their kills, nor among the dead
		std::vector<Pieces> heroes;
		std::vector<size_t> heroIndex;
		for (size_t i : present) {
			const GC::Mention& m = e.mentions[i];
			if (fellHere(m.unit) || std::any_of(top.begin(), top.end(), [&](size_t t) { return e.mentions[t].unit == m.unit; }))
				continue;
			Pieces h = unit(i);
			if (m.dead)
				h.insert(h.begin(), Piece{ "the late " });
			heroes.push_back(std::move(h));
			heroIndex.push_back(i);
		}
		if (heroes.size() == 1) {
			const GC::Mention& m = e.mentions[heroIndex[0]];
			Vars w = v;
			w["H"] = unit(heroIndex[0]);
			w["K"] = Text(m.kills > 0 ? ", slayer of " + Number(m.kills) + "," : std::string());
			static const char* alive[] = { "The renowned {H}{K} fought there as well.", "{H}, a hero already sung of, was among them.", "Renown was there too: {H}." };
			Emit(out, m.dead ? "{H}, who would not live to see the war's end, fought there as well." : alive[Pick(e, slot + 10, 3)], w);
		}
		else if (heroes.size() > 1) {
			Vars w = v;
			w["H"] = List(heroes);
			static const char* forms[] = { "Heroes of renown stood in the lines: {H}.", "Many of renown fought there: {H}." };
			Emit(out, forms[Pick(e, slot + 10, 2)], w);
		}
		for (size_t n = 0; n < fallen.size(); n++) {
			const GC::Mention& m = e.mentions[fallen[n]];
			Vars w = v;
			w["N"] = unit(fallen[n]);
			w["A"] = player(m.player);
			const std::string cause = Cause(m.extra);
			w["C"] = Text(cause.empty() ? std::string() : ", " + cause);
			w["K"] = Text(KillsPhrase(m.kills));
			static const char* forms[] = {
				"There fell {N}, a hero of {K}{C}.",
				"{A} mourned {N} there, a hero of {K}{C}.",
				"Among the dead was {N}, who had made {K}{C}.",
			};
			Emit(out, forms[Pick(e, slot + 20 + (uint32_t)n, 3)], w);
		}
	};

	// the losses, and who held the field
	auto outcome = [&](uint32_t slot) {
		std::map<uint32_t, int> lost;
		std::map<uint32_t, std::pair<std::string, int>> mostType;
		int total = 0;
		for (uint32_t p : sides) {
			int n = 0;
			if (auto it = e.lost.find(p); it != e.lost.end())
				for (const auto& [type, k] : it->second) {
					n += k;
					if (k > mostType[p].second)
						mostType[p] = { type, k };
				}
			lost[p] = n;
			total += n;
		}
		if (sides.empty())
			return;
		if (total == 0) {
			static const char* forms[] = { "No life of note was lost.", "When it was over, no one was counted among the dead." };
			Emit(out, forms[Pick(e, slot, 2)], v);
			return;
		}
		Vars w = v;
		if (sides.size() == 1) {
			w["A"] = player(sides[0]);
			w["L"] = Text(Number(lost[sides[0]]));
			Emit(out, "{A} lost {L} there.", w);
			return;
		}
		auto byLoss = sides;
		std::sort(byLoss.begin(), byLoss.end(), [&](uint32_t a, uint32_t b) { return lost[a] < lost[b] || (lost[a] == lost[b] && a < b); });
		const uint32_t winner = byLoss.front(), loser = byLoss.back();
		const int lo = lost[winner], hi = lost[loser];
		w["W"] = player(winner);
		w["B"] = player(loser);
		w["H"] = Text(Number(hi));
		w["O"] = Text(Number(lo));
		// most of the dead of one type: "most of them Spearmen"
		const auto& [type, k] = mostType[loser];
		w["T"] = Text(!type.empty() && k * 10 >= hi * 7 && hi >= 3 ? (k == hi ? ", all of them " : ", most of them ") + Plural(type) : std::string());
		if (hi >= 2 * lo + 2) {
			static const char* forms[] = {
				"{W} held the field; {B} left {H} dead behind{T}.",
				"The day belonged to {W}: {B} lost {H}{T}, {W} only {O}.",
				"It ended badly for {B}, who lost {H}{T} against {O} of {W}.",
			};
			Emit(out, lo == 0 ? "{W} held the field without loss; {B} left {H} dead behind{T}." : forms[Pick(e, slot, 3)], w);
		}
		else {
			std::vector<Pieces> each;
			for (uint32_t p : sides) {
				Pieces x = player(p);
				x.push_back({ " " + Number(lost[p]) });
				each.push_back(x);
			}
			w["E"] = List(each);
			static const char* forms[] = { "Neither side could claim the day; the dead were counted: {E}.", "It was a bitter, even fight. The losses: {E}." };
			Emit(out, forms[Pick(e, slot, 2)], w);
		}
	};

	std::vector<Pieces> names;
	for (uint32_t p : sides)
		names.push_back(player(p));
	switch (e.kind) {
	case GC::Kind::Battle:
	case GC::Kind::Siege:
	case GC::Kind::Raid: {
		v["L"] = List(names);
		const int n = e.size;
		v["S"] = Text(n < 8 ? "a skirmish" : n < 25 ? "battle" : n < 60 ? "a great battle" : "one of the great slaughters of the age");
		if (e.kind == GC::Kind::Battle) {
			static const char* forms[] = {
				"{D}, the hosts of {L} met in {S} at {P}.",
				"{D}, {L} came to blows at {P}.",
				"{D}, steel rang out at {P}, where the warriors of {L} clashed.",
			};
			Emit(out, sides.size() == 1 ? "{D}, {L} fought at {P}." : forms[Pick(e, 1, 3)], v);
		}
		else if (e.kind == GC::Kind::Siege) {
			static const char* forms[] = {
				"{D}, the walls of {P} were assailed, and {L} fought beneath them.",
				"{D}, {P} came under siege, as {L} contested its walls.",
			};
			Emit(out, forms[Pick(e, 1, 2)], v);
		}
		else {
			// the raided: the side that lost the most there
			uint32_t victim = 0;
			int most = -1;
			for (uint32_t p : sides) {
				int k = 0;
				if (auto it = e.lost.find(p); it != e.lost.end())
					for (const auto& [type, x] : it->second)
						k += x;
				if (k > most) { most = k; victim = p; }
			}
			Vars w = v;
			w["V"] = victim ? player(victim) : Text("its people");
			static const char* forms[] = {
				"{D}, raiders struck at {P}, falling upon the fields and folk of {V}.",
				"{D}, a raid fell upon {P}, and the people of {V} paid for it.",
			};
			Emit(out, most > 0 && sides.size() > 1 ? forms[Pick(e, 1, 2)] : "{D}, raiders struck at {P}.", w);
		}
		// its size and the sides' strength
		if (n > 0) {
			Vars w = v;
			w["N"] = Text(Number(n));
			const char* form = n < 8 ? "It was no more than a skirmish, of some {N} fighters."
				: n < 25 ? (Pick(e, 2, 2) ? "Some {N} fighters were drawn into it." : "Some {N} fighters took the field.")
				: n < 60 ? "It was a great battle: {N} fighters at its height."
				: "Few had ever seen its like: {N} fighters locked in combat at its height.";
			Emit(out, form, w);
			if (sides.size() == 2) {
				auto a = e.strength.find(sides[0]), b = e.strength.find(sides[1]);
				if (a != e.strength.end() && b != e.strength.end() && a->second > 0 && b->second > 0 && (a->second * 2 <= b->second || b->second * 2 <= a->second)) {
					const bool aMore = a->second > b->second;
					Vars x = v;
					x["M"] = player(aMore ? sides[0] : sides[1]);
					x["F"] = player(aMore ? sides[1] : sides[0]);
					x["X"] = Text(Number(std::max(a->second, b->second)));
					x["Y"] = Text(Number(std::min(a->second, b->second)));
					Emit(out, "{M} came in strength, {X} against the {Y} of {F}.", x);
				}
			}
		}
		const float d = e.endTime - e.time;
		if (d < 20.0f)
			Emit(out, "It was over almost as soon as it had begun.", v);
		else if (d > 240.0f)
			Emit(out, Pick(e, 3, 2) ? "The fighting dragged on for a long time." : "It raged for a long while before it was decided.", v);
		outcome(4);
		deeds(100);
		break;
	}
	case GC::Kind::Founded: {
		Vars w = v;
		w["A"] = sides.empty() ? Text("settlers") : player(sides[0]);
		w["S"] = Text(e.what.empty() ? std::string("settlement") : Lower(e.what));
		w["a"] = Text(Article(e.what));
		static const char* forms[] = {
			"{D}, {A} raised {a}{S} at {P}.",
			"{D}, settlers of {A} broke ground for {a}{S} at {P}.",
			"{D}, {a}{S} of {A} rose at {P}.",
		};
		Emit(out, forms[Pick(e, 1, 3)], w);
		break;
	}
	case GC::Kind::Destroyed: {
		// its owner: a side none of the razers belong to
		std::vector<uint32_t> razerSides;
		for (size_t i : razers)
			razerSides.push_back(e.mentions[i].player);
		uint32_t owner = 0;
		for (uint32_t p : sides)
			if (std::find(razerSides.begin(), razerSides.end(), p) == razerSides.end()) {
				owner = p;
				break;
			}
		Vars w = v;
		w["S"] = Text(e.what.empty() ? std::string("settlement") : Lower(e.what));
		w["O"] = owner ? possessive(owner) : Text("its people's");
		std::vector<Pieces> by;
		for (size_t i : razers) {
			Pieces x = possessive(e.mentions[i].player);
			x.push_back({ " " });
			Pieces u = unit(i);
			x.insert(x.end(), u.begin(), u.end());
			by.push_back(x);
		}
		w["R"] = by.empty() ? Text(std::string()) : Pieces{};
		if (!by.empty()) {
			Pieces r{ { by.size() == 1 ? ", by the hand of " : ", by the hands of " } };
			Pieces l = List(by);
			r.insert(r.end(), l.begin(), l.end());
			w["R"] = r;
		}
		static const char* forms[] = {
			"{D}, {O} {S} at {P} was put to the torch{R}.",
			"{D}, {O} {S} at {P} was razed to the ground{R}.",
			"{D}, {O} {S} at {P} fell to its enemies{R}.",
		};
		Emit(out, forms[Pick(e, 1, 3)], w);
		if (!top.empty() || !present.empty())
			Emit(out, Pick(e, 2, 2) ? "Blood was spilled around it." : "It did not fall without a fight.", v);
		deeds(200);
		break;
	}
	case GC::Kind::Fallen: {
		for (size_t n = 0; n < fallen.size(); n++) {
			const GC::Mention& m = e.mentions[fallen[n]];
			Vars w = v;
			w["N"] = unit(fallen[n]);
			w["A"] = player(m.player);
			w["K"] = Text(KillsPhrase(m.kills));
			const std::string cause = Cause(m.extra);
			w["C"] = Text(cause.empty() ? std::string() : ", " + cause);
			static const char* forms[] = {
				"{D}, far from any battle, death found {N}, a hero of {A}, at {P}{C}.",
				"{D}, {A} lost {N}, a hero of {K}, at {P}{C}.",
				"{D}, word came from {P} that {N} of {A} was dead{C}.",
			};
			Emit(out, forms[Pick(e, 1 + (uint32_t)n, 3)], w);
			if (Pick(e, 50 + (uint32_t)n, 2) == 0 && m.kills > 0) {
				Vars x = w;
				x["K"] = Text(m.kills == 1 ? std::string("the one foe slain") : "the " + Number(m.kills) + " foes slain");
				Emit(out, "The songs would remember {N} for {K}.", x);
			}
		}
		if (fallen.empty())
			Emit(out, "{D}, a hero fell at {P}.", v);
		break;
	}
	}
	return out;
}
