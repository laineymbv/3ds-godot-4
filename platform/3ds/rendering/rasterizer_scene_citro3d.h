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

namespace Citro3D {
struct Texture;
}

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
		// Filled by _prepare_draw_items() after the lists are built.
		Citro3D::Texture *texture = nullptr; // Resolved albedo texture.
		AABB world_aabb; // For picking lights per object.
		uint32_t state_key = 0; // Raster/TEV/lighting setup, see _state_key().
		uint32_t material_hash = 0; // Light material and texture parameters.
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

	// Lights bound to slots by _setup_lights(), for picking per object.
	struct LightSlot {
		Vector3 origin; // World space.
		float range = 0;
		bool directional = false;
	} light_slots[MAX_LIGHTS];
	int light_slot_count = 0;
	uint8_t _light_mask_for(const DrawItem &p_item) const;

	// The GPU state _apply_material() and _draw_list() last set, so unchanged
	// state is not sent again. render_scene() resets it for each eye after the
	// sky and _setup_lights(), the last things before the lists that touch
	// this state; the shadow pass and the canvas run outside it.
	struct StateCache {
		bool valid = false;
		uint32_t state_key = 0;
		Citro3D::Texture *texture = nullptr;
		int tex_filter = -1;
		int tex_wrap = -1;
		Vector2 uv_scale;
		Vector2 uv_offset;
		uint32_t tev_color = 0;
		C3D_Material light_material = {};
		int8_t two_sided = -1; // Two-sided diffuse on the light slots; -1 unknown.
		uint8_t light_mask = 0; // Enabled light slots. Not reset by valid = false.
		const Citro3D::Vertex *vertices = nullptr;
	} state_cache;

	// Per-frame counters, to measure the effect of the state cache. Reported
	// through the viewport's render info (Performance monitors) and, when
	// rendering/3ds/debug/print_frame_stats is on, printed once a second.
	struct FrameStats {
		uint32_t draw_calls = 0;
		uint32_t shadow_draw_calls = 0;
		uint32_t primitives = 0;
		uint32_t objects = 0;
		uint32_t state_changes = 0; // Groups of GPU state sent (see _apply_material).
		uint32_t state_skips = 0; // Groups skipped because they were unchanged.
		uint32_t texture_binds = 0;
		uint32_t tev_updates = 0;
		uint32_t light_material_updates = 0;
		uint32_t light_mask_changes = 0;
		uint32_t vertex_buffer_binds = 0;
		uint32_t lit_draws = 0;
		uint32_t lights_enabled_total = 0; // Sum over lit draws, for an average.
	} frame_stats, stats_accum;
	int stats_frames = 0;
	uint64_t stats_last_print_ms = 0;
	void _report_stats(RenderingServerTypes::RenderInfo *r_render_info);

	double time = 0;

	// Sky: a sphere around the camera, colored (procedural/physical) or
	// textured (panorama), drawn first without depth.
	struct Sky {
		RID material;
	};
	RID_Owner<Sky, true> sky_owner;
	static constexpr int SKY_SEGMENTS = 24;
	static constexpr int SKY_RINGS = 12;
	Citro3D::Vertex *sky_vertices = nullptr;
	uint16_t *sky_indices = nullptr;
	int sky_index_count = 0;
	Color sky_colors_cache[4];
	bool sky_colors_valid = false;
	Color sky_ambient;
	bool _draw_sky(RID p_environment, const Transform3D &p_view, const Projection &p_proj, const PagedArray<RID> &p_lights, Color &r_ambient);
	void _draw_sun_disks(const PagedArray<RID> &p_lights, const Transform3D &p_view, float p_radius, float p_angle_max, float p_exposure);
	C3D_Tex sun_tex;
	bool sun_tex_ready = false;
	Citro3D::Vertex *sun_vertices = nullptr; // 4 per sun, up to 4 suns.

	// Shadows: one directional light, an orthographic shadow map around the
	// camera, rendered in the PICA200's shadow mode and read by the lighting
	// unit through texture unit 0 (albedo textures use unit 1).
	C3D_Tex shadow_tex;
	C3D_RenderTarget *shadow_target = nullptr;
	int shadow_size = 0;
	bool shadow_active = false;
	bool shadow_perspective = false;
	RID shadow_light_instance;
	Projection shadow_projview; // Godot-style projection * light view.
	bool _render_shadow_map(const PagedArray<RID> &p_lights, const CameraData *p_camera_data);
	void _set_shadow_matrix(const Transform3D &p_view);

	C3D_FogLut fog_lut;
	struct FogKey {
		int mode = -1;
		float a = 0, b = 0, c = 0, near = 0, far = 0;
		bool operator==(const FogKey &o) const { return mode == o.mode && a == o.a && b == o.b && c == o.c && near == o.near && far == o.far; }
	} fog_key;
	void _setup_fog(RID p_environment, const Projection &p_proj);

	void _add_mesh(GeometryInstanceCitro3D *p_gi, RID p_mesh, const Transform3D &p_model, const Color &p_color, float p_depth);
	uint32_t _state_key(const DrawItem &p_item, bool p_transparent) const;
	void _prepare_draw_items(LocalVector<DrawItem> &p_list, bool p_transparent);
	void _fill_draw_lists(const CameraData *p_camera_data, const PagedArray<RenderGeometryInstance *> &p_instances);
	void _setup_lights(const PagedArray<RID> &p_lights, const Transform3D &p_view, const Vector3 &p_cam_pos, const Color &p_ambient);
	void _draw_item_geometry(const DrawItem &p_item, const Transform3D &p_view);
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

	RID sky_allocate() override;
	void sky_initialize(RID p_rid) override;
	void sky_set_material(RID p_sky, RID p_material) override;
	bool free(RID p_rid) override;
	Ref<RenderSceneBuffers> render_buffers_create() override;
};
