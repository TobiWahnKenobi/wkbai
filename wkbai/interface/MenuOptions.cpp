// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#include "MenuOptions.h"
#include "../gfx/RenderSettings.h"
#include "../Environment.h"
#include "../settings.h"
#include "../SoundPlayer.h"
#include "../matchmaking/Matchmaking.h"
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

	// A text in wkconfig.json's "matchmaking" (path: its keys under it); empty: the key removed
	MenuOption matchmakingText(std::string id, std::string label, std::vector<std::string> path, std::string hint, int maxLength = 100)
	{
		MenuOption o;
		o.id = std::move(id); o.page = "online"; o.label = std::move(label);
		o.type = MenuOption::TEXT;
		o.hint = std::move(hint);
		o.maxLength = maxLength;
		o.getText = [path]() {
			const nlohmann::json* j = &g_settings;
			std::vector<std::string> keys = { "matchmaking" };
			keys.insert(keys.end(), path.begin(), path.end());
			for (const std::string& key : keys) {
				if (!j->is_object() || !j->contains(key)) return std::string();
				j = &(*j)[key];
			}
			return j->is_string() ? j->get<std::string>() : std::string();
		};
		o.setText = [path](const std::string& text) {
			if (!g_settings["matchmaking"].is_object())
				g_settings["matchmaking"] = nlohmann::json::object();
			nlohmann::json* j = &g_settings["matchmaking"];
			for (size_t i = 0; i + 1 < path.size(); i++) {
				if (!(*j)[path[i]].is_object())
					(*j)[path[i]] = nlohmann::json::object();
				j = &(*j)[path[i]];
			}
			if (text.empty())
				j->erase(path.back());
			else
				(*j)[path.back()] = text;
		};
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
		list.push_back(toggle("sky", "sky", "Sky", rs.sky));
		{
			// (the physical sky: with the volumetric clouds, instead of the sky box)
			MenuOption o;
			o.id = "skyModel"; o.page = "sky"; o.label = "Sky model";
			o.type = MenuOption::CHOICE;
			o.choices = { "Normal", "Physical" };
			o.get = [&rs]() { return (float)rs.skyModel; };
			o.set = [&rs](float v) { rs.skyModel = std::clamp((int)v, 0, 1); };
			list.push_back(withEnabled(o, [&rs]() { return rs.sky && rs.dynamicClouds >= 3; }));
		}

		// Picture (the 3D scene, not the HUD)
		list.push_back(logarithmic(slider("exposure", "picture", "Exposure", rs.exposure, 0.25f, 4.0f)));
		list.push_back(slider("brightness", "picture", "Brightness", rs.brightness, -0.5f, 0.5f));
		list.push_back(slider("contrast", "picture", "Contrast", rs.contrast, 0.25f, 2.0f));
		list.push_back(slider("saturation", "picture", "Saturation", rs.saturation, 0.0f, 2.0f));

		// Lighting and shadows
		list.push_back(toggle("cloudShadows", "sky", "Cloud shadows", rs.cloudShadows));
		list.push_back(toggle("buildingShadows", "lighting", "Building shadows", rs.staticShadows));
		list.push_back(withEnabled(slider("staticShadowLight", "lighting", "Light in building shadows", rs.staticShadowLight, 0.0f, 1.0f),
			[&rs]() { return rs.staticShadows; }));
		list.push_back(withEnabled(toggle("wallShadows", "lighting", "Wall shadows", rs.wallShadows), [&rs]() { return rs.staticShadows; }));
		{
			MenuOption o;
			o.id = "shadowResolution"; o.page = "lighting"; o.label = "Building shadow detail";
			o.type = MenuOption::CHOICE;
			o.choices = { "Original", "High", "Very high" };
			o.get = [&rs]() { return rs.shadowResolution == 4 ? 2.0f : rs.shadowResolution == 2 ? 1.0f : 0.0f; };
			o.set = [&rs](float v) { rs.shadowResolution = v >= 2.0f ? 4 : v >= 1.0f ? 2 : 1; };
			o.enabled = [&rs]() { return rs.staticShadows; };
			list.push_back(o);
		}
		list.push_back(toggle("characterShadows", "lighting", "Character shadows", rs.characterShadows));
		list.push_back(withEnabled(toggle("realCharacterShadows", "lighting", "Real character shadows", rs.realCharacterShadows),
			[&rs]() { return rs.characterShadows && rs.staticShadows; }));
		list.push_back(withEnabled(toggle("rtShadows", "lighting", "Ray-traced shadows", rs.rtShadows), [&rs]() { return rs.staticShadows; }));
		list.push_back(toggle("contactShadows", "lighting", "Contact shadows", rs.contactShadows));
		{
			MenuOption o;
			o.id = "waterQuality"; o.page = "water"; o.label = "Water";
			o.type = MenuOption::CHOICE;
			o.choices = { "Original", "Off", "Very low", "Low", "Medium", "High", "Very high", "Ultra" }; // RenderSettings::WATER_*
			o.get = [&rs]() { return (float)rs.waterQuality; };
			o.set = [&rs](float v) { rs.waterQuality = std::clamp((int)v, 0, (int)RenderSettings::WATER_LEVELS - 1); };
			list.push_back(o);
		}
		list.push_back(withEnabled(toggle("waterFlow", "water", "Flowing rivers", rs.waterFlow), [&rs]() { return rs.waterQuality >= RenderSettings::WATER_LOW; }));
		{
			MenuOption o;
			o.id = "waterfalls"; o.page = "water"; o.label = "Waterfalls";
			o.type = MenuOption::CHOICE;
			o.choices = { "Off", "Low", "Medium", "High", "Ultra" };
			o.get = [&rs]() { return (float)rs.waterfalls; };
			o.set = [&rs](float v) { rs.waterfalls = std::clamp((int)v, 0, 4); };
			list.push_back(o);
		}
		list.push_back(toggle("fireLights", "lighting", "Light from fires", rs.fireLights));
		list.push_back(toggle("skyGI", "lighting", "Light from the sky", rs.skyGI));
		{
			// the dynamic GI (DiligentGI): needs the sky light
			MenuOption o;
			o.id = "dynamicGI"; o.page = "lighting"; o.label = "Bounced light (dynamic GI)";
			o.type = MenuOption::CHOICE;
			o.choices = { "Off", "Low", "High" };
			o.get = [&rs]() { return (float)rs.dynamicGI; };
			o.set = [&rs](float v) { rs.dynamicGI = std::clamp((int)v, 0, 2); };
			list.push_back(withEnabled(o, [&rs]() { return rs.skyGI; }));
		}
		list.push_back(withEnabled(slider("dynamicGIStrength", "lighting", "Bounced light strength", rs.dynamicGIStrength, 0.0f, 3.0f),
			[&rs]() { return rs.skyGI && rs.dynamicGI > 0; }));
		list.push_back(withEnabled(toggle("ssReflections", "water", "Screen-space water reflections", rs.ssReflections), [&rs]() { return rs.waterQuality >= RenderSettings::WATER_LOW; }));
		list.push_back(withEnabled(toggle("rtReflections", "water", "Ray-traced water reflections", rs.rtReflections), [&rs]() { return rs.waterQuality >= RenderSettings::WATER_LOW; }));
		list.push_back(slider("terrainFresnel", "terrain", "Ground Fresnel (sky sheen)", rs.terrainFresnel, 0.0f, 1.0f));
		{
			MenuOption o;
			o.id = "dynamicClouds"; o.page = "sky"; o.label = "Clouds";
			o.type = MenuOption::CHOICE;
			o.choices = { "Original", "Moving shadows", "Light shafts", "Volumetric" };
			o.get = [&rs]() { return (float)rs.dynamicClouds; };
			o.set = [&rs](float v) { rs.dynamicClouds = std::clamp((int)v, 0, 3); };
			list.push_back(withEnabled(o, [&rs]() { return rs.cloudShadows; }));
		}
		{
			MenuOption o;
			o.id = "cloudQuality"; o.page = "sky"; o.label = "Cloud quality"; // (the volumetric clouds' steps; the high clouds' kinds and samples, the stratus' texture)
			o.type = MenuOption::CHOICE;
			o.choices = { "Low", "Medium", "High", "Ultra" };
			o.get = [&rs]() { return (float)rs.cloudQuality; };
			o.set = [&rs](float v) { rs.cloudQuality = std::clamp((int)v, 0, 3); };
			list.push_back(withEnabled(o, [&rs]() { return rs.cloudShadows && rs.dynamicClouds >= 3; }));
		}
		{
			MenuOption o;
			o.id = "cloudDetail"; o.page = "sky"; o.label = "Cloud detail"; // (the volumetric clouds' shapes: Low one density, Medium varying, High billows and cumulonimbus)
			o.type = MenuOption::CHOICE;
			o.choices = { "Low", "Medium", "High" };
			o.get = [&rs]() { return (float)rs.cloudDetail; };
			o.set = [&rs](float v) { rs.cloudDetail = std::clamp((int)v, 0, 2); };
			list.push_back(withEnabled(o, [&rs]() { return rs.cloudShadows && rs.dynamicClouds >= 3; }));
		}
		// (the cloud weather and cover: the game settings' now, in the lobby's Weather tab)
		{
			MenuOption o;
			o.id = "weatherEffects"; o.page = "sky"; o.label = "Weather and season effects";
			o.type = MenuOption::CHOICE;
			o.choices = { "Low", "Medium", "High", "Ultra" };
			o.get = [&rs]() { return (float)rs.weatherEffects; };
			o.set = [&rs](float v) { rs.weatherEffects = std::clamp((int)v, 0, 3); };
			list.push_back(o);
		}
		{
			MenuOption o;
			o.id = "terrainTessellation"; o.page = "terrain"; o.label = "Terrain tessellation";
			o.type = MenuOption::CHOICE;
			o.choices = { "Off", "Low", "Medium", "High" };
			o.get = [&rs]() { return (float)rs.terrainTessellation; };
			o.set = [&rs](float v) { rs.terrainTessellation = std::clamp((int)v, 0, 3); };
			list.push_back(o);
		}
		{
			// the land continued beyond the map's edge (drawn only; ExtendMap.h)
			MenuOption o;
			o.id = "extendMap"; o.page = "terrain"; o.label = "Extend map";
			o.type = MenuOption::CHOICE;
			o.choices = { "Off", "Near", "Far", "Horizon" };
			o.get = [&rs]() { return (float)rs.extendMap; };
			o.set = [&rs](float v) { rs.extendMap = std::clamp((int)v, 0, 3); };
			list.push_back(o);
		}
		{
			// its far land's detail (the first far ring's grid; Ultra builds slower)
			MenuOption o;
			o.id = "extendMapDetail"; o.page = "terrain"; o.label = "Extend map detail";
			o.type = MenuOption::CHOICE;
			o.choices = { "Low", "Medium", "High", "Ultra" };
			o.get = [&rs]() { return (float)rs.extendMapDetail; };
			o.set = [&rs](float v) { rs.extendMapDetail = std::clamp((int)v, 0, 3); };
			list.push_back(withEnabled(o, [&rs]() { return rs.extendMap >= 2; }));
		}
		{
			// the far land's soft terrain shadows (a little longer to build)
			MenuOption o;
			o.id = "extendMapShadows"; o.page = "terrain"; o.label = "Far land shadows";
			o.type = MenuOption::TOGGLE;
			o.get = [&rs]() { return rs.extendMapShadows ? 1.0f : 0.0f; };
			o.set = [&rs](float v) { rs.extendMapShadows = v != 0.0f; };
			list.push_back(withEnabled(o, [&rs]() { return rs.extendMap >= 2; }));
		}
		list.push_back(toggle("grass", "terrain", "Grass", rs.grass));
		list.push_back(withEnabled(slider("grassDensity", "terrain", "Grass density", rs.grassDensity, 0.0f, 24.0f, "%.1f"),
			[&rs]() { return rs.grass; }));
		list.push_back(withEnabled(slider("grassHeight", "terrain", "Grass height", rs.grassHeight, 0.2f, 3.0f, "%.2f"),
			[&rs]() { return rs.grass; }));
		list.push_back(withEnabled(slider("grassDistance", "terrain", "Grass distance", rs.grassDistance, 10.0f, 250.0f, "%.0f"),
			[&rs]() { return rs.grass; }));
		list.push_back(toggle("ssao", "lighting", "Ambient occlusion", rs.ssao));
		list.push_back(toggle("bloom", "effects", "Bloom", rs.bloom));
		{
			MenuOption o;
			o.id = "antiAliasing"; o.page = "quality"; o.label = "Anti-aliasing";
			o.type = MenuOption::CHOICE;
			o.choices = { "Off", "FXAA", "TAA", "AMD FSR 3.1" };
			o.get = [&rs]() { return (float)rs.antiAliasing; };
			o.set = [&rs](float v) { rs.antiAliasing = std::clamp((int)v, 0, 3); };
			// FSR 3.1 upscaling anti-aliases by itself
			list.push_back(withEnabled(o, [&rs]() { return !(rs.upscaling > 0 && rs.upscaler == 1); }));
		}
		{
			MenuOption o;
			o.id = "upscaling"; o.page = "quality"; o.label = "Upscaling (AMD FSR)";
			o.type = MenuOption::CHOICE;
			o.choices = { "Off", "Ultra quality", "Quality", "Balanced", "Performance" };
			o.get = [&rs]() { return (float)rs.upscaling; };
			o.set = [&rs](float v) { rs.upscaling = std::clamp((int)v, 0, 4); };
			list.push_back(o);
		}
		{
			MenuOption o;
			o.id = "upscaler"; o.page = "quality"; o.label = "Upscaler";
			o.type = MenuOption::CHOICE;
			o.choices = { "FSR 1", "FSR 3.1" };
			o.get = [&rs]() { return (float)rs.upscaler; };
			o.set = [&rs](float v) { rs.upscaler = std::clamp((int)v, 0, 1); };
			list.push_back(withEnabled(o, [&rs]() { return rs.upscaling > 0; }));
		}
		list.push_back(withEnabled(slider("sharpness", "quality", "Sharpening", rs.sharpness, 0.0f, 1.0f),
			[&rs]() { return rs.upscaling > 0 || rs.antiAliasing >= 2; }));
		// camera motion blur: 0 off, up to the whole frame's motion
		list.push_back(slider("motionBlur", "effects", "Camera motion blur", rs.motionBlur, 0.0f, 1.0f, "%.2f"));
		// tilt-shift: 0 off, up to the strongest blur outside the sharp band
		list.push_back(slider("tiltShift", "effects", "Tilt-shift", rs.tiltShift, 0.0f, 1.0f, "%.2f"));
		{
			MenuOption o;
			o.id = "anisotropy"; o.page = "quality"; o.label = "Anisotropic filtering";
			o.type = MenuOption::CHOICE;
			o.choices = { "Off", "2x", "4x", "8x", "16x" };
			o.get = [&rs]() { int k = 0; while ((2 << k) <= rs.anisotropy && k < 4) k++; return (float)k; };
			o.set = [&rs](float v) { const int k = std::clamp((int)v, 0, 4); rs.anisotropy = k == 0 ? 1 : (1 << k); };
			o.needsRestart = true;
			list.push_back(o);
		}
		list.push_back(toggle("bumpMapping", "terrain", "Terrain bump mapping", rs.bumpMapping));
		list.push_back(toggle("modelLighting", "models", "Model highlights", rs.modelLighting));
		list.push_back(withEnabled(slider("modelDetailNormals", "models", "Model surface detail", rs.modelDetailNormals, 0.0f, 8.0f, "%.1f"),
			[&rs]() { return rs.modelLighting; }));
		list.push_back(slider("meshAmbient", "models", "Model ambient light", rs.meshAmbient, 0.0f, 1.0f));
		list.push_back(slider("meshDiffuse", "models", "Model sunlight", rs.meshDiffuse, 0.0f, 1.5f));

		// Camera and controls
		list.push_back(slider("scrollSpeed", "camera", "Scroll speed", rs.scrollSpeed, 0.1f, 5.0f));
		list.push_back(slider("shiftScrollFactor", "camera", "Scroll speed with Shift", rs.shiftScrollFactor, 1.0f, 10.0f, "%.1f"));
		list.push_back(slider("cameraRotationSpeed", "camera", "Camera rotation speed", rs.mouseLookSensitivity, 0.001f, 0.05f, "%.3f"));
		list.push_back(slider("minHeight", "camera", "Lowest height above ground", rs.minHeightAboveGround, 0.5f, 50.0f, "%.1f"));
		list.push_back(slider("maxHeight", "camera", "Highest camera height", rs.maxHeight, 100.0f, 3000.0f, "%.0f"));
		list.push_back(toggle("invertMouseWheel", "camera", "Invert mouse wheel", rs.invertMouseWheel));
		list.push_back(toggle("fixedCamera", "camera", "Fixed camera (no free look)", rs.fixedCamera));

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
		{
			// applied at once by the window (window.cpp)
			MenuOption o;
			o.id = "displayMode"; o.page = "system"; o.label = "Display mode";
			o.type = MenuOption::CHOICE;
			o.choices = { "Windowed", "Borderless fullscreen", "Fullscreen" };
			o.get = [&rs]() { return (float)rs.displayMode; };
			o.set = [&rs](float v) { rs.displayMode = std::clamp((int)v, 0, 2); };
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
		// Online games (matchmaking/NakamaService.cpp); a changed server takes effect when OK is
		// pressed (Matchmaking::settingsChanged)
		list.push_back(matchmakingText("nakamaServer", "Nakama server", { "nakama", "server" }, "none (e.g. https://games.example.org)"));
		list.push_back(withEnabled(matchmakingText("nakamaServerKey", "Nakama server key", { "nakama", "serverKey" }, "defaultkey"),
			[]() { return !MatchmakingSettings("nakama").value("server", std::string()).empty(); }));
		list.push_back(withEnabled(matchmakingText("nakamaTitle", "Its name in the game list", { "nakama", "title" }, "Nakama", 40),
			[]() { return !MatchmakingSettings("nakama").value("server", std::string()).empty(); }));
		{
			MenuOption o;
			o.id = "nakamaRelay"; o.page = "online"; o.label = "Games through the Nakama server";
			o.type = MenuOption::CHOICE;
			o.choices = { "If direct fails", "Always", "Never" };
			static const char* const modes[] = { "auto", "always", "never" };
			o.get = []() {
				const std::string m = MatchmakingSettings("nakama").value("relay", std::string("auto"));
				return m == "always" ? 1.0f : m == "never" ? 2.0f : 0.0f;
			};
			o.set = [](float v) {
				if (!g_settings["matchmaking"].is_object())
					g_settings["matchmaking"] = nlohmann::json::object();
				if (!g_settings["matchmaking"]["nakama"].is_object())
					g_settings["matchmaking"]["nakama"] = nlohmann::json::object();
				g_settings["matchmaking"]["nakama"]["relay"] = modes[std::clamp((int)v, 0, 2)];
			};
			o.enabled = []() { return !MatchmakingSettings("nakama").value("server", std::string()).empty(); };
			list.push_back(o);
		}
		list.push_back(matchmakingText("stunServer", "STUN server (public address)", { "stun" },
			std::string(DEFAULT_STUN_SERVER) + " (none: off)"));
		return list;
	}
}

const std::vector<MenuPage>& GetMenuPages()
{
	// the graphics in pages of their own, a row of tabs under the Graphics tab
	static const std::vector<MenuPage> pages = {
		{ "view", "View and fog", "Graphics" },
		{ "sky", "Sky and weather", "Graphics" },
		{ "lighting", "Shadows and light", "Graphics" },
		{ "water", "Water", "Graphics" },
		{ "terrain", "Terrain and grass", "Graphics" },
		{ "models", "Models", "Graphics" },
		{ "picture", "Picture", "Graphics" },
		{ "quality", "Anti-aliasing", "Graphics" },
		{ "effects", "Effects", "Graphics" },
		{ "camera", "Camera", "" },
		{ "sound", "Sound", "" },
		{ "system", "System", "" },
		{ "online", "Online", "" },
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
	g_renderSettings.applyExtendMap(); // (the Extend map rebuilt now, if it changed)
	SoundPlayer::getSoundPlayer()->applyVolumes();
	Matchmaking::get().settingsChanged();
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
