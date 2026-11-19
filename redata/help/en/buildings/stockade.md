---
title: Stockade
ids: [building/Stockade, command/Build Stockade]
order: 10
---

# Stockade

<!-- gen:icon -->
![Stockade](game:interface/icons/buildings/Stockade.tga)
<!-- /gen:icon -->

<!-- gen:facts -->
```facts
blueprint: Stockade
```

<!-- facts:static -->
| | |
|---|---|
| Cost | 30 wood |
| Build time | 5 s |
| Strength (structural integrity) | 100 |
| Repair cost | 30 wood |
<!-- /facts:static -->
<!-- /gen:facts -->

## Role

The stockade is the first wall: a wooden fence, cheap (30 wood) and quick to put up, but weak (strength 100). After [Stockade to Fortified Wall](../upgrades/stockade-to-unaligned-wall.md) you build stone walls instead.

## What it does

- **Grows with the city.** Each new city building pushes the palisade out around it ([placing a building](../mechanics/buildings-and-construction.md#placing-a-building)).
- **Blocks the way:** enemies must break through or come in by a gate.
- **Turns to stone** after [Stockade to Fortified Wall](../upgrades/stockade-to-unaligned-wall.md).

## Defence

- **Strength:** 100.
- **What hurts it:** soldiers barely scratch it (0.1 a blow). Siege engines, sappers and fire do the work ([siege](../mechanics/siege.md#structural-damage)).
- **Fire:** each flaming arrow that hits it has a 30% chance to set it alight. Left alone, a fire grows until the building burns down, and it can leap to buildings within 25 m. Send peasants to [Extinguish Fire](../commands/extinguish.md).
- **Repair:** peasants [repair](../commands/repair.md) it, paying as they go ([strength and repair](../mechanics/buildings-and-construction.md#strength-damage-and-repair)).

## Built by

<!-- gen:trained -->
- **Built by:** [Peasant](../units/peasant.md)
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
- None of its own.
<!-- /gen:commands -->

## Tips

- Put a [gatehouse](stockade-gatehouse-closed.md) in the line so your own units can get through.
- It burns easily: research the stone wall before the enemy has flaming arrows.
