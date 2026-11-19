---
title: Training Dummy
ids: [building/Training Dummy, command/Build Training Dummy]
order: 10
---

# Training Dummy

<!-- gen:icon -->
![Training Dummy](game:interface/icons/buildings/TrainingDummy.tga)
<!-- /gen:icon -->

<!-- gen:facts -->
```facts
blueprint: Training Dummy
```

<!-- facts:static -->
| | |
|---|---|
| Cost | 50 wood |
| Build time | 20 s |
| Strength (structural integrity) | 50 |
| Repair cost | 50 wood |
<!-- /facts:static -->
<!-- /gen:facts -->

## Role

The training dummy lets soldiers practise without a fight: units sent to [Train](../commands/train.md) at it swing at it and slowly gain [experience](../mechanics/experience.md). It needs a Barracks and costs only 50 wood.

## What it does

- **Four at a time.** A dummy has four places, two in front and two behind. A fifth unit sent there walks on to the nearest dummy with a free place within 350 m, yours or an ally's; with none free, it waits near the one you clicked.
- **How much.** Every swing adds 0.0025 experience. Training stops at **half** the unit's maximum: 0.375 for the plain early troops (150 swings), 0.5 for most others (200 swings). The unit then stops by itself with a "training complete" call.
- **Who.** Most soldiers, foot and horse: archers, spearmen, pikemen, knights, horsemen, gunners, scouts, mercenaries, the Pagan troops. Peasants, militia, holy men and siege engines can't train.
- **The Train button** appears on your soldiers once you own a dummy. A formation sent to train breaks up and its members train one by one.
- **Straight from training.** Put a building's rally point on a dummy: new recruits start training as soon as they come out.
- **What it is worth.** Experience adds its share to every hand-to-hand blow (at 0.5, half as much again) and makes shots land closer ([experience](../mechanics/experience.md#what-it-does)).

## Defence

- **Strength:** 50.
- **What hurts it:** soldiers barely scratch it (0.1 a blow). Siege engines, sappers and fire do the work ([siege](../mechanics/siege.md#structural-damage)).
- **Fire:** each flaming arrow that hits it has a 15% chance to set it alight. Left alone, a fire grows until the building burns down, and it can leap to buildings within 25 m. Send peasants to [Extinguish Fire](../commands/extinguish.md).
- **Repair:** peasants [repair](../commands/repair.md) it, paying as they go ([strength and repair](../mechanics/buildings-and-construction.md#strength-damage-and-repair)).

## Built by

<!-- gen:trained -->
- **Built by:** [Peasant](../units/peasant.md), [Sapper](../units/sapper.md)
<!-- /gen:trained -->

## Needs

<!-- gen:needs -->
- building a [Barracks](barracks-stone.md)
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

- Build two or three next to the Barracks and set the Barracks' rally point on one of them.
- Train cavalry too: knights and horsemen gain as much as foot soldiers.
- The dummy only takes a unit half way; the rest of its experience must come from battle.
- An ally's dummy works for your troops as well.
