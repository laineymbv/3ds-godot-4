/**************************************************************************/
/*  rasterizer_scene_citro3d.cpp                                          */
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

#include "rasterizer_scene_citro3d.h"

#include "light_storage_citro3d.h"
#include "rasterizer_citro3d.h"
#include "texture_storage_citro3d.h"
#include "utilities_citro3d.h"

#include "core/config/project_settings.h"

using namespace Citro3D;

static void _transform_to_mtx(const Transform3D &p_xform, C3D_Mtx *r_mtx) {
	for (int r = 0; r < 3; r++) {
		r_mtx->r[r].x = p_xform.basis.rows[r][0];
		r_mtx->r[r].y = p_xform.basis.rows[r][1];
		r_mtx->r[r].z = p_xform.basis.rows[r][2];
		r_mtx->r[r].w = p_xform.origin[r];
	}
	r_mtx->r[3].x = 0;
	r_mtx->r[3].y = 0;
	r_mtx->r[3].z = 0;
	r_mtx->r[3].w = 1;
}

// PICA light colors and materials are stored blue first.
static void _bgr(float *r_out, const Color &p_c) {
	r_out[0] = p_c.b;
	r_out[1] = p_c.g;
	r_out[2] = p_c.r;
}

// Godot's omni/spot falloff: d^-attenuation * (1 - (d/range)^4)^2, clamped to
// the LUT's [0, 1] range.
static float _godot_distance_attenuation(float p_dist, float p_attenuation, float p_range) {
	float nd = p_dist / MAX(p_range, 0.0001f);
	nd = CLAMP(1.0f - nd * nd * nd * nd, 0.0f, 1.0f);
	float a = Math::pow(MAX(p_dist, 0.0001f), -p_attenuation) * nd * nd;
	return CLAMP(a, 0.0f, 1.0f);
}

static float _spot_soft_step(float p_cos, float p_cutoff) {
	float edge = p_cutoff + (1.0f - p_cutoff) * 0.15f;
	if (p_cos <= p_cutoff) {
		return 0.0f;
	}
	if (p_cos >= edge) {
		return 1.0f;
	}
	float t = (p_cos - p_cutoff) / (edge - p_cutoff);
	return t * t * (3.0f - 2.0f * t);
}

void RasterizerSceneCitro3D::initialize() {
	// Blinn-Phong highlight sizes from rough to glossy.
	static const float shininess[SPECULAR_BUCKETS] = { 2, 6, 14, 30, 64, 128 };
	for (int i = 0; i < SPECULAR_BUCKETS; i++) {
		LightLut_Phong(&specular_luts[i], shininess[i]);
	}
	GLOBAL_DEF("rendering/3ds/stereo/max_separation", 0.12);
	GLOBAL_DEF(PropertyInfo(Variant::INT, "rendering/3ds/shadows/size", PROPERTY_HINT_ENUM, "256,512,1024"), 512);
	GLOBAL_DEF("rendering/3ds/shadows/max_distance", 20.0);
	GLOBAL_DEF("rendering/3ds/shadows/bias", 0.012);
	GLOBAL_DEF("rendering/3ds/stereo/convergence", 3.0);
}

void RasterizerSceneCitro3D::finalize() {
	if (sun_tex_ready) {
		C3D_TexDelete(&sun_tex);
		linearFree(sun_vertices);
		sun_tex_ready = false;
	}
	if (shadow_target) {
		C3D_RenderTargetDelete(shadow_target);
		C3D_TexDelete(&shadow_tex);
		shadow_target = nullptr;
	}
	if (sky_vertices) {
		linearFree(sky_vertices);
		sky_vertices = nullptr;
	}
	if (sky_indices) {
		linearFree(sky_indices);
		sky_indices = nullptr;
	}
}

RID RasterizerSceneCitro3D::sky_allocate() {
	return sky_owner.allocate_rid();
}

void RasterizerSceneCitro3D::sky_initialize(RID p_rid) {
	sky_owner.initialize_rid(p_rid, Sky());
}

void RasterizerSceneCitro3D::sky_set_material(RID p_sky, RID p_material) {
	Sky *sky = sky_owner.get_or_null(p_sky);
	ERR_FAIL_NULL(sky);
	sky->material = p_material;
	sky_colors_valid = false;
}

bool RasterizerSceneCitro3D::free(RID p_rid) {
	if (sky_owner.owns(p_rid)) {
		sky_owner.free(p_rid);
		return true;
	}
	return RasterizerSceneDummy::free(p_rid);
}

static Color _sky_param_color(const Material *p_m, const char *p_name, const Color &p_default) {
	const Variant *v = p_m->params.getptr(StringName(p_name));
	return (v && v->get_type() == Variant::COLOR) ? Color(*v) : p_default;
}

static float _sky_param_float(const Material *p_m, const char *p_name, float p_default) {
	const Variant *v = p_m->params.getptr(StringName(p_name));
	return (v && (v->get_type() == Variant::FLOAT || v->get_type() == Variant::INT)) ? float(*v) : p_default;
}

