/**************************************************************************/
/*  rasterizer_citro3d.cpp                                                */
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

#include "rasterizer_citro3d.h"

#include "rasterizer_canvas_citro3d.h"
#include "rasterizer_scene_citro3d.h"

#include "light_storage_citro3d.h"
#include "material_storage_citro3d.h"
#include "mesh_storage_citro3d.h"
#include "texture_storage_citro3d.h"
#include "utilities_citro3d.h"

#include "platform/3ds/rendering/shaders/canvas.v.pica.gen.h"
#include "platform/3ds/rendering/shaders/scene.v.pica.gen.h"
#include "platform/3ds/rendering/shaders/shadow.v.pica.gen.h"

#include "core/config/project_settings.h"
#include "core/io/image.h"
#include "servers/rendering/dummy/environment/fog.h"
#include "servers/rendering/dummy/environment/gi.h"
#include "servers/rendering/dummy/storage/particles_storage.h"

using namespace Citro3D;

#define DISPLAY_TRANSFER_FLAGS                                                                    \
	(GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) |              \
			GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8) | \
			GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO))

RasterizerCitro3D::RasterizerCitro3D() {
	singleton = this;
	texture_storage = memnew(TextureStorage);
	material_storage = memnew(MaterialStorage);
	mesh_storage = memnew(MeshStorage);
	light_storage = memnew(LightStorage);
	utilities = memnew(Utilities);
	particles_storage = memnew(RendererDummy::ParticlesStorage);
	fog = memnew(RendererDummy::Fog);
	gi = memnew(RendererDummy::GI);
	canvas = memnew(RasterizerCanvasCitro3D);
	scene = memnew(RasterizerSceneCitro3D);
}

RasterizerCitro3D::~RasterizerCitro3D() {
	memdelete(scene);
	memdelete(canvas);
	memdelete(gi);
	memdelete(fog);
	memdelete(particles_storage);
	memdelete(utilities);
	memdelete(light_storage);
	memdelete(mesh_storage);
	memdelete(material_storage);
	memdelete(texture_storage);
	singleton = nullptr;
}

RendererCanvasRender *RasterizerCitro3D::get_canvas() {
	return canvas;
}
RendererSceneRender *RasterizerCitro3D::get_scene() {
	return scene;
}
RendererFog *RasterizerCitro3D::get_fog() {
	return fog;
}
RendererGI *RasterizerCitro3D::get_gi() {
	return gi;
}
RendererLightStorage *RasterizerCitro3D::get_light_storage() {
	return light_storage;
}
RendererMaterialStorage *RasterizerCitro3D::get_material_storage() {
	return material_storage;
}
RendererMeshStorage *RasterizerCitro3D::get_mesh_storage() {
	return mesh_storage;
}
RendererParticlesStorage *RasterizerCitro3D::get_particles_storage() {
	return particles_storage;
}
RendererTextureStorage *RasterizerCitro3D::get_texture_storage() {
	return texture_storage;
}
RendererUtilities *RasterizerCitro3D::get_utilities() {
	return utilities;
}

static DVLB_s *_load_shader(const uint8_t *p_data, uint32_t p_size, void **r_copy) {
	// DVLB_ParseFile keeps pointers into the buffer, so it must outlive it.
	*r_copy = memalloc(p_size);
	memcpy(*r_copy, p_data, p_size);
	return DVLB_ParseFile((u32 *)*r_copy, p_size);
}

