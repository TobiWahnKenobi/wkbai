// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#pragma once

#include "util/vecmat.h"
#include <string>
#include <utility>
#include <cstdint>
#include <array>
#include <vector>
#include "util/DynArray.h"

struct AttachmentPointState
{
	Vector3 position;
	std::array<float, 4> orientation;
	bool on;
};

struct AttachmentPoint
{
	std::string tag;
	AttachmentPointState staticState;
	std::string path;
	//Model *model;
};

struct Material
{
	std::string textureFilename;
	bool alphaTest;
	// the albedo times this (in the shaders' gamma space; 0-2): glTF's baseColorFactor, or a
	// "name#rrggbb.ext" texture's tint (byte / 128), which StaticModel::prepare turns into the
	// plain name and this colour (one texture on the GPU for every tint of it)
	float colour[3] = { 1.0f, 1.0f, 1.0f };
};

struct IndexTuple
{
	uint16_t vertex, normal, uv;
};

struct PolyListGroup {
	DynArray<std::array<uint16_t, 3>> tupleIndex;
};

struct PolygonList
{
	uint16_t numVerts, numNorms, numUvs;
	float distance;
	DynArray<PolyListGroup> groups;
};

struct Mesh
{
	uint16_t numVertices, numNormals, numUvs, numAttachPoints;
	DynArray<AttachmentPoint> attachPoints;
	DynArray<float> vertices;
	Vector3 sphereCenter; float sphereRadius;
	DynArray<uint16_t> vertexRemapper;
	DynArray<Material> materials;
	DynArray<DynArray<float>> uvLists;
	DynArray<DynArray<IndexTuple>> groupIndices;
	DynArray<PolygonList> polyLists;
	DynArray<uint8_t> normals;
	DynArray<uint16_t> normalRemapper;
	// a glTF model's own normals, indexed like "normals"; empty for a Mesh3. The renderers and
	// decodeNormals use them instead of the 256-entry table, so smooth and bevelled surfaces of
	// a new model shade as made (the ids in "normals" stay for code that only knows those)
	std::vector<Vector3> exactNormals;

	Vector3 aabbLow, aabbHigh;

	// a Mesh3 file; "<name>.glb" or "<name>.gltf" beside it (or in a mod) is read instead
	void load(const char *filename);
	// glTF 2.0 (gltf.cpp): a static model read into this mesh, or this mesh written as .glb
	void loadGltf(const char* filename);
	bool saveGltf(const char* path) const;
	// the Mesh3 file itself, without looking for a glTF override
	void loadMesh3(const char* filename);
	// a mesh made in code (gltf.cpp): positions (x y z each), their normals and UVs, and per
	// material its triangles (vertex indices, the front where (b - a) x (c - a) points)
	struct BuildGroup { Material mat; std::vector<uint32_t> triangles; };
	void build(const std::vector<float>& positions, const std::vector<Vector3>& normals, const std::vector<float>& uvs, const std::vector<BuildGroup>& groups);
	// the nearest of s_normalTable's
	static uint8_t nearestNormal(const Vector3& n);
	const Vector3* decodeNormals();

	Mesh() {}
	Mesh(const char *filename) { load(filename); }

	static const std::array<Vector3, 256> s_normalTable;
};