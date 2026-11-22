---
title: Questions and answers
ids: [help/faq]
order: 920
---

# Questions and answers

Short answers to common questions, each with a link to the full story.

## Getting started

**Do I need the original game?**
Yes. wkbai contains no part of *Warrior Kings: Battles*; it reads the files of your own copy ([installing wkbai](install.md)).

**Does wkbai change my game's folder?**
No. It only reads from it. Saves, settings and everything else go to wkbai's folder and your own folder ([files and folders](files.md)).

**Does it run on Linux or a Mac?**
Not yet: wkbai runs on 64-bit Windows only for now.

**Why is the first start so slow?**
wkbai prepares its shaders for your graphics card once and keeps them. Later starts are much quicker ([troubleshooting](troubleshooting.md#the-game-runs-slowly)).

**How do I go back to the default settings?**
Close wkbai and delete `wkconfig.json` in its folder. You lose your keys too, and wkbai looks for the game again ([troubleshooting](troubleshooting.md#wkbai-doesnt-start)).

## Playing

**Where do I start?**
With [your first skirmish](getting-started.md), or the [tutorials](ui/frontend/tutorial.md).

**How do I get help for a button or a unit?**
Point at it and press F1, or right-click it and choose Help ([using the help](using-help.md)).

**Can I change the keys?**
Yes, every one, with two keys per action ([Keyboard](options/controls/keyboard.md)).

**Can I load my saves from the original game?**
Yes. wkbai reads and writes saves in the original's format ([saves and replays](mechanics/saves-and-replays.md)).

**Can I go on with a campaign I started in the original game?**
No. wkbai keeps campaigns in its own files and can't read the original's profiles ([Campaign](ui/frontend/campaign.md)).

**Why is it night? Why are my fields bare?**
You switched on day and night or the seasons in the lobby's [Seasons & Weather tab](ui/frontend/skirmish/weather.md). They are off at first. See [day and night](mechanics/day-night.md) and [seasons and weather](mechanics/seasons-weather.md).

**Why do my peasants stop working in the evening?**
With day and night on, they may sleep indoors at night, and farmers may spend the winter inside ([peasants at night and in winter](mechanics/peasant-lodging.md)).

**Are there cheats?**
Not the original's chat codes. A skirmish can switch on [Cheat mode](ui/frontend/skirmish/weather.md#cheat-mode), which opens the [developer and cheat tools](controls/developer-tools.md). It is never available in multiplayer.

**Can I make the game faster or slower?**
Yes, with keypad + and −: each press doubles or halves the speed ([top bar](ui/hud/top-bar.md)).

**Can I watch a game again?**
Switch on Record game in the lobby's settings before the start, then open Load Game > Replays afterwards ([saves and replays](mechanics/saves-and-replays.md#recordings)).

## Opponents and multiplayer

**The AI Generals are too passive, or too hard. What can I change?**
Their tier, Aggressive AI, the ministry AI or a Lua AI in the lobby ([AI opponents](mechanics/ai-opponents.md)).

**Can I play against people with the original game?**
No. wkbai has its own network play; the original's online service, GameSpy, is gone ([Multiplayer](mechanics/multiplayer.md)).

**How do I play over the Internet?**
Host a game and give your friends your address, or use a matchmaking server ([Multiplayer](ui/frontend/multiplayer/index.md), [Online options](options/online.md), [running a Nakama server](multiplayer/nakama-server.md)).

## Mods and the look

**Can I use mods such as WKB Plus?**
Yes. wkbai loads them the way the original does, and its Mods screen can switch them on and off and set their order ([Mods](ui/frontend/mods.md)).

**Can it look like the original?**
Yes. Set `enhancedGraphics` to `false` in `wkconfig.json` ([settings without a menu](options/config-file.md#enhancedgraphics)), or set the graphics options to their Original levels ([graphics](options/graphics/index.md)).

**My computer is slow. What should I turn down?**
[On a slower computer](options/graphics/index.md#on-a-slower-computer) lists the settings in the order that helps most.

## Problems

**wkbai crashed. What now?**
It saved a crash report and told you where. See [when wkbai crashes](troubleshooting.md#when-wkbai-crashes).

**Is wkbai made by the makers of the game?**
No. It is an independent fan project, not affiliated with Black Cactus Games or the owners of the rights to *Warrior Kings* ([about wkbai](about.md)).
