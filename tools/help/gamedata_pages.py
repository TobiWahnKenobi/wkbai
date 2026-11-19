"""Help pages for every unit, building, upgrade and command of the game set.

    python -I tools/help/gamedata_pages.py [--gameset DIR] [--force] [--only units,buildings,upgrades,commands]
                                           [--list] [--dump FILE]

Reads the base game set (default C:\\Dev\\gsfull\\Warrior Kings Game Set), starting at
"multi-player extensions.cpp" and following its LINK_GAME_SET lines as wkbai does, and writes
redata/help/en/{units,buildings,upgrades,commands}/<name>.md plus each folder's index.md.

Each page has front matter, a ```facts block (the live table in the game; units and buildings
only), a static copy of the key numbers for the HTML export, and the sections Role, Trained at /
Built by, Needs, Leads to, Commands and Tips.

What the script owns, and what the author owns:
- The script writes only between <!-- gen:<part> --> and <!-- /gen:<part> --> markers (the
  facts block with its <!-- facts:static --> numbers, and the sections filled from the data).
- Everything else (the title line, the Role and Tips prose, extra sections or notes) belongs to
  the author and is kept when the script runs again. A new page gets placeholders for the prose.
- The front matter is written for new pages only; on an existing page the script adds missing
  ids but changes nothing else in it.
- --force rewrites whole pages from scratch (the prose is lost).
- The index pages are the script's (between gen markers too, so an intro above stays).

--list prints what would get a page and why (nothing is written). --dump writes the cross
references as JSON, for writing the prose.

Titles: with --language <the game's Languages/Language.txt> each page is titled with the name the
player sees in the game: a unit's or building's BLUEPRINT_TOOLTIP, an upgrade's or command's
DEFAULT_HINT (its first line), and an existing page's title and top heading follow it. Only those
names are taken from Language.txt, never its descriptions; the prose stays our own words. Without
--language the titles come from the blueprint names, tidied by the TITLES table below. (In the
game the help viewer titles these pages from the loaded game's own Language.txt, so a translated
or modded game shows its own names.)

Other languages (see redata/help/TRANSLATING.md): --lang <code> works on redata/help/<code>/.
It never creates pages there (a translator starts them with translation.py start); it refreshes
the generated parts of the pages that exist, in that language: the names from --language (that
language's Language.txt) and the fixed phrases from tools/help/strings/<code>.json. A phrase without
a translation stays English. --phrases writes the list of phrases (tools/help/strings/template.json).

Standard library only.
"""
import argparse, json, os, re, sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
HELP = os.path.join(ROOT, "redata", "help", "en")
DEFAULT_GS = r"C:\Dev\gsfull\Warrior Kings Game Set"
START_FILE = "multi-player extensions.cpp"

# ---------------------------------------------------------------------------------------------
# Reading the game set
# ---------------------------------------------------------------------------------------------

BLUEPRINT_CLASSES = ("LEVEL", "PLAYER", "CHARACTER", "BUILDING", "MARKER", "CONTAINER", "PROP", "CITY",
                     "TOWN", "FORMATION", "ARMY", "MISSILE", "TERRAIN_ZONE", "USER", "BUILDING_MARKER")
BLOCKS = {c: "END_" + c for c in BLUEPRINT_CLASSES}
BLOCKS.update({c + "_EXTENSION": "END_" + c + "_EXTENSION" for c in BLUEPRINT_CLASSES})
for _t in ("COMMAND", "ORDER", "TASK", "CONDITION", "EQUATION", "ACTION_SEQUENCE", "REACTION",
           "PACKAGE_RECEIPT_TRIGGER", "OBJECT_CREATION", "OBJECT_FINDER_DEFINITION", "ORDER_ASSIGNMENT",
           "PACKAGE", "DIPLOMATIC_LADDER", "MISSION_OBJECTIVES", "TRIBUTES", "COMPANY", "3D_CLIP",
           "CAMERA_PATH", "SOUND", "FOOTPRINT", "TERRAIN", "PLAN", "ARMY_CREATION_SCHEDULE", "WORK_ORDER",
           "COMMISSION", "CHARACTER_LADDER", "PASSABILITY_FIELDS", "TERRAIN_BLUEPRINT_LIST"):
    BLOCKS[_t] = "END_" + _t
BLOCKS["GAME_TEXT_WINDOW"] = "GAME_TEXT_WINDOW_END"

_COMMENT = re.compile(r'"[^"\r\n]*"|/\*.*?(?:\*/|\Z)|//[^\r\n]*', re.S)


def strip_comments(text):
    """Drops /* ... */ and // comments (outside quotes), keeping the line count."""
    def repl(m):
        t = m.group(0)
        return t if t.startswith('"') else "\n" * t.count("\n")
    return _COMMENT.sub(repl, text)


def tokens(line):
    return [a if a else b for a, b in re.findall(r'"([^"\r\n]*)"?|(\S+)', line)]


def low(name):
    return name.lower()


class Block:
    def __init__(self, kind, name, lines, file, extension, model):
        self.kind, self.name, self.lines, self.file = kind, name, lines, file
        self.extension, self.model = extension, model


class GameSet:
    """The blocks of the game set. Names are looked up without case, as the engine does."""

    def __init__(self, root):
        self.root = root
        self.files = []
        self.blocks = {}          # kind -> lower name -> [Block], extensions after the base
        self.defined = {}         # DEFINE_VALUE
        self.seen = set()
        self.load(START_FILE)

    def load(self, rel):
        key = rel.lower().replace("/", "\\")
        if key in self.seen or "*" in rel:
            return
        self.seen.add(key)
        p = os.path.join(self.root, rel.replace("\\", os.sep))
        if not os.path.exists(p):
            return
        self.files.append(rel)
        text = strip_comments(open(p, encoding="latin-1").read())
        lines = [tokens(l) for l in text.splitlines()]
        links, i = [], 0
        while i < len(lines):
            t = lines[i]
            i += 1
            if not t:
                continue
            tag = t[0]
            if tag == "LINK_GAME_SET" and len(t) > 1:
                links.append(t[1])
            elif tag == "DEFINE_VALUE" and len(t) > 2:
                try:
                    self.defined[low(t[1])] = float(t[2])
                except ValueError:
                    pass
            elif tag in BLOCKS:
                end, body = BLOCKS[tag], []
                while i < len(lines):
                    l = lines[i]
                    i += 1
                    if l and l[0] == end:
                        break
                    if l:
                        body.append(l)
                ext = tag.endswith("_EXTENSION")
                kind = tag[:-len("_EXTENSION")] if ext else tag
                name = t[1] if len(t) > 1 else ""
                b = Block(kind, name, body, rel, ext, t[2] if len(t) > 2 else "")
                self.blocks.setdefault(kind, {}).setdefault(low(name), []).append(b)
        # linked files are read after the file that links them
        for l in links:
            self.load(l)

    def get(self, kind, name):
        return self.blocks.get(kind, {}).get(low(name), [])

    def first(self, kind, name):
        bs = self.get(kind, name)
        base = [b for b in bs if not b.extension]
        return base[0] if base else (bs[0] if bs else None)


# ---------------------------------------------------------------------------------------------
# Blueprints, commands, research
# ---------------------------------------------------------------------------------------------

RESOURCES = ("Food", "Wood", "Gold", "Stone")

# Working out an equation as far as the blueprint alone decides it (Data.usable).
ITEM_CHANGERS = ("SET_ITEM", "INCREASE_ITEM", "DECREASE_ITEM", "SET_INDEXED_ITEM", "INCREASE_INDEXED_ITEM",
                 "DECREASE_INDEXED_ITEM", "ITEM_MODIFICATION")
SELF_FINDERS = ("FINDER_SELF", "FINDER_SELECTED_OBJECT")    # in a command's test both are the unit
ENODE_ARITY = {op: 1 for op in ("NEGATE", "NOT", "IS_ZERO", "IS_POSITIVE", "IS_NEGATIVE", "ABSOLUTE_VALUE",
                                "RANDOM_UP_TO", "ROUND", "TRUNC", "CEIL")}
ENODE_ARITY.update({op: 2 for op in ("ADDITION", "SUBTRACTION", "MULTIPLICATION", "DIVISION", "AND", "OR",
                                     "LESS_THAN", "LESS_THAN_OR_EQUAL_TO", "GREATER_THAN",
                                     "GREATER_THAN_OR_EQUAL_TO", "EQUALS", "MAX", "MIN", "RANDOM_INTEGER",
                                     "RANDOM_RANGE")})
ENODE_ARITY.update({"IF_THEN_ELSE": 3, "IS_BETWEEN": 3, "FRONT_BACK_LEFT_RIGHT": 4})


def enode(op, a):
    """One equation node, as wkbai's Enode* classes work it out; None where an input is unknown
    (AND and OR still decide when one known input settles them)."""
    if op == "AND":
        if any(x is not None and x <= 0 for x in a):
            return 0.0
        return 1.0 if all(x is not None for x in a) else None
    if op == "OR":
        if any(x is not None and x > 0 for x in a):
            return 1.0
        return 0.0 if all(x is not None for x in a) else None
    if op == "IF_THEN_ELSE":
        if a[0] is None:
            return a[1] if a[1] is not None and a[1] == a[2] else None
        return a[1] if a[0] > 0 else a[2]
    if any(x is None for x in a):
        return None
    f = {"NEGATE": lambda x: -x[0], "NOT": lambda x: float(x[0] <= 0), "IS_ZERO": lambda x: float(x[0] == 0),
         "IS_POSITIVE": lambda x: float(x[0] > 0), "IS_NEGATIVE": lambda x: float(x[0] < 0),
         "ABSOLUTE_VALUE": lambda x: abs(x[0]), "ADDITION": lambda x: x[0] + x[1],
         "SUBTRACTION": lambda x: x[0] - x[1], "MULTIPLICATION": lambda x: x[0] * x[1],
         "LESS_THAN": lambda x: float(x[0] < x[1]), "LESS_THAN_OR_EQUAL_TO": lambda x: float(x[0] <= x[1]),
         "GREATER_THAN": lambda x: float(x[0] > x[1]), "GREATER_THAN_OR_EQUAL_TO": lambda x: float(x[0] >= x[1]),
         "EQUALS": lambda x: float(x[0] == x[1]), "MAX": max, "MIN": min,
         "IS_BETWEEN": lambda x: float(x[1] < x[0] < x[2])}.get(op)
    return f(a) if f else None


