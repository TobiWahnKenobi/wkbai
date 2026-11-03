// GPU path finding and visibility spike for wkbai (docs/engine/gpu-pathfinding-visibility.md in
// the RE repo). Replays a workload the game dumped (WKB_PATHDUMP: grid_<n>.bin, queries.txt,
// viewers.txt) on the CPU the way wkbai does it, as CPU flow fields, and as D3D11 compute
// shaders, timing each with the upload and the readback the simulation would need.
//
//   cl /O2 /EHsc /std:c++20 pathvis_bench.cpp /I ../../wkbai d3d11.lib dxgi.lib d3dcompiler.lib
//   pathvis_bench <dump folder> [adapter name part]

#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#include "Pathfinding.h"

using Microsoft::WRL::ComPtr;
using Clock = std::chrono::high_resolution_clock;
static double msSince(Clock::time_point t) { return std::chrono::duration<double, std::milli>(Clock::now() - t).count(); }

struct Grid { int w = 0, h = 0; std::vector<uint8_t> blocked; std::string name; };
struct Query { float t; int grid, sx, sz, ex, ez; double ms; int tiles, cut; size_t len; };
struct Viewer { uint32_t id, player; int building; float x, y, z, sight; int receives; };

static std::map<int, Grid> grids;
static std::vector<Query> queries;
static std::vector<Viewer> viewers;

static void load(const std::string& dir) {
	std::ifstream q(dir + "/queries.txt");
	std::string line;
	while (std::getline(q, line)) {
		std::istringstream s(line);
		std::string kind;
		s >> kind;
		if (kind == "grid") {
			int n; s >> n;
			Grid& g = grids[n];
			std::getline(s, g.name);
			FILE* f = fopen((dir + "/grid_" + std::to_string(n) + ".bin").c_str(), "rb");
			fread(&g.w, 4, 1, f); fread(&g.h, 4, 1, f);
			g.blocked.resize((size_t)g.w * g.h);
			fread(g.blocked.data(), 1, g.blocked.size(), f);
			fclose(f);
		}
		else if (kind == "q") {
			Query r{};
			s >> r.t >> r.grid >> r.sx >> r.sz >> r.ex >> r.ez >> r.ms >> r.tiles >> r.cut >> r.len;
			queries.push_back(r);
		}
	}
	std::ifstream v(dir + "/viewers.txt");
	while (std::getline(v, line)) {
		Viewer r{};
		std::istringstream s(line);
		s >> r.id >> r.player >> r.building >> r.x >> r.y >> r.z >> r.sight >> r.receives;
		viewers.push_back(r);
	}
}

// ---- CPU flow field: Dijkstra from the goal over the whole grid, the A*'s moves and costs
// (8 neighbours, 100 / 141, a blocked tile not entered, corners may be cut as in wkbai) ----
// Dial's algorithm: the costs are whole numbers up to 141, so a ring of 142 buckets replaces
// the heap (each bucket the tiles at one distance)
static void cpuField(const Grid& g, int gx, int gz, std::vector<uint32_t>& dist) {
	const int w = g.w, h = g.h;
	dist.assign((size_t)w * h, 0xFFFFFFFFu);
	static std::vector<int> ring[142];
	for (auto& b : ring) b.clear();
	const int gi = gz * w + gx;
	dist[gi] = 0;
	ring[0].push_back(gi);
	size_t pending = 1;
	static const int dx[8] = { 1, -1, 0, 0, 1, -1, 1, -1 }, dz[8] = { 0, 0, 1, -1, 1, 1, -1, -1 };
	static const uint32_t c[8] = { 100, 100, 100, 100, 141, 141, 141, 141 };
	for (uint32_t d = 0; pending; d++) {
		auto& b = ring[d % 142];
		for (size_t k0 = 0; k0 < b.size(); k0++) {
			const int i = b[k0];
			pending--;
			if (dist[i] != d) continue;
			const int x = i % w, z = i / w;
			for (int k = 0; k < 8; k++) {
				const int nx = x + dx[k], nz = z + dz[k];
				if (nx < 0 || nz < 0 || nx >= w || nz >= h) continue;
				const int ni = nz * w + nx;
				if (g.blocked[ni]) continue;
				const uint32_t nd = d + c[k];
				if (nd < dist[ni]) { dist[ni] = nd; ring[nd % 142].push_back(ni); pending++; }
			}
		}
		b.clear();
	}
}

