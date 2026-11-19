---
title: Holy Fortress
ids: [building/Fort Stone, command/Build Fort Stone]
order: 10
---

# Holy Fortress

<!-- gen:icon -->
![Holy Fortress](game:interface/icons/buildings/Fortstone.tga)
<!-- /gen:icon -->

<!-- gen:facts -->
```facts
blueprint: Fort Stone
```

<!-- facts:static -->
| | |
|---|---|
| Cost | 1000 wood, 150 gold |
| Build time | 60 s |
| Strength (structural integrity) | 900 |
| Missile attack | 20 per arrow |
| Repair cost | 1000 wood, 150 gold |
| Sight | 23 |
<!-- /facts:static -->
<!-- /gen:facts -->

## Role

The Holy Fortress is the strongest lone strongpoint: strength 900, room for 20 men and the longest reach of any building (sight 23). It needs [Shield of God](../upgrades/masonry.md) and is not offered on the Pagan path.

## What it does

- **Garrison:** 20 of your units fit inside, foot or horse, peasants too (not siege engines or carts). Select them and right-click it ([Garrison](../commands/garrison.md)).
- **Shooting:** it shoots only while someone is inside. About every 5 seconds each archer inside looses an arrow and each gunner fires a bullet (30 damage). Other troops shelter but don't shoot, until the Cathedral's [Crossbow](../upgrades/crossbow.md) research: from then on everyone inside shoots.
- **Range and damage:** its range is its sight (23, about 115 m, a little more from high ground). It aims at enemy units only, never at buildings: the nearest, one target for every two shots. Arrows do 20 damage, more when the target stands lower (up to double), and the Blacksmith's [light infantry weapons](../upgrades/light-infantry-iron-tipped-weapon.md) make them hit harder.
- **Orders:** [Attack (Fort)](../commands/fort-attack.md) picks a target in range; [Hold Fire](../commands/hold-fire.md) stops it shooting, [Fire at Will](../commands/fire-at-will.md) starts it again.
- **Leaving:** [Ungarrison](../commands/ungarrison.md) lets everyone out into a ring 10 m around it; [Ungarrison Hand to Hand Defenders](../commands/ungarrison-hand-to-hand-units.md) only those who don't shoot. When it falls below 10% of its strength, everyone inside runs out unhurt.
- **Resupply:** your and your allies' archers, gunners, siege engines and other troops that use ammunition get 10 back every 5 seconds or so while within 25 m of it; 50 m after [Logistics](../upgrades/logistics.md).

## Defence

- **Strength:** 900.
- **What hurts it:** soldiers barely scratch it (0.1 a blow). Siege engines, sappers and fire do the work ([siege](../mechanics/siege.md#structural-damage)).
- **Fire:** each flaming arrow that hits it has a 1% chance to set it alight. Left alone, a fire grows until the building burns down, and it can leap to buildings within 25 m. Send peasants to [Extinguish Fire](../commands/extinguish.md).
- **Repair:** peasants [repair](../commands/repair.md) it, paying as they go ([strength and repair](../mechanics/buildings-and-construction.md#strength-damage-and-repair)).

## Built by

<!-- gen:trained -->
- **Built by:** [Peasant](../units/peasant.md)
- **Comes from:** [Upgrade to Holy Fortress](../upgrades/upgrade-to-stone-fort.md)
<!-- /gen:trained -->

## Needs

<!-- gen:needs -->
- [Shield of God](../upgrades/masonry.md)
- [Fort Upgrade](../upgrades/fort-wood.md)
- Imperial or neutral side only: the button goes once you build a [Maypole](maypole.md) (the Pagan path).
<!-- /gen:needs -->

## Leads to

<!-- gen:leads -->
- Nothing new: it trains no units and offers no research.
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

- Fill it with archers or gunners: twenty shooters loose twenty shots every 5 seconds.
- The [Crossbow](../upgrades/crossbow.md) research makes everyone inside shoot, not only archers.
- Build it where your armies gather: it refills their ammunition.
