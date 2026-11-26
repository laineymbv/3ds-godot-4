/**************************************************************************/
/*  rasterizer_canvas_citro3d.cpp                                         */
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

#include "rasterizer_canvas_citro3d.h"

#include "material_storage_citro3d.h"
#include "mesh_storage_citro3d.h"
#include "rasterizer_citro3d.h"
#include "texture_storage_citro3d.h"

using namespace Citro3D;

RendererCanvasRender::PolygonID RasterizerCanvasCitro3D::request_polygon(const Vector<int> &p_indices, const Vector<Point2> &p_points, const Vector<Color> &p_colors, const Vector<Point2> &p_uvs, const Vector<int> &p_bones, const Vector<float> &p_weights, int p_count) {
	PolygonData pd;
	int count = p_count < 0 ? p_indices.size() : p_count * 3;
	pd.indices = p_indices.slice(0, count);
	pd.points = p_points;
	pd.colors = p_colors;
	pd.uvs = p_uvs;
	PolygonID id = polygon_id_counter++;
	polygons[id] = pd;
	return id;
}

void RasterizerCanvasCitro3D::free_polygon(PolygonID p_polygon) {
	polygons.erase(p_polygon);
}

void RasterizerCanvasCitro3D::_flush() {
	// (Batches only; pending command triangles go through _emit_pending.)
	if (batch_open && current.count > 0) {
		batches.push_back(current);
	}
	batch_open = false;
	current.count = 0;
}

void RasterizerCanvasCitro3D::_set_state(Texture *p_tex, bool p_nearest, bool p_repeat, BlendMode p_blend, bool p_clip, const Rect2i &p_clip_rect) {
	_emit_pending(); // Finish the previous command.
	command_state = Batch();
	command_state.texture = p_tex;
	command_state.nearest = p_nearest;
	command_state.repeat = p_repeat;
	command_state.blend = p_blend;
	command_state.clip = p_clip;
	command_state.clip_rect = p_clip_rect;
	command_state.depth = item_depth;
}

void RasterizerCanvasCitro3D::_open_batch(const Batch &p_state) {
	if (batch_open && current.texture == p_state.texture && current.nearest == p_state.nearest && current.repeat == p_state.repeat && current.blend == p_state.blend && current.clip == p_state.clip && (!p_state.clip || current.clip_rect == p_state.clip_rect) && current.light_pass == p_state.light_pass && current.light_subtract == p_state.light_subtract && current.light_texture == p_state.light_texture && current.light_scale == p_state.light_scale && current.depth == p_state.depth) {
		return;
	}
	_flush();
	current = p_state;
	current.count = 0;
	batch_open = true;
}

static inline void _put(CanvasVertex &r_v, const Vector2 &p_pos, const Vector2 &p_uv, const Color &p_col) {
	r_v.x = p_pos.x;
	r_v.y = p_pos.y;
	r_v.u = p_uv.x;
	r_v.v = p_uv.y;
	r_v.color[0] = (uint8_t)CLAMP(p_col.r * 255.0f, 0.0f, 255.0f);
	r_v.color[1] = (uint8_t)CLAMP(p_col.g * 255.0f, 0.0f, 255.0f);
	r_v.color[2] = (uint8_t)CLAMP(p_col.b * 255.0f, 0.0f, 255.0f);
	r_v.color[3] = (uint8_t)CLAMP(p_col.a * 255.0f, 0.0f, 255.0f);
}

void RasterizerCanvasCitro3D::_add_triangle(const Vector2 *p_pos, const Vector2 *p_uv, const Color *p_col) {
	for (int i = 0; i < 3; i++) {
		pending.push_back({ p_pos[i], p_uv[i], p_col[i] });
	}
}

