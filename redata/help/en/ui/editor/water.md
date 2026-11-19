---
title: Water
ids: [ui/editor/water, ui/editor/water/addlake#add-a-lake, ui/editor/water/depth#depth-m, ui/editor/water/removelake#remove-a-lake, ui/editor/water/surface#surface, ui/editor/water/waterways#waterways, ui/editor/water/waterfalls#waterfalls, ui/editor/water/seas#sea-regions-wind, ui/editor/water/save#save-water-design, ui/editor/water/clear#clear-design]
order: 30
---

# Water

The **Water** mode puts water on the map and says what kind of water it is. Its first two tools make the water itself: lakes and seas. The other three only change how the water looks and moves: rivers that flow, the waves of the seas, and waterfalls.

![The Water mode with a waterway and two sea regions drawn over the map](shot:editor-water)

## How water works

A map's water comes from **lake points**. From each point the water spreads over every tile around it that is lower than the water's level, as far as the ground stays lower. So:

- **Make the hollow first.** Lower the ground in the [Terrain](terrain.md) mode, then add a lake in the hollow. On flat ground even 1 m of water floods far.
- **A sea** is a lake point at sea level in low ground that is open to the map's edge.
- **The water follows the ground.** After every height stroke the lakes fill again. Dig a channel from a lake and the water runs into it, as long as the channel stays below the water.
- **Water blocks land units.** Units walk round it; only ships sail on it ([ships](../../mechanics/naval.md)).

## Add a lake

Click the ground: a lake point goes there, with its water level the ground's height at the click plus the **Depth**. The water fills everything around it that is lower.

## Depth (m)

How far above the clicked ground the water stands: 0.5 to 30 metres (3 at first). Below the tool, the editor counts the map's lakes.

## Remove a lake

Click on water: the lake that made it goes, and its water drains. Where two lakes of the same level meet, a second click removes the other one. A click on dry ground says "No water here".

## Surface

A brush that says what the water under it is. Hold the left button and paint; it changes water tiles only, never land.

- **Automatic:** wkbai decides from the shape of the water, as it does on maps without a design.
- **River:** flowing water with the river look.
- **Lake:** calm water with small waves.
- **Sea:** the waves of a sea region. Choose the region under **Sea region** (see [Sea regions](#sea-regions-wind)).
- **Radius (m):** the brush's size, 2.5 to 100 metres.

Painted tiles show over the map in colour: rivers light blue, lakes green, seas in their region's colour.

## Waterways

A waterway is a river's course, from its source to its mouth. The water within its width flows along it.

1. Click **New waterway**.
2. Click the ground at the river's source, then at points along its course, ending at its mouth. Each click adds a point.
3. A right click takes the last point away.
4. Set its **Width (m)** (2.5 to 100).

The list shows the map's waterways and their points. Click one to add points to it, change its width, or remove it with **Delete waterway**. Over the map, waterways are drawn as lines with arrows showing the flow; the chosen one is white.

A waterway only steers water that is there. To make the river itself, dig its bed below a lake's level, or paint river textures.

## Waterfalls

1. Click one end of the lip the water pours over, then the other end. The lip follows the tile edges along that line.
2. The water falls to the lower side: from the water above, or from a spring if there is none, into the water below or onto the ground at the foot of the slope.
3. A right click near a waterfall removes it.

Placed waterfalls are drawn as orange lines over the map.

**Also the waterfalls found in the map:** on at first. wkbai finds waterfalls by itself, where a pond lies behind a low ridge at a cliff's edge and the ground drops steeply to water below. Turn it off to keep only the ones you placed.

You see waterfalls only with the [Waterfalls](help:option/waterfalls) graphics setting above Off.

## Sea regions (wind)

Each sea region has its own wind, and so its own waves. A map can have up to 8. Each row shows:

- **its colour,** used for its painted sea over the map;
- **its name;**
- **its wind:** the way its waves run, in degrees (0 runs east, 90 south). An arrow over its sea shows it;
- **its waves:** their height, from x0 (calm) to x3;
- **x:** removes the region. Its sea goes back to the first region. The first region can't be removed.

**Add a region** adds one and selects it for the **Surface** brush. Paint a region's sea with **Surface** and **Sea**. Where two regions meet, their waves blend. The first region is also the wind of the seas left automatic, and of the lakes.

## Save water design

Writes the design (the surface, waterways, waterfalls and sea regions) for the map that is open now, as `Maps\<map>\<map>.water` in your user folder (shown below the button). This works for the game's own maps too, without changing them. **Save map** also writes the design, under your map's new name ([saving](saving.md#save-map)).

## Clear design

Removes the whole design: surface, waterways, waterfalls and regions. Lakes stay. Save afterwards to remove the file too. Undo brings the design back.

## Seasons change the water

With seasons on, the water you design lives through the year: floods swell rivers and waterfalls, droughts dry them, and winter freezes them so units can walk on the ice. See [seasons and weather](../../mechanics/seasons-weather.md).

> wkbai: the original game has no water designs, waterfalls or sea regions. It reads a map's lakes, but only the first 63.
