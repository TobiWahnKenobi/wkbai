---
title: DeathWyrm
ids: [unit/Infernal Engine, command/Spawn Infernal Engine]
order: 10
---

# DeathWyrm

<!-- gen:icon -->
![DeathWyrm](game:interface/icons/characters/pagan_infernal_engine.tga)
<!-- /gen:icon -->

<!-- gen:facts -->
```facts
blueprint: Infernal Engine
```

<!-- facts:static -->
| | |
|---|---|
| Cost | 400 food, 50 gold |
| Food upkeep | 2 |
| Hit points | 500 |
| Armour | 5 (front 5, sides 3.75, rear 0.5) |
| Melee attack | 20 every 2 s |
| Missile attack | 70 every 5 s, range 60 m |
| Damage to buildings | 60 per missile |
| Speed | 3.5 |
| Sight | 12 (60 m) |
<!-- /facts:static -->
<!-- /gen:facts -->

## Role

The DeathWyrm is the Pagan war machine from the [Henge](../buildings/henge.md): 500 hit points and three ranged attacks, a stone that hits for 70 (60 to buildings), a poison cloud and a patch of slime. It costs 400 food and 50 gold. It is slow (speed 3.5), has armour 5 at the front and almost none behind, and does not fire on its own until you tell it to.

## Trained at / Built by

<!-- gen:trained -->
- **Trained at:** [Henge](../buildings/henge.md)
<!-- /gen:trained -->

## Needs

<!-- gen:needs -->
- [DeathWyrm](../upgrades/infernal-engine.md) or building a [University](../buildings/university.md) or building a [The Wickerman](../buildings/wickerman.md)
- Pagan or neutral side only: the button goes once you build a [Church](../buildings/church-stone.md) (the Imperial path).
<!-- /gen:needs -->

## Leads to

<!-- gen:leads -->
- Nothing: it unlocks no building, unit or research.
<!-- /gen:leads -->

## Commands

<!-- gen:commands -->
- [Capture](../commands/capture.md)
- [Halt](../commands/halt.md)
- [Enter Dark Portal](../commands/enter-dark-portal.md)
- [Hold Fire](../commands/hold-fire.md)
- [Fire at Will](../commands/fire-at-will.md)
- [Spit Gallstone](../commands/spit-projectile.md)
- [Squirt Poison Bile](../commands/poison-bile.md)
- [Squirt Slime](../commands/squirt-ichor.md)
- [Resupply Ammunition](../commands/resupply.md)
- [Board Vessel](../commands/board-vessel.md)
- [Guard Next Unit Selected](../commands/guard.md)
- [Flee](../commands/run-away.md)
- [Move](../commands/move.md)
<!-- /gen:commands -->

## Abilities

All three attacks reach 60 m and draw on a charge bar of 60 that starts empty and refills by itself at about 1 point a second.

- **[Spit Gallstone](../commands/spit-projectile.md):** 2 charge a shot. 70 damage and 60 structure damage, minus armour. Every unit within 8 m of the hit, yours included, takes a tenth of that.
- **[Squirt Poison Bile](../commands/poison-bile.md):** 30 charge. Every unit within 15 m of where it lands, of any side, loses 10 hit points every 3 s, 8 times (up to 80 in about 24 s).
- **[Squirt Slime](../commands/squirt-ichor.md):** 50 charge. A 15 m patch where everyone loses 2 hit points a second and slows to a crawl (speed 1) for about 31 s.
- **Demonic:** holy fire does it double damage and [Exorcise Demons](../commands/exorcism.md) works on it.

## Strong against

Damage numbers below are per blow or per shot, before research, experience, height and formation bonuses: the attack minus the armour on the side that is hit (see [Combat](../mechanics/combat.md)).

- **Everything at range.** 70 a stone: 58 against a knight, 55 against an [Imperial Pikeman](man-at-arms.md).
- **Blobs of troops.** Poison bile and slime hit whole formations, through armour, and the slime holds them in place for your other weapons.
- **Buildings**: 60 structure damage a stone.

## Weak against

- **Cavalry and fast troops.** Every kind of cavalry is rated very well against it; a knight does 24 a blow from the front and 28 from behind.
- **Archers and gunners**: it has no arrow armour, so an arrow does 10 and a bullet 15.
- **Your own troops in the way**: bile and slime hurt your units as much as the enemy's.

## Tips

- Keep it behind your line and order it to fire; it waits for orders.
- Slime a formation, then shoot it with stones while it crawls.
- Keep your own troops out of the bile and slime.
