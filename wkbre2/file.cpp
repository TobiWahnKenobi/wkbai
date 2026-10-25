// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#include "wkbre2.h"
#include "file.h"
#include "util/util.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <memory>
#include <mutex>
#include <set>
#include <nlohmann/json.hpp>
#include "settings.h"
#include "util/StriCompare.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

extern "C" {

//#include "lzrw_headers.h" // This redefines some types from the Windows API :/
// Instead we will define a part of the LZRW header directly here:
#define COMPRESS_ACTION_IDENTITY   0
#define COMPRESS_ACTION_COMPRESS   1
#define COMPRESS_ACTION_DECOMPRESS 2

#define COMPRESS_OVERRUN 1024
#define COMPRESS_MAX_COM 0x70000000
#define COMPRESS_MAX_ORG (COMPRESS_MAX_COM-COMPRESS_OVERRUN)

#define COMPRESS_MAX_STRLEN 255

void compress(unsigned short action, unsigned char *wrk_mem, unsigned char *src_adr, unsigned int src_len, unsigned char *dst_adr, unsigned int *p_dst_len);

}

#include <bzlib.h>

// Fixs for wkbre2 until refactor of file.cpp
uint32_t readInt(FILE *f) { uint32_t temp; fread(&temp, 4, 1, f); return temp; } 

#define MEM_REQ ( 4096*sizeof(char*) + 16 + 16 ) // TODO: Check if correct, there are rare crashes of mem corruption!!!

struct BCPDirectory
{
	BCPDirectory *dirs; int ndirs;
	BCPFile *files; int nfiles;
	std::string name;
	BCPDirectory *parent;
};

struct BCPReader
{
	FILE *bcpfile;
	int fentof;
	int nfiles, ftsize;
	BCPFileData *fent;
	int bcpver;
	BCPDirectory bcproot;

	std::string BCPReadString();
	void LookAtDir(BCPDirectory *bd, int isroot, BCPDirectory *prev);
	BCPReader(const std::filesystem::path& bcpPath);
	~BCPReader();
	BCPReader(const BCPReader&) = delete;
	BCPReader& operator=(const BCPReader&) = delete;
	void ExtractFile(int id, char **out, int *outsize, int extraBytes);
	BCPFile *getFile(const char *fn);
	BCPDirectory *getDirectory(const char *dn);
	bool loadFile(const char *fn, char **out, int *outsize, int extraBytes);
	bool fileExists(const char *fn);
	void listFileNames(const char *dn, std::vector<std::string>* gsl);
	void listDirectories(const char *dn, std::vector<std::string>* gsl);
};

std::filesystem::path g_gamePath = ".";
bool g_allowDataDirectory = true, g_macFileNamesFallbackEnabled = true;

static FILE* FsOpen(const std::filesystem::path& filePath, const char* mode)
{
#ifdef _WIN32
	FILE* file = nullptr;
	auto error = _wfopen_s(&file, filePath.c_str(), std::wstring(mode, mode + strlen(mode)).c_str());
	return (error == 0) ? file : nullptr;
#else
	return fopen(filePath.c_str(), mode);
#endif
}

std::string BCPReader::BCPReadString()
{
	uint8_t cs = (uint8_t)fgetc(bcpfile);
	std::string str(cs, 0);
	if (bcpver == 0)
		fread((char*)str.data(), cs, 1, bcpfile);
	else
		for (int i = 0; i < cs; i++) {
			str[i] = fgetc(bcpfile);
			fgetc(bcpfile);
		}
	return str;
}

void BCPReader::LookAtDir(BCPDirectory *bd, int isroot, BCPDirectory *prev)
{
	if(!isroot) bd->name = BCPReadString();
	bd->parent = prev;
	bd->nfiles = readInt(bcpfile);
	if(bd->nfiles)
	{
		bd->files = new BCPFile[bd->nfiles];
		for(int i = 0; i < bd->nfiles; i++)
		{
			bd->files[i].id = readInt(bcpfile);
			fread(&bd->files[i].time, 8, 1, bcpfile);
			bd->files[i].name = BCPReadString();
		}
	}
	bd->ndirs = readInt(bcpfile);
	if(bd->ndirs)
	{
		bd->dirs = new BCPDirectory[bd->ndirs];
		for(int i = 0; i < bd->ndirs; i++)
			LookAtDir(&bd->dirs[i], 0, bd);
	}
}

