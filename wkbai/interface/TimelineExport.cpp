// wkbai - the timeline's Export (TimelineExport.h)

#include "TimelineExport.h"
#include "../file.h"
#include <cstdio>
#include <ctime>
#include <set>
#include <utility>
#include <vector>

std::string TimelineExport::text(const Input& in, bool html, const std::string& when)
{
	auto esc = [&](const std::string& s) {
		if (!html)
			return s;
		std::string o;
		for (char ch : s) {
			if (ch == '&') o += "&amp;";
			else if (ch == '<') o += "&lt;";
			else if (ch == '>') o += "&gt;";
			else if (ch == '"') o += "&quot;";
			else o += ch;
		}
		return o;
	};
	auto clock = [](float t) {
		char buf[16];
		snprintf(buf, sizeof(buf), "%d:%02d", (int)t / 60, (int)t % 60);
		return std::string(buf);
	};
	auto hexOf = [&](uint32_t p) {
		const uint32_t c = in.colour ? in.colour(p) : 0xFFFFFFFF;
		char buf[8];
		snprintf(buf, sizeof(buf), "#%02x%02x%02x", (unsigned)(c & 0xFF), (unsigned)((c >> 8) & 0xFF), (unsigned)((c >> 16) & 0xFF));
		return std::string(buf);
	};
	auto nameOf = [&](uint32_t p) { return in.name ? in.name(p) : std::to_string(p); };
	// a player's name: bold in Markdown, in its colour on the web page; a unit's name in italics
	auto player = [&](uint32_t p) {
		return html ? "<span class=\"p\" style=\"color:" + hexOf(p) + "\">" + esc(nameOf(p)) + "</span>" : "**" + nameOf(p) + "**";
	};
	auto unit = [&](const std::string& n) { return html ? "<em>" + esc(n) + "</em>" : "*" + n + "*"; };

	std::vector<const GameChronicle::Event*> events;
	std::set<uint32_t> players;
	if (in.chronicle)
		for (const GameChronicle::Event& e : in.chronicle->events)
			if (in.all || e.seen) {
				events.push_back(&e);
				players.insert(e.players.begin(), e.players.end());
			}
	std::string out;
	if (html)
		out += "<!doctype html>\n<html lang=\"en\"><head><meta charset=\"utf-8\">"
			"<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
			"<title>Chronicle</title><style>"
			"body{background:#241c14;color:#e6dcc4;font:17px/1.55 Georgia,'Times New Roman',serif;max-width:46em;margin:2em auto;padding:0 16px}"
			"h1,h2{color:#edd18c;font-weight:normal}h2{margin:1.6em 0 .1em}.date{color:#9c917c;font-size:.9em}"
			".p{font-weight:bold}em{color:#f0e6c8}ul{color:#c9bea6;font-size:.92em;padding-left:1.3em}.note{color:#9c917c}"
			"</style></head><body>\n<h1>Chronicle</h1>\n";
	else
		out += "# Chronicle\n\n";
	const std::string note = "Exported from wkbai on " + when + ", " + std::to_string(events.size()) + (events.size() == 1 ? " event" : " events")
		+ (in.all ? "." : ", as seen by " + in.viewer + ".");
	out += html ? "<p class=\"note\">" + esc(note) + "</p>\n" : "_" + note + "_\n\n";
	if (!players.empty()) {
		std::string list;
		for (uint32_t p : players)
			list += (list.empty() ? "" : ", ") + player(p);
		out += html ? "<p>Players: " + list + "</p>\n" : "Players: " + list + "\n\n";
	}
	for (const GameChronicle::Event* ep : events) {
		const GameChronicle::Event& e = *ep;
		const std::string date = ChronicleNarrator::dateLine(e.time, in.context);
		out += html ? "<h2>" + esc(e.title()) + "</h2>\n<div class=\"date\">" + esc(date) + "</div>\n<p>"
			: "## " + e.title() + "\n\n_" + date + "_\n\n";
		// the chronicle's paragraph
		for (const ChronicleNarrator::Piece& piece : ChronicleNarrator::narrate(e, in.context)) {
			if (piece.player)
				out += player(piece.player);
			else if (piece.mention >= 0)
				out += unit(piece.text);
			else
				out += esc(piece.text);
		}
		out += html ? "</p>\n<ul>\n" : "\n\n";
		// the Statistics view's numbers
		std::vector<std::string> items;
		std::string first = "At " + clock(e.time);
		if (e.size)
			first += ", " + std::to_string(e.size) + " fighters, lasting " + clock(e.endTime - e.time);
		items.push_back(esc(first));
		for (uint32_t p : e.players) {
			std::string side = player(p);
			if (auto it = e.strength.find(p); it != e.strength.end())
				side += ": " + std::to_string(it->second) + " fighting";
			if (auto it = e.lost.find(p); it != e.lost.end()) {
				int total = 0;
				std::string types;
				for (const auto& [type, n] : it->second) {
					total += n;
					types += (types.empty() ? "" : ", ") + std::to_string(n) + " " + type;
				}
				side += ", lost " + std::to_string(total) + " (" + esc(types) + ")";
			}
			items.push_back(side);
		}
		static const std::pair<int, const char*> kinds[] = {
			{ GameChronicle::MENTION_TOP_KILLER, "Most kills" }, { GameChronicle::MENTION_RAZED_BY, "Razed by" },
			{ GameChronicle::MENTION_PRESENT, "Present" }, { GameChronicle::MENTION_FALLEN, "Fallen" } };
		for (const auto& [kind, label] : kinds) {
			std::string line;
			for (const GameChronicle::Mention& m : e.mentions) {
				if (m.kind != kind)
					continue;
				const std::string kills = std::to_string(m.kills) + (m.kills == 1 ? " kill" : " kills");
				std::string one;
				if (kind == GameChronicle::MENTION_TOP_KILLER)
					one = player(m.player) + "'s " + unit(m.name) + " (" + std::to_string(m.kills) + ")";
				else if (kind == GameChronicle::MENTION_PRESENT)
					one = std::string(m.dead ? "the late " : "") + unit(m.name) + " (" + kills + ")";
				else if (kind == GameChronicle::MENTION_FALLEN)
					one = unit(m.name) + " (" + kills + ")" + (m.extra.empty() ? std::string() : ", " + esc(m.extra));
				else
					one = std::string(m.dead ? "the late " : "") + player(m.player) + "'s " + unit(m.name);
				line += (line.empty() ? "" : (kind == GameChronicle::MENTION_FALLEN ? "; " : ", ")) + one;
			}
			if (!line.empty())
				items.push_back(std::string(label) + ": " + line);
		}
		for (const std::string& item : items)
			out += html ? "<li>" + item + "</li>\n" : "- " + item + "\n";
		out += html ? "</ul>\n" : "\n";
	}
	if (events.empty()) {
		const std::string none = in.all ? "Nothing has happened yet." : "Nothing seen yet.";
		out += html ? "<p>" + none + "</p>\n" : none + "\n";
	}
	if (html)
		out += "</body></html>\n";
	return out;
}

std::filesystem::path TimelineExport::write(const Input& in, bool html)
{
	const std::time_t now = std::time(nullptr);
	std::tm local {};
	localtime_s(&local, &now);
	char stamp[32], when[32];
	std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H-%M-%S", &local);
	std::strftime(when, sizeof(when), "%Y-%m-%d %H:%M", &local);
	const std::string out = text(in, html, when);
	std::error_code ec;
	const std::filesystem::path dir = g_userPath / "chronicles";
	std::filesystem::create_directories(dir, ec);
	const std::filesystem::path file = dir / PathFromUtf8(std::string("Chronicle ") + stamp + (html ? ".html" : ".md"));
	FILE* f = nullptr;
	if (_wfopen_s(&f, file.c_str(), L"wb") != 0 || !f)
		return std::filesystem::path();
	const bool ok = fwrite(out.data(), 1, out.size(), f) == out.size();
	fclose(f);
	return ok ? file : std::filesystem::path();
}
