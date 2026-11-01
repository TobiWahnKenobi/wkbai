// wkbai - matchmaking: services from plugin libraries (wkbai_mm_plugin.h)
// Licensed under the GNU General Public License 3

#include "Services.h"
#include "EnetTransport.h"
#include "wkbai_mm_plugin.h"
#include "../network.h"
#include "../settings.h"
#include "../file.h"
#include <SDL3/SDL_timer.h>
#include <deque>
#include <map>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace {

using json = nlohmann::json;

struct PluginService;

// a connection the plugin carries
struct PluginLink : NetLink {
	PluginService* service;
	int64_t connection;
	bool closed = false;
	PluginLink(PluginService* s, int64_t c) : service(s), connection(c) {}
	bool available() override;
	NetPacket receive() override;
	void send(const NetPacket& packet) override;
	void disconnect(bool afterSending) override;
	std::string next; // a packet taken to see whether there is one
	bool hasNext = false;
};

struct PluginHostTransport : HostTransport {
	std::vector<Event> events;
	void poll(std::vector<Event>& out) override
	{
		out.insert(out.end(), events.begin(), events.end());
		events.clear();
	}
};

struct PluginGuestConnection;

void PluginLog(const char* line) { printf("Matchmaking plugin: %s\n", line); }

struct PluginService : MatchmakingService {
	void* library = nullptr;
	const wkbai_mm_plugin* api = nullptr;
	wkbai_mm_service* service = nullptr;
	std::string settingsText, userPath, pluginPath, playerName, bind;
	wkbai_mm_host host = {};
	PluginHostTransport transport;
	std::map<int64_t, PluginLink*> hostLinks;
	std::map<int64_t, PluginGuestConnection*> guestConnections;
	bool hosting = false;

	~PluginService() override
	{
		if (api && service)
			api->destroy(service);
#ifdef _WIN32
		if (library)
			FreeLibrary((HMODULE)library);
#else
		if (library)
			dlclose(library);
#endif
	}

	std::string id() const override { return api->id ? api->id : "plugin"; }
	std::string title() const override { return api->title ? api->title : id(); }
	std::string icon() const override
	{
		if (!api->icon || !*api->icon)
			return std::string();
		return PathToUtf8(PathFromUtf8(pluginPath) / PathFromUtf8(api->icon));
	}
	State state() const override
	{
		const char* text = nullptr;
		switch (api->state(service, &text)) {
		case WKBAI_MM_SIGNING_IN: return State::SIGNING_IN;
		case WKBAI_MM_READY: return State::READY;
		case WKBAI_MM_FAILED: return State::FAILED;
		default: return State::OFF;
		}
	}
	std::string stateText() const override
	{
		const char* text = nullptr;
		api->state(service, &text);
		return text ? text : "";
	}
	void signIn() override { api->sign_in(service); }

	void publish(const GameListing& listing, const HostContext& context) override
	{
		lastContext = context;
		hosting = true;
		api->publish(service, listing.toJson().dump().c_str());
	}
	void update(const GameListing& listing) override { api->publish(service, listing.toJson().dump().c_str()); }
	void withdraw() override
	{
		if (!hosting)
			return;
		hosting = false;
		api->withdraw(service);
		for (auto& [c, link] : hostLinks) {
			link->closed = true;
			transport.events.push_back({ HostTransport::Event::DISCONNECTED, link });
		}
		hostLinks.clear();
	}
	HostTransport* hostTransport() override { return &transport; }

	void refresh(const ListFilter& filter) override { api->refresh(service, filter.toJson().dump().c_str()); }
	std::vector<GameListing> listings() const override
	{
		std::vector<GameListing> list;
		const char* text = api->listings(service);
		const json j = json::parse(text ? text : "[]", nullptr, false);
		if (j.is_array())
			for (const auto& item : j) {
				GameListing l = GameListing::fromJson(item);
				l.service = id();
				l.id = item.value("id", std::string());
				l.ping = item.value("ping", -1);
				l.seen = SDL_GetTicks();
				list.push_back(std::move(l));
			}
		return list;
	}
	std::unique_ptr<GuestConnection> connect(const GameListing& listing) override;
	void tick() override;
};

struct PluginGuestConnection : GuestConnection {
	PluginService* service;
	int64_t connection;
	PluginLink* netLink = nullptr;
	std::deque<Event> events;
	bool gone = false;
	PluginGuestConnection(PluginService* s, int64_t c) : service(s), connection(c) { service->guestConnections[c] = this; }
	~PluginGuestConnection() override
	{
		close();
		service->guestConnections.erase(connection);
	}
	Event poll() override
	{
		if (events.empty())
			return NONE;
		const Event e = events.front();
		events.pop_front();
		return e;
	}
	NetLink* link() override { return netLink; }
	void close() override
	{
		if (gone)
			return;
		gone = true;
		service->api->close(service->service, connection, 1);
		if (netLink)
			netLink->closed = true;
	}
	std::string describe() const override { return service->title(); }
};

