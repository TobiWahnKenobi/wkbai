---
title: Orders
ids: [controls/orders, controls/orders/right-click#the-right-click, controls/orders/queue#now-after-or-first, controls/orders/groups#a-group-sent-to-one-place, controls/orders/facing#facing-with-a-right-drag, controls/orders/flags#flags, controls/orders/rally#a-buildings-new-units, controls/orders/patrol#patrol, controls/orders/follow-guard#follow-and-guard, controls/orders/halt#halt, controls/orders/delete#destroying-your-own]
order: 40
---

# Orders

## The right-click

With units selected, **right-click** where you want them to act. The [cursor](../ui/hud/cursors.md) shows in advance which order you will give: move over open ground, attack over an enemy, chop over a tree, deliver over your storehouse, and so on. Each selected unit that can carry out that order gets it; the others are left alone.

When several orders fit, **E** picks the next one before you click ([More than one order](../ui/hud/cursors.md#more-than-one-order)).

Orders that need no target come from the [command buttons](../ui/hud/command-panel.md).

## Now, after or first

| Hold | The order |
|---|---|
| Nothing | Replaces everything the units were doing |
| **Shift** | Joins the end of their queue: a route of several moves, or "build this, then that" |
| **Ctrl** | Is done first; then they go back to what they were doing |

The same keys work with the command buttons and when placing buildings.

## A group sent to one place

Several units sent to one point don't all stand on it: they spread round it in rings 3 m apart. Formations keep their shape instead (see [Formations](formations.md)).

## Facing with a right-drag

![A right-drag: the flag at the destination, the white line showing the facing](shot:controls-right-drag)

Press the right button on the ground, hold it and drag the way the units should face:

- the order's flag stands at the point from the moment you press;
- after 2.5 m of dragging a short white line from the flag shows the facing;
- let go: the units go there and turn that way. A formation wheels into the facing as it marches in.

Without a drag, they face the way they walked.

## Flags

A move to the ground plants a flag where the units go (a buoy on water). It stays until they are there or stop. With Shift every point of the route gets its flag; a unit sent off with a new plain order forgets its old flags.

## A building's new units

Select a building that trains units and right-click:

- **the ground:** its new units walk there. Its rally flag shows where while it is selected;
- **a target** (a tree, a mine, an enemy): its new units go and do that.

The order's picture shows in the screen's bottom left corner while the building is selected.

## Patrol

Hold **R** and right-click: the selected units walk back and forth between where each of them stands and that point, until given another order. **Shift + R** + right-click adds one more point to the patrol they walk. The cursor shows a patrol (or waypoint) sign while R is held.

Patrols are for units and formations, not buildings. A saved game forgets the patrol points: loaded, the units just finish their present walk.

## Follow and guard

- **Hold F** and right-click a unit: the selection follows it.
- **Hold G** and right-click a unit or building: the selection guards it.

They are the game set's Follow and Guard orders, given where the target allows them.

## Halt

**H** stops the selection at once, as its Halt button does. A formation halted forms up again where it stands.

## Destroying your own

**Delete** destroys everything selected, at once, without asking. Use it to clear a misplaced foundation, or with care.

> wkbai: each unit plays its own answer to an attack or other order, so a big group can sound like a chorus.
