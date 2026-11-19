---
title: Stockade Gatehouse
ids: [building/Stockade Gatehouse, building/Stockade Gatehouse Closed, command/Build Stockade Gatehouse Closed]
order: 10
---

# Stockade Gatehouse

<!-- gen:icon -->
![Stockade Gatehouse](game:interface/icons/buildings/Stockade_gatehouse.tga)
<!-- /gen:icon -->

<!-- gen:facts -->
```facts
blueprint: Stockade Gatehouse Closed
```

<!-- facts:static -->
| | |
|---|---|
| Cost | 600 wood |
| Build time | 20 s |
| Strength (structural integrity) | 50 |
<!-- /facts:static -->
<!-- /gen:facts -->

## Role

The stockade gatehouse is the gate in a wooden stockade. Your units pass when it is open; close it to keep the enemy out.

## What it does

- **A door in the wall.** Open ([Open (Stockade Gatehouse)](../commands/open-stockade-gatehouse.md)) your troops walk through; closed ([Close (Stockade Gatehouse)](../commands/close-stockade-gatehouse.md)) it blocks the way like the wall.
- **It doesn't shoot** and holds no garrison. Raise the wall sections next to it into towers to cover it.

## Defence

- **Strength:** 50. Weaker than the fence around it: it is the first thing attackers go for.
- **What hurts it:** soldiers barely scratch it (0.1 a blow). Siege engines, sappers and fire do the work ([siege](../mechanics/siege.md#structural-damage)).
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
- [Open (Stockade Gatehouse)](../commands/open-stockade-gatehouse.md)
- [Close (Stockade Gatehouse)](../commands/close-stockade-gatehouse.md)
<!-- /gen:commands -->

## Tips

- [Open (Stockade Gatehouse)](../commands/open-stockade-gatehouse.md) and [Close (Stockade Gatehouse)](../commands/close-stockade-gatehouse.md) switch it.
