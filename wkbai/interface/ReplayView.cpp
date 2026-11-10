// wkbai - watching a recording: its playback controls, and the game's events announced on the
// screen and marked on the minimap for a while

#include "ReplayView.h"
#include "GameChronicle.h"
#include "../client.h"
#include "../Camera.h"
#include "../server.h"
#include "../tags.h"
#include "../file.h"
#include "../settings.h"
#include "../window.h"
#include "../gameset/gameset.h"
#include "../gfx/TextureCache.h"
#include "../gfx/BitmapFont.h"
#include "../util/FrameStats.h"
#include "../util/StriCompare.h"
#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_scancode.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <set>

namespace {
	constexpr float SPEEDS[] = { 0.5f, 1.0f, 2.0f, 4.0f, 8.0f, 16.0f };
	constexpr uint32_t TEXT = 0xFFE6E6E6, GOLD = 0xFF46C8E6, DIM_TEXT = 0xFFA0A0A0;
	constexpr float ANNOUNCE_SECONDS = 6.0f, FADE_SECONDS = 1.0f;
	constexpr size_t ANNOUNCE_LINES = 6;

	std::string clock(uint32_t ms)
	{
		const uint32_t s = ms / 1000;
		char buf[24];
		if (s >= 3600)
			snprintf(buf, sizeof(buf), "%u:%02u:%02u", s / 3600, s / 60 % 60, s % 60);
		else
			snprintf(buf, sizeof(buf), "%u:%02u", s / 60, s % 60);
		return buf;
	}

	std::string speedText(float speed)
	{
		char buf[16];
		if (speed < 1.0f)
			snprintf(buf, sizeof(buf), "%.1fx", speed);
		else
			snprintf(buf, sizeof(buf), "%gx", std::round(speed * 10.0f) / 10.0f);
		return buf;
	}

	uint32_t withAlpha(uint32_t colour, float alpha)
	{
		const uint32_t a = (uint32_t)std::clamp((float)(colour >> 24) * alpha, 0.0f, 255.0f);
		return (colour & 0x00FFFFFF) | (a << 24);
	}

	bool endsWith(const std::string& s, const char* suffix)
	{
		const size_t n = strlen(suffix);
		return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
	}

	// "a Barracks", "3 Farms", "2 Swordsmen": a count of a blueprint
	std::string counted(int n, const std::string& name)
	{
		if (n == 1)
			return (std::strchr("AEIOUaeiou", name.empty() ? 'x' : name[0]) ? "an " : "a ") + name;
		std::string plural = name;
		if (endsWith(plural, "man"))
			plural.replace(plural.size() - 3, 3, "men");
		else if (endsWith(plural, "y") && !endsWith(plural, "ey"))
			plural.replace(plural.size() - 1, 1, "ies");
		else if (!endsWith(plural, "s"))
			plural += "s";
		return std::to_string(n) + " " + plural;
	}

	// wall pieces, put up and taken down again as a wall is built: not worth a line each
	bool isWallPiece(const std::string& name)
	{
		if (name.find("Gatehouse") != std::string::npos || name.find("Tower") != std::string::npos)
			return false;
		return name.find("Stockade") != std::string::npos || name.find("Wall") != std::string::npos
			|| name.find("Paling") != std::string::npos || name.find("Palisade") != std::string::npos;
	}
}

ReplayView::ReplayView() = default;
ReplayView::~ReplayView() = default;

const char* ReplayView::iconTexture(Icon icon)
{
	switch (icon) {
	case Icon::Battle: return "Interface\\Icons\\SIC_Attack.tga";
	case Icon::SeaBattle: return "Interface\\Icons\\SIC_Unload_Boat.tga";
	case Icon::Siege: return "Interface\\Icons\\SIC_Fire_Greek_Fire.tga";
	case Icon::Raid: return "Interface\\Icons\\SIC_Skirmish.tga";
	case Icon::Founded: return "Interface\\Icons\\Buildings\\Manor.tga";
	case Icon::Built: return "Interface\\Icons\\SIC_Build_Civic.tga";
	case Icon::Destroyed: return "Interface\\Icons\\SIC_Arson.tga";
	case Icon::Trained: return "Interface\\Icons\\SIC_Train.tga";
	case Icon::Captured: return "Interface\\Icons\\SIC_Bell.tga";
	case Icon::Defeated: return nullptr; // a drawn cross
	}
	return nullptr;
}

void ReplayView::loadFonts(IRenderer* gfx)
{
	fontsLoaded = true;
	auto load = [gfx](const char* dir) -> std::unique_ptr<BitmapFont> {
		std::unique_ptr<std::vector<std::string>> files(ListFiles(dir));
		if (files)
			for (const std::string& f : *files)
				if (f.size() > 5 && StrCICompare(std::string_view(f).substr(f.size() - 5), ".font") == 0) {
					auto font = std::make_unique<BitmapFont>();
					if (font->load(gfx, std::string(dir) + "\\" + f))
						return font;
				}
		return nullptr;
	};
	titleFont = load("Fonts\\Morpheus\\20");
	textFont = load("Fonts\\Proportional\\20");
}

uint32_t ReplayView::playerColour(uint32_t player) const
{
	int c = 0;
	if (auto it = chronicle->playerColours.find(player); it != chronicle->playerColours.end())
		c = it->second;
	else if (CommonGameObject* p = client ? client->findObject(player) : nullptr)
		c = p->color;
	return palette[(c >= 0 && c < 9) ? c : 0];
}

