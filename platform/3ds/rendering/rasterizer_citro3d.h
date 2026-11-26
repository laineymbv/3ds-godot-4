/**************************************************************************/
/*  rasterizer_citro3d.h                                                  */
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

#include "servers/rendering/renderer_compositor.h"

class RasterizerCanvasCitro3D;
class RasterizerSceneCitro3D;

namespace RendererDummy {
class Fog;
class GI;
class ParticlesStorage;
} // namespace RendererDummy

namespace Citro3D {
class LightStorage;
class MaterialStorage;
class MeshStorage;
class TextureStorage;
class Utilities;
struct RenderTarget;
struct VertexQuant;

// 2D vertex as streamed to the canvas shader.
struct CanvasVertex {
	float x, y;
	float u, v;
	uint8_t color[4];
	float lu, lv; // 2D light texture coordinates.
};
} // namespace Citro3D

// RendererCompositor for the Nintendo 3DS on citro3d. Render targets are
// VRAM textures; at the end of a frame the ones attached to windows are drawn
// onto the screens. The top screen's target holds one image per eye.
class RasterizerCitro3D : public RendererCompositor {
	static inline RasterizerCitro3D *singleton = nullptr;

	uint64_t frame = 1;
	double delta = 0;
	double time = 0.0;
	bool in_frame = false;

	RasterizerCanvasCitro3D *canvas = nullptr;
	RasterizerSceneCitro3D *scene = nullptr;
	RendererDummy::Fog *fog = nullptr;
	RendererDummy::GI *gi = nullptr;
	RendererDummy::ParticlesStorage *particles_storage = nullptr;
	Citro3D::LightStorage *light_storage = nullptr;
	Citro3D::MaterialStorage *material_storage = nullptr;
	Citro3D::MeshStorage *mesh_storage = nullptr;
	Citro3D::TextureStorage *texture_storage = nullptr;
	Citro3D::Utilities *utilities = nullptr;

	// Shaders.
	void *scene_shbin = nullptr;
	void *canvas_shbin = nullptr;
	void *shadow_shbin = nullptr;
	DVLB_s *shadow_dvlb = nullptr;
	shaderProgram_s shadow_program;
	DVLB_s *scene_dvlb = nullptr;
	DVLB_s *canvas_dvlb = nullptr;
	shaderProgram_s scene_program;
	shaderProgram_s canvas_program;
	int bound_program = -1;
	int bound_layout = -1; // 0 = float Vertex, 1 = PackedVertex, -1 = other.
	void _set_mesh_attributes(bool p_packed);

	// Screens: top left eye, top right eye, bottom.
	C3D_RenderTarget *screen_targets[3] = {};
	struct ScreenBlit {
		RID render_target;
		Rect2 dst;
	};
	Vector<ScreenBlit> screen_blits[2]; // Per window: 0 = top, 1 = bottom.

	// 2D vertex/index streams, rewound every frame.
	Citro3D::CanvasVertex *stream_vertices = nullptr;
	uint16_t *stream_indices = nullptr;
	uint32_t stream_vertex_used = 0;
	uint32_t stream_index_used = 0;

	float slider = 0.0;
	bool stereo_active = false;

	String screen_dump_dir;
	int screen_dump_state = 0; // 1 = render this frame, 2 = read next frame.
	void _dump_screens();

	void _present();
	void _draw_screen_quad(C3D_RenderTarget *p_target, int p_screen_w, const C3D_Tex *p_tex, const Rect2 &p_dst, const Vector2 &p_uv_max);

public:
	static constexpr uint32_t STREAM_VERTICES = 32768;
	static constexpr uint32_t STREAM_INDICES = 49152;

	static RasterizerCitro3D *get_singleton() { return singleton; }

	// Shader uniform locations.
	struct {
		int projection = -1;
		int model_view = -1;
		int uv_transform = -1;
		int color_scale = -1;
		int color_bias = -1;
		int shadow_mtx = -1;
		int pos_scale = -1;
		int pos_offset = -1;
		int uv_quant = -1;
	} scene_uniforms;
	struct {
		int projection = -1;
	} canvas_uniforms;
	struct {
		int projection = -1;
		int model_view = -1;
		int pos_scale = -1;
		int pos_offset = -1;
	} shadow_uniforms;
	void bind_shadow_program();
	// Mesh vertex layout for the bound scene or shadow program: packed
	// (Citro3D::PackedVertex, dequantized with p_quant) or, when p_quant is
	// null, the float Citro3D::Vertex. Also sets the dequantize uniforms.
	void set_mesh_vertex_format(const Citro3D::VertexQuant *p_quant);

	void bind_scene_program();
	void bind_canvas_program();

	// Reserves streamed 2D vertices; returns null when the stream is full.
	Citro3D::CanvasVertex *stream_alloc(uint32_t p_count, uint32_t &r_first_index);
	uint16_t *stream_alloc_indices(uint32_t p_count);
	Citro3D::CanvasVertex *get_stream_vertices() const { return stream_vertices; }

	float stereo_separation_override = -1.0;
	float stereo_convergence_override = -1.0;
	float get_stereo_separation() const;
	float get_stereo_convergence() const;
	void request_screen_dump(const String &p_dir);

	float get_3d_slider() const { return slider; }
	bool is_stereo_active() const { return stereo_active; }
	int get_eye_count(const Citro3D::RenderTarget *p_rt) const;

	RendererCanvasRender *get_canvas() override;
	RendererSceneRender *get_scene() override;
	RendererFog *get_fog() override;
	RendererGI *get_gi() override;
	RendererLightStorage *get_light_storage() override;
	RendererMaterialStorage *get_material_storage() override;
	RendererMeshStorage *get_mesh_storage() override;
	RendererParticlesStorage *get_particles_storage() override;
	RendererTextureStorage *get_texture_storage() override;
	RendererUtilities *get_utilities() override;

	void set_boot_image_with_stretch(const Ref<Image> &p_image, const Color &p_color, RSE::SplashStretchMode p_stretch_mode, bool p_use_filter = true) override;

	void initialize() override;
	void begin_frame(double frame_step) override;
	void blit_render_targets_to_screen(DisplayServerEnums::WindowID p_screen, const RenderingServerTypes::BlitToScreen *p_render_targets, int p_amount) override;
	bool is_opengl() override { return false; }
	void gl_end_frame(bool p_swap_buffers) override {}
	void end_frame(bool p_present) override;
	void finalize() override;

	uint64_t get_frame_number() const override { return frame; }
	double get_frame_delta_time() const override { return delta; }
	double get_total_time() const override { return time; }
	bool can_create_resources_async() const override { return false; }

	static RendererCompositor *_create_current() { return memnew(RasterizerCitro3D); }
	static void make_current() {
		_create_func = _create_current;
		low_end = true;
	}

	RasterizerCitro3D();
	~RasterizerCitro3D();
};
