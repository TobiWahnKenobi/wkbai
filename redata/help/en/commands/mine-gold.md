---
title: Mine Gold
ids: [command/Mine Gold]
order: 10
---

# Mine Gold

<!-- gen:icon -->
![Mine Gold](game:interface/icons/SIC_Gold_Mine.tga)
<!-- /gen:icon -->

## Role

Sends [peasants](../units/peasant.md) to mine gold at a gold outcrop. Right-click the outcrop; the cursor shows the mining cursor.

## How it works

- **Target:** a gold outcrop that still has gold. There is no button for it: you give it with a right-click.
- **Speed:** 1 gold per blow of the pick. A load is full at 10 gold.
- **The outcrop:** it shrinks as it is mined and is gone when empty. The miner then moves to the nearest gold outcrop within 30 m of the old one, or else within 70 m of himself.
- **Going home:** to the nearest Manor-type building, [Warehouse](../buildings/warehouse-stone.md) or [Village](../buildings/village-wood.md) of yours ([Deliver Resources](deliver-resources-to-deliverable-building.md)). Gold reaches your stock as it is; no building multiplies it.
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