void RasterizerCitro3D::initialize() {
	C3D_Init(C3D_DEFAULT_CMDBUF_SIZE * 2);

	screen_targets[0] = C3D_RenderTargetCreate(240, 400, GPU_RB_RGBA8, C3D_DEPTHTYPE(-1));
	screen_targets[1] = C3D_RenderTargetCreate(240, 400, GPU_RB_RGBA8, C3D_DEPTHTYPE(-1));
	screen_targets[2] = C3D_RenderTargetCreate(240, 320, GPU_RB_RGBA8, C3D_DEPTHTYPE(-1));
	C3D_RenderTargetSetOutput(screen_targets[0], GFX_TOP, GFX_LEFT, DISPLAY_TRANSFER_FLAGS);
	C3D_RenderTargetSetOutput(screen_targets[1], GFX_TOP, GFX_RIGHT, DISPLAY_TRANSFER_FLAGS);
	C3D_RenderTargetSetOutput(screen_targets[2], GFX_BOTTOM, GFX_LEFT, DISPLAY_TRANSFER_FLAGS);

	scene_dvlb = _load_shader(shader_scene_shbin, shader_scene_shbin_size, &scene_shbin);
	shaderProgramInit(&scene_program);
	shaderProgramSetVsh(&scene_program, &scene_dvlb->DVLE[0]);
	scene_uniforms.projection = shaderInstanceGetUniformLocation(scene_program.vertexShader, "projection");
	scene_uniforms.model_view = shaderInstanceGetUniformLocation(scene_program.vertexShader, "modelView");
	scene_uniforms.uv_transform = shaderInstanceGetUniformLocation(scene_program.vertexShader, "uvTransform");
	scene_uniforms.color_scale = shaderInstanceGetUniformLocation(scene_program.vertexShader, "colorScale");
	scene_uniforms.color_bias = shaderInstanceGetUniformLocation(scene_program.vertexShader, "colorBias");
	scene_uniforms.shadow_mtx = shaderInstanceGetUniformLocation(scene_program.vertexShader, "shadowMtx");

	canvas_dvlb = _load_shader(shader_canvas_shbin, shader_canvas_shbin_size, &canvas_shbin);
	shaderProgramInit(&canvas_program);
	shaderProgramSetVsh(&canvas_program, &canvas_dvlb->DVLE[0]);
	canvas_uniforms.projection = shaderInstanceGetUniformLocation(canvas_program.vertexShader, "projection");

	shadow_dvlb = _load_shader(shader_shadow_shbin, shader_shadow_shbin_size, &shadow_shbin);
	shaderProgramInit(&shadow_program);
	shaderProgramSetVsh(&shadow_program, &shadow_dvlb->DVLE[0]);
	shadow_uniforms.projection = shaderInstanceGetUniformLocation(shadow_program.vertexShader, "projection");
	shadow_uniforms.model_view = shaderInstanceGetUniformLocation(shadow_program.vertexShader, "modelView");

	stream_vertices = (CanvasVertex *)linearAlloc(sizeof(CanvasVertex) * STREAM_VERTICES);
	stream_indices = (uint16_t *)linearAlloc(sizeof(uint16_t) * STREAM_INDICES);
	ERR_FAIL_COND_MSG(!stream_vertices || !stream_indices, "3DS: out of linear memory for the 2D vertex stream.");

	scene->initialize();
	canvas->initialize();

	print_line(vformat("citro3d renderer: linear free %d KiB, VRAM free %d KiB.", (int)(linearSpaceFree() / 1024), (int)(vramSpaceFree() / 1024)));
}

void RasterizerCitro3D::finalize() {
	if (in_frame) {
		C3D_FrameEnd(0);
		in_frame = false;
	}
	scene->finalize();
	canvas->finalize();
	for (C3D_RenderTarget *&t : screen_targets) {
		if (t) {
			C3D_RenderTargetDelete(t);
			t = nullptr;
		}
	}
	if (stream_vertices) {
		linearFree(stream_vertices);
		stream_vertices = nullptr;
	}
	if (stream_indices) {
		linearFree(stream_indices);
		stream_indices = nullptr;
	}
	shaderProgramFree(&scene_program);
	shaderProgramFree(&canvas_program);
	shaderProgramFree(&shadow_program);
	if (shadow_dvlb) {
		DVLB_Free(shadow_dvlb);
		memfree(shadow_shbin);
	}
	if (scene_dvlb) {
		DVLB_Free(scene_dvlb);
		memfree(scene_shbin);
	}
	if (canvas_dvlb) {
		DVLB_Free(canvas_dvlb);
		memfree(canvas_shbin);
	}
	C3D_Fini();
}

void RasterizerCitro3D::bind_scene_program() {
	if (bound_program == 0) {
		return;
	}
	C3D_BindProgram(&scene_program);
	C3D_AttrInfo *ai = C3D_GetAttrInfo();
	AttrInfo_Init(ai);
	AttrInfo_AddLoader(ai, 0, GPU_FLOAT, 3); // position
	AttrInfo_AddLoader(ai, 1, GPU_FLOAT, 3); // normal
	AttrInfo_AddLoader(ai, 2, GPU_FLOAT, 2); // uv
	AttrInfo_AddLoader(ai, 3, GPU_UNSIGNED_BYTE, 4); // color
	bound_program = 0;
}

