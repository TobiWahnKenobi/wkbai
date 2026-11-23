---
title: Terrain and grass
ids: [ui/frontend/options/terrain, option/terrainFresnel#ground-fresnel-sky-sheen, option/terrainTessellation#terrain-tessellation, option/pomTerrain#parallax-occlusion-mapping-terrain, option/pomTerrainDepth#parallax-depth-terrain, option/extendMap#extend-map, option/extendMapDetail#extend-map-detail, option/extendMapShadows#far-land-shadows, option/farLandCache#keep-the-far-land-on-disk, option/farLandCacheMB#far-land-cache-size-mb, option/windTrees#trees-in-the-wind, option/grass#grass, option/grassWind#grass-in-the-wind, option/grassDensity#grass-density, option/grassHeight#grass-height, option/grassDistance#grass-distance, option/bumpMapping#terrain-bump-mapping]
order: 50
---

# Terrain and grass

The ground: its shape and sheen, the land drawn beyond the map's edge, the grass on it, and how trees and grass move in the wind.

![Graphics > Terrain and grass](shot:options-terrain)

## Ground Fresnel (sky sheen)

How much the ground reflects the sky when you look across it at a low angle, as real ground does: 0 (off) to 1, default 0.35. **Cost:** about 0.1 ms when much distant ground is in view.

## Terrain tessellation

The ground is divided into finer pieces near the camera, smoothed between the map's height points, so hills are round instead of faceted. Buildings and units stand on the smoothed ground; the game's own heights don't change.

- **Off:** the original's ground.
- **Low:** up to 4 pieces per tile edge.
- **Medium** (the default): up to 8.
- **High:** up to 16.

Needs a graphics card that can tessellate. **Cost:** small at Medium, more at High with the camera low.

## Parallax occlusion mapping (terrain)

Gives the ground's textures real relief: stones, ruts and clods stand out and hide what lies behind them, and cast their own small shadows. It needs ground textures with height maps, from a mod or texture pack ([material maps](../../mechanics/mods.md#material-maps-for-the-ground)); the game's own textures have none, so without such a pack this setting changes nothing.

- **Off** (the default).
- **Low**, **Medium**, **High:** more steps along each view and sun ray, so the relief is finer and its edges cleaner.

Ground far enough away that its relief would be less than a third of a pixel deep is drawn without it. **Cost:** up to about 0.5 ms with near ground filling the screen at High, about 0.2 ms over usual views (Radeon RX 6900 XT, 1920 x 1080).

## Parallax depth (terrain)

With Parallax occlusion mapping (terrain) on: how deep the ground's relief is, 0 to 4 times its real depth (default 1, up to 12 cm; 4 makes it 48 cm). At the game's usual zoom the real depth is shallow: raise it to see the relief more. **Cost:** none.

## Extend map

The land goes on beyond the map's edge, with hills, coasts and seas made to match the map, so the world doesn't end at the border. It is only drawn: units can't go there, and the game itself is unchanged. A map that brings its own far land (a map of a real place, for example) shows that instead ([stored far land](../../ui/editor/map-format.md#stored-far-land)).

- **Off** (the default).
- **Near:** a band of land around the map, 64 tiles wide.
- **Far:** coarser land further out, to about 3.6 km beyond the map.
- **Horizon:** to about 30 km, as far as the eye can see.

A change is applied when you press OK (and when a map loads), since building the far land takes a few seconds. Not shown in the map editor. **Cost:** almost nothing per frame (0.1 to 0.2 ms); the building time is in Extend map detail.

## Extend map detail

With Extend map at Far or Horizon: how fine the far land is: **Low**, **Medium** (the default), **High** or **Ultra**. Each level is twice as fine as the one before. **Cost:** the time to build it when a map loads: a few seconds at Medium and High, up to about 12 seconds at Ultra. Keeping it on disk (below) saves that time the next time.

## Far land shadows

With Extend map at Far or Horizon; on by default. The far land's hills shade each other in the low sun. They take a little longer to build.

## Keep the far land on disk

With Extend map at Far or Horizon; on by default. The far land, once built, is kept in your user folder, and built again only when the map or a setting that changes it changes. The next game on the same map starts sooner.

## Far land cache size (MB)

With Keep the far land on disk: the most space it may take, 256 to 32,768 MB, in steps of 256; default 4,096. When it is full, the far land used least recently goes first.

## Trees in the wind

How the trees and bushes move in the game's [wind](../../ui/frontend/skirmish/wind.md). The trunk stays rooted and the crown and branches bend downwind, more in a stronger wind and in the gusts. The trees sway in a few dozen groups, so neighbours don't move in step. Their shadows bend with them, the [ray-traced shadows](shadows-light.md#ray-traced-shadows) too.

- **Off:** the trees stand still.
- **Sway:** the trees bend and sway.
- **Sway and leaves** (the default): also the twigs and leaves flutter.

With the lobby's Wind off, the trees and grass still move as in a steady gentle breeze. **Cost:** none to speak of (at most about 0.04 ms with hundreds of trees in view).

Large forests are drawn by the graphics card itself: trees that stand still stay there, and it picks and draws the ones in view, with their shadows. A map with 30,000 trees draws several times faster than before. A tree that is being chopped, is selected or is under the mouse is drawn the old way while it is. If forests look wrong on your graphics card, [`forest`](../config-file.md#forest) switches this off.

## Grass

On (the default): blades of grass grow on green ground near the camera.

## Grass in the wind

With Grass: how the blades move in the game's [wind](../../ui/frontend/skirmish/wind.md).

- **Off:** the blades stand still.
- **Waves:** the blades lean downwind, with waves of gusts running over the grass.
- **Field** (the default): the wind near the ground. It is calmer in the lee of hills, woods, buildings and walls, and stronger on crests. Gusts drift over in patches, the wind's way swirls a little, and the tips flutter.

**Cost:** Waves almost nothing; Field about 0.14 ms more.

## Grass density

With Grass: how many blades grow on each square unit of ground, 0 to 24, default 3. **Cost:** small; more blades mean more to draw with the camera low.

## Grass height

With Grass: how tall the blades are, 0.2 to 3 times their usual height, default 1.

## Grass distance

With Grass: how far from the camera grass is drawn, 10 to 250 units, default 80. **Cost:** the further, the more blades to draw.

## Terrain bump mapping

On (the default): the ground's texture shows small bumps in the light, as in the original.

> wkbai: the original's ground has the bump mapping and nothing else on this page. The original has no wind.
