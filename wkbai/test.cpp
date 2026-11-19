// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#include "wkbre2.h"
#include "platform.h"
#include "Environment.h"

#include <map>
#include <functional>
#include <string>
#include <cstring>
//#include <direct.h>
#include <utility>
#include <thread>
#include <fstream>
#include <sstream>
#include <array>
#include "gameset/gameset.h"
#include "file.h"
#include "gameset/values.h"
#include "server.h"
#include "window.h"
#include "gfx/renderer.h"
#include "imgui/imgui.h"
#include "imguiimpl.h"
#include "gfx/TextureCache.h"
#include "gfx/TeamEmblems.h"
#include "Model.h"
#include "TrnTextureDb.h"
#include "terrain.h"
#include "MapFolder.h"
#include "MapAuthoring.h"
#include "util/StriCompare.h"
#include "gfx/bitmap.h"
#include <SDL3/SDL_keycode.h>
#include "mesh.h"
#include "client.h"
#include "network.h"
#include "netenetlink.h"
#include "gfx/SceneRenderer.h"
#include "gfx/DefaultSceneRenderer.h"
#include "interface/ServerDebugger.h"
#include "interface/ClientDebugger.h"
#include "anim.h"
#include "Camera.h"
#include "gfx/DefaultTerrainRenderer.h"
#include "interface/ClientInterface.h"
#include "interface/KeyBindings.h"
#include "scene.h"
#include <SDL3/SDL_timer.h>
#include "SoundPlayer.h"
#include "Pathfinding.h"
#include "NNSearch.h"
#include "ParticleSystem.h"
#include "gfx/DefaultParticleRenderer.h"
#include "ParticleContainer.h"
#include "settings.h"
#include "MovementController.h"
#include "WaterDesign.h"
#include "FormationController.h"
#include <set>
#include "interface/QuickSkirmishMenu.h"
#include "AIUtil.h"
#include "MinistryDepartments.h"
#include "gameset/finder.h"
#include "gameset/Commission.h"
#include "AIController.h"
#include "gameset/ScriptContext.h"
#include "gameset/actions.h"
#include "util/GSFileParser.h"
#include "util/SimRandom.h"
#include "StampdownPlan.h"
#include "matchmaking/Matchmaking.h"
#include <SDL3/SDL.h>
#include "BuildIdentity.h"
#include "AgentInput.h"
#include "interface/ChronicleNarrator.h"
#include "interface/FogOfWar.h"
#include "gfx/ChimneySmoke.h"
#include "gfx/BloodDrip.h"
#include "gfx/FireEffects.h"
#include "gfx/RenderSettings.h"
#include "interface/MenuOptions.h"
#include "Wind.h"
#include <nlohmann/json.hpp>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <enet/enet.h>

void Test_GameSet()
{
	LoadBCP("data.bcp"); // C:\\Apps\\Warrior Kings - Battles\\

	// Use the configured game version like Server::loadSaveGame does; Battles scripts need the Battles readers
	GameSet gameSet("Warrior Kings Game Set\\multi-player extensions.cpp", g_settings.value<int>("gameVersion", 0));

	printf("List of %i items:\n", gameSet.items.names.size());
	for (const std::string &item : gameSet.items.names)
		printf("%s\n", item.c_str());

	printf("List of %i defined values:\n", gameSet.definedValues.size());
	for (auto &dv : gameSet.definedValues)
		printf("%s: %f\n", dv.first.c_str(), dv.second);

	//printf("List of %i equations are their evaluations:\n", gameSet.equationNames.size());
	//for (int i = 0; i < gameSet.equationNames.size(); i++) {
	//	float v = gameSet.equations[i]->eval(nullptr);
	//	printf("%s -> %f\n", gameSet.equationNames[i].c_str(), v);
	//}

	if (const GameObjBlueprint *bp = gameSet.findBlueprint(Tags::GAMEOBJCLASS_CHARACTER, "Peasant")) {
		printf("%p", bp);
	}

	getchar();
}

void Test_GSFileParser()
{
	char *test; int testsize;
	LoadBCP("data.bcp");
	LoadFile("Warrior Kings Game Set\\Interrogate.cpp", &test, &testsize, 1);
	test[testsize] = 0;

	GSFileParser gsf(test);
	int linecnt = 1;
	while (!gsf.eof)
	{
		printf("Line %i\n", linecnt++);
		int tagcnt = 1;
		while (!gsf.eol)
		{
			printf("Tag %i: \"%s\"\n", tagcnt++, gsf.nextString().c_str());
		}
		gsf.advanceLine();
		getchar();
	}

	printf("End of file");
	getchar();
}

void Test_Server()
{
	LoadBCP("data.bcp");
	Server server;
	printf("Loading savegame\n");
	server.loadSaveGame("Save_Games\\Interactive Tutorial.lvl");
	printf("Savegame loaded!\n");
	getchar();
}

void Test_Actions()
{
	LoadBCP("data.bcp");
	Server server;
	server.loadSaveGame("Save_Games\\heymap.sav");
	printf("Savegame loaded!\n");

	int actseq = server.gameSet->actionSequences.names.getIndex("My act seq");
	//server.gameSet->actionSequences[actseq].run(server.level);

	getchar();
}

void Test_SDL()
{
	InitWindow();
	IRenderer *gfx = CreateRenderer();
	gfx->Init();
	while (!g_windowQuit) {
		gfx->ClearFrame();
		gfx->BeginDrawing();
		gfx->InitRectDrawing();
		gfx->DrawFrame(64, 64, 128, 128, -1);
		gfx->EndDrawing();
		HandleWindow();
	}
}

void Test_ImGui()
{
	LoadBCP("data.bcp");
	InitWindow();
	ImGuiImpl_Init();

	IRenderer *gfx = CreateRenderer();
	gfx->Init();
	ImGuiImpl_CreateFontsTexture(gfx);

	TextureCache texcache(gfx);
	texture mytex = texcache.getTexture("default_create.tga");

	while (!g_windowQuit) {
		ImGuiImpl_NewFrame();
		ImGui::Text("Hello guys and gals! :)");
		ImGui::Image(mytex, ImVec2(64, 64));

		gfx->ClearFrame();
		gfx->BeginDrawing();
		gfx->InitImGuiDrawing();
		ImGuiImpl_Render(gfx);
		gfx->EndDrawing();

		HandleWindow();
	}
}

void Test_ServerExplorer()
{
	LoadBCP("data.bcp");
	Server server;
	server.loadSaveGame("Save_Games\\Interactive Tutorial.lvl");
	printf("Savegame loaded!\n");

	InitWindow();
	ImGuiImpl_Init();

	IRenderer *gfx = CreateRenderer();
	gfx->Init();
	ImGuiImpl_CreateFontsTexture(gfx);

	ServerDebugger serverdbg(&server);

	while (!g_windowQuit) {
		ImGuiImpl_NewFrame();
		//for (auto ole : server.idmap)
		//	ImGui::Text("%i: %s \"%s\"", ole.first, Tags::GAMEOBJCLASS_tagDict.getStringFromID(ole.second->blueprint->bpClass), ole.second->blueprint->name.c_str());
		serverdbg.draw();

		gfx->ClearFrame();
		gfx->BeginDrawing();
		gfx->InitImGuiDrawing();
		ImGuiImpl_Render(gfx);
		gfx->EndDrawing();

		HandleWindow();
	}
}

void Test_TrnTexDb() {
	LoadBCP("data.bcp");
	TerrainTextureDatabase tdb("Maps\\Map_Textures\\All_Textures.dat");
	for (const auto& grp : tdb.groups) {
		printf("%s\n", grp.second->name.c_str());
	}
	getchar();
}

void Test_Terrain() {
	LoadBCP("data.bcp");
	Terrain trn;
	trn.readFromFile("Maps\\128_4Peaks\\128_4Peaks.bcm");
	//trn.readFromFile("Maps\\rotfliptest\\rotfliptest.bcm");
	printf("yeah!\n");

	InitWindow();
	ImGuiImpl_Init();
	IRenderer *gfx = CreateRenderer();
	gfx->Init();
	SetRenderer(gfx);
	ImGuiImpl_CreateFontsTexture(gfx);

	Camera camera;
	camera.position = Vector3(256, 100, 256);
	camera.orientation = Vector3(0, 0, 0);
	DefaultTerrainRenderer trnrender(gfx, &trn, &camera);

	while (!g_windowQuit)
	{
		ImGuiImpl_NewFrame();
		ImGui::DragFloat3("Position", &camera.position.x);
		ImGui::DragFloat2("Orientation", &camera.orientation.x, 0.1f);
		ImGui::DragFloat3("Sun Vector", &trn.sunVector.x);
		camera.updateMatrix();

		gfx->ClearFrame();
		gfx->BeginDrawing();
		gfx->SetTransformMatrix(&camera.sceneMatrix);

		trnrender.render();

		gfx->InitImGuiDrawing();
		ImGuiImpl_Render(gfx);
		gfx->EndDrawing();

		HandleWindow();
		if (g_keyDown[SDL_SCANCODE_UP])
			camera.position.z += 0.5;
		if (g_keyDown[SDL_SCANCODE_DOWN])
			camera.position.z -= 0.5;
		if (g_keyDown[SDL_SCANCODE_RIGHT])
			camera.position.x += 0.5;
		if (g_keyDown[SDL_SCANCODE_LEFT])
			camera.position.x -= 0.5;
		if (g_keyDown[SDL_SCANCODE_0])
			;

	}

	//getchar();
}



std::string t29curdir = ".", feselfile;
std::vector<std::string> *t29curfiles = nullptr;

void T29_UpdateFiles()
{
	if (t29curfiles) delete t29curfiles;
	t29curfiles = ListFiles(t29curdir.c_str());
}

void T29_TreeDir(const char *s)
{
	std::vector<std::string>* dirs = ListDirectories(s);
	for (std::string &dir : *dirs)
	{
		bool b_open, b_click;
		b_open = ImGui::TreeNodeEx(dir.c_str(), ImGuiTreeNodeFlags_OpenOnArrow);
		b_click = ImGui::IsItemClicked();
		if(b_open)
		{
			std::string tb = s;
			tb.push_back('\\');
			tb += dir;
			T29_TreeDir(tb.c_str());
			ImGui::TreePop();
		}
		if (b_click)
		{
			t29curdir = s;
			t29curdir.push_back('\\');
			t29curdir += dir;
			T29_UpdateFiles();
		}
	}
	delete dirs;
}

void FileExplorer()
{
	ImGui::Begin("File explorer");
	ImGui::Columns(2, "DirsFilesCols", true);

	ImGui::Text("Directory");
	ImGui::NextColumn();
	ImGui::TextUnformatted(t29curdir.c_str());
	ImGui::Separator();

	ImGui::NextColumn();
	ImGui::BeginChild("DirCol");
	T29_TreeDir(".");
	ImGui::EndChild();

	ImGui::NextColumn();
	ImGui::BeginChild("FileCol");
	if (t29curfiles)
		for (const std::string &filename : *t29curfiles) {
			const char *ext = strrchr(filename.c_str(), '.');
			if (!StrCICompare(ext, ".mesh3")) {
				if (ImGui::Selectable(filename.c_str())) {
					feselfile = t29curdir + "\\" + filename;
					printf("Selected file: %s\n", feselfile.c_str());
				}
			}
			else {
				ImGui::TextDisabled("%s", filename.c_str());
			}
		}
	ImGui::EndChild();

	ImGui::Columns(1);
	ImGui::End();
}

void Test_Mesh()
{
	LoadBCP("data.bcp");
	Mesh mesh;
	//mesh.newload("Warrior Kings Game Set\\Buildings\\Houses\\house01\\DEFAULT.MESH3");
	//mesh.newload("Warrior Kings Game Set\\Characters\\Peasant\\Male\\Peasant1.MESH3");
	//mesh.newload("Warrior Kings Game Set\\Characters\\Peasant\\Female\\Mrs1.MESH3");
	//mesh.newload("Warrior Kings Game Set\\Characters\\Add On - Wood Elemental\\WOODELEMENTAL.MESH3");
	mesh.load("MINI_MAP_ARROW5.mesh3");

	InitWindow();
	ImGuiImpl_Init();
	IRenderer *gfx = CreateRenderer();
	gfx->Init();
	SetRenderer(gfx);
	ImGuiImpl_CreateFontsTexture(gfx);

	RBatch *batch = gfx->CreateBatch(400, 600);

	Matrix pers, cammat, persandcam;
	Vector3 campos(0, 0, -20), camdir(0.0, 0, 1);

	TextureCache texcache(gfx, "Warrior Kings Game Set\\Textures\\");
	std::map<std::string, texture> texmap;
	for (Material &mat : mesh.materials) {
		texmap[mat.textureFilename] = texcache.getTexture(mat.textureFilename.c_str());
	}

	while (!g_windowQuit) {
		ImGuiImpl_NewFrame();
		FileExplorer();
		if (!feselfile.empty()) {
			mesh = Mesh();
			mesh.load(feselfile.c_str());
			for (Material &mat : mesh.materials) {
				texmap[mat.textureFilename] = texcache.getTexture(mat.textureFilename.c_str());
			}
			feselfile.clear();
		}

		ImGui::DragFloat3("Position", &campos.x);
		ImGui::DragFloat3("Direction", &camdir.x);
		static int requvlist = 0;
		ImGui::InputInt("UV List", &requvlist);
		requvlist %= mesh.uvLists.size();

		gfx->ClearFrame();
		gfx->BeginDrawing();
		//gfx->InitRectDrawing();
		//gfx->DrawFrame(64, 64, 128, 128, -1);

		gfx->BeginMeshDrawing();
		gfx->BeginBatchDrawing();
		batch->begin();

		pers = Matrix::getLHPerspectiveMatrix(0.9f, (float)g_windowWidth / (float)g_windowHeight, 1.0f, 400.0f);
		cammat = Matrix::getLHLookAtViewMatrix(campos, campos + camdir, Vector3(0, 1, 0));
		persandcam = cammat * pers;
		gfx->SetTransformMatrix(&persandcam);

		std::string curtex;

		int plcnt = 0;
		for (PolygonList &polylist : mesh.polyLists) {
			for (size_t g = 0; g < polylist.groups.size(); g++) {
				PolyListGroup &group = polylist.groups[g];
				if (curtex != mesh.materials[g].textureFilename) {
					batch->flush();
					curtex = mesh.materials[g].textureFilename;
					gfx->SetTexture(0, texmap[curtex]);
				}
				for (auto &tuple : group.tupleIndex) {
					batchVertex *bver; uint16_t *bind; uint32_t bstart;
					batch->next(3, 3, &bver, &bind, &bstart);
					for (int i = 0; i < 3; i++) {
						uint16_t triver = tuple[i];
						IndexTuple &tind = mesh.groupIndices[g][triver];
						bver[i].x = mesh.vertices[3 * tind.vertex] + 6 * plcnt;
						bver[i].y = mesh.vertices[3 * tind.vertex + 1];
						bver[i].z = mesh.vertices[3 * tind.vertex + 2];
						bver[i].u = mesh.uvLists[requvlist][2 * tind.uv];
						bver[i].v = mesh.uvLists[requvlist][2 * tind.uv + 1];
						bver[i].color = -1;
						bind[i] = bstart + i;
					}
				}
			}
			plcnt++;
		}

		batch->flush();
		batch->end();

		gfx->InitImGuiDrawing();
		ImGuiImpl_Render(gfx);
		gfx->EndDrawing();
		HandleWindow();
	}

	//getchar();
}

void Test_Network()
{
	LoadBCP("data.bcp");
	Server server; Client client(true);
	NetLocalBuffer queue1, queue2;
	NetLocalLink srv2cli(&queue1,&queue2), cli2srv(&queue2,&queue1);
	server.addClient(&srv2cli);
	client.serverLink = &cli2srv;

	InitWindow();
	ImGuiImpl_Init();
	IRenderer *gfx = CreateRenderer();
	gfx->Init();
	SetRenderer(gfx);
	ImGuiImpl_CreateFontsTexture(gfx);

	std::vector<std::string> *gsl = ListFiles("Save_Games");

	bool savchosen = false;
	int savselected = 0;
	while (!savchosen) {
		ImGuiImpl_NewFrame();
		ImGui::Begin("Select sav");
		if (ImGui::BeginListBox("##SaveBox")) {
			for (int i = 0; i < (int)gsl->size(); i++)
				if (ImGui::Selectable(gsl->at(i).c_str(), savselected == i))
					savselected = i;
			ImGui::EndListBox();
		}
		if (ImGui::Button("Let me in"))
			savchosen = true;
		ImGui::End();

		gfx->ClearFrame();
		gfx->BeginDrawing();
		gfx->InitImGuiDrawing();
		ImGuiImpl_Render(gfx);
		gfx->EndDrawing();
		HandleWindow();
	}

	std::string savfile = std::string("Save_Games\\") + gsl->at(savselected);
	std::mutex srvMutex;
	std::thread srvThread([&server, savfile, &srvMutex]() {
		srvMutex.lock();
		server.loadSaveGame(savfile.c_str());
		srvMutex.unlock();
		while (!g_windowQuit) {
			srvMutex.lock();
			server.tick();
			srvMutex.unlock();
			_sleep(1000/60);
		}
	});

	ClientInterface cliui(&client, gfx);
	client.attachInterface(&cliui);
	ServerDebugger srvdbg(&server);
	bool onServerUi = false;

	while (!g_windowQuit) {
		client.tick();
		if (g_keyPressed[SDL_SCANCODE_F1]) {
			onServerUi = !onServerUi;
			if (onServerUi)
				srvMutex.lock();
			else
				srvMutex.unlock();
		}
		if (onServerUi) {
			server.tick();
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
	}
	if (onServerUi)
		srvMutex.unlock();

	srvThread.join();
}

void Test_EnetServer() {
	LoadBCP("data.bcp");
	Server server;

	enet_initialize();
	ENetAddress address;
	ENetHost *eserver;
	address.host = ENET_HOST_ANY;
	address.port = 1234;
	eserver = enet_host_create(&address, 16, 2, 0, 0);

	InitWindow();
	ImGuiImpl_Init();
	IRenderer *gfx = CreateRenderer();
	gfx->Init();
	SetRenderer(gfx);
	ImGuiImpl_CreateFontsTexture(gfx);
	ServerDebugger serverdbg(&server);

	while (!g_windowQuit) {
		server.tick();

		ENetEvent event;
		while (enet_host_service(eserver, &event, 0) > 0) {
			switch (event.type) {
			case ENET_EVENT_TYPE_CONNECT: {
				printf("connection\n");
				NetEnetLink *link = new NetEnetLink(eserver, event.peer);
				server.addClient(link);
				event.peer->data = link;
				break;
			}
			case ENET_EVENT_TYPE_DISCONNECT: {
				printf("disconnected\n");
				server.removeClient((NetEnetLink*)event.peer->data);
				break;
			}
			case ENET_EVENT_TYPE_RECEIVE: {
				printf("message: %zu\n", event.packet->dataLength);
				NetEnetLink *link = (NetEnetLink*)event.peer->data;
				link->packets.push(event.packet);
				break;
			}
			}
		}

		ImGuiImpl_NewFrame();
		serverdbg.draw();

		gfx->ClearFrame();
		gfx->BeginDrawing();
		gfx->InitImGuiDrawing();
		ImGuiImpl_Render(gfx);
		gfx->EndDrawing();
		HandleWindow();

	}
	enet_host_destroy(eserver);
	enet_deinitialize();
}

void Test_EnetClient() {
	LoadBCP("data.bcp");
	Client client;

	enet_initialize();
	ENetHost *eclient;
	eclient = enet_host_create(nullptr, 1, 2, 0, 0);

	ENetAddress address;
	enet_address_set_host(&address, "localhost");
	address.port = 1234;

	ENetPeer *pserver = enet_host_connect(eclient, &address, 2, 0);

	InitWindow();
	ImGuiImpl_Init();
	IRenderer *gfx = CreateRenderer();
	gfx->Init();
	SetRenderer(gfx);
	ImGuiImpl_CreateFontsTexture(gfx);

	//DefaultSceneRenderer *scenerender = new DefaultSceneRenderer(gfx, &client);
	//DefaultSceneRenderer scenerender(gfx, &client);

	//ClientDebugger clidbg(&client);
	ClientInterface cliui = ClientInterface(&client, gfx);
	client.attachInterface(&cliui);

	while (!g_windowQuit) {
		client.tick();

		ENetEvent event;
		while (enet_host_service(eclient, &event, 0) > 0) {
			switch (event.type) {
			case ENET_EVENT_TYPE_CONNECT: {
				printf("connection\n");
				NetEnetLink *link = new NetEnetLink(eclient, event.peer);
				client.serverLink = link;
				event.peer->data = link;
				break;
			}
			case ENET_EVENT_TYPE_DISCONNECT: {
				printf("disconnected\n");
				client.serverLink = nullptr;
				break;
			}
			case ENET_EVENT_TYPE_RECEIVE: {
				printf("message: %zu\n", event.packet->dataLength);
				NetEnetLink *link = (NetEnetLink*)event.peer->data;
				assert(link == client.serverLink);
				link->packets.push(event.packet);
				break;
			}
			}
		}

		//ImGuiImpl_NewFrame();
		//clidbg.draw();

		//gfx->BeginDrawing();
		//scenerender.render();
		//gfx->InitImGuiDrawing();
		//ImGuiImpl_Render(gfx);
		//gfx->EndDrawing();
		//HandleWindow();

		cliui.iter();

	}

	enet_host_destroy(eclient);
	enet_deinitialize();
}

void Test_Anim()
{
	LoadBCP("data.bcp");
	Anim anim;
	anim.load("Warrior Kings Game Set\\Characters\\Peasant\\Male\\MOVE1.ANIM3");
	getchar();
}

void Test_Scene()
{
	LoadBCP("data.bcp");

	InitWindow();
	ImGuiImpl_Init();
	IRenderer *gfx = CreateRenderer();
	gfx->Init();
	SetRenderer(gfx);
	ImGuiImpl_CreateFontsTexture(gfx);

	Camera camera;
	camera.position = Vector3(0, 0, -6);
	camera.orientation = Vector3(0, 0, 0);

	ModelCache modcache;
	Model* models[2];
	models[0] = modcache.getModel("Warrior Kings Game Set\\Characters\\Peasant\\Male\\Dance1.ANIM3");
	models[1] = modcache.getModel("Warrior Kings Game Set\\Characters\\Add On - Arch Druid\\Summon.ANIM3");

	const int numents = 1024;

	DynArray<SceneEntity> scents(numents);
	for (SceneEntity &se : scents) {
		se.model = models[rand() & 1];
		se.color = rand() & 7;
	}
	float scalestart[numents], xpos[numents], zpos[numents];
	for (int i = 0; i < numents; i++) {
		scalestart[i] = (rand() % 628) / 100.0f;
		xpos[i] = (float)(rand() % 40 - 20);
		zpos[i] = (float)(rand() % 40 - 20);
	}

	Scene scene(gfx, &modcache);

	SceneRenderer *sceneRenderer = new DefaultSceneRenderer(gfx, &scene, &camera);

	int fps = 0, nextfps = 0;
	uint32_t lastfpscheck = SDL_GetTicks();
	while (!g_windowQuit)
	{
		uint32_t ticks = SDL_GetTicks();
		if (ticks - lastfpscheck >= 1000) {
			fps = nextfps;
			nextfps = 0;
			lastfpscheck = ticks;
		}
		nextfps++;
		ImGuiImpl_NewFrame();
		ImGui::Text("FPS: %i", fps);
		ImGui::DragFloat3("Position", &camera.position.x);
		ImGui::DragFloat2("Orientation", &camera.orientation.x, 0.1f);
		camera.updateMatrix();

		for (int i = 0; i < numents; i++) {
			SceneEntity &se = scents[i];
			float s = (sinf(ticks / 1000.0f + scalestart[i]) + 1.0f) * 0.5f;
			se.transform = Matrix::getScaleMatrix(Vector3(s, s, s));
			se.transform._41 = xpos[i];
			se.transform._43 = zpos[i];
			se.animTime = ticks;
		}

		gfx->ClearFrame();
		gfx->BeginDrawing();
		gfx->SetTransformMatrix(&camera.sceneMatrix);
		for(SceneEntity &se : scents)
			scene.add(&se);
		sceneRenderer->render();
		scene.clear();

		gfx->InitImGuiDrawing();
		ImGuiImpl_Render(gfx);
		gfx->EndDrawing();

		HandleWindow();
		if (g_keyDown[SDL_SCANCODE_UP])
			camera.position.z += 0.5;
		if (g_keyDown[SDL_SCANCODE_DOWN])
			camera.position.z -= 0.5;
		if (g_keyDown[SDL_SCANCODE_RIGHT])
			camera.position.x += 0.5;
		if (g_keyDown[SDL_SCANCODE_LEFT])
			camera.position.x -= 0.5;
		if (g_keyDown[SDL_SCANCODE_0])
			;

	}
}

void Test_SoundPlayer() {
	LoadBCP("data.bcp");

	SoundPlayer* player = SoundPlayer::getSoundPlayer();
	std::string music = R"(Warrior Kings Game Set\Sounds\Map Briefing\Map Music 1.mp2)";
	for (int i = 1; i <= 4; i++) {
		music[53] = '0' + i;
		player->playMusic(music);
		Sleep(4000);
	}
	player->playSound(R"(Warrior Kings Game Set\Sounds\Gongs\gong_3.wav)");
	Sleep(500);
	player->playSound(R"(Warrior Kings Game Set\Sounds\Cog\cog_1.wav)");
	player->playSound(R"(Warrior Kings Game Set\Sounds\Death\death_1_1.wav)");
	printf("Press enter to stop\n");
	getchar();
}

void Test_Pathfinding()
{
	using namespace Pathfinding;
	static constexpr int WIDTH = 1000, HEIGHT = 1000;
	//static constexpr char map[HEIGHT][WIDTH] = {
	//	{1,1,1,1,1,1,1,1,1,1},
	//	{1,1,0,1,1,1,1,1,1,1},
	//	{1,0,0,1,1,1,1,1,1,1},
	//	{1,1,0,0,0,0,0,1,1,1},
	//	{1,1,1,1,0,1,1,1,1,1},
	//	{1,0,0,0,0,1,1,1,1,1},
	//	{1,1,0,1,1,1,1,1,1,1},
	//	{1,1,0,1,1,1,1,1,1,1},
	//	{1,1,0,1,1,1,1,1,1,1},
	//	{1,1,1,1,1,1,1,1,1,1}
	//};
	auto pred = [](PFPos pos) -> bool {
		if (pos.x >= 0 && pos.x < WIDTH && pos.z >= 0 && pos.z < HEIGHT)
			return false; //return map[pos.z][pos.x] != 0;
		return true;
	};
	auto vec = DoPathfinding({ 2,2 }, { 800,900 }, pred, ManhattanDiagHeuristic);
	printf("Results:\n");
	for (const PFPos& pfp : vec)
		printf("(%i, %i)\n", pfp.x, pfp.z);
	getchar();
}

void Test_ParticleSystem()
{
	LoadBCP("data.bcp");
	PSCache pscache{ "Warrior Kings Game Set\\Particle_Systems" };
	ParticleSystem* ps = pscache.getPS("HighPriestessSpell");

	std::vector<std::string> psNames;
	ListFiles("Warrior Kings Game Set\\Particle_Systems", &psNames);

	InitWindow();
	ImGuiImpl_Init();
	IRenderer* gfx = CreateRenderer();
	gfx->Init();
	SetRenderer(gfx);
	ImGuiImpl_CreateFontsTexture(gfx);

	Camera camera;
	camera.position = Vector3(0, 0, -6);
	camera.orientation = Vector3(0, 0, 0);

	ParticleContainer particleContainer;
	DefaultParticleRenderer particleRenderer(gfx, &particleContainer);

	uint32_t prevTimeMs = SDL_GetTicks();
	uint32_t nextTimeMs = prevTimeMs;

	int fps = 0, nextfps = 0;
	uint32_t lastfpscheck = SDL_GetTicks();
	while (!g_windowQuit)
	{
		uint32_t ticks = SDL_GetTicks();
		if (ticks - lastfpscheck >= 1000) {
			fps = nextfps;
			nextfps = 0;
			lastfpscheck = ticks;
		}
		nextfps++;
		ImGuiImpl_NewFrame();
		ImGui::Text("FPS: %i", fps);
		ImGui::DragFloat3("Position", &camera.position.x);
		ImGui::DragFloat2("Orientation", &camera.orientation.x, 0.1f);
		ImGui::Text("Num particles: %zu", particleContainer.particles.size());
		if (ImGui::BeginListBox("PSystem")) {
			for (auto& name : psNames) {
				if (ImGui::Selectable(name.c_str())) {
					ps = pscache.getPS(name.substr(0, name.find('.')).c_str());
					particleContainer.clearParticles();
				}
			}
			ImGui::EndListBox();
		}
		camera.updateMatrix();

		//
		prevTimeMs = nextTimeMs;
		nextTimeMs = ticks;
		float prevTimeFlt = (float)prevTimeMs / 1000.0f;
		float nextTimeFlt = (float)nextTimeMs / 1000.0f;
		particleContainer.generate(ps, { 0,0,0 }, 0, prevTimeFlt, nextTimeFlt);
		particleContainer.update(nextTimeFlt);

		gfx->ClearFrame();
		gfx->BeginDrawing();
		//gfx->InitRectDrawing();
		//gfx->DrawRect(10, 10, 20, 20);
		gfx->BeginParticles();
		gfx->BeginBatchDrawing();
		Matrix matId = Matrix::getIdentity();
		gfx->SetTransformMatrix(&matId);
		particleRenderer.render(prevTimeFlt, nextTimeFlt, camera);

		gfx->InitImGuiDrawing();
		ImGuiImpl_Render(gfx);
		gfx->EndDrawing();

		HandleWindow();
		if (g_keyDown[SDL_SCANCODE_UP])
			camera.position.z += 0.5;
		if (g_keyDown[SDL_SCANCODE_DOWN])
			camera.position.z -= 0.5;
		if (g_keyDown[SDL_SCANCODE_RIGHT])
			camera.position.x += 0.5;
		if (g_keyDown[SDL_SCANCODE_LEFT])
			camera.position.x -= 0.5;
	}

}

void Test_PFRayTraversal() {
	using namespace Pathfinding;
	static constexpr int WIDTH = 10, HEIGHT = 10;
	static constexpr std::array<char, WIDTH*HEIGHT> map = {
		1,1,1,1,1,1,1,1,1,1,
		1,0,0,0,0,0,0,0,0,1,
		1,0,0,0,0,0,0,0,0,1,
		1,0,0,0,0,0,0,0,0,1,
		1,0,0,0,0,0,0,0,0,1,
		1,0,0,0,0,0,0,0,0,1,
		1,0,0,0,0,0,0,0,0,1,
		1,0,0,0,0,0,0,0,0,1,
		1,0,0,0,0,0,0,0,0,1,
		1,1,1,1,1,1,1,1,1,1
	};
	auto pred = [](PFPos pos) -> bool {
		if (pos.x >= 0 && pos.x < WIDTH && pos.z >= 0 && pos.z < HEIGHT)
			return map[pos.z * WIDTH + pos.x] != 0;
		return true;
	};
	
	//float start[2] = { 2.0f, 2.0f }, end[2] = { 2.0f, 3.0f };
	float start[2] = { 7.5f, 1.5f }, end[2] = { 3.5f, 3.5f };

	//auto res = SegmentTraversal(start[0], start[1], end[0], end[1], pred);
	//printf("Result: %s, (%i, %i)\n", res ? "true" : "false", res ? res->x : 0, res ? res->z : 0);

	for (int i = 0; i < 2; ++i) {
		auto annotatedMap = map;
		annotatedMap[(int)start[1] * WIDTH + (int)start[0]] = 2;
		const PFPos endTile = { (int)end[0], (int)end[1] };
		PFPos curTile = { (int)start[0], (int)start[1] };
		TileWalker walker(start[0], start[1], end[0] - start[0], end[1] - start[1]);
		while (curTile != endTile) {
			curTile = walker.next();
			annotatedMap[(int)curTile.z * WIDTH + (int)curTile.x] = 3;
		}

		printf("===== Start (%f,%f) -> End (%f,%f)\n", start[0], start[1], end[0], end[1]);
		for (int y = 0; y < HEIGHT; ++y) {
			for (int x = 0; x < WIDTH; ++x) {
				printf("%i", (int)annotatedMap[y * WIDTH + x]);
			}
			printf("\n");
		}

		std::swap(start, end);
	}
	getchar();
}

// Mounts the file system, then prints the mods and, for every file name read from stdin
// (until an empty line), the archive/folder it resolves to and its contents.
// Driven by tests/test_vfs.py.
void Test_VFS()
{
	LoadBCP("data.bcp");
	printf("ORDER %s\n", IsCustomModLoadOrder() ? "custom" : "original");
	for (const ModInfo& mod : GetMods())
		printf("MOD %s|%s|%s|%d\n", mod.id.c_str(), mod.name.c_str(), mod.kind.c_str(), mod.enabled ? 1 : 0);
	char line[512];
	while (fgets(line, sizeof(line), stdin)) {
		line[strcspn(line, "\r\n")] = 0;
		if (!line[0])
			break;
		std::string content;
		if (FileExists(line)) {
			char* data; int size;
			LoadFile(line, &data, &size);
			content.assign(data, size);
			free(data);
		}
		printf("RESOLVE %s|%s|%s\n", line, DescribeFileSource(line).c_str(), content.c_str());
	}
	fflush(stdout);
}

// Building placement (needs the game data): on another building it is refused, in the open allowed.
// Prints "STAMPDOWN <where> <status>" (0 = valid, see StampdownPlan::Status).
void Test_Stampdown()
{
	LoadBCP("data.bcp");
	Server server;
	server.loadSaveGame("Save_Games\\2 Player 4 Peaks.lvl");
	const GameObjBlueprint* windmill = server.gameSet->findBlueprint(Tags::GAMEOBJCLASS_BUILDING, "Windmill");
	if (!windmill) {
		printf("STAMPDOWN no Windmill blueprint\n");
		return;
	}
	for (const auto& [type, players] : server.getLevel()->children) {
		for (CommonGameObject* player : players) {
			if (player->blueprint->bpClass != Tags::GAMEOBJCLASS_PLAYER)
				continue;
			// the player's buildings, at any depth (towns, cities)
			std::vector<CommonGameObject*> buildings;
			auto collect = [&](CommonGameObject* obj, auto& self) -> void {
				for (const auto& [ctype, children] : obj->children)
					for (CommonGameObject* child : children) {
						if (child->blueprint->bpClass == Tags::GAMEOBJCLASS_BUILDING && child->blueprint->footprint)
							buildings.push_back(child);
						self(child, self);
					}
			};
			collect(player, collect);
			for (CommonGameObject* b : buildings) {
				const auto plan = StampdownPlan::getStampdownPlan(&server, player, windmill, b->position, Vector3(0, 0, 0));
				printf("STAMPDOWN on %s %d\n", b->blueprint->name.c_str(), (int)plan.status);
			}
			if (!buildings.empty()) {
				const Vector3 open = buildings[0]->position + Vector3(60, 0, 60);
				const auto plan = StampdownPlan::getStampdownPlan(&server, player, windmill, open, Vector3(0, 0, 0));
				printf("STAMPDOWN open %d\n", (int)plan.status);
				// a field on a field (fields are passable, but may not share tiles)
				if (const GameObjBlueprint* field = server.gameSet->findBlueprint(Tags::GAMEOBJCLASS_BUILDING, "Standard Farm")) {
					const auto first = StampdownPlan::getStampdownPlan(&server, player, field, open, Vector3(0, 0, 0));
					ServerGameObject* placed = server.spawnObject(field, player->dyncast<ServerGameObject>(), open, Vector3(0, 0, 0));
					const auto second = StampdownPlan::getStampdownPlan(&server, player, field, open + Vector3(5, 0, 5), Vector3(0, 0, 0));
					printf("STAMPDOWN field on open ground %d, on a field %d\n", (int)first.status, (int)second.status);
					server.deleteObject(placed);
				}
			}
		}
	}
	fflush(stdout);
}

// Formations (needs the game data): the player's characters form up and the formation moves; the
// members should walk (with an animation) into and along with it. Prints per second
// "FORMATION t=<s> formation (x,z) member <i> (x,z) moving <0|1> anim <name>".
void Test_Formation()
{
	LoadBCP("data.bcp");
	Server server;
	server.loadSaveGame("Save_Games\\2 Player 4 Peaks.lvl");
	ServerGameObject* player = server.findObject(1027);
	std::vector<ServerGameObject*> members;
	auto collect = [&](CommonGameObject* obj, auto& self) -> void {
		for (const auto& [type, children] : obj->children)
			for (CommonGameObject* child : children) {
				if (child->blueprint->bpClass == Tags::GAMEOBJCLASS_CHARACTER)
					members.push_back(child->dyncast<ServerGameObject>());
				else
					self(child, self);
			}
	};
	if (player)
		collect(player, collect);
	const int moveCommand = server.gameSet->commands.names.getIndex("Move");
	if (members.size() < 2 || moveCommand < 0 || server.gameSet->objBlueprints[Tags::GAMEOBJCLASS_FORMATION].size() == 0) {
		printf("FORMATION not set up: %zu members\n", members.size());
		return;
	}
	// as NETSRVMSG_CREATE_NEW_FORMATION
	ServerGameObject* formation = server.spawnObject(&server.gameSet->objBlueprints[Tags::GAMEOBJCLASS_FORMATION][0], player, {}, {});
	Vector3 sum;
	for (ServerGameObject* m : members) {
		sum += m->position;
		m->setParent(formation);
	}
	formation->setPosition(sum / (float)members.size());
	formation->sendEvent(Tags::PDEVENT_ON_SPAWN);
	server.timeManager.unpause();
	const uint64_t start = SDL_GetTicks();
	bool moved = false;
	int lastPrint = -1;
	while (SDL_GetTicks() - start < 24000) {
		server.tick();
		const float t = (SDL_GetTicks() - start) / 1000.0f;
		if (!moved && t > 4.0f) {
			moved = true;
			// as a right-drag: then facing 20 m east of the destination (orientation pi/2)
			const Vector3 destination = formation->position + Vector3(30, 0, 30);
			server.gameSet->commands[moveCommand].execute(formation, nullptr, Tags::ORDERASSIGNMODE_FORGET_EVERYTHING_ELSE, destination, destination + Vector3(20, 0, 0));
		}
		if ((int)t != lastPrint) {
			lastPrint = (int)t;
			printf("FORMATION t=%d formation (%.1f,%.1f) facing %.2f", lastPrint, formation->position.x, formation->position.z, formation->orientation.y);
			for (size_t i = 0; i < members.size(); i++)
				printf(" | %zu (%.1f,%.1f) %d %s from %.1f", i, members[i]->position.x, members[i]->position.z, (int)members[i]->movement.isMoving(),
					members[i]->animationIndex >= 0 ? server.gameSet->animations.names.getString(members[i]->animationIndex).c_str() : "-",
					members[i]->animStartTime);
			printf("\n");
		}
		SDL_Delay(16);
	}
	fflush(stdout);
}

// Siege (needs the game data): a Mangonel of player 1027 on 4 Peaks, 120 m from the other
// player's Manor, told to attack it ("Fire Trebuchet", its DEFAULT_ATTACK_COMMAND). Prints
// "SIEGE t=<s> distance=<m> order=<order> task=<task> proximity=<0|1> manor SI=<n>" every game
// second for a minute, and "SIEGE fired=<0|1>" at the end (the Manor's structural integrity fell).
void Test_Siege()
{
	LoadBCP("data.bcp");
	Server server;
	server.loadSaveGame("Save_Games\\2 Player 4 Peaks.lvl");
	ServerGameObject* own = server.findObject(1027);
	ServerGameObject* enemy = server.findObject(1030);
	ServerGameObject* manor = nullptr;
	auto findManor = [&](CommonGameObject* obj, auto& self) -> void {
		for (const auto& [type, children] : obj->children)
			for (CommonGameObject* child : children) {
				if (!manor && child->blueprint->bpClass == Tags::GAMEOBJCLASS_BUILDING && child->blueprint->name.find("Manor") != std::string::npos)
					manor = child->dyncast<ServerGameObject>();
				self(child, self);
			}
	};
	if (enemy)
		findManor(enemy, findManor);
	const char* name = getenv("WKB_SIEGE_UNIT") ? getenv("WKB_SIEGE_UNIT") : "Mangonel";
	const GameObjBlueprint* bp = server.gameSet->findBlueprint(Tags::GAMEOBJCLASS_CHARACTER, name);
	// WKB_SIEGE_COMMAND: the command given (default "Fire Trebuchet"; "Attack" is the right-click's)
	const int fire = server.gameSet->commands.names.getIndex(getenv("WKB_SIEGE_COMMAND") ? getenv("WKB_SIEGE_COMMAND") : "Fire Trebuchet");
	if (!own || !manor || !bp || fire < 0) {
		printf("SIEGE not set up: player %p manor %p unit %p command %d\n", (void*)own, (void*)manor, (void*)bp, fire);
		return;
	}
	// WKB_SIEGE_DISTANCE: how far from the Manor it starts (default 120 m)
	const float distance = getenv("WKB_SIEGE_DISTANCE") ? (float)atof(getenv("WKB_SIEGE_DISTANCE")) : 120.0f;
	Vector3 at = manor->position + Vector3(distance, 0, 0);
	if (at.x > server.terrain->getPlayableArea().first - 10.0f)
		at = manor->position - Vector3(distance, 0, 0);
	at.y = server.terrain->getHeight(at.x, at.z);
	ServerGameObject* unit = server.spawnObject(bp, own, at, Vector3(0, 0, 0));
	SrvGORef ref = unit;
	printf("SIEGE unit %u (%s) at (%.0f, %.0f), manor %u at (%.0f, %.0f)\n", unit->id, name, at.x, at.z, manor->id, manor->position.x, manor->position.z);
	{
		SrvScriptContext ctx(&server, unit);
		auto _ = ctx.change(ctx.target, manor);
		for (const char* eq : { "I'm out of missiles", "I Am Not A Manognel Of Some Description", "Out Of Trebuchet Range", "Sight Range Modified By Missile Range Multiplier in Metres", "Target Too Close" }) {
			const int i = server.gameSet->equations.names.getIndex(eq);
			printf("SIEGE equation \"%s\" = %.2f\n", eq, i >= 0 ? server.gameSet->equations[i]->eval(&ctx) : -999.0f);
		}
		// what a right-click on the Manor would offer: each command's cursor conditions, in the
		// order the client looks at them (ClientInterface: the first whose cursor shows wins)
		for (Command* cmd : bp->offeredCommands) {
			std::string conds;
			for (auto& [cond, cursor] : cmd->cursorAvailable)
				conds += " [" + cond->hint + "=" + std::to_string((int)cond->test->eval(&ctx)) + (cursor ? "" : " no cursor") + "]";
			bool all = true;
			for (int eq : cmd->cursorConditions)
				all = all && server.gameSet->equations[eq]->eval(&ctx) > 0.0f;
			printf("SIEGE offers \"%s\":%s%s default cursor %d\n", server.gameSet->commands.getString(cmd).c_str(), conds.c_str(), all ? "" : " (cursor conditions fail)", cmd->cursor ? 1 : 0);
		}
	}
	const int siItem = server.gameSet->items.names.getIndex("Structural Integrity");
	const float siStart = manor->getItem(siItem);
	server.timeManager.unpause();
	server.timeManager.stepMs = 20;
	server.gameSet->commands[fire].execute(unit, manor, Tags::ORDERASSIGNMODE_FORGET_EVERYTHING_ELSE);
	int lastPrint = -1;
	for (int step = 0; step * 20 < 60000 && ref; step++) {
		server.timeManager.pendingMs = server.timeManager.stepMs;
		if (server.timeManager.step())
			server.simulate();
		const int t = (step * 20) / 1000;
		if (t == lastPrint)
			continue;
		lastPrint = t;
		std::string order = "-", task = "-";
		int proximity = -1;
		if (Order* o = unit->orderConfig.getCurrentOrder()) {
			order = server.gameSet->orders.getString(o->blueprint->bpid);
			if (Task* tk = o->getCurrentTask()) {
				task = server.gameSet->tasks.getString(tk->blueprint->bpid);
				proximity = tk->proximitySatisfied ? 1 : 0;
			}
		}
		static const int chargeItem = server.gameSet->items.names.getIndex("Charge Bar");
		printf("SIEGE t=%d distance=%.1f order=%s task=%s proximity=%d sight=%.2f charge=%.0f manor SI=%.0f\n", t, (unit->position - manor->position).len2xz(),
			order.c_str(), task.c_str(), proximity, unit->getItem(Tags::PDITEM_ACTUAL_SIGHT_RANGE), unit->getItem(chargeItem), manor->getItem(siItem));
	}
	printf("SIEGE fired=%d (SI %.0f -> %.0f)\n", manor->getItem(siItem) < siStart ? 1 : 0, siStart, manor->getItem(siItem));
	fflush(stdout);
}

// Training (needs the game data): six Men at Arms ordered to train at one Training Dummy, a
// second dummy 40 m away. The original lets 4 train at a dummy (Training.cpp's "CONSTANT 4"), each
// at an attack point of its own (AT_1..AT_4 on the dummy's model, 2 on each side); the others
// go to the next dummy with a free slot (the Train command's POST_ORDER_SEQUENCE). Prints
// "TRAIN t=<s> unit <id>: <order>/<task> -> dummy <id> at (<x>, <z>) in the dummy's frame,
// facing <deg>" every 5 s, and "TRAIN worked at" counts.
void Test_Train()
{
	LoadBCP("data.bcp");
	Server server;
	server.loadSaveGame("Save_Games\\2 Player 4 Peaks.lvl");
	ServerGameObject* own = server.findObject(1027);
	ServerGameObject* manor = nullptr;
	auto findManor = [&](CommonGameObject* obj, auto& self) -> void {
		for (const auto& [type, children] : obj->children)
			for (CommonGameObject* child : children) {
				if (!manor && child->blueprint->name == "Manor")
					manor = child->dyncast<ServerGameObject>();
				self(child, self);
			}
	};
	if (own)
		findManor(own, findManor);
	const GameObjBlueprint* dummyType = server.gameSet->findBlueprint(Tags::GAMEOBJCLASS_BUILDING, "Training Dummy");
	const GameObjBlueprint* unitType = server.gameSet->findBlueprint(Tags::GAMEOBJCLASS_CHARACTER, "Man At Arms");
	const int train = server.gameSet->commands.names.getIndex("Train");
	const int worked = server.gameSet->items.names.getIndex("I'm Being Worked at");
	if (!own || !manor || !dummyType || !unitType || train < 0 || worked < 0) {
		printf("TRAIN not set up\n");
		return;
	}
	g_settings["simSeed"] = 1;
	server.startLevel();
	// two free spots for dummies, 40 m apart, near the Manor
	std::vector<ServerGameObject*> dummies;
	for (int i = 0; i < 200 && dummies.size() < 2; i++) {
		const Vector3 at = manor->position + Vector3(-40.0f + 5.0f * (i % 16), 0, 40.0f + 5.0f * (i / 16));
		if (!dummies.empty() && (at - dummies[0]->position).len2xz() < 40.0f)
			continue;
		if (StampdownPlan::getStampdownPlan(&server, own, dummyType, at, Vector3(0, 0, 0)).status != StampdownPlan::Status::Valid)
			continue;
		ServerGameObject* d = server.spawnObject(dummyType, own, at, Vector3(0, 0, 0));
		d->sendEvent(Tags::PDEVENT_ON_STAMPDOWN);
		dummies.push_back(d);
	}
	if (dummies.size() < 2) {
		printf("TRAIN no room for the dummies\n");
		return;
	}
	ServerGameObject* dummy = dummies[0];
	if (Model* model = dummy->getModel())
		for (size_t i = 0; i < model->getNumAPs(); i++) {
			const auto& ap = model->getAPInfo(i);
			const Vector3 p = ap.staticState.position.transform(dummy->getWorldMatrix());
			printf("TRAIN dummy %u point %s at (%.2f, %.2f) from its centre\n", dummy->id, ap.tag.c_str(), p.x - dummy->position.x, p.z - dummy->position.z);
		}
	printf("TRAIN dummy %u at (%.1f, %.1f), dummy %u at (%.1f, %.1f), %.0f m apart\n", dummies[0]->id, dummies[0]->position.x, dummies[0]->position.z,
		dummies[1]->id, dummies[1]->position.x, dummies[1]->position.z, (dummies[1]->position - dummies[0]->position).len2xz());
	std::vector<SrvGORef> units;
	for (int i = 0; i < 6; i++) {
		Vector3 at = dummy->position + Vector3(-6.0f + 2.5f * i, 0, -15.0f);
		at.y = server.terrain->getHeight(at.x, at.z);
		units.emplace_back(server.spawnObject(unitType, own, at, Vector3(0, 0, 0)));
	}
	server.timeManager.unpause();
	server.timeManager.stepMs = 20;
	// one after the other, as for a selection of single units
	for (auto& u : units)
		server.gameSet->commands[train].execute(u.get(), dummy, Tags::ORDERASSIGNMODE_FORGET_EVERYTHING_ELSE);
	for (int step = 0; step * 20 <= 40000; step++) {
		server.timeManager.pendingMs = server.timeManager.stepMs;
		if (server.timeManager.step())
			server.simulate();
		if (step % 250)
			continue;
		const int t = step * 20 / 1000;
		printf("TRAIN t=%d worked at: dummy %u %d, dummy %u %d\n", t, dummies[0]->id, (int)dummies[0]->getItem(worked), dummies[1]->id, (int)dummies[1]->getItem(worked));
		for (auto& ref : units) {
			ServerGameObject* u = ref.get();
			if (!u)
				continue;
			std::string order = "-", task = "-";
			ServerGameObject* at = nullptr;
			if (Order* o = u->orderConfig.getCurrentOrder()) {
				order = server.gameSet->orders.getString(o->blueprint->bpid);
				if (Task* tk = o->getCurrentTask()) {
					task = server.gameSet->tasks.getString(tk->blueprint->bpid);
					at = tk->target.get();
				}
			}
			ServerGameObject* ref2 = at && at->blueprint == dummyType ? at : dummy;
			// in the dummy's frame (it stands unrotated here)
			const Vector3 rel = u->position - ref2->position;
			Vector3 dir = (ref2->position - u->position).normal2xz();
			const float faceErr = std::abs(std::remainder(u->orientation.y - std::atan2(dir.x, -dir.z), 6.2831853f)) * 57.29578f;
			printf("TRAIN t=%d unit %u: %s / %s -> %s%u at (%.2f, %.2f), %.2f m, facing the dummy within %.0f deg, anim %s\n", t, u->id, order.c_str(), task.c_str(),
				at ? "" : "(none) ", ref2->id, rel.x, rel.z, rel.len2xz(), faceErr,
				u->animationIndex > 0 ? server.gameSet->animations.names.getString(u->animationIndex).c_str() : "-");
		}
	}
	fflush(stdout);
}

// Guarding (needs the game data): a peasant guards another one, which then walks away; the
// guard should follow at its offset, not walk into it. Prints "GUARD t=<s> distance=<m>".
void Test_Guard()
{
	LoadBCP("data.bcp");
	Server server;
	server.loadSaveGame("Save_Games\\2 Player 4 Peaks.lvl");
	std::vector<ServerGameObject*> characters;
	auto collect = [&](CommonGameObject* obj, auto& self) -> void {
		for (const auto& [type, children] : obj->children)
			for (CommonGameObject* child : children) {
				if (child->blueprint->bpClass == Tags::GAMEOBJCLASS_CHARACTER)
					characters.push_back(child->dyncast<ServerGameObject>());
				self(child, self);
			}
	};
	if (ServerGameObject* player = server.findObject(1027))
		collect(player, collect);
	const int guardCommand = server.gameSet->commands.names.getIndex("Guard");
	const int moveCommand = server.gameSet->commands.names.getIndex("Move");
	if (characters.size() < 2 || guardCommand < 0 || moveCommand < 0) {
		printf("GUARD not set up: %zu characters, commands %d %d\n", characters.size(), guardCommand, moveCommand);
		return;
	}
	ServerGameObject* guard = characters[0];
	ServerGameObject* guardee = characters[1];
	SrvGORef guardRef = guard, guardeeRef = guardee;
	server.timeManager.unpause();
	server.gameSet->commands[guardCommand].execute(guard, guardee, Tags::ORDERASSIGNMODE_FORGET_EVERYTHING_ELSE);
	const uint64_t start = SDL_GetTicks();
	bool moved = false;
	int lastPrint = -1;
	while (SDL_GetTicks() - start < 20000) {
		server.tick();
		const float t = (SDL_GetTicks() - start) / 1000.0f;
		if (!guardRef || !guardeeRef)
			break;
		if (!moved && t > 3.0f) {
			moved = true;
			server.gameSet->commands[moveCommand].execute(guardee, nullptr, Tags::ORDERASSIGNMODE_FORGET_EVERYTHING_ELSE, guardee->position + Vector3(40, 0, 25));
		}
		if ((int)t != lastPrint) {
			lastPrint = (int)t;
			std::string order = "-";
			uint32_t taskTarget = 0;
			if (Order* o = guard->orderConfig.getCurrentOrder()) {
				order = server.gameSet->orders.getString(o->blueprint->bpid);
				if (Task* task = o->getCurrentTask())
					taskTarget = task->target ? task->target->id : 0;
			}
			printf("GUARD t=%d distance=%.1f order=%s target=%u guardee=%u guard (%.0f,%.0f) guardee (%.0f,%.0f) parent %s\n", lastPrint,
				(guard->position - guardee->position).len2xz(), order.c_str(), taskTarget, guardee->id,
				guard->position.x, guard->position.z, guardee->position.x, guardee->position.z, guard->parent ? guard->parent->blueprint->name.c_str() : "-");
		}
		SDL_Delay(16);
	}
	fflush(stdout);
}

// Save games (needs the game data): a game is loaded, run for a few seconds (with a unit walking),
// saved, loaded into a new server and saved again. The two files should be identical. Done for
// 4 Peaks and, when it is in the user folder's Save_Games, for "Original.sav", a save written by
// the original game. Prints "SAVE <name> objects=<n> identical=<0|1>" and the first differing line.
static size_t CountObjects(const CommonGameObject* obj)
{
	size_t n = 1;
	for (const auto& [type, children] : obj->children)
		for (const CommonGameObject* child : children)
			n += CountObjects(child);
	return n;
}

// Same words, numbers equal within 0.05 (a unit's height snaps to the terrain again on loading);
// a save file's section line by its name and version (its length follows the numbers' digits)
static bool SameSaveLine(const std::string& a, const std::string& b)
{
	if (a.rfind("SECTION ", 0) == 0 && b.rfind("SECTION ", 0) == 0)
		return a.substr(0, a.rfind(' ')) == b.substr(0, b.rfind(' '));
	// the chronicle's "first scan" flag is set again by loading (GameChronicle::stateFromText: what
	// it had seen isn't saved, so its first scan only notes it); the narrator's, not the game's
	if (a.rfind("CHRONICLE STATE ", 0) == 0 && b.rfind("CHRONICLE STATE ", 0) == 0) {
		auto without = [](const std::string& s) {
			std::istringstream in(s);
			std::string w, out;
			for (int i = 0; in >> w; i++)
				if (i != 0 && i != 5) // (without "CHRONICLE" too: compared as numbers, not again here)
					out += w + " ";
			return out;
		};
		return SameSaveLine(without(a), without(b));
	}
	std::istringstream sa(a), sb(b);
	std::string wa, wb;
	while (true) {
		const bool ga = (bool)(sa >> wa), gb = (bool)(sb >> wb);
		if (!ga || !gb)
			return ga == gb;
		if (wa == wb)
			continue;
		char* ea, * eb;
		const double da = strtod(wa.c_str(), &ea), db = strtod(wb.c_str(), &eb);
		if (*ea || *eb || std::abs(da - db) > 0.05)
			return false;
	}
}

static void SaveRoundTrip(const char* source, const char* label, uint32_t runMs)
{
	const std::string name = std::string("Save_Games\\wkbre2 round trip ") + label;
	const std::string first = PathToUtf8(g_userPath / "saved" / PathFromUtf8(name + " 1.sav"));
	const std::string second = PathToUtf8(g_userPath / "saved" / PathFromUtf8(name + " 2.sav"));
	size_t objects = 0;
	{
		Server server;
		server.loadSaveGame(source);
		const int moveCommand = server.gameSet->commands.names.getIndex("Move");
		ServerGameObject* walker = nullptr;
		auto find = [&](CommonGameObject* obj, auto& self) -> void {
			for (const auto& [type, children] : obj->children)
				for (CommonGameObject* child : children) {
					if (!walker && child->blueprint->bpClass == Tags::GAMEOBJCLASS_CHARACTER)
						walker = child->dyncast<ServerGameObject>();
					self(child, self);
				}
		};
		if (ServerGameObject* player = server.findObject(1027))
			find(player, find);
		if (walker && moveCommand >= 0)
			server.gameSet->commands[moveCommand].execute(walker, nullptr, Tags::ORDERASSIGNMODE_FORGET_EVERYTHING_ELSE, walker->position + Vector3(40, 0, 25));
		const uint64_t start = SDL_GetTicks();
		while (SDL_GetTicks() - start < runMs) {
			server.tick();
			SDL_Delay(16);
		}
		server.timeManager.pause();
		objects = CountObjects(server.level);
		printf("SAVE %s first: %d\n", label, (int)server.saveGame(first.c_str()));
	}
	{
		Server server;
		server.loadSaveGame((name + " 1.sav").c_str());
		server.timeManager.pause(); // the loader starts the clock
		printf("SAVE %s loaded objects %zu of %zu\n", label, CountObjects(server.level), objects);
		printf("SAVE %s second: %d\n", label, (int)server.saveGame(second.c_str()));
	}
	std::ifstream a(PathFromUtf8(first)), b(PathFromUtf8(second));
	std::string la, lb;
	int line = 0;
	bool identical = true;
	while (true) {
		const bool ga = (bool)std::getline(a, la), gb = (bool)std::getline(b, lb);
		line++;
		if (!ga && !gb)
			break;
		if (ga != gb || !SameSaveLine(la, lb)) {
			printf("SAVE %s differ at line %d:\n  %s\n  %s\n", label, line, la.c_str(), lb.c_str());
			identical = false;
			break;
		}
	}
	printf("SAVE %s objects=%zu lines=%d identical=%d\n", label, objects, line, (int)identical);
}

void Test_SaveRoundTrip()
{
	LoadBCP("data.bcp");
	SaveRoundTrip("Save_Games\\2 Player 4 Peaks.lvl", "4 Peaks", 3000);
	if (FileExists("Save_Games\\Original.sav"))
		SaveRoundTrip("Save_Games\\Original.sav", "original", 3000);
	fflush(stdout);
}

// Rally point (needs the game data): peasants sent one after another to the same place get
// different free spots. Prints "RALLY <i> (x,z)" and the closest pair's distance.
void Test_RallyPoint()
{
	LoadBCP("data.bcp");
	Server server;
	server.loadSaveGame("Save_Games\\2 Player 4 Peaks.lvl");
	ServerGameObject* player = server.findObject(1027);
	const GameObjBlueprint* peasant = server.gameSet->findBlueprint(Tags::GAMEOBJCLASS_CHARACTER, "Peasant");
	if (!player || !peasant)
		return;
	const Vector3 rally(560, 0, 160);
	std::vector<Vector3> spots;
	for (int i = 0; i < 5; i++) {
		ServerGameObject* unit = server.spawnObject(peasant, player, Vector3(540, 0, 140), Vector3(0, 0, 0));
		const Vector3 spot = FreeStandingSpot(unit, rally);
		unit->movementController.startMovement(spot);
		spots.push_back(spot);
		printf("RALLY %d (%.1f,%.1f)\n", i, spot.x, spot.z);
	}
	float closest = 1e9f;
	for (size_t a = 0; a < spots.size(); a++)
		for (size_t b = a + 1; b < spots.size(); b++)
			closest = std::min(closest, (spots[a] - spots[b]).len2xz());
	printf("RALLY closest pair %.1f m\n", closest);
	fflush(stdout);
}

// Gathering (needs the game data): a peasant chops a tree that has only 12 wood left; once it
// is gone the peasant should go on with the next tree. Prints "GATHER t=<s> order task target",
// and when the peasant goes idle what it could have gone on with. Options (environment):
// WKB_GATHER_KIND=gold|stone, WKB_GATHER_FULL=1, WKB_GATHER_PEASANTS=<n>, WKB_GATHER_SECONDS=<s>,
// WKB_GATHER_CLEAR=<m>; four miners on a full outcropping went idle when it ran out.
void Test_Gather()
{
	LoadBCP("data.bcp");
	Server server;
	server.loadSaveGame("Save_Games\\2 Player 4 Peaks.lvl");
	ServerGameObject* player = server.findObject(1027);
	// WKB_GATHER_KIND=gold|stone: mine an outcropping with 12 of that left instead
	const char* kindEnv = getenv("WKB_GATHER_KIND");
	const std::string kind = kindEnv ? kindEnv : "wood";
	const char* command = kind == "gold" ? "Mine Gold" : kind == "stone" ? "Mine" : "Chop Tree";
	const char* resource = kind == "gold" ? "Gold" : kind == "stone" ? "Stone" : "Wood";
	const int chop = server.gameSet->commands.names.getIndex(command);
	const int wood = server.gameSet->items.names.getIndex(resource);
	ServerGameObject* peasant = nullptr;
	auto find = [&](CommonGameObject* obj, auto& self) -> void {
		for (const auto& [type, children] : obj->children)
			for (CommonGameObject* child : children) {
				if (!peasant && child->blueprint->name == "Peasant")
					peasant = child->dyncast<ServerGameObject>();
				self(child, self);
			}
	};
	if (player)
		find(player, find);
	// the nearest tree
	ServerGameObject* tree = nullptr;
	float best = 1e12f;
	auto findTree = [&](CommonGameObject* obj, auto& self) -> void {
		for (const auto& [type, children] : obj->children)
			for (CommonGameObject* child : children) {
				if (peasant && (kind == "wood" ? child->blueprint->name == "Coniferous Tree"
					: child->blueprint->bpClass == Tags::GAMEOBJCLASS_CONTAINER && child->dyncast<ServerGameObject>()->getItem(wood) > 0.0f)) {
					const float d = (child->position - peasant->position).sqlen2xz();
					if (d < best) { best = d; tree = child->dyncast<ServerGameObject>(); }
				}
				self(child, self);
			}
	};
	findTree(server.getLevel(), findTree);
	if (!peasant || !tree || chop < 0) {
		printf("GATHER not set up\n");
		return;
	}
	// WKB_GATHER_FULL=1: the target keeps what it has (a longer game, see WKB_GATHER_SECONDS)
	if (!getenv("WKB_GATHER_FULL"))
		tree->setItem(wood, kind == "wood" ? 12.0f : 4.0f); // an outcropping is gone before the peasant is full
	// WKB_GATHER_CLEAR=<m>: the other trees within that many metres of the first are removed
	// (as chopped ones are), so the next one is farther than the 25 m the chop order looks
	// around its tree
	if (const char* e = getenv("WKB_GATHER_CLEAR")) {
		const float r = (float)atof(e);
		std::vector<ServerGameObject*> cleared;
		auto collect = [&](CommonGameObject* obj, auto& self) -> void {
			for (const auto& [type, children] : obj->children)
				for (CommonGameObject* child : children) {
					if (child != tree && child->blueprint->bpClass == Tags::GAMEOBJCLASS_CONTAINER && (kind == "wood" ? child->blueprint->name.find("Tree") != std::string::npos : child->dyncast<ServerGameObject>()->getItem(wood) > 0.0f)
						&& (child->position - tree->position).len2xz() < r)
						cleared.push_back(child->dyncast<ServerGameObject>());
					self(child, self);
				}
		};
		collect(server.getLevel(), collect);
		for (ServerGameObject* obj : cleared) {
			obj->terminate();
			obj->destroy();
		}
		printf("GATHER removed %zu trees within %.0f m\n", cleared.size(), r);
	}
	SrvGORef treeRef = tree;
	printf("GATHER %s: peasant %u, target %u (%s) at %.0f m\n", kind.c_str(), peasant->id, tree->id, tree->blueprint->name.c_str(), std::sqrt(best));
	server.timeManager.unpause();
	server.startLevel(); // as a game does: the level start reactions register the deliverable buildings
	server.gameSet->commands[chop].execute(peasant, tree, Tags::ORDERASSIGNMODE_FORGET_EVERYTHING_ELSE);
	// WKB_GATHER_PEASANTS=<n>: n more peasants beside the first chop the same tree
	std::vector<SrvGORef> others;
	if (const char* e = getenv("WKB_GATHER_PEASANTS"))
		for (int i = 0; i < atoi(e); i++) {
			ServerGameObject* other = server.spawnObject(peasant->blueprint, player, peasant->position + Vector3((float)(i % 3) * 2.0f, 0, (float)(i / 3) * 2.0f + 2.0f), Vector3(0, 0, 0));
			server.gameSet->commands[chop].execute(other, tree, Tags::ORDERASSIGNMODE_FORGET_EVERYTHING_ELSE);
			others.push_back(other);
		}
	// WKB_GATHER_SECONDS=<s>: game seconds to run (default 80), simulated without the real clock
	const int seconds = getenv("WKB_GATHER_SECONDS") ? atoi(getenv("WKB_GATHER_SECONDS")) : 80;
	server.timeManager.stepMs = 20;
	int lastPrint = -1, step = 0;
	while (step * 20 < seconds * 1000) {
		server.timeManager.pendingMs = server.timeManager.stepMs;
		if (server.timeManager.step())
			server.simulate();
		const int t = (++step * 20) / 1000;
		{
			std::string order = "-", task = "-", target = "-";
			if (Order* o = peasant->orderConfig.getCurrentOrder()) {
				order = server.gameSet->orders.getString(o->blueprint->bpid);
				if (Task* tk = o->getCurrentTask()) {
					task = server.gameSet->tasks.getString(tk->blueprint->bpid);
					if (tk->target)
						target = tk->target->blueprint->name + " #" + std::to_string(tk->target->id);
				}
			}
			// each change of order, task or target, and every 10 seconds
			static std::string last;
			const std::string now = order + " / " + task + " -> " + target;
			if (now != last || t / 10 != lastPrint) {
				last = now;
				lastPrint = t / 10;
				printf("GATHER t=%d %s | first target %s | carrying %.0f\n", t, now.c_str(), treeRef ? "there" : "gone", peasant->getItem(wood));
				// on going idle: the targets in reach of it
				static bool reported = false;
				if (order == "-" && !reported) {
					reported = true;
					std::vector<std::pair<float, CommonGameObject*>> inReach;
					auto list = [&](CommonGameObject* obj, auto& self) -> void {
						for (const auto& [type, children] : obj->children)
							for (CommonGameObject* child : children) {
								if (child->blueprint->bpClass == Tags::GAMEOBJCLASS_CONTAINER && child->dyncast<ServerGameObject>()->getItem(wood) > 0.0f)
									inReach.push_back({ (child->position - peasant->position).len2xz(), child });
								self(child, self);
							}
					};
					list(server.getLevel(), list);
					std::sort(inReach.begin(), inReach.end(), [](auto& a, auto& b) { return a.first < b.first; });
					for (size_t i = 0; i < inReach.size() && i < 4; i++)
						printf("GATHER   idle: %s #%u at %.0f m (%.0f left, interactable %d)\n", inReach[i].second->blueprint->name.c_str(), inReach[i].second->id, inReach[i].first,
							inReach[i].second->dyncast<ServerGameObject>()->getItem(wood), (int)inReach[i].second->isInteractable());
					const char* finderName = kind == "gold" ? "I Need Gold" : kind == "stone" ? "I Need Rock" : "I Need Wood";
					const int fi = server.gameSet->objectFinderDefinitions.names.getIndex(finderName);
					if (fi >= 0) {
						SrvScriptContext ctx(&server, peasant);
						for (ServerGameObject* found : server.gameSet->objectFinderDefinitions[fi]->eval(&ctx))
							printf("GATHER   \"%s\" finds %s #%u (interactable %d)\n", finderName, found->blueprint->name.c_str(), found->id, (int)found->isInteractable());
					}
				}
			}
		}
	}
	for (const SrvGORef& ref : others) {
		Order* o = ref ? ref->orderConfig.getCurrentOrder() : nullptr;
		printf("GATHER other %u: %s\n", ref ? ref->id : 0, o ? server.gameSet->orders.getString(o->blueprint->bpid).c_str() : "idle");
	}
	fflush(stdout);
}

// Farming (needs the game data): a peasant builds a field and should farm it after; then it is
// sent away and a second peasant is ordered to farm the field. Prints "FARM t=<s> ..." on changes.
void Test_Farm()
{
	LoadBCP("data.bcp");
	Server server;
	server.loadSaveGame("Save_Games\\2 Player 4 Peaks.lvl");
	ServerGameObject* player = server.findObject(1027);
	std::vector<ServerGameObject*> peasants;
	auto find = [&](CommonGameObject* obj, auto& self) -> void {
		for (const auto& [type, children] : obj->children)
			for (CommonGameObject* child : children) {
				if (child->blueprint->name == "Peasant")
					peasants.push_back(child->dyncast<ServerGameObject>());
				self(child, self);
			}
	};
	if (player)
		find(player, find);
	const GameObjBlueprint* foundationType = server.gameSet->findBlueprint(Tags::GAMEOBJCLASS_BUILDING, "Standard Farm Foundation");
	const int build = server.gameSet->commands.names.getIndex("Build");
	const int farm = server.gameSet->commands.names.getIndex("Farm");
	const int move = server.gameSet->commands.names.getIndex("Move");
	if (peasants.size() < 2 || !foundationType || build < 0 || farm < 0 || move < 0) {
		printf("FARM not set up (%zu peasants)\n", peasants.size());
		return;
	}
	server.timeManager.unpause();
	server.startLevel();
	ServerGameObject* a = peasants[0];
	ServerGameObject* b = peasants[1];
	// a free spot for the field, near the first peasant
	Vector3 spot = a->position + Vector3(25, 0, 0);
	for (int i = 0; i < 40; i++) {
		const Vector3 tryAt = a->position + Vector3(20.0f + 5.0f * (i % 8), 0, -20.0f + 10.0f * (i / 8));
		if (StampdownPlan::getStampdownPlan(&server, player, foundationType, tryAt, Vector3(0, 0, 0)).status == StampdownPlan::Status::Valid) {
			spot = tryAt;
			break;
		}
	}
	ServerGameObject* foundation = server.spawnObject(foundationType, player, spot, Vector3(0, 0, 0));
	foundation->sendEvent(Tags::PDEVENT_ON_STAMPDOWN);
	server.gameSet->commands[build].execute(a, foundation, Tags::ORDERASSIGNMODE_FORGET_EVERYTHING_ELSE);
	const int worked = server.gameSet->items.names.getIndex("I'm Being Worked at");
	SrvGORef fieldRef = foundation;
	auto describe = [&](ServerGameObject* p) {
		std::string order = "-", task = "-", target = "-";
		if (Order* o = p->orderConfig.getCurrentOrder()) {
			order = server.gameSet->orders.getString(o->blueprint->bpid);
			if (Task* tk = o->getCurrentTask()) {
				task = server.gameSet->tasks.getString(tk->blueprint->bpid);
				if (tk->target)
					target = tk->target->blueprint->name + " #" + std::to_string(tk->target->id);
			}
		}
		return order + " / " + task + " -> " + target;
	};
	const uint64_t start = SDL_GetTicks();
	bool sentAway = false, secondOrdered = false;
	std::string last;
	while (SDL_GetTicks() - start < 150000) {
		server.tick();
		const int t = (int)((SDL_GetTicks() - start) / 1000);
		ServerGameObject* field = fieldRef.get();
		// once the first peasant farms, send it away; then order the second one to the field
		static int farmingSince = -1;
		if (farmingSince < 0 && describe(a).rfind("Farm /", 0) == 0)
			farmingSince = t;
		if (!sentAway && field && field->blueprint->name.find("Foundation") == std::string::npos
			&& farmingSince >= 0 && t > farmingSince + 25) {
			sentAway = true;
			server.gameSet->commands[move].execute(a, nullptr, Tags::ORDERASSIGNMODE_FORGET_EVERYTHING_ELSE, a->position + Vector3(-30, 0, 0));
			printf("FARM t=%d first peasant sent away\n", t);
		}
		static int awayAt = -1;
		if (sentAway && awayAt < 0)
			awayAt = t;
		if (sentAway && !secondOrdered && t > awayAt + 4 && field) {
			secondOrdered = true;
			server.gameSet->commands[farm].execute(b, field, Tags::ORDERASSIGNMODE_FORGET_EVERYTHING_ELSE);
			printf("FARM t=%d second peasant ordered to farm\n", t);
		}
		const std::string now = "A: " + describe(a) + " | B: " + describe(b) + " | field " +
			(field ? field->blueprint->name + " worked at " + std::to_string((int)field->getItem(worked)) : std::string("gone"));
		if (now != last) {
			last = now;
			printf("FARM t=%d %s\n", t, now.c_str());
		}
		// where the farmers work: their distance from the field's centre, every 10 s (a 3 x 3
		// tile field reaches 7.5 m from it; they had stood 6.5 m outside its edge)
		static int lastWhere = -10;
		if (field && t >= lastWhere + 10) {
			lastWhere = t;
			printf("FARM t=%d where: A %.1f m, B %.1f m from the field's centre\n", t,
				(a->position - field->position).len2xz(), (b->position - field->position).len2xz());
		}
		if (secondOrdered && t > awayAt + 40)
			break;
		SDL_Delay(16);
	}

	// a spawned farmer: the Manor's spawned units get the Farm command on a second, finished field
	const GameObjBlueprint* fieldType = server.gameSet->findBlueprint(Tags::GAMEOBJCLASS_BUILDING, "Standard Farm");
	ServerGameObject* manor = nullptr;
	auto findManor = [&](CommonGameObject* obj, auto& self) -> void {
		for (const auto& [type, children] : obj->children)
			for (CommonGameObject* child : children) {
				if (!manor && child->blueprint->name == "Manor")
					manor = child->dyncast<ServerGameObject>();
				self(child, self);
			}
	};
	findManor(player, findManor);
	const int spawnPeasant = server.gameSet->commands.names.getIndex("Spawn Peasant");
	if (!fieldType || !manor || spawnPeasant < 0) {
		printf("FARM spawn part not set up\n");
		return;
	}
	Vector3 spot2 = spot + Vector3(0, 0, 30);
	for (int i = 0; i < 40; i++) {
		const Vector3 tryAt = spot + Vector3(-20.0f + 5.0f * (i % 8), 0, 20.0f + 10.0f * (i / 8));
		if (StampdownPlan::getStampdownPlan(&server, player, fieldType, tryAt, Vector3(0, 0, 0)).status == StampdownPlan::Status::Valid) {
			spot2 = tryAt;
			break;
		}
	}
	ServerGameObject* field2 = server.spawnObject(fieldType, player, spot2, Vector3(0, 0, 0));
	field2->sendEvent(Tags::PDEVENT_ON_STAMPDOWN);
	manor->setBuildingSpawnedUnitOrderToTarget(farm, field2);
	std::set<ServerGameObject*> before(peasants.begin(), peasants.end());
	server.gameSet->commands[spawnPeasant].execute(manor, nullptr, Tags::ORDERASSIGNMODE_DO_LAST);
	printf("FARM spawning a peasant at the Manor for field %u\n", field2->id);
	ServerGameObject* spawned = nullptr;
	const uint64_t start2 = SDL_GetTicks();
	last.clear();
	while (SDL_GetTicks() - start2 < 60000) {
		server.tick();
		const int t = (int)((SDL_GetTicks() - start2) / 1000);
		if (!spawned) {
			peasants.clear();
			find(player, find);
			for (ServerGameObject* p : peasants)
				if (!before.count(p))
					spawned = p;
		}
		const std::string now = std::string("spawned: ") + (spawned ? describe(spawned) : std::string("not yet")) +
			" | field 2 worked at " + std::to_string((int)field2->getItem(worked));
		if (now != last) {
			last = now;
			printf("FARM spawn t=%d %s\n", t, now.c_str());
		}
		SDL_Delay(16);
	}

	// a third peasant told to farm the first field, which the second one farms: the Farm command's
	// POST_ORDER_SEQUENCE ("I didn't get the farmer job") sends it to the nearest free farm, a
	// third field put down for it (it had stood idle by the farmed one)
	peasants.clear();
	find(player, find);
	ServerGameObject* c = nullptr;
	for (ServerGameObject* p : peasants)
		if (p != a && p != b && p != spawned && !p->orderConfig.getCurrentOrder()) {
			c = p;
			break;
		}
	ServerGameObject* field1 = fieldRef.get();
	if (!c || !field1) {
		printf("FARM third part not set up\n");
		fflush(stdout);
		return;
	}
	Vector3 spot3 = spot + Vector3(0, 0, -30);
	for (int i = 0; i < 40; i++) {
		const Vector3 tryAt = spot + Vector3(-20.0f + 5.0f * (i % 8), 0, -20.0f - 10.0f * (i / 8));
		if (StampdownPlan::getStampdownPlan(&server, player, fieldType, tryAt, Vector3(0, 0, 0)).status == StampdownPlan::Status::Valid) {
			spot3 = tryAt;
			break;
		}
	}
	ServerGameObject* field3 = server.spawnObject(fieldType, player, spot3, Vector3(0, 0, 0));
	field3->sendEvent(Tags::PDEVENT_ON_STAMPDOWN);
	server.gameSet->commands[farm].execute(c, field1, Tags::ORDERASSIGNMODE_FORGET_EVERYTHING_ELSE);
	printf("FARM third peasant %u told to farm field %u (worked at %d); free field 3 is %u\n", c->id, field1->id, (int)field1->getItem(worked), field3->id);
	const uint64_t start3 = SDL_GetTicks();
	last.clear();
	while (SDL_GetTicks() - start3 < 40000) {
		server.tick();
		const int t = (int)((SDL_GetTicks() - start3) / 1000);
		const std::string now = "third: " + describe(c) + " | field 1 worked at " + std::to_string((int)field1->getItem(worked)) +
			", field 3 worked at " + std::to_string((int)field3->getItem(worked));
		if (now != last) {
			last = now;
			printf("FARM third t=%d %s\n", t, now.c_str());
		}
		SDL_Delay(16);
	}
	fflush(stdout);
}

// Job positions (needs the game data): on 4 Peaks, a peasant on each job beside the Manor (shop
// keeping, farming, wood chopping with two on one tree, gold and stone mining, building with
// two on one foundation, repairing, delivering) and the buildings they visit on the way (windmill,
// sawmill, bank). 120 fixed game seconds; each time a worker starts working at its target
// ("proximity satisfied") it prints where it stands: in the target's own frame (x right, z
// forward), from its centre, and from the nearest attachment point of the task's category
// (AC_ for ACTIVITY_PATH, DE_ for DELIVERY), which is where WKB.exe puts it: the end of one of
// the target's paths. "JOBPOS summary" lines give each job's farthest distance from its point.
void Test_JobPositions()
{
	LoadBCP("data.bcp");
	Server server;
	server.loadSaveGame("Save_Games\\2 Player 4 Peaks.lvl");
	g_settings["simSeed"] = 1;
	ServerGameObject* player = server.findObject(1027);
	ServerGameObject* peasant0 = nullptr, * manor = nullptr;
	auto find = [&](CommonGameObject* obj, auto& self) -> void {
		for (const auto& [type, children] : obj->children)
			for (CommonGameObject* child : children) {
				if (!peasant0 && child->blueprint->name == "Peasant")
					peasant0 = child->dyncast<ServerGameObject>();
				if (!manor && child->blueprint->name == "Manor")
					manor = child->dyncast<ServerGameObject>();
				self(child, self);
			}
	};
	if (player)
		find(player, find);
	if (!peasant0 || !manor) {
		printf("JOBPOS not set up\n");
		return;
	}
	server.startLevel();
	// buildings in free spots around the Manor, each before the next is looked for
	auto place = [&](const char* name, float minR) -> ServerGameObject* {
		const GameObjBlueprint* bp = server.gameSet->findBlueprint(Tags::GAMEOBJCLASS_BUILDING, name);
		if (!bp) {
			printf("JOBPOS no building type \"%s\"\n", name);
			return nullptr;
		}
		for (int r = (int)minR; r < 120; r += 5)
			for (int a = 0; a < 16; a++) {
				const float ang = a * 6.2831853f / 16.0f;
				const Vector3 at = manor->position + Vector3(std::cos(ang) * r, 0, std::sin(ang) * r);
				if (StampdownPlan::getStampdownPlan(&server, player, bp, at, Vector3(0, 0, 0)).status == StampdownPlan::Status::Valid) {
					ServerGameObject* b = server.spawnObject(bp, player, at, Vector3(0, 0, 0));
					b->sendEvent(Tags::PDEVENT_ON_STAMPDOWN);
					printf("JOBPOS placed %s #%u at (%.1f, %.1f), %.0f m from the Manor\n", name, b->id, b->position.x, b->position.z, (b->position - manor->position).len2xz());
					return b;
				}
			}
		printf("JOBPOS no spot for %s\n", name);
		return nullptr;
	};
	ServerGameObject* shop = place("Shop Stone", 25);
	ServerGameObject* farm = place("Standard Farm", 30);
	ServerGameObject* foundation = place("Barracks Stone Foundation", 30);
	ServerGameObject* damaged = place("Barracks Stone", 30);
	place("Windmill", 30);
	place("Sawmill", 30);
	place("Bank", 30);
	if (damaged) {
		const int hp = server.gameSet->items.names.getIndex("Hit Points");
		const int hpc = server.gameSet->items.names.getIndex("Hit Point Capacity");
		if (hp >= 0 && hpc >= 0)
			damaged->setItem(hp, damaged->getItem(hpc) * 0.3f);
	}
	// the nearest tree and outcroppings to the Manor
	const int goldItem = server.gameSet->items.names.getIndex("Gold"), stoneItem = server.gameSet->items.names.getIndex("Stone");
	ServerGameObject* tree = nullptr, * gold = nullptr, * stone = nullptr;
	float dTree = 1e12f, dGold = 1e12f, dStone = 1e12f;
	auto findRes = [&](CommonGameObject* obj, auto& self) -> void {
		for (const auto& [type, children] : obj->children)
			for (CommonGameObject* child : children) {
				ServerGameObject* s = child->dyncast<ServerGameObject>();
				const float d = (child->position - manor->position).sqlen2xz();
				if (child->blueprint->name == "Coniferous Tree" && d < dTree) { dTree = d; tree = s; }
				else if (child->blueprint->bpClass == Tags::GAMEOBJCLASS_CONTAINER && goldItem >= 0 && s->getItem(goldItem) > 0.0f && d < dGold) { dGold = d; gold = s; }
				else if (child->blueprint->bpClass == Tags::GAMEOBJCLASS_CONTAINER && stoneItem >= 0 && s->getItem(stoneItem) > 0.0f && d < dStone) { dStone = d; stone = s; }
				self(child, self);
			}
	};
	findRes(server.getLevel(), findRes);

	struct Job { const char* name; const char* command; ServerGameObject* target; int workers; };
	const std::vector<Job> jobs = {
		{ "shop keeping", "Shop", shop, 1 },
		{ "farming", "Farm", farm, 1 }, // (one farmer a field: "Nearest local, unoccupied Farm")
		{ "wood chopping", "Chop Tree", tree, 2 },
		{ "gold mining", "Mine Gold", gold, 2 },
		{ "stone mining", "Mine", stone, 1 },
		{ "building", "Build", foundation, 2 },
		{ "repairing", "Repair", damaged, 1 },
	};
	struct Worker { SrvGORef unit; std::string job; std::string lastTask; uint32_t lastTarget = 0; bool reported = true; };
	std::vector<Worker> workers;
	int n = 0;
	for (const Job& job : jobs) {
		const int cmd = server.gameSet->commands.names.getIndex(job.command);
		if (!job.target || cmd < 0) {
			printf("JOBPOS %s: not set up (%s)\n", job.name, job.target ? "no command" : "no target");
			continue;
		}
		for (int i = 0; i < job.workers; i++, n++) {
			// from around the Manor, 10 m out
			const float ang = n * 0.7f;
			ServerGameObject* w = server.spawnObject(peasant0->blueprint, player, manor->position + Vector3(std::cos(ang) * 12.0f, 0, std::sin(ang) * 12.0f), Vector3(0, 0, 0));
			server.gameSet->commands[cmd].execute(w, job.target, Tags::ORDERASSIGNMODE_FORGET_EVERYTHING_ELSE);
			workers.push_back({ w, job.name });
		}
	}
	// where a worker stands relative to its task's target
	std::map<std::string, float> worst;
	auto report = [&](Worker& wk, ServerGameObject* w, Task* task, ServerGameObject* target) {
		const Vector3 d = w->position - target->position;
		const float c = std::cos(target->orientation.y), s = std::sin(target->orientation.y);
		const float lx = d.x * c + d.z * s, lz = -d.x * s + d.z * c;
		const char* prefix = task->blueprint->activityPath ? "AC_" : task->blueprint->deliveryPath ? "DE_" : nullptr;
		std::string nearestTag = "-";
		float nearest = -1.0f;
		if (prefix)
			if (Model* model = target->blueprint->getModel(target->subtype, target->appearance, target->animationIndex, target->animationVariant)) {
				// (as the server places them: the type's scale, not the random size variation)
				const float c = std::cos(-target->orientation.y), sn = std::sin(-target->orientation.y);
				const Vector3& sc = target->blueprint->scaleAppearance;
				for (size_t i = 0; i < model->getNumAPs(); i++) {
					const AttachmentPoint& ap = model->getAPInfo(i);
					if (ap.tag.compare(0, 3, prefix) != 0)
						continue;
					const Vector3& p = ap.staticState.position;
					const Vector3 at(p.x * sc.x * c + p.z * sc.z * sn + target->position.x, 0.0f, -p.x * sc.x * sn + p.z * sc.z * c + target->position.z);
					const float dist = (at - w->position).len2xz();
					if (nearest < 0.0f || dist < nearest) { nearest = dist; nearestTag = ap.tag; }
				}
			}
		const std::string taskName = server.gameSet->tasks.getString(task->blueprint->bpid);
		// from the end of the path it was given (WKB.exe's rule: it works there)
		const float fromEnd = task->pathNo >= 0 && !task->pathPoints.empty() ? (task->pathPoints.back() - w->position).len2xz() : -1.0f;
		char endText[64] = "";
		if (fromEnd >= 0.0f)
			snprintf(endText, sizeof(endText), ", path %d end %.2f m", task->pathNo, fromEnd);
		printf("JOBPOS %-13s #%u %-42s -> %-30s local (%6.2f, %6.2f) centre %5.2f m, nearest %s point %s %s%s\n",
			wk.job.c_str(), w->id, ("\"" + taskName + "\"").c_str(), (target->blueprint->name + " #" + std::to_string(target->id)).c_str(),
			lx, lz, d.len2xz(), prefix ? prefix : "(none)", nearestTag.c_str(),
			nearest >= 0.0f ? (std::to_string(nearest).substr(0, 4) + " m").c_str() : "", endText);
		if (fromEnd >= 0.0f) {
			float& wv = worst[wk.job + " / " + taskName + " (from its path's end)"];
			wv = std::max(wv, fromEnd);
		}
		else if (nearest >= 0.0f) {
			float& wv = worst[wk.job + " / " + taskName + " (no path; nearest point)"];
			wv = std::max(wv, nearest);
		}
		else
			worst.try_emplace(wk.job + " / " + taskName + " (no points; from the centre)", d.len2xz());
	};
	server.timeManager.unpause();
	server.timeManager.stepMs = 20;
	for (int step = 0; step < 6000; step++) {
		server.timeManager.pendingMs = server.timeManager.stepMs;
		if (server.timeManager.step())
			server.simulate();
		for (Worker& wk : workers) {
			ServerGameObject* w = wk.unit.get();
			if (!w) continue;
			Order* o = w->orderConfig.getCurrentOrder();
			Task* t = o ? o->getCurrentTask() : nullptr;
			const std::string tname = t ? server.gameSet->tasks.getString(t->blueprint->bpid) : "-";
			const uint32_t tid = t && t->target ? t->target->id : 0;
			if (tname != wk.lastTask || tid != wk.lastTarget) {
				wk.lastTask = tname;
				wk.lastTarget = tid;
				wk.reported = false;
				continue; // (a cycled task's flags are the last cycle's until it has started)
			}
			if (t && t->target && t->isWorking() && t->proximitySatisfied && !wk.reported) {
				wk.reported = true;
				report(wk, w, t, t->target.get());
			}
		}
		// WKB_JOBPOS_SAVE=<name>: the game saved as <name>.sav in the user folder once the shop
		// keeper, the farmer and the stone miner all work at their targets (to look at them),
		// with a benchmark camera for each
		static const char* saveName = getenv("WKB_JOBPOS_SAVE");
		static bool saved = false;
		if (saveName && !saved) {
			std::vector<std::pair<std::string, ServerGameObject*>> atWork;
			for (Worker& wk : workers)
				if (ServerGameObject* w = wk.unit.get(); w && (wk.job == "shop keeping" || wk.job == "farming" || wk.job == "stone mining")) {
					Order* o = w->orderConfig.getCurrentOrder();
					Task* t = o ? o->getCurrentTask() : nullptr;
					const std::string tname = t ? server.gameSet->tasks.getString(t->blueprint->bpid) : "";
					if (t && t->isWorking() && t->proximitySatisfied && (tname == "Collect Gold From Shop" || tname == "Wait For Gold" || tname == "Farm" || tname == "Mine"))
						atWork.emplace_back(wk.job, w);
				}
			if (atWork.size() == 3) {
				saved = true;
				server.clientPlayerObjects.clear();
				server.clientPlayerObjects.emplace_back(player);
				const std::string path = PathToUtf8(g_userPath / "saved" / "Save_Games" / PathFromUtf8(std::string(saveName) + ".sav"));
				printf("JOBPOS saved %d to %s at t=%.1f\n", (int)server.saveGame(path.c_str()), path.c_str(), server.timeManager.currentTime);
				for (const auto& [job, w] : atWork)
					printf("JOBPOS camera for %s #%u: [%.1f, %.1f, %.1f, -0.75, 0]\n", job.c_str(), w->id, w->position.x, w->position.y + 11.0f, w->position.z - 12.0f);
			}
		}
	}
	for (const auto& [job, d] : worst)
		printf("JOBPOS summary %-70s %5.2f m\n", job.c_str(), d);
	fflush(stdout);
}

// Determinism (needs the game data): 4 Peaks with a fixed seed, every peasant chopping wood, 3000
// fixed steps (60 s); a hash of every object's position, orientation and items. Twice with the
// same seed must give the same hash. Prints "DETERMINISM seed <s> hash <h> objects <n>" and
// "DETERMINISM same 1" when it holds.
static uint64_t DeterminismRun(uint32_t seed)
{
	Server server;
	server.loadSaveGame("Save_Games\\2 Player 4 Peaks.lvl");
	g_settings["simSeed"] = seed;
	server.startLevel();
	// WKB_GATHER_KIND=gold|stone: mine an outcropping with 12 of that left instead
	const char* kindEnv = getenv("WKB_GATHER_KIND");
	const std::string kind = kindEnv ? kindEnv : "wood";
	const char* command = kind == "gold" ? "Mine Gold" : kind == "stone" ? "Mine" : "Chop Tree";
	const char* resource = kind == "gold" ? "Gold" : kind == "stone" ? "Stone" : "Wood";
	const int chop = server.gameSet->commands.names.getIndex(command);
	const int wood = server.gameSet->items.names.getIndex(resource);
	std::vector<ServerGameObject*> peasants, trees;
	auto find = [&](CommonGameObject* obj, auto& self) -> void {
		for (const auto& [type, children] : obj->children)
			for (CommonGameObject* child : children) {
				if (child->blueprint->name == "Peasant")
					peasants.push_back(child->dyncast<ServerGameObject>());
				else if (child->blueprint->name == "Coniferous Tree")
					trees.push_back(child->dyncast<ServerGameObject>());
				self(child, self);
			}
	};
	find(server.getLevel(), find);
	for (ServerGameObject* peasant : peasants) {
		ServerGameObject* best = nullptr;
		float bestDist = 1e12f;
		for (ServerGameObject* tree : trees)
			if (float d = (tree->position - peasant->position).sqlen2xz(); d < bestDist) { bestDist = d; best = tree; }
		if (best && chop >= 0)
			server.gameSet->commands[chop].execute(peasant, best, Tags::ORDERASSIGNMODE_FORGET_EVERYTHING_ELSE);
	}
	server.timeManager.unpause();
	server.timeManager.stepMs = 20;
	const Vector3 firstAt = peasants.empty() ? Vector3() : peasants[0]->position;
	const uint32_t randomBefore = g_simRandom.state;
	for (int i = 0; i < 3000; i++) {
		server.timeManager.pendingMs = server.timeManager.stepMs; // a step without the real clock
		if (server.timeManager.step())
			server.simulate();
	}
	uint64_t hash = 1469598103934665603ull;
	auto mix = [&](const void* data, size_t n) {
		for (size_t i = 0; i < n; i++) { hash ^= ((const uint8_t*)data)[i]; hash *= 1099511628211ull; }
	};
	size_t objects = 0;
	auto walk = [&](CommonGameObject* obj, auto& self) -> void {
		objects++;
		mix(&obj->id, sizeof(obj->id));
		mix(&obj->position, sizeof(obj->position));
		mix(&obj->orientation, sizeof(obj->orientation));
		for (const auto& [item, value] : obj->items) { mix(&item, sizeof(item)); mix(&value, sizeof(value)); }
		for (const auto& [type, children] : obj->children)
			for (CommonGameObject* child : children)
				self(child, self);
	};
	walk(server.getLevel(), walk);
	printf("DETERMINISM seed %u hash %016llx objects %zu, first peasant moved %.1f m, random numbers drawn %d\n", seed,
		(unsigned long long)hash, objects, peasants.empty() ? 0.0f : (peasants[0]->position - firstAt).len2xz(),
		(int)(g_simRandom.state != randomBefore));
	fflush(stdout);
	return hash;
}

// Save format (needs the game data): 4 Peaks with a fixed seed, every peasant chopping wood. At 30 s
// the game is saved and plays on to 60 s; the save loaded in a new server plays the same 30 s.
// Checked: the file is a wkbai save file (WKBAI_SAVE, a world section, no side files); the same
// hash and positions after the 30 s; the original format (saveOriginalFormat: FORMAT SCENARIO and
// the side files) loads with as many objects; a section from a newer build is skipped; a file
// whose line breaks were converted to \r\n loads. Prints "SAVEFMT <check> <0|1>" and
// "SAVEFMT all <0|1>".
static uint64_t WorldHash(Server& server, std::map<uint32_t, Vector3>* positions = nullptr, size_t* count = nullptr)
{
	uint64_t hash = 1469598103934665603ull;
	auto mix = [&](const void* data, size_t n) {
		for (size_t i = 0; i < n; i++) { hash ^= ((const uint8_t*)data)[i]; hash *= 1099511628211ull; }
	};
	size_t objects = 0;
	auto walk = [&](CommonGameObject* obj, auto& self) -> void {
		objects++;
		mix(&obj->id, sizeof(obj->id));
		mix(&obj->position, sizeof(obj->position));
		mix(&obj->orientation, sizeof(obj->orientation));
		if (positions)
			(*positions)[obj->id] = obj->position;
		for (const auto& [item, value] : obj->items) { mix(&item, sizeof(item)); mix(&value, sizeof(value)); }
		for (const auto& [type, children] : obj->children)
			for (CommonGameObject* child : children)
				self(child, self);
	};
	walk(server.getLevel(), walk);
	if (count)
		*count = objects;
	return hash;
}

// Every object's state by id, in an order that doesn't depend on how the objects were created or
// loaded (the children lists and item maps of a loaded game come in the save's order): its
// position, orientation and items, sorted
static std::map<uint32_t, std::string> WorldState(Server& server)
{
	std::map<uint32_t, std::string> state;
	auto walk = [&](CommonGameObject* obj, auto& self) -> void {
		char buf[160];
		snprintf(buf, sizeof(buf), "pos %.9g %.9g %.9g ori %.9g %.9g %.9g", obj->position.x, obj->position.y, obj->position.z,
			obj->orientation.x, obj->orientation.y, obj->orientation.z);
		std::string s = buf;
		std::vector<std::pair<int, float>> items(obj->items.begin(), obj->items.end());
		std::sort(items.begin(), items.end());
		for (const auto& [item, value] : items)
			if (value != 0.0f) { // (a missing item reads as 0, and the save leaves 0 out)
				snprintf(buf, sizeof(buf), " %s=%.9g", server.gameSet->items.names.getString(item).c_str(), value);
				s += buf;
			}
		state[obj->id] = s;
		for (const auto& [type, children] : obj->children)
			for (CommonGameObject* child : children)
				self(child, self);
	};
	walk(server.getLevel(), walk);
	return state;
}

static void StepServer(Server& server, int steps)
{
	server.timeManager.stepMs = 20;
	for (int i = 0; i < steps; i++) {
		server.timeManager.pendingMs = server.timeManager.stepMs; // a step without the real clock
		if (server.timeManager.step())
			server.simulate();
	}
}

void Test_SaveFormat()
{
	LoadBCP("data.bcp");
	bool all = true;
	auto check = [&](const char* what, bool ok) {
		printf("SAVEFMT %s %d\n", what, (int)ok);
		all &= ok;
	};
	const auto dir = g_userPath / "saved" / "Save_Games";
	const std::string name = "wkbai save format test";
	const std::string file = PathToUtf8(dir / PathFromUtf8(name + ".sav"));
	const std::string vfsName = "Save_Games\\" + name + ".sav";
	auto readAll = [](const std::string& path) {
		std::ifstream f(PathFromUtf8(path), std::ios::binary);
		return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
	};
	auto writeAll = [](const std::string& path, const std::string& text) {
		std::ofstream f(PathFromUtf8(path), std::ios::binary);
		f.write(text.data(), (std::streamsize)text.size());
	};
	auto exists = [](const std::string& path) { std::error_code ec; return std::filesystem::exists(PathFromUtf8(path), ec); };

	// the game played straight through, saved at 30 s
	uint64_t straight = 0;
	std::map<uint32_t, Vector3> straightAt;
	std::map<uint32_t, std::string> straightState;
	size_t savedObjects = 0;
	float savedTime = 0.0f;
	{
		Server server;
		server.loadSaveGame("Save_Games\\2 Player 4 Peaks.lvl");
		g_settings["simSeed"] = 4242;
		server.startLevel();
		const int chop = server.gameSet->commands.names.getIndex("Chop Tree");
		std::vector<ServerGameObject*> peasants, trees;
		auto find = [&](CommonGameObject* obj, auto& self) -> void {
			for (const auto& [type, children] : obj->children)
				for (CommonGameObject* child : children) {
					if (child->blueprint->name == "Peasant")
						peasants.push_back(child->dyncast<ServerGameObject>());
					else if (child->blueprint->name == "Coniferous Tree")
						trees.push_back(child->dyncast<ServerGameObject>());
					self(child, self);
				}
		};
		find(server.getLevel(), find);
		for (ServerGameObject* peasant : peasants) {
			ServerGameObject* best = nullptr;
			float bestDist = 1e12f;
			for (ServerGameObject* tree : trees)
				if (float d = (tree->position - peasant->position).sqlen2xz(); d < bestDist) { bestDist = d; best = tree; }
			if (best && chop >= 0)
				server.gameSet->commands[chop].execute(peasant, best, Tags::ORDERASSIGNMODE_FORGET_EVERYTHING_ELSE);
		}
		server.timeManager.unpause();
		StepServer(server, 1500);
		check("saved", server.saveGame(file.c_str()));
		WorldHash(server, nullptr, &savedObjects);
		savedTime = server.timeManager.currentTime;
		StepServer(server, 1500);
		straight = WorldHash(server, &straightAt);
		straightState = WorldState(server);
	}
	const std::string text = readAll(file);
	check("magic", text.rfind("WKBAI_SAVE 1\n", 0) == 0);
	check("world section", text.find("\nSECTION world 1 ") != std::string::npos && text.find("\nEND_SECTION world\n") != std::string::npos);
	check("settings section", text.find("\nSECTION settings 1 ") != std::string::npos);
	check("no side files", !exists(file + ".ai.txt") && !exists(file + ".env.txt") && !exists(file + ".paths.txt") && !exists(file + ".history.txt"));
	printf("SAVEFMT file %zu bytes\n", text.size());

	// loaded and played on for the same 30 s
	{
		Server server;
		server.loadSaveGame(vfsName.c_str());
		size_t loadedObjects = 0;
		WorldHash(server, nullptr, &loadedObjects);
		check("objects loaded", loadedObjects == savedObjects);
		check("time loaded", server.timeManager.currentTime == savedTime);
		StepServer(server, 1500);
		std::map<uint32_t, Vector3> at;
		const uint64_t loaded = WorldHash(server, &at);
		int moved = 0, missing = 0;
		float worst = 0.0f;
		for (const auto& [id, p] : straightAt) {
			auto it = at.find(id);
			if (it == at.end()) { missing++; continue; }
			const float d = (it->second - p).len3();
			if (d > 0.0f) {
				moved++;
				worst = std::max(worst, d);
				if (moved <= 10) {
					const ServerGameObject* o = server.findObject(id);
					std::string orders;
					if (o)
						for (const Order& order : o->orderConfig.orders) {
							orders += " " + server.gameSet->orders.getString(order.blueprint);
							if (order.currentTask >= 0 && order.currentTask < (int)order.tasks.size())
								orders += "/" + server.gameSet->tasks.getString(order.tasks[order.currentTask]->blueprint);
						}
					printf("SAVEFMT elsewhere %u %s: %.2f m (straight %.1f %.1f, loaded %.1f %.1f), orders:%s\n", id,
						o ? o->blueprint->name.c_str() : "?", d, p.x, p.z, it->second.x, it->second.z, orders.c_str());
				}
			}
		}
		printf("SAVEFMT play on: straight %016llx, loaded %016llx; objects %zu vs %zu, %d missing, %d elsewhere (worst %.2f m)\n",
			(unsigned long long)straight, (unsigned long long)loaded, straightAt.size(), at.size(), missing, moved, worst);
		// the same state, object by object (the hash also depends on the order of the lists)
		const std::map<uint32_t, std::string> state = WorldState(server);
		int differ = 0;
		for (const auto& [id, s] : straightState) {
			auto it = state.find(id);
			if (it != state.end() && it->second == s)
				continue;
			if (++differ <= 10) {
				const ServerGameObject* o = server.findObject(id);
				printf("SAVEFMT differs %u %s:\n  straight %s\n  loaded   %s\n", id, o ? o->blueprint->name.c_str() : "?", s.c_str(),
					it == state.end() ? "(none)" : it->second.c_str());
			}
		}
		for (const auto& [id, s] : state)
			if (!straightState.count(id) && ++differ <= 10)
				printf("SAVEFMT differs %u: only after loading: %s\n", id, s.c_str());
		printf("SAVEFMT play on: %d of %zu objects differ in position, orientation or items; hashes %s\n", differ, straightState.size(),
			loaded == straight ? "same" : "differ (order of the lists)");
		check("play on same", differ == 0);
	}

	// the original format, with its side files
	{
		const std::string orig = PathToUtf8(dir / PathFromUtf8(name + " original.sav"));
		Server server;
		server.loadSaveGame(vfsName.c_str());
		server.timeManager.pause();
		server.saveOriginalFormat = true;
		check("original saved", server.saveGame(orig.c_str()));
		const std::string otext = readAll(orig);
		check("original format", otext.rfind("FORMAT SCENARIO", 0) == 0 && otext.find("WKBAI_SAVE") == std::string::npos);
		check("original side file", exists(orig + ".ai.txt"));
		Server again;
		again.loadSaveGame(("Save_Games\\" + name + " original.sav").c_str());
		size_t n = 0;
		WorldHash(again, nullptr, &n);
		check("original loaded", n == savedObjects);
	}

	// a section of a newer build, and the line breaks converted
	{
		std::string future = text;
		const size_t end = future.rfind("END_SAVE");
		future.insert(end, "SECTION ministries 3 12\nhello world\nEND_SECTION ministries\n");
		const std::string fpath = PathToUtf8(dir / PathFromUtf8(name + " future.sav"));
		writeAll(fpath, future);
		Server server;
		server.loadSaveGame(("Save_Games\\" + name + " future.sav").c_str());
		size_t n = 0;
		WorldHash(server, nullptr, &n);
		check("unknown section skipped", n == savedObjects);
	}
	{
		std::string crlf;
		for (char c : text) {
			if (c == '\n')
				crlf += '\r';
			crlf += c;
		}
		const std::string cpath = PathToUtf8(dir / PathFromUtf8(name + " crlf.sav"));
		writeAll(cpath, crlf);
		Server server;
		server.loadSaveGame(("Save_Games\\" + name + " crlf.sav").c_str());
		size_t n = 0;
		WorldHash(server, nullptr, &n);
		check("crlf loaded", n == savedObjects && server.timeManager.currentTime == savedTime);
	}
	printf("SAVEFMT all %d\n", (int)all);
	fflush(stdout);
}

// The ministry AI's framework (needs the game data). The blackboard on its own: requests shared
// out (emergencies first, then value x urgency x weight), a request asked again replaced, workers
// held for their minimum time. Then 4 Peaks with two AI Generals and "aiMinistries": a government
// per AI player, each department thinking at its rate (every 2 s: 30 times a minute), staggered;
// reports from the game; nothing in the game changed by them (the same state as without); the
// "ai" section through a save (the same text loaded and written again; the schedule going on).
// Prints "MINISTRY <check> <0|1>" and "MINISTRY all <0|1>".
static void MinistryGame(Server& server, bool ministries, uint32_t seed, bool diplomacy = false)
{
	server.loadSaveGame("Save_Games\\2 Player 4 Peaks.lvl");
	g_settings["simSeed"] = seed;
	QuickSkirmishMenu skirmish;
	skirmish.changeSettingsForLevel("2 Player 4 Peaks.lvl");
	// two Elite Generals by name: an empty slot draws a random one with the menus' rand(), which
	// differs from game to game
	std::vector<QuickSkirmishMenu::Slot> slots(2);
	const char* generals[2] = { "Belisar The Hammer Kingmaker", "Lady Elinor Kingmaker" };
	for (int i = 0; i < 2; i++) { slots[i].type = QuickSkirmishMenu::Slot::AI; slots[i].general = generals[i]; }
	skirmish.setSlots(slots);
	skirmish.setSetting("aiMinistries", ministries ? 1 : 0);
	if (diplomacy)
		skirmish.setSetting("allowDiplomacy", 1);
	server.timeManager.unpause();
	server.timeManager.stepMs = 20;
	skirmish.applySettings(server);
}

void Test_MinistryFramework()
{
	LoadBCP("data.bcp");
	bool all = true;
	auto check = [&](const char* what, bool ok) {
		printf("MINISTRY %s %d\n", what, (int)ok);
		all &= ok;
	};
	using namespace Ministry;
	{
		Blackboard b;
		b.weights[WAR] = 2.0f;
		b.ask(ECONOMY, "peasants", 5, 10, 1, false, 0);     // priority 10
		b.ask(WAR, "peasants", 4, 4, 2, false, 0);          // 16 (weight 2)
		b.ask(BUILDERS, "peasants", 3, 1, 1, true, 0);      // an emergency
		b.ask(RESEARCH, "peasants", 2, 10, 1, false, 0);    // 10, ties Economy: Economy first
		const std::vector<Grant> g = b.allocate("peasants", 10);
		check("allocate order", g.size() == 3 && g[0].to == BUILDERS && g[1].to == WAR && g[2].to == ECONOMY);
		check("allocate amounts", g.size() == 3 && g[0].amount == 3 && g[1].amount == 4 && g[2].amount == 3);
		const uint32_t id = b.find(WAR, "peasants")->id;
		b.ask(WAR, "peasants", 1, 1, 1, false, 5);
		check("ask again replaces", b.requests.size() == 4 && b.find(WAR, "peasants")->id == id && b.find(WAR, "peasants")->amount == 1);
		b.withdraw(WAR, "peasants");
		check("withdraw", b.requests.size() == 3 && !b.find(WAR, "peasants"));
		check("assign", b.assign(100, ECONOMY, 10, 30, false) && b.assignedTo(100) == ECONOMY);
		check("held for its time", !b.assign(100, WAR, 20, 30, false) && b.assignedTo(100) == ECONOMY);
		check("taken in an emergency", b.assign(100, WAR, 21, 30, true) && b.assignedTo(100) == WAR);
		check("free after its time", b.assign(100, ECONOMY, 52, 30, false) && b.assignedTo(100) == ECONOMY);
	}

	// without them: no governments; with them, the same game twice (they act since step 5, so the
	// game with them is another; it must be the same every time)
	std::map<uint32_t, std::string> without; // (the reference: the first game with them)
	{
		Server server;
		MinistryGame(server, true, 99);
		StepServer(server, 3000);
		without = WorldState(server);
	}
	const auto dir = g_userPath / "saved" / "Save_Games";
	const std::string file = PathToUtf8(dir / "wkbai ministry test.sav");
	std::string savedAi;
	std::map<uint32_t, uint64_t> thinksAt60;
	{
		Server server;
		g_settings["aiTrace"] = 1;
		MinistryGame(server, true, 99);
		for (int i = 0; i < DEPT_COUNT; i++) { g_deptMs[i] = 0; g_deptThinks[i] = 0; }
		StepServer(server, 1500);
		check("saved", server.saveGame(file.c_str()));
		savedAi = server.ministries.saveText();
		StepServer(server, 1500);
		printf("MINISTRY profile over 3000 steps: %s\n", Ministry::Ministries::profileText(3000).c_str());
		check("governments", server.ministries.governments.size() == 2);
		std::vector<float> firsts;
		for (const auto& [id, g] : server.ministries.governments) {
			for (const auto& d : g->departments) {
				printf("MINISTRY player %u %s: %llu thinks, next at %.2f\n", id, DeptName(d->id()), (unsigned long long)d->thinks, d->nextAt);
				const int expected = (int)std::lround(60.0f / d->period());
				check("thinks at its rate", (int)d->thinks >= expected - 1 && (int)d->thinks <= expected + 1);
				thinksAt60[id * 16 + d->id()] = d->thinks;
				firsts.push_back(d->period() * 1000.0f + std::fmod(d->nextAt, d->period()));
			}
			printf("MINISTRY player %u reports: peasants %.0f (%.0f without work), soldiers %.0f, buildings %.0f; food %.0f (%+.0f a minute), wood %.0f (%+.0f)\n", id,
				g->board.get("labour.peasants"), g->board.get("labour.idle"), g->board.get("labour.soldiers"), g->board.get("labour.buildings"),
				g->board.get("economy.food"), g->board.get("economy.food.income"), g->board.get("economy.wood"), g->board.get("economy.wood.income"));
			int economyReports = 0, labourReports = 0;
			for (const auto& [key, v] : g->board.reports) {
				economyReports += key.rfind("economy.", 0) == 0;
				labourReports += key.rfind("labour.", 0) == 0;
			}
			// (the food reported, not that there is some: since the AI pays its spawns queued at busy
			// buildings as WKB.exe, its food can be spent down to 0 at the minute)
			check("reports", g->board.get("labour.peasants") > 0 && g->board.reports.count("economy.food") && economyReports >= 8 && labourReports >= 5);
		}
		std::sort(firsts.begin(), firsts.end());
		check("staggered", std::adjacent_find(firsts.begin(), firsts.end(), [](float a, float b) { return std::abs(a - b) < 0.001f; }) == firsts.end());
		const std::map<uint32_t, std::string> with = WorldState(server);
		int differ = 0;
		for (const auto& [id, s] : without)
			if (!with.count(id) || with.at(id) != s) {
				if (++differ <= 5) {
					ServerGameObject* o = server.findObject(id);
					printf("MINISTRY differs %u %s:\n  first  %s\n  second %s\n", id, o ? o->blueprint->name.c_str() : "?", s.c_str(),
						with.count(id) ? with.at(id).c_str() : "(none)");
				}
			}
		printf("MINISTRY the same game twice with them: %d of %zu objects differ\n", differ, without.size());
		check("the same game twice", differ == 0 && with.size() == without.size());
	}
	g_settings["aiTrace"] = 0;
	{
		Server server;
		server.loadSaveGame("Save_Games\\wkbai ministry test.sav");
		check("loaded on", server.aiMinistries);
		check("ai section loaded", server.ministries.saveText() == savedAi && !savedAi.empty());
		StepServer(server, 1500);
		bool same = true;
		for (const auto& [id, g] : server.ministries.governments)
			for (const auto& d : g->departments)
				same &= thinksAt60[id * 16 + d->id()] == d->thinks;
		check("schedule goes on", same && server.ministries.governments.size() == 2);
	}
	{
		// (after the two compared games: a game before them leaves state behind that moved one
		// peasant 1.5 cm in the game after the next, the RE repo's docs/engine/ministry-ai.md)
		Server server;
		MinistryGame(server, false, 99);
		StepServer(server, 100);
		check("off: no governments", server.ministries.governments.empty());
	}
	const std::string text = [&] { std::ifstream f(PathFromUtf8(file), std::ios::binary); return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()); }();
	check("ai section in the file", text.find("\nSECTION ai 1 ") != std::string::npos && text.find("\nGOVERNMENT ") != std::string::npos);
	printf("MINISTRY all %d\n", (int)all);
	fflush(stdout);
}

// The ministry AI's Intelligence (needs the game data): 4 Peaks, two Elite Generals, the
// ministries on. Only what was seen: at the start neither knows the other's palace (with
// "aiVision" 1 both do). An enemy Pikeman put next to player A's palace is seen at the next think,
// with its power; the threat map has it at the think after (it is worked out meanwhile); moved
// far away it is still remembered where it was seen; gone from a place in sight, it is forgotten.
// The game the same with Intelligence as without; its picture through a save.
// Prints "INTEL <check> <0|1>" and "INTEL all <0|1>".
void Test_MinistryIntelligence()
{
	LoadBCP("data.bcp");
	bool all = true;
	auto check = [&](const char* what, bool ok) {
		printf("INTEL %s %d\n", what, (int)ok);
		all &= ok;
	};
	using namespace Ministry;
	auto knownPalace = [](Server& server, uint32_t me, uint32_t of) {
		Government* g = server.ministries.governments.count(me) ? server.ministries.governments[me].get() : nullptr;
		return g && g->board.get("intel." + std::to_string(of) + ".palace") > 0;
	};
	{
		Server server;
		MinistryGame(server, true, 99);
		StepServer(server, 500); // 10 s
		check("governments", server.ministries.governments.size() == 2);
		check("fair: the other palace not known", !knownPalace(server, 1027, 1030) && !knownPalace(server, 1030, 1027));
		printf("INTEL fair vision at 10 s: %.0f and %.0f sightings\n", server.ministries.governments[1027]->board.get("intel.sightings"),
			server.ministries.governments[1030]->board.get("intel.sightings"));
	}
	{
		Server server;
		MinistryGame(server, true, 99);
		server.aiVision = 1;
		StepServer(server, 500);
		check("aiVision 1: the palaces known", knownPalace(server, 1027, 1030) && knownPalace(server, 1030, 1027));
		printf("INTEL full vision at 10 s: %.0f and %.0f sightings\n", server.ministries.governments[1027]->board.get("intel.sightings"),
			server.ministries.governments[1030]->board.get("intel.sightings"));
	}

	// the probe: a Pikeman of player B next to A's palace
	const auto dir = g_userPath / "saved" / "Save_Games";
	const std::string file = PathToUtf8(dir / "wkbai intel test.sav");
	std::map<uint32_t, std::string> withIntel;
	float threatAfterLoad = -1.0f, threatStraight = -2.0f;
	size_t sightingsSaved = 0;
	{
		Server server;
		MinistryGame(server, true, 99);
		StepServer(server, 250);
		Government* a = server.ministries.governments[1027].get();
		ServerGameObject* palace = AIUtil::Palace(server.findObject(1027));
		const GameObjBlueprint* pike = server.gameSet->findBlueprint(Tags::GAMEOBJCLASS_CHARACTER, "Pikeman");
		check("probe set up", a && palace && pike);
		if (!a || !palace || !pike) { printf("INTEL all 0\n"); return; }
		ServerGameObject* probe = server.spawnObject(pike, server.findObject(1030), palace->position + Vector3(25, 0, 0), Vector3(0, 0, 0));
		const uint32_t probeId = probe->id;
		StepServer(server, 105); // a think (every 2 s) and a bit
		const bool seen = a->picture.sightings.count(probeId) != 0;
		check("seen", seen && a->picture.sightings[probeId].fighter && a->picture.sightings[probeId].power > 0);
		StepServer(server, 105);
		const float threat = a->picture.threatNear(palace->position.x, palace->position.z, 100.0f);
		printf("INTEL the threat at A's palace with the Pikeman: %.1f (its power %.1f)\n", threat, seen ? a->picture.sightings[probeId].power : 0.0f);
		check("on the threat map", threat > 0.0f && a->board.get("intel.threat.home") > 0.0f);
		// gone from a place in sight: forgotten (A sees the place empty)
		server.deleteObject(probe);
		StepServer(server, 105);
		check("forgotten when gone from sight", !a->picture.sightings.count(probeId));
		// out in the field: a viewer of A's next to a Pikeman of B's (both standing still), far from
		// both palaces; the viewer goes home: the Pikeman is remembered where it was seen
		const Vector3 field = palace->position + Vector3(-120, 0, 120);
		auto still = [&](ServerGameObject* o) { o->stopMovement(); o->disableCount++; return o; };
		ServerGameObject* viewer = still(server.spawnObject(pike, server.findObject(1027), field, Vector3(0, 0, 0)));
		ServerGameObject* distant = still(server.spawnObject(pike, server.findObject(1030), field + Vector3(10, 0, 0), Vector3(0, 0, 0)));
		const uint32_t farId = distant->id;
		StepServer(server, 105);
		check("seen in the field", a->picture.sightings.count(farId) != 0);
		const float seenX = a->picture.sightings.count(farId) ? a->picture.sightings[farId].x : -1.0f;
		viewer->updatePosition(palace->position + Vector3(20, 0, 0));
		StepServer(server, 105);
		check("remembered out of sight", a->picture.sightings.count(farId) && a->picture.sightings[farId].x == seenX);
		const float threatField = a->picture.threatNear(field.x, field.z, 40.0f);
		printf("INTEL the threat in the field, out of sight: %.1f\n", threatField);
		check("its threat remembered", threatField > 0.0f);
		check("saved", server.saveGame(file.c_str()));
		sightingsSaved = a->picture.sightings.size();
		StepServer(server, 105);
		threatStraight = a->picture.threatNear(field.x, field.z, 40.0f);
		// it leaves unseen; the viewer comes back: forgotten
		server.deleteObject(distant);
		StepServer(server, 105);
		check("still remembered, unseen", a->picture.sightings.count(farId) != 0);
		viewer->updatePosition(field);
		StepServer(server, 105);
		check("forgotten when its place is seen again", !a->picture.sightings.count(farId));
	}
	{
		Server server;
		server.loadSaveGame("Save_Games\\wkbai intel test.sav");
		Government* a = server.ministries.governments.count(1027) ? server.ministries.governments[1027].get() : nullptr;
		check("picture loaded", a && a->picture.sightings.size() == sightingsSaved);
		StepServer(server, 105);
		ServerGameObject* palace = AIUtil::Palace(server.findObject(1027));
		const Vector3 field = palace ? palace->position + Vector3(-120, 0, 120) : Vector3();
		threatAfterLoad = a && palace ? a->picture.threatNear(field.x, field.z, 40.0f) : -1.0f;
		printf("INTEL the threat one think after the save: straight %.3f, loaded %.3f\n", threatStraight, threatAfterLoad);
		check("threat map after loading", threatAfterLoad == threatStraight);
	}
	// the same game twice with the ministries (Intelligence's threat map on a worker thread)
	{
		// (one server at a time: Server::instance)
		std::map<uint32_t, std::string> a, b;
		{
			Server first;
			MinistryGame(first, true, 5);
			StepServer(first, 2000);
			a = WorldState(first);
		}
		for (int i = 0; i < DEPT_COUNT; i++) { g_deptMs[i] = 0; g_deptThinks[i] = 0; }
		{
			Server on;
			MinistryGame(on, true, 5);
			StepServer(on, 2000);
			b = WorldState(on);
		}
		int differ = 0;
		for (const auto& [id, s] : a)
			if (!b.count(id) || b.at(id) != s)
				differ++;
		printf("INTEL the game with Intelligence: %d of %zu objects differ\n", differ, a.size());
		check("the same game twice", differ == 0 && a.size() == b.size());
		printf("INTEL profile: %s\n", Ministry::Ministries::profileText(2000).c_str());
	}
	printf("INTEL all %d\n", (int)all);
	fflush(stdout);
}

// The ministry AI's Strategist, War and Generals (needs the game data): 4 Peaks, Belisar the Hammer
// against Lady Elinor, both with the ministries, 25 minutes. No attack before the enemy's buildings are known (a scout, or seen otherwise); attacks are
// given as missions; the war layer sends no waves of its own; missions end. Saved at 12 minutes
// and loaded: the missions go on. Prints the missions every 2 minutes, "WAR <check> <0|1>" and
// "WAR all <0|1>".
void Test_MinistryWar()
{
	LoadBCP("data.bcp");
	bool all = true;
	auto check = [&](const char* what, bool ok) {
		printf("WAR %s %d\n", what, (int)ok);
		all &= ok;
	};
	using namespace Ministry;
	const int minutes = getenv("WKB_WAR_MINUTES") ? atoi(getenv("WKB_WAR_MINUTES")) : 25;
	const std::string file = PathToUtf8(g_userPath / "saved" / "Save_Games" / "wkbai war test.sav");
	std::map<uint32_t, size_t> missionsAtSave;
	std::set<std::string> kindsGiven;
	std::set<uint32_t> firstAttack;
	bool blindAttack = false;
	double given = 0, done = 0, failed = 0;
	int warLayerWaves = 0;
	g_settings["aiTrace"] = 1;
	{
		Server server;
		MinistryGame(server, true, 99);
		for (int m = 1; m <= minutes; m++) {
			StepServer(server, 3000);
			for (const auto& [id, g] : server.ministries.governments) {
				for (const Mission& mi : g->missions) {
					kindsGiven.insert(Mission::KindName(mi.kind));
					// each side's first attack: the enemy's buildings known by then (a scout went first, or
					// they were seen otherwise)
					const bool attack = mi.kind != Mission::SCOUT && mi.kind != Mission::DECOY && mi.kind != Mission::DEFEND;
					if (attack && firstAttack.insert(id).second)
						blindAttack |= g->board.get("intel." + std::to_string(mi.enemy) + ".buildings") <= 0.0;
				}
				if (m % 2 == 0 || m == minutes) {
					std::string list;
					for (Mission& mi : g->missions)
						list += " " + std::string(Mission::KindName(mi.kind)) + "#" + std::to_string(mi.id) + (mi.state == Mission::ASSEMBLING ? "(assembling " : "(out ")
							+ std::to_string(g->alive(mi).size()) + "/" + std::to_string(mi.want) + ")";
					printf("WAR t=%d min player %u: home %.0f (guard %.0f, %.0f free, power %.0f), in missions %.0f; given %.0f; enemy buildings known %.0f;%s\n", m, id,
						g->board.get("war.home"), g->board.get("war.guard"), g->board.get("war.available"), g->board.get("war.availablePower"),
						g->board.get("war.inMissions"), g->board.get("missions.given"), g->board.get("intel." + std::to_string(id == 1027 ? 1030 : 1027) + ".buildings"), list.c_str());
				}
			}
			if (m == 12) {
				check("saved", server.saveGame(file.c_str()));
				for (const auto& [id, g] : server.ministries.governments)
					missionsAtSave[id] = g->missions.size();
			}
		}
		for (const auto& [id, g] : server.ministries.governments) {
			given += g->board.get("missions.given");
			for (const auto& [key, v] : g->board.reports) {
				if (key.rfind("missions.", 0) == 0 && key.size() > 5 && key.compare(key.size() - 5, 5, ".done") == 0)
					done += v;
				if (key.rfind("missions.", 0) == 0 && key.size() > 7 && key.compare(key.size() - 7, 7, ".failed") == 0)
					failed += v;
			}
			ServerGameObject* p = server.findObject(id);
			warLayerWaves += p ? p->aiController.warWavesSent : 0;
			printf("WAR player %u at the end: %d characters' kills by its missions; reports:", id, 0);
			for (const auto& [key, v] : g->board.reports)
				if (key.rfind("missions.", 0) == 0)
					printf(" %s=%.0f", key.c_str(), v);
			printf("\n");
		}
	}
	std::string kinds;
	for (const std::string& k : kindsGiven)
		kinds += " " + k;
	printf("WAR missions given %.0f (kinds:%s), done %.0f, failed %.0f; the war layer's own waves %d\n", given, kinds.c_str(), done, failed, warLayerWaves);
	// (a scout goes only while nothing of the enemy is known: in a game where the other side's buildings
	// were seen first, or no soldier was free, there is none; since the farms level their ground, 4 Peaks'
	// seed 99 plays such a game)
	printf("WAR scouts given %d\n", (int)kindsGiven.count("scout"));
	check("no attack before the enemy is known", !firstAttack.empty() && !blindAttack);
	check("attacks given", kindsGiven.count("raze") || kindsGiven.count("palace") || kindsGiven.count("raid"));
	check("missions end", done + failed > 0);
	check("no waves from the war layer", warLayerWaves == 0);
	g_settings["aiTrace"] = 0;
	{
		Server server;
		server.loadSaveGame("Save_Games\\wkbai war test.sav");
		bool same = server.ministries.governments.size() == missionsAtSave.size();
		for (const auto& [id, g] : server.ministries.governments)
			same &= missionsAtSave[id] == g->missions.size();
		check("missions through a save", same);
		StepServer(server, 6000);
		check("going on after loading", server.ministries.governments.size() == 2);
	}
	printf("WAR all %d\n", (int)all);
	fflush(stdout);
}

// The ministry AI's tactics (needs the game data): 4 Peaks, the two Elite Generals with the
// ministries, 12 minutes. Then for Lady Elinor (1030), with resources to spend: Intelligence told
// of 12 Knights of Belisar's, War trains by counters (heavy infantry against heavy cavalry);
// told of 12 Archers, what it trains then. A Monastic Knight beside a wounded Knight of its own
// heals it; a High Priestess beside an enemy Pikeman curses or entangles it (if their conditions
// let them). Prints "TACTICS <check> <0|1>" and "TACTICS all <0|1>".
void Test_MinistryTactics()
{
	LoadBCP("data.bcp");
	bool all = true;
	auto check = [&](const char* what, bool ok) {
		printf("TACTICS %s %d\n", what, (int)ok);
		all &= ok;
	};
	using namespace Ministry;
	g_settings["aiTrace"] = 1;
	Server server;
	MinistryGame(server, true, 99);
	StepServer(server, 36000);
	Government* g = server.ministries.governments.count(1030) ? server.ministries.governments[1030].get() : nullptr;
	ServerGameObject* me = server.findObject(1030);
	ServerGameObject* palace = me ? AIUtil::Palace(me) : nullptr;
	check("set up", g && me && palace);
	if (!g || !me || !palace) { printf("TACTICS all 0\n"); return; }
	const GameSet* gs = server.gameSet.get();
	// resources to spend, and room under the population limit (at 12 minutes the scripts' peasants
	// fill it: "Have not reached Population Limit" refuses every Spawn)
	const int popLimit = gs->items.names.getIndex("Total Population Count"), popCount = gs->items.names.getIndex("Population Count");
	auto rich = [&]() {
		me->setItem(Tags::PDITEM_FOOD, 5000);
		me->setItem(Tags::PDITEM_WOOD, 5000);
		me->setItem(Tags::PDITEM_GOLD, 5000);
		if (popLimit >= 0 && popCount >= 0)
			me->setItem(popLimit, me->getItem(popCount) + 50);
	};
	printf("TACTICS population %.0f of %.0f at 12 min\n", popCount >= 0 ? me->getItem(popCount) : -1.0f, popLimit >= 0 ? me->getItem(popLimit) : -1.0f);
	// what the buildings were ordered to train: their Spawn orders' types
	auto buildingsOf = [&]() {
		std::vector<ServerGameObject*> list, todo{ me };
		while (!todo.empty()) {
			ServerGameObject* o = todo.back();
			todo.pop_back();
			for (const auto& [t, children] : o->children)
				for (CommonGameObject* c : children) {
					auto* b = (ServerGameObject*)c;
					if (b->deleted) continue;
					todo.push_back(b);
					if (b->blueprint->bpClass == Tags::GAMEOBJCLASS_BUILDING)
						list.push_back(b);
				}
		}
		return list;
	};
	auto training = [&]() {
		std::map<int, int> byClass;
		std::string list;
		for (ServerGameObject* b : buildingsOf())
			for (Order& order : b->orderConfig.orders)
				for (auto& task : order.tasks)
					if (auto* spawn = dynamic_cast<SpawnTask*>(task.get()); spawn && spawn->toSpawn) {
						byClass[AIUtil::TypeClass(spawn->toSpawn)]++;
						list += " " + spawn->toSpawn->name;
					}
		return std::make_pair(byClass, list);
	};
	auto clearTraining = [&]() {
		for (ServerGameObject* b : buildingsOf())
			b->orderConfig.cancelAllOrders();
		StepServer(server, 1); // (the cancelled orders go at the next step)
	};
	auto pretend = [&](int cls, const char* type) {
		g->picture.sightings.clear();
		for (int i = 0; i < 12; i++) {
			Sighting s;
			s.id = 900000 + i; s.player = 1027; s.fighter = true; s.cls = (uint8_t)cls; s.power = 100; s.x = 300; s.z = 300; s.seen = g->now(); s.type = type;
			g->picture.sightings[s.id] = s;
		}
	};
	clearTraining();
	rich();
	pretend(3, "Knight");
	{
		int buildings = 0, spawners = 0;
		for (ServerGameObject* b : buildingsOf()) {
			buildings++;
			for (const Command* cmd : b->blueprint->offeredCommands)
				if (gs->commands.getString(cmd).rfind("Spawn ", 0) == 0) {
					spawners++;
					printf("TACTICS %s %u offers %s (busy %d, can %d)\n", b->blueprint->name.c_str(), b->id, gs->commands.getString(cmd).c_str(),
						(int)(b->orderConfig.getCurrentOrder() != nullptr), (int)cmd->canBeExecuted(b));
					break;
				}
		}
		const auto trainers = AIUtil::Trainers(me, false);
		printf("TACTICS %d buildings, %d offer a Spawn; trainers now %zu; food %.0f, time %.0f\n", buildings, spawners, trainers.size(), me->getItem(Tags::PDITEM_FOOD), g->now());
		for (const auto& [b, names] : trainers) {
			std::string n;
			for (const auto& s : names) n += " " + s;
			printf("TACTICS trainer %s:%s\n", b->blueprint->name.c_str(), n.c_str());
		}
	}
	const int against = TrainByCounters(*g);
	auto [knightsMix, knightsList] = training();
	printf("TACTICS against 12 Knights: %d ordered:%s\n", against, knightsList.c_str());
	int heavyInfantry = knightsMix[1], others = 0;
	for (const auto& [cls, n] : knightsMix)
		if (cls != 1) others += n;
	check("trains by counters", against > 0);
	check("heavy infantry against heavy cavalry", heavyInfantry > 0 && heavyInfantry >= others);
	clearTraining();
	rich();
	pretend(2, "Archer");
	const int against2 = TrainByCounters(*g);
	auto [archersMix, archersList] = training();
	printf("TACTICS against 12 Archers: %d ordered:%s\n", against2, archersList.c_str());
	check("trains against archers too", against2 > 0);
	clearTraining();

	// abilities
	const GameObjBlueprint* monk = gs->findBlueprint(Tags::GAMEOBJCLASS_CHARACTER, "Monastic Knight");
	const GameObjBlueprint* knight = gs->findBlueprint(Tags::GAMEOBJCLASS_CHARACTER, "Knight");
	const GameObjBlueprint* priestess = gs->findBlueprint(Tags::GAMEOBJCLASS_CHARACTER, "High Priestess");
	const GameObjBlueprint* pike = gs->findBlueprint(Tags::GAMEOBJCLASS_CHARACTER, "Pikeman");
	check("ability types found", monk && knight && priestess && pike);
	if (monk && knight && priestess && pike) {
		const Vector3 at = palace->position + Vector3(30, 0, 30);
		ServerGameObject* healer = server.spawnObject(monk, me, at, Vector3(0, 0, 0));
		ServerGameObject* hurt = server.spawnObject(knight, me, at + Vector3(4, 0, 0), Vector3(0, 0, 0));
		const int hp = gs->items.names.getIndex("Hit Points"), cap = gs->items.names.getIndex("Hit Point Capacity");
		if (hp >= 0 && cap >= 0)
			hurt->setItem(hp, hurt->getItem(cap) * 0.3f);
		// (a caster's Charge Bar fills over time: a new one has none, and "Have Enough Charge" waits)
		const int charge = gs->items.names.getIndex("Charge Bar");
		if (charge >= 0)
			healer->setItem(charge, 1000);
		std::map<uint64_t, float> ready;
		const int healed = UseAbilities(*g, { healer, hurt }, ready);
		Order* o = healer->orderConfig.getCurrentOrder();
		printf("TACTICS the Monastic Knight beside a Knight at 30%%: %d abilities used, its order now %s\n", healed,
			o ? gs->orders.getString(o->blueprint->bpid).c_str() : "none");
		check("heals a wounded friend", healed > 0);
		check("not again within 8 s", UseAbilities(*g, { healer, hurt }, ready) == 0);
		const Vector3 at2 = palace->position + Vector3(-30, 0, 30);
		ServerGameObject* caster = server.spawnObject(priestess, me, at2, Vector3(0, 0, 0));
		if (charge >= 0)
			caster->setItem(charge, 1000);
		server.spawnObject(pike, server.findObject(1027), at2 + Vector3(8, 0, 0), Vector3(0, 0, 0));
		std::map<uint64_t, float> ready2;
		const int cast = UseAbilities(*g, { caster }, ready2);
		Order* o2 = caster->orderConfig.getCurrentOrder();
		printf("TACTICS the High Priestess beside an enemy Pikeman: %d abilities used, its order now %s\n", cast,
			o2 ? gs->orders.getString(o2->blueprint->bpid).c_str() : "none");
		check("casts at an enemy", cast > 0);
	}
	g_settings["aiTrace"] = 0;
	printf("TACTICS all %d\n", (int)all);
	fflush(stdout);
}

// The ministry AI's Diplomacy (needs the game data): 4 Peaks, Belisar (1027) and Lady Elinor
// (1030), diplomacy allowed. A tribute of 2000 food from Belisar: Elinor's gratitude, and her
// trust fades by half in its half-life; she offers him peace. Neutral, then units of hers lost to
// him: a grudge (counting double while she trusted him), and she declares war. A real kill: a
// Pikeman of his kills a peasant of hers: in her ledger. The ledger through a save.
// Prints "DIPLO <check> <0|1>" and "DIPLO all <0|1>".
void Test_MinistryDiplomacy()
{
	LoadBCP("data.bcp");
	bool all = true;
	auto check = [&](const char* what, bool ok) {
		printf("DIPLO %s %d\n", what, (int)ok);
		all &= ok;
	};
	using namespace Ministry;
	g_settings["aiTrace"] = 1030;
	Server server;
	MinistryGame(server, true, 99, true);
	StepServer(server, 500);
	ServerGameObject* belisar = server.findObject(1027), * elinor = server.findObject(1030);
	Government* g = server.ministries.governments.count(1030) ? server.ministries.governments[1030].get() : nullptr;
	const GameSet* gs = server.gameSet.get();
	const int allied = gs->diplomaticStatuses.names.getIndex("Allied"), neutral = gs->diplomaticStatuses.names.getIndex("Neutral"),
		enemy = gs->diplomaticStatuses.names.getIndex("Enemy");
	check("set up", belisar && elinor && g && neutral >= 0 && enemy >= 0);
	if (!belisar || !elinor || !g) { printf("DIPLO all 0\n"); return; }
	const int disallow = gs->items.names.getIndex("Disallow Diplomacy");
	printf("DIPLO Disallow Diplomacy %.0f; the status between them %s\n", disallow >= 0 ? server.getLevel()->getItem(disallow) : -1.0f,
		gs->diplomaticStatuses.names.getString(server.getDiplomaticStatus(belisar, elinor)).c_str());
	// gratitude
	const int food = gs->items.names.getIndex("Food");
	belisar->setItem(food, 5000);
	server.giveTribute(belisar, elinor, food, 2000);
	const float t0 = g->trust(1027);
	printf("DIPLO after a tribute of 2000: trust %.0f\n", t0);
	check("gratitude", t0 > 1000.0f);
	StepServer(server, 700); // 14 s: a Diplomacy think or more
	const int offer = server.getDiplomaticOffer(elinor, belisar);
	const int status1 = server.getDiplomaticStatus(belisar, elinor);
	printf("DIPLO 14 s later: her offer %d, the status %s\n", offer, gs->diplomaticStatuses.names.getString(status1).c_str());
	check("offers peace", offer == neutral || status1 == neutral || status1 == allied);
	// the fading on its own: a ledger of a player that doesn't exist (no fighting changes it)
	const float halfLife = 600.0f * (1.5f - g->personality.friendliness);
	g->ledgerOf(99999).received = 1000.0;
	const float before = g->trust(99999);
	StepServer(server, (int)(halfLife * 50.0f));
	const float after = g->trust(99999);
	printf("DIPLO trust %.0f, one half-life (%.0f s) later %.0f\n", before, halfLife, after);
	check("fades by half", std::abs(after - before * 0.5f) < before * 0.05f);
	// a grudge
	server.setDiplomaticStatus(elinor, belisar, neutral, elinor);
	const GameObjBlueprint* peasantBp = gs->findBlueprint(Tags::GAMEOBJCLASS_CHARACTER, "Peasant");
	const GameObjBlueprint* pikeBp = gs->findBlueprint(Tags::GAMEOBJCLASS_CHARACTER, "Pikeman");
	ServerGameObject* palace = AIUtil::Palace(elinor);
	ServerGameObject* victim = server.spawnObject(peasantBp, elinor, palace->position + Vector3(30, 0, 0), Vector3(0, 0, 0));
	const Vector3 madeAt = victim->position;
	for (int i = 0; i < 40; i++)
		server.ministries.onLoss(elinor, belisar, victim);
	const Ledger& l = g->ledgerOf(1027);
	printf("DIPLO 40 peasants lost to him: trust %.0f, grudge %.0f, %d betrayals\n", g->trust(1027), l.lost, l.betrayals);
	check("a grudge", g->trust(1027) < 0.0f);
	check("betrayal while trusted", l.betrayals >= 1);
	StepServer(server, 3600); // 72 s: past her last act's minute and a think
	const int status2 = server.getDiplomaticStatus(belisar, elinor);
	printf("DIPLO then the status %s\n", gs->diplomaticStatuses.names.getString(status2).c_str());
	check("declares war", status2 == enemy);
	check("the Strategist sees the grudge", g->board.get("trust.1027") < 0.0);
	// a real kill: one of her working peasants, at one blow (the peasant made for the losses above,
	// held in place, was never hit; before 2026-11-13 this check counted another kill)
	const int killsBefore = g->ledgerOf(1027).kills;
	std::vector<ServerGameObject*> hers = AIUtil::Peasants(elinor);
	ServerGameObject* worker = nullptr;
	for (ServerGameObject* p : hers)
		if (p != victim && p->isInteractable()) { worker = p; break; }
	// (none left when Belisar's army has killed them all: a new one at her palace, the one made for
	// the losses above may be dead by now)
	printf("DIPLO a working peasant of hers %d\n", (int)(worker != nullptr));
	if (worker)
		victim = worker;
	else {
		ServerGameObject* home = AIUtil::Palace(elinor);
		victim = server.spawnObject(peasantBp, elinor, (home ? home->position : madeAt) + Vector3(30, 0, 10), Vector3(0, 0, 0));
	}
	// (a Spearman: a Pikeman going for peasants switched between them and never struck, 60 s)
	const GameObjBlueprint* spearBp = gs->findBlueprint(Tags::GAMEOBJCLASS_CHARACTER, "Spearman");
	ServerGameObject* pike = server.spawnObject(spearBp ? spearBp : pikeBp, belisar, victim->position + Vector3(3, 0, 0), Vector3(0, 0, 0));
	if (const int hp = gs->items.names.getIndex("Hit Points"); hp >= 0) {
		victim->setItem(hp, 1.0f); // (one blow)
		pike->setItem(hp, 100000.0f); // (her defenders come for it: it lives, and whatever of hers it kills counts)
	}
	// The blow itself is the game's (an attack package notes its sender, UnitHistory::hit): here it is
	// recorded directly and the peasant dies through the game's own path (terminate ->
	// UnitHistory::terminated, the kill's credit -> the ledger). A real fight was tried: a walking
	// peasant got away from an attacker of the same speed (peasants walk home when no help can win).
	victim->lastHitBy = pike->id;
	victim->lastHitTime = server.timeManager.currentTime;
	victim->setItem(gs->items.names.getIndex("Hit Points"), 0.0f);
	victim->terminate();
	StepServer(server, 5);
	printf("DIPLO the Pikeman's kill: victim gone %d, kills in her ledger %d -> %d\n", (int)!server.findObject(victim->id), killsBefore, g->ledgerOf(1027).kills);
	check("a real kill in the ledger", g->ledgerOf(1027).kills > killsBefore);
	// through a save
	const std::string file = PathToUtf8(g_userPath / "saved" / "Save_Games" / "wkbai diplomacy test.sav");
	check("saved", server.saveGame(file.c_str()));
	const double lost = g->ledgerOf(1027).lost;
	g_settings["aiTrace"] = 0;
	{
		Server again;
		again.loadSaveGame("Save_Games\\wkbai diplomacy test.sav");
		Government* h = again.ministries.governments.count(1030) ? again.ministries.governments[1030].get() : nullptr;
		check("the ledger through a save", h && std::abs(h->ledgerOf(1027).lost - lost) < 0.01);
	}
	(void)allied;
	printf("DIPLO all %d\n", (int)all);
	fflush(stdout);
}

// The ministry AI's economy (needs the game data): 4 Peaks, Belisar against Lady Elinor, 20
// minutes, without the ministries and with them (the first two games of the process). Per player
// every 5 minutes: peasants, soldiers, gold; with them also the cap, the gold miners, research and
// repairs. Checks: the peasants within the cap; gold miners sent when gold didn't come in; more
// gold than without. Prints "ECON <check> <0|1>" and "ECON all <0|1>".
struct EconCensus { int peasants = 0, soldiers = 0; float gold = 0, food = 0, wood = 0; };
static EconCensus EconCount(Server& server, uint32_t id)
{
	EconCensus c;
	ServerGameObject* p = server.findObject(id);
	if (!p)
		return c;
	c.gold = p->getItem(Tags::PDITEM_GOLD);
	c.food = p->getItem(Tags::PDITEM_FOOD);
	c.wood = p->getItem(Tags::PDITEM_WOOD);
	std::vector<ServerGameObject*> todo{ p };
	while (!todo.empty()) {
		ServerGameObject* o = todo.back();
		todo.pop_back();
		for (const auto& [t, children] : o->children)
			for (CommonGameObject* ch : children) {
				auto* u = (ServerGameObject*)ch;
				if (u->deleted) continue;
				todo.push_back(u);
				if (AIUtil::IsPeasant(u)) c.peasants++;
				else if (AIUtil::IsFighter(u)) c.soldiers++;
			}
	}
	return c;
}

void Test_MinistryEconomy()
{
	LoadBCP("data.bcp");
	bool all = true;
	auto check = [&](const char* what, bool ok) {
		printf("ECON %s %d\n", what, (int)ok);
		all &= ok;
	};
	using namespace Ministry;
	const int minutes = getenv("WKB_ECON_MINUTES") ? atoi(getenv("WKB_ECON_MINUTES")) : 20;
	std::map<uint32_t, EconCensus> without, with;
	{
		Server server;
		MinistryGame(server, false, 7);
		// (WKB_ECON_FIRST: this first game's minutes, for finding what an earlier game leaves behind)
		const int firstMinutes = getenv("WKB_ECON_FIRST") ? atoi(getenv("WKB_ECON_FIRST")) : minutes;
		for (int m = 5; m <= firstMinutes; m += 5) {
			StepServer(server, 15000);
			for (uint32_t id : { 1027u, 1030u }) {
				const EconCensus c = EconCount(server, id);
				printf("ECON without t=%d min player %u: %d peasants, %d fighters, gold %.0f, food %.0f, wood %.0f\n", m, id, c.peasants, c.soldiers, c.gold, c.food, c.wood);
				without[id] = c;
			}
		}
	}
	g_settings["aiTrace"] = 1027;
	bool withinCap = true, goldSent = false, goldInTheWay = false;
	{
		Server server;
		MinistryGame(server, true, 7);
		for (int m = 5; m <= minutes; m += 5) {
			if (getenv("WKB_ECON_HASH") && m == 5)
				for (int s = 0; s < 30; s++) {
					StepServer(server, 500);
					const auto st = WorldState(server);
					uint64_t h = 1469598103934665603ull;
					for (const auto& [id, str] : st) {
						h ^= id; h *= 1099511628211ull;
						for (char ch : str) { h ^= (uint8_t)ch; h *= 1099511628211ull; }
					}
					printf("ECONHASH t=%d %016llx %zu\n", (s + 1) * 10, (unsigned long long)h, st.size());
					if (getenv("WKB_ECON_DUMP") && (s + 1) * 10 == atoi(getenv("WKB_ECON_DUMP")))
						for (const auto& [id, str] : st)
							printf("ECONDUMP %u %s\n", id, str.c_str());
				}
			else
				StepServer(server, 15000);
			for (uint32_t id : { 1027u, 1030u }) {
				const EconCensus c = EconCount(server, id);
				Government* g = server.ministries.governments[id].get();
				const int cap = (int)g->board.get("labour.cap");
				printf("ECON with t=%d min player %u: %d peasants (cap %d), %d fighters, gold %.0f (%+.0f a minute, %.0f miners), food %.0f, wood %.0f; research %.0f, repaired %.0f, missions %.0f\n",
					m, id, c.peasants, cap, c.soldiers, c.gold, g->board.get("economy.gold.income"), g->board.get("labour.goldMiners"), c.food, c.wood,
					g->board.get("research.ordered"), g->board.get("builders.repaired"), g->board.get("missions.given"));
				withinCap &= c.peasants <= cap + 3; // (those in training when the cap was reached)
				goldSent |= g->board.get("labour.goldSent") > 0;
				goldInTheWay |= g->board.get("economy.goldNeeded") > 0;
				with[id] = c;
			}
		}
	}
	g_settings["aiTrace"] = 0;
	check("peasants within the cap", withinCap);
	// (since 2026-11-14 only when a fighter that can be trained costs more gold than there is)
	printf("ECON gold in the way %d, miners sent %d\n", (int)goldInTheWay, (int)goldSent);
	check("gold miners only when gold is in the way", !goldSent || goldInTheWay);
	float goldWithout = 0, goldWith = 0;
	int soldiersWithout = 0, soldiersWith = 0;
	for (uint32_t id : { 1027u, 1030u }) {
		goldWithout += without[id].gold; goldWith += with[id].gold;
		soldiersWithout += without[id].soldiers; soldiersWith += with[id].soldiers;
	}
	printf("ECON at %d min: gold %.0f without, %.0f with; fighters %d without, %d with\n", minutes, goldWithout, goldWith, soldiersWithout, soldiersWith);
	printf("ECON all %d\n", (int)all);
	fflush(stdout);
}

// The ministry AI from Lua, and its difficulty (needs the game data). A Lua AI of the test's own
// (saved\LuaAI\MinistryProbe.lua in the test profile) for Belisar reads a report, sets War's
// weight and gives a raze mission; with "aiReaction" 200 the departments think half as often.
// Prints "LUAMIN <check> <0|1>" and "LUAMIN all <0|1>".
void Test_MinistryLua()
{
	LoadBCP("data.bcp");
	bool all = true;
	auto check = [&](const char* what, bool ok) {
		printf("LUAMIN %s %d\n", what, (int)ok);
		all &= ok;
	};
	using namespace Ministry;
	{
		const auto dir = g_userPath / "saved" / "LuaAI";
		std::error_code ec;
		std::filesystem::create_directories(dir, ec);
		std::ofstream f(dir / "MinistryProbe.lua", std::ios::binary);
		f << "food = -1\nweightSet = false\ngiven = nil\nmissions = 0\n"
			"function think(ai)\n"
			"  food = ai.report(\"economy.food\") or -2\n"
			"  if not weightSet then weightSet = ai.setWeight(\"war\", 3.0) end\n"
			"  missions = #ai.missions()\n"
			"  if not given and ai.time() > 30 then\n"
			"    local b = ai.buildingsNear(300, 300, 5000)\n"
			"    if #b > 0 then given = ai.giveMission(\"raze\", b[1], 6) end\n"
			"  end\n"
			"end\n"
			"function census() return \"food \" .. tostring(food) .. \" weight \" .. tostring(weightSet) .. \" mission \" .. tostring(given) .. \" missions \" .. tostring(missions) end\n";
	}
	auto game = [&](Server& server, const char* script, int reaction) {
		server.loadSaveGame("Save_Games\\2 Player 4 Peaks.lvl");
		g_settings["simSeed"] = 99;
		QuickSkirmishMenu skirmish;
		skirmish.changeSettingsForLevel("2 Player 4 Peaks.lvl");
		std::vector<QuickSkirmishMenu::Slot> slots(2);
		const char* generals[2] = { "Belisar The Hammer Kingmaker", "Lady Elinor Kingmaker" };
		for (int i = 0; i < 2; i++) { slots[i].type = QuickSkirmishMenu::Slot::AI; slots[i].general = generals[i]; }
		slots[0].aiScript = script;
		skirmish.setSlots(slots);
		skirmish.setSetting("aiMinistries", 1);
		skirmish.setSetting("aiReaction", reaction);
		server.timeManager.unpause();
		server.timeManager.stepMs = 20;
		skirmish.applySettings(server);
	};
	{
		Server server;
		game(server, "MinistryProbe", 100);
		StepServer(server, 3000);
		ServerGameObject* belisar = server.findObject(1027);
		Government* g = server.ministries.governments.count(1027) ? server.ministries.governments[1027].get() : nullptr;
		const std::string census = belisar ? belisar->aiController.luaCensus() : std::string();
		printf("LUAMIN the Lua AI's census: %s\n", census.c_str());
		check("the Lua AI runs with the government", g && census.find("food ") != std::string::npos);
		check("ai.report", census.find("food -") == std::string::npos && census.find("food ") != std::string::npos);
		check("ai.setWeight", g && g->board.weights[WAR] == 3.0f);
		bool luaMission = false;
		if (g)
			for (const Mission& m : g->missions)
				luaMission |= m.kind == Mission::RAZE && m.want == 6;
		check("ai.giveMission", census.find("mission nil") == std::string::npos && (luaMission || (g && g->board.get("missions.raze.done") + g->board.get("missions.raze.failed") > 0)));
	}
	{
		Server server;
		game(server, "", 200);
		StepServer(server, 3000);
		Government* g = server.ministries.governments.count(1027) ? server.ministries.governments[1027].get() : nullptr;
		Department* economy = g ? g->department(ECONOMY) : nullptr;
		printf("LUAMIN aiReaction 200: economy thought %llu times in 60 s (every 2 s at 100)\n", economy ? (unsigned long long)economy->thinks : 0ull);
		check("aiReaction slows them", economy && economy->thinks >= 14 && economy->thinks <= 16);
	}
	printf("LUAMIN all %d\n", (int)all);
	fflush(stdout);
}

// The ministry AI's defence (needs the game data): 4 Peaks, Belisar (1027) against Lady Elinor, the
// ministries on. 12 Spearmen of Belisar's at home. Two Pikemen of Elinor's 60 m from his palace: a
// defence mission with some of the Spearmen, not all; the war layer sends none; done when they're
// gone, and the Spearmen are free again. 40 Pikemen at the palace: all go, and the missions are
// recalled. Prints "DEFENCE <check> <0|1>" and "DEFENCE all <0|1>".
void Test_MinistryDefence()
{
	LoadBCP("data.bcp");
	bool all = true;
	auto check = [&](const char* what, bool ok) {
		printf("DEFENCE %s %d\n", what, (int)ok);
		all &= ok;
	};
	using namespace Ministry;
	g_settings["aiTrace"] = 1027;
	Server server;
	MinistryGame(server, true, 99);
	StepServer(server, 250);
	ServerGameObject* me = server.findObject(1027), * enemy = server.findObject(1030);
	Government* g = server.ministries.governments.count(1027) ? server.ministries.governments[1027].get() : nullptr;
	ServerGameObject* palace = me ? AIUtil::Palace(me) : nullptr;
	ServerGameObject* homeArmy = me ? AIUtil::HomeArmy(me) : nullptr;
	const GameSet* gs = server.gameSet.get();
	const GameObjBlueprint* spear = gs->findBlueprint(Tags::GAMEOBJCLASS_CHARACTER, "Spearman");
	const GameObjBlueprint* pike = gs->findBlueprint(Tags::GAMEOBJCLASS_CHARACTER, "Pikeman");
	check("set up", me && enemy && g && palace && homeArmy && spear && pike);
	if (!(me && enemy && g && palace && homeArmy && spear && pike)) { printf("DEFENCE all 0\n"); return; }
	g->board.budgets["war.defence"] = 1.0; // (War's defence: by default the war layer's)
	for (int i = 0; i < 12; i++)
		server.spawnObject(spear, homeArmy, palace->position + Vector3(15.0f + (i % 4) * 3.0f, 0, 15.0f + (i / 4) * 3.0f), Vector3(0, 0, 0));
	StepServer(server, 200);
	auto defences = [&]() {
		int n = 0, units = 0;
		for (Mission& m : g->missions)
			if (m.kind == Mission::DEFEND) { n++; units += (int)g->alive(m).size(); }
		return std::make_pair(n, units);
	};
	check("no threat, no defence", defences().first == 0);
	const double freeBefore = g->board.get("war.home");
	printf("DEFENCE fighters free at home: %.0f\n", freeBefore);
	// a raid
	std::vector<ServerGameObject*> raiders;
	for (int i = 0; i < 2; i++) {
		raiders.push_back(server.spawnObject(pike, enemy, palace->position + Vector3(-60.0f, 0, 3.0f * i), Vector3(0, 0, 0)));
		raiders.back()->setItem(gs->items.names.getIndex("Hit Points"), 30.0f); // (the test is the orders, not the fight)
	}
	// they stand (Elinor's scripts walked them home, the defence after them; a disabled unit can't
	// be hit): no orders, no walk, every second
	auto standStill = [&]() {
		for (ServerGameObject* r : raiders)
			if (server.findObject(r->id) && !(r->flags & ServerGameObject::fTerminated)) {
				r->orderConfig.cancelAllOrders();
				r->stopMovement();
			}
	};
	auto stepStill = [&](int steps) {
		for (int s = 0; s < steps; s += 50) {
			standStill();
			StepServer(server, std::min(50, steps - s));
		}
	};
	stepStill(200);
	auto [n, units] = defences();
	printf("DEFENCE two Pikemen at the palace: %d defence missions, %d defenders; the war layer's defences %zu\n", n, units, me->aiController.warDefences.size());
	check("a defence", n == 1 && units >= 1);
	check("not all of them", units < 12);
	check("the war layer sends none", me->aiController.warDefences.empty());
	for (int s = 0; s < 6; s++) {
		stepStill(250);
		ServerGameObject* d = nullptr;
		for (Mission& m : g->missions)
			if (m.kind == Mission::DEFEND && !g->alive(m).empty())
				d = g->alive(m).front();
		printf("DEFENCE t=%.0f defender %s (%.1f, %.1f) moving %d; raiders (%.1f, %.1f) moving %d, (%.1f, %.1f)\n", server.timeManager.currentTime,
			d ? "" : "none", d ? d->position.x : 0, d ? d->position.z : 0, d ? (int)d->movement.isMoving() : 0,
			raiders[0]->position.x, raiders[0]->position.z, (int)raiders[0]->movement.isMoving(), raiders[1]->position.x, raiders[1]->position.z);
	}
	for (Mission& m : g->missions)
		if (m.kind == Mission::DEFEND)
			for (ServerGameObject* u : g->alive(m)) {
				Order* o = u->orderConfig.getCurrentOrder();
				Task* t = o ? o->getCurrentTask() : nullptr;
				ServerGameObject* tg = t ? t->target.get() : nullptr;
				printf("DEFENCE defender %u at (%.0f, %.0f): %s / %s -> %u %s, parent %s\n", u->id, u->position.x, u->position.z,
					o ? gs->orders.getString(o->blueprint->bpid).c_str() : "-", t ? gs->tasks.getString(t->blueprint).c_str() : "-",
					tg ? tg->id : 0, tg ? tg->blueprint->name.c_str() : "", u->getParent() ? u->getParent()->blueprint->name.c_str() : "?");
			}
	for (ServerGameObject* r : raiders)
		if (server.findObject(r->id))
			printf("DEFENCE raider %u at (%.0f, %.0f), hit points %.0f\n", r->id, r->position.x, r->position.z, r->getItem(gs->items.names.getIndex("Hit Points")));
	for (int i = 0; i < 60 && defences().first > 0; i++)
		stepStill(50);
	auto [n2, units2] = defences();
	int raidersAlive = 0;
	for (ServerGameObject* r : raiders)
		raidersAlive += server.findObject(r->id) && !(r->flags & ServerGameObject::fTerminated) ? 1 : 0;
	printf("DEFENCE later: %d raiders alive, %d defence missions; done %.0f\n", raidersAlive, n2, g->board.get("missions.defend.done"));
	check("done when they're gone", n2 == 0 && g->board.get("missions.defend.done") >= 1);
	StepServer(server, 200);
	printf("DEFENCE fighters free at home again: %.0f\n", g->board.get("war.home"));
	check("free again", g->board.get("war.home") >= freeBefore - 2);
	// an attack on the palace
	for (int i = 0; i < 40; i++)
		server.spawnObject(pike, enemy, palace->position + Vector3(-50.0f - (i % 8) * 3.0f, 0, -12.0f + (i / 8) * 3.0f), Vector3(0, 0, 0));
	StepServer(server, 200);
	auto [n3, units3] = defences();
	printf("DEFENCE forty Pikemen at the palace: %d defence missions, %d defenders, free left %.0f, recall %.0f\n", n3, units3, g->board.get("war.home"), g->board.get("war.recall"));
	check("all go to the palace", n3 >= 1 && g->board.get("war.home") == 0);
	check("the missions are recalled", g->board.get("war.recall") > 0);
	g_settings["aiTrace"] = 0;
	printf("DEFENCE all %d\n", (int)all);
	fflush(stdout);
}

void Test_Determinism()
{
	LoadBCP("data.bcp");
	const uint64_t a = DeterminismRun(12345);
	const uint64_t b = DeterminismRun(12345);
	DeterminismRun(777);
	printf("DETERMINISM same %d\n", (int)(a == b));
	fflush(stdout);
}

// The map editor's BCM writer: every map of data.bcp read and written again unchanged must give
// the same bytes; then a height changed must read back. Prints "BCM <map> identical <0|1>" and
// "BCM edited read back <0|1>".
void Test_BCMRoundTrip()
{
	LoadBCP("data.bcp");
	std::unique_ptr<std::vector<std::string>> dirs(ListDirectories("Maps"));
	int maps = 0, identical = 0;
	std::string firstMap;
	for (const std::string& dir : *dirs) {
		const std::string bcm = "Maps\\" + dir + "\\" + dir + ".bcm";
		if (!FileExists(bcm.c_str()))
			continue;
		Terrain t;
		t.readBCM(bcm.c_str());
		const std::filesystem::path out = std::filesystem::temp_directory_path() / "wkbre2-bcm-test.bcm";
		t.writeBCM(out);
		std::ifstream f(out, std::ios::binary);
		std::vector<uint8_t> written((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
		const bool same = written == t.bcmData;
		maps++;
		identical += same;
		if (!same)
			printf("BCM %s identical 0 (%zu vs %zu bytes)\n", bcm.c_str(), written.size(), t.bcmData.size());
		if (firstMap.empty())
			firstMap = bcm;
	}
	printf("BCM maps %d identical %d\n", maps, identical);
	if (!firstMap.empty()) {
		Terrain t;
		t.readBCM(firstMap.c_str());
		const int x = t.width / 2, z = t.height / 3;
		const uint8_t before = t.vertices[z * (t.width + 1) + x];
		t.vertices[z * (t.width + 1) + x] = (uint8_t)(before + 37);
		const std::filesystem::path out = std::filesystem::temp_directory_path() / "wkbre2-bcm-test.bcm";
		t.writeBCM(out);
		// read it back through a copy in the user folder's saved\ (LoadFile looks there)
		const std::filesystem::path saved = g_userPath / "saved" / "wkbre2-bcm-test.bcm";
		std::filesystem::create_directories(saved.parent_path());
		std::filesystem::copy_file(out, saved, std::filesystem::copy_options::overwrite_existing);
		Terrain back;
		back.readBCM("wkbre2-bcm-test.bcm");
		const bool ok = back.vertices[z * (back.width + 1) + x] == (uint8_t)(before + 37) && back.width == t.width;
		printf("BCM edited read back %d\n", (int)ok);

		// a tile painted with a texture the map's tables lack (when there is one), turned and
		// flipped: the tables grow, the tile and its neighbour read back as written
		TerrainTexture* paint = nullptr;
		for (const auto& [name, group] : t.texDb.groups)
			for (const auto& [id, tex] : group->textures)
				if (!paint || (std::find(t.bcmGroups.begin(), t.bcmGroups.end(), group.get()) == t.bcmGroups.end()
					&& std::find(t.bcmIds.begin(), t.bcmIds.end(), id) == t.bcmIds.end()))
					paint = tex.get();
		TerrainTile* tile = t.getTileMutable(x, z);
		const TerrainTile neighbour = *t.getTile(x + 1, z);
		tile->texture = paint;
		tile->rot = 3; tile->xflip = 1; tile->zflip = 0;
		t.writeBCM(out);
		std::filesystem::copy_file(out, saved, std::filesystem::copy_options::overwrite_existing);
		Terrain painted;
		painted.readBCM("wkbre2-bcm-test.bcm");
		const TerrainTile* pt = painted.getTile(x, z);
		const TerrainTile* pn = painted.getTile(x + 1, z);
		const bool paintOk = pt->texture->grp->name == paint->grp->name && pt->texture->id == paint->id && pt->rot == 3 && pt->xflip == 1
			&& pt->zflip == 0 && pn->texture->grp->name == neighbour.texture->grp->name && pn->texture->id == neighbour.texture->id
			&& painted.vertices[z * (painted.width + 1) + x] == (uint8_t)(before + 37);
		printf("BCM painted tile read back %d (%s %d; tables %zu groups %zu ids, were %zu %zu)\n", (int)paintOk, paint->grp->name.c_str(), paint->id,
			painted.bcmGroups.size(), painted.bcmIds.size(), t.bcmGroups.size(), t.bcmIds.size());

		// a lake added a tile away (its water 5 units above the ground there): it reads back and
		// floods that tile, and the painted tile is still painted
		const size_t lakesBefore = t.lakes.size();
		const int lx = x + 3, lz = z + 3;
		t.lakes.emplace_back(lx * 5.0f + 2.5f, t.getVertex(lx, lz) + 5.0f, lz * 5.0f + 2.5f);
		t.writeBCM(out);
		std::filesystem::copy_file(out, saved, std::filesystem::copy_options::overwrite_existing);
		Terrain flooded;
		flooded.readBCM("wkbre2-bcm-test.bcm");
		const bool lakeOk = flooded.lakes.size() == lakesBefore + 1 && flooded.getTile(lx, lz)->fullOfWater
			&& flooded.getTile(x, z)->texture->grp->name == paint->grp->name;
		printf("BCM lake read back %d (%zu lakes, were %zu)\n", (int)lakeOk, flooded.lakes.size(), lakesBefore);

		// the wkbai map file (.wkbmap next to the BCM): a fine height and 70 lakes (the BCM holds
		// 63) read back; the BCM alone has the whole step and the first 63 lakes
		const int fi = (z + 1) * (t.width + 1) + x;
		const float fine = t.heightSteps(fi) + 0.375f;
		t.setHeightSteps(fi, fine);
		while (t.lakes.size() < 70)
			t.lakes.emplace_back(lx * 5.0f + 2.5f, t.getVertex(lx, lz) + 1.0f, lz * 5.0f + 2.5f);
		t.writeBCM(out);
		std::filesystem::copy_file(out, saved, std::filesystem::copy_options::overwrite_existing);
		const std::filesystem::path extras = g_userPath / "saved" / "wkbre2-bcm-test.wkbmap";
		const bool extrasWritten = t.needsMapExtras() && t.writeMapExtras(extras);
		Terrain full;
		full.readBCM("wkbre2-bcm-test.bcm");
		std::filesystem::remove(extras);
		Terrain bcmOnly;
		bcmOnly.readBCM("wkbre2-bcm-test.bcm");
		const bool extrasOk = extrasWritten && std::abs(full.heightSteps(fi) - fine) < 0.002f && full.lakes.size() == 70
			&& bcmOnly.heightSteps(fi) == std::floor(fine) && bcmOnly.lakes.size() == 63;
		printf("WKBMAP read back %d (height %.4f, was %.4f; %zu lakes; without it %.4f and %zu lakes)\n", (int)extrasOk, full.heightSteps(fi), fine,
			full.lakes.size(), bcmOnly.heightSteps(fi), bcmOnly.lakes.size());
		std::filesystem::remove(saved);
	}
	fflush(stdout);
}

// AI against AI without a window (needs the game data): every position an AI General, the
// simulation run as fast as it goes. "aiTest": { "level": "<.lvl>", "generals": ["<blueprint>" or ""
// (random), ...], "minutes": n, "seed": n } (default: 4 Peaks, two random generals, 20 minutes).
// Prints "AITEST t=<s> player <id> (<general>): <characters> characters, <buildings> buildings,
// food/wood/gold" every game minute, then "AITEST end ... standing: ..." and the speed against
// real time.
void Test_AIvsAI()
{
	LoadBCP("data.bcp");
	// WKB_WATCHDOG=<seconds>: a step that doesn't end in that time: where the simulation is, then exit
	if (const char* wd = getenv("WKB_WATCHDOG")) {
		extern std::atomic<const char*> g_simPhase;
		extern std::atomic<uint32_t> g_simPhaseObject;
		extern std::atomic<uint64_t> g_simSteps;
		const int limit = std::max(2, atoi(wd));
		std::thread([limit]() {
			uint64_t last = 0;
			int still = 0;
			for (;;) {
				std::this_thread::sleep_for(std::chrono::seconds(1));
				const uint64_t now = g_simSteps.load();
				still = now == last && now > 0 ? still + 1 : 0;
				last = now;
				if (still >= limit) {
					printf("WATCHDOG step %llu has not ended in %d s: in %s, object %u\n", (unsigned long long)now, limit, g_simPhase.load(), g_simPhaseObject.load());
					fflush(stdout);
					_exit(3);
				}
			}
		}).detach();
	}
	const nlohmann::json cfg = g_settings.value("aiTest", nlohmann::json::object());
	const std::string level = cfg.value("level", std::string("2 Player 4 Peaks.lvl"));
	const float minutes = cfg.value("minutes", 20.0f);
	g_settings["simSeed"] = cfg.value("seed", 1u);
	// "record": "<folder>" records the game there; "replay": "<folder>" plays such a recording
	// instead of setting up a game (the same census, then the same end hash if it plays the same)
	const std::string recordTo = cfg.value("record", std::string());
	const std::string replayFrom = cfg.value("replay", std::string());
	Server server;
	server.loadSaveGame(("Save_Games\\" + level).c_str());
	QuickSkirmishMenu skirmish;
	skirmish.changeSettingsForLevel(level);
	std::vector<QuickSkirmishMenu::Slot> slots;
	const nlohmann::json generals = cfg.value("generals", nlohmann::json::array({ "", "" }));
	// "aiScripts": ["<Lua AI>" or "", ...]: each position's Lua AI
	const nlohmann::json scripts = cfg.value("aiScripts", nlohmann::json::array());
	for (const auto& g : generals) {
		QuickSkirmishMenu::Slot slot;
		slot.type = QuickSkirmishMenu::Slot::AI;
		slot.general = g.get<std::string>();
		// "human": a human player who does nothing (no AI); the census counts the AIs' attacks on it
		if (slot.general == "human") {
			slot.type = QuickSkirmishMenu::Slot::HUMAN;
			slot.general.clear();
		}
		if (slots.size() < scripts.size() && scripts[slots.size()].is_string())
			slot.aiScript = scripts[slots.size()].get<std::string>();
		slots.push_back(slot);
	}
	skirmish.setSlots(slots);
	// the skirmish settings, as the menu's ("settings": { "populationLimit": 1000, ... }); the
	// original's defaults otherwise
	// (the json kept in a variable: items() of the temporary that value() returns dangles)
	const nlohmann::json settings = cfg.value("settings", nlohmann::json::object());
	for (const auto& item : settings.items())
		skirmish.setSetting(item.key(), item.value().get<int>());
	// as a game does: the clock runs when the level starts (its start reactions schedule sequences);
	// applySettings starts it (starting it a second time ran every "On Level Start" reaction twice:
	// two of every AI marker, commission and work order)
	server.timeManager.unpause();
	server.timeManager.stepMs = 20;
	skirmish.applySettings(server);
	if (!recordTo.empty() && !server.startRecording(PathFromUtf8(recordTo), skirmish.setupJson(level, server.lastSeed)))
		printf("AITEST can't record to %s\n", recordTo.c_str());
	// a replay: the same setup (this config), then the recorded events
	if (!replayFrom.empty() && !server.startReplay(PathFromUtf8(replayFrom))) {
		printf("AITEST no recording in %s\n", replayFrom.c_str());
		return;
	}

	std::map<uint32_t, std::set<uint32_t>> raised; // per player, every soldier seen at a census
	std::map<uint32_t, std::set<uint32_t>> siegeRaised; // per player, every siege engine seen at a census
	auto census = [&](const char* when) {
		ServerGameObject* lvl = server.getLevel();
		std::vector<std::string> standing;
		for (const auto& [type, children] : lvl->children)
			for (CommonGameObject* c : children) {
				ServerGameObject* player = (ServerGameObject*)c;
				if (player->blueprint->bpClass != Tags::GAMEOBJCLASS_PLAYER || !player->aiController.planBlueprint)
					continue;
				float damage = 0.0f; // hit points missing from the standing buildings
				std::pair<float, float> sightLow{ 1e9f, 0.0f }, sightHigh{ -1e9f, 0.0f }; // characters' sight (tiles), at height
				int characters = 0, buildings = 0, soldiers = 0; // soldiers: characters but peasants, carts, merchants, clergy
				int living = 0, livingSoldiers = 0; // (the characters count corpses too: "Dead Peasant", "Rider Dying")
				int carts = 0, villages = 0;
				float villageFood = 0, villageWood = 0, villageGold = 0; // what the villages hold, not yet carted home
				std::map<std::string, int> orders; // the characters' current orders
				std::map<std::string, int> markers; // markers by blueprint
				std::map<std::string, int> siege; // siege engines alive, by blueprint and parent
				char sample[256] = {}, villageStock[128] = {}, cartInfo[256] = {};
				auto count = [&](CommonGameObject* obj, auto& self) -> void {
					for (const auto& [t, ch] : obj->children)
						for (CommonGameObject* o : ch) {
							if (o->blueprint->bpClass == Tags::GAMEOBJCLASS_CHARACTER) {
								characters++;
								{
									// the height term of the sight range (CommonGameObject::getItem)
									const float s = o->getItem(Tags::PDITEM_ACTUAL_SIGHT_RANGE);
									if (s < sightLow.first) sightLow = { s, o->position.y };
									if (s > sightHigh.first) sightHigh = { s, o->position.y };
								}
								{
									static const char* civilians[] = { "Peasant", "Cart", "Merchant", "Priest", "Bishop", "Inquisitor", "Scout" };
									const std::string& n = o->blueprint->name;
									if (n.find("Cart") != std::string::npos)
										carts++;
									const bool civilian = std::any_of(std::begin(civilians), std::end(civilians), [&](const char* c) { return n.find(c) != std::string::npos; });
									if (!civilian) {
										soldiers++;
										raised[player->id].insert(o->id);
									}
									if (n.rfind("Dead ", 0) != 0 && n.find("Dying") == std::string::npos) {
										living++;
										livingSoldiers += !civilian;
									}
									static const char* engines[] = { "Battering Ram", "Trebuchet", "Mangonel", "Siege Tower" };
									if (std::any_of(std::begin(engines), std::end(engines), [&](const char* e) { return n.find(e) != std::string::npos; })) {
										siegeRaised[player->id].insert(o->id);
										siege[n + " in " + obj->blueprint->name]++;
									}
								}
								Order* order = ((ServerGameObject*)o)->orderConfig.getCurrentOrder();
								orders[order ? server.gameSet->orders.getString(order->blueprint->bpid) : std::string("idle")]++;
								if (order && server.gameSet->orders.getString(order->blueprint->bpid).rfind("Transport", 0) == 0)
									if (Task* task = order->getCurrentTask())
										snprintf(cartInfo, sizeof(cartInfo), "cart %u: task %s -> %s %u, carrying wood %.0f, at (%.0f,%.0f), target at (%.0f,%.0f), moving %d", o->id,
											server.gameSet->tasks.getString(task->blueprint->bpid).c_str(), task->target ? task->target->blueprint->name.c_str() : "-",
											task->target ? task->target->id : 0, o->getItem(Tags::PDITEM_WOOD), o->position.x, o->position.z,
											task->target ? task->target->position.x : 0.0f, task->target ? task->target->position.z : 0.0f,
											(int)((ServerGameObject*)o)->movement.isMoving());
									// what stands on the cart's tile (a building placed over it traps it)
									if (order && server.gameSet->orders.getString(order->blueprint->bpid).rfind("Transport", 0) == 0 && server.tiles && server.terrain) {
										const auto [tw, th] = server.terrain->getNumPlayableTiles();
										const int tx = (int)(o->position.x / 5.0f), tz = (int)(o->position.z / 5.0f);
										if (tx >= 0 && tz >= 0 && tx < tw && tz < th) {
											const CommonGameObject* b = server.tiles[tz * tw + tx].building.getFrom(&server);
											const size_t len = strlen(cartInfo);
											snprintf(cartInfo + len, sizeof(cartInfo) - len, ", tile: %s%s", b ? b->blueprint->name.c_str() : "free",
												b && server.tiles[tz * tw + tx].buildingPassable ? " (passable)" : "");
										}
									}
								// a builder: where it is, what it builds and how far it is from it; once per
								// player and target the blocked tiles between them
								if (order && server.gameSet->orders.getString(order->blueprint->bpid) == "Build")
									if (Task* task = order->getCurrentTask(); task && task->target) {
										static std::map<uint32_t, uint32_t> shownBuilder;
										if (shownBuilder[player->id] != task->target->id) {
											shownBuilder[player->id] = task->target->id;
											const int ux = (int)(o->position.x / 5), uz = (int)(o->position.z / 5);
											const int tx = (int)(task->target->position.x / 5), tz = (int)(task->target->position.z / 5);
											printf("AITEST   a builder %u: task %s -> %s#%u at (%.0f,%.0f), builder at (%.0f,%.0f) y %.1f, %.0f m, moving %d\n", o->id,
												server.gameSet->tasks.getString(task->blueprint->bpid).c_str(), task->target->blueprint->name.c_str(), task->target->id,
												task->target->position.x, task->target->position.z, o->position.x, o->position.z, o->position.y,
												(task->target->position - o->position).len2xz(), (int)((ServerGameObject*)o)->movement.isMoving());
											if (std::abs(ux - tx) < 70 && std::abs(uz - tz) < 70)
												for (int z = std::max(uz, tz) + 6; z >= std::min(uz, tz) - 6; z--) {
													std::string row;
													for (int x = std::min(ux, tx) - 6; x <= std::max(ux, tx) + 6; x++)
														row += (x == ux && z == uz) ? 'U' : (x == tx && z == tz) ? 'T' : MovementController::isTileBlocked(o->blueprint, x, z) ? '#' : '.';
													printf("AITEST     %s\n", row.c_str());
												}
										}
									}
								if (order && !sample[0] && server.gameSet->orders.getString(order->blueprint->bpid) == "AI Chop Tree")
									if (Task* task = order->getCurrentTask()) {
										const std::string tname = server.gameSet->tasks.getString(task->blueprint->bpid);
										snprintf(sample, sizeof(sample), "%u: task %s -> %s, carrying wood %.0f, at (%.0f,%.0f), %.0f m from it, moving %d", o->id, tname.c_str(),
											task->target ? task->target->blueprint->name.c_str() : "-", o->getItem(Tags::PDITEM_WOOD), o->position.x, o->position.z,
											task->target ? (task->target->position - o->position).len2xz() : 0.0f, (int)((ServerGameObject*)o)->movement.isMoving());
											// once per player: the blocked tiles between a delivering woodcutter and its building
											static std::set<uint32_t> mapped;
											if (task->target && tname.rfind("Deliver", 0) == 0 && mapped.insert(player->id).second) {
												const int ux = (int)(o->position.x / 5), uz = (int)(o->position.z / 5);
												const int tx = (int)(task->target->position.x / 5), tz = (int)(task->target->position.z / 5);
												printf("AITEST   tiles from woodcutter U (%d,%d) to %s T (%d,%d), # blocked:\n", ux, uz, task->target->blueprint->name.c_str(), tx, tz);
												for (int z = std::max(uz, tz) + 6; z >= std::min(uz, tz) - 6; z--) {
													std::string row;
													for (int x = std::min(ux, tx) - 6; x <= std::max(ux, tx) + 6; x++)
														row += (x == ux && z == uz) ? 'U' : (x == tx && z == tz) ? 'T' : MovementController::isTileBlocked(o->blueprint, x, z) ? '#' : '.';
													printf("AITEST     %s\n", row.c_str());
												}
											}
										}
							}
							else if (o->blueprint->bpClass == Tags::GAMEOBJCLASS_BUILDING) {
								buildings++;
								{
									// hit points missing from standing buildings (not foundations, which fill up)
									static const int hp = server.gameSet->items.names.getIndex("Hit Points"), hpCap = server.gameSet->items.names.getIndex("Hit Point Capacity");
									static const int isFoundation = server.gameSet->items.names.getIndex("Is a Foundation");
									if (hp >= 0 && hpCap >= 0 && !(isFoundation >= 0 && o->getItem(isFoundation) > 0.0f))
										damage += std::max(0.0f, o->getItem(hpCap) - o->getItem(hp));
								}
								if (o->blueprint->name.rfind("Village", 0) == 0 && o->blueprint->name.find("Foundation") == std::string::npos) {
									villages++;
									villageFood += o->getItem(Tags::PDITEM_FOOD);
									villageWood += o->getItem(Tags::PDITEM_WOOD);
									villageGold += o->getItem(Tags::PDITEM_GOLD);
								}
								if (o->blueprint->name.rfind("Village", 0) == 0 && o->blueprint->name.find("Foundation") == std::string::npos)
									snprintf(villageStock, sizeof(villageStock), "%s wood %.0f food %.0f", o->blueprint->name.c_str(), o->getItem(Tags::PDITEM_WOOD), o->getItem(Tags::PDITEM_FOOD));
							}
							else if (o->blueprint->bpClass == Tags::GAMEOBJCLASS_MARKER) {
								markers[o->blueprint->name]++;
							}
							self(o, self);
						}
				};
				count(player, count);
				printf("AITEST %s t=%.0f player %u (%s): %d characters (%d soldiers, %zu raised, %d sent home wounded), %d buildings, food %.0f wood %.0f gold %.0f; living %d (%d soldiers)\n", when,
					server.timeManager.currentTime, player->id, player->blueprint->name.c_str(), characters, soldiers, raised[player->id].size(), player->aiController.retreated, buildings,
					player->getItem(Tags::PDITEM_FOOD), player->getItem(Tags::PDITEM_WOOD), player->getItem(Tags::PDITEM_GOLD), living, livingSoldiers);
				{
					static const int cartsRequired = server.gameSet->items.names.getIndex("Carts Required");
					printf("AITEST   economy player %u: carts %d (the scripts want %.0f), villages %d holding food %.0f wood %.0f gold %.0f\n", player->id, carts,
						cartsRequired >= 0 ? player->getItem(cartsRequired) : -1.0f, villages, villageFood, villageWood, villageGold);
				}
				std::string markerList;
				for (const auto& [name, n] : markers)
					markerList += " " + name + "=" + std::to_string(n);
				printf("AITEST   markers:%s\n", markerList.c_str());
				// the armies: type, then its members (direct children; formations count their own)
				std::map<std::string, std::pair<int, int>> armies; // type -> (armies, members)
				std::function<int(const CommonGameObject*)> members = [&](const CommonGameObject* o) {
					int n = 0;
					for (const auto& [t, cs] : o->children)
						for (const CommonGameObject* c : cs)
							n += c->blueprint->bpClass == Tags::GAMEOBJCLASS_CHARACTER ? 1 : members(c);
					return n;
				};
				for (const auto& [t, cs] : player->children)
					for (const CommonGameObject* c : cs)
						if (c->blueprint->bpClass == Tags::GAMEOBJCLASS_ARMY) {
							auto& a = armies[c->blueprint->name];
							a.first++;
							a.second += members(c);
						}
				std::string armyList;
				for (const auto& [name, a] : armies)
					armyList += " " + name + "=" + std::to_string(a.first) + "/" + std::to_string(a.second);
				printf("AITEST   armies (count/units):%s; attack waves sent %d, %d units, %d formations, %d focus-fire orders\n", armyList.c_str(), player->aiController.waves, player->aiController.waveUnits, player->aiController.formationsSent, player->aiController.focusOrders);
				{
					const AIController& ai = player->aiController;
					static const GameSet* itemsOf = nullptr;
					static int mix[4];
					if (itemsOf != server.gameSet.get()) {
						itemsOf = server.gameSet.get();
						static const char* kinds[4] = { "Heavy Infantry", "Light Infantry", "Heavy Cavalry", "Light Cavalry" };
						for (int k = 0; k < 4; k++)
							mix[k] = server.gameSet->items.names.getIndex(std::string("Combined Arms ") + kinds[k] + " Percentage");
					}
					printf("AITEST   adapt player %u: seen HI %.0f LI %.0f HC %.0f LC %.0f; mix %.0f/%.0f/%.0f/%.0f (personality %.0f/%.0f/%.0f/%.0f), %d changes; grudge %u\n", player->id,
						ai.enemySeen[1], ai.enemySeen[2], ai.enemySeen[3], ai.enemySeen[4],
						mix[0] >= 0 ? player->getItem(mix[0]) : 0.0f, mix[1] >= 0 ? player->getItem(mix[1]) : 0.0f, mix[2] >= 0 ? player->getItem(mix[2]) : 0.0f, mix[3] >= 0 ? player->getItem(mix[3]) : 0.0f,
						ai.baseMix[0], ai.baseMix[1], ai.baseMix[2], ai.baseMix[3], ai.mixChanges, ai.grudgePlayer);
				}
				// its Lua AI, if any, and what it did (the script's census())
				if (const std::string lua = player->aiController.luaCensus(); !lua.empty())
					printf("AITEST   lua player %u: %s\n", player->id, lua.c_str());
				// the advanced AI (AIAdvanced.cpp): whether this player has it, and what it did
				printf("AITEST   advanced player %u: %s, lifelines %d, home guard %d; threats met %d (%d defenders), peasants sent home %d; waves held %d, recalled %d, retargeted %d, out %zu\n",
					player->id, player->aiController.advancedState == 1 ? "on" : "off", player->aiController.lifelines, player->aiController.guardTrained, player->aiController.threatsMet, player->aiController.defendersSent, player->aiController.peasantsFled, player->aiController.wavesHeld, player->aiController.wavesRecalled, player->aiController.wavesRetargeted, player->aiController.advancedWaves.size());
				{
					const AIController& ai = player->aiController;
					printf("AITEST   war player %u: fighters %d, surplus trained %d; threats %d (%d defenders), waves %d (%d units), struck back %d, retargets %d, retreats %d, engaged %d, breaches %d; out: %zu defences, %zu waves\n",
						player->id, ai.fighters(), ai.surplusTrained, ai.warThreats, ai.warDefenders, ai.warWavesSent, ai.warWaveUnits, ai.warCounters, ai.warRetargets, ai.warRetreats, ai.warEngaged, ai.warBreaches,
						ai.warDefences.size(), ai.warWaves.size());
				}
				printf("AITEST   losses player %u: %d buildings destroyed, building hit points lost %.0f (%.0f destroyed, %.0f standing)\n", player->id, server.buildingsDestroyed[player->id],
					server.buildingHpDestroyed[player->id] + damage, server.buildingHpDestroyed[player->id], damage);
				{
					std::string siegeList;
					for (const auto& [name, n] : siege)
						siegeList += " " + name + "=" + std::to_string(n);
					printf("AITEST   siege player %u: %zu raised;%s\n", player->id, siegeRaised[player->id].size(), siegeList.c_str());
					if (characters)
						printf("AITEST   sight player %u: characters' actual sight %.2f at y %.1f to %.2f at y %.1f (lowest ground %.1f)\n", player->id,
							sightLow.first, sightLow.second, sightHigh.first, sightHigh.second, g_lowestTerrainHeight);
				}
				{
					SrvScriptContext pctx(&server, player);
					std::string probe;
					for (const char* name : { "Commission Route", "Kingmaker Resource Villages Required", "Kingmaker Farms Required", "Kingmaker Farming Villages Required", "War Mode", "War Override", "War Peasants Needed", "The Army Type I Was Trying To Spawn", "I Am Rushing", "Peasants Required", "Total Population Count", "Population Count", "Can Spawn Peasant", "Food", "Aggression", "Blitzkreig", "Kingmaker Do I Need Flaming Arrow Upgrade Pause Buildings", "Flaming Arrow Upgrade Status", "War Wait", "I Should Shift Into War Mode", "Defensive Mode", "Current Impatience", "Using Full Commission", "Economic Commission", "Impatience", "My Combined Arms Preference", "My Rushing Army Preference", "My Fire Arrow Preference", "My Raiding Party Preference", "My Siege Weapon Preference", "Idle Peasant Count", "Wood Chopper Count", "Builder Count", "Gold Miner Count", "Shop Keeper Count" }) {
						const int item = server.gameSet->items.names.getIndex(name);
						probe += std::string(" ") + name + "=" + (item >= 0 ? std::to_string((int)player->getItem(item)) : "?");
					}
					for (const char* name : { "My Resource Markers", "All Tree Markers", "All Non Associated Tree Markers", "Number of Resource Villages With More Than Village Limit Associated Resources", "All My Kingmaker Villages & Foundations", "All My Kingmaker Villages", "Nearest Resource Marker To My Manor", "Palace Building Of Player" }) {
						const int fi = server.gameSet->objectFinderDefinitions.names.getIndex(name);
						probe += std::string(" | ") + name + ": " + (fi >= 0 ? std::to_string(server.gameSet->objectFinderDefinitions[fi]->eval(&pctx).size()) : "?");
					}
					printf("AITEST   probe:%s\n", probe.c_str());
					// who the scripts see at the door (Defensive Mode): "Enemies Within 250m of My
					// Manor", by player and blueprint
					if (const int fi = server.gameSet->objectFinderDefinitions.names.getIndex("Enemies Within 250m of My Manor"); fi >= 0) {
						std::map<std::string, int> door;
						for (ServerGameObject* o : server.gameSet->objectFinderDefinitions[fi]->eval(&pctx))
							door[std::to_string(o->getPlayer() ? o->getPlayer()->id : 0) + ":" + o->blueprint->name]++;
						std::string list;
						for (const auto& [k, n] : door)
							list += " " + k + "=" + std::to_string(n);
						printf("AITEST   doorstep player %u:%s\n", player->id, list.c_str());
					}
				}
				std::string works;
				for (const auto& woi : player->aiController.workOrderInstances)
					if (ServerGameObject* city = woi.city.get()) {
						SrvScriptContext cityCtx(&server, city);
						works += " " + city->blueprint->name + "#" + std::to_string(city->id) + ":" + std::to_string(woi.unitFinder->eval(&cityCtx).size()) + " units";
					}
				printf("AITEST   work orders:%s\n", works.c_str());
				// the commissions: suspended or not, and per requirement how many are needed and there
				for (const CommissionInstance& ci : player->aiController.commissionInstances) {
					ServerGameObject* handler = ci.handlerObject.get();
					if (!handler)
						continue;
					SrvScriptContext hctx(&server, handler);
					const GSCommission* gc = ci.blueprint;
					std::string reqs;
					for (const auto& req : gc->characterRequirements)
						reqs += " chars " + std::to_string((int)std::ceil(req.vdCountNeeded->eval(&hctx))) + "/" + std::to_string(req.fdAlreadyHaving->eval(&hctx).size());
					// wanted/there, then +foundations placed
					for (size_t bi = 0; bi < gc->buildingRequirements.size(); bi++) {
						const auto& req = gc->buildingRequirements[bi];
						reqs += " " + req.bpBuilding->name + " " + std::to_string((int)req.vdCountNeeded->eval(&hctx)) + "/" + std::to_string(req.fdAlreadyHaving->eval(&hctx).size());
						if (bi < ci.buildingReqInsts.size() && !ci.buildingReqInsts[bi].foundations.empty())
							reqs += "+" + std::to_string(ci.buildingReqInsts[bi].foundations.size());
					}
					printf("AITEST   commission %s at %s#%u%s:%s\n", server.gameSet->commissions.getString(gc).c_str(), handler->blueprint->name.c_str(), handler->id,
						gc->suspensionCondition && gc->suspensionCondition->booleval(&hctx) ? " (suspended)" : "", reqs.c_str());
					std::string hitems;
					for (const char* name : { "The Army Type I Was Trying To Spawn", "I Can Commission Minimum Army", "I Can Commission Maximum Army", "Army Can Be Spawned", "I Have Spawned An Army", "Spawn Size", "Number Of Heavy Infantry In Defence", "Number Of Light Infantry In Defence", "Number of Attacking Heavy Infantry In Defence", "Number of Attacking Light Infantry In Defence", "We Are Outnumbered In Defence", "My Next Army Type", "Last Army Has Been Finished", "Current Offensive Loop", "Combined Arms Spawn Chance", "Fire Arrow Spawn Chance", "Raiding Army Spawn Chance", "Siege Weapon Army Spawn Chance" }) {
						const int item = server.gameSet->items.names.getIndex(name);
						if (item >= 0 && handler->getItem(item) != 0.0f)
							hitems += std::string(" ") + name + "=" + std::to_string(handler->getItem(item));
					}
					const Order* hord = handler->orderConfig.getCurrentOrder();
					printf("AITEST     handler items:%s; order %s\n", hitems.c_str(), hord ? server.gameSet->orders.getString(hord->blueprint).c_str() : "none");
					if (handler->blueprint->name.find("Defensive Army") != std::string::npos && !gc->characterRequirements.empty()) {
						std::string rungs;
						for (const auto& req : gc->characterRequirements) {
							rungs += " [";
							for (const auto& rung : req.ladder->entries) {
								const SrvFinderResult where = rung.finder->eval(&hctx);
								const std::string spawnName = "Spawn " + rung.type->name;
								const int cmdid = server.gameSet->commands.names.getIndex(spawnName);
								const char* verdict = server.gameSet->orders.names.getIndex(spawnName) < 0 ? "no order" : cmdid < 0 ? "no command" : "impossible";
								std::string failed;
								for (ServerGameObject* b : where) {
									if (b->blueprint->bpClass != Tags::GAMEOBJCLASS_BUILDING || cmdid < 0)
										continue;
									SrvScriptContext bctx(&server, b);
									auto _ = bctx.selectedObject.change(b);
									failed.clear();
									for (GSCondition* cond : server.gameSet->commands[cmdid].conditionsImpossible)
										if (!cond->test->booleval(&bctx))
											failed += "!" + server.gameSet->conditions.getString(cond);
									if (failed.empty()) { verdict = "ok"; break; }
								}
								rungs += " " + rung.type->name + " at " + std::to_string(where.size()) + " " + verdict + failed + ";";
							}
							rungs += "]";
						}
						printf("AITEST     rungs:%s\n", rungs.c_str());
					}
				}
				std::string line;
				for (const auto& [name, n] : orders)
					line += " " + name + "=" + std::to_string(n);
				printf("AITEST   orders:%s\n", line.c_str());
				printf("AITEST   a woodcutter: %s; %s; %s\n", sample, villageStock, cartInfo);
				if (characters + buildings > 0)
					standing.push_back(player->blueprint->name);
			}
		// every player's forts (the buildings with a "Fort Army", Mechanics\fort attack.cpp): how
		// many see enemy characters, and how many have them as targets, by the enemies' player
		{
			std::string shots;
			for (const auto& [pair, n] : server.buildingShots)
				shots += " " + std::to_string(pair.first) + "->" + std::to_string(pair.second) + "=" + std::to_string(n);
			printf("AITEST building shots (shooter->target player):%s\n", shots.c_str());
			const int myFort = server.gameSet->associations.names.getIndex("My Fort");
			const int fortTargets = server.gameSet->associations.names.getIndex("My Fort Targets");
			for (const auto& [type, children] : lvl->children)
				for (CommonGameObject* c : children) {
					ServerGameObject* player = (ServerGameObject*)c;
					if (player->blueprint->bpClass != Tags::GAMEOBJCLASS_PLAYER || myFort < 0 || fortTargets < 0)
						continue;
					int forts = 0;
					std::map<uint32_t, int> seeing, targeting; // enemy player -> forts
					for (const auto& [t, cs] : player->children)
						for (CommonGameObject* a : cs) {
							auto* army = (ServerGameObject*)a;
							if (army->blueprint->name != "Fort Army" || !army->associates.count(myFort))
								continue;
							for (const SrvGORef& ref : army->associates.at(myFort)) {
								ServerGameObject* fort = ref.get();
								if (!fort || fort->deleted)
									continue;
								forts++;
								const float sight = fort->getItem(Tags::PDITEM_ACTUAL_SIGHT_RANGE) * 5.0f;
								std::set<uint32_t> seen;
								NNSearch search;
								search.start(&server, fort->position, sight);
								while (CommonGameObject* o = search.next()) {
									CommonGameObject* op = o->getPlayer();
									if (o->blueprint->bpClass == Tags::GAMEOBJCLASS_CHARACTER && op && op != player && op->blueprint->name != "Neutral Player"
										&& server.getDiplomaticStatus(op, player) == 2 && (o->position - fort->position).sqlen2xz() <= sight * sight)
										seen.insert(op->id);
								}
								for (uint32_t p : seen)
									seeing[p]++;
								std::set<uint32_t> aimed;
								if (fort->associates.count(fortTargets))
									for (const SrvGORef& tr : fort->associates.at(fortTargets))
										if (ServerGameObject* tg = tr.get(); tg && tg->getPlayer())
											aimed.insert(tg->getPlayer()->id);
								for (uint32_t p : aimed)
									targeting[p]++;
								// a fort that sees enemies: its archers inside ("Number Of Archers Inside"),
								// Hold Fire, its Fort Army's order, the targets it holds
								if (!seen.empty()) {
									static const int archersEq = server.gameSet->equations.names.getIndex("Number Of Archers Inside");
									static const int holdFire = server.gameSet->items.names.getIndex("Hold Fire");
									SrvScriptContext fctx(&server, fort);
									const Order* fo = army->orderConfig.getCurrentOrder();
									std::string ids;
									for (uint32_t p : seen)
										ids += " " + std::to_string(p);
									printf("AITEST     fort %s#%u of %u: archers inside %.1f, hold fire %.0f, army order %s, %zu targets; enemies of%s\n", fort->blueprint->name.c_str(), fort->id, player->id,
										archersEq >= 0 ? server.gameSet->equations[archersEq]->eval(&fctx) : -1.0f, holdFire >= 0 ? fort->getItem(holdFire) : -1.0f,
										fo ? server.gameSet->orders.getString(fo->blueprint).c_str() : "none",
										fort->associates.count(fortTargets) ? fort->associates.at(fortTargets).size() : (size_t)0, ids.c_str());
								}
							}
						}
					if (!forts)
						continue;
					std::string list;
					for (const auto& [p, n] : seeing)
						list += " " + std::to_string(p) + ":" + std::to_string(n) + "/" + std::to_string(targeting.count(p) ? targeting[p] : 0);
					printf("AITEST   forts player %u: %d; enemies in sight (player:forts seeing/targeting):%s\n", player->id, forts, list.c_str());
				}
		}
		// the human players (no AI): what is left, what was lost, the enemy soldiers at their base
		for (const auto& [type, children] : lvl->children)
			for (CommonGameObject* c : children) {
				ServerGameObject* player = (ServerGameObject*)c;
				if (player->blueprint->bpClass != Tags::GAMEOBJCLASS_PLAYER || player->aiController.planBlueprint || player->blueprint->name == "Neutral Player")
					continue;
				int characters = 0, buildings = 0;
				std::vector<ServerGameObject*> own;
				auto count = [&](ServerGameObject* obj, auto& self) -> void {
					for (const auto& [t, ch] : obj->children)
						for (CommonGameObject* o : ch) {
							if (o->blueprint->bpClass == Tags::GAMEOBJCLASS_CHARACTER)
								characters++;
							else if (o->blueprint->bpClass == Tags::GAMEOBJCLASS_BUILDING) {
								buildings++;
								own.push_back((ServerGameObject*)o);
							}
							self((ServerGameObject*)o, self);
						}
				};
				count(player, count);
				// enemy soldiers within 100 m of any of its buildings, by player
				std::map<uint32_t, std::set<uint32_t>> atBase;
				for (ServerGameObject* b : own) {
					NNSearch search;
					search.start(&server, b->position, 100.0f);
					while (CommonGameObject* o = search.next()) {
						CommonGameObject* op = o->getPlayer();
						if (o->blueprint->bpClass == Tags::GAMEOBJCLASS_CHARACTER && op && op != player && op->blueprint->name != "Neutral Player"
							&& o->blueprint->name.find("Peasant") == std::string::npos && o->blueprint->name.find("Cart") == std::string::npos)
							atBase[op->id].insert(o->id);
					}
				}
				std::string list;
				for (const auto& [p, s] : atBase)
					list += " " + std::to_string(p) + "=" + std::to_string(s.size());
				// hit points missing from its standing buildings (not foundations), as the AIs' losses
				float damage = 0.0f;
				{
					static const int hp = server.gameSet->items.names.getIndex("Hit Points"), hpCap = server.gameSet->items.names.getIndex("Hit Point Capacity");
					static const int isFoundation = server.gameSet->items.names.getIndex("Is a Foundation");
					for (ServerGameObject* b : own)
						if (hp >= 0 && hpCap >= 0 && !(isFoundation >= 0 && b->getItem(isFoundation) > 0.0f))
							damage += std::max(0.0f, b->getItem(hpCap) - b->getItem(hp));
				}
				printf("AITEST %s t=%.0f human %u: %d characters, %d buildings; %d buildings destroyed, building hit points lost %.0f; enemy soldiers at its buildings:%s\n", when,
					server.timeManager.currentTime, player->id, characters, buildings, server.buildingsDestroyed[player->id], server.buildingHpDestroyed[player->id] + damage, list.c_str());
			}
		return standing;
	};
	const uint64_t start = SDL_GetTicks();
	// for that many minutes from the level's own clock (Coastline's is saved at 7272 s: the game
	// ended before it began)
	const float startTime = server.timeManager.currentTime;
	float nextCensus = startTime + 60.0f;
	// walking in place (play test 3: units with the walk animation that didn't move until given
	// a new order): every second, a character playing one of its walk animations that hasn't
	// moved half a metre in 5 s is reported once per stand
	struct Stand { Vector3 pos; float since; bool shown; };
	std::map<uint32_t, Stand> stands;
	int walkingInPlace = 0;
	float nextStandCheck = startTime + 1.0f;
	const int moveAnim = server.gameSet->animations.names.getIndex("Move");
	auto isWalkAnim = [&](ServerGameObject* o) {
		if (o->animationIndex == moveAnim)
			return true;
		for (const auto& mb : o->blueprint->movementBands) {
			if (o->animationIndex == mb.defaultAnim)
				return true;
			for (const auto& p : mb.onEquAnims)
				if (o->animationIndex == p.second)
					return true;
		}
		return false;
	};
	// "corpses": where each corpse was first seen; one that has since moved more than 0.05 m is
	// printed ("AITEST corpse moved ..."), once a second while it moves
	struct CorpseSeen { Vector3 pos; float since; float moved = 0.0f; std::string from; };
	std::map<uint32_t, CorpseSeen> corpsesSeen;
	int corpsesMoved = 0;
	std::function<void(ServerGameObject*)> watchCorpses = [&](ServerGameObject* obj) {
		for (const auto& [type, children] : obj->children)
			for (CommonGameObject* c : children) {
				ServerGameObject* o = (ServerGameObject*)c;
				if (o->blueprint->bpClass == Tags::GAMEOBJCLASS_CHARACTER && o->blueprint->name.rfind("Dead ", 0) == 0) {
					auto it = corpsesSeen.find(o->id);
					if (it == corpsesSeen.end())
						corpsesSeen[o->id] = CorpseSeen{ o->position, server.timeManager.currentTime, 0.0f, Tags::GAMEOBJCLASS_tagDict.getStringFromID(obj->blueprint->bpClass) };
					else {
						const float d = (o->position - it->second.pos).len2xz();
						if (d > it->second.moved + 0.05f) {
							if (it->second.moved <= 0.05f)
								corpsesMoved++;
							it->second.moved = d;
							Order* ord = o->orderConfig.getCurrentOrder();
							printf("AITEST corpse moved t=%.0f %u %s %.2f m since t=%.0f, parent %s %u (first %s), moving=%d mc=%d order=%s\n", server.timeManager.currentTime, o->id,
								o->blueprint->name.c_str(), d, it->second.since, Tags::GAMEOBJCLASS_tagDict.getStringFromID(obj->blueprint->bpClass), obj->id, it->second.from.c_str(),
								(int)o->movement.isMoving(), (int)o->movementController.isMoving(), ord ? server.gameSet->orders.getString(ord->blueprint->bpid).c_str() : "-");
						}
					}
				}
				watchCorpses(o);
			}
	};
	std::function<void(ServerGameObject*)> checkStands = [&](ServerGameObject* obj) {
		for (const auto& [type, children] : obj->children)
			for (CommonGameObject* c : children) {
				ServerGameObject* o = (ServerGameObject*)c;
				if (o->blueprint->bpClass != Tags::GAMEOBJCLASS_CHARACTER) {
					checkStands(o);
					continue;
				}
				const float now = server.timeManager.currentTime;
				// a cart unloading at a manor (play test 3: it stopped at the palisade's gate): how
				// far from the building's centre, once per delivery
				if (o->blueprint->name.find("Cart") != std::string::npos) {
					static std::map<uint32_t, bool> unloading;
					Order* order = o->orderConfig.getCurrentOrder();
					Task* task = order ? order->getCurrentTask() : nullptr;
					const bool now_ = task && task->target && task->proximitySatisfied
						&& server.gameSet->tasks.getString(task->blueprint->bpid).rfind("Deliver", 0) == 0;
					if (now_ && !unloading[o->id])
						printf("AITEST cart unloads t=%.0f cart %u at %s %u: %.1f m from its centre\n", now - startTime, o->id,
							task->target->blueprint->name.c_str(), task->target->id, (task->target->position - o->position).len2xz());
					unloading[o->id] = now_;
				}
				if (const char* tid = getenv("WKB_TRACE_STAND_ID"); tid && (uint32_t)atoi(tid) == o->id) {
					const auto& m = o->movement;
					printf("STAND t=%.1f at (%.2f,%.2f) moving %d from (%.2f,%.2f) to (%.2f,%.2f) start %.2f speed %.2f; controller %d nodes:", now, o->position.x, o->position.z,
						(int)m.isMoving(), m.getSource().x, m.getSource().z, m.getDestination().x, m.getDestination().z, m.getStartTime(), m.getSpeed(), (int)o->movementController.isMoving());
					for (const Vector3& n : o->movementController.m_pathNodes)
						printf(" (%.1f,%.1f)", n.x, n.z);
					printf("\n");
				}
				auto it = stands.try_emplace(o->id, Stand{ o->position, now, false }).first;
				Stand& s = it->second;
				if (!isWalkAnim(o) || (o->position - s.pos).sqlen2xz() > 0.25f) {
					s = { o->position, now, false };
					continue;
				}
				if (!s.shown && now - s.since >= 5.0f) {
					s.shown = true;
					walkingInPlace++;
					Order* order = o->orderConfig.getCurrentOrder();
					Task* task = order ? order->getCurrentTask() : nullptr;
					printf("AITEST walking in place t=%.0f %s %u player %u at (%.1f,%.1f): order %s, task %s -> %s %u at (%.1f,%.1f), movement %d, controller %d (%zu nodes), speed %.2f\n",
						now - startTime, o->blueprint->name.c_str(), o->id, o->getPlayer() ? o->getPlayer()->id : 0, o->position.x, o->position.z,
						order ? server.gameSet->orders.getString(order->blueprint->bpid).c_str() : "-",
						task ? server.gameSet->tasks.getString(task->blueprint->bpid).c_str() : "-",
						task && task->target ? task->target->blueprint->name.c_str() : "-", task && task->target ? task->target->id : 0,
						task && task->target ? task->target->position.x : 0.0f, task && task->target ? task->target->position.z : 0.0f,
						(int)o->movement.isMoving(), (int)o->movementController.isMoving(), o->movementController.m_pathNodes.size(), o->computeSpeed());
				}
			}
	};
	// the simulation's own speed over each game minute (the census, slow itself, left out): a
	// game falls behind 1x where this drops under 1
	double minuteSimMs = 0.0, minuteMaxStepMs = 0.0;
	// "saveAt": [<game s>, "<name>"]: the game saved then as saved\Save_Games\<name>.sav, e.g. a
	// skirmish's setup for the original to play on (RE repository, original-comparison.md)
	float saveAt = -1.0f;
	std::string saveName;
	if (cfg.contains("saveAt") && cfg["saveAt"].is_array() && cfg["saveAt"].size() == 2) {
		saveAt = startTime + cfg["saveAt"][0].get<float>();
		saveName = cfg["saveAt"][1].get<std::string>();
	}
	while (server.timeManager.currentTime < startTime + minutes * 60.0f) {
		const double stepStart = ((double)SDL_GetPerformanceCounter() * 1000.0 / (double)SDL_GetPerformanceFrequency());
		if (saveAt >= 0.0f && server.timeManager.currentTime >= saveAt) {
			saveAt = -1.0f;
			const std::string path = PathToUtf8(g_userPath / "saved" / "Save_Games" / PathFromUtf8(saveName + ".sav"));
			printf("AITEST saved %d to %s at t=%.2f\n", (int)server.saveGame(path.c_str()), path.c_str(), server.timeManager.currentTime);
		}
		if (server.replay) {
			if (!server.replayEvent())
				break;
		}
		else {
			server.timeManager.pendingMs = server.timeManager.stepMs;
			if (server.timeManager.step())
				server.simulate();
		}
		if (server.timeManager.currentTime >= nextStandCheck) {
			nextStandCheck += 1.0f;
			if (ServerGameObject* lvl = server.getLevel())
				checkStands(lvl);
			// "corpses": true: every corpse ("Dead ...") must stay where it became one
			if (cfg.value("corpses", false))
				if (ServerGameObject* lvl = server.getLevel())
					watchCorpses(lvl);
		}
		const double stepMs = ((double)SDL_GetPerformanceCounter() * 1000.0 / (double)SDL_GetPerformanceFrequency()) - stepStart;
		minuteSimMs += stepMs;
		// in a game the server catches up at most 10 steps (200 ms): a longer step loses time
		minuteMaxStepMs = std::max(minuteMaxStepMs, stepMs);
		if (server.timeManager.currentTime >= nextCensus) {
			nextCensus += 60.0f;
			size_t objects = 0;
			auto countAll = [&](CommonGameObject* obj, auto& self) -> void {
				objects++;
				for (const auto& [type, children] : obj->children)
					for (CommonGameObject* child : children)
						self(child, self);
			};
			countAll(server.getLevel(), countAll);
			printf("AITEST speed t=%.0f: %.2f game s per real s (%.2f ms per step, longest %.1f ms), %zu objects\n", server.timeManager.currentTime,
				60000.0 / std::max(minuteSimMs, 0.001), minuteSimMs / 3000.0, minuteMaxStepMs, objects);
			minuteSimMs = minuteMaxStepMs = 0.0;
			census("at");
			// wkbai's day and night, seasons and weather, when on
			if (server.environment.enabled()) {
				const ServerEnvironment& env = server.environment;
				const EnvironmentState& e = env.state;
				printf("AITEST environment t=%.0f: %02d:%02d %s (%s) %s %.1f C, clouds %.2f rain %.2f snow %.2f fog %.2f, wet %.2f snow cover %.2f ice %.2f water %+.2f m, sight %.2f speed %.2f, lit blocks %zu/%zu\n",
					server.timeManager.currentTime, (int)(e.dayFraction * 24.0f), (int)(e.dayFraction * 1440.0f) % 60, SeasonName(e.season),
					e.severity == 0 ? "gentle" : e.severity == 2 ? "harsh" : "normal", WeatherName(e.weather), e.temperatureC, e.cloudCover, e.rain,
					e.snowfall, e.fog, e.wetness, e.snowCover, e.ice, e.waterRise, e.sightFactor, e.speedFactor,
					(size_t)std::count(server.nightSight.lit.begin(), server.nightSight.lit.end(), (uint8_t)1), server.nightSight.lit.size());
				const auto& st = env.stats;
				{
					std::string reserves;
					if (CommonGameObject* level = server.getLevel())
						for (const auto& [type, children] : level->children)
							for (CommonGameObject* c : children)
								if (auto* pl = (ServerGameObject*)c; c->blueprint->bpClass == Tags::GAMEOBJCLASS_PLAYER && pl->aiController.winterReserve > 0.0f)
							reserves += " " + std::to_string(pl->id) + "=" + std::to_string((int)pl->aiController.winterReserve) + "/" + std::to_string((int)pl->getItem(Tags::PDITEM_FOOD));
					if (!reserves.empty())
						printf("AITEST environment AI winter stores (kept back/food):%s\n", reserves.c_str());
				}
				const int burningAlias = server.gameSet->aliases.names.getIndex("Burning Buildings");
				printf("AITEST environment flood tiles %zu, flooded now %zu; burning buildings: %zu; on the ice: at most %d units, %d unit-seconds; harvests lost to fallow fields %d; wildfires %d (%d from missiles, %d from buildings, %d out of nowhere), %d patches (at most %d at once), %d buildings set alight, %d killed, %zu trees charred\n", st.floodTiles, st.floodedTiles,
					burningAlias >= 0 ? server.aliases[burningAlias].size() : (size_t)0, st.onIceMost, st.onIceSeconds, st.fallowHarvests,
					st.wildfires, st.fromMissiles, st.fromBuildings, st.spontaneous, st.fireCells, st.fireCellsMost, st.buildingsFired, st.fireDeaths, server.environment.charredCount());
				printf("AITEST environment deaths: frozen %d, thirst %d, drowned %d; lightning: %d units, %d buildings, %d ground; flooded building-seconds %d (%.2f of capacity)\n",
					st.frozen, st.thirst, st.drowned, st.lightningUnits, st.lightningBuildings, st.lightningGround, st.floodedBuildings, st.floodDamage);
				// the peasants' lodging (PeasantLodging): who is in, and each player's farmers counted
				// while in, their "Number of Farmers" and crafts bonus
				{
					const PeasantLodging& lg = env.lodging;
					std::string per;
					const int bonusItem = server.gameSet->items.names.getIndex("Winter Crafts Bonus");
					if (CommonGameObject* level = server.getLevel())
						for (const auto& [type, children] : level->children)
							for (CommonGameObject* c : children)
								if (auto* pl = (ServerGameObject*)c; c->blueprint->bpClass == Tags::GAMEOBJCLASS_PLAYER && pl->blueprint->name != "Neutral Player") {
									char b[96];
									snprintf(b, sizeof(b), " %u: farmers in %d, Number of Farmers %.0f, crafts %.3f;", pl->id, lg.farmersOf(pl->id),
										pl->getItem(Tags::PDITEM_NUMBER_OF_FARMERS), bonusItem >= 0 ? pl->getItem(bonusItem) : 0.0f);
									per += b;
								}
					printf("AITEST lodging t=%.0f: %zu lodgers, %d inside; departures %d, lodged %d, woken %d, waited %d, evicted %d, stuck %d;%s\n",
						server.timeManager.currentTime, lg.count(), lg.lodgedCount(), lg.stats.departures, lg.stats.lodged, lg.stats.woken,
						lg.stats.waited, lg.stats.evicted, lg.stats.stuck, per.c_str());
				}
				// each player's: the weather's deaths, and the AI's answers (AIWeather.cpp)
				if (CommonGameObject* level = server.getLevel())
					for (const auto& [type, children] : level->children)
						for (CommonGameObject* c : children) {
							auto* pl = (ServerGameObject*)c;
							if (c->blueprint->bpClass != Tags::GAMEOBJCLASS_PLAYER || !pl->aiController.planBlueprint)
								continue;
							std::array<int, 5> d{};
							if (auto it = st.deathsBy.find(pl->id); it != st.deathsBy.end())
								d = it->second;
							printf("AITEST weather player %u (%s): weather AI %s; deaths: frozen %d, thirst %d, drowned %d, fire %d, lightning %d; %s\n", pl->id,
								pl->blueprint->name.c_str(), pl->aiController.weatherState == 1 ? "on" : "off", d[0], d[1], d[2], d[3], d[4], pl->aiController.weatherCensus().c_str());
						}
			}
		}
	}
	{
		const int alias = server.gameSet->aliases.names.getIndex("Deliverable Buildings");
		std::string list;
		if (alias >= 0)
			for (auto& ref : server.aliases[alias])
				if (CommonGameObject* obj = ref.getFrom(&server))
					list += " " + obj->blueprint->name + "/" + std::to_string(obj->getPlayer()->id);
		printf("AITEST alias Deliverable Buildings:%s\n", list.c_str());
	}
	const std::vector<std::string> standing = census("end");
	server.stopRecording();
	// "save": "<path>.sav": the game at its end, e.g. a late game for the client's benchmark
	if (const std::string save = cfg.value("save", std::string()); !save.empty())
		printf("AITEST saved to %s: %d\n", save.c_str(), (int)server.saveGame(save.c_str()));
	{
		// every object's id, position, orientation and items (in any order), and the random state:
		// a replay must end with the same
		size_t objects = 0;
		auto fnv = [](const void* data, size_t n, uint64_t h) {
			for (size_t i = 0; i < n; i++) { h ^= ((const uint8_t*)data)[i]; h *= 1099511628211ull; }
			return h;
		};
		uint64_t sum = 0;
		// (not the height, which loading sets for cities, nor items at 0, which a save leaves out:
		// neither changes the game)
		auto walk = [&](CommonGameObject* obj, auto& self) -> void {
			objects++;
			uint64_t h = fnv(&obj->id, sizeof(obj->id), 1469598103934665603ull);
			h = fnv(&obj->position.x, sizeof(float), h);
			h = fnv(&obj->position.z, sizeof(float), h);
			h = fnv(&obj->orientation, sizeof(obj->orientation), h);
			for (const auto& [item, value] : obj->items)
				if (value != 0.0f)
					h += fnv(&value, sizeof(value), fnv(&item, sizeof(item), 1469598103934665603ull));
			sum += h;
			for (const auto& [type, children] : obj->children)
				for (CommonGameObject* child : children)
					self(child, self);
		};
		walk(server.getLevel(), walk);
		// "WKB_HASH_DUMP=<file>": each object's state, to find where two runs differ
		if (const char* dump = getenv("WKB_HASH_DUMP"))
			if (FILE* f = fopen(dump, "w")) {
				auto write = [&](CommonGameObject* obj, auto& self) -> void {
					fprintf(f, "%u %s pos %.4f %.4f %.4f ori %.4f", obj->id, obj->blueprint->name.c_str(), obj->position.x, obj->position.y, obj->position.z, obj->orientation.y);
					std::map<int, float> sorted(obj->items.begin(), obj->items.end());
					for (const auto& [item, value] : sorted)
						fprintf(f, " %d=%g", item, value);
					fprintf(f, "\n");
					for (const auto& [type, children] : obj->children)
						for (CommonGameObject* child : children)
							self(child, self);
				};
				write(server.getLevel(), write);
				fclose(f);
			}
		printf("AITEST hash %016llx (%zu objects, random state %u, t=%.2f)\n", (unsigned long long)(sum ^ g_simRandom.state), objects, g_simRandom.state, server.timeManager.currentTime);
	}
	const double wall = (SDL_GetTicks() - start) / 1000.0;
	std::string list;
	for (const std::string& n : standing)
		list += " " + n;
	printf("AITEST end after %.0f game s in %.1f real s (%.1fx real time); standing:%s\n", server.timeManager.currentTime, wall,
		server.timeManager.currentTime / std::max(wall, 0.001), list.c_str());
	printf("AITEST walking in place: %d stands\n", walkingInPlace);
	if (cfg.value("corpses", false))
		printf("AITEST corpses %zu, moved %d\n", corpsesSeen.size(), corpsesMoved);
	// the players no longer at war with each other (diplomacy: offers and the AIs' answers)
	std::string peace;
	std::vector<CommonGameObject*> players;
	if (CommonGameObject* level = server.getLevel())
		for (const auto& [type, children] : level->children)
			for (CommonGameObject* c : children)
				if (c->blueprint->bpClass == Tags::GAMEOBJCLASS_PLAYER && c->blueprint->name != "Neutral Player")
					players.push_back(c);
	for (size_t i = 0; i < players.size(); i++)
		for (size_t j = i + 1; j < players.size(); j++) {
			const int st = server.getDiplomaticStatus(players[i], players[j]);
			if (st != 2)
				peace += " " + std::to_string(players[i]->id) + "-" + std::to_string(players[j]->id) + ":" + server.gameSet->diplomaticStatuses.names.getString(st);
		}
	printf("AITEST end diplomacy:%s\n", peace.empty() ? " all at war" : peace.c_str());
	// the game's timeline told as the chronicle tells it ("chronicle": true), with the weather each
	// event began in, for the RE repo's checker (STORY lines like test 58's)
	if (cfg.value("chronicle", false)) {
		auto& chron = server.unitHistory.chronicle.chron;
		ChronicleNarrator::Context ctx;
		ctx.fromSettings(server.environment.settings, server.environment.enabled());
		ctx.playerName = [&](uint32_t id) { auto it = chron.playerNames.find(id); return it != chron.playerNames.end() ? it->second : std::to_string(id); };
		for (auto& e : chron.events) {
			// (the mentions, as the clients get them)
			const std::string text = ChronicleNarrator::plain(ChronicleNarrator::narrate(e, ctx));
			static const char* seasons[] = { "spring", "summer", "autumn", "winter" };
			const float el = e.time / std::max(ctx.seasonLengthS, 1.0f) + ctx.yearShift * 4.0f + ctx.startSeason;
			printf("STORY %s|%s|-|%s|%s\n", ctx.seasons ? seasons[(int)el % 4] : "-", e.weather >= 0 ? WeatherName(e.weather) : "-", GameChronicle::kindName(e.kind), text.c_str());
			printf("AITEST chronicle %s  %s\n", ChronicleNarrator::dateLine(e.time, ctx).c_str(), e.toText().c_str());
		}
	}
	fflush(stdout);
}

// glTF round trip: the game's Mesh3 models (all under Warrior Kings Game Set, up to 400) written as
// .glb and read back; the triangles, positions, normals, UVs and textures must come back. Prints
// "GLTF meshes <n> identical <m>" and the worst position error.
void Test_GltfRoundTrip()
{
	LoadBCP("data.bcp");
	std::vector<std::string> meshes;
	auto walk = [&](const std::string& dir, auto& self) -> void {
		if (meshes.size() >= 400)
			return;
		std::unique_ptr<std::vector<std::string>> files(ListFiles(dir.c_str()));
		for (const std::string& f : *files)
			if (f.size() > 6 && StrCICompare(f.substr(f.size() - 6).c_str(), ".mesh3") == 0)
				meshes.push_back(dir + "\\" + f);
		std::unique_ptr<std::vector<std::string>> dirs(ListDirectories(dir.c_str()));
		for (const std::string& d : *dirs)
			self(dir + "\\" + d, self);
	};
	walk("Warrior Kings Game Set", walk);
	const std::filesystem::path out = std::filesystem::temp_directory_path() / "wkbre2-gltf-test.glb";
	const std::filesystem::path saved = g_userPath / "saved" / "wkbre2-gltf-test.glb";
	std::filesystem::create_directories(saved.parent_path());
	int identical = 0;
	float worst = 0;
	for (const std::string& path : meshes) {
		Mesh m;
		m.load(path.c_str());
		if (!m.saveGltf(out.string().c_str()))
			continue;
		std::filesystem::copy_file(out, saved, std::filesystem::copy_options::overwrite_existing);
		Mesh back;
		back.loadGltf("wkbre2-gltf-test.glb");
		// every triangle of the most detailed list, corner by corner
		bool same = back.materials.size() == m.materials.size();
		size_t tris = 0, backTris = 0;
		for (size_t g = 0; same && g < m.groupIndices.size(); g++) {
			const auto& a = m.polyLists[0].groups[g].tupleIndex;
			const auto& b = back.polyLists[0].groups[g].tupleIndex;
			tris += a.size();
			backTris += b.size();
			same = a.size() == b.size() && m.materials[g].textureFilename == back.materials[g].textureFilename
				&& m.materials[g].alphaTest == back.materials[g].alphaTest;
			for (size_t t = 0; same && t < a.size(); t++)
				for (int k = 0; k < 3; k++) {
					const IndexTuple& ta = m.groupIndices[g][a[t][k]];
					const IndexTuple& tb = back.groupIndices[g][b[t][k]];
					for (int c = 0; c < 3; c++)
						worst = std::max(worst, std::abs(m.vertices[ta.vertex * 3 + c] - back.vertices[tb.vertex * 3 + c]));
					same = same && m.normals[ta.normal] == back.normals[tb.normal]
						&& m.uvLists[0][ta.uv * 2] == back.uvLists[0][tb.uv * 2] && m.uvLists[0][ta.uv * 2 + 1] == back.uvLists[0][tb.uv * 2 + 1];
				}
		}
		same = same && worst < 1e-4f;
		identical += same;
		if (!same)
			printf("GLTF %s differs (%zu vs %zu triangles)\n", path.c_str(), tris, backTris);
	}
	std::filesystem::remove(saved);
	printf("GLTF meshes %zu identical %d, worst position error %g\n", meshes.size(), identical, worst);
	fflush(stdout);
}

// Team colours of HD models (docs/engine/gltf.md): a hand-made glTF of three quads, A "wkTeam",
// B "wkEmblem" with its TEXCOORD_1 the emblem quad, C plain with a TEXCOORD_1 of its own (UV list
// 1, the original's way); read, written as .glb and read back; the emblem slots cut and fit; team
// materials apart from plain ones of the same texture. Prints "TEAM <check> <0/1>" lines and
// "TEAM all <0/1>". No game files needed.
void Test_TeamColours()
{
	bool all = true;
	auto check = [&](const char* what, bool ok) { printf("TEAM %s %d\n", what, ok ? 1 : 0); all &= ok; };

	// the buffer: per quad its positions, normals, UV 0, UV 1 (B and C), indices
	std::vector<uint8_t> bin;
	auto put = [&](const void* p, size_t n) { const size_t at = bin.size(); bin.insert(bin.end(), (const uint8_t*)p, (const uint8_t*)p + n); while (bin.size() % 4) bin.push_back(0); return at; };
	nlohmann::json views = nlohmann::json::array(), accessors = nlohmann::json::array(), prims = nlohmann::json::array();
	auto accessor = [&](size_t at, size_t len, int ctype, size_t count, const char* type) {
		views.push_back({ { "buffer", 0 }, { "byteOffset", at }, { "byteLength", len } });
		accessors.push_back({ { "bufferView", (int)views.size() - 1 }, { "componentType", ctype }, { "count", count }, { "type", type } });
		return (int)accessors.size() - 1;
	};
	for (int q = 0; q < 3; q++) {
		const float x = q * 2.0f;
		const float pos[12] = { x, 0, 0, x + 1, 0, 0, x + 1, 1, 0, x, 1, 0 };
		const float nrm[12] = { 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1 };
		const float uv0[8] = { 0.25f, 0.25f, 0.5f, 0.25f, 0.5f, 0.5f, 0.25f, 0.5f };
		const float uv1[8] = { 0, 1, 1, 1, 1, 0, 0, 0 };
		const uint16_t idx[6] = { 0, 1, 2, 0, 2, 3 };
		nlohmann::json attrs = { { "POSITION", accessor(put(pos, 48), 48, 5126, 4, "VEC3") }, { "NORMAL", accessor(put(nrm, 48), 48, 5126, 4, "VEC3") },
			{ "TEXCOORD_0", accessor(put(uv0, 32), 32, 5126, 4, "VEC2") } };
		if (q > 0)
			attrs["TEXCOORD_1"] = accessor(put(uv1, 32), 32, 5126, 4, "VEC2");
		prims.push_back({ { "attributes", attrs }, { "indices", accessor(put(idx, 12), 12, 5123, 6, "SCALAR") }, { "material", q } });
	}
	std::string b64;
	{
		static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
		for (size_t i = 0; i < bin.size(); i += 3) {
			uint32_t v = (uint32_t)bin[i] << 16;
			int n = 1;
			if (i + 1 < bin.size()) { v |= (uint32_t)bin[i + 1] << 8; n++; }
			if (i + 2 < bin.size()) { v |= bin[i + 2]; n++; }
			for (int k = 0; k < 4; k++)
				b64 += k <= n ? t[(v >> (18 - 6 * k)) & 63] : '=';
		}
	}
	nlohmann::json mat = { { "pbrMetallicRoughness", { { "baseColorFactor", { 1, 1, 1, 1 } } } } };
	nlohmann::json matA = mat, matB = mat, matC = mat;
	matA["extras"] = { { "wkTexture", "TeamTest.tga" }, { "wkTeam", true } };
	matB["extras"] = { { "wkTexture", "TeamTest.tga" }, { "wkEmblem", true } };
	matC["extras"] = { { "wkTexture", "TeamTest.tga" } };
	nlohmann::json doc = { { "asset", { { "version", "2.0" } } }, { "buffers", { { { "byteLength", bin.size() }, { "uri", "data:application/octet-stream;base64," + b64 } } } },
		{ "bufferViews", views }, { "accessors", accessors }, { "materials", { matA, matB, matC } },
		{ "meshes", { { { "primitives", prims } } } }, { "nodes", { { { "mesh", 0 } } } }, { "scenes", { { { "nodes", { 0 } } } } }, { "scene", 0 } };
	const std::filesystem::path dir = std::filesystem::temp_directory_path();
	const std::filesystem::path gltf = dir / "wkbai-team-test.gltf", glb = dir / "wkbai-team-test.glb";
	{ std::ofstream f(gltf, std::ios::binary); f << doc.dump(); }

	auto verify = [&](const char* pass, Mesh& m) {
		char n[64];
		auto name = [&](const char* what) { snprintf(n, sizeof(n), "%s %s", pass, what); return n; };
		check(name("materials"), m.materials.size() == 3 && m.materials[0].team && !m.materials[0].emblem
			&& !m.materials[1].team && m.materials[1].emblem && !m.materials[2].team && !m.materials[2].emblem);
		// B's TEXCOORD_1 is its emblem, not UV list 1; C's is list 1 (B's list 1 is its list 0)
		bool lists = m.uvLists.size() == 2, emblem = m.emblemUvs.size() == (size_t)m.numUvs * 2;
		for (int g = 0; g < 3 && lists && emblem; g++)
			for (const IndexTuple& t : m.groupIndices[g]) {
				const float u0 = m.uvLists[0][t.uv * 2], u1 = m.uvLists[1][t.uv * 2], e = m.emblemUvs[t.uv * 2], ev = m.emblemUvs[t.uv * 2 + 1];
				if (g == 0) emblem &= e == -1.0f && ev == -1.0f, lists &= u1 == u0;
				if (g == 1) emblem &= (e == 0.0f || e == 1.0f) && (ev == 0.0f || ev == 1.0f), lists &= u1 == u0;
				if (g == 2) emblem &= e == -1.0f, lists &= (u1 == 0.0f || u1 == 1.0f) && u0 != u1;
			}
		check(name("uv lists"), lists);
		check(name("emblem uvs"), emblem);
	};
	Mesh a;
	a.loadGltf(gltf.string().c_str());
	verify("gltf", a);
	// written and read back (the .glb keeps the flags; the emblem's UVs only with one UV list:
	// here C's list 1 is kept and B's emblem UVs aren't)
	const bool saved = a.saveGltf(glb.string().c_str());
	Mesh back;
	back.loadGltf(glb.string().c_str());
	check("glb flags", saved && back.materials.size() == 3 && back.materials[0].team && back.materials[1].emblem && !back.materials[2].team);
	{
		// one UV list: a mesh of A and B only, round trip with the emblem's UVs
		nlohmann::json d2 = doc;
		d2["meshes"][0]["primitives"].erase(2);
		d2["materials"].erase(2);
		const std::filesystem::path g2 = dir / "wkbai-team-test2.gltf", b2 = dir / "wkbai-team-test2.glb";
		{ std::ofstream f(g2, std::ios::binary); f << d2.dump(); }
		Mesh m2, m2back;
		m2.loadGltf(g2.string().c_str());
		const bool ok = m2.uvLists.size() == 1 && m2.saveGltf(b2.string().c_str());
		m2back.loadGltf(b2.string().c_str());
		bool same = ok && m2back.emblemUvs.size() == m2back.numUvs * 2 && m2back.uvLists.size() == 1 && m2back.materials.size() == 2 && m2back.materials[1].emblem;
		for (size_t g = 0; same && g < 2; g++)
			for (size_t i = 0; same && i < m2.groupIndices[g].size(); i++)
				same = m2.emblemUvs[m2.groupIndices[g][i].uv * 2] == m2back.emblemUvs[m2back.groupIndices[g][i].uv * 2]
					&& m2.emblemUvs[m2.groupIndices[g][i].uv * 2 + 1] == m2back.emblemUvs[m2back.groupIndices[g][i].uv * 2 + 1];
		check("glb emblem uvs", same);
		std::filesystem::remove(g2);
		std::filesystem::remove(b2);
	}
	std::filesystem::remove(gltf);
	std::filesystem::remove(glb);

	// colours: 8 of them, player 8 and Gaia on list 0 as the UV lists have it
	check("colour index", TeamColours::index(0) == 0 && TeamColours::index(3) == 3 && TeamColours::index(8) == 0 && TeamColours::index(-1) == 7);

	// emblems: a 2 x 4 opaque image fit centred (128 wide, transparent at the sides)
	{
		std::vector<uint8_t> img(2 * 4 * 4, 0);
		for (int i = 0; i < 8; i++) { img[i * 4] = 200; img[i * 4 + 3] = 255; }
		const std::vector<uint8_t> f = TeamEmblems::fit(img.data(), 2, 4);
		const int S = TeamEmblems::SIZE;
		auto px = [&](int x, int y, int c) { return (int)f[((size_t)y * S + x) * 4 + c]; };
		check("fit", f.size() == (size_t)S * S * 4 && px(S / 2, S / 2, 3) == 255 && px(S / 2, S / 2, 0) == 200
			&& px(10, S / 2, 3) == 0 && px(S - 10, S / 2, 3) == 0 && px(S / 4 + 2, 0, 3) == 255);
	}
	// a banner texture with column k's emblem area of value 10k + 5: slot k gets it
	{
		std::vector<uint8_t> banner(256 * 256 * 4, 0);
		for (int y = 0; y < 256; y++)
			for (int x = 0; x < 256; x++) {
				uint8_t* p = &banner[((size_t)y * 256 + x) * 4];
				p[0] = (uint8_t)(10 * (x / 32) + 5); p[1] = (uint8_t)y; p[3] = 255;
			}
		bool ok = true;
		for (int k = 0; k < 8; k++) {
			const std::vector<uint8_t> e = TeamEmblems::cutBanner(banner.data(), 256, 256, k);
			const int S = TeamEmblems::SIZE;
			const uint8_t* mid = &e[((size_t)(S / 2) * S + S / 2) * 4];
			const uint8_t* top = &e[((size_t)1 * S + S / 2) * 4];
			ok &= mid[0] == 10 * k + 5 && mid[3] == 255 && top[1] >= 22 && top[1] <= 23 && e[3] == 0;
		}
		check("banner cut", ok);
	}
	// a player's own emblem (the run-time entry point): the slot's generation moves on
	{
		TeamEmblems& te = TeamEmblems::get();
		const uint32_t g0 = te.generation(3);
		Bitmap own;
		own.width = own.height = 4;
		own.format = BMFORMAT_R8G8B8A8;
		own.pixels.resize(64);
		for (int i = 0; i < 16; i++) { own.pixels[i * 4 + 1] = 255; own.pixels[i * 4 + 3] = 255; }
		const bool set = te.setEmblem(3, own);
		const bool changed = te.generation(3) != g0 && te.pixels(3)[((size_t)128 * TeamEmblems::SIZE + 128) * 4 + 1] == 255;
		const bool bad = !te.setEmblem(8, own) && !te.setEmblem(-1, own);
		te.resetEmblem(3);
		check("set emblem", set && changed && bad && te.generation(3) != g0 + 1);
	}
	// a team material is not the plain one of the same texture and colour
	{
		ModelCache cache;
		auto build = [](bool team) {
			return [team](Mesh& m) {
				Mesh::BuildGroup g;
				g.mat.textureFilename = "TeamTest.tga";
				g.mat.alphaTest = false;
				g.mat.team = team;
				g.triangles = { 0, 1, 2 };
				m.build({ 0, 0, 0, 1, 0, 0, 0, 1, 0 }, { Vector3(0, 0, 1), Vector3(0, 0, 1), Vector3(0, 0, 1) }, { 0, 0, 1, 0, 0, 1 }, { g });
			};
		};
		Model* plain = cache.getGeneratedModel("team test plain", build(false));
		Model* team = cache.getGeneratedModel("team test team", build(true));
		const uint32_t ip = plain->getMaterialIds()[0], it = team->getMaterialIds()[0];
		check("material ids", ip != it && !cache.getMaterial(ip).team && cache.getMaterial(it).team);
	}
	printf("TEAM all %d\n", all ? 1 : 0);
	fflush(stdout);
}

// Chimney smoke (gfx/ChimneySmoke.cpp, docs/engine/gltf.md "Chimney smoke"): the glTF's wkSmoke
// nodes, the Mesh3's PS_Chimney points and their rule by building, the houses' table, the rules
// by day, night and season, the puffs (the same every time, rising, downwind, fading). No game files.
void Test_ChimneySmoke()
{
	bool all = true;
	auto check = [&](const char* what, bool ok) { printf("SMOKE %s %d\n", what, ok ? 1 : 0); all &= ok; };
	auto closeTo = [](const Vector3& a, const Vector3& b) { return (a - b).sqlen3() < 1e-4f; };

	// a glTF: one triangle, and smoke nodes: A the defaults (true) at (1, 5, 2); B under a parent
	// moved by (10, 0, 0): always, rate 2, size 1.5 at (0, 8, -3); C "day" with an unknown key; and
	// a plain node without wkSmoke
	const float pos[9] = { 0, 0, 0, 1, 0, 0, 0, 1, 0 };
	std::string b64;
	{
		static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
		const uint8_t* bin = (const uint8_t*)pos;
		const size_t n = sizeof(pos);
		for (size_t i = 0; i < n; i += 3) {
			uint32_t v = (uint32_t)bin[i] << 16;
			int k = 1;
			if (i + 1 < n) { v |= (uint32_t)bin[i + 1] << 8; k++; }
			if (i + 2 < n) { v |= bin[i + 2]; k++; }
			for (int j = 0; j < 4; j++)
				b64 += j <= k ? t[(v >> (18 - 6 * j)) & 63] : '=';
		}
	}
	auto makeDoc = [&](const nlohmann::json& nodes, const nlohmann::json& roots) {
		return nlohmann::json{ { "asset", { { "version", "2.0" } } },
			{ "buffers", { { { "byteLength", sizeof(pos) }, { "uri", "data:application/octet-stream;base64," + b64 } } } },
			{ "bufferViews", { { { "buffer", 0 }, { "byteOffset", 0 }, { "byteLength", sizeof(pos) } } } },
			{ "accessors", { { { "bufferView", 0 }, { "componentType", 5126 }, { "count", 3 }, { "type", "VEC3" } } } },
			{ "materials", { { { "extras", { { "wkTexture", "SmokeTest.tga" } } } } } },
			{ "meshes", { { { "primitives", { { { "attributes", { { "POSITION", 0 } } }, { "material", 0 } } } } } } },
			{ "nodes", nodes }, { "scenes", { { { "nodes", roots } } } }, { "scene", 0 } };
	};
	const nlohmann::json nodes = {
		{ { "mesh", 0 } },
		{ { "translation", { 1, 5, 2 } }, { "extras", { { "wkSmoke", true } } } },
		{ { "translation", { 10, 0, 0 } }, { "children", { 3 } } },
		{ { "translation", { 0, 8, -3 } }, { "extras", { { "wkSmoke", { { "when", "always" }, { "rate", 2.0 }, { "size", 1.5 } } } } } },
		{ { "translation", { 0, 3, 0 } }, { "extras", { { "wkSmoke", { { "when", "day" }, { "colour", "red" } } } } } },
		{ { "translation", { 0, 9, 0 } } },
	};
	const std::filesystem::path dir = std::filesystem::temp_directory_path();
	const std::filesystem::path g1 = dir / "wkbai-smoke-test.gltf", g2 = dir / "wkbai-smoke-none.gltf";
	{ std::ofstream f(g1, std::ios::binary); f << makeDoc(nodes, { 0, 1, 2, 4, 5 }).dump(); }
	{
		const nlohmann::json off = { { { "mesh", 0 } }, { { "extras", { { "wkSmoke", false } } } } };
		std::ofstream f(g2, std::ios::binary); f << makeDoc(off, { 0, 1 }).dump();
	}
	Mesh m;
	m.loadGltf(g1.string().c_str());
	check("declared", m.smokeDeclared && m.smokeEmitters.size() == 3);
	if (m.smokeEmitters.size() == 3) {
		const SmokeEmitter &a = m.smokeEmitters[0], &b = m.smokeEmitters[1], &c = m.smokeEmitters[2];
		printf("SMOKE emitters (%.2f %.2f %.2f) %s, (%.2f %.2f %.2f) %s rate %.1f size %.1f, (%.2f %.2f %.2f) %s\n",
			a.position.x, a.position.y, a.position.z, SmokeEmitter::whenName(a.when), b.position.x, b.position.y, b.position.z,
			SmokeEmitter::whenName(b.when), b.rate, b.size, c.position.x, c.position.y, c.position.z, SmokeEmitter::whenName(c.when));
		// (glTF's z is the game's -z)
		check("default node", closeTo(a.position, Vector3(1, 5, -2)) && a.when == SmokeEmitter::NIGHT_WINTER && a.rate == 1.0f && a.size == 1.0f);
		check("child node", closeTo(b.position, Vector3(10, 8, 3)) && b.when == SmokeEmitter::ALWAYS && b.rate == 2.0f && b.size == 1.5f);
		check("day node", closeTo(c.position, Vector3(0, 3, 0)) && c.when == SmokeEmitter::DAY);
	}
	Mesh none;
	none.loadGltf(g2.string().c_str());
	check("declared none", none.smokeDeclared && none.smokeEmitters.empty());
	std::filesystem::remove(g1);
	std::filesystem::remove(g2);
	SmokeEmitter::When w;
	check("when names", SmokeEmitter::parseWhen("always", w) && w == SmokeEmitter::ALWAYS && SmokeEmitter::parseWhen("day", w) && w == SmokeEmitter::DAY
		&& SmokeEmitter::parseWhen("night_winter", w) && w == SmokeEmitter::NIGHT_WINTER && !SmokeEmitter::parseWhen("dusk", w) && w == SmokeEmitter::NIGHT_WINTER);

	// the Mesh3's way: PS_Chimney points, their rule by the building (the Blacksmith always); the
	// houses from the table; a glTF's declaration replaces the points
	{
		ModelCache cache;
		auto triangle = [](Mesh& mesh) {
			Mesh::BuildGroup g;
			g.mat.textureFilename = "SmokeTest.tga";
			g.mat.alphaTest = false;
			g.triangles = { 0, 1, 2 };
			mesh.build({ 0, 0, 0, 1, 0, 0, 0, 1, 0 }, { Vector3(0, 0, 1), Vector3(0, 0, 1), Vector3(0, 0, 1) }, { 0, 0, 1, 0, 0, 1 }, { g });
		};
		auto withPoints = [triangle](bool declared) {
			return [declared, triangle](Mesh& mesh) {
				triangle(mesh);
				mesh.attachPoints.resize(3);
				const char* tags[3] = { "PS_Chimney", "SP_Spawn", "PS_CHIMNEY" };
				for (int i = 0; i < 3; i++) {
					mesh.attachPoints[i].tag = tags[i];
					mesh.attachPoints[i].staticState.position = Vector3((float)i, 10.0f, 0.0f);
					mesh.attachPoints[i].staticState.on = true;
				}
				mesh.numAttachPoints = 3;
				if (declared) {
					SmokeEmitter e;
					e.position = Vector3(0, 12, 0);
					e.when = SmokeEmitter::DAY;
					mesh.smokeEmitters = { e };
					mesh.smokeDeclared = true;
				}
			};
		};
		Model* smith = cache.getGeneratedModel("Warrior Kings Game Set\\Buildings\\Blacksmith\\Fully Built\\DEFAULT.MESH3", withPoints(false));
		Model* manor = cache.getGeneratedModel("Warrior Kings Game Set/Buildings/Manor/Fully Built/DEFAULT.MESH3", withPoints(false));
		Model* hd = cache.getGeneratedModel("Warrior Kings Game Set\\Buildings\\Tavern\\Wood\\Fully Built\\DEFAULT.MESH3", withPoints(true));
		Model* house = cache.getGeneratedModel("Warrior Kings Game Set\\Buildings\\Houses\\house02\\DEFAULT.MESH3", triangle);
		Model* ruin = cache.getGeneratedModel("Warrior Kings Game Set\\Buildings\\Houses\\house02\\Ruined\\DEFAULT.MESH3", triangle);
		const auto& es = ChimneySmoke::EmittersOf(smith);
		const auto& em = ChimneySmoke::EmittersOf(manor);
		const auto& eh = ChimneySmoke::EmittersOf(hd);
		const auto& ehouse = ChimneySmoke::EmittersOf(house);
		printf("SMOKE emitters of blacksmith %zu, manor %zu, HD tavern %zu, house %zu, ruin %zu, counted\n", es.size(), em.size(), eh.size(), ehouse.size(), ChimneySmoke::EmittersOf(ruin).size());
		check("blacksmith always", es.size() == 2 && es[0].when == SmokeEmitter::ALWAYS && es[1].when == SmokeEmitter::ALWAYS
			&& std::abs(es[1].position.x - 2.0f) < 1e-4f && es[0].position.y > 10.0f);
		check("manor night and winter", em.size() == 2 && em[0].when == SmokeEmitter::NIGHT_WINTER);
		check("glTF replaces the points", eh.size() == 1 && eh[0].when == SmokeEmitter::DAY && closeTo(eh[0].position, Vector3(0, 12, 0)));
		check("house table", ehouse.size() == 1 && ehouse[0].when == SmokeEmitter::NIGHT_WINTER && ehouse[0].position.y > 6.5f
			&& ChimneySmoke::EmittersOf(ruin).empty());
		check("cached", &ChimneySmoke::EmittersOf(smith) == &es);
		ChimneySmoke::Clear();
		ChimneySmoke::Add(smith, Matrix::getTranslationMatrix(Vector3(100, 0, 50)), 7);
		ChimneySmoke::Add(house, Matrix::getTranslationMatrix(Vector3(0, 0, 0)), 8);
		check("added", ChimneySmoke::PendingCount() == 3);
		ChimneySmoke::Clear();
	}

	// the rules
	{
		EnvironmentView v;
		v.active = false;
		const bool off = ChimneySmoke::Strength(SmokeEmitter::NIGHT_WINTER, v) == 1.0f && ChimneySmoke::Strength(SmokeEmitter::DAY, v) == 1.0f
			&& ChimneySmoke::Strength(SmokeEmitter::ALWAYS, v) == 1.0f;
		check("environment off: all smoke", off);
		v.active = true;
		auto at = [&](float daylight, float yearFraction, float temperature, SmokeEmitter::When when) {
			v.sun.daylight = daylight; v.yearFraction = yearFraction; v.temperatureC = temperature;
			return ChimneySmoke::Strength(when, v);
		};
		const float sNoon[3] = { at(1, 0.4f, 20, SmokeEmitter::NIGHT_WINTER), at(1, 0.4f, 20, SmokeEmitter::ALWAYS), at(1, 0.4f, 20, SmokeEmitter::DAY) };
		const float sNight[3] = { at(0, 0.4f, 15, SmokeEmitter::NIGHT_WINTER), at(0, 0.4f, 15, SmokeEmitter::ALWAYS), at(0, 0.4f, 15, SmokeEmitter::DAY) };
		const float sWinter[3] = { at(1, 0.87f, -3, SmokeEmitter::NIGHT_WINTER), at(1, 0.87f, -3, SmokeEmitter::ALWAYS), at(1, 0.87f, -3, SmokeEmitter::DAY) };
		const float dusk = at(0.35f, 0.4f, 15, SmokeEmitter::NIGHT_WINTER);
		const float coldSpring = at(1, 0.05f, -2, SmokeEmitter::NIGHT_WINTER), mildWinter = at(1, 0.87f, 8, SmokeEmitter::NIGHT_WINTER);
		printf("SMOKE strength noon %.2f %.2f %.2f, midnight %.2f %.2f %.2f, winter noon %.2f %.2f %.2f, dusk %.2f, cold spring %.2f, mild winter %.2f\n",
			sNoon[0], sNoon[1], sNoon[2], sNight[0], sNight[1], sNight[2], sWinter[0], sWinter[1], sWinter[2], dusk, coldSpring, mildWinter);
		check("summer noon", sNoon[0] == 0.0f && sNoon[1] == 1.0f && sNoon[2] == 1.0f);
		check("summer midnight", sNight[0] == 1.0f && sNight[1] == 1.0f && sNight[2] == 0.0f);
		check("winter noon", sWinter[0] == 1.0f && sWinter[1] == 1.0f && sWinter[2] == 1.0f);
		check("dusk between", dusk > 0.0f && dusk < 1.0f);
		check("cold days and winter", coldSpring > 0.5f && mildWinter == 1.0f);
	}

	// the puffs
	{
		SmokeEmitter e;
		const Vector3 at(100, 20, 100), wind(3, 0, 0);
		std::vector<ChimneySmoke::Puff> p1, p2, calm, zero, half;
		ChimneySmoke::Puffs(e, at, 1234, 500.0f, 1.0f, wind, p1);
		ChimneySmoke::Puffs(e, at, 1234, 500.0f, 1.0f, wind, p2);
		ChimneySmoke::Puffs(e, at, 1234, 500.0f, 1.0f, Vector3(0, 0, 0), calm);
		ChimneySmoke::Puffs(e, at, 1234, 500.0f, 0.0f, wind, zero);
		ChimneySmoke::Puffs(e, at, 1234, 500.0f, 0.4f, wind, half);
		bool same = p1.size() == p2.size();
		for (size_t i = 0; same && i < p1.size(); i++)
			same = closeTo(p1[i].position, p2[i].position) && p1[i].alpha == p2[i].alpha;
		check("the same every time", same && !p1.empty());
		const float expected = 1.3f * ChimneySmoke::LIFE;
		printf("SMOKE %zu puffs (about %.1f expected), %zu at strength 0.4\n", p1.size(), expected, half.size());
		check("puff count", p1.size() >= expected - 2 && p1.size() <= expected + 2);
		// the oldest (first) higher, bigger and further downwind than the youngest (last)
		bool order = p1.size() > 2;
		bool fades = true, above = true;
		float meanDx = 0, meanCalm = 0;
		for (auto& p : p1) {
			fades &= p.alpha > 0.0f && p.alpha <= 0.8f;
			above &= p.position.y >= at.y;
			meanDx += (p.position.x - at.x) / p1.size();
		}
		for (auto& p : calm)
			meanCalm += (p.position.x - at.x) / calm.size();
		if (order) {
			const auto &old = p1.front(), &young = p1.back();
			order = old.age > young.age && old.position.y > young.position.y + 3.0f && old.size > young.size * 2.0f && old.position.x > young.position.x + 5.0f;
			printf("SMOKE oldest %.1f s at +%.1f up, +%.1f downwind, %.1f m, alpha %.2f; youngest %.1f s at +%.1f up, %.1f m, alpha %.2f\n",
				old.age, old.position.y - at.y, old.position.x - at.x, old.size, old.alpha, young.age, young.position.y - at.y, young.size, young.alpha);
		}
		check("rise, grow and drift", order && above);
		check("downwind", meanDx > 3.0f && std::abs(meanCalm) < 1.5f);
		check("fade", fades);
		check("off: none", zero.empty());
		check("thinned", half.size() < p1.size() && !half.empty());
		// grey and white: each puff its own tone and texture puff; young darker, lighter as it thins;
		// wood smoke a warm grey, the forge's darker and cooler
		{
			float lo = 1, hi = 0;
			int seen = 0;
			for (auto& p : p1) {
				lo = std::min(lo, p.tone); hi = std::max(hi, p.tone);
				seen |= 1 << (p.variant & 3);
			}
			auto puff = [](float age, float tone) { ChimneySmoke::Puff f{}; f.age = age; f.tone = tone; return f; };
			const Vector3 grey = ChimneySmoke::Albedo(puff(0.5f, 0.0f), SmokeEmitter::NIGHT_WINTER);
			const Vector3 white = ChimneySmoke::Albedo(puff(0.5f, 1.0f), SmokeEmitter::NIGHT_WINTER);
			const Vector3 young = ChimneySmoke::Albedo(puff(0.5f, 0.5f), SmokeEmitter::NIGHT_WINTER);
			const Vector3 old = ChimneySmoke::Albedo(puff(6.0f, 0.5f), SmokeEmitter::NIGHT_WINTER);
			const Vector3 forge = ChimneySmoke::Albedo(puff(0.5f, 0.5f), SmokeEmitter::ALWAYS);
			printf("SMOKE tones %.2f-%.2f; wood young (%.2f %.2f %.2f), old (%.2f %.2f %.2f); forge young (%.2f %.2f %.2f)\n",
				lo, hi, young.x, young.y, young.z, old.x, old.y, old.z, forge.x, forge.y, forge.z);
			check("grey and white puffs", hi - lo > 0.5f && white.x > grey.x + 0.2f && seen == 15);
			check("darker young, lighter thinning", old.x > young.x + 0.1f && old.x <= 1.0f);
			check("wood smoke warm", young.x > young.z + 0.05f && young.y < young.x);
			check("the forge's darker and cooler", forge.x < young.x - 0.1f && forge.z >= forge.x);
		}
		// half a second later: the puffs moved on
		std::vector<ChimneySmoke::Puff> later;
		ChimneySmoke::Puffs(e, at, 1234, 500.5f, 1.0f, wind, later);
		check("moves on", !later.empty() && later.front().age != p1.front().age);
	}
	printf("SMOKE all %d\n", all ? 1 : 0);
	fflush(stdout);
}

// Drips (gfx/BloodDrip.cpp, docs/engine/gltf.md "Drips"): the glTF's wkDrip nodes (a fall, a "to"
// point, the defaults), the drops' rhythm (gaps 0.5 to 1.5 times 1 / rate), forming, falling under
// gravity, the splash and the pool, the same every time, few sprites. No game files.
void Test_BloodDrip()
{
	bool all = true;
	auto check = [&](const char* what, bool ok) { printf("DRIP %s %d\n", what, ok ? 1 : 0); all &= ok; };
	auto closeTo = [](const Vector3& a, const Vector3& b) { return (a - b).sqlen3() < 1e-4f; };

	const float pos[9] = { 0, 0, 0, 1, 0, 0, 0, 1, 0 };
	std::string b64;
	{
		static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
		const uint8_t* bin = (const uint8_t*)pos;
		const size_t n = sizeof(pos);
		for (size_t i = 0; i < n; i += 3) {
			uint32_t v = (uint32_t)bin[i] << 16;
			int k = 1;
			if (i + 1 < n) { v |= (uint32_t)bin[i + 1] << 8; k++; }
			if (i + 2 < n) { v |= bin[i + 2]; k++; }
			for (int j = 0; j < 4; j++)
				b64 += j <= k ? t[(v >> (18 - 6 * j)) & 63] : '=';
		}
	}
	// A: the defaults (true) at (1, 5, 2); B: under a parent moved by (10, 0, 0), a fall of 3.8 m,
	// rate 0.5, size 1.5, a colour; C: "to" a point (glTF space) below and aside; D: false (none)
	const nlohmann::json nodes = {
		{ { "mesh", 0 } },
		{ { "translation", { 1, 5, 2 } }, { "extras", { { "wkDrip", true } } } },
		{ { "translation", { 10, 0, 0 } }, { "children", { 3 } } },
		{ { "translation", { 0, 6, -2.4 } }, { "extras", { { "wkDrip", { { "fall", 3.8 }, { "rate", 0.5 }, { "size", 1.5 }, { "colour", { 0.5, 0.0, 0.1 } } } } } } },
		{ { "translation", { 0, 4, 0 } }, { "extras", { { "wkDrip", { { "to", { 0.5, 1, 0 } } } } } } },
		{ { "translation", { 0, 9, 0 } }, { "extras", { { "wkDrip", false } } } },
	};
	const nlohmann::json doc = { { "asset", { { "version", "2.0" } } },
		{ "buffers", { { { "byteLength", sizeof(pos) }, { "uri", "data:application/octet-stream;base64," + b64 } } } },
		{ "bufferViews", { { { "buffer", 0 }, { "byteOffset", 0 }, { "byteLength", sizeof(pos) } } } },
		{ "accessors", { { { "bufferView", 0 }, { "componentType", 5126 }, { "count", 3 }, { "type", "VEC3" } } } },
		{ "materials", { { { "extras", { { "wkTexture", "DripTest.tga" } } } } } },
		{ "meshes", { { { "primitives", { { { "attributes", { { "POSITION", 0 } } }, { "material", 0 } } } } } } },
		{ "nodes", nodes }, { "scenes", { { { "nodes", { 0, 1, 2, 4, 5 } } } } }, { "scene", 0 } };
	const std::filesystem::path g = std::filesystem::temp_directory_path() / "wkbai-drip-test.gltf";
	{ std::ofstream f(g, std::ios::binary); f << doc.dump(); }
	Mesh m;
	m.loadGltf(g.string().c_str());
	check("emitters", m.dripEmitters.size() == 3 && m.smokeEmitters.empty() && !m.smokeDeclared);
	if (m.dripEmitters.size() == 3) {
		const DripEmitter &a = m.dripEmitters[0], &b = m.dripEmitters[1], &c = m.dripEmitters[2];
		printf("DRIP emitters (%.2f %.2f %.2f)->(%.2f %.2f %.2f), (%.2f %.2f %.2f)->(%.2f %.2f %.2f) rate %.2f size %.1f, (%.2f %.2f %.2f)->(%.2f %.2f %.2f)\n",
			a.position.x, a.position.y, a.position.z, a.landing.x, a.landing.y, a.landing.z, b.position.x, b.position.y, b.position.z,
			b.landing.x, b.landing.y, b.landing.z, b.rate, b.size, c.position.x, c.position.y, c.position.z, c.landing.x, c.landing.y, c.landing.z);
		check("default node", closeTo(a.position, Vector3(1, 5, -2)) && closeTo(a.landing, Vector3(1, 4, -2)) && a.rate == 0.5f && a.size == 1.0f
			&& a.colour[0] > a.colour[1] && a.colour[0] < 0.5f);
		check("child node and fall", closeTo(b.position, Vector3(10, 6, 2.4f)) && closeTo(b.landing, Vector3(10, 2.2f, 2.4f)) && b.rate == 0.5f
			&& b.size == 1.5f && b.colour[0] == 0.5f && b.colour[2] == 0.1f);
		check("to a point", closeTo(c.position, Vector3(0, 4, 0)) && closeTo(c.landing, Vector3(0.5f, 1, 0)));
	}
	std::filesystem::remove(g);

	// the rhythm: one drop every 1 to 3 seconds at rate 0.5
	DripEmitter e;
	e.position = Vector3(0, 6, 0);
	e.landing = Vector3(0, 2.2f, 0);
	float minGap = 1e9f, maxGap = 0, sum = 0;
	const int n = 400;
	for (int i = 0; i < n; i++) {
		const float gap = BloodDrip::BirthOf(e, 99, i + 1) - BloodDrip::BirthOf(e, 99, i);
		minGap = std::min(minGap, gap); maxGap = std::max(maxGap, gap); sum += gap;
	}
	printf("DRIP gaps %.2f to %.2f s, mean %.2f s\n", minGap, maxGap, sum / n);
	check("rhythm", minGap >= 0.99f && maxGap <= 3.01f && std::abs(sum / n - 2.0f) < 0.1f && maxGap - minGap > 1.0f);

	// one drop through its life: forming at the tip, falling, splashing, pooling
	const float born = BloodDrip::BirthOf(e, 99, 1000);
	const float fall = std::sqrt(2.0f * 3.8f / BloodDrip::GRAVITY);
	auto at = [&](float t, BloodDrip::Sprite::Kind kind, std::vector<BloodDrip::Sprite>& found) {
		std::vector<BloodDrip::Sprite> every;
		BloodDrip::Sprites(e, e.position, e.landing, 99, t, every);
		found.clear();
		for (auto& s : every)
			if (s.kind == kind)
				found.push_back(s);
		return every.size();
	};
	std::vector<BloodDrip::Sprite> s1, s2, s3, s4, s5, s6;
	const float form = 0.45f / 0.5f;
	at(born + form * 0.5f, BloodDrip::Sprite::FORMING, s1);
	at(born + form + fall * 0.3f, BloodDrip::Sprite::FALLING, s2);
	at(born + form + fall * 0.8f, BloodDrip::Sprite::FALLING, s3);
	at(born + form + fall + 0.1f, BloodDrip::Sprite::SPLASH, s4);
	at(born + form + fall + 1.0f, BloodDrip::Sprite::POOL, s5);
	bool forming = false;
	for (auto& s : s1) forming |= s.position.y < e.position.y && s.position.y > e.position.y - 0.1f;
	check("forms at the tip", forming);
	bool falls = false;
	for (auto& a : s2) for (auto& b : s3)
		falls |= std::abs(a.position.y - (e.position.y - 0.5f * BloodDrip::GRAVITY * (fall * 0.3f) * (fall * 0.3f))) < 0.01f
			&& b.position.y < a.position.y - 1.0f && b.length > a.length && a.axis.y < -0.99f;
	check("falls under gravity", falls);
	bool splash = s4.size() >= 3;
	for (auto& s : s4) splash &= s.position.y >= e.landing.y && s.position.y < e.landing.y + 0.5f && (s.position - e.landing).len3() > 0.01f;
	check("splashes", splash);
	bool pool = false;
	for (auto& s : s5) pool |= std::abs(s.position.y - e.landing.y) < 0.05f && s.width > 0.1f && s.alpha > 0.1f;
	check("pools", pool);
	// the same every time, and a handful of sprites at any time
	std::vector<BloodDrip::Sprite> a1, a2;
	BloodDrip::Sprites(e, e.position, e.landing, 99, 777.7f, a1);
	BloodDrip::Sprites(e, e.position, e.landing, 99, 777.7f, a2);
	bool same = a1.size() == a2.size();
	for (size_t i = 0; same && i < a1.size(); i++)
		same = closeTo(a1[i].position, a2[i].position) && a1[i].alpha == a2[i].alpha;
	check("the same every time", same && !a1.empty());
	size_t most = 0;
	for (float t = 1000.0f; t < 1060.0f; t += 0.05f)
		most = std::max(most, at(t, BloodDrip::Sprite::POOL, s6));
	printf("DRIP at most %zu sprites at once\n", most);
	check("cheap", most <= 16);
	printf("DRIP all %d\n", all ? 1 : 0);
}

// Automatic walls: a Manor stamped down on open ground of 4 Peaks for player 1027, turned 0 and a
// quarter: the stockade foundations around it and the gatehouse, which must be on the door's side
// (+z turned with the building). Prints "AUTOWALL turn <t>: <n> wall pieces, gatehouse at (dx, dz)".
void Test_AutoWall()
{
	LoadBCP("data.bcp");
	Server server;
	server.loadSaveGame("Save_Games\\2 Player 4 Peaks.lvl");
	ServerGameObject* player = server.findObject(1027);
	const GameObjBlueprint* manor = server.gameSet->findBlueprint(Tags::GAMEOBJCLASS_BUILDING, "Manor");
	if (!player || !manor) {
		printf("AUTOWALL no player or Manor\n");
		return;
	}
	player->setItem(Tags::PDITEM_FOOD, 1e6f);
	player->setItem(Tags::PDITEM_WOOD, 1e6f);
	player->setItem(Tags::PDITEM_GOLD, 1e6f);
	// both on one spot: (300, 300) is partly ground no building may stand on (the placement rules,
	// as WKB.exe's), and the two plans are only looked at, not stamped down
	const Vector3 at[2] = { Vector3(300, 0, 420), Vector3(300, 0, 420) };
	const float turns[2] = { 0.0f, 1.5707963f };
	for (int k = 0; k < 2; k++) {
		const auto plan = StampdownPlan::getStampdownPlan(&server, player, manor, at[k], Vector3(0, turns[k], 0));
		int walls = 0;
		Vector3 gate(-1, -1, -1);
		for (const auto& c : plan.toCreate) {
			if (c.blueprint->name.find("Gatehouse") != std::string::npos)
				gate = c.position;
			else
				walls++;
		}
		printf("AUTOWALL turn %.2f: status %d, %d wall pieces, gatehouse at (%.0f, %.0f) from the Manor\n", turns[k], (int)plan.status, walls,
			gate.x - at[k].x, gate.z - at[k].z);
	}
	// the player's wall type: after the stone wall research ("Wall Index" 2) the ring is of
	// unaligned stone wall foundations and its gatehouse too, not palisades
	for (const float wallIndex : { 0.0f, 2.0f, 4.0f, 6.0f }) {
		const int item = server.gameSet->items.names.getIndex("Wall Index");
		if (item < 0)
			break;
		player->setItem(item, wallIndex);
		const auto plan = StampdownPlan::getStampdownPlan(&server, player, manor, at[0], Vector3(0, 0, 0));
		std::map<std::string, int> kinds;
		for (const auto& c : plan.toCreate)
			kinds[c.blueprint->name]++;
		printf("AUTOWALL wall index %.0f:", wallIndex);
		for (const auto& [name, n] : kinds)
			printf(" %d x %s,", n, name.c_str());
		printf("\n");
	}
	fflush(stdout);
}

// The wall ring as a city grows: a Manor stamped down on 4 Peaks for player 1027, then barracks
// overlapping its rectangle on one side and then on another, so that the ring gets steps with
// inner corners. After each, every tile of the city's border must have its one piece and no tile
// inside a wall. Prints the ring as a map (- | straight, o outer corner, i inner corner, + cross
// piece, G gatehouse, ? missing, ! not on the border, 2 two pieces, x turned so that its wall
// ends at a tile with no piece) and
// "WALLRING <stage>: <n> border tiles, <m> missing, <k> doubled, <j> inside, <l> turned wrong".
void Test_WallRing()
{
	LoadBCP("data.bcp");
	Server server;
	server.loadSaveGame("Save_Games\\2 Player 4 Peaks.lvl");
	ServerGameObject* player = server.findObject(1027);
	const GameObjBlueprint* manor = server.gameSet->findBlueprint(Tags::GAMEOBJCLASS_BUILDING, "Manor Foundation");
	const GameObjBlueprint* barracks = server.gameSet->findBlueprint(Tags::GAMEOBJCLASS_BUILDING, "Barracks Stone Foundation");
	if (!player || !manor || !barracks || !server.terrain || !server.tiles) {
		printf("WALLRING not set up\n");
		return;
	}
	player->setItem(Tags::PDITEM_FOOD, 1e6f);
	player->setItem(Tags::PDITEM_WOOD, 1e6f);
	player->setItem(Tags::PDITEM_GOLD, 1e6f);
	auto tileCentre = [](const GameObjBlueprint* bp, int tx, int tz) {
		return Vector3(tx * 5.0f + 2.5f + bp->footprint->originX, 0.0f, tz * 5.0f + 2.5f + bp->footprint->originZ);
	};
	auto check = [&](const char* stage, ServerGameObject* city) {
		const auto [tw, th] = server.terrain->getNumPlayableTiles();
		int x0 = 1 << 30, z0 = 1 << 30, x1 = -1, z1 = -1;
		for (const auto& r : city->cityRectangles) {
			x0 = std::min(x0, r.xStart - 1); z0 = std::min(z0, r.yStart - 1);
			x1 = std::max(x1, r.xEnd + 1); z1 = std::max(z1, r.yEnd + 1);
		}
		auto inside = [&](int x, int z) {
			for (const auto& r : city->cityRectangles)
				if (r.xStart <= x && x <= r.xEnd && r.yStart <= z && z <= r.yEnd)
					return true;
			return false;
		};
		// the pieces positioned on a tile and the one whose footprint takes it (a gatehouse)
		auto piecesOn = [&](int x, int z) {
			std::vector<ServerGameObject*> pieces;
			auto add = [&](ServerGameObject* o) {
				if (o && o->parent == city && o->blueprint->bpClass == Tags::GAMEOBJCLASS_BUILDING
					&& (o->blueprint->buildingType == Tags::BUILDINGTYPE_GATEHOUSE
						|| (o->blueprint->buildingType >= Tags::BUILDINGTYPE_WALL && o->blueprint->buildingType <= Tags::BUILDINGTYPE_WALL_CROSSROADS))
					&& std::find(pieces.begin(), pieces.end(), o) == pieces.end())
					pieces.push_back(o);
			};
			if (x >= 0 && z >= 0 && x < tw && z < th) {
				for (const auto& ref : server.tiles[z * tw + x].objList)
					add(ref.getFrom<Server>());
				add(server.tiles[z * tw + x].building.getFrom<Server>());
			}
			return pieces;
		};
		// where a piece's wall leaves its tile, from the models: a straight piece and a gatehouse
		// along their x, the corners along -x and -z, the cross piece all four ways; turned a
		// quarter at a time from x to z
		auto arms = [](const ServerGameObject* o) {
			std::vector<std::pair<int, int>> a;
			switch (o->blueprint->buildingType) {
			case Tags::BUILDINGTYPE_WALL_CORNER_IN: case Tags::BUILDINGTYPE_WALL_CORNER_OUT: a = { { -1, 0 }, { 0, -1 } }; break;
			case Tags::BUILDINGTYPE_WALL_CROSSROADS: a = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } }; break;
			default: a = { { 1, 0 }, { -1, 0 } }; break;
			}
			const int quarter = ((int)std::lround(o->orientation.y / 1.5707963f) % 4 + 4) % 4;
			for (auto& [dx, dz] : a)
				for (int q = 0; q < quarter; q++)
					std::tie(dx, dz) = std::make_pair(-dz, dx);
			return a;
		};
		int border = 0, missing = 0, doubled = 0, within = 0, loose = 0;
		for (int z = z1; z >= z0; z--) {
			std::string row;
			for (int x = x0; x <= x1; x++) {
				const std::vector<ServerGameObject*> pieces = piecesOn(x, z);
				bool onBorder = false;
				for (int dz = -1; dz <= 1; dz++)
					for (int dx = -1; dx <= 1; dx++)
						onBorder |= !inside(x + dx, z + dz);
				const bool in = inside(x, z);
				char c = in ? ' ' : '.';
				if (in && onBorder) {
					border++;
					if (pieces.empty()) { c = '?'; missing++; }
					else if (pieces.size() > 1) { c = '2'; doubled++; }
					else {
						const int quarter = ((int)std::lround(pieces[0]->orientation.y / 1.5707963f) % 4 + 4) % 4;
						switch (pieces[0]->blueprint->buildingType) {
						case Tags::BUILDINGTYPE_WALL: c = (quarter & 1) ? '|' : '-'; break;
						case Tags::BUILDINGTYPE_WALL_CORNER_OUT: c = 'o'; break;
						case Tags::BUILDINGTYPE_WALL_CORNER_IN: c = 'i'; break;
						case Tags::BUILDINGTYPE_WALL_CROSSROADS: c = '+'; break;
						default: c = 'G'; break;
						}
						// an arm into a tile with no piece leaves the ring open there
						for (const auto& [dx, dz] : arms(pieces[0]))
							if (piecesOn(x + dx, z + dz).empty()) {
								c = 'x';
								loose++;
								break;
							}
					}
				}
				else if (!pieces.empty()) { c = '!'; within++; }
				row += c;
			}
			printf("  %s\n", row.c_str());
		}
		printf("WALLRING %s: %d border tiles, %d missing, %d doubled, %d inside, %d turned wrong\n", stage, border, missing, doubled, within, loose);
	};

	// a Manor on open ground near the start, then barracks overlapping it on the east and the north
	ServerGameObject* first = nullptr;
	int mx = 0, mz = 0;
	for (int tz = 50; tz <= 80 && !first; tz++)
		for (int tx = 50; tx <= 80 && !first; tx++)
			if ((first = server.stampdownObject(manor, player, tileCentre(manor, tx, tz), Vector3(0, 0, 0), true, true)))
				mx = tx, mz = tz;
	if (!first || !first->parent || first->parent->blueprint->bpClass != Tags::GAMEOBJCLASS_CITY) {
		printf("WALLRING no Manor stamped down\n");
		return;
	}
	ServerGameObject* city = (ServerGameObject*)first->parent;
	check("manor", city);
	const int steps[2][2] = { { 6, 3 }, { -3, 6 } };
	const char* stages[2] = { "east barracks", "north barracks" };
	for (int k = 0; k < 2; k++) {
		ServerGameObject* b = nullptr;
		for (int d = 0; d < 4 && !b; d++)
			b = server.stampdownObject(barracks, player, tileCentre(barracks, mx + steps[k][0] + d, mz + steps[k][1]), Vector3(0, 0, 0), true, true);
		if (!b)
			printf("WALLRING %s not stamped down\n", stages[k]);
		else if (b->parent != city)
			printf("WALLRING %s at (%.1f, %.1f) started a city of its own\n", stages[k], b->position.x, b->position.z);
		else
			check(stages[k], city);
	}

	// for a look at it in game: the foundations swapped for their finished buildings (the
	// blueprint without " Foundation"), saved as saved\Save_Games\WallRing.sav
	const Vector3 manorAt = first->position;
	std::vector<ServerGameObject*> foundations;
	for (const auto& [bpid, objlist] : city->children)
		for (CommonGameObject* c : objlist)
			if (c->blueprint->bpClass == Tags::GAMEOBJCLASS_BUILDING && c->blueprint->name.ends_with(" Foundation"))
				foundations.push_back((ServerGameObject*)c);
	for (ServerGameObject* o : foundations) {
		const std::string built = o->blueprint->name.substr(0, o->blueprint->name.size() - 11);
		if (const GameObjBlueprint* bp = server.gameSet->findBlueprint(Tags::GAMEOBJCLASS_BUILDING, built)) {
			const Vector3 at = o->position, turn = o->orientation;
			server.destroyObject(o);
			server.spawnObject(bp, city, at, turn);
		}
	}
	// the player as the human one, which a save to play needs
	if (server.clientPlayerObjects.empty())
		server.clientPlayerObjects.emplace_back(player->id);
	const std::string path = PathToUtf8(g_userPath / "saved" / "Save_Games" / "WallRing.sav");
	printf("WALLRING saved %d to %s, the Manor at (%.1f, %.1f)\n", (int)server.saveGame(path.c_str()), path.c_str(), manorAt.x, manorAt.z);
	fflush(stdout);
}

// The differential harness's wkbre2 side ("diffRun": { "source": "Save_Games\\X.sav",
// "seconds": N, "out": "name" } in wkconfig.json): the save loaded, run for N game seconds in
// fixed 20 ms steps, then saved as saved\Save_Games\<out>.sav, to compare with the original's
// save after the same time (RE repository, tools/savdiff.py).
void Test_DiffRun()
{
	LoadBCP("data.bcp");
	const nlohmann::json cfg = g_settings.value("diffRun", nlohmann::json::object());
	const std::string source = cfg.value("source", std::string("Save_Games\\2 Player 4 Peaks.lvl"));
	const float seconds = cfg.value("seconds", 60.0f);
	const std::string out = cfg.value("out", std::string("diff run"));
	Server server;
	server.aggressiveAI = cfg.value("aggressiveAI", true);
	server.loadSaveGame(source.c_str());
	const float start = server.timeManager.currentTime;
	// "probe": { "self": <object id>, "equations": [ ... ] }: those equations at the start
	if (cfg.contains("probe")) {
		const nlohmann::json& probe = cfg["probe"];
		ServerGameObject* self = server.findObject(probe.value("self", 0u));
		SrvScriptContext ctx(&server, self);
		for (const auto& name : probe.value("equations", nlohmann::json::array())) {
			const int eq = server.gameSet->equations.names.getIndex(name.get<std::string>());
			const std::string value = eq >= 0 && self ? std::to_string(server.gameSet->equations[eq]->eval(&ctx)) : "?";
			printf("DIFFRUN probe %s = %s\n", name.get<std::string>().c_str(), value.c_str());
		}
	}
	server.timeManager.unpause();
	server.timeManager.stepMs = 20;
	while (server.timeManager.currentTime < start + seconds) {
		server.timeManager.pendingMs = server.timeManager.stepMs;
		if (server.timeManager.step())
			server.simulate();
	}
	server.timeManager.pause();
	const std::string path = PathToUtf8(g_userPath / "saved" / "Save_Games" / PathFromUtf8(out + ".sav"));
	server.saveOriginalFormat = true; // (compared with the original's save, which the original loads too)
	const bool ok = server.saveGame(path.c_str());
	printf("DIFFRUN %s: %.2f -> %.2f game s, saved %d to %s\n", source.c_str(), start, server.timeManager.currentTime, (int)ok, path.c_str());
	fflush(stdout);
}

// The terrain's vertex normals (Terrain::getNormal) and heights of 4 Peaks as a text file in the
// temp folder ("x z height nx ny nz" per vertex, world units), to compare with the original's
// per-vertex Dot3 light vectors (RE repository, docs/engine/original-comparison.md)
// Levelling (needs the game data): a Barracks foundation stamped down in play on the most
// sloped spot near player 1027's Manor that still takes it flattens the ground under it.
// Prints "LEVEL" lines: the spot, the heights under the footprint before and after.
void Test_Levelling()
{
	LoadBCP("data.bcp");
	Server server;
	server.loadSaveGame("Save_Games\\2 Player 4 Peaks.lvl");
	ServerGameObject* player = server.findObject(1027);
	const GameObjBlueprint* bp = server.gameSet->findBlueprint(Tags::GAMEOBJCLASS_BUILDING, "Barracks Stone Foundation");
	if (!player || !bp || !bp->footprint || !server.terrain) {
		printf("LEVEL not set up\n");
		return;
	}
	player->setItem(Tags::PDITEM_WOOD, 10000.0f);
	player->setItem(Tags::PDITEM_FOOD, 10000.0f);
	player->setItem(Tags::PDITEM_GOLD, 10000.0f);
	const Terrain& t = *server.terrain;
	// the heights at the footprint's tile corners
	auto spread = [&](const Vector3& at, float& lo, float& hi) {
		const auto rot = bp->footprint->rotateOrigin(0.0f);
		const int ox = (int)((at.x - rot.first) / 5.0f), oz = (int)((at.z - rot.second) / 5.0f);
		lo = 1e9f; hi = -1e9f;
		for (const auto& to : bp->footprint->tiles) {
			if (!to.flatten)
				continue;
			const auto [rx, rz] = to.rotate(0.0f);
			for (int dz = 0; dz <= 1; dz++)
				for (int dx = 0; dx <= 1; dx++) {
					const float h = t.getVertex(ox + rx + t.edge + dx, oz + rz + t.edge + dz);
					lo = std::min(lo, h);
					hi = std::max(hi, h);
				}
		}
	};
	// the spots around (540, 140), the start, that take a barracks: the most uneven one
	Vector3 best;
	float bestSpread = -1.0f;
	for (float z = 60.0f; z <= 260.0f; z += 5.0f)
		for (float x = 440.0f; x <= 640.0f; x += 5.0f) {
			const Vector3 at(std::floor(x / 5.0f) * 5.0f + 2.5f + bp->footprint->originX, 0.0f, std::floor(z / 5.0f) * 5.0f + 2.5f + bp->footprint->originZ);
			if (StampdownPlan::getStampdownPlan(&server, player, bp, at, Vector3(0, 0, 0)).status != StampdownPlan::Status::Valid)
				continue;
			float lo, hi;
			spread(at, lo, hi);
			if (hi - lo > bestSpread) {
				bestSpread = hi - lo;
				best = at;
			}
		}
	if (bestSpread < 0.0f) {
		printf("LEVEL no spot takes a barracks\n");
		return;
	}
	float lo, hi;
	spread(best, lo, hi);
	printf("LEVEL spot (%.1f, %.1f): heights %.2f to %.2f before\n", best.x, best.z, lo, hi);
	best.y = t.getHeight(best.x, best.z);
	ServerGameObject* obj = server.stampdownObject(bp, player, best, Vector3(0, 0, 0), true, true);
	spread(best, lo, hi);
	printf("LEVEL %s: heights %.2f to %.2f after, the foundation at y %.2f (ground %.2f)\n", obj ? "stamped down" : "refused", lo, hi,
		obj ? obj->position.y : 0.0f, t.getHeight(best.x, best.z));
	fflush(stdout);
}

// Farmer's farm levelled (needs the game data): a "Peasant Farmer" trained at player 1027's Manor
// on 4 Peaks gets a farm from its "Auto Farm" OBJECT_CREATION (NEAREST_VALID_STAMPDOWN_POS); its
// FLATTEN tiles must end as level as those of a farm stamped down by hand (WKB.exe levels every
// building it puts on the map, 0x513790). Prints "AUTOFARM" lines: the auto farm's spot and its
// corners' spread before and after, the hand-placed farm's, and "AUTOFARM PASS" or "FAIL".
void Test_AutoFarmLevelled()
{
	LoadBCP("data.bcp");
	Server server;
	server.loadSaveGame("Save_Games\\2 Player 4 Peaks.lvl");
	ServerGameObject* player = server.findObject(1027);
	const GameObjBlueprint* farmBp = server.gameSet->findBlueprint(Tags::GAMEOBJCLASS_BUILDING, "Standard Farm Foundation");
	const int spawnFarmer = server.gameSet->commands.names.getIndex("Spawn Peasant Farmer");
	ServerGameObject* manor = nullptr;
	auto findManor = [&](CommonGameObject* obj, auto& self) -> void {
		for (const auto& [type, children] : obj->children)
			for (CommonGameObject* child : children) {
				if (!manor && child->blueprint->name == "Manor")
					manor = child->dyncast<ServerGameObject>();
				self(child, self);
			}
	};
	if (player)
		findManor(player, findManor);
	if (!player || !farmBp || !farmBp->footprint || spawnFarmer < 0 || !manor || !server.terrain) {
		printf("AUTOFARM not set up\n");
		printf("AUTOFARM FAIL\n");
		return;
	}
	player->setItem(Tags::PDITEM_WOOD, 10000.0f);
	player->setItem(Tags::PDITEM_FOOD, 10000.0f);
	player->setItem(Tags::PDITEM_GOLD, 10000.0f);
	Terrain& t = *server.terrain;
	// the heights as loaded, to measure the ground before
	const int verts = (t.width + 1) * (t.height + 1);
	std::vector<float> loaded(verts);
	for (int i = 0; i < verts; i++)
		loaded[i] = t.heightSteps(i) * t.scale;
	// the spread of a farm's FLATTEN tile corners, now or as loaded
	auto spread = [&](const Footprint* fp, const Vector3& at, float rotY, bool asLoaded) {
		const auto rot = fp->rotateOrigin(rotY);
		const int ox = (int)((at.x - rot.first) / 5.0f), oz = (int)((at.z - rot.second) / 5.0f);
		float lo = 1e9f, hi = -1e9f;
		for (const auto& to : fp->tiles) {
			if (!to.flatten)
				continue;
			const auto [rx, rz] = to.rotate(rotY);
			for (int dz = 0; dz <= 1; dz++)
				for (int dx = 0; dx <= 1; dx++) {
					const int vx = ox + rx + t.edge + dx, vz = oz + rz + t.edge + dz;
					const float h = asLoaded ? loaded[vz * (t.width + 1) + vx] : t.getVertex(vx, vz);
					lo = std::min(lo, h);
					hi = std::max(hi, h);
				}
		}
		return hi - lo;
	};
	auto farms = [&]() {
		std::set<ServerGameObject*> found;
		auto walk = [&](CommonGameObject* obj, auto& self) -> void {
			for (const auto& [type, children] : obj->children)
				for (CommonGameObject* child : children) {
					// the foundation turns into its ground's kind on stampdown ("Fertile Farm Foundation")
					if (child->blueprint->bpClass == Tags::GAMEOBJCLASS_BUILDING && child->blueprint->name.find("Farm Foundation") != std::string::npos)
						found.insert(child->dyncast<ServerGameObject>());
					self(child, self);
				}
		};
		walk(player, walk);
		return found;
	};
	const std::set<ServerGameObject*> before = farms();
	server.timeManager.unpause();
	server.timeManager.stepMs = 20;
	server.gameSet->commands[spawnFarmer].execute(manor, nullptr, Tags::ORDERASSIGNMODE_DO_LAST);
	ServerGameObject* autoFarm = nullptr;
	for (int step = 0; step * 20 < 120000 && !autoFarm; step++) {
		server.timeManager.pendingMs = server.timeManager.stepMs;
		if (server.timeManager.step())
			server.simulate();
		for (ServerGameObject* f : farms())
			if (!before.count(f))
				autoFarm = f;
	}
	if (!autoFarm) {
		printf("AUTOFARM no farm after 120 s\n");
		printf("AUTOFARM FAIL\n");
		return;
	}
	const Footprint* autoFp = autoFarm->blueprint->footprint;
	const float autoBefore = spread(autoFp, autoFarm->position, autoFarm->orientation.y, true), autoAfter = spread(autoFp, autoFarm->position, autoFarm->orientation.y, false);
	printf("AUTOFARM auto %s %u at (%.1f, %.1f) t=%.1f: corners spread %.2f before, %.2f after; farm y %.2f, ground %.2f\n", autoFarm->blueprint->name.c_str(), autoFarm->id,
		autoFarm->position.x, autoFarm->position.z, server.timeManager.currentTime, autoBefore, autoAfter, autoFarm->position.y,
		t.getHeight(autoFarm->position.x, autoFarm->position.z));
	// a farm placed by hand on the most uneven spot near the Manor that takes one
	Vector3 best;
	float bestSpread = -1.0f;
	for (float z = manor->position.z - 80.0f; z <= manor->position.z + 80.0f; z += 5.0f)
		for (float x = manor->position.x - 80.0f; x <= manor->position.x + 80.0f; x += 5.0f) {
			const Vector3 at(std::floor(x / 5.0f) * 5.0f + 2.5f + farmBp->footprint->originX, 0.0f, std::floor(z / 5.0f) * 5.0f + 2.5f + farmBp->footprint->originZ);
			if (StampdownPlan::getStampdownPlan(&server, player, farmBp, at, Vector3(0, 0, 0)).status != StampdownPlan::Status::Valid)
				continue;
			const float s = spread(farmBp->footprint, at, 0.0f, false);
			if (s > bestSpread) {
				bestSpread = s;
				best = at;
			}
		}
	float handAfter = -1.0f;
	if (bestSpread >= 0.0f) {
		best.y = t.getHeight(best.x, best.z);
		if (ServerGameObject* hand = server.stampdownObject(farmBp, player, best, Vector3(0, 0, 0), true, true))
			handAfter = spread(hand->blueprint->footprint, hand->position, hand->orientation.y, false);
	}
	printf("AUTOFARM hand-placed farm at (%.1f, %.1f): corners spread %.2f before, %.2f after\n", best.x, best.z, bestSpread, handAfter);
	const bool pass = handAfter >= 0.0f && autoAfter <= handAfter + 0.01f;
	printf("AUTOFARM %s\n", pass ? "PASS" : "FAIL");
	fflush(stdout);
}

// Placement rules (needs the game data), as WKB.exe's (StampdownPlan::getStampdownPlan), for a
// human player on 4 Peaks: every tile of the map tried with a barracks (8 m limit) and a farm
// (12 m, "Can Be Farmed On"). Prints "PLACE" lines: the count of each outcome, the steepest
// accepted spot, the spots the old rule (levelled corners only, no terrain types) took that are
// now refused, and the farm spots on hills or rock (none). Then a barracks is stamped down on
// an uneven spot, and the spots touching it and one tile further are tried before and after.
void Test_PlacementRules()
{
	LoadBCP("data.bcp");
	Server server;
	server.loadSaveGame("Save_Games\\2 Player 4 Peaks.lvl");
	ServerGameObject* player = server.findObject(1027);
	if (!player || !server.terrain) {
		printf("PLACE not set up\n");
		return;
	}
	player->setItem(Tags::PDITEM_WOOD, 10000.0f);
	player->setItem(Tags::PDITEM_FOOD, 10000.0f);
	player->setItem(Tags::PDITEM_GOLD, 10000.0f);
	const Terrain& t = *server.terrain;
	const auto [tw, th] = t.getNumPlayableTiles();
	using Status = StampdownPlan::Status;
	const char* names[] = { "valid", "no explanation", "off the map", "not in city", "occupied", "terrain type", "too steep", "neighbour" };
	auto spotAt = [](const Footprint* fp, int tx, int tz) {
		return Vector3(tx * 5.0f + 2.5f + fp->originX, 0.0f, tz * 5.0f + 2.5f + fp->originZ);
	};
	// the corners' spread: over all the tiles, or (the old rule) over the levelled ones
	auto spread = [&](const Footprint* fp, const Vector3& at, bool levelledOnly) {
		const int ox = (int)((at.x - fp->originX) / 5.0f), oz = (int)((at.z - fp->originZ) / 5.0f);
		float lo = 1e9f, hi = -1e9f;
		for (const auto& to : fp->tiles) {
			if (levelledOnly && !to.flatten)
				continue;
			for (int dz = 0; dz <= 1; dz++)
				for (int dx = 0; dx <= 1; dx++) {
					const int vx = std::clamp(ox + to.offsetX + t.edge + dx, 0, t.width), vz = std::clamp(oz + to.offsetZ + t.edge + dz, 0, t.height);
					lo = std::min(lo, t.getVertex(vx, vz));
					hi = std::max(hi, t.getVertex(vx, vz));
				}
		}
		return hi - lo;
	};
	auto terrainName = [&](const Vector3& at) -> std::string {
		const auto* tile = t.getPlayableTile((int)(at.x / 5.0f), (int)(at.z / 5.0f));
		if (!tile || !tile->texture || !tile->texture->grp)
			return "?";
		auto it = server.gameSet->associatedTileTexGroups.find(tile->texture->grp->name);
		return it == server.gameSet->associatedTileTexGroups.end() ? "none" : server.gameSet->terrains.getString(it->second);
	};
	// tiles whose texture group no terrain names: refused by every terrain list
	{
		std::map<std::string, int> unnamed;
		const Footprint none;
		for (int tz = 0; tz < th; tz++)
			for (int tx = 0; tx < tw; tx++)
				if (const std::string tn = terrainName(spotAt(&none, tx, tz)); tn == "none" || tn == "?")
					unnamed[t.getPlayableTile(tx, tz)->texture ? t.getPlayableTile(tx, tz)->texture->grp->name : "?"]++;
		printf("PLACE tiles of no terrain:");
		for (const auto& [name, n] : unnamed)
			printf(" %s %d,", name.c_str(), n);
		printf("\n");
	}
	for (const char* bpName : { "Barracks Stone Foundation", "Standard Farm Foundation" }) {
		const GameObjBlueprint* bp = server.gameSet->findBlueprint(Tags::GAMEOBJCLASS_BUILDING, bpName);
		if (!bp || !bp->footprint)
			continue;
		const Footprint* fp = bp->footprint;
		int counts[8] = {};
		int oldTook = 0, hillFarms = 0;
		float steepest = 0.0f;
		std::map<std::string, int> refusedTerrains;
		for (int tz = 0; tz < th; tz++)
			for (int tx = 0; tx < tw; tx++) {
				const Vector3 at = spotAt(fp, tx, tz);
				const Status s = StampdownPlan::getStampdownPlan(&server, player, bp, at, Vector3(0, 0, 0)).status;
				counts[std::clamp((int)s, 0, 7)]++;
				if (s == Status::Valid) {
					steepest = std::max(steepest, spread(fp, at, false));
					const std::string tn = terrainName(at);
					if (tn == "Hills" || tn == "Rock" || tn == "Mountains")
						hillFarms++;
				}
				else if ((s == Status::InvalidTooSteep || s == Status::InvalidTerrainType || s == Status::InvalidNeighbour)
					&& (fp->maxHeightDifferential <= 0.0f || spread(fp, at, true) <= fp->maxHeightDifferential)) {
					oldTook++;
					if (s == Status::InvalidTerrainType)
						refusedTerrains[terrainName(at)]++;
				}
			}
		printf("PLACE %s (limit %.0f m):", bpName, fp->maxHeightDifferential);
		for (int i = 0; i < 8; i++)
			if (counts[i])
				printf(" %s %d,", names[i], counts[i]);
		printf(" steepest accepted %.2f m over the footprint\n", steepest);
		printf("PLACE   the old rule took %d of the refused spots; accepted on hills, rock or mountains: %d; refused terrains:", oldTook, hillFarms);
		for (const auto& [name, n] : refusedTerrains)
			printf(" %s %d,", name.c_str(), n);
		printf("\n");
	}
	// a barracks on the most uneven spot near the start that takes it, then its neighbours
	const GameObjBlueprint* bp = server.gameSet->findBlueprint(Tags::GAMEOBJCLASS_BUILDING, "Barracks Stone Foundation");
	const Footprint* fp = bp ? bp->footprint : nullptr;
	if (fp) {
		Vector3 best;
		float bestSpread = -1.0f;
		for (int tz = 12; tz <= 52; tz++)
			for (int tx = 88; tx <= 128; tx++) {
				const Vector3 at = spotAt(fp, tx, tz);
				if (StampdownPlan::getStampdownPlan(&server, player, bp, at, Vector3(0, 0, 0)).status == Status::Valid && spread(fp, at, false) > bestSpread) {
					bestSpread = spread(fp, at, false);
					best = at;
				}
			}
		int minX = 0, maxX = 0, minZ = 0, maxZ = 0;
		for (const auto& to : fp->tiles) {
			minX = std::min(minX, to.offsetX); maxX = std::max(maxX, to.offsetX);
			minZ = std::min(minZ, to.offsetZ); maxZ = std::max(maxZ, to.offsetZ);
		}
		const int w = maxX - minX + 1, d = maxZ - minZ + 1;
		std::vector<Vector3> around;
		for (int gap = 0; gap <= 1; gap++)
			for (const auto [sx, sz] : { std::pair{ w + gap, 0 }, std::pair{ -(w + gap), 0 }, std::pair{ 0, d + gap }, std::pair{ 0, -(d + gap) } })
				around.push_back(best + Vector3(sx * 5.0f, 0.0f, sz * 5.0f));
		std::vector<Status> before;
		for (const Vector3& at : around)
			before.push_back(StampdownPlan::getStampdownPlan(&server, player, bp, at, Vector3(0, 0, 0)).status);
		best.y = t.getHeight(best.x, best.z);
		ServerGameObject* obj = server.stampdownObject(bp, player, best, Vector3(0, 0, 0), true, true);
		printf("PLACE barracks at (%.1f, %.1f), %.2f m uneven: %s, now %.2f m\n", best.x, best.z, bestSpread, obj ? "stamped down" : "refused", spread(fp, best, false));
		for (size_t i = 0; i < around.size(); i++) {
			const Status after = StampdownPlan::getStampdownPlan(&server, player, bp, around[i], Vector3(0, 0, 0)).status;
			printf("PLACE   %s at (%.1f, %.1f): %.2f m uneven, %s -> %s\n", i < 4 ? "touching" : "a tile off", around[i].x, around[i].z,
				spread(fp, around[i], false), names[std::clamp((int)before[i], 0, 7)], names[std::clamp((int)after, 0, 7)]);
		}
	}
	// a farm placed in play on the most uneven spot near the start that takes it: levelled as any
	// building (WKB.exe's footprint tiles flatten unless they say LEAVE_AS_IS, 0x53f1f0; the farm's
	// say nothing)
	if (const GameObjBlueprint* farm = server.gameSet->findBlueprint(Tags::GAMEOBJCLASS_BUILDING, "Standard Farm Foundation"); farm && farm->footprint) {
		const Footprint* ffp = farm->footprint;
		Vector3 best;
		float bestSpread = -1.0f;
		for (int tz = 12; tz <= 52; tz++)
			for (int tx = 88; tx <= 128; tx++) {
				const Vector3 at = spotAt(ffp, tx, tz);
				if (StampdownPlan::getStampdownPlan(&server, player, farm, at, Vector3(0, 0, 0)).status == Status::Valid && spread(ffp, at, false) > bestSpread) {
					bestSpread = spread(ffp, at, false);
					best = at;
				}
			}
		best.y = t.getHeight(best.x, best.z);
		ServerGameObject* obj = server.stampdownObject(farm, player, best, Vector3(0, 0, 0), true, true);
		printf("PLACE farm at (%.1f, %.1f), %.2f m uneven: %s, now %.2f m\n", best.x, best.z, bestSpread, obj ? "stamped down" : "refused", spread(ffp, best, false));
	}
	// WKB_PLACE_SAVE=<name>: a Standard Farm set, unlevelled, on the most uneven free spot near
	// the start (as a map's own farms or an AI's can stand), and the game saved as <name>.sav in
	// the user folder: a save to look at how fields lie on a slope
	if (const char* saveName = getenv("WKB_PLACE_SAVE")) {
		const GameObjBlueprint* farm = server.gameSet->findBlueprint(Tags::GAMEOBJCLASS_BUILDING, "Standard Farm");
		const GameObjBlueprint* farmFoundation = server.gameSet->findBlueprint(Tags::GAMEOBJCLASS_BUILDING, "Standard Farm Foundation");
		if (farm && farmFoundation && farmFoundation->footprint) {
			const Footprint* ffp = farmFoundation->footprint;
			Vector3 best;
			float bestSpread = -1.0f;
			for (int tz = 12; tz <= 52; tz++)
				for (int tx = 88; tx <= 128; tx++) {
					const Vector3 at = spotAt(ffp, tx, tz);
					const Status s = StampdownPlan::getStampdownPlan(&server, player, farmFoundation, at, Vector3(0, 0, 0)).status;
					if (s != Status::InvalidOccupied && s != Status::InvalidTerrain && s != Status::InvalidNeighbour && spread(ffp, at, false) > bestSpread) {
						bestSpread = spread(ffp, at, false);
						best = at;
					}
				}
			best.y = t.getHeight(best.x, best.z);
			ServerGameObject* obj = server.stampdownObject(farm, player, best, Vector3(0, 0, 0), false, false);
			// the player as the save's human (CLIENT_STATE), so that it loads as a game to look at
			server.clientPlayerObjects.clear();
			server.clientPlayerObjects.emplace_back(player);
			const std::string path = PathToUtf8(g_userPath / "saved" / "Save_Games" / PathFromUtf8(std::string(saveName) + ".sav"));
			printf("PLACE farm %s at (%.1f, %.1f, %.1f), %.2f m uneven, terrain %s; saved %d to %s\n", obj ? "set" : "not set", best.x, best.y, best.z, bestSpread,
				terrainName(best).c_str(), (int)server.saveGame(path.c_str()), path.c_str());
		}
	}
	fflush(stdout);
}

void Test_TerrainNormals()
{
	LoadBCP("data.bcp");
	Terrain trn;
	trn.readFromFile("Maps\\128_4Peaks\\128_4Peaks.bcm");
	const std::string path = (std::filesystem::temp_directory_path() / "wkbre2-terrain-normals.txt").string();
	FILE* f = fopen(path.c_str(), "w");
	if (!f)
		return;
	for (int z = 0; z <= trn.height; z++)
		for (int x = 0; x <= trn.width; x++) {
			const Vector3 n = trn.getNormal(x, z);
			fprintf(f, "%.1f %.1f %.3f %.4f %.4f %.4f\n", (x - trn.edge) * 5.0f, (z - trn.edge) * 5.0f, trn.getVertex(x, z), n.x, n.y, n.z);
		}
	fclose(f);
	printf("NORMALS %dx%d edge %d scale %.3f -> %s\n", trn.width, trn.height, trn.edge, trn.scale, path.c_str());
	fflush(stdout);
}

// The passability fields (USE_PASSABILITY_FIELDS): on 8 Player Continent, per terrain type, the
// tiles a Peasant can't stand on with the fields and without them (water depth and slope only)
void Test_Passability()
{
	LoadBCP("data.bcp");
	Server server;
	server.loadSaveGame("Save_Games\\8 Player Continent.lvl");
	const GameObjBlueprint* peasant = server.gameSet->findBlueprint(Tags::GAMEOBJCLASS_CHARACTER, "Peasant");
	if (!peasant || !server.terrain) {
		printf("PASSABILITY no Peasant or terrain\n");
		return;
	}
	const std::vector<bool>* fields = peasant->getPassableTerrains();
	printf("PASSABILITY Peasant fields %s (%zu field sets)\n", fields ? "found" : "missing", server.gameSet->passabilityFields.size());
	const auto area = server.terrain->getNumPlayableTiles();
	std::map<std::string, std::array<int, 3>> perTerrain; // tiles, blocked with fields, blocked without
	int with = 0, without = 0;
	for (int z = 0; z < area.second; z++)
		for (int x = 0; x < area.first; x++) {
			const bool a = MovementController::isTileBlocked(peasant, x, z);
			// without the fields (resolved again below); the uncached ground test, as the path
			// finding's cache keeps a type's class (MovementController::movementClass)
			peasant->passableTerrains = nullptr;
			const auto& tile = server.tiles[z * area.first + x];
			const bool b = (tile.building.getFrom<Server>() && !tile.buildingPassable) || MovementController::isTerrainBlocked(peasant, x, z);
			peasant->passableTerrains = fields;
			const short t = server.tiles[z * area.first + x].terrainType;
			auto& row = perTerrain[t >= 0 ? server.gameSet->terrains.names.getString(t) : std::string("(none)")];
			row[0]++;
			row[1] += a;
			row[2] += b;
			with += a;
			without += b;
		}
	for (const auto& [name, row] : perTerrain)
		printf("PASSABILITY %-24s tiles %6d blocked %6d (without fields %6d)\n", name.c_str(), row[0], row[1], row[2]);
	printf("PASSABILITY total blocked %d with fields, %d without, of %d tiles\n", with, without, area.first * area.second);
	fflush(stdout);
}

// The minimap drawn from the terrain (Terrain::drawMinimap) against every map's own in its BCM: the
// mean difference per channel (0-255) for each of the four orientations; prints the best one and
// the mean difference over all maps. Writes the 4 Peaks pair as a PCX to the temp folder.
void Test_Minimap()
{
	LoadBCP("data.bcp");
	std::unique_ptr<std::vector<std::string>> dirs(ListDirectories("Maps"));
	double total[4] = {};
	int maps = 0, best[4] = {};
	for (const std::string& dir : *dirs) {
		const std::string bcm = "Maps\\" + dir + "\\" + dir + ".bcm";
		if (!FileExists(bcm.c_str()))
			continue;
		Terrain t;
		t.readBCM(bcm.c_str());
		double diff[4];
		for (int o = 0; o < 4; o++) {
			const std::vector<uint8_t> drawn = t.drawMinimap(o & 1, (o & 2) != 0);
			double sum = 0;
			for (int i = 0; i < 128 * 128; i++)
				for (int c = 0; c < 3; c++)
					sum += std::abs((int)drawn[i * 3 + c] - (int)t.minimapRGBA[i * 4 + c]);
			diff[o] = sum / (128 * 128 * 3);
			total[o] += diff[o];
		}
		best[std::min_element(diff, diff + 4) - diff]++;
		maps++;
		if (dir.find("Peaks") != std::string::npos && dir.find("Snow") == std::string::npos) {
			// the map's own minimap and the drawn one side by side, as a binary PPM
			const std::vector<uint8_t> drawn = t.drawMinimap();
			const std::filesystem::path ppm = std::filesystem::temp_directory_path() / "wkbre2-minimap.ppm";
			std::ofstream f(ppm, std::ios::binary);
			f << "P6\n256 128\n255\n";
			for (int y = 0; y < 128; y++) {
				for (int x = 0; x < 128; x++)
					f.write((const char*)&t.minimapRGBA[(y * 128 + x) * 4], 3);
				f.write((const char*)&drawn[y * 128 * 3], 128 * 3);
			}
			printf("MINIMAP %s: %s\n", dir.c_str(), ppm.string().c_str());
		}
	}
	for (int o = 0; o < 4; o++)
		printf("MINIMAP orientation flipX %d flipZ %d: mean difference %.1f, best for %d of %d maps\n", o & 1, (o & 2) != 0, total[o] / std::max(maps, 1), best[o], maps);
	fflush(stdout);
}

// DDS: made-up files decoded (Bitmap::loadDDS): an uncompressed BGRA one, a BC1 block of red
// and blue, a BC3 block with an alpha ramp and a BC5 normal. Prints "DDS <case> <ok>".
void Test_DDS()
{
	auto header = [](std::vector<uint8_t>& f, int w, int h, uint32_t pfFlags, const char* fourCC, uint32_t bits, uint32_t rMask, uint32_t aMask) {
		f.assign(128, 0);
		memcpy(f.data(), "DDS ", 4);
		auto put = [&](size_t off, uint32_t v) { memcpy(&f[off], &v, 4); };
		put(4, 124); put(12, h); put(16, w); put(76, 32); put(80, pfFlags);
		if (fourCC) memcpy(&f[84], fourCC, 4);
		put(88, bits); put(92, rMask); put(104, aMask);
	};
	std::vector<uint8_t> f;
	// 2 x 1 BGRA: (10, 20, 30, 40), (50, 60, 70, 80) in RGBA
	header(f, 2, 1, 0x41, nullptr, 32, 0x00FF0000, 0xFF000000);
	for (uint8_t v : { 30, 20, 10, 40, 70, 60, 50, 80 }) f.push_back(v);
	Bitmap a = Bitmap::loadDDS(f.data(), f.size());
	const bool bgraOk = a.width == 2 && a.pixels[0] == 10 && a.pixels[2] == 30 && a.pixels[3] == 40 && a.pixels[4] == 50 && a.pixels[7] == 80;
	printf("DDS uncompressed BGRA %d\n", bgraOk);
	// BC1: colour 0 red, colour 1 blue, texel i takes index i % 4
	header(f, 4, 4, 4, "DXT1", 0, 0, 0);
	for (uint8_t v : { 0x00, 0xF8, 0x1F, 0x00, 0xE4, 0xE4, 0xE4, 0xE4 }) f.push_back(v);
	Bitmap b = Bitmap::loadDDS(f.data(), f.size());
	// (block-compressed ones decoded as the CPU's users of the pixels see them)
	auto px = [](const Bitmap& bm, int i, int c) { return bm.compressed() ? (int)bm.convertToR8G8B8A8().pixels[i * 4 + c] : (int)bm.pixels[i * 4 + c]; };
	const bool bc1Ok = px(b, 0, 0) == 255 && px(b, 0, 2) == 0 && px(b, 1, 0) == 0 && px(b, 1, 2) == 255 && px(b, 2, 0) == 170 && px(b, 3, 0) == 85;
	printf("DDS BC1 %d (texel 2: %d %d %d)\n", bc1Ok, px(b, 2, 0), px(b, 2, 1), px(b, 2, 2));
	// BC3: alpha 255 to 0 over the eight indices (0, 1: the ends; 2-7 between), colour as above
	header(f, 4, 4, 4, "DXT5", 0, 0, 0);
	for (uint8_t v : { 255, 0, 0x88, 0xC6, 0xFA, 0x00, 0x00, 0x00, 0x00, 0xF8, 0x1F, 0x00, 0xE4, 0xE4, 0xE4, 0xE4 }) f.push_back(v);
	Bitmap c = Bitmap::loadDDS(f.data(), f.size());
	const bool bc3Ok = px(c, 0, 3) == 255 && px(c, 1, 3) == 0 && px(c, 0, 0) == 255;
	printf("DDS BC3 %d (alpha %d %d %d %d)\n", bc3Ok, px(c, 0, 3), px(c, 1, 3), px(c, 2, 3), px(c, 3, 3));
	// BC5: x and y both at the middle (128): the normal points along z
	header(f, 4, 4, 4, "ATI2", 0, 0, 0);
	for (int k = 0; k < 2; k++)
		for (uint8_t v : { 128, 128, 0, 0, 0, 0, 0, 0 }) f.push_back(v);
	Bitmap d = Bitmap::loadDDS(f.data(), f.size());
	const bool bc5Ok = px(d, 0, 0) == 128 && px(d, 0, 1) == 128 && px(d, 0, 2) >= 254;
	printf("DDS BC5 %d (normal %d %d %d)\n", bc5Ok, px(d, 0, 0), px(d, 0, 1), px(d, 0, 2));
	// BC7 (DX10 header, DXGI 98), 8 x 8 with 2 mip levels: kept compressed, both levels
	header(f, 8, 8, 4, "DX10", 0, 0, 0);
	{ uint32_t mips = 2; memcpy(&f[28], &mips, 4); }
	f.resize(148, 0);
	{ uint32_t dxgi = 98; memcpy(&f[128], &dxgi, 4); }
	for (int i = 0; i < 64 + 16; i++) f.push_back((uint8_t)i);
	Bitmap e = Bitmap::loadDDS(f.data(), f.size());
	const bool bc7Ok = e.format == BMFORMAT_BC7 && e.levels.size() == 2 && e.levels[1] == 64 && e.pixels.size() == 80 && e.pixels[64] == 64;
	printf("DDS BC7 %d (kept compressed, %zu levels)\n", bc7Ok, e.levels.size());
	// BC7 decoded on the CPU: a mode 6 block with both endpoints at the maximum (and the p-bits
	// set) is opaque white whatever its indices
	{
		Bitmap w;
		w.width = w.height = 4;
		w.format = BMFORMAT_BC7;
		w.levels = { 0 };
		w.pixels.resize(16);
		w.pixels[0] = 0xC0;
		for (int i = 1; i < 16; i++) w.pixels[i] = 0xFF;
		const Bitmap d7 = w.convertToR8G8B8A8();
		const bool white = d7.pixels[0] == 255 && d7.pixels[1] == 255 && d7.pixels[2] == 255 && d7.pixels[3] == 255 && d7.pixels[63] == 255;
		printf("BC7 CPU decode %d (%d %d %d %d)\n", white, d7.pixels[0], d7.pixels[1], d7.pixels[2], d7.pixels[3]);
	}
	// KTX2: the header, then the level index (offset, length, uncompressed length per level)
	auto ktx2 = [](uint32_t vkFormat, int w, int h, const std::vector<std::vector<uint8_t>>& levels) {
		std::vector<uint8_t> k(80 + 24 * levels.size(), 0);
		static const uint8_t magic[12] = { 0xAB, 'K', 'T', 'X', ' ', '2', '0', 0xBB, 0x0D, 0x0A, 0x1A, 0x0A };
		memcpy(k.data(), magic, 12);
		auto put = [&](size_t off, uint64_t v, int bytes) { memcpy(&k[off], &v, bytes); };
		put(12, vkFormat, 4); put(16, 1, 4); put(20, w, 4); put(24, h, 4); put(40, levels.size(), 4);
		// the levels' data smallest first, as KTX2 files keep them
		std::vector<uint64_t> offsets(levels.size());
		for (size_t l = levels.size(); l-- > 0;) {
			offsets[l] = k.size();
			k.insert(k.end(), levels[l].begin(), levels[l].end());
		}
		for (size_t l = 0; l < levels.size(); l++) {
			put(80 + 24 * l, offsets[l], 8);
			put(88 + 24 * l, levels[l].size(), 8);
			put(96 + 24 * l, levels[l].size(), 8);
		}
		return k;
	};
	std::vector<uint8_t> k = ktx2(37, 2, 1, { { 10, 20, 30, 40, 50, 60, 70, 80 } });
	Bitmap g = Bitmap::loadKTX2(k.data(), k.size());
	printf("KTX2 RGBA8 %d\n", g.width == 2 && g.pixels[0] == 10 && g.pixels[7] == 80);
	k = ktx2(131, 4, 4, { { 0x00, 0xF8, 0x1F, 0x00, 0xE4, 0xE4, 0xE4, 0xE4 } });
	Bitmap hb = Bitmap::loadKTX2(k.data(), k.size());
	printf("KTX2 BC1 %d\n", px(hb, 0, 0) == 255 && px(hb, 1, 2) == 255 && px(hb, 3, 0) == 85);
	std::vector<uint8_t> big(64, 1), small(16, 2);
	k = ktx2(145, 8, 8, { big, small });
	Bitmap kb = Bitmap::loadKTX2(k.data(), k.size());
	const bool ktxBc7Ok = kb.format == BMFORMAT_BC7 && kb.levels.size() == 2 && kb.pixels[0] == 1 && kb.pixels[64] == 2;
	printf("KTX2 BC7 %d (levels largest first: %d %d)\n", ktxBc7Ok, kb.pixels.size() ? kb.pixels[0] : -1, kb.pixels.size() > 64 ? kb.pixels[64] : -1);
	fflush(stdout);
}

// Turned placement (needs the game data): a building turned a quarter at a time before it is
// placed, with [ and ] as in WKB.exe (0x64ff22). For each of the four turns:
// - the plan of a Blacksmith (6 x 5 tiles, its origin and its city rectangle off centre) has the
//   footprint's tiles and the city rectangle turned about the building's tile, as WKB.exe turns
//   them (0x59a420: (x, z) to (-z, x) a quarter; 0x59a790 the rectangle's corners);
// - a Manor (5 x 6) stamped down in play away from the cities, through the stampdown message
//   with the orientation, faces that way and takes the turned tiles.
// A message without the orientation (as in recordings from before) places it unturned. A save
// and its reload keep the orientations and the tiles. Prints "TURN" lines, then "TURN all ok"
// or "TURN FAILED".
void Test_TurnedPlacement()
{
	LoadBCP("data.bcp");
	bool ok = true;
	auto turn = [](int x, int z, int q) {
		for (int i = 0; i < q; i++) {
			const int nx = -z;
			z = x;
			x = nx;
		}
		return std::pair<int, int>{ x, z };
	};
	auto turnF = [](float x, float z, int q) {
		for (int i = 0; i < q; i++) {
			const float nx = -z;
			z = x;
			x = nx;
		}
		return std::pair<float, float>{ x, z };
	};
	const float quarter = 1.5707964f;
	// what a save keeps: object id, quarter, its tiles
	struct Placed { uint32_t id; int q; std::set<std::pair<int, int>> tiles; };
	std::vector<Placed> placed;
	const std::string savePath = PathToUtf8(g_userPath / "saved" / PathFromUtf8("Save_Games\\wkbai turned placement.sav"));
	{
		Server server;
		server.loadSaveGame("Save_Games\\2 Player 4 Peaks.lvl");
		ServerGameObject* player = server.findObject(1027);
		const GameObjBlueprint* smith = server.gameSet->findBlueprint(Tags::GAMEOBJCLASS_BUILDING, "Blacksmith Foundation");
		const GameObjBlueprint* manor = server.gameSet->findBlueprint(Tags::GAMEOBJCLASS_BUILDING, "Manor Foundation");
		if (!player || !server.terrain || !smith || !smith->footprint || !manor || !manor->footprint) {
			printf("TURN not set up\nTURN FAILED\n");
			fflush(stdout);
			return;
		}
		const auto [tw, th] = server.terrain->getNumPlayableTiles();
		// the spot the preview gives for the cursor's tile (tx, tz), turned q quarters
		auto spotAt = [&](const Footprint* fp, int tx, int tz, int q) {
			const auto [rx, rz] = turnF(fp->originX, fp->originZ, q);
			return Vector3(tx * 5.0f + 2.5f + rx, 0.0f, tz * 5.0f + 2.5f + rz);
		};
		auto expectedTiles = [&](const Footprint* fp, int tx, int tz, int q) {
			std::set<std::pair<int, int>> tiles;
			for (const auto& to : fp->tiles) {
				const auto [rx, rz] = turn(to.offsetX, to.offsetZ, q);
				tiles.insert({ tx + rx, tz + rz });
			}
			return tiles;
		};
		auto extent = [](const std::set<std::pair<int, int>>& tiles) {
			int x0 = 1 << 30, x1 = -(1 << 30), z0 = 1 << 30, z1 = -(1 << 30);
			for (const auto& [x, z] : tiles) {
				x0 = std::min(x0, x); x1 = std::max(x1, x);
				z0 = std::min(z0, z); z1 = std::max(z1, z);
			}
			return std::pair<int, int>{ x1 - x0 + 1, z1 - z0 + 1 };
		};

		// the Blacksmith's plan at one tile, turned
		{
			const Footprint* fp = smith->footprint;
			const int tx = 110, tz = 30;
			for (int q = 0; q < 4; q++) {
				const Vector3 orientation(0.0f, q * quarter, 0.0f);
				const StampdownPlan plan = StampdownPlan::getStampdownPlan(&server, player, smith, spotAt(fp, tx, tz, q), orientation);
				std::set<std::pair<int, int>> got;
				for (const auto& v : plan.tiles)
					got.insert({ v.x, v.z });
				const auto want = expectedTiles(fp, tx, tz, q);
				const auto [w, d] = extent(got);
				const auto [c0x, c0z] = turn(fp->wallOriginDefaultMinX, fp->wallOriginDefaultMinY, q);
				const auto [c1x, c1z] = turn(fp->wallOriginDefaultMaxX, fp->wallOriginDefaultMaxY, q);
				const CommonGameObject::CityRectangle r = plan.cityRectangle;
				const bool rectOk = r.xStart == std::min(c0x, c1x) + tx && r.xEnd == std::max(c0x, c1x) + tx
					&& r.yStart == std::min(c0z, c1z) + tz && r.yEnd == std::max(c0z, c1z) + tz;
				const bool tilesOk = got == want && got.size() == fp->tiles.size();
				printf("TURN Blacksmith plan, %d quarter(s): %zu tiles %d x %d, %s; city rectangle x %d..%d z %d..%d, %s\n", q, got.size(), w, d,
					tilesOk ? "as turned" : "NOT as turned", r.xStart, r.xEnd, r.yStart, r.yEnd, rectOk ? "as turned" : "NOT as turned");
				ok &= tilesOk && rectOk;
			}
		}

		// Manors stamped down in play through the message, one for each turn, then one sent without
		// the orientation
		player->setItem(Tags::PDITEM_WOOD, 100000.0f);
		player->setItem(Tags::PDITEM_FOOD, 100000.0f);
		player->setItem(Tags::PDITEM_GOLD, 100000.0f);
		player->setItem(Tags::PDITEM_STONE, 100000.0f);
		// (a second Manor needs the player's "Can Build Manor", which the Castle gives: the server
		// refuses a build-menu stampdown its command doesn't allow)
		player->setItem(server.gameSet->items.names.getIndex("Can Build Manor"), 1.0f);
		const Footprint* fp = manor->footprint;
		auto send = [&](const Vector3& at, const Vector3* orientation) -> ServerGameObject* {
			NetPacketWriter msg(NETSRVMSG_STAMPDOWN);
			msg.writeUint32(manor->getFullId());
			msg.writeUint32(player->id);
			msg.writeVector3(at);
			msg.writeUint8(2); // in gameplay
			if (orientation)
				msg.writeVector3(*orientation);
			server.handlePacket(0, std::string((const char*)msg.data, msg.size()));
			return server.clientInfos.empty() ? nullptr : server.clientInfos[0].lastStampdown.get();
		};
		int startX = 20, startZ = 60;
		for (int q = 0; q <= 4; q++) {
			const int turns = q % 4; // the fifth: without the orientation, unturned
			const Vector3 orientation(0.0f, turns * quarter, 0.0f);
			ServerGameObject* obj = nullptr;
			int tx = 0, tz = 0;
			// the first spot, along the map from the west, that takes it so turned
			// (on the last one's row east of it, then the rows after it from the west: trees keep
			// buildings off since the play test of 2026-11-16, and the row filled up)
			for (int z = startZ; z < th - 20 && !obj; z += 3)
				for (int x = z == startZ ? startX : 20; x < tw - 20 && !obj; x += 3) {
					Vector3 at = spotAt(fp, x, z, turns);
					if (StampdownPlan::getStampdownPlan(&server, player, manor, at, orientation).status != StampdownPlan::Status::Valid)
						continue;
					at.y = server.terrain->getHeight(at.x, at.z);
					obj = send(at, q < 4 ? &orientation : nullptr);
					tx = x;
					tz = z;
				}
			if (!obj) {
				printf("TURN Manor, %d quarter(s): no spot takes it\n", turns);
				ok = false;
				continue;
			}
			startX = tx + 12;
			startZ = tz;
			std::set<std::pair<int, int>> got;
			for (int z = 0; z < th; z++)
				for (int x = 0; x < tw; x++)
					if (server.tiles[z * tw + x].building == obj->id)
						got.insert({ x, z });
			const auto want = expectedTiles(fp, tx, tz, turns);
			const auto [w, d] = extent(got);
			const int faces = (int)std::lround(obj->orientation.y / quarter);
			const bool good = got == want && faces == turns;
			printf("TURN Manor %u at tile (%d, %d), %d quarter(s)%s: faces %.4f (%d), %zu tiles %d x %d, %s\n", obj->id, tx, tz, turns,
				q == 4 ? " (message without the orientation)" : "", obj->orientation.y, faces, got.size(), w, d, good ? "as turned" : "NOT as turned");
			ok &= good;
			placed.push_back({ obj->id, turns, got });
		}
		ok &= server.saveGame(savePath.c_str());
	}
	{
		Server server;
		server.loadSaveGame(savePath.c_str());
		const auto [tw, th] = server.terrain->getNumPlayableTiles();
		for (const Placed& p : placed) {
			ServerGameObject* obj = server.findObject(p.id);
			std::set<std::pair<int, int>> got;
			if (obj)
				for (int z = 0; z < th; z++)
					for (int x = 0; x < tw; x++)
						if (server.tiles[z * tw + x].building == obj->id)
							got.insert({ x, z });
			const bool good = obj && (int)std::lround(obj->orientation.y / quarter) == p.q && got == p.tiles;
			int missing = 0;
			for (const auto& t : p.tiles)
				if (!got.count(t)) {
					missing++;
					const CommonGameObject* holder = server.tiles[t.second * tw + t.first].building.getFrom(&server);
					printf("TURN   tile (%d, %d) held by %s\n", t.first, t.second, holder ? holder->blueprint->name.c_str() : "nothing");
				}
			printf("TURN reloaded Manor %u: %s (faces %.4f, %zu tiles, %d of the saved ones missing)\n", p.id, good ? "same orientation and tiles" : "DIFFERENT",
				obj ? obj->orientation.y : -1.0f, got.size(), missing);
			ok &= good;
		}
	}
	// recorded and replayed: a turned Manor sent by a client over a link (Server::network records
	// the message), then the recording played from its start: the same building, facing the same
	// way, on the same tiles
	const std::filesystem::path recording = g_userPath / "saved" / "Recordings" / "wkbai turned placement";
	Placed recorded{ 0, 3, {} };
	{
		Server server;
		server.loadSaveGame("Save_Games\\2 Player 4 Peaks.lvl");
		ServerGameObject* player = server.findObject(1027);
		const GameObjBlueprint* manor = server.gameSet->findBlueprint(Tags::GAMEOBJCLASS_BUILDING, "Manor Foundation");
		NetLocalBuffer toServer, toClient;
		NetLocalLink link(&toServer, &toClient);
		server.addClient(&link);
		player->setItem(Tags::PDITEM_WOOD, 100000.0f);
		player->setItem(Tags::PDITEM_GOLD, 100000.0f);
		player->setItem(server.gameSet->items.names.getIndex("Can Build Manor"), 1.0f);
		ok &= server.startRecording(recording, std::string());
		const Vector3 orientation(0.0f, 3 * quarter, 0.0f);
		const auto [rx, rz] = turnF(manor->footprint->originX, manor->footprint->originZ, 3);
		for (int z = 60; z < 200 && !recorded.id; z += 3)
			for (int x = 30; x < 200 && !recorded.id; x += 3) {
				Vector3 at(x * 5.0f + 2.5f + rx, 0.0f, z * 5.0f + 2.5f + rz);
				if (StampdownPlan::getStampdownPlan(&server, player, manor, at, orientation).status != StampdownPlan::Status::Valid)
					continue;
				at.y = server.terrain->getHeight(at.x, at.z);
				NetPacketWriter msg(NETSRVMSG_STAMPDOWN);
				msg.writeUint32(manor->getFullId());
				msg.writeUint32(player->id);
				msg.writeVector3(at);
				msg.writeUint8(2);
				msg.writeVector3(orientation);
				{
					std::lock_guard<std::mutex> lock(toServer.mutex);
					toServer.queue.push(NetPacket(msg.getString()));
				}
				server.network();
				if (ServerGameObject* obj = server.clientInfos[0].lastStampdown.get()) {
					recorded.id = obj->id;
					const auto [tw, th] = server.terrain->getNumPlayableTiles();
					for (int tz = 0; tz < th; tz++)
						for (int tx = 0; tx < tw; tx++)
							if (server.tiles[tz * tw + tx].building == obj->id)
								recorded.tiles.insert({ tx, tz });
					recorded.q = (int)std::lround(obj->orientation.y / quarter);
				}
			}
		for (int i = 0; i < 30; i++) {
			server.timeManager.pendingMs = server.timeManager.stepMs;
			if (server.timeManager.step())
				server.simulate();
		}
		server.stopRecording();
		server.removeClient(&link);
	}
	{
		Server server;
		server.loadSaveGame(PathToUtf8(recording / "start.sav").c_str());
		const bool started = server.startReplay(recording);
		while (started && server.replayEvent()) {}
		ServerGameObject* obj = recorded.id ? server.findObject(recorded.id) : nullptr;
		std::set<std::pair<int, int>> got;
		if (obj) {
			const auto [tw, th] = server.terrain->getNumPlayableTiles();
			for (int tz = 0; tz < th; tz++)
				for (int tx = 0; tx < tw; tx++)
					if (server.tiles[tz * tw + tx].building == obj->id)
						got.insert({ tx, tz });
		}
		const bool good = started && obj && obj->blueprint == server.gameSet->findBlueprint(Tags::GAMEOBJCLASS_BUILDING, "Manor Foundation")
			&& recorded.q == 3 && (int)std::lround(obj->orientation.y / quarter) == 3 && got == recorded.tiles;
		printf("TURN recorded Manor %u, %d quarter(s), %zu tiles; replayed: %s\n", recorded.id, recorded.q, recorded.tiles.size(),
			good ? "the same" : "DIFFERENT");
		ok &= good;
	}
	printf(ok ? "TURN all ok\n" : "TURN FAILED\n");
	fflush(stdout);
}

// A mass order (needs the game data): "massOrder": { "level": "<.lvl>", "units": n, "seconds": s }
// (default Tribes Unite, 200, 60). n characters of the first player's first character's type
// stand in a block by it and are all sent with one Move to the far side of the map, as a human
// right-clicking an army there. Prints "MASSORDER" lines: the order's step and the next ones
// (ms, the path finding's part), then after s game seconds how many arrived within 25 m and a
// hash of their positions. WKB_PATH_NOCACHE / WKB_PATH_NOFIELDS switch the path finding's
// cache and group fields off (MovementController.cpp).
void Test_MassOrder()
{
	LoadBCP("data.bcp");
	const nlohmann::json cfg = g_settings.value("massOrder", nlohmann::json::object());
	const std::string level = cfg.value("level", std::string("6 Player Tribes Unite.lvl"));
	const int units = cfg.value("units", 200);
	const int seconds = cfg.value("seconds", 60);
	Server server;
	server.loadSaveGame(("Save_Games\\" + level).c_str());
	ServerGameObject* player = nullptr;
	ServerGameObject* first = nullptr;
	auto find = [&](CommonGameObject* obj, auto& self) -> void {
		for (const auto& [type, children] : obj->children)
			for (CommonGameObject* child : children) {
				if (!first && child->blueprint->bpClass == Tags::GAMEOBJCLASS_CHARACTER)
					first = child->dyncast<ServerGameObject>();
				self(child, self);
			}
	};
	for (const auto& [type, children] : server.getLevel()->children)
		for (CommonGameObject* p : children)
			if (!first && p->blueprint->bpClass == Tags::GAMEOBJCLASS_PLAYER && p->blueprint->name != "Neutral Player") {
				find(p, find);
				if (first)
					player = p->dyncast<ServerGameObject>();
			}
	const int moveCommand = server.gameSet->commands.names.getIndex("Move");
	if (!player || !first || moveCommand < 0) {
		printf("MASSORDER not set up\n");
		return;
	}
	const auto [tw, th] = server.terrain->getNumPlayableTiles();
	std::vector<SrvGORef> army;
	const int side = (int)std::ceil(std::sqrt((float)units));
	for (int i = 0; i < units; i++) {
		Vector3 pos = first->position + Vector3((float)(i % side) * 2.0f - side, 0, (float)(i / side) * 2.0f + 4.0f);
		pos = MovementController::nearestValidPosition(first->blueprint, pos);
		army.push_back(server.spawnObject(first->blueprint, player, pos, Vector3(0, 0, 0)));
	}
	Vector3 goal(tw * 5.0f - first->position.x, 0, th * 5.0f - first->position.z);
	goal = MovementController::nearestValidPosition(first->blueprint, goal);
	printf("MASSORDER %d %s of player %u at (%.0f,%.0f) to (%.0f,%.0f), map %dx%d tiles\n", units, first->blueprint->name.c_str(), player->id,
		first->position.x, first->position.z, goal.x, goal.z, tw, th);
	extern double g_pathfindingMs;
	server.timeManager.unpause();
	server.timeManager.stepMs = 20;
	auto step = [&]() {
		server.timeManager.pendingMs = server.timeManager.stepMs;
		if (server.timeManager.step())
			server.simulate();
	};
	for (int i = 0; i < 50; i++)
		step();
	for (SrvGORef& u : army)
		if (u)
			server.gameSet->commands[moveCommand].execute(u.get(), nullptr, Tags::ORDERASSIGNMODE_FORGET_EVERYTHING_ELSE, goal);
	double totalPath = 0, worst = 0, stepsMs = 0;
	std::vector<double> sortedMs; // the steps after the order's
	for (int i = 0; i < seconds * 50; i++) {
		const double pathBefore = g_pathfindingMs;
		extern double g_pathBatchMs[5];
		extern uint64_t g_pathBatchRequests, g_pathBatchSearches;
		const double batch[5] = { g_pathBatchMs[0], g_pathBatchMs[1], g_pathBatchMs[2], g_pathBatchMs[3], g_pathBatchMs[4] };
		const uint64_t batchRequests = g_pathBatchRequests, batchSearches = g_pathBatchSearches;
		const uint64_t t0 = SDL_GetPerformanceCounter();
		step();
		const double ms = (double)(SDL_GetPerformanceCounter() - t0) * 1000.0 / (double)SDL_GetPerformanceFrequency();
		const double path = g_pathfindingMs - pathBefore;
		totalPath += path;
		worst = std::max(worst, ms);
		if (i > 0) {
			stepsMs += ms;
			sortedMs.push_back(ms);
		}
		if (i < 5)
			printf("MASSORDER step %d: %.2f ms, path finding %.2f ms (end of step: %llu requests, %llu searches; fill %.2f, lines %.2f, fields %.2f, search %.2f, apply %.2f ms)\n", i, ms, path,
				(unsigned long long)(g_pathBatchRequests - batchRequests), (unsigned long long)(g_pathBatchSearches - batchSearches),
				g_pathBatchMs[0] - batch[0], g_pathBatchMs[1] - batch[1], g_pathBatchMs[2] - batch[2], g_pathBatchMs[3] - batch[3], g_pathBatchMs[4] - batch[4]);
		if ((i + 1) % 1000 == 0) {
			int alive = 0, moving = 0;
			float sum = 0, nearest = 1e9f;
			for (SrvGORef& u : army)
				if (u) {
					alive++;
					moving += u->movementController.isMoving();
					const float d = (u->position - goal).len2xz();
					sum += d;
					nearest = std::min(nearest, d);
				}
			printf("MASSORDER t=%d s: %d alive, %d moving, distance to the goal mean %.0f m, nearest %.0f m\n", (i + 1) / 50, alive, moving, alive ? sum / alive : 0.0f, nearest);
		}
	}
	int arrived = 0;
	uint64_t hash = 1469598103934665603ull;
	for (SrvGORef& u : army)
		if (u) {
			arrived += (u->position - goal).len2xz() < 25.0f;
			for (float f : { u->position.x, u->position.z }) {
				uint32_t b; memcpy(&b, &f, 4);
				for (int k = 0; k < 4; k++) { hash ^= (b >> (k * 8)) & 0xFF; hash *= 1099511628211ull; }
			}
		}
	std::sort(sortedMs.begin(), sortedMs.end());
	if (!sortedMs.empty())
		printf("MASSORDER steps after the order's: mean %.2f ms, median %.2f ms, 95th percentile %.2f ms\n", stepsMs / sortedMs.size(),
			sortedMs[sortedMs.size() / 2], sortedMs[sortedMs.size() * 95 / 100]);
	printf("MASSORDER end after %d s: path finding %.1f ms in all, longest step %.1f ms; %d of %d within 25 m of the goal; positions hash %016llx\n",
		seconds, totalPath, worst, arrived, units, (unsigned long long)hash);
	fflush(stdout);
}

// Gatherers through a save (needs the game data): on 4 Peaks, player 1027's peasants chop wood,
// more spawned beside them mine gold and stone, build a farm and farm a field. After
// WKB_GSAVE_SECONDS game seconds (default 45) the game is saved, run on for WKB_GSAVE_AFTER more
// (default 30) and its state noted; then the save is loaded into a new server and run as long.
// Each worker's order, task, its state, target, proximity and triggers, animation, load and
// place are printed before saving ("GSAVE before"), just after loading ("GSAVE loaded") and at
// the end of both runs ("GSAVE end saved-run" / "GSAVE end loaded-run"), with the differences
// and "GSAVE idle <n>" (workers without an order); the play test's woodcutters stood idle after
// loading. Ends "GSAVE loaded-same <a>/<n> end-same <b>/<n> idle-after-load <c>".
struct GatherSaveWorker { uint32_t id; std::string job; };

static std::string GatherSaveState(Server& server, ServerGameObject* go, bool withPlace)
{
	if (!go)
		return "gone";
	const GameSet& gs = *server.gameSet;
	char buf[512];
	std::string order = "-", task = "-", target = "-";
	int taskState = -1, prox = -1, trig = -1;
	if (Order* o = go->orderConfig.getCurrentOrder()) {
		order = gs.orders.getString(o->blueprint->bpid) + "(" + std::to_string(o->state) + ")";
		if (Task* tk = o->getCurrentTask()) {
			task = gs.tasks.getString(tk->blueprint->bpid);
			taskState = tk->state;
			prox = tk->proximitySatisfied;
			trig = tk->triggersStarted;
			if (tk->target)
				target = std::to_string(tk->target->id);
		}
	}
	std::string items;
	for (const char* name : { "Wood", "Gold", "Stone", "Food" }) {
		const int i = gs.items.names.getIndex(name);
		if (i >= 0 && go->getItem(i) != 0.0f)
			items += std::string(" ") + name + "=" + std::to_string((int)std::round(go->getItem(i)));
	}
	const std::string anim = go->animationIndex > 0 ? gs.animations.names.getString(go->animationIndex) : "-";
	snprintf(buf, sizeof(buf), "%s / %s(%d) -> %s prox=%d trig=%d anim=%s moving=%d%s", order.c_str(), task.c_str(), taskState,
		target.c_str(), prox, trig, anim.c_str(), (int)go->movementController.isMoving(), items.c_str());
	std::string s = buf;
	if (withPlace) {
		snprintf(buf, sizeof(buf), " at (%.1f,%.1f)", go->position.x, go->position.z);
		s += buf;
	}
	return s;
}

static void GatherSaveStep(Server& server, int seconds)
{
	for (int i = 0; i < seconds * 50; i++) {
		server.timeManager.pendingMs = server.timeManager.stepMs; // a step without the real clock
		if (server.timeManager.step())
			server.simulate();
	}
}

// The rally part of test 43: player 1027's Manor sends its new peasants to chop a tree (the
// building's "Chop Tree" for spawned units); the tree is felled and removed, a peasant trained;
// then saved, loaded and another trained. Prints "GSAVE rally <step>: ..." and
// "GSAVE rally felled-ok <0|1> loaded-kept <0|1> loaded-ok <0|1>".
static std::string RallyWorker(Server& server, ServerGameObject* go)
{
	if (!go)
		return "none";
	Order* o = go->orderConfig.getCurrentOrder();
	Task* tk = o ? o->getCurrentTask() : nullptr;
	return std::to_string(go->id) + " " + (o ? server.gameSet->orders.getString(o->blueprint->bpid) : std::string("idle"))
		+ (tk && tk->target ? " -> " + tk->target->blueprint->name + " #" + std::to_string(tk->target->id) : std::string());
}

static ServerGameObject* RallySpawn(Server& server, ServerGameObject* manor, ServerGameObject* player)
{
	const GameSet& gs = *server.gameSet;
	std::set<uint32_t> before;
	auto list = [&](CommonGameObject* obj, std::vector<ServerGameObject*>& out, auto& self) -> void {
		for (const auto& [type, children] : obj->children)
			for (CommonGameObject* child : children) {
				if (child->blueprint->name == "Peasant")
					out.push_back(child->dyncast<ServerGameObject>());
				self(child, out, self);
			}
	};
	std::vector<ServerGameObject*> peasants;
	list(player, peasants, list);
	for (ServerGameObject* p : peasants)
		before.insert(p->id);
	player->setItem(Tags::PDITEM_FOOD, 5000.0f);
	player->setItem(Tags::PDITEM_WOOD, 5000.0f);
	player->setItem(Tags::PDITEM_GOLD, 5000.0f);
	gs.commands[gs.commands.names.getIndex("Spawn Peasant")].execute(manor, nullptr, Tags::ORDERASSIGNMODE_DO_LAST);
	ServerGameObject* spawned = nullptr;
	for (int s = 0; s < 60 && !spawned; s++) {
		GatherSaveStep(server, 1);
		peasants.clear();
		list(player, peasants, list);
		for (ServerGameObject* p : peasants)
			if (!before.count(p->id))
				spawned = p;
	}
	GatherSaveStep(server, 5); // its order given, its start sequence run
	return spawned;
}

static void GatherSaveRally()
{
	const std::string name = "Save_Games\\wkbai rally save.sav";
	const std::string path = PathToUtf8(g_userPath / "saved" / PathFromUtf8(name));
	uint32_t manorId = 0, rallyId = 0;
	bool felledOk = false;
	{
		Server server;
		server.loadSaveGame("Save_Games\\2 Player 4 Peaks.lvl");
		g_settings["simSeed"] = 4243u;
		server.timeManager.unpause();
		server.startLevel();
		server.timeManager.stepMs = 20;
		const GameSet& gs = *server.gameSet;
		ServerGameObject* player = server.findObject(1027);
		ServerGameObject* manor = nullptr, * tree = nullptr;
		float best = 1e12f;
		auto find = [&](CommonGameObject* obj, auto& self) -> void {
			for (const auto& [type, children] : obj->children)
				for (CommonGameObject* child : children) {
					if (!manor && child->blueprint->name == "Manor" && child->getPlayer() == player)
						manor = child->dyncast<ServerGameObject>();
					self(child, self);
				}
		};
		find(server.getLevel(), find);
		auto findTree = [&](CommonGameObject* obj, auto& self) -> void {
			for (const auto& [type, children] : obj->children)
				for (CommonGameObject* child : children) {
					if (manor && child->blueprint->name == "Coniferous Tree")
						if (float d = (child->position - manor->position).sqlen2xz(); d < best) { best = d; tree = child->dyncast<ServerGameObject>(); }
					self(child, self);
				}
		};
		findTree(server.getLevel(), findTree);
		const int chop = gs.commands.names.getIndex("Chop Tree");
		if (!player || !manor || !tree || chop < 0 || gs.commands.names.getIndex("Spawn Peasant") < 0) {
			printf("GSAVE rally not set up\n");
			return;
		}
		manorId = manor->id;
		manor->setBuildingSpawnedUnitOrderToTarget(chop, tree);
		const uint32_t felled = tree->id;
		printf("GSAVE rally: Manor %u sends its peasants to tree %u (%.0f m), which is felled\n", manor->id, felled, std::sqrt(best));
		tree->terminate();
		tree->destroy();
		ServerGameObject* first = RallySpawn(server, manor, player);
		const std::string state = RallyWorker(server, first);
		Order* o = first ? first->orderConfig.getCurrentOrder() : nullptr;
		felledOk = o && gs.orders.getString(o->blueprint->bpid) == "Chop Tree";
		rallyId = manor->spawnedUnitTarget.objid;
		printf("GSAVE rally felled: spawned %s; the Manor's rally target now %u\n", state.c_str(), rallyId);
		server.timeManager.pause();
		printf("GSAVE rally saved %d\n", (int)server.saveGame(path.c_str()));
	}
	bool kept = false, loadedOk = false;
	{
		Server server;
		server.loadSaveGame(name.c_str());
		server.timeManager.stepMs = 20;
		ServerGameObject* manor = server.findObject(manorId);
		ServerGameObject* player = server.findObject(1027);
		kept = manor && manor->spawnedUnitCommand == server.gameSet->commands.names.getIndex("Chop Tree") && manor->spawnedUnitTarget.objid == rallyId;
		printf("GSAVE rally loaded: the Manor's spawned unit command %d, target %u\n", manor ? manor->spawnedUnitCommand : -1, manor ? manor->spawnedUnitTarget.objid : 0);
		ServerGameObject* second = manor && player ? RallySpawn(server, manor, player) : nullptr;
		Order* o = second ? second->orderConfig.getCurrentOrder() : nullptr;
		loadedOk = o && server.gameSet->orders.getString(o->blueprint->bpid) == "Chop Tree";
		printf("GSAVE rally after loading: spawned %s\n", RallyWorker(server, second).c_str());
	}
	printf("GSAVE rally felled-ok %d loaded-kept %d loaded-ok %d\n", (int)felledOk, (int)kept, (int)loadedOk);
	fflush(stdout);
}

void Test_GatherSave()
{
	LoadBCP("data.bcp");
	if (!getenv("WKB_GSAVE_FILE") && !getenv("WKB_GSAVE_NORALLY"))
		GatherSaveRally();
	const int seconds = getenv("WKB_GSAVE_SECONDS") ? atoi(getenv("WKB_GSAVE_SECONDS")) : 45;
	const int after = getenv("WKB_GSAVE_AFTER") ? atoi(getenv("WKB_GSAVE_AFTER")) : 30;
	const std::string name = "Save_Games\\wkbai gather save.sav";
	const std::string path = PathToUtf8(g_userPath / "saved" / PathFromUtf8(name));
	std::filesystem::create_directories(PathFromUtf8(path).parent_path());
	std::vector<GatherSaveWorker> workers;
	std::map<uint32_t, std::string> before, endSaved;
	// WKB_GSAVE_FILE=<save in Save_Games>: that save loaded instead (a play test's), player 1027's
	// gatherers and builders watched for WKB_GSAVE_AFTER seconds, each change printed
	if (const char* file = getenv("WKB_GSAVE_FILE")) {
		Server server;
		server.loadSaveGame((std::string("Save_Games\\") + file).c_str());
		server.timeManager.stepMs = 20;
		ServerGameObject* player = server.findObject(1027);
		std::map<uint32_t, std::string> last;
		auto collect = [&](CommonGameObject* obj, auto& self) -> void {
			for (const auto& [type, children] : obj->children)
				for (CommonGameObject* child : children) {
					ServerGameObject* so = child->dyncast<ServerGameObject>();
					if (child->blueprint->bpClass == Tags::GAMEOBJCLASS_CHARACTER)
						if (Order* o = so->orderConfig.getCurrentOrder()) {
							const std::string& n = server.gameSet->orders.getString(o->blueprint->bpid);
							if (n == "Chop Tree" || n == "Farm" || n == "Mine" || n == "Mine Gold" || n == "Build")
								workers.push_back({ so->id, n });
						}
					self(child, self);
				}
		};
		if (player)
			collect(player, collect);
		for (const auto& w : workers) {
			last[w.id] = GatherSaveState(server, server.findObject(w.id), true);
			printf("GSAVE file t=0 %u %s: %s\n", w.id, w.job.c_str(), last[w.id].c_str());
		}
		int idle = 0;
		for (int s = 1; s <= after; s++) {
			GatherSaveStep(server, 1);
			for (const auto& w : workers) {
				ServerGameObject* go = server.findObject(w.id);
				const std::string now = GatherSaveState(server, go, false);
				std::string& prev = last[w.id];
				if (now != prev) {
					printf("GSAVE file t=%d %u %s: %s\n", s, w.id, w.job.c_str(), now.c_str());
					prev = now;
				}
			}
		}
		for (const auto& w : workers)
			if (ServerGameObject* go = server.findObject(w.id); go && !go->orderConfig.getCurrentOrder())
				idle++;
		printf("GSAVE file workers %zu idle-after-load %d\n", workers.size(), idle);
		fflush(stdout);
		return;
	}
	{
		Server server;
		server.loadSaveGame("Save_Games\\2 Player 4 Peaks.lvl");
		g_settings["simSeed"] = 4242u;
		server.timeManager.unpause();
		server.startLevel();
		server.timeManager.stepMs = 20;
		const GameSet& gs = *server.gameSet;
		ServerGameObject* player = server.findObject(1027);
		const GameObjBlueprint* peasantType = gs.findBlueprint(Tags::GAMEOBJCLASS_CHARACTER, "Peasant");
		std::vector<ServerGameObject*> peasants, trees, gold, stone;
		ServerGameObject* manor = nullptr;
		const int goldItem = gs.items.names.getIndex("Gold"), stoneItem = gs.items.names.getIndex("Stone");
		auto find = [&](CommonGameObject* obj, auto& self) -> void {
			for (const auto& [type, children] : obj->children)
				for (CommonGameObject* child : children) {
					ServerGameObject* so = child->dyncast<ServerGameObject>();
					if (so->getPlayer() == player && child->blueprint->name == "Peasant")
						peasants.push_back(so);
					else if (so->getPlayer() == player && child->blueprint->name == "Manor" && !manor)
						manor = so;
					else if (child->blueprint->name == "Coniferous Tree")
						trees.push_back(so);
					else if (child->blueprint->bpClass == Tags::GAMEOBJCLASS_CONTAINER && goldItem >= 0 && so->getItem(goldItem) > 0.0f)
						gold.push_back(so);
					else if (child->blueprint->bpClass == Tags::GAMEOBJCLASS_CONTAINER && stoneItem >= 0 && so->getItem(stoneItem) > 0.0f)
						stone.push_back(so);
					self(child, self);
				}
		};
		find(server.getLevel(), find);
		if (!player || !peasantType || !manor || peasants.empty() || trees.empty()) {
			printf("GSAVE not set up\n");
			return;
		}
		auto nearest = [](std::vector<ServerGameObject*>& list, const Vector3& at) -> ServerGameObject* {
			ServerGameObject* best = nullptr;
			float bd = 1e12f;
			for (ServerGameObject* o : list)
				if (float d = (o->position - at).sqlen2xz(); d < bd) { bd = d; best = o; }
			return best;
		};
		auto command = [&](const char* cmd, ServerGameObject* unit, ServerGameObject* target) {
			const int c = gs.commands.names.getIndex(cmd);
			if (c >= 0 && target)
				gs.commands[c].execute(unit, target, Tags::ORDERASSIGNMODE_FORGET_EVERYTHING_ELSE);
		};
		// the level's peasants and four more chop wood; two mine gold, two stone
		for (int i = 0; i < 4; i++)
			peasants.push_back(server.spawnObject(peasantType, player, manor->position + Vector3(12.0f + 2.0f * i, 0, 12.0f), Vector3(0, 0, 0)));
		for (ServerGameObject* p : peasants) {
			command("Chop Tree", p, nearest(trees, p->position));
			workers.push_back({ p->id, "wood" });
		}
		for (int i = 0; i < 4; i++) {
			const bool isGold = i < 2;
			ServerGameObject* p = server.spawnObject(peasantType, player, manor->position + Vector3(-12.0f - 2.0f * i, 0, 12.0f), Vector3(0, 0, 0));
			ServerGameObject* target = nearest(isGold ? gold : stone, p->position);
			if (!target)
				continue;
			command(isGold ? "Mine Gold" : "Mine", p, target);
			workers.push_back({ p->id, isGold ? "gold" : "stone" });
		}
		// a builder at a farm's foundation, and a farmer on a finished field
		const GameObjBlueprint* foundationType = gs.findBlueprint(Tags::GAMEOBJCLASS_BUILDING, "Standard Farm Foundation");
		const GameObjBlueprint* fieldType = gs.findBlueprint(Tags::GAMEOBJCLASS_BUILDING, "Standard Farm");
		std::vector<Vector3> spots;
		for (int i = 0; i < 80 && spots.size() < 2; i++) {
			const Vector3 at = manor->position + Vector3(-40.0f + 10.0f * (i % 9), 0, 25.0f + 10.0f * (i / 9));
			bool clear = true;
			for (const Vector3& s : spots)
				clear = clear && (s - at).len2xz() > 20.0f;
			if (clear && foundationType && StampdownPlan::getStampdownPlan(&server, player, foundationType, at, Vector3(0, 0, 0)).status == StampdownPlan::Status::Valid)
				spots.push_back(at);
		}
		if (spots.size() == 2 && fieldType) {
			ServerGameObject* foundation = server.spawnObject(foundationType, player, spots[0], Vector3(0, 0, 0));
			ServerGameObject* builder = server.spawnObject(peasantType, player, manor->position + Vector3(0, 0, 20), Vector3(0, 0, 0));
			command("Build", builder, foundation);
			workers.push_back({ builder->id, "build" });
			ServerGameObject* field = server.spawnObject(fieldType, player, spots[1], Vector3(0, 0, 0));
			ServerGameObject* farmer = server.spawnObject(peasantType, player, manor->position + Vector3(4, 0, 20), Vector3(0, 0, 0));
			command("Farm", farmer, field);
			workers.push_back({ farmer->id, "farm" });
		}
		else
			printf("GSAVE no room for the farm (%zu spots)\n", spots.size());
		GatherSaveStep(server, seconds);
		server.timeManager.pause();
		for (const auto& w : workers) {
			before[w.id] = GatherSaveState(server, server.findObject(w.id), true);
			printf("GSAVE before %u %s: %s\n", w.id, w.job.c_str(), before[w.id].c_str());
		}
		printf("GSAVE saved %d\n", (int)server.saveGame(path.c_str()));
		server.timeManager.unpause();
		GatherSaveStep(server, after);
		for (const auto& w : workers)
			endSaved[w.id] = GatherSaveState(server, server.findObject(w.id), true);
	}
	int loadedSame = 0, endSame = 0, idle = 0;
	{
		Server server;
		server.loadSaveGame(name.c_str());
		server.timeManager.stepMs = 20;
		for (const auto& w : workers) {
			const std::string now = GatherSaveState(server, server.findObject(w.id), true);
			const bool same = now == before[w.id];
			loadedSame += same;
			printf("GSAVE loaded %u %s: %s%s\n", w.id, w.job.c_str(), now.c_str(), same ? "" : "   <-- differs");
		}
		GatherSaveStep(server, after);
		for (const auto& w : workers) {
			ServerGameObject* go = server.findObject(w.id);
			const std::string now = GatherSaveState(server, go, true);
			const bool same = now == endSaved[w.id];
			endSame += same;
			if (go && !go->orderConfig.getCurrentOrder())
				idle++;
			printf("GSAVE end saved-run  %u %s: %s\n", w.id, w.job.c_str(), endSaved[w.id].c_str());
			printf("GSAVE end loaded-run %u %s: %s%s\n", w.id, w.job.c_str(), now.c_str(), same ? "" : "   <-- differs");
		}
	}
	printf("GSAVE idle %d\n", idle);
	printf("GSAVE loaded-same %d/%zu end-same %d/%zu idle-after-load %d\n", loadedSame, workers.size(), endSame, workers.size(), idle);
	fflush(stdout);
}

// Corpses (needs the game data): spearmen of player 1027, ten in a formation and six loose,
// fight spearmen of 1030 on 4 Peaks; the survivors are then sent 60 m away. Each unit that
// dies becomes its corpse ("Dead Spearman") in place; the corpse must not move after that. The
// commands go to every unit the test set out with, the dead too, as the AI's lists did.
// Prints "CORPSE t=<s> <id> <name> parent=<class> moved=<m> moving=<0|1>" every 5 game seconds
// for each corpse, and "CORPSE corpses=<n> moved=<n> max=<m>" at the end (moved: > 0.05 m).
void Test_Corpses()
{
	LoadBCP("data.bcp");
	Server server;
	server.loadSaveGame("Save_Games\\2 Player 4 Peaks.lvl");
	ServerGameObject* own = server.findObject(1027);
	ServerGameObject* enemy = server.findObject(1030);
	const GameObjBlueprint* bp = server.gameSet->findBlueprint(Tags::GAMEOBJCLASS_CHARACTER, getenv("WKB_CORPSE_UNIT") ? getenv("WKB_CORPSE_UNIT") : "Spearman");
	const int attack = server.gameSet->commands.names.getIndex("Attack");
	const int move = server.gameSet->commands.names.getIndex("Move");
	if (!own || !enemy || !bp || attack < 0 || move < 0 || server.gameSet->objBlueprints[Tags::GAMEOBJCLASS_FORMATION].size() == 0) {
		printf("CORPSE not set up\n");
		return;
	}
	// a flat-ish spot near the middle of the map
	const auto area = server.terrain->getPlayableArea();
	const Vector3 centre(area.first * 0.5f, 0, area.second * 0.5f);
	auto at = [&](float x, float z) { Vector3 p = centre + Vector3(x, 0, z); p.y = server.terrain->getHeight(p.x, p.z); return p; };
	std::vector<SrvGORef> ownUnits, enemyUnits;
	ServerGameObject* formation = server.spawnObject(&server.gameSet->objBlueprints[Tags::GAMEOBJCLASS_FORMATION][0], own, {}, {});
	Vector3 sum;
	for (int i = 0; i < 10; i++) {
		ServerGameObject* u = server.spawnObject(bp, own, at(-6.0f + (i % 5) * 3.0f, -4.0f + (i / 5) * 3.0f), Vector3(0, 0, 0));
		sum += u->position;
		u->setParent(formation);
		ownUnits.emplace_back(u);
	}
	formation->setPosition(sum / 10.0f);
	formation->sendEvent(Tags::PDEVENT_ON_SPAWN);
	for (int i = 0; i < 6; i++)
		ownUnits.emplace_back(server.spawnObject(bp, own, at(-20.0f + i * 3.0f, 6.0f), Vector3(0, 0, 0)));
	for (int i = 0; i < 16; i++)
		enemyUnits.emplace_back(server.spawnObject(bp, enemy, at(-15.0f + (i % 8) * 3.0f, 14.0f + (i / 8) * 3.0f), Vector3(0, 3.14159f, 0)));
	// the enemy is weakened so that it loses, and its dead lie amid the survivors
	const int hp = server.gameSet->items.names.getIndex("Hit Points");
	for (auto& r : enemyUnits)
		r->setItem(hp, r->getItem(hp) * 0.5f);
	server.timeManager.unpause();
	server.timeManager.stepMs = 20;
	auto nearest = [](ServerGameObject* from, std::vector<SrvGORef>& list) -> ServerGameObject* {
		ServerGameObject* best = nullptr;
		float bd = 1e30f;
		for (auto& r : list)
			if (r && r->blueprint->name.rfind("Dead ", 0) != 0) {
				const float d = (r->position - from->position).sqlen2xz();
				if (d < bd) { bd = d; best = r.get(); }
			}
		return best;
	};
	struct Corpse { SrvGORef ref; Vector3 pos; float since; float maxMoved = 0.0f; };
	std::map<uint32_t, Corpse> corpses;
	// (the formation breaks up when fewer than 8 are left in it, as WKB.exe's: members hit on the
	// flank leave it, as the game set's human-player rule has it; then its survivors go on loose)
	SrvGORef formationRef(formation);
	auto anchor = [&]() -> ServerGameObject* {
		if (formationRef)
			return formationRef.get();
		for (auto& r : ownUnits)
			if (r && r->blueprint->name.rfind("Dead ", 0) != 0)
				return r.get();
		return nullptr;
	};
	std::vector<SrvGORef> all = ownUnits;
	all.insert(all.end(), enemyUnits.begin(), enemyUnits.end());
	bool sentAway = false;
	int lastPrint = -1;
	for (int step = 0; step * 20 < 120000; step++) {
		const float t = step * 0.02f;
		// every 3 s, the living attack the nearest living enemy (the formation as a whole)
		if (step % 150 == 0 && !sentAway) {
			if (formationRef)
				if (ServerGameObject* tg = nearest(formationRef.get(), enemyUnits))
					server.gameSet->commands[attack].execute(formationRef.get(), tg, Tags::ORDERASSIGNMODE_FORGET_EVERYTHING_ELSE);
			// to everyone it set out with, the dead too: the AI's lists kept a unit's id, which its
			// corpse keeps (it is the unit converted in place); the corpses got "Attack" and walked
			for (auto& r : all) {
				if (!r || (formationRef && r->getParent() == formationRef.get()))
					continue;
				if (ServerGameObject* tg = nearest(r.get(), r->getParent() == enemy ? ownUnits : enemyUnits))
					server.gameSet->commands[attack].execute(r.get(), tg, Tags::ORDERASSIGNMODE_FORGET_EVERYTHING_ELSE);
			}
		}
		// once the enemy is beaten (or after 60 s), the survivors walk 60 m away
		if (!sentAway && (t > 60.0f || !anchor() || !nearest(anchor(), enemyUnits))) {
			sentAway = true;
			printf("CORPSE t=%.1f survivors sent away (the formation %s)\n", t, formationRef ? "stands" : "broke up");
			if (formationRef)
				server.gameSet->commands[move].execute(formationRef.get(), nullptr, Tags::ORDERASSIGNMODE_FORGET_EVERYTHING_ELSE, formationRef->position + Vector3(60, 0, 0));
			for (auto& r : all)
				if (r && !(formationRef && r->getParent() == formationRef.get()))
					server.gameSet->commands[move].execute(r.get(), nullptr, Tags::ORDERASSIGNMODE_FORGET_EVERYTHING_ELSE, r->position + Vector3(60, 0, 0));
		}
		server.timeManager.pendingMs = server.timeManager.stepMs;
		if (server.timeManager.step())
			server.simulate();
		for (auto& r : all) {
			if (!r || r->blueprint->name.rfind("Dead ", 0) != 0)
				continue;
			auto it = corpses.find(r->id);
			if (it == corpses.end())
				corpses[r->id] = Corpse{ r, r->position, t };
			else
				it->second.maxMoved = std::max(it->second.maxMoved, (r->position - it->second.pos).len2xz());
		}
		if ((int)t / 5 != lastPrint) {
			lastPrint = (int)t / 5;
			for (auto& [id, c] : corpses) {
				if (!c.ref)
					continue;
				ServerGameObject* o = c.ref.get();
				ServerGameObject* p = o->getParent();
				printf("CORPSE t=%.0f %u %s parent=%s moved=%.2f moving=%d mc=%d order=%s\n", t, id, o->blueprint->name.c_str(),
					p ? Tags::GAMEOBJCLASS_tagDict.getStringFromID(p->blueprint->bpClass) : "-", (o->position - c.pos).len2xz(),
					(int)o->movement.isMoving(), (int)o->movementController.isMoving(),
					o->orderConfig.getCurrentOrder() ? server.gameSet->orders.getString(o->orderConfig.getCurrentOrder()->blueprint->bpid).c_str() : "-");
			}
		}
	}
	int moved = 0;
	float maxMoved = 0.0f;
	for (auto& [id, c] : corpses) {
		if (c.maxMoved > 0.05f)
			moved++;
		maxMoved = std::max(maxMoved, c.maxMoved);
	}
	printf("CORPSE corpses=%zu moved=%d max=%.2f\n", corpses.size(), moved, maxMoved);
	fflush(stdout);
}

// Hover cursors: a local server and client on 4 Peaks, the player's peasant, a soldier and
// objects to point at (its farm, a foundation, the Manor, a damaged building, a watch tower, a
// training dummy, the enemy's Manor and a peasant of the enemy's). For each pair prints
// "HOVER <unit> over <object>: <command> <cursor file>" (the right-click's command and the
// cursor shown) and the other commands offered there; "hoverCursor": { "verbose": true } in
// wkconfig.json also prints each offered command's failing cursor conditions.
void Test_HoverCursor()
{
	LoadBCP("data.bcp");
	const bool verbose = g_settings.value("hoverCursor", nlohmann::json::object()).value("verbose", false);
	Server server;
	Client client(true);
	NetLocalBuffer queue1, queue2;
	NetLocalLink srv2cli(&queue1, &queue2), cli2srv(&queue2, &queue1);
	server.addClient(&srv2cli);
	client.serverLink = &cli2srv;
	server.loadSaveGame("Save_Games\\2 Player 4 Peaks.lvl");
	ServerGameObject* player = server.findObject(1027);
	ServerGameObject* enemy = nullptr;
	for (const auto& [type, children] : server.getLevel()->children)
		for (CommonGameObject* p : children)
			if (p != player && p->blueprint->bpClass == Tags::GAMEOBJCLASS_PLAYER && p->blueprint->name != "Neutral Player" && !enemy)
				enemy = p->dyncast<ServerGameObject>();
	auto findIn = [](ServerGameObject* owner, const char* name) -> ServerGameObject* {
		ServerGameObject* found = nullptr;
		auto walk = [&](CommonGameObject* obj, auto& self) -> void {
			for (const auto& [type, children] : obj->children)
				for (CommonGameObject* child : children) {
					if (!found && child->blueprint->name == name)
						found = child->dyncast<ServerGameObject>();
					self(child, self);
				}
		};
		if (owner)
			walk(owner, walk);
		return found;
	};
	ServerGameObject* peasant = findIn(player, "Peasant");
	ServerGameObject* manor = findIn(player, "Manor");
	ServerGameObject* enemyManor = findIn(enemy, "Manor");
	ServerGameObject* enemyPeasant = findIn(enemy, "Peasant");
	if (!player || !enemy || !peasant || !manor) {
		printf("HOVER not set up\n");
		return;
	}
	server.timeManager.unpause();
	server.startLevel();
	auto spawn = [&](const char* name, ServerGameObject* owner, const Vector3& offset, int cls = Tags::GAMEOBJCLASS_BUILDING) -> ServerGameObject* {
		const GameObjBlueprint* bp = server.gameSet->findBlueprint(cls, name);
		if (!bp) {
			printf("HOVER no blueprint %s\n", name);
			return nullptr;
		}
		Vector3 at = manor->position + offset;
		if (cls == Tags::GAMEOBJCLASS_CHARACTER)
			at = MovementController::nearestValidPosition(bp, at);
		ServerGameObject* obj = server.spawnObject(bp, owner, at, Vector3(0, 0, 0));
		if (cls == Tags::GAMEOBJCLASS_BUILDING)
			obj->sendEvent(Tags::PDEVENT_ON_STAMPDOWN);
		return obj;
	};
	ServerGameObject* farm = spawn("Standard Farm", player, Vector3(40, 0, 0));
	ServerGameObject* foundation = spawn("Watch Tower Foundation", player, Vector3(-40, 0, 0));
	ServerGameObject* tower = spawn("Watch Tower", player, Vector3(0, 0, 40));
	ServerGameObject* damaged = spawn("Barracks Stone", player, Vector3(0, 0, -45));
	ServerGameObject* dummy = spawn("Training Dummy", player, Vector3(40, 0, 40));
	ServerGameObject* soldier = spawn("Spearman", player, Vector3(20, 0, 20), Tags::GAMEOBJCLASS_CHARACTER);
	ServerGameObject* carrier = spawn("Peasant", player, Vector3(20, 0, -20), Tags::GAMEOBJCLASS_CHARACTER);
	auto step = [&](int frames) {
		for (int i = 0; i < frames; i++) {
			server.tick();
			client.tick();
			SDL_Delay(5);
		}
	};
	step(60);
	// the damaged building: half its integrity; the carrier: wood in its hands
	if (damaged) {
		const int si = server.gameSet->items.names.getIndex("Structural Integrity");
		if (si >= 0)
			damaged->setItem(si, damaged->getItem(si) * 0.5f);
	}
	if (carrier) {
		const int wood = server.gameSet->items.names.getIndex("Wood");
		if (wood >= 0)
			carrier->setItem(wood, 10.0f);
	}
	step(60);
	const GameSet* gs = client.gameSet.get();
	if (!gs) {
		printf("HOVER client has no game set\n");
		return;
	}
	auto cli = [&](ServerGameObject* obj) { return obj ? client.findObject(obj->id) : nullptr; };
	struct Pair { const char* unitLabel; ServerGameObject* unit; const char* targetLabel; ServerGameObject* target; };
	const std::vector<Pair> pairs = {
		{ "peasant", peasant, "own farm", farm },
		{ "peasant", peasant, "own foundation", foundation },
		{ "peasant", peasant, "own manor", manor },
		{ "peasant with wood", carrier, "own manor", manor },
		{ "peasant", peasant, "damaged barracks", damaged },
		{ "peasant", peasant, "watch tower", tower },
		{ "peasant", peasant, "training dummy", dummy },
		{ "peasant", peasant, "enemy manor", enemyManor },
		{ "peasant", peasant, "enemy peasant", enemyPeasant },
		{ "peasant", peasant, "ground", nullptr },
		{ "spearman", soldier, "training dummy", dummy },
		{ "spearman", soldier, "own farm", farm },
		{ "spearman", soldier, "own manor", manor },
		{ "spearman", soldier, "watch tower", tower },
		{ "spearman", soldier, "own foundation", foundation },
		{ "spearman", soldier, "enemy manor", enemyManor },
		{ "spearman", soldier, "enemy peasant", enemyPeasant },
		{ "spearman", soldier, "ground", nullptr },
	};
	auto hover = [&](const Pair& p) -> std::vector<std::pair<WndCursor*, Command*>> {
		ClientGameObject* unit = cli(p.unit);
		ClientGameObject* target = cli(p.target);
		if (!unit || (p.target && !target)) {
			printf("HOVER %s over %s: (missing on the client)\n", p.unitLabel, p.targetLabel);
			return {};
		}
		const auto found = FindCursorCommands(&client, { unit }, target);
		std::string others;
		for (size_t i = 1; i < found.size(); i++)
			others += (i > 1 ? ", " : "") + gs->commands.getString(found[i].second);
		printf("HOVER %s over %s: %s %s%s%s\n", p.unitLabel, p.targetLabel,
			found.empty() ? "-" : gs->commands.getString(found[0].second).c_str(),
			found.empty() ? "(arrow)" : WndCursorPath(found[0].first),
			others.empty() ? "" : "  | also: ", others.c_str());
		if (verbose) {
			CliScriptContext ctx(&client, unit);
			auto _ = ctx.change(ctx.target, target);
			for (Command* cmd : unit->blueprint->offeredCommands) {
				if (!cmd->cursor && cmd->cursorAvailable.empty())
					continue;
				std::string failing;
				for (int eq : cmd->cursorConditions)
					if (!(gs->equations[eq]->eval(&ctx) > 0.0f))
						failing += " [" + gs->equations.getString(eq) + "]";
				bool available = cmd->cursorAvailable.empty();
				for (auto& [cond, cursor] : cmd->cursorAvailable)
					available = available || cond->test->eval(&ctx) > 0.0f;
				printf("    %-40s%s%s\n", gs->commands.getString(cmd).c_str(), failing.empty() && available ? " ok" : failing.c_str(), available ? "" : " [no CURSOR_AVAILABLE]");
			}
		}
		return found;
	};
	for (const Pair& p : pairs)
		hover(p);
	// Live states: a field built by a peasant who then farms it, a second spearman training at the dummy
	const int build = server.gameSet->commands.names.getIndex("Build");
	const int train = server.gameSet->commands.names.getIndex("Train");
	ServerGameObject* field = spawn("Standard Farm Foundation", player, Vector3(-40, 0, 40));
	ServerGameObject* trainee = spawn("Spearman", player, Vector3(30, 0, 30), Tags::GAMEOBJCLASS_CHARACTER);
	SrvGORef fieldRef = field;
	if (field && build >= 0)
		server.gameSet->commands[build].execute(carrier, field, Tags::ORDERASSIGNMODE_FORGET_EVERYTHING_ELSE);
	if (trainee && dummy && train >= 0)
		server.gameSet->commands[train].execute(trainee, dummy, Tags::ORDERASSIGNMODE_FORGET_EVERYTHING_ELSE);
	const int worked = server.gameSet->items.names.getIndex("I'm Being Worked at");
	const uint64_t start = SDL_GetTicks();
	// the field is replaced by the finished farm (a new object): found again by position
	Vector3 fieldAt = field ? field->position : Vector3();
	ServerGameObject* builtFarm = nullptr;
	while (SDL_GetTicks() - start < 90000) {
		step(10);
		if (!builtFarm) {
			auto look = [&](CommonGameObject* obj, auto& self) -> void {
				for (const auto& [type, children] : obj->children)
					for (CommonGameObject* c : children) {
						if (c->blueprint->bpClass == Tags::GAMEOBJCLASS_BUILDING && c->blueprint->name.find("Farm") != std::string::npos
							&& c->blueprint->name.find("Foundation") == std::string::npos && (c->position - fieldAt).sqlen2xz() < 9.0f)
							builtFarm = c->dyncast<ServerGameObject>();
						self(c, self);
					}
			};
			look(player, look);
		}
		if (builtFarm && builtFarm->getItem(worked) > 0 && SDL_GetTicks() - start > 20000)
			break;
		static int lastPrint = -1;
		if ((int)((SDL_GetTicks() - start) / 10000) != lastPrint) {
			lastPrint = (int)((SDL_GetTicks() - start) / 10000);
			std::string order = "-";
			if (Order* o = carrier->orderConfig.getCurrentOrder())
				order = server.gameSet->orders.getString(o->blueprint->bpid);
			printf("HOVER   t=%ds field %s, farm %s, builder's order %s\n", lastPrint * 10, fieldRef ? fieldRef->blueprint->name.c_str() : "(gone)",
				builtFarm ? builtFarm->blueprint->name.c_str() : "-", order.c_str());
		}
	}
	auto workedAt = [&](ServerGameObject* b) {
		ClientGameObject* c = cli(b);
		printf("HOVER   %s: worked at %.0f (server) %.0f (client)\n", b ? b->blueprint->name.c_str() : "-", b ? b->getItem(worked) : -1.0f, c ? c->getItem(worked) : -1.0f);
	};
	workedAt(builtFarm);
	workedAt(dummy);
	hover({ "peasant", peasant, "farm being worked", builtFarm });
	hover({ "spearman", soldier, "training dummy in use", dummy });
	hover({ "trainee", trainee, "training dummy (training there)", dummy });
	// a formation of spearmen (as NETSRVMSG_CREATE_NEW_FORMATION) and a peasant with a spearman
	ServerGameObject* second = spawn("Spearman", player, Vector3(22, 0, 24), Tags::GAMEOBJCLASS_CHARACTER);
	ServerGameObject* formation = server.spawnObject(&server.gameSet->objBlueprints[Tags::GAMEOBJCLASS_FORMATION][0], player, {}, {});
	soldier->setParent(formation);
	second->setParent(formation);
	formation->setPosition((soldier->position + second->position) * 0.5f);
	formation->sendEvent(Tags::PDEVENT_ON_SPAWN);
	step(60);
	for (auto [label, target] : std::vector<std::pair<const char*, ServerGameObject*>>{ { "training dummy", dummy }, { "own farm", farm },
		{ "own manor", manor }, { "watch tower", tower }, { "enemy manor", enemyManor }, { "ground", nullptr } })
		hover({ "formation", formation, label, target });
	for (auto [label, target] : std::vector<std::pair<const char*, ServerGameObject*>>{ { "training dummy", dummy }, { "own farm", farm }, { "enemy manor", enemyManor } }) {
		const auto found = FindCursorCommands(&client, { cli(peasant), cli(trainee) }, cli(target));
		printf("HOVER peasant+spearman over %s: %s %s\n", label, found.empty() ? "-" : gs->commands.getString(found[0].second).c_str(),
			found.empty() ? "(arrow)" : WndCursorPath(found[0].first));
	}
	// the scene saved (the user folder's Save_Games): for a benchmark run that shows the cursors,
	// "benchmark": { "saveGame": "Hover Cursors.sav", "select": ..., "hover": ..., "drawCursor": true }
	const std::string save = PathToUtf8(g_userPath / "saved" / "Save_Games" / "Hover Cursors.sav");
	std::error_code ec;
	std::filesystem::create_directories(g_userPath / "saved" / "Save_Games", ec);
	printf("HOVER saved to %s: %d\n", save.c_str(), (int)server.saveGame(save.c_str()));
	fflush(stdout);
}

// Move arrival (needs the game data): "moveArrival": { "level": "<.lvl>", "sizes": [10, 50, 200],
// "distance": m, "seconds": s, "corpses": n, "unit": "<type>" } (default Tribes Unite, 10/50/200,
// 200 m, 150 s, 100, Pikeman). Groups of that type of the first player stand in a block by its
// first character and are sent with one Move a distance towards the map's middle, loose (each to
// its own spot, as a right-click spreads them: 3 m rings) and as a Line formation, each
//  - on open ground,
//  - with n corpses (every other one the next player's, killed with "Die") strewn over the middle
//    of the way, and a tenth more of the group (of its formation) killed where it stands,
//  - and that after a 20 s fight with half as many of the next player's (the ones left killed).
// Prints per case how many arrived (within 3 m of their spot, or of their formation place with
// the formation there; members the formation's army let go during the fight left out), how far
// the others got and why they stand: the movement controller's state, the order they have, their
// last path search; for a formation its own order and walk. WKB_MOVEARRIVAL_CASE=<n> runs only
// case n (0-17: size, loose/formation, open/corpses/fight); WKB_MOVEARRIVAL_TRACE=1 prints the
// formation's and its army's orders around the order; WKB_MOVEARRIVAL_PROGRESS=1 every 10 s.
// "MOVEARRIVAL case ... arrived a/n" lines; "MOVEARRIVAL all arrived A/N" at the end.
void Test_MoveArrival()
{
	LoadBCP("data.bcp");
	const nlohmann::json cfg = g_settings.value("moveArrival", nlohmann::json::object());
	const std::string level = cfg.value("level", std::string("6 Player Tribes Unite.lvl"));
	const std::vector<int> sizes = cfg.value("sizes", std::vector<int>{ 10, 50, 200 });
	const float distance = cfg.value("distance", 200.0f);
	const int seconds = cfg.value("seconds", 150);
	const int numCorpses = cfg.value("corpses", 100);
	const std::string unitName = cfg.value("unit", std::string("Pikeman"));
	const int onlyCase = getenv("WKB_MOVEARRIVAL_CASE") ? atoi(getenv("WKB_MOVEARRIVAL_CASE")) : -1;
	extern bool g_recordSearches;
	extern std::unordered_map<uint32_t, LastPathSearch> g_lastSearches;
	g_recordSearches = true;
	int allArrived = 0, allUnits = 0, caseIndex = -1;
	for (int units : sizes)
		for (int formed = 0; formed < 2; formed++)
			for (int variant = 0; variant < 3; variant++) {
				// 0 open ground, 1 corpses on the way, 2 that and a fight first
				const bool withCorpses = variant >= 1, battle = variant == 2;
				caseIndex++;
				if (onlyCase >= 0 && onlyCase != caseIndex)
					continue;
				g_lastSearches.clear();
				Server server;
				server.loadSaveGame(("Save_Games\\" + level).c_str());
				ServerGameObject* player = nullptr;
				ServerGameObject* enemy = nullptr;
				ServerGameObject* first = nullptr;
				auto find = [&](CommonGameObject* obj, auto& self) -> void {
					for (const auto& [type, children] : obj->children)
						for (CommonGameObject* child : children) {
							if (!first && child->blueprint->bpClass == Tags::GAMEOBJCLASS_CHARACTER)
								first = child->dyncast<ServerGameObject>();
							self(child, self);
						}
				};
				for (const auto& [type, children] : server.getLevel()->children)
					for (CommonGameObject* p : children)
						if (p->blueprint->bpClass == Tags::GAMEOBJCLASS_PLAYER && p->blueprint->name != "Neutral Player") {
							if (!first) {
								find(p, find);
								if (first)
									player = p->dyncast<ServerGameObject>();
							}
							else if (!enemy)
								enemy = p->dyncast<ServerGameObject>();
						}
				const int moveCommand = server.gameSet->commands.names.getIndex("Move");
				const int dieEvent = server.gameSet->events.names.getIndex("Die");
				if (!player || !first || moveCommand < 0 || dieEvent < 0) {
					printf("MOVEARRIVAL not set up\n");
					return;
				}
				const GameObjBlueprint* bp = server.gameSet->findBlueprint(Tags::GAMEOBJCLASS_CHARACTER, unitName);
				if (!bp)
					bp = first->blueprint;
				const auto [tw, th] = server.terrain->getNumPlayableTiles();
				const Vector3 middle(tw * 2.5f, 0, th * 2.5f);
				const Vector3 dir = (middle - first->position).normal2xz();
				const Vector3 side(-dir.z, 0, dir.x);
				const int columns = (int)std::ceil(std::sqrt((float)units));
				// the block 10 m beyond the first character, towards the middle
				const Vector3 blockCentre = first->position + dir * (10.0f + columns);
				const Vector3 goal = MovementController::nearestValidPosition(bp, blockCentre + dir * distance);
				server.timeManager.unpause();
				server.timeManager.stepMs = 20;
				auto step = [&]() {
					server.timeManager.pendingMs = server.timeManager.stepMs;
					if (server.timeManager.step())
						server.simulate();
				};
				// the corpses first: spawned over the middle of the way, then killed
				uint32_t rnd = 12345u + caseIndex;
				auto random = [&]() { rnd = rnd * 1664525u + 1013904223u; return (float)(rnd >> 8) / 16777216.0f; };
				std::vector<SrvGORef> corpses;
				if (withCorpses) {
					for (int i = 0; i < numCorpses; i++) {
						const Vector3 at = blockCentre + dir * (distance * (0.25f + 0.5f * random())) + side * (40.0f * (random() - 0.5f));
						// every other one the enemy's (the next player's), as on a battlefield
						ServerGameObject* owner = (i & 1) && enemy ? enemy : player;
						corpses.push_back(server.spawnObject(bp, owner, MovementController::nearestValidPosition(bp, at), Vector3(0, 0, 0)));
					}
					for (SrvGORef& c : corpses)
						if (c)
							c->sendEvent(dieEvent, c.get());
				}
				std::vector<SrvGORef> army;
				for (int i = 0; i < units; i++) {
					const Vector3 pos = blockCentre + side * ((i % columns - (columns - 1) * 0.5f) * 2.0f) - dir * ((float)(i / columns) * 2.0f);
					army.push_back(server.spawnObject(bp, player, MovementController::nearestValidPosition(bp, pos), Vector3(0, 0, 0)));
				}
				// with corpses, a tenth more stood with the group and fell (in its formation, if any)
				std::vector<SrvGORef> fallen;
				for (int i = 0; withCorpses && i < std::max(1, units / 10); i++) {
					const Vector3 pos = blockCentre + side * ((i % columns - (columns - 1) * 0.5f) * 2.0f) + dir * (2.0f + (float)(i / columns) * 2.0f);
					fallen.push_back(server.spawnObject(bp, player, MovementController::nearestValidPosition(bp, pos), Vector3(0, 0, 0)));
				}
				SrvGORef formation;
				if (formed) {
					// as NETSRVMSG_CREATE_NEW_FORMATION
					formation = server.spawnObject(&server.gameSet->objBlueprints[Tags::GAMEOBJCLASS_FORMATION][0], player, {}, {});
					Vector3 sum;
					for (SrvGORef& u : army) { sum += u->position; u->setParent(formation.get()); }
					for (SrvGORef& u : fallen) { sum += u->position; u->setParent(formation.get()); }
					formation->setPosition(sum / (float)(units + fallen.size()));
					formation->sendEvent(Tags::PDEVENT_ON_SPAWN);
				}
				// (a second for the formation to take them in, then they fall)
				for (int i = 0; i < 50; i++)
					step();
				for (SrvGORef& u : fallen)
					if (u)
						u->sendEvent(dieEvent, u.get());
				// a fight first: half as many of the enemy's come at the group from the way
				// ahead; after 20 s the ones left fall where they stand
				int enemiesKilled = 0;
				if (battle && enemy) {
					std::vector<SrvGORef> foes;
					const int numFoes = std::max(5, units / 2);
					for (int i = 0; i < numFoes; i++) {
						const Vector3 pos = blockCentre + side * ((i % columns - (columns - 1) * 0.5f) * 2.0f) + dir * (10.0f + (float)(i / columns) * 2.0f);
						foes.push_back(server.spawnObject(bp, enemy, MovementController::nearestValidPosition(bp, pos), Vector3(0, 0, 0)));
					}
					for (int i = 0; i < 20 * 50; i++)
						step();
					for (SrvGORef& f : foes)
						if (f && f->blueprint->name.rfind("Dead", 0) != 0) {
							f->sendEvent(dieEvent, f.get());
							enemiesKilled++;
						}
				}
				// 5 s more: the corpses fall, the formation forms up
				for (int i = 0; i < 250; i++)
					step();
				// (the dying are converted into their corpses, "Dead ...": counted by name on the way)
				int corpsesLeft = 0;
				{
					NNSearch nns;
					nns.start(&server, blockCentre + dir * (distance * 0.5f), distance * 0.5f + 40.0f);
					std::set<const ServerGameObject*> ownersShown;
					while (CommonGameObject* o = nns.next()) {
						if (o->blueprint->name.rfind("Dead", 0) != 0)
							continue;
						corpsesLeft++;
						// what a right-click on the corpse would give a unit of the army (the
						// client's choice: the first offered command whose cursor shows)
						ServerGameObject* corpse = o->dyncast<ServerGameObject>();
						if (!ownersShown.insert(corpse->getPlayer()).second || army.empty() || !army[0])
							continue;
						SrvScriptContext ctx(&server, army[0].get());
						auto _ = ctx.change(ctx.target, corpse);
						std::string pick = "(none)";
						for (Command* cmd : bp->offeredCommands) {
							bool shows = cmd->cursor != nullptr;
							for (auto& [cond, cursor] : cmd->cursorAvailable)
								if (cond->test->eval(&ctx)) { shows = cursor != nullptr; break; }
							bool all = true;
							for (int eq : cmd->cursorConditions)
								all = all && server.gameSet->equations[eq]->eval(&ctx) > 0.0f;
							if (shows && all) { pick = server.gameSet->commands.getString(cmd); break; }
						}
						printf("MOVEARRIVAL   corpse %u %s of player %u: flags selectable %d targetable %d, interactable %d; a right-click on it: %s\n", corpse->id,
							corpse->blueprint->name.c_str(), corpse->getPlayer() ? corpse->getPlayer()->id : 0, (corpse->flags & CommonGameObject::fSelectable) ? 1 : 0,
							(corpse->flags & CommonGameObject::fTargetable) ? 1 : 0, (int)corpse->isInteractable(), pick.c_str());
					}
				}
				// (in a fight the formation's army lets members too far from its centre go to the
				// player: they aren't in the formation the order is for, and are left out)
				int alive = 0, released = 0;
				std::vector<bool> leftOut(units, false);
				for (int i = 0; i < units; i++) {
					SrvGORef& u = army[i];
					if (!u || u->blueprint->name.rfind("Dead", 0) == 0)
						continue;
					if (formed && formation && u->getParent() != formation.get()) {
						leftOut[i] = true;
						released++;
						continue;
					}
					alive++;
				}
				// the order, as a right-click (ClientInterface::sendRightClickCommand)
				std::vector<Vector3> spots(units, goal);
				std::vector<Vector3> starts(units);
				std::vector<int> orderIds(units, -1);
				for (int i = 0; i < units; i++)
					if (army[i])
						starts[i] = army[i]->position;
				const float orderTime = server.timeManager.currentTime;
				auto formationOrders = [&](const char* when) {
					if (!getenv("WKB_MOVEARRIVAL_TRACE") || !formation)
						return;
					printf("MOVEARRIVAL   formation %u %s (controller %s %u, can be ordered %d):", formation->id, when,
						formation->getParent() ? formation->getParent()->blueprint->name.c_str() : "-", formation->getParent() ? formation->getParent()->id : 0,
						(int)server.gameSet->commands[moveCommand].canBeExecuted(formation.get(), nullptr));
					for (Order& o : formation->orderConfig.orders)
						printf(" [%s state %d]", server.gameSet->orders.getString(o.blueprint->bpid).c_str(), o.state);
					if (ServerGameObject* army = formation->getParent(); army && army->blueprint->bpClass == Tags::GAMEOBJCLASS_ARMY) {
						printf("; its army's:");
						for (Order& o : army->orderConfig.orders) {
							Task* t = o.getCurrentTask();
							printf(" [%s state %d, task %s]", server.gameSet->orders.getString(o.blueprint->bpid).c_str(), o.state,
								t ? server.gameSet->tasks.getString(t->blueprint->bpid).c_str() : "-");
						}
					}
					printf("\n");
				};
				if (formed) {
					formationOrders("before the order");
					server.gameSet->commands[moveCommand].execute(formation.get(), nullptr, Tags::ORDERASSIGNMODE_FORGET_EVERYTHING_ELSE, goal);
					formationOrders("after the order");
					step();
					formationOrders("a step later");
					for (int i = 0; i < 49; i++)
						step();
					formationOrders("a second later");
				}
				else {
					Vector3 destPos = goal;
					float nextPosDir = 0.0f, nextPosRadius = 3.0f;
					for (int i = 0; i < units; i++) {
						if (!army[i] || army[i]->blueprint->name.rfind("Dead", 0) == 0)
							continue;
						spots[i] = destPos;
						server.gameSet->commands[moveCommand].execute(army[i].get(), nullptr, Tags::ORDERASSIGNMODE_FORGET_EVERYTHING_ELSE, destPos);
						if (Order* o = army[i]->orderConfig.getCurrentOrder())
							orderIds[i] = o->id;
						destPos = goal + Vector3(std::cos(nextPosDir), 0.0f, std::sin(nextPosDir)) * nextPosRadius;
						nextPosDir += 3.0f / nextPosRadius;
						if (nextPosDir >= 2.0f * 3.1415f) { nextPosDir = 0.0f; nextPosRadius += 3.0f; }
					}
				}
				auto distToSpot = [&](int i) -> float {
					ServerGameObject* u = army[i].get();
					Vector3 spot = spots[i];
					if (formed && formation)
						FormationController::memberPlace(formation.get(), u, spot);
					return (u->position - spot).len2xz();
				};
				auto isDead = [&](int i) { return !army[i] || army[i]->blueprint->name.rfind("Dead", 0) == 0 || leftOut[i]; };
				auto isArrived = [&](int i) {
					if (isDead(i))
						return false;
					if (formed && (!formation || (formation->position - goal).len2xz() > 3.0f))
						return false;
					if (distToSpot(i) < 3.0f)
						return true;
					// or its Move ended at the free spot nearest to it (WKB.exe's stop discs: PREFERRED_SPACING
					// round each unit; the spots are 3 m apart, pikemen keep 2 m each)
					ServerGameObject* u = army[i].get();
					if (formed || u->movementController.isMoving() || u->orderConfig.getCurrentOrder())
						return false;
					return distToSpot(i) < 3.0f + 4.0f * u->blueprint->preferredSpacing;
				};
				int arrived = 0, arrivalSecond = -1;
				for (int s = 0; s < seconds; s++) {
					for (int k = 0; k < 50; k++)
						step();
					arrived = 0;
					int moving = 0;
					for (int i = 0; i < units; i++) {
						arrived += isArrived(i);
						moving += army[i] && army[i]->movement.isMoving();
					}
					if (s % 10 == 9 && getenv("WKB_MOVEARRIVAL_PROGRESS"))
						printf("MOVEARRIVAL   t=%d s: %d arrived, %d walking\n", s + 1, arrived, moving);
					if (arrived == alive) { arrivalSecond = s + 1; break; }
				}
				// why the others stand
				std::map<std::string, int> reasons;
				float progressSum = 0.0f;
				int listed = 0;
				for (int i = 0; i < units; i++) {
					ServerGameObject* u = army[i].get();
					if (isDead(i)) continue;
					if (isArrived(i)) continue;
					const float total = (spots[i] - starts[i]).len2xz();
					const float left = distToSpot(i);
					progressSum += total > 0 ? 1.0f - left / total : 1.0f;
					const MovementController& mc = u->movementController;
					Order* o = u->orderConfig.getCurrentOrder();
					const std::string orderName = o ? server.gameSet->orders.getString(o->blueprint->bpid) : std::string("(none)");
					Task* t = o ? o->getCurrentTask() : nullptr;
					std::string why = mc.m_pending ? "pending" : mc.m_started ? (u->movement.isMoving() ? "walking" : "started, no walk") : "stands";
					why += formed ? std::string(", formation ") + (formation && formation->movement.isMoving() ? "moving" : "standing") : ", order " + orderName + (o && o->id != orderIds[i] ? " (another)" : "");
					reasons[why]++;
					if (listed++ < 12) {
						const auto it = g_lastSearches.find(u->id);
						printf("MOVEARRIVAL   unit %u at (%.0f,%.0f): %.0f of %.0f m to go; %s; nodes %zu", u->id, u->position.x, u->position.z, left, total, why.c_str(), mc.m_pathNodes.size());
						if (t)
							printf(", task next try %.1f (now %.1f)", t->nextMoveAttempt, server.timeManager.currentTime);
						if (it != g_lastSearches.end()) {
							const LastPathSearch& ls = it->second;
							printf("; %d searches, last %.1f s after the order: %s%s%s, %d tiles, %d steps, to (%.0f,%.0f) -> %s", ls.count, ls.time - orderTime,
								ls.straight ? "straight" : ls.fromField ? "field" : "A*", ls.cut ? " cut" : "", ls.unreachable ? " unreachable" : "",
								ls.tiles, ls.steps, ls.destination.x, ls.destination.z, ls.moving ? "moving" : "not moving");
						}
						else
							printf("; no search");
						printf("\n");
					}
				}
				printf("MOVEARRIVAL case %d: %d %s %s, %d corpses (%d left at the order)%s, %.0f m from (%.0f,%.0f) to (%.0f,%.0f): arrived %d/%d", caseIndex, units, bp->name.c_str(),
					formed ? "in formation" : "loose", withCorpses ? numCorpses : 0, corpsesLeft,
					battle ? (", after a fight (" + std::to_string(units - alive - released) + " fell, " + std::to_string(released) + " let go by the formation's army, "
						+ std::to_string(enemiesKilled) + " enemies left killed)").c_str() : "",
					distance, blockCentre.x, blockCentre.z, goal.x, goal.z, arrived, alive);
				if (arrivalSecond > 0)
					printf(" in %d s", arrivalSecond);
				if (arrived < alive) {
					printf("; the others %.0f%% of the way on average:", 100.0f * progressSum / std::max(1, alive - arrived));
					for (const auto& [why, n] : reasons)
						printf(" [%s: %d]", why.c_str(), n);
				}
				if (formed && formation) {
					int corpseMembers = 0;
					for (const auto& [type, members] : formation->children)
						for (CommonGameObject* m : members)
							corpseMembers += m->blueprint->name.rfind("Dead", 0) == 0;
					printf("; formation %.0f m from the goal (speed %.2f, %d corpses among its members)", (formation->position - goal).len2xz(), formation->computeSpeed(), corpseMembers);
					// the formation's own order and walk
					const MovementController& fmc = formation->movementController;
					Order* fo = formation->orderConfig.getCurrentOrder();
					Task* ft = fo ? fo->getCurrentTask() : nullptr;
					printf("; its order %s (%zu orders), task %s, %s, controller %s %u", fo ? server.gameSet->orders.getString(fo->blueprint->bpid).c_str() : "(none)",
						formation->orderConfig.orders.size(), ft ? server.gameSet->tasks.getString(ft->blueprint->bpid).c_str() : "-",
						fmc.m_pending ? "pending" : fmc.m_started ? (formation->movement.isMoving() ? "walking" : "started, no walk") : "stands",
						formation->getParent() ? formation->getParent()->blueprint->name.c_str() : "-", formation->getParent() ? formation->getParent()->id : 0);
					const auto it = g_lastSearches.find(formation->id);
					if (it != g_lastSearches.end())
						printf("; %d searches, last %.1f s after the order: %s%s, %d tiles to (%.0f,%.0f) -> %s", it->second.count, it->second.time - orderTime,
							it->second.straight ? "straight" : it->second.fromField ? "field" : "A*", it->second.cut ? " cut" : "", it->second.tiles,
							it->second.destination.x, it->second.destination.z, it->second.moving ? "moving" : "not moving");
					else
						printf("; no search");
				}
				printf("\n");
				fflush(stdout);
				allArrived += arrived;
				allUnits += alive;
			}
	g_recordSearches = false;
	extern uint64_t g_formationWalks, g_pathBatchRequests, g_pathBatchSearches;
	printf("MOVEARRIVAL all arrived %d/%d; formation members' walks %llu, path requests %llu, searches %llu\n", allArrived, allUnits,
		(unsigned long long)g_formationWalks, (unsigned long long)g_pathBatchRequests, (unsigned long long)g_pathBatchSearches);
	fflush(stdout);
}

// A city's area, as WKB.exe's (needs the game data): a Manor stamped down on 4 Peaks for player
// 1027, then barracks one after another, each on the spot furthest from the Manor that still joins the city, so
// that the city's walls reach far from the Manor. A local client takes the game over a
// NetLocalLink. Then civic buildings are placed (looked at only) on a grid around the far end and
// beyond: the client's preview must agree with the server on every spot (wkbre2 kept the grown
// rectangles on the server only, so the client refused them as not "built near a Manor"), some
// spots far from the Manor must be accepted, and the spots well outside the city refused as not
// connected. Prints "CITYAREA" lines and "CITYAREA result: <ok|FAIL> ...".
void Test_CityArea()
{
	LoadBCP("data.bcp");
	Server server;
	Client client(true);
	NetLocalBuffer toClient, toServer;
	NetLocalLink srv2cli(&toClient, &toServer), cli2srv(&toServer, &toClient);
	server.addClient(&srv2cli);
	client.serverLink = &cli2srv;
	server.loadSaveGame("Save_Games\\2 Player 4 Peaks.lvl");
	auto pump = [&]() {
		for (int i = 0; i < 1000 && cli2srv.available(); i++)
			client.tick();
	};
	pump();
	ServerGameObject* player = server.findObject(1027);
	ClientGameObject* cliPlayer = client.findObject(1027);
	const GameObjBlueprint* manor = server.gameSet->findBlueprint(Tags::GAMEOBJCLASS_BUILDING, "Manor Foundation");
	const GameObjBlueprint* barracks = server.gameSet->findBlueprint(Tags::GAMEOBJCLASS_BUILDING, "Barracks Stone Foundation");
	if (!player || !cliPlayer || !manor || !barracks || !server.terrain || !server.tiles || !client.gameSet || !client.terrain) {
		printf("CITYAREA not set up (player %d, client player %d)\n", player != nullptr, cliPlayer != nullptr);
		return;
	}
	const GameObjBlueprint* cliBarracks = client.gameSet->findBlueprint(Tags::GAMEOBJCLASS_BUILDING, "Barracks Stone Foundation");
	player->setItem(Tags::PDITEM_FOOD, 1e6f);
	player->setItem(Tags::PDITEM_WOOD, 1e6f);
	player->setItem(Tags::PDITEM_GOLD, 1e6f);
	auto tileCentre = [](const GameObjBlueprint* bp, int tx, int tz) {
		return Vector3(tx * 5.0f + 2.5f + bp->footprint->originX, 0.0f, tz * 5.0f + 2.5f + bp->footprint->originZ);
	};
	ServerGameObject* first = nullptr;
	int mx = 0, mz = 0;
	for (int tz = 50; tz <= 80 && !first; tz++)
		for (int tx = 50; tx <= 80 && !first; tx++)
			if ((first = server.stampdownObject(manor, player, tileCentre(manor, tx, tz), Vector3(0, 0, 0), true, true)))
				mx = tx, mz = tz;
	if (!first || !first->parent || first->parent->blueprint->bpClass != Tags::GAMEOBJCLASS_CITY) {
		printf("CITYAREA no Manor stamped down\n");
		return;
	}
	ServerGameObject* city = (ServerGameObject*)first->parent;
	// barracks one after another, each on the spot that still joins the city and lies furthest
	// from the Manor (in tiles, the larger of the two distances), up to 16 tiles from the last
	using Status = StampdownPlan::Status;
	auto reach = [&](int tx, int tz) { return std::max(std::abs(tx - mx), std::abs(tz - mz)); };
	int lastX = mx, lastZ = mz, grown = 0;
	for (int step = 0; step < 6; step++) {
		int bx = 0, bz = 0, best = reach(lastX, lastZ);
		for (int tz = lastZ - 16; tz <= lastZ + 16; tz++)
			for (int tx = lastX - 16; tx <= lastX + 16; tx++)
				if (reach(tx, tz) > best && StampdownPlan::getStampdownPlan(&server, player, barracks, tileCentre(barracks, tx, tz), Vector3(0, 0, 0)).status == Status::Valid)
					best = reach(tx, tz), bx = tx, bz = tz;
		if (best == reach(lastX, lastZ))
			break;
		ServerGameObject* b = server.stampdownObject(barracks, player, tileCentre(barracks, bx, bz), Vector3(0, 0, 0), true, true);
		if (!b)
			break;
		lastX = bx, lastZ = bz;
		grown++;
		if (b->parent != city)
			printf("CITYAREA barracks %d at (%.1f, %.1f) started a city of its own\n", step, b->position.x, b->position.z);
	}
	pump();
	const ClientGameObject* cliCity = client.findObject(city->id);
	int x0 = 1 << 30, z0 = 1 << 30, x1 = -1, z1 = -1;
	for (const auto& r : city->cityRectangles) {
		x0 = std::min(x0, r.xStart); z0 = std::min(z0, r.yStart);
		x1 = std::max(x1, r.xEnd); z1 = std::max(z1, r.yEnd);
	}
	printf("CITYAREA Manor at tile (%d, %d), %d barracks, the last at tile (%d, %d), %d tiles from it; the city %d rectangles on the server, %d on the client, tiles x %d..%d z %d..%d\n",
		mx, mz, grown, lastX, lastZ, reach(lastX, lastZ), (int)city->cityRectangles.size(), cliCity ? (int)cliCity->cityRectangles.size() : -1, x0, x1, z0, z1);

	// a barracks placed on a grid around the city's far end and beyond
	auto gap = [&](const CommonGameObject::CityRectangle& r) {
		int g = 1 << 30;
		for (const auto& c : city->cityRectangles)
			g = std::min(g, std::max({ c.xStart - r.xEnd, r.xStart - c.xEnd, c.yStart - r.yEnd, r.yStart - c.yEnd }));
		return g;
	};
	int compared = 0, differ = 0, farAccepted = 0, outsideRefused = 0, outsideTried = 0, outsideAccepted = 0;
	for (int tz = lastZ - 24; tz <= lastZ + 24; tz++)
		for (int tx = lastX - 24; tx <= lastX + 24; tx++) {
			const Vector3 at = tileCentre(barracks, tx, tz);
			const auto sp = StampdownPlan::getStampdownPlan(&server, player, barracks, at, Vector3(0, 0, 0));
			const auto cp = StampdownPlan::getStampdownPlan(&client, cliPlayer, cliBarracks, at, Vector3(0, 0, 0));
			compared++;
			if (sp.status != cp.status) {
				if (differ < 8)
					printf("CITYAREA at tile (%d, %d) the server says %d, the client %d\n", tx, tz, (int)sp.status, (int)cp.status);
				differ++;
			}
			// far from the Manor, along or in the city's far end
			if (sp.status == Status::Valid && reach(tx, tz) >= 20 && gap(sp.cityRectangle) <= 0)
				farAccepted++;
			// well clear of the city (its rectangle 6 tiles or more from the city's)
			if (gap(sp.cityRectangle) >= 6 && (sp.status == Status::Valid || sp.status == Status::InvalidNotConnected)) {
				outsideTried++;
				outsideRefused += sp.status == Status::InvalidNotConnected;
				outsideAccepted += sp.status == Status::Valid;
			}
		}
	printf("CITYAREA %d spots: %d differ between server and client; %d accepted 20 tiles or more from the Manor at the city's end; "
		"well outside the city %d of %d refused as not connected, %d accepted\n",
		compared, differ, farAccepted, outsideRefused, outsideTried, outsideAccepted);
	const bool ok = grown >= 3 && differ == 0 && farAccepted > 0 && outsideTried > 0 && outsideAccepted == 0;
	printf("CITYAREA result: %s\n", ok ? "ok" : "FAIL");

	// for a look at it in game: the furthest accepted spot that builds into the city's walls,
	// and the city with its foundations swapped for the finished buildings, saved as
	// saved\Save_Games\CityArea.sav
	Vector3 shown(-1, -1, -1);
	int shownReach = -1;
	for (int tz = lastZ - 24; tz <= lastZ + 24; tz++)
		for (int tx = lastX - 24; tx <= lastX + 24; tx++) {
			const Vector3 at = tileCentre(barracks, tx, tz);
			const auto sp = StampdownPlan::getStampdownPlan(&server, player, barracks, at, Vector3(0, 0, 0));
			if (sp.status == Status::Valid && !sp.toRemove.empty() && reach(tx, tz) > shownReach)
				shownReach = reach(tx, tz), shown = at;
		}
	std::vector<ServerGameObject*> foundations;
	for (const auto& [bpid, objlist] : city->children)
		for (CommonGameObject* c : objlist)
			if (c->blueprint->bpClass == Tags::GAMEOBJCLASS_BUILDING && c->blueprint->name.ends_with(" Foundation"))
				foundations.push_back((ServerGameObject*)c);
	for (ServerGameObject* o : foundations) {
		const std::string built = o->blueprint->name.substr(0, o->blueprint->name.size() - 11);
		if (const GameObjBlueprint* bp = server.gameSet->findBlueprint(Tags::GAMEOBJCLASS_BUILDING, built)) {
			const Vector3 at = o->position, turn = o->orientation;
			server.destroyObject(o);
			server.spawnObject(bp, city, at, turn);
		}
	}
	const std::string path = PathToUtf8(g_userPath / "saved" / "Save_Games" / "CityArea.sav");
	printf("CITYAREA saved %d to %s; a barracks into the walls accepted at (%.1f, %.1f), ground %.2f, %d tiles from the Manor\n",
		(int)server.saveGame(path.c_str()), path.c_str(), shown.x, shown.z, server.terrain->getHeight(shown.x, shown.z), shownReach);
	fflush(stdout);
}

// Build requirements: every build command a peasant offers at the start of a skirmish, as the
// build menu shows it (hidden by ICON_CONDITION, IMPOSSIBLE, WAIT, ENABLED) and whether the server
// takes its stampdown; then with a Barracks built, before and after the Fort's research ("Can Build
// Fort Wood", as Research Wooden Forts.cpp sets it): its command must be IMPOSSIBLE and refused
// before, and not after. And the Gatehouse Battlements before and after the walls' research.
bool StampdownAllowedByCommand(Server* server, const GameObjBlueprint* blueprint, ServerGameObject* player);
void Test_BuildRequirements()
{
	LoadBCP("data.bcp");
	const nlohmann::json cfg = g_settings.value("buildRequirements", nlohmann::json::object());
	const std::string level = cfg.value("level", std::string("6 Player Tribes Unite.lvl"));
	Server server;
	server.loadSaveGame(("Save_Games\\" + level).c_str());
	const GameSet& gs = *server.gameSet;
	ServerGameObject* player = nullptr;
	ServerGameObject* peasant = nullptr;
	auto find = [&](CommonGameObject* obj, auto& self) -> void {
		for (const auto& [type, children] : obj->children)
			for (CommonGameObject* child : children) {
				if (!peasant && child->blueprint->name == "Peasant")
					peasant = child->dyncast<ServerGameObject>();
				self(child, self);
			}
	};
	for (const auto& [type, children] : server.getLevel()->children)
		for (CommonGameObject* p : children)
			if (!peasant && p->blueprint->bpClass == Tags::GAMEOBJCLASS_PLAYER && p->blueprint->name != "Neutral Player") {
				find(p, find);
				if (peasant)
					player = p->dyncast<ServerGameObject>();
			}
	if (!peasant) {
		printf("BUILDREQ no peasant\n");
		return;
	}
	auto state = [&](const Command* cmd) -> std::string {
		SrvScriptContext ctx(&server, peasant);
		auto _ = ctx.selectedObject.change(peasant);
		auto cond = [&](GSCondition* c) { return c->test->booleval(&ctx); };
		for (int eq : cmd->iconConditions)
			if (!gs.equations[eq]->booleval(&ctx))
				return "hidden";
		if (!std::all_of(cmd->conditionsImpossible.begin(), cmd->conditionsImpossible.end(), cond))
			return "IMPOSSIBLE";
		if (!std::all_of(cmd->conditionsWait.begin(), cmd->conditionsWait.end(), cond))
			return "WAIT";
		return "ENABLED";
	};
	printf("BUILDREQ %s, player %u, peasant %u\n", level.c_str(), player->id, peasant->id);
	for (const Command* cmd : peasant->blueprint->offeredCommands)
		if (cmd->stampdownObject)
			printf("BUILDREQ %-40s %-10s server %s\n", gs.commands.names.getString(cmd->id).c_str(), state(cmd).c_str(),
				StampdownAllowedByCommand(&server, cmd->stampdownObject, player) ? "takes it" : "refuses it");
	const int fortCmd = gs.commands.names.getIndex("Build Fort Wood");
	const int fortItem = gs.items.names.getIndex("Can Build Fort Wood");
	if (fortCmd < 0 || fortItem < 0) {
		printf("BUILDREQ no Fort\n");
		return;
	}
	const Command* fort = &gs.commands[fortCmd];
	// the Fort's condition also tests "Can Build Watch Tower", which a finished Barracks gives
	// (multi-player extensions.cpp: Barracks Stone Foundation's "Allow Watch Tower"): build one first
	if (const int towerItem = gs.items.names.getIndex("Can Build Watch Tower"); towerItem >= 0)
		player->setItem(towerItem, player->getItem(towerItem) + 1.0f);
	const std::string before = state(fort);
	const bool takenBefore = StampdownAllowedByCommand(&server, fort->stampdownObject, player);
	player->setItem(fortItem, player->getItem(fortItem) + 1.0f); // the research's INCREASE_ITEM
	const std::string after = state(fort);
	const bool takenAfter = StampdownAllowedByCommand(&server, fort->stampdownObject, player);
	bool ok = before == "IMPOSSIBLE" && !takenBefore && (after == "ENABLED" || after == "WAIT") && takenAfter;
	printf("BUILDREQ Fort before the research: %s, server %s; after: %s, server %s -> %s\n", before.c_str(), takenBefore ? "takes it" : "refuses it",
		after.c_str(), takenAfter ? "takes it" : "refuses it", ok ? "PASS" : "FAIL");
	// the Gatehouse Battlements: none at the start (its STARTS_WITH_ITEM is in a /* */ block of
	// multi-player extensions.cpp), offered after "Unaligned Wall to Technological Wall" (item 2)
	const int gateCmd = gs.commands.names.getIndex("Build Technological Gatehouse Closed");
	const int gateItem = gs.items.names.getIndex("Can Build Technological Gatehouse");
	if (gateCmd >= 0 && gateItem >= 0) {
		const Command* gate = &gs.commands[gateCmd];
		const std::string gBefore = state(gate);
		const bool gTakenBefore = StampdownAllowedByCommand(&server, gate->stampdownObject, player);
		player->setItem(gateItem, 2.0f);
		const std::string gAfter = state(gate);
		const bool gOk = gBefore == "hidden" && !gTakenBefore && gAfter != "hidden" && gAfter != "IMPOSSIBLE";
		printf("BUILDREQ Gatehouse Battlements before the research: %s, server %s; after: %s -> %s\n", gBefore.c_str(),
			gTakenBefore ? "takes it" : "refuses it", gAfter.c_str(), gOk ? "PASS" : "FAIL");
		ok = ok && gOk;
	}
	printf("BUILDREQ %s\n", ok ? "PASS" : "FAIL");
	fflush(stdout);
}

// Moon phases (wkbai's day and night, no game data): each fixed phase's lit part, the side lit
// (towards the sun: west when waxing, east when waning), the moon's place (its elongation from
// the sun), its light and shadows by the lit part (none at new moon), the natural month over
// the game's days, and the setting through the save game's / recording's text
void Test_MoonPhases()
{
	int failures = 0;
	auto check = [&](bool ok, const char* what) {
		if (!ok) failures++;
		printf("MOONTEST %s: %s\n", ok ? "ok" : "FAILED", what);
	};
	const float expectLit[8] = { 0.0f, 0.1464f, 0.5f, 0.8536f, 1.0f, 0.8536f, 0.5f, 0.1464f };
	auto lum = [](const SunState& s) { return s.colour[0] * 0.3f + s.colour[1] * 0.59f + s.colour[2] * 0.11f; };
	float fullBest = 0.0f;
	float best[9] = {};
	for (int p = 1; p <= 8; p++) {
		EnvironmentSettings es;
		es.dayNight = true;
		es.moonPhase = p;
		const float age = es.moonAge(1234.0f);
		const float lit = MoonLitFraction(age);
		char what[160];
		snprintf(what, sizeof(what), "%s: age %.3f lit %.3f (expected %.3f)", MoonPhaseName(p), age, lit, expectLit[p - 1]);
		check(std::fabs(age - (p - 1) / 8.0f) < 1e-5f && std::fabs(lit - expectLit[p - 1]) < 0.002f, what);
		// over the night (the sun 5 degrees or more down): the moon's best light, its height then,
		// its lit side (the sun's direction across the disc: east component)
		float bestLight = 0.0f, bestUp = -1.0f, sideEast = 0.0f, bestElong = 0.0f;
		for (int m = 0; m < 24 * 60; m += 5) {
			const SunState s = ComputeSun(m / (24.0f * 60.0f), 0.125f, false, 0.0f, age);
			if (s.sunTrue.y > -0.09f)
				continue;
			if (lum(s) > bestLight || bestUp < -0.5f) {
				bestLight = lum(s);
				bestUp = s.moonTrue.y;
				// the sky's east at the moon: where it was a minute before (the sky turns westwards)
				const Vector3 across = s.sunTrue - s.moonTrue * s.sunTrue.dot(s.moonTrue);
				const SunState before = ComputeSun((m - 1) / (24.0f * 60.0f), 0.125f, false, 0.0f, age);
				sideEast = across.dot(before.moonTrue - s.moonTrue);
				bestElong = std::acos(std::clamp(s.sunTrue.dot(s.moonTrue), -1.0f, 1.0f));
			}
		}
		best[p] = bestLight;
		if (p == 5)
			fullBest = bestLight;
		printf("MOONTEST %-16s best light %.4f (moon %.2f up), lit side %s, %.0f deg from the sun\n", MoonPhaseName(p), bestLight, bestUp,
			p == 1 || p == 5 ? "-" : sideEast < 0 ? "west" : "east", bestElong * 180.0f / 3.14159265f);
		if (p > 1 && p < 5) check(sideEast < 0.0f, "waxing: the side towards the sun is the west");
		if (p > 5) check(sideEast > 0.0f, "waning: the side towards the sun is the east");
		snprintf(what, sizeof(what), "%s: the moon %.0f degrees from the sun", MoonPhaseName(p), bestElong * 180.0f / 3.14159265f);
		const float expectElong = (p - 1) / 8.0f * 360.0f, e = expectElong > 180.0f ? 360.0f - expectElong : expectElong;
		check(std::fabs(bestElong * 180.0f / 3.14159265f - e) < 25.0f, what);
	}
	check(best[1] < 1e-4f, "new moon: no direct light (no shadows) all night");
	for (int p = 2; p <= 8; p++) {
		char what[160];
		snprintf(what, sizeof(what), "%s: its best light %.3f of the full moon's, at most its lit part %.3f", MoonPhaseName(p), best[p] / fullBest, expectLit[p - 1]);
		check(best[p] <= fullBest * expectLit[p - 1] + 0.002f && best[p] > 0.0f, what);
	}
	// the full moon's light as before the phases (opposite the sun, the old default)
	{
		const SunState s = ComputeSun(0.0f, 0.125f, false, 0.0f);
		check(s.moonLight > 0.99f && s.moonTrue.y > 0.5f, "the default (no phase given): a full moon high at midnight");
	}
	// the scene's sky light at midnight by the phase: a moonless night about half a full moon's,
	// the noon's untouched
	{
		const SunState full = ComputeSun(0.0f, 0.125f, false, 0.0f, 0.5f), none = ComputeSun(0.0f, 0.125f, false, 0.0f, 0.0f);
		const SunState noon = ComputeSun(0.5f, 0.125f, false, 0.0f, 0.0f);
		char what[200];
		snprintf(what, sizeof(what), "sky light at midnight: full moon %.2f, new moon %.2f, noon %.2f; ambient %.2f / %.2f",
			full.skyLightScale, none.skyLightScale, noon.skyLightScale, full.ambient[1], none.ambient[1]);
		check(full.skyLightScale > 0.88f && full.skyLightScale < 0.96f && none.skyLightScale < 0.4f && std::fabs(noon.skyLightScale - 1.0f) < 0.01f
			&& none.ambient[1] < 0.6f * full.ambient[1], what);
	}
	// the natural month: on by 1/29.53 a game day, from moonStart
	{
		EnvironmentSettings es;
		es.dayNight = true;
		es.dayLengthMin = 20;
		es.startHour = 0;
		es.apply("moonDay", 7.0f);
		const float a0 = es.moonAge(0.0f), a1 = es.moonAge(es.dayLengthS()), a30 = es.moonAge(es.dayLengthS() * 29.530589f);
		char what[160];
		snprintf(what, sizeof(what), "natural: day 0 %.4f, day 1 %.4f, a month later %.4f", a0, a1, a30);
		check(std::fabs(a0 - 7.0f / 29.530589f) < 1e-4f && std::fabs(a1 - a0 - 1.0f / 29.530589f) < 1e-4f && std::fabs(a30 - a0) < 1e-3f, what);
		EnvironmentSettings unset;
		unset.dayNight = true;
		unset.startSeason = 3;
		unset.startHour = 0;
		check(unset.text().find("moon") == std::string::npos && std::fabs(unset.moonAge(0.0f) - HistoricMoonAge(3)) < 1e-5f,
			"natural, not chosen (a game from before): no moon lines, the year 1000's for the season");
	}
	// the save game's and the recording's text
	for (int p = 0; p <= 8; p++) {
		EnvironmentSettings es, back;
		es.dayNight = back.dayNight = true;
		es.moonPhase = p;
		es.moonStart = 0.123456f;
		const std::string text = es.text();
		size_t start = 0;
		while (start < text.size()) {
			size_t end = text.find('\n', start);
			if (end == std::string::npos) end = text.size();
			const std::string line = text.substr(start, end - start);
			start = end + 1;
			const size_t eq = line.find('=');
			if (eq != std::string::npos)
				back.apply(line.substr(0, eq), (float)atof(line.c_str() + eq + 1));
		}
		bool same = back.moonPhase == es.moonPhase && back.moonStart == es.moonStart;
		for (float t : { 0.0f, 600.0f, 98765.0f })
			same = same && back.moonAge(t) == es.moonAge(t);
		char what[160];
		snprintf(what, sizeof(what), "%s through the saved text", MoonPhaseName(p));
		check(same, what);
	}
	// the year 1000's moon on the first day of each season (the mean month: spring 6.6 days
	// old, summer 9.3, autumn 12.1, winter 14.8, within the day the midnight takes)
	const float expectDays[4] = { 6.6f, 9.3f, 12.1f, 14.8f };
	for (int season = 0; season < 4; season++) {
		const float a = HistoricMoonAge(season);
		printf("MOONTEST the year 1000, first day of %s: age %.3f (%.1f days), lit %.2f\n", SeasonName(season), a,
			a * 29.530589f, MoonLitFraction(a));
		char what[160];
		snprintf(what, sizeof(what), "the year 1000's %s moon %.1f days old", SeasonName(season), a * 29.530589f);
		check(a * 29.530589f > expectDays[season] - 1.0f && a * 29.530589f <= expectDays[season] + 0.05f, what);
	}
	printf("MOONTEST %s (%d failed)\n", failures ? "FAILED" : "PASSED", failures);
}

// The environment debug's clock jumps (ServerEnvironment::debugSet, F9 and "environmentSet")
// on a game whose environment has not started: an original's save without day and night, the
// game time far from 0, switched on in the same batch as the jump. The hour and the season asked
// for must be what the clock shows after the game's first update puts the clock at its start
// (no game data: the first update's startClock called as it would be)
void Test_EnvironmentClock()
{
	int failures = 0;
	auto check = [&](bool ok, const char* what) {
		if (!ok) failures++;
		printf("ENVCLOCK %s: %s\n", ok ? "ok" : "FAILED", what);
	};
	struct Case { float time; std::vector<std::pair<const char*, float>> set; float hour, season; };
	const Case cases[] = {
		{ 54321.0f, { { "dayNight", 1 }, { "seasons", 1 }, { "season", 1.5f }, { "hour", 12 } }, 12.0f, 1.5f },
		{ 54321.0f, { { "dayNight", 1 }, { "seasons", 1 }, { "season", 3.5f }, { "hour", 0.5f } }, 0.5f, 3.5f },
		{ 7777.0f, { { "dayNight", 1 }, { "seasons", 1 }, { "season", 0.5f }, { "hour", 18 } }, 18.0f, 0.5f },
		{ 0.0f, { { "dayNight", 1 }, { "seasons", 1 }, { "season", 2.5f }, { "hour", 6 } }, 6.0f, 2.5f },
		{ 123456.0f, { { "dayNight", 1 }, { "hour", 9 }, { "seasons", 1 }, { "season", 2.25f } }, 9.0f, 2.25f },
	};
	for (const Case& c : cases) {
		Server server;
		server.timeManager.currentTime = c.time;
		ServerEnvironment& env = server.environment;
		for (const auto& [name, value] : c.set)
			env.debugSet(server, name, value);
		env.startClock(c.time); // the first update's
		const float hour = EnvDayFraction(env.settings, c.time) * 24.0f;
		const float season = EnvYearFraction(env.settings, c.time) * 4.0f;
		auto within = [](float a, float b, float range) { float d = std::fabs(a - b); return std::min(d, range - d) < 0.01f; };
		char what[200];
		snprintf(what, sizeof(what), "t=%.0f: asked %05.2f h, season %.2f; shows %05.2f h, season %.2f (%s)", c.time, c.hour, c.season, hour,
			season, SeasonName((int)std::floor(season) % 4));
		check(within(hour, c.hour, 24.0f) && within(season, c.season, 4.0f), what);
	}
	// sunset: hours after this season's sunset, measured on the clock the game goes on with
	{
		Server server;
		server.timeManager.currentTime = 54321.0f;
		ServerEnvironment& env = server.environment;
		env.debugSet(server, "dayNight", 1);
		env.debugSet(server, "seasons", 1);
		env.debugSet(server, "season", 1.5f);
		env.debugSet(server, "sunset", 0.6f);
		env.startClock(54321.0f);
		const float df = EnvDayFraction(env.settings, 54321.0f), yf = EnvYearFraction(env.settings, 54321.0f);
		const SunState sun = ComputeSun(df, yf, true, 0.0f);
		char what[200];
		snprintf(what, sizeof(what), "sunset +0.6 h in summer: shows %05.2f h, the sunset at %05.2f h", df * 24.0f, sun.sunset);
		check(std::fabs(df * 24.0f - (sun.sunset + 0.6f)) < 0.02f && df * 24.0f > 12.0f, what);
	}
	printf("ENVCLOCK %s (%d failed)\n", failures ? "FAILED" : "PASSED", failures);
}

// Unit history (UnitHistory): on 4 Peaks, three fights in three places, and checks that every
// death is credited to a unit of the other side: spearmen against weakened spearmen; archers
// against weakened spearmen (the arrows' kills to the archers); two archers garrisoned in a
// watch tower against weakened spearmen (the tower's arrows to the archers inside). Then a unit
// that was hit but is terminated with hit points left (a removal) is no kill, and a save and
// load keeps the counts. Prints "UNITTEST ok/FAILED ..." and "UNITTEST PASSED/FAILED".
void Test_UnitHistory()
{
	int failures = 0;
	auto check = [&](bool ok, const std::string& what) {
		if (!ok) failures++;
		printf("UNITTEST %s: %s\n", ok ? "ok" : "FAILED", what.c_str());
		fflush(stdout);
	};
	LoadBCP("data.bcp");
	std::map<uint32_t, std::pair<int, int>> savedCounts;
	const std::string saveName = "Save_Games\\wkbai unit history";
	const std::string savePath = PathToUtf8(g_userPath / "saved" / PathFromUtf8(saveName + ".sav"));
	{
		Server server;
		server.loadSaveGame("Save_Games\\2 Player 4 Peaks.lvl");
		server.unitHistory.trace = getenv("WKB_TRACE_KILLS") != nullptr;
		ServerGameObject* own = server.findObject(1027);
		ServerGameObject* enemy = server.findObject(1030);
		const GameSet& gs = *server.gameSet;
		const GameObjBlueprint* spear = gs.findBlueprint(Tags::GAMEOBJCLASS_CHARACTER, "Spearman");
		const GameObjBlueprint* archer = gs.findBlueprint(Tags::GAMEOBJCLASS_CHARACTER, "Archer");
		const GameObjBlueprint* towerBp = gs.findBlueprint(Tags::GAMEOBJCLASS_BUILDING, "Watch Tower");
		const int attack = gs.commands.names.getIndex("Attack");
		const int garrison = gs.commands.names.getIndex("Garrison");
		const int hp = gs.items.names.getIndex("Hit Points");
		if (!own || !enemy || !spear || !archer || attack < 0 || hp < 0) {
			printf("UNITTEST not set up\n");
			return;
		}
		const auto area = server.terrain->getPlayableArea();
		const Vector3 centre(area.first * 0.5f, 0, area.second * 0.5f);
		auto at = [&](const Vector3& base, float x, float z) { Vector3 p = base + Vector3(x, 0, z); p.y = server.terrain->getHeight(p.x, p.z); return p; };
		struct Fight { const char* name; std::vector<SrvGORef> own, enemy; };
		std::vector<Fight> fights;
		auto weaken = [&](std::vector<SrvGORef>& list, float f) { for (auto& r : list) r->setItem(hp, r->getItem(hp) * f); };
		// 1: spearmen against weakened spearmen
		{
			Fight f{ "melee" };
			const Vector3 b = centre + Vector3(-60, 0, -120);
			for (int i = 0; i < 6; i++)
				f.own.emplace_back(server.spawnObject(spear, own, at(b, -8.0f + i * 3.0f, 0.0f), Vector3(0, 0, 0)));
			for (int i = 0; i < 6; i++)
				f.enemy.emplace_back(server.spawnObject(spear, enemy, at(b, -8.0f + i * 3.0f, 10.0f), Vector3(0, 3.14159f, 0)));
			weaken(f.enemy, 0.5f);
			fights.push_back(std::move(f));
		}
		// 2: archers against weakened spearmen 35 m away
		{
			Fight f{ "archers" };
			const Vector3 b = centre + Vector3(-60, 0, 120);
			for (int i = 0; i < 5; i++)
				f.own.emplace_back(server.spawnObject(archer, own, at(b, -6.0f + i * 3.0f, 0.0f), Vector3(0, 0, 0)));
			for (int i = 0; i < 4; i++)
				f.enemy.emplace_back(server.spawnObject(spear, enemy, at(b, -5.0f + i * 3.0f, 35.0f), Vector3(0, 3.14159f, 0)));
			weaken(f.enemy, 0.25f);
			fights.push_back(std::move(f));
		}
		// 3: two archers garrisoned in a watch tower
		ServerGameObject* tower = nullptr;
		std::vector<SrvGORef> garrisoned;
		const Vector3 towerAt = centre + Vector3(140, 0, 0);
		if (towerBp && garrison >= 0) {
			tower = server.spawnObject(towerBp, own, at(towerAt, 0, 0), Vector3(0, 0, 0));
			for (int i = 0; i < 2; i++) {
				ServerGameObject* a = server.spawnObject(archer, own, at(towerAt, -4.0f + i * 8.0f, -10.0f), Vector3(0, 0, 0));
				garrisoned.emplace_back(a);
				gs.commands[garrison].execute(a, tower, Tags::ORDERASSIGNMODE_FORGET_EVERYTHING_ELSE);
			}
		}
		server.timeManager.unpause();
		server.timeManager.stepMs = 20;
		auto run = [&](float seconds, const std::function<void(int)>& each) {
			const int steps = (int)(seconds * 50.0f);
			for (int step = 0; step < steps; step++) {
				if (each)
					each(step);
				server.timeManager.pendingMs = server.timeManager.stepMs;
				if (server.timeManager.step())
					server.simulate();
			}
		};
		// the archers go into the tower first
		run(15.0f, nullptr);
		int inside = 0;
		if (tower) {
			const int cat = gs.associations.names.getIndex("Garrison");
			if (auto it = tower->associates.find(cat); cat >= 0 && it != tower->associates.end())
				inside = (int)it->second.size();
			Fight f{ "tower" };
			for (int i = 0; i < 4; i++)
				f.enemy.emplace_back(server.spawnObject(spear, enemy, at(towerAt, -5.0f + i * 3.0f, 30.0f), Vector3(0, 3.14159f, 0)));
			weaken(f.enemy, 0.15f);
			fights.push_back(std::move(f));
		}
		printf("UNITTEST %d archers in the tower\n", inside);
		auto alive = [](ServerGameObject* o) { return o && !o->deleted && !(o->flags & ServerGameObject::fTerminated) && o->blueprint->name.rfind("Dead ", 0) != 0; };
		auto nearest = [&](ServerGameObject* from, std::vector<SrvGORef>& list) -> ServerGameObject* {
			ServerGameObject* best = nullptr;
			float bd = 1e30f;
			for (auto& r : list)
				if (alive(r.get())) {
					const float d = (r->position - from->position).sqlen2xz();
					if (d < bd) { bd = d; best = r.get(); }
				}
			return best;
		};
		run(90.0f, [&](int step) {
			if (step % 150)
				return;
			for (Fight& f : fights) {
				for (auto& r : f.own)
					if (alive(r.get()))
						if (ServerGameObject* tg = nearest(r.get(), f.enemy))
							gs.commands[attack].execute(r.get(), tg, Tags::ORDERASSIGNMODE_FORGET_EVERYTHING_ELSE);
				for (auto& r : f.enemy)
					if (alive(r.get())) {
						ServerGameObject* tg = f.own.empty() ? tower : nearest(r.get(), f.own);
						if (tg)
							gs.commands[attack].execute(r.get(), tg, Tags::ORDERASSIGNMODE_FORGET_EVERYTHING_ELSE);
					}
			}
		});
		auto dead = [&](std::vector<SrvGORef>& list) { int n = 0; for (auto& r : list) if (!alive(r.get())) n++; return n; };
		auto kills = [&](std::vector<SrvGORef>& list) { int n = 0; for (auto& r : list) if (r) n += r->kills; return n; };
		int allEnemyDead = 0, allOwnDead = 0, allOwnKills = 0, allEnemyKills = 0;
		for (Fight& f : fights) {
			std::vector<SrvGORef>& ownSide = strcmp(f.name, "tower") == 0 ? garrisoned : f.own;
			const int enemyDead = dead(f.enemy), ownDead = dead(f.own), ownKills = kills(ownSide), enemyKills = kills(f.enemy);
			printf("UNITTEST %s: enemy dead %d, own dead %d; own kills %d, enemy kills %d\n", f.name, enemyDead, ownDead, ownKills, enemyKills);
			for (auto& r : ownSide)
				if (r)
					printf("UNITTEST   %s %u %s kills %d\n", f.name, r->id, r->blueprint->name.c_str(), r->kills);
			check(enemyDead > 0, std::string(f.name) + ": the enemy lost units");
			check(ownKills > 0, std::string(f.name) + ": our side has kills");
			allEnemyDead += enemyDead; allOwnDead += ownDead; allOwnKills += ownKills; allEnemyKills += enemyKills;
		}
		// (units of one fight may shoot into another: the totals must match)
		check(allOwnKills == allEnemyDead, "every enemy death credited to our side (" + std::to_string(allOwnKills) + " of " + std::to_string(allEnemyDead) + ")");
		check(allEnemyKills == allOwnDead, "every own death credited to the enemy (" + std::to_string(allEnemyKills) + " of " + std::to_string(allOwnDead) + ")");
		if (tower) {
			check(inside == 2, "two archers went into the tower");
			check(tower->kills == 0, "the tower itself has no kills (" + std::to_string(tower->kills) + ")");
			int shooters = 0;
			for (auto& r : garrisoned)
				if (r && r->kills > 0)
					shooters++;
			check(shooters >= 1, "the garrisoned archers got the tower's kills (" + std::to_string(shooters) + " of 2 with kills)");
		}
		// a unit hit lately but terminated with hit points left (a removal): no kill
		{
			ServerGameObject* by = fights[0].own[0].get();
			ServerGameObject* victim = server.spawnObject(spear, enemy, at(centre, 0, 120), Vector3(0, 0, 0));
			victim->lastHitBy = by->id;
			victim->lastHitTime = server.timeManager.currentTime;
			const int before = by->kills;
			victim->terminate();
			check(by->kills == before, "a removal with hit points left is no kill");
		}
		server.timeManager.pause();
		for (const auto& [id, cobj] : server.idmap)
			if (cobj->kills || cobj->buildingsDestroyed)
				savedCounts[id] = { cobj->kills, cobj->buildingsDestroyed };
		check(server.saveGame(savePath.c_str()), "saved");
	}
	{
		Server server;
		server.loadSaveGame((saveName + ".sav").c_str());
		server.timeManager.pause();
		int same = 0;
		for (const auto& [id, counts] : savedCounts)
			if (ServerGameObject* o = server.findObject(id); o && o->kills == counts.first && o->buildingsDestroyed == counts.second)
				same++;
		check(!savedCounts.empty() && same == (int)savedCounts.size(),
			"the kills kept by the save (" + std::to_string(same) + " of " + std::to_string(savedCounts.size()) + " units)");
	}
	printf("UNITTEST %s (%d failed)\n", failures ? "FAILED" : "PASSED", failures);
	fflush(stdout);
}

// wkbai's unit history, phases 2-4: names (by faction, unique, the same from the same seed),
// conscripts, heroes at the threshold, the formation rules, a hero slain, and all of it
// through a save (the RE repo's docs/engine/unit-history.md)
void Test_UnitNames()
{
	int failures = 0;
	auto check = [&](bool ok, const std::string& what) {
		if (!ok) failures++;
		printf("UNITTEST %s: %s\n", ok ? "ok" : "FAILED", what.c_str());
		fflush(stdout);
	};
	LoadBCP("data.bcp");
	g_settings["simSeed"] = 4242;
	const std::string saveName = "Save_Games\\wkbai unit names";
	const std::string savePath = PathToUtf8(g_userPath / "saved" / PathFromUtf8(saveName + ".sav"));
	// the names of a fresh game, by unit id
	auto gameNames = [&](std::map<uint32_t, std::string>& out) {
		Server server;
		server.loadSaveGame("Save_Games\\2 Player 4 Peaks.lvl");
		server.startLevel();
		ServerGameObject* own = server.findObject(1027);
		const GameObjBlueprint* spear = server.gameSet->findBlueprint(Tags::GAMEOBJCLASS_CHARACTER, "Spearman");
		const GameObjBlueprint* bow = server.gameSet->findBlueprint(Tags::GAMEOBJCLASS_CHARACTER, "Pagan Mounted Bowoman");
		for (int i = 0; i < 20 && own && spear; i++)
			server.spawnObject(i % 4 == 3 && bow ? bow : spear, own, own->position + Vector3(5.0f + i, 0, 5.0f), Vector3(0, 0, 0));
		for (const auto& [id, rec] : server.unitHistory.records)
			out[id] = rec.name;
	};
	std::map<uint32_t, std::string> first, second;
	gameNames(first);
	gameNames(second);
	check(!first.empty() && first == second, "the same seed gives the same names (" + std::to_string(first.size()) + " units)");
	{
		int shown = 0;
		for (const auto& [id, name] : first)
			if (shown++ < 8)
				printf("UNITTEST   %u \"%s\"\n", id, name.c_str());
	}
	Server server;
	server.loadSaveGame("Save_Games\\2 Player 4 Peaks.lvl");
	server.startLevel();
	server.unitHistory.trace = getenv("WKB_TRACE_KILLS") != nullptr;
	UnitHistory& uh = server.unitHistory;
	ServerGameObject* own = server.findObject(1027);
	ServerGameObject* enemy = server.findObject(1030);
	const GameSet& gs = *server.gameSet;
	const GameObjBlueprint* spear = gs.findBlueprint(Tags::GAMEOBJCLASS_CHARACTER, "Spearman");
	const GameObjBlueprint* peasantBp = gs.findBlueprint(Tags::GAMEOBJCLASS_CHARACTER, "Peasant");
	const GameObjBlueprint* militiaBp = gs.findBlueprint(Tags::GAMEOBJCLASS_CHARACTER, "Militia");
	const GameObjBlueprint* cogBp = gs.findBlueprint(Tags::GAMEOBJCLASS_CHARACTER, "Cog");
	const GameObjBlueprint* tbBp = gs.findBlueprint(Tags::GAMEOBJCLASS_CHARACTER, "Trebuchet");
	const int hp = gs.items.names.getIndex("Hit Points");
	const GameObjBlueprint* formationBp = gs.objBlueprints[Tags::GAMEOBJCLASS_FORMATION].getPointer(0);
	if (!own || !enemy || !spear || !peasantBp || !militiaBp || hp < 0 || !formationBp) {
		printf("UNITTEST not set up\n");
		return;
	}
	// the name pools: 300 names for one player, none twice
	for (int faction = 0; faction < 3; faction++) {
		std::set<std::string> seen;
		std::string samples;
		for (int i = 0; i < 300; i++) {
			const std::string n = uh.drawName(900 + faction, faction, i % 3 == 0, false);
			seen.insert(n);
			if (i < 6)
				samples += "\"" + n + "\" ";
		}
		printf("UNITTEST   faction %d: %s\n", faction, samples.c_str());
		check(seen.size() == 300, "faction " + std::to_string(faction) + ": 300 names, all different (" + std::to_string(seen.size()) + ")");
	}
	{
		std::string ships;
		for (int i = 0; i < 4; i++)
			ships += "\"" + uh.drawName(950, 0, false, true) + "\" ";
		printf("UNITTEST   ships: %s\n", ships.c_str());
	}
	// the units on the map at the start: every one tracked has a name, unique within its player
	{
		std::map<uint32_t, std::set<std::string>> byPlayer;
		int named = 0, duplicate = 0, missing = 0;
		for (const auto& [id, rec] : uh.records) {
			if (rec.kind == UnitHistory::KIND_NONAME)
				continue;
			if (rec.name.empty()) { missing++; continue; }
			named++;
			if (!byPlayer[rec.player].insert(rec.name).second)
				duplicate++;
		}
		// (4 Peaks starts with peasants only: none to name)
		check(missing == 0 && duplicate == 0, "the map's units named: " + std::to_string(named) + ", none missing (" + std::to_string(missing)
			+ "), none twice (" + std::to_string(duplicate) + ")");
	}
	const Vector3 base = own->position + Vector3(30.0f, 0, 30.0f);
	auto spawn = [&](const GameObjBlueprint* bp, ServerGameObject* player, float x) {
		Vector3 p = base + Vector3(x, 0, 0);
		p.y = server.terrain->getHeight(p.x, p.z);
		return server.spawnObject(bp, player, p, Vector3(0, 0, 0));
	};
	// a kill by the engine's own path: the victim's last hit, no hit points, terminated
	float x = 0.0f;
	auto killBy = [&](ServerGameObject* killer, int count) {
		for (int i = 0; i < count; i++) {
			ServerGameObject* v = spawn(spear, killer->getPlayer() == enemy ? own : enemy, 200.0f + (x += 1.0f));
			v->setItem(hp, 0.0f);
			v->lastHitBy = killer->id;
			v->lastHitTime = server.timeManager.currentTime;
			v->terminate();
		}
	};
	const UnitHistory::Record* r = nullptr;
	// boats and siege engines
	if (cogBp) {
		ServerGameObject* cog = spawn(cogBp, own, -20.0f);
		r = uh.find(cog->id);
		check(r && r->kind == UnitHistory::KIND_SHIP && !r->name.empty(), "a cog gets a ship's name (" + (r ? r->name : std::string("none")) + ")");
	}
	if (tbBp) {
		ServerGameObject* tb = spawn(tbBp, own, -30.0f);
		r = uh.find(tb->id);
		check(r && r->kind == UnitHistory::KIND_NONAME && r->name.empty(), "a trebuchet is tracked without a name");
	}
	// conscripts
	ServerGameObject* peasant = spawn(peasantBp, own, -10.0f);
	check(!uh.tracked(peasant->id), "a peasant isn't tracked");
	peasant->convertTo(militiaBp);
	r = uh.find(peasant->id);
	check(r && r->conscript && !r->name.empty(), "conscripted, it is tracked and named (" + (r ? r->name : std::string()) + ")");
	peasant->convertTo(peasantBp);
	check(!uh.tracked(peasant->id), "back to the fields without a kill: untracked");
	peasant->convertTo(militiaBp);
	r = uh.find(peasant->id);
	const std::string conscriptName = r ? r->name : std::string();
	killBy(peasant, 1);
	peasant->convertTo(peasantBp);
	r = uh.find(peasant->id);
	check(r && r->name == conscriptName && peasant->kills == 1, "back to the fields with a kill: still tracked, its name kept (" + conscriptName + ")");
	// heroes
	uh.threshold = 3;
	ServerGameObject* soldier = spawn(spear, own, 0.0f);
	killBy(soldier, 2);
	check(!uh.find(soldier->id)->hero, "2 kills: no hero yet");
	killBy(soldier, 1);
	r = uh.find(soldier->id);
	check(r->hero && !r->epithet.empty() && r->displayName() == r->given + " " + r->epithet,
		"3 kills: a hero, \"" + r->name + "\" is now \"" + r->displayName() + "\"");
	// formations
	ServerGameObject* formation = server.spawnObject(formationBp, own, base, Vector3(0, 0, 0));
	ServerGameObject* a = spawn(spear, own, 2.0f), * b = spawn(spear, own, 3.0f), * c = spawn(spear, own, 4.0f), * d = spawn(spear, own, 5.0f);
	// (one that stays: a formation without members is removed)
	ServerGameObject* keeper = spawn(spear, own, 1.0f);
	for (ServerGameObject* m : { keeper, a, b, c, d })
		m->setParent(formation);
	killBy(a, 3);
	check(uh.find(a->id)->hero, "the first member at the threshold becomes the formation's hero");
	killBy(b, 7);
	check(!uh.find(b->id)->hero && uh.find(b->id)->heroPending, "a second member at 7 kills is no hero (one per formation)");
	check(formation->kills == 10, "the formation's tally: 10 (" + std::to_string(formation->kills) + ")");
	b->setParent(own);
	check(b->kills == 4 && !uh.find(b->id)->hero && uh.find(b->id)->heroPending, "it leaves: 7 formation kills -> 4 kept, not a hero yet (" + std::to_string(b->kills) + ")");
	ServerGameObject* foe = spawn(spear, enemy, 50.0f);
	uh.hit(server, b, foe);
	check(!uh.find(b->id)->hero && uh.find(b->id)->heroPending, "a hit outside any fight's area: still waiting");
	uh.reachedFight(server, b->id);
	check(uh.find(b->id)->hero, "its next fight makes it a hero (" + uh.find(b->id)->displayName() + ")");
	killBy(c, 2);
	c->setParent(own);
	check(c->kills == 1, "a member with 2 formation kills leaves with 1 (" + std::to_string(c->kills) + ")");
	killBy(d, 2);
	d->setItem(hp, 0.0f);
	d->setParent(own);
	check(d->kills == 2, "a dying member keeps all its kills (" + std::to_string(d->kills) + ")");
	a->setParent(own);
	check(a->kills == 3 && uh.find(a->id)->hero, "the hero leaves with all its kills, still a hero");
	a->setParent(formation);
	soldier->setParent(formation);
	int heroes = 0;
	for (const auto& [type, members] : formation->children)
		for (CommonGameObject* m : members)
			if (const auto* mr = uh.find(m->id); mr && mr->hero)
				heroes++;
	check(heroes == 2, "two heroes join a formation: it has two (" + std::to_string(heroes) + ")");
	ServerGameObject* g = spawn(spear, own, 6.0f);
	g->setParent(formation);
	killBy(g, 4);
	check(!uh.find(g->id)->hero, "a member that is no hero can't become one in a formation with a hero");
	// a hero slain
	ServerGameObject* slayer = spawn(spear, enemy, 60.0f);
	const uint32_t aId = a->id;
	const std::string aName = uh.find(aId)->displayName();
	a->setItem(hp, 0.0f);
	a->lastHitBy = slayer->id;
	a->lastHitTime = server.timeManager.currentTime;
	a->terminate();
	r = uh.find(aId);
	const UnitHistory::Record* sr = uh.find(slayer->id);
	check(r && r->death >= 0.0f && r->slainBy == slayer->id && sr && std::count(sr->slew.begin(), sr->slew.end(), aId) == 1,
		"a hero slain: its record kept, its slayer noted (" + aName + " slain by " + (r ? r->slainByText : std::string()) + ")");
	// all of it through a save
	server.timeManager.unpause();
	server.timeManager.stepMs = 20;
	for (int step = 0; step < 100; step++) {
		server.timeManager.pendingMs = server.timeManager.stepMs;
		if (server.timeManager.step())
			server.simulate();
	}
	const std::map<uint32_t, UnitHistory::Record> before = uh.records;
	const uint64_t seed = uh.nameSeed;
	check(server.saveGame(savePath.c_str()), "saved");
	{
		Server loaded;
		loaded.loadSaveGame(savePath.c_str());
		int same = 0, differ = 0;
		for (const auto& [id, rb] : before) {
			const UnitHistory::Record* rl = loaded.unitHistory.find(id);
			ServerGameObject* ob = server.findObject(id);
			ServerGameObject* ol = loaded.findObject(id);
			const bool killsSame = (!ob && !ol) || (ob && ol && ob->kills == ol->kills) || (ob && !ol && rb.death >= 0.0f);
			if (rl && rl->name == rb.name && rl->epithet == rb.epithet && rl->hero == rb.hero && rl->heroPending == rb.heroPending && rl->birth == rb.birth
				&& rl->death == rb.death && rl->formation == rb.formation && rl->formationKills == rb.formationKills && rl->slew == rb.slew && killsSame)
				same++;
			else {
				differ++;
				if (differ < 5)
					printf("UNITTEST   differs: %u \"%s\"\n", id, rb.name.c_str());
			}
		}
		check(differ == 0 && same == (int)before.size(), "after loading: every record the same (" + std::to_string(same) + " of " + std::to_string(before.size()) + ")");
		check(loaded.unitHistory.nameSeed == seed && loaded.unitHistory.threshold == uh.threshold, "the name stream and the threshold kept");
		// a unit made after loading draws the next name in both
		ServerGameObject* lo = loaded.findObject(1027);
		Vector3 p = base + Vector3(-40.0f, 0, 0);
		p.y = server.terrain->getHeight(p.x, p.z);
		ServerGameObject* n1 = server.spawnObject(spear, own, p, Vector3(0, 0, 0));
		ServerGameObject* n2 = lo ? loaded.spawnObject(spear, lo, p, Vector3(0, 0, 0)) : nullptr;
		const UnitHistory::Record* r1 = uh.find(n1->id);
		const UnitHistory::Record* r2 = n2 ? loaded.unitHistory.find(n2->id) : nullptr;
		check(r1 && r2 && r1->name == r2->name, "the next unit gets the same name in the game and the loaded one (" + (r1 ? r1->name : std::string()) + ")");
	}
	printf(failures ? "UNITTEST FAILED (%d)\n" : "UNITTEST PASSED\n", failures);
}

// wkbai's unit history, phases 6-7: the timeline on the server. A battle names each side's top
// killer, the heroes present and those who fell in it; a hero slain far from any fight, and one
// dying without a killer, get an event of their own; the same seed gives the same events; the
// events survive their text form and a save (the RE repo's docs/engine/unit-history.md)
void Test_UnitTimeline()
{
	int failures = 0;
	auto check = [&](bool ok, const std::string& what) {
		if (!ok) failures++;
		printf("UNITTEST %s: %s\n", ok ? "ok" : "FAILED", what.c_str());
		fflush(stdout);
	};
	LoadBCP("data.bcp");
	g_settings["simSeed"] = 777;
	const std::string savePath = PathToUtf8(g_userPath / "saved" / PathFromUtf8("Save_Games\\wkbai unit timeline.sav"));
	// one game: a battle of 12 against 12, then a lone hero slain awayA away and one that falls
	// without a killer. Returns the events' texts.
	uint32_t razerId = 0;
	auto game = [&](bool keep, std::vector<std::string>& texts) {
		auto server = std::make_unique<Server>();
		Server& srv = *server;
		srv.loadSaveGame("Save_Games\\2 Player 4 Peaks.lvl");
		srv.startLevel();
		srv.unitHistory.trace = getenv("WKB_TRACE_KILLS") != nullptr;
		srv.unitHistory.threshold = 2;
		ServerGameObject* own = srv.findObject(1027);
		ServerGameObject* enemy = srv.findObject(1030);
		const GameSet& gs = *srv.gameSet;
		const GameObjBlueprint* spear = gs.findBlueprint(Tags::GAMEOBJCLASS_CHARACTER, "Spearman");
		const int attack = gs.commands.names.getIndex("Attack");
		const int hp = gs.items.names.getIndex("Hit Points");
		const int si = gs.items.names.getIndex("Structural Integrity");
		if (!own || !enemy || !spear || attack < 0 || hp < 0)
			return std::unique_ptr<Server>();
		const auto area = srv.terrain->getPlayableArea();
		const Vector3 centre(area.first * 0.5f, 0, area.second * 0.5f);
		auto at = [&](const Vector3& base, float x, float z) { Vector3 p = base + Vector3(x, 0, z); p.y = srv.terrain->getHeight(p.x, p.z); return p; };
		const Vector3 b = centre + Vector3(-60, 0, -120);
		std::vector<SrvGORef> ours, theirs;
		for (int i = 0; i < 12; i++) {
			ours.emplace_back(srv.spawnObject(spear, own, at(b, -16.0f + i * 3.0f, 0.0f), Vector3(0, 0, 0)));
			theirs.emplace_back(srv.spawnObject(spear, enemy, at(b, -16.0f + i * 3.0f, 10.0f), Vector3(0, 3.14159f, 0)));
		}
		for (auto& r : theirs)
			r->setItem(hp, r->getItem(hp) * 0.6f);
		// an enemy settlement by the battlefield, razed by one of ours while the battle goes on:
		// its Destroyed event names the razer and the battle's fighters
		const GameObjBlueprint* manor = gs.findBlueprint(Tags::GAMEOBJCLASS_BUILDING, "Manor");
		SrvGORef village;
		if (manor && si >= 0)
			village = srv.spawnObject(manor, enemy, at(b, 0.0f, 40.0f), Vector3(0, 0, 0));
		srv.timeManager.unpause();
		srv.timeManager.stepMs = 20;
		auto alive = [](ServerGameObject* o) { return o && !o->deleted && !(o->flags & ServerGameObject::fTerminated); };
		auto nearest = [&](ServerGameObject* from, std::vector<SrvGORef>& list) -> ServerGameObject* {
			ServerGameObject* best = nullptr;
			float bd = 1e30f;
			for (auto& r : list)
				if (alive(r.get())) {
					const float d = (r->position - from->position).sqlen2xz();
					if (d < bd) { bd = d; best = r.get(); }
				}
			return best;
		};
		for (int step = 0; step < 50 * 70; step++) {
			if (step == 50 * 12)
				if (ServerGameObject* v = village.get())
					for (auto& r : ours)
						if (alive(r.get())) {
							razerId = r->id;
							v->setItem(si, 0.0f);
							v->lastHitBy = r->id;
							v->lastHitTime = srv.timeManager.currentTime;
							v->terminate();
							break;
						}
			if (step % 150 == 0)
				for (auto* side : { &ours, &theirs })
					for (auto& r : *side)
						if (alive(r.get()))
							if (ServerGameObject* tg = nearest(r.get(), side == &ours ? theirs : ours))
								gs.commands[attack].execute(r.get(), tg, Tags::ORDERASSIGNMODE_FORGET_EVERYTHING_ELSE);
			// at 40 s, one of our heroes falls in the fight, to the nearest enemy
			if (step == 50 * 40)
				for (auto& r : ours)
					if (const UnitHistory::Record* rec = srv.unitHistory.find(r.objid); rec && rec->hero && alive(r.get())) {
						ServerGameObject* foe = nearest(r.get(), theirs);
						if (!foe)
							foe = srv.spawnObject(spear, enemy, r->position + Vector3(2, 0, 0), Vector3(0, 0, 0));
						r->setItem(hp, 0.0f);
						r->lastHitBy = foe->id;
						r->lastHitTime = srv.timeManager.currentTime;
						r->terminate();
						break;
					}
			srv.timeManager.pendingMs = srv.timeManager.stepMs;
			if (srv.timeManager.step())
				srv.simulate();
		}
		// a hero far from the fight, slain by an enemy there
		const Vector3 awayA = centre + Vector3(150, 0, 150);
		ServerGameObject* lone = srv.spawnObject(spear, own, at(awayA, 0, 0), Vector3(0, 0, 0));
		ServerGameObject* slayer = srv.spawnObject(spear, enemy, at(awayA, 3, 0), Vector3(0, 0, 0));
		for (int i = 0; i < 2; i++) {
			ServerGameObject* v = srv.spawnObject(spear, enemy, at(awayA, 6.0f + i, 6.0f), Vector3(0, 0, 0));
			v->setItem(hp, 0.0f);
			v->lastHitBy = lone->id;
			v->lastHitTime = srv.timeManager.currentTime;
			v->terminate();
		}
		lone->setItem(hp, 0.0f);
		lone->lastHitBy = slayer->id;
		lone->lastHitTime = srv.timeManager.currentTime;
		lone->terminate();
		// one more, no killer: it falls (the cold, an Act of God)
		const Vector3 awayB = centre + Vector3(-150, 0, 150);
		ServerGameObject* frozen = srv.spawnObject(spear, own, at(awayB, 0, 0), Vector3(0, 0, 0));
		for (int i = 0; i < 2; i++) {
			ServerGameObject* v = srv.spawnObject(spear, enemy, at(awayB, 6.0f + i, 6.0f), Vector3(0, 0, 0));
			v->setItem(hp, 0.0f);
			v->lastHitBy = frozen->id;
			v->lastHitTime = srv.timeManager.currentTime;
			v->terminate();
		}
		// (the weather's harm: ServerEnvironment::damage notes the cause)
		srv.unitHistory.noteCause(frozen->id, "froze to death", (float)srv.timeManager.currentTime);
		frozen->setItem(hp, 0.0f);
		frozen->terminate();
		auto runFor = [&](float seconds) {
			for (int step = 0; step < (int)(50 * seconds); step++) {
				srv.timeManager.pendingMs = srv.timeManager.stepMs;
				if (srv.timeManager.step())
					srv.simulate();
			}
		};
		runFor(6.0f);
		for (const auto& e : srv.unitHistory.chronicle.chron.events)
			texts.push_back(e.toText());
		if (!keep)
			server.reset();
		return server;
	};
	std::vector<std::string> first, second;
	game(false, second);
	auto server = game(true, first);
	if (!server) {
		printf("UNITTEST not set up\n");
		return;
	}
	Server& srv = *server;
	check(!first.empty() && first == second, "the same seed gives the same events (" + std::to_string(first.size()) + ")");
	const auto& events = srv.unitHistory.chronicle.chron.events;
	const GameChronicle::Event* battle = nullptr;
	for (const auto& e : events) {
		printf("UNITTEST   %s\n", e.toText().c_str());
		if (e.fight() && !battle)
			battle = &e;
	}
	check(battle != nullptr, "a fight on the timeline");
	if (battle) {
		int top = 0, present = 0, fallenHere = 0;
		for (const auto& m : battle->mentions) {
			top += m.kind == GameChronicle::MENTION_TOP_KILLER;
			present += m.kind == GameChronicle::MENTION_PRESENT;
			fallenHere += m.kind == GameChronicle::MENTION_FALLEN;
		}
		check(top >= 1, "the fight names a top killer per side (" + std::to_string(top) + ")");
		// its top killers have the most kills of their side's units in it
		for (const auto& m : battle->mentions)
			if (m.kind == GameChronicle::MENTION_TOP_KILLER) {
				ServerGameObject* u = srv.findObject(m.unit);
				const UnitHistory::Record* r = srv.unitHistory.find(m.unit);
				const int total = u && !(r && r->death >= 0.0f) ? u->kills : r ? r->kills : -1;
				check(m.kills >= 1 && m.kills <= total, "top killer " + m.name + ": " + std::to_string(m.kills) + " here, " + std::to_string(total) + " in all"
					+ " (" + (u ? u->blueprint->name + " kills " + std::to_string(u->kills) : std::string("gone")) + ", record death " + (r ? std::to_string(r->death) + " kills " + std::to_string(r->kills) : std::string("none")) + ")");
			}
		check(present >= 1, "heroes present named (" + std::to_string(present) + ")");
		check(fallenHere >= 1, "heroes fallen in the fight named (" + std::to_string(fallenHere) + ")");
	}
	int ownFallen = 0;
	bool slainBy = false, fell = false;
	for (const auto& e : events)
		if (e.kind == GameChronicle::Kind::Fallen) {
			ownFallen++;
			for (const auto& m : e.mentions)
				if (m.kind == GameChronicle::MENTION_FALLEN) {
					slainBy |= m.extra.rfind("slain by ", 0) == 0;
					fell |= m.extra == "froze to death";
				}
			check(e.title().rfind("The fall of ", 0) == 0, "its title: " + e.title());
		}
	check(ownFallen == 2 && slainBy && fell, "the two heroes slain away from the fight: events of their own, one slain, one froze to death (" + std::to_string(ownFallen) + ")");
	// the settlement destroyed: who razed it, who fought there
	{
		const GameChronicle::Event* destroyed = nullptr;
		for (const auto& e : events)
			if (e.kind == GameChronicle::Kind::Destroyed)
				destroyed = &e;
		bool razedBy = false;
		int fighters = 0;
		if (destroyed)
			for (const auto& m : destroyed->mentions) {
				razedBy |= m.kind == GameChronicle::MENTION_RAZED_BY && m.unit == razerId && m.kills >= 1;
				fighters += m.kind == GameChronicle::MENTION_TOP_KILLER || m.kind == GameChronicle::MENTION_PRESENT;
			}
		check(destroyed && razedBy, "the settlement destroyed names who razed it");
		check(fighters >= 1, "and who fought there (" + std::to_string(fighters) + ")");
	}
	// the text form
	{
		int same = 0;
		for (const auto& e : events) {
			GameChronicle::Event back;
			if (back.fromText(e.toText()) && back.toText() == e.toText())
				same++;
		}
		check(same == (int)events.size(), "every event the same through its text (" + std::to_string(same) + ")");
	}
	// a save
	check(srv.saveGame(savePath.c_str()), "saved");
	std::vector<std::string> before;
	for (auto& e : srv.unitHistory.chronicle.chron.events)
		before.push_back(e.toText());
	{
		Server loaded;
		loaded.loadSaveGame(savePath.c_str());
		std::vector<std::string> after;
		for (auto& e : loaded.unitHistory.chronicle.chron.events)
			after.push_back(e.toText());
		check(after == before, "the timeline kept by the save (" + std::to_string(after.size()) + " events)");
		size_t samples = 0, samplesBefore = 0;
		for (auto& [p, l] : loaded.unitHistory.chronicle.chron.samples) samples += l.size();
		for (auto& [p, l] : srv.unitHistory.chronicle.chron.samples) samplesBefore += l.size();
		check(samples == samplesBefore && samples > 0, "the statistics kept by the save (" + std::to_string(samples) + " samples)");
	}
	printf(failures ? "UNITTEST FAILED (%d)\n" : "UNITTEST PASSED\n", failures);
}

// wkbai's unit history: splash kills (a rocket launcher at a cluster: every kill its own) and an
// Act of God (the lightning's own damage sequence: the victims send the attack to themselves, no
// unit is credited) (the RE repo's docs/engine/unit-history.md, section 2)
void Test_UnitSplashKills()
{
	int failures = 0;
	auto check = [&](bool ok, const std::string& what) {
		if (!ok) failures++;
		printf("UNITTEST %s: %s\n", ok ? "ok" : "FAILED", what.c_str());
		fflush(stdout);
	};
	LoadBCP("data.bcp");
	g_settings["simSeed"] = 99;
	Server server;
	server.loadSaveGame("Save_Games\\2 Player 4 Peaks.lvl");
	server.startLevel();
	server.unitHistory.trace = getenv("WKB_TRACE_KILLS") != nullptr;
	ServerGameObject* own = server.findObject(1027);
	ServerGameObject* enemy = server.findObject(1030);
	const GameSet& gs = *server.gameSet;
	const GameObjBlueprint* spear = gs.findBlueprint(Tags::GAMEOBJCLASS_CHARACTER, "Spearman");
	const GameObjBlueprint* rocketBp = gs.findBlueprint(Tags::GAMEOBJCLASS_CHARACTER, "Rocket Launcher");
	const int attack = gs.commands.names.getIndex("Fire Rocket") >= 0 ? gs.commands.names.getIndex("Fire Rocket") : gs.commands.names.getIndex("Attack");
	const int hp = gs.items.names.getIndex("Hit Points");
	const int lightning = gs.actionSequences.names.getIndex("Damage Done By Lightning");
	if (!own || !enemy || !spear || !rocketBp || attack < 0 || hp < 0) {
		printf("UNITTEST not set up\n");
		return;
	}
	const auto area = server.terrain->getPlayableArea();
	const Vector3 centre(area.first * 0.5f, 0, area.second * 0.5f);
	auto at = [&](const Vector3& base, float x, float z) { Vector3 p = base + Vector3(x, 0, z); p.y = server.terrain->getHeight(p.x, p.z); return p; };
	auto alive = [](ServerGameObject* o) { return o && !o->deleted && !(o->flags & ServerGameObject::fTerminated) && o->blueprint->name.rfind("Dead ", 0) != 0; };
	server.timeManager.unpause();
	server.timeManager.stepMs = 20;
	auto run = [&](float seconds, const std::function<void(int)>& each) {
		for (int step = 0; step < (int)(seconds * 50.0f); step++) {
			if (each)
				each(step);
			server.timeManager.pendingMs = server.timeManager.stepMs;
			if (server.timeManager.step())
				server.simulate();
		}
	};
	// a rocket launcher 45 m from 9 weakened spearmen standing close together
	const Vector3 b = centre + Vector3(-60, 0, -120);
	ServerGameObject* launcher = server.spawnObject(rocketBp, own, at(b, 0, 0), Vector3(0, 0, 0));
	std::vector<SrvGORef> cluster;
	for (int i = 0; i < 9; i++) {
		ServerGameObject* s = server.spawnObject(spear, enemy, at(b, -3.0f + (i % 3) * 3.0f, 45.0f + (i / 3) * 3.0f), Vector3(0, 3.14159f, 0));
		s->setItem(hp, 1.0f);
		cluster.emplace_back(s);
	}
	run(90.0f, [&](int step) {
		// once: given again, the launcher starts its aim over
		if (step != 0 || !alive(launcher))
			return;
		for (auto& r : cluster)
			if (alive(r.get())) {
				gs.commands[attack].execute(launcher, r.get(), Tags::ORDERASSIGNMODE_FORGET_EVERYTHING_ELSE);
				break;
			}
	});
	int dead = 0;
	for (auto& r : cluster)
		dead += !alive(r.get());
	printf("UNITTEST rockets: %d of 9 dead, the launcher has %d kills\n", dead, launcher->kills);
	check(dead >= 2, "the rockets killed several (" + std::to_string(dead) + ")");
	check(launcher->kills == dead, "every one the launcher's kill (" + std::to_string(launcher->kills) + " of " + std::to_string(dead) + ")");
	// an Act of God: lightning's damage on 4 soldiers with 1 hit point, none hit before
	if (lightning >= 0) {
		const Vector3 c = centre + Vector3(140, 0, 0);
		std::vector<SrvGORef> struck;
		int creditedBefore = 0;
		for (auto* side : { own, enemy })
			for (const auto& [type, list] : side->children)
				for (CommonGameObject* o : list)
					creditedBefore += o->kills;
		for (int i = 0; i < 4; i++) {
			ServerGameObject* s = server.spawnObject(spear, enemy, at(c, i * 2.0f, 0.0f), Vector3(0, 0, 0));
			s->setItem(hp, 1.0f);
			struck.emplace_back(s);
			// (the strike where it stands: the damage is by the distance to the sequence's executor)
			SrvScriptContext ctx(&server, s);
			auto _e = ctx.change(ctx.sequenceExecutor, s);
			gs.actionSequences[lightning].run(&ctx);
		}
		for (auto& r : struck)
			if (r)
				printf("UNITTEST   struck %u: %.1f hit points\n", r->id, r->getItem(hp));
		run(5.0f, nullptr);
		int struckDead = 0, credited = 0;
		for (auto& r : struck)
			struckDead += !alive(r.get());
		for (auto* side : { own, enemy })
			for (const auto& [type, list] : side->children)
				for (CommonGameObject* o : list)
					credited += o->kills;
		printf("UNITTEST lightning: %d of 4 dead, kills credited %d -> %d\n", struckDead, creditedBefore, credited);
		check(struckDead >= 1, "the lightning killed (" + std::to_string(struckDead) + ")");
		check(credited == creditedBefore, "nobody credited with the Act of God's kills");
	}
	else
		printf("UNITTEST no lightning sequence in this game set\n");
	printf(failures ? "UNITTEST FAILED (%d)\n" : "UNITTEST PASSED\n", failures);
}

// Lightning on buildings (redata's "wkbai patches\Lightning.cpp", the game set's own fire): two
// enemy barracks struck by "Damage Done By Direct Lightning" as the Act of God and a thunderstorm
// (ServerEnvironment::strikeLightning) run it, one with the caster's player as the fire's possible
// source (the Act of God's task), one with none (a storm). Each loses only 5-10% of its Structural
// Integrity, is set alight (Fire Strength, "Burning Buildings") and burns on; a soldier struck is
// hurt as before (400 Hit Points); no unit is credited with what the fires destroy.
void Test_LightningBuildings()
{
	int failures = 0;
	auto check = [&](bool ok, const std::string& what) {
		if (!ok) failures++;
		printf("UNITTEST %s: %s\n", ok ? "ok" : "FAILED", what.c_str());
		fflush(stdout);
	};
	LoadBCP("data.bcp");
	g_settings["simSeed"] = 99;
	Server server;
	server.loadSaveGame("Save_Games\\2 Player 4 Peaks.lvl");
	server.startLevel();
	ServerGameObject* own = server.findObject(1027);
	ServerGameObject* enemy = server.findObject(1030);
	const GameSet& gs = *server.gameSet;
	const GameObjBlueprint* barracks = gs.findBlueprint(Tags::GAMEOBJCLASS_BUILDING, "Barracks Stone");
	const GameObjBlueprint* spear = gs.findBlueprint(Tags::GAMEOBJCLASS_CHARACTER, "Spearman");
	const int strike = gs.actionSequences.names.getIndex("Damage Done By Direct Lightning");
	const int si = gs.items.names.getIndex("Structural Integrity");
	const int siCap = gs.items.names.getIndex("Structural Integrity Capacity");
	const int fire = gs.items.names.getIndex("Fire Strength (SI)");
	const int hp = gs.items.names.getIndex("Hit Points");
	const int burning = gs.aliases.names.getIndex("Burning Buildings");
	const int source = gs.associations.names.getIndex("Possible Source Of Fire");
	if (!own || !enemy || !barracks || !spear || strike < 0 || si < 0 || siCap < 0 || fire < 0 || hp < 0 || burning < 0 || source < 0) {
		printf("UNITTEST not set up\n");
		printf("UNITTEST FAILED (1)\n");
		return;
	}
	const auto area = server.terrain->getPlayableArea();
	const Vector3 centre(area.first * 0.5f, 0, area.second * 0.5f);
	auto at = [&](const Vector3& base, float x, float z) { Vector3 p = base + Vector3(x, 0, z); p.y = server.terrain->getHeight(p.x, p.z); return p; };
	auto alive = [](ServerGameObject* o) { return o && !o->deleted && !(o->flags & ServerGameObject::fTerminated) && o->blueprint->name.rfind("Dead ", 0) != 0; };
	auto credits = [&]() {
		int n = 0;
		for (auto* side : { own, enemy })
			for (const auto& [type, list] : side->children)
				for (CommonGameObject* o : list)
					n += o->kills + o->buildingsDestroyed;
		return n;
	};
	auto inAlias = [&](ServerGameObject* b) {
		for (const auto& ref : server.aliases[burning])
			if (ref.getFrom<Server>() == b)
				return true;
		return false;
	};
	auto strikeOn = [&](ServerGameObject* o) {
		SrvScriptContext ctx(&server, o);
		auto _ = ctx.change(ctx.sequenceExecutor, o);
		gs.actionSequences[strike].run(&ctx);
	};
	server.timeManager.unpause();
	server.timeManager.stepMs = 20;
	auto run = [&](float seconds) {
		for (int step = 0; step < (int)(seconds * 50.0f); step++) {
			server.timeManager.pendingMs = server.timeManager.stepMs;
			if (server.timeManager.step())
				server.simulate();
		}
	};
	const int creditedBefore = credits();
	// two barracks of the enemy's, far from everything, 40 m apart (a fire spreads 25 m at most)
	const Vector3 c = centre + Vector3(140, 0, 40);
	SrvGORef struck[2] = { server.spawnObject(barracks, enemy, at(c, 0, 0), Vector3(0, 0, 0)),
		server.spawnObject(barracks, enemy, at(c, 40, 0), Vector3(0, 0, 0)) };
	float before[2], cap[2];
	for (int i = 0; i < 2; i++) {
		ServerGameObject* b = struck[i].get();
		const std::string who = i == 0 ? "Act of God" : "storm";
		before[i] = b->getItem(si);
		cap[i] = b->getItem(siCap);
		// the Act of God's task: the caster's player the fire's possible source; a storm: none
		b->clearAssociates(source);
		if (i == 0)
			b->associateObject(source, own);
		strikeOn(b);
		const float lost = before[i] - b->getItem(si);
		const float share = lost / std::max(cap[i], 1.0f);
		printf("UNITTEST barracks %u (%s): %.0f of %.0f structural integrity -> %.0f (lost %.0f, %.1f%%), fire strength %.1f\n", b->id,
			who.c_str(), before[i], cap[i], b->getItem(si), lost, share * 100.0f, b->getItem(fire));
		check(share >= 0.05f && share <= 0.10f, who + ": only slight damage (" + std::to_string(share * 100.0f) + "%)");
		check(b->getItem(fire) > 0.0f, who + ": the building is on fire");
		check(inAlias(b), who + ": among the Burning Buildings");
	}
	// a soldier struck: as before
	ServerGameObject* soldier = server.spawnObject(spear, enemy, at(c, 20, 60), Vector3(0, 0, 0));
	const float soldierHp = soldier->getItem(hp);
	strikeOn(soldier);
	printf("UNITTEST soldier: %.0f -> %.0f hit points\n", soldierHp, soldier->getItem(hp));
	// (400 taken, or all it had: the hit points stop at 0)
	check(soldier->getItem(hp) <= std::max(0.0f, soldierHp - 399.0f), "a soldier struck loses 400 hit points, as before");
	// the fires burn on (Father.cpp's "Process Fire" every 2.9 s)
	float afterStrike[2];
	for (int i = 0; i < 2; i++)
		afterStrike[i] = struck[i]->getItem(si);
	run(20.0f);
	for (int i = 0; i < 2; i++) {
		ServerGameObject* b = struck[i].get();
		const bool standing = alive(b);
		printf("UNITTEST barracks %d after 20 s: %.0f structural integrity, fire strength %.1f\n", i, standing ? b->getItem(si) : 0.0f, standing ? b->getItem(fire) : 0.0f);
		check(standing && b->getItem(si) < afterStrike[i] && b->getItem(fire) > 0.0f, "the fire burns on (" + std::to_string(i) + ")");
	}
	// both brought close to their end (as a long fire would): they burn down; nobody credited
	for (auto& r : struck)
		if (alive(r.get()))
			r->setItem(si, 20.0f);
	run(60.0f);
	int destroyed = 0;
	for (auto& r : struck)
		destroyed += !alive(r.get());
	printf("UNITTEST after 80 s: %d of 2 barracks burnt down, credits %d -> %d\n", destroyed, creditedBefore, credits());
	check(destroyed == 2, "both burnt down (" + std::to_string(destroyed) + ")");
	check(credits() == creditedBefore, "nobody credited with what the lightning's fires did");
	printf(failures ? "UNITTEST FAILED (%d)\n" : "UNITTEST PASSED\n", failures);
}

// wkbai's timeline told as a chronicle (interface/ChronicleNarrator, redata/chronicle/english.txt):
// the word file whole (every slot ten forms or more, every placeholder a word or a fact), every
// kind of event as a paragraph, every named unit a link in it, the players' names as theirs,
// nothing left of the templates, the same words every time, the weather's words, the calendar
void Test_ChronicleStory()
{
	int failures = 0;
	auto check = [&](bool ok, const std::string& what) {
		if (!ok) failures++;
		printf("UNITTEST %s: %s\n", ok ? "ok" : "FAILED", what.c_str());
		fflush(stdout);
	};
	using GC = GameChronicle;
	check(ChronicleNarrator::load(), "redata/chronicle/english.txt read");
	const auto& words = ChronicleNarrator::words();
	const auto& sentences = ChronicleNarrator::sentences();
	{
		size_t forms = 0, alts = 0;
		std::string few;
		for (const auto& [slot, list] : sentences) {
			forms += list.size();
			if (list.size() < 10 && slot != "date.war")
				few += " " + slot + "(" + std::to_string(list.size()) + ")";
		}
		for (const auto& [w, list] : words)
			alts += list.size();
		printf("UNITTEST   %zu sentence slots with %zu forms, %zu words with %zu alternatives\n", sentences.size(), forms, words.size(), alts);
		check(few.empty(), "every sentence slot has ten forms or more" + (few.empty() ? std::string() : ":" + few));
		// every {NAME}: a word (or a tagged one) or a fact the game puts in
		static const std::set<std::string> facts = { "DATE", "PLACE", "SIDES", "SIDE", "SIDE_S", "YEAR", "DAY", "COUNT", "MORE", "FEWER", "MORE_N",
			"FEWER_N", "WINNER", "LOSER", "WINNER_S", "LOSER_S", "LOST_HI", "LOST_LO", "TYPE_CLAUSE", "LOSSES", "UNIT", "UNIT_S", "KILLS",
			"KILLS_CAP", "KILLS_PHRASE", "KILLS_FOES", "N_FOES", "KILLS_CLAUSE", "HEROES", "CAUSE_CLAUSE", "SETTLEMENT", "A_SETTLEMENT",
			"OWNER", "OWNER_S", "RAZERS", "VICTIM", "KILLER", "EXTRA", "TYPES" };
		std::set<std::string> bases;
		for (const auto& [w, list] : words)
			bases.insert(w.substr(0, w.find('.')));
		std::set<std::string> unknown;
		auto scan = [&](const std::string& s) {
			for (size_t a = s.find('{'); a != std::string::npos; a = s.find('{', a + 1)) {
				const size_t b = s.find('}', a);
				const std::string name = s.substr(a + 1, b - a - 1);
				if (!facts.count(name) && !bases.count(name))
					unknown.insert(name);
			}
		};
		for (const auto& [slot, list] : sentences)
			for (const auto& s : list)
				scan(s);
		for (const auto& [w, list] : words)
			for (const auto& s : list)
				scan(s);
		std::string list;
		for (const auto& u : unknown)
			list += " " + u;
		check(unknown.empty(), "every placeholder a word or a fact" + (list.empty() ? std::string() : ":" + list));
	}
	auto mention = [](int kind, uint32_t unit, uint32_t player, int kills, bool dead, const std::string& name, const std::string& extra = std::string()) {
		GC::Mention m;
		m.kind = kind; m.unit = unit; m.player = player; m.kills = kills; m.dead = dead; m.name = name; m.extra = extra;
		return m;
	};
	std::vector<GC::Event> events;
	{
		GC::Event e{ GC::Kind::Battle, 300.0f, 420.0f, Vector3(), { 1027, 1030 }, 22, std::string(), "Nen Hithoel" };
		e.id = 1;
		e.weather = WEATHER_FOG;
		e.temperature = 8.0f;
		e.strength = { { 1027, 12 }, { 1030, 10 } };
		e.lost[1027]["Spearman"] = 3;
		e.lost[1030]["Spearman"] = 10;
		e.mentions = { mention(GC::MENTION_TOP_KILLER, 4136, 1027, 3, true, "Ralph the Wolf"),
			mention(GC::MENTION_TOP_KILLER, 4127, 1030, 1, false, "Aelfred of Bamburgh"),
			mention(GC::MENTION_PRESENT, 4128, 1027, 2, true, "Thorstein Iron-Fist"),
			mention(GC::MENTION_PRESENT, 4140, 1027, 5, false, "Gaut Raven-Feeder"),
			mention(GC::MENTION_FALLEN, 4128, 1027, 2, true, "Thorstein Iron-Fist", "slain by Aelfred of Bamburgh") };
		events.push_back(e);
	}
	{
		GC::Event e{ GC::Kind::Siege, 900.0f, 1300.0f, Vector3(), { 1027, 1030 }, 70, std::string(), "Duncraig" };
		e.id = 2;
		e.weather = WEATHER_THUNDERSTORM;
		e.strength = { { 1027, 50 }, { 1030, 20 } };
		e.lost[1027]["Archer"] = 6;
		e.lost[1030]["Militia"] = 14;
		e.lost[1030]["Spearman"] = 2;
		events.push_back(e);
	}
	{
		GC::Event e{ GC::Kind::Raid, 1000.0f, 1010.0f, Vector3(), { 1027, 1030 }, 6, std::string(), "Fangorn" };
		e.id = 3;
		e.weather = WEATHER_SNOW;
		e.temperature = -6.0f;
		e.lost[1030]["Peasant"] = 4;
		events.push_back(e);
	}
	{
		GC::Event e{ GC::Kind::Founded, 1200.0f, 1200.0f, Vector3(), { 1030 }, 0, "Village", "Ergor" };
		e.id = 4;
		events.push_back(e);
	}
	{
		GC::Event e{ GC::Kind::Destroyed, 1500.0f, 1500.0f, Vector3(), { 1027, 1030 }, 0, "Manor", "Nen Hithoel" };
		e.id = 5;
		e.weather = WEATHER_HEAVY_RAIN;
		e.mentions = { mention(GC::MENTION_TOP_KILLER, 4136, 1027, 3, false, "Ralph the Wolf"),
			mention(GC::MENTION_RAZED_BY, 4122, 1027, 1, false, "Olivier de Clermont") };
		events.push_back(e);
	}
	{
		GC::Event e{ GC::Kind::Fallen, 1900.0f, 1900.0f, Vector3(), { 1027 }, 0, "Savaric le Redoutable", "Duncraig" };
		e.id = 6;
		e.weather = WEATHER_BLIZZARD;
		e.temperature = -12.0f;
		e.mentions = { mention(GC::MENTION_FALLEN, 4211, 1027, 2, true, "Savaric le Redoutable", "froze to death") };
		events.push_back(e);
	}
	{
		GC::Event e{ GC::Kind::Fallen, 2100.0f, 2100.0f, Vector3(), { 1027, 1030 }, 0, "Hamund the Strong", std::string() };
		e.id = 7;
		e.mentions = { mention(GC::MENTION_FALLEN, 4207, 1027, 7, true, "Hamund the Strong", "slain by Ceolwulf Reeve") };
		events.push_back(e);
	}
	ChronicleNarrator::Context ctx;
	ctx.playerName = [](uint32_t id) { return id == 1027 ? std::string("Saint Jane") : std::string("the Baron"); };
	ctx.seasons = true;
	ctx.dayNight = true;
	ctx.seasonLengthS = 600.0f;
	ctx.dayLengthS = 300.0f;
	ctx.startSeason = 0;
	ctx.startHour = 8;
	auto noTemplate = [](const std::string& text) {
		return text.find('{') == std::string::npos && text.find('}') == std::string::npos && text.find("  ") == std::string::npos
			&& text.find(" a a") == std::string::npos && text.find(" a e") == std::string::npos && text.find(" a o") == std::string::npos
			&& text.find(" a i") == std::string::npos && text.find("..") == std::string::npos && text.find(",,") == std::string::npos
			&& text.find(" the the ") == std::string::npos && text.find("when,") == std::string::npos;
	};
	for (const auto& e : events) {
		const auto pieces = ChronicleNarrator::narrate(e, ctx);
		const std::string text = ChronicleNarrator::plain(pieces);
		printf("UNITTEST   [%s] %s\nUNITTEST     %s\n", ChronicleNarrator::dateLine(e.time, ctx).c_str(), e.title().c_str(), text.c_str());
		check(text.size() > 40 && noTemplate(text), std::string(GC::kindName(e.kind)) + ": a paragraph, no template left");
		check(text == ChronicleNarrator::plain(ChronicleNarrator::narrate(e, ctx)), std::string(GC::kindName(e.kind)) + ": the same words again");
		// every unit named as a link, the players' names as theirs
		std::set<int> linked;
		bool players = false;
		for (const auto& p : pieces) {
			if (p.mention >= 0)
				linked.insert(p.mention);
			players |= p.player != 0;
		}
		bool allNamed = true;
		for (size_t i = 0; i < e.mentions.size(); i++) {
			// (a hero present who also fell there or led the kills is named once, by that)
			bool elsewhere = false;
			for (size_t j = 0; j < e.mentions.size(); j++)
				elsewhere |= j != i && e.mentions[j].unit == e.mentions[i].unit && linked.count((int)j);
			allNamed &= linked.count((int)i) || elsewhere;
		}
		check(allNamed, std::string(GC::kindName(e.kind)) + ": every unit named, as a link (" + std::to_string(linked.size()) + ")");
		// (a hero's fall may name the hero alone)
		check(players || e.kind == GC::Kind::Fallen, std::string(GC::kindName(e.kind)) + ": the players named as theirs");
	}
	auto any = [&](const std::string& text, const std::string& word) {
		auto it = words.find(word);
		if (it == words.end())
			return false;
		for (const auto& alt : it->second) {
			const std::string a = alt.substr(0, alt.find('{'));
			if (!a.empty() && text.find(a) != std::string::npos)
				return true;
		}
		return false;
	};
	// the facts in the words
	const std::string battle = ChronicleNarrator::plain(ChronicleNarrator::narrate(events[0], ctx));
	check(battle.find("ten") != std::string::npos, "the battle's losses told");
	check(battle.find("Aelfred of Bamburgh") != std::string::npos && any(battle, "CAUSE.slain"), "the fallen hero's slayer told");
	check(any(battle, "WEATHER_ADJ.fog") || any(battle, "WEATHER_UNDER.fog") || battle.find("mist") != std::string::npos || battle.find("fog") != std::string::npos,
		"a foggy battle told as foggy");
	const std::string cold = ChronicleNarrator::plain(ChronicleNarrator::narrate(events[5], ctx));
	check(any(cold, "CAUSE.cold"), "a death by the cold told as such");
	const std::string siege = ChronicleNarrator::plain(ChronicleNarrator::narrate(events[1], ctx));
	check(siege.find("Militia") != std::string::npos && siege.find("fifty") == std::string::npos && siege.find("50") != std::string::npos,
		"the siege: the dead by type, the stronger side's numbers");
	// a clear night: no sun in it
	{
		int sunny = 0;
		for (uint32_t id = 1; id <= 40; id++) {
			GC::Event e = events[0];
			e.id = id;
			e.weather = WEATHER_CLEAR;
			e.time = 300.0f * 4 + 300.0f * 0.6f; // 300 s days from 8 o'clock: 22:24
			const std::string t = ChronicleNarrator::plain(ChronicleNarrator::narrate(e, ctx));
			bool sun = false;
			for (size_t at = t.find("sun"); at != std::string::npos; at = t.find("sun", at + 1))
				sun |= (at == 0 || !std::isalpha((unsigned char)t[at - 1])) && (at + 3 >= t.size() || t[at + 3] != 'g');
			if (sun || t.find("Sun") != std::string::npos)
				sunny++, printf("UNITTEST   sunny night: %s\n", t.c_str());
		}
		check(sunny == 0, "a clear night's battles never mention the sun (" + std::to_string(sunny) + " of 40)");
	}
	// the wording changes from event to event
	{
		std::set<std::string> openings, wholes;
		for (uint32_t id = 1; id <= 40; id++) {
			GC::Event e = events[0];
			e.id = id;
			const std::string t = ChronicleNarrator::plain(ChronicleNarrator::narrate(e, ctx));
			openings.insert(t.substr(0, t.find('.')));
			wholes.insert(t);
			check(noTemplate(t), "battle " + std::to_string(id) + ": no template left");
		}
		check(openings.size() >= 30 && wholes.size() == 40, "40 battles: " + std::to_string(openings.size()) + " different openings, " + std::to_string(wholes.size()) + " different paragraphs");
	}
	// the calendar: 600 s seasons from spring; 300 s days from 8 o'clock
	check(ChronicleNarrator::dateLine(300.0f, ctx).rfind("Spring, the year 1000", 0) == 0, "300 s: " + ChronicleNarrator::dateLine(300.0f, ctx));
	check(ChronicleNarrator::dateLine(1900.0f, ctx).rfind("Winter, the year 1000", 0) == 0, "1900 s: " + ChronicleNarrator::dateLine(1900.0f, ctx));
	check(ChronicleNarrator::dateLine(2500.0f, ctx).rfind("Spring, the year 1001", 0) == 0, "2500 s: " + ChronicleNarrator::dateLine(2500.0f, ctx));
	ChronicleNarrator::Context days = ctx;
	days.seasons = false;
	check(ChronicleNarrator::dateLine(700.0f, days).rfind("Day 3", 0) == 0, "days only, 700 s: " + ChronicleNarrator::dateLine(700.0f, days));
	ChronicleNarrator::Context none = ctx;
	none.seasons = none.dayNight = false;
	check(ChronicleNarrator::dateLine(65.0f, none) == "1:05", "no calendar: the clock (" + ChronicleNarrator::dateLine(65.0f, none) + ")");
	{
		GC::Event e = events[0];
		e.weather = -1;
		check(any(ChronicleNarrator::plain(ChronicleNarrator::narrate(e, none)), "WAR_STAGE.early"), "no calendar, no weather: the war's progress opens it");
	}
	printf(failures ? "UNITTEST FAILED (%d)\n" : "UNITTEST PASSED\n", failures);
}

// wkbai's chronicle over every season, the weathers each can have, the six times of day, every
// kind of event, sizes, outcomes and causes; also without a calendar and with days only. Every
// paragraph is printed ("STORY season|weather|time|kind|text") for the RE repo's checker, and the
// plain rules are checked here: no template left, a capital after each full stop, no "a" before
// a vowel, a full stop at the end
void Test_ChronicleSweep()
{
	int failures = 0, bad = 0, total = 0;
	auto check = [&](bool ok, const std::string& what) {
		if (!ok) failures++;
		printf("UNITTEST %s: %s\n", ok ? "ok" : "FAILED", what.c_str());
		fflush(stdout);
	};
	using GC = GameChronicle;
	check(ChronicleNarrator::load(), "redata/chronicle/english.txt read");
	auto mention = [](int kind, uint32_t unit, uint32_t player, int kills, bool dead, const std::string& name, const std::string& extra = std::string()) {
		GC::Mention m;
		m.kind = kind; m.unit = unit; m.player = player; m.kills = kills; m.dead = dead; m.name = name; m.extra = extra;
		return m;
	};
	// the weathers each season can have (ServerEnvironment's WEATHER_WEIGHTS above 0)
	const std::vector<std::vector<int>> seasonWeathers = {
		{ WEATHER_CLEAR, WEATHER_CLOUDY, WEATHER_RAIN, WEATHER_HEAVY_RAIN, WEATHER_THUNDERSTORM, WEATHER_FOG, WEATHER_SNOW },
		{ WEATHER_CLEAR, WEATHER_CLOUDY, WEATHER_RAIN, WEATHER_HEAVY_RAIN, WEATHER_THUNDERSTORM, WEATHER_FOG, WEATHER_HEATWAVE, WEATHER_DROUGHT },
		{ WEATHER_CLEAR, WEATHER_CLOUDY, WEATHER_RAIN, WEATHER_HEAVY_RAIN, WEATHER_THUNDERSTORM, WEATHER_FOG, WEATHER_SNOW },
		{ WEATHER_CLEAR, WEATHER_CLOUDY, WEATHER_RAIN, WEATHER_FOG, WEATHER_SNOW, WEATHER_BLIZZARD } };
	const float seasonTemps[4] = { 10.0f, 24.0f, 9.0f, -4.0f };
	static const char* seasonNames[] = { "spring", "summer", "autumn", "winter" };
	const float hours[6] = { 6.0f, 9.0f, 12.5f, 16.0f, 19.5f, 1.0f };
	static const char* hourNames[] = { "dawn", "morning", "noon", "afternoon", "dusk", "night" };
	static const char* causes[] = { "slain by Ceolwulf Reeve", "froze to death", "died of thirst", "drowned", "went through the ice", "burned to death",
		"struck by lightning", "fell", "slain by the Baron's Trebuchet" };
	ChronicleNarrator::Context ctx;
	// (1040 the barbarians' player, 1026 a player without a name: the map's own villages)
	ctx.playerName = [](uint32_t id) { return id == 1027 ? std::string("Saint Jane") : id == 1030 ? std::string("the Baron") : id == 1040 ? std::string("Baresark Player") : std::string(); };
	ctx.seasons = ctx.dayNight = true;
	ctx.seasonLengthS = 3000.0f;
	ctx.dayLengthS = 300.0f;
	ctx.startHour = 8;
	// an event of the kind k (0..10) with sizes, losses and mentions by the index i
	uint32_t nextId = 1;
	auto make = [&](int k, int i, float time) {
		GC::Event e{ GC::Kind::Battle, time, time + (i % 3 == 0 ? 10.0f : i % 3 == 1 ? 120.0f : 400.0f), Vector3(), { 1027, 1030 }, 0, std::string(), (i % 7 == 0) ? std::string() : "Nen Hithoel" };
		e.id = nextId++;
		const int sizes[] = { 5, 15, 40, 90 };
		const int size = sizes[i % 4];
		auto losses = [&](int a, int b) {
			if (a) e.lost[1027][i % 2 ? "Spearman" : "Archer"] = a;
			if (b) { e.lost[1030]["Militia"] = b; if (i % 5 == 0) e.lost[1030]["Peasant"] = 1; }
		};
		switch (k) {
		case 0: case 1: case 2: // battles: a win, without loss, even; none lost
			e.size = size;
			e.strength = { { 1027, size * (i % 2 ? 3 : 1) / 2 + 1 }, { 1030, size / 2 + 1 } };
			if (k == 0) losses(2, 9 + i % 5);
			if (k == 1) losses(0, 4 + i % 3);
			if (k == 2 && i % 2) losses(5, 6);
			if (k == 2 && i % 4 == 0) losses(0, 1); // (one side lost one, the other none)
			e.mentions = { mention(GC::MENTION_TOP_KILLER, 10, 1027, 1 + i % 4, i % 3 == 0, "Ralph the Wolf"),
				mention(GC::MENTION_PRESENT, 11, 1027, 3 + i % 2, false, "Gaut Raven-Feeder") };
			if (i % 2)
				e.mentions.push_back(mention(GC::MENTION_PRESENT, 12, 1030, 7, true, "Ulrich von Hagen"));
			if (i % 3 == 1)
				e.mentions.push_back(mention(GC::MENTION_FALLEN, 13, 1030, 4, true, "Bertrand le Hardi", causes[i % 9]));
			break;
		case 3: // a battle of three sides
			e.players = { 1027, 1030, 1040 };
			e.size = size;
			e.lost[1040]["Knight"] = 3;
			losses(1, 2);
			break;
		case 4: // a battle of one side (against the wild)
			e.players = { 1027 };
			e.size = size;
			if (i % 2) losses(2, 0);
			break;
		case 5: // a siege
			e.kind = GC::Kind::Siege;
			e.size = size;
			losses(3, 12);
			e.mentions = { mention(GC::MENTION_TOP_KILLER, 10, 1030, 5, false, "Ulrich von Hagen") };
			// (a unit known by its type only, as the server names one: "unnamed")
			if (i % 2)
				e.mentions.push_back(mention(GC::MENTION_TOP_KILLER, 14, 1027, 1 + i % 3, i % 4 == 1, "Gibbering Horde", "unnamed"));
			if (i % 3 == 0)
				e.mentions.push_back(mention(GC::MENTION_PRESENT, 15, 1027, 2, true, "Trebuchet", "unnamed"));
			break;
		case 6: // a raid, with and without a victim
			e.kind = GC::Kind::Raid;
			e.size = 1 + i % 8;
			if (i % 2)
				e.lost[1030]["Peasant"] = 3 + i % 3;
			break;
		case 7:
			e.kind = GC::Kind::Founded;
			e.players = { 1030 };
			e.what = i % 3 == 0 ? "Village Wood" : i % 3 == 1 ? "Village Stone" : "Abbey";
			break;
		case 8: // destroyed, with and without razers and a fight
			e.kind = GC::Kind::Destroyed;
			e.what = i % 2 ? "Manor" : "Village Wood";
			if (i % 4 == 0)
				e.players = { 1026, 1027 };
			if (i % 3)
				e.mentions.push_back(mention(GC::MENTION_RAZED_BY, 20, 1027, 2, false, i % 2 ? "Olivier de Clermont" : "Trebuchet"));
			if (i % 3 == 2)
				e.mentions.push_back(mention(GC::MENTION_RAZED_BY, 21, 1027, 1, false, "Eadric Shieldbreaker"));
			if (i % 2)
				e.mentions.push_back(mention(GC::MENTION_TOP_KILLER, 10, 1027, 3, false, "Ralph the Wolf"));
			break;
		case 9: // a hero's fall away from battle, every cause
			e.kind = GC::Kind::Fallen;
			e.players = { 1027 };
			e.what = "Savaric le Redoutable";
			e.mentions = { mention(GC::MENTION_FALLEN, 30, 1027, i % 4 == 0 ? 1 : 2 + i % 6, true, "Savaric le Redoutable", causes[i % 9]) };
			break;
		case 10: // a fall with no mention
			e.kind = GC::Kind::Fallen;
			e.players = { 1027 };
			break;
		}
		return e;
	};
	auto rules = [&](const std::string& text, std::string& why) {
		if (text.find('{') != std::string::npos || text.find('}') != std::string::npos) why += " template";
		if (text.find("  ") != std::string::npos) why += " double-space";
		if (text.find(",,") != std::string::npos || text.find(" ,") != std::string::npos || text.find(",.") != std::string::npos || text.find("..") != std::string::npos) why += " punctuation";
		if (text.empty() || (text.back() != '.' && text.back() != '!')) why += " no-full-stop";
		if (!text.empty() && std::islower((unsigned char)text[0])) why += " lower-start";
		for (size_t at = text.find(". "); at != std::string::npos; at = text.find(". ", at + 1))
			if (at + 2 < text.size() && std::islower((unsigned char)text[at + 2]))
				why += " lower-after-stop";
		for (size_t at = 0; (at = text.find(" a ", at)) != std::string::npos; at++)
			if (at + 3 < text.size() && std::strchr("aeiouAEIOU", text[at + 3]) && text.compare(at + 3, 3, "one") && text.compare(at + 3, 2, "us") && text.compare(at + 3, 3, "uni"))
				why += " a-vowel";
		if (text.find("when,") != std::string::npos || text.find("that,") != std::string::npos) why += " when-comma";
	};
	auto tell = [&](const GC::Event& e, const ChronicleNarrator::Context& c, const std::string& label) {
		const std::string text = ChronicleNarrator::plain(ChronicleNarrator::narrate(e, c));
		std::string why;
		rules(text, why);
		total++;
		if (!why.empty()) {
			bad++;
			printf("UNITTEST   BAD%s: %s\n", why.c_str(), text.c_str());
		}
		printf("STORY %s|%s|%s\n", label.c_str(), GC::kindName(e.kind), text.c_str());
	};
	for (int s = 0; s < 4; s++)
		for (int w : seasonWeathers[s])
			for (int h = 0; h < 6; h++)
				for (int k = 0; k <= 10; k++) {
					const int i = (int)nextId;
					// in season s of the second year, day 3, at the hour
					const float time = (4 + s) * ctx.seasonLengthS + 2 * ctx.dayLengthS + std::fmod(hours[h] - 8.0f + 24.0f, 24.0f) / 24.0f * ctx.dayLengthS;
					GC::Event e = make(k, i, time);
					e.weather = w;
					e.temperature = w == WEATHER_BLIZZARD ? -12.0f : w == WEATHER_HEATWAVE ? 36.0f : seasonTemps[s] + (i % 3 - 1) * 3.0f;
					e.snowCover = s == 3 || w == WEATHER_SNOW ? 0.6f : 0.0f;
					tell(e, ctx, std::string(seasonNames[s]) + "|" + WeatherName(w) + "|" + hourNames[h]);
				}
	// no weather known; days only; no calendar at all (each stage of the war)
	ChronicleNarrator::Context daysOnly = ctx, none = ctx;
	daysOnly.seasons = false;
	none.seasons = none.dayNight = false;
	for (int k = 0; k <= 10; k++)
		for (float time : { 30.0f, 400.0f, 1200.0f, 4000.0f }) {
			GC::Event e = make(k, (int)nextId, time);
			tell(e, ctx, "season-no-weather|-|-");
			e = make(k, (int)nextId, time);
			tell(e, daysOnly, "days|-|-");
			e = make(k, (int)nextId, time);
			e.weather = WEATHER_FOG;
			tell(e, daysOnly, "days|fog|-");
			e = make(k, (int)nextId, time);
			tell(e, none, "none|-|-");
			e = make(k, (int)nextId, time);
			e.weather = WEATHER_RAIN;
			tell(e, none, "none|rain|-");
		}
	check(bad == 0, std::to_string(total) + " paragraphs, " + std::to_string(bad) + " breaking the plain rules");
	printf(failures ? "UNITTEST FAILED (%d)\n" : "UNITTEST PASSED\n", failures);
}

// The key bindings (interface/KeyBindings.cpp): a key given to another action fires it and the
// old key no longer does; Ctrl+digit and the digit alone are two actions; held keys ignore extra
// modifiers; conflicts only where both actions work; the bindings through wkconfig.json and
// back; the cheat keys only with the cheat gate open, whatever key they have, and that gate
// being ClientInterface::cheatKeysAllowed
void Test_KeyBindings()
{
	int failures = 0;
	auto check = [&](bool ok, const std::string& what) {
		if (!ok) failures++;
		printf("UNITTEST %s: %s\n", ok ? "ok" : "FAILED", what.c_str());
		fflush(stdout);
	};
	using A = KeyAction;
	auto clearKeys = []() {
		std::fill(std::begin(g_keyDown), std::end(g_keyDown), false);
		std::fill(std::begin(g_keyPressed), std::end(g_keyPressed), false);
		g_modCtrl = g_modShift = g_modAlt = false;
	};
	auto press = [&](SDL_Scancode sc, bool ctrl = false, bool shift = false) {
		clearKeys();
		g_keyPressed[sc] = g_keyDown[sc] = true;
		g_modCtrl = ctrl;
		g_modShift = shift;
	};
	ResetAllKeyBindings();

	// every default key as text and back
	bool roundTrip = true;
	std::set<std::string> ids;
	for (const KeyActionInfo& info : GetKeyActions())
		for (int s = 0; s < KEY_SLOTS; s++) {
			ids.insert(info.id);
			const KeyChord c = info.defaults[s];
			if (KeyChord::parse(c.text()) != c) {
				roundTrip = false;
				printf("  %s slot %d: \"%s\" read back differently\n", info.id, s, c.text().c_str());
			}
		}
	check(roundTrip, "every default key written as text and read back the same (" + std::to_string(GetKeyActions().size()) + " actions)");
	check(ids.size() == GetKeyActions().size() && GetKeyActions().size() == (size_t)A::COUNT, "every action in the table once, with an id of its own");
	check(KeyChord::parse("Ctrl+F12") == KeyChord{ SDL_SCANCODE_F12, KeyChord::CTRL } && KeyChord::parse("Keypad 0").key == SDL_SCANCODE_KP_0
		&& KeyChord::parse("nonsense").empty(), "\"Ctrl+F12\", \"Keypad 0\" read; an unknown name gives no key");

	// rebinding: Pause from P to K
	press(SDL_SCANCODE_P);
	check(KeyPressed(A::Pause), "P pauses by default");
	SetKeyBinding(A::Pause, 0, KeyChord{ SDL_SCANCODE_K, 0 });
	press(SDL_SCANCODE_K);
	check(KeyPressed(A::Pause), "Pause bound to K: K pauses");
	press(SDL_SCANCODE_P);
	check(!KeyPressed(A::Pause), "Pause bound to K: P no longer does");
	check(KeyPressed(A::ReplayPause), "P still pauses a replay (its own action)");
	press(SDL_SCANCODE_K, true);
	check(!KeyPressed(A::Pause), "Ctrl+K isn't K");
	// a held key: Scroll forward moved from W/Up to I
	SetKeyBinding(A::CameraForward, 1, KeyChord{ SDL_SCANCODE_I, 0 });
	press(SDL_SCANCODE_I, false, true);
	check(KeyDown(A::CameraForward), "Scroll forward on I: held with Shift (faster) it still scrolls");
	press(SDL_SCANCODE_W);
	check(!KeyDown(A::CameraForward), "Scroll forward on I: W no longer scrolls");
	press(SDL_SCANCODE_LSHIFT);
	check(KeyDown(A::CameraFast), "Left Shift alone held: scroll faster");

	// control groups: Ctrl+3 stores, 3 selects; keypad 3 the same
	press(SDL_SCANCODE_3, true);
	check(KeyPressed(A::GroupStore3) && !KeyPressed(A::GroupSelect3), "Ctrl+3 stores group 3, doesn't select it");
	press(SDL_SCANCODE_3);
	check(KeyPressed(A::GroupSelect3) && !KeyPressed(A::GroupStore3), "3 selects group 3");
	press(SDL_SCANCODE_KP_3, true);
	check(KeyPressed(A::GroupStore3), "Ctrl+keypad 3 stores group 3");
	press(SDL_SCANCODE_0);
	check(KeyPressed(A::GroupSelect0) && !KeyPressed(A::CameraRotate), "0 selects group 0 (keypad 0 turns the camera)");

	// conflicts: only where both work
	auto conflictsWith = [](A a, int slot, KeyChord c, A other) {
		for (const KeyConflict& k : FindKeyConflicts(a, slot, c))
			if (k.action == other) return true;
		return false;
	};
	check(conflictsWith(A::Pause, 0, KeyChord{ SDL_SCANCODE_SPACE, 0 }, A::CameraToSelection), "Space for Pause clashes with Jump to the selection");
	check(!conflictsWith(A::Pause, 0, KeyChord{ SDL_SCANCODE_L, 0 }, A::ReplayLog), "L for Pause doesn't clash with the replay's L (not in a game)");
	check(conflictsWith(A::EditorGrab, 0, KeyChord{ SDL_SCANCODE_E, 0 }, A::NextCommand), "E for the editor's Move clashes with Next command (the editor is in a game)");
	// (K selects economic buildings since the original's hot keys: Q is the free letter)
	check(FindKeyConflicts(A::Pause, 0, KeyChord{ SDL_SCANCODE_Q, 0 }).empty(), "Q for Pause clashes with nothing");
	check(!conflictsWith(A::GroupSelect3, 0, KeyChord{ SDL_SCANCODE_3, KeyChord::CTRL }, A::GroupSelect3) && conflictsWith(A::GroupSelect3, 0, KeyChord{ SDL_SCANCODE_3, KeyChord::CTRL }, A::GroupStore3),
		"Ctrl+3 for Select group 3 clashes with Store group 3");

	// through wkconfig.json: only the changed actions written; read back as they were
	SaveKeyBindings();
	const nlohmann::json written = g_settings.value("keyBindings", nlohmann::json::object());
	check(written.size() == 2 && written.contains("pause") && written.contains("cameraForward"), "keyBindings holds the two changed actions only: " + written.dump());
	SaveSettings();
	ResetAllKeyBindings();
	check(GetKeyBinding(A::Pause, 0).key == SDL_SCANCODE_P, "reset to defaults: Pause on P again");
	LoadSettings();
	LoadKeyBindings();
	check(GetKeyBinding(A::Pause, 0) == KeyChord{ SDL_SCANCODE_K, 0 } && GetKeyBinding(A::Pause, 1).empty(), "after saving and loading wkconfig.json: Pause on K");
	check(GetKeyBinding(A::CameraForward, 0).key == SDL_SCANCODE_UP && GetKeyBinding(A::CameraForward, 1).key == SDL_SCANCODE_I, "after saving and loading: Scroll forward on Up and I");
	check(KeyBindingIsDefault(A::Chat) && GetKeyBinding(A::FrameTimings, 0) == KeyChord{ SDL_SCANCODE_F12, KeyChord::CTRL }, "after saving and loading: the others as they were");
	press(SDL_SCANCODE_K);
	check(KeyPressed(A::Pause), "after loading: K pauses");
	press(SDL_SCANCODE_P);
	check(!KeyPressed(A::Pause), "after loading: P doesn't");

	// the cheat keys: shut without a gate whatever key they are on
	SetKeyBinding(A::Debugger, 0, KeyChord{ SDL_SCANCODE_J, 0 });
	SetKeyCheatGate(nullptr);
	press(SDL_SCANCODE_J);
	check(!KeyPressed(A::Debugger) && KeyPressedUngated(A::Debugger), "Debugger on J: no cheats, J does nothing (closing works: ungated)");
	press(SDL_SCANCODE_F2);
	check(!KeyPressed(A::Debugger), "Debugger on J: F2 does nothing either");
	bool gateOpen = false;
	SetKeyCheatGate([&]() { return gateOpen; });
	press(SDL_SCANCODE_J);
	check(!KeyPressed(A::Debugger), "cheat gate shut: J does nothing");
	gateOpen = true;
	check(KeyPressed(A::Debugger), "cheat gate open: J opens the debugger");
	SetKeyBinding(A::ServerWindow, 0, KeyChord{ SDL_SCANCODE_LALT, 0 });
	press(SDL_SCANCODE_LALT);
	check(KeyPressed(A::ServerWindow), "cheat gate open: the server window on Left Alt alone");

	// the gate in a game: ClientInterface::cheatKeysAllowed (single player, a skirmish's cheat mode)
	{
		LoadBCP("data.bcp");
		Server server;
		Client client(true);
		ClientInterface cliui(&client, nullptr);
		cliui.withServer = [&](const std::function<void(Server&)>& f) { f(server); };
		cliui.singlePlayer = true;
		SetKeyCheatGate([&]() { return cliui.cheatKeysAllowed(); });
		press(SDL_SCANCODE_J);
		server.cheatMode = false;
		check(!KeyPressed(A::Debugger), "single player without cheat mode: the debugger's key (J) does nothing");
		server.cheatMode = true;
		check(KeyPressed(A::Debugger), "single player with cheat mode: J opens the debugger");
		cliui.singlePlayer = false;
		check(!KeyPressed(A::Debugger), "multiplayer, cheat mode or not: J does nothing");
		press(SDL_SCANCODE_F9);
		cliui.singlePlayer = true;
		server.cheatMode = false;
		check(!KeyPressed(A::EnvironmentPanel), "F9 (the environment panel) without cheat mode: nothing");
	}
	check(!KeyCheatsAllowed(), "the ClientInterface gone: its gate let go, no cheats");

	// back to the defaults, in wkconfig.json too
	ResetAllKeyBindings();
	SaveKeyBindings();
	check(!g_settings.contains("keyBindings"), "all defaults: no keyBindings in wkconfig.json");
	SaveSettings();
	clearKeys();
	printf(failures ? "UNITTEST FAILED (%d)\n" : "UNITTEST PASSED\n", failures);
}

// Game over view (Server::finishedPlayers, REMOVE_MULTIPLAYER_PLAYER): 4 Peaks with two clients
// over local links, one for each player. Player A's game set loss (Alive Multiplayer Player 0,
// REMOVE_MULTIPLAYER_PLAYER) while B still fights: A's client has no fog of war and shows
// everyone in its statistics and timeline, B's keeps both; what A says reaches no one still
// playing. Then the game set's own ending: A's Manor destroyed ("End Game On Destruction"), A
// lost, B the last one alive and won ("A Player Has Won the Multiplayer Game"). A save keeps it.
void Test_GameOverView()
{
	int failures = 0;
	auto check = [&](bool ok, const std::string& what) {
		if (!ok) failures++;
		printf("UNITTEST %s: %s\n", ok ? "ok" : "FAILED", what.c_str());
		fflush(stdout);
	};
	LoadBCP("data.bcp");
	g_settings["simSeed"] = 7;
	const uint32_t idA = 1027, idB = 1030;
	const std::string saveName = "Save_Games\\wkbai game over view.sav";
	const std::string savePath = PathToUtf8(g_userPath / "saved" / PathFromUtf8(saveName));
	{
		Server server;
		Server::clearFogOfWar = false;
		Client clientA(true), clientB(true);
		NetLocalBuffer a1, a2, b1, b2;
		NetLocalLink srvA(&a1, &a2), cliA(&a2, &a1), srvB(&b1, &b2), cliB(&b2, &b1);
		server.addClient(&srvA);
		server.addClient(&srvB);
		clientA.serverLink = &cliA;
		clientB.serverLink = &cliB;
		server.loadSaveGame("Save_Games\\2 Player 4 Peaks.lvl");
		server.setClientPlayerControl(0, idA);
		server.setClientPlayerControl(1, idB);
		ServerGameObject* playerA = server.findObject(idA);
		ServerGameObject* playerB = server.findObject(idB);
		const int alive = server.gameSet->items.names.getIndex("Alive Multiplayer Player");
		const int palace = server.gameSet->items.names.getIndex("Palace Building");
		if (!playerA || !playerB || alive < 0 || palace < 0) {
			printf("UNITTEST not set up\n");
			printf("UNITTEST FAILED (1)\n");
			return;
		}
		ClientInterface uiA(&clientA, nullptr), uiB(&clientB, nullptr);
		server.timeManager.unpause();
		server.startLevel();
		auto step = [&](int frames) {
			for (int i = 0; i < frames; i++) {
				server.tick();
				clientA.tick();
				clientB.tick();
				SDL_Delay(5);
			}
		};
		step(60);
		printf("UNITTEST alive items: A %.0f, B %.0f; client players A %u, B %u\n", playerA->getItem(alive), playerB->getItem(alive),
			clientA.clientPlayer ? clientA.clientPlayer->id : 0, clientB.clientPlayer ? clientB.clientPlayer->id : 0);
		check(uiA.fogOfWarOn() && uiB.fogOfWarOn(), "at the start both clients have the fog of war");
		check(!uiA.showsEveryone() && !uiB.showsEveryone(), "at the start both show their own side's statistics only");

		// A beaten as the game set does it, B still fighting (no win event sent)
		{
			SrvScriptContext ctx(&server, playerA);
			for (const char* line : { "SET_ITEM \"Alive Multiplayer Player\" FINDER_SELF CONSTANT 0", "REMOVE_MULTIPLAYER_PLAYER FINDER_SELF" }) {
				GSFileParser gsf(line);
				std::unique_ptr<Action> act(ReadAction(gsf, *server.gameSet));
				act->run(&ctx);
			}
		}
		step(20);
		check(server.finishedPlayers.count(idA) && server.finishedPlayers[idA] == 2, "the server notes A as lost");
		check(clientA.ownOutcome() == 2 && clientB.ownOutcome() == 0, "client A knows it lost, client B still plays");
		check(!uiA.fogOfWarOn(), "A, beaten and still in the game: no fog of war");
		check(uiA.showsEveryone(), "A: every player's statistics and every event");
		check(uiB.fogOfWarOn(), "B, still fighting: keeps its fog of war");
		check(!uiB.showsEveryone(), "B: still its own side's statistics only");
		check(server.watchesOnly(0) && !server.watchesOnly(1), "A only watches while B plays");
		{
			ClientGameObject* seenA = clientB.findObject(idA);
			check((playerA->flags & CommonGameObject::fPlayerTerminated) && !(playerB->flags & CommonGameObject::fPlayerTerminated),
				"A PLAYER_TERMINATED (WKB.exe's REMOVE_MULTIPLAYER_PLAYER: its AI gone), B not");
			check(seenA && (seenA->flags & CommonGameObject::fPlayerTerminated), "client B knows A's flag");
		}
		// A's chat: to A only (B still plays)
		clientA.chatMessages.clear();
		clientB.chatMessages.clear();
		clientA.sendMessage("I see your army");
		clientB.sendMessage("still here");
		step(20);
		auto heard = [](Client& c, const char* text) { return std::find(c.chatMessages.begin(), c.chatMessages.end(), text) != c.chatMessages.end(); };
		check(heard(clientA, "I see your army") && !heard(clientB, "I see your army"), "what the beaten A says doesn't reach B, still playing");
		check(heard(clientA, "still here") && heard(clientB, "still here"), "what B says reaches both");

		// a save made now keeps it
		server.timeManager.pause();
		check(server.saveGame(savePath.c_str()), "saved after the loss");
		server.timeManager.unpause();

		// the game set's own ending: A's Manor destroyed; B the last one alive wins
		server.finishedPlayers.clear();
		server.sendFinishedPlayers();
		playerA->setItem(alive, 1.0f);
		playerA->updateFlags(playerA->flags & ~CommonGameObject::fPlayerTerminated);
		step(10);
		check(clientA.ownOutcome() == 0 && uiA.fogOfWarOn(), "the list cleared: A has its fog again");
		ServerGameObject* manor = nullptr;
		auto walk = [&](CommonGameObject* obj, auto& self) -> void {
			for (const auto& [type, children] : obj->children)
				for (CommonGameObject* child : children) {
					if (!manor && child->blueprint->bpClass == Tags::GAMEOBJCLASS_BUILDING && child->getItem(palace) > 0.0f)
						manor = child->dyncast<ServerGameObject>();
					self(child, self);
				}
		};
		walk(playerA, walk);
		check(manor != nullptr, "A has a Palace Building (its Manor)");
		if (manor) {
			printf("UNITTEST destroying A's %s %u\n", manor->blueprint->name.c_str(), manor->id);
			manor->terminate();
		}
		step(60);
		printf("UNITTEST after the Manor: A %d, B %d (server), alive items A %.0f B %.0f\n", server.finishedPlayers.count(idA) ? server.finishedPlayers[idA] : 0,
			server.finishedPlayers.count(idB) ? server.finishedPlayers[idB] : 0, playerA->getItem(alive), playerB->getItem(alive));
		check(clientA.ownOutcome() == 2, "the Manor destroyed: A lost (Game Endings.cpp)");
		check(clientB.ownOutcome() == 1, "B, the last one alive: won");
		check(!uiA.fogOfWarOn() && !uiB.fogOfWarOn(), "both, the game over: no fog of war");
		check(uiA.showsEveryone() && uiB.showsEveryone(), "both show every player's statistics");
		check(!server.watchesOnly(0) && !server.watchesOnly(1), "nobody plays on: nobody's chat held back");
		// the Manor box's Exit to Main Menu (EXIT_TO_MAIN_MENU), WKB.exe 0x483C50: to the clicking
		// player's own client only. The guest B leaves alone; the host's A ends the hosted game.
		auto exitFor = [&](ServerGameObject* p) {
			SrvScriptContext ctx(&server, p);
			GSFileParser gsf("EXIT_TO_MAIN_MENU");
			std::unique_ptr<Action> act(ReadAction(gsf, *server.gameSet));
			act->run(&ctx);
			step(10);
		};
		exitFor(playerB);
		check(clientB.exitToMenu && !clientA.exitToMenu && !server.exitToMenu, "the guest's Exit: only the guest's client leaves, the host plays on");
		exitFor(playerA);
		check(server.exitToMenu && !clientA.exitToMenu, "the host's Exit: the hosted game ends");
		server.exitToMenu = false;
	}
	// the save from after A's loss: loaded with a client for A
	{
		Server server;
		Client client(true);
		NetLocalBuffer q1, q2;
		NetLocalLink srv(&q1, &q2), cli(&q2, &q1);
		server.addClient(&srv);
		client.serverLink = &cli;
		server.loadSaveGame(saveName.c_str());
		server.setClientPlayerControl(0, idA);
		ClientInterface ui(&client, nullptr);
		for (int i = 0; i < 40; i++) {
			server.tick();
			client.tick();
			SDL_Delay(5);
		}
		check(server.finishedPlayers.count(idA) && server.finishedPlayers[idA] == 2 && !server.finishedPlayers.count(idB), "the save keeps A lost, B playing");
		check(client.ownOutcome() == 2 && !ui.fogOfWarOn() && ui.showsEveryone(), "loaded: A's client without fog, every player shown");
		ServerGameObject* loadedA = server.findObject(idA);
		check(loadedA && (loadedA->flags & CommonGameObject::fPlayerTerminated), "loaded: A still PLAYER_TERMINATED");
	}
	{
		Server server;
		server.loadSaveGame("Save_Games\\2 Player 4 Peaks.lvl");
		check(server.finishedPlayers.empty(), "a new game: nobody finished");
	}
	// King of the Hill (Valhalla's Victory Point Limit, Valhalla Win Condition.cpp): the winner's
	// Victory Timer task, run on one of its buildings, as the game set does when its Victory Time
	// reaches the limit. Once with each player winning (the game set's finder takes them in id
	// order, so both orders are covered): the winner won, without the defeat box or a second win
	// box; the other lost, with the defeat box.
	// (winner 0: Valhalla's other ending, A's last unit dead, "I am the Last Unit to Die": the
	// patched reactions as before, B the last one alive with the armies' win box)
	for (const uint32_t winnerId : { idB, idA, 0u }) {
		const bool armies = winnerId == 0;
		const uint32_t loserId = armies ? idA : winnerId == idA ? idB : idA;
		Server server;
		Client clientA(true), clientB(true);
		NetLocalBuffer a1, a2, b1, b2;
		NetLocalLink srvA(&a1, &a2), cliA(&a2, &a1), srvB(&b1, &b2), cliB(&b2, &b1);
		server.addClient(&srvA);
		server.addClient(&srvB);
		clientA.serverLink = &cliA;
		clientB.serverLink = &cliB;
		server.loadSaveGame("Save_Games\\2 Player 4 Peaks.lvl");
		server.setClientPlayerControl(0, idA);
		server.setClientPlayerControl(1, idB);
		const GameSet& gs = *server.gameSet;
		ServerGameObject* winner = server.findObject(armies ? idB : winnerId);
		ServerGameObject* loser = server.findObject(loserId);
		const int limit = gs.items.names.getIndex("Victory Time Limit"), time = gs.items.names.getIndex("Victory Time");
		ServerGameObject* hill = nullptr;
		auto walk = [&](CommonGameObject* obj, auto& self) -> void {
			for (const auto& [type, children] : obj->children)
				for (CommonGameObject* child : children) {
					if (!hill && child->blueprint->bpClass == Tags::GAMEOBJCLASS_BUILDING)
						hill = child->dyncast<ServerGameObject>();
					self(child, self);
				}
		};
		walk(winner, walk);
		if (!winner || !loser || limit < 0 || time < 0 || !hill) {
			check(false, "King of the Hill: set up");
			continue;
		}
		for (ServerGameObject* p : { winner, loser })
			p->setItem(limit, 1000.0f);
		winner->setItem(time, armies ? 300.0f : 1000.0f);
		loser->setItem(time, 300.0f);
		server.timeManager.unpause();
		server.startLevel();
		auto step = [&](int frames) {
			for (int i = 0; i < frames; i++) {
				server.tick();
				clientA.tick();
				clientB.tick();
				SDL_Delay(5);
			}
		};
		step(30);
		if (armies) {
			SrvScriptContext ctx(&server, loser);
			GSFileParser gsf("SEND_EVENT \"I am the Last Unit to Die\" FINDER_SELF\n");
			std::unique_ptr<Action> act(ReadAction(gsf, gs));
			act->run(&ctx);
		}
		else {
			SrvScriptContext ctx(&server, hill);
			// the block as the task has it (UPON_CONDITION reads up to its END_UPON_CONDITION)
			GSFileParser gsf("UPON_CONDITION EQUATION_RESULT \"Player Has Won the Game\" FINDER_SELF\n"
				"ACTION SET_ITEM \"Alive Multiplayer Player\" FINDER_PLAYER CONSTANT 1\n"
				"ACTION SEND_EVENT \"You have lost the Valhalla Game\" FINDER_RESULTS \"Alive Multiplayer Players\"\n"
				"ACTION DISPLAY_GAME_TEXT_WINDOW \"VALHALLA_WIN\" FINDER_PLAYER\n"
				"END_UPON_CONDITION\n");
			std::unique_ptr<Action> act(ReadAction(gsf, gs));
			act->run(&ctx);
		}
		step(30);
		Client& wc = winner->id == idA ? clientA : clientB;
		Client& lc = winner->id == idA ? clientB : clientA;
		auto shown = [&](Client& c, const char* name) { const int x = gs.gameTextWindows.names.getIndex(name); return x >= 0 && c.gtwStates.count(x) > 0; };
		const std::string who = armies ? std::string("Valhalla, A's armies destroyed: ") : "King of the Hill, player " + std::to_string(winnerId) + " winning: ";
		printf("UNITTEST %swinner %d, loser %d; winner's boxes: win %d, armies win %d, defeat %d; loser's defeat box %d\n", who.c_str(), wc.ownOutcome(), lc.ownOutcome(),
			(int)shown(wc, "VALHALLA_WIN"), (int)shown(wc, "VALHALLA_ARMIES_WIN"), (int)shown(wc, "VALHALLA_ARMIES_DESTROYED_TEXT"), (int)shown(lc, "VALHALLA_ARMIES_DESTROYED_TEXT"));
		check(wc.ownOutcome() == 1, who + "the winner won");
		check(lc.ownOutcome() == 2, who + "the other lost");
		if (armies)
			check(shown(wc, "VALHALLA_ARMIES_WIN") && !shown(wc, "VALHALLA_WIN") && !shown(wc, "VALHALLA_ARMIES_DESTROYED_TEXT"), who + "the winner sees the armies' win box, no defeat box");
		else
			check(shown(wc, "VALHALLA_WIN") && !shown(wc, "VALHALLA_ARMIES_WIN") && !shown(wc, "VALHALLA_ARMIES_DESTROYED_TEXT"), who + "the winner sees one win box, no defeat box");
		check(shown(lc, "VALHALLA_ARMIES_DESTROYED_TEXT"), who + "the other sees its defeat box");
		check(!(lc.gtwStates.count(gs.gameTextWindows.names.getIndex("VALHALLA_ARMIES_WIN"))), who + "the other no win box");
	}
	printf(failures ? "UNITTEST FAILED (%d)\n" : "UNITTEST PASSED\n", failures);
}

// The original's keys (Hot Keys.cpp) and the patrol: on 4 Peaks, the selection key M picks the
// player's Manor; a peasant sent on patrol 25 m away walks there and back again
void Test_HotKeys()
{
	int failures = 0;
	auto check = [&](bool ok, const std::string& what) {
		if (!ok) failures++;
		printf("UNITTEST %s: %s\n", ok ? "ok" : "FAILED", what.c_str());
		fflush(stdout);
	};
	LoadBCP("data.bcp");
	Server server;
	Client client(true);
	NetLocalBuffer b1, b2;
	NetLocalLink srv(&b1, &b2), cli(&b2, &b1);
	server.addClient(&srv);
	client.serverLink = &cli;
	server.loadSaveGame("Save_Games\\2 Player 4 Peaks.lvl");
	server.setClientPlayerControl(0, 1027);
	server.timeManager.unpause();
	server.startLevel();
	auto step = [&](int frames) {
		for (int i = 0; i < frames; i++) {
			server.tick();
			client.tick();
			SDL_Delay(20);
		}
	};
	step(10);
	ClientInterface ui(&client, nullptr);
	// M: the Manor (Palace Building) of this player
	ui.hotKeySelect("Hot Key - 'M'");
	ClientGameObject* picked = ui.selection.empty() ? nullptr : ui.selection.begin()->get();
	const int palace = client.gameSet->items.names.getIndex("Palace Building");
	check(picked && picked->getPlayer() == client.clientPlayer.get() && palace >= 0 && picked->getItem(palace) > 0,
		"M: the player's Manor selected");
	check(!ui.minimapPings.empty(), "M: a ping on the minimap");
	// a peasant on patrol
	ServerGameObject* me = server.findObject(1027);
	ServerGameObject* peasant = nullptr;
	auto findUnit = [&](CommonGameObject* o, auto& self) -> void {
		for (const auto& [type, children] : o->children)
			for (CommonGameObject* c : children) {
				if (!peasant && c->blueprint->bpClass == Tags::GAMEOBJCLASS_CHARACTER)
					peasant = c->dyncast<ServerGameObject>();
				self(c, self);
			}
	};
	findUnit(me, findUnit);
	if (peasant)
		printf("UNITTEST the unit sent on patrol: %s %u\n", peasant->blueprint->name.c_str(), peasant->id);
	if (!peasant) {
		printf("UNITTEST FAILED (no peasant)\n");
		return;
	}
	const Vector3 start = peasant->position;
	Vector3 point = start + Vector3(25.0f, 0.0f, 0.0f);
	point.y = server.terrain ? server.terrain->getHeight(point.x, point.z) : start.y;
	client.sendPatrol(client.findObject(peasant->id), false, point);
	float nearPoint = 1e9f, backNearStart = 1e9f;
	bool reached = false;
	for (int s = 0; s < 50 * 40; s++) {
		step(1);
		const float dp = (peasant->position - point).len2xz(), ds = (peasant->position - start).len2xz();
		nearPoint = std::min(nearPoint, dp);
		if (dp < 3.0f)
			reached = true;
		if (reached)
			backNearStart = std::min(backNearStart, ds);
		if (reached && ds < 3.0f)
			break;
	}
	Order* order = peasant->orderConfig.getCurrentOrder();
	printf("UNITTEST patrol: nearest to the point %.1f m, back to the start %.1f m, order %s\n", nearPoint, backNearStart,
		order ? server.gameSet->orders.getString(order->blueprint).c_str() : "-");
	check(reached, "the patrolling peasant reached the point");
	check(backNearStart < 3.0f, "and walked back to where it started");
	check(order && order->patrolPoints.size() == 2, "still patrolling, two points");
	// Shift+R: one more point
	client.sendPatrol(client.findObject(peasant->id), true, start + Vector3(0.0f, 0.0f, 20.0f));
	step(5);
	order = peasant->orderConfig.getCurrentOrder();
	check(order && order->patrolPoints.size() == 3, "Shift+R: a third point");
	printf(failures ? "UNITTEST FAILED (%d)\n" : "UNITTEST PASSED\n", failures);
}

// Waypoint flags (ClientInterface::updateOrderFlags): a peasant on 4 Peaks sent to one point and
// Shift-queued to two more: the client stands three flags, two once the first point is reached,
// none once it has walked them all; a new order without Shift forgets the queued ones
void Test_WaypointFlags()
{
	int failures = 0;
	auto check = [&](bool ok, const std::string& what) {
		if (!ok) failures++;
		printf("UNITTEST %s: %s\n", ok ? "ok" : "FAILED", what.c_str());
		fflush(stdout);
	};
	LoadBCP("data.bcp");
	Server server;
	Client client(true);
	NetLocalBuffer b1, b2;
	NetLocalLink srv(&b1, &b2), cli(&b2, &b1);
	server.addClient(&srv);
	client.serverLink = &cli;
	server.loadSaveGame("Save_Games\\2 Player 4 Peaks.lvl");
	server.setClientPlayerControl(0, 1027);
	server.timeManager.unpause();
	server.startLevel();
	ClientInterface ui(&client, nullptr);
	auto step = [&](int frames) {
		for (int i = 0; i < frames; i++) {
			server.tick();
			client.tick();
			ui.updateOrderFlags();
			SDL_Delay(20);
		}
	};
	step(10);
	ServerGameObject* me = server.findObject(1027);
	ServerGameObject* peasant = nullptr;
	auto findUnit = [&](CommonGameObject* o, auto& self) -> void {
		for (const auto& [type, children] : o->children)
			for (CommonGameObject* c : children) {
				if (!peasant && c->blueprint->bpClass == Tags::GAMEOBJCLASS_CHARACTER)
					peasant = c->dyncast<ServerGameObject>();
				self(c, self);
			}
	};
	findUnit(me, findUnit);
	const int moveIndex = client.gameSet->commands.names.getIndex("Move");
	ClientGameObject* unit = peasant ? client.findObject(peasant->id) : nullptr;
	if (!unit || moveIndex < 0) {
		printf("UNITTEST FAILED (no peasant or no Move command)\n");
		return;
	}
	Command* move = const_cast<Command*>(&client.gameSet->commands[moveIndex]);
	const Vector3 start = peasant->position;
	auto ground = [&](float dx, float dz) {
		Vector3 p = start + Vector3(dx, 0.0f, dz);
		p.y = server.terrain ? server.terrain->getHeight(p.x, p.z) : start.y;
		return p;
	};
	const Vector3 points[3] = { ground(12.0f, 0.0f), ground(25.0f, 0.0f), ground(25.0f, 8.0f) };
	printf("UNITTEST the unit: %s %u at %.1f %.1f %.1f\n", peasant->blueprint->name.c_str(), peasant->id, start.x, start.y, start.z);
	ui.selection.insert(unit);
	for (int i = 0; i < 3; i++) {
		g_modShift = i > 0;
		ui.sendRightClickCommand(move, nullptr, points[i], Vector3(-1, 0, 0));
	}
	g_modShift = false;
	ui.updateOrderFlags();
	check(ui.orderFlagCount() == 3, "three points queued: three flags (" + std::to_string(ui.orderFlagCount()) + ")");
	step(5);
	check(ui.orderFlagCount() == 3, "still three while it walks to the first");
	// walk to the first point; a frame later the client has the unit there
	int reached = -1, afterFirst = -1, minFlags = 99;
	for (int s = 0; s < 50 * 60; s++) {
		step(1);
		minFlags = std::min(minFlags, ui.orderFlagCount());
		if (reached < 0 && (peasant->position - points[0]).len2xz() < 2.5f) {
			reached = s;
			step(3);
			afterFirst = ui.orderFlagCount();
		}
		if (reached >= 0 && ui.orderFlagCount() == 0)
			break;
	}
	printf("UNITTEST first point reached at frame %d: %d flags after it, %d at the end, %.1f m from the last point\n", reached, afterFirst,
		ui.orderFlagCount(), (peasant->position - points[2]).len2xz());
	check(reached >= 0 && afterFirst == 2, "the first point reached: two flags");
	check(ui.orderFlagCount() == 0, "all three walked: no flag");
	// queued again, then a new order without Shift: only its flag stays (once the old ones' second is up)
	for (int i = 0; i < 3; i++) {
		g_modShift = i > 0;
		ui.sendRightClickCommand(move, nullptr, points[2 - i], Vector3(-1, 0, 0));
	}
	g_modShift = false;
	check(ui.orderFlagCount() == 3, "queued again: three flags");
	step(60);
	ui.sendRightClickCommand(move, nullptr, start, Vector3(-1, 0, 0));
	ui.updateOrderFlags();
	check(ui.orderFlagCount() == 1, "a new order without Shift: its flag only (" + std::to_string(ui.orderFlagCount()) + ")");
	printf(failures ? "UNITTEST FAILED (%d)\n" : "UNITTEST PASSED\n", failures);
}

// Disguises and fog reveals (ServerDiplomacy.cpp): a unit of B (1030) on 4 Peaks disguised as a
// peasant to A (1027), unmasked for A, dropped; revealed to A through the fog for a time
void Test_Disguises()
{
	int failures = 0;
	auto check = [&](bool ok, const std::string& what) {
		if (!ok) failures++;
		printf("UNITTEST %s: %s\n", ok ? "ok" : "FAILED", what.c_str());
		fflush(stdout);
	};
	LoadBCP("data.bcp");
	Server server;
	Client clientA(true), clientB(true);
	NetLocalBuffer a1, a2, b1, b2;
	NetLocalLink srvA(&a1, &a2), cliA(&a2, &a1), srvB(&b1, &b2), cliB(&b2, &b1);
	server.addClient(&srvA);
	server.addClient(&srvB);
	clientA.serverLink = &cliA;
	clientB.serverLink = &cliB;
	server.loadSaveGame("Save_Games\\2 Player 4 Peaks.lvl");
	server.setClientPlayerControl(0, 1027);
	server.setClientPlayerControl(1, 1030);
	server.timeManager.unpause();
	server.startLevel();
	const GameSet& gs = *server.gameSet;
	auto step = [&](int frames) {
		for (int i = 0; i < frames; i++) {
			server.tick();
			clientA.tick();
			clientB.tick();
			SDL_Delay(20);
		}
	};
	auto run = [&](ServerGameObject* self, const char* line) {
		SrvScriptContext ctx(&server, self);
		GSFileParser gsf(line);
		std::unique_ptr<Action> act(ReadAction(gsf, gs));
		act->run(&ctx);
	};
	step(10);
	ServerGameObject* B = server.findObject(1030);
	std::vector<ServerGameObject*> units;
	auto findUnits = [&](CommonGameObject* o, auto& self) -> void {
		for (const auto& [type, children] : o->children)
			for (CommonGameObject* c : children) {
				if (c->blueprint->bpClass == Tags::GAMEOBJCLASS_CHARACTER)
					units.push_back(c->dyncast<ServerGameObject>());
				self(c, self);
			}
	};
	if (B)
		findUnits(B, findUnits);
	if (units.size() < 2 || !clientA.clientPlayer || !clientB.clientPlayer) {
		printf("UNITTEST FAILED (not set up)\n");
		return;
	}
	ServerGameObject* u = units[0];
	ServerGameObject* u2 = units[1];
	printf("UNITTEST B's unit: %s %u\n", u->blueprint->name.c_str(), u->id);
	auto seenBy = [&](Client& c) { return c.disguiseFor(c.findObject(u->id)); };

	// Spell Possess.cpp's: a shopper peasant
	run(u, "ADOPT_APPEARANCE_FOR CHARACTER \"Peasant\" \"Female\" \"Shopper\" FINDER_SELF");
	step(3);
	const Client::Disguise* d = seenBy(clientA);
	check(d && d->blueprint->name == "Peasant" && d->blueprint->getAppearance(d->subtype, d->appearance),
		"A (the enemy) sees a peasant (Female, Shopper)");
	check(!seenBy(clientB), "B (its own player) sees it as it is");
	// Counter Espionage.cpp's "Succubus Discovered": unmasked for the player who saw through it
	run(u, "ADOPT_DEFAULT_APPEARANCE_FOR FINDER_SELF FINDER_SPECIFIC_ID 1027");
	step(3);
	check(!seenBy(clientA), "unmasked for A: A sees it as it is");
	// and "On Stop Seeing Object": disguised again
	run(u, "ADOPT_APPEARANCE_FOR CHARACTER \"Peasant\" \"Male\" \"Disguised Peasant\" FINDER_SELF");
	step(3);
	check(seenBy(clientA) != nullptr, "disguised again (Male, Disguised Peasant)");
	// a save keeps it
	const std::string text = server.aiSettingsText();
	check(text.find("disguise." + std::to_string(u->id) + "=") != std::string::npos, "the save's .ai.txt holds the disguise");
	// "Spy Discovered": for its own player, the disguise dropped
	run(u, "ADOPT_DEFAULT_APPEARANCE_FOR FINDER_SELF FINDER_PLAYER");
	step(3);
	check(!seenBy(clientA) && clientA.disguises.empty() && server.disguises.empty(), "for its own player: dropped for everyone");
	server.applyAiSettingsText(text);
	check(server.disguises.count(u->id) == 1, "read back from the .ai.txt");

	// REVEAL_FOG_OF_WAR: u for 10 s, u2 for 1 s, 3 tiles, to A
	FogOfWar fog;
	fog.update(&clientA);
	const bool hidden = !fog.isSeen(u->position) && !fog.isSeen(u2->position);
	printf("UNITTEST before the reveal: B's units %s to A\n", hidden ? "hidden" : "seen");
	run(u, "REVEAL_FOG_OF_WAR FINDER_SELF CONSTANT 10 CONSTANT 3 FINDER_SPECIFIC_ID 1027");
	run(u2, "REVEAL_FOG_OF_WAR FINDER_SELF CONSTANT 1 CONSTANT 3 FINDER_SPECIFIC_ID 1027");
	step(15); // (300 ms: the fog's next update)
	check(clientA.fogReveals.size() == 2 && clientB.fogReveals.empty(), "A got both reveals, B none");
	fog.update(&clientA);
	check(fog.isSeen(u->position) && fog.isSeen(u2->position), "both seen by A through the fog");
	step(60); // 1.2 s
	fog.update(&clientA);
	check(clientA.fogReveals.size() == 1 && clientA.fogReveals[0].object == u->id, "the 1 s reveal over, the 10 s one still on");
	check(fog.isSeen(u->position), "u still seen");
	if (hidden)
		check(!fog.isSeen(u2->position) || (u2->position - u->position).len2xz() < 20.0f, "u2 hidden again");
	printf(failures ? "UNITTEST FAILED (%d)\n" : "UNITTEST PASSED\n", failures);
}

// A standing formation turns to face an attack from its flank or rear (FormationController::
// faceAttackers), not one from the front, and not again within 4 s
void Test_FormationFacing()
{
	// (wkbai's own turn, off by default: WKB.exe has none)
	g_settings["formationsFaceAttacks"] = true;
	int failures = 0;
	auto check = [&](bool ok, const std::string& what) {
		if (!ok) failures++;
		printf("UNITTEST %s: %s\n", ok ? "ok" : "FAILED", what.c_str());
		fflush(stdout);
	};
	LoadBCP("data.bcp");
	Server server;
	server.loadSaveGame("Save_Games\\2 Player 4 Peaks.lvl");
	ServerGameObject* player = server.findObject(1027);
	ServerGameObject* enemyPlayer = server.findObject(1030);
	std::vector<ServerGameObject*> members, enemies;
	auto collect = [&](CommonGameObject* obj, std::vector<ServerGameObject*>& into, auto& self) -> void {
		for (const auto& [type, children] : obj->children)
			for (CommonGameObject* child : children) {
				if (child->blueprint->bpClass == Tags::GAMEOBJCLASS_CHARACTER)
					into.push_back(child->dyncast<ServerGameObject>());
				else
					self(child, into, self);
			}
	};
	if (player && enemyPlayer) {
		collect(player, members, collect);
		collect(enemyPlayer, enemies, collect);
	}
	if (members.size() < 2 || enemies.empty() || server.gameSet->objBlueprints[Tags::GAMEOBJCLASS_FORMATION].size() == 0) {
		printf("UNITTEST FAILED (not set up)\n");
		return;
	}
	ServerGameObject* formation = server.spawnObject(&server.gameSet->objBlueprints[Tags::GAMEOBJCLASS_FORMATION][0], player, {}, {});
	Vector3 sum;
	for (ServerGameObject* m : members) {
		sum += m->position;
		m->setParent(formation);
	}
	formation->setPosition(sum / (float)members.size());
	formation->setOrientation(Vector3(0, 0, 0)); // its front to the north (-z)
	formation->sendEvent(Tags::PDEVENT_ON_SPAWN);
	server.timeManager.unpause();
	auto run = [&](float seconds) {
		const uint64_t start = SDL_GetTicks();
		while (SDL_GetTicks() - start < (uint64_t)(seconds * 1000.0f)) {
			server.tick();
			SDL_Delay(10);
		}
	};
	run(2.0f);
	ServerGameObject* enemy = enemies[0];
	auto hitFrom = [&](const Vector3& offset) {
		enemy->setPosition(formation->position + offset);
		members[0]->lastHitBy = enemy->id;
		members[0]->lastHitTime = server.timeManager.currentTime;
	};
	printf("UNITTEST formation of %zu at (%.0f, %.0f), its order %s\n", members.size(), formation->position.x, formation->position.z,
		formation->orderConfig.getCurrentOrder() ? "some" : "none");
	// from the front (north, 20° off): it stays
	hitFrom(Vector3(std::sin(0.35f) * 30.0f, 0, -std::cos(0.35f) * 30.0f));
	run(0.5f);
	check(std::abs(formation->orientation.y) < 0.01f, "an attack from the front: no turn");
	// from the right flank (east): it turns east (pi/2)
	hitFrom(Vector3(30, 0, 0));
	run(0.5f);
	printf("UNITTEST facing after the flank attack: %.2f\n", formation->orientation.y);
	check(std::abs(formation->orientation.y - 1.5708f) < 0.05f, "an attack on the flank: it turns to face it");
	// from the rear right away: not within 4 s; then it turns
	hitFrom(Vector3(-30, 0, 0));
	run(0.5f);
	check(std::abs(formation->orientation.y - 1.5708f) < 0.05f, "a rear attack within 4 s: no turn yet");
	run(3.8f);
	hitFrom(Vector3(-30, 0, 0));
	run(0.5f);
	printf("UNITTEST facing after the rear attack: %.2f\n", formation->orientation.y);
	check(std::abs(std::abs(formation->orientation.y) - 1.5708f) < 0.05f && formation->orientation.y < 0.0f, "then it turns to face the rear attack (west)");
	// damage sharing (WKB.exe 0x551f50): a hit of 10 on a member of the formed formation costs the
	// formation DAMAGE_SHARE_PERCENTAGE of it, spread over NUMBER_OF_DAMAGE_SHARERS members (all of
	// them when that's at least the members less one)
	{
		extern int g_simulating;
		const GameObjBlueprint* fbp = formation->blueprint;
		std::vector<float> before;
		float total = 0.0f;
		for (ServerGameObject* m : members) {
			before.push_back(m->getItem(Tags::PDITEM_HIT_POINTS));
			total += before.back();
		}
		g_simulating++;
		members[1]->setItem(Tags::PDITEM_HIT_POINTS, before[1] - 10.0f);
		g_simulating--;
		float after = 0.0f;
		for (ServerGameObject* m : members)
			after += m->getItem(Tags::PDITEM_HIT_POINTS);
		const int n = fbp->damageSharers >= (int)members.size() - 1 ? (int)members.size() : fbp->damageSharers;
		const float expected = fbp->damageSharePercentage * 10.0f * (float)n / (float)std::max(fbp->damageSharers, 1);
		printf("UNITTEST %s shares %.2f over %d: a hit of 10 cost %.2f in all, the one hit %.2f\n", fbp->name.c_str(),
			fbp->damageSharePercentage, fbp->damageSharers, total - after, before[1] - members[1]->getItem(Tags::PDITEM_HIT_POINTS));
		check(fbp->damageSharers > 0 && std::abs((total - after) - expected) < 0.05f, "a hit on a member is shared out by the formation");
	}
	printf(failures ? "UNITTEST FAILED (%d)\n" : "UNITTEST PASSED\n", failures);
}

// The original's lobby items: Team Placement on 8 Player Continent (a team's members start side
// by side), a renamed AI General, Unit Lives "Infinite" (-1) and a number, Lock Server
void Test_LobbyItems()
{
	int failures = 0;
	auto check = [&](bool ok, const std::string& what) {
		if (!ok) failures++;
		printf("UNITTEST %s: %s\n", ok ? "ok" : "FAILED", what.c_str());
		fflush(stdout);
	};
	LoadBCP("data.bcp");
	using Slot = QuickSkirmishMenu::Slot;
	// where each player of a level starts: its buildings' middle
	auto startOf = [](ServerGameObject* player) {
		Vector3 sum;
		int n = 0;
		auto walk = [&](CommonGameObject* o, auto& self) -> void {
			for (const auto& [type, children] : o->children)
				for (CommonGameObject* c : children) {
					if (c->blueprint->bpClass == Tags::GAMEOBJCLASS_BUILDING || c->blueprint->bpClass == Tags::GAMEOBJCLASS_CHARACTER) {
						sum += c->position;
						n++;
					}
					self(c, self);
				}
		};
		walk(player, walk);
		return n ? sum / (float)n : player->position;
	};
	// teams 1, 2, 1, 2 at the first four positions: the average distance between team-mates and
	// between enemies, with and without Team Placement
	auto distances = [&](bool placement, float& mates, float& foes) {
		Server server;
		server.loadSaveGame("Save_Games\\8 Player Continent.lvl");
		std::map<uint32_t, Vector3> starts;
		for (const auto& [type, children] : server.getLevel()->children)
			for (CommonGameObject* c : children)
				if (c->blueprint->bpClass == Tags::GAMEOBJCLASS_PLAYER)
					starts[c->id] = startOf(c->dyncast<ServerGameObject>());
		QuickSkirmishMenu skirmish;
		skirmish.changeSettingsForLevel("8 Player Continent.lvl");
		skirmish.setSetting("teamPlacement", placement ? 1 : 0);
		std::vector<Slot> slots(8);
		for (Slot& s : slots) s.type = Slot::EMPTY;
		slots[0].type = Slot::HUMAN; slots[0].team = 1;
		slots[1].type = Slot::AI; slots[1].team = 2; slots[1].name = "Sir Testalot";
		slots[2].type = Slot::AI; slots[2].team = 1;
		slots[3].type = Slot::AI; slots[3].team = 2;
		skirmish.setSlots(slots);
		server.timeManager.unpause();
		server.timeManager.stepMs = 20;
		skirmish.applySettings(server);
		std::vector<std::pair<Vector3, int>> at; // the players left, with their teams
		bool renamed = false;
		for (const auto& [type, children] : server.getLevel()->children)
			for (CommonGameObject* c : children)
				if (c->blueprint->bpClass == Tags::GAMEOBJCLASS_PLAYER && starts.count(c->id)) {
					ServerGameObject* p = c->dyncast<ServerGameObject>();
					if (p->name == "Sir Testalot") renamed = true;
					at.push_back({ starts[c->id], 0 });
				}
		// the teams from the diplomacy: allied pairs
		mates = foes = 0.0f;
		int nm = 0, nf = 0;
		std::vector<ServerGameObject*> ps;
		for (const auto& [type, children] : server.getLevel()->children)
			for (CommonGameObject* c : children)
				if (c->blueprint->bpClass == Tags::GAMEOBJCLASS_PLAYER && starts.count(c->id) && c->blueprint->name != "Neutral Player" && c->blueprint->name != "Baresark Player" && !c->dyncast<ServerGameObject>()->deleted)
				{
					ps.push_back(c->dyncast<ServerGameObject>());
					printf("UNITTEST player %u %s \"%s\" starts at (%.0f, %.0f)\n", c->id, c->blueprint->name.c_str(), c->dyncast<ServerGameObject>()->name.c_str(), starts[c->id].x, starts[c->id].z);
				}
		const int allied = server.gameSet->diplomaticStatuses.names.getIndex("Allied");
		for (size_t a = 0; a < ps.size(); a++)
			for (size_t b = a + 1; b < ps.size(); b++) {
				const float d = (starts[ps[a]->id] - starts[ps[b]->id]).len2xz();
				if (server.getDiplomaticStatus(ps[a], ps[b]) == allied) { mates += d; nm++; }
				else { foes += d; nf++; }
			}
		if (nm) mates /= nm;
		if (nf) foes /= nf;
		printf("UNITTEST Team Placement %s: %zu players, team-mates %.0f m apart, enemies %.0f m\n", placement ? "on" : "off", ps.size(), mates, foes);
		return renamed && ps.size() == 4 && nm == 2;
	};
	float mates0 = 0, foes0 = 0, mates1 = 0, foes1 = 0;
	const bool setUp0 = distances(false, mates0, foes0);
	const bool setUp1 = distances(true, mates1, foes1);
	check(setUp0 && setUp1, "four players, two teams of two; the AI General renamed \"Sir Testalot\"");
	check(mates1 < foes1, "Team Placement: team-mates start nearer each other than enemies");
	check(mates1 <= mates0 + 0.1f, "and no farther apart than without it");

	// Unit Lives (Valhalla): 0 is "Infinite" (-1), else the number
	for (int lives : { 0, 3 }) {
		Server server;
		server.loadSaveGame("Save_Games\\2 Player 4 Peaks.lvl");
		QuickSkirmishMenu skirmish;
		skirmish.changeSettingsForLevel("2 Player 4 Peaks.lvl");
		skirmish.setSetting("mode", 1);
		skirmish.setSetting("unitLives", lives);
		std::vector<Slot> slots(2);
		slots[0].type = Slot::HUMAN;
		slots[1].type = Slot::EMPTY;
		skirmish.setSlots(slots);
		server.timeManager.unpause();
		skirmish.applySettings(server);
		ServerGameObject* human = server.findObject(1027);
		const int item = server.gameSet->items.names.getIndex("No. of Lives Left");
		const float v = human && item >= 0 ? human->getItem(item) : -999.0f;
		check(v == (lives == 0 ? -1.0f : (float)lives), "Unit Lives " + std::to_string(lives) + ": \"No. of Lives Left\" " + std::to_string((int)v));
	}

	// Lock Server: a guest joining a locked server is refused, one already in stays
	{
		Server server;
		Client host(true), guest(true), late(true);
		NetLocalBuffer h1, h2, g1, g2, l1, l2;
		NetLocalLink srvH(&h1, &h2), cliH(&h2, &h1), srvG(&g1, &g2), cliG(&g2, &g1), srvL(&l1, &l2), cliL(&l2, &l1);
		server.addClient(&srvH);
		server.addClient(&srvG);
		host.serverLink = &cliH;
		guest.serverLink = &cliG;
		// (as a joining guest does: its mods first, which the host compares)
		guest.sendModManifest();
		guest.sendLobbyHello("Early");
		for (int i = 0; i < 10; i++) { server.tick(); host.tick(); guest.tick(); }
		server.lockServer = true;
		server.addClient(&srvL);
		late.serverLink = &cliL;
		late.sendModManifest();
		late.sendLobbyHello("Late");
		for (int i = 0; i < 10; i++) { server.tick(); host.tick(); guest.tick(); late.tick(); }
		printf("UNITTEST guests: \"%s\" refused %d, \"%s\" refused %d\n", server.clientInfos[1].name.c_str(), (int)server.clientInfos[1].refused,
			server.clientInfos.size() > 2 ? server.clientInfos[2].name.c_str() : "-", server.clientInfos.size() > 2 ? (int)server.clientInfos[2].refused : -1);
		check(server.clientInfos.size() == 3 && !server.clientInfos[1].refused, "the guest already in stays");
		check(server.clientInfos.size() == 3 && server.clientInfos[2].refused, "the late guest is refused: the server is locked");
	}
	printf(failures ? "UNITTEST FAILED (%d)\n" : "UNITTEST PASSED\n", failures);
}

// The campaign's and tutorials' script actions: DISABLE_/ENABLE_GAME_INTERFACE, the mission
// objectives' position markers (a place, an object followed, stopped), UNLOCK_LEVEL
void Test_CampaignInterface()
{
	int failures = 0;
	auto check = [&](bool ok, const std::string& what) {
		if (!ok) failures++;
		printf("UNITTEST %s: %s\n", ok ? "ok" : "FAILED", what.c_str());
		fflush(stdout);
	};
	LoadBCP("data.bcp");
	Server server;
	Client client(true);
	NetLocalBuffer b1, b2;
	NetLocalLink srv(&b1, &b2), cli(&b2, &b1);
	server.addClient(&srv);
	client.serverLink = &cli;
	server.loadSaveGame("Save_Games\\2 Player 4 Peaks.lvl");
	server.setClientPlayerControl(0, 1027);
	server.timeManager.unpause();
	server.startLevel();
	const GameSet& gs = *server.gameSet;
	auto step = [&](int frames) {
		for (int i = 0; i < frames; i++) {
			server.tick();
			client.tick();
			SDL_Delay(10);
		}
	};
	auto run = [&](ServerGameObject* self, const char* line) {
		SrvScriptContext ctx(&server, self);
		GSFileParser gsf(line);
		std::unique_ptr<Action> act(ReadAction(gsf, gs));
		act->run(&ctx);
	};
	step(5);
	ServerGameObject* me = server.findObject(1027);
	if (!me) {
		printf("UNITTEST FAILED (not set up)\n");
		return;
	}
	check(client.gameInterfaceEnabled, "at the start: the interface is on");
	run(me, "DISABLE_GAME_INTERFACE FINDER_SELF");
	step(3);
	check(!client.gameInterfaceEnabled, "DISABLE_GAME_INTERFACE: off on the player's client");
	run(me, "ENABLE_GAME_INTERFACE FINDER_SELF");
	step(3);
	check(client.gameInterfaceEnabled && server.interfaceDisabled.empty(), "ENABLE_GAME_INTERFACE: on again");

	// Feature Tests.cpp's: a place, then a unit followed
	run(me, "INDICATE_POSITION_OF_MISSION_OBJECTIVES_ENTRY TEST_MISSION_OBJECTIVE_2 ABSOLUTE_POSITION 50 0 50 0 0 FINDER_SELF");
	ServerGameObject* unit = nullptr;
	auto findUnit = [&](CommonGameObject* o, auto& self) -> void {
		for (const auto& [type, children] : o->children)
			for (CommonGameObject* c : children) {
				if (!unit && c->blueprint->bpClass == Tags::GAMEOBJCLASS_CHARACTER)
					unit = c->dyncast<ServerGameObject>();
				self(c, self);
			}
	};
	findUnit(me, findUnit);
	if (unit) {
		const std::string line = "TRACK_OBJECT_POSITION_FROM_MISSION_OBJECTIVES_ENTRY TEST_MISSION_OBJECTIVE_3 FINDER_SPECIFIC_ID " + std::to_string(unit->id) + " FINDER_SELF";
		run(me, line.c_str());
	}
	step(3);
	auto m2 = client.objectiveMarkers.find("TEST_MISSION_OBJECTIVE_2");
	auto m3 = client.objectiveMarkers.find("TEST_MISSION_OBJECTIVE_3");
	check(m2 != client.objectiveMarkers.end() && m2->second.kind == 1 && m2->second.x == 50.0f && m2->second.z == 50.0f, "a marker at (50, 50)");
	check(unit && m3 != client.objectiveMarkers.end() && m3->second.kind == 2 && m3->second.object == unit->id, "a marker following the unit");
	run(me, "STOP_INDICATING_POSITION_OF_MISSION_OBJECTIVES_ENTRY TEST_MISSION_OBJECTIVE_2 FINDER_SELF");
	step(3);
	check(client.objectiveMarkers.count("TEST_MISSION_OBJECTIVE_2") == 0 && client.objectiveMarkers.size() == 1, "STOP_INDICATING: that marker gone, the other kept");

	run(me, "UNLOCK_LEVEL \"New_Game.sav\"");
	step(3);
	check(client.unlockedLevels.count("New_Game.sav") == 1, "UNLOCK_LEVEL: noted on the client");
	printf(failures ? "UNITTEST FAILED (%d)\n" : "UNITTEST PASSED\n", failures);
}

// Minimap alerts (SHOW_BLINKING_DOT_ON_MINIMAP, COLLAPSING_CIRCLE_ON_MINIMAP) on 4 Peaks: only the
// players listed get them, with their colour, seconds and circles; they end on time
void Test_MinimapAlerts()
{
	int failures = 0;
	auto check = [&](bool ok, const std::string& what) {
		if (!ok) failures++;
		printf("UNITTEST %s: %s\n", ok ? "ok" : "FAILED", what.c_str());
		fflush(stdout);
	};
	LoadBCP("data.bcp");
	Server server;
	Client clientA(true), clientB(true);
	NetLocalBuffer a1, a2, b1, b2;
	NetLocalLink srvA(&a1, &a2), cliA(&a2, &a1), srvB(&b1, &b2), cliB(&b2, &b1);
	server.addClient(&srvA);
	server.addClient(&srvB);
	clientA.serverLink = &cliA;
	clientB.serverLink = &cliB;
	server.loadSaveGame("Save_Games\\2 Player 4 Peaks.lvl");
	server.setClientPlayerControl(0, 1027);
	server.setClientPlayerControl(1, 1030);
	server.timeManager.unpause();
	server.startLevel();
	const GameSet& gs = *server.gameSet;
	auto step = [&](int frames) {
		for (int i = 0; i < frames; i++) {
			server.tick();
			clientA.tick();
			clientB.tick();
			SDL_Delay(20);
		}
	};
	auto run = [&](ServerGameObject* self, const char* line) {
		SrvScriptContext ctx(&server, self);
		GSFileParser gsf(line);
		std::unique_ptr<Action> act(ReadAction(gsf, gs));
		act->run(&ctx);
	};
	step(10);
	ServerGameObject* A = server.findObject(1027);
	ServerGameObject* B = server.findObject(1030);
	if (!A || !B) {
		printf("UNITTEST FAILED (not set up)\n");
		return;
	}
	// Attacking and Death.cpp's: a red dot for 8 s on the player's own minimap
	run(A, "SHOW_BLINKING_DOT_ON_MINIMAP LOCATION_OF FINDER_SELF COLOUR_RED CONSTANT 8 FINDER_PLAYER");
	// Charm of Madness's false alarm: B's colour, to A
	run(B, "SHOW_BLINKING_DOT_ON_MINIMAP LOCATION_OF FINDER_SELF PLAYER_COLOUR FINDER_SELF CONSTANT 1 FINDER_SPECIFIC_ID 1027");
	// Fire.cpp-like: orange circles, 1 s, 3 of them, to B
	run(B, "COLLAPSING_CIRCLE_ON_MINIMAP LOCATION_OF FINDER_SELF COLOUR_ORANGE CONSTANT 1 CONSTANT 3 FINDER_PLAYER");
	step(3);
	const auto& alertsA = clientA.minimapAlerts;
	const auto& alertsB = clientB.minimapAlerts;
	printf("UNITTEST alerts: A %zu, B %zu\n", alertsA.size(), alertsB.size());
	check(alertsA.size() == 2 && alertsB.size() == 1, "A got its two dots, B its circles");
	if (alertsA.size() == 2) {
		check(!alertsA[0].circle && alertsA[0].colour == 0xFF2020 && alertsA[0].seconds == 8.0f
			&& std::abs(alertsA[0].x - A->position.x) < 0.01f, "the red dot: 8 s, at A's position");
		check(alertsA[1].colour == (Server::ALERT_PLAYER_COLOUR | (uint32_t)B->color), "the false alarm in B's colour");
	}
	if (alertsB.size() == 1)
		check(alertsB[0].circle && alertsB[0].circles == 3 && alertsB[0].colour == 0xFF9010 && alertsB[0].seconds == 1.0f,
			"the orange circles: 3 in 1 s");
	// they end on time (1.5 s on: the 1 s ones gone, the 8 s one left)
	step(75);
	check(alertsA.size() == 1 && alertsA[0].seconds == 8.0f && alertsB.empty(), "after 1.5 s only the 8 s dot is left");
	// Home: the warnings' places, the latest first, then the one before
	run(A, "COLLAPSING_CIRCLE_ON_MINIMAP CENTRE_OF_MAP COLOUR_RED CONSTANT 1 CONSTANT 1 FINDER_PLAYER");
	step(3);
	const auto area = server.terrain->getPlayableArea();
	printf("UNITTEST A's warning places: %zu\n", clientA.warningPlaces.size());
	check(clientA.warningPlaces.size() >= 2 && std::abs(clientA.warningPlaces[0].first - area.first * 0.5f) < 0.1f
		&& std::abs(clientA.warningPlaces[0].second - area.second * 0.5f) < 0.1f, "the centre (CENTRE_OF_MAP) is the latest warning place");
	ClientInterface uiA(&clientA, nullptr);
	uiA.lastWarning();
	check(uiA.hotKeyCameraTarget && std::abs(uiA.hotKeyCameraTarget->x - area.first * 0.5f) < 0.1f && !uiA.minimapPings.empty(),
		"Home: the camera to the latest, pinged");
	uiA.lastWarning();
	check(uiA.hotKeyCameraTarget && std::abs(uiA.hotKeyCameraTarget->x - clientA.warningPlaces[1].first) < 0.1f,
		"Home again: the one before");
	// STOP_SOUND reaches the clients (no sound device here: nothing to hear)
	run(A, "STOP_SOUND FINDER_PLAYER");
	step(2);
	printf(failures ? "UNITTEST FAILED (%d)\n" : "UNITTEST PASSED\n", failures);
}

// Valhalla's Army Enlistment: the companies read; a human's enlisted army (the lobby's ";v")
// mustered at its Manor on level start (Valhalla Mode.cpp)
void Test_ValhallaArmy()
{
	int failures = 0;
	auto check = [&](bool ok, const std::string& what) {
		if (!ok) failures++;
		printf("UNITTEST %s: %s\n", ok ? "ok" : "FAILED", what.c_str());
		fflush(stdout);
	};
	LoadBCP("data.bcp");
	Server server;
	server.loadSaveGame("Save_Games\\2 Player 4 Peaks.lvl");
	const GameSet& gs = *server.gameSet;
	const int league = gs.companies.names.getIndex("The League Of Knights");
	check(gs.companies.size() >= 40, "the companies read (" + std::to_string(gs.companies.size()) + ")");
	check(league >= 0 && gs.companies[league].cost == 100 && gs.companies[league].weaponPlatform == "Heavy Cavalry"
		&& gs.companies[league].alignments.size() == 5 && !gs.companies[league].playerStartSequence.actionList.empty(),
		"The League Of Knights: 100 points, heavy cavalry, five alignments, a start sequence");
	QuickSkirmishMenu skirmish;
	skirmish.changeSettingsForLevel("2 Player 4 Peaks.lvl");
	skirmish.setSetting("mode", 1);
	std::vector<QuickSkirmishMenu::Slot> slots(2);
	slots[0].type = QuickSkirmishMenu::Slot::HUMAN;
	slots[0].army = { "The League Of Knights", "The League Of Knights" };
	slots[1].type = QuickSkirmishMenu::Slot::EMPTY;
	skirmish.setSlots(slots);
	server.timeManager.unpause();
	server.timeManager.stepMs = 20;
	skirmish.applySettings(server);
	ServerGameObject* human = server.findObject(1027);
	const int spawn = gs.items.names.getIndex("League Of Knights Spawn");
	check(human && spawn >= 0 && human->getItem(spawn) == 2, "the human's \"League Of Knights Spawn\" is 2 (two enlistments)");
	auto characters = [&]() {
		int n = 0;
		auto count = [&](CommonGameObject* o, auto& self) -> void {
			for (const auto& [type, children] : o->children)
				for (CommonGameObject* c : children) {
					if (c->blueprint->bpClass == Tags::GAMEOBJCLASS_CHARACTER) n++;
					self(c, self);
				}
		};
		if (human) count(human, count);
		return n;
	};
	const int before = characters();
	// (the game time follows the clock: a step every 20 ms)
	for (int i = 0; i < 50 * 20; i++) {
		server.tick();
		SDL_Delay(20);
	}
	const int after = characters();
	printf("UNITTEST characters of the human: %d at the start, %d after 20 s (game time %.1f)\n", before, after, server.timeManager.currentTime);
	check(after >= before + 40, "the two companies mustered (40 or more new characters)");
	printf(failures ? "UNITTEST FAILED (%d)\n" : "UNITTEST PASSED\n", failures);
}

// Diplomacy as WKB.exe's (ServerDiplomacy.cpp): two players on 4 Peaks, A (1027) and B (1030)
void Test_Diplomacy()
{
	int failures = 0;
	auto check = [&](bool ok, const std::string& what) {
		if (!ok) failures++;
		printf("UNITTEST %s: %s\n", ok ? "ok" : "FAILED", what.c_str());
		fflush(stdout);
	};
	LoadBCP("data.bcp");
	Server server;
	Client clientA(true), clientB(true);
	NetLocalBuffer a1, a2, b1, b2;
	NetLocalLink srvA(&a1, &a2), cliA(&a2, &a1), srvB(&b1, &b2), cliB(&b2, &b1);
	server.addClient(&srvA);
	server.addClient(&srvB);
	clientA.serverLink = &cliA;
	clientB.serverLink = &cliB;
	server.loadSaveGame("Save_Games\\2 Player 4 Peaks.lvl");
	const uint32_t idA = 1027, idB = 1030;
	server.setClientPlayerControl(0, idA);
	server.setClientPlayerControl(1, idB);
	ServerGameObject* A = server.findObject(idA);
	ServerGameObject* B = server.findObject(idB);
	const GameSet& gs = *server.gameSet;
	const int allied = gs.diplomaticStatuses.names.getIndex("Allied"), neutral = gs.diplomaticStatuses.names.getIndex("Neutral"),
		enemy = gs.diplomaticStatuses.names.getIndex("Enemy");
	if (!A || !B || allied < 0 || neutral < 0 || enemy < 0) {
		printf("UNITTEST FAILED (not set up)\n");
		return;
	}
	auto step = [&](int frames) {
		for (int i = 0; i < frames; i++) {
			server.tick();
			clientA.tick();
			clientB.tick();
			SDL_Delay(2);
		}
	};
	auto run = [&](ServerGameObject* self, const char* line) {
		SrvScriptContext ctx(&server, self);
		GSFileParser gsf(line);
		std::unique_ptr<Action> act(ReadAction(gsf, gs));
		act->run(&ctx);
	};
	auto item = [&](ServerGameObject* o, const char* name) { const int i = gs.items.names.getIndex(name); return i >= 0 ? o->getItem(i) : -999.0f; };
	auto noticeFor = [](Client& c, const std::string& key) {
		for (const auto& n : c.notices) if (n.key == key) return true;
		return false;
	};
	step(10);
	check(server.getDiplomaticStatus(A, B) == enemy, "at the start: enemies");

	// B (the script's player) offers A an alliance; A sees it and its notice
	run(B, "MAKE_DIPLOMATIC_OFFER \"Allied\" FINDER_SELF FINDER_SPECIFIC_ID 1027");
	step(5);
	check(server.getDiplomaticOffer(B, A) == allied, "the server keeps B's offer");
	check(clientA.diplomaticOffers.count({ idB, idA }) && clientA.diplomaticOffers[{ idB, idA }] == allied, "A's client knows the offer");
	check(noticeFor(clientA, "Diplomatic_Offer_Received_Allied") && clientA.diplomacyButtons[0].news, "A: \"An Alliance has been offered!\", the button flashing");
	// an offer of no better status withdraws it
	run(B, "MAKE_DIPLOMATIC_OFFER \"Enemy\" FINDER_SELF FINDER_SPECIFIC_ID 1027");
	step(5);
	check(server.getDiplomaticOffer(B, A) == -1 && !clientA.diplomaticOffers.count({ idB, idA }), "an offer of Enemy withdraws it");
	check(noticeFor(clientA, "Diplomatic_Offer_Cancelled"), "A: \"Diplomatic offer retracted!\"");
	run(B, "MAKE_DIPLOMATIC_OFFER \"Allied\" FINDER_SELF FINDER_SPECIFIC_ID 1027");
	step(5);

	// A accepts in its Diplomacy window: the status set directly, symmetric, the offer gone, the
	// items on both, B told
	clientA.sendDiplomaticStatus(idB, allied);
	step(5);
	check(server.getDiplomaticStatus(A, B) == allied && server.getDiplomaticStatus(B, A) == allied, "accepted: allied both ways");
	check(server.getDiplomaticOffer(B, A) == -1 && !clientB.diplomaticOffers.count({ idB, idA }), "the offer is gone");
	check(item(A, "Diplomatic Status Player ID") == idB && item(A, "Diplomatic Status Old") == enemy && item(A, "Diplomatic Status New") == allied,
		"A's items: player B, Enemy to Allied");
	check(item(B, "Diplomatic Status Player ID") == idA && item(B, "Diplomatic Status New") == allied, "B's items: player A, Allied");
	check(noticeFor(clientB, "Diplomatic_Change_Received_Allied"), "B: \"Our alliance is forged!\"");
	check(clientA.getDiplomaticStatus(clientA.findObject(idA), clientA.findObject(idB)) == allied, "A's client: allied");
	// A declares war: no offer needed
	clientA.sendDiplomaticStatus(idB, enemy);
	step(5);
	check(server.getDiplomaticStatus(A, B) == enemy, "war declared at once");
	// ACCEPT_DIPLOMATIC_OFFER (the scripts'): B offers peace, A's script accepts
	run(B, "MAKE_DIPLOMATIC_OFFER \"Neutral\" FINDER_SELF FINDER_SPECIFIC_ID 1027");
	run(A, "ACCEPT_DIPLOMATIC_OFFER FINDER_SELF FINDER_SPECIFIC_ID 1030");
	step(5);
	check(server.getDiplomaticStatus(A, B) == neutral, "ACCEPT_DIPLOMATIC_OFFER: neutral");
	run(A, "ACCEPT_DIPLOMATIC_OFFER FINDER_SELF FINDER_SPECIFIC_ID 1030");
	check(server.getDiplomaticStatus(A, B) == neutral, "accepting without an offer changes nothing");

	// a tribute: moved at once, no more than A has, the items and event on B, B's notice
	const int food = gs.items.names.getIndex("Food");
	A->setItem(food, 300);
	B->setItem(food, 50);
	clientA.sendTribute(idB, food, 100);
	step(5);
	check(A->getItem(food) == 200 && B->getItem(food) == 150, "100 Food from A to B");
	printf("UNITTEST tribute items on B: player %g, amount %g, type %g\n", item(B, "Tribute Received Player ID"), item(B, "Tribute Received Amount"), item(B, "Tribute Received Type"));
	check(item(B, "Tribute Received Player ID") == idA && item(B, "Tribute Received Amount") == 100 && item(B, "Tribute Received Type") == 2,
		"B's items: from A, 100, type 2 (Food)");
	check(noticeFor(clientB, "Tribute_Received"), "B: \"You have received some tribute!\"");
	clientA.sendTribute(idB, food, 1000);
	step(5);
	check(A->getItem(food) == 0 && B->getItem(food) == 350, "a tribute larger than A has: all of it");

	// Disallow Diplomacy: the players' changes refused
	const int disallow = gs.items.names.getIndex("Disallow Diplomacy");
	server.getLevel()->setItem(disallow, 1);
	clientA.sendDiplomaticStatus(idB, enemy);
	step(5);
	check(server.getDiplomaticStatus(A, B) == neutral, "Disallow Diplomacy: A can't declare war");
	server.getLevel()->setItem(disallow, 0);

	// the AI Generals' messages: MAP_CHAT_MESSAGE read with its values, %d and %r filled
	const GameObjBlueprint* kang = gs.findBlueprint(Tags::GAMEOBJCLASS_PLAYER, "Kang The Merciless Kingmaker");
	bool values = false;
	if (kang)
		if (auto p = kang->chatMessages.find("Arrogant"); p != kang->chatMessages.end())
			if (auto d = p->second.find("Request resource for Alliance"); d != p->second.end())
				values = !d->second.empty() && d->second[0].values.size() == 2;
	check(values, "Kang's \"Request resource for Alliance\" texts, each with 2 values");
	ClientInterface ui(&clientA, nullptr);
	const std::string text = ui.chatDisplayText("Diplomatic_Statement_Request_Demand_Tribute_Neutral_1", { 250, 3 });
	printf("UNITTEST message: %s\n", text.c_str());
	check(text == "250 Gold as tribute.  Now.  Or it's war", "%d and %r: \"250 Gold as tribute...\"");
	check(ui.chatDisplayText("Diplomatic_Statement_Request_Demand_Tribute_Neutral_1", {}).find('%') == std::string::npos, "missing values: no % left, no hang");
	printf(failures ? "UNITTEST FAILED (%d)\n" : "UNITTEST PASSED\n", failures);
}

// Kingmaker provinces in skirmish, and the game windows (the text windows and the mission
// objectives, drawn by FrontEndUI.lua instead of ImGui boxes)
void Test_GameWindows()
{
	int failures = 0;
	auto check = [&](bool ok, const std::string& what) {
		if (!ok) failures++;
		printf("UNITTEST %s: %s\n", ok ? "ok" : "FAILED", what.c_str());
		fflush(stdout);
	};
	// a province from the skirmish menu: a skirmish map ("Skirmish Campaign Level" stays 0, so its
	// script's intro, campaign resources and conquest don't run); from the campaign map: campaign
	{
		QuickSkirmishMenu menu;
		menu.changeSettingsForLevel("CH2_Angland.lvl");
		check(!menu.isCampaignMode(), "CH2_Angland.lvl from the skirmish menu: not campaign mode");
		menu.changeSettingsForLevel("CH2_Angland_Multi.lvl");
		check(!menu.isCampaignMode(), "CH2_Angland_Multi.lvl: not campaign mode");
		menu.campaignProfile = true;
		menu.changeSettingsForLevel("CH2_Angland.lvl");
		check(menu.isCampaignMode(), "CH2_Angland.lvl from the campaign map: campaign mode");
		menu.campaignProfile = false;
		menu.changeSettingsForLevel("Interactive Tutorial.lvl");
		check(menu.isCampaignMode(), "the Interactive Tutorial: campaign mode (its scripts)");
		menu.changeSettingsForLevel("2 Player 4 Peaks.lvl");
		check(!menu.isCampaignMode(), "a skirmish map: not campaign mode");
	}

	LoadBCP("data.bcp");
	Server server;
	Client client(true);
	NetLocalBuffer b1, b2;
	NetLocalLink srv(&b1, &b2), cli(&b2, &b1);
	server.addClient(&srv);
	client.serverLink = &cli;
	server.loadSaveGame("Save_Games\\Interactive Tutorial.lvl");
	ServerGameObject* player = server.findObject(1027);
	if (!player) {
		printf("UNITTEST FAILED (no player 1027)\n");
		return;
	}
	server.setClientPlayerControl(0, 1027);
	ClientInterface ui(&client, nullptr);
	auto step = [&](int frames) {
		for (int i = 0; i < frames; i++) {
			server.tick();
			client.tick();
			SDL_Delay(2);
		}
	};
	auto run = [&](const char* line) {
		SrvScriptContext ctx(&server, player);
		GSFileParser gsf(line);
		std::unique_ptr<Action> act(ReadAction(gsf, *server.gameSet));
		act->run(&ctx);
	};
	step(10);

	// the objectives: MISSION_OBJECTIVES "Tutorial" (14 entries, all hidden at first)
	const int tutorial = server.gameSet->missionObjectives.names.getIndex("Tutorial");
	check(tutorial >= 0 && server.gameSet->missionObjectives[tutorial].entries.size() == 14
		&& server.gameSet->missionObjectives[tutorial].missionName == "TUTORIAL", "MISSION_OBJECTIVES \"Tutorial\" read: TUTORIAL, 14 entries");
	run("SET_ACTIVE_MISSION_OBJECTIVES \"Tutorial\" FINDER_SELF");
	run("SHOW_MISSION_OBJECTIVES_ENTRY SELECT_UNITS FINDER_SELF");
	run("SHOW_MISSION_OBJECTIVES_ENTRY SPAWN_A_PEASANT FINDER_SELF");
	step(5);
	auto entryState = [&](const std::string& key) {
		for (const auto& [k, s] : client.objectiveEntries)
			if (k == key) return (int)s;
		return -1;
	};
	check(client.missionObjectives == tutorial, "the client has the Tutorial objectives");
	check(entryState("SELECT_UNITS") == MissionObjectives::SHOWN && entryState("SPAWN_A_PEASANT") == MissionObjectives::SHOWN
		&& entryState("BUILD_A_VILLAGE") == MissionObjectives::HIDDEN, "two entries shown, the others hidden");
	const uint32_t changed = client.objectivesChanged;
	run("HIDE_MISSION_OBJECTIVES_ENTRY SELECT_UNITS FINDER_SELF");
	run("SHOW_MISSION_OBJECTIVES_ENTRY_INACTIVE SPAWN_A_PEASANT FINDER_SELF");
	step(5);
	check(entryState("SELECT_UNITS") == MissionObjectives::HIDDEN && entryState("SPAWN_A_PEASANT") == MissionObjectives::INACTIVE,
		"an entry hidden, one inactive");
	check(client.objectivesChanged == changed + 1, "news counted for the inactive entry, not for the hidden one");

	// a text window: its layout read, its pages turned by its buttons, closed by OK
	const int camera = server.gameSet->gameTextWindows.names.getIndex("Tutorial 1 Camera");
	if (camera < 0) {
		printf("UNITTEST FAILED (no \"Tutorial 1 Camera\")\n");
		return;
	}
	const GameTextWindow& gtw = server.gameSet->gameTextWindows[camera];
	check(std::abs(gtw.x - 0.2f) < 1e-4f && std::abs(gtw.y - 0.8f) < 1e-4f && gtw.width == 550 && gtw.atBottom && gtw.pages.size() == 7,
		"\"Tutorial 1 Camera\": at 0.2, 0.8, 550 wide, at the bottom, 7 pages");
	auto buttonFor = [&](int action) {
		for (size_t i = 0; i < gtw.buttons.size(); i++)
			if (gtw.buttons[i].onClickWindowAction == action) return (int)i;
		return -1;
	};
	const int next = buttonFor(Tags::GTW_BUTTON_WINDOW_ACTION_MOVE_NEXT_PAGE), back = buttonFor(Tags::GTW_BUTTON_WINDOW_ACTION_MOVE_PREVIOUS_PAGE),
		close = buttonFor(Tags::GTW_BUTTON_WINDOW_ACTION_CLOSE_WINDOW);
	check(next >= 0 && back >= 0 && close >= 0 && gtw.buttons[back].x == 250 && gtw.buttons[back].width == 75,
		"its buttons: next, back (at 250, 75 wide) and close");
	run("DISPLAY_GAME_TEXT_WINDOW \"Tutorial 1 Camera\" FINDER_SELF");
	step(5);
	check(client.gtwStates.count(camera) && client.gtwStates[camera] == 0 && client.gtwOrder.count(camera), "shown at its first page, in the order");
	if (next >= 0 && back >= 0 && close >= 0) {
		ui.gameWindowButton(camera, back);
		check(client.gtwStates[camera] == 0, "back on the first page: still the first");
		ui.gameWindowButton(camera, next);
		ui.gameWindowButton(camera, next);
		check(client.gtwStates[camera] == 2, "next twice: the third page");
		ui.gameWindowButton(camera, back);
		check(client.gtwStates[camera] == 1, "back: the second page");
		ui.gameWindowButton(camera, close);
		check(client.gtwStates[camera] == -1, "OK: closed");
	}
	printf(failures ? "UNITTEST FAILED (%d)\n" : "UNITTEST PASSED\n", failures);
}

// Spawn point (needs the game data): player 1027's Manor on 4 Peaks trains two peasants with no
// rally point. As in WKB.exe's Task_Spawn, each comes out at the last point of the Manor's
// nearest CR_ path, outside its footprint, where a click finds the peasant and not the Manor,
// and stays there with no order. Prints "SPAWNPOINT ..." and "SPAWNPOINT PASSED/FAILED".
void Test_SpawnPoint()
{
	LoadBCP("data.bcp");
	Server server;
	server.loadSaveGame("Save_Games\\2 Player 4 Peaks.lvl");
	g_settings["simSeed"] = 4243u;
	server.timeManager.unpause();
	server.startLevel();
	server.timeManager.stepMs = 20;
	ServerGameObject* player = server.findObject(1027);
	ServerGameObject* manor = nullptr;
	auto find = [&](CommonGameObject* obj, auto& self) -> void {
		for (const auto& [type, children] : obj->children)
			for (CommonGameObject* child : children) {
				if (!manor && child->blueprint->name == "Manor" && child->getPlayer() == player)
					manor = child->dyncast<ServerGameObject>();
				self(child, self);
			}
	};
	find(server.getLevel(), find);
	if (!player || !manor) {
		printf("SPAWNPOINT not set up\nSPAWNPOINT FAILED\n");
		return;
	}
	manor->spawnedUnitCommand = -1; // no rally point
	manor->spawnedUnitTarget = -1;
	manor->spawnedUnitDestination = Vector3(-1, -1, -1);
	if (Model* model = manor->getModel())
		for (size_t i = 0; i < model->getNumAPs(); i++)
			if (model->getAPInfo(i).tag.compare(0, 3, "CR_") == 0) {
				const Vector3 p = model->getAPInfo(i).staticState.position.transform(manor->getWorldMatrix());
				printf("SPAWNPOINT %s at (%.1f,%.1f)\n", model->getAPInfo(i).tag.c_str(), p.x, p.z);
			}
	const Vector3 spot = SpawnSpot(manor);
	printf("SPAWNPOINT Manor %u at (%.1f,%.1f), spawn spot (%.1f,%.1f), %.1f m out\n", manor->id, manor->position.x,
		manor->position.z, spot.x, spot.z, (spot - manor->position).len2xz());
	const auto [tw, th] = server.terrain->getNumPlayableTiles();
	auto onManor = [&](const Vector3& p) {
		const int tx = (int)(p.x / 5.0f), tz = (int)(p.z / 5.0f);
		if (tx < 0 || tz < 0 || tx >= tw || tz >= th)
			return false;
		const auto& tile = server.tiles[(size_t)tz * tw + tx];
		return tile.building.getFrom<Server>() == manor && !tile.buildingPassable;
	};
	int failures = 0;
	for (int n = 0; n < 2; n++) {
		ServerGameObject* unit = RallySpawn(server, manor, player);
		if (!unit) {
			printf("SPAWNPOINT peasant %d not trained\n", n);
			failures++;
			continue;
		}
		const Vector3 at = unit->position;
		GatherSaveStep(server, 30);
		Order* o = unit->orderConfig.getCurrentOrder();
		const bool inside = onManor(unit->position);
		const float out = (unit->position - manor->position).len2xz();
		printf("SPAWNPOINT peasant %u came out at (%.1f,%.1f); 30 s later at (%.1f,%.1f), %.1f m from the Manor's centre, on its footprint %d, selectable %d, order %s\n",
			unit->id, at.x, at.z, unit->position.x, unit->position.z, out, (int)inside, (int)((unit->flags & CommonGameObject::fSelectable) != 0),
			o ? server.gameSet->orders.getString(o->blueprint->bpid).c_str() : "none");
		if (inside || out < 1.0f || !(unit->flags & CommonGameObject::fSelectable) || (at - spot).len2xz() > 0.5f)
			failures++;
	}
	printf(failures ? "SPAWNPOINT FAILED (%d)\n" : "SPAWNPOINT PASSED\n", failures);
	fflush(stdout);
}

// Agent control's input path (AgentInput, docs/engine/agent-control.md): the agent's frames give
// the same input state as the same events from the operating system, frame by frame; Ctrl from
// the agent's own keys; the operating system's input dropped while the agent has it; the
// take-over key; nothing changes with agent control off
void Test_AgentInput()
{
	int failures = 0;
	auto check = [&](bool ok, const std::string& what) {
		if (!ok) failures++;
		printf("UNITTEST %s: %s\n", ok ? "ok" : "FAILED", what.c_str());
		fflush(stdout);
	};
	SDL_Init(SDL_INIT_EVENTS);
	ResetAllKeyBindings();
	auto resetInput = []() {
		std::fill(std::begin(g_keyDown), std::end(g_keyDown), false);
		std::fill(std::begin(g_mouseDown), std::end(g_mouseDown), false);
		g_mouseX = g_mouseY = 0;
		SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);
	};
	// the input state after a frame (the modifiers apart: the operating system's come from its
	// keyboard state, which pushed events don't change)
	struct State {
		int x, y, wheel, dbl;
		std::string text;
		std::vector<bool> keyDown, keyPressed, mouseDown, mousePressed, mouseReleased;
		bool operator==(const State&) const = default;
	};
	auto snapshot = []() {
		State s{ g_mouseX, g_mouseY, g_mouseWheel, g_mouseDoubleClicked, g_textTyped };
		s.keyDown.assign(std::begin(g_keyDown), std::end(g_keyDown));
		s.keyPressed.assign(std::begin(g_keyPressed), std::end(g_keyPressed));
		s.mouseDown.assign(std::begin(g_mouseDown), std::end(g_mouseDown));
		s.mousePressed.assign(std::begin(g_mousePressed), std::end(g_mousePressed));
		s.mouseReleased.assign(std::begin(g_mouseReleased), std::end(g_mouseReleased));
		return s;
	};
	auto describe = [](const State& s) {
		std::string d = "mouse " + std::to_string(s.x) + "," + std::to_string(s.y);
		for (int b = 1; b < 6; b++)
			d += std::string(" b") + std::to_string(b) + (s.mouseDown[b] ? "D" : "") + (s.mousePressed[b] ? "P" : "") + (s.mouseReleased[b] ? "R" : "");
		return d + " wheel " + std::to_string(s.wheel) + " dbl " + std::to_string(s.dbl) + " text \"" + s.text + "\"";
	};
	// a person's scene: a click, a double click, a box drag, a right-drag, a wheel, keys, text
	auto scene = []() {
		AgentInput::Click(400, 300);
		AgentInput::Wait(2);
		AgentInput::DoubleClick(410, 305);
		AgentInput::Drag(100, 100, 300, 250);
		AgentInput::Drag(500, 400, 560, 380, SDL_BUTTON_RIGHT, 4);
		AgentInput::Wheel(-2);
		AgentInput::Key(SDL_SCANCODE_W, true);
		AgentInput::Wait(3);
		AgentInput::Key(SDL_SCANCODE_W, false);
		AgentInput::Tap(SDL_SCANCODE_P);
		AgentInput::Text("gg wp \xc3\xa4");
	};

	// the scene's events, filled in as the agent delivers them
	AgentInput::SetEnabled(true);
	scene();
	const size_t frames = AgentInput::PendingFrames();
	std::vector<std::vector<SDL_Event>> events;
	std::vector<std::vector<std::string>> texts;
	std::vector<SDL_Event> f;
	while (AgentInput::TakeFrame(f)) {
		events.push_back(f);
		texts.emplace_back();
		for (const SDL_Event& e : f)
			texts.back().push_back(e.type == SDL_EVENT_TEXT_INPUT ? e.text.text : "");
	}
	AgentInput::SetEnabled(false);
	check(frames == events.size() && frames == 3 + 2 + 5 + 9 + 7 + 1 + 1 + 3 + 1 + 2 + 7,
		"the scene in frames as a hand takes them: " + std::to_string(frames));

	// (A) through the operating system's queue, agent control off
	resetInput();
	std::vector<State> viaSystem;
	for (size_t i = 0; i < events.size(); i++) {
		for (size_t j = 0; j < events[i].size(); j++) {
			SDL_Event e = events[i][j];
			if (e.type == SDL_EVENT_TEXT_INPUT)
				e.text.text = texts[i][j].c_str();
			SDL_PushEvent(&e);
		}
		HandleWindow();
		viaSystem.push_back(snapshot());
	}
	// (B) the same scene from the agent
	resetInput();
	AgentInput::SetEnabled(true);
	scene();
	std::vector<State> viaAgent;
	for (size_t i = 0; i < events.size(); i++) {
		HandleWindow();
		viaAgent.push_back(snapshot());
	}
	size_t firstDiff = viaSystem.size();
	for (size_t i = 0; i < viaSystem.size() && i < viaAgent.size(); i++)
		if (!(viaSystem[i] == viaAgent[i])) { firstDiff = i; break; }
	check(viaSystem.size() == viaAgent.size() && firstDiff == viaSystem.size(), "the agent's scene gives the same input state as the system's, every one of "
		+ std::to_string(viaSystem.size()) + " frames" + (firstDiff < viaSystem.size() ? " (frame " + std::to_string(firstDiff) + ": system "
			+ describe(viaSystem[firstDiff]) + ", agent " + describe(viaAgent[firstDiff]) + ")" : ""));
	check(viaAgent[1].mousePressed[SDL_BUTTON_LEFT] && viaAgent[2].mouseReleased[SDL_BUTTON_LEFT] && viaAgent[0].x == 400 && !viaAgent[0].mousePressed[SDL_BUTTON_LEFT],
		"a click: the cursor there first, pressed the frame after, released the one after that");
	check(viaAgent[8].dbl == SDL_BUTTON_LEFT, "the double click's second press is a double click");
	check(viaAgent[11].mouseDown[SDL_BUTTON_LEFT] && viaAgent[17].mouseDown[SDL_BUTTON_LEFT] && viaAgent[18].mouseReleased[SDL_BUTTON_LEFT]
		&& viaAgent[18].x == 300 && viaAgent[18].y == 250, "the box drag: held over the moves, released at its end");
	std::string typed;
	for (const State& s : viaAgent)
		typed += s.text;
	check(typed == "gg wp \xc3\xa4", "the text typed, a character a frame (a two-byte one whole): \"" + typed + "\"");
	check(AgentInput::PendingFrames() == 0, "every frame delivered");

	// Ctrl from the agent's own keys: Ctrl+3 stores control group 3, a Shift+click
	resetInput();
	AgentInput::Tap(SDL_SCANCODE_3, KeyChord::CTRL);
	HandleWindow();
	check(g_modCtrl && (InputModState() & SDL_KMOD_LCTRL), "the agent holds Left Ctrl: Ctrl is held for the game and ImGui");
	HandleWindow();
	check(g_modCtrl && KeyPressed(KeyAction::GroupStore3) && !KeyPressed(KeyAction::GroupSelect3), "the agent's Ctrl+3 stores control group 3");
	HandleWindow();
	HandleWindow();
	check(!g_modCtrl && !g_keyDown[SDL_SCANCODE_LCTRL], "Ctrl let go after the chord");
	AgentInput::Key(SDL_SCANCODE_LSHIFT, true);
	AgentInput::Click(50, 60);
	AgentInput::Key(SDL_SCANCODE_LSHIFT, false);
	bool shiftClick = false;
	for (int i = 0; i < 5; i++) {
		HandleWindow();
		shiftClick |= g_mousePressed[SDL_BUTTON_LEFT] && g_modShift;
	}
	check(shiftClick && !g_modShift, "a Shift+click: Shift held while the button goes down, let go after");

	// the operating system's input dropped while the agent has it
	resetInput();
	AgentInput::Move(10, 20);
	HandleWindow();
	auto pushSystem = [](SDL_EventType type, int a = 0, int mods = 0) {
		SDL_Event e{};
		e.type = type;
		if (type == SDL_EVENT_MOUSE_MOTION) { e.motion.x = 700; e.motion.y = 500; }
		if (type == SDL_EVENT_MOUSE_BUTTON_DOWN || type == SDL_EVENT_MOUSE_BUTTON_UP) { e.button.button = (Uint8)a; e.button.down = type == SDL_EVENT_MOUSE_BUTTON_DOWN; e.button.clicks = 1; }
		if (type == SDL_EVENT_KEY_DOWN || type == SDL_EVENT_KEY_UP) { e.key.scancode = (SDL_Scancode)a; e.key.mod = (SDL_Keymod)mods; e.key.down = type == SDL_EVENT_KEY_DOWN; }
		SDL_PushEvent(&e);
	};
	pushSystem(SDL_EVENT_MOUSE_MOTION);
	pushSystem(SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_LEFT);
	pushSystem(SDL_EVENT_KEY_DOWN, SDL_SCANCODE_DELETE);
	HandleWindow();
	check(g_mouseX == 10 && g_mouseY == 20 && !g_mousePressed[SDL_BUTTON_LEFT] && !g_keyPressed[SDL_SCANCODE_DELETE],
		"the agent has the input: the system's mouse move, click and Delete are dropped (the cursor stays at the agent's 10,20)");

	// the take-over key: the person at the keyboard takes the input, the agent's queue waits
	AgentInput::Button(SDL_BUTTON_LEFT, true);
	HandleWindow();
	check(g_mouseDown[SDL_BUTTON_LEFT], "the agent holds the left button down");
	AgentInput::Click(80, 90);
	const size_t waiting = AgentInput::PendingFrames();
	const int handOvers = AgentInput::HandOverCount();
	pushSystem(SDL_EVENT_KEY_DOWN, SDL_SCANCODE_F12, SDL_KMOD_LCTRL | SDL_KMOD_LALT);
	HandleWindow();
	check(AgentInput::TakenOver() && !AgentInput::HasInput() && AgentInput::HandOverCount() == handOvers + 1, "Ctrl+Alt+F12 from the keyboard: the person takes the input over");
	check(!g_mouseDown[SDL_BUTTON_LEFT] && !g_keyPressed[SDL_SCANCODE_F12] && !KeyPressed(KeyAction::FrameTimings),
		"the agent's held button let go; the take-over key reaches nothing else (not Ctrl+F12's frame timings)");
	pushSystem(SDL_EVENT_MOUSE_MOTION);
	pushSystem(SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_LEFT);
	HandleWindow();
	check(g_mouseX == 700 && g_mousePressed[SDL_BUTTON_LEFT] && AgentInput::PendingFrames() == waiting,
		"taken over: the system's mouse works, the agent's " + std::to_string(waiting) + " frames wait");
	pushSystem(SDL_EVENT_MOUSE_BUTTON_UP, SDL_BUTTON_LEFT);
	pushSystem(SDL_EVENT_KEY_DOWN, SDL_SCANCODE_F12, SDL_KMOD_LCTRL | SDL_KMOD_LALT);
	HandleWindow();
	check(AgentInput::HasInput() && AgentInput::PendingFrames() == waiting - 1, "the key again: the input back to the agent, its queue going on");
	HandleWindow();
	HandleWindow();
	check(g_mouseReleased[SDL_BUTTON_LEFT] && g_mouseX == 80, "the agent's waiting click done at 80,90");
	// rebound: the take-over follows the binding
	SetKeyBinding(KeyAction::AgentTakeOver, 0, KeyChord{ SDL_SCANCODE_PAUSE, KeyChord::SHIFT });
	pushSystem(SDL_EVENT_KEY_DOWN, SDL_SCANCODE_F12, SDL_KMOD_LCTRL | SDL_KMOD_LALT);
	HandleWindow();
	check(AgentInput::HasInput(), "rebound to Shift+Pause: Ctrl+Alt+F12 is dropped like any key");
	pushSystem(SDL_EVENT_KEY_DOWN, SDL_SCANCODE_PAUSE, SDL_KMOD_RSHIFT);
	HandleWindow();
	check(AgentInput::TakenOver(), "rebound to Shift+Pause: Shift+Pause takes over");
	ResetKeyBinding(KeyAction::AgentTakeOver);

	// off: the queue gone, the system's keys as ever (Ctrl+Alt+F12 an ordinary key)
	AgentInput::SetEnabled(false);
	AgentInput::Click(1, 1);
	resetInput();
	check(!AgentInput::HasInput(), "agent control off: the agent hasn't the input");
	pushSystem(SDL_EVENT_KEY_DOWN, SDL_SCANCODE_F12, SDL_KMOD_LCTRL | SDL_KMOD_LALT);
	HandleWindow();
	HandleWindow();
	HandleWindow();
	check(g_keyDown[SDL_SCANCODE_F12] && g_mouseX == 0 && !AgentInput::TakenOver(), "off: Ctrl+Alt+F12 reaches the game as a key; no agent click happens");
	AgentInput::ClearQueue();
	resetInput();

	// the human limits: 120 actions a minute (one each 500 ms), a reaction delay of 300 ms
	AgentInput::SetEnabled(true);
	AgentInput::SetLimits(120, 0);
	AgentInput::Click(10, 10);
	AgentInput::Click(20, 20);
	AgentInput::Tap(SDL_SCANCODE_P, KeyChord::CTRL);
	std::vector<uint64_t> downs;
	const uint64_t begin = SDL_GetTicks();
	while (AgentInput::PendingFrames() && SDL_GetTicks() - begin < 5000) {
		std::vector<SDL_Event> evs;
		if (AgentInput::TakeFrame(evs))
			for (const SDL_Event& e : evs)
				if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN || (e.type == SDL_EVENT_KEY_DOWN && e.key.scancode == SDL_SCANCODE_P))
					downs.push_back(SDL_GetTicks());
		SDL_Delay(5);
	}
	check(downs.size() == 3 && downs[1] - downs[0] >= 495 && downs[2] - downs[1] >= 495,
		"120 actions a minute: two clicks and Ctrl+P at least 500 ms apart (" + std::to_string(downs.size() > 2 ? downs[1] - downs[0] : 0) + ", "
		+ std::to_string(downs.size() > 2 ? downs[2] - downs[1] : 0) + " ms; Ctrl itself no action)");
	AgentInput::SetLimits(0, 300);
	const uint64_t asked = SDL_GetTicks();
	const uint64_t firstSerial = AgentInput::QueuedFrames() + 1;
	AgentInput::Click(30, 30);
	AgentInput::HoldFrom(firstSerial, asked + AgentInput::ReactionMs());
	uint64_t moved = 0;
	while (AgentInput::PendingFrames() && SDL_GetTicks() - asked < 3000) {
		std::vector<SDL_Event> evs;
		if (AgentInput::TakeFrame(evs) && !moved)
			moved = SDL_GetTicks();
		SDL_Delay(5);
	}
	check(moved && moved - asked >= 295 && moved - asked < 400, "a reaction delay of 300 ms: the request's first frame " + std::to_string(moved - asked) + " ms later");
	AgentInput::SetLimits(0, 0);
	AgentInput::SetEnabled(false);
	resetInput();
	SDL_QuitSubSystem(SDL_INIT_EVENTS);
	printf(failures ? "UNITTEST FAILED (%d)\n" : "UNITTEST PASSED\n", failures);
}

// a function only the build identity test changes in memory (its first byte, then put back)
#ifdef _MSC_VER
__declspec(noinline)
#endif
int BuildIdentityTestTarget(int a) { return a * 7 + 3; }

// The build check (BuildIdentity, Server::buildPolicy): the code hash, the challenge answers, a
// code byte changed in memory, the policies' verdicts
void Test_BuildIdentity()
{
	int failures = 0;
	auto check = [&](bool ok, const std::string& what) {
		if (!ok) failures++;
		printf("UNITTEST %s: %s\n", ok ? "ok" : "FAILED", what.c_str());
		fflush(stdout);
	};
	using namespace BuildIdentity;
	printf("This build: %s, code %s, network %s\n", Describe().c_str(), CodeHash().c_str(), MatchmakingGameVersion());
	check(CodeHash().size() == 64 && CodeHash().find_first_not_of("0123456789abcdef") == std::string::npos, "the code hash is a SHA-256 in hex");
	check(std::string(Commit()) != "unknown" && strlen(Commit()) == 12, std::string("the commit from git: ") + Commit());
	const std::string a1 = ChallengeAnswer("1234"), a2 = ChallengeAnswer("1234"), b = ChallengeAnswer("1235");
	check(a1 == a2 && a1 != b && a1 != CodeHash() && a1.size() == 64, "the same challenge, the same answer; another challenge, another answer");
	check(ChallengeAnswer("").empty(), "no challenge, no answer");
#ifdef _WIN32
	// a copy changed in memory: one byte of a function's code
	uint8_t* code = (uint8_t*)&BuildIdentityTestTarget;
	DWORD old = 0;
	if (VirtualProtect(code, 1, PAGE_EXECUTE_READWRITE, &old)) {
		const uint8_t was = code[0];
		code[0] ^= 0x5a;
		const std::string changed = ChallengeAnswer("1234");
		code[0] = was;
		VirtualProtect(code, 1, old, &old);
		check(changed != a1 && !changed.empty(), "one byte of the code changed in memory: another answer to the same challenge");
		check(ChallengeAnswer("1234") == a1, "the byte put back: the first answer again");
	}
	else
		check(false, "VirtualProtect on the test function");
	check(BuildIdentityTestTarget(2) == 17, "the test function still works");
#endif

	// the verdicts
	const nlohmann::json host = Json();
	nlohmann::json guest = host;
	check(Compare(SAME_VERSION, host, guest, false).empty(), "the same build: fits the default policy (no answer needed)");
	check(Compare(IDENTICAL, host, guest, true).empty(), "identical with the right answer: fits");
	check(!Compare(IDENTICAL, host, guest, false).empty(), "identical policy, a wrong answer: turned away: " + Compare(IDENTICAL, host, guest, false));
	guest["commit"] = "0123456789ab";
	check(!Compare(SAME_VERSION, host, guest, false).empty() && Compare(SAME_NETWORK, host, guest, false).empty(),
		"another commit: turned away by the default policy, fits 'any of this network version': " + Compare(SAME_VERSION, host, guest, false));
	guest = host;
	guest["modified"] = true;
	guest["code"] = std::string(64, 'f');
	check(!Compare(SAME_VERSION, host, guest, false).empty(), "the same commit, modified, other code: turned away by the default policy");
	nlohmann::json hostModified = host, guestModified = host;
	hostModified["modified"] = guestModified["modified"] = true;
	check(Compare(SAME_VERSION, hostModified, guestModified, false).empty(), "both modified with the very same code (a developer's two copies): fit");
	guest = host;
	guest["net"] = "wkbai-net-1";
	check(!Compare(SAME_NETWORK, host, guest, true).empty(), "another network version: turned away under every policy: " + Compare(SAME_NETWORK, host, guest, true));
	check(!Compare(SAME_NETWORK, host, nlohmann::json(), true).empty(), "an older wkbai that doesn't say its build: turned away: " + Compare(SAME_NETWORK, host, nlohmann::json(), true));
	const nlohmann::json manifest = nlohmann::json::parse(ModManifest(), nullptr, false);
	check(manifest.is_object() && manifest.value("build", nlohmann::json()) == host, "the mod manifest a guest sends carries its build");

	// agent control in saves: the players an agent played, through the save's settings text
	{
		Server a;
		a.agentPlayersEver[1027] = "Aldric";
		a.agentPlayersEver[1030] = "Guest Two";
		a.allowAgents = false;
		const std::string text = a.aiSettingsText();
		Server b;
		b.applyAiSettingsText(text);
		check(b.agentPlayersEver == a.agentPlayersEver, "a save keeps the players an agent played (" + std::to_string(b.agentPlayersEver.size()) + ", names with spaces)");
		Server c;
		check(c.allowAgents && c.agentPlayersEver.empty(), "a new game: agent players allowed, none yet");
		c.clientInfos.resize(3);
		c.clientInfos[1].name = "Bob";
		check(c.clientDisplayName(1) == "Bob" && c.clientDisplayName(2) == "Player 3", "agent announcements name the player as the lobby did, else by place");
	}
	printf(failures ? "UNITTEST FAILED (%d)\n" : "UNITTEST PASSED\n", failures);
}
// The smaller items as WKB.exe's (the RE repo's docs/re/combat-smaller-items.md): the sight scan
// once a second, the "Dangerous Buildings" map and a scout's path round a tower seen, a wheeling
// unit's arcs, the stop spacing by PREFERRED_SPACING
void Test_SmallerItems()
{
	int failures = 0;
	auto check = [&](bool ok, const std::string& what) {
		if (!ok) failures++;
		printf("UNITTEST %s: %s\n", ok ? "ok" : "FAILED", what.c_str());
		fflush(stdout);
	};
	LoadBCP("data.bcp");
	Server server;
	server.loadSaveGame("Save_Games\\2 Player 4 Peaks.lvl");
	ServerGameObject* player = server.findObject(1027);
	ServerGameObject* enemyPlayer = server.findObject(1030);
	const GameSet& gs = *server.gameSet;
	const GameObjBlueprint* scoutBp = gs.findBlueprint(Tags::GAMEOBJCLASS_CHARACTER, "Scout");
	const GameObjBlueprint* towerBp = gs.findBlueprint(Tags::GAMEOBJCLASS_BUILDING, "Watch Tower");
	if (!player || !enemyPlayer || !scoutBp || !towerBp) {
		printf("UNITTEST FAILED (not set up)\n");
		return;
	}
	server.timeManager.unpause();
	auto run = [&](float seconds) {
		const float until = server.timeManager.currentTime + seconds;
		const uint64_t start = SDL_GetTicks();
		while (server.timeManager.currentTime < until && SDL_GetTicks() - start < (uint64_t)(seconds * 4000.0f)) {
			server.tick();
			SDL_Delay(5);
		}
	};
	// 1. the sight scan: each receiver about once a second
	{
		extern uint64_t g_sightCalls;
		int receivers = 0;
		for (const auto& [id, o] : server.idmap)
			if (o->blueprint->receiveSightRangeEvents && !(o->flags & CommonGameObject::fTerminated))
				receivers++;
		run(1.0f);
		const uint64_t before = g_sightCalls;
		const float t0 = server.timeManager.currentTime;
		run(5.0f);
		const float secs = server.timeManager.currentTime - t0;
		const double perSecond = (double)(g_sightCalls - before) / std::max(1, receivers) / std::max(0.1f, secs);
		printf("UNITTEST sight scans: %d receivers, %.2f scans each a second over %.1f s\n", receivers, perSecond, secs);
		check(receivers > 0 && perSecond > 0.2 && perSecond < 1.4, "each unit is scanned about once a second (at most; some skip it by their conditions)");
	}
	// 2. "Dangerous Buildings": the player's map (as the Kingmaker AI's), a tower of the enemy's
	// seen by its scout, the scout's path round it
	{
		const_cast<GameObjBlueprint*>(player->blueprint)->createsIntensityMaps.push_back("Dangerous Buildings");
		ServerGameObject* scout = server.spawnObject(scoutBp, player, {}, {});
		// open ground: the scout's tile and 120 m east of it free
		Vector3 from = player->position;
		for (const auto& [id, o] : server.idmap)
			if (o->blueprint->bpClass == Tags::GAMEOBJCLASS_CHARACTER && o->getPlayer() == player) { from = o->position; break; }
		from = MovementController::nearestValidPosition(scoutBp, from);
		scout->setPosition(from);
		const Vector3 to = MovementController::nearestValidPosition(scoutBp, from + Vector3(120.0f, 0, 0));
		const Vector3 mid = (from + to) * 0.5f;
		ServerGameObject* tower = server.spawnObject(towerBp, enemyPlayer, mid, {});
		tower->setPosition(mid);
		run(2.5f); // seen; the maps made again
		const std::vector<int32_t>* cost = server.intensityMaps.costFor(scout);
		const auto [w, h] = server.terrain->getNumPlayableTiles();
		const int tx = (int)(mid.x / 5.0f), tz = (int)(mid.z / 5.0f);
		const int here = cost ? (*cost)[(size_t)tz * w + tx] : 0;
		printf("UNITTEST Dangerous Buildings: the tower's tile costs %d more for the scout\n", here);
		check(cost && here > 0, "a tower seen marks the player's Dangerous Buildings map, which the scout applies");
		scout->movementController.startMovement(to);
		run(0.5f);
		float nearest = 1e9f;
		const auto& nodes = scout->movementController.m_pathNodes;
		for (size_t i = 0; i + 1 < nodes.size(); i++) {
			// the nearest the leg between two nodes comes to the tower
			const Vector3 a = nodes[i + 1], b = nodes[i];
			const Vector3 ab = b - a;
			const float l2 = std::max(1e-4f, ab.sqlen2xz());
			const float t = std::clamp(((mid - a).x * ab.x + (mid - a).z * ab.z) / l2, 0.0f, 1.0f);
			nearest = std::min(nearest, (a + ab * t - mid).len2xz());
		}
		printf("UNITTEST the scout's path: %zu nodes, at least %.1f m from the tower\n", nodes.size(), nearest);
		check(nodes.size() > 2 && nearest > 10.0f, "the scout's path goes round the tower");
		scout->movementController.stopMovement();
		// another player's units without the map: none
		check(server.intensityMaps.costFor(tower) == nullptr, "a type that applies no map has no extra cost");
	}
	// 3. turning: a wheeling type with a turn radius of 3 m or more walks an arc to a point behind it
	{
		const GameObjBlueprint* wheeler = nullptr;
		const auto& characters = gs.objBlueprints[Tags::GAMEOBJCLASS_CHARACTER];
		for (size_t i = 0; i < characters.size() && !wheeler; i++)
			if (characters[i].wheelOnFacing && characters[i].idealTurnRadius >= 3.0f && !characters[i].canWalkOnWater())
				wheeler = &characters[i];
		if (!wheeler)
			check(false, "a wheeling type with a large turn radius in the game set");
		else {
			ServerGameObject* unit = server.spawnObject(wheeler, player, {}, {});
			const Vector3 at = MovementController::nearestValidPosition(wheeler, player->position + Vector3(0, 0, 40.0f));
			unit->setPosition(at);
			unit->setOrientation(Vector3(0, 0, 0)); // facing north (-z)
			const Vector3 behind = MovementController::nearestValidPosition(wheeler, at + Vector3(0, 0, 30.0f));
			unit->movementController.startMovement(behind);
			run(0.2f);
			const size_t n = unit->movementController.m_pathNodes.size();
			printf("UNITTEST %s (turn radius %.1f): %zu nodes to a point 30 m behind it\n", wheeler->name.c_str(), wheeler->idealTurnRadius, n);
			check(n > 3, "it turns on an arc instead of on the spot");
			unit->movementController.stopMovement();
		}
	}
	// 4. stop spacing: two units sent to the same rally point keep their PREFERRED_SPACING apart
	{
		ServerGameObject* a = server.spawnObject(scoutBp, player, {}, {});
		ServerGameObject* b = server.spawnObject(scoutBp, player, {}, {});
		const Vector3 place = MovementController::nearestValidPosition(scoutBp, player->position + Vector3(-60.0f, 0, 0));
		a->setPosition(FreeStandingSpot(a, place));
		const Vector3 sb = FreeStandingSpot(b, place);
		const float need = 2.0f * scoutBp->preferredSpacing;
		printf("UNITTEST spacing: the second scout's spot %.2f m from the first (spacing %.2f each)\n", (sb - a->position).len2xz(), scoutBp->preferredSpacing);
		check((sb - a->position).len2xz() >= need - 0.01f, "the second keeps both spacings from the first");
	}
	// 5. stop spots (WKB.exe: no unit collisions; a walk ends at a spot with PREFERRED_SPACING clear
	// of the others'): a scout sent onto a standing one ends its Move next to it, not on top of it
	{
		const int move = gs.commands.names.getIndex("Move");
		ServerGameObject* stander = server.spawnObject(scoutBp, player, {}, {});
		ServerGameObject* walker = server.spawnObject(scoutBp, player, {}, {});
		const Vector3 spot = MovementController::nearestValidPosition(scoutBp, player->position + Vector3(0, 0, -60.0f));
		stander->setPosition(spot);
		walker->setPosition(MovementController::nearestValidPosition(scoutBp, spot + Vector3(25.0f, 0, 0)));
		gs.commands[move].execute(walker, nullptr, Tags::ORDERASSIGNMODE_FORGET_EVERYTHING_ELSE, spot);
		run(15.0f);
		const float d = (walker->position - stander->position).len2xz();
		const float need = 2.0f * scoutBp->preferredSpacing;
		printf("UNITTEST stop spot: the walker stopped %.2f m from the standing scout (spacings %.2f), its order %s, moving %d\n", d,
			need, walker->orderConfig.getCurrentOrder() ? "on" : "done", (int)walker->movementController.isMoving());
		check(d >= need - 0.1f && d < need + 3.0f && !walker->movementController.isMoving(), "a unit sent onto another stops at the free spot next to it");
		check(!walker->orderConfig.getCurrentOrder(), "its Move ends there");
	}
	// 6. an uninterruptible loop (the Rocket Launcher's limbering): a Move given meanwhile waits
	// behind it; the launcher limbers, then moves
	if (const GameObjBlueprint* rlBp = gs.findBlueprint(Tags::GAMEOBJCLASS_CHARACTER, "Rocket Launcher")) {
		const int limber = gs.commands.names.getIndex("Limber"), move = gs.commands.names.getIndex("Move");
		ServerGameObject* rl = server.spawnObject(rlBp, player, {}, {});
		const Vector3 at = MovementController::nearestValidPosition(rlBp, player->position + Vector3(40.0f, 0, 40.0f));
		rl->setPosition(at);
		run(0.5f);
		gs.commands[limber].execute(rl, nullptr, Tags::ORDERASSIGNMODE_FORGET_EVERYTHING_ELSE, {});
		run(1.0f);
		const Vector3 goal = MovementController::nearestValidPosition(rlBp, at + Vector3(30.0f, 0, 0));
		gs.commands[move].execute(rl, nullptr, Tags::ORDERASSIGNMODE_FORGET_EVERYTHING_ELSE, goal);
		const size_t orders = rl->orderConfig.orders.size();
		const Order* front = rl->orderConfig.getCurrentOrder();
		const std::string frontName = front ? gs.orders.getString(front->blueprint) : "-";
		printf("UNITTEST limbering: after the Move, %zu orders, the current %s\n", orders, frontName.c_str());
		check(orders >= 2 && frontName == "Limber", "a Move during the limbering waits behind it");
		run(12.0f);
		printf("UNITTEST then: %s, %.1f m from where it limbered\n", rl->blueprint->name.c_str(), (rl->position - at).len2xz());
		check(rl->blueprint->name == "Rocket Launcher Limbered" && (rl->position - at).len2xz() > 5.0f, "it limbers, then moves");
	}
	else
		check(false, "the Rocket Launcher in the game set");
	printf(failures ? "UNITTEST FAILED (%d)\n" : "UNITTEST PASSED\n", failures);
}


// Night cold (the day's temperature swing, ServerEnvironment::update): on 4 Peaks in midsummer,
// a clear day and night of 20 minutes, one update a second: the coldest shortly before sunrise,
// the warmest in the early afternoon, the night colder than the afternoon by a summer's swing,
// smooth from second to second; an overcast day's swing smaller; a heatwave's nights warm (more
// than its 8 degrees' pull above the clear night, the swing smaller); without day and night the
// season's temperature all day. A save and load goes on with the same temperatures, and a save
// from before the day's mean (no "temperature" line) goes on without a jump. Prints "NIGHTCOLD
// ok/FAILED ..." and "NIGHTCOLD PASSED/FAILED".
void Test_NightCold()
{
	int failures = 0;
	auto check = [&](bool ok, const std::string& what) {
		if (!ok) failures++;
		printf("NIGHTCOLD %s: %s\n", ok ? "ok" : "FAILED", what.c_str());
		fflush(stdout);
	};
	LoadBCP("data.bcp");
	Server server;
	server.loadSaveGame("Save_Games\\2 Player 4 Peaks.lvl");
	ServerEnvironment& env = server.environment;
	const float t0 = 1000.0f;
	auto setUp = [&](bool dayNight, int weather) {
		env.reset();
		server.timeManager.currentTime = t0;
		env.settings.seasons = true;
		env.settings.seasonLengthMin = 600; // (the season's own drift negligible over a day)
		env.settings.dayNight = dayNight;
		env.settings.dayLengthMin = 20;
		for (int s = 0; s < 4; s++)
			env.settings.severity[s] = 1;
		env.forceWeather = weather;
		env.debugSet(server, "season", 1.5f); // midsummer
		env.debugSet(server, "hour", 20.0f);  // the evening: the clouds settle before midnight
	};
	auto step = [&]() {
		server.timeManager.currentTime += 1.0f;
		env.update(server);
		return env.state.temperatureC;
	};
	// a whole day from midnight: the temperature by the hour (each second's), its coldest and warmest
	struct Day { float lo = 1e9f, hi = -1e9f, loHour = 0, hiHour = 0, maxStep = 0, at2 = 0, at14 = 0, sunrise = 0; };
	auto runDay = [&](bool dayNight, int weather) {
		setUp(dayNight, weather);
		env.update(server); // the first update (started)
		// (to midnight; without day and night the clock stands at noon: a day's length on from here)
		while (dayNight && EnvDayFraction(env.settings, server.timeManager.currentTime + 1.0f) > 0.01f)
			step();
		Day d;
		float prev = env.state.temperatureC;
		const float len = env.settings.dayLengthS();
		for (int i = 0; i < (int)len; i++) {
			const float T = step();
			const float hour = env.state.dayFraction * 24.0f;
			if (T < d.lo) d.lo = T, d.loHour = hour;
			if (T > d.hi) d.hi = T, d.hiHour = hour;
			d.maxStep = std::max(d.maxStep, std::fabs(T - prev));
			prev = T;
			if (std::fabs(hour - 2.0f) < 0.03f) d.at2 = T;
			if (std::fabs(hour - 14.0f) < 0.03f) d.at14 = T;
		}
		d.sunrise = ComputeSun(env.state.dayFraction, env.state.yearFraction, true, 0.0f).sunrise;
		return d;
	};
	char what[300];
	const Day clear = runDay(true, WEATHER_CLEAR);
	snprintf(what, sizeof(what), "clear midsummer: coldest %.1f C at %05.2f h (sunrise %05.2f), warmest %.1f C at %05.2f h, 02:00 %.1f, 14:00 %.1f, step at most %.3f",
		clear.lo, clear.loHour, clear.sunrise, clear.hi, clear.hiHour, clear.at2, clear.at14, clear.maxStep);
	check(clear.loHour > clear.sunrise - 1.5f && clear.loHour <= clear.sunrise + 0.1f && clear.hiHour > 13.0f && clear.hiHour < 16.0f, std::string("the turns: ") + what);
	check(clear.at14 - clear.at2 > 8.0f && clear.hi - clear.lo > 11.0f && clear.hi - clear.lo < 16.0f, "a summer's swing (11-16 degrees, the night colder than the afternoon)");
	check(clear.maxStep < 0.1f, "smooth (under 0.1 degrees a second)");
	const Day cloudy = runDay(true, WEATHER_CLOUDY);
	snprintf(what, sizeof(what), "cloudy midsummer: %.1f to %.1f C (swing %.1f against the clear day's %.1f)", cloudy.lo, cloudy.hi, cloudy.hi - cloudy.lo, clear.hi - clear.lo);
	check(cloudy.hi - cloudy.lo < 0.8f * (clear.hi - clear.lo) && cloudy.hi - cloudy.lo > 3.0f, what);
	const Day heat = runDay(true, WEATHER_HEATWAVE);
	snprintf(what, sizeof(what), "heatwave midsummer: %.1f C at %05.2f h to %.1f C at %05.2f h (swing %.1f; the clear night's %.1f, its swing %.1f)",
		heat.lo, heat.loHour, heat.hi, heat.hiHour, heat.hi - heat.lo, clear.lo, clear.hi - clear.lo);
	check(heat.lo - clear.lo > 8.0f + 3.0f && heat.hi - heat.lo < 0.8f * (clear.hi - clear.lo) && heat.hi > clear.hi && heat.maxStep < 0.1f,
		std::string("warm nights: ") + what);
	const Day flat = runDay(false, WEATHER_CLEAR);
	snprintf(what, sizeof(what), "day and night off: %.2f to %.2f C all day", flat.lo, flat.hi);
	check(flat.hi - flat.lo < 0.2f, what);

	// the save: at 03:00 of a clear day
	setUp(true, WEATHER_CLEAR);
	env.update(server);
	while (std::fabs(EnvDayFraction(env.settings, server.timeManager.currentTime) * 24.0f - 3.0f) > 0.05f)
		step();
	const float savedTime = server.timeManager.currentTime;
	const std::string text = env.saveText();
	std::vector<float> ahead;
	for (int i = 0; i < 120; i++)
		ahead.push_back(step());
	check(text.find("\ntemperature ") != std::string::npos, "the day's mean in the save");
	env.loadText(text);
	check(env.saveText() == text, "saved, loaded and saved again: the same text");
	server.timeManager.currentTime = savedTime;
	float worst = 0.0f;
	for (int i = 0; i < 120; i++)
		worst = std::max(worst, std::fabs(step() - ahead[i]));
	snprintf(what, sizeof(what), "loaded: the same temperatures on (at most %.4f off over 2 minutes)", worst);
	check(worst < 1e-3f, what);
	// a save from before the day's mean
	std::string old;
	{
		std::istringstream in(text);
		std::string line;
		while (std::getline(in, line))
			if (line.rfind("temperature ", 0) != 0)
				old += line + "\n";
	}
	env.loadText(old);
	server.timeManager.currentTime = savedTime;
	worst = 0.0f;
	for (int i = 0; i < 120; i++)
		worst = std::max(worst, std::fabs(step() - ahead[i]));
	snprintf(what, sizeof(what), "an older save (no day's mean): goes on with at most %.3f degrees off", worst);
	check(worst < 0.05f, what);
	printf("NIGHTCOLD %s (%d failed)\n", failures ? "FAILED" : "PASSED", failures);
}
void Test_ShaderCache(); // gfx/ShaderCacheTest.cpp
void Test_FarLandCache(); // FarLandCacheTest.cpp

// The original's keywords only mods use (RE repo docs/re/keyword-coverage.md): each one read
// without a warning and doing what WKB.exe's does (or what its name says, where the original
// has an evident bug)
void Test_ModKeywords()
{
	int failures = 0;
	auto check = [&](bool ok, const std::string& what) {
		if (!ok) failures++;
		printf("UNITTEST %s: %s\n", ok ? "ok" : "FAILED", what.c_str());
		fflush(stdout);
	};
	LoadBCP("data.bcp");
	Server server;
	Client client(true);
	NetLocalBuffer b1, b2;
	NetLocalLink srv(&b1, &b2), cli(&b2, &b1);
	server.addClient(&srv);
	client.serverLink = &cli;
	server.loadSaveGame("Save_Games\\2 Player 4 Peaks.lvl");
	ServerGameObject* player = server.findObject(1027);
	ServerGameObject* enemyPlayer = server.findObject(1030);
	const GameSet& gs = *server.gameSet;
	if (!player || !enemyPlayer) {
		printf("UNITTEST FAILED (not set up)\n");
		return;
	}
	server.setClientPlayerControl(0, 1027);
	server.timeManager.unpause();
	auto step = [&](int frames) {
		for (int i = 0; i < frames; i++) {
			server.tick();
			client.tick();
			SDL_Delay(2);
		}
	};
	step(10);
	// the player's characters and buildings
	std::vector<ServerGameObject*> units, buildings;
	for (const auto& [id, o] : server.idmap) {
		if (o->flags & CommonGameObject::fTerminated || o->getPlayer() != player)
			continue;
		if (o->blueprint->bpClass == Tags::GAMEOBJCLASS_CHARACTER)
			units.push_back((ServerGameObject*)o);
		else if (o->blueprint->bpClass == Tags::GAMEOBJCLASS_BUILDING && o->blueprint->footprint)
			buildings.push_back((ServerGameObject*)o);
	}
	if (units.size() < 3 || buildings.empty()) {
		printf("UNITTEST FAILED (%zu units, %zu buildings)\n", units.size(), buildings.size());
		return;
	}
	ServerGameObject* unit = units[0];
	auto value = [&](ServerGameObject* self, const std::string& line) {
		SrvScriptContext ctx(&server, self);
		GSFileParser gsf(line.c_str());
		std::unique_ptr<ValueDeterminer> vd(ReadValueDeterminer(gsf, gs));
		return vd->eval(&ctx);
	};
	auto finder = [&](ServerGameObject* self, const std::string& text, bool node) {
		SrvScriptContext ctx(&server, self);
		GSFileParser gsf(text.c_str());
		std::unique_ptr<ObjectFinder> f(node ? ReadFinderNode(gsf, gs) : ReadFinder(gsf, gs));
		return f->eval(&ctx);
	};
	auto position = [&](ServerGameObject* self, const std::string& line) {
		SrvScriptContext ctx(&server, self);
		GSFileParser gsf(line.c_str());
		std::unique_ptr<PositionDeterminer> pd(PositionDeterminer::createFrom(gsf, gs));
		return pd->eval(&ctx);
	};
	auto run = [&](ServerGameObject* self, const std::string& line) {
		SrvScriptContext ctx(&server, self);
		GSFileParser gsf(line.c_str());
		std::unique_ptr<Action> act(ReadAction(gsf, gs));
		act->run(&ctx);
	};

	// ---- values
	{
		const std::string tag = gs.valueTags.names.getString(0);
		const std::string one = " FINDER_SPECIFIC_ID " + std::to_string(units[0]->id);
		const float v0 = value(player, "VALUE_TAG_INTERPRETATION \"" + tag + "\"" + one);
		check(value(player, "TOTAL_VALUE_TAG_INTERPRETATION \"" + tag + "\"" + one) == v0
			&& value(player, "AVERAGE_VALUE_TAG_INTERPRETATION \"" + tag + "\"" + one) == v0
			&& value(player, "MAX_VALUE_TAG_INTERPRETATION \"" + tag + "\"" + one) == v0
			&& value(player, "MIN_VALUE_TAG_INTERPRETATION \"" + tag + "\"" + one) == v0,
			"TOTAL/AVERAGE/MAX/MIN_VALUE_TAG_INTERPRETATION of one object: its value");
		check(value(unit, "WITHIN_ARC FINDER_SELF FINDER_SPECIFIC_ID " + std::to_string(units[1]->id) + " CONSTANT 0 CONSTANT 360 CONSTANT 0") == 1.0f,
			"WITHIN_ARC with a full circle: in it");
		// a point straight behind the unit is outside a 30-degree arc ahead, inside one turned by 180
		ServerGameObject* other = units[1];
		const Vector3 saved = other->position;
		const float yaw = unit->orientation.y;
		other->position = unit->position - Vector3(std::sin(yaw), 0, -std::cos(yaw)) * 10.0f;
		const std::string pair = "FINDER_SELF FINDER_SPECIFIC_ID " + std::to_string(other->id);
		check(value(unit, "WITHIN_ARC " + pair + " CONSTANT 0 CONSTANT 30 CONSTANT 0") == 0.0f
			&& value(unit, "WITHIN_ARC " + pair + " CONSTANT 180 CONSTANT 30 CONSTANT 0") == 1.0f
			&& value(unit, "WITHIN_ARC " + pair + " CONSTANT 180 CONSTANT 30 CONSTANT 5") == 0.0f,
			"WITHIN_ARC: behind is outside the arc ahead, inside the arc turned round, out of a 5 m range");
		check(value(unit, "WITHIN_ARC_TOWARDS FINDER_SELF FINDER_SPECIFIC_ID " + std::to_string(other->id) + " FINDER_SPECIFIC_ID " + std::to_string(other->id) + " CONSTANT 10 CONSTANT 0") == 1.0f,
			"WITHIN_ARC_TOWARDS: the target itself is in the arc towards it");
		other->position = saved;
		check(value(unit, "IS_FORMED FINDER_SELF") == 0.0f && value(unit, "FORMATION_DIMENSION_MAX FINDER_SELF") == 0.0f,
			"IS_FORMED and FORMATION_DIMENSION_MAX of a unit outside a formation: 0");
		const float discovered = value(player, "NUM_DISCOVERED_BUILDINGS FINDER_SELF FINDER_SPECIFIC_ID " + std::to_string(enemyPlayer->id));
		check(discovered >= 0.0f, "NUM_DISCOVERED_BUILDINGS: " + std::to_string((int)discovered) + " of the enemy's buildings");
		const std::string commission = gs.commissions.names.getString(0);
		const float possible = value(player, "COMMISSION_POSSIBLE \"" + commission + "\" FINDER_SPECIFIC_ID " + std::to_string(buildings[0]->id));
		check(possible == 0.0f || possible == 1.0f, "COMMISSION_POSSIBLE \"" + commission + "\": " + std::to_string((int)possible));
		// LENGTH_OF_PATH_REMAINING: a walk 60 m away, then standing
		const Vector3 to = MovementController::nearestValidPosition(unit->blueprint, unit->position + Vector3(60.0f, 0, 0));
		unit->movementController.startMovement(to);
		step(30);
		const float walking = value(unit, "LENGTH_OF_PATH_REMAINING FINDER_SELF");
		unit->movementController.stopMovement();
		const float standing = value(unit, "LENGTH_OF_PATH_REMAINING FINDER_SELF");
		check(walking > 1.0f && walking < 80.0f && standing == 0.0f,
			"LENGTH_OF_PATH_REMAINING: " + std::to_string(walking) + " m walking, 0 standing");
	}

	// ---- finders
	{
		const std::string around = "FINDER_METRE_RADIUS CONSTANT 200 CHARACTERS_ONLY";
		const auto all = finder(unit, "\n" + around + "\n", true);
		const auto two = finder(unit, "\nFINDER_GRADE_SELECT_CANDIDATES BY_LOWEST DISTANCE_BETWEEN HORIZONTAL FINDER_SELF FINDER_CANDIDATE CONSTANT 2\n" + around + "\n", true);
		bool nearestFirst = two.size() == 2;
		if (nearestFirst)
			for (CommonGameObject* o : all)
				if ((o->position - unit->position).sqlen2xz() < (two[0]->position - unit->position).sqlen2xz())
					nearestFirst = false;
		check(all.size() > 2 && nearestFirst, "GRADE_SELECT_CANDIDATES BY_LOWEST distance, 2: the two nearest of " + std::to_string(all.size()));
		const auto kept = finder(unit, "\nFINDER_CULL_CANDIDATES CONSTANT 0\n" + around + "\n", true);
		const auto culled = finder(unit, "\nFINDER_CULL_CANDIDATES CONSTANT 1\n" + around + "\n", true);
		check(kept.size() == all.size() && culled.empty(), "CULL_CANDIDATES keeps those for which the value isn't above 0");
		const auto me = finder(unit, "FINDER_CHAIN_ORIGINAL_SELF", false);
		check(me.size() == 1 && me[0] == unit, "CHAIN_ORIGINAL_SELF outside a chain: self");
		const auto nearby = finder(unit, "FINDER_WITHIN_HORIZ_DISTANCE CONSTANT 30", false);
		bool inside = true;
		for (CommonGameObject* o : nearby)
			if (o == unit || (o->position - unit->position).sqlen2xz() > 30.0f * 30.0f)
				inside = false;
		check(inside, "WITHIN_HORIZ_DISTANCE 30: " + std::to_string(nearby.size()) + " objects, all within 30 m on the ground, not self");
	}

	// ---- positions
	{
		check(!position(unit, "ATTACHMENT_POINT_BY_INDEX \"No Such Point\" FINDER_SELF CONSTANT 0").found,
			"ATTACHMENT_POINT_BY_INDEX: no point of that tag, no position");
		check(!position(unit, "POINT_ALONG_PATH FINDER_SELF LOCATION_OF FINDER_SELF LOCATION_OF FINDER_SELF CONSTANT 5").found,
			"POINT_ALONG_PATH: read, no position (as WKB.exe's)");
	}

	// ---- actions
	{
		// SET_TILE_STATUS: a tile blocked for the way and for buildings, never unblocked
		const Vector3 spot = MovementController::nearestValidPosition(unit->blueprint, unit->position + Vector3(25.0f, 0, 25.0f));
		const int tx = (int)(spot.x / 5.0f), tz = (int)(spot.z / 5.0f);
		const std::string at = "OFFSET_FROM FINDER_SELF CONSTANT " + std::to_string(spot.x - unit->position.x) + " CONSTANT 0 CONSTANT " + std::to_string(spot.z - unit->position.z);
		run(unit, "SET_TILE_STATUS BLOCKED " + at);
		run(unit, "SET_TILE_STATUS PASSABLE " + at);
		step(3);
		const auto [w, h] = server.terrain->getNumPlayableTiles();
		check(server.tiles[(size_t)tz * w + tx].scriptStatus == 0 && MovementController::isTileBlocked(unit->blueprint, tx, tz)
			&& client.tiles[(size_t)tz * w + tx].scriptStatus == 0,
			"SET_TILE_STATUS BLOCKED: the tile blocked (server and client); PASSABLE doesn't unblock it");
		// SUSPEND_ORDER / RESUME_ORDER
		ServerGameObject* walker = units[1];
		const Vector3 away = MovementController::nearestValidPosition(walker->blueprint, walker->position + Vector3(-50.0f, 0, 0));
		const OrderBlueprint* moveOrder = nullptr;
		for (size_t i = 0; i < gs.orders.size() && !moveOrder; i++)
			if (gs.orders[i].classType == Tags::ORDTSKTYPE_MOVE)
				moveOrder = &gs.orders[i];
		bool suspendedHeld = false, resumed = false;
		if (moveOrder) {
			Order* order = walker->orderConfig.addOrder(moveOrder, Tags::ORDERASSIGNMODE_DO_FIRST, nullptr, away);
			step(5);
			run(walker, "SUSPEND_ORDER FINDER_SELF");
			step(20);
			suspendedHeld = order && walker->orderConfig.getCurrentOrder() && walker->orderConfig.getCurrentOrder()->scriptSuspended
				&& walker->orderConfig.getCurrentOrder()->state == OTS_SUSPENDED;
			run(walker, "RESUME_ORDER FINDER_SELF");
			step(3);
			resumed = walker->orderConfig.getCurrentOrder() && !walker->orderConfig.getCurrentOrder()->scriptSuspended
				&& walker->orderConfig.getCurrentOrder()->state == OTS_PROCESSING;
			run(walker, "REEVALUATE_TASK_PROXIMITY FINDER_SELF");
			run(walker, "ABORT_TASK FINDER_SELF");
		}
		check(moveOrder && suspendedHeld && resumed, "SUSPEND_ORDER holds the order over 20 steps; RESUME_ORDER takes it up again");
		// shadows, the looped sound, the save screen, the objectives' picture
		run(buildings[0], "DISABLE_SHADOW FINDER_SELF");
		step(3);
		ClientGameObject* cb = client.findObject(buildings[0]->id);
		const bool off = cb && cb->shadowSwitch == 0;
		run(buildings[0], "ENABLE_SHADOW FINDER_SELF");
		step(3);
		check(off && cb->shadowSwitch == 1, "DISABLE_SHADOW / ENABLE_SHADOW: the building's static shadow off, on again");
		const std::string sound = gs.soundTags.names.getString(0);
		run(unit, "PLAY_LOOPED_SOUND \"" + sound + "\" FINDER_SELF FINDER_PLAYER");
		step(3);
		const size_t loops = client.loopedSounds.size();
		run(unit, "STOP_SOUND FINDER_SELF");
		step(3);
		check(loops == 1 && client.loopedSounds.empty(), "PLAY_LOOPED_SOUND loops until STOP_SOUND");
		run(unit, "DISPLAY_SAVE_GAME_MENU");
		run(player, "SET_ACTIVE_MISSION_OBJECTIVES_MAP_GRAPHIC \"Maps\\Test.pcx\" FINDER_SELF");
		step(3);
		check(client.saveMenuRequested && client.objectivesMapGraphic == "Maps\\Test.pcx",
			"DISPLAY_SAVE_GAME_MENU and SET_ACTIVE_MISSION_OBJECTIVES_MAP_GRAPHIC reach the player's client");
		// RESET_FOG_OF_WAR after REVEAL_FOG_OF_WAR
		ServerGameObject* enemyThing = nullptr;
		for (const auto& [id, o] : server.idmap)
			if (o->getPlayer() == enemyPlayer && o->blueprint->bpClass == Tags::GAMEOBJCLASS_BUILDING) { enemyThing = (ServerGameObject*)o; break; }
		bool revealed = false, reset = false;
		if (enemyThing) {
			const std::string obj = "FINDER_SPECIFIC_ID " + std::to_string(enemyThing->id);
			run(player, "REVEAL_FOG_OF_WAR " + obj + " CONSTANT 0 CONSTANT 3 FINDER_SELF");
			step(3);
			for (const auto& r : client.fogReveals) revealed |= r.object == enemyThing->id;
			run(player, "RESET_FOG_OF_WAR " + obj + " FINDER_SELF");
			step(3);
			reset = true;
			for (const auto& r : client.fogReveals) reset &= r.object != enemyThing->id;
		}
		check(revealed && reset, "RESET_FOG_OF_WAR ends REVEAL_FOG_OF_WAR's view");
		// FLOODFILL_AREA_WITH: one object per tile of the square
		const GameObjBlueprint* fill = nullptr;
		if (gs.objBlueprints[Tags::GAMEOBJCLASS_MARKER].size() > 0)
			fill = &gs.objBlueprints[Tags::GAMEOBJCLASS_MARKER][0];
		size_t before = 0, after = 0;
		if (fill) {
			for (const auto& [id, o] : server.idmap) before += o->blueprint == fill && !(o->flags & CommonGameObject::fTerminated);
			run(unit, "FLOODFILL_AREA_WITH MARKER \"" + fill->name + "\" LOCATION_OF FINDER_SELF CONSTANT 4");
			for (const auto& [id, o] : server.idmap) after += o->blueprint == fill && !(o->flags & CommonGameObject::fTerminated);
		}
		check(fill && after - before == 9, "FLOODFILL_AREA_WITH, half size 4 m: " + std::to_string(after - before) + " markers, one on each of 3 x 3 tiles");
		// REPEAT_SEQUENCE_AFTER_DELAY: count runs queued
		const size_t queued = server.delayedSequences.size();
		run(unit, "REPEAT_SEQUENCE_AFTER_DELAY \"" + gs.actionSequences.names.getString(0) + "\" FINDER_SELF CONSTANT 3 CONSTANT 100");
		check(server.delayedSequences.size() == queued + 3, "REPEAT_SEQUENCE_AFTER_DELAY 3 after 100 s: three runs queued");
		// SWITCH_FOOTPRINT: the building's tiles follow the new footprint
		ServerGameObject* b = buildings[0];
		auto held = [&]() { size_t n = 0; for (size_t i = 0; i < (size_t)w * h; i++) n += server.tiles[i].building == b->id; return n; };
		const size_t heldBefore = held();
		std::string otherFp;
		for (size_t i = 0; i < gs.footprints.size(); i++)
			if (&gs.footprints[i] != b->blueprint->footprint && gs.footprints[i].tiles.size() != b->blueprint->footprint->tiles.size()) {
				otherFp = gs.footprints.names.getString(i);
				break;
			}
		run(b, "SWITCH_FOOTPRINT \"" + otherFp + "\"");
		const size_t heldAfter = held();
		check(!otherFp.empty() && heldAfter != heldBefore && b->footprintOverride != nullptr,
			"SWITCH_FOOTPRINT \"" + otherFp + "\": the building holds " + std::to_string(heldAfter) + " tiles, " + std::to_string(heldBefore) + " before");
		// read and run without effect or with effects not checked here
		run(unit, "ASSERT CONSTANT 1 \"never printed\"");
		run(unit, "DISPLAY_ON_MINIMAP FINDER_SELF CONSTANT 1 CONSTANT 2 FINDER_SELF");
		run(unit, "SET_AMBIENT_LIGHTING 10 20 30");
		run(unit, "SWITCH_OFF_INTENSITY_MAP \"Threat\" FINDER_SELF");
		run(b, "DEREGISTER_WORK_ORDER FINDER_RESULTS \"" + gs.objectFinderDefinitions.names.getString(0) + "\" FINDER_SELF");
		check(true, "ASSERT, DISPLAY_ON_MINIMAP, SET_AMBIENT_LIGHTING, SWITCH_OFF_INTENSITY_MAP, DEREGISTER_WORK_ORDER run");
	}
	printf("UNITTEST %s: %d failures\n", failures ? "FAILED" : "PASSED", failures);
}


// Several farmers, several farms (the play test 2026-11-16): three peasants each given Build on
// three new farm foundations, the first FORGET_EVERYTHING_ELSE, the others DO_LAST, as Shift-placed
// farms with the three selected. In the original each finished farm takes its nearest builder
// (Building and Repairing.cpp "Only 1 Builder", Farming.cpp "Auto-attend Farm",
// FORGET_EVERYTHING_ELSE), who farms it; the others go on to the next. Prints "FARMB t=<s>" with
// each peasant's order and target every 10 s, and at the end how many farm a field.
void Test_FarmBuilders()
{
	LoadBCP("data.bcp");
	Server server;
	server.loadSaveGame("Save_Games\\2 Player 4 Peaks.lvl");
	ServerGameObject* player = server.findObject(1027);
	std::vector<ServerGameObject*> peasants;
	auto find = [&](CommonGameObject* obj, auto& self) -> void {
		for (const auto& [type, children] : obj->children)
			for (CommonGameObject* child : children) {
				if (child->blueprint->name == "Peasant" && peasants.size() < 3)
					peasants.push_back(child->dyncast<ServerGameObject>());
				self(child, self);
			}
	};
	if (player)
		find(player, find);
	const GameObjBlueprint* foundationType = server.gameSet->findBlueprint(Tags::GAMEOBJCLASS_BUILDING, "Standard Farm Foundation");
	const int build = server.gameSet->commands.names.getIndex("Build");
	if (peasants.size() < 3 || !foundationType || build < 0) {
		printf("FARMB not set up (%zu peasants)\n", peasants.size());
		return;
	}
	g_settings["simSeed"] = 1;
	server.startLevel();
	std::vector<SrvGORef> farms;
	const Vector3 base = peasants[0]->position;
	// WKB_FARMB_FARMS=<n>: n farms instead of 3
	const size_t farmCount = getenv("WKB_FARMB_FARMS") ? (size_t)std::max(1, atoi(getenv("WKB_FARMB_FARMS"))) : 3;
	for (int i = 0; i < 800 && farms.size() < farmCount; i++) {
		const Vector3 at = base + Vector3(20.0f + 5.0f * (i % 20), 0, -40.0f + 5.0f * (i / 20));
		if (StampdownPlan::getStampdownPlan(&server, player, foundationType, at, Vector3(0, 0, 0)).status != StampdownPlan::Status::Valid)
			continue;
		ServerGameObject* f = server.spawnObject(foundationType, player, at, Vector3(0, 0, 0));
		f->sendEvent(Tags::PDEVENT_ON_STAMPDOWN);
		farms.emplace_back(f);
	}
	if (farms.size() < farmCount) {
		printf("FARMB no room\n");
		return;
	}
	server.timeManager.unpause();
	server.timeManager.stepMs = 20;
	for (size_t f = 0; f < farms.size(); f++)
		for (ServerGameObject* p : peasants)
			server.gameSet->commands[build].execute(p, farms[f].get(), f == 0 ? Tags::ORDERASSIGNMODE_FORGET_EVERYTHING_ELSE : Tags::ORDERASSIGNMODE_DO_LAST);
	auto describe = [&](ServerGameObject* p) {
		std::string order = "-", target = "-";
		if (Order* o = p->orderConfig.getCurrentOrder()) {
			order = server.gameSet->orders.getString(o->blueprint->bpid);
			if (Task* tk = o->getCurrentTask(); tk && tk->target)
				target = tk->target->blueprint->name + " #" + std::to_string(tk->target->id);
		}
		std::string queue;
		for (Order& o : p->orderConfig.orders)
			queue += (queue.empty() ? "" : ",") + server.gameSet->orders.getString(o.blueprint->bpid) + (o.isDone() ? "(done)" : "");
		return order + " -> " + target + " [" + queue + "]";
	};
	for (int step = 0; step * 20 <= 400000; step++) {
		server.timeManager.pendingMs = server.timeManager.stepMs;
		if (server.timeManager.step())
			server.simulate();
		if (step % 500)
			continue;
		printf("FARMB t=%d", step * 20 / 1000);
		for (ServerGameObject* p : peasants)
			printf(" | %u: %s", p->id, describe(p).c_str());
		printf("\n");
	}
	int farming = 0;
	for (ServerGameObject* p : peasants)
		if (Order* o = p->orderConfig.getCurrentOrder(); o && server.gameSet->orders.getString(o->blueprint->bpid) == "Farm")
			farming++;
	printf("FARMB end: %d of 3 peasants farm\n", farming);
	fflush(stdout);
}

IRenderer* CreateNULLRenderer();

// The wind (Wind.h): the lobby's settings and their text, the target by the weather and the
// settings, the veering's bounds, the gusts' mean and range, the names, the wind chill and the
// fires' factor, the save's round trip; all without the simulation's random numbers
void Test_Wind()
{
	bool all = true;
	auto check = [&](const char* what, bool ok) { printf("WIND %s %d\n", what, ok ? 1 : 0); all &= ok; };

	// off by default (a game from before, the campaign): no text, nothing to send
	EnvironmentSettings off;
	check("off by default", off.wind == 0 && off.text().find("wind=") == std::string::npos);
	EnvironmentSettings es;
	for (auto [n, v] : { std::pair<const char*, float>{ "wind", 1 }, { "windFrom", 7 }, { "windStrength", 4 }, { "windVeers", 0 }, { "windEffects", 1 }, { "windSeed", 4242 } })
		check(n, es.apply(n, v));
	EnvironmentSettings back;
	{
		const std::string text = es.text();
		size_t at = 0;
		while (at < text.size()) {
			const size_t end = text.find('\n', at), eq = text.find('=', at);
			if (eq != std::string::npos && eq < end)
				back.apply(text.substr(at, eq - at), (float)atof(text.c_str() + eq + 1));
			at = end == std::string::npos ? text.size() : end + 1;
		}
	}
	check("settings text round trip", back.wind == 1 && back.windFrom == 7 && back.windStrength == 4 && !back.windVeers && back.windEffects && back.windSeed == 4242);
	check("clamped", [] { EnvironmentSettings c; c.apply("windFrom", 12); c.apply("windStrength", -3); return c.windFrom == 8 && c.windStrength == 0; }());

	// fixed: from the west (7), not veering: it blows to the east (+x), the same at any time
	{
		const WindState a = WindTarget(es, WEATHER_CLEAR, 10.0f), b = WindTarget(es, WEATHER_BLIZZARD, 5000.0f);
		check("from the west blows east", std::fabs(a.dirX - 1.0f) < 1e-4f && std::fabs(a.dirZ) < 1e-4f && std::fabs(b.dirX - a.dirX) < 1e-6f);
		check("compass W", strcmp(WindCompass(a.dirX, a.dirZ), "W") == 0);
		// Strong (4): 11 m/s, +-15 %, whatever the weather
		check("fixed strength", a.speed > 9.3f && a.speed < 12.7f && b.speed > 9.3f && b.speed < 12.7f);
	}
	EnvironmentSettings n = es;
	n.windFrom = 1;
	check("compass N", strcmp(WindCompass(WindTarget(n, 0, 0).dirX, WindTarget(n, 0, 0).dirZ), "N") == 0
		&& WindTarget(n, 0, 0).dirZ < -0.999f);
	// by the weather, random, veering: fog calmer than a blizzard, always; the direction within
	// 90 + 35 + 12 degrees of the west-south-west; deterministic
	EnvironmentSettings w;
	w.wind = 1;
	w.windSeed = 777;
	bool calmer = true, bounded = true, same = true;
	float minDot = 1.0f;
	for (int i = 0; i < 400; i++) {
		const float t = i * 37.0f;
		const WindState fog = WindTarget(w, WEATHER_FOG, t), bliz = WindTarget(w, WEATHER_BLIZZARD, t);
		calmer &= fog.speed < 2.0f && bliz.speed > 11.0f;
		const float dot = fog.dirX * WIND_DEFAULT_X + fog.dirZ * WIND_DEFAULT_Z;
		minDot = std::min(minDot, dot);
		bounded &= dot > std::cos((90.0f + 35.0f + 12.0f) * 3.14159265f / 180.0f) - 1e-3f;
		same &= WindTarget(w, WEATHER_FOG, t).speed == fog.speed;
	}
	check("fog calmer than a blizzard", calmer);
	check("direction bounded", bounded);
	check("deterministic", same);
	{
		EnvironmentSettings steady = w;
		steady.windVeers = false;
		const WindState a = WindTarget(steady, WEATHER_RAIN, 0), b = WindTarget(steady, WEATHER_RAIN, 3000);
		const Vector3 p = WindPrevailing(steady);
		check("steady keeps its way", std::fabs(a.dirX - b.dirX) < 1e-6f && std::fabs(a.dirX - p.x) < 1e-5f && std::fabs(a.dirZ - p.z) < 1e-5f);
	}
	// the gusts: mean about 1, never below 0.3, squalls only when gusty
	{
		double sum = 0, sumCalm = 0;
		float lo = 9, hi = 0, hiCalm = 0;
		const int N = 20000;
		for (int i = 0; i < N; i++) {
			const float t = i * 0.37f;
			const float g = WindGust(t, 0.6f, 99), c = WindGust(t, 0.1f, 99);
			sum += g; sumCalm += c;
			lo = std::min(lo, g); hi = std::max(hi, g); hiCalm = std::max(hiCalm, c);
		}
		printf("WIND gusts: mean %.3f (calm %.3f), %.2f .. %.2f (calm up to %.2f)\n", sum / N, sumCalm / N, lo, hi, hiCalm);
		check("gust mean", std::fabs(sum / N - 1.0) < 0.12 && std::fabs(sumCalm / N - 1.0) < 0.05);
		check("gust range", lo >= 0.3f && hi > 1.5f && hiCalm < 1.3f);
	}
	check("speed names", strcmp(WindSpeedName(0.2f), "calm") == 0 && strcmp(WindSpeedName(5.0f), "gentle breeze") == 0
		&& strcmp(WindSpeedName(30.0f), "storm") == 0);

	// the server's: the chill and the fires' factor by the speed, none with the effects off
	{
		ServerEnvironment env;
		env.settings.wind = 1;
		env.wind.speed = 17.0f;
		const float chill = env.windChill(), fire = env.windFireFactor();
		printf("WIND at 17 m/s: chill %.2f, fire factor %.2f\n", chill, fire);
		check("gale chills", chill > 4.0f && chill < 5.5f && std::fabs(fire - 17.0f / 8.0f) < 1e-4f);
		env.settings.windEffects = false;
		check("no effects", env.windChill() == 0.0f && env.windFireFactor() == 0.0f);
		env.settings.windEffects = true;
		env.settings.wind = 0;
		check("off no effects", env.windChill() == 0.0f && env.windFireFactor() == 0.0f);
		// the save: the settings and the wind as it is
		env.settings = es;
		env.wind.dirX = 0.6f; env.wind.dirZ = -0.8f; env.wind.speed = 12.5f; env.wind.gustiness = 0.45f;
		ServerEnvironment loaded;
		loaded.loadText(env.saveText());
		check("save round trip", loaded.settings.wind == 1 && loaded.settings.windSeed == 4242 && loaded.settings.windFrom == 7
			&& std::fabs(loaded.wind.dirX - 0.6f) < 1e-6f && std::fabs(loaded.wind.dirZ + 0.8f) < 1e-6f
			&& std::fabs(loaded.wind.speed - 12.5f) < 1e-5f && std::fabs(loaded.wind.gustiness - 0.45f) < 1e-6f);
	}
	// the graphics settings: their menu choices and defaults
	{
		const MenuOption* o = FindMenuOption("windTrees");
		check("windTrees option", o && o->choices.size() == 3 && RenderSettings().windTrees == 2);
		const MenuOption* g = FindMenuOption("grassWind");
		check("grassWind option", g && g->choices.size() == 3 && RenderSettings().grassWind == 2);
	}
	// boats: before the wind faster, into it slower, across a little faster; a gale's limits
	{
		WindState w;
		w.dirX = 1.0f; w.dirZ = 0.0f; w.speed = 8.0f;
		const float before = WindBoatFactor(Vector3(10, 0, 0), w), into = WindBoatFactor(Vector3(-10, 0, 0), w), across = WindBoatFactor(Vector3(0, 0, 5), w);
		printf("WIND boats at 8 m/s: before %.2f, across %.2f, into %.2f\n", before, across, into);
		check("boats by the wind", std::fabs(before - 1.25f) < 1e-3f && std::fabs(across - 1.12f) < 1e-3f && std::fabs(into - 0.65f) < 1e-3f);
		w.speed = 30.0f;
		check("boats in a gale", std::fabs(WindBoatFactor(Vector3(-1, 0, 0), w) - 0.35f) < 1e-4f && std::fabs(WindBoatFactor(Vector3(1, 0, 0), w) - 1.5f) < 1e-4f);
		ServerEnvironment env;
		check("boats without the wind", env.windBoatFactor(Vector3(-1, 0, 0)) == 1.0f);
	}
	// the wind field's map: 1 before a build and outside, bilinear between the tiles' middles
	{
		WindShelterMap m;
		check("shelter neutral", m.at(10, 10) == 1.0f);
		m.width = 2; m.height = 1;
		m.value = { 85, 255 }; // 0.5, 1.5
		const float mid = m.at(5.0f, 2.5f), left = m.at(2.5f, 2.5f), out = m.at(-50.0f, 2.5f);
		check("shelter bilinear", std::fabs(mid - 1.0f) < 1e-3f && std::fabs(left - 0.5f) < 1e-3f && std::fabs(out - 0.5f) < 1e-3f);
	}
	printf("WIND %s\n", all ? "PASSED" : "FAILED");
}

void Test_FireQuality()
{
	bool all = true;
	auto check = [&](const char* what, bool ok) { printf("FIRE %s %d\n", what, ok ? 1 : 0); all &= ok; };

	// the option: its values, the menu's choice, the defaults and the clamping
	check("names", strcmp(FireFx::QualityName(0), "Original") == 0 && strcmp(FireFx::QualityName(1), "Low") == 0
		&& strcmp(FireFx::QualityName(2), "High") == 0 && strcmp(FireFx::QualityName(9), "High") == 0);
	check("default", RenderSettings().fireQuality == FireFx::HIGH);
	const RenderSettings savedRender = g_renderSettings;
	const nlohmann::json savedSettings = g_settings;
	const MenuOption* o = FindMenuOption("fireQuality");
	check("menu option", o && o->type == MenuOption::CHOICE && o->page == "lighting"
		&& o->choices == std::vector<std::string>{ "Original", "Low", "High" });
	if (o) {
		bool ok = true;
		for (int v = 0; v < 3; v++) {
			o->set((float)v);
			ok &= g_renderSettings.fireQuality == v && o->get() == (float)v;
		}
		o->set(7.0f);
		ok &= g_renderSettings.fireQuality == 2;
		o->set(-3.0f);
		ok &= g_renderSettings.fireQuality == 0;
		check("menu set and get", ok);
	}
	// the config's round trip (wkconfig.json, as the other quality options: a graphics setting,
	// not the game's, so not in the save game)
	{
		bool ok = true;
		for (int v = 0; v < 3; v++) {
			g_renderSettings.fireQuality = v;
			g_renderSettings.save();
			ok &= g_settings.value("fireQuality", -1) == v;
			g_renderSettings.fireQuality = -1;
			g_renderSettings.load();
			ok &= g_renderSettings.fireQuality == v;
			// and from the file itself
			std::ifstream f("wkconfig.json");
			nlohmann::json written = nlohmann::json::parse(f, nullptr, false);
			ok &= written.is_object() && written.value("fireQuality", -1) == v;
		}
		check("config round trip", ok);
		g_settings["fireQuality"] = 9;
		g_renderSettings.load();
		const bool high = g_renderSettings.fireQuality == 2;
		g_settings["fireQuality"] = -4;
		g_renderSettings.load();
		const bool low = g_renderSettings.fireQuality == 0;
		g_settings.erase("fireQuality");
		g_renderSettings.load();
		check("config clamped, missing is the default", high && low && g_renderSettings.fireQuality == RenderSettings().fireQuality);
	}
	g_settings = savedSettings;
	g_renderSettings = savedRender;
	SaveSettings();

	// which systems the fire shader takes: the flame sprites' (a building's fire, an arrow's, the
	// fireballs), not the smoke, the smoke-puff fire nor the fort's grapeshot sparks
	LoadBCP("data.bcp");
	PSCache cache("Warrior Kings Game Set\\Particle_Systems");
	ParticleSystem* building = cache.getPS("Fire_Building");
	ParticleSystem* arrow = cache.getPS("Flame_Arrow");
	ParticleSystem* fireball = cache.getPS("Fireball");
	ParticleSystem* smoke = cache.getPS("Burningobjectsmoke");
	ParticleSystem* smoky = cache.getPS("Smokyfire");
	ParticleSystem* fort = cache.getPS("Fort_Fire");
	ParticleSystem* dust = cache.getPS("Dust");
	printf("FIRE flags building %d %d arrow %d %d fireball %d smoke %d smoky %d fort %d dust %d\n", building->flameSprite, building->standingFire,
		arrow->flameSprite, arrow->standingFire, fireball->flameSprite, smoke->flameSprite, smoky->flameSprite, fort->flameSprite, dust->flameSprite);
	check("flame systems", building->flameSprite && building->standingFire && arrow->flameSprite && !arrow->standingFire && fireball->flameSprite
		&& !smoke->flameSprite && !smoky->flameSprite && !fort->flameSprite && !dust->flameSprite);
	check("takes", !FireFx::Takes(0, building) && !FireFx::Takes(0, arrow) && FireFx::Takes(1, building) && FireFx::Takes(2, arrow)
		&& !FireFx::Takes(1, smoke) && !FireFx::Takes(2, dust));

	// the level in effect: Original without a renderer that can draw the others
	FireFx::BeginFrame(2, false);
	check("unavailable is Original", FireFx::Level() == FireFx::ORIGINAL);
	FireFx::BeginFrame(1, true);
	check("available", FireFx::Level() == FireFx::LOW);

	// the damage and the flicker
	check("damage scale", FireFx::DamageScale(1.0f) == 1.0f && std::abs(FireFx::DamageScale(0.0f) - 1.6f) < 1e-5f
		&& FireFx::DamageScale(0.3f) > FireFx::DamageScale(0.6f) && FireFx::DamageScale(-1.0f) == FireFx::DamageScale(0.0f));
	FireFx::SetDamage(77, 0.25f);
	check("damage of", std::abs(FireFx::DamageOf(77) - FireFx::DamageScale(0.25f)) < 1e-6f && FireFx::DamageOf(78) == 1.0f);
	{
		bool inRange = true, varies = false;
		const float first = FireFx::Flicker(0.0f, 5);
		for (int i = 0; i < 2000; i++) {
			const float f = FireFx::Flicker(i * 0.0137f, 5);
			inRange &= f >= 0.7f && f <= 1.3f && f == FireFx::Flicker(i * 0.0137f, 5);
			varies |= std::abs(f - first) > 0.1f;
		}
		check("flicker", inRange && varies && FireFx::Flicker(1.0f, 5) != FireFx::Flicker(1.0f, 6));
	}

	// the particles: a burning building's fire (its object 41, damaged), an arrow's flight and the
	// burning object's smoke, 6 s of them
	ParticleContainer pc;
	srand(1234);
	const float dt = 0.05f;
	float now = 0.0f;
	for (int i = 0; i < 120; i++) {
		const float prev = now;
		now += dt;
		pc.generate(building, Vector3(10, 2, 10), 41, prev, now);
		pc.generate(arrow, Vector3(-5.0f + now * 6.0f, 4, 3), 42, prev, now);
		pc.generate(smoke, Vector3(10, 4, 10), 41, prev, now);
		pc.update(now);
	}
	Camera camera;
	camera.position = Vector3(0, 30, -30);
	camera.direction = Vector3(0, -0.6f, 0.8f).normal();
	camera.sceneMatrix = Matrix::getIdentity();
	IRenderer* nul = CreateNULLRenderer();
	DefaultParticleRenderer renderer(nul, &pc);

	// the old path's quads, worked out here as DefaultParticleRenderer always drew a sprite (its
	// position, its scale, the camera's side and up), for the particles of the systems asked
	auto reference = [&](bool fires) {
		std::vector<batchVertex> out;
		const Vector3 camside = camera.direction.cross(Vector3(0, 1, 0)).normal();
		const Vector3 camhor = camside.cross(camera.direction).normal();
		for (auto& kv : pc.particles)
			for (Particle& p : kv.second) {
				const uint32_t color = p.getColor(now);
				if (color >> 24 == 0 || (!fires && p.system->flameSprite))
					continue;
				const Vector3 pos = p.getPosition(now);
				const auto [s1, s2, si] = p.system->Particles.Scale_Gradient.getAtTime(now - p.startTime, 1.0f);
				const float size = s1 + (s2 - s1) * si;
				const Vector3 pts[4] = { pos + (camside + camhor) * size * 0.5f, pos + (camside - camhor) * size * 0.5f,
					pos + (-camside - camhor) * size * 0.5f, pos + (-camside + camhor) * size * 0.5f };
				const float uv[4][2] = { { 0, 0 }, { 0, 1 }, { 1, 1 }, { 1, 0 } };
				for (int k = 0; k < 4; k++)
					out.emplace_back(pts[k].x, pts[k].y, pts[k].z, (int)color, uv[k][0], uv[k][1]);
			}
		return out;
	};
	auto same = [](const std::vector<batchVertex>& a, const std::vector<batchVertex>& b) {
		if (a.size() != b.size())
			return false;
		for (size_t i = 0; i < a.size(); i++)
			if (a[i].x != b[i].x || a[i].y != b[i].y || a[i].z != b[i].z || a[i].color != b[i].color || a[i].u != b[i].u || a[i].v != b[i].v)
				return false;
		return true;
	};
	std::vector<batchVertex> rec;
	DefaultParticleRenderer::s_record = &rec;
	FireFx::BeginFrame(FireFx::ORIGINAL, true);
	renderer.render(now - dt, now, camera);
	const std::vector<batchVertex> original = reference(true);
	size_t flames = 0, visible = 0;
	for (auto& kv : pc.particles)
		for (Particle& p : kv.second)
			if (p.getColor(now) >> 24) {
				visible++;
				flames += p.system->flameSprite;
			}
	printf("FIRE particles %zu visible, %zu of them flames, Original drew %zu quads\n", visible, flames, rec.size() / 4);
	check("Original is the old path", flames > 0 && rec.size() == visible * 4 && same(rec, original) && FireFx::PendingCount() == 0);

	for (int level = FireFx::LOW; level <= FireFx::HIGH; level++) {
		rec.clear();
		FireFx::Clear();
		FireFx::BeginFrame(level, true);
		FireFx::SetDamage(41, 0.2f);
		renderer.render(now - dt, now, camera);
		bool match = FireFx::PendingCount() == flames;
		for (const FireFx::Flame& f : FireFx::Pending())
			match &= f.system->flameSprite && f.alpha > 0 && f.size > 0 && f.life >= 0 && f.life <= 1
				&& (f.objid == 41 ? std::abs(f.heat - FireFx::DamageScale(0.2f)) < 1e-6f : f.heat == 1.0f);
		check(level == 1 ? "Low: the rest as before" : "High: the rest as before", same(rec, reference(false)));
		check(level == 1 ? "Low: the flames to the shader" : "High: the flames to the shader", match);
		std::vector<FireFx::Instance> inst, again;
		FireFx::BuildInstances(level, now, true, inst);
		FireFx::BuildInstances(level, now, true, again);
		int kinds[4] = {};
		int lastKind = -1;
		bool ordered = true;
		const int order[4] = { 2, 3, 1, 0 }; // drawn: the shimmer, the smoke, the flames, the glow
		for (const FireFx::Instance& i : inst) {
			const int k = (int)i.c[0];
			kinds[k]++;
			ordered &= lastKind < 0 || order[lastKind] <= order[k];
			lastKind = k;
		}
		printf("FIRE level %d instances: %d flames, %d glows, %d smoke, %d shimmer\n", level, kinds[0], kinds[1], kinds[2], kinds[3]);
		const bool deterministic = inst.size() == again.size() && memcmp(inst.data(), again.data(), inst.size() * sizeof(FireFx::Instance)) == 0;
		if (level == FireFx::LOW)
			check("Low: flames only", kinds[0] == (int)flames && kinds[1] == 0 && kinds[2] == 0 && kinds[3] == 0 && deterministic);
		else
			// one glow per fire (the building's emitter, the arrow), the shimmer and the smoke over
			// the standing fire only
			check("High: flames, glow, smoke, shimmer", kinds[0] == (int)flames && kinds[1] == 2 && kinds[3] == 1 && kinds[2] >= 3
				&& ordered && deterministic);
	}
	DefaultParticleRenderer::s_record = nullptr;
	FireFx::Clear();
	FireFx::BeginFrame(FireFx::ORIGINAL, false);
	delete nul;
	printf("FIRE all %d\n", all ? 1 : 0);
}

// Rain and snow on burning buildings (ServerEnvironment::updateBuildingFires, redata's "wkbai
// patches\Snow and Fire.cpp"): on 4 Peaks with the seasons on, a barracks of the enemy's far
// from everything, its integrity raised so that it cannot burn down, set alight and its fire
// brought to the most (27, three "Building Fire" effects) by the game set's "Set Fire to Building".
//  - clear spring weather: the fire as the game set burns it (its strength never drops, its
//    share whole, no fire put out);
//  - rain and heavy rain (held, the rain set in first): each puts the fire out, heavy rain
//    sooner (about a minute) than rain (a few minutes), the effects and the share shrinking;
//  - snow (winter, the cover held full): the building loses less in 40 s than in clear winter
//    weather, the fire melts its roof's snow, a building not burning has its "Snow Fire Proofing";
//  - the save: in heavy rain 20 s in, saved and loaded (the environment section's "roof" lines),
//    the same lines and share after loading, the fire still being put out; an older save's
//    environment (no "roof" lines) loads with nothing tracked.
// Prints "RAINFIRE ok/FAILED ..." and "RAINFIRE PASSED/FAILED".
void Test_RainFire()
{
	int failures = 0;
	auto check = [&](bool ok, const std::string& what) {
		if (!ok) failures++;
		printf("RAINFIRE %s: %s\n", ok ? "ok" : "FAILED", what.c_str());
		fflush(stdout);
	};
	LoadBCP("data.bcp");
	g_settings["simSeed"] = 96;
	struct Result {
		float outAfter = -1.0f, siLost = 0.0f, shareMin = 1.0f, melt = 0.0f, proofOther = -1.0f;
		bool dropped = false, setUp = false, saveSame = false, loadedShare = false, goesOnOut = false, oldLoads = false;
		int shrunk = 0, rainedOut = 0;
	};
	const std::string saveName = "Save_Games\\wkbai rain fire.sav";
	// weather: an EnvWeather held; season: where the year is; snowCover: held (below 0: not);
	// seconds: how long the fire is watched at most
	auto runFire = [&](int weather, float season, float snowCover, float seconds, bool save) {
		Result r;
		std::string savedEnv;
		uint32_t fireId = 0;
		float savedShare = 1.0f;
		{
			Server server;
			server.loadSaveGame("Save_Games\\2 Player 4 Peaks.lvl");
			server.startLevel();
			const GameSet& gs = *server.gameSet;
			ServerGameObject* enemy = server.findObject(1030);
			const GameObjBlueprint* barracks = gs.findBlueprint(Tags::GAMEOBJCLASS_BUILDING, "Barracks Stone");
			const int setFire = gs.actionSequences.names.getIndex("Set Fire to Building");
			const int si = gs.items.names.getIndex("Structural Integrity");
			const int siCap = gs.items.names.getIndex("Structural Integrity Capacity");
			const int strength = gs.items.names.getIndex("Fire Strength (SI)");
			const int effects = gs.items.names.getIndex("Fire Effects Attached");
			const int proof = gs.items.names.getIndex("Snow Fire Proofing");
			if (!enemy || !barracks || setFire < 0 || si < 0 || siCap < 0 || strength < 0 || effects < 0 || proof < 0) {
				printf("RAINFIRE not set up\n");
				return r;
			}
			ServerEnvironment& env = server.environment;
			env.settings.seasons = true;
			env.settings.seasonLengthMin = 600;
			env.forceWeather = weather;
			env.forceSnowCover = snowCover;
			env.debugSet(server, "season", season);
			server.timeManager.unpause();
			server.timeManager.stepMs = 20;
			// (each: after every game second; false stops)
			auto run = [&](float secs, const std::function<bool()>& each) {
				for (int step = 0; step < (int)(secs * 50.0f); step++) {
					server.timeManager.pendingMs = server.timeManager.stepMs;
					if (server.timeManager.step())
						server.simulate();
					if (step % 50 == 49 && each && !each())
						return;
				}
			};
			run(30.0f, nullptr); // (the weather set in: the rain at its fall)
			const auto area = server.terrain->getPlayableArea();
			auto at = [&](float x, float z) { Vector3 p(area.first * 0.5f + x, 0, area.second * 0.5f + z); p.y = server.terrain->getHeight(p.x, p.z); return p; };
			SrvGORef b = server.spawnObject(barracks, enemy, at(140, 40), Vector3(0, 0, 0));
			SrvGORef other = server.spawnObject(barracks, enemy, at(140, 120), Vector3(0, 0, 0));
			if (!b.get() || !other.get()) {
				printf("RAINFIRE no barracks\n");
				return r;
			}
			fireId = b->id;
			b->setItem(siCap, 100000.0f);
			b->setItem(si, 100000.0f);
			for (int i = 0; i < 17; i++) {
				SrvScriptContext ctx(&server, b.get());
				auto _ = ctx.change(ctx.sequenceExecutor, b.get());
				gs.actionSequences[setFire].run(&ctx);
			}
			printf("RAINFIRE weather %d: rain %.2f, snow cover %.2f, fire %.1f with %.0f effects\n", weather, env.state.rain, env.state.snowCover,
				b->getItem(strength), b->getItem(effects));
			r.setUp = b->getItem(strength) >= 26.9f && b->getItem(effects) >= 3.0f;
			const float siStart = b->getItem(si);
			float last = b->getItem(strength), t = 0.0f;
			const int shrunk0 = env.stats.firesShrunk, out0 = env.stats.rainedOut;
			run(seconds, [&]() {
				t += 1.0f;
				const float s = b->getItem(strength);
				if (s < last - 1e-4f)
					r.dropped = true;
				last = s;
				r.shareMin = std::min(r.shareMin, env.fireShare(b->id));
				if (env.state.snowCover > 0.0f)
					r.melt = std::max(r.melt, 1.0f - env.roofSnow(b->id) / env.state.snowCover);
				if (s <= 0.0f && r.outAfter < 0.0f) {
					r.outAfter = t;
					printf("RAINFIRE weather %d: out after %.0f s, effects %.0f\n", weather, t, b->getItem(effects));
				}
				if (save && t == 20.0f) {
					server.timeManager.pause();
					savedShare = env.fireShare(b->id);
					printf("RAINFIRE save at 20 s (fire %.1f, share %.3f): %d\n", s, savedShare, (int)server.saveGame(PathToUtf8(g_userPath / "saved" / PathFromUtf8(saveName)).c_str()));
					savedEnv = env.saveText();
					return false;
				}
				return r.outAfter < 0.0f || snowCover >= 0.0f || weather == WEATHER_CLEAR;
			});
			r.siLost = siStart - b->getItem(si);
			r.shrunk = env.stats.firesShrunk - shrunk0;
			r.rainedOut = env.stats.rainedOut - out0;
			r.proofOther = other->getItem(proof);
		}
		if (save) {
			Server server;
			server.loadSaveGame(saveName.c_str());
			server.timeManager.pause(); // (the loader starts the clock)
			ServerEnvironment& env = server.environment;
			auto roofLines = [](const std::string& text, bool keep) {
				std::string o;
				std::istringstream in(text);
				std::string line;
				while (std::getline(in, line))
					if ((line.rfind("roof ", 0) == 0 || line.rfind("snowproofed", 0) == 0) == keep)
						o += line + "\n";
				return o;
			};
			const std::string loaded = env.saveText();
			printf("RAINFIRE saved roofs:\n%sRAINFIRE loaded roofs:\n%s", roofLines(savedEnv, true).c_str(), roofLines(loaded, true).c_str());
			r.saveSame = !roofLines(savedEnv, true).empty() && roofLines(savedEnv, true) == roofLines(loaded, true);
			r.loadedShare = savedShare < 1.0f && std::fabs(env.fireShare(fireId) - savedShare) < 1e-5f;
			const int strength = server.gameSet->items.names.getIndex("Fire Strength (SI)");
			ServerGameObject* b = server.findObject(fireId);
			const float before = b ? b->getItem(strength) : 0.0f;
			server.timeManager.unpause();
			server.timeManager.stepMs = 20;
			for (int step = 0; step < 15 * 50; step++) {
				server.timeManager.pendingMs = server.timeManager.stepMs;
				if (server.timeManager.step())
					server.simulate();
			}
			b = server.findObject(fireId);
			const float after = b ? b->getItem(strength) : 0.0f;
			printf("RAINFIRE loaded: fire %.1f -> %.1f in 15 s\n", before, after);
			r.goesOnOut = before > 0.0f && after < before - 3.0f;
			// a save from before (its environment without the "roof" lines)
			env.loadText(roofLines(savedEnv, false));
			r.oldLoads = env.fireShare(fireId) == 1.0f && env.saveText().find("\nroof ") == std::string::npos && env.state.rain > 0.0f;
		}
		return r;
	};
	char what[300];
	const Result clear = runFire(WEATHER_CLEAR, 0.5f, -1.0f, 40.0f, false);
	snprintf(what, sizeof(what), "clear: lost %.0f in 40 s, strength dropped %d, share %.2f, put out %d, effects shed %d", clear.siLost, (int)clear.dropped,
		clear.shareMin, clear.rainedOut, clear.shrunk);
	check(clear.setUp && !clear.dropped && clear.shareMin == 1.0f && clear.rainedOut == 0 && clear.shrunk == 0 && clear.siLost > 300.0f,
		std::string("no rain, burning unchanged: ") + what);
	const Result rain = runFire(WEATHER_RAIN, 0.5f, -1.0f, 400.0f, false);
	const Result heavy = runFire(WEATHER_HEAVY_RAIN, 0.5f, -1.0f, 400.0f, false);
	snprintf(what, sizeof(what), "rain: out after %.0f s (lost %.0f), heavy rain: after %.0f s (lost %.0f); effects shed %d and %d, share down to %.2f and %.2f",
		rain.outAfter, rain.siLost, heavy.outAfter, heavy.siLost, rain.shrunk, heavy.shrunk, rain.shareMin, heavy.shareMin);
	check(rain.setUp && heavy.setUp && rain.outAfter > 0.0f && heavy.outAfter > 0.0f && heavy.outAfter < rain.outAfter,
		std::string("heavy rain puts it out sooner than rain: ") + what);
	check(heavy.outAfter >= 35.0f && heavy.outAfter <= 110.0f && rain.outAfter >= 110.0f && rain.outAfter <= 360.0f,
		"about a minute in heavy rain, a few minutes in rain");
	check(heavy.siLost < rain.siLost && rain.siLost < clear.siLost / 40.0f * rain.outAfter * 0.75f, "the harm drops as it goes out");
	check(rain.shrunk >= 2 && heavy.shrunk >= 2 && heavy.shareMin < 0.2f && rain.rainedOut == 1 && heavy.rainedOut == 1,
		"the flames shrink (two effects shed, the share down) and it goes out once");
	const Result snow = runFire(WEATHER_SNOW, 3.5f, 1.0f, 40.0f, false);
	const Result winterClear = runFire(WEATHER_CLEAR, 3.5f, 0.0f, 40.0f, false);
	snprintf(what, sizeof(what), "snow: lost %.0f in 40 s against %.0f in clear winter weather, its roof %.0f%% melted, a building not burning: Snow Fire Proofing %.0f (clear: %.0f)",
		snow.siLost, winterClear.siLost, snow.melt * 100.0f, snow.proofOther, winterClear.proofOther);
	check(snow.setUp && winterClear.setUp && snow.siLost > 0.0f && snow.siLost < 0.8f * winterClear.siLost, std::string("snow-covered, it burns slower: ") + what);
	check(snow.melt > 0.1f && snow.melt < 1.0f && snow.proofOther == ServerEnvironment::SNOW_PROOFING && winterClear.proofOther == 0.0f,
		"the fire melts its roof's snow; a building not burning proofed by its snow");
	const Result saved = runFire(WEATHER_HEAVY_RAIN, 0.5f, -1.0f, 400.0f, true);
	check(saved.saveSame && saved.loadedShare, "saved and loaded: the same roofs, the same share");
	check(saved.goesOnOut, "loaded: the rain still puts the fire out");
	check(saved.oldLoads, "an older save's environment (no roofs) loads, nothing tracked");
	printf("RAINFIRE %s (%d failed)\n", failures ? "FAILED" : "PASSED", failures);
}

// Map folders (MapFolder.h; needs the game data). A headless run of a level: a fixed seed, every
// peasant chopping the nearest tree, `steps` steps of 20 ms; the world's hash (WorldHash) and one
// of the terrain (heights, tiles, water)
static uint64_t MapPlayHash(const std::string& level, int steps, uint64_t& terrainHash, size_t& objects)
{
	Server server;
	server.loadSaveGame(level.c_str());
	g_settings["simSeed"] = 4242u;
	server.startLevel();
	const int chop = server.gameSet->commands.names.getIndex("Chop Tree");
	std::vector<ServerGameObject*> peasants, trees;
	auto find = [&](CommonGameObject* obj, auto& self) -> void {
		for (const auto& [type, children] : obj->children)
			for (CommonGameObject* child : children) {
				if (child->blueprint->name == "Peasant")
					peasants.push_back(child->dyncast<ServerGameObject>());
				else if (child->blueprint->bpClass == Tags::GAMEOBJCLASS_CONTAINER && child->blueprint->name.find("Tree") != std::string::npos)
					trees.push_back(child->dyncast<ServerGameObject>());
				self(child, self);
			}
	};
	find(server.getLevel(), find);
	for (ServerGameObject* peasant : peasants) {
		ServerGameObject* best = nullptr;
		float bestDist = 1e12f;
		for (ServerGameObject* tree : trees)
			if (float d = (tree->position - peasant->position).sqlen2xz(); d < bestDist) { bestDist = d; best = tree; }
		if (best && chop >= 0)
			server.gameSet->commands[chop].execute(peasant, best, Tags::ORDERASSIGNMODE_FORGET_EVERYTHING_ELSE);
	}
	server.timeManager.unpause();
	StepServer(server, steps);
	const Terrain& t = *server.terrain;
	uint64_t h = 1469598103934665603ull;
	auto mix = [&](const void* data, size_t n) {
		for (size_t i = 0; i < n; i++) { h ^= ((const uint8_t*)data)[i]; h *= 1099511628211ull; }
	};
	for (int i = 0; i < (t.width + 1) * (t.height + 1); i++) {
		const float steps256 = t.heightSteps(i);
		mix(&steps256, sizeof(steps256));
	}
	for (int i = 0; i < t.width * t.height; i++) {
		const TerrainTile& tile = t.tiles[i];
		mix(tile.texture->grp->name.data(), tile.texture->grp->name.size());
		mix(&tile.texture->id, sizeof(int));
		const uint8_t bits[4] = { (uint8_t)tile.rot, (uint8_t)tile.xflip, (uint8_t)tile.zflip, (uint8_t)tile.fullOfWater };
		mix(bits, 4);
		mix(&tile.waterLevel, sizeof(float));
	}
	for (const Vector3& l : t.lakes)
		mix(&l, sizeof(l));
	// the header: texture database, sky, scale, sun, fog, edge, the minimap
	mix(t.texDbPath.data(), t.texDbPath.size() * sizeof(wchar_t));
	mix(t.skyTextureDirectory.data(), t.skyTextureDirectory.size() * sizeof(wchar_t));
	const float header[5] = { t.scale, t.sunVector.x, t.sunVector.y, t.sunVector.z, (float)t.edge };
	mix(header, sizeof(header));
	const uint32_t colours[2] = { t.sunColor, t.fogColor };
	mix(colours, sizeof(colours));
	mix(t.minimapRGBA.data(), t.minimapRGBA.size());
	terrainHash = h;
	return WorldHash(server, nullptr, &objects);
}

// the test LaunchTest started (a test that runs itself again in child processes names it)
static size_t g_launchedTest = 0;

// Every original map imported into a map folder and played headless gives the same game hash as
// the original: per map (its first level in Save_Games, "mapFolderTest": { "from": n, "count": n,
// "steps": n } to run part of them) "MAPFOLDER <map> <level>: world <h> <h> terrain <h> <h> objects
// <n> <n> same <0|1>", then "MAPFOLDER maps <n> same <n>".
// Each game runs in a process of its own (this program again, as a child, with WKB_TEST and
// WKB_MAPFOLDER_ONLY / _FROM): a second game in one process doesn't always play the same as the
// first (some state outlives a Server), and the 54 maps' games one after another in one process
// ran out of memory ("bad allocation" on the sixth map, 2026-11-19). "inProcess": true plays
// both in this process as before; "only": "original" or "imported" plays that one game alone
// (what a child does).
static void MapFolderImportInChildren(const nlohmann::json& cfg)
{
	char* program = nullptr;
	_get_pgmptr(&program);
	const std::string exe = program ? program : "wkbai.exe";
	const int from = cfg.value("from", 0), count = cfg.value("count", 1000);
	auto child = [&](int index, const char* only, std::string& line) {
		_putenv_s("WKB_TEST", std::to_string(g_launchedTest).c_str());
		_putenv_s("WKB_MAPFOLDER_ONLY", only);
		_putenv_s("WKB_MAPFOLDER_FROM", std::to_string(index).c_str());
		// (cmd /c strips the outer quotes: the path in its own)
		FILE* p = _popen(("\"\"" + exe + "\" < NUL\"").c_str(), "r");
		if (!p)
			return 0;
		char buf[4096];
		int found = 0; // 1: the game's line, 2: no level, 3: past the last map
		while (fgets(buf, sizeof(buf), p)) {
			const std::string l = buf;
			if (l.rfind("MAPFOLDERONE ", 0) == 0) {
				line = l;
				found = 1;
			}
			else if (l.rfind("MAPFOLDER ", 0) == 0 && l.find("no level uses it") != std::string::npos) {
				line = l;
				found = 2;
			}
			else if (l.rfind("MAPFOLDER maps 0 ", 0) == 0 && !found)
				found = 3;
		}
		const int code = _pclose(p);
		if (!found && code != 0)
			line = "the child process ended with " + std::to_string(code);
		return found;
	};
	int maps = 0, same = 0;
	for (int index = from; index < from + count; index++) {
		std::string a, b;
		const int fa = child(index, "original", a);
		if (fa == 3)
			break;
		maps++;
		if (fa == 2) {
			printf("%s", a.c_str());
			fflush(stdout);
			continue;
		}
		const int fb = fa == 1 ? child(index, "imported", b) : 0;
		// "MAPFOLDERONE <index> <dir> <kind> world <h> terrain <h> objects <n>..."
		auto field = [](const std::string& l, const char* name) {
			const size_t at = l.find(std::string(" ") + name + " ");
			if (at == std::string::npos)
				return std::string();
			const size_t start = at + strlen(name) + 2;
			return l.substr(start, l.find_first_of(" \r\n", start) - start);
		};
		const size_t kindAt = a.find(" original world ");
		const std::string dir = fa == 1 && kindAt != std::string::npos ? a.substr(13, kindAt - 13) : std::to_string(index);
		const bool ok = fa == 1 && fb == 1 && b.find(" import failed") == std::string::npos && field(a, "world") == field(b, "world")
			&& field(a, "terrain") == field(b, "terrain") && field(a, "objects") == field(b, "objects") && !field(a, "world").empty();
		same += ok;
		printf("MAPFOLDER %s: world %s %s terrain %s %s objects %s %s same %d%s%s\n", dir.c_str(), field(a, "world").c_str(), field(b, "world").c_str(),
			field(a, "terrain").c_str(), field(b, "terrain").c_str(), field(a, "objects").c_str(), field(b, "objects").c_str(), (int)ok,
			ok ? "" : " | ", ok ? "" : (fa != 1 ? a : b).c_str());
		fflush(stdout);
	}
	printf("MAPFOLDER maps %d same %d\n", maps, same);
	fflush(stdout);
}

void Test_MapFolderImport()
{
	LoadBCP("data.bcp");
	nlohmann::json cfg = g_settings.value("mapFolderTest", nlohmann::json::object());
	// a child of MapFolderImportInChildren: its one game
	if (const char* only = getenv("WKB_MAPFOLDER_ONLY"); only && *only) {
		cfg["only"] = only;
		cfg["from"] = atoi(getenv("WKB_MAPFOLDER_FROM") ? getenv("WKB_MAPFOLDER_FROM") : "0");
		cfg["count"] = 1;
	}
	else if (!cfg.value("inProcess", false) && !cfg.contains("only")) {
		MapFolderImportInChildren(cfg);
		return;
	}
	const int from = cfg.value("from", 0), count = cfg.value("count", 1000), steps = cfg.value("steps", 750);
	// each map's first level
	std::map<std::string, std::string> levelOf;
	std::unique_ptr<std::vector<std::string>> files(ListFiles("Save_Games"));
	std::sort(files->begin(), files->end());
	for (const std::string& f : *files) {
		if (f.size() < 4 || StrCICompare(std::string_view(f).substr(f.size() - 4), ".lvl") != 0)
			continue;
		char* data = nullptr;
		int size = 0;
		LoadFile(("Save_Games\\" + f).c_str(), &data, &size, 1);
		data[size] = 0;
		std::string map;
		if (const char* m = strstr(data, "MAP \"")) {
			if (const char* end = strchr(m + 5, '"'))
				map.assign(m + 5, end);
		}
		free(data);
		std::string key = map;
		for (char& c : key)
			c = (char)tolower((unsigned char)c);
		if (!key.empty() && !levelOf.count(key))
			levelOf[key] = f;
	}
	std::unique_ptr<std::vector<std::string>> dirs(ListDirectories("Maps"));
	std::sort(dirs->begin(), dirs->end());
	int maps = 0, same = 0, index = -1;
	const std::filesystem::path testRoot = g_userPath / "saved" / "Maps" / "__import_test";
	for (const std::string& dir : *dirs) {
		const std::string bcm = "Maps\\" + dir + "\\" + dir + ".bcm";
		if (!FileExists(bcm.c_str()))
			continue;
		index++;
		if (index < from || index >= from + count)
			continue;
		std::string key = bcm;
		for (char& c : key)
			c = (char)tolower((unsigned char)c);
		maps++;
		auto it = levelOf.find(key);
		if (it == levelOf.end()) {
			printf("MAPFOLDER %d %s: no level uses it\n", index, bcm.c_str());
			fflush(stdout);
			continue;
		}
		const std::string level = it->second;
		uint64_t t1 = 0, t2 = 0;
		size_t o1 = 0, o2 = 0;
		// "only": "original" or "imported": that one game alone (a second game in one process
		// doesn't play the same as the first: a fresh process for each, tools in the RE repo's notes)
		const std::string only = cfg.value("only", std::string());
		const uint64_t h1 = only == "imported" ? 0 : MapPlayHash("Save_Games\\" + level, steps, t1, o1);
		const std::string stem = level.substr(0, level.size() - 4);
		const std::filesystem::path folder = testRoot / PathFromUtf8(stem + ".wkbai");
		std::error_code ec;
		std::filesystem::remove_all(folder, ec);
		const MapFolder::Result r = MapFolder::Import(level, folder);
		uint64_t h2 = 0;
		if (only == "original") {
			printf("MAPFOLDERONE %d %s original world %016llx terrain %016llx objects %zu\n", index, dir.c_str(), (unsigned long long)h1, (unsigned long long)t1, o1);
			fflush(stdout);
			continue;
		}
		// "twice": the original a second time instead (is a second game in this process the same?)
		if (cfg.value("twice", false))
			h2 = MapPlayHash("Save_Games\\" + level, steps, t2, o2);
		else if (r.ok)
			h2 = MapPlayHash(MapFolder::GamePathOf(folder), steps, t2, o2);
		if (only == "imported") {
			printf("MAPFOLDERONE %d %s imported world %016llx terrain %016llx objects %zu%s%s\n", index, dir.c_str(), (unsigned long long)h2, (unsigned long long)t2, o2,
				r.ok ? "" : " import failed: ", r.ok ? "" : r.error.c_str());
			fflush(stdout);
			continue;
		}
		const bool ok = r.ok && h1 == h2 && t1 == t2 && o1 == o2;
		same += ok;
		printf("MAPFOLDER %d %s %s: world %016llx %016llx terrain %016llx %016llx objects %zu %zu same %d%s%s\n", index, dir.c_str(), level.c_str(),
			(unsigned long long)h1, (unsigned long long)h2, (unsigned long long)t1, (unsigned long long)t2, o1, o2, (int)ok,
			r.ok ? "" : " import failed: ", r.ok ? "" : r.error.c_str());
		fflush(stdout);
	}
	printf("MAPFOLDER maps %d same %d\n", maps, same);
	fflush(stdout);
}

// A map folder made from nothing (needs the game data): a 16-bit PNG of heights with two basins,
// one material (GRASS1, from 4 Peaks' texture database), a lake in each basin, a Manor and peasants
// per player by the base rule; compiled, loaded as a skirmish level, played for a minute.
// "MAPFOLDERNEW <check> <0|1>" and "MAPFOLDERNEW all <0|1>"
void Test_MapFolderNew()
{
	LoadBCP("data.bcp");
	const std::string name = g_settings.value("mapFolderNewName", std::string("PNG Test"));
	const std::filesystem::path folder = g_userPath / "saved" / "Maps" / PathFromUtf8(name + ".wkbai");
	std::error_code ec;
	std::filesystem::remove_all(folder, ec);
	std::filesystem::create_directories(folder, ec);
	Terrain ref;
	ref.readBCM("Maps\\128_4Peaks\\128_4Peaks.bcm");
	const int playable = 160, edge = ref.edge, w = playable + 2 * edge, h = playable + 2 * edge;
	// the heights: a plain at 20 steps, rolling, two round basins 7 steps deep, a ridge between
	// the halves with a pass in the middle, rising towards the edge band
	const float centreX[2] = { 200.0f, 600.0f }, centreZ[2] = { 560.0f, 240.0f };
	std::vector<uint16_t> png((size_t)(w + 1) * (h + 1));
	for (int r = 0; r <= h; r++)
		for (int x = 0; x <= w; x++) {
			const int zv = h - r;
			const float px = (x - edge) * 5.0f, pz = (zv - edge) * 5.0f;
			float steps = 20.0f + 1.5f * std::sin(px * 0.013f) * std::cos(pz * 0.011f);
			for (int b = 0; b < 2; b++) {
				const float d = std::hypot(px - centreX[b], pz - centreZ[b]);
				if (d < 90.0f)
					steps -= 7.0f * 0.5f * (1.0f + std::cos(d / 90.0f * 3.14159265f));
			}
			const float ridge = std::abs(px - 400.0f);
			if (ridge < 40.0f && std::abs(pz - 400.0f) > 50.0f)
				steps += 9.0f * 0.5f * (1.0f + std::cos(ridge / 40.0f * 3.14159265f));
			const float out = std::max({ -px, px - playable * 5.0f, -pz, pz - playable * 5.0f, 0.0f });
			steps += out * 0.12f;
			png[(size_t)r * (w + 1) + x] = (uint16_t)std::clamp((int)std::lround(steps * 256.0f), 0, 65535);
		}
	bool ok = MapFolder::WritePng16(folder / "heights.png", w + 1, h + 1, png);
	std::string db, sky;
	for (wchar_t c : ref.texDbPath) db += (char)c;
	for (wchar_t c : ref.skyTextureDirectory) sky += (char)c;
	nlohmann::ordered_json mj;
	mj["format"] = "wkbai map";
	mj["version"] = 1;
	mj["name"] = name;
	mj["size"] = { { "tiles", { w, h } }, { "edge", edge } };
	mj["heights"] = { { "scale", ref.scale } };
	mj["textures"] = { { "database", db }, { "translation", "Maps\\Map_Textures\\128_4Peaks.lttt" } };
	mj["sky"] = sky;
	mj["sun"] = { { "colour", { (ref.sunColor >> 16) & 255, (ref.sunColor >> 8) & 255, ref.sunColor & 255 } },
		{ "direction", { ref.sunVector.x, ref.sunVector.y, ref.sunVector.z } } };
	mj["fog"] = { (ref.fogColor >> 16) & 255, (ref.fogColor >> 8) & 255, ref.fogColor & 255 };
	mj["seed"] = 7;
	mj["ground"] = { { "fill", "GRASS1" } };
	mj["players"] = nlohmann::ordered_json::array({
		{ { "name", "North" }, { "colour", 1 }, { "start", { 160.0f, 300.0f } }, { "base", "Manor" }, { "peasants", 4 } },
		{ { "name", "South" }, { "colour", 2 }, { "start", { 640.0f, 500.0f } }, { "base", "Manor" }, { "peasants", 4 } } });
	{
		std::ofstream f(folder / "map.json", std::ios::binary);
		f << mj.dump(1) << "\n";
		ok = ok && (bool)f;
	}
	{
		// a lake in each basin, 4 steps above its floor (whole-map coordinates)
		nlohmann::ordered_json wj;
		wj["format"] = "wkbai water";
		wj["version"] = 1;
		auto lakes = nlohmann::ordered_json::array();
		for (int b = 0; b < 2; b++)
			lakes.push_back({ centreX[b] + edge * 5.0f, (20.0f - 7.0f + 4.0f) * ref.scale, centreZ[b] + edge * 5.0f, 0 });
		wj["lakes"] = lakes;
		std::ofstream f(folder / "water.json", std::ios::binary);
		f << wj.dump(1) << "\n";
		ok = ok && (bool)f;
	}
	printf("MAPFOLDERNEW written %d (%d x %d tiles, texture database %s)\n", (int)ok, w, h, db.c_str());
	const MapFolder::Result r = MapFolder::Compile(folder, true);
	for (const std::string& warning : r.warnings)
		printf("MAPFOLDERNEW warning: %s\n", warning.c_str());
	printf("MAPFOLDERNEW compiled %d %s\n", (int)r.ok, r.ok ? r.reportPath.c_str() : r.error.c_str());
	const bool fresh = MapFolder::IsFresh(folder);
	printf("MAPFOLDERNEW fresh after compiling %d\n", (int)fresh);
	if (!r.ok) {
		printf("MAPFOLDERNEW all 0\n");
		fflush(stdout);
		return;
	}
	// loaded as the skirmish list names it, and played a minute
	Server server;
	server.loadSaveGame(("Save_Games\\" + name + ".wkbai").c_str());
	g_settings["simSeed"] = 7u;
	server.startLevel();
	int manors = 0, peasants = 0, players = 0;
	std::vector<std::string> owners;
	auto walk = [&](CommonGameObject* obj, ServerGameObject* player, auto& self) -> void {
		for (const auto& [type, children] : obj->children)
			for (CommonGameObject* child : children) {
				ServerGameObject* c = child->dyncast<ServerGameObject>();
				ServerGameObject* p = player;
				if (c->blueprint->bpClass == Tags::GAMEOBJCLASS_PLAYER) {
					p = c;
					if (c->blueprint->name == "Human Player")
						players++;
				}
				if (c->blueprint->name == "Manor") {
					manors++;
					owners.push_back(p ? std::to_string(p->id) : "-");
				}
				if (c->blueprint->name == "Peasant")
					peasants++;
				self(child, p, self);
			}
	};
	walk(server.getLevel(), nullptr, walk);
	int flooded = 0;
	for (int i = 0; i < server.terrain->width * server.terrain->height; i++)
		flooded += server.terrain->tiles[i].fullOfWater;
	const bool basesOk = players == 2 && manors == 2 && peasants == 8 && owners.size() == 2 && owners[0] != owners[1];
	const bool waterOk = flooded > 100 && server.terrain->lakes.size() == 2;
	server.timeManager.unpause();
	StepServer(server, 3000);
	size_t objects = 0;
	WorldHash(server, nullptr, &objects);
	int manorsAfter = 0;
	auto count = [&](CommonGameObject* obj, auto& self) -> void {
		for (const auto& [type, children] : obj->children)
			for (CommonGameObject* child : children) {
				if (child->blueprint->name == "Manor" && !child->dyncast<ServerGameObject>()->deleted)
					manorsAfter++;
				self(child, self);
			}
	};
	count(server.getLevel(), count);
	printf("MAPFOLDERNEW bases %d (%d players, %d Manors of %s, %d peasants)\n", (int)basesOk, players, manors,
		owners.empty() ? "-" : (owners[0] + (owners.size() > 1 ? " and " + owners[1] : "")).c_str(), peasants);
	printf("MAPFOLDERNEW water %d (%zu lakes, %d tiles under water)\n", (int)waterOk, server.terrain->lakes.size(), flooded);
	printf("MAPFOLDERNEW played %d (60 s, t=%.1f, %zu objects, %d Manors standing)\n", (int)(manorsAfter == 2), server.timeManager.currentTime, objects, manorsAfter);
	// a second load: the cache is fresh, nothing compiled again
	const bool stillFresh = MapFolder::IsFresh(folder) && !MapFolder::Compile(folder).compiled;
	printf("MAPFOLDERNEW cache reused %d\n", (int)stillFresh);
	// map.json's "environment" block (docs/engine/map-environment.md): day and night from 5:00,
	// the seasons from winter; the rest of the block (climate, latitude, wind, ...) kept, not applied.
	// A new game set up as the skirmish lobby does, with its defaults: the clock at 5:00 in winter
	mj["environment"] = { { "climate", "nordic" }, { "latitude", 62 },
		{ "dayNight", { { "on", true }, { "dayLengthMin", 24 }, { "start", 5 }, { "moonPhase", "natural" } } },
		{ "seasons", { { "on", true }, { "seasonLengthMin", 30 }, { "start", "winter" }, { "severity", { { "winter", "harsh" } } } } },
		{ "weather", { { "stability", "normal" }, { "allowed", { "clear", "cloudy", "snow", "fog" } } } },
		{ "wind", { { "gustiness", 0.4 } } }, { "fixed", { "dayNight.on" } } };
	{
		std::ofstream f(folder / "map.json", std::ios::binary);
		f << mj.dump(1) << "\n";
	}
	bool envOk = false;
	{
		Server envServer;
		envServer.loadSaveGame(("Save_Games\\" + name + ".wkbai").c_str());
		QuickSkirmishMenu lobby;
		lobby.changeSettingsForLevel(name + ".wkbai");
		lobby.applySettings(envServer);
		envServer.startLevel();
		envServer.timeManager.unpause();
		StepServer(envServer, 5);
		const EnvironmentSettings& es = envServer.environment.settings;
		const EnvironmentState& st = envServer.environment.state;
		envOk = es.dayNight && es.seasons && es.startHour == 5 && es.startSeason == 3 && es.dayLengthMin == 24 && es.severity[3] == 2
			&& es.weatherStability == 1 && std::abs(st.dayFraction - 5.0f / 24.0f) < 0.01f && st.season == 3;
		printf("MAPFOLDERNEW environment %d (day and night %d, start hour %d, day %d min; seasons %d, start %d, winter %d, stability %d; "
			"at t=%.1f day fraction %.3f, season %d)\n", (int)envOk, (int)es.dayNight, es.startHour, es.dayLengthMin, (int)es.seasons, es.startSeason,
			es.severity[3], es.weatherStability, envServer.timeManager.currentTime, st.dayFraction, st.season);
	}
	printf("MAPFOLDERNEW all %d\n", (int)(ok && r.ok && fresh && basesOk && waterOk && manorsAfter == 2 && stillFresh && envOk));
	fflush(stdout);
}

// River currents (ServerEnvironment::applyCurrents, the game setting "riverCurrents"): on the first
// map with water a spearman can walk into that flows at least normally (a census of each map's
// water first), with the seasons on, spearmen of the enemy's put in its water (a census of its tiles first):
//  - in shallow water flowing slowly: nothing (they stand, unhurt);
//  - in the fastest water of normal depth: carried along the flow, hurt as fast as its speed's
//    share says, swimming out to the shore by themselves, alive there and let go;
//  - the setting off: the same water does nothing;
//  - a flood (the water risen 1.2 m at once): on flooded land they are carried and hurt faster
//    than in the river (3% a second) yet not at once;
//  - the save: "riverCurrents=1" and the "swim" lines, saved, loaded and saved again the same.
// Prints "CURRENTS ok/FAILED ..." and "CURRENTS PASSED/FAILED".
void Test_RiverCurrents()
{
	int failures = 0;
	auto check = [&](bool ok, const std::string& what) {
		if (!ok) failures++;
		printf("CURRENTS %s: %s\n", ok ? "ok" : "FAILED", what.c_str());
		fflush(stdout);
	};
	LoadBCP("data.bcp");
	g_settings["simSeed"] = 97;
	// Forking River: its rivers' fords and banks a spearman walks into (4 Peaks has little such
	// water; a second map can't be loaded in the same run)
	const std::string map = "2 Player Forking River.lvl";
	Server server;
	server.loadSaveGame(("Save_Games\\" + map).c_str());
	server.startLevel();
	const GameSet& gs = *server.gameSet;
	ServerGameObject* enemy = server.findObject(1030);
	const GameObjBlueprint* spear = gs.findBlueprint(Tags::GAMEOBJCLASS_CHARACTER, "Spearman");
	const int hp = gs.items.names.getIndex("Hit Points");
	const int hpCap = gs.items.names.getIndex("Hit Point Capacity");
	if (!enemy || !spear || hp < 0 || hpCap < 0) {
		printf("CURRENTS not set up\nCURRENTS FAILED (1)\n");
		return;
	}
	ServerEnvironment& env = server.environment;
	env.settings.seasons = true;
	env.settings.seasonLengthMin = 600;
	env.settings.riverCurrents = true;
	env.forceWeather = WEATHER_CLEAR;
	env.debugSet(server, "season", 1.5f); // (midsummer: the rain falls as rain)
	server.timeManager.unpause();
	server.timeManager.stepMs = 20;
	auto run = [&](float seconds) {
		for (int step = 0; step < (int)(seconds * 50.0f); step++) {
			server.timeManager.pendingMs = server.timeManager.stepMs;
			if (server.timeManager.step())
				server.simulate();
		}
	};
	run(2.0f);
	const auto [pw, ph] = server.terrain->getNumPlayableTiles();
	auto centre = [&](int ti) { Vector3 p(ti % pw * 5.0f + 2.5f, 0, ti / pw * 5.0f + 2.5f); p.y = server.terrain->getHeight(p.x, p.z); return p; };
	// the census: the water's depths and speeds
	int count[4][3] = {};
	int calm = -1, fast = -1;
	float fastest = -1.0f;
	for (int ti : env.wadeableTiles(server)) {
		const auto c = env.currentAt(server, centre(ti));
		if (c.depth == ServerEnvironment::DEPTH_NONE)
			continue;
		count[c.depth][c.speed]++;
		// (away from the map's edge and with a shore to swim to)
		const int x = ti % pw, z = ti / pw;
		if (x < 4 || z < 4 || x >= pw - 4 || z >= ph - 4 || c.shore.len2xz() < 0.5f || MovementController::isTileBlocked(spear, x, z))
			continue;
		if (c.depth == ServerEnvironment::DEPTH_SHALLOW && c.speed == 0 && calm < 0)
			calm = ti;
		if (c.depth == ServerEnvironment::DEPTH_NORMAL && c.flow > fastest && c.dir.len2xz() > 0.5f)
			fastest = c.flow, fast = ti;
	}
	{
		int bins[10] = {};
		for (int ti : env.wadeableTiles(server))
			if (!MovementController::isTileBlocked(spear, ti % pw, ti / pw))
				bins[std::clamp((int)(env.currentAt(server, centre(ti)).flow * 10.0f), 0, 9)]++;
		printf("CURRENTS the walkable water's flow by tenths:");
		for (int b : bins)
			printf(" %d", b);
		printf("\n");
		int all[10] = {}, deep[10] = {};
		for (int ti = 0; ti < pw * ph; ti++)
			if (const auto c = env.currentAt(server, centre(ti)); c.water)
				(c.metres <= 1.5f ? all : deep)[std::clamp((int)(c.flow * 10.0f), 0, 9)]++;
		printf("CURRENTS all the water up to 1.5 m by tenths:");
		for (int b : all)
			printf(" %d", b);
		printf("; deeper:");
		for (int b : deep)
			printf(" %d", b);
		printf("\n");
	}
	printf("CURRENTS census (slow/normal/fast): shallow %d/%d/%d, normal %d/%d/%d\n", count[1][0], count[1][1], count[1][2], count[2][0], count[2][1], count[2][2]);
	check(calm >= 0 && fast >= 0, "calm shallow water and moving water of normal depth on the map");
	if (calm < 0 || fast < 0) {
		printf("CURRENTS FAILED (%d)\n", failures);
		return;
	}
	auto alive = [&](const SrvGORef& r) { return r.get() && !r->deleted && !(r->flags & ServerGameObject::fTerminated) && r->getItem(hp) > 0.0f; };
	auto share = [&](const SrvGORef& r) { return alive(r) ? r->getItem(hp) / std::max(r->getItem(hpCap), 1.0f) : 0.0f; };
	char what[300];

	// calm shallow water: nothing
	{
		SrvGORef u = server.spawnObject(spear, enemy, centre(calm), Vector3(0, 0, 0));
		const Vector3 at = u->position;
		run(10.0f);
		const float moved = alive(u) ? (u->position - at).len2xz() : -1.0f;
		snprintf(what, sizeof(what), "calm shallow water: moved %.2f m in 10 s, health %.0f%%", moved, share(u) * 100.0f);
		check(moved >= 0.0f && moved < 0.3f && share(u) > 0.999f, what);
		if (u.get()) server.deleteObject(u.get());
	}
	// a waterway of the map's water design with its own speed (2 m/s) over that water: fast
	{
		const std::shared_ptr<WaterDesign> had = server.terrain->waterDesign;
		auto design = std::make_shared<WaterDesign>(had ? *had : WaterDesign::blank((int)server.terrain->width, (int)server.terrain->height));
		const Vector3 at = centre(fast);
		WaterDesign::Waterway way;
		way.points = { Vector3(at.x - 30.0f, 0, at.z), Vector3(at.x + 30.0f, 0, at.z) };
		way.width = 6.0f;
		way.speed = 2.0f;
		design->waterways.push_back(way);
		// (its JSON keeps the speed: the map folder's water.json)
		WaterDesign back;
		std::string error;
		const bool read = back.fromText(design->toText(), error) && !back.waterways.empty() && back.waterways.back().speed == 2.0f;
		server.terrain->waterDesign = design;
		env.forgetCurrents();
		const auto c = env.currentAt(server, at);
		snprintf(what, sizeof(what), "a waterway of 2 m/s: flow %.2f (speed class %d), drag %.1f m/s, harm %.3f, flowing (%.2f, %.2f); its JSON read back %d",
			c.flow, c.speed, c.drag, c.harm, c.dir.x, c.dir.z, (int)read);
		check(read && c.speed == 2 && c.drag == ServerEnvironment::CURRENT_DRAG[c.depth - 1][2] && c.dir.x > 0.5f, std::string("the map maker's speed: ") + what);
		server.terrain->waterDesign = had;
		env.forgetCurrents();
		check(env.currentAt(server, at).speed == 0, "without it: slow again in fair weather");
	}
	// heavy rain (the water's level held where it is: no flood): the water flows faster
	env.debugSet(server, "weather", (float)WEATHER_HEAVY_RAIN); // (held from now on, at once)
	env.forceWaterRise = 0.0f;
	env.forceWaterRiseNow = true;
	run(25.0f);
	// the fastest water of normal depth: carried, hurt, out by itself
	const auto fc = env.currentAt(server, centre(fast));
	printf("CURRENTS heavy rain: weather %d, rain %.2f, snowfall %.2f, %.1f C, water risen %.2f m, flow factor %.2f\n", env.state.weather, env.state.rain, env.state.snowfall,
		env.state.temperatureC, env.state.waterRise, EnvWaterFlowFactor(env.state.waterRise, env.state.rain));
	check(fc.depth == ServerEnvironment::DEPTH_NORMAL && fc.speed >= 1, "in heavy rain the water of normal depth flows normally or fast");
	printf("CURRENTS the fastest normal water: tile %d, flow %.2f (speed class %d), drag %.1f m/s, harm %.3f a second, flowing (%.2f, %.2f), shore (%.2f, %.2f)\n",
		fast, fc.flow, fc.speed, fc.drag, fc.harm, fc.dir.x, fc.dir.z, fc.shore.x, fc.shore.z);
	{
		SrvGORef u = server.spawnObject(spear, enemy, centre(fast), Vector3(0, 0, 0));
		const Vector3 at = u->position;
		const int carried0 = env.stats.carriedSeconds;
		// (step by step to the first update after it went in: carried from it)
		for (int i = 0; i < 110 && env.swimmerCount() == 0; i++)
			run(0.02f);
		const size_t carriedNow = env.swimmerCount();
		// the save while it is carried
		const std::string text = env.saveText();
		check(carriedNow >= 1 && text.find("riverCurrents=1") != std::string::npos && text.find("\nswim ") != std::string::npos, "the setting and the swimmer in the save");
		{
			ServerEnvironment copy;
			copy.loadText(text);
			check(copy.saveText() == text && copy.swimmerCount() == carriedNow && copy.settings.riverCurrents, "saved, loaded and saved again: the same");
		}
		run(1.9f);
		const Vector3 d = alive(u) ? u->position - at : Vector3();
		const float along = d.x * fc.dir.x + d.z * fc.dir.z;
		const float hurt = 1.0f - share(u);
		const int carriedFor = env.stats.carriedSeconds - carried0;
		snprintf(what, sizeof(what), "normal water: %.2f m along the flow, %.2f m in all in 3 s (drag %.1f m/s), carried %d s, %.1f%% hurt",
			along, d.len2xz(), fc.drag, carriedFor, hurt * 100.0f);
		check(alive(u) && carriedFor >= 1 && along > 0.5f * fc.drag && d.len2xz() > 1.0f, std::string("carried along the flow: ") + what);
		check(fc.harm <= 0.0f ? hurt < 1e-4f : (hurt >= fc.harm * 0.99f && hurt < fc.harm * 3.5f), "hurt as the water's speed says, while in it");
		run(60.0f);
		const auto out = env.currentAt(server, alive(u) ? u->position : at);
		snprintf(what, sizeof(what), "after a minute: alive %d, %.0f%% health, in water %d (drag %.1f), %zu carried, %.0f m from where it went in",
			(int)alive(u), share(u) * 100.0f, out.depth != ServerEnvironment::DEPTH_NONE, out.drag, env.swimmerCount(), alive(u) ? (u->position - at).len2xz() : 0.0f);
		check(alive(u) && out.drag <= 0.0f && env.swimmerCount() == 0, std::string("swam out by itself: ") + what);
		if (u.get()) server.deleteObject(u.get());
	}
	// the path finding's cost for the dangerous water, near it only
	{
		const ServerEnvironment::PathCost* pc = env.currentsPathCost();
		const int cost = pc ? pc->cost[fast] : -1;
		const Vector3 cornerA(pw * 5.0f - 3.0f, 0, 3.0f), cornerB(pw * 5.0f - 8.0f, 0, 8.0f);
		snprintf(what, sizeof(what), "path cost there %d (%zu costly tiles), near it %d, at the map's corner %d", cost, pc ? pc->tiles.size() : (size_t)0,
			pc ? (int)pc->around(centre(fast), centre(fast) + Vector3(20, 0, 0)) : -1, pc ? (int)pc->around(cornerA, cornerB, 0) : -1);
		check(pc && cost == ServerEnvironment::CURRENT_PATH_COST[fc.depth - 1][fc.speed] && cost > 0 && pc->around(centre(fast), centre(fast) + Vector3(20, 0, 0)),
			std::string("the path finding goes round it: ") + what);
	}
	// the AI walks its unit out of the water that harms it (AIWeather.cpp)
	{
		SrvGORef u = server.spawnObject(spear, enemy, centre(fast), Vector3(0, 0, 0));
		AIController& ai = enemy->aiController;
		const int before = ai.weatherCurrentRetreats;
		ai.nextWeatherCheck = 0.0f;
		ai.weatherUpdate();
		const bool held = u.get() && ai.weatherHolds(u->id);
		run(0.1f); // (its order taken up in the next steps)
		const bool walking = u.get() && (u->movementController.isMoving() || u->movement.isMoving());
		snprintf(what, sizeof(what), "the AI's unit in harmful water: retreats %d -> %d, held %d, walking %d", before, ai.weatherCurrentRetreats, (int)held, (int)walking);
		check(ai.weatherCurrentRetreats == before + 1 && held && walking, std::string("the AI takes it out: ") + what);
		run(20.0f);
		ai.nextWeatherCheck = 0.0f;
		ai.weatherUpdate();
		snprintf(what, sizeof(what), "after 20 s: in harmful water %d, still held %d", u.get() ? (int)(env.currentAt(server, u->position).harm > 0.0f) : -1,
			u.get() ? (int)ai.weatherHolds(u->id) : -1);
		check(u.get() && env.currentAt(server, u->position).harm <= 0.0f, std::string("out, let go once it stands: ") + what);
		if (u.get()) server.deleteObject(u.get());
	}
	// a Scout (the one model with a swim) swims; a spearman walks the water (no swim of its own)
	{
		const GameObjBlueprint* scout = gs.findBlueprint(Tags::GAMEOBJCLASS_CHARACTER, "Scout");
		const int swim = gs.animations.names.getIndex("Swim"), move = gs.animations.names.getIndex("Move");
		if (scout && swim >= 0) {
			SrvGORef u = server.spawnObject(scout, enemy, centre(fast), Vector3(0, 0, 0));
			for (int i = 0; i < 110 && env.swimmerCount() == 0; i++)
				run(0.02f);
			const int whileIn = u.get() ? u->animationIndex : -1;
			run(20.0f);
			const int after = u.get() ? u->animationIndex : -1;
			snprintf(what, sizeof(what), "the Scout carried: animation %d (Swim %d, Move %d); out of it: %d, moving %d", whileIn, swim, move, after,
				u.get() ? (int)u->movement.isMoving() : -1);
			check(whileIn == swim && after == 0 && u.get() && !u->movement.isMoving(), std::string("it swims, then stands: ") + what);
			if (u.get()) server.deleteObject(u.get());
		}
		else
			check(false, "a Scout and the Swim animation in the game set");
	}
	// the setting off: nothing
	{
		env.settings.riverCurrents = false;
		SrvGORef u = server.spawnObject(spear, enemy, centre(fast), Vector3(0, 0, 0));
		const Vector3 at = u->position;
		run(5.0f);
		const float moved = alive(u) ? (u->position - at).len2xz() : -1.0f;
		snprintf(what, sizeof(what), "currents off: moved %.2f m in 5 s, health %.0f%%", moved, share(u) * 100.0f);
		check(moved >= 0.0f && moved < 0.3f && share(u) > 0.999f && env.saveText().find("riverCurrents") == std::string::npos, what);
		if (u.get()) server.deleteObject(u.get());
		env.settings.riverCurrents = true;
	}
	// a flood: on the flooded land
	{
		env.debugSet(server, "weather", (float)WEATHER_HEAVY_RAIN);
		env.forceWaterRise = 1.2f;
		env.forceWaterRiseNow = true;
		run(2.0f);
		int flooded = -1;
		for (int ti = 0; ti < pw * ph && flooded < 0; ti++) {
			const int x = ti % pw, z = ti / pw;
			if (x < 4 || z < 4 || x >= pw - 4 || z >= ph - 4)
				continue;
			const auto c = env.currentAt(server, centre(ti));
			if (c.depth == ServerEnvironment::DEPTH_FLOOD && !c.water && c.metres > 0.4f && c.metres < 0.9f && c.dir.len2xz() > 0.5f
				&& !MovementController::isTileBlocked(spear, x, z))
				flooded = ti;
		}
		check(flooded >= 0, "flooded land to stand on");
		if (flooded >= 0) {
			const auto c = env.currentAt(server, centre(flooded));
			SrvGORef u = server.spawnObject(spear, enemy, centre(flooded), Vector3(0, 0, 0));
			const Vector3 at = u->position;
			run(1.1f);
			const float after1 = share(u);
			run(3.9f);
			const float after5 = share(u);
			const float moved = alive(u) ? (u->position - at).len2xz() : 0.0f;
			snprintf(what, sizeof(what), "flood %.2f m deep, flow %.2f, drag %.1f m/s: health %.1f%% after 1 s, %.1f%% after 5 s, moved %.1f m (out of it)",
				c.metres, c.flow, c.drag, after1 * 100.0f, after5 * 100.0f, moved);
			check(alive(u) && 1.0f - after1 >= 0.029f && 1.0f - after1 < 0.1f && 1.0f - after1 > 2.0f * fc.harm && moved > 1.0f,
				std::string("hurt fast (3% a second), not at once, carried and swimming: ") + what);
			if (u.get()) server.deleteObject(u.get());
		}
	}
	// in the flood: a war machine sinks, dragged a little; a huge creature drifts, can't swim,
	// hurt; the otherworldly (the Archangel Michael, Abaddon) stand on the bed untouched
	{
		int flooded = -1;
		for (int ti = 0; ti < pw * ph && flooded < 0; ti++) {
			const int x = ti % pw, z = ti / pw;
			if (x < 4 || z < 4 || x >= pw - 4 || z >= ph - 4)
				continue;
			const auto c = env.currentAt(server, centre(ti));
			if (c.depth == ServerEnvironment::DEPTH_FLOOD && !c.water && c.metres > 0.4f && c.metres < 0.9f && c.drag > 0.0f)
				flooded = ti;
		}
		const auto c = flooded >= 0 ? env.currentAt(server, centre(flooded)) : ServerEnvironment::CurrentAt();
		for (const char* name : { "Battering Ram", "Behemoth", "Abaddon", "Michael" }) {
			const GameObjBlueprint* bp = gs.findBlueprint(Tags::GAMEOBJCLASS_CHARACTER, name);
			if (!bp || flooded < 0) {
				check(false, std::string("a ") + name + " and flooded land");
				continue;
			}
			const uint8_t hardy = env.waterHardiness(bp);
			SrvGORef u = server.spawnObject(bp, enemy, centre(flooded), Vector3(0, 0, 0));
			const Vector3 at = u->position;
			const int carried0 = env.stats.carriedSeconds;
			run(3.0f);
			const float moved = alive(u) ? (u->position - at).len2xz() : -1.0f, health = share(u);
			const int carried = env.stats.carriedSeconds - carried0;
			snprintf(what, sizeof(what), "%s (kind %d) in the flood (drag %.1f m/s): health %.1f%%, moved %.2f m, carried %d s", name, hardy, c.drag,
				health * 100.0f, moved, carried);
			if (hardy == ServerEnvironment::WATER_MACHINE)
				check(health < 0.7f && health > 0.0f && carried >= 1 && moved > 0.2f && moved < c.drag * 3.0f * 0.6f, std::string("a war machine sinks, dragged a little: ") + what);
			else if (hardy == ServerEnvironment::WATER_HUGE)
				check(alive(u) && health < 0.97f && carried >= 1 && moved > 0.5f * c.drag, std::string("a huge creature drifts, hurt: ") + what);
			else
				check(hardy == ServerEnvironment::WATER_UNHARMED && alive(u) && health > 0.999f && carried == 0 && moved < 0.3f, std::string("otherworldly, untouched: ") + what);
			if (u.get()) server.deleteObject(u.get());
		}
	}
	const auto& st = env.stats;
	printf("CURRENTS stats: carried seconds %d, most at once %d, swept away %d\n", st.carriedSeconds, st.carriedMost, st.sweptAway);
	printf(failures ? "CURRENTS FAILED (%d)\n" : "CURRENTS PASSED\n", failures);
}

// A wedge of riders charging a line (the play test: the wedge stopped at first contact, only its
// tip fought, the others stood). WKB.exe's scripts (Formation Army.cpp, Standard Heavy Cavalry
// Targets.cpp; docs/re/combat-original-formations.md): the formation marches to the target, then
// every member picks its own victim among the enemies within "My Individual Formation Attack
// Range" (40 m) and rides out to it, at most "Number Of Units I Can Be Attacked By" (3) on one.
// On 4 Peaks, 15 Knights in a Wedge and 24 Men at Arms in a Line 50 m off, facing each other:
//  - case 0, "Formation Army Attack" on a man of the line (a right-click on it);
//  - case 1, a Move through the line to 30 m behind it (a charge by moving);
// every second each rider's order, target, distance to it and to the nearest enemy can be logged
// (WKB_WEDGE_LOG=1). Fighting: an attack order on an enemy, or the game set's break-off of cavalry
// that hit formed heavy infantry from the front. Checked: within 6 s of the first contact 60 %
// of the riders fighting at once, from 2 to 12 s after it half of them on average (the
// scripts hand out victims every 2.5 s, at most 3 riders on one), and in those 12 s none standing
// idle (no order, not walking) with an enemy within 10 m for more than 3 s. Before the fix to ARE_ASSOCIATED (an idle
// member's FINDER_TARGET finds nothing: 0, as WKB.exe's) only the tip and the 2 others nearest the
// target fought, the rest stood. WKB_WEDGE_CASE=<n> runs one case.
// Prints "WEDGE ok/FAILED ..." and "WEDGE PASSED/FAILED".
void Test_WedgeCharge()
{
	int failures = 0;
	auto check = [&](bool ok, const std::string& what) {
		if (!ok) failures++;
		printf("WEDGE %s: %s\n", ok ? "ok" : "FAILED", what.c_str());
		fflush(stdout);
	};
	LoadBCP("data.bcp");
	const bool log = getenv("WKB_WEDGE_LOG") != nullptr;
	const int onlyCase = getenv("WKB_WEDGE_CASE") ? atoi(getenv("WKB_WEDGE_CASE")) : -1;
	for (int scenario = 0; scenario < 2; scenario++) {
		if (onlyCase >= 0 && onlyCase != scenario)
			continue;
		Server server;
		server.loadSaveGame("Save_Games\\2 Player 4 Peaks.lvl");
		ServerGameObject* own = server.findObject(1027);
		ServerGameObject* enemy = server.findObject(1030);
		const GameSet& gs = *server.gameSet;
		const GameObjBlueprint* rider = gs.findBlueprint(Tags::GAMEOBJCLASS_CHARACTER, getenv("WKB_WEDGE_RIDER") ? getenv("WKB_WEDGE_RIDER") : "Knight");
		const GameObjBlueprint* foot = gs.findBlueprint(Tags::GAMEOBJCLASS_CHARACTER, getenv("WKB_WEDGE_FOOT") ? getenv("WKB_WEDGE_FOOT") : "Man At Arms");
		const int wedgeCmd = gs.commands.names.getIndex(getenv("WKB_WEDGE_TYPE") ? getenv("WKB_WEDGE_TYPE") : "Wedge"), lineCmd = gs.commands.names.getIndex("Line");
		const int attackCmd = gs.commands.names.getIndex("Formation Army Attack"), moveCmd = gs.commands.names.getIndex("Move");
		if (!own || !enemy || !rider || !foot || wedgeCmd < 0 || lineCmd < 0 || attackCmd < 0 || moveCmd < 0
			|| gs.objBlueprints[Tags::GAMEOBJCLASS_FORMATION].size() == 0) {
			printf("WEDGE FAILED (not set up)\n");
			return;
		}
		const auto area = server.terrain->getPlayableArea();
		// open ground: the spot nearest the map's middle with no tile blocked for either within
		// 35 m across, 50 m ahead of the line and 55 m behind the riders (and under 12 m of height difference)
		Vector3 centre(area.first * 0.5f, 0, area.second * 0.5f);
		{
			auto open = [&](const Vector3& c) {
				float lo = 1e9f, hi = -1e9f;
				for (float dz = -50.0f; dz <= 55.0f; dz += 5.0f)
					for (float dx = -35.0f; dx <= 35.0f; dx += 5.0f) {
						const float x = c.x + dx, z = c.z + dz;
						if (x < 10.0f || z < 10.0f || x > area.first - 10.0f || z > area.second - 10.0f)
							return false;
						if (MovementController::isTileBlocked(rider, (int)(x / 5.0f), (int)(z / 5.0f))
							|| MovementController::isTileBlocked(foot, (int)(x / 5.0f), (int)(z / 5.0f)))
							return false;
						const float h = server.terrain->getHeight(x, z);
						lo = std::min(lo, h);
						hi = std::max(hi, h);
					}
				return hi - lo < 12.0f;
			};
			const Vector3 middle = centre;
			bool found = false;
			for (float r = 0.0f; r < 400.0f && !found; r += 10.0f)
				for (int k = 0; k < std::max(1, (int)(r / 5.0f)) && !found; k++) {
					const float a = k * 6.2831855f / std::max(1, (int)(r / 5.0f));
					const Vector3 c = middle + Vector3(std::sin(a) * r, 0, std::cos(a) * r);
					if (open(c)) {
						centre = c;
						found = true;
					}
				}
			printf("WEDGE open ground at (%.0f, %.0f)\n", centre.x, centre.z);
			if (!found) {
				printf("WEDGE FAILED (no open ground)\n");
				return;
			}
		}
		auto at = [&](float x, float z, const GameObjBlueprint* bp) { return MovementController::nearestValidPosition(bp, centre + Vector3(x, 0, z)); };
		server.timeManager.unpause();
		server.timeManager.stepMs = 20;
		auto step = [&]() {
			server.timeManager.pendingMs = server.timeManager.stepMs;
			if (server.timeManager.step())
				server.simulate();
		};
		auto makeFormation = [&](ServerGameObject* player, const std::vector<ServerGameObject*>& units, float heading, int command) {
			ServerGameObject* f = server.spawnObject(&gs.objBlueprints[Tags::GAMEOBJCLASS_FORMATION][0], player, {}, {});
			Vector3 sum;
			for (ServerGameObject* u : units) {
				sum += u->position;
				u->setParent(f);
			}
			f->setPosition(sum / (float)units.size());
			f->setOrientation(Vector3(0, heading, 0));
			f->sendEvent(Tags::PDEVENT_ON_SPAWN);
			gs.commands[command].execute(f, nullptr, Tags::ORDERASSIGNMODE_FORGET_EVERYTHING_ELSE);
			return SrvGORef(f);
		};
		// the riders south (+z), facing north (heading 0 faces -z); the line north, facing south
		std::vector<ServerGameObject*> riders, foes;
		for (int i = 0; i < 15; i++)
			riders.push_back(server.spawnObject(rider, own, at(-12.0f + (i % 5) * 6.0f, 30.0f + (i / 5) * 6.0f, rider), Vector3(0, 0, 0)));
		for (int i = 0; i < 24; i++)
			foes.push_back(server.spawnObject(foot, enemy, at(-23.0f + (i % 12) * 4.0f, -20.0f - (i / 12) * 3.0f, foot), Vector3(0, 3.14159f, 0)));
		std::vector<SrvGORef> riderRefs(riders.begin(), riders.end()), foeRefs(foes.begin(), foes.end());
		SrvGORef wedge = makeFormation(own, riders, 0.0f, wedgeCmd);
		SrvGORef line = makeFormation(enemy, foes, 3.14159f, lineCmd);
		// (the formations' own objects: the type commands replace them)
		for (int i = 0; i < 5 * 50; i++)
			step();
		wedge = riderRefs[0] ? SrvGORef(riderRefs[0]->getParent()) : SrvGORef();
		line = foeRefs[0] ? SrvGORef(foeRefs[0]->getParent()) : SrvGORef();
		const bool formedUp = wedge && wedge->blueprint->bpClass == Tags::GAMEOBJCLASS_FORMATION && line && line->blueprint->bpClass == Tags::GAMEOBJCLASS_FORMATION;
		printf("WEDGE case %d: %s of %zu (%s) at (%.0f, %.0f), %s of %zu (%s) at (%.0f, %.0f)\n", scenario,
			wedge ? wedge->blueprint->name.c_str() : "-", riders.size(), rider->name.c_str(), wedge ? wedge->position.x : 0.0f, wedge ? wedge->position.z : 0.0f,
			line ? line->blueprint->name.c_str() : "-", foes.size(), foot->name.c_str(), line ? line->position.x : 0.0f, line ? line->position.z : 0.0f);
		if (log)
			for (const SrvGORef& f : foeRefs)
				if (f)
					printf("WEDGE   foe %u at (%.1f, %.1f) under %s\n", f->id, f->position.x, f->position.z, f->getParent() ? f->getParent()->blueprint->name.c_str() : "-");
		if (!formedUp) {
			check(false, "both formations formed");
			continue;
		}
		if (scenario == 0)
			gs.commands[attackCmd].execute(wedge.get(), foes[5], Tags::ORDERASSIGNMODE_FORGET_EVERYTHING_ELSE);
		else
			gs.commands[moveCmd].execute(wedge.get(), nullptr, Tags::ORDERASSIGNMODE_FORGET_EVERYTHING_ELSE, line->position + Vector3(0, 0, -30.0f));
		auto alive = [](const SrvGORef& r) { return r && !(r->flags & CommonGameObject::fTerminated) && r->blueprint->name.rfind("Dead", 0) != 0; };
		auto nearestFoe = [&](const ServerGameObject* u) {
			float best = 1e9f;
			for (const SrvGORef& f : foeRefs)
				if (alive(f))
					best = std::min(best, std::sqrt((f->position - u->position).sqlen2xz()));
			return best;
		};
		// fighting: an attack order of its own on a (living) enemy, or the game set's break-off of
		// cavalry that struck formed heavy infantry from the front ("Run Away After Attack": "Rear
		// and Flee", then "Move To Cavalry Broken Marker", and back to the fight)
		auto inMelee = [&](ServerGameObject* u) {
			Order* o = u->orderConfig.getCurrentOrder();
			if (!o)
				return false;
			const std::string& on = gs.orders.names.getString(o->blueprint->bpid);
			if (on.find("Flee") != std::string::npos || on.find("Broken Marker") != std::string::npos)
				return true;
			if (o->blueprint->classType != Tags::ORDTSKTYPE_ATTACK)
				return false;
			Task* t = o->getCurrentTask();
			ServerGameObject* tg = t ? t->target.get() : nullptr;
			return tg && !(tg->flags & CommonGameObject::fTerminated) && tg->getPlayer() != own;
		};
		float contact = -1.0f;
		double shareSum = 0.0;
		int shareCount = 0;
		int peak = 0;
		float longestIdle = 0.0f;
		uint32_t longestIdleOf = 0;
		std::map<uint32_t, float> idleSince; // riders standing idle with an enemy within 10 m, since when
		const float t0 = server.timeManager.currentTime;
		for (int s = 0; s < 40 * 50; s++) {
			step();
			const float now = server.timeManager.currentTime - t0;
			int living = 0, melee = 0, idleNear = 0, foesLeft = 0;
			for (const SrvGORef& f : foeRefs)
				foesLeft += alive(f) ? 1 : 0;
			for (const SrvGORef& r : riderRefs)
				if (alive(r)) {
					living++;
					if (contact < 0.0f && nearestFoe(r.get()) < 4.0f)
						contact = now;
					if (inMelee(r.get()))
						melee++;
					if (!r->orderConfig.getCurrentOrder() && !r->movementController.isMoving() && nearestFoe(r.get()) < 10.0f) {
						idleNear++;
						auto it = idleSince.emplace(r->id, now).first;
						if (contact >= 0.0f && now <= contact + 12.0f && foesLeft > 0)
							if (now - it->second > longestIdle) {
								longestIdle = now - it->second;
								longestIdleOf = r->id;
							}
					}
					else
						idleSince.erase(r->id);
				}
			if (s % 50 == 49) {
				ServerGameObject* wf = riderRefs[0] && alive(riderRefs[0]) ? riderRefs[0]->getParent() : nullptr;
				if (log) {
					printf("WEDGE t=%4.1f %s %s at (%.1f, %.1f)%s; riders %d, in melee %d, idle near %d; foes %d\n", now,
						wf ? wf->blueprint->name.c_str() : "-",
						wf && wf->orderConfig.getCurrentOrder() ? gs.orders.names.getString(wf->orderConfig.getCurrentOrder()->blueprint->bpid).c_str() : "-",
						wf ? wf->position.x : 0.0f, wf ? wf->position.z : 0.0f, wf && wf->movement.isMoving() ? " moving" : "", living, melee, idleNear, foesLeft);
					ServerGameObject* lf = foeRefs[0] && alive(foeRefs[0]) ? foeRefs[0]->getParent() : nullptr;
					printf("WEDGE   the foes' %s %s at (%.1f, %.1f)%s; foe 0 at (%.1f, %.1f) %s\n", lf ? lf->blueprint->name.c_str() : "-",
						lf && lf->orderConfig.getCurrentOrder() ? gs.orders.names.getString(lf->orderConfig.getCurrentOrder()->blueprint->bpid).c_str() : "-",
						lf ? lf->position.x : 0.0f, lf ? lf->position.z : 0.0f, lf && lf->movement.isMoving() ? " moving" : "",
						foeRefs[0] ? foeRefs[0]->position.x : 0.0f, foeRefs[0] ? foeRefs[0]->position.z : 0.0f,
						foeRefs[0] && foeRefs[0]->orderConfig.getCurrentOrder() ? gs.orders.names.getString(foeRefs[0]->orderConfig.getCurrentOrder()->blueprint->bpid).c_str() : "(no order)");
					for (const SrvGORef& r : riderRefs) {
						if (!alive(r))
							continue;
						if (getenv("WKB_WEDGE_DEBUG")) {
							SrvScriptContext ctx(&server, r.get());
							const int fi = gs.objectFinderDefinitions.names.getIndex("Enemy Units Within Formation Attack Range metres of Me");
							const int e1 = gs.equations.names.getIndex("I Am Not Already Attacking A Victim Of My Controller");
							const int e2 = gs.equations.names.getIndex("No Victims Left");
							const int e3 = gs.equations.names.getIndex("I Am In A Formation");
							const int e4 = gs.equations.names.getIndex("I Am Currently Attacking");
							printf("WEDGE   debug %u: victims %d, not already %.0f, none left (controller) %.0f, in a formation %.0f, attacking %.0f\n", r->id,
								fi >= 0 ? (int)gs.objectFinderDefinitions[fi]->eval(&ctx).size() : -1,
								e1 >= 0 ? gs.equations[e1]->eval(&ctx) : -9.0f,
								[&] { SrvScriptContext c2(&server, r->getParent()); return e2 >= 0 ? gs.equations[e2]->eval(&c2) : -9.0f; }(),
								e3 >= 0 ? gs.equations[e3]->eval(&ctx) : -9.0f, e4 >= 0 ? gs.equations[e4]->eval(&ctx) : -9.0f);
						}
						Order* o = r->orderConfig.getCurrentOrder();
						Task* t = o ? o->getCurrentTask() : nullptr;
						ServerGameObject* tg = t ? t->target.get() : nullptr;
						printf("WEDGE   %u at (%.1f, %.1f)%s %s -> %u (%.1f m), nearest foe %.1f m, place %.1f m off, under %s\n", r->id, r->position.x, r->position.z,
							r->movementController.isMoving() ? " moving" : "", o ? gs.orders.names.getString(o->blueprint->bpid).c_str() : "(no order)",
							tg ? tg->id : 0u, tg ? std::sqrt((tg->position - r->position).sqlen2xz()) : 0.0f, nearestFoe(r.get()),
							[&] { Vector3 pl; return r->getParent() && FormationController::memberPlace(r->getParent(), r.get(), pl) ? std::sqrt((pl - r->position).sqlen2xz()) : -1.0f; }(),
							r->getParent() ? r->getParent()->blueprint->name.c_str() : "-");
					}
				}
				// 2 to 12 s after the first contact: the riders fighting, of those alive
				if (contact >= 0.0f && now >= contact + 2.0f && now <= contact + 12.0f && foesLeft > 0 && living > 0) {
					shareSum += (double)melee / living;
					shareCount++;
				}
				// the most fighting at once within 6 s of it
				if (contact >= 0.0f && now <= contact + 6.0f && living > 0)
					peak = std::max(peak, (int)std::lround(100.0 * melee / living));
			}
		}
		const int share = shareCount ? (int)std::lround(100.0 * shareSum / shareCount) : -1;
		printf("WEDGE case %d: first contact %.1f s after the order; within 6 s of it at most %d%% of the riders fighting at once, 2-12 s after it %d%% on average; the longest stand idle with an enemy within 10 m in those 12 s %.1f s (%u)\n",
			scenario, contact, peak, share, longestIdle, longestIdleOf);
		const std::string name = "case " + std::to_string(scenario) + (scenario == 0 ? " (an attack order)" : " (a move through the line)");
		check(contact >= 0.0f, name + ": the wedge reaches the line");
		check(peak >= 60, name + ": within 6 s of contact most riders fight at once");
		check(share >= 50, name + ": most riders fight in the 12 s after contact");
		check(longestIdle <= 3.0f, name + ": none stands idle with an enemy in reach (more than 3 s)");
	}
	printf(failures ? "WEDGE FAILED (%d)\n" : "WEDGE PASSED\n", failures);
}

// A gatehouse on a wall (the play test of 2026-11-18: "You cannot build a gatehouse on top of
// walls"): a Manor stamped down on 4 Peaks for player 1027, then a Stockade Gatehouse placed as
// the client's preview places it (the cursor's tile, snapped) over four straight pieces of its
// ring, along the south run and, turned a quarter, along the west run. The plan must be valid, the
// four pieces gone, the gatehouse in the city between the two pieces left at the run's ends.
// Across the run, on a corner or on a gatehouse it is refused. Then, the walls built: a peasant
// inside walks out through the open gate; with the gate closed, neither it nor an enemy crosses
// the gate's tiles. Prints "GATEWALL ok|FAILED: <check>" and "GATEWALL all <0/1>".
void Test_GatehouseOnWall()
{
	int failures = 0;
	auto check = [&](bool ok, const std::string& what) {
		if (!ok) failures++;
		printf("GATEWALL %s: %s\n", ok ? "ok" : "FAILED", what.c_str());
		fflush(stdout);
	};
	LoadBCP("data.bcp");
	// a client on a local link too: the preview's plan (the client's own copy of the walls) must
	// agree with the server's
	Server server;
	Client client(true);
	NetLocalBuffer toClient, toServer;
	NetLocalLink srv2cli(&toClient, &toServer), cli2srv(&toServer, &toClient);
	server.addClient(&srv2cli);
	client.serverLink = &cli2srv;
	server.loadSaveGame("Save_Games\\2 Player 4 Peaks.lvl");
	server.startLevel();
	auto pump = [&]() {
		for (int i = 0; i < 1000 && cli2srv.available(); i++)
			client.tick();
	};
	pump();
	ServerGameObject* player = server.findObject(1027);
	ServerGameObject* enemy = server.findObject(1030);
	ClientGameObject* cliPlayer = client.findObject(1027);
	const GameSet& gs = *server.gameSet;
	const GameObjBlueprint* cliGate = client.gameSet ? client.gameSet->findBlueprint(Tags::GAMEOBJCLASS_BUILDING, "Stockade Gatehouse Foundation") : nullptr;
	const GameObjBlueprint* manor = gs.findBlueprint(Tags::GAMEOBJCLASS_BUILDING, "Manor Foundation");
	const GameObjBlueprint* gateFoundation = gs.findBlueprint(Tags::GAMEOBJCLASS_BUILDING, "Stockade Gatehouse Foundation");
	const GameObjBlueprint* gateClosed = gs.findBlueprint(Tags::GAMEOBJCLASS_BUILDING, "Stockade Gatehouse Closed");
	const GameObjBlueprint* peasant = gs.findBlueprint(Tags::GAMEOBJCLASS_CHARACTER, "Peasant");
	const int move = gs.commands.names.getIndex("Move");
	if (!player || !enemy || !cliPlayer || !cliGate || !manor || !gateFoundation || !gateClosed || !peasant || move < 0 || !server.terrain || !server.tiles || !client.tiles) {
		printf("GATEWALL not set up\nGATEWALL all 0\n");
		return;
	}
	for (ServerGameObject* p : { player, enemy }) {
		p->setItem(Tags::PDITEM_FOOD, 1e6f);
		p->setItem(Tags::PDITEM_WOOD, 1e6f);
		p->setItem(Tags::PDITEM_GOLD, 1e6f);
	}
	const auto [tw, th] = server.terrain->getNumPlayableTiles();
	auto tileCentre = [](const GameObjBlueprint* bp, int tx, int tz) {
		return Vector3(tx * 5.0f + 2.5f + bp->footprint->originX, 0.0f, tz * 5.0f + 2.5f + bp->footprint->originZ);
	};
	// Manors stamped down one at a time, on a grid of spots from near the start outwards: each
	// run is tried on the first city that has a site for it
	ServerGameObject* first = nullptr;
	ServerGameObject* city = nullptr;
	int x0 = 0, z0 = 0, x1 = 0, z1 = 0;
	int nextSpot = 0, manors = 0;
	auto nextCity = [&]() {
		for (; nextSpot < 12 * 12; nextSpot++) {
			const int tx = 50 + (nextSpot % 12) * 10, tz = 50 + (nextSpot / 12) * 10;
			ServerGameObject* m = server.stampdownObject(manor, player, tileCentre(manor, tx, tz), Vector3(0, 0, 0), true, true);
			if (!m || !m->parent || m->parent->blueprint->bpClass != Tags::GAMEOBJCLASS_CITY)
				continue;
			nextSpot++;
			manors++;
			pump();
			first = m;
			city = (ServerGameObject*)m->parent;
			x0 = 1 << 30, z0 = 1 << 30, x1 = -1, z1 = -1;
			for (const auto& r : city->cityRectangles) {
				x0 = std::min(x0, r.xStart); z0 = std::min(z0, r.yStart);
				x1 = std::max(x1, r.xEnd); z1 = std::max(z1, r.yEnd);
			}
			printf("GATEWALL Manor %d at tile (%d, %d), city tiles (%d, %d) to (%d, %d)\n", manors, tx, tz, x0, z0, x1, z1);
			return true;
		}
		return false;
	};
	if (!nextCity()) {
		printf("GATEWALL no Manor stamped down\nGATEWALL all 0\n");
		return;
	}
	auto pieceOn = [&](int x, int z) -> ServerGameObject* {
		if (x < 0 || z < 0 || x >= tw || z >= th)
			return nullptr;
		ServerGameObject* o = server.tiles[z * tw + x].building.getFrom<Server>();
		return o && o->parent == city ? o : nullptr;
	};
	auto isStraight = [](const ServerGameObject* o) { return o && o->blueprint->buildingType == Tags::BUILDINGTYPE_WALL; };
	auto quarterOf = [](float y) { return ((int)std::lround(y / 1.5707963f) % 4 + 4) % 4; };
	// where the client's preview puts a building for a cursor on tile (cx, cz) (ClientInterface:
	// the tile's centre plus the turned origin)
	auto snapped = [&](const GameObjBlueprint* bp, int cx, int cz, float turn) {
		const auto o = bp->footprint->rotateOrigin(turn);
		Vector3 p(cx * 5.0f + 2.5f + o.first, 0.0f, cz * 5.0f + 2.5f + o.second);
		p.y = server.terrain->getHeight(p.x, p.z);
		return p;
	};
	int clientDiffers = 0, otherRefusals = 0;
	struct Placed { Vector3 at; float turn = 0; std::vector<std::pair<int, int>> tiles; ServerGameObject* gate = nullptr; };
	// along a run of the ring: the south one (z0, along x, no turn) and the west one (x0, along
	// z, a quarter): every cursor tile whose footprint lies on four straight pieces with a
	// straight piece beyond each end; the server's verdicts counted, the client's compared, and the
	// first valid one (from the run's middle) taken. Sites on sloping ground beside the Manor are
	// refused as WKB.exe does (OVERLAPS_NEIGHBOURING_BUILDING, 0x59c4d0: only wall pieces are
	// left out of the neighbours).
	auto placeOn = [&](bool west, const std::string& run) {
		Placed r;
		r.turn = west ? 1.5707964f : 0.0f;
		const int len = west ? z1 - z0 + 1 : x1 - x0 + 1;
		std::map<int, int> verdicts;
		int sites = 0, differ = 0;
		for (int d = 0; d < len; d++) {
			const int k = len / 2 + ((d & 1) ? -(d + 1) / 2 : d / 2);
			const int cx = west ? x0 : x0 + k, cz = west ? z0 + k : z0;
			const Vector3 at = snapped(gateFoundation, cx, cz, r.turn);
			const StampdownPlan plan = StampdownPlan::getStampdownPlan(&server, player, gateFoundation, at, Vector3(0, r.turn, 0));
			if (plan.tiles.size() != 4)
				continue;
			bool onRun = true;
			std::vector<std::pair<int, int>> tiles;
			for (const auto& t : plan.tiles) {
				onRun &= isStraight(pieceOn(t.x, t.z));
				tiles.emplace_back(t.x, t.z);
			}
			std::sort(tiles.begin(), tiles.end());
			const auto [ax, az] = tiles.front();
			const auto [bx, bz] = tiles.back();
			const int ex = west ? 0 : 1, ez = west ? 1 : 0;
			if (!onRun || !isStraight(pieceOn(ax - ex, az - ez)) || !isStraight(pieceOn(bx + ex, bz + ez)))
				continue;
			sites++;
			verdicts[(int)plan.status]++;
			const StampdownPlan cp = StampdownPlan::getStampdownPlan(&client, cliPlayer, cliGate, at, Vector3(0, r.turn, 0));
			if (cp.status != plan.status || cp.toRemove.size() != plan.toRemove.size()) {
				printf("GATEWALL %s at (%.1f, %.1f): the server says %d (%zu to remove), the client %d (%zu)\n", run.c_str(), at.x, at.z,
					(int)plan.status, plan.toRemove.size(), (int)cp.status, cp.toRemove.size());
				differ++;
			}
			if (r.tiles.empty() && plan.status == StampdownPlan::Status::Valid) {
				r.at = at;
				r.tiles = tiles;
			}
		}
		printf("GATEWALL %s: %d sites on four straight pieces;", run.c_str(), sites);
		for (const auto& [s, n] : verdicts)
			printf(" status %d x %d,", s, n);
		printf("\n");
		clientDiffers += differ;
		for (const auto& [s, n] : verdicts)
			if (s != (int)StampdownPlan::Status::Valid && s != (int)StampdownPlan::Status::InvalidNeighbour)
				otherRefusals += n;
		return r;
	};
	for (bool west : { false, true }) {
		const std::string run = west ? "west run (turned a quarter)" : "south run";
		Placed p = placeOn(west, run);
		// none here (the ground too uneven beside the Manor): another Manor elsewhere
		while (p.tiles.empty() && manors < 8 && nextCity())
			p = placeOn(west, run);
		check(!p.tiles.empty(), run + ": a site on four straight pieces the gatehouse may take");
		if (p.tiles.empty())
			continue;
		const StampdownPlan plan = StampdownPlan::getStampdownPlan(&server, player, gateFoundation, p.at, Vector3(0, p.turn, 0));
		printf("GATEWALL %s: at (%.1f, %.1f) turn %.2f, status %d, %zu to remove\n", run.c_str(), p.at.x, p.at.z, p.turn, (int)plan.status, plan.toRemove.size());
		check(plan.toRemove.size() == 4, run + ": it takes the four pieces down");
		// the placing interface pointed at one of those pieces (the client's copy, as hovered),
		// the cursor's ground point one and two tiles behind the wall (inside the city: where the
		// ray through a standing wall meets the ground), the gatehouse unturned and turned: onto
		// the wall, a valid plan taking the four pieces
		{
			pump(); // (the client's copy of the walls, built since by the walk's part)
			const auto [hx, hz] = p.tiles[1];
			const ClientGameObject* hovered = nullptr;
			if (const ServerGameObject* s = pieceOn(hx, hz))
				hovered = client.findObject(s->id);
			int onto = 0, tries = 0;
			// (the eye outside the wall, looking in: 1 tile behind at the usual pitch, 0.9 rad,
			// where the ray is 6.3 m up over the wall; 2 tiles behind at a flatter 0.6)
			for (const int behind : { 1, 2 })
				for (const int startQuarter : { 0, 1 }) {
					tries++;
					Vector3 ground(hx * 5.0f + 2.5f + (west ? behind * 5.0f : 0.0f), 0.0f, hz * 5.0f + 2.5f + (west ? 0.0f : behind * 5.0f));
					ground.y = client.terrain->getHeight(ground.x, ground.z);
					const float pitch = behind == 1 ? 0.9f : 0.6f, back = 40.0f / std::tan(pitch);
					const Vector3 eye = ground + Vector3(west ? -back : 0.0f, 40.0f, west ? 0.0f : -back);
					const CommonGameObject* pointed = StampdownPlan::wallOnRay(&client, eye, ground);
					int quarter = startQuarter;
					Vector3 at;
					const bool snapped = pointed == hovered && StampdownPlan::gatehouseOntoWall(&client, cliPlayer, cliGate, pointed, ground, quarter, at);
					const StampdownPlan sp = StampdownPlan::getStampdownPlan(&server, player, gateFoundation, at, Vector3(0, quarter * 1.5707964f, 0));
					const bool ok = snapped && sp.status == StampdownPlan::Status::Valid && sp.toRemove.size() == 4 && quarter % 2 == (west ? 1 : 0);
					if (!ok)
						printf("GATEWALL %s pointed at (%d, %d), ground %d behind, turn %d: snapped %d to (%.1f, %.1f) turn %d, status %d\n", run.c_str(), hx, hz,
							behind, startQuarter, (int)snapped, at.x, at.z, quarter, (int)sp.status);
					onto += ok;
				}
			check(hovered && onto == tries, run + ": pointed at the wall, the preview goes onto it (" + std::to_string(onto) + " of " + std::to_string(tries) + ")");
		}
		// across the run (turned the other way) it stands on one piece and off the wall: refused
		const float across = west ? 0.0f : 1.5707964f;
		const auto [mx, mz] = p.tiles[1];
		const StampdownPlan crossing = StampdownPlan::getStampdownPlan(&server, player, gateFoundation, snapped(gateFoundation, mx, mz, across), Vector3(0, across, 0));
		printf("GATEWALL %s across: status %d\n", run.c_str(), (int)crossing.status);
		// (the wall's own tile is red: a gatehouse takes no piece lying across it)
		const bool wallTileRed = std::any_of(crossing.tiles.begin(), crossing.tiles.end(), [&](const StampdownPlan::TileVerdict& t) {
			return isStraight(pieceOn(t.x, t.z)) && !t.ok; });
		check(crossing.status != StampdownPlan::Status::Valid && crossing.toRemove.empty() && wallTileRed, run + ": across the run it is refused, on the wall's tile");
		std::vector<SrvGORef> pieces;
		for (const auto& [x, z] : p.tiles)
			pieces.emplace_back(pieceOn(x, z));
		p.gate = server.stampdownObject(gateFoundation, player, p.at, Vector3(0, p.turn, 0), true, true);
		pump();
		check(p.gate != nullptr, run + ": the server stamps it down");
		if (!p.gate)
			continue;
		int gone = 0;
		for (auto& ref : pieces)
			if (!ref.get() || ref->deleted)
				gone++;
		check(gone == 4, run + ": the four pieces are gone (" + std::to_string(gone) + ")");
		check(p.gate->parent == city, run + ": the gatehouse is in the wall's city");
		int own = 0;
		for (const auto& [x, z] : p.tiles)
			own += server.tiles[z * tw + x].building.getFrom<Server>() == p.gate;
		check(own == 4, run + ": it takes the four tiles (" + std::to_string(own) + ")");
		const auto [ax, az] = p.tiles.front();
		const auto [bx, bz] = p.tiles.back();
		const int ex = west ? 0 : 1, ez = west ? 1 : 0;
		ServerGameObject* endA = pieceOn(ax - ex, az - ez);
		ServerGameObject* endB = pieceOn(bx + ex, bz + ez);
		check(isStraight(endA) && isStraight(endB) && quarterOf(endA->orientation.y) % 2 == quarterOf(p.gate->orientation.y) % 2
			&& quarterOf(endB->orientation.y) % 2 == quarterOf(p.gate->orientation.y) % 2,
			run + ": the wall's ends meet the gatehouse, along its line");
		// a second gatehouse over the first one: refused
		const StampdownPlan again = StampdownPlan::getStampdownPlan(&server, player, gateFoundation, p.at, Vector3(0, p.turn, 0));
		check(again.status != StampdownPlan::Status::Valid, run + ": no gatehouse on a gatehouse");
		if (west)
			continue;
		// the walk: the walls built (the foundations swapped for their finished buildings, the
		// gatehouse's the open one), a peasant 3 tiles inside walks to 3 tiles outside
		std::vector<ServerGameObject*> foundations;
		for (const auto& [bpid, objlist] : city->children)
			for (CommonGameObject* c : objlist)
				if (c->blueprint->bpClass == Tags::GAMEOBJCLASS_BUILDING && c->blueprint->name.ends_with(" Foundation") && c != first)
					foundations.push_back((ServerGameObject*)c);
		ServerGameObject* openGate = nullptr;
		for (ServerGameObject* o : foundations) {
			const std::string built = o->blueprint->name.substr(0, o->blueprint->name.size() - 11);
			if (const GameObjBlueprint* bp = gs.findBlueprint(Tags::GAMEOBJCLASS_BUILDING, built)) {
				const Vector3 at = o->position, turn = o->orientation;
				const bool mine = o == p.gate;
				server.destroyObject(o);
				ServerGameObject* b = server.spawnObject(bp, city, at, turn);
				if (mine)
					openGate = b;
			}
		}
		check(openGate && openGate->blueprint->buildingType == Tags::BUILDINGTYPE_GATEHOUSE, "the walls and the gatehouse built");
		if (!openGate)
			continue;
		const float gx = (p.tiles[1].first + p.tiles[2].first) * 2.5f + 2.5f;
		const float gz = p.tiles[1].second * 5.0f + 2.5f;
		auto ground = [&](float x, float z) { return Vector3(x, server.terrain->getHeight(x, z), z); };
		const Vector3 inside = ground(gx, gz + 15.0f), outside = ground(gx, gz - 15.0f);
		auto crosses = [&](ServerGameObject* unit, const Vector3& to, float seconds, float& left) {
			gs.commands[move].execute(unit, nullptr, Tags::ORDERASSIGNMODE_FORGET_EVERYTHING_ELSE, to);
			const float start = server.timeManager.currentTime;
			bool onGate = false;
			while (server.timeManager.currentTime < start + seconds) {
				server.timeManager.pendingMs = server.timeManager.stepMs;
				if (server.timeManager.step())
					server.simulate();
				const int ux = (int)(unit->position.x / 5.0f), uz = (int)(unit->position.z / 5.0f);
				for (const auto& [x, z] : p.tiles)
					onGate |= x == ux && z == uz;
			}
			left = std::sqrt((unit->position - to).len2xz());
			return onGate;
		};
		server.timeManager.unpause();
		server.timeManager.stepMs = 20;
		ServerGameObject* walker = server.spawnObject(peasant, player, inside, Vector3(0, 0, 0));
		float left = 0;
		const bool through = crosses(walker, outside, 40.0f, left);
		printf("GATEWALL open gate: the peasant through %d, %.1f m from the point outside\n", (int)through, left);
		check(through && left < 3.0f, "a peasant walks out through the open gate");
		// closed: the gatehouse's closed version blocks all its tiles, for its own player and
		// an enemy alike (the scripts open and close it by command)
		const Vector3 gateAt = openGate->position, gateTurn = openGate->orientation;
		server.destroyObject(openGate);
		ServerGameObject* closed = server.spawnObject(gateClosed, city, gateAt, gateTurn);
		float walkerLeft = 0;
		const bool backIn = crosses(walker, inside, 40.0f, walkerLeft);
		check(closed && !backIn, "with the gate closed the peasant doesn't cross it");
		ServerGameObject* foe = server.spawnObject(peasant, enemy, ground(gx + 5.0f, gz - 15.0f), Vector3(0, 0, 0));
		const bool foeIn = crosses(foe, inside, 40.0f, left);
		printf("GATEWALL closed gate: own peasant crossed %d (%.1f m left), enemy crossed %d (%.1f m left)\n", (int)backIn, walkerLeft, (int)foeIn, left);
		check(!foeIn, "with the gate closed an enemy doesn't cross it");
		server.timeManager.pause();
	}
	check(clientDiffers == 0, "the client's preview agrees with the server on every site (" + std::to_string(clientDiffers) + " differ)");
	// the straight sites refused were refused for the levelling only, as in WKB.exe
	check(otherRefusals == 0, "sites on four straight pieces refused only by the levelling's neighbour rule (" + std::to_string(otherRefusals) + " otherwise)");
	// a gatehouse over a corner piece, along the run that reaches it from the other side (the
	// corner's arms run along -x and -z turned with it, so the gatehouse along the run is across
	// one arm): refused. The ring's four outer corners, the first that has a piece.
	{
		const int corners[4][2] = { { x0, z1 }, { x1, z1 }, { x0, z0 }, { x1, z0 } };
		ServerGameObject* corner = nullptr;
		int cx = 0, cz = 0;
		for (const auto& c : corners)
			if (ServerGameObject* o = pieceOn(c[0], c[1]); o && o->blueprint->buildingType == Tags::BUILDINGTYPE_WALL_CORNER_OUT) {
				corner = o, cx = c[0], cz = c[1];
				break;
			}
		if (corner) {
			// along x, the corner on its second tile and the rest on the run; and along z
			const int inward = cx == x0 ? 1 : -1;
			int refused = 0;
			for (const float turn : { 0.0f, 1.5707964f }) {
				const int qx = turn == 0.0f ? cx + inward : cx, qz = turn == 0.0f ? cz : cz + (cz == z0 ? 1 : -1);
				const StampdownPlan plan = StampdownPlan::getStampdownPlan(&server, player, gateFoundation, snapped(gateFoundation, qx, qz, turn), Vector3(0, turn, 0));
				const bool onCorner = std::any_of(plan.tiles.begin(), plan.tiles.end(), [&](const StampdownPlan::TileVerdict& t) { return t.x == cx && t.z == cz; });
				printf("GATEWALL corner (%d, %d) %s, turn %.2f: on it %d, status %d\n", cx, cz, corner->blueprint->name.c_str(), turn, (int)onCorner, (int)plan.status);
				refused += onCorner && plan.status != StampdownPlan::Status::Valid;
			}
			// one of the two lies along the corner's turned axis, as WKB.exe's 0x609cb0 allows
			// (0x59bd63 takes all four piece types); the other crosses it
			check(refused >= 1, "over an outer corner, across its axis, it is refused");
		}
		else
			check(false, "an outer corner piece found");
	}
	// for a look in game: one more city, its walls built, saved as saved\Save_Games\
	// GatehouseOnWall.sav with the first free site on its south run printed (the cursor's spot)
	for (int k = 0; k < 6 && nextCity(); k++) {
		std::vector<ServerGameObject*> foundations;
		for (const auto& [bpid, objlist] : city->children)
			for (CommonGameObject* c : objlist)
				if (c->blueprint->bpClass == Tags::GAMEOBJCLASS_BUILDING && c->blueprint->name.ends_with(" Foundation"))
					foundations.push_back((ServerGameObject*)c);
		for (ServerGameObject* o : foundations)
			if (const GameObjBlueprint* bp = gs.findBlueprint(Tags::GAMEOBJCLASS_BUILDING, o->blueprint->name.substr(0, o->blueprint->name.size() - 11))) {
				const Vector3 at = o->position, turn = o->orientation;
				server.destroyObject(o);
				server.spawnObject(bp, city, at, turn);
			}
		pump();
		const Placed site = placeOn(false, "saved city's south run");
		if (site.tiles.empty())
			continue;
		if (server.clientPlayerObjects.empty())
			server.clientPlayerObjects.emplace_back(player->id);
		const std::string path = PathToUtf8(g_userPath / "saved" / "Save_Games" / "GatehouseOnWall.sav");
		printf("GATEWALL saved %d to %s: the site at (%.1f, %.1f, %.1f), the Manor at (%.1f, %.1f)\n", (int)server.saveGame(path.c_str()), path.c_str(),
			site.at.x, site.at.y, site.at.z, first->position.x, first->position.z);
		break;
	}
	printf("GATEWALL all %d\n", failures == 0 ? 1 : 0);
	fflush(stdout);
}

// The trees under the fog of war (FogOfWar's tree memory) on 4 Peaks, A (1027) and B (1030): all
// known at the start; one felled and one used up out of A's sight stay as A saw them, a new one
// unknown, until A sees there; one felled where only B sees is updated once B shares its sight
// with A as an ally; no sound from a fogged tree; the memory through the save
void Test_TreeMemory()
{
	int failures = 0;
	auto check = [&](bool ok, const std::string& what) {
		if (!ok) failures++;
		printf("UNITTEST %s: %s\n", ok ? "ok" : "FAILED", what.c_str());
		fflush(stdout);
	};
	LoadBCP("data.bcp");
	Server::clearFogOfWar = false;
	Server server;
	Client clientA(true), clientB(true);
	NetLocalBuffer a1, a2, b1, b2;
	NetLocalLink srvA(&a1, &a2), cliA(&a2, &a1), srvB(&b1, &b2), cliB(&b2, &b1);
	server.addClient(&srvA);
	server.addClient(&srvB);
	clientA.serverLink = &cliA;
	clientB.serverLink = &cliB;
	server.loadSaveGame("Save_Games\\2 Player 4 Peaks.lvl");
	server.setClientPlayerControl(0, 1027);
	server.setClientPlayerControl(1, 1030);
	server.timeManager.unpause();
	server.startLevel();
	FogOfWar fog; // A's
	clientA.treeMemory = &fog;
	fog.memoryOn = true;
	auto step = [&](int frames) {
		for (int i = 0; i < frames; i++) {
			server.tick();
			clientA.tick();
			clientB.tick();
			SDL_Delay(20);
		}
	};
	// the fog's update, 250 ms apart
	auto update = [&]() { step(14); fog.update(&clientA); };
	step(10);
	update();
	ServerGameObject* A = server.findObject(1027);
	ServerGameObject* B = server.findObject(1030);
	check(fog.treesStarted() && fog.treeGhosts.empty() && fog.treeUnknown.empty(), "at the start: every tree known as it stands");
	// the trees out of A's sight: the farthest from A's units, and one in B's sight
	std::vector<ServerGameObject*> trees;
	std::vector<Vector3> unitsB;
	auto find = [&](CommonGameObject* obj, auto& self) -> void {
		for (const auto& [type, children] : obj->children)
			for (CommonGameObject* child : children) {
				if (child->blueprint->name == "Coniferous Tree" || child->blueprint->name == "Deciduous Tree")
					trees.push_back(child->dyncast<ServerGameObject>());
				if (child->blueprint->bpClass == Tags::GAMEOBJCLASS_CHARACTER && child->getPlayer() == B)
					unitsB.push_back(child->position);
				self(child, self);
			}
	};
	find(server.getLevel(), find);
	ServerGameObject *felled = nullptr, *usedUp = nullptr, *nearB = nullptr;
	float nearest = 1e9f;
	auto distB = [&](ServerGameObject* t) {
		float d = 1e9f;
		for (const Vector3& u : unitsB)
			d = std::min(d, (u - t->position).len2xz());
		return d;
	};
	for (ServerGameObject* t : trees)
		if (!fog.isSeen(t->position) && distB(t) < nearest) {
			nearest = distB(t);
			nearB = t;
		}
	for (ServerGameObject* t : trees) {
		if (fog.isSeen(t->position))
			continue;
		const bool byB = distB(t) < 80.0f;
		if (byB)
			continue;
		if (!felled)
			felled = t;
		else if (!byB && !usedUp && (t->position - felled->position).len2xz() > 30.0f)
			usedUp = t;
	}
	const GameObjBlueprint* fallenBp = server.gameSet->findBlueprint(Tags::GAMEOBJCLASS_CONTAINER, "Fallen Tree");
	const GameObjBlueprint* treeBp = server.gameSet->findBlueprint(Tags::GAMEOBJCLASS_CONTAINER, "Coniferous Tree");
	if (!A || !B || !felled || !usedUp || !nearB || !fallenBp || !treeBp) {
		printf("UNITTEST FAILED (not set up: %zu trees, B's units %zu)\n", trees.size(), unitsB.size());
		return;
	}
	const uint32_t felledId = felled->id, usedUpId = usedUp->id, nearBId = nearB->id;
	const Vector3 usedUpAt = usedUp->position;
	const std::string felledType = felled->blueprint->name;
	printf("UNITTEST %zu trees; felled %u (%s) at (%.0f, %.0f), used up %u, by B's units %u (%.0f m)\n", trees.size(), felledId, felledType.c_str(),
		felled->position.x, felled->position.z, usedUpId, nearBId, nearest);

	// felled out of sight (as Tree Chopping.cpp's "Falling Version"), another used up and gone, a
	// new tree out of sight
	felled->convertTo(fallenBp);
	usedUp->terminate();
	const Vector3 newAt = felled->position + Vector3(8, 0, 8);
	ServerGameObject* grown = server.spawnObject(treeBp, server.getLevel(), newAt, Vector3(0, 0, 0));
	const uint32_t grownId = grown ? grown->id : 0;
	nearB->convertTo(fallenBp);
	update();
	ClientGameObject* liveFelled = clientA.findObject(felledId);
	check(liveFelled && liveFelled->blueprint == fallenBp, "A's client has the felled tree as the server: fallen");
	auto ghost = [&](uint32_t id) -> ClientGameObject* { auto it = fog.treeGhosts.find(id); return it == fog.treeGhosts.end() ? nullptr : it->second.get(); };
	check(ghost(felledId) && ghost(felledId)->blueprint->name == felledType && liveFelled && fog.treeHidden(liveFelled),
		"felled out of sight: A still sees it standing");
	ClientGameObject* liveUsed = clientA.findObject(usedUpId);
	check(ghost(usedUpId) && ghost(usedUpId)->blueprint->name.find("Tree") != std::string::npos,
		std::string("used up out of sight: still standing for A (the live one ") + (liveUsed ? "terminated" : "removed") + ")");
	check(grownId && fog.treeUnknown.count(grownId) && clientA.findObject(grownId) && fog.treeHidden(clientA.findObject(grownId)),
		"a tree grown out of sight: unknown to A");
	check(ghost(nearBId) != nullptr, "felled where only B sees: standing for A");
	check(liveFelled && fog.hidesTreeEffect(&clientA, liveFelled), "no sound from the fogged tree");
	check(fog.treeGhosts.size() == 3 && fog.treeUnknown.size() == 1, "nothing else remembered (" + std::to_string(fog.treeGhosts.size()) + " copies, "
		+ std::to_string(fog.treeUnknown.size()) + " unknown)");
	printf("UNITTEST memory: %zu copies of %zu bytes and %zu unknown ids\n", fog.treeGhosts.size(), sizeof(ClientGameObject), fog.treeUnknown.size());

	// the save: the local server has A's memory; written to the file, read back
	const std::string mine = fog.treeMemoryText(&clientA);
	check(server.getTreeMemory(1027) == mine && mine.find("ghost " + std::to_string(felledId) + " ") != std::string::npos
		&& mine.find("unknown " + std::to_string(grownId)) != std::string::npos, "the server holds A's memory for a save");
	const std::string file = PathToUtf8(g_userPath / "saved" / PathFromUtf8("Save_Games\\wkbai tree memory test.sav"));
	check(server.saveGame(file.c_str()), "saved");
	{
		const Server::SaveParts parts = Server::ReadSaveParts(file.c_str());
		check(parts.saveFile && parts.treeMemory.find("player 1027\n") != std::string::npos, "the save's treeMemory section");
		server.treeMemoryLoadText(parts.treeMemory);
		check(server.getTreeMemory(1027) == mine, "read back as written");
		FogOfWar loaded;
		loaded.loadTreeMemory(&clientA, server.getTreeMemory(1027));
		check(loaded.treeMemoryText(&clientA) == mine && loaded.treeGhosts.count(felledId) && loaded.treeGhosts.at(felledId)->blueprint->name == felledType
			&& (loaded.treeGhosts.at(felledId)->position - fog.treeGhosts.at(felledId)->position).len3() < 0.001f, "loaded: the same memory");
		// an older save (none): every tree known as it stands
		const Server::SaveParts old = Server::ReadSaveParts("Save_Games\\2 Player 4 Peaks.lvl");
		FogOfWar fresh;
		fresh.loadTreeMemory(&clientA, old.treeMemory);
		check(old.treeMemory.empty() && fresh.treeGhosts.empty() && fresh.treeUnknown.empty(), "an older save: no memory, all trees known");
	}
	{
		std::error_code ec;
		std::filesystem::remove(PathFromUtf8(file), ec);
	}

	// A sees there: the felled tree fallen, the used up one gone, the new one there
	fog.testViewers.push_back({ felled->position, 25.0f });
	update();
	check(!ghost(felledId) && liveFelled && !fog.treeHidden(liveFelled), "seen: A sees the felled tree fallen");
	check(!fog.treeUnknown.count(grownId), "seen: and the new tree");
	fog.testViewers.clear();
	fog.testViewers.push_back({ usedUpAt, 25.0f });
	update();
	check(!ghost(usedUpId), "seen: the used up tree gone");
	fog.testViewers.clear();
	// B's sight shared with A: allies, Shared Vision (Reconnaissance Upgrade Status 2)
	update();
	check(ghost(nearBId) != nullptr, "an enemy's sight: nothing for A");
	server.setDiplomaticStatus(A, B, 0);
	const int recon = server.gameSet->items.names.getIndex("Reconnaissance Upgrade Status");
	if (recon >= 0)
		B->setItem(recon, 2.0f);
	update();
	update();
	check(recon >= 0 && fog.isSeen(nearB->position) && !ghost(nearBId), "an ally's shared sight: A sees it fallen");
	// a new game: the memory afresh
	fog.resetTrees();
	check(!fog.treesStarted() && fog.treeGhosts.empty(), "a new game: no memory until it starts");
	clientA.treeMemory = nullptr;
	printf(failures ? "UNITTEST FAILED (%d)\n" : "UNITTEST PASSED\n", failures);
}

#include "Language.h"
#include <sol/sol.hpp>
void Test_TextLineBreaks()
{
	bool all = true;
	auto check = [&](const char* what, bool ok) { printf("BREAKS %s %d\n", what, ok ? 1 : 0); all &= ok; };

	// the escape: "\n" a line break, "\\" and "\c" kept for the readers that interpret them
	check("decode", Language::decodeLineBreaks("a\\nb") == "a\nb" && Language::decodeLineBreaks("a\\\\nb") == "a\\\\nb"
		&& Language::decodeLineBreaks("\\cAdvisor:\\nLord") == "\\cAdvisor:\nLord" && Language::decodeLineBreaks("end\\") == "end\\"
		&& Language::decodeLineBreaks("plain") == "plain");

	// the real Language.txt: the lobby's question has a line break, no backslash
	LoadBCP("data.bcp");
	Language lang;
	lang.load("Languages\\Language.txt");
	const std::string text = lang.getText("FRONT_END_CHAT_SINGLE_PLAYER_GAME");
	check("loaded", text.find('\n') != std::string::npos && text.find('\\') == std::string::npos);
	size_t escaped = 0;
	for (const auto& [key, value] : lang.text)
		for (size_t p = value.find("\\n"); p != std::string::npos; p = value.find("\\n", p + 2))
			if (p == 0 || value[p - 1] != '\\')
				escaped++;
	check("none left", escaped == 0);

	// FrontEndUI.lua's wrapText (as the dialog wraps it): its own line breaks kept, each line
	// wrapped at the width; textWidth here 10 pixels a character
	std::ifstream in("redata\\FrontEndUI.lua", std::ios::binary);
	std::string ui((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	ui.erase(std::remove(ui.begin(), ui.end(), '\r'), ui.end());
	const size_t from = ui.find("local function wrapText(");
	const size_t to = from == std::string::npos ? from : ui.find("\nend\n", from);
	check("wrapText found", to != std::string::npos);
	if (to != std::string::npos) {
		sol::state lua;
		lua.open_libraries(sol::lib::base, sol::lib::string, sol::lib::table);
		lua.set_function("textWidth", [](const std::string&, const std::string& s, float scale) { return 10.0f * (float)s.size() * scale; });
		sol::protected_function wrap = lua.script("S = 1\n" + ui.substr(from, to + 5 - from) + "return wrapText");
		auto shown = [&](const std::string& s, float width) {
			std::vector<std::string> lines;
			sol::table t = wrap("Proportional 20", s, width);
			for (size_t i = 1; i <= t.size(); i++) lines.push_back(t[i]);
			return lines;
		};
		// wide enough for each sentence: the two of them
		const auto wide = shown(text, 600.0f);
		for (const auto& l : wide) printf("BREAKS  wide | %s\n", l.c_str());
		check("two lines", wide == std::vector<std::string>{ "You have no opponents selected.", "Are you sure you want to start a single player game?" });
		// narrower: the second wrapped too, never joined to the first
		const auto narrow = shown(text, 320.0f);
		for (const auto& l : narrow) printf("BREAKS  narrow | %s\n", l.c_str());
		bool ok = narrow.size() == 3 && narrow[0] == "You have no opponents selected." && narrow[1].rfind("Are you", 0) == 0;
		for (const auto& l : narrow) ok &= l.size() * 10 <= 320 && l.find('\\') == std::string::npos;
		check("wrapped", ok);
	}
	printf("BREAKS all %d\n", all ? 1 : 0);
}

// The map catalog and describe (step 17.3, MapAuthoring.h; needs the game data): the catalog of
// the base game alone and with WKB Plus (WKB_Plus.bcp, when the game folder has it) against the
// game set's own counts (every class's blueprints) and the texture database file's (its
// TEXTURE_GROUP, TEXTURE and MATCHING lines and EDGE rules); then describe on three original
// levels and on test 98's PNG map (made first when it isn't there).
// "MAPCATALOG <check> <0|1> ..." and "MAPCATALOG all <0|1>"
void Test_MapCatalog()
{
	LoadBCP("data.bcp");
	const std::string gsFile = "Warrior Kings Game Set\\multi-player extensions.cpp";
	// the mods to try: none (the base game), and WKB Plus
	std::string plusId;
	for (const ModInfo& m : GetMods())
		if (StrCICompare(m.id, "WKB_Plus.bcp") == 0)
			plusId = m.id;
	const nlohmann::json configured = g_settings.value("disabledMods", nlohmann::json::array());
	bool all = true;
	std::vector<std::pair<std::string, size_t>> totals;
	for (int run = 0; run < (plusId.empty() ? 1 : 2); run++) {
		nlohmann::json disabled = nlohmann::json::array();
		for (const ModInfo& m : GetMods())
			if (run == 0 || m.id != plusId)
				disabled.push_back(m.id);
		g_settings["disabledMods"] = disabled;
		InitFileSystem();
		const std::string label = run == 0 ? "base" : "WKB Plus";
		GameSet gs(gsFile.c_str(), g_settings.value<int>("gameVersion", 0));
		const nlohmann::ordered_json c = MapAuthoring::Catalog(gs, gsFile, run == 0);
		bool ok = true;
		size_t listed = 0, own = 0;
		for (int k = 0; k < Tags::GAMEOBJCLASS_COUNT; k++) {
			std::string cls = Tags::GAMEOBJCLASS_tagDict.tags[k];
			for (char& ch : cls)
				ch = (char)tolower((unsigned char)ch);
			const size_t n = c["classes"][cls]["blueprints"].size();
			listed += n;
			own += gs.objBlueprints[k].size();
			if (n != gs.objBlueprints[k].size()) {
				printf("MAPCATALOG %s class %s: %zu listed, the game set has %zu\n", label.c_str(), cls.c_str(), n, gs.objBlueprints[k].size());
				ok = false;
			}
			// every name once
			std::set<std::string> names;
			for (const auto& b : c["classes"][cls]["blueprints"])
				names.insert(b["name"].get<std::string>());
			ok = ok && names.size() == n;
		}
		// the texture database against its file's own lines
		size_t groupsListed = 0, texturesListed = 0, rulesListed = 0, coloured = 0, withTerrain = 0;
		size_t groupLines = 0, textureLines = 0, edgeLines = 0;
		for (const auto& db : c["textureDatabases"]) {
			groupsListed += db["groups"].size();
			for (const auto& g : db["groups"]) {
				texturesListed += g["textures"].size();
				rulesListed += g["edgeRules"].get<size_t>();
				coloured += !g["meanColour"].is_null();
				withTerrain += !g["terrain"].is_null();
			}
			char* data = nullptr;
			int size = 0;
			LoadFile(db["file"].get<std::string>().c_str(), &data, &size, 1);
			data[size] = 0;
			std::istringstream in(std::string(data, size));
			free(data);
			for (std::string line; std::getline(in, line);) {
				std::istringstream ls(line);
				std::string tag;
				ls >> tag;
				groupLines += tag == "TEXTURE_GROUP";
				textureLines += tag == "TEXTURE" || tag == "MATCHING";
				edgeLines += tag == "EDGE";
			}
		}
		const bool texOk = groupsListed == groupLines && texturesListed == textureLines && rulesListed == edgeLines && coloured == groupsListed && groupsListed > 0;
		const auto& n = c["counts"];
		const bool restOk = n["skies"].get<int>() > 0 && n["trees"].get<int>() > 0 && c["environment"]["weathers"].size() == WEATHER_COUNT
			&& c["resources"].size() > 0 && c["terrains"].size() == gs.terrains.size();
		printf("MAPCATALOG %s blueprints %d (%zu listed, the game set's %zu)\n", label.c_str(), (int)(ok && listed == own), listed, own);
		printf("MAPCATALOG %s textures %d (%zu groups of %zu TEXTURE_GROUP lines, %zu textures of %zu TEXTURE/MATCHING lines, %zu edge rules of %zu EDGE lines, "
			"%zu with a mean colour, %zu with a terrain)\n", label.c_str(), (int)texOk, groupsListed, groupLines, texturesListed, textureLines, rulesListed, edgeLines, coloured, withTerrain);
		printf("MAPCATALOG %s rest %d (%d skies, %d trees, %zu terrains, %zu resources, %zu weathers)\n", label.c_str(), (int)restOk, n["skies"].get<int>(),
			n["trees"].get<int>(), c["terrains"].size(), c["resources"].size(), c["environment"]["weathers"].size());
		std::string perClass;
		for (const auto& [k, v] : n.items())
			if (k != "blueprints" && v.is_number() && c["classes"].contains(k))
				perClass += " " + k + "=" + std::to_string(v.get<int>());
		printf("MAPCATALOG %s per class:%s\n", label.c_str(), perClass.c_str());
		all = all && ok && listed == own && texOk && restOk;
		totals.emplace_back(label, listed);
		fflush(stdout);
	}
	if (totals.size() == 2)
		printf("MAPCATALOG mod adds %d (base %zu, with WKB Plus %zu blueprints)\n", (int)(totals[1].second > totals[0].second), totals[0].second, totals[1].second);
	else
		printf("MAPCATALOG no WKB_Plus.bcp in the game folder: the base game only\n");
	g_settings["disabledMods"] = configured;
	InitFileSystem();

	// describe: three original levels and the PNG map
	const std::string pngName = g_settings.value("mapFolderNewName", std::string("PNG Test"));
	if (!std::filesystem::exists(g_userPath / "saved" / "Maps" / PathFromUtf8(pngName + ".wkbai") / "map.json"))
		Test_MapFolderNew();
	const std::filesystem::path outDir = g_userPath / "describe";
	std::vector<std::string> maps = { "2 Player 4 Peaks.lvl", "2 Player Forking River.lvl", "8 Player Flynn's Fjords.lvl", "Maps\\" + pngName + ".wkbai" };
	for (const auto& m : g_settings.value("mapCatalogMaps", nlohmann::json::array()))
		maps.push_back(m.get<std::string>());
	int described = 0;
	for (const std::string& m : maps) {
		nlohmann::ordered_json d;
		std::string error, stem = m;
		for (char& ch : stem)
			if (ch == '\\' || ch == '/' || ch == ' ' || ch == '\'')
				ch = '_';
		const std::filesystem::path png = outDir / PathFromUtf8(stem + ".png");
		std::error_code ec;
		std::filesystem::remove(png, ec);
		const bool loaded = MapAuthoring::Describe(m, PathToUtf8(png), d, error);
		bool ok = loaded;
		if (loaded) {
			const int w = d["size"]["tiles"][0], h = d["size"]["tiles"][1];
			double share = 0;
			for (const auto& g : d["materials"]["textureGroups"])
				share += g["share"].get<double>();
			const int ppt = d["png"]["pixelsPerTile"], pw = d["png"]["size"][0], ph = d["png"]["size"][1];
			const bool pic = std::filesystem::file_size(png, ec) > 1000 && pw == w * ppt && ph == h * ppt;
			ok = w > 0 && h > 0 && d["heights"]["maxM"].get<double>() >= d["heights"]["minM"].get<double>() && std::abs(share - 1.0) < 1e-6
				&& d["objects"]["count"].get<int>() > 0 && d["players"].size() > 0 && pic;
			std::ofstream(outDir / PathFromUtf8(stem + ".json"), std::ios::binary) << d.dump(1) << "\n";
			printf("MAPCATALOG describe %d %s: %d x %d tiles, heights %.1f-%.1f m, %d lakes, %d tiles under water, %zu waterways, %zu groups (top %s %.2f), "
				"%d objects, %zu players, picture %dx%d\n", (int)ok, m.c_str(), w, h, d["heights"]["minM"].get<double>(), d["heights"]["maxM"].get<double>(),
				d["water"]["lakeCount"].get<int>(), d["water"]["tilesUnderWater"].get<int>(),
				d["water"]["design"].is_null() ? (size_t)0 : d["water"]["design"]["waterways"].size(), d["materials"]["textureGroups"].size(),
				d["materials"]["textureGroups"][0]["name"].get<std::string>().c_str(), d["materials"]["textureGroups"][0]["share"].get<double>(),
				d["objects"]["count"].get<int>(), d["players"].size(), pw, ph);
		}
		else
			printf("MAPCATALOG describe 0 %s: %s\n", m.c_str(), error.c_str());
		described += ok;
		fflush(stdout);
	}
	all = all && described == (int)maps.size();
	printf("MAPCATALOG described %d of %zu\n", described, maps.size());
	printf("MAPCATALOG all %d\n", (int)all);
	fflush(stdout);
}

// Step 17.4's test: MapTerrainTest.cpp
void Test_MapTerrain();

const std::vector<std::pair<void(*)(), const char*> > testList = {
{Test_GameSet, "Game set loading"},
{Test_GSFileParser, "GSF Parser"},
{Test_Server, "Server"},
{Test_Actions, "Actions"},
{Test_SDL, "SDL"},
{Test_ImGui, "ImGui"},
{Test_ServerExplorer, "Server Explorer"},
{Test_TrnTexDb, "Terrain Texture Database"},
{Test_Terrain, "Terrain"},
{Test_Mesh, "Mesh"},
{Test_Network, "Network"},
{Test_EnetServer, "Enet Server"},
{Test_EnetClient, "Enet Client"},
{Test_Anim, "Anim"},
{Test_Scene, "Scene"},
{Test_SoundPlayer, "Sound Player"},
{Test_Pathfinding, "Pathfinding"},
{Test_ParticleSystem, "Particle system"},
{Test_PFRayTraversal, "PF Ray Traversal"},
{Test_VFS, "VFS (file system and mods)"},
{Test_Stampdown, "Stampdown (building placement)"},
{Test_Guard, "Guard (follow at a distance)"},
{Test_Formation, "Formation (members walk into place)"},
{Test_SaveRoundTrip, "Save game round trip"},
{Test_RallyPoint, "Rally point (free spots)"},
{Test_Gather, "Gathering (the next tree)"},
{Test_Farm, "Farming (build a field, farm it, a second farmer)"},
{Test_Determinism, "Determinism (same seed, same game)"},
{[]() { printf("CRASHTEST crashing now\n"); fflush(stdout); volatile int* p = nullptr; *p = 1; }, "Crash (writes a crash report)"},
{Test_BCMRoundTrip, "BCM round trip (map editor's writer)"},
{Test_AIvsAI, "AI against AI (headless)"},
{Test_GltfRoundTrip, "glTF round trip (the game's meshes as .glb and back)"},
{Test_AutoWall, "Automatic walls and gatehouse around a Manor"},
{Test_Minimap, "Minimap drawn from the terrain against the maps' own"},
{Test_DDS, "DDS textures decoded (uncompressed, BC1, BC3, BC5)"},
{Test_Passability, "Passability fields (terrain types a unit can't stand on)"},
{Test_DiffRun, "Differential run (a save run for N game seconds, then saved)"},
{Test_TerrainNormals, "Terrain normals of 4 Peaks (a text file, for the lighting comparison)"},
{Test_Levelling, "Levelling: a barracks stamped down flattens the ground under it"},
{Test_WallRing, "Wall ring as a city grows (corners where rectangles meet)"},
{Test_PlacementRules, "Placement rules: slope limit, terrain types and neighbours, as WKB.exe's"},
{Test_MassOrder, "Mass order (an army of 200 sent across the map at once)"},
{Test_Siege, "Siege: a Mangonel told to attack the enemy Manor"},
{Test_GatherSave, "Gatherers through a save (woodcutters, miners, builder, farmer carry on)"},
{Test_Corpses, "Corpses: the fallen of a fight stay where they fell"},
{Test_HoverCursor, "Hover cursors: the command and cursor of a unit over a building"},
{Test_MoveArrival, "Move arrival (groups of 10, 50, 200, loose and in formation, with and without corpses)"},
{Test_CityArea, "City area: civic buildings far from the Manor along a grown city, client and server"},
{Test_TurnedPlacement, "Turned placement: a building turned by quarters with [ and ] before it is placed"},
{Test_Train, "Training: six units at one Training Dummy (4 slots, a spot each)"},
{Test_JobPositions, "Job positions: where each peasant job's worker stands at its building or target"},
{Test_BuildRequirements, "Build requirements: the build menu's commands before and after the Fort's research"},
{Test_MoonPhases, "Moon phases: lit part and side, the moon's light and shadows, the natural month, the saved setting"},
{Test_UnitHistory, "Unit history: kills credited (melee, archers, a garrisoned tower), no kill for a removal, kept by a save"},
{Test_UnitNames, "Unit names: by faction, unique, from the seed; conscripts; heroes, formations, a hero slain; kept by a save"},
{Test_UnitTimeline, "Unit timeline: the server's events name top killers, heroes present and fallen; Fallen events; the same from the same seed; kept by a save"},
{Test_UnitSplashKills, "Unit kills by splash (rockets at a cluster) and none for an Act of God (lightning)"},
{Test_ChronicleStory, "Chronicle story: the timeline's events told as a historian's paragraphs, the same every time"},
{Test_ChronicleSweep, "Chronicle sweep: every season, weather, time of day and kind of event told, the plain rules checked"},
{Test_KeyBindings, "Key bindings: a key rebound fires its action and the old one not, kept through wkconfig.json, cheat keys gated"},
{Test_LightningBuildings, "Lightning on buildings: a scratch (5-10%) and set alight, a soldier as before, nobody credited"},
{Test_GameOverView, "Game over view: after a loss or a win, no fog of war and every player's statistics; a player still fighting keeps both; kept by a save"},
{Test_TeamColours, "Team colours of HD models: wkTeam, wkEmblem and its TEXCOORD_1, the emblem slots, the materials apart"},
{Test_GameWindows, "Game windows: a province plays as a skirmish map unless from the campaign; mission objectives reach the client; text window layout and buttons"},
{Test_Diplomacy, "Diplomacy: offers, accepting, status items and events, tributes, the AI Generals' messages, Disallow Diplomacy"},
{Test_ValhallaArmy, "Valhalla army: the companies read, an enlisted army mustered at the start"},
{Test_HotKeys, "The original's keys: a selection key picks the nearest of its kind; a patrol there and back"},
{Test_Disguises, "Disguises and fog reveals: an enemy unit seen as a peasant, unmasked, dropped; revealed through the fog for a time"},
{Test_MinimapAlerts, "Minimap alerts: blinking dots and collapsing circles to the players listed, with colour and time"},
{Test_FormationFacing, "A standing formation turns to face an attack from the flank or rear"},
{Test_LobbyItems, "Lobby items: Team Placement, a renamed AI General, Unit Lives Infinite, Lock Server"},
{Test_CampaignInterface, "Campaign and tutorial actions: the interface off and on, objectives markers, UNLOCK_LEVEL"},
{Test_SpawnPoint, "Spawn point: a unit trained with no rally point comes out at the building's CR_ path, off its footprint"},
{Test_AgentInput, "Agent input: the agent's frames give the same input as the system's; its Ctrl; the system's dropped; the take-over key"},
{Test_BuildIdentity, "Build identity: the code hash, challenge answers, a code byte changed in memory, the build policies"},
{Test_ShaderCache, "Shader cache: a cold start fills it, a warm one compiles nothing, an edited shader or include alone, damaged entries, a driver update rebuilds all"},
{Test_ChimneySmoke, "Chimney smoke: wkSmoke nodes, PS_Chimney points and their rule, the houses' table, night/winter/always/day, the puffs"},
{Test_FarLandCache, "Far land cache: a cold load fills it with the same bytes, a warm one makes nothing, a changed map, generator or quality makes its own, damaged entries, the size cap"},
{Test_EnvironmentClock, "Environment clock: the debug's hour, season and sunset on a game whose environment has not started (an original's save)"},
{Test_BloodDrip, "Drips: wkDrip nodes (a fall, a point, the defaults), the rhythm, forming, falling, splash and pool, the same every time"},
{Test_WaypointFlags, "Waypoint flags: three queued move points stand three flags, two once the first is reached, none at the end; a new order forgets the queue"},
{Test_AutoFarmLevelled, "Farmer's farm: the farm a trained farmer gets is levelled like one placed by hand"},
{Test_SaveFormat, "Save format: wkbai's save file with sections, the original format, unknown sections, play on after loading"},
{Test_MinistryFramework, "Ministry AI framework: the blackboard, departments at their rates and staggered, reports, nothing changed in the game, the ai section through a save"},
{Test_MinistryIntelligence, "Ministry AI Intelligence: only what was seen (aiVision), the threat map, remembered and forgotten, through a save"},
{Test_MinistryWar, "Ministry AI war: no attack before the enemy is known, attacks as missions, no waves from the war layer, missions through a save"},
{Test_MinistryTactics, "Ministry AI tactics: training by counters, abilities (heal, curse) by the rule table"},
{Test_MinistryDiplomacy, "Ministry AI diplomacy: gratitude for a tribute, peace, a grudge and war, a real kill in the ledger, through a save"},
{Test_MinistryEconomy, "Ministry AI economy: the peasant cap, gold miners, research and repairs against the same game without"},
{Test_MinistryLua, "Ministry AI from Lua (reports, weights, missions) and its difficulty (aiReaction)"},
{Test_MinistryDefence, "Ministry AI defence: a raid met by as many as it needs, done when it's gone; an attack on the palace met by all, the missions recalled"},
{Test_NightCold, "Night cold: the day's temperature swing, coldest before sunrise and warmest in the early afternoon, smaller overcast, none without day and night, through a save"},
{Test_SmallerItems, "The smaller items as WKB.exe's: the sight scan once a second, Dangerous Buildings round a tower, turning arcs, stop spacing, stop spots, an uninterruptible loop"},
{Test_ModKeywords, "The keywords only mods use: values, finders, positions and actions as WKB.exe's (or as named where it has a bug)"},
{Test_FarmBuilders, "Farm builders: three peasants given three farms each; a finished farm keeps its nearest builder, who farms it"},
{Test_FireQuality, "Fire quality: Original, Low and High, the option and its config round trip, Original the old sprites, the flames, glow, smoke and shimmer"},
{Test_RainFire, "Rain and snow on burning buildings: rain puts a fire out, heavy rain sooner; no rain as before; snow slows it and melts; through a save"},
{Test_MapFolderImport, "Map folders: every original map imported into a .wkbai folder plays with the same game hash as the original"},
{Test_MapFolderNew, "Map folders: a map from a 16-bit PNG, one material, lakes and a Manor per player, compiled, loaded and played"},
{Test_RiverCurrents, "River currents: calm shallow water nothing, moving water carries and hurts, swim to the shore, off nothing, floods faster, through a save"},
{Test_Wind, "Wind: the lobby's settings and text, the target by the weather, veering and gusts, wind chill and the fires' factor, the save"},
{Test_WedgeCharge, "Wedge charge: riders in a Wedge charge a Line (an attack, a move through it); after contact most fight, none idle with an enemy in reach"},
{Test_GatehouseOnWall, "Gatehouse on a wall: over four straight pieces (turned too) it replaces them between the wall's ends; refused across a run, on a corner, on a gatehouse; walked through open, not closed"},
{Test_TreeMemory, "Trees under the fog of war: all known at the start, felled or used up out of sight still standing, new ones unknown, until seen (an ally's sight too), through a save"},
{Test_TextLineBreaks, "Text line breaks: Language.txt's \\n a real line break at load, the dialog's word wrap keeping it (the lobby's single player question)"},
{Test_MapCatalog, "Map catalog: every blueprint and texture group of the base game and WKB Plus against the game set's counts; describe on three original maps and the PNG map"},
{Test_MapTerrain, "Map terrain: three maps from three feature plans, one from a PNG heightmap, one from elevation files; walkable share and slopes, compiled and played"},
};

void LaunchTest()
{
	printf("Tests available:\n");
	for (size_t i = 0; i < testList.size(); i++) {
		printf(" %2zu: %s\n", i, testList[i].second);
	}
	printf("Launch test number? ");
	char inp[12] = {};
	// WKB_TEST=<n>: that test without asking (a test running itself again in a child process)
	if (const char* env = getenv("WKB_TEST"); env && *env)
		snprintf(inp, sizeof(inp), "%s", env);
	else
		gets_s(inp);
	printf("\n");
	size_t testnum = (size_t)atoi(inp);
	if (testnum >= testList.size()) testnum = 0;
	g_launchedTest = testnum;
	testList[testnum].first();
}
