// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#ifdef _WIN32

#include "D3D11EnhancedTerrainRenderer.h"
#include "EnhancedTerrainData.h"
#include "../util/vecmat.h"
#include "renderer.h"
#include "../window.h"
#include "../terrain.h"
#include <utility>
#include <array>
#include "TextureCache.h"
#include "../Camera.h"
#include <cassert>
#include <algorithm>
#include "../util/util.h"
#include "bitmap.h"
#include "../settings.h"
#include <nlohmann/json.hpp>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "renderer_d3d11.h"
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;

using D11NTRVtx = EnhancedTerrainVertex;

const char tshaderCode[] = R"---(
Texture2D inpTexture : register(t0);
Texture2D normalTexture : register(t1);
SamplerState inpSampler : register(s0);

cbuffer ConstantBuffer : register(b0)
{
	matrix Transform;
};

cbuffer FogBuffer : register(b1)
{
	float4 FogColor;
	float FogStartDist;
	float FogEndDist;
};

cbuffer SunBuffer : register(b2)
{
	float3 SunDirection;
	int BumpOn;
};

struct VS_OUTPUT
{
	float4 Pos : SV_POSITION;
	float3 FragPos : COLOR1;
	float3 Norm: NORMAL;
	float4 Color : COLOR0;
	float2 Texcoord : TEXCOORD0;
	float Fog : FOG;
	float3 Tangent : TANGENT0;
	float3 Bitangent : TANGENT1;
};

VS_OUTPUT VS(float4 Pos : POSITION, float4 Normal : NORMAL, float3 Tangent : TANGENT, float3 Bitangent : BITANGENT, float2 Texcoord : TEXCOORD)
{
	VS_OUTPUT output = (VS_OUTPUT)0;
	output.Pos = mul(Pos, Transform);
	output.FragPos = Pos.xyz;
	output.Norm = Normal.xyz * 2 - float3(1,1,1);
	output.Color = float4((Normal.w * 0.5 + 0.5).xxx, 1);
	output.Texcoord = Texcoord;
	output.Fog = clamp((output.Pos.w-FogStartDist) / (FogEndDist-FogStartDist), 0, 1);
	output.Tangent = Tangent * 2 - float3(1,1,1);
	output.Bitangent = Bitangent * 2 - float3(1,1,1);
	return output;
};

float4 PS(VS_OUTPUT input) : SV_Target
{
	// Compute the length of a half pixel in the current mipmap
	float lod = inpTexture.CalculateLevelOfDetail(inpSampler, input.Texcoord);
	float ha = 0.5 / (256 >> (int)ceil(lod));

	float2 stc = clamp(input.Texcoord, float2(ha,ha), float2(1.0-ha,1.0-ha));
	float2 tile = floor(stc / float2(0.25,0.25)); // tile index in the 4x4 atlas
	float2 subuv = fmod(stc, float2(0.25,0.25)); // coords inside the used tile

	float2 fuv = clamp(subuv, float2(ha,ha), float2(0.25-ha,0.25-ha)) + tile*0.25;

	float4 tex = inpTexture.Sample(inpSampler, fuv);
	//float4 tex = float4(1,1,1,1);

	float lum = 0.2;

	if(BumpOn) {
		float3 nrm = normalTexture.Sample(inpSampler, fuv).xyz * 2 - 1;

		float3 m1 = input.Tangent;
		float3 m2 = input.Norm;
		float3 m3 = input.Bitangent;
		float3 actnorm = nrm.xxx * m1 + nrm.yyy * m2 + nrm.zzz * m3;
		lum += clamp(dot(normalize(actnorm), SunDirection), 0, 1);
		lum = clamp(lum, 0, 1);
	}
	else {
		lum += clamp(dot(input.Norm, SunDirection), 0, 1);
		lum = clamp(lum, 0, 1);
	}
	//lum = clamp(lum, 0, 1);
	return lerp(float4(lum.xxx, 1) * input.Color * tex, FogColor, input.Fog);
}

// TODO
//float4 PS_Lake(VS_OUTPUT input) : SV_Target
//{
//	return noise(input.Texcoord);
//}
)---";

