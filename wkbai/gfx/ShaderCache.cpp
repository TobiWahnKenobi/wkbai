// wkbai - the compiled shaders and the driver's pipeline cache kept on disk (see ShaderCache.h)

#include "ShaderCache.h"
#include "../file.h"
#include "../util/LoadingProgress.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <mutex>
#include <set>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#pragma comment(lib, "version.lib")

#include "RenderDevice.h"
#include "Shader.h"
#include "PipelineState.h"
#include "PipelineStateCache.h"
#include "DataBlob.h"
#include "APIInfo.h"
#include "RefCntAutoPtr.hpp"
#include "RenderDeviceD3D12.h"
#include "RenderDeviceVk.h"

using namespace Diligent;

namespace ShaderCache {
namespace {

// Raised whenever the entry layout or what goes into a key changes: every older entry is a miss.
constexpr uint32_t FORMAT_VERSION = 1;
constexpr uint32_t ENTRY_MAGIC = 0x4353574B; // "KWSC"
constexpr uint32_t PIPELINES_MAGIC = 0x5053574B; // "KWSP"
constexpr uint32_t FLAG_FAILED = 1;
constexpr const char* ENTRY_EXT = ".wks";
constexpr const char* PIPELINES_FILE = "pipelines-vk.bin";
constexpr const char* HEADER_FILE = "adapter.txt";
constexpr auto STALE_AFTER = std::chrono::hours(24 * 90);
// a burst of creation is over after this long without a shader or pipeline being made
constexpr double QUIET_SECONDS = 3.0;

// Two independent 64-bit hashes of the same bytes: the first names the entry's file, the second
// is kept inside it and checked, so a collision of one alone is still a miss.
struct Hasher {
	uint64_t a = 1469598103934665603ull; // FNV-1a
	uint64_t b = 0x6A09E667F3BCC909ull;  // a multiplicative hash, finished by splitmix
	void bytes(const void* data, size_t size)
	{
		const uint8_t* p = (const uint8_t*)data;
		for (size_t i = 0; i < size; i++) {
			a = (a ^ p[i]) * 1099511628211ull;
			b = (b + p[i] + 1) * 0x9E3779B97F4A7C15ull;
			b ^= b >> 29;
		}
	}
	template <class T> void value(const T& v) { bytes(&v, sizeof(v)); }
	void text(const char* s)
	{
		const uint64_t n = s ? strlen(s) : ~0ull; // null differs from ""
		value(n);
		if (s)
			bytes(s, (size_t)n);
	}
	void text(const char* s, size_t n)
	{
		const uint64_t len = s ? n : ~0ull;
		value(len);
		if (s)
			bytes(s, n);
	}
	uint64_t second() const
	{
		uint64_t z = b + 0x9E3779B97F4A7C15ull;
		z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
		z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
		return z ^ (z >> 31);
	}
};

uint64_t HashBytes(const void* data, size_t size)
{
	Hasher h;
	h.bytes(data, size);
	return h.second();
}

#pragma pack(push, 1)
struct EntryHeader {
	uint32_t magic;
	uint32_t format;
	uint64_t key;      // the file's name
	uint64_t check;    // the key's second hash
	uint32_t flags;
	uint32_t reserved;
	uint64_t size;     // of the bytecode after this header
	uint64_t dataHash; // of the bytecode
};
struct PipelinesHeader {
	uint32_t magic;
	uint32_t format;
	uint64_t adapter;  // hash of the adapter header
	uint64_t size;
	uint64_t dataHash;
};
#pragma pack(pop)

struct RecordedShader {
	std::string source, filePath, entryPoint, name, combinedSamplerSuffix, glslExtensions;
	std::vector<std::string> macroNames, macroDefinitions;
	ShaderCreateInfo sci;
	RefCntAutoPtr<IShaderSourceInputStreamFactory> factory;
};

struct State {
	std::recursive_mutex lock;
	IRenderDevice* device = nullptr;
	bool open = false;
	bool vulkan = false;
	std::filesystem::path dir;
	std::string header;        // the adapter and driver (adapter.txt)
	std::string fakeDriver;    // tests
	uint64_t compilerHash = 0; // the compilers' versions
	std::string compilerText;
	std::string dxcPath;
	uint64_t sizeCap = 256ull << 20;
	RefCntAutoPtr<IPipelineStateCache> psoCache;
	uint64_t savedPipelinesSize = 0;
	bool pipelinesDirty = false;
	Stats stats, reported;
	bool activity = false;
	std::chrono::steady_clock::time_point lastActivity;
	int bursts = 0;
	std::atomic<uint32_t> tmpCounter{ 0 };
	bool recording = false;
	std::vector<std::unique_ptr<RecordedShader>> recorded;
};

State& S()
{
	static State s;
	return s;
}

double MsSince(std::chrono::steady_clock::time_point t)
{
	return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count();
}

std::string FileVersion(const std::string& path)
{
	if (path.empty())
		return "none";
	DWORD handle = 0;
	const DWORD size = GetFileVersionInfoSizeA(path.c_str(), &handle);
	std::vector<char> data(size ? size : 1);
	VS_FIXEDFILEINFO* info = nullptr;
	UINT len = 0;
	if (!size || !GetFileVersionInfoA(path.c_str(), 0, size, data.data()) || !VerQueryValueA(data.data(), "\\", (void**)&info, &len) || !info)
		return "unknown";
	char text[64];
	snprintf(text, sizeof(text), "%u.%u.%u.%u", HIWORD(info->dwFileVersionMS), LOWORD(info->dwFileVersionMS),
		HIWORD(info->dwFileVersionLS), LOWORD(info->dwFileVersionLS));
	return text;
}

std::string ModulePath(const char* dll)
{
	HMODULE m = LoadLibraryA(dll);
	if (!m)
		return std::string();
	char path[MAX_PATH] = {};
	GetModuleFileNameA(m, path, MAX_PATH);
	return path;
}

// the adapter and driver, as text: a change in any line drops the whole cache
std::string BuildHeader(IRenderDevice* device, bool vulkan)
{
	const GraphicsAdapterInfo& ai = device->GetAdapterInfo();
	char line[512];
	std::string h;
	snprintf(line, sizeof(line), "wkbai shader cache %u\nbackend %s\nadapter %s\nvendor 0x%04X\ndevice 0x%04X\n", FORMAT_VERSION,
		vulkan ? "vulkan" : "d3d12", ai.Description, ai.VendorId, ai.DeviceId);
	h += line;
	if (vulkan) {
		RefCntAutoPtr<IRenderDeviceVk> vk(device, IID_RenderDeviceVk);
		HMODULE lib = LoadLibraryA("vulkan-1.dll");
		auto getProc = lib ? (PFN_vkGetInstanceProcAddr)GetProcAddress(lib, "vkGetInstanceProcAddr") : nullptr;
		auto getProps = (vk && getProc) ? (PFN_vkGetPhysicalDeviceProperties)getProc(vk->GetVkInstance(), "vkGetPhysicalDeviceProperties") : nullptr;
		if (getProps) {
			VkPhysicalDeviceProperties props{};
			getProps(vk->GetVkPhysicalDevice(), &props);
			char uuid[VK_UUID_SIZE * 2 + 1] = {};
			for (int i = 0; i < VK_UUID_SIZE; i++)
				snprintf(uuid + 2 * i, 3, "%02x", props.pipelineCacheUUID[i]);
			snprintf(line, sizeof(line), "api 0x%08X\ndriver 0x%08X\npipeline cache %s\n", props.apiVersion, props.driverVersion, uuid);
			h += line;
		}
		else
			h += "driver unknown\n";
	}
	else {
		RefCntAutoPtr<IRenderDeviceD3D12> d3d(device, IID_RenderDeviceD3D12);
		bool found = false;
		IDXGIFactory4* factory = nullptr;
		if (d3d && SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory4), (void**)&factory))) {
			IDXGIAdapter1* adapter = nullptr;
			if (SUCCEEDED(factory->EnumAdapterByLuid(d3d->GetD3D12Device()->GetAdapterLuid(), __uuidof(IDXGIAdapter1), (void**)&adapter))) {
				DXGI_ADAPTER_DESC1 d = {};
				adapter->GetDesc1(&d);
				LARGE_INTEGER umd = {};
				const bool haveUmd = SUCCEEDED(adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &umd));
				snprintf(line, sizeof(line), "subsystem 0x%08X\nrevision 0x%02X\ndriver %u.%u.%u.%u\n", d.SubSysId, d.Revision,
					haveUmd ? HIWORD(umd.HighPart) : 0, haveUmd ? LOWORD(umd.HighPart) : 0, haveUmd ? HIWORD(umd.LowPart) : 0, haveUmd ? LOWORD(umd.LowPart) : 0);
				h += line;
				found = true;
				adapter->Release();
			}
			factory->Release();
		}
		if (!found)
			h += "driver unknown\n";
	}
	return h;
}

