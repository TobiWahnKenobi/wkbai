// wkbai - a small BC7 encoder and DDS writer (Bc7Encode.h)
// Licensed under the GNU General Public License 3
//
// Two modes, the better of them per block: mode 6 (one subset, 7-bit endpoints with a p-bit each,
// 4-bit indices) and mode 1 (two subsets in one of 64 shapes, 6-bit endpoints with a p-bit shared
// per subset, 3-bit indices), the shapes ranked by how well a line fits each half, the best four
// fitted fully. The shapes and their second anchors are BC7's (as in util/bcdec.h's table).

#include "Bc7Encode.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {
	const int W3[8] = { 0, 9, 18, 27, 37, 46, 55, 64 };
	const int W4[16] = { 0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64 };
	// two-subset shapes: bit i set = pixel i in subset 1; the anchor of subset 1
	const uint16_t SHAPES[64] = { 0xcccc, 0x8888, 0xeeee, 0xecc8, 0xc880, 0xfeec, 0xfec8, 0xec80, 0xc800, 0xffec, 0xfe80, 0xe800, 0xffe8, 0xff00,
		0xfff0, 0xf000, 0xf710, 0x008e, 0x7100, 0x08ce, 0x008c, 0x7310, 0x3100, 0x8cce, 0x088c, 0x3110, 0x6666, 0x366c, 0x17e8, 0x0ff0, 0x718e,
		0x399c, 0xaaaa, 0xf0f0, 0x5a5a, 0x33cc, 0x3c3c, 0x55aa, 0x9696, 0xa55a, 0x73ce, 0x13c8, 0x324c, 0x3bdc, 0x6996, 0xc33c, 0x9966, 0x0660,
		0x0272, 0x04e4, 0x4e40, 0x2720, 0xc936, 0x936c, 0x39c6, 0x639c, 0x9336, 0x9cc6, 0x817e, 0xe718, 0xccf0, 0x0fcc, 0x7744, 0xee22 };
	const uint8_t ANCHOR2[64] = { 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 2, 8, 2, 2, 8, 8, 15, 2, 8, 2, 2, 8, 8, 2, 2,
		15, 15, 6, 8, 2, 8, 15, 15, 2, 8, 2, 2, 2, 15, 15, 6, 6, 2, 6, 8, 15, 15, 2, 2, 15, 15, 15, 15, 15, 2, 2, 15 };

	struct Bits {
		uint8_t* b;
		int pos = 0;
		void put(uint32_t v, int n)
		{
			for (int i = 0; i < n; i++, pos++)
				if (v >> i & 1) b[pos >> 3] |= (uint8_t)(1 << (pos & 7));
		}
	};

	inline int Mix(int e0, int e1, int w) { return ((64 - w) * e0 + w * e1 + 32) >> 6; }

	// a subset's fit: quantized endpoints (bits a channel), p-bits, indices of its pixels
	struct Sub {
		int q[2][3];
		int p[2];
		uint8_t idx[16];
		long long err = 1LL << 62;
	};

	// an endpoint's 8-bit value from its quantized channel and p-bit (bits + 1 bits, expanded)
	inline int Expand(int q, int p, int bits)
	{
		const int v = q << 1 | p; // bits + 1 bits
		const int n = bits + 1;
		return n >= 8 ? v : (v << (8 - n)) | (v >> (2 * n - 8));
	}

	// indices and error for the pixels list (n of them) with the endpoints of s
	void Assign(const int (*px)[3], const int* list, int n, Sub& s, int bits, int indexBits)
	{
		const int* W = indexBits == 3 ? W3 : W4;
		const int levels = 1 << indexBits;
		int e[2][3];
		for (int k = 0; k < 2; k++)
			for (int c = 0; c < 3; c++) e[k][c] = Expand(s.q[k][c], s.p[k], bits);
		int pal[16][3];
		for (int i = 0; i < levels; i++)
			for (int c = 0; c < 3; c++) pal[i][c] = Mix(e[0][c], e[1][c], W[i]);
		s.err = 0;
		for (int j = 0; j < n; j++) {
			const int* x = px[list[j]];
			int best = 0;
			long long be = 1LL << 60;
			for (int i = 0; i < levels; i++) {
				const int dr = x[0] - pal[i][0], dg = x[1] - pal[i][1], db = x[2] - pal[i][2];
				const long long d = (long long)dr * dr + dg * dg + db * db;
				if (d < be) { be = d; best = i; }
			}
			s.idx[j] = (uint8_t)best;
			s.err += be;
		}
	}

	// the best quantization of float endpoints: each p-bit choice (shared: both the same), each
	// endpoint channel rounded and one step either way when that lowers the error
	Sub Quantize(const int (*px)[3], const int* list, int n, const float e0[3], const float e1[3], int bits, int indexBits, bool sharedP)
	{
		const int maxq = (1 << bits) - 1;
		Sub best;
		for (int pc = 0; pc < (sharedP ? 2 : 4); pc++) {
			Sub s;
			s.p[0] = sharedP ? pc : pc & 1;
			s.p[1] = sharedP ? pc : pc >> 1;
			for (int c = 0; c < 3; c++)
				for (int k = 0; k < 2; k++) {
					const float v = (k ? e1 : e0)[c];
					// the q whose expanded value is nearest v
					int bq = 0, bd = 1 << 30;
					const int guess = std::clamp((int)std::lround(v / 255.0f * maxq), 0, maxq);
					for (int q = std::max(0, guess - 2); q <= std::min(maxq, guess + 2); q++) {
						const int d = std::abs(Expand(q, s.p[k], bits) - (int)std::lround(v));
						if (d < bd) { bd = d; bq = q; }
					}
					s.q[k][c] = bq;
				}
			Assign(px, list, n, s, bits, indexBits);
			// one step either way per endpoint channel, kept when better
			for (int c = 0; c < 3; c++)
				for (int k = 0; k < 2; k++)
					for (int d : { -1, 1 }) {
						Sub t = s;
						t.q[k][c] = std::clamp(t.q[k][c] + d, 0, maxq);
						if (t.q[k][c] == s.q[k][c]) continue;
						Assign(px, list, n, t, bits, indexBits);
						if (t.err < s.err) s = t;
					}
			if (s.err < best.err) best = s;
		}
		return best;
	}

	// a subset fitted: the principal line, its ends, quantized, then refined by least squares
	Sub FitSubset(const int (*px)[3], const int* list, int n, int bits, int indexBits, bool sharedP)
	{
		float mean[3] = { 0, 0, 0 };
		for (int j = 0; j < n; j++)
			for (int c = 0; c < 3; c++) mean[c] += px[list[j]][c] / (float)n;
		float cov[6] = { 0, 0, 0, 0, 0, 0 };
		for (int j = 0; j < n; j++) {
			const float r = px[list[j]][0] - mean[0], g = px[list[j]][1] - mean[1], b = px[list[j]][2] - mean[2];
			cov[0] += r * r; cov[1] += r * g; cov[2] += r * b; cov[3] += g * g; cov[4] += g * b; cov[5] += b * b;
		}
		float ax[3] = { 0.577f, 0.577f, 0.577f };
		for (int it = 0; it < 8; it++) {
			const float x = cov[0] * ax[0] + cov[1] * ax[1] + cov[2] * ax[2];
			const float y = cov[1] * ax[0] + cov[3] * ax[1] + cov[4] * ax[2];
			const float z = cov[2] * ax[0] + cov[4] * ax[1] + cov[5] * ax[2];
			const float nn = std::sqrt(x * x + y * y + z * z);
			if (nn < 1e-6f) break;
			ax[0] = x / nn; ax[1] = y / nn; ax[2] = z / nn;
		}
		float tmin = 1e9f, tmax = -1e9f;
		for (int j = 0; j < n; j++) {
			const float t = (px[list[j]][0] - mean[0]) * ax[0] + (px[list[j]][1] - mean[1]) * ax[1] + (px[list[j]][2] - mean[2]) * ax[2];
			tmin = std::min(tmin, t);
			tmax = std::max(tmax, t);
		}
		float e0[3], e1[3];
		for (int c = 0; c < 3; c++) {
			e0[c] = std::clamp(mean[c] + tmin * ax[c], 0.0f, 255.0f);
			e1[c] = std::clamp(mean[c] + tmax * ax[c], 0.0f, 255.0f);
		}
		Sub best = Quantize(px, list, n, e0, e1, bits, indexBits, sharedP);
		const int* W = indexBits == 3 ? W3 : W4;
		for (int round = 0; round < 2 && best.err > 0; round++) {
			float aa = 0, ab = 0, bb = 0, ra[3] = { 0, 0, 0 }, rb[3] = { 0, 0, 0 };
			for (int j = 0; j < n; j++) {
				const float w = W[best.idx[j]] / 64.0f, a = 1 - w;
				aa += a * a; ab += a * w; bb += w * w;
				for (int c = 0; c < 3; c++) { ra[c] += a * px[list[j]][c]; rb[c] += w * px[list[j]][c]; }
			}
			const float det = aa * bb - ab * ab;
			if (std::fabs(det) < 1e-6f) break;
			float n0[3], n1[3];
			for (int c = 0; c < 3; c++) {
				n0[c] = std::clamp((ra[c] * bb - rb[c] * ab) / det, 0.0f, 255.0f);
				n1[c] = std::clamp((rb[c] * aa - ra[c] * ab) / det, 0.0f, 255.0f);
			}
			const Sub s = Quantize(px, list, n, n0, n1, bits, indexBits, sharedP);
			if (s.err >= best.err) break;
			best = s;
		}
		return best;
	}

	// how far a subset's pixels lie from their principal line (for ranking the shapes)
	float LineError(const int (*px)[3], const int* list, int n)
	{
		if (n <= 1) return 0;
		float mean[3] = { 0, 0, 0 };
		for (int j = 0; j < n; j++)
			for (int c = 0; c < 3; c++) mean[c] += px[list[j]][c] / (float)n;
		float cov[6] = { 0, 0, 0, 0, 0, 0 };
		for (int j = 0; j < n; j++) {
			const float r = px[list[j]][0] - mean[0], g = px[list[j]][1] - mean[1], b = px[list[j]][2] - mean[2];
			cov[0] += r * r; cov[1] += r * g; cov[2] += r * b; cov[3] += g * g; cov[4] += g * b; cov[5] += b * b;
		}
		float ax[3] = { 0.577f, 0.577f, 0.577f }, lambda = 0;
		for (int it = 0; it < 6; it++) {
			const float x = cov[0] * ax[0] + cov[1] * ax[1] + cov[2] * ax[2];
			const float y = cov[1] * ax[0] + cov[3] * ax[1] + cov[4] * ax[2];
			const float z = cov[2] * ax[0] + cov[4] * ax[1] + cov[5] * ax[2];
			lambda = std::sqrt(x * x + y * y + z * z);
			if (lambda < 1e-6f) break;
			ax[0] = x / lambda; ax[1] = y / lambda; ax[2] = z / lambda;
		}
		return cov[0] + cov[3] + cov[5] - lambda; // the variance off the line
	}

	// mode 6's 16 bytes
	void PutMode6(const Sub& in, uint8_t out[16])
	{
		Sub s = in;
		if (s.idx[0] & 8) { // the anchor's top bit must be clear: the endpoints swapped
			for (int c = 0; c < 3; c++) std::swap(s.q[0][c], s.q[1][c]);
			std::swap(s.p[0], s.p[1]);
			for (int i = 0; i < 16; i++) s.idx[i] = (uint8_t)(15 - s.idx[i]);
		}
		std::memset(out, 0, 16);
		Bits bits{ out };
		bits.put(1 << 6, 7);
		for (int c = 0; c < 3; c++) { bits.put((uint32_t)s.q[0][c], 7); bits.put((uint32_t)s.q[1][c], 7); }
		bits.put(127, 7); bits.put(127, 7); // alpha: opaque (254 or 255 by the p-bits)
		bits.put((uint32_t)s.p[0], 1);
		bits.put((uint32_t)s.p[1], 1);
		bits.put(s.idx[0], 3);
		for (int i = 1; i < 16; i++) bits.put(s.idx[i], 4);
	}
}

