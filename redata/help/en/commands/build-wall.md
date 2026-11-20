---
title: Build Wall
ids: [command/Build Wall]
order: 10
---

# Build Wall

<!-- gen:icon -->
![Build Wall](game:interface/icons/sic_build_civic.tga)
<!-- /gen:icon -->

## Role

Puts peasants or sappers to work on your unfinished walls. With the button, they look for the nearest wall piece or gatehouse still under construction and keep going from piece to piece until none are left nearby. (You lay out the wall itself with the wall's own build button, such as [Stockade](../buildings/stockade.md).)

## How it works

- **Target:** the nearest unfinished wall piece, corner or gatehouse within 50 m that has fewer than two builders. They check again after every piece, so they may move on to a nearer piece first.
- **Speed:** each builder adds a fixed share every second. One builder finishes a piece of [Stockade](../buildings/stockade.md) or [Palings](../buildings/palings.md) in 5 s, of [Fortified Wall](../buildings/unaligned-wall.md) or [Battlements](../buildings/technological-wall.md) in 10 s, of [Ramparts](../buildings/celestial-wall.md) in 15 s, and any gatehouse in 20 s. Two builders take half as long.
- **Limits:** at most two builders on one piece; the others pick the next piece.
- **Cost:** nothing more; the wall is paid when you place it.
- **When it ends:** when no unfinished piece is left within 50 m, the order ends and the workers stand idle.

## Offered by

<!-- gen:trained -->
- **Offered by:** [Peasant](../units/peasant.md), [Sapper](../units/sapper.md)
<!-- /gen:trained -->

## Needs

<!-- gen:needs -->
- Nothing: it is there whenever the unit or building is.
<!-- /gen:needs -->

