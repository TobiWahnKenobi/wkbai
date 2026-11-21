---
title: Extinguish Fire
ids: [command/Extinguish]
order: 10
---

# Extinguish Fire

<!-- gen:icon -->
![Extinguish Fire](game:interface/icons/sic_Extinguish.tga)
<!-- /gen:icon -->

## Role

Sends [peasants](../units/peasant.md) to put out a burning building. Right-click a building on fire; the cursor shows the extinguish cursor.

## How it works

- **Target:** a burning building of yours or an ally's.
- **The fire:** it starts small and burns away 3 strength about every 3 seconds. Each time it may grow by 1.5, up to 27; the better the building's fire proofing, the less often it grows ([fire](../mechanics/buildings-and-construction.md#fire)).
- **Effect:** each throw of water by a peasant takes 1 off the fire's strength. When it reaches nothing, the fire is out.
- **Several peasants:** their throws add up, so a big fire needs many.
- **Afterwards:** every peasant who fought that fire goes on to the nearest burning building of yours or an ally's within 70 m that has fewer than two peasants at it. With no fire left, they start [repairing](repair.md) the nearest damaged building of yours within 50 m.
- **Limits:** a unit under [Possess](possess.md) can't take the order.

## Offered by

<!-- gen:trained -->
- **Offered by:** [Peasant](../units/peasant.md)
<!-- /gen:trained -->

## Needs

<!-- gen:needs -->
- Nothing: it is there whenever the unit or building is.
<!-- /gen:needs -->

## Tips

- Act early: a young fire needs only a few throws, a full one 27.
