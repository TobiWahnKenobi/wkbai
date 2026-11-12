"""wkbai agent control as an MCP server (stdio), so that an AI assistant such as Claude Code can
play wkbai through tools: start a game, look at it (observe, screenshot), and click, type and
press keys the way a person does. Everything goes through wkbai_agent.py and the game's agent
control; nothing here gives the game a command directly.

Register it, e.g. in Claude Code:
    claude mcp add wkbai -- python <repo>/tools/agent/wkbai_agent_mcp.py --game-path "<Warrior Kings Battles folder>"

The protocol (JSON-RPC 2.0, one message a line on stdin/stdout) is written out here to need no
package beyond Python and Pillow (for screenshots as images). Licensed under the GNU General
Public License 3 (as wkbai).
"""
import argparse
import base64
import json
import os
import sys
import traceback

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wkbai_agent import Game, AgentError, bmp_to_png  # noqa: E402

PROTOCOL = "2024-11-05"


def obj(props, required=()):
    return {"type": "object", "properties": props, "required": list(required)}


NUM = {"type": "number"}
INT = {"type": "integer"}
STR = {"type": "string"}
BOOL = {"type": "boolean"}
BUTTON = {"type": "string", "enum": ["left", "right", "middle"]}

TOOLS = [
    ("wkbai_start", "Start wkbai with agent control on (its own folder). mode: menu (the main menu), skirmish (single player; slots as the skirmish menu, e.g. H|A|E: you, an AI General, empty; probes allowed) or hosted (a hosted game alone: no probes). Stops a game this server started before.",
     obj({"mode": {"type": "string", "enum": ["menu", "skirmish", "hosted"]}, "level": STR, "slots": STR}, ["mode"])),
    ("wkbai_stop", "Close the game this server started.", obj({})),
    ("wkbai_observe", "What the player sees: time, resources, population, selection, hovered object, chat, and every unit and building the fog of war shows (screen points when in view). scenery: trees and other objects on the screen too.",
     obj({"scenery": BOOL})),
    ("wkbai_ui", "The clickable elements drawn on the screen now (ids such as cmd:Build Farm, tab:ECONOMIC, button:Options, minimap), the hovered object and the selection.", obj({})),
    ("wkbai_screenshot", "The screen as the player sees it now, as an image.", obj({"name": STR})),
    ("wkbai_click", "A mouse click at a window pixel (move, press, release over three frames).", obj({"x": INT, "y": INT, "button": BUTTON}, ["x", "y"])),
    ("wkbai_drag", "A drag with a mouse button held (e.g. a selection box, a formation's facing).", obj({"x0": INT, "y0": INT, "x1": INT, "y1": INT, "button": BUTTON}, ["x0", "y0", "x1", "y1"])),
    ("wkbai_click_ui", "Click a screen element by its id (from wkbai_ui).", obj({"element": STR, "button": BUTTON}, ["element"])),
    ("wkbai_click_object", "Click a unit or building by its id, as a person would (the camera moves there by the minimap if it is off the screen; the click is made only if that object is under the cursor). right: a right-click (an order on it). add: Shift held.",
     obj({"object": INT, "right": BOOL, "add": BOOL}, ["object"])),
    ("wkbai_click_ground", "Click a map point (x, z in metres). right: a right-click (move there, or place the building being placed). add: Shift held. Refused if an object is under the cursor there.",
     obj({"x": NUM, "z": NUM, "right": BOOL, "add": BOOL}, ["x", "z"])),
    ("wkbai_camera", "Move the camera to a map point by a click on the minimap.", obj({"x": NUM, "z": NUM}, ["x", "z"])),
    ("wkbai_key", "Press a key or chord, e.g. P, Ctrl+3, Escape.", obj({"key": STR}, ["key"])),
    ("wkbai_key_action", "Press the key bound to a game action, e.g. pause, speedUp, cameraToSelection, groupSelect1.", obj({"action": STR}, ["action"])),
    ("wkbai_text", "Type text (chat, save names).", obj({"text": STR}, ["text"])),
    ("wkbai_wait", "Let the game run some frames.", obj({"frames": INT}, ["frames"])),
    ("wkbai_probe", "Test data a player can't see (single player in cheat mode, skirmish mode here, or a replay): what=hash (the game state's hash), unit (object: its orders, tasks, path), objects (everything, whatever the fog).",
     obj({"what": {"type": "string", "enum": ["hash", "unit", "objects"]}, "object": INT}, ["what"])),
]