class Blueprint:
    def __init__(self, gs, cls, name):
        self.cls = cls
        self.items, self.commands, self.reactions = {}, [], []
        self.spawns, self.builds, self.typetags = [], [], {}
        self.tooltip, self.btype, self.model, self.file = "", "", "", ""
        self.selectable, self.traverse = True, []
        base = gs.first(cls, name)
        self.name = base.name if base else name
        self._read(gs, cls, name, set())

    def _read(self, gs, cls, name, stack):
        if low(name) in stack:
            return
        stack.add(low(name))
        blocks = gs.get(cls, name)
        base = [b for b in blocks if not b.extension][:1]
        for b in base + [b for b in blocks if b.extension]:
            if not b.extension:
                self.model, self.file = b.model, b.file
            for l in b.lines:
                tag, a = l[0], l[1:]
                if tag == "INHERITS_FROM" and a:
                    self._read(gs, cls, a[0], stack)
                elif tag in ("STARTS_WITH_ITEM", "ITEM") and len(a) >= 2:
                    try:
                        self.items[a[0]] = float(a[1])
                    except ValueError:
                        pass
                elif tag == "OFFERS_COMMAND" and a:
                    self.commands.append(a[0])
                elif tag == "CAN_SPAWN" and a:
                    self.spawns.append(a[0])
                elif tag == "CAN_BUILD" and a:
                    self.builds.append(a[0])
                elif tag == "INTRINSIC_REACTION" and a:
                    self.reactions.append(a[0])
                elif tag == "BLUEPRINT_TOOLTIP" and a:
                    self.tooltip = a[0]
                elif tag == "BUILDING_TYPE" and a:
                    self.btype = a[0]
                elif tag == "OBJECT_IS_SELECTABLE" and a:
                    self.selectable = a[0] != "0"
                elif tag == "CAN_TRAVERSE" and a:
                    self.traverse.append(a[0])
                elif tag == "MAP_TYPE_TAG" and len(a) >= 3:
                    self.typetags[a[0]] = a[2]

    def offered(self):
        """Command names on this blueprint's buttons, as the engine reads them."""
        out = []
        for c in self.commands + ["Spawn " + s for s in self.spawns] + ["Build " + s for s in self.builds]:
            if low(c) not in (low(x) for x in out):
                out.append(c)
        return out

    def item(self, *names):
        for n in names:
            if self.items.get(n):
                return self.items[n]
        return 0


def value_of(gs, toks):
    """CONSTANT n / DEFINED_VALUE "x" -> number, or None."""
    if len(toks) >= 2 and toks[0] == "CONSTANT":
        try:
            return float(toks[1])
        except ValueError:
            return None
    if len(toks) >= 2 and toks[0] == "DEFINED_VALUE":
        return gs.defined.get(low(toks[1]))
    return None


class Data:
    """Everything the pages need, cross-referenced."""

    def __init__(self, gs):
        self.gs = gs
        self.bp = {}
        for cls in ("CHARACTER", "BUILDING"):
            for key in gs.blocks.get(cls, {}):
                if any(not b.extension for b in gs.blocks[cls][key]):
                    b = Blueprint(gs, cls, key)
                    self.bp[(cls, key)] = b
        self.commands = {}
        for key, bs in gs.blocks.get("COMMAND", {}).items():
            self.commands[key] = self.read_command(bs[0])
        self.setters = {}         # lower item -> [(kind, name)]; kind: research / built
        self.makers = {}          # (cls, lower name) -> [(kind, name)]; kind: research / command / built
        self._effects()
        # items something in the game set changes (on anyone): their value on a unit is not its start value
        self.changed_items = set()
        for kd in gs.blocks.values():
            for bs in kd.values():
                for b in bs:
                    for l in b.lines:
                        for i, t in enumerate(l[:-1]):
                            if t in ITEM_CHANGERS:
                                self.changed_items.add(low(l[i + 1]))

    def blueprint(self, cls, name):
        return self.bp.get((cls, low(name)))

    def command(self, name):
        return self.commands.get(low(name))

    # ---- commands ----
    def read_command(self, b):
        c = {"name": b.name, "icon": "", "order": "", "impossible": [], "wait": [], "icon_cond": [],
             "stampdown": None, "hint": "", "file": b.file, "start": []}
        for l in b.lines:
            tag, a = l[0], l[1:]
            if tag in ("BUTTON_AVAILABLE", "BUTTON_ENABLED") and a and not c["icon"]:
                c["icon"] = a[0]
            elif tag == "USE_ORDER" and a:
                c["order"] = a[0]
            elif tag == "CONDITION_IMPOSSIBLE" and a:
                c["impossible"].append(a[0])
            elif tag == "CONDITION_WAIT" and a:
                c["wait"].append(a[0])
            elif tag == "ICON_CONDITION" and a:
                c["icon_cond"].append(a[0])
            elif tag == "STAMPDOWN_OBJECT" and len(a) >= 2:
                c["stampdown"] = (a[0], a[1])
            elif tag == "DEFAULT_HINT" and a:
                c["hint"] = a[0]
            elif tag == "ACTION" and a:
                c["start"].append(a)
        c["cursor_only"] = not c["icon"]
        o = self.gs.first("ORDER", c["order"]) if c["order"] else None
        c["class_type"], c["tasks"], c["order_actions"] = "", [], []
        if o:
            for l in o.lines:
                if l[0] == "CLASS_TYPE" and len(l) > 1:
                    c["class_type"] = l[1]
                elif l[0] == "USE_TASK" and len(l) > 1:
                    c["tasks"].append(l[1])
                elif l[0] == "ACTION" and len(l) > 1:
                    c["order_actions"].append(l[1:])
        return c

    def actions(self, kinds, name, seen):
        """All ACTION lines of a block, following EXECUTE_SEQUENCE and object creations."""
        out = []
        for kind in kinds:
            if (kind, low(name)) in seen:
                continue
            seen.add((kind, low(name)))
            for b in self.gs.get(kind, name):
                for l in b.lines:
                    if l[0] != "ACTION" or len(l) < 2:
                        continue
                    a = l[1:]
                    out.append(a)
                    if a[0].startswith("EXECUTE_SEQUENCE") and len(a) > 1:
                        out += self.actions(("ACTION_SEQUENCE",), a[1], seen)
                    elif a[0] == "CREATE_OBJECT_VIA" and len(a) > 1:
                        for oc in self.gs.get("OBJECT_CREATION", a[1]):
                            for ol in oc.lines:
                                if ol[0] == "TYPE_TO_CREATE" and len(ol) > 2:
                                    out.append(["CREATE_OBJECT", ol[1], ol[2]])
                                elif ol[0] == "ACTION" and len(ol) > 1:
                                    out.append(ol[1:])
                    elif a[0] == "ADD_REACTION" and len(a) > 2:
                        out += self.actions(("REACTION",), a[2], seen)
                    elif a[0] == "ASSIGN_ORDER_VIA" and len(a) > 1:
                        for oa in self.gs.get("ORDER_ASSIGNMENT", a[1]):
                            for ol in oa.lines:
                                if ol[0] == "ORDER_TO_ASSIGN" and len(ol) > 1:
                                    out += self.order_actions(ol[1], seen)
        return out

    def order_actions(self, name, seen):
        out = []
        if ("ORDER", low(name)) in seen:
            return out
        seen.add(("ORDER", low(name)))
        for o in self.gs.get("ORDER", name):
            for l in o.lines:
                if l[0] == "USE_TASK" and len(l) > 1:
                    out += self.actions(("TASK",), l[1], seen)
        return out

    def command_actions(self, name):
        c = self.command(name)
        if not c:
            return []
        seen = set()
        out = list(c["start"]) + list(c["order_actions"])
        for t in c["tasks"]:
            out += self.actions(("TASK",), t, seen)
        return out

    def research(self, name):
        """Cost, time, unlocked items and conversions of a research command."""
        cost, time, sets, converts = {}, None, [], []
        for a in self.command_actions(name):
            if a[0] == "DECREASE_ITEM" and len(a) >= 4 and a[1] in RESOURCES and a[2] == "FINDER_PLAYER":
                v = value_of(self.gs, a[3:])
                if v:
                    cost[a[1]] = cost.get(a[1], 0) + v
            elif a[0] == "SET_ITEM" and len(a) >= 4 and a[1] == "Hit Point Capacity Of Object Being Spawned":
                time = value_of(self.gs, a[3:])
            elif a[0] in ("SET_ITEM", "INCREASE_ITEM") and len(a) >= 3 and a[2] == "FINDER_PLAYER":
                if a[1] not in RESOURCES and not a[1].endswith("Status") and a[1] not in sets:
                    sets.append(a[1])
        converts = self.made_by_actions(self.command_actions(name))
        return cost, time, sets, converts

    def made_by_actions(self, acts):
        out = []
        for a in acts:
            if a[0] in ("CONVERT_TO", "CREATE_OBJECT") and len(a) >= 3 and a[1] in ("CHARACTER", "BUILDING"):
                n = a[2]
                if n.startswith("Spawning "):
                    n = n[len("Spawning "):]
                fb = self.bp.get((a[1], low(n)))
                if fb and n.endswith("Foundation") and "Fully Built Version" in fb.typetags:
                    n = fb.typetags["Fully Built Version"]
                if (a[1], n) not in out:
                    out.append((a[1], n))
        return out

    def _effects(self):
        for key, c in self.commands.items():
            acts = self.command_actions(key)
            kind = "research" if (c["class_type"] == "UPGRADE" or key.startswith("research ")) else "command"
            if kind == "research":
                for it in self.research(key)[2]:
                    self.setters.setdefault(low(it), []).append(("research", c["name"]))
            for cls, n in self.made_by_actions(acts):
                if cls == "BUILDING" and c["stampdown"]:
                    continue
                self.makers.setdefault((cls, low(n)), []).append((kind, c["name"]))
        for (cls, key), b in self.bp.items():
            seen = set()
            acts = []
            for r in b.reactions:
                acts += self.actions(("REACTION",), r, seen)
            for a in acts:
                if a[0] in ("SET_ITEM", "INCREASE_ITEM") and len(a) >= 3 and a[2] == "FINDER_PLAYER":
                    if a[1] not in RESOURCES:
                        lst = self.setters.setdefault(low(a[1]), [])
                        if ("built", b.name) not in lst:
                            lst.append(("built", b.name))
            for mcls, n in self.made_by_actions(acts):
                if low(n) == key:
                    continue
                lst = self.makers.setdefault((mcls, low(n)), [])
                if ("built", b.name) not in lst:
                    lst.append(("built", b.name))

    # ---- can a blueprint ever use a command? ----
    # The engine's test (Command::canBeExecuted, the button panel and the right-click): every
    # CONDITION_IMPOSSIBLE and CONDITION_WAIT holds, and either the button shows (all ICON_CONDITIONs)
    # or the cursor does (all CURSOR_CONDITIONs, or one CURSOR_AVAILABLE). The equations are worked
    # out here with only what is fixed for the blueprint itself: its class and name, and its start
    # value of items nothing ever changes. The target, the player, the level and everything else
    # are unknown (None). A command whose every way in is surely false for the blueprint is one it
    # offers but can never use (the Peasant Farmer's "Convert to Militia", meant for the Peasant).
    def _leaf(self, t, bp, stack):
        if not t:
            return None
        h = t[0]
        try:
            if h == "CONSTANT":
                return float(t[1])
            if h == "DEFINED_VALUE":
                return self.gs.defined.get(low(t[1]))
            if h == "ITEM_VALUE" and len(t) == 3 and t[2] in SELF_FINDERS:
                if low(t[1]) in self.changed_items:
                    return None
                return next((v for k, v in bp.items.items() if low(k) == low(t[1])), 0.0)
            if h == "OBJECT_TYPE" and len(t) == 4 and t[3] in SELF_FINDERS:
                return 1.0 if t[1] == bp.cls and low(t[2]) == low(bp.name) else 0.0
            if h == "OBJECT_CLASS" and len(t) == 3 and t[2] in SELF_FINDERS:
                return 1.0 if t[1] == bp.cls else 0.0
            if h == "EQUATION_RESULT" and len(t) == 3 and t[2] in SELF_FINDERS:
                return self.eq_value(t[1], bp, stack)
        except (ValueError, IndexError):
            return None
        return None

    def eq_value(self, name, bp, stack=frozenset()):
        """An equation's value for the blueprint, or None if it depends on anything else."""
        if low(name) in stack:
            return None
        b = self.gs.first("EQUATION", name)
        if not b:
            return None
        lines = [l[1:] for l in b.lines if l[0] == "ENODE"]
        pos = [0]
        inner = stack | {low(name)}

        def node():
            if pos[0] >= len(lines):
                return None
            t = lines[pos[0]]
            pos[0] += 1
            n = ENODE_ARITY.get(t[0] if t else "", 0)
            if not n:
                return self._leaf(t, bp, inner)
            return enode(t[0], [node() for _ in range(n)])
        return node()

    def cond_value(self, name, bp):
        b = self.gs.first("CONDITION", name)
        for l in (b.lines if b else []):
            if l[0] == "TEST":
                return self._leaf(l[1:], bp, frozenset())
        return None

    def usable(self, bp, cname):
        """False if the blueprint offers the command but can never meet its conditions."""
        b = self.gs.first("COMMAND", cname)
        if not b:
            return True
        never = lambda v: v is not None and v <= 0
        tests, icon, cursor, avail = [], [], [], []
        button = has_cursor = False
        for l in b.lines:
            tag, a = l[0], l[1:]
            if tag in ("CONDITION_IMPOSSIBLE", "CONDITION_WAIT") and a:
                tests.append(a[0])
            elif tag == "ICON_CONDITION" and a:
                icon.append(a[0])
            elif tag == "CURSOR_CONDITION" and a:
                cursor.append(a[0])
            elif tag == "CURSOR_AVAILABLE" and a:
                avail.append(a[0])
            elif tag in ("BUTTON_ENABLED", "BUTTON_AVAILABLE") and a:
                button = True
            elif tag == "CURSOR" and a:
                has_cursor = True
        if any(never(self.cond_value(c, bp)) for c in tests):
            return False
        if not (button or has_cursor or avail):
            return True          # given by a key (Halt) or by the engine
        by_button = button and not any(never(self.eq_value(e, bp)) for e in icon)
        if avail:
            by_cursor = any(not never(self.cond_value(c, bp)) for c in avail)
        else:
            by_cursor = has_cursor and not any(never(self.eq_value(e, bp)) for e in cursor)
        return by_button or by_cursor

    # ---- conditions ----
    def cond_items(self, cname, seen=None):
        """The player items a condition (or equation) tests."""
        seen = seen if seen is not None else set()
        out = []
        for kind in ("CONDITION", "EQUATION"):
            if (kind, low(cname)) in seen:
                continue
            seen.add((kind, low(cname)))
            for b in self.gs.get(kind, cname):
                for l in b.lines:
                    for i, tok in enumerate(l):
                        if tok == "ITEM_VALUE" and i + 2 < len(l) and l[i + 2] == "FINDER_PLAYER":
                            out.append(l[i + 1])
                        elif tok == "EQUATION_RESULT" and i + 1 < len(l):
                            out += self.cond_items(l[i + 1], seen)
        return [o for o in out if o not in RESOURCES]

    def needs(self, cname):
        """[(item, [(kind, source)])] for a command's impossible conditions."""
        c = self.command(cname)
        if not c:
            return []
        out = []
        conds = list(c["impossible"])
        for ic in c["icon_cond"]:
            if ic in ICON_NOTES or re.search(r"^(No |Not |Haven|Player ha)|Disallowed|Tech Level|Ghost", ic):
                continue
            neg = any(t in ("IS_ZERO", "NOT", "LESS_THAN", "EQUALS") for b in self.gs.get("EQUATION", ic)
                      for l in b.lines for t in l)
            if not neg:
                conds.append(ic)
        for cond in conds:
            for it in self.cond_items(cond):
                if low(it) in (low(x[0]) for x in out):
                    continue
                if it.startswith(("Population", "Food Consumption")) or "Status" in it or "Ghost" in it:
                    continue
                out.append((it, self.setters.get(low(it), [])))
        return out


