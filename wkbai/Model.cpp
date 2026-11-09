// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#include "Model.h"
#include "gfx/TextureCache.h"
#include "util/util.h"
#include <mutex>
#include <algorithm>
#include <cmath>
#include <cstring>

static std::recursive_mutex g_modelPreparationMutex;

Model * ModelCache::getModel(const std::string & filename) {
	std::lock_guard<decltype(g_modelPreparationMutex)> lock(g_modelPreparationMutex);
	numCalls++;
	auto it = models.find(filename);
	if (it != models.end()) {
		numHits++;
		return it->second.get();
	}

	const char *ext = strrchr(filename.c_str(), '.');
	if (!StrCICompare(ext + 1, "mesh3") || !StrCICompare(ext + 1, "glb") || !StrCICompare(ext + 1, "gltf")) {
		StaticModel *m = new StaticModel(this, filename);
		models[filename].reset(m);
		return m;
	}
	else if (!StrCICompare(ext + 1, "anim3")) {
		AnimatedModel *m = new AnimatedModel(this, filename);
		models[filename].reset(m);
		return m;
	}
	else {
		ferr("Model file %s has wrong extension.", filename.c_str());
	}
}

Model* ModelCache::getGeneratedModel(const std::string& name, std::function<void(Mesh&)> generator)
{
	std::lock_guard<decltype(g_modelPreparationMutex)> lock(g_modelPreparationMutex);
	auto it = models.find(name);
	if (it != models.end())
		return it->second.get();
	StaticModel* m = new StaticModel(this, name);
	m->generator = std::move(generator);
	models[name].reset(m);
	return m;
}

const Material& ModelCache::getMaterial(int id)
{
	std::lock_guard<decltype(g_modelPreparationMutex)> lock(g_modelPreparationMutex);
	return materials.at(id);
}

const Material* StaticModel::getMaterials() { prepare();  return this->mesh.materials.begin(); }
const uint32_t* StaticModel::getMaterialIds() { prepare(); return this->matIds.begin(); }
size_t StaticModel::getNumMaterials() { prepare(); return this->mesh.materials.size(); }
StaticModel* StaticModel::getStaticModel() { prepare(); return this; }
Vector3 StaticModel::getSphereCenter() { prepare(); return mesh.sphereCenter; }
float StaticModel::getSphereRadius() { prepare(); return mesh.sphereRadius; }
const float* StaticModel::interpolate(uint32_t animTime) { prepare(); return mesh.vertices.data(); }
const Vector3* StaticModel::interpolateNormals(uint32_t animTime) { prepare(); return mesh.decodeNormals(); }
std::array<Vector3, 2> StaticModel::interpolateBoundingBox(uint32_t animTime) { prepare(); return { mesh.aabbLow, mesh.aabbHigh }; }
size_t StaticModel::getNumAPs() { prepare(); return mesh.attachPoints.size(); }
const AttachmentPoint& StaticModel::getAPInfo(size_t index) { prepare(); return mesh.attachPoints[index]; }
AttachmentPointState StaticModel::getAPState(size_t index, uint32_t animTime) { prepare(); return mesh.attachPoints[index].staticState; }

void StaticModel::prepare() {
	if (ready) return;
	std::lock_guard<decltype(g_modelPreparationMutex)> lock(g_modelPreparationMutex);
	if (ready) return;

	//printf("Preparing mesh %s\n", fileName.c_str());
	if (generator)
		generator(mesh);
	else
		mesh.load(fileName.c_str());

	matIds.resize(mesh.materials.size());
	for (size_t i = 0; i < matIds.size(); i++) {
		Material &mat = mesh.materials[i];
		// a tinted texture's name: the plain texture and the tint as the material's colour
		std::string base;
		float tint[3];
		if (TextureCache::splitTint(mat.textureFilename, base, tint)) {
			mat.textureFilename = base;
			for (int c = 0; c < 3; c++)
				mat.colour[c] *= tint[c];
		}
		cache->textureFilenames.insertString(mat.textureFilename);
		int texid = cache->textureFilenames.getIndex(mat.textureFilename);
		uint64_t colourKey = 0;
		for (int c = 0; c < 3; c++)
			colourKey = colourKey << 21 | (uint64_t)std::clamp((int)std::lround(mat.colour[c] * 65536.0f), 0, (1 << 21) - 1);
		auto [mi, isNew] = cache->materialIndices.try_emplace({ texid, colourKey }, (uint32_t)cache->materialIndices.size());
		uint32_t matid = (mi->second << 1) | (mat.alphaTest ? 1 : 0);
		cache->materials[matid] = mat;
		matIds[i] = matid;
	}

	ready = true;
}

