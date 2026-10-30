// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#include "EnhancedTerrainData.h"
#include "../terrain.h"
#include "TextureCache.h"
#include "bitmap.h"
#include "../settings.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <map>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>

static constexpr uint32_t Vec3ToR10G10B10A2(const Vector3& vec) {
	uint32_t res = (uint32_t)(vec.x * 1023.0f);
	res |= (uint32_t)(vec.y * 1023.0f) << 10;
	res |= (uint32_t)(vec.z * 1023.0f) << 20;
	return res;
}
static constexpr uint32_t NormalToR10G10B10A2(const Vector3& vec) {
	return Vec3ToR10G10B10A2((vec + Vector3(1, 1, 1)) * 0.5f);
}

// The up component of a bump map texel's normal before normalizing, against height differences
// / 510: fitted so the terrain is as dark as the original's on 4 Peaks (docs/engine/
// original-comparison.md in the RE repository); Rock1's normals then average 0.80 up
static constexpr float BUMP_NORMAL_UP = 0.05f;

void LoadEnhancedTerrainTextures(Terrain* terrain, TextureCache* texcache, std::map<TerrainTexture*, EnhancedTerrainTexture>& textures)
{
	// Load terrain color textures
	for (const auto &grp : terrain->texDb.groups) {
		printf("%s\n", grp.first.c_str());
		for (const auto &tex : grp.second->textures) {
			EnhancedTerrainTexture ld;
			ld.diffuseMap = texcache->getTexture(tex.second->file.c_str());

			std::string nrm;
			size_t ext = tex.second->file.rfind('.');
			if (ext != nrm.npos)
				nrm = tex.second->file.substr(0, ext);
			else
				nrm = tex.second->file;
			if (g_settings.value<int>("gameVersion", 0) < 2) {
				nrm.append("_NormalMap.pcx");
				ld.normalMap = texcache->getTexture(nrm.c_str());
			}
			else {
				nrm.append("_BumpMap.pcx");
				ld.normalMap = texcache->getTextureIfCached(nrm.c_str());
				if (!ld.normalMap) {
					Bitmap bmp = Bitmap::loadBitmap((texcache->directory + nrm).c_str());
					Bitmap nmap; nmap.width = bmp.width; nmap.height = bmp.height; nmap.format = BMFORMAT_R8G8B8A8;
					nmap.pixels.resize(nmap.width * nmap.height * 4);
					uint8_t* bmpix = (uint8_t*)bmp.pixels.data();
					uint8_t* nmpix = (uint8_t*)nmap.pixels.data();
					int pitch = nmap.width;
					for (int tz = 0; tz < 4; tz++) {
						for (int tx = 0; tx < 4; tx++) {
							int sx = 64 * tx, sz = 64 * tz;
							for (int cz = 0; cz < 64; cz++) {
								for (int cx = 0; cx < 64; cx++) {
									int ax = (cx + 1) & 63, az = (cz + 1) & 63;
									int bx = (cx - 1) & 63, bz = (cz - 1) & 63;

									int cheight = bmpix[(sz + cz) * pitch + sx + cx];

									int dx1 = 0, dx2 = 0, dz1 = 0, dz2 = 0;
									if(ax != 0)
										dx1 = (int)bmpix[(sz + cz) * pitch + sx + ax] - cheight;
									if(bx != 63)
										dx2 = (int)bmpix[(sz + cz) * pitch + sx + bx] - cheight;
									if(az != 0)
										dz1 = (int)bmpix[(sz + az) * pitch + sx + cx] - cheight;
									if(bz != 63)
										dz2 = (int)bmpix[(sz + bz) * pitch + sx + cx] - cheight;

									float fx = (-dx1+dx2) / 510.0f, fz = (-dz1+dz2) / 510.0f;
									//fx *= 3.0f; fz *= 3.0f;
									//float fy = 1.0f - fx * fx - fz * fz;
									float fy = BUMP_NORMAL_UP;
									float norm = std::sqrt(fx * fx + fy * fy + fz * fz);
									fx /= norm; fy /= norm; fz /= norm;

									int dx = (int)(fx * 255.0f);
									int dy = (int)(fy * 255.0f);
									int dz = (int)(fz * 255.0f);

									uint8_t* out = nmpix + ((sz + cz) * pitch + sx + cx) * 4;
									out[0] = (uint8_t)((dx + 255) / 2);
									out[1] = (uint8_t)((dy + 255) / 2);
									out[2] = (uint8_t)((dz + 255) / 2);
									out[3] = 255;
								}
							}
						}
					}
					ld.normalMap = texcache->createTexture(nrm.c_str(), 0, nmap);
				}
			}
			textures[tex.second.get()] = ld;
		}
	}
}

// The atlas UVs (0..1 over the 256-texel atlas) at a tile's corners (x, z), (x + 1, z),
// (x + 1, z + 1), (x, z + 1): its texture's rectangle, flipped and turned as the tile says
static std::array<std::pair<float, float>, 4> TileCornerUVs(const TerrainTile* tile)
{
	const TerrainTexture* trntex = tile->texture;
	float sx = trntex->startx / 256.0f;
	float sy = trntex->starty / 256.0f;
	float tw = trntex->width / 256.0f;
	float th = trntex->height / 256.0f;
	std::array<std::pair<float, float>, 4> uvs{ { {sx, sy}, {sx + tw, sy}, { sx + tw, sy + th }, {sx, sy + th } } };
	bool xflip = (bool)tile->xflip != (bool)(tile->rot & 2);
	bool zflip = (bool)tile->zflip != (bool)(tile->rot & 2);
	bool rot = tile->rot & 1;
	if (!xflip) {
		std::swap(uvs[0], uvs[3]);
		std::swap(uvs[1], uvs[2]);
	}
	if (zflip) {
		std::swap(uvs[0], uvs[1]);
		std::swap(uvs[2], uvs[3]);
	}
	if (rot) {
		auto tmp = uvs[0];
		uvs[0] = uvs[1];
		uvs[1] = uvs[2];
		uvs[2] = uvs[3];
		uvs[3] = tmp;
	}
	return uvs;
}

