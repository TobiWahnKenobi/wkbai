---
title: Diplomacy windows
ids: [ui/hud/diplomacy, ui/hud/diplomacy/window#diplomacy, ui/hud/diplomacy/status-buttons#the-status-buttons, ui/hud/diplomacy/report#global-diplomacy, ui/hud/diplomacy/tributes#tributes]
order: 80
---

# Diplomacy windows

The top bar's **Global Diplomacy**, **Diplomacy** and **Tributes** buttons open three windows in the middle of the screen. Each player is listed with its flag and its name in its colour. Click the same button again, or the window's Cancel or OK, to close it. See [Diplomacy](help:mechanic/diplomacy) for the rules.

## Diplomacy

![The Diplomacy window with an alliance offered](shot:hud-diplomacy)

**Opens:** the top bar's Diplomacy button, lit when the lobby's **Allow Diplomacy** was on.

One row per other player, with a button for each status: **Allied**, **Neutral** and **Enemy**, best first.

### The status buttons

What a button does depends on where you stand with that player:

| The button | Meaning | Click |
|---|---|---|
| Your present status | Shown pressed in | – |
| A worse status | Change it: declare war, or end an alliance | Set at once when you press Send |
| A better status | Offer it | The other player must accept |
| The status you offered | Blinks "Cancel offer" | Withdraw your offer |
| The status they offered you | Blinks "Accept offer" | Accept it |

The tooltip of each button says what it will do ("Declare war", "Offer alliance", ...).

At the bottom:

- **Send** (it reads **OK** while nothing is changed) sends your changes and closes the window;
- **Cancel** closes it and forgets them;
- **Reset changes** forgets them and keeps it open.

On a level that doesn't allow diplomacy the buttons are greyed.

## Global Diplomacy

![The Global Diplomacy report for four players](shot:hud-global-diplomacy)

**Opens:** the top bar's Global Diplomacy button, lit once you own a **Tavern**.

A table of every pair of players, each pair once, as its status's picture. Point at a picture to read its name. **OK** closes it. With a Tavern you also get a notice whenever two other players change how they stand.

## Tributes

![The Tributes window with gold set aside for an ally](shot:hud-tributes)

**Opens:** the top bar's Tributes button, lit when you own a **Trading Post** and diplomacy is allowed or the game has teams.

A row per other player, a column for each resource (food, wood, gold). Under each resource's icon, what you have left to give.

- **Left-click** a cell: 10 more for that player (with **Shift**, 50 more). You can't give more than you have.
- **Right-click** a cell: 10 less.
- **Send** gives it all at once and closes the window.
- **Clear tributes** sets every cell back to 0; **Cancel** closes without giving.

The other player gets a notice and the resources at once.

> wkbai: the original's three windows, drawn in the game windows' gold frame. Their exact sizes and positions weren't measured from the original. The original has no surrender, and neither has wkbai.