std::string ReplayView::playerName(uint32_t player) const
{
	// an AI's General ("Saint Jane Kingmaker" -> "Saint Jane") before the map's "player 1"
	std::string name;
	if (auto it = chronicle->playerNames.find(player); it != chronicle->playerNames.end())
		name = it->second;
	if (client)
		if (CommonGameObject* p = client->findObject(player)) {
			std::string general = p->blueprint->name;
			if (endsWith(general, " Kingmaker"))
				general.resize(general.size() - 10);
			const bool mapName = name.empty() || StrCICompare(std::string_view(name).substr(0, std::min<size_t>(7, name.size())), "player ") == 0;
			if (mapName && general != "Human Player" && general != "Neutral Player")
				name = general;
			else if (name.empty())
				name = p->name;
		}
	return name.empty() ? "Player " + std::to_string(player) : name;
}

std::string ReplayView::placeAt(const Vector3& position) const
{
	const MapRegions::Region* r = chronicle->regions.at(position);
	return r ? r->name : std::string();
}

int ReplayView::logGroup(Icon icon)
{
	switch (icon) {
	case Icon::Battle: case Icon::SeaBattle: case Icon::Siege: case Icon::Raid: return 0;
	case Icon::Founded: case Icon::Defeated: case Icon::Captured: return 1;
	case Icon::Built: case Icon::Destroyed: return 2;
	case Icon::Trained: return 3;
	}
	return 2;
}

void ReplayView::addLine(std::vector<Part> parts, Icon icon, const std::string& iconPath, const Vector3& position)
{
	const uint32_t played = server ? (uint32_t)server->replayPlayedMs : 0;
	if (BenchmarkEnabled()) {
		std::string line;
		for (const Part& p : parts)
			line += p.text;
		printf("REPLAY event at %s: %s (%.0f, %.0f)\n", clock(played).c_str(), line.c_str(), position.x, position.z);
	}
	log.push_back({ played, parts, icon, iconPath, position });
	announcements.push_back({ std::move(parts), icon, iconPath, SDL_GetTicks() });
	while (announcements.size() > ANNOUNCE_LINES)
		announcements.pop_front();
}

void ReplayView::lookAt(Client* client, const Vector3& position)
{
	// the camera looks at it from where it is now, as high up (as the timeline, F7)
	Camera& cam = client->camera;
	const Vector3 dir = cam.direction;
	const float down = std::max(-dir.y, 0.2f);
	const float reach = (cam.position.y - position.y) / down;
	cam.position.x = position.x - dir.x * reach;
	cam.position.z = position.z - dir.z * reach;
}

void ReplayView::announce(std::vector<Part> parts, Icon icon, const Vector3& position, uint32_t colour, const std::string& iconPath, int chronicleEvent)
{
	const uint64_t now = SDL_GetTicks();
	addLine(std::move(parts), icon, iconPath, position);
	// one marker per place and kind, and per fight (a raid grown into a battle): a new one there
	// starts it again
	markers.erase(std::remove_if(markers.begin(), markers.end(), [&](const Marker& m) {
		return chronicleEvent >= 0 ? m.chronicleEvent == chronicleEvent
			: m.chronicleEvent < 0 && m.icon == icon && (m.position - position).sqlen2xz() < 20.0f * 20.0f; }), markers.end());
	const float seconds = (icon == Icon::Built || icon == Icon::Trained) ? 4.0f : 8.0f;
	markers.push_back({ icon, iconPath, position, colour, now, seconds, chronicleEvent });
}

void ReplayView::announceHero(Client* client, uint32_t id, const uint32_t* palette)
{
	auto it = client->unitInfo.find(id);
	if (it == client->unitInfo.end())
		return;
	ClientGameObject* obj = client->findObject(id);
	const CommonGameObject* player = obj ? obj->getPlayer() : nullptr;
	const int c = player ? player->color : 0;
	const uint32_t colour = palette[(c >= 0 && c < 9) ? c : 0];
	std::vector<Part> parts{ { it->second.birthName, colour },
		{ " has slain " + std::to_string(obj ? obj->kills : 0) + " enemies and is now ", TEXT }, { it->second.name, colour } };
	announce(std::move(parts), Icon::Trained, obj ? obj->position : Vector3(), colour, "Interface\\InGame\\Info Panel\\Sword.tga", -1);
}

void ReplayView::announceFallen(const std::string& text, uint32_t colour, const Vector3& position)
{
	announce({ { text, colour } }, Icon::Defeated, position, colour, std::string(), -1);
}

