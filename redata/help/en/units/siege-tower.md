---
title: Siege Tower
ids: [unit/Siege Tower, unit/Siege Tower Open, building/Siege Tower, command/Build Siege Tower]
order: 10
---

# Siege Tower

<!-- gen:icon -->
![Siege Tower](game:interface/icons/characters/unaligned_Seige_tower.tga)
<!-- /gen:icon -->

<!-- gen:facts -->
```facts
blueprint: Siege Tower
```

<!-- facts:static -->
| | |
|---|---|
| Cost | 200 wood |
| Build time | 30 s |
| Food upkeep | 1 |
| Hit points | 600 |
| Armour | 5 (front 15, sides 15, rear 15) |
| Melee attack | 5 every 2 s |
| Speed | 3.5 |
| Sight | 5 (25 m) |
<!-- /facts:static -->
<!-- /gen:facts -->

## Role

The siege tower carries foot soldiers over a wall. A [Sapper](sapper.md) builds it in 30 seconds. It has 600 hit points, armour 15 on every side while closed, and takes only a quarter of an arrow's damage.

## Abilities

- **[Siege Tower Dock With Wall](../commands/siege-tower-dock-with-wall.md):** point at a Stockade or stone wall section. The tower drives up to it, opens and faces the wall. If another tower already uses that section, it picks the nearest free one within 25 m.
- **While open** it can't move and its armour drops to 5 on every side.
- **[Scale Siege Tower](../commands/scale-siege-tower.md):** your foot soldiers climb the open tower one after another and come down about 10 m beyond it, on the far side of the wall. Archers, gunners, spearmen, pikemen and spearwomen, mercenaries, barbarians, sappers and scouts can climb; horsemen can't. Only the tower's owner's troops can use it.
- **[Close Siege Tower](../commands/close-siege-tower.md)** shuts it so it can move again.

## Strong against

- **Walls**: it gets troops past a wall without breaking it.

## Weak against

- **Any attack while it is open** (armour 5).
- **Heavy siege**: trebuchet stones, rockets and mines.
- It has no real attack of its own (5 a blow).

## Trained at / Built by

<!-- gen:trained -->
- **Built by:** [Sapper](sapper.md) (the engine is put up on the spot like a building, then rolls out)
<!-- /gen:trained -->

## Needs

<!-- gen:needs -->
- Nothing beyond the building that trains it and the cost.
<!-- /gen:needs -->

## Leads to

<!-- gen:leads -->
- Nothing: it unlocks no building, unit or research.
<!-- /gen:leads -->

## Commands

<!-- gen:commands -->
- [Capture](../commands/capture.md)
- [Halt](../commands/halt.md)
- [Siege Tower Dock With Wall](../commands/siege-tower-dock-with-wall.md)
- [Interrogate](../commands/interrogate.md)
- [Attack](../commands/attack.md)
- [Board Vessel](../commands/board-vessel.md)
- [Resupply Ammunition](../commands/resupply.md)
- [Flee](../commands/run-away.md)
- [Move](../commands/move.md)
- [Close Siege Tower](../commands/close-siege-tower.md)
<!-- /gen:commands -->

## Tips

- Clear the wall's defenders with missile troops before you send men over.
- Send the climbers in a stream and have them meet up beyond the wall before they fight.
- [Close Siege Tower](../commands/close-siege-tower.md) as soon as the troops are across, to restore its armour.
