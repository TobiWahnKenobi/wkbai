"""wkbai map authoring as an MCP server (stdio), so that an AI assistant such as Claude Code can
design maps for wkbai: know exactly what can go on a map (catalog), read existing maps back
(describe_map, get_map), make and change map folders (new_map, import_map, set_map, compile), and
make the land (plan_terrain, generate_terrain, import_heightmap, import_elevation), and texture it
by rule (set_material_rules, paint_materials, material_report).
Steps 17.3, 17.4 and 17.5 of the RE repo's docs/engine/map-authoring.md; the water, object,
check and preview tools follow in later steps. import_elevation never downloads: it names the
tiles an area needs, and the agent asks the user before each download.

Everything runs wkbai's headless commands (no window, nothing on the screen):
    wkbai --author catalog [--mod <name>|none] [--edges] --out <file>
    wkbai --author describe <map> --png <file> --out <file>
    wkbai --author import <level> <folder>
    wkbai --author <folder>                    (compile)
    wkbai --author materials <folder> --out <file>   (compile, the materials report)
    wkbai --author material-presets --out <file>
in a work folder of its own with its own wkconfig.json ("test": true, so a crash never shows a
dialog) and its own user folder: maps are made in <work>\\profile\\saved\\Maps\\<name>.wkbai, which the
game there loads as "Maps\\<name>.wkbai". --user-path puts them in another wkbai user folder (e.g.
the player's, so the skirmish list shows them).

Register it, e.g. in Claude Code:
    claude mcp add wkbai-author -- python <repo>/tools/agent/wkbai_author_mcp.py --game-path "<Warrior Kings Battles folder>"

The protocol (JSON-RPC 2.0, one message a line on stdin/stdout) is written out here to need no
package beyond Python. Licensed under the GNU General Public License 3 (as wkbai).
"""
import argparse
import base64
import copy
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import traceback
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
DEFAULT_EXE = os.path.join(REPO, "build", "wkbai", "RelWithDebInfo", "wkbai.exe")
PROTOCOL = "2024-11-05"


class AuthorError(Exception):
    def __init__(self, reply):
        super().__init__(json.dumps(reply))
        self.reply = reply


def obj(props, required=()):
    return {"type": "object", "properties": props, "required": list(required)}


NUM = {"type": "number"}
INT = {"type": "integer"}
STR = {"type": "string"}
BOOL = {"type": "boolean"}
MAP = {"type": "string", "description": "a map folder: its name (<name> or <name>.wkbai, in the maps folder) or a full path"}

SECTIONS = ["summary", "blueprints", "blueprint", "textures", "texture", "terrains", "environment", "skies", "trees", "all"]

