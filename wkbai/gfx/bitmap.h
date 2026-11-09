// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#pragma once

#include "../util/DynArray.h"
#include <vector>
#include <string>

#define BMFORMAT_P8 1
#define BMFORMAT_R8G8B8 2
#define BMFORMAT_B8G8R8 3
#define BMFORMAT_R8G8B8A8 4
#define BMFORMAT_B8G8R8A8 5
// Block-compressed (DDS, KTX2), kept so for the GPU (the renderer's CreateTexture uploads them
// as they are, with the file's mip levels): pixels holds every level, levels their byte
// offsets. convertToR8G8B8A8 decodes the largest level for what needs the pixels (BC7 with
// bcdec). BC4 is a single channel (drawn grey), BC5 two (a normal map's
// x and y; the shaders rebuild z).
#define BMFORMAT_BC7 6
#define BMFORMAT_BC1 7
#define BMFORMAT_BC2 8
#define BMFORMAT_BC3 9
#define BMFORMAT_BC4 10
#define BMFORMAT_BC5 11
#define CM_BGRA(r, g, b, a) ( (b) | ((g)<<8) | ((r)<<16) | ((a)<<24) )
#define CM_RGBA(r, g, b, a) ( (r) | ((g)<<8) | ((b)<<16) | ((a)<<24) )

struct Bitmap
{
	int width, height, format; DynArray<unsigned char> pixels, palette;
	std::vector<size_t> levels; // block-compressed: the byte offset of each mip level in pixels
	// the BCn kind (1-5, 7) of a block-compressed bitmap, else 0
	int bcKind() const { return format == BMFORMAT_BC7 ? 7 : (format >= BMFORMAT_BC1 && format <= BMFORMAT_BC5) ? format - BMFORMAT_BC1 + 1 : 0; }
	bool compressed() const { return bcKind() != 0; }
	static int bcFormat(int bc) { return bc == 7 ? BMFORMAT_BC7 : BMFORMAT_BC1 + bc - 1; }

	Bitmap() : width(0), height(0), format(0) {}
	~Bitmap();

	// Loads by extension; an original .tga or .pcx with a replacement (replacementFile) loads that
	static Bitmap loadBitmap(const char* fn);
	// A mod's replacement for an original .tga or .pcx: the same name as .png, or with gpu first
	// .ktx2 or .dds (which may be BC7, so only for textures going to the GPU as they are);
	// else fn itself. High-resolution texture packs use it (mod format 2).
	static std::string replacementFile(const std::string& fn, bool gpu = false);
	static Bitmap loadTGA(const void* data, size_t length);
	static Bitmap loadPCX(const void* data, size_t length);
	static Bitmap loadImage(const void* data, size_t length); // PNG, JPEG
	// DDS: uncompressed RGBA/BGRA (8 bits a channel), the largest mipmap, as RGBA; BC1-BC5
	// (DXT1/3/5, ATI1/2) and BC7 kept compressed with every level (a size that isn't a
	// multiple of 4: decoded, BC1-BC5)
	static Bitmap loadDDS(const void* data, size_t length);
	// KTX2 without supercompression: 8-bit RGBA/BGRA (the largest level, as RGBA), BC1-BC5 and
	// BC7 kept compressed as DDS
	static Bitmap loadKTX2(const void* data, size_t length);

	Bitmap convertToR8G8B8A8() const;
	Bitmap convertToB8G8R8A8() const;
	void blit32(int dx, int dy, const Bitmap& sb, int sx, int sy, int sw, int sh);
	void savePCX(const char* fname) const;
	Bitmap resize(int nwidth, int nheight) const;
};