// the same with a binary heap (the textbook version)
static void cpuFieldHeap(const Grid& g, int gx, int gz, std::vector<uint32_t>& dist) {
	const int w = g.w, h = g.h;
	dist.assign((size_t)w * h, 0xFFFFFFFFu);
	using E = std::pair<uint32_t, int>;
	static std::vector<E> heap;
	heap.clear();
	auto cmp = [](const E& a, const E& b) { return a.first > b.first; };
	const int gi = gz * w + gx;
	dist[gi] = 0;
	heap.push_back({ 0, gi });
	static const int dx[8] = { 1, -1, 0, 0, 1, -1, 1, -1 }, dz[8] = { 0, 0, 1, -1, 1, 1, -1, -1 };
	static const uint32_t c[8] = { 100, 100, 100, 100, 141, 141, 141, 141 };
	while (!heap.empty()) {
		std::pop_heap(heap.begin(), heap.end(), cmp);
		auto [d, i] = heap.back();
		heap.pop_back();
		if (d != dist[i]) continue;
		const int x = i % w, z = i / w;
		for (int k = 0; k < 8; k++) {
			const int nx = x + dx[k], nz = z + dz[k];
			if (nx < 0 || nz < 0 || nx >= w || nz >= h) continue;
			const int ni = nz * w + nx;
			if (g.blocked[ni]) continue;
			const uint32_t nd = d + c[k];
			if (nd < dist[ni]) { dist[ni] = nd; heap.push_back({ nd, ni }); std::push_heap(heap.begin(), heap.end(), cmp); }
		}
	}
}

// the path from a tile down a distance field (to the goal); empty when unreachable
static size_t walkField(const Grid& g, const uint32_t* dist, int sx, int sz) {
	static const int dx[8] = { 1, -1, 0, 0, 1, -1, 1, -1 }, dz[8] = { 0, 0, 1, -1, 1, 1, -1, -1 };
	int i = sz * g.w + sx;
	if (dist[i] == 0xFFFFFFFFu) return 0;
	size_t n = 1;
	while (dist[i] != 0) {
		const int x = i % g.w, z = i / g.w;
		int best = -1; uint32_t bd = dist[i];
		for (int k = 0; k < 8; k++) {
			const int nx = x + dx[k], nz = z + dz[k];
			if (nx < 0 || nz < 0 || nx >= g.w || nz >= g.h) continue;
			const int ni = nz * g.w + nx;
			if (dist[ni] < bd) { bd = dist[ni]; best = ni; }
		}
		if (best < 0) break;
		i = best; n++;
	}
	return n;
}

// ---- D3D11 ----
static ComPtr<ID3D11Device> dev;
static ComPtr<ID3D11DeviceContext> ctx;
static std::string adapterName;

static bool initD3D(const char* want) {
	ComPtr<IDXGIFactory6> f;
	if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&f)))) return false;
	ComPtr<IDXGIAdapter1> pick;
	for (UINT i = 0;; i++) {
		ComPtr<IDXGIAdapter1> a;
		if (f->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&a)) == DXGI_ERROR_NOT_FOUND) break;
		DXGI_ADAPTER_DESC1 d; a->GetDesc1(&d);
		char name[128]; WideCharToMultiByte(CP_UTF8, 0, d.Description, -1, name, 128, 0, 0);
		printf("adapter %u: %s (%llu MB)%s\n", i, name, (unsigned long long)(d.DedicatedVideoMemory >> 20), (d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) ? " software" : "");
		if (!pick && !(d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) && (!want || strstr(name, want))) { pick = a; adapterName = name; }
	}
	if (!pick) return false;
	D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0;
	return SUCCEEDED(D3D11CreateDevice(pick.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, &fl, 1, D3D11_SDK_VERSION, &dev, nullptr, &ctx));
}

