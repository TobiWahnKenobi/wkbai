---
title: Keys
ids: [controls/keys, controls/keys/rebind#changing-the-keys, controls/keys/modifiers#keys-with-ctrl-shift-or-alt, key/cameraForward#camera, key/cameraBack#camera, key/cameraLeft#camera, key/cameraRight#camera, key/cameraFast#camera, key/cameraRotate#camera, key/cameraToSelection#camera, key/nextCommand#selection-and-orders, key/deleteSelected#selection-and-orders, key/turnBuildingLeft#selection-and-orders, key/turnBuildingRight#selection-and-orders, key/selectVillages#selection-and-orders, key/selectIdlePeasants#selection-and-orders, key/selectSpecialUnits#selection-and-orders, key/selectBarracks#selection-and-orders, key/selectStables#selection-and-orders, key/selectEconomicBuildings#selection-and-orders, key/selectPalace#selection-and-orders, key/selectBurningBuildings#selection-and-orders, key/selectScouts#selection-and-orders, key/selectSupplyWagons#selection-and-orders, key/selectTradingPosts#selection-and-orders, key/selectTowers#selection-and-orders, key/selectDocks#selection-and-orders, key/selectPlacesOfInterest#selection-and-orders, key/follow#selection-and-orders, key/guard#selection-and-orders, key/halt#selection-and-orders, key/patrol#selection-and-orders, key/lastWarning#selection-and-orders, key/groupStore1#control-groups, key/groupStore2#control-groups, key/groupStore3#control-groups, key/groupStore4#control-groups, key/groupStore5#control-groups, key/groupStore6#control-groups, key/groupStore7#control-groups, key/groupStore8#control-groups, key/groupStore9#control-groups, key/groupStore0#control-groups, key/groupSelect1#control-groups, key/groupSelect2#control-groups, key/groupSelect3#control-groups, key/groupSelect4#control-groups, key/groupSelect5#control-groups, key/groupSelect6#control-groups, key/groupSelect7#control-groups, key/groupSelect8#control-groups, key/groupSelect9#control-groups, key/groupSelect0#control-groups, key/openMenu#game, key/openOptions#game, key/chat#game, key/pause#game, key/speedUp#game, key/slowDown#game, key/objectives#game, key/agentTakeOver#game, key/hideInterface#windows-and-tools, key/statistics#windows-and-tools, key/timeline#windows-and-tools, key/frameTimings#windows-and-tools, key/mapEditor#windows-and-tools, key/editorUndo#map-editor-f8, key/editorRedo#map-editor-f8, key/editorGrab#map-editor-f8, key/editorTurn#map-editor-f8, key/editorTurnBack#map-editor-f8, key/replayPause#replays, key/replayLog#replays, key/replayFaster#replays, key/replaySlower#replays]
order: 70
---

# Keys

Every key below is a **key action**: it has up to two keys, and both can be changed (see [Changing the keys](#changing-the-keys)). The tables give the defaults, grouped as on the **Options > Controls > Keyboard** page. Keys marked *held* work while you hold them; the rest act once per press.

"Where" says when a key works: **always** (in a game, a replay or the menus over them), **game** (playing, not in a replay), **editor** or **replay**.

## Camera

| Action | Keys | Where | Does |
|---|---|---|---|
| Scroll forward | Up, W | always, *held* | Moves the camera the way it looks |
| Scroll back | Down, S | always, *held* | Back |
| Scroll left | Left, A | always, *held* | Left |
| Scroll right | Right, D | always, *held* | Right |
| Scroll faster | Left Shift, Right Shift | always, *held* | Scrolling three times as fast (option) |
| Turn the camera | Keypad 0 | always, *held* | Move the mouse to turn, as with the middle button |
| Jump to the selection | Space | game | Again: the next selected unit; a formation is one stop |

See [Camera](camera.md).

## Selection and orders

| Action | Keys | Where | Does |
|---|---|---|---|
| Next command at the cursor | E | game | The next order that fits under the mouse ([Cursors](../ui/hud/cursors.md#more-than-one-order)) |
| Destroy the selection | Delete | game | Destroys what is selected, at once (in the map editor: removes it, with undo) |
| Turn the building being placed (left) | [ | game | A quarter turn |
| Turn the building being placed (right) | ] | game | A quarter turn the other way |
| Select a Village | V | game | Again: the next |
| Select an idle Peasant | I | game | Again: the next |
| Select a priest, druid, succubus or sapper | X | game | Again: the next |
| Select a Barracks | B | game | Again: the next |
| Select a Stables | L | game | Again: the next |
| Select an economic building | K | game | Again: the next |
| Select the Manor, Keep, Castle or Palace | M | game | Again: the next |
| Select a burning building | Z | game | Again: the next |
| Select a Scout or Spy | N | game | Again: the next |
| Select a Supply Wagon | C | game | Again: the next |
| Select a Trading Post | T | game | Again: the next |
| Select a tower or fort | Y | game | Again: the next |
| Select a Dock | U | game | Again: the next |
| Show a place of interest | J | game | Again: the next (the camera goes there; nothing is selected) |
| Follow | F | game, *held* | Right-click a unit to follow it |
| Guard | G | game, *held* | Right-click a unit or building to guard it |
| Halt | H | game | Stops the selection |
| Patrol | R | game, *held* | Right-click: patrol there; with Shift: one more point |
| To the last warning | Home | game | The camera to the latest warning; again, the one before (up to four) |

The selection keys pick the nearest to the camera first; see [Selection keys](selection.md#selection-keys). Orders: [Orders](orders.md).

## Control groups

| Action | Keys | Where |
|---|---|---|
| Store control group 1 ... 9 | Ctrl+1 ... Ctrl+9, Ctrl+Keypad 1 ... Ctrl+Keypad 9 | game |
| Store control group 0 | Ctrl+0 | game |
| Select control group 1 ... 9 (twice: go there) | 1 ... 9, Keypad 1 ... Keypad 9 | game |
| Select control group 0 (twice: go there) | 0 | game |

Each group number is an action of its own (20 in all). See [Control groups](selection.md#control-groups).

## Game

| Action | Keys | Where | Does |
|---|---|---|---|
| Game menu | Esc | always | Opens the game menu; pauses a single-player game |
| Options | F10 | always | Opens the Options over the game; Back goes to the game menu. Pauses a single-player game |
| Chat | Tab, Enter | always | Opens the chat line in multiplayer ([Chat](../ui/hud/chat.md)) |
| Pause | P | game | Pauses or goes on |
| Game faster | Keypad + | game | Doubles the speed |
| Game slower | Keypad − | game | Halves the speed |
| Mission objectives | O | game | Opens or closes the objectives window, on levels that have one |
| Agent control: take over | Ctrl+Alt+F12 | always | Takes the input back from an agent; again: gives it back ([Agent control](developer-tools.md#agent-control)) |

## Windows and tools

| Action | Keys | Where | Does |
|---|---|---|---|
| Show or hide the interface | F3 | always | [Hiding the interface](../ui/hud/index.md#hiding-the-interface) |
| Statistics | F6 | always | [Statistics](../ui/hud/chronicle.md#statistics) |
| Timeline | F7 | always | [Timeline](../ui/hud/chronicle.md#timeline) |
| Frame timings | Ctrl+F12 | always | [Frame timings](developer-tools.md#frame-timings) |
| Map editor | F8 | game | Opens or closes the map editor: single player, levels opened for editing only |

## Map editor (F8)

| Action | Keys | Where |
|---|---|---|
| Undo | Ctrl+Z | editor |
| Redo | Ctrl+Y, Ctrl+Shift+Z | editor |
| Move the selected objects | G | editor |
| Turn the selected objects | R | editor |
| Turn the selected objects back | Shift+R | editor |

See [the map editor](help:ui/editor) for the editor itself.

## Replays

| Action | Keys | Where |
|---|---|---|
| Pause or play | P | replay |
| Show the log | L | replay |
| Faster | Keypad + | replay |
| Slower | Keypad − | replay |

See [Replay controls](../ui/hud/replay-controls.md).

## Cheat keys (cheat mode only)

| Action | Keys | Where |
|---|---|---|
| Server window | F1 | game, cheats |
| Debugger | F2 | game, cheats |
| Environment panel | F9 | game, cheats |

They work only in single player with cheat mode on; see [Developer and cheat tools](developer-tools.md).

## Keys with Ctrl, Shift or Alt

- A key action **pressed once** needs exactly its modifiers: with Shift held, **E** is not "Next command" and **1** is not "Select group 1".
- A key action **held** (the camera keys, F, G, R) needs at least its modifiers, so you can scroll with Shift held for speed.
- A modifier on its own (Left Shift for "Scroll faster") works whatever else is held.
- While you type (the chat line, a name), no key action works.

## Changing the keys

On **Options > Controls > Keyboard** each action has two key boxes:

1. Click a box; it waits for a key.
2. Press the key, with Ctrl, Shift or Alt if you like. A modifier pressed and let go alone becomes the key itself. **Esc** cancels.
3. If another action uses that key where both work, you are asked first.

Each action can go back to its default; all of them at once too. Changed keys are kept with your settings (under `keyBindings` in `wkconfig.json`).

> wkbai: the original's letters are kept (its game set's hot keys: V, I, X, B, L, K, M, Z, N, C, T, Y, U, J, and E, F, G, H, O, P, R, Space, Home). wkbai adds the rest, makes every key changeable and gives each two slots. The original's Q ("ignore distance check") isn't in wkbai: what it did is unknown.
