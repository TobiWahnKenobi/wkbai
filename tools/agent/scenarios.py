"""First evaluation scenarios for wkbai's agent control: python scenarios.py <name> [--game-path ...]

  menus        main menu -> Options -> Agent control page -> Cancel; Skirmish -> Back
  pathfinding  4 Peaks: every peasant box-selected, sent across the map; arrival, path, stuck
  building     4 Peaks: a peasant builds the first building its build menu offers
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


def pathfinding(game, out, minutes=3):
    game.start(skirmish="2 Player 4 Peaks.lvl", slots="H|A|E")
    a = game.agent()
    o = a.wait_for_game()
    peasants = [x for x in o["objects"] if x.get("own") and x["kind"] == "character"]
    a.look_at(*peasants[0]["position"]); a.wait(10)
    on = [x for x in a.observe()["objects"] if x.get("own") and x["kind"] == "character" and "screen" in x]
    sel = a.box_select([x["id"] for x in on])
    out["selected"] = [s["id"] for s in sel]
    # across the map: towards the other player's Manor (where it is: a probe, the evaluation's
    # knowledge, not the player's), stopping 40 m short
    allobj = a.probe("objects")["objects"]
    me = o["player"]
    enemy = [x for x in allobj if x["player"] not in (me, 0) and x["type"] == "Manor"]
    start = peasants[0]["position"]
    goal = enemy[0]["position"] if enemy else [start[0] - 200, start[1] + 200]
    d = dist(start, goal)
    target = [goal[0] + (start[0] - goal[0]) * 40 / d, goal[1] + (start[1] - goal[1]) * 40 / d]
    for dx, dz in ((0, 0), (6, 0), (0, 6), (-6, 0), (0, -6), (10, 10)):
        r = a.request("rightclickground", x=target[0] + dx, z=target[1] + dz)
        if r.get("ok"):
            target = [target[0] + dx, target[1] + dz]
            break
    out["order"] = r
    if not r.get("ok"):
        out["ok"] = False
        return
    a.key_action("speedUp"); a.key_action("speedUp")  # 4x (single player)
    t0 = a.observe()["time"]
    units = {i: {"start": next(x["position"] for x in on if x["id"] == i)} for i in out["selected"]}
    for i, u in units.items():
        p = a.probe("unit", object=i)
        path = [u["start"]] + p["movement"]["path"]
        u["pathLength"] = round(sum(dist(path[k], path[k + 1]) for k in range(len(path) - 1)), 1)
        u["pathPoints"] = len(p["movement"]["path"])
    deadline = time.time() + minutes * 60
    while time.time() < deadline:
        moving = 0
        t = a.observe()["time"]
        for i, u in units.items():
            if "arrived" in u:
                continue
            p = a.probe("unit", object=i)
            if p["movement"]["moving"]:
                moving += 1
            else:
                u["arrived"] = round(t - t0, 1)
                u["end"] = [round(v, 1) for v in p["position"][::2]]
                u["toTarget"] = round(dist(u["end"], target), 1)
                u["searchFailed"] = p["movement"]["searchFailed"]
        if moving == 0:
            break
        time.sleep(1)
    # once all stopped, a while later again: a stop that was only a pause shows here
    a.wait(300)
    for i, u in units.items():
        p = a.probe("unit", object=i)
        u["endLater"] = [round(v, 1) for v in p["position"][::2]]
        u["toTarget"] = round(dist(u["endLater"], target), 1)
        u["movingLater"] = p["movement"]["moving"]
    out["target"] = [round(v, 1) for v in target]
    out["units"] = units
    # arrived = stopped within 15 m of the point (a group spreads around it)
    out["ok"] = all("arrived" in u and u["toTarget"] < 15 and not u["searchFailed"] for u in units.values())


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


SCENARIOS = {"menus": menus, "pathfinding": pathfinding, "building": building, "versus-ai": versus_ai}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("name", choices=sorted(SCENARIOS))
    ap.add_argument("--game-path", default=os.environ.get("WKB_GAME_PATH"))
    ap.add_argument("--exe")
    ap.add_argument("--work")
    ap.add_argument("--minutes", type=float, default=3)
    args = ap.parse_args()
    out = {"scenario": args.name}
    t = time.time()
    with Game(game_path=args.game_path, exe=args.exe, work=args.work) as game:
        try:
            fn = SCENARIOS[args.name]
            if args.name in ("pathfinding", "versus-ai"):
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
