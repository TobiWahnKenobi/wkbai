---
title: Skirmish lobby
ids: [ui/frontend/skirmish, ui/frontend/skirmish/tabs#the-tabs, ui/frontend/skirmish/start#start, ui/frontend/skirmish/back#back, ui/frontend/skirmish/problem#why-start-is-grey, FRONT_END_LOBBY_TITLE_SKIRMISH, FRONT_END_CHAT_START#start, FRONT_END_SETTINGS_MUST_SELECT_MAP#why-start-is-grey, FRONT_END_CHAT_AI_NOT_SUPPORTED#why-start-is-grey, FRONT_END_CHAT_DUPLICATE_COLOURS#why-start-is-grey, FRONT_END_CHAT_ARMIES_NOT_ENLISTED#why-start-is-grey, FRONT_END_CHAT_SINGLE_PLAYER_GAME#questions-at-the-start, FRONT_END_CHAT_ALL_ON_SAME_TEAM#questions-at-the-start, FRONT_END_CHAT_TOO_MANY_PLAYERS#questions-at-the-start]
order: 50
---

# Skirmish lobby

Set up a game on any map against AI Generals: who plays at each start position, the map and the rules, and (wkbai's own) day and night, seasons and weather. Then press Start.

The same lobby, with a list of guests and a chat added, is the host's lobby of a [multiplayer game](../multiplayer/host.md).

![The skirmish lobby, Players tab](shot:frontend-skirmish-players)

## The tabs

- **[Players](players.md):** the start positions: you, AI Generals or nobody, with colours and teams.
- **[Settings](settings.md):** the map, the starting resources, the game mode and the rule switches.
- **[Seasons & Weather](weather.md):** day and night, the seasons, the weather and the sky.
- **[Army Enlistment](army.md):** only in Valhalla mode: the companies your army starts with.

## Start

Starts the game with this setup. It is grey while something stops the game from starting; the reason is shown in red beside it.

## Why Start is grey

- **No map chosen:** pick one on the [Settings tab](settings.md#the-map-list).
- **AI Generals on an island map:** the AI can't play "3 Isles" and "Four Islands"; make their positions human or empty, or choose another map.
- **Two players with one colour:** every player needs a colour of their own; click a colour on the [Players tab](players.md#colour) to change it.
- **Valhalla without an army:** in Valhalla mode every human player must enlist an army on the [Army Enlistment tab](army.md).
- **No human position:** one position must be yours.
- In multiplayer also: a guest who isn't ready, or a position held by a guest who has left.

## Questions at the start

Some setups are allowed, but you are asked first:

- **Only one player:** starting with no opponents at all.
- **Everyone on one team:** nobody to fight.
- **Too many guests** (multiplayer): guests without a position, beyond the spectators allowed, are removed when the game starts.

## Back

Returns to the [main menu](../main.md). Esc does the same. The lobby keeps its setup for the next time you open it, until you close wkbai.

## Tips

- The lobby remembers the map you chose last; a new map sets up its positions again (you at the first, Random AI Generals at the others).
- Hover over an AI General in the list to read its story before you pick it.

> wkbai: the checks before the start are the original's, in its order. Seasons & Weather, the Lua AI, the ministry AI, Aggressive AI and Record game are wkbai's own.
