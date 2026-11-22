---
title: Installing and starting wkbai
ids: [help/install, help/system-requirements#what-you-need, help/first-start#the-first-start, help/game-path#when-wkbai-cant-find-the-game, help/package#whats-in-the-package, help/update#updating-wkbai]
order: 2
---

# Installing and starting wkbai

wkbai is a new engine for *Warrior Kings: Battles*. It brings no game of its own: it reads the maps, models, sounds and rules from your copy of the game.

## What you need

- **Your own copy of *Warrior Kings: Battles*.** wkbai needs its file `data.bcp`. wkbai never changes the game's folder; it only reads from it.
- **64-bit Windows.** wkbai runs only on Windows for now.
- **A graphics card and driver for Direct3D 12 or Vulkan.** wkbai draws with one of them (see [Renderer](options/system.md#renderer)).
- **For ray tracing (optional):** a graphics card that can trace rays from its shaders. Without one, the ray-traced settings fall back to the ordinary way ([ray tracing](options/graphics/index.md#ray-tracing)).

## What's in the package

wkbai comes as a zip file. Unpack it into a folder of its own, for example on your desktop or in your documents. It holds:

- `wkbai.exe`, the program, and the DLL files it needs;
- the `redata` folder: the menus, the interface scripts, the Lua AIs and this help;
- `LICENSE`, `README.md`, `THIRD-PARTY-NOTICES.md`, the `licenses` folder and `SOURCE.txt`: the licences and where the source code is ([about wkbai](about.md)).

There is no installer: wkbai runs from the folder you unpacked it to. Keep all these files together.

## The first start

Start `wkbai.exe`. On the first start wkbai:

1. **Looks for the game.** If the folder named in its settings has no `data.bcp`, it looks for the Steam installation of *Warrior Kings: Battles*, in Steam's own folder and in every Steam library folder. When it finds it, it remembers that folder.
2. **Finds out which game it is.** A folder with the *Battles* program or its `WKB_Plus.bcp` counts as *Warrior Kings: Battles*.
3. **Writes its settings file** `wkconfig.json` in the folder it was started from ([settings without a menu](options/config-file.md)).
4. **Makes your own folder,** `%APPDATA%\wkbai`, for saves, replays, campaigns and crash reports ([files and folders](files.md)).
5. **Plays the game's intro video**, if it is in the game folder, then shows the [main menu](ui/frontend/main.md). Esc, Space, Enter or a left click skips the video.

The first start, and the first time you load a map, take longer: wkbai prepares its shaders for your graphics card. It keeps them, so later starts are much quicker. After a driver update it prepares them once more.

Along with the game window, a text window opens. It shows what wkbai is doing (its log). Leave it open; closing it closes wkbai.

Then set up the game the way you like it in the [Options](options/index.md), or go straight to [your first skirmish](getting-started.md).

## When wkbai can't find the game

If the game isn't in a Steam folder, wkbai says that it can't find `data.bcp`, and closes. Then tell it where the game is:

1. Close wkbai.
2. Open `wkconfig.json` (in wkbai's folder) with a text editor, such as Notepad.
3. Set `gamePath` to the game's folder, the one that holds `data.bcp`. Write every backslash twice: `"gamePath": "D:\\Games\\Warrior Kings Battles",`
4. Save the file and start wkbai again.

You can also copy `data.bcp` (with the rest of the game's folder) into wkbai's own folder: the default `gamePath` is wkbai's folder itself.

## Updating wkbai

Unpack the new version into its folder, over the old one or into a new folder. Your saves, replays, campaigns and armies stay in your own folder and are found by every version. Copy `wkconfig.json` across if you unpack into a new folder and want to keep your settings.

> wkbai: the original game comes with a launcher, "Game Starter.exe". wkbai doesn't need it: its main menu has all the options and the [mod manager](ui/frontend/mods.md).
