---
title: Manor
ids: [building/Manor, command/Build Manor]
order: 10
---

# Manor

<!-- gen:icon -->
![Manor](game:interface/icons/buildings/manor.tga)
<!-- /gen:icon -->

<!-- gen:facts -->
```facts
blueprint: Manor
```

<!-- facts:static -->
| | |
|---|---|
| Cost | 750 wood, 150 gold |
| Build time | 20 s |
| Strength (structural integrity) | 600 |
| Missile attack | 15 per arrow |
| Repair cost | 750 wood, 150 gold |
| Sight | 10 |
<!-- /facts:static -->
<!-- /gen:facts -->

## Role

The Manor is the heart of your town. It trains peasants and carts, stores goods, shoots at attackers and is where most games start. As you build the buildings of a path it grows: into a Keep, then a Castle, then a Palace, each one stronger.

## What it does

- **Its own archers:** four archers live inside. About every 5 seconds each looses an arrow. Its range is its sight (10, about 50 m, a little more from high ground). It aims at enemy units only, never at buildings: the nearest, one target for every two shots. Arrows do 15 damage, more when the target stands lower (up to double), and the Blacksmith's [light infantry weapons](../upgrades/light-infantry-iron-tipped-weapon.md) make them hit harder.
- **Shelter for peasants:** 50 places. [Sanctuary (Ring Bell)](../commands/ring-bell.md) sends every peasant to the nearest of your buildings with room within 200 m; [Sanctuary (Recall Peasants)](../commands/recall-peasants.md) sends them back to the work they left. Soldiers can't be ordered inside (only Artos can enter). After the [Crossbow](../upgrades/crossbow.md) research everyone inside shoots too, sheltering peasants included.
- **Stores goods:** peasants and carts deliver here, straight into your stock. If an enemy destroys it, they take a share of your stock ([sacks and losses](../mechanics/resources.md#sacks-and-losses)).
- **Militia:** [Conscript all Peasants into Militia](../commands/conscript-all-peasants.md) arms every peasant at once; [Convert all Militia into Peasants](../commands/demob-all-militia.md) sends them back to work.

## Defence

- **Strength:** 600.
- **What hurts it:** soldiers barely scratch it (0.1 a blow). Siege engines, sappers and fire do the work ([siege](../mechanics/siege.md#structural-damage)).
- **Fire:** each flaming arrow that hits it has a 5% chance to set it alight. Left alone, a fire grows until the building burns down, and it can leap to buildings within 25 m. Send peasants to [Extinguish Fire](../commands/extinguish.md).
- **Repair:** peasants [repair](../commands/repair.md) it, paying as they go ([strength and repair](../mechanics/buildings-and-construction.md#strength-damage-and-repair)).

## Built by

<!-- gen:trained -->
- **Built by:** [Peasant](../units/peasant.md)
<!-- /gen:trained -->

## Needs

<!-- gen:needs -->
- building a [Imperial Castle](castle-celestial.md) or building a [Pagan Castle](castle-pagan.md) or building a [Renaissance Castle](castle-tech.md)
<!-- /gen:needs -->

## Leads to

<!-- gen:leads -->
- **Trains:** [Peasant](../units/peasant.md), [Cart](../units/cart.md)
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

- Keep it training peasants for most of the early game.
- Ring the bell ([Sanctuary (Ring Bell)](../commands/ring-bell.md)) when raiders come: the peasants hide, and come back to work when you recall them.
- It shoots without a garrison, but only four arrows at a time: it needs soldiers around it against a real attack.
- [Conscript all Peasants into Militia](../commands/conscript-all-peasants.md) arms the whole town at once; [Convert all Militia into Peasants](../commands/demob-all-militia.md) sends them back to work.
- A second Manor needs a Castle first.
