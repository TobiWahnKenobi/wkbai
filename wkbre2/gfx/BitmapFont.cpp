// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#include "BitmapFont.h"
#include "bitmap.h"
#include "../file.h"
#include "../imgui/imgui.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <vector>

namespace {
	// ".\Fonts\Morpheus\20\33_126.tga" -> "Fonts\Morpheus\20\33_126.tga"
	std::string cleanPath(std::string path)
	{
		while (!path.empty() && (path.back() == '\r' || path.back() == ' '))
			path.pop_back();
		if (path.rfind(".\\", 0) == 0 || path.rfind("./", 0) == 0)
			path = path.substr(2);
		return path;
	}

	// The next character of UTF-8 text (wkbre2 converts Language.txt to UTF-8); a byte that
	// doesn't start valid UTF-8 counts as a Windows-1252 character. Characters past 255 have no
	// glyph in the original's fonts.
	unsigned nextChar(std::string_view text, size_t& i)
	{
		const unsigned char b = (unsigned char)text[i++];
		auto continuation = [&](size_t n) {
			if (i + n > text.size())
				return false;
			for (size_t k = 0; k < n; k++)
				if (((unsigned char)text[i + k] & 0xC0) != 0x80)
					return false;
			return true;
		};
		if (b < 0x80)
			return b;
		if ((b & 0xE0) == 0xC0 && continuation(1)) {
			const unsigned c = ((b & 0x1Fu) << 6) | ((unsigned char)text[i] & 0x3Fu);
			i += 1;
			return c;
		}
		if ((b & 0xF0) == 0xE0 && continuation(2)) { i += 2; return '?'; }
		if ((b & 0xF8) == 0xF0 && continuation(3)) { i += 3; return '?'; }
		return b;
	}
}

bool BitmapFont::load(IRenderer* gfx, const std::string& fontFile)
{
	if (!FileExists(fontFile.c_str())) {
		printf("WARNING: font \"%s\" not found\n", fontFile.c_str());
		return false;
	}
	char* data = nullptr;
	int size = 0;
	LoadFile(fontFile.c_str(), &data, &size, 1);
	data[size] = 0;
	std::istringstream in(data);
	free(data);

	std::vector<std::pair<std::string, int>> strips; // file, first glyph
	std::vector<std::pair<int, int>> advanceDeltas;
	std::string line;
	while (std::getline(in, line)) {
		std::istringstream ls(line);
		std::string key;
		ls >> key;
		if (key == "height") ls >> height;
		else if (key == "glyph_gap") ls >> glyphGap;
		else if (key == "space_size") ls >> spaceSize;
		else if (key == "source_file") {
			std::string rest;
			std::getline(ls, rest);
			rest.erase(0, rest.find_first_not_of(" \t"));
			strips.emplace_back(cleanPath(rest), 0);
		}
		else if (key == "first_glyph" && !strips.empty()) ls >> strips.back().second;
		else if (key == "advance_x") {
			int code = 0, delta = 0;
			ls >> code >> delta;
			advanceDeltas.emplace_back(code, delta);
		}
	}

	for (const auto& [file, first] : strips) {
		if (!FileExists(file.c_str())) {
			printf("WARNING: font image \"%s\" not found\n", file.c_str());
			continue;
		}
		Bitmap bmp = Bitmap::loadBitmap(file.c_str()).convertToR8G8B8A8();
		const uint8_t* px = bmp.pixels.data();
		auto columnEmpty = [&](int x) {
			for (int y = 0; y < bmp.height; y++)
				if (px[(y * bmp.width + x) * 4 + 3] != 0)
					return false;
			return true;
		};
		// The glyphs: runs of columns with any opaque pixel
		std::vector<std::pair<int, int>> runs; // start, width
		for (int x = 0; x < bmp.width;) {
			if (columnEmpty(x)) { x++; continue; }
			const int start = x;
			while (x < bmp.width && !columnEmpty(x)) x++;
			runs.emplace_back(start, x - start);
		}
		const int count = (first < 128) ? 126 - first + 1 : 255 - first + 1;
		// A glyph with a gap inside (such as '"') splits into two runs: join the closest pairs
		while ((int)runs.size() > count) {
			size_t best = 0;
			int bestGap = INT32_MAX;
			for (size_t i = 0; i + 1 < runs.size(); i++) {
				const int gap = runs[i + 1].first - (runs[i].first + runs[i].second);
				if (gap < bestGap) { bestGap = gap; best = i; }
			}
			runs[best].second = runs[best + 1].first + runs[best + 1].second - runs[best].first;
			runs.erase(runs.begin() + best + 1);
		}
		texture tex = gfx->CreateTexture(bmp, 1);
		for (size_t i = 0; i < runs.size() && first + (int)i < 256; i++) {
			Glyph& g = glyphs[first + i];
			g.tex = tex;
			g.u0 = (float)runs[i].first / bmp.width;
			g.u1 = (float)(runs[i].first + runs[i].second) / bmp.width;
			g.v0 = 0.0f;
			g.v1 = 1.0f;
			g.width = runs[i].second;
			g.advance = runs[i].second + glyphGap;
		}
		if (height == 0)
			height = bmp.height;
	}
	for (const auto& [code, delta] : advanceDeltas)
		if (code >= 0 && code < 256)
			glyphs[code].advance += delta;
	return true;
}

float BitmapFont::measure(std::string_view text) const
{
	float widest = 0.0f, x = 0.0f;
	for (size_t i = 0; i < text.size();) {
		const unsigned c = std::min(nextChar(text, i), 255u);
		if (c == '\n') { widest = std::max(widest, x); x = 0.0f; continue; }
		x += (c == ' ' || !glyphs[c].tex) ? (float)spaceSize : (float)glyphs[c].advance;
	}
	return std::max(widest, x);
}

void BitmapFont::draw(ImDrawList* list, float x, float y, std::string_view text, uint32_t colour, float scale) const
{
	const float startX = x;
	for (size_t i = 0; i < text.size();) {
		const unsigned c = std::min(nextChar(text, i), 255u);
		if (c == '\n') { x = startX; y += height * scale; continue; }
		const Glyph& g = glyphs[c];
		if (c == ' ' || !g.tex) { x += spaceSize * scale; continue; }
		list->AddImage((ImTextureID)g.tex, ImVec2(x, y), ImVec2(x + g.width * scale, y + height * scale), ImVec2(g.u0, g.v0), ImVec2(g.u1, g.v1), colour);
		x += g.advance * scale;
	}
}
