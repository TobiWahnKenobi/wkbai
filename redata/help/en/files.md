---
title: Files and folders
ids: [help/files, help/user-folder#your-own-folder, help/folders, option/userPath#your-own-folder, help/wkbai-folder#wkbais-folder, help/backup#backing-up]
order: 3
---

# Files and folders

wkbai uses three folders. It reads the game's folder, keeps its settings beside itself, and writes everything else into a folder of your own.

## The game's folder

The folder of your *Warrior Kings: Battles*, with its `data.bcp`. wkbai only reads from it and never writes there. Its setting is `gamePath` ([installing wkbai](install.md#when-wkbai-cant-find-the-game)).

Archives (`.bcp`) beside `data.bcp` and the game's `data` folder are read as mods, as the original game does ([the Mods screen](ui/frontend/mods.md)). Saved games in the game's own `saved` folder can be loaded too.

## wkbai's folder

The folder you unpacked wkbai into (strictly, the folder wkbai is started from):

- **`wkconfig.json`:** all your settings, the keys too ([settings without a menu](options/config-file.md)).
- **`mods`:** your mod folders, each with its `mod.json`. The setting `modsPath` can point to another folder.
- **`redata`:** wkbai's own menus, interface scripts, Lua AIs and this help. Leave it as it is: an update replaces it.

## Your own folder

`%APPDATA%\wkbai`: type that into the address bar of a Windows Explorer window to open it. Everything wkbai writes goes here:

| Folder or file | What it holds |
|---|---|
| `saved\Save_Games` | your saved games ([Save Game As](ui/frontend/save.md)) |
| `saved\Recordings` | recorded games, one folder each ([saves and replays](mechanics/saves-and-replays.md)) |
| `saved\Campaigns` | your Kingmaker campaigns ([Campaign](ui/frontend/campaign.md)) |
| `saved\Armies` | the Valhalla armies you saved ([Army Enlistment](ui/frontend/skirmish/army.md)) |
| `saved\Warrior Kings Game Set\AI Generals` | the AI Generals you made ([AI General Editor](ui/frontend/general-editor.md)) |
| `saved\Unlocked Levels.txt` | the levels a campaign has unlocked |
| `saved`, other folders | maps you made in the Map Editor, and files that replace game files (see below) |
| `crashes` | crash reports ([troubleshooting](troubleshooting.md#when-wkbai-crashes)) |
| `shadercache` | the shaders prepared for your graphics card; safe to delete, wkbai makes them again |
| `farlandcache` | the land beyond the map's edge, kept on disk ([Extend map](options/graphics/terrain-grass.md#keep-the-far-land-on-disk)); safe to delete |
| `help\shots` | the screenshots this help shows, once they are taken |
| `HostMods` | mods a multiplayer host sent you, for that game only; wkbai removes them after the game |
| `chronicles` | timelines you exported as Markdown or web pages ([Export](ui/hud/chronicle.md#export)) |
| `exported` | models exported for modding ([mods](mechanics/mods.md#replacing-models-and-textures)) |
| `plugins` | matchmaking plugins, if you have any |
| `agent` | pictures taken by a program playing through [agent control](options/agent-control.md) |
| `matchmaking.json` | this computer's id for online matchmaking |

**A file in `saved` replaces the game's file of the same path,** when neither the game nor a mod has it. Mods use this too.

**Another place:** the setting `userPath` in `wkconfig.json` moves your own folder somewhere else, for example `"userPath": "D:\\wkbai-files",`. wkbai then uses that folder and no longer looks in `%APPDATA%\wkbai`.

**From wkbre2:** wkbai used to be called wkbre2. On its first start, wkbai copies an old `%APPDATA%\wkbre2` folder to `%APPDATA%\wkbai`. The old folder stays as it was.

## Backing up

To keep your games safe, copy the `saved` folder of your own folder, and `wkconfig.json` from wkbai's folder. Nothing else needs a backup: the other folders are made again by themselves.
