// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#include "RenderSettings.h"
#include "../settings.h"
#include <nlohmann/json.hpp>

RenderSettings g_renderSettings;

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
	waterSurface = g_settings.value<bool>("waterSurface", def.waterSurface);
	waterDetail = g_settings.value<bool>("waterDetail", def.waterDetail);
	fireLights = g_settings.value<bool>("fireLights", def.fireLights);
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
	ssao = g_settings.value<bool>("ssao", def.ssao);
	bloom = g_settings.value<bool>("bloom", def.bloom);
	ssaoRadius = g_settings.value<float>("ssaoRadius", def.ssaoRadius);
	ssaoStrength = g_settings.value<float>("ssaoStrength", def.ssaoStrength);
	bloomThreshold = g_settings.value<float>("bloomThreshold", def.bloomThreshold);
	bloomIntensity = g_settings.value<float>("bloomIntensity", def.bloomIntensity);
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
	g_settings["waterSurface"] = waterSurface;
	g_settings["waterDetail"] = waterDetail;
	g_settings["fireLights"] = fireLights;
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
	g_settings["ssao"] = ssao;
	g_settings["bloom"] = bloom;
	g_settings["ssaoRadius"] = ssaoRadius;
	g_settings["ssaoStrength"] = ssaoStrength;
	g_settings["bloomThreshold"] = bloomThreshold;
	g_settings["bloomIntensity"] = bloomIntensity;
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
