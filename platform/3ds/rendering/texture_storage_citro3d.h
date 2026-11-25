/**************************************************************************/
/*  texture_storage_citro3d.h                                                 */
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

#include "core/templates/rid_owner.h"
#include "servers/rendering/dummy/storage/texture_storage.h"

namespace Citro3D {

struct Texture {
	int width = 0; // Logical size (what Godot sees).
	int height = 0;
	int tex_width = 0; // Power-of-two allocation.
	int tex_height = 0;
	Image::Format format = Image::FORMAT_RGBA8;
	GPU_TEXCOLOR gpu_format = GPU_RGBA8;
	C3D_Tex tex;
	bool allocated = false;
	bool filter = true;
	bool mipmaps = false;

	RID render_target; // Set when this is a viewport texture.
	RID proxy_to; // Set for proxies (AnimatedTexture frames, etc.).
	HashSet<RID> proxies;
	String path;

	// UVs are scaled by this so 0..1 covers the logical area of a padded
	// non-power-of-two texture.
	Vector2 uv_scale() const { return tex_width ? Vector2((float)width / tex_width, (float)height / tex_height) : Vector2(1, 1); }
};

struct CanvasTexture {
	RID diffuse;
	Color modulate = Color(1, 1, 1, 1);
	RSE::CanvasItemTextureFilter filter = RSE::CANVAS_ITEM_TEXTURE_FILTER_DEFAULT;
	RSE::CanvasItemTextureRepeat repeat = RSE::CANVAS_ITEM_TEXTURE_REPEAT_DEFAULT;
};

struct RenderTarget {
	RID self;
	Size2i size;
	Point2i position;
	uint32_t view_count = 1;
	bool transparent = false;
	bool direct_to_screen = false;
	bool used = false;

	// The top screen's target renders one image per eye.
	bool stereo = false;
	int tex_width = 0;
	int tex_height = 0;
	C3D_Tex color[2];
	C3D_RenderTarget *target[2] = { nullptr, nullptr };
	bool allocated = false;
	bool realloc_pending = false; // Resized/stereo changed mid-frame.

	RID texture;
	bool clear_requested = false;
	Color clear_color;
};

class TextureStorage : public RendererDummy::TextureStorage {
	static inline TextureStorage *singleton = nullptr;

	mutable RID_Owner<Texture, true> texture_owner;
	mutable RID_Owner<CanvasTexture, true> canvas_texture_owner;
	mutable RID_Owner<RenderTarget> render_target_owner;

	// citro3d can't create or delete render targets inside a frame, and the
	// GPU may still read freed memory until the next frame starts.
	bool frame_active = false;
	LocalVector<RID> pending_realloc;
	struct DeadTarget {
		C3D_RenderTarget *target[2];
		C3D_Tex color[2];
		int eyes;
	};
	LocalVector<DeadTarget> dead_targets;
	LocalVector<C3D_Tex> dead_textures;

	void _texture_upload(Texture *p_tex, const Ref<Image> &p_image);
	void _texture_release(Texture *p_tex);
	void _render_target_allocate(RenderTarget *p_rt);
	void _render_target_release(RenderTarget *p_rt);
	Ref<Image> _read_back(const C3D_Tex &p_tex, GPU_TEXCOLOR p_format, int p_width, int p_height) const;

public:
	static TextureStorage *get_singleton() { return singleton; }

	Texture *get_texture(RID p_rid) const;
	RenderTarget *get_render_target(RID p_rid) const { return render_target_owner.get_or_null(p_rid); }
	bool owns_render_target(RID p_rid) const { return render_target_owner.owns(p_rid); }

	// Resolves canvas textures and proxies to a drawable texture.
	Texture *resolve_texture(RID p_rid, Color *r_modulate = nullptr) const;

	// Called by the compositor around C3D_FrameBegin/End.
	void frame_prepare(); // Before C3D_FrameBegin: deferred (re)allocations.
	void frame_started(); // After C3D_FrameBegin: the GPU is done with the last frame.
	void set_frame_active(bool p_active) { frame_active = p_active; }

