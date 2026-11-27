---
title: Sun and latitude
ids: [ui/editor/latitude, ui/editor/latitude/slider#the-latitude, ui/editor/latitude/days#the-three-days, ui/editor/latitude/preview#preview-the-day-here]
order: 85
---

# Sun and latitude

The **Sun and latitude** section sets how far north or south of the equator your map lies. It is shown in every mode of the [map editor](index.md), above the map's name.

The latitude drives the sun's path and the length of the day by the season, in games with [day and night](../../mechanics/day-night.md) or [the seasons](../../mechanics/seasons-weather.md). The further from the equator, the lower the sun and the longer the summer days and winter nights. It also sets how high the night sky's pole stands.

## The latitude

- **Latitude:** the slider, from 75° S to 75° N.
- **Britain:** sets 53° N. A map that has no latitude of its own opens at Britain's.
- **Like...:** a list of places to start from: the Equator, the Sahara, Rome, Central Europe, London, Edinburgh, Oslo and the Arctic Circle.

South of the equator the game's summer is still the season of long days; the noon sun then stands in the north.

## The three days

A picture shows the sun's height over 24 hours at midsummer, at the equinox and at midwinter, against the horizon. Below it, for each of those days:

- **the day's length,** or **midnight sun** when the sun doesn't set, or **polar night** when it doesn't rise;
- **the noon sun's height.**

The last line turns the hours into the game's clock: how many minutes of daylight a game day has at midsummer and at midwinter.

| Latitude | Midsummer day | Midwinter day | Noon sun at midsummer |
|---|---|---|---|
| 0° (the equator) | 12 h | 12 h | 67° |
| 47° N (Central Europe) | 15.7 h | 8.3 h | 66° |
| 53° N (Britain) | 16.7 h | 7.3 h | 60° |
| 70° N | midnight sun | polar night | 43° |

With a 20-minute game day at 53° N, that is about 14 minutes of daylight at midsummer and 6 at midwinter.

## Preview the day here

**Preview the day here** turns on day and night and the seasons, with clear weather, so you can see the sun on your map. Two controls come with it:

- **Season:** spring, summer (midsummer), autumn or winter (midwinter).
- **Hour:** the time of day, 0 to 24 h.

The clock runs on from the hour you set. Turn the box off and the settings from before come back; the map is saved without the preview.

## Saving

[Save](saving.md#save-map) writes the latitude into the map's `map.json`, as `environment.latitude` ([wkbai's map format](map-format.md)). It takes effect in every game started on the map.

> wkbai: the original game's sun always runs as at 47° N. Original maps and maps without a latitude keep that.
