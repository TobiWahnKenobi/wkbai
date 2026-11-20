---
title: Fire Boat
ids: [unit/Fire Boat, command/Spawn Fire Boat]
order: 10
---

# Fire Boat

<!-- gen:icon -->
![Fire Boat](game:interface/icons/characters/Fire_Boat.tga)
<!-- /gen:icon -->

<!-- gen:facts -->
```facts
blueprint: Fire Boat
```

<!-- facts:static -->
| | |
|---|---|
| Cost | 100 food, 200 wood |
| Food upkeep | 2 |
| Hit points | 70 |
| Armour | 10 (front 1, sides 1, rear 1) |
| Melee attack | 20 every 2 s |
| Missile attack | 30 every 2 s, range 35 m |
| Damage to buildings | 5 per missile |
| Speed | 10 |
| Sight | 7 (35 m) |
<!-- /facts:static -->
<!-- /gen:facts -->

## Role

The fire boat is a fast (speed 10) but flimsy ship, 70 hit points and armour 1, that showers ships and the shore with burning arrows: three every 2 seconds, 30 damage each. It costs 100 food and 200 wood at the [Dock](../buildings/dock-wood.md).

## Trained at / Built by

<!-- gen:trained -->
- **Trained at:** [Dock](../buildings/dock-wood.md)
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
- [Halt](../commands/halt.md)
- [Hold Fire](../commands/hold-fire.md)
- [Fire at Will](../commands/fire-at-will.md)
- [Flaming Arrow Salvo](../commands/arrow-boat-attack.md)
- [Guard Next Unit Selected](../commands/guard.md)
- [Repair (Docking)](../commands/docking.md)
- [Move](../commands/move.md)
<!-- /gen:commands -->

## Abilities

- **[Flaming Arrow Salvo](../commands/arrow-boat-attack.md):** every 2 s it shoots 3 burning arrows at enemies within 35 m: 30 damage and 5 structure damage each. They are not ordinary arrows, so arrow armour does not help against them. They do not set buildings on fire, but they can set off an [Explosive Cart](explosive-cart.md).
- **[Repair (Docking)](../commands/docking.md):** within 25 m of your dock it is repaired by 10 hit points a second, for 1 wood a second.

## Strong against

Damage numbers below are per blow or per shot, before research, experience, height and formation bonuses: the attack minus the armour on the side that is hit (see [Combat](../mechanics/combat.md)).

- **Ships at close range.** 22 an arrow to a [Corsair](arrow-boat.md), 20 to a [Cog](cog.md)'s side, 8 even to a [Galleass](bombard-boat.md), three arrows every 2 s.
- **Troops on the shore**, including siege engines and clerics with arrow armour: 18 to a knight's front, 25 to a mangonel's.

## Weak against

- **Corsairs.** One volley of three bolts (25 each) sinks it.
- **Anything that shoots back**: 70 hit points last two Galleass balls or five arrows.
- **Range**: at 35 m it has to get close.

## Tips

- Send several at once: they burn fast and die fast.
- Use their speed to close on Galleasses and Cogs, and keep away from Corsairs.
