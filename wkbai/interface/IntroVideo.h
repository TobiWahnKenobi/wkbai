// wkbai - the original's intro video (docs/engine/front-end.md)
// Licensed under the GNU General Public License 3

#pragma once

#include <filesystem>

struct IRenderer;

// WKB.exe plays "Warrior Kings Battles FMV.wmv" from the game folder before the main menu
// (0x6ab216, through DirectShow). wkbai plays it the same way with Windows' Media Foundation:
// the picture letterboxed on a black screen, the sound through OpenAL; Esc, Space, Enter or a
// click skips it. Nothing happens when the file isn't there (a renamed one, "...wmv.no", is
// the player's way of switching it off), when "introVideo" is false in the settings, or when
// Media Foundation can't decode it.
// file: the video to play (empty: the game folder's)
void PlayIntroVideo(IRenderer* gfx, const std::filesystem::path& file = {});
