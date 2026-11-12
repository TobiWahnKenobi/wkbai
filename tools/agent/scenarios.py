"""First evaluation scenarios for wkbai's agent control: python scenarios.py <name> [--game-path ...]

  menus        main menu -> Options -> Agent control page -> Cancel; Skirmish -> Back
  menus-more   a key rebound on the Keyboard page (Pause on K); a game saved and loaded again
  pathfinding  a group (--units, made beyond the level's; --level) box-selected and sent across
               the map; arrival, path length, end distance, stuck units
  building     4 Peaks: a peasant builds the first building its build menu offers
  placement    a building turned a quarter with the key before it is placed; the refusal box
  lockstep     the simulation held between advances, n steps moving the clock by n steps
  versus-ai    4 Peaks against an AI General: idle peasants (the activity button) sent to trees,
               a few game minutes at 4x; both sides' strength at the end (probe)

Each plays through the agent's input only (wkbai_agent.py) and prints a JSON result; "ok" says
whether the scenario's checks held. Licensed under the GNU General Public License 3 (as wkbai).
"""
import argparse
import json
import math
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wkbai_agent import Game, AgentError, bmp_to_png  # noqa: E402


def dist(a, b):
    return math.hypot(a[0] - b[0], a[1] - b[1])


def menus(game, out):
    game.start()
    a = game.agent()
    steps = []
    def has(prefix):
        return a.until(lambda: [e["id"] for e in a.elements(prefix)], timeout=20)
    def step(name, ok):
        steps.append({"step": name, "ok": bool(ok)})
        return ok
    step("main menu shows Options", has("button:Options"))
    a.click_ui("button:Options"); a.wait(10)
    step("Options open: the Agent control tab", has("tab:Agent control"))
    a.click_ui("tab:Agent control"); a.wait(10)
    labels = [e["id"] for e in a.elements("button:")]
    step("Agent control page: its switch shows On", "button:On" in labels)
    out["optionsShot"] = bmp_to_png(a.screenshot("menus-options"))
    a.click_ui("button:Cancel"); a.wait(10)
    step("Cancel: back in the main menu", has("button:Skirmish"))
    a.click_ui("button:Skirmish"); a.wait(20)
    tabs = has("tab:")
    step("Skirmish lobby: its tabs", tabs)
    out["lobbyTabs"] = tabs
    out["lobbyShot"] = bmp_to_png(a.screenshot("menus-lobby"))
    back = [e["id"] for e in a.elements("button:") if "Back" in e["id"]]
    if back:
        a.click_ui(back[0]); a.wait(10)
    step("Back: the main menu again", has("button:Options"))
    out["steps"] = steps
    out["ok"] = all(s["ok"] for s in steps)


