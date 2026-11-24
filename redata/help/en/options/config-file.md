---
title: Settings without a menu
ids: [options/config-file, option/gamePath#gamepath, option/gameVersion#gameversion, help/config-file, option/enhancedGraphics#enhancedgraphics, option/adapterIndex#adapterindex, option/windowWidth#windowwidth-and-windowheight, option/windowHeight#windowwidth-and-windowheight, option/ssaoRadius#ssaoradius-and-ssaostrength, option/ssaoStrength#ssaoradius-and-ssaostrength, option/bloomThreshold#bloomthreshold, option/bloomIntensity#bloomintensity, option/lightShaftStrength#lightshaftstrength, option/fogOfWarBrightness#fogofwarbrightness, option/stainedGlass#stainedglass, option/skyLight#skylight, option/cloudReflections#cloudreflections, option/modelSpecular#modelspecular, option/terrainOriginalLight#terrainoriginallight, option/chimneySmoke#chimneysmoke, option/drips#drips, option/introVideo#introvideo, option/playerName#playername, option/joinAddress#joinaddress, option/hostPort#hostport, option/hostBind#hostbind, option/modsPath#modspath, option/modAllowList#modallowlist, option/gfxValidation#gfxvalidation, option/madeSwimAnimations#madeswimanimations, option/editorSaveFormat#editorsaveformat, option/forest#forest]
order: 90
---

# Settings without a menu

All settings are kept in one file, `wkconfig.json` in wkbai's folder ([files and folders](../files.md#wkbais-folder)), which wkbai reads at start and writes when you press OK in the Options. A few settings have no place in the menus: they are for unusual setups, or are kept by the menus themselves. You can change them in the file with a text editor while wkbai is closed. A setting left out of the file has its default.

The file is JSON: names in quotes, then a colon and the value, separated by commas, for example `"introVideo": false,`.

## gamePath

The folder of your *Warrior Kings: Battles* installation, which wkbai reads the game's files from. Default `"."`, the folder wkbai runs in. When that folder has no `data.bcp`, wkbai looks for a Steam installation of the game and sets `gamePath` to it ([installing wkbai](../install.md#the-first-start)). Write each backslash twice: `"D:\\Games\\Warrior Kings Battles"`.

## gameVersion

Which game the files are from: `2` for *Warrior Kings: Battles*, `0` for the original *Warrior Kings*. wkbai sets it on the first start: a game folder with the *Battles* program or its `WKB_Plus.bcp` is *Battles*. If the ground is drawn with black or wrong textures, check that it is `2`.

## userPath

Your own folder, where saves, replays, campaigns and crash reports go. Left out (the default), it is `%APPDATA%\wkbai`. See [files and folders](../files.md#your-own-folder).

## enhancedGraphics

`true` (the default): wkbai's enhanced renderer, which every graphics option beyond the original's needs. `false`: the game drawn as the original draws it. Takes effect after a restart.

## adapterIndex

Which graphics card to use when the computer has more than one, by its number (0, 1, ...). Default `-1`: the system's choice.

## windowWidth and windowHeight

The window's size in Windowed [display mode](system.md#display-mode), in pixels. Default 1280 x 720.

## ssaoRadius and ssaoStrength

The [ambient occlusion](graphics/shadows-light.md#ambient-occlusion)'s reach, in units (default 2.5), and its strength from 0 to 1 (default 0.7).

## bloomThreshold

How bright a part of the picture must be to [bloom](graphics/effects.md#bloom): default 0.72. Lower values make more of the picture glow.

## bloomIntensity

How strongly it glows: default 0.35.

## lightShaftStrength

How bright the light shafts between cloud shadows are (with [Clouds](graphics/sky-weather.md#clouds) at Light shafts or Volumetric): 0 to 4, default 1.

## fogOfWarBrightness

How bright the explored land that none of your units sees right now is drawn under the [fog of war](help:mechanic/fog-of-war): 0 (black) to 1 (as bright as seen land). Default 0.55.

## stainedGlass

`true` (the default): the stained-glass window of the lower left panel is drawn as real glass that bends the light. `false`: as the original's flat picture.

## cloudReflections

`true` (the default): with [volumetric clouds](graphics/sky-weather.md#clouds) and [Water](graphics/water.md#water) at Low or above, lakes, the sea and the ice mirror the clouds' own shapes and storm cells. `false`: they mirror the sky's colours alone, greyed by the cloud cover. **Cost:** about 0.1 ms.

## skyLight

`true` (the default): with [Model highlights](graphics/models.md#model-highlights), the light around models and their reflections take the sky's colours.

## modelSpecular

With Model highlights: how strong the highlights on models are. Default 1.

## terrainOriginalLight

`true` (the default): slopes facing away from the sun stay at least a little lit, as in the original. Used without [Light from the sky](graphics/shadows-light.md#light-from-the-sky).

## chimneySmoke

`true` (the default): smoke rises from the chimneys of HD models that have them.

## drips

`true` (the default): drips falling from HD models that have them.

## forest

`true` (the default): trees that stand still are kept and drawn by the graphics card, much faster on maps with large forests ([trees in the wind](graphics/terrain-grass.md#trees-in-the-wind)). `false`: every tree is drawn the old way, one by one.

## introVideo

`true` (the default): the original's intro video plays before the main menu, if it is in the game folder. `false`: straight to the menu.

## playerName

Your name in multiplayer; the [Multiplayer screen](../ui/frontend/multiplayer/index.md#name) sets it.

## joinAddress

The last address you joined; the Multiplayer screen's [Host address](../ui/frontend/multiplayer/index.md#host-address) sets it. Default `localhost`.

## hostPort

The network port a hosted game uses, and the one the local game list asks: default 1234. Guests then join with `address:port`.

## hostBind

Hosting: the network address to accept guests on, when the computer has several. Default: all of them.

## modsPath

The folder the [mod manager](../ui/frontend/mods.md) looks for mod folders in. Default `mods`.

## modAllowList

For a host with **Mods: only the host's**: the mods guests may have besides the host's own, as a list of their folder names or ids, for example `["BetterTrees", "hd-music"]`.

## gfxValidation

`true`: the graphics interface checks every call and reports mistakes, much slower. For finding graphics bugs only. Default `false`.

## madeSwimAnimations

`true` (the default): units that have no swimming animation of their own get one that wkbai makes from their walk, and are drawn at the water's surface while they swim ([swimming](../mechanics/water-and-currents.md#swimming)). `false`: none is made; a swimmer plays its walk, slowed down.

## editorSaveFormat

The format the [map editor](../ui/editor/saving.md#save-map) saves in. Left out (the default): wkbai's own map folder, `saved\Maps\<name>.wkbai`. `"original"`: the original game's files instead, a `.lvl` level and a `.bcm` map, as earlier versions of wkbai saved ([the original format](../ui/editor/saving.md#saving-in-the-original-format)).

## Kept by the menus

These are in the file too, written by their menus: `keyBindings` (the [Keyboard](controls/keyboard.md) page), `matchmaking` (the [Online](online.md) page; the server itself: [Hosting a Nakama server](../multiplayer/nakama-server.md)), `disabledMods` and `modLoadOrder` (the [Mods screen](../ui/frontend/mods.md)) and `agentControl` (the [Agent control](agent-control.md) page). Every other Options entry is saved under a name of its own; its help ids (`option/<name>`) include that name.
