---
title: Trading Post
ids: [building/Trading Post, command/Build Trading Post]
order: 10
---

# Trading Post

<!-- gen:icon -->
![Trading Post](game:interface/icons/buildings/trading_post.tga)
<!-- /gen:icon -->

<!-- gen:facts -->
```facts
blueprint: Trading Post
```

<!-- facts:static -->
| | |
|---|---|
| Cost | 175 wood |
| Build time | 20 s |
| Strength (structural integrity) | 500 |
| Repair cost | 175 wood |
| Sight | 30 |
<!-- /facts:static -->
<!-- /gen:facts -->

## Role

The trading post hires scouts, barbarians and Vandavar Barbarian raiders, and offers the [Shared Vision](../upgrades/reconnaissance.md) research. It sees far (sight 30).

## What it does

- **Trade:** [Merchants](../units/merchant.md) load goods at a Trading Post (1 every 2 seconds, up to 10) and carry them to a Village, Warehouse or Manor-type building. Each trip brings gold: the goods × the distance ÷ 400, plus the distance ÷ 50, plus 50 if the post is an enemy's.
- **Tributes:** while you own one, you can send food, materials or gold to other players ([tributes](../mechanics/diplomacy.md#tributes)).
- **Lookout:** it sees far (sight 30, about 150 m).
- **Hires** scouts, barbarians and Vandavar Barbarian raiders, and offers [Shared Vision](../upgrades/reconnaissance.md).

## Defence

- **Strength:** 500.
- **What hurts it:** soldiers barely scratch it (0.1 a blow). Siege engines, sappers and fire do the work ([siege](../mechanics/siege.md#structural-damage)).
- **Fire:** each flaming arrow that hits it has a 15% chance to set it alight. Left alone, a fire grows until the building burns down, and it can leap to buildings within 25 m. Send peasants to [Extinguish Fire](../commands/extinguish.md).
- **Repair:** peasants [repair](../commands/repair.md) it, paying as they go ([strength and repair](../mechanics/buildings-and-construction.md#strength-damage-and-repair)).

## Built by

<!-- gen:trained -->
- **Built by:** [Peasant](../units/peasant.md)
<!-- /gen:trained -->

## Needs

<!-- gen:needs -->
- Nothing beyond the cost.
<!-- /gen:needs -->

## Leads to

<!-- gen:leads -->
- **Trains:** [Scout](../units/scout.md), [Barbarian](../units/barbarian.md), [Vandavar Barbarian](../units/vandavar.md)
- **Research here:** [Shared Vision](../upgrades/reconnaissance.md)
<!-- /gen:leads -->

## Commands

<!-- gen:commands -->
- [Scout Locator](../commands/scout-locator.md)
<!-- /gen:commands -->

## Tips

- A cheap way to get a scout out early.
- The further the post from where the merchants deliver, the more each trip is worth: 400 m makes a full load worth about 18 gold.
- Barbarians and Vandavar Barbarian raiders need the Pagan buildings (Maypole, Sacred Grove).
