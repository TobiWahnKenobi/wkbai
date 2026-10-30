// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#include "terrain.h"
#include "file.h"
#include "util/util.h"
#include "util/GSFileParser.h"
#include "util/StriCompare.h"
#include "gfx/bitmap.h"
#include <algorithm>
#include <array>
#include <map>
#include <cstring>
#include <fstream>

float Terrain::getHeight(float ipx, float ipy) const {
	//int tx = floorf(x / 5) + edge, tz = floorf(z / 5) + edge;
	//if (tx < 0 || tx >= width || tz < 0 || tz >= height)
	//	return 0.0f;
	//return getVertex(tx, tz);
	float x = edge + ipx / 5.0f, y = edge + ipy / 5.0f;
	if (x < 0) x = 0; if (x >= width) x = width - 0.001f;
	if (y < 0) y = 0; if (y >= height) y = height - 0.001f;
	float h1 = getVertex((int)x, (int)y), h2 = getVertex((int)x + 1, (int)y);
	float h3 = getVertex((int)x, (int)y + 1), h4 = getVertex((int)x + 1, (int)y + 1);
	int rx = (int)x; int ry = (int)y;
	float qx = x - rx, qy = y - ry;
	float h5 = h1 * (1.0f - qx) + h2 * qx;
	float h6 = h3 * (1.0f - qx) + h4 * qx;
	float hr = h5 * (1.0f - qy) + h6 * qy;
	return hr;
}

float Terrain::getHeightEx(float x, float z, bool floatOnWater) const
{
	float h = getHeight(x, z);
	if (floatOnWater) {
		int ix = (int)(edge + x / 5.0f);
		int iz = (int)(edge + z / 5.0f);
		if (ix >= 0 && ix < width && iz >= 0 && iz < height) {
			if (const Tile* tile = getTile(ix, iz)) {
				if (tile->fullOfWater && tile->waterLevel > h)
					h = tile->waterLevel;
			}
		}
	}
	return h;
}

Vector3 Terrain::getNormal(int x, int z) const {
	//Vector3 nrm(0,0,0);
	//float h = getVertex(x, z);
	//if (x > 0 && z > 0)
	//	nrm += Vector3(h - getVertex(x - 1, z), 1, h - getVertex(x, z - 1)).normal();
	//if (x < width && z > 0)
	//	nrm += Vector3(h - getVertex(x + 1, z), 1, h - getVertex(x, z - 1)).normal();
	//if (x < width && z < height)
	//	nrm += Vector3(h - getVertex(x + 1, z), 1, h - getVertex(x, z + 1)).normal();
	//if (x > 0 && z < height)
	//	nrm += Vector3(h - getVertex(x - 1, z), 1, h - getVertex(x, z + 1)).normal();
	//return nrm.normal();
	Vector3 nrm(0, 0, 0);
	float h = getVertex(x, z);

	float a, b, c, d;
	if (z > 0)		a = getVertex(x, z - 1) - h;
	if (x > 0)		b = getVertex(x - 1, z) - h;
	if (z < height)	c = getVertex(x, z + 1) - h;
	if (x < width)	d = getVertex(x + 1, z) - h;

	if (x > 0 && z > 0)				nrm += Vector3(b , 5,  a);
	if (x > 0 && z < height)		nrm += Vector3(b , 5, -c);
	if (x < width && z < height)	nrm += Vector3(-d, 5, -c);
	if (x < width && z > 0)			nrm += Vector3(-d, 5,  a);
	return nrm.normal();

}

void Terrain::createEmpty(int newWidth, int newHeight) {
	width = newWidth; height = newHeight; edge = 0;
	scale = 1.0f;
	fogColor = 0x9FC5E6;
	sunColor = -1;
	sunVector = Vector3(1, 1, 1);
}

int Terrain::GetMaxBits(int x)
{
	for (int i = 1; i < 32; i++)
		if ((1 << i) > x) // > sign (not >=) intentional!
			return i;
	//assert(0 & (int)"no max bits found");
	return 0;
}

