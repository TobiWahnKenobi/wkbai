---
title: Heal
ids: [command/Monastic Knight Heal]
order: 10
---

# Heal

<!-- gen:icon -->
![Heal](game:interface/icons/sic_heal.tga)
<!-- /gen:icon -->

## Role

The [Holy Warrior](../units/monastic-knight.md) tends the wounded: he rides up to an injured friendly unit and gives back its hit points with his mana, then moves on to the next one.

## How it works

- **Give it:** press the button, then click a wounded friendly unit (the healing cursor shows only over one).
- **Target:** one of your own or an allied unit that has lost hit points. Machines and boats can't be healed (they are repaired, see [Repair (Character)](repair-character.md)), nor can units that refuse healing, such as the Archangel.
- **Range:** he comes to within 5 m.
- **Effect:** once a second he heals up to 15 hit points (25 after [Surgery](../upgrades/surgery.md)), never more than the unit has lost.
- **Cost:** 10 mana for each second of healing. With too little mana he waits beside the unit until he has 10 again.
- **Next patient:** when the unit is whole, he goes on by himself to the nearest wounded friendly unit within 30 m.
- **Recharge:** his bar holds 30 mana (three heals) and refills by 2 every 1.9 s, so one heal about every 9.5 s once it is empty. Supply wagons don't refill it.

## Offered by

<!-- gen:trained -->
- **Offered by:** [Holy Warrior](../units/monastic-knight.md)
<!-- /gen:trained -->

## Needs

<!-- gen:needs -->
- Nothing: it is there whenever the unit or building is.
<!-- /gen:needs -->

## Tips

- Research [Surgery](../upgrades/surgery.md): every heal gives 25 instead of 15 for the same mana.
