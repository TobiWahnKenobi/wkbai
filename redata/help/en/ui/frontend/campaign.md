---
title: Campaign
ids: [ui/frontend/campaign, ui/frontend/campaign/start-options#starting-resources, ui/frontend/campaign/list#your-campaigns, ui/frontend/campaign/name#enter-campaign-name, ui/frontend/campaign/new#new-campaign, ui/frontend/campaign/load#load-campaign, ui/frontend/campaign/delete#delete, ui/frontend/campaign/back#back, FRONT_END_CAMPAIGN_TITLE, FRONT_END_CAMPAIGN_NEW#new-campaign, FRONT_END_CAMPAIGN_LOAD#load-campaign, FRONT_END_CAMPAIGN_STARTING_RESOURCES#starting-resources]
order: 40
---

# Campaign

The Kingmaker campaign is a war of conquest over 22 provinces. Each campaign is kept under a name of its own (a profile): which provinces you have taken, and the resources, population and advances you carry from one province to the next. This screen starts a new campaign or carries on with one; the provinces themselves are on the [campaign map](campaign-map.md).

![The Campaign screen](shot:frontend-campaign)

## Starting resources

The panel on the left sets how a **new** campaign begins:

- **Starting Food, Starting Materials, Starting Gold:** 0 to 10,000 each (500, 500 and 200 at first), in steps of 100, 100 and 50.
- **Pagan Advance, Imperial Advance, Renaissance Advance:** 0 to 3 each: how far along each research path you start.

Hold the **-** or **+** button to step faster. These values are used only when you press New Campaign; a loaded campaign keeps its own.

## Your campaigns

The campaigns you have started. Click one to select it; a double click loads it.

## Enter Campaign Name

The name for a new campaign, up to 40 characters.

## New Campaign

Starts a campaign under the typed name with the starting resources on the left, and opens its map. Enter in the name field does the same. If a campaign of that name exists, you are asked whether to overwrite it.

## Load Campaign

Opens the selected campaign's map. A double click on it does the same.

## Delete

Deletes the selected campaign after asking. It can't be undone.

## Back

Returns to the [main menu](main.md).

## Tips

- The population limit of a new campaign is the one set in the skirmish lobby's [Settings tab](skirmish/settings.md#population-limit).
- Your campaigns are kept in your user folder (`%APPDATA%\wkbai\saved\Campaigns`).

> wkbai: campaigns are kept in wkbai's own files, not in the original's profile files, so the original's campaigns can't be carried over.
