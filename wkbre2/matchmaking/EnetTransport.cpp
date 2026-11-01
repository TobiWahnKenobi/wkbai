// wkbai - matchmaking: ENet, the game's transport, for hosts and guests
// Licensed under the GNU General Public License 3

#include "EnetTransport.h"
#include "../netenetlink.h"
#include <enet/enet.h>
#include <SDL3/SDL_timer.h>
#include <algorithm>
#include <cstring>
#include <map>
#include <random>

namespace EnetRaw {
	const char QUERY[7] = { '\xFF', 'W', 'K', 'B', 'A', 'I', '?' };
	const char ANSWER[7] = { '\xFF', 'W', 'K', 'B', 'A', 'I', '!' };
	const char PUNCH[7] = { '\xFF', 'W', 'K', 'B', 'A', 'I', '*' };

	namespace {
		struct Handlers {
			std::vector<std::pair<int, Handler>> list;
		};
		std::map<ENetHost*, Handlers> g_handlers;
		int g_nextHandle = 1;

		int ENET_CALLBACK intercept(ENetHost* host, ENetEvent* event)
		{
			auto it = g_handlers.find(host);
			if (it == g_handlers.end())
				return 0;
			// a copy: a handler may add or remove handlers
			const auto handlers = it->second.list;
			for (const auto& [handle, handler] : handlers)
				if (handler(host->receivedAddress.host, host->receivedAddress.port, host->receivedData, host->receivedDataLength))
					return 1;
			return 0;
		}
	}

	int addHandler(_ENetHost* host, Handler handler)
	{
		host->intercept = intercept;
		const int handle = g_nextHandle++;
		g_handlers[host].list.emplace_back(handle, std::move(handler));
		return handle;
	}

	void removeHandler(_ENetHost* host, int handle)
	{
		auto it = g_handlers.find(host);
		if (it == g_handlers.end())
			return;
		auto& l = it->second.list;
		l.erase(std::remove_if(l.begin(), l.end(), [handle](const auto& h) { return h.first == handle; }), l.end());
		if (l.empty())
			g_handlers.erase(it);
	}

	void removeAll(_ENetHost* host) { g_handlers.erase(host); }

	// the other side's punches are only there to open the way
	bool swallowPunch(uint32_t, uint16_t, const uint8_t* data, size_t size) { return size >= 7 && memcmp(data, PUNCH, 7) == 0; }

	void send(_ENetHost* host, uint32_t ip, uint16_t port, const std::string& data)
	{
		ENetAddress address;
		address.host = ip;
		address.port = port;
		ENetBuffer buffer;
		buffer.data = (void*)data.data();
		buffer.dataLength = data.size();
		enet_socket_send(host->socket, &address, &buffer, 1);
	}

	std::string addressText(uint32_t ip, uint16_t port)
	{
		const uint8_t* b = (const uint8_t*)&ip;
		return std::to_string(b[0]) + "." + std::to_string(b[1]) + "." + std::to_string(b[2]) + "." + std::to_string(b[3]) + ":" + std::to_string(port);
	}

	bool resolve(const std::string& text, uint16_t defaultPort, uint32_t& ip, uint16_t& port)
	{
		std::string name;
		if (!SplitHostPort(text, name, port, defaultPort))
			return false;
		ENetAddress address;
		if (enet_address_set_host(&address, name.c_str()) != 0)
			return false;
		ip = address.host;
		return true;
	}

