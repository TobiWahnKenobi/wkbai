---
title: Make a skirmish map
ids: [ui/editor/make-a-map, ui/editor/tutorial]
order: 5
---

# Make a skirmish map

This walk-through takes you from an empty map to one you can play against AI Generals in Skirmish. Save often: **Save map** never changes the map you started from.

## 1. Choose a map to start from

The editor can't add or remove players, so your map has the players of the map you start from. Choose one with the number of players you want.

1. In the main menu, click **Editors**, then **Map Editor**.
2. Choose a map. The number in brackets is its number of players: "4 Peaks (2)" has two.
3. Click **Edit**. The map opens paused, with the **Map editor (F8)** window on the left.

## 2. Name your map

At the bottom of the window, type a name in **Map name**. Start it with the number of players and the word "Player", for example **2 Player Green Vale**. Skirmish then gives it two start positions ([why](saving.md#name-it-for-skirmish)).

## 3. Clear the land

You can build on the map as it is, or start from empty ground:

- **Same size:** click **New blank map...**, then **Make it blank**. The ground goes flat and bare, and every object but the players goes.
- **Another size:** click **New map of another size...**, set **Width** and **Height** in tiles (128 tiles are 640 m), then **Make it**. The new map opens in the editor.

The players' start cameras, where their views begin, stay where they are. Before you clear the map, note where each player's town stands, so you can build the new towns in the same places (see [step 7](#7-set-up-the-start-positions)).

## 4. Shape the land

Click **Terrain**.

1. Choose **Raise**, set **Radius (m)** to about 40 and **Strength** to about 20.
2. Hold the left mouse button and move over the ground to build hills. Let go and start again: each stroke is one step of undo (**Ctrl+Z**).
3. Use **Lower** for valleys and the hollows that lakes will fill.
4. Use **Smooth** to round off the hills, and **Flatten** for level ground where towns will stand. Flatten keeps the height where your stroke began.

Leave each start area wide and level: a town needs room. Keep slopes gentle where armies should pass. See [Terrain](terrain.md).

## 5. Add water

Click **Water**, then **Add a lake**.

1. Set **Depth (m)**: 2 or 3 m is a pond, more is a deep lake.
2. Click the bottom of a hollow. The water fills it up to that height above the ground there.
3. Too much water? **Ctrl+Z**, then click higher up or with less depth. On flat ground even 1 m of water spreads far.
4. **Remove a lake** and a click on water takes a lake away.

For flowing rivers, sea waves and waterfalls, see [Water](water.md).

## 6. Paint the ground

Click **Textures**.

1. Choose a **Group** (for example a grass group) and a texture in the list.
2. Tick **Random turn**, so the tiles don't repeat in a pattern.
3. Hold the left mouse button and paint. **Radius (m)** sets the brush's size.
4. Paint rock or mountain on steep slopes, sand on shores, and a transition group (two names joined by "-") along each border.

Mountain and water ground blocks land units: use it for walls and passes. See [Textures](textures.md).

## 7. Set up the start positions

A start position is a player of the kind **Human Player** with a town to start from. Each needs at least a Manor and a few peasants.

1. Click **Objects**, then **Place**.
2. **Class:** Buildings. **Player:** the first player ("1027 Human Player", the numbers differ). **Filter:** type "manor". Click **Manor** in the list.
3. Click the ground where this player's town is to be.
4. **Class:** Characters, **Filter:** "peasant", click **Peasant**, then click a few times beside the Manor: one peasant each click.
5. Repeat for every Human Player, with the same buildings and units, so the start is fair.

**The start cameras:** the editor can't move a player's start camera, the place their view begins. It stays where the map you started from had it (on a new map of another size it is moved inside the map if it was outside). So build each player's town where that player's town stood on the map you started from. To check, use **Save and play**: your view starts at the first player's camera.

To move a building or unit you placed, choose **Select, move, turn**, click it, press **G**, move the mouse and click. Only your own player's things can be selected unless you turn on **Dev mode** ([Objects](objects.md#select-move-turn)).

## 8. Add trees, stone and gold

Still in **Objects** and **Place**:

1. **Class:** Containers (trees, ...).
2. **Filter:** "tree" for trees, "stone" for stone outcrops, "gold" for gold.
3. Click the ground for each one. Woods are many trees close together.
4. Give every start the same: a wood nearby, and stone and gold within a short walk.

For their owner (**Player**), use the one the map you started from used for its trees ([Objects](objects.md#player)).

Note that in Skirmish the **Randomise Resource** setting (Medium unless you change it) replaces the map's stone and gold outcrops with new ones. Set it to Off to play with yours ([Settings tab](../frontend/skirmish/settings.md#randomise-resource)).

## 9. Set the players

Click **Players**. For each player, set its starting **Food**, **Wood**, **Gold** and **Stone**, and whether it is **Allied**, **Neutral** or **Enemy** towards each other player. In a skirmish the lobby's settings and teams take over, so the defaults are fine for most maps. See [Players](players.md).

## 10. Check and save

1. Click **Check**. Fix what it lists in orange: a player with no units or buildings, or a start that can't be reached by land. "No problems found" means all is well.
2. Click **Save map**. The message says "Saved Maps\2 Player Green Vale.wkbai (in Skirmish)".
3. Click **Save and play** to try it at once.

## 11. Play it in Skirmish

1. Leave the game and go back to the main menu.
2. Click **Skirmish**. With the Players filter on **All**, your map is at the end of the map list as "2 Player Green Vale".
3. Choose it, set up the players and settings, and click **Start**.

To change it again: main menu, **Editors**, **Map Editor**, your map, **Edit**. Save under the same name to update it.

## 12. Share it

- **With wkbai players:** send them your map's folder, `2 Player Green Vale.wkbai` ([sharing a map](saving.md#sharing-a-map)).
- **With players of the original game:** use [Export for the original](saving.md#export-for-the-original).
