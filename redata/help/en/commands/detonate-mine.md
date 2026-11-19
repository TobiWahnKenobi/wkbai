---
title: Explosive Mine
ids: [command/Detonate Mine]
order: 10
---

# Explosive Mine

<!-- gen:icon -->
![Explosive Mine](game:interface/icons/SIC_Detonate.tga)
<!-- /gen:icon -->

## Role

Sets off the [Explosive Mine](../buildings/explosive-mine.md) your [Sapper](../units/sapper.md) has laid.

## How it works

- **Button:** it shows once the sapper's mine is laid. Each sapper has one mine at a time.
- **What happens:** the sapper lights a fuse. After 1 second a burning fuse appears 2 m in front of him and runs along the ground to the mine. When it stops within 10 m of the mine, the mine explodes.
- **Damage:** every unit and building within 30 m, yours and your allies' too. The closer, the harder: next to the mine about 480 damage to units and 300 to buildings, falling to nothing at 30 m.
- **Sight:** the blast lights up 40 m around it for 5 seconds.
- **Failure:** if the fuse can't get within 10 m of the mine, it goes out and the button comes back.
- **Afterwards:** the sapper can lay a new mine.
- **Counter:** an enemy sapper can [Defuse Mine](defuse-mine.md) first. If the mine is removed while the fuse runs, the fuse goes out too.

## Offered by

<!-- gen:trained -->
- **Offered by:** [Sapper](../units/sapper.md)
<!-- /gen:trained -->

## Needs

<!-- gen:needs -->
- Nothing: it is there whenever the unit or building is.
<!-- /gen:needs -->

## Tips

- Keep the sapper and your own troops more than 30 m from the mine when you set it off.
