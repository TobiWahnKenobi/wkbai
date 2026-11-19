---
title: Combat
ids: [mechanics/combat, mechanic/combat, mechanic/damage, mechanic/armour, mechanic/unit-classes, mechanic/high-ground, mechanic/missiles, mechanic/ammunition, mechanic/friendly-fire, mechanic/stances, mechanic/healing, mechanic/charge]
order: 70
---

# Combat

## Unit classes

Every fighting unit belongs to a class, and the classes beat one another in a circle:

| Class | Examples | Typical role |
|---|---|---|
| **Heavy infantry** (HI) | [Man-at-Arms](../units/pikeman.md), [Imperial Pikeman](../units/man-at-arms.md) | holds a line; stops cavalry from the front |
| **Light infantry** (LI) | [Archer](../units/archer.md), [Gunner](../units/gunner.md), [Javelineer](../units/javelineer.md) | wears the enemy down from a distance |
| **Heavy cavalry** (HC) | [Knight](../units/knight.md), [Battle Rider](../units/heavy-horseman.md) | rides down missile troops; strikes flanks and rears |
| **Light cavalry** (LC) | [Squire](../units/mounted-spearman.md), [Scout](../units/scout.md) | scouting, raids, chasing |
| **Siege weapons** (SW) | [Mangonel](../units/mangonel.md), [Trebuchet](../units/trebuchet.md) | buildings, packed troops |

Each unit type also rates how well it does against each class; that rating is what the attack cursor shows.

The attack cursor tells you how a match-up looks: **green** when your unit suits this target clearly better than the target suits it, **red** when it is the other way round, plain otherwise.

## Hand-to-hand damage

Each blow does:

> (attack × weapon research + attack × height bonus + attack × experience) × formation attack bonus
> − armour on the side that was hit × armour research × formation armour bonus

and never less than 0.

- **Armour by side.** Every unit has a front, left, right and rear armour value. A blow counts the side of the victim that faces the attacker. Most units are armoured mainly at the front, so a hit in the flank or the back often goes through whole.
- **Height.** Striking down from above adds up to half the attack again; on level ground or from below, nothing.
- **Experience** adds its own share of the attack (up to +75% or +100%, see [experience](experience.md)).
- **Research** multiplies attack and armour (×1.15, ×1.3, ×1.45; [training and research](upgrades-and-research.md#armour-and-weapons)).
- **Formations** multiply both again ([formations](formations.md#bonuses)).
- **Three at a time.** At most three units fight one enemy hand to hand; others look for another victim nearby.
- **Charge.** Cavalry gallop at their target, but a charge does no extra damage by itself. In a [Wedge](formations.md) the first blow does three times the damage. Heavy cavalry that hits formed heavy infantry from the front, outside a Wedge, breaks off after one blow and rides back.
- **The Wedge's first blow** does three times the damage ([formations](formations.md)).
- **Cavalry against pikes.** Heavy cavalry that strikes formed heavy infantry from the front breaks off after its blow and rides away, unless it attacks in a Wedge.

## Missiles

- **Range** is the unit's sight in metres (sight range × 5): an archer sees and shoots 70 m. Higher ground lengthens both ([fog of war and sight](fog-of-war.md#how-far-a-unit-sees)).
- **Rate:** about one shot every 3 s for an archer, 4 s for a gunner, 7 s for a mangonel, 12 s for a trebuchet; each time a little faster or slower at random.
- **Aim, not dice.** There is no hit roll: a missile hits whatever its flight path meets. Each shot scatters around its target, more the further away and less the more experienced the shooter. An inexperienced archer at full range misses by up to 3 m, a gunner by up to 5 m. Shots at buildings go straight at them.
- **Over obstacles.** Archers and javelin throwers lob over walls and hills when they can't see their target directly, but much less accurately (three times the scatter). **Gunners need a clear line of sight** and don't fire without one.
- **Damage:** the missile's damage × research, less the armour on the side it strikes, but always at least 1. Shooting from above adds 5% a metre of height, up to double. Heavy infantry in an Orb take 30% less from missiles.
- **Minimum range.** Mangonels can't shoot closer than 15 m, trebuchets and bombards 20 m, rocket launchers 25 m.
- **Ammunition.** Missile units carry a limited supply (an archer 6 shots, a gunner 16); siege engines a few. Their supply refills by itself, slowly. A [Supply Wagon](../units/supply-wagon.md), a tower, a fort, the Barracks or the Armoury refills them faster. Flaming arrows use ammunition fast.

## Splash and friendly fire

- **Arrows, bolts and javelins** fly through trees and through your and your allies' units, but **buildings stop them**, allied ones too (see the wkbai note below).
- **Stones from mangonels, trebuchets and bombards** hurt **everyone within 8 m** of where they land, by a tenth of their damage: your own units and allies as well.
- **Rockets** ignore armour and hurt everything within 30 m, much more within 10 m.
- Being shot reveals the shooter to its target's owner for a few seconds, even in the fog of war.

## Stances

Set on the unit's command panel ([commands](../commands/index.md)):

- **[Hold Fire](../commands/hold-fire.md)** / **[Fire at Will](../commands/fire-at-will.md)**: missile units wait for an order, or shoot what comes in range.
- **[Hold Ground](../commands/hold-ground.md)**: the unit doesn't walk to its targets.
- **[Skirmish](../commands/skirmish-mode.md)**: archers and gunners step back from approaching enemies, keeping about half their shooting range away (an archer about 35 m), and shoot as they go.
- Missile units forced into hand-to-hand fight back with a dagger.

## Wounded and dying

- A unit in a formation that falls to **15%** of its hit points leaves the formation and runs to the nearest Village or Manor-type building.
- Peasants panic when hit ([peasants](peasants-and-work.md#danger)).
- There is no morale: soldiers don't rout.

## Healing

- **[Supply Wagons](../units/supply-wagon.md)** heal and resupply nearby allied units.
- **[Monks](../units/monk.md)** and some holy units heal.
- A slain sheep leaves mutton: a hurt unit can eat it, 5 hit points a bite.

![Archers in a line firing down on cavalry from a hill](shot:mech-combat-archers-hill)

> **wkbai.** In this version of wkbai, arrows, bolts and javelins can still hit allied units and fly into farms, and a blocked path doesn't yet make soldiers attack the wall in their way. Matching the original here is work in progress. Kills are counted per unit for [unit history](unit-history.md).
