---
title: Water
ids: [ui/frontend/options/water, option/waterQuality#water, option/waterSetting#water, option/waterFlow#flowing-rivers, option/waterfalls#waterfalls, option/ssReflections#screen-space-water-reflections, option/rtReflections#ray-traced-water-reflections, option/cloudReflections#clouds-in-the-water]
order: 40
---

# Water

Seas, lakes and rivers, and the waterfalls and cascades where rivers drop.

![Graphics > Water](shot:options-water)

## Water

How the water is drawn. A click takes the next level.

- **Original:** the original's water: two layers of the map's water texture scrolling over each other.
- **Off:** a plain colour.
- **Very low:** the water texture with rippling waves and the sun's glint.
- **Low:** also the sky reflected in it (with [volumetric clouds](sky-weather.md#clouds), the clouds' own shapes and storm cells too, on lakes, the sea and the ice; off with [Clouds in the water](#clouds-in-the-water)), deeper water darker, cloud shadows, foam along the shores, different looks for rivers and the sea, and short reflections of the land.
- **Medium:** the open sea's look of High on a flat surface, with longer reflections.
- **High** (the default): the surface itself moves with waves, with foam on the crests and in the surf. Needs a graphics card that can tessellate; without one it looks as Medium.
- **Very high:** between High and Ultra.
- **Ultra:** finer waves further out, and the longest reflections.

**Cost:** on a view with a large river and a waterfall, Original took 4.8 ms a frame, High 5.4 and Ultra 5.7 (water and waterfalls together, Radeon RX 6900 XT).

## Flowing rivers

With Water at Low or above; on by default. Rivers flow downstream instead of rippling in place, following the map's own rivers. **Cost:** none to measure. Working out the flow takes a moment when a map with many rivers loads.

## Waterfalls

The waterfalls and the cascades where painted rivers drop, and the rapids, cascades and falls of a map folder's sloped rivers ([rivers with water](../../ui/editor/map-format.md#the-land-from-a-plan)):

- **Off:** none (the original has none either).
- **Low:** the falling sheet of water and the cascades.
- **Medium:** also foam on the water below and some mist.
- **High** (the default): also the view through the sheet, which tears into strands, more mist and spray. The see-through sheet needs Water at Low or above.
- **Ultra:** the finest detail, a swaying sheet, rings spreading from its foot, all the mist and spray.

**Cost:** part of the water's cost above; only where a waterfall is in view.

## Screen-space water reflections

With Water at Low or above; on by default. The water mirrors the land, the buildings and the units above it, as far as they are on the screen. Off: it mirrors the sky alone (and the ray-traced reflections, if on).

## Ray-traced water reflections

With Water at Low or above; off by default. Buildings, trees and units are mirrored even where they are off the screen or hidden, by tracing rays. The screen-space reflections still show the land. Needs a graphics card with ray tracing; see [Graphics](index.md#ray-tracing). **Cost:** about 0.3 to 0.4 ms, most of it building the scene for the rays each frame. With Ray-traced shadows on as well, the two together cost less, since the shadow map isn't drawn any more.

## Clouds in the water

With Water at Low or above and the Volumetric clouds; on by default. Lakes, the sea and the ice mirror the clouds' own shapes and a storm's towers, drifting with the clouds in the sky; the waves break them up. Off: the water mirrors the sky's colours alone, greyed evenly by the cloud cover. **Cost:** about 0.1 ms.

> wkbai: the original's water is the Original level. Everything above it, the flow and the waterfalls are wkbai's own.
