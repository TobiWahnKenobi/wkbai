// wkbai - the in-game help: the pages of a language, their help ids, the search
// Licensed under the GNU General Public License 3

#include "HelpInternal.h"
#include "../file.h"
#include "../Language.h"
#include "../gameset/gameset.h"
#include "../gameset/GameObjBlueprint.h"
#include "../gameset/command.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>

namespace Help::detail {

namespace {

const std::filesystem::path HELP_ROOT = std::filesystem::path("redata") / "help";

std::string lower(std::string s)
{
	for (char& c : s)
		if (c >= 'A' && c <= 'Z')
			c = (char)(c - 'A' + 'a');
	return s;
}

std::string trim(const std::string& s)
{
	size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
	return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

std::vector<std::string> split(const std::string& s, char sep)
{
	std::vector<std::string> out;
	std::string cur;
	for (char c : s) {
		if (c == sep) { out.push_back(trim(cur)); cur.clear(); }
		else cur += c;
	}
	out.push_back(trim(cur));
	return out;
}

bool readFile(const std::filesystem::path& p, std::string& out)
{
	std::ifstream f(p, std::ios::binary);
	if (!f)
		return false;
	std::stringstream ss;
	ss << f.rdbuf();
	out = ss.str();
	out.erase(std::remove(out.begin(), out.end(), '\r'), out.end());
	if (out.size() >= 3 && (unsigned char)out[0] == 0xEF && (unsigned char)out[1] == 0xBB && (unsigned char)out[2] == 0xBF)
		out.erase(0, 3);
	return true;
}

// The front matter: "key: value" lines and "[a, b]" lists between the "---" lines
struct Front {
	std::map<std::string, std::string> values;
	std::vector<std::string> ids;
};

std::string splitFront(const std::string& text, Front& front)
{
	if (text.compare(0, 3, "---") != 0)
		return text;
	const size_t end = text.find("\n---", 3);
	if (end == std::string::npos)
		return text;
	std::istringstream in(text.substr(3, end - 3));
	std::string line;
	while (std::getline(in, line)) {
		const size_t colon = line.find(':');
		if (colon == std::string::npos)
			continue;
		const std::string key = trim(line.substr(0, colon));
		std::string value = trim(line.substr(colon + 1));
		if (key == "ids" && value.size() >= 2 && value.front() == '[' && value.back() == ']') {
			for (const std::string& v : split(value.substr(1, value.size() - 2), ','))
				if (!v.empty())
					front.ids.push_back(v);
			continue;
		}
		if (value.size() >= 2 && (value.front() == '"' || value.front() == '\'') && value.back() == value.front())
			value = value.substr(1, value.size() - 2);
		front.values[key] = value;
	}
	size_t body = end + 4;
	while (body < text.size() && text[body] == '\n')
		body++;
	return text.substr(body);
}

// SHA-1 (FIPS 180-4), for the "translation-of" fingerprint (tools/help/translation.py)
std::string sha1Hex(const std::string& msg)
{
	uint32_t h[5] = { 0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0 };
	std::string m = msg;
	const uint64_t bits = (uint64_t)msg.size() * 8;
	m += (char)0x80;
	while (m.size() % 64 != 56)
		m += (char)0;
	for (int i = 7; i >= 0; i--)
		m += (char)((bits >> (i * 8)) & 0xFF);
	auto rol = [](uint32_t x, int n) { return (x << n) | (x >> (32 - n)); };
	for (size_t off = 0; off < m.size(); off += 64) {
		uint32_t w[80];
		for (int i = 0; i < 16; i++)
			w[i] = ((uint32_t)(uint8_t)m[off + i * 4] << 24) | ((uint32_t)(uint8_t)m[off + i * 4 + 1] << 16)
				| ((uint32_t)(uint8_t)m[off + i * 4 + 2] << 8) | (uint32_t)(uint8_t)m[off + i * 4 + 3];
		for (int i = 16; i < 80; i++)
			w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
		uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
		for (int i = 0; i < 80; i++) {
			uint32_t f, k;
			if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999; }
			else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
			else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
			else { f = b ^ c ^ d; k = 0xCA62C1D6; }
			const uint32_t t = rol(a, 5) + f + e + k + w[i];
			e = d; d = c; c = rol(b, 30); b = a; a = t;
		}
		h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
	}
	char out[41];
	for (int i = 0; i < 5; i++)
		snprintf(out + i * 8, 9, "%08x", h[i]);
	return out;
}

std::string fingerprint(const std::string& body)
{
	return sha1Hex(trim(body)).substr(0, 12);
}

// The anchors of a body's headings: "<!-- #x -->" at a heading's end (translations), else its slug
std::vector<std::string> headingAnchors(const std::string& body, std::string* headingText = nullptr)
{
	std::vector<std::string> out;
	std::istringstream in(body);
	std::string line;
	bool code = false;
	while (std::getline(in, line)) {
		const std::string t = trim(line);
		if (t.compare(0, 3, "```") == 0)
			code = !code;
		if (code || t.empty() || t[0] != '#')
			continue;
		size_t n = 0;
		while (n < t.size() && t[n] == '#')
			n++;
		if (n > 6 || n >= t.size() || t[n] != ' ')
			continue;
		std::string text = trim(t.substr(n));
		std::string anchor;
		if (const size_t c = text.find("<!--"); c != std::string::npos) {
			const size_t hash = text.find('#', c);
			const size_t end = text.find("-->", c);
			if (hash != std::string::npos && end != std::string::npos && hash < end)
				anchor = trim(text.substr(hash + 1, end - hash - 1));
			text = trim(text.substr(0, c));
		}
		out.push_back(anchor.empty() ? Slug(text) : anchor);
		if (headingText)
			*headingText += lower(text) + " ";
	}
	return out;
}

// Markdown as plain lower-case words, for the search
std::string plainText(const std::string& body)
{
	std::string s;
	s.reserve(body.size());
	for (size_t i = 0; i < body.size(); i++) {
		if (body.compare(i, 4, "<!--") == 0) {
			const size_t e = body.find("-->", i);
			if (e == std::string::npos) break;
			i = e + 2;
			continue;
		}
		if (body[i] == ']' && i + 1 < body.size() && body[i + 1] == '(') {
			const size_t e = body.find(')', i);
			if (e == std::string::npos) break;
			i = e;
			s += ' ';
			continue;
		}
		// table rules ("---", ":---:") and code fence lines ("```facts") say nothing
		if (body.compare(i, 3, "---") == 0) {
			while (i + 1 < body.size() && strchr("-:", body[i + 1])) i++;
			s += ' ';
			continue;
		}
		if (body.compare(i, 3, "```") == 0) {
			const size_t e = body.find('\n', i);
			if (e == std::string::npos) break;
			i = e;
			s += ' ';
			continue;
		}
		const char c = body[i];
		if (strchr("#*_`|>[]!", c))
			s += ' ';
		else
			s += c;
	}
	return s;
}

// The game's name of a thing, as a page title: the first line of its language entry, without
// colour codes, jokes and shouting (tools/help/gamedata_pages.py: tidy_game_name)
std::string gameName(const Language* game, const std::string& key)
{
	if (!game || key.empty())
		return {};
	const std::string v = game->getText(key);
	if (v == key)
		return {};
	std::string s = v.substr(0, v.find("\\n"));
	for (size_t p; (p = s.find("\\c")) != std::string::npos;)
		s.erase(p, 2);
	if (const size_t p = s.find(", heh"); p != std::string::npos)
		s.resize(p);
	if (s.compare(0, 4, "Set ") == 0)
		s.erase(0, 4);
	s = trim(s);
	while (!s.empty() && (s.back() == '!' || s.back() == '?' || s.back() == '.'))
		s.pop_back();
	if (s.find('%') != std::string::npos || std::count(s.begin(), s.end(), ' ') > 4)
		return {};
	return trim(s);
}

// ImGui windows by name: their help pages (an "imguiwindow:<name>" help id)
const std::map<std::string, std::string>& imguiWindows()
{
	static const std::map<std::string, std::string> m = {
		{ "Statistics (F6)", "ui/hud/chronicle#statistics" },
		{ "Timeline (F7)", "ui/hud/chronicle#timeline" },
		{ "##replay", "ui/hud/replay-controls" },
		{ "Events (L)", "ui/hud/replay-controls" },
		{ "Map editor (F8)", "ui/editor" },
		{ "New blank map", "ui/editor/blank" },
		{ "Export for the original game", "ui/editor/export" },
		{ "New map of another size", "ui/editor/newsize" },
		{ "Environment debug (F9)", "controls/developer-tools#environment-panel-f9" },
		{ "Client messages", "controls/developer-tools#debugger-f2" },
		{ "Client Object Tree", "controls/developer-tools#debugger-f2" },
		{ "Client Object Properties", "controls/developer-tools#debugger-f2" },
		{ "Server messages", "controls/developer-tools#server-window-ctrlf1" },
		{ "Server Object Tree", "controls/developer-tools#server-window-ctrlf1" },
		{ "Server Object Properties", "controls/developer-tools#server-window-ctrlf1" },
	};
	return m;
}

std::map<std::string, std::vector<std::string>> s_pageIds; // page path -> its English ids

} // namespace

std::string Slug(const std::string& text)
{
	std::string out;
	for (char c : text) {
		if (strchr("`*_[]()", c))
			continue;
		if (c >= 'A' && c <= 'Z')
			c = (char)(c - 'A' + 'a');
		if (c == ' ')
			c = '-';
		if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')
			out += c;
	}
	// trim as Python's strip() did before the replacements
	while (!out.empty() && out.front() == '-') out.erase(out.begin());
	while (!out.empty() && out.back() == '-') out.pop_back();
	return out;
}

bool ReadText(const std::string& path, std::string& out)
{
	if (!FileExists(path.c_str()))
		return false;
	char* data = nullptr;
	int size = 0;
	LoadFile(path.c_str(), &data, &size, 1);
	out.assign(data, size);
	free(data);
	out.erase(std::remove(out.begin(), out.end(), '\r'), out.end());
	return true;
}

bool PageLink(const std::string& from, const std::string& url, std::string& path, std::string& anchor)
{
	if (url.find("://") != std::string::npos || url.find(':') != std::string::npos)
		return false;
	std::string target = url, frag;
	if (const size_t h = url.find('#'); h != std::string::npos) {
		target = url.substr(0, h);
		frag = url.substr(h + 1);
	}
	if (target.empty()) {
		path = from;
		anchor = frag;
		return true;
	}
	if (target.size() < 3 || target.compare(target.size() - 3, 3, ".md") != 0)
		return false;
	target.resize(target.size() - 3);
	std::vector<std::string> parts;
	const size_t slash = from.rfind('/');
	if (slash != std::string::npos)
		for (const std::string& p : split(from.substr(0, slash), '/'))
			parts.push_back(p);
	for (const std::string& p : split(target, '/')) {
		if (p == "..") { if (!parts.empty()) parts.pop_back(); }
		else if (p != "." && !p.empty()) parts.push_back(p);
	}
	path.clear();
	for (const std::string& p : parts)
		path += (path.empty() ? "" : "/") + p;
	anchor = frag;
	return true;
}

Library& Lib()
{
	static Library lib;
	return lib;
}

void Library::load(const std::string& language, const Language* game)
{
	pages.clear();
	ids.clear();
	uiTexts.clear();
	langs.clear();
	s_pageIds.clear();
	std::error_code ec;

	// the languages: folders with a _language.json
	std::map<std::string, nlohmann::json> metas;
	for (const auto& entry : std::filesystem::directory_iterator(HELP_ROOT, ec)) {
		if (!entry.is_directory())
			continue;
		std::string text;
		if (!readFile(entry.path() / "_language.json", text))
			continue;
		try {
			nlohmann::json j = nlohmann::json::parse(text);
			const std::string code = entry.path().filename().string();
			langs.push_back({ code, j.value("name", code) });
			metas[code] = std::move(j);
		}
		catch (const std::exception& e) {
			printf("WARNING: help: %s/_language.json: %s\n", entry.path().string().c_str(), e.what());
		}
	}
	std::sort(langs.begin(), langs.end());

	// which language: the one asked for, else the one whose "detect" texts the game has, else English
	lang = "en";
	Language own;
	if (!game && FileExists("Languages\\Language.txt")) {
		own.load("Languages\\Language.txt");
		game = &own;
	}
	if (!language.empty() && language != "auto" && metas.count(language))
		lang = language;
	else if (game) {
		for (const auto& [code, meta] : metas) {
			if (code == "en" || !meta.contains("detect") || !meta["detect"].is_object() || meta["detect"].empty())
				continue;
			bool all = true;
			for (const auto& [key, v] : meta["detect"].items())
				if (!v.is_string() || v.get<std::string>().empty() || game->getText(key) != v.get<std::string>())
					all = false;
			if (all) {
				lang = code;
				break;
			}
		}
	}
	for (const std::string& code : { std::string("en"), lang })
		if (metas.count(code) && metas[code].contains("ui") && metas[code]["ui"].is_object())
			for (const auto& [k, v] : metas[code]["ui"].items())
				if (v.is_string() && !v.get<std::string>().empty())
					uiTexts[k] = v.get<std::string>();

	// the English pages, each with the language's translation over it when there is one
	const std::filesystem::path base = HELP_ROOT / "en";
	for (const auto& entry : std::filesystem::recursive_directory_iterator(base, ec)) {
		if (!entry.is_regular_file() || entry.path().extension() != ".md")
			continue;
		std::string rel = std::filesystem::relative(entry.path(), base, ec).generic_string();
		rel.resize(rel.size() - 3);
		std::string text;
		if (!readFile(entry.path(), text))
			continue;
		Front front;
		const std::string enBody = splitFront(text, front);
		Page p;
		p.path = rel;
		p.body = enBody;
		p.lang = "en";
		p.title = front.values.count("title") ? front.values["title"] : rel.substr(rel.rfind('/') + 1);
		p.parent = front.values.count("parent") ? front.values["parent"] : "";
		try { p.order = front.values.count("order") ? std::stod(front.values["order"]) : 1000; }
		catch (...) { p.order = 1000; }
		if (lang != "en") {
			std::string tr;
			if (readFile(HELP_ROOT / lang / (rel + ".md"), tr)) {
				Front tf;
				p.body = splitFront(tr, tf);
				p.lang = lang;
				if (tf.values.count("title")) p.title = tf.values["title"];
				if (tf.values.count("order")) try { p.order = std::stod(tf.values["order"]); } catch (...) {}
				p.state = tf.values.count("translation-of") && tf.values["translation-of"] == fingerprint(enBody)
					? Page::TRANSLATED : Page::OUT_OF_DATE;
			}
			else
				p.state = Page::ENGLISH;
		}
		p.anchors = headingAnchors(p.body, &p.searchHeadings);
		p.plainText = plainText(p.body);
		p.searchText = lower(p.plainText);
		p.searchTitle = lower(p.title);
		s_pageIds[rel] = front.ids;
		pages[rel] = std::move(p);
	}
	// the ids: each page's path, and what its English front matter lists
	for (const auto& [rel, list] : s_pageIds) {
		ids.try_emplace(rel, rel, "");
		if (rel.size() > 6 && rel.compare(rel.size() - 6, 6, "/index") == 0)
			ids.try_emplace(rel.substr(0, rel.size() - 6), rel, "");
		for (const std::string& id : list) {
			const size_t h = id.find('#');
			ids.try_emplace(id.substr(0, h), rel, h == std::string::npos ? "" : id.substr(h + 1));
		}
	}
	printf("Help: %zu pages, %zu help ids, language %s\n", pages.size(), ids.size(), lang.c_str());
}

const Page* Library::page(const std::string& path) const
{
	auto it = pages.find(path);
	return it == pages.end() ? nullptr : &it->second;
}

bool Library::resolveOne(const std::string& idIn, std::string& path, std::string& anchor) const
{
	std::string id = idIn, wantAnchor;
	if (const size_t h = id.find('#'); h != std::string::npos) {
		wantAnchor = id.substr(h + 1);
		id.resize(h);
	}
	if (id.compare(0, 12, "imguiwindow:") == 0) {
		// (a child window: its window's, "Map editor (F8)/list_C7115161")
		auto it = imguiWindows().find(id.substr(12, id.find('/', 12) == std::string::npos ? std::string::npos : id.find('/', 12) - 12));
		return it != imguiWindows().end() && resolveOne(it->second, path, anchor);
	}
	// "page:<path>#<anchor>": a page by its path (the heading a label matched: Help.cpp)
	if (id.compare(0, 5, "page:") == 0) {
		if (!pages.count(id.substr(5)))
			return false;
		path = id.substr(5);
		anchor = wantAnchor;
		return true;
	}
	// "prefix/**/last": an id under prefix that ends in /last (the lobby's tabs, whatever the tab)
	if (const size_t w = id.find("/**/"); w != std::string::npos) {
		const std::string prefix = id.substr(0, w + 1), last = "/" + id.substr(w + 4);
		std::string best;
		for (const auto& [k, v] : ids)
			if (k.size() > prefix.size() + last.size() && k.compare(0, prefix.size(), prefix) == 0
				&& k.compare(k.size() - last.size(), last.size(), last) == 0 && (best.empty() || k < best))
				best = k;
		if (best.empty())
			return false;
		id = best;
	}
	auto it = ids.find(id);
	if (it == ids.end())
		return false;
	path = it->second.first;
	anchor = wantAnchor.empty() ? it->second.second : wantAnchor;
	return true;
}

bool Library::resolve(const std::string& list, std::string& path, std::string& anchor, bool* exact) const
{
	const std::vector<std::string> cands = split(list, '|');
	for (const std::string& c : cands)
		if (!c.empty() && resolveOne(c, path, anchor)) {
			if (exact) *exact = true;
			return true;
		}
	if (exact) *exact = false;
	// the nearest parent of the first candidate that has a page
	for (const std::string& c : cands) {
		std::string probe = c.substr(0, c.find('#'));
		if (probe.compare(0, 12, "imguiwindow:") == 0)
			continue;
		if (const size_t w = probe.find("/**/"); w != std::string::npos)
			probe.resize(w);
		while (true) {
			const size_t slash = probe.rfind('/');
			if (slash == std::string::npos)
				break;
			probe.resize(slash);
			if (resolveOne(probe, path, anchor))
				return true;
		}
	}
	path = "index";
	anchor.clear();
	return pages.count("index") > 0;
}

std::vector<Hit> Library::search(const std::string& query) const
{
	std::vector<std::string> words;
	{
		std::istringstream in(lower(query));
		std::string w;
		while (in >> w)
			if (w.size() >= 2)
				words.push_back(w);
	}
	std::vector<Hit> hits;
	if (words.empty())
		return hits;
	for (const auto& [path, p] : pages) {
		int score = 0;
		bool all = true;
		size_t first = std::string::npos;
		for (const std::string& w : words) {
			int s = 0;
			if (p.searchTitle.find(w) != std::string::npos) s += 20;
			if (p.searchHeadings.find(w) != std::string::npos) s += 6;
			for (size_t pos = p.searchText.find(w); pos != std::string::npos && s < 60; pos = p.searchText.find(w, pos + 1)) {
				s += 1;
				first = std::min(first, pos);
			}
			if (s == 0) all = false;
			score += s;
		}
		if (!all)
			continue;
		Hit h;
		h.path = path;
		h.score = score + (p.state == Page::ENGLISH ? 0 : 1);
		if (first != std::string::npos) {
			const size_t from = first > 50 ? first - 50 : 0;
			h.snippet = p.plainText.substr(from, 140);
			std::replace(h.snippet.begin(), h.snippet.end(), '\n', ' ');
			for (size_t q; (q = h.snippet.find("  ")) != std::string::npos;)
				h.snippet.erase(q, 1);
			h.snippet = (from > 0 ? "..." : "") + trim(h.snippet) + "...";
		}
		hits.push_back(std::move(h));
	}
	std::sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) { return a.score != b.score ? a.score > b.score : a.path < b.path; });
	if (hits.size() > 60)
		hits.resize(60);
	return hits;
}

