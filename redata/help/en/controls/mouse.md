---
title: Mouse
ids: [controls/mouse, controls/mouse/left#left-button, controls/mouse/right#right-button, controls/mouse/middle#middle-button-and-wheel, controls/mouse/side#side-button, controls/mouse/modifiers#shift-and-ctrl]
order: 10
---

# Mouse

## Left button

| On | Click | Does |
|---|---|---|
| A unit or building of yours | Click | Selects it (the selection is replaced) |
| | Shift + click | Adds it to the selection |
| | Double-click | Selects every one of that kind within 25 m (for some soldiers the other way round: see [All of a kind](selection.md#all-of-a-kind)) |
| Empty ground | Click | Empties the selection |
| | Drag | Draws a box; what is in it is selected when you let go |
| A button, the minimap, a window | Click | Uses it; nothing on the battlefield underneath is touched |
| While placing a building | Click | Stops placing it |

Details in [Selecting](selection.md).

## Right button

| On | Click | Does |
|---|---|---|
| Anything, with a selection | Click | Gives the order the [cursor](../ui/hud/cursors.md) shows |
| The ground | Click and drag | A move, facing the way you dragged |
| The ground, a building selected | Click | Sets where its new units go |
| While placing a building | Click | Places it |
| A command button | Click | Cancels that order, or takes one off a training queue |
| A tributes cell | Click | 10 less |

Details in [Orders](orders.md).

## Shift and Ctrl

With orders, the modifiers change **when** the order is carried out:

- **no modifier:** now, dropping everything else;
- **Shift:** after the present orders (a queue);
- **Ctrl:** first, then back to the present orders.

With selection, **Shift** adds. With training, **Shift** makes it ten at a time.

## Middle button and wheel

- **Hold the middle button** and move the mouse: turn the camera (left and right; also up and down with the free camera).
- **Wheel:** raise or lower the camera; with the fixed camera, zoom in and out. **Invert mouse wheel** ([option](help:option/invertMouseWheel)) swaps the direction.

## Side button

Hold the mouse's first side button (the "back" button) and move the mouse up or down: the camera rises or sinks.

## At the window's edge

With **Scroll at the window's edge** ([option](help:option/edgeScroll), off at first), moving the mouse to the edge of the window scrolls the camera that way.

See [Camera](camera.md) for the camera's limits and speeds.

> wkbai: a click that moves slightly while pressed already counts as a small box. The original made a box only beyond 25 pixels; usually the small box holds what you clicked anyway.
