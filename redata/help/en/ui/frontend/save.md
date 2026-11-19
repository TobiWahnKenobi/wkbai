---
title: Save Game As
ids: [ui/frontend/save, ui/frontend/save/list#the-list-of-saves, ui/frontend/save/filename#filename, ui/frontend/save/save#save, ui/frontend/save/back#back, FRONT_END_MAIN_MENU_SAVE_GAME_AS, FRONT_END_SAVE_GAME_FILENAME#filename]
order: 35
---

# Save Game As

Saves the game you are playing under a name. It opens from the [in-game menu](ingame-menu.md) (Esc) with **Save Game As**, and with **Save Game** the first time you save a game.

![The Save Game As window](shot:frontend-save)

## The list of saves

Your saved games so far. Clicking one fills in its name below, so that you can save over it. A double click saves over it straight away (after the question).

## Filename

The name to save under. It starts with the name this game was last saved as. Characters a file name can't hold (`\ / : * ? " < > |`) are ignored, and a name of dots only isn't accepted.

## Save

Saves the game. If a save of that name exists, you are asked whether to overwrite it. Enter in the name field does the same. When the save is written, "Game saved successfully" appears and you are back in the in-game menu; if it can't be written, a message says so.

## Back

Returns to the in-game menu without saving.

## Tips

- **Save Game** in the in-game menu saves straight over the last name used, without this window.
- Saving works in single player and for the host of a multiplayer game. A guest in someone else's game can't save; the buttons are grey.

> wkbai: saves are written in the original game's own format, so they look like the original's saves. A few things the original keeps are not saved yet: the AI's progress through its plan starts again after loading.
