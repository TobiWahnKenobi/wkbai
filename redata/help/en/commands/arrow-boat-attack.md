---
title: Flaming Arrow Salvo
ids: [command/Arrow Boat Attack]
order: 10
---

# Flaming Arrow Salvo

## Role

The volley of the [Corsair](../units/arrow-boat.md) (ballista bolts) and the [Fire Boat](../units/fire-boat.md) (flaming arrows). The boats shoot by themselves at enemy units they see; right-click a target to make them concentrate on it.

## How it works

- **By itself:** when the boat sees enemies and isn't already shooting, it looses 3 shots every 4 s (Fire Boat every 2 s), spread over the 3 nearest enemy units in range, the least-targeted first.
- **Right-click:** the boat moves into range of the target (unit or building) and puts all 3 shots into it every 5 s, until it is dead or out of range.
- **Range:** 70 m for the Corsair, 35 m for the Fire Boat (sight range × 5 m).
- **Damage:** Corsair 26, Fire Boat 30 per shot, minus the target's armour on the side it strikes; at least 1. Buildings lose 0.1 (Corsair) or 5 (Fire Boat) structure per shot. Arrow-proof targets take only a quarter of a Corsair bolt; Fire Boat arrows ignore that.
- **Flaming arrows:** despite the fire, the Fire Boat's arrows do ordinary damage. They don't set buildings alight.
- **Aim:** each shot leaves after a random delay of up to 2 s. The scatter grows with distance, up to about 4 m either way at full range, down to 1 m with [experience](../mechanics/experience.md).
- **Ammunition:** none: the boats never run out.
- **Hold fire:** [Hold Fire](hold-fire.md) stops the volleys, even a right-click target. This order switches Hold Fire off.

## Offered by

<!-- gen:trained -->
- **Offered by:** [Corsair](../units/arrow-boat.md), [Fire Boat](../units/fire-boat.md)
- No button of its own: you give it with a right-click on the target (see [Orders](../controls/orders.md#the-right-click)).
<!-- /gen:trained -->

## Needs

<!-- gen:needs -->
- Nothing: it is there whenever the unit or building is.
<!-- /gen:needs -->