std::vector<EnhancedTerrainVertex> BuildEnhancedTerrainVertices(Terrain* terrain)
{
	// Precompute terrain normals
	std::vector<uint32_t> trnNormals((terrain->width + 1) * (terrain->height + 1));
	size_t ni = 0;
	for (int z = 0; z <= terrain->height; z++) {
		for (int x = 0; x <= terrain->width; x++) {
			trnNormals[ni++] = NormalToR10G10B10A2(terrain->getNormal(x, z));
		}
	}

	// Terrain vertex buffer
	std::vector<EnhancedTerrainVertex> trnVertices;
	assert(terrain != nullptr);
	for (int z = 0; z < terrain->height; z++) {
		for (int x = 0; x < terrain->width; x++) {
			int lx = x - terrain->edge, lz = z - terrain->edge;
			const Terrain::Tile* tile = terrain->getTile(x, z);

			const std::array<std::pair<float, float>, 4> uvs = TileCornerUVs(tile);

			constexpr float tilesize = 5.0f;
			Vector3 poses[4];
			poses[0] = Vector3(lx * tilesize, terrain->getVertex(x, z), lz * tilesize);
			poses[1] = Vector3((lx + 1) * tilesize, terrain->getVertex(x + 1, z), lz * tilesize);
			poses[2] = Vector3((lx + 1) * tilesize, terrain->getVertex(x + 1, z + 1), (lz + 1) * tilesize);
			poses[3] = Vector3(lx * tilesize, terrain->getVertex(x, z + 1), (lz + 1) * tilesize);

			float du1 = uvs[3].first - uvs[0].first, dv1 = uvs[3].second - uvs[0].second;
			float du2 = uvs[1].first - uvs[0].first, dv2 = uvs[1].second - uvs[0].second;
			Vector3 edge1 = poses[3] - poses[0];
			Vector3 edge2 = poses[1] - poses[0];
			float idet = 1.0f / (du1 * dv2 - du2 * dv1);
			Vector3 tangent = (edge1 * dv2 - edge2 * dv1) * idet;
			Vector3 bitangent = (edge2 * du1 - edge1 * du2) * idet;
			uint32_t cmprTangent = NormalToR10G10B10A2(tangent.normal());
			uint32_t cmprBitangent = NormalToR10G10B10A2(bitangent.normal());

			auto getPrecomputedNormal = [&](int x, int z) { return trnNormals[z * (terrain->width + 1) + x]; };
			const uint32_t colorBits =
				(x >= terrain->edge && z >= terrain->edge && x < terrain->width - terrain->edge && z < terrain->height - terrain->edge) ?
				0xC000'0000 : 0;

			EnhancedTerrainVertex outvert[4];
			outvert[0].pos = poses[0];
			outvert[0].normal = getPrecomputedNormal(x, z) | colorBits;
			outvert[0].tangent = cmprTangent;
			outvert[0].bitangent = cmprBitangent;
			outvert[0].u = uvs[0].first;
			outvert[0].v = uvs[0].second;
			outvert[1].pos = poses[1];
			outvert[1].normal = getPrecomputedNormal(x + 1, z) | colorBits;
			outvert[1].tangent = cmprTangent;
			outvert[1].bitangent = cmprBitangent;
			outvert[1].u = uvs[1].first;
			outvert[1].v = uvs[1].second;
			outvert[2].pos = poses[2];
			outvert[2].normal = getPrecomputedNormal(x + 1, z + 1) | colorBits;
			outvert[2].tangent = cmprTangent;
			outvert[2].bitangent = cmprBitangent;
			outvert[2].u = uvs[2].first;
			outvert[2].v = uvs[2].second;
			outvert[3].pos = poses[3];
			outvert[3].normal = getPrecomputedNormal(x, z + 1) | colorBits;
			outvert[3].tangent = cmprTangent;
			outvert[3].bitangent = cmprBitangent;
			outvert[3].u = uvs[3].first;
			outvert[3].v = uvs[3].second;
			trnVertices.insert(trnVertices.end(), std::begin(outvert), std::end(outvert));
		}
	}
	return trnVertices;
}

// A height map tile (a bump map's 8-bit indices) as normals, as LoadEnhancedTerrainTextures
// makes them: the height differences to the neighbours, wrapping within the tile
static void BumpTileToNormals(const uint8_t* heights, int pitch, int size, uint8_t* out, int outPitch)
{
	for (int cz = 0; cz < size; cz++)
		for (int cx = 0; cx < size; cx++) {
			const int c = heights[cz * pitch + cx];
			const int dx1 = (int)heights[cz * pitch + (cx + 1) % size] - c;
			const int dx2 = (int)heights[cz * pitch + (cx + size - 1) % size] - c;
			const int dz1 = (int)heights[((cz + 1) % size) * pitch + cx] - c;
			const int dz2 = (int)heights[((cz + size - 1) % size) * pitch + cx] - c;
			float fx = (-dx1 + dx2) / 510.0f, fz = (-dz1 + dz2) / 510.0f, fy = BUMP_NORMAL_UP;
			const float norm = std::sqrt(fx * fx + fy * fy + fz * fz);
			fx /= norm; fy /= norm; fz /= norm;
			uint8_t* o = out + (cz * outPitch + cx) * 4;
			o[0] = (uint8_t)(((int)(fx * 255.0f) + 255) / 2);
			o[1] = (uint8_t)(((int)(fy * 255.0f) + 255) / 2);
			o[2] = (uint8_t)(((int)(fz * 255.0f) + 255) / 2);
			o[3] = 255;
		}
}

