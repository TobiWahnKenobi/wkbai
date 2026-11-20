---
title: Bishop
ids: [unit/Bishop, command/Spawn Bishop]
order: 10
---

# Bishop

<!-- gen:icon -->
![Bishop](game:interface/icons/characters/celestial_bishop.tga)
<!-- /gen:icon -->

<!-- gen:facts -->
```facts
blueprint: Bishop
```

<!-- facts:static -->
| | |
|---|---|
| Cost | 200 food, 50 gold |
| Food upkeep | 5 |
| Hit points | 100 |
| Armour | 5 (front 5, sides 3.75, rear 0.5) |
| Melee attack | 20 every 2 s |
| Speed | 4 |
| Sight | 5 (25 m) |
<!-- /facts:static -->
<!-- /gen:facts -->

## Role

The bishop is the senior Imperial cleric from the [Cathedral](../buildings/cathedral-stone.md). He calls down acts of God: visions, blindness, plague, winter, lightning, the wrath of heaven, earthquake and the falling star Wormwood. He has 100 hit points, armour 5 at the front, a 20-point blow and arrow armour.

## Trained at / Built by

<!-- gen:trained -->
- **Trained at:** [Cathedral](../buildings/cathedral-stone.md)
<!-- /gen:trained -->

## Needs

<!-- gen:needs -->
- building a [Cathedral](../buildings/cathedral-stone.md)
<!-- /gen:needs -->

## Leads to

<!-- gen:leads -->
- Nothing: it unlocks no building, unit or research.
<!-- /gen:leads -->

## Commands

<!-- gen:commands -->
- [Capture](../commands/capture.md)
- [Visions](../commands/inflict-vision.md)
- [Inflict Blindness](../commands/inflict-blindness.md)
- [Inflict Plague](../commands/inflict-plague.md)
- [Inflict Fimbulwinter](../commands/inflict-fimblewinter.md)
- [Inflict Lightning](../commands/inflict-lightning.md)
- [Inflict Wrath of Heaven](../commands/inflict-firestorm.md)
- [Inflict Earthquake](../commands/inflict-earthquake.md)
- [Inflict Wormwood](../commands/inflict-wormwood.md)
- [Halt](../commands/halt.md)
- [Interrogate](../commands/interrogate.md)
- [Attack](../commands/attack.md)
- [Pray](../commands/pray-at-church.md)
- [Pray for Righteousness](../commands/pray-for-righteousness.md)
- [Garrison](../commands/garrison.md)
- [Resupply Ammunition](../commands/resupply.md)
- [Board Vessel](../commands/board-vessel.md)
- [Guard Next Unit Selected](../commands/guard.md)
- [Flee](../commands/run-away.md)
- [Move](../commands/move.md)
<!-- /gen:commands -->

## Abilities

**Where the power comes from.** The acts of God do not use the bishop's own charge but the charge of your Cathedrals (your allies' Cathedrals count too); a cast drains them all. Each Cathedral holds 100 at first, 400 after [Crusade](../upgrades/crusade.md), 1000 after [Witch Hunt](../upgrades/malleus-maleficarum.md) and 1200 once you finish a [Statue of the Archangel](../buildings/michael-statue.md). After Crusade each Cathedral fills by itself at about 0.7 a second. **[Pray for Righteousness](../commands/pray-for-righteousness.md)** at a Cathedral adds about 0.5 a second per bishop, before Crusade too. Acts of God reach 100 m (Blindness 200 m).

- **[Visions](../commands/inflict-vision.md):** 100. Shows you all the military units of the target's owner.
- **[Inflict Blindness](../commands/inflict-blindness.md):** 400, after Crusade. Units and buildings within 5 m of the target see nothing for 45 s.
- **[Inflict Plague](../commands/inflict-plague.md):** 300, after Crusade. The target and up to 13 units within 20 m lose 5-15 hit points every 3 s for 30 s. It can strike your own units too.
- **[Inflict Fimbulwinter](../commands/inflict-fimblewinter.md):** 300, after Witch Hunt. Every unit within 30 m, of any side, is frozen in place for 60 s.
- **[Inflict Lightning](../commands/inflict-lightning.md):** 700, needs the Statue. The struck unit loses 400 hit points; units within 12 m lose up to 480, more the closer they are. A struck building catches fire.
- **[Inflict Wrath of Heaven](../commands/inflict-firestorm.md):** 1000, after Witch Hunt. For 16 s every unit within 30 m loses 8 hit points every 2 s (64 in all), and buildings within 20 m may catch fire.
- **[Inflict Earthquake](../commands/inflict-earthquake.md):** 900, after Witch Hunt. Every unit and building within 50 m loses 30-120 hit points and 200-420 structure.
- **[Inflict Wormwood](../commands/inflict-wormwood.md):** 1000, needs the Statue. A falling star lands near the target: buildings within 20 m are smashed, units within 10 m lose about 250 hit points, and every unit within 30 m loses a random part of its hit points, up to all of them.
- **[Pray](../commands/pray-at-church.md):** at a Church; peasants who go there to worship bring back more (piety 3).
- **Fizzle:** enemy curses, entangles and possessions aimed at units within 50 m of him fail.

> **wkbai.** Lightning only scratches a building (6% of its strength) but still sets it on fire; in the original game it took a flat 350.

## Strong against

- **Massed armies and towns.** Earthquake, Wormwood, Plague and the Wrath of Heaven hit everything in a wide circle, through armour.
- **Archers**: his arrow armour and front armour 5 mean an arrow does him nothing from the front. The game rates him well against archers and siege engines.

## Weak against

Damage numbers below are per blow or per shot, before research, experience, height and formation bonuses: the attack minus the armour on the side that is hit (see [Combat](../mechanics/combat.md)).

- **Cavalry and gunners.** A knight does 24 a blow, a bullet 15; he has 100 hit points.
- **Time.** The big acts need research, a Statue and a lot of prayer.
- **Crowded fights**: Plague, Fimbulwinter, Lightning, Earthquake and Wormwood hurt your own units in the area too.

## Tips

- Keep a bishop or two praying at a Cathedral all game; that is what fills the pool.
- Strike before the armies meet, while only the enemy is in the area.
- Keep bishops well behind the line: 100 hit points do not last.