std::string ReadFile(const std::filesystem::path& p, bool* found = nullptr)
{
	std::ifstream in(p, std::ios::binary);
	if (found)
		*found = (bool)in;
	if (!in)
		return std::string();
	return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// written to a temporary name beside it, then renamed over it: a crash leaves the old file or none
bool WriteAtomic(const std::filesystem::path& p, const void* a, size_t aSize, const void* b, size_t bSize)
{
	std::error_code ec;
	std::filesystem::create_directories(p.parent_path(), ec);
	char suffix[64];
	snprintf(suffix, sizeof(suffix), ".%lu.%u.tmp", GetCurrentProcessId(), S().tmpCounter++);
	std::filesystem::path tmp = p;
	tmp += suffix;
	{
		std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
		if (!out)
			return false;
		out.write((const char*)a, (std::streamsize)aSize);
		if (bSize)
			out.write((const char*)b, (std::streamsize)bSize);
		out.flush();
		if (!out) {
			out.close();
			std::filesystem::remove(tmp, ec);
			return false;
		}
	}
	if (!MoveFileExW(tmp.c_str(), p.c_str(), MOVEFILE_REPLACE_EXISTING)) {
		std::filesystem::remove(tmp, ec);
		return false;
	}
	return true;
}

bool IsEntry(const std::filesystem::path& p)
{
	return p.extension() == ENTRY_EXT;
}

// Removes temporary files, the old FSR 3 cache files (fsr3-*.bin), entries unused for 90 days,
// then the least recently used entries until the folder is under the cap. Returns entries left.
int Prune()
{
	State& s = S();
	std::error_code ec;
	struct Entry {
		std::filesystem::path path;
		std::filesystem::file_time_type time;
		uint64_t size;
	};
	std::vector<Entry> entries;
	uint64_t total = 0;
	const auto now = std::filesystem::file_time_type::clock::now();
	for (auto it = std::filesystem::directory_iterator(s.dir, ec); !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
		if (!it->is_regular_file(ec))
			continue;
		const std::filesystem::path p = it->path();
		const std::string name = p.filename().string();
		const auto time = it->last_write_time(ec);
		if (p.extension() == ".tmp") {
			// another running copy may be writing it: only old ones
			if (now - time > std::chrono::minutes(10))
				std::filesystem::remove(p, ec);
			continue;
		}
		if (name.rfind("fsr3-", 0) == 0 && p.extension() == ".bin") {
			std::filesystem::remove(p, ec);
			continue;
		}
		if (!IsEntry(p))
			continue;
		if (now - time > STALE_AFTER) {
			std::filesystem::remove(p, ec);
			continue;
		}
		const uint64_t size = it->file_size(ec);
		entries.push_back({ p, time, size });
		total += size;
	}
	if (total > s.sizeCap) {
		std::sort(entries.begin(), entries.end(), [](const Entry& x, const Entry& y) { return x.time < y.time; });
		size_t i = 0;
		for (; i < entries.size() && total > s.sizeCap; i++) {
			std::filesystem::remove(entries[i].path, ec);
			total -= entries[i].size;
		}
		entries.erase(entries.begin(), entries.begin() + i);
	}
	return (int)entries.size();
}

void DropAll()
{
	State& s = S();
	std::error_code ec;
	for (auto it = std::filesystem::directory_iterator(s.dir, ec); !ec && it != std::filesystem::directory_iterator(); it.increment(ec))
		if (IsEntry(it->path()) || it->path().filename() == PIPELINES_FILE)
			std::filesystem::remove(it->path(), ec);
}

void OpenPipelineCache()
{
	State& s = S();
	s.psoCache.Release();
	s.savedPipelinesSize = 0;
	s.pipelinesDirty = false;
	if (!s.vulkan)
		return; // Direct3D 12: the driver keeps its own disk cache of the compiled bytecode
	const std::string data = ReadFile(s.dir / PIPELINES_FILE);
	const void* blob = nullptr;
	Uint32 blobSize = 0;
	if (data.size() >= sizeof(PipelinesHeader)) {
		PipelinesHeader h;
		memcpy(&h, data.data(), sizeof(h));
		const char* payload = data.data() + sizeof(h);
		if (h.magic == PIPELINES_MAGIC && h.format == FORMAT_VERSION && h.adapter == HashBytes(s.header.data(), s.header.size())
			&& h.size == data.size() - sizeof(h) && h.dataHash == HashBytes(payload, (size_t)h.size)) {
			blob = payload;
			blobSize = (Uint32)h.size;
		}
		else
			printf("Shader cache: the pipeline cache is damaged or stale, starting it again\n");
	}
	PipelineStateCacheCreateInfo ci;
	ci.Desc.Name = "wkbai pipelines";
	ci.pCacheData = blob;
	ci.CacheDataSize = blobSize;
	s.device->CreatePipelineStateCache(ci, &s.psoCache);
	if (!s.psoCache && blob) {
		ci.pCacheData = nullptr;
		ci.CacheDataSize = 0;
		s.device->CreatePipelineStateCache(ci, &s.psoCache);
	}
	s.savedPipelinesSize = blobSize;
}

void SavePipelineCache()
{
	State& s = S();
	if (!s.psoCache || !s.pipelinesDirty)
		return;
	RefCntAutoPtr<IDataBlob> blob;
	s.psoCache->GetData(&blob);
	s.pipelinesDirty = false;
	if (!blob || !blob->GetSize() || blob->GetSize() == s.savedPipelinesSize)
		return;
	PipelinesHeader h{ PIPELINES_MAGIC, FORMAT_VERSION, HashBytes(s.header.data(), s.header.size()), (uint64_t)blob->GetSize(),
		HashBytes(blob->GetConstDataPtr(), blob->GetSize()) };
	if (WriteAtomic(s.dir / PIPELINES_FILE, &h, sizeof(h), blob->GetConstDataPtr(), blob->GetSize()))
		s.savedPipelinesSize = blob->GetSize();
}

void Open(const std::string& fakeDriver)
{
	State& s = S();
	s.fakeDriver = fakeDriver;
	s.dir = g_userPath / "shadercache";
	std::error_code ec;
	std::filesystem::create_directories(s.dir, ec);
	s.vulkan = s.device->GetDeviceInfo().Type == RENDER_DEVICE_TYPE_VULKAN;
	s.header = BuildHeader(s.device, s.vulkan) + fakeDriver;
	// the compilers: Diligent's own (and the glslang built into it), FXC and DXC
	s.compilerText = "Diligent " + std::to_string(DILIGENT_API_VERSION) + ", FXC " + FileVersion(ModulePath("d3dcompiler_47.dll"))
		+ ", DXC " + FileVersion(s.dxcPath);
	s.compilerHash = HashBytes(s.compilerText.data(), s.compilerText.size());

	const std::string old = ReadFile(s.dir / HEADER_FILE);
	s.stats.rebuilt = false;
	s.stats.reason.clear();
	if (old != s.header) {
		bool hadEntries = false;
		for (auto it = std::filesystem::directory_iterator(s.dir, ec); !ec && it != std::filesystem::directory_iterator(); it.increment(ec))
			hadEntries |= IsEntry(it->path());
		if (!old.empty() || hadEntries) {
			// the first line that differs, for the log
			auto lines = [](const std::string& t) {
				std::vector<std::string> v;
				size_t a = 0;
				while (a < t.size()) {
					size_t b = t.find('\n', a);
					if (b == std::string::npos) b = t.size();
					v.push_back(t.substr(a, b - a));
					a = b + 1;
				}
				return v;
			};
			const auto was = lines(old), now = lines(s.header);
			std::string reason = "the adapter header changed";
			for (size_t i = 0; i < std::max(was.size(), now.size()); i++) {
				const std::string x = i < was.size() ? was[i] : "(none)", y = i < now.size() ? now[i] : "(none)";
				if (x != y) {
					reason = "\"" + x + "\" -> \"" + y + "\"";
					break;
				}
			}
			s.stats.rebuilt = true;
			s.stats.reason = reason;
			printf("Shader cache: the graphics adapter or driver changed (%s): rebuilding every shader\n", reason.c_str());
		}
		DropAll();
		WriteAtomic(s.dir / HEADER_FILE, s.header.data(), s.header.size(), nullptr, 0);
	}
	const int entries = Prune();
	OpenPipelineCache();
	s.open = true;
	printf("Shader cache: %s, %d entries; %s\n", s.dir.string().c_str(), entries, s.compilerText.c_str());
}

// Adds the source and, recursively, every file it includes (through the shader's source
// factory), each once.
void HashSource(Hasher& h, const char* text, size_t length, IShaderSourceInputStreamFactory* factory, std::set<std::string>& seen, int depth)
{
	h.text(text, length);
	if (!factory || depth > 32)
		return;
	const char* end = text + length;
	for (const char* p = text; p < end; p++) {
		p = (const char*)memchr(p, '#', end - p);
		if (!p)
			break;
		const char* q = p + 1;
		while (q < end && (*q == ' ' || *q == '\t')) q++;
		if (end - q < 7 || memcmp(q, "include", 7) != 0)
			continue;
		q += 7;
		while (q < end && (*q == ' ' || *q == '\t')) q++;
		if (q >= end || (*q != '"' && *q != '<'))
			continue;
		const char close = *q == '"' ? '"' : '>';
		const char* nameEnd = (const char*)memchr(q + 1, close, end - q - 1);
		if (!nameEnd)
			continue;
		const std::string name(q + 1, nameEnd);
		h.text(name.c_str());
		if (!seen.insert(name).second)
			continue;
		RefCntAutoPtr<IFileStream> stream;
		factory->CreateInputStream2(name.c_str(), CREATE_SHADER_SOURCE_INPUT_STREAM_FLAG_SILENT, &stream);
		if (!stream) {
			h.text("(missing)");
			continue;
		}
		std::string data((size_t)stream->GetSize(), '\0');
		if (!data.empty())
			stream->Read(data.data(), data.size());
		HashSource(h, data.data(), data.size(), factory, seen, depth + 1);
	}
}

Hasher Key(const ShaderCreateInfo& sci)
{
	State& s = S();
	Hasher h;
	h.value(FORMAT_VERSION);
	h.value(s.compilerHash);
	h.value((int)s.device->GetDeviceInfo().Type);
	std::set<std::string> seen;
	if (sci.Source) {
		const size_t length = sci.SourceLength ? sci.SourceLength : strlen(sci.Source);
		HashSource(h, sci.Source, length, sci.pShaderSourceStreamFactory, seen, 0);
	}
	else if (sci.FilePath && sci.pShaderSourceStreamFactory) {
		h.text(sci.FilePath);
		RefCntAutoPtr<IFileStream> stream;
		sci.pShaderSourceStreamFactory->CreateInputStream2(sci.FilePath, CREATE_SHADER_SOURCE_INPUT_STREAM_FLAG_SILENT, &stream);
		std::string data(stream ? (size_t)stream->GetSize() : 0, '\0');
		if (!data.empty())
			stream->Read(data.data(), data.size());
		seen.insert(sci.FilePath);
		HashSource(h, data.data(), data.size(), sci.pShaderSourceStreamFactory, seen, 0);
	}
	else
		h.text("(no source)");
	h.text(sci.EntryPoint);
	for (Uint32 m = 0; m < sci.Macros.Count; m++) {
		h.text(sci.Macros.Elements[m].Name);
		h.text(sci.Macros.Elements[m].Definition);
	}
	h.value((int)sci.Desc.ShaderType);
	h.value((int)sci.Desc.UseCombinedTextureSamplers);
	h.text(sci.Desc.CombinedSamplerSuffix);
	h.value((int)sci.SourceLanguage);
	h.value((int)sci.ShaderCompiler);
	h.value(sci.HLSLVersion);
	h.value(sci.GLSLVersion);
	h.value(sci.GLESSLVersion);
	h.value(sci.MSLVersion);
	h.value((int)sci.CompileFlags);
	h.value((int)sci.LoadConstantBufferReflection);
	h.text(sci.GLSLExtensions);
	return h;
}

std::filesystem::path EntryPath(uint64_t key)
{
	char name[32];
	snprintf(name, sizeof(name), "%016llx%s", (unsigned long long)key, ENTRY_EXT);
	return S().dir / name;
}

void NoteActivity()
{
	State& s = S();
	s.activity = true;
	s.lastActivity = std::chrono::steady_clock::now();
}

void Record(const ShaderCreateInfo& sci)
{
	auto r = std::make_unique<RecordedShader>();
	r->sci = sci;
	if (sci.Source) {
		r->source.assign(sci.Source, sci.SourceLength ? sci.SourceLength : strlen(sci.Source));
		r->sci.Source = r->source.c_str();
		r->sci.SourceLength = r->source.size();
	}
	auto keep = [](std::string& own, const char*& p) {
		if (p) {
			own = p;
			p = own.c_str();
		}
	};
	keep(r->filePath, r->sci.FilePath);
	keep(r->entryPoint, r->sci.EntryPoint);
	keep(r->name, r->sci.Desc.Name);
	keep(r->combinedSamplerSuffix, r->sci.Desc.CombinedSamplerSuffix);
	keep(r->glslExtensions, r->sci.GLSLExtensions);
	r->factory = sci.pShaderSourceStreamFactory;
	r->macroNames.reserve(sci.Macros.Count);
	r->macroDefinitions.reserve(sci.Macros.Count);
	for (Uint32 m = 0; m < sci.Macros.Count; m++) {
		r->macroNames.push_back(sci.Macros.Elements[m].Name ? sci.Macros.Elements[m].Name : "");
		r->macroDefinitions.push_back(sci.Macros.Elements[m].Definition ? sci.Macros.Elements[m].Definition : "");
	}
	S().recorded.push_back(std::move(r));
}

} // namespace

