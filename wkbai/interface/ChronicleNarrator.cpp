// wkbai - the timeline told as a chronicle (ChronicleNarrator.h)

#include "ChronicleNarrator.h"
#include "../Environment.h"
#include "../file.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <set>

namespace {
using Piece = ChronicleNarrator::Piece;
using Pieces = std::vector<Piece>;
using Vars = std::map<std::string, Pieces>;
using GC = GameChronicle;

// ---- the words (redata/chronicle/english.txt)

struct Book {
	std::map<std::string, std::vector<std::string>> words, sentences;
	bool tried = false, loaded = false;
};
Book g_book;

std::string Trim(const std::string& s)
{
	const size_t a = s.find_first_not_of(" \t\r\n");
	if (a == std::string::npos)
		return std::string();
	const size_t b = s.find_last_not_of(" \t\r\n");
	return s.substr(a, b - a + 1);
}

void Parse(const std::string& text)
{
	g_book.words.clear();
	g_book.sentences.clear();
	std::string slot;
	size_t at = 0;
	while (at <= text.size()) {
		size_t end = text.find('\n', at);
		if (end == std::string::npos)
			end = text.size();
		const std::string line = Trim(text.substr(at, end - at));
		at = end + 1;
		if (line.empty() || line[0] == '#')
			continue;
		if (line[0] == '@') {
			// @NAME = one | another
			const size_t eq = line.find('=');
			if (eq == std::string::npos)
				continue;
			const std::string name = Trim(line.substr(1, eq - 1));
			auto& list = g_book.words[name];
			size_t a = eq + 1;
			while (a <= line.size()) {
				size_t b = line.find('|', a);
				if (b == std::string::npos)
					b = line.size();
				const std::string alt = Trim(line.substr(a, b - a));
				if (!alt.empty())
					list.push_back(alt);
				a = b + 1;
			}
			slot.clear(); // (a word ends a sentence block)
			continue;
		}
		if (line[0] == '[' && line.back() == ']') {
			slot = Trim(line.substr(1, line.size() - 2));
			continue;
		}
		if (!slot.empty())
			g_book.sentences[slot].push_back(line);
	}
}

void EnsureLoaded()
{
	if (!g_book.tried)
		ChronicleNarrator::load();
}

uint32_t HashString(const std::string& s)
{
	uint32_t h = 2166136261u;
	for (unsigned char ch : s)
		h = (h ^ ch) * 16777619u;
	return h;
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

std::string Capital(std::string s)
{
	if (!s.empty())
		s[0] = (char)std::toupper((unsigned char)s[0]);
	return s;
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

bool VowelSound(const char* w)
{
	static const char* silentH[] = { "honou", "hour", "heir", "honest" };
	for (const char* h : silentH)
		if (_strnicmp(w, h, strlen(h)) == 0)
			return true;
	return w[0] && std::strchr("aeiouAEIOU", w[0]) && _strnicmp(w, "one", 3) != 0 && _strnicmp(w, "use", 3) != 0;
}

// the plain text tidied: "a" before a vowel sound becomes "an"; the comma after an opening
// that ends in "when" or "that" goes; doubled commas and spaces go
void Tidy(std::string& s)
{
	for (const auto& [from, to] : std::vector<std::pair<const char*, const char*>>{ { "when, ", "when " }, { "that, ", "that " },
		{ ",, ", ", " }, { ",,", "," }, { " ,", "," }, { ",.", "." }, { "  ", " " }, { " on on ", " on " }, { " in in ", " in " }, { " at at ", " at " } }) {
		size_t at;
		while ((at = s.find(from)) != std::string::npos)
			s.replace(at, strlen(from), to);
	}
	for (size_t i = 0; i + 2 < s.size(); i++)
		if ((s[i] == 'a' || s[i] == 'A') && s[i + 1] == ' ' && (i == 0 || s[i - 1] == ' ') && VowelSound(s.c_str() + i + 2))
			s.insert(i + 1, "n");
}

// ---- telling one event

struct Teller {
	const GC::Event& e;
	std::vector<std::string> tags;
	uint32_t counter = 0;
	std::map<std::string, std::set<int>> used; // the alternatives used in this paragraph

	explicit Teller(const GC::Event& ev) : e(ev) {}

	int pick(const std::string& key, int n)
	{
		if (n <= 1)
			return 0;
		uint32_t h = e.id * 2654435761u ^ (uint32_t)(e.time * 10.0f) * 40503u ^ HashString(key) ^ (counter++) * 2246822519u;
		h ^= h >> 15; h *= 2654435761u; h ^= h >> 13;
		// not the same one twice in a paragraph while others are left
		auto& u = used[key];
		if ((int)u.size() >= n)
			u.clear();
		int i = (int)(h % (uint32_t)n);
		while (u.count(i))
			i = (i + 1) % n;
		u.insert(i);
		return i;
	}

	// a word or a slot by the event's tags: "NAME.tag" first, in the tags' order, then "NAME"
	const std::vector<std::string>* find(const std::map<std::string, std::vector<std::string>>& in, const std::string& name, const std::vector<std::string>& extra) const
	{
		for (const auto* list : { &extra, &tags })
			for (const std::string& t : *list)
				if (auto it = in.find(name + "." + t); it != in.end() && !it->second.empty())
					return &it->second;
		auto it = in.find(name);
		return it != in.end() && !it->second.empty() ? &it->second : nullptr;
	}

	void expand(Pieces& out, const std::string& tpl, const Vars& facts, const std::vector<std::string>& extra, int depth)
	{
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
			if (close == std::string::npos)
				break;
			const std::string key = tpl.substr(open + 1, close - open - 1);
			i = close + 1;
			if (auto it = facts.find(key); it != facts.end())
				out.insert(out.end(), it->second.begin(), it->second.end());
			else if (const auto* alts = depth < 8 ? find(g_book.words, key, extra) : nullptr)
				expand(out, (*alts)[pick("@" + key, (int)alts->size())], facts, extra, depth + 1);
			// (an unknown word: left out)
		}
	}

	// a slot's form, expanded, its plain pieces merged and tidied, the first letter a capital;
	// empty if the slot has none
	Pieces sentence(const std::string& slot, const Vars& facts, const std::vector<std::string>& extra = {}, bool capital = true)
	{
		Pieces out;
		const auto* forms = find(g_book.sentences, slot, extra);
		if (!forms)
			return out;
		expand(out, (*forms)[pick("[" + slot, (int)forms->size())], facts, extra, 0);
		Pieces merged;
		for (Piece& p : out) {
			if (p.player == 0 && p.mention < 0 && !merged.empty() && merged.back().player == 0 && merged.back().mention < 0)
				merged.back().text += p.text;
			else
				merged.push_back(std::move(p));
		}
		for (Piece& p : merged)
			if (p.player == 0 && p.mention < 0)
				Tidy(p.text);
		// (a player's name begins it, or a sentence within it: "the Baron" -> "The Baron"; a unit's
		// name stays as it is)
		for (size_t j = 0; j < merged.size(); j++)
			if (merged[j].player != 0 && !merged[j].text.empty()) {
				const bool starts = j == 0 ? capital : merged[j - 1].mention < 0 && merged[j - 1].player == 0
					&& merged[j - 1].text.size() >= 2 && merged[j - 1].text.compare(merged[j - 1].text.size() - 2, 2, ". ") == 0;
				if (starts)
					merged[j].text[0] = (char)std::toupper((unsigned char)merged[j].text[0]);
			}
		if (capital && !merged.empty() && merged[0].player == 0 && merged[0].mention < 0) {
			std::string& s = merged[0].text;
			const size_t first = s.find_first_not_of(' ');
			if (first != std::string::npos) {
				s.erase(0, first);
				s[0] = (char)std::toupper((unsigned char)s[0]);
			}
		}
		return merged;
	}

	// a word expanded to plain text
	std::string word(const std::string& name, const Vars& facts = {}, const std::vector<std::string>& extra = {})
	{
		Pieces p;
		expand(p, "{" + name + "}", facts, extra, 0);
		std::string s;
		for (const Piece& x : p)
			s += x.text;
		return s;
	}
};

void Append(Pieces& out, Pieces sentence)
{
	if (sentence.empty())
		return;
	if (!out.empty())
		out.push_back({ " " });
	out.insert(out.end(), sentence.begin(), sentence.end());
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

const char* StorySeason(int s)
{
	static const char* names[] = { "spring", "summer", "autumn", "winter" };
	return names[((s % 4) + 4) % 4];
}

// the time of day: the tag of its words, and the date line's word
const char* TodTag(float hour)
{
	if (hour >= 5.0f && hour < 7.0f) return "dawn";
	if (hour >= 7.0f && hour < 11.0f) return "morning";
	if (hour >= 11.0f && hour < 14.0f) return "noon";
	if (hour >= 14.0f && hour < 18.0f) return "afternoon";
	if (hour >= 18.0f && hour < 21.0f) return "dusk";
	return "night";
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

// a settlement's blueprint as a word: "Village Wood" -> "village", "Manor" -> "manor"
std::string SettlementWord(const std::string& what)
{
	std::string s = what.empty() ? std::string("settlement") : what;
	for (const char* material : { " Wood", " Stone", " Brick", " Timber" }) {
		const size_t n = strlen(material);
		if (s.size() > n && s.compare(s.size() - n, n, material) == 0)
			s.resize(s.size() - n);
	}
	for (char& ch : s)
		ch = (char)std::tolower((unsigned char)ch);
	return s;
}

// a death's cause as a tag of the CAUSE word (and the killer's name, for "slain")
std::string CauseTag(const std::string& extra, std::string& killer)
{
	static const std::map<std::string, std::string> causes = {
		{ "froze to death", "cold" }, { "died of thirst", "heat" }, { "drowned", "drowned" }, { "went through the ice", "ice" },
		{ "burned to death", "fire" }, { "struck by lightning", "lightning" }, { "fell", "none" }, { "", "none" } };
	if (auto it = causes.find(extra); it != causes.end())
		return it->second;
	if (extra.rfind("slain by ", 0) == 0) {
		killer = extra.substr(9);
		return "slain";
	}
	return "other";
}
}

bool ChronicleNarrator::load()
{
	g_book.tried = true;
	const char* path = "chronicle/english.txt";
	if (!FileExists(path))
		return g_book.loaded = false;
	char* text = nullptr;
	int size = 0;
	LoadFile(path, &text, &size);
	Parse(std::string(text, size));
	free(text);
	return g_book.loaded = !g_book.sentences.empty();
}

void ChronicleNarrator::loadText(const std::string& text)
{
	g_book.tried = true;
	Parse(text);
	g_book.loaded = !g_book.sentences.empty();
}

const std::map<std::string, std::vector<std::string>>& ChronicleNarrator::words() { EnsureLoaded(); return g_book.words; }
const std::map<std::string, std::vector<std::string>>& ChronicleNarrator::sentences() { EnsureLoaded(); return g_book.sentences; }

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

std::string ChronicleNarrator::dateLine(float time, const Context& c)
{
	const Calendar cal = CalendarAt(time, c);
	char clock[16];
	snprintf(clock, sizeof(clock), "%d:%02d", (int)time / 60, (int)time % 60);
	std::string s;
	if (c.seasons) {
		s = Capital(StorySeason(cal.season)) + ", the year " + std::to_string(cal.year);
		if (c.dayNight)
			s += std::string(", ") + TodTag(cal.hour);
	}
	else if (c.dayNight)
		s = "Day " + std::to_string(cal.day) + ", " + TodTag(cal.hour);
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
	EnsureLoaded();
	Pieces out;
	if (g_book.sentences.empty()) {
		out.push_back({ e.title() + "." });
		return out;
	}
	Teller t(e);
	const Calendar cal = CalendarAt(e.time, c);
	const bool weatherKnown = e.weather >= 0 && e.weather < WEATHER_COUNT;
	// the event's tags, in the order its words are looked up by
	const char* tod = c.dayNight ? TodTag(cal.hour) : nullptr;
	const bool night = tod && std::strcmp(tod, "night") == 0;
	if (weatherKnown) {
		static const std::vector<std::vector<std::string>> byWeather = {
			{ "clear" }, { "cloudy" }, { "rain" }, { "heavyrain", "rain" }, { "storm", "rain" }, { "fog" },
			{ "snow" }, { "blizzard", "snow" }, { "heat" }, { "drought", "heat" } };
		if (night && e.weather == WEATHER_CLEAR)
			t.tags.push_back("clearnight");
		if (night && (e.weather == WEATHER_HEATWAVE || e.weather == WEATHER_DROUGHT))
			t.tags.push_back("hotnight");
		for (const std::string& w : byWeather[e.weather])
			t.tags.push_back(w);
		if (e.temperature < -2.0f)
			t.tags.push_back("cold");
		if (e.snowCover > 0.3f)
			t.tags.push_back("snowground");
	}
	if (tod)
		t.tags.push_back(tod);
	if (c.seasons)
		t.tags.push_back(StorySeason(cal.season));
	t.tags.push_back(e.time < 120.0f ? "outset" : e.time < 600.0f ? "early" : e.time < 1800.0f ? "mid" : "late");
	const int n = e.size;
	t.tags.push_back(n < 8 ? "skirmish" : n < 25 ? "battle" : n < 60 ? "great" : "huge");

	// a player's name: an unnamed one (the map's own villages) as the people of the land, the
	// barbarians' "Baresark Player" as "the Baresarks"; the same words for it all through
	std::map<uint32_t, std::string> sideNames;
	auto player = [&](uint32_t id) {
		auto it = sideNames.find(id);
		if (it == sideNames.end()) {
			std::string name = c.playerName ? c.playerName(id) : std::string();
			if (name.empty() || name.find_first_not_of("0123456789") == std::string::npos)
				name = t.word("UNKNOWN_SIDE");
			else if (name.size() > 7 && name.compare(name.size() - 7, 7, " Player") == 0)
				name = "the " + name.substr(0, name.size() - 7) + "s";
			if (name.empty())
				name = "the people of the land";
			it = sideNames.emplace(id, name).first;
		}
		return Pieces{ Piece{ it->second, id } };
	};
	// (one piece: "'s" never wraps away from the name)
	auto possessive = [&](uint32_t id) { Pieces p = player(id); p[0].text += !p[0].text.empty() && p[0].text.back() == 's' ? "'" : "'s"; return p; };
	// a unit with a name ("Sigurd the Valiant"), or one known by its type only ("unnamed", the
	// server's: "a Trebuchet")
	auto unnamed = [&](size_t i) { return e.mentions[i].extra == "unnamed" || e.mentions[i].name.empty(); };
	auto unit = [&](size_t i) {
		const GC::Mention& m = e.mentions[i];
		Pieces out;
		if (unnamed(i))
			out.push_back({ m.name.empty() ? std::string("a warrior whose name is lost") : std::string(VowelSound(m.name.c_str()) ? "an " : "a ") });
		Piece p{ m.name.empty() ? std::string() : m.name };
		p.mention = (int)i;
		if (!p.text.empty())
			out.push_back(p);
		return out;
	};
	// the type alone, for "Saint Jane's Trebuchet"
	auto unitBare = [&](size_t i) {
		Piece p{ e.mentions[i].name.empty() ? std::string("warrior") : e.mentions[i].name };
		p.mention = (int)i;
		return Pieces{ p };
	};

	// the facts every sentence may use
	Vars v;
	v["PLACE"] = Text(e.place.empty() ? std::string("a nameless place in the wilds") : e.place);
	v["YEAR"] = Text(std::to_string(cal.year));
	v["DAY"] = Text(Ordinal(cal.day));
	v["COUNT"] = Text(Number(n));
	std::vector<uint32_t> sides(e.players.begin(), e.players.end());
	{
		std::vector<Pieces> names;
		for (uint32_t p : sides)
			names.push_back(player(p));
		v["SIDES"] = names.empty() ? Text("the warring sides") : List(names);
	}
	// the opening, by the calendar known
	{
		const char* slot = c.seasons ? (c.dayNight ? (weatherKnown ? "date.season.tod.weather" : "date.season.tod") : (weatherKnown ? "date.season.weather" : "date.season"))
			: c.dayNight ? (weatherKnown ? "date.day.tod.weather" : "date.day.tod") : (weatherKnown ? "date.weather" : "date.war");
		Pieces d = t.sentence(slot, v, {}, false);
		v["DATE"] = d.empty() ? Text("Once") : d;
	}

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
	// a unit's facts ({UNIT} {UNIT_S} {SIDE} {SIDE_S} {KILLS} ...), and its death's cause as a tag
	auto unitFacts = [&](size_t i, bool late, std::vector<std::string>& extra) {
		const GC::Mention& m = e.mentions[i];
		Vars w = v;
		w["UNIT"] = unit(i);
		if (late && !unnamed(i))
			w["UNIT"].insert(w["UNIT"].begin(), Piece{ "the late " });
		w["UNIT_S"] = w["UNIT"];
		w["UNIT_S"].push_back({ "'s" });
		w["SIDE"] = player(m.player);
		w["SIDE_S"] = possessive(m.player);
		w["KILLS"] = Text(Number(m.kills));
		w["KILLS_CAP"] = Text(Capital(Number(m.kills)));
		w["KILLS_PHRASE"] = Text(m.kills == 1 ? "one kill" : Number(m.kills) + " kills");
		w["KILLS_FOES"] = Text(m.kills == 1 ? std::string("the one foe slain") : "the " + Number(m.kills) + " foes slain");
		w["N_FOES"] = Text(m.kills == 1 ? "one " + t.word("FOE") : Number(m.kills) + " " + t.word("FOES"));
		w["KILLS_CLAUSE"] = Text(m.kills > 0 ? ", slayer of " + Number(m.kills) + "," : std::string());
		std::string killer;
		const std::string tag = CauseTag(m.extra, killer);
		extra = { tag };
		w["KILLER"] = Text(killer);
		w["EXTRA"] = Text(m.extra);
		w["CAUSE_CLAUSE"] = Text(tag == "none" ? std::string() : ", " + t.word("CAUSE", w, extra));
		return w;
	};

	// who fought there and their deeds, the dead
	auto deeds = [&]() {
		for (size_t i : top) {
			const GC::Mention& m = e.mentions[i];
			std::vector<std::string> extra;
			Vars w = unitFacts(i, m.dead && !fellHere(m.unit), extra);
			Append(out, t.sentence(m.kills == 1 ? "top.one" : "top", w));
		}
		// the heroes there not named for their kills nor among the dead
		std::vector<size_t> heroes;
		for (size_t i : present) {
			const GC::Mention& m = e.mentions[i];
			// (a hero has a name: one known by its type only is no hero to tell of)
			if (unnamed(i) || fellHere(m.unit) || std::any_of(top.begin(), top.end(), [&](size_t k) { return e.mentions[k].unit == m.unit; }))
				continue;
			heroes.push_back(i);
		}
		if (heroes.size() == 1) {
			std::vector<std::string> extra;
			Vars w = unitFacts(heroes[0], false, extra);
			Append(out, t.sentence(e.mentions[heroes[0]].dead ? "hero.one.dead" : "hero.one", w));
		}
		else if (heroes.size() > 1) {
			std::vector<Pieces> list;
			for (size_t i : heroes) {
				Pieces h = unit(i);
				if (e.mentions[i].dead && !unnamed(i))
					h.insert(h.begin(), Piece{ "the late " });
				list.push_back(h);
			}
			Vars w = v;
			w["HEROES"] = List(list);
			Append(out, t.sentence("hero.many", w));
		}
		for (size_t i : fallen) {
			std::vector<std::string> extra;
			Vars w = unitFacts(i, false, extra);
			Append(out, t.sentence("fallen.here", w, extra));
		}
	};

	// the losses, and who held the field
	auto outcome = [&]() {
		std::map<uint32_t, int> lost;
		std::map<uint32_t, std::pair<std::string, int>> mostType;
		int total = 0;
		for (uint32_t p : sides) {
			int k = 0;
			if (auto it = e.lost.find(p); it != e.lost.end())
				for (const auto& [type, x] : it->second) {
					k += x;
					if (x > mostType[p].second)
						mostType[p] = { type, x };
				}
			lost[p] = k;
			total += k;
		}
		if (sides.empty())
			return;
		if (total == 0) {
			Append(out, t.sentence("outcome.none", v));
			return;
		}
		auto byLoss = sides;
		std::sort(byLoss.begin(), byLoss.end(), [&](uint32_t a, uint32_t b) { return lost[a] < lost[b] || (lost[a] == lost[b] && a < b); });
		const uint32_t winner = byLoss.front(), loser = byLoss.back();
		const int lo = lost[winner], hi = lost[loser];
		Vars w = v;
		w["WINNER"] = player(winner);
		w["LOSER"] = player(loser);
		w["WINNER_S"] = possessive(winner);
		w["LOSER_S"] = possessive(loser);
		w["SIDE"] = player(loser);
		w["SIDE_S"] = possessive(loser);
		w["LOST_HI"] = Text(Number(hi));
		w["LOST_LO"] = Text(Number(lo));
		const auto& [type, k] = mostType[loser];
		Vars types = v;
		types["TYPES"] = Text(Plural(type));
		w["TYPE_CLAUSE"] = Text(!type.empty() && k * 10 >= hi * 7 && hi >= 3 ? ", " + t.word(k == hi ? "ALL_OF_THEM" : "MOST_OF_THEM", types) : std::string());
		if (sides.size() == 1) {
			Append(out, t.sentence("outcome.single", w));
			return;
		}
		if (hi >= 2 * lo + 2 || (lo == 0 && hi > 0))
			Append(out, t.sentence(lo == 0 ? "outcome.win.noloss" : "outcome.win", w));
		else {
			std::vector<Pieces> each;
			for (uint32_t p : sides) {
				Pieces x = player(p);
				x.push_back({ " " + Number(lost[p]) });
				each.push_back(x);
			}
			w["LOSSES"] = List(each);
			Append(out, t.sentence("outcome.even", w));
		}
	};

	switch (e.kind) {
	case GC::Kind::Battle:
	case GC::Kind::Siege:
	case GC::Kind::Raid: {
		if (e.kind == GC::Kind::Battle)
			Append(out, t.sentence(sides.size() == 1 ? "battle.open.one" : "battle.open", v));
		else if (e.kind == GC::Kind::Siege)
			Append(out, t.sentence("siege.open", v));
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
			w["VICTIM"] = victim ? player(victim) : Text("its people");
			Append(out, t.sentence(most > 0 && sides.size() > 1 ? "raid.open" : "raid.open.plain", w));
		}
		// (one or two: no count worth telling)
		if (n >= 3) {
			Append(out, t.sentence("size", v));
			if (sides.size() == 2) {
				auto a = e.strength.find(sides[0]), b = e.strength.find(sides[1]);
				if (a != e.strength.end() && b != e.strength.end() && a->second > 0 && b->second > 0 && (a->second * 2 <= b->second || b->second * 2 <= a->second)) {
					const bool aMore = a->second > b->second;
					Vars w = v;
					w["MORE"] = player(aMore ? sides[0] : sides[1]);
					w["FEWER"] = player(aMore ? sides[1] : sides[0]);
					w["MORE_N"] = Text(Number(std::max(a->second, b->second)));
					w["FEWER_N"] = Text(Number(std::min(a->second, b->second)));
					Append(out, t.sentence("strength", w));
				}
			}
		}
		// the weather, often
		if (weatherKnown && t.pick("weather?", 5) < 3)
			Append(out, t.sentence("weather.fight", v));
		const float d = e.endTime - e.time;
		if (d < 20.0f)
			Append(out, t.sentence("duration.short", v));
		else if (d > 240.0f)
			Append(out, t.sentence("duration.long", v));
		outcome();
		deeds();
		break;
	}
	case GC::Kind::Founded: {
		Vars w = v;
		std::string lower = SettlementWord(e.what);
		w["SIDE"] = sides.empty() ? Text("settlers") : player(sides[0]);
		w["SIDE_S"] = sides.empty() ? Text("the settlers'") : possessive(sides[0]);
		w["SETTLEMENT"] = Text(lower);
		w["A_SETTLEMENT"] = Text("a " + lower);
		Append(out, t.sentence("founded", w));
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
		std::string lower = SettlementWord(e.what);
		w["SETTLEMENT"] = Text(lower);
		w["A_SETTLEMENT"] = Text("a " + lower);
		w["OWNER"] = owner ? player(owner) : Text("its people");
		w["OWNER_S"] = owner ? possessive(owner) : Text("its people's");
		std::vector<Pieces> by;
		for (size_t i : razers) {
			Pieces x = possessive(e.mentions[i].player);
			x.push_back({ " " });
			Pieces u = unitBare(i);
			x.insert(x.end(), u.begin(), u.end());
			by.push_back(x);
		}
		w["RAZERS"] = List(by);
		Append(out, t.sentence(by.empty() ? "destroyed" : "destroyed.by", w));
		if (!top.empty() || !present.empty())
			Append(out, t.sentence("destroyed.fight", v));
		deeds();
		break;
	}
	case GC::Kind::Fallen: {
		for (size_t i : fallen) {
			std::vector<std::string> extra;
			Vars w = unitFacts(i, false, extra);
			Append(out, t.sentence("fallen.away", w, extra));
			if (e.mentions[i].kills > 0 && t.pick("songs?", 2) == 0)
				Append(out, t.sentence("fallen.songs", w, extra));
		}
		if (fallen.empty())
			Append(out, t.sentence("fallen.unknown", v));
		break;
	}
	}
	return out;
}
