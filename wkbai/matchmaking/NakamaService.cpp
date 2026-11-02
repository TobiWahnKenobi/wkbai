// wkbai - matchmaking: games on a Nakama server (Heroic Labs, Apache-2.0, self-hosted)
// Licensed under the GNU General Public License 3
//
// wkconfig.json "matchmaking": { "nakama": {
//     "server": "http://127.0.0.1:7350"   the server's API (https:// with TLS); none: off
//     "serverKey": "defaultkey"           the server's socket.server_key
//     "relay": "auto"                     "auto": through Nakama when ENet can't connect directly,
//                                         "always", "never"
//     "title": "Nakama"                   the name the game list shows
// } }, and "matchmaking": { "stun": "host:port" } for this side's public address (absent: a public
// one, MatchmakingStunServer; "" or "none": none).
//
// Only Nakama's own API is used, no server module:
//  - sign-in: a device id (made once, kept in the user folder's matchmaking.json), an account
//    created for it the first time (POST /v2/account/authenticate/device);
//  - a hosted game: a publicly readable storage object (collection "wkbai_games", key "game")
//    holding the listing, written again every 20 s (a listing not written for 60 s is left out:
//    its host is gone without withdrawing it); the newest first;
//  - a relayed match (the realtime socket) per hosted game, through which guests reach the host:
//      op 3 a guest's addresses: the host punches its NAT towards them, then the guest connects
//           with ENet to the host's addresses
//      op 4 the guest asks for the relay (or the host says it is there): the game's packets go
//           through Nakama, op 1 (each packet: its 16-bit length, then the packet)
//      op 5 gone

#include "Services.h"
#include "EnetTransport.h"
#include "HttpClient.h"
#include "../network.h"
#include "../settings.h"
#include "../file.h"
#include <enet/enet.h>
#include <SDL3/SDL_timer.h>
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <ctime>
#include <deque>
#include <fstream>
#include <functional>
#include <map>
#include <mutex>
#include <queue>
#include <random>
#include <thread>

