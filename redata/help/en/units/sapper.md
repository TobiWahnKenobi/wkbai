---
title: Sapper
ids: [unit/Sapper, command/Spawn Sapper]
order: 10
---

# Sapper

<!-- gen:icon -->
![Sapper](game:interface/icons/characters/unaligned_sapper.tga)
<!-- /gen:icon -->

<!-- gen:facts -->
```facts
blueprint: Sapper
```

<!-- facts:static -->
| | |
|---|---|
| Cost | 150 food, 25 gold |
| Food upkeep | 1 |
| Hit points | 100 |
| Armour | 5 (front 5, sides 3.75, rear 0.5) |
| Melee attack | 15 every 2 s |
| Damage to buildings | 20 per blow |
| Speed | 4 |
| Sight | 5 (25 m) |
<!-- /facts:static -->
<!-- /gen:facts -->

## Role

The sapper is the army's engineer. It puts up siege engines ([Battering Rams](battering-ram.md), [Siege Towers](siege-tower.md), [Trebuchets](trebuchet.md), [Rocket Launchers](rocket-launcher.md)), wooden [Palings](../buildings/palings.md), [Training Dummies](../buildings/training-dummy.md) and explosive mines, and repairs engines in the field. It costs 150 food and 25 gold at the [Armoury](../buildings/armoury-stone.md) and has 100 hit points; its own blows do 20 structural damage to buildings.

## Abilities

- **Explosive mine:** after the [Explosive Mine](../upgrades/explosive-mine.md) research (100 gold at the Armoury) each sapper can plant one [mine](../buildings/explosive-mine.md), free and in half a second. Planting a new one removes its old one, and the mine goes when the sapper dies.
- **[Explosive Mine](../commands/detonate-mine.md) (detonate):** the sapper lights a fuse that runs to the mine. The blast hits every unit and building within 30 m, of any side, for up to 480 hit points and 300 structure at the centre, less farther out. The sapper can then plant another.
- **[Defuse](../commands/defuse-mine.md):** 5 seconds next to any mine remove it.
- **[Repair (Character)](../commands/repair-character.md):** mends siege engines, carts and boats, 3 hit points at a time for 1 wood.
- **[Repair (Sapper)](../commands/sapper-repair.md):** speeds up building your Palings.
- **Palings** (20 wood, 5 seconds to build) are a fence no unit can cross. Your archers don't skirmish away from enemies when Palings stand in front of them.

## Strong against

- **Walls and buildings**, through the engines it builds and its mines.
- **Packed troops** that walk over a mine.

## Weak against

- **Any soldier**: it has 100 hit points and armour 5.
- **Your own troops** near its mine: the blast spares no one.

## Trained at / Built by

<!-- gen:trained -->
- **Trained at:** [Armoury](../buildings/armoury-stone.md)
<!-- /gen:trained -->

## Needs

<!-- gen:needs -->
- Nothing beyond the building that trains it and the cost.
<!-- /gen:needs -->

## Leads to

<!-- gen:leads -->
- **Builds:** [Explosive Mine](../buildings/explosive-mine.md), [Training Dummy](../buildings/training-dummy.md), [Palings](../buildings/palings.md), [Battering Ram](battering-ram.md), [Siege Tower](siege-tower.md), [Trebuchet](trebuchet.md), [Rocket Launcher](rocket-launcher.md)
<!-- /gen:leads -->

## Commands

<!-- gen:commands -->
- [Capture](../commands/capture.md)
- [Explosive Mine](../commands/detonate-mine.md)
- [Defuse](../commands/defuse-mine.md)
- [Halt](../commands/halt.md)
- [Enter Dark Portal](../commands/enter-dark-portal.md)
- [Repair (Character)](../commands/repair-character.md)
- [Interrogate](../commands/interrogate.md)
- [Attack](../commands/attack.md)
- [Scale Siege Tower](../commands/scale-siege-tower.md)
- [Build](../commands/build.md)
- [Take Cart](../commands/take-cart.md)
- [Repair (Sapper)](../commands/sapper-repair.md)
- [Finish Building](../commands/finish-building.md)
- [Finish Wall](../commands/finish-wall.md)
- [Repair Wall](../commands/repair-wall.md)
- [Convert Troops to Peasants](../commands/military-unit-convert-to-peasant.md)
- [Garrison](../commands/garrison.md)
- [Board Vessel](../commands/board-vessel.md)
- [Resupply Ammunition](../commands/resupply.md)
- [Guard Next Unit Selected](../commands/guard.md)
- [Flee](../commands/run-away.md)
- [Move](../commands/move.md)
<!-- /gen:commands -->

## Tips

- Take sappers with the army: the siege engines are built on the spot, close to the walls you want to break.
- Plant a mine in a gateway or a narrow pass, pull your men back past 30 m, and detonate when the enemy is on it.
- Ring your archers with Palings so cavalry can't reach them.
