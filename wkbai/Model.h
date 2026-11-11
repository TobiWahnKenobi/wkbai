// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#pragma once
#include <tuple>

#include <array>
#include <functional>
#include <map>
#include <string>
#include <memory>

#include "anim.h"
#include "mesh.h"
#include "util/StriCompare.h"
#include "util/DynArray.h"
#include "util/IndexedStringList.h"

//struct Mesh;
//struct Anim;
struct StaticModel;
struct ModelCache;

struct Model {
	ModelCache *cache;
	Model(ModelCache *cache) : cache(cache) {}
	virtual ~Model() {}
	virtual const Material *getMaterials() = 0;
	virtual const uint32_t *getMaterialIds() = 0;
	virtual size_t getNumMaterials() = 0;
	virtual StaticModel *getStaticModel() = 0;
	virtual Vector3 getSphereCenter() { return Vector3(0, 0, 0); }
	virtual float getSphereRadius() { return 3.0f; }
	virtual float getDuration() { return 1.5f; }
	virtual const float* interpolate(uint32_t animTime) = 0;
	virtual const Vector3* interpolateNormals(uint32_t animTime) = 0;
	virtual std::array<Vector3, 2> interpolateBoundingBox(uint32_t animTime) = 0;
	virtual size_t getNumAPs() = 0;
	virtual const AttachmentPoint& getAPInfo(size_t index) = 0;
	virtual AttachmentPointState getAPState(size_t index, uint32_t animTime) = 0;
	virtual std::pair<bool, bool> hasAPFlagSwitched(size_t index, uint32_t prevAnimTime, uint32_t nextAnimTime) { return { false, false }; }
	virtual void prepare() = 0;
};

// A glTF replacement (an HD model) of a mesh that Anim3 files animate: the original Mesh3, and
// every new vertex and normal bound to the original triangle nearest to it, as its place in that
// triangle's frame (two edges and the normal). Each key frame moves the original's triangles,
// and the new mesh follows them, so new models of mills, flags and units move as the originals.
struct ReplacementBinding {
	struct Bind { uint32_t v[3]; float a, b, c; };
	Mesh original;
	std::vector<Bind> vertices, normals; // per new vertex, per new normal
	bool usable = false;                 // the original was read and has triangles
};

struct StaticModel : Model {
	Mesh mesh;
	std::unique_ptr<ReplacementBinding> replacement; // for a glTF replacement that is animated
	// the binding (made once, for every animation of the mesh); null for a Mesh3 itself
	const ReplacementBinding* getReplacementBinding();
	DynArray<uint32_t> matIds;
	std::string fileName;
	bool ready = false;
	// a model made in code (ModelCache::getGeneratedModel): fills the mesh instead of a file
	std::function<void(Mesh&)> generator;

	StaticModel(ModelCache *cache, const std::string &fileName) : Model(cache), fileName(fileName) {}

	const Material *getMaterials() override;
	const uint32_t *getMaterialIds() override;
	size_t getNumMaterials() override;
	StaticModel *getStaticModel() override;
	Vector3 getSphereCenter() override;
	float getSphereRadius() override;
	const float* interpolate(uint32_t animTime) override;
	const Vector3* interpolateNormals(uint32_t animTime) override;
	std::array<Vector3, 2> interpolateBoundingBox(uint32_t animTime) override;
	size_t getNumAPs() override;
	const AttachmentPoint& getAPInfo(size_t index) override;
	AttachmentPointState getAPState(size_t index, uint32_t animTime) override;
	void prepare() override;

	Mesh &getMesh() {
		prepare();
		return mesh;
	}
};

struct AnimatedModel : Model {
	Anim anim;
	StaticModel *staticModel;
	// an HD replacement's vertices and normals at the last time asked (see ReplacementBinding)
	std::vector<float> reboundVertices;
	std::vector<Vector3> reboundNormals;
	uint32_t reboundVerticesTime = UINT32_MAX, reboundNormalsTime = UINT32_MAX;
	std::string fileName;
	bool ready = false;

	AnimatedModel(ModelCache *cache, const std::string &fileName) : Model(cache), fileName(fileName) {}

	const Material *getMaterials() override;
	const uint32_t *getMaterialIds() override;
	size_t getNumMaterials() override;
	StaticModel *getStaticModel() override;
	Vector3 getSphereCenter() override;
	float getSphereRadius() override;
	float getDuration() override;
	const float* interpolate(uint32_t animTime) override;
	const Vector3* interpolateNormals(uint32_t animTime) override;
	std::array<Vector3, 2> interpolateBoundingBox(uint32_t animTime) override;
	size_t getNumAPs() override;
	const AttachmentPoint& getAPInfo(size_t index) override;
	AttachmentPointState getAPState(size_t index, uint32_t animTime) override;
	std::pair<bool, bool> hasAPFlagSwitched(size_t index, uint32_t prevAnimTime, uint32_t nextAnimTime) override;
	void prepare() override;

	Anim &getAnim() {
		prepare();
		return anim;
	}
};

struct ModelCache {
	friend Model;
	friend StaticModel;
	friend AnimatedModel;
private:
	std::map<std::string, std::unique_ptr<Model>, StrCICompareClass> models;
	int numCalls = 0, numHits = 0;
	std::map<uint32_t, Material> materials;
	IndexedStringList textureFilenames;
	// (the texture's index, the colour packed, the team flags: 1 team, 2 emblem) -> the
	// material's index: a material id is that index << 1 | alpha test
	std::map<std::tuple<int, uint64_t, int>, uint32_t> materialIndices;
public:
	Model *getModel(const std::string &filename);
	// a static model made in code, by a name of its own (never a file's): made when first drawn
	Model *getGeneratedModel(const std::string &name, std::function<void(Mesh&)> generator);
	const Material& getMaterial(int id);
};