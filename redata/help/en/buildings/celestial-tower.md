---
title: Ramparts Tower
ids: [building/Celestial Tower, building/Celestial Tower Corner In, building/Celestial Tower Corner Out]
order: 10
---

# Ramparts Tower

<!-- gen:facts -->
```facts
blueprint: Celestial Tower
```

<!-- facts:static -->
| | |
|---|---|
| Strength (structural integrity) | 900 |
| Missile attack | 15 per arrow |
| Sight | 19 |
<!-- /facts:static -->
<!-- /gen:facts -->

## Role

A Ramparts Tower is a section of Ramparts raised into a tower ([Upgrade to Ramparts Tower](../upgrades/upgrade-to-celestial-tower.md)): strength 900, and troops inside shoot from it.

## What it does

- **Garrison:** 5 of your units fit inside, foot or horse, peasants too (not siege engines or carts). Select them and right-click it ([Garrison](../commands/garrison.md)).
- **Shooting:** it shoots only while someone is inside. About every 5 seconds each archer inside looses an arrow. Other troops shelter but don't shoot, until the Cathedral's [Crossbow](../upgrades/crossbow.md) research: from then on everyone inside shoots.
- **Range and damage:** its range is its sight (19, about 95 m, a little more from high ground). It aims at enemy units only, never at buildings: the nearest, one target for every two shots. Arrows do 15 damage, more when the target stands lower (up to double), and the Blacksmith's [light infantry weapons](../upgrades/light-infantry-iron-tipped-weapon.md) make them hit harder.
- **Orders:** [Attack (Fort)](../commands/fort-attack.md) picks a target in range; [Hold Fire](../commands/hold-fire.md) stops it shooting, [Fire at Will](../commands/fire-at-will.md) starts it again.
- **Leaving:** [Ungarrison](../commands/ungarrison.md) lets everyone out into a ring 10 m around it. When it falls below 10% of its strength, everyone inside runs out unhurt.
- **Made from a wall:** a tower is a wall section raised by research at that section. It blocks like the wall around it.

## Defence

- **Strength:** 900.
- **What hurts it:** soldiers barely scratch it (0.1 a blow). Siege engines, sappers and fire do the work ([siege](../mechanics/siege.md#structural-damage)).
- **Fire:** fireproof. Flaming arrows don't set it alight; a spy's arson, Greek fire and lightning still can.
- **Repair:** peasants [repair](../commands/repair.md) it, paying as they go ([strength and repair](../mechanics/buildings-and-construction.md#strength-damage-and-repair)).

## Built by

<!-- gen:trained -->
- **Comes from:** [Upgrade Wall to Rampart Tower](../upgrades/upgrade-to-celestial-tower.md), [Ramparts Upgrade](../upgrades/technological-wall-to-celestial-wall.md)
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