TerrainSplatData BuildTerrainSplat(Terrain* terrain)
{
	TerrainSplatData splat;
	const std::string dir = "Maps\\Map_Textures\\";
	const bool battles = g_settings.value<int>("gameVersion", 0) >= 2;
	std::map<std::string, Bitmap> atlases, bumps; // by texture file
	auto atlas = [&](const std::string& file) -> const Bitmap& {
		auto it = atlases.find(file);
		if (it == atlases.end())
			it = atlases.emplace(file, Bitmap::loadBitmap((dir + file).c_str()).convertToR8G8B8A8()).first;
		return it->second;
	};
	auto normalSource = [&](const std::string& file) -> const Bitmap& {
		auto it = bumps.find(file);
		if (it == bumps.end()) {
			std::string base = file.substr(0, file.rfind('.'));
			Bitmap bmp = Bitmap::loadBitmap((dir + base + (battles ? "_BumpMap.pcx" : "_NormalMap.pcx")).c_str());
			if (!battles)
				bmp = bmp.convertToR8G8B8A8();
			it = bumps.emplace(file, std::move(bmp)).first;
		}
		return it->second;
	};

	// how often each texture is used on this map
	std::map<const TerrainTexture*, int> uses;
	for (int z = 0; z < terrain->height; z++)
		for (int x = 0; x < terrain->width; x++)
			uses[terrain->getTile(x, z)->texture]++;

	// the layers' size: LAYER_SIZE, or 2 or 4 times that with a high-resolution mod's atlases
	// (Bitmap::replacementFile), so the blended ground keeps their detail
	for (const auto& [tex, n] : uses)
		if (tex && n > 0)
			while (splat.layerSize < TerrainSplatData::LAYER_SIZE * 4 && atlas(tex->file).width >= 256 * splat.layerSize / TerrainSplatData::LAYER_SIZE * 2)
				splat.layerSize *= 2;

	// the layers, by material (group) name
	std::map<std::string, int> layerOf;
	std::vector<std::array<float, 3>> meanColour;
	auto layer = [&](const std::string& name) -> int {
		if (auto it = layerOf.find(name); it != layerOf.end())
			return it->second;
		auto git = terrain->texDb.groups.find(name);
		if (git == terrain->texDb.groups.end() || git->second->textures.empty())
			return -1;
		std::vector<const TerrainTexture*> textures;
		for (const auto& [id, tex] : git->second->textures)
			textures.push_back(tex.get());
		std::stable_sort(textures.begin(), textures.end(), [&](const TerrainTexture* a, const TerrainTexture* b) { return uses[a] > uses[b]; });
		// the four quadrants: the material's most used tiles on the map (repeated if it uses
		// fewer); the shader picks one per tile of the ground
		std::vector<const TerrainTexture*> variants;
		for (const TerrainTexture* t : textures)
			if (uses[t] > 0 && variants.size() < 4)
				variants.push_back(t);
		if (variants.empty())
			variants.push_back(textures[0]);
		const int L = splat.layerSize, T = L / 2;
		constexpr int T0 = TerrainSplatData::LAYER_SIZE / 2; // a tile's texels in the original atlases
		std::vector<uint8_t> diffuse(L * L * 4), normal(L * L * 4), tileNormal(T0 * T0 * 4);
		double sum[3] = {};
		for (int q = 0; q < 4; q++) {
			const TerrainTexture* tex = variants[q % variants.size()];
			const int ox = (q & 1) * T, oz = (q >> 1) * T;
			const Bitmap& a = atlas(tex->file);
			const Bitmap& b = normalSource(tex->file);
			for (int cz = 0; cz < T; cz++)
				for (int cx = 0; cx < T; cx++) {
					// the atlas may be larger than the 256 texels the database counts in
					const int sx = std::min((tex->startx * a.width + cx * tex->width * a.width / T) / 256, a.width - 1);
					const int sz = std::min((tex->starty * a.height + cz * tex->height * a.height / T) / 256, a.height - 1);
					uint8_t* d = &diffuse[((oz + cz) * L + ox + cx) * 4];
					if (a.width > 0)
						std::copy_n(&a.pixels[(sz * a.width + sx) * 4], 4, d);
					else
						d[0] = d[1] = d[2] = d[3] = 255;
					for (int c = 0; c < 3; c++)
						sum[c] += d[c];
				}
			// the tile's normals at the bump map's resolution, then bilinearly (wrapping) to T
			if (battles && b.width >= tex->startx + tex->width && b.height >= tex->starty + tex->height && tex->width == T0 && tex->height == T0)
				BumpTileToNormals(&b.pixels[tex->starty * b.width + tex->startx], b.width, T0, tileNormal.data(), T0);
			else if (!battles && b.width >= tex->startx + tex->width && tex->width == T0 && tex->height == T0) {
				for (int cz = 0; cz < T0; cz++)
					std::copy_n(&b.pixels[((tex->starty + cz) * b.width + tex->startx) * 4], T0 * 4, &tileNormal[cz * T0 * 4]);
			}
			else
				for (int i = 0; i < T0 * T0; i++) {
					uint8_t* o = &tileNormal[i * 4];
					o[0] = 128; o[1] = 255; o[2] = 128; o[3] = 255;
				}
			for (int cz = 0; cz < T; cz++)
				for (int cx = 0; cx < T; cx++) {
					uint8_t* o = &normal[((oz + cz) * L + ox + cx) * 4];
					if (T == T0) {
						std::copy_n(&tileNormal[(cz * T0 + cx) * 4], 4, o);
						continue;
					}
					const float fx = (cx + 0.5f) * T0 / T - 0.5f, fz = (cz + 0.5f) * T0 / T - 0.5f;
					const int x0 = (int)std::floor(fx), z0 = (int)std::floor(fz);
					const float wx = fx - x0, wz = fz - z0;
					auto at = [&](int x, int z) { return &tileNormal[((((z % T0) + T0) % T0) * T0 + ((x % T0) + T0) % T0) * 4]; };
					for (int c = 0; c < 4; c++)
						o[c] = (uint8_t)std::lround((at(x0, z0)[c] * (1 - wx) + at(x0 + 1, z0)[c] * wx) * (1 - wz)
							+ (at(x0, z0 + 1)[c] * (1 - wx) + at(x0 + 1, z0 + 1)[c] * wx) * wz);
				}
		}
		const int index = (int)splat.names.size();
		splat.names.push_back(name);
		splat.diffuse.push_back(std::move(diffuse));
		splat.normal.push_back(std::move(normal));
		meanColour.push_back({ (float)(sum[0] / (L * L)), (float)(sum[1] / (L * L)), (float)(sum[2] / (L * L)) });
		layerOf[name] = index;
		return index;
	};

	// the corners' votes, then the most voted material per corner
	const int cw = terrain->width + 1;
	std::vector<std::array<int, 4>> votes(cw * (terrain->height + 1), { -1, -1, -1, -1 });
	auto vote = [&](int x, int z, int l) {
		for (int& v : votes[z * cw + x])
			if (v < 0) { v = l; break; }
	};
	static const int cornerX[4] = { 0, 1, 1, 0 }, cornerZ[4] = { 0, 0, 1, 1 };
	for (int z = 0; z < terrain->height; z++)
		for (int x = 0; x < terrain->width; x++) {
			const TerrainTile* tile = terrain->getTile(x, z);
			const std::string& group = tile->texture->grp ? tile->texture->grp->name : std::string();
			const size_t dash = group.find('-');
			int la = -1, lb = -1;
			if (dash != std::string::npos) {
				la = layer(group.substr(0, dash));
				lb = layer(group.substr(dash + 1));
			}
			if (la < 0 || lb < 0) { // a material, or a transition between unknown ones: its own layer
				int l = layer(group);
				if (l < 0)
					l = 0;
				for (int k = 0; k < 4; k++)
					vote(x + cornerX[k], z + cornerZ[k], l);
				continue;
			}
			// each corner: the mean colour of the 12² texels in that corner of the tile's picture
			const Bitmap& a = atlas(tile->texture->file);
			const auto uvs = TileCornerUVs(tile);
			const float cu = (uvs[0].first + uvs[2].first) * 0.5f * 256.0f, cv = (uvs[0].second + uvs[2].second) * 0.5f * 256.0f;
			const float as = a.width > 0 ? a.width / 256.0f : 1.0f; // a high-resolution atlas: its texels per original texel
			for (int k = 0; k < 4; k++) {
				float u = uvs[k].first * 256.0f, v = uvs[k].second * 256.0f;
				const float du = cu > u ? 1.0f : -1.0f, dv = cv > v ? 1.0f : -1.0f;
				double c[3] = {};
				int n = 0;
				for (int i = 0; i < 12 && a.width > 0; i++)
					for (int j = 0; j < 12; j++) {
						const int px = std::clamp((int)((u + du * (i + 0.5f)) * as), 0, a.width - 1);
						const int pz = std::clamp((int)((v + dv * (j + 0.5f)) * as), 0, a.height - 1);
						const uint8_t* t = &a.pixels[(pz * a.width + px) * 4];
						c[0] += t[0]; c[1] += t[1]; c[2] += t[2];
						n++;
					}
				auto dist = [&](int l) {
					float d = 0;
					for (int ch = 0; ch < 3; ch++) {
						const float e = (float)(c[ch] / std::max(n, 1)) - meanColour[l][ch];
						d += e * e;
					}
					return d;
				};
				vote(x + cornerX[k], z + cornerZ[k], dist(la) <= dist(lb) ? la : lb);
			}
		}
	splat.corners.resize(votes.size());
	for (size_t i = 0; i < votes.size(); i++) {
		int best = votes[i][0] < 0 ? 0 : votes[i][0], bestCount = 0;
		for (int v : votes[i]) {
			if (v < 0)
				continue;
			const int count = (int)std::count(votes[i].begin(), votes[i].end(), v);
			if (count > bestCount) { best = v; bestCount = count; }
		}
		splat.corners[i] = (uint8_t)std::min(best, 255);
	}
	if (splat.names.empty()) { // no texture database: one white layer
		splat.names.push_back("none");
		splat.diffuse.emplace_back(splat.layerSize * splat.layerSize * 4, (uint8_t)255);
		splat.normal.emplace_back(splat.layerSize * splat.layerSize * 4, (uint8_t)128);
	}
	printf("Splat terrain: %zu materials, %i texels\n", splat.names.size(), splat.layerSize);
	return splat;
}

