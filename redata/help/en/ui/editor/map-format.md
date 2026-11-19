---
title: wkbai's map format
ids: [ui/editor/map-format, ui/editor/map-folder, mechanic/map-format, mechanic/map-folders, mechanic/map-authoring#designing-maps-with-an-ai-assistant, mechanic/map-catalog#the-catalog-and-describe, ui/editor/import#importing-an-original-map, ui/editor/compile#the-build-folder]
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
| `map.json` | The map's settings: its size, edge, height scale, sky, sun and fog, its textures, its ground's material, its players, and optionally its own day, seasons and weather (below) |
| `heights.png` | The heights, as a 16-bit grey picture with a point for each corner of a 5 m tile; north at the top |
| `heights.hand.png` | What the editor's brushes added to the heights |
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

The skirmish lobby and the Map Editor list each folder once. A folder named "2 Player Green Vale" is listed among the skirmish maps as "Green Vale (2)" and found by the Players filter; under any other name it is at the end of the list, with the filter on All. A level of the same name as a folder is not listed again.

## The map's own day and weather

A map's `map.json` may carry an `environment` block: whether the map has day and night and how long a day is, the hour and moon it starts with, whether it has seasons, how long they are, which comes first and how harsh they are, and how often the weather changes and which weathers may come. A new game on the map from the lobby starts with these in place of the [Seasons & Weather tab](../frontend/skirmish/weather.md)'s settings that were left at their defaults. A setting changed in the lobby stays as it was set there.

## The build folder

Before a map folder can be played, wkbai builds it into `build\` inside the folder: a map, a level and the water in the form the game loads, with a report of what it found. It does this by itself when the folder is new or something in it changed, so the first load after a change takes a moment longer. You can delete `build\` at any time, or leave it out when you share the map.

## Importing an original map

`wkbai --author import "<level>"` turns an original map (for example `2 Player 4 Peaks.lvl`) into a map folder in `saved\Maps`, with every tile kept as it was. An imported map plays exactly as the original. Saving an original map from the editor also makes a map folder of it, with your changes in it.

## The catalog and describe

Two commands help a map maker see what can go on a map and what is on one. They run without opening the game's window and write their answer as JSON:

- `wkbai --author catalog`: everything that can go on a map, read from the game set and the texture databases that are loaded (so a mod's units and textures are in it): every unit, building and object by kind, with its footprint and what it holds; every group of ground textures with its kind of terrain, how units pass over it and which textures may border it; the skies, the trees and the limits of a map's size. `--mod <name>` reads one mod, `--mod none` the game alone.
- `wkbai --author describe "<map>"`: a map, either a folder or an original level, in numbers: its size, its heights and slopes, its water, how much of each ground it has, and its objects by kind and player. `--png <file>` also draws it from above.
- `wkbai --author <folder>` builds a map folder; its report, with any warnings and the tiles they are about, is `report.json` in its `build` folder.

## Designing maps with an AI assistant

The source code's `tools\agent` folder has a second server for AI assistants such as Claude Code, `wkbai_author_mcp.py`, beside the one for [agent control](../../options/agent-control.md). It lets an assistant design maps: look things up in the catalog, describe existing maps, make a new map folder, import an original map, read and change a folder's `map.json`, and build it. It never opens the game's window. Its maps go into a work folder of its own, or into your own folder's `saved\Maps` so that Skirmish lists them. The file's own notes say how to set it up.

## In multiplayer

A map folder counts as game logic, like a level: every player needs the same copy, or the lobby says the logic doesn't match ([mods in multiplayer](../../mechanics/mods.md#mods-in-multiplayer)).

> wkbai: the original game has no map folders; its maps are a `.bcm` map and a `.lvl` level in its archives. wkbai still reads those, and the editor can still save them ([saving in the original format](saving.md#saving-in-the-original-format)).
