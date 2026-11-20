---
title: Explosive Cart
ids: [unit/Explosive Cart, command/Spawn Explosive Cart]
order: 10
---

# Explosive Cart

<!-- gen:icon -->
![Explosive Cart](game:interface/icons/characters/Tech_Explosive_Cart.tga)
<!-- /gen:icon -->

<!-- gen:facts -->
```facts
blueprint: Explosive Cart
```

<!-- facts:static -->
| | |
|---|---|
| Cost | 200 food, 50 gold |
| Food upkeep | 3 |
| Hit points | 200 |
| Armour | 5 (front 5, sides 3.75, rear 0.5) |
| Melee attack | 10 every 2 s |
| Speed | 5 |
| Sight | 7 (35 m) |
<!-- /facts:static -->
<!-- /gen:facts -->

## Role

The explosive cart is a moving bomb: drive it next to a target and [Detonate](../commands/trigger-explosives.md) it. It costs 200 food and 50 gold at the [Armoury](../buildings/armoury-stone.md) and needs an [Observatory](../buildings/observatory.md). It has 200 hit points and arrow armour, but no attack.

## Trained at / Built by

<!-- gen:trained -->
- **Trained at:** [Armoury](../buildings/armoury-stone.md)
<!-- /gen:trained -->

## Needs

<!-- gen:needs -->
- building a [Observatory](../buildings/observatory.md)
<!-- /gen:needs -->

## Leads to

<!-- gen:leads -->
- Nothing: it unlocks no building, unit or research.
<!-- /gen:leads -->

## Commands

<!-- gen:commands -->
- [Detonate](../commands/trigger-explosives.md)
- [Board Vessel](../commands/board-vessel.md)
- [Resupply Ammunition](../commands/resupply.md)
- [Guard Next Unit Selected](../commands/guard.md)
- [Flee](../commands/run-away.md)
- [Move](../commands/move.md)
<!-- /gen:commands -->

## Abilities

- **[Detonate](../commands/trigger-explosives.md):** lights a 30-second fuse; the charge bar counts it down. While the fuse burns the cart speeds up from 5 to 8. Then it explodes and is gone.
- **The blast** hits every unit and building within 30 m, yours included, through any armour: up to 480 hit points and 300 structure right next to it, falling to nothing at 30 m. The ring shown when you select it is only 20 m.
- **Flaming arrows** from archers or Fire Boats set it off at once, two times in three, lit or not.
- **On a ship:** if it explodes aboard a [Cog](cog.md), the cog is destroyed too.
- **Arrow armour:** arrows and javelins do only a quarter of their damage.

## Strong against

- **Gatehouses, walls and buildings**: 300 structure at the centre.
- **Packed formations**: 480 at the centre kills anything except the biggest monsters.

## Weak against

Damage numbers below are per blow or per shot, before research, experience, height and formation bonuses: the attack minus the armour on the side that is hit (see [Combat](../mechanics/combat.md)).

- **Being caught on the way.** A knight does 24 a blow, and it cannot fight back.
- **Flaming arrows**, which can blow it up among your own troops.

## Tips

- Light the fuse about 30 s before it reaches the target, and keep your own troops more than 30 m away.
- Use it on gatehouses and packed formations.
- Keep it away from enemy archers with flaming arrows.
