#include <cmath>
#include <algorithm>
// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#include "bitmap.h"
#include "../file.h"
#include "../util/util.h"
#include "../util/StriCompare.h"
#include <cstdint>

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#define STBIR_DEFAULT_FILTER_DOWNSAMPLE  STBIR_FILTER_BOX
#include <stb_image_resize2.h>
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#include <stb_image.h>

Bitmap Bitmap::loadBitmap(const char *fn)
{
	char *d; int s;
	const char *e = strrchr(fn, '.');
	LoadFile(fn, &d, &s);
	if(!StrCICompare(e, ".tga"))
		return loadTGA(d, s);
	if(!StrCICompare(e, ".pcx"))
		return loadPCX(d, s);
	if (!StrCICompare(e, ".png") || !StrCICompare(e, ".jpg") || !StrCICompare(e, ".jpeg"))
		return loadImage(d, s);
	if (!StrCICompare(e, ".dds"))
		return loadDDS(d, s);
	ferr("Unknown bitmap file extension.");
	return {};
}

// A BC1 colour block (8 bytes) into 4 x 4 RGBA texels; fourColours: BC2/BC3's colour part,
// which has no transparent mode
static void DecodeBC1Block(const uint8_t* b, uint8_t out[16][4], bool fourColours)
{
	const uint16_t c0 = b[0] | (b[1] << 8), c1 = b[2] | (b[3] << 8);
	auto expand = [](uint16_t c, uint8_t* o) {
		o[0] = (uint8_t)(((c >> 11) & 31) * 255 / 31);
		o[1] = (uint8_t)(((c >> 5) & 63) * 255 / 63);
		o[2] = (uint8_t)((c & 31) * 255 / 31);
		o[3] = 255;
	};
	uint8_t pal[4][4];
	expand(c0, pal[0]);
	expand(c1, pal[1]);
	for (int k = 0; k < 3; k++) {
		if (c0 > c1 || fourColours) {
			pal[2][k] = (uint8_t)((2 * pal[0][k] + pal[1][k]) / 3);
			pal[3][k] = (uint8_t)((pal[0][k] + 2 * pal[1][k]) / 3);
		}
		else {
			pal[2][k] = (uint8_t)((pal[0][k] + pal[1][k]) / 2);
			pal[3][k] = 0;
		}
	}
	pal[2][3] = 255;
	pal[3][3] = (c0 > c1 || fourColours) ? 255 : 0;
	const uint32_t idx = b[4] | (b[5] << 8) | (b[6] << 16) | ((uint32_t)b[7] << 24);
	for (int i = 0; i < 16; i++)
		memcpy(out[i], pal[(idx >> (2 * i)) & 3], 4);
}

// A BC4 block (BC3's alpha, BC4/BC5's channels: 8 bytes) into 16 values
static void DecodeBC4Block(const uint8_t* b, uint8_t out[16])
{
	uint8_t v[8];
	v[0] = b[0];
	v[1] = b[1];
	if (v[0] > v[1])
		for (int i = 1; i < 7; i++)
			v[i + 1] = (uint8_t)(((7 - i) * v[0] + i * v[1]) / 7);
	else {
		for (int i = 1; i < 5; i++)
			v[i + 1] = (uint8_t)(((5 - i) * v[0] + i * v[1]) / 5);
		v[6] = 0;
		v[7] = 255;
	}
	uint64_t bits = 0;
	for (int i = 0; i < 6; i++)
		bits |= (uint64_t)b[2 + i] << (8 * i);
	for (int i = 0; i < 16; i++)
		out[i] = v[(bits >> (3 * i)) & 7];
}

