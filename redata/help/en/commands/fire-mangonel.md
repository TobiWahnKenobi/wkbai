---
title: Fire (Mangonel)
ids: [command/Fire Mangonel]
order: 10
---

# Fire (Mangonel)

## Role

The stone throw of the [Mangonel](../units/mangonel.md) and the [Carrack](../units/mangonel-boat.md): a heavy ball that smashes buildings and hurts every unit near where it lands. Right-click an enemy unit or building.

## How it works

- **Target:** any enemy unit, building or formation. The cursor shows "too close" for a target nearer than 15 m, and the order is refused.
- **Range:** 90 m for the Mangonel, 150 m for the Carrack (sight range × 7.5 m), a little more from high ground; at least 15 m. A target further away: the engine moves closer first. If the target then gets out of range, it stops.
- **Rate:** one throw about every 7 s (Carrack 8 s), varied at random by up to 20 %.
- **Damage:** Mangonel 30, Carrack 35 to the unit hit, minus its armour; at least 1. Buildings lose 40 (Carrack 25) structure per hit. Shooting from above adds 5 % per metre of height, up to double.
- **Area:** when the ball hits a unit on land, or the ground, every unit within 8 m takes a tenth of the damage, minus its own armour. That includes your own and your allies' units. When it hits a building, the units building or repairing it take that tenth. A ball landing on a field damages the field.
- **Aim:** there is no hit roll. The scatter grows with distance, up to about 8 m either way at full range, down to 2 m with [experience](../mechanics/experience.md). The Carrack always scatters at least 5 m, even close in. A Mangonel's shot at a boat isn't scattered.
- **Ammunition:** the Mangonel uses 20 per throw from a bar of 75 (3 throws). It refills by itself, 2 points about every 2 s, and by 10 every 5 s within 25 m of a [Supply Wagon](../units/supply-wagon.md) or a barracks, armoury, fort, Watch Tower, keep, castle or palace (50 m with [Logistics](../upgrades/logistics.md)). The Carrack's throws cost nothing.
- **Hold fire:** a new Mangonel starts out holding fire: it only shoots when you order it, until you press [Fire at Will](fire-at-will.md). Giving this order switches [Hold Fire](hold-fire.md) off.

## Offered by

<!-- gen:trained -->
- **Offered by:** [Carrack](../units/mangonel-boat.md), [Mangonel](../units/mangonel.md)
- No button of its own: you give it with a right-click on the target (see [Orders](../controls/orders.md#the-right-click)).
<!-- /gen:trained -->

## Needs

<!-- gen:needs -->
- Nothing: it is there whenever the unit or building is.
<!-- /gen:needs -->

