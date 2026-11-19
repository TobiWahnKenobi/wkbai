---
title: Priest
ids: [unit/Priest, command/Spawn Priest]
order: 10
---

# Priest

<!-- gen:icon -->
![Priest](game:interface/icons/characters/celestial_priest.tga)
<!-- /gen:icon -->

<!-- gen:facts -->
```facts
blueprint: Priest
```

<!-- facts:static -->
| | |
|---|---|
| Cost | 150 food, 150 wood |
| Food upkeep | 4 |
| Hit points | 80 |
| Armour | 5 (front 5, sides 3.75, rear 0.5) |
| Melee attack | 15 every 2 s |
| Speed | 4 |
| Sight | 5 (25 m) |
<!-- /facts:static -->
<!-- /gen:facts -->

## Role

The priest is the Imperial holy man from the [Church](../buildings/church-stone.md), for 150 food and 150 wood. He doubles the damage of your troops, burns demons, unmasks spies and succubi, and makes your peasants work harder. He has only 80 hit points and a 15-point blow, but arrows hardly hurt him: he takes a quarter of an arrow's damage.

## Abilities

- **Charge:** his spells use a charge bar of 60, which refills by about 1 a second.
- **[Bless](../commands/bless.md)** (60 charge, range 50 m): the target and up to 7 of its nearest allies within 20 m deal **double damage**, hand to hand and with missiles, for 30 seconds. A blessing also lifts a curse. A priest set to [guard](../commands/guard.md) a unit blesses it by himself when he has the charge.
- **[Exorcise Demons](../commands/exorcism.md)** (15 charge, range 70 m): 100 damage to a demonic unit, shared out with that player's demonic units within 10 m of it. Demonic units are the [Succubus](succubus.md), [High Priestess](high-priestess.md), [Arch Druid](arch-druid.md), the elementals, the [Fomorian Beast](rock-thrower.md), [DeathWyrm](infernal-engine.md), [Spider Demon](horde-spewer.md), [Gibbering Horde](gibbering-horde.md), [Behemoth](behemoth.md) and [Abaddon](abaddon.md).
- **[Burn a Heretic](../commands/burn-heretic.md):** burns a preaching heretic (a peasant possessed by a succubus) and frees the peasants who were listening to it. No charge needed.
- **Unmasking:** enemy spies, succubi and possessed peasants that come within his sight (25 m) lose their disguise.
- **Holy protection:** enemy spells (Charm, Possess, Berserker Rage) fail on your units within 50 m of him.
- **[Pray](../commands/pray-at-church.md):** point him at a Church nobody else is using. While he prays there, your peasants stop to pray at that Church after every seventh delivery and come back able to carry 1 more unit of goods.
- **[Pray for Righteousness](../commands/pray-for-righteousness.md):** at your Cathedral or a building making the Sacrifice for the Archangel, he speeds it up (0.3 every 1.1 seconds).

## Strong against

- **Demons and Pagan casters**: one exorcism takes 100 hit points off a lone [Succubus](succubus.md) (who has 100).
- **Spies and possessed peasants**, which he unmasks.
- **Archers**: their arrows barely scratch him.

## Weak against

- **Any soldier that reaches him**, especially cavalry and gunners (bullets do their full damage less his armour 5).
- A demon standing in a group shares the exorcism with the others, so each takes less.

## Trained at / Built by

<!-- gen:trained -->
- **Trained at:** [Church](../buildings/church-stone.md)
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
- [Exorcise Demons](../commands/exorcism.md)
- [Interrogate](../commands/interrogate.md)
- [Burn a Heretic](../commands/burn-heretic.md)
- [Bless](../commands/bless.md)
- [Attack](../commands/attack.md)
- [Pray](../commands/pray-at-church.md)
- [Convert Troops to Peasants](../commands/military-unit-convert-to-peasant.md)
- [Garrison](../commands/garrison.md)
- [Board Vessel](../commands/board-vessel.md)
- [Pray for Righteousness](../commands/pray-for-righteousness.md)
- [Resupply Ammunition](../commands/resupply.md)
- [Guard Next Unit Selected](../commands/guard.md)
- [Flee](../commands/run-away.md)
- [Move](../commands/move.md)
<!-- /gen:commands -->

## Tips

- [Bless](../commands/bless.md) your front line just before it meets the enemy; 30 seconds of double damage wins most fights.
- Keep a priest with every army facing Pagans: he unmasks succubi and makes their spells fail.
- Exorcise lone demons; against a crowd the damage is spread thin.
- Put an idle priest to [Pray](../commands/pray-at-church.md) at home.
