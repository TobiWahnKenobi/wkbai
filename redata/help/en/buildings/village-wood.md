---
title: Village
ids: [building/Village Wood, command/Build Village Wood]
order: 10
---

# Village

<!-- gen:icon -->
![Village](game:interface/icons/buildings/village.tga)
<!-- /gen:icon -->

<!-- gen:facts -->
```facts
blueprint: Village Wood
```

<!-- facts:static -->
| | |
|---|---|
| Cost | 300 wood |
| Build time | 30 s |
| Strength (structural integrity) | 400 |
| Repair cost | 300 wood |
<!-- /facts:static -->
<!-- /gen:facts -->

## Role

The Village is a small settlement of peasants away from the Manor. It trains peasants and carts, takes in goods gathered nearby and sends them home by cart. Most other buildings need a Village first.

## What it does

- **Store:** peasants working nearby deliver here instead of walking home. The goods stay in the Village's own store.
- **Cart:** when it is finished, the nearest Manor sends it a [Cart](../units/cart.md) for free. The cart carries up to 500 at a time to your stock ([carrying and delivering](../mechanics/resources.md#carrying-and-delivering)).
- **Trains** peasants and carts.
- **Losses:** destroyed while holding goods, it leaves them on the ground as sacks that anyone's peasants can pick up.
- **Supply line:** [Up Overland Supply Line](../commands/set-up-overland-supply-route.md) sets a cart route.

## Defence

- **Strength:** 400.
- **What hurts it:** soldiers barely scratch it (0.1 a blow). Siege engines, sappers and fire do the work ([siege](../mechanics/siege.md#structural-damage)).
- **Fire:** each flaming arrow that hits it has a 15% chance to set it alight. Left alone, a fire grows until the building burns down, and it can leap to buildings within 25 m. Send peasants to [Extinguish Fire](../commands/extinguish.md).
- **Repair:** peasants [repair](../commands/repair.md) it, paying as they go ([strength and repair](../mechanics/buildings-and-construction.md#strength-damage-and-repair)).

## Built by

<!-- gen:trained -->
- **Built by:** [Peasant](../units/peasant.md)
- **Comes from:** building a [Imperial Castle](castle-celestial.md), building a [Pagan Castle](castle-pagan.md), building a [Renaissance Castle](castle-tech.md), building a [Imperial Keep](keep-celestial.md), building a [Pagan Keep](keep-pagan.md), building a [Renaissance Keep](keep-tech.md), building a [Manor](manor.md), building a [Imperial Palace](palace-celestial.md), building a [Pagan Palace](palace-pagan.md), building a [Renaissance Palace](palace-tech.md)
<!-- /gen:trained -->

## Needs

<!-- gen:needs -->
- Nothing beyond the cost.
<!-- /gen:needs -->

## Leads to

<!-- gen:leads -->
- **Trains:** [Peasant](../units/peasant.md), [Cart](../units/cart.md)
- **Building it unlocks:** [Barracks](barracks-stone.md), [Church](church-stone.md), [Dock](dock-wood.md), [Guildhall](guildhall-stone.md), [Maypole](maypole.md), [Shop](shop-stone.md), [Stables](stables.md), [Standard Farm](standard-farm.md)
<!-- /gen:leads -->

## Commands

<!-- gen:commands -->
- [Up Overland Supply Line](../commands/set-up-overland-supply-route.md)
- [Clear Supply Line](../commands/clear-supply-route.md)
<!-- /gen:commands -->

## Tips

- Build Villages next to good farmland, woods or mines to save walking.
- Carts carry the Village's goods to your stocks: protect the road.
- Don't let a Village fill up far from home: what it holds is lost to the enemy if it falls.
- Unlocks the Farm, Barracks, Stables, Church, Maypole, Guildhall, Shop and Dock.
