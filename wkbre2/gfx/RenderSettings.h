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
	float edgeFogStart = 0.0f, edgeFogOpaque = 0.8f; // fractions of the band between playable and mesh edge
	bool fogColourOverride = false;  // instead of the map's fog colour
	float fogColour[3] = { 159 / 255.0f, 197 / 255.0f, 230 / 255.0f };
	float fovYDegrees = 51.566f;     // 0.9 rad, as wkbre2 always had

	// Colour grading of the 3D scene (not the HUD)
	float exposure = 1.0f, brightness = 0.0f, contrast = 1.0f, saturation = 1.0f;

	// The original's sky, shadows and model lighting
	bool sky = true, cloudShadows = true, staticShadows = true;
	float staticShadowLight = 0xB4 / 255.0f;
	float meshAmbient = 97.0f / 255.0f, meshDiffuse = 200.0f / 256.0f;

	// Camera
	float scrollSpeed = 1.0f;        // WASD/arrows, multiplier
	float shiftScrollFactor = 3.0f;  // holding Shift
	float mouseLookSensitivity = 0.01f; // radians per pixel, middle mouse button
	float minHeightAboveGround = 3.0f;
	float maxHeight = 1000.0f;

	void load();  // from g_settings
	void save();  // into g_settings and wkconfig.json
	float fovY() const { return fovYDegrees * 3.14159265f / 180.0f; }
};

extern RenderSettings g_renderSettings;
