// wkbai - agent control: the local connection an agent plays through
// Licensed under the GNU General Public License 3

#include "AgentServer.h"
#include "AgentInput.h"
#include "AgentUi.h"
#include "BuildIdentity.h"
#include "window.h"
#include "file.h"
#include "settings.h"
#include "interface/KeyBindings.h"
#include "gfx/renderer.h"
#include <nlohmann/json.hpp>
#include <SDL3/SDL.h>
#include <chrono>
#include <cstdio>
#include <algorithm>
#include <deque>
#include <functional>
#include <fstream>
#include <random>
#include <string>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

namespace AgentServer {

namespace {
#ifdef _WIN32
	SOCKET listener = INVALID_SOCKET, peer = INVALID_SOCKET;
	bool wsaStarted = false;
#endif
	int port = 0;
	std::string token;
	bool helloDone = false;
	std::string inbox; // the bytes received and not yet a whole line
	int lastHandOvers = 0;

	// a request waiting for its input to be delivered: the reply goes once the agent's frames
	// up to "serial" have been run
	struct Waiting {
		nlohmann::json id;
		uint64_t serial;
		bool fromScript;
		int line;
		// an aimed action's next step, once these frames have run (instead of the reply)
		std::function<void()> then;
	};
	std::deque<Waiting> waiting;
	// a screenshot asked for: its file, and the request waiting for it
	std::string shotPath;
	bool shotTaken = false;
	Waiting shotRequest;

	// the script: its lines, the next one, and a wall-clock pause
	std::vector<std::string> scriptLines;
	size_t scriptNext = 0;
	bool scriptBusy = false;
	std::chrono::steady_clock::time_point scriptResume;

	void sendLine(const nlohmann::json& j)
	{
#ifdef _WIN32
		if (peer == INVALID_SOCKET)
			return;
		const std::string text = j.dump() + "\n";
		size_t sent = 0;
		while (sent < text.size()) {
			const int n = send(peer, text.data() + sent, (int)(text.size() - sent), 0);
			if (n == SOCKET_ERROR) {
				if (WSAGetLastError() == WSAEWOULDBLOCK) { SDL_Delay(1); continue; }
				return;
			}
			sent += (size_t)n;
		}
#endif
	}

	void reply(const nlohmann::json& j, bool fromScript, int line)
	{
		if (fromScript) {
			printf("AGENT script %d: %s\n", line, j.dump().c_str());
			fflush(stdout);
			scriptBusy = false;
		}
		else
			sendLine(j);
	}

	int buttonOf(const nlohmann::json& req)
	{
		const std::string b = req.value("button", std::string("left"));
		if (b == "left") return SDL_BUTTON_LEFT;
		if (b == "middle") return SDL_BUTTON_MIDDLE;
		if (b == "right") return SDL_BUTTON_RIGHT;
		if (b == "x1") return SDL_BUTTON_X1;
		if (b == "x2") return SDL_BUTTON_X2;
		return 0;
	}

	// Aimed actions (layer 2): where to click is worked out from what the screen shows (AgentUi),
	// then the click is ordinary input. A world click first moves the cursor there; once the game
	// has run that frame, its hover pick must have the aimed object under the cursor (for the
	// ground: nothing), or the click isn't made. A point off the screen first moves the camera by
	// a click on the minimap, as a person would.
	struct Aim {
		nlohmann::json out;
		bool fromScript;
		int line;
		std::function<bool(int&, int&, std::string&)> point; // the screen point now
		bool hasWorld = false;
		float wx = 0, wz = 0;   // where it is on the map (for the camera)
		uint32_t expect = 0;    // the object to be under the cursor; 0: none (the ground)
		bool ground = false;
		int button = SDL_BUTTON_LEFT;
		bool add = false;       // Shift held (add to the selection, queue the order)
		bool cameraMoved = false;
	};

	void queueThen(const Aim& a, std::function<void()> then)
	{
		waiting.push_back({ a.out["id"], AgentInput::QueuedFrames(), a.fromScript, a.line, std::move(then) });
	}

