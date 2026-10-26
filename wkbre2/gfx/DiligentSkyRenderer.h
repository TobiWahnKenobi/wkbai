// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#pragma once

struct IRenderer;
struct Terrain;
struct Camera;

// The original's sky: the cube of the map's sky_front/back/left/right/top.tga, faded into the
// fog colour near the horizon (see docs/re/sky-and-fog.md in the RE repository). Drawn behind
// everything at the start of the frame, on Diligent.
struct DiligentSkyRenderer {
	DiligentSkyRenderer(IRenderer* gfx, const Terrain* terrain, const Camera* camera);
	~DiligentSkyRenderer();
	void render();
private:
	struct Impl;
	Impl* _impl;
};
