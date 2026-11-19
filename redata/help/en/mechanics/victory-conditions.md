---
title: Victory and defeat
ids: [mechanics/victory-conditions, mechanic/victory, mechanic/defeat, mechanic/game-modes, mechanic/standard-mode, mechanic/king-piece, mechanic/valhalla, mechanic/king-of-the-hill]
order: 190
---

# Victory and defeat

## Standard mode

- **You lose** when your last Manor, Keep, Castle or Palace is destroyed. Everything else you own then falls: your units are gone and your buildings collapse. You also lose if you have nothing left at all.
- **You win** when you are the last player standing, or when everyone left is your ally: allies win together.
- Losing players are told with a message; you may stay and watch the rest of the game, with the fog lifted ([fog of war](fog-of-war.md#settings)), or leave.

### King Piece

With **King Piece** on in the lobby, each player also gets a **king**, who stands before the Manor at the start. He looks like his side's lord and changes with your [alignment](alignment.md).

- **You lose** when your king is killed, or when the Manor-type building he has made his seat is destroyed. He changes his seat by going into another of your headquarters.
- Keep him safe: garrison him, guard him, don't send him to war.

## Valhalla mode

A battle without an economy: armies bought before the game fight for flags.

- **The army:** in the lobby's *Army Enlistment* tab each player spends **Resource Points** (2500 by default) on companies of his alignment ([Army Enlistment](help:ui/frontend/skirmish/army)). At the start everything but your Manor is removed and your companies muster round it.
- **Food:** you start with *Valhalla Mode Food* (20,000 by default) for your army's upkeep; there are no farms to make more.
- **Lives:** with *Unit Lives* set, a slain soldier comes back as a reinforcement at your Manor's muster points while he has lives left. *Infinite* (the default) brings everyone back.
- **Flags:** the map has capturable flags (4 by default). [Capture](../commands/capture.md) one by bringing a unit next to it. Each flag you hold adds its victory points to your score every second.
- **You win** by reaching the **Victory Point Limit** (10,000 by default), or by being the last army standing (with your allies).

The flag rules are the game set's "King of the Hill" scripts.

> **wkbai.** The shipped script told the player who reached the Victory Point Limit that it had lost, and the loser that it had won. wkbai's patch to it gives the win to the right player.

## The campaign

Each province of the [Kingmaker campaign](campaign.md) has its own goals, given at the start and in the mission objectives (O). Winning conquers the province; losing sends you back to the main menu with nothing gained.

![The victory message after the last enemy Manor fell](shot:mech-victory-message)
