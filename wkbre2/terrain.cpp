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
#include <set>
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

float Terrain::getSmoothHeight(float ipx, float ipz) const
{
	const float gx = edge + ipx / 5.0f, gz = edge + ipz / 5.0f;
	const int ix = (int)std::floor(gx), iz = (int)std::floor(gz);
	const float fx = gx - ix, fz = gz - iz;
	auto at = [&](int x, int z) {
		return getVertex(std::clamp(x, 0, (int)width), std::clamp(z, 0, (int)height));
	};
	auto spline = [](float a, float b, float c, float d, float t) {
		return 0.5f * (2 * b + (c - a) * t + (2 * a - 5 * b + 4 * c - d) * t * t + (3 * b - a - 3 * c + d) * t * t * t);
	};
	float row[4];
	for (int k = -1; k <= 2; k++)
		row[k + 1] = spline(at(ix - 1, iz + k), at(ix, iz + k), at(ix + 1, iz + k), at(ix + 2, iz + k), fx);
	return spline(row[0], row[1], row[2], row[3], fz);
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

	// what the original's format can't hold, from <map>.wkbmap next to it (wkbai's map editor)
	std::string extras = filename;
	extras = extras.substr(0, extras.size() - 4) + ".wkbmap";
	if (FileExists(extras.c_str()))
		readMapExtras(extras.c_str());

	floodfillWater();
}

// <map>.wkbmap: "WKBMAP\0\0", u32 version (1), then chunks of a 4-character tag, a u32 size and
// the data; chunks with other tags are skipped, so later versions can add their own.
//   HFRC: per vertex (rows as `vertices`), the fraction of a height step above it, in 256ths
//   LAKE: u32 count, then per lake f32 x, y, z and u8 the 2 bits the BCM keeps (all of them;
//         the BCM holds at most 63)
static const char MAP_EXTRAS_MAGIC[8] = { 'W', 'K', 'B', 'M', 'A', 'P', 0, 0 };

void Terrain::readMapExtras(const char* filename)
{
	char* data; int size;
	LoadFile(filename, &data, &size);
	const uint8_t* p = (const uint8_t*)data;
	const uint8_t* end = p + size;
	if (size < 12 || memcmp(p, MAP_EXTRAS_MAGIC, 8) != 0) {
		printf("WARNING: %s is not a wkbai map file\n", filename);
		free(data);
		return;
	}
	p += 12;
	while (end - p >= 8) {
		char tag[5] = {};
		memcpy(tag, p, 4);
		uint32_t len;
		memcpy(&len, p + 4, 4);
		p += 8;
		if ((size_t)(end - p) < len)
			break;
		if (!strcmp(tag, "HFRC") && len == vertices.size())
			heightFraction.assign(p, p + len);
		else if (!strcmp(tag, "LAKE") && len >= 4) {
			uint32_t n;
			memcpy(&n, p, 4);
			if (len >= 4 + n * 13ull) {
				lakes.clear();
				lakeExtraBits.clear();
				for (uint32_t i = 0; i < n; i++) {
					const uint8_t* l = p + 4 + i * 13;
					Vector3 v;
					memcpy(&v.x, l, 4);
					memcpy(&v.y, l + 4, 4);
					memcpy(&v.z, l + 8, 4);
					lakes.push_back(v);
					lakeExtraBits.push_back(l[12]);
				}
			}
		}
		p += len;
	}
	free(data);
}

bool Terrain::needsMapExtras() const
{
	return lakes.size() > 63 || std::any_of(heightFraction.begin(), heightFraction.end(), [](uint8_t f) { return f != 0; });
}

