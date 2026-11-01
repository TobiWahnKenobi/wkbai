// wkbai - matchmaking: ENet, the game's transport, for hosts and guests
// Licensed under the GNU General Public License 3
//
// Besides ENet's own packets, the game port takes a few raw datagrams of wkbai's (ENet's
// "intercept"), so that no other port has to be opened:
//   "\xFFWKBAI?" + token(4)          a query (the local network's discovery, pings): the host
//                                    answers "\xFFWKBAI!" + token + its listing (JSON)
//   "\xFFWKBAI*"                     a punch: opens the sender's NAT towards the other side
//   STUN binding responses           this side's public address (RFC 5389), when asked for

#pragma once

#include "Matchmaking.h"
#include <functional>
#include <string>
#include <vector>

struct _ENetHost;
struct _ENetAddress;
struct NetEnetLink;

namespace EnetRaw {
	extern const char QUERY[7], ANSWER[7], PUNCH[7];
	// A raw datagram the game port got: the handlers are asked in turn, true takes it
	using Handler = std::function<bool(uint32_t ip, uint16_t port, const uint8_t* data, size_t size)>;
	// returns a handle for removeHandler
	int addHandler(_ENetHost* host, Handler handler);
	void removeHandler(_ENetHost* host, int handle);
	void send(_ENetHost* host, uint32_t ip, uint16_t port, const std::string& data);
	// "a.b.c.d:port" (ip and port in network order, as ENet keeps them)
	std::string addressText(uint32_t ip, uint16_t port);
	bool resolve(const std::string& text, uint16_t defaultPort, uint32_t& ip, uint16_t& port);

	// The public address this ENet host's socket is seen at, from a STUN server ("host:port"),
	// asked once; "" until answered (or when no server is set)
	struct Stun {
		void start(_ENetHost* host, const std::string& server);
		void stop();
		void tick(); // asks again after a while when no answer came
		std::string publicAddress;
		~Stun() { stop(); }
	private:
		_ENetHost* host = nullptr;
		int handle = 0;
		uint32_t serverIp = 0;
		uint16_t serverPort = 0;
		uint8_t transaction[12] = {};
		uint64_t askedAt = 0;
		int tries = 0;
		void ask();
	};
}

// The host's game port: ENet guests (the address typed in, the local network, services that
// hand out addresses)
struct EnetHostTransport : HostTransport {
	// false: the port could not be opened (in use)
	bool open(const std::string& bind, uint16_t port);
	void close();
	void poll(std::vector<Event>& events) override;
	_ENetHost* host() const { return enet; }
	~EnetHostTransport() override { close(); }
private:
	_ENetHost* enet = nullptr;
};

// A guest's ENet connection to one of a host's addresses: all are tried at once, the first to
// answer is kept
struct EnetGuestConnection : GuestConnection {
	// bind: wkconfig.json "hostBind" (tests: this machine only), else any
	EnetGuestConnection(const std::vector<std::string>& addresses, const std::string& bind);
	~EnetGuestConnection() override;
	Event poll() override;
	NetLink* link() override;
	void close() override;
	std::string describe() const override { return "ENet " + target; }
	// before connecting, a raw datagram from this socket (a punch through this side's NAT)
	_ENetHost* host() const { return enet; }
	bool failed() const { return lost && !everConnected; }
	// how long it has been trying, milliseconds
	uint64_t trying() const;
private:
	_ENetHost* enet = nullptr;
	NetEnetLink* netLink = nullptr;
	std::string target;
	bool lost = false, everConnected = false;
	uint64_t startTime = 0;
	int pending = 0;
};
