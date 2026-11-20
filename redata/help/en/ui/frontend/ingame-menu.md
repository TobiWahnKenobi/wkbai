---
title: In-game menu
ids: [ui/frontend/ingame, ui/frontend/ingame/return#return-to-game, ui/frontend/ingame/save#save-game, ui/frontend/ingame/save-as#save-game-as, ui/frontend/ingame/load#load-game, ui/frontend/ingame/restart#restart-level, ui/frontend/ingame/options#options, ui/frontend/ingame/exit-to-menu#exit-to-main-menu, ui/frontend/ingame/exit#exit-to-windows, IN_GAME_Return_To_Game#return-to-game, IN_GAME_Save#save-game, IN_GAME_Save_As#save-game-as, IN_GAME_Load#load-game, IN_GAME_Restart_Level#restart-level, IN_GAME_Options#options, IN_GAME_Exit_To_Main_Menu#exit-to-main-menu, IN_GAME_Exit_Application#exit-to-windows, INGAME_GAME_PAUSED#game-paused]
order: 120
---

# In-game menu

Press **Esc** during a game (the key is [`openMenu`](help:key/openMenu), and can be changed). A gold-framed column of buttons opens over the darkened battlefield. Press Esc again, or Return To Game, to close it.

![The in-game menu over a running game](shot:frontend-ingame)

## Game paused

In single player the game stops while the menu is open, and the menu says so at the top. In multiplayer the game goes on for everyone.

## Return To Game

Closes the menu and goes back to the game.

## Save Game

Saves the game. The first time it asks for a name ([Save Game As](save.md)); after that it saves over the same name at once. Grey when you can't save: as a guest in another player's game.

## Save Game As

Saves under a new name, see [Save Game As](save.md).

## Load Game

Opens [Load Game](load.md) to load a saved game or a replay instead.

## Restart Level

Asks first, then starts the same map again from its beginning, with the same setup.

## Options

Opens the [Options](../../options/index.md). Most settings take effect as soon as you press OK, while the game stays open behind.

## Exit To Main Menu

Asks first, then ends the game and returns to the [main menu](main.md). In a campaign province, leaving like this doesn't conquer it.

## Exit To Windows

Asks first, then closes wkbai.

## Tips

- Esc closes whatever is open first: a question, then a screen opened from this menu, then the menu itself.
- The briefing and objective windows of a level stay under the menu while it is open and can't be clicked until it closes; see [the in-game screen](help:ui/hud).

> wkbai: the menu has the original's entries and look. Restart Level, Exit To Main Menu and Exit To Windows ask before acting, as the original does.