# ---------------------------------------------------------------------------------------------
# Which blueprints get pages, and how pages refer to each other
# ---------------------------------------------------------------------------------------------

def slugify(name):
    s = name.lower().replace("'", "").replace("&", "and")
    return re.sub(r"[^a-z0-9]+", "-", s).strip("-")


TITLES = {
    "Keep_Pagan": "Keep (Pagan)", "Keep_Celestial": "Keep (Imperial)", "Keep_Tech": "Keep (Renaissance)",
    "Castle_Pagan": "Castle (Pagan)", "Castle_Celestial": "Castle (Imperial)", "Castle_Tech": "Castle (Renaissance)",
    "Palace_Pagan": "Palace (Pagan)", "Palace_Celestial": "Palace (Imperial)", "Palace_Tech": "Palace (Renaissance)",
    "Barracks Stone": "Barracks", "Armoury Stone": "Armoury", "Cathedral Stone": "Cathedral",
    "Church Stone": "Church", "Guildhall Stone": "Guildhall", "Shop Stone": "Shop",
    "Warehouse Stone": "Warehouse", "Dock Wood": "Dock", "Tavern Wood": "Tavern", "Village Wood": "Village",
    "Fort Wood": "Fort (wooden)", "Fort Stone": "Fort (stone)", "Standard Farm": "Farm",
    "Stockade Gatehouse Closed": "Stockade Gatehouse", "Unaligned Gatehouse Closed": "Unaligned Gatehouse",
    "Technological Gatehouse Closed": "Technological Gatehouse", "Celestial Gatehouse Closed": "Celestial Gatehouse",
    "Unaligned Tower Stone": "Unaligned Tower", "Technological Tower Stone": "Technological Tower",
    "Gunner Bayonet": "Gunner with Bayonet",
    "Gold Elemental Large": "Gold Elemental", "Stone Elemental Large": "Stone Elemental",
    "Hero Pagan": "Pagan Hero", "Hero Technological": "Technological Hero",
    "Heavy Horseman Upgrade": "Heavy Horseman (Wickerman)", "Pagan Archer Upgrade": "Pagan Archer (Wickerman)",
}

UPGRADE_TITLES = {"Fort Wood": "Wooden Forts"}

# Blueprints that are another state of a unit or building: their ids go on that page.
ALIASES = {
    ("CHARACTER", "Rocket Launcher Limbered"): "Rocket Launcher",
    ("CHARACTER", "Trebuchet Limbered"): "Trebuchet",
    ("CHARACTER", "Multiplayer Trebuchet"): "Trebuchet",
    ("CHARACTER", "Siege Tower Open"): "Siege Tower",
    ("CHARACTER", "Artos Young"): "Artos",
    ("CHARACTER", "Gunner Bayonet Without Bayonet"): "Gunner Bayonet",
    ("CHARACTER", "Wood Elemental - Awakening Animation"): "Wood Elemental",
    ("CHARACTER", "Large Stone Elemental - Awakening Animation"): "Stone Elemental Large",
    ("CHARACTER", "Large Gold Elemental - Awakening Animation"): "Gold Elemental Large",
    ("BUILDING", "Fertile Farm"): "Standard Farm", ("BUILDING", "Barren Farm"): "Standard Farm",
    ("BUILDING", "Farm Wood"): "Standard Farm",
    ("BUILDING", "Stockade Gatehouse"): "Stockade Gatehouse Closed",
    ("BUILDING", "Unaligned Gatehouse Open"): "Unaligned Gatehouse Closed",
    ("BUILDING", "Technological Gatehouse Open"): "Technological Gatehouse Closed",
    ("BUILDING", "Celestial Gatehouse Open"): "Celestial Gatehouse Closed",
}
for _w in ("Stockade", "Unaligned Wall", "Technological Wall", "Celestial Wall", "Unaligned Tower Stone",
           "Technological Tower Stone", "Celestial Tower"):
    for _p in (" Corner In", " Corner Out", " Cross Piece"):
        ALIASES[("BUILDING", _w + _p)] = _w

# Characters that come into play without a Spawn command (made by research, spells, buildings).
EXTRA_UNITS = ("Militia", "Man at Arms", "Celestial Knight", "Michael", "Abaddon", "Behemoth", "Horde Spewer",
               "Undead Legion", "Wood Elemental", "Stone Elemental Large", "Gold Elemental Large",
               "Pagan Archer Upgrade", "Heavy Horseman Upgrade", "Gunner Bayonet", "Monastic Knight",
               "Artos", "Celestial Hero", "Hero Pagan", "Hero Technological", "Gibbering Horde")
# Buildings the Manor (and walls, forts) turn into.
EXTRA_BUILDINGS = ("Keep_Pagan", "Keep_Celestial", "Keep_Tech", "Castle_Pagan", "Castle_Celestial", "Castle_Tech",
                   "Palace_Pagan", "Palace_Celestial", "Palace_Tech", "Celestial Tower", "Technological Tower Stone",
                   "Unaligned Tower Stone", "Holy Chapter", "Explosive Mine")
