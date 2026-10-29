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
									float fy = 0.1f;
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
			float fx = (-dx1 + dx2) / 510.0f, fz = (-dz1 + dz2) / 510.0f, fy = 0.1f;
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
		constexpr int L = TerrainSplatData::LAYER_SIZE, T = L / 2;
		std::vector<uint8_t> diffuse(L * L * 4), normal(L * L * 4);
		double sum[3] = {};
		for (int q = 0; q < 4; q++) {
			const TerrainTexture* tex = textures[0];
			const int ox = (q & 1) * T, oz = (q >> 1) * T;
			const Bitmap& a = atlas(tex->file);
			const Bitmap& b = normalSource(tex->file);
			for (int cz = 0; cz < T; cz++)
				for (int cx = 0; cx < T; cx++) {
					const int sx = std::min(tex->startx + cx * tex->width / T, a.width - 1);
					const int sz = std::min(tex->starty + cz * tex->height / T, a.height - 1);
					uint8_t* d = &diffuse[((oz + cz) * L + ox + cx) * 4];
					if (a.width > 0)
						std::copy_n(&a.pixels[(sz * a.width + sx) * 4], 4, d);
					else
						d[0] = d[1] = d[2] = d[3] = 255;
					for (int c = 0; c < 3; c++)
						sum[c] += d[c];
				}
			uint8_t* n = &normal[(oz * L + ox) * 4];
			if (battles && b.width >= tex->startx + tex->width && b.height >= tex->starty + tex->height && tex->width == T && tex->height == T)
				BumpTileToNormals(&b.pixels[tex->starty * b.width + tex->startx], b.width, T, n, L);
			else if (!battles && b.width >= tex->startx + tex->width && tex->width == T && tex->height == T) {
				for (int cz = 0; cz < T; cz++)
					std::copy_n(&b.pixels[((tex->starty + cz) * b.width + tex->startx) * 4], T * 4, &normal[((oz + cz) * L + ox) * 4]);
			}
			else
				for (int cz = 0; cz < T; cz++)
					for (int cx = 0; cx < T; cx++) {
						uint8_t* o = &normal[((oz + cz) * L + ox + cx) * 4];
						o[0] = 128; o[1] = 255; o[2] = 128; o[3] = 255;
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
			for (int k = 0; k < 4; k++) {
				float u = uvs[k].first * 256.0f, v = uvs[k].second * 256.0f;
				const float du = cu > u ? 1.0f : -1.0f, dv = cv > v ? 1.0f : -1.0f;
				double c[3] = {};
				int n = 0;
				for (int i = 0; i < 12 && a.width > 0; i++)
					for (int j = 0; j < 12; j++) {
						const int px = std::clamp((int)(u + du * (i + 0.5f)), 0, a.width - 1);
						const int pz = std::clamp((int)(v + dv * (j + 0.5f)), 0, a.height - 1);
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
		splat.diffuse.emplace_back(TerrainSplatData::LAYER_SIZE * TerrainSplatData::LAYER_SIZE * 4, (uint8_t)255);
		splat.normal.emplace_back(TerrainSplatData::LAYER_SIZE * TerrainSplatData::LAYER_SIZE * 4, (uint8_t)128);
	}
	printf("Splat terrain: %zu materials\n", splat.names.size());
	return splat;
}
