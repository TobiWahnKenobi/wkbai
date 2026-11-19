---
title: Terrain
ids: [ui/editor/terrain, ui/editor/terrain/raise#raise, ui/editor/terrain/lower#lower, ui/editor/terrain/smooth#smooth, ui/editor/terrain/flatten#flatten, ui/editor/terrain/radius#radius-m, ui/editor/terrain/strength#strength, ui/editor/terrain/fine#fine-heights]
order: 10
---

# Terrain

The **Terrain** mode shapes the ground: hills, valleys, ramps, plateaus and the hollows that lakes fill.

Choose a tool, then hold the left mouse button on the ground and move the mouse. The brush is round. It works hardest at its centre and fades out towards its edge, so hills come out rounded.

## Raise

Lifts the ground under the brush, a little more for every moment you hold the button.

## Lower

Sinks the ground under the brush. Lower the ground to make the hollow for a lake ([Water](water.md)).

## Smooth

Moves each point of the ground towards the height of its neighbours. Use it to round off sharp edges, soften a slope units should climb, or blend a raised area into the land around it.

## Flatten

Brings the ground under the brush to the height where you started the stroke. Start on a hilltop and paint outwards to make a plateau, or start in a valley to cut a level path. Buildings and farms want level ground.

## Radius (m)

The brush's size: from 5 to 100 metres (20 at first). A tile is 5 m wide, so a radius of 20 m covers about 8 tiles across.

## Strength

How fast the brush works: 1 to 100 (20 at first). For **Raise** and **Lower** it is the number of height steps per second at the brush's centre. For **Smooth** and **Flatten** it is how quickly the ground moves towards its target.

## Fine heights

On at first. The ground can then take any height between the map's steps, which gives smooth slopes.

- **Off:** the brush works in whole steps, as the original game's maps do.
- **Where they are kept:** the fine part of each height goes into a file of wkbai's own beside the map (`.wkbmap`). The original game reads the map's whole steps only, so it sees your hills slightly rounded down.

## What happens after a stroke

- **Water fills again:** the lakes flood the new ground. A hollow you dig beside a lake fills if it is lower than the water.
- **Objects stand on the new ground:** buildings, units and trees in the area you changed are moved up or down to it.
- **Undo** takes back the whole stroke ([Undo and redo](index.md#undo-and-redo)).

## Tips

- **Heights are steps:** the map has 256 whole steps. A map's scale sets how tall one step is, so the same strength makes taller hills on some maps than on others.
- **Steep slopes stop units:** units get stuck on very steep ground. Leave ramps up to every plateau you want armies to reach, and run [Check](saving.md#check) to make sure every start can reach the others by land.
- **Height matters in battle:** units on higher ground see further, which gives archers more range. A hill near the centre is worth fighting for.
- **The edge band:** the brushes also reach the strip of land around the playable area. Nobody plays there; it is the backdrop. With [Extend map](help:option/extendMap) on, wkbai continues the land beyond it from this strip.
