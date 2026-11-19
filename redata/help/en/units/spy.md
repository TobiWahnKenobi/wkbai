---
title: Spy
ids: [unit/Spy, command/Spawn Spy]
order: 10
---

# Spy

<!-- gen:icon -->
![Spy](game:interface/icons/characters/unaligned_spy.tga)
<!-- /gen:icon -->

<!-- gen:facts -->
```facts
blueprint: Spy
```

<!-- facts:static -->
| | |
|---|---|
| Cost | 100 food, 50 gold |
| Food upkeep | 1 |
| Hit points | 120 |
| Armour | 5 (front 5, sides 3.75, rear 0.5) |
| Melee attack | 15 every 2 s |
| Speed | 3 |
| Sight | 10 (50 m) |
<!-- /facts:static -->
<!-- /gen:facts -->

## Role

The spy works behind enemy lines. It goes about disguised as a peasant, hides in carts, sets fire to buildings, blows open gates and lures enemy peasants to your side. It costs 100 food and 50 gold at the [Tavern](../buildings/tavern-wood.md), has 120 hit points and sees 50 m, but fights no better than a peasant (15-point blow, armour 5).

## Abilities

- **Disguise:** a spy always looks like a peasant to the enemy, and enemy soldiers don't attack it on their own. It is unmasked when an enemy [Priest](priest.md) or Bishop (25 m), Inquisitor (70 m), High Priestess (30 m), Arch Druid (35 m) or spy (50 m) sees it; then everyone attacks it. When it can no longer see any of them it disguises itself again. Scouts and towers don't see through it. More in [disguises and spies](../mechanics/disguises-and-spies.md).
- **[Enter Cart](../commands/enter-cart.md):** the spy hides in a cart, one spy a cart, and rides along unseen. It climbs out when the cart delivers its load or is destroyed. Every 4 seconds you get a glimpse of where the cart is.
- **[Blow Open Gates](../commands/blow-open-gates.md):** 10 seconds at an enemy gatehouse destroy it outright.
- **[Spy on Infantry](../commands/spy-on-infantry.md):** 5 seconds at an enemy Barracks or Hall of Warriors reveal all of that player's foot soldiers on the map for a short while.
- **[Arson Attack](../commands/arson.md):** 15 seconds at an enemy building set it on fire.
- **[Start Mission](../commands/start-mission.md):** once you have built a [Monastery](../buildings/monastery.md), every 20 seconds the nearest enemy peasant within 25 m comes to pray with the spy, walks to your Monastery and becomes your [Archer](archer.md), [Squire](mounted-spearman.md) or [Spearman](spearman.md) (while you are under your population limit). [Stop converting enemy Peasants](../commands/stop-mission.md) ends it.

## Strong against

- **Gatehouses and buildings**, through its tricks rather than its blade.
- **Enemy peasants**, which its mission turns into your soldiers.

## Weak against

- **Clergy and casters** that unmask it, and then any soldier.
- In open fight it loses to almost everything.

## Trained at / Built by

<!-- gen:trained -->
- **Trained at:** [Tavern](../buildings/tavern-wood.md)
<!-- /gen:trained -->

## Needs

<!-- gen:needs -->
- Nothing beyond the building that trains it and the cost.
<!-- /gen:needs -->

## Leads to

<!-- gen:leads -->
- **Can turn into or call up:** [Archer](archer.md), [Squire](mounted-spearman.md), [Spearman](spearman.md)
<!-- /gen:leads -->

## Commands

<!-- gen:commands -->
- [Capture](../commands/capture.md)
- [Take Cart](../commands/take-cart.md)
- [Enter Cart](../commands/enter-cart.md)
- [Halt](../commands/halt.md)
- [Blow Open Gates](../commands/blow-open-gates.md)
- [Spy on Infantry](../commands/spy-on-infantry.md)
- [Interrogate](../commands/interrogate.md)
- [Arson Attack](../commands/arson.md)
- [Attack](../commands/attack.md)
- [Board Vessel](../commands/board-vessel.md)
- [Garrison](../commands/garrison.md)
- [Resupply Ammunition](../commands/resupply.md)
- [Flee](../commands/run-away.md)
- [Move](../commands/move.md)
- [Start Mission](../commands/start-mission.md)
- [Stop converting enemy Peasants](../commands/stop-mission.md)
<!-- /gen:commands -->

## Tips

- Open an enemy gate with [Blow Open Gates](../commands/blow-open-gates.md) just as your army arrives.
- Ride into an enemy town in a cart, then slip out when it delivers.
- Check for enemy priests before you send a spy in: one in sight blows its cover.
- Your own priests and spies unmask enemy spies; keep one near your Barracks and gates.
