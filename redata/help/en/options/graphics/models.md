---
title: Models
ids: [ui/frontend/options/models, option/modelLighting#model-highlights, option/modelReflections#reflections-on-buildings-and-units, option/modelDetailNormals#model-surface-detail, option/meshAmbient#model-ambient-light, option/meshDiffuse#model-sunlight, option/meshLighting#model-ambient-light]
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

## Model ambient light

The light that reaches models from all around, 0 to 1. Default 0.38, the original's. Higher values make shaded sides brighter.

## Model sunlight

How strongly the sun lights the models, 0 to 1.5. Default 0.78, the original's.

> wkbai: the original lights models corner by corner, with the two values Model ambient light and Model sunlight. Highlights, reflections and surface detail are wkbai's own.
