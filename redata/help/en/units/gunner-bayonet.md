---
title: Gunner (with Bayonet)
ids: [unit/Gunner Bayonet, unit/Gunner Bayonet Without Bayonet, command/Spawn Gunner Bayonet Without Bayonet]
order: 10
---

# Gunner (with Bayonet)

<!-- gen:icon -->
![Gunner (with Bayonet)](game:interface/icons/characters/tech_gunner.tga)
<!-- /gen:icon -->

<!-- gen:facts -->
```facts
blueprint: Gunner Bayonet
```

<!-- facts:static -->
| | |
|---|---|
| Cost | 100 food, 15 gold |
| Food upkeep | 1 |
| Hit points | 80 |
| Armour | 6 (front 10, sides 5, rear 2) |
| Melee attack | 35 every 3 s |
| Damage to buildings | 1 per missile |
| Speed | 4.5 |
| Sight | 14 (70 m) |
<!-- /facts:static -->
<!-- /gen:facts -->

## Role

After the [Bayonet](../upgrades/upgrade-to-bayonet.md) research (at the [Scriptorium](../buildings/scriptorium.md) or the [University](../buildings/university.md), 180 s), every [Gunner](gunner.md) becomes a gunner with a bayonet. It shoots like a gunner but has 80 hit points instead of 50 and armour 5 at the front. With the bayonet fixed it becomes heavy infantry: it cannot shoot, but fights hand to hand with a 35-point blow every 3 seconds and armour 10 at the front.

## Trained at / Built by

<!-- gen:trained -->
- **Trained at:** [Barracks](../buildings/barracks-stone.md)
- **Comes from:** [Gunner](gunner.md), [Bayonet](../upgrades/upgrade-to-bayonet.md)
<!-- /gen:trained -->

## Needs

<!-- gen:needs -->
- [Bayonet](../upgrades/upgrade-to-bayonet.md)
<!-- /gen:needs -->

## Leads to

<!-- gen:leads -->
- Nothing: it unlocks no building, unit or research.
<!-- /gen:leads -->

## Commands

<!-- gen:commands -->
- [Capture](../commands/capture.md)
- [Take Cart](../commands/take-cart.md)
- [Join Formation](../commands/join-formation.md)
- [Halt](../commands/halt.md)
- [Attack at Will](../commands/aggression.md)
- [Stand your Ground](../commands/passivity.md)
- [Enter Dark Portal](../commands/enter-dark-portal.md)
- [Interrogate](../commands/interrogate.md)
- [Attack](../commands/attack.md)
- [Garrison](../commands/garrison.md)
- [Convert Troops to Peasants](../commands/military-unit-convert-to-peasant.md)
- [Board Vessel](../commands/board-vessel.md)
- [Form Up](../commands/formation.md)
- [Convert to Gunner](../commands/convert-to-gunner.md)
- [Train](../commands/train.md)
- [Scale Siege Tower](../commands/scale-siege-tower.md)
- [Resupply Ammunition](../commands/resupply.md)
- [Guard Next Unit Selected](../commands/guard.md)
- [Flee](../commands/run-away.md)
- [Move](../commands/move.md)
- [Hold Fire](../commands/hold-fire.md)
- [Fire at Will](../commands/fire-at-will.md)
- [Hold Ground](../commands/hold-ground.md)
- [Skirmish](../commands/skirmish-mode.md)
- [Fire (Bullet)](../commands/fire-bullet.md)
- [Convert to Bayonet](../commands/convert-to-bayonet.md)
<!-- /gen:commands -->

## Abilities

- **Musket (bayonet off):** [Fire (Bullet)](../commands/fire-bullet.md), 20 damage about every 4 s, range 70 m, 16 shots, a clear line of sight needed. Bullets go through arrow armour.
- **[Convert to Bayonet](../commands/convert-to-bayonet.md):** fixes the bayonet. It now fights as heavy infantry and can no longer fire.
- **[Convert to Gunner](../commands/convert-to-gunner.md):** takes the bayonet off again to shoot.
- Each change takes as long as the soldier's loading animation.

## Strong against

Damage numbers below are per blow or per shot, before research, experience, height and formation bonuses: the attack minus the armour on the side that is hit (see [Combat](../mechanics/combat.md)).

- **Heavy cavalry, with the bayonet fixed.** 23 a blow to a knight's front, and a knight does it only 19 back. The game rates it well against heavy cavalry, and heavy cavalry that hits it in formation from the front breaks off.
- **Siege engines and clerics, with the musket**, as the plain gunner: 15 a bullet where arrows do nothing.

## Weak against

- **Missiles, with the bayonet fixed.** It can no longer shoot back; the game rates archers and mounted archers well against it.
- **Light cavalry**: javelin riders and mounted archers keep their distance.
- **Real heavy infantry in a long melee**: a [Man-at-Arms](pikeman.md) does it 25 a blow.

## Tips

- Shoot while the enemy approaches, and fix bayonets when cavalry closes in instead of letting the gunners be ridden down.
- Take bayonets off again once the cavalry is gone: a gunner's bullets are worth more than its blade.
