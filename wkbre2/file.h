// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

struct BCPFileData
{
	uint32_t offset, endos, size, unk, form;
};

struct BCPFile
{
	uint32_t id;
	uint64_t time;
	std::string name;
};

class BCPWriter
{
private:
	FILE * file;
	std::vector<BCPFileData> ftable;

	void write8(uint8_t n) { fwrite(&n, 1, 1, file); }
	void write16(uint16_t n) { fwrite(&n, 2, 1, file); }
	void write32(uint32_t n) { fwrite(&n, 4, 1, file); }

public:
	struct WDirectory
	{
		std::vector<WDirectory*> dirs;
		std::vector<BCPFile> files;
		std::string name;
		BCPWriter *writer;

		WDirectory* addDir(const char *name);
		void insertFile(uint32_t id, const char *name);
		~WDirectory();

		void write();
	};
	WDirectory root;

	BCPWriter(const char *fn);
	uint32_t createFile(void *pnt, uint32_t siz);
	void finalize();
	void copyFile(const char *fn);
};

extern std::filesystem::path g_gamePath;
// Where the player's own files go (save games), so the game folder is never written to:
// "userPath" in wkconfig.json, else %APPDATA%\wkbai (copied from %APPDATA%\wkbre2 the first time). Its "saved" folder is searched before the game's.
extern std::filesystem::path g_userPath;
extern bool g_allowDataDirectory, g_macFileNamesFallbackEnabled;

// A content source mounted on top of data.bcp
struct ModInfo {
	std::string id;          // "<name>.bcp" (game folder archive), "data/" (loose files of the game folder) or "mods/<folder>"
	std::string name;        // display name (from mod.json if present)
	std::string description; // from mod.json
	std::string kind;        // human-readable source type
	bool enabled = true;
	uint64_t newestTime = 0; // newest file timestamp (FILETIME), orders the list in original mode
	// From mod.json: its own id (others name it in "requires"; default: the folder name), its
	// version, the mods it needs, and the oldest mod format ("wkbai", formerly "wkbre2") it works with
	std::string modId, version;
	std::vector<std::string> dependencies; // mod.json "requires"
	int apiVersion = 0;
	// Why it may not work, "" if nothing: a needed mod missing or disabled, a newer mod format
	std::string problem;
};

// The mod format this build supports (mod.json "wkbai"; "wkbre2" before the rename): 1 = folders and archives overriding any
// game file, mod.json, Lua\FrontEnd\*.lua and Lua\InGame\*.lua extending the UI scripts;
// 2 = also .png/.dds/.ktx2 replacing a .tga/.pcx texture of any size (Bitmap::replacementFile)
constexpr int MOD_API_VERSION = 2;

// Mounts data.bcp and the enabled mods according to wkconfig.json ("disabledMods", "modLoadOrder", "modsPath").
// Without "modLoadOrder" files resolve like the original game: data.bcp and all mods form one group
// and each file comes from the source with the newest timestamp. With it, mods are layered in that
// order on top of data.bcp and a later mod overrides an earlier one.
void InitFileSystem();
// Discovered mods in their current load order (lowest priority first)
const std::vector<ModInfo>& GetMods();
bool IsCustomModLoadOrder();
// Saves the mods' enabled state and, if customOrder, their order to wkconfig.json, then remounts
void ApplyModConfiguration(const std::vector<ModInfo>& mods, bool customOrder);
// Which archive or folder a file is loaded from ("" if it isn't in data.bcp or a mod)
std::string DescribeFileSource(const char* fn);

// UTF-8 conversions between std::string and paths. In C++20, path::u8string() returns
// std::u8string and std::filesystem::u8path is deprecated.
inline std::filesystem::path PathFromUtf8(const std::string& str) {
	return std::filesystem::path(std::u8string(str.begin(), str.end()));
}
inline std::string PathToUtf8(const std::filesystem::path& path) {
	std::u8string u8 = path.u8string();
	return std::string(u8.begin(), u8.end());
}

void LoadBCP(const char *fn); // kept for existing callers; mounts everything via InitFileSystem()
void LoadFile(const char *fn, char **out, int *outsize, int extraBytes = 0);
//void TestBCP();
int FileExists(const char *fn);
// The timestamp (FILETIME) of the copy LoadFile would read, 0 if none: a changed file in a mod
// folder or redata gets a new one (hot reload); files in archives keep theirs
uint64_t FileModificationTime(const char* fn);
std::vector<std::string> *ListFiles(const char *dn, std::vector<std::string> *gsl = nullptr);
std::vector<std::string> *ListDirectories(const char *dn);
//void SetGameDir();