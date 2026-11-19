---
title: Archer
ids: [unit/Archer, command/Spawn Archer]
order: 10
---

# Archer

<!-- gen:icon -->
![Archer](game:interface/icons/characters/unaligned_archer.tga)
<!-- /gen:icon -->

<!-- gen:facts -->
```facts
blueprint: Archer
```

<!-- facts:static -->
| | |
|---|---|
| Cost | 60 food, 20 wood |
| Food upkeep | 1 |
| Hit points | 50 |
| Armour | 6 (front 2, sides 1, rear 0) |
| Melee attack | 15 every 1.5 s |
| Missile attack | 15 every 3.2 s, range 70 m |
| Speed | 5.5 |
| Sight | 14 (70 m) |
<!-- /facts:static -->
<!-- /gen:facts -->

## Role

The archer is the basic missile soldier: 60 food and 20 wood at the [Barracks](../buildings/barracks-stone.md), no research needed. It shoots 15-point arrows every 3.2 seconds out to 70 m, but has only 50 hit points and armour 2 at the front, 0 at the rear.

## Trained at / Built by

<!-- gen:trained -->
- **Trained at:** [Barracks](../buildings/barracks-stone.md)
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
- [Hold Fire](../commands/hold-fire.md)
- [Fire at Will](../commands/fire-at-will.md)
- [Hold Ground](../commands/hold-ground.md)
- [Skirmish](../commands/skirmish-mode.md)
- [Enter Dark Portal](../commands/enter-dark-portal.md)
- [Interrogate](../commands/interrogate.md)
- [Resupply Ammunition](../commands/resupply.md)
- [Scale Siege Tower](../commands/scale-siege-tower.md)
- [Take Cart](../commands/take-cart.md)
- [Fire Arrow](../commands/fire-arrow.md)
- [Eat Carrion](../commands/eat-carrion.md)
- [Convert Troops to Peasants](../commands/military-unit-convert-to-peasant.md)
- [Garrison](../commands/garrison.md)
- [Board Vessel](../commands/board-vessel.md)
- [Form Up](../commands/formation.md)
- [Train](../commands/train.md)
- [Guard Next Unit Selected](../commands/guard.md)
- [Flee](../commands/run-away.md)
- [Move](../commands/move.md)
<!-- /gen:commands -->

## Abilities

- **[Fire Arrow](../commands/fire-arrow.md):** 15 damage, about one arrow every 3.2 s, range 70 m. When it cannot see its target directly it lobs the arrow over walls and hills, less accurately.
- **Ammunition:** 6 arrows. Ammunition comes back by itself at about 1 point a second, and faster within 25 m of a [Supply Wagon](supply-wagon.md) (50 m after [Logistics](../upgrades/logistics.md)). One arrow takes about 14 s to come back on its own.
- **Flaming arrows.** After [Flaming Arrow](../upgrades/flaming-arrow.md) (at the [Guildhall](../buildings/guildhall-stone.md)), archers shoot burning arrows by themselves at buildings, ships and explosive carts. A burning arrow uses two arrows' worth of ammunition. A building it hits may catch fire, depending on its fire proofing, and fire spreads to buildings within 25 m.
- **[Skirmish](../commands/skirmish-mode.md):** steps back to 30 m from approaching enemies, shooting as it goes.
- **[Take Cart](../commands/take-cart.md)** and **[Eat Carrion](../commands/eat-carrion.md)** (a hurt archer can eat mutton).

## Strong against

Damage numbers below are per blow or per shot, before research, experience, height and formation bonuses: the attack minus the armour on the side that is hit (see [Combat](../mechanics/combat.md)).

- **Unarmoured troops.** An arrow does 14 to a peasant or a javelin rider, 13 to an archer or a gunner, 13 to a mounted huntress.
- **Gunners**: the game rates archers better against gunners than the other way round, and archers can shoot over cover, which gunners cannot.
- **Cavalry from the side or behind**: 10 into a knight's flank, 14 into its rear, against 3 at the front.

## Weak against

- **Armour from the front.** A knight's front takes 3, a [Man-at-Arms](pikeman.md) 3, an [Imperial Pikeman](man-at-arms.md) 1.
- **Anything with arrow armour.** Siege engines, clerics, supply wagons, elementals, the Behemoth, Abaddon and the Archangel take only a quarter of an arrow's damage: against a mangonel's front that is nothing.
- **Heavy cavalry.** A knight kills an archer in two blows (27 each); every kind of heavy cavalry is rated well against archers.

## Tips

- Put archers behind spearmen or pikemen and let them shoot over the line.
- Aim at flanks and rears; from the front, armoured troops barely notice arrows.
- Keep a Supply Wagon near them in long fights; 6 arrows run out fast.
- Use gunners or engines, not archers, against siege weapons.
