---
title: Formations
ids: [controls/formations, controls/formations/make#making-a-formation, controls/formations/shapes#the-four-shapes, controls/formations/select#selecting-a-formation, controls/formations/move#moving, controls/formations/fight#fighting, controls/formations/break#breaking-up]
order: 50
---

# Formations

![Twelve spearmen in a Line, wheeling to the dragged facing](shot:controls-formation-line)

A formation is a block of soldiers that moves and turns as one. See [Formations](help:mechanic/formations) for what they are good for.

## Making a formation

1. Select **8 or more** soldiers.
2. Click their **Formation** button.

They take their places, facing the way most of them faced. With fewer than 8, nothing happens.

## The four shapes

With a formation selected, the [formation pad](../ui/hud/command-panel.md#formation-buttons) in the selection panel's window changes its shape: Column top left, Line top right, Wedge bottom left, Orb bottom right.

| Shape | Layout |
|---|---|
| **Line** | Wide and shallow: at least two rows, about a dozen side by side. |
| **Column** | Narrow and deep, for marching. |
| **Wedge** | One in front, then two, three, ...: a point leading. |
| **Orb** | Rings round a hollow middle, everyone facing outwards. |

## Selecting a formation

- Pointing at one member lights the whole formation; clicking selects the formation. A click on a single soldier near a formation selects that soldier's kind around it, but never the members of a formation.
- **Right-click inside your own selected formation** is a move to that point, not an order on its member. An enemy under the cursor is still attacked.
- A box over its members selects the formation, not them.
- **Space** goes to its centre.
- Over the panel, a line gives its kill tally and its heroes.

## Moving

- **Right-click** the ground: the front row's middle ends on the point.
- **Right-drag**: it also ends facing the way you dragged, wheeling round as it marches in ([Facing with a right-drag](orders.md#facing-with-a-right-drag)).
- A formation marches at three quarters of its slowest member's pace. Rear rows follow where the front went, so it turns corners as a block.
- Setting off in a very different direction, it first forms up facing the way, then marches.

## Fighting

A member that gets into a fight leaves its place; the others close up round it. A plain move is stopped when the fighting starts. When nobody is out of place any more, everyone goes back to their places.

## Breaking up

- **Halt (H)** on a formation: it forms up again where it stands.
- A formation left with **fewer than 8** members breaks up.
- Its **Disband** button breaks it up at once.
- **Formation** with a formation and loose units selected: the formation's members join the new one.
- **Training:** a formation or its members can't be sent to train at a [Training Dummy](../buildings/training-dummy.md). A soldier who is training stops when it joins a formation.

> wkbai: the formations' places are the original's; the march along one shared track and the wheeling are wkbai's close copy of the original's behaviour.