def pathfinding(game, out, minutes=3, units=4, level="2 Player 4 Peaks.lvl"):
    """units peasants (the level's, plus made ones: harness.spawn) box-selected and sent across
    the map; per unit the path, arrival, end distance; a group of n may spread over a radius of
    5 + 2.2 * sqrt(n) metres (15 for up to 4)"""
    players = int(level.split(" ")[0]) if level[0].isdigit() else 2
    # an AI General at the second place: its Manor is the far point to walk to
    slots = "|".join(["H", "A"] + ["E"] * (players - 2))
    game.start(skirmish=level, slots=slots)
    a = game.agent()
    o = a.wait_for_game()
    me = o["player"]
    peasants = [x for x in o["objects"] if x.get("own") and x["kind"] == "character"]
    start = peasants[0]["position"]
    if units > len(peasants):
        a.spawn("Peasant", units - len(peasants), start[0] + 6, start[1] + 6, 2.2)
        a.wait(30)
    a.look_at(start[0] + 4, start[1] + 4); a.wait(10)
    own = [x for x in a.observe()["objects"] if x.get("own") and x["kind"] == "character"]
    on = [x for x in own if "screen" in x]
    if len(on) < len(own):
        # a group too big for the screen: the camera higher (the wheel), as a person zooms out
        for _ in range(6):
            a.wheel(-3); a.wait(5)
            own = [x for x in a.observe()["objects"] if x.get("own") and x["kind"] == "character"]
            on = [x for x in own if "screen" in x]
            if len(on) == len(own):
                break
    sel = a.box_select([x["id"] for x in on])
    out["units"] = len(on)
    out["selected"] = len(sel)
    # across the map: towards the other player's Manor (where it is: a probe, the evaluation's
    # knowledge, not the player's), else the map's centre
    allobj = a.probe("objects")["objects"]
    enemy = [x for x in allobj if x["player"] not in (me, 0) and x["type"] == "Manor"]
    mapw, mapd = a.observe()["map"]
    goal = enemy[0]["position"] if enemy else [mapw / 2, mapd / 2]
    d = dist(start, goal)
    target = [goal[0] + (start[0] - goal[0]) * 40 / d, goal[1] + (start[1] - goal[1]) * 40 / d] if enemy else goal
    r = {"ok": False}
    for dx, dz in ((0, 0), (6, 0), (0, 6), (-6, 0), (0, -6), (10, 10), (-10, 10), (15, -10)):
        r = a.request("rightclickground", x=target[0] + dx, z=target[1] + dz)
        if r.get("ok"):
            target = [target[0] + dx, target[1] + dz]
            break
    out["order"] = {k: r.get(k) for k in ("ok", "error", "clicked")}
    if not r.get("ok"):
        out["ok"] = False
        return
    a.key_action("speedUp"); a.key_action("speedUp")  # 4x (single player)
    t0 = a.observe()["time"]
    ids = [s["id"] for s in sel]
    starts = {x["id"]: x["position"] for x in on}
    stats = {}
    for i in ids:
        p = a.probe("unit", object=i)
        path = [starts[i]] + p["movement"]["path"]
        stats[i] = {"pathLength": round(sum(dist(path[k], path[k + 1]) for k in range(len(path) - 1)), 1)}
    deadline = time.time() + minutes * 60
    while time.time() < deadline:
        t = a.observe()["time"]
        moving = 0
        for i in ids:
            u = stats[i]
            if "arrived" in u:
                continue
            p = a.probe("unit", object=i)
            if p["movement"]["moving"]:
                moving += 1
            else:
                u["arrived"] = round(t - t0, 1)
                u["searchFailed"] = p["movement"]["searchFailed"]
        if moving == 0:
            break
        time.sleep(1)
    a.wait(300)  # a stop that was only a pause shows here
    radius = 15 if len(ids) <= 4 else 5 + 2.2 * math.sqrt(len(ids))
    for i in ids:
        p = a.probe("unit", object=i)
        stats[i]["toTarget"] = round(dist([p["position"][0], p["position"][2]], target), 1)
        stats[i]["movingLater"] = p["movement"]["moving"]
    arrived = [u for u in stats.values() if "arrived" in u]
    times = sorted(u["arrived"] for u in arrived)
    dists = sorted(u["toTarget"] for u in stats.values())
    lengths = sorted(u["pathLength"] for u in stats.values())
    far = [i for i, u in stats.items() if u["toTarget"] > radius]
    out.update({"level": level, "target": [round(v, 1) for v in target], "radius": round(radius, 1),
                "arrived": len(arrived), "stillMoving": len(ids) - len(arrived),
                "arrivalSeconds": {"first": times[0] if times else None, "median": times[len(times) // 2] if times else None, "last": times[-1] if times else None},
                "toTarget": {"median": dists[len(dists) // 2], "max": dists[-1]},
                "pathLength": {"min": lengths[0], "median": lengths[len(lengths) // 2], "max": lengths[-1]},
                "searchFailed": sum(1 for u in stats.values() if u.get("searchFailed")),
                "outsideRadius": {str(i): stats[i]["toTarget"] for i in far}})
    out["shot"] = bmp_to_png(a.screenshot("pathfinding-%d" % len(ids)))
    out["ok"] = out["selected"] == len(ids) == units and not far and out["stillMoving"] == 0 and out["searchFailed"] == 0


def placement(game, out):
    """A building turned with the key before it is placed, and the refusal box where it can't go"""
    # resources for two foundations (the lobby's start resources: a destroyed foundation isn't paid back)
    game.start(skirmish="2 Player 4 Peaks.lvl", slots="H|E|E", settings={"startWood": 3000, "startGold": 1000, "startFood": 3000})
    a = game.agent()
    o = a.wait_for_game()
    peasant = next(x for x in o["objects"] if x.get("own") and x["kind"] == "character")
    manor = next(x for x in o["objects"] if x.get("own") and x["type"] == "Manor")
    a.look_at(*manor["position"]); a.wait(10)
    def pick():
        a.click_object(peasant["id"])
        a.click_ui("tab:ECONOMIC"); a.wait(3)
        builds = [e for e in a.elements("cmd:Build ") if e["enabled"]]
        a.click_ui(builds[0]["id"]); a.wait(3)
        return builds[0]["id"][4:]
    out["command"] = pick()
    placing = a.observe().get("placing")
    out["placingAtFirst"] = placing
    a.key_action("turnBuildingRight"); a.wait(3)
    out["placingTurned"] = a.observe().get("placing")
    # where it can go and where not: the refusal box's text with the cursor there
    mx, mz = manor["position"]
    refused, accepted = None, None
    for dx in range(-60, 61, 12):
        for dz in range(-60, 61, 12):
            try:
                sx, sy = a.point(mx + dx, mz + dz)
            except Exception:
                continue
            if not (160 < sx < 1100 and 90 < sy < 560):
                continue  # the middle of the screen (not under the banner or the panels)
            if any(sx >= e["rect"][0] and sx < e["rect"][0] + e["rect"][2] and sy >= e["rect"][1] and sy < e["rect"][1] + e["rect"][3]
                   for e in a.ui()["elements"]):
                continue
            a.move(sx, sy); a.wait(3)
            p = a.observe().get("placing") or {}
            if p.get("refusal") and not refused:
                refused = {"at": [mx + dx, mz + dz], "screen": [sx, sy], "refusal": p["refusal"]}
            elif not p.get("refusal") and not accepted and abs(dx) >= 24 and abs(dz) >= 24:
                accepted = {"at": [mx + dx, mz + dz], "screen": [sx, sy]}
            if refused and accepted:
                break
        if refused and accepted:
            break
    out["refusedSpot"] = refused
    before = {x["id"] for x in a.observe()["objects"] if x.get("own")}
    if refused:
        a.move(*refused["screen"]); a.wait(3)
        a.click(refused["screen"][0], refused["screen"][1], "right"); a.wait(20)
        out["placedWhereRefused"] = [x["id"] for x in a.observe()["objects"] if x.get("own") and x["id"] not in before]
    # where it can go: first unturned (its orientation noted, then destroyed with the Delete key, as
    # a player cancels a foundation), then the same spot turned a quarter with the key
    out["acceptedSpot"] = accepted
    def place_at(turns):
        if a.observe().get("placing"):
            a.click(640, 300, "left"); a.wait(3)  # (drops the outline)
        pick()
        for _ in range(turns):
            a.key_action("turnBuildingRight"); a.wait(3)
        a.move(*accepted["screen"]); a.wait(3)
        turn = (a.observe().get("placing") or {}).get("turn")
        known = {x["id"] for x in a.observe()["objects"] if x.get("own")}
        a.click(accepted["screen"][0], accepted["screen"][1], "right"); a.wait(20)
        new = [x for x in a.observe()["objects"] if x.get("own") and x["id"] not in known and x["kind"] == "building"]
        return turn, (new[0]["id"] if new else None)
    if accepted:
        turn0, first = place_at(0)
        if first:
            out["unturnedOrientation"] = round(a.probe("unit", object=first)["orientation"], 3)
            # selected by a click on it (a peasant may stand in front: then a little beside)
            b = next(x for x in a.observe()["objects"] if x["id"] == first)
            for ox, oy in ((0, 0), (12, -8), (-12, -8), (0, -14)):
                a.click(b["screen"][0] + ox, b["screen"][1] + oy); a.wait(3)
                if [x["id"] for x in a.observe()["selection"]] == [first]:
                    break
            a.key_action("deleteSelected"); a.wait(30)
            out["deletedFirst"] = first not in {x["id"] for x in a.observe()["objects"]}
        out["turnWhenPlaced"], second = place_at(1)
        out["placedTurned"] = second
        if second:
            out["turnedOrientation"] = round(a.probe("unit", object=second)["orientation"], 3)
    out["shot"] = bmp_to_png(a.screenshot("placement"))
    turned = out.get("turnedOrientation")
    unturned = out.get("unturnedOrientation")
    quarter = abs(math.fmod(abs((turned or 0) - (unturned or 0)), math.pi) - math.pi / 2) < 0.05
    out["ok"] = bool(placing and placing["turn"] == 0 and out["placingTurned"] and out["placingTurned"]["turn"] in (1, 3)
                     and refused and not out.get("placedWhereRefused") and out.get("placedTurned")
                     and turned is not None and unturned is not None
                     and quarter and out.get("deletedFirst"))


def menus_more(game, out):
    """A key rebound on the Keyboard page (Pause on K), and a game saved and loaded again"""
    steps = []
    def step(name, ok, **extra):
        steps.append(dict({"step": name, "ok": bool(ok)}, **extra))
        return ok
    # the key: Options -> Controls -> Keyboard -> Pause's first key -> K -> OK
    game.start()
    a = game.agent()
    a.until(lambda: a.elements("button:Options"), 20)
    a.click_ui("button:Options"); a.wait(10)
    a.click_ui("tab:Controls"); a.wait(5)
    a.click_ui("tab:Keyboard"); a.wait(5)
    cell = None
    for _ in range(12):
        cell = next((e for e in a.elements("key:pause:1")), None)
        if cell:
            break
        page = next(e for e in a.elements("key:"))  # any cell: the page is there
        a.move(page["rect"][0], page["rect"][1]); a.wheel(-3); a.wait(5)
    step("Keyboard page: Pause's key cell", cell, label=cell and cell["label"])
    a.click_ui("key:pause:1"); a.wait(5)
    a.tap("K"); a.wait(5)
    cell = next((e for e in a.elements("key:pause:1")), None)
    step("Pause's cell shows K after pressing K", cell and cell["label"] == "K", label=cell and cell["label"])
    a.click_ui("button:OK"); a.wait(10)
    cfg = json.load(open(os.path.join(game.work, "wkconfig.json")))
    step("saved in wkconfig.json", cfg.get("keyBindings", {}).get("pause", [""])[0] == "K", keyBindings=cfg.get("keyBindings"))
    # a game saved under a name and loaded again
    game.start(skirmish="2 Player 4 Peaks.lvl", slots="H|E|E")
    a = game.agent()
    o = a.wait_for_game()
    a.tap("Escape"); a.wait(10)
    saveButton = next((e["id"] for e in a.elements("button:") if "Save" in e["id"]), None)
    step("in-game menu: a Save button", saveButton, button=saveButton)
    a.click_ui(saveButton); a.wait(10)
    a.text("agent save test"); a.wait(5)
    savedAt = a.observe()["time"]
    a.click_ui("button:Save"); a.wait(30)
    path = os.path.join(game.work, "profile", "saved", "Save_Games", "agent save test.sav")
    step("the save file written", a.until(lambda: os.path.exists(path), 10), path=path)
    # back in the in-game menu (the save screen closes itself once saved; else Escape opens the menu)
    def find_load():
        return next((e["id"] for e in a.elements("button:") if "Load" in e["id"]), None)
    loadButton = a.until(find_load, 5)
    for _ in range(3):
        if loadButton:
            break
        a.tap("Escape"); a.wait(15)
        loadButton = a.until(find_load, 5)
    if not step("in-game menu: a Load button", loadButton, button=loadButton, onScreen=[e["id"] for e in a.ui()["elements"]][:20]):
        out["steps"] = steps
        out["ok"] = False
        return
    a.click_ui(loadButton); a.wait(10)
    row = next((e for e in a.elements("item:agent save test")), None)
    step("Load list: the save", row)
    a.wait(300)  # (the game goes on a while: the loaded one must be back at the save's time)
    before = a.request("observe").get("time", 0)
    a.click_ui(row["id"]); a.wait(5)
    a.double_click(row["rect"][0] + row["rect"][2] // 2, row["rect"][1] + row["rect"][3] // 2); a.wait(10)
    # (the game is paused while its menu is open, so the clock says nothing: the log says what loaded)
    def loaded_now():
        log = open(game.log, encoding="latin1").read()
        at = log.rfind("Starting Save_Games" + chr(92) + "agent save test.sav")
        return at >= 0 and "Loading: done" in log[at:]
    done = a.until(loaded_now, 90)
    r = a.until(lambda: (lambda r: r.get("ok") and r.get("player") and r.get("objects") and r)(a.request("observe")), 60) if done else None
    step("loaded: the log's load of the save finished, the player's objects back", done and r
         and len([x for x in r["objects"] if x.get("own")]) >= len([x for x in o["objects"] if x.get("own")]),
         savedAt=round(savedAt, 1), after=r and round(r["time"], 1))
    out["steps"] = steps
    out["ok"] = all(s["ok"] for s in steps)


def lockstep(game, out):
    """The simulation in lockstep: it doesn't move between advances, and an advance of n steps
    moves the game clock by n steps"""
    game.start(skirmish="2 Player 4 Peaks.lvl", slots="H|A|E")
    a = game.agent()
    a.wait_for_game()
    r = a.lockstep(True)
    t0 = a.probe("hash")
    a.wait(60)
    t1 = a.probe("hash")
    adv = a.advance(100)
    t2 = a.probe("hash")
    out.update({"stepMs": adv.get("stepMs"), "held": {"time": [t0["time"], t1["time"]], "same": t0["hash"] == t1["hash"]},
                "advanced": {"time": t2["time"], "by": round(t2["time"] - t1["time"], 3), "hashChanged": t2["hash"] != t1["hash"]}})
    a.lockstep(False)
    a.wait(60)
    t3 = a.probe("hash")
    out["after"] = {"time": t3["time"]}
    step = (adv.get("stepMs") or 20) / 1000.0
    out["ok"] = t0["hash"] == t1["hash"] and abs((t2["time"] - t1["time"]) - 100 * step) < 1e-3 and t3["time"] > t2["time"]


def building(game, out):
    game.start(skirmish="2 Player 4 Peaks.lvl", slots="H|E|E")
    a = game.agent()
    o = a.wait_for_game()
    peasants = [x for x in o["objects"] if x.get("own") and x["kind"] == "character"]
    manor = next(x for x in o["objects"] if x.get("own") and x["type"] == "Manor")
    a.look_at(*peasants[0]["position"]); a.wait(10)
    a.click_object(peasants[0]["id"])
    before = {x["id"] for x in a.observe()["objects"] if x.get("own")}
    # the build menu's first enabled building (the Economic tab)
    a.click_ui("tab:ECONOMIC"); a.wait(3)
    builds = [e for e in a.elements("cmd:Build ") if e["enabled"]]
    out["offered"] = [e["id"][4:] for e in builds]
    if not builds:
        out["ok"] = False
        return
    command = builds[0]["id"][4:]
    out["command"] = command
    placed = None
    mx, mz = manor["position"]
    for dx, dz in ((-30, -10), (30, -10), (-30, 20), (30, 25), (0, -35), (-45, 0), (45, 0)):
        a.click_ui("cmd:" + command); a.wait(3)
        r = a.request("rightclickground", x=mx + dx, z=mz + dz)
        a.wait(15)
        new = [x for x in a.observe()["objects"] if x.get("own") and x["id"] not in before and x["kind"] == "building"]
        if new:
            placed = new[0]
            out["at"] = [mx + dx, mz + dz]
            break
        # not placed (the red refusal box): a left click drops the outline, then the peasant again
        a.request("click", x=640, y=300, button="left")
        a.click_object(peasants[0]["id"])
    out["placed"] = placed
    if not placed:
        out["ok"] = False
        return
    p = a.probe("unit", object=peasants[0]["id"])
    out["peasantOrders"] = [x["order"] for x in p["orders"]]
    a.key_action("speedUp"); a.key_action("speedUp")
    progress = []
    deadline = time.time() + 150
    while time.time() < deadline:
        b = next((x for x in a.observe()["objects"] if x["id"] == placed["id"]), None)
        if not b:
            break
        progress.append(b.get("health"))
        if b.get("health") and b["health"][0] >= b["health"][1]:
            break
        time.sleep(3)
    out["health"] = progress[-1] if progress else None
    out["shot"] = bmp_to_png(a.screenshot("building"))
    out["ok"] = bool(out["health"] and out["health"][0] > 0 and any("Build" in x for x in out["peasantOrders"]))


def versus_ai(game, out, minutes=3):
    game.start(skirmish="2 Player 4 Peaks.lvl", slots="H|A|E")
    a = game.agent()
    o = a.wait_for_game()
    me = o["player"]
    peasants = [x for x in o["objects"] if x.get("own") and x["kind"] == "character"]
    a.look_at(*peasants[0]["position"]); a.wait(10)
    # the Peasant Activity Interface, opened by its minimap button (its idle bar)
    a.click_ui("minimapButton:activity"); a.wait(5)
    a.key_action("speedUp"); a.key_action("speedUp")
    log = []
    t0 = a.observe()["time"]
    deadline = time.time() + minutes * 60 / 4 + 20
    while time.time() < deadline:
        # the idle peasants, by the Peasant Activity Interface's idle bar, as a person would
        acts = [e["id"] for e in a.elements("activity:") if "dle" in e["label"]] or [e["id"] for e in a.elements("activity:")][-1:]
        sent = 0
        seen = {"activityButtons": acts}
        if acts:
            a.click_ui(acts[0]); a.wait(3)
            sel = a.observe()["selection"]
            seen["selected"] = len(sel)
            if sel:
                everything = a.observe(scenery=True)["objects"]
                units = {x["id"] for x in everything if x["kind"] != "other"}
                trees = [x for x in everything if "Tree" in x["type"] and "screen" in x]
                c = sel[0]["position"]
                treeIds = {x["id"] for x in trees}
                seen["trees"] = len(trees)
                # the nearest tree; trees stand close, so another tree under the cursor is as good
                for tree in sorted(trees, key=lambda x: dist(x["position"], c))[:6]:
                    r = a.request("rightclickobject", object=tree["id"])
                    # (another tree in front, its centre off the screen: not a unit or building)
                    if not r.get("ok") and r.get("underCursor") and (r["underCursor"] in treeIds or r["underCursor"] not in units):
                        r = a.request("rightclickobject", object=r["underCursor"])
                    seen["reply"] = r.get("error", "ok")
                    if r.get("ok"):
                        sent = len(sel)
                        break
        o = a.observe()
        log.append({"time": round(o["time"] - t0), "resources": o["resources"], "population": o["population"], "sentToTrees": sent, "seen": seen})
        if o["time"] - t0 >= minutes * 60:
            break
        time.sleep(4)
    out["log"] = log
    allobj = a.probe("objects")["objects"]
    sides = {}
    for x in allobj:
        if x["player"]:
            s = sides.setdefault(x["player"], {"characters": 0, "buildings": 0})
            s["characters" if x["type"] in ("Peasant",) or "Militia" in x["type"] else "buildings"] += 1
    out["sides"] = {("agent" if k == me else str(k)): v for k, v in sides.items()}
    out["shot"] = bmp_to_png(a.screenshot("versus-ai"))
    out["ok"] = bool(log) and any(e["sentToTrees"] for e in log) and log[-1]["resources"]["Wood"] >= log[0]["resources"]["Wood"]


SCENARIOS = {"menus": menus, "menus-more": menus_more, "pathfinding": pathfinding, "building": building, "placement": placement,
             "lockstep": lockstep, "versus-ai": versus_ai}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("name", choices=sorted(SCENARIOS))
    ap.add_argument("--game-path", default=os.environ.get("WKB_GAME_PATH"))
    ap.add_argument("--exe")
    ap.add_argument("--work")
    ap.add_argument("--minutes", type=float, default=3)
    ap.add_argument("--units", type=int, default=4, help="pathfinding: the group's size (made units beyond the level's)")
    ap.add_argument("--level", default="2 Player 4 Peaks.lvl", help="pathfinding: the map")
    args = ap.parse_args()
    out = {"scenario": args.name}
    t = time.time()
    with Game(game_path=args.game_path, exe=args.exe, work=args.work) as game:
        try:
            fn = SCENARIOS[args.name]
            if args.name == "pathfinding":
                fn(game, out, args.minutes, args.units, args.level)
            elif args.name == "versus-ai":
                fn(game, out, args.minutes)
            else:
                fn(game, out)
        except AgentError as e:
            out["ok"] = False
            out["refused"] = e.reply
    out["seconds"] = round(time.time() - t)
    print(json.dumps(out, indent=1))
    return 0 if out.get("ok") else 1


if __name__ == "__main__":
    sys.exit(main())
