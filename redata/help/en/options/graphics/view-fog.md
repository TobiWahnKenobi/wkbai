---
title: View and fog
ids: [ui/frontend/options/view, option/wholeMapView#view-distance-whole-map, option/viewDistance#view-distance, option/farClip#view-distance, option/fogOff#fog-off, option/fogCustom#own-fog-distances, option/fogNear#fog-near, option/fogFar#fog-far-opaque, option/fogStart#fog-start-of-fog-end, option/fogColourOverride#own-fog-colour, option/fogColourR#fog-colour-red, option/fogColourG#fog-colour-green, option/fogColourB#fog-colour-blue, option/edgeFog#map-edge-fog, option/edgeFogStart#edge-fog-starts, option/edgeFogOpaque#edge-fog-opaque-at, option/fieldOfView#field-of-view]
order: 10
---

# View and fog

How far you can see, the haze in the distance, and how wide the camera's view is. Distances are in the game's units (a tile of the map is 5 units).

![Graphics > View and fog](shot:options-view)

## View distance: whole map

On: everything on the map is drawn, however far away. Off (the default): the View distance below applies.

- **Cost:** with the camera high above a large map, a lot more is drawn. On today's computers usually little.

## View distance

How far from the camera the landscape and everything on it is drawn: 100 to 50,000, default 350 (the original's). The distance fog grows with it, so the far edge fades out instead of ending sharply. Greyed out while View distance: whole map is on.

- **Cost:** more to draw the further it reaches; noticeable only with the camera high up.

## Fog off

On: no distance fog at all; the landscape stays clear up to the view distance. Off by default.

## Own fog distances

On: the fog uses the two distances below instead of following the view distance. Off by default. Greyed out while Fog off is on.

## Fog near

With Own fog distances: where the fog begins, 1 to 50,000. Default 166.

## Fog far (opaque)

With Own fog distances: where the fog has hidden everything, 1 to 50,000. Default 332. Keep it within the view distance, or the landscape ends before the fog does.

## Fog start (of fog end)

Without Own fog distances: where the fog begins, as a part of the distance where it ends: 0 to 0.99, default 0.5 (half way). Smaller values give a long, soft haze; larger ones a short band near the end.

## Own fog colour

On: the fog has the colour set below instead of the map's own. Off by default.

## Fog colour: red

With Own fog colour: the fog colour's red part, 0 to 1. Default 0.62.

## Fog colour: green

With Own fog colour: the green part, 0 to 1. Default 0.77.

## Fog colour: blue

With Own fog colour: the blue part, 0 to 1. Default 0.90. A swatch after this slider shows the colour the three make.

## Map-edge fog

On (the default): the land outside the playable area fades into fog before the map's mesh ends, so you never see its edge drop away. With [Extend map](terrain-grass.md#extend-map) on, the drawn land reaches further out beyond the map.

## Edge fog starts

With Map-edge fog: where the fog begins, measured across the band between the playable area's edge (0) and the end of the map's mesh (1). -1 to 0.95, default -0.25: a little inside the playable area.

## Edge fog opaque at

With Map-edge fog: where it hides everything, on the same scale, 0 to 0.95. Default 0.5, half way out.

## Field of view

How wide the camera sees, from top to bottom, in degrees: 20 to 100, default 52. Narrow values look like a telephoto lens, wide ones show more around you.

> wkbai: the original has a fixed view distance and fog. All of these are wkbai's own; the defaults give the original's look.
