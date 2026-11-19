---
title: The map editor
ids: [ui/editor, ui/hud/map-editor, ui/editor/window#the-map-editor-window, ui/editor/modes#the-modes, ui/editor/opening#opening-and-closing-it, ui/editor/mapname#map-name, ui/editor/undo#undo-and-redo, ui/editor/redo#undo-and-redo, ui/editor/status#messages, ui/editor/mouse#mouse-and-keys, ui/editor/limits#limits]
order: 30
---

# The map editor

wkbai's map editor changes a map while it is open in the game: its hills and valleys, its ground, its water, its objects and its players. You save the result as a new map of your own, which you can then play in Skirmish.

![The map editor's window beside the map, in the Terrain mode](shot:editor-terrain)

- **New here?** [Make a skirmish map](make-a-map.md) walks you through it from start to finish.
- **Each mode:** [Terrain](terrain.md), [Textures](textures.md), [Water](water.md), [Objects](objects.md), [Players](players.md), [Zones](zones.md), [Scripts](scripts.md) and [Regions](regions.md).
- **Keeping it:** [saving, testing and sharing](saving.md).

## Opening and closing it

1. In the main menu, click **Editors**, then **Map Editor**.
2. Choose a map from the [list of maps](../frontend/map-editor.md) and click **Edit** (or double-click the map).

The map opens paused, with the editor's window on the left. Your edits change the game as it stands; nothing is written to disk until you [save](saving.md#save-map).

- **F8** ([`mapEditor`](help:key/mapEditor)) hides the editor's window and shows it again. The cross on the window's title bar hides it too.
- F8 works only on a map opened this way, in single player. In a normal game, a skirmish, multiplayer or a replay it does nothing.
- To leave, use the in-game menu (Esc), as in any game. Anything you did not save is lost.

While the editor is on:

- **No fog of war:** you see the whole map.
- **No Extend map:** the land beyond the map's edge isn't drawn, so you see where the map really ends.
- **The developer tools work:** the [server window (Ctrl+F1), the debugger (F2) and the environment panel (F9)](../../controls/developer-tools.md), as with cheat mode on.

## The map editor window

The window is called **Map editor (F8)**. You can move it and change its size. From top to bottom:

1. **The modes:** a row of buttons that choose what you work on (below).
2. **The mode's panel:** its tools and settings. Each mode has a page of its own.
3. **The common part:** the map's name, undo and redo, saving and the map-wide buttons. See [Map name](#map-name), [Undo and redo](#undo-and-redo) and [saving](saving.md).
4. **Messages:** what the last action did, and the results of **Check** (below).

## The modes

| Mode | What you work on | Page |
|---|---|---|
| **Terrain** | The heights: raise, lower, smooth and flatten the ground | [Terrain](terrain.md) |
| **Textures** | The ground's pictures, tile by tile (grass, rock, sand, snow...) | [Textures](textures.md) |
| **Water** | Lakes and seas, and how the water looks and flows | [Water](water.md) |
| **Objects** | Buildings, units, trees, outcrops, props and markers: place, move, turn, remove, edit | [Objects](objects.md) |
| **Players** | Each player's starting resources and diplomacy | [Players](players.md) |
| **Zones** | Areas of tiles that a mission's scripts can test | [Zones](zones.md) |
| **Scripts** | The level's game set file and its aliases | [Scripts](scripts.md) |
| **Regions** | The named areas the timeline (F7) uses | [Regions](regions.md) |

## Mouse and keys

The camera moves as in the game: see [the camera](../../controls/camera.md).

| What | How | Where |
|---|---|---|
| Use a brush (paint heights, textures, water surface, zones, regions) | Hold the left mouse button on the ground and move | Terrain, Textures, Water (Surface), Zones, Regions |
| Place a lake, an object, a waterway point, a waterfall's end | Left click on the ground | Water, Objects |
| Take away the last waterway point; remove the nearest waterfall | Right click | Water |
| Select | Click, or drag a box, as in the game | Objects (Select, move, turn), Scripts |
| Move the selection | **G**, then left click to put it down, right click to put it back | Objects |
| Turn the selection by 15° | **R**; **Shift+R** turns it back | Objects |
| Remove the selection | **Delete** | Any mode |
| Undo | **Ctrl+Z** | Any mode |
| Redo | **Ctrl+Y** or **Ctrl+Shift+Z** | Any mode |
| Show or hide the editor | **F8** | Any mode |

While the mouse is over the editor's window, clicks and brushes don't reach the map. Keys don't act while you type in a text field. You can change these keys: see [the key list](../../controls/keys.md#map-editor-f8) and [changing the keys](../../controls/keys.md#changing-the-keys).

## Map name

**Map name** is the name your map is saved under ("My map" at first). It is also the name it has in the [Map Editor](../frontend/map-editor.md) list and in Skirmish.

- A name can't contain `\ / : * ? " < > |`.
- A name of the form **"4 Player Green Vale"** puts your map among the skirmish maps, as "Green Vale (4)", with four start positions in the lobby. See [Name it for Skirmish](saving.md#name-it-for-skirmish).

## Undo and redo

**Undo (Ctrl+Z)** takes back your last change, **Redo (Ctrl+Y)** does it again. Beside them, "3 / 1 steps" counts the steps you can undo and redo.

- **Kept:** the last 30 steps each way. A new change after an undo clears the redo steps.
- **What a step is:** one brush stroke (from pressing the button to letting go), one lake added or removed, objects placed, removed, moved or turned, a change to an object's name, owner or items, a change to an alias, a water design change, a region change.
- **Not undone:** the Players mode's resources and diplomacy, and zone changes. [New blank map](saving.md#new-blank-map) and [New map of another size](saving.md#new-map-of-another-size) clear all undo steps.
- **An object removed and put back by undo** returns with its type, owner, place and facing, but without its items and orders.

## Messages

Under the buttons the editor says what the last action did: "Lake added", "Placed Manor", "Saved My map.lvl (in Skirmish)", or what went wrong ("Not a valid name", "Select objects first"). The results of **Check** are listed above it in orange. See [Check](saving.md#check).

## Limits

- **Single player only,** on a map opened from the Map Editor list.
- **Players:** you can't add or remove players. A map keeps the players of the map you started from. Choose a map with the number of players you want.
- **Start cameras:** you can't move a player's start camera (where its view begins). See [start positions](make-a-map.md#7-set-up-the-start-positions).
- **Size:** 16 to 1024 tiles a side, each tile 5 m ([New map of another size](saving.md#new-map-of-another-size)).
- **Lakes:** up to 1024; the original game sees the first 63.
- **Heights:** 256 whole steps; finer heights only in wkbai ([Fine heights](terrain.md#fine-heights)).
- **Not in the editor:** a unit's orders, and a mission's triggers. Triggers are written in the level's game set file ([Scripts](scripts.md)).

> wkbai: the original game has no map editor in the game. Maps from wkbai's editor are written in the original's file formats, and [Export for the original](saving.md#export-for-the-original) packs them so the original can load them.
