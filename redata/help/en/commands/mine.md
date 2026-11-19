---
title: Mine for Stone
ids: [command/Mine]
order: 10
---

# Mine for Stone

<!-- gen:icon -->
![Mine for Stone](game:interface/icons/sic_mine.tga)
<!-- /gen:icon -->

## Role

Sends [peasants](../units/peasant.md) to cut stone at a stone outcrop. Right-click the outcrop; the cursor shows the mining cursor. Stone isn't a stock of its own: each stone delivered adds 2 materials.

## How it works

- **Target:** a stone outcrop that still has stone. There is no button for it: you give it with a right-click.
- **Speed:** 1 stone per blow of the pick. A load is full at 10 stone, which gives 20 materials on delivery.
- **The outcrop:** it shrinks as it is mined and is gone when empty. The miner then moves to the nearest stone outcrop within 30 m of the old one, or else within 70 m of himself.
- **Going home:** like any gatherer, to the nearest Manor-type building, [Warehouse](../buildings/warehouse-stone.md) or [Village](../buildings/village-wood.md) of yours ([Deliver Resources](deliver-resources-to-deliverable-building.md)). There is no mill bonus for stone.
- **Day of rest:** every seventh trip a visit to a church nearby ([church and maypole](../mechanics/peasants-and-work.md#church-and-maypole)).
- **Limits:** a unit under [Possess](possess.md) can't take the order.

## Offered by

<!-- gen:trained -->
- **Offered by:** [Peasant](../units/peasant.md)
<!-- /gen:trained -->

## Needs

<!-- gen:needs -->
- Nothing: it is there whenever the unit or building is.
<!-- /gen:needs -->

