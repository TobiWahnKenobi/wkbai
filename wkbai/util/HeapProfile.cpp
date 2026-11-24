// wkbai - a sampling heap profiler for finding what holds the memory in a long game (built only
// with the CMake option WKBRE2_HEAPPROF, off by default).
//
// Every operator new/delete goes through here: each block gets a 16-byte header with its size and
// call site. One allocation in SAMPLE has its call stack taken; that site's live bytes and blocks
// count up, and down again when the block is freed. With WKB_HEAPPROF=<file> set, every
// WKB_HEAPPROF_SECONDS (300) the sites holding the most (x SAMPLE: an estimate of the whole) are
// written to <file>.<minutes>.txt, symbolised. malloc() itself (C libraries, Lua) is not seen.

#ifdef WKBRE2_HEAPPROF
#include <windows.h>
#include <dbghelp.h>
#include <psapi.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <new>
#include <thread>
#include <algorithm>

#pragma comment(lib, "dbghelp.lib")
#pragma comment(lib, "psapi.lib")

void HeapProfileReportBig(FILE* f);
namespace {
void hookAllModules();

constexpr int DEPTH = 14;
constexpr uint32_t SAMPLE = 64;
constexpr uint32_t MAXSITES = 1 << 16;

struct Site {
	void* frames[DEPTH];
	uint32_t hash;
	uint16_t depth;
	int64_t liveBytes, liveBlocks, totalBlocks;
};

struct Header {
	size_t size;
	uint32_t site; // 0: not sampled
	uint32_t magic;
};
static_assert(sizeof(Header) == 16);
constexpr uint32_t MAGIC = 0x48505246;

Site* g_sites = nullptr; // [MAXSITES], index 0 unused
uint32_t g_siteCount = 1;
SRWLOCK g_lock = SRWLOCK_INIT;
std::atomic<uint32_t> g_counter{ 0 };
std::atomic<int64_t> g_liveBytes{ 0 }, g_liveBlocks{ 0 };
thread_local bool t_inside = false;

uint32_t findSite(void** frames, uint16_t depth)
{
	uint32_t h = 2166136261u;
	for (int i = 0; i < depth; i++) {
		h ^= (uint32_t)((uintptr_t)frames[i] >> 2);
		h *= 16777619u;
	}
	if (!g_sites) {
		g_sites = (Site*)VirtualAlloc(nullptr, sizeof(Site) * MAXSITES, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
		if (!g_sites)
			return 0;
	}
	// open addressing over a separate index would be faster; a linear probe on the hash is enough
	// for a few thousand sites... use a hash table of indices
	static uint32_t* table = nullptr;
	constexpr uint32_t TABLE = MAXSITES * 2;
	if (!table)
		table = (uint32_t*)VirtualAlloc(nullptr, sizeof(uint32_t) * TABLE, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	for (uint32_t i = h & (TABLE - 1);; i = (i + 1) & (TABLE - 1)) {
		const uint32_t s = table[i];
		if (!s) {
			if (g_siteCount >= MAXSITES)
				return 0;
			const uint32_t n = g_siteCount++;
			Site& site = g_sites[n];
			memcpy(site.frames, frames, sizeof(void*) * depth);
			site.depth = depth;
			site.hash = h;
			table[i] = n;
			return n;
		}
		Site& site = g_sites[s];
		if (site.hash == h && site.depth == depth && !memcmp(site.frames, frames, sizeof(void*) * depth))
			return s;
	}
}

void* allocate(size_t size)
{
	Header* h = (Header*)malloc(size + sizeof(Header));
	if (!h)
		return nullptr;
	h->size = size;
	h->site = 0;
	h->magic = MAGIC;
	g_liveBytes += (int64_t)size;
	g_liveBlocks++;
	if (g_counter.fetch_add(1, std::memory_order_relaxed) % SAMPLE == 0 && !t_inside) {
		t_inside = true;
		void* frames[DEPTH + 2];
		const uint16_t depth = (uint16_t)CaptureStackBackTrace(2, DEPTH, frames, nullptr);
		AcquireSRWLockExclusive(&g_lock);
		const uint32_t s = findSite(frames, depth);
		if (s) {
			g_sites[s].liveBytes += (int64_t)size;
			g_sites[s].liveBlocks++;
			g_sites[s].totalBlocks++;
		}
		ReleaseSRWLockExclusive(&g_lock);
		h->site = s;
		t_inside = false;
	}
	return h + 1;
}

void release(void* p)
{
	if (!p)
		return;
	Header* h = (Header*)p - 1;
	if (h->magic != MAGIC) { // not ours (cannot happen unless a block from elsewhere is deleted)
		free(p);
		return;
	}
	h->magic = 0;
	g_liveBytes -= (int64_t)h->size;
	g_liveBlocks--;
	if (h->site) {
		AcquireSRWLockExclusive(&g_lock);
		g_sites[h->site].liveBytes -= (int64_t)h->size;
		g_sites[h->site].liveBlocks--;
		ReleaseSRWLockExclusive(&g_lock);
	}
	free(h);
}

void report(const char* base, int minutes)
{
	t_inside = true;
	static bool symInit = false;
	HANDLE proc = GetCurrentProcess();
	if (!symInit) {
		SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);
		SymInitialize(proc, nullptr, TRUE);
		symInit = true;
	}
	// a copy of the sites (VirtualAlloc: not through new)
	AcquireSRWLockShared(&g_lock);
	const uint32_t count = g_siteCount;
	Site* copy = (Site*)VirtualAlloc(nullptr, sizeof(Site) * count, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	memcpy(copy, g_sites, sizeof(Site) * count);
	ReleaseSRWLockShared(&g_lock);
	uint32_t* order = (uint32_t*)VirtualAlloc(nullptr, sizeof(uint32_t) * count, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	uint32_t n = 0;
	for (uint32_t i = 1; i < count; i++)
		if (copy[i].liveBytes > 0)
			order[n++] = i;
	std::sort(order, order + n, [&](uint32_t a, uint32_t b) { return copy[a].liveBytes > copy[b].liveBytes; });

	char path[MAX_PATH];
	snprintf(path, sizeof(path), "%s.%03d.txt", base, minutes);
	FILE* f = fopen(path, "w");
	if (f) {
		PROCESS_MEMORY_COUNTERS_EX pmc{};
		pmc.cb = sizeof(pmc);
		typedef BOOL(WINAPI * PGPMI)(HANDLE, PPROCESS_MEMORY_COUNTERS, DWORD);
		static PGPMI gpmi = (PGPMI)GetProcAddress(GetModuleHandleA("kernel32.dll"), "K32GetProcessMemoryInfo");
		if (gpmi)
			gpmi(proc, (PPROCESS_MEMORY_COUNTERS)&pmc, sizeof(pmc));
		fprintf(f, "minute %d: operator new live %.1f MB in %lld blocks; process private %.1f MB, working set %.1f MB; %u sites\n",
			minutes, g_liveBytes / 1048576.0, (long long)g_liveBlocks.load(), pmc.PrivateUsage / 1048576.0, pmc.WorkingSetSize / 1048576.0, count);
		// the heaps (malloc's is the process heap), then the private memory by allocation
		HANDLE heaps[64];
		const DWORD nh = GetProcessHeaps(64, heaps);
		for (DWORD i = 0; i < nh && i < 64; i++) {
			HEAP_SUMMARY hs{};
			hs.cb = sizeof(hs);
			if (HeapSummary(heaps[i], 0, &hs))
				fprintf(f, "heap %p%s: allocated %.1f MB, committed %.1f MB\n", heaps[i], heaps[i] == GetProcessHeap() ? " (process)" : "",
					hs.cbAllocated / 1048576.0, hs.cbCommitted / 1048576.0);
		}
		struct Region { void* base; size_t committed; DWORD protect; };
		static Region regions[4096];
		int nr = 0;
		size_t privTotal = 0;
		MEMORY_BASIC_INFORMATION mbi;
		for (char* a = nullptr; VirtualQuery(a, &mbi, sizeof(mbi)) == sizeof(mbi); a = (char*)mbi.BaseAddress + mbi.RegionSize) {
			if (mbi.State == MEM_COMMIT && mbi.Type == MEM_PRIVATE) {
				privTotal += mbi.RegionSize;
				if (nr && regions[nr - 1].base == mbi.AllocationBase)
					regions[nr - 1].committed += mbi.RegionSize;
				else if (nr < 4096)
					regions[nr++] = { mbi.AllocationBase, mbi.RegionSize, mbi.Protect };
			}
			if ((char*)mbi.BaseAddress + mbi.RegionSize < a)
				break;
		}
		std::sort(regions, regions + nr, [](const Region& x, const Region& y) { return x.committed > y.committed; });
		fprintf(f, "private committed regions: %.1f MB in %d allocations; the largest:\n", privTotal / 1048576.0, nr);
		for (int i = 0; i < nr && i < 25; i++)
			fprintf(f, "  %p  %.1f MB  protect %lx\n", regions[i].base, regions[i].committed / 1048576.0, regions[i].protect);
		size_t smallBytes = 0; int smallCount = 0;
		for (int i = 0; i < nr; i++)
			if (regions[i].committed <= (1 << 20)) { smallBytes += regions[i].committed; smallCount++; }
		fprintf(f, "  (allocations of 1 MB or less: %d, %.1f MB)\n", smallCount, smallBytes / 1048576.0);
		alignas(SYMBOL_INFO) char symbuf[sizeof(SYMBOL_INFO) + 512];
		for (uint32_t k = 0; k < n && k < 60; k++) {
			const Site& s = copy[order[k]];
			fprintf(f, "\n#%u  ~%.1f MB live in ~%lld blocks (~%lld made)\n", k + 1, s.liveBytes * (double)SAMPLE / 1048576.0,
				(long long)s.liveBlocks * SAMPLE, (long long)s.totalBlocks * SAMPLE);
			for (int i = 0; i < s.depth; i++) {
				SYMBOL_INFO* sym = (SYMBOL_INFO*)symbuf;
				memset(sym, 0, sizeof(SYMBOL_INFO));
				sym->SizeOfStruct = sizeof(SYMBOL_INFO);
				sym->MaxNameLen = 511;
				DWORD64 disp = 0;
				IMAGEHLP_LINE64 line{};
				line.SizeOfStruct = sizeof(line);
				DWORD ldisp = 0;
				const DWORD64 addr = (DWORD64)s.frames[i];
				const char* name = SymFromAddr(proc, addr, &disp, sym) ? sym->Name : "?";
				if (SymGetLineFromAddr64(proc, addr, &ldisp, &line)) {
					const char* file = strrchr(line.FileName, '\\');
					fprintf(f, "    %s  (%s:%lu)\n", name, file ? file + 1 : line.FileName, line.LineNumber);
				}
				else
					fprintf(f, "    %s\n", name);
			}
		}
		HeapProfileReportBig(f);
		fclose(f);
	}
	hookAllModules(); // (modules loaded since)
	VirtualFree(copy, 0, MEM_RELEASE);
	VirtualFree(order, 0, MEM_RELEASE);
	t_inside = false;
}

struct Reporter {
	Reporter()
	{
		const char* base = getenv("WKB_HEAPPROF");
		if (!base || !*base)
			return;
		static char path[MAX_PATH];
		strncpy(path, base, MAX_PATH - 1);
		const char* sec = getenv("WKB_HEAPPROF_SECONDS");
		const int seconds = sec ? atoi(sec) : 300;
		CreateThread(nullptr, 0, [](LPVOID param) -> DWORD {
			const int seconds = (int)(intptr_t)param;
			Sleep(15000);
			hookAllModules();
			for (int t = 1;; t++) {
				Sleep(t == 1 ? seconds * 1000 - 15000 : seconds * 1000);
				report(path, t * seconds / 60);
			}
		}, (LPVOID)(intptr_t)seconds, 0, nullptr);
	}
} g_reporter;

// malloc/calloc/realloc/free of every module (their import tables pointed here): the live blocks
// of 4 MB or more with their call stacks
typedef void* (__cdecl* MallocFn)(size_t);
typedef void* (__cdecl* CallocFn)(size_t, size_t);
typedef void* (__cdecl* ReallocFn)(void*, size_t);
typedef void(__cdecl* FreeFn)(void*);
MallocFn realMalloc;
CallocFn realCalloc;
ReallocFn realRealloc;
FreeFn realFree;
constexpr size_t BIG = 4 << 20;
constexpr uint32_t BIGTABLE = 1 << 20;
struct Big { void* ptr; size_t size; uint16_t depth; uint16_t weight; void* frames[DEPTH]; };
std::atomic<uint32_t> g_hookCounter{ 0 };
Big* g_big = nullptr; // [BIGTABLE], open addressing by ptr; ptr (void*)1: removed
std::atomic<int> g_bigCount{ 0 };

uint32_t bigSlot(void* p) { return (uint32_t)(((uintptr_t)p >> 16) * 2654435761u) & (BIGTABLE - 1); }
void bigAdd(void* p, size_t size)
{
	if (!p || !g_big)
		return;
	// the big ones all, one in SAMPLE of the others
	uint16_t weight = 1;
	if (size < BIG) {
		if (g_hookCounter.fetch_add(1, std::memory_order_relaxed) % SAMPLE)
			return;
		weight = SAMPLE;
	}
	void* frames[DEPTH];
	const uint16_t depth = (uint16_t)CaptureStackBackTrace(2, DEPTH, frames, nullptr);
	AcquireSRWLockExclusive(&g_lock);
	for (uint32_t i = bigSlot(p), k = 0; k < BIGTABLE; i = (i + 1) & (BIGTABLE - 1), k++)
		if (!g_big[i].ptr || g_big[i].ptr == (void*)1) {
			g_big[i].ptr = p;
			g_big[i].size = size;
			g_big[i].depth = depth;
			g_big[i].weight = weight;
			memcpy(g_big[i].frames, frames, sizeof(void*) * depth);
			g_bigCount++;
			break;
		}
	ReleaseSRWLockExclusive(&g_lock);
}
void bigRemove(void* p)
{
	if (!p || !g_big || !g_bigCount.load(std::memory_order_relaxed))
		return;
	AcquireSRWLockExclusive(&g_lock);
	for (uint32_t i = bigSlot(p), k = 0; k < BIGTABLE && g_big[i].ptr; i = (i + 1) & (BIGTABLE - 1), k++)
		if (g_big[i].ptr == p) {
			g_big[i].ptr = (void*)1;
			g_bigCount--;
			break;
		}
	ReleaseSRWLockExclusive(&g_lock);
}
void* __cdecl hookMalloc(size_t n) { void* p = realMalloc(n); bigAdd(p, n); return p; }
void* __cdecl hookCalloc(size_t c, size_t n) { void* p = realCalloc(c, n); bigAdd(p, c * n); return p; }
void* __cdecl hookRealloc(void* o, size_t n)
{
	void* p = realRealloc(o, n);
	if (p || !n) {
		bigRemove(o);
		if (n)
			bigAdd(p, n);
	}
	return p;
}
void __cdecl hookFree(void* p) { bigRemove(p); realFree(p); }
typedef void* (__cdecl* AlignedMallocFn)(size_t, size_t);
typedef void(__cdecl* AlignedFreeFn)(void*);
typedef void* (__cdecl* AlignedReallocFn)(void*, size_t, size_t);
typedef LPVOID(WINAPI* HeapAllocFn)(HANDLE, DWORD, SIZE_T);
typedef LPVOID(WINAPI* HeapReAllocFn)(HANDLE, DWORD, LPVOID, SIZE_T);
typedef BOOL(WINAPI* HeapFreeFn)(HANDLE, DWORD, LPVOID);
AlignedMallocFn realAlignedMalloc;
AlignedFreeFn realAlignedFree;
AlignedReallocFn realAlignedRealloc;
HeapAllocFn realHeapAlloc;
HeapReAllocFn realHeapReAlloc;
HeapFreeFn realHeapFree;
void* __cdecl hookAlignedMalloc(size_t n, size_t a) { void* p = realAlignedMalloc(n, a); bigAdd(p, n); return p; }
void __cdecl hookAlignedFree(void* p) { bigRemove(p); realAlignedFree(p); }
void* __cdecl hookAlignedRealloc(void* o, size_t n, size_t a)
{
	void* p = realAlignedRealloc(o, n, a);
	if (p || !n) { bigRemove(o); if (n) bigAdd(p, n); }
	return p;
}
LPVOID WINAPI hookHeapAlloc(HANDLE h, DWORD fl, SIZE_T n) { void* p = realHeapAlloc(h, fl, n); bigAdd(p, n); return p; }
LPVOID WINAPI hookHeapReAlloc(HANDLE h, DWORD fl, LPVOID o, SIZE_T n)
{
	void* p = realHeapReAlloc(h, fl, o, n);
	if (p) { bigRemove(o); if (n) bigAdd(p, n); }
	return p;
}
BOOL WINAPI hookHeapFree(HANDLE h, DWORD fl, LPVOID p) { bigRemove(p); return realHeapFree(h, fl, p); }

void patchImports(HMODULE mod)
{
	auto* base = (BYTE*)mod;
	auto* dos = (IMAGE_DOS_HEADER*)base;
	if (dos->e_magic != IMAGE_DOS_SIGNATURE)
		return;
	auto* nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
	const IMAGE_DATA_DIRECTORY& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
	if (!dir.VirtualAddress)
		return;
	for (auto* imp = (IMAGE_IMPORT_DESCRIPTOR*)(base + dir.VirtualAddress); imp->Name; imp++) {
		const char* dll = (const char*)(base + imp->Name);
		const bool crt = !_strnicmp(dll, "api-ms-win-crt-heap", 19) || !_stricmp(dll, "ucrtbase.dll") || !_strnicmp(dll, "msvcrt", 6);
		// HeapAlloc called directly (not the C runtime's own, ucrtbase is not patched)
		const bool k32 = !_stricmp(dll, "kernel32.dll") || !_strnicmp(dll, "api-ms-win-core-heap", 20);
		if (!crt && !k32)
			continue;
		if (!imp->OriginalFirstThunk)
			continue;
		auto* names = (IMAGE_THUNK_DATA*)(base + imp->OriginalFirstThunk);
		auto* addrs = (IMAGE_THUNK_DATA*)(base + imp->FirstThunk);
		for (; names->u1.AddressOfData; names++, addrs++) {
			if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal))
				continue;
			const char* fn = ((IMAGE_IMPORT_BY_NAME*)(base + names->u1.AddressOfData))->Name;
			void* to = nullptr;
			if (crt)
				to = !strcmp(fn, "malloc") ? (void*)hookMalloc : !strcmp(fn, "calloc") ? (void*)hookCalloc
					: !strcmp(fn, "realloc") ? (void*)hookRealloc : !strcmp(fn, "free") ? (void*)hookFree
					: !strcmp(fn, "_aligned_malloc") ? (void*)hookAlignedMalloc : !strcmp(fn, "_aligned_free") ? (void*)hookAlignedFree
					: !strcmp(fn, "_aligned_realloc") ? (void*)hookAlignedRealloc : nullptr;
			else
				to = !strcmp(fn, "HeapAlloc") ? (void*)hookHeapAlloc : !strcmp(fn, "HeapReAlloc") ? (void*)hookHeapReAlloc
					: !strcmp(fn, "HeapFree") ? (void*)hookHeapFree : nullptr;
			if (!to || (void*)addrs->u1.Function == to)
				continue;
			DWORD old;
			VirtualProtect(&addrs->u1.Function, sizeof(void*), PAGE_READWRITE, &old);
			addrs->u1.Function = (ULONGLONG)to;
			VirtualProtect(&addrs->u1.Function, sizeof(void*), old, &old);
		}
	}
}

void hookAllModules()
{
	if (!g_big) {
		HMODULE ucrt = GetModuleHandleA("ucrtbase.dll");
		realMalloc = (MallocFn)GetProcAddress(ucrt, "malloc");
		realCalloc = (CallocFn)GetProcAddress(ucrt, "calloc");
		realRealloc = (ReallocFn)GetProcAddress(ucrt, "realloc");
		realFree = (FreeFn)GetProcAddress(ucrt, "free");
		realAlignedMalloc = (AlignedMallocFn)GetProcAddress(ucrt, "_aligned_malloc");
		realAlignedFree = (AlignedFreeFn)GetProcAddress(ucrt, "_aligned_free");
		realAlignedRealloc = (AlignedReallocFn)GetProcAddress(ucrt, "_aligned_realloc");
		HMODULE k = GetModuleHandleA("kernel32.dll");
		realHeapAlloc = (HeapAllocFn)GetProcAddress(k, "HeapAlloc");
		realHeapReAlloc = (HeapReAllocFn)GetProcAddress(k, "HeapReAlloc");
		realHeapFree = (HeapFreeFn)GetProcAddress(k, "HeapFree");
		g_big = (Big*)VirtualAlloc(nullptr, sizeof(Big) * BIGTABLE, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	}
	HMODULE mods[512];
	DWORD needed = 0;
	if (K32EnumProcessModules(GetCurrentProcess(), mods, sizeof(mods), &needed))
		for (DWORD i = 0; i < needed / sizeof(HMODULE) && i < 512; i++)
			if (mods[i] != GetModuleHandleA("ucrtbase.dll") && mods[i] != GetModuleHandleA("kernel32.dll")
				&& mods[i] != GetModuleHandleA("kernelbase.dll") && mods[i] != GetModuleHandleA("ntdll.dll"))
				patchImports(mods[i]);
}

} // namespace

// the big malloc blocks, grouped by call stack (called from report)
void HeapProfileReportBig(FILE* f)
{
	if (!g_big)
		return;
	HANDLE proc = GetCurrentProcess();
	AcquireSRWLockShared(&g_lock);
	Big* copy = (Big*)VirtualAlloc(nullptr, sizeof(Big) * BIGTABLE, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	memcpy(copy, g_big, sizeof(Big) * BIGTABLE);
	ReleaseSRWLockShared(&g_lock);
	int n = 0;
	for (uint32_t i = 0; i < BIGTABLE; i++)
		if (copy[i].ptr && copy[i].ptr != (void*)1)
			copy[n++] = copy[i];
	std::sort(copy, copy + n, [](const Big& a, const Big& b) { return memcmp(a.frames, b.frames, sizeof(a.frames)) < 0; });
	fprintf(f, "\nmalloc/HeapAlloc/_aligned_malloc blocks (all of 4 MB or more, one in %u of the others), live: %d entries\n", SAMPLE, n);
	static int groups[BIGTABLE];
	int ng = 0;
	alignas(SYMBOL_INFO) char symbuf[sizeof(SYMBOL_INFO) + 512];
	for (int i = 0; i < n;) {
		int j = i;
		size_t total = 0;
		int blocks = 0;
		while (j < n && copy[j].depth == copy[i].depth && !memcmp(copy[j].frames, copy[i].frames, sizeof(void*) * copy[i].depth)) {
			total += copy[j].size * copy[j].weight;
			blocks += copy[j++].weight;
		}
		copy[i].size = total; // (for the order below)
		copy[i].weight = (uint16_t)(blocks < 65535 ? blocks : 65535);
		groups[ng++] = i;
		i = j;
	}
	std::sort(groups, groups + ng, [&](int a, int b) { return copy[a].size > copy[b].size; });
	size_t all = 0;
	for (int g = 0; g < ng; g++)
		all += copy[groups[g]].size;
	fprintf(f, "  ~%.1f MB in %d call stacks\n", all / 1048576.0, ng);
	for (int g = 0; g < ng && g < 40; g++) {
		const int i = groups[g];
		fprintf(f, "\n~%d blocks, ~%.1f MB\n", (int)copy[i].weight, copy[i].size / 1048576.0);
		for (int k = 0; k < copy[i].depth; k++) {
			SYMBOL_INFO* sym = (SYMBOL_INFO*)symbuf;
			memset(sym, 0, sizeof(SYMBOL_INFO));
			sym->SizeOfStruct = sizeof(SYMBOL_INFO);
			sym->MaxNameLen = 511;
			DWORD64 disp = 0;
			IMAGEHLP_LINE64 line{};
			line.SizeOfStruct = sizeof(line);
			DWORD ldisp = 0;
			const DWORD64 addr = (DWORD64)copy[i].frames[k];
			char modname[MAX_PATH] = "";
			HMODULE m = nullptr;
			if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)addr, &m))
				GetModuleBaseNameA(proc, m, modname, MAX_PATH);
			const char* name = SymFromAddr(proc, addr, &disp, sym) ? sym->Name : "?";
			if (SymGetLineFromAddr64(proc, addr, &ldisp, &line)) {
				const char* file = strrchr(line.FileName, 92);
				fprintf(f, "    %s!%s  (%s:%lu)\n", modname, name, file ? file + 1 : line.FileName, line.LineNumber);
			}
			else
				fprintf(f, "    %s!%s +%llx\n", modname, name, (unsigned long long)disp);
		}
	}
	VirtualFree(copy, 0, MEM_RELEASE);
}

namespace {
struct HookAtStart { HookAtStart() { if (getenv("WKB_HEAPPROF")) hookAllModules(); } } g_hookAtStart;
}

void* operator new(size_t size) { if (void* p = allocate(size ? size : 1)) return p; throw std::bad_alloc(); }
void* operator new[](size_t size) { if (void* p = allocate(size ? size : 1)) return p; throw std::bad_alloc(); }
void* operator new(size_t size, const std::nothrow_t&) noexcept { return allocate(size ? size : 1); }
void* operator new[](size_t size, const std::nothrow_t&) noexcept { return allocate(size ? size : 1); }
void operator delete(void* p) noexcept { release(p); }
void operator delete[](void* p) noexcept { release(p); }
void operator delete(void* p, size_t) noexcept { release(p); }
void operator delete[](void* p, size_t) noexcept { release(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { release(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { release(p); }
#endif