class Server:
    def __init__(self, game_path, exe, work):
        self.game = Game(game_path=game_path, exe=exe, work=work)
        self.agent = None

    def need(self):
        if not self.agent:
            raise AgentError({"error": "no game: call wkbai_start first"})
        return self.agent

    def call(self, name, a):
        if name == "wkbai_start":
            mode = a.get("mode", "menu")
            level = a.get("level", "2 Player 4 Peaks.lvl")
            if mode == "skirmish":
                self.game.start(skirmish=level, slots=a.get("slots", "H|A|E"))
            elif mode == "hosted":
                self.game.start(host_solo=level)
            else:
                self.game.start()
            self.agent = self.game.agent()
            if mode != "menu":
                self.agent.wait_for_game()
            return {"started": mode, "window": self.agent.hello.get("window"), "build": self.agent.hello.get("build")}
        if name == "wkbai_stop":
            self.game.stop()
            self.agent = None
            return {"stopped": True}
        ag = self.need()
        if name == "wkbai_observe":
            return ag.observe(scenery=a.get("scenery", False))
        if name == "wkbai_ui":
            return ag.ui()
        if name == "wkbai_screenshot":
            path = ag.screenshot(a.get("name", "mcp"))
            png = bmp_to_png(path)
            return {"image": base64.b64encode(open(png, "rb").read()).decode(), "path": png}
        if name == "wkbai_click":
            return ag.click(a["x"], a["y"], a.get("button", "left"))
        if name == "wkbai_drag":
            return ag.drag((a["x0"], a["y0"]), (a["x1"], a["y1"]), a.get("button", "left"))
        if name == "wkbai_click_ui":
            return ag.click_ui(a["element"], a.get("button", "left"))
        if name == "wkbai_click_object":
            f = ag.right_click_object if a.get("right") else ag.click_object
            return f(a["object"], a.get("add", False))
        if name == "wkbai_click_ground":
            f = ag.right_click_ground if a.get("right") else ag.click_ground
            return f(a["x"], a["z"], a.get("add", False))
        if name == "wkbai_camera":
            return ag.camera(a["x"], a["z"])
        if name == "wkbai_key":
            return ag.tap(a["key"])
        if name == "wkbai_key_action":
            return ag.key_action(a["action"])
        if name == "wkbai_text":
            return ag.text(a["text"])
        if name == "wkbai_wait":
            return ag.wait(a["frames"])
        if name == "wkbai_probe":
            fields = {"object": a["object"]} if "object" in a else {}
            return ag.probe(a["what"], **fields)
        raise AgentError({"error": "unknown tool " + name})


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--game-path", default=os.environ.get("WKB_GAME_PATH"))
    ap.add_argument("--exe")
    ap.add_argument("--work")
    args = ap.parse_args()
    server = Server(args.game_path, args.exe, args.work)
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
                  "serverInfo": {"name": "wkbai-agent", "version": "1"}}})
        elif method == "tools/list":
            send({"jsonrpc": "2.0", "id": mid, "result": {"tools": [{"name": n, "description": d, "inputSchema": s} for n, d, s in TOOLS]}})
        elif method == "tools/call":
            p = msg.get("params", {})
            try:
                r = server.call(p.get("name"), p.get("arguments") or {})
                if isinstance(r, dict) and "image" in r:
                    content = [{"type": "image", "data": r["image"], "mimeType": "image/png"}, {"type": "text", "text": r["path"]}]
                else:
                    content = [{"type": "text", "text": json.dumps(r)}]
                send({"jsonrpc": "2.0", "id": mid, "result": {"content": content}})
            except AgentError as e:
                send({"jsonrpc": "2.0", "id": mid, "result": {"content": [{"type": "text", "text": json.dumps(e.reply)}], "isError": True}})
            except Exception as e:  # the tool failed, the server goes on
                send({"jsonrpc": "2.0", "id": mid, "result": {"content": [{"type": "text", "text": "".join(traceback.format_exception_only(type(e), e))}], "isError": True}})
        elif method == "ping":
            send({"jsonrpc": "2.0", "id": mid, "result": {}})
        elif mid is not None and method:
            send({"jsonrpc": "2.0", "id": mid, "error": {"code": -32601, "message": "unknown method " + method}})
        # notifications (no id) need no answer
    server.game.stop()


if __name__ == "__main__":
    main()
