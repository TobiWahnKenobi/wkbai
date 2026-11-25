---
title: Top bar
ids: [ui/hud/top-bar, ui/hud/top-bar/settings#settings, ui/hud/top-bar/objectives#mission-objectives, ui/hud/top-bar/global-diplomacy#global-diplomacy, ui/hud/top-bar/diplomacy#diplomacy, ui/hud/top-bar/tributes#tributes, ui/hud/top-bar/player-colour#your-colour, ui/hud/top-bar/population#population, ui/hud/top-bar/food#food-wood-and-gold, ui/hud/top-bar/wood#food-wood-and-gold, ui/hud/top-bar/gold#food-wood-and-gold, ui/hud/top-bar/food-balance#the-food-bar, ui/hud/speed#game-speed-line, ui/hud/environment#weather-line, ui/hud/wind#wind-gauge, ui/hud/wind-gauge#wind-gauge, ui/hud/top-bar/mission-objectives#mission-objectives, ui/hud/top-bar/statistics#statistics-and-timeline, ui/hud/top-bar/timeline#statistics-and-timeline]
order: 20
---

# Top bar

![The top bar: seven buttons at the left, the resources at the right](shot:hud-top-bar)

The bar across the top has two halves. The **buttons** sit at its left end, the **resources** at its right end, and a band of Celtic knotwork fills the space between them on a wide screen.

## Statistics and Timeline

Two buttons of wkbai's own sit left of the five, where the knotwork was: the rail of the five starts further left and holds them too.

- **Statistics** (a bar chart) opens the [statistics window](chronicle.md#statistics), as **F6** does;
- **Timeline** (an hourglass) opens the [timeline](chronicle.md#timeline), as **F7** does.

A button lights up under the mouse, and stays **lit** while its window is open. Click it again (or press the key) to close the window. The tooltip names the key, as set in the [key bindings](help:controls/keys/rebind).

> wkbai: the original had no statistics or timeline during a game; the space held only knotwork.

## The five buttons

The original's five small buttons, from left to right. A button is **greyed** when it can't be used; clicking a grey one does nothing (the click doesn't go through to the battlefield either). A button **flashes** when there is something new in its window, until you open it. Each tooltip says what the button does, or why it is grey.

### Settings

Opens the game menu, as **Esc** does: return to the game, save, load, restart, options, leave. In single player the game pauses while the menu is open. See [the game menu](help:ui/frontend/ingame).

### Mission Objectives

Opens the [objectives window](text-windows.md#mission-objectives). It is lit only on levels that have objectives (the tutorials); elsewhere it is grey. It flashes when a new objective was added that you haven't looked at. Key: **O**.

### Global Diplomacy

Opens the report of how every player stands with every other ([Global Diplomacy](diplomacy-window.md#global-diplomacy)). It lights up once you own a **Tavern**.

### Diplomacy

Opens the [Diplomacy window](diplomacy-window.md#diplomacy), where you offer alliances or declare war. It is lit when the lobby's **Allow Diplomacy** was on, and when there is at least one other player. It flashes when someone makes you an offer.

### Tributes

Opens the [Tributes window](diplomacy-window.md#tributes), for giving food, wood and gold away. It needs a **Trading Post**, and either diplomacy allowed or a game with teams.

## The resources

### Your colour

A small banner in your player colour, left of the numbers.

### Population

Two numbers, one above the other:

- **top:** how many people you have now;
- **bottom:** your population limit (the lobby's Population Limit, or the level's).

When the top number reaches the bottom one, you can't train more until someone dies or the limit rises.

### Food, wood and gold

What you have in store, rounded to whole numbers. Peasants add to them when they deliver at a building that takes their load.

### The food bar

Under the food number, a small bar compares what your people eat with what your farmers can feed:

- **two colours, brown and green:** the brown part is what your people eat, the green part what your farms could feed on top;
- **flashing red:** your people eat as much as or more than your farmers provide. Build farms or put more peasants on them.

The text over it reads *eaten / fed*, for example "38/60". With no farms at all there is no bar.

## Game speed line

Under the left end of the bar, a line shows the speed:

- **"Speed 1x"**, "Speed 2x", "Speed 0.5x": the speed set with **Keypad +** and **Keypad −**;
- **"(reached 1.6x)"** after it when the computer can't keep up with that speed on a busy map;
- **"Paused"** while the game stands still (**P**).

In a replay this line is replaced by the [replay controls](replay-controls.md).

> wkbai: the original knew three speeds. wkbai halves or doubles the speed with each press, without a limit, and shows the speed it really reaches.

## Weather line

Under the right end of the bar, when day and night, seasons or weather are switched on for the game (wkbai's own), one line sums them up, for example:

"14:20  Winter (harsh)  Snow  −4 °C  Frozen  Fields fallow"

- **the time of day** with day and night;
- **the season**, with "(gentle)" or "(harsh)" when it isn't a normal one; **the weather**; **the temperature**;
- **"Flood +1.2 m"** or **"Low water −0.8 m"** when the rivers have risen or fallen;
- **"Frozen"** when the water has frozen hard enough to walk on;
- **"Fields fallow"** in winter.

The line may carry a few more notes of the same kind (farmers wintering indoors, for example). See [Seasons and weather](help:mechanic/seasons-weather) for what they do.

## Wind gauge

At the top right, below the resources, when the game has the lobby's [wind](../frontend/skirmish/wind.md) on (wkbai's own):

- **the dial** turns with the camera: its top is the way the camera looks, and N, E, S and W mark the map's sides (N is the top of the minimap);
- **the arrow** points the way the wind blows, from where it comes from to where it goes. It is longer the stronger the wind, and green in a light wind (up to about 5 m/s), yellow up to about 11 m/s and red in a stronger wind;
- **below the dial,** where it comes from and its speed, for example "WSW 7 m/s", and the wind's name on the Beaufort scale, from "calm" to "storm";
- **its tooltip** also gives the speed of the gust blowing now and what the wind does in the game.

The option [Wind gauge (direction and strength)](help:option/windGauge) hides it.
