// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#include "QuickStartMenu.h"
#include "../file.h"
#include "../imgui/imgui.h"
#include "../imguiimpl.h"
#include "../window.h"
#include "../gfx/renderer.h"
#include <SDL3/SDL_keycode.h>

#include "../server.h"
#include "../client.h"
#include "../network.h"
#include <SDL3/SDL_timer.h>
#include "ClientInterface.h"
#include "ServerDebugger.h"
#include "../SoundPlayer.h"
#include "../netenetlink.h"
#include <enet/enet.h>

#ifdef HAS_DISCORD
#include <discord_rpc.h>
#endif

#include "../settings.h"
#include <nlohmann/json.hpp>

#include <thread>
#include "../platform.h"
#include "../util/FrameStats.h"
#include <tracy/Tracy.hpp>
#include <algorithm>
#include "../util/StriCompare.h"
#include "CampaignProfile.h"
#include "../gameset/gameset.h"
#include <atomic>

namespace {
#ifdef HAS_DISCORD
	bool g_discordRpcEnabled = false;
	void initDiscordRPC() {
		if (!g_settings.value<bool>("discord-rpc", false)) return;
		static DiscordEventHandlers deh;
		deh.ready = [](const DiscordUser* request) {
			printf("Discord-RPC: Hello %s!\n", request->username);
		};
		deh.disconnected = [](int errorCode, const char* message) {
			printf("Discord-RPC: Disconnected %i, %s\n", errorCode, message);
		};
		deh.errored = [](int errorCode, const char* message) {
			printf("Discord-RPC: Error %i, %s\n", errorCode, message);
		};
		deh.joinGame = nullptr;
		deh.spectateGame = nullptr;
		deh.joinRequest = nullptr;

		Discord_Initialize("885838737379045377", &deh, 0, nullptr);
		g_discordRpcEnabled = true;
	}

	void updateDiscordRPC(const char* state, const char* details) {
		if (!g_discordRpcEnabled) return;
		DiscordRichPresence rp;
		memset(&rp, 0, sizeof(DiscordRichPresence));
		rp.state = state;
		rp.details = details;
		rp.largeImageKey = "peasont_color_0";
		rp.largeImageText = details;
		rp.smallImageKey = "peasont_color_0";
		rp.smallImageText = "Player X";
		rp.startTimestamp = time(nullptr);
		Discord_UpdatePresence(&rp);
	}

	void runDiscordCallbacks() {
		if (!g_discordRpcEnabled) return;
		Discord_RunCallbacks();
	}

	void closeDiscordRPC() {
		if (!g_discordRpcEnabled) return;
		Discord_Shutdown();
	}
#else
	void initDiscordRPC() {}
	void updateDiscordRPC(const char* state, const char* details) {}
	void runDiscordCallbacks() {}
	void closeDiscordRPC() {}
#endif
}

QuickStartMenu::QuickStartMenu(IRenderer* gfx) : gfx(gfx)
{
	refreshSaveGames();
	//SoundPlayer::getSoundPlayer()->playMusic("Warrior Kings Game Set\\Sounds\\Music\\title_version_2.mp2");
	
	newSession();

	initDiscordRPC();
	updateDiscordRPC("In menu", nullptr);

	if (BenchmarkEnabled()) {
		auto it = std::find(savegames.begin(), savegames.end(), BenchmarkSaveGame());
		if (it != savegames.end()) {
			savselected = it - savegames.begin();
			skirmishMenu.changeSettingsForLevel(*it);
			modeChosen = MODE_SINGLEPLAYER;
			// test runs: "benchmark": { ..., "slots": "H|A:<AI General>|E" } as chosen in the skirmish menu
			const std::string slots = g_settings.value("benchmark", nlohmann::json()).value("slots", std::string());
			if (!slots.empty())
				handleRequest({ "slots", slots });
		}
		else
			printf("Benchmark: save game \"%s\" not found in Save_Games\n", BenchmarkSaveGame());
	}
}

