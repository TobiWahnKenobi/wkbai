---
title: wkbai's map format
ids: [ui/editor/map-format, ui/editor/map-folder, mechanic/map-format, mechanic/map-folders, mechanic/map-authoring#designing-maps-with-an-ai-assistant, mechanic/map-catalog#the-catalog-and-describe, mechanic/map-terrain#the-land-from-a-plan, mechanic/map-materials#the-ground-from-rules, mechanic/map-scatter#woods-and-resources-from-rules, mechanic/generated-transitions#generated-transitions, mechanic/far-land#stored-far-land, mechanic/ground-rules#the-ground-from-rules, mechanic/terrain-generator#the-land-from-a-plan, mechanic/heightmap-import#heightmaps-and-real-land, mechanic/elevation-import#heightmaps-and-real-land, ui/editor/import#importing-an-original-map, ui/editor/compile#the-build-folder]
order: 95
---

# wkbai's map format

wkbai keeps the maps it makes in a format of its own: a **map folder**, `<name>.wkbai`, that holds the map, its level and its water together. The [map editor](index.md) saves into one, and wkbai plays the original game's maps as well as map folders. This page is for map makers; to play a map, you only need to find it in [Skirmish](../frontend/skirmish/settings.md#the-map-list).

## Why a folder

- **One thing to share.** The map, its level and its water design travel together.
- **Your hand edits kept apart.** The folder keeps what a map maker changed by hand in layers of its own, on top of what the map was made from. When the map is built again from its source (by a tool, or after an import), your height strokes, painted tiles and moved objects stay.
- **No old limits.** Any number of lakes, finer heights, and a speed for each river.

The original game can't read map folders. To play a map in the original, use [Export for the original](saving.md#export-for-the-original).

## What's in a map folder

| File | What |
|---|---|
| `map.json` | The map's settings: its size, edge, height scale, sky, sun and fog, its textures, its ground's material, its players, optionally its own day, seasons and weather (below), and optionally the plan its land is made from ([the land from a plan](#the-land-from-a-plan)) |
| `heights.png` | The heights, as a 16-bit grey picture with a point for each corner of a 5 m tile; north at the top |
| `heights.hand.png` | What the editor's brushes added to the heights |
| `layers\source.png` | A heightmap or real land the heights were made from (optional, see [heightmaps and real land](#heightmaps-and-real-land)) |
| `tiles.hand.json` | The tiles painted by hand |
| `water.json` | The lakes (any number) and the [water design](water.md): surface, sea regions, waterways and their speeds, waterfalls |
| `level.txt` | The level: players, objects, zones, aliases, and which objects were made by the map's rules and which by hand |
| `minimap.png` | The minimap's picture (optional) |
| `build\` | Made by wkbai, never edited (see [the build folder](#the-build-folder)) |

The files are plain JSON, text and PNG pictures, so a map maker can also change them with other tools.

## Where wkbai finds them

- `saved\Maps` in your [own folder](../../files.md#your-own-folder), where the editor saves;
- the `Maps` folder of an enabled [mod](../../mechanics/mods.md);
- the game folder's `saved\Maps`.

The skirmish lobby and the Map Editor list each folder once. A folder named "2 Player Green Vale" is listed among the skirmish maps as "Green Vale (2)" and found by the Players filter; under any other name it is at the end of the list, with the filter on All, with one player position for each of its starts. A level of the same name as a folder is not listed again.

## The map's own day and weather

A map's `map.json` may carry an `environment` block: whether the map has day and night and how long a day is, the hour and moon it starts with, whether it has seasons, how long they are, which comes first and how harsh they are, and how often the weather changes and which weathers may come. A new game on the map from the lobby starts with these in place of the [Seasons & Weather tab](../frontend/skirmish/weather.md)'s settings that were left at their defaults. A setting changed in the lobby stays as it was set there.

## The land from a plan

Instead of painting every hill by hand, a map maker (or an AI assistant) can describe the land in `map.json` and let wkbai make the heights from it:

- **`terrain`:** the land in general: its base height, the sea's level and how wide its beaches are, how rough the land is (noise), and how much water and weather have worn it down (erosion).
- **`features`:** a list of shapes, each at a position in metres, as objects are placed: **hill**, **range** (a ridge along a line, with passes through it), **plateau** (with ramps up), **valley**, **basin**, **coast**, **island**, **river** (with fords), **pass**, **terraces** and **flat** (a level site, for example for a base). Each may have a name and a note of what it is for ("player 1's base", "the ford").

`wkbai --author terrain "<folder>"` turns the plan into `heights.png`: the shapes, then the roughness and the wearing, then the passes, ramps and river beds carved last, so that they stay passable. The same plan and seed always give the same land. Where the sea is, the folder's water gets a lake. It answers with numbers about the land: its heights, how steep it is, how much of it units can walk on (most units can stand on slopes up to 45 degrees), and for each shape whether it came out as asked: a plateau flat enough to build on, a pass or ramp not too steep, a river that never runs uphill. `wkbai --author terrain-stats "<folder>"` gives the same numbers for the land as it is now.

**Rivers with water:** a river in the plan with `"water": true` gets its water too: one sloped surface that runs down its course, with rapids, cascades and falls where the land drops ([waterfalls](../../options/graphics/water.md#waterfalls)). `grade` smooths its bed into an even fall over that many metres; `"trough": false` leaves out the broad valley it carves first, for rivers in real land that has its own valleys. The water never stands above its banks, and lakes on the land keep their rims. Where the land beside it is flat and lower than the water, it gets a low natural bank (at most half a metre over the water), so the water ends along the river's own curve instead of in steps of the tile grid. Where the bank would have to rise more than 0.75 m, the land is left as it is; the build's report (`terrain.generated.riverBanks`) counts those places and names every 50th (`leftLowAt`).

**Your hand edits stay.** The plan only ever writes `heights.png`. What you changed with the [height brushes](terrain.md) is in `heights.hand.png`, which is never touched, and is added on top as before.

## The ground from rules

The ground's textures can come from rules too, in `map.json`'s `ground`: a preset (temperate, nordic or dry), the material where no rule holds, and a list of rules, the first that holds wins. A rule names a material (such as grass, rock, sand or snow) and when it applies: by height, height above the sea, slope, how the land curves, how wet it is, near a river or the sea, a painted mask, and so on. `wkbai --author materials "<folder>"` paints the tiles from them: it picks a material for each point, then a picture for each tile whose sides match its neighbours' by the texture database's own edge rules, so that one ground passes into the next without seams. Tiles you painted by hand stay. It answers with how much of each material there is and where tiles didn't fit, with `--png` also as a map from above. `wkbai --author material-presets` lists the materials and the presets.

## Generated transitions

The game's textures have no transition for many pairs of grounds, such as grass to rock or sand to hillside, and none for three grounds meeting in one tile. Where the ground rules need one, wkbai makes it:

- **Made on your machine:** blended from the game's own pictures (the HD pack's when it is on) the first time the game loads its ground textures, and kept in `saved\Maps\Map_Textures\Generated` in your user folder. They are made again on the next load when a picture they come from changes, for example when you switch the HD pack or a mod's ground pictures on or off. Deleting that folder makes them all anew. They are never shipped, and the game's own transitions are always used first; a generated one only fills a pattern the game lacks.
- **Edges the rules leave out:** two pictures the texture database's edge rules don't allow side by side are still taken when their pixels meet well enough that no seam shows.

With both, the tiles where no picture fits are rare: on large maps of real, hilly land a few dozen, where they had been thousands. The build's report still names them.

## Woods and resources from rules

`map.json`'s `scatter` places trees and resources for the map by rule each time it is built, as the Neutral Player's: forests on chosen materials, with their density, spacing, clumping, slope and distance from the bases, up to a most; gold and stone in groups near the bases or over the map. A forest's mix of trees can change with the height (`objectsHigh`), or follow a painted layer with `"highMask"` (for example a picture of where the pines are). Nothing is put on water, on steep ground or on another object. The build's report counts what was placed.

## Stored far land

A map folder can bring the land around it: `farland.png` (16-bit heights, north at the top) and `farland.json` (where its south-west corner lies, its cell size, its lowest and highest heights and its sea level). With [Extend map](../../options/graphics/terrain-grass.md#extend-map) on, that land is drawn around the map in place of made-up hills and coasts, blending in from the map's edge.

The materials are data: `Maps\Materials\All_Textures.json` for the game's textures; a mod or texture set brings its own file of the same kind for its texture database.

## Heightmaps and real land

- **A heightmap picture** (`wkbai --author heightmap`): a grey picture (PNG of 8 or 16 bits, a raw file, an SRTM `.hgt` or a GeoTIFF), cropped and fitted to a range of heights, becomes the base of the plan (`layers\source.png`). The plan's shapes are blended with it, so rivers and passes can be carved into it.
- **Real land** (`wkbai --author elevation`): elevation data of a real place, from Copernicus GLO-30 GeoTIFF or SRTM `.hgt` files, or for Great Britain the Ordnance Survey's OS Terrain 50 (`.asc` tiles on the British National Grid, `"source": "os-terrain-50"`), fitted to the map: squeezed to the map's size, its slopes kept or made steeper, flattened where too steep to play, and the sea found. wkbai never downloads anything: without the files, `wkbai --author elevation-tiles` names the tiles the place needs, with where to get them, their size and their licence. The data's licence and the credit it asks for go into `map.json`'s references; keep them when you share the map. Maps of real places may carry more credits, for example for their woods and rivers, under `region.data`.

## The build folder

Before a map folder can be played, wkbai builds it into `build\` inside the folder: a map, a level and the water in the form the game loads, with a report of what it found. It does this by itself when the folder is new, when something in it changed, or when a new version of wkbai builds maps differently (as the one that brought [the ground from rules](#the-ground-from-rules) does), so the first load after a change takes a moment longer. In the skirmish lobby this happens in the background, under a "Preparing the map" window with a progress bar ([the map list](../frontend/skirmish/settings.md#the-map-list)). You can delete `build\` at any time, or leave it out when you share the map.

## Importing an original map

`wkbai --author import "<level>"` turns an original map (for example `2 Player 4 Peaks.lvl`) into a map folder in `saved\Maps`, with every tile kept as it was. An imported map plays exactly as the original. Saving an original map from the editor also makes a map folder of it, with your changes in it.

## The catalog and describe

Two commands help a map maker see what can go on a map and what is on one. They run without opening the game's window and write their answer as JSON:

- `wkbai --author catalog`: everything that can go on a map, read from the game set and the texture databases that are loaded (so a mod's units and textures are in it): every unit, building and object by kind, with its footprint and what it holds; every group of ground textures with its kind of terrain, how units pass over it and which textures may border it; the skies, the trees and the limits of a map's size. The [generated transitions](#generated-transitions) are counted (`generatedGroups`), not listed with the game's groups. `--mod <name>` reads one mod, `--mod none` the game alone.
- `wkbai --author describe "<map>"`: a map, either a folder or an original level, in numbers: its size, its heights and slopes, its water, how much of each ground it has, and its objects by kind and player. `--png <file>` also draws it from above.
- `wkbai --author <folder>` builds a map folder; its report, with any warnings and the tiles they are about, is `report.json` in its `build` folder.

## Designing maps with an AI assistant

The source code's `tools\agent` folder has a second server for AI assistants such as Claude Code, `wkbai_author_mcp.py`, beside the one for [agent control](../../options/agent-control.md). It lets an assistant design maps: look things up in the catalog, describe existing maps, make a new map folder, import an original map, read and change a folder's `map.json`, plan and make the land ([the land from a plan](#the-land-from-a-plan)), paint the ground by rules ([the ground from rules](#the-ground-from-rules)), bring in a heightmap or real land ([heightmaps and real land](#heightmaps-and-real-land)), and build it. Before it uses real elevation data, it tells you which files it needs and where they come from, so you decide whether to fetch them. It never opens the game's window. Its maps go into a work folder of its own, or into your own folder's `saved\Maps` so that Skirmish lists them. The file's own notes say how to set it up.

## In multiplayer

A map folder counts as game logic, like a level: every player needs the same copy, or the lobby says the logic doesn't match ([mods in multiplayer](../../mechanics/mods.md#mods-in-multiplayer)).

> wkbai: the original game has no map folders; its maps are a `.bcm` map and a `.lvl` level in its archives. wkbai still reads those, and the editor can still save them ([saving in the original format](saving.md#saving-in-the-original-format)).