BCPReader::BCPReader(const std::filesystem::path& bcpPath)
{
	nfiles = 0;

	bcpfile = FsOpen(g_gamePath / bcpPath, "rb");
	if(!bcpfile) ferr("Cannot load BCP file.");
	fseek(bcpfile, 9, SEEK_SET);
	bcpver = fgetc(bcpfile) - '0';
	fseek(bcpfile, 0x30, SEEK_SET);
	fentof = readInt(bcpfile);
	ftsize = readInt(bcpfile);

	fseek(bcpfile, fentof, SEEK_SET);
	nfiles = readInt(bcpfile);
	fent = (BCPFileData*)malloc(nfiles*20);
	if(!fent) ferr("Failed to allocate memory for the file position/size table.");
	fread(fent, nfiles*20, 1, bcpfile);

	fseek(bcpfile, fentof+4+nfiles*20, SEEK_SET);
	LookAtDir(&bcproot, 1, &bcproot);
}

static void FreeBCPDirectory(BCPDirectory *bd)
{
	// files/dirs are only allocated when their count is non-zero
	if (bd->nfiles) delete[] bd->files;
	if (bd->ndirs) {
		for (int i = 0; i < bd->ndirs; i++)
			FreeBCPDirectory(&bd->dirs[i]);
		delete[] bd->dirs;
	}
}

BCPReader::~BCPReader()
{
	FreeBCPDirectory(&bcproot);
	free(fent);
	if (bcpfile) fclose(bcpfile);
}

void BCPReader::ExtractFile(int id, char **out, int *outsize, int extraBytes)
{
	char *min, *mout, *ws;

	static std::mutex bcpmutex;
	const std::lock_guard<std::mutex> bml(bcpmutex);

	fseek(bcpfile, fent[id].offset, SEEK_SET);
	int s = fent[id].endos - fent[id].offset;
	mout = (char*)malloc(fent[id].size+extraBytes); if(!mout) ferr("Cannot alloc mem for loading a file.");
	*out = mout;

	switch(fent[id].form)
	{
		case 1: // Uncompressed
			fread(mout, s, 1, bcpfile);
			*outsize = s;
			break;
		case 2: // Zero-sized file
			*outsize = 0;
			break;
		case 3: // bzip2-compressed file
			{int be;
			BZFILE *bz = BZ2_bzReadOpen(&be, bcpfile, 0, 0, NULL, 0);
			if(be != BZ_OK) ferr("Failed to initialize bzip2 decompression.");
			int l = BZ2_bzRead(&be, bz, mout, fent[id].size);
			if((be != BZ_OK) && (be != BZ_STREAM_END)) ferr("Failed to decompress bzip2 file.");
			BZ2_bzReadClose(&be, bz);
			*outsize = l;
			break;}
		case 4: // LZRW3-compressed file
			{ws = (char*)malloc(MEM_REQ);
			min = (char*)malloc(s);
			fread(min, s, 1, bcpfile);
			//*outsize = fent[id].size;
			unsigned int us = fent[id].size;
			compress(COMPRESS_ACTION_DECOMPRESS, (unsigned char*)ws, (unsigned char*)min, s, (unsigned char*)mout, &us);
			*outsize = (int)us;
			free(ws); free(min);
			break;}
		default:
			ferr("Cannot load a file with an unknown format.");
	}
}

BCPFile *BCPReader::getFile(const char *fn)
{
	char *p, *c; BCPDirectory *ad = &bcproot;
	std::string ftb = fn;
	p = (char*)ftb.c_str();
	for(auto &e : ftb)
		if(e == '/')
			e = '\\';
	while(1)
	{
lflp:		c = strchr(p, '\\');
		if(!c)
		{
			// A file!
			for(int i = 0; i < ad->nfiles; i++)
			if(!StrCICompare(p, ad->files[i].name))
			{
				// File found!
				return &ad->files[i];
			}
			break; // File not found.

		} else {
			// A directory!
			*c = 0;
			if(!strcmp(p, "."))
				{p = c+1; goto lflp;}
			if(!strcmp(p, ".."))
			{
				// Go one directory backwards
				p = c+1;
				ad = ad->parent;
				goto lflp;
			}
			for(int i = 0; i < ad->ndirs; i++)
			if(!StrCICompare(p, ad->dirs[i].name))
			{
				// Dir found!
				p = c+1;
				ad = &ad->dirs[i];
				goto lflp;
			}
			break; // Dir not found.
		}
	}
	return 0;
}

