---
title: Models
ids: [ui/frontend/options/models, option/modelLighting#model-highlights, option/modelReflections#reflections-on-buildings-and-units, option/modelDetailNormals#model-surface-detail, option/pomModels#parallax-occlusion-mapping-models, option/pomModelsDepth#parallax-depth-models, option/meshAmbient#model-ambient-light, option/meshDiffuse#model-sunlight, option/meshLighting#model-ambient-light]
order: 60
---

# Models

How buildings, units, trees and props are lit.

![Graphics > Models](shot:options-models)

## Model highlights

Off by default. On: models are lit point by point instead of corner by corner as in the original, with the sun's highlight on shiny parts: metal, wet stone, glass. Models and textures that come with surface maps of their own (such as HD mods) show their full detail only with this on. **Cost:** a few more texture reads per pixel of model; small.

## Reflections on buildings and units

With Model highlights: what shiny surfaces reflect.

- **Low** (the default): the sky and the ground.
- **High (screen-space):** also what is on the screen around them.
- **Ultra (screen-space + ray-traced):** also what is off the screen, by tracing rays. Needs a graphics card with ray tracing; without one it looks as High.

**Cost:** small at Low; High and Ultra add a pass over the screen, Ultra also rays.

## Model surface detail

With Model highlights: for textures without a surface map of their own, how strongly their light and dark parts are shown as small bumps: 0 (flat) to 8, default 2.

## Parallax occlusion mapping (models)

With Model highlights: gives the HD models' textures real relief, such as the joints between bricks and the grain of boards, with their own small shadows. It needs textures with height maps, such as the HD models' ([material maps](../../mechanics/mods.md)).

- **Off**.
- **Low:** fewer steps per ray.
- **High** (the default): the models' look before this setting existed.

**Cost:** small: about 0.05 ms on a view of several HD buildings at High.

## Parallax depth (models)

With Parallax occlusion mapping (models) on: how deep the relief is, 0 to 4 times its real depth (default 1, up to 6 cm; 4 makes it 24 cm). At the game's usual zoom the real depth moves the surface by less than a pixel, so mostly the relief's shadows show; raise it to see more. **Cost:** none.

## Model ambient light

The light that reaches models from all around, 0 to 1. Default 0.38, the original's. Higher values make shaded sides brighter.

## Model sunlight

How strongly the sun lights the models, 0 to 1.5. Default 0.78, the original's.

> wkbai: the original lights models corner by corner, with the two values Model ambient light and Model sunlight. Highlights, reflections and surface detail are wkbai's own.
