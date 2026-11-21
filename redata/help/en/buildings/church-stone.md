---
title: Church
ids: [building/Church Stone, command/Build Church Stone]
order: 10
---

# Church

<!-- gen:icon -->
![Church](game:interface/icons/buildings/church_stone.tga)
<!-- /gen:icon -->

<!-- gen:facts -->
```facts
blueprint: Church Stone
```

<!-- facts:static -->
| | |
|---|---|
| Cost | 250 wood |
| Build time | 40 s |
| Strength (structural integrity) | 500 |
| Repair cost | 275 wood |
<!-- /facts:static -->
<!-- /gen:facts -->

## Role

The church is the first Imperial building: it trains priests and lets peasants and soldiers shelter inside. Building it sets you on the Imperial path: Pagan buildings and units are no longer offered.

## What it does

- **Prayer.** On every seventh delivery a peasant rests; on the way back he visits a church of yours or an ally's within 50 m, prays, and from then on carries 12 instead of 10 ([church and maypole](../mechanics/peasants-and-work.md#church-and-maypole)). Selecting it shows the 50 m.
- **Piety.** Post one [Priest](../units/priest.md), [Bishop](../units/bishop.md) or [Inquisitor](../units/inquisitor.md) at it with [Pray](../commands/pray-at-church.md): peasants who pray there then carry 13, 14 or 15.
- **Shelter:** 20 places for your units. They don't shoot from it. Peasants run here when you ring the bell, if it is the nearest building with room.
- **Trains** priests.

## Defence

- **Strength:** 500.
- **What hurts it:** soldiers barely scratch it (0.1 a blow). Siege engines, sappers and fire do the work ([siege](../mechanics/siege.md#structural-damage)).
- **Fire:** each flaming arrow that hits it has a 7.5% chance to set it alight. Left alone, a fire grows until the building burns down, and it can leap to buildings within 25 m. Send peasants to [Extinguish Fire](../commands/extinguish.md).
- **Repair:** peasants [repair](../commands/repair.md) it, paying as they go ([strength and repair](../mechanics/buildings-and-construction.md#strength-damage-and-repair)).

## Built by

<!-- gen:trained -->
- **Built by:** [Peasant](../units/peasant.md)
<!-- /gen:trained -->

## Needs

<!-- gen:needs -->
- building a [Village](village-wood.md)
- Imperial or neutral side only: the button goes once you build a [Maypole](maypole.md) (the Pagan path).
<!-- /gen:needs -->

## Leads to

<!-- gen:leads -->
- **Trains:** [Priest](../units/priest.md)
- **Building it unlocks:** [Monastery](monastery.md)
<!-- /gen:leads -->

## Commands

<!-- gen:commands -->
- [Ungarrison](../commands/ungarrison.md)
- [Ungarrison Hand to Hand Defenders](../commands/ungarrison-hand-to-hand-units.md)
- [Priest Locator](../commands/priest-locator.md)
<!-- /gen:commands -->

## Tips

- Think before you build it: it closes the Pagan path for the rest of the game.
- Build it close to where your peasants work, so they pass it on their day of rest.
- A bishop praying there is worth two more goods per trip for every peasant who visits.
- Unlocks the Monastery.