QuickStartMenu::~QuickStartMenu()
{
	closeDiscordRPC();
}

void QuickStartMenu::newSession()
{
	// The previous game's objects are not freed yet (Server and Client have no destructors)
	delete server;
	delete client;
	delete srv2cli;
	delete cli2srv;
	delete queue1;
	delete queue2;
	server = new Server;
	client = new Client;
	queue1 = new NetLocalBuffer;
	queue2 = new NetLocalBuffer;
	srv2cli = new NetLocalLink(queue1, queue2);
	cli2srv = new NetLocalLink(queue2, queue1);
	server->addClient(srv2cli);
	client->serverLink = cli2srv;
}

void QuickStartMenu::refreshSaveGames(bool keepSelection)
{
	const std::string selected = (keepSelection && savselected < savegames.size()) ? savegames[savselected] : std::string();
	auto* gsl = ListFiles("Save_Games");
	savegames = std::move(*gsl);
	delete gsl;
	savselected = 0;
	if (auto it = std::find(savegames.begin(), savegames.end(), selected); !selected.empty() && it != savegames.end())
		savselected = it - savegames.begin();
	else if (!savegames.empty()) {
		skirmishMenu.changeSettingsForLevel(savegames[0]);
	}
	if (FrontEnd* fe = FrontEnd::get())
		fe->setSaveGames(savegames);
}

void QuickStartMenu::selectSaveGame(const std::string& name)
{
	auto it = std::find(savegames.begin(), savegames.end(), name);
	if (it == savegames.end())
		return;
	const size_t index = it - savegames.begin();
	if (index != savselected) {
		savselected = index;
		skirmishMenu.changeSettingsForLevel(name);
	}
}

