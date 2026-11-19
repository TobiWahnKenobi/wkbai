---
title: Cart
ids: [unit/Cart, command/Spawn Cart]
order: 10
---

# Cart

<!-- gen:icon -->
![Cart](game:interface/icons/characters/unaligned_cart.tga)
<!-- /gen:icon -->

<!-- gen:facts -->
```facts
blueprint: Cart
```

<!-- facts:static -->
| | |
|---|---|
| Cost | 50 food, 50 wood |
| Hit points | 400 |
| Armour | 5 (front 5, sides 3.5, rear 0.5) |
| Melee attack | 15 every 2 s |
| Speed | 5 |
| Sight | 5 (25 m) |
<!-- /facts:static -->
<!-- /gen:facts -->

## Role

The cart moves goods: up to 500 food, wood and gold in all, from a Village or a Dock to your stocks, or to a building that takes deliveries. It has 400 hit points and armour 5 at the front, but its 15-point blow is no real defence.

## Trained at / Built by

<!-- gen:trained -->
- **Trained at:** [Imperial Castle](../buildings/castle-celestial.md), [Imperial Keep](../buildings/keep-celestial.md), [Imperial Palace](../buildings/palace-celestial.md), [Manor](../buildings/manor.md), [Pagan Castle](../buildings/castle-pagan.md), [Pagan Keep](../buildings/keep-pagan.md), [Pagan Palace](../buildings/palace-pagan.md), [Renaissance Castle](../buildings/castle-tech.md), [Renaissance Keep](../buildings/keep-tech.md), [Renaissance Palace](../buildings/palace-tech.md), [Village](../buildings/village-wood.md)
<!-- /gen:trained -->

## Needs

<!-- gen:needs -->
- Nothing beyond the building that trains it and the cost.
<!-- /gen:needs -->

## Leads to

<!-- gen:leads -->
- Nothing: it unlocks no building, unit or research.
<!-- /gen:leads -->

## Commands

<!-- gen:commands -->
- [Capture](../commands/capture.md)
- [Halt](../commands/halt.md)
- [Interrogate](../commands/interrogate.md)
- [Board Vessel](../commands/board-vessel.md)
- [Resupply Ammunition](../commands/resupply.md)
- [Transport Resources from Village to Global Stocks](../commands/transport-resources-from-village-to-global-stocks.md)
- [Deliver Resources (Cart  to Deliverable Building and Return to Village)](../commands/cart-deliver-resources-to-deliverable-building-and-return-to-village.md)
- [Deliver Resources (to Deliverable Building)](../commands/deliver-resources-to-deliverable-building.md)
- [Guard Next Unit Selected](../commands/guard.md)
- [Flee](../commands/run-away.md)
- [Move](../commands/move.md)
<!-- /gen:commands -->

## Abilities

- **[Transport Resources from Village to Global Stocks](../commands/transport-resources-from-village-to-global-stocks.md):** right-click a Village or a Dock. The cart waits until it holds at least 50 goods, loads up, drives them to the Village's supply route or the nearest Palace, Warehouse or Village, and comes back. It keeps doing this until you give it another order.
- **[Deliver Resources (to Deliverable Building)](../commands/deliver-resources-to-deliverable-building.md)** and **[Deliver Resources (Cart to Deliverable Building and Return to Village)](../commands/cart-deliver-resources-to-deliverable-building-and-return-to-village.md):** take the load to a building that accepts deliveries.
- **Losing the driver.** When the cart is down to a quarter of its hit points (100), its driver is killed. The cart stops, keeps its load and has only 40 hit points. Any soldier with [Take Cart](../commands/take-cart.md) (archers, gunners, javelineers, barbarians, knights and others) can climb on: the cart becomes his player's, back at full hit points, with the load.
- It can be carried in a [Cog](cog.md); it takes the room of 3 soldiers.

## Strong against

- Nothing: it is a transport.

## Weak against

- **Raiders.** Cavalry, barbarians and javelin riders kill the driver quickly; an arrow does it 10.

## Tips

- Villages far from the Manor need carts to get their goods home: guard the road between them.
- If an enemy cart loses its driver, send an archer or a rider to take it, load and all.
- Do not leave carts alone near the front.
