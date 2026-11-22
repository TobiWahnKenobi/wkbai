---
title: Corsair
ids: [unit/Arrow Boat, command/Spawn Arrow Boat]
order: 10
---

# Corsair

<!-- gen:icon -->
![Corsair](game:interface/icons/characters/unaligned_archer_boat.tga)
<!-- /gen:icon -->

<!-- gen:facts -->
```facts
blueprint: Arrow Boat
```

<!-- facts:static -->
| | |
|---|---|
| Cost | 100 food, 400 wood |
| Food upkeep | 3 |
| Hit points | 300 |
| Armour | 10 (front 8, sides 8, rear 8) |
| Melee attack | 20 every 2 s |
| Missile attack | 26 every 4 s, range 70 m |
| Speed | 8 |
| Sight | 14 (70 m) |
<!-- /facts:static -->
<!-- /gen:facts -->

## Role

The Corsair is a warship full of archers: 300 hit points, armour 8 on every side and volleys of three bolts, each for 26. It costs 100 food and 400 wood at the [Dock](../buildings/dock-wood.md). It never runs out of bolts.

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

- **[Flaming Arrow Salvo](../commands/arrow-boat-attack.md):** left to itself, it shoots 3 bolts every 4 s at up to the 3 nearest enemy units within 70 m. Given a target, it puts all 3 bolts into it every 5 s.
- Its bolts count as arrows: anything with arrow armour takes only a quarter.
- **[Repair (Docking)](../commands/docking.md):** within 25 m of your dock it is repaired by 10 hit points a second, for 1 wood a second.

## Strong against

Damage numbers below are per blow or per shot, before research, experience, height and formation bonuses: the attack minus the armour on the side that is hit (see [Combat](../mechanics/combat.md)).

- **Fire Boats.** Three bolts do 25 each to a Fire Boat's 70 hit points: one volley sinks it.
- **Troops on the shore.** 24 a bolt to an archer, 14 to a knight, three bolts at a time.
- **Cogs from the side** (16 a bolt) and anything without armour.

## Weak against

- **[Galleasses](bombard-boat.md)**: armour 22 lets only 4 of each bolt through, while each Galleass ball does 42.
- **Siege engines and clerics on the shore**, with their arrow armour.
- **Massed Fire Boats** if it cannot sink them first: each one does 22 an arrow, three arrows every 2 s.

## Tips

- Clear enemy ships and shore troops before your cogs land.
- Hunt Fire Boats with it; leave Galleasses to your own Galleasses.
- Send it home to the dock to repair between fights.
