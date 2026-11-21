---
title: Graphics
ids: [options/graphics, ui/frontend/options/graphics]
order: 10
---

# Graphics

The Graphics tab has nine pages. Together they decide how far you see, how the sky, light, water and ground look, and how the picture is finished.

| Page | What it sets |
|---|---|
| [View and fog](view-fog.md) | How far you see, the distance fog, the fog at the map's edge, the field of view |
| [Sky and weather](sky-weather.md) | The sky, the clouds, how much rain and snow is drawn, snow and ice, rainbows |
| [Shadows and light](shadows-light.md) | Shadows of buildings and units, ray-traced shadows, light from fires and the sky, bounced light, ambient occlusion |
| [Water](water.md) | The water's look, flowing rivers, waterfalls, reflections |
| [Terrain and grass](terrain-grass.md) | The ground's shape and sheen, the land beyond the map's edge, grass |
| [Models](models.md) | How buildings and units are lit |
| [Picture](picture.md) | Exposure, brightness, contrast, saturation |
| [Anti-aliasing](anti-aliasing.md) | Smooth edges, upscaling, sharpening, texture filtering |
| [Effects](effects.md) | Bloom, motion blur, tilt-shift |

## The enhanced renderer

Most of wkbai's own effects (volumetric clouds, the new water, grass, tessellation, the light from the sky and from fires, ray tracing, post-processing) need the enhanced renderer, which is on by default (`enhancedGraphics` in the [settings file](../config-file.md#enhancedgraphics)). Without it, the game is drawn as the original draws it, and those settings have no effect.

## What costs the most

The frame times below were measured at 1920 x 1080 on a Radeon RX 6900 XT; a slower graphics card pays more, but in the same proportion.

- **Cloud quality** High or Ultra: up to about 2.5 ms a frame more than Medium when much sky is in view. The biggest single cost.
- **Water** and **Waterfalls:** about 0.6 ms a frame at High and 0.9 ms at Ultra over Original, on a view with a big river and a waterfall.
- **Anti-aliasing** AMD FSR 3.1 at full resolution: about 0.5 ms; with FSR 3.1 upscaling the frame gets faster instead.
- **Ray-traced reflections:** about 0.3 to 0.4 ms. Ray-traced shadows cost about what the shadow map they replace costs.
- **Bounced light** (dynamic GI) Low or High: 0.1 to 0.3 ms.
- **Extend map:** almost nothing per frame, but building the far land takes seconds when a map loads: a few seconds at Medium and High, up to about 12 seconds at Ultra (less once it is kept on disk).
- Most other settings (grass, fog, rainbows, the picture controls, model lighting) cost too little to measure.

## On a slower computer

Turn these down first, in this order:

1. **Upscaling** to Quality or Balanced ([Anti-aliasing](anti-aliasing.md#upscaling-amd-fsr)): the largest gain for the smallest loss.
2. **Cloud quality** to Low, or **Clouds** to Light shafts ([Sky and weather](sky-weather.md)).
3. **Water** to Medium or Low ([Water](water.md#water)).
4. **Bounced light** off, **Weather and season effects** to Low.
5. **Grass distance** and **Grass density** down ([Terrain and grass](terrain-grass.md)).

## Ray tracing

**Ray-traced shadows**, **Ray-traced water reflections** and the Ultra **Reflections on buildings and units** need a graphics card that can trace rays from its shaders (DirectX 12 ray tracing 1.1, or the same in Vulkan). Without one, they fall back to the ordinary way and cost nothing.