void Init(IRenderDevice* device, const std::string& dxcPath)
{
	State& s = S();
	std::lock_guard<std::recursive_mutex> guard(s.lock);
	s.device = device;
	s.dxcPath = dxcPath;
	if (!device)
		return;
	Open(std::string());
}

void CreateShader(IRenderDevice* device, const ShaderCreateInfo& sci, IShader** shader)
{
	State& s = S();
	*shader = nullptr;
	const auto start = std::chrono::steady_clock::now();
	uint64_t key = 0, check = 0;
	std::filesystem::path path;
	bool corrupt = false;
	{
		std::lock_guard<std::recursive_mutex> guard(s.lock);
		if (s.recording)
			Record(sci);
		NoteActivity();
		s.stats.requests++;
		if (!s.open || device != s.device || sci.ByteCode) {
			device->CreateShader(sci, shader);
			s.stats.compiled++;
			s.stats.compileMs += MsSince(start);
			return;
		}
		const Hasher h = Key(sci);
		key = h.a;
		check = h.second();
		path = EntryPath(key);
	}
	// the entry, if it is whole
	bool found = false;
	const std::string data = ReadFile(path, &found);
	if (found) {
		EntryHeader eh{};
		bool ok = data.size() >= sizeof(eh);
		if (ok) {
			memcpy(&eh, data.data(), sizeof(eh));
			ok = eh.magic == ENTRY_MAGIC && eh.format == FORMAT_VERSION && eh.key == key && eh.check == check
				&& eh.size == data.size() - sizeof(eh) && eh.dataHash == HashBytes(data.data() + sizeof(eh), (size_t)eh.size);
		}
		if (ok && (eh.flags & FLAG_FAILED)) {
			std::lock_guard<std::recursive_mutex> guard(s.lock);
			s.stats.loaded++;
			s.stats.failed++;
			s.stats.loadMs += MsSince(start);
			std::error_code ec;
			std::filesystem::last_write_time(path, std::filesystem::file_time_type::clock::now(), ec);
			return;
		}
		if (ok && eh.size) {
			ShaderCreateInfo bci = sci;
			bci.Source = nullptr;
			bci.SourceLength = 0;
			bci.FilePath = nullptr;
			bci.pShaderSourceStreamFactory = nullptr;
			bci.Macros = {};
			bci.ByteCode = data.data() + sizeof(eh);
			bci.ByteCodeSize = (size_t)eh.size;
			device->CreateShader(bci, shader);
			if (*shader) {
				std::lock_guard<std::recursive_mutex> guard(s.lock);
				s.stats.loaded++;
				s.stats.loadMs += MsSince(start);
				// the least recently used go first when the folder is over its cap
				std::error_code ec;
				std::filesystem::last_write_time(path, std::filesystem::file_time_type::clock::now(), ec);
				return;
			}
		}
		corrupt = true;
	}
	// a miss: compile it and keep the bytecode (or the failure)
	const auto compileStart = std::chrono::steady_clock::now();
	device->CreateShader(sci, shader);
	const void* code = nullptr;
	Uint64 size = 0;
	if (*shader)
		(*shader)->GetBytecode(&code, size);
	EntryHeader eh{ ENTRY_MAGIC, FORMAT_VERSION, key, check, *shader ? 0u : FLAG_FAILED, 0, 0, 0 };
	if (*shader && code && size) {
		eh.size = size;
		eh.dataHash = HashBytes(code, (size_t)size);
		WriteAtomic(path, &eh, sizeof(eh), code, (size_t)size);
	}
	else if (!*shader) {
		eh.dataHash = HashBytes(nullptr, 0);
		WriteAtomic(path, &eh, sizeof(eh), nullptr, 0);
	}
	std::lock_guard<std::recursive_mutex> guard(s.lock);
	s.stats.compiled++;
	s.stats.corrupt += corrupt;
	s.stats.failed += !*shader;
	s.stats.compileMs += MsSince(compileStart);
	s.stats.loadMs += std::chrono::duration<double, std::milli>(compileStart - start).count();
}

