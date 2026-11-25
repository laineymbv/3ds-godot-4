/**************************************************************************/
/*  citro3d_common.h                                                      */
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

// Shared helpers for the citro3d (PICA200) renderer of the 3DS platform.

#include "platform/3ds/ctru.h"

#include <citro3d.h>

#include "core/math/color.h"
#include "core/math/math_funcs.h"
#include "core/math/projection.h"

namespace Citro3D {

// Smallest power of two >= p_v, clamped to the PICA200 texture limits.
inline int pot(int p_v) {
	int r = 8;
	while (r < p_v && r < 1024) {
		r <<= 1;
	}
	return r;
}

// Offset of texel (x, y) inside a tiled texture `p_w` texels wide, where y
// counts tile rows from the start of memory. The PICA200 samples v = 0 at the
// END of the texture, so Godot's top image row (v = 0) goes in the last row:
// use image_offset() for images.
inline uint32_t tiled_offset(uint32_t p_x, uint32_t p_y, uint32_t p_w) {
	uint32_t x = p_x & 7, y = p_y & 7;
	uint32_t morton = (x & 1) | ((y & 1) << 1) | ((x & 2) << 1) | ((y & 2) << 2) | ((x & 4) << 2) | ((y & 4) << 3);
	return (((p_y >> 3) * (p_w >> 3) + (p_x >> 3)) << 6) + morton;
}

// Texel (x, y) of a Godot image (y = 0 at the top) in a p_w x p_h texture.
inline uint32_t image_offset(uint32_t p_x, uint32_t p_y, uint32_t p_w, uint32_t p_h) {
	return tiled_offset(p_x, p_h - 1 - p_y, p_w);
}

inline u32 color_to_u32(const Color &p_c) {
	// GPU_RGBA8 / clear color layout: 0xRRGGBBAA.
	return ((u32)CLAMP(p_c.r * 255.0f, 0.0f, 255.0f) << 24) | ((u32)CLAMP(p_c.g * 255.0f, 0.0f, 255.0f) << 16) |
			((u32)CLAMP(p_c.b * 255.0f, 0.0f, 255.0f) << 8) | (u32)CLAMP(p_c.a * 255.0f, 0.0f, 255.0f);
}

// TEV constant colors are 0xAABBGGRR.
inline u32 color_to_abgr(const Color &p_c) {
	return ((u32)CLAMP(p_c.a * 255.0f, 0.0f, 255.0f) << 24) | ((u32)CLAMP(p_c.b * 255.0f, 0.0f, 255.0f) << 16) |
			((u32)CLAMP(p_c.g * 255.0f, 0.0f, 255.0f) << 8) | (u32)CLAMP(p_c.r * 255.0f, 0.0f, 255.0f);
}

// Godot projections are OpenGL style (z in [-1, 1]); the PICA200 wants
// z in [-1, 0] with w-buffer style mapping, the way citro3d's Mtx_Persp does.
// Render targets are drawn upside down (y flipped) so that their image has
// the same layout as uploaded textures (see image_offset()).
inline void projection_to_c3d(const Projection &p_proj, C3D_Mtx *r_mtx) {
	for (int row = 0; row < 4; row++) {
		float x = p_proj.columns[0][row];
		float y = p_proj.columns[1][row];
		float z = p_proj.columns[2][row];
		float w = p_proj.columns[3][row];
		r_mtx->r[row].x = x;
		r_mtx->r[row].y = y;
		r_mtx->r[row].z = z;
		r_mtx->r[row].w = w;
	}
	// z' = 0.5 z - 0.5 w
	for (int c = 0; c < 4; c++) {
		r_mtx->r[2].c[c] = 0.5f * r_mtx->r[2].c[c] - 0.5f * r_mtx->r[3].c[c];
	}
	// y' = -y
	for (int c = 0; c < 4; c++) {
		r_mtx->r[1].c[c] = -r_mtx->r[1].c[c];
	}
}

} // namespace Citro3D