void RasterizerSceneCitro3D::_draw_sun_disks(const PagedArray<RID> &p_lights, const Transform3D &p_view, float p_radius, float p_angle_max, float p_exposure) {
	if (!sun_tex_ready) {
		// Soft disc: a bright core and a glow falling off to the edge.
		const int S = 64;
		if (!C3D_TexInit(&sun_tex, S, S, GPU_RGBA8)) {
			return;
		}
		uint8_t *d = (uint8_t *)sun_tex.data;
		for (int y = 0; y < S; y++) {
			for (int x = 0; x < S; x++) {
				float dx = (x + 0.5f) / S * 2.0f - 1.0f, dy = (y + 0.5f) / S * 2.0f - 1.0f;
				float r = Math::sqrt(dx * dx + dy * dy);
				float core = CLAMP((0.09f - r) / 0.02f, 0.0f, 1.0f);
				float glow = Math::pow(MAX(1.0f - r, 0.0f), 4.0f) * 0.55f;
				uint8_t a = (uint8_t)(CLAMP(core + glow, 0.0f, 1.0f) * 255.0f);
				uint8_t *p = d + image_offset(x, y, S, S) * 4;
				p[0] = a;
				p[1] = p[2] = p[3] = 255;
			}
		}
		C3D_TexFlush(&sun_tex);
		C3D_TexSetFilter(&sun_tex, GPU_LINEAR, GPU_LINEAR);
		C3D_TexSetWrap(&sun_tex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
		sun_vertices = (Vertex *)linearAlloc(sizeof(Vertex) * 16);
		sun_tex_ready = sun_vertices != nullptr;
		if (!sun_tex_ready) {
			return;
		}
	}
	LightStorage *ls = LightStorage::get_singleton();
	RasterizerCitro3D *r = RasterizerCitro3D::get_singleton();
	int count = 0;
	float half = p_radius * Math::tan(CLAMP(p_angle_max, 0.05f, 1.2f));
	for (uint32_t i = 0; i < p_lights.size() && count < 4; i++) {
		LightInstance *li = ls->get_light_instance(p_lights[i]);
		Light *l = li ? ls->get_light(li->light) : nullptr;
		if (!l || l->type != RSE::LIGHT_DIRECTIONAL || l->param[RSE::LIGHT_PARAM_ENERGY] <= 0.0f) {
			continue;
		}
		Vector3 dir = li->transform.basis.get_column(2).normalized(); // Towards the light.
		Vector3 up = Math::abs(dir.y) > 0.95f ? Vector3(1, 0, 0) : Vector3(0, 1, 0);
		Vector3 right = up.cross(dir).normalized() * half;
		Vector3 top = dir.cross(right).normalized() * half;
		Vector3 c = dir * p_radius * 0.98f;
		Color col = l->color * l->param[RSE::LIGHT_PARAM_ENERGY] * p_exposure;
		Vector3 corners[4] = { c - right + top, c - right - top, c + right + top, c + right - top };
		const float uvs[4][2] = { { 0, 0 }, { 0, 1 }, { 1, 0 }, { 1, 1 } };
		for (int k = 0; k < 4; k++) {
			Vertex &v = sun_vertices[count * 4 + k];
			v.position[0] = corners[k].x;
			v.position[1] = corners[k].y;
			v.position[2] = corners[k].z;
			v.normal[0] = 0;
			v.normal[1] = 1;
			v.normal[2] = 0;
			v.uv[0] = uvs[k][0];
			v.uv[1] = uvs[k][1];
			v.color[0] = (uint8_t)CLAMP(col.r * 255.0f, 0.0f, 255.0f);
			v.color[1] = (uint8_t)CLAMP(col.g * 255.0f, 0.0f, 255.0f);
			v.color[2] = (uint8_t)CLAMP(col.b * 255.0f, 0.0f, 255.0f);
			v.color[3] = 255;
		}
		count++;
	}
	if (count == 0) {
		return;
	}
	GSPGPU_FlushDataCache(sun_vertices, sizeof(Vertex) * 16);
	C3D_Mtx mtx;
	_transform_to_mtx(Transform3D(p_view.basis, Vector3()), &mtx);
	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, r->scene_uniforms.model_view, &mtx);
	C3D_FVUnifSet(GPU_VERTEX_SHADER, r->scene_uniforms.uv_transform, 1, 1, 0, 0);
	C3D_TexBind(1, &sun_tex);
	C3D_TexEnv *e0 = C3D_GetTexEnv(0);
	C3D_TexEnvInit(e0);
	C3D_TexEnvSrc(e0, C3D_Both, GPU_TEXTURE1, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
	C3D_TexEnvFunc(e0, C3D_Both, GPU_MODULATE);
	C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE, GPU_ZERO, GPU_ONE);
	C3D_BufInfo *bi = C3D_GetBufInfo();
	BufInfo_Init(bi);
	BufInfo_Add(bi, sun_vertices, sizeof(Vertex), 4, 0x3210);
	for (int i = 0; i < count; i++) {
		C3D_DrawArrays(GPU_TRIANGLE_STRIP, i * 4, 4);
	}
	C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
}

