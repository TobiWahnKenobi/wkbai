---
title: Peasants and their work
ids: [mechanics/peasants-and-work, mechanic/peasants, mechanic/gathering, mechanic/farming, mechanic/woodcutting, mechanic/mining, mechanic/shopping, mechanic/piety, mechanic/panic]
order: 20
---

# Peasants and their work

[Peasants](../units/peasant.md) do all the work of your town. A peasant costs 50 food and eats nothing, so the only limit on how many you keep is your population limit.

## Giving work

Select peasants and right-click what they should work on. The cursor shows the job:

| Right-click on | Job | What they do |
|---|---|---|
| a farm | [Farm](../commands/farm.md) | work the field, carry the food home |
| a tree | [Gather Materials](../commands/chop-tree.md) | cut wood, then the next tree nearby |
| a stone outcrop | [Mine for Stone](../commands/mine.md) | cut stone (each stone = 2 materials) |
| a gold outcrop | [Mine Gold](../commands/mine-gold.md) | mine gold |
| a shop | [Trade at Shop](../commands/shop.md) | turn materials into gold ([resources](resources.md#gold)) |
| a foundation | [Build](../commands/build.md) | raise the building |
| a damaged building | [Repair](../commands/repair.md) | repair it, at a cost |
| a burning building | [Extinguish Fire](../commands/extinguish.md) | put the fire out |
| a sack | [Pick Up Sack](../commands/peasant-pick-up-sack.md) | carry its goods home |

- **One farmer a farm.** A second peasant sent to a busy farm looks for a free one.
- **After building**, a builder goes on to the next unfinished foundation within 50 m; a peasant who built a sawmill goes to chop the nearest tree.
- **New peasants** go to the building's rally point. If you set the rally point on a tree, a farm or a mine, they start working there at once.
- **Idle peasants** stand and doze. The idle count and a button that selects them are on the peasant activity panel above the minimap ([peasant activity](../ui/hud/peasant-activity.md)).

## How fast they work

- Each stroke of the axe, pick or hoe adds 1 to the load (a farmer 0.5 to 1.5 by the farm's ground; see [farms and the ground](resources.md#farms-and-the-ground)).
- The load is full at **10**. The peasant then walks to deliver, and walks back. A short way home is the biggest gain: build a [Village](../buildings/village-wood.md) near the work, or a [Stone Warehouse](../buildings/warehouse-stone.md).
- A [Windmill](../buildings/windmill.md) or [Sawmill](../buildings/sawmill.md) within 40 m of the way home doubles a farmer's or woodcutter's load (×2.5 after the advanced research).

## Church and maypole

Every seventh delivery is a day of rest. On the way back from it, a peasant visits a place of worship of yours or an ally's within 50 m, if there is one:

- **[Church](../buildings/church-stone.md):** he prays, and from then on carries **12** instead of 10. The more a church is prayed at ([Pray](../commands/pray-at-church.md)), the more its piety, and up to 3 more on top.
- **[Maypole](../buildings/maypole.md):** he dances, and from then on farms at **1.5 times** the rate, but carries the plain 10.

The last visit counts: a peasant who prays after dancing loses the dance's bonus, and the other way round.

## Danger

- **Panic.** A peasant who is hit while not fighting runs about 15 m away from the attacker, and goes back to work after about 5 seconds.
- **Fighting back.** Peasants can attack, weakly (5 damage a blow, 80 hit points).
- **Militia.** [Conscript Peasant into Militia](../commands/convert-to-militia.md) turns peasants into [Militia](../units/militia.md) for a fight, and back again afterwards. A Manor-type building can call up all your peasants at once ([Conscript All Peasants](../commands/conscript-all-peasants.md)).
- **Shelter.** Peasants can [garrison](../commands/garrison.md) in your Manor, Keep, Castle or Palace (50 places).

![Peasants farming, chopping and carrying, with the peasant activity panel open](shot:mech-peasants-at-work)

> **wkbai.** With wkbai's day and night or seasons on, peasants can sleep indoors at night and farmers spend the winter inside: see [peasants at night and in winter](peasant-lodging.md).