void ReplayView::update(Client* client, Server* srv, const GameChronicle& chron, const uint32_t* colours)
{
	server = srv;
	chronicle = &chron;
	palette = colours;
	this->client = client;
	if (!client || !client->getLevel() || !client->gameSet)
		return;
	const float now = client->timeManager.currentTime;
	// another game: start over (not on the clock put back a few seconds by the time sync)
	if (client->getLevel() != level || chron.events.size() < seenEvents.size()) {
		level = client->getLevel();
		seenEvents.clear();
		known.clear();
		trained.clear();
		alive.clear();
		markers.clear();
		announcements.clear();
		log.clear();
		firstLook = true;
		nextLook = 0.0f;
	}
	lastGameTime = now;

	// the chronicle's events: new ones, raids grown into battles, battles over
	for (size_t i = 0; i < chron.events.size(); i++) {
		const GameChronicle::Event& e = chron.events[i];
		using Kind = GameChronicle::Kind;
		const bool fight = e.kind == Kind::Battle || e.kind == Kind::Siege || e.kind == Kind::Raid;
		if (i >= seenEvents.size())
			seenEvents.push_back({ -1 });
		SeenEvent& seen = seenEvents[i];
		if (seen.kind != (int)e.kind) {
			seen.kind = (int)e.kind;
			// (a hero fallen is announced from the chronicle's fallen list)
			if (e.kind == Kind::Fallen)
				continue;
			std::vector<Part> parts;
			Icon icon = Icon::Battle;
			std::string iconPath;
			uint32_t colour = 0xFF3030E0;
			if (fight) {
				// at sea when most of the fighters there are boats
				int boats = 0, others = 0;
				auto walk = [&](CommonGameObject* obj, auto& self) -> void {
					for (const auto& [t, ch] : obj->children)
						for (CommonGameObject* o : ch) {
							if (o->blueprint->bpClass == Tags::GAMEOBJCLASS_CHARACTER && !(o->flags & CommonGameObject::fTerminated)
								&& (o->position - e.position).sqlen2xz() < 60.0f * 60.0f) {
								const std::string& n = o->blueprint->name;
								(n.find("Boat") != std::string::npos || n.find("Ship") != std::string::npos ? boats : others)++;
							}
							self(o, self);
						}
				};
				walk(client->getLevel(), walk);
				const bool atSea = boats > others;
				std::string title = e.title();
				if (atSea && e.kind == Kind::Battle)
					title = "Sea battle off " + (e.place.empty() ? std::string("the coast") : e.place);
				parts.push_back({ title, GOLD });
				icon = e.kind == Kind::Siege ? Icon::Siege : e.kind == Kind::Raid ? Icon::Raid : atSea ? Icon::SeaBattle : Icon::Battle;
				// the sides
				std::vector<uint32_t> sides(e.players.begin(), e.players.end());
				for (size_t k = 0; k < sides.size(); k++) {
					parts.push_back({ k == 0 ? ": " : (k + 1 == sides.size() ? " and " : ", "), TEXT });
					parts.push_back({ playerName(sides[k]), playerColour(sides[k]) });
				}
				if (e.kind != Kind::Raid && e.size > 0)
					parts.push_back({ " (" + std::to_string(e.size) + " fighting)", DIM_TEXT });
			}
			else {
				const uint32_t player = e.players.empty() ? 0 : *e.players.begin();
				colour = playerColour(player);
				parts.push_back({ playerName(player), colour });
				if (e.kind == Kind::Founded) {
					parts.push_back({ " founded " + counted(1, e.what), TEXT });
					icon = Icon::Founded;
					const std::string path = "Interface\\Icons\\Buildings\\" + e.what + ".tga";
					if (FileExists(path.c_str()))
						iconPath = path;
				}
				else {
					parts.push_back({ " lost " + counted(1, e.what), TEXT });
					icon = Icon::Destroyed;
				}
				if (!e.place.empty())
					parts.push_back({ " at " + e.place, GOLD });
			}
			announce(std::move(parts), icon, e.position, colour, iconPath, fight ? (int)i : -1);
		}
		// a battle or siege over (the chronicle joins fights 30 s apart): who lost what
		if (fight && e.kind != Kind::Raid && !seen.over && now - e.endTime > 35.0f) {
			seen.over = true;
			std::vector<Part> parts{ { e.title() + " is over", GOLD } };
			bool first = true;
			for (uint32_t p : e.players) {
				int lost = 0;
				if (auto it = e.lost.find(p); it != e.lost.end())
					for (const auto& [type, n] : it->second)
						lost += n;
				parts.push_back({ first ? ": " : ", ", TEXT });
				parts.push_back({ playerName(p), playerColour(p) });
				parts.push_back({ " lost " + std::to_string(lost), TEXT });
				first = false;
			}
			addLine(std::move(parts), e.kind == Kind::Siege ? Icon::Siege : Icon::Battle, std::string(), e.position);
		}
	}

	if (now >= nextLook) {
		nextLook = now + 2.0f;
		look(client, now);
	}
	// units trained: one line per player every 15 game seconds
	for (auto& [player, t] : trained)
		if (!t.counts.empty() && now - t.since >= 15.0f) {
			std::vector<std::pair<int, std::string>> list;
			for (const auto& [name, n] : t.counts)
				list.emplace_back(n, name);
			std::sort(list.begin(), list.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
			std::string what;
			for (size_t k = 0; k < list.size() && k < 4; k++)
				what += (k ? ", " : "") + counted(list[k].first, list[k].second);
			if (list.size() > 4)
				what += " and more";
			announce({ { playerName(player), playerColour(player) }, { " trained " + what, TEXT } }, Icon::Trained, t.position, playerColour(player));
			t.counts.clear();
		}
	// the markers that are done
	const uint64_t ticks = SDL_GetTicks();
	markers.erase(std::remove_if(markers.begin(), markers.end(), [&](const Marker& m) {
		if (m.chronicleEvent >= 0 && m.chronicleEvent < (int)chron.events.size() && now - chron.events[m.chronicleEvent].endTime <= 30.0f)
			return false;
		return (float)(ticks - m.ticks) / 1000.0f > m.seconds;
	}), markers.end());
}

void ReplayView::look(Client* client, float now)
{
	std::map<uint32_t, Known> current;
	std::map<uint32_t, int> counts; // per player: its characters and buildings
	auto walk = [&](CommonGameObject* obj, uint32_t player, auto& self) -> void {
		for (const auto& [t, ch] : obj->children)
			for (CommonGameObject* o : ch) {
				uint32_t owner = player;
				if (o->blueprint->bpClass == Tags::GAMEOBJCLASS_PLAYER) {
					owner = o->id;
					if (o->blueprint->name != "Neutral Player")
						counts.try_emplace(owner, 0);
				}
				const int cls = o->blueprint->bpClass;
				if (owner && !(o->flags & CommonGameObject::fTerminated) && (cls == Tags::GAMEOBJCLASS_CHARACTER || cls == Tags::GAMEOBJCLASS_BUILDING)) {
					const std::string& name = o->blueprint->name;
					const bool foundation = endsWith(name, " Foundation");
					const bool settlement = cls == Tags::GAMEOBJCLASS_BUILDING
						&& (o->blueprint->buildingType == Tags::BUILDINGTYPE_CIVIC_CENTRE || o->blueprint->buildingType == Tags::BUILDINGTYPE_RURAL_CENTRE);
					current[o->id] = Known{ owner, cls, name, o->position, foundation, settlement };
					if (auto it = counts.find(owner); it != counts.end())
						it->second++;
				}
				self(o, owner, self);
			}
	};
	walk(client->getLevel(), 0, walk);

	if (!firstLook) {
		// per player, this look's buildings finished, destroyed and captured
		std::map<uint32_t, std::map<std::string, int>> built, destroyed;
		std::map<uint32_t, Vector3> builtAt, destroyedAt;
		for (const auto& [id, c] : current) {
			if (known.count(id))
				continue;
			if (c.cls == Tags::GAMEOBJCLASS_CHARACTER) {
				Trained& t = trained[c.player];
				if (t.counts.empty())
					t.since = now;
				t.counts[c.name]++;
				t.position = c.position;
			}
			else if (!c.foundation && !c.settlement && !isWallPiece(c.name)) {
				built[c.player][c.name]++;
				builtAt.try_emplace(c.player, c.position);
			}
		}
		for (const auto& [id, k] : known) {
			if (k.cls != Tags::GAMEOBJCLASS_BUILDING || k.foundation || k.settlement || isWallPiece(k.name))
				continue;
			auto it = current.find(id);
			if (it != current.end() && it->second.player == k.player)
				continue;
			if (it != current.end()) {
				const uint32_t taker = it->second.player;
				std::vector<Part> parts{ { playerName(taker), playerColour(taker) }, { " captured ", TEXT },
					{ playerName(k.player), playerColour(k.player) }, { "'s " + k.name, TEXT } };
				if (const std::string place = placeAt(k.position); !place.empty())
					parts.push_back({ " at " + place, GOLD });
				announce(std::move(parts), Icon::Captured, k.position, playerColour(taker));
				continue;
			}
			// gone, unless built up into another building of its player there
			const bool replaced = std::any_of(current.begin(), current.end(), [&](const auto& c) {
				return c.second.cls == Tags::GAMEOBJCLASS_BUILDING && c.second.player == k.player && (c.second.position - k.position).sqlen2xz() < 9.0f; });
			if (!replaced) {
				destroyed[k.player][k.name]++;
				destroyedAt.try_emplace(k.player, k.position);
			}
		}
		auto list = [](const std::map<std::string, int>& m) {
			std::string s;
			size_t k = 0;
			for (const auto& [name, n] : m)
				s += (k++ ? ", " : "") + counted(n, name);
			return s;
		};
		for (const auto& [player, m] : built) {
			std::vector<Part> parts{ { playerName(player), playerColour(player) }, { " built " + list(m), TEXT } };
			if (const std::string place = placeAt(builtAt[player]); !place.empty())
				parts.push_back({ " at " + place, GOLD });
			announce(std::move(parts), Icon::Built, builtAt[player], playerColour(player));
		}
		for (const auto& [player, m] : destroyed) {
			std::vector<Part> parts{ { playerName(player), playerColour(player) }, { " lost " + list(m), TEXT } };
			if (const std::string place = placeAt(destroyedAt[player]); !place.empty())
				parts.push_back({ " at " + place, GOLD });
			announce(std::move(parts), Icon::Destroyed, destroyedAt[player], playerColour(player));
		}
		// players with nothing left
		for (const auto& [player, n] : counts) {
			bool& was = alive[player];
			if (n > 0) {
				was = true;
				continue;
			}
			if (!was)
				continue;
			was = false;
			// where it had its last things
			Vector3 at;
			int found = 0;
			for (const auto& [id, k] : known)
				if (k.player == player) {
					at += k.position;
					found++;
				}
			if (found)
				at = at * (1.0f / (float)found);
			announce({ { playerName(player), playerColour(player) }, { " has been defeated", TEXT } }, Icon::Defeated, at, 0xFF2020FF);
		}
	}
	else
		for (const auto& [player, n] : counts)
			alive[player] = n > 0;
	known = std::move(current);
	firstLook = false;
}

void ReplayView::drawControls(Client* client, Server* server, IRenderer* gfx)
{
	if (!server || !client || !client->getLevel())
		return;
	if (!fontsLoaded)
		loadFonts(gfx);
	const bool paused = server->replayPaused;
	const uint32_t played = server->replayPlayedMs, total = server->replayTotalMs, seeking = server->replaySeekMs;
	const float speed = client->timeManager.timeSpeed;
	if (startGameTime < 0.0f)
		startGameTime = client->timeManager.currentTime - played / 1000.0f;
	auto setSpeed = [&](float s) { client->sendGameSpeedChange(s); };
	auto setPaused = [&](bool p) { client->sendPauseRequest(p ? 1 : 0); };
	auto seekTo = [&](uint32_t ms) {
		if (ms > played)
			server->replaySeekMs = std::min(ms, total);
	};
	// the speed reached, over a second or so
	const uint64_t ticks = SDL_GetTicks();
	if (ticks - measuredFromTicks >= 1000) {
		if (measuredFromTicks)
			measuredSpeed = (float)(played - std::min(played, measuredFromMs)) / (float)(ticks - measuredFromTicks);
		measuredFromTicks = ticks;
		measuredFromMs = played;
	}

	// the keys: P pauses, keypad + and - change the speed (no text being typed)
	if (!ImGui::GetIO().WantTextInput) {
		if (g_keyPressed[SDL_SCANCODE_P])
			setPaused(!paused);
		if (g_keyPressed[SDL_SCANCODE_L])
			logOpen = !logOpen;
		size_t cur = 0;
		for (size_t i = 0; i < std::size(SPEEDS); i++)
			if (std::fabs(SPEEDS[i] - speed) < std::fabs(SPEEDS[cur] - speed))
				cur = i;
		if (g_keyPressed[SDL_SCANCODE_KP_PLUS] && cur + 1 < std::size(SPEEDS))
			setSpeed(SPEEDS[cur + 1]);
		if (g_keyPressed[SDL_SCANCODE_KP_MINUS] && cur > 0)
			setSpeed(SPEEDS[cur - 1]);
	}
	// test runs: "benchmark": { ..., "replayControls": [[frame, "pause" | "play" | "speed" | "seek",
	// value (speed: times, seek: seconds of played time)], ...] } (frames since the controls
	// first showed), and a line every 60 frames
	if (BenchmarkEnabled()) {
		static int frame = 0;
		frame++;
		for (const auto& step : g_settings.value("benchmark", nlohmann::json::object()).value("replayControls", nlohmann::json::array()))
			if (step.is_array() && step.size() >= 2 && step[0].get<int>() == frame) {
				const std::string what = step[1].get<std::string>();
				const float value = step.size() > 2 ? step[2].get<float>() : 0.0f;
				if (what == "pause") setPaused(true);
				else if (what == "play") setPaused(false);
				else if (what == "speed") setSpeed(value);
				else if (what == "seek") seekTo((uint32_t)(value * 1000.0f));
				printf("REPLAY control frame %d: %s %g\n", frame, what.c_str(), value);
			}
		if (frame % 60 == 0)
			printf("REPLAY frame %d: played %s of %s (%u of %u ms), speed %g (reached %.2f), %s%s\n", frame, clock(played).c_str(),
				clock(total).c_str(), played, total, speed, measuredSpeed, paused ? "paused" : "playing", seeking ? ", jumping" : "");
	}

	// the window: under the top panel, in the middle
	const ImGuiIO& io = ImGui::GetIO();
	const float width = 610.0f;
	ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f - width * 0.5f, 92.0f), ImGuiCond_Always);
	ImGui::SetNextWindowSize(ImVec2(width, 0.0f), ImGuiCond_Always);
	ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(18, 12, 8, 200));
	ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(150, 120, 60, 200));
	ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(70, 50, 30, 220));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(120, 90, 45, 230));
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(170, 130, 60, 255));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
	if (ImGui::Begin("##replay", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings
		| ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav)) {
		ImDrawList* dl = ImGui::GetWindowDrawList();
		// pause / play: a drawn symbol on a button
		const ImVec2 bsize(30.0f, 24.0f);
		const ImVec2 bpos = ImGui::GetCursorScreenPos();
		if (ImGui::Button("##playpause", bsize))
			setPaused(!paused);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip(paused ? "Play (P)" : "Pause (P)");
		const ImVec2 c(bpos.x + bsize.x * 0.5f, bpos.y + bsize.y * 0.5f);
		if (paused)
			dl->AddTriangleFilled(ImVec2(c.x - 5, c.y - 7), ImVec2(c.x - 5, c.y + 7), ImVec2(c.x + 7, c.y), IM_COL32(240, 220, 160, 255));
		else {
			dl->AddRectFilled(ImVec2(c.x - 6, c.y - 7), ImVec2(c.x - 2, c.y + 7), IM_COL32(240, 220, 160, 255));
			dl->AddRectFilled(ImVec2(c.x + 2, c.y - 7), ImVec2(c.x + 6, c.y + 7), IM_COL32(240, 220, 160, 255));
		}
		// the speeds
		for (float s : SPEEDS) {
			ImGui::SameLine();
			const bool selected = std::fabs(s - speed) < 0.01f;
			if (selected)
				ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(170, 130, 60, 255));
			if (ImGui::Button(speedText(s).c_str(), ImVec2(40.0f, bsize.y)) && !selected)
				setSpeed(s);
			if (selected)
				ImGui::PopStyleColor();
		}
		ImGui::SameLine();
		if (ImGui::Button(">> 1:00", ImVec2(60.0f, bsize.y)))
			seekTo(std::max(played, seeking) + 60000);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Jump a minute ahead");
		ImGui::SameLine();
		const bool wasOpen = logOpen;
		if (wasOpen)
			ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(170, 130, 60, 255));
		if (ImGui::Button("Log", ImVec2(40.0f, bsize.y)))
			logOpen = !logOpen;
		if (wasOpen)
			ImGui::PopStyleColor();
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("The event log (L)");
		// the time played and the whole
		ImGui::SameLine();
		std::string time = clock(played) + " / " + clock(total);
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (bsize.y - ImGui::GetTextLineHeight()) * 0.5f);
		ImGui::TextUnformatted(time.c_str());

		// the bar: played, the events (fights red, the rest gold), the jump's target; a click
		// ahead jumps there
		const float barH = 14.0f;
		const ImVec2 p0 = ImGui::GetCursorScreenPos();
		const float barW = ImGui::GetContentRegionAvail().x;
		ImGui::InvisibleButton("##bar", ImVec2(barW, barH));
		const bool barHovered = ImGui::IsItemHovered();
		const ImVec2 p1(p0.x + barW, p0.y + barH);
		auto xAt = [&](double ms) { return p0.x + (float)std::clamp(total ? ms / total : 0.0, 0.0, 1.0) * barW; };
		dl->AddRectFilled(p0, p1, IM_COL32(40, 30, 20, 255), 3.0f);
		dl->AddRectFilledMultiColor(ImVec2(p0.x, p0.y), ImVec2(xAt(played), p1.y), IM_COL32(200, 150, 50, 255), IM_COL32(240, 200, 90, 255),
			IM_COL32(170, 120, 40, 255), IM_COL32(140, 100, 30, 255));
		if (seeking)
			dl->AddRectFilled(ImVec2(xAt(played), p0.y), ImVec2(xAt(seeking), p1.y), IM_COL32(240, 200, 90, 70));
		// the logged fights (red) and settlements and players (white); the one under the mouse
		const LogEntry* hoveredEntry = nullptr;
		float hoveredDistance = 4.0f;
		for (const LogEntry& e : log) {
			const int group = logGroup(e.icon);
			if (group > 1 || e.icon == Icon::Raid)
				continue;
			const float x = xAt(e.playedMs);
			dl->AddLine(ImVec2(x, p0.y + 1), ImVec2(x, p1.y - 1), group == 0 ? IM_COL32(230, 50, 40, 255) : IM_COL32(255, 240, 200, 180), group == 0 ? 2.0f : 1.0f);
			if (barHovered && std::fabs(io.MousePos.x - x) < hoveredDistance) {
				hoveredDistance = std::fabs(io.MousePos.x - x);
				hoveredEntry = &e;
			}
		}
		dl->AddRect(p0, p1, IM_COL32(150, 120, 60, 255), 3.0f);
		if (hoveredEntry) {
			// a past event: what it was; a click shows its place
			std::string text = clock(hoveredEntry->playedMs) + "  ";
			for (const Part& part : hoveredEntry->parts)
				text += part.text;
			ImGui::SetTooltip("%s", text.c_str());
			if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
				lookAt(client, hoveredEntry->position);
		}
		else if (barHovered && total) {
			const uint32_t at = (uint32_t)(std::clamp((io.MousePos.x - p0.x) / barW, 0.0f, 1.0f) * total);
			dl->AddLine(ImVec2(io.MousePos.x, p0.y - 2), ImVec2(io.MousePos.x, p1.y + 2), IM_COL32(255, 255, 255, 200));
			ImGui::SetTooltip(at > played ? "Jump to %s" : "%s (a replay goes forward only)", clock(at).c_str());
			if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
				seekTo(at);
		}
		// what is going on: the speed reached when lower, a jump, the end
		std::string status;
		if (seeking)
			status = "Jumping ahead to " + clock(seeking) + "...";
		else if (played >= total && total)
			status = "The end of the recording";
		else if (paused)
			status = "Paused";
		else if (measuredSpeed > 0.0f && measuredSpeed < speed * 0.9f)
			status = "Speed " + speedText(speed) + " (reached " + speedText(measuredSpeed) + ")";
		if (!status.empty())
			ImGui::TextColored(ImVec4(0.9f, 0.8f, 0.55f, 1.0f), "%s", status.c_str());
	}
	ImGui::End();
	ImGui::PopStyleVar(3);
	ImGui::PopStyleColor(5);
}

