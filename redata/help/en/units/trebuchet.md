---
title: Trebuchet
ids: [unit/Trebuchet, building/Trebuchet, command/Build Trebuchet]
order: 10
---

# Trebuchet

<!-- gen:icon -->
![Trebuchet](game:interface/icons/characters/unaligned_trebuchet.tga)
<!-- /gen:icon -->

<!-- gen:facts -->
```facts
blueprint: Trebuchet
```

<!-- facts:static -->
| | |
|---|---|
| Cost | 200 food, 500 wood |
| Build time | 40 s |
| Food upkeep | 2 |
| Hit points | 300 |
| Armour | 5 (front 5, sides 5, rear 5) |
| Melee attack | 5 every 2 s |
| Missile attack | 75 every 12 s, range 160 m |
| Damage to buildings | 65 per missile |
| Speed | 2 |
| Sight | 12 (60 m) |
<!-- /facts:static -->
<!-- /gen:facts -->

## Role

The trebuchet is a huge siege catapult: 75-point stones (65 to buildings) about every 12 seconds, from 20 m out to 160 m. A [Sapper](sapper.md) builds it for 200 food and 500 wood after the [Trebuchet](../upgrades/trebuchet.md) research at the [Observatory](../buildings/observatory.md); building it takes 40 seconds. A trebuchet built this way has 400 hit points, armour 5 all round, and **can't move**.

![A trebuchet throwing at an enemy tower](shot:gamedata-trebuchet)

## Abilities

- **[Fire (Trebuchet)](../commands/fire-trebuchet.md):** it doesn't walk to a target that is out of range, and won't fire at anything closer than 20 m. Every stone also hurts all units within 8 m of where it lands, yours and your allies' included, by a tenth of its damage less each one's armour.
- **Ammunition:** a full charge bar holds four stones; the bar refills by about 1 a second, and much faster near a [Supply Wagon](supply-wagon.md), so after the first four it throws about one stone every 33 seconds.
- **[Greek Fire](../upgrades/greek-fire.md) and [Explosives](../upgrades/grapeshot.md):** after these researches it throws fire pots or grapeshot instead, one shot per full bar. Greek fire sets buildings within 10 m alight.
- Arrows do it only a quarter of their damage.
- Deleting a trebuchet gives back half its wood and food, less if it is damaged.

## Strong against

- **Towers, forts and walls**: its 160 m range outreaches them.
- **Other siege engines**, the game's preferred target for siege weapons.
- **Massed troops**: 67 a stone through a [Spearman](spearman.md)'s front, 60 through an [Imperial Pikeman](man-at-arms.md)'s.

## Weak against

- **Cavalry**: a [Knight](knight.md) does 24 a blow and it can't run or shoot back at close range.
- **Gunners**: bullets do 15 a shot.
- **Rocket launchers** (200 m) and enemy trebuchets outrange or match it.

## Trained at / Built by

<!-- gen:trained -->
- **Built by:** [Sapper](sapper.md) (the engine is put up on the spot like a building, then rolls out)
<!-- /gen:trained -->

## Needs

<!-- gen:needs -->
- [Trebuchet](../upgrades/trebuchet.md)
<!-- /gen:needs -->

## Leads to

<!-- gen:leads -->
- Nothing: it unlocks no building, unit or research.
<!-- /gen:leads -->

## Commands

<!-- gen:commands -->
- [Capture](../commands/capture.md)
- [Halt](../commands/halt.md)
- [Enter Dark Portal](../commands/enter-dark-portal.md)
- [Hold Fire](../commands/hold-fire.md)
- [Fire at Will](../commands/fire-at-will.md)
- [Fire (Trebuchet)](../commands/fire-trebuchet.md)
<!-- /gen:commands -->

## Tips

- Its range lets it destroy towers and forts without coming within their reach.
- It can't move once built: have the sapper build it where you mean to use it, within 160 m of the target.
- Leave spearmen with it; it is helpless against horsemen.
