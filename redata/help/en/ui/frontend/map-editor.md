---
title: Map Editor
ids: [ui/frontend/editlevels, ui/frontend/editlevels/list#the-list-of-maps, ui/frontend/editlevels/edit#edit, ui/frontend/editlevels/back#back]
parent: ui/frontend/editors
order: 90
---

# Map Editor

Choose a map to change. It opens paused, with [the map editor](../editor/index.md)'s window beside it: heights, ground textures, water, objects and players. What you make is saved as a new map in your user folder, and then appears in this list and in the skirmish lobby. The map you opened is never changed.

![The Map Editor's list of maps](shot:frontend-editlevels)

New to the editor? [Make a skirmish map](../editor/make-a-map.md) walks you through it.

## The list of maps

Every map you can play: the skirmish maps as "4 Peaks (2)" (the name, then the number of players), then the Kingmaker campaign's provinces under their names, then the maps saved with the editor under their own names. The editor saves each map as a [map folder](../editor/map-format.md), listed at the end under its whole name ("2 Player Green Vale"). A map saved in the original's format whose name starts with the number of players is listed among the skirmish maps instead, as "Green Vale (2)". The tutorials and the story levels aren't listed. Click a map to select it; a double click opens it.

The editor can't add or remove players, so choose a map with as many players as you want yours to have.

## Edit

Opens the selected map in the editor. A double click on a map does the same.

## Back

Returns to the [Editors](editors/index.md) screen. Esc does the same.

## Tips

- In a map opened from here, **F8** ([`mapEditor`](help:key/mapEditor)) hides the editor and shows it again. In other games F8 does nothing.
- The editor's modes, tools and keys are described in [the map editor](../editor/index.md).
- The editor is not available in multiplayer.

> wkbai: the original game has no map editor in the game. It can't read wkbai's map folders, but [Export for the original](../editor/saving.md#export-for-the-original) turns your map into files it reads; it sees the heights in whole steps and the first 63 lakes.
