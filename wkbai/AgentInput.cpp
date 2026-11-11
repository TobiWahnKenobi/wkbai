// wkbai - agent control
// Licensed under the GNU General Public License 3

#include "AgentInput.h"
#include "window.h"
#include "interface/KeyBindings.h"
#include "imgui/imgui.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cstring>
#include <deque>

namespace AgentInput {

namespace {
	bool enabled = false;
	bool takenOver = false;
	int handOvers = 0;
	std::deque<Frame> queue;
	SDL_Keymod mods = SDL_KMOD_NONE;
	bool cursorDrawn = false;
	// the frame being delivered: its text events point into it until the next TakeFrame
	Frame delivering;
	uint64_t queuedTotal = 0, deliveredTotal = 0;

	SDL_Event blank(SDL_EventType type)
	{
		SDL_Event e;
		std::memset(&e, 0, sizeof(e));
		e.type = type;
		e.common.timestamp = SDL_GetTicksNS();
		return e;
	}
	Frame one(const SDL_Event& e, const std::string& text = std::string())
	{
		Frame f;
		f.items.push_back({ e, text });
		return f;
	}
	SDL_Keymod modOf(SDL_Scancode sc)
	{
		switch (sc) {
		case SDL_SCANCODE_LCTRL: return SDL_KMOD_LCTRL;
		case SDL_SCANCODE_RCTRL: return SDL_KMOD_RCTRL;
		case SDL_SCANCODE_LSHIFT: return SDL_KMOD_LSHIFT;
		case SDL_SCANCODE_RSHIFT: return SDL_KMOD_RSHIFT;
		case SDL_SCANCODE_LALT: return SDL_KMOD_LALT;
		case SDL_SCANCODE_RALT: return SDL_KMOD_RALT;
		default: return SDL_KMOD_NONE;
		}
	}
	// the window pixels the mouse was last moved to (button events carry them)
	float lastX = 0.0f, lastY = 0.0f;
}

void SetEnabled(bool on)
{
	if (enabled == on)
		return;
	enabled = on;
	takenOver = false;
	mods = SDL_KMOD_NONE;
	ClearQueue();
}

bool Enabled() { return enabled; }
bool HasInput() { return enabled && !takenOver; }

void SetTakenOver(bool taken)
{
	if (takenOver == taken)
		return;
	takenOver = taken;
	handOvers++;
	mods = SDL_KMOD_NONE; // the agent's held keys are let go; window.cpp clears the game's input state
}

bool TakenOver() { return takenOver; }
int HandOverCount() { return handOvers; }

void QueueFrame(Frame frame) { queue.push_back(std::move(frame)); queuedTotal++; }
size_t PendingFrames() { return queue.size(); }
// (the dropped frames count as delivered: nothing waits for them)
void ClearQueue() { deliveredTotal += queue.size(); queue.clear(); }
uint64_t QueuedFrames() { return queuedTotal; }
uint64_t DeliveredFrames() { return deliveredTotal; }

void Move(int x, int y)
{
	SDL_Event e = blank(SDL_EVENT_MOUSE_MOTION);
	e.motion.x = (float)x;
	e.motion.y = (float)y;
	QueueFrame(one(e));
}

void Button(int button, bool down, int clicks)
{
	SDL_Event e = blank(down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP);
	e.button.button = (Uint8)button;
	e.button.down = down;
	e.button.clicks = (Uint8)clicks;
	QueueFrame(one(e));
}

void Click(int x, int y, int button)
{
	Move(x, y);
	Button(button, true);
	Button(button, false);
}

void DoubleClick(int x, int y, int button)
{
	Click(x, y, button);
	Button(button, true, 2);
	Button(button, false, 2);
}

void Drag(int x0, int y0, int x1, int y1, int button, int steps)
{
	steps = std::max(steps, 4);
	Move(x0, y0);
	Button(button, true);
	for (int i = 1; i <= steps; i++)
		Move(x0 + (x1 - x0) * i / steps, y0 + (y1 - y0) * i / steps);
	Button(button, false);
}

void Key(int scancode, bool down)
{
	SDL_Event e = blank(down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP);
	e.key.scancode = (SDL_Scancode)scancode;
	e.key.down = down;
	QueueFrame(one(e));
}

void Tap(int scancode, int chordMods)
{
	std::vector<int> held;
	if (chordMods & KeyChord::CTRL) held.push_back(SDL_SCANCODE_LCTRL);
	if (chordMods & KeyChord::SHIFT) held.push_back(SDL_SCANCODE_LSHIFT);
	if (chordMods & KeyChord::ALT) held.push_back(SDL_SCANCODE_LALT);
	for (int m : held)
		Key(m, true);
	Key(scancode, true);
	Key(scancode, false);
	for (auto it = held.rbegin(); it != held.rend(); ++it)
		Key(*it, false);
}

void Text(const std::string& utf8)
{
	// a character at a time (a UTF-8 sequence kept whole)
	for (size_t i = 0; i < utf8.size();) {
		size_t n = 1;
		const unsigned char c = (unsigned char)utf8[i];
		if (c >= 0xF0) n = 4; else if (c >= 0xE0) n = 3; else if (c >= 0xC0) n = 2;
		QueueFrame(one(blank(SDL_EVENT_TEXT_INPUT), utf8.substr(i, n)));
		i += n;
	}
}

void Wheel(int clicks)
{
	SDL_Event e = blank(SDL_EVENT_MOUSE_WHEEL);
	e.wheel.y = (float)clicks;
	e.wheel.integer_y = clicks;
	e.wheel.mouse_x = lastX;
	e.wheel.mouse_y = lastY;
	QueueFrame(one(e));
}

void Wait(int frames)
{
	for (int i = 0; i < frames; i++)
		QueueFrame(Frame{});
}

bool TakeFrame(std::vector<SDL_Event>& out)
{
	out.clear();
	if (!HasInput() || queue.empty())
		return false;
	delivering = std::move(queue.front());
	queue.pop_front();
	deliveredTotal++;
	for (Frame::Item& item : delivering.items) {
		SDL_Event& e = item.event;
		switch (e.type) {
		case SDL_EVENT_KEY_DOWN:
		case SDL_EVENT_KEY_UP: {
			// as SDL fills them in: the modifier state after the event, the key code from the scancode
			const SDL_Keymod m = modOf(e.key.scancode);
			if (e.type == SDL_EVENT_KEY_DOWN)
				mods = (SDL_Keymod)(mods | m);
			else
				mods = (SDL_Keymod)(mods & ~m);
			e.key.mod = mods;
			e.key.key = SDL_GetKeyFromScancode(e.key.scancode, mods, false);
			break;
		}
		case SDL_EVENT_MOUSE_MOTION:
			e.motion.xrel = e.motion.x - lastX;
			e.motion.yrel = e.motion.y - lastY;
			lastX = e.motion.x;
			lastY = e.motion.y;
			break;
		case SDL_EVENT_MOUSE_BUTTON_DOWN:
		case SDL_EVENT_MOUSE_BUTTON_UP:
			e.button.x = lastX;
			e.button.y = lastY;
			break;
		case SDL_EVENT_TEXT_INPUT:
			e.text.text = item.text.c_str();
			break;
		}
		out.push_back(e);
	}
	return true;
}

int Mods() { return mods; }

void MarkCursorDrawn() { cursorDrawn = true; }

void DrawCursorOverlay()
{
	const bool drawn = cursorDrawn;
	cursorDrawn = false;
	if (!Enabled() || !ImGui::GetCurrentContext())
		return;
	ImDrawList* dl = ImGui::GetForegroundDrawList();
	// the banner: whoever sees this screen (or a screenshot of it) knows an agent may play
	{
		const char* text = takenOver ? "AGENT CONTROL ON - the input is yours (Ctrl+Alt+F12: back to the agent)"
			: "AGENT CONTROL ON - an agent plays this screen (Ctrl+Alt+F12: take over)";
		const ImVec2 size = ImGui::CalcTextSize(text);
		const float w = ImGui::GetIO().DisplaySize.x;
		const ImVec2 p((w - size.x) * 0.5f, 4.0f);
		dl->AddRectFilled(ImVec2(p.x - 10, p.y - 3), ImVec2(p.x + size.x + 10, p.y + size.y + 3), IM_COL32(230, 150, 20, 235), 4.0f);
		dl->AddRect(ImVec2(p.x - 10, p.y - 3), ImVec2(p.x + size.x + 10, p.y + size.y + 3), IM_COL32(0, 0, 0, 255), 4.0f, 0, 1.5f);
		dl->AddText(p, IM_COL32(0, 0, 0, 255), text);
		// a frame around the screen in the same colour
		dl->AddRect(ImVec2(1, 1), ImVec2(w - 1, ImGui::GetIO().DisplaySize.y - 1), IM_COL32(230, 150, 20, 200), 0.0f, 0, 3.0f);
	}
	if (!HasInput())
		return;
	const ImVec2 at((float)UiMouseX(), (float)UiMouseY());
	if (!drawn) {
		// an arrow (the front end, the loading screens: no game cursor there)
		static const ImVec2 shape[] = { {0, 0}, {0, 17}, {4, 13}, {7, 20}, {10, 19}, {7, 12}, {12, 12} };
		ImVec2 pts[std::size(shape)];
		for (size_t i = 0; i < std::size(shape); i++)
			pts[i] = ImVec2(at.x + shape[i].x, at.y + shape[i].y);
		dl->AddConcavePolyFilled(pts, (int)std::size(pts), IM_COL32(255, 255, 255, 255));
		dl->AddPolyline(pts, (int)std::size(pts), IM_COL32(0, 0, 0, 255), ImDrawFlags_Closed, 1.5f);
	}
	// the tag: whoever watches sees that the agent moves this cursor
	const char* tag = "AGENT";
	const ImVec2 size = ImGui::CalcTextSize(tag);
	const ImVec2 p(at.x + 16, at.y + 18);
	dl->AddRectFilled(ImVec2(p.x - 3, p.y - 1), ImVec2(p.x + size.x + 3, p.y + size.y + 1), IM_COL32(230, 150, 20, 230), 3.0f);
	dl->AddText(p, IM_COL32(0, 0, 0, 255), tag);
}

}
