---
title: Transport Resources from Village to Global Stocks
ids: [command/Transport Resources from Village to Global Stocks]
order: 10
---

# Transport Resources from Village to Global Stocks

## Role

The [Cart](../units/cart.md)'s main job: it carries a Village's goods to your stock, over and over. Right-click a [Village](../buildings/village-wood.md) or a [Dock](../buildings/dock-wood.md) with the cart; the cursor shows the delivery cursor. A new Village asks for a cart by itself when it is finished.

## How it works

- **Loading:** the cart waits at the Village until it holds at least 50 goods and no other cart is loading there (checking every 5 seconds), then takes up to 500, a share of each good the village holds.
- **Unloading:** at the nearest Manor-type building or [Warehouse](../buildings/warehouse-stone.md) of yours it can reach, or at a Village of yours on the way that lies nearer to it. Wood, food and gold arrive as they are, each stone as 2 materials.
- **Supply lines:** if the Village has a [supply line](set-up-overland-supply-route.md), the cart takes its goods there instead. If no Manor-type building can be reached over land (a village on an island), the Village's goods go to your nearest Dock, for a [Cog](../units/cog.md) to ship ([Set Up Overseas Supply Line](set-up-oversea-supply-route.md)).
- **A spy inside:** a spy hiding in the cart ([Enter Cart](enter-cart.md)) climbs out when it unloads.
- **It keeps going** until you give the cart another order or the Village is gone.

## Offered by

<!-- gen:trained -->
- **Offered by:** [Cart](../units/cart.md)
- No button of its own: you give it with a right-click on the target (see [Orders](../controls/orders.md#the-right-click)).
<!-- /gen:trained -->

## Needs

<!-- gen:needs -->
- Nothing: it is there whenever the unit or building is.
<!-- /gen:needs -->

## Tips

- Carts are slow and easy prey. Keep the road between a far Village and your Manor guarded, or put a [Warehouse](../buildings/warehouse-stone.md) closer.