bool Terrain::writeMapExtras(const std::filesystem::path& path) const
{
	std::vector<uint8_t> out(MAP_EXTRAS_MAGIC, MAP_EXTRAS_MAGIC + 8);
	auto put32 = [&](uint32_t v) { out.insert(out.end(), (const uint8_t*)&v, (const uint8_t*)&v + 4); };
	auto chunk = [&](const char* tag, const std::vector<uint8_t>& data) {
		out.insert(out.end(), tag, tag + 4);
		put32((uint32_t)data.size());
		out.insert(out.end(), data.begin(), data.end());
	};
	put32(1);
	if (std::any_of(heightFraction.begin(), heightFraction.end(), [](uint8_t f) { return f != 0; }))
		chunk("HFRC", heightFraction);
	if (lakes.size() > 63) {
		std::vector<uint8_t> l;
		const uint32_t n = (uint32_t)lakes.size();
		l.insert(l.end(), (const uint8_t*)&n, (const uint8_t*)&n + 4);
		for (size_t i = 0; i < lakes.size(); i++) {
			for (float f : { lakes[i].x, lakes[i].y, lakes[i].z })
				l.insert(l.end(), (const uint8_t*)&f, (const uint8_t*)&f + 4);
			l.push_back(i < lakeExtraBits.size() ? lakeExtraBits[i] : 0);
		}
		chunk("LAKE", l);
	}
	std::error_code ec;
	std::filesystem::create_directories(path.parent_path(), ec);
	std::ofstream f(path, std::ios::binary);
	f.write((const char*)out.data(), (std::streamsize)out.size());
	return (bool)f;
}

