// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

// glTF 2.0 for static models (Mesh): reading .glb and .gltf (an external .bin or a data: URI
// buffer) into a Mesh, so that every renderer draws it like a Mesh3, and writing a Mesh as .glb.
//  - glTF is right-handed, the game left-handed: z is negated and the triangles turned around.
//  - Normals become the nearest of the 256 the game's meshes use (Mesh::s_normalTable).
//  - One material per primitive, its texture the base colour image. Textures are named from the
//    game set's Textures directory, so an image beside the model becomes "..\..\<its path>". A
//    material's "extras": { "wkTexture": "<name>" } names the game texture directly (the exporter
//    writes it); "alphaMode": "MASK" is the game's alpha test.
//  - Animations and skins are not read (the game's are per-vertex, Anim3).

#include "mesh.h"
#include "file.h"
#include "util/util.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace {
	std::vector<uint8_t> Base64Decode(const std::string& text)
	{
		auto value = [](char c) -> int {
			if (c >= 'A' && c <= 'Z') return c - 'A';
			if (c >= 'a' && c <= 'z') return c - 'a' + 26;
			if (c >= '0' && c <= '9') return c - '0' + 52;
			if (c == '+') return 62;
			if (c == '/') return 63;
			return -1;
		};
		std::vector<uint8_t> out;
		uint32_t acc = 0;
		int bits = 0;
		for (char c : text) {
			const int v = value(c);
			if (v < 0)
				continue;
			acc = (acc << 6) | (uint32_t)v;
			bits += 6;
			if (bits >= 8) {
				bits -= 8;
				out.push_back((uint8_t)(acc >> bits));
			}
		}
		return out;
	}

	std::string Directory(const std::string& path)
	{
		const size_t slash = path.find_last_of("\\/");
		return slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
	}

	std::vector<uint8_t> ReadFile(const std::string& path)
	{
		char* data; int size;
		LoadFile(path.c_str(), &data, &size);
		std::vector<uint8_t> v((uint8_t*)data, (uint8_t*)data + size);
		free(data);
		return v;
	}

	// row-vector 4x4 (as Matrix): column-major glTF matrices transposed on the way in
	struct Mat4 {
		float m[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
		Mat4 operator*(const Mat4& b) const {
			Mat4 r;
			for (int i = 0; i < 4; i++)
				for (int j = 0; j < 4; j++) {
					float s = 0;
					for (int k = 0; k < 4; k++)
						s += m[i * 4 + k] * b.m[k * 4 + j];
					r.m[i * 4 + j] = s;
				}
			return r;
		}
		Vector3 point(const Vector3& p) const { return Vector3(p.x * m[0] + p.y * m[4] + p.z * m[8] + m[12], p.x * m[1] + p.y * m[5] + p.z * m[9] + m[13], p.x * m[2] + p.y * m[6] + p.z * m[10] + m[14]); }
		Vector3 dir(const Vector3& p) const { return Vector3(p.x * m[0] + p.y * m[4] + p.z * m[8], p.x * m[1] + p.y * m[5] + p.z * m[9], p.x * m[2] + p.y * m[6] + p.z * m[10]); }
	};

	Mat4 NodeMatrix(const nlohmann::json& node)
	{
		Mat4 r;
		if (node.contains("matrix")) {
			// column-major for column vectors: read row by row it is the same matrix for row vectors
			for (int i = 0; i < 16; i++)
				r.m[i] = node["matrix"][i].get<float>();
			return r;
		}
		Vector3 t(0, 0, 0), s(1, 1, 1);
		float q[4] = { 0, 0, 0, 1 };
		if (node.contains("translation")) t = Vector3(node["translation"][0], node["translation"][1], node["translation"][2]);
		if (node.contains("scale")) s = Vector3(node["scale"][0], node["scale"][1], node["scale"][2]);
		if (node.contains("rotation")) for (int i = 0; i < 4; i++) q[i] = node["rotation"][i];
		const float x = q[0], y = q[1], z = q[2], w = q[3];
		// rotation matrix rows for row vectors (p' = p R)
		const float rot[9] = {
			1 - 2 * (y * y + z * z), 2 * (x * y + z * w), 2 * (x * z - y * w),
			2 * (x * y - z * w), 1 - 2 * (x * x + z * z), 2 * (y * z + x * w),
			2 * (x * z + y * w), 2 * (y * z - x * w), 1 - 2 * (x * x + y * y),
		};
		const float sc[3] = { s.x, s.y, s.z };
		for (int i = 0; i < 3; i++)
			for (int j = 0; j < 3; j++)
				r.m[i * 4 + j] = rot[i * 3 + j] * sc[i];
		r.m[12] = t.x; r.m[13] = t.y; r.m[14] = t.z;
		return r;
	}

	uint8_t NearestNormal(const Vector3& n)
	{
		int best = 0;
		float bestDot = -2;
		for (int i = 0; i < 256; i++) {
			const Vector3& t = Mesh::s_normalTable[i];
			const float d = t.x * n.x + t.y * n.y + t.z * n.z;
			if (d > bestDot) { bestDot = d; best = i; }
		}
		return (uint8_t)best;
	}
}