static ComPtr<ID3D11ComputeShader> compile(const char* src, const char* entry) {
	ComPtr<ID3DBlob> code, err;
	if (FAILED(D3DCompile(src, strlen(src), nullptr, nullptr, nullptr, entry, "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &err))) {
		printf("%s\n", err ? (const char*)err->GetBufferPointer() : "compile failed");
		exit(1);
	}
	ComPtr<ID3D11ComputeShader> cs;
	dev->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &cs);
	return cs;
}

struct GpuBuf { ComPtr<ID3D11Buffer> buf, staging; ComPtr<ID3D11UnorderedAccessView> uav; ComPtr<ID3D11ShaderResourceView> srv; UINT bytes = 0; };
static GpuBuf makeBuf(UINT bytes, bool staging, const void* init = nullptr) {
	GpuBuf b; b.bytes = bytes;
	D3D11_BUFFER_DESC d{ bytes, D3D11_USAGE_DEFAULT, D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE, 0, D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS, 0 };
	D3D11_SUBRESOURCE_DATA sd{ init, 0, 0 };
	dev->CreateBuffer(&d, init ? &sd : nullptr, &b.buf);
	D3D11_UNORDERED_ACCESS_VIEW_DESC u{}; u.Format = DXGI_FORMAT_R32_TYPELESS; u.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
	u.Buffer.NumElements = bytes / 4; u.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
	dev->CreateUnorderedAccessView(b.buf.Get(), &u, &b.uav);
	D3D11_SHADER_RESOURCE_VIEW_DESC s{}; s.Format = DXGI_FORMAT_R32_TYPELESS; s.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
	s.BufferEx.NumElements = bytes / 4; s.BufferEx.Flags = D3D11_BUFFEREX_SRV_FLAG_RAW;
	dev->CreateShaderResourceView(b.buf.Get(), &s, &b.srv);
	if (staging) {
		D3D11_BUFFER_DESC sdsc{ bytes, D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ, 0, 0 };
		dev->CreateBuffer(&sdsc, nullptr, &b.staging);
	}
	return b;
}
static void readback(GpuBuf& b, void* out) {
	ctx->CopyResource(b.staging.Get(), b.buf.Get());
	D3D11_MAPPED_SUBRESOURCE m;
	ctx->Map(b.staging.Get(), 0, D3D11_MAP_READ, 0, &m);
	memcpy(out, m.pData, b.bytes);
	ctx->Unmap(b.staging.Get(), 0);
}
static void gpuSync() { // waits for the GPU (an event query)
	ComPtr<ID3D11Query> q; D3D11_QUERY_DESC d{ D3D11_QUERY_EVENT, 0 };
	dev->CreateQuery(&d, &q); ctx->End(q.Get());
	BOOL done = FALSE; while (ctx->GetData(q.Get(), &done, sizeof done, 0) != S_OK || !done) {}
}

// Wavefront relaxation of G distance fields at once (dispatch z = field). Each 16x16 group
// loads its tiles and a 1-tile halo into shared memory and relaxes them there ITER times
// (Gauss-Seidel in shared memory: a wave crosses the group in one dispatch), then writes back
// what got shorter and counts the changes; the host dispatches until nothing changes.
static const char* FIELD_HLSL = R"(
cbuffer P : register(b0) { uint W, H, G, pad; };
ByteAddressBuffer blocked : register(t0);   // 1 byte per tile (packed in uints)
RWByteAddressBuffer dist : register(u0);    // G * W * H uints
RWByteAddressBuffer changed : register(u1); // one uint
#define T 16
#define ITER 24
groupshared uint sd[T + 2][T + 2];
groupshared uint sb[T + 2][T + 2];
groupshared uint anyChange;
uint isBlocked(int x, int z) {
	if (x < 0 || z < 0 || x >= (int)W || z >= (int)H) return 1;
	uint i = z * W + x;
	return (blocked.Load((i >> 2) << 2) >> ((i & 3) * 8)) & 0xFF;
}
[numthreads(T, T, 1)]
void relax(uint3 gid : SV_GroupID, uint3 lid : SV_GroupThreadID) {
	int ox = gid.x * T - 1, oz = gid.y * T - 1;
	uint base = gid.z * W * H;
	if (lid.x == 0 && lid.y == 0) anyChange = 0;
	for (uint k = lid.y * T + lid.x; k < (T + 2) * (T + 2); k += T * T) {
		int lx = k % (T + 2), lz = k / (T + 2), x = ox + lx, z = oz + lz;
		bool inside = x >= 0 && z >= 0 && x < (int)W && z < (int)H;
		sd[lz][lx] = inside ? dist.Load((base + z * W + x) * 4) : 0x3FFFFFFF; // (not reached: no wrap-around when a cost is added)
		sb[lz][lx] = inside ? isBlocked(x, z) : 1;
	}
	GroupMemoryBarrierWithGroupSync();
	uint lx = lid.x + 1, lz = lid.y + 1;
	uint start = sd[lz][lx];
	bool open = sb[lz][lx] == 0;
	for (uint it = 0; it < ITER; it++) {
		uint d = sd[lz][lx];
		if (open) {
			d = min(d, min(min(sd[lz][lx - 1], sd[lz][lx + 1]), min(sd[lz - 1][lx], sd[lz + 1][lx])) + 100);
			d = min(d, min(min(sd[lz - 1][lx - 1], sd[lz - 1][lx + 1]), min(sd[lz + 1][lx - 1], sd[lz + 1][lx + 1])) + 141);
		}
		GroupMemoryBarrierWithGroupSync();
		sd[lz][lx] = d;
		GroupMemoryBarrierWithGroupSync();
	}
	int x = ox + lx, z = oz + lz;
	if (x < (int)W && z < (int)H && sd[lz][lx] < start) {
		dist.InterlockedMin((base + z * W + x) * 4, sd[lz][lx]);
		anyChange = 1;
	}
	GroupMemoryBarrierWithGroupSync();
	if (lid.x == 0 && lid.y == 0 && anyChange) changed.InterlockedAdd(0, 1);
}
)";

