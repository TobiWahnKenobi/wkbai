// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#include "TeamEmblems.h"
#include "bitmap.h"
#include "../file.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

// Barracks.tga's shields, UV list 0-7 (see TeamEmblems.h): 0 brick red (Gaia, player 8), 1 blue,
// 2 yellow, 3 red, 4 green, 5 magenta, 6 orange, 7 cyan
const uint8_t TeamColours::table[COUNT][3] = {
	{ 184, 65, 49 }, { 50, 47, 231 }, { 252, 245, 4 }, { 249, 7, 41 },
	{ 3, 234, 64 }, { 255, 5, 203 }, { 255, 116, 67 }, { 10, 200, 238 },
};

namespace {
	// banners.tga (256 x 256): 8 banners side by side, 32 texels each, the upper half; the emblem
	// area of each, inside its border, at 256 x 256
	constexpr int BANNER_W = 32, EMBLEM_X0 = 1, EMBLEM_X1 = 31, EMBLEM_Y0 = 22, EMBLEM_Y1 = 106;
	const char* BANNER_FILE = "Warrior Kings Game Set\\Textures\\banners.tga";
	const char* EMBLEM_DIR = "Warrior Kings Game Set\\Emblems\\";
}

TeamEmblems& TeamEmblems::get()
{
	static TeamEmblems emblems;
	return emblems;
}

std::vector<uint8_t> TeamEmblems::fit(const uint8_t* src, int w, int h)
{
	std::vector<uint8_t> out((size_t)SIZE * SIZE * 4, 0);
	if (w <= 0 || h <= 0)
		return out;
	const float scale = std::min((float)SIZE / w, (float)SIZE / h);
	const int ow = std::max(1, (int)std::lround(w * scale)), oh = std::max(1, (int)std::lround(h * scale));
	const int x0 = (SIZE - ow) / 2, y0 = (SIZE - oh) / 2;
	for (int y = 0; y < oh; y++)
		for (int x = 0; x < ow; x++) {
			// bilinear, at the texel centres, premultiplied so that transparent texels don't bleed
			const float sx = std::clamp((x + 0.5f) / scale - 0.5f, 0.0f, (float)w - 1), sy = std::clamp((y + 0.5f) / scale - 0.5f, 0.0f, (float)h - 1);
			const int ix = (int)sx, iy = (int)sy, jx = std::min(ix + 1, w - 1), jy = std::min(iy + 1, h - 1);
			const float fx = sx - ix, fy = sy - iy;
			float acc[4] = {};
			const int xs[2] = { ix, jx }, ys[2] = { iy, jy };
			for (int b = 0; b < 2; b++)
				for (int a = 0; a < 2; a++) {
					const float wgt = (a ? fx : 1 - fx) * (b ? fy : 1 - fy);
					const uint8_t* p = src + ((size_t)ys[b] * w + xs[a]) * 4;
					const float al = p[3] / 255.0f;
					for (int c = 0; c < 3; c++)
						acc[c] += wgt * p[c] * al;
					acc[3] += wgt * al;
				}
			uint8_t* o = &out[((size_t)(y0 + y) * SIZE + x0 + x) * 4];
			for (int c = 0; c < 3; c++)
				o[c] = (uint8_t)std::lround(acc[3] > 0 ? std::min(255.0f, acc[c] / acc[3]) : 0.0f);
			o[3] = (uint8_t)std::lround(acc[3] * 255);
		}
	return out;
}

std::vector<uint8_t> TeamEmblems::cutBanner(const uint8_t* src, int w, int h, int slot)
{
	// (a banner texture of another size: the same places, scaled)
	const float sx = w / 256.0f, sy = h / 256.0f;
	const int x0 = (int)std::lround((slot * BANNER_W + EMBLEM_X0) * sx), x1 = (int)std::lround((slot * BANNER_W + EMBLEM_X1) * sx);
	const int y0 = (int)std::lround(EMBLEM_Y0 * sy), y1 = (int)std::lround(EMBLEM_Y1 * sy);
	if (x1 <= x0 || y1 <= y0 || x1 > w || y1 > h)
		return std::vector<uint8_t>((size_t)SIZE * SIZE * 4, 0);
	std::vector<uint8_t> cut((size_t)(x1 - x0) * (y1 - y0) * 4);
	for (int y = y0; y < y1; y++)
		std::copy_n(src + ((size_t)y * w + x0) * 4, (size_t)(x1 - x0) * 4, &cut[(size_t)(y - y0) * (x1 - x0) * 4]);
	return fit(cut.data(), x1 - x0, y1 - y0);
}

void TeamEmblems::setSlot(int slot, std::vector<uint8_t> px)
{
	_pixels[slot] = std::move(px);
	_generation[slot]++;
}

void TeamEmblems::loadDefaults()
{
	if (_loaded)
		return;
	_loaded = true;
	Bitmap banner;
	if (FileExists(BANNER_FILE))
		banner = Bitmap::loadBitmap(BANNER_FILE).convertToR8G8B8A8();
	for (int k = 0; k < SLOTS; k++) {
		_defaults[k] = banner.width > 0 ? cutBanner(banner.pixels.data(), banner.width, banner.height, k) : std::vector<uint8_t>((size_t)SIZE * SIZE * 4, 0);
		setSlot(k, _defaults[k]);
		// the user folder's (or a mod's) own emblem for the slot
		for (const char* ext : { ".png", ".tga" }) {
			const std::string name = std::string(EMBLEM_DIR) + "Emblem" + std::to_string(k) + ext;
			if (!FileExists(name.c_str()))
				continue;
			const Bitmap own = Bitmap::loadBitmap(name.c_str());
			if (setEmblem(k, own))
				printf("Team emblem %d: %s\n", k, name.c_str());
			break;
		}
	}
}

bool TeamEmblems::setEmblem(int slot, const Bitmap& image)
{
	if (slot < 0 || slot >= SLOTS || image.width <= 0 || image.height <= 0)
		return false;
	const Bitmap px = image.convertToR8G8B8A8();
	if (px.pixels.size() < (size_t)px.width * px.height * 4)
		return false;
	setSlot(slot, fit(px.pixels.data(), px.width, px.height));
	return true;
}

void TeamEmblems::resetEmblem(int slot)
{
	if (slot >= 0 && slot < SLOTS)
		setSlot(slot, _defaults[slot].empty() ? std::vector<uint8_t>((size_t)SIZE * SIZE * 4, 0) : _defaults[slot]);
}