void Bc7::EncodeBlock(const uint8_t rgba[64], uint8_t out[16])
{
	int px[16][3];
	for (int i = 0; i < 16; i++)
		for (int c = 0; c < 3; c++) px[i][c] = rgba[i * 4 + c];
	int all[16];
	for (int i = 0; i < 16; i++) all[i] = i;
	// mode 6
	const Sub m6 = FitSubset(px, all, 16, 7, 4, false);
	long long bestErr = m6.err;
	PutMode6(m6, out);
	if (bestErr == 0)
		return;
	// mode 1: the shapes ranked by the line error of their halves, the best four fitted
	float rank[64];
	int order[64];
	for (int s = 0; s < 64; s++) {
		int l0[16], l1[16], n0 = 0, n1 = 0;
		for (int i = 0; i < 16; i++)
			if (SHAPES[s] >> i & 1) l1[n1++] = i;
			else l0[n0++] = i;
		rank[s] = LineError(px, l0, n0) + LineError(px, l1, n1);
		order[s] = s;
	}
	std::partial_sort(order, order + 4, order + 64, [&](int a, int b) { return rank[a] < rank[b]; });
	// (16 fitted instead of 4: 0.03 dB on grass1.pcx at twice the time)
	for (int k = 0; k < 4; k++) {
		const int s = order[k];
		int l[2][16], n[2] = { 0, 0 };
		for (int i = 0; i < 16; i++) {
			const int sub = SHAPES[s] >> i & 1;
			l[sub][n[sub]++] = i;
		}
		Sub f[2];
		for (int sub = 0; sub < 2; sub++)
			f[sub] = FitSubset(px, l[sub], n[sub], 6, 3, true);
		const long long err = f[0].err + f[1].err;
		if (err >= bestErr)
			continue;
		bestErr = err;
		// the indices per pixel, the anchors (pixel 0, the shape's second) with their top bit clear
		int idx[16];
		for (int sub = 0; sub < 2; sub++) {
			const int anchor = sub == 0 ? 0 : ANCHOR2[s];
			int ai = -1;
			for (int j = 0; j < n[sub]; j++) if (l[sub][j] == anchor) ai = j;
			if (ai >= 0 && (f[sub].idx[ai] & 4)) {
				for (int c = 0; c < 3; c++) std::swap(f[sub].q[0][c], f[sub].q[1][c]);
				for (int j = 0; j < n[sub]; j++) f[sub].idx[j] = (uint8_t)(7 - f[sub].idx[j]);
			}
			for (int j = 0; j < n[sub]; j++) idx[l[sub][j]] = f[sub].idx[j];
		}
		std::memset(out, 0, 16);
		Bits bits{ out };
		bits.put(1 << 1, 2); // mode 1
		bits.put((uint32_t)s, 6);
		for (int c = 0; c < 3; c++)
			for (int sub = 0; sub < 2; sub++) { bits.put((uint32_t)f[sub].q[0][c], 6); bits.put((uint32_t)f[sub].q[1][c], 6); }
		bits.put((uint32_t)f[0].p[0], 1);
		bits.put((uint32_t)f[1].p[0], 1);
		for (int i = 0; i < 16; i++)
			bits.put((uint32_t)idx[i], i == 0 || i == ANCHOR2[s] ? 2 : 3);
	}
}

