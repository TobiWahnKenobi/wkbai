---
title: Replay controls
ids: [ui/hud/replay, ui/hud/replay/play#the-controls, ui/hud/replay/speed#the-controls, ui/hud/replay/jump#jumping-ahead, ui/hud/replay/bar#the-bar, ui/hud/replay/log#the-event-log, ui/hud/replay/events#events]
order: 100
---

# Replay controls

![The replay controls under the top bar](shot:hud-replay-controls)

A recorded game plays back in the normal game screen, with a bar of controls under the top bar, in the middle. You can move the camera, select units to look at them, and open the [statistics and timeline](chronicle.md), which show every player. You can't give orders: there are no command buttons. Load a recording from **Load Game > Replays** (see [Load Game](help:ui/frontend/load)).

## The controls

From left to right:

- **Play / pause** (**P**).
- **The speeds** 0.5x, 1x, 2x, 4x, 8x and 16x; the one in use is lit. **Keypad +** and **Keypad −** step through them.
- **">> 1:00"** jumps a minute ahead.
- **Log** opens the [event log](#the-event-log) (**L**).
- **The time played / the whole length**, for example "12:40 / 25:03".

Under them, a line says what is going on: "Paused", "Jumping ahead to 15:00...", "The end of the recording", or "Speed 16x (reached 6.2x)" when the computer can't play that fast.

## The bar

Under the buttons, a bar fills as the recording plays. Ticks on it mark events: **red** for fights, **white** for settlements and players. Point at a tick to read the event; click it to look at its place.

## Jumping ahead

Click the bar **ahead** of the played part to jump there, or press ">> 1:00". The game runs to that point as fast as it can while you watch, the target shown lighter on the bar. A replay can't go back.

## The event log

![The replay's event log with all filters on](shot:hud-replay-log)

**L** or **Log** opens a window listing every event announced so far, newest first, with its time and icon. The switches at its top filter it: **Fights**, **Settlements**, **Buildings**, **Units**. Click a row to look at the place.

## Events

As the replay goes, each event is announced in a line at the top of the screen (fading after 6 seconds) and marked on the minimap for a while:

- battles (crossed swords; a boat for a sea battle), sieges, raids, and when a battle ends who lost how many;
- settlements founded, buildings finished, destroyed or captured;
- units trained (summed per player every 15 game seconds);
- players defeated (a red cross);
- heroes risen and fallen.

> wkbai: recordings and replays are wkbai's own. "Record game" in the lobby's settings switches recording on.
