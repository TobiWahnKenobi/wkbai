---
title: wkbai and the original game
ids: [help/differences, help/original-game, help/whats-new#what-wkbai-adds, help/known-gaps#not-yet-as-in-the-original]
order: 5
---

# wkbai and the original game

wkbai plays *Warrior Kings: Battles* with a new engine. The game itself, its maps, units, buildings and rules, comes from your copy of the game. This page sums up what stays the same, what wkbai adds, and where it still differs. Each point links to its page; the pages mark wkbai's own parts with a "wkbai" note.

## What stays the same

- **The rules.** wkbai runs the game's own scripts (the game set): costs, hit points, damage, research, alignment, the AI Generals' Kingmaker scripts and the victory conditions. A few fixes are kept as patches beside the game set ([mods](mechanics/mods.md#what-a-mod-can-change)).
- **The game modes:** skirmish in Standard and Valhalla mode, King Piece, the Kingmaker [campaign](mechanics/campaign.md), the [tutorials](ui/frontend/tutorial.md) and multiplayer.
- **The interface:** the original's layout, menus, buttons, cursors and windows ([the in-game screen](ui/hud/index.md)).
- **The original's keys,** as the defaults ([keys](controls/keys.md)).
- **Saved games** are written in the original's format, and the original's saves can be loaded ([saves and replays](mechanics/saves-and-replays.md)).
- **Mods** made for the original, such as WKB Plus, load as the original loads them ([the Mods screen](ui/frontend/mods.md)).
- **With wkbai's extras switched off** (no seasons, no day and night, the original's graphics settings), a game plays like the original.

## What wkbai adds

**Graphics**

- Direct3D 12 or Vulkan, any window size, high-resolution screens ([System](options/system.md)).
- A view as far as the whole map, land beyond the map's edge, new water, flowing rivers and waterfalls, clouds, grass, shadows, light from fires and the sky, ray tracing, anti-aliasing and upscaling ([graphics](options/graphics/index.md)).

**The world**

- [Day and night](mechanics/day-night.md) and [seasons and weather](mechanics/seasons-weather.md): snow, rain, floods, frozen rivers, lightning and wildfires; peasants who sleep at night and winter indoors ([peasants at night and in winter](mechanics/peasant-lodging.md)); rain that puts burning buildings out; [river currents](mechanics/water-and-currents.md) that carry units off and make them swim. All of it is off until you switch it on in the lobby.
- A [wind](ui/frontend/skirmish/wind.md) that moves the clouds, rain, smoke, waves, trees and grass, fans wildfires, chills units and speeds or slows boats. It is on at first; its lobby tab switches it off.
- The fog of war remembers the trees as you last saw them ([fog of war](mechanics/fog-of-war.md#trees)).

**Playing**

- Every key can be changed, and each action has two keys ([Keyboard](options/controls/keyboard.md)).
- Larger games: up to 10,000 people per player ([Population Limit](ui/frontend/skirmish/settings.md#population-limit)), and fast path finding, so eight-player games stay smooth.
- Any game speed: each press halves or doubles it ([top bar](ui/hud/top-bar.md)).
- [Unit history and heroes](mechanics/unit-history.md), [statistics and the timeline](ui/hud/chronicle.md), building health bars and hero names.
- [Recordings and replays](ui/hud/replay-controls.md) of whole games, with a progress bar of the battles.

**Opponents**

- New choices for the AI Generals: Aggressive AI, the ministry AI and Lua AIs ([AI opponents](mechanics/ai-opponents.md)).
- [Agent control](options/agent-control.md): a program on your computer may play in your place, visible to everyone.

**Menus and tools**

- A [Mods screen](ui/frontend/mods.md) with your own load order, a [Map Editor](ui/frontend/map-editor.md), the [AI General Editor](ui/frontend/general-editor.md) and this help.
- [A map format of its own](ui/editor/map-format.md): map folders that keep a map maker's hand edits, any number of lakes and river speeds, with tools to import the original's maps, list what can go on a map and describe a map, and a server for AI assistants that design maps.
- [Many more options](options/index.md).
- Multiplayer with its own network play, a local game list, passwords, spectators, joining running games and AI takeover for players who leave ([Multiplayer](mechanics/multiplayer.md)).

## Not yet as in the original

wkbai is still being worked on. In this version:

- **Online play:** the original's GameSpy service is gone. wkbai has its own network play and can't play against the original game ([Multiplayer](mechanics/multiplayer.md)).
- **Cheat codes:** the original's chat cheat codes don't exist. wkbai has a cheat mode in the skirmish lobby instead ([Cheat mode](ui/frontend/skirmish/weather.md#cheat-mode)).
- **Campaigns** are kept in wkbai's own files: the original's campaign profiles can't be loaded ([Campaign](ui/frontend/campaign.md)).
- **Movement:** every land unit follows the same land rules ([movement and terrain](mechanics/movement.md)).
- **Combat:** missiles can still hit allied units ([combat](mechanics/combat.md)).
- **Formations** don't share damage among their members, and a standing formation turns to face an attack on its flank ([formations](mechanics/formations.md)).
- **Voices:** units don't answer with a voice when selected ([selecting](controls/selection.md)).
- **Disguises:** the tooltip of a disguised unit names what it really is ([disguises and spies](mechanics/disguises-and-spies.md)).
- **Saves:** the AI's plan in progress starts again after loading ([saves and replays](mechanics/saves-and-replays.md#not-kept-by-a-save)).
- **Lobby defaults:** Clear Fog Of War is off at first ([Settings tab](ui/frontend/skirmish/settings.md)).