void QuickStartMenu::handleRequest(const FrontEnd::Request& request)
{
	if (request.action == "start") {
		selectSaveGame(request.arg);
		modeChosen = MODE_SINGLEPLAYER;
	}
	else if (request.action == "select_level")
		selectSaveGame(request.arg);
	else if (request.action == "slots") {
		// "H;c1;t1|A:<AI General>;c3|A|E": per position human, an AI General (or a random one) or
		// empty, then optionally ";c<colour>" and ";t<team>"
		std::vector<QuickSkirmishMenu::Slot> slots;
		auto split = [](const std::string& text, char sep) {
			std::vector<std::string> parts;
			size_t start = 0;
			while (start <= text.size()) {
				size_t end = text.find(sep, start);
				if (end == std::string::npos)
					end = text.size();
				parts.push_back(text.substr(start, end - start));
				start = end + 1;
			}
			return parts;
		};
		for (const std::string& part : split(request.arg, '|')) {
			if (part.empty())
				continue;
			const std::vector<std::string> fields = split(part, ';');
			QuickSkirmishMenu::Slot slot;
			const std::string& type = fields[0];
			if (type == "H") {
				slot.type = QuickSkirmishMenu::Slot::HUMAN;
				if (hosting)
					slot.name = playerName();
			}
			else if (type.size() > 1 && type[0] == 'G') {
				// a guest by its lobby id: its client index now
				slot.type = QuickSkirmishMenu::Slot::EMPTY;
				const int id = atoi(type.c_str() + 1);
				for (const auto& [link, guestId] : guestIds)
					if (guestId == id)
						if (auto it = std::find(server->clientLinks.begin(), server->clientLinks.end(), (NetLink*)link); it != server->clientLinks.end()) {
							slot.type = QuickSkirmishMenu::Slot::GUEST;
							slot.client = (int)(it - server->clientLinks.begin());
							slot.name = server->clientInfos[slot.client].name;
						}
			}
			else if (type == "E")
				slot.type = QuickSkirmishMenu::Slot::EMPTY;
			else if (type.rfind("A:", 0) == 0)
				slot.general = type.substr(2);
			for (size_t f = 1; f < fields.size(); f++) {
				if (fields[f].size() < 2)
					continue;
				const int value = atoi(fields[f].c_str() + 1);
				if (fields[f][0] == 'c') slot.colour = value;
				else if (fields[f][0] == 't') slot.team = value;
			}
			slots.push_back(slot);
		}
		skirmishMenu.setSlots(std::move(slots));
	}
	else if (request.action == "skirmish_setting") {
		// "<name>=<value>"
		const size_t eq = request.arg.find('=');
		if (eq != std::string::npos)
			skirmishMenu.setSetting(request.arg.substr(0, eq), atoi(request.arg.c_str() + eq + 1));
	}
	else if (request.action == "skirmish_settings")
		showSkirmishWnd = true;
	else if (request.action == "host") {
		// the lobby screen (FrontEndUI.lua) is open: guests can join
		netInit();
		hosting = true;
	}
	else if (request.action == "stop_hosting")
		hosting = false;
	else if (request.action == "campaign_play") {
		// "<profile>|<province>", before the "slots" and "start" of the province's level
		const size_t bar = request.arg.find('|');
		campaignProfile = request.arg.substr(0, bar);
		campaignProvince = bar == std::string::npos ? std::string() : request.arg.substr(bar + 1);
		CampaignProfile profile;
		const bool loaded = CampaignProfile::load(campaignProfile, profile);
		skirmishMenu.campaignProfile = true;
		skirmishMenu.campaignUserItems = loaded ? profile.items : std::map<std::string, float>();
	}
	else if (request.action == "mp_saved_players") {
		// "<player id>=<H|G<id>|AI>|...", before "start_multiplayer" of a save game
		savedPlayers.clear();
		size_t start = 0;
		while (start < request.arg.size()) {
			size_t end = request.arg.find('|', start);
			if (end == std::string::npos)
				end = request.arg.size();
			const std::string part = request.arg.substr(start, end - start);
			if (const size_t eq = part.find('='); eq != std::string::npos)
				savedPlayers.push_back({ (uint32_t)atoi(part.c_str()), part.substr(eq + 1) });
			start = end + 1;
		}
	}
	else if (request.action == "start_multiplayer") {
		selectSaveGame(request.arg);
		modeChosen = MODE_MULTIPLAYER_HOST;
	}
	else if (request.action == "join") {
		joinTarget = request.arg;
		modeChosen = MODE_MULTIPLAYER_JOIN;
	}
	else if (request.action == "chat") {
		if (!request.arg.empty())
			client->sendMessage(playerName() + ": " + request.arg);
	}
	else if (request.action == "lobby_state")
		lobbyText = request.arg;
	else if (request.action == "player_name" || request.action == "join_address") {
		g_settings[request.action == "player_name" ? "playerName" : "joinAddress"] = request.arg;
		SaveSettings();
	}
	else if (request.action == "mods")
		modMenu.open();
	else if (request.action == "quit")
		g_windowQuit = true;
}

void QuickStartMenu::afterGame(const std::string& leaveAction, const std::string& leaveArg)
{
	// Kingmaker campaign: a won province goes into the profile, with the USER object's items
	// (as UPDATE_USER_PROFILE kept them, else as they are now)
	const bool conquered = leaveAction == "campaign_conquered";
	const std::string profileName = campaignProfile;
	if (conquered) {
		CampaignProfile profile;
		CampaignProfile::load(campaignProfile, profile);
		profile.name = campaignProfile;
		if (std::find(profile.conquered.begin(), profile.conquered.end(), campaignProvince) == profile.conquered.end())
			profile.conquered.push_back(campaignProvince);
		if (server->userProfileUpdated)
			profile.items = server->userProfileItems;
		else if (ServerGameObject* user = server->findUser()) {
			profile.items.clear();
			for (const auto& [item, value] : user->items)
				profile.items[server->gameSet->items.names.getString(item)] = value;
		}
		printf("Campaign: %s conquered %s (%zu provinces): %s\n", campaignProfile.c_str(), campaignProvince.c_str(),
			profile.conquered.size(), profile.save() ? "saved" : "not saved");
	}
	savedPlayers.clear();
	if (leaveAction != "restart") {
		campaignProfile.clear();
		campaignProvince.clear();
		skirmishMenu.campaignProfile = false;
		skirmishMenu.campaignUserItems.clear();
	}
	newSession();
	hosting = false;
	modeChosen = MODE_UNDEFINED;
	if (FrontEnd* fe = FrontEnd::get())
		fe->close();
	if (leaveAction == "restart")
		modeChosen = MODE_SINGLEPLAYER;
	else if (leaveAction == "start")
		handleRequest({ "start", leaveArg });
	else if (conquered || leaveAction == "campaign_lost")
		if (FrontEnd* fe = FrontEnd::get())
			fe->open("campaignmap." + profileName);
}

