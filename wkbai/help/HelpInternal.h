// wkbai - the in-game help: what the library (HelpLibrary.cpp), the viewer (HelpView.cpp) and the
// glue (Help.cpp) share
// Licensed under the GNU General Public License 3

#pragma once

#include <map>
#include <string>
#include <unordered_map>
#include <vector>

struct IRenderer;
struct GameSet;
struct Language;
struct ImFont;

namespace Help::detail {

// ---- the pages (HelpLibrary.cpp) ----

struct Page {
	std::string path;            // "ui/hud/minimap" (no ".md")
	std::string title;           // the title shown (the translation's, else the English one)
	std::string body;            // the Markdown after the front matter, in `lang`
	std::string lang;            // the language the body is in
	enum State { ORIGINAL, TRANSLATED, OUT_OF_DATE, ENGLISH } state = ORIGINAL;
	double order = 1000;
	std::string parent;          // "parent:" in the front matter, else ""
	std::vector<std::string> anchors;
	std::string plainText;       // the body without Markdown, for the search's snippets
	std::string searchText;      // plainText in lower case, for the search
	std::string searchTitle;     // lower-case title
	std::string searchHeadings;  // lower-case headings
	std::string gameName;        // the game's own name for the page's unit/building/command, if any
};

struct Hit {
	std::string path, anchor, snippet;
	int score = 0;
};

class Library {
public:
	// (re)loads the pages of a language ("" or "auto": the one that matches the game); the
	// game's Language.txt for the matching (may be nullptr)
	void load(const std::string& language, const Language* game);
	bool loaded() const { return !pages.empty(); }

	// the first of "a|b|c" that names a page (with its anchor); the nearest parent's page when
	// none does. exact: whether one of them had a page of its own.
	bool resolve(const std::string& ids, std::string& path, std::string& anchor, bool* exact = nullptr) const;
	const Page* page(const std::string& path) const;
	const std::map<std::string, Page>& all() const { return pages; }
	std::vector<Hit> search(const std::string& query) const;

	// the help window's texts in the help's language ("contents", ...); {0}, {1} filled in
	std::string ui(const std::string& key, const std::string& a = {}, const std::string& b = {}) const;
	const std::string& language() const { return lang; }
	// the languages there are: code, own name
	const std::vector<std::pair<std::string, std::string>>& languages() const { return langs; }

	// the game's names for the pages of units, buildings, upgrades and commands (a game set loaded)
	void setGameNames(const GameSet* gs, const Language* game);

	// every link, id and anchor checked; the problems, one per line
	std::string check() const;

private:
	std::string lang;
	std::vector<std::pair<std::string, std::string>> langs;
	std::map<std::string, Page> pages;
	// help id -> page path, anchor
	std::unordered_map<std::string, std::pair<std::string, std::string>> ids;
	std::map<std::string, std::string> uiTexts;
	bool resolveOne(const std::string& id, std::string& path, std::string& anchor) const;
};

Library& Lib();

// "../hud/minimap.md#zoom" from "ui/frontend/main": "ui/hud/minimap", "zoom"; false: not a page link
bool PageLink(const std::string& from, const std::string& url, std::string& path, std::string& anchor);
std::string Slug(const std::string& text);
// reads a file of the game's file system (redata included) as text; false: none
bool ReadText(const std::string& path, std::string& out);

// ---- the viewer (HelpView.cpp) ----

enum FontKind { F_BODY, F_BOLD, F_ITALIC, F_CODE, F_H1, F_H2, F_H3, F_COUNT };
ImFont* Font(int kind);

struct ViewState {
	std::string path, anchor;     // the page shown and where to scroll (consumed)
	double flashUntil = 0;        // the heading scrolled to is highlighted until then
	std::string flashAnchor;
};

// Draws the page into the current ImGui child window; calls back for a clicked link
struct LinkAction {
	enum Kind { NONE, PAGE, ID, SHOW, WEB } kind = NONE;
	std::string a, b;             // PAGE: path, anchor; ID: ids; SHOW: id; WEB: url
};
LinkAction DrawPage(IRenderer* gfx, const Page& page, ViewState& view, const GameSet* gs, const Language* game);
// forgets the parsed pages and pictures (after a language change)
void ClearViewCache();

}