const Material* AnimatedModel::getMaterials() { prepare();  return this->staticModel->getMaterials(); }
const uint32_t* AnimatedModel::getMaterialIds() { prepare(); return this->staticModel->getMaterialIds(); }
size_t AnimatedModel::getNumMaterials() { prepare(); return this->staticModel->getNumMaterials(); }
StaticModel* AnimatedModel::getStaticModel() { prepare(); return this->staticModel; }
Vector3 AnimatedModel::getSphereCenter() { prepare(); return anim.sphereCenter; }
float AnimatedModel::getSphereRadius() { prepare(); return anim.sphereRadius; }
float AnimatedModel::getDuration() { prepare(); return (float)this->anim.duration / 1000.0f; }
// the point of a triangle nearest to p (Ericson, Real-Time Collision Detection 5.1.5)
static Vector3 ClosestOnTriangle(const Vector3& p, const Vector3& a, const Vector3& b, const Vector3& c)
{
	const Vector3 ab = b - a, ac = c - a, ap = p - a;
	const float d1 = ab.dot(ap), d2 = ac.dot(ap);
	if (d1 <= 0 && d2 <= 0) return a;
	const Vector3 bp = p - b;
	const float d3 = ab.dot(bp), d4 = ac.dot(bp);
	if (d3 >= 0 && d4 <= d3) return b;
	const float vc = d1 * d4 - d3 * d2;
	if (vc <= 0 && d1 >= 0 && d3 <= 0) return a + ab * (d1 / (d1 - d3));
	const Vector3 cp = p - c;
	const float d5 = ab.dot(cp), d6 = ac.dot(cp);
	if (d6 >= 0 && d5 <= d6) return c;
	const float vb = d5 * d2 - d1 * d6;
	if (vb <= 0 && d2 >= 0 && d6 <= 0) return a + ac * (d2 / (d2 - d6));
	const float va = d3 * d6 - d5 * d4;
	if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) return b + (c - b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)));
	const float denom = 1.0f / (va + vb + vc);
	return a + ab * (vb * denom) + ac * (vc * denom);
}

// the coefficients of d = a e1 + b e2 + c n
static void FrameCoords(const Vector3& e1, const Vector3& e2, const Vector3& n, const Vector3& d, float& a, float& b, float& c)
{
	const float det = e1.dot(e2.cross(n));
	if (std::abs(det) < 1e-12f) { a = b = 0; c = d.dot(n); return; }
	a = d.dot(e2.cross(n)) / det;
	b = e1.dot(d.cross(n)) / det;
	c = e1.dot(e2.cross(d)) / det;
}

static Vector3 TriNormal(const Vector3& e1, const Vector3& e2) { return e1.cross(e2).normal(); }

