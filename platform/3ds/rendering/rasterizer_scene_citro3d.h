/**************************************************************************/
/*  rasterizer_scene_citro3d.h                                            */
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
#include "material_storage_citro3d.h"
#include "mesh_storage_citro3d.h"

#include "core/templates/paged_allocator.h"
#include "servers/rendering/dummy/rasterizer_scene_dummy.h"
#include "servers/rendering/renderer_geometry_instance.h"
#include "servers/rendering/storage/render_scene_buffers.h"

class RenderSceneBuffersCitro3D : public RenderSceneBuffers {
	GDCLASS(RenderSceneBuffersCitro3D, RenderSceneBuffers);

public:
	RID render_target;
	Size2i size;

	void configure(const RenderSceneBuffersConfiguration *p_config) override {
		render_target = p_config->get_render_target();
		size = p_config->get_internal_size();
	}
	void set_fsr_sharpness(float p_fsr_sharpness) override {}
	void set_texture_mipmap_bias(float p_texture_mipmap_bias) override {}
	void set_anisotropic_filtering_level(RSE::ViewportAnisotropicFiltering p_anisotropic_filtering_level) override {}
	void set_use_debanding(bool p_use_debanding) override {}
};

class RasterizerSceneCitro3D : public RasterizerSceneDummy {
public:
	class GeometryInstanceCitro3D : public RenderGeometryInstanceBase {
	public:
		void _mark_dirty() override {}
		void set_use_lightmap(RID p_lightmap_instance, const Rect2 &p_lightmap_uv_scale, int p_lightmap_slice_index) override {}
		void set_lightmap_capture(const Color *p_sh9) override {}
		void clear_light_instances() override {}
		void pair_light_instance(const RID p_light_instance, RSE::LightType light_type, uint32_t placement_idx) override {}
		void pair_reflection_probe_instances(const RID *p_reflection_probe_instances, uint32_t p_reflection_probe_instance_count) override {}
		void pair_decal_instances(const RID *p_decal_instances, uint32_t p_decal_instance_count) override {}
		void pair_voxel_gi_instances(const RID *p_voxel_gi_instances, uint32_t p_voxel_gi_instance_count) override {}
		void set_softshadow_projector_pairing(bool p_softshadow, bool p_projector) override {}
	};

private:
	PagedAllocator<GeometryInstanceCitro3D> geometry_instance_alloc;

	struct DrawItem {
		GeometryInstanceCitro3D *instance = nullptr;
		const Citro3D::Surface *surface = nullptr;
		const Citro3D::Vertex *vertices = nullptr; // Skinned copy, or the surface's.
		Transform3D model;
		Citro3D::MaterialState material;
		float depth = 0;
	};
	LocalVector<DrawItem> opaque_list;
	LocalVector<DrawItem> transparent_list;

	static constexpr int MAX_LIGHTS = 8;
	static constexpr int SPECULAR_BUCKETS = 6;
	C3D_LightEnv light_env;
	C3D_Light lights[MAX_LIGHTS];
	C3D_LightLutDA light_da[MAX_LIGHTS];
	C3D_LightLut light_spot[MAX_LIGHTS];
	struct LutKey {
		float range = -1;
		float attenuation = -1;
		float spot_angle = -1;
	} light_lut_keys[MAX_LIGHTS];
	C3D_LightLut specular_luts[SPECULAR_BUCKETS];
	int bound_specular = -1;

	double time = 0;

	void _add_mesh(GeometryInstanceCitro3D *p_gi, RID p_mesh, const Transform3D &p_model, const Color &p_color, float p_depth);
	void _fill_draw_lists(const CameraData *p_camera_data, const PagedArray<RenderGeometryInstance *> &p_instances);
	void _setup_lights(const PagedArray<RID> &p_lights, const Transform3D &p_view, const Vector3 &p_cam_pos, const Color &p_ambient);
	void _draw_list(LocalVector<DrawItem> &p_list, const Transform3D &p_view, bool p_transparent);
	void _apply_material(const DrawItem &p_item, bool p_transparent);

public:
	void initialize();
	void finalize();

	RenderGeometryInstance *geometry_instance_create(RID p_base) override;
	void geometry_instance_free(RenderGeometryInstance *p_geometry_instance) override;
	uint32_t geometry_instance_get_pair_mask() override { return 0; }
	uint32_t get_max_lights_total() override { return MAX_LIGHTS; }
	uint32_t get_max_lights_per_mesh() override { return MAX_LIGHTS; }

	void render_scene(const Ref<RenderSceneBuffers> &p_render_buffers, const CameraData *p_camera_data, const CameraData *p_prev_camera_data, const PagedArray<RenderGeometryInstance *> &p_instances, const PagedArray<RID> &p_lights, const PagedArray<RID> &p_reflection_probes, const PagedArray<RID> &p_voxel_gi_instances, const PagedArray<RID> &p_decals, const PagedArray<RID> &p_lightmaps, const PagedArray<RID> &p_fog_volumes, RID p_environment, RID p_camera_attributes, RID p_compositor, RID p_shadow_atlas, RID p_occluder_debug_tex, RID p_reflection_atlas, RID p_reflection_probe, int p_reflection_probe_pass, float p_screen_mesh_lod_threshold, const RenderShadowData *p_render_shadows, int p_render_shadow_count, const RenderSDFGIData *p_render_sdfgi_regions, int p_render_sdfgi_region_count, float p_window_output_max_value, const RenderSDFGIUpdateData *p_sdfgi_update_data = nullptr, RenderingServerTypes::RenderInfo *r_render_info = nullptr) override;

	void set_time(double p_time, double p_step) override { time = p_time; }
	Ref<RenderSceneBuffers> render_buffers_create() override;
};
