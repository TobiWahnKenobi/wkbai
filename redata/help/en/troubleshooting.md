---
title: Troubleshooting
ids: [help/troubleshooting, help/crash#when-wkbai-crashes, help/crash-reports#when-wkbai-crashes, help/log#the-log, help/performance#the-game-runs-slowly, help/graphics-problems#graphics-problems, help/startup-problems#wkbai-doesnt-start]
order: 910
---

# Troubleshooting

What to do when wkbai doesn't start, looks wrong, runs slowly or crashes.

## wkbai doesn't start

- **"Could not find the file data.bcp":** wkbai doesn't know where the game is. Set `gamePath` in `wkconfig.json` ([installing wkbai](install.md#when-wkbai-cant-find-the-game)).
- **"wkconfig.json Parsing Error":** the settings file has a mistake, often a missing comma, quote or brace after you edited it. The message says where. Correct it, or delete `wkconfig.json`: wkbai then starts with its default settings and writes a new file. You lose your settings and keys, and wkbai looks for the game again.
- **It closes at once with no message:** look at the [log](#the-log) in its text window, and check `wkconfig.json` for mistakes as above.
- **It starts on the wrong graphics card,** or doesn't start since you changed the [Renderer](options/system.md#renderer): open `wkconfig.json` and set `"diligentBackend": "d3d12"` (or `"vulkan"`) to choose the renderer, and `adapterIndex` to choose the card ([settings without a menu](options/config-file.md#adapterindex)).

## Graphics problems

- **Trouble with one renderer** (black areas, flicker, a crash when a map loads): switch between Direct3D 12 and Vulkan in [Options > System](options/system.md#renderer) and restart. Update your graphics driver too.
- **Black or wrong ground textures:** wkbai may take the game for the original *Warrior Kings*. Set `"gameVersion": 2` in `wkconfig.json` ([gameVersion](options/config-file.md#gameversion)).
- **A blurry picture on a high-resolution screen:** switch on [Full display resolution (high DPI)](options/system.md#full-display-resolution-high-dpi), and set [Interface size](options/system.md#interface-size) if the interface is then too small.
- **Torn pictures:** switch on [V-sync](options/system.md#v-sync).
- **Something looks different from the original:** [enhancedGraphics](options/config-file.md#enhancedgraphics) set to `false` draws the game as the original draws it, without wkbai's effects.

## The game runs slowly

- Switch on [Show FPS](options/system.md#show-fps) to see the frame rate.
- Turn down the most costly graphics settings first: [on a slower computer](options/graphics/index.md#on-a-slower-computer) lists them in order. [Upscaling](options/graphics/anti-aliasing.md#upscaling-amd-fsr) gives the most for the least.
- **The first start and the first map load are slow:** wkbai prepares its shaders for your graphics card once, and again after a driver update. Later starts are much faster.
- **A map takes long to load:** [Extend map](options/graphics/terrain-grass.md#extend-map) builds the land beyond the map's edge, up to about 12 seconds at Ultra. [Keep the far land on disk](options/graphics/terrain-grass.md#keep-the-far-land-on-disk) makes the next load of the same map quicker.
- **The game itself runs slow** (units move slowly, the speed line shows less than the speed you chose): the computer can't keep up with the simulation. This is rare even with eight players; fewer AI Generals help.

## When wkbai crashes

wkbai writes a crash report and tells you where it is: a file `crash-<date>-<time>.dmp` in the `crashes` folder of your own folder, `%APPDATA%\wkbai\crashes` ([files and folders](files.md#your-own-folder)).

To report a crash:

1. Write down what you were doing: the map, the screen, the last order.
2. Attach the `.dmp` file and the [log](#the-log).
3. Send them to wkbai's project; `SOURCE.txt` beside `wkbai.exe` says where it is.

The report holds the state of the program at the crash, for a programmer's debugger. It holds no game files.

## The log

The text window that opens with wkbai shows its log: what it loads, which game folder and mods it uses, the matchmaking's messages and errors. Closing that window closes wkbai.

To keep the whole log in a file, start wkbai from a command prompt in its folder:

```
wkbai.exe > wkbai-log.txt
```

The log then goes into `wkbai-log.txt` instead of the window, right up to a crash.

## Other problems

- **A mod makes trouble:** switch it off on the [Mods screen](ui/frontend/mods.md), or go back to the **Original order**.
- **Keys don't do what they should:** [Reset all to defaults](options/controls/keyboard.md#reset-all-to-defaults) on the Keyboard page.
- **You can't click or move the camera during a campaign scene:** the level has locked the interface for its scene ([when the interface is locked](ui/hud/index.md#when-the-interface-is-locked)). Its window's button gives it back; Esc still opens the menu.
- **Guests can't join your game:** a hosted game uses the network port 1234, UDP ([hostPort](options/config-file.md#hostport)). Your firewall must let wkbai through. Over the Internet without a matchmaking service, the port has to reach your computer, which may mean forwarding it on your router. See [Multiplayer](ui/frontend/multiplayer/index.md) and the [Online](options/online.md) options.
- **The help shows frames instead of screenshots:** they haven't been taken on your computer yet ([using the help](using-help.md#the-help-window)).
