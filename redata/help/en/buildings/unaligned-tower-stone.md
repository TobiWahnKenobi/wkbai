---
title: Fortified Tower
ids: [building/Unaligned Tower Stone, building/Unaligned Tower Stone Corner In, building/Unaligned Tower Stone Corner Out]
order: 10
---

# Fortified Tower

<!-- gen:facts -->
```facts
blueprint: Unaligned Tower Stone
```

<!-- facts:static -->
| | |
|---|---|
| Strength (structural integrity) | 400 |
| Missile attack | 15 per arrow |
| Sight | 19 |
<!-- /facts:static -->
<!-- /gen:facts -->

## Role

A Fortified Tower is a section of a Fortified Wall raised into a tower ([Upgrade to Fortified Tower](../upgrades/upgrade-to-unaligned-tower.md)). Troops garrisoned inside shoot from it.

## What it does

- **Garrison:** 5 of your units fit inside, foot or horse, peasants too (not siege engines or carts). Select them and right-click it ([Garrison](../commands/garrison.md)).
- **Shooting:** it shoots only while someone is inside. About every 5 seconds each archer inside looses an arrow. Other troops shelter but don't shoot, until the Cathedral's [Crossbow](../upgrades/crossbow.md) research: from then on everyone inside shoots.
- **Range and damage:** its range is its sight (19, about 95 m, a little more from high ground). It aims at enemy units only, never at buildings: the nearest, one target for every two shots. Arrows do 15 damage, more when the target stands lower (up to double), and the Blacksmith's [light infantry weapons](../upgrades/light-infantry-iron-tipped-weapon.md) make them hit harder.
- **Orders:** [Attack (Fort)](../commands/fort-attack.md) picks a target in range; [Hold Fire](../commands/hold-fire.md) stops it shooting, [Fire at Will](../commands/fire-at-will.md) starts it again.
- **Leaving:** [Ungarrison](../commands/ungarrison.md) lets everyone out into a ring 10 m around it. When it falls below 10% of its strength, everyone inside runs out unhurt.
- **Made from a wall:** a tower is a wall section raised by research at that section. It blocks like the wall around it.

## Defence

- **Strength:** 400.
- **What hurts it:** soldiers barely scratch it (0.1 a blow). Siege engines, sappers and fire do the work ([siege](../mechanics/siege.md#structural-damage)).
- **Fire:** each flaming arrow that hits it has a 5% chance to set it alight. Left alone, a fire grows until the building burns down, and it can leap to buildings within 25 m. Send peasants to [Extinguish Fire](../commands/extinguish.md).
- **Repair:** peasants [repair](../commands/repair.md) it, paying as they go ([strength and repair](../mechanics/buildings-and-construction.md#strength-damage-and-repair)).

## Built by

<!-- gen:trained -->
- **Comes from:** [Upgrade Wall to Fortified Tower](../upgrades/upgrade-to-unaligned-tower.md)
<!-- /gen:trained -->

## Needs

<!-- gen:needs -->
- Nothing beyond the cost.
<!-- /gen:needs -->

## Leads to

<!-- gen:leads -->
- Nothing new: it trains no units and offers no research.
<!-- /gen:leads -->

## Commands

<!-- gen:commands -->
- [Attack (Fort)](../commands/fort-attack.md)
- [Ungarrison](../commands/ungarrison.md)
- [Hold Fire](../commands/hold-fire.md)
- [Fire at Will](../commands/fire-at-will.md)
<!-- /gen:commands -->

## Tips

- An empty tower doesn't shoot: put five archers in each.
- Raise towers at corners and next to gates, where attackers bunch up.
