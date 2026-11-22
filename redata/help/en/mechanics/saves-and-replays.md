---
title: Saves and replays
ids: [mechanics/saves-and-replays, mechanic/saves, mechanic/save-games, mechanic/replays, mechanic/recordings]
order: 220
---

# Saves and replays

## Saving

- **Save** and **Save As** are in the in-game menu (Esc). Saves go to your own folder (`saved\Save_Games` under `%APPDATA%\wkbai`), never into the game's folder.
- A save holds the whole game: every unit and building with its orders, the players' stocks and research, relations, offers and the AI's state. wkbai keeps its own extras beside the save file: the AI settings, the weather and season, disguises, the unit history and the timeline.
- **Loading** (main menu or in-game menu) puts every human's camera back at their start position.
- **Deleting:** the Load Game screen can delete your own saves; the game's own can't be deleted.
- **Multiplayer:** only the host saves ([multiplayer](multiplayer.md#the-host)).

wkbai writes saves in the original's text format, and reads the original's saves.

## Not kept by a save

- The AI's plan in progress restarts after loading (its towns, armies and orders stay).
- Game text windows open at the moment of saving don't come back.

## Recordings

> **wkbai.** Recordings and replays are wkbai's own.

- Turn on **Record game** in the lobby's settings. The multiplayer host records too.
- A recording isn't a film: it keeps the start and every order given, so the game can be played again exactly as it went. A 25-minute game without human orders takes a few bytes; one with yours a little more.
- Recordings are kept in `saved\Recordings`, one folder per game.

## Watching a replay

Open **Load Game → Replays** ([replay controls](../ui/hud/replay-controls.md)). The list shows each recording's name, map, date and length; *Rename* gives it a name of your own.

- You watch as the recorded player, **without fog of war**, and can't give orders.
- **Controls:** pause and play (P), speeds from 0.5× to 16× (keypad + and −), and a jump forward. A replay can't go back.
- **The progress bar** marks the game's battles (red) and new settlements (white); click one to jump there.
- **Announcements** at the top: battles and sieges, buildings finished, destroyed or captured, players defeated and units trained, with markers on the minimap. **L** opens the log of them all; click an entry to move the camera.

![A replay with its controls, the progress bar and a battle announced](shot:mech-replay-viewer)
