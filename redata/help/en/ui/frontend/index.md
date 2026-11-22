---
title: Menus
ids: [ui/frontend, ui/frontend/dialog#questions-and-messages, ui/frontend/notice#questions-and-messages]
order: 20
---

# Menus

The menus are everything you see before a game starts and when you press Esc during one. They use the original game's art, fonts and sounds, laid out for today's wide screens.

![The main menu over the scrolling sunset](shot:frontend-main)

## The screens

| Screen | What it is for | Page |
|---|---|---|
| Main menu | The start of everything | [Main menu](main.md) |
| Load Game | Saved games and recorded replays | [Load Game](load.md) |
| Campaign | Your Kingmaker campaigns | [Campaign](campaign.md) |
| Campaign map | The provinces of one campaign | [Campaign map](campaign-map.md) |
| Skirmish | A game against AI Generals on any map | [Skirmish lobby](skirmish/index.md) |
| Multiplayer | Host, join or find games over a network | [Multiplayer](multiplayer/index.md) |
| Tutorial | The interactive tutorial and four lessons | [Tutorials](tutorial.md) |
| Options | Graphics, controls, sound and more | [Options](../../options/index.md) |
| Mods | Switch mods on and off, set their order | [Mods](mods.md) |
| Map Editor | Open a map to change it | [Map Editor](map-editor.md) |
| AI General Editor | Make AI Generals of your own | [AI General Editor](general-editor.md) |
| Credits | The people behind the game and wkbai | [Credits](credits.md) |
| In-game menu | Pause, save, load, restart, leave | [In-game menu](ingame-menu.md) |
| Save Game As | Name a save during a game | [Save Game As](save.md) |

## How the menus work

- **Buttons:** a click on a lit button acts. A grey button can't be used yet; its page says what it is waiting for (a map to be chosen, a name to be typed and so on).
- **Lists:** a click selects a row, a double click is the same as the screen's main button (Load, Edit, Join). The mouse wheel scrolls.
- **Text fields:** click one to type in it; the blinking bar shows which field gets your typing. Backspace deletes, Enter confirms where the screen has a confirm button. Characters a file name can't hold (`\ / : * ? " < > |`) are ignored in name fields.
- **Tabs:** the row of tabs at the top of a screen switches between its pages. A grey tab isn't available yet.
- **Esc** goes back one screen (on the main menu it does nothing). On Options, Esc is the same as Cancel.
- **Size:** the menus are drawn for a 1920 x 1080 window and scale with the window's height, so they look the same at any resolution. Wide windows show more of the background at the sides.

## Questions and messages

- **Questions:** before something that can't be undone (exiting, deleting, overwriting, restarting) a gold-framed box asks **Yes** or **No**. Esc is the same as No.
- **Messages:** a short note such as "Game saved successfully" appears near the bottom of the screen for a few seconds and goes away by itself.

## The background and music

- The sunset sky and the map behind the army scroll slowly; the campaign map has the sea instead.
- Whenever no music is playing in the menus, one of the original's four title tracks starts, picked at random. The music switch and volume in [Sound](../../options/sound.md) apply here too.
- Before the first menu, the original's intro video plays if it is in the game folder. Esc, Space, Enter or a click skips it.

> wkbai: the original game has no Mods, Map Editor or Replays screens and no Seasons & Weather tab; they are wkbai's own. The Options screen is wkbai's own as well, with far more settings than the original's.

## For modders

A mod can add its own screens and main-menu entries: scripts in a mod's `Lua\FrontEnd` folder run after the game's menus and reach them through the `UI` table (`UI.Screens`, `UI.push`, `MainMenuItems`).