BCPDirectory *BCPReader::getDirectory(const char *dn)
{
	char *p, *c; BCPDirectory *ad = &bcproot;
	std::string ftb = dn;
	p = (char*)ftb.c_str();
	for (auto& e : ftb)
		if (e == '/')
			e = '\\';
	while(1)
	{
lflp:		c = strchr(p, '\\');
		if(c)
		{
			// A directory!
			*c = 0;
			if(!strcmp(p, "."))
				{p = c+1; goto lflp;}
			if(!strcmp(p, ".."))
			{
				// Go one directory backwards
				p = c+1;
				ad = ad->parent;
				goto lflp;
			}
			for(int i = 0; i < ad->ndirs; i++)
			if(!StrCICompare(p, ad->dirs[i].name))
			{
				// Dir found!
				p = c+1;
				ad = &ad->dirs[i];
				goto lflp;
			}
			return 0; // Dir not found.
		} else {
			// A directory!
			if(!*p) return ad;
			if(!strcmp(p, "."))
				return ad;
			if(!strcmp(p, ".."))
			{
				// Go one directory backwards
				ad = ad->parent;
				return ad;
			}
			for(int i = 0; i < ad->ndirs; i++)
			if(!StrCICompare(p, ad->dirs[i].name))
			{
				// Dir found!
				ad = &ad->dirs[i];
				return ad;
			}
			return 0; // Dir not found.
		}
	}
}

bool BCPReader::loadFile(const char *fn, char **out, int *outsize, int extraBytes)
{
	BCPFile *bf = getFile(fn);
	if(!bf) return 0;
	ExtractFile(bf->id, out, outsize, extraBytes);
	return 1;
}

bool BCPReader::fileExists(const char *fn)
{
	BCPFile *bf = getFile(fn);
	return bf ? 1 : 0;
}

void BCPReader::listFileNames(const char *dn, std::vector<std::string> *gsl)
{
	BCPDirectory *ad = getDirectory(dn);
	if(!ad) return;
	for(int i = 0; i < ad->nfiles; i++)
		if (std::find_if(gsl->begin(), gsl->end(), [&](const std::string& s) { return !StrCICompare(s, ad->files[i].name); }) == gsl->end())
			gsl->push_back(ad->files[i].name);
}

void BCPReader::listDirectories(const char *dn, std::vector<std::string> *gsl)
{
	BCPDirectory *ad = getDirectory(dn);
	if(!ad) return;
	for(int i = 0; i < ad->ndirs; i++)
		if (std::find_if(gsl->begin(), gsl->end(), [&](const std::string& s) { return !StrCICompare(s, ad->dirs[i].name); }) == gsl->end())
			gsl->push_back(ad->dirs[i].name);
}

std::string GetMacName(const char *fn)
{
	if (const char *sh = strrchr(fn, '\\'))
	{
		sh += 1;
		if (strlen(sh) > 31)
		{
			const char *ext = strrchr(sh, '.');
			int mmplen = 31 - (ext ? strlen(ext) : 0) - 1;
			if (mmplen < 0) mmplen = 0;

			return std::string(fn, sh) + std::string(sh, mmplen) + '_' + ext;
		}
	}
	return {};
}

std::string portablePath(std::string p)
{
	for(char& c : p)
		if(c == '\\')
			c = '/';
	return p;
}

// ---- Layered file system ----
//
// The original game (WKB 1.1, FUN_00741790 / FUN_007415c0) mounts data.bcp and every other *.bcp
// in the game folder into one group, and for each file uses the copy with the newest timestamp
// (File::is_newer_than via CompareFileTime; ties keep the first-mounted copy). That is the default
// here too, extended with the game folder's loose data\ directory and mod folders in "mods/".
// A user-defined load order ("modLoadOrder") instead stacks the mods on top of data.bcp, a later mod
// overriding an earlier one. Below all of this come the saved\ folder, the REDATA resources and redata/.

