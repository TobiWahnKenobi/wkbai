---
title: Bombard
ids: [unit/Bombard, command/Spawn Bombard]
order: 10
---

# Bombard

<!-- gen:icon -->
![Bombard](game:interface/icons/characters/tech_bombard.tga)
<!-- /gen:icon -->

<!-- gen:facts -->
```facts
blueprint: Bombard
```

<!-- facts:static -->
| | |
|---|---|
| Cost | 100 food, 100 wood, 50 gold |
| Food upkeep | 2 |
| Hit points | 100 |
| Armour | 5 (front 5, sides 3.5, rear 0.5) |
| Missile attack | 45 every 10 s, range 150 m |
| Damage to buildings | 50 per missile |
| Speed | 3.7 |
| Sight | 12 (60 m) |
<!-- /facts:static -->
<!-- /gen:facts -->

## Role

The bombard is an early cannon: 45 damage (50 to buildings) every 10 seconds or so, from 20 m out to about 150 m. It needs [Bombard Upgrade](../upgrades/gunpowder.md) and is hidden once you take a pure path. It has only 100 hit points, armour 5 at the front and almost none behind, and starts on Hold Fire.

## Trained at / Built by

<!-- gen:trained -->
- **Trained at:** [Armoury](../buildings/armoury-stone.md)
<!-- /gen:trained -->

## Needs

<!-- gen:needs -->
- [Bombard Upgrade](../upgrades/gunpowder.md)
- Not on the pure paths: the button goes once you build a [Statue of the Archangel](../buildings/michael-statue.md) or a [The Wickerman](../buildings/wickerman.md).
<!-- /gen:needs -->

## Leads to

<!-- gen:leads -->
- Nothing: it unlocks no building, unit or research.
<!-- /gen:leads -->

## Commands

<!-- gen:commands -->
- [Capture](../commands/capture.md)
- [Halt](../commands/halt.md)
- [Hold Fire](../commands/hold-fire.md)
- [Interrogate](../commands/interrogate.md)
- [Fire at Will](../commands/fire-at-will.md)
- [Fire (Bombard)](../commands/fire-bombard.md)
- [Board Vessel](../commands/board-vessel.md)
- [Resupply Ammunition](../commands/resupply.md)
- [Guard Next Unit Selected](../commands/guard.md)
- [Flee](../commands/run-away.md)
- [Move](../commands/move.md)
<!-- /gen:commands -->

## Abilities

- **[Fire (Bombard)](../commands/fire-bombard.md):** 45 damage and 50 structure damage, one shot every 8-12 s. It picks targets by itself out to about 140 m and fires from up to 150 m. It cannot hit anything closer than 20 m. Every unit within 8 m of the hit, yours included, takes a tenth of the damage.
- **Ammunition:** 3 shots. Ammunition comes back by itself at about 1 point a second, and faster within 25 m of a [Supply Wagon](supply-wagon.md) (50 m after [Logistics](../upgrades/logistics.md)). One shot takes about 43 s to come back on its own.
- **[Hold Fire](../commands/hold-fire.md):** it starts with fire held; give it [Fire at Will](../commands/fire-at-will.md) or a target.
- **Arrow armour:** arrows and javelins do only a quarter of their damage. It cannot climb slopes steeper than 30°.

## Strong against

Damage numbers below are per blow or per shot, before research, experience, height and formation bonuses: the attack minus the armour on the side that is hit (see [Combat](../mechanics/combat.md)).

- **Towers, forts and buildings**: 50 structure a shot from outside their range.
- **Heavy targets.** A ball does 33 to a knight, 30 to an [Imperial Pikeman](man-at-arms.md) and still 20 to a [Behemoth](behemoth.md) or [Abaddon](abaddon.md), where arrows do nothing.
- **Archers**: their arrows do it nothing from the front.

## Weak against

- **Cavalry and anything that gets close.** Inside 20 m it cannot fire; a knight does 24 a blow and it has 100 hit points. Every kind of cavalry and infantry is rated well against it.
- **Gunners**: 15 a bullet.

## Tips

- Shell towers and forts from a safe distance.
- Bring a [Supply Wagon](supply-wagon.md): three shots go fast, and refilling alone is slow.
- Guard it with pikes or spears against cavalry.
