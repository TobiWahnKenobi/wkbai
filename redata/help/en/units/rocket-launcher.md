---
title: Rocket Launcher
ids: [unit/Rocket Launcher, unit/Rocket Launcher Limbered, building/Rocket Launcher, command/Build Rocket Launcher]
order: 10
---

# Rocket Launcher

<!-- gen:icon -->
![Rocket Launcher](game:interface/icons/characters/tech_missile_launcher.tga)
<!-- /gen:icon -->

<!-- gen:facts -->
```facts
blueprint: Rocket Launcher
```

<!-- facts:static -->
| | |
|---|---|
| Cost | 500 wood, 500 gold |
| Build time | 80 s |
| Food upkeep | 2 |
| Hit points | 400 |
| Armour | 5 (front 5, sides 5, rear 5) |
| Melee attack | 20 every 6 s |
| Sight | 16 (80 m) |
<!-- /facts:static -->
<!-- /gen:facts -->

## Role

The rocket launcher fires rockets that ignore armour. A [Sapper](sapper.md) builds it for 500 wood and 500 gold in 80 seconds, after [Rocketry](../upgrades/rocketry.md). It has 400 hit points and armour 5, and comes out limbered: it must be unlimbered to fire, and can't move while unlimbered.

## Abilities

- **[Fire Rocket](../commands/fire-rocket.md):** from 25 m out to 200 m. A rocket takes 150 hit points and 250 structure from everything within 10 m of where it hits, and 60 hit points and 80 structure from everything within 30 m. The unit or building it strikes takes another 150 and 250. Armour, research and arrow-proofing don't count, and it hits your own and allied units as well.
- **Ammunition:** one rocket per full charge bar; the bar refills in about 38 seconds, faster near a [Supply Wagon](supply-wagon.md).
- **[Limber](../commands/limber.md)** (5 seconds) packs it up to move at speed 3; **[Unlimber](../commands/unlimber.md)** sets it up again. It can't fire while limbered.
- Set up, arrows do it only a quarter of their damage.

## Strong against

- **Packed formations**: one rocket can wreck a whole block of men.
- **Heavily armoured targets**: the [Archangel](michael.md), giants, elephants and the [Undead Legion](undead-legion.md) take the full damage.
- **Buildings**: 580 structure to the building it strikes.

## Weak against

- **Cavalry**: a [Knight](knight.md) does 24 a blow, and it can't shoot inside 25 m or run while set up.
- **Slow fire**: one rocket every 38 seconds.
- **Your own troops** near the target.

## Trained at / Built by

<!-- gen:trained -->
- **Built by:** [Sapper](sapper.md) (the engine is put up on the spot like a building, then rolls out)
<!-- /gen:trained -->

## Needs

<!-- gen:needs -->
- [Rocketry](../upgrades/rocketry.md)
<!-- /gen:needs -->

## Leads to

<!-- gen:leads -->
- Nothing: it unlocks no building, unit or research.
<!-- /gen:leads -->

## Commands

<!-- gen:commands -->
- [Capture](../commands/capture.md)
- [Interrogate](../commands/interrogate.md)
- [Limber](../commands/limber.md)
- [Hold Fire](../commands/hold-fire.md)
- [Fire at Will](../commands/fire-at-will.md)
- [Fire Rocket](../commands/fire-rocket.md)
- [Halt](../commands/halt.md)
- [Resupply Ammunition](../commands/resupply.md)
- [Guard Next Unit Selected](../commands/guard.md)
- [Unlimber](../commands/unlimber.md)
- [Flee](../commands/run-away.md)
- [Move](../commands/move.md)
<!-- /gen:commands -->

## Tips

- [Limber](../commands/limber.md) it to move and [Unlimber](../commands/unlimber.md) it to shoot; plan where it will stand.
- Fire into enemy formations before your troops close in, never into a melee.
- Guard it closely: it can't escape once set up.
