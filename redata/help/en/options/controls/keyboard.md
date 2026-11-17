---
title: Keyboard
ids: [ui/frontend/options/keyboard, option/keyBindings, ui/frontend/options/keyboard/key#key-and-second-key, ui/frontend/options/keyboard/second-key#key-and-second-key, ui/frontend/options/keyboard/capture#changing-a-key, ui/frontend/options/keyboard/clear#clear, ui/frontend/options/keyboard/default#default, ui/frontend/options/keyboard/conflict#when-a-key-is-already-used, ui/frontend/options/keyboard/swap#when-a-key-is-already-used, ui/frontend/options/keyboard/reset-all#reset-all-to-defaults]
order: 10
---

# Keyboard

Every key action of the game, grouped under headings, each with two keys. Any of them can be changed, including combinations with Ctrl, Shift and Alt. What each action does is described in [the list of keys](help:controls/keys).

![Controls > Keyboard](shot:options-keyboard)

## The groups

- **Camera:** scrolling, turning the camera, jumping to the selection.
- **Selection and orders:** selecting buildings and units of a kind, orders such as halt, patrol, follow and guard, turning a building while placing it.
- **Control groups:** storing and selecting groups 0 to 9.
- **Game:** the in-game menu, Options, chat, pause, game speed, the mission objectives, agent control's take-over key.
- **Windows and tools:** hiding the interface, statistics, the timeline, frame timings, the map editor.
- **Map editor (F8):** undo, redo, moving and turning objects in the editor.
- **Replays:** pause, the log, faster and slower while watching a replay.
- **Cheat keys (cheat mode only):** shown in grey; they work only in a skirmish with [Cheat mode](../../ui/frontend/skirmish/weather.md#cheat-mode) on.

The mouse wheel scrolls the list; "Scroll for more keys" shows when there are more than fit.

## Key and Second key

The two buttons in each row: the action's first and second key, or "(none)". Either one triggers the action.

## Changing a key

Click a key button: it reads "Press a key...". Press the key, or hold Ctrl, Shift or Alt and press the key for a combination. **Esc** stops waiting and leaves the key as it was.

## Clear

Shown while a key is being waited for: leaves that slot with no key at all.

## Default

Puts the action's own two keys back. Grey when they are the defaults already.

## When a key is already used

A key that another action uses at the same time and place has a red frame. When you give an action such a key, you are asked what to do:

- **Swap:** the other action gets this action's old key.
- **Clear the other:** the other action loses the key.
- **Cancel:** nothing changes.

Keys only clash where both actions work: the map editor's keys and the replay keys can share keys with the game's, since they work at other times.

## Reset all to defaults

Puts every key of every action back to its default, after asking.

## Tips

- Changes are kept when you press OK, and undone by Cancel, like every other setting.
- The keys are saved in the settings file under `keyBindings`.

> wkbai: the original's keys can't be changed. wkbai's defaults follow the original's where it has a key for the action (O for the mission objectives, for example), and every key can be changed.
