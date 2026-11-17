---
title: Siege
ids: [mechanics/siege, mechanic/siege, mechanic/walls, mechanic/siege-engines, mechanic/structural-damage]
order: 110
---

# Siege

Buildings and walls are tough. A sword or a spear does only **0.1** structural damage a blow, so an army without siege engines can take an age over a single house. To break a town you need the right tools.

## Structural damage

Damage to buildings is counted separately from damage to units. Some examples from the game set:

| Attacker | Structural damage |
|---|---|
| soldiers, peasants | 0.1 a blow |
| [Sapper](../units/sapper.md) | 20 a blow |
| [Battering Ram](../units/battering-ram.md), [Rock Thrower](../units/rock-thrower.md) | 40 a blow |
| [Mangonel](../units/mangonel.md) ball | 40 |
| [Bombard](../units/bombard.md) ball | 50 |
| [Trebuchet](../units/trebuchet.md) ball | 65 |
| [Rocket Launcher](../units/rocket-launcher.md) | 135 a blow, rockets 250 near where they land |
| [War Elephant](../units/war-elephant.md), large elementals | 80 to 100 |
| [Michael](../units/michael.md), [Abaddon](../units/abaddon.md) | 150 |

Fire does steady damage too ([buildings and construction](buildings-and-construction.md#fire)).

## Siege engines

- **Ranges.** Siege engines shoot far (a mangonel 90 m, a bombard 150 m, a trebuchet 160 m, rockets 200 m) but can't hit anything close: 15 m for a mangonel, 20 m for a trebuchet or bombard, 25 m for rockets.
- **Setting up.** A trebuchet is built on the spot and must be packed up ([Limber](../commands/limber.md), 5 seconds) to move and [unpacked](../commands/unlimber.md) to shoot. The rocket launcher does the same. Mangonels and bombards shoot straight away.
- **Splash.** Stones hit everyone within 8 m of where they land, your own men included ([combat](combat.md#splash-and-friendly-fire)). Builders and repairers at a building that is hit are hurt too.
- **Greek fire** (trebuchet research) sets buildings within 10 m alight.
- **Ammunition** runs out; a [Supply Wagon](../units/supply-wagon.md) refills them.
- **Protect them.** Siege engines are slow and weak close up. Light cavalry and hand-to-hand troops make short work of an unguarded one.

## Getting over and through walls

- **[Siege Tower](../units/siege-tower.md):** pushed up to a wall, it lets the troops inside [scale](../commands/scale-siege-tower.md) it and come down on the far side.
- **[Sappers](../units/sapper.md)** dig [explosive mines](../buildings/explosive-mine.md) (research at the Armoury) under walls and buildings and set them off. An enemy sapper can [defuse](../commands/defuse-mine.md) a mine.
- **[Spies](../units/spy.md)** can set buildings on fire ([Arson](../commands/arson.md)) and [blow open gates](../commands/blow-open-gates.md).
- **A blocked path:** when a wall stands between soldiers and their target, the original makes them attack the wall (not yet in this version of wkbai).

## Defending

- **Walls** come with the city. The Guildhall's research turns your palisade into stone walls and then into the Renaissance and Imperial walls ([Stockade to Unaligned Wall](../upgrades/stockade-to-unaligned-wall.md), [Unaligned to Technological](../upgrades/unaligned-wall-to-technological-wall.md), [Technological to Celestial](../upgrades/technological-wall-to-celestial-wall.md)).
- **Gatehouses** let your troops out and keep enemies in front of the wall. Open and close them from their panel.
- **Towers and forts** shoot when garrisoned ([garrisons](buildings-and-construction.md#garrisons)). A garrison is thrown out, unhurt, when its building falls below 10%.
- **Repair** walls with peasants ([Repair Wall](../commands/repair-wall.md)), even under attack.

![A trebuchet and mangonels shelling a stone wall while pikemen guard them](shot:mech-siege-trebuchet)

> **wkbai.** Neither the original nor wkbai has walkable wall tops: the siege tower's troops land on the ground beyond the wall.
