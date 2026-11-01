// wkbai - matchmaking: games on the local network
// Licensed under the GNU General Public License 3
//
// A guest sends "\xFFWKBAI?" + token to the game port of every broadcast address (and of this
// machine, and of the address typed in); a host's game port answers with its listing
// (EnetTransport.h). No port besides the game's own is opened.

#include "Services.h"
#include "EnetTransport.h"
#include "../settings.h"
#include <enet/enet.h>
#include <SDL3/SDL_timer.h>
#include <cstring>
#include <map>
#include <random>

namespace {

struct LocalService : MatchmakingService {
	std::string id() const override { return "local"; }
	std::string title() const override { return "Local network"; }
	State state() const override { return State::READY; }

	// Host
	_ENetHost* hostEnet = nullptr;
	int handle = 0;
	std::string answer; // the listing as sent

	void publish(const GameListing& listing, const HostContext& context) override
	{
		lastContext = context;
		answer = listing.toJson().dump();
		if (hostEnet != context.enet) {
			withdraw();
			answer = listing.toJson().dump();
			hostEnet = context.enet;
			if (!hostEnet)
				return;
			handle = EnetRaw::addHandler(hostEnet, [this](uint32_t ip, uint16_t port, const uint8_t* data, size_t size) {
				if (size < 11 || memcmp(data, EnetRaw::QUERY, 7) != 0)
					return false;
				// the token back, so the guest knows which query (and its ping)
				EnetRaw::send(hostEnet, ip, port, std::string(EnetRaw::ANSWER, 7) + std::string((const char*)data + 7, 4) + answer);
				return true;
			});
		}
	}
	void update(const GameListing& listing) override { answer = listing.toJson().dump(); }
	void withdraw() override
	{
		if (hostEnet && handle)
			EnetRaw::removeHandler(hostEnet, handle);
		hostEnet = nullptr;
		handle = 0;
	}

	// Guest
	ENetSocket sock = ENET_SOCKET_NULL;
	std::map<uint32_t, uint64_t> queries; // token: when sent
	std::map<std::string, GameListing> found; // by the host's game id
	std::mt19937 random{ std::random_device{}() };

	~LocalService() override
	{
		withdraw();
		if (sock != ENET_SOCKET_NULL)
			enet_socket_destroy(sock);
	}

	bool openSocket()
	{
		if (sock != ENET_SOCKET_NULL)
			return true;
		sock = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
		if (sock == ENET_SOCKET_NULL)
			return false;
		enet_socket_set_option(sock, ENET_SOCKOPT_NONBLOCK, 1);
		enet_socket_set_option(sock, ENET_SOCKOPT_BROADCAST, 1);
		// "hostBind": "127.0.0.1" (tests): this machine only, no firewall prompt
		if (const std::string bind = g_settings.value("hostBind", std::string()); !bind.empty()) {
			ENetAddress local;
			local.port = 0;
			enet_address_set_host(&local, bind.c_str());
			enet_socket_bind(sock, &local);
		}
		return true;
	}

	// where the query goes: "matchmaking": { "local": { "targets": [...], "port": n } }, else
	// the local networks' broadcast addresses, this machine and the address typed in
	std::vector<std::string> targets() const
	{
		const nlohmann::json settings = MatchmakingSettings("local");
		const uint16_t port = (uint16_t)settings.value("port", g_settings.value("hostPort", 1234));
		std::vector<std::string> list;
		if (settings.contains("targets")) {
			for (const auto& t : settings["targets"])
				if (t.is_string())
					list.push_back(t.get<std::string>());
		}
		else {
			std::vector<std::string> addresses, broadcasts;
			LocalIPv4Addresses(addresses, broadcasts);
			list = broadcasts;
			list.push_back("255.255.255.255");
			list.push_back("127.0.0.1");
			if (const std::string typed = g_settings.value("joinAddress", std::string()); !typed.empty())
				list.push_back(typed);
		}
		for (std::string& t : list) {
			std::string host;
			uint16_t p;
			if (SplitHostPort(t, host, p, port))
				t = host + ":" + std::to_string(p);
		}
		return list;
	}

	void refresh(const ListFilter&) override
	{
		if (!openSocket())
			return;
		const uint32_t token = random();
		queries[token] = SDL_GetTicks();
		const std::string query = std::string(EnetRaw::QUERY, 7) + std::string((const char*)&token, 4);
		for (const std::string& t : targets()) {
			uint32_t ip;
			uint16_t port;
			if (!EnetRaw::resolve(t, 1234, ip, port))
				continue;
			ENetAddress address;
			address.host = ip;
			address.port = port;
			ENetBuffer buffer;
			buffer.data = (void*)query.data();
			buffer.dataLength = query.size();
			enet_socket_send(sock, &address, &buffer, 1);
		}
		// queries older than 10 s are given up, and games not seen for 12 s
		const uint64_t now = SDL_GetTicks();
		for (auto it = queries.begin(); it != queries.end();)
			it = now - it->second > 10000 ? queries.erase(it) : std::next(it);
		for (auto it = found.begin(); it != found.end();)
			it = now - it->second.seen > 12000 ? found.erase(it) : std::next(it);
	}

	void tick() override
	{
		if (sock == ENET_SOCKET_NULL)
			return;
		static uint8_t data[65536];
		for (;;) {
			ENetAddress from;
			ENetBuffer buffer;
			buffer.data = data;
			buffer.dataLength = sizeof(data);
			const int size = enet_socket_receive(sock, &from, &buffer, 1);
			if (size <= 0)
				break;
			if (size < 11 || memcmp(data, EnetRaw::ANSWER, 7) != 0)
				continue;
			uint32_t token;
			memcpy(&token, data + 7, 4);
			auto q = queries.find(token);
			if (q == queries.end())
				continue;
			const nlohmann::json j = nlohmann::json::parse(std::string((const char*)data + 11, size - 11), nullptr, false);
			if (!j.is_object())
				continue;
			GameListing l = GameListing::fromJson(j);
			l.service = "local";
			const std::string address = EnetRaw::addressText(from.host, from.port);
			l.ping = (int)(SDL_GetTicks() - q->second);
			l.seen = SDL_GetTicks();
			// one host answers through several addresses (broadcast, this machine): kept once,
			// at the first address it answered from
			const std::string key = j.value("hostId", address);
			auto it = found.find(key);
			if (it != found.end() && it->second.addresses.size() == 1 && it->second.addresses[0] != address && it->second.seen + 3000 > l.seen) {
				it->second.seen = l.seen;
				continue;
			}
			l.id = address;
			l.addresses = { address };
			found[key] = l;
		}
	}

	std::vector<GameListing> listings() const override
	{
		std::vector<GameListing> list;
		for (const auto& [key, l] : found)
			list.push_back(l);
		return list;
	}

	std::unique_ptr<GuestConnection> connect(const GameListing& listing) override
	{
		return std::make_unique<EnetGuestConnection>(listing.addresses, g_settings.value("hostBind", std::string()));
	}
};

}

std::unique_ptr<MatchmakingService> CreateLocalService() { return std::make_unique<LocalService>(); }
