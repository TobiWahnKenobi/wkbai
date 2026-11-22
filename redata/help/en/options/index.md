---
title: Options
ids: [options, ui/frontend/options, ui/frontend/options/tabs#the-tabs, ui/frontend/options/pages#the-tabs, ui/frontend/options/settings#the-kinds-of-setting, ui/frontend/options/ok#ok-and-cancel, ui/frontend/options/cancel#ok-and-cancel, ui/frontend/options/scroll#the-kinds-of-setting, FRONT_END_OPTIONS_TITLE]
order: 30
---

# Options

wkbai's settings: graphics, controls and keys, the interface, sound, the display, online play and agent control. Every setting has its own entry in these pages, with what it does, its range and its default, and for graphics what it costs.

![The Options screen, Graphics > View and fog](shot:options-view)

> wkbai: wkbai's other windows (Statistics, Timeline, the map editor's panels, the environment panel and the developer windows) are drawn in Warrior Kings' colours: dark brown windows, bronze title bars, tabs and buttons, gold check marks and sliders, and parchment text.

## Opening the options

- **From the main menu:** Options.
- **In a game:** Esc, then Options, or **F10** at any time (the key [`openOptions`](help:key/openOptions) can be changed). The game stays open behind, and most changes show at once.

## The tabs

The top row of tabs holds the groups; Graphics and Controls have a second row with their pages.

| Tab | Pages |
|---|---|
| **Graphics** | [View and fog](graphics/view-fog.md), [Sky and weather](graphics/sky-weather.md), [Shadows and light](graphics/shadows-light.md), [Water](graphics/water.md), [Terrain and grass](graphics/terrain-grass.md), [Models](graphics/models.md), [Picture](graphics/picture.md), [Anti-aliasing](graphics/anti-aliasing.md), [Effects](graphics/effects.md) |
| **Controls** | [Keyboard](controls/keyboard.md), [Mouse](controls/mouse.md), [Camera](controls/camera.md) |
| **Interface** | [Interface](interface.md) |
| **Sound** | [Sound](sound.md) |
| **System** | [System](system.md) |
| **Online** | [Online](online.md) |
| **Agent control** | [Agent control](agent-control.md) |

Each group remembers the page you had open in it. Some settings have no place in the menus at all; they are listed in [settings without a menu](config-file.md).

## The kinds of setting

- **On / Off:** a button that switches the setting.
- **Choice:** a button showing the current choice; each click takes the next one, and after the last comes the first again.
- **Slider:** drag the knob; the value is shown after it.
- **Text:** a field to type in; a grey hint shows while it is empty, and the default applies then.
- **Greyed out:** a setting that has no effect with the others as they are (its entry says what it needs).
- **(after a restart):** the setting is saved with OK, but takes effect the next time wkbai starts.
- **More than fits:** "Scroll for more settings" at the bottom right; the mouse wheel scrolls the page.

## OK and Cancel

- **OK** saves every change to the settings file `wkconfig.json` and applies those that need it, such as the sound volumes, the Extend map and the online settings.
- **Cancel** (or Esc) puts every setting back as it was when you opened the Options, keys included.

## Where to start with graphics

See [Graphics](graphics/index.md) for what each page does, which settings cost the most, and what to turn down on a slower computer.

> wkbai: the original's Options have a handful of settings on a few pages. wkbai's are its own, many times more, drawn with the original's sliders and buttons. The original's own switches that still matter (Camera Snapping, Single click selects all units, 3D Sound) are here too.
