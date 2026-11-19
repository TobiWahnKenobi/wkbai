---
title: Settings tab
ids: [ui/frontend/skirmish/settings, ui/frontend/mphost/settings, ui/frontend/skirmish/settings/preview#map-preview, ui/frontend/skirmish/settings/filter#players-filter, ui/frontend/skirmish/settings/maps#the-map-list, ui/frontend/skirmish/settings/startFood#start-food-wood-and-gold, ui/frontend/skirmish/settings/startWood#start-food-wood-and-gold, ui/frontend/skirmish/settings/startGold#start-food-wood-and-gold, ui/frontend/skirmish/settings/populationLimit#population-limit, ui/frontend/skirmish/settings/randomiseResources#randomise-resource, ui/frontend/skirmish/settings/resourcePoints#resource-points, ui/frontend/skirmish/settings/victoryPointLimit#victory-point-limit, ui/frontend/skirmish/settings/valhallaModeFood#valhalla-mode-food, ui/frontend/skirmish/settings/unitLives#unit-lives, ui/frontend/skirmish/settings/capturableFlags#capturable-flags, ui/frontend/skirmish/settings/mode#standard-mode-and-valhalla-mode, ui/frontend/skirmish/settings/teamPlacement#team-placement, ui/frontend/skirmish/settings/kingPiece#king-piece, ui/frontend/skirmish/settings/clearFogOfWar#clear-fog-of-war, ui/frontend/skirmish/settings/allowDiplomacy#allow-diplomacy, ui/frontend/skirmish/settings/teamReconnaissance#team-reconnaissance, ui/frontend/skirmish/settings/barbarianTribes#barbarian-tribes, ui/frontend/skirmish/settings/aggressiveAI#aggressive-ai, ui/frontend/skirmish/settings/recordGame#record-game, ui/frontend/skirmish/settings/aiMinistries#ministry-ai, FRONT_END_LOBBY_SETTINGS, FRONT_END_SETTINGS_MAP_ALL#players-filter, FRONT_END_SETTINGS_START_FOOD#start-food-wood-and-gold, FRONT_END_SETTINGS_START_WOOD#start-food-wood-and-gold, FRONT_END_SETTINGS_START_GOLD#start-food-wood-and-gold, FRONT_END_SETTINGS_POPULATION_LIMIT#population-limit, FRONT_END_SETTINGS_RESOURCE_LEVEL#randomise-resource, FRONT_END_SETTINGS_RESOURCE_POINTS#resource-points, FRONT_END_SETTINGS_VICTORY_TIME#victory-point-limit, FRONT_END_SETTINGS_VALHALLA_MODE_FOOD#valhalla-mode-food, FRONT_END_SETTINGS_UNIT_LIVES#unit-lives, FRONT_END_SETTINGS_CAPTURABLE_FLAGS#capturable-flags, FRONT_END_SETTINGS_GAME_TYPE_STANDARD_MODE#standard-mode-and-valhalla-mode, FRONT_END_SETTINGS_GAME_TYPE_VALHALLA_MODE#standard-mode-and-valhalla-mode, FRONT_END_SETTINGS_TEAM_PLACEMENT#team-placement, FRONT_END_SETTINGS_GAME_TYPE_KING_PIECE#king-piece, FRONT_END_SETTINGS_FOG_OF_WAR#clear-fog-of-war, FRONT_END_SETTINGS_ALLOW_DIPLOMACY#allow-diplomacy, FRONT_END_SETTINGS_TEAM_RECONNAISSANCE#team-reconnaissance, FRONT_END_SETTINGS_BARBARIAN_TRIBES#barbarian-tribes]
order: 20
---

# Settings tab

The map and the rules of the game. The values here start as the original game's defaults, except that Clear Fog Of War is off.

![The Settings tab in Standard mode](shot:frontend-skirmish-settings)

## Map preview

The chosen map's picture, at the top left. A map without a picture shows its name instead.

## Players filter

**All** lists every map; the round buttons **2** to **8** list only the maps for that many players. The chosen map stays selected when it is still in the filtered list.

## The map list

The maps to play, as "4 Peaks (2)": the name, then the number of start positions. The skirmish maps come first, then the Kingmaker campaign's provinces as skirmish maps, then maps saved with the [map editor](../map-editor.md) (only with All; a map named "N Player ..." is listed with the skirmish maps, see [Name it for Skirmish](../../editor/saving.md#name-it-for-skirmish)). Click one to choose it; the [Players tab](players.md) sets up its positions again.

## Start Food, Wood and Gold

How much of each resource every player starts with (Standard mode only). 0 to 100,000; at first 500 food, 500 wood and 200 gold. **-** and **+** step by 100 (gold by 50); hold a button to step faster, hold Shift to step ten times as far.

## Population Limit

The most people (peasants and soldiers) each player may have: 10 to 10,000, at first 200, in steps of 10 (100 with Shift). Standard mode only.

## Randomise Resource

Whether the map's stone and gold stay where the map maker put them. **Off:** they do. **Low**, **Medium** (at first) or **High:** they are removed and placed again at random, near each Manor and over the map, in that amount. A click takes the next.

## Standard mode and Valhalla mode

The two kinds of game:

- **STANDARD MODE:** each player starts with a town and builds up an economy and an army.
- **VALHALLA MODE:** each player starts with a Manor and an army bought with Resource Points on the [Army Enlistment tab](army.md). Capturing and holding flags scores victory points; the first player to reach the limit wins, and a player whose units are all gone loses. The resource steppers make way for Valhalla's own five.

## Resource Points

Valhalla only: the points each player has to enlist an army with. 0 to 100,000, at first 2,500, in steps of 250.

## Victory Point Limit

Valhalla only: the victory points that win the game. Each held flag adds to its holder's points every second. 1,000 to 1,000,000, at first 10,000, in steps of 1,000.

## Valhalla Mode Food

Valhalla only: the food each player has to keep the army fed. 0 to 1,000,000, at first 20,000, in steps of 1,000.

## Unit Lives

Valhalla only: how often a fallen unit comes back. 0 is **Infinite**, the default; otherwise 1 to 100.

## Capturable Flags

Valhalla only: how many flags are placed on the map to fight over. 0 to 20, at first 4.

## Team Placement

With teams chosen on the Players tab, team-mates start at neighbouring positions round the map instead of where the list put them. Each player keeps their colour. Off at first.

## King Piece

Standard mode only. Each player gets a king in front of their Manor, with a banner; if your king dies, you lose. Off at first.

## Clear Fog Of War

On: the whole map is visible to everyone all game. Off (wkbai's default): you see only what your units and buildings see, and what you have explored stays dimmed. See [fog of war](help:mechanic/fog-of-war).

## Allow Diplomacy

On, with no teams chosen: players can change their relations during the game (offer alliances, make peace, declare war). With teams chosen, the teams stay as they are all game either way, and team-mates can send each other tribute. See [diplomacy](help:mechanic/diplomacy).

## Team Reconnaissance

On: every player starts with the research that lets allies see what each other's units see.

## Barbarian Tribes

On: one or two barbarian huts appear at random places that can be reached from a Manor. The barbarians are a player of their own, hostile to everyone.

## Aggressive AI

wkbai's own, on at first: the AI Generals keep fewer peasants and attack whenever they can afford an army. Off, they play as the original's scripts alone make them. A [Lua AI](players.md#lua-ai) needs it on.

## Record game

wkbai's own, off at first: the game is recorded to be watched again from [Load Game > Replays](../load.md#replays). A recording holds only the setup and the orders given, so it stays small.

## Ministry AI

wkbai's own, off at first: every AI General runs as a government of departments (war, economy, intelligence and so on) that plan and carry out missions, on top of the Kingmaker scripts. It acts only on what its units have seen. See [the AI](help:mechanic/ai).

## In multiplayer

The host's lobby has more settings on this tab, see [the host's lobby](../multiplayer/host.md#the-hosts-settings): Lock Server, AI takes over players who leave, Allow spectators, and the rules for guests' mods, builds and agent players.

> wkbai: the original's switches and steppers are all here, with the same defaults except Clear Fog Of War, which wkbai leaves off.
