---
title: Deliver Resources (Cart  to Deliverable Building and Return to Village)
ids: [command/Cart Deliver Resources to Deliverable Building and Return to Village]
order: 10
---

# Deliver Resources (Cart  to Deliverable Building and Return to Village)

## Role

Starts a [Cart](../units/cart.md)'s round trip with the stores it carries: it takes them to your stock and then goes back to its Village for the next load. Right-click your Manor-type building or a [Warehouse](../buildings/warehouse-stone.md) with the cart; the cursor shows the delivery cursor.

## How it works

- **Where it unloads:** the nearest Manor-type building or Warehouse of yours that it can reach. A [Village](../buildings/village-wood.md) of yours that lies on the way, nearer to that store, takes the load instead. If the cart's own Village has a [supply line](set-up-overland-supply-route.md), the load goes there.
- **What arrives:** wood, food and gold as they are, each stone as 2 materials. Food carried by cart isn't cut for upkeep a second time.
- **Back at the Village:** the cart waits until the village holds at least 50 goods and no other cart is loading there (it checks every 5 seconds), then loads up to 500 of whatever the village holds, and sets off again.
- **A spy inside:** a spy hiding in the cart ([Enter Cart](enter-cart.md)) climbs out when it unloads.

## Offered by

<!-- gen:trained -->
- **Offered by:** [Cart](../units/cart.md)
- No button of its own: you give it with a right-click on the target (see [Orders](../controls/orders.md#the-right-click)).
<!-- /gen:trained -->

## Needs

<!-- gen:needs -->
- Nothing: it is there whenever the unit or building is.
<!-- /gen:needs -->

