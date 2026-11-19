---
title: Mercenary
ids: [unit/Mercenary, command/Spawn Mercenary]
order: 10
---

# Mercenary

<!-- gen:icon -->
![Mercenary](game:interface/icons/characters/unaligned_mercenary.tga)
<!-- /gen:icon -->

<!-- gen:facts -->
```facts
blueprint: Mercenary
```

<!-- facts:static -->
| | |
|---|---|
| Cost | 20 gold |
| Hit points | 120 |
| Armour | 8 (front 10, sides 5, rear 2) |
| Melee attack | 25 every 3 s |
| Speed | 3 |
| Sight | 3 (15 m) |
<!-- /facts:static -->
<!-- /gen:facts -->

## Role

The mercenary is paid in gold only (20 gold at the [Tavern](../buildings/tavern-wood.md)) and eats no food, which makes it useful when food is short. It is heavy infantry with 120 hit points, a 25-point blow every 3 seconds and armour 10 from the front, 5 on the sides and 2 from behind. It can also set buildings on fire.

## Abilities

- **[Arson Attack](../commands/arson.md):** the mercenary stands at an enemy building for 15 seconds and sets it alight. If it is interrupted, nothing burns. A fresh fire takes 5 structure at once and then keeps burning, growing stronger and spreading to buildings within 25 m until peasants put it out. A formation that is given the order breaks up and every member sets a fire.

## Strong against

- **Buildings**, through arson: fire does far more than its 0.1 structure damage a blow.
- **Light foot**: 23 a blow to an [Archer](archer.md) or [Gunner](gunner.md).

## Weak against

- **Regular heavy infantry.** It does 17 every 3 seconds to a [Spearman](spearman.md); the spearman does 20 every 2.5 seconds back.
- **Cavalry**: a [Knight](knight.md) does 19 a blow and the mercenary only 13 back.
- **Archangel and demons**: idle mercenaries flee from them.
- It sees little (sight 3) and is slow (speed 3).

## Trained at / Built by

<!-- gen:trained -->
- **Trained at:** [Tavern](../buildings/tavern-wood.md)
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
- [Attack at Will](../commands/aggression.md)
- [Stand your Ground](../commands/passivity.md)
- [Take Cart](../commands/take-cart.md)
- [Enter Dark Portal](../commands/enter-dark-portal.md)
- [Scale Siege Tower](../commands/scale-siege-tower.md)
- [Interrogate](../commands/interrogate.md)
- [Arson Attack](../commands/arson.md)
- [Attack](../commands/attack.md)
- [Convert Troops to Peasants](../commands/military-unit-convert-to-peasant.md)
- [Garrison](../commands/garrison.md)
- [Board Vessel](../commands/board-vessel.md)
- [Form Up](../commands/formation.md)
- [Train](../commands/train.md)
- [Resupply Ammunition](../commands/resupply.md)
- [Guard Next Unit Selected](../commands/guard.md)
- [Flee](../commands/run-away.md)
- [Move](../commands/move.md)
<!-- /gen:commands -->

## Tips

- Hire mercenaries when your farms cannot feed more soldiers.
- Use them to burn outlying enemy buildings while your army keeps the enemy busy.
- Pair them with faster units that spot for them.
