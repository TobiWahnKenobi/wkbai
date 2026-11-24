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

A second MCP server, `wkbai_author_mcp.py`, is for designing maps rather than playing them. It doesn't open the game's window: every tool runs one of wkbai's headless `--author` commands. The design is in the RE repo's `docs/engine/map-authoring.md` (steps 17.3, 17.4 and 17.5; the water, object, check and preview tools come in later steps).

| Tool | What it does |
|---|---|
| `catalog` | What can go on a map, read from the game set and texture databases that are loaded, so a mod's units and textures are in it. `section`: `summary` (the counts, classes, items and resources, terrains, passability fields, skies, trees, map size limits and the environment's names), `blueprints` (one class, e.g. `class: building`, with footprints, items, passability, held resources, placeable), `blueprint` (one by name), `textures` (texture groups with terrain type, passability, mean colour, transitions and edge rules in short), `texture` (one group; `edges: true` gives every texture's edge rules), `terrains`, `environment`, `skies`, `trees`, `all` (only the file's path). `mod`: a mod's name, or `none` for the base game alone. |
| `describe_map` | An existing map, a `.wkbai` folder or an original level such as `2 Player 4 Peaks.lvl`, as numbers: its size, height range, histogram and slopes, its water (lakes, waterways, falls, tiles under water), material shares, and object counts by class, blueprint and player. It also returns a top-down picture with the relief, the water and the objects as symbols. |
| `new_map` | A new map folder: `map.json` and a flat `heights.png`, with the size, edge, height, ground material, sky and players (the compiler makes each player's base). |
| `import_map` | A map folder made from an original level. |
| `get_map` | A folder's `map.json`, its files, and the last compile's `report.json`. |
| `set_map` | Changes `map.json` by a JSON patch: a list of RFC 6902 operations, or an object as an RFC 7386 merge patch. |
| `compile` | Compiles the folder into `build\` and returns the report, with warnings and their tile coordinates. |
| `plan_terrain` | Sets or changes the map's feature plan (`map.json`'s `terrain` and `features`: hills, ranges with passes, plateaus with ramps, valleys, basins, coasts, islands, rivers with fords (`"water": true` puts water in a river: a sloped waterway down its course, never rising, falls where the bed drops; `"speed"` m/s its current), passes, terraces, flat sites; positions in playable metres, heights in metres) and generates the land from it. `features` replaces the list, `add` appends, `remove` drops by id or index, `patch` merges into `terrain`. |
| `generate_terrain` | Generates `heights.png` from the plan: shapes, noise, thermal and hydraulic erosion, terraces, then passes, ramps and river beds carved last. The same seed gives the same heights, and `heights.hand.png` is never touched. Returns the stats: height range, slope histogram (the game's own per-tile measure), walkable share (45-degree units; 30 and 60 too; the largest walkable region), the sea, each feature's checks (plateau flatness, ramp and pass slopes, the river bed never rising) and the time taken, plus the relief picture. |
| `import_heightmap` | A heightmap picture as the plan's base layer (`layers/source.png`): a PNG of 8 or 16 bits, a raw file, an SRTM `.hgt` or a GeoTIFF, cropped, mapped to a height range and blended with the plan, so rivers can be carved into real land. |
| `import_elevation` | Real elevation data from files already on disk (Copernicus GLO-30 GeoTIFF, SRTM `.hgt`), fitted to the map: squeezed horizontally, the real slopes kept or exaggerated, flattened where too steep, the sea found. It never downloads: without the files it names each tile with its source, URL, size and licence, so the user is asked first. The licence and attribution go into `map.json`'s references. |
| `set_material_rules` | Sets or changes the ground's rules (`map.json`'s `ground`): a `preset` (`temperate`, `nordic`, `dry`), the `default` material, ordered `rules` (the first that holds wins; conditions as `[min, max]` on height, heightRel, aboveSea, slope, curvature, moisture, drainage, river, seaDistance, noise, a mask in `layers/<name>.png`, water), `add` (rules put first), `remove` (by index), `materials` (added or changed: groups with weights), `moisture`; then paints. |
| `paint_materials` | Compiles the tiles from the rules: a material per vertex, then the edge-rule solver (every tile's sides matched to its neighbours by the texture database's EDGE rules, bridging groups between materials with no transition, a random picture, turn and flips from the seed; hand tiles kept). Returns the materials report (shares, blocked and slow tiles, every unmatched tile with its place and reason, the moisture's spread) and the top-down material map. |
| `material_report` | The last paint's report and material map without compiling (`moisture: true` adds the moisture picture), or with `presets: true` the material data: every material, the presets and their rules. |
| `set_water` | Sets or changes the map's water by intent (`water.json`): lake points (`lakes`, `addLakes`, `removeLakes`: `[x, z, level]`), the rivers' courses (`waterways`, `addWaterways`, `removeWaterways`: points, width, `speed` m/s, `speeds` one per point), the current of existing waterways (`speed`: by index or by-name, e.g. `terrain:beck`), sea regions (`seas`: name, wind, waves; painted A-H), `paint` (the surface by rectangle, circle or all: auto, river, lake, a sea region), placed `waterfalls`, `autoWaterfalls`, and the far-land hints (`farland`, `addFarland`: kind sea, land, mountains, lowland or river, toward, spread, from, weight, at, level) Extend map follows beyond the map; then compiles and returns the water report. |
| `water_report` | The water by intent from the last compile (`compile: true` compiles first): each lake's flood (tiles, depth, sea or lake, leaks off the map, past the painted lake, over half the map, a lower lake drowned; puddles on flat ground), each waterway (wet share, dry stretches, where it rises, its speed), painted river tiles left dry, the falls the renderer will draw, the rapids of a sloped river's steep stretches, placed falls that draw nothing, the rivers leaving the map and the far-land hints; the water warnings with their places. |

The server works in a folder of its own (`--work`, by default `%TEMP%\wkbai-author-run`). It writes its own `wkconfig.json` there with `"test": true`, so a crash writes a dump and never opens a dialog. Maps go into `<work>\profile\saved\Maps\<name>.wkbai`, where that folder's game loads them as `Maps\<name>.wkbai`. With `--user-path <folder>`, they go into another wkbai user folder instead, such as yours (`%APPDATA%\wkbai`), so that the skirmish list shows them.

```
claude mcp add wkbai-author -- python tools\agent\wkbai_author_mcp.py --game-path "C:\Games\Steam\steamapps\common\Warrior Kings Battles"
```

The materials are data: `redata\Maps\Materials\All_Textures.json` for the base game's texture database (a mod or a texture set ships its own `Maps\Materials\<database name>.json`, which replaces it).

The same commands work by hand, from a folder holding a `wkconfig.json` and a copy of `redata`:

```
wkbai --author catalog [--mod <name>|none] [--gameset <file>] [--edges] [--out catalog.json]
wkbai --author describe "2 Player 4 Peaks.lvl" --png 4peaks.png --out 4peaks.json
wkbai --author import "2 Player 4 Peaks.lvl" C:\maps\4Peaks.wkbai
wkbai --author C:\maps\4Peaks.wkbai
wkbai --author terrain C:\maps\4Peaks.wkbai [--seed <n>] --out stats.json
wkbai --author terrain-stats C:\maps\4Peaks.wkbai
wkbai --author heightmap C:\maps\4Peaks.wkbai --options heightmap.json
wkbai --author elevation C:\maps\4Peaks.wkbai --options elevation.json
wkbai --author elevation-tiles --options elevation.json
wkbai --author materials C:\maps\4Peaks.wkbai [--out materials.json] [--png materials.png]
wkbai --author material-presets [--database <file>] [--out presets.json]
```

Without `--out`, the JSON goes to standard output between the lines `AUTHOR-JSON-BEGIN` and `AUTHOR-JSON-END`. The last line printed is always `AUTHOR ok, ...` or `AUTHOR failed: ...`, and the exit code is 0 or 1.
