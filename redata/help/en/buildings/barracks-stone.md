---
title: Barracks
ids: [building/Barracks Stone, command/Build Barracks Stone]
order: 10
---

# Barracks

<!-- gen:icon -->
![Barracks](game:interface/icons/buildings/barracksstone.tga)
<!-- /gen:icon -->

<!-- gen:facts -->
```facts
blueprint: Barracks Stone
```

<!-- facts:static -->
| | |
|---|---|
| Cost | 200 wood |
| Build time | 50 s |
| Strength (structural integrity) | 600 |
| Repair cost | 200 wood |
<!-- /facts:static -->
<!-- /gen:facts -->

## Role

The barracks trains foot soldiers: archers and spearmen from the start, then pikemen, men at arms, Imperial archers and gunners as you research them. It also unlocks the Armoury, the Training Dummy, the Watch Tower and the Fort.

## What it does

- **Trains** foot soldiers, one after another from its queue (see the list below).
- **Resupply:** your and your allies' archers, gunners, siege engines and other troops that use ammunition get 10 back every 5 seconds or so while within 25 m of it; 50 m after [Logistics](../upgrades/logistics.md).
- **Rally point:** set it on a [Training Dummy](training-dummy.md) and new recruits start training at once.

## Defence

- **Strength:** 600.
- **What hurts it:** soldiers barely scratch it (0.1 a blow). Siege engines, sappers and fire do the work ([siege](../mechanics/siege.md#structural-damage)).
- **Fire:** each flaming arrow that hits it has a 2.5% chance to set it alight. Left alone, a fire grows until the building burns down, and it can leap to buildings within 25 m. Send peasants to [Extinguish Fire](../commands/extinguish.md).
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
- **Trains:** [Archer](../units/archer.md), [Spearman](../units/spearman.md), [Man-at-Arms](../units/pikeman.md), [Imperial Archer](../units/celestial-archer.md), [Imperial Pikeman](../units/man-at-arms.md), [Gunner](../units/gunner.md), [Gunner (with Bayonet)](../units/gunner-bayonet.md)
- **Building it unlocks:** [Armoury](armoury-stone.md), [Fort](fort-wood.md), [Training Dummy](training-dummy.md), [Watch Tower](watch-tower.md)
<!-- /gen:leads -->

## Commands

<!-- gen:commands -->
- None of its own.
<!-- /gen:commands -->

## Tips

- Build it early; a few archers and spearmen deter the first raids.
- Put it where your archers fight from: it refills their arrows.
- Two or three Training Dummies next to it give every recruit some experience.
