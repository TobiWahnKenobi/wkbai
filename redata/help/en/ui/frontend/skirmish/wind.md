---
title: Wind tab
ids: [ui/frontend/skirmish/wind, ui/frontend/mphost/wind, ui/frontend/mpguest/wind, ui/frontend/skirmish/wind/wind#wind, ui/frontend/skirmish/wind/windFrom#from, ui/frontend/skirmish/wind/windStrength#strength, ui/frontend/skirmish/wind/windVeers#changes-direction, ui/frontend/skirmish/wind/windEffects#effects-on-the-game]
order: 35
---

# Wind

wkbai's own: a wind that blows over the whole map, the same for every player. The left half of the tab sets the wind; the right half lists what it does in the game.

![The Wind tab with the wind on](shot:frontend-skirmish-wind)

## Wind

On at first. The wind has a direction and a strength, and both change as the game goes on. The clouds, the rain and the snow, smoke and flames, the sea's waves, the grass, the banners and the trees all move with it. Off: everything looks as before, with a light, steady breeze, and the wind changes nothing in the game.

The settings below appear when it is on.

## From

Where the wind comes from. A click takes the next choice.

- **Random (mostly westerly)** (at first): chosen anew for each game, somewhere on the western side of the compass (from south-south-east round to north-north-west), like the westerly winds over Europe.
- **North**, **North-east**, **East**, **South-east**, **South**, **South-west**, **West**, **North-west**: from that side all game. North is the top of the minimap.

## Strength

How strong the wind blows. A click takes the next choice.

- **By the weather** (at first): the weather sets it. Calm in fog, light in a heatwave, a breeze in clear or cloudy weather, fresh in rain and droughts, strong in heavy rain, a gale in thunderstorms and blizzards. When the weather changes, the wind rises or falls to match over about 40 seconds, faster when a storm comes in. Without the seasons, the sky chosen on the [Seasons & Weather tab](weather.md#clouds) sets it.
- **Calm**, **Light**, **Breeze**, **Strong**, **Gale**: about that strength all game.

Either way it rises and falls a little as it goes, and it gusts.

## Changes direction

On at first. The wind veers slowly, up to 35 degrees either way of where it comes from, and faster in storms. Off: it keeps its direction all game.

## Effects on the game

On at first. Off: the wind is only seen. On, it does this:

- **Wildfires:** with the [seasons](weather.md#seasons-and-weather), in a heatwave or a drought, fires on the land break out more often in a wind. They spread faster and further, mostly downwind, burn hotter and burn out sooner, and set buildings alight more often. In a strong wind they throw embers well ahead of the flames.
- **Wind chill:** with the seasons, a wind makes the cold bite harder: about 1.5 degrees colder in a gentle breeze, 5 in a gale. Units in the open freeze sooner, most of all in a blizzard. In the heat it cools them a little.
- **Boats:** they sail faster with the wind behind them and slower into it ([ships](../../../mechanics/naval.md#sailing-and-the-wind)).

How the trees and the grass move in the wind is a graphics setting of your own: Options, Terrain and grass, [Trees in the wind](help:option/windTrees) and [Grass in the wind](help:option/grassWind). The [wind gauge](../../hud/top-bar.md#wind-gauge) shows the wind during the game.

> wkbai: the original game has no wind. With the wind off, the game plays as before. The campaign has no wind.