void RasterizerCanvasCitro3D::_emit_pending() {
	if (pending.is_empty()) {
		return;
	}
	RasterizerCitro3D *r = RasterizerCitro3D::get_singleton();
	// Pass 0 is the item itself; then one additive pass per light.
	for (int pass = -1; pass < (int)item_lights.size(); pass++) {
		Batch state = command_state;
		const LightInfo *li = pass >= 0 ? item_lights[pass] : nullptr;
		if (li) {
			state.light_pass = true;
			state.light_subtract = li->light->blend_mode == RSE::CANVAS_LIGHT_BLEND_MODE_SUB;
			state.light_texture = li->texture;
			state.light_scale = li->scale;
		}
		_open_batch(state);
		uint32_t first;
		CanvasVertex *v = r->stream_alloc(pending.size(), first);
		if (!v) {
			break;
		}
		if (current.count == 0) {
			current.first = first;
		} else if (current.first + current.count != first) {
			Batch b = current;
			_flush();
			current = b;
			current.first = first;
			current.count = 0;
			batch_open = true;
		}
		for (uint32_t i = 0; i < pending.size(); i++) {
			const PendingVertex &pv = pending[i];
			Color c = li ? pv.color * li->color : pv.color * canvas_modulate;
			_put(v[i], pv.pos, pv.uv, c);
			if (li && li->texture) {
				Vector2 luv = li->to_uv.xform(pv.pos) * li->texture->uv_scale();
				v[i].lu = luv.x;
				v[i].lv = luv.y;
			} else {
				v[i].lu = v[i].lv = 0;
			}
		}
		current.count += pending.size();
	}
	pending.clear();
}

void RasterizerCanvasCitro3D::_add_quad(const Vector2 p_pos[4], const Vector2 p_uv[4], const Color &p_col) {
	Color c[3] = { p_col, p_col, p_col };
	Vector2 a[3] = { p_pos[0], p_pos[1], p_pos[2] };
	Vector2 uv_a[3] = { p_uv[0], p_uv[1], p_uv[2] };
	_add_triangle(a, uv_a, c);
	Vector2 b[3] = { p_pos[0], p_pos[2], p_pos[3] };
	Vector2 uv_b[3] = { p_uv[0], p_uv[2], p_uv[3] };
	_add_triangle(b, uv_b, c);
}

