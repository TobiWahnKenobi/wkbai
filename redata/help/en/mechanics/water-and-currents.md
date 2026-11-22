---
title: River currents and swimming
ids: [mechanics/water-and-currents, mechanic/river-currents, mechanic/currents, mechanic/water, mechanic/swimming#swimming, mechanic/swim#swimming, mechanic/waterway-speed#how-fast-the-water-flows]
order: 105
---

# River currents and swimming

> **wkbai.** River currents are wkbai's own; in the original, water only stops units or lets them wade. They come with the seasons: the lobby's **River currents** switch on the [Seasons & Weather tab](../ui/frontend/skirmish/weather.md#river-currents) (on at first) appears once Seasons and weather is on. The host decides, and the game works them out the same for every player, whatever their graphics settings.

Moving water pushes the units standing in it. Where it is deep or fast it hurts them, and they swim to the shore. The water's flow is the one you see: rivers run along their course, faster in rain and floods. Frozen water has no current.

## How deep the water is

- **Shallow:** up to 0.75 m. Units walk on through it; only fast water pushes them.
- **Normal:** 0.75 to 1.5 m, as deep as land units can go.
- **Flood:** all the water, and the flooded land, while the rivers have risen 0.3 m or more ([floods](seasons-weather.md#floods)).

## How fast the water flows

- **Slow:** most fordable water in fair weather.
- **Normal:** the same water in rain and heavy rain, which speed it up, the more as it rises.
- **Fast:** the fastest stretches of a river in rain and in floods.

A map's maker can also set a river's speed (see [waterway speeds](../ui/editor/water.md#waterway-speeds)); the weather then speeds it up the same way.

## What it does

Once a second, each unit in moving water is pushed along the flow and may lose some of its hit points:

| Water | Slow | Normal | Fast |
|---|---|---|---|
| Shallow | nothing | nothing | pushed 0.4 m a second |
| Normal | pushed 0.5 m/s | pushed 1 m/s, 0.5 % a second | pushed 1.8 m/s, 1.5 % a second |
| Flood | pushed 0.6 m/s, 3 % a second | pushed 1.2 m/s, 3 % a second | pushed 2 m/s, 3 % a second |

- 0.5 % a second kills a unit in about 3 minutes, 1.5 % in about a minute, 3 % in about 30 seconds. The undead lose twice as much.
- Flooded land deep enough to drown units still drowns them as before ([floods](seasons-weather.md#floods)); the current adds nothing on top.

## Swimming

- **Units swim out.** A unit carried by the water swims at half its speed, together with the current: on towards where it was going, or to the nearest shore if it had no order. Once on dry land it carries on.
- **In shallow water** units aren't carried off their feet; they walk on.
- **Formations, boats and buildings** aren't carried. Members of a formation are still hurt.
- **Swimmers are drawn at the water's surface** with a swimming stroke: the body tipped forward with the head up, the legs kicking and the arms pulling. Robed units tip less; mounted units swim level, the horse's legs paddling; the Gibbering Horde sinks to its heads. wkbai makes these strokes from each unit's own walk (the Scout has its own in the game's files, and a mod's or an HD model's own swim is used where there is one).

## Who fares differently

- **War machines** (rams, bombards, trebuchets, mangonels, siege towers, carts, the Infernal Engine and the like) can't swim. Where the water is deep or flooded enough to carry them, they sink, losing 15 % a second, dragged along at a third of the current as they go under. In shallow water they are only dragged a little.
- **Huge creatures** (the Behemoth, the large Elementals, the Horde Spewer) drift with the current but can't swim out, and are hurt like anyone.
- **The Archangel Michael and Abaddon** walk the river bed untouched: the water neither carries nor hurts them. They also no longer drown in floods or fall through thawing ice, whether the currents are on or off.

## Paths and the AI

- **Paths** go round the dangerous water when they can: a land unit's way avoids water that would carry or hurt it, the more the more dangerous it is.
- **The AI Generals** walk their units out of water that harms them, and war machines out of water that would carry them, to the nearest dry land.

## Settings

- **River currents** (lobby): with it off, or without the seasons, none of this happens.
- **Swim strokes:** `"madeSwimAnimations": false` in `wkconfig.json` ([settings without a menu](../options/config-file.md#madeswimanimations)) keeps wkbai from making the strokes; a swimmer then plays its walk, slowed down.