bool RasterizerSceneCitro3D::_draw_sky(RID p_environment, const Transform3D &p_view, const Projection &p_proj, const PagedArray<RID> &p_lights, Color &r_ambient) {
	Sky *sky = sky_owner.get_or_null(environment_get_sky(p_environment));
	Material *m = sky ? MaterialStorage::get_singleton()->get_material(sky->material) : nullptr;
	if (!m) {
		return false;
	}
	RasterizerCitro3D *r = RasterizerCitro3D::get_singleton();
	TextureStorage *ts = TextureStorage::get_singleton();

	if (!sky_vertices) {
		int vc = (SKY_RINGS + 1) * (SKY_SEGMENTS + 1);
		sky_vertices = (Vertex *)linearAlloc(sizeof(Vertex) * vc);
		sky_index_count = SKY_RINGS * SKY_SEGMENTS * 6;
		sky_indices = (uint16_t *)linearAlloc(sizeof(uint16_t) * sky_index_count);
		ERR_FAIL_COND_V(!sky_vertices || !sky_indices, false);
		for (int ring = 0; ring <= SKY_RINGS; ring++) {
			float lat = Math::PI * (0.5f - (float)ring / SKY_RINGS); // +90 .. -90
			for (int seg = 0; seg <= SKY_SEGMENTS; seg++) {
				float lon = Math::TAU * seg / SKY_SEGMENTS;
				Vector3 d(Math::cos(lat) * Math::sin(lon), Math::sin(lat), -Math::cos(lat) * Math::cos(lon));
				Vertex &v = sky_vertices[ring * (SKY_SEGMENTS + 1) + seg];
				v.position[0] = d.x;
				v.position[1] = d.y;
				v.position[2] = d.z;
				v.normal[0] = 0;
				v.normal[1] = 1;
				v.normal[2] = 0;
				// Godot's panorama mapping: u from atan(x, -z), v from acos(y).
				v.uv[0] = (float)seg / SKY_SEGMENTS + 0.5f; // Wraps (GPU_REPEAT).
				v.uv[1] = (float)ring / SKY_RINGS;
			}
		}
		int k = 0;
		for (int ring = 0; ring < SKY_RINGS; ring++) {
			for (int seg = 0; seg < SKY_SEGMENTS; seg++) {
				uint16_t a = ring * (SKY_SEGMENTS + 1) + seg;
				uint16_t b = a + SKY_SEGMENTS + 1;
				sky_indices[k++] = a;
				sky_indices[k++] = b;
				sky_indices[k++] = a + 1;
				sky_indices[k++] = a + 1;
				sky_indices[k++] = b;
				sky_indices[k++] = b + 1;
			}
		}
		GSPGPU_FlushDataCache(sky_indices, sizeof(uint16_t) * sky_index_count);
		sky_colors_valid = false;
	}

	// Colors: ProceduralSkyMaterial's gradient, or PhysicalSkyMaterial's
	// rayleigh/ground colors, or white under a panorama.
	Texture *pano = nullptr;
	const Variant *pv = m->params.getptr(StringName("source_panorama"));
	if (pv && pv->get_type() == Variant::RID) {
		pano = ts->resolve_texture(*pv);
	}
	float exposure = _sky_param_float(m, "exposure", 1.0f);
	Color top, horizon, ground_h, ground_b;
	float inv_sky_curve = 0.6f / 0.15f, inv_ground_curve = 0.6f / 0.02f;
	if (pano) {
		top = horizon = ground_h = ground_b = Color(exposure, exposure, exposure);
	} else if (m->params.has(StringName("sky_top_color"))) {
		top = _sky_param_color(m, "sky_top_color", Color(0.385, 0.454, 0.55)) * exposure;
		horizon = _sky_param_color(m, "sky_horizon_color", Color(0.646, 0.656, 0.67)) * exposure;
		ground_b = _sky_param_color(m, "ground_bottom_color", Color(0.2, 0.169, 0.133)) * exposure;
		ground_h = _sky_param_color(m, "ground_horizon_color", Color(0.646, 0.656, 0.67)) * exposure;
		inv_sky_curve = _sky_param_float(m, "inv_sky_curve", inv_sky_curve);
		inv_ground_curve = _sky_param_float(m, "inv_ground_curve", inv_ground_curve);
	} else if (m->params.has(StringName("rayleigh_color"))) {
		Color ray = _sky_param_color(m, "rayleigh_color", Color(0.3, 0.405, 0.6));
		top = ray * exposure;
		horizon = ray.lerp(Color(1, 1, 1), 0.55f) * exposure;
		ground_b = _sky_param_color(m, "ground_color", Color(0.1, 0.07, 0.034)) * exposure;
		ground_h = ground_b.lerp(horizon, 0.5f);
	} else {
		return false; // Custom sky shader: background color instead.
	}
	Color key[4] = { top, horizon, ground_h, ground_b };
	bool changed = !sky_colors_valid;
	for (int i = 0; i < 4 && !changed; i++) {
		changed = key[i] != sky_colors_cache[i];
	}
	if (changed) {
		for (int i = 0; i < 4; i++) {
			sky_colors_cache[i] = key[i];
		}
		sky_colors_valid = true;
		int vc = (SKY_RINGS + 1) * (SKY_SEGMENTS + 1);
		for (int i = 0; i < vc; i++) {
			Vertex &v = sky_vertices[i];
			float y = v.position[1];
			Color c;
			if (y >= 0.0f) {
				c = top.lerp(horizon, CLAMP(Math::pow(1.0f - y, inv_sky_curve), 0.0f, 1.0f));
			} else {
				c = ground_b.lerp(ground_h, CLAMP(Math::pow(1.0f + y, inv_ground_curve), 0.0f, 1.0f));
			}
			v.color[0] = (uint8_t)CLAMP(c.r * 255.0f, 0.0f, 255.0f);
			v.color[1] = (uint8_t)CLAMP(c.g * 255.0f, 0.0f, 255.0f);
			v.color[2] = (uint8_t)CLAMP(c.b * 255.0f, 0.0f, 255.0f);
			v.color[3] = 255;
		}
		GSPGPU_FlushDataCache(sky_vertices, sizeof(Vertex) * vc);
		sky_ambient = (top + horizon * 2.0f + ground_h) * 0.25f;
	}
	r_ambient = sky_ambient;

	// Rotation only (the sky follows the camera), sized between near and far.
	float radius = Math::sqrt(MAX(p_proj.get_z_near(), 0.01f) * p_proj.get_z_far());
	Transform3D mv(p_view.basis * environment_get_sky_orientation(p_environment), Vector3());
	mv.basis.scale(Vector3(radius, radius, radius));
	C3D_Mtx mtx;
	_transform_to_mtx(mv, &mtx);

	r->bind_scene_program();
	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, r->scene_uniforms.model_view, &mtx);
	C3D_FVUnifSet(GPU_VERTEX_SHADER, r->scene_uniforms.color_scale, 1, 1, 1, 1);
	C3D_FVUnifSet(GPU_VERTEX_SHADER, r->scene_uniforms.color_bias, 0, 0, 0, 0);
	Vector2 uvs = pano ? pano->uv_scale() : Vector2(1, 1);
	C3D_FVUnifSet(GPU_VERTEX_SHADER, r->scene_uniforms.uv_transform, uvs.x, uvs.y, 0, 0);

	C3D_LightEnvBind(nullptr);
	C3D_TexEnv *e0 = C3D_GetTexEnv(0);
	C3D_TexEnvInit(e0);
	if (pano) {
		C3D_TexSetFilter(&pano->tex, GPU_LINEAR, GPU_LINEAR);
		C3D_TexSetWrap(&pano->tex, GPU_REPEAT, GPU_CLAMP_TO_EDGE);
		C3D_TexBind(1, &pano->tex);
		C3D_TexEnvSrc(e0, C3D_Both, GPU_TEXTURE1, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
		C3D_TexEnvFunc(e0, C3D_Both, GPU_MODULATE);
	} else {
		C3D_TexEnvSrc(e0, C3D_Both, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
		C3D_TexEnvFunc(e0, C3D_Both, GPU_REPLACE);
	}
	for (int i = 1; i < 6; i++) {
		C3D_TexEnvInit(C3D_GetTexEnv(i));
	}
	C3D_CullFace(GPU_CULL_NONE);
	C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR);
	C3D_AlphaTest(false, GPU_ALWAYS, 0);
	C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);

	C3D_BufInfo *bi = C3D_GetBufInfo();
	BufInfo_Init(bi);
	BufInfo_Add(bi, sky_vertices, sizeof(Vertex), 4, 0x3210);
	C3D_DrawElements(GPU_TRIANGLES, sky_index_count, C3D_UNSIGNED_SHORT, sky_indices);
	if (!pano) {
		// Sun discs for directional lights (procedural and physical skies).
		float angle_max = Math::acos(CLAMP(_sky_param_float(m, "sun_angle_max", Math::cos(Math::deg_to_rad(30.0f))), -1.0f, 1.0f));
		_draw_sun_disks(p_lights, p_view, radius, angle_max * 0.35f, exposure);
	}
	return true;
}

Ref<RenderSceneBuffers> RasterizerSceneCitro3D::render_buffers_create() {
	Ref<RenderSceneBuffersCitro3D> rb;
	rb.instantiate();
	return rb;
}

RenderGeometryInstance *RasterizerSceneCitro3D::geometry_instance_create(RID p_base) {
	RSE::InstanceType type = Utilities::get_singleton()->get_base_type(p_base);
	ERR_FAIL_COND_V(!((1 << type) & RSE::INSTANCE_GEOMETRY_MASK), nullptr);
	GeometryInstanceCitro3D *gi = geometry_instance_alloc.alloc();
	gi->data = memnew(GeometryInstanceCitro3D::Data);
	gi->data->base = p_base;
	gi->data->base_type = type;
	return gi;
}

void RasterizerSceneCitro3D::geometry_instance_free(RenderGeometryInstance *p_geometry_instance) {
	GeometryInstanceCitro3D *gi = static_cast<GeometryInstanceCitro3D *>(p_geometry_instance);
	ERR_FAIL_NULL(gi);
	memdelete(gi->data);
	gi->data = nullptr;
	geometry_instance_alloc.free(gi);
}

