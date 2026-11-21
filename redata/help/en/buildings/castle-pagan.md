---
title: Pagan Castle
ids: [building/Castle_Pagan]
order: 10
---

# Pagan Castle

<!-- gen:facts -->
```facts
blueprint: Castle_Pagan
```

<!-- facts:static -->
| | |
|---|---|
| Strength (structural integrity) | 2000 |
| Missile attack | 15 per arrow |
| Repair cost | 750 wood, 150 gold |
| Sight | 10 |
<!-- /facts:static -->
<!-- /gen:facts -->

## Role

The Pagan Castle is your Manor's third level. The Keep becomes this Castle by itself when you finish a Temple of the Moon. It has structural integrity 2000 and opens the third tier of research at the Blacksmith; with a Castle you may also build another Manor.

## What it does

- **Its own archers:** four archers live inside. About every 5 seconds each looses an arrow. Its range is its sight (10, about 50 m, a little more from high ground). It aims at enemy units only, never at buildings: the nearest, one target for every two shots. Arrows do 15 damage, more when the target stands lower (up to double), and the Blacksmith's [light infantry weapons](../upgrades/light-infantry-iron-tipped-weapon.md) make them hit harder.
- **Shelter for peasants:** 50 places. [Sanctuary (Ring Bell)](../commands/ring-bell.md) sends every peasant to the nearest of your buildings with room within 200 m; [Sanctuary (Recall Peasants)](../commands/recall-peasants.md) sends them back to the work they left. Soldiers can't be ordered inside (only Artos can enter). After the [Crossbow](../upgrades/crossbow.md) research everyone inside shoots too, sheltering peasants included.
- **Stores goods:** peasants and carts deliver here, straight into your stock. If an enemy destroys it, they take a share of your stock ([sacks and losses](../mechanics/resources.md#sacks-and-losses)).
- **Militia:** [Conscript all Peasants into Militia](../commands/conscript-all-peasants.md) arms every peasant at once; [Convert all Militia into Peasants](../commands/demob-all-militia.md) sends them back to work.
- **Resupply:** your and your allies' archers, gunners, siege engines and other troops that use ammunition get 10 back every 5 seconds or so while within 25 m of it; 50 m after [Logistics](../upgrades/logistics.md).
- **Falling:** below 10% of its strength, everyone inside runs out unhurt.

## Defence

- **Strength:** 2000.
- **What hurts it:** soldiers barely scratch it (0.1 a blow). Siege engines, sappers and fire do the work ([siege](../mechanics/siege.md#structural-damage)).
- **Fire:** each flaming arrow that hits it has a 0.5% chance to set it alight. Left alone, a fire grows until the building burns down, and it can leap to buildings within 25 m. Send peasants to [Extinguish Fire](../commands/extinguish.md).
- **Repair:** peasants [repair](../commands/repair.md) it, paying as they go ([strength and repair](../mechanics/buildings-and-construction.md#strength-damage-and-repair)).

## Built by

<!-- gen:trained -->
- **Comes from:** building a [Temple of the Moon](black-mass.md)
<!-- /gen:trained -->

## Needs

<!-- gen:needs -->
- Nothing beyond the cost.
<!-- /gen:needs -->

## Leads to

<!-- gen:leads -->
- **Trains:** [Peasant](../units/peasant.md), [Cart](../units/cart.md)
- **Building it unlocks:** [Manor](manor.md)
<!-- /gen:leads -->

## Commands

<!-- gen:commands -->
- [Attack (Fort)](../commands/fort-attack.md)
- [Ungarrison](../commands/ungarrison.md)
- [Hold Fire](../commands/hold-fire.md)
- [Fire at Will](../commands/fire-at-will.md)
- [Sanctuary (Ring Bell)](../commands/ring-bell.md)
- [Sanctuary (Recall Peasants)](../commands/recall-peasants.md)
- [Conscript all Peasants into Militia](../commands/conscript-all-peasants.md)
- [Convert all Militia into Peasants](../commands/demob-all-militia.md)
<!-- /gen:commands -->

## Tips

- Use [Sanctuary (Ring Bell)](../commands/ring-bell.md) to send peasants into shelter during a raid.
- A second [Manor](manor.md) elsewhere gives you another place to train peasants and store goods; it also spreads your stock, so one lost headquarters costs you less.