// Fog of war for P players at once: one thread per tile and player, looping over that player's
// viewers (FogOfWar::update's stamp: whole 5 m tiles, the last one fading), 8 bits per tile.
static const char* FOG_HLSL = R"(
cbuffer P : register(b0) { uint W, H, NP, pad; };
struct V { float x, z, r; uint pad; };
StructuredBuffer<V> vs : register(t0);
StructuredBuffer<uint2> ranges : register(t1); // per player: first viewer, count
RWByteAddressBuffer fog : register(u0);       // NP * W * H bytes, 4 tiles per uint
[numthreads(64, 1, 1)]
void fogCS(uint3 id : SV_DispatchThreadID) {
	uint quad = id.x, p = id.y;
	uint per = (W * H + 3) / 4;
	if (quad >= per) return;
	uint packed = 0;
	for (uint k = 0; k < 4; k++) {
		uint i = quad * 4 + k;
		if (i >= W * H) break;
		float tx = (i % W) * 5.0 + 2.5, tz = (i / W) * 5.0 + 2.5;
		float best = 0;
		uint2 rg = ranges[p];
		for (uint v = rg.x; v < rg.x + rg.y; v++) {
			float2 d = float2(tx - vs[v].x, tz - vs[v].z);
			best = max(best, saturate((vs[v].r - length(d)) / 5.0 + 0.5));
		}
		packed |= (uint)(best * 255.0) << (k * 8);
	}
	fog.Store((p * per + quad) * 4, packed);
}
)";

static ComPtr<ID3D11Buffer> makeCB(const uint32_t v[4]) {
	D3D11_BUFFER_DESC d{ 16, D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0 };
	D3D11_SUBRESOURCE_DATA s{ v, 0, 0 };
	ComPtr<ID3D11Buffer> b; dev->CreateBuffer(&d, &s, &b); return b;
}

