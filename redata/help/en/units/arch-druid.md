---
title: Arch Druid
ids: [unit/Arch Druid, command/Spawn Arch Druid]
order: 10
---

# Arch Druid

<!-- gen:icon -->
![Arch Druid](game:interface/icons/characters/Pagan_Arch_Druid.tga)
<!-- /gen:icon -->

<!-- gen:facts -->
```facts
blueprint: Arch Druid
```

<!-- facts:static -->
| | |
|---|---|
| Cost | 250 food, 150 gold |
| Food upkeep | 3 |
| Hit points | 60 |
| Armour | 5 (front 2, sides 0, rear 0) |
| Melee attack | 10 every 2 s |
| Speed | 5 |
| Sight | 7 (35 m) |
<!-- /facts:static -->
<!-- /gen:facts -->

## Role

The arch druid is the senior Pagan magic user from the [Temple of the Moon](../buildings/black-mass.md), one per player. He wakes elementals out of trees, rocks and gold, turns enemies into trees, entangles them, curses them and hardens your troops against arrows. He is a poor fighter: 60 hit points, armour 2 at the front and a 10-point blow.

## Trained at / Built by

<!-- gen:trained -->
- **Trained at:** [Temple of the Moon](../buildings/black-mass.md)
<!-- /gen:trained -->

## Needs

<!-- gen:needs -->
- Pagan or neutral side only: the button goes once you build a [Church](../buildings/church-stone.md) (the Imperial path).
<!-- /gen:needs -->

## Leads to

<!-- gen:leads -->
- **Can turn into or call up:** [Wood Elemental](wood-elemental.md), [Stone Elemental](stone-elemental-large.md), [Gold Elemental](gold-elemental-large.md)
<!-- /gen:leads -->

## Commands

<!-- gen:commands -->
- [Awaken Wood Elemental](../commands/awaken-wood-elemental.md)
- [Awaken Stone Elemental](../commands/awaken-stone-elemental.md)
- [Awaken Gold Elemental](../commands/awaken-gold-elemental.md)
- [Greengrow](../commands/greengrow.md)
- [Barkskin](../commands/barkskin.md)
- [Entangle](../commands/entangle.md)
- [Capture](../commands/capture.md)
- [Halt](../commands/halt.md)
- [Create Dark Portal](../commands/dark-portal.md)
- [Interrogate](../commands/interrogate.md)
- [Burn Unbeliever](../commands/burn-unbeliever.md)
- [Repair (High Priestess)](../commands/high-priestess-repair.md)
- [Enter Dark Portal](../commands/enter-dark-portal.md)
- [Curse](../commands/curse.md)
- [Attack](../commands/attack.md)
- [Convert Troops to Peasants](../commands/military-unit-convert-to-peasant.md)
- [Garrison](../commands/garrison.md)
- [Board Vessel](../commands/board-vessel.md)
- [Resupply Ammunition](../commands/resupply.md)
- [Guard Next Unit Selected](../commands/guard.md)
- [Flee](../commands/run-away.md)
- [Move](../commands/move.md)
<!-- /gen:commands -->

## Abilities

His spells draw on a charge bar of 200 that starts empty and fills by itself at about 1 point a second, so about 190 s from empty to full. Supply Wagons do not fill it. Spells reach 70 m. Every elemental he has alive slows the bar: a wood elemental by a quarter, a stone elemental by a third, a gold elemental by half. With 4 wood or 2 gold elementals it stops.

- **[Awaken Wood Elemental](../commands/awaken-wood-elemental.md):** 75 charge, on any tree. The tree shrinks for 15 s and becomes a [Wood Elemental](wood-elemental.md): 200 hit points, 40-point blows, armour 15 all round, speed 5. No building needed.
- **[Awaken Stone Elemental](../commands/awaken-stone-elemental.md) / [Awaken Gold Elemental](../commands/awaken-gold-elemental.md):** on a rock outcrop or a gold deposit, which is used up. The cost is the stone or gold left in it, so a full 200 deposit needs a full bar. The bigger the deposit, the bigger the elemental: a full one has 500 hit points and 100-point blows. Needs an [Arcanum](../buildings/arcanum.md).
- **[Greengrow](../commands/greengrow.md):** 100 charge, 35 m. Turns an enemy unit and up to 3 of its comrades within 7 m into trees. Needs [The Wickerman](../buildings/wickerman.md). The Spider Demon, the DeathWyrm, the Behemoth, Abaddon and the druid himself are immune.
- **[Entangle](../commands/entangle.md):** 70 m. Enemy units within 20 m of the target stop dead and lose 4 hit points every 3 s. Units walking into the area are caught too. It lasts 30 s; units that get out move again.
- **[Barkskin](../commands/barkskin.md):** on your own unit within 70 m, and the units next to it within 20 m. For 20 s arrows and javelins do only a quarter of their damage to them. It costs 5 charge per unit, but needs 150 in the bar to cast. Each unit can receive it only once a game.
- **[Curse](../commands/curse.md):** 45 charge. The target and its 7 nearest comrades within 10 m do half damage, hand to hand and with missiles, for 30 s. It also removes a Bless.
- **[Create Dark Portal](../commands/dark-portal.md):** 60 charge. Opens a passage 10 m in front of him to a possessed peasant of yours (a [Succubus](succubus.md) makes those), which becomes the exit. It stays open about 120 s, or until units with 1200 hit points in all have gone through. Needs an [Arcanum](../buildings/arcanum.md).
- **[Burn Unbeliever](../commands/burn-unbeliever.md):** walks up to a peasant who is preaching heresy and burns him.
- **Fizzle:** enemy curses, entangles, possessions and summons aimed at units within 50 m of him fail.

When the druid dies, his elementals turn back: wood elementals into trees, stone and gold elementals into small heaps of stone or gold.

## Strong against

- **Massed armies.** Entangle and Greengrow work on groups, and Curse halves a whole block's damage.
- **Siege engines and slow troops**, which cannot get out of an Entangle; the game rates him well against siege engines.
- **Archers**, through Barkskin on your own troops, and through his own armour against arrows: an arrow does him only about 2 damage.

## Weak against

- **Anything that reaches him.** 60 hit points and armour 2: a knight does 27 a blow and a gunner's bullet 18.
- **Gunners and cavalry**: bullets ignore his arrow armour, and riders get to him before his spells do.
- **Enemy clerics.** A [Priest](priest.md), a [Bishop](bishop.md), a king piece, a church or a temple within 50 m of his target makes Curse and Entangle fail, and the charge is lost. An [Inquisitor](inquisitor.md) does him double damage with holy fire, and can exorcise him.

## Tips

- You can have only one: keep him behind your line and out of gunner range.
- Wake a wood elemental early; it is the cheapest and needs no research.
- Entangle the enemy, then hit them with missiles or charge them while they stand still.
- Greengrow and the stone, gold and portal spells need opposite buildings ([The Wickerman](../buildings/wickerman.md) shuts out the [Arcanum](../buildings/arcanum.md) and the other way round): choose your path.