namespace {

using json = nlohmann::json;

enum { OP_DATA = 1, OP_HELLO = 3, OP_RELAY = 4, OP_BYE = 5 };
const size_t RELAY_CHUNK = 2400; // raw bytes a message (Nakama takes 4096 bytes a message by default)
const char* COLLECTION = "wkbai_games";

struct NakamaService;

int64_t OpCode(const json& j)
{
	if (j.is_number()) return j.get<int64_t>();
	if (j.is_string()) return atoll(j.get<std::string>().c_str());
	return 0;
}

// RFC 3339 ("2026-11-02T10:00:00Z", maybe with fractions) to Unix time
int64_t ParseTime(const std::string& t)
{
	std::tm tm = {};
	if (sscanf(t.c_str(), "%d-%d-%dT%d:%d:%d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday, &tm.tm_hour, &tm.tm_min, &tm.tm_sec) != 6)
		return 0;
	tm.tm_year -= 1900;
	tm.tm_mon -= 1;
#ifdef _WIN32
	return _mkgmtime(&tm);
#else
	return timegm(&tm);
#endif
}

// The game's packets through Nakama
struct RelayLink : NetLink {
	NakamaService* service;
	std::string matchId;
	json target; // the other side's presence
	std::queue<NetPacket> in;
	std::string out; // packets waiting: length, packet, ...
	bool closed = false;
	RelayLink(NakamaService* s, const std::string& match, const json& presence) : service(s), matchId(match), target(presence) {}
	bool available() override { return !in.empty(); }
	NetPacket receive() override
	{
		NetPacket p = std::move(in.front());
		in.pop();
		return p;
	}
	void send(const NetPacket& packet) override
	{
		if (closed)
			return;
		const uint16_t n = (uint16_t)packet.data.size();
		out.append((const char*)&n, 2);
		out.append(packet.data);
	}
	void disconnect(bool afterSending) override;
	void received(const std::string& data)
	{
		for (size_t at = 0; at + 2 <= data.size();) {
			uint16_t n;
			memcpy(&n, data.data() + at, 2);
			if (at + 2 + n > data.size())
				break;
			in.push(NetPacket(data.substr(at + 2, n)));
			at += 2 + n;
		}
	}
	void flush();
};

struct NakamaGuestConnection;

struct NakamaHostTransport : HostTransport {
	NakamaService* service = nullptr;
	std::map<std::string, RelayLink*> links; // by session id
	std::vector<Event> events;
	void poll(std::vector<Event>& out) override
	{
		out.insert(out.end(), events.begin(), events.end());
		events.clear();
	}
	void gone(const std::string& session)
	{
		auto it = links.find(session);
		if (it == links.end())
			return;
		it->second->closed = true;
		events.push_back({ Event::DISCONNECTED, it->second });
		links.erase(it);
	}
};

struct NakamaService : MatchmakingService {
	json settings;
	std::string server, wsServer, serverKey, relayMode, stateMessage;
	std::atomic<State> current{ State::OFF };

	// the account, set by the worker thread
	std::mutex accountMutex;
	std::string token, userId, deviceId;
	int64_t tokenExpiry = 0;

	// REST calls on a worker thread; their results run in tick()
	std::thread worker;
	std::mutex jobMutex;
	std::condition_variable jobWake;
	std::deque<std::function<void()>> jobs;
	std::deque<std::function<void()>> results;
	std::atomic<bool> stopping{ false };
	bool busy = false; // a job running (jobMutex)

	// the realtime socket: a reader and a sender thread
	WebSocketClient ws;
	std::thread reader, sender;
	std::mutex wsMutex;
	std::condition_variable sendWake;
	std::deque<std::string> outgoing, incoming;
	std::atomic<bool> wsOpen{ false };
	int nextCid = 1;
	std::map<std::string, std::function<void(const json&)>> pending;

	// Host
	bool hosting = false;
	json hostListing;
	std::string hostMatchId;
	uint64_t lastWrite = 0;
	NakamaHostTransport transport;
	EnetRaw::Stun stun;
	std::string listedStun;

	// Guest
	std::vector<GameListing> found;
	std::map<std::string, std::string> matchOf; // listing id: its match
	std::vector<NakamaGuestConnection*> guests;
	bool listing = false;

	NakamaService()
	{
		settings = MatchmakingSettings("nakama");
		server = settings.value("server", std::string());
		while (!server.empty() && server.back() == '/')
			server.pop_back();
		serverKey = settings.value("serverKey", std::string("defaultkey"));
		relayMode = settings.value("relay", std::string("auto"));
		wsServer = server.rfind("https://", 0) == 0 ? "wss://" + server.substr(8) : server.rfind("http://", 0) == 0 ? "ws://" + server.substr(7) : server;
		transport.service = this;
	}

	~NakamaService() override
	{
		withdraw();
		// the listing's removal goes out first (at most 3 s)
		for (const uint64_t until = SDL_GetTicks() + 3000; worker.joinable() && SDL_GetTicks() < until; SDL_Delay(20)) {
			std::lock_guard lock(jobMutex);
			if (jobs.empty() && !busy)
				break;
		}
		stopping = true;
		jobWake.notify_all();
		if (worker.joinable())
			worker.join();
		closeSocket();
	}

	std::string id() const override { return "nakama"; }
	std::string title() const override { return settings.value("title", std::string("Nakama")); }
	State state() const override { return current; }
	bool inUse() const override { return hosting || !guests.empty(); }
	std::string stateText() const override { return stateMessage; }

	// Worker -----------------------------------------------------------------------------

	void post(std::function<void()> job)
	{
		std::lock_guard lock(jobMutex);
		jobs.push_back(std::move(job));
		jobWake.notify_one();
	}
	void result(std::function<void()> f)
	{
		std::lock_guard lock(jobMutex);
		results.push_back(std::move(f));
	}
	void work()
	{
		while (!stopping) {
			std::function<void()> job;
			{
				std::unique_lock lock(jobMutex);
				jobWake.wait(lock, [&] { return stopping || !jobs.empty(); });
				if (stopping)
					return;
				job = std::move(jobs.front());
				jobs.pop_front();
				busy = true;
			}
			job();
			std::lock_guard lock(jobMutex);
			busy = false;
		}
	}

	// the device id: made once, kept in the user folder
	std::string loadDeviceId()
	{
		const std::filesystem::path path = g_userPath / "matchmaking.json";
		json j = json::object();
		if (std::ifstream f(path); f)
			j = json::parse(f, nullptr, false);
		if (!j.is_object())
			j = json::object();
		std::string id = j.value("deviceId", std::string());
		if (id.size() < 10) {
			std::random_device rd;
			char text[40];
			snprintf(text, sizeof(text), "wkbai-%08x%08x%08x%08x", rd(), rd(), rd(), rd());
			id = text;
			j["deviceId"] = id;
			std::error_code ec;
			std::filesystem::create_directories(g_userPath, ec);
			std::ofstream(path) << j.dump(1);
		}
		return id;
	}

	// on the worker thread: a session token (signing in again shortly before it expires)
	bool authenticate(std::string& error)
	{
		{
			std::lock_guard lock(accountMutex);
			if (!token.empty() && tokenExpiry - (int64_t)time(nullptr) > 20)
				return true;
		}
		const HttpResult r = HttpRequest("POST", server + "/v2/account/authenticate/device?create=true",
			json{ { "id", deviceId } }.dump(), { { "Authorization", "Basic " + Base64Encode(serverKey + ":") }, { "Content-Type", "application/json" } });
		if (r.status != 200) {
			error = r.status ? "the server refused the sign-in (" + std::to_string(r.status) + ")" : "no answer from " + server + (r.error.empty() ? "" : ": " + r.error);
			return false;
		}
		const json j = json::parse(r.body, nullptr, false);
		const std::string t = j.is_object() ? j.value("token", std::string()) : std::string();
		// the token's payload: "uid" the user id, "exp" when it expires
		const size_t a = t.find('.'), b = t.find('.', a + 1);
		const json claims = (a != std::string::npos && b != std::string::npos) ? json::parse(Base64Decode(t.substr(a + 1, b - a - 1)), nullptr, false) : json();
		if (t.empty() || !claims.is_object()) {
			error = "the server's answer to the sign-in wasn't understood";
			return false;
		}
		std::lock_guard lock(accountMutex);
		token = t;
		userId = claims.value("uid", std::string());
		tokenExpiry = claims.value("exp", (int64_t)0);
		return true;
	}

	HttpResult api(const std::string& method, const std::string& path, const std::string& body)
	{
		std::string error;
		if (!authenticate(error)) {
			HttpResult r;
			r.error = error;
			return r;
		}
		std::string t;
		{
			std::lock_guard lock(accountMutex);
			t = token;
		}
		return HttpRequest(method, server + path, body, { { "Authorization", "Bearer " + t }, { "Content-Type", "application/json" } });
	}

	void signIn() override
	{
		if (server.empty()) {
			current = State::OFF;
			stateMessage = "no server set (wkconfig.json: \"matchmaking\": { \"nakama\": { \"server\": \"http://host:7350\" } })";
			return;
		}
		if (current == State::SIGNING_IN || current == State::READY)
			return;
		current = State::SIGNING_IN;
		stateMessage = "signing in to " + server;
		deviceId = loadDeviceId();
		if (!worker.joinable())
			worker = std::thread([this] { work(); });
		post([this] {
			std::string error;
			const bool ok = authenticate(error) && openSocket(error);
			result([this, ok, error] {
				current = ok ? State::READY : State::FAILED;
				stateMessage = ok ? std::string() : error;
				printf("Matchmaking: Nakama %s%s\n", ok ? "signed in at " : "sign-in failed: ", ok ? server.c_str() : error.c_str());
			});
		});
	}

	// Realtime socket --------------------------------------------------------------------

	bool openSocket(std::string& error)
	{
		std::string t;
		{
			std::lock_guard lock(accountMutex);
			t = token;
		}
		if (!ws.open(wsServer + "/ws?lang=en&status=false&format=json&token=" + t, error)) {
			error = "the realtime socket: " + error;
			return false;
		}
		wsOpen = true;
		reader = std::thread([this] {
			std::string message;
			while (ws.receive(message)) {
				std::lock_guard lock(wsMutex);
				incoming.push_back(message);
			}
			wsOpen = false;
			sendWake.notify_all();
		});
		sender = std::thread([this] {
			for (;;) {
				std::string message;
				{
					std::unique_lock lock(wsMutex);
					sendWake.wait(lock, [&] { return !outgoing.empty() || !wsOpen; });
					if (!wsOpen)
						return;
					message = std::move(outgoing.front());
					outgoing.pop_front();
				}
				if (!ws.send(message))
					printf("Matchmaking: Nakama: a message wasn't sent\n");
			}
		});
		return true;
	}

	void closeSocket()
	{
		wsOpen = false;
		sendWake.notify_all();
		ws.close();
		if (reader.joinable()) reader.join();
		if (sender.joinable()) sender.join();
	}

	void sendEnvelope(json envelope, std::function<void(const json&)> answer = nullptr)
	{
		if (answer) {
			const std::string cid = std::to_string(nextCid++);
			envelope["cid"] = cid;
			pending[cid] = std::move(answer);
		}
		std::lock_guard lock(wsMutex);
		outgoing.push_back(envelope.dump());
		sendWake.notify_one();
	}

	void sendMatchData(const std::string& match, int op, const std::string& data, const json& to)
	{
		json d = { { "match_id", match }, { "op_code", op }, { "data", Base64Encode(data) }, { "reliable", true } };
		if (!to.is_null())
			d["presences"] = json::array({ to });
		sendEnvelope({ { "match_data_send", d } });
	}

	void dispatch(const json& m);

	// Host ---------------------------------------------------------------------------------

	json listingJson(const GameListing& l)
	{
		GameListing copy = l;
		// where ENet reaches the host: its public address (STUN), its local addresses
		copy.addresses.clear();
		const std::string port = std::to_string(lastContext.port);
		if (!stun.publicAddress.empty())
			copy.addresses.push_back(stun.publicAddress);
		if (!lastContext.bind.empty() && lastContext.bind != "0.0.0.0")
			copy.addresses.push_back(lastContext.bind + ":" + port);
		else {
			std::vector<std::string> mine, broadcasts;
			LocalIPv4Addresses(mine, broadcasts);
			for (const std::string& a : mine)
				copy.addresses.push_back(a + ":" + port);
		}
		json j = copy.toJson();
		j["match"] = hostMatchId;
		return j;
	}

	void writeListing()
	{
		if (!hosting || hostMatchId.empty())
			return;
		lastWrite = SDL_GetTicks();
		// Nakama leaves an object it is given unchanged as it was, its update_time too: the time of
		// writing makes every write a change
		json value = hostListing;
		value["written"] = (int64_t)time(nullptr);
		const json body = { { "objects", json::array({ { { "collection", COLLECTION }, { "key", "game" }, { "value", value.dump() },
			{ "permission_read", 2 }, { "permission_write", 1 } } }) } };
		post([this, body] {
			const HttpResult r = api("PUT", "/v2/storage", body.dump());
			if (r.status != 200)
				result([r] { printf("Matchmaking: Nakama: the listing wasn't written (%d %s%s)\n", r.status, r.error.c_str(), r.body.substr(0, 200).c_str()); });
		});
	}

	GameListing lastPublished;

	void publish(const GameListing& listing, const HostContext& context) override
	{
		lastContext = context;
		lastPublished = listing;
		if (!hosting) {
			hosting = true;
			// this side's public address for the listing (MatchmakingStunServer)
			if (const std::string s = MatchmakingStunServer(); !s.empty())
				stun.start(context.enet, s);
			sendEnvelope({ { "match_create", json::object() } }, [this](const json& m) {
				if (!hosting)
					return;
				hostMatchId = m.contains("match") ? m["match"].value("match_id", std::string()) : std::string();
				if (hostMatchId.empty()) {
					printf("Matchmaking: Nakama: no match for the game (%s)\n", m.dump().c_str());
					return;
				}
				hostListing = listingJson(lastPublished);
				writeListing();
			});
		}
		else {
			hostListing = listingJson(listing);
			writeListing();
		}
	}

	void update(const GameListing& listing) override { publish(listing, lastContext); }

	void withdraw() override
	{
		if (!hosting)
			return;
		hosting = false;
		stun.stop();
		for (auto& [session, link] : transport.links) {
			sendMatchData(hostMatchId, OP_BYE, std::string(), link->target);
			link->closed = true;
			transport.events.push_back({ HostTransport::Event::DISCONNECTED, link });
		}
		transport.links.clear();
		if (!hostMatchId.empty())
			sendEnvelope({ { "match_leave", { { "match_id", hostMatchId } } } });
		hostMatchId.clear();
		post([this] {
			api("PUT", "/v2/storage/delete", json{ { "object_ids", json::array({ { { "collection", COLLECTION }, { "key", "game" } } }) } }.dump());
		});
	}

	HostTransport* hostTransport() override { return &transport; }

	void hostReceived(const json& presence, int64_t op, const std::string& data)
	{
		const std::string session = presence.value("session_id", std::string());
		if (op == OP_HELLO) {
			// a guest's addresses: a few datagrams there open this side's NAT for its ENet connect
			const json j = json::parse(data, nullptr, false);
			if (j.is_object() && lastContext.enet)
				for (const auto& e : j.value("endpoints", json::array())) {
					uint32_t ip;
					uint16_t port;
					if (e.is_string() && EnetRaw::resolve(e.get<std::string>(), 0, ip, port) && port)
						for (int i = 0; i < 3; i++)
							EnetRaw::send(lastContext.enet, ip, port, std::string(EnetRaw::PUNCH, 7));
				}
		}
		else if (op == OP_RELAY) {
			if (!transport.links.count(session)) {
				RelayLink* link = new RelayLink(this, hostMatchId, presence);
				transport.links[session] = link;
				transport.events.push_back({ HostTransport::Event::CONNECTED, link });
				printf("Matchmaking: a guest joins through the Nakama relay\n");
			}
			sendMatchData(hostMatchId, OP_RELAY, std::string(), presence);
		}
		else if (op == OP_DATA) {
			if (auto it = transport.links.find(session); it != transport.links.end())
				it->second->received(data);
		}
		else if (op == OP_BYE)
			transport.gone(session);
	}

	// Guest --------------------------------------------------------------------------------

	void refresh(const ListFilter&) override
	{
		if (listing || current != State::READY)
			return;
		listing = true;
		post([this] {
			const HttpResult r = api("GET", std::string("/v2/storage/") + COLLECTION + "?limit=100", std::string());
			std::string me;
			{
				std::lock_guard lock(accountMutex);
				me = userId;
			}
			std::vector<std::pair<int64_t, GameListing>> written; // update time, listing
			std::map<std::string, std::string> matches;
			const json j = json::parse(r.body, nullptr, false);
			const int64_t now = time(nullptr);
			if (r.status == 200 && j.is_object())
				for (const auto& o : j.value("objects", json::array())) {
					const std::string owner = o.value("user_id", std::string());
					// a listing not written for 60 s: its host is gone
					const int64_t updated = ParseTime(o.value("update_time", std::string()));
					if (owner.empty() || owner == me || now - updated > 60)
						continue;
					const json v = json::parse(o.value("value", std::string()), nullptr, false);
					if (!v.is_object() || v.value("match", std::string()).empty())
						continue;
					GameListing l = GameListing::fromJson(v);
					l.service = "nakama";
					l.id = owner;
					l.seen = SDL_GetTicks();
					matches[owner] = v.value("match", std::string());
					written.emplace_back(updated, std::move(l));
				}
			std::stable_sort(written.begin(), written.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
			std::vector<GameListing> list;
			for (auto& w : written)
				list.push_back(std::move(w.second));
			result([this, r, list = std::move(list), matches = std::move(matches)] {
				listing = false;
				if (r.status != 200) {
					printf("Matchmaking: Nakama: the games weren't listed (%d %s)\n", r.status, r.error.c_str());
					return;
				}
				found = list;
				matchOf = matches;
			});
		});
	}

	std::vector<GameListing> listings() const override { return found; }

	std::unique_ptr<GuestConnection> connect(const GameListing& listing) override;

	void tick() override
	{
		{
			std::deque<std::function<void()>> todo;
			{
				std::lock_guard lock(jobMutex);
				todo.swap(results);
			}
			for (auto& f : todo)
				f();
		}
		std::deque<std::string> messages;
		{
			std::lock_guard lock(wsMutex);
			messages.swap(incoming);
		}
		for (const std::string& text : messages) {
			const json m = json::parse(text, nullptr, false);
			if (m.is_object())
				dispatch(m);
		}
		if (current == State::READY && !wsOpen) {
			current = State::FAILED;
			stateMessage = "the connection to the server was lost";
			printf("Matchmaking: Nakama: %s\n", stateMessage.c_str());
		}
		if (hosting) {
			stun.tick();
			// the public address came: in the listing
			if (!hostMatchId.empty() && stun.publicAddress != listedStun) {
				listedStun = stun.publicAddress;
				hostListing = listingJson(lastPublished);
				writeListing();
			}
			if (SDL_GetTicks() - lastWrite > 20000)
				writeListing();
		}
		for (auto& [session, link] : transport.links)
			link->flush();
		tickGuests();
	}
	void tickGuests();
};

struct NakamaGuestConnection : GuestConnection {
	NakamaService* service;
	GameListing listing;
	std::string matchId;
	json hostPresence;
	enum Phase { JOINING, DIRECT, RELAY_ASKED, CONNECTED_DIRECT, CONNECTED_RELAY, GONE } phase = JOINING;
	std::unique_ptr<EnetGuestConnection> direct;
	EnetRaw::Stun stun;
	std::string helloSent;
	RelayLink* relay = nullptr;
	std::deque<Event> events;
	uint64_t phaseStart = 0;

	NakamaGuestConnection(NakamaService* s, const GameListing& l, const std::string& match) : service(s), listing(l), matchId(match)
	{
		phaseStart = SDL_GetTicks();
		service->guests.push_back(this);
		service->sendEnvelope({ { "match_join", { { "match_id", matchId } } } }, [this](const json& m) { joined(m); });
	}
	~NakamaGuestConnection() override
	{
		close();
		auto& g = service->guests;
		g.erase(std::remove(g.begin(), g.end(), this), g.end());
	}

	void fail(const std::string& why)
	{
		printf("Matchmaking: joining through Nakama: %s\n", why.c_str());
		if (phase != GONE)
			events.push_back(DISCONNECTED);
		phase = GONE;
	}

	void joined(const json& m)
	{
		if (phase != JOINING)
			return;
		if (!m.contains("match")) {
			fail("the game's match is gone");
			return;
		}
		for (const auto& p : m["match"].value("presences", json::array()))
			if (p.value("user_id", std::string()) == listing.id)
				hostPresence = p;
		if (hostPresence.is_null()) {
			fail("the host isn't there");
			return;
		}
		if (service->relayMode != "always" && !listing.addresses.empty()) {
			direct = std::make_unique<EnetGuestConnection>(listing.addresses, g_settings.value("hostBind", std::string()));
			if (const std::string s = MatchmakingStunServer(); !s.empty() && direct->host())
				stun.start(direct->host(), s);
			phase = DIRECT;
			sendHello();
		}
		else
			askRelay();
		phaseStart = SDL_GetTicks();
	}

	// this side's addresses (its local ones, its public one when STUN told it) for the host's punch
	void sendHello()
	{
		if (!direct || !direct->host())
			return;
		std::vector<std::string> endpoints;
		ENetAddress local;
		if (enet_socket_get_address(direct->host()->socket, &local) == 0) {
			if (local.host != ENET_HOST_ANY)
				endpoints.push_back(EnetRaw::addressText(local.host, local.port));
			else {
				std::vector<std::string> mine, broadcasts;
				LocalIPv4Addresses(mine, broadcasts);
				for (const std::string& a : mine)
					endpoints.push_back(a + ":" + std::to_string(local.port));
			}
		}
		if (!stun.publicAddress.empty())
			endpoints.push_back(stun.publicAddress);
		const std::string hello = json{ { "endpoints", endpoints } }.dump();
		if (hello != helloSent) {
			helloSent = hello;
			service->sendMatchData(matchId, OP_HELLO, hello, hostPresence);
		}
	}

	void askRelay()
	{
		if (service->relayMode == "never") {
			fail("no direct connection, and the relay is off (\"relay\": \"never\")");
			return;
		}
		if (direct) {
			stun.stop();
			direct.reset();
		}
		phase = RELAY_ASKED;
		phaseStart = SDL_GetTicks();
		service->sendMatchData(matchId, OP_RELAY, std::string(), hostPresence);
	}

	void received(const json& presence, int64_t op, const std::string& data)
	{
		if (presence.value("session_id", std::string()) != hostPresence.value("session_id", std::string()))
			return;
		if (op == OP_RELAY && phase == RELAY_ASKED) {
			relay = new RelayLink(service, matchId, hostPresence);
			phase = CONNECTED_RELAY;
			printf("Matchmaking: connected through the Nakama relay\n");
			events.push_back(CONNECTED);
		}
		else if (op == OP_DATA && relay)
			relay->received(data);
		else if (op == OP_BYE)
			hostGone();
	}

	void hostGone()
	{
		if (phase == CONNECTED_RELAY) {
			relay->closed = true;
			events.push_back(DISCONNECTED);
			phase = GONE;
		}
		else if (phase != CONNECTED_DIRECT && phase != GONE)
			fail("the host left");
	}

	void tick()
	{
		if (phase == DIRECT) {
			stun.tick();
			sendHello();
		}
		if (relay)
			relay->flush();
		// no answer to the relay request
		if (phase == RELAY_ASKED && SDL_GetTicks() - phaseStart > 10000)
			fail("the host didn't answer through the relay");
		if (phase == JOINING && SDL_GetTicks() - phaseStart > 10000)
			fail("the server didn't answer");
	}

	Event poll() override
	{
		if (phase == DIRECT || phase == CONNECTED_DIRECT) {
			const Event e = direct->poll();
			if (e == CONNECTED) {
				phase = CONNECTED_DIRECT;
				stun.stop();
				printf("Matchmaking: connected directly (%s)\n", direct->describe().c_str());
				// the match was for finding the host; the game goes through ENet
				service->sendEnvelope({ { "match_leave", { { "match_id", matchId } } } });
				return CONNECTED;
			}
			if (e == DISCONNECTED) {
				if (phase == CONNECTED_DIRECT) {
					phase = GONE;
					return DISCONNECTED;
				}
				askRelay(); // no direct way
			}
			else if (phase == DIRECT && direct && direct->trying() > 9000)
				askRelay();
		}
		if (events.empty())
			return NONE;
		const Event e = events.front();
		events.pop_front();
		return e;
	}

	NetLink* link() override
	{
		if (phase == CONNECTED_DIRECT && direct)
			return direct->link();
		return relay;
	}

	void close() override
	{
		if (phase == CONNECTED_DIRECT && direct)
			direct->close();
		else if (phase != GONE && !matchId.empty()) {
			if (!hostPresence.is_null())
				service->sendMatchData(matchId, OP_BYE, std::string(), hostPresence);
			if (relay)
				relay->flush();
			service->sendEnvelope({ { "match_leave", { { "match_id", matchId } } } });
			// the messages go out before the program goes on (it may quit)
			const uint64_t until = SDL_GetTicks() + 1000;
			for (;;) {
				{
					std::lock_guard lock(service->wsMutex);
					if (service->outgoing.empty())
						break;
				}
				if (SDL_GetTicks() > until)
					break;
				SDL_Delay(10);
			}
		}
		if (relay)
			relay->closed = true;
		phase = GONE;
	}

	std::string describe() const override
	{
		if (phase == CONNECTED_DIRECT && direct)
			return "Nakama, then " + direct->describe();
		return "Nakama relay";
	}
};

void RelayLink::disconnect(bool)
{
	if (closed)
		return;
	flush();
	service->sendMatchData(matchId, OP_BYE, std::string(), target);
	closed = true;
	// the host's side: the guest is gone from its transport
	service->transport.gone(target.value("session_id", std::string()));
}

void RelayLink::flush()
{
	if (closed || matchId.empty())
		return;
	for (int n = 0; n < 16 && !out.empty(); n++) {
		size_t at = 0;
		while (at + 2 <= out.size()) {
			uint16_t len;
			memcpy(&len, out.data() + at, 2);
			if (at > 0 && at + 2 + len > RELAY_CHUNK)
				break;
			at += 2 + len;
		}
		service->sendMatchData(matchId, OP_DATA, out.substr(0, at), target);
		out.erase(0, at);
	}
}

void NakamaService::dispatch(const json& m)
{
	if (m.contains("cid")) {
		const std::string cid = m["cid"].is_string() ? m["cid"].get<std::string>() : m["cid"].dump();
		if (auto it = pending.find(cid); it != pending.end()) {
			auto f = std::move(it->second);
			pending.erase(it);
			f(m);
			return;
		}
	}
	if (m.contains("match_data")) {
		const json& d = m["match_data"];
		const std::string match = d.value("match_id", std::string());
		const json presence = d.value("presence", json::object());
		const int64_t op = OpCode(d.value("op_code", json()));
		const std::string data = Base64Decode(d.value("data", std::string()));
		if (hosting && match == hostMatchId)
			hostReceived(presence, op, data);
		else
			for (NakamaGuestConnection* g : guests)
				if (g->matchId == match)
					g->received(presence, op, data);
	}
	else if (m.contains("match_presence_event")) {
		const json& e = m["match_presence_event"];
		const std::string match = e.value("match_id", std::string());
		for (const auto& p : e.value("leaves", json::array())) {
			const std::string session = p.value("session_id", std::string());
			if (hosting && match == hostMatchId)
				transport.gone(session);
			for (NakamaGuestConnection* g : guests)
				if (g->matchId == match && g->hostPresence.value("session_id", std::string()) == session)
					g->hostGone();
		}
	}
	else if (m.contains("error"))
		printf("Matchmaking: Nakama: %s\n", m["error"].dump().c_str());
}

void NakamaService::tickGuests()
{
	for (NakamaGuestConnection* g : std::vector<NakamaGuestConnection*>(guests))
		g->tick();
}

std::unique_ptr<GuestConnection> NakamaService::connect(const GameListing& l)
{
	auto it = matchOf.find(l.id);
	if (current != State::READY || it == matchOf.end())
		return nullptr;
	return std::make_unique<NakamaGuestConnection>(this, l, it->second);
}

}

std::unique_ptr<MatchmakingService> CreateNakamaService() { return std::make_unique<NakamaService>(); }