void ReplayView::drawAnnouncements(TextureCache& textures, IRenderer* gfx)
{
	if (!fontsLoaded)
		loadFonts(gfx);
	ImDrawList* dl = ImGui::GetBackgroundDrawList();
	const ImGuiIO& io = ImGui::GetIO();
	const uint64_t ticks = SDL_GetTicks();
	while (!announcements.empty() && (float)(ticks - announcements.front().ticks) / 1000.0f > ANNOUNCE_SECONDS)
		announcements.pop_front();
	const BitmapFont* font = textFont ? textFont.get() : nullptr;
	const BitmapFont* title = titleFont ? titleFont.get() : font;
	const float lineH = 28.0f;
	float y = 172.0f;
	for (const Announcement& a : announcements) {
		const float age = (float)(ticks - a.ticks) / 1000.0f;
		// in from the right in a quarter second, out over the last second
		const float alpha = std::clamp((ANNOUNCE_SECONDS - age) / FADE_SECONDS, 0.0f, 1.0f) * std::clamp(age / 0.25f, 0.0f, 1.0f);
		auto measure = [&](const Part& p, bool first) {
			const BitmapFont* f = first ? title : font;
			return f ? f->measure(p.text) : ImGui::CalcTextSize(p.text.c_str()).x;
		};
		float w = 0.0f;
		for (size_t i = 0; i < a.parts.size(); i++)
			w += measure(a.parts[i], i == 0);
		const float iconSize = 24.0f;
		const float total = iconSize + 8.0f + w;
		float x = io.DisplaySize.x * 0.5f - total * 0.5f + (1.0f - std::clamp(age / 0.25f, 0.0f, 1.0f)) * 40.0f;
		dl->AddRectFilled(ImVec2(x - 10.0f, y - 2.0f), ImVec2(x + total + 10.0f, y + lineH - 2.0f), withAlpha(0xA0000000, alpha), 5.0f);
		// the icon: the marker's
		const char* path = a.iconPath.empty() ? iconTexture(a.icon) : a.iconPath.c_str();
		if (path) {
			if (texture tex = textures.getTexture(path, true))
				dl->AddImage(tex, ImVec2(x, y), ImVec2(x + iconSize, y + iconSize), ImVec2(0, 0), ImVec2(1, 1), withAlpha(0xFFFFFFFF, alpha));
		}
		else {
			const ImVec2 c(x + iconSize * 0.5f, y + iconSize * 0.5f);
			dl->AddLine(ImVec2(c.x - 8, c.y - 8), ImVec2(c.x + 8, c.y + 8), withAlpha(0xFF2020FF, alpha), 4.0f);
			dl->AddLine(ImVec2(c.x - 8, c.y + 8), ImVec2(c.x + 8, c.y - 8), withAlpha(0xFF2020FF, alpha), 4.0f);
		}
		x += iconSize + 8.0f;
		for (size_t i = 0; i < a.parts.size(); i++) {
			const Part& p = a.parts[i];
			const BitmapFont* f = i == 0 ? title : font;
			const float ty = y + (iconSize - (f ? (float)f->height : ImGui::GetTextLineHeight())) * 0.5f;
			if (f) {
				f->draw(dl, x + 1.0f, ty + 1.0f, p.text, withAlpha(0xFF000000, alpha));
				f->draw(dl, x, ty, p.text, withAlpha(p.colour, alpha));
			}
			else {
				dl->AddText(ImVec2(x + 1.0f, ty + 1.0f), withAlpha(0xFF000000, alpha), p.text.c_str());
				dl->AddText(ImVec2(x, ty), withAlpha(p.colour, alpha), p.text.c_str());
			}
			x += measure(p, i == 0);
		}
		y += lineH + 2.0f;
	}
}

