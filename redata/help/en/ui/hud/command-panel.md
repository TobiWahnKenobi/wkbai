---
title: Command buttons
ids: [ui/hud/commands, ui/hud/commands/knuckles#knuckle-buttons, ui/hud/commands/button-states#how-a-button-looks, ui/hud/commands/click#clicking-a-button, ui/hud/build-menu#build-menu, ui/hud/build-menu/military#build-menu, ui/hud/build-menu/alignment#build-menu, ui/hud/build-menu/economic#build-menu, ui/hud/queue#train-and-research, ui/hud/queue/train#train-and-research, ui/hud/queue/research#train-and-research, ui/hud/rally-icon#new-units-order, ui/hud/formation-buttons#formation-buttons]
order: 40
---

# Command buttons

What you can tell the selection to do appears as square buttons. Where they appear depends on what is selected:

| Selected | Buttons |
|---|---|
| Units or a formation | Their orders round the [selection panel's rim](#knuckle-buttons). |
| A unit that can build (a peasant) | Also the [build menu](#build-menu) at the upper left. |
| A building | Its [train and research columns](#train-and-research) at the upper left, and its other orders round the rim. |
| A formation | Also the round [formation pad](#formation-buttons) in the panel's window. |

The buttons always belong to the **first** selected object. With several units selected, a button's order goes to every selected unit that has it.

In a replay there are no command buttons: a replay takes no orders.

## How a button looks

- **Normal:** the order can be given.
- **Lit:** the mouse is over it; **pressed in** while you hold the button down.
- **Greyed:** it can't be given now (too little gold, the wrong time, a building missing). The tooltip says why.
- **Waiting:** the order is possible but not yet (a spell recharging, for example), shown by the button's own "wait" picture.

The tooltip is the game set's hint for the command: the first reason it can't be given, else a warning, else what it does.

## Clicking a button

| Click | Ordinary orders | Train (in a building) | Research |
|---|---|---|---|
| **Left** | Give it now, dropping what the units were doing | Train one more | Start it (or queue it) |
| **Shift + left** | Add it after their present orders | Train **ten** more | – |
| **Ctrl + left** | Do it first, then go back to the present orders | – | – |
| **Right** | Take it off: cancel that order | Take one off the queue | Cancel it |
| **Shift + right** | – | Take **ten** off | – |

Some buttons don't give an order straight away:

- **Build buttons** start [placing the building](../../controls/building-placement.md).
- **Formation** puts the selected soldiers into a new [formation](../../controls/formations.md). It needs at least 8.

Spells and acts of God are only given through their buttons: the right-click never picks them, so a priest's right-click on an enemy won't waste a spell.

## Knuckle buttons

The orders of the selection sit on the six "knuckles" round the rim of the lower left panel: the first at the left, then over the top and down the right side. A seventh and further ones continue in a row to the right of the sixth.

![Command buttons round the selection panel, a spearman selected](shot:hud-knuckle-buttons)

## Build menu

![The build menu, the Economic tab](shot:hud-build-menu)

A unit that can build gets a framed menu at the upper left, under the top bar, with three tabs at its top:

- **Military:** barracks, stables, towers and the like;
- **Alignment:** the buildings of your faith (civic buildings);
- **Economic:** houses, farms, storage and trade (the tab open at first).

The buildings of the open tab are in two columns. Click one to start [placing it](../../controls/building-placement.md). The tab stays as you left it for the next builder.

## Train and research

![A barracks training spearmen, with its queue and progress bar](shot:hud-train-queue)

A selected building shows two columns at the upper left:

- **left: the units it trains** (the "Spawn" orders);
- **right: what it researches.**

On a button with something queued:

- **the number** at its lower left is how many are waiting, the one being made included;
- **a red bar** across its top shows how far along the one in work is.

Training always joins the end of the queue. See [Training and research](help:mechanic/training) for costs and times.

### New units' order

When a building has an order for the units it makes (a rally point, or "go and chop wood there"), the order's picture shows in the bottom left corner of the screen. You give it by right-clicking the ground or a target with the building selected: see [Orders](../../controls/orders.md#a-buildings-new-units).

## Formation buttons

![The formation pad in the selection panel, a Line selected](shot:hud-formation-buttons)

With a formation selected, the window of the [selection panel](selection-panel.md) holds the formation pad, as in the original game: a round pad split into four quarters, each showing its shape in dots.

| Quarter | Shape |
|---|---|
| Top left | **Column**: dots in a vertical line |
| Top right | **Line**: dots in a horizontal row |
| Bottom left | **Wedge**: a triangle of dots |
| Bottom right | **Orb**: a diamond of dots |

- The quarter of the shape in use is pressed in (lighter).
- Pointing at a quarter lights it and names its shape.
- A quarter whose shape can't be taken now is grey; pointing at it says why (a ghost army can't change its shape).
- Click a quarter to change the formation's shape; see [Formations](../../controls/formations.md#the-four-shapes).

The formation's orders (Disband, Charge and the others) stay on the [knuckles](#knuckle-buttons) round the rim.
