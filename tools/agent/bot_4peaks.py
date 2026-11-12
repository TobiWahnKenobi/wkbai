"""An economy-and-army bot for 4 Peaks against an AI General: python bot_4peaks.py [--minutes 40]

Plays through the agent's input only and decides from what the player sees (observe, the
screen's buttons, the refusal box while placing). The probe is used only for the record (both
sides' strength each minute), never for a decision.

  economy  the first peasants build a village well outside the city (rural buildings away from
           the walls and the gate), then farms beside it; the village spawns farmers; idle
           peasants chop wood (the activity panel's idle bar)
  city     a stone Barracks joined to the city's walls (the game's own preview says where)
  army     spearmen and archers from the Barracks while there is food beyond what the farmers
           need; all soldiers in control group 1, gathered at home; an attack once 18 are ready,
           new soldiers joining the group; targets: soldiers, peasants, the Manor, buildings
  victory  the game's rule (Game Endings.cpp): every character and building of the AI gone

Licensed under the GNU General Public License 3 (as wkbai).
"""
import argparse
import json
import math
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wkbai_agent import Game, bmp_to_png  # noqa: E402

SOLDIER_HINTS = ("Militia", "Man at Arms", "Archer", "Crossbow", "Knight", "Pike", "Sword", "Spear", "Lancer", "Cavalry", "Guard", "Hero", "Lord", "Gunner")
WALLS = ("Stockade", "Wall", "Gatehouse", "Palisade")


def dist(a, b):
    return math.hypot(a[0] - b[0], a[1] - b[1])


def safe(sx, sy):
    """A screen point not under the HUD (the top bar, the command circle, the minimap)"""
    return 70 < sy < 600 and 20 < sx < 1260 and not (sx < 345 and sy > 380) and not (sx > 930 and sy > 380)


def is_soldier(x):
    return x["kind"] == "character" and any(h in x["type"] for h in SOLDIER_HINTS)