void Terrain::readFromFile(const char* filename)
{
	const char* ext = strrchr(filename, '.');
	if (!StrCICompare(ext, ".bcm"))
		readBCM(filename);
	else
		readSNR(filename);
}

void Terrain::readBCM(const char * filename) {
	char *fcnt, *fp; int fsize;

	LoadFile(filename, &fcnt, &fsize);
	fp = fcnt + 12;
	width = *(uint32_t*)fp; fp += 4;
	height = *(uint32_t*)fp; fp += 4;
	edge = *(uint32_t*)fp; fp += 4;
	uint16_t strs1 = *(uint16_t*)fp / 2; fp += 2;
	texDbPath = std::wstring((wchar_t*)fp, strs1);  fp += strs1 * 2;
	scale = *(float*)fp; fp += 4;
	sunColor = (*(uint8_t*)fp << 16) | ((*(uint8_t*)(fp + 4)) << 8) | (*(uint8_t*)(fp + 8)); fp += 12;
	sunVector.x = *(float*)fp; sunVector.y = *(float*)(fp + 4); sunVector.z = *(float*)(fp + 8); fp += 12;
	fogColor = (*(uint8_t*)fp << 16) | ((*(uint8_t*)(fp + 4)) << 8) | (*(uint8_t*)(fp + 8)); fp += 12;
	uint16_t strs2 = *(uint16_t*)fp / 2; fp += 2;
	skyTextureDirectory = std::wstring((wchar_t*)fp, strs2); fp += strs2 * 2;

	std::string ctexDbPath((uint16_t*)texDbPath.data(), (uint16_t*)texDbPath.data()+strs1);
	texDb.load(ctexDbPath.c_str());
	texDb.translate(texDb.getLTTTPath(filename).c_str());

	int numVerts = (width + 1)*(height + 1);
	vertices.resize(numVerts);

	// 128x128 RGB minimap of the playable area
	bcmMinimapOffset = (size_t)((const char*)fp - (const char*)fcnt);
	minimapWidth = minimapHeight = 128;
	minimapRGBA.resize(128 * 128 * 4);
	for (int i = 0; i < 128 * 128; i++) {
		minimapRGBA[i * 4 + 0] = (uint8_t)fp[i * 3 + 0];
		minimapRGBA[i * 4 + 1] = (uint8_t)fp[i * 3 + 1];
		minimapRGBA[i * 4 + 2] = (uint8_t)fp[i * 3 + 2];
		minimapRGBA[i * 4 + 3] = 255;
	}

	BitReader br(fp + 0xC000);

	// Lakes
	bcmLakesBitOffset = (uint64_t)(br.bytepos - (uint8_t*)fcnt) * 8 + br.bitpos;
	int mapnlakes = br.readnb(6);
	lakeExtraBits.clear();
	for (int i = 0; i < mapnlakes; i++)
	{
		Vector3 v; int j;
		j = br.readnb(32); v.x = *(float*)&j;
		j = br.readnb(32); v.y = *(float*)&j;
		j = br.readnb(32); v.z = *(float*)&j;
		lakeExtraBits.push_back((uint8_t)br.readnb(2));
		lakes.push_back(std::move(v));
	}

	// Group name table
	bcmTablesBitOffset = (uint64_t)(br.bytepos - (uint8_t*)fcnt) * 8 + br.bitpos;
	int nnames = br.readnb(16);
	TerrainTextureGroup **grptable = new TerrainTextureGroup*[nnames];
	for (int i = 0; i < nnames; i++)
	{
		int n = br.readnb(8);
		char *a = new char[n + 1];
		for (int j = 0; j < n; j++) a[j] = br.readnb(8);
		a[n] = 0;
		grptable[i] = texDb.groups[a].get();
		if(grptable[i] == nullptr)
			ferr("BCM file uses unknown texture group.");
		delete[] a;
	}

	// ID table
	int nids = br.readnb(16);
	int *idtable = new int[nids];
	for (int i = 0; i < nids; i++)
		idtable[i] = br.readnb(32);

	// Tiles
	tiles = new Tile[width*height];
	Tile *t = tiles;
	int ngrpbits = GetMaxBits(nnames);
	int nidbits = GetMaxBits(nids);
	for (int z = height - 1; z >= 0; z--) {
		t = &tiles[z*width];
		for (int x = 0; x < width; x++)
		{
			int tg = br.readnb(ngrpbits); int ti = br.readnb(nidbits);
			int tr = br.readnb(2); int tx = br.readbit(); int tz = br.readbit();
			TerrainTextureGroup *g = grptable[tg];
			int id = idtable[ti];
			TerrainTexture *tex = g->textures[id].get();
			t->texture = tex;
			t->rot = tr; t->xflip = tx; t->zflip = tz;
			t->x = x; t->z = z;
			t++;
		}
	}
	bcmGroups.assign(grptable, grptable + nnames);
	bcmIds.assign(idtable, idtable + nids);

	// ?
	int nunk = br.readnb(32);
	bcmExtraConstant = br.readnb(32); // this should return 0x62
	bcmExtra.clear();
	for (int i = 0; i < nunk; i++)
	{
		const int g = br.readnb(ngrpbits), d = br.readnb(nidbits);
		bcmExtra.emplace_back(g < nnames ? grptable[g] : nullptr, d < nids ? idtable[d] : 0);
	}
	delete[] grptable; delete[] idtable;

	// Heightmap (kept with the file's bytes for writeBCM)
	bcmData.assign((uint8_t*)fcnt, (uint8_t*)fcnt + fsize);
	bcmHeightBitOffset = (uint64_t)(br.bytepos - (uint8_t*)fcnt) * 8 + br.bitpos;
	for (int y = 0; y <= height; y++)
		for (int x = 0; x <= width; x++)
			vertices[(height - y) * (width + 1) + x] = (uint8_t)br.readnb(8);

	free(fcnt);

	floodfillWater();
}