void ReplayView::drawMinimapMarkers(ImDrawList* dl, TextureCache& textures, const std::function<ImVec2(float, float)>& toMinimap, float size)
{
	const uint64_t ticks = SDL_GetTicks();
	const float t = (float)ticks / 1000.0f;
	const float base = std::max(12.0f, size * 0.085f);
	for (const Marker& m : markers) {
		const float age = (float)(ticks - m.ticks) / 1000.0f;
		const bool ongoing = m.chronicleEvent >= 0 && age > m.seconds;
		// it grows in, beats while a fight goes on, fades at the end
		float alpha = ongoing ? 1.0f : std::clamp((m.seconds - age) / 1.5f, 0.0f, 1.0f);
		float scale = std::min(1.0f, 0.4f + age * 2.5f);
		if (m.chronicleEvent >= 0)
			scale *= 1.0f + 0.12f * std::sin(t * 6.0f);
		const bool small = m.icon == Icon::Built || m.icon == Icon::Trained;
		const float s = base * scale * (small ? 0.7f : 1.0f);
		const ImVec2 c = toMinimap(m.position.x, m.position.z);
		// a ring going out from the place at first, in the colour of who it is about
		if (age < 1.2f) {
			const float r = s * (0.6f + age * 1.5f);
			dl->AddCircle(c, r, withAlpha(m.colour, (1.0f - age / 1.2f) * 0.9f), 24, 2.0f);
		}
		if (m.icon == Icon::Defeated) {
			// a blinking cross
			if (std::fmod(t, 0.6f) < 0.4f) {
				const float h = s * 0.5f;
				dl->AddLine(ImVec2(c.x - h, c.y - h), ImVec2(c.x + h, c.y + h), withAlpha(0xFF000000, alpha), 5.0f);
				dl->AddLine(ImVec2(c.x - h, c.y + h), ImVec2(c.x + h, c.y - h), withAlpha(0xFF000000, alpha), 5.0f);
				dl->AddLine(ImVec2(c.x - h, c.y - h), ImVec2(c.x + h, c.y + h), withAlpha(0xFF2020FF, alpha), 3.0f);
				dl->AddLine(ImVec2(c.x - h, c.y + h), ImVec2(c.x + h, c.y - h), withAlpha(0xFF2020FF, alpha), 3.0f);
			}
			continue;
		}
		// fire flickers
		if (m.icon == Icon::Destroyed)
			alpha *= 0.75f + 0.25f * std::sin(t * 17.0f + m.position.x);
		const char* path = m.iconPath.empty() ? iconTexture(m.icon) : m.iconPath.c_str();
		texture tex = path ? textures.getTexture(path, true) : nullptr;
		if (!tex) {
			// a blinking cross when there is no picture
			if (std::fmod(t, 0.6f) < 0.4f) {
				const float h = s * 0.4f;
				dl->AddLine(ImVec2(c.x - h, c.y - h), ImVec2(c.x + h, c.y + h), withAlpha(m.colour, alpha), 2.5f);
				dl->AddLine(ImVec2(c.x - h, c.y + h), ImVec2(c.x + h, c.y - h), withAlpha(m.colour, alpha), 2.5f);
			}
			continue;
		}
		// a dark disc in the player's colour behind the icon, so that it reads on any ground
		dl->AddCircleFilled(c, s * 0.62f, withAlpha(0xC0000000, alpha), 20);
		dl->AddCircle(c, s * 0.62f, withAlpha(m.colour, alpha), 20, 1.5f);
		const float h = s * 0.5f;
		dl->AddImage(tex, ImVec2(c.x - h, c.y - h), ImVec2(c.x + h, c.y + h), ImVec2(0, 0), ImVec2(1, 1), withAlpha(0xFFFFFFFF, alpha));
	}
}