void QuickStartMenu::draw()
{	
	// Mods can add or replace levels, so list them again after a change
	if (modMenu.draw())
		refreshSaveGames();

	if (showSkirmishWnd) {
		ImGui::Begin("Skirmish settings", &showSkirmishWnd);
		skirmishMenu.imguiWindow();
		ImGui::End();
	}

	// the network is up while the multiplayer lobby is open
	if (!hosting) {
		netDeinit();
	}
	updateLobby();
	lobbyTest();
	// Test runs: "campaignTest": { "profile": name, "province": key, "level": file, "conquerAfter": s }
	// starts a province of a campaign profile and wins it after that many seconds of play
	if (static bool started = false; !started && modeChosen == MODE_UNDEFINED) {
		const nlohmann::json test = g_settings.value("campaignTest", nlohmann::json());
		if (test.is_object()) {
			started = true;
			handleRequest({ "slots", "H;c1|A;c2|A;c3|E|E|E|E|E" });
			handleRequest({ "campaign_play", test.value("profile", std::string()) + "|" + test.value("province", std::string()) });
			handleRequest({ "start", test.value("level", std::string()) });
		}
	}

	netHandle();
	server->tick();
	runDiscordCallbacks();
}

void QuickStartMenu::launchGame()
{
	if (modeChosen == MODE_UNDEFINED)
		return;

	// What the player chose in the in-game menu when the game ended
	std::string leaveAction, leaveArg;
	// The main menu's screens are gone in the game; Esc opens the in-game menu
	if (FrontEnd* fe = FrontEnd::get())
		fe->close();

	if (modeChosen == MODE_MULTIPLAYER_JOIN) {
		updateDiscordRPC("Multiplayer", "?");
		enet_initialize();
		ENetHost* eclient = enet_host_create(nullptr, 1, 2, 0, 0);
		// "address[:port]"
		std::string host = joinTarget.empty() ? std::string("localhost") : joinTarget;
		ENetAddress address;
		address.port = 1234;
		if (const size_t colon = host.rfind(':'); colon != std::string::npos) {
			address.port = (uint16_t)atoi(host.c_str() + colon + 1);
			host.resize(colon);
		}
		const bool resolved = enet_address_set_host(&address, host.c_str()) == 0;
		if (resolved)
			enet_host_connect(eclient, &address, 2, 0);
		printf("Joining %s port %u%s\n", host.c_str(), address.port, resolved ? "" : ": address not found");
		std::string status = resolved ? "connecting" : "lost";

		ClientInterface cliui(client, gfx);
		client->attachInterface(&cliui);
		// until the host starts the game, the guest's lobby screen (FrontEndUI.lua)
		FrontEnd* fe = FrontEnd::get();
		if (fe)
			fe->open("mpguest");
		bool inLobby = true, leave = false;

		while (!g_windowQuit && !cliui.wantsToLeave() && !leave) {
			client->tick();

			ENetEvent event;
			while (enet_host_service(eclient, &event, 0) > 0) {
				switch (event.type) {
				case ENET_EVENT_TYPE_CONNECT: {
					printf("connection\n");
					NetEnetLink* link = new NetEnetLink(eclient, event.peer);
					client->serverLink = link;
					event.peer->data = link;
					client->sendLobbyHello(playerName());
					status = "connected";
					// test runs: "lobbyTest": { ..., "chat": "<text>" }
					if (const std::string chat = g_settings.value("lobbyTest", nlohmann::json()).value("chat", std::string()); !chat.empty())
						client->sendMessage(playerName() + ": " + chat);
					break;
				}
				case ENET_EVENT_TYPE_DISCONNECT: {
					printf("disconnected\n");
					client->serverLink = nullptr;
					status = "lost";
					break;
				}
				case ENET_EVENT_TYPE_RECEIVE: {
					//printf("message: %zu\n", event.packet->dataLength);
					NetEnetLink* link = (NetEnetLink*)event.peer->data;
					assert(link == client->serverLink);
					link->packets.push(event.packet);
					break;
				}
				}
			}

			if (inLobby && client->getLevel()) {
				// the host started: the game
				inLobby = false;
				if (fe)
					fe->close();
			}
			if (inLobby && fe) {
				ImGuiImpl_NewFrame();
				fe->setLobby({}, client->chatMessages, client->lobbyState, status);
				fe->frame(FrontEnd::Place::MAIN_MENU);
				for (const FrontEnd::Request& r : fe->takeRequests()) {
					if (r.action == "chat" && !r.arg.empty() && client->serverLink)
						client->sendMessage(playerName() + ": " + r.arg);
					else if (r.action == "leave_lobby")
						leave = true;
					else if (r.action == "quit")
						g_windowQuit = true;
				}
				gfx->ClearFrame();
				gfx->BeginDrawing();
				gfx->InitImGuiDrawing();
				ImGuiImpl_Render(gfx);
				// test runs: "lobbyTest": { ..., "screenshot": path } of the lobby after 6 seconds
				static const uint64_t lobbyStart = SDL_GetTicks();
				static bool lobbyShot = false;
				if (!lobbyShot && SDL_GetTicks() - lobbyStart > 6000) {
					lobbyShot = true;
					if (const std::string shot = g_settings.value("lobbyTest", nlohmann::json()).value("screenshot", std::string()); !shot.empty())
						gfx->SaveScreenshot(shot.c_str());
				}
				gfx->EndDrawing();
				HandleWindow();
			}
			else
				cliui.iter();
			runDiscordCallbacks();
		}
		if (leave)
			leaveAction = "exit_to_menu";
		else {
			leaveAction = cliui.leaveAction;
			leaveArg = cliui.leaveArg;
		}
		enet_host_destroy(eclient);
		enet_deinitialize();
	}
	else {
		updateDiscordRPC((modeChosen == MODE_MULTIPLAYER_HOST) ? "Multiplayer (Host)" : "Singleplayer", savegames.at(savselected).c_str());
		client->localServer = true;
		std::string savfile = std::string("Save_Games\\") + savegames.at(savselected);
		printf("Starting %s\n", savfile.c_str());
		std::mutex srvMutex;
		std::atomic<bool> stopServer = false;
		std::thread srvThread([this, savfile, &srvMutex, &stopServer]() {
			tracy::SetThreadName("Server");
			srvMutex.lock();
			server->loadSaveGame(savfile.c_str());
			skirmishMenu.applySettings(*server);
			applySavedPlayers();
			// a save game doesn't start its level (startLevel), which puts the cameras: put each
			// human's at its player's start
			if (savfile.size() > 4 && StrCICompare(std::string_view(savfile).substr(savfile.size() - 4), ".sav") == 0)
				for (const SrvGORef& ref : server->clientPlayerObjects)
					if (ServerGameObject* player = ref.get())
						server->snapCameraPosition(player, player->playerStartCameraPosition, player->playerStartCameraOrientation);
			if (BenchmarkPaused())
				server->timeManager.pause();
			srvMutex.unlock();
			while (!g_windowQuit && !stopServer) {
				srvMutex.lock();
				server->tick();
				srvMutex.unlock();
				_sleep(1000 / 60);
			}
			});

		ClientInterface cliui(client, gfx);
		cliui.singlePlayer = modeChosen == MODE_SINGLEPLAYER;
		// single player and the multiplayer host (who runs the game) can save
		cliui.saveGame = [this, &srvMutex](const std::string& name) {
			// a file name, not a path
			if (name.find_first_of("\\/:*?\"<>|") != std::string::npos || name.find_first_not_of(" .") == std::string::npos)
				return false;
			const std::string path = PathToUtf8(g_userPath / "saved" / "Save_Games" / PathFromUtf8(name + ".sav"));
			srvMutex.lock();
			const bool ok = server->saveGame(path.c_str());
			srvMutex.unlock();
			printf("Saving the game to %s: %s\n", path.c_str(), ok ? "done" : "failed");
			refreshSaveGames(true);
			return ok;
		};
		if (FrontEnd* fe = FrontEnd::get()) {
			// a save game (not a level) is the one "Save Game" writes to
			const std::string& file = savegames.at(savselected);
			const bool isSave = file.size() > 4 && StrCICompare(std::string_view(file).substr(file.size() - 4), ".sav") == 0;
			fe->setCurrentSave(isSave ? file.substr(0, file.size() - 4) : std::string());
		}
		client->attachInterface(&cliui);
		ServerDebugger srvdbg(server);
		bool onServerUi = false;

		while (!g_windowQuit && !cliui.wantsToLeave()) {
			// Kingmaker campaign: the province is won (CONQUER_LEVEL)
			if (server->levelConquered && !campaignProvince.empty())
				cliui.leaveAction = "campaign_conquered";
			// the scripts end the game: EXIT_TO_MAIN_MENU (in a campaign: back to its map), BOOT_LEVEL
			else if (server->exitToMenu)
				cliui.leaveAction = campaignProvince.empty() ? "exit_to_menu" : "campaign_lost";
			else if (server->levelBooted) {
				cliui.leaveAction = "start";
				cliui.leaveArg = server->bootLevel;
			}
			// test runs: "campaignTest": { ..., "conquerAfter": seconds } runs the province's ending
			// actions through the script chat ("!<action>")
			if (static bool conquerSent = false; !conquerSent && !campaignProvince.empty() && client->getLevel()) {
				const float after = g_settings.value("campaignTest", nlohmann::json()).value("conquerAfter", 0.0f);
				if (after > 0.0f && client->timeManager.currentTime > after) {
					conquerSent = true;
					client->sendMessage("!UPDATE_USER_PROFILE");
					client->sendMessage("!CONQUER_LEVEL");
				}
			}
			client->tick();
			netHandle();
			if (g_keyPressed[SDL_SCANCODE_F1]) {
				onServerUi = !onServerUi;
				if (onServerUi)
					srvMutex.lock();
				else
					srvMutex.unlock();
			}
			if (onServerUi) {
				server->tick();
				ImGuiImpl_NewFrame();
				srvdbg.draw();
				gfx->ClearFrame();
				gfx->BeginDrawing();
				gfx->InitImGuiDrawing();
				ImGuiImpl_Render(gfx);
				gfx->EndDrawing();
				HandleWindow();
			}
			else
				cliui.iter();
			runDiscordCallbacks();
		}
		if (onServerUi)
			srvMutex.unlock();

		stopServer = true;
		srvThread.join();
		leaveAction = cliui.leaveAction;
		leaveArg = cliui.leaveArg;
	}
	if (!g_windowQuit)
		afterGame(leaveAction, leaveArg);
}

