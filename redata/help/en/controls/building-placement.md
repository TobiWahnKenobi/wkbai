---
title: Placing buildings
ids: [controls/building-placement, controls/building-placement/preview#the-preview, controls/building-placement/turn#turning-it, controls/building-placement/place#placing-it, controls/building-placement/refused#why-it-cant-go-there, controls/building-placement/farm#farms-and-fertile-soil, controls/building-placement/walls#walls-round-a-city, controls/building-placement/gatehouse#a-gatehouse-in-a-wall]
order: 60
---

# Placing buildings

![A barracks being placed: the green footprint, its range ring](shot:controls-placement-valid)

## Starting

Select one or more peasants (or other builders) and click a building in the [build menu](../ui/hud/command-panel.md#build-menu) at the upper left.

## The preview

The building follows the mouse over the ground, snapped to the map's 5 m tiles, shown **as it will look finished**:

- **green:** it can stand there;
- **red:** it can't;
- **squares on the ground:** one per tile it covers, green or red each, so you can see which tiles are in the way;
- **range rings:** some buildings show the reach of what they do.

## Turning it

- **[** turns it a quarter turn one way, **]** the other.
- These are the two keys right of P, wherever your keyboard has them (Ü and + on a German keyboard).
- A new pick starts unturned. Key actions: [Turn the building being placed](keys.md#selection-and-orders).

## Placing it

- **Right-click:** the foundation is placed and the selected builders go and build it.
- **Shift + right-click:** the builders add it to their queue, and you keep placing: right-click again for another of the same.
- **Ctrl + right-click:** they build it first, and you keep placing too.
- **Left-click:** stop placing without building.

## Why it can't go there

![A red preview with the "Cannot build here" box](shot:controls-placement-refused)

Where it is refused, a dark red box under the cursor says why, in the game's own words:

- the ground is taken (another building, a tree, a unit in the way);
- the ground is the wrong kind (water, for example), or too steep;
- it overlaps a neighbouring building;
- it must be **joined to a city**: civic buildings go inside or right against the walls of a city round your Manor, Keep, Castle or Palace;
- it must be **away from every city** (a new village centre);
- a new wall would cross a gatehouse, or a rural building would end up inside the walls.

## Farms and fertile soil

While you place a farm, a tooltip rates the soil under it: arid, ordinary or fertile, with its fertility as a percentage (from 50 to 150 %). See [Farming](help:mechanic/farming).

## Walls round a city

When a civic building grows a city, the preview shows the banner poles of the new stretch of palisade round it, on green tiles. Old walls that would end up inside the city disappear from the preview: they will be taken down.

## A gatehouse in a wall

A gatehouse is built into a wall, as in the original:

- **Point at your wall.** Move the gatehouse over a straight stretch of your own wall. The preview jumps onto the wall piece under the cursor, even when the cursor's ground point is a tile or two behind it, and turns the gatehouse along the wall.
- **Place it.** The wall pieces under it disappear from the preview; once it is placed they are gone, and the wall's ends meet the gatehouse.
- **Where it can't go:** over a wall's corner, onto another gatehouse, across a run of wall foundations, or onto someone else's wall.

> wkbai: the preview, the footprint squares, the refusal box and the quarter turns follow the original. The city rules are the original's, read from its code.
