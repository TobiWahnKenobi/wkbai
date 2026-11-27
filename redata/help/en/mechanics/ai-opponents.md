---
title: AI opponents
ids: [mechanics/ai-opponents, mechanic/ai, mechanic/ai-generals, mechanic/ai-difficulty, mechanic/ai-tiers, mechanic/ministries#the-ministry-ai, mechanic/lua-ai, mechanic/ai-at-sea#the-ai-at-sea, mechanic/naval-ai#the-ai-at-sea, mechanic/colonies#the-ai-at-sea]
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
| **A Lua AI** | the players tab's AI button, for the selected AI position ([players](help:ui/frontend/skirmish/players/luaai)) | a Lua script plays that General (the bundled *Advanced*, or one from a mod); the war layer is off for it |
| **Ministries** | the same button, for the selected AI position; also with a Lua AI | the General becomes a government of departments under a strategist, on top of the scripts ([below](#the-ministry-ai)) |

- **Aggressive AI** (lobby setting, on by default): the Kingmaker AI builds its barracks and stables first, keeps a minimum of peasants, focuses its attacks on its nearest enemy, and shares farmers between its settlements. Off, the AI plays as the original's scripts alone, which is much more passive.
- **Weather and night:** wkbai's AIs shelter from the cold, store food for winter, fight fires and may wait for the night to attack ([seasons and weather](seasons-weather.md)).
- **At sea:** every AI uses ships where the map has water between it and its enemies ([below](#the-ai-at-sea)).
- **Archers and kiting:** an AI soldier gives up a chase it isn't gaining on, and AI armies leave out enemies their soldiers can't reach, so you can no longer lead them round in circles or pin them against water. An AI's own archers that fire as they fall back still count as free to defend its home.
- **Players who leave** a multiplayer game can be taken over by an AI General ([multiplayer](multiplayer.md)).

## The ministry AI

> **wkbai.** Chosen per AI position with the players tab's AI button ([players](help:ui/frontend/skirmish/players/luaai)).

The General becomes a government of departments (economy, labour, builders, research, war, intelligence, diplomacy, and at sea an admiralty) under a strategist, on top of the Kingmaker scripts. It acts only on what its units have seen.

- **War:** its war department sends its Generals on missions: raze a town, kill peasants, tie the enemy down. It defends its own towns and peasants first and calls up militia when attacked. An enemy army landing anywhere on its home island makes its guard bigger, and brings its missions home when the landing is stronger than what is at home. In a game of two, the opening keeps its new peasants at home until its home guard stands; with three or more players it doesn't hold them back.
- **Every unit its alignment has:** once its line soldiers stand, it also trains supports (priests, monks, druids, succubi), engines, monsters and spies. Its Sappers build Rocket Launchers and Trebuchets. Its units use their powers: Possess, Desecrate, the curses and storms, the summons, Trigger Explosives. Supports and spies go with its attacks on palaces and towns, the engines first. It builds the buildings and researches the research these units need ahead of its usual plan, so it fields them sooner, and in games of four it is stronger.
- **Night attacks:** with peasant lodging, when the peasants have gone indoors for the night (its own and so its enemies'), it sends no raids on peasants but attacks sooner and with a smaller lead.
- **Building:** it keeps its building sites about 275 m clear of an enemy's palace, and lays no village on land its peasants can't walk to.
- **Diplomacy:** it remembers. What it loses to a player leaves a grudge (twice as deep from a player it trusted), and tribute from a player leaves gratitude; both fade over some minutes. A deep grudge leads to war and draws its attacks; trust brings peace offers and alliances, and offers to it are accepted when it trusts you enough. A treacherous General may break an alliance with one it doesn't trust ([diplomacy](diplomacy.md)).

## The AI at sea

> **wkbai.** The original's AI never uses ships.

Every AI (the Kingmaker scripts, Aggressive AI, a Lua AI and the ministry AI) reads the map. Where water lies between it and its enemies, and the sea is the shorter way or the only one, it builds a [Dock](../buildings/dock-wood.md), trains [Cogs](../units/cog.md) and war boats, ferries its armies over, lands them at a safe shore, regroups and attacks. Its war boats fight enemy ships, go for enemy docks and shoot at the coast.

- **Colonies:** an AI may settle other islands for their resources. It either builds a Manor of its own on the island, where it is allowed to and the island holds enough to repay it, or opens a supply route by sea: the island village's cart to a dock, a Cog to the home dock, a cart to the Manor ([supply lines](../commands/set-up-oversea-supply-route.md)).
- **Settlers at sea:** they board a Cog that has stopped near them, and the Cog sails close in to the shore for those still waiting; if it can't, they walk to the shore by it. War boats sail with the settlers' Cog, and after settlers have been lost at an island, none go there again without two war boats.
- **A Castle sooner:** where an island is worth a Manor but the AI may not build one yet, it puts up the building that opens the Keep, then the one that opens the Castle, as the game's rules allow; the Castle's waits until there is gold for it.
- **Saved games** keep every AI's ships, ferries and colonies (and the ministry AI's missions at sea), so a loaded game goes on with them.
- **Island maps:** "3 Isles" and "Four Islands" still don't take AI Generals in the lobby, as in the original.

![The lobby's player list with three AI Generals of different tiers](shot:mech-ai-lobby)
