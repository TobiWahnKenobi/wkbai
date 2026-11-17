---
title: Mods
ids: [mechanics/mods, mechanic/mods, mechanic/modding, mechanic/game-set]
order: 230
---

# Mods

A mod adds to or replaces the game's files. wkbai reads the rules from the game set, so a mod can change nearly every rule on these pages. How to switch mods on and off is in [the Mods screen](help:ui/frontend/mods).

## What a mod can change

| Kind | What it changes |
|---|---|
| **The game set** (`Warrior Kings Game Set\*.cpp`, `.gsf`) | the rules: units, buildings, costs, hit points, damage, research, upkeep, alignment, victory, the AI's scripts |
| **Maps and levels** | new maps, campaign levels, scenarios |
| **Models, textures, sounds, music** | how things look and sound (wkbai also takes `.png`, `.dds` and `.ktx2` textures of any size in place of the originals) |
| **Lua scripts** | the menus and the in-game interface; new menu entries and screens |
| **Lua AIs** (`LuaAI\<name>.lua`) | new computer players, chosen per position in the lobby ([AI opponents](ai-opponents.md)) |

- A mod ships only its own files. A file with the same path as a game file replaces it.
- The help's tables of costs and hit points read the loaded game set, so they show a mod's own numbers.
- **wkbai's own fixes to the game set** are kept in a folder of patches (`Warrior Kings Game Set\wkbai patches`) that loads after the game set. A mod can replace a patch with its own copy, or add patches of its own. The current patches fix the King of the Hill ending, make lightning only scratch buildings, and add the winter crafts bonus.
- WKB.exe knows a few dozen script keywords that no shipped script uses. wkbai supports all of them, so mods written for the original that use them work.

## Load order

- **Original order** (the default): as in the original game, the newest copy of each file wins, whichever mod or archive holds it. Mods like WKB Plus rely on this.
- **Your own order:** move a mod up or down on the Mods screen; then mods lower in the list override those above them.
- A mod's `mod.json` can name the mods it needs. A missing one is shown in red; the mod still loads.

## Mods in multiplayer

The game runs on the host, but every computer loads the game set and the map itself, so the rules must match:

- **Logic mods** (game set scripts, maps, levels and saves) must be the same as the host's.
- **Looks-only mods** (models, textures, sounds, interface scripts) are free: each player may use their own.
- **The host's choice** (lobby settings): the same logic as the host (default), the host sends its logic mods to guests for this game, no mods at all, or only the host's mods. A guest who doesn't fit is told why.

![The Mods screen with two mods enabled](shot:mech-mods-screen)
