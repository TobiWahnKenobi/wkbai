---
title: Ships
ids: [mechanics/naval, mechanic/naval, mechanic/ships, mechanic/boats, mechanic/docks, mechanic/transport, mechanic/sailing#sailing-and-the-wind]
order: 115
---

# Ships

## Docks only without AI Generals

The [Dock](../buildings/dock-wood.md) appears in the build menu only when **no AI General plays** in the game: human-only multiplayer games, or a skirmish where you are alone on the map. The AI can't use ships, so the game set takes them out when it plays. A dock needs a [Village](../buildings/village-wood.md).

## Boats

The dock builds:

| Boat | Role |
|---|---|
| [Cog](../units/cog.md) | transport for troops and supply routes |
| [Corsair](../units/arrow-boat.md) | shoots bolts at ships and shores |
| [Fire Boat](../units/fire-boat.md) | a warship that shoots like the Corsair |
| [Carrack](../units/mangonel-boat.md) | throws stones (needs an Armoury or Arcanum) |
| [Galleass](../units/bombard-boat.md) | fires cannon balls (not with a Cathedral or Temple of the Moon) |

- Boats sail only on water and aren't slowed by the ground.
- A damaged boat is repaired at a dock ([Repair (Docking)](../commands/docking.md)).

## Sailing and the wind

With the lobby's [wind](../ui/frontend/skirmish/wind.md) and its **Effects on the game** on (wkbai's own), the wind changes a boat's speed by the way it sails. The ships have square sails and oars, so they run best with the wind behind them and worst straight into it:

| Course | Wind of 8 m/s | Wind of 16 m/s or more |
|---|---|---|
| Wind behind | +25 % | +50 % |
| Wind across | +12 % | +24 % |
| Into the wind | −35 % | −65 % |

A boat is never slower than 35 % of its speed, nor faster than 160 %. The [wind gauge](../ui/hud/top-bar.md#wind-gauge) shows where the wind blows. This works with or without the seasons.

> wkbai: in the original the wind doesn't exist, and boats keep their speed on every course.

## Carrying troops

- A Cog carries a **load of 40**. Each passenger weighs: foot soldiers and peasants 1, most horsemen 2, carts, wagons, rams, bombards and rocket launchers 5, a few big units 3 or 4, the largest creatures 10 to 15.
- Select the troops and right-click the Cog ([Board Vessel](../commands/board-vessel.md)), or have the Cog [pick them up](../commands/pick-up-passenger.md). It sails to the nearest shore point next to them.
- [Disembark](../commands/unload-passengers.md) sets them ashore at the nearest place they can stand.
- Passengers still count towards your population and eat. If the Cog sinks, they go down with it.

## Supply routes

A Cog can carry goods between docks ([Up Overseas Supply Line](../commands/set-up-oversea-supply-route.md)), as carts do overland ([Up Overland Supply Line](../commands/set-up-overland-supply-route.md)). [Clear Supply Line](../commands/clear-supply-route.md) ends it.

![A Cog loading pikemen at a dock](shot:mech-naval-cog)