namespace {

uint64_t FileTimeOf(const std::filesystem::path& path)
{
	std::error_code ec;
	auto ft = std::filesystem::last_write_time(path, ec);
	if (ec) return 0;
	auto sys = std::chrono::clock_cast<std::chrono::system_clock>(ft);
	auto ticks = std::chrono::duration_cast<std::chrono::duration<int64_t, std::ratio<1, 10000000>>>(sys.time_since_epoch()).count();
	return (uint64_t)(ticks + 116444736000000000LL); // FILETIME: 100 ns intervals since 1601
}

void AddUniqueName(std::vector<std::string>* gsl, std::string name)
{
	if (std::find_if(gsl->begin(), gsl->end(), [&](const std::string& s) { return !StrCICompare(s, name); }) == gsl->end())
		gsl->push_back(std::move(name));
}

bool HasBcpExtension(const std::filesystem::path& path)
{
	return !StrCICompare(PathToUtf8(path.extension()), ".bcp");
}

struct FileSource {
	std::string description;
	virtual ~FileSource() = default;
	// True if the source has the file; also returns its timestamp (FILETIME)
	virtual bool find(const char* fn, uint64_t& time) = 0;
	virtual bool load(const char* fn, char** out, int* outsize, int extraBytes) = 0;
	virtual void listFiles(const char* dn, std::vector<std::string>* gsl) = 0;
	virtual void listDirectories(const char* dn, std::vector<std::string>* gsl) = 0;
	virtual uint64_t newestTime() = 0;
};

struct BCPSource : FileSource {
	BCPReader reader;
	BCPSource(const std::filesystem::path& path) : reader(path) { description = PathToUtf8(path.filename()); }
	bool find(const char* fn, uint64_t& time) override {
		BCPFile* file = reader.getFile(fn);
		if (!file) return false;
		time = file->time;
		return true;
	}
	bool load(const char* fn, char** out, int* outsize, int extraBytes) override { return reader.loadFile(fn, out, outsize, extraBytes); }
	void listFiles(const char* dn, std::vector<std::string>* gsl) override { reader.listFileNames(dn, gsl); }
	void listDirectories(const char* dn, std::vector<std::string>* gsl) override { reader.listDirectories(dn, gsl); }
	uint64_t newestTime() override {
		uint64_t newest = 0;
		auto walk = [&](auto&& self, BCPDirectory* dir) -> void {
			for (int i = 0; i < dir->nfiles; i++) newest = std::max(newest, dir->files[i].time);
			for (int i = 0; i < dir->ndirs; i++) self(self, &dir->dirs[i]);
		};
		walk(walk, &reader.bcproot);
		return newest;
	}
};

struct DirectorySource : FileSource {
	std::filesystem::path root;
	DirectorySource(std::filesystem::path root, std::string desc) : root(std::move(root)) { description = std::move(desc); }
	std::filesystem::path pathOf(const char* fn) const { return root / PathFromUtf8(portablePath(fn)); }
	bool find(const char* fn, uint64_t& time) override {
		std::error_code ec;
		auto path = pathOf(fn);
		if (!std::filesystem::is_regular_file(path, ec)) return false;
		time = FileTimeOf(path);
		return true;
	}
	bool load(const char* fn, char** out, int* outsize, int extraBytes) override {
		FILE* sf = FsOpen(pathOf(fn), "rb");
		if (!sf) return false;
		fseek(sf, 0, SEEK_END);
		int ss = ftell(sf);
		fseek(sf, 0, SEEK_SET);
		char* mout = (char*)malloc(ss + extraBytes);
		if (!mout) ferr("No mem. left for reading the file \"%s\".", fn);
		fread(mout, ss, 1, sf);
		fclose(sf);
		*out = mout; *outsize = ss;
		return true;
	}
	void list(const char* dn, std::vector<std::string>* gsl, bool directories) {
		std::error_code ec;
		for (const auto& entry : std::filesystem::directory_iterator(pathOf(dn), ec))
			if (directories ? entry.is_directory(ec) : entry.is_regular_file(ec))
				AddUniqueName(gsl, PathToUtf8(entry.path().filename()));
	}
	void listFiles(const char* dn, std::vector<std::string>* gsl) override { list(dn, gsl, false); }
	void listDirectories(const char* dn, std::vector<std::string>* gsl) override { list(dn, gsl, true); }
	uint64_t newestTime() override {
		uint64_t newest = 0;
		std::error_code ec;
		for (const auto& entry : std::filesystem::recursive_directory_iterator(root, ec))
			if (entry.is_regular_file(ec))
				newest = std::max(newest, FileTimeOf(entry.path()));
		return newest;
	}
};

// Sources resolved together: per file, the newest timestamp wins (first source on a tie)
struct FileGroup {
	std::string name;
	std::vector<std::unique_ptr<FileSource>> sources;
	FileSource* findNewest(const char* fn) {
		FileSource* best = nullptr;
		uint64_t bestTime = 0;
		for (auto& src : sources) {
			uint64_t time;
			if (src->find(fn, time) && (!best || time > bestTime)) {
				best = src.get();
				bestTime = time;
			}
		}
		return best;
	}
};

std::vector<FileGroup> g_fileGroups; // lowest priority first
std::vector<ModInfo> g_mods;
bool g_customModOrder = false;

std::filesystem::path ModsPath()
{
	return PathFromUtf8(g_settings.value<std::string>("modsPath", "mods"));
}

void AddModSources(const ModInfo& mod, FileGroup& group)
{
	if (mod.id == "data/") {
		group.sources.push_back(std::make_unique<DirectorySource>(g_gamePath / "data", "data/"));
	}
	else if (mod.id.rfind("mods/", 0) == 0) {
		std::filesystem::path folder = ModsPath() / PathFromUtf8(mod.id.substr(5));
		group.sources.push_back(std::make_unique<DirectorySource>(folder, mod.id));
		std::vector<std::filesystem::path> archives;
		std::error_code ec;
		for (const auto& entry : std::filesystem::directory_iterator(folder, ec))
			if (entry.is_regular_file(ec) && HasBcpExtension(entry.path()))
				archives.push_back(entry.path());
		std::sort(archives.begin(), archives.end());
		for (const auto& archive : archives)
			group.sources.push_back(std::make_unique<BCPSource>(archive));
	}
	else {
		group.sources.push_back(std::make_unique<BCPSource>(g_gamePath / PathFromUtf8(mod.id)));
	}
}

std::vector<ModInfo> DiscoverMods()
{
	std::vector<ModInfo> mods;
	std::error_code ec;

	// Archives next to data.bcp, as loaded by the original game
	std::vector<std::string> archives;
	for (const auto& entry : std::filesystem::directory_iterator(g_gamePath, ec))
		if (entry.is_regular_file(ec) && HasBcpExtension(entry.path()) && StrCICompare(PathToUtf8(entry.path().filename()), "data.bcp"))
			archives.push_back(PathToUtf8(entry.path().filename()));
	std::sort(archives.begin(), archives.end(), [](const std::string& a, const std::string& b) { return StrCICompare(a, b) < 0; });
	for (const auto& archive : archives)
		mods.push_back({ archive, archive, "", "archive in the game folder" });

	if (g_allowDataDirectory && std::filesystem::is_directory(g_gamePath / "data", ec))
		mods.push_back({ "data/", "data (loose files)", "", "data folder in the game folder" });

	std::vector<std::string> folders;
	for (const auto& entry : std::filesystem::directory_iterator(ModsPath(), ec))
		if (entry.is_directory(ec))
			folders.push_back(PathToUtf8(entry.path().filename()));
	std::sort(folders.begin(), folders.end(), [](const std::string& a, const std::string& b) { return StrCICompare(a, b) < 0; });
	for (const auto& folder : folders) {
		ModInfo mod{ "mods/" + folder, folder, "", "mod folder" };
		std::ifstream manifest(ModsPath() / PathFromUtf8(folder) / "mod.json");
		if (manifest.good()) {
			try {
				nlohmann::json json;
				manifest >> json;
				mod.name = json.value<std::string>("name", mod.name);
				mod.description = json.value<std::string>("description", "");
			}
			catch (const nlohmann::json::exception& exc) {
				printf("WARNING: Invalid mod.json in mod %s: %s\n", folder.c_str(), exc.what());
			}
		}
		mods.push_back(std::move(mod));
	}
	return mods;
}

FileSource* FindFileSource(const char* fn)
{
	for (auto group = g_fileGroups.rbegin(); group != g_fileGroups.rend(); ++group)
		if (FileSource* src = group->findNewest(fn))
			return src;
	return nullptr;
}

} // namespace

