---
title: Multiplayer
ids: [ui/frontend/multiplayer, ui/frontend/multiplayer/tabs#the-tabs, ui/frontend/multiplayer/new-game#new-game, ui/frontend/multiplayer/load-game#load-game, ui/frontend/multiplayer/games#find-games, ui/frontend/multiplayer/games/list#find-games, ui/frontend/multiplayer/games/hide-full#hide-full-and-hide-locked, ui/frontend/multiplayer/games/hide-locked#hide-full-and-hide-locked, ui/frontend/multiplayer/games/refresh#refresh, ui/frontend/multiplayer/name#name, ui/frontend/multiplayer/address#host-address, ui/frontend/multiplayer/password#password, ui/frontend/multiplayer/create#create, ui/frontend/multiplayer/join-ip#join-ip-address, ui/frontend/multiplayer/join#join-game, ui/frontend/multiplayer/back#back, FRONT_END_MULTIPLAYER_BUTTON_CREATE#create, FRONT_END_MULTIPLAYER_BUTTON_JOIN_IP#join-ip-address]
order: 55
---

# Multiplayer

Play with other people over a local network or the internet. From here you host a new game on a map, host a saved game to carry on with it, join a game by its address, or pick one from the list of games on offer.

![The Multiplayer screen, Find Games tab](shot:frontend-multiplayer-games)

## The tabs

- **New Game:** host a new game on a map.
- **Load Game:** host a saved game to play on.
- **Find Games:** the games others are hosting, to join one.

## New Game

The list of maps, as in the [skirmish lobby](../skirmish/settings.md#the-map-list). Choose one, type your name and press **Create**.

![The Multiplayer screen, New Game tab](shot:frontend-multiplayer)

## Load Game

The saved games. Choose one and press **Create**: the [saved game's lobby](mpload.md) opens, where you decide who plays which of the saved players.

## Find Games

The games on offer, grouped by where they were found: first the local network, then each online matchmaking service under its name. A heading says when a service is off, still signing in, or has failed, and why. The list looks again every five seconds.

Each game shows:

- **Game:** its name.
- **Map:** the map it is played on.
- **Players:** the players in it and the places it has; "+n watch" when spectators may join.
- **Lock:** "locked" when it needs a password.
- **Mods:** the host's mods, in green when yours fit, in red when they don't. "other version" when the host runs another version of wkbai.
- **Ping:** how long a message takes to the host and back.
- The last column: **lobby** (not started yet) or **in progress** (you can still join a running game where a place is free, or watch).

A grey game can't be joined; when you select it, the reason is shown at the bottom of the screen. Double-click a game to join it.

## Hide full and Hide locked

The two round switches above the list leave out the games that have no free place, and the games that need a password.

## Refresh

Looks for games at once instead of waiting for the next five-second update.

## Name

Your name for other players, up to 24 characters. It is kept for the next time. Hosting and joining need one.

## Host address

The address of the computer to join: a name or IP address, with `:port` when the host doesn't use the usual port 1234 (for example `192.168.1.20` or `example.org:1240`). Spaces are ignored. It is kept for the next time; Enter in this field joins.

## Password

**Hosting:** a password guests must give to join (empty: anyone may join). **Joining:** the host's password. It shows as stars.

## Create

Hosts a game: on the New Game tab, the [host's lobby](host.md) opens on the chosen map; on the Load Game tab, the [saved game's lobby](mpload.md). Needs a name and a chosen map or save. Not shown on the Find Games tab.

## Join IP Address

Joins the game at the host address; the [guest's lobby](guest.md) opens and connects.

## Join game

On the Find Games tab: joins the selected game (with the password, if it has one). Grey when the game can't be joined.

## Back

Returns to the [main menu](../main.md).

## Tips

- On a local network, games appear in Find Games by themselves; there is no need to type an address.
- To host over the internet without a matchmaking service, the port 1234 (UDP) has to reach your computer, which may mean forwarding it on your router.
- The online matchmaking service is set up on the [Online options](../../../options/online.md) page.

> wkbai: the original's multiplayer ran through GameSpy, which is long gone. wkbai's has its own network play, a local game list, optional online matchmaking, passwords, spectators and joining running games.
