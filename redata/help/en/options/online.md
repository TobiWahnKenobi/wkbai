---
title: Online
ids: [ui/frontend/options/online, option/nakamaServer#nakama-server, option/nakamaServerKey#nakama-server-key, option/nakamaTitle#its-name-in-the-game-list, option/nakamaRelay#games-through-the-nakama-server, option/stunServer#stun-server-public-address, option/matchmaking#online]
order: 60
---

# Online

Online matchmaking: a server where hosted games are listed for everyone, so that players outside your local network can find and join them in [Multiplayer > Find Games](../ui/frontend/multiplayer/index.md#find-games). wkbai uses a Nakama server (free software by Heroic Labs), which anyone can run; there is no official one built in. Games on your local network are found without any of this.

Changes take effect when you press OK.

![The Online page](shot:options-online)

## Nakama server

The server's address, with `https://` or `http://` and the port if it isn't the usual one (for example `https://games.example.org`). Empty (the default): online matchmaking is off, and Find Games says so. The other fields are greyed out until there is a server.

## Nakama server key

The server's key, which its owner gives you. Empty: `defaultkey`, the key a new Nakama server starts with.

## Its name in the game list

The heading the server's games get in Find Games, up to 40 characters. Empty: "Nakama".

## Games through the Nakama server

How a game's network traffic goes once you have found it:

- **If direct fails** (the default): the computers connect directly; if that doesn't work within about nine seconds (some routers block it), the game's traffic goes through the Nakama server instead.
- **Always:** always through the server. Slower, but it works behind any router.
- **Never:** only directly.

## STUN server (public address)

A server that tells your computer its public internet address, so the other player can reach it directly: `host:port`. Empty: `stun.l.google.com:19302`. Type `none` to use none. It is only asked when you host or join a game through the Nakama server.

## Tips

- The settings are kept in the settings file under `matchmaking`.
- Running your own Nakama server is described in wkbai's source, in its `server` folder.

> wkbai: the original's online play went through GameSpy, which no longer exists. This is wkbai's own.
