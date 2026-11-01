// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#include "RenderSettings.h"

bool g_terrainTessellationActive = false;
#include "../settings.h"
#include <nlohmann/json.hpp>

RenderSettings g_renderSettings;

float RenderSettings::renderScale() const
{
	// FSR 1's presets: 1.3, 1.5, 1.7 and 2 times smaller per side
	static const float scales[5] = { 1.0f, 1.0f / 1.3f, 1.0f / 1.5f, 1.0f / 1.7f, 0.5f };
	return scales[std::clamp(upscaling, 0, 4)];
}

void RenderSettings::load()
{
	const RenderSettings def;
	const float far = g_settings.value<float>("farClip", def.farClip);
	farClipWholeMap = far <= 0.0f;
	farClip = farClipWholeMap ? def.farClip : far;
	fogStart = g_settings.value<float>("fogStart", def.fogStart);
	fogOff = g_settings.value<bool>("fogOff", def.fogOff);
	fogCustom = g_settings.value<bool>("fogCustom", def.fogCustom);
	fogNear = g_settings.value<float>("fogNear", def.fogNear);
	fogFar = g_settings.value<float>("fogFar", def.fogFar);
	edgeFog = g_settings.value<bool>("edgeFog", def.edgeFog);
	edgeFogStart = g_settings.value<float>("edgeFogStart", def.edgeFogStart);
	edgeFogOpaque = g_settings.value<float>("edgeFogOpaque", def.edgeFogOpaque);
	const auto fog = g_settings.value("fogColourOverride", nlohmann::json());
	fogColourOverride = fog.is_array() && fog.size() == 3;
	if (fogColourOverride)
		for (int i = 0; i < 3; i++)
			fogColour[i] = fog[i].get<float>();
	fovYDegrees = g_settings.value<float>("fieldOfView", def.fovYDegrees);
	exposure = g_settings.value<float>("exposure", def.exposure);
	brightness = g_settings.value<float>("brightness", def.brightness);
	contrast = g_settings.value<float>("contrast", def.contrast);
	saturation = g_settings.value<float>("saturation", def.saturation);
	sky = g_settings.value<bool>("sky", def.sky);
	cloudShadows = g_settings.value<bool>("cloudShadows", def.cloudShadows);
	// "waterLevel" (six levels), or from the older "waterQuality" (Off, Original, Low, High) and
	// the switches "waterSurface" and "waterDetail" before it
	if (g_settings.contains("waterLevel"))
		waterQuality = std::clamp(g_settings.value<int>("waterLevel", def.waterQuality), 0, 5);
	else if (g_settings.contains("waterQuality")) {
		static const int fromFour[4] = { 0, 1, 2, 4 };
		waterQuality = fromFour[std::clamp(g_settings.value<int>("waterQuality", 3), 0, 3)];
	}
	else if (g_settings.contains("waterSurface") || g_settings.contains("waterDetail"))
		waterQuality = !g_settings.value<bool>("waterSurface", true) ? 0 : !g_settings.value<bool>("waterDetail", true) ? 1 : def.waterQuality;
	else
		waterQuality = def.waterQuality;
	waterFlow = g_settings.value<bool>("waterFlow", def.waterFlow);
	fireLights = g_settings.value<bool>("fireLights", def.fireLights);
	grass = g_settings.value<bool>("grass", def.grass);
	terrainTessellation = std::clamp(g_settings.value<int>("terrainTessellation", def.terrainTessellation), 0, 3);
	grassDensity = g_settings.value<float>("grassDensity", def.grassDensity);
	grassHeight = g_settings.value<float>("grassHeight", def.grassHeight);
	grassDistance = g_settings.value<float>("grassDistance", def.grassDistance);
	bumpMapping = g_settings.value<bool>("bumpMapping", def.bumpMapping);
	terrainSplat = g_settings.value<bool>("terrainSplat", def.terrainSplat);
	modelLighting = g_settings.value<bool>("modelLighting", def.modelLighting);
	modelSpecular = g_settings.value<float>("modelSpecular", def.modelSpecular);
	modelDetailNormals = g_settings.value<float>("modelDetailNormals", def.modelDetailNormals);
	skyLight = g_settings.value<bool>("skyLight", def.skyLight);
	terrainOriginalLight = g_settings.value<bool>("terrainOriginalLight", def.terrainOriginalLight);
	characterShadows = g_settings.value<bool>("characterShadows", def.characterShadows);
	realCharacterShadows = g_settings.value<bool>("realCharacterShadows", def.realCharacterShadows);
	wallShadows = g_settings.value<bool>("wallShadows", def.wallShadows);
	fogOfWar = g_settings.value<bool>("fogOfWar", def.fogOfWar);
	stainedGlass = g_settings.value<bool>("stainedGlass", def.stainedGlass);
	fogOfWarBrightness = g_settings.value<float>("fogOfWarBrightness", def.fogOfWarBrightness);
	ssao = g_settings.value<bool>("ssao", def.ssao);
	bloom = g_settings.value<bool>("bloom", def.bloom);
	ssaoRadius = g_settings.value<float>("ssaoRadius", def.ssaoRadius);
	ssaoStrength = g_settings.value<float>("ssaoStrength", def.ssaoStrength);
	bloomThreshold = g_settings.value<float>("bloomThreshold", def.bloomThreshold);
	bloomIntensity = g_settings.value<float>("bloomIntensity", def.bloomIntensity);
	antiAliasing = std::clamp(g_settings.value<int>("antiAliasing", def.antiAliasing), 0, 2);
	upscaling = std::clamp(g_settings.value<int>("upscaling", def.upscaling), 0, 4);
	sharpness = std::clamp(g_settings.value<float>("sharpness", def.sharpness), 0.0f, 1.0f);
	anisotropy = std::clamp(g_settings.value<int>("anisotropy", def.anisotropy), 1, 16);
	vsync = g_settings.value<bool>("vsync", def.vsync);
	staticShadows = g_settings.value<bool>("staticShadows", def.staticShadows);
	staticShadowLight = g_settings.value<float>("staticShadowLight", def.staticShadowLight);
	shadowResolution = g_settings.value<int>("shadowResolution", def.shadowResolution);
	if (shadowResolution != 2 && shadowResolution != 4)
		shadowResolution = 1;
	const auto meshLighting = g_settings.value("meshLighting", nlohmann::json());
	if (meshLighting.is_array() && meshLighting.size() == 2) {
		meshAmbient = meshLighting[0].get<float>();
		meshDiffuse = meshLighting[1].get<float>();
	}
	scrollSpeed = g_settings.value<float>("scrollSpeed", def.scrollSpeed);
	shiftScrollFactor = g_settings.value<float>("shiftScrollFactor", def.shiftScrollFactor);
	mouseLookSensitivity = g_settings.value<float>("mouseLookSensitivity", def.mouseLookSensitivity);
	minHeightAboveGround = g_settings.value<float>("cameraMinHeightAboveGround", def.minHeightAboveGround);
	maxHeight = g_settings.value<float>("cameraMaxHeight", def.maxHeight);
	invertMouseWheel = g_settings.value<bool>("invertMouseWheel", def.invertMouseWheel);
	musicVolume = g_settings.value<float>("musicVolume", def.musicVolume);
	effectsVolume = g_settings.value<float>("effectsVolume", def.effectsVolume);
	showFps = g_settings.value<bool>("showFps", def.showFps);
}