bool PluginLink::available()
{
	if (hasNext)
		return true;
	if (closed)
		return false;
	char buffer[1024];
	const uint32_t size = service->api->receive(service->service, connection, buffer, sizeof(buffer));
	if (size == 0)
		return false;
	if (size <= sizeof(buffer))
		next.assign(buffer, size);
	else {
		next.resize(size);
		service->api->receive(service->service, connection, next.data(), size);
	}
	hasNext = true;
	return true;
}

NetPacket PluginLink::receive()
{
	available();
	hasNext = false;
	return NetPacket(std::move(next));
}

void PluginLink::send(const NetPacket& packet)
{
	if (!closed)
		service->api->send(service->service, connection, packet.data.data(), (uint32_t)packet.data.size());
}

void PluginLink::disconnect(bool afterSending)
{
	if (closed)
		return;
	service->api->close(service->service, connection, afterSending ? 1 : 0);
}

std::unique_ptr<GuestConnection> PluginService::connect(const GameListing& listing)
{
	const char* address = nullptr;
	const int64_t c = api->connect(service, listing.id.c_str(), &address);
	if (c == 0 && address && *address)
		return std::make_unique<EnetGuestConnection>(std::vector<std::string>{ address }, g_settings.value("hostBind", std::string()));
	if (c <= 0)
		return nullptr;
	return std::make_unique<PluginGuestConnection>(this, c);
}

void PluginService::tick()
{
	api->tick(service);
	for (;;) {
		int64_t c = 0;
		const int e = api->poll_event(service, &c);
		if (e == WKBAI_MM_EVENT_NONE)
			break;
		if (auto g = guestConnections.find(c); g != guestConnections.end()) {
			PluginGuestConnection* conn = g->second;
			if (e == WKBAI_MM_EVENT_CONNECTED && !conn->netLink) {
				conn->netLink = new PluginLink(this, c);
				conn->events.push_back(GuestConnection::CONNECTED);
			}
			else if (e == WKBAI_MM_EVENT_DISCONNECTED) {
				if (conn->netLink)
					conn->netLink->closed = true;
				conn->gone = true;
				conn->events.push_back(GuestConnection::DISCONNECTED);
			}
		}
		else if (e == WKBAI_MM_EVENT_CONNECTED && hosting && !hostLinks.count(c)) {
			PluginLink* link = new PluginLink(this, c);
			hostLinks[c] = link;
			transport.events.push_back({ HostTransport::Event::CONNECTED, link });
		}
		else if (e == WKBAI_MM_EVENT_DISCONNECTED) {
			if (auto it = hostLinks.find(c); it != hostLinks.end()) {
				it->second->closed = true;
				transport.events.push_back({ HostTransport::Event::DISCONNECTED, it->second });
				hostLinks.erase(it);
			}
		}
	}
}

}

std::unique_ptr<MatchmakingService> LoadPluginService(const std::filesystem::path& path)
{
#ifdef _WIN32
	HMODULE lib = LoadLibraryExW(path.wstring().c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
	if (!lib) {
		printf("Matchmaking: %s can't be loaded (%lu)\n", PathToUtf8(path).c_str(), GetLastError());
		return nullptr;
	}
	auto entry = (wkbai_mm_plugin_entry_fn)GetProcAddress(lib, WKBAI_MM_PLUGIN_ENTRY);
#else
	void* lib = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
	if (!lib) {
		printf("Matchmaking: %s can't be loaded (%s)\n", path.c_str(), dlerror());
		return nullptr;
	}
	auto entry = (wkbai_mm_plugin_entry_fn)dlsym(lib, WKBAI_MM_PLUGIN_ENTRY);
#endif
	const wkbai_mm_plugin* api = entry ? entry(WKBAI_MM_API_VERSION) : nullptr;
	if (!api || api->api_version != WKBAI_MM_API_VERSION || !api->create || !api->destroy || !api->tick || !api->state || !api->sign_in
		|| !api->publish || !api->withdraw || !api->refresh || !api->listings || !api->connect || !api->poll_event || !api->send
		|| !api->receive || !api->close || !api->id) {
		printf("Matchmaking: %s is not a wkbai matchmaking plugin (of interface version %d)\n", PathToUtf8(path).c_str(), WKBAI_MM_API_VERSION);
#ifdef _WIN32
		FreeLibrary(lib);
#else
		dlclose(lib);
#endif
		return nullptr;
	}
	auto s = std::make_unique<PluginService>();
	s->library = lib;
	s->api = api;
	s->settingsText = MatchmakingSettings(api->id).dump();
	s->userPath = PathToUtf8(g_userPath);
	s->pluginPath = PathToUtf8(path.parent_path());
	s->playerName = g_settings.value("playerName", std::string("Player"));
	s->bind = g_settings.value("hostBind", std::string());
	s->host.api_version = WKBAI_MM_API_VERSION;
	s->host.log = PluginLog;
	s->host.settings_json = s->settingsText.c_str();
	s->host.user_path = s->userPath.c_str();
	s->host.plugin_path = s->pluginPath.c_str();
	s->host.game_version = MatchmakingGameVersion();
	s->host.player_name = s->playerName.c_str();
	s->host.bind_address = s->bind.c_str();
	s->service = api->create(&s->host);
	if (!s->service) {
		printf("Matchmaking: the plugin %s didn't start\n", api->id);
		s->api = nullptr; // nothing to destroy
		return nullptr;
	}
	return s;
}
