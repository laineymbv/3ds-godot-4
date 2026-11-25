/**************************************************************************/
/*  rasterizer_canvas_citro3d.h                                           */
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

#include "citro3d_common.h"

#include "servers/rendering/dummy/rasterizer_canvas_dummy.h"

namespace Citro3D {
struct Texture;
struct RenderTarget;
} // namespace Citro3D

// 2D canvas items on the PICA200: CPU-transformed vertices streamed into one
// buffer, batched by texture/blend/clip, recorded once and drawn per eye.
class RasterizerCanvasCitro3D : public RasterizerCanvasDummy {
	struct PolygonData {
		Vector<int> indices;
		Vector<Point2> points;
		Vector<Color> colors;
		Vector<Point2> uvs;
	};
	HashMap<PolygonID, PolygonData> polygons;
	PolygonID polygon_id_counter = 1;

	enum BlendMode {
		BLEND_MIX,
		BLEND_ADD,
		BLEND_SUB,
		BLEND_MUL,
		BLEND_PREMUL,
		BLEND_DISABLED,
	};

	struct Batch {
		uint32_t first = 0;
		uint32_t count = 0;
		Citro3D::Texture *texture = nullptr;
		bool nearest = false;
		bool repeat = false;
		BlendMode blend = BLEND_MIX;
		bool clip = false;
		Rect2i clip_rect;
	};
	LocalVector<Batch> batches;
	Batch current;
	bool batch_open = false;

	void _flush();
	void _set_state(Citro3D::Texture *p_tex, bool p_nearest, bool p_repeat, BlendMode p_blend, bool p_clip, const Rect2i &p_clip_rect);
	void _add_triangle(const Vector2 *p_pos, const Vector2 *p_uv, const Color *p_col);
	void _add_quad(const Vector2 p_pos[4], const Vector2 p_uv[4], const Color &p_col);
	void _add_mesh(RID p_mesh, const Transform2D &p_xform, const Color &p_modulate, Citro3D::Texture *p_tex);
	void _draw_batches(Citro3D::RenderTarget *p_rt);

public:
	void initialize() {}
	void finalize() {}

	PolygonID request_polygon(const Vector<int> &p_indices, const Vector<Point2> &p_points, const Vector<Color> &p_colors, const Vector<Point2> &p_uvs = Vector<Point2>(), const Vector<int> &p_bones = Vector<int>(), const Vector<float> &p_weights = Vector<float>(), int p_count = -1) override;
	void free_polygon(PolygonID p_polygon) override;

	void canvas_render_items(RID p_to_render_target, Item *p_item_list, const Color &p_modulate, Light *p_light_list, Light *p_directional_list, const Transform2D &p_canvas_transform, RSE::CanvasItemTextureFilter p_default_filter, RSE::CanvasItemTextureRepeat p_default_repeat, bool p_snap_2d_vertices_to_pixel, bool &r_sdf_used, RenderingServerTypes::RenderInfo *r_render_info = nullptr) override;
};
