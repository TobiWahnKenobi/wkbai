---
title: Diplomacy
ids: [mechanics/diplomacy, mechanic/diplomacy, mechanic/alliances, mechanic/tributes, mechanic/teams]
order: 150
---

# Diplomacy

## The three relations

Between any two players there is one relation, the same both ways:

| Relation | What it means |
|---|---|
| **Allied** | Your units never attack each other and can guard each other. Allies' units are shown to you even in the fog. Allied churches, windmills and the like serve your peasants too. |
| **Neutral** (at peace) | Units leave each other alone, but nothing is shared. |
| **Enemy** | Units fight on sight. Every player starts as everyone's enemy unless teams say otherwise. |

## Changing relations

What the lobby chose decides whether relations can change during the game ([the lobby's settings](../ui/frontend/skirmish/settings.md)):

- **Allow Diplomacy on, no teams:** every player may change its relations.
- **Teams chosen:** team-mates are allies and everyone else an enemy, all game.
- **Neither:** everyone is everyone's enemy, all game.

When changes are allowed:

- **A worse relation is yours to set at once:** declare war on an ally or a neutral, or end an alliance.
- **A better one is an offer.** The other player sees it and may accept; accepting makes it the relation for both. Offers don't expire. An offer is withdrawn when you cancel it or set a worse relation instead.
- There is **no surrender**.

## The windows

The top bar has three buttons ([the diplomacy windows](../ui/hud/diplomacy-window.md)):

- **Diplomacy:** your relation with each player, offers to make, accept or cancel. It is lit only when changes are allowed.
- **Global Diplomacy:** everyone's relations with everyone. It needs a [Tavern](../buildings/tavern-wood.md).
- **Tributes:** give food, materials or gold to another player. It needs a [Trading Post](../buildings/trading-post.md), and diplomacy allowed or teams chosen.

## Tributes

- Pick amounts per player and good: a click adds 10, Shift+click 50, a right-click takes 10 away. You can't give more than you have.
- **Send** delivers it all at once, straight into their stock. They get a notice.
- AI Generals may ask you for tribute, sometimes with a threat.

## AI Generals and diplomacy

- AI Generals make and answer offers among themselves and with you, by their personality: some fawning, some plain, some arrogant. Their messages appear in a window with their portrait.
- Their editor settings (greed, treachery, pride, friendliness, chattiness) steer how they behave ([AI opponents](ai-opponents.md)).

![The Diplomacy window with an alliance offered by an AI General](shot:mech-diplomacy-offer)

> **wkbai.** All three windows, offers, tributes and the AI's messages work as in the original. wkbai shows a notice to the other side whenever a script changes a relation; the original only refreshed its window.