# Commands that change a unit into another only for a while (disguises, modes): not "comes from".
MAKER_SKIP_COMMANDS = ("Start Mission", "Stop Mission", "Limber", "Unlimber", "Close Siege Tower",
                       "Siege Tower Dock With Wall", "Take Cart", "Enter Cart")
# Offered commands without a page of their own (internal, or duplicates of another page).
SKIP_COMMANDS = ("Fire At Will",)

# Unit groups for the index (by the game set's own unit-class items, then by hand).
UNIT_GROUPS = [
    ("civilians", "Civilians and workers"), ("religious", "Priests and magic users"),
    ("infantry", "Foot soldiers"), ("missile", "Missile troops"), ("cavalry", "Cavalry"),
    ("siege", "Siege engines"), ("vessels", "Ships"), ("creatures", "Summoned creatures"),
    ("heroes", "Heroes and leaders"),
]
UNIT_GROUP_OF = {
    "Peasant": "civilians", "Peasant Farmer": "civilians", "Cart": "civilians", "Merchant": "civilians",
    "Supply Wagon": "civilians", "Scout": "civilians", "Spy": "civilians", "Sapper": "civilians",
    "Militia": "infantry", "Explosive Cart": "siege",
    "Priest": "religious", "Monk": "religious", "Bishop": "religious", "Inquisitor": "religious",
    "High Priestess": "religious", "Arch Druid": "religious", "Succubus": "religious",
    "Michael": "creatures", "Abaddon": "creatures", "Behemoth": "creatures", "Horde Spewer": "creatures",
    "Gibbering Horde": "creatures", "Undead Legion": "creatures", "Wood Elemental": "creatures",
    "Stone Elemental Large": "creatures", "Gold Elemental Large": "creatures",
    "Artos": "heroes", "Celestial Hero": "heroes", "Hero Pagan": "heroes", "Hero Technological": "heroes",
    "Vandavar": "heroes",
}

BUILDING_GROUPS = [
    ("centre", "Town centre"), ("economy", "Economy and supply"), ("military", "Training and research"),
    ("religion", "Religion and the paths"), ("defence", "Walls, towers and forts"), ("other", "Other"),
]
BUILDING_GROUP_OF = {
    "Manor": "centre", "Keep_Pagan": "centre", "Keep_Celestial": "centre", "Keep_Tech": "centre",
    "Castle_Pagan": "centre", "Castle_Celestial": "centre", "Castle_Tech": "centre",
    "Palace_Pagan": "centre", "Palace_Celestial": "centre", "Palace_Tech": "centre",
    "Standard Farm": "economy", "Windmill": "economy", "Sawmill": "economy", "Village Wood": "economy",
    "Warehouse Stone": "economy", "Shop Stone": "economy", "Trading Post": "economy", "Bank": "economy",
    "Dock Wood": "economy", "Tavern Wood": "economy", "Guildhall Stone": "military",
    "Barracks Stone": "military", "Stables": "military", "Armoury Stone": "military",
    "Blacksmith": "military", "Hall of Warriors": "military", "University": "military",
    "Observatory": "military", "Observation Point": "other", "Training Dummy": "military",
    "Church Stone": "religion", "Cathedral Stone": "religion", "Monastery": "religion",
    "Scriptorium": "religion", "Holy Chapter": "religion", "Michael Statue": "religion",
    "Maypole": "religion", "Temple": "religion", "Henge": "religion", "Black Mass": "religion",
    "Arcanum": "religion", "Wickerman": "religion",
    "Palings": "defence", "Stockade": "defence", "Stockade Gatehouse Closed": "defence",
    "Unaligned Wall": "defence", "Unaligned Gatehouse Closed": "defence", "Technological Wall": "defence",
    "Technological Gatehouse Closed": "defence", "Celestial Wall": "defence",
    "Celestial Gatehouse Closed": "defence", "Watch Tower": "defence", "Fort Wood": "defence",
    "Fort Stone": "defence", "Unaligned Tower Stone": "defence", "Technological Tower Stone": "defence",
    "Celestial Tower": "defence", "Explosive Mine": "defence",
}

UPGRADE_GROUPS = [
    ("economy", "Economy"), ("infantry", "Foot soldiers"), ("cavalry", "Cavalry"), ("missile", "Missiles and gunpowder"),
    ("siege", "Siege and engineering"), ("defence", "Walls and forts"), ("religion", "Faith and the paths"),
    ("other", "Other"),
]

COMMAND_GROUPS = [
    ("basic", "Moving and fighting"), ("formation", "Formations"), ("work", "Work and supply"),
    ("buildings", "Buildings and garrisons"), ("magic", "Prayers and spells"), ("special", "Special abilities"),
]

# Short notes for the icon conditions that hide a button: the alignment paths.
ICON_NOTES = {
    "No Celestial Buildings": "Pagan or neutral side only: the button goes once you build a [Church](../buildings/church-stone.md) (the Imperial path).",
    "No Pagan Buildings": "Imperial or neutral side only: the button goes once you build a [Maypole](../buildings/maypole.md) (the Pagan path).",
    "Player has decided against the Pure Tech Path": "Not on the pure paths: the button goes once you build a [Michael Statue](../buildings/michael-statue.md) or a [Wickerman](../buildings/wickerman.md).",
    "Completed Festival of Beltane": "Shown after the [Festival of Beltane](../upgrades/festival-of-beltane.md).",
    "Crusade Completed": "Shown after the [Crusade](../upgrades/crusade.md).",
    "Has Chosen the Wickerman Path": "Shown once you have chosen the Wickerman path.",
    "Am at Tech Level 2": "Shown from the second Manor level (Keep) on.",
    "Am at Tech Level 3": "Shown from the third Manor level (Castle) on.",
    "Have researched Greek Fire": "Shown after [Greek Fire](../upgrades/greek-fire.md).",
}


# Commands without a button of their own: how the player gives them (controls/orders.md).
CURSOR_NOTE = ("No button of its own: you give it with a right-click on the target "
               "(see [Orders](../controls/orders.md#the-right-click)).")
CURSOR_NOTES = {
    "Halt": "Given with the **H** key (see [Orders](../controls/orders.md#halt)).",
    "Guard": "Hold **G** and right-click a unit or building (see [Orders](../controls/orders.md#follow-and-guard)).",
    "Follow": "Hold **F** and right-click a unit (see [Orders](../controls/orders.md#follow-and-guard)).",
    "Fort Attack": "No button: the building shoots at enemies in range by itself.",
}


class Page:
    def __init__(self, folder, key, title, cls=None, bp=None):
        self.folder, self.key, self.title, self.cls, self.bp = folder, key, title, cls, bp
        self.slug = slugify(key)
        self.ids, self.icon, self.group = [], "", "other"
        self.sections = {}        # gen part -> markdown
        self.static = []          # (label, value)
        self.facts = None         # blueprint name for the ```facts block

    @property
    def path(self):
        return self.folder + "/" + self.slug

    def link(self, frm_folder):
        rel = self.slug + ".md" if frm_folder == self.folder else "../" + self.folder + "/" + self.slug + ".md"
        return f"[{self.title}]({rel})"


def fmt(v):
    if v is None:
        return ""
    return str(int(v)) if float(v).is_integer() else f"{v:g}"


def cost_text(items_or_cost):
    parts = []
    for r in RESOURCES:
        v = items_or_cost.get(r + " Cost", items_or_cost.get(r, 0)) or 0
        if v:
            parts.append(f"{fmt(v)} {r.lower()}")
    return ", ".join(parts)