void RasterizerSceneCitro3D::_add_mesh(GeometryInstanceCitro3D *p_gi, RID p_mesh, const Transform3D &p_model, const Color &p_color, float p_depth) {
	MeshStorage *ms = MeshStorage::get_singleton();
	MaterialStorage *mat = MaterialStorage::get_singleton();
	Mesh *mesh = ms->get_mesh(p_mesh);
	if (!mesh) {
		return;
	}
	MeshInstance *mi = ms->get_mesh_instance(p_gi->mesh_instance);
	for (int s = 0; s < mesh->surfaces.size(); s++) {
		const Surface &surf = mesh->surfaces[s];
		if (!surf.vertices || surf.primitive != RSE::PRIMITIVE_TRIANGLES) {
			continue;
		}
		RID material = p_gi->data->material_override;
		if (material.is_null() && s < p_gi->data->surface_materials.size()) {
			material = p_gi->data->surface_materials[s];
		}
		if (material.is_null()) {
			material = surf.material;
		}
		DrawItem item;
		item.instance = p_gi;
		item.surface = &surf;
		item.vertices = (mi && s < mi->vertices.size() && mi->vertices[s]) ? mi->vertices[s] : surf.vertices;
		item.model = p_model;
		item.depth = p_depth;
		mat->get_state(material, item.material);
		// Per-instance COLOR only shows where the material uses vertex color.
		if (p_color != Color(1, 1, 1, 1) && item.material.shader && item.material.shader->info.uses_vertex_color) {
			item.material.albedo *= p_color;
			if (p_color.a < 1.0f) {
				item.material.transparent = true;
			}
		}
		if (p_gi->force_alpha < 1.0 || p_gi->parent_fade_alpha < 1.0) {
			item.material.albedo.a *= p_gi->force_alpha * p_gi->parent_fade_alpha;
			item.material.transparent = true;
		}
		if (item.material.transparent) {
			transparent_list.push_back(item);
		} else {
			opaque_list.push_back(item);
		}
	}
}

void RasterizerSceneCitro3D::_fill_draw_lists(const CameraData *p_camera_data, const PagedArray<RenderGeometryInstance *> &p_instances) {
	opaque_list.clear();
	transparent_list.clear();
	MeshStorage *ms = MeshStorage::get_singleton();
	Vector3 cam_pos = p_camera_data->main_transform.origin;
	Vector3 cam_fwd = -p_camera_data->main_transform.basis.get_column(2);

	static bool warned_particles = false;
	for (uint32_t i = 0; i < p_instances.size(); i++) {
		GeometryInstanceCitro3D *gi = static_cast<GeometryInstanceCitro3D *>(p_instances[i]);
		if (!(gi->layer_mask & p_camera_data->visible_layers)) {
			continue;
		}
		if (gi->data->base_type == RSE::INSTANCE_MESH) {
			float depth = cam_fwd.dot(gi->transformed_aabb.get_center() - cam_pos);
			_add_mesh(gi, gi->data->base, gi->transform, Color(1, 1, 1, 1), depth);
		} else if (gi->data->base_type == RSE::INSTANCE_MULTIMESH) {
			MultiMesh *mm = ms->get_multimesh(gi->data->base);
			if (!mm || mm->xform_format != RSE::MULTIMESH_TRANSFORM_3D) {
				continue;
			}
			int n = mm->visible_count();
			for (int k = 0; k < n; k++) {
				Transform3D model = gi->transform * mm->get_transform(k);
				float depth = cam_fwd.dot(model.origin - cam_pos);
				_add_mesh(gi, mm->mesh, model, mm->get_color(k), depth);
			}
		} else if (!warned_particles) {
			WARN_PRINT("3DS: GPUParticles need compute shaders; use CPUParticles3D/2D (Convert to CPUParticles in the editor).");
			warned_particles = true;
		}
	}

	struct FrontToBack {
		bool operator()(const DrawItem &a, const DrawItem &b) const { return a.depth < b.depth; }
	};
	struct BackToFront {
		bool operator()(const DrawItem &a, const DrawItem &b) const { return a.depth > b.depth; }
	};
	opaque_list.sort_custom<FrontToBack>();
	transparent_list.sort_custom<BackToFront>();
}

void RasterizerSceneCitro3D::_setup_lights(const PagedArray<RID> &p_lights, const Transform3D &p_view, const Vector3 &p_cam_pos, const Color &p_ambient) {
	LightStorage *ls = LightStorage::get_singleton();

	// Directional lights first, then the nearest positional ones.
	struct Candidate {
		LightInstance *li;
		Light *light;
		float score;
	};
	LocalVector<Candidate> candidates;
	for (uint32_t i = 0; i < p_lights.size(); i++) {
		LightInstance *li = ls->get_light_instance(p_lights[i]);
		Light *l = li ? ls->get_light(li->light) : nullptr;
		if (!l) {
			continue;
		}
		float score = l->type == RSE::LIGHT_DIRECTIONAL ? -1.0f : li->transform.origin.distance_to(p_cam_pos);
		candidates.push_back({ li, l, score });
	}
	struct ByScore {
		bool operator()(const Candidate &a, const Candidate &b) const { return a.score < b.score; }
	};
	candidates.sort_custom<ByScore>();

	C3D_LightEnvInit(&light_env);
	C3D_LightEnvBind(&light_env);
	C3D_LightEnvAmbient(&light_env, p_ambient.r, p_ambient.g, p_ambient.b);
	C3D_LightEnvLut(&light_env, GPU_LUT_D0, GPU_LUTINPUT_NH, false, &specular_luts[0]);
	// Spot lights read their cone table by the spot angle (signed), not by
	// the default N.H input; the tables themselves are per light.
	C3D_LightEnvLut(&light_env, GPU_LUT_SP, GPU_LUTINPUT_SP, true, nullptr);
	bound_specular = 0;
	if (shadow_active) {
		C3D_LightEnvShadowMode(&light_env, GPU_SHADOW_PRIMARY | GPU_SHADOW_SECONDARY);
		C3D_LightEnvShadowSel(&light_env, 0);
	}

	int count = MIN((int)candidates.size(), MAX_LIGHTS);
	for (int i = 0; i < count; i++) {
		const Candidate &c = candidates[i];
		C3D_Light *pl = &lights[i];
		C3D_LightInit(pl, &light_env);
		float energy = c.light->param[RSE::LIGHT_PARAM_ENERGY] * (c.light->negative ? -1.0f : 1.0f);
		Color col = c.light->color * MAX(energy, 0.0f);
		C3D_LightDiffuse(pl, col.r, col.g, col.b);
		float spec = c.light->param[RSE::LIGHT_PARAM_SPECULAR];
		C3D_LightSpecular0(pl, col.r * spec, col.g * spec, col.b * spec);
		C3D_LightSpecular1(pl, 0, 0, 0);

		bool shadowed = shadow_active && c.li == LightStorage::get_singleton()->get_light_instance(shadow_light_instance);
		C3D_LightShadowEnable(pl, shadowed);

		const Transform3D &lx = c.li->transform;
		if (c.light->type == RSE::LIGHT_DIRECTIONAL) {
			// w = 0: the position is the direction towards the light.
			Vector3 to_light = p_view.basis.xform(lx.basis.get_column(2)).normalized();
			C3D_FVec pos = FVec4_New(to_light.x, to_light.y, to_light.z, 0.0f);
			C3D_LightPosition(pl, &pos);
			C3D_LightDistAttnEnable(pl, false);
			continue;
		}

		Vector3 vp = p_view.xform(lx.origin);
		C3D_FVec pos = FVec4_New(vp.x, vp.y, vp.z, 1.0f);
		C3D_LightPosition(pl, &pos);

		float range = c.light->param[RSE::LIGHT_PARAM_RANGE];
		float attenuation = c.light->param[RSE::LIGHT_PARAM_ATTENUATION];
		LutKey &key = light_lut_keys[i];
		if (key.range != range || key.attenuation != attenuation) {
			LightLutDA_Create(&light_da[i], _godot_distance_attenuation, 0.0f, range, attenuation, range);
			key.range = range;
			key.attenuation = attenuation;
		}
		C3D_LightDistAttnEnable(pl, true);
		C3D_LightDistAttn(pl, &light_da[i]);

		if (c.light->type == RSE::LIGHT_SPOT) {
			float angle = Math::deg_to_rad(c.light->param[RSE::LIGHT_PARAM_SPOT_ANGLE]);
			if (key.spot_angle != angle) {
				LightLut_FromFunc(&light_spot[i], _spot_soft_step, Math::cos(angle), true);
				key.spot_angle = angle;
			}
			Vector3 dir = p_view.basis.xform(-lx.basis.get_column(2)).normalized(); // Beam direction.
			C3D_LightSpotEnable(pl, true);
			C3D_LightSpotDir(pl, dir.x, dir.y, dir.z);
			C3D_LightSpotLut(pl, &light_spot[i]);
		}
	}
}

