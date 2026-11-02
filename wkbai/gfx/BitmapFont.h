// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#pragma once

#include <array>
#include <string>
#include <string_view>
#include "renderer.h"

struct ImDrawList;

// The original's bitmap fonts (Fonts\<name>\<size>\*.font): a text file naming the glyph strips
// (TGA images of glyphs side by side, separated by fully transparent columns) and their first
// character code, the line height, the gap between glyphs, the width of a space, and per-glyph
// advance corrections ("advance_x <code> <delta>"). The glyphs are Windows-1252 characters, as in
// Language.txt; text is UTF-8 (as wkbre2 converts Language.txt).
struct BitmapFont {
	struct Glyph {
		texture tex = nullptr;
		float u0 = 0, v0 = 0, u1 = 0, v1 = 0;
		int width = 0, advance = 0;
	};
	std::array<Glyph, 256> glyphs;
	int height = 0, glyphGap = 0, spaceSize = 0;

	bool load(IRenderer* gfx, const std::string& fontFile);
	// Width of the text's longest line, in pixels at scale 1
	float measure(std::string_view text) const;
	// Draws the text with its top-left corner at (x, y); lines are separated by '\n'
	void draw(ImDrawList* list, float x, float y, std::string_view text, uint32_t colour, float scale = 1.0f) const;
};