	void aimFail(const Aim& a, const std::string& why, nlohmann::json extra = nlohmann::json::object())
	{
		nlohmann::json out = a.out;
		out["ok"] = false;
		out["error"] = why;
		for (auto& [k, v] : extra.items())
			out[k] = v;
		reply(out, a.fromScript, a.line);
	}

	// a click on the minimap at a map point (the camera then looks there)
	bool minimapPoint(float wx, float wz, int& x, int& y)
	{
		const AgentUi::Minimap& m = AgentUi::GetMinimap();
		if (!m.valid || m.areaWidth <= 0 || m.areaDepth <= 0)
			return false;
		x = (int)((m.x + std::clamp(wx / m.areaWidth, 0.0f, 1.0f) * m.w) * g_uiScale);
		y = (int)((m.y + (1.0f - std::clamp(wz / m.areaDepth, 0.0f, 1.0f)) * m.h) * g_uiScale);
		return true;
	}

	void aimStep(Aim a)
	{
		int x, y;
		std::string why;
		if (!a.point(x, y, why)) {
			int mx, my;
			if (!a.cameraMoved && a.hasWorld && minimapPoint(a.wx, a.wz, mx, my)) {
				a.cameraMoved = true;
				AgentInput::Click(mx, my);
				AgentInput::Wait(2);
				queueThen(a, [a]() { aimStep(a); });
				return;
			}
			aimFail(a, why + (a.cameraMoved ? " (after moving the camera by the minimap)" : ""));
			return;
		}
		if (const AgentUi::Element* e = AgentUi::At(x, y)) {
			aimFail(a, "the interface is in the way: " + e->id, { { "at", { x, y } } });
			return;
		}
		AgentInput::Move(x, y);
		queueThen(a, [a, x, y]() {
			const AgentUi::World* w = AgentUi::GetWorld();
			const uint32_t under = w && w->hovered ? w->hovered() : 0;
			if (a.ground ? under != 0 : under != a.expect) {
				aimFail(a, a.ground ? "an object is under the cursor there" : "another object is under the cursor",
					{ { "at", { x, y } }, { "underCursor", under } });
				return;
			}
			if (a.add) AgentInput::Key(SDL_SCANCODE_LSHIFT, true);
			AgentInput::Button(a.button, true);
			AgentInput::Button(a.button, false);
			if (a.add) AgentInput::Key(SDL_SCANCODE_LSHIFT, false);
			queueThen(a, [a, x, y, under]() {
				nlohmann::json out = a.out;
				out["ok"] = true;
				out["clicked"] = { x, y };
				out["underCursor"] = under;
				out["frame"] = AgentInput::DeliveredFrames();
				reply(out, a.fromScript, a.line);
			});
		});
	}

