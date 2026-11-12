// wkbai - which build this is, for multiplayer's build check
// Licensed under the GNU General Public License 3

#include "BuildIdentity.h"
#include "buildinfo.h"
#include "matchmaking/Matchmaking.h"
#include <cstdint>
#include <cstring>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <bcrypt.h>
#endif

#ifndef WKBAI_OFFICIAL_BUILD
#define WKBAI_OFFICIAL_BUILD 0
#endif

namespace BuildIdentity {

const char* Commit() { return WKBAI_COMMIT; }
bool Modified() { return WKBAI_MODIFIED != 0; }
bool Official() { return WKBAI_OFFICIAL_BUILD != 0; }

std::string Describe()
{
	return std::string(Commit()) + (Modified() ? " (modified)" : "");
}

namespace {
#ifdef _WIN32
	// the exe's code in memory: where its .text section is, and the relocated places in it
	// (offset, size) that the loader changes with the address the exe was put at
	struct CodeImage {
		const uint8_t* text = nullptr;
		size_t size = 0;
		std::vector<std::pair<uint32_t, uint8_t>> relocs;
	};
	const CodeImage& codeImage()
	{
		static const CodeImage image = [] {
			CodeImage img;
			const uint8_t* base = (const uint8_t*)GetModuleHandleW(nullptr);
			const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)base;
			const IMAGE_NT_HEADERS* nt = (const IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
			const IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
			uint32_t textRva = 0;
			for (int i = 0; i < nt->FileHeader.NumberOfSections; i++)
				if (!std::memcmp(sec[i].Name, ".text", 6)) {
					textRva = sec[i].VirtualAddress;
					img.text = base + textRva;
					img.size = sec[i].Misc.VirtualSize;
				}
			const IMAGE_DATA_DIRECTORY& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
			for (uint32_t at = 0; img.text && at + sizeof(IMAGE_BASE_RELOCATION) <= dir.Size;) {
				const IMAGE_BASE_RELOCATION* block = (const IMAGE_BASE_RELOCATION*)(base + dir.VirtualAddress + at);
				if (block->SizeOfBlock < sizeof(IMAGE_BASE_RELOCATION))
					break;
				const uint16_t* entries = (const uint16_t*)(block + 1);
				const size_t n = (block->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / 2;
				for (size_t i = 0; i < n; i++) {
					const int type = entries[i] >> 12;
					const uint32_t rva = block->VirtualAddress + (entries[i] & 0xFFF);
					const uint8_t size = type == IMAGE_REL_BASED_DIR64 ? 8 : type == IMAGE_REL_BASED_HIGHLOW ? 4 : 0;
					if (size && rva >= textRva && rva + size <= textRva + img.size)
						img.relocs.push_back({ rva - textRva, size });
				}
				at += block->SizeOfBlock;
			}
			return img;
		}();
		return image;
	}

	std::string sha256(const std::string& prefix, const uint8_t* data, size_t size)
	{
		BCRYPT_ALG_HANDLE alg = nullptr;
		BCRYPT_HASH_HANDLE hash = nullptr;
		unsigned char digest[32];
		std::string hex;
		if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0)
			return hex;
		if (BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) == 0) {
			BCryptHashData(hash, (PUCHAR)prefix.data(), (ULONG)prefix.size(), 0);
			for (size_t at = 0; at < size; at += 1u << 30)
				BCryptHashData(hash, (PUCHAR)(data + at), (ULONG)std::min<size_t>(size - at, 1u << 30), 0);
			if (BCryptFinishHash(hash, digest, sizeof(digest), 0) == 0) {
				static const char* const digits = "0123456789abcdef";
				for (unsigned char c : digest) {
					hex += digits[c >> 4];
					hex += digits[c & 15];
				}
			}
			BCryptDestroyHash(hash);
		}
		BCryptCloseAlgorithmProvider(alg, 0);
		return hex;
	}

	// the code as it is in memory now, its relocated places zeroed
	std::string hashCode(const std::string& prefix)
	{
		const CodeImage& img = codeImage();
		if (!img.text || !img.size)
			return std::string();
		std::vector<uint8_t> copy(img.text, img.text + img.size);
		for (const auto& [offset, size] : img.relocs)
			std::memset(copy.data() + offset, 0, size);
		return sha256(prefix, copy.data(), copy.size());
	}
#else
	std::string hashCode(const std::string&) { return std::string(); }
#endif
}

const std::string& CodeHash()
{
	static const std::string hash = hashCode(std::string());
	return hash;
}

std::string ChallengeAnswer(const std::string& challenge)
{
	if (challenge.empty())
		return std::string();
	return hashCode("wkbai build challenge " + challenge + "\n");
}

nlohmann::json Json()
{
	return { { "net", MatchmakingGameVersion() }, { "commit", Commit() }, { "modified", Modified() },
		{ "official", Official() }, { "code", CodeHash() } };
}

std::string Compare(int policy, const nlohmann::json& host, const nlohmann::json& guest, bool answerOk)
{
	if (!guest.is_object() || !guest.contains("net"))
		return "Your wkbai is older than the host's (it doesn't say which build it is); the host runs " + host.value("commit", std::string("?"));
	const std::string guestDesc = guest.value("commit", std::string("?")) + (guest.value("modified", false) ? " (modified)" : "");
	const std::string hostDesc = host.value("commit", std::string("?")) + (host.value("modified", false) ? " (modified)" : "");
	if (guest.value("net", std::string()) != host.value("net", std::string()))
		return "Your wkbai can't play with the host's (network version " + guest.value("net", std::string("?")) + ", the host's "
			+ host.value("net", std::string("?")) + ")";
	if (policy >= SAME_VERSION) {
		const bool sameCommit = guest.value("commit", std::string()) == host.value("commit", std::string()) && host.value("commit", std::string()) != "unknown";
		const bool sameCode = !host.value("code", std::string()).empty() && guest.value("code", std::string()) == host.value("code", std::string());
		const bool clean = !guest.value("modified", true) && !host.value("modified", true);
		if (!sameCommit)
			return "This game needs the host's wkbai version: yours is " + guestDesc + ", the host's " + hostDesc;
		if (!(clean || sameCode))
			return "This game needs the host's wkbai version: yours is built from the same source (" + guestDesc
				+ ") but its program code differs from the host's";
	}
	if (policy >= IDENTICAL && !answerOk)
		return "This game needs a build identical to the host's (" + hostDesc + "); your copy's program code differs";
	return std::string();
}

}
