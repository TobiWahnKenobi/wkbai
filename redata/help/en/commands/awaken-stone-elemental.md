---
title: Awaken Stone Elemental
ids: [command/Awaken Stone Elemental]
order: 10
---

# Awaken Stone Elemental

## Role

The [Arch Druid](../units/arch-druid.md) wakes a rock outcrop and turns it into a [Stone Elemental](../units/stone-elemental-large.md). It needs an [Arcanum](../buildings/arcanum.md).

## How it works

- **Target:** a rock outcrop. Right-click it; the cursor changes over it when you have built an Arcanum and he has enough mana.
- **Range:** he walks up to it (within 5 m).
- **Effect:** the rock outcrop is gone and the elemental rises there. Its size depends on how much stone was left: a full rock outcrop (200) gives a full-size elemental, a nearly empty one a small one (at least a fifth of full size). Hit points, reach, melee damage and damage to buildings grow with the size; small elementals are a little faster. As it is hurt, it shrinks.
- **Cost:** as much mana as there is stone left in the rock outcrop: 10 to 200. The arch druid's mana bar holds 200. It refills by 2 every 1.9 s (about 3 minutes from empty to full), less while his elementals live (see [Awaken Wood Elemental](awaken-wood-elemental.md)). Supply wagons don't refill it.
- **Upkeep:** while it lives, his bar refills a third slower (1.34 instead of 2 every 1.9 s). The full rate comes back when it dies.
- **Limits:** if the arch druid dies, his stone elementals crumble into stone that peasants can gather.

## Offered by

<!-- gen:trained -->
- **Offered by:** [Arch Druid](../units/arch-druid.md)
- No button of its own: you give it with a right-click on the target (see [Orders](../controls/orders.md#the-right-click)).
<!-- /gen:trained -->

## Needs

<!-- gen:needs -->
- Nothing: it is there whenever the unit or building is.
<!-- /gen:needs -->

