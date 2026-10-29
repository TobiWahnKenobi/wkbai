// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

// wkbre2.cpp : définit le point d'entrée de l'application.
//

#include "wkbre2.h"
#include <cstdio>
#include <filesystem>
#include "settings.h"
#include "file.h"
#include "util/util.h"
#include <exception>
#include <fstream>
#include <vector>
#include <nlohmann/json.hpp>

#include "interface/QuickStartMenu.h"
#include "interface/FrontEnd.h"
#include "gfx/renderer.h"
#include "window.h"
#include <SDL3/SDL_mouse.h>
#include "imguiimpl.h"
#include "gfx/bitmap.h"
#include "gfx/RenderSettings.h"
#include "mesh.h"
#include "util/StriCompare.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <DbgHelp.h>
#undef LoadBitmap
#pragma comment(lib, "dbghelp.lib")

// Prints the exception and a symbolized stack to stdout (the log of scripted runs), then lets
// Windows handle the crash as usual.
static LONG WINAPI PrintCrash(EXCEPTION_POINTERS* info)
{
	const EXCEPTION_RECORD* rec = info->ExceptionRecord;
	printf("\nCRASH: exception 0x%08lX at %p", rec->ExceptionCode, rec->ExceptionAddress);
	if (rec->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && rec->NumberParameters >= 2)
		printf(" (%s address %p)", rec->ExceptionInformation[0] ? "writing" : "reading", (void*)rec->ExceptionInformation[1]);
	printf("\n");
	HANDLE process = GetCurrentProcess();
	SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);
	SymInitialize(process, nullptr, TRUE);
	CONTEXT ctx = *info->ContextRecord;
	STACKFRAME64 frame{};
	frame.AddrPC.Offset = ctx.Rip; frame.AddrPC.Mode = AddrModeFlat;
	frame.AddrFrame.Offset = ctx.Rbp; frame.AddrFrame.Mode = AddrModeFlat;
	frame.AddrStack.Offset = ctx.Rsp; frame.AddrStack.Mode = AddrModeFlat;
	for (int i = 0; i < 32 && StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, GetCurrentThread(), &frame, &ctx, nullptr,
		SymFunctionTableAccess64, SymGetModuleBase64, nullptr); i++) {
		char buffer[sizeof(SYMBOL_INFO) + 256] = {};
		SYMBOL_INFO* symbol = (SYMBOL_INFO*)buffer;
		symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
		symbol->MaxNameLen = 255;
		DWORD64 displacement = 0;
		IMAGEHLP_LINE64 line{ sizeof(IMAGEHLP_LINE64) };
		DWORD lineDisplacement = 0;
		const bool hasSymbol = SymFromAddr(process, frame.AddrPC.Offset, &displacement, symbol);
		const bool hasLine = SymGetLineFromAddr64(process, frame.AddrPC.Offset, &lineDisplacement, &line);
		printf("  #%02d %s", i, hasSymbol ? symbol->Name : "?");
		if (hasLine)
			printf("  %s:%lu", line.FileName, line.LineNumber);
		printf("\n");
	}
	fflush(stdout);

	// A crash report: a minidump in the user folder's "crashes" (for a debugger), and in normal
	// play a message saying where it is (test and benchmark runs leave no dialog on the screen)
	std::error_code ec;
	const std::filesystem::path dir = g_userPath / "crashes";
	std::filesystem::create_directories(dir, ec);
	char name[64];
	SYSTEMTIME st;
	GetLocalTime(&st);
	snprintf(name, sizeof(name), "crash-%04d%02d%02d-%02d%02d%02d.dmp", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
	const std::filesystem::path dumpPath = dir / name;
	HANDLE file = CreateFileW(dumpPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file != INVALID_HANDLE_VALUE) {
		MINIDUMP_EXCEPTION_INFORMATION mei{ GetCurrentThreadId(), info, FALSE };
		const bool ok = MiniDumpWriteDump(process, GetCurrentProcessId(), file,
			(MINIDUMP_TYPE)(MiniDumpWithThreadInfo | MiniDumpWithIndirectlyReferencedMemory), &mei, nullptr, nullptr);
		CloseHandle(file);
		printf("Crash report %s: %s\n", ok ? "written" : "failed", PathToUtf8(dumpPath).c_str());
		const bool scripted = g_settings.value("test", false) || g_settings.contains("benchmark") || g_settings.contains("menuTest")
			|| g_settings.contains("lobbyTest");
		if (ok && !scripted) {
			const std::string text = "wkbre2 has crashed. A crash report was saved to\n\n" + PathToUtf8(dumpPath)
				+ "\n\nPlease attach it (and wkbre2's log) when you report the problem.";
			MessageBoxA(nullptr, text.c_str(), "wkbre2", MB_ICONERROR);
		}
	}
	return EXCEPTION_CONTINUE_SEARCH;
}

