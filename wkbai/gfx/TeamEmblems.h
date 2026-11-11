// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#pragma once

// Team colours and emblems of the HD models (docs/engine/gltf.md "Team colours of HD models").
// The original models switch team colour by UV list (ent->color % the mesh's UV lists: a strip of
// their atlas per colour); an HD model's material instead says "extras": { "wkTeam": true } (its
// albedo times the owner's colour from TeamColours) or "wkEmblem": true (the owner's emblem laid
// over it through its TEXCOORD_1, from TeamEmblems).

#include <array>
#include <cstdint>
#include <string>
#include <vector>

struct Bitmap;

namespace TeamColours {
	static constexpr int COUNT = 8;
	// The original's 8 team colours, as its UV lists pick them: list k is colour k (ent->color
	// % 8, so Gaia's 0 and player 8 share list 0). Read off Barracks.tga's row of 8 shields (the
	// painted quarter of each, x 32k+4..10, y 229..235, the median), the same order as banners.tga's
	// 8 columns. sRGB, as the shaders take the albedo.
	extern const uint8_t table[COUNT][3];
	// a scene entity's colour (SceneEntity::color: the player's colour index) -> its row
	inline int index(int color) { return ((color % COUNT) + COUNT) % COUNT; }
}

// The 8 emblem slots: SIZE x SIZE RGBA8 each (straight alpha), the default of slot k banners.tga's
// column k (the emblem area of the banner), or the user folder's override
// "saved\Warrior Kings Game Set\Emblems\Emblem<k>.png" (or .tga). A renderer keeps them in a
// texture array (a slice per slot) and uploads a slot again when its generation changes.
class TeamEmblems
{
public:
	static constexpr int SLOTS = TeamColours::COUNT;
	static constexpr int SIZE = 256;

	static TeamEmblems& get();

	// Slots not loaded yet: the defaults and the user folder's overrides (once; needs the game
	// files, so the first renderer to draw calls it)
	void loadDefaults();
	bool loaded() const { return _loaded; }

	// The run-time entry point for a player's own emblem (the user folder's now; later the lobby
	// sends each player's, saves and replays keep them): the image fit into the slot (its aspect
	// kept, centred, transparent around). False if the slot or the image is unusable.
	bool setEmblem(int slot, const Bitmap& image);
	// back to the default (banners.tga's column)
	void resetEmblem(int slot);

	const uint8_t* pixels(int slot) const { return _pixels[slot].data(); }
	uint32_t generation(int slot) const { return _generation[slot]; }

	// the default emblem of a slot, from the banner texture's pixels (RGBA8, w x h): its column's
	// emblem area, fit into the slot (exposed for the test)
	static std::vector<uint8_t> cutBanner(const uint8_t* rgba, int w, int h, int slot);
	// an RGBA8 image (w x h) fit into a slot: scaled (bilinear) to fit, aspect kept, centred
	static std::vector<uint8_t> fit(const uint8_t* rgba, int w, int h);

private:
	std::array<std::vector<uint8_t>, SLOTS> _pixels, _defaults;
	std::array<uint32_t, SLOTS> _generation = {};
	bool _loaded = false;
	void setSlot(int slot, std::vector<uint8_t> px);
};