void InitFileSystem()
{
	g_fileGroups.clear();
	std::vector<ModInfo> mods = DiscoverMods();

	std::set<std::string> disabled;
	if (auto it = g_settings.find("disabledMods"); it != g_settings.end() && it->is_array())
		for (const auto& id : *it)
			if (id.is_string()) disabled.insert(id.get<std::string>());
	for (auto& mod : mods)
		mod.enabled = !disabled.count(mod.id);

	FileGroup base{ "data.bcp" };
	base.sources.push_back(std::make_unique<BCPSource>(std::filesystem::path("data.bcp")));

	auto orderIt = g_settings.find("modLoadOrder");
	g_customModOrder = orderIt != g_settings.end() && orderIt->is_array();
	if (g_customModOrder) {
		// Mods named in the saved order first, in that order; mods found since keep discovery order at the end
		std::vector<ModInfo> ordered;
		for (const auto& id : *orderIt) {
			if (!id.is_string()) continue;
			auto mod = std::find_if(mods.begin(), mods.end(), [&](const ModInfo& m) { return m.id == id.get<std::string>(); });
			if (mod != mods.end()) {
				ordered.push_back(std::move(*mod));
				mods.erase(mod);
			}
		}
		for (auto& mod : mods)
			ordered.push_back(std::move(mod));
		mods = std::move(ordered);

		g_fileGroups.push_back(std::move(base));
		for (auto& mod : mods) {
			FileGroup group{ mod.id };
			AddModSources(mod, group);
			for (auto& src : group.sources) mod.newestTime = std::max(mod.newestTime, src->newestTime());
			if (mod.enabled)
				g_fileGroups.push_back(std::move(group));
		}
	}
	else {
		for (auto& mod : mods) {
			FileGroup scratch;
			AddModSources(mod, scratch);
			for (auto& src : scratch.sources) mod.newestTime = std::max(mod.newestTime, src->newestTime());
			if (mod.enabled)
				for (auto& src : scratch.sources)
					base.sources.push_back(std::move(src));
		}
		// Shown in the menu in order of their newest file: the effective order when files compete by timestamp
		std::stable_sort(mods.begin(), mods.end(), [](const ModInfo& a, const ModInfo& b) { return a.newestTime < b.newestTime; });
		g_fileGroups.push_back(std::move(base));
	}
	g_mods = std::move(mods);

	printf("File system: data.bcp + %zu mod(s), %s order\n", g_mods.size(), g_customModOrder ? "custom" : "original (newest file wins)");
	for (const auto& mod : g_mods)
		printf("  [%c] %s (%s)\n", mod.enabled ? 'x' : ' ', mod.id.c_str(), mod.kind.c_str());
}