void Mesh::loadGltf(const char* filename)
{
	const std::vector<uint8_t> file = ReadFile(filename);
	nlohmann::json doc;
	std::vector<std::vector<uint8_t>> buffers;
	std::vector<uint8_t> glbBin;
	if (file.size() >= 12 && !memcmp(file.data(), "glTF", 4)) {
		// .glb: a JSON chunk, then a binary chunk (buffer 0)
		size_t at = 12;
		while (at + 8 <= file.size()) {
			uint32_t len, type;
			memcpy(&len, &file[at], 4);
			memcpy(&type, &file[at + 4], 4);
			if (at + 8 + len > file.size())
				break;
			if (type == 0x4E4F534A) // JSON
				doc = nlohmann::json::parse(file.begin() + at + 8, file.begin() + at + 8 + len);
			else if (type == 0x004E4942) // BIN
				glbBin.assign(file.begin() + at + 8, file.begin() + at + 8 + len);
			at += 8 + len;
		}
	}
	else
		doc = nlohmann::json::parse(file.begin(), file.end());
	const std::string dir = Directory(filename);
	for (const auto& b : doc.value("buffers", nlohmann::json::array())) {
		const std::string uri = b.value("uri", std::string());
		if (uri.empty())
			buffers.push_back(glbBin);
		else if (uri.rfind("data:", 0) == 0)
			buffers.push_back(Base64Decode(uri.substr(uri.find(',') + 1)));
		else
			buffers.push_back(ReadFile(dir + uri));
	}

	// an accessor's elements as floats (count × components)
	auto accessorFloats = [&](int index, int& components) {
		const auto& acc = doc["accessors"][index];
		const auto& view = doc["bufferViews"][acc.value("bufferView", 0)];
		const std::vector<uint8_t>& buf = buffers.at(view.value("buffer", 0));
		const std::string type = acc["type"];
		components = type == "SCALAR" ? 1 : type == "VEC2" ? 2 : type == "VEC3" ? 3 : 4;
		const int ctype = acc["componentType"];
		const int csize = (ctype == 5126 || ctype == 5125) ? 4 : (ctype == 5123 || ctype == 5122) ? 2 : 1;
		const size_t stride = view.value("byteStride", (size_t)(components * csize));
		const size_t base = view.value("byteOffset", (size_t)0) + acc.value("byteOffset", (size_t)0);
		const size_t count = acc["count"];
		const bool normalized = acc.value("normalized", false);
		std::vector<float> out(count * components);
		for (size_t i = 0; i < count; i++)
			for (int c = 0; c < components; c++) {
				const uint8_t* p = &buf.at(base + i * stride + c * csize);
				float v = 0;
				switch (ctype) {
				case 5126: memcpy(&v, p, 4); break;
				case 5125: { uint32_t u; memcpy(&u, p, 4); v = (float)u; break; }
				case 5123: { uint16_t u; memcpy(&u, p, 2); v = normalized ? u / 65535.0f : (float)u; break; }
				case 5122: { int16_t u; memcpy(&u, p, 2); v = normalized ? std::max(u / 32767.0f, -1.0f) : (float)u; break; }
				case 5121: v = normalized ? *p / 255.0f : (float)*p; break;
				case 5120: v = normalized ? std::max(*(const int8_t*)p / 127.0f, -1.0f) : (float)*(const int8_t*)p; break;
				}
				out[i * components + c] = v;
			}
		return out;
	};

	std::vector<float> positions, uvs;
	std::vector<uint8_t> normalIds;
	struct Group { Material mat; std::vector<uint32_t> triangles; };
	std::vector<Group> groups;

	auto addPrimitive = [&](const nlohmann::json& prim, const Mat4& world) {
		if (prim.value("mode", 4) != 4 || !prim["attributes"].contains("POSITION"))
			return;
		int comps;
		const std::vector<float> pos = accessorFloats(prim["attributes"]["POSITION"], comps);
		const size_t count = pos.size() / 3;
		std::vector<float> nrm, uv;
		if (prim["attributes"].contains("NORMAL")) nrm = accessorFloats(prim["attributes"]["NORMAL"], comps);
		if (prim["attributes"].contains("TEXCOORD_0")) uv = accessorFloats(prim["attributes"]["TEXCOORD_0"], comps);
		const uint32_t first = (uint32_t)(positions.size() / 3);
		for (size_t i = 0; i < count; i++) {
			const Vector3 p = world.point(Vector3(pos[i * 3], pos[i * 3 + 1], pos[i * 3 + 2]));
			positions.insert(positions.end(), { p.x, p.y, -p.z });
			Vector3 n = nrm.empty() ? Vector3(0, 1, 0) : world.dir(Vector3(nrm[i * 3], nrm[i * 3 + 1], nrm[i * 3 + 2]));
			n.z = -n.z;
			const float len = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
			normalIds.push_back(NearestNormal(len > 0 ? n * (1.0f / len) : Vector3(0, 1, 0)));
			uvs.push_back(uv.empty() ? 0.0f : uv[i * 2]);
			uvs.push_back(uv.empty() ? 0.0f : uv[i * 2 + 1]);
		}
		Group g;
		g.mat.alphaTest = false;
		if (prim.contains("material")) {
			const auto& m = doc["materials"][(int)prim["material"]];
			g.mat.alphaTest = m.value("alphaMode", std::string()) == "MASK";
			if (m.contains("extras") && m["extras"].contains("wkTexture"))
				g.mat.textureFilename = m["extras"]["wkTexture"];
			else if (m.contains("pbrMetallicRoughness") && m["pbrMetallicRoughness"].contains("baseColorTexture")) {
				const auto& tex = doc["textures"][(int)m["pbrMetallicRoughness"]["baseColorTexture"]["index"]];
				const std::string uri = doc["images"][(int)tex["source"]].value("uri", std::string());
				// from the game set's Textures directory, two levels down from the game's root
				std::string path = dir + uri;
				std::replace(path.begin(), path.end(), '/', '\\');
				g.mat.textureFilename = "..\\..\\" + path;
			}
		}
		if (prim.contains("indices")) {
			const std::vector<float> idx = accessorFloats(prim["indices"], comps);
			for (size_t i = 0; i + 2 < idx.size(); i += 3)
				g.triangles.insert(g.triangles.end(), { first + (uint32_t)idx[i], first + (uint32_t)idx[i + 2], first + (uint32_t)idx[i + 1] });
		}
		else
			for (uint32_t i = 0; i + 2 < count; i += 3)
				g.triangles.insert(g.triangles.end(), { first + i, first + i + 2, first + i + 1 });
		groups.push_back(std::move(g));
	};
	auto addNode = [&](int index, const Mat4& parent, auto& self) -> void {
		const auto& node = doc["nodes"][index];
		const Mat4 world = NodeMatrix(node) * parent;
		if (node.contains("mesh"))
			for (const auto& prim : doc["meshes"][(int)node["mesh"]]["primitives"])
				addPrimitive(prim, world);
		for (const auto& child : node.value("children", nlohmann::json::array()))
			self(child.get<int>(), world, self);
	};
	const int sceneIndex = doc.value("scene", 0);
	if (doc.contains("scenes") && sceneIndex < (int)doc["scenes"].size()) {
		for (const auto& n : doc["scenes"][sceneIndex].value("nodes", nlohmann::json::array()))
			addNode(n.get<int>(), Mat4(), addNode);
	}
	else if (doc.contains("meshes"))
		for (const auto& prim : doc["meshes"][0]["primitives"])
			addPrimitive(prim, Mat4());

	const size_t numV = positions.size() / 3;
	if (numV > 65535)
		ferr("glTF model %s has %zu vertices; at most 65535 are supported.", filename, numV);

	numVertices = numNormals = numUvs = (uint16_t)numV;
	numAttachPoints = 0;
	attachPoints.resize(0);
	vertices.resize(positions.size());
	std::copy(positions.begin(), positions.end(), vertices.begin());
	vertexRemapper.resize(numV);
	normalRemapper.resize(numV);
	normals.resize(numV);
	for (size_t i = 0; i < numV; i++) {
		vertexRemapper[i] = normalRemapper[i] = (uint16_t)i;
		normals[i] = normalIds[i];
	}
	uvLists.resize(1);
	uvLists[0].resize(uvs.size());
	std::copy(uvs.begin(), uvs.end(), uvLists[0].begin());
	materials.resize(groups.size());
	groupIndices.resize(groups.size());
	polyLists.resize(1);
	polyLists[0].numVerts = polyLists[0].numNorms = polyLists[0].numUvs = (uint16_t)numV;
	polyLists[0].distance = 0;
	polyLists[0].groups.resize(groups.size());
	for (size_t g = 0; g < groups.size(); g++) {
		materials[g] = groups[g].mat;
		// a tuple per vertex the group uses, and its triangles through them
		std::vector<int> tupleOf(numV, -1);
		std::vector<IndexTuple> tuples;
		std::vector<std::array<uint16_t, 3>> tris;
		for (size_t t = 0; t + 2 < groups[g].triangles.size(); t += 3) {
			std::array<uint16_t, 3> tri;
			for (int k = 0; k < 3; k++) {
				const uint32_t v = groups[g].triangles[t + k];
				if (tupleOf[v] < 0) {
					tupleOf[v] = (int)tuples.size();
					tuples.push_back({ (uint16_t)v, (uint16_t)v, (uint16_t)v });
				}
				tri[k] = (uint16_t)tupleOf[v];
			}
			tris.push_back(tri);
		}
		groupIndices[g].resize(tuples.size());
		std::copy(tuples.begin(), tuples.end(), groupIndices[g].begin());
		polyLists[0].groups[g].tupleIndex.resize(tris.size());
		std::copy(tris.begin(), tris.end(), polyLists[0].groups[g].tupleIndex.begin());
	}

	static const float inf = std::numeric_limits<float>::infinity();
	aabbLow = Vector3(inf, inf, inf);
	aabbHigh = -Vector3(inf, inf, inf);
	for (size_t v = 0; v < numV; v++)
		for (int c = 0; c < 3; c++) {
			aabbLow.coord[c] = std::min(aabbLow.coord[c], positions[v * 3 + c]);
			aabbHigh.coord[c] = std::max(aabbHigh.coord[c], positions[v * 3 + c]);
		}
	if (numV == 0)
		aabbLow = aabbHigh = Vector3(0, 0, 0);
	sphereCenter = (aabbLow + aabbHigh) * 0.5f;
	sphereRadius = 0;
	for (size_t v = 0; v < numV; v++) {
		const Vector3 p(positions[v * 3], positions[v * 3 + 1], positions[v * 3 + 2]);
		sphereRadius = std::max(sphereRadius, (p - sphereCenter).len3());
	}
}

