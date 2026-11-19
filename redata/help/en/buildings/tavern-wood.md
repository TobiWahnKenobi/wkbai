---
title: Tavern
ids: [building/Tavern Wood, command/Build Tavern Wood]
order: 10
---

# Tavern

<!-- gen:icon -->
![Tavern](game:interface/icons/buildings/tavern.tga)
<!-- /gen:icon -->

<!-- gen:facts -->
```facts
blueprint: Tavern Wood
```

<!-- facts:static -->
| | |
|---|---|
| Cost | 300 wood |
| Build time | 20 s |
| Strength (structural integrity) | 200 |
| Repair cost | 300 wood |
<!-- /facts:static -->
<!-- /gen:facts -->

## Role

The tavern hires mercenaries (gold only, no food upkeep) and spies. It needs a Shop.

## What it does

- **Hires** mercenaries and spies (see the list below). Mercenaries eat no food.
- **Diplomacy:** while you own a Tavern, the Global Diplomacy window is open to you: everyone's relations with everyone ([diplomacy](../mechanics/diplomacy.md#the-windows)).

## Defence

- **Strength:** 200.
- **What hurts it:** soldiers barely scratch it (0.1 a blow). Siege engines, sappers and fire do the work ([siege](../mechanics/siege.md#structural-damage)).
- **Fire:** each flaming arrow that hits it has a 15% chance to set it alight. Left alone, a fire grows until the building burns down, and it can leap to buildings within 25 m. Send peasants to [Extinguish Fire](../commands/extinguish.md).
- **Repair:** peasants [repair](../commands/repair.md) it, paying as they go ([strength and repair](../mechanics/buildings-and-construction.md#strength-damage-and-repair)).

## Built by

<!-- gen:trained -->
- **Built by:** [Peasant](../units/peasant.md)
<!-- /gen:trained -->

## Needs

<!-- gen:needs -->
- building a [Shop](shop-stone.md)
<!-- /gen:needs -->

## Leads to

<!-- gen:leads -->
- **Trains:** [Mercenary](../units/mercenary.md), [Spy](../units/spy.md)
<!-- /gen:leads -->

## Commands

<!-- gen:commands -->
- [Spy Locator](../commands/spy-locator.md)
<!-- /gen:commands -->

## Tips

- Hire mercenaries when your farms cannot feed more soldiers.
