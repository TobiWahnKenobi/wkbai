// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#pragma once

// whether the terrain is drawn tessellated this frame (the setting is on and the GPU can):
// the client draws objects on the smooth surface then (Terrain::getSmoothHeight)
extern bool g_terrainTessellationActive;
// the enhanced terrain renderer drew wkbai's flood water this frame in its water shader
// (EnvironmentView's DrawGroundWeather then draws no flat sheet of its own; it clears this)
extern bool g_terrainDrawsFlood;

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
	// the sky without the original's sky box (the volumetric clouds' physical sky): 0 Normal; 1
	// Physical: the earth's shadow after sunset, ozone's deep blue, the horizon's haze in the
	// sky's own colours (warm towards a low sun), a lower horizon band
	int skyModel = 1;
	// the water (WATER_*; saved as "waterSetting"): 0 Original (the original's two scrolling
	// layers of the map's water texture and nothing else), 1 Off (a plain colour), 2 Very low (the
	// texture with bump-mapped ripples and a sun glint), 3 Low (with the sky reflected, deeper
	// water darker, cloud shadows, shore foam, separate river and sea looks, short screen-space
	// reflections), 4 Medium (High's ocean look on the flat surface, longer reflections), 5 High
	// (the surface tessellated on the GPU and moved by waves, foam on the crests and in the surf;
	// Medium without a GPU that can tessellate), 6 Very high (between High and Ultra), 7 Ultra
	// (finer tessellation farther out, the longest reflections). Original comes first, so
	// "from Low up" is waterQuality >= WATER_LOW; only Off and Original are below Very low.
	enum { WATER_ORIGINAL = 0, WATER_OFF, WATER_VERYLOW, WATER_LOW, WATER_MEDIUM, WATER_HIGH, WATER_VERYHIGH, WATER_ULTRA, WATER_LEVELS };
	int waterQuality = WATER_HIGH;
	// the waterfalls and the cascades of painted rivers (DiligentWaterfalls), apart from the
	// water: 0 off, 1 Low, 2 Medium, 3 High, 4 Ultra (High's refraction needs the water from Low)
	int waterfalls = 3;
	bool waterFlow = true;          // from Low: rivers flowing downstream (a flow map from the map's water)
	// fires (burning buildings, flaming arrows, fire spells) light the terrain and models around
	// them, up to 16 at once (enhanced renderers on Diligent)
	bool fireLights = true;
	// wkbai's day and night, seasons and weather, how much of it is drawn: 0 Low (a third of the
	// rain and snow), 1 Medium (two thirds; snow on roofs and models, wet models darker), 2 High
	// (all of it; footprints in snow and mud), 3 Ultra (half as much rain and snow again, more
	// footprints that last longer). The flood water, the ground's snow and ice and the light of
	// day and night are drawn at every level: they are part of the game.
	int weatherEffects = 2;
	// grass blades on green ground near the camera (enhanced renderers on Diligent): blades per
	// square unit (0 to TerrainGrassBuilder::MAX_DENSITY), their height (times 0.4-0.85 units)
	// and how far from the camera they are drawn
	// the terrain subdivided on the GPU into a smooth surface through its height points (0 off,
	// 1-3: up to 4, 8, 16 pieces per tile edge near the camera); objects are drawn on that surface
	// (the game's own heights don't change). Only with a GPU that can tessellate.
	int terrainTessellation = 2;
	// wkbai's Extend map (ExtendMap.h): the land continued beyond the map's edge, drawn only (the
	// game never sees it). 0 off, 1 near (64 tiles beyond the mesh's edge), 2 far (and coarser
	// rings to about 3.6 km beyond), 3 horizon (to about 30 km); not in the map editor
	int extendMap = 0;
	// its detail (Low, Medium, High, Ultra): the first far ring's grid 20, 10, 5 or 2.5 units
	// (each ring twice as coarse as the one inside it); Ultra takes longer to build
	int extendMapDetail = 1;
	// the two as the terrain is built with them: changed only by OK in the Options menu, Apply in
	// the F10 panel and loading (building takes seconds, and switching through the choices rebuilt
	// it at each one, the play test of 2026-11-11)
	// the far land's terrain shadows: each far vertex's horizon in 8 directions, worked out as the
	// far land is built (a little longer to build), the sun against it soft-edged as drawn
	bool extendMapShadows = true;
	int appliedExtendMap = 0, appliedExtendMapDetail = 1;
	bool appliedExtendMapShadows = true;
	void applyExtendMap() { appliedExtendMap = extendMap; appliedExtendMapDetail = extendMapDetail; appliedExtendMapShadows = extendMapShadows; }
	bool grass = true;
	float grassDensity = 3.0f, grassHeight = 1.0f, grassDistance = 80.0f;
	// camera motion blur (post-processing): the shutter, 0 off to 1 (the whole frame's motion)
	float motionBlur = 0.0f;
	// tilt-shift (post-processing): the map as a model, sharp across the middle, 0 off to 1
	float tiltShift = 0.0f;
	bool bumpMapping = true;         // the terrain's Dot3 bump mapping
	// the terrain's light vector as the original's: in the slope's tangent frame, but at least
	// 1/3 up, so slopes facing away from the sun stay lit
	bool terrainOriginalLight = true;
	bool characterShadows = true;    // the units' blob shadows
	// the units drawn into the sun's shadow map instead (their real shape; enhanced renderers with
	// building shadows on): the original only has the blobs
	bool realCharacterShadows = true;
	// walls and palisades (SHEAR_MODEL buildings) cast shadows too: the game set gives them
	// HAS_STATIC_SHADOW 0, so the original has none
	bool wallShadows = true;
	// post-processing of the 3D scene (enhanced graphics on Diligent): ambient occlusion and bloom
	// fog of war (the game option Clear Fog Of War off, not a graphics setting): what none of the
	// player's units sees is darker (the original's), by this much
	float fogOfWarBrightness = 0.55f;
	// the lower left panel's stained-glass window as refracting glass (enhanced graphics)
	bool stainedGlass = true;
	bool ssao = true, bloom = true;
	float ssaoRadius = 2.5f, ssaoStrength = 0.7f;      // world units; 0-1
	float bloomThreshold = 0.72f, bloomIntensity = 0.35f;
	// anti-aliasing of the 3D scene: 0 off, 1 FXAA (a pass after the post-processing; works with
	// any renderer, forward or deferred, unlike MSAA, which the water's scene copy can't use),
	// 2 TAA (the camera jittered, the frames blended through their depth; smoother edges and
	// no shimmer, slightly softer, sharpened after), 3 AMD FSR 3.1 at the window's resolution
	// (its temporal anti-aliasing, with motion vectors: the best edges and the least smearing)
	int antiAliasing = 1;
	// upscaling of the 3D scene (enhanced graphics on Diligent): 0 off (the window's resolution),
	// 1 Ultra quality (77%), 2 Quality (67%), 3 Balanced (59%), 4 Performance (50% of the width
	// and height); the HUD stays sharp at the window's size. By upscaler: 0 AMD FSR 1 (spatial,
	// after the anti-aliasing), 1 AMD FSR 3.1 (temporal: anti-aliases and upscales together,
	// replacing the anti-aliasing setting; sharper and steadier)
	int upscaling = 0;
	int upscaler = 1;
	float sharpness = 0.8f;          // FSR's sharpening (and TAA's): 0 soft to 1 sharpest
	float renderScale() const;       // the scene's size by the upscaling, of the window's
	// whether FSR 3.1 renders the scene: as the anti-aliasing, or as the upscaler
	bool fsr3() const { return antiAliasing == 3 || (upscaling > 0 && upscaler == 1); }
	// the window: 0 windowed, 1 borderless fullscreen (the desktop's resolution), 2 fullscreen
	// (the display switched to the desktop's mode); applied at once (window.cpp)
	int displayMode = 0;
	// anisotropic filtering of the terrain's, water's and models' textures: 1 (off) to 16;
	// applied to renderers made after the change (the next map)
	int anisotropy = 16;
	bool vsync = true;               // "vsync" in wkconfig.json, applied on the next frame
	float staticShadowLight = 0xB4 / 255.0f;
	// static shadow detail: 1 = the original's 4 x 4 points per tile on a 2048² sun shadow map,
	// 2 = 8 x 8 on 4096², 4 = 16 x 16 on 4096² (the map's size takes effect on the next map)
	int shadowResolution = 1;
	float meshAmbient = 97.0f / 255.0f, meshDiffuse = 200.0f / 256.0f;
	// models lit per pixel with the sun's highlight (enhanced renderer on Diligent; the
	// original lights them per vertex, without highlights)
	bool modelLighting = false;
	float modelSpecular = 1.0f;
	// with per-pixel lighting: the strength of the normals derived from the brightness of a
	// texture without a <texture>_normal map (PBR legacy adapter); 0 = flat
	float modelDetailNormals = 2.0f;
	// with per-pixel lighting: the ambient and reflections take the sky box's colours
	bool skyLight = true;
	// global illumination from the sky (enhanced renderers on Diligent): the terrain and the
	// models lit by the sky box's colours as much as they see of the sky (the terrain's and the
	// buildings' horizon around the camera), plus the sun's light bounced off the ground where
	// the sky is hidden; instead of the original's minimum light on slopes away from the sun
	bool skyGI = true;
	// dynamic global illumination (enhanced renderers on Diligent, with skyGI; DiligentGI): the
	// sun's, the moon's, the sky's and the fires' light bounced off the ground, the buildings
	// and (High) the units onto what is around them, in the colour of what it bounced off: a
	// radiosity map of the ground and the roofs seen from above, around the camera. 0 Off (the
	// sky light's plain bounce), 1 Low (the buildings, one bounce), 2 High (the units too, a
	// second bounce, a wider gather)
	int dynamicGI = 0;
	float dynamicGIStrength = 1.0f; // the bounce's strength (0-3)
	// dynamic clouds (enhanced renderers on Diligent): 0 the original's (sky_shadow_map.pcx
	// scrolling over the ground), 1 a cloud field drifting with the wind whose shadows follow the
	// sun, 2 with light shafts through the air between the shadows, 3 with the clouds themselves
	// raymarched in the sky (lit through their depth, silver linings towards the sun)
	int dynamicClouds = 3;
	// the volumetric clouds' quality: 0 Low (a quarter of the size, 48 steps), 1 Medium (half,
	// 64), 2 High (the full size, 96), 3 Ultra (the full size, 128, finer light)
	int cloudQuality = 1;
	// the volumetric clouds' detail (their shapes, beside the quality's resolution and steps): 0 Low
	// (the layer's one density, as before 2026-11-09), 1 Medium (the density varying by region
	// within the layer: thick and thin clouds, a deck with denser and thinner parts), 2 High (with
	// finer billows on the tops, the sides bulging with the height, and the cumulonimbus as towers on
	// a flat base under wide anvils); Medium and High round the tops (no spikes while clouds form)
	int cloudDetail = 1;
	// the clouds' weather (enhanced renderers on Diligent): 0 cumulus by the cloud cover below,
	// 1-10 a weather's clouds (clear, cloudy, rain, heavy rain, thunderstorm, fog, snow, blizzard,
	// heatwave, drought); the seasons' weather, when on, sets it itself
	int cloudWeather = 0;
	float cloudCover = 0.45f;        // 0 clear to 1 overcast
	float lightShaftStrength = 1.0f; // the light shafts' brightness
	// ray-traced sun shadows of the buildings, trees and units (enhanced renderers on Diligent,
	// with a GPU that has inline ray tracing): instead of the static shadow map
	bool rtShadows = false;
	// contact shadows (enhanced renderers on Diligent, with the post-processing effects): a short
	// march towards the sun in the depth pre-pass's depth, the small shadows where units' feet,
	// fences and walls meet the ground (with or without the ray-traced shadows)
	bool contactShadows = true;
	// ray-traced reflections of the buildings, trees and units in the water (the same; water Low
	// or better), over the screen-space reflections, which still show the terrain
	bool rtReflections = false;
	// the ground's Fresnel (enhanced terrain renderer on Diligent): its reflection of the sky
	// at grazing angles, as a rough dielectric's (Schlick, F0 0.04) times this; 0 off. The
	// models' per-pixel lighting (modelLighting) has its own Fresnel, PBR packs or not.
	float terrainFresnel = 0.35f;

	// Camera
	float scrollSpeed = 1.0f;        // WASD/arrows, multiplier
	float shiftScrollFactor = 3.0f;  // holding Shift
	float mouseLookSensitivity = 0.01f; // radians per pixel, middle mouse button
	float minHeightAboveGround = 3.0f;
	float maxHeight = 1000.0f;

	bool invertMouseWheel = false;
	// The original's two cameras (WKB.exe's "enable_mouse_free_look", off by default): the fixed
	// one keeps its height above the ground and a pitch set by the zoom, the middle mouse button
	// turning it only round; the free one (false) looks anywhere
	bool fixedCamera = false;

	// Sound, 0..1
	float musicVolume = 1.0f, effectsVolume = 1.0f;

	bool showFps = false;            // FPS counter in the top-left corner

	void load();  // from g_settings
	void save();  // into g_settings and wkconfig.json
	float fovY() const { return fovYDegrees * 3.14159265f / 180.0f; }
};

extern RenderSettings g_renderSettings;
