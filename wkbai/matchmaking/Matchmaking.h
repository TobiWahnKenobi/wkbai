// wkbai - matchmaking: finding games to join, offering one's own
// Licensed under the GNU General Public License 3
//
// Every way of finding games is a MatchmakingService: "local" (the local network, by UDP
// broadcast to the game port), "nakama" (a Nakama server, self-hosted), and plugin libraries
// from the plugins folder (Epic Online Services and any other: see wkbai_mm_plugin.h). A host
// offers its game on the services it chooses; a guest sees all of them in one list.
//
// What is needed to play together (the mods' game logic, the region names, the water design) is
// carried by the game's own messages once connected, so it works the same through every service.
// A service either hands back an address for ENet (the game's transport) or carries the packets
// itself (GuestConnection / HostTransport with its own NetLink).

#pragma once

#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

struct NetLink;
struct _ENetHost;

// Builds that can play together say the same: the game's messages (network.h) and the lobby
const char* MatchmakingGameVersion();

// A hosted game as a service lists it
struct GameListing {
	std::string service;          // the service's id ("local", "nakama", a plugin's)
	std::string id;               // the listing's id within its service
	std::string hostId;           // the hosting program's (one host found through several ways is listed once)
	std::string name, hostName, map;
	int players = 0, places = 0;  // humans in the game, the level's positions
	int spectators = 0, maxSpectators = 0;
	bool allowSpectators = false;
	bool password = false;        // guests have to give one
	bool running = false;         // a game in progress (joinable: free places, spectators), else the lobby
	std::string version;          // MatchmakingGameVersion() of the host
	std::string modManifest;      // the host's ModManifest() (JSON text)
	int modPolicy = 0;            // Server::ModPolicy
	std::vector<std::string> addresses; // "address:port" ENet can reach the host at (may be empty)
	int ping = -1;                // milliseconds, -1: unknown
	uint64_t seen = 0;            // SDL_GetTicks() when last seen (the list drops old ones)

	nlohmann::json toJson() const;
	static GameListing fromJson(const nlohmann::json& j);
};

struct ListFilter {
	bool hideFull = false, hidePassword = false, sameVersionOnly = false;
	std::string text; // in the name, host or map
	nlohmann::json toJson() const;
	bool accepts(const GameListing& l) const;
};

// A guest's connection to its host, whatever carries it
struct GuestConnection {
	enum Event { NONE, CONNECTED, DISCONNECTED };
	virtual ~GuestConnection() = default;
	// Every frame: runs the network; what happened since (one event a call)
	virtual Event poll() = 0;
	// After CONNECTED, until DISCONNECTED
	virtual NetLink* link() = 0;
	// Leaving: tells the host and waits for it (at most a second)
	virtual void close() = 0;
	// For the log: what it connects through ("ENet 1.2.3.4:1234", "Nakama relay")
	virtual std::string describe() const = 0;
};

// A host's way of taking guests: ENet's port, or a service that carries the packets itself
struct HostTransport {
	struct Event {
		enum Type { CONNECTED, DISCONNECTED } type;
		NetLink* link;
	};
	virtual ~HostTransport() = default;
	// Every frame: the guests who came and went
	virtual void poll(std::vector<Event>& events) = 0;
};

// What a host gives the services it offers its game on
struct HostContext {
	_ENetHost* enet = nullptr; // the game port's ENet host (raw datagrams: EnetTransport.h)
	uint16_t port = 1234;
	std::string bind;          // wkconfig.json "hostBind" ("": every interface)
};

struct MatchmakingService {
	enum class State { OFF, SIGNING_IN, READY, FAILED };
	virtual ~MatchmakingService() = default;
	virtual std::string id() const = 0;
	virtual std::string title() const = 0;     // above its games in the list
	virtual std::string icon() const { return std::string(); } // an image (game path or file), or ""
	virtual State state() const = 0;
	virtual std::string stateText() const { return std::string(); } // why it is off or failed
	// Anonymous / device sign-in; the manager calls it once at start
	virtual void signIn() {}
	virtual void tick() {}
	// hosting or joining a game: the manager doesn't replace it for new settings until it's done
	virtual bool inUse() const { return false; }

	// Host: offer the game (again: its listing changed), take it off
	virtual void publish(const GameListing& listing, const HostContext& context) = 0;
	virtual void update(const GameListing& listing) { publish(listing, lastContext); }
	virtual void withdraw() = 0;
	// The service's own transport for guests, if it carries packets itself
	virtual HostTransport* hostTransport() { return nullptr; }

	// Guest: look for games (the answers come in over the next frames), the games found
	virtual void refresh(const ListFilter& filter) = 0;
	virtual std::vector<GameListing> listings() const = 0;
	// A connection to a listed game, or null (why: stateText)
	virtual std::unique_ptr<GuestConnection> connect(const GameListing& listing) = 0;

protected:
	HostContext lastContext;
};

// All services: the built-in ones and the plugins'
struct Matchmaking {
	static Matchmaking& get();
	// Once, at start: creates the services and signs them in
	void init();
	void shutdown();
	void tick();
	// After the Options page saved: a built-in service whose settings changed is made again (when
	// it isn't in use, else once it is done)
	void settingsChanged();
	const std::vector<std::unique_ptr<MatchmakingService>>& services() const { return list; }
	MatchmakingService* find(const std::string& id) const;

	// Host: the game offered on these services ("local" always among them), its listing kept
	// up to date, withdrawn from the others
	void host(const GameListing& listing, const std::set<std::string>& where, const HostContext& context);
	void stopHosting();
	const std::set<std::string>& hostedOn() const { return offered; }
	// the services the player chose to offer games on (the host lobby's "Offer on")
	std::set<std::string> offerOn;
	// the services offered on that carry packets themselves
	std::vector<HostTransport*> hostTransports() const;

	// Guest: every service looks again (also every few seconds while the list is shown)
	void refresh();
	ListFilter filter;
	// pings listings that have addresses (the host's game port answers); MatchmakingPing
	void probePings();

private:
	std::vector<std::unique_ptr<MatchmakingService>> list;
	std::set<std::string> offered;
	std::string lastListing;
	bool started = false;
	bool reloadWanted = false;
	nlohmann::json builtSettings; // the built-in services' settings when they were made
	void loadPlugins();
	void reloadBuiltIn();
};

// A listing's ping: its own (the local network's answers), else the best of its addresses
// (Matchmaking::probePings), -1 when none answered
int MatchmakingPing(const GameListing& listing);
// "address:port" or "address" (then defaultPort)
bool SplitHostPort(const std::string& text, std::string& host, uint16_t& port, uint16_t defaultPort);
// This computer's IPv4 addresses, and their networks' broadcast addresses (no loopback)
void LocalIPv4Addresses(std::vector<std::string>& addresses, std::vector<std::string>& broadcasts);
// wkconfig.json "matchmaking": { "<service>": {...} }
nlohmann::json MatchmakingSettings(const std::string& service);
// "matchmaking": { "stun": "host:port" } for this side's public address; absent: a public one
// (DEFAULT_STUN_SERVER), "" or "none": none (then "")
std::string MatchmakingStunServer();
extern const char* const DEFAULT_STUN_SERVER;
std::string Base64Encode(const std::string& data);
std::string Base64Decode(const std::string& text);
