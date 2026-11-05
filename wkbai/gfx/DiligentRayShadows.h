// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#pragma once

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
	Diligent::IBottomLevelAS* blas(Diligent::IDeviceContext* ctx, const void* key, uint32_t frame, Diligent::IBuffer* positions,
		uint64_t positionOffset, uint32_t vertexCount, Diligent::IBuffer* indices, const std::vector<Group>& groups);

	// The terrain as a caster too (hills shading the valleys): its height grid, two triangles per
	// tile, built again when version changes (the terrain renderer's heights). The terrain
	// renderer draws a smooth (tessellated) surface through the same vertices, which lies under
	// these flat triangles in hollows: the terrain's triangles are hit from their upper side only
	// (back faces culled), so a ray starting a little under them goes through; the models stay
	// two-sided. Only the shadow mask sees the terrain (instance mask), not the reflections.
	// null: no terrain.
	void setTerrain(Diligent::IDeviceContext* ctx, const Terrain* terrain, uint64_t version);

	void beginFrame() { instances.clear(); }
	// a caster this frame (its world transform, row vectors)
	void add(Diligent::IBottomLevelAS* blas, const Matrix& transform);
	// the frame's top-level structure from the casters added
	void build(Diligent::IDeviceContext* ctx);
	// the shadow mask (R8: 1 sunlit, 0 in shadow) of the scene's depth, for its size;
	// projection: the camera's (P11, P22, P33, P43), inverseView: view -> world (row vectors);
	// fogBuffer: the renderers' FogBuffer (the shadows fade into the fog)
	Diligent::ITextureView* renderMask(Diligent::IDeviceContext* ctx, Diligent::ITexture* depth, const float projection[4],
		const Matrix& inverseView, const Vector3& sunDirection, uint64_t frameIndex, Diligent::IBuffer* fogBuffer);
	// starts the water's reflection pass: the target (RGBA16F, the frame's size, cleared) and the
	// pipeline are set with the depth target; the caller draws its water surface (float3
	// position, float depth per vertex) with transformBuffer's view-projection, then restores
	// its targets. scene and sceneDepth: the frame before the water; projZ: the projection's
	// m22 and m32. The reflections' view (rgb, a: how much they show), or null.
	Diligent::ITextureView* beginWaterReflections(Diligent::IDeviceContext* ctx, Diligent::IBuffer* transformBuffer,
		Diligent::ITexture* scene, Diligent::ITexture* sceneDepth, Diligent::ITextureView* dsv, const Vector3& camera,
		float projZ22, float projZ32, const float fallback[3]);
	bool reflectionsUsable() const { return reflectOk; }
	bool valid() const { return built; }
	bool usable() const { return ok; } // the mask's shader and pipeline were made
	size_t blasCount() const { return blases.size(); }
	size_t instanceCount() const { return lastInstanceCount; }

private:
	Diligent::IRenderDevice* device;
	std::map<std::pair<const void*, uint32_t>, Diligent::RefCntAutoPtr<Diligent::IBottomLevelAS>> blases;
	Diligent::RefCntAutoPtr<Diligent::IBuffer> scratch, tlasScratch, instanceBuffer;
	Diligent::RefCntAutoPtr<Diligent::ITopLevelAS> tlas;
	uint32_t tlasCapacity = 0;
	struct Instance { Diligent::IBottomLevelAS* blas; Matrix transform; uint32_t geometryBase; };
	// the alpha test's data: per structure's geometry (in order) its first UV and its texture + 1
	// (0: opaque), the instances' CustomId being their structure's first geometry
	std::map<const void*, uint32_t> geometryBases; // by the mesh's group list
	std::map<Diligent::IBottomLevelAS*, uint32_t> blasGeometryBase;
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
	Diligent::ITopLevelAS* boundTlas = nullptr;
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
	Diligent::ITopLevelAS* reflectBoundTlas = nullptr;
};