// A multiplayer save game: its human players go to the host and the guests the lobby gave them;
// the ones nobody takes become AI Generals (a random one; "Setup My AI Player Stuff" starts its AI
// as the game set does for a player who leaves)
void QuickStartMenu::applySavedPlayers()
{
	if (savedPlayers.empty())
		return;
	std::vector<const GameObjBlueprint*> generals;
	const auto& playerTypes = server->gameSet->objBlueprints[Tags::GAMEOBJCLASS_PLAYER];
	for (size_t i = 0; i < playerTypes.size(); i++) {
		const std::string& name = playerTypes.names.getString(i);
		if (name.size() > 10 && StrCICompare(std::string_view(name).substr(name.size() - 10), " Kingmaker") == 0)
			generals.push_back(&playerTypes[i]);
	}
	// no client keeps a player the save gave it by its order
	for (size_t i = 0; i < server->clientLinks.size(); i++)
		server->setClientPlayerControl((int)i, 0);
	const int setupAi = server->gameSet->events.names.getIndex("Setup My AI Player Stuff");
	for (const SavedPlayer& sp : savedPlayers) {
		ServerGameObject* player = server->findObject(sp.playerId);
		if (!player)
			continue;
		int client = -1;
		if (sp.who == "H")
			client = 0;
		else if (sp.who.size() > 1 && sp.who[0] == 'G') {
			const int id = atoi(sp.who.c_str() + 1);
			for (const auto& [link, guestId] : guestIds)
				if (guestId == id)
					if (auto it = std::find(server->clientLinks.begin(), server->clientLinks.end(), (NetLink*)link); it != server->clientLinks.end())
						client = (int)(it - server->clientLinks.begin());
		}
		if (client >= 0) {
			server->setClientPlayerControl(client, player->id);
			if (client > 0 && !server->clientInfos[client].name.empty())
				player->setName(server->clientInfos[client].name);
			else if (client == 0)
				player->setName(playerName());
			printf("Save game: player %u to client %d\n", player->id, client);
		}
		else if (!generals.empty()) {
			const GameObjBlueprint* general = generals[rand() % generals.size()];
			player->convertTo(general);
			if (setupAi != -1)
				player->sendEvent(setupAi);
			printf("Save game: player %u to the AI (%s)\n", player->id, general->name.c_str());
		}
	}
}