class Catalogue:
    def __init__(self, data):
        self.d = data
        self.units, self.buildings, self.upgrades, self.cmds = {}, {}, {}, {}
        self.offerers = {}        # lower command -> [blueprint key (cls, lower)]
        self._reach()
        self._pages()

    # ---- what the player can get ----
    def fully_built(self, foundation):
        b = self.d.blueprint("BUILDING", foundation)
        if b and "Fully Built Version" in b.typetags:
            return self.d.blueprint("BUILDING", b.typetags["Fully Built Version"])
        return b

    def _reach(self):
        d = self.d
        todo = [("CHARACTER", "peasant"), ("BUILDING", "manor")]
        todo += [("CHARACTER", low(n)) for n in EXTRA_UNITS] + [("BUILDING", low(n)) for n in EXTRA_BUILDINGS]
        seen = set()
        while todo:
            k = todo.pop()
            if k in seen or k not in d.bp:
                continue
            seen.add(k)
            for cname in d.bp[k].offered():
                c = d.command(cname)
                if not c:
                    continue
                lst = self.offerers.setdefault(low(cname), [])
                if k not in lst:
                    lst.append(k)
                if low(cname).startswith("spawn "):
                    todo.append(("CHARACTER", low(cname[6:])))
                if c["stampdown"] and c["stampdown"][0] == "BUILDING":
                    fb = self.fully_built(c["stampdown"][1])
                    if fb:
                        todo.append(("BUILDING", low(fb.name)))
                        todo.append(("CHARACTER", low(fb.name)))   # siege engines
                for cls, n in d.research(cname)[3] if c["class_type"] == "UPGRADE" else d.made_by_actions(d.command_actions(cname)):
                    todo.append((cls, low(n)))
        self.reached = seen

    def canonical(self, cls, name):
        """The blueprint whose page shows this one."""
        t = ALIASES.get((cls, name))
        if t is None:
            for (c, n), v in ALIASES.items():
                if c == cls and low(n) == low(name):
                    t = v
        return t or name

    def page_of(self, cls, name):
        name = self.canonical(cls, name)
        if cls == "CHARACTER" or (cls == "BUILDING" and low(name) in self.units):
            return self.units.get(low(name))
        return self.buildings.get(low(name))

    def bp_page(self, name):
        """A page for a blueprint name of either class."""
        return self.units.get(low(self.canonical("CHARACTER", name))) or \
            self.buildings.get(low(self.canonical("BUILDING", name)))

    def _pages(self):
        d = self.d
        skip_char = re.compile(r"^(Dead |Spawning )|King Piece|Possessed|^Pos |Rider|sans Rider|^Horse$|^Still Horse$|"
                               r"^Sheep$|^Chicken$|^Camel$|^Fuse$|Creature Creator|Sub-Missile|Junk$|Constructing$|"
                               r"^Peasant Farmer$|^Resource Wagon$")
        # units
        for cls, key in sorted(self.reached):
            if cls != "CHARACTER":
                continue
            b = d.bp[(cls, key)]
            name = self.canonical(cls, b.name)
            if skip_char.search(b.name) or not b.selectable:
                continue
            if low(name) not in self.units:
                main = d.blueprint("CHARACTER", name)
                p = Page("units", main.name, TITLES.get(main.name, main.name), "CHARACTER", main)
                p.facts = main.name
                self.units[low(name)] = p
            p = self.units[low(name)]
            for i in ("unit/" + b.name,):
                if i not in p.ids:
                    p.ids.append(i)
        # buildings
        for cls, key in sorted(self.reached):
            if cls != "BUILDING":
                continue
            b = d.bp[(cls, key)]
            if b.name.endswith("Foundation") or (not b.selectable and b.name not in EXTRA_BUILDINGS):
                continue
            name = self.canonical(cls, b.name)
            if low(name) in self.units:
                self.units[low(name)].ids.append("building/" + b.name)
                continue
            if low(name) not in self.buildings:
                main = d.blueprint("BUILDING", name)
                p = Page("buildings", main.name, TITLES.get(main.name, main.name), "BUILDING", main)
                p.facts = main.name
                self.buildings[low(name)] = p
            p = self.buildings[low(name)]
            if "building/" + b.name not in p.ids:
                p.ids.append("building/" + b.name)
        # commands: spawn / build / research / others
        for lc, who in sorted(self.offerers.items()):
            c = d.commands[lc]
            if c["name"] in SKIP_COMMANDS:
                continue
            if lc.startswith("spawn "):
                p = self.page_of("CHARACTER", c["name"][6:])
                if p:
                    p.ids.append("command/" + c["name"])
                    p.icon = p.icon or c["icon"]
                    p.spawn = c["name"]
                continue
            if c["stampdown"]:
                fb = self.fully_built(c["stampdown"][1])
                p = self.bp_page(fb.name) if fb else None
                if p:
                    p.ids.append("command/" + c["name"])
                    p.icon = p.icon or c["icon"]
                    p.__dict__.setdefault("build_cmds", []).append(c["name"])
                continue
            if c["class_type"] == "UPGRADE" or lc.startswith("research "):
                t = c["name"][len("Research "):] if lc.startswith("research ") else c["name"]
                base = re.sub(r"( Stone)? Corner (In|Out)$", "", t)
                if base != t and low("research " + base) in self.upgrades:
                    q = self.upgrades[low("research " + base)]
                    q.ids += ["upgrade/" + t, "command/" + c["name"]]
                    q.__dict__.setdefault("also", []).append(c["name"])
                    continue
                p = Page("upgrades", t, UPGRADE_TITLES.get(t, t))
                p.ids = ["upgrade/" + t, "command/" + c["name"]]
                p.icon, p.cmd = c["icon"], c["name"]
                self.upgrades[lc] = p
                continue
            p = Page("commands", c["name"], c["name"])
            p.ids = ["command/" + c["name"]]
            p.icon, p.cmd = c["icon"], c["name"]
            self.cmds[lc] = p
        for p in list(self.units.values()) + list(self.buildings.values()):
            p.ids = list(dict.fromkeys(p.ids))
        self._fill()

    # ---- sections ----
    def can_use(self, b, cname):
        """The blueprint offers the command and can use it: not "Build" without anything to build
        (the build menu holds its "Build <building>" buttons; wkbai shows no button of its own),
        and not a command whose conditions it can never meet (Data.usable)."""
        if low(cname) == "build" and not any(low(x).startswith("build ") for x in b.offered()):
            return False
        return self.d.usable(b, cname)

    def who_links(self, keys, frm):
        out = []
        for cls, key in keys:
            b = self.d.bp[(cls, key)]
            p = self.bp_page(b.name)
            if p and p.link(frm) not in out:
                out.append(p.link(frm))
        return sorted(out, key=str.lower)

    def source_link(self, kind, name, frm):
        if kind == "research":
            p = self.upgrades.get(low(name))
            return p.link(frm) if p else "the research " + re.sub(r"^Research ", "", name) + " (not offered in a normal game)"
        if kind == "command":
            p = self.cmds.get(low(name)) or self.upgrades.get(low(name))
            return p.link(frm) if p else f"the command {name}"
        p = self.bp_page(name.replace(" Foundation", ""))
        if p:
            return "building a " + p.link(frm) if p.folder == "buildings" else p.link(frm)
        return name

    def needs_lines(self, cname, frm):
        out = []
        c = self.d.command(cname)
        if not c:
            return out
        start = self.d_player_start()
        for it, setters in self.d.needs(cname):
            if start.get(low(it)):
                continue
            srcs = []
            for kind, n in setters:
                if kind == "built":
                    q = self.bp_page(n.replace(" Foundation", ""))
                    if q is not None and q.folder == "units":
                        continue
                s = self.source_link(kind, n, frm)
                if s not in srcs:
                    srcs.append(s)
            linked = [s for s in srcs if "](" in s]
            if linked:
                srcs = linked
            if linked:
                out.append("- " + " or ".join(srcs))
            elif not it.startswith("Can "):
                continue
            else:
                out.append(f"- `{it}` (set by the level or a script, not by anything you build)")
        for ic in c["icon_cond"]:
            if ic in ICON_NOTES:
                out.append("- " + self.title_links(ICON_NOTES[ic]).replace("../" + frm + "/", ""))
        return out

    def title_links(self, text):
        """[x](../units/slug.md) and so on: the link text set to that page's current title."""
        by_path = {q.path: q for q in list(self.units.values()) + list(self.buildings.values())
                   + list(self.upgrades.values()) + list(self.cmds.values())}

        def fix(m):
            q = by_path.get(m.group(2) + "/" + m.group(3))
            return f"[{q.title if q else m.group(1)}](../{m.group(2)}/{m.group(3)}.md{m.group(4) or ''})"
        return re.sub(r"\[([^\]]*)\]\(\.\./(units|buildings|upgrades|commands)/([^)#]+)\.md(#[^)]*)?\)", fix, text)

    def d_player_start(self):
        if not hasattr(self, "_start"):
            self._start = {}
            for key, bs in self.d.gs.blocks.get("PLAYER", {}).items():
                for b in bs:
                    for l in b.lines:
                        if l[0] == "STARTS_WITH_ITEM" and len(l) >= 3:
                            try:
                                v = float(l[2])
                            except ValueError:
                                continue
                            if v:
                                self._start[low(l[1])] = v
        return self._start

    def unlocks_of(self, kind, name):
        """Pages whose commands need an item that this source sets."""
        out = []
        for page in list(self.units.values()) + list(self.buildings.values()) + list(self.upgrades.values()):
            cmds = []
            if getattr(page, "spawn", None):
                cmds.append(page.spawn)
            cmds += getattr(page, "build_cmds", [])
            if getattr(page, "cmd", None):
                cmds.append(page.cmd)
            for c in cmds:
                for it, setters in self.d.needs(c):
                    if (kind, name) in setters and page not in out:
                        out.append(page)
        return out

    def states(self, page):
        """The page's blueprint and its other states in play (ALIASES: an open gatehouse, a
        limbered rocket launcher), each as the blueprint it is then."""
        out = [page.bp]
        for (cls, n), main in ALIASES.items():
            if low(main) == low(page.key) and (cls, low(n)) in self.reached and self.d.bp[(cls, low(n))] not in out:
                out.append(self.d.bp[(cls, low(n))])
        return out

    def command_links(self, page, frm, exclude=()):
        """The commands the unit or building can use, in any of its states (Catalogue.can_use)."""
        out = []
        for b in self.states(page):
            for cname in b.offered():
                lc = low(cname)
                if lc in exclude or lc.startswith(("spawn ", "build ")) or not self.can_use(b, cname):
                    continue
                p = self.cmds.get(lc)
                if p and p.link(frm) not in out:
                    out.append(p.link(frm))
        return out

    def _fill(self):
        d = self.d
        for p in self.units.values():
            b = p.bp
            sp = getattr(p, "spawn", None)
            at = self.who_links(self.offerers.get(low(sp), []), "units") if sp else []
            by = []
            for bc in getattr(p, "build_cmds", []):
                by += [x for x in self.who_links(self.offerers.get(low(bc), []), "units") if x not in by]
            made = []
            for k in [("CHARACTER", low(p.key))] + [("CHARACTER", low(a[1])) for a in ALIASES if ALIASES[a] == p.key]:
                for kind, n in d.makers.get(k, []):
                    if kind == "command" and (low(n) not in self.cmds and low(n) not in self.upgrades
                                              or n in MAKER_SKIP_COMMANDS
                                              or low(n) in (low(x) for x in b.offered())):
                        continue
                    if kind == "research" and low(n) not in self.upgrades:
                        continue
                    if kind == "built" and p.key in ("Cart", "Militia", "Peasant"):
                        continue
                    src = self.bp_page(n.replace(" Foundation", "")) if kind == "built" else None
                    if kind == "built" and (src is None or src is p):
                        continue
                    s = self.source_link(kind, n, "units")
                    if s not in made:
                        made.append(s)
            lines = []
            if at:
                lines.append("- **Trained at:** " + ", ".join(at))
            if by:
                lines.append("- **Built by:** " + ", ".join(by) + " (the engine is put up on the spot like a building, then rolls out)")
            if made:
                lines.append("- **Comes from:** " + ", ".join(made))
            if not lines:
                lines.append("- Not trained in a normal game: it comes with the level or the campaign.")
            p.sections["trained"] = "\n".join(lines)
            needs = []
            if sp:
                needs += self.needs_lines(sp, "units")
            for bc in getattr(p, "build_cmds", []):
                needs += [x for x in self.needs_lines(bc, "units") if x not in needs]
            p.sections["needs"] = "\n".join(needs) or "- Nothing beyond the building that trains it and the cost."
            leads = []
            builds = []
            for cname in b.offered():
                c = d.command(cname)
                if c and c["stampdown"]:
                    fb = self.fully_built(c["stampdown"][1])
                    q = self.bp_page(fb.name) if fb else None
                    if q and q.link("units") not in builds:
                        builds.append(q.link("units"))
            if builds:
                leads.append("- **Builds:** " + ", ".join(builds))
            conv = []
            for cname in b.offered():
                for cls, n in d.made_by_actions(d.command_actions(cname)):
                    q = self.bp_page(n)
                    if n in ("Cart", "Peasant", "Cart sans Rider"):
                        continue
                    if q and q is not p and q.link("units") not in conv:
                        conv.append(q.link("units"))
            if conv:
                leads.append("- **Can turn into or call up:** " + ", ".join(conv))
            p.sections["leads"] = "\n".join(leads) or "- Nothing: it unlocks no building, unit or research."
            cl = self.command_links(p, "units")
            p.sections["commands"] = ("- " + "\n- ".join(cl)) if cl else "- Only the basic orders."
            self.static_unit(p)
        for p in self.buildings.values():
            b = p.bp
            by = []
            for bc in getattr(p, "build_cmds", []):
                by += [x for x in self.who_links(self.offerers.get(low(bc), []), "buildings") if x not in by]
            made = []
            for kind, n in d.makers.get(("BUILDING", low(p.key)), []):
                if kind == "command" and n.startswith(("Close ", "Open ")):
                    continue
                src = self.bp_page(n.replace(" Foundation", "")) if kind == "built" else None
                if kind == "built" and (src is None or src is p):
                    continue
                s = self.source_link(kind, n, "buildings")
                if s not in made:
                    made.append(s)
            lines = []
            if by:
                lines.append("- **Built by:** " + ", ".join(by))
            if made:
                lines.append("- **Comes from:** " + ", ".join(made))
            if not lines:
                lines.append("- Not built directly: it is on the map from the start.")
            p.sections["trained"] = "\n".join(lines)
            needs = []
            for bc in getattr(p, "build_cmds", []):
                needs += [x for x in self.needs_lines(bc, "buildings") if x not in needs]
            p.sections["needs"] = "\n".join(needs) or "- Nothing beyond the cost."
            leads = []
            trains = []
            for cname in b.offered():
                if low(cname).startswith("spawn "):
                    q = self.page_of("CHARACTER", cname[6:])
                    if q and q.link("buildings") not in trains:
                        trains.append(q.link("buildings"))
            if trains:
                leads.append("- **Trains:** " + ", ".join(trains))
            res = []
            for cname in b.offered():
                q = self.upgrades.get(low(cname))
                if q and q.link("buildings") not in res:
                    res.append(q.link("buildings"))
            if res:
                leads.append("- **Research here:** " + ", ".join(res))
            unl = []
            names = [b.name] + [b.name + " Foundation"] + [a[1] for a in ALIASES if ALIASES[a] == p.key]
            for n in names:
                for q in self.unlocks_of("built", n):
                    if q is not p and q.link("buildings") not in unl:
                        unl.append(q.link("buildings"))
            if unl:
                leads.append("- **Building it unlocks:** " + ", ".join(unl))
            conv = []
            for r in b.reactions:
                for cls, n in d.made_by_actions(d.actions(("REACTION",), r, set())):
                    q = self.bp_page(n)
                    if n in ("Cart", "Militia", "Peasant") or (cls == "BUILDING" and q and q.folder == "buildings"
                                                               and BUILDING_GROUP_OF.get(q.key) != "centre"):
                        continue
                    if q and q is not p and q.link("buildings") not in conv:
                        conv.append(q.link("buildings"))
            if conv:
                leads.append("- **Finishing it turns into or calls up:** " + ", ".join(conv))
            p.sections["leads"] = "\n".join(leads) or "- Nothing new: it trains no units and offers no research."
            cl = self.command_links(p, "buildings")
            p.sections["commands"] = ("- " + "\n- ".join(cl)) if cl else "- None of its own."
            self.static_building(p)
        for lc, p in self.upgrades.items():
            cost, time, sets, conv = d.research(lc)
            at = self.who_links(self.offerers.get(lc, []), "upgrades")
            p.sections["trained"] = "- **Researched at:** " + (", ".join(at) or "-")
            p.sections["needs"] = "\n".join(self.needs_lines(lc, "upgrades")) or "- Nothing beyond the building and the cost."
            leads = []
            unl = [q.link("upgrades") for q in self.unlocks_of("research", d.commands[lc]["name"]) if q is not p]
            if unl:
                leads.append("- **Unlocks:** " + ", ".join(dict.fromkeys(unl)))
            cv = []
            for cls, n in conv:
                q = self.bp_page(n)
                if q and q.link("upgrades") not in cv:
                    cv.append(q.link("upgrades"))
            if cv:
                leads.append("- **Turns units or buildings into, or calls up:** " + ", ".join(cv))
            p.sections["leads"] = "\n".join(leads) or "- No new buttons: it improves what you already have."
            p.sections["commands"] = "- " + self.cmd_button_text(lc)
            p.static = [("Cost", cost_text(cost))] if cost else []
            if time:
                p.static.append(("Research time", fmt(time) + " s"))
            p.sets = sets
        for lc, p in self.cmds.items():
            c = d.commands[lc]
            who = self.who_links([k for k in self.offerers.get(lc, []) if self.can_use(d.bp[k], c["name"])], "commands")
            p.sections["trained"] = "- **Offered by:** " + (", ".join(who) or "-")
            units = [w for w in who if "../units/" in w]
            if len(units) > 15:
                allu = sorted((q.link("commands") for q in self.units.values()), key=str.lower)
                without = [x for x in allu if x not in units]
                rest = [w for w in who if w not in units]
                txt = ", ".join(units)
                if len(without) <= 15:
                    txt = "every unit except " + (", ".join(without) if without else "none")
                if rest:
                    txt += "; and " + ", ".join(rest)
                p.sections["trained"] = "- **Offered by:** " + txt
            if c["cursor_only"]:
                p.sections["trained"] += "\n- " + CURSOR_NOTES.get(c["name"], CURSOR_NOTE)
            p.sections["needs"] = "\n".join(self.needs_lines(lc, "commands")) or "- Nothing: it is there whenever the unit or building is."
            p.sections["leads"] = ""
            p.sections["commands"] = ""
            cost = {}
            for a in d.command_actions(lc):
                if a[0] == "DECREASE_ITEM" and len(a) >= 4 and a[1] in RESOURCES and a[2] == "FINDER_PLAYER":
                    v = value_of(d.gs, a[3:])
                    if v:
                        cost[a[1]] = cost.get(a[1], 0) + v
            if cost:
                p.static = [("Cost", cost_text(cost))]

    def cmd_button_text(self, lc):
        return "The research is its own button: `" + self.d.commands[lc]["name"] + "`."

    def static_unit(self, p):
        b = p.bp
        s = [("Cost", cost_text(b.items))] if cost_text(b.items) else []
        found = None
        for bc in getattr(p, "build_cmds", []):
            c = self.d.command(bc)
            if c and c["stampdown"]:
                found = self.d.blueprint("BUILDING", c["stampdown"][1])
        if found:
            s = [("Cost", cost_text(found.items))] if cost_text(found.items) else []
            if found.item("Construction Time"):
                s.append(("Build time", fmt(found.item("Construction Time")) + " s"))
        if b.item("Food Consumption"):
            s.append(("Food upkeep", fmt(b.item("Food Consumption"))))
        hp = b.item("Hit Point Capacity", "Hit Points")
        if hp:
            s.append(("Hit points", fmt(hp)))
        if "Armour" in b.items:
            arm = fmt(b.items["Armour"])
            dirs = [b.items.get(k + " Armour") for k in ("Front", "Left", "Rear")]
            if all(x is not None for x in dirs):
                arm += f" (front {fmt(dirs[0])}, sides {fmt(dirs[1])}, rear {fmt(dirs[2])})"
            s.append(("Armour", arm))
        if b.item("H2H Damage"):
            v = fmt(b.item("H2H Damage"))
            if b.item("Attack Time"):
                v += f" every {fmt(b.item('Attack Time'))} s"
            s.append(("Melee attack", v))
        if b.item("Missile Damage"):
            v = fmt(b.item("Missile Damage"))
            if b.item("Missile Attack Time"):
                v += f" every {fmt(b.item('Missile Attack Time'))} s"
            if b.item("Sight Range"):
                reach = b.item("Sight Range") * (b.item("My Missile Sight Range Modifier") or 5)
                v += f", range {fmt(round(reach))} m"
            s.append(("Missile attack", v))
        if b.item("Missile Structural Integrity Damage") >= 1:
            s.append(("Damage to buildings", fmt(b.item("Missile Structural Integrity Damage")) + " per missile"))
        elif b.item("Structural Integrity Damage") >= 1:
            s.append(("Damage to buildings", fmt(b.item("Structural Integrity Damage")) + " per blow"))
        if b.item("Base Movement Speed"):
            s.append(("Speed", fmt(b.item("Base Movement Speed"))))
        if b.item("Sight Range"):
            s.append(("Sight", f"{fmt(b.item('Sight Range'))} ({fmt(round(b.item('Sight Range') * 5))} m)"))
        p.static = s

    def static_building(self, p):
        b = p.bp
        found = None
        for bc in getattr(p, "build_cmds", []):
            c = self.d.command(bc)
            if c and c["stampdown"]:
                found = self.d.blueprint("BUILDING", c["stampdown"][1])
                break
        s = []
        if found and cost_text(found.items):
            s.append(("Cost", cost_text(found.items)))
            if found.item("Construction Time"):
                s.append(("Build time", fmt(found.item("Construction Time")) + " s"))
        si = b.item("Structural Integrity Capacity", "Structural Integrity", "Hit Point Capacity", "Hit Points")
        if si:
            s.append(("Strength (structural integrity)", fmt(si)))
        if "Armour" in b.items:
            s.append(("Armour", fmt(b.items["Armour"])))
        if b.item("Missile Damage") and "Fort Attack On Sight" in b.reactions:
            s.append(("Missile attack", fmt(b.item("Missile Damage")) + " per arrow"))
        rep = {r: b.item(r + " Repair Cost") for r in RESOURCES}
        if any(rep.values()):
            s.append(("Repair cost", cost_text(rep)))
        if b.item("Sight Range"):
            s.append(("Sight", fmt(b.item("Sight Range"))))
        p.static = s


