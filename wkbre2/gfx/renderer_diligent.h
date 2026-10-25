// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#pragma once

#include "renderer.h"

#include "RenderDevice.h"
#include "DeviceContext.h"
#include "SwapChain.h"
#include "RefCntAutoPtr.hpp"

// The Diligent IRenderer, as seen by the renderers that draw with Diligent directly
// (DiligentEnhancedSceneRenderer, DiligentEnhancedTerrainRenderer).
struct DiligentGfx : IRenderer
{
	Diligent::RefCntAutoPtr<Diligent::IRenderDevice> device;
	Diligent::RefCntAutoPtr<Diligent::IDeviceContext> ctx;
	Diligent::RefCntAutoPtr<Diligent::ISwapChain> swapChain;

	// Dynamic constant buffers kept up to date by SetTransformMatrix and SetFog:
	// cbuffer TransformBuffer { matrix Transform; } and
	// cbuffer FogBuffer { float4 FogColor; float FogStartDist; float FogEndDist; }
	Diligent::RefCntAutoPtr<Diligent::IBuffer> transformBuffer, fogBuffer;

	// Shader resource view of a texture from CreateTexture (a white texture for nullptr).
	virtual Diligent::ITextureView* GetTextureView(texture t) = 0;

	// Copy data into the current frame's stream buffers, returning the buffer and the byte
	// offset to bind it at. The data stays valid until the end of the frame.
	virtual Diligent::IBuffer* StreamVertexData(const void* data, uint64_t size, uint64_t& offset) = 0;
	virtual Diligent::IBuffer* StreamIndexData(const void* data, uint64_t size, uint64_t& offset) = 0;
};