// The game folder when wkconfig.json's "gamePath" has no data.bcp: the Steam install of Warrior
// Kings: Battles (Steam's folder from the registry, its other library folders from
// steamapps\libraryfolders.vdf). Empty if not found.
static std::filesystem::path FindSteamGameFolder()
{
	wchar_t steam[MAX_PATH] = {};
	DWORD size = sizeof(steam);
	if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath", RRF_RT_REG_SZ, nullptr, steam, &size) != ERROR_SUCCESS)
		return {};
	std::vector<std::filesystem::path> libraries = { std::filesystem::path(steam) };
	std::ifstream vdf(std::filesystem::path(steam) / "steamapps" / "libraryfolders.vdf");
	std::string line;
	while (std::getline(vdf, line)) {
		// "path"		"D:\\SteamLibrary"
		const size_t key = line.find("\"path\"");
		if (key == std::string::npos)
			continue;
		const size_t a = line.find('"', key + 6), b = line.rfind('"');
		if (a == std::string::npos || b <= a)
			continue;
		std::string path = line.substr(a + 1, b - a - 1);
		for (size_t i = 0; (i = path.find("\\\\", i)) != std::string::npos; i++)
			path.erase(i, 1);
		libraries.push_back(PathFromUtf8(path));
	}
	for (const auto& library : libraries) {
		const auto folder = library / "steamapps" / "common" / "Warrior Kings Battles";
		std::error_code ec;
		if (std::filesystem::exists(folder / "data.bcp", ec))
			return folder;
	}
	return {};
}
#endif

void LaunchTest();

// "exportGltf": [paths] in wkconfig.json: each Mesh3 model (or every one in a folder and its
// subfolders, in data.bcp or a mod) written as .glb under the user folder's "exported" folder, at
// its own path ("Buildings\Manor\Fully Built\DEFAULT.glb"); then wkbre2 exits. A modder edits
// one and puts it in "saved" at the same path, where it replaces the model (docs/engine/gltf.md).
static void ExportGltfModels(const nlohmann::json& list)
{
	LoadBCP("data.bcp");
	std::vector<std::string> meshes;
	auto isMesh = [](const std::string& f) { return f.size() > 6 && StrCICompare(f.substr(f.size() - 6).c_str(), ".mesh3") == 0; };
	auto walk = [&](const std::string& dir, auto& self) -> void {
		std::unique_ptr<std::vector<std::string>> files(ListFiles(dir.c_str()));
		for (const std::string& f : *files)
			if (isMesh(f))
				meshes.push_back(dir + "\\" + f);
		std::unique_ptr<std::vector<std::string>> dirs(ListDirectories(dir.c_str()));
		for (const std::string& d : *dirs)
			self(dir + "\\" + d, self);
	};
	for (const auto& item : list.is_array() ? list : nlohmann::json::array({ list })) {
		const std::string path = item.get<std::string>();
		if (isMesh(path))
			meshes.push_back(path);
		else
			walk(path, walk);
	}
	int written = 0;
	for (const std::string& path : meshes) {
		Mesh mesh;
		mesh.load(path.c_str());
		std::filesystem::path out = g_userPath / "exported" / PathFromUtf8(path);
		out.replace_extension(".glb");
		std::filesystem::create_directories(out.parent_path());
		if (mesh.saveGltf(PathToUtf8(out).c_str())) {
			printf("Exported %s\n", PathToUtf8(out).c_str());
			written++;
		}
		else
			printf("Could not export %s\n", path.c_str());
	}
	printf("Exported %d of %zu models\n", written, meshes.size());
}

