---
title: Standard Farm
ids: [building/Standard Farm, command/Build Farm Wood]
order: 10
---

# Standard Farm

<!-- gen:icon -->
![Standard Farm](game:interface/icons/buildings/farms.tga)
<!-- /gen:icon -->

<!-- gen:facts -->
```facts
blueprint: Standard Farm
```

<!-- facts:static -->
| | |
|---|---|
| Cost | 50 wood |
| Build time | 15 s |
| Strength (structural integrity) | 100 |
| Repair cost | 50 wood |
<!-- /facts:static -->
<!-- /gen:facts -->

## Role

The farm is where your food comes from. Peasants work its fields; every soldier eats food, so the number of farmers decides how big an army you can keep. Fertile land gives more, barren land less.

## What it does

- **One farmer.** Only one peasant works a farm at a time; a second goes to the nearest free farm.
- **The ground decides.** Placed on fertile ground it becomes a Fertile Farm (1.5 food a turn of the hoe), on barren ground a Barren Farm (0.5) ([farms and the ground](../mechanics/resources.md#farms-and-the-ground)).
- **Food support.** Each working farmer feeds 4 of your army's upkeep, more after the farming research ([food support](../mechanics/population-and-food.md#the-food-support)).
- **Mills.** A [Windmill](windmill.md) within 40 m of the farmer's way home doubles his load. Selecting a farm shows your windmills' reach.
- **Arrows pass over it**: it doesn't stop missiles.

## Defence

- **Strength:** 100. Fields are easy to burn and easy to replace.
- **What hurts it:** soldiers barely scratch it (0.1 a blow). Siege engines, sappers and fire do the work ([siege](../mechanics/siege.md#structural-damage)).
- **Fire:** each flaming arrow that hits it has a 20% chance to set it alight. Left alone, a fire grows until the building burns down, and it can leap to buildings within 25 m. Send peasants to [Extinguish Fire](../commands/extinguish.md).
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
- Nothing new: it trains no units and offers no research.
<!-- /gen:leads -->

## Commands

<!-- gen:commands -->
- None of its own.
<!-- /gen:commands -->

## Tips

- Watch your food upkeep: a new unit that would put food in the red is refused or warned about.
- The Guildhall's farming research ([Heavy Plough](../upgrades/heavy-plough.md), [Horse Harness](../upgrades/horse-harness.md), [Crop Rotation](../upgrades/crop-rotation.md)) makes each farmer feed more.
