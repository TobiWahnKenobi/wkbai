"""wkbai agent control: a Python client (docs: agent-control.md in the RE repo; the protocol in
wkbai/AgentServer.h).

A program plays wkbai the way a person does: it moves the mouse, clicks and presses keys, and the
game turns that into commands exactly as it does for a person. This module starts the game with
agent control on (in a folder of its own, never the user's profile), connects with the token,
and offers the protocol's ops plus a few composites a hand would make (select by box, place a
building).

    from wkbai_agent import Game
    with Game(game_path=r"C:\\Games\\...\\Warrior Kings Battles") as game:
        game.start(skirmish="2 Player 4 Peaks.lvl", slots="H|A|E")
        a = game.agent()
        a.wait_for_game()
        print(a.observe()["resources"])

Licensed under the GNU General Public License 3 (as wkbai).
"""
import json
import os
import shutil
import socket
import subprocess
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
DEFAULT_EXE = os.path.join(REPO, "build", "wkbai", "RelWithDebInfo", "wkbai.exe")


class AgentError(Exception):
    """A request the game refused: the reply is in .reply"""
    def __init__(self, reply):
        super().__init__(reply.get("error", "refused"))
        self.reply = reply


class Agent:
    """The connection to a running wkbai with agent control on."""

    def __init__(self, token, port=7790, host="127.0.0.1", timeout=120):
        self.events = []
        self._id = 0
        deadline = time.time() + 60
        while True:
            try:
                self._sock = socket.create_connection((host, port), timeout=timeout)
                break
            except OSError:
                if time.time() > deadline:
                    raise
                time.sleep(0.5)
        self._file = self._sock.makefile("rwb")
        self.hello = self.ask("hello", token=token)

    def close(self):
        try:
            self._sock.close()
        except OSError:
            pass

    # -- the protocol
    def request(self, op, **fields):
        """One request; the reply as it came (ok or not)"""
        self._id += 1
        req = dict(fields, op=op, id=self._id)
        self._file.write((json.dumps(req) + "\n").encode())
        self._file.flush()
        while True:
            line = self._file.readline()
            if not line:
                raise ConnectionError("the game closed the connection")
            reply = json.loads(line)
            if "event" in reply:
                self.events.append(reply)
                continue
            return reply

    def ask(self, op, **fields):
        """One request; raises AgentError when refused"""
        reply = self.request(op, **fields)
        if not reply.get("ok"):
            raise AgentError(reply)
        return reply

    # -- layer 1: input
    def move(self, x, y): return self.ask("move", x=x, y=y)
    def click(self, x, y, button="left"): return self.ask("click", x=x, y=y, button=button)
    def double_click(self, x, y, button="left"): return self.ask("doubleclick", x=x, y=y, button=button)
    def drag(self, start, end, button="left", steps=6): return self.ask("drag", **{"from": list(start), "to": list(end), "button": button, "steps": steps})
    def key(self, name, down): return self.ask("key", key=name, down=down)
    def tap(self, chord): return self.ask("tap", key=chord)
    def text(self, text): return self.ask("text", text=text)
    def wheel(self, clicks): return self.ask("wheel", clicks=clicks)
    def wait(self, frames): return self.ask("wait", frames=frames)
    def status(self): return self.ask("status")
    def screenshot(self, name="screenshot"): return self.ask("screenshot", name=name)["path"]

    # -- layer 2: aimed
    def ui(self): return self.ask("ui")
    def elements(self, prefix=""): return [e for e in self.ui()["elements"] if e["id"].startswith(prefix)]
    def click_ui(self, element, button="left"): return self.ask("clickui", element=element, button=button)
    def click_object(self, object_id, add=False): return self.ask("clickobject", object=object_id, add=add)
    def right_click_object(self, object_id, add=False): return self.ask("rightclickobject", object=object_id, add=add)
    def click_ground(self, x, z, add=False): return self.ask("clickground", x=x, z=z, add=add)
    def right_click_ground(self, x, z, add=False): return self.ask("rightclickground", x=x, z=z, add=add)
    def camera(self, x, z): return self.ask("camera", x=x, z=z)
    def key_action(self, action): return self.ask("keyaction", action=action)

    # -- layer 3: what the player sees, and the test probes
    def observe(self, scenery=False): return self.ask("observe", scenery=scenery)
    def own(self, kind=None, type_=None):
        return [o for o in self.observe()["objects"] if o.get("own") and (kind is None or o["kind"] == kind)
                and (type_ is None or o["type"] == type_)]
    def probe(self, what, **fields): return self.ask("probe", what=what, **fields)

    # -- composites, as a hand makes them
    def wait_for_game(self, timeout=120):
        """Until the game shows the player's own objects (and the fog has been worked out)"""
        deadline = time.time() + timeout
        while time.time() < deadline:
            r = self.request("observe")
            if r.get("ok") and r.get("player") and r.get("objects") and r.get("fogReady"):
                self.wait(30)
                return self.observe()
            time.sleep(0.5)
        raise TimeoutError("no game on the screen")

    def look_at(self, x, z, frames=5):
        """The camera to a map point, by the minimap"""
        self.camera(x, z)
        self.wait(frames)

    def select(self, ids):
        """A click on the first, Shift+click on the others; the selection afterwards"""
        ids = list(ids)
        self.click_object(ids[0])
        for i in ids[1:]:
            self.click_object(i, add=True)
        return self.observe()["selection"]

    def box_select(self, ids, margin=24):
        """A box dragged around the objects' screen points (all must be on the screen)"""
        seen = {o["id"]: o for o in self.observe()["objects"]}
        pts = [seen[i]["screen"] for i in ids if i in seen and "screen" in seen[i]]
        if len(pts) != len(ids):
            raise AgentError({"error": "not every object is on the screen"})
        x0, y0 = min(p[0] for p in pts) - margin, min(p[1] for p in pts) - margin
        x1, y1 = max(p[0] for p in pts) + margin, max(p[1] for p in pts) + margin
        self.drag((x0, y0), (x1, y1))
        return self.observe()["selection"]

    def place(self, command, x, z, tab=None):
        """A building by its build command ("Build Farm") at a map point: the build tab, the
        button, then a right-click there (the outline follows the cursor). The selection must
        offer the command."""
        if tab:
            self.click_ui("tab:" + tab)
            self.wait(2)
        self.click_ui("cmd:" + command)
        self.wait(2)
        # the ground there: the outline under the cursor, then the right-click that places it
        return self.ask("rightclickground", x=x, z=z, add=False)

    def until(self, test, timeout=60, every=0.5):
        """Polls test() (wall clock) until it gives something true"""
        deadline = time.time() + timeout
        while time.time() < deadline:
            r = test()
            if r:
                return r
            time.sleep(every)
        return None