template <class CI, class F>
static void CreatePipeline(IRenderDevice* device, const CI& ci, IPipelineState** pso, F create)
{
	State& s = S();
	const auto start = std::chrono::steady_clock::now();
	IPipelineStateCache* cache = nullptr;
	{
		std::lock_guard<std::recursive_mutex> guard(s.lock);
		NoteActivity();
		if (s.open && device == s.device)
			cache = s.psoCache;
	}
	if (cache && !ci.pPSOCache) {
		CI c = ci;
		c.pPSOCache = cache;
		create(c, pso);
	}
	else
		create(ci, pso);
	std::lock_guard<std::recursive_mutex> guard(s.lock);
	s.stats.pipelines++;
	s.stats.pipelineMs += MsSince(start);
	s.pipelinesDirty |= cache != nullptr;
}

void CreateGraphicsPipeline(IRenderDevice* device, const GraphicsPipelineStateCreateInfo& ci, IPipelineState** pso)
{
	CreatePipeline(device, ci, pso, [device](const GraphicsPipelineStateCreateInfo& c, IPipelineState** p) { device->CreateGraphicsPipelineState(c, p); });
}

void CreateComputePipeline(IRenderDevice* device, const ComputePipelineStateCreateInfo& ci, IPipelineState** pso)
{
	CreatePipeline(device, ci, pso, [device](const ComputePipelineStateCreateInfo& c, IPipelineState** p) { device->CreateComputePipelineState(c, p); });
}