template<typename Vtx> struct RCustomBatchD3D11
{
	uint32_t maxverts, maxindis;
	uint32_t curverts, curindis;

	ID3D11Buffer* vbuf;
	ID3D11Buffer* ibuf;
	Vtx* vlock;
	uint16_t* ilock;
	bool locked;
	ID3D11DeviceContext* ddImmediateContext;

	RCustomBatchD3D11(int mv, int mi, ID3D11Device* ddDevice, ID3D11DeviceContext* ddic)
	{
		maxverts = mv; maxindis = mi;
		curverts = curindis = 0;

		D3D11_BUFFER_DESC bd;
		bd.Usage = D3D11_USAGE_DYNAMIC;
		bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		bd.MiscFlags = 0;
		bd.StructureByteStride = 0;

		bd.ByteWidth = mv * sizeof(Vtx);
		bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		ddDevice->CreateBuffer(&bd, nullptr, &vbuf);

		bd.ByteWidth = mi * 2;
		bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
		ddDevice->CreateBuffer(&bd, nullptr, &ibuf);

		locked = false;
		ddImmediateContext = ddic;
	}

	~RCustomBatchD3D11()
	{
		if (locked) unlock();
		vbuf->Release();
		ibuf->Release();
	}

	void lock()
	{
		D3D11_MAPPED_SUBRESOURCE ms;
		ddImmediateContext->Map(vbuf, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms);
		vlock = (Vtx*)ms.pData;
		ddImmediateContext->Map(ibuf, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms);
		ilock = (uint16_t*)ms.pData;
		locked = true;
	}
	void unlock()
	{
		ddImmediateContext->Unmap(vbuf, 0);
		ddImmediateContext->Unmap(ibuf, 0);
		locked = false;
	}

	void begin() {}
	void end() {}

	void next(uint32_t nverts, uint32_t nindis, Vtx** vpnt, uint16_t** ipnt, uint32_t* fi)
	{
		if (nverts > maxverts) ferr("Too many vertices to fit in the batch.");
		if (nindis > maxindis) ferr("Too many indices to fit in the batch.");

		if ((curverts + nverts > maxverts) || (curindis + nindis > maxindis))
			flush();

		if (!locked) lock();
		*vpnt = vlock + curverts;
		*ipnt = ilock + curindis;
		*fi = curverts;

		curverts += nverts; curindis += nindis;
	}

	void flush()
	{
		if (locked) unlock();
		if (!curverts) return;
		UINT stride = sizeof(Vtx); UINT offset = 0;
		ddImmediateContext->IASetVertexBuffers(0, 1, &vbuf, &stride, &offset);
		ddImmediateContext->IASetIndexBuffer(ibuf, DXGI_FORMAT_R16_UINT, 0);
		ddImmediateContext->DrawIndexed(curindis, 0, 0);
		curverts = curindis = 0;
	}

};

template<typename Idx> struct RIndexBatchD3D11
{
	static constexpr DXGI_FORMAT format = (sizeof(Idx) == 2) ? DXGI_FORMAT_R16_UINT : (sizeof(Idx) == 4) ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_UNKNOWN;
	static_assert(format != DXGI_FORMAT_UNKNOWN, "Invalid index integer size for RIndexBatchD3D11");

	uint32_t maxindis;
	uint32_t curindis;
	ComPtr<ID3D11Buffer> ibuf;
	Idx* ilock;
	bool locked;
	ID3D11DeviceContext* ddImmediateContext;

	RIndexBatchD3D11(int mi, ID3D11Device* ddDevice, ID3D11DeviceContext* ddic)
	{
		maxindis = mi;
		curindis = 0;

		D3D11_BUFFER_DESC bd;
		bd.Usage = D3D11_USAGE_DYNAMIC;
		bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		bd.MiscFlags = 0;
		bd.StructureByteStride = 0;
		bd.ByteWidth = mi * sizeof(Idx);
		bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
		ddDevice->CreateBuffer(&bd, nullptr, &ibuf);

		locked = false;
		ddImmediateContext = ddic;
	}

	~RIndexBatchD3D11()
	{
		if (locked) unlock();
	}

	void lock()
	{
		D3D11_MAPPED_SUBRESOURCE ms;
		ddImmediateContext->Map(ibuf.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms);
		ilock = (Idx*)ms.pData;
		locked = true;
	}
	void unlock()
	{
		ddImmediateContext->Unmap(ibuf.Get(), 0);
		locked = false;
	}

	void next(uint32_t nindis, Idx** ipnt)
	{
		if (curindis + nindis > maxindis)
			flush();

		if (!locked) lock();
		*ipnt = ilock + curindis;

		curindis += nindis;
	}

	void flush()
	{
		if (locked) unlock();
		if (!curindis) return;
		ddImmediateContext->IASetIndexBuffer(ibuf.Get(), format, 0);
		ddImmediateContext->DrawIndexed(curindis, 0, 0);
		curindis = 0;
	}

};

