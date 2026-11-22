---
title: Warehouse
ids: [building/Warehouse Stone, command/Build Warehouse Stone]
order: 10
---

# Warehouse

<!-- gen:icon -->
![Warehouse](game:interface/icons/buildings/warehousestone.tga)
<!-- /gen:icon -->

<!-- gen:facts -->
```facts
blueprint: Warehouse Stone
```

<!-- facts:static -->
| | |
|---|---|
| Cost | 200 wood |
| Build time | 20 s |
| Strength (structural integrity) | 800 |
| Repair cost | 150 wood |
<!-- /facts:static -->
<!-- /gen:facts -->

## Role

The warehouse stores goods and trains merchants and supply wagons. It needs a Guildhall.

## What it does

- **Store:** peasants and carts deliver here, straight into your stock, as at the Manor. A warehouse near distant work saves long walks.
- **Trains** merchants and supply wagons.
- **Losses:** if an enemy destroys it, they take a share of your stock, as with a Manor ([sacks and losses](../mechanics/resources.md#sacks-and-losses)).

## Defence

- **Strength:** 800.
- **What hurts it:** soldiers barely scratch it (0.1 a blow). Siege engines, sappers and fire do the work ([siege](../mechanics/siege.md#structural-damage)).
- **Repair:** peasants [repair](../commands/repair.md) it, paying as they go ([strength and repair](../mechanics/buildings-and-construction.md#strength-damage-and-repair)).

## Built by

<!-- gen:trained -->
- **Built by:** [Peasant](../units/peasant.md)
<!-- /gen:trained -->

## Needs

<!-- gen:needs -->
- building a [Guildhall](guildhall-stone.md)
<!-- /gen:needs -->

## Leads to

<!-- gen:leads -->
- **Trains:** [Merchant](../units/merchant.md), [Supply Wagon](../units/supply-wagon.md)
- **Research here:** [Logistics](../upgrades/logistics.md)
<!-- /gen:leads -->

## Commands

<!-- gen:commands -->
- None of its own.
<!-- /gen:commands -->

## Tips

- Supply wagons from here keep an army in the field fed and supplied.
- Research [Logistics](../upgrades/logistics.md) here.
- Merchants coming back from a Trading Post can deliver here.