void OnFrame()
{
	State& s = S();
	std::unique_lock<std::recursive_mutex> guard(s.lock, std::try_to_lock);
	if (!guard.owns_lock() || !s.activity)
		return;
	// (a map load pauses longer between its bursts: its line once it is over)
	if (LoadingActive() || std::chrono::duration<double>(std::chrono::steady_clock::now() - s.lastActivity).count() < QUIET_SECONDS)
		return;
	s.activity = false;
	// this burst: the change since the last line
	const Stats& t = s.stats;
	const Stats& r = s.reported;
	const int requests = t.requests - r.requests;
	if (requests > 0 || t.pipelines != r.pipelines) {
		printf("Shader cache (%s): %d shaders, %d from the cache, %d compiled%s%s (%.0f ms compiling, %.0f ms loading), %d pipelines in %.0f ms%s%s\n",
			s.bursts ? "map load" : "start", requests, t.loaded - r.loaded, t.compiled - r.compiled,
			t.corrupt - r.corrupt ? (", " + std::to_string(t.corrupt - r.corrupt) + " damaged entries replaced").c_str() : "",
			t.failed - r.failed ? (", " + std::to_string(t.failed - r.failed) + " failing").c_str() : "",
			t.compileMs - r.compileMs, t.loadMs - r.loadMs, t.pipelines - r.pipelines, t.pipelineMs - r.pipelineMs,
			(!s.bursts && t.rebuilt) ? ", rebuilt: " : "", (!s.bursts && t.rebuilt) ? t.reason.c_str() : "");
		fflush(stdout);
		s.bursts++;
	}
	s.reported = s.stats;
	SavePipelineCache();
}