Bitmap Bitmap::loadDDS(const void* data, size_t length)
{
	const uint8_t* p = (const uint8_t*)data;
	auto u32 = [&](size_t off) { return off + 4 <= length ? (uint32_t)(p[off] | (p[off + 1] << 8) | (p[off + 2] << 16) | ((uint32_t)p[off + 3] << 24)) : 0u; };
	if (length < 128 || memcmp(p, "DDS ", 4) != 0)
		ferr("Not a DDS file.");
	const int height = (int)u32(12), width = (int)u32(16);
	const uint32_t pfFlags = u32(80), fourCC = u32(84), bits = u32(88);
	const uint32_t rMask = u32(92), aMask = u32(104);
	size_t off = 128;
	auto cc = [](const char* s) { return (uint32_t)(uint8_t)s[0] | ((uint32_t)(uint8_t)s[1] << 8) | ((uint32_t)(uint8_t)s[2] << 16) | ((uint32_t)(uint8_t)s[3] << 24); };
	int bc = 0; // 0: uncompressed, 1-5: BCn
	bool bgra = false;
	if (pfFlags & 4) { // a FourCC
		if (fourCC == cc("DXT1")) bc = 1;
		else if (fourCC == cc("DXT2") || fourCC == cc("DXT3")) bc = 2;
		else if (fourCC == cc("DXT4") || fourCC == cc("DXT5")) bc = 3;
		else if (fourCC == cc("ATI1") || fourCC == cc("BC4U")) bc = 4;
		else if (fourCC == cc("ATI2") || fourCC == cc("BC5U")) bc = 5;
		else if (fourCC == cc("DX10")) {
			const uint32_t dxgi = u32(128);
			off += 20;
			if (dxgi == 70 || dxgi == 71 || dxgi == 72) bc = 1;
			else if (dxgi == 73 || dxgi == 74 || dxgi == 75) bc = 2;
			else if (dxgi == 76 || dxgi == 77 || dxgi == 78) bc = 3;
			else if (dxgi == 79 || dxgi == 80) bc = 4;
			else if (dxgi == 82 || dxgi == 83) bc = 5;
			else if (dxgi == 27 || dxgi == 28 || dxgi == 29) bc = 0;
			else if (dxgi == 87 || dxgi == 88 || dxgi == 90 || dxgi == 91) bgra = true;
			else
				ferr("DDS: DXGI format %u is not supported (BC1-BC5 and 8-bit RGBA/BGRA are).", dxgi);
		}
		else
			ferr("DDS: this FourCC is not supported (DXT1-5, ATI1/2 and DX10 BC1-BC5 are).");
	}
	else {
		if (bits != 32)
			ferr("DDS: only 32-bit uncompressed textures are supported.");
		bgra = rMask == 0x00FF0000;
	}
	Bitmap bm;
	bm.width = width;
	bm.height = height;
	bm.format = BMFORMAT_R8G8B8A8;
	bm.pixels.resize((size_t)width * height * 4);
	uint8_t* dst = bm.pixels.data();
	if (bc == 0) {
		if (off + (size_t)width * height * 4 > length)
			ferr("DDS: the file is too short.");
		for (size_t i = 0; i < (size_t)width * height; i++) {
			const uint8_t* s = p + off + i * 4;
			dst[i * 4 + 0] = bgra ? s[2] : s[0];
			dst[i * 4 + 1] = s[1];
			dst[i * 4 + 2] = bgra ? s[0] : s[2];
			dst[i * 4 + 3] = (aMask || (pfFlags & 4)) ? s[3] : 255;
		}
		return bm;
	}
	const size_t blockBytes = (bc == 1 || bc == 4) ? 8 : 16;
	const int bw = (width + 3) / 4, bh = (height + 3) / 4;
	if (off + (size_t)bw * bh * blockBytes > length)
		ferr("DDS: the file is too short.");
	for (int by = 0; by < bh; by++)
		for (int bx = 0; bx < bw; bx++) {
			const uint8_t* b = p + off + ((size_t)by * bw + bx) * blockBytes;
			uint8_t texel[16][4];
			uint8_t a[16], g[16];
			switch (bc) {
			case 1: DecodeBC1Block(b, texel, false); break;
			case 2:
				DecodeBC1Block(b + 8, texel, true);
				for (int i = 0; i < 16; i++)
					texel[i][3] = (uint8_t)(((b[i / 2] >> (4 * (i & 1))) & 15) * 17);
				break;
			case 3:
				DecodeBC1Block(b + 8, texel, true);
				DecodeBC4Block(b, a);
				for (int i = 0; i < 16; i++)
					texel[i][3] = a[i];
				break;
			case 4:
				DecodeBC4Block(b, a);
				for (int i = 0; i < 16; i++)
					texel[i][0] = texel[i][1] = texel[i][2] = a[i], texel[i][3] = 255;
				break;
			case 5: // two channels of a normal map: z from x and y
				DecodeBC4Block(b, a);
				DecodeBC4Block(b + 8, g);
				for (int i = 0; i < 16; i++) {
					const float x = a[i] / 127.5f - 1, y = g[i] / 127.5f - 1;
					const float z = std::sqrt(std::max(0.0f, 1 - x * x - y * y));
					texel[i][0] = a[i];
					texel[i][1] = g[i];
					texel[i][2] = (uint8_t)std::clamp((z * 0.5f + 0.5f) * 255.0f, 0.0f, 255.0f);
					texel[i][3] = 255;
				}
				break;
			}
			for (int i = 0; i < 16; i++) {
				const int x = bx * 4 + (i & 3), y = by * 4 + (i >> 2);
				if (x < width && y < height)
					memcpy(dst + ((size_t)y * width + x) * 4, texel[i], 4);
			}
		}
	return bm;
}

