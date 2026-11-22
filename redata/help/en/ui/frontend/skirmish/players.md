---
title: Players tab
ids: [ui/frontend/skirmish/players, ui/frontend/mphost/players, ui/frontend/skirmish/players/positions#start-positions, ui/frontend/skirmish/players/colour#colour, ui/frontend/skirmish/players/team#team, ui/frontend/skirmish/players/human#human-player, ui/frontend/skirmish/players/kick#kick-ai-general, ui/frontend/skirmish/players/add#add-ai-general, ui/frontend/skirmish/players/tier#tiers, ui/frontend/skirmish/players/generals#ai-generals, ui/frontend/skirmish/players/random#ai-generals, ui/frontend/skirmish/players/luaai#ai, ui/frontend/skirmish/settings/aiMinistries#ai, ui/frontend/skirmish/players/biography#portrait-and-story, ui/frontend/skirmish/players/map#the-map, ui/frontend/skirmish/players/rename#rename, ui/frontend/skirmish/players/portrait#select-portrait, ui/frontend/skirmish/players/rename-dialog#rename, FRONT_END_LOBBY_CHAT, FRONT_END_CHAT_PLAYER_NAME#human-player, FRONT_END_CHAT_KICK_GENERAL#kick-ai-general, FRONT_END_CHAT_ADD_AI_GENERAL#add-ai-general, FRONT_END_CHAT_RANDOM_GENERAL#ai-generals, FRONT_END_CHAT_AI_PLAYER#start-positions, FRONT_END_CHAT_COLOUR#colour, FRONT_END_CHAT_TEAM#team, FRONT_END_CHAT_AI_CONSCRIPT#tiers, FRONT_END_CHAT_AI_VETERAN#tiers, FRONT_END_CHAT_AI_ELITE#tiers, FRONT_END_CHAT_RENAME#rename, FRONT_END_CHAT_RENAME_LABEL#rename, FRONT_END_CHAT_PORTRAIT#select-portrait, FRONT_END_CHAT_POINTS#start-positions]
order: 10
---

# Players tab

Who plays at each of the map's start positions: you, an AI General, or nobody. Each position also has a colour and a team.

![The Players tab with three AI Generals](shot:frontend-skirmish-players)

## Start positions

The list at the top left has one row per start position of the map, numbered as on the map:

- **Your position** shows your name (or "Human Player") in orange.
- **An AI General's position** shows the general's name, or "Random AI General" when the game picks one at the start. A chosen [Lua AI or the ministries](#ai) follow in brackets.
- **An empty position** shows a grey "&lt;AI General Slot&gt;": nobody plays there, and that start's buildings and units are removed when the game starts.
- **In Valhalla mode** each human's row also shows the points of the army they enlisted.

Click a row to select that position; the buttons below and the general list act on it. A new map puts you at the first position and Random AI Generals at all the others.

## Colour

The swatch in each row is that player's colour. A click on it takes the next of the eight colours. Every player needs a colour of their own, or Start stays grey.

## Team

The number in each row is the player's team: a click steps through 1, 2, 3, 4 and "-" (no team). Players of one team are allies and enemies of everyone else. With no teams chosen at all, the map's own alliances apply. A player without a team fights alone.

## Human Player

Makes the selected position yours. You can only hold one position: the one you held before becomes empty.

## Kick AI General

Empties the selected AI General's position.

## Add AI General

Puts a Random AI General at the selected position, if it is empty or yours.

## Tiers

The three bars above the list of generals choose which tier of AI General it shows: **Conscript** (the easiest), **Veteran** or **Elite** (the hardest).

## AI Generals

The generals of the chosen tier, by name, with **Random AI General** first. A click puts that general at the selected position (it becomes an AI position if it wasn't one). The row of the general already chosen there is highlighted. Your own generals from the [AI General Editor](../general-editor.md) are listed under their tier too.

## AI

When an AI position is selected, the **AI** button under the general list shows which AI plays it. A click takes the next one:

- **Kingmaker (default):** the general plays with the game's own AI scripts alone.
- **Ministries:** the [ministry AI](../../../mechanics/ai-opponents.md#the-ministry-ai): the general runs as a government of departments on top of the scripts.
- **Lua *name*** (wkbai comes with **Advanced**; mods can add more): the Kingmaker scripts still build the economy and decide which soldiers to train, but the script decides where and when the army attacks, defends and retreats.
- **Lua *name* + Ministries:** the Lua AI with the ministries under it, which it reads and steers.

Each AI position has its own choice, so one General can play with the ministries and another without. The player list shows it after the name: "[Ministries]", "[Advanced]" or "[Advanced+Min]". A Lua AI needs [Aggressive AI](settings.md#aggressive-ai) on.

## Portrait and story

The large panel shows the portrait, name and story of the general under the mouse in the list, or of the general at the selected position. With your own position selected, it shows your portrait (see Select Portrait).

## The map

The chosen map at the bottom right of the panel, with a flag at each start position in its player's colour and its number above it. Empty positions have a faded flag.

## Rename

Gives the selected position (yours or an AI General's) a name for this game. A box opens: type the name, then **OK** or Enter; **Cancel** or Esc keeps the old one. An empty name brings back the position's own name.

## Select Portrait

With your own position selected: steps through the portraits for you. It is shown in this lobby only, not in the game.

> wkbai: the original's lobby has the same positions, colours, teams, tiers, Rename and Select Portrait. The map with its start flags and the Lua AI are wkbai's own.
