---
title: Gather Materials
ids: [command/Chop Tree]
order: 10
---

# Gather Materials

<!-- gen:icon -->
![Gather Materials](game:interface/icons/SIC_Chop_Wood.tga)
<!-- /gen:icon -->

## Role

Sends [peasants](../units/peasant.md) to cut wood, which becomes your materials. Right-click a tree; the cursor shows the wood-cutting cursor.

## How it works

- **Target:** a tree that still has wood and fewer than five woodcutters.
- **Speed:** 1 wood per stroke, one stroke every 2 seconds. A peasant fills his load of 10 in about 20 seconds.
- **Trees run out:** a tree that has no wood left is gone. The woodcutter then takes the nearest tree with room for him, within 25 m of where he was cutting.
- **Going home:** with a full load he walks to the nearest [Manor](../buildings/manor.md) (or Keep, Castle, Palace), [Warehouse](../buildings/warehouse-stone.md) or [Village](../buildings/village-wood.md) of yours, and comes back for more ([Deliver Resources](deliver-resources-to-deliverable-building.md)).
- **Sawmill:** if a [Sawmill](../buildings/sawmill.md) of yours or an ally's stands within 40 m on the way, he stops there for 6 seconds and his load counts double (×2.5 after [Advanced Sawmill](../upgrades/advanced-sawmill.md)).
- **Day of rest:** every seventh trip he visits a church or maypole nearby, if there is one ([church and maypole](../mechanics/peasants-and-work.md#church-and-maypole)).
- **Limits:** at most five woodcutters on one tree. A unit under [Possess](possess.md) can't take the order.

## Offered by

<!-- gen:trained -->
- **Offered by:** [Peasant](../units/peasant.md)
<!-- /gen:trained -->

## Needs

<!-- gen:needs -->
- Nothing: it is there whenever the unit or building is.
<!-- /gen:needs -->

## Tips

- Woods close to a Village or the Manor save walking time; a Sawmill at the edge of the wood doubles every load.
- A building's rally point on a tree sends its new peasants straight to work there.