// PNG and JPEG (the usual images of glTF models), as RGBA
Bitmap Bitmap::loadImage(const void* data, size_t length)
{
	Bitmap bm;
	int w = 0, h = 0, channels = 0;
	stbi_uc* pixels = stbi_load_from_memory((const stbi_uc*)data, (int)length, &w, &h, &channels, 4);
	if (!pixels)
		ferr("Could not read an image: %s", stbi_failure_reason());
	bm.width = w;
	bm.height = h;
	bm.format = BMFORMAT_R8G8B8A8;
	bm.pixels.resize((size_t)w * h * 4);
	memcpy(bm.pixels.data(), pixels, (size_t)w * h * 4);
	stbi_image_free(pixels);
	return bm;
}

Bitmap Bitmap::loadTGA(const void* _data, size_t length)
{
	Bitmap bm;
	const char* data = static_cast<const char*>(_data);
	bm.width = *(uint16_t*)(data+12);
	bm.height = *(uint16_t*)(data+14);
	if(data[1] && ((data[2]&7) == 1) && (data[7] == 24) && (data[16] == 8))
		bm.format = BMFORMAT_P8;
	else if(((data[2]&7) == 2) && (data[16] == 24))
		bm.format = BMFORMAT_B8G8R8;
	else if(((data[2]&7) == 2) && (data[16] == 32))
		bm.format = BMFORMAT_B8G8R8A8;
	else
		ferr("Unknown TGA image format.");
	const char *dp = data + 18 + (uint8_t)data[0];

	// 24-bits palette: BB GG RR    (!)
	// 24-bits bitmap:  BB GG RR    (!)
	// 32-bits bitmap:  BB GG RR AA (!)
	if(data[1])
	{
		bm.palette.resize(768);
		for(int i = 0; i < 256; i++)
		{
			bm.palette[i*3+0] = dp[i*3+2];
			bm.palette[i*3+1] = dp[i*3+1];
			bm.palette[i*3+2] = dp[i*3+0];
		}
		dp += 768;
	}

	int e = data[16] / 8;
	e = ((data[16]&7)?(e+1):e);
	int s = bm.width * bm.height * e;
	bm.pixels.resize(s);
	if(data[2] & 8) // RLE
	{
		int x = 0, y = (data[17]&32) ? 0 : (bm.height - 1);
		const char *c = dp;
		while((y >= 0) || (y < bm.height))
		{
			uint8_t ua = *(c++);
			int us = ((uint8_t)ua & 127) + 1;

			for(int i = 0; i < us; i++)
			{
			 	for(int j = 0; j < e; j++)	
					bm.pixels[(y * bm.width + x) * e + j] = c[j];
				x++;
				if(!(ua & 128)) c += e;
				if(x >= bm.width)
				{
					x = 0;
					y += (data[17]&32) ? 1 : (-1);
					if((y < 0) || (y >= bm.height)) goto rleend;
				}
			}
			if(ua & 128) c += e;
		}
	rleend:	;
	}
	else
	{
		if(data[17] & 32)
			for(int y = 0; y < bm.height; y++)
				memcpy(bm.pixels.data() + (y * bm.width * e), dp + (y * bm.width * e), bm.width * e);
		else
			for(int y = 0; y < bm.height; y++)
				memcpy(bm.pixels.data() + ((bm.height-1-y) * bm.width * e), dp + (y * bm.width * e), bm.width * e);
	}

	return bm;
}

