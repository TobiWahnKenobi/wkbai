---
title: Knight
ids: [unit/Knight, command/Spawn Knight]
order: 10
---

# Knight

<!-- gen:icon -->
![Knight](game:interface/icons/characters/unaligned_knight.tga)
<!-- /gen:icon -->

<!-- gen:facts -->
```facts
blueprint: Knight
```

<!-- facts:static -->
| | |
|---|---|
| Cost | 100 food, 15 gold |
| Food upkeep | 2 |
| Hit points | 180 |
| Armour | 14 (front 12, sides 5, rear 1) |
| Melee attack | 29 every 2.5 s |
| Speed | 8.5 |
| Sight | 4 (20 m) |
<!-- /facts:static -->
<!-- /gen:facts -->

## Role

The knight is the Imperial heavy cavalry: 180 hit points, armour 12 at the front, 5 at the sides and 1 at the rear, and a 29-point blow at speed 8.5. It costs 100 food and 15 gold and needs [Chivalry](../upgrades/chivalry.md) or a [Statue of the Archangel](../buildings/michael-statue.md).

![Knights in a wedge charging enemy archers](shot:gamedata-knight-charge)

## Trained at / Built by

<!-- gen:trained -->
- **Trained at:** [Stables](../buildings/stables.md)
<!-- /gen:trained -->

## Needs

<!-- gen:needs -->
- [Chivalry](../upgrades/chivalry.md) or building a [Statue of the Archangel](../buildings/michael-statue.md)
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
- [Attack at Will](../commands/aggression.md)
- [Stand your Ground](../commands/passivity.md)
- [Interrogate](../commands/interrogate.md)
- [Attack](../commands/attack.md)
- [Garrison](../commands/garrison.md)
- [Convert Troops to Peasants](../commands/military-unit-convert-to-peasant.md)
- [Board Vessel](../commands/board-vessel.md)
- [Form Up](../commands/formation.md)
- [Train](../commands/train.md)
- [Resupply Ammunition](../commands/resupply.md)
- [Guard Next Unit Selected](../commands/guard.md)
- [Flee](../commands/run-away.md)
- [Move](../commands/move.md)
<!-- /gen:commands -->

## Abilities

- **Wedge.** In a [Wedge](../mechanics/formations.md) each rider's first blow does three times the damage.
- **Breaking off.** A rider that strikes formed heavy infantry from the front, when not in a Wedge, breaks off after the blow and rides 25-35 m away.
- **[Take Cart](../commands/take-cart.md):** it can take over a cart whose driver was killed.
- **[Statue of the Archangel](../buildings/michael-statue.md):** finishing it turns all your knights into [Imperial Knights](celestial-knight.md).

## Strong against

Damage numbers below are per blow or per shot, before research, experience, height and formation bonuses: the attack minus the armour on the side that is hit (see [Combat](../mechanics/combat.md)).

- **Missile troops.** 27 a blow to an archer or a gunner: two blows each. Arrows do it 3 from the front, bullets 8.
- **Siege engines**: 24 a blow to a mangonel or a bombard; engines are rated poorly against heavy cavalry.
- **Flanks and rears of infantry**: 23 into a [Man-at-Arms](pikeman.md)'s flank, 28 into his rear.

## Weak against

- **Pikes and spears from the front.** A [Man-at-Arms](pikeman.md) does it 23, an [Imperial Pikeman](man-at-arms.md) 26, a [Warrior of the Sun](pagan-pikeman.md) 28, and it breaks off after one blow from the front unless it attacks in a Wedge. The game rates pikes, spears and militia well against heavy cavalry.
- **Light cavalry**: javelin riders and mounted archers are rated well against it and too fast to catch.
- **Being stuck in a melee**: its rear armour is 1.

## Tips

- Charge archers, siege engines and the flanks of infantry; avoid the front of a pike line.
- Form a Wedge before the charge: the first blow does three times the damage.
- Its rear armour is 1: once it is stuck in a melee, pull it out and charge again.
