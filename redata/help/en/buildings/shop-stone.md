---
title: Shop
ids: [building/Shop Stone, command/Build Shop Stone]
order: 10
---

# Shop

<!-- gen:icon -->
![Shop](game:interface/icons/buildings/shops.tga)
<!-- /gen:icon -->

<!-- gen:facts -->
```facts
blueprint: Shop Stone
```

<!-- facts:static -->
| | |
|---|---|
| Cost | 100 wood |
| Build time | 20 s |
| Strength (structural integrity) | 300 |
| Repair cost | 100 wood |
<!-- /facts:static -->
<!-- /gen:facts -->

## Role

The shop is where trade turns goods into gold. Peasants and merchants go shopping there.

## What it does

- **One shopkeeper.** One peasant at a time trades there ([Trade at Shop](../commands/shop.md)); others sent there look for a free shop.
- **Materials into gold.** Each round takes 1 materials from your stock and makes 0.28 gold ([gold](../mechanics/resources.md#gold)). A [Merchant](../units/merchant.md) as shopkeeper makes half as much again.
- **Bank.** With a [Bank](bank.md) within 60 m of the shopkeeper's way, the gold is worth 1.5 times as much. Selecting a shop shows your banks.
- It stops when your materials run out.

## Defence

- **Strength:** 300.
- **What hurts it:** soldiers barely scratch it (0.1 a blow). Siege engines, sappers and fire do the work ([siege](../mechanics/siege.md#structural-damage)).
- **Fire:** each flaming arrow that hits it has a 5% chance to set it alight. Left alone, a fire grows until the building burns down, and it can leap to buildings within 25 m. Send peasants to [Extinguish Fire](../commands/extinguish.md).
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
- **Building it unlocks:** [Tavern](tavern-wood.md)
<!-- /gen:leads -->

## Commands

<!-- gen:commands -->
- [Pause trading at Shop](../commands/pause-shopping.md)
- [Continue trading at Shop](../commands/continue-shopping.md)
<!-- /gen:commands -->

## Tips

- [Pause trading at Shop](../commands/pause-shopping.md) and [Continue trading at Shop](../commands/continue-shopping.md) start and stop its trade, for when you need the materials.
- Build several shops around one Bank.
- A Shop unlocks the Tavern.
