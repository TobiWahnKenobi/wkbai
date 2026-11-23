// wkbai - a small BC7 encoder (modes 6 and 1, the better per block: one subset with 4-bit indices,
// or two subsets in one of BC7's 64 shapes with 3-bit indices) for pictures wkbai makes itself
// (the generated ground transitions, TransitionGen.h), and a DDS writer for them with every mip
// level. wkbai's own code (GPL-3); the decoder that checks it is bcdec (util/bcdec.h). Opaque
// only: alpha comes out 254 or 255 (mode 6) or 255 (mode 1).

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Bc7 {
	// one 4 x 4 block of RGBA (row by row, 64 bytes) into 16 bytes
	void EncodeBlock(const uint8_t rgba[64], uint8_t out[16]);
	// an RGBA picture (width and height multiples of 4) into its blocks, row by row
	std::vector<uint8_t> Encode(const uint8_t* rgba, int width, int height);
	// a DDS file (DX10 header, BC7_UNORM) with the picture and its mip levels down to 4 x 4 (a box
	// filter, in linear light when srgb); false when it can't be written
	bool WriteDds(const std::string& path, const uint8_t* rgba, int width, int height, bool srgb = true);
}
