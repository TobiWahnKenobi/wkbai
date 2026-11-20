---
title: Scout
ids: [unit/Scout, command/Spawn Scout]
order: 10
---

# Scout

<!-- gen:icon -->
![Scout](game:interface/icons/characters/unaligned_scout.tga)
<!-- /gen:icon -->

<!-- gen:facts -->
```facts
blueprint: Scout
```

<!-- facts:static -->
| | |
|---|---|
| Cost | 60 food |
| Food upkeep | 1 |
| Hit points | 50 |
| Armour | 5 (front 1, sides 0, rear 0) |
| Melee attack | 4 every 2 s |
| Speed | 8 |
| Sight | 20 (100 m) |
<!-- /facts:static -->
<!-- /gen:facts -->

## Role

The scout explores. It is fast (speed 8) and sees 100 m (sight 20), the farthest of any foot or horse unit, but has only 50 hit points and a 4-point blow. Train it at the [Trading Post](../buildings/trading-post.md) for 60 food. It can climb mountain slopes that other units can't.

## Abilities

- **[Reconnoitre](../commands/recon.md):** the scout explores on its own, heading for the enemy's least explored base. When an enemy tower, fort, gatehouse, Manor-type building, archer or light horseman first sees it, it backs off 100 m and tries another way, and it avoids places where it was scared. Below half health it goes home to heal if you have a Supply Wagon or Monastery.
- **[Stop Exploring](../commands/stop-recon.md)** hands it back to you.
- **Warnings:** when it is not exploring on its own, it sounds the alarm whenever it sees an enemy soldier. It can't see through a spy's disguise.
- **[Telescope](../upgrades/telescope.md):** after this research (100 gold at the [Observatory](../buildings/observatory.md)) a scout that stands still raises its telescope and sees twice as far, 200 m.

## Strong against

- Nothing in a fight.

## Weak against

- **Everything.** Light cavalry rates scouts as an ideal target and is faster.
- **Towers**, which outrange its 50 hit points.
- The [Archangel](michael.md) and similar demons make idle scouts flee.

## Trained at / Built by

<!-- gen:trained -->
- **Trained at:** [Trading Post](../buildings/trading-post.md)
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
- [Take Cart](../commands/take-cart.md)
- [Halt](../commands/halt.md)
- [Enter Dark Portal](../commands/enter-dark-portal.md)
- [Interrogate](../commands/interrogate.md)
- [Attack](../commands/attack.md)
- [Convert Troops to Peasants](../commands/military-unit-convert-to-peasant.md)
- [Garrison](../commands/garrison.md)
- [Board Vessel](../commands/board-vessel.md)
- [Scale Siege Tower](../commands/scale-siege-tower.md)
- [Reconnoitre](../commands/recon.md)
- [Stop Exploring](../commands/stop-recon.md)
- [Train](../commands/train.md)
- [Resupply Ammunition](../commands/resupply.md)
- [Guard Next Unit Selected](../commands/guard.md)
- [Flee](../commands/run-away.md)
- [Move](../commands/move.md)
<!-- /gen:commands -->

## Tips

- Send it off with [Reconnoitre](../commands/recon.md) early on to find where the enemy builds.
- With the Telescope, park scouts on hilltops near the enemy's approach as a cheap watch post.
- Leave one standing near your border to warn you of attacks.
