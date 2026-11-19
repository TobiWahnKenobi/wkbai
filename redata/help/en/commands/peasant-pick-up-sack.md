---
title: Pick Up Resource Sack (Peasant Pick up Sack)
ids: [command/Peasant Pick up Sack]
order: 10
---

# Pick Up Resource Sack (Peasant Pick up Sack)

## Role

A [peasant](../units/peasant.md) picks up the goods in a sack lying on the ground and carries them home. Sacks are what is left when a Village or a cart is destroyed with goods inside, and anyone's sacks can be taken. Right-click a sack; the cursor shows the delivery cursor.

## How it works

- **Target:** a sack with something in it, while the peasant has room in his load.
- **Speed:** every fifth of a second he takes 1 of each good the sack holds, until his load is full (10) or the sack is empty.
- **Then:** he delivers to the nearest Manor-type building, [Warehouse](../buildings/warehouse-stone.md) or [Village](../buildings/village-wood.md) of yours ([Deliver Resources](deliver-resources-to-deliverable-building.md)) and comes back for more. An empty sack vanishes, and he moves on to the nearest sack within 30 m.
- **What arrives:** as with any delivery, stone counts as 2 materials and food is cut for upkeep.

## Offered by

<!-- gen:trained -->
- **Offered by:** [Peasant](../units/peasant.md)
- No button of its own: you give it with a right-click on the target (see [Orders](../controls/orders.md#the-right-click)).
<!-- /gen:trained -->

## Needs

<!-- gen:needs -->
- Nothing: it is there whenever the unit or building is.
<!-- /gen:needs -->

