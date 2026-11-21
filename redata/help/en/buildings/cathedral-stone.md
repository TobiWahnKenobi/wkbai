---
title: Cathedral
ids: [building/Cathedral Stone, command/Build Cathedral Stone]
order: 10
---

# Cathedral

<!-- gen:icon -->
![Cathedral](game:interface/icons/buildings/cathedral.tga)
<!-- /gen:icon -->

<!-- gen:facts -->
```facts
blueprint: Cathedral Stone
```

<!-- facts:static -->
| | |
|---|---|
| Cost | 600 wood, 150 gold |
| Build time | 200 s |
| Strength (structural integrity) | 1000 |
| Repair cost | 600 wood, 150 gold |
<!-- /facts:static -->
<!-- /gen:facts -->

## Role

The cathedral trains bishops and inquisitors and researches [Shield of God](../upgrades/masonry.md), [Surgery](../upgrades/surgery.md), the [Crusade](../upgrades/crusade.md) and the [Crossbow](../upgrades/crossbow.md). It turns your Keep into the Imperial Castle.

## What it does

- **Piety.** After the Crusade every Cathedral gathers piety: 1.4 every 1.9 seconds (about 44 a minute), up to 400 each; 1200 each once you build the [Statue of the Archangel](michael-statue.md).
- **Acts of God.** [Bishops](../units/bishop.md) spend the piety of all your cathedrals together: [Inflict Vision](../commands/inflict-vision.md) 100, [Inflict Plague](../commands/inflict-plague.md) 300, [Inflict Blindness](../commands/inflict-blindness.md) 400, [Inflict Lightning](../commands/inflict-lightning.md) 700, [Inflict Earthquake](../commands/inflict-earthquake.md) 900.
- **Imperial Castle:** finishing it turns your Keep into the Imperial Castle.

## Defence

- **Strength:** 1000.
- **What hurts it:** soldiers barely scratch it (0.1 a blow). Siege engines, sappers and fire do the work ([siege](../mechanics/siege.md#structural-damage)).
- **Fire:** fireproof. Flaming arrows don't set it alight; a spy's arson, Greek fire and lightning still can.
- **Repair:** peasants [repair](../commands/repair.md) it, paying as they go ([strength and repair](../mechanics/buildings-and-construction.md#strength-damage-and-repair)).

## Built by

<!-- gen:trained -->
- **Built by:** [Peasant](../units/peasant.md)
<!-- /gen:trained -->

## Needs

<!-- gen:needs -->
- building a [Monastery](monastery.md)
- Imperial or neutral side only: the button goes once you build a [Maypole](maypole.md) (the Pagan path).
<!-- /gen:needs -->

## Leads to

<!-- gen:leads -->
- **Trains:** [Bishop](../units/bishop.md), [Inquisitor](../units/inquisitor.md)
- **Research here:** [Shield of God](../upgrades/masonry.md), [Surgery](../upgrades/surgery.md), [Crusade](../upgrades/crusade.md), [Crossbow](../upgrades/crossbow.md)
- **Building it unlocks:** [Bishop](../units/bishop.md), [Galleass](../units/bombard-boat.md), [Scriptorium](scriptorium.md), [University](university.md), [Ramparts Upgrade](../upgrades/technological-wall-to-celestial-wall.md), [Holy Chapter Upgrade](../upgrades/upgrade-holy-chapter.md)
<!-- /gen:leads -->

## Commands

<!-- gen:commands -->
- [Bishop Locator](../commands/bishop-locator.md)
<!-- /gen:commands -->

## Tips

- The Crusade opens the Statue of the Archangel and the first acts of God.
- One Cathedral holds 400 piety: build more to save up for the great acts.
- The [Crossbow](../upgrades/crossbow.md) makes everyone in your forts and towers shoot.
