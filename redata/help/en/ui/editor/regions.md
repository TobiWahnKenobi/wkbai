---
title: Regions
ids: [ui/editor/regions, ui/editor/regions/paint#paint-cells-and-pick-a-region, ui/editor/regions/pick#paint-cells-and-pick-a-region, ui/editor/regions/radius#radius-m, ui/editor/regions/list#the-region-list, ui/editor/regions/name#name, ui/editor/regions/kind#kind, ui/editor/regions/delete#delete-region, ui/editor/regions/new#new-region, ui/editor/regions/save#save-regions]
order: 80
---

# Regions

The [timeline (F7)](../hud/chronicle.md#timeline) names its events after the place they happen in: "Battle of Ergor". Those places are the map's **regions**. wkbai works them out by itself when a map is first played, from the ground: forest, hills, water and so on. The **Regions** mode lets you change them and name them yourself.

A region is made of **cells** of 8 by 8 tiles (40 m a side). Over the map, the cells' borders are drawn: strong white lines between regions, faint ones inside a region. The chosen region is filled in its colour, and every region's name stands at its centre.

## Paint cells and Pick a region

- **Paint cells:** hold the left mouse button on the ground: the cells under the brush join the chosen region.
- **Pick a region:** click the map to choose the region there.

## Radius (m)

The painting brush's size: 2.5 to 150 metres.

## The region list

The map's regions, each with its name, kind and number of cells. Click one to choose it.

## Name

The chosen region's name, as the timeline shows it.

## Kind

What the region is: plains, meadows, forest, hills, mountains, marsh, desert, shore, water or sea.

## Delete region

Removes the chosen region. Its cells go to the region beside it. The last region can't be deleted.

## New region

Choose a kind in the box before the button, then click **New region**. It gets a name of that kind and is chosen, ready to paint.

## Save regions

Keeps your regions for this map, in your user folder (`saved\Regions\<map>.json`).

- Saved regions stay when the map is edited, as long as it keeps its size.
- **Save map** saves edited regions under your map's new name too.
- Region changes are part of undo.

> wkbai: the original game has no timeline and no regions.