TOOLS = [
    ("catalog", "What can go on a map, from the loaded game set and texture database (a mod's units and textures included): "
     "section=summary (counts, classes, items and resources, terrains, passability fields, skies, trees, map size limits, environment names), "
     "blueprints (every blueprint of a class, e.g. class=building, with footprint, items, passability, resource, placeable; name: a part of the name to filter by), "
     "blueprint (one by name, every class), textures (the texture groups: terrain type, passability, mean colour, transitions, edge rules in short; name filters), "
     "texture (one group by name; edges=true for every texture's edge rules), terrains, environment, skies, trees, all (the whole file's path only). "
     "mod: a mod's name (e.g. WKB_Plus) or none (the base game alone); default the mods as configured.",
     obj({"section": {"type": "string", "enum": SECTIONS}, "class": STR, "name": STR, "mod": STR, "edges": BOOL, "refresh": BOOL})),
    ("describe_map", "An existing map in numbers and a top-down picture: size, height range, histogram and slopes, the water (lakes, waterways, falls, tiles under water), "
     "material shares (texture groups, terrains), object counts by class, blueprint and player, the placement checks (every building against the server's rules) and the bases' fairness. map: a map folder (name or path) or an original level "
     "(e.g. \"2 Player 4 Peaks.lvl\" or Save_Games\\\\<level>.lvl). picture: false for the numbers only.",
     obj({"map": STR, "picture": BOOL, "mod": STR}, ["map"])),
    ("new_map", "Make a new map folder: map.json and flat heights.png (no level: the compiler makes the players' bases from players). "
     "playable: [w, h] tiles (16-1024), edge tiles (default 30), height steps (default 20; a step is scale metres, default 1), "
     "fill: the ground's texture group (default GRASS1), sky (a catalog sky name or path), players: [{name, colour, start [x, z] in playable metres, base, peasants}]. "
     "replace: overwrite a folder there. Compile it to play it.",
     obj({"name": STR, "playable": {"type": "array", "items": INT}, "edge": INT, "height": NUM, "scale": NUM, "fill": STR, "sky": STR,
          "players": {"type": "array", "items": {"type": "object"}}, "seed": INT, "replace": BOOL}, ["name", "playable"])),
    ("import_map", "Make a map folder from an original map's level (e.g. \"2 Player 4 Peaks.lvl\"): its heights, every tile, the lakes and water design, the level. name: the folder's (default the level's).",
     obj({"level": STR, "name": STR, "replace": BOOL}, ["level"])),
    ("get_map", "A map folder's map.json, its files, and the last compile's report (report.json) if there is one.", obj({"map": MAP}, ["map"])),
    ("set_map", "Change a map folder's map.json by a JSON patch: a list of RFC 6902 operations ([{op: add|remove|replace|move|copy|test, path: \"/sky\", value}]) "
     "or an object (an RFC 7386 merge patch: null removes a key). The result must still be a wkbai map; compile to check it.",
     obj({"map": MAP, "patch": {"type": ["array", "object"]}}, ["map", "patch"])),
    ("compile", "Compile a map folder into its build\\ cache (heights, tiles, water, the level with the rules' objects) and return the report: sizes, hand vertices and tiles, lakes, tiles under water, objects, warnings with tile coordinates.",
     obj({"map": MAP}, ["map"])),
    # --- the land (step 17.4): MapTerrain.h ---
    ("plan_terrain", "Set or change a map's feature plan (map.json \"terrain\" and \"features\"), then generate the land from it (generate: false to only save the plan). "
     "Positions in playable metres [x, z] (x east, z north, 0 the south-west corner), heights in metres. "
     "terrain: {base (the plain's height, default 20), seaLevel, beach (m flattened around the sea level), noise {amplitude m, scale m, octaves, ridged 0-1, warp}, "
     "erosion {hydraulic 0-2, thermal 0-2, talusDeg, dropsPerVertex}}. "
     "features: a list, each {kind, id?, role?, irregularity 0-1.5 (how far the outline wanders, default 0.5)} and by kind: "
     "hill {at, radius, height (peak), shape: dome|peak}; range {line [[x,z],...], width, height (peak, or [start, end]), passes (a count or points [[x,z]] or [{at, height}]), passHeight, passWidth, passMaxSlopeDeg}; "
     "plateau {at (point + radius, or a polygon [[x,z],...]), height, cliffs 0-1, ramps (count), rampsAt [[x,z]], rampDeg, rampWidth}; "
     "valley {line, width, depth (below base) or floorHeight [from, to], fall, floor (flat share 0-0.9)}; basin {at, radius, depth, water (a lake level)}; "
     "coast {side north|south|east|west (or line + seaSide [x,z]), sea (m of sea from the side), seaDepth, shelf, beach, cliffs 0-1}; "
     "island {at, radius or polygon, height, seaDepth, shelf}; river {line or from/to, width, depth, meander 0-1, fords (count), valleyWidth, valleyDepth, bank, bankDeg, water (true: water in it, a sloped waterway down its course from source to mouth, never rising, its falls where the bed drops; false or left out: a dry channel painted as river bed), speed (m/s: its current, for the renderer and the gameplay; left out: found from the land)}; "
     "pass {at, height (saddle), width, direction (deg, the way across), maxSlopeDeg}; terraces {at? (none: everywhere), step m, strength 0-1}; flat {at, radius, height? (default the mean)}. "
     "features replaces the list; add appends; remove takes ids or indices; patch is a merge patch on terrain. Returns the stats as generate_terrain.",
     obj({"map": MAP, "terrain": {"type": "object"}, "patch": {"type": "object"}, "features": {"type": "array", "items": {"type": "object"}},
          "add": {"type": "array", "items": {"type": "object"}}, "remove": {"type": "array"}, "generate": BOOL, "picture": BOOL}, ["map"])),
    ("generate_terrain", "Generate heights.png from the map's feature plan (shapes, noise, hydraulic and thermal erosion, terraces, carved passes, ramps and rivers), deterministic from the seed; "
     "heights.hand.png (hand edits) is left alone. Returns the stats: height range, slope histogram (per tile, the game's own measure), walkable share "
     "(tiles a 45-degree unit can stand on; 30 and 60 too; the largest walkable region), the sea, each feature's checks (plateau flatness, ramp and pass slopes, "
     "river bed falling), time taken. seed: another seed for this run. picture (default true): the relief from describe_map.",
     obj({"map": MAP, "seed": INT, "picture": BOOL}, ["map"])),
    ("import_heightmap", "Use a heightmap picture as the land (the plan's base layer layers/source.png, blended with the plan's features, so e.g. rivers can be carved into it): "
     "file (a PNG 8 or 16-bit, a .raw/.r16/.r8, an SRTM .hgt or a GeoTIFF; a full path), raw {width, height, bits 8|16, bigEndian, signed} for a raw file, "
     "crop [x0, y0, x1, y1] pixels, cover playable (the picture over the playable area, the edge band continuing it mirrored) or whole, "
     "range [lo, hi] metres the picture's lowest and highest map to (default [2, 60]), or verticalScale + offset, weight 0-1 (blend with the plan's base), "
     "licence and attribution (go into map.json's references). Generates, returns the stats.",
     obj({"map": MAP, "file": STR, "raw": {"type": "object"}, "crop": {"type": "array", "items": NUM}, "cover": {"type": "string", "enum": ["playable", "whole"]},
          "range": {"type": "array", "items": NUM}, "verticalScale": NUM, "offset": NUM, "weight": NUM, "licence": STR, "attribution": STR, "generate": BOOL, "picture": BOOL},
         ["map", "file"])),
    ("import_elevation", "Real land from public elevation data, from files ALREADY ON DISK: this tool never downloads. "
     "Without files (or with some missing) it lists the tiles the area needs: name, source, URL, size, licence; ASK THE USER before each download, then call again with files. "
     "source copernicus (Copernicus DEM GLO-30 GeoTIFF, free with attribution) or srtm (SRTM .hgt, public domain); or os-terrain-50 (Ordnance Survey's 50 m grid of Great Britain, .asc tiles, Open Government Licence v3: the area as bng [east0, north0, east1, north1] metres or bngKm, files the tiles; tiles left out of the product are sea); seaAt: points known to be sea (BNG metres, or [lat, lon]) to start the sea from instead of the map's edge (land below sea level, the Fens), seaBelow (m, default 0.5); bbox [south, west, north, east] or centre [lat, lon] + sizeKm [ew, ns]; "
     "the area is fitted to the map (keepAspect, default true): squeezed horizontally, heights by verticalScale (a number, map m per real m) or the real slopes kept (default) "
     "times exaggerate, flattened so at most maxSteepShare (0.08) of the land is steeper than maxSlopeDeg (40); floor (lowest land, m), sea (auto: real 0 m is the sea), seaDepth. "
     "The licence and attribution go into map.json's references. Generates, returns the stats and the scales used.",
     obj({"map": MAP, "files": {"type": "array", "items": STR}, "source": {"type": "string", "enum": ["copernicus", "srtm", "os-terrain-50"]}, "bng": {"type": "array", "items": NUM}, "bngKm": {"type": "array", "items": NUM}, "seaAt": {"type": "array"}, "seaBelow": NUM, "seaFromEdge": BOOL,
          "bbox": {"type": "array", "items": NUM}, "centre": {"type": "array", "items": NUM}, "sizeKm": {"type": ["number", "array"]},
          "verticalScale": NUM, "exaggerate": NUM, "maxSlopeDeg": NUM, "maxSteepShare": NUM, "floor": NUM, "sea": BOOL, "seaDepth": NUM,
          "keepAspect": BOOL, "weight": NUM, "generate": BOOL, "picture": BOOL}, ["map"])),
    # --- the ground (step 17.5): MapMaterials.h ---
    ("set_material_rules", "Set or change the rules that choose the ground's material per vertex (map.json \"ground\"), then paint (paint: false to only save). "
     "preset: temperate | nordic | dry (a start: ordered rules and a default material), or none to drop it; default: the material where no rule holds; "
     "rules: an ordered list, the first rule that holds wins, each {material, and conditions as [min, max] (null open): height m, heightRel 0-1 over the playable land, "
     "aboveSea m, slope deg, curvature m (+ a crest, - a hollow, against the 5 x 5 vertices round it), moisture 0-1 (from water, rivers, drainage, hollows; "
     "or layers/moisture.png), drainage 0-1, river (distance to a carved river's middle in half-widths: [null, 0.9] is its bed), seaDistance m, "
     "noise 0-1 (a patch noise of the rule's own, noiseScale m), mask (layers/<name>.png) with maskRange (default [0.5, null]), water true|false; "
     "jitter (a factor, or per condition) and jitterScale m (how the limits wander; borders are organic by default)}. "
     "rulesMode: before (the map's rules first, then the preset's; default) or replace. add: rules put first; remove: rule indices. "
     "materials: {name: {groups: [[GROUP, weight], ...], patchScale m, description}} adds or changes materials (null removes). "
     "moisture: {waterReachM, riverReachM, drainage, noise}; despeckle: passes (default 1). "
     "Materials of the base game: grass, meadow, dry grass, forest floor, sand, beach, hillside, rocky grass, rock, cliff (blocks), sea cliff (blocks), snow, mud, "
     "river bed (a ford), deep river bed (blocks), farmland, road, track, desert rock (blocks), sandstone (blocks); material_report presets: true lists them with the presets' rules.",
     obj({"map": MAP, "preset": STR, "default": STR, "rules": {"type": "array", "items": {"type": "object"}}, "add": {"type": "array", "items": {"type": "object"}},
          "remove": {"type": "array", "items": INT}, "rulesMode": {"type": "string", "enum": ["before", "replace"]}, "materials": {"type": "object"},
          "moisture": {"type": "object"}, "despeckle": INT, "paint": BOOL, "picture": BOOL}, ["map"])),
    ("paint_materials", "Compile the map's tiles from its ground rules: the materials per vertex, then the edge-rule solver (every tile's four sides matched to its "
     "neighbours by the texture database's EDGE rules, transitions found between materials, a group with no transition to its neighbour bridged by the "
     "next one on the way, e.g. grass to rock through the hillside; a random picture, turn and flips from the map's seed). Hand tiles (tiles.hand.json) stay "
     "as they are; the solver works around them. Returns the materials report: each material's share, tiles and passability (blocked and slow tiles), "
     "every unmatched tile with its place and why, the solver's numbers and time; picture (default true): the top-down material map (unmatched tiles red).",
     obj({"map": MAP, "picture": BOOL}, ["map"])),
    # --- the water (step 17.6): WaterDesign.h, MapWater.h ---
    ("set_water", "Set or change the map's water by intent (water.json), then compile and return the water report (compile: false to only save). "
     "Positions in playable metres [x, z] (x east, z north), levels in metres. "
     "lakes: [[x, z, level], ...] replaces the lake points (each floods its basin to its level; the sea is a lake point at the sea level), addLakes appends, removeLakes (indices); "
     "waterways: [{points [[x, z] or [x, z, level]], width m, speed m/s, speeds [m/s per point], by}] replaces the rivers' courses (from source to mouth; a level on every point: "
     "a sloped river whose surface runs down the course, falls where it drops), addWaterways appends, removeWaterways (indices or by-names); "
     "speed: [{waterway (index or by), speed, speeds}] sets the current of existing ones (the generator's \"terrain:<id>\" ones too; a lazy river in the plain, fast in a gorge: speeds per point); "
     "seas: [{name, wind (deg from +x towards +z, the way the waves run), waves (height, 1 default)}] (at most 8, painted A-H); "
     "paint: [{surface: \".\" auto | r river | l lake | A-H sea region, rect [x0, z0, x1, z1] or circle {at, radius} (m), or all: true}] in order; "
     "waterfalls: [{from [x, z], to [x, z]}] placed lips (replaces), autoWaterfalls (the finder's falls too, default true); "
     "farland: [{kind sea|land|mountains|lowland|river, toward (a compass name, north the top of the map, or deg from +x towards +z), spread deg (90), "
     "from (tiles beyond the edge where it is full), weight 0-1, at [x, z] (a river: its exit on the map's edge), level (a sea's, where the map has none)}] "
     "what Extend map's far land does beyond the map (replaces; addFarland appends). Old files read as before.",
     obj({"map": MAP, "lakes": {"type": "array"}, "addLakes": {"type": "array"}, "removeLakes": {"type": "array", "items": INT},
          "waterways": {"type": "array", "items": {"type": "object"}}, "addWaterways": {"type": "array", "items": {"type": "object"}}, "removeWaterways": {"type": "array"},
          "speed": {"type": "array", "items": {"type": "object"}}, "seas": {"type": "array", "items": {"type": "object"}},
          "paint": {"type": "array", "items": {"type": "object"}}, "waterfalls": {"type": "array", "items": {"type": "object"}}, "autoWaterfalls": BOOL,
          "farland": {"type": "array", "items": {"type": "object"}}, "addFarland": {"type": "array", "items": {"type": "object"}}, "compile": BOOL}, ["map"])),
    ("water_report", "The map's water by intent from its last compile (compile: true to compile first): each lake point's flood (tiles, depth, the sea or a lake, leaks: "
     "off the map's edge, past the painted lake, over half the map, a lower lake drowned; a puddle on flat ground), each waterway (wet share, dry stretches, where it rises, "
     "its speed), painted river tiles left dry, the waterfalls the renderer will draw (place, the way it falls, width, drop), a sloped river's steep stretches drawn as rapids (white water, place, drop) and placed falls that draw nothing, the rivers "
     "leaving the map (Extend map carries them on) and the far-land hints (a river hint with no river leaving there); warnings: every water fault with its place.",
     obj({"map": MAP, "compile": BOOL}, ["map"])),
    ("material_report", "The last paint's materials report (as paint_materials returns it) and the material map, without compiling; "
     "moisture: true for the moisture picture too; presets: true for the material data instead (every material with its groups, the presets and their rules).",
     obj({"map": STR, "picture": BOOL, "moisture": BOOL, "presets": BOOL})),
    # --- the objects (step 17.7): MapObjects.h, the scatter rules ---
    ("set_bases", "Set or change the players' bases by intent (map.json players and \"bases\"), then compile and return the objects report (compile: false to only save). "
     "Every building the compiler places goes through the server's own placement rules (the footprint free and on the map, its terrains, the slope limit, "
     "the neighbour rule, the city rules): the civic centre goes on the nearest accepted site within searchM of its start, facing the map's middle; "
     "the base's buildings on the nearest accepted sites at their distance from it, facing it (civic ones join its city as a peasant's stampdown would); "
     "the peasants before its door. players: [{index, name, colour, start [x, z] (playable metres) or \"auto\" (chosen), base (default Manor), peasants, "
     "buildings (this base's list, null: bases.buildings)}] changes those players (index: which; without index appends). "
     "bases (a merge patch): {auto (every player without a start gets one), edgeM (80), waterM (40: from water), maxRiseM (6: within 25 m), room (0.6: the share of "
     "buildable tiles within 30 m), minApartM (warned under), searchM (40), fairnessRadiusM (200), buildings: [{blueprint (a catalog building, e.g. City Houses, "
     "Standard Farm, Barracks Stone), count, distance [min, max] m from the civic centre, toward centre|away|degrees}]}. "
     "removePlayers: indices.",
     obj({"map": MAP, "players": {"type": "array", "items": {"type": "object"}}, "removePlayers": {"type": "array", "items": INT},
          "bases": {"type": "object"}, "compile": BOOL, "picture": BOOL}, ["map"])),
    ("set_scatter", "Set or change the scatter rules (map.json \"scatter\": the Neutral Player's trees, deposits and props, made again at every compile), then compile "
     "and return the objects report (compile: false to only save). A rule: {id, objects: [blueprint | [blueprint, [subtypes]]], and either "
     "(by density) on: [materials or texture groups] (none: any ground), density (per hectare), spacing m, clump 0-1, max, maxSlopeDeg, awayFromBases m, "
     "habitat {cover (0.2: the share of the land forested), soilScaleM (350: the size of the good-soil patches), moisture (0.2: how much wetter ground helps), aspect (0.12: north-facing slopes keep their water), "
     "treelineRel (0.72: the high ground above it bare), waterGapM (12: no trees on waterlogged banks), clearings (0.15)} (THE forest rule: organic patches on good soil, a little likelier near water, "
     "never a line along a river or a ring round a lake; shrunk to keep within maxPerKm2), maxPerKm2 (a tree budget; 700 by default with habitat, about the original 4 Peaks; trees cost frame time), "
     "moisture [from, to] (density by moisture alone; draws contour lines round water, prefer habitat), densityMask (layers/<name>.png, 0-1), "
     "objectsHigh with highFrom/highTo m and highMin/highMax (the share of the second list rising with height) or highMask (a layer); "
     "or (in groups) near: bases with groups (per base), size (objects a group), spread m, distance [min, max] m, fair (default true: every base gets its groups "
     "at the same distances with the same objects, each nearer its own base than any other), and count (groups over the map). "
     "Objects never go on water, steep ground, a building or its neighbours, or a town (its city rectangles and two tiles round). "
     "rules replaces the list; add appends; remove takes ids; patch: {id: merge patch}.",
     obj({"map": MAP, "rules": {"type": "array", "items": {"type": "object"}}, "add": {"type": "array", "items": {"type": "object"}},
          "remove": {"type": "array", "items": STR}, "patch": {"type": "object"}, "compile": BOOL, "picture": BOOL}, ["map"])),
    ("objects_report", "The map's objects from its last compile (compile: true to compile first): the bases (each start, chosen or given, the civic centre's site, "
     "how far it moved, the buildings asked and placed with their places, the peasants), the placement checks (every building against the server's rules as it "
     "stands: passed, failed with status and place, by blueprint; characters blocked, containers on buildings), the fairness (per base within fairnessRadiusM: "
     "wood, gold, stone, food the containers hold, deposits, farmable tiles, the nearest base; per resource the smallest over the largest), the scatter counts "
     "(per rule, per base for the near-bases rules, a habitat forest's cover, patches, budget and its trees by the water against the land's, the moisture under a moisture forest), and the object warnings. picture: the top-down map.",
     obj({"map": MAP, "compile": BOOL, "picture": BOOL}, ["map"])),
]


