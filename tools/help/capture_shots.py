"""The help's screenshots, taken with your own wkbai and your own copy of the game.

    python -I tools/help/capture_shots.py --exe <wkbai.exe> [--out DIR] [--only name,name] [--skip-existing]

Reads redata/help/shots/*.json. Each entry is one screenshot:
- "menu": [screens]   front-end screens to open (FrontEndUI.lua's Screens.<name>), via "menuTest"
- "game": { ... }     a running game, via "benchmark":
      "save"      a level or save (default "2 Player 4 Peaks.lvl"); its start camera unless
      "camera"    [x, y, z, pitch, yaw]
      "select", "openMenu", "buildTab", "diplomacyWindow", "hover", "keys", "script", "mouse",
      "stampdown", "drawCursor", "warmupFrames": passed on to the benchmark as they are
  A "game" entry is taken only with "ready": true; the others still need a scene set up by hand
  (a replay, a tutorial window, a battle) and are listed as skipped.

The screenshots show game art: they go to DIR (default C:\\Dev\\help-html\\shots), never into
git. Your wkconfig.json is copied (graphics settings), never written; the runs use a profile
folder of their own beside DIR. No window or dialog asks anything: the runs are scripted.
"""
import argparse, glob, json, os, shutil, subprocess, sys, time

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
GAME_KEYS = ("select", "openMenu", "buildTab", "diplomacyWindow", "hover", "keys", "script", "mouse",
             "stampdown", "drawCursor", "warmupFrames", "camera", "moveQueue", "fogReveal",
             "peasantActivity", "statistics", "timeline")


def run(exe, folder, cfg, out, timeout):
    with open(os.path.join(folder, "wkconfig.json"), "w", encoding="utf-8") as f:
        json.dump(cfg, f, indent=1)
    if os.path.exists(out):
        os.remove(out)
    log = open(os.path.join(folder, "run.log"), "w")
    p = subprocess.Popen([exe], cwd=folder, stdout=log, stderr=subprocess.STDOUT,
                         creationflags=subprocess.DETACHED_PROCESS | subprocess.CREATE_NO_WINDOW)
    deadline = time.time() + timeout
    while p.poll() is None and time.time() < deadline:
        if os.path.exists(out) and time.time() - os.path.getmtime(out) > 2:
            break
        time.sleep(1)
    time.sleep(1)
    if p.poll() is None:
        p.kill()
    return os.path.exists(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", required=True)
    ap.add_argument("--out", default=r"C:\Dev\help-html\shots")
    ap.add_argument("--config", help="wkconfig.json to copy settings from (default: the one beside the exe)")
    ap.add_argument("--only")
    ap.add_argument("--skip-existing", action="store_true")
    ap.add_argument("--timeout", type=int, default=240)
    a = ap.parse_args()
    exe = os.path.abspath(a.exe)
    base_cfg = json.load(open(a.config or os.path.join(os.path.dirname(exe), "wkconfig.json"), encoding="utf-8"))
    for k in ("benchmark", "menuTest", "test"):
        base_cfg.pop(k, None)
    os.makedirs(a.out, exist_ok=True)
    work = os.path.join(os.path.dirname(os.path.abspath(a.out)), "shot-run")
    os.makedirs(work, exist_ok=True)
    shutil.copytree(os.path.join(ROOT, "redata"), os.path.join(work, "redata"), dirs_exist_ok=True)
    base_cfg.update({"userPath": os.path.join(work, "user").replace("\\", "/"),
                     "musicEnabled": False, "soundEnabled": False, "introVideo": False, "showFps": False})

    entries = []
    for path in sorted(glob.glob(os.path.join(ROOT, "redata", "help", "shots", "*.json"))):
        entries += json.load(open(path, encoding="utf-8"))
    only = set(a.only.split(",")) if a.only else None
    done, failed = 0, []
    for e in entries:
        name = e["name"]
        if only and name not in only:
            continue
        png = os.path.join(a.out, name + ".png")
        if a.skip_existing and os.path.exists(png):
            continue
        if "menu" not in e and not e.get("game", {}).get("ready"):
            print(name, "skipped (no scripted set-up yet)", flush=True)
            continue
        bmp = os.path.join(work, name + ".bmp")
        cfg = dict(base_cfg)
        if "menu" in e:
            cfg["menuTest"] = {"open": e["menu"], "frames": e.get("frames", 90), "screenshot": bmp.replace("\\", "/")}
        else:
            g = e.get("game", {})
            bench = {"saveGame": g.get("save", "2 Player 4 Peaks.lvl"), "warmupFrames": 240, "frames": 30,
                     "screenshot": bmp.replace("\\", "/"), "report": os.path.join(work, name + ".json").replace("\\", "/")}
            bench.update({k: g[k] for k in GAME_KEYS if k in g})
            cfg["benchmark"] = bench
        ok = run(exe, work, cfg, bmp, a.timeout)
        if ok:
            # Pillow sits in the user's site packages, which "python -I" leaves out
            subprocess.run(["python", "-c", "import sys; from PIL import Image; Image.open(sys.argv[1]).save(sys.argv[2])", bmp, png])
            if os.path.exists(png):
                os.remove(bmp)
            done += 1
        else:
            failed.append(name)
        print(name, "ok" if ok else "NO SHOT", flush=True)
    print(f"{done} taken, {len(failed)} failed: {' '.join(failed)}")


if __name__ == "__main__":
    main()
