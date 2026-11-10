// wkbai - watching a recording: its playback controls, and the game's events announced on the
// screen and marked on the minimap for a while

#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "../util/vecmat.h"
#include "../imgui/imgui.h"

struct Client;
struct Server;
struct GameChronicle;
struct TextureCache;
struct BitmapFont;
struct IRenderer;

// The events come from the chronicle (battles, sieges, raids, settlements founded and destroyed)
// and from this view's own look at the objects every 2 game seconds: buildings finished,
// destroyed and captured, units trained (summed per player over 15 game seconds) and players
// defeated (nothing left). Each is a line of text at the top of the screen (6 seconds, then it
// fades) and a marker on the minimap: crossed swords for a battle, a ship for one at sea, a
// siege engine for a siege, a sword for a raid, the settlement's icon where one is founded, a
// burning house where buildings are destroyed, a blinking cross where a player fell. A battle's
// marker stays, beating, as long as the fighting goes on. Every line is kept in the event log
// too (L, or the controls' Log button): the whole replay so far, newest first, with the time
// played, filtered by kind; an entry clicked shows its place. The progress bar marks them.
struct ReplayView {
	ReplayView();
	~ReplayView();

	// once a frame, before drawing: the new events. palette: the player colours (9)
	void update(Client* client, Server* server, const GameChronicle& chronicle, const uint32_t* palette);
	// the controls under the top panel: pause, the speeds, the time played and the whole, a bar
	// to jump forward on; and the keys (P, keypad + and -, Right: a minute on)
	void drawControls(Client* client, Server* server, IRenderer* gfx);
	void drawAnnouncements(TextureCache& textures, IRenderer* gfx);
	// wkbai's unit history: a unit became a hero ("Sigurd Ironside has slain 50 enemies and is
	// now Sigurd the Valiant", the names in its player's colour), in a replay and in a game
	void announceHero(Client* client, uint32_t id, const uint32_t* palette);
	// the event log's window, while open
	void drawLog(Client* client, TextureCache& textures);
	bool logOpen = true;
	// on the minimap: toMinimap gives a map position's place there, size its side
	void drawMinimapMarkers(ImDrawList* list, TextureCache& textures, const std::function<ImVec2(float, float)>& toMinimap, float size);

private:
	enum class Icon { Battle, SeaBattle, Siege, Raid, Founded, Built, Destroyed, Trained, Defeated, Captured };
	// a piece of a line, in a player's colour or the text's
	struct Part {
		std::string text;
		uint32_t colour;
	};
	struct Announcement {
		std::vector<Part> parts;
		Icon icon;
		std::string iconPath; // the founded settlement's
		uint64_t ticks; // SDL ticks when it came
	};
	struct Marker {
		Icon icon;
		std::string iconPath;
		Vector3 position;
		uint32_t colour;
		uint64_t ticks;
		float seconds; // how long it shows
		int chronicleEvent = -1; // a battle's: shown while it goes on
	};
	std::deque<Announcement> announcements;
	// every line so far: when (ms of the replay played), where
	struct LogEntry {
		uint32_t playedMs;
		std::vector<Part> parts;
		Icon icon;
		std::string iconPath;
		Vector3 position;
	};
	std::vector<LogEntry> log;
	// what the log shows: fights, settlements and players, buildings, units trained
	bool showFights = true, showSettlements = true, showBuildings = true, showUnits = false;
	static int logGroup(Icon icon); // 0 fights, 1 settlements and players, 2 buildings, 3 units
	void addLine(std::vector<Part> parts, Icon icon, const std::string& iconPath, const Vector3& position);
	void lookAt(Client* client, const Vector3& position);
	std::vector<Marker> markers;

	// the chronicle's events seen so far, and what each was (a raid may grow into a battle)
	struct SeenEvent {
		int kind;
		bool over = false;
	};
	std::vector<SeenEvent> seenEvents;
	// the objects at the last look
	struct Known {
		uint32_t player;
		int cls;
		std::string name;
		Vector3 position;
		bool foundation, settlement;
	};
	std::map<uint32_t, Known> known;
	bool firstLook = true;
	float nextLook = 0.0f;
	// units trained, per player, since the first of them (game time)
	struct Trained {
		std::map<std::string, int> counts;
		float since = 0.0f;
		Vector3 position;
	};
	std::map<uint32_t, Trained> trained;
	std::map<uint32_t, bool> alive; // players that had something, and whether they still have
	float lastGameTime = -1.0f;
	const void* level = nullptr;

	const GameChronicle* chronicle = nullptr;
	Client* client = nullptr;
	Server* server = nullptr;
	const uint32_t* palette = nullptr;
	uint32_t playerColour(uint32_t player) const;
	std::string playerName(uint32_t player) const;
	std::string placeAt(const Vector3& position) const;
	void announce(std::vector<Part> parts, Icon icon, const Vector3& position, uint32_t colour, const std::string& iconPath = std::string(), int chronicleEvent = -1);
	void look(Client* client, float now);

	// the playback: the game time at the start of the replay (its played time 0), where the
	// measured speed was last taken
	float startGameTime = -1.0f;
	uint32_t measuredFromMs = 0;
	uint64_t measuredFromTicks = 0;
	float measuredSpeed = 0.0f;
	bool benchmarkStepsDone = false;

	std::unique_ptr<BitmapFont> titleFont, textFont;
	bool fontsLoaded = false;
	void loadFonts(IRenderer* gfx);
	static const char* iconTexture(Icon icon);
};