void RasterizerSceneCitro3D::_setup_fog(RID p_environment, const Projection &p_proj) {
	if (!is_environment(p_environment) || !environment_get_fog_enabled(p_environment)) {
		C3D_FogGasMode(GPU_NO_FOG, GPU_PLAIN_DENSITY, false);
		return;
	}
	// The PICA200 fog unit looks up visibility (1 = clear) by depth; build
	// the table from Godot's fog curve, in view-space distance.
	FogKey key;
	key.mode = (int)environment_get_fog_mode(p_environment);
	key.near = p_proj.get_z_near();
	key.far = p_proj.get_z_far();
	if (key.mode == RSE::ENV_FOG_MODE_DEPTH) {
		key.a = environment_get_fog_depth_begin(p_environment);
		key.b = environment_get_fog_depth_end(p_environment);
		key.c = environment_get_fog_depth_curve(p_environment);
	} else {
		key.a = environment_get_fog_density(p_environment);
	}
	if (!(key == fog_key)) {
		fog_key = key;
		float data[256];
		for (int i = 0; i <= 128; i++) {
			float z = FogLut_CalcZ(i / 128.0f, key.near, key.far);
			float fog;
			if (key.mode == RSE::ENV_FOG_MODE_DEPTH) {
				float end = key.b > key.a ? key.b : key.a + 0.001f;
				float t = CLAMP((z - key.a) / (end - key.a), 0.0f, 1.0f);
				fog = Math::pow(t, MAX(key.c, 0.0001f));
			} else {
				fog = 1.0f - Math::exp(-key.a * z);
			}
			float vis = CLAMP(1.0f - fog, 0.0f, 1.0f);
			if (i < 128) {
				data[i] = vis;
			}
			if (i > 0) {
				data[i + 127] = vis - data[i - 1];
			}
		}
		FogLut_FromArray(&fog_lut, data);
	}
	Color c = environment_get_fog_light_color(p_environment) * environment_get_fog_light_energy(p_environment);
	C3D_FogGasMode(GPU_FOG, GPU_PLAIN_DENSITY, false);
	C3D_FogColor(color_to_abgr(c) & 0xFFFFFF);
	C3D_FogLutBind(&fog_lut);
}