void TerrainGrassBuilder::addTile(Terrain* terrain, int x, int z, float density, std::vector<GrassBlade>& out)
{
	const TerrainTile* tile = terrain->getTile(x, z);
	if (tile->fullOfWater || !tile->texture)
		return;
	constexpr float tilesize = 5.0f;
	const float h00 = terrain->getVertex(x, z), h10 = terrain->getVertex(x + 1, z);
	const float h11 = terrain->getVertex(x + 1, z + 1), h01 = terrain->getVertex(x, z + 1);
	// none on steep ground (a rise of more than 0.6 per unit)
	const float rise = std::max({ std::abs(h10 - h00), std::abs(h01 - h00), std::abs(h11 - h01), std::abs(h11 - h10) }) / tilesize;
	if (rise > 0.6f)
		return;
	auto it = atlases.find(tile->texture->file);
	if (it == atlases.end())
		it = atlases.emplace(tile->texture->file, Bitmap::loadBitmap(("Maps\\Map_Textures\\" + tile->texture->file).c_str()).convertToR8G8B8A8()).first;
	const Bitmap& a = it->second;
	if (a.width <= 0 || a.height <= 0)
		return;
	const auto uvs = TileCornerUVs(tile);
	uint32_t seed = ((uint32_t)x * 73856093u) ^ ((uint32_t)z * 19349663u) ^ 0x9e3779b9u;
	auto rnd = [&]() {
		seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
		return (seed & 0xFFFFFF) / 16777216.0f;
	};
	const float lx = (x - (int)terrain->edge) * tilesize, lz = (z - (int)terrain->edge) * tilesize;
	const int count = (int)(density * tilesize * tilesize + 0.5f);
	for (int i = 0; i < count; i++) {
		const float fx = rnd(), fz = rnd();
		// the picture's texel there: the corners' UVs, bilinearly
		const float u = (uvs[0].first + (uvs[1].first - uvs[0].first) * fx) * (1 - fz) + (uvs[3].first + (uvs[2].first - uvs[3].first) * fx) * fz;
		const float v = (uvs[0].second + (uvs[1].second - uvs[0].second) * fx) * (1 - fz) + (uvs[3].second + (uvs[2].second - uvs[3].second) * fx) * fz;
		// 3 x 3 texels: a single one is often a dark speck of the picture
		float r = 0, g = 0, b = 0;
		for (int dz = -1; dz <= 1; dz++)
			for (int dx = -1; dx <= 1; dx++) {
				const int px = std::clamp((int)(u * a.width) + dx, 0, a.width - 1), pz = std::clamp((int)(v * a.height) + dz, 0, a.height - 1);
				const uint8_t* t = &a.pixels[(pz * a.width + px) * 4];
				r += t[0] / 9.0f; g += t[1] / 9.0f; b += t[2] / 9.0f;
			}
		if (!(g > r * 1.05f && g > b * 1.15f && g > 40))
			continue; // not grass: rock, sand, earth, snow
		const float shade = 0.8f + 0.4f * rnd();
		auto channel = [&](float c) { return (uint32_t)std::clamp(c * shade, 0.0f, 255.0f); };
		GrassBlade blade;
		blade.x = lx + fx * tilesize;
		blade.z = lz + fz * tilesize;
		blade.y = (h00 + (h10 - h00) * fx) * (1 - fz) + (h01 + (h11 - h01) * fx) * fz;
		blade.colour = channel(r) | (channel(g) << 8) | (channel(b) << 16) | ((uint32_t)(rnd() * 255.0f) << 24);
		blade.yaw = rnd() * 6.2831853f;
		blade.height = 0.4f + 0.45f * rnd();
		out.push_back(blade);
	}
}

