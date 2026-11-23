// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#pragma once
#include <functional>
#include <string>
#include <vector>

// Ray-traced sun shadows of the buildings, trees, units and the terrain (RenderSettings::rtShadows; RE
// repository docs/engine/gi-clouds.md), with hardware ray tracing through Diligent (inline ray
// queries, DXR 1.1 / VK_KHR_ray_query): instead of the static shadow map.
//
// The scene renderer adds every shadow caster it draws (SEFLAG_STATIC_SHADOW or
// SEFLAG_DYNAMIC_SHADOW) with the bottom-level structure of its mesh, built once per mesh, or
// for an animated model once per key frame (the instance takes its current key frame's), and
// builds the frame's top-level structure. Before the post-processing, one ray per pixel from
// the scene's depth towards the sun (jittered inside the sun's disc, so the temporal
// anti-aliasing softens the edges) gives the shadow mask, which the composite applies.
// Alpha-tested materials (leaves) are non-opaque geometry: a candidate hit on one counts only
// where its texture's alpha passes the raster pass's test (0.94), from the triangle's UVs
// (copied once per mesh) and the texture (one of up to MAX_ALPHA_TEXTURES).
//
// The same scene gives the water's ray-traced reflections (RenderSettings::rtReflections): the
// terrain renderer draws its flat water surface once more through beginWaterReflections, one
// mirror ray per pixel; what it hits takes its colour from the frame drawn before the water
// where that point is on the screen (else a dark silhouette in the sky's colour).

#include "RefCntAutoPtr.hpp"
#include "RenderDevice.h"
#include "DeviceContext.h"
#include "BottomLevelAS.h"
#include "TopLevelAS.h"
#include "../util/vecmat.h"
#include <map>
#include <string>
#include <vector>

class Terrain;

class DiligentRayShadows {
public:
	// whether the device can (inline ray tracing, DXC for the mask's shader)
	static bool Supported(Diligent::IRenderDevice* device);
	DiligentRayShadows(Diligent::IRenderDevice* device);
	~DiligentRayShadows();

	// a material group; alpha-tested ones also with their texture and per triangle its corners'
	// UVs (6 floats), filled before the mesh's first structure is built
	struct Group {
		uint32_t firstIndex, indexCount, firstVertex, vertexCount;
		Diligent::RefCntAutoPtr<Diligent::ITextureView> alphaTexture;
		std::vector<float> alphaUVs;
	};
	static constexpr uint32_t MAX_ALPHA_TEXTURES = 128;
	// a mesh's structure: key (the mesh, or the animation and key frame), its positions (float3,
	// from the byte offset), 16-bit indices per group relative to the group's first vertex
	// animated: an animation's key frame, dropped when unused for a while (evictAnimated)
	// The structures themselves (GPU allocations, which took 25-140 ms each on the play test's
	// save, the more the slower) are made on a worker thread and taken from the key's pool:
	// null while the key's first one is being made (the caster then casts no shadow for a frame
	// or two); an animation has at most POSES_PER_KEY of them, built again for new poses
	Diligent::IBottomLevelAS* blas(Diligent::IDeviceContext* ctx, const void* key, uint32_t frame, Diligent::IBuffer* positions,
		uint64_t positionOffset, uint32_t vertexCount, Diligent::IBuffer* indices, const std::vector<Group>& groups, bool animated = false);
	// the structure if made already (an animated one marked as used now)
	Diligent::IBottomLevelAS* cached(const void* key, uint32_t frame);
	// an animated pose's structure from its positions (float3 per vertex, in the mesh's order):
	// copied into one shared buffer (none made per pose), built into a structure of the key's
	// pool (a spare one, else its pose unused longest, at most POSES_PER_KEY); null: none free
	Diligent::IBottomLevelAS* pose(Diligent::IDeviceContext* ctx, const void* key, uint32_t frame, const float* positions,
		uint32_t vertexCount, Diligent::IBuffer* indices, const std::vector<Group>& groups);
	// whether pose would find a structure for key now (else one is asked of the worker)
	bool poseSlot(const void* key, const std::vector<Group>& groups);
	// the cached pose of key nearest to frame, at most range away (marked as used), or null: a
	// stand-in while frame's own pose waits for a structure or the frame's allowance
	Diligent::IBottomLevelAS* cachedNear(const void* key, uint32_t frame, uint32_t range);
	static constexpr int POSES_PER_KEY = 16;

