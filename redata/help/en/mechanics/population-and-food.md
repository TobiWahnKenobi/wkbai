---
title: Food, population and upkeep
ids: [mechanics/population-and-food, mechanic/population, mechanic/population-limit, mechanic/food-support, mechanic/upkeep, mechanic/starvation]
order: 30
---

# Food, population and upkeep

Soldiers eat. Every military unit has a **food consumption** (most foot soldiers 1, cavalry and siege engines 2, priests and big creatures more; peasants and militia 0). Your farmers provide **food support**. While the support covers the consumption all is well; when it doesn't, your food store drains and, once it is empty, your soldiers go hungry.

## The food support

Support = **4 + the number of farmers at work × the support per farmer**.

| Research (at the [Guildhall](../buildings/guildhall-stone.md)) | Support per farmer |
|---|---|
| none | 4 |
| [Heavy Plough](../upgrades/heavy-plough.md) | 6 |
| [Horse Harness](../upgrades/horse-harness.md) | 10 |
| [Crop Rotation](../upgrades/crop-rotation.md) | 15 |

A farmer counts only while he is doing the Farm job (walking to deliver included). The top bar's food bar compares the consumption with the support.

## The upkeep penalty

Two things happen whenever your army eats more than a little:

1. **Food is taxed on delivery.** Each load of food a peasant delivers is cut by the share consumption ÷ support. With an army eating half your support, farmers bring in half their food; at or above the support, they bring in nothing. (When you are over the support, a "0" shows over peasants as they deliver.)
2. **The store drains.** Every 3.1 seconds, if the consumption is above the support, a tenth of the shortfall is taken from your food store.

So a small, well-fed army costs little; an army near your support eats most of your harvest.

## Starving

When the food store is empty and the consumption is still above the support:

- every 3.1 seconds soldiers that eat lose hit points: a tenth of the shortfall in all, spread over as many of them as the shortfall (an army 20 over its support hurts 20 soldiers by 0.1 each);
- only units above a tenth of their hit points suffer, and **starvation never kills**: a unit stops at 1 hit point;
- the cure is more farmers, the farming research, fewer soldiers, or food from tribute.

Training a unit that would take you over your support shows a warning on its button, but doesn't stop you.

## The population limit

- **What counts:** every character you own, the ones garrisoned in buildings and carried on ships included (ghost army members don't). Peasants count like soldiers.
- **The limit** is set in the lobby (default 200, up to 10,000 in wkbai). At the limit, training waits: a building's queue holds until a place is free.
- **AI Generals** of the Conscript tier get half the human's limit and Veterans three quarters; Elite generals get the full limit ([AI opponents](ai-opponents.md)).
- **In the campaign** the limit grows with the provinces you conquer ([campaign](campaign.md)).

![The top bar's food bar and population, with an army over its support](shot:mech-food-support)

> **wkbai.** In winter with wkbai's seasons on, fields lie fallow and the support per farmer is cut (a half in a gentle winter, a quarter in a normal one, nothing in a harsh one). A town then lives off the food it stored. See [seasons and weather](seasons-weather.md#fields-in-winter).
