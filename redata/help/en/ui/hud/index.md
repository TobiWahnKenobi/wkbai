---
title: The in-game screen
ids: [ui/hud, ui/hud/screen, ui/hud/hidden#hiding-the-interface, ui/hud/locked#when-the-interface-is-locked, ui/hud/fps#frames-per-second]
order: 10
---

# The in-game screen

![The whole screen in a skirmish, a peasant selected](shot:hud-overview)

The battlefield fills the window. The interface sits around its edges, so the middle stays free for the view.

## The parts

| Where | What | Page |
|---|---|---|
| Along the top | **The top bar.** At the left: Settings, Mission Objectives, Global Diplomacy, Diplomacy and Tributes. At the right: your colour, population, food, wood and gold. | [Top bar](top-bar.md) |
| Under the top bar, left | **The speed line:** the game speed, or "Paused". | [Top bar](top-bar.md#game-speed-line) |
| Under the top bar, right | **The weather line:** time of day, season, weather and temperature, when they are switched on. | [Top bar](top-bar.md#weather-line) |
| Under the top bar, middle | **Notices:** diplomacy news and tributes, in the colour of the player concerned. | [Chat and notices](chat.md#notices) |
| Upper left | **The build menu** (a builder selected), or **the train and research buttons** (a building selected). | [Command buttons](command-panel.md) |
| Lower left | **The selection panel:** the selected unit or building turning in its window, its hit points, its name and its stats, with the command buttons around the rim. | [Selection panel](selection-panel.md) |
| Bottom, middle | **Formation buttons** (a formation selected). | [Command buttons](command-panel.md#formation-buttons) |
| Lower right | **The minimap** with its four buttons, and above it the **peasant activity** tubes. | [Minimap](minimap.md), [Peasant activity](peasant-activity.md) |
| Left, a tenth of the way down | **Chat lines** (multiplayer). | [Chat and notices](chat.md) |
| On the battlefield | Health rings, building health bars, hero names, range circles, order flags. | [On the battlefield](world-markers.md) |
| Anywhere | **Windows:** game texts and briefings, mission objectives, the diplomacy windows, statistics and the timeline. | [Text windows](text-windows.md), [Diplomacy windows](diplomacy-window.md), [Statistics and timeline](chronicle.md) |

The mouse pointer itself changes with what a right-click would do: see [Cursors](cursors.md).

## Tooltips

Hold the mouse over any button for a moment and a tooltip names it. Command buttons say why they can't be used right now ("not enough gold", for example), in the game set's own words.

## Hiding the interface

**F3** (key action [Show or hide the interface](../../controls/keys.md#windows-and-tools)) hides the top bar, both bottom panels, the minimap, the command buttons, the health bars and the hero names. Press it again to bring them back. Windows that are open, such as the statistics, stay. Use it for screenshots or to watch a battle.

## When the interface is locked

During some campaign and tutorial scenes (an intro, an ending), the level script switches the interface off:

- the top bar and the panels are gone;
- the game takes no clicks and no keys, so you can't select, give orders or move the camera;
- **Esc** still opens the game menu, and the scene's own window still takes its clicks, so its OK button gives the interface back.

A replay is never locked.

## Frames per second

With **Show FPS** on ([option](help:option/showFps)), a small dark box under the speed line shows the frame rate and the frame time ("60 FPS 16.40 ms (p99 18.20)"). The second number is the average time per frame; p99 is the slowest frames' time.

## Agent control

When a player has agent control switched on, every player sees a gold banner at the top centre: "[AGENT] Agent control on for:" and the names. See [Developer and cheat tools](../../controls/developer-tools.md#agent-control).

## Screen size

The interface is laid out for a window 1024 pixels wide or more. In a narrower window the top bar shrinks to fit. **Interface size** in the options scales the whole interface for high-resolution screens.

> wkbai: the original's layout is kept, with additions of wkbai's own: the speed and weather lines, the unit names and kill counts, the turning portrait, building health bars and the peasant activity counts in colour.
