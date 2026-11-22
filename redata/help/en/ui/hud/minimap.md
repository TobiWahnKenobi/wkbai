---
title: Minimap
ids: [ui/hud/minimap, ui/hud/minimap/map#what-it-shows, ui/hud/minimap/camera#the-camera-marker, ui/hud/minimap/click#moving-the-camera, ui/hud/minimap/buttons#the-four-buttons, ui/hud/minimap/military#the-four-buttons, ui/hud/minimap/economic#the-four-buttons, ui/hud/minimap/resource#the-four-buttons, ui/hud/minimap/activity#the-four-buttons, ui/hud/minimap/alerts#alerts, ui/hud/minimap/pings#pings, ui/hud/minimap/objective-markers#objective-markers]
order: 50
---

# Minimap

![The minimap panel, with its four arc buttons and a red alert dot](shot:hud-minimap)

The round panel in the lower right corner holds a map of the whole playable area, north at the top.

## What it shows

- **The land:** the map's own picture. With weather on, snow and ice show on it too.
- **Fog of war:** where none of your units see, the map is darker (with fog of war on; see [Fog of war](help:mechanic/fog-of-war)).
- **Units and buildings** as small squares in their player's colour; buildings are larger. Trees and other neutral resources are dark green; a forest shows as a dark-green patch, one dot per point of the minimap.
- **Enemies in the fog:** enemy units you can't see aren't shown; enemy buildings show where you last saw them.
- **A disguised enemy** shows in your own colour, as it looks to you.

A map that isn't square sits in the middle of the square with dark bars at its sides, as in the original.

## The camera marker

A white arrow stands where the camera is and points the way it looks.

## Moving the camera

- **Click** the minimap: the camera goes to look at that point. It glides there in about a third of a second; with **Camera Snapping** on ([option](help:option/cameraSnapping)) it jumps there at once.
- **Hold and drag** on the minimap: the camera follows the mouse.
- A click on the dark bars of a non-square map goes to the nearest edge.

A click on the minimap never selects or orders anything.

## The four buttons

Four arcs round the map are buttons. An arc with a **green rim** is switched on.

| Arc | Button | What it does |
|---|---|---|
| Left | **Military units** | Shows or hides soldiers on the map. |
| Bottom | **Civilian units** | Shows or hides peasants and other civilians. |
| Right | **Resources** | Shows or hides trees, mines and other resources. |
| Top | **Peasant activity** | Opens the [peasant activity](peasant-activity.md) tubes above the minimap. Each click goes on: closed, the idle peasants' tube, all five tubes, closed. |

Which group a thing belongs to is set by the game set. Hiding a group only tidies the map; nothing changes in the game.

## Pings

A **white ring shrinking** onto a place for a second and a half shows where a key just took you: a [selection key](../../controls/selection.md#selection-keys) (V, I, B, ...) or **Home** (the last warning).

## Alerts

The game set warns you on the minimap:

- **a blinking square:** something going on at that place for some seconds, for example one of your units under attack (red);
- **collapsing circles:** rings shrinking onto a place one after another, for example a building on fire (orange), a scout's or tower's sighting (green).

The colour is the script's choice: red, orange, green, yellow, white, blue, or a player's colour. **Home** takes the camera to the last four warning places in turn, the latest first.

> wkbai: the original's two script alerts are kept; their exact size and speed on the original's minimap weren't measured.

## Objective markers

In the tutorials and some campaign levels, a **gold ring breathing** in and out marks the place an objective is about, or follows the unit it is about.

## In a replay

A replay marks its events on the minimap for a while with the game's own icons: crossed swords for a battle, a siege engine, a sword for a raid, a building for a new settlement, a flickering fire for a building destroyed, a blinking red cross for a player defeated. A fight's icon beats for as long as it lasts. See [Replay controls](replay-controls.md).