std::string QuickStartMenu::playerName() const
{
	return g_settings.value("playerName", std::string("Player"));
}

void QuickStartMenu::updateLobby()
{
	FrontEnd* fe = FrontEnd::get();
	if (!fe || !hosting) {
		guestIds.clear();
		return;
	}
	std::vector<FrontEnd::Guest> list;
	for (NetEnetLink* link : guests) {
		auto [it, isNew] = guestIds.try_emplace(link, nextGuestId);
		if (isNew)
			nextGuestId++;
		std::string name;
		if (auto ci = std::find(server->clientLinks.begin(), server->clientLinks.end(), (NetLink*)link); ci != server->clientLinks.end())
			name = server->clientInfos[ci - server->clientLinks.begin()].name;
		list.push_back({ it->second, name.empty() ? "Player " + std::to_string(it->second) : name });
	}
	for (auto it = guestIds.begin(); it != guestIds.end();)
		it = (std::find(guests.begin(), guests.end(), it->first) == guests.end()) ? guestIds.erase(it) : std::next(it);
	fe->setLobby(std::move(list), client->chatMessages, std::string(), "connected");

	// the lobby for the guests, again every second (for the ones who just joined), in parts
	const uint64_t now = SDL_GetTicks();
	if (!lobbyText.empty() && now - lobbySentTime >= 1000) {
		lobbySentTime = now;
		const size_t PART = 200;
		const size_t parts = std::min<size_t>(255, (lobbyText.size() + PART - 1) / PART);
		for (size_t i = 0; i < parts; i++) {
			NetPacketWriter msg(NETCLIMSG_LOBBY_STATE);
			msg.writeUint8((uint8_t)i);
			msg.writeUint8((uint8_t)parts);
			msg.writeStringZ(lobbyText.substr(i * PART, PART));
			for (NetEnetLink* link : guests)
				static_cast<NetLink*>(link)->send(msg);
		}
	}
}

