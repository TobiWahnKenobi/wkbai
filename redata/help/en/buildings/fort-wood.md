---
title: Fort
ids: [building/Fort Wood, command/Build Fort Wood]
order: 10
---

# Fort

<!-- gen:icon -->
![Fort](game:interface/icons/buildings/Fortwood.tga)
<!-- /gen:icon -->

<!-- gen:facts -->
```facts
blueprint: Fort Wood
```

<!-- facts:static -->
| | |
|---|---|
| Cost | 700 wood, 70 gold |
| Build time | 40 s |
| Strength (structural integrity) | 400 |
| Missile attack | 15 per arrow |
| Repair cost | 700 wood, 70 gold |
| Sight | 18 |
<!-- /facts:static -->
<!-- /gen:facts -->

## Role

The Fort is a wooden strongpoint for ten men. Garrisoned, it shoots at enemies around it and refills your archers' ammunition. It needs the Guildhall's [Fort Upgrade](../upgrades/fort-wood.md) research and a Barracks, and it can be rebuilt in stone as the [Holy Fortress](fort-stone.md).

## What it does

- **Garrison:** 10 of your units fit inside, foot or horse, peasants too (not siege engines or carts). Select them and right-click it ([Garrison](../commands/garrison.md)).
- **Shooting:** it shoots only while someone is inside. About every 5 seconds each archer inside looses an arrow and each gunner fires a bullet (25 damage). Other troops shelter but don't shoot, until the Cathedral's [Crossbow](../upgrades/crossbow.md) research: from then on everyone inside shoots.
- **Range and damage:** its range is its sight (18, about 90 m, a little more from high ground). It aims at enemy units only, never at buildings: the nearest, one target for every two shots. Arrows do 15 damage, more when the target stands lower (up to double), and the Blacksmith's [light infantry weapons](../upgrades/light-infantry-iron-tipped-weapon.md) make them hit harder.
- **Orders:** [Attack (Fort)](../commands/fort-attack.md) picks a target in range; [Hold Fire](../commands/hold-fire.md) stops it shooting, [Fire at Will](../commands/fire-at-will.md) starts it again.
- **Leaving:** [Ungarrison](../commands/ungarrison.md) lets everyone out into a ring 10 m around it; [Ungarrison Hand to Hand Defenders](../commands/ungarrison-hand-to-hand-units.md) only those who don't shoot. When it falls below 10% of its strength, everyone inside runs out unhurt.
- **Resupply:** your and your allies' archers, gunners, siege engines and other troops that use ammunition get 10 back every 5 seconds or so while within 25 m of it; 50 m after [Logistics](../upgrades/logistics.md).
- **Upgrade:** [Upgrade to Holy Fortress](../upgrades/upgrade-to-stone-fort.md) after [Shield of God](../upgrades/masonry.md): 20 places, strength 900.

## Defence

- **Strength:** 400.
- **What hurts it:** soldiers barely scratch it (0.1 a blow). Siege engines, sappers and fire do the work ([siege](../mechanics/siege.md#structural-damage)).
- **Fire:** each flaming arrow that hits it has a 7.5% chance to set it alight. Left alone, a fire grows until the building burns down, and it can leap to buildings within 25 m. Send peasants to [Extinguish Fire](../commands/extinguish.md).
- **Repair:** peasants [repair](../commands/repair.md) it, paying as they go ([strength and repair](../mechanics/buildings-and-construction.md#strength-damage-and-repair)).

## Built by

<!-- gen:trained -->
- **Built by:** [Peasant](../units/peasant.md)
- **Comes from:** [Upgrade Watch Tower to Fort](../upgrades/upgrade-to-wooden-fort.md)
<!-- /gen:trained -->

## Needs

<!-- gen:needs -->
- [Fort Upgrade](../upgrades/fort-wood.md)
- building a [Barracks](barracks-stone.md)
<!-- /gen:needs -->

## Leads to

<!-- gen:leads -->
- **Research here:** [Upgrade to Holy Fortress](../upgrades/upgrade-to-stone-fort.md)
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

- Garrison archers or gunners: other troops only shelter (until the Crossbow research).
- Ten archers mean ten arrows every 5 seconds at five targets.
- Archers and siege engines fighting next to it keep their ammunition topped up.
- [Upgrade to Holy Fortress](../upgrades/upgrade-to-stone-fort.md) after [Shield of God](../upgrades/masonry.md).
