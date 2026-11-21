---
title: Anti-aliasing
ids: [ui/frontend/options/quality, option/antiAliasing#anti-aliasing, option/upscaling#upscaling-amd-fsr, option/upscaler#upscaler, option/sharpness#sharpening, option/anisotropy#anisotropic-filtering]
order: 80
---

# Anti-aliasing

Smooth edges, rendering at a lower resolution and scaling up for speed, sharpening, and the sharpness of textures seen at a low angle.

![Graphics > Anti-aliasing](shot:options-quality)

## Anti-aliasing

How jagged edges are smoothed:

- **Off:** no smoothing; edges and thin lines shimmer as the camera moves.
- **FXAA** (the default): a quick pass that softens edges in the finished picture.
- **TAA:** the camera shifts by a fraction of a pixel each frame and the frames are blended; smoother edges with no shimmer, a little softer (Sharpening makes up for it).
- **AMD FSR 3.1:** AMD's temporal anti-aliasing at full resolution, which also follows how things move: the cleanest edges and the least smearing.

Greyed out while Upscaling is on with the FSR 3.1 upscaler, which smooths edges itself. **Cost:** FXAA and TAA almost nothing; FSR 3.1 at full resolution about 0.5 ms.

## Upscaling (AMD FSR)

Draws the 3D scene at a lower resolution and scales it up to the window, which makes each frame faster. The interface stays sharp at the window's own resolution.

- **Off** (the default): the window's resolution.
- **Ultra quality:** 77 % of the width and height.
- **Quality:** 67 %.
- **Balanced:** 59 %.
- **Performance:** 50 %.

**Cost:** it saves time instead. In one measurement, FSR 3.1 at Quality took 2.2 ms a frame against 2.6 ms with TAA at full resolution; Performance 2.1 ms. The gain is largest on a slow graphics card or a large window.

## Upscaler

With Upscaling on: which AMD FSR does the scaling.

- **FSR 1:** scales the finished, anti-aliased picture up; quick, a little softer.
- **FSR 3.1** (the default): smooths edges and scales up together, using the frames before; sharper and steadier. It takes the place of the Anti-aliasing setting.

## Sharpening

With Upscaling on, or Anti-aliasing at TAA or FSR 3.1: how much the picture is sharpened afterwards, 0 (soft) to 1 (sharpest). Default 0.8.

## Anisotropic filtering

Keeps the textures of the ground, the water and the models sharp where you see them at a low angle, as on distant ground: **Off**, **2x**, **4x**, **8x** or **16x** (the default). Takes effect after a restart (or with the next map). **Cost:** very small on today's graphics cards.

> wkbai: the original has no anti-aliasing or upscaling; these are wkbai's own. AMD FidelityFX Super Resolution (FSR) is AMD's technique and is used under its own open-source licence.
