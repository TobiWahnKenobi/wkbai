---
title: Holy Warrior
ids: [unit/Monastic Knight, command/Spawn Monastic Knight]
order: 10
---

# Holy Warrior

<!-- gen:icon -->
![Holy Warrior](game:interface/icons/characters/celestial_knight_upgrade.tga)
<!-- /gen:icon -->

<!-- gen:facts -->
```facts
blueprint: Monastic Knight
```

<!-- facts:static -->
| | |
|---|---|
| Cost | 100 food, 20 gold |
| Food upkeep | 2 |
| Hit points | 180 |
| Armour | 12 (front 16, sides 9, rear 6) |
| Melee attack | 37 every 2.5 s |
| Speed | 8.5 |
| Sight | 4 (20 m) |
<!-- /facts:static -->
<!-- /gen:facts -->

## Role

The Holy Warrior is a holy knight from the [Holy Chapter](../buildings/holy-chapter.md): 180 hit points, a 37-point blow every 2.5 seconds, speed 8.5 and good armour all round (16 at the front, 9 on the sides, 6 behind), for 100 food and 20 gold. It is heavy cavalry that also heals and fights demons.

## Abilities

- **[Heal](../commands/monastic-knight-heal.md):** costs 10 charge a second and heals one ally within 5 m by 15 hit points a second (25 after [Surgery](../upgrades/surgery.md)). A full charge bar (30) gives three ticks, 45 hit points. When the patient is whole it moves on to the nearest wounded ally within 30 m. Siege engines and other repairable units can't be healed this way.
- **Heals itself** when it stands idle, hurt and with no enemy within 30 m, as long as it has charge.
- **[Exorcise Demons](../commands/exorcism.md):** 20 damage to a demonic unit within 70 m, shared with the target's demonic comrades within 10 m. It costs 15 charge.
- **Holy protection:** enemy spells fail on your units within 50 m of a Holy Warrior (Charm, Possess, Berserker Rage). It can't be charmed itself.
- The charge bar refills by about 1 a second.

## Strong against

- **Archers and gunners**: 35 a blow to an [Archer](archer.md); 32 to a [Mangonel](mangonel.md).
- **Missiles**: arrows do little through armour 16 (an [Archer](archer.md) does 1, a [Huntress of the Dawn](pagan-archer.md) 4).
- **Pagan casters and demons** such as the [Succubus](succubus.md), which it hits hard and whose spells it blocks.

## Weak against

- **Formed pikes from the front**: a [Warrior of the Sun](pagan-pikeman.md) does 24 a blow, and a formed line makes it break off after one blow unless it is in a Wedge.
- **Monsters and giants**: a [Stone Elemental](stone-elemental-large.md) or the [Imperial War Elephant](war-elephant.md) outclass it.

## Trained at / Built by

<!-- gen:trained -->
- **Trained at:** [Holy Chapter](../buildings/holy-chapter.md)
<!-- /gen:trained -->

## Needs

<!-- gen:needs -->
- building a [Holy Chapter](../buildings/holy-chapter.md)
- Imperial or neutral side only: the button goes once you build a [Maypole](../buildings/maypole.md) (the Pagan path).
<!-- /gen:needs -->

## Leads to

<!-- gen:leads -->
- Nothing: it unlocks no building, unit or research.
<!-- /gen:leads -->

## Commands

<!-- gen:commands -->
- [Capture](../commands/capture.md)
- [Take Cart](../commands/take-cart.md)
- [Join Formation](../commands/join-formation.md)
- [Halt](../commands/halt.md)
- [Attack at Will](../commands/aggression.md)
- [Stand your Ground](../commands/passivity.md)
- [Interrogate](../commands/interrogate.md)
- [Attack](../commands/attack.md)
- [Convert Troops to Peasants](../commands/military-unit-convert-to-peasant.md)
- [Garrison](../commands/garrison.md)
- [Board Vessel](../commands/board-vessel.md)
- [Distribute Food Rations (Baron Artos Formation Supply)](../commands/artos-formation-supply.md)
- [Heal](../commands/monastic-knight-heal.md)
- [Exorcise Demons](../commands/exorcism.md)
- [Form Up](../commands/formation.md)
- [Train](../commands/train.md)
- [Resupply Ammunition](../commands/resupply.md)
- [Guard Next Unit Selected](../commands/guard.md)
- [Flee](../commands/run-away.md)
- [Move](../commands/move.md)
<!-- /gen:commands -->

## Tips

- Use [Heal](../commands/monastic-knight-heal.md) between fights to patch up the army; leave it idle out of reach of the enemy and it heals itself.
- Ride with your army against Pagans: its presence makes enemy succubus spells fail.
- Charge archers and engines in a [Wedge](../commands/formation.md) for a triple first blow.
