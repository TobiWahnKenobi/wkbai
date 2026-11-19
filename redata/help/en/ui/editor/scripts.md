---
title: Scripts
ids: [ui/editor/scripts, ui/editor/scripts/gameset#game-set-file, ui/editor/scripts/use#game-set-file, ui/editor/scripts/aliases#aliases, ui/editor/scripts/add#add-the-selection-and-take-the-selection-out, ui/editor/scripts/remove#add-the-selection-and-take-the-selection-out, ui/editor/scripts/select#select-its-objects, ui/editor/scripts/empty#empty-it]
order: 70
---

# Scripts

The **Scripts** mode links the map to a mission's scripts. A skirmish map doesn't need it.

A level has no triggers of its own. It names a **game set file**, and that file holds the mission's triggers. The triggers find the level's objects through **aliases**: named lists of objects that the game set declares and the level fills.

The editor doesn't write triggers. You write them in the game set file, outside the game.

## Game set file

The file the level is saved with. To change it, type the new file and click **Use for the saved level**. The game set in use doesn't change now: the new one is loaded the next time the saved level is loaded.

## Aliases

"Aliases (31 declared by the game set)": every alias the game set declares, each with the number of objects in it. **Alias filter** shortens the list. Click an alias to choose it; the line below says how many objects it holds and how many are selected.

Select the objects first, with the [Objects](objects.md#select-move-turn) mode's **Select, move, turn** tool or by clicking them on the map.

## Add the selection and Take the selection out

Put the selected objects into the chosen alias, or take them out of it.

## Select its objects

Makes the alias's objects the selection, so you can see them.

## Empty it

Takes every object out of the chosen alias.

Alias changes are part of undo, and are saved with the map.