void RasterizerCanvasCitro3D::_draw_batches(RenderTarget *p_rt, int p_eye, int p_eye_count) {
	RasterizerCitro3D *r = RasterizerCitro3D::get_singleton();
	r->bind_canvas_program();

	C3D_Mtx proj;
	// y flipped like the scene, see projection_to_c3d().
	Mtx_Ortho(&proj, 0.0f, (float)p_rt->size.x, 0.0f, (float)p_rt->size.y, 1.0f, -1.0f, true);
	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, r->canvas_uniforms.projection, &proj);
	int bound_shift = 0;
	const float slider = r->get_3d_slider();
	C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR);
	C3D_CullFace(GPU_CULL_NONE);
	C3D_AlphaTest(false, GPU_ALWAYS, 0);
	for (int i = 2; i < 6; i++) {
		C3D_TexEnvInit(C3D_GetTexEnv(i));
	}

	Texture *bound_tex = nullptr;
	int bound_tev = -1;
	int bound_blend = -1;
	int bound_clip = -1;
	Rect2i bound_clip_rect;
	for (const Batch &b : batches) {
		// Manual stereo depth: each eye sees the item shifted sideways, whole
		// pixels so text and sprites stay sharp. Left eye shifts right for
		// items in front of the screen.
		int shift = 0;
		if (p_eye_count == 2 && b.depth != 0.0f) {
			shift = (int)Math::round(b.depth * slider * 0.5f) * (p_eye == 0 ? 1 : -1);
		}
		if (shift != bound_shift) {
			Mtx_Ortho(&proj, (float)-shift, (float)(p_rt->size.x - shift), 0.0f, (float)p_rt->size.y, 1.0f, -1.0f, true);
			C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, r->canvas_uniforms.projection, &proj);
			bound_shift = shift;
			bound_clip = -1; // The scissor moves with it.
		}
		if (b.texture) {
			C3D_Tex *t = &b.texture->tex;
			GPU_TEXTURE_FILTER_PARAM f = b.nearest ? GPU_NEAREST : GPU_LINEAR;
			GPU_TEXTURE_WRAP_PARAM w = b.repeat ? GPU_REPEAT : GPU_CLAMP_TO_EDGE;
			C3D_TexSetFilter(t, f, f);
			C3D_TexSetWrap(t, w, w);
			if (b.texture != bound_tex) {
				C3D_TexBind(0, t);
				bound_tex = b.texture;
				TextureStorage::get_singleton()->mark_used(b.texture);
			}
		}
		if (b.light_texture) {
			C3D_Tex *lt = &b.light_texture->tex;
			C3D_TexSetFilter(lt, GPU_LINEAR, GPU_LINEAR);
			C3D_TexSetWrap(lt, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
			C3D_TexBind(1, lt);
		}
		int tev = (b.texture ? 1 : 0) | (b.light_pass ? 2 : 0) | (b.light_texture ? 4 : 0) | (b.light_scale << 3);
		if (tev != bound_tev) {
			C3D_TexEnv *env = C3D_GetTexEnv(0);
			C3D_TexEnvInit(env);
			if (b.texture) {
				C3D_TexEnvSrc(env, C3D_Both, GPU_TEXTURE0, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
				C3D_TexEnvFunc(env, C3D_Both, GPU_MODULATE);
			} else {
				C3D_TexEnvSrc(env, C3D_Both, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
				C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);
			}
			// Light pass: x the light texture, scaled back up for energy > 1.
			C3D_TexEnv *env1 = C3D_GetTexEnv(1);
			C3D_TexEnvInit(env1);
			if (b.light_pass && b.light_texture) {
				C3D_TexEnvSrc(env1, C3D_Both, GPU_PREVIOUS, GPU_TEXTURE1, GPU_PREVIOUS);
				C3D_TexEnvFunc(env1, C3D_Both, GPU_MODULATE);
			} else {
				C3D_TexEnvSrc(env1, C3D_Both, GPU_PREVIOUS, GPU_PREVIOUS, GPU_PREVIOUS);
				C3D_TexEnvFunc(env1, C3D_Both, GPU_REPLACE);
			}
			if (b.light_pass && b.light_scale > 1) {
				C3D_TexEnvScale(env1, C3D_RGB, b.light_scale == 4 ? GPU_TEVSCALE_4 : GPU_TEVSCALE_2);
			}
			bound_tev = tev;
		}
		int blend_key = b.light_pass ? (b.light_subtract ? 101 : 100) : (int)b.blend;
		if (blend_key != bound_blend) {
			if (b.light_pass) {
				// Lights add (or subtract) color, weighted by coverage; dst alpha stays.
				C3D_AlphaBlend(b.light_subtract ? GPU_BLEND_REVERSE_SUBTRACT : GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE, GPU_ZERO, GPU_ONE);
			} else {
				switch (b.blend) {
					case BLEND_ADD:
						C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE, GPU_SRC_ALPHA, GPU_ONE);
						break;
					case BLEND_SUB:
						C3D_AlphaBlend(GPU_BLEND_REVERSE_SUBTRACT, GPU_BLEND_REVERSE_SUBTRACT, GPU_SRC_ALPHA, GPU_ONE, GPU_SRC_ALPHA, GPU_ONE);
						break;
					case BLEND_MUL:
						C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_DST_COLOR, GPU_ZERO, GPU_DST_ALPHA, GPU_ZERO);
						break;
					case BLEND_PREMUL:
						C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ONE_MINUS_SRC_ALPHA, GPU_ONE, GPU_ONE_MINUS_SRC_ALPHA);
						break;
					case BLEND_DISABLED:
						C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
						break;
					default:
						C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA, GPU_ONE, GPU_ONE_MINUS_SRC_ALPHA);
						break;
				}
			}
			bound_blend = blend_key;
		}
		if ((int)b.clip != bound_clip || (b.clip && b.clip_rect != bound_clip_rect)) {
			if (b.clip) {
				// With the flipped projection, scissor rows match Godot's.
				Rect2i c = Rect2i(b.clip_rect.position + Point2i(shift, 0), b.clip_rect.size).intersection(Rect2i(Point2i(), p_rt->size));
				C3D_SetScissor(GPU_SCISSOR_NORMAL, c.position.x, c.position.y, c.position.x + c.size.x, c.position.y + c.size.y);
			} else {
				C3D_SetScissor(GPU_SCISSOR_DISABLE, 0, 0, 0, 0);
			}
			bound_clip = b.clip;
			bound_clip_rect = b.clip_rect;
		}
		C3D_DrawArrays(GPU_TRIANGLES, b.first, b.count);
	}
	C3D_SetScissor(GPU_SCISSOR_DISABLE, 0, 0, 0, 0);
}

