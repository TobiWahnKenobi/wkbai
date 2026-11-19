---
title: Saving, testing and sharing
ids: [ui/editor/saving, ui/editor/save#save-map, ui/editor/saveplay#save-and-play, ui/editor/check#check, ui/editor/blank#new-blank-map, ui/editor/export#export-for-the-original, ui/editor/newsize#new-map-of-another-size, ui/editor/files#where-the-files-go, ui/editor/skirmish#name-it-for-skirmish]
order: 90
---

# Saving, testing and sharing

The buttons at the bottom of the [map editor's window](index.md#the-map-editor-window) keep your work, test it and start new maps. They work the same in every mode.

## Save map

Saves the map under the **Map name** as a new map and level of your own. "Saved My map.lvl (in Skirmish)" confirms it.

- The map you opened stays as it was. Saving again under the same name replaces your earlier save.
- From then on the saved map is listed in the [Map Editor](../frontend/map-editor.md) list and in Skirmish.
- To work on it again later: main menu, **Editors**, **Map Editor**, your map, **Edit**.

## Save and play

Saves, then starts your map as a single-player game, so you can try it at once. The editor is off in that game. To edit again, go back to the main menu and open your map from the Map Editor list.

To play it against AI Generals with your own settings, start it from [Skirmish](../frontend/skirmish/index.md) instead.

## Check

Looks for the mistakes that spoil a game, and lists them in orange under the buttons:

- **"no start camera":** a player whose view would begin at the map's corner;
- **"no units or buildings":** a player who starts with nothing;
- **"can't be reached by land":** a player whose start a peasant can't walk to from the first player's start. Water, mountains and other ground land units can't cross are in the way. On an island map that is meant to be; elsewhere, open a path.

The Neutral Player and the barbarians' player are left out. "No problems found" means all is well.

## New blank map

Asks first, then turns the open map into an empty one of the same size:

- flat ground, all of one texture: the one chosen in the [Textures](textures.md) mode, or plain grass if none is chosen;
- no lakes;
- no objects but the players. Their start cameras and resources stay.

The map keeps its size, its sky and its textures. **Make it blank** does it; **Cancel** doesn't. Undo can't bring the old map back, but the file you opened is unchanged: save the blank map under a new name.

## New map of another size

Makes a new, empty map of any size and opens it in the editor.

1. Type the map's name in **Map name** first: the new map is saved under it.
2. Click **New map of another size...**
3. Set **Width (tiles of 5 m)** and **Height (tiles of 5 m)**: 16 to 1024 tiles each (the open map's size at first). 128 tiles are 640 m.
4. Click **Make it**.

The new map is like a [blank map](#new-blank-map): flat, one texture, no lakes, only the players. Their start cameras are moved inside the new area if they were outside it. The game goes back to the menu for a moment and opens the new map with the editor. The map you had open stays as it was saved.

## Name it for Skirmish

The skirmish lobby finds out how many start positions a map has from its name:

- **"2 Player Green Vale"** is listed among the skirmish maps as "Green Vale (2)", with two start positions, and the lobby's [Players filter](../frontend/skirmish/settings.md#players-filter) finds it.
- **Any other name,** such as "Green Vale", is listed at the end of the list, under its own name, only with the filter on All. The lobby then gives it two start positions.

Use the number of players your map really has: those listed in the [Players](players.md) mode as **Human Player**. It is the number the map you started from had.

## Where the files go

Everything goes into your user folder, `%APPDATA%\wkbai\saved`, never into the game's folder:

| File | What |
|---|---|
| `Save_Games\<name>.lvl` | The level: players, objects, zones, aliases |
| `Maps\<name>\<name>.bcm` | The map: heights, tiles, lakes, sky and the minimap |
| `Maps\<name>\<name>.wkbmap` | wkbai's extras: fine heights and lakes past the 63rd (only when needed) |
| `Maps\<name>\<name>.water` | The water design (only when there is one) |
| `Maps\Map_Textures\<name>.lttt` | The map's texture translation, copied from the map you started from (if it has one) |
| `Regions\<name>.json` | The timeline's regions (only if you edited them) |

To give your map to a friend who plays wkbai, send these files. They go into the same folders of their user folder.

## Export for the original

The original *Warrior Kings: Battles* can play maps made in wkbai's editor, but it only reads maps packed in archives in its own folder, and its Skirmish screen lists only its own maps. **Export for the original...** packs your map for it:

1. Click **Export for the original...**
2. Under **Also replace in Skirmish**, choose:
   - **None (Load Game only):** you start the map from the original's Load Game screen;
   - **one of the original's skirmish maps:** your map then takes that map's place in the original's Skirmish list, under that map's name. Take one with the same number of players as yours.
3. Click **Export**. It saves your map first, then writes two files to `saved\Export` in your user folder: `<name>.bcp` (the map) and `<name>.sav` (the level).
4. Copy `<name>.bcp` into the original game's folder, next to its `data.bcp`, and `<name>.sav` into its `saved\Save_Games` folder.

To undo a replaced skirmish map, remove the `.bcp` file from the original's folder.

**What the original sees:** heights in whole steps (your hills slightly rounded down), the first 63 lakes, and your ground, objects, players, zones and aliases. It has no water designs, waterfalls, sea regions or timeline regions.