const ReplacementBinding* StaticModel::getReplacementBinding()
{
	prepare();
	if (mesh.exactNormals.empty() || generator)
		return nullptr; // a Mesh3 (or a made mesh): the animation fits it as it is
	std::lock_guard<decltype(g_modelPreparationMutex)> lock(g_modelPreparationMutex);
	if (replacement)
		return replacement.get();
	replacement = std::make_unique<ReplacementBinding>();
	ReplacementBinding& rb = *replacement;
	const char* ext = strrchr(fileName.c_str(), '.');
	if (!ext || StrCICompare(ext + 1, "mesh3"))
		return &rb;
	rb.original.loadMesh3(fileName.c_str());
	const Mesh& om = rb.original;
	struct Tri { uint32_t v[3]; Vector3 c; float r; };
	std::vector<Tri> tris;
	auto pos = [&](uint32_t v) { return Vector3(om.vertices[3 * v], om.vertices[3 * v + 1], om.vertices[3 * v + 2]); };
	if (om.polyLists.size() > 0)
		for (size_t m = 0; m < om.polyLists[0].groups.size() && m < om.groupIndices.size(); m++)
			for (const auto& t : om.polyLists[0].groups[m].tupleIndex) {
				Tri tr;
				for (int j = 0; j < 3; j++) tr.v[j] = om.groupIndices[m][t[j]].vertex;
				const Vector3 a = pos(tr.v[0]), b = pos(tr.v[1]), c = pos(tr.v[2]);
				if ((b - a).cross(c - a).len3() < 1e-8f) continue;
				tr.c = (a + b + c) / 3.0f;
				tr.r = std::max({ (a - tr.c).len3(), (b - tr.c).len3(), (c - tr.c).len3() });
				tris.push_back(tr);
			}
	if (tris.empty())
		return &rb;
	// each new vertex: the nearest original triangle and its place in that triangle's frame
	std::vector<uint32_t> vertexTri(mesh.vertices.size() / 3, 0);
	rb.vertices.resize(vertexTri.size());
	for (size_t i = 0; i < vertexTri.size(); i++) {
		const Vector3 p(mesh.vertices[3 * i], mesh.vertices[3 * i + 1], mesh.vertices[3 * i + 2]);
		float best = 1e30f;
		size_t bi = 0;
		for (size_t t = 0; t < tris.size(); t++) {
			const float lower = (p - tris[t].c).len3() - tris[t].r;
			if (lower > 0 && lower * lower >= best) continue;
			const Vector3 q = ClosestOnTriangle(p, pos(tris[t].v[0]), pos(tris[t].v[1]), pos(tris[t].v[2]));
			const Vector3 d = p - q;
			const float dd = d.dot(d);
			if (dd < best) { best = dd; bi = t; }
		}
		vertexTri[i] = (uint32_t)bi;
		auto& b = rb.vertices[i];
		std::copy(tris[bi].v, tris[bi].v + 3, b.v);
		const Vector3 v0 = pos(b.v[0]), e1 = pos(b.v[1]) - v0, e2 = pos(b.v[2]) - v0;
		FrameCoords(e1, e2, TriNormal(e1, e2), p - v0, b.a, b.b, b.c);
	}
	// each new normal: the triangle of a vertex it is used with
	const Vector3* restNormals = mesh.decodeNormals();
	rb.normals.assign(mesh.exactNormals.size(), ReplacementBinding::Bind{ { tris[0].v[0], tris[0].v[1], tris[0].v[2] }, 0, 0, 1 });
	std::vector<bool> seen(rb.normals.size(), false);
	for (const auto& group : mesh.groupIndices)
		for (const auto& tuple : group) {
			if (tuple.normal >= rb.normals.size() || seen[tuple.normal] || tuple.vertex >= vertexTri.size()) continue;
			seen[tuple.normal] = true;
			auto& b = rb.normals[tuple.normal];
			const Tri& tr = tris[vertexTri[tuple.vertex]];
			std::copy(tr.v, tr.v + 3, b.v);
			const Vector3 v0 = pos(b.v[0]), e1 = pos(b.v[1]) - v0, e2 = pos(b.v[2]) - v0;
			FrameCoords(e1, e2, TriNormal(e1, e2), restNormals[tuple.normal], b.a, b.b, b.c);
		}
	rb.usable = true;
	return &rb;
}

