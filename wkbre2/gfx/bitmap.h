// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#pragma once

#include "../util/DynArray.h"
#include <vector>

#define BMFORMAT_P8 1
#define BMFORMAT_R8G8B8 2
#define BMFORMAT_B8G8R8 3
#define BMFORMAT_R8G8B8A8 4
#define BMFORMAT_B8G8R8A8 5
// BC7 blocks, kept compressed for the GPU (the renderer's CreateTexture uploads them as they
// are; there is no CPU decoder): pixels holds every mip level, levels their byte offsets
#define BMFORMAT_BC7 6
#define CM_BGRA(r, g, b, a) ( (b) | ((g)<<8) | ((r)<<16) | ((a)<<24) )
#define CM_RGBA(r, g, b, a) ( (r) | ((g)<<8) | ((b)<<16) | ((a)<<24) )

struct Bitmap
{
	int width, height, format; DynArray<unsigned char> pixels, palette;
	std::vector<size_t> levels; // BMFORMAT_BC7: the byte offset of each mip level in pixels

	Bitmap() : width(0), height(0), format(0) {}
	~Bitmap();

	static Bitmap loadBitmap(const char* fn);
	static Bitmap loadTGA(const void* data, size_t length);
	static Bitmap loadPCX(const void* data, size_t length);
	static Bitmap loadImage(const void* data, size_t length); // PNG, JPEG
	// DDS: uncompressed RGBA/BGRA (8 bits a channel) and BC1-BC5 (DXT1/3/5, ATI1/2), the
	// largest mipmap, as RGBA; BC5 as a normal map (z rebuilt)
	static Bitmap loadDDS(const void* data, size_t length);
	// KTX2 without supercompression: 8-bit RGBA/BGRA and BC1-BC5 (the largest level, as RGBA),
	// BC7 (compressed, every level, see BMFORMAT_BC7)
	static Bitmap loadKTX2(const void* data, size_t length);

	Bitmap convertToR8G8B8A8() const;
	Bitmap convertToB8G8R8A8() const;
	void blit32(int dx, int dy, const Bitmap& sb, int sx, int sy, int sw, int sh);
	void savePCX(const char* fname) const;
	Bitmap resize(int nwidth, int nheight) const;
};
