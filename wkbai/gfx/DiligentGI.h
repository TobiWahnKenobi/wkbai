// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#pragma once

// Dynamic global illumination for the enhanced renderers on Diligent (RenderSettings::dynamicGI;
// RE repository docs/engine/gi-clouds.md, "Dynamic GI").
//
// A radiosity map of what is seen from above, around the camera (ShadowEnvironment::giRadiance,
// 256 x 256 over 512 units): per texel the light leaving the ground or the roof or unit on it,
// its albedo (the terrain's baked over the whole map by the terrain renderer, the models' drawn
// from above each frame by the scene renderer) times the sun's or the moon's light (n.l, the
// clouds' shadow, a shadow marched through the sky light's heights), the sky's light as much as
// the point sees of it, and the fires'; High adds the last frame's bounce (a second bounce).
// Its mips are the light averaged over growing footprints: the terrain's and the models'
// shaders gather from them (BounceLight in g_cloudShaderCode), so light bounces off the meadow
// onto the walls, off a sunlit wall's roof onto the ground beside it, in their colours.

struct DiligentGfx;

// The bounce map, the models' capture, the terrain albedo's placeholder, the radiance pass;
// with the device, after CreateCloudResources
void CreateGIResources(DiligentGfx* gfx);
// The radiance pass, after the sky light's map and the models' capture (the scene renderer,
// while ShadowEnvironment::giMode is on); fires: ShadowEnvironment::fireLights (7 floats each)
void RenderGIRadiance(DiligentGfx* gfx);
// The benchmark's GPU time of the GI passes (g_giGpuMs, with timestamp queries): around the
// scene renderer's capture and radiance pass
void GITimerBegin(DiligentGfx* gfx);
void GITimerEnd(DiligentGfx* gfx);
void GITimerSkip(); // a frame that keeps the last map
