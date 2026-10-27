// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#pragma once

#include <functional>
#include <map>
#include <unordered_map>
#include <vector>
#include "util/vecmat.h"
#include "mesh.h"
#include "gfx/renderer.h"
#include "gfx/TextureCache.h"

struct Model;
struct ModelCache;

struct SceneEntity {
	Matrix transform;
	Model *model = nullptr;
	int color = 0;
	uint32_t animTime = 0;
	uint32_t flags = 0;

	enum Flags {
		SEFLAG_NOLIGHTING = 1, // disable light shading on entity
		SEFLAG_ANIM_CLAMP_END = 2, // clamp to last anim frame instead of repeating animation from the beginning
		SEFLAG_STATIC_SHADOW = 4, // casts the original's static shadow on the terrain (HAS_STATIC_SHADOW)
		SEFLAG_BLOCKED = 8, // a building to be placed where it can't stand: drawn red
		SEFLAG_PLACEABLE = 16, // a field to be placed where it can be: drawn green
	};
};

struct Scene {
	//struct MaterialLess {
	//	bool operator() (const Material &a, const Material &b) const {
	//		int r = StrCICompare(a.textureFilename.c_str(), b.textureFilename.c_str());
	//		if (r == 0) return a.alphaTest < b.alphaTest;
	//		return r < 0;
	//	}
	//};

	struct MaterialInst {
		std::vector<SceneEntity*> list;
		texture tex;
	};

	struct MaterialKeyHash {
		size_t operator()(const std::pair<uint32_t, Model*>& k) const {
			return std::hash<Model*>()(k.second) ^ ((size_t)k.first * 0x9E3779B97F4A7C15ull);
		}
	};
	// a hash map: added to for every drawn object each frame
	std::unordered_map<std::pair<uint32_t, Model*>, MaterialInst, MaterialKeyHash> matInsts;
	TextureCache texCache;
	ModelCache *modelCache;

	Vector3 sunDirection = Vector3(1,1,1);
	uint32_t sunColor = 0xFFFFFF; // 0xRRGGBB

	void add(SceneEntity *ent);
	// Loads the model and its materials' textures without drawing it
	void prewarm(Model* model);
	void clear();

	Scene(IRenderer *gfx, ModelCache *modelCache) : texCache(gfx, "Warrior Kings Game Set\\Textures\\"), modelCache(modelCache) {}
};