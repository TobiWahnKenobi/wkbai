---
title: Gunner
ids: [unit/Gunner, command/Spawn Gunner]
order: 10
---

# Gunner

<!-- gen:icon -->
![Gunner](game:interface/icons/characters/tech_gunner.tga)
<!-- /gen:icon -->

<!-- gen:facts -->
```facts
blueprint: Gunner
```

<!-- facts:static -->
| | |
|---|---|
| Cost | 100 food, 15 gold |
| Food upkeep | 1 |
| Hit points | 50 |
| Armour | 6 (front 2, sides 1, rear 0) |
| Melee attack | 15 every 2 s |
| Missile attack | 20 every 4.2 s, range 70 m |
| Damage to buildings | 1 per missile |
| Speed | 5.5 |
| Sight | 14 (70 m) |
<!-- /facts:static -->
<!-- /gen:facts -->

## Role

The gunner fires a musket for 20 damage about every 4 seconds, out to 70 m. It costs 100 food and 15 gold at the [Barracks](../buildings/barracks-stone.md) and needs [Bombard Upgrade](../upgrades/gunpowder.md), [Gunpowder](../upgrades/celestial-gunpowder.md) or a [University](../buildings/university.md). It is hidden once you take a pure path (Statue of the Archangel or Wickerman). It has 50 hit points and armour 2 at the front.

## Trained at / Built by

<!-- gen:trained -->
- **Trained at:** [Barracks](../buildings/barracks-stone.md)
<!-- /gen:trained -->

## Needs

<!-- gen:needs -->
- [Bombard Upgrade](../upgrades/gunpowder.md) or [Gunpowder](../upgrades/celestial-gunpowder.md) or building a [University](../buildings/university.md)
- Not on the pure paths: the button goes once you build a [Statue of the Archangel](../buildings/michael-statue.md) or a [The Wickerman](../buildings/wickerman.md).
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
- [Take Cart](../commands/take-cart.md)
- [Enter Dark Portal](../commands/enter-dark-portal.md)
- [Interrogate](../commands/interrogate.md)
- [Fire (Bullet)](../commands/fire-bullet.md)
- [Convert Troops to Peasants](../commands/military-unit-convert-to-peasant.md)
- [Garrison](../commands/garrison.md)
- [Board Vessel](../commands/board-vessel.md)
- [Form Up](../commands/formation.md)
- [Train](../commands/train.md)
- [Scale Siege Tower](../commands/scale-siege-tower.md)
- [Resupply Ammunition](../commands/resupply.md)
- [Guard Next Unit Selected](../commands/guard.md)
- [Flee](../commands/run-away.md)
- [Move](../commands/move.md)
<!-- /gen:commands -->

## Abilities

- **[Fire (Bullet)](../commands/fire-bullet.md):** 20 damage, one shot every 3.4-5 s, range 70 m. It needs a clear line of sight: it does not fire over walls or hills.
- **Bullets go through arrow armour.** Siege engines, clerics and monsters that shrug off arrows take full damage from bullets.
- **Ammunition:** 16 shots. Ammunition comes back by itself at about 1 point a second, and faster within 25 m of a [Supply Wagon](supply-wagon.md) (50 m after [Logistics](../upgrades/logistics.md)).
- **[Skirmish](../commands/skirmish-mode.md):** steps back to 40 m from approaching enemies.
- **[Take Cart](../commands/take-cart.md).**
- The [Bayonet](../upgrades/upgrade-to-bayonet.md) research turns every gunner into a [Gunner (with Bayonet)](gunner-bayonet.md).

## Strong against

Damage numbers below are per blow or per shot, before research, experience, height and formation bonuses: the attack minus the armour on the side that is hit (see [Combat](../mechanics/combat.md)).

- **Siege engines and clerics.** 15 a bullet to a mangonel, a bombard or a bishop, where an arrow does nothing.
- **Light troops**: 18 to an archer, 19 to a peasant or a javelin rider.
- **Armour, better than arrows do**: 8 to a knight's or a [Man-at-Arms](pikeman.md)'s front, against 3 for an arrow.

## Weak against

- **Archers**: the game rates archers better against gunners than the other way round, and archers can shoot over cover while the gunner needs a clear view.
- **Heavy cavalry.** A knight does 27 a blow: two blows. Heavy cavalry is rated well against all missile troops.
- **The heaviest armour**: 2 to a [Warrior of the Sun](pagan-pikeman.md), 1 to a [Behemoth](behemoth.md) or [Abaddon](abaddon.md).

## Tips

- Gunners are fragile (50 hit points): keep them behind a line.
- Use them against what arrows cannot hurt: engines, clerics and arrow-armoured monsters.
- Give them open ground; behind a hedge or a crest they will not fire.
