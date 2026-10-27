// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#include "wkbre2.h"
#include "platform.h"

#include <map>
#include <string>
#include <cstring>
//#include <direct.h>
#include <utility>
#include <thread>
#include <fstream>
#include <sstream>
#include "gameset/gameset.h"
#include "file.h"
#include "gameset/values.h"
#include "server.h"
#include "window.h"
#include "gfx/renderer.h"
#include "imgui/imgui.h"
#include "imguiimpl.h"
#include "gfx/TextureCache.h"
#include "TrnTextureDb.h"
#include "terrain.h"
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
#include "scene.h"
#include <SDL3/SDL_timer.h>
#include "SoundPlayer.h"
#include "Pathfinding.h"
#include "ParticleSystem.h"
#include "gfx/DefaultParticleRenderer.h"
#include "ParticleContainer.h"
#include "settings.h"
#include "util/SimRandom.h"
#include "StampdownPlan.h"
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
// is gone the peasant should go on with the next tree. Prints "GATHER t=<s> order task target".
void Test_Gather()
{
	LoadBCP("data.bcp");
	Server server;
	server.loadSaveGame("Save_Games\\2 Player 4 Peaks.lvl");
	ServerGameObject* player = server.findObject(1027);
	const int chop = server.gameSet->commands.names.getIndex("Chop Tree");
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
				if (peasant && child->blueprint->name == "Coniferous Tree") {
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
	const int wood = server.gameSet->items.names.getIndex("Wood");
	tree->setItem(wood, 12.0f);
	SrvGORef treeRef = tree;
	printf("GATHER peasant %u, tree %u at %.0f m\n", peasant->id, tree->id, std::sqrt(best));
	server.timeManager.unpause();
	server.startLevel(); // as a game does: the level start reactions register the deliverable buildings
	server.gameSet->commands[chop].execute(peasant, tree, Tags::ORDERASSIGNMODE_FORGET_EVERYTHING_ELSE);
	const uint64_t start = SDL_GetTicks();
	int lastPrint = -1;
	while (SDL_GetTicks() - start < 80000) {
		server.tick();
		const int t = (int)((SDL_GetTicks() - start) / 1000);
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
				printf("GATHER t=%d %s | first tree %s | wood %.0f\n", t, now.c_str(), treeRef ? "there" : "gone", peasant->getItem(wood));
			}
		}
		SDL_Delay(16);
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
