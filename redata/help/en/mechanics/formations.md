---
title: Formations
ids: [mechanics/formations, mechanic/formations, mechanic/line, mechanic/column, mechanic/orb, mechanic/wedge, mechanic/damage-sharing]
order: 90
---

# Formations

Eight or more soldiers of one type can form up as a block. A formation moves together, fights together and shares the blows it takes. How to give the order is in [the formation command](../commands/formation.md); this page is about the rules.

## Forming up

- **At least 8** units. Foot and horse, archers and pikes may be mixed in one formation ([mixed formations](#mixed-formations)).
- **Four shapes:** Line, Column, Orb and Wedge. The Wedge is for heavy units only.
- **Joining:** more units of the type can [join](../commands/join-formation.md) a formation; it re-forms with them.
- **Breaking up:** a formation that falls below **8** members disbands into single units.

| Shape | How it stands |
|---|---|
| **Line** | at least two ranks, up to 12 a rank |
| **Column** | about ten ranks deep, narrow |
| **Wedge** | one at the point, then 2, 3, 4 ... |
| **Orb** | rings facing out; the outer ring's radius is 12 m or more |

## Bonuses

A formation multiplies its members' attack and armour, and changes how fast they march:

| Shape | Attack | Armour | Speed (foot / horse) | Shares a hit with | Total damage taken |
|---|---|---|---|---|---|
| Line | ×1.2 | ×1.2 | −10% / −10% | 5 nearest | 60% |
| Column | ×0.7 | ×0.5 | +40% / ±0 | 2 nearest | 20% |
| Wedge | ×1.5 | ×0.5 | ±0 / +10% | 4 nearest | 40% |
| Orb | ×1.0 | ×1.5 | ±0 / ±0 | 8 nearest | 80% |

- **Armour above 1 protects more:** armour is taken off the attack, so ×1.5 armour takes half as much again off every blow.
- **Shared damage.** A hit of more than 4 hit points on a formed member isn't taken by that member alone: the stated share of it is spread evenly over the nearest members, the one hit included. A Line hit for 10 loses 6 hit points in all, 1.2 each over 5 men. A formation that isn't formed up (still marching into shape) doesn't share.
- **The Wedge's first blow** does three times the damage, once per Wedge order.
- **Heavy infantry in an Orb** take 30% less from missiles, and an Orb answers attacks from every side.
- **Experience** is the formation's average for everyone below it ([experience](experience.md)).

## Fighting

- **Ordered to attack**, a formation marches to its target as a block and attacks as soon as it arrives: each member picks a victim within 40 m (8 m for an Orb) and goes for it, riding or marching out to it when it is not yet in reach. Every member that has no victim of its own yet is sent at one, so the whole formation fights, in a Line, Column or Orb as in a Wedge, not only the first few to make contact. Every 2.5 seconds stragglers more than 30 m away are called back, and when the fight is over the block re-forms where it stands.
- **Not ordered**, a hand-to-hand formation doesn't charge what comes near. It reacts to blows:
  - a blow on a member's **front** (or anywhere on an Orb) makes the whole formation fight back;
  - a blow on a member's **flank or rear** makes that member leave the formation and fight alone; after 5 seconds the rest of the formation takes up the fight too. The member rejoins when it is idle.
- **Missile formations** shoot from their places and never walk out of the block to shoot. They fire at anything in range while standing, and ignore Hold Fire. Attacked hand to hand, they fight back.
- **A Wedge's charge** works the same: the riders at the tip strike first, and the rest join in within a few seconds, also when the Wedge is ordered to move through the enemy.
- **Riding into the enemy:** a rider that ends up among enemy soldiers, for example after a Wedge rides through a line of Javelineers or other skirmishers, fights them instead of standing among them.
- **After a change of formation**, the formation counts as idle once it has formed up, so archers and other missile troops in it shoot at the enemies they see again.
- **Light troops in formation don't skirmish.**
- **Badly hurt members** (15% of their hit points or less) leave and run home.
- There is no morale: a formation doesn't rout.

## Moving

- A formation keeps together at about the pace of its slowest member, changed by the shape (table above).
- Over rough ground members may fall out of their places for a while and catch up.

## Mixed formations

In the original, a mixed formation took its spacing from whichever unit came first and put each member where it happened to stand, so archers ended up in the front rank and knights crowded spearmen.

With wkbai's **Improved mixed formations** (on by default, [the option](../options/controls/mouse.md#improved-mixed-formations)):

- the places are spaced for the widest member, but never more than 25 m from the centre, so the ends stay in the fight;
- in a Line, Column or Wedge, horsemen take the outer files, hand-to-hand troops the front rows and archers and gunners the rear;
- whether the formation is in reach of a target is judged for each kind of member, and when the formation is sent to fight, its archers and gunners shoot instead of drawing swords.

The formation marches at its slowest member's pace either way.

![A Line of pikemen holding against cavalry, with an Orb behind](shot:mech-formations-line-orb)

> **wkbai.** In this version of wkbai a standing formation without an order turns to face an attack from its flank or rear, when the attackers are more than 50° off its front: wkbai's own rule. The original never turns a formation; its members only leave it. This version also doesn't share damage between members yet: each member takes its own hits in full. Both are being brought in line with the original. Earlier versions of wkbai let only the first three members of a formation fight while the rest stood by; now every member fights, as in the original.
