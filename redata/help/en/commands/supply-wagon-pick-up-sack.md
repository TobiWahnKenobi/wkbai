---
title: Pick Up Resource Sack (Supply Wagon - Pick up Sack)
ids: [command/Supply Wagon - Pick up Sack]
order: 10
---

# Pick Up Resource Sack (Supply Wagon - Pick up Sack)

## Role

A [Supply Wagon](../units/supply-wagon.md) collects the goods from sacks lying on the ground. Right-click a sack; the cursor shows the delivery cursor.

## How it works

- **Target:** a sack with something in it, while the wagon has room.
- **Load:** the wagon holds up to 800, far more than a peasant.
- **Speed:** every fifth of a second it takes 1 of each good the sack holds.
- **Then:** when a sack is empty it vanishes, and the wagon moves on to the nearest sack within 30 m, until none are left or it is full. It doesn't take the goods home by itself: send it to a store with [Deliver Resources](deliver-resources-to-deliverable-building.md).

## Offered by

<!-- gen:trained -->
- **Offered by:** [Supply Wagon](../units/supply-wagon.md)
- No button of its own: you give it with a right-click on the target (see [Orders](../controls/orders.md#the-right-click)).
<!-- /gen:trained -->

## Needs

<!-- gen:needs -->
- Nothing: it is there whenever the unit or building is.
<!-- /gen:needs -->

