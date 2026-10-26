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
#include <nlohmann/json.hpp>

#include "interface/QuickStartMenu.h"
#include "gfx/renderer.h"
#include "window.h"
#include "imguiimpl.h"
#include "gfx/bitmap.h"
#include "gfx/RenderSettings.h"

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
	return EXCEPTION_CONTINUE_SEARCH;
}
#endif

void LaunchTest();

void LaunchQSM() {
	LoadBCP("data.bcp");
	InitWindow();
	ImGuiImpl_Init();
	IRenderer* gfx = CreateRenderer();
	gfx->Init();
	SetRenderer(gfx);
	ImGuiImpl_CreateFontsTexture(gfx);

	texture bgtex = gfx->CreateTexture(Bitmap::loadBitmap("Interface\\Startup Screen.tga"), 1);

	QuickStartMenu qsm{ gfx };

	while (!g_windowQuit) {
		ImGuiImpl_NewFrame();
		qsm.draw();

		gfx->ClearFrame();
		gfx->BeginDrawing();
		gfx->InitRectDrawing();
		gfx->SetTexture(0, bgtex);
		gfx->DrawRect(0, 0, g_windowWidth, g_windowHeight);
		gfx->InitImGuiDrawing();
		ImGuiImpl_Render(gfx);
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
		if (g_settings.value<bool>("test", false))
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
