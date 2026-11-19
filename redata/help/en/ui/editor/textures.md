---
title: Textures
ids: [ui/editor/textures, ui/editor/textures/group#group, ui/editor/textures/list#the-texture-list, ui/editor/textures/radius#radius-m, ui/editor/textures/randomturn#random-turn, ui/editor/textures/turn#turn, ui/editor/textures/flip#flip-x-and-flip-z]
order: 20
---

# Textures

The **Textures** mode paints the ground's pictures. The map is a grid of square tiles, 5 m a side, and each tile shows one texture: grass, earth, rock, sand, snow, a river bed, or a transition between two of them.

Choose a group and a texture, then hold the left mouse button on the ground and move the mouse. Every tile whose centre is inside the brush gets the texture.

## Group

The map's textures come in groups, listed by name.

- **Materials:** one kind of ground, such as `GRASS1`, `SAND1`, `SNOW3` or `MOUNTAIN1`.
- **Transitions:** two materials meeting, such as `CLIFF1-SAND1` or `GRASS1-SNOW4`. Paint them along the border between two materials, so that one blends into the other instead of meeting in a hard square edge.

The groups are those of the texture set the map uses, so they can differ from map to map.

## The texture list

The textures of the chosen group, each as its number, then its picture file and, in brackets, its place in that picture. Click one to paint with it. A material's textures are variants of the same ground; mixing them hides the repeats.

## Radius (m)

The brush's size: 2.5 to 60 metres. At 2.5 m it paints a single tile.

## Random turn

Gives each tile its own quarter turn. A field of one texture then looks less like a pattern. Painting over the same tiles again keeps their turns.

## Turn

With **Random turn** off: the quarter turns every painted tile gets, 0 to 3 ("1 x 90" is a quarter turn). Turn transition tiles until their edges match their neighbours.

## Flip x and Flip z

Mirror the painted tiles left to right (**Flip x**) or top to bottom (**Flip z**). With turns, they give every way a transition tile can lie.

## The ground decides who can walk there

Each texture group belongs to a terrain type, and the terrain type decides who may stand on it. Land units can't stand on **Coastline**, **Desert Mountain**, **Desert Rock**, **Grass 'n' Water**, **Mountains**, **River Bed** or **Water** ground. So painting is also level design:

- a band of mountain ground is a wall no army can cross;
- a gap left in it is a pass, which an army must go through.

After painting, run [Check](saving.md#check): it tells you if a player can't be reached by land.

The ground also matters to farms: a farm is fertile, standard or barren depending on the ground it is built on ([resources](../../mechanics/resources.md#farms-and-the-ground)).

> wkbai: the editor paints what you choose. It doesn't check that neighbouring tiles fit together, so look at the borders from close up.
