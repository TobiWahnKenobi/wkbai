// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

// Renderer that draws nothing ("renderer": "null"), for tests and for measuring the CPU side
// of a frame without the GPU. Batches and buffers keep their data in memory so callers can
// fill them as usual.

#include "renderer.h"
#include "../util/util.h"
#include <vector>

struct RBatchNULL : public RBatch
{
	std::vector<batchVertex> verts;
	std::vector<uint16_t> indis;

	void begin() override {}
	void end() override {}
	void flush() override { curverts = curindis = 0; }
	void next(uint32_t nverts, uint32_t nindis, batchVertex** vpnt, uint16_t** ipnt, uint32_t* fi) override
	{
		if (nverts > maxverts) ferr("Too many vertices to fit in the batch.");
		if (nindis > maxindis) ferr("Too many indices to fit in the batch.");
		if ((curverts + nverts > maxverts) || (curindis + nindis > maxindis))
			flush();
		*vpnt = verts.data() + curverts;
		*ipnt = indis.data() + curindis;
		*fi = curverts;
		curverts += nverts; curindis += nindis;
	}
};

struct RVertexBufferNULL : public RVertexBuffer
{
	std::vector<batchVertex> mem;
	batchVertex* lock() override { return mem.data(); }
	void unlock() override {}
};

struct RIndexBufferNULL : public RIndexBuffer
{
	std::vector<uint16_t> mem;
	uint16_t* lock() override { return mem.data(); }
	void unlock() override {}
};

struct NULLRenderer : public IRenderer
{
	void Init() override {}
	void Reset() override {}

	void BeginDrawing() override {}
	void EndDrawing() override {}
	void ClearFrame(bool clearColors, bool clearDepth, uint32_t color) override {}

	texture CreateTexture(const Bitmap& bm, int mipmaps) override { return nullptr; }
	void FreeTexture(texture t) override {}
	void UpdateTexture(texture t, const Bitmap& bmp) override {}

	void SetTransformMatrix(const Matrix* m) override {}
	void SetTexture(uint32_t x, texture t) override {}
	void NoTexture(uint32_t x) override {}
	void SetFog(uint32_t color, float farz) override {}
	void DisableFog() override {}
	void EnableAlphaTest() override {}
	void DisableAlphaTest() override {}
	void EnableColorBlend() override {}
	void DisableColorBlend() override {}
	void SetBlendColor(int c) override {}
	void EnableAlphaBlend() override {}
	void DisableAlphaBlend() override {}
	void EnableScissor() override {}
	void DisableScissor() override {}
	void SetScissorRect(int x, int y, int w, int h) override {}
	void EnableDepth() override {}
	void DisableDepth() override {}

	void InitRectDrawing() override {}
	void DrawRect(int x, int y, int w, int h, int c, float u, float v, float o, float p) override {}
	void DrawGradientRect(int x, int y, int w, int h, int c0, int c1, int c2, int c3) override {}
	void DrawFrame(int x, int y, int w, int h, int c) override {}

	void BeginMapDrawing() override {}
	void BeginLakeDrawing() override {}
	void BeginMeshDrawing() override {}

	RBatch* CreateBatch(int mv, int mi) override
	{
		RBatchNULL* r = new RBatchNULL;
		r->maxverts = mv; r->maxindis = mi;
		r->curverts = r->curindis = 0;
		r->verts.resize(mv);
		r->indis.resize(mi);
		return r;
	}
	void BeginBatchDrawing() override {}
	int ConvertColor(int c) override { return c; }

	RVertexBuffer* CreateVertexBuffer(int nv) override
	{
		RVertexBufferNULL* r = new RVertexBufferNULL;
		r->size = nv;
		r->mem.resize(nv);
		return r;
	}
	RIndexBuffer* CreateIndexBuffer(int ni) override
	{
		RIndexBufferNULL* r = new RIndexBufferNULL;
		r->size = ni;
		r->mem.resize(ni);
		return r;
	}
	void SetVertexBuffer(RVertexBuffer* _rv) override {}
	void SetIndexBuffer(RIndexBuffer* _ri) override {}
	void DrawBuffer(int first, int count) override {}

	void InitImGuiDrawing() override {}
};

IRenderer* CreateNULLRenderer() { return new NULLRenderer; }
