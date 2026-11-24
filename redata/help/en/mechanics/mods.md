---
title: Mods
ids: [mechanics/mods, mechanic/mods, mechanic/modding, mechanic/game-set, mechanic/unknown-orders#levels-that-name-what-the-game-set-lacks]
order: 230
---

# Mods

A mod adds to or replaces the game's files. wkbai reads the rules from the game set, so a mod can change nearly every rule on these pages. How to switch mods on and off is in [the Mods screen](help:ui/frontend/mods).

## What a mod can change

| Kind | What it changes |
|---|---|
| **The game set** (`Warrior Kings Game Set\*.cpp`, `.gsf`) | the rules: units, buildings, costs, hit points, damage, research, upkeep, alignment, victory, the AI's scripts |
| **Maps and levels** | new maps, campaign levels, scenarios; also wkbai's own [map folders](../ui/editor/map-format.md) in the mod's `Maps` folder |
| **Models, textures, sounds, music** | how things look and sound (wkbai also takes `.png`, `.dds` and `.ktx2` textures of any size in place of the originals) |
| **Lua scripts** | the menus and the in-game interface; new menu entries and screens |
| **Lua AIs** (`LuaAI\<name>.lua`) | new computer players, chosen per position in the lobby ([AI opponents](ai-opponents.md)) |

- A mod ships only its own files. A file with the same path as a game file replaces it.
- The help's tables of costs and hit points read the loaded game set, so they show a mod's own numbers.
- **wkbai's own fixes to the game set** are kept in a folder of patches (`Warrior Kings Game Set\wkbai patches`) that loads after the game set. A mod can replace a patch with its own copy, or add patches of its own. The current patches fix the King of the Hill ending, make lightning only scratch buildings, add the winter crafts bonus, and let snow on a roof protect a building from fire (`Snow and Fire.cpp`, [rain and snow on burning buildings](seasons-weather.md#rain-and-snow-on-burning-buildings)).
- WKB.exe knows a few dozen script keywords that no shipped script uses. wkbai supports all of them, so mods written for the original that use them work.

## Replacing models and textures

wkbai reads more kinds of model and texture than the original, so a mod can bring new art without the game's old tools.

- **glTF models:** a file `X.glb` or `X.gltf` beside a game model `X.mesh3`, at the same path, is drawn in its place. It may be in a mod, or in the `saved` folder of [your own folder](../files.md#your-own-folder). A glTF model has at most 65,535 vertices and no animation of its own, and keeps the original model's attachment points (flags, smoke, the place where new units appear).
- **Exporting the game's models:** `"exportGltf": ["Warrior Kings Game Set\\Buildings\\Manor"]` in `wkconfig.json` writes each model of that folder and its subfolders (or a single `.mesh3` named there) as a `.glb` file into the `exported` folder of your own folder, at its own path. wkbai then closes. Take the setting out again afterwards. Edit an exported model and put it into `saved` at the same path to try it, for example `saved\Warrior Kings Game Set\Buildings\Manor\Fully Built\DEFAULT.glb`.
- **Material maps:** beside a texture `T.tga`, the files `T_albedo`, `T_normal`, `T_orm` (ambient occlusion, roughness and metalness) and `T_emissive` (light of its own, such as lit windows), each `.png`, `.tga`, `.dds` or `.ktx2`, give the enhanced renderer more detail for every model that uses that texture. They show best with [Model highlights](../options/graphics/models.md#model-highlights) on. A `T_height` map adds relief ([parallax occlusion mapping](../options/graphics/models.md#parallax-occlusion-mapping-models)).

## Material maps for the ground

The ground's pictures (`Maps\Map_Textures\<name>`, each an atlas of 4 x 4 cells) can have material maps too, beside a mod's replacement picture in the same folder; any of them may be missing:

- **`<name>_normal`:** the surface's bumps as a normal map (+z out, the DirectX layout). Without it, the game's own bump map is used as before.
- **`<name>_orm`:** occlusion, roughness and metalness, so the ground catches the sun and the sky as the models do. Without it, the ground is lit as before.
- **`<name>_height`:** the relief for [parallax occlusion mapping](../options/graphics/terrain-grass.md#parallax-occlusion-mapping-terrain): white is the surface, black 12 cm below it.

The maps cover the whole atlas in the picture's own layout, cell for cell. Blur and make mipmaps within each cell, never across a cell's edge, since the cells are unrelated pictures. They are read as linear data, not sRGB, and the normal map should match the height map's slopes.

The ground's [generated transitions](../ui/editor/map-format.md#generated-transitions) are blended from the pictures as loaded, a mod's included, and made again on the next load when a mod changes one of them. To make them all anew, delete `saved\Maps\Map_Textures\Generated` in your user folder.

## Levels that name what the game set lacks

A level made for another game set, or for a mod that isn't on, can give its objects orders or tasks that the game set you play with doesn't have. wkbai still loads it:

- **An order the game set doesn't have** is left out.
- **A task it doesn't have** is replaced by the order's own task where there is one, else left out.

Each one writes a line starting with `WARNING:` to the game's log (the console window, see [the log](../troubleshooting.md#the-log)), with the object and the name. If a map behaves oddly, look there first: it often means a mod the map needs is off ([the Mods screen](../ui/frontend/mods.md)).

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
