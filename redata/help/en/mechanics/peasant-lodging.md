---
title: Peasants at night and in winter
ids: [mechanics/peasant-lodging, mechanic/peasant-lodging, mechanic/peasants-sleep, mechanic/farmers-winter, mechanic/winter-crafts]
order: 35
---

# Peasants at night and in winter

> **wkbai.** All of this is wkbai's own. The original game has no day, night or seasons. It only happens when the lobby's *Seasons & Weather* tab turns it on ([the weather tab](../ui/frontend/skirmish/weather.md)).

Two switches decide it:

| Switch | Needs | Lobby default |
|---|---|---|
| **Peasants sleep at night** | day and night on | on |
| **Farmers winter indoors** | seasons on | on |

## Night

- **At dusk** (when daylight falls below half) every peasant walks to the nearest shelter of its own: a house, a village, the Manor, a Keep, Castle or Palace. A foundation doesn't count, nor a building below 10% of its strength. The departures are spread over half a minute.
- **On the way and inside** the peasant can't be selected or given orders. Enemies can still attack it on the way.
- **Builders, repairers, fire-fighters** and peasants on their way to become militia finish their job first, then go in.
- **At dawn** they come out and go back to the job they had: the same tree, mine, farm or shop, or the nearest one of its kind within 40 m if that is gone.
- **No gathering at night.** A peasant indoors brings in nothing.
- **Walks are hurried.** The way in and out is sped up (up to twice the speed) so that it takes at most a twentieth of the day.
- **No shelter:** a peasant whose player has none stops where it stands until one is built.
- **Shelter lost:** if its building drops below 10% or is destroyed, the peasant comes out and looks for the next shelter (or goes to work, by day).

## Winter

- **Farmers** go in at winter's first dusk and come out at spring's first dawn. Other peasants keep their normal day.
- **Support goes on.** A farmer indoors still counts towards your food support, scaled for the winter as every farmer is ([fields in winter](seasons-weather.md#fields-in-winter)).
- **Winter crafts.** Each farmer indoors adds **0.2%** to the wood and gold your peasants deliver, at most **5%** (25 farmers). Stone gets it too, as materials. Food doesn't. The top bar shows it, for example "Crafts +1.4%".

## What it means for you

- Your income stops at night. Gather by day what the night won't bring, and keep food stored for winter.
- A sleeping town's fields and mines are empty: night is a good time to raid, for you and for your enemies.
- Idle-peasant counts leave out peasants who are going in, inside or waiting.

![Peasants walking home to a village at dusk](shot:mech-peasants-dusk)
