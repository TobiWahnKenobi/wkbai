---
title: Seasons & Weather tab
ids: [ui/frontend/skirmish/weather, ui/frontend/mphost/weather, ui/frontend/skirmish/weather/dayNight#day-and-night, ui/frontend/skirmish/weather/dayLength#day-min, ui/frontend/skirmish/weather/startHour#start-hour, ui/frontend/skirmish/weather/moonPhase#moon, ui/frontend/skirmish/weather/seasons#seasons-and-weather, ui/frontend/skirmish/weather/seasonLength#season-min, ui/frontend/skirmish/weather/startSeason#first-season, ui/frontend/skirmish/weather/tempScale#temperature, ui/frontend/skirmish/weather/weatherStability#weather-changes, ui/frontend/skirmish/weather/weatherMinChange#weather-changes, ui/frontend/skirmish/weather/weatherMaxChange#weather-changes, ui/frontend/skirmish/weather/weatherTypes#weather-that-may-come, ui/frontend/skirmish/weather/cloudWeather#clouds, ui/frontend/skirmish/weather/cloudCover#cover, ui/frontend/skirmish/weather/weatherWinter#each-seasons-weather, ui/frontend/skirmish/weather/weatherSpring#each-seasons-weather, ui/frontend/skirmish/weather/weatherSummer#each-seasons-weather, ui/frontend/skirmish/weather/weatherAutumn#each-seasons-weather, ui/frontend/skirmish/weather/peasantsSleep#peasants-sleep-at-night, ui/frontend/skirmish/weather/farmersWinter#farmers-winter-indoors, ui/frontend/skirmish/weather/riverCurrents#river-currents, ui/frontend/skirmish/weather/cheatMode#cheat-mode, ui/frontend/skirmish/seasons-weather]
order: 30
---

# Seasons & Weather tab

wkbai's own: a day that passes, seasons that change, and weather that matters. All of it is off at first, so a game plays as in the original until you switch it on. The left half sets the day and the seasons, the right half the weather of each season.

![The Seasons & Weather tab with both switched on](shot:frontend-skirmish-weather)

## Day and night

On: the sun crosses the sky and its shadows follow it; at night it is dark. At night, sight ranges and the fog of war close in, except near lit buildings and fires. The settings below appear when it is on.

## Day (min)

How long a whole day and night lasts, in minutes of play: 2 to 240, at first 20.

## Start hour

The hour the game starts at, 0 to 23; at first 8 in the morning.

## Moon

The moon's phase, which sets how bright the nights are and how much the moonlight casts shadows. **Natural** (at first) starts with the moon as it was in the year 1000 on the first day of the starting season and lets it wax and wane day by day; the other eight choices (New moon to Waning crescent) keep one phase all game. A click takes the next.

## Seasons and weather

On: the year turns through spring, summer, autumn and winter, with their temperatures and weather: rain, storms, fog, snow, heatwaves and droughts. The weather has effects on the game:

- Units in the open suffer in the cold (marching through snow is worse) and can die of thirst in great heat. They are safe in their own or their allies' villages and manors.
- Wet and snowy ground slows units down.
- Lightning strikes units and sets buildings on fire. Rain puts burning buildings out, and snow on a roof slows a fire.
- Rivers rise in spring and freeze below zero in winter; floods damage buildings and drown units in deep water.

See [seasons and weather](help:mechanic/seasons-weather) for the rules. The wind has a tab of its own ([Wind](wind.md)).

## Season (min)

How long each season lasts, in minutes: up to 600, at first 30. With day and night on, a season lasts at least one day.

## First season

The season the game starts in: **Spring** (at first), **Summer**, **Autumn** or **Winter**.

## Temperature

The scale temperatures are shown in during the game: **Celsius** (at first), **Fahrenheit** or **Kelvin**.

## Weather changes

How often the weather changes:

- **Frequent:** a new weather every 1 to 4 minutes.
- **Normal:** every 3 to 10 minutes.
- **Seldom:** every 8 to 20 minutes.
- **Random** (at first): each weather lasts 10 to 35 % of a season, between 45 seconds and 10 minutes.
- **Custom:** you set the shortest and the longest time between changes, **Min. (min)** and **Max. (min)**, 1 to 120 minutes each (at first 3 and 10). The shortest can't be longer than the longest. Custom also lets you choose which weathers may come at all (below).

## Weather that may come

With Custom only: a switch for each of the ten weathers (Clear, Cloudy, Rain, Heavy rain, Thunderstorm, Fog, Snow, Blizzard, Heatwave, Drought). The seasons still decide when each can come: snow only in winter, heatwaves only in summer, and so on. A season none of the chosen weathers can come in stays clear, and the tab says which.

## Clouds

With the seasons off: the sky's clouds, which stay as chosen all game. **By the cloud cover** (at first) shows fair-weather clouds over as much of the sky as **Cover** says; the other choices show one weather's sky (Clear, Cloudy, Rain and so on). This is the look of the sky only; rain and snow and their effects come only with the seasons.

## Cover

With Clouds set to By the cloud cover: how much of the sky is covered, 0 to 100 %, at first 45 %.

## Each season's weather

With the seasons on, a button for each season: how harsh its weather is.

- **Gentle:** milder seasons, rare storms.
- **Normal.**
- **Harsh:** colder winters, hotter summers, more storms, floods and droughts.
- **Random** (at first): chosen anew every year.

Under each season the tab lists what it brings: winter snow, blizzards and frost; spring rain, storms and rising rivers; summer heatwaves, droughts and thunderstorms; autumn rain, heavy rain and fog.

## Peasants sleep at night

With day and night on; on at first. Peasants go home to their houses, villages, manors and castles at night and come out again in the morning.

## Farmers winter indoors

With the seasons on; on at first. Farmers spend the winter indoors, when nothing grows.

## River currents

With the seasons on; on at first. Moving water carries the units in it downstream and, where it is deep or fast, hurts them. They swim to the shore. Rain and floods make the water faster. See [water, currents and swimming](help:mechanic/river-currents).

## Cheat mode

Skirmish only, never in multiplayer; off at first. On: **F9** in the game opens a panel to set the time of day, the season and the weather at will. Also turns on the other [cheat keys](help:key/serverWindow).

> wkbai: none of this is in the original game, where the light and the weather never change. With everything here off, wkbai plays like the original.
