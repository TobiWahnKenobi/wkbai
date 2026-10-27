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
			if (type == "H")
				slot.type = QuickSkirmishMenu::Slot::HUMAN;
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
		netInit();
		showHostWnd = true;
	}
	else if (request.action == "join")
		showJoinWnd = true;
	else if (request.action == "mods")
		modMenu.open();
	else if (request.action == "quit")
		g_windowQuit = true;
}

void QuickStartMenu::afterGame(const std::string& leaveAction, const std::string& leaveArg)
{
	newSession();
	showHostWnd = showJoinWnd = false;
	modeChosen = MODE_UNDEFINED;
	if (FrontEnd* fe = FrontEnd::get())
		fe->close();
	if (leaveAction == "restart")
		modeChosen = MODE_SINGLEPLAYER;
	else if (leaveAction == "start")
		handleRequest({ "start", leaveArg });
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

	if (showHostWnd) {
		ImGui::Begin("Host game", &showHostWnd);
		char taddress[64];
		enet_address_get_host_ip(&eserver->address, taddress, sizeof(taddress));
		ImGui::Text("IP Address: %s", taddress);
		ImGui::Text("Num players: %zu", server->clientLinks.size());
		ImGui::Separator();
		ImGui::BeginChild("Chat", ImVec2(0, 128), true);
		for (auto& s : server->chatMessages)
			ImGui::TextUnformatted(s.data(), s.data() + s.size());
		ImGui::EndChild();
		static char chatInput[128];
		bool enter = ImGui::InputText("##ChatInput", chatInput, sizeof(chatInput), ImGuiInputTextFlags_EnterReturnsTrue);
		ImGui::SameLine();
		enter |= ImGui::Button("Send");
		if (enter)
			client->sendMessage(chatInput);
		ImGui::Separator();
		if (ImGui::Button("Start"))
			modeChosen = MODE_MULTIPLAYER_HOST;
		ImGui::SameLine();
		if (ImGui::Button("Cancel"))
			showHostWnd = false;
		ImGui::End();
	}
	else {
		netDeinit();
	}

	if (showJoinWnd) {
		ImGui::Begin("Join", &showJoinWnd);
		ImGui::InputText("Address (IP)", joinAddress, sizeof(joinAddress));
		int port = (int)joinPort;
		ImGui::InputInt("Port", &port);
		joinPort = (uint16_t)port;
		if (ImGui::Button("Join")) {
			modeChosen = MODE_MULTIPLAYER_JOIN;
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel"))
			showJoinWnd = false;
		ImGui::End();
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
		netInit();
		enet_initialize();
		ENetHost* eclient;
		eclient = enet_host_create(nullptr, 1, 2, 0, 0);
		ENetAddress address;
		enet_address_set_host(&address, "localhost");
		address.port = 1234;
		ENetPeer* pserver = enet_host_connect(eclient, &address, 2, 0);

		ClientInterface cliui(client, gfx);
		client->attachInterface(&cliui);

		while (!g_windowQuit && !cliui.wantsToLeave()) {
			client->tick();

			ENetEvent event;
			while (enet_host_service(eclient, &event, 0) > 0) {
				switch (event.type) {
				case ENET_EVENT_TYPE_CONNECT: {
					printf("connection\n");
					NetEnetLink* link = new NetEnetLink(eclient, event.peer);
					client->serverLink = link;
					event.peer->data = link;
					break;
				}
				case ENET_EVENT_TYPE_DISCONNECT: {
					printf("disconnected\n");
					client->serverLink = nullptr;
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

			cliui.iter();
			runDiscordCallbacks();
		}
		leaveAction = cliui.leaveAction;
		leaveArg = cliui.leaveArg;
		enet_host_destroy(eclient);
		netDeinit();
	}
	else {
		updateDiscordRPC((modeChosen == MODE_MULTIPLAYER_HOST) ? "Multiplayer (Host)" : "Singleplayer", savegames.at(savselected).c_str());
		client->localServer = true;
		std::string savfile = std::string("Save_Games\\") + savegames.at(savselected);
		std::mutex srvMutex;
		std::atomic<bool> stopServer = false;
		std::thread srvThread([this, savfile, &srvMutex, &stopServer]() {
			tracy::SetThreadName("Server");
			srvMutex.lock();
			server->loadSaveGame(savfile.c_str());
			skirmishMenu.applySettings(*server);
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
		if (cliui.singlePlayer)
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

void QuickStartMenu::netInit()
{
	if (enetOn) return;
	enetOn = true;
	enet_initialize();
	ENetAddress address;
	address.host = ENET_HOST_ANY;
	address.port = 1234;
	eserver = enet_host_create(&address, 16, 2, 0, 0);
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
