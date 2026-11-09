// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#pragma once

#include "TerrainRenderer.h"
#include "../ExtendMap.h"

struct TextureCache;
namespace Diligent { struct ITextureView; }

// Bump-mapped terrain with per-pixel sun light, and lakes, on Diligent (Direct3D 12 / Vulkan);
// a port of the former D3D11EnhancedTerrainRenderer.
struct DiligentEnhancedTerrainRenderer : TerrainRenderer {
	// extendTiles: wkbai's Extend map (ExtendMap.h): the renderer draws a bigger copy of the map
	// with that many tiles more on each side (0: the map itself), kept in step with it
	DiligentEnhancedTerrainRenderer(IRenderer* gfx, Terrain* terrain, Camera* camera, int extendTiles = 0);
	~DiligentEnhancedTerrainRenderer();
	void init();
	void render() override;
	// The depth pre-pass (DiligentGfx::LightingPrepassWanted): the frame's preparation and the
	// ground's depth alone into the lighting depth (dsv; tessellated more coarsely: the full
	// tessellation a second time cost 1.75 ms, the flat ground lost the rocks' occlusion and
	// shadowed itself where the relief rises above it), the next render() without preparing again;
	// then the water's surfaces into it
	void renderDepth(Diligent::ITextureView* dsv);
	void renderWaterDepth();
	bool updateHeights() override;
	bool updateTiles() override;
	bool updateWater() override;
	bool* bumpMapping() override { return &m_bumpOn; }
	int extendedTiles() const override;

	bool m_bumpOn = true;
private:
	struct Impl;
	Impl* _impl;
	TextureCache* texcache;
	Terrain* mapTerrain; // the map's own (the client's, maybe shared with a local server)
	ExtendedTerrain extended; // its copy drawn, with Extend map (else null: terrain is the map's)
};