const std::vector<ModInfo>& GetMods()
{
	return g_mods;
}

bool IsCustomModLoadOrder()
{
	return g_customModOrder;
}

void ApplyModConfiguration(const std::vector<ModInfo>& mods, bool customOrder)
{
	nlohmann::json disabled = nlohmann::json::array(), order = nlohmann::json::array();
	for (const auto& mod : mods) {
		if (!mod.enabled) disabled.push_back(mod.id);
		order.push_back(mod.id);
	}
	g_settings["disabledMods"] = disabled;
	if (customOrder)
		g_settings["modLoadOrder"] = order;
	else
		g_settings.erase("modLoadOrder");
	SaveSettings();
	InitFileSystem();
}

std::string DescribeFileSource(const char* fn)
{
	FileSource* src = FindFileSource(fn);
	return src ? src->description : std::string();
}

void LoadBCP(const char *fn)
{
	InitFileSystem();
}

void LoadFile(const char *fn, char **out, int *outsize, int extraBytes)
{
	if (FileSource* src = FindFileSource(fn))
		if (src->load(fn, out, outsize, extraBytes))
			return;

	// Not in data.bcp or a mod. Find it in the "saved" and "redata" directories.
	const auto u8fn = PathFromUtf8(portablePath(fn));
	FILE *sf = FsOpen(g_gamePath / "saved" / u8fn, "rb");
#ifdef _WIN32
	if (!sf)
		if(HRSRC h = FindResourceA(NULL, fn, "REDATA"))
			if (HGLOBAL g = LoadResource(NULL, h))
			{
				void *p = LockResource(g);
				uint32_t siz = SizeofResource(NULL, h);
				void *mout = malloc(siz+extraBytes);
				memcpy(mout, p, siz);
				*out = (char*)mout;
				*outsize = siz;
				return;
			}
#endif
	if (!sf)
	{
		sf = FsOpen("redata" / u8fn, "rb");
	}
	if (!sf && g_macFileNamesFallbackEnabled)
		if (auto nfn = GetMacName(fn); !nfn.empty())
		{
			LoadFile(nfn.c_str(), out, outsize, extraBytes);
			return;
		}
	if(!sf)
		ferr("The file \"%s\" could not be found in the game directories/archives (data.bcp, saved, redata, data, ...).\n\nIf it is the first time you use this program, be sure that you have configured the GAME_DIR parameter in settings.txt . See help.htm for more details.", fn);
		//ferr("File cannot be found in data.bcp, in the \"saved\" folder and the \"redata\" directory.");
	fseek(sf, 0, SEEK_END);
	int ss = ftell(sf);
	fseek(sf, 0, SEEK_SET);
	char *mout = (char*)malloc(ss+extraBytes);
	if(!mout) ferr("No mem. left for reading a file from saved dir.");
	fread(mout, ss, 1, sf);
	fclose(sf);
	*out = mout; *outsize = ss;
	return;
}