Bitmap Bitmap::loadPCX(const void* _data, size_t length)
{
	Bitmap bm;
	const char* data = static_cast<const char*>(_data);
	int nplanes = (uint8_t)data[65];
	if((uint8_t)data[3] != 8) ferr("PCX files that don't have 8 bits per channel are not supported.");
	bm.width = *((uint16_t*)&(data[8])) - *((uint16_t*)&(data[4])) + 1;
	bm.height = *((uint16_t*)&(data[10])) - *((uint16_t*)&(data[6])) + 1;
	bm.format = (nplanes==3) ? BMFORMAT_R8G8B8 : BMFORMAT_P8;
	bm.pixels.resize(bm.width * bm.height * nplanes);
	int pitch = (bm.width & 1) ? (bm.width + 1) : bm.width;

	uint8_t *f = (uint8_t*)data + 128, *o = bm.pixels.data(); int pp = 0;
	while(pp < (pitch * bm.height * nplanes))
	{
		int a = *f++, c;
		if(a >= 192)
			{c = a & 63; a = *f++;}
		else	c = 1;
		for(; c > 0; c--)
			{if(bm.width & 1)
				if( (pp % (bm.width+1)) == bm.width)	
					{pp++; continue;}
			if(bm.format == BMFORMAT_P8)
			{
				*o++ = a;
			/*
				int oy = pp / pitch;
				int ox = pp % pitch;
				bm.pixels[oy*bm.width + ox] = a;
			*/
			}
			else if(bm.format == BMFORMAT_R8G8B8)
			{
				int pl = (pp / pitch) % 3;
				int oy = (pp / pitch) / 3;
				int ox = pp % pitch;
				bm.pixels[(oy*bm.width+ox)*3+pl] = a;
			}
			pp++;}
	}

	bm.palette.resize(768);
	memcpy(bm.palette.data(), data + length - 768, 768);

	return bm;
}

Bitmap Bitmap::convertToR8G8B8A8() const
{
	Bitmap bm; const Bitmap* sb = this;
	bm.width = sb->width; bm.height = sb->height;
	bm.format = BMFORMAT_R8G8B8A8;
	int l = bm.width * bm.height;
	bm.pixels.resize(l * 4);
	if(sb->format == BMFORMAT_R8G8B8A8)
		memcpy(bm.pixels.data(), sb->pixels.data(), l * 4);
	else if(sb->format == BMFORMAT_P8)
	{
		for(int i = 0; i < l; i++)
			((uint32_t*)bm.pixels.data())[i] =	CM_RGBA(sb->palette[sb->pixels[i]*3+0],
							sb->palette[sb->pixels[i]*3+1],
							sb->palette[sb->pixels[i]*3+2],
							255);
	}
	else if(sb->format == BMFORMAT_R8G8B8)
	{
		for(int i = 0; i < l; i++)
			((uint32_t*)bm.pixels.data())[i] =	CM_RGBA(sb->pixels[i*3+0],
							sb->pixels[i*3+1],
							sb->pixels[i*3+2],
							255);
	}
	else if(sb->format == BMFORMAT_B8G8R8)
	{
		for(int i = 0; i < l; i++)
			((uint32_t*)bm.pixels.data())[i] =	CM_RGBA(sb->pixels[i*3+2],
							sb->pixels[i*3+1],
							sb->pixels[i*3+0],
							255);
	}
	else if(sb->format == BMFORMAT_B8G8R8A8)
	{
		for(int i = 0; i < l; i++)
			((uint32_t*)bm.pixels.data())[i] =	CM_RGBA(sb->pixels[i*4+2],
							sb->pixels[i*4+1],
							sb->pixels[i*4+0],
							sb->pixels[i*4+3]);
	}
	else	ferr("Failed to convert a bitmap to R8G8B8A8");

	return bm;
}

