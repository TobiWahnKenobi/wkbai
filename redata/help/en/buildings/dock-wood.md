---
title: Dock
ids: [building/Dock Wood, command/Build Dock Wood]
order: 10
---

# Dock

<!-- gen:icon -->
![Dock](game:interface/icons/buildings/dock.tga)
<!-- /gen:icon -->

<!-- gen:facts -->
```facts
blueprint: Dock Wood
```

<!-- facts:static -->
| | |
|---|---|
| Cost | 200 wood |
| Build time | 20 s |
| Strength (structural integrity) | 600 |
| Repair cost | 200 wood |
<!-- /facts:static -->
<!-- /gen:facts -->

## Role

The dock builds ships: cogs to carry troops and goods, and warships (arrow boats, fire boats, mangonel boats, bombard boats). It can also run supply routes over sea or land.

## What it does

- **Builds ships** (see the list below; [ships](../mechanics/naval.md)).
- **Repairs ships:** a damaged boat is mended at a dock ([Repair (Docking)](../commands/docking.md)).
- **Supply lines:** cogs carry goods between docks, carts overland ([supply routes](../mechanics/naval.md#supply-routes)).
- **Only without AI Generals:** it is offered only when no AI plays ([docks](../mechanics/naval.md#docks-only-without-ai-generals)).

## Defence

- **Strength:** 600.
- **What hurts it:** soldiers barely scratch it (0.1 a blow). Siege engines, sappers and fire do the work ([siege](../mechanics/siege.md#structural-damage)).
- **Repair:** peasants [repair](../commands/repair.md) it, paying as they go ([strength and repair](../mechanics/buildings-and-construction.md#strength-damage-and-repair)).

## Built by

<!-- gen:trained -->
- **Built by:** [Peasant](../units/peasant.md)
<!-- /gen:trained -->

## Needs

<!-- gen:needs -->
- building a [Village](village-wood.md)
<!-- /gen:needs -->

## Leads to

<!-- gen:leads -->
- **Trains:** [Cog](../units/cog.md), [Fire Boat](../units/fire-boat.md), [Corsair](../units/arrow-boat.md), [Galleass](../units/bombard-boat.md), [Carrack](../units/mangonel-boat.md)
<!-- /gen:leads -->

## Commands

<!-- gen:commands -->
- [Up Overseas Supply Line](../commands/set-up-oversea-supply-route.md)
- [Up Overland Supply Line](../commands/set-up-overland-supply-route.md)
- [Clear Supply Line](../commands/clear-supply-route.md)
<!-- /gen:commands -->

## Tips

- Only maps with water need one.
- [Up Overseas Supply Line](../commands/set-up-oversea-supply-route.md) lets cogs carry goods home from far away.
