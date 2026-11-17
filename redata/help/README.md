# wkbai's in-game help: how pages are written

The help is a folder of Markdown pages. wkbai shows them in its help viewer (F1, or right-click > Help on any part of the interface). `tools/help/build_html.py` turns the same pages into a static HTML manual for a browser. The design is in the RE repo's `docs/engine/help-system.md`.

## Files

- `redata/help/<language>/<path>.md`, UTF-8. English is `en/`. A missing page in another language falls back to `en/`.
- `redata/help/img/`: our own pictures and diagrams (never game art). A diagram's source (`.mmd`, `.dot`, `.svg`) sits next to its `.png`.
- `redata/help/shots/*.json`: the lists of screenshots the pages use, one file per area (see "Screenshots").

## Front matter

Every page starts with a block like this:

```
---
title: Minimap
ids: [ui/hud/minimap, ui/hud/minimap/zoom#zoom-buttons, MINIMAP]
order: 30
---
```

- **title:** the name in the contents tree, the search results and the window's title bar.
- **ids:** the help ids that open this page. The page's path without `.md` (`ui/hud/minimap`) is always one of them. `id#slug` opens the page at a heading. A heading's slug is its text in lower case, with spaces as `-` and everything but letters, digits and `-` dropped ("Zoom buttons" -> `zoom-buttons`).
- **order:** the place among its siblings in the contents tree (smaller first; then by title).
- **parent:** optional; a page from another folder to sit under in the tree.

## Help ids

A help id names a thing in the game. The interface tags each part of itself with one, and the viewer opens the page that lists it.

| Kind | Form | Example |
|---|---|---|
| A screen or window | `ui/<area>/<screen>` | `ui/frontend/skirmish` |
| A part of one | `ui/<area>/<screen>/<part>` | `ui/frontend/skirmish/players/team` |
| A setting | `option/<settings key>` | `option/cameraSnapping` |
| A key action | `key/<action>` | `key/openMenu` |
| A unit, building, upgrade | `unit/<blueprint>`, `building/<blueprint>`, `upgrade/<blueprint>` | `unit/Peasant` |
| A command (button or order) | `command/<command>` | `command/Gather` |
| A game rule | `mechanic/<topic>` | `mechanic/fog-of-war` |

- Ids are case sensitive and use the game set's own names for blueprints and commands, spaces included (`building/Peasant Hut`).
- **Fallback:** an id without a page falls back to its parent (`ui/hud/minimap/zoom` -> `ui/hud/minimap` -> `ui/hud` -> the start page), so every part of the interface opens something useful even before it has a page of its own.

## Links

- **Between pages:** relative links to the `.md` file, with an optional heading: `[the minimap](../hud/minimap.md#zoom-buttons)`. They work in the game, in the HTML manual and on GitHub.
- **By id:** `[Peasant](help:unit/Peasant)`, for pages whose path the writer doesn't know.
- **Show it on screen:** `[Show me](show:ui/hud/minimap)` makes the viewer flash that part of the interface when it is on screen.
- **Outside:** `https://` links open in the system browser, after a question.

## Pictures, screenshots, videos, diagrams

- **Screenshot:** `![The skirmish lobby](shot:frontend-skirmish-players)`. Screenshots show game art, so they are never in git and never shipped. Each player's wkbai takes them itself, from the lists in `shots/`; until then the viewer shows the alt text in a frame.
- **Our own picture or diagram:** `![Formation wheel](../../img/formation-wheel.png)`.
- **Game art from the player's install:** `![](game:Interface/InGame/Info Panel/Heart.tga)`, read from the installed game at run time, never copied.
- **Video:** an image whose file is `.mp4` or `.webm` (`![Drag a formation](../../img/clips/formation-drag.mp4)`) plays inline with a play button. Clips of the game are local, like screenshots (`shot:` names).
- **Live game data:** a fenced block with the info string `facts` is replaced by a table from the loaded game set (costs, hit points, armour, what it trains, what it needs). A mod's units get correct numbers without a page change:

  ````
  ```facts
  blueprint: Peasant
  ```
  ````

## Screenshots: `shots/*.json`

Each file is a JSON array, one entry per screenshot:

```json
{ "name": "frontend-skirmish-players", "what": "Skirmish lobby, Players tab, 4 slots filled", "menu": ["skirmish"] }
```

- `menu`: front-end screens to open (`Screens.<name>` in `FrontEndUI.lua`), for `menuTest`.
- `game`: `{ "save": "...", "camera": [x, y, z, pitch, yaw], "openMenu": [...], "select": "..." }` for a shot in a running game.
- `what`: what must be visible, for whoever sets up the shot.

## Writing style

- Plain words, short sentences, lists with a bold lead-in where a paragraph would run long.
- Say what the player sees and does first, the rule behind it second. Numbers where the game has them.
- Our own words only: never copy text from the game's `Language.txt`, its manual or other games' help.
- Where wkbai differs from the original game, say so in a short "wkbai" note.
- wkbai is not affiliated with Black Cactus, the rights holders of Warrior Kings, or wkbre2 / AdrienTD (the start page says so).
