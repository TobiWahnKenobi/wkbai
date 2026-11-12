// wkbai - agent control: the local connection an agent plays through
// Licensed under the GNU General Public License 3

#pragma once

struct IRenderer;

// Agent control (docs/engine/agent-control.md in the RE repo), its connection. Off unless
// wkconfig.json's "agentControl": { "enabled": true } (Options > Agent control). On, it listens on
// 127.0.0.1 only ("port", 7790) for one program at a time, which must first send the token the
// game writes to <user folder>\agent\token (new at every start). The protocol is one JSON object
// a line:
//   { "id": 1, "op": "hello", "token": "..." }   first, always
//   { "id": 2, "op": "click", "x": 400, "y": 300, "button": "left" }
// Every reply carries the request's id, and comes once the input's last frame has been
// delivered and the game has run that frame: { "id": 2, "ok": true, "frame": n, "mouse": [x, y] }
// or { "id": 2, "ok": false, "error": "..." }. Events have no id: { "event": "input_taken" },
// { "event": "input_returned" }. The ops (all input, AgentInput's builders; nothing else):
//   status; move x y; down/up button; click/doubleclick x y [button]; drag from [x, y] to [x, y]
//   [button] [steps]; key key ("P", "Left Ctrl") down (bool); tap key ("Ctrl+3", a KeyChord);
//   text text; wheel clicks; wait frames; screenshot [name] (the next frame drawn, as the
//   player sees it, into <user folder>\agent\<name>.bmp: the reply has its path)
// Scripts: "agentControl": { "script": "<file>" } runs a file of such lines (no hello) one after
// the other, each once the one before is done, printing "AGENT script <line>: <reply>"; scripts
// also have "sleep" ms (wall clock) and "harness.quit" (closes the game).
namespace AgentServer {

// reads the setting: starts or stops listening, AgentInput::SetEnabled (at the start and when the
// options are saved)
void ApplySettings();
// once a frame, from HandleWindow before the agent's frame is taken: connections, requests,
// replies, events, the script
void Poll();
bool Listening();
int Port();
// ImGuiImpl_Render, after the frame's interface is drawn: a screenshot the agent asked for
// ("screenshot": the frame as the player sees it, into <user folder>\agent\<name>.bmp)
void TakeScreenshotIfAsked(IRenderer* gfx);

}
