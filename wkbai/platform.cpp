// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#include <cstdio>
#include <charconv>
#include <chrono>
#include <thread>
#include <utility>

#ifndef _WIN32 // temp
void _sleep(int duration) {std::this_thread::sleep_for(std::chrono::milliseconds(duration));}
void Sleep(int duration) {_sleep(duration);}
void gets_s(char* destination, size_t len) {fgets(destination, len, stdin);}
void __debugbreak() {}
char* itoa(int num, char* buffer, int base) {std::to_chars(buffer, buffer+10, num, base);}
#endif

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#include <string>
#include "platform.h"

HiddenChild PopenHidden(const char* exePath)
{
	HiddenChild child;
	SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
	HANDLE readEnd = nullptr, writeEnd = nullptr;
	if (!CreatePipe(&readEnd, &writeEnd, &sa, 0))
		return child;
	SetHandleInformation(readEnd, HANDLE_FLAG_INHERIT, 0); // only the child's end is inherited
	HANDLE nul = CreateFileA("NUL", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
	STARTUPINFOA si{};
	si.cb = sizeof(si);
	si.dwFlags = STARTF_USESTDHANDLES;
	si.hStdInput = nul;
	si.hStdOutput = writeEnd;
	si.hStdError = nul;
	PROCESS_INFORMATION pi{};
	std::string commandLine = std::string("\"") + exePath + "\"";
	// the environment (WKB_TEST and the like, set with _putenv_s) inherited
	const BOOL started = CreateProcessA(exePath, commandLine.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
	CloseHandle(writeEnd); // else the read never ends
	if (nul != INVALID_HANDLE_VALUE)
		CloseHandle(nul);
	if (!started) {
		CloseHandle(readEnd);
		return child;
	}
	CloseHandle(pi.hThread);
	child.out = _fdopen(_open_osfhandle((intptr_t)readEnd, _O_RDONLY | _O_TEXT), "r");
	child.process = pi.hProcess;
	return child;
}

int PcloseHidden(HiddenChild& child)
{
	if (child.out)
		fclose(child.out); // closes the pipe's handle too
	DWORD code = (DWORD)-1;
	if (child.process) {
		WaitForSingleObject((HANDLE)child.process, INFINITE);
		GetExitCodeProcess((HANDLE)child.process, &code);
		CloseHandle((HANDLE)child.process);
	}
	child = {};
	return (int)code;
}
#endif
