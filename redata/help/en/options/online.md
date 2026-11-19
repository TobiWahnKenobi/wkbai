---
title: Online
ids: [ui/frontend/options/online, option/nakamaServer#nakama-server, option/nakamaServerKey#nakama-server-key, option/nakamaTitle#its-name-in-the-game-list, option/nakamaRelay#games-through-the-nakama-server, option/stunServer#stun-server-public-address, option/matchmaking#online]
order: 60
---

# Online

Online matchmaking: a server where hosted games are listed for everyone, so that players outside your local network can find and join them in [Multiplayer > Find Games](../ui/frontend/multiplayer/index.md#find-games). wkbai uses a Nakama server (free software by Heroic Labs), which anyone can run; there is no official one built in. Games on your local network are found without any of this.

To run a server yourself, follow [Hosting a Nakama server](../multiplayer/nakama-server.md). To use someone else's, ask its owner for its address and key and enter them here.

Changes take effect when you press OK: wkbai signs in again with the new settings. While you host or join a game through the server, they take effect once that game is over.

![The Online page](shot:options-online)

## Nakama server

The server's address, with `https://` or `http://` and the port if it isn't the usual one. For example:

- `https://games.example.org`: a server with HTTPS on the usual port;
- `http://203.0.113.5:7350`: a server on someone's home PC (7350 is Nakama's own port);
- `http://127.0.0.1:7350`: a server on this PC.

Empty (the default): online matchmaking is off, and Find Games says so. **Nakama server key**, **Its name in the game list** and **Games through the Nakama server** are greyed out until there is a server.

wkbai signs in when it starts and when you change these settings. If the sign-in fails, Find Games shows why under the server's heading; [Problems](../multiplayer/nakama-server.md#problems) explains the messages.

## Nakama server key

The server's key (Nakama's `socket.server_key`), which its owner gives you. Type it exactly. Empty: `defaultkey`, the key a new Nakama server starts with; a server set up as the [guide](../multiplayer/nakama-server.md#the-keys) shows has its own. The key only says which server this is; it is no password, and every player of that server has the same one.

## Its name in the game list

The heading the server's games get in Find Games, and the server's name in the host's [Offer on](../ui/frontend/multiplayer/host.md#offer-on) row, up to 40 characters. Only you see it, so choose any name, for example your community's. Empty: "Nakama".

## Games through the Nakama server

How a game's network traffic goes once you have found it:

- **If direct fails** (the default): the computers connect directly; if that doesn't work within about nine seconds (some routers block it), the game's traffic goes through the Nakama server instead.
- **Always:** always through the server. Slower, but it works behind any router.
- **Never:** only directly. When no direct connection comes up, joining fails.

The host's setting doesn't matter for this: the joining player's setting decides. Relayed games put all their traffic on the server, so its owner may ask you to keep **If direct fails**.

## STUN server (public address)

A server that tells your computer its public internet address, so the other player can reach it directly: `host:port`. Empty: `stun.l.google.com:19302`. Type `none` to use none. It is only asked when you host or join a game through the Nakama server.

A server set up with the [guide](../multiplayer/nakama-server.md#6-start-it) can run its own STUN server: then enter its name with port 3478, for example `games.example.org:3478`. Without a public address, direct connections between two home networks rarely work, and the game goes through the relay.

## Tips

- The settings are kept in the settings file under `matchmaking`.
- Running your own Nakama server: [Hosting a Nakama server](../multiplayer/nakama-server.md), step by step.

> wkbai: the original's online play went through GameSpy, which no longer exists. This is wkbai's own.