void ReplayView::drawLog(Client* client, TextureCache& textures)
{
	if (!logOpen || !client)
		return;
	ImGui::SetNextWindowPos(ImVec2(12.0f, 180.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(440.0f, 300.0f), ImGuiCond_FirstUseEver);
	ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(18, 12, 8, 205));
	ImGui::PushStyleColor(ImGuiCol_TitleBg, IM_COL32(60, 40, 20, 230));
	ImGui::PushStyleColor(ImGuiCol_TitleBgActive, IM_COL32(90, 62, 30, 240));
	ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32(120, 90, 45, 160));
	ImGui::PushStyleColor(ImGuiCol_HeaderHovered, IM_COL32(150, 110, 55, 170));
	if (ImGui::Begin("Events (L)", &logOpen, ImGuiWindowFlags_NoFocusOnAppearing)) {
		ImGui::Checkbox("Fights", &showFights);
		ImGui::SameLine();
		ImGui::Checkbox("Settlements", &showSettlements);
		ImGui::SameLine();
		ImGui::Checkbox("Buildings", &showBuildings);
		ImGui::SameLine();
		ImGui::Checkbox("Units", &showUnits);
		ImGui::Separator();
		ImGui::BeginChild("##entries");
		const bool shown[4] = { showFights, showSettlements, showBuildings, showUnits };
		int count = 0;
		const float iconSize = ImGui::GetTextLineHeight() + 2.0f;
		for (size_t i = log.size(); i-- > 0;) {
			const LogEntry& e = log[i];
			if (!shown[logGroup(e.icon)])
				continue;
			count++;
			ImGui::PushID((int)i);
			// the whole row clicked: the camera there
			const ImVec2 start = ImGui::GetCursorPos();
			if (ImGui::Selectable("##row", false, ImGuiSelectableFlags_AllowOverlap, ImVec2(0.0f, iconSize)))
				lookAt(client, e.position);
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Show the place");
			ImGui::SetCursorPos(start);
			ImGui::AlignTextToFramePadding();
			ImGui::TextColored(ImVec4(0.65f, 0.6f, 0.5f, 1.0f), "%6s", clock(e.playedMs).c_str());
			ImGui::SameLine();
			const char* path = e.iconPath.empty() ? iconTexture(e.icon) : e.iconPath.c_str();
			if (texture tex = path ? textures.getTexture(path, true) : nullptr)
				ImGui::Image((ImTextureID)tex, ImVec2(iconSize, iconSize));
			else {
				// the defeated's cross
				const ImVec2 p = ImGui::GetCursorScreenPos();
				ImDrawList* dl = ImGui::GetWindowDrawList();
				dl->AddLine(ImVec2(p.x + 3, p.y + 3), ImVec2(p.x + iconSize - 3, p.y + iconSize - 3), IM_COL32(230, 40, 40, 255), 2.5f);
				dl->AddLine(ImVec2(p.x + 3, p.y + iconSize - 3), ImVec2(p.x + iconSize - 3, p.y + 3), IM_COL32(230, 40, 40, 255), 2.5f);
				ImGui::Dummy(ImVec2(iconSize, iconSize));
			}
			ImGui::SameLine(0.0f, 6.0f);
			for (size_t k = 0; k < e.parts.size(); k++) {
				if (k)
					ImGui::SameLine(0.0f, 0.0f);
				ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(e.parts[k].colour), "%s", e.parts[k].text.c_str());
			}
			ImGui::PopID();
		}
		if (!count)
			ImGui::TextUnformatted(log.empty() ? "Nothing has happened yet." : "Nothing of these kinds yet.");
		ImGui::EndChild();
	}
	ImGui::End();
	ImGui::PopStyleColor(5);
}
