// wkbai - matchmaking: finding games to join, offering one's own
// Licensed under the GNU General Public License 3

#include "Matchmaking.h"
#include "Services.h"
#include "EnetTransport.h"
#include "../settings.h"
#include "../file.h"
#include <enet/enet.h>
#include <SDL3/SDL_timer.h>
#include <algorithm>
#include <cstring>
#include <map>
#include <random>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <windows.h>
#endif

// the lobby and game messages (network.h, the lobby's text) of this build: raised when a change
// keeps older builds from playing with it
const char* MatchmakingGameVersion() { return "wkbai-net-1"; }

nlohmann::json GameListing::toJson() const
{
	nlohmann::json j = {
		{ "name", name }, { "host", hostName }, { "map", map },
		{ "players", players }, { "places", places },
		{ "spectators", spectators }, { "maxSpectators", maxSpectators }, { "allowSpectators", allowSpectators },
		{ "password", password }, { "running", running }, { "version", version },
		{ "modPolicy", modPolicy }, { "addresses", addresses }, { "hostId", hostId },
	};
	// the manifest as JSON (not text in text), so that it stays readable in a service's console
	const nlohmann::json manifest = nlohmann::json::parse(modManifest, nullptr, false);
	j["mods"] = manifest.is_discarded() ? nlohmann::json() : manifest;
	return j;
}

GameListing GameListing::fromJson(const nlohmann::json& j)
{
	GameListing l;
	if (!j.is_object())
		return l;
	auto str = [&](const char* key) { return j.contains(key) && j[key].is_string() ? j[key].get<std::string>() : std::string(); };
	auto num = [&](const char* key) { return j.contains(key) && j[key].is_number() ? j[key].get<int>() : 0; };
	auto flag = [&](const char* key) { return j.contains(key) && j[key].is_boolean() && j[key].get<bool>(); };
	l.name = str("name");
	l.hostName = str("host");
	l.map = str("map");
	l.players = num("players");
	l.places = num("places");
	l.spectators = num("spectators");
	l.maxSpectators = num("maxSpectators");
	l.allowSpectators = flag("allowSpectators");
	l.password = flag("password");
	l.running = flag("running");
	l.version = str("version");
	l.modPolicy = num("modPolicy");
	l.hostId = str("hostId");
	if (j.contains("mods") && j["mods"].is_object())
		l.modManifest = j["mods"].dump();
	if (j.contains("addresses") && j["addresses"].is_array())
		for (const auto& a : j["addresses"])
			if (a.is_string())
				l.addresses.push_back(a.get<std::string>());
	return l;
}

nlohmann::json ListFilter::toJson() const
{
	return { { "hideFull", hideFull }, { "hidePassword", hidePassword }, { "sameVersionOnly", sameVersionOnly }, { "text", text }, { "version", MatchmakingGameVersion() } };
}

bool ListFilter::accepts(const GameListing& l) const
{
	if (hideFull && l.players >= l.places && !(l.allowSpectators && l.spectators < l.maxSpectators))
		return false;
	if (hidePassword && l.password)
		return false;
	if (sameVersionOnly && l.version != MatchmakingGameVersion())
		return false;
	if (!text.empty()) {
		auto lower = [](std::string s) { std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)tolower(c); }); return s; };
		const std::string t = lower(text);
		if (lower(l.name).find(t) == std::string::npos && lower(l.hostName).find(t) == std::string::npos && lower(l.map).find(t) == std::string::npos)
			return false;
	}
	return true;
}

nlohmann::json MatchmakingSettings(const std::string& service)
{
	const nlohmann::json all = g_settings.value("matchmaking", nlohmann::json::object());
	const nlohmann::json s = all.is_object() ? all.value(service, nlohmann::json::object()) : nlohmann::json::object();
	return s.is_object() ? s : nlohmann::json::object();
}

bool SplitHostPort(const std::string& text, std::string& host, uint16_t& port, uint16_t defaultPort)
{
	std::string t = text;
	while (!t.empty() && isspace((unsigned char)t.back())) t.pop_back();
	while (!t.empty() && isspace((unsigned char)t.front())) t.erase(t.begin());
	if (t.empty())
		return false;
	port = defaultPort;
	const size_t colon = t.rfind(':');
	if (colon != std::string::npos && t.find(':') == colon) {
		const int p = atoi(t.c_str() + colon + 1);
		if (p > 0 && p < 65536)
			port = (uint16_t)p;
		t.resize(colon);
	}
	host = t;
	return !host.empty();
}