	// STUN (RFC 5389): a binding request; the answer's (XOR-)MAPPED-ADDRESS is how the server
	// saw this socket
	void Stun::start(_ENetHost* h, const std::string& server)
	{
		stop();
		publicAddress.clear();
		if (server.empty() || !h)
			return;
		if (!resolve(server, 3478, serverIp, serverPort)) {
			printf("Matchmaking: STUN server %s not found\n", server.c_str());
			return;
		}
		host = h;
		std::random_device rd;
		for (uint8_t& b : transaction)
			b = (uint8_t)rd();
		handle = addHandler(host, [this](uint32_t ip, uint16_t port, const uint8_t* data, size_t size) {
			if (size < 20 || ip != serverIp || port != serverPort)
				return false;
			const uint16_t type = (data[0] << 8) | data[1];
			const uint32_t cookie = (data[4] << 24) | (data[5] << 16) | (data[6] << 8) | data[7];
			if (type != 0x0101 || cookie != 0x2112A442 || memcmp(data + 8, transaction, 12) != 0)
				return false;
			const size_t length = std::min<size_t>(size - 20, (data[2] << 8) | data[3]);
			for (size_t at = 20; at + 4 <= 20 + length;) {
				const uint16_t attr = (data[at] << 8) | data[at + 1];
				const uint16_t len = (data[at + 2] << 8) | data[at + 3];
				const uint8_t* v = data + at + 4;
				if ((attr == 0x0020 || attr == 0x0001) && len >= 8 && v[1] == 0x01 && at + 4 + len <= size) {
					uint16_t p = (v[2] << 8) | v[3];
					uint8_t a[4] = { v[4], v[5], v[6], v[7] };
					if (attr == 0x0020) {
						p ^= 0x2112;
						a[0] ^= 0x21; a[1] ^= 0x12; a[2] ^= 0xA4; a[3] ^= 0x42;
					}
					uint32_t mapped;
					memcpy(&mapped, a, 4);
					publicAddress = addressText(mapped, p);
					if (attr == 0x0020)
						break;
				}
				at += 4 + ((len + 3) & ~3);
			}
			if (!publicAddress.empty())
				printf("Matchmaking: this game port is seen at %s\n", publicAddress.c_str());
			return true;
		});
		tries = 0;
		ask();
	}

	void Stun::ask()
	{
		uint8_t request[20] = { 0x00, 0x01, 0x00, 0x00, 0x21, 0x12, 0xA4, 0x42 };
		memcpy(request + 8, transaction, 12);
		send(host, serverIp, serverPort, std::string((const char*)request, sizeof(request)));
		askedAt = SDL_GetTicks();
		tries++;
	}

	void Stun::tick()
	{
		if (host && publicAddress.empty() && tries < 4 && SDL_GetTicks() - askedAt > 1500)
			ask();
	}

	void Stun::stop()
	{
		if (host && handle)
			removeHandler(host, handle);
		host = nullptr;
		handle = 0;
	}
}

bool EnetHostTransport::open(const std::string& bind, uint16_t port)
{
	if (enet)
		return true;
	ENetAddress address;
	address.host = ENET_HOST_ANY;
	address.port = port;
	if (!bind.empty())
		enet_address_set_host(&address, bind.c_str());
	enet = enet_host_create(&address, 16, 2, 0, 0);
	if (!enet) {
		printf("Hosting: could not open port %u\n", port);
		return false;
	}
	EnetRaw::addHandler(enet, EnetRaw::swallowPunch);
	return true;
}

void EnetHostTransport::close()
{
	if (!enet)
		return;
	EnetRaw::removeAll(enet);
	enet_host_destroy(enet);
	enet = nullptr;
}

void EnetHostTransport::poll(std::vector<Event>& events)
{
	if (!enet)
		return;
	ENetEvent event;
	while (enet_host_service(enet, &event, 0) > 0) {
		switch (event.type) {
		case ENET_EVENT_TYPE_CONNECT: {
			printf("connection\n");
			NetEnetLink* link = new NetEnetLink(enet, event.peer);
			event.peer->data = link;
			// a guest loading the game set (or a running game's state) answers nothing for a while
			enet_peer_timeout(event.peer, 32, 20000, 60000);
			events.push_back({ Event::CONNECTED, link });
			break;
		}
		case ENET_EVENT_TYPE_DISCONNECT:
			printf("disconnected\n");
			if (event.peer->data)
				events.push_back({ Event::DISCONNECTED, (NetEnetLink*)event.peer->data });
			event.peer->data = nullptr;
			break;
		case ENET_EVENT_TYPE_RECEIVE:
			if (NetEnetLink* link = (NetEnetLink*)event.peer->data)
				link->packets.push(event.packet);
			else
				enet_packet_destroy(event.packet);
			break;
		default:
			break;
		}
	}
}

