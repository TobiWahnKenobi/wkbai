---
title: Fog of war and sight
ids: [mechanics/fog-of-war, mechanic/fog-of-war, mechanic/sight, mechanic/vision, mechanic/reveal, mechanic/clear-fog-of-war, mechanic/trees-in-the-fog#trees]
order: 120
---

# Fog of war and sight

You see only what your units and buildings see. Everything else on the map is darker, and enemy units there are hidden, on the map and on the minimap.

## What is hidden

- **Enemy units** (and their arrows and shots) outside your sight aren't shown.
- **Enemy buildings** are shown only once you have seen them, and then **as you last saw them**: a building destroyed out of your sight stays on your map until you look again. Neutral buildings that belong to the map are always shown.
- **Trees** are shown as you last saw them (below).
- **The ground** itself is always drawn, only darker.
- **Your allies' units** are always shown, but they don't light the map for you unless they share their sight (below).

## Trees

You know every tree on the map as it stands at the start. After that you see a tree change only where you, or an ally who shares sight with you, can see it:

- **Felled, used up or removed out of your sight:** still drawn standing, on the map and on the minimap, until you see its ground again.
- **New or grown again out of your sight:** not shown until you see it.
- A tree in the fog makes no sound and no effects when it falls.

So an enemy's woodcutters can clear a wood behind the fog without your map showing it. Your memory of the trees is kept in a save game. In multiplayer, a guest's memory stays on the guest's computer and isn't saved.

## How far a unit sees

- Each type has a **sight range** (a peasant 7, so 35 m; many soldiers more). It is counted in 5 m tiles, in all three directions.
- **Height.** Every metre a unit stands above the lowest point of the map adds 0.6 m to its sight. A scout on a hill 50 m up sees 30 m further than one in the valley. Buildings gain the same way.
- **Missile range is sight range.** An archer shoots as far as it sees, so high ground lengthens its range too ([combat](combat.md#missiles)).
- Some powers and buildings change sight: the Observatory's research, the Acts of God *Vision* and *Blindness*, and others listed on their pages.

## Allies

- With **Team Reconnaissance** on in the lobby, allies see what each other sees from the start.
- Otherwise a player shares its sight with its allies once it has the [Shared Vision](../upgrades/reconnaissance.md) research (at the Trading Post).

## Brief reveals

Some events show the enemy to you for a moment, even in the fog:

- a unit hit by a missile sees its shooter for about 3 seconds;
- artillery shelling you, and a priest's Act of God on you, are shown for 10 seconds around the caster;
- a spy's [Spy on Infantry](../commands/spy-on-infantry.md) reveals the enemy's troops for 10 seconds.

## Settings

- **Clear Fog Of War** (lobby) shows the whole map to everyone all game. It is off by default. The campaign always has the fog.
- **After the game ends** for you (won or lost) while others still play, the fog lifts and you watch everything. You can then only chat with other watchers.
- **Replays and spectators** see the whole map.

![The same valley seen from a scout on a hill and from one below it](shot:mech-fog-hill-scout)

> **wkbai.** Earlier versions of wkbai showed every tree in the fog as it really was. With wkbai's day and night on, sight shrinks at night away from light, and fog, rain and snow shorten it further: see [day and night](day-night.md#night-sight). The original's exact fog darkness wasn't read from its files; wkbai's is an estimate from screenshots.
