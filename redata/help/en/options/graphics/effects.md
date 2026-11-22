---
title: Effects
ids: [ui/frontend/options/effects, option/bloom#bloom, option/motionBlur#camera-motion-blur, option/tiltShift#tilt-shift]
order: 90
---

# Effects

Finishing touches on the picture after it is drawn.

![Graphics > Effects](shot:options-effects)

## Bloom

On (the default): the brightest parts of the picture (the sun on water, fires, lit windows at night) glow softly into their surroundings. **Cost:** small. How bright something must be and how strongly it glows can be set in the [settings file](../config-file.md#bloomthreshold).

## Camera motion blur

How much the picture blurs while the camera moves, as a film camera's would: 0 (off, the default) to 1 (the whole of each frame's movement). It follows the camera's movement only, not the units' own. **Cost:** small.

## Tilt-shift

Makes the battlefield look like a model on a table: a sharp band across the middle of the picture, blurring more and more above and below it. 0 (off, the default) to 1 (the strongest blur). **Cost:** small.

> wkbai: none of these is in the original.
