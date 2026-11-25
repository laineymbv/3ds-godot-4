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
	GLOBAL_DEF("rendering/3ds/stereo/convergence", 3.0);
}

void RasterizerSceneCitro3D::finalize() {
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
	bound_specular = 0;

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
			Vector3 dir = p_view.basis.xform(-lx.basis.get_column(2)).normalized();
			C3D_LightSpotEnable(pl, true);
			C3D_LightSpotDir(pl, dir.x, dir.y, dir.z);
			C3D_LightSpotLut(pl, &light_spot[i]);
		}
	}
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
	Vector2 uv_offset = m.uv_offset;
	if (tex) {
		Vector2 ts_scale = tex->uv_scale();
		uv_scale *= ts_scale;
		uv_offset *= ts_scale;
		GPU_TEXTURE_FILTER_PARAM f = (info && info->texture_nearest) ? GPU_NEAREST : GPU_LINEAR;
		GPU_TEXTURE_WRAP_PARAM w = (info && info->texture_repeat) ? GPU_REPEAT : GPU_CLAMP_TO_EDGE;
		C3D_TexSetFilter(&tex->tex, f, f);
		C3D_TexSetWrap(&tex->tex, w, w);
		C3D_TexBind(0, &tex->tex);
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
		C3D_TexEnvSrc(e0, C3D_RGB, base, GPU_TEXTURE0, GPU_PRIMARY_COLOR);
		C3D_TexEnvSrc(e0, C3D_Alpha, GPU_CONSTANT, GPU_TEXTURE0, GPU_PRIMARY_COLOR);
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

void RasterizerSceneCitro3D::_draw_list(LocalVector<DrawItem> &p_list, const Transform3D &p_view, bool p_transparent) {
	RasterizerCitro3D *r = RasterizerCitro3D::get_singleton();
	for (const DrawItem &item : p_list) {
		_apply_material(item, p_transparent);

		C3D_Mtx mv;
		_transform_to_mtx(p_view * item.model, &mv);
		C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, r->scene_uniforms.model_view, &mv);

		C3D_BufInfo *bi = C3D_GetBufInfo();
		BufInfo_Init(bi);
		BufInfo_Add(bi, item.vertices, sizeof(Vertex), 4, 0x3210);
		if (item.surface->indices) {
			C3D_DrawElements(GPU_TRIANGLES, item.surface->index_count, C3D_UNSIGNED_SHORT, item.surface->indices);
		} else {
			C3D_DrawArrays(GPU_TRIANGLES, 0, item.surface->vertex_count);
		}
	}
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

		_setup_lights(p_lights, eye_view, p_camera_data->main_transform.origin, ambient);
		_draw_list(opaque_list, eye_view, false);
		_draw_list(transparent_list, eye_view, true);
	}

	// The scene cleared the target itself.
	ts->render_target_disable_clear_request(rb->render_target);

	// The canvas draws next with its own buffers and state.
	C3D_LightEnvBind(nullptr);
	C3D_AlphaTest(false, GPU_ALWAYS, 0);
	r->bind_canvas_program();
}