void RasterizerCitro3D::bind_shadow_program() {
	if (bound_program == 2) {
		return;
	}
	C3D_BindProgram(&shadow_program);
	C3D_AttrInfo *ai = C3D_GetAttrInfo();
	AttrInfo_Init(ai);
	AttrInfo_AddLoader(ai, 0, GPU_FLOAT, 3); // Same vertex layout as the scene.
	AttrInfo_AddLoader(ai, 1, GPU_FLOAT, 3);
	AttrInfo_AddLoader(ai, 2, GPU_FLOAT, 2);
	AttrInfo_AddLoader(ai, 3, GPU_UNSIGNED_BYTE, 4);
	bound_program = 2;
}

void RasterizerCitro3D::bind_canvas_program() {
	if (bound_program == 1) {
		return;
	}
	C3D_BindProgram(&canvas_program);
	C3D_AttrInfo *ai = C3D_GetAttrInfo();
	AttrInfo_Init(ai);
	AttrInfo_AddLoader(ai, 0, GPU_FLOAT, 2); // position
	AttrInfo_AddLoader(ai, 1, GPU_FLOAT, 2); // uv
	AttrInfo_AddLoader(ai, 2, GPU_UNSIGNED_BYTE, 4); // color
	C3D_BufInfo *bi = C3D_GetBufInfo();
	BufInfo_Init(bi);
	BufInfo_Add(bi, stream_vertices, sizeof(CanvasVertex), 3, 0x210);
	// The scene binds per-mesh buffers, so this has to be redone after it.
	C3D_LightEnvBind(nullptr);
	bound_program = 1;
}

CanvasVertex *RasterizerCitro3D::stream_alloc(uint32_t p_count, uint32_t &r_first_index) {
	if (stream_vertex_used + p_count > STREAM_VERTICES) {
		WARN_PRINT_ONCE("3DS: 2D vertex stream full this frame; some canvas items were skipped.");
		return nullptr;
	}
	r_first_index = stream_vertex_used;
	CanvasVertex *ptr = stream_vertices + stream_vertex_used;
	stream_vertex_used += p_count;
	return ptr;
}

uint16_t *RasterizerCitro3D::stream_alloc_indices(uint32_t p_count) {
	if (stream_index_used + p_count > STREAM_INDICES) {
		WARN_PRINT_ONCE("3DS: 2D index stream full this frame; some canvas items were skipped.");
		return nullptr;
	}
	uint16_t *ptr = stream_indices + stream_index_used;
	stream_index_used += p_count;
	return ptr;
}

float RasterizerCitro3D::get_stereo_separation() const {
	if (stereo_separation_override >= 0.0f) {
		return stereo_separation_override;
	}
	return GLOBAL_GET_CACHED(float, "rendering/3ds/stereo/max_separation");
}

float RasterizerCitro3D::get_stereo_convergence() const {
	if (stereo_convergence_override > 0.0f) {
		return stereo_convergence_override;
	}
	return GLOBAL_GET_CACHED(float, "rendering/3ds/stereo/convergence");
}

void RasterizerCitro3D::request_screen_dump(const String &p_dir) {
	screen_dump_dir = p_dir;
	screen_dump_state = 1;
}