void LocalIPv4Addresses(std::vector<std::string>& addresses, std::vector<std::string>& broadcasts)
{
#ifdef _WIN32
	ULONG size = 16384;
	std::vector<uint8_t> buffer(size);
	ULONG r = GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER, nullptr, (IP_ADAPTER_ADDRESSES*)buffer.data(), &size);
	if (r == ERROR_BUFFER_OVERFLOW) {
		buffer.resize(size);
		r = GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER, nullptr, (IP_ADAPTER_ADDRESSES*)buffer.data(), &size);
	}
	if (r != NO_ERROR)
		return;
	for (auto* a = (IP_ADAPTER_ADDRESSES*)buffer.data(); a; a = a->Next) {
		if (a->OperStatus != IfOperStatusUp || a->IfType == IF_TYPE_SOFTWARE_LOOPBACK)
			continue;
		for (auto* u = a->FirstUnicastAddress; u; u = u->Next) {
			if (u->Address.lpSockaddr->sa_family != AF_INET)
				continue;
			const uint32_t ip = ((sockaddr_in*)u->Address.lpSockaddr)->sin_addr.s_addr;
			const uint8_t* b = (const uint8_t*)&ip;
			if (b[0] == 169 && b[1] == 254) // no DHCP answer: nobody else there
				continue;
			const uint32_t mask = u->OnLinkPrefixLength >= 32 ? 0xFFFFFFFFu : htonl(~(0xFFFFFFFFu >> u->OnLinkPrefixLength));
			const std::string a1 = EnetRaw::addressText(ip, 0), a2 = EnetRaw::addressText(ip | ~mask, 0);
			addresses.push_back(a1.substr(0, a1.rfind(':')));
			broadcasts.push_back(a2.substr(0, a2.rfind(':')));
		}
	}
#endif
}

std::string Base64Encode(const std::string& data)
{
	static const char* chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	std::string out;
	out.reserve((data.size() + 2) / 3 * 4);
	size_t i = 0;
	for (; i + 2 < data.size(); i += 3) {
		const uint32_t v = ((uint8_t)data[i] << 16) | ((uint8_t)data[i + 1] << 8) | (uint8_t)data[i + 2];
		out += chars[v >> 18]; out += chars[(v >> 12) & 63]; out += chars[(v >> 6) & 63]; out += chars[v & 63];
	}
	if (i + 1 == data.size()) {
		const uint32_t v = (uint8_t)data[i] << 16;
		out += chars[v >> 18]; out += chars[(v >> 12) & 63]; out += "==";
	}
	else if (i + 2 == data.size()) {
		const uint32_t v = ((uint8_t)data[i] << 16) | ((uint8_t)data[i + 1] << 8);
		out += chars[v >> 18]; out += chars[(v >> 12) & 63]; out += chars[(v >> 6) & 63]; out += '=';
	}
	return out;
}

std::string Base64Decode(const std::string& text)
{
	auto value = [](char c) -> int {
		if (c >= 'A' && c <= 'Z') return c - 'A';
		if (c >= 'a' && c <= 'z') return c - 'a' + 26;
		if (c >= '0' && c <= '9') return c - '0' + 52;
		if (c == '+' || c == '-') return 62;
		if (c == '/' || c == '_') return 63;
		return -1;
	};
	std::string out;
	uint32_t acc = 0;
	int bits = 0;
	for (char c : text) {
		const int v = value(c);
		if (v < 0)
			continue;
		acc = (acc << 6) | v;
		bits += 6;
		if (bits >= 8) {
			bits -= 8;
			out += (char)((acc >> bits) & 0xFF);
		}
	}
	return out;
}

// Pings: a query to a listing's addresses, the host's game port answers (EnetTransport.h)
namespace {
	ENetSocket g_pingSocket = ENET_SOCKET_NULL;
	std::map<uint32_t, std::pair<std::string, uint64_t>> g_pingQueries; // token: address, when
	std::map<std::string, int> g_pings; // address: ms
	std::mt19937 g_pingRandom{ std::random_device{}() };

	void receivePings()
	{
		if (g_pingSocket == ENET_SOCKET_NULL)
			return;
		uint8_t data[2048];
		for (;;) {
			ENetAddress from;
			ENetBuffer buffer;
			buffer.data = data;
			buffer.dataLength = sizeof(data);
			const int size = enet_socket_receive(g_pingSocket, &from, &buffer, 1);
			if (size <= 0) // a bigger answer than the buffer is also an answer, but not taken here
				break;
			if (size < 11 || memcmp(data, EnetRaw::ANSWER, 7) != 0)
				continue;
			uint32_t token;
			memcpy(&token, data + 7, 4);
			if (auto it = g_pingQueries.find(token); it != g_pingQueries.end()) {
				g_pings[it->second.first] = (int)(SDL_GetTicks() - it->second.second);
				g_pingQueries.erase(it);
			}
		}
	}
}

Matchmaking& Matchmaking::get()
{
	static Matchmaking m;
	return m;
}

void Matchmaking::init()
{
	if (started)
		return;
	started = true;
	enet_initialize();
	list.push_back(CreateLocalService());
	list.push_back(CreateNakamaService());
	loadPlugins();
	for (auto& s : list)
		s->signIn();
}