int main(int argc, char** argv) {
	if (argc < 2) { printf("pathvis_bench <dump folder> [adapter]\n"); return 1; }
	load(argv[1]);
	printf("%zu queries, %zu grids, %zu viewers\n", queries.size(), grids.size(), viewers.size());
	if (queries.empty()) return 1;
	const Grid& g0 = grids.begin()->second;
	printf("grid %d x %d tiles\n", g0.w, g0.h);
	const float t0 = queries.front().t, t1 = queries.back().t, span = std::max(1.0f, t1 - t0);
	const double stepsInSpan = span / 0.02;

	// ---- 1. CPU A* replayed with a precomputed blocked grid ----
	double inGame = 0, replay = 0; uint64_t tilesIn = 0; int cut = 0;
	std::vector<double> perQ;
	for (const Query& q : queries) {
		const Grid& g = grids[q.grid];
		auto pred = [&](Pathfinding::PFPos p) { return p.x < 0 || p.z < 0 || p.x >= g.w || p.z >= g.h || g.blocked[p.z * g.w + p.x] != 0; };
		auto t = Clock::now();
		auto path = Pathfinding::DoPathfinding<decltype(pred)>({ q.sx, q.sz }, { q.ex, q.ez }, pred, Pathfinding::ManhattanDiagHeuristic, g.w, g.h);
		double ms = msSince(t);
		replay += ms; perQ.push_back(ms); inGame += q.ms; tilesIn += q.tiles; cut += q.cut;
	}
	std::sort(perQ.begin(), perQ.end());
	printf("\n[A*] %zu searches over %.0f game s (%.2f per step); in game %.3f ms per search (%.3f ms per step), %.0f tiles per search, %d cut short\n",
		queries.size(), span, queries.size() / stepsInSpan, inGame / queries.size(), inGame / stepsInSpan, (double)tilesIn / queries.size(), cut);
	printf("[A*] replayed on a byte grid: %.3f ms per search (p50 %.3f, p99 %.3f, max %.2f), %.3f ms per step\n",
		replay / queries.size(), perQ[perQ.size() / 2], perQ[perQ.size() * 99 / 100], perQ.back(), replay / stepsInSpan);

	// ---- 2. how many searches share a goal (what a flow field could share) ----
	for (float win : { 0.02f, 1.0f, 5.0f }) {
		std::set<std::tuple<int, int, int, int>> goals;
		for (const Query& q : queries) goals.insert({ (int)((q.t - t0) / win), q.grid, q.ex, q.ez });
		printf("[goals] window %.2f s: %zu distinct (grid, goal) for %zu searches (%.2f searches per field)\n", win, goals.size(), queries.size(), (double)queries.size() / goals.size());
	}
	std::map<int, std::set<std::tuple<int, int, int>>> perStep; // step -> distinct (grid, goal)
	for (const Query& q : queries) perStep[(int)std::lround(q.t / 0.02f)].insert({ q.grid, q.ex, q.ez });
	size_t maxPerStep = 0, sumPerStep = 0;
	for (auto& [s, set] : perStep) { maxPerStep = std::max(maxPerStep, set.size()); sumPerStep += set.size(); }
	printf("[goals] steps with a search: %zu, distinct goals per such step avg %.2f, max %zu\n", perStep.size(), (double)sumPerStep / perStep.size(), maxPerStep);

	// ---- 3. CPU flow fields: one Dijkstra per distinct goal per step ----
	std::vector<uint32_t> dist;
	double fieldMs = 0; size_t fields = 0;
	auto tf = Clock::now();
	for (auto& [s, set] : perStep)
		for (auto& [gi, ex, ez] : set) {
			const Grid& g = grids[gi];
			if (ex < 0 || ez < 0 || ex >= g.w || ez >= g.h) continue;
			cpuField(g, ex, ez, dist); fields++;
		}
	fieldMs = msSince(tf);
	printf("[cpu field] %zu fields (one per goal per step), Dial's buckets: %.3f ms per field, %.3f ms per step\n", fields, fieldMs / fields, fieldMs / stepsInSpan);
	{
		std::vector<uint32_t> d2;
		auto th = Clock::now();
		size_t n = 0;
		for (auto& [s, set] : perStep)
			for (auto& [gi, ex, ez] : set) {
				const Grid& g = grids[gi];
				if (ex < 0 || ez < 0 || ex >= g.w || ez >= g.h) continue;
				cpuFieldHeap(g, ex, ez, d2); n++;
				if (n <= 50) { cpuField(g, ex, ez, dist); if (d2 != dist) printf("[cpu field] Dial and heap differ!\n"); }
			}
		printf("[cpu field] with a binary heap: %.3f ms per field\n", msSince(th) / n);
	}
	// path agreement: field path length against A*'s, on the first 300 queries
	{
		int same = 0, n = 0; double ratio = 0;
		for (size_t i = 0; i < queries.size() && n < 300; i++) {
			const Query& q = queries[i];
			if (q.cut || q.len == 0) continue;
			const Grid& g = grids[q.grid];
			cpuField(g, q.ex, q.ez, dist);
			size_t L = walkField(g, dist.data(), q.sx, q.sz);
			if (!L) continue;
			n++; ratio += (double)L / q.len; same += L == q.len;
		}
		if (n) printf("[cpu field] paths down the field: %d of %d as many tiles as A*'s, length ratio %.3f\n", same, n, ratio / n);
	}

	// ---- 3b. a mass order: 200 units in one corner of the map sent to the far side (the case
	// flow fields are made for: a human's whole army right-clicked across the map) ----
	{
		const Grid& g = g0;
		auto open = [&](int x, int z) { return x >= 0 && z >= 0 && x < g.w && z < g.h && !g.blocked[z * g.w + x]; };
		auto nearestOpen = [&](int cx, int cz) {
			for (int r = 0; r < 64; r++)
				for (int z = cz - r; z <= cz + r; z++)
					for (int x = cx - r; x <= cx + r; x++)
						if (open(x, z)) return std::pair<int, int>{ x, z };
			return std::pair<int, int>{ -1, -1 };
		};
		auto goal = nearestOpen(g.w * 4 / 5, g.h * 4 / 5);
		std::vector<std::pair<int, int>> starts;
		uint32_t rng = 12345;
		while (starts.size() < 200) {
			rng = rng * 1664525u + 1013904223u; int x = g.w / 5 + (int)(rng >> 24) % 20;
			rng = rng * 1664525u + 1013904223u; int z = g.h / 5 + (int)(rng >> 24) % 20;
			if (open(x, z)) starts.push_back({ x, z });
		}
		auto pred = [&](Pathfinding::PFPos p) { return !open(p.x, p.z); };
		auto t = Clock::now();
		size_t lenA = 0; int cutA = 0;
		for (auto [x, z] : starts) {
			lenA += Pathfinding::DoPathfinding<decltype(pred)>({ x, z }, { goal.first, goal.second }, pred, Pathfinding::ManhattanDiagHeuristic, g.w, g.h).size();
			cutA += Pathfinding::g_lastSearchCut;
		}
		double a = msSince(t);
		t = Clock::now();
		cpuField(g, goal.first, goal.second, dist);
		double f = msSince(t);
		size_t lenF = 0;
		for (auto [x, z] : starts) lenF += walkField(g, dist.data(), x, z);
		double fw = msSince(t);
		printf("[mass order] 200 units (%d,%d) area to (%d,%d): A* %.2f ms (%.3f each, %d cut short, %.0f tiles avg); one CPU field %.2f ms, with the 200 walks down it %.2f ms (%.0f tiles avg)\n",
			g.w / 5, g.h / 5, goal.first, goal.second, a, a / 200, cutA, (double)lenA / 200, f, fw, (double)lenF / 200);
	}

	// ---- 4. GPU flow fields ----
	if (!initD3D(argc > 2 ? argv[2] : nullptr)) { printf("no D3D11 GPU\n"); return 0; }
	printf("\nGPU: %s\n", adapterName.c_str());
	auto relaxCS = compile(FIELD_HLSL, "relax");
	auto fogCS = compile(FOG_HLSL, "fogCS");
	const UINT W = g0.w, H = g0.h, N = W * H;
	std::vector<uint32_t> packedBlocked((N + 3) / 4, 0);
	for (UINT i = 0; i < N; i++) packedBlocked[i / 4] |= (uint32_t)g0.blocked[i] << ((i % 4) * 8);
	GpuBuf blockedBuf = makeBuf((UINT)packedBlocked.size() * 4, false, packedBlocked.data());
	GpuBuf changedBuf = makeBuf(16, true);
	for (UINT G : { 1u, 4u, 16u }) {
		GpuBuf distBuf = makeBuf(G * N * 4, true);
		uint32_t cbv[4] = { W, H, G, 0 };
		auto cb = makeCB(cbv);
		std::vector<uint32_t> init(G * N, 0x3FFFFFFFu), out(G * N);
		// goals from the queries (the first G distinct on grid 0)
		std::vector<std::pair<int, int>> goals;
		for (const Query& q : queries) {
			if (q.grid != grids.begin()->first) continue;
			std::pair<int, int> gl{ q.ex, q.ez };
			if (std::find(goals.begin(), goals.end(), gl) == goals.end()) goals.push_back(gl);
			if (goals.size() == G) break;
		}
		while (goals.size() < G) goals.push_back(goals.back());
		for (UINT k = 0; k < G; k++) init[k * N + goals[k].second * W + goals[k].first] = 0;
		double best = 1e9, bestKernel = 0; int passes = 0;
		for (int rep = 0; rep < 5; rep++) {
			gpuSync();
			auto t = Clock::now();
			ctx->UpdateSubresource(distBuf.buf.Get(), 0, nullptr, init.data(), 0, 0);
			ID3D11UnorderedAccessView* uavs[2] = { distBuf.uav.Get(), changedBuf.uav.Get() };
			ID3D11ShaderResourceView* srvs[1] = { blockedBuf.srv.Get() };
			ctx->CSSetShader(relaxCS.Get(), nullptr, 0);
			ctx->CSSetConstantBuffers(0, 1, cb.GetAddressOf());
			ctx->CSSetShaderResources(0, 1, srvs);
			ctx->CSSetUnorderedAccessViews(0, 2, uavs, nullptr);
			passes = 0;
			// dispatch in batches of 8 passes, then ask whether any changed (each check waits for the GPU)
			for (;;) {
				const UINT zero[4] = { 0, 0, 0, 0 };
				ctx->ClearUnorderedAccessViewUint(changedBuf.uav.Get(), zero);
				for (int b = 0; b < 8; b++) { ctx->Dispatch((W + 15) / 16, (H + 15) / 16, G); passes++; }
				uint32_t ch[4]; readback(changedBuf, ch);
				if (ch[0] == 0 || passes > 4000) break;
			}
			double kernel = msSince(t);
			readback(distBuf, out.data());
			double ms = msSince(t);
			if (ms < best) { best = ms; bestKernel = kernel; }
		}
		// check against the CPU's fields
		int wrong = 0;
		for (UINT k = 0; k < G; k++) {
			cpuField(g0, goals[k].first, goals[k].second, dist);
			for (UINT i = 0; i < N; i++) wrong += dist[i] != (out[k * N + i] == 0x3FFFFFFFu ? 0xFFFFFFFFu : out[k * N + i]);
		}
		auto tc = Clock::now();
		for (UINT k = 0; k < G; k++) cpuField(g0, goals[k].first, goals[k].second, dist);
		double cpu = msSince(tc);
		printf("[gpu field] %2u fields: %.2f ms with upload and readback (%.2f ms before the readback), %d passes; %d tiles differ from the CPU's; CPU Dijkstra %.2f ms\n",
			G, best, bestKernel, passes, wrong, cpu);
	}

	// ---- 5. fog of war ----
	std::map<uint32_t, std::vector<const Viewer*>> byPlayer;
	for (const Viewer& v : viewers) if (v.sight > 0) byPlayer[v.player].push_back(&v);
	std::vector<uint8_t> fog(N);
	auto stamp = [&](const Viewer& v) {
		const float radius = std::floor(v.sight + 0.5f) * 5.0f;
		if (radius <= 0.0f) return;
		const int x0 = std::max(0, (int)((v.x - radius) / 5.0f)), x1 = std::min((int)W - 1, (int)((v.x + radius) / 5.0f));
		const int z0 = std::max(0, (int)((v.z - radius) / 5.0f)), z1 = std::min((int)H - 1, (int)((v.z + radius) / 5.0f));
		for (int z = z0; z <= z1; z++) {
			const float dz = z * 5.0f + 2.5f - v.z;
			for (int x = x0; x <= x1; x++) {
				const float dx = x * 5.0f + 2.5f - v.x;
				const float d = std::sqrt(dx * dx + dz * dz);
				const float seen = std::clamp((radius - d) / 5.0f + 0.5f, 0.0f, 1.0f);
				uint8_t& t = fog[z * W + x];
				t = std::max(t, (uint8_t)(seen * 255.0f));
			}
		}
	};
	double cpuFogAll = 0;
	printf("\n[fog] %zu players with viewers:", byPlayer.size());
	for (auto& [p, vs] : byPlayer) printf(" %u:%zu", p, vs.size());
	printf("\n");
	{
		const int reps = 20;
		auto t = Clock::now();
		for (int r = 0; r < reps; r++)
			for (auto& [p, vs] : byPlayer) { std::fill(fog.begin(), fog.end(), 0); for (const Viewer* v : vs) stamp(*v); }
		cpuFogAll = msSince(t) / reps;
		printf("[fog] CPU stamps (FogOfWar::update): %.3f ms for all players, %.3f ms per player\n", cpuFogAll, cpuFogAll / byPlayer.size());
	}
	{
		struct GV { float x, z, r; uint32_t pad; };
		std::vector<GV> gv; std::vector<uint32_t> ranges;
		for (auto& [p, vs] : byPlayer) {
			ranges.push_back((uint32_t)gv.size()); ranges.push_back((uint32_t)vs.size());
			for (const Viewer* v : vs) gv.push_back({ v->x, v->z, std::floor(v->sight + 0.5f) * 5.0f, 0 });
		}
		const UINT NP = (UINT)byPlayer.size(), per = (N + 3) / 4;
		auto mkStructured = [&](const void* data, UINT stride, UINT count, ComPtr<ID3D11ShaderResourceView>& srv) {
			D3D11_BUFFER_DESC d{ stride * count, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, stride };
			D3D11_SUBRESOURCE_DATA s{ data, 0, 0 };
			ComPtr<ID3D11Buffer> b; dev->CreateBuffer(&d, &s, &b);
			dev->CreateShaderResourceView(b.Get(), nullptr, &srv);
			return b;
		};
		ComPtr<ID3D11ShaderResourceView> vsrv, rsrv;
		auto vb = mkStructured(gv.data(), sizeof(GV), (UINT)gv.size(), vsrv);
		auto rb = mkStructured(ranges.data(), 8, NP, rsrv);
		GpuBuf fogBuf = makeBuf(NP * per * 4, true);
		uint32_t cbv[4] = { W, H, NP, 0 };
		auto cb = makeCB(cbv);
		std::vector<uint32_t> out(NP * per);
		double best = 1e9, bestKernel = 0;
		for (int rep = 0; rep < 20; rep++) {
			gpuSync();
			auto t = Clock::now();
			ctx->UpdateSubresource(vb.Get(), 0, nullptr, gv.data(), 0, 0); // the viewers move: uploaded every time
			ID3D11ShaderResourceView* srvs[2] = { vsrv.Get(), rsrv.Get() };
			ctx->CSSetShader(fogCS.Get(), nullptr, 0);
			ctx->CSSetConstantBuffers(0, 1, cb.GetAddressOf());
			ctx->CSSetShaderResources(0, 2, srvs);
			ctx->CSSetUnorderedAccessViews(0, 1, fogBuf.uav.GetAddressOf(), nullptr);
			ctx->Dispatch((per + 63) / 64, NP, 1);
			gpuSync();
			double kernel = msSince(t);
			readback(fogBuf, out.data());
			double ms = msSince(t);
			if (ms < best) { best = ms; bestKernel = kernel; }
		}
		// check player 0 against the CPU
		std::fill(fog.begin(), fog.end(), 0);
		for (const Viewer* v : byPlayer.begin()->second) stamp(*v);
		int diff = 0;
		for (UINT i = 0; i < N; i++) { int gvv = (out[i / 4] >> ((i % 4) * 8)) & 0xFF; diff += std::abs(gvv - fog[i]) > 1; }
		printf("[fog] GPU, all %u players in one dispatch: %.3f ms with upload and readback (%.3f ms without the readback); %d tiles differ by more than 1\n", NP, best, bestKernel, diff);
	}

	// ---- 6. sight events: the in-range test, brute force on the CPU as a reference ----
	{
		size_t recv = 0, pairs = 0;
		std::vector<const Viewer*> rs;
		for (const Viewer& v : viewers) if (v.receives && v.sight > 0) rs.push_back(&v);
		auto t = Clock::now();
		for (const Viewer* r : rs) {
			const float radius = std::floor(r->sight + 0.5f) * 5.0f;
			for (const Viewer& o : viewers) {
				const float dx = o.x - r->x, dy = o.y - r->y, dz = o.z - r->z;
				pairs += dx * dx + dy * dy + dz * dz <= radius * radius;
			}
			recv++;
		}
		printf("\n[sight] %zu receivers, %zu objects: brute force all pairs %.3f ms, %zu in range\n", recv, viewers.size(), msSince(t), pairs);
	}
	return 0;
}