std::vector<float> BuildRiverField(Terrain* terrain, const std::vector<uint8_t>& flow, std::vector<float>* halfWidth)
{
	constexpr int R = RIVER_FIELD_RES;
	constexpr float cell = 5.0f / R;
	const int W = (int)terrain->width, H = (int)terrain->height;
	const int GW = W * R, GH = H * R;
	const size_t N = (size_t)GW * GH;
	std::vector<float> out(N * 4, 0.0f);
	if (halfWidth)
		halfWidth->assign(N, 0.0f);
	// the river share between the tiles' centres
	auto share = [&](float gx, float gz) {
		const float fx = gx - 0.5f, fz = gz - 0.5f;
		const int ix = (int)std::floor(fx), iz = (int)std::floor(fz);
		const float tx = fx - ix, tz = fz - iz;
		auto at = [&](int x, int z) {
			x = std::clamp(x, 0, W - 1);
			z = std::clamp(z, 0, H - 1);
			return flow[((size_t)z * W + x) * 4 + 3] / 255.0f;
		};
		return (at(ix, iz) * (1 - tx) + at(ix + 1, iz) * tx) * (1 - tz) + (at(ix, iz + 1) * (1 - tx) + at(ix + 1, iz + 1) * tx) * tz;
	};
	std::vector<uint8_t> wet(N, 0), riv(N, 0);
	bool anyRiver = false;
	for (int z = 0; z < GH; z++)
		for (int x = 0; x < GW; x++) {
			const TerrainTile* tile = terrain->getTile(x / R, z / R);
			if (!tile->fullOfWater)
				continue;
			const float gx = (x + 0.5f) / R, gz = (z + 0.5f) / R;
			const float ground = terrain->getSmoothHeight((gx - terrain->edge) * 5.0f, (gz - terrain->edge) * 5.0f);
			if (tile->waterLevel <= ground)
				continue;
			const size_t i = (size_t)z * GW + x;
			wet[i] = 1;
			if (share(gx, gz) >= 0.5f) {
				riv[i] = 1;
				anyRiver = true;
			}
		}
	if (!anyRiver)
		return out;
	using Item = std::pair<float, int>;
	// the nearest bank: each texel keeps the dry point it was reached from (Euclidean distance)
	std::vector<float> bank(N, 1e30f);
	std::vector<int> from(N, -1);
	{
		std::vector<Item> heap;
		for (int i = 0; i < (int)N; i++)
			if (!wet[i]) {
				const int x = i % GW, z = i / GW;
				bool nearWet = false;
				for (int dz = -1; dz <= 1 && !nearWet; dz++)
					for (int dx = -1; dx <= 1; dx++) {
						const int ax = x + dx, az = z + dz;
						if (ax >= 0 && az >= 0 && ax < GW && az < GH && wet[(size_t)az * GW + ax]) {
							nearWet = true;
							break;
						}
					}
				if (nearWet) {
					bank[i] = 0;
					from[i] = i;
					heap.push_back({ 0.0f, i });
				}
			}
		std::make_heap(heap.begin(), heap.end(), std::greater<>());
		while (!heap.empty()) {
			std::pop_heap(heap.begin(), heap.end(), std::greater<>());
			const auto [dt, i] = heap.back();
			heap.pop_back();
			if (dt > bank[i])
				continue;
			const int x = i % GW, z = i / GW;
			const int sx = from[i] % GW, sz = from[i] / GW;
			for (int dz = -1; dz <= 1; dz++)
				for (int dx = -1; dx <= 1; dx++) {
					const int ax = x + dx, az = z + dz;
					if ((dx == 0 && dz == 0) || ax < 0 || az < 0 || ax >= GW || az >= GH)
						continue;
					const int n = az * GW + ax;
					if (!wet[n])
						continue;
					const float nd = std::sqrt((float)((ax - sx) * (ax - sx) + (az - sz) * (az - sz))) * cell;
					if (nd < bank[n]) {
						bank[n] = nd;
						from[n] = from[i];
						heap.push_back({ nd, n });
						std::push_heap(heap.begin(), heap.end(), std::greater<>());
					}
				}
		}
	}
	// the way to the mouth along the channel: Dijkstra over the river texels with sixteen
	// neighbours (the knight's moves too, so that the fronts stay round and cross the channel
	// at right angles), from the texels next to wide water
	static const int mx[16] = { 1, -1, 0, 0, 1, 1, -1, -1, 2, 2, -2, -2, 1, 1, -1, -1 };
	static const int mz[16] = { 0, 0, 1, -1, 1, -1, 1, -1, 1, -1, 1, -1, 2, -2, 2, -2 };
	std::vector<float> down(N, 1e30f);
	auto march = [&](std::vector<Item> heap) {
		std::make_heap(heap.begin(), heap.end(), std::greater<>());
		while (!heap.empty()) {
			std::pop_heap(heap.begin(), heap.end(), std::greater<>());
			const auto [dt, i] = heap.back();
			heap.pop_back();
			if (dt > down[i])
				continue;
			const int x = i % GW, z = i / GW;
			for (int k = 0; k < 16; k++) {
				const int ax = x + mx[k], az = z + mz[k];
				if (ax < 0 || az < 0 || ax >= GW || az >= GH)
					continue;
				const int n = az * GW + ax;
				if (!riv[n])
					continue;
				if (k >= 8) { // a knight's move: through river water only
					const int hx = x + (mx[k] / 2), hz = z + (mz[k] / 2);
					const int ox = x + mx[k] - (mx[k] / 2), oz = z + mz[k] - (mz[k] / 2);
					if (!riv[(size_t)hz * GW + hx] || !riv[(size_t)oz * GW + ox])
						continue;
				}
				const float nd = dt + std::sqrt((float)(mx[k] * mx[k] + mz[k] * mz[k])) * cell;
				if (nd < down[n]) {
					down[n] = nd;
					heap.push_back({ nd, n });
					std::push_heap(heap.begin(), heap.end(), std::greater<>());
				}
			}
		}
	};
	// Each river network (river texels, connected) flows into the largest wide water it meets,
	// or off the map's edge: a small lake on its way feeds it instead of drawing it both ways
	{
		std::vector<int> wide(N, -1), wideSize;
		for (int i = 0; i < (int)N; i++) {
			if (!wet[i] || riv[i] || wide[i] >= 0)
				continue;
			const int c = (int)wideSize.size();
			std::vector<int> stack = { i };
			wide[i] = c;
			int size = 0;
			while (!stack.empty()) {
				const int t = stack.back();
				stack.pop_back();
				size++;
				const int x = t % GW, z = t / GW;
				for (int k = 0; k < 4; k++) {
					const int ax = x + mx[k], az = z + mz[k];
					if (ax < 0 || az < 0 || ax >= GW || az >= GH)
						continue;
					const int n = az * GW + ax;
					if (wet[n] && !riv[n] && wide[n] < 0) {
						wide[n] = c;
						stack.push_back(n);
					}
				}
			}
			wideSize.push_back(size);
		}
		std::vector<uint8_t> inNet(N, 0);
		for (int i = 0; i < (int)N; i++) {
			if (!riv[i] || inNet[i])
				continue;
			std::vector<int> net = { i };
			inNet[i] = 1;
			for (size_t c = 0; c < net.size(); c++) {
				const int x = net[c] % GW, z = net[c] / GW;
				for (int k = 0; k < 4; k++) {
					const int ax = x + mx[k], az = z + mz[k];
					if (ax < 0 || az < 0 || ax >= GW || az >= GH)
						continue;
					const int n = az * GW + ax;
					if (riv[n] && !inNet[n]) {
						inNet[n] = 1;
						net.push_back(n);
					}
				}
			}
			// its outlet: the map's edge (-2) above all, else its largest wide water
			int best = -1;
			auto outletOf = [&](int t) {
				const int x = t % GW, z = t / GW;
				int o = -1;
				if (x == 0 || z == 0 || x == GW - 1 || z == GH - 1)
					return -2;
				for (int k = 0; k < 4; k++) {
					const int n = (z + mz[k]) * GW + x + mx[k];
					if (wide[n] >= 0 && (o < 0 || wideSize[wide[n]] > wideSize[o]))
						o = wide[n];
				}
				return o;
			};
			auto better = [&](int a, int b) { return a == -2 ? b != -2 : a >= 0 && b != -2 && (b < 0 || wideSize[a] > wideSize[b]); };
			for (int t : net) {
				const int o = outletOf(t);
				if (better(o, best))
					best = o;
			}
			std::vector<Item> seeds;
			for (int t : net)
				if (best != -1) {
					const int o = outletOf(t);
					bool touches = o == best;
					if (!touches && best >= 0) { // any wide neighbour of that water, not only the largest
						const int x = t % GW, z = t / GW;
						for (int k = 0; k < 4 && !touches; k++) {
							const int ax = x + mx[k], az = z + mz[k];
							if (ax >= 0 && az >= 0 && ax < GW && az < GH && wide[(size_t)az * GW + ax] == best)
								touches = true;
						}
					}
					if (touches) {
						down[t] = 0;
						seeds.push_back({ 0.0f, t });
					}
				}
			march(seeds);
		}
	}
	// rivers without a mouth: towards their widest point
	for (int i = 0; i < (int)N; i++) {
		if (!riv[i] || down[i] < 1e29f)
			continue;
		std::vector<int> comp = { i };
		std::vector<uint8_t> seen(N, 0);
		seen[i] = 1;
		int widest = i;
		for (size_t c = 0; c < comp.size(); c++) {
			const int x = comp[c] % GW, z = comp[c] / GW;
			if (bank[comp[c]] > bank[widest])
				widest = comp[c];
			for (int k = 0; k < 4; k++) {
				const int ax = x + mx[k], az = z + mz[k];
				if (ax < 0 || az < 0 || ax >= GW || az >= GH)
					continue;
				const int n = az * GW + ax;
				if (riv[n] && !seen[n] && down[n] >= 1e29f) {
					seen[n] = 1;
					comp.push_back(n);
				}
			}
		}
		down[widest] = 0;
		march({ { 0.0f, widest } });
	}
	// the way downstream: down the distance, smoothed (three 3 x 3 passes) and of unit length
	std::vector<float> tx(N, 0.0f), tz(N, 0.0f);
	for (int i = 0; i < (int)N; i++) {
		if (!riv[i])
			continue;
		const int x = i % GW, z = i / GW;
		auto at = [&](int ax, int az) {
			if (ax < 0 || az < 0 || ax >= GW || az >= GH || !riv[(size_t)az * GW + ax])
				return down[i];
			return down[(size_t)az * GW + ax];
		};
		tx[i] = at(x - 1, z) - at(x + 1, z);
		tz[i] = at(x, z - 1) - at(x, z + 1);
	}
	for (int pass = 0; pass < 3; pass++) {
		std::vector<float> ax(N, 0.0f), az(N, 0.0f);
		for (int i = 0; i < (int)N; i++) {
			if (!riv[i])
				continue;
			const int x = i % GW, z = i / GW;
			for (int dz = -1; dz <= 1; dz++)
				for (int dx = -1; dx <= 1; dx++) {
					const int bx = x + dx, bz = z + dz;
					if (bx < 0 || bz < 0 || bx >= GW || bz >= GH || !riv[(size_t)bz * GW + bx])
						continue;
					ax[i] += tx[(size_t)bz * GW + bx];
					az[i] += tz[(size_t)bz * GW + bx];
				}
		}
		tx = std::move(ax);
		tz = std::move(az);
	}
	// the half width: the largest bank distance, falling off by 0.15 per unit away from it (so
	// it reaches the banks at 85% and changes gently along the river)
	if (halfWidth) {
		std::vector<float>& hw = *halfWidth;
		std::vector<Item> heap;
		for (int i = 0; i < (int)N; i++)
			if (wet[i]) {
				hw[i] = bank[i];
				heap.push_back({ -bank[i], i });
			}
		std::make_heap(heap.begin(), heap.end(), std::greater<>());
		// largest first: a max-heap by -value under std::greater is a min-heap of -value
		while (!heap.empty()) {
			std::pop_heap(heap.begin(), heap.end(), std::greater<>());
			const auto [nv, i] = heap.back();
			heap.pop_back();
			if (-nv < hw[i])
				continue;
			const int x = i % GW, z = i / GW;
			for (int k = 0; k < 8; k++) {
				const int ax = x + mx[k], az = z + mz[k];
				if (ax < 0 || az < 0 || ax >= GW || az >= GH)
					continue;
				const int n = az * GW + ax;
				if (!wet[n])
					continue;
				const float v = hw[i] - 0.15f * cell * (k < 4 ? 1.0f : 1.41421f);
				if (v > hw[n]) {
					hw[n] = v;
					heap.push_back({ -v, n });
					std::push_heap(heap.begin(), heap.end(), std::greater<>());
				}
			}
		}
		// onto the land beside, for the filtering
		for (int pass = 0; pass < 4; pass++) {
			std::vector<float> next = hw;
			for (int i = 0; i < (int)N; i++) {
				if (wet[i])
					continue;
				const int x = i % GW, z = i / GW;
				float m = 0;
				for (int k = 0; k < 8; k++) {
					const int ax = x + mx[k], az = z + mz[k];
					if (ax >= 0 && az >= 0 && ax < GW && az < GH)
						m = std::max(m, hw[(size_t)az * GW + ax]);
				}
				next[i] = m;
			}
			hw = std::move(next);
		}
	}
	std::vector<uint8_t> done(N, 0);
	for (int i = 0; i < (int)N; i++) {
		if (!riv[i] || down[i] >= 1e29f)
			continue;
		const float len = std::sqrt(tx[i] * tx[i] + tz[i] * tz[i]);
		out[i * 4] = down[i];
		out[i * 4 + 1] = bank[i];
		out[i * 4 + 2] = len > 1e-6f ? tx[i] / len : 0.0f;
		out[i * 4 + 3] = len > 1e-6f ? tz[i] / len : 0.0f;
		done[i] = 1;
	}
	// out from the rivers: their values carried six texels into wide water and onto the land
	// (the bank's distance 0 on land)
	for (int pass = 0; pass < 6; pass++) {
		std::vector<uint8_t> next = done;
		for (int i = 0; i < (int)N; i++) {
			if (done[i])
				continue;
			const int x = i % GW, z = i / GW;
			float acc[4] = {};
			int n = 0;
			for (int dz = -1; dz <= 1; dz++)
				for (int dx = -1; dx <= 1; dx++) {
					const int bx = x + dx, bz = z + dz;
					if (bx < 0 || bz < 0 || bx >= GW || bz >= GH || !done[(size_t)bz * GW + bx])
						continue;
					for (int c = 0; c < 4; c++)
						acc[c] += out[((size_t)bz * GW + bx) * 4 + c];
					n++;
				}
			if (n == 0)
				continue;
			for (int c = 0; c < 4; c++)
				out[i * 4 + c] = acc[c] / n;
			if (!wet[i])
				out[i * 4 + 1] = 0;
			else
				out[i * 4 + 1] = std::min(out[i * 4 + 1], bank[i]);
			next[i] = 1;
		}
		done = std::move(next);
	}
	// debugging: "WKB_DUMP_RIVER=<file>" writes the field as a PPM: red the distance downstream
	// in bands of 10 units, green the bank's distance (up to 25 units), blue the river texels
	if (const char* dump = getenv("WKB_DUMP_RIVER")) {
		if (FILE* f = fopen(dump, "wb")) {
			fprintf(f, "P6\n%d %d\n255\n", GW, GH);
			for (size_t i = 0; i < N; i++) {
				const uint8_t px[3] = { (uint8_t)(done[i] ? std::fmod(out[i * 4], 10.0f) * 25.5f : 0),
					(uint8_t)std::min(out[i * 4 + 1] * 10.0f, 255.0f), (uint8_t)(riv[i] ? 200 : wet[i] ? 80 : 0) };
				fwrite(px, 1, 3, f);
			}
			fclose(f);
		}
	}
	return out;
}

