# Agent control tools

A program, such as an AI assistant, can play wkbai the way a person does. It moves the mouse, clicks and presses keys, and the game turns that into commands exactly as it does for a person. The option is off by default (Options > Agent control), and every player sees when it is on. The protocol is described in `wkbai/AgentServer.h`, and the design in the RE repo's `docs/engine/agent-control.md`.

| File | What it is |
|---|---|
| `wkbai_agent.py` | The Python client. `Game` starts wkbai with agent control on, in a folder of its own (never your profile). `Agent` sends the protocol's ops (input, aimed clicks, `observe`, `probe`, `screenshot`) and offers composites such as `box_select` and `place`. |
| `scenarios.py` | The first evaluation scenarios: `menus`, `pathfinding`, `building`, `versus-ai`. Each prints a JSON result and exits with 0 when its checks hold. |
| `wkbai_agent_mcp.py` | An MCP server (stdio) offering the same as tools (`wkbai_start`, `wkbai_observe`, `wkbai_screenshot`, `wkbai_click_object`, and so on), so that an assistant can play from a chat. It needs no package beyond Python, plus Pillow for screenshots. |

Running them:

```
set WKB_GAME_PATH=C:\Games\Steam\steamapps\common\Warrior Kings Battles
python tools\agent\scenarios.py pathfinding
claude mcp add wkbai -- python tools\agent\wkbai_agent_mcp.py
```

The game runs in its own window and doesn't need your mouse or the foreground. Ctrl+Alt+F12 takes the input over from the agent, and pressing it again gives it back.

Probes (paths, orders, the game's hash, objects hidden by the fog of war) work only where cheat keys do: a single-player game in cheat mode, the test runs (`skirmish` mode here), or a replay.

## Map authoring

A second MCP server, `wkbai_author_mcp.py`, is for designing maps rather than playing them. It doesn't open the game's window: every tool runs one of wkbai's headless `--author` commands. The design is in the RE repo's `docs/engine/map-authoring.md` (step 17.3; the land, ground, water, object, check and preview tools come in later steps).

| Tool | What it does |
|---|---|
| `catalog` | What can go on a map, read from the game set and texture databases that are loaded, so a mod's units and textures are in it. `section`: `summary` (the counts, classes, items and resources, terrains, passability fields, skies, trees, map size limits and the environment's names), `blueprints` (one class, e.g. `class: building`, with footprints, items, passability, held resources, placeable), `blueprint` (one by name), `textures` (texture groups with terrain type, passability, mean colour, transitions and edge rules in short), `texture` (one group; `edges: true` gives every texture's edge rules), `terrains`, `environment`, `skies`, `trees`, `all` (only the file's path). `mod`: a mod's name, or `none` for the base game alone. |
| `describe_map` | An existing map, a `.wkbai` folder or an original level such as `2 Player 4 Peaks.lvl`, as numbers: its size, height range, histogram and slopes, its water (lakes, waterways, falls, tiles under water), material shares, and object counts by class, blueprint and player. It also returns a top-down picture with the relief, the water and the objects as symbols. |
| `new_map` | A new map folder: `map.json` and a flat `heights.png`, with the size, edge, height, ground material, sky and players (the compiler makes each player's base). |
| `import_map` | A map folder made from an original level. |
| `get_map` | A folder's `map.json`, its files, and the last compile's `report.json`. |
| `set_map` | Changes `map.json` by a JSON patch: a list of RFC 6902 operations, or an object as an RFC 7386 merge patch. |
| `compile` | Compiles the folder into `build\` and returns the report, with warnings and their tile coordinates. |

The server works in a folder of its own (`--work`, by default `%TEMP%\wkbai-author-run`). It writes its own `wkconfig.json` there with `"test": true`, so a crash writes a dump and never opens a dialog. Maps go into `<work>\profile\saved\Maps\<name>.wkbai`, where that folder's game loads them as `Maps\<name>.wkbai`. With `--user-path <folder>`, they go into another wkbai user folder instead, such as yours (`%APPDATA%\wkbai`), so that the skirmish list shows them.

```
claude mcp add wkbai-author -- python tools\agent\wkbai_author_mcp.py --game-path "C:\Games\Steam\steamapps\common\Warrior Kings Battles"
```

The same commands work by hand, from a folder holding a `wkconfig.json` and a copy of `redata`:

```
wkbai --author catalog [--mod <name>|none] [--gameset <file>] [--edges] [--out catalog.json]
wkbai --author describe "2 Player 4 Peaks.lvl" --png 4peaks.png --out 4peaks.json
wkbai --author import "2 Player 4 Peaks.lvl" C:\maps\4Peaks.wkbai
wkbai --author C:\maps\4Peaks.wkbai
```

Without `--out`, the JSON goes to standard output between the lines `AUTHOR-JSON-BEGIN` and `AUTHOR-JSON-END`. The last line printed is always `AUTHOR ok, ...` or `AUTHOR failed: ...`, and the exit code is 0 or 1.