std::vector<uint8_t> Terrain::drawMinimap(bool flipX, bool flipZ) const
{
	std::vector<uint8_t> rgb(128 * 128 * 3, 0);
	if (!tiles || width <= 2 * edge || height <= 2 * edge)
		return rgb;
	// the mean colour of each texture's piece of its atlas
	std::map<const TerrainTexture*, std::array<float, 3>> colours;
	std::map<std::string, Bitmap> atlases;
	auto colourOf = [&](const TerrainTexture* tex) -> const std::array<float, 3>& {
		auto it = colours.find(tex);
		if (it != colours.end())
			return it->second;
		auto at = atlases.find(tex->file);
		if (at == atlases.end())
			at = atlases.emplace(tex->file, Bitmap::loadBitmap(("Maps\\Map_Textures\\" + tex->file).c_str()).convertToR8G8B8A8()).first;
		const Bitmap& a = at->second;
		double sum[3] = {};
		int n = 0;
		const int s = std::max(a.width / 256, 1); // a high-resolution mod's atlas: its texels per original texel
		for (int z = tex->starty * s; z < (tex->starty + tex->height) * s && z < a.height; z++)
			for (int x = tex->startx * s; x < (tex->startx + tex->width) * s && x < a.width; x++, n++)
				for (int c = 0; c < 3; c++)
					sum[c] += a.pixels[(z * a.width + x) * 4 + c];
		std::array<float, 3> mean = { 128, 128, 128 };
		if (n > 0)
			mean = { (float)(sum[0] / n), (float)(sum[1] / n), (float)(sum[2] / n) };
		return colours[tex] = mean;
	};
	const int pw = width - 2 * edge, ph = height - 2 * edge;
	const Vector3 sun = sunVector.normal();
	for (int py = 0; py < 128; py++)
		for (int px = 0; px < 128; px++) {
			int tx = px * pw / 128, tz = py * ph / 128;
			if (flipX) tx = pw - 1 - tx;
			if (flipZ) tz = ph - 1 - tz;
			const Tile* tile = getPlayableTile(tx, tz);
			std::array<float, 3> c = tile && tile->texture ? colourOf(tile->texture) : std::array<float, 3>{ 128, 128, 128 };
			const Vector3 n = getNormal(tx + edge, tz + edge);
			const float light = std::clamp(0.45f + 0.75f * (n.x * sun.x + n.y * sun.y + n.z * sun.z), 0.35f, 1.15f);
			// the original's minimaps are more saturated than the textures' means
			const float grey = 0.299f * c[0] + 0.587f * c[1] + 0.114f * c[2];
			for (float& v : c)
				v = (grey + (v - grey) * 1.4f) * light;
			if (tile && tile->fullOfWater) { // water: blue over the bed
				c[0] = c[0] * 0.3f + 60 * 0.7f;
				c[1] = c[1] * 0.3f + 140 * 0.7f;
				c[2] = c[2] * 0.3f + 185 * 0.7f;
			}
			for (int k = 0; k < 3; k++)
				rgb[(py * 128 + px) * 3 + k] = (uint8_t)std::clamp(c[k], 0.0f, 255.0f);
		}
	return rgb;
}