static Vector2 _uv(const Texture *p_tex, const Vector2 &p_uv) {
	return p_tex ? p_uv * p_tex->uv_scale() : p_uv;
}

void RasterizerCanvasCitro3D::_add_mesh(RID p_mesh, const Transform2D &p_xform, const Color &p_modulate, Texture *p_tex) {
	Mesh *mesh = MeshStorage::get_singleton()->get_mesh(p_mesh);
	if (!mesh) {
		return;
	}
	for (const Surface &s : mesh->surfaces) {
		if (!s.vertices || s.primitive != RSE::PRIMITIVE_TRIANGLES) {
			continue;
		}
		int count = s.indices ? s.index_count : s.vertex_count;
		for (int i = 0; i + 2 < count; i += 3) {
			Vector2 pos[3];
			Vector2 uv[3];
			Color col[3];
			for (int k = 0; k < 3; k++) {
				int idx = s.indices ? s.indices[i + k] : i + k;
				const PackedVertex &v = s.vertices[idx];
				Vector3 p = s.quant.position(v);
				pos[k] = p_xform.xform(Vector2(p.x, p.y));
				uv[k] = _uv(p_tex, s.quant.uv(v));
				col[k] = VertexQuant::color(v) * p_modulate;
			}
			_add_triangle(pos, uv, col);
		}
	}
}