std::vector<uint8_t> BuildWaterFlow(Terrain* terrain)
{
	const int W = (int)terrain->width, H = (int)terrain->height;
	std::vector<uint8_t> out((size_t)W * H * 4);
	for (size_t i = 0; i < (size_t)W * H; i++) {
		out[i * 4] = out[i * 4 + 1] = 128;
		out[i * 4 + 2] = 0;
		out[i * 4 + 3] = 255;
	}
	auto isWater = [&](int x, int z) { return x >= 0 && z >= 0 && x < W && z < H && terrain->getTile(x, z)->fullOfWater; };
	auto level = [&](int t) { return terrain->getTile(t % W, t / W)->waterLevel; };
	auto depth = [&](int t) {
		const int x = t % W, z = t / W;
		return level(t) - (terrain->getVertex(x, z) + terrain->getVertex(x + 1, z) + terrain->getVertex(x, z + 1) + terrain->getVertex(x + 1, z + 1)) * 0.25f;
	};
	static const int nx[8] = { 1, -1, 0, 0, 1, 1, -1, -1 }, nz[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };

	// distance to the shore, in tiles
	std::vector<int> shore((size_t)W * H, 1 << 30);
	std::vector<int> queue;
	for (int z = 0; z < H; z++)
		for (int x = 0; x < W; x++)
			if (!isWater(x, z)) {
				shore[z * W + x] = 0;
				queue.push_back(z * W + x);
			}
	for (size_t qi = 0; qi < queue.size(); qi++) {
		const int t = queue[qi], x = t % W, z = t / W;
		for (int k = 0; k < 4; k++) {
			const int ax = x + nx[k], az = z + nz[k];
			if (ax < 0 || az < 0 || ax >= W || az >= H)
				continue;
			const int a = az * W + ax;
			if (shore[a] > shore[t] + 1) {
				shore[a] = shore[t] + 1;
				queue.push_back(a);
			}
		}
	}

	std::vector<int> body((size_t)W * H, -1);
	std::vector<float> dist((size_t)W * H, 1e30f);
	std::vector<float> dirX((size_t)W * H, 0), dirZ((size_t)W * H, 0), speed((size_t)W * H, 0);
	std::vector<float> river((size_t)W * H, 0); // 1 in rivers, 0 in wide water (a sea, a lake), between off a river's mouth
	// the bodies of water: their tiles, level and widest point
	struct Body { std::vector<int> tiles, edge; float lo = 1e30f, hi = -1e30f; int widest = 0; bool border = false; };
	std::vector<Body> bodies;
	for (int start = 0; start < W * H; start++) {
		if (body[start] >= 0 || !isWater(start % W, start / W))
			continue;
		Body bd;
		bd.tiles = { start };
		body[start] = (int)bodies.size();
		for (size_t i = 0; i < bd.tiles.size(); i++) {
			const int x = bd.tiles[i] % W, z = bd.tiles[i] / W;
			for (int k = 0; k < 4; k++) {
				const int ax = x + nx[k], az = z + nz[k];
				if (isWater(ax, az) && body[az * W + ax] < 0) {
					body[az * W + ax] = (int)bodies.size();
					bd.tiles.push_back(az * W + ax);
				}
			}
		}
		for (int t : bd.tiles) {
			const int x = t % W, z = t / W;
			bd.lo = std::min(bd.lo, level(t));
			bd.hi = std::max(bd.hi, level(t));
			bd.widest = std::max(bd.widest, shore[t]);
			if (shore[t] == 1)
				bd.edge.push_back(t);
			if (x == 0 || z == 0 || x == W - 1 || z == H - 1)
				bd.border = true;
		}
		bodies.push_back(std::move(bd));
	}
	// Dijkstra over body bi from seeds (eight neighbours), through the tiles allowed
	auto distances = [&](int bi, const std::vector<int>& seeds, auto allowed, std::vector<float>& d) {
		for (int t : bodies[bi].tiles)
			d[t] = 1e30f;
		std::vector<std::pair<float, int>> heap;
		for (int t : seeds) {
			d[t] = 0;
			heap.push_back({ 0.0f, t });
		}
		std::make_heap(heap.begin(), heap.end(), std::greater<>());
		while (!heap.empty()) {
			std::pop_heap(heap.begin(), heap.end(), std::greater<>());
			const auto [dt, t] = heap.back();
			heap.pop_back();
			if (dt > d[t])
				continue;
			const int x = t % W, z = t / W;
			for (int k = 0; k < 8; k++) {
				const int ax = x + nx[k], az = z + nz[k];
				if (!isWater(ax, az) || body[az * W + ax] != bi || !allowed(az * W + ax))
					continue;
				const int n = az * W + ax;
				const float nd = dt + (k < 4 ? 1.0f : 1.41421f);
				if (nd < d[n]) {
					d[n] = nd;
					heap.push_back({ nd, n });
					std::push_heap(heap.begin(), heap.end(), std::greater<>());
				}
			}
		}
	};
	// the direction down a distance field at tile t; a neighbour outside the field counts as
	// higher (or lower, with dryIsLow: for the way to the shore)
	auto downhill = [&](int t, const std::vector<float>& d, bool dryIsLow, float& gx, float& gz) {
		const int x = t % W, z = t / W;
		auto at = [&](int ax, int az) {
			if (isWater(ax, az) && body[az * W + ax] == body[t] && d[az * W + ax] < 1e29f)
				return d[az * W + ax];
			return dryIsLow ? d[t] - 1.0f : d[t] + 1.0f;
		};
		gx = at(x - 1, z) - at(x + 1, z);
		gz = at(x, z - 1) - at(x, z + 1);
		const float len = std::sqrt(gx * gx + gz * gz);
		if (len < 1e-4f) {
			gx = gz = 0;
			return false;
		}
		gx /= len;
		gz /= len;
		return true;
	};
	std::vector<float> toOpen((size_t)W * H, 1e30f), toRiver((size_t)W * H, 1e30f), toShore((size_t)W * H, 1e30f);
	const auto everywhere = [](int) { return true; };
	int flowing = 0;
	for (int bi = 0; bi < (int)bodies.size(); bi++) {
		const Body& bd = bodies[bi];
		flowing++;
		// wide water: a third of the body's widest point from the shore (6-30 tiles), so a fjord's
		// channels are rivers of its sea and a lake's middle is its open water
		const float openAt = std::clamp(bd.widest / 3.0f, 6.0f, 30.0f);
		std::vector<int> open;
		for (int t : bd.tiles)
			if (shore[t] >= openAt)
				open.push_back(t);
		if (open.empty()) {
			// narrow all through: towards its widest part
			std::vector<int> widest;
			for (int t : bd.tiles)
				if (shore[t] == bd.widest)
					widest.push_back(t);
			distances(bi, widest, everywhere, toOpen);
			for (int t : bd.tiles) {
				downhill(t, toOpen, false, dirX[t], dirZ[t]);
				speed[t] = std::clamp((7.0f - shore[t]) / 5.0f, 0.4f, 1.0f);
				river[t] = 1.0f;
			}
			continue;
		}
		// the rivers: narrow water beyond the wide water's coastal band, flowing into it
		distances(bi, open, everywhere, toOpen);
		// (well beyond the coastal band, so that a bay of the coast stays the sea's)
		auto isRiver = [&](int t) { return shore[t] < openAt && toOpen[t] > openAt * 1.6f + 6.0f; };
		std::vector<int> rivers, coast;
		for (int t : bd.tiles) {
			if (isRiver(t))
				rivers.push_back(t);
			if (shore[t] == 1 && !isRiver(t))
				coast.push_back(t);
		}
		// how far the rivers' water reaches into the wide water, and the way to its shore
		distances(bi, rivers, [&](int n) { return !isRiver(n); }, toRiver);
		distances(bi, coast, [&](int n) { return !isRiver(n); }, toShore);
		for (int t : bd.tiles) {
			if (isRiver(t)) {
				downhill(t, toOpen, false, dirX[t], dirZ[t]);
				speed[t] = std::clamp((8.0f - shore[t]) / 5.0f, 0.45f, 1.0f);
				river[t] = 1.0f;
				continue;
			}
			// wide water: towards the nearest shore, except off a river's mouth, where the
			// river's water carries on out into it
			float sx, sz, rx = 0, rz = 0;
			downhill(t, toShore, true, sx, sz);
			const float w = rivers.empty() ? 0.0f : std::clamp(1.0f - toRiver[t] / std::max(10.0f, openAt), 0.0f, 1.0f);
			if (w > 0) {
				downhill(t, toRiver, false, rx, rz); // towards the river: its water comes the other way
				rx = -rx;
				rz = -rz;
			}
			float gx = sx * (1 - w) + rx * w, gz = sz * (1 - w) + rz * w;
			const float len = std::sqrt(gx * gx + gz * gz);
			dirX[t] = len > 1e-4f ? gx / len : 0.0f;
			dirZ[t] = len > 1e-4f ? gz / len : 0.0f;
			speed[t] = 0.3f + 0.5f * w;
			river[t] = w;
		}
	}
	const int numBodies = (int)bodies.size();
	// smoothed: the velocity (direction x speed) and the river share, ten passes of a 3 x 3
	// average over water tiles of the same body. Two passes of the bare direction had left it
	// turning from tile to tile, and the ripples carried along it tore into swirls and streaks;
	// averaging the velocity also slows the water where flows meet instead of turning it.
	for (int t = 0; t < W * H; t++) {
		dirX[t] *= speed[t];
		dirZ[t] *= speed[t];
	}
	for (int pass = 0; pass < 10; pass++) {
		std::vector<float> sx = dirX, sz = dirZ, sr = river;
		for (int t = 0; t < W * H; t++) {
			if (body[t] < 0)
				continue;
			const int x = t % W, z = t / W;
			float ax = 0, az = 0, ar = 0;
			int n = 0;
			for (int dz = -1; dz <= 1; dz++)
				for (int dx = -1; dx <= 1; dx++)
					if (isWater(x + dx, z + dz) && body[(z + dz) * W + x + dx] == body[t]) {
						const int u = (z + dz) * W + x + dx;
						ax += dirX[u];
						az += dirZ[u];
						ar += river[u];
						n++;
					}
			sx[t] = ax / n;
			sz[t] = az / n;
			sr[t] = ar / n;
		}
		dirX = std::move(sx);
		dirZ = std::move(sz);
		river = std::move(sr);
	}
	for (int t = 0; t < W * H; t++) {
		const float len = std::sqrt(dirX[t] * dirX[t] + dirZ[t] * dirZ[t]);
		speed[t] = std::min(len, 1.0f);
		if (len > 1e-4f) {
			dirX[t] /= len;
			dirZ[t] /= len;
		}
	}
	for (int t = 0; t < W * H; t++) {
		out[t * 4] = (uint8_t)std::clamp(dirX[t] * 127.5f + 128.0f, 0.0f, 255.0f);
		out[t * 4 + 1] = (uint8_t)std::clamp(dirZ[t] * 127.5f + 128.0f, 0.0f, 255.0f);
		out[t * 4 + 2] = (uint8_t)std::clamp(speed[t] * 255.0f, 0.0f, 255.0f);
		out[t * 4 + 3] = (uint8_t)std::clamp(river[t] * 255.0f, 0.0f, 255.0f);
	}
	printf("Water flow: %d bodies of water, %d flowing\n", numBodies, flowing);
	// debugging: "WKB_DUMP_FLOW=<file>" writes the flow map as a PPM (direction in red/green,
	// speed in blue) and the water tiles (body, level, depth, shore distance, river) as text beside it
	if (const char* dump = getenv("WKB_DUMP_FLOW")) {
		if (FILE* f = fopen(dump, "wb")) {
			fprintf(f, "P6\n%d %d\n255\n", W, H);
			for (int t = 0; t < W * H; t++)
				fwrite(&out[t * 4], 1, 3, f);
			fclose(f);
		}
		if (FILE* f = fopen((std::string(dump) + ".txt").c_str(), "w")) {
			for (int t = 0; t < W * H; t++)
				if (body[t] >= 0)
					fprintf(f, "%d %d %d %.2f %.2f %d %.2f\n", t % W, t / W, body[t], level(t), depth(t), shore[t], river[t]);
			fclose(f);
		}
	}
	return out;
}