// Test runs (two instances on one machine): "lobbyTest": { "host": "<level>", "guestSlot": n }
// hosts the level and starts once a guest has joined, with the guest at position n;
// { "join": "<address>", "chat": "<text>" } joins and says something in the lobby
void QuickStartMenu::lobbyTest()
{
	static bool done = false;
	const nlohmann::json test = g_settings.value("lobbyTest", nlohmann::json());
	if (done || !test.is_object() || modeChosen != MODE_UNDEFINED)
		return;
	if (test.contains("join")) {
		done = true;
		handleRequest({ "join", test.value("join", std::string("localhost")) });
		return;
	}
	FrontEnd* fe = FrontEnd::get();
	// { "load": "<save game>" }: host a saved game (the menu script matches the players and starts)
	if (const std::string save = test.value("load", std::string()); !save.empty()) {
		done = true;
		handleRequest({ "host", "" });
		if (fe)
			fe->open("mpload." + save);
		return;
	}
	const std::string level = test.value("host", std::string());
	if (level.empty())
		return;
	if (!hosting) {
		handleRequest({ "select_level", level });
		handleRequest({ "host", "" });
		if (fe)
			fe->open("mphost");
		return;
	}
	// a guest with its name: wait a moment for its chat, then start
	static uint64_t joinedTime = 0;
	if (guests.empty() || guestIds.empty())
		return;
	auto ci = std::find(server->clientLinks.begin(), server->clientLinks.end(), (NetLink*)guests[0]);
	if (ci == server->clientLinks.end() || server->clientInfos[ci - server->clientLinks.begin()].name.empty())
		return;
	if (!joinedTime)
		joinedTime = SDL_GetTicks();
	if (SDL_GetTicks() - joinedTime < test.value("startAfter", 3000u))
		return;
	done = true;
	const int players = atoi(level.c_str()) > 0 ? atoi(level.c_str()) : 2;
	const int guestSlot = test.value("guestSlot", 2);
	std::string slots;
	for (int i = 1; i <= players; i++) {
		if (i > 1)
			slots += "|";
		slots += (i == 1) ? "H;c1" : (i == guestSlot) ? "G" + std::to_string(guestIds[guests[0]]) + ";c2" : "E";
	}
	printf("Lobby test: starting %s with slots %s\n", level.c_str(), slots.c_str());
	handleRequest({ "slots", slots });
	handleRequest({ "start_multiplayer", level });
}