# --- PNG (16-bit grey) without packages ---
def png16(path, w, h, value):
    raw = bytearray()
    row = struct.pack(">H", value) * w
    for _ in range(h):
        raw += b"\x00" + row
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)
    data = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 16, 0, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(data)


# --- JSON patches ---
def _pointer(path):
    if path == "":
        return []
    if not path.startswith("/"):
        raise AuthorError({"error": "a JSON pointer starts with /: " + path})
    return [p.replace("~1", "/").replace("~0", "~") for p in path[1:].split("/")]


def _parent(doc, parts):
    cur = doc
    for p in parts[:-1]:
        if isinstance(cur, list):
            cur = cur[int(p)]
        elif isinstance(cur, dict) and p in cur:
            cur = cur[p]
        else:
            raise AuthorError({"error": "no such path: /" + "/".join(parts)})
    return cur


def _get(doc, parts):
    cur = doc
    for p in parts:
        if isinstance(cur, list):
            cur = cur[int(p)]
        elif isinstance(cur, dict) and p in cur:
            cur = cur[p]
        else:
            raise AuthorError({"error": "no such path: /" + "/".join(parts)})
    return cur


def _add(doc, parts, value):
    if not parts:
        return value
    par, key = _parent(doc, parts), parts[-1]
    if isinstance(par, list):
        if key == "-":
            par.append(value)
        else:
            par.insert(int(key), value)
    else:
        par[key] = value
    return doc