std::vector<uint8_t> Bc7::Encode(const uint8_t* rgba, int width, int height)
{
	const int bw = width / 4, bh = height / 4;
	std::vector<uint8_t> out((size_t)bw * bh * 16);
	uint8_t block[64];
	for (int by = 0; by < bh; by++)
		for (int bx = 0; bx < bw; bx++) {
			for (int y = 0; y < 4; y++)
				std::memcpy(block + y * 16, rgba + ((size_t)(by * 4 + y) * width + bx * 4) * 4, 16);
			EncodeBlock(block, &out[((size_t)by * bw + bx) * 16]);
		}
	return out;
}

bool Bc7::WriteDds(const std::string& path, const uint8_t* rgba, int width, int height, bool srgb)
{
	if (width < 4 || height < 4 || width % 4 || height % 4)
		return false;
	// the levels: the picture, then halves (a box filter) while both sides stay multiples of 4
	std::vector<std::vector<uint8_t>> levels;
	std::vector<std::pair<int, int>> sizes;
	levels.emplace_back(rgba, rgba + (size_t)width * height * 4);
	sizes.push_back({ width, height });
	float lin[256];
	for (int i = 0; i < 256; i++) {
		const float x = i / 255.0f;
		lin[i] = srgb ? (x <= 0.04045f ? x / 12.92f : std::pow((x + 0.055f) / 1.055f, 2.4f)) : x;
	}
	auto toEnc = [&](float x) {
		x = std::clamp(x, 0.0f, 1.0f);
		const float y = srgb ? (x <= 0.0031308f ? x * 12.92f : 1.055f * std::pow(x, 1 / 2.4f) - 0.055f) : x;
		return (uint8_t)std::lround(y * 255.0f);
	};
	while (sizes.back().first % 8 == 0 && sizes.back().second % 8 == 0) {
		const auto [w, h] = sizes.back();
		const std::vector<uint8_t>& src = levels.back();
		std::vector<uint8_t> dst((size_t)(w / 2) * (h / 2) * 4);
		for (int y = 0; y < h / 2; y++)
			for (int x = 0; x < w / 2; x++)
				for (int c = 0; c < 4; c++) {
					auto at = [&](int xx, int yy) { return src[((size_t)yy * w + xx) * 4 + c]; };
					uint8_t& o = dst[((size_t)y * (w / 2) + x) * 4 + c];
					if (c == 3)
						o = (uint8_t)((at(2 * x, 2 * y) + at(2 * x + 1, 2 * y) + at(2 * x, 2 * y + 1) + at(2 * x + 1, 2 * y + 1) + 2) / 4);
					else
						o = toEnc((lin[at(2 * x, 2 * y)] + lin[at(2 * x + 1, 2 * y)] + lin[at(2 * x, 2 * y + 1)] + lin[at(2 * x + 1, 2 * y + 1)]) / 4);
				}
		levels.push_back(std::move(dst));
		sizes.push_back({ w / 2, h / 2 });
	}
	FILE* f = fopen(path.c_str(), "wb");
	if (!f)
		return false;
	uint32_t hdr[32] = {};
	hdr[0] = 0x20534444; // "DDS "
	hdr[1] = 124;
	hdr[2] = 0x1 | 0x2 | 0x4 | 0x1000 | 0x20000 | 0x80000; // caps, height, width, pixel format, mip count, linear size
	hdr[3] = (uint32_t)height;
	hdr[4] = (uint32_t)width;
	hdr[5] = (uint32_t)((width / 4) * (height / 4) * 16);
	hdr[7] = (uint32_t)levels.size();
	hdr[19] = 32;         // the pixel format's size
	hdr[20] = 0x4;        // DDPF_FOURCC
	hdr[21] = 0x30315844; // "DX10"
	hdr[27] = 0x1000 | 0x400000 | 0x8; // texture, mipmap, complex
	const uint32_t dx10[5] = { 98, 3, 0, 1, 0 }; // DXGI_FORMAT_BC7_UNORM, a 2D texture, one of it
	bool ok = fwrite(hdr, 4, 32, f) == 32 && fwrite(dx10, 4, 5, f) == 5;
	for (size_t l = 0; l < levels.size() && ok; l++) {
		const std::vector<uint8_t> blocks = Encode(levels[l].data(), sizes[l].first, sizes[l].second);
		ok = fwrite(blocks.data(), 1, blocks.size(), f) == blocks.size();
	}
	return fclose(f) == 0 && ok;
}
