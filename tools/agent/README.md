# Agent control tools

A program, such as an AI assistant, can play wkbai the way a person does. It moves the mouse, clicks and presses keys, and the game turns that into commands exactly as it does for a person. The option is off by default (Options > Agent control), and every player sees when it is on. The protocol is described in `wkbai/AgentServer.h`, and the design in the RE repo's `docs/engine/agent-control.md`.

| File | What it is |
|---|---|
| `wkbai_agent.py` | The Python client. `Game` starts wkbai with agent control on, in a folder of its own (never your profile). `Agent` sends the protocol's ops (input, aimed clicks, `observe`, `probe`, `screenshot`) and offers composites such as `box_select` and `place`. |
| `scenarios.py` | The first evaluation scenarios: `menus`, `pathfinding`, `building`, `versus-ai`. Each prints a JSON result and exits with 0 when its checks hold. |
| `wkbai_agent_mcp.py` | An MCP server (stdio) offering the same as tools (`wkbai_start`, `wkbai_observe`, `wkbai_screenshot`, `wkbai_click_object`, and so on), so that an assistant can play from a chat. It needs no package beyond Python, plus Pillow for screenshots. |

Running them:

```
set WKB_GAME_PATH=C:\Games\Steam\steamapps\common\Warrior Kings Battles
python tools\agent\scenarios.py pathfinding
claude mcp add wkbai -- python tools\agent\wkbai_agent_mcp.py
```

The game runs in its own window and doesn't need your mouse or the foreground. Ctrl+Alt+F12 takes the input over from the agent, and pressing it again gives it back.

Probes (paths, orders, the game's hash, objects hidden by the fog of war) work only where cheat keys do: a single-player game in cheat mode, the test runs (`skirmish` mode here), or a replay.
