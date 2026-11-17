---
title: Sky and weather
ids: [ui/frontend/options/sky, option/sky#sky, option/skyModel#sky-model, option/cloudShadows#cloud-shadows, option/dynamicClouds#clouds, option/cloudQuality#cloud-quality, option/cloudDetail#cloud-detail, option/weatherEffects#weather-and-season-effects, option/snowQuality#snow-quality, option/iceQuality#ice-quality, option/rainbows#rainbows, option/heatShimmer#heat-shimmer-in-a-heatwave]
order: 20
---

# Sky and weather

The sky and its clouds, and how much of the weather is drawn. Which weather a game has, and whether its clouds are fair or stormy, is chosen in the lobby's [Seasons & Weather tab](../../ui/frontend/skirmish/weather.md); this page only decides how it looks.

![Graphics > Sky and weather](shot:options-sky)

## Sky

On (the default): the sky is drawn. Off: no sky is drawn behind the landscape, and Sky model is greyed out.

## Sky model

How the sky behind the volumetric clouds is drawn:

- **Normal:** the original's painted sky.
- **Physical** (the default): a sky worked out from the sun's place: the deep blue overhead, the haze at the horizon in the sky's own colours, warmer towards a low sun, and the earth's shadow rising after sunset.

Needs Sky on and Clouds set to Volumetric. **Cost:** none to speak of.

## Cloud shadows

On (the default): the shadows of clouds pass over the land. Off: no cloud shadows, and the Clouds settings below are greyed out.

## Clouds

How the clouds are made. Needs Cloud shadows.

- **Original:** the original's cloud shadows, a pattern scrolling over the ground.
- **Moving shadows:** a field of clouds drifting with the wind; its shadows fall where the sun puts them.
- **Light shafts:** the same, with shafts of sunlight through the air between the shadows.
- **Volumetric** (the default): real clouds in the sky as well, lit through their depth, with bright edges towards the sun. They form, drift and change with the weather.

**Cost:** the first three cost almost nothing (about 0.1 ms). Volumetric at Medium quality adds about 0.4 ms.

## Cloud quality

With Volumetric clouds: how finely the clouds are worked out.

- **Low:** at a quarter of the screen's size, the fewest steps.
- **Medium** (the default): at half size.
- **High:** at full size, more steps.
- **Ultra:** at full size, the most steps and finer light.

**Cost:** the heaviest graphics setting. Against Medium, Low saves about 0.3 ms; High costs about 1.7 ms more and Ultra about 2.5 ms more when much sky is in view.

## Cloud detail

With Volumetric clouds: how detailed the clouds' shapes are.

- **Low:** one even density through the cloud layer.
- **Medium** (the default): thick and thin clouds; a cloud deck with denser and thinner parts.
- **High:** also finer billows on the tops, and storm clouds as towers under wide anvils.
- **Very high** and **Ultra:** also the billows shading each other.

**Cost:** small next to Cloud quality; the higher levels add a few texture reads per pixel of sky.

## Weather and season effects

How much of the weather is drawn, when the game has [seasons and weather](../../ui/frontend/skirmish/weather.md#seasons-and-weather):

- **Low:** a third of the rain and snow.
- **Medium:** two thirds; snow on roofs and models, wet models darker.
- **High** (the default): all of it; footprints in snow and mud.
- **Ultra:** half as much rain and snow again, and more footprints that last longer.

Floods, the snow and ice on the ground and the light of day and night are drawn at every level: they are part of the game, not decoration. **Cost:** mostly on the processor, in heavy rain or snow.

## Snow quality

How lying snow looks:

- **Low:** plain white patches.
- **Medium** (the default): softer patches that keep their cover far into the distance, bluer where the snow is thin.
- **High:** also the sun glinting in the snow.

**Cost:** very small.

## Ice quality

How frozen water looks:

- **Low:** a plain grey-blue.
- **Medium** (the default): lit, mirroring the sky, with cracks.
- **High:** also a glossy highlight, glittering frost and bubbles frozen in.

**Cost:** very small.

## Rainbows

On (the default): a rainbow can appear opposite the sun when the sun is low and shines while rain falls or has just fallen.

## Heat shimmer in a heatwave

On (the default): in a heatwave, the air over the distance shimmers very slightly by day.

> wkbai: the original has a painted sky and a scrolling cloud shadow, which Original keeps. Everything else on this page is wkbai's own.
