// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#pragma once

#include <string>
#include <map>
#include <set>
#include "renderer.h"

struct Bitmap;

struct TextureCache
{
	IRenderer *gfx;
	std::map<std::string, texture> loadedTextures;
	std::string directory;
	// the file and its timestamp each texture was loaded from (hot reload)
	std::map<std::string, std::pair<std::string, uint64_t>> loadedFrom;

	texture loadTexture(const char *fn, int mipmaps);
	texture createTexture(const char* filename, int mipmaps, const Bitmap &bmp);

	texture getTexture(const char *filename, bool mipmaps = true);
	texture getTextureIfCached(const char* filename) const;

	// Hot reload: textures whose file changed since (a mod's or a loose file being edited) get the
	// new picture, in place, so everything that holds them shows it. Same size only (else a
	// restart). Every cache; returns how many were reloaded.
	static int ReloadChangedTextures();

	TextureCache(IRenderer *gfx, const std::string &directory = "") : gfx(gfx), directory(directory) { allCaches().insert(this); }
	~TextureCache() { allCaches().erase(this); }
	TextureCache(const TextureCache&) = delete;
	TextureCache& operator=(const TextureCache&) = delete;

private:
	static std::set<TextureCache*>& allCaches() { static std::set<TextureCache*> caches; return caches; }
};
