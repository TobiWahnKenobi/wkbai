// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#include "MenuOptions.h"
#include "../gfx/RenderSettings.h"
#include "../settings.h"
#include "../SoundPlayer.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>

namespace {
	MenuOption toggle(std::string id, std::string page, std::string label, bool& value)
	{
		MenuOption o;
		o.id = std::move(id); o.page = std::move(page); o.label = std::move(label);
		o.type = MenuOption::TOGGLE;
		o.get = [&value]() { return value ? 1.0f : 0.0f; };
		o.set = [&value](float v) { value = v != 0.0f; };
		return o;
	}

	MenuOption slider(std::string id, std::string page, std::string label, float& value, float min, float max, std::string format = "%.2f")
	{
		MenuOption o;
		o.id = std::move(id); o.page = std::move(page); o.label = std::move(label);
		o.type = MenuOption::SLIDER;
		o.min = min; o.max = max;
		o.format = std::move(format);
		o.get = [&value]() { return value; };
		o.set = [&value](float v) { value = v; };
		return o;
	}

	MenuOption withEnabled(MenuOption o, std::function<bool()> enabled)
	{
		o.enabled = std::move(enabled);
		return o;
	}

	MenuOption logarithmic(MenuOption o)
	{
		o.logarithmic = true;
		return o;
	}

