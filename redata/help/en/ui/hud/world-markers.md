---
title: On the battlefield
ids: [ui/hud/world, ui/hud/world/highlight#selected-and-pointed-at, ui/hud/world/health-ring#health-ring, ui/hud/world/charge-bar#charge-bar, ui/hud/world/building-health#building-health-bars, ui/hud/world/hero-names#hero-names, ui/hud/world/hover-name#the-name-under-the-mouse, ui/hud/world/range#range-circles, ui/hud/world/order-flags#order-flags, ui/hud/world/rally-flag#rally-flags, ui/hud/world/drag-line#facing-line, ui/hud/world/selection-box#selection-box]
order: 120
---

# On the battlefield

Besides the panels, the interface draws some marks into the world itself.

![Selected spearmen with their health rings, a waypoint flag and a tower's range circle](shot:hud-world-markers)

## Selected and pointed at

Selected units and buildings, and the one under the mouse, are drawn **brighter**, without shading. Pointing at one member of a formation lights the whole formation.

### Health ring

Under each selected or pointed-at unit, a ring on the ground shows its health in eight steps, full to nearly gone.

### Charge bar

Beside the health ring, a second arc shows a unit's charge, where it has one: what a peasant or cart carries, an archer's arrows. It is shown while it is above nothing.

## Building health bars

Over a selected building, and over the one under the mouse, a small bar shows its structural integrity: green when whole, through yellow, to red. Switch them off with [Building health bars](help:option/buildingHealthBars).

## Hero names

Heroes carry their name over their heads, in their player's colour, fading out between 120 and 220 metres from the camera. Option: [Show hero names](help:option/showHeroNames).

## The name under the mouse

Point at a unit that has a name and a tooltip gives its name, its type and its kills: "Guillaume le Hardi · Knight · 9 kills". Not for units hidden in the fog of war. Option: [Show unit names on hover](help:option/showUnitNamesOnHover).

## Range circles

Some selected things draw a ring of arrows, dots or links on the ground round them, a reach or an area that matters for them; the game set decides which and how far. Some also ring other objects they work with. A building being placed shows its own (see [Building placement](../../controls/building-placement.md)).

## Order flags

A move to the ground plants a **flag** at the destination (a **buoy** on water). The flag stays until the units get there or stop. Orders queued with **Shift** each get their flag, so you can see the whole route. See [Orders](../../controls/orders.md#flags).

## Rally flags

A selected building that sends its new units somewhere shows its **rally flag** there, in your colour.

## Facing line

While you hold the right button after a move order and drag, a short **white line** from the flag points the way the units will face. See [Orders](../../controls/orders.md#facing-with-a-right-drag).

## Selection box

Dragging with the left button draws a white **box**; what is inside it when you let go is selected. See [Selection](../../controls/selection.md#box-selection).
