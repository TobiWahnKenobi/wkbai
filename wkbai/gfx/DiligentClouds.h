// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#pragma once

// Dynamic clouds and the sky light's occlusion for the enhanced renderers on Diligent
// (RenderSettings::dynamicClouds, ::skyGI; RE repository docs/engine/gi-clouds.md).
//
// Clouds: one cloud field over the map, two layers of a tiling noise texture drifting with the
// wind, cut by the cloud cover. The terrain, the water, the grass and the models take their
// cloud shadow from where the sun's ray through them crosses the cloud layer (instead of the
// original's sky_shadow_map.pcx); the post-processing marches the view rays through the air for
// the light shafts between the clouds' shadows; the sky raymarches the cloud layer itself, lit by
// the sun through the cloud above each point (silver linings towards the sun) and by the sky.
//
// Sky light: a map of how much of the sky each point of the ground sees, around the camera:
// the terrain's heights and the static shadow casters seen from straight above (the scene
// renderer draws them), horizon-searched in 16 directions. The terrain and the models take
// their ambient light from the sky box's colours times that, the sun's bounce off the ground
// where the sky is hidden.

#include "RefCntAutoPtr.hpp"
#include "RenderDevice.h"
#include "DeviceContext.h"
#include "../util/vecmat.h"

struct DiligentGfx;
struct Camera;

// HLSL: cbuffer CloudBuffer, Texture2D cloudNoise and skyOccTexture, SamplerState
// cloudNoiseSampler (wrapping, linear) and skyOccSampler (clamped, linear), which each signature
// using them declares as immutable samplers; CloudCover(xz, lod), CloudShadow(world position),
// SkyVisibility(world position, ground or model) and SkyAmbient(normal). Prepended to the
// enhanced renderers' shaders that use them.
extern const char* const g_cloudShaderCode;

// The cloud noise and the cloud constant buffer, made with the device
void CreateCloudResources(DiligentGfx* gfx);
// Once per frame before the 3D scene: the clouds' wind, cover and the sun
// (and where the sky light's map lies, around the point the camera looks at); by the scene
// renderer, before it draws
void UpdateCloudBuffer(DiligentGfx* gfx, const Camera& camera, const Vector3& sunDirection, uint32_t sunColour);

// A weather's clouds: the cover (0-1), the layer's base altitude and thickness, how flat (stratus
// 0-1) and how stormy (towers, anvils, dark bases 0-1), the density, the share of the sunlight
// their shadows take, the light on them (1 a fair day's); the high clouds: cirrus and altocumulus
// (0-1), and the altocumulus' form (0 floccus: scattered tufts, 1 stratiformis: a sheet of cells)
struct CloudPreset { float cover, base, thickness, stratus, storm, density, shadow, light, cirrus, alto, altoSheet; };
constexpr int CLOUD_WEATHER_COUNT = 10; // as the seasons' weather kinds (EnvWeather)
extern const CloudPreset g_cloudPresets[CLOUD_WEATHER_COUNT];
