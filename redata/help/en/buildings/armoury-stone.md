---
title: Armoury
ids: [building/Armoury Stone, command/Build Armoury Stone]
order: 10
---

# Armoury

<!-- gen:icon -->
![Armoury](game:interface/icons/buildings/armoury.tga)
<!-- /gen:icon -->

<!-- gen:facts -->
```facts
blueprint: Armoury Stone
```

<!-- facts:static -->
| | |
|---|---|
| Cost | 300 wood, 50 gold |
| Build time | 70 s |
| Strength (structural integrity) | 800 |
| Repair cost | 300 wood, 50 gold |
<!-- /facts:static -->
<!-- /gen:facts -->

## Role

The armoury trains sappers and siege engines (mangonels, bombards, explosive carts and, on the Imperial path, war elephants). It needs a Barracks.

## What it does

- **Trains** sappers and siege engines (see the list below).
- **Research:** [Explosive Mine](../upgrades/explosive-mine.md), which lets sappers bury [explosive mines](explosive-mine.md).
- **Resupply:** your and your allies' archers, gunners, siege engines and other troops that use ammunition get 10 back every 5 seconds or so while within 25 m of it; 50 m after [Logistics](../upgrades/logistics.md).

## Defence

- **Strength:** 800.
- **What hurts it:** soldiers barely scratch it (0.1 a blow). Siege engines, sappers and fire do the work ([siege](../mechanics/siege.md#structural-damage)).
- **Fire:** each flaming arrow that hits it has a 5% chance to set it alight. Left alone, a fire grows until the building burns down, and it can leap to buildings within 25 m. Send peasants to [Extinguish Fire](../commands/extinguish.md).
- **Repair:** peasants [repair](../commands/repair.md) it, paying as they go ([strength and repair](../mechanics/buildings-and-construction.md#strength-damage-and-repair)).

## Built by

<!-- gen:trained -->
- **Built by:** [Peasant](../units/peasant.md)
<!-- /gen:trained -->

## Needs

<!-- gen:needs -->
- building a [Barracks](barracks-stone.md)
<!-- /gen:needs -->

## Leads to

<!-- gen:leads -->
- **Trains:** [Sapper](../units/sapper.md), [Mangonel](../units/mangonel.md), [Bombard](../units/bombard.md), [Imperial War Elephant](../units/war-elephant.md), [Explosive Cart](../units/explosive-cart.md)
- **Research here:** [Explosive Mine](../upgrades/explosive-mine.md)
- **Building it unlocks:** [Carrack](../units/mangonel-boat.md)
<!-- /gen:leads -->

## Commands

<!-- gen:commands -->
- [Sapper Locator](../commands/sapper-locator.md)
<!-- /gen:commands -->

## Tips

- Train sappers here to build battering rams, siege towers and trebuchets in the field.
- Siege engines parked next to it refill their ammunition.
- Research [Explosive Mine](../upgrades/explosive-mine.md) here.