class Bot:
    def __init__(self, a, say):
        self.a = a
        self.say = say
        self.stale = set()
        self.sweep = []

    # -- seeing
    def own(self, o, kind=None, type_=None):
        return [x for x in o["objects"] if x.get("own") and (kind is None or x["kind"] == kind) and (type_ is None or x["type"] == type_)]

    def enemies(self, o, remembered=False):
        me = o["player"]
        return [x for x in o["objects"] if not x.get("own") and x["player"] not in (0, me, 1026) and (remembered or not x.get("remembered"))]

    # -- hands
    def select(self, x):
        """One object: an aimed click, else a click on (or beside) its screen point"""
        a = self.a
        if a.request("clickobject", object=x["id"]).get("ok"):
            return True
        x = next((y for y in a.observe()["objects"] if y["id"] == x["id"]), x)
        if not x.get("screen"):
            return False
        for ox, oy in ((0, 0), (10, -10), (-10, -10), (0, -20), (15, 5), (-15, 5)):
            if not safe(x["screen"][0] + ox, x["screen"][1] + oy):
                continue
            a.click(x["screen"][0] + ox, x["screen"][1] + oy); a.wait(3)
            if x["id"] in [s["id"] for s in a.observe()["selection"]]:
                return True
        return False

    def box(self, pts, add=False, margin=26):
        a = self.a
        x0 = max(5, min(p[0] for p in pts) - margin); y0 = max(72, min(p[1] for p in pts) - margin)
        x1 = min(1275, max(p[0] for p in pts) + margin); y1 = min(598, max(p[1] for p in pts) + margin)
        if add:
            a.key("Left Shift", True)
        a.drag((x0, y0), (x1, y1))
        if add:
            a.key("Left Shift", False)

    def place(self, tab, command, candidates):
        """The selection builds command at the first candidate map point the preview accepts
        (the refusal box empty with the cursor there); the new building or None"""
        a = self.a
        def pick():
            a.click_ui("tab:" + tab); a.wait(3)
            if not a.request("clickui", element="cmd:" + command).get("ok"):
                return False
            a.wait(3)
            return True
        if not pick():
            return None
        if candidates:
            self.look(candidates[0], 6)  # (the camera where the spots are)
        for x, z in candidates:
            try:
                sx, sy = a.point(x, z)
            except Exception:
                continue
            if not safe(sx, sy):
                continue
            a.move(sx, sy); a.wait(3)
            p = a.observe().get("placing")
            if not p:
                if not pick():
                    return None
                continue
            if p.get("refusal"):
                continue
            known = {o["id"] for o in a.observe()["objects"]}
            a.click(sx, sy, "right"); a.wait(15)
            new = [o for o in a.observe()["objects"] if o["id"] not in known and o["kind"] == "building" and not any(w in o["type"] for w in WALLS)]
            if new:
                return new[0]
            if not a.observe().get("placing") and not pick():
                return None
        if a.observe().get("placing"):
            a.click(640, 300); a.wait(3)
        return None

    def look(self, p, frames=6):
        self.a.look_at(*p); self.a.wait(frames)

    # -- the economy
    def rural_spots(self, centre, manor, gate, rmin, rmax, away=70):
        """Map points around centre, rmin..rmax away, at least `away` from the Manor and not in
        front of the gate (where the city's road is)"""
        spots = []
        for r in range(rmin, rmax + 1, 8):
            for ang in range(0, 360, 20):
                p = [centre[0] + r * math.cos(math.radians(ang)), centre[1] + r * math.sin(math.radians(ang))]
                if dist(p, manor) < away:
                    continue
                if gate and dist(p, gate) < 45:
                    continue
                spots.append(p)
        spots.sort(key=lambda p: dist(p, centre))
        return spots

    def peasants_box(self, near):
        a = self.a
        self.look(near, 8)
        pes = [x for x in self.own(a.observe(), "character", "Peasant") if "screen" in x and safe(*x["screen"])]
        if not pes:
            return 0
        self.box([p["screen"] for p in pes])
        return len([s for s in a.observe()["selection"] if s["type"] == "Peasant"])

    def to_wood(self, peasants):
        """These peasants (selected by clicks, Shift for the rest) to the nearest tree"""
        a = self.a
        if not peasants:
            return 0
        self.look(peasants[0]["position"], 5)
        on = [x for x in self.own(a.observe(), "character", "Peasant") if x["id"] in {p["id"] for p in peasants} and "screen" in x and safe(*x["screen"])]
        if not on:
            return 0
        first = True
        for p in on[:10]:
            if a.request("clickobject", object=p["id"], add=not first).get("ok"):
                first = False
        sel = [s for s in a.observe()["selection"] if s["type"] == "Peasant"]
        if not sel:
            return 0
        c = sel[0]["position"]
        return len(sel) if self.right_click_tree_near(c) else 0

    def select_group(self, units):
        """These units selected (clicks, Shift for the rest; the camera to each in turn)"""
        a = self.a
        first = True
        for u in units:
            self.look(u["position"], 3)
            if a.request("clickobject", object=u["id"], add=not first).get("ok"):
                first = False
        return not first

    def find_trees(self, around):
        """Where trees are near a point: the camera panned round it (scenery is only what is on
        the screen), as a player looks for a forest; remembered"""
        a = self.a
        known = getattr(self, "trees", {})
        if not any(dist(p, around) < 120 for p in known.values()):
            for r in (0, 55, 100):
                for ang in (range(0, 360, 60) if r else [0]):
                    self.look([around[0] + r * math.cos(math.radians(ang)), around[1] + r * math.sin(math.radians(ang))], 4)
                    for x in a.observe(scenery=True)["objects"]:
                        if "Tree" in x["type"]:
                            known[x["id"]] = x["position"]
                if any(dist(p, around) < 120 for p in known.values()):
                    break
        self.trees = known
        return sorted(known.items(), key=lambda kv: dist(kv[1], around))

    def right_click_tree_near(self, around):
        """A right-click on a tree near the point (the selection's order, or a rally point)"""
        a = self.a
        found = self.find_trees(around)
        if not found:
            return False
        self.look(found[0][1], 5)
        every = a.observe(scenery=True)["objects"]
        units = {x["id"] for x in every if x["kind"] != "other"}
        trees = [x for x in every if "Tree" in x["type"] and "screen" in x and safe(*x["screen"])]
        for t in sorted(trees, key=lambda x: dist(x["position"], around))[:6]:
            r = a.request("rightclickobject", object=t["id"])
            if not r.get("ok") and r.get("underCursor") and r["underCursor"] not in units:
                r = a.request("rightclickobject", object=r["underCursor"])
            if r.get("ok"):
                return True
        # (gone: a tree remembered there no more)
        self.trees.pop(found[0][0], None)
        return False

    def rally_to_tree(self, building):
        """The building's new units go to the nearest tree (selected, a right-click on a tree:
        new peasants then chop it), as a player sets a rally point"""
        a = self.a
        self.find_trees(building["position"])  # (first: the panning would lose nothing then)
        self.look(building["position"], 5)
        if not self.select(building):
            return False
        return self.right_click_tree_near(building["position"])

    def rally_to(self, building, point):
        """The building's new units gather at a map point (selected, a right-click there)"""
        a = self.a
        self.look(building["position"], 5)
        if not self.select(building):
            return False
        for dx, dz in ((0, 0), (6, 0), (0, 6), (-6, 0), (0, -6), (10, 10)):
            if a.request("rightclickground", x=point[0] + dx, z=point[1] + dz).get("ok"):
                return True
        return False

    def convert(self, peasants):
        """These peasants to militia (clicks, Shift for the rest; the command)"""
        a = self.a
        if not peasants:
            return 0
        self.look(peasants[0]["position"], 5)
        on = [x for x in self.own(a.observe(), "character", "Peasant") if x["id"] in {p["id"] for p in peasants} and "screen" in x and safe(*x["screen"])]
        first = True
        for p in on[:10]:
            if a.request("clickobject", object=p["id"], add=not first).get("ok"):
                first = False
        if first:
            return 0
        n = len([s for s in a.observe()["selection"] if s["type"] == "Peasant"])
        a.click_ui("tab:ECONOMIC"); a.wait(2)
        return n if a.request("clickui", element="cmd:Convert to Militia").get("ok") else 0

    def idle_to_wood(self):
        a = self.a
        if not a.elements("activity:"):
            a.request("clickui", element="minimapButton:activity"); a.wait(5)
        acts = [e["id"] for e in a.elements("activity:") if "dle" in e["label"]]
        if not acts:
            return 0
        a.click_ui(acts[0]); a.wait(3)
        sel = [s for s in a.observe()["selection"] if s["type"] == "Peasant"]
        if not sel:
            return 0
        c = sel[0]["position"]
        self.look(c, 5)
        every = a.observe(scenery=True)["objects"]
        near = sorted([x for x in every if "Tree" in x["type"]], key=lambda x: dist(x["position"], c))
        if near:
            # the camera on the nearest tree (trees at the screen's edge lie under the HUD)
            self.look(near[0]["position"], 5)
            every = a.observe(scenery=True)["objects"]
        units = {x["id"] for x in every if x["kind"] != "other"}
        trees = [x for x in every if "Tree" in x["type"] and "screen" in x and safe(*x["screen"])]
        for t in sorted(trees, key=lambda x: dist(x["position"], c))[:6]:
            r = a.request("rightclickobject", object=t["id"])
            if not r.get("ok") and r.get("underCursor") and r["underCursor"] not in units:
                r = a.request("rightclickobject", object=r["underCursor"])
            if r.get("ok"):
                return len(sel)
        return 0

    def spawn(self, building, command, count):
        """count clicks on a building's spawn command (the building selected first)"""
        a = self.a
        self.look(building["position"], 5)
        if not self.select(building):
            return 0
        n = 0
        for _ in range(count):
            if a.request("clickui", element="cmd:" + command).get("ok"):
                n += 1
            a.wait(2)
        return n

    # -- the army
    def regroup(self, home):
        """Group 1: its members plus every soldier in view at home"""
        a = self.a
        a.key_action("groupSelect1"); a.wait(3)
        had = len(a.observe()["selection"])
        self.look(getattr(self, "gather", home), 6)
        sol = [x for x in self.own(a.observe(), "character") if is_soldier(x) and "screen" in x and safe(*x["screen"])]
        if sol:
            self.box([s["screen"] for s in sol], add=had > 0)
        a.tap("Ctrl+1"); a.wait(3)
        return len(a.observe()["selection"])

    def attack(self, fallback):
        a = self.a
        a.key_action("groupSelect1"); a.wait(3)
        sel = a.observe()["selection"]
        if not sel:
            return "no army"
        centre = [sum(s["position"][0] for s in sel) / len(sel), sum(s["position"][1] for s in sel) / len(sel)]
        self.look(centre, 5)
        o = a.observe()
        foes = self.enemies(o)
        foe_ids = {x["id"] for x in foes}
        def rank(x):
            wall = any(w in x["type"] for w in WALLS)
            maker = x["kind"] == "building" and any(k in x["type"] for k in ("Village", "Manor", "Keep", "Castle", "Palace", "Barracks", "Town", "Hall"))
            order = 0 if is_soldier(x) else 1 if maker else 2 if x["kind"] == "character" else 5 if wall else 3
            return (order, dist(x["position"], centre))
        for f in sorted([x for x in foes if dist(x["position"], centre) < 150], key=rank)[:6]:
            r = a.request("rightclickobject", object=f["id"])
            if r.get("ok"):
                return "attack %s %d (army %d)" % (f["type"], f["id"], len(sel))
            u = r.get("underCursor")
            if u in foe_ids and a.request("rightclickobject", object=u).get("ok"):
                return "attack %d (army %d)" % (u, len(sel))
        for x in self.enemies(o, remembered=True):
            if x.get("remembered") and dist(x["position"], centre) < 50:
                self.stale.add(x["id"])
        rem = [x for x in self.enemies(o, remembered=True) if x.get("remembered") and x["id"] not in self.stale
               and not any(w in x["type"] for w in WALLS)]
        manors = [x for x in rem if x["type"] == "Manor"]
        if manors:
            goal = manors[0]["position"]
        elif rem:
            goal = max(rem, key=lambda x: dist(x["position"], self.home))["position"]
        else:
            while self.sweep and (dist(self.sweep[0], centre) < 45 or dist(self.sweep[0], self.home) < dist(centre, self.home) - 30):
                self.sweep.pop(0)
            goal = self.sweep[0] if self.sweep else fallback
        for dx, dz in ((0, 0), (8, 0), (0, 8), (-8, 0), (0, -8), (12, 12), (-12, 12)):
            if a.request("rightclickground", x=goal[0] + dx, z=goal[1] + dz).get("ok"):
                return "march to %.0f,%.0f (army %d)" % (goal[0], goal[1], len(sel))
        return "no way (army %d)" % len(sel)


