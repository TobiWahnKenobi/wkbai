---
title: Fortified Wall
ids: [building/Unaligned Wall, building/Unaligned Wall Corner In, building/Unaligned Wall Corner Out, building/Unaligned Wall Cross Piece, command/Build Unaligned Wall]
order: 10
---

# Fortified Wall

<!-- gen:facts -->
```facts
blueprint: Unaligned Wall
```

<!-- facts:static -->
| | |
|---|---|
| Cost | 30 wood |
| Build time | 10 s |
| Strength (structural integrity) | 500 |
| Repair cost | 25 wood |
<!-- /facts:static -->
<!-- /gen:facts -->

## Role

The stone wall (unaligned wall) replaces the stockade after [Stockade to Fortified Wall](../upgrades/stockade-to-unaligned-wall.md): strength 500 per section, and existing stockades turn to stone. Sections can be raised into towers.

## What it does

- **Blocks the way:** enemies must break through or come in by a gate.
- **Towers:** select a section and use [Upgrade to Fortified Tower](../upgrades/upgrade-to-unaligned-tower.md) to raise it into a tower for five archers.
- **Next:** [Fortified Wall to Battlements](../upgrades/unaligned-wall-to-technological-wall.md) rebuilds it stronger.

## Defence

- **Strength:** 500.
- **What hurts it:** soldiers barely scratch it (0.1 a blow). Siege engines, sappers and fire do the work ([siege](../mechanics/siege.md#structural-damage)).
- **Repair:** peasants [repair](../commands/repair.md) it, paying as they go ([strength and repair](../mechanics/buildings-and-construction.md#strength-damage-and-repair)).

## Built by

<!-- gen:trained -->
- **Built by:** [Peasant](../units/peasant.md)
- **Comes from:** [Fortified Wall Upgrade](../upgrades/stockade-to-unaligned-wall.md)
<!-- /gen:trained -->

## Needs

<!-- gen:needs -->
- [Fortified Wall Upgrade](../upgrades/stockade-to-unaligned-wall.md) or [Battlements Upgrade](../upgrades/unaligned-wall-to-technological-wall.md)
<!-- /gen:needs -->

## Leads to

<!-- gen:leads -->
- **Research here:** [Upgrade Wall to Fortified Tower](../upgrades/upgrade-to-unaligned-tower.md)
<!-- /gen:leads -->

## Commands

<!-- gen:commands -->
- None of its own.
<!-- /gen:commands -->

## Tips

- Use [Upgrade to Fortified Tower](../upgrades/upgrade-to-unaligned-tower.md) on wall sections at corners and gates.
