---
title: Cog
ids: [unit/Cog, command/Spawn Cog]
order: 10
---

# Cog

<!-- gen:icon -->
![Cog](game:interface/icons/characters/unaligned_cog.tga)
<!-- /gen:icon -->

<!-- gen:facts -->
```facts
blueprint: Cog
```

<!-- facts:static -->
| | |
|---|---|
| Cost | 25 food, 150 wood, 10 gold |
| Food upkeep | 1 |
| Hit points | 700 |
| Armour | 5 (front 25, sides 10, rear 5) |
| Melee attack | 20 every 2 s |
| Speed | 6 |
| Sight | 15 (75 m) |
<!-- /facts:static -->
<!-- /gen:facts -->

## Role

The cog is the transport ship: it carries troops, carts and engines across water, and can run goods between docks. It has 700 hit points and armour 25 at the bow, 10 at the sides and 5 at the stern, and no weapon. It costs 25 food, 150 wood and 10 gold at the [Dock](../buildings/dock-wood.md).

## Trained at / Built by

<!-- gen:trained -->
- **Trained at:** [Dock](../buildings/dock-wood.md)
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
- [Halt](../commands/halt.md)
- [Deliver Resources (Cog Set Up Drop Off Dock)](../commands/cog-set-up-drop-off-dock.md)
- [Repair (Docking)](../commands/docking.md)
- [Embark Next unit Selected](../commands/pick-up-passenger.md)
- [Disembark](../commands/unload-passengers.md)
- [Guard Next Unit Selected](../commands/guard.md)
- [Move](../commands/move.md)
<!-- /gen:commands -->

## Abilities

- **Passengers.** It holds 40: a soldier or a rider counts 1, a cart 3, a battering ram, a bombard or an explosive cart 5. Other ships, the Archangel and Abaddon cannot board.
  - **[Board Vessel](../commands/board-vessel.md)** (on the unit) or **[Embark Next unit Selected](../commands/pick-up-passenger.md)** (on the cog): units board from within 6 m; the cog waits for boarders up to 40 m away.
  - **[Disembark](../commands/unload-passengers.md):** each passenger is set on the nearest land. The cog must lie next to land troops can walk on.
  - **Sinking.** If the cog sinks at sea, everyone aboard drowns; near the shore they are put ashore.
- **[Deliver Resources (Cog Set Up Drop Off Dock)](../commands/cog-set-up-drop-off-dock.md):** a sea supply route. The cog waits at your dock until it holds 50 goods or more, loads up to 500, sails them to the dock's overseas depot (or your nearest dock with a road to the palace), and repeats.
- **[Repair (Docking)](../commands/docking.md):** within 25 m of your dock it is repaired by 10 hit points a second, for 1 wood a second.

## Strong against

- Nothing: it has no attack.

## Weak against

Damage numbers below are per blow or per shot, before research, experience, height and formation bonuses: the attack minus the armour on the side that is hit (see [Combat](../mechanics/combat.md)).

- **Warships.** A [Galleass](bombard-boat.md) ball does 25 to its bow and 40 to its side; a [Fire Boat](fire-boat.md) does 20 an arrow to its side, three arrows every 2 s.
- **Explosive carts on board**: if one explodes, the cog is destroyed.

## Tips

- Clear the water with warships before you send a loaded cog.
- Turn its bow to the enemy: armour 25 at the front against 10 at the sides.
- Land troops where the shore is flat and open, or they cannot get off.
