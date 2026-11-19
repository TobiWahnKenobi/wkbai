---
title: Dragoon
ids: [unit/Mounted Hand Gunner, command/Spawn Mounted Hand Gunner]
order: 10
---

# Dragoon

<!-- gen:icon -->
![Dragoon](game:interface/icons/characters/tech_mounted_gunner.tga)
<!-- /gen:icon -->

<!-- gen:facts -->
```facts
blueprint: Mounted Hand Gunner
```

<!-- facts:static -->
| | |
|---|---|
| Cost | 100 food, 20 gold |
| Food upkeep | 1 |
| Hit points | 120 |
| Armour | 7 (front 1, sides 0, rear 0) |
| Melee attack | 22 every 2 s |
| Missile attack | 20 every 4 s, range 75 m |
| Speed | 12 |
| Sight | 15 (75 m) |
<!-- /facts:static -->
<!-- /gen:facts -->

## Role

The Dragoon is a horseman with a firearm: speed 12, 120 hit points and a 20-point shot every 4 seconds out to 75 m, for 100 food and 20 gold at the [Stables](../buildings/stables.md). It needs [Gunpowder](../upgrades/celestial-gunpowder.md) and is not offered on the Pagan path. It is light cavalry, armoured almost nowhere (1 at the front, none elsewhere).

## Abilities

- **[Fire (Bullet)](../commands/fire-bullet.md):** 13 shots on a full charge bar; the bar refills by about 1 a second, faster near a [Supply Wagon](supply-wagon.md). It needs a clear line of sight and won't fire over hills or walls.
- **Bullets are not arrows:** units that shrug off arrows (siege engines, priests, the Archangel) take a bullet's full damage less their armour.
- **[Skirmish](../commands/skirmish-mode.md):** when enemy heavy infantry, heavy cavalry or a fighting siege beast comes within about 37 m, it rides away to the far side and keeps shooting. It doesn't skirmish in a formation, or with your Palings or heavy infantry close in front of it. [Hold Ground](../commands/hold-ground.md) switches this off.
- **[Follow Next Unit Selected](../commands/follow.md)** keeps it next to another unit.

## Strong against

- **Slow heavy infantry**, which light cavalry rates as its ideal target: 12 a shot through a [Spearman](spearman.md)'s front, 19 in its back, and the spearman can't catch it.
- **Siege engines and priests**: 15 a shot to a [Mangonel](mangonel.md), where an archer does nothing.
- **Light foot**: 18 a shot to an [Archer](archer.md).

## Weak against

- **Heavy cavalry**: a [Knight](knight.md) does 28 a blow and kills it in five.
- **Any melee**: its own blow is 22, but with armour 1 it loses every close fight.
- **Archers**: an [Archer](archer.md) does 14 a shot.
- Hills, walls and woods that block its view.

## Trained at / Built by

<!-- gen:trained -->
- **Trained at:** [Stables](../buildings/stables.md)
<!-- /gen:trained -->

## Needs

<!-- gen:needs -->
- [Gunpowder](../upgrades/celestial-gunpowder.md)
- Imperial or neutral side only: the button goes once you build a [Maypole](../buildings/maypole.md) (the Pagan path).
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
- [Hold Fire](../commands/hold-fire.md)
- [Fire at Will](../commands/fire-at-will.md)
- [Hold Ground](../commands/hold-ground.md)
- [Skirmish](../commands/skirmish-mode.md)
- [Follow Next Unit Selected](../commands/follow.md)
- [Interrogate](../commands/interrogate.md)
- [Fire (Bullet)](../commands/fire-bullet.md)
- [Convert Troops to Peasants](../commands/military-unit-convert-to-peasant.md)
- [Garrison](../commands/garrison.md)
- [Board Vessel](../commands/board-vessel.md)
- [Form Up](../commands/formation.md)
- [Resupply Ammunition](../commands/resupply.md)
- [Guard Next Unit Selected](../commands/guard.md)
- [Flee](../commands/run-away.md)
- [Train](../commands/train.md)
- [Move](../commands/move.md)
<!-- /gen:commands -->

## Tips

- Ride in, fire, ride out: it is a raider, not a line unit. Leave Skirmish on so it backs away from heavy troops by itself.
- Shoot enemy siege engines and clergy, which archers can hardly hurt.
- Keep your own spearmen near it to fend off cavalry.
