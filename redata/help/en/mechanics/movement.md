---
title: Movement and terrain
ids: [mechanics/movement, mechanic/movement, mechanic/terrain, mechanic/pathfinding, mechanic/speed, mechanic/roads, mechanic/slopes]
order: 100
---

# Movement and terrain

## Speed and ground

Each unit type has a base speed (a peasant walks at 4, most cavalry much faster). The ground under it changes that:

| Ground | Foot | Horse |
|---|---|---|
| Cobbles (roads, town streets) | ×1.65 | ×1.65 |
| Grass of all kinds | ×1 | ×1 |
| Hills, rocky grass, grass and forest | ×0.85 | ×0.8 |
| Sand | ×0.8 | ×0.8 |
| Snow, earth, forest, ford | ×0.75 | ×0.7 to ×0.75 |
| Shore | ×0.7 | ×0.75 |
| Grass and water | ×0.6 | ×0.5 |
| Rock, mountains | ×0.5 | ×0.5 |
| Swamp | ×0.5 | ×0.45 |

- **Slopes don't slow anyone.** Height is still worth having: see [combat](combat.md#hand-to-hand-damage) and [sight](fog-of-war.md#how-far-a-unit-sees).
- **A walk is made of legs.** The speed is set when a leg starts, so a unit keeps a road's pace a few metres into the grass.
- **Charging cavalry** speeds up near its target ([combat](combat.md#hand-to-hand-damage)).
- **Formations** have their own pace ([formations](formations.md#bonuses)).

## Where units can go

- **Ground by type.** Every unit type has a list of the terrains it may cross. Land units keep off deep water, coastlines and river beds; siege engines and wagons also keep off forest and rock. Boats only sail.
- **Steepness.** Most units climb slopes up to 45°. Heavy machines (the battering ram, bombard, DeathWyrm) only 30°.
- **Water.** Land units ford shallow water. Archangel, Abaddon and the large elementals wade through any depth. With wkbai's [river currents](water-and-currents.md) on, moving water pushes and hurts the units in it, and they swim to the shore.
- **Buildings and walls** block the way; an open gatehouse lets units through. Units don't block each other: they walk through, and spread out where they stop.

## Paths

- Right-click a destination: the unit works out a path around obstacles. If the place can't be reached, it goes as close as it can.
- Groups spread out round the destination instead of piling onto one spot.
- **Waypoints:** Shift+right-click adds a waypoint, Ctrl+right-click puts one first ([controls](../controls/index.md)).

![A column marching along a cobbled road past a forest](shot:mech-movement-road)

> **wkbai.** In this version of wkbai the per-type terrain lists and slope limits aren't applied yet: every land unit follows the same land rules and climbs up to 45°, siege engines and wagons can enter forest and rock, and water deeper than 1.5 m stops everyone but boats (Archangel, Abaddon and the elementals included). This is being brought in line with the original.

> **wkbai.** Path searches run on several threads and are much faster than the original's, so large games stay smooth. With wkbai's seasons on, wet and snowy ground slows everyone further, and frozen rivers can be walked on ([seasons and weather](seasons-weather.md#effects-on-play)). With the river currents on, paths keep away from water that would carry or hurt a unit ([river currents and swimming](water-and-currents.md#paths-and-the-ai)).
