---
title: Repair
ids: [command/Repair]
order: 10
---

# Repair

<!-- gen:icon -->
![Repair](game:interface/icons/sic_repair.tga)
<!-- /gen:icon -->

## Role

Peasants mend a damaged building and pay for the repair from your stock. Select them and right-click a damaged building; the cursor shows the repair cursor. [King Artos](../units/hero-technological.md) can repair too.

## How it works

- **Target:** a finished, damaged building of yours or an ally's that isn't burning (walls and gatehouses too). Units and unfinished foundations can't be repaired this way.
- **Reach:** the repairer works from up to 6.5 m away.
- **Speed:** each repairer adds 3 strength a second. Manors, Keeps, Castles and Palaces take 2 a second, the [Fort](../buildings/fort-wood.md) and the [Holy Fortress](../buildings/fort-stone.md) 1 a second. Several repairers add up.
- **Cost:** a full repair from nothing costs half of the building's repair price, and every second's step costs its share. The repair price is in wood (materials); a few buildings also ask food. The gold part of a repair price is not charged. Example: a [Village](../buildings/village-wood.md) has a repair price of 300, so mending it from rubble to whole takes about 150 materials.
- **Out of resources:** a repairer who can't pay the next step stops and checks again every 3 seconds.
- **Fire:** if the building is burning, the repairer drops the repair and goes to [put the fire out](extinguish.md).
- **Afterwards:** when the building is whole, the repairer goes on to the nearest damaged building of yours within 50 m that has no more than two others repairing it. When none is left, he stops.
- **Limits:** a unit under [Possess](possess.md) can't repair.

## Offered by

<!-- gen:trained -->
- **Offered by:** [King Artos](../units/hero-technological.md), [Peasant](../units/peasant.md)
<!-- /gen:trained -->

## Needs

<!-- gen:needs -->
- Nothing: it is there whenever the unit or building is.
<!-- /gen:needs -->

## Tips

- Three repairers on one building mend it three times as fast for the same total price, so send several to an important building under attack.
