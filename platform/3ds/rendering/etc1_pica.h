/**************************************************************************/
/*  etc1_pica.h                                                           */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#pragma once

// Godot ETC1 / ETC2 blocks to the PICA200's ETC1 and ETC1A4 layouts.
//
// Godot stores ETC blocks the way GL wants them: 8 bytes, big-endian, rows
// top to bottom. The PICA200 reads each block as a little-endian u64, and it
// samples v = 0 at the end of the texture (see image_offset()), so every block
// is also flipped upside down here. No GPU or libctru types: the export test
// and the editor can use this file too.

#include <cstdint>

namespace Citro3D {
namespace ETC {

inline uint64_t read_be64(const uint8_t *p_src) {
	uint64_t v = 0;
	for (int i = 0; i < 8; i++) {
		v = (v << 8) | p_src[i];
	}
	return v;
}

inline int sign3(uint32_t p_v) {
	return (p_v & 4) ? (int)p_v - 8 : (int)p_v;
}

// True when a standard color block decodes the same as ETC1: no ETC2 T, H or
// planar mode (those are differential blocks whose sum overflows 0..31).
inline bool color_block_is_etc1(const uint8_t *p_block) {
	uint32_t hi = (uint32_t)(read_be64(p_block) >> 32);
	if (!(hi & 2)) {
		return true; // Individual mode.
	}
	for (int shift = 27; shift >= 11; shift -= 8) {
		int base = (hi >> shift) & 31;
		int sum = base + sign3((hi >> (shift - 3)) & 7);
		if (sum < 0 || sum > 31) {
			return false;
		}
	}
	return true;
}

// Reverses the four bits of every nibble: flips each column of a block.
inline uint32_t flip_nibbles(uint32_t p_v) {
	p_v = ((p_v & 0x55555555u) << 1) | ((p_v >> 1) & 0x55555555u);
	p_v = ((p_v & 0x33333333u) << 2) | ((p_v >> 2) & 0x33333333u);
	return p_v;
}

// A standard ETC1 color block as the PICA200 value, flipped vertically.
// Pixel (x, y) uses bit x * 4 + y of each index plane, so flipping y reverses
// each nibble. When the block is split top/bottom (flip bit set) the two
// halves trade places too. A differential block with a delta of -4 can't be
// mirrored exactly (+4 doesn't fit), so that rare block becomes an individual
// block with 4-bit colors (a little lossy).
inline uint64_t color_block_to_pica(const uint8_t *p_block) {
	uint64_t v = read_be64(p_block);
	uint32_t hi = (uint32_t)(v >> 32);
	uint32_t lo = flip_nibbles((uint32_t)v);
	if (hi & 1) {
		uint32_t cw1 = (hi >> 5) & 7, cw2 = (hi >> 2) & 7;
		hi = (hi & ~0xFCu) | (cw2 << 5) | (cw1 << 2);
		if (!(hi & 2)) {
			// Individual: swap the 4-bit color pairs (R1 R2 G1 G2 B1 B2).
			uint32_t c = hi >> 8;
			c = ((c & 0x0F0F0F) << 4) | ((c >> 4) & 0x0F0F0F);
			hi = (c << 8) | (hi & 0xFF);
		} else {
			uint32_t out = hi & 0xFF;
			bool exact = true;
			int base[3], sum[3];
			for (int ch = 0; ch < 3; ch++) {
				int shift = 27 - ch * 8;
				base[ch] = (hi >> shift) & 31;
				int d = sign3((hi >> (shift - 3)) & 7);
				sum[ch] = base[ch] + d;
				if (d == -4) {
					exact = false;
				}
				out |= (uint32_t)(sum[ch] & 31) << shift;
				out |= (uint32_t)(-d & 7) << (shift - 3);
			}
			if (exact) {
				hi = out;
			} else {
				out = hi & 0xFC; // Codewords and flip bit; diff bit cleared.
				out |= 1;
				for (int ch = 0; ch < 3; ch++) {
					// Nearest 4-bit color (expanded x * 17) to each 5-bit one.
					auto to4 = [](int c5) {
						int e = (c5 << 3) | (c5 >> 2);
						return (uint32_t)((e + 8) / 17);
					};
					int shift = 28 - ch * 8;
					out |= to4(sum[ch]) << shift; // New top half = old bottom.
					out |= to4(base[ch]) << (shift - 4);
				}
				hi = out;
			}
		}
	}
	return ((uint64_t)hi << 32) | lo;
}

// An EAC alpha block (ETC2 RGBA8, first 8 bytes) as the PICA200's ETC1A4
// alpha word, flipped vertically: 4 bits per texel at bit 4 * (x * 4 + y).
inline uint64_t eac_alpha_to_pica(const uint8_t *p_block) {
	static const int8_t modifiers[16][8] = {
		{ -3, -6, -9, -15, 2, 5, 8, 14 },
		{ -3, -7, -10, -13, 2, 6, 9, 12 },
		{ -2, -5, -8, -13, 1, 4, 7, 12 },
		{ -2, -4, -6, -13, 1, 3, 5, 12 },
		{ -3, -6, -8, -12, 2, 5, 7, 11 },
		{ -3, -7, -9, -11, 2, 6, 8, 10 },
		{ -4, -7, -8, -11, 3, 6, 7, 10 },
		{ -3, -5, -8, -11, 2, 4, 7, 10 },
		{ -2, -6, -8, -10, 1, 5, 7, 9 },
		{ -2, -5, -8, -10, 1, 4, 7, 9 },
		{ -2, -4, -8, -10, 1, 3, 7, 9 },
		{ -2, -5, -7, -10, 1, 4, 6, 9 },
		{ -3, -4, -7, -10, 2, 3, 6, 9 },
		{ -1, -2, -3, -10, 0, 1, 2, 9 },
		{ -4, -6, -8, -9, 3, 5, 7, 8 },
		{ -3, -5, -7, -9, 2, 4, 6, 8 },
	};
	uint64_t v = read_be64(p_block);
	int base = (int)(v >> 56);
	int mult = (int)(v >> 52) & 15;
	const int8_t *mod = modifiers[(v >> 48) & 15];
	uint64_t out = 0;
	for (int i = 0; i < 16; i++) {
		// Texel i = x * 4 + y, three bits each from the top of the index field.
		int idx = (int)(v >> (45 - i * 3)) & 7;
		int a = base + mod[idx] * mult;
		a = a < 0 ? 0 : (a > 255 ? 255 : a);
		uint64_t a4 = (uint64_t)((a * 15 + 127) / 255);
		int x = i >> 2, y = 3 - (i & 3);
		out |= a4 << (4 * (x * 4 + y));
	}
	return out;
}

// Byte offset of 4x4 block (p_bx, p_by) of a Godot image (block row 0 at the
// top) inside a p_w x p_h PICA200 texture: 8x8 tiles, four blocks per tile in
// Z order, block rows counted from the end like image rows.
inline uint32_t block_offset(uint32_t p_bx, uint32_t p_by, uint32_t p_w, uint32_t p_h, uint32_t p_block_bytes) {
	uint32_t my = p_h / 4 - 1 - p_by;
	uint32_t tile = (my >> 1) * (p_w >> 3) + (p_bx >> 1);
	uint32_t sub = (p_bx & 1) | ((my & 1) << 1);
	return (tile * 4 + sub) * p_block_bytes;
}

} // namespace ETC
} // namespace Citro3D