void RasterizerSceneCitro3D::_apply_material(const DrawItem &p_item, bool p_transparent) {
	const MaterialState &m = p_item.material;
	const Shader::Info *info = m.shader ? &m.shader->info : nullptr;
	RasterizerCitro3D *r = RasterizerCitro3D::get_singleton();
	TextureStorage *ts = TextureStorage::get_singleton();

	bool unshaded = info && info->unshaded;
	bool use_vertex_color = (info && info->uses_vertex_color) || (!info && p_item.surface->has_color);

	// Texture and UV transform.
	Texture *tex = ts->resolve_texture(m.albedo_texture);
	Vector2 uv_scale = m.uv_scale;
	Vector2 uv_offset = m.uv_offset + m.uv_scroll * (float)Math::fmod(time, 1000.0);
	if (tex) {
		Vector2 ts_scale = tex->uv_scale();
		uv_scale *= ts_scale;
		uv_offset *= ts_scale;
		GPU_TEXTURE_FILTER_PARAM f = (info && info->texture_nearest) ? GPU_NEAREST : GPU_LINEAR;
		GPU_TEXTURE_WRAP_PARAM w = (info && info->texture_repeat) ? GPU_REPEAT : GPU_CLAMP_TO_EDGE;
		C3D_TexSetFilter(&tex->tex, f, f);
		C3D_TexSetWrap(&tex->tex, w, w);
		C3D_TexBind(1, &tex->tex); // Unit 0 is reserved for the shadow map.
		ts->mark_used(tex);
	}
	C3D_FVUnifSet(GPU_VERTEX_SHADER, r->scene_uniforms.uv_transform, uv_scale.x, uv_scale.y, uv_offset.x, uv_offset.y);
	if (use_vertex_color) {
		C3D_FVUnifSet(GPU_VERTEX_SHADER, r->scene_uniforms.color_scale, 1, 1, 1, 1);
		C3D_FVUnifSet(GPU_VERTEX_SHADER, r->scene_uniforms.color_bias, 0, 0, 0, 0);
	} else {
		C3D_FVUnifSet(GPU_VERTEX_SHADER, r->scene_uniforms.color_scale, 0, 0, 0, 0);
		C3D_FVUnifSet(GPU_VERTEX_SHADER, r->scene_uniforms.color_bias, 1, 1, 1, 1);
	}

	// TEV: stage 0 = base color (lit or unlit) x texture, stage 1 = x vertex
	// color, stage 2 = + specular.
	C3D_TexEnv *e0 = C3D_GetTexEnv(0);
	C3D_TexEnvInit(e0);
	C3D_TexEnvColor(e0, color_to_abgr(m.albedo));
	GPU_TEVSRC base = unshaded ? GPU_CONSTANT : GPU_FRAGMENT_PRIMARY_COLOR;
	if (tex) {
		C3D_TexEnvSrc(e0, C3D_RGB, base, GPU_TEXTURE1, GPU_PRIMARY_COLOR);
		C3D_TexEnvSrc(e0, C3D_Alpha, GPU_CONSTANT, GPU_TEXTURE1, GPU_PRIMARY_COLOR);
		C3D_TexEnvFunc(e0, C3D_Both, GPU_MODULATE);
	} else {
		C3D_TexEnvSrc(e0, C3D_RGB, base, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
		C3D_TexEnvSrc(e0, C3D_Alpha, GPU_CONSTANT, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
		C3D_TexEnvFunc(e0, C3D_Both, GPU_REPLACE);
	}
	C3D_TexEnv *e1 = C3D_GetTexEnv(1);
	C3D_TexEnvInit(e1);
	C3D_TexEnvSrc(e1, C3D_Both, GPU_PREVIOUS, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
	C3D_TexEnvFunc(e1, C3D_Both, GPU_MODULATE);
	C3D_TexEnv *e2 = C3D_GetTexEnv(2);
	C3D_TexEnvInit(e2);
	if (unshaded) {
		C3D_TexEnvSrc(e2, C3D_Both, GPU_PREVIOUS, GPU_PREVIOUS, GPU_PREVIOUS);
		C3D_TexEnvFunc(e2, C3D_Both, GPU_REPLACE);
	} else {
		C3D_TexEnvSrc(e2, C3D_RGB, GPU_PREVIOUS, GPU_FRAGMENT_SECONDARY_COLOR, GPU_PRIMARY_COLOR);
		C3D_TexEnvFunc(e2, C3D_RGB, GPU_ADD);
		C3D_TexEnvSrc(e2, C3D_Alpha, GPU_PREVIOUS, GPU_PREVIOUS, GPU_PREVIOUS);
		C3D_TexEnvFunc(e2, C3D_Alpha, GPU_REPLACE);
	}
	for (int i = 3; i < 6; i++) {
		C3D_TexEnvInit(C3D_GetTexEnv(i));
	}

	if (unshaded) {
		C3D_LightEnvBind(nullptr);
	} else {
		C3D_LightEnvBind(&light_env);
		C3D_Material mtl = {};
		Color albedo = m.albedo;
		// Metals tint their highlight and lose diffuse.
		Color diffuse = albedo * (1.0f - m.metallic * 0.8f);
		_bgr(mtl.ambient, diffuse);
		_bgr(mtl.diffuse, diffuse);
		float rough = CLAMP(m.roughness, 0.0f, 1.0f);
		Color spec = Color(0.5f, 0.5f, 0.5f).lerp(albedo, m.metallic) * (m.specular * 2.0f) * (1.0f - rough);
		_bgr(mtl.specular0, spec);
		_bgr(mtl.emission, m.emission);
		C3D_LightEnvMaterial(&light_env, &mtl);
		int bucket = CLAMP((int)((1.0f - rough) * SPECULAR_BUCKETS), 0, SPECULAR_BUCKETS - 1);
		if (bucket != bound_specular) {
			C3D_LightEnvLut(&light_env, GPU_LUT_D0, GPU_LUTINPUT_NH, false, &specular_luts[bucket]);
			bound_specular = bucket;
		}
		for (int i = 0; i < MAX_LIGHTS; i++) {
			C3D_LightTwoSideDiffuse(&lights[i], info && info->cull == RSE::CULL_MODE_DISABLED);
		}
	}

	// Raster state.
	RSE::CullMode cull = info ? info->cull : RSE::CULL_MODE_BACK;
	bool mirror = p_item.model.basis.determinant() < 0.0f;
	if (cull == RSE::CULL_MODE_DISABLED) {
		C3D_CullFace(GPU_CULL_NONE);
	} else {
		// Godot's front faces are clockwise; through the y-flipped projection
		// the PICA200 sees them counter-clockwise.
		bool back = (cull == RSE::CULL_MODE_BACK) != mirror;
		C3D_CullFace(back ? GPU_CULL_BACK_CCW : GPU_CULL_FRONT_CCW);
	}

	bool depth_test = !(info && info->depth_test_disabled);
	bool depth_write = !(info && info->depth_draw_never) && (!p_transparent || (info && info->depth_draw_always));
	C3D_DepthTest(depth_test, GPU_GREATER, depth_write ? GPU_WRITE_ALL : GPU_WRITE_COLOR);

	if (info && info->uses_alpha_scissor) {
		C3D_AlphaTest(true, GPU_GREATER, (int)CLAMP(m.alpha_scissor * 255.0f, 0.0f, 255.0f));
	} else {
		C3D_AlphaTest(false, GPU_ALWAYS, 0);
	}

	if (!p_transparent) {
		C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
	} else {
		Shader::Blend blend = info ? info->blend : Shader::BLEND_MIX;
		switch (blend) {
			case Shader::BLEND_ADD:
				C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE, GPU_ZERO, GPU_ONE);
				break;
			case Shader::BLEND_SUB:
				C3D_AlphaBlend(GPU_BLEND_REVERSE_SUBTRACT, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE, GPU_ZERO, GPU_ONE);
				break;
			case Shader::BLEND_MUL:
				C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_DST_COLOR, GPU_ZERO, GPU_ZERO, GPU_ONE);
				break;
			case Shader::BLEND_PREMUL_ALPHA:
				C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ONE_MINUS_SRC_ALPHA, GPU_ONE, GPU_ONE_MINUS_SRC_ALPHA);
				break;
			default:
				C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA, GPU_ONE, GPU_ONE_MINUS_SRC_ALPHA);
				break;
		}
	}
}

void RasterizerSceneCitro3D::_draw_item_geometry(const DrawItem &p_item, const Transform3D &p_view) {
	RasterizerCitro3D *r = RasterizerCitro3D::get_singleton();
	C3D_Mtx mv;
	_transform_to_mtx(p_view * p_item.model, &mv);
	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, r->scene_uniforms.model_view, &mv);
	C3D_BufInfo *bi = C3D_GetBufInfo();
	BufInfo_Init(bi);
	BufInfo_Add(bi, p_item.vertices, sizeof(Vertex), 4, 0x3210);
	if (p_item.surface->indices) {
		C3D_DrawElements(GPU_TRIANGLES, p_item.surface->index_count, C3D_UNSIGNED_SHORT, p_item.surface->indices);
	} else {
		C3D_DrawArrays(GPU_TRIANGLES, 0, p_item.surface->vertex_count);
	}
}

void RasterizerSceneCitro3D::_draw_list(LocalVector<DrawItem> &p_list, const Transform3D &p_view, bool p_transparent) {
	for (const DrawItem &item : p_list) {
		_apply_material(item, p_transparent);
		_draw_item_geometry(item, p_view);
	}
}

