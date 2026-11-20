---
title: Camera
ids: [controls/camera, controls/camera/scroll#scrolling, controls/camera/turn#turning, controls/camera/height#height-and-zoom, controls/camera/fixed#the-fixed-camera, controls/camera/jump#jumping, controls/camera/limits#limits]
order: 20
---

# Camera

## Scrolling

- **Arrow keys** or **W A S D:** move the camera forwards, back, left and right, along the ground the way it looks.
- **Shift** held: three times as fast (option [Scroll speed held faster](help:option/shiftScrollFactor)).
- **The window's edge:** with [Scroll at the window's edge](help:option/edgeScroll) on, the mouse at the edge scrolls too.
- **Scroll speed** ([option](help:option/scrollSpeed)) sets the speed of both.

## Turning

Hold the **middle mouse button** (or **Keypad 0**) and move the mouse:

- left and right turns the camera round;
- up and down tilts it, from straight down to straight up (not with the fixed camera).

Its speed is **Camera rotation speed** ([option](help:option/cameraRotationSpeed)).

## Height and zoom

- **Mouse wheel:** up and down, 2 metres a notch.
- **Side button held** and the mouse moved up or down: up and down smoothly.

## The fixed camera

With **Fixed camera** on ([option](help:option/fixedCamera), off at first) the camera behaves like the original's default view:

- the wheel **zooms** between near (12 m over the ground, looking 35° down) and far (110 m, 62° down);
- it keeps its height over hills and valleys as it scrolls, easing smoothly;
- the middle button only turns it round, never tilts it.

Without it, the camera is free: any height, any angle.

## Jumping

| Key or click | The camera goes to |
|---|---|
| **Space** | The selection. Press again for the next selected unit (a formation is one stop, at its centre). |
| **Click the minimap** | That point (gliding, or at once with [Camera Snapping](help:option/cameraSnapping)) |
| A **group key twice** (1 ... 0 within 0.4 s) | The middle of that group |
| A [selection key](selection.md#selection-keys) (V, I, B, ...) | The unit or building it picked |
| **Home** | The last warning's place; again, the one before (up to four) |
| An entry of the **timeline** (F7), the **replay log** or the replay bar | That event's place |

Each jump keeps the camera's height and angle and moves it so the place is in the middle of the view.

## Limits

- The camera stays over the playable map.
- It never goes lower than **3 m** above the ground under it ([Lowest height above ground](help:option/minHeight)) nor higher than **1000 m** ([Highest camera height](help:option/maxHeight)).
- How far you see is set in the graphics options (view distance).