bool Terrain::writeBCM(const std::filesystem::path& path, bool newMinimap) const
{
	if (bcmData.empty() || !tiles)
		return false;
	// everything before the lakes as read (header, minimap), then the lakes, texture tables, tiles,
	// the (group, id) list and the heights written again, then whatever followed them
	std::vector<uint8_t> out(bcmData.begin(), bcmData.begin() + (size_t)((bcmLakesBitOffset + 7) / 8));
	if (newMinimap && bcmMinimapOffset > 0 && bcmMinimapOffset + 128 * 128 * 3 <= out.size()) {
		const std::vector<uint8_t> minimap = drawMinimap();
		std::copy(minimap.begin(), minimap.end(), out.begin() + bcmMinimapOffset);
	}
	uint64_t bit = bcmLakesBitOffset;
	// the bits as BitReader reads them: least significant first
	auto put = [&](uint32_t value, int bits) {
		for (int i = 0; i < bits; i++, bit++) {
			const size_t byte = (size_t)(bit / 8);
			const uint8_t mask = (uint8_t)(1 << (bit % 8));
			if (byte >= out.size())
				out.resize(byte + 1, 0);
			out[byte] = ((value >> i) & 1) ? (out[byte] | mask) : (out[byte] & ~mask);
		}
	};
	// the tables as read, with the textures painted since appended
	std::vector<TerrainTextureGroup*> groups = bcmGroups;
	std::vector<int> ids = bcmIds;
	auto indexOf = [](auto& list, auto value) {
		const auto it = std::find(list.begin(), list.end(), value);
		if (it != list.end())
			return (uint32_t)(it - list.begin());
		list.push_back(value);
		return (uint32_t)(list.size() - 1);
	};
	for (int i = 0; i < width * height; i++)
		if (TerrainTexture* tex = tiles[i].texture) {
			indexOf(groups, tex->grp);
			indexOf(ids, tex->id);
		}
	// the lakes: a point each (its y is the water level), at most 63
	const size_t lakeCount = std::min<size_t>(lakes.size(), 63);
	put((uint32_t)lakeCount, 6);
	for (size_t i = 0; i < lakeCount; i++) {
		for (float f : { lakes[i].x, lakes[i].y, lakes[i].z }) {
			uint32_t u;
			memcpy(&u, &f, 4);
			put(u, 32);
		}
		put(i < lakeExtraBits.size() ? lakeExtraBits[i] : 0, 2);
	}
	put((uint32_t)groups.size(), 16);
	for (TerrainTextureGroup* g : groups) {
		put((uint32_t)g->name.size(), 8);
		for (char c : g->name)
			put((uint8_t)c, 8);
	}
	put((uint32_t)ids.size(), 16);
	for (int id : ids)
		put((uint32_t)id, 32);
	const int groupBits = GetMaxBits((int)groups.size()), idBits = GetMaxBits((int)ids.size());
	for (int z = height - 1; z >= 0; z--)
		for (int x = 0; x < width; x++) {
			const Tile& t = tiles[z * width + x];
			put(t.texture ? indexOf(groups, t.texture->grp) : 0, groupBits);
			put(t.texture ? indexOf(ids, t.texture->id) : 0, idBits);
			put(t.rot, 2);
			put(t.xflip, 1);
			put(t.zflip, 1);
		}
	put((uint32_t)bcmExtra.size(), 32);
	put(bcmExtraConstant, 32);
	for (const auto& [g, id] : bcmExtra) {
		put(g ? indexOf(groups, g) : 0, groupBits);
		put(indexOf(ids, id), idBits);
	}
	for (int y = 0; y <= height; y++)
		for (int x = 0; x <= width; x++)
			put(vertices[(height - y) * (width + 1) + x], 8);
	// the rest of the file after the heights, bit by bit
	const uint64_t end = (uint64_t)bcmData.size() * 8;
	for (uint64_t from = bcmHeightBitOffset + (uint64_t)(width + 1) * (height + 1) * 8; from < end; from++)
		put((bcmData[(size_t)(from / 8)] >> (from % 8)) & 1, 1);
	std::error_code ec;
	std::filesystem::create_directories(path.parent_path(), ec);
	std::ofstream f(path, std::ios::binary);
	f.write((const char*)out.data(), (std::streamsize)out.size());
	return (bool)f;
}