class Game:
    """wkbai started for an agent, in its own folder (never the user's profile)."""

    def __init__(self, game_path=None, exe=None, work=None, port=7790, window=(1280, 720), process_name="wkbai-agent"):
        self.game_path = game_path or os.environ.get("WKB_GAME_PATH")
        if not self.game_path:
            raise ValueError("game_path (or WKB_GAME_PATH): the Warrior Kings Battles folder")
        self.exe = exe or DEFAULT_EXE
        self.work = work or os.path.join(os.environ.get("TEMP", "."), "wkbai-agent-run")
        self.port = port
        self.window = window
        self.process_name = process_name  # the exe is copied under this name (a run is easy to tell apart)
        self.proc = None
        self._agent = None

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.stop()

    def start(self, skirmish=None, slots="H|A|E", save=None, cheat=False, host_solo=None, config=None, log_name="game"):
        """Starts the game: skirmish="<level>" (single player, slots as the skirmish menu's, e.g.
        "H|A|E"; probes allowed, as in every benchmark run), save="<save game>", host_solo=
        "<level>" (a hosted game alone: no cheats, no probes), or none of them (the main menu)."""
        self.stop()
        os.makedirs(self.work, exist_ok=True)
        bindir = os.path.dirname(self.exe)
        exe = os.path.join(bindir, self.process_name + ".exe")
        shutil.copy2(self.exe, exe)
        redata = os.path.join(REPO, "redata")
        shutil.copytree(redata, os.path.join(self.work, "redata"), dirs_exist_ok=True)
        cfg = {"gamePath": self.game_path, "gameVersion": 2, "musicEnabled": False, "soundEnabled": False,
               "userPath": os.path.join(self.work, "profile"), "windowWidth": self.window[0], "windowHeight": self.window[1],
               "agentControl": {"enabled": True, "port": self.port}}
        if skirmish or save:
            bench = {"saveGame": skirmish or save, "warmupFrames": 10 ** 9, "frames": 10}
            if skirmish and slots:
                bench["slots"] = slots
            if cheat:
                bench["skirmishSettings"] = {"cheatMode": 1}
            cfg["benchmark"] = bench
        elif host_solo:
            cfg["lobbyTest"] = {"host": host_solo, "solo": True}
            if slots and "|" in slots and ";" in slots:
                cfg["lobbyTest"]["slots"] = slots
            cfg["hostBind"] = "127.0.0.1"
        else:
            cfg["benchmark"] = {"saveGame": "none"}  # the main menu, no dialogs
        cfg.update(config or {})
        json.dump(cfg, open(os.path.join(self.work, "wkconfig.json"), "w"), indent=1)
        self.token_file = os.path.join(self.work, "profile", "agent", "token")
        if os.path.exists(self.token_file):
            os.remove(self.token_file)
        self.log = os.path.join(self.work, log_name + ".log")
        self.proc = subprocess.Popen([exe], cwd=self.work, stdout=open(self.log, "w", encoding="latin1"),
                                     stderr=subprocess.STDOUT, creationflags=getattr(subprocess, "DETACHED_PROCESS", 0))
        return self

    def agent(self, timeout=120):
        """The connection (waits for the token)"""
        deadline = time.time() + timeout
        while not (os.path.exists(self.token_file) and os.path.getsize(self.token_file) > 0):
            if time.time() > deadline or (self.proc and self.proc.poll() is not None):
                raise TimeoutError("the game didn't start agent control (see " + self.log + ")")
            time.sleep(0.3)
        self._agent = Agent(open(self.token_file).read().strip(), self.port)
        return self._agent

    def stop(self):
        if self._agent:
            self._agent.close()
            self._agent = None
        if self.proc and self.proc.poll() is None:
            self.proc.kill()
            try:
                self.proc.wait(10)
            except subprocess.TimeoutExpired:
                pass
        self.proc = None
        time.sleep(1)  # the exe copy is free again


def bmp_to_png(path):
    """The game's screenshot as PNG (needs Pillow); the path of the PNG"""
    from PIL import Image
    png = os.path.splitext(path)[0] + ".png"
    Image.open(path).save(png)
    return png
