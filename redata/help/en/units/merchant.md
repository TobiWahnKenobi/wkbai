---
title: Merchant
ids: [unit/Merchant, command/Spawn Merchant]
order: 10
---

# Merchant

<!-- gen:icon -->
![Merchant](game:interface/icons/characters/unaligned_merchant.tga)
<!-- /gen:icon -->

<!-- gen:facts -->
```facts
blueprint: Merchant
```

<!-- facts:static -->
| | |
|---|---|
| Cost | 100 food, 20 gold |
| Food upkeep | 2 |
| Hit points | 80 |
| Armour | 5 (front 5, sides 3.75, rear 0.5) |
| Melee attack | 8 every 1.5 s |
| Speed | 4 |
| Sight | 5 (25 m) |
<!-- /facts:static -->
<!-- /gen:facts -->

## Role

The merchant turns your wood into gold. Trained at the [Warehouse](../buildings/warehouse-stone.md) for 100 food and 20 gold, it works a [Shop](../buildings/shop-stone.md) half as well again as a peasant does, then carries the money home. It has 80 hit points and no real fighting value.

## Abilities

- **[Trade at Shop](../commands/shop.md):** at one of your own Shops that nobody else is using, each round of trade (about 2.75 seconds) uses 1 wood from your stock and earns 0.42 gold (a peasant earns 0.28). The merchant carries up to 20 gold, about 48 wood's worth.
- **Banks:** on the way home it stops at your [Bank](../buildings/bank.md) if one is within 60 m (or nearer than the Village), which raises the load by half, to 30 gold.
- It stops when you run out of wood or the Shop is paused.

## Strong against

- Nothing: it fights only to defend itself (8-point blows).

## Weak against

- **Everything.** Raiders and light cavalry kill merchants quickly, and the [Archangel](michael.md) and similar demons make them flee.

## Trained at / Built by

<!-- gen:trained -->
- **Trained at:** [Warehouse](../buildings/warehouse-stone.md)
<!-- /gen:trained -->

## Needs

<!-- gen:needs -->
- Nothing beyond the building that trains it and the cost.
<!-- /gen:needs -->

## Leads to

<!-- gen:leads -->
- Nothing: it unlocks no building, unit or research.
<!-- /gen:leads -->

## Commands

<!-- gen:commands -->
- [Capture](../commands/capture.md)
- [Halt](../commands/halt.md)
- [Interrogate](../commands/interrogate.md)
- [Convert Goods to Gold](../commands/convert-goods-to-gold.md)
- [Trade at Shop](../commands/shop.md)
- [Deliver Resources (to Deliverable Building)](../commands/deliver-resources-to-deliverable-building.md)
- [Garrison](../commands/garrison.md)
- [Board Vessel](../commands/board-vessel.md)
- [Resupply Ammunition](../commands/resupply.md)
- [Guard Next Unit Selected](../commands/guard.md)
- [Flee](../commands/run-away.md)
- [Move](../commands/move.md)
<!-- /gen:commands -->

## Tips

- Merchants are a steady source of gold when your mines run dry, as long as you have wood to spare.
- Build a Bank close to your Shops: it adds half to every load.
- Keep their route short and away from enemy patrols.