struct D3D11EnhancedTerrainRenderer::Impl {
	std::unique_ptr<RCustomBatchD3D11<D11NTRVtx>> batch;
	std::unique_ptr<RBatch> lakeBatch;
	ComPtr<ID3D11PixelShader> shaderPix;
	ComPtr<ID3D11VertexShader> shaderVtx;
	ComPtr<ID3D11InputLayout> ddInputLayout;
	ComPtr<ID3D11Buffer> sunBuffer;

	ComPtr<ID3D11Buffer> trnVertexBuffer;
	std::unique_ptr<RIndexBatchD3D11<uint32_t>> trnIndexBatch;
};

void D3D11EnhancedTerrainRenderer::init()
{
	auto* d11gfx = (D3D11Renderer*)gfx;
	_impl = new Impl;
	_impl->batch = std::make_unique<RCustomBatchD3D11<D11NTRVtx>>(4 * 256, 6 * 256, d11gfx->ddDevice, d11gfx->ddImmediateContext);
	_impl->lakeBatch.reset(d11gfx->CreateBatch(4 * 256, 6 * 256));
	texcache = new TextureCache(gfx, "Maps\\Map_Textures\\");

	// Load terrain color textures
	std::map<TerrainTexture*, EnhancedTerrainTexture> textures;
	LoadEnhancedTerrainTextures(terrain, texcache, textures);
	for (const auto& [trntex, et] : textures)
		ttexmap[trntex] = LoadedTexture{ et.diffuseMap, et.normalMap };

	// Terrain vertex buffer
	std::vector<D11NTRVtx> trnVertices = BuildEnhancedTerrainVertices(terrain);
	D3D11_BUFFER_DESC vbd;
	vbd.Usage = D3D11_USAGE_DEFAULT;
	vbd.CPUAccessFlags = 0;
	vbd.MiscFlags = 0;
	vbd.StructureByteStride = 0;
	vbd.ByteWidth = trnVertices.size() * sizeof(D11NTRVtx);
	vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	D3D11_SUBRESOURCE_DATA vsd;
	vsd.pSysMem = trnVertices.data();
	d11gfx->ddDevice->CreateBuffer(&vbd, &vsd, &_impl->trnVertexBuffer);
	_impl->trnIndexBatch = std::make_unique<RIndexBatchD3D11<uint32_t>>(4 * 256, d11gfx->ddDevice, d11gfx->ddImmediateContext);

	// D3D11 stuff
	auto psBlob = d11gfx->compileShader(tshaderCode, sizeof(tshaderCode), "PS", "ps_4_1");
	auto vsBlob = d11gfx->compileShader(tshaderCode, sizeof(tshaderCode), "VS", "vs_4_0");
	static const D3D11_INPUT_ELEMENT_DESC iadesc[] = {
		{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
		{"NORMAL", 0, DXGI_FORMAT_R10G10B10A2_UNORM, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
		{"TANGENT", 0, DXGI_FORMAT_R10G10B10A2_UNORM, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0},
		{"BITANGENT", 0, DXGI_FORMAT_R10G10B10A2_UNORM, 0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0},
		{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
	};
	HRESULT hres;
	hres = d11gfx->ddDevice->CreateInputLayout(iadesc, std::size(iadesc), vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &_impl->ddInputLayout);
	assert(!FAILED(hres));
	hres = d11gfx->ddDevice->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &_impl->shaderPix);
	assert(!FAILED(hres));
	hres = d11gfx->ddDevice->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &_impl->shaderVtx);
	assert(!FAILED(hres));

	D3D11_BUFFER_DESC bd;
	bd.Usage = D3D11_USAGE_DYNAMIC;
	bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	bd.MiscFlags = 0;
	bd.StructureByteStride = 0;
	bd.ByteWidth = 16;
	bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	d11gfx->ddDevice->CreateBuffer(&bd, nullptr, &_impl->sunBuffer);
}

D3D11EnhancedTerrainRenderer::~D3D11EnhancedTerrainRenderer()
{
	delete texcache;
}

void D3D11EnhancedTerrainRenderer::render() {
	auto* d11gfx = (D3D11Renderer*)gfx;

	gfx->BeginMapDrawing();

	constexpr float tilesize = 5.0f;
	Vector3 sunNormal = terrain->sunVector.normal();

	// camera space bounding box
	const Vector3& camstart = camera->position;
	Vector3 camend = camera->position + camera->direction * camera->farDist;
	float farheight = std::tan(0.9f) * camera->farDist;
	float farwidth = farheight * camera->aspect;
	Vector3 camside = camera->direction.cross(Vector3(0, 1, 0)).normal();
	Vector3 campup = camside.cross(camera->direction).normal();
	Vector3 farleft = camend + camside * farwidth;
	Vector3 farright = camend - camside * farwidth;
	Vector3 farup = camend + campup * farheight;
	Vector3 fardown = camend - campup * farheight;
	float bbx1, bbz1, bbx2, bbz2;
	std::tie(bbx1, bbx2) = std::minmax({ camstart.x, farleft.x, farright.x, farup.x, fardown.x });
	std::tie(bbz1, bbz2) = std::minmax({ camstart.z, farleft.z, farright.z, farup.z, fardown.z });
	auto clamp = [](auto val, auto low, auto high) {return std::max(low, std::min(high, val)); };
	int tlsx = clamp((int)std::floor(bbx1 / tilesize) + (int)terrain->edge, 0, (int)terrain->width - 1);
	int tlsz = clamp((int)std::floor(bbz1 / tilesize) + (int)terrain->edge, 0, (int)terrain->height - 1);
	int tlex = clamp((int)std::ceil(bbx2 / tilesize) + (int)terrain->edge, 0, (int)terrain->width - 1);
	int tlez = clamp((int)std::ceil(bbz2 / tilesize) + (int)terrain->edge, 0, (int)terrain->height - 1);

	gfx->BeginBatchDrawing();
	auto* dimm = ((D3D11Renderer*)gfx)->ddImmediateContext;
	dimm->PSSetShader(_impl->shaderPix.Get(), nullptr, 0);
	dimm->IASetInputLayout(_impl->ddInputLayout.Get());
	dimm->VSSetShader(_impl->shaderVtx.Get(), nullptr, 0);

	D3D11_MAPPED_SUBRESOURCE mappedRes;
	HRESULT hres = dimm->Map(_impl->sunBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedRes);
	assert(!FAILED(hres));
	char* sbdata = (char*)mappedRes.pData;
	*(Vector3*)(sbdata+0) = sunNormal;
	*(int*)(sbdata + 12) = m_bumpOn ? 1 : 0;
	dimm->Unmap(_impl->sunBuffer.Get(), 0);
	dimm->VSSetConstantBuffers(2, 1, _impl->sunBuffer.GetAddressOf());
	dimm->PSSetConstantBuffers(2, 1, _impl->sunBuffer.GetAddressOf());

	texture oldgfxtex = 0;
	for (int z = tlsz; z <= tlez; z++) {
		for (int x = tlsx; x <= tlex; x++) {
			int lx = x - terrain->edge, lz = z - terrain->edge;
			Vector3 pp((lx + 0.5f)*tilesize, terrain->getVertex(x, z), (lz + 0.5f)*tilesize);
			Vector3 camcenter = camera->position + camera->direction * 125.0f;
			pp += (camcenter - pp).normal() * tilesize * sqrtf(2.0f);
			Vector3 ttpp = pp.transformScreenCoords(camera->sceneMatrix);
			if (ttpp.x < -1 || ttpp.x > 1 || ttpp.y < -1 || ttpp.y > 1 || ttpp.z < -1 || ttpp.z > 1)
				continue;
			const Terrain::Tile* tile = terrain->getTile(x, z);
			TerrainTexture *trntex = tile->texture;
			auto& newgfxtex = ttexmap.at(trntex);
			tilesPerTex[newgfxtex].push_back(tile);
		}
	}

	UINT vstride = sizeof(D11NTRVtx); UINT voffset = 0;
	dimm->IASetVertexBuffers(0, 1, _impl->trnVertexBuffer.GetAddressOf(), &vstride, &voffset);
	for(auto &pack : tilesPerTex) {
		if (pack.second.empty())
			continue;
		gfx->SetTexture(0, pack.first.diffuseMap);
		gfx->SetTexture(1, pack.first.normalMap);
		for (const TerrainTile* tile : pack.second) {
			unsigned int x = tile->x;
			unsigned int z = tile->z;

			uint32_t firstindex = 4 * (z * terrain->width + x);
			uint32_t* oix;
			_impl->trnIndexBatch->next(6, &oix);
			for (const int &c : { 0,3,1,1,3,2 })
				*(oix++) = firstindex + c;
		}
		_impl->trnIndexBatch->flush();
	}

	dimm->IASetInputLayout(((D3D11Renderer*)gfx)->ddInputLayout);

	// ----- Lakes -----
	gfx->BeginBatchDrawing();
	gfx->NoTexture(0);
	auto* lakeBatch = _impl->lakeBatch.get();
	lakeBatch->begin();
	gfx->BeginLakeDrawing();
	dimm->PSSetShader(d11gfx->ddLakePixelShader, nullptr, 0);
	dimm->VSSetShader(d11gfx->ddFogVertexShader, nullptr, 0);
	const uint32_t color = (terrain->fogColor & 0xFFFFFF) | 0x80000000;
	for (int z = tlsz; z <= tlez; z++) {
		for (int x = tlsx; x <= tlex; x++) {
			const TerrainTile* tile = terrain->getTile(x, z);
			if (tile && tile->fullOfWater) {
				unsigned int x = tile->x;
				unsigned int z = tile->z;
				int lx = x - terrain->edge, lz = z - terrain->edge;

				// is water tile visible on screen?
				Vector3 pp((lx + 0.5f) * tilesize, tile->waterLevel, (lz + 0.5f) * tilesize);
				Vector3 camcenter = camera->position + camera->direction * 125.0f;
				pp += (camcenter - pp).normal() * tilesize * sqrtf(2.0f);
				Vector3 ttpp = pp.transformScreenCoords(camera->sceneMatrix);
				if (ttpp.x < -1 || ttpp.x > 1 || ttpp.y < -1 || ttpp.y > 1 || ttpp.z < -1 || ttpp.z > 1)
					continue;

				batchVertex* outvert; uint16_t* outindices; unsigned int firstindex;
				lakeBatch->next(4, 6, &outvert, &outindices, &firstindex);
				outvert[0].x = lx * tilesize; outvert[0].y = tile->waterLevel ; outvert[0].z = lz * tilesize;
				outvert[0].color = color; outvert[0].u = 0.0f; outvert[0].v = 0.0f;
				outvert[1].x = (lx + 1) * tilesize; outvert[1].y = tile->waterLevel; outvert[1].z = lz * tilesize;
				outvert[1].color = color; outvert[1].u = 0.0f; outvert[1].v = 0.0f;
				outvert[2].x = (lx + 1) * tilesize; outvert[2].y = tile->waterLevel; outvert[2].z = (lz + 1) * tilesize;
				outvert[2].color = color; outvert[2].u = 0.0f; outvert[2].v = 0.0f;
				outvert[3].x = lx * tilesize; outvert[3].y = tile->waterLevel; outvert[3].z = (lz + 1) * tilesize;
				outvert[3].color = color; outvert[3].u = 0.0f; outvert[3].v = 0.0f;
				uint16_t* oix = outindices;
				for (const int& c : { 0,3,1,1,3,2 })
					*(oix++) = firstindex + c;
			}
		}
	}
	lakeBatch->flush();
	lakeBatch->end();

	for (auto& pack : tilesPerTex)
		pack.second.clear();
}

#endif // _WIN32