void Terrain::setHeightSteps(int i, float steps)
{
	steps = std::clamp(steps, 0.0f, 255.0f + 255.0f / 256.0f);
	int whole = (int)std::floor(steps);
	int frac = (int)std::lround((steps - whole) * 256.0f);
	if (frac >= 256) {
		frac -= 256;
		whole++;
	}
	if (whole > 255) {
		whole = 255;
		frac = 255;
	}
	vertices[i] = (uint8_t)whole;
	if (frac && heightFraction.size() != vertices.size())
		heightFraction.assign(vertices.size(), 0);
	if (!heightFraction.empty())
		heightFraction[i] = (uint8_t)frac;
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
	// the (group, id) list: in all 54 shipped maps exactly the textures the tiles use, each once,
	// and the original's loader looks a tile's texture up in it (a painted texture missing from it
	// crashed the original). The pairs read are kept in their order while still used, new ones added
	std::vector<std::pair<TerrainTextureGroup*, int>> extra;
	{
		std::set<std::pair<TerrainTextureGroup*, int>> used;
		std::vector<std::pair<TerrainTextureGroup*, int>> usedInOrder;
		for (int i = 0; i < width * height; i++)
			if (TerrainTexture* tex = tiles[i].texture)
				if (used.insert({ tex->grp, tex->id }).second)
					usedInOrder.push_back({ tex->grp, tex->id });
		std::set<std::pair<TerrainTextureGroup*, int>> listed;
		for (const auto& pair : bcmExtra)
			if (used.count(pair) && listed.insert(pair).second)
				extra.push_back(pair);
		for (const auto& pair : usedInOrder)
			if (listed.insert(pair).second)
				extra.push_back(pair);
	}
	put((uint32_t)extra.size(), 32);
	put(bcmExtraConstant, 32);
	for (const auto& [g, id] : extra) {
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

bool Terrain::writeBlankBCM(const std::filesystem::path& path, int playableWidth, int playableHeight, const TerrainTexture* texture, uint8_t heightSteps) const
{
	if (bcmData.size() < 0x18 || bcmMinimapOffset < 0x18 || !texture || !texture->grp || playableWidth <= 0 || playableHeight <= 0)
		return false;
	const uint32_t w = (uint32_t)(playableWidth + 2 * edge), h = (uint32_t)(playableHeight + 2 * edge), e = (uint32_t)edge;
	// the 12 header bytes, the size, then this map's texture database, scale, sun, fog and sky
	std::vector<uint8_t> out(bcmData.begin(), bcmData.begin() + 12);
	for (uint32_t v : { w, h, e })
		out.insert(out.end(), (const uint8_t*)&v, (const uint8_t*)&v + 4);
	out.insert(out.end(), bcmData.begin() + 0x18, bcmData.begin() + bcmMinimapOffset);
	// the minimap: one colour, the mean of this map's (the editor draws it again on saving)
	uint32_t sum[3] = {};
	for (size_t i = 0; i < minimapRGBA.size() / 4; i++)
		for (int c = 0; c < 3; c++)
			sum[c] += minimapRGBA[i * 4 + c];
	const size_t n = std::max<size_t>(1, minimapRGBA.size() / 4);
	for (int i = 0; i < 128 * 128; i++)
		for (int c = 0; c < 3; c++)
			out.push_back((uint8_t)(sum[c] / n));
	uint64_t bit = (uint64_t)out.size() * 8;
	auto put = [&](uint32_t value, int bits) {
		for (int i = 0; i < bits; i++, bit++) {
			const size_t byte = (size_t)(bit / 8);
			if (byte >= out.size())
				out.resize(byte + 1, 0);
			if ((value >> i) & 1)
				out[byte] |= (uint8_t)(1 << (bit % 8));
		}
	};
	put(0, 6); // no lakes
	put(1, 16);
	put((uint32_t)texture->grp->name.size(), 8);
	for (char c : texture->grp->name)
		put((uint8_t)c, 8);
	put(1, 16);
	put((uint32_t)texture->id, 32);
	const int groupBits = GetMaxBits(1), idBits = GetMaxBits(1);
	for (uint32_t i = 0; i < w * h; i++) {
		put(0, groupBits);
		put(0, idBits);
		put(0, 4); // no turn, no flips
	}
	// the (group, id) list of the textures used: the one
	put(1, 32);
	put(bcmExtraConstant, 32);
	put(0, groupBits);
	put(0, idBits);
	for (uint32_t i = 0; i < (w + 1) * (h + 1); i++)
		put(heightSteps, 8);
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
	// Each lake is a point whose height is its water level. The water spreads from the point's
	// tile to a neighbour only across their shared edge, where one of the edge's two corners is
	// below the level: a tile that merely touches the water with its other corners stays dry.
	// (Spreading to any tile with a corner below the level let a mountain tarn on 6 Player Tribes
	// Unite leak over its rim through diagonal corners and flood the whole map.) A higher lake
	// overrides a lower one's tiles.
	std::vector<std::pair<int, int>> queue;
	for (const Vector3& lake : lakes) {
		const int sx = (int)(lake.x / 5.0f), sz = (int)(lake.z / 5.0f);
		const float lvl = lake.y;
		if (sx < 0 || sz < 0 || sx >= (int)width || sz >= (int)height)
			continue;
		Tile& seed = tiles[sz * width + sx];
		if ((seed.fullOfWater && lvl <= seed.waterLevel)
			|| std::min({ getVertex(sx, sz), getVertex(sx + 1, sz), getVertex(sx, sz + 1), getVertex(sx + 1, sz + 1) }) >= lvl)
			continue;
		seed.waterLevel = lvl;
		seed.fullOfWater = true;
		queue.assign(1, { sx, sz });
		for (size_t q = 0; q < queue.size(); q++) {
			const auto [x, z] = queue[q];
			// the neighbour, and the two corners of the edge shared with it
			const int nb[4][6] = { { x + 1, z, x + 1, z, x + 1, z + 1 }, { x - 1, z, x, z, x, z + 1 },
				{ x, z + 1, x, z + 1, x + 1, z + 1 }, { x, z - 1, x, z, x + 1, z } };
			for (const auto& n : nb) {
				if (n[0] < 0 || n[1] < 0 || n[0] >= (int)width || n[1] >= (int)height)
					continue;
				Tile& tile = tiles[n[1] * width + n[0]];
				if (tile.fullOfWater && lvl <= tile.waterLevel)
					continue;
				if (std::min(getVertex(n[2], n[3]), getVertex(n[4], n[5])) >= lvl)
					continue;
				tile.waterLevel = lvl;
				tile.fullOfWater = true;
				queue.push_back({ n[0], n[1] });
			}
		}
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
