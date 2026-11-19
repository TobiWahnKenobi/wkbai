---
title: Bank
ids: [building/Bank, command/Build Bank]
order: 10
---

# Bank

<!-- gen:icon -->
![Bank](game:interface/icons/buildings/Bank.tga)
<!-- /gen:icon -->

<!-- gen:facts -->
```facts
blueprint: Bank
```

<!-- facts:static -->
| | |
|---|---|
| Cost | 600 wood |
| Build time | 60 s |
| Strength (structural integrity) | 300 |
| Repair cost | 600 wood |
<!-- /facts:static -->
<!-- /gen:facts -->

## Role

The bank is a late economy building of the Renaissance side. It costs 600 wood, needs an Observatory and is not offered once you take a pure path (Statue of the Archangel or Wickerman).

## What it does

- **More gold from shops.** A shopkeeper who passes it within 60 m gets 1.5 times the gold for his trade ([gold](../mechanics/resources.md#gold)). Selecting it shows the 60 m.
- It does nothing on its own: it needs shops around it.

## Defence

- **Strength:** 300.
- **What hurts it:** soldiers barely scratch it (0.1 a blow). Siege engines, sappers and fire do the work ([siege](../mechanics/siege.md#structural-damage)).
- **Fire:** each flaming arrow that hits it has a 7.5% chance to set it alight. Left alone, a fire grows until the building burns down, and it can leap to buildings within 25 m. Send peasants to [Extinguish Fire](../commands/extinguish.md).
- **Repair:** peasants [repair](../commands/repair.md) it, paying as they go ([strength and repair](../mechanics/buildings-and-construction.md#strength-damage-and-repair)).

## Built by

<!-- gen:trained -->
- **Built by:** [Peasant](../units/peasant.md)
<!-- /gen:trained -->

## Needs

<!-- gen:needs -->
- building a [Observatory](observatory.md)
- Not on the pure paths: the button goes once you build a [Statue of the Archangel](michael-statue.md) or a [The Wickerman](wickerman.md).
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

- It is weak (strength 300): build it inside your walls.
- Put it in the middle of a ring of shops.
