---
title: High Priestess
ids: [unit/High Priestess, command/Spawn High Priestess]
order: 10
---

# High Priestess

<!-- gen:icon -->
![High Priestess](game:interface/icons/characters/pagan_high_priestess.tga)
<!-- /gen:icon -->

<!-- gen:facts -->
```facts
blueprint: High Priestess
```

<!-- facts:static -->
| | |
|---|---|
| Cost | 100 food, 50 gold |
| Food upkeep | 3 |
| Hit points | 100 |
| Armour | 5 (front 5, sides 3.75, rear 0.5) |
| Melee attack | 10 every 2 s |
| Speed | 3.6 |
| Sight | 6 (30 m) |
<!-- /facts:static -->
<!-- /gen:facts -->

## Role

The high priestess is the Pagan cleric from the [Sacred Grove](../buildings/temple.md). She builds the [Henge](../buildings/henge.md), turns enemy soldiers into gibbering hordes, curses the enemy, opens dark portals and can raise a ghost army. She is no fighter: 100 hit points and a 10-point blow.

## Trained at / Built by

<!-- gen:trained -->
- **Trained at:** [Sacred Grove](../buildings/temple.md)
<!-- /gen:trained -->

## Needs

<!-- gen:needs -->
- Pagan or neutral side only: the button goes once you build a [Church](../buildings/church-stone.md) (the Imperial path).
<!-- /gen:needs -->

## Leads to

<!-- gen:leads -->
- **Builds:** [Henge](../buildings/henge.md)
- **Can turn into or call up:** [Gibbering Horde](gibbering-horde.md)
<!-- /gen:leads -->

## Commands

<!-- gen:commands -->
- [Capture](../commands/capture.md)
- [Build](../commands/build.md)
- [Halt](../commands/halt.md)
- [Create Dark Portal](../commands/dark-portal.md)
- [Interrogate](../commands/interrogate.md)
- [Burn Unbeliever](../commands/burn-unbeliever.md)
- [Repair (High Priestess)](../commands/high-priestess-repair.md)
- [Enter Dark Portal](../commands/enter-dark-portal.md)
- [Polymorph to Gibbering Horde](../commands/summon-gibbering-horde.md)
- [Curse](../commands/curse.md)
- [Attack](../commands/attack.md)
- [Convert Troops to Peasants](../commands/military-unit-convert-to-peasant.md)
- [Garrison](../commands/garrison.md)
- [Board Vessel](../commands/board-vessel.md)
- [Resupply Ammunition](../commands/resupply.md)
- [Guard Next Unit Selected](../commands/guard.md)
- [Flee](../commands/run-away.md)
- [Move](../commands/move.md)
- [Ghost Army](../commands/ghost-army.md)
<!-- /gen:commands -->

## Abilities

Her spells use a charge bar of 60 that starts empty and fills by itself at about 1 point a second, so about a minute from empty to full. Supply Wagons do not fill it. Spells reach 50 m.

- **[Build](../commands/build.md):** she builds the [Henge](../buildings/henge.md). [Repair (High Priestess)](../commands/high-priestess-repair.md) helps finish a Henge under construction, at 7.5 points a second; it does not repair a finished Henge.
- **[Polymorph to Gibbering Horde](../commands/summon-gibbering-horde.md):** a full bar (60), 60 m. The target enemy unit dies at once and a pack of [Gibbering Hordes](gibbering-horde.md) of yours appears in its place, one for every 10 hit points it had. It needs 3 free places in your population. It works on peasants, archers, spearmen, pikemen, knights, scouts, militia, monks and most other ordinary soldiers.
- **[Curse](../commands/curse.md):** 45 charge. The target and its 7 nearest comrades within 10 m do half damage for 30 s, and lose any Bless.
- **[Create Dark Portal](../commands/dark-portal.md):** 60 charge. A passage to one of your possessed peasants, open about 120 s or until units with 1200 hit points in all have gone through. Needs an [Arcanum](../buildings/arcanum.md).
- **[Ghost Army](../commands/ghost-army.md)** and **[Burn Unbeliever](../commands/burn-unbeliever.md)** (burns a peasant preaching heresy).
- **Arrow armour:** arrows and javelins do only a quarter of their damage.
- **Fizzle:** enemy spells aimed at units within 50 m of her fail.

## Strong against

- **Big single units.** Polymorph kills a knight or a pikeman outright and gives you hordes for it: the bigger the victim, the more hordes.
- **Archers**: her arrow armour and front armour 5 mean an arrow does her nothing from the front.

## Weak against

- **Gunners and cavalry.** A bullet does 15, a knight 24 a blow: she dies quickly.
- **Enemy clerics.** A [Priest](priest.md), a [Bishop](bishop.md), a king piece, a church or a temple within 50 m of her target makes her spells fail, and the charge is lost. [Inquisitors](inquisitor.md) burn her with double damage and can exorcise her.

## Tips

- Train one early on the Pagan path: you need her for the Henge.
- Save a full bar for the enemy's best unit, a knight or a heavy pikeman.
- Keep her behind your line; she is slow (3.6) and easy to catch.