	// The terrain as a caster too (hills shading the valleys): its height grid, two triangles per
	// tile, built again when version changes (the terrain renderer's heights). The terrain
	// renderer draws a smooth (tessellated) surface through the same vertices, which lies under
	// these flat triangles in hollows: the terrain's triangles are hit from their upper side only
	// (back faces culled), so a ray starting a little under them goes through; the models stay
	// two-sided. Only the shadow mask sees the terrain (instance mask), not the reflections.
	// null: no terrain.
	void setTerrain(Diligent::IDeviceContext* ctx, const Terrain* terrain, uint64_t version);

	// (and the animated structures unused for ANIMATED_KEEP frames back into their pool; a
	// pool whose animation went unused that long freed, at most MAX_FREES_PER_FRAME a frame;
	// the worker's new structures taken in)
	void beginFrame();
	static constexpr uint64_t ANIMATED_KEEP = 600;
	static constexpr int MAX_FREES_PER_FRAME = 8;
	// a pose's structure is built again for another only once no frame in flight can use it
	static constexpr uint64_t IN_FLIGHT = 8;
	// a caster this frame (its world transform, row vectors)
	void add(Diligent::IBottomLevelAS* blas, const Matrix& transform);
	// The forest (Forest.h) as still casters: a structure of their own with the terrain, built in
	// full when the forest was rebuilt (or its slots or the terrain changed), else updated for the
	// slots changed, at most every 30 frames. blasOf: a tree model's structure (null: not ready).
	// The shaders trace it and the frame's structure of the other casters (build).
	void setForest(Diligent::IDeviceContext* ctx, const struct Forest* forest, bool rebuilt, const std::vector<uint32_t>& changed,
		const std::function<Diligent::IBottomLevelAS*(const struct ForestTree&)>& blasOf);
	// beginFrame's count (SceneEntity::rtFrame)
	uint64_t frame() const { return frameCounter; }

