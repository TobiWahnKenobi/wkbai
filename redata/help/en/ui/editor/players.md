---
title: Players
ids: [ui/editor/players, ui/editor/players/resources#starting-resources, ui/editor/players/diplomacy#diplomacy]
order: 50
---

# Players

The **Players** mode sets what each player of the map starts with and how the players stand towards each other. It lists every player but the Neutral Player, each under its number and kind ("1027 Human Player"). Click a player's name to open or close its settings.

The editor can't add or remove players: a map keeps the players of the map you started from. A player of the kind **Human Player** is a start position in the skirmish lobby.

## Starting resources

**Food**, **Wood**, **Gold** and **Stone**: what the player has at the start. The buttons step by 50; type a number for any other amount.

When your map is played as a skirmish, the lobby's [Start Food, Wood and Gold](../frontend/skirmish/settings.md#start-food-wood-and-gold) replace these.

## Diplomacy

For each other player, a list: **Allied**, **Neutral** or **Enemy**. A change applies both ways: if A is allied towards B, B is allied towards A.

In a skirmish, the [teams](../frontend/skirmish/players.md#team) chosen in the lobby decide who is allied. With no teams chosen at all, the map's own diplomacy, set here, applies.

These settings are saved with the map. They are not part of undo.
