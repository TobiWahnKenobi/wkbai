---
title: Guildhall
ids: [building/Guildhall Stone, command/Build Guildhall Stone]
order: 10
---

# Guildhall

<!-- gen:icon -->
![Guildhall](game:interface/icons/buildings/guildhallstone.tga)
<!-- /gen:icon -->

<!-- gen:facts -->
```facts
blueprint: Guildhall Stone
```

<!-- facts:static -->
| | |
|---|---|
| Cost | 200 wood |
| Build time | 20 s |
| Strength (structural integrity) | 600 |
| Repair cost | 300 wood |
<!-- /facts:static -->
<!-- /gen:facts -->

## Role

The guildhall is the town's place of learning for crafts. It researches farming improvements, wooden forts, flaming arrows and the wall upgrades, and it unlocks the Blacksmith, Warehouse, Windmill and Sawmill.

## What it does

- **Research:** farming (more food support per farmer), the Fort, flaming arrows and the three wall upgrades (see the list below).
- **Unlocks** the Blacksmith, Warehouse, Windmill and Sawmill.

## Defence

- **Strength:** 600.
- **What hurts it:** soldiers barely scratch it (0.1 a blow). Siege engines, sappers and fire do the work ([siege](../mechanics/siege.md#structural-damage)).
- **Fire:** each flaming arrow that hits it has a 2.5% chance to set it alight. Left alone, a fire grows until the building burns down, and it can leap to buildings within 25 m. Send peasants to [Extinguish Fire](../commands/extinguish.md).
- **Repair:** peasants [repair](../commands/repair.md) it, paying as they go ([strength and repair](../mechanics/buildings-and-construction.md#strength-damage-and-repair)).

## Built by

<!-- gen:trained -->
- **Built by:** [Peasant](../units/peasant.md)
<!-- /gen:trained -->

## Needs

<!-- gen:needs -->
- building a [Village](village-wood.md)
<!-- /gen:needs -->

## Leads to

<!-- gen:leads -->
- **Research here:** [Horse Harness](../upgrades/horse-harness.md), [Heavy Plough](../upgrades/heavy-plough.md), [Crop Rotation](../upgrades/crop-rotation.md), [Fort Upgrade](../upgrades/fort-wood.md), [Flaming Arrow](../upgrades/flaming-arrow.md), [Fortified Wall Upgrade](../upgrades/stockade-to-unaligned-wall.md), [Battlements Upgrade](../upgrades/unaligned-wall-to-technological-wall.md), [Ramparts Upgrade](../upgrades/technological-wall-to-celestial-wall.md)
- **Building it unlocks:** [Blacksmith](blacksmith.md), [Sawmill](sawmill.md), [Warehouse](warehouse-stone.md), [Windmill](windmill.md), [Fortified Wall Upgrade](../upgrades/stockade-to-unaligned-wall.md)
<!-- /gen:leads -->

## Commands

<!-- gen:commands -->
- None of its own.
<!-- /gen:commands -->

## Tips

- Build it early: almost every later economy building needs it.
- The wall research goes in order: [Stockade to Fortified Wall](../upgrades/stockade-to-unaligned-wall.md), then [Fortified Wall to Battlements](../upgrades/unaligned-wall-to-technological-wall.md), then [Battlements to Ramparts](../upgrades/technological-wall-to-celestial-wall.md).
