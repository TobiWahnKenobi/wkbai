"""A militia rush on 4 Peaks against an AI General: python bot_rush.py [--minutes 25] [--speed 2]

Plays through the agent's input only and decides from what the player sees (observe, the
screen's buttons). The probe is used only for the record (both sides' strength each minute,
who is left at the end), never for a decision.

  - the Manor spawns peasants while there is food (50 each);
  - peasants become militia ("Convert to Militia", free) and go into control group 1;
  - the army walks to where the enemy must be (the other start of a two-player map: the mirror of
    our own across the map's centre) and attacks what it sees: soldiers first, then peasants,
    then buildings, the Manor last;
  - new militia join group 1 (Shift+box, Ctrl+1).

Licensed under the GNU General Public License 3 (as wkbai).
"""
import argparse
import json
import math
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wkbai_agent import Game, AgentError, bmp_to_png  # noqa: E402

SOLDIER_HINTS = ("Militia", "Man At Arms", "Men At Arms", "Archer", "Crossbow", "Knight", "Pike", "Sword", "Spear", "Lancer", "Cavalry", "Guard", "Hero", "Lord")


def dist(a, b):
    return math.hypot(a[0] - b[0], a[1] - b[1])


class Bot:
    def __init__(self, a, log):
        self.sweep = []
        self.stale = set()
        self.a = a
        self.log = log
        self.say = lambda *x: (print(*x, flush=True), log.append(" ".join(str(v) for v in x)))

    def own(self, o, kind=None, type_=None):
        return [x for x in o["objects"] if x.get("own") and (kind is None or x["kind"] == kind) and (type_ is None or x["type"] == type_)]

    def enemies(self, o):
        me = o["player"]
        return [x for x in o["objects"] if not x.get("own") and x["player"] not in (0, me, 1026) and not x.get("remembered")]

    def remembered(self, o):
        me = o["player"]
        return [x for x in o["objects"] if not x.get("own") and x["player"] not in (0, me, 1026)]

    def click_thing(self, x):
        """Select one object: an aimed click, else a click on its screen point (something in front)"""
        a = self.a
        r = a.request("clickobject", object=x["id"])
        if r.get("ok"):
            return True
        if x.get("screen"):
            a.click(*x["screen"]); a.wait(3)
            return x["id"] in [s["id"] for s in a.observe()["selection"]]
        return False

    def box(self, pts, add=False, margin=26):
        a = self.a
        x0 = max(5, min(p[0] for p in pts) - margin); y0 = max(70, min(p[1] for p in pts) - margin)
        x1 = min(1275, max(p[0] for p in pts) + margin); y1 = min(560, max(p[1] for p in pts) + margin)
        if add:
            a.key("Left Shift", True)
        a.drag((x0, y0), (x1, y1))
        if add:
            a.key("Left Shift", False)

    def place(self, command, tab, near, spots):
        """The selection builds command at the first spot (offsets from near) where it can go"""
        a = self.a
        for dx, dz in spots:
            if not (a.observe().get("placing") or {}).get("type"):
                a.click_ui("tab:" + tab); a.wait(3)
                if not a.request("clickui", element="cmd:" + command).get("ok"):
                    return None
                a.wait(3)
            known = {x["id"] for x in a.observe()["objects"]}
            if a.request("rightclickground", x=near[0] + dx, z=near[1] + dz).get("ok"):
                a.wait(15)
                new = [x for x in a.observe()["objects"] if x["id"] not in known and x["kind"] == "building"]
                if new:
                    return new[0]
        if (a.observe().get("placing") or {}).get("type"):
            a.click(640, 300); a.wait(3)  # (drops the outline)
        return None

    def start_economy(self, o):
        """The four first peasants: a village, then a farm beside it; then wood"""
        a = self.a
        pes = self.own(o, "character", "Peasant")
        self.workers = [p["id"] for p in pes]
        a.look_at(*pes[0]["position"]); a.wait(8)
        on = [x for x in self.own(a.observe(), "character", "Peasant") if "screen" in x]
        self.box([p["screen"] for p in on])
        ring = [(-30, -15), (35, -20), (-30, 10), (30, 10), (0, 30), (-45, 25), (20, 40), (-50, -35), (50, -40)]
        v = self.place("Build Village Wood", "ECONOMIC", pes[0]["position"], ring)
        self.say("village:", v and (v["type"], v["position"]))
        self.village = v
        return v

    def after_village(self, o):
        """The village stands: a farm beside it (the same peasants), farmers from the village"""
        a = self.a
        v = next((x for x in o["objects"] if x.get("own") and x["type"] == "Village Wood"), None)
        if not v:
            return False
        a.look_at(*v["position"]); a.wait(8)
        on = [x for x in self.own(a.observe(), "character", "Peasant") if "screen" in x]
        if on:
            self.box([p["screen"] for p in on])
            ring = [(25, 25), (-25, 25), (25, -25), (-25, -25), (35, 0), (-35, 0), (0, 35), (0, -35), (45, 30), (-45, -30)]
            f = self.place("Build Farm Wood", "ECONOMIC", v["position"], ring)
            self.say("farm:", f and (f["type"], f["position"]))
        self.village = v
        return True

    def village_farmers(self, o, keep_food=0):
        """The village spawns peasant farmers (they work the farm)"""
        a = self.a
        v = next((x for x in o["objects"] if x.get("own") and x["type"] == "Village Wood"), None)
        if not v or o["resources"]["Food"] - keep_food < 60:
            return 0
        a.look_at(*v["position"]); a.wait(5)
        v = next((x for x in a.observe()["objects"] if x["id"] == v["id"]), v)
        if not self.click_thing(v):
            return 0
        n = 0
        for _ in range(2):
            if a.request("clickui", element="cmd:Spawn Peasant Farmer").get("ok"):
                n += 1
            a.wait(2)
        return n

    def idle_to_wood(self):
        """Idle peasants (the activity panel's idle bar) to the nearest tree on the screen"""
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
        a.look_at(*c); a.wait(5)
        every = a.observe(scenery=True)["objects"]
        units = {x["id"] for x in every if x["kind"] != "other"}
        trees = [x for x in every if "Tree" in x["type"] and "screen" in x]
        for t in sorted(trees, key=lambda x: dist(x["position"], c))[:6]:
            r = a.request("rightclickobject", object=t["id"])
            if not r.get("ok") and r.get("underCursor") and r["underCursor"] not in units:
                r = a.request("rightclickobject", object=r["underCursor"])
            if r.get("ok"):
                return len(sel)
        return 0

    def spawn_peasants(self, o, manor, keep_food=0):
        a = self.a
        food = o["resources"]["Food"]
        n = int((food - keep_food) // 50)
        if n <= 0 or not manor:
            return 0
        n = min(n, 10)
        a.look_at(*manor["position"]); a.wait(5)
        m = next((x for x in a.observe()["objects"] if x["id"] == manor["id"]), manor)
        if not self.click_thing(m):
            return 0
        done = 0
        for _ in range(n):
            if a.request("clickui", element="cmd:Spawn Peasant").get("ok"):
                done += 1
            a.wait(2)
        return done

    def convert_and_group(self, manor):
        """Every peasant in view near the Manor becomes militia and joins group 1"""
        a = self.a
        a.look_at(*manor["position"]); a.wait(5)
        o = a.observe()
        keep = set(getattr(self, "workers", []))
        pes = [x for x in self.own(o, "character", "Peasant") if "screen" in x and 70 < x["screen"][1] < 560 and x["id"] not in keep]
        if not pes:
            return 0
        self.box([p["screen"] for p in pes])
        # (the box may catch a worker: Ctrl+click... simpler: keep the workers out by re-selecting)
        sel = a.observe()["selection"]
        if any(s["id"] in keep for s in sel):
            first = True
            for p in pes:
                r = a.request("clickobject", object=p["id"], add=not first)
                if r.get("ok"):
                    first = False
        sel = a.observe()["selection"]
        if not any(s["type"] == "Peasant" for s in sel):
            return 0
        a.request("clickui", element="cmd:Convert to Militia"); a.wait(40)
        self.regroup(manor)
        return len(pes)

    def regroup(self, manor):
        """Group 1 again: its members (1) plus every militia in view at home (Shift+box), Ctrl+1"""
        a = self.a
        a.key_action("groupSelect1"); a.wait(3)
        before = len(a.observe()["selection"])
        a.look_at(*manor["position"]); a.wait(5)
        o = a.observe()
        mil = [x for x in self.own(o, "character", "Militia") if "screen" in x and 70 < x["screen"][1] < 560]
        if mil:
            self.box([m["screen"] for m in mil], add=before > 0)
        a.tap("Ctrl+1"); a.wait(3)
        return len(a.observe()["selection"])

    def attack(self, target_point, o):
        """Group 1: the nearest enemy it can see (soldiers, peasants, buildings, the Manor last),
        else on to the enemy's start"""
        a = self.a
        a.key_action("groupSelect1"); a.wait(3)
        sel = a.observe()["selection"]
        if not sel:
            return "no army"
        self.armySize = len(sel)
        centre = [sum(s["position"][0] for s in sel) / len(sel), sum(s["position"][1] for s in sel) / len(sel)]
        a.look_at(*centre); a.wait(5)
        o = a.observe()
        foes = self.enemies(o)
        def rank(x):
            soldier = x["kind"] == "character" and any(h in x["type"] for h in SOLDIER_HINTS)
            peasant = x["kind"] == "character" and not soldier
            manor = x["type"] == "Manor"
            wall = "Stockade" in x["type"] or "Wall" in x["type"]
            order = 0 if soldier else 1 if peasant else 2 if manor else 4 if wall else 3
            return (order, dist(x["position"], centre))
        near = [x for x in foes if dist(x["position"], centre) < 140]
        for f in sorted(near, key=rank)[:6]:
            r = a.request("rightclickobject", object=f["id"])
            if r.get("ok"):
                return "attack %s %d" % (f["type"], f["id"])
            # another enemy under the cursor is as good a target
            u = r.get("underCursor")
            if u and u in {x["id"] for x in foes}:
                if a.request("rightclickobject", object=u).get("ok"):
                    return "attack %d" % u
        # nothing near: a remembered enemy building is the way; else the next point of the sweep
        # (a remembered building reached and not seen there: gone, or not worth the walk)
        for x in self.remembered(o):
            if dist(x["position"], centre) < 50:
                self.stale.add(x["id"])
        rem = [x for x in self.remembered(o) if x["id"] not in self.stale]
        manors = [x for x in rem if x["type"] == "Manor"]
        if manors:
            goal = manors[0]["position"]
        elif rem:
            # deeper into their base: the known building farthest from our home
            goal = max(rem, key=lambda x: dist(x["position"], self.home))["position"]
        else:
            # the sweep's next point not yet passed (not nearer home than the army is)
            while self.sweep and (dist(self.sweep[0], centre) < 45 or dist(self.sweep[0], self.home) < dist(centre, self.home) - 30):
                self.sweep.pop(0)
            goal = self.sweep[0] if self.sweep else target_point
        for dx, dz in ((0, 0), (8, 0), (0, 8), (-8, 0), (0, -8), (12, 12)):
            if a.request("rightclickground", x=goal[0] + dx, z=goal[1] + dz).get("ok"):
                return "march to %.0f,%.0f" % (goal[0], goal[1])
        return "no way"


def record(a, me):
    """Both sides' strength (a probe: the record only)"""
    sides = {}
    for x in a.probe("objects")["objects"]:
        p = x["player"]
        if not p or p == 1026:
            continue
        s = sides.setdefault("agent" if p == me else "ai", {"soldiers": 0, "peasants": 0, "buildings": 0, "manor": 0})
        if x["type"] == "Manor":
            s["manor"] += 1
        if x["type"] in ("Peasant",):
            s["peasants"] += 1
        elif any(h in x["type"] for h in SOLDIER_HINTS):
            s["soldiers"] += 1
        else:
            s["buildings"] += 1
    return sides


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--game-path", default=os.environ.get("WKB_GAME_PATH"))
    ap.add_argument("--work", default=r"C:\Dev\wkbai-agent\rush")
    ap.add_argument("--minutes", type=float, default=25, help="game minutes at most")
    ap.add_argument("--speed", type=int, default=3, help="speed-up key presses (2: 4x, 3: 8x)")
    ap.add_argument("--general", default="", help="the AI General (empty: a random one)")
    args = ap.parse_args()
    log = []
    result = {"log": log}
    slots = "H|A" + (":" + args.general if args.general else "") + "|E"
    with Game(game_path=args.game_path, work=args.work) as game:
        game.start(skirmish="2 Player 4 Peaks.lvl", slots=slots, settings={"recordGame": 1})
        a = game.agent()
        bot = Bot(a, log)
        o = a.wait_for_game()
        me = o["player"]
        mapw, mapd = o["map"]
        manor = bot.own(o, type_="Manor")[0]
        home = manor["position"]
        enemy_start = [mapw - home[0], mapd - home[1]]
        # where the enemy is isn't known: a sweep over the far half of the map, from the centre
        cx, cz = mapw / 2, mapd / 2
        far = [mapw - home[0], mapd - home[1]]
        bot.sweep = [[cx, cz], [(cx + far[0]) / 2 + 80, (cz + far[1]) / 2], [(cx + far[0]) / 2, (cz + far[1]) / 2 + 80],
                     [far[0] + 150, far[1]], [far[0], far[1] - 150], [cx - 150, cz + 150], [cx + 150, cz - 150]]
        bot.sweep = [[min(max(p[0], 40), mapw - 40), min(max(p[1], 40), mapd - 40)] for p in bot.sweep]
        bot.say("start: our Manor at", home, "; the sweep:", bot.sweep)
        bot.home = home
        bot.start_economy(o)
        farm_done = False
        for _ in range(args.speed):
            a.key_action("speedUp")
        t0 = o["time"]
        records = []
        last_record = -60
        phase = "build"
        army_out_at = None
        while True:
            o = a.observe()
            t = o["time"] - t0
            if t >= args.minutes * 60:
                break
            if t - last_record >= 60:
                last_record = t
                r = record(a, me)
                records.append({"time": round(t), **r, "resources": o["resources"]})
                bot.say("t=%3.0fs" % t, json.dumps(r), o["resources"])
                ai = r.get("ai", {})
                if not ai or (ai.get("soldiers", 0) + ai.get("peasants", 0) == 0 and ai.get("manor", 0) == 0):
                    bot.say("the AI has no units and no Manor left")
                    result["won"] = True
                    break
                if not r.get("agent", {}).get("manor") and not r.get("agent", {}).get("soldiers") and not r.get("agent", {}).get("peasants"):
                    result["won"] = False
                    bot.say("we have nothing left")
                    break
            if not farm_done and any(x.get("own") and x["type"] == "Village Wood" for x in o["objects"]):
                farm_done = bot.after_village(o)
                o = a.observe()
            if farm_done and len([x for x in bot.own(o, "character") if "Farmer" in x["type"]]) < 6:
                n = bot.village_farmers(o)
                if n:
                    bot.say("t=%3.0fs %d farmers" % (t, n))
                o = a.observe()
            w = bot.idle_to_wood()
            if w:
                bot.say("t=%3.0fs %d idle peasants to the trees" % (t, w))
            manors = bot.own(o, type_="Manor")
            if manors:
                # (food for the farmers first, until there are some)
                farmers = len([x for x in bot.own(o, "character") if "Farmer" in x["type"]])
                n = bot.spawn_peasants(o, manors[0], keep_food=200 if farmers < 4 else 0)
                if n:
                    bot.say("t=%3.0fs spawned %d peasants" % (t, n))
                    time.sleep(3)
                c = bot.convert_and_group(manors[0])
                if c:
                    bot.say("t=%3.0fs %d peasants to militia" % (t, c))
            militia = len(bot.own(o, "character", "Militia"))
            if phase == "build" and (militia >= 12 or t > 150):
                phase = "attack"
                army_out_at = t
                bot.say("t=%3.0fs attack with %d militia" % (t, militia))
            if phase == "attack":
                what = bot.attack(enemy_start, o)
                bot.say("t=%3.0fs %s (army %s)" % (t, what, getattr(bot, "armySize", "?")))
            time.sleep(2)
        r = record(a, me)
        records.append({"time": round(a.observe()["time"] - t0), **r})
        result["records"] = records
        result.setdefault("won", None)
        result["shot"] = bmp_to_png(a.screenshot("rush-end"))
    out = os.path.join(args.work, "rush-result.json")
    json.dump(result, open(out, "w"), indent=1)
    print(json.dumps({k: v for k, v in result.items() if k not in ("log", "records")}, indent=1))
    print("records:", json.dumps(result.get("records", [])[-2:]))
    print("full:", out)


if __name__ == "__main__":
    main()
