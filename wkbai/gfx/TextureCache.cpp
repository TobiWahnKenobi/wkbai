// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#include "TextureCache.h"
#include "../file.h"
#include "renderer.h"
#include "bitmap.h"
#include <cstdio>
#include <cstdlib>
#include <algorithm>

texture TextureCache::loadTexture(const char * fn, int mipmaps)
{
	Bitmap bm;
	if (FileExists(fn))
		bm = Bitmap::loadBitmap(fn);
	else
		return nullptr; //bm = LoadBitmap("netexfb.tga");
	texture t = lazy ? gfx->CreateTextureLazy(bm, mipmaps) : gfx->CreateTexture(bm, mipmaps);
	return t;
}

texture TextureCache::createTexture(const char* filename, int mipmaps, const Bitmap& bmp)
{
	texture t = gfx->CreateTexture(bmp, mipmaps);
	loadedTextures[filename] = t;
	return t;
}

// "name#rrggbb.ext": a tinted copy of name.ext, each channel times byte/128 (0..2) in its sRGB
// value. HD models share one material library and match each building's original colours
// through tints; the base, normal, ORM and height maps stay single files on disk.
bool TextureCache::splitTint(const std::string& filename, std::string& base, float tint[3])
{
	const size_t hash = filename.rfind('#');
	const size_t dot = filename.find_last_of('.');
	if (hash == std::string::npos || dot == std::string::npos || dot != hash + 7)
		return false;
	for (int c = 0; c < 3; c++) {
		const std::string hex = filename.substr(hash + 1 + 2 * c, 2);
		char* end = nullptr;
		const long v = std::strtol(hex.c_str(), &end, 16);
		if (end != hex.c_str() + 2)
			return false;
		tint[c] = (float)v / 128.0f;
	}
	base = filename.substr(0, hash) + filename.substr(dot);
	return true;
}

texture TextureCache::getTexture(const char * filename, bool mipmaps) {
	auto it = loadedTextures.find(filename);
	if (it != loadedTextures.end())
		return it->second;
	std::string base;
	float tint[3];
	if (splitTint(filename, base, tint)) {
		const std::string path = Bitmap::replacementFile(directory + base, false);
		texture tex = nullptr;
		if (FileExists(path.c_str())) {
			Bitmap bm = Bitmap::loadBitmap(path.c_str());
			if (bm.format != BMFORMAT_R8G8B8A8)
				bm = bm.convertToR8G8B8A8();
			for (size_t i = 0; i + 3 < bm.pixels.size(); i += 4)
				for (int c = 0; c < 3; c++)
					bm.pixels[i + c] = (unsigned char)std::min(255.0f, bm.pixels[i + c] * tint[c] + 0.5f);
			tex = gfx->CreateTexture(bm, mipmaps ? 0 : 1);
		}
		loadedTextures[filename] = tex;
		return tex;
	}
	const std::string path = Bitmap::replacementFile(directory + filename, true); // a mod's .png/.dds/.ktx2 for a .tga/.pcx
	texture tex = loadTexture(path.c_str(), mipmaps ? 0 : 1); // todo
	loadedTextures[filename] = tex;
	if (tex)
		loadedFrom[filename] = { path, FileModificationTime(path.c_str()) };
	return tex;
}

int TextureCache::ReloadChangedTextures()
{
	int reloaded = 0;
	for (TextureCache* cache : allCaches())
		for (auto& [name, from] : cache->loadedFrom) {
			const uint64_t time = FileModificationTime(from.first.c_str());
			if (time == from.second || time == 0)
				continue;
			from.second = time;
			auto it = cache->loadedTextures.find(name);
			if (it == cache->loadedTextures.end() || !it->second)
				continue;
			Bitmap bm = Bitmap::loadBitmap(from.first.c_str());
			cache->gfx->UpdateTexture(it->second, bm);
			printf("Hot reload: texture %s\n", from.first.c_str());
			reloaded++;
		}
	return reloaded;
}

texture TextureCache::getTextureIfCached(const char* filename) const
{
	auto it = loadedTextures.find(filename);
	if (it != loadedTextures.end())
		return it->second;
	else
		return nullptr;
}