bool RasterizerSceneCitro3D::_render_shadow_map(const PagedArray<RID> &p_lights, const CameraData *p_camera_data) {
	LightStorage *ls = LightStorage::get_singleton();
	// One shadow map: the first shadowed directional light, else the first
	// shadowed spot light (omni shadows would need six passes).
	LightInstance *li = nullptr;
	Light *light = nullptr;
	for (int pass = 0; pass < 2 && !li; pass++) {
		RSE::LightType want = pass == 0 ? RSE::LIGHT_DIRECTIONAL : RSE::LIGHT_SPOT;
		for (uint32_t i = 0; i < p_lights.size(); i++) {
			LightInstance *cand = ls->get_light_instance(p_lights[i]);
			Light *l = cand ? ls->get_light(cand->light) : nullptr;
			if (l && l->shadow && l->type == want) {
				li = cand;
				light = l;
				shadow_light_instance = p_lights[i];
				break;
			}
		}
	}
	if (!li) {
		return false;
	}
	shadow_perspective = light->type == RSE::LIGHT_SPOT;

	int size = CLAMP(Math::next_power_of_2((uint32_t)(int)GLOBAL_GET_CACHED(int, "rendering/3ds/shadows/size")), 128u, 1024u);
	if (size != shadow_size || !shadow_target) {
		if (shadow_target) {
			return false; // Size changes take a restart (no deletes mid-frame).
		}
		if (!C3D_TexInitShadow(&shadow_tex, size, size)) {
			ERR_PRINT_ONCE("3DS: out of VRAM for the shadow map.");
			return false;
		}
		shadow_target = C3D_RenderTargetCreateFromTex(&shadow_tex, GPU_TEXFACE_2D, 0, GPU_RB_DEPTH16);
		if (!shadow_target) {
			C3D_TexDelete(&shadow_tex);
			return false;
		}
		shadow_size = size;
		// Outside the map counts as lit.
		shadow_tex.border = 0xFFFFFFFF;
		C3D_TexSetWrap(&shadow_tex, GPU_CLAMP_TO_BORDER, GPU_CLAMP_TO_BORDER);
		C3D_TexSetFilter(&shadow_tex, GPU_NEAREST, GPU_NEAREST);
	}

	Transform3D light_view;
	Projection proj;
	if (shadow_perspective) {
		// The spot light's own cone.
		float angle = CLAMP(light->param[RSE::LIGHT_PARAM_SPOT_ANGLE], 1.0f, 89.0f);
		float range = MAX(light->param[RSE::LIGHT_PARAM_RANGE], 0.1f);
		light_view = li->transform.orthonormalized().affine_inverse();
		proj.set_perspective(angle * 2.0f, 1.0f, MAX(range * 0.01f, 0.05f), range);
	} else {
		// An orthographic box around the part of the view nearest the camera.
		float max_dist = MIN(light->param[RSE::LIGHT_PARAM_SHADOW_MAX_DISTANCE], (float)GLOBAL_GET_CACHED(float, "rendering/3ds/shadows/max_distance"));
		max_dist = MAX(max_dist, 1.0f);
		const Transform3D &cam = p_camera_data->main_transform;
		Vector3 center = cam.origin - cam.basis.get_column(2) * (max_dist * 0.5f);
		float radius = max_dist * 0.6f;
		Basis lb = li->transform.basis.orthonormalized();
		// Snap the center to shadow texels so edges don't crawl as the camera moves.
		float texel = (radius * 2.0f) / size;
		Vector3 lc = lb.xform_inv(center);
		lc.x = Math::floor(lc.x / texel) * texel;
		lc.y = Math::floor(lc.y / texel) * texel;
		center = lb.xform(lc);
		// Depth is only 8 bits (alpha), so keep the light's depth range tight:
		// just the sphere around the shadowed area.
		light_view = Transform3D(lb, center + lb.get_column(2) * radius * 1.1f).affine_inverse();
		proj.set_orthogonal(-radius, radius, -radius, radius, 0.05f, radius * 2.2f);
	}
	shadow_projview = proj * Projection(light_view);

	RasterizerCitro3D *r = RasterizerCitro3D::get_singleton();
	C3D_FrameSplit(0);
	C3D_RenderTargetClear(shadow_target, C3D_CLEAR_ALL, 0xFFFFFFFF, 0); // Far depth, far alpha.
	C3D_FrameDrawOn(shadow_target);
	C3D_SetViewport(0, 0, size, size);

	// Casters write their light-space depth into alpha (RGB white); a
	// regular depth buffer keeps the nearest one. Same technique as
	// devkitPro's 3ds-examples shadow_mapping.
	r->bind_shadow_program();
	C3D_Mtx pm;
	projection_to_c3d(proj, &pm);
	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, r->shadow_uniforms.projection, &pm);
	C3D_LightEnvBind(nullptr);
	C3D_FogGasMode(GPU_NO_FOG, GPU_PLAIN_DENSITY, false);
	C3D_TexEnv *e0 = C3D_GetTexEnv(0);
	C3D_TexEnvInit(e0);
	C3D_TexEnvColor(e0, 0xFFFFFFFF);
	C3D_TexEnvSrc(e0, C3D_RGB, GPU_CONSTANT, GPU_CONSTANT, GPU_CONSTANT);
	C3D_TexEnvFunc(e0, C3D_RGB, GPU_REPLACE);
	C3D_TexEnvSrc(e0, C3D_Alpha, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
	C3D_TexEnvFunc(e0, C3D_Alpha, GPU_REPLACE);
	for (int i = 1; i < 6; i++) {
		C3D_TexEnvInit(C3D_GetTexEnv(i));
	}
	C3D_CullFace(GPU_CULL_NONE);
	C3D_DepthTest(true, GPU_GREATER, GPU_WRITE_ALL);
	C3D_AlphaTest(false, GPU_ALWAYS, 0);
	C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);

	for (const DrawItem &item : opaque_list) {
		C3D_Mtx mv;
		_transform_to_mtx(light_view * item.model, &mv);
		C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, r->shadow_uniforms.model_view, &mv);
		C3D_BufInfo *bi = C3D_GetBufInfo();
		BufInfo_Init(bi);
		BufInfo_Add(bi, item.vertices, sizeof(Vertex), 4, 0x3210);
		if (item.surface->indices) {
			C3D_DrawElements(GPU_TRIANGLES, item.surface->index_count, C3D_UNSIGNED_SHORT, item.surface->indices);
		} else {
			C3D_DrawArrays(GPU_TRIANGLES, 0, item.surface->vertex_count);
		}
	}
	return true;
}

void RasterizerSceneCitro3D::_set_shadow_matrix(const Transform3D &p_view) {
	RasterizerCitro3D *r = RasterizerCitro3D::get_singleton();
	C3D_Mtx m = {};
	if (shadow_active) {
		// Godot clip space of the light -> (u, v, depth) as the shadow pass
		// stored it: u = x/2 + 1/2, v = 1/2 - y/2 (top row at v = 0),
		// depth = z/2 + 1/2.
		Projection bias;
		bias.columns[0] = Vector4(0.5, 0, 0, 0);
		bias.columns[1] = Vector4(0, -0.5, 0, 0);
		bias.columns[2] = Vector4(0, 0, 0.5, 0);
		bias.columns[3] = Vector4(0.5, 0.5, 0.5, 1);
		Projection full = bias * shadow_projview * Projection(p_view.affine_inverse());
		for (int row = 0; row < 4; row++) {
			m.r[row].x = full.columns[0][row];
			m.r[row].y = full.columns[1][row];
			m.r[row].z = full.columns[2][row];
			m.r[row].w = full.columns[3][row];
		}
	} else {
		m.r[3].w = 1.0f; // Keep the divide finite.
	}
	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, r->scene_uniforms.shadow_mtx, &m);
}

