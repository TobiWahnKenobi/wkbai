"""wkbai map authoring as an MCP server (stdio), so that an AI assistant such as Claude Code can
design maps for wkbai: know exactly what can go on a map (catalog), read existing maps back
(describe_map, get_map), and make and change map folders (new_map, import_map, set_map, compile).
Step 17.3 of the RE repo's docs/engine/map-authoring.md; the land, ground, water, object, check
and preview tools follow in later steps.

Everything runs wkbai's headless commands (no window, nothing on the screen):
    wkbai --author catalog [--mod <name>|none] [--edges] --out <file>
    wkbai --author describe <map> --png <file> --out <file>
    wkbai --author import <level> <folder>
    wkbai --author <folder>                    (compile)
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
     "material shares (texture groups, terrains), object counts by class, blueprint and player. map: a map folder (name or path) or an original level "
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

    def call(self, name, a):
        f = {"catalog": self.catalog, "describe_map": self.describe_map, "new_map": self.new_map, "import_map": self.import_map,
             "get_map": self.get_map, "set_map": self.set_map, "compile": self.compile}.get(name)
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
                    content = [{"type": "text", "text": json.dumps(r["description"])}, {"type": "image", "data": r["image"], "mimeType": "image/png"},
                               {"type": "text", "text": r["path"]}]
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