EnetGuestConnection::EnetGuestConnection(const std::vector<std::string>& addresses, const std::string& bind)
{
	startTime = SDL_GetTicks();
	ENetAddress local;
	local.host = ENET_HOST_ANY;
	local.port = 0;
	if (!bind.empty())
		enet_address_set_host(&local, bind.c_str());
	enet = enet_host_create(bind.empty() ? nullptr : &local, std::max<size_t>(1, addresses.size()), 2, 0, 0);
	if (!enet) {
		lost = true;
		return;
	}
	EnetRaw::addHandler(enet, EnetRaw::swallowPunch);
	for (const std::string& a : addresses) {
		uint32_t ip;
		uint16_t port;
		if (!EnetRaw::resolve(a, 1234, ip, port)) {
			printf("Joining %s: address not found\n", a.c_str());
			continue;
		}
		ENetAddress address;
		address.host = ip;
		address.port = port;
		if (ENetPeer* peer = enet_host_connect(enet, &address, 2, 0)) {
			// an address that doesn't answer is given up sooner than a game in progress
			enet_peer_timeout(peer, 32, 5000, 10000);
			pending++;
			target += (target.empty() ? "" : ", ") + a;
			printf("Joining %s\n", a.c_str());
		}
	}
	if (pending == 0)
		lost = true;
}

EnetGuestConnection::~EnetGuestConnection()
{
	if (enet) {
		EnetRaw::removeAll(enet);
		enet_host_destroy(enet);
	}
}

uint64_t EnetGuestConnection::trying() const { return SDL_GetTicks() - startTime; }

GuestConnection::Event EnetGuestConnection::poll()
{
	if (!enet) {
		if (pending < 0)
			return NONE;
		pending = -1; // said once
		return DISCONNECTED;
	}
	ENetEvent event;
	while (enet_host_service(enet, &event, 0) > 0) {
		switch (event.type) {
		case ENET_EVENT_TYPE_CONNECT:
			if (netLink) {
				enet_peer_disconnect_now(event.peer, 0); // another address answered too
				break;
			}
			printf("connection\n");
			netLink = new NetEnetLink(enet, event.peer);
			event.peer->data = netLink;
			enet_peer_timeout(event.peer, 32, 20000, 60000);
			target = EnetRaw::addressText(event.peer->address.host, event.peer->address.port);
			everConnected = true;
			// the other addresses tried are given up
			for (size_t i = 0; i < enet->peerCount; i++)
				if (&enet->peers[i] != event.peer && enet->peers[i].state != ENET_PEER_STATE_DISCONNECTED)
					enet_peer_reset(&enet->peers[i]);
			return CONNECTED;
		case ENET_EVENT_TYPE_DISCONNECT:
			if (event.peer->data && event.peer->data == netLink) {
				printf("disconnected\n");
				event.peer->data = nullptr;
				lost = true;
				return DISCONNECTED;
			}
			if (!everConnected && --pending <= 0) {
				printf("Joining %s: no answer\n", target.c_str());
				lost = true;
				pending = -1;
				return DISCONNECTED;
			}
			break;
		case ENET_EVENT_TYPE_RECEIVE:
			if (netLink && event.peer->data == netLink)
				netLink->packets.push(event.packet);
			else
				enet_packet_destroy(event.packet);
			break;
		default:
			break;
		}
	}
	if (lost && !everConnected && pending == 0) {
		pending = -1; // said once
		return DISCONNECTED;
	}
	return NONE;
}

NetLink* EnetGuestConnection::link() { return netLink; }

void EnetGuestConnection::close()
{
	// the host is told at once (else it waits for the timeout before an AI takes over)
	if (!enet || !netLink || lost)
		return;
	enet_peer_disconnect(netLink->peer, 0);
	ENetEvent event;
	const uint64_t until = SDL_GetTicks() + 1000;
	bool gone = false;
	while (!gone && SDL_GetTicks() < until && enet_host_service(enet, &event, 50) >= 0)
		if (event.type == ENET_EVENT_TYPE_DISCONNECT)
			gone = true;
		else if (event.type == ENET_EVENT_TYPE_RECEIVE)
			enet_packet_destroy(event.packet);
	lost = true;
}