	// a request: its input queued (the reply waits for it), or answered at once
	void handle(const nlohmann::json& req, bool fromScript, int line)
	{
		nlohmann::json out = { { "id", req.value("id", nlohmann::json()) } };
		auto fail = [&](const std::string& why) {
			out["ok"] = false;
			out["error"] = why;
			reply(out, fromScript, line);
		};
		if (!req.is_object()) { fail("not a JSON object"); return; }
		const std::string op = req.value("op", std::string());
		if (op == "hello") {
			if (fromScript) { fail("a script needs no hello"); return; }
			if (req.value("token", std::string()) != token) { fail("wrong token"); return; }
			helloDone = true;
			out["ok"] = true;
			out["build"] = BuildIdentity::Describe();
			out["window"] = { g_windowWidth, g_windowHeight };
			out["uiScale"] = g_uiScale;
			reply(out, fromScript, line);
			return;
		}
		if (!fromScript && !helloDone) { fail("send hello with the token first"); return; }
		if (op == "status") {
			out["ok"] = true;
			out["hasInput"] = AgentInput::HasInput();
			out["takenOver"] = AgentInput::TakenOver();
			out["pending"] = AgentInput::PendingFrames();
			out["mouse"] = { g_mouseX, g_mouseY };
			out["window"] = { g_windowWidth, g_windowHeight };
			reply(out, fromScript, line);
			return;
		}
		if (fromScript && op == "sleep") {
			scriptResume = std::chrono::steady_clock::now() + std::chrono::milliseconds(req.value("ms", 0));
			out["ok"] = true;
			reply(out, fromScript, line);
			return;
		}
		if (fromScript && op == "harness.quit") {
			out["ok"] = true;
			reply(out, fromScript, line);
			g_windowQuit = true;
			return;
		}
		if (op == "screenshot") {
			std::string name = req.value("name", std::string("screenshot"));
			if (name.empty() || name.size() > 60 || name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_") != std::string::npos) {
				fail("a screenshot's name: letters, digits, - and _ (up to 60)");
				return;
			}
			if (!shotPath.empty()) { fail("a screenshot is already being taken"); return; }
			const std::filesystem::path dir = g_userPath / "agent";
			std::error_code ec;
			std::filesystem::create_directories(dir, ec);
			shotPath = (dir / (name + ".bmp")).string();
			shotTaken = false;
			shotRequest = { out["id"], 0, fromScript, line };
			return;
		}
		// layer 2: what the screen shows, and aimed actions
		if (op == "ui") {
			nlohmann::json list = nlohmann::json::array();
			for (const AgentUi::Element& e : AgentUi::Elements())
				list.push_back({ { "id", e.id }, { "label", e.label }, { "rect", { e.x, e.y, e.w, e.h } }, { "enabled", e.enabled } });
			out["ok"] = true;
			out["elements"] = list;
			if (const AgentUi::World* w = AgentUi::GetWorld()) {
				out["hovered"] = w->hovered ? w->hovered() : 0;
				out["selection"] = w->selection ? w->selection() : std::vector<uint32_t>();
			}
			reply(out, fromScript, line);
			return;
		}
		if (op == "observe") {
			const AgentUi::World* w = AgentUi::GetWorld();
			if (!w || !w->observe) { fail("no game on the screen"); return; }
			out["ok"] = true;
			const nlohmann::json seen = w->observe(req); // (kept: items() of a temporary would dangle)
			for (const auto& [k, v] : seen.items())
				out[k] = v;
			reply(out, fromScript, line);
			return;
		}
		if (op == "probe") {
			const AgentUi::World* w = AgentUi::GetWorld();
			if (!w || !w->probe) { fail("no game on the screen"); return; }
			nlohmann::json r = w->probe(req);
			r["id"] = out["id"];
			reply(r, fromScript, line);
			return;
		}
		if (op == "objects") {
			const AgentUi::World* w = AgentUi::GetWorld();
			if (!w || !w->ownOnScreen) { fail("no game on the screen"); return; }
			nlohmann::json list = nlohmann::json::array();
			for (const auto& s : w->ownOnScreen()) {
				nlohmann::json o = { { "id", s.id }, { "type", s.type }, { "kind", s.character ? "character" : "building" } };
				if (s.onScreen)
					o["screen"] = { s.x, s.y };
				float px, pz;
				if (w->objectPosition && w->objectPosition(s.id, px, pz))
					o["position"] = { px, pz };
				list.push_back(o);
			}
			out["ok"] = true;
			out["player"] = w->player ? w->player() : 0;
			out["objects"] = list;
			reply(out, fromScript, line);
			return;
		}
		if (op == "clickui") {
			const AgentUi::Element* e = AgentUi::Find(req.value("element", std::string()));
			if (!e) { fail("no such element on the screen: " + req.value("element", std::string())); return; }
			if (!e->enabled) { fail("the element is disabled: " + e->id); return; }
			const int b = buttonOf(req);
			if (!b) { fail("button is left, middle, right, x1 or x2"); return; }
			const int x = e->x + e->w / 2, y = e->y + e->h / 2;
			AgentInput::Click(x, y, b);
			Aim a{ out, fromScript, line };
			queueThen(a, [a, x, y]() {
				nlohmann::json o = a.out;
				o["ok"] = true;
				o["clicked"] = { x, y };
				o["frame"] = AgentInput::DeliveredFrames();
				reply(o, a.fromScript, a.line);
			});
			return;
		}
		if (op == "clickobject" || op == "rightclickobject" || op == "clickground" || op == "rightclickground") {
			const AgentUi::World* w = AgentUi::GetWorld();
			if (!w || !w->objectPoint) { fail("no game on the screen"); return; }
			Aim a{ out, fromScript, line };
			a.button = op[0] == 'r' ? SDL_BUTTON_RIGHT : SDL_BUTTON_LEFT;
			a.add = req.value("add", false);
			if (op.find("object") != std::string::npos) {
				const uint32_t id = req.value("object", 0u);
				a.expect = id;
				a.point = [id](int& x, int& y, std::string& why) {
					const AgentUi::World* w = AgentUi::GetWorld();
					return w && w->objectPoint && w->objectPoint(id, x, y, why);
				};
				if (w->objectPosition)
					a.hasWorld = w->objectPosition(id, a.wx, a.wz);
			}
			else {
				if (!req.contains("x") || !req.contains("z")) { fail(op + " needs x and z (metres)"); return; }
				const float gx = req.value("x", 0.0f), gz = req.value("z", 0.0f);
				a.ground = true;
				a.hasWorld = true;
				a.wx = gx; a.wz = gz;
				a.point = [gx, gz](int& x, int& y, std::string& why) {
					const AgentUi::World* w = AgentUi::GetWorld();
					return w && w->groundPoint && w->groundPoint(gx, gz, x, y, why);
				};
			}
			aimStep(a);
			return;
		}
		if (op == "camera") {
			int mx, my;
			if (!minimapPoint(req.value("x", 0.0f), req.value("z", 0.0f), mx, my)) { fail("no minimap on the screen"); return; }
			AgentInput::Click(mx, my);
			AgentInput::Wait(2);
			waiting.push_back({ out["id"], AgentInput::QueuedFrames(), fromScript, line });
			return;
		}
		if (op == "keyaction") {
			const KeyActionInfo* info = FindKeyAction(req.value("action", std::string()));
			if (!info) { fail("no such key action: " + req.value("action", std::string())); return; }
			const KeyChord c = GetKeyBinding(info->action, 0).empty() ? GetKeyBinding(info->action, 1) : GetKeyBinding(info->action, 0);
			if (c.empty()) { fail(std::string("no key bound to ") + info->id); return; }
			AgentInput::Tap(c.key, c.mods);
			out["key"] = c.text();
			waiting.push_back({ out["id"], AgentInput::QueuedFrames(), fromScript, line });
			return;
		}
		auto xy = [&](const char* key) -> std::pair<int, int> {
			const nlohmann::json p = req.value(key, nlohmann::json());
			return p.is_array() && p.size() == 2 ? std::pair<int, int>{ p[0].get<int>(), p[1].get<int>() } : std::pair<int, int>{ -1, -1 };
		};
		const int x = req.value("x", -1), y = req.value("y", -1);
		const uint64_t before = AgentInput::QueuedFrames();
		if (op == "move" || op == "click" || op == "doubleclick") {
			if (x < 0 || y < 0) { fail(op + " needs x and y"); return; }
			const int b = buttonOf(req);
			if (!b) { fail("button is left, middle, right, x1 or x2"); return; }
			if (op == "move") AgentInput::Move(x, y);
			else if (op == "click") AgentInput::Click(x, y, b);
			else AgentInput::DoubleClick(x, y, b);
		}
		else if (op == "down" || op == "up") {
			const int b = buttonOf(req);
			if (!b) { fail("button is left, middle, right, x1 or x2"); return; }
			AgentInput::Button(b, op == "down");
		}
		else if (op == "drag") {
			const auto [x0, y0] = xy("from");
			const auto [x1, y1] = xy("to");
			const int b = buttonOf(req);
			if (x0 < 0 || x1 < 0 || !b) { fail("drag needs from [x, y], to [x, y] and a button"); return; }
			AgentInput::Drag(x0, y0, x1, y1, b, req.value("steps", 6));
		}
		else if (op == "key") {
			const SDL_Scancode sc = SDL_GetScancodeFromName(req.value("key", std::string()).c_str());
			if (sc == SDL_SCANCODE_UNKNOWN) { fail("no such key: " + req.value("key", std::string())); return; }
			AgentInput::Key(sc, req.value("down", true));
		}
		else if (op == "tap") {
			const KeyChord c = KeyChord::parse(req.value("key", std::string()));
			if (c.empty()) { fail("no such key: " + req.value("key", std::string())); return; }
			AgentInput::Tap(c.key, c.mods);
		}
		else if (op == "text") {
			const std::string t = req.value("text", std::string());
			if (t.empty() || t.size() > 1000) { fail("text needs 1 to 1000 bytes"); return; }
			AgentInput::Text(t);
		}
		else if (op == "wheel")
			AgentInput::Wheel(req.value("clicks", 0));
		else if (op == "wait") {
			const int frames = req.value("frames", 1);
			if (frames < 1 || frames > 100000) { fail("wait needs 1 to 100000 frames"); return; }
			AgentInput::Wait(frames);
		}
		else { fail("unknown op: " + op); return; }
		if (AgentInput::QueuedFrames() == before) { fail("nothing to do"); return; }
		waiting.push_back({ out["id"], AgentInput::QueuedFrames(), fromScript, line });
	}

