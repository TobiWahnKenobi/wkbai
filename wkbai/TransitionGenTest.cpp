// wkbai - headless test of the generated ground transitions (TransitionGen.h) and the BC7 encoder
// (gfx/Bc7Encode.h): a game atlas through the encoder and bcdec back (PSNR), the database with
// its generated groups (corners, files, BC7 with mips), no second making from the same sources,
// made again when the stamp is stale, the progress while they are made (none from the cache), and the solver's library taking them (a grass to rock
// patch painted without a strip between). Test "Generated ground transitions" in test.cpp.
// Licensed under the GNU General Public License 3

#include "TransitionGen.h"
#include "TrnTextureDb.h"
#include "MapMaterials.h"
#include "file.h"
#include "gfx/bitmap.h"
#include "gfx/Bc7Encode.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {
	namespace fs = std::filesystem;
}

void Test_GeneratedTransitions()
{
	LoadBCP("data.bcp");
	bool all = true;
	auto check = [&](const char* what, bool ok, const std::string& detail) {
		printf("GENTRANS %s %d %s\n", what, (int)ok, detail.c_str());
		fflush(stdout);
		all = all && ok;
	};
	// 1. the encoder: grass1.pcx's atlas (RGBA) into BC7 and back
	{
		Bitmap src = Bitmap::loadBitmapExact("Maps\\Map_Textures\\grass1.pcx").convertToR8G8B8A8();
		const std::vector<uint8_t> blocks = Bc7::Encode(src.pixels.data(), src.width, src.height);
		// decoded by bcdec (Bitmap's BC7 decoding)
		Bitmap enc;
		enc.width = src.width;
		enc.height = src.height;
		enc.format = BMFORMAT_BC7;
		enc.pixels.resize(blocks.size());
		std::memcpy(enc.pixels.data(), blocks.data(), blocks.size());
		enc.levels = { 0 };
		const Bitmap dec = enc.convertToR8G8B8A8();
		const uint8_t* back = dec.pixels.data();
		double se = 0;
		int alphaLow = 0;
		for (size_t i = 0; i < (size_t)src.width * src.height; i++) {
			for (int c = 0; c < 3; c++) {
				const double d = (double)src.pixels[i * 4 + c] - back[i * 4 + c];
				se += d * d;
			}
			alphaLow += back[i * 4 + 3] < 254;
		}
		const double mse = se / ((double)src.width * src.height * 3);
		const double psnr = mse > 0 ? 10 * std::log10(255.0 * 255.0 / mse) : 99;
		char buf[96];
		snprintf(buf, sizeof(buf), "grass1.pcx %d x %d: %.2f dB, %d texels below alpha 254", src.width, src.height, psnr, alphaLow);
		check("bc7 quality", psnr > 34.0 && alphaLow == 0, buf);
	}
	// 2. the database with its generated groups
	const fs::path dir = g_userPath / "saved" / "Maps" / "Map_Textures" / "Generated";
	std::error_code ec;
	fs::remove_all(dir, ec);
	// the progress while they are made: each reported before it is made, counted 1, 2, ... to the
	// total, the total the number made (TransitionGen::SetProgressObserver)
	struct Seen { int done, total; };
	std::vector<Seen> seen;
	TransitionGen::SetProgressObserver([&](int done, int total, const std::string&) { seen.push_back({ done, total }); });
	TerrainTextureDatabase db;
	db.load("Maps\\Map_Textures\\All_Textures.dat");
	{
		bool rising = !seen.empty();
		for (size_t i = 0; i < seen.size(); i++)
			rising = rising && seen[i].done == (int)i + 1 && seen[i].total == (int)seen.size();
		check("progress", rising, std::to_string(seen.size()) + " reports, " + (seen.empty() ? std::string("none")
			: std::to_string(seen.front().done) + " of " + std::to_string(seen.front().total) + " to " + std::to_string(seen.back().done) + " of " + std::to_string(seen.back().total)));
	}
	const size_t madeFirst = seen.size();
	int generated = 0, cornersOk = 0, triples = 0;
	std::string names;
	for (const auto& [name, g] : db.groups) {
		if (!g || g->textures.empty() || g->textures.begin()->second->genCorners[0].empty())
			continue;
		generated++;
		names += name + " ";
		// a pair: 8 pictures of its two groups; a triple ("A-B-C"): 12 of all three, each of its
		// three on two corners side by side and across
		const size_t n = std::count(name.begin(), name.end(), '-') + 1;
		bool ok = g->textures.size() == (n == 2 ? 8u : 12u);
		std::set<std::string> layouts;
		for (const auto& [id, t] : g->textures) {
			const auto& c = t->genCorners;
			const std::set<std::string> distinct(c.begin(), c.end());
			ok = ok && !c[3].empty() && distinct.size() == n;
			if (n == 3)
				for (int k = 0; k < 4; k++)
					for (int m = k + 1; m < 4; m++)
						if (c[k] == c[m])
							layouts.insert(c[k] + (m - k == 2 ? " across" : " side"));
		}
		ok = ok && (n == 2 || layouts.size() == 6);
		triples += n == 3;
		cornersOk += ok;
	}
	check("progress total", (int)madeFirst == generated, std::to_string(madeFirst) + " reported, " + std::to_string(generated) + " groups generated");
	check("groups", generated >= 9 && triples >= 1 && cornersOk == generated, std::to_string(generated) + " generated (" + std::to_string(triples) + " triples): " + names);
	// the files: BC7 with mips, heights 256 x 256
	{
		const fs::path dds = dir / "GRASS1-ROCK1.dds", bump = dir / "GRASS1-ROCK1_BumpMap.png";
		Bitmap d = Bitmap::loadBitmapExact(PathToUtf8(dds).c_str());
		Bitmap b = Bitmap::loadBitmap("Maps\\Map_Textures\\Generated\\GRASS1-ROCK1_BumpMap.pcx");
		check("files", fs::exists(dds) && fs::exists(bump) && d.bcKind() == 7 && d.levels.size() >= 7 && b.width == 256,
			"bc" + std::to_string(d.bcKind()) + ", " + std::to_string(d.width) + " px, " + std::to_string(d.levels.size()) + " levels; heights " + std::to_string(b.width));
		// the CPU path takes the DDS for the .pcx name and decodes it
		Bitmap cpu = Bitmap::loadBitmap("Maps\\Map_Textures\\Generated\\GRASS1-ROCK1.pcx");
		check("cpu decode", cpu.format == BMFORMAT_R8G8B8A8 && cpu.width == d.width, std::to_string(cpu.width) + " px RGBA");
	}
	// 3. a second load: nothing made again (the files' times unchanged); a stale stamp: made again
	{
		const fs::path dds = dir / "GRASS1-ROCK1.dds";
		const auto t0 = fs::last_write_time(dds, ec);
		seen.clear();
		TerrainTextureDatabase db2;
		db2.load("Maps\\Map_Textures\\All_Textures.dat");
		check("cached", fs::last_write_time(dds, ec) == t0, "the second load left the pictures alone");
		check("no progress when cached", seen.empty(), std::to_string(seen.size()) + " reports");
		std::ofstream(dir / "GRASS1-ROCK1.json") << "{}";
		TerrainTextureDatabase db3;
		seen.clear();
		db3.load("Maps\\Map_Textures\\All_Textures.dat");
		check("remade", fs::last_write_time(dds, ec) != t0, "a stale stamp: made again");
		check("progress for the one", seen.size() == 1 && seen[0].done == 1 && seen[0].total == 1, std::to_string(seen.size()) + " reports");
	}
	TransitionGen::SetProgressObserver(nullptr);
	printf("GENTRANS all %d\n", (int)all);
}
