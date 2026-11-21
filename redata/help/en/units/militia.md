---
title: Militia
ids: [unit/Militia]
order: 10
---

# Militia

<!-- gen:facts -->
```facts
blueprint: Militia
```

<!-- facts:static -->
| | |
|---|---|
| Cost | 75 food |
| Hit points | 80 |
| Armour | 10 (front 7, sides 2, rear 0) |
| Melee attack | 20 every 2 s |
| Speed | 4 |
| Sight | 5 (25 m) |
<!-- /facts:static -->
<!-- /gen:facts -->

## Role

Militia are peasants with weapons. You get them by [converting a peasant](../commands/convert-to-militia.md): it walks to your nearest Manor or [Village](../buildings/village-wood.md) and takes up arms there, at no cost. Militia eat no food. With 80 hit points, a 20-point blow every 2 seconds and armour 7 from the front (2 on the sides, none behind) they count as heavy infantry, but they hold off raiders, not a real army. They don't go back to work on their own.

## Abilities

- **[Convert to Peasant](../commands/convert-to-peasant.md):** point at your own Manor or Village. The militiaman walks there, waits 2 seconds and becomes a peasant again.

## Strong against

- **Raiders and light foot**: 18 a blow to an [Archer](archer.md), 15 to a [Vandavar Barbarian](vandavar.md).
- **Peasants and other civilians**, which have almost no armour.

## Weak against

- **Real soldiers.** A [Spearman](spearman.md) hits militia for 23 and takes 12; a [Knight](knight.md) kills one in four blows.
- **Their back**, armour 0.
- The [Archangel](michael.md) and similar demons make idle militia flee.

## Trained at / Built by

<!-- gen:trained -->
- **Comes from:** [Conscript Peasant into Militia](../commands/convert-to-militia.md)
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
- [Join Formation](../commands/join-formation.md)
- [Halt](../commands/halt.md)
- [Attack at Will](../commands/aggression.md)
- [Stand your Ground](../commands/passivity.md)
- [Take Cart](../commands/take-cart.md)
- [Enter Dark Portal](../commands/enter-dark-portal.md)
- [Interrogate](../commands/interrogate.md)
- [Convert to Peasant](../commands/convert-to-peasant.md)
- [Attack](../commands/attack.md)
- [Garrison](../commands/garrison.md)
- [Form Up](../commands/formation.md)
- [Resupply Ammunition](../commands/resupply.md)
- [Guard Next Unit Selected](../commands/guard.md)
- [Flee](../commands/run-away.md)
- [Move](../commands/move.md)
<!-- /gen:commands -->

## Tips

- Call them up when the town is attacked and send them back to work with [Convert to Peasant](../commands/convert-to-peasant.md) afterwards.
- Every peasant under arms is a peasant not gathering, so keep it short.
- They can [form up](../commands/formation.md) like other heavy infantry; a Line makes them a little sturdier.
