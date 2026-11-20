---
title: About wkbai
ids: [help/about, help/licence#the-licence, help/license#the-licence, help/source-code#the-source-code, help/third-party#other-peoples-work, help/not-affiliated#not-affiliated]
order: 950
---

# About wkbai

wkbai is a new engine for the real-time strategy game *Warrior Kings: Battles* (2003). It is an independent fan project, made with AI assistance and by studying how the original game works.

## Not affiliated

wkbai is in no way affiliated with, endorsed by or connected to Black Cactus Games, the makers, publishers or owners of the rights to *Warrior Kings* and *Warrior Kings: Battles*. Nor is it connected to wkbre2 or its author AdrienTD. *Warrior Kings* is a trademark of its owners.

wkbai contains no part of the game: no program code, maps, models, textures, sounds or texts. You need your own copy of *Warrior Kings: Battles*, which wkbai reads ([installing wkbai](install.md)). The help's screenshots are taken on your own computer from your copy, and never shipped.

## The licence

wkbai is free software under the **GNU General Public License, version 3** (GPL-3). The full text is the file `LICENSE` beside `wkbai.exe`. In short:

- You may use wkbai for any purpose, and give copies to anyone.
- You may change it. If you pass on a changed version, it must be under the same licence, with its source code.
- wkbai comes with no warranty.

## The source code

The file `SOURCE.txt` beside `wkbai.exe` names the exact version of the source code your copy was built from, and where to get it at no charge.

wkbai is a modified version of **wkbre2**, AdrienTD's open-source engine for *Warrior Kings* and *Warrior Kings: Battles*, also under the GPL-3. All credit for wkbre2 goes to AdrienTD and its contributors.

## Other people's work

wkbai is built with other free software, each under its own licence, all of them compatible with the GPL-3. `THIRD-PARTY-NOTICES.md` lists them with their versions, and the `licenses` folder holds their full licence texts. Among them:

| Component | What it does in wkbai | Licence |
|---|---|---|
| Diligent Engine | draws the game with Direct3D 12 and Vulkan | Apache-2.0 |
| SDL | the window, keyboard and mouse | Zlib |
| OpenAL Soft | 3D sound | LGPL-2.0 or later |
| mpg123 | plays the music | LGPL-2.1 |
| Lua | the menus' and interface's scripts, Lua AIs | MIT |
| Dear ImGui | draws the menus' and windows' shapes and text, and the developer tools | MIT |
| AMD FidelityFX | FSR upscaling and anti-aliasing | MIT |
| ENet | network play | MIT |
| MD4C, lunasvg | this help's pages and diagrams | MIT |

The two LGPL libraries are separate DLL files, so they can be replaced by other builds of the same version.

**Not part of wkbai:** the Nakama game server, which you can run for online games, is a separate program by Heroic Labs under the Apache-2.0 licence ([running a Nakama server](multiplayer/nakama-server.md)).

## Credits

The [Credits](ui/frontend/credits.md) screen in the main menu shows the people who made *Warrior Kings: Battles*, then wkbai's.
