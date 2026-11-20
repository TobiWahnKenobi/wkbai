---
title: Watch Tower
ids: [building/Watch Tower, command/Build Watch Tower]
order: 10
---

# Watch Tower

<!-- gen:icon -->
![Watch Tower](game:interface/icons/buildings/pagan_watchtower.tga)
<!-- /gen:icon -->

<!-- gen:facts -->
```facts
blueprint: Watch Tower
```

<!-- facts:static -->
| | |
|---|---|
| Cost | 400 wood |
| Build time | 15 s |
| Strength (structural integrity) | 150 |
| Missile attack | 18 per arrow |
| Repair cost | 400 wood |
| Sight | 15 |
<!-- /facts:static -->
<!-- /gen:facts -->

## Role

The watch tower is a lone wooden lookout. It sees far, warns you of enemies and, with troops inside, shoots at them. It needs a Barracks and can be upgraded into a [Fort](fort-wood.md).

## What it does

- **Garrison:** 5 of your units fit inside, foot or horse, peasants too (not siege engines or carts). Select them and right-click it ([Garrison](../commands/garrison.md)).
- **Shooting:** it shoots only while someone is inside. About every 5 seconds each archer inside looses an arrow and each gunner fires a bullet (28 damage). Other troops shelter but don't shoot, until the Cathedral's [Crossbow](../upgrades/crossbow.md) research: from then on everyone inside shoots.
- **Range and damage:** its range is its sight (15, about 75 m, a little more from high ground). It aims at enemy units only, never at buildings: the nearest, one target for every two shots. Arrows do 18 damage, more when the target stands lower (up to double), and the Blacksmith's [light infantry weapons](../upgrades/light-infantry-iron-tipped-weapon.md) make them hit harder.
- **Orders:** [Attack (Fort)](../commands/fort-attack.md) picks a target in range; [Hold Fire](../commands/hold-fire.md) stops it shooting, [Fire at Will](../commands/fire-at-will.md) starts it again.
- **Leaving:** [Ungarrison](../commands/ungarrison.md) lets everyone out into a ring 10 m around it; [Ungarrison Hand to Hand Defenders](../commands/ungarrison-hand-to-hand-units.md) only those who don't shoot. When it falls below 10% of its strength, everyone inside runs out unhurt.
- **Resupply:** your and your allies' archers, gunners, siege engines and other troops that use ammunition get 10 back every 5 seconds or so while within 25 m of it; 50 m after [Logistics](../upgrades/logistics.md).
- **Lookout:** it alerts you when it spots enemies, like a scout.
- **Upgrade:** [Upgrade Watch Tower to Fort](../upgrades/upgrade-to-wooden-fort.md) turns it into a [Fort](fort-wood.md) (10 places, more strength), once the Guildhall has the [Fort Upgrade](../upgrades/fort-wood.md).

## Defence

- **Strength:** 150. Weak for a strongpoint: a few siege shots bring it down.
- **What hurts it:** soldiers barely scratch it (0.1 a blow). Siege engines, sappers and fire do the work ([siege](../mechanics/siege.md#structural-damage)).
- **Fire:** each flaming arrow that hits it has a 7.5% chance to set it alight. Left alone, a fire grows until the building burns down, and it can leap to buildings within 25 m. Send peasants to [Extinguish Fire](../commands/extinguish.md).
- **Repair:** peasants [repair](../commands/repair.md) it, paying as they go ([strength and repair](../mechanics/buildings-and-construction.md#strength-damage-and-repair)).

## Built by

<!-- gen:trained -->
- **Built by:** [Peasant](../units/peasant.md)
<!-- /gen:trained -->

## Needs

<!-- gen:needs -->
- building a [Barracks](barracks-stone.md)
<!-- /gen:needs -->

## Leads to

<!-- gen:leads -->
- **Research here:** [Upgrade Watch Tower to Fort](../upgrades/upgrade-to-wooden-fort.md)
<!-- /gen:leads -->

## Commands

<!-- gen:commands -->
- [Attack (Fort)](../commands/fort-attack.md)
- [Ungarrison](../commands/ungarrison.md)
- [Ungarrison Hand to Hand Defenders](../commands/ungarrison-hand-to-hand-units.md)
- [Hold Fire](../commands/hold-fire.md)
- [Fire at Will](../commands/fire-at-will.md)
<!-- /gen:commands -->

## Tips

- An empty watch tower doesn't shoot. Put five archers in it.
- Put one on each approach to an outlying Village: its warning buys time.
- Build it on high ground: it sees and shoots further, and its arrows hit harder.
- [Upgrade Watch Tower to Fort](../upgrades/upgrade-to-wooden-fort.md) after the Guildhall's fort research.
