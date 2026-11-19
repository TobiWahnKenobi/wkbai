---
title: Fire (Bullet)
ids: [command/Fire Bullet]
order: 10
---

# Fire (Bullet)

## Role

The handgun shot of the [Gunner](../units/gunner.md), the [Gunner (with Bayonet)](../units/gunner-bayonet.md) and the [Dragoon](../units/mounted-hand-gunner.md). Right-click an enemy. They also fire at enemies they see by themselves, unless you told them to [Hold Fire](hold-fire.md).

## How it works

- **Target:** any enemy unit, building or formation.
- **Range:** 70 m for the gunners, 75 m for the Dragoon (sight range × 5 m), a little more from high ground. If the target is further away, the gunner walks into range first. On [Hold Ground](hold-ground.md) or in a formation it gives up instead.
- **Line of sight:** gunners never lob. If a wall, a building or a hill crest is in the way, the shot isn't fired (a sign shows the gunner can't see the target) and no ammunition is used.
- **Rate:** about one shot every 4.2 s (Dragoon 4 s), varied at random by up to 20 %.
- **Damage:** 20, minus the target's armour on the side the bullet strikes; at least 1 always gets through. +5 % for each metre the gunner stands above the target, up to double. Arrow-proof armour doesn't help against bullets. A building loses 1 per gunner's shot, 0.1 per Dragoon's.
- **Aim:** there is no hit roll: the bullet hits whatever it meets on its almost flat path. It scatters more than an arrow, up to about 5 m either way at full range, less with [experience](../mechanics/experience.md).
- **Ammunition:** 15 per shot from a bar of 250 (16 shots; Dragoon 200, 13 shots). It refills by itself, 2 points about every 2 s, and by 10 every 5 s within 25 m of a [Supply Wagon](../units/supply-wagon.md) or a barracks, armoury, fort, Watch Tower, keep, castle or palace (50 m with [Logistics](../upgrades/logistics.md)).
- **Stepping back:** while firing, the gunner checks every 2 s for enemy heavy infantry or heavy cavalry within half its range. If it finds some, it moves away to their far side. It stays put on [Hold Ground](hold-ground.md), in a formation, behind palings, behind your own heavy infantry, or when the enemy is across a cliff (see [Skirmish Mode](skirmish-mode.md)).
- Giving this order switches [Hold Fire](hold-fire.md) off.

## Offered by

<!-- gen:trained -->
- **Offered by:** [Dragoon](../units/mounted-hand-gunner.md), [Gunner (with Bayonet)](../units/gunner-bayonet.md), [Gunner](../units/gunner.md)
- No button of its own: you give it with a right-click on the target (see [Orders](../controls/orders.md#the-right-click)).
<!-- /gen:trained -->

## Needs

<!-- gen:needs -->
- Nothing: it is there whenever the unit or building is.
<!-- /gen:needs -->

