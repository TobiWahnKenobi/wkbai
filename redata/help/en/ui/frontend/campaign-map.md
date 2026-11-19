---
title: Campaign map
ids: [ui/frontend/campaignmap, ui/frontend/campaignmap/map#the-map, ui/frontend/campaignmap/panel#the-panel, ui/frontend/campaignmap/start#start, ui/frontend/campaignmap/back#back]
order: 45
---

# Campaign map

The map of the Kingmaker campaign's 22 provinces, with a panel beside it. Choose a province you can attack and press Start to fight for it. Win, and it is yours: its bonus is added to what you carry on, and its neighbours can be attacked next.

![The campaign map with a province chosen](shot:frontend-campaignmap)

## The map

- **Conquered provinces** are drawn in full colour, with a small marker.
- **Provinces you can attack** carry a pulsing target marker: at the start the first province only, after that every neighbour of a province you hold.
- **Under the mouse** a province lights up faintly; a click chooses it, and it stays lit more strongly.

## The panel

- **The title:** the campaign's name.
- **Progress:** how many of the 22 provinces you hold, and the food, materials and gold you carry into the next province.
- **The province** under the mouse, or the chosen one: its name, whether you have conquered it, how many AI Generals defend it and of which tiers, what you receive if you conquer it, and its story.

## Start

Plays the chosen province. You start at the first position, against its AI Generals: the campaign picks them at random from the tiers the province names. Start is grey until you choose a province you can attack. Provinces you hold can be played again, but give no bonus a second time.

## Back

Returns to the [Campaign](campaign.md) screen.

## Tips

- Winning a province brings you back to this map with the province conquered. Losing, or leaving through Exit To Main Menu, returns to the main menu and the province stays unconquered.
- A province's bonus carries on into every later province, so the order you take them in matters. The panel names each province's bonus before you attack it.

> wkbai: the layout is wkbai's own (the whole map beside a fixed panel); the markers, the neighbours and the rules for which province can be attacked are the original's. The original's short scenes between provinces are not shown.
