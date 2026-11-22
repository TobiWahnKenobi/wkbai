---
title: Load Game
ids: [ui/frontend/load, ui/frontend/load/tabs#the-tabs, ui/frontend/load/skirmish#the-tabs, ui/frontend/load/campaign#the-tabs, ui/frontend/load/multiplayer#the-tabs, ui/frontend/load/replays#replays, ui/frontend/load/list#the-list, ui/frontend/load/load#load, ui/frontend/load/rename#rename, ui/frontend/load/rename-dialog#rename, ui/frontend/load/delete#delete, ui/frontend/load/back#back, FRONT_END_LOAD_GAME_TITLE]
order: 30
---

# Load Game

Opens a saved game, or plays back a recorded one. You reach it from the [main menu](main.md) and from the [in-game menu](ingame-menu.md).

![Load Game with the saved games listed](shot:frontend-load)

## The tabs

- **Skirmish:** every saved game: the ones you saved, and the scenario saves that come with the game (such as "Marjoria's Keep").
- **Campaign** and **Multiplayer:** greyed out for now. Saved games aren't sorted by mode yet, so all of them are under Skirmish.
- **Replays:** the recorded games, see below.

## The list

One row per saved game, by name. Click one to select it; a double click loads it at once. The mouse wheel scrolls.

## Replays

The games recorded with **Record game** on the skirmish lobby's [Settings tab](skirmish/settings.md#record-game), newest first, with four columns:

- **Name:** the recording's name (at first the map, the date and the time).
- **Map:** the map it was played on.
- **Recorded:** when.
- **Length:** how long the game ran.

A replay plays the whole game again in the game itself, as it happened, with no fog of war. While it plays you can pause it, change its speed and jump forward; see [watching a replay](help:ui/hud/replay).

![The Replays tab](shot:frontend-load-replays)

## Load

Loads the selected saved game, or starts the selected replay. A double click on a row does the same.

## Rename

On the Replays tab only: gives the selected recording a new name. A box opens with the name to change; **OK** (or Enter) keeps the new name, **Cancel** (or Esc) leaves it as it was. Only the shown name changes, not the recording's folder.

## Delete

Deletes the selected saved game after asking you. Only saves you made yourself can be deleted; the game's own saves and levels can't, and the button stays grey for them. The save's side files go with it.

## Back

Returns to the screen you came from. Esc does the same.

## Tips

- Your saves are in your user folder, `%APPDATA%\wkbai\saved\Save_Games`, never in the game folder.
- A saved multiplayer game is continued from [Multiplayer > Load Game](multiplayer/index.md#load-game), not from here, so that the other players can take their places again.

> wkbai: replays are wkbai's own. Saves are written in the original game's own text format.
