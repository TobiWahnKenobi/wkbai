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
#include "FormationController.h"
#include <set>
#include "interface/QuickSkirmishMenu.h"
#include "gameset/finder.h"
#include "gameset/Commission.h"
#include "AIController.h"
#include "gameset/ScriptContext.h"
#include "gameset/actions.h"
#include "util/GSFileParser.h"
#include "util/SimRandom.h"
#include "StampdownPlan.h"
#include "interface/ChronicleNarrator.h"
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

// Same words, numbers equal within 0.05 (a unit's height snaps to the terrain again on loading)
static bool SameSaveLine(const std::string& a, const std::string& b)
{
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
									if (std::none_of(std::begin(civilians), std::end(civilians), [&](const char* c) { return n.find(c) != std::string::npos; })) {
										soldiers++;
										raised[player->id].insert(o->id);
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
				printf("AITEST %s t=%.0f player %u (%s): %d characters (%d soldiers, %zu raised, %d sent home wounded), %d buildings, food %.0f wood %.0f gold %.0f\n", when,
					server.timeManager.currentTime, player->id, player->blueprint->name.c_str(), characters, soldiers, raised[player->id].size(), player->aiController.retreated, buildings,
					player->getItem(Tags::PDITEM_FOOD), player->getItem(Tags::PDITEM_WOOD), player->getItem(Tags::PDITEM_GOLD));
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
		int startX = 20;
		for (int q = 0; q <= 4; q++) {
			const int turns = q % 4; // the fifth: without the orientation, unturned
			const Vector3 orientation(0.0f, turns * quarter, 0.0f);
			ServerGameObject* obj = nullptr;
			int tx = 0, tz = 0;
			// the first spot, along the map from the west, that takes it so turned
			for (int z = 60; z < th - 20 && !obj; z += 3)
				for (int x = startX; x < tw - 20 && !obj; x += 3) {
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
	std::vector<SrvGORef> all = ownUnits;
	all.insert(all.end(), enemyUnits.begin(), enemyUnits.end());
	bool sentAway = false;
	int lastPrint = -1;
	for (int step = 0; step * 20 < 120000; step++) {
		const float t = step * 0.02f;
		// every 3 s, the living attack the nearest living enemy (the formation as a whole)
		if (step % 150 == 0 && !sentAway) {
			if (ServerGameObject* tg = nearest(formation, enemyUnits))
				server.gameSet->commands[attack].execute(formation, tg, Tags::ORDERASSIGNMODE_FORGET_EVERYTHING_ELSE);
			// to everyone it set out with, the dead too: the AI's lists kept a unit's id, which its
			// corpse keeps (it is the unit converted in place); the corpses got "Attack" and walked
			for (auto& r : all) {
				if (!r || r->getParent() == formation)
					continue;
				if (ServerGameObject* tg = nearest(r.get(), r->getParent() == enemy ? ownUnits : enemyUnits))
					server.gameSet->commands[attack].execute(r.get(), tg, Tags::ORDERASSIGNMODE_FORGET_EVERYTHING_ELSE);
			}
		}
		// once the enemy is beaten (or after 60 s), the survivors walk 60 m away
		if (!sentAway && (t > 60.0f || !nearest(formation, enemyUnits))) {
			sentAway = true;
			printf("CORPSE t=%.1f survivors sent away\n", t);
			server.gameSet->commands[move].execute(formation, nullptr, Tags::ORDERASSIGNMODE_FORGET_EVERYTHING_ELSE, formation->position + Vector3(60, 0, 0));
			for (auto& r : all)
				if (r && r->getParent() != formation)
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
					return distToSpot(i) < 3.0f;
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
	printf("MOVEARRIVAL all arrived %d/%d\n", allArrived, allUnits);
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
	check(FindKeyConflicts(A::Pause, 0, KeyChord{ SDL_SCANCODE_K, 0 }).empty(), "K for Pause clashes with nothing");
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
};

void LaunchTest()
{
	printf("Tests available:\n");
	for (size_t i = 0; i < testList.size(); i++) {
		printf(" %2zu: %s\n", i, testList[i].second);
	}
	printf("Launch test number? ");
	char inp[12];
	gets_s(inp);
	printf("\n");
	size_t testnum = (size_t)atoi(inp);
	if (testnum >= testList.size()) testnum = 0;
	testList[testnum].first();
}