const float* AnimatedModel::interpolate(uint32_t animTime)
{
	prepare(); staticModel->prepare();
	const ReplacementBinding* rb = staticModel->getReplacementBinding();
	if (!rb)
		return anim.interpolate(animTime, staticModel->mesh);
	if (!rb->usable || rb->original.numVertices != anim.numVertices)
		return staticModel->mesh.vertices.data(); // not animatable: the new model stands still
	if (animTime == reboundVerticesTime && !reboundVertices.empty())
		return reboundVertices.data();
	const float* ov = anim.interpolate(animTime, rb->original);
	auto pos = [&](uint32_t v) { return Vector3(ov[3 * v], ov[3 * v + 1], ov[3 * v + 2]); };
	reboundVertices.resize(rb->vertices.size() * 3);
	for (size_t i = 0; i < rb->vertices.size(); i++) {
		const auto& b = rb->vertices[i];
		const Vector3 v0 = pos(b.v[0]), e1 = pos(b.v[1]) - v0, e2 = pos(b.v[2]) - v0;
		const Vector3 p = v0 + e1 * b.a + e2 * b.b + TriNormal(e1, e2) * b.c;
		reboundVertices[3 * i] = p.x; reboundVertices[3 * i + 1] = p.y; reboundVertices[3 * i + 2] = p.z;
	}
	reboundVerticesTime = animTime;
	return reboundVertices.data();
}

const Vector3* AnimatedModel::interpolateNormals(uint32_t animTime)
{
	prepare(); staticModel->prepare();
	const ReplacementBinding* rb = staticModel->getReplacementBinding();
	if (!rb)
		return anim.interpolateNormals(animTime, staticModel->mesh);
	if (!rb->usable || rb->original.numVertices != anim.numVertices)
		return staticModel->mesh.decodeNormals();
	if (animTime == reboundNormalsTime && !reboundNormals.empty())
		return reboundNormals.data();
	const float* ov = anim.interpolate(animTime, rb->original);
	auto pos = [&](uint32_t v) { return Vector3(ov[3 * v], ov[3 * v + 1], ov[3 * v + 2]); };
	reboundNormals.resize(rb->normals.size());
	for (size_t i = 0; i < rb->normals.size(); i++) {
		const auto& b = rb->normals[i];
		const Vector3 v0 = pos(b.v[0]), e1 = pos(b.v[1]) - v0, e2 = pos(b.v[2]) - v0;
		reboundNormals[i] = (e1 * b.a + e2 * b.b + TriNormal(e1, e2) * b.c).normal();
	}
	reboundNormalsTime = animTime;
	return reboundNormals.data();
}
std::array<Vector3, 2> AnimatedModel::interpolateBoundingBox(uint32_t animTime) { prepare(); return this->anim.interpolateBoundingBox(animTime); }
size_t AnimatedModel::getNumAPs() { prepare(); return anim.attachPoints.size(); }
const AttachmentPoint& AnimatedModel::getAPInfo(size_t index) { prepare(); return staticModel->getAPInfo(index); }
AttachmentPointState AnimatedModel::getAPState(size_t index, uint32_t animTime) { prepare(); return anim.getAPState(index, animTime); }

std::pair<bool, bool> AnimatedModel::hasAPFlagSwitched(size_t index, uint32_t prevAnimTime, uint32_t nextAnimTime)
{
	prepare();
	auto& aap = anim.attachPoints[index];
	if ((int32_t)prevAnimTime < 0) prevAnimTime = 0; else prevAnimTime %= anim.duration;
	if ((int32_t)nextAnimTime < 0) nextAnimTime = 0; else nextAnimTime %= anim.duration;
	auto prevFrame = aap.getAPFrame(prevAnimTime);
	auto nextFrame = aap.getAPFrame(nextAnimTime);
	for (auto frame = prevFrame+1; frame <= nextFrame; frame++)
		if (aap.states[frame].on != aap.states[prevFrame].on)
			return { true, aap.states[frame].on };
	return { false, false };
}

void AnimatedModel::prepare() {
	if (ready) return;
	std::lock_guard<decltype(g_modelPreparationMutex)> lock(g_modelPreparationMutex);
	if (ready) return;

	//printf("Preparing anim %s\n", fileName.c_str());
	anim.load(fileName.c_str());

	std::string dir;
	if (const char *lastbs = strrchr(fileName.c_str(), '\\'))
		dir = std::string(fileName.c_str(), lastbs + 1);

	staticModel = (StaticModel*)cache->getModel(dir + anim.meshname);
	ready = true;
}
