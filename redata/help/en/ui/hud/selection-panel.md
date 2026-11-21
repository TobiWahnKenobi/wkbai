---
title: Selection panel
ids: [ui/hud/selection-panel, ui/hud/selection-panel/portrait#the-portrait, ui/hud/selection-panel/hit-points#hit-points, ui/hud/selection-panel/name#name-age-and-kills, ui/hud/selection-panel/experience#experience, ui/hud/selection-panel/stats#the-stat-rows, ui/hud/selection-panel/formation#over-the-panel]
order: 30
---

# Selection panel

![The selection panel with a spearman selected](shot:hud-selection-panel-soldier)

The round panel in the lower left corner shows the **first selected** unit or building. With nothing selected, it shows **whatever is under the mouse**, your own or an enemy's, so you can size up an enemy soldier by pointing at it.

The [command buttons](command-panel.md#knuckle-buttons) for the selection sit around the panel's rim.

## The portrait

The panel's window shows the unit or building as a small 3D model, turning slowly (once every 12 seconds), in the middle of the glass rose. A soldier is centred on its body, so a long spear or a banner doesn't push it aside. The window itself is drawn as stained glass.

> wkbai: the portrait and the glass need the enhanced graphics; with the basic renderer the window shows the panel's art only.

## Hit points

Under the heart: **hit points now / most hit points**, for example "45/60". For a building these are its structural integrity, which falls as it is damaged and burns.

## Name, age and kills

wkbai names the soldiers it keeps a history of:

- **the name** under the unit's picture in the window, with a shadow so it reads over the glass. A **hero** has its name in gold, with "born ..." and its birth name under it;
- under the hit points, **its age and its kills**: "Age 4:12, 7 kills +2". The age is in game minutes, or in days when day and night are on, or in years (from 25) with seasons. "+2" counts the buildings it has destroyed.

The age is always the unit's whole age: a saved game keeps it. A unit from a save made before wkbai kept these histories counts as born when the game began.

Peasants have no history. See [Unit history and heroes](help:mechanic/unit-history).

## Experience

A soldier's experience fills an **arc of 16 lit segments** round the right of the window. Each segment stands for a sixteenth of the most experience it can gain.

## The stat rows

The rows at the left, an icon and a number each, depend on what is shown:

| Shown | Rows |
|---|---|
| **A soldier** | Shield: its front armour. Sword: its hand-to-hand damage (left out for a pure missile unit). Bow: its missile damage, when it has one. |
| **A merchant** | Trade goods and gold carried. |
| **A peasant or a cart** | Food, wood, gold and stone it carries. |
| **A building** | Hit points only. |

**Upgrades:** where your armour or weapon research adds to a soldier's value, the number shows its share: "8+2" is 8 of its own and 2 from the upgrade.

## Over the panel

A short line just above the panel:

- **a formation** (or one of its members): "Formation: 212 kills · heroes: Sigurd the Valiant";
- **several units selected:** the one with the most kills, "Best: Sigurd the Valiant, 64 kills".

## What it doesn't show

A disguised enemy (a spy dressed as one of your peasants) shows nothing: the panel would give it away.

![The panel with a peasant carrying wood](shot:hud-selection-panel-peasant)