void RasterizerCitro3D::_dump_screens() {
	// The screen targets hold last frame's final image (before the RGB8
	// display transfer): tiled RGBA8, 240 x 400/320, rotated a quarter turn.
	static const char *names[3] = { "top_left.png", "top_right.png", "bottom.png" };
	for (int i = 0; i < 3; i++) {
		C3D_RenderTarget *t = screen_targets[i];
		if (!t || (i == 1 && !stereo_active)) {
			continue;
		}
		int fw = t->frameBuf.width; // 240
		int fh = t->frameBuf.height; // 400 or 320
		const uint8_t *src = (const uint8_t *)t->frameBuf.colorBuf;
		Vector<uint8_t> data;
		data.resize(fw * fh * 4);
		uint8_t *w = data.ptrw();
		// Framebuffer column c / row m -> screen pixel (x = m, y = fw - 1 - c).
		for (int m = 0; m < fh; m++) {
			for (int c = 0; c < fw; c++) {
				const uint8_t *px = src + tiled_offset(c, m, fw) * 4;
				int x = m;
				int y = fw - 1 - c;
				uint8_t *d = w + (y * fh + x) * 4;
				d[0] = px[3];
				d[1] = px[2];
				d[2] = px[1];
				d[3] = 255;
			}
		}
		Ref<Image> img = Image::create_from_data(fh, fw, false, Image::FORMAT_RGBA8, data);
		img->save_png(screen_dump_dir.path_join(names[i]));
	}
	print_line(vformat("3DS: saved screens to %s", screen_dump_dir));
}

int RasterizerCitro3D::get_eye_count(const RenderTarget *p_rt) const {
	return (p_rt->stereo && stereo_active) ? 2 : 1;
}

void RasterizerCitro3D::begin_frame(double frame_step) {
	frame++;
	delta = frame_step;
	time += frame_step;

	slider = osGet3DSliderState();
	stereo_active = slider > 0.0f;
	gfxSet3D(stereo_active);

	stream_vertex_used = 0;
	stream_index_used = 0;
	bound_program = -1;
	screen_blits[0].clear();
	screen_blits[1].clear();

	if (frame < 6 || frame % 60 == 0) {
		print_verbose(vformat("C3D trace: begin_frame %d slider=%.2f", (int64_t)frame, slider));
	}
	texture_storage->frame_prepare();
	C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
	in_frame = true;
	texture_storage->set_frame_active(true);
	texture_storage->frame_started();
	if (screen_dump_state == 2) {
		// C3D_FrameBegin waited for the GPU, so last frame is complete.
		_dump_screens();
		screen_dump_state = 0;
	} else if (screen_dump_state == 1) {
		screen_dump_state = 2;
	}
	mesh_storage->frame_started();
	scene->set_time(time, frame_step);
}

void RasterizerCitro3D::blit_render_targets_to_screen(DisplayServerEnums::WindowID p_screen, const RenderingServerTypes::BlitToScreen *p_render_targets, int p_amount) {
	// Window 0 is the top screen; any other window is on the bottom screen.
	int screen = p_screen == DisplayServerEnums::MAIN_WINDOW_ID ? 0 : 1;
	for (int i = 0; i < p_amount; i++) {
		ScreenBlit b;
		b.render_target = p_render_targets[i].render_target;
		b.dst = p_render_targets[i].dst_rect;
		screen_blits[screen].push_back(b);
		if (screen == 0) {
			// The top screen's target gets a second (right eye) image.
			texture_storage->render_target_set_stereo(b.render_target, true);
		}
	}
}

void RasterizerCitro3D::_draw_screen_quad(C3D_RenderTarget *p_target, int p_screen_w, const C3D_Tex *p_tex, const Rect2 &p_dst, const Vector2 &p_uv_max) {
	uint32_t first;
	CanvasVertex *v = stream_alloc(4, first);
	if (!v) {
		return;
	}
	Rect2 dst = p_dst.size == Size2() ? Rect2(0, 0, p_screen_w, 240) : p_dst;
	float x0 = dst.position.x, y0 = dst.position.y, x1 = dst.position.x + dst.size.x, y1 = dst.position.y + dst.size.y;
	float corners[4][4] = {
		{ x0, y0, 0, 0 },
		{ x0, y1, 0, p_uv_max.y },
		{ x1, y0, p_uv_max.x, 0 },
		{ x1, y1, p_uv_max.x, p_uv_max.y },
	};
	for (int i = 0; i < 4; i++) {
		v[i].x = corners[i][0];
		v[i].y = corners[i][1];
		v[i].u = corners[i][2];
		v[i].v = corners[i][3];
		v[i].color[0] = v[i].color[1] = v[i].color[2] = v[i].color[3] = 255;
	}
	GSPGPU_FlushDataCache(v, sizeof(CanvasVertex) * 4);

	C3D_Mtx proj;
	Mtx_OrthoTilt(&proj, 0.0f, (float)p_screen_w, 240.0f, 0.0f, 1.0f, -1.0f, true);
	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, canvas_uniforms.projection, &proj);

	C3D_TexBind(0, (C3D_Tex *)p_tex);
	C3D_TexEnv *env = C3D_GetTexEnv(0);
	C3D_TexEnvInit(env);
	C3D_TexEnvSrc(env, C3D_Both, GPU_TEXTURE0, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
	C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);
	for (int i = 1; i < 6; i++) {
		C3D_TexEnvInit(C3D_GetTexEnv(i));
	}
	C3D_DrawArrays(GPU_TRIANGLE_STRIP, first, 4);
}

