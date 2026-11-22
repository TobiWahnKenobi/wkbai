---
title: Inquisitor
ids: [unit/Inquisitor, command/Spawn Inquisitor]
order: 10
---

# Inquisitor

<!-- gen:icon -->
![Inquisitor](game:interface/icons/characters/celestial_Inquisitor.tga)
<!-- /gen:icon -->

<!-- gen:facts -->
```facts
blueprint: Inquisitor
```

<!-- facts:static -->
| | |
|---|---|
| Cost | 200 food, 50 gold |
| Food upkeep | 6 |
| Hit points | 100 |
| Armour | 5 (front 5, sides 3.75, rear 0.5) |
| Melee attack | 15 every 2 s |
| Missile attack | 30 every 5 s, range 70 m |
| Damage to buildings | 25 per missile |
| Speed | 4 |
| Sight | 14 (70 m) |
<!-- /facts:static -->
<!-- /gen:facts -->

## Role

The inquisitor is the Imperial witch hunter from the [Cathedral](../buildings/cathedral-stone.md). He burns heretics, exorcises and banishes demons, and throws holy fire at enemies from 70 m. He has 100 hit points, armour 5 at the front and arrow armour.

## Trained at / Built by

<!-- gen:trained -->
- **Trained at:** [Cathedral](../buildings/cathedral-stone.md)
<!-- /gen:trained -->

## Needs

<!-- gen:needs -->
- building a [Statue of the Archangel](../buildings/michael-statue.md) or building a [Observatory](../buildings/observatory.md)
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
- [Burn a Heretic](../commands/burn-heretic.md)
- [Exorcise Demons](../commands/exorcism.md)
- [Banish Demon](../commands/banish.md)
- [Interrogate](../commands/interrogate.md)
- [Holy Flame](../commands/holy-fire.md)
- [Pray](../commands/pray-at-church.md)
- [Pray for Righteousness](../commands/pray-for-righteousness.md)
- [Resupply Ammunition](../commands/resupply.md)
- [Convert Troops to Peasants](../commands/military-unit-convert-to-peasant.md)
- [Garrison](../commands/garrison.md)
- [Board Vessel](../commands/board-vessel.md)
- [Guard Next Unit Selected](../commands/guard.md)
- [Flee](../commands/run-away.md)
- [Move](../commands/move.md)
<!-- /gen:commands -->

## Abilities

His powers use his own charge bar of 90, which fills by itself at about 1 point a second. Supply Wagons do not fill it.

- **[Holy Flame](../commands/holy-fire.md):** 15 charge a shot, so 6 shots from full. About one shot every 5 s, range 70 m, a clear line of sight needed. 30 damage and 25 structure damage; units within 8 m of the hit take a tenth. **Against demons it does double damage.**
- **[Exorcise Demons](../commands/exorcism.md):** 15 charge, 70 m, on a demon only. 200 damage, shared among the demons of that player within 10 m of the target: a lone demon takes all 200.
- **[Banish Demon](../commands/banish.md):** 50 m, needs at least 20 charge. It costs 1 charge for every 17 hit points the demon has left; if you have enough, the demon is sent back to its owner's palace.
- **[Burn a Heretic](../commands/burn-heretic.md):** walks up to a peasant preaching heresy and burns him.
- **[Pray](../commands/pray-at-church.md)** at a Church (piety 4: worshippers bring back more) and **[Pray for Righteousness](../commands/pray-for-righteousness.md)** at a Cathedral, which fills the pool for the [Bishop](bishop.md)'s acts of God, about 0.9 a second per inquisitor.

Demons are the Gibbering Horde, Succubus, High Priestess, Arch Druid, Spider Demon, DeathWyrm, Fomorian Beast, the elementals, the Behemoth and Abaddon.

## Strong against

Damage numbers below are per blow or per shot, before research, experience, height and formation bonuses: the attack minus the armour on the side that is hit (see [Combat](../mechanics/combat.md)).

- **Pagan magic users and monsters.** Holy flame does 58 to an [Arch Druid](arch-druid.md) or a Spider Demon, 55 to a [Succubus](succubus.md) or a [High Priestess](high-priestess.md), 35 even to a Behemoth or Abaddon. An exorcism does up to 200.
- **Archers**: his arrow armour means an arrow does him nothing from the front. The game rates him well against archers and siege engines.

## Weak against

- **Ordinary soldiers.** Against a knight his holy flame does only 18, and he has 100 hit points: a knight does him 24 a blow, a bullet 15.
- **Running dry**: 6 shots, then about 15 s for each new one.

## Tips

- Send inquisitors after enemy succubi, druids, priestesses and monsters; that is where they pay.
- Save an exorcism for the biggest demon, or for a tight group of them.
- Keep heavy infantry in front of him; he cannot hold a line.
