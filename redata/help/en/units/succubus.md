---
title: Succubus
ids: [unit/Succubus, command/Spawn Succubus]
order: 10
---

# Succubus

<!-- gen:icon -->
![Succubus](game:interface/icons/characters/pagan_succubus.tga)
<!-- /gen:icon -->

<!-- gen:facts -->
```facts
blueprint: Succubus
```

<!-- facts:static -->
| | |
|---|---|
| Cost | 100 food, 50 gold |
| Food upkeep | 2 |
| Hit points | 100 |
| Armour | 5 (front 5, sides 3.75, rear 0.5) |
| Melee attack | 15 every 2 s |
| Speed | 3.6 |
| Sight | 5 (25 m) |
<!-- /facts:static -->
<!-- /gen:facts -->

## Role

The succubus is a Pagan temptress from the [Henge](../buildings/henge.md), for 100 food and 50 gold. She goes about disguised as a peasant woman, steals enemy soldiers, drives them into a frenzy and turns peasants into heretics. She has 100 hit points and a 15-point blow, and doesn't show on the minimap.

## Abilities

- **Charge:** her spells use a charge bar of 60, which refills by about 1 a second. Every spell has a range of 50 m and takes effect 4 seconds after she casts it.
- **[Charm](../commands/charm.md)** (45 charge): an enemy soldier becomes yours for good. Most troops can be charmed, knights included; holy units, clergy, the Archangel, elementals, succubi and the Undead Legion can't.
- **[Berserker Rage](../commands/make-baresark.md)** (60 charge): up to four soldiers near her, those closest to the target first, go berserk for 50 seconds. They leave their owner's control and attack every unit around them, friend or foe, with a quarter more hit points, damage and attack speed but a quarter less armour. Then they go back to their owner. Soldiers of any side can be caught, so keep your own troops away. Peasants can't be driven berserk.
- **[Possess](../commands/possess.md)** (needs a full bar): an enemy peasant becomes yours, disguised as a peasant. It can [spread heresy](../commands/become-heretic.md): enemy peasants within 50 m stop work and kneel around it until a priest burns it.
- **[Desecrate](../commands/desecrate.md):** standing at an enemy [Cathedral](../buildings/cathedral-stone.md), she drains the charge it builds up for Acts of God.
- **Disguise:** an enemy [Priest](priest.md), Bishop, Inquisitor, High Priestess, Arch Druid or spy that sees her unmasks her; she disguises herself again when none of them can see her.
- **Spells fail** on targets within 50 m of their own side's holy units or temple buildings (priests, monks, Holy Warriors, Arch Druids, Churches, Cathedrals, Monasteries and the like). The charge is spent all the same.

## Strong against

- **Expensive enemy soldiers**, which she charms one at a time from 50 m.
- **Massed enemy troops** that a Berserker Rage turns on each other.
- **The enemy economy**, through possessed peasants and heresy.

## Weak against

- **Clergy**, which unmask her and make her spells fail; an exorcism does 100 to her, a whole succubus.
- **Any soldier that reaches her**: she has 100 hit points and armour 5.

## Trained at / Built by

<!-- gen:trained -->
- **Trained at:** [Henge](../buildings/henge.md)
<!-- /gen:trained -->

## Needs

<!-- gen:needs -->
- Pagan or neutral side only: the button goes once you build a [Church](../buildings/church-stone.md) (the Imperial path).
<!-- /gen:needs -->

## Leads to

<!-- gen:leads -->
- Nothing: it unlocks no building, unit or research.
<!-- /gen:leads -->

## Commands

<!-- gen:commands -->
- [Capture](../commands/capture.md)
- [Desecrate](../commands/desecrate.md)
- [Halt](../commands/halt.md)
- [Enter Dark Portal](../commands/enter-dark-portal.md)
- [Interrogate](../commands/interrogate.md)
- [Berserker Rage](../commands/make-baresark.md)
- [Possess](../commands/possess.md)
- [Attack](../commands/attack.md)
- [Convert Troops to Peasants](../commands/military-unit-convert-to-peasant.md)
- [Garrison](../commands/garrison.md)
- [Board Vessel](../commands/board-vessel.md)
- [Charm](../commands/charm.md)
- [Resupply Ammunition](../commands/resupply.md)
- [Guard Next Unit Selected](../commands/guard.md)
- [Flee](../commands/run-away.md)
- [Move](../commands/move.md)
<!-- /gen:commands -->

## Tips

- [Charm](../commands/charm.md) the enemy's best units, away from his priests and churches.
- Cast [Berserker Rage](../commands/make-baresark.md) into the middle of an enemy army, with your own men out of the way.
- Possess peasants at the edge of the enemy town and set them preaching.
- To stop a succubus, keep a priest with your army: she can't hide from him and her spells fail near him.