Bitmap Bitmap::convertToB8G8R8A8() const
{
	Bitmap bm; const Bitmap* sb = this;
	bm.width = sb->width; bm.height = sb->height;
	bm.format = BMFORMAT_B8G8R8A8;
	int l = bm.width * bm.height;
	bm.pixels.resize(l * 4);
	if(sb->format == BMFORMAT_B8G8R8A8)
		memcpy(bm.pixels.data(), sb->pixels.data(), l * 4);
	else if(sb->format == BMFORMAT_P8)
	{
		for(int i = 0; i < l; i++)
			((uint32_t*)bm.pixels.data())[i] =	CM_BGRA(sb->palette[sb->pixels[i]*3+0],
							sb->palette[sb->pixels[i]*3+1],
							sb->palette[sb->pixels[i]*3+2],
							255);
	}
	else if(sb->format == BMFORMAT_R8G8B8)
	{
		for(int i = 0; i < l; i++)
			((uint32_t*)bm.pixels.data())[i] =	CM_BGRA(sb->pixels[i*3+0],
							sb->pixels[i*3+1],
							sb->pixels[i*3+2],
							255);
	}
	else if(sb->format == BMFORMAT_B8G8R8)
	{
		for(int i = 0; i < l; i++)
			((uint32_t*)bm.pixels.data())[i] =	CM_BGRA(sb->pixels[i*3+2],
							sb->pixels[i*3+1],
							sb->pixels[i*3+0],
							255);
	}
	else if(sb->format == BMFORMAT_R8G8B8A8)
	{
		for(int i = 0; i < l; i++)
			((uint32_t*)bm.pixels.data())[i] =	CM_BGRA(sb->pixels[i*4+0],
							sb->pixels[i*4+1],
							sb->pixels[i*4+2],
							sb->pixels[i*4+3]);
	}
	else	ferr("Failed to convert a bitmap to B8G8R8A8");

	return bm;
}

Bitmap::~Bitmap() = default;

void Bitmap::blit32(int dx, int dy, const Bitmap& sb, int sx, int sy, int sw, int sh)
{
	for (int y = sy; y < (sy + sh); y++)
		for (int x = sx; x < (sx + sw); x++)
			((uint32_t*)pixels.data())[((y - sy + dy) * width + (x - sx + dx))] = ((uint32_t*)sb.pixels.data())[(y * sb.width + x)];
}

static void write8(FILE *f, uint8_t a) {fwrite(&a, 1, 1, f);}
static void write16(FILE *f, uint16_t a) {fwrite(&a, 2, 1, f);}
static void write32(FILE *f, uint32_t a) {fwrite(&a, 4, 1, f);}

static void WritePCXHeader(FILE *f, int w, int h, int np)
{
	int i;
	write8(f, 10); write8(f, 5); write8(f, 1); write8(f, 8);
	write32(f, 0);
	write16(f, w-1); write16(f, h-1);
	write16(f, w); write16(f, h);
	for(i = 0; i < 12; i++) write32(f, 0);
	write8(f, 0);
	write8(f, np);
	write16(f, w + (w&1));
	write16(f, 1);
	write16(f, 0);
	for(i = 0; i < 14; i++) write32(f, 0);
}

static void WritePCXData(FILE *f, const uint8_t *pix, int w, int h, int np)
{
	int y, p, x;
	for(y = 0; y < h; y++)
	for(p = 0; p < np; p++)
	{
		const uint8_t *sl = pix + y*w*np + p;
		const uint8_t *d = sl;
		uint8_t o; // = *d;
		int cnt = 1;
		//d += np;
		for(x = 0; x < w-1; x++)
		{
			o = *d;
			d += np;
			if((*d != o) || (cnt >= 63))
			{
				if((cnt > 1) || (o >= 0xC0))
					write8(f, 0xC0 | cnt);
				write8(f, o);
				cnt = 1;
			}
			else cnt++;
		}
		if((cnt > 1) || (*d >= 0xC0))
			write8(f, 0xC0 | cnt);
		write8(f, *d);
		if(w & 1) write8(f, 0);
	}
}

void Bitmap::savePCX(const char *fname) const
{
	FILE *f = fopen(fname, "wb"); if(!f) ferr("Cannot open \"%s\" for writing PCX file.", fname);
	switch(format)
	{
		case BMFORMAT_P8:
			WritePCXHeader(f, width, height, 1);
			WritePCXData(f, pixels.data(), width, height, 1);
			write8(f, 12);
			fwrite(palette.data(), 768, 1, f);
			break;
		case BMFORMAT_R8G8B8:
			WritePCXHeader(f, width, height, 3);
			WritePCXData(f, pixels.data(), width, height, 3);
			break;
		default:
			ferr("Bitmap::savePCX doesn't support format %i.", format);
	}
	fclose(f);
}

Bitmap Bitmap::resize(int nwidth, int nheight) const {
	Bitmap res;
	const Bitmap& original = *this;
	res.width = nwidth;
	res.height = nheight;
	res.format = original.format;
	int npitch = nwidth * 4;
	int totalSize = npitch * nheight;
	res.pixels.resize(totalSize);
	stbir_resize_uint8_linear(original.pixels.data(), original.width, original.height, original.width * 4,
		res.pixels.data(), res.width, res.height, npitch, STBIR_RGBA);
	return res;
}