---
title: Mangonel
ids: [unit/Mangonel, command/Spawn Mangonel]
order: 10
---

# Mangonel

<!-- gen:icon -->
![Mangonel](game:interface/icons/characters/tech_mangonel.tga)
<!-- /gen:icon -->

<!-- gen:facts -->
```facts
blueprint: Mangonel
```

<!-- facts:static -->
| | |
|---|---|
| Cost | 200 food, 200 wood |
| Food upkeep | 2 |
| Hit points | 200 |
| Armour | 5 (front 5, sides 3.75, rear 0.5) |
| Melee attack | 30 every 2 s |
| Missile attack | 30 every 7 s, range 90 m |
| Damage to buildings | 40 per missile |
| Speed | 3.7 |
| Sight | 12 (60 m) |
<!-- /facts:static -->
<!-- /gen:facts -->

## Role

The mangonel is the first siege catapult you can train, with no research: 200 hit points and a 30-point stone (40 to buildings) about every 7 seconds, from 15 m out to 90 m. It costs 200 food and 200 wood at the [Armoury](../buildings/armoury-stone.md). It can't enter forest or rocky ground.

## Abilities

- **[Fire (Mangonel)](../commands/fire-mangonel.md):** a target out of range makes it walk closer by itself. Every stone also hurts all units within 8 m of where it lands, yours and your allies' included, by a tenth of its damage less each one's armour.
- **Ammunition:** a full charge bar holds three stones; the bar refills by about 1 a second, and much faster near a [Supply Wagon](supply-wagon.md), so after the first three it throws about one stone every 19 seconds.
- **Arrows** do it only a quarter of their damage; it even swings a 30-point blow at anyone who closes in.

## Strong against

- **Other siege engines**, which the game rates as a siege weapon's best targets: rams, trebuchets, rocket launchers, siege towers.
- **Towers and walls**: 40 structure a stone from beyond most towers' reach.
- **Packed infantry**: 22 a stone through a [Spearman](spearman.md)'s front, plus the splash on his neighbours.
- **Archers**, whose arrows hardly hurt it.

## Weak against

- **Cavalry**: a [Knight](knight.md) does 24 a blow (armour 5 at the front, almost none behind), and it can't shoot anything closer than 15 m.
- **Gunners and Dragoons**: bullets do 15 a shot.
- **Its own side**: the splash hits your troops fighting near the target.

## Trained at / Built by

<!-- gen:trained -->
- **Trained at:** [Armoury](../buildings/armoury-stone.md)
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
- [Halt](../commands/halt.md)
- [Hold Fire](../commands/hold-fire.md)
- [Interrogate](../commands/interrogate.md)
- [Fire at Will](../commands/fire-at-will.md)
- [Fire (Mangonel)](../commands/fire-mangonel.md)
- [Attack](../commands/attack.md)
- [Board Vessel](../commands/board-vessel.md)
- [Resupply Ammunition](../commands/resupply.md)
- [Guard Next Unit Selected](../commands/guard.md)
- [Flee](../commands/run-away.md)
- [Move](../commands/move.md)
<!-- /gen:commands -->

## Tips

- Keep it behind your troops: it cannot hit anything closer than 15 m.
- Fire the first three stones at what matters most; then it slows down until a Supply Wagon refills it.
- Guard it with spearmen against horsemen.
