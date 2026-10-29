// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#pragma once

// Render and camera settings that can change while the game runs (the temporary settings
// window, F10). Loaded from wkconfig.json at startup; the renderers read them every frame.
// The defaults are the original game's values where it has them.
struct RenderSettings {
	// View distance and fog (docs/re/sky-and-fog.md)
	float farClip = 350.0f;          // the original's far_clip; "farClip": 0 in wkconfig.json
	bool farClipWholeMap = false;    // view distance: the whole map
	float fogStart = 0.5f;           // fog start as a fraction of the fog's end
	bool fogOff = false;             // no distance fog at all
	bool fogCustom = false;          // own fog distances instead of the original's (from the view distance)
	float fogNear = 165.75f, fogFar = 331.5f; // the original's values at far_clip 350; far <= view distance
	// Map-edge fog: by distance outside the playable area, opaque before the map mesh ends
	bool edgeFog = true;
	// fractions of the band between playable and mesh edge; the start may lie inside the playable area
	float edgeFogStart = -0.25f, edgeFogOpaque = 0.5f;
	bool fogColourOverride = false;  // instead of the map's fog colour
	float fogColour[3] = { 159 / 255.0f, 197 / 255.0f, 230 / 255.0f };
	float fovYDegrees = 51.566f;     // 0.9 rad, as wkbre2 always had

	// Colour grading of the 3D scene (not the HUD)
	float exposure = 1.0f, brightness = 0.0f, contrast = 1.0f, saturation = 1.0f;

	// The original's sky, shadows and model lighting
	bool sky = true, cloudShadows = true, staticShadows = true;
	bool waterSurface = true;        // textured, rippled water with a sun glint; off: flat fog colour
	bool bumpMapping = true;         // the terrain's Dot3 bump mapping
	// the terrain's materials blended at the tile corners instead of the original's painted tiles
	// (enhanced renderer on Diligent; gfx/EnhancedTerrainData.h)
	bool terrainSplat = false;
	bool characterShadows = true;    // the units' blob shadows
	// the units drawn into the sun's shadow map instead (their real shape; enhanced renderers with
	// building shadows on): the original only has the blobs
	bool realCharacterShadows = true;
	// walls and palisades (SHEAR_MODEL buildings) cast shadows too: the game set gives them
	// HAS_STATIC_SHADOW 0, so the original has none
	bool wallShadows = true;
	// post-processing of the 3D scene (enhanced graphics on Diligent): ambient occlusion and bloom
	bool ssao = true, bloom = true;
	float ssaoRadius = 2.5f, ssaoStrength = 0.7f;      // world units; 0-1
	float bloomThreshold = 0.72f, bloomIntensity = 0.35f;
	bool vsync = true;               // "vsync" in wkconfig.json, applied on the next frame
	float staticShadowLight = 0xB4 / 255.0f;
	float meshAmbient = 97.0f / 255.0f, meshDiffuse = 200.0f / 256.0f;
	// models lit per pixel with the sun's highlight (enhanced renderer on Diligent; the
	// original lights them per vertex, without highlights)
	bool modelLighting = false;
	float modelSpecular = 1.0f;

	// Camera
	float scrollSpeed = 1.0f;        // WASD/arrows, multiplier
	float shiftScrollFactor = 3.0f;  // holding Shift
	float mouseLookSensitivity = 0.01f; // radians per pixel, middle mouse button
	float minHeightAboveGround = 3.0f;
	float maxHeight = 1000.0f;

	bool invertMouseWheel = false;

	// Sound, 0..1
	float musicVolume = 1.0f, effectsVolume = 1.0f;

	bool showFps = false;            // FPS counter in the top-left corner

	void load();  // from g_settings
	void save();  // into g_settings and wkconfig.json
	float fovY() const { return fovYDegrees * 3.14159265f / 180.0f; }
};

extern RenderSettings g_renderSettings;