	void closePeer(const char* why)
	{
#ifdef _WIN32
		if (peer != INVALID_SOCKET) {
			closesocket(peer);
			peer = INVALID_SOCKET;
			printf("Agent control: the agent's connection closed (%s)\n", why);
			fflush(stdout);
		}
#endif
		helloDone = false;
		inbox.clear();
		// its input still to come is dropped; its waiting replies with it
		for (auto it = waiting.begin(); it != waiting.end();)
			it = it->fromScript ? std::next(it) : waiting.erase(it);
		if (scriptLines.empty())
			AgentInput::ClearQueue();
	}

	void stop()
	{
		closePeer("agent control turned off");
#ifdef _WIN32
		if (listener != INVALID_SOCKET) {
			closesocket(listener);
			listener = INVALID_SOCKET;
		}
#endif
		port = 0;
	}

	std::string newToken()
	{
		std::random_device rd;
		char hex[33];
		snprintf(hex, sizeof(hex), "%08x%08x%08x%08x", rd(), rd(), rd(), rd());
		return hex;
	}
}

void TakeScreenshotIfAsked(IRenderer* gfx)
{
	if (shotPath.empty() || shotTaken || !gfx)
		return;
	gfx->SaveScreenshot(shotPath.c_str());
	shotTaken = true;
}

bool Listening()
{
#ifdef _WIN32
	return listener != INVALID_SOCKET;
#else
	return false;
#endif
}

int Port() { return port; }

void ApplySettings()
{
	const nlohmann::json cfg = g_settings.value("agentControl", nlohmann::json::object());
	const bool on = cfg.is_object() && cfg.value("enabled", false);
	const int wantPort = cfg.is_object() ? cfg.value("port", 7790) : 7790;
	AgentInput::SetEnabled(on);
	if (!on) {
		if (Listening())
			printf("Agent control: off\n");
		stop();
		scriptLines.clear();
		return;
	}
	// the script, read once
	if (const std::string script = cfg.value("script", std::string()); !script.empty() && scriptLines.empty() && scriptNext == 0) {
		std::ifstream in(script);
		for (std::string l; std::getline(in, l);)
			scriptLines.push_back(l);
		printf("Agent control: script %s, %zu lines\n", script.c_str(), scriptLines.size());
	}
#ifdef _WIN32
	if (Listening() && wantPort == port)
		return;
	stop();
	if (!wsaStarted) {
		WSADATA wsa;
		wsaStarted = WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
	}
	listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	sockaddr_in addr{};
	addr.sin_family = AF_INET;
	addr.sin_port = htons((u_short)wantPort);
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // this computer only
	u_long nonBlocking = 1;
	if (listener == INVALID_SOCKET || bind(listener, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR
		|| listen(listener, 1) == SOCKET_ERROR || ioctlsocket(listener, FIONBIO, &nonBlocking) == SOCKET_ERROR) {
		printf("Agent control: can't listen on 127.0.0.1:%d (error %d); the agent's input works from a script only\n", wantPort, WSAGetLastError());
		if (listener != INVALID_SOCKET)
			closesocket(listener);
		listener = INVALID_SOCKET;
		return;
	}
	port = wantPort;
	token = newToken();
	const std::filesystem::path dir = g_userPath / "agent";
	std::error_code ec;
	std::filesystem::create_directories(dir, ec);
	std::ofstream(dir / "token", std::ios::binary | std::ios::trunc) << token;
	printf("Agent control: on, listening on 127.0.0.1:%d (the token is in %s)\n", port, (dir / "token").string().c_str());
	fflush(stdout);
#endif
}

void Poll()
{
	if (!AgentInput::Enabled())
		return;
	// replies whose input the game has run (delivered in an earlier frame: this frame's comes after)
	while (!waiting.empty() && AgentInput::DeliveredFrames() >= waiting.front().serial) {
		const Waiting w = waiting.front();
		waiting.pop_front();
		if (w.then)
			w.then();
		else
			reply({ { "id", w.id }, { "ok", true }, { "frame", AgentInput::DeliveredFrames() }, { "mouse", { g_mouseX, g_mouseY } } }, w.fromScript, w.line);
	}
	// the screenshot, taken in the frame before
	if (!shotPath.empty() && shotTaken) {
		reply({ { "id", shotRequest.id }, { "ok", true }, { "path", shotPath }, { "window", { g_windowWidth, g_windowHeight } } }, shotRequest.fromScript, shotRequest.line);
		shotPath.clear();
		shotTaken = false;
	}
	// the input changed hands
	if (AgentInput::HandOverCount() != lastHandOvers) {
		lastHandOvers = AgentInput::HandOverCount();
		sendLine({ { "event", AgentInput::TakenOver() ? "input_taken" : "input_returned" } });
	}
	// the script's next line, once the one before is done
	if (scriptNext < scriptLines.size() && !scriptBusy && std::chrono::steady_clock::now() >= scriptResume) {
		const int line = (int)scriptNext + 1;
		const std::string text = scriptLines[scriptNext++];
		if (text.find_first_not_of(" \t\r") != std::string::npos && text[text.find_first_not_of(" \t\r")] != '#') {
			scriptBusy = true;
			handle(nlohmann::json::parse(text, nullptr, false), true, line);
		}
	}
#ifdef _WIN32
	if (listener == INVALID_SOCKET)
		return;
	// a new connection: one agent at a time
	for (SOCKET s; (s = accept(listener, nullptr, nullptr)) != INVALID_SOCKET;) {
		if (peer != INVALID_SOCKET) {
			const char* busy = "{\"ok\":false,\"error\":\"another agent is connected\"}\n";
			send(s, busy, (int)strlen(busy), 0);
			closesocket(s);
			continue;
		}
		u_long nonBlocking = 1;
		ioctlsocket(s, FIONBIO, &nonBlocking);
		const int noDelay = 1;
		setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char*)&noDelay, sizeof(noDelay));
		peer = s;
		helloDone = false;
		printf("Agent control: an agent connected\n");
		fflush(stdout);
	}
	if (peer == INVALID_SOCKET)
		return;
	char buf[4096];
	for (;;) {
		const int n = recv(peer, buf, sizeof(buf), 0);
		if (n > 0) {
			inbox.append(buf, (size_t)n);
			if (inbox.size() > (1 << 20)) { closePeer("too much without a line end"); return; }
			continue;
		}
		if (n == 0) { closePeer("the agent closed it"); return; }
		if (WSAGetLastError() != WSAEWOULDBLOCK) { closePeer("error"); return; }
		break;
	}
	for (size_t nl; (nl = inbox.find('\n')) != std::string::npos;) {
		const std::string text = inbox.substr(0, nl);
		inbox.erase(0, nl + 1);
		if (text.find_first_not_of(" \t\r") == std::string::npos)
			continue;
		const nlohmann::json req = nlohmann::json::parse(text, nullptr, false);
		const bool wasHello = helloDone;
		handle(req, false, 0);
		// a wrong first line ends the connection
		if (!wasHello && !helloDone) { closePeer("no hello with the right token"); return; }
	}
#endif
}

}
