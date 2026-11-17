---
title: Hosting a game
ids: [ui/frontend/mphost, ui/frontend/mphost/guests#the-guests, ui/frontend/mphost/remove#the-guests, ui/frontend/mphost/chat#chat, ui/frontend/mphost/offer#offer-on, ui/frontend/mphost/agents#agent-players-in-the-game, ui/frontend/mphost/start#start, ui/frontend/mphost/back#back, ui/frontend/mphost/settings/lockServer#lock-server, ui/frontend/mphost/settings/aiTakeOver#ai-takes-over-players-who-leave, ui/frontend/mphost/settings/allowSpectators#allow-spectators, ui/frontend/mphost/settings/maxSpectators#allow-spectators, ui/frontend/mphost/settings/modPolicy#mods, ui/frontend/mphost/settings/buildPolicy#builds, ui/frontend/mphost/settings/allowAgents#agent-players, FRONT_END_CHAT_LOCK_SERVER#lock-server]
order: 10
---

# Hosting a game

The host's lobby is the [skirmish lobby](../skirmish/index.md) with the guests who have joined, a chat and a few more settings. The game runs on the host's computer; the guests' computers show it and send their orders.

![The host's lobby with two guests](shot:frontend-mphost)

## The guests

The list beside the general's story, under "Player (your name, host)": every guest who has joined. A guest at a position shows its number and whether the guest is **ready** or **not ready**; one without a position shows its name alone. "Waiting for players to join ..." means nobody has come yet.

- **Placing a guest:** select a position in the list of start positions, then click the guest. A guest can also take a free position (an AI General's or an empty one) from their own screen.
- **Remove:** appears at the end of the row under the mouse; a click disconnects that guest.

## Chat

Under the guests: the lobby's messages. Type in the line at the bottom and press Enter to send.

## Offer on

The row at the bottom left: where the game is offered. The local network is always ticked. Each online matchmaking service you have set up can be ticked to list the game there too; one that isn't ready shows its state, and the mouse over it shows more.

## Agent players in the game

When a guest (or you) lets a program play through agent control, a yellow line at the bottom names them, on every tab. Everyone in the lobby sees it.

## The host's settings

On the [Settings tab](../skirmish/settings.md), the host has these as well:

## Lock Server

On: no more guests can join. Those already in stay. Hosting anew unlocks it.

## AI takes over players who leave

On at first: when a guest leaves a running game, an AI General takes over their side, with " (AI)" after the name. Another guest can later take that place. Off: nobody plays that side any more.

## Allow spectators

Off at first. On: guests without a position may watch the game, and guests may join a running game to watch. **at most** sets how many: 1 to 12, at first 4. Spectators see the whole map.

## Mods

What the guests' mods must be (a click takes the next):

- **Mods: same game logic as the host** (at first): mods that change the rules must be the same as yours; mods that only change looks or sounds are free.
- **Mods: the host sends its own:** a guest who lacks your rule-changing mods gets their files from you for this game, and is back to their own mods afterwards.
- **Mods: none allowed:** guests may have no mods at all.
- **Mods: only the host's:** guests may only have mods you have on (or those named in the setting [`modAllowList`](../../../options/config-file.md#modallowlist)).

A guest who doesn't fit is turned away with the reason.

## Builds

Which wkbai the guests must run (a click takes the next):

- **Builds: any of this network version:** any wkbai that speaks the same network version.
- **Builds: the host's version** (at first): the same version as yours, unchanged.
- **Builds: identical to the host's:** a program identical to yours, checked by a challenge when the guest connects.

## Agent players

**Agent players: allowed** (at first) or **not allowed**: whether guests may join whose game a program plays through [agent control](../../../options/agent-control.md).

## Start

Starts the game once every guest has a position (or may watch) and is ready; see [why Start is grey](../skirmish/index.md#why-start-is-grey).

## Back

Closes the lobby and stops hosting: every guest is disconnected. Esc does the same.

## Tips

- Cheat mode is never available in multiplayer.
- A saved multiplayer game is hosted from the Multiplayer screen's Load Game tab, see [the saved game's lobby](mpload.md).

> wkbai: Lock Server is the original's. Spectators, AI takeover, joining running games and the mod and build checks are wkbai's own.
