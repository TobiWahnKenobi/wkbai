// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#pragma once
#include <cstddef>

#ifndef _WIN32
void _sleep(int duration);
void Sleep(int duration);
void gets_s(char* destination, size_t len);
template<size_t N> void gets_s(char (&array)[N]) { gets_s(array, N); }
void __debugbreak();
char* itoa(int num, char* buffer, int base);
#endif

#ifdef _WIN32
#include <cstdio>
// A program started with no console window: its standard output read from `out`, its input and
// errors NUL. (_popen runs cmd.exe, which a windowed program opens a console window for, one each
// time: test 97 flashed 108 of them on the screen.) PcloseHidden waits for it: its exit code, -1
// if it never started.
struct HiddenChild { std::FILE* out = nullptr; void* process = nullptr; };
HiddenChild PopenHidden(const char* exePath);
int PcloseHidden(HiddenChild& child);
#endif
