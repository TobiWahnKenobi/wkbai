---
title: AI General Editor
ids: [ui/frontend/generaleditor, ui/frontend/generaleditor/portrait#portrait, ui/frontend/generaleditor/name#name, ui/frontend/generaleditor/category#category, ui/frontend/generaleditor/alignment#alignment, ui/frontend/generaleditor/random#randomise-all, ui/frontend/generaleditor/military#military-behaviour, ui/frontend/generaleditor/diplomatic#diplomatic-behaviour, ui/frontend/generaleditor/generals#your-generals, ui/frontend/generaleditor/delete#delete, ui/frontend/generaleditor/new#new-ai-general, ui/frontend/generaleditor/background#background, ui/frontend/generaleditor/save#save-ai-general, ui/frontend/generaleditor/back#back, FRONT_END_GENERAL_CREATION_TITLE, FRONT_END_GENERAL_CREATION_RANDOM#randomise-all, FRONT_END_GENERAL_CREATION_SAVE_GENERAL#save-ai-general, FRONT_END_GENERAL_CREATION_NEW_GENERAL#new-ai-general]
order: 100
---

# AI General Editor

Make AI Generals of your own. A general you save here joins the game's own generals in the skirmish lobby's list (under its tier) and can be dealt to you as an opponent in the campaign. The Kingmaker AI reads its fourteen personality values to decide how it builds, fights and deals with the other players.

![The AI General Editor](shot:frontend-generaleditor)

## Portrait

The picture shown for the general in the lobby and in its messages during a game. **<** and **>** step through the portraits; the counter between them shows which one of how many.

## Name

The general's name, up to 32 characters. It is also the file name, so the characters a file name can't hold are ignored. Save AI General needs a name.

## Category

The general's tier: **Conscript**, **Veteran** or **Elite**. A click takes the next. The skirmish lobby lists the general under this tier, and the campaign deals generals by tier.

## Alignment

The general's side of the game's three paths: **Pagan**, **Renaissance** or **Imperial**. A click takes the next.

## Randomise All

Gives every slider a random value and picks a random portrait. The name, category, alignment and background stay.

## Military Behaviour

Seven sliders from 0 to 100, each showing its value. A two-ended slider leans towards one end or the other; 50 is the middle.

- **Economic - Military:** building up the town, or building up the army.
- **Raiding - Siege:** small, quick raids, or full attacks on towns.
- **Pagan - Renaissance - Imperial:** which of the three research paths it favours.
- **Infantry - Cavalry:** soldiers on foot or on horseback.
- **Heavy - Light:** heavily armoured troops, or light and fast ones.
- **Leadership:** how well it leads its armies.
- **Fervour:** how eagerly it throws itself into the fight.

## Diplomatic Behaviour

Seven more sliders, for how the general treats the other players when diplomacy is allowed.

- **Toady - Neutral - Arrogant:** humble and accommodating, even-handed, or haughty.
- **Diplomatic Frequency:** how often it makes offers and changes its relations.
- **Greediness:** how much it wants in return, for instance in tribute.
- **Treachery:** how readily it breaks an alliance.
- **Pride:** how much a slight counts with it.
- **Friendliness:** how warmly it is inclined towards others.
- **Chattiness:** how often it sends messages.

## Your generals

The button above the background shows the general being edited: **New AI General** for a new one, or the name of one you made before. A click steps through the generals you made, loading each one's values to change them. It is grey until you have saved one.

## Delete

Deletes the general being edited, after asking. Only your own generals can be deleted.

## New AI General

Starts a new general: every slider back to 50, the first portrait, Veteran, Renaissance, and no name or background.

## Background

The general's story, up to 200 characters. The skirmish lobby shows it beside the portrait when the general is chosen or under the mouse.

## Save AI General

Saves the general (a message confirms it). Saving under the name of a general you made before replaces it. The file goes into your user folder (`%APPDATA%\wkbai\saved\Warrior Kings Game Set\AI Generals`), never into the game folder.

## Back

Returns to the [main menu](main.md). Changes you haven't saved are lost.

## Tips

- In multiplayer, the host's own generals are sent to every guest, so everyone plays with the same list.
- To try a general, start a skirmish, select an AI position on the [Players tab](skirmish/players.md) and pick it from its tier.

> wkbai: the original's dialog also has a "random" box per slider and a personality list for the chat messages; wkbai has neither yet.