def record(a, me):
    sides = {}
    for x in a.probe("objects")["objects"]:
        p = x["player"]
        if not p or p == 1026:
            continue
        s = sides.setdefault("agent" if p == me else "ai", {"soldiers": 0, "peasants": 0, "buildings": 0, "manor": 0})
        if x["type"] == "Manor":
            s["manor"] += 1
        if any(h in x["type"] for h in SOLDIER_HINTS):
            s["soldiers"] += 1
        elif x["type"] in ("Peasant", "Peasant Farmer") or "Peasant" in x["type"]:
            s["peasants"] += 1
        else:
            s["buildings"] += 1
    return sides


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--game-path", default=os.environ.get("WKB_GAME_PATH"))
    ap.add_argument("--work", default=r"C:\Dev\wkbai-agent\bot")
    ap.add_argument("--minutes", type=float, default=40)
    ap.add_argument("--speed", type=int, default=1, help="speed-up key presses (1: 2x)")
    ap.add_argument("--attack-at", type=int, default=25)
    ap.add_argument("--pop", type=int, default=200, help="the lobby's population limit (200 by default)")
    ap.add_argument("--port", type=int, default=7790)
    args = ap.parse_args()
    log = []
    def say(*x):
        line = " ".join(str(v) for v in x)
        print(line, flush=True)
        log.append(line)
    result = {"log": log}
    with Game(game_path=args.game_path, work=args.work, process_name="wkbai-agent-pop%d" % args.pop) as game:
        game.port = args.port
        game.start(skirmish="2 Player 4 Peaks.lvl", slots="H|A|E", settings={"recordGame": 1, "populationLimit": args.pop})
        result["populationLimit"] = args.pop
        a = game.agent()
        bot = Bot(a, say)
        o = a.wait_for_game()
        me = o["player"]
        mapw, mapd = o["map"]
        manor = bot.own(o, type_="Manor")[0]
        home = manor["position"]
        bot.home = home
        gate = next((x["position"] for x in bot.own(o) if "Gatehouse" in x["type"]), None)
        cx, cz = mapw / 2, mapd / 2
        far = [mapw - home[0], mapd - home[1]]
        bot.sweep = [[cx, cz], [(cx + far[0]) / 2 + 80, (cz + far[1]) / 2], [(cx + far[0]) / 2, (cz + far[1]) / 2 + 80],
                     [far[0] + 150, far[1]], [far[0], far[1] - 150], [cx - 150, cz + 150], [cx + 150, cz - 150]]
        bot.sweep = [[min(max(p[0], 40), mapw - 40), min(max(p[1], 40), mapd - 40)] for p in bot.sweep]
        say("start: Manor", home, "gate", gate, "map", [mapw, mapd])
        for _ in range(args.speed):
            a.key_action("speedUp")
        t0 = o["time"]
        # the first peasants: a village well outside the city, towards the map's centre (room to grow)
        towards = [home[0] + (cx - home[0]) * 0.25, home[1] + (cz - home[1]) * 0.25]
        peasants = bot.own(o, "character", "Peasant")
        bot.peasants_box(peasants[0]["position"])
        village = bot.place("ECONOMIC", "Build Village Wood", bot.rural_spots(towards, home, gate, 0, 40, away=75))
        say("village:", village and (village["type"], [round(v) for v in village["position"]]))
        state = {"farms": 0, "barracks": None, "attacking": False, "lastFarm": -999, "lastBarracksTry": -999}
        # (the village's builders are held while it goes up, as every placement's)
        state["busy"] = {p["id"]: 150 for p in peasants} if village else {}  # (the village's builders)
        records = []
        last_record = -60
        # the Manor's first food: peasants for wood (2), the rest kept for farmers and soldiers
        say("Manor's rally point on a tree:", bot.rally_to_tree(manor))
        bot.spawn(manor, "Spawn Peasant", 2)
        while True:
            o = a.observe()
            t = o["time"] - t0
            if t >= args.minutes * 60:
                break
            if t - last_record >= 60:
                last_record = t
                r = record(a, me)
                records.append({"time": round(t), **r, "resources": o["resources"]})
                say("t=%4.0fs" % t, json.dumps(r), {k: round(v) for k, v in o["resources"].items()})
                ai = r.get("ai")
                if not ai or ai["soldiers"] + ai["peasants"] + ai["buildings"] == 0:
                    say("the AI has nothing left: won")
                    result["won"] = True
                    break
                ag = r.get("agent", {})
                if ag.get("soldiers", 0) + ag.get("peasants", 0) == 0 and not ag.get("manor"):
                    say("nothing of ours left: lost")
                    result["won"] = False
                    break
            res = o["resources"]
            vil = next((x for x in bot.own(o) if x["type"] == "Village Wood"), None)
            farms = [x for x in bot.own(o) if "Farm" in x["type"] and x["kind"] == "building"]
            farmers = len([x for x in bot.own(o, "character") if "Farmer" in x["type"]])
            # farms beside the village (one, then more as farmers grow), away from the city
            barracks_any = any(x["type"].startswith("Barracks Stone") for x in bot.own(o))
            want_wood = 100 if (not farms or barracks_any) else 300  # (the barracks' 200 first, after one farm)
            if vil and res["Wood"] >= want_wood and len(farms) < 1 + farmers // 4 and t - state["lastFarm"] > 30:
                state["lastFarm"] = t
                builders = [x for x in bot.own(o, "character", "Peasant") if state["busy"].get(x["id"], -1) < t][:4]
                if builders and bot.select_group(builders):
                    f = bot.place("ECONOMIC", "Build Farm Wood", bot.rural_spots(vil["position"], home, gate, 22, 46, away=75))
                    say("t=%4.0fs farm:" % t, f and [round(v) for v in f["position"]])
                    if f:
                        for b2 in builders:
                            state["busy"][b2["id"]] = t + 100
            # farmers from the village (food for them first)
            if vil and farms and farmers < 5 * len(farms) and res["Food"] >= 60:
                n = bot.spawn(vil, "Spawn Peasant Farmer", min(2, int(res["Food"] // 60)))
                if n:
                    say("t=%4.0fs %d farmers" % (t, n))
                o = a.observe(); res = o["resources"]
            # the Barracks: joined to the city, on the side away from the gate
            if not state["barracks"] and farms and res["Wood"] >= 200 and t - state["lastBarracksTry"] > 40:
                state["lastBarracksTry"] = t
                builders = [x for x in bot.own(o, "character", "Peasant") if state["busy"].get(x["id"], -1) < t][:6]
                if builders and bot.select_group(builders):
                    ring = []
                    for r in (32, 38, 44):
                        for ang in range(0, 360, 15):
                            p = [home[0] + r * math.cos(math.radians(ang)), home[1] + r * math.sin(math.radians(ang))]
                            if not gate or dist(p, gate) > 30:
                                ring.append(p)
                    if gate:
                        ring.sort(key=lambda p: -dist(p, gate))
                    b = bot.place("MILITARY", "Build Barracks Stone", ring)
                    say("t=%4.0fs barracks:" % t, b and (b["type"], [round(v) for v in b["position"]]))
                    if b:
                        for b2 in builders:
                            state["busy"][b2["id"]] = t + 160
                    if any(x["type"].startswith("Barracks Stone") for x in bot.own(a.observe())):
                        state["barracks"] = True
            barracks = next((x for x in bot.own(o) if x["type"] == "Barracks Stone"), None)
            gather = [home[0] + (cx - home[0]) * 0.18, home[1] + (cz - home[1]) * 0.18]
            if barracks and not state.get("barracksRally"):
                state["barracksRally"] = bot.rally_to(barracks, gather)
                say("t=%4.0fs Barracks' rally point at" % t, [round(v) for v in gather], state["barracksRally"])
            bot.gather = gather
            # soldiers: spearmen and archers, keeping food for the next farmers
            keep = 60 if farms and farmers < 5 * len(farms) else 0
            if barracks and res["Food"] - keep >= 60:
                n = int((res["Food"] - keep) // 60)
                k = 0
                count = n if res["Food"] > 600 else max(1, n // 2)
                archers = count // 2 if res["Wood"] >= 20 * (count // 2) else 0
                k += bot.spawn(barracks, "Spawn Spearman", count - archers)
                if archers:
                    k += bot.spawn(barracks, "Spawn Archer", archers)
                if k:
                    say("t=%4.0fs %d soldiers ordered" % (t, k))
            # wood (not while a foundation of ours waits: its builders count as idle on the way)
            foundations = [x for x in bot.own(o) if x["type"] == "Barracks Stone Foundation"]
            w = 0 if foundations else bot.idle_to_wood()
            if w:
                say("t=%4.0fs %d idle peasants to the trees" % (t, w))
            # (builders stay: peasants near a foundation of ours, a wall's apart)
            # (a finished farm keeps "Foundation" in its type: only unfinished ones, below full health)
            plain = [x for x in bot.own(o, "character", "Peasant") if state["busy"].get(x["id"], -1) < t]
            if not foundations and t - state.get("lastWood", -999) > 40 and plain:
                state["lastWood"] = t
                n = bot.to_wood(plain[:10])
                if n:
                    say("t=%4.0fs %d peasants to the trees" % (t, n))
            # more peasants from the Manor while food allows (wood, and militia from the surplus)
            o = a.observe(); res = o["resources"]
            if res["Food"] >= 250 and (not barracks or res["Food"] >= 400):
                k = bot.spawn(manor, "Spawn Peasant", min(3, int((res["Food"] - 150) // 50)))
                if k:
                    say("t=%4.0fs %d peasants from the Manor" % (t, k))
            if not barracks and res["Food"] >= 500 and len(plain) > 12 and t > 600:
                c = bot.convert(plain[10:])
                if c:
                    say("t=%4.0fs %d peasants to militia (no barracks yet)" % (t, c))
            # the army
            size = bot.regroup(home)
            if not state["attacking"] and size >= args.attack_at:
                state["attacking"] = True
                say("t=%4.0fs attack with %d" % (t, size))
            if state["attacking"]:
                say("t=%4.0fs %s" % (t, bot.attack(far)))
            time.sleep(1)
        r = record(a, me)
        records.append({"time": round(a.observe()["time"] - t0), **r})
        result["records"] = records
        result.setdefault("won", None)
        result["shot"] = bmp_to_png(a.screenshot("bot-end"))
        out = os.path.join(args.work, "bot-result.json")
        json.dump(result, open(out, "w"), indent=1)
    print("won:", result["won"])
    print("last records:", json.dumps(records[-2:]))


if __name__ == "__main__":
    main()
