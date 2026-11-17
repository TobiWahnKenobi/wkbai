---
title: Developer and cheat tools
ids: [controls/developer-tools, key/serverWindow#server-window-f1, key/debugger#debugger-f2, key/environmentPanel#environment-panel-f9, ui/hud/debug/server#server-window-f1, ui/hud/debug/client#debugger-f2, ui/hud/debug/environment#environment-panel-f9, ui/hud/debug/frame-timings#frame-timings, ui/hud/agent#agent-control]
order: 90
---

# Developer and cheat tools

These windows are for mod makers, testers and the curious. They show, and can change, the whole game's state, so they are **cheat keys**: they work only

- in **single player**,
- not in a replay,
- with **cheat mode** on in the skirmish settings, or on a level opened for editing.

Elsewhere the keys do nothing. The windows are plain tool windows (you can move them and change their size), in English only.

## Server window (F1)

Key action [Server window](keys.md#cheat-keys-cheat-mode-only). The game as the server holds it, which is the truth, fog of war or not:

- **Server messages:** what the server has said.
- **Server Object Tree:** every object of the game, player by player.
- **Server Object Properties:** the object picked in the tree: its items (values), associates, orders and tasks.
- **Additional windows**, such as the map's tiles.

While it is open the server window takes the screen in place of the game. **F1** closes it again, always.

## Debugger (F2)

Key action [Debugger](keys.md#cheat-keys-cheat-mode-only). The game as your screen sees it:

- **Client messages**, **Client Object Tree** and **Client Object Properties**, as above, for what your client knows. A left-click on the battlefield picks the object under the mouse.
- **Camera:** where it is and where it looks.
- **Stampdown:** pick any object of the game set and place it for any player.
- **Diagnostics**, and **Commands:** its tabs give orders, spawn units and put up buildings.
- **Client Interface Debug:** objects drawn, the frame rate and frame times, **Dev mode** (select anything, show editor stand-ins), the terrain on or off, the sun's direction and bump mapping.

## Environment panel (F9)

Key action [Environment panel](keys.md#cheat-keys-cheat-mode-only). wkbai's day and night, seasons and weather, as they are now (time, season, weather, temperature, clouds, rain, snow, wetness, ice, water level, the moon) with controls to change them on the spot.

## Frame timings

**Ctrl+F12** (key action [Frame timings](keys.md#windows-and-tools), no cheat needed): how long each frame takes on the processor and the graphics card over the last seconds, and each part of the frame's share. For finding what slows a big map down.

## Map editor

**F8** (key action [Map editor](keys.md#windows-and-tools)) opens the map editor over the running level, in single player, on a level opened from the main menu for editing. See [the map editor](help:ui/editor).

## Agent control

wkbai can let a program (an "agent", such as an AI assistant) play through the screen, when switched on in **Options > Agent control**. While it is on:

- the agent's own screen has a banner and a frame, and every player sees "[AGENT] Agent control on for:" with the names at the top of the screen;
- **Ctrl+Alt+F12** (key action [Agent control: take over](keys.md#game)) takes the mouse and keyboard back from the agent; press it again to hand them back.

See [Agent control](help:option/agentControl).

