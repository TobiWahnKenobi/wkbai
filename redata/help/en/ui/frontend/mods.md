---
title: Mods
ids: [ui/frontend/mods, ui/frontend/mods/list#the-list-of-mods, ui/frontend/mods/on-off#on-and-off, ui/frontend/mods/up#up-and-down, ui/frontend/mods/down#up-and-down, ui/frontend/mods/apply#apply, ui/frontend/mods/original-order#original-order, ui/frontend/mods/back#back]
order: 80
---

# Mods

The mod manager lists every mod wkbai found and lets you switch each one on or off and choose their order.

![The Mods screen with a few mods listed](shot:frontend-mods)

## Where mods come from

- **Archives:** `.bcp` files next to the game's `data.bcp` in the game folder.
- **Folders:** the game folder's `data\` folder, and each mod folder inside the `mods` folder (the setting `modsPath` in `wkconfig.json` can point to another folder, see [settings without a menu](../../options/config-file.md#modspath)).
- The game's own `data.bcp` is always loaded first, under every mod. The screen's second line says so.

## The list of mods

One row per mod, with its name and version, and under it its description (or what kind of mod it is). A mod that is on but has a problem shows the problem in red under its name instead. With more mods than fit, the mouse wheel scrolls the list.

## On and off

The first button of each row switches that mod **On** or **Off**. A mod that is off is greyed and not loaded.

## Up and down

**Up** and **Down** move a mod in the list. As soon as you move one, the list becomes your own load order: a mod further down overrides the ones above it wherever they change the same file. The line at the top of the screen tells you which order is in use.

## Apply

Loads the mods as the list now shows them. It is lit once you have changed something. Changes are not used until you press it.

## Original order

Goes back to the original game's rule: each file comes from whichever archive or folder has the newest copy of it, whatever the list's order. It is lit only while your own order is in use; press Apply afterwards.

## Back

Returns to the [main menu](main.md) without applying changes you haven't applied. Esc does the same.

## Tips

- If two mods fight over the same file, put the one you want to win further down.
- In multiplayer, a mod that changes the game's rules must match the host's; mods that only change looks or sounds are free. The host chooses how strict that is, see [the host's settings](multiplayer/host.md#mods).

> wkbai: the original game has no mod manager; it simply reads every archive and the `data` folder, the newest file winning. wkbai keeps that as the Original order.