# ---------------------------------------------------------------------------------------------
# Writing pages
# ---------------------------------------------------------------------------------------------

SECTION_TITLES = [("role", "Role"), ("trained", None), ("needs", "Needs"), ("leads", "Leads to"),
                  ("commands", "Commands"), ("tips", "Tips")]
TRAINED_TITLE = {"units": "Trained at / Built by", "buildings": "Built by", "upgrades": "Researched at",
                 "commands": "Offered by"}
PLACEHOLDER = "*To be written.*"


# The fixed phrases of the generated parts, for translations (tools/help/strings/<lang>.json).
# "{0}" stands for a link. Matched whole (not inside a longer word), longest first.
SENTENCES = [
    "Trained at:", "Built by:", "Comes from:", "Builds:", "Can turn into or call up:", "Trains:",
    "Research here:", "Building it unlocks:", "Finishing it turns into or calls up:", "Researched at:",
    "Unlocks:", "Turns units or buildings into, or calls up:", "Offered by:",
    "(the engine is put up on the spot like a building, then rolls out)",
    "Not trained in a normal game: it comes with the level or the campaign.",
    "Nothing beyond the building that trains it and the cost.",
    "Nothing: it unlocks no building, unit or research.", "Only the basic orders.",
    "Not built directly: it is on the map from the start.", "Nothing beyond the cost.",
    "Nothing new: it trains no units and offers no research.", "None of its own.",
    "Nothing beyond the building and the cost.", "No new buttons: it improves what you already have.",
    "Nothing: it is there whenever the unit or building is.", "The research is its own button:",
    "(set by the level or a script, not by anything you build)", "(not offered in a normal game)",
    "building a", "the research", "the command", "every unit except", "(any level)",
]
LABELS = [
    "Cost", "Build time", "Train time", "Food upkeep", "Hit points", "Armour", "Melee attack",
    "Missile attack", "Damage to buildings", "Speed", "Sight", "Strength (structural integrity)",
    "Repair cost", "Research time", "Name", "Trained at / built by", "Built by", "Research", "Where",
    "Command", "Button",
]
WORDS = ["food", "wood", "gold", "stone", "every", "s", "per missile", "per blow", "range", "front",
         "sides", "rear", "yes", "no", "units", "buildings", "and", "or", "none"]

