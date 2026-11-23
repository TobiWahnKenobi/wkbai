// wkbai - generated ground transitions: tile pictures for pairs of ground groups the game's texture
// set has no transition for (grass to rock, sand to hillside...), made on the player's own machine
// from the game's own pictures (never shipped: they are the game's art), so the map compiler's
// ground solver can put the two side by side instead of a strip of a third ground between them.
//
// The pairs come from the materials data file of a texture database ("generatedTransitions" in
// Maps\Materials\All_Textures.json: { "version": n, "pairs": [ [ "GRASS1", "ROCK1" ], ... ] }).
// Each pair becomes a texture group "<A>-<B>" of 8 pictures (one, two side by side, two across
// and three corners of B, two variants each) in Generated\<A>-<B>.dds (BC7 with every mip level,
// gfx/Bc7Encode.h; the size of the largest source: an HD pack's 1024 pixels an atlas) and
// <A>-<B>_BumpMap.png (8-bit heights, 256 pixels, as the game's bump maps), in the user folder's
// saved\Maps\Map_Textures (the file system finds them there after the archives).
// They are made again when the generator's version, the pair list's version or a source picture
// (the HD pack switched on or off) changes; the same inputs give the same pictures everywhere.
// Each picture carries its corners' groups (TerrainTexture::genCorners): the solver needs no edge
// rules for them. The RE repo's docs/engine/map-authoring.md, "Generated transitions".

#pragma once

#include <string>

struct TerrainTextureDatabase;

namespace TransitionGen {
	constexpr int VERSION = 2; // 2: a soft ramp, the noise close to the border, a grain on the edge
	// adds the generated groups to db (dbFile: its game path, "Maps\Map_Textures\All_Textures.dat"),
	// making their pictures first where the cache lacks them; a pair whose groups db doesn't have,
	// or that has a transition already, is left out. Returns the number of groups added.
	int Add(TerrainTextureDatabase& db, const std::string& dbFile);
}