void Terrain::readSNR(const char* filename)
{
	char* text; int textsize;
	LoadFile(filename, &text, &textsize, 1);
	text[textsize] = 0;

	GSFileParser gsf(text);
	while (!gsf.eof) {
		auto tag = gsf.nextTag();
		if (tag == "SCENARIO_DIMENSIONS") {
			width = gsf.nextInt();
			height = gsf.nextInt();
		}
		else if (tag == "SCENARIO_EDGE_WIDTH")
			edge = gsf.nextInt();
		else if (tag == "SCENARIO_TEXTURE_DATABASE") {
			auto str = gsf.nextString(true);
			texDbPath = std::wstring(str.begin(), str.end());
			texDb.load(str.c_str());
		}
		else if (tag == "SCENARIO_TERRAIN") {
			auto str = gsf.nextString(true);
			readTRN(str.c_str());
			texDb.translate(texDb.getLTTTPath(str).c_str());
		}
		else if (tag == "SCENARIO_HEIGHTMAP") {
			int numVerts = (width + 1) * (height + 1);
			Bitmap bmp = Bitmap::loadBitmap(gsf.nextString(true).c_str());
			if (bmp.width != width + 1 || bmp.height != height + 1)
				ferr("Incorrect heightmap bitmap size");
			vertices.resize(numVerts);
			for (int y = 0; y <= height; ++y)
				memcpy(vertices.data() + (height - y) * (width + 1), bmp.pixels.data() + y * (width + 1), width + 1);
		}
		else if (tag == "SCENARIO_HEIGHT_SCALE_FACTOR") {
			scale = gsf.nextFloat();
		}
		else if (tag == "SCENARIO_SUN_COLOUR") {
			sunColor = 0;
			for (int i = 0; i < 3; i++)
				sunColor = (sunColor << 8) | gsf.nextInt();
		}
		else if (tag == "SCENARIO_SUN_VECTOR") {
			for (float& f : sunVector)
				f = gsf.nextFloat();
		}
		else if (tag == "SCENARIO_FOG_COLOUR") {
			fogColor = 0;
			for (int i = 0; i < 3; i++)
				fogColor = (fogColor << 8) | gsf.nextInt();
		}
		else if (tag == "SCENARIO_SKY_TEXTURES_DIRECTORY") {
			auto str = gsf.nextString(true);
			skyTextureDirectory = std::wstring(str.begin(), str.end());
		}
		else if (tag == "SCENARIO_MINIMAP") {
			std::string minimapFile = gsf.nextString(true);
			if (!minimapFile.empty() && FileExists(minimapFile.c_str())) {
				Bitmap bmp = Bitmap::loadBitmap(minimapFile.c_str()).convertToR8G8B8A8();
				minimapWidth = bmp.width;
				minimapHeight = bmp.height;
				minimapRGBA.assign(bmp.pixels.data(), bmp.pixels.data() + bmp.width * bmp.height * 4);
			}
		}
		else if (tag == "SCENARIO_LAKE") {
			Vector3 vec;
			for (float& f : vec) f = gsf.nextFloat();
			lakes.push_back(vec);
		}

		gsf.advanceLine();
	}

	free(text);

	floodfillWater();
}