std::string Library::ui(const std::string& key, const std::string& a, const std::string& b) const
{
	auto it = uiTexts.find(key);
	std::string s = it == uiTexts.end() ? key : it->second;
	for (size_t p; (p = s.find("{0}")) != std::string::npos;) s.replace(p, 3, a);
	for (size_t p; (p = s.find("{1}")) != std::string::npos;) s.replace(p, 3, b);
	return s;
}

void Library::setGameNames(const GameSet* gs, const Language* game)
{
	std::map<std::string, int> count;
	std::map<std::string, std::string> found;
	for (auto& [path, p] : pages) {
		p.gameName.clear();
		if (!gs || !game)
			continue;
		const auto& list = s_pageIds[path];
		std::string name;
		for (const std::string& id : list) {
			const std::string bare = id.substr(0, id.find('#'));
			if (bare.compare(0, 5, "unit/") == 0 || bare.compare(0, 9, "building/") == 0) {
				const bool unit = bare[0] == 'u';
				const auto* bp = gs->findBlueprint(unit ? Tags::GAMEOBJCLASS_CHARACTER : Tags::GAMEOBJCLASS_BUILDING, bare.substr(unit ? 5 : 9));
				if (bp)
					name = gameName(game, bp->tooltip);
			}
			else if (bare.compare(0, 8, "command/") == 0 && (path.compare(0, 9, "commands/") == 0 || path.compare(0, 9, "upgrades/") == 0)) {
				const int x = gs->commands.names.getIndex(bare.substr(8));
				if (x >= 0)
					name = gameName(game, gs->commands[x].defaultHint);
				if (path.compare(0, 9, "upgrades/") == 0 && name.compare(0, 9, "Research ") == 0)
					name.erase(0, 9);
			}
			if (!name.empty())
				break;
		}
		if (!name.empty()) {
			found[path] = name;
			count[lower(name)]++;
		}
	}
	// two pages with one game name ("Close") keep their own, more telling titles
	for (const auto& [path, name] : found)
		if (count[lower(name)] == 1)
			pages[path].gameName = name;
}