	std::vector<MenuOption> buildOptions()
	{
		RenderSettings& rs = g_renderSettings;
		std::vector<MenuOption> list;

		// View and fog
		list.push_back(toggle("wholeMapView", "view", "View distance: whole map", rs.farClipWholeMap));
		list.push_back(withEnabled(logarithmic(slider("viewDistance", "view", "View distance", rs.farClip, 100.0f, 50000.0f, "%.0f")),
			[&rs]() { return !rs.farClipWholeMap; }));
		list.push_back(toggle("fogOff", "view", "Fog off", rs.fogOff));
		list.push_back(withEnabled(toggle("fogCustom", "view", "Own fog distances", rs.fogCustom), [&rs]() { return !rs.fogOff; }));
		list.push_back(withEnabled(logarithmic(slider("fogNear", "view", "Fog near", rs.fogNear, 1.0f, 50000.0f, "%.0f")),
			[&rs]() { return !rs.fogOff && rs.fogCustom; }));
		list.push_back(withEnabled(logarithmic(slider("fogFar", "view", "Fog far (opaque)", rs.fogFar, 1.0f, 50000.0f, "%.0f")),
			[&rs]() { return !rs.fogOff && rs.fogCustom; }));
		list.push_back(withEnabled(slider("fogStart", "view", "Fog start (of fog end)", rs.fogStart, 0.0f, 0.99f),
			[&rs]() { return !rs.fogOff && !rs.fogCustom; }));
		list.push_back(toggle("fogColourOverride", "view", "Own fog colour", rs.fogColourOverride));
		static const char* const fogChannels[3] = { "Fog colour: red", "Fog colour: green", "Fog colour: blue" };
		for (int i = 0; i < 3; i++)
			list.push_back(withEnabled(slider(std::string("fogColour") + "RGB"[i], "view", fogChannels[i], rs.fogColour[i], 0.0f, 1.0f),
				[&rs]() { return rs.fogColourOverride; }));
		list.push_back(toggle("edgeFog", "view", "Map-edge fog", rs.edgeFog));
		list.push_back(withEnabled(slider("edgeFogStart", "view", "Edge fog starts", rs.edgeFogStart, -1.0f, 0.95f),
			[&rs]() { return rs.edgeFog; }));
		list.push_back(withEnabled(slider("edgeFogOpaque", "view", "Edge fog opaque at", rs.edgeFogOpaque, 0.0f, 0.95f),
			[&rs]() { return rs.edgeFog; }));
		list.push_back(slider("fieldOfView", "view", "Field of view", rs.fovYDegrees, 20.0f, 100.0f, "%.0f"));
		list.push_back(toggle("sky", "view", "Sky", rs.sky));

		// Picture (the 3D scene, not the HUD)
		list.push_back(logarithmic(slider("exposure", "picture", "Exposure", rs.exposure, 0.25f, 4.0f)));
		list.push_back(slider("brightness", "picture", "Brightness", rs.brightness, -0.5f, 0.5f));
		list.push_back(slider("contrast", "picture", "Contrast", rs.contrast, 0.25f, 2.0f));
		list.push_back(slider("saturation", "picture", "Saturation", rs.saturation, 0.0f, 2.0f));

		// Lighting and shadows
		list.push_back(toggle("cloudShadows", "lighting", "Cloud shadows", rs.cloudShadows));
		list.push_back(toggle("buildingShadows", "lighting", "Building shadows", rs.staticShadows));
		list.push_back(withEnabled(slider("staticShadowLight", "lighting", "Light in building shadows", rs.staticShadowLight, 0.0f, 1.0f),
			[&rs]() { return rs.staticShadows; }));
		list.push_back(toggle("characterShadows", "lighting", "Character shadows", rs.characterShadows));
		list.push_back(withEnabled(toggle("realCharacterShadows", "lighting", "Real character shadows", rs.realCharacterShadows),
			[&rs]() { return rs.characterShadows && rs.staticShadows; }));
		list.push_back(toggle("waterSurface", "lighting", "Water surface", rs.waterSurface));
		list.push_back(toggle("ssao", "lighting", "Ambient occlusion", rs.ssao));
		list.push_back(toggle("bloom", "lighting", "Bloom", rs.bloom));
		list.push_back(toggle("bumpMapping", "lighting", "Terrain bump mapping", rs.bumpMapping));
		list.push_back(slider("meshAmbient", "lighting", "Model ambient light", rs.meshAmbient, 0.0f, 1.0f));
		list.push_back(slider("meshDiffuse", "lighting", "Model sunlight", rs.meshDiffuse, 0.0f, 1.5f));

		// Camera and controls
		list.push_back(slider("scrollSpeed", "camera", "Scroll speed", rs.scrollSpeed, 0.1f, 5.0f));
		list.push_back(slider("shiftScrollFactor", "camera", "Scroll speed with Shift", rs.shiftScrollFactor, 1.0f, 10.0f, "%.1f"));
		list.push_back(slider("cameraRotationSpeed", "camera", "Camera rotation speed", rs.mouseLookSensitivity, 0.001f, 0.05f, "%.3f"));
		list.push_back(slider("minHeight", "camera", "Lowest height above ground", rs.minHeightAboveGround, 0.5f, 50.0f, "%.1f"));
		list.push_back(slider("maxHeight", "camera", "Highest camera height", rs.maxHeight, 100.0f, 3000.0f, "%.0f"));
		list.push_back(toggle("invertMouseWheel", "camera", "Invert mouse wheel", rs.invertMouseWheel));

		// Sound
		{
			MenuOption o;
			o.id = "music"; o.page = "sound"; o.label = "Music";
			o.get = []() { return g_settings.value<bool>("musicEnabled", true) ? 1.0f : 0.0f; };
			o.set = [](float v) { g_settings["musicEnabled"] = v != 0.0f; };
			list.push_back(o);
		}
		list.push_back(slider("musicVolume", "sound", "Music volume", rs.musicVolume, 0.0f, 1.0f, "%.0f%%"));
		list.push_back(slider("effectsVolume", "sound", "Effects volume", rs.effectsVolume, 0.0f, 1.0f, "%.0f%%"));

		// System
		{
			MenuOption o;
			o.id = "renderer"; o.page = "system"; o.label = "Renderer";
			o.type = MenuOption::CHOICE;
			o.choices = { "Direct3D 12", "Vulkan" };
			o.get = []() { return g_settings.value<std::string>("diligentBackend", "d3d12") == "vulkan" ? 1.0f : 0.0f; };
			o.set = [](float v) { g_settings["diligentBackend"] = v != 0.0f ? "vulkan" : "d3d12"; };
			o.needsRestart = true;
			list.push_back(o);
		}
		list.push_back(toggle("vsync", "system", "V-sync", rs.vsync));
		list.push_back(toggle("showFps", "system", "Show FPS", rs.showFps));
		// the display's real pixels, and the interface's size (window.h, g_uiScale)
		{
			MenuOption o;
			o.id = "dpiAware"; o.page = "system"; o.label = "Full display resolution (high DPI)";
			o.get = []() { return g_settings.value("dpiAware", false) ? 1.0f : 0.0f; };
			o.set = [](float v) { g_settings["dpiAware"] = v != 0.0f; };
			o.needsRestart = true;
			list.push_back(o);
		}
		{
			MenuOption o;
			o.id = "uiScale"; o.page = "system"; o.label = "Interface size";
			o.type = MenuOption::CHOICE;
			o.choices = { "Automatic", "100%", "125%", "150%", "175%", "200%" };
			static const float scales[] = { 0.0f, 1.0f, 1.25f, 1.5f, 1.75f, 2.0f };
			o.get = []() {
				const float s = g_settings.value("uiScale", 0.0f);
				for (int i = 0; i < 6; i++)
					if (std::abs(scales[i] - s) < 0.01f)
						return (float)i;
				return 0.0f;
			};
			o.set = [](float v) { g_settings["uiScale"] = scales[std::clamp((int)v, 0, 5)]; };
			o.needsRestart = true;
			list.push_back(o);
		}
		return list;
	}
}

const std::vector<MenuPage>& GetMenuPages()
{
	static const std::vector<MenuPage> pages = {
		{ "view", "View and Fog" },
		{ "picture", "Picture" },
		{ "lighting", "Lighting and Shadows" },
		{ "camera", "Camera" },
		{ "sound", "Sound" },
		{ "system", "System" },
	};
	return pages;
}

const std::vector<MenuOption>& GetMenuOptions()
{
	static const std::vector<MenuOption> options = buildOptions();
	return options;
}

const MenuOption* FindMenuOption(const std::string& id)
{
	for (const MenuOption& o : GetMenuOptions())
		if (o.id == id)
			return &o;
	return nullptr;
}

void SaveMenuOptions()
{
	g_renderSettings.save();
	SoundPlayer::getSoundPlayer()->applyVolumes();
}

namespace {
	nlohmann::json g_optionsSnapshot;
}

void BeginEditMenuOptions()
{
	g_optionsSnapshot = g_settings;
}

void RevertMenuOptions()
{
	if (!g_optionsSnapshot.is_null())
		g_settings = g_optionsSnapshot;
	g_renderSettings.load();
	SoundPlayer::getSoundPlayer()->applyVolumes();
}