	// Binds a render target for drawing one eye (0 = left/mono, 1 = right).
	bool render_target_begin(RenderTarget *p_rt, int p_eye);
	void render_target_set_stereo(RID p_rt, bool p_stereo);
	void render_target_clear(RenderTarget *p_rt, int p_eye, const Color &p_color, bool p_depth_only = false);

	/* Canvas texture */
	RID canvas_texture_allocate() override;
	void canvas_texture_initialize(RID p_rid) override;
	void canvas_texture_free(RID p_rid) override;
	void canvas_texture_set_channel(RID p_canvas_texture, RSE::CanvasTextureChannel p_channel, RID p_texture) override;
	void canvas_texture_set_shading_parameters(RID p_canvas_texture, const Color &p_base_color, float p_shininess) override;
	void canvas_texture_set_texture_filter(RID p_item, RSE::CanvasItemTextureFilter p_filter) override;
	void canvas_texture_set_texture_repeat(RID p_item, RSE::CanvasItemTextureRepeat p_repeat) override;
	bool owns_canvas_texture(RID p_rid) const { return canvas_texture_owner.owns(p_rid); }

	/* Texture */
	bool owns_texture(RID p_rid) { return texture_owner.owns(p_rid); }
	RID texture_allocate() override;
	void texture_free(RID p_rid) override;
	void texture_2d_initialize(RID p_texture, const Ref<Image> &p_image) override;
	void texture_2d_layered_initialize(RID p_texture, const Vector<Ref<Image>> &p_layers, RSE::TextureLayeredType p_layered_type) override;
	void texture_3d_initialize(RID p_texture, Image::Format, int p_width, int p_height, int p_depth, bool p_mipmaps, const Vector<Ref<Image>> &p_data) override;
	void texture_proxy_initialize(RID p_texture, RID p_base) override;
	void texture_2d_update(RID p_texture, const Ref<Image> &p_image, int p_layer = 0) override;
	void texture_proxy_update(RID p_proxy, RID p_base) override;
	void texture_2d_placeholder_initialize(RID p_texture) override;
	void texture_2d_layered_placeholder_initialize(RID p_texture, RSE::TextureLayeredType p_layered_type) override;
	void texture_3d_placeholder_initialize(RID p_texture) override;
	Ref<Image> texture_2d_get(RID p_texture) const override;
	void texture_replace(RID p_texture, RID p_by_texture) override;
	void texture_set_size_override(RID p_texture, int p_width, int p_height) override;
	void texture_set_path(RID p_texture, const String &p_path) override;
	String texture_get_path(RID p_texture) const override;
	Image::Format texture_get_format(RID p_texture) const override;
	Size2 texture_size_with_proxy(RID p_proxy) override;
	void texture_debug_usage(List<RenderingServerTypes::TextureInfo> *r_info) override;

	/* Render target */
	RID render_target_create() override;
	void render_target_free(RID p_rid) override;
	void render_target_set_position(RID p_render_target, int p_x, int p_y) override;
	Point2i render_target_get_position(RID p_render_target) const override;
	void render_target_set_size(RID p_render_target, int p_width, int p_height, uint32_t p_view_count) override;
	Size2i render_target_get_size(RID p_render_target) const override;
	void render_target_set_transparent(RID p_render_target, bool p_is_transparent) override;
	bool render_target_get_transparent(RID p_render_target) const override;
	void render_target_set_direct_to_screen(RID p_render_target, bool p_direct_to_screen) override;
	bool render_target_get_direct_to_screen(RID p_render_target) const override;
	bool render_target_was_used(RID p_render_target) const override;
	void render_target_set_as_unused(RID p_render_target) override;
	void render_target_request_clear(RID p_render_target, const Color &p_clear_color) override;
	bool render_target_is_clear_requested(RID p_render_target) override;
	Color render_target_get_clear_request_color(RID p_render_target) override;
	void render_target_disable_clear_request(RID p_render_target) override;
	void render_target_do_clear_request(RID p_render_target) override;
	RID render_target_get_texture(RID p_render_target) override;

	TextureStorage();
	~TextureStorage();
};

} // namespace Citro3D