bool Mesh::saveGltf(const char* path) const
{
	// one primitive per material group, from the most detailed polygon list; the first UV list
	nlohmann::json doc;
	doc["asset"] = { { "version", "2.0" }, { "generator", "wkbai" } };
	std::vector<uint8_t> bin;
	auto append = [&](const void* data, size_t size) {
		while (bin.size() % 4) bin.push_back(0);
		const size_t at = bin.size();
		bin.insert(bin.end(), (const uint8_t*)data, (const uint8_t*)data + size);
		return at;
	};
	nlohmann::json primitives = nlohmann::json::array(), accessors = nlohmann::json::array(), views = nlohmann::json::array(), mats = nlohmann::json::array();
	auto addView = [&](size_t offset, size_t length, int target) {
		views.push_back({ { "buffer", 0 }, { "byteOffset", offset }, { "byteLength", length }, { "target", target } });
		return (int)views.size() - 1;
	};
	if (polyLists.size() == 0 || uvLists.size() == 0)
		return false;
	const PolygonList& pl = polyLists[0];
	for (size_t g = 0; g < groupIndices.size() && g < pl.groups.size(); g++) {
		const DynArray<IndexTuple>& tuples = groupIndices[g];
		std::vector<float> pos, nrm, uv;
		Vector3 lo(1e30f, 1e30f, 1e30f), hi(-1e30f, -1e30f, -1e30f);
		for (const IndexTuple& t : tuples) {
			const Vector3 p(vertices[t.vertex * 3], vertices[t.vertex * 3 + 1], -vertices[t.vertex * 3 + 2]);
			pos.insert(pos.end(), { p.x, p.y, p.z });
			lo = Vector3(std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z));
			hi = Vector3(std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z));
			const Vector3 n = s_normalTable[t.normal < normals.size() ? normals[t.normal] : 0];
			nrm.insert(nrm.end(), { n.x, n.y, -n.z });
			uv.insert(uv.end(), { uvLists[0][t.uv * 2], uvLists[0][t.uv * 2 + 1] });
		}
		std::vector<uint16_t> idx;
		for (const auto& tri : pl.groups[g].tupleIndex)
			idx.insert(idx.end(), { tri[0], tri[2], tri[1] });
		if (tuples.size() == 0 || idx.empty())
			continue;
		const int pv = addView(append(pos.data(), pos.size() * 4), pos.size() * 4, 34962);
		const int nv = addView(append(nrm.data(), nrm.size() * 4), nrm.size() * 4, 34962);
		const int uvv = addView(append(uv.data(), uv.size() * 4), uv.size() * 4, 34962);
		const int iv = addView(append(idx.data(), idx.size() * 2), idx.size() * 2, 34963);
		const size_t n = tuples.size();
		accessors.push_back({ { "bufferView", pv }, { "componentType", 5126 }, { "count", n }, { "type", "VEC3" },
			{ "min", { lo.x, lo.y, lo.z } }, { "max", { hi.x, hi.y, hi.z } } });
		accessors.push_back({ { "bufferView", nv }, { "componentType", 5126 }, { "count", n }, { "type", "VEC3" } });
		accessors.push_back({ { "bufferView", uvv }, { "componentType", 5126 }, { "count", n }, { "type", "VEC2" } });
		accessors.push_back({ { "bufferView", iv }, { "componentType", 5123 }, { "count", idx.size() }, { "type", "SCALAR" } });
		const int a = (int)accessors.size() - 4;
		const Material& m = materials[g];
		nlohmann::json mat = { { "name", m.textureFilename }, { "extras", { { "wkTexture", m.textureFilename } } },
			{ "pbrMetallicRoughness", { { "metallicFactor", 0.0 }, { "roughnessFactor", 1.0 } } } };
		if (m.alphaTest)
			mat["alphaMode"] = "MASK";
		mats.push_back(mat);
		primitives.push_back({ { "attributes", { { "POSITION", a }, { "NORMAL", a + 1 }, { "TEXCOORD_0", a + 2 } } }, { "indices", a + 3 },
			{ "material", (int)mats.size() - 1 } });
	}
	while (bin.size() % 4) bin.push_back(0);
	doc["buffers"] = { { { "byteLength", bin.size() } } };
	doc["bufferViews"] = views;
	doc["accessors"] = accessors;
	doc["materials"] = mats;
	doc["meshes"] = { { { "primitives", primitives } } };
	doc["nodes"] = { { { "mesh", 0 } } };
	doc["scenes"] = { { { "nodes", { 0 } } } };
	doc["scene"] = 0;
	std::string json = doc.dump();
	while (json.size() % 4) json.push_back(' ');
	std::ofstream f(path, std::ios::binary);
	auto u32 = [&](uint32_t v) { f.write((const char*)&v, 4); };
	f.write("glTF", 4);
	u32(2);
	u32((uint32_t)(12 + 8 + json.size() + 8 + bin.size()));
	u32((uint32_t)json.size());
	u32(0x4E4F534A);
	f.write(json.data(), (std::streamsize)json.size());
	u32((uint32_t)bin.size());
	u32(0x004E4942);
	f.write((const char*)bin.data(), (std::streamsize)bin.size());
	return (bool)f;
}