void RasterizerCitro3D::_present() {
	bound_program = -1;
	bind_canvas_program();
	C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR);
	C3D_CullFace(GPU_CULL_NONE);
	C3D_AlphaTest(false, GPU_ALWAYS, 0);
	C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);

	struct Out {
		int target;
		int window;
		int eye;
		int width;
	};
	const Out outs[3] = { { 0, 0, 0, 400 }, { 1, 0, 1, 400 }, { 2, 1, 0, 320 } };
	for (const Out &o : outs) {
		if (o.eye == 1 && !stereo_active) {
			continue;
		}
		C3D_RenderTarget *target = screen_targets[o.target];
		C3D_RenderTargetClear(target, C3D_CLEAR_ALL, 0x000000FF, 0);
		C3D_FrameDrawOn(target);
		for (const ScreenBlit &b : screen_blits[o.window]) {
			RenderTarget *rt = texture_storage->get_render_target(b.render_target);
			if (!rt || !rt->allocated) {
				continue;
			}
			int eye = (o.eye == 1 && rt->stereo && rt->target[1]) ? 1 : 0;
			Vector2 uv_max((float)rt->size.x / rt->tex_width, (float)rt->size.y / rt->tex_height);
			_draw_screen_quad(target, o.width, &rt->color[eye], b.dst, uv_max);
		}
	}
}

void RasterizerCitro3D::end_frame(bool p_present) {
	if (!in_frame) {
		return;
	}
	_present();
	if (frame < 6) {
		print_verbose(vformat("C3D trace: end_frame %d blits top=%d bottom=%d verts=%d", (int64_t)frame, screen_blits[0].size(), screen_blits[1].size(), stream_vertex_used));
	}
	C3D_FrameEnd(0);
	in_frame = false;
	texture_storage->set_frame_active(false);
}

void RasterizerCitro3D::set_boot_image_with_stretch(const Ref<Image> &p_image, const Color &p_color, RSE::SplashStretchMode p_stretch_mode, bool p_use_filter) {
	if (p_image.is_null() || p_image->is_empty()) {
		return;
	}
	RID tex_rid = texture_storage->texture_allocate();
	texture_storage->texture_2d_initialize(tex_rid, p_image);
	Texture *tex = texture_storage->get_texture(tex_rid);
	if (!tex || !tex->allocated) {
		texture_storage->texture_free(tex_rid);
		return;
	}
	Size2 screen(400, 240);
	Rect2 dst = RenderingServerTypes::get_splash_stretched_screen_rect(Size2(p_image->get_width(), p_image->get_height()), screen, p_stretch_mode);

	C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
	stream_vertex_used = 0;
	bound_program = -1;
	bind_canvas_program();
	C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR);
	C3D_CullFace(GPU_CULL_NONE);
	C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA, GPU_ONE, GPU_ZERO);
	for (int i = 0; i < 2; i++) {
		C3D_RenderTargetClear(screen_targets[i], C3D_CLEAR_ALL, color_to_u32(p_color), 0);
		C3D_FrameDrawOn(screen_targets[i]);
		_draw_screen_quad(screen_targets[i], 400, &tex->tex, dst, tex->uv_scale());
	}
	C3D_RenderTargetClear(screen_targets[2], C3D_CLEAR_ALL, color_to_u32(p_color), 0);
	C3D_FrameDrawOn(screen_targets[2]);
	C3D_FrameEnd(0);
	C3D_FrameSync(); // Make sure the GPU is done with the texture.
	texture_storage->texture_free(tex_rid);
}
