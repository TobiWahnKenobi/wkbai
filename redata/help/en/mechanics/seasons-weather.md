---
title: Seasons and weather
ids: [mechanics/seasons-weather, mechanic/seasons-weather, mechanic/seasons, mechanic/weather, mechanic/temperature, mechanic/cold, mechanic/snow, mechanic/rain, mechanic/floods, mechanic/ice, mechanic/ice-holes#breaking-the-ice, mechanic/breaking-ice#breaking-the-ice, mechanic/lightning, mechanic/wildfires, mechanic/winter, mechanic/wind#wind, mechanic/wind-chill#wind, mechanic/rain-on-fires#rain-and-snow-on-burning-buildings, mechanic/building-fires-weather#rain-and-snow-on-burning-buildings]
order: 140
---

# Seasons and weather

> **wkbai.** Seasons, temperature and weather are wkbai's own; the original game has none of them. They are off unless the lobby's *Seasons & Weather* tab turns them on ([the weather tab](../ui/frontend/skirmish/weather.md)). With them off, a game plays exactly as the original. The campaign never has them.

## The year

- **Season length:** set in the lobby (30 minutes by default). With day and night on too, a season lasts at least one day.
- **Start season:** spring, summer, autumn or winter.
- **Severity per season:** gentle, normal, harsh, or random (chosen again each year; the default).
- **Temperature** follows the season (about 12 °C in mid-spring, 24 °C in summer, 11 °C in autumn, −3 °C in winter), swings over the day, and is pulled by the weather. Severity widens it: a harsh winter averages about −9 °C, a harsh summer's heatwave reaches 40 °C. The HUD shows it in °C, °F or K as chosen.

## Weather

Each season has its own mix: spring clear, cloudy and rainy in equal parts, with some storms and fog; summer mostly clear, with thunderstorms, heatwaves and droughts; autumn cloud, rain and fog, often heavy rain; winter mostly snow, with cloud, fog and some blizzards. Harsh seasons bring the rough weather more often, gentle ones less.

- **Rain or snow** depends on the temperature: snow below about 0.5 °C, rain above 2.5 °C, sleet between.
- **How often it changes** (the lobby's *Weather changes*): frequent (1 to 4 minutes), normal (3 to 10), seldom (8 to 20), random (a share of the season; the default), or custom, with your own times and the kinds of weather allowed. A season with none of the allowed kinds stays clear.
- **The ground** reacts: it gets wet in rain and dries in sun and heat; snow settles below 1 °C and melts above; water freezes below 0 °C; rivers and lakes rise in rain (up to 1.5 m in heavy rain) and sink in heat and drought.

## Effects on play

- **Slow going.** Wet and snowy ground slows everyone on foot and on horseback, down to 0.55 of their speed in deep snow. Boats aren't slowed.
- **Cold.** Below about 2 °C (felt colder in a blizzard, in snow, on wet ground or in the [wind](#wind)) units grow cold, faster while marching. Once they are frozen through, they lose hit points: slowly at −8 °C, faster when colder (about two minutes to death at −12 °C).
- **Heat.** Above 30 °C (33 °C in a drought) units suffer the same way, losing hit points above about 34 °C.
- **Shelter** from cold and heat: within 45 m of your own or an ally's finished Manor, Keep, Castle, Palace or Village, or inside a building. Carts, siege engines, boats and elementals don't feel the weather.
- **Night sight** is shorter in fog, rain and snow ([day and night](day-night.md#night-sight)).

### Fields in winter

- **Fields lie fallow.** Farmers work but bring in nothing all winter.
- **Food support** per farmer is cut: to a half in a gentle winter, a quarter in a normal one, nothing in a harsh one ([food support](population-and-food.md#the-food-support)). Your army then eats from your store.
- **Farmers can winter indoors**, and give a small bonus to wood and gold while they do ([peasants at night and in winter](peasant-lodging.md#winter)).

### Thunderstorms

A bolt strikes every 5 to 20 seconds (less often when gentle, more when harsh). It may hit a house or tower, a wall piece, a unit, or the bare ground. A building struck loses 6% of its strength and catches fire; a unit struck loses 400 hit points, and those within 12 m are hurt. The storm's rain then fights the fire ([below](#rain-and-snow-on-burning-buildings)).

### Floods

Heavy rain raises the rivers and lakes. Low land near water (within 50 m and less than 4 m above it) can flood:
- buildings standing in the flood slowly lose strength, more the deeper the water (up to 2 m);
- units in more than 1 m of flood water drown, 5% of their hit points a second.

With [river currents](water-and-currents.md) on, the flood water also carries units away and hurts them in shallower water.

On the shipped maps towns stand above the floods; they matter mostly on shores and on custom maps.

### Ice

In a hard frost rivers and lakes freeze. Once the ice is thick, land units can walk across water of any depth and boats are held fast. When it thaws, anyone still on deep water falls through and drowns quickly.

### Breaking the ice

> **wkbai.** On by default in a skirmish with the seasons; the lobby has no switch for it.

Artillery shots that land on the ice, or hit a unit standing on it, break a hole into it. Its radius, from the middle to the edge: a [Trebuchet](../units/trebuchet.md)'s ball 5.5 m (about 11 m across), a [Bombard](../units/bombard.md)'s 4 m, a [Mangonel](../units/mangonel.md)'s 3.5 m (also a Mangonel Boat's or a ship's), a [Rocket Launcher](../units/rocket-launcher.md)'s rocket 2.5 m. Arrows, bullets, javelins and fire balls break nothing, nor does a shot that hits more than 4 m above the ice (a unit on a tower).

- **Falling in:** every unit over deep water in the hole (deeper than 1.5 m) falls in and loses a tenth of its health at once, then 6 % a second while it stays in the icy water (the undead 12 %). War machines lose 25 % a second and sink. In shallow water inside a hole units just wade.
- **Swimming out:** a unit that can swim swims at half its speed to the nearest ice or shore it can stand on. Units that can't swim drown where they are, and the huge creatures can't swim out. Members of a formation are hurt but stay with their formation. The Archangel Michael and Abaddon walk the bed unharmed.
- **Drowned:** a unit that dies in the hole counts as the shooter's kill, if their players are enemies, and as drowned in the statistics.
- **Holes on the map:** walkers go round them, and a boat in a hole is free to move.
- **Freezing over:** while it freezes, a hole closes from its edges, faster the harder the frost: a Bombard's hole in about a minute and a half at -10 °C, about 13 minutes at -1 °C. Above 0 °C it stays open; when the ice thaws away, its holes go with it. A shot into a hole that is freezing over breaks the new ice again.
- **Drawn** as broken ice: the edge runs along the ice's plates, the plates round it are sunk and tilted with open cracks between them, and floes drift in the dark water ([ice quality](../options/graphics/sky-weather.md#ice-quality)). Once a hole has closed, its new ice is drawn darker and thinner for a while. Saved games keep the holes.

### Wildfires

In a heatwave or drought fires can start on dry land, from burning buildings and from flaming arrows, Greek fire and fire trebuchets coming down. Fire spreads over grass, fields and forest floor, not over rock, sand, snow, roads or water. Units within 4.5 m of the flames lose 4% of their hit points a second; buildings beside them may catch fire. Burnt trees stay charred; their wood is still there.

In the [wind](#wind) wildfires are worse: they break out more often, spread faster and further, mostly downwind, burn hotter and out sooner, and set buildings alight more often. In a strong wind they throw embers ahead of the fire.

### Rain and snow on burning buildings

- **Rain puts burning buildings out.** The fire dies down by itself in the rain: a fully burning building is out after about 2.5 minutes of rain, or about a minute of heavy rain. A thunderstorm's rain is in between. Its flames grow smaller as it dies down.
- **Snow on a roof slows a fire.** A building with snow on its roof burns at most half as fiercely with a full cover, and catches fire from its neighbours less often (a house about 1 time in 20 instead of 1 in 5). The fire melts the snow on its own roof, and falling snow covers it again.

## Wind

With the lobby's [Wind](../ui/frontend/skirmish/wind.md) on (on at first), a wind blows over the map, the same for every player. It works with or without the seasons, but most of its effects need them.

- **Its direction** is set in the lobby, or chosen at random, mostly from the west. It veers slowly, up to 35 degrees either way, faster in storms, unless the lobby holds it steady.
- **Its strength** follows the weather, or is fixed in the lobby. About: fog 1 m/s, heatwave 3, clear 3.5, snow 5, cloudy 5.5, rain 7, drought 7.5, heavy rain 10, thunderstorm 14, blizzard 17. It swings slowly up and down around that, and gusts.
- **Wildfires** spread with it (above).
- **Wind chill:** the wind makes the cold bite harder, about 1.5 °C colder at 5 m/s, 3 °C at 11 m/s and almost 5 °C at 17 m/s. Units in the open freeze sooner, most of all in a blizzard. In the heat a wind cools them a little. The AI Generals feel the same.
- **Boats** sail faster before the wind and slower into it, also without the seasons ([ships](naval.md#sailing-and-the-wind)).
- **The look:** clouds, rain and snow, smoke and flames, the sea's waves, banners, trees and grass all move with it. The [wind gauge](../ui/hud/top-bar.md#wind-gauge) shows it.

With **Effects on the game** off in the lobby, the wind is only seen.


### The undead

The Undead Legion and the Gibbering Horde feel no cold or heat, but mud drags at them much more than at the living (down to a quarter of their speed), and water drowns them twice as fast.

![A town in a harsh winter: snow on the roofs, fields fallow, the river frozen](shot:mech-weather-winter)

![Rain over a flooding river, the top bar showing the temperature and weather](shot:mech-weather-flood)

> **wkbai.** The AI Generals know the weather: they shelter from the cold, keep food back for winter, send peasants to put out lightning fires, pull units out of floods and off thawing ice.