void Terrain::readTRN(const char* filename)
{
	tiles = new Tile[width * height];

	char* text; int textsize;
	LoadFile(filename, &text, &textsize, 1);
	text[textsize] = 0;

	GSFileParser gsf(text);
	while (!gsf.eof) {
		std::string str;

		str = gsf.nextString();
		if (str == "X") {
			int x = gsf.nextInt() - 1;

			str = gsf.nextString();
			int z = gsf.nextInt() - 1;

			str = gsf.nextString();
			auto grp = gsf.nextString(true);

			str = gsf.nextString();
			int id = gsf.nextInt();

			str = gsf.nextString();
			int rot = gsf.nextInt();

			str = gsf.nextString();
			bool xflip = gsf.nextInt();

			str = gsf.nextString();
			bool zflip = gsf.nextInt();

			Tile& t = tiles[width * z + x];
			t.x = x; t.z = z;
			t.rot = rot; t.xflip = xflip; t.zflip = zflip;
			t.texture = texDb.groups.at(grp)->textures.at(id).get();
		}
		gsf.advanceLine();
	}

	free(text);
}

void Terrain::floodfillWater()
{
	auto fillTile = [this](int tx, int tz, float lvl) -> bool {
		Tile& tile = tiles[tz * width + tx];
		if (tile.fullOfWater && lvl <= tile.waterLevel)
			return false;

		float h00 = getVertex(tx, tz);
		float h01 = getVertex(tx, tz+1);
		float h10 = getVertex(tx+1, tz);
		float h11 = getVertex(tx+1, tz+1);
		if (std::min({ h00,h01,h10,h11 }) < lvl) {
			tile.waterLevel = lvl;
			tile.fullOfWater = true;
			return true;
		}
		return false;
	};
	auto floodfill = [this, &fillTile](int tx, int tz, float lvl, const auto& rec) -> void {
		bool done = fillTile(tx, tz, lvl);
		if (!done) return;
		int x, xmin, xmax;
		for (x = tx - 1; x >= 0; x--) {
			bool done = fillTile(x, tz, lvl);
			if (!done) break;
		}
		xmin = x+1;
		for (x = tx + 1; x < width; x++) {
			bool done = fillTile(x, tz, lvl);
			if (!done) break;
		}
		xmax = x-1;
		for (x = xmin; x <= xmax; x++) {
			if (tz - 1 >= 0)
				rec(x, tz - 1, lvl, rec);
			if (tz + 1 < height)
				rec(x, tz + 1, lvl, rec);
		}
	};
	for (Vector3& lake : lakes) {
		floodfill((int)(lake.x / 5.0f), (int)(lake.z / 5.0f), lake.y, floodfill);
	}
}

void Terrain::refillWater()
{
	for (int i = 0; i < width * height; i++) {
		tiles[i].fullOfWater = false;
		tiles[i].waterLevel = -1.0f;
	}
	floodfillWater();
}