int FileExists(const char *fn)
{
	if (FindFileSource(fn))
		return 1;

	const auto u8fn = PathFromUtf8(portablePath(fn));

	// Not in data.bcp or a mod. Find it in the "saved" and "redata" folder.
	if (std::filesystem::exists(g_gamePath / "saved" / u8fn))
		return 1;

#ifdef _WIN32
	if (FindResourceA(NULL, fn, "REDATA"))
		return 1;
#endif

	if (std::filesystem::exists("redata" / u8fn))
		return 1;

	if (g_macFileNamesFallbackEnabled)
		if (auto nfn = GetMacName(fn); !nfn.empty()) {
			auto r = FileExists(nfn.c_str());
			return r;
		}
	return 0;
}

void FindFiles(const std::filesystem::path& filter, std::vector<std::string>* gsl)
{
#ifdef _WIN32
	HANDLE hf; WIN32_FIND_DATAW fnd;
	hf = FindFirstFileW(filter.c_str(), &fnd);
	if(hf == INVALID_HANDLE_VALUE) return;
	do {
		if (!(fnd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
			auto upath = PathToUtf8(std::filesystem::path(fnd.cFileName));
			if (std::find_if(gsl->begin(), gsl->end(), [&](const std::string& s) { return !StrCICompare(s, upath); }) == gsl->end()) {
				gsl->push_back(std::move(upath));
			}
		}
	} while(FindNextFileW(hf, &fnd));
#endif
}

std::vector<std::string>* ListFiles(const char *dn, std::vector<std::string>* gsl)
{
	if(!gsl) gsl = new std::vector<std::string>;

	for (auto& group : g_fileGroups)
		for (auto& src : group.sources)
			src->listFiles(dn, gsl);

	FindFiles(g_gamePath / "saved" / dn / "*.*", gsl);

	return gsl;
}

std::vector<std::string>* ListDirectories(const char *dn)
{
	std::vector<std::string>* gsl = new std::vector<std::string>;
	for (auto& group : g_fileGroups)
		for (auto& src : group.sources)
			src->listDirectories(dn, gsl);
	return gsl;
}

BCPWriter::WDirectory* BCPWriter::WDirectory::addDir(const char *name)
{
	WDirectory* nwd = new WDirectory;
	nwd->writer = writer;
	nwd->name = name;
	this->dirs.push_back(nwd);
	return nwd;
}

void BCPWriter::WDirectory::insertFile(uint32_t id, const char *name)
{
	files.emplace_back();
	BCPFile* wf = &files.back();
	wf->id = id;
	wf->name = name;
	wf->time = 0;
}

BCPWriter::WDirectory::~WDirectory()
{
	for (size_t i = 0; i < dirs.size(); i++)
		delete dirs[i];
}

BCPWriter::BCPWriter(const char *fn)
{
	file = fopen(fn, "wb");
	if (!file) ferr("Failed to create BCP for writing.");

	// Every mod is owned by BC
	fwrite("PAK File 2.01 (c) Black Cactus Games Limited \xAD\x91\xE4", 48, 1, file);

	// Placeholder, will be overwritten at the end
	write32(0); write32(0);

	root.writer = this;
}

uint32_t BCPWriter::createFile(void *pnt, uint32_t siz)
{
	uint id = (uint)ftable.size();
	ftable.emplace_back();
	BCPFileData* e = &ftable.back();
	e->offset = ftell(file);
	e->size = siz;
	e->unk = 0;

	unsigned char *lzws = (unsigned char*)malloc(MEM_REQ);
	uint slz = siz + COMPRESS_OVERRUN; //4 + siz + (siz / 16) * 2 + 4;
	void *cplz = malloc(slz);
	compress(COMPRESS_ACTION_COMPRESS, lzws, (unsigned char*)pnt, siz, (unsigned char*)cplz, &slz);
	free(lzws);

	if (slz < siz) {
		e->form = 4;
		fwrite(cplz, slz, 1, file);
	}
	else {
		e->form = 1;
		fwrite(pnt, siz, 1, file);
	}
	free(cplz);

	e->endos = ftell(file);
	return id;
}

void BCPWriter::finalize()
{
	uint ftoff = ftell(file);
	write32((uint)ftable.size());
	for (uint i = 0; i < (uint)ftable.size(); i++)
	{
		BCPFileData& e = ftable[i];
		write32(e.offset);
		write32(e.endos);
		write32(e.size);
		write32(e.unk);
		write32(e.form);
	}
	root.write();
	uint eof = ftell(file);
	fseek(file, 48, SEEK_SET);
	write32(ftoff);
	write32(eof - ftoff);
	fclose(file);
}

void BCPWriter::WDirectory::write()
{
	writer->write32((uint)files.size());
	for (uint i = 0; i < (uint)files.size(); i++)
	{
		BCPFile &f = files[i];
		writer->write32(f.id);
		writer->write32(f.time & 0xFFFFFFFF);
		writer->write32(f.time >> 32);
		uint sl = f.name.size();
		writer->write8(sl);
		for (uint j = 0; j < sl; j++)
			writer->write16((unsigned char)f.name[j]);
	}
	writer->write32((uint)dirs.size());
	for (uint i = 0; i < (uint)dirs.size(); i++)
	{
		WDirectory *d = dirs[i];
		//WDirectory *d = dirs.at(i);
		uint sl = d->name.size();
		writer->write8(sl);
		for (uint j = 0; j < sl; j++)
			writer->write16((unsigned char)d->name[j]);
		dirs[i]->write();
	}
}

void BCPWriter::copyFile(const char *fn)
{
	BCPWriter::WDirectory *d = &root;
	std::string fncopy = fn;
	char *p = const_cast<char*>(fncopy.data());
	while (char *s = strchr(p, '\\')) {
		*s = 0;
		for (uint i = 0; i < (uint)d->dirs.size(); i++)
			if (!StrCICompare(d->dirs[i]->name, p)) {
				d = d->dirs[i];
				goto dirfnd;
			}
		d = d->addDir(p);
	dirfnd:
		p = s + 1;
	}
	char *fdat; int fsiz;
	LoadFile(fn, &fdat, &fsiz);
	uint id = createFile(fdat, fsiz);
	d->insertFile(id, p);
}
