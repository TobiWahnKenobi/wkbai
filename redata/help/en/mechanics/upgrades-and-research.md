---
title: Training and research
ids: [mechanics/upgrades-and-research, mechanic/training, mechanic/research, mechanic/upgrades, mechanic/queue, mechanic/armour-and-weapons]
order: 65
---

# Training and research

## Training units

Select a building and click a unit's button on the command panel ([command panel](../ui/hud/command-panel.md)).

- **The price is paid on the click.** Each click queues one more unit behind the others. Cancelling a queued unit (click its icon in the queue) gives the full price back.
- **Button states:**
  - lit: you can train it now;
  - grey: you can't afford it yet, or you are at your population limit; it trains as soon as you can;
  - ghost: you lack a building or research (the tooltip says which), or your [alignment](alignment.md) rules it out;
  - with a warning: training it would take your army over your food support ([food and upkeep](population-and-food.md)). You may still train it.
- **Training time.** A unit grows in its building until it reaches its full hit points, a fixed amount each moment, so a big unit takes longer than a small one. The building's progress bar shows it.
- **The population limit** pauses the queue while you are at it ([population](population-and-food.md#the-population-limit)).
- **Rally point.** Right-click the ground (or a tree, farm, mine or Training Dummy) with the building selected: new units walk there, and start that job if it is one.

## Research

Some buildings offer **research** (also called upgrades) instead of units. A research is bought once per player and then holds for all your units, including those trained later.

- Click its button; the price is paid at once and the building's bar fills, one step a second, until it is done. A gong sounds when it completes.
- If the building is destroyed before the research finishes, it can be started again elsewhere.
- Research is shown on the button's tooltip with what it does. The [upgrades](../upgrades/index.md) pages list every one.

## Armour and weapons

The [Blacksmith](../buildings/blacksmith.md) improves each class's armour and weapons in three tiers. Each tier multiplies the class's attack or armour:

| Tier | Multiplier | Opened by |
|---|---|---|
| 1 | ×1.15 | the Blacksmith itself |
| 2 | ×1.3 | a [Monastery](../buildings/monastery.md), [Temple](../buildings/temple.md) or [Sawmill](../buildings/sawmill.md) |
| 3 | ×1.45 | the [University](../buildings/university.md) |

The four classes are researched separately:

| Class | Armour | Weapons |
|---|---|---|
| Heavy infantry | [Scalemail](../upgrades/heavy-infantry-scalemail.md), [Chainmail](../upgrades/heavy-infantry-chainmail.md), [Plate Armour](../upgrades/heavy-infantry-plate-armour.md) | [Damascene Blade](../upgrades/heavy-infantry-damascene-blade.md), [Pattern Welded Blade](../upgrades/heavy-infantry-pattern-welded-blade.md), [Tempered Steel Blade](../upgrades/heavy-infantry-tempered-steel-blade.md) |
| Light infantry | [Leather Armour](../upgrades/light-infantry-leather-armour.md), [Hardened Leather](../upgrades/light-infantry-hardened-leather-armour.md), [Ringmail](../upgrades/light-infantry-ringmail-armour.md) | [Iron-tipped](../upgrades/light-infantry-iron-tipped-weapon.md), [Steel-tipped](../upgrades/light-infantry-steel-tipped-weapon.md), [Armour Piercing](../upgrades/light-infantry-armour-piercing-weapon.md) |
| Heavy cavalry | [Leather Barding](../upgrades/heavy-cavalry-leather-barding.md), [Mail Barding](../upgrades/heavy-cavalry-mail-barding.md), [Plate Barding](../upgrades/heavy-cavalry-plate-barding.md) | [Horse Shoe](../upgrades/horse-shoe.md), [Stirrup](../upgrades/stirrup.md), [Warhorse](../upgrades/warhorse.md) |
| Light cavalry | [Quilted Hauberk](../upgrades/light-cavalry-quilted-hauberk.md), [Mail Barding](../upgrades/light-cavalry-mail-barding.md), [Plate Barding](../upgrades/light-cavalry-plate-barding.md) | [Iron-tipped](../upgrades/light-cavalry-iron-tipped-weapon.md), [Steel-tipped](../upgrades/light-cavalry-steel-tipped-weapon.md), [Armour Piercing](../upgrades/light-cavalry-armour-piercing-weapon.md) |

The multipliers apply in the damage sum ([combat](combat.md#hand-to-hand-damage)): better weapons raise the attack before armour; better armour raises the armour that is taken off.

## Economy research

| Research | Where | Effect |
|---|---|---|
| [Heavy Plough](../upgrades/heavy-plough.md), [Horse Harness](../upgrades/horse-harness.md), [Crop Rotation](../upgrades/crop-rotation.md) | [Guildhall](../buildings/guildhall-stone.md) | food support per farmer 6, 10, 15 ([food](population-and-food.md#the-food-support)) |
| [Advanced Windmill](../upgrades/advanced-windmill.md), [Advanced Sawmill](../upgrades/advanced-sawmill.md) | [University](../buildings/university.md) | the mill's bonus ×2.5 instead of ×2 |
| [Reconnaissance](../upgrades/reconnaissance.md) | [Trading Post](../buildings/trading-post.md) | your allies see what your units see ([fog of war](fog-of-war.md#allies)) |

![A Blacksmith's command panel with the armour and weapon research](shot:mech-research-blacksmith)
