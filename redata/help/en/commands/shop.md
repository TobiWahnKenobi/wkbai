---
title: Trade at Shop
ids: [command/Shop]
order: 10
---

# Trade at Shop

<!-- gen:icon -->
![Trade at Shop](game:interface/icons/sic_shop.tga)
<!-- /gen:icon -->

<!-- gen:facts -->
<!-- facts:static -->
| | |
|---|---|
| Cost | 2 wood |
<!-- /facts:static -->
<!-- /gen:facts -->

## Role

Sets a [peasant](../units/peasant.md) or [Merchant](../units/merchant.md) to keep a [Shop](../buildings/shop-stone.md), turning your materials into gold. Right-click one of your shops that has no keeper; the cursor shows the shop cursor.

## How it works

- **Target:** a Shop of yours without a keeper. One keeper per shop: if it is taken, he goes to your nearest shop that has none, and gives up if there isn't one.
- **Trade:** every 2.75 seconds the keeper takes 1 materials from your stock and makes 0.28 gold (a Merchant 0.42 gold).
- **Load:** a peasant is full at 10 gold, about 36 rounds (100 seconds); a Merchant carries 20. He then takes the gold to your nearest Manor-type building or [Warehouse](../buildings/warehouse-stone.md), or a [Village](../buildings/village-wood.md) of yours on the way, and comes back.
- **Bank:** if a [Bank](../buildings/bank.md) of yours or an ally's stands within 60 m (or nearer than the nearest Village), he stops there first and his gold counts 1.5 times.
- **No materials:** when your materials run out, the keeper waits at the shop and checks every 5 seconds.
- **Paused:** while the shop is [paused](pause-shopping.md), the keeper waits and uses nothing.
- **Limits:** a unit under [Possess](possess.md) can't take the order.

## Offered by

<!-- gen:trained -->
- **Offered by:** [Merchant](../units/merchant.md), [Peasant](../units/peasant.md)
<!-- /gen:trained -->

## Needs

<!-- gen:needs -->
- Nothing: it is there whenever the unit or building is.
<!-- /gen:needs -->

## Tips

- When a Shop is finished, one of its builders becomes its keeper by himself.
- A Merchant earns half as much again as a peasant for the same materials; a Bank next to the shop multiplies that again.
