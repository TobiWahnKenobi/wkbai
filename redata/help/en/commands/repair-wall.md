---
title: Repair Wall
ids: [command/Repair Wall]
order: 10
---

# Repair Wall

## Role

The [Sapper](../units/sapper.md)'s way of mending damaged walls and gatehouses. Right-click a damaged wall piece or gatehouse; the cursor shows the repair cursor.

## How it works

- **Target:** a finished, damaged wall piece, corner or gatehouse that isn't burning.
- **Speed:** each sapper adds 3 strength a second. A [Stockade](../buildings/stockade.md) piece (100 strength) takes a lone sapper about 33 seconds from nothing.
- **Cost:** like [Repair](repair.md): a full repair costs half of the piece's repair price, paid step by step from your stock (a Stockade piece: 15 materials).
- **Short of wood:** the sapper then waits, checks every 5 seconds, and goes back to work once you have more than 50 materials.
- **Afterwards:** he goes on to the nearest damaged wall piece or gatehouse within 100 m that has fewer than two workers, and stops when none is left.

## Offered by

<!-- gen:trained -->
- **Offered by:** [Sapper](../units/sapper.md)
- No button of its own: you give it with a right-click on the target (see [Orders](../controls/orders.md#the-right-click)).
<!-- /gen:trained -->

## Needs

<!-- gen:needs -->
- Nothing: it is there whenever the unit or building is.
<!-- /gen:needs -->

