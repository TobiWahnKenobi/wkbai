---
title: Awaken Gold Elemental
ids: [command/Awaken Gold Elemental]
order: 10
---

# Awaken Gold Elemental

## Role

The [Arch Druid](../units/arch-druid.md) wakes a gold deposit and turns it into a [Gold Elemental](../units/gold-elemental-large.md). It needs an [Arcanum](../buildings/arcanum.md).

## How it works

- **Target:** a gold deposit. Right-click it; the cursor changes over it when you have built an Arcanum and he has enough mana.
- **Range:** he walks up to it (within 5 m).
- **Effect:** the gold deposit is gone and the elemental rises there. Its size depends on how much gold was left: a full gold deposit (200) gives a full-size elemental, a nearly empty one a small one (at least a fifth of full size). Hit points, reach, melee damage and damage to buildings grow with the size; small elementals are a little faster. As it is hurt, it shrinks.
- **Cost:** as much mana as there is gold left in the gold deposit: 10 to 200. The arch druid's mana bar holds 200. It refills by 2 every 1.9 s (about 3 minutes from empty to full), less while his elementals live (see [Awaken Wood Elemental](awaken-wood-elemental.md)). Supply wagons don't refill it.
- **Upkeep:** while it lives, his bar refills at half speed (1 instead of 2 every 1.9 s). The full rate comes back when it dies.
- **Limits:** if the arch druid dies, his gold elementals crumble into gold that peasants can gather.

## Offered by

<!-- gen:trained -->
- **Offered by:** [Arch Druid](../units/arch-druid.md)
- No button of its own: you give it with a right-click on the target (see [Orders](../controls/orders.md#the-right-click)).
<!-- /gen:trained -->

## Needs

<!-- gen:needs -->
- Nothing: it is there whenever the unit or building is.
<!-- /gen:needs -->

