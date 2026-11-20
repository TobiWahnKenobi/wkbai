---
title: Army Enlistment tab
ids: [ui/frontend/skirmish/army, ui/frontend/mphost/army, ui/frontend/mpguest/army, ui/frontend/skirmish/army/alignment#alignment, ui/frontend/skirmish/army/platform#weapon-platforms, ui/frontend/skirmish/army/points#resource-points-left, ui/frontend/skirmish/army/companies#companies, ui/frontend/skirmish/army/chosen#chosen-units, ui/frontend/skirmish/army/enlist#enlist, ui/frontend/skirmish/army/disband#disband, ui/frontend/skirmish/army/details#the-companys-details, ui/frontend/skirmish/army/name#saving-and-loading-armies, ui/frontend/skirmish/army/save#saving-and-loading-armies, ui/frontend/skirmish/army/saved#saving-and-loading-armies, ui/frontend/skirmish/army/load#saving-and-loading-armies, FRONT_END_LOBBY_ARMIES, FRONT_END_ARMIES_COMPANIES#companies, FRONT_END_ARMIES_CHOSEN_UNITS#chosen-units, FRONT_END_ARMIES_ENLIST#enlist, FRONT_END_ARMIES_DISBAND#disband, FRONT_END_ARMIES_RESOURCE_POINTS#resource-points-left, FRONT_END_ARMIES_SAVE_ARMY#saving-and-loading-armies, FRONT_END_ARMIES_LOAD_ARMY#saving-and-loading-armies, FRONT_END_ARMIES_ALIGNMENT_RANDOM#alignment]
order: 40
---

# Army Enlistment tab

In [Valhalla mode](settings.md#standard-mode-and-valhalla-mode) you don't build an army, you enlist one before the game. This tab appears when Valhalla mode is chosen. Spend your Resource Points on companies; at the start they muster around your Manor. Start stays grey until every human player has enlisted an army.

![The Army Enlistment tab with a few companies enlisted](shot:frontend-skirmish-army)

## Alignment

The six buttons at the top left: **Pagan**, **Pagan Renaissance**, **Renaissance**, **Imperial Renaissance**, **Imperial** and **Random Alignment**. Each alignment has its own companies. Changing it removes the companies the new alignment can't have from your army.

**Random Alignment** picks one of the five at the start of the game, and companies of it at random while the points last. You then don't enlist anything yourself.

## Weapon platforms

The buttons below the alignments filter the list of companies: **All**, **HI** (heavy infantry), **LI** (light infantry), **HC** (heavy cavalry), **LC** (light cavalry) and **Misc** (the companies that are none of these).

## Resource points left

The points you have left of the Resource Points set on the [Settings tab](settings.md#resource-points). It turns red if the army costs more than you have.

## Companies

The companies of your alignment and the chosen platform, each with its cost in points. One you can't afford any more is grey. Click one to see its details; a double click enlists it.

## Chosen units

The companies in your army so far, with their costs. A double click on one disbands it.

## Enlist

Adds the selected company to your army. A company can be enlisted more than once. Grey when you can't afford it.

## Disband

Removes the selected company from your army and gives its points back.

## The company's details

The panel on the right shows the selected company's picture, name, unit type, cost and description.

## Saving and loading armies

The bottom panel keeps armies for another game:

- **Filename:** the name to save the army under (up to 40 characters).
- **Save Army:** saves your alignment and companies under that name.
- **The list of saved armies:** click one to select it; a double click loads it.
- **Load Army:** replaces your army with the selected one. Companies that no longer exist are left out.

Saved armies are kept in your user folder (`%APPDATA%\wkbai\saved\Armies`).

## In multiplayer

A guest opens the same panel with the **Army Enlistment** button in the [guest's lobby](../multiplayer/guest.md#army-enlistment); the host sees each guest's points in the positions list. The AI Generals choose their own armies.

> wkbai: the tab follows the original's Army Enlistment, with its alignments, filters and buttons.
