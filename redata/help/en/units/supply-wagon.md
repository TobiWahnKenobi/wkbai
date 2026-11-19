---
title: Supply Wagon
ids: [unit/Supply Wagon, command/Spawn Supply Wagon]
order: 10
---

# Supply Wagon

<!-- gen:icon -->
![Supply Wagon](game:interface/icons/characters/unaligned_supply_wagon.tga)
<!-- /gen:icon -->

<!-- gen:facts -->
```facts
blueprint: Supply Wagon
```

<!-- facts:static -->
| | |
|---|---|
| Cost | 100 food, 150 wood |
| Food upkeep | 1 |
| Hit points | 100 |
| Armour | 5 (front 5, sides 3.75, rear 0.5) |
| Melee attack | 5 every 2 s |
| Speed | 3.3 |
| Sight | 5 (25 m) |
<!-- /facts:static -->
<!-- /gen:facts -->

## Role

The supply wagon is a field hospital and ammunition cart. It heals wounded troops around it with your food, refills their charge bars and can collect sacks of goods. It costs 100 food and 150 wood, has 100 hit points, and is slow (speed 3.3); like other wagons it can't enter forest or rocky ground. Arrows do it little harm: it takes only a quarter of an arrow's damage.

## Abilities

- **[Distribute Food Rations (Supply)](../commands/supply.md):** the wagon goes to a wounded ally and then, every second, heals every wounded ally within 30 m by 5 hit points (about 8 after [Surgery](../upgrades/surgery.md)). Each heal costs you 1 food. It stops when no one nearby is hurt or your food runs out. Siege engines, elementals and other units that can't be healed are skipped.
- **[Distribute Food Rations (Formation Supply)](../commands/formation-supply.md):** the same for every wounded member of one of your formations.
- **Resupply:** your units within 25 m (50 m with [Logistics](../upgrades/logistics.md)) get 10 extra charge every 5 seconds, which refills archers', gunners' and siege engines' ammunition much faster. A unit given [Resupply Ammunition](../commands/resupply.md) walks to within 10 m of the wagon and is healed there.
- **[Pick Up Resource Sack](../commands/supply-wagon-pick-up-sack.md):** it loads goods from sacks (up to 800) and takes them to a store.
- **On guard**, it heals wounded allies within 15 m of the unit it guards.

## Strong against

- Nothing in a fight (a 5-point blow).

## Weak against

- **Any soldier**, cavalry most of all. It is easy prey: heavy cavalry rates it among the best targets.
- **Siege engines** rate it as their best target too.

## Trained at / Built by

<!-- gen:trained -->
- **Trained at:** [Warehouse](../buildings/warehouse-stone.md)
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
- [Interrogate](../commands/interrogate.md)
- [Distribute Food Rations (Formation Supply)](../commands/formation-supply.md)
- [Distribute Food Rations (Supply)](../commands/supply.md)
- [Board Vessel](../commands/board-vessel.md)
- [Pick Up Resource Sack (Supply Wagon - Pick up Sack)](../commands/supply-wagon-pick-up-sack.md)
- [Resupply Ammunition](../commands/resupply.md)
- [Deliver Resources (to Deliverable Building)](../commands/deliver-resources-to-deliverable-building.md)
- [Guard Next Unit Selected](../commands/guard.md)
- [Flee](../commands/run-away.md)
- [Move](../commands/move.md)
<!-- /gen:commands -->

## Tips

- Park a supply wagon just behind a formation on a long campaign so the soldiers neither walk home to heal nor run out of arrows.
- Watch your food: every hit point healed costs it.
- Guard it with a few spearmen; losing it far from home leaves the army without healing.
