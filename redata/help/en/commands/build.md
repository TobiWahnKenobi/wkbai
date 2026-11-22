---
title: Build
ids: [command/Build]
order: 10
---

# Build

<!-- gen:icon -->
![Build](game:interface/icons/sic_build_civic.tga)
<!-- /gen:icon -->

## Role

Build is the work of raising a building from its foundation. In wkbai you don't press a button called Build: the builder's build menu shows one button per building it can put up. Pick one, place it ([placing buildings](../controls/building-placement.md)), and the selected builders walk over and start work.

## How it works

- **Who builds what:** [Peasants](../units/peasant.md) raise your town's buildings. The [Sapper](../units/sapper.md) is the only one who may build siege engine sites and [Explosive Mines](../buildings/explosive-mine.md); the [High Priestess](../units/high-priestess.md) is the only one who may raise the [Henge](../buildings/henge.md).
- **Reach:** a builder works from up to 6.5 m from the site.
- **Progress:** every stroke of the builder's work animation adds the same share of the building's strength. The share is set by the building's construction time, so big buildings need many more strokes than small ones (a [Cathedral](../buildings/cathedral-stone.md) needs ten times the work of a [Dock](../buildings/dock-wood.md)).
- **Several builders:** their strokes simply add up. Two builders finish twice as fast, three three times; there is no cap.
- **Cost:** the price is paid when you place the foundation. Building it costs nothing more.
- **Damaged during building:** if the site was hit while it was going up, it is still short of full strength when the work is done. It then counts as a damaged building and the builders switch to [Repair](repair.md), which costs resources.
- **When it is finished:** the frame turns into the finished building and every builder stops. Each then goes on to the nearest unfinished foundation within 50 m; wall builders go on to the next wall piece ([Build Wall](build-wall.md)).
- **Limits:** a unit under [Possess](possess.md) can't build.

> **wkbai.** The game set has a plain "Build" button too; wkbai's command panel leaves it out and shows only the "Build <building>" buttons.

## Offered by

<!-- gen:trained -->
- **Offered by:** [High Priestess](../units/high-priestess.md), [Observatory](../buildings/observatory.md), [Peasant](../units/peasant.md), [Sapper](../units/sapper.md)
<!-- /gen:trained -->

## Needs

<!-- gen:needs -->
- Nothing: it is there whenever the unit or building is.
<!-- /gen:needs -->