void LaunchQSM() {
	LoadBCP("data.bcp");
	InitWindow();
	ImGuiImpl_Init();
	IRenderer* gfx = CreateRenderer();
	gfx->Init();
	SetRenderer(gfx);
	ImGuiImpl_CreateFontsTexture(gfx);

	// The menus (redata/FrontEndUI.lua), with the game's cursor
	FrontEnd frontEnd{ gfx };
	WndSetCursor(WndCreateCursor("Interface\\C_DEFAULT.TGA"));

	QuickStartMenu qsm{ gfx };

	// Test runs: "menuTest": { "open": [screens], "frames": n, "screenshot": path } opens the
	// screens, saves a screenshot after n frames and quits
	const nlohmann::json menuTest = g_settings.value("menuTest", nlohmann::json());
	int menuFrame = 0;

	while (!g_windowQuit) {
		// "menuTest": { ..., "hold": { "x", "y", "from", "to" } }: the left button held there from
		// frame "from" to frame "to" (window pixels), as a player holding a button
		if (menuTest.is_object() && menuTest.contains("hold")) {
			const nlohmann::json& hold = menuTest["hold"];
			const int from = hold.value("from", 30), to = hold.value("to", 90);
			if (menuFrame + 1 >= from && menuFrame + 1 <= to) {
				g_mouseX = hold.value("x", 0);
				g_mouseY = hold.value("y", 0);
				g_mousePressed[SDL_BUTTON_LEFT] = menuFrame + 1 == from;
				g_mouseDown[SDL_BUTTON_LEFT] = true;
			}
			else if (menuFrame + 1 == to + 1) {
				g_mouseDown[SDL_BUTTON_LEFT] = false;
				g_mouseReleased[SDL_BUTTON_LEFT] = true;
			}
		}
		ImGuiImpl_NewFrame();
		qsm.draw();
		frontEnd.frame(FrontEnd::Place::MAIN_MENU);
		for (const FrontEnd::Request& request : frontEnd.takeRequests())
			qsm.handleRequest(request);

		gfx->ClearFrame();
		gfx->BeginDrawing();
		gfx->InitImGuiDrawing();
		ImGuiImpl_Render(gfx);
		if (menuTest.is_object()) {
			menuFrame++;
			if (menuFrame == 2)
				for (const auto& screen : menuTest.value("open", nlohmann::json::array()))
					frontEnd.open(screen.get<std::string>());
			if (menuFrame == menuTest.value("frames", 60)) {
				gfx->SaveScreenshot(menuTest.value("screenshot", std::string("menu.bmp")).c_str());
				g_windowQuit = true;
			}
		}
		gfx->EndDrawing();
		HandleWindow();

		qsm.launchGame();
	}
}

int main()
//int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR cmdArgs, int showMode)
{
	// Unbuffered so redirected logs keep everything up to a crash
	setvbuf(stdout, nullptr, _IONBF, 0);
#ifdef _WIN32
	SetUnhandledExceptionFilter(PrintCrash);
#endif
	printf("Welcome to wkbre2 ! :)\n\n");

	LoadSettings();
	g_renderSettings.load();
	g_gamePath = PathFromUtf8(g_settings.value<std::string>("gamePath", "."));
	if (g_settings.contains("userPath"))
		g_userPath = PathFromUtf8(g_settings.at("userPath").get<std::string>());
	else if (const char* appData = getenv("APPDATA"))
		g_userPath = PathFromUtf8(appData) / "wkbre2";

#ifdef _WIN32
	if (!std::filesystem::exists(g_gamePath / "data.bcp"))
		if (const auto steam = FindSteamGameFolder(); !steam.empty()) {
			// the first run: use the Steam install (only read from, never written to)
			printf("Game folder found: %s\n", PathToUtf8(steam).c_str());
			g_gamePath = steam;
			g_settings["gamePath"] = PathToUtf8(steam);
			SaveSettings();
		}
#endif
	if (!std::filesystem::exists(g_gamePath / "data.bcp")) {
		std::string message = "Could not find the file \"data.bcp\".\n\nYou need to copy \"data.bcp\" from Warrior Kings (Battles) game to the following path:\n\n";
		message += PathToUtf8(std::filesystem::absolute(g_gamePath));
		message += "\n\nYou can also change the entire path in the configuration file \"wkconfig.json\".";
		printf("%s\n", message.c_str());
#ifdef _WIN32
		MessageBoxA(NULL, message.c_str(), "wkbre2", 16);
#endif
		return -5;
	}

	try {
		if (g_settings.contains("exportGltf"))
			ExportGltfModels(g_settings.at("exportGltf"));
		else if (g_settings.value<bool>("test", false))
			LaunchTest();
		else
			LaunchQSM();
	}
	catch (const std::exception& exc) {
		// Report instead of terminating silently
		ferr("Unhandled exception: %s", exc.what());
	}

	return 0;
}