void QuickStartMenu::netInit()
{
	if (enetOn) return;
	enetOn = true;
	enet_initialize();
	ENetAddress address;
	address.host = ENET_HOST_ANY;
	address.port = 1234;
	// "hostBind": "127.0.0.1" only takes guests from this machine (tests)
	if (const std::string bind = g_settings.value("hostBind", std::string()); !bind.empty())
		enet_address_set_host(&address, bind.c_str());
	eserver = enet_host_create(&address, 16, 2, 0, 0);
	if (!eserver) {
		printf("Hosting: could not open port %u\n", address.port);
		enetOn = false;
		enet_deinitialize();
	}
}

void QuickStartMenu::netHandle()
{
	if (!enetOn) return;
	ENetEvent event;
	while (enet_host_service(eserver, &event, 0) > 0) {
		switch (event.type) {
		case ENET_EVENT_TYPE_CONNECT: {
			printf("connection\n");
			NetEnetLink* link = new NetEnetLink(eserver, event.peer);
			event.peer->data = link;
			guests.push_back(link);
			if (server)
				server->addClient(link);
			break;
		}
		case ENET_EVENT_TYPE_DISCONNECT: {
			printf("disconnected\n");
			guests.erase(std::find(guests.begin(), guests.end(), (NetEnetLink*)event.peer->data));
			if (server)
				server->removeClient((NetEnetLink*)event.peer->data);
			break;
		}
		case ENET_EVENT_TYPE_RECEIVE: {
			printf("message: %zu\n", event.packet->dataLength);
			NetEnetLink* link = (NetEnetLink*)event.peer->data;
			link->packets.push(event.packet);
			break;
		}
		}
	}
}

void QuickStartMenu::netDeinit()
{
	if (!enetOn) return;
	enetOn = false;
	enet_host_destroy(eserver);
	enet_deinitialize();
}