def _remove(doc, parts):
    par, key = _parent(doc, parts), parts[-1]
    if isinstance(par, list):
        return par.pop(int(key))
    if key not in par:
        raise AuthorError({"error": "no such path: /" + "/".join(parts)})
    return par.pop(key)


def json_patch(doc, ops):
    doc = copy.deepcopy(doc)
    for op in ops:
        kind, parts = op.get("op"), _pointer(op.get("path", ""))
        if kind == "add":
            doc = _add(doc, parts, copy.deepcopy(op["value"]))
        elif kind == "remove":
            _remove(doc, parts)
        elif kind == "replace":
            if not parts:
                doc = copy.deepcopy(op["value"])
            else:
                _get(doc, parts)
                par = _parent(doc, parts)
                par[int(parts[-1]) if isinstance(par, list) else parts[-1]] = copy.deepcopy(op["value"])
        elif kind in ("move", "copy"):
            src = _pointer(op["from"])
            value = _remove(doc, src) if kind == "move" else copy.deepcopy(_get(doc, src))
            doc = _add(doc, parts, value)
        elif kind == "test":
            if _get(doc, parts) != op.get("value"):
                raise AuthorError({"error": "test failed at " + op.get("path", "")})
        else:
            raise AuthorError({"error": "unknown patch op " + str(kind)})
    return doc


def merge_patch(doc, patch):
    if not isinstance(patch, dict):
        return copy.deepcopy(patch)
    doc = copy.deepcopy(doc) if isinstance(doc, dict) else {}
    for k, v in patch.items():
        if v is None:
            doc.pop(k, None)
        else:
            doc[k] = merge_patch(doc.get(k), v)
    return doc


