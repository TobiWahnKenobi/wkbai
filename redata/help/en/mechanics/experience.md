---
title: Experience
ids: [mechanics/experience, mechanic/experience, mechanic/veterans, mechanic/training-dummy]
order: 80
---

# Experience

Units get better by fighting. Each unit has an **experience** value that starts at 0 and grows to its type's maximum. The selection panel shows it as an arc of 16 segments around the unit's portrait ([selection panel](../ui/hud/selection-panel.md)).

## How it is gained

| Action | Experience gained |
|---|---|
| a hand-to-hand blow that lands | 0.008 |
| an arrow or javelin fired | 0.016 |
| a bullet fired | 0.008 |
| a shot from a siege engine, a flaming arrow, grapeshot, a rocket | 0.02 |
| a swing at a [Training Dummy](../buildings/training-dummy.md) | 0.0025 |

The **maximum** is 0.75 for the plain early troops (spearmen, archers, javelineers, scouts, mercenaries and the like), 1 for most others, and 1.15 for a few great units and heroes. Gains stop at the maximum.

## What it does

- **Hand to hand:** experience adds that share of the unit's attack to every blow. At 0.75 a blow does 75% more before armour; at 1, double.
- **Missiles:** experience makes shots land closer. An archer's scatter falls from 3 m to 2.25 m at full range when fully experienced; a mangonel's or trebuchet's halves. It doesn't raise the missile's damage.
- **In a formation** each member fights with its own experience or the formation's average, whichever is higher ([formations](formations.md)).

## Training Dummy

A [Training Dummy](../buildings/training-dummy.md) (once you have a Barracks) lets soldiers gain experience without a fight:

- **Four at a time.** A dummy has four places, two in front and two behind. A fifth unit sent there goes to the nearest dummy with a free place within 350 m, or waits.
- **Half way only.** Training stops at half the unit's maximum experience; the rest has to be earned in battle.
- Use [Train](../commands/train.md), or set a barracks' rally point on a dummy so new recruits train at once.

![The selection panel's experience arc for a veteran knight](shot:mech-experience-arc)

> **wkbai.** wkbai also counts each unit's kills and age, names it, and makes the best fighters heroes: see [unit history and heroes](unit-history.md). That doesn't change how they fight.