void RenderSettings::save()
{
	g_settings["farClip"] = farClipWholeMap ? 0.0f : farClip;
	g_settings["fogStart"] = fogStart;
	g_settings["fogOff"] = fogOff;
	g_settings["fogCustom"] = fogCustom;
	g_settings["fogNear"] = fogNear;
	g_settings["fogFar"] = fogFar;
	g_settings["edgeFog"] = edgeFog;
	g_settings["edgeFogStart"] = edgeFogStart;
	g_settings["edgeFogOpaque"] = edgeFogOpaque;
	if (fogColourOverride)
		g_settings["fogColourOverride"] = { fogColour[0], fogColour[1], fogColour[2] };
	else
		g_settings.erase("fogColourOverride");
	g_settings["fieldOfView"] = fovYDegrees;
	g_settings["exposure"] = exposure;
	g_settings["brightness"] = brightness;
	g_settings["contrast"] = contrast;
	g_settings["saturation"] = saturation;
	g_settings["sky"] = sky;
	g_settings["cloudShadows"] = cloudShadows;
	g_settings["waterLevel"] = waterQuality;
	g_settings.erase("waterSurface");
	g_settings.erase("waterDetail");
	g_settings["waterFlow"] = waterFlow;
	g_settings["fireLights"] = fireLights;
	g_settings["grass"] = grass;
	g_settings["terrainTessellation"] = terrainTessellation;
	g_settings["grassDensity"] = grassDensity;
	g_settings["grassHeight"] = grassHeight;
	g_settings["grassDistance"] = grassDistance;
	g_settings["bumpMapping"] = bumpMapping;
	g_settings["terrainSplat"] = terrainSplat;
	g_settings["modelLighting"] = modelLighting;
	g_settings["modelSpecular"] = modelSpecular;
	g_settings["modelDetailNormals"] = modelDetailNormals;
	g_settings["skyLight"] = skyLight;
	g_settings["terrainOriginalLight"] = terrainOriginalLight;
	g_settings["characterShadows"] = characterShadows;
	g_settings["realCharacterShadows"] = realCharacterShadows;
	g_settings["wallShadows"] = wallShadows;
	g_settings["fogOfWar"] = fogOfWar;
	g_settings["stainedGlass"] = stainedGlass;
	g_settings["fogOfWarBrightness"] = fogOfWarBrightness;
	g_settings["ssao"] = ssao;
	g_settings["bloom"] = bloom;
	g_settings["ssaoRadius"] = ssaoRadius;
	g_settings["ssaoStrength"] = ssaoStrength;
	g_settings["bloomThreshold"] = bloomThreshold;
	g_settings["bloomIntensity"] = bloomIntensity;
	g_settings["antiAliasing"] = antiAliasing;
	g_settings["upscaling"] = upscaling;
	g_settings["sharpness"] = sharpness;
	g_settings["anisotropy"] = anisotropy;
	g_settings["vsync"] = vsync;
	g_settings["staticShadows"] = staticShadows;
	g_settings["staticShadowLight"] = staticShadowLight;
	g_settings["shadowResolution"] = shadowResolution;
	g_settings["meshLighting"] = { meshAmbient, meshDiffuse };
	g_settings["scrollSpeed"] = scrollSpeed;
	g_settings["shiftScrollFactor"] = shiftScrollFactor;
	g_settings["mouseLookSensitivity"] = mouseLookSensitivity;
	g_settings["cameraMinHeightAboveGround"] = minHeightAboveGround;
	g_settings["cameraMaxHeight"] = maxHeight;
	g_settings["invertMouseWheel"] = invertMouseWheel;
	g_settings["musicVolume"] = musicVolume;
	g_settings["effectsVolume"] = effectsVolume;
	g_settings["showFps"] = showFps;
	SaveSettings();
}
