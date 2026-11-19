---
title: Camera
ids: [ui/frontend/options/camera, option/scrollSpeed#scroll-speed, option/shiftScrollFactor#scroll-speed-held-faster-shift, option/minHeight#lowest-height-above-ground, option/cameraMinHeightAboveGround#lowest-height-above-ground, option/maxHeight#highest-camera-height, option/cameraMaxHeight#highest-camera-height, option/fixedCamera#fixed-camera-no-free-look, option/cameraSnapping#camera-snapping-minimap]
order: 30
---

# Camera

How the camera moves over the battlefield. How to steer it is explained in [the camera](help:controls/camera).

![Controls > Camera](shot:options-camera)

## Scroll speed

How fast the view scrolls with the arrow keys or W, A, S, D (and at the window's edge, if that is on): 0.1 to 5 times the usual speed, default 1.

## Scroll speed held faster (Shift)

How many times faster it scrolls while you hold Shift: 1 to 10, default 3.

## Lowest height above ground

How close to the ground the camera may come, 0.5 to 50 units, default 3.

## Highest camera height

How high the camera may rise, 100 to 3,000 units, default 1,000. High values let you look over the whole map; the View distance on the [View and fog](../graphics/view-fog.md) page decides how much of it is drawn.

## Fixed camera (no free look)

Off by default. On: the original's fixed camera. It keeps its height above the ground, zooming tilts it to a set angle, and the middle mouse button only turns it round. Off: the free camera, which can look in any direction.

## Camera Snapping (minimap)

Off by default, as in the original. Off: a click on the minimap glides the camera there in about a third of a second. On: the camera jumps there at once. Dragging on the minimap follows the mouse either way.

> wkbai: Camera Snapping is the original's setting, and Fixed camera is the original's camera (its free look is the setting the other way round). The other values are wkbai's own.
