// wkbai - the in-game help: a manual of Markdown pages, opened for whatever is on the screen
// Licensed under the GNU General Public License 3
//
// The pages are redata/help/<language>/*.md (the format: redata/help/README.md; the design: the
// RE repo's docs/engine/help-system.md). Every part of the interface is tagged with help ids
// through agent control's UI registry (AgentUi): a right-click on it (Alt+right-click where a
// right-click already does something), F1 over it, or a click in "What's this?" mode (Shift+F1)
// opens its page.

#pragma once

#include <string>

struct IRenderer;
struct ImFontAtlas;
struct ImFont;
struct GameSet;
struct Language;

namespace Help {

// The help's fonts into ImGui's atlas, before it is built (imguiimpl.cpp)
void AddFonts(ImFontAtlas* atlas);

// After the input of a frame is read (window.cpp HandleWindow): F1, Shift+F1, the right-click
// that opens the help menu; the clicks the help takes are taken away from the game and ImGui
void AfterInput();

// The help window, its menu and "What's this?", drawn last in the frame (ImGuiImpl_Render)
void Frame(IRenderer* gfx);

// Opens the help at a help id, or the first of several ("a|b|c") that has a page; the nearest
// parent's page when none has one
void Open(const std::string& ids);
bool IsOpen();

// The screen being shown, as a help id (F1 over nothing opens it): the front end sets its screen
// each frame, the game "ui/hud"; reset at each frame's start
void SetScreen(const std::string& id);

// The game set being played (its blueprints' names and numbers for the pages' facts tables), and
// the game's Language.txt: set by the game interface, nullptr when it ends
void SetGame(const GameSet* gameSet, const Language* language);

// The object under the cursor in the 3D world, as help ids ("unit/Knight"): F1 and
// Alt+right-click on it; set by the game interface each frame
void SetHovered(const std::string& ids);

// Tests: the pages of a language checked (every link, id and anchor); the problems found
std::string Check(const std::string& language);

}
