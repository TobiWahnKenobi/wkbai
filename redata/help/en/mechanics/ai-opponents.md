---
title: AI opponents
ids: [mechanics/ai-opponents, mechanic/ai, mechanic/ai-generals, mechanic/ai-difficulty, mechanic/ai-tiers, mechanic/ministries, mechanic/lua-ai]
order: 180
---

# AI opponents

Computer players are **AI Generals**: named lords, each with a portrait, a story and a personality. You pick them in the lobby, or let the game pick at random ([the skirmish lobby](help:ui/frontend/skirmish)).

## Tiers

Each General has a tier, which mainly sets how large an army and town it may have:

| Tier | Population limit |
|---|---|
| **Conscript** | half the human player's |
| **Veteran** | three quarters of the human player's |
| **Elite** | the full limit |

## Personality

A General's personality is a set of stats, each from 0 to 100. The [AI General Editor](help:ui/frontend/generaleditor) makes Generals of your own with them:

- **War:** economic or military, raiding or siege, its preferred path (Pagan, Renaissance or Imperial), infantry or cavalry, heavy or light troops, leadership, fervour.
- **Diplomacy:** fawning, neutral or arrogant in its messages, how often it talks and makes offers, greed, treachery, pride, friendliness, chattiness ([diplomacy](diplomacy.md)).

## How the AI plays

The AI of *Warrior Kings: Battles* is mostly game-set script, the "Kingmaker" AI, which wkbai runs as it is:

- it lays out towns and villages, sends peasants to work and builds along its chosen path;
- it trains the best unit it can for each role, falling back to cheaper ones when it lacks the building, the research or the money;
- it musters armies to defend, raid and besiege, garrisons towers and calls up militia when attacked;
- it uses priests' powers, spies and diplomacy as its personality says.

## wkbai's AI choices

> **wkbai.** Besides the original scripts, wkbai offers more ways for an AI General to play:

| Choice | Where | What runs |
|---|---|---|
| **The Kingmaker scripts** (default) | | the original's scripts, with wkbai's war layer: it defends its palace, towns and peasants, sends attack waves sized to the population limit, strikes back and retreats when outmatched |
| **A Lua AI** | the players tab, for the selected AI position ([players](help:ui/frontend/skirmish/players)) | a Lua script plays that General (the bundled *Advanced*, or one from a mod); the war layer is off for it |
| **Ministry AI** | the settings tab, for every AI General ([settings](help:ui/frontend/skirmish/settings/aiMinistries)) | each General becomes a government of departments (economy, labour, builders, research, war, intelligence, diplomacy) under a strategist, on top of the scripts. Its war department sends Generals on missions: raze a town, kill peasants, tie the enemy down. It acts only on what its units have seen. |

- **Aggressive AI** (lobby setting, on by default): the Kingmaker AI builds its barracks and stables first, keeps a minimum of peasants, focuses its attacks on its nearest enemy, and shares farmers between its settlements. Off, the AI plays as the original's scripts alone, which is much more passive.
- **Weather and night:** wkbai's AIs shelter from the cold, store food for winter, fight fires and may wait for the night to attack ([seasons and weather](seasons-weather.md)).
- **Islands:** the AI can't use ships, so maps where players start on separate islands don't allow AI Generals, and docks are only offered without them ([ships](naval.md)).
- **Players who leave** a multiplayer game can be taken over by an AI General ([multiplayer](multiplayer.md)).

![The lobby's player list with three AI Generals of different tiers](shot:mech-ai-lobby)
