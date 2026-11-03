// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#pragma once

// Ray-traced sun shadows of the buildings, trees and units (RenderSettings::rtShadows; RE
// repository docs/engine/gi-clouds.md), with hardware ray tracing through Diligent (inline ray
// queries, DXR 1.1 / VK_KHR_ray_query): instead of the static shadow map.
//
// The scene renderer adds every shadow caster it draws (SEFLAG_STATIC_SHADOW or
// SEFLAG_DYNAMIC_SHADOW) with the bottom-level structure of its mesh, built once per mesh, or
// for an animated model once per key frame (the instance takes its current key frame's), and
// builds the frame's top-level structure. Before the post-processing, one ray per pixel from
// the scene's depth towards the sun (jittered inside the sun's disc, so the temporal
// anti-aliasing softens the edges) gives the shadow mask, which the composite applies.
// Alpha-tested materials (leaves) count as solid.

#include "RefCntAutoPtr.hpp"
#include "RenderDevice.h"
#include "DeviceContext.h"
#include "BottomLevelAS.h"
#include "TopLevelAS.h"
#include "../util/vecmat.h"
#include <map>
#include <string>
#include <vector>

class DiligentRayShadows {
public:
	// whether the device can (inline ray tracing, DXC for the mask's shader)
	static bool Supported(Diligent::IRenderDevice* device);
	DiligentRayShadows(Diligent::IRenderDevice* device);

	struct Group { uint32_t firstIndex, indexCount, firstVertex, vertexCount; };
	// a mesh's structure: key (the mesh, or the animation and key frame), its positions (float3,
	// from the byte offset), 16-bit indices per group relative to the group's first vertex
	Diligent::IBottomLevelAS* blas(Diligent::IDeviceContext* ctx, const void* key, uint32_t frame, Diligent::IBuffer* positions,
		uint64_t positionOffset, uint32_t vertexCount, Diligent::IBuffer* indices, const std::vector<Group>& groups);

	void beginFrame() { instances.clear(); }
	// a caster this frame (its world transform, row vectors)
	void add(Diligent::IBottomLevelAS* blas, const Matrix& transform);
	// the frame's top-level structure from the casters added
	void build(Diligent::IDeviceContext* ctx);
	// the shadow mask (R8: 1 sunlit, 0 in shadow) of the scene's depth, for its size;
	// projection: the camera's (P11, P22, P33, P43), inverseView: view -> world (row vectors)
	Diligent::ITextureView* renderMask(Diligent::IDeviceContext* ctx, Diligent::ITexture* depth, const float projection[4],
		const Matrix& inverseView, const Vector3& sunDirection, uint64_t frameIndex);
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
	struct Instance { Diligent::IBottomLevelAS* blas; Matrix transform; };
	std::vector<Instance> instances;
	std::vector<std::string> names;
	size_t lastInstanceCount = 0;
	bool built = false, ok = false;
	Diligent::RefCntAutoPtr<Diligent::IPipelineResourceSignature> signature;
	Diligent::RefCntAutoPtr<Diligent::IPipelineState> pipeline;
	Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> srb;
	Diligent::RefCntAutoPtr<Diligent::IBuffer> constants;
	Diligent::RefCntAutoPtr<Diligent::ITexture> mask;
	Diligent::ITexture* boundDepth = nullptr;
	Diligent::ITopLevelAS* boundTlas = nullptr;
};
