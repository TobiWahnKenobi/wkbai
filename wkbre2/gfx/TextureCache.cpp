// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#include "TextureCache.h"
#include "../file.h"
#include "renderer.h"
#include "bitmap.h"
#include <cstdio>

texture TextureCache::loadTexture(const char * fn, int mipmaps)
{
	Bitmap bm;
	if (FileExists(fn))
		bm = Bitmap::loadBitmap(fn);
	else
		return nullptr; //bm = LoadBitmap("netexfb.tga");
	texture t = gfx->CreateTexture(bm, mipmaps);
	return t;
}

texture TextureCache::createTexture(const char* filename, int mipmaps, const Bitmap& bmp)
{
	texture t = gfx->CreateTexture(bmp, mipmaps);
	loadedTextures[filename] = t;
	return t;
}

texture TextureCache::getTexture(const char * filename, bool mipmaps) {
	auto it = loadedTextures.find(filename);
	if (it != loadedTextures.end())
		return it->second;
	const std::string path = directory + filename;
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
