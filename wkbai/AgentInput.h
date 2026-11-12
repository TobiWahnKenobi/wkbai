// wkbai - agent control
// Licensed under the GNU General Public License 3

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include <SDL3/SDL_events.h>

// Agent control (docs/engine/agent-control.md in the RE repo): an agent plays the game as a person
// does, through input only. Its mouse moves, button presses, keys and typed text are SDL events
// that HandleWindow (window.cpp) passes through the same code as the operating system's
// (ProcessInputEvent: the g_mouse*/g_key* state, ImGui), so the interface, the Lua HUD and the
// key bindings turn them into game commands exactly as a person's. Nothing here sends a command.
//
// The agent's events come in frames: each HandleWindow call delivers one queued frame, so a click
// is "move" in one frame, "down" in the next and "up" in the one after, as the interface needs
// (the hover pick of a frame is used by the next frame's click, buttons fire on the release).
//
// Off unless SetEnabled(true). While enabled and not taken over, the agent has the input: the
// operating system's mouse and keyboard events for the window are dropped, and the agent's own
// modifier keys stand for Ctrl, Shift and Alt. The take-over key (KeyAction::AgentTakeOver,
// Ctrl+Alt+F12) gives the input to the person at the keyboard and back.
namespace AgentInput {

// the option (14.2 ties it to the settings); turning it off empties the queue
void SetEnabled(bool on);
bool Enabled();
// the agent has the input: enabled and not taken over by the person at the keyboard
bool HasInput();
// the person took the input (true) or gave it back (false); held keys and buttons are let go
void SetTakenOver(bool taken);
bool TakenOver();
// how often the input changed hands (for the agent's "input_taken" / "input_returned" events)
int HandOverCount();

// one frame of the agent's input; a frame with no events is a wait
struct Frame {
	struct Item {
		SDL_Event event;
		std::string text; // a text event's text (its pointer is set when delivered)
	};
	std::vector<Item> items;
	uint64_t serial = 0;    // its number among all frames queued (QueuedFrames when it was queued)
	uint64_t notBefore = 0; // held until then (SDL_GetTicks ms): the reaction delay
};

// The queue. Builders add frames at the end; HandleWindow takes one per frame while the agent has
// the input (taken over: the queue waits).
void QueueFrame(Frame frame);
size_t PendingFrames();
void ClearQueue();
// frames ever queued and ever delivered (AgentServer: a request's reply waits until its last
// frame is delivered)
uint64_t QueuedFrames();
uint64_t DeliveredFrames();

// Human limits ("agentControl": "apm", "reactionMs"; 0: none), for fair games against people or
// the AI: at most apm actions a minute (an action: a mouse button or a key going down, a
// modifier key not counted), a frame with one held until its time; and a request's input starts
// reactionMs after the request came (HoldFrom). Held frames wait, the game goes on.
void SetLimits(int apm, int reactionMs);
int Apm();
int ReactionMs();
// the frames from this serial on wait until then (ms, SDL_GetTicks)
void HoldFrom(uint64_t serial, uint64_t untilMs);

// builders, in window pixels (g_mouseX/Y): each adds the frames a person's hand would take
// button: 1 left, 2 middle, 3 right, 4 x1, 5 x2 (SDL_BUTTON_*); key: an SDL_Scancode
void Move(int x, int y);                          // 1 frame
void Button(int button, bool down, int clicks = 1); // 1 frame
void Click(int x, int y, int button = 1);         // move, down, up: 3 frames
void DoubleClick(int x, int y, int button = 1);   // move, down, up, down (clicks 2), up: 5 frames
// down at (x0, y0), moves to (x1, y1) over steps frames (at least 4), up: steps + 3 frames
void Drag(int x0, int y0, int x1, int y1, int button = 1, int steps = 6);
void Key(int scancode, bool down);                // 1 frame
// a key with modifiers (KeyChord::CTRL | SHIFT | ALT): the modifiers down, the key down, up,
// the modifiers up, a frame each
void Tap(int scancode, int mods = 0);
void Text(const std::string& utf8);               // one text event per character, a frame each
void Wheel(int clicks);                           // 1 frame
void Wait(int frames);

// window.cpp: the next queued frame's events, filled in (key codes, modifiers, text pointers)
// and in order; false when the agent hasn't the input or nothing is queued
bool TakeFrame(std::vector<SDL_Event>& out);
// the agent's modifier keys as an SDL_Keymod (left/right Ctrl, Shift, Alt it holds down)
int Mods();

// the drawn cursor (the OS cursor stays where the person's mouse is): ClientInterface draws the
// game cursor's picture and marks it drawn; ImGuiImpl_Render draws an arrow otherwise, and the
// "AGENT" tag beside either; while agent control is on, a banner at the top of the screen says so
void MarkCursorDrawn();
void DrawCursorOverlay();

}