Stats GetStats()
{
	std::lock_guard<std::recursive_mutex> guard(S().lock);
	return S().stats;
}

void ResetStats()
{
	State& s = S();
	std::lock_guard<std::recursive_mutex> guard(s.lock);
	const bool rebuilt = s.stats.rebuilt;
	const std::string reason = s.stats.reason;
	s.stats = Stats();
	s.stats.rebuilt = rebuilt;
	s.stats.reason = reason;
	s.reported = s.stats;
}

std::filesystem::path Directory()
{
	return S().dir;
}

std::string AdapterHeader()
{
	return S().header;
}

void ReopenForTest(const std::string& fakeDriver)
{
	State& s = S();
	std::lock_guard<std::recursive_mutex> guard(s.lock);
	if (s.device)
		Open(fakeDriver);
}

void SetSizeCapForTest(uint64_t bytes)
{
	std::lock_guard<std::recursive_mutex> guard(S().lock);
	S().sizeCap = bytes;
}

int PruneNow()
{
	std::lock_guard<std::recursive_mutex> guard(S().lock);
	return Prune();
}

void SetRecording(bool on)
{
	std::lock_guard<std::recursive_mutex> guard(S().lock);
	S().recording = on;
}

size_t RecordedCount()
{
	std::lock_guard<std::recursive_mutex> guard(S().lock);
	return S().recorded.size();
}

int Replay(int touched)
{
	State& s = S();
	std::vector<RecordedShader*> list;
	{
		std::lock_guard<std::recursive_mutex> guard(s.lock);
		for (auto& r : s.recorded)
			list.push_back(r.get());
	}
	int made = 0;
	for (size_t i = 0; i < list.size(); i++) {
		RecordedShader& r = *list[i];
		ShaderCreateInfo sci = r.sci;
		std::vector<ShaderMacro> macros;
		for (size_t m = 0; m < r.macroNames.size(); m++)
			macros.push_back({ r.macroNames[m].c_str(), r.macroDefinitions[m].c_str() });
		sci.Macros = ShaderMacroArray{ macros.data(), (Uint32)macros.size() };
		sci.pShaderSourceStreamFactory = r.factory;
		std::string source;
		if ((int)i == touched && sci.Source) {
			source = r.source + "\n// touched by the shader cache test\n";
			sci.Source = source.c_str();
			sci.SourceLength = source.size();
		}
		RefCntAutoPtr<IShader> shader;
		CreateShader(s.device, sci, &shader);
		made += shader != nullptr;
	}
	return made;
}

} // namespace ShaderCache
