---
title: Fortified Gatehouse
ids: [building/Unaligned Gatehouse Closed, building/Unaligned Gatehouse Open, command/Build Unaligned Gatehouse Closed]
order: 10
---

# Fortified Gatehouse

<!-- gen:facts -->
```facts
blueprint: Unaligned Gatehouse Closed
```

<!-- facts:static -->
| | |
|---|---|
| Cost | 600 wood |
| Build time | 20 s |
| Strength (structural integrity) | 400 |
| Sight | 19 |
<!-- /facts:static -->
<!-- /gen:facts -->

## Role

The Fortified Gatehouse is the gate in a Fortified Wall. It opens for your own units and blocks the way when closed.

## What it does

- **A door in the wall.** Open ([Open](../commands/open-unaligned-gatehouse.md)) your troops walk through; closed ([Close](../commands/close-unaligned-gatehouse.md)) it blocks the way like the wall.
- **It doesn't shoot** and holds no garrison. Raise the wall sections next to it into towers to cover it.

## Defence

- **Strength:** 400. Weaker than the wall (500).
- **What hurts it:** soldiers barely scratch it (0.1 a blow). Siege engines, sappers and fire do the work ([siege](../mechanics/siege.md#structural-damage)).
- **Repair:** peasants [repair](../commands/repair.md) it, paying as they go ([strength and repair](../mechanics/buildings-and-construction.md#strength-damage-and-repair)).

## Built by

<!-- gen:trained -->
- **Built by:** [Peasant](../units/peasant.md)
- **Comes from:** [Fortified Wall Upgrade](../upgrades/stockade-to-unaligned-wall.md)
<!-- /gen:trained -->

## Needs

<!-- gen:needs -->
- [Fortified Wall Upgrade](../upgrades/stockade-to-unaligned-wall.md)
<!-- /gen:needs -->

## Leads to

<!-- gen:leads -->
- Nothing new: it trains no units and offers no research.
<!-- /gen:leads -->

## Commands

<!-- gen:commands -->
- [Open (Fortified Gatehouse)](../commands/open-unaligned-gatehouse.md)
- [Close (Fortified Gatehouse)](../commands/close-unaligned-gatehouse.md)
<!-- /gen:commands -->

## Tips

- Keep it closed when the enemy is near.