LANG = {"code": "en", "phrases": {}}   # set by --lang


def protect_links(text):
    links = []

    def keep(m):
        links.append(m.group(0))
        return "{0}"
    return re.sub(r"!?\[[^\]]*\]\([^)]*\)", keep, text), links


def phrase_list():
    """Every phrase a translation of the generated parts can give, English first."""
    out = list(SENTENCES)
    out += [protect_links(v)[0] for v in ICON_NOTES.values()]
    out += [protect_links(v)[0] for v in CURSOR_NOTES.values()] + [protect_links(CURSOR_NOTE)[0]]
    out += [t for _, t in UNIT_GROUPS + BUILDING_GROUPS + UPGRADE_GROUPS + COMMAND_GROUPS]
    out += [t for t, _ in INDEX_FRONT.values()] + [PLACEHOLDER]
    out += LABELS + WORDS
    return list(dict.fromkeys(out))


def translate(md):
    """Generated Markdown in LANG's phrases; links, code and the facts block stay as they are."""
    table = {k: v for k, v in LANG["phrases"].items() if v}
    if not table and LANG.get("decimal", ".") == ".":
        return md
    table = table or {"\x00": "\x00"}
    out, pos = [], 0
    for c in re.finditer(r"```.*?```|`[^`]*`|<!--.*?-->", md, re.S):
        out.append(_translate_text(md[pos:c.start()], table))
        out.append(c.group(0))
        pos = c.end()
    out.append(_translate_text(md[pos:], table))
    return "".join(out)


def _translate_text(text, table):
    text, links = protect_links(text)
    keys = sorted(table, key=len, reverse=True)
    rx = re.compile(r"(?<![\w{])(" + "|".join(re.escape(k) for k in keys) + r")(?![\w}])")
    text = rx.sub(lambda m: table[m.group(1)], text)
    if LANG.get("decimal", ".") != ".":          # 2.5 -> 2,5
        text = re.sub(r"(?<=\d)\.(?=\d)", LANG["decimal"], text)
    it = iter(links)
    return re.sub(r"\{0\}", lambda m: next(it, "{0}"), text)


def static_table(rows):
    if not rows:
        return ""
    out = ["| | |", "|---|---|"]
    out += [f"| {k} | {v} |" for k, v in rows]
    return "\n".join(out)


def gen(part, body):
    return f"<!-- gen:{part} -->\n{body.strip()}\n<!-- /gen:{part} -->"


def facts_part(p):
    body = []
    if p.facts:
        body.append("```facts\nblueprint: " + p.facts + "\n```")
    if p.static:
        body.append("<!-- facts:static -->\n" + static_table(p.static) + "\n<!-- /facts:static -->")
    return gen("facts", "\n\n".join(body)) if body else ""


def icon_line(p):
    if p.icon and " " not in p.icon:
        return f"![{p.title}](game:{p.icon.replace(chr(92), '/')})"
    return ""


def load_language(path):
    """KEY "text" lines of the game's Language.txt (ANSI or UTF-8)."""
    raw = open(path, "rb").read()
    try:
        text = raw.decode("utf-8")
    except UnicodeDecodeError:
        text = raw.decode("cp1252")
    lang = {}
    for line in text.splitlines():
        m = re.match(r'^\s*([A-Za-z0-9_]+)\s+"(.*)"\s*$', line)
        if m:
            lang[m.group(1).lower()] = m.group(2)
    return lang


def game_name(lang, key):
    """The first line of a language entry: the name the player sees ("Archer\\nFood - %d..." -> Archer)."""
    v = lang.get((key or "").lower())
    if not v:
        return ""
    v = v.split("\\n")[0].strip().rstrip(".").strip()
    return "" if "%" in v else v


def tidy_game_name(v):
    """A game name as a page title: colour codes, jokes and shouting taken off."""
    v = re.sub(r"\\c", "", v)
    v = re.split(r", heh", v)[0]
    v = re.sub(r"^Set ", "", v)
    return v.strip().rstrip("!?").strip()


def apply_game_titles(cat, data, lang):
    """Titles as the game shows them (see the module text). Returns {folder: {old title: new title}}.

    - a name of more than five words is a sentence (a hint such as "Locates all your Bishops,
      ..."), not a name: the page keeps its own title;
    - two pages of one folder with the same game name ("Close", "Fire") get what tells them
      apart in brackets: "Close (Stockade Gatehouse)", using the game's names for it.
    """
    renamed = {}
    folders = [list(cat.units.values()), list(cat.buildings.values()),
               list(cat.upgrades.values()), list(cat.cmds.values())]
    for pages in folders:
        names = {}
        for p in pages:
            if p.folder in ("units", "buildings"):
                name = game_name(lang, p.bp.tooltip if p.bp else "")
                if not name:
                    verb = "Spawn " if p.folder == "units" else "Build "
                    c = data.command(verb + p.key)
                    name = game_name(lang, c["hint"]) if c else ""
            else:
                c = data.command(getattr(p, "cmd", p.key))
                name = game_name(lang, c["hint"]) if c else ""
            name = tidy_game_name(name) if name else ""
            if p.folder == "upgrades":         # the button says "Research Chivalry": the upgrade is Chivalry
                name = re.sub(r"^Research ", "", name)
            if name and len(name.split()) <= 5:
                names[p] = name
        count = {}
        for n in names.values():
            count[n.lower()] = count.get(n.lower(), 0) + 1
        for p, n in names.items():
            if count[n.lower()] > 1:
                rest = re.sub(r"\b" + re.escape(n) + r"\b", "", p.title, flags=re.I).strip(" -")
                for o, nw in {**renamed.get("units", {}), **renamed.get("buildings", {})}.items():   # renamed first
                    rest = re.sub(r"\b" + re.escape(o) + r"\b", nw, rest)
                if not rest or rest.lower() == n.lower():
                    continue                     # nothing tells them apart: keep the page's own title
                n = f"{n} ({rest})"
            if n != p.title:
                renamed.setdefault(p.folder, {})[p.title] = n
                p.title = n
            p.game_title = True
    return renamed


def retitle(text, title):
    """An existing page's front-matter title and top heading set to the game's name."""
    text = re.sub(r"^(---\n(?:.*\n)*?)title:.*\n", lambda m: m.group(1) + "title: " + title + "\n", text, count=1)
    return re.sub(r"^# .*$", lambda m: "# " + title, text, count=1, flags=re.M)


def new_page(p, order):
    ids = ", ".join(p.ids)
    out = [f"---\ntitle: {p.title}\nids: [{ids}]\norder: {order}\n---", f"# {p.title}"]
    il = icon_line(p)
    if il:
        out.append(gen("icon", il))
    fp = facts_part(p)
    if fp:
        out.append(fp)
    for key, title in SECTION_TITLES:
        if key == "trained":
            title = TRAINED_TITLE[p.folder]
        if key in ("leads", "commands") and p.folder == "commands":
            continue
        if key in ("role", "tips"):
            out.append(f"## {title}\n\n{PLACEHOLDER}")
        else:
            out.append(f"## {title}\n\n" + gen(key, p.sections.get(key, "") or "-"))
    return "\n\n".join(out) + "\n"


def update_page(text, p):
    """Replaces the gen parts of an existing page; adds missing ids to its front matter."""
    parts = {"facts": facts_part(p), "icon": gen("icon", icon_line(p)) if icon_line(p) else ""}
    for key in GEN_KEYS:
        if key in p.sections:
            parts[key] = gen(key, p.sections[key] or "-")
    if LANG["code"] != "en":
        parts = {k: translate(v) for k, v in parts.items()}
    for key, new in parts.items():
        pat = re.compile(r"<!-- gen:" + key + r" -->.*?<!-- /gen:" + key + r" -->", re.S)
        if new and pat.search(text):
            text = pat.sub(lambda m: new, text, count=1)
    m = re.match(r"---\n(.*?)\n---", text, re.S)
    if m and LANG["code"] == "en":   # a translation takes its ids from the English page
        fm = m.group(1)
        im = re.search(r"^ids:\s*\[(.*)\]\s*$", fm, re.M)
        if im:
            have = [x.strip() for x in im.group(1).split(",") if x.strip()]
            add = [i for i in p.ids if i not in have]
            if add:
                fm2 = fm[:im.start()] + "ids: [" + ", ".join(have + add) + "]" + fm[im.end():]
                text = "---\n" + fm2 + "\n---" + text[m.end():]
    return text


GEN_KEYS = ("trained", "needs", "leads", "commands")


def write(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)