std::string Library::check() const
{
	std::string out;
	auto problem = [&](const std::string& s) { out += s + "\n"; };
	static const std::regex linkRe(R"(\[[^\]]*\]\((<[^>]+>|[^)\s]+)\))");
	for (const auto& [path, p] : pages) {
		std::set<std::string> anchors(p.anchors.begin(), p.anchors.end());
		for (std::sregex_iterator it(p.body.begin(), p.body.end(), linkRe), end; it != end; ++it) {
			std::string url = (*it)[1].str();
			if (!url.empty() && url[0] == '<')
				url = url.substr(1, url.size() - 2);
			if (url.compare(0, 4, "http") == 0 || url.compare(0, 5, "shot:") == 0 || url.compare(0, 5, "game:") == 0
				|| url.compare(0, 5, "show:") == 0 || url.compare(0, 7, "mailto:") == 0)
				continue;
			for (size_t q; (q = url.find("%20")) != std::string::npos;)
				url.replace(q, 3, " ");
			if (url.compare(0, 5, "help:") == 0) {
				std::string tp, ta;
				bool exact = false;
				resolve(url.substr(5), tp, ta, &exact);
				if (!exact)
					problem(path + ": help id " + url.substr(5) + " has no page");
				continue;
			}
			std::string tp, ta;
			if (!PageLink(path, url, tp, ta))
				continue; // a picture beside the pages, not a page
			const Page* t = page(tp);
			if (!t)
				problem(path + ": link " + url + ": no such page");
			else if (!ta.empty() && std::find(t->anchors.begin(), t->anchors.end(), ta) == t->anchors.end())
				problem(path + ": link " + url + ": no heading '" + ta + "'");
		}
	}
	for (const auto& [id, target] : ids) {
		const Page* t = page(target.first);
		if (!t)
			problem("id " + id + ": no page " + target.first);
		else if (!target.second.empty() && std::find(t->anchors.begin(), t->anchors.end(), target.second) == t->anchors.end())
			problem("id " + id + ": " + target.first + " has no heading '" + target.second + "'");
	}
	return out;
}

}
