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
	// the albedo times this (in the shaders' gamma space; 0-2): glTF's baseColorFactor (the HD
	// models' tints of their shared textures: one texture on the GPU for every tint of it)
	float colour[3] = { 1.0f, 1.0f, 1.0f };
	// HD models' team colour (docs/engine/gltf.md "Team colours of HD models"), instead of the
	// original's UV list per colour: team, glTF's "extras": { "wkTeam": true }: the albedo times
	// the owner's colour (TeamColours); emblem, "wkEmblem": true: the owner's emblem
	// (TeamEmblems) laid over the albedo where the mesh's emblemUvs fall in 0..1
	bool team = false;
	bool emblem = false;
};

// A chimney's smoke (gfx/ChimneySmoke.cpp; docs/engine/gltf.md "Chimney smoke"): where it comes
// out, in the model's space, and when. From a glTF's "wkSmoke" nodes, the Mesh3's PS_Chimney
// attachment points (their when from the building type), or ChimneySmoke's table of models
// without either (the houses).
struct SmokeEmitter
{
	enum When : uint8_t { NIGHT_WINTER, ALWAYS, DAY };
	Vector3 position;
	float rate = 1.0f;   // puffs per second (the default about 1.3 puffs a second), 0.1-10
	float size = 1.0f;   // the puffs' size, 1 the default's (1 m at the chimney, about 6 m at the end)
	When when = NIGHT_WINTER;
	static bool parseWhen(const std::string& text, When& out);
	static const char* whenName(When w);
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
	// a glTF model's emblem UVs (its wkEmblem materials' TEXCOORD_1), indexed like a UV list;
	// -1 (no emblem) on the other materials' vertices. Empty without an emblem material.
	std::vector<float> emblemUvs;
	// a glTF model's smoke emitters (its nodes with "wkSmoke"); smokeDeclared: it has at least one
	// such node ("wkSmoke": false declares no smoke), and they replace the Mesh3's PS_Chimney points
	std::vector<SmokeEmitter> smokeEmitters;
	bool smokeDeclared = false;

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