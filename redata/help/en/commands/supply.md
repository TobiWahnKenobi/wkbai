---
title: Distribute Food Rations (Supply)
ids: [command/Supply]
order: 10
---

# Distribute Food Rations (Supply)

<!-- gen:icon -->
![Distribute Food Rations (Supply)](game:interface/icons/sic_heal.tga)
<!-- /gen:icon -->

## Role

A [Supply Wagon](../units/supply-wagon.md) hands out food to heal your wounded. Select the wagon and right-click a wounded unit of yours or an ally's; the cursor shows the heal cursor.

## How it works

- **Target:** a wounded unit of yours or an ally's. Machines, elementals and units that can't be healed don't count.
- **Reach:** the wagon drives to within 5 m of the unit you clicked, then heals all around it.
- **Area:** every wounded unit of yours or your allies within 30 m of the wagon.
- **Effect:** each second, every one of them gets 5 hit points back (about 8 after [Surgery](../upgrades/surgery.md)), never above full.
- **Cost:** 1 food from your stock for every unit healed, every second. With no food left, the wagon stops healing.
- **When it ends:** when nobody within 30 m is wounded any more.

## Offered by

<!-- gen:trained -->
- **Offered by:** [Supply Wagon](../units/supply-wagon.md)
<!-- /gen:trained -->

## Needs

<!-- gen:needs -->
- Nothing: it is there whenever the unit or building is.
<!-- /gen:needs -->

## Tips

- Many wounded units around the wagon cost food fast: one food each per second.
