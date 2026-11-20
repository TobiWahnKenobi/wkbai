---
title: System
ids: [ui/frontend/options/system, option/renderer#renderer, option/diligentBackend#renderer, option/displayMode#display-mode, option/vsync#v-sync, option/showFps#show-fps, option/dpiAware#full-display-resolution-high-dpi, option/uiScale#interface-size]
order: 50
---

# System

The window, the graphics interface and the size of the interface.

![The System page](shot:options-system)

## Renderer

The graphics interface wkbai draws with: **Direct3D 12** (the default) or **Vulkan**. Both look the same; try the other if one gives trouble with your graphics driver. Takes effect after a restart.

## Display mode

- **Windowed** (the default): a window you can move and resize.
- **Borderless fullscreen:** a window without a frame the size of the desktop; switching to other programs is quick.
- **Fullscreen:** the display switched to the game, at the desktop's resolution.

Takes effect at once.

## V-sync

On (the default): each frame waits for the display, so the picture never tears and the game runs at most at the display's refresh rate. Off: frames go out as fast as they are made; useful to measure speed, but the picture may tear.

## Show FPS

Off by default. On: the frames per second and the frame times show in the top left corner.

## Full display resolution (high DPI)

Off by default. On a display that Windows scales up (125 %, 150 % and so on):

- **Off:** Windows stretches wkbai's window, so the game draws at the lower resolution and looks softer.
- **On:** the game draws at the display's real resolution, and the interface is scaled up instead (see Interface size).

Takes effect after a restart.

## Interface size

How large the in-game interface (panels, buttons, text) is drawn: **Automatic** (the default), or **100 %** to **200 %**. Automatic is the display's own scale with Full display resolution on, else 100 %. The menus always scale with the window's height. Takes effect after a restart.

> wkbai: all of these are wkbai's own.