def group_of(folder, p, cat):
    if folder == "units":
        if p.key in UNIT_GROUP_OF:
            return UNIT_GROUP_OF[p.key]
        it = p.bp.items
        if it.get("I am a boat") or "Water" in p.bp.traverse and "Grass" not in p.bp.traverse:
            return "vessels"
        if it.get("I'm an SW unit") or it.get("I'm an H2H SW unit"):
            return "siege"
        if it.get("I'm an HC unit") or it.get("I'm an LC unit"):
            return "missile" if it.get("Missile Damage") and not it.get("I'm an HC unit") and "Javelin" in p.key else "cavalry"
        if it.get("Missile Damage"):
            return "missile"
        return "infantry"
    if folder == "buildings":
        return BUILDING_GROUP_OF.get(p.key, "other")
    if folder == "upgrades":
        k = p.key
        if re.search(r"Infantry", k):
            return "infantry"
        if re.search(r"Cavalry|Horse|Stirrup|Warhorse|Chivalry", k):
            return "cavalry"
        if re.search(r"Arrow|Gunpowder|Grapeshot|Crossbow|Greek Fire|Rocketry|Bayonet", k):
            return "missile"
        if re.search(r"Wall|Fort|Tower|Masonry", k):
            return "defence"
        if re.search(r"Trebuchet|Infernal|Explosive|Logistics|Surgery|Telescope|Reconnaissance|Espionage", k):
            return "siege"
        if re.search(r"Sawmill|Windmill|Plough|Harness|Crop", k):
            return "economy"
        if re.search(r"Sacrifice|Festival|Crusade|Malleus|Holy|Charm|Ghost|Undead", k):
            return "religion"
        return "other"
    k = p.key
    c = cat.d.commands[low(k)]
    if re.search(r"Formation", k):
        return "formation"
    if re.search(r"^(Move|Attack|Halt|Guard|Follow|Aggression|Passivity|Hold|Fire|Run Away|Skirmish|Capture|Patrol|Charge)", k):
        return "basic"
    if re.search(r"Garrison|Gatehouse|Bell|Recall|Conscript|Demob|Fort Attack|Repair|Finish|Unload|Board|Pick up Passenger|Enter|Docking", k):
        return "buildings"
    if re.search(r"Inflict|Bless|Curse|Pray|Exorcism|Banish|Holy Fire|Burn|Possess|Charm|Entangle|Barkskin|Greengrow|Awaken|Summon|Dark Portal|Ghost|Heal|Make Baresark|Desecrate", k):
        return "magic"
    if re.search(r"Farm|Mine|Chop|Shop|Deliver|Supply|Sack|Cart|Transport|Convert Goods|Take Cart|Train|Resupply|Clear Supply|Cog", k):
        return "work"
    return "special"


def short_where(where):
    """The Manor's later levels (Keep, Castle, Palace of each path) folded into "Manor (any level)"."""
    if "[Manor](" in where:
        where = re.sub(r"\[(Keep|Castle|Palace) \([^)]*\)\]\([^)]*\)(, )?", "", where).strip().rstrip(",")
        where = re.sub(r"(\[Manor\]\([^)]*\))", r"\1 (any level)", where)
    return where


def index_page(folder, pages, cat):
    groups = {"units": UNIT_GROUPS, "buildings": BUILDING_GROUPS, "upgrades": UPGRADE_GROUPS,
              "commands": COMMAND_GROUPS}[folder]
    by = {}
    for p in pages:
        by.setdefault(group_of(folder, p, cat), []).append(p)
    out = []
    for key, title in groups:
        ps = sorted(by.get(key, []), key=lambda x: x.title.lower())
        if not ps:
            continue
        out.append(f"## {title}\n")
        if folder in ("units", "buildings"):
            out.append("| Name | Cost | Trained at / built by |" if folder == "units" else "| Name | Cost | Built by |")
            out.append("|---|---|---|")
            for p in ps:
                cost = dict(p.static).get("Cost", "")
                where = p.sections.get("trained", "").split("\n")[0]
                where = re.sub(r"^- \*\*[^*]+:\*\* ", "", where).replace("- ", "")
                where = re.sub(r"\(\.\./", "(../", where)
                where = re.sub(r" \(the engine.*\)$", "", where)
                where = where.replace("](", "](../" + folder + "/").replace("](../" + folder + "/../", "](../")
                out.append(f"| {p.link(folder)} | {cost} | {short_where(where)} |")
        elif folder == "upgrades":
            out.append("| Research | Cost | Where |")
            out.append("|---|---|---|")
            for p in ps:
                cost = dict(p.static).get("Cost", "")
                where = re.sub(r"^- \*\*[^*]+:\*\* ", "", p.sections.get("trained", ""))
                where = where.replace("](", "](../" + folder + "/").replace("](../" + folder + "/../", "](../")
                out.append(f"| {p.link(folder)} | {cost} | {where} |")
        else:
            out.append("| Command | Button | Offered by |")
            out.append("|---|---|---|")
            for p in ps:
                where = p.sections.get("trained", "").split("\n")[0]
                where = re.sub(r"^- \*\*[^*]+:\*\* ", "", where)
                where = short_where(where.replace("](", "](../" + folder + "/").replace("](../" + folder + "/../", "](../"))
                ws = where.split(", ")
                if len(ws) > 6:
                    nb = sum(1 for w in ws if "../buildings/" in w)
                    nu = len(ws) - nb
                    where = " and ".join(x for x in (f"{nu} units" if nu else "", f"{nb} buildings" if nb else "") if x)
                how = "no" if cat.d.commands[low(p.key)]["cursor_only"] else "yes"
                out.append(f"| {p.link(folder)} | {how} | {where} |")
        out.append("")
    return "\n".join(out)


INDEX_FRONT = {
    "units": ("Units", 50), "buildings": ("Buildings", 51), "upgrades": ("Upgrades and research", 52),
    "commands": ("Commands", 53),
}


def write_index(folder, pages, cat, force):
    path = os.path.join(HELP, folder, "index.md")
    body = gen("index", translate(index_page(folder, pages, cat)))
    if LANG["code"] != "en" and not os.path.exists(path):
        return
    if os.path.exists(path) and not force:
        text = open(path, encoding="utf-8").read()
        pat = re.compile(r"<!-- gen:index -->.*?<!-- /gen:index -->", re.S)
        if pat.search(text):
            write(path, pat.sub(lambda m: body, text, count=1))
            return
    title, order = INDEX_FRONT[folder]
    write(path, f"---\ntitle: {title}\nids: [{folder[:-1]}]\norder: {order}\n---\n\n# {title}\n\n{PLACEHOLDER}\n\n{body}\n")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--gameset", default=DEFAULT_GS)
    ap.add_argument("--force", action="store_true")
    ap.add_argument("--only", default="")
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--dump", default="")
    ap.add_argument("--language", default="", help="the game's Languages/Language.txt: titles as the player sees them")
    ap.add_argument("--renames", default="", help="with --language: write {old title: new title} as JSON")
    ap.add_argument("--lang", default="en", help="the help language folder to work on (redata/help/<lang>)")
    ap.add_argument("--phrases", action="store_true", help="write tools/help/strings/template.json and stop")
    a = ap.parse_args()
    global HELP
    if a.phrases:
        path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "strings", "template.json")
        os.makedirs(os.path.dirname(path), exist_ok=True)
        json.dump({"language": "", "decimal": ".", "phrases": {k: "" for k in phrase_list()}}, open(path, "w", encoding="utf-8"),
                  indent=1, ensure_ascii=False)
        print(f"{len(phrase_list())} phrases -> {path}")
        return
    if a.lang != "en":
        HELP = os.path.join(os.path.dirname(HELP), a.lang)
        if not os.path.isdir(HELP):
            sys.exit(f"no help folder for '{a.lang}' yet: start one with tools/help/translation.py new {a.lang}")
        sp = os.path.join(os.path.dirname(os.path.abspath(__file__)), "strings", a.lang + ".json")
        LANG["code"] = a.lang
        if os.path.exists(sp):
            strings = json.load(open(sp, encoding="utf-8"))
            LANG["phrases"] = strings.get("phrases", {})
            LANG["decimal"] = strings.get("decimal", ".") or "."
    gs = GameSet(a.gameset)
    data = Data(gs)
    cat = Catalogue(data)
    folders = {"units": cat.units, "buildings": cat.buildings, "upgrades": cat.upgrades, "commands": cat.cmds}
    if not a.language:
        # no Language.txt: the pages' own titles (the game's names from an earlier --language run)
        # are kept, and the generated links use them
        for f, ps in folders.items():
            for p in ps.values():
                path = os.path.join(HELP, p.folder, p.slug + ".md")
                if os.path.exists(path):
                    t = re.search(r"^title:\s*(.*)$", open(path, encoding="utf-8").read(), re.M)
                    if t and t.group(1).strip():
                        p.title = t.group(1).strip()
        cat._fill()
    if a.language:
        renamed = apply_game_titles(cat, data, load_language(a.language))
        cat._fill()   # the generated sections link by title: fill them again with the new ones
        print(f"{sum(len(v) for v in renamed.values())} titles taken from the game's names")
        if a.renames:
            json.dump(renamed, open(a.renames, "w", encoding="utf-8"), indent=1, ensure_ascii=False)
    only = set(a.only.split(",")) if a.only else set(folders)
    if a.list:
        for f, ps in folders.items():
            print(f"== {f}: {len(ps)}")
            for p in sorted(ps.values(), key=lambda x: x.slug):
                print(f"  {p.slug:45} {p.title:35} {', '.join(p.ids)}")
        return
    if a.dump:
        out = {}
        for f, ps in folders.items():
            out[f] = {p.slug: {"title": p.title, "key": p.key, "ids": p.ids, "static": p.static,
                               "sections": p.sections, "group": group_of(f, p, cat),
                               "items": (p.bp.items if p.bp else {}), "sets": getattr(p, "sets", []),
                               "offered": (p.bp.offered() if p.bp else [])}
                      for p in ps.values()}
        json.dump(out, open(a.dump, "w", encoding="utf-8"), indent=1)
        return
    counts = {}
    for f, ps in folders.items():
        if f not in only:
            continue
        n_new = n_upd = 0
        for p in sorted(ps.values(), key=lambda x: x.slug):
            path = os.path.join(HELP, p.folder, p.slug + ".md")
            if a.lang != "en" and not os.path.exists(path):
                continue                 # translations are started by hand (translation.py start)
            if os.path.exists(path) and not a.force:
                old = open(path, encoding="utf-8").read()
                new = update_page(old, p)
                if getattr(p, "game_title", False):
                    new = retitle(new, p.title)
                if new != old:
                    write(path, new)
                n_upd += 1
            else:
                write(path, new_page(p, 10))
                n_new += 1
        write_index(f, list(ps.values()), cat, a.force)
        counts[f] = (len(ps), n_new, n_upd)
    for f, (n, nn, nu) in counts.items():
        print(f"{f}: {n} pages ({nn} new, {nu} kept and updated)")


if __name__ == "__main__":
    main()
