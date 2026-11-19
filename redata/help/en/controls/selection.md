---
title: Selecting
ids: [controls/selection, controls/selection/click#clicking, controls/selection/all-of-a-kind#all-of-a-kind, controls/selection/box#box-selection, controls/selection/rules#what-can-be-selected-together, controls/selection/groups#control-groups, controls/selection/hot-keys#selection-keys, controls/selection/activity#by-activity]
order: 30
---

# Selecting

What you select gets your orders. Its [selection panel](../ui/hud/selection-panel.md) and [command buttons](../ui/hud/command-panel.md) show at the lower left.

## Clicking

- **Click** a unit or building of yours: it alone is selected.
- **Shift + click:** add it to the selection. (Shift-clicking something already selected keeps it.)
- **Click the ground:** nothing is selected any more.
- Pointing at something you can't select (an enemy, a tree) still shows it in the selection panel, as long as nothing is selected.

## All of a kind

Pick out every unit of one kind **within 25 m** of the one you click:

- **Double-click** a unit: all of its kind nearby.
- **Some soldiers** (where the game set says so) work the other way round: a **single** click takes all of their kind nearby, a double click only that one.
- **Single click selects all units** ([option](help:option/singleClickSelectsAll), on at first): switch it off and a single click is always one unit, a double click always all of its kind.

> wkbai: the original speaks of "local units"; wkbai counts that as 25 m round the clicked unit, on screen or not.

## Box selection

![A selection box drawn round a group of archers](shot:controls-box-select)

Press the left button on the ground and drag: a white box follows the mouse. When you let go, everything of yours whose centre is inside the box is **added**. Without Shift the press has already emptied the selection, so you get just the box's contents; with Shift you add the box to what you had.

## What can be selected together

The game tidies the selection every moment:

- only **your own** units and buildings stay; enemies and neutral things can't be selected;
- with any unit selected, **buildings drop out**: a box over a town and its peasants selects the peasants;
- a soldier in a **formation** stands for its formation: the formation is selected instead;
- there is **no limit** on how many.

## Control groups

| Keys | Does |
|---|---|
| **Ctrl + 1 ... 9, 0** (or Ctrl + Keypad 1 ... 9) | Stores the selection as that group, replacing what it held |
| **1 ... 9, 0** (or Keypad 1 ... 9) | Selects that group again |
| The same number **twice** within 0.4 seconds | Also moves the camera to the group's middle |

Units lost since are left out. Keypad 0 isn't a group: it turns the camera.

## Selection keys

One key per kind of thing, each pressed again for the next one, nearest the camera first. The camera goes there and a white ring pings it on the minimap. Pressing another key in between starts over.

| Key | Selects |
|---|---|
| **V** | A village |
| **I** | An idle peasant |
| **X** | A priest, bishop, inquisitor, druid, succubus, high priestess or sapper |
| **B** | A barracks (or hall of warriors) |
| **L** | A stables |
| **K** | An economic building |
| **M** | Your Manor, Keep, Castle or Palace |
| **Z** | A burning building |
| **N** | A scout or spy |
| **C** | A supply wagon |
| **T** | A trading post |
| **Y** | A tower or fort |
| **U** | A dock |
| **J** | A place of interest (only shown, not selected) |

The lists come from the game set (its "Hot Key" finders), so a mod may change what each key picks.

## By activity

The [peasant activity](../ui/hud/peasant-activity.md) tubes select every peasant doing one job: click the farm icon and all your farmers are selected.

> wkbai: the original's units answer with a voice when selected; wkbai doesn't play that yet.
