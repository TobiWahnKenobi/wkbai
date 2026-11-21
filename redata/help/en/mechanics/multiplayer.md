---
title: Multiplayer
ids: [mechanics/multiplayer, mechanic/multiplayer, mechanic/spectators, mechanic/ai-takeover, mechanic/host]
order: 210
---

# Multiplayer

The rules of a multiplayer game are those of a skirmish. This page covers what is different with other people. How to host and join is in [the multiplayer screens](help:ui/frontend/multiplayer).

## The host

- **The game runs on the host's computer.** Everyone else sends their orders there and sees the result. If the host leaves, the game ends.
- **Up to 8 players**, humans and AI Generals together.
- **Only the host saves.** Loading a saved multiplayer game opens its lobby: each saved player is taken by the guest of the same name, or given out by the host; players nobody takes are played by AI Generals.
- **Cheat mode** is never available with guests.

## Before the start

- Every human must pick a unique colour and press Ready.
- Teams, Allow Diplomacy and Team Reconnaissance decide the relations ([diplomacy](diplomacy.md)).
- **Docks** appear only when no AI General plays ([ships](naval.md)).
- **Password** and **Lock Server** keep strangers out: a locked server refuses new guests; those already in stay.

## During the game

- **Chat:** Enter (or Tab) opens a line for everyone.
- **A player who leaves:** with *AI takes over players who leave* (on by default), a random AI General takes over that player's town and army, and everyone is told. Otherwise the player's side stands idle.
- **Joining a game in progress:** a guest who connects later may take over a player an AI plays (or one whose human left), or watch.
- **Spectators** (if the host allows them, up to a set number) see the whole map and every player's statistics, but can't act.
- **Beaten players** may stay and watch; they then see everything, and their chat reaches only other watchers, so they can't tell the players what they see.

## Mods and versions

- Everyone must run a compatible wkbai. The host chooses how strict: the same network version, the same build (default), or an identical copy.
- **Mods that change the rules** (the game set, maps, levels) must match the host's, unless the host sends its own to guests for the game. Mods that only change looks or sounds are free. The host can also allow no mods, or only its own. See [mods](mods.md#mods-in-multiplayer).

> **wkbai.** The original's GameSpy service is long gone; wkbai has its own matchmaking and its own network protocol, so it can't play against the original game. Joining running games, spectators, AI takeover and the build check are wkbai's own.

![A multiplayer lobby with two humans and an AI General](shot:mech-multiplayer-lobby)
