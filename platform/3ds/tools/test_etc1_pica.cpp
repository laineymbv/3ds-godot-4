// Host-side check of platform/3ds/rendering/etc1_pica.h against etcpak's own
// ETC1/EAC decoder: every converted block must decode to the same texels,
// upside down. Run from the repository root:
//
//   E=thirdparty/etcpak
//   g++ -O2 -std=c++17 -w -I. -I$E platform/3ds/tools/test_etc1_pica.cpp \
//       $E/ProcessRGB.cpp $E/DecodeRGB.cpp $E/Dither.cpp $E/Tables.cpp -o /tmp/test_etc1_pica && /tmp/test_etc1_pica
//
// This checks the flip and the bit layout only. It can't check what the PICA200
// itself expects; that needs Azahar or a console.

#include "DecodeRGB.hpp"
#include "ProcessRGB.hpp"

#include "platform/3ds/rendering/etc1_pica.h"

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
using namespace Citro3D::ETC;

static void to_be(uint64_t v, uint8_t *o) {
	for (int i = 7; i >= 0; i--) {
		o[i] = v & 0xFF;
		v >>= 8;
	}
}
static int ch(uint32_t px, int c) {
	return (px >> (c * 8)) & 0xFF;
}

int fails = 0, fallback_blocks = 0, total = 0;
int max_fallback_err = 0;

void check_color(const uint8_t *std) {
	if (!color_block_is_etc1(std)) {
		return;
	}
	total++;
	uint32_t a[16], b[16];
	DecodeRGBBlock(std, a, 4);
	uint64_t p = color_block_to_pica(std);
	uint8_t pb[8];
	to_be(p, pb);
	DecodeRGBBlock(pb, b, 4);
	uint32_t hi = (uint32_t)(read_be64(std) >> 32);
	bool fb = false;
	if ((hi & 3) == 3) {
		for (int s = 24; s >= 8; s -= 8) {
			if (((hi >> s) & 7) == 4) {
				fb = true;
			}
		}
	}
	if (fb) {
		fallback_blocks++;
	}
	for (int y = 0; y < 4; y++) {
		for (int x = 0; x < 4; x++) {
			uint32_t pa = a[(3 - y) * 4 + x], pbx = b[y * 4 + x];
			for (int c = 0; c < 3; c++) {
				int d = abs(ch(pa, c) - ch(pbx, c));
				if (fb) {
					if (d > max_fallback_err) {
						max_fallback_err = d;
					}
					if (d > 12) {
						fails++;
					}
				} else if (d) {
					if (fails < 5) {
						printf("mismatch hi=%08x x=%d y=%d c=%d %d vs %d\n", hi, x, y, c, ch(pa, c), ch(pbx, c));
					}
					fails++;
				}
			}
		}
	}
	if (!color_block_is_etc1(pb)) {
		printf("output not etc1 %08x\n", hi);
		fails++;
	}
}

int main() {
	srand(1);
	// Random raw blocks: exercises every mode and both flips.
	for (int i = 0; i < 2000000; i++) {
		uint8_t blk[8];
		for (int j = 0; j < 8; j++) {
			blk[j] = rand() & 0xFF;
		}
		check_color(blk);
	}
	printf("raw: %d blocks, %d fallback, max fallback err %d, fails %d\n", total, fallback_blocks, max_fallback_err, fails);

	// Real encoder output with alpha: 64x64 noisy gradient.
	const int W = 64, H = 64;
	std::vector<uint32_t> img(W * H), aimg(W * H);
	for (int y = 0; y < H; y++) {
		for (int x = 0; x < W; x++) {
			int r = x * 4, g = y * 4, b = (x * y) & 255, al = ((x + y) * 3 + rand() % 40) & 255;
			img[y * W + x] = b | (g << 8) | (r << 16) | (al << 24); // BGRA
			aimg[y * W + x] = al << 16;
		}
	}
	int blocks = W * H / 16;
	std::vector<uint64_t> color(blocks), alpha(blocks);
	CompressEtc1RgbDither(img.data(), color.data(), blocks, W);
	CompressEacR(aimg.data(), alpha.data(), blocks, W);
	int afails = 0, amax = 0;
	for (int i = 0; i < blocks; i++) {
		check_color((const uint8_t *)&color[i]);
		uint64_t pair[2] = { alpha[i], color[i] };
		uint32_t dec[16];
		DecodeRGBABlock(pair, dec, 4);
		uint64_t pa = eac_alpha_to_pica((const uint8_t *)&alpha[i]);
		for (int y = 0; y < 4; y++) {
			for (int x = 0; x < 4; x++) {
				int ref = ch(dec[(3 - y) * 4 + x], 3);
				int ref4 = (ref * 15 + 127) / 255;
				int got = (pa >> (4 * (x * 4 + y))) & 15;
				if (ref4 != got) {
					afails++;
				}
			}
		}
	}
	// Block offsets: every block of a 32x16 texture lands on a distinct slot.
	std::vector<int> used(32 * 16 / 16, 0);
	for (int by = 0; by < 4; by++) {
		for (int bx = 0; bx < 8; bx++) {
			used[block_offset(bx, by, 32, 16, 8) / 8]++;
		}
	}
	int ofails = 0;
	for (int u : used) {
		if (u != 1) {
			ofails++;
		}
	}
	// Block (0,0) of the image (top-left) sits in the last tile row.
	printf("offset(0,0)=%u (expect tile row 1 => %u)\n", block_offset(0, 0, 32, 16, 8), (1 * 4 * 4 + 2) * 8);
	printf("encoded: total %d, color fails %d, alpha fails %d, offset fails %d\n", total, fails, afails, ofails);
	return fails || afails || ofails;
}
