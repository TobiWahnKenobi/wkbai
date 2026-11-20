---
title: The Kingmaker campaign
ids: [mechanics/campaign, mechanic/campaign, mechanic/kingmaker, mechanic/provinces, mechanic/spoils-of-war]
order: 200
---

# The Kingmaker campaign

The campaign is a conquest of the land of Orbis, province by province. Each province is a battle on its own map against one or more AI Generals. What you win in one province you carry into the next. The screens are described in [the campaign screens](help:ui/frontend/campaign).

## Starting a campaign

- **New campaign:** give it a name. You may set its start: starting food, materials and gold, and up to 3 **Advances** each of Pagan, Imperial and Renaissance. A new campaign begins with the story and Angland chosen.
- **Load campaign:** carry on with a saved one. Each campaign keeps its conquered provinces, what it carries over and its own random seed.

## The map

- **Where you can attack:** at first only the starting province; after that, any province next to one you hold. Those carry the target marker.
- **Conquered** provinces are shown in their region's colours. You can play one again, but it gives no bonus a second time.
- **The panel** shows the province under the mouse (or the chosen one): its AI Generals and their tiers, what you win by conquering it, and its story.
- **The Generals** you meet are dealt from the campaign's seed: the story's own Generals in their provinces, the others by tier (Conscript, Veteran, Elite; [AI opponents](ai-opponents.md)).

## Playing a province

- You start at position 1 with what your campaign carries: its **food, materials and gold**, its **population limit** and its **Advances**.
- **Angland**, the first province, sets its own start the first time you play it: 600 food, 600 materials, 300 gold and a population limit of 100. Your Advances are kept.
- **Advances** decide which buildings you may build: a building needs enough Advances of its kind ([alignment](alignment.md#in-the-campaign)).
- **Each province has its own goals** and story scenes. Its briefing comes as a window at the start; the mission objectives (O, or the top bar's button) list what to do, and a gold ring on the minimap may show where.
- The fog of war is always on. wkbai's day and night, seasons and weather are off.
- **Losing** sends you back to the main menu; the province stays unconquered, and you can try again.

## Spoils of war

Winning a province conquers it and gives its bonus. The bonuses are mostly:

- a larger **population limit** (+20 or +40 for most provinces);
- an **Advance** (Pagan, Imperial or Renaissance);
- extra **food** or **materials** (+50 or +100) for later provinces.

Your stock at the end of a province isn't kept; what carries over is the campaign's own record, raised by each bonus.

![The campaign map with conquered provinces and the next targets](shot:mech-campaign-map)

> **wkbai.** Campaigns are kept as wkbai's own files; the original's `.usr` profiles can't be loaded. Every province's map is also in the skirmish list from the start.
