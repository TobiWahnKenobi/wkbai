// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#pragma once

#include "TerrainRenderer.h"

struct TextureCache;

// Bump-mapped terrain with per-pixel sun light, and lakes, on Diligent (Direct3D 12 / Vulkan);
// a port of the former D3D11EnhancedTerrainRenderer.
struct DiligentEnhancedTerrainRenderer : TerrainRenderer {
	DiligentEnhancedTerrainRenderer(IRenderer* gfx, Terrain* terrain, Camera* camera) : TerrainRenderer(gfx, terrain, camera) { init(); }
	~DiligentEnhancedTerrainRenderer();
	void init();
	void render() override;
	bool* bumpMapping() override { return &m_bumpOn; }

	bool m_bumpOn = true;
private:
	struct Impl;
	Impl* _impl;
	TextureCache* texcache;
};