void Matchmaking::loadPlugins()
{
	// "matchmaking": { "plugins": false } loads none
	if (!g_settings.value("matchmaking", nlohmann::json::object()).value("plugins", true))
		return;
	std::vector<std::filesystem::path> folders;
#ifdef _WIN32
	wchar_t exe[MAX_PATH] = {};
	if (GetModuleFileNameW(nullptr, exe, MAX_PATH))
		folders.push_back(std::filesystem::path(exe).parent_path() / "plugins");
#endif
	folders.push_back(g_userPath / "plugins");
	std::set<std::string> seen;
	for (const auto& folder : folders) {
		std::error_code ec;
		for (const auto& entry : std::filesystem::directory_iterator(folder, ec)) {
			const std::filesystem::path p = entry.path();
#ifdef _WIN32
			if (p.extension() != ".dll")
				continue;
#else
			if (p.extension() != ".so" && p.extension() != ".dylib")
				continue;
#endif
			if (auto service = LoadPluginService(p)) {
				if (find(service->id()) || !seen.insert(service->id()).second) {
					printf("Matchmaking: plugin %s left out: a service \"%s\" is there already\n", PathToUtf8(p).c_str(), service->id().c_str());
					continue;
				}
				printf("Matchmaking: plugin %s (%s)\n", service->title().c_str(), PathToUtf8(p).c_str());
				list.push_back(std::move(service));
			}
		}
	}
}

void Matchmaking::shutdown()
{
	stopHosting();
	list.clear();
	if (g_pingSocket != ENET_SOCKET_NULL)
		enet_socket_destroy(g_pingSocket);
	g_pingSocket = ENET_SOCKET_NULL;
	if (started)
		enet_deinitialize();
	started = false;
}

void Matchmaking::tick()
{
	for (auto& s : list)
		s->tick();
	receivePings();
}

MatchmakingService* Matchmaking::find(const std::string& id) const
{
	for (const auto& s : list)
		if (s->id() == id)
			return s.get();
	return nullptr;
}

void Matchmaking::host(const GameListing& listing, const std::set<std::string>& where, const HostContext& context)
{
	std::set<std::string> want = where;
	want.insert("local");
	const std::string text = listing.toJson().dump();
	for (const auto& s : list) {
		const bool on = want.count(s->id()) && s->state() == MatchmakingService::State::READY;
		if (on && !offered.count(s->id())) {
			s->publish(listing, context);
			offered.insert(s->id());
			printf("Matchmaking: the game is offered on %s\n", s->title().c_str());
		}
		else if (on && text != lastListing)
			s->update(listing);
		else if (!on && offered.count(s->id())) {
			s->withdraw();
			offered.erase(s->id());
			printf("Matchmaking: the game is no longer offered on %s\n", s->title().c_str());
		}
	}
	lastListing = text;
}

void Matchmaking::stopHosting()
{
	for (const auto& s : list)
		if (offered.count(s->id()))
			s->withdraw();
	offered.clear();
	lastListing.clear();
}

std::vector<HostTransport*> Matchmaking::hostTransports() const
{
	std::vector<HostTransport*> t;
	for (const auto& s : list)
		if (offered.count(s->id()))
			if (HostTransport* h = s->hostTransport())
				t.push_back(h);
	return t;
}

void Matchmaking::refresh()
{
	for (auto& s : list)
		if (s->state() == MatchmakingService::State::READY)
			s->refresh(filter);
	probePings();
}

void Matchmaking::probePings()
{
	if (g_pingSocket == ENET_SOCKET_NULL) {
		g_pingSocket = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
		if (g_pingSocket == ENET_SOCKET_NULL)
			return;
		enet_socket_set_option(g_pingSocket, ENET_SOCKOPT_NONBLOCK, 1);
		if (const std::string bind = g_settings.value("hostBind", std::string()); !bind.empty()) {
			ENetAddress local;
			local.port = 0;
			enet_address_set_host(&local, bind.c_str());
			enet_socket_bind(g_pingSocket, &local);
		}
	}
	const uint64_t now = SDL_GetTicks();
	for (auto it = g_pingQueries.begin(); it != g_pingQueries.end();)
		it = now - it->second.second > 5000 ? g_pingQueries.erase(it) : std::next(it);
	for (const auto& s : list) {
		if (s->id() == "local") // its answers are its pings
			continue;
		for (const GameListing& l : s->listings())
			for (const std::string& a : l.addresses) {
				uint32_t ip;
				uint16_t port;
				if (!EnetRaw::resolve(a, 1234, ip, port))
					continue;
				const uint32_t token = g_pingRandom();
				g_pingQueries[token] = { a, now };
				const std::string query = std::string(EnetRaw::QUERY, 7) + std::string((const char*)&token, 4);
				ENetAddress address;
				address.host = ip;
				address.port = port;
				ENetBuffer buffer;
				buffer.data = (void*)query.data();
				buffer.dataLength = query.size();
				enet_socket_send(g_pingSocket, &address, &buffer, 1);
			}
	}
}

int MatchmakingPing(const GameListing& l)
{
	if (l.ping >= 0)
		return l.ping;
	int best = -1;
	for (const std::string& a : l.addresses)
		if (auto it = g_pings.find(a); it != g_pings.end() && (best < 0 || it->second < best))
			best = it->second;
	return best;
}