void RasterizerSceneCitro3D::render_scene(const Ref<RenderSceneBuffers> &p_render_buffers, const CameraData *p_camera_data, const CameraData *p_prev_camera_data, const PagedArray<RenderGeometryInstance *> &p_instances, const PagedArray<RID> &p_lights, const PagedArray<RID> &p_reflection_probes, const PagedArray<RID> &p_voxel_gi_instances, const PagedArray<RID> &p_decals, const PagedArray<RID> &p_lightmaps, const PagedArray<RID> &p_fog_volumes, RID p_environment, RID p_camera_attributes, RID p_compositor, RID p_shadow_atlas, RID p_occluder_debug_tex, RID p_reflection_atlas, RID p_reflection_probe, int p_reflection_probe_pass, float p_screen_mesh_lod_threshold, const RenderShadowData *p_render_shadows, int p_render_shadow_count, const RenderSDFGIData *p_render_sdfgi_regions, int p_render_sdfgi_region_count, float p_window_output_max_value, const RenderSDFGIUpdateData *p_sdfgi_update_data, RenderingServerTypes::RenderInfo *r_render_info) {
	RenderSceneBuffersCitro3D *rb = Object::cast_to<RenderSceneBuffersCitro3D>(p_render_buffers.ptr());
	ERR_FAIL_NULL(rb);
	TextureStorage *ts = TextureStorage::get_singleton();
	RenderTarget *rt = ts->get_render_target(rb->render_target);
	if (!rt || !rt->allocated) {
		return;
	}
	RasterizerCitro3D *r = RasterizerCitro3D::get_singleton();

	// Background and ambient light.
	bool clear_color = true;
	Color bg = ts->get_default_clear_color();
	Color ambient(0, 0, 0);
	if (is_environment(p_environment)) {
		RSE::EnvironmentBG mode = environment_get_background(p_environment);
		float bg_energy = environment_get_bg_energy_multiplier(p_environment);
		switch (mode) {
			case RSE::ENV_BG_COLOR:
				bg = environment_get_bg_color(p_environment);
				bg = Color(bg.r * bg_energy, bg.g * bg_energy, bg.b * bg_energy, bg.a);
				break;
			case RSE::ENV_BG_CANVAS:
			case RSE::ENV_BG_KEEP:
			case RSE::ENV_BG_CAMERA_FEED:
				clear_color = false;
				break;
			default:
				break; // Sky: no sky rendering yet, use the clear color.
		}
		RSE::EnvironmentAmbientSource src = environment_get_ambient_source(p_environment);
		float amb_energy = environment_get_ambient_light_energy(p_environment);
		if (src == RSE::ENV_AMBIENT_SOURCE_COLOR) {
			ambient = environment_get_ambient_light(p_environment) * amb_energy;
		} else if (src == RSE::ENV_AMBIENT_SOURCE_BG || src == RSE::ENV_AMBIENT_SOURCE_SKY) {
			ambient = (mode == RSE::ENV_BG_SKY ? environment_get_ambient_light(p_environment) : bg) * amb_energy;
		}
	}
	bool draw_sky = false;
	bool ambient_from_sky = false;
	if (is_environment(p_environment) && environment_get_background(p_environment) == RSE::ENV_BG_SKY && sky_owner.owns(environment_get_sky(p_environment))) {
		draw_sky = true;
		RSE::EnvironmentAmbientSource src = environment_get_ambient_source(p_environment);
		ambient_from_sky = src == RSE::ENV_AMBIENT_SOURCE_BG || src == RSE::ENV_AMBIENT_SOURCE_SKY;
	}
	if (rt->transparent) {
		bg.a = 0;
	}

	_fill_draw_lists(p_camera_data, p_instances);
	static int trace = 0;
	if (trace++ < 3) {
		print_verbose(vformat("C3D trace: env valid=%s bg=%d", is_environment(p_environment) ? "yes" : "no", is_environment(p_environment) ? (int)environment_get_background(p_environment) : -1));
		print_verbose(vformat("C3D trace: render_scene rt=%dx%d stereo=%s opaque=%d transparent=%d lights=%d", rt->size.x, rt->size.y, rt->stereo ? "yes" : "no", opaque_list.size(), transparent_list.size(), p_lights.size()));
	}

	Transform3D view = p_camera_data->main_transform.affine_inverse();
	const Projection &proj = p_camera_data->main_projection;
	int eyes = r->get_eye_count(rt);
	float separation = 0;
	float convergence = 1;
	if (eyes == 2 && !p_camera_data->is_orthogonal) {
		separation = r->get_3d_slider() * r->get_stereo_separation();
		convergence = MAX(r->get_stereo_convergence(), 0.01f);
	}

	shadow_active = _render_shadow_map(p_lights, p_camera_data);
	// Never bind null: citro3d dereferences the texture (a data abort on
	// hardware; Azahar tolerates it). Unused units are simply not sampled.
	if (shadow_active) {
		C3D_TexBind(0, &shadow_tex);
		C3D_TexShadowParams(shadow_perspective, (float)GLOBAL_GET_CACHED(float, "rendering/3ds/shadows/bias"));
	}

	for (int eye = 0; eye < eyes; eye++) {
		if (!ts->render_target_begin(rt, eye)) {
			continue;
		}
		if (clear_color) {
			ts->render_target_clear(rt, eye, bg);
		} else {
			ts->render_target_clear(rt, eye, Color(), true);
		}
		ts->render_target_begin(rt, eye);

		// Each eye sits half the separation to the side; the frustum is sheared
		// so the convergence plane has zero parallax.
		float e = eyes == 2 ? (eye == 0 ? -0.5f : 0.5f) * separation : 0.0f;
		Transform3D eye_view = view;
		eye_view.origin.x -= e;
		Projection eye_proj = proj;
		eye_proj.columns[2][0] += -proj.columns[0][0] * e / convergence;

		C3D_Mtx pm;
		projection_to_c3d(eye_proj, &pm);
		r->bind_scene_program();
		C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, r->scene_uniforms.projection, &pm);

		_setup_fog(p_environment, proj);
		if (draw_sky) {
			Color sky_amb;
			if (_draw_sky(p_environment, eye_view, eye_proj, p_lights, sky_amb) && ambient_from_sky) {
				ambient = sky_amb * environment_get_ambient_light_energy(p_environment);
			}
		}
		_setup_lights(p_lights, eye_view, p_camera_data->main_transform.origin, ambient);
		_set_shadow_matrix(eye_view);
		_draw_list(opaque_list, eye_view, false);
		_draw_list(transparent_list, eye_view, true);
	}

	// The scene cleared the target itself.
	ts->render_target_disable_clear_request(rb->render_target);

	// The canvas draws next with its own buffers and state.
	C3D_FogGasMode(GPU_NO_FOG, GPU_PLAIN_DENSITY, false);
	C3D_LightEnvBind(nullptr);
	C3D_AlphaTest(false, GPU_ALWAYS, 0);
	r->bind_canvas_program();
}