class Author:
    def __init__(self, game_path, exe, work, user_path=None, timeout=900):
        if not game_path:
            raise ValueError("--game-path (or WKB_GAME_PATH): the Warrior Kings Battles folder")
        self.game_path = game_path
        self.exe = exe or DEFAULT_EXE
        self.work = work or os.path.join(os.environ.get("TEMP", "."), "wkbai-author-run")
        self.user_path = user_path or os.path.join(self.work, "profile")
        self.timeout = timeout
        self.catalogs = {}
        os.makedirs(self.work, exist_ok=True)
        redata = os.path.join(REPO, "redata")
        if os.path.isdir(redata):
            shutil.copytree(redata, os.path.join(self.work, "redata"), dirs_exist_ok=True)
        cfg = {"test": True, "gamePath": self.game_path, "gameVersion": 2, "userPath": self.user_path, "introVideoFile": "",
               "musicEnabled": False, "soundEnabled": False}
        with open(os.path.join(self.work, "wkconfig.json"), "w") as f:
            json.dump(cfg, f, indent=1)

    @property
    def maps(self):
        return os.path.join(self.user_path, "saved", "Maps")

    def run(self, *args):
        """wkbai --author <args>: (exit code, output)"""
        p = subprocess.run([self.exe, "--author"] + [str(a) for a in args], cwd=self.work, input="", capture_output=True,
                           text=True, errors="replace", timeout=self.timeout)
        out = p.stdout + p.stderr
        if p.returncode != 0:
            lines = [l for l in out.splitlines() if l.startswith("AUTHOR") or "Error" in l or "WARNING" in l]
            raise AuthorError({"error": "wkbai --author %s failed (exit %d)" % (args[0], p.returncode), "output": lines[-30:] or out.splitlines()[-30:]})
        return out

    def folder(self, m):
        if not m:
            raise AuthorError({"error": "no map given"})
        if os.path.isabs(m):
            return m
        name = m if m.lower().endswith(".wkbai") else m + ".wkbai"
        return os.path.join(self.maps, name)

    def need_folder(self, m):
        f = self.folder(m)
        if not os.path.isfile(os.path.join(f, "map.json")):
            raise AuthorError({"error": "no map folder " + f})
        return f

    # --- knowledge ---
    def catalog(self, a):
        mod = a.get("mod") or ""
        edges = bool(a.get("edges"))
        key = (mod.lower(), edges)
        path = os.path.join(self.work, "catalog", "catalog%s%s.json" % ("-" + re.sub(r"[^\w.-]", "_", mod) if mod else "", "-edges" if edges else ""))
        if a.get("refresh") or key not in self.catalogs:
            if a.get("refresh") or not os.path.isfile(path):
                args = ["catalog", "--out", path]
                if mod:
                    args += ["--mod", mod]
                if edges:
                    args.append("--edges")
                self.run(*args)
            with open(path, encoding="utf-8") as f:
                self.catalogs[key] = json.load(f)
        c = self.catalogs[key]
        section = a.get("section", "summary")
        name = (a.get("name") or "").lower()
        if section == "all":
            return {"path": path, "counts": c["counts"]}
        if section == "summary":
            return {"gameSet": c["gameSet"], "mods": c["mods"], "counts": c["counts"],
                    "classes": {k: {"placeable": v["placeable"], "structural": v.get("structural", False), "count": len(v["blueprints"])} for k, v in c["classes"].items()},
                    "items": c["items"], "resources": c["resources"],
                    "terrains": [{"name": t["name"], "cost": t["cost"]} for t in c["terrains"]], "passabilityFields": c["passabilityFields"],
                    "textureGroups": [g["name"] for db in c["textureDatabases"] for g in db["groups"]],
                    "textureDatabases": [db["file"] for db in c["textureDatabases"]],
                    "skies": [s["name"] for s in c["skies"]], "trees": [t["name"] for t in c["trees"]], "mapSize": c["mapSize"], "environment": c["environment"],
                    "path": path}
        if section == "blueprints":
            cls = (a.get("class") or "").lower()
            if cls not in c["classes"]:
                raise AuthorError({"error": "class: one of " + ", ".join(c["classes"])})
            bps = [b for b in c["classes"][cls]["blueprints"] if name in b["name"].lower()]
            return {"class": cls, "count": len(bps), "blueprints": bps}
        if section == "blueprint":
            found = [dict(b, **{"class": k}) for k, v in c["classes"].items() for b in v["blueprints"] if b["name"].lower() == name]
            if not found:
                raise AuthorError({"error": "no blueprint named " + a.get("name", "")})
            return {"blueprints": found}
        if section in ("textures", "texture"):
            groups = []
            for db in c["textureDatabases"]:
                for g in db["groups"]:
                    if (g["name"].lower() == name) if section == "texture" else (name in g["name"].lower()):
                        g = dict(g, database=db["file"], textureCount=len(g.get("textures", [])))
                        if section == "textures":
                            g = {k: v for k, v in g.items() if k not in ("textures", "edgesBySide")}
                        groups.append(g)
            if section == "texture" and not groups:
                raise AuthorError({"error": "no texture group named " + a.get("name", "")})
            return {"count": len(groups), "groups": groups, "edgeRuleFormat": c["edgeRuleFormat"]}
        if section == "terrains":
            return {"terrains": c["terrains"], "passabilityFields": c["passabilityFields"], "terrainBlueprintLists": c["terrainBlueprintLists"]}
        if section in ("environment", "skies", "trees"):
            return {section: c[section]}
        raise AuthorError({"error": "section: one of " + ", ".join(SECTIONS)})

    def describe_map(self, a):
        m = a["map"]
        target = m
        if m.lower().endswith(".wkbai") or os.path.isdir(self.folder(m)):
            target = self.need_folder(m)
        stem = re.sub(r"[^\w.-]", "_", os.path.basename(m.rstrip("\\/")))
        out = os.path.join(self.work, "describe", stem + ".json")
        png = os.path.join(self.work, "describe", stem + ".png")
        args = ["describe", target, "--out", out]
        if a.get("picture", True):
            args += ["--png", png]
        if a.get("mod"):
            args += ["--mod", a["mod"]]
        self.run(*args)
        with open(out, encoding="utf-8") as f:
            d = json.load(f)
        if a.get("picture", True) and os.path.isfile(png):
            return {"description": d, "image": base64.b64encode(open(png, "rb").read()).decode(), "path": png}
        return d

    # --- map folders ---
    def new_map(self, a):
        name = a["name"]
        folder = self.folder(name)
        if os.path.exists(folder):
            if not a.get("replace"):
                raise AuthorError({"error": "there is a map folder " + folder + " (replace: true to overwrite it)"})
            shutil.rmtree(folder)
        pw, ph = (list(a["playable"]) + [a["playable"][0]])[:2]
        edge = int(a.get("edge", 30))
        if not (16 <= pw <= 1024 and 16 <= ph <= 1024):
            raise AuthorError({"error": "playable: 16-1024 tiles a side"})
        w, h = pw + 2 * edge, ph + 2 * edge
        os.makedirs(folder)
        steps = float(a.get("height", 20))
        png16(os.path.join(folder, "heights.png"), w + 1, h + 1, max(0, min(65535, int(round(steps * 256)))))
        sky = a.get("sky") or "Bright Cloudy Blue Sky"
        if "\\" not in sky:
            sky = "Maps\\Sky Textures\\" + sky + "\\"
        mj = {"format": "wkbai map", "version": 1, "name": os.path.basename(folder)[:-6],
              "size": {"tiles": [w, h], "edge": edge}, "heights": {"scale": float(a.get("scale", 1.0))},
              "textures": {"database": "Maps\\Map_Textures\\All_Textures.dat", "translation": ""},
              "sky": sky, "sun": {"colour": [255, 255, 255], "direction": [1.0, 1.0, 1.0]}, "fog": [159, 197, 230],
              "seed": int(a.get("seed", 1)), "ground": {"fill": a.get("fill") or "GRASS1"}, "players": a.get("players") or []}
        with open(os.path.join(folder, "map.json"), "w", encoding="utf-8") as f:
            json.dump(mj, f, indent=1)
        return {"folder": folder, "gamePath": "Maps\\" + os.path.basename(folder), "map": mj}

    def import_map(self, a):
        level = a["level"]
        name = a.get("name") or os.path.splitext(os.path.basename(level.replace("\\", "/")))[0]
        folder = self.folder(name)
        if os.path.exists(folder):
            if not a.get("replace"):
                raise AuthorError({"error": "there is a map folder " + folder + " (replace: true to overwrite it)"})
            shutil.rmtree(folder)
        out = self.run("import", level, folder)
        return {"folder": folder, "gamePath": "Maps\\" + os.path.basename(folder), "output": [l for l in out.splitlines() if l.startswith("AUTHOR")]}

    def get_map(self, a):
        folder = self.need_folder(a["map"])
        with open(os.path.join(folder, "map.json"), encoding="utf-8") as f:
            mj = json.load(f)
        files = []
        for root, _, names in os.walk(folder):
            for n in names:
                p = os.path.join(root, n)
                files.append({"file": os.path.relpath(p, folder).replace("\\", "/"), "bytes": os.path.getsize(p)})
        r = {"folder": folder, "map": mj, "files": sorted(files, key=lambda x: x["file"])}
        rep = os.path.join(folder, "build", "report.json")
        if os.path.isfile(rep):
            with open(rep, encoding="utf-8") as f:
                r["report"] = json.load(f)
        return r

    def set_map(self, a):
        folder = self.need_folder(a["map"])
        path = os.path.join(folder, "map.json")
        with open(path, encoding="utf-8") as f:
            mj = json.load(f)
        patch = a["patch"]
        new = json_patch(mj, patch) if isinstance(patch, list) else merge_patch(mj, patch)
        if not isinstance(new, dict) or new.get("format") != "wkbai map":
            raise AuthorError({"error": "the result isn't a wkbai map (\"format\": \"wkbai map\")"})
        with open(path, "w", encoding="utf-8") as f:
            json.dump(new, f, indent=1)
        return {"folder": folder, "map": new}

    def compile(self, a):
        folder = self.need_folder(a["map"])
        out = self.run(folder)
        rep = os.path.join(folder, "build", "report.json")
        r = {"folder": folder, "gamePath": "Maps\\" + os.path.basename(folder), "output": [l for l in out.splitlines() if l.startswith("AUTHOR")]}
        if os.path.isfile(rep):
            with open(rep, encoding="utf-8") as f:
                r["report"] = json.load(f)
        return r

    # --- the land ---
    def _terrain_run(self, cmd, folder, options=None, extra=()):
        """wkbai --author <cmd> <folder> [--options <file>] --out <file>: its JSON (an error's JSON in the AuthorError)"""
        tdir = os.path.join(self.work, "terrain")
        os.makedirs(tdir, exist_ok=True)
        stem = re.sub(r"[^\w.-]", "_", os.path.basename(folder.rstrip("\\/")))
        out = os.path.join(tdir, stem + "." + cmd + ".json")
        if os.path.exists(out):
            os.remove(out)
        args = [cmd, folder, "--out", out] + list(extra)
        if options is not None:
            opt = os.path.join(tdir, stem + "." + cmd + ".options.json")
            with open(opt, "w", encoding="utf-8") as f:
                json.dump(options, f, indent=1)
            args += ["--options", opt]
        try:
            self.run(*args)
        except AuthorError as e:
            if os.path.isfile(out):
                with open(out, encoding="utf-8") as f:
                    e.reply["result"] = json.load(f)
            raise
        with open(out, encoding="utf-8") as f:
            return json.load(f)

    def _with_picture(self, a, result, folder):
        if not a.get("picture", True):
            return result
        try:
            self.run(folder)  # compiled first, so describe pictures the new land
            pic = self.describe_map({"map": folder, "picture": True})
        except AuthorError as e:
            result["picture"] = e.reply
            return result
        if isinstance(pic, dict) and "image" in pic:
            return {"description": result, "image": pic["image"], "path": pic["path"]}
        return result

    def plan_terrain(self, a):
        folder = self.need_folder(a["map"])
        path = os.path.join(folder, "map.json")
        with open(path, encoding="utf-8") as f:
            mj = json.load(f)
        if "terrain" in a:
            gen = (mj.get("terrain") or {}).get("generated")
            mj["terrain"] = copy.deepcopy(a["terrain"])
            if gen and "generated" not in mj["terrain"]:
                mj["terrain"]["generated"] = gen  # the generator's own lakes are replaced next time
        if "patch" in a:
            mj["terrain"] = merge_patch(mj.get("terrain") or {}, a["patch"])
        feats = list(mj.get("features") or [])
        if "features" in a:
            feats = list(a["features"])
        if a.get("remove"):
            rm = a["remove"]
            feats = [f for i, f in enumerate(feats) if i not in rm and f.get("id") not in rm]
        feats += list(a.get("add") or [])
        kinds = {"hill", "range", "ridge", "plateau", "valley", "basin", "coast", "island", "river", "pass", "terraces", "flat"}
        bad = [i for i, f in enumerate(feats) if not isinstance(f, dict) or f.get("kind") not in kinds]
        if bad:
            raise AuthorError({"error": "features %s: kind must be one of %s" % (bad, ", ".join(sorted(kinds)))})
        mj["features"] = feats
        with open(path, "w", encoding="utf-8") as f:
            json.dump(mj, f, indent=1)
        r = {"folder": folder, "terrain": mj.get("terrain"), "features": feats}
        if a.get("generate", True):
            r["stats"] = self._terrain_run("terrain", folder)
            return self._with_picture(a, r, folder)
        return r

    def generate_terrain(self, a):
        folder = self.need_folder(a["map"])
        extra = ["--seed", str(int(a["seed"]))] if a.get("seed") else []
        return self._with_picture(a, self._terrain_run("terrain", folder, extra=extra), folder)

    def import_heightmap(self, a):
        folder = self.need_folder(a["map"])
        opts = {k: v for k, v in a.items() if k not in ("map", "picture")}
        r = self._terrain_run("heightmap", folder, opts)
        return self._with_picture(a, r, folder) if a.get("generate", True) else r

    def import_elevation(self, a):
        folder = self.need_folder(a["map"])
        opts = {k: v for k, v in a.items() if k not in ("map", "picture")}
        try:
            r = self._terrain_run("elevation", folder, opts)
        except AuthorError as e:
            res = e.reply.get("result") or {}
            if "needed" in res:
                # nothing downloaded: the tiles named, for the user to agree to
                return {"downloaded": False, "askTheUser": "Ask the user before each download: name the source, the tile, its size and licence.",
                        "needed": res["needed"], "missing": res.get("missing", [])}
            raise
        return self._with_picture(a, r, folder) if a.get("generate", True) else r

    # --- the ground ---
    def _material_result(self, folder, picture=True, moisture=False):
        rep = os.path.join(folder, "build", "report.json")
        if not os.path.isfile(rep):
            raise AuthorError({"error": "no report: paint (or compile) the map first"})
        with open(rep, encoding="utf-8") as f:
            report = json.load(f)
        if "materials" not in report:
            return {"folder": folder, "materials": None, "note": "map.json has no ground rules or preset: set_material_rules first",
                    "warnings": report.get("warnings", [])}
        r = {"folder": folder, "report": report["materials"], "warnings": report.get("warnings", [])}
        out = {"description": r}
        images = []
        for want, name in ((picture, "materials.png"), (moisture, "moisture.png")):
            png = os.path.join(folder, "build", name)
            if want and os.path.isfile(png):
                images.append({"image": base64.b64encode(open(png, "rb").read()).decode(), "path": png})
        if not images:
            return r
        out["image"] = images[0]["image"]
        out["path"] = images[0]["path"]
        if len(images) > 1:
            out["images"] = images
        return out

    def set_material_rules(self, a):
        folder = self.need_folder(a["map"])
        path = os.path.join(folder, "map.json")
        with open(path, encoding="utf-8") as f:
            mj = json.load(f)
        g = dict(mj.get("ground") or {})
        if "preset" in a:
            if a["preset"] in (None, "", "none"):
                g.pop("preset", None)
            else:
                g["preset"] = a["preset"]
        for k in ("default", "rulesMode", "moisture", "despeckle"):
            if k in a:
                g[k] = a[k]
        rules = list(g.get("rules") or [])
        if "rules" in a:
            rules = list(a["rules"])
        if a.get("remove"):
            rules = [r for i, r in enumerate(rules) if i not in a["remove"]]
        rules = list(a.get("add") or []) + rules
        bad = [i for i, r in enumerate(rules) if not isinstance(r, dict) or not isinstance(r.get("material"), str)]
        if bad:
            raise AuthorError({"error": "rules %s: each needs a material (a name)" % bad})
        if rules or "rules" in a or a.get("remove"):
            g["rules"] = rules
        if "materials" in a:
            g["materials"] = merge_patch(g.get("materials") or {}, a["materials"])
        mj["ground"] = g
        with open(path, "w", encoding="utf-8") as f:
            json.dump(mj, f, indent=1)
        r = {"folder": folder, "ground": g}
        if a.get("paint", True):
            return self.paint_materials({"map": folder, "picture": a.get("picture", True)})
        return r

    def paint_materials(self, a):
        folder = self.need_folder(a["map"])
        self.run("materials", folder, "--out", os.path.join(self.work, "materials.json"))
        return self._material_result(folder, a.get("picture", True))

    def material_report(self, a):
        if a.get("presets"):
            out = os.path.join(self.work, "material-presets.json")
            self.run("material-presets", "--out", out)
            with open(out, encoding="utf-8") as f:
                return json.load(f)
        if not a.get("map"):
            raise AuthorError({"error": "map (or presets: true)"})
        return self._material_result(self.need_folder(a["map"]), a.get("picture", True), a.get("moisture", False))

    # --- the water ---
    def _water_file(self, folder):
        with open(os.path.join(folder, "map.json"), encoding="utf-8") as f:
            mj = json.load(f)
        W, H = mj["size"]["tiles"]
        E = mj["size"].get("edge", 30)
        path = os.path.join(folder, "water.json")
        wj = {}
        if os.path.isfile(path):
            with open(path, encoding="utf-8") as f:
                wj = json.load(f)
        wj.setdefault("format", "wkbai water")
        wj.setdefault("version", 1)
        wj["tiles"] = [W, H]
        return path, wj, W, H, E

    def set_water(self, a):
        folder = self.need_folder(a["map"])
        path, wj, W, H, E = self._water_file(folder)

        def lake(l):
            if not isinstance(l, (list, tuple)) or len(l) < 3:
                raise AuthorError({"error": "a lake is [x, z, level] (playable metres): %r" % (l,)})
            return [l[0] + E * 5.0, l[2], l[1] + E * 5.0, 0]
        lakes = list(wj.get("lakes") or [])
        if "lakes" in a:
            lakes = [lake(l) for l in a["lakes"]]
        if a.get("removeLakes"):
            lakes = [l for i, l in enumerate(lakes) if i not in a["removeLakes"]]
        lakes += [lake(l) for l in a.get("addLakes") or []]
        wj["lakes"] = lakes

        def way(w):
            if not isinstance(w, dict) or not isinstance(w.get("points"), list) or len(w["points"]) < 2:
                raise AuthorError({"error": "a waterway needs points: [[x, z] or [x, z, level], ...] (at least 2)"})
            if "speeds" in w and len(w["speeds"]) != len(w["points"]):
                raise AuthorError({"error": "a waterway's speeds: one per point (%d points, %d speeds)" % (len(w["points"]), len(w["speeds"]))})
            return {k: v for k, v in w.items() if k in ("points", "width", "speed", "speeds", "by")}
        ways = list(wj.get("waterways") or [])
        if "waterways" in a:
            ways = [way(w) for w in a["waterways"]]
        if a.get("removeWaterways"):
            rm = a["removeWaterways"]
            ways = [w for i, w in enumerate(ways) if i not in rm and w.get("by") not in rm]
        ways += [way(w) for w in a.get("addWaterways") or []]
        for sp in a.get("speed") or []:
            key = sp.get("waterway")
            hit = [w for i, w in enumerate(ways) if i == key or (isinstance(key, str) and w.get("by") == key)]
            if not hit:
                raise AuthorError({"error": "speed: no waterway %r (an index or a by-name such as terrain:<id>)" % (key,)})
            for w in hit:
                if "speed" in sp:
                    if sp["speed"]:
                        w["speed"] = sp["speed"]
                    else:
                        w.pop("speed", None)
                if "speeds" in sp:
                    if sp["speeds"] and len(sp["speeds"]) != len(w["points"]):
                        raise AuthorError({"error": "speeds: one per point (%d points)" % len(w["points"])})
                    if sp["speeds"]:
                        w["speeds"] = sp["speeds"]
                    else:
                        w.pop("speeds", None)
        wj["waterways"] = ways
        if "seas" in a:
            if len(a["seas"]) > 8:
                raise AuthorError({"error": "at most 8 sea regions (A-H)"})
            wj["seas"] = a["seas"]
        if a.get("paint"):
            rows = [list((r + "." * W)[:W]) for r in (wj.get("surface") or [])][:H]
            rows += [["."] * W for _ in range(H - len(rows))]
            for p in a["paint"]:
                c = p.get("surface", ".")
                if c not in (".", "r", "l") and not ("A" <= c <= "H"):
                    raise AuthorError({"error": "paint surface: . r l or A-H, not %r" % c})
                for z in range(H):
                    for x in range(W):
                        cx, cz = (x - E + 0.5) * 5.0, (z - E + 0.5) * 5.0
                        if p.get("all"):
                            hit = True
                        elif "rect" in p:
                            x0, z0, x1, z1 = p["rect"]
                            hit = min(x0, x1) <= cx <= max(x0, x1) and min(z0, z1) <= cz <= max(z0, z1)
                        elif "circle" in p:
                            q = p["circle"]
                            hit = (cx - q["at"][0]) ** 2 + (cz - q["at"][1]) ** 2 <= q["radius"] ** 2
                        else:
                            raise AuthorError({"error": "paint: rect, circle or all"})
                        if hit:
                            rows[z][x] = c
            wj["surface"] = ["".join(r) for r in rows]
        if "waterfalls" in a:
            wj["waterfalls"] = a["waterfalls"]
        if "autoWaterfalls" in a:
            wj["autoWaterfalls"] = bool(a["autoWaterfalls"])
        kinds = ("sea", "land", "mountains", "lowland", "river")
        hints = list(wj.get("farland") or [])
        if "farland" in a:
            hints = list(a["farland"])
        hints += list(a.get("addFarland") or [])
        for h in hints:
            if not isinstance(h, dict) or h.get("kind") not in kinds or "toward" not in h:
                raise AuthorError({"error": "a far-land hint needs kind (%s) and toward: %r" % ("|".join(kinds), h)})
        if hints:
            wj["farland"] = hints
        else:
            wj.pop("farland", None)
        wj["version"] = 2 if hints or any("speeds" in w for w in ways) else 1
        with open(path, "w", encoding="utf-8") as f:
            json.dump(wj, f, indent=1)
        if a.get("compile", True):
            return self.water_report({"map": folder, "compile": True})
        return {"folder": folder, "water": wj}

    def water_report(self, a):
        folder = self.need_folder(a["map"])
        r = {"folder": folder}
        if a.get("compile"):
            r["output"] = [l for l in self.run(folder).splitlines() if l.startswith("AUTHOR")]
        rep = os.path.join(folder, "build", "report.json")
        if not os.path.isfile(rep):
            raise AuthorError({"error": "not compiled yet (compile: true)"})
        with open(rep, encoding="utf-8") as f:
            report = json.load(f)
        r["water"] = report.get("water")
        r["lakes"] = report.get("lakes")
        r["tilesUnderWater"] = report.get("tilesUnderWater")
        r["warnings"] = [w for w in report.get("warnings", []) if w.startswith("water") or w.startswith("lake ")]
        return r

    # --- the objects ---
    SCATTER_KEYS = {"id", "note", "role", "objects", "objectsHigh", "highFrom", "highTo", "highMin", "highMax", "highMask", "on", "density", "spacing",
                    "clump", "max", "maxPerKm2", "maxSlopeDeg", "awayFromBases", "moisture", "habitat", "densityMask", "near", "groups", "size", "spread", "distance", "fair", "count"}
    PLAYER_KEYS = {"name", "colour", "start", "base", "peasants", "buildings"}
    BASES_KEYS = {"auto", "edgeM", "waterM", "maxRiseM", "room", "minApartM", "searchM", "fairnessRadiusM", "buildings"}

    def _map_json(self, folder):
        with open(os.path.join(folder, "map.json"), encoding="utf-8") as f:
            return json.load(f)

    def _save_map_json(self, folder, mj):
        with open(os.path.join(folder, "map.json"), "w", encoding="utf-8") as f:
            json.dump(mj, f, indent=1)

    def set_bases(self, a):
        folder = self.need_folder(a["map"])
        mj = self._map_json(folder)
        players = list(mj.get("players") or [])
        for p in a.get("players") or []:
            if not isinstance(p, dict):
                raise AuthorError({"error": "a player is an object: %r" % (p,)})
            bad = [k for k in p if k not in self.PLAYER_KEYS and k != "index"]
            if bad:
                raise AuthorError({"error": "unknown player keys %s (known: index, %s)" % (bad, ", ".join(sorted(self.PLAYER_KEYS)))})
            st = p.get("start")
            if st is not None and st != "auto" and not (isinstance(st, list) and len(st) == 2):
                raise AuthorError({"error": "start: [x, z] in playable metres or \"auto\", not %r" % (st,)})
            body = {k: v for k, v in p.items() if k != "index"}
            if "index" in p:
                i = p["index"]
                if not isinstance(i, int) or i < 0 or i >= len(players):
                    raise AuthorError({"error": "no player %r (%d players)" % (i, len(players))})
                players[i] = merge_patch(players[i], body)
            else:
                players.append(body)
        rm = set(a.get("removePlayers") or [])
        players = [p for i, p in enumerate(players) if i not in rm]
        mj["players"] = players
        if "bases" in a:
            bad = [k for k in a["bases"] if k not in self.BASES_KEYS]
            if bad:
                raise AuthorError({"error": "unknown bases keys %s (known: %s)" % (bad, ", ".join(sorted(self.BASES_KEYS)))})
            mj["bases"] = merge_patch(mj.get("bases") or {}, a["bases"])
        for b in (mj.get("bases") or {}).get("buildings") or []:
            if not (isinstance(b, str) or (isinstance(b, dict) and b.get("blueprint"))):
                raise AuthorError({"error": "a base building is a blueprint name or {blueprint, count, distance, toward}: %r" % (b,)})
        self._save_map_json(folder, mj)
        if a.get("compile", True):
            return self.objects_report({"map": folder, "compile": True, "picture": a.get("picture", False)})
        return {"folder": folder, "players": players, "bases": mj.get("bases")}

    def set_scatter(self, a):
        folder = self.need_folder(a["map"])
        mj = self._map_json(folder)
        rules = list(mj.get("scatter") or [])
        if "rules" in a:
            rules = list(a["rules"])
        if a.get("remove"):
            rules = [r for r in rules if r.get("id") not in a["remove"]]
        rules += list(a.get("add") or [])
        for rid, patch in (a.get("patch") or {}).items():
            hit = [i for i, r in enumerate(rules) if r.get("id") == rid]
            if not hit:
                raise AuthorError({"error": "patch: no scatter rule %r" % rid})
            for i in hit:
                rules[i] = merge_patch(rules[i], patch)
        ids = set()
        for r in rules:
            if not isinstance(r, dict) or not r.get("objects"):
                raise AuthorError({"error": "a scatter rule needs objects: %r" % (r,)})
            bad = [k for k in r if k not in self.SCATTER_KEYS]
            if bad:
                raise AuthorError({"error": "rule %r: unknown keys %s (known: %s)" % (r.get("id"), bad, ", ".join(sorted(self.SCATTER_KEYS)))})
            if "moisture" in r and not (isinstance(r["moisture"], list) and len(r["moisture"]) == 2):
                raise AuthorError({"error": "rule %r: moisture is [from, to]" % r.get("id")})
            if r.get("id") in ids:
                raise AuthorError({"error": "two scatter rules with the id %r" % r.get("id")})
            ids.add(r.get("id"))
        mj["scatter"] = rules
        self._save_map_json(folder, mj)
        if a.get("compile", True):
            return self.objects_report({"map": folder, "compile": True, "picture": a.get("picture", False)})
        return {"folder": folder, "scatter": rules}

    def objects_report(self, a):
        folder = self.need_folder(a["map"])
        r = {"folder": folder}
        if a.get("compile"):
            r["output"] = [l for l in self.run(folder).splitlines() if l.startswith("AUTHOR")]
        rep = os.path.join(folder, "build", "report.json")
        if not os.path.isfile(rep):
            raise AuthorError({"error": "not compiled yet (compile: true)"})
        with open(rep, encoding="utf-8") as f:
            report = json.load(f)
        for k in ("bases", "placement", "fairness", "scatter", "scatterNearBases", "scatterMoisture", "scatterHabitat", "objectsByRules"):
            if k in report:
                r[k] = report[k]
        pre = ("base:", "bases:", "scatter", "placement:", "fairness:", "objects:")
        r["warnings"] = [w for w in report.get("warnings", []) if w.startswith(pre)]
        if a.get("picture"):
            try:
                pic = self.describe_map({"map": folder, "picture": True})
                if isinstance(pic, dict) and "image" in pic:
                    return {"description": r, "image": pic["image"], "path": pic["path"]}
            except AuthorError as e:
                r["picture"] = e.reply
        return r

    def call(self, name, a):
        f = {"catalog": self.catalog, "describe_map": self.describe_map, "new_map": self.new_map, "import_map": self.import_map,
             "get_map": self.get_map, "set_map": self.set_map, "compile": self.compile,
             "plan_terrain": self.plan_terrain, "generate_terrain": self.generate_terrain,
             "import_heightmap": self.import_heightmap, "import_elevation": self.import_elevation,
             "set_material_rules": self.set_material_rules, "paint_materials": self.paint_materials, "material_report": self.material_report,
             "set_water": self.set_water, "water_report": self.water_report,
             "set_bases": self.set_bases, "set_scatter": self.set_scatter, "objects_report": self.objects_report}.get(name)
        if not f:
            raise AuthorError({"error": "unknown tool " + str(name)})
        return f(a)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--game-path", default=os.environ.get("WKB_GAME_PATH"))
    ap.add_argument("--exe")
    ap.add_argument("--work")
    ap.add_argument("--user-path", help="the wkbai user folder the maps go into (its saved\\Maps); default <work>\\profile")
    args = ap.parse_args()
    author = Author(args.game_path, args.exe, args.work, args.user_path)
    out = sys.stdout
    def send(msg):
        out.write(json.dumps(msg) + "\n")
        out.flush()
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        try:
            msg = json.loads(line)
        except ValueError:
            continue
        mid, method = msg.get("id"), msg.get("method")
        if method == "initialize":
            send({"jsonrpc": "2.0", "id": mid, "result": {"protocolVersion": PROTOCOL, "capabilities": {"tools": {}},
                  "serverInfo": {"name": "wkbai-author", "version": "1"}}})
        elif method == "tools/list":
            send({"jsonrpc": "2.0", "id": mid, "result": {"tools": [{"name": n, "description": d, "inputSchema": s} for n, d, s in TOOLS]}})
        elif method == "tools/call":
            p = msg.get("params", {})
            try:
                r = author.call(p.get("name"), p.get("arguments") or {})
                if isinstance(r, dict) and "image" in r:
                    content = [{"type": "text", "text": json.dumps(r["description"])}]
                    for im in r.get("images") or [{"image": r["image"], "path": r["path"]}]:
                        content += [{"type": "image", "data": im["image"], "mimeType": "image/png"}, {"type": "text", "text": im["path"]}]
                else:
                    content = [{"type": "text", "text": json.dumps(r)}]
                send({"jsonrpc": "2.0", "id": mid, "result": {"content": content}})
            except AuthorError as e:
                send({"jsonrpc": "2.0", "id": mid, "result": {"content": [{"type": "text", "text": json.dumps(e.reply)}], "isError": True}})
            except Exception as e:  # the tool failed, the server goes on
                send({"jsonrpc": "2.0", "id": mid, "result": {"content": [{"type": "text", "text": "".join(traceback.format_exception_only(type(e), e))}], "isError": True}})
        elif method == "ping":
            send({"jsonrpc": "2.0", "id": mid, "result": {}})
        elif mid is not None and method:
            send({"jsonrpc": "2.0", "id": mid, "error": {"code": -32601, "message": "unknown method " + method}})


if __name__ == "__main__":
    main()