void RasterizerCanvasCitro3D::canvas_render_items(RID p_to_render_target, Item *p_item_list, const Color &p_modulate, Light *p_light_list, Light *p_directional_list, const Transform2D &p_canvas_transform, RSE::CanvasItemTextureFilter p_default_filter, RSE::CanvasItemTextureRepeat p_default_repeat, bool p_snap_2d_vertices_to_pixel, bool &r_sdf_used, RenderingServerTypes::RenderInfo *r_render_info) {
	r_sdf_used = false;
	TextureStorage *ts = TextureStorage::get_singleton();
	MaterialStorage *ms = MaterialStorage::get_singleton();
	RenderTarget *rt = ts->get_render_target(p_to_render_target);
	if (!rt || !rt->allocated) {
		return;
	}

	// A 2D-only viewport's clear happens before its first items.
	if (ts->render_target_is_clear_requested(p_to_render_target)) {
		ts->render_target_do_clear_request(p_to_render_target);
	}

	batches.clear();
	batch_open = false;
	current = Batch();
	pending.clear();
	canvas_modulate = p_modulate;

	// 2D lights: positional ones carry a texture mapped by light_shader_xform
	// (unit square -> render target pixels); directional ones light evenly.
	lights.clear();
	for (int list = 0; list < 2; list++) {
		for (Light *l = list == 0 ? p_light_list : p_directional_list; l; l = l->next_ptr) {
			if (!l->enabled || l->energy <= 0.0f) {
				continue;
			}
			LightInfo info;
			info.light = l;
			info.texture = list == 0 ? ts->resolve_texture(l->texture) : nullptr;
			if (list == 0 && !info.texture) {
				continue;
			}
			info.to_uv = l->light_shader_xform.affine_inverse();
			Color c = l->color * l->energy;
			float peak = MAX(c.r, MAX(c.g, c.b));
			info.scale = peak > 2.0f ? 4 : (peak > 1.0f ? 2 : 1);
			info.color = Color(c.r / info.scale, c.g / info.scale, c.b / info.scale, l->color.a);
			lights.push_back(info);
		}
	}

	static bool warned_mesh = false;
	for (Item *ci = p_item_list; ci; ci = ci->next) {
		Transform2D base_xform = p_canvas_transform * ci->final_transform;
		Transform2D xform = base_xform;
		_emit_pending();
		Color item_modulate = ci->final_modulate;
		item_lights.clear();
		for (const LightInfo &li : lights) {
			if ((li.light->item_mask & ci->light_mask) && ci->z_final >= li.light->z_min && ci->z_final <= li.light->z_max) {
				item_lights.push_back(&li);
			}
		}

		RSE::CanvasItemTextureFilter filter = ci->texture_filter == RSE::CANVAS_ITEM_TEXTURE_FILTER_DEFAULT ? p_default_filter : ci->texture_filter;
		bool nearest = filter == RSE::CANVAS_ITEM_TEXTURE_FILTER_NEAREST || filter == RSE::CANVAS_ITEM_TEXTURE_FILTER_NEAREST_WITH_MIPMAPS || filter == RSE::CANVAS_ITEM_TEXTURE_FILTER_NEAREST_WITH_MIPMAPS_ANISOTROPIC;
		RSE::CanvasItemTextureRepeat repeat_mode = ci->texture_repeat == RSE::CANVAS_ITEM_TEXTURE_REPEAT_DEFAULT ? p_default_repeat : ci->texture_repeat;
		bool repeat = repeat_mode == RSE::CANVAS_ITEM_TEXTURE_REPEAT_ENABLED || repeat_mode == RSE::CANVAS_ITEM_TEXTURE_REPEAT_MIRROR;

		BlendMode blend = BLEND_MIX;
		item_depth = 0.0f;
		RID material = ci->material_owner ? ci->material_owner->material : ci->material;
		if (material.is_valid()) {
			Material *m = ms->get_material(material);
			if (m) {
				item_depth = m->stereo_depth;
			}
			Shader *s = m ? ms->get_shader(m->shader) : nullptr;
			if (s) {
				switch (s->info.blend) {
					case Shader::BLEND_ADD:
						blend = BLEND_ADD;
						break;
					case Shader::BLEND_SUB:
						blend = BLEND_SUB;
						break;
					case Shader::BLEND_MUL:
						blend = BLEND_MUL;
						break;
					case Shader::BLEND_PREMUL_ALPHA:
						blend = BLEND_PREMUL;
						break;
					default:
						break;
				}
			}
		}

		bool clip = ci->final_clip_owner != nullptr;
		Rect2i clip_rect = clip ? Rect2i(ci->final_clip_owner->final_clip_rect) : Rect2i();

		for (const Item::Command *c = ci->commands; c; c = c->next) {
			switch (c->type) {
				case Item::Command::TYPE_RECT: {
					const Item::CommandRect *rect = static_cast<const Item::CommandRect *>(c);
					Color tex_mod(1, 1, 1, 1);
					Texture *tex = ts->resolve_texture(rect->texture, &tex_mod);
					bool tile = (rect->flags & CANVAS_RECT_TILE) && tex;
					_set_state(tex, nearest, repeat || tile, blend, clip, clip_rect);

					Rect2 src(0, 0, 1, 1);
					if (tex) {
						Size2 tsize(MAX(tex->width, 1), MAX(tex->height, 1));
						if (rect->flags & CANVAS_RECT_REGION) {
							src = Rect2(rect->source.position / tsize, rect->source.size / tsize);
						} else if (tile) {
							src = Rect2(Vector2(), rect->rect.size.abs() / tsize);
						}
					}
					Vector2 uv[4] = {
						src.position,
						src.position + Vector2(src.size.x, 0),
						src.position + src.size,
						src.position + Vector2(0, src.size.y),
					};
					if (rect->flags & CANVAS_RECT_FLIP_H) {
						SWAP(uv[0], uv[1]);
						SWAP(uv[2], uv[3]);
					}
					if (rect->flags & CANVAS_RECT_FLIP_V) {
						SWAP(uv[0], uv[3]);
						SWAP(uv[1], uv[2]);
					}
					if (rect->flags & CANVAS_RECT_TRANSPOSE) {
						SWAP(uv[1], uv[3]);
					}
					const Rect2 &d = rect->rect;
					Vector2 pos[4] = {
						xform.xform(d.position),
						xform.xform(d.position + Vector2(d.size.x, 0)),
						xform.xform(d.position + d.size),
						xform.xform(d.position + Vector2(0, d.size.y)),
					};
					for (int i = 0; i < 4; i++) {
						uv[i] = _uv(tex, uv[i]);
					}
					_add_quad(pos, uv, rect->modulate * item_modulate * tex_mod);
				} break;

				case Item::Command::TYPE_NINEPATCH: {
					const Item::CommandNinePatch *np = static_cast<const Item::CommandNinePatch *>(c);
					Color tex_mod(1, 1, 1, 1);
					Texture *tex = ts->resolve_texture(np->texture, &tex_mod);
					_set_state(tex, nearest, false, blend, clip, clip_rect);
					Size2 tsize = tex ? Size2(MAX(tex->width, 1), MAX(tex->height, 1)) : Size2(1, 1);
					Rect2 src = np->source.size == Size2() ? Rect2(Vector2(), tsize) : np->source;
					const Rect2 &d = np->rect;
					// margin order: left, top, right, bottom.
					float dx[4] = { d.position.x, d.position.x + np->margin[0], d.position.x + d.size.x - np->margin[2], d.position.x + d.size.x };
					float dy[4] = { d.position.y, d.position.y + np->margin[1], d.position.y + d.size.y - np->margin[3], d.position.y + d.size.y };
					float sx[4] = { src.position.x, src.position.x + np->margin[0], src.position.x + src.size.x - np->margin[2], src.position.x + src.size.x };
					float sy[4] = { src.position.y, src.position.y + np->margin[1], src.position.y + src.size.y - np->margin[3], src.position.y + src.size.y };
					Color col = np->color * item_modulate * tex_mod;
					for (int y = 0; y < 3; y++) {
						for (int x = 0; x < 3; x++) {
							if (x == 1 && y == 1 && !np->draw_center) {
								continue;
							}
							Vector2 pos[4] = {
								xform.xform(Vector2(dx[x], dy[y])),
								xform.xform(Vector2(dx[x + 1], dy[y])),
								xform.xform(Vector2(dx[x + 1], dy[y + 1])),
								xform.xform(Vector2(dx[x], dy[y + 1])),
							};
							Vector2 uv[4] = {
								_uv(tex, Vector2(sx[x], sy[y]) / tsize),
								_uv(tex, Vector2(sx[x + 1], sy[y]) / tsize),
								_uv(tex, Vector2(sx[x + 1], sy[y + 1]) / tsize),
								_uv(tex, Vector2(sx[x], sy[y + 1]) / tsize),
							};
							_add_quad(pos, uv, col);
						}
					}
				} break;

				case Item::Command::TYPE_POLYGON: {
					const Item::CommandPolygon *poly = static_cast<const Item::CommandPolygon *>(c);
					const PolygonData *pd = polygons.getptr(poly->polygon.polygon_id);
					if (!pd || poly->primitive != RSE::PRIMITIVE_TRIANGLES) {
						break;
					}
					Color tex_mod(1, 1, 1, 1);
					Texture *tex = ts->resolve_texture(poly->texture, &tex_mod);
					_set_state(tex, nearest, repeat, blend, clip, clip_rect);
					int pc = pd->points.size();
					bool per_vertex_color = pd->colors.size() == pc;
					Color single = pd->colors.size() == 1 ? pd->colors[0] : Color(1, 1, 1, 1);
					bool has_uv = pd->uvs.size() == pc;
					for (int i = 0; i + 2 < pd->indices.size(); i += 3) {
						Vector2 pos[3];
						Vector2 uv[3];
						Color col[3];
						bool ok = true;
						for (int k = 0; k < 3; k++) {
							int idx = pd->indices[i + k];
							if (idx < 0 || idx >= pc) {
								ok = false;
								break;
							}
							pos[k] = xform.xform(pd->points[idx]);
							uv[k] = has_uv ? _uv(tex, pd->uvs[idx]) : Vector2();
							col[k] = (per_vertex_color ? pd->colors[idx] : single) * item_modulate * tex_mod;
						}
						if (ok) {
							_add_triangle(pos, uv, col);
						}
					}
				} break;

				case Item::Command::TYPE_PRIMITIVE: {
					const Item::CommandPrimitive *prim = static_cast<const Item::CommandPrimitive *>(c);
					Color tex_mod(1, 1, 1, 1);
					Texture *tex = ts->resolve_texture(prim->texture, &tex_mod);
					_set_state(tex, nearest, repeat, blend, clip, clip_rect);
					Vector2 pos[4];
					Vector2 uv[4];
					Color col[4];
					for (uint32_t i = 0; i < prim->point_count && i < 4; i++) {
						pos[i] = xform.xform(prim->points[i]);
						uv[i] = _uv(tex, prim->uvs[i]);
						col[i] = prim->colors[i] * item_modulate * tex_mod;
					}
					if (prim->point_count <= 2) {
						// Points and hairlines become 1 px quads.
						Vector2 a = pos[0];
						Vector2 b = prim->point_count == 2 ? pos[1] : pos[0] + Vector2(1, 0);
						Vector2 dir = (b - a).normalized();
						if (dir == Vector2()) {
							dir = Vector2(1, 0);
						}
						Vector2 n = Vector2(-dir.y, dir.x) * 0.5f;
						Vector2 q[4] = { a - n, b - n, b + n, a + n };
						Vector2 quv[4] = { uv[0], uv[0], uv[0], uv[0] };
						_add_quad(q, quv, col[0]);
					} else {
						_add_triangle(pos, uv, col);
						if (prim->point_count == 4) {
							Vector2 p2[3] = { pos[0], pos[2], pos[3] };
							Vector2 u2[3] = { uv[0], uv[2], uv[3] };
							Color c2[3] = { col[0], col[2], col[3] };
							_add_triangle(p2, u2, c2);
						}
					}
				} break;

				case Item::Command::TYPE_TRANSFORM: {
					const Item::CommandTransform *t = static_cast<const Item::CommandTransform *>(c);
					xform = base_xform * t->xform;
				} break;

				case Item::Command::TYPE_CLIP_IGNORE: {
					const Item::CommandClipIgnore *ig = static_cast<const Item::CommandClipIgnore *>(c);
					clip = ig->ignore ? false : (ci->final_clip_owner != nullptr);
				} break;

				case Item::Command::TYPE_MESH: {
					const Item::CommandMesh *cm = static_cast<const Item::CommandMesh *>(c);
					Color tex_mod(1, 1, 1, 1);
					Texture *tex = ts->resolve_texture(cm->texture, &tex_mod);
					_set_state(tex, nearest, repeat, blend, clip, clip_rect);
					_add_mesh(cm->mesh, xform * cm->transform, cm->modulate * item_modulate * tex_mod, tex);
				} break;

				case Item::Command::TYPE_MULTIMESH: {
					const Item::CommandMultiMesh *cmm = static_cast<const Item::CommandMultiMesh *>(c);
					MultiMesh *mm = MeshStorage::get_singleton()->get_multimesh(cmm->multimesh);
					if (!mm || mm->xform_format != RSE::MULTIMESH_TRANSFORM_2D) {
						break;
					}
					Color tex_mod(1, 1, 1, 1);
					Texture *tex = ts->resolve_texture(cmm->texture, &tex_mod);
					_set_state(tex, nearest, repeat, blend, clip, clip_rect);
					int n = mm->visible_count();
					for (int k = 0; k < n; k++) {
						_add_mesh(mm->mesh, xform * mm->get_transform_2d(k), mm->get_color(k) * item_modulate * tex_mod, tex);
					}
				} break;

				default: {
					if (!warned_mesh) {
						WARN_PRINT("3DS: GPUParticles2D need compute shaders; use CPUParticles2D.");
						warned_mesh = true;
					}
				} break;
			}
		}
	}
	_emit_pending();
	_flush();

	if (batches.is_empty()) {
		return;
	}
	// One cache flush for everything this call streamed.
	uint32_t lo = batches[0].first;
	uint32_t hi = batches[batches.size() - 1].first + batches[batches.size() - 1].count;
	RasterizerCitro3D *r = RasterizerCitro3D::get_singleton();
	CanvasVertex *base = r->get_stream_vertices() + lo;
	GSPGPU_FlushDataCache(base, sizeof(CanvasVertex) * (hi - lo));

	int eyes = r->get_eye_count(rt);
	static int trace = 0;
	if (trace++ < 4) {
		print_verbose(vformat("C3D trace: canvas rt=%dx%d batches=%d", rt->size.x, rt->size.y, batches.size()));
	}
	for (int eye = 0; eye < eyes; eye++) {
		if (ts->render_target_begin(rt, eye)) {
			_draw_batches(rt, eye, eyes);
		}
	}
}