	// The trees in the wind (TreeWind.h): per tree mesh (key) one bent structure per wind group,
	// all of a mesh's groups bent by one compute pass and refitted every frame (windBegin, with
	// the frame's wind: WindBend); their bounds held fixed (two tiny triangles at the corners of
	// the farthest a bend reaches), so the still structure of the forest that holds them needn't
	// be built again as they bend. rest: the mesh's positions (asked for once). nullptr until
	// made, and with the wind off
	void windBegin(Diligent::IDeviceContext* ctx, const float wind[4]);
	Diligent::IBottomLevelAS* windBlas(Diligent::IDeviceContext* ctx, const void* key, const std::function<std::vector<Vector3>()>& rest,
		Diligent::IBuffer* indices, const std::vector<Group>& groups, uint32_t group);
	bool windOn() const { return windNow[3] > 0 && windPipeline; }
	// the frame's top-level structure from the casters added (kept when they are the last
	// build's: the same structures at the same places)
	void build(Diligent::IDeviceContext* ctx);
	// the shadow mask (R8: 1 sunlit, 0 in shadow) of the scene's depth, for its size;
	// projection: the camera's (P11, P22, P33, P43), inverseView: view -> world (row vectors);
	// fogBuffer: the renderers' FogBuffer (the shadows fade into the fog); heights: the terrain's
	// vertex heights (R32F, (width + 1) x (height + 1); null: none) and its map edge in tiles
	Diligent::ITextureView* renderMask(Diligent::IDeviceContext* ctx, Diligent::ITexture* depth, const float projection[4],
		const Matrix& inverseView, const Vector3& sunDirection, uint64_t frameIndex, Diligent::IBuffer* fogBuffer,
		Diligent::ITextureView* heights, int terrainEdge);
	// starts the water's reflection pass: the target (RGBA16F, the frame's size, cleared) and the
	// pipeline are set with the depth target; the caller draws its water surface (float3
	// position, float depth per vertex) with transformBuffer's view-projection, then restores
	// its targets. scene and sceneDepth: the frame before the water; projZ: the projection's
	// m22 and m32. The reflections' view (rgb, a: how much they show), or null.
	Diligent::ITextureView* beginWaterReflections(Diligent::IDeviceContext* ctx, Diligent::IBuffer* transformBuffer,
		Diligent::ITexture* scene, Diligent::ITexture* sceneDepth, Diligent::ITextureView* dsv, const Vector3& camera,
		float projZ22, float projZ32, const float fallback[3]);
	// the reflections on buildings and units, Ultra (RenderSettings::modelReflections): at half
	// the depth's size, per pixel the mirror ray of its surface (the position and the normal
	// from the depth pre-pass's depth: the shape without the normal maps), the nearest model
	// it hits; rgb the colour there in the frame before (prevScene, graded as drawn, through
	// prevViewProj; transposed), a: 0 no hit, else 1 + how much of that colour is known (the
	// hit on the screen then and in sight now; 0: off the screen, the shaders darken it).
	// projection, inverseView as renderMask's; viewProj: this frame's (transposed). Or null.
	Diligent::ITextureView* renderModelReflections(Diligent::IDeviceContext* ctx, Diligent::ITexture* depth, const float projection[4],
		const Matrix& inverseView, const Matrix& viewProj, Diligent::ITexture* prevScene, const Matrix& prevViewProj);
	bool reflectionsUsable() const { return reflectOk; }
	bool valid() const { return built; }
	bool usable() const { return ok; } // the mask's shader and pipeline were made
	size_t blasCount() const { return blases.size(); }
	size_t instanceCount() const { return lastInstanceCount; }
	// this frame's work (the hitch report: RT structures over 30 ms)
	struct FrameStats { int created = 0, evicted = 0, terrain = 0, tlasGrown = 0; double createMs = 0, evictMs = 0, terrainMs = 0, tlasMs = 0; };
	FrameStats stats;

private:
	Diligent::IRenderDevice* device;
	uint32_t geometryBaseOf(const std::vector<Group>& groups);
	struct WindMesh {
		std::vector<Diligent::RefCntAutoPtr<Diligent::IBottomLevelAS>> blas; // per wind group
		Diligent::RefCntAutoPtr<Diligent::IBuffer> rest, bent;
		Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> srb;
		std::vector<std::string> names;
		std::vector<Diligent::BLASBuildTriangleData> data; // group 0's (the others' vertices further on)
		uint32_t vertexCount = 0, stride = 0; // the bent buffer: per group stride vertices
		std::vector<uint64_t> scratchOffset; // per group, in windScratch
		uint64_t lastUse = 0;
		bool inStill = false;
	};
	std::map<const void*, WindMesh> windMeshes;
	Diligent::RefCntAutoPtr<Diligent::IPipelineState> windPipeline;
	Diligent::RefCntAutoPtr<Diligent::IBuffer> windConstants, windCorners, windScratch;
	uint64_t windScratchNeed = 0;
	bool windTried = false, collectingStill = false, stillWind = false;
	float windNow[4] = {};
	void windDispatch(Diligent::IDeviceContext* ctx, WindMesh& wm);
	std::map<std::pair<const void*, uint32_t>, Diligent::RefCntAutoPtr<Diligent::IBottomLevelAS>> blases;
	Diligent::RefCntAutoPtr<Diligent::IBuffer> scratch, tlasScratch, instanceBuffer;
	Diligent::RefCntAutoPtr<Diligent::ITopLevelAS> tlas;
	uint32_t tlasCapacity = 0;
	struct Instance { Diligent::IBottomLevelAS* blas; Matrix transform; uint32_t geometryBase; };
	// the alpha test's data: per structure's geometry (in order) its first UV and its texture + 1
	// (0: opaque), the instances' CustomId being their structure's first geometry
	std::map<const void*, uint32_t> geometryBases; // by the mesh's group list
	std::map<Diligent::IBottomLevelAS*, uint32_t> blasGeometryBase;
	std::map<std::pair<const void*, uint32_t>, uint64_t> animatedLastUse;
	// per key its pool: structures not in use (made by the worker, or dropped poses: the same
	// geometries' sizes), how many it has in all (built, spare, being made), whether one is
	// being made, its last use
	struct Pool {
		std::vector<Diligent::RefCntAutoPtr<Diligent::IBottomLevelAS>> spare;
		int owned = 0;
		bool pending = false;
		uint64_t lastUse = 0;
	};
	std::map<const void*, Pool> pools;
	struct Maker; // the worker thread
	Maker* maker = nullptr;
	// a structure for key from its pool (stolen from its pose unused longest when at the
	// limit), or null and one asked of the worker
	Diligent::RefCntAutoPtr<Diligent::IBottomLevelAS> take(const void* key, const std::vector<Group>& groups, bool animated, bool peek = false);
	void request(const void* key, Pool& pool, const std::vector<Group>& groups);
	// the poses' positions, this frame's one after another (from the start again each frame:
	// the GPU copies and builds in order)
	Diligent::RefCntAutoPtr<Diligent::IBuffer> poseBuffer;
	uint64_t poseOffset = 0, poseFrame = ~0ull;
	int terrainW = -1, terrainH = -1;
	uint64_t frameCounter = 0;
	std::vector<uint32_t> geometryRecords; // pairs
	std::vector<float> alphaUVs;
	std::vector<Diligent::RefCntAutoPtr<Diligent::ITextureView>> alphaTextures;
	bool alphaDirty = true;
	uint32_t alphaVersion = 0, maskAlphaVersion = ~0u, reflectAlphaVersion = ~0u;
	Diligent::RefCntAutoPtr<Diligent::IBuffer> geometryBuffer, uvBuffer;
	Diligent::RefCntAutoPtr<Diligent::ITexture> whiteTexture;
	void uploadAlpha(Diligent::IDeviceContext* ctx);
	void bindAlpha(Diligent::IShaderResourceBinding* srb);
	std::vector<Instance> instances;
	// the instances of the last top-level structure built: a frame with the same ones (a still
	// scene; a forest's trees) keeps it; any structure made or dropped since builds it again
	std::vector<Instance> builtInstances;
	Diligent::IBottomLevelAS* builtTerrain = nullptr;
	bool structuresChanged = true;
	// the still structure (setForest) and the moving one's state; each is bound for the other
	// while that one is empty
	Diligent::RefCntAutoPtr<Diligent::ITopLevelAS> stillTlas;
	Diligent::RefCntAutoPtr<Diligent::IBuffer> stillInstanceBuffer, stillScratch;
	uint32_t stillCapacity = 0, stillCount = 0, stillSlots = ~0u, stillMissing = 0, stillAge = 0;
	size_t stillCasters = 0;
	Diligent::IBottomLevelAS* stillTerrain = nullptr;
	std::vector<std::string> stillNames;
	bool stillBuilt = false, stillPending = false, movingBuilt = false;
	Diligent::ITopLevelAS* primary() const { return movingBuilt ? tlas.RawPtr() : stillTlas.RawPtr(); }
	Diligent::ITopLevelAS* secondary() const { return stillBuilt ? stillTlas.RawPtr() : tlas.RawPtr(); }
	// add's last structure and its first geometry (an entity list is mostly of one mesh)
	Diligent::IBottomLevelAS* lastAdded = nullptr;
	uint32_t lastAddedBase = 0;
	// the terrain's structure (setTerrain), its positions and indices
	Diligent::RefCntAutoPtr<Diligent::IBottomLevelAS> terrainBlas;
	Diligent::RefCntAutoPtr<Diligent::IBuffer> terrainVertices, terrainIndices;
	const Terrain* terrainKey = nullptr;
	uint64_t terrainVersion = ~0ull;
	std::vector<std::string> names;
	size_t lastInstanceCount = 0;
	bool built = false, ok = false;
	Diligent::RefCntAutoPtr<Diligent::IPipelineResourceSignature> signature;
	Diligent::RefCntAutoPtr<Diligent::IPipelineState> pipeline;
	Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> srb;
	Diligent::RefCntAutoPtr<Diligent::IBuffer> constants;
	Diligent::RefCntAutoPtr<Diligent::ITexture> mask;
	Diligent::ITexture* boundDepth = nullptr;
	Diligent::IBuffer* boundFog = nullptr;
	Diligent::ITextureView* boundHeights = nullptr;
	Diligent::ITopLevelAS* boundTlas = nullptr, *boundTlas2 = nullptr;
	// the water's reflections
	bool reflectOk = false, reflectTried = false;
	Diligent::RefCntAutoPtr<Diligent::IShader> reflectVs, reflectPs;
	Diligent::RefCntAutoPtr<Diligent::IPipelineResourceSignature> reflectSignature;
	Diligent::RefCntAutoPtr<Diligent::IPipelineState> reflectPipeline;
	Diligent::TEXTURE_FORMAT reflectDsvFormat = Diligent::TEX_FORMAT_UNKNOWN;
	Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> reflectSrb;
	Diligent::RefCntAutoPtr<Diligent::IBuffer> reflectConstants;
	Diligent::RefCntAutoPtr<Diligent::ITexture> reflections;
	Diligent::ITexture* reflectBound[2] = {};
	Diligent::ITopLevelAS* reflectBoundTlas = nullptr, *reflectBoundTlas2 = nullptr;
	// the models' reflections (renderModelReflections): made at the first use
	bool modelReflTried = false;
	Diligent::RefCntAutoPtr<Diligent::IPipelineResourceSignature> modelReflSignature;
	Diligent::RefCntAutoPtr<Diligent::IPipelineState> modelReflPipeline;
	Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> modelReflSrb;
	Diligent::RefCntAutoPtr<Diligent::IBuffer> modelReflConstants;
	Diligent::RefCntAutoPtr<Diligent::ITexture> modelRefl;
	Diligent::ITexture* modelReflBound[2] = {};
	Diligent::ITopLevelAS* modelReflBoundTlas = nullptr, *modelReflBoundTlas2 = nullptr;
	uint32_t modelReflAlphaVersion = ~0u;
};
