/**************************************************************************/
/*  texture_storage_citro3d.cpp                                           */
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

#include "texture_storage_citro3d.h"

#include "core/io/image.h"

using namespace Citro3D;

TextureStorage::TextureStorage() {
	singleton = this;
}

TextureStorage::~TextureStorage() {
	singleton = nullptr;
}

/* Canvas texture */

RID TextureStorage::canvas_texture_allocate() {
	return canvas_texture_owner.allocate_rid();
}

void TextureStorage::canvas_texture_initialize(RID p_rid) {
	canvas_texture_owner.initialize_rid(p_rid);
}

void TextureStorage::canvas_texture_free(RID p_rid) {
	canvas_texture_owner.free(p_rid);
}

void TextureStorage::canvas_texture_set_channel(RID p_canvas_texture, RSE::CanvasTextureChannel p_channel, RID p_texture) {
	CanvasTexture *ct = canvas_texture_owner.get_or_null(p_canvas_texture);
	ERR_FAIL_NULL(ct);
	if (p_channel == RSE::CANVAS_TEXTURE_CHANNEL_DIFFUSE) {
		ct->diffuse = p_texture; // Normal/specular maps need 2D lights: not supported.
	}
}

void TextureStorage::canvas_texture_set_shading_parameters(RID p_canvas_texture, const Color &p_base_color, float p_shininess) {
	CanvasTexture *ct = canvas_texture_owner.get_or_null(p_canvas_texture);
	ERR_FAIL_NULL(ct);
	ct->modulate = p_base_color;
}

void TextureStorage::canvas_texture_set_texture_filter(RID p_item, RSE::CanvasItemTextureFilter p_filter) {
	CanvasTexture *ct = canvas_texture_owner.get_or_null(p_item);
	ERR_FAIL_NULL(ct);
	ct->filter = p_filter;
}

void TextureStorage::canvas_texture_set_texture_repeat(RID p_item, RSE::CanvasItemTextureRepeat p_repeat) {
	CanvasTexture *ct = canvas_texture_owner.get_or_null(p_item);
	ERR_FAIL_NULL(ct);
	ct->repeat = p_repeat;
}

/* Texture */

Texture *TextureStorage::get_texture(RID p_rid) const {
	return texture_owner.get_or_null(p_rid);
}

Texture *TextureStorage::resolve_texture(RID p_rid, Color *r_modulate) const {
	if (p_rid.is_null()) {
		return nullptr;
	}
	CanvasTexture *ct = canvas_texture_owner.get_or_null(p_rid);
	if (ct) {
		if (r_modulate) {
			*r_modulate = ct->modulate;
		}
		p_rid = ct->diffuse;
	}
	Texture *t = texture_owner.get_or_null(p_rid);
	for (int i = 0; t && t->proxy_to.is_valid() && i < 8; i++) {
		t = texture_owner.get_or_null(t->proxy_to);
	}
	if (t && t->render_target.is_valid()) {
		RenderTarget *rt = render_target_owner.get_or_null(t->render_target);
		if (!rt || !rt->allocated) {
			return nullptr;
		}
	}
	return (t && t->allocated) ? t : nullptr;
}

RID TextureStorage::texture_allocate() {
	return texture_owner.allocate_rid();
}

void TextureStorage::_texture_release(Texture *p_tex) {
	if (p_tex->allocated && p_tex->render_target.is_null()) {
		dead_textures.push_back(p_tex->tex); // Freed once the GPU is done.
	}
	p_tex->allocated = false;
}

void TextureStorage::frame_prepare() {
	for (DeadTarget &d : dead_targets) {
		for (int i = 0; i < 2; i++) {
			if (d.target[i]) {
				C3D_RenderTargetDelete(d.target[i]);
			}
		}
		for (int i = 0; i < d.eyes; i++) {
			C3D_TexDelete(&d.color[i]);
		}
	}
	dead_targets.clear();
	LocalVector<RID> pending;
	for (const RID &rid : pending_realloc) {
		pending.push_back(rid);
	}
	pending_realloc.clear();
	for (const RID &rid : pending) {
		RenderTarget *rt = render_target_owner.get_or_null(rid);
		if (rt && rt->realloc_pending) {
			rt->realloc_pending = false;
			_render_target_allocate(rt);
		}
	}
}

void TextureStorage::frame_started() {
	for (C3D_Tex &t : dead_textures) {
		C3D_TexDelete(&t);
	}
	dead_textures.clear();
}

void TextureStorage::texture_free(RID p_rid) {
	Texture *t = texture_owner.get_or_null(p_rid);
	ERR_FAIL_NULL(t);
	if (t->proxy_to.is_valid()) {
		Texture *base = texture_owner.get_or_null(t->proxy_to);
		if (base) {
			base->proxies.erase(p_rid);
		}
	}
	for (const RID &proxy : t->proxies) {
		Texture *p = texture_owner.get_or_null(proxy);
		if (p) {
			p->proxy_to = RID();
		}
	}
	_texture_release(t);
	texture_owner.free(p_rid);
}

void TextureStorage::_texture_upload(Texture *p_tex, const Ref<Image> &p_image) {
	ERR_FAIL_COND(p_image.is_null() || p_image->is_empty());
	Ref<Image> img = p_image->duplicate();
	if (img->is_compressed()) {
		if (img->decompress() != OK) {
			ERR_PRINT(vformat("3DS: can't decompress texture format %s; import textures as Lossless/Lossy.", Image::get_format_name(img->get_format())));
			img->initialize_data(8, 8, false, Image::FORMAT_RGBA8);
			img->fill(Color(1, 0, 1, 1));
		}
	}
	img->clear_mipmaps();

	p_tex->width = img->get_width();
	p_tex->height = img->get_height();
	p_tex->format = p_image->get_format();

	// The PICA200 tops out at 1024x1024.
	while (img->get_width() > 1024 || img->get_height() > 1024) {
		img->resize(MAX(1, img->get_width() / 2), MAX(1, img->get_height() / 2), Image::INTERPOLATE_BILINEAR);
	}
	int iw = img->get_width();
	int ih = img->get_height();
	int tw = pot(iw);
	int th = pot(ih);

	GPU_TEXCOLOR fmt;
	int bpp;
	switch (img->get_format()) {
		case Image::FORMAT_L8:
			fmt = GPU_L8;
			bpp = 1;
			break;
		case Image::FORMAT_LA8:
			fmt = GPU_LA8;
			bpp = 2;
			break;
		default:
			if (img->get_format() != Image::FORMAT_RGBA8) {
				img->convert(Image::FORMAT_RGBA8);
			}
			fmt = GPU_RGBA8;
			bpp = 4;
			break;
	}

	if (p_tex->allocated && (p_tex->tex_width != tw || p_tex->tex_height != th || p_tex->gpu_format != fmt)) {
		_texture_release(p_tex);
	}
	if (!p_tex->allocated) {
		if (!C3D_TexInit(&p_tex->tex, tw, th, fmt)) {
			ERR_PRINT(vformat("3DS: out of linear memory for a %dx%d texture.", tw, th));
			return;
		}
		p_tex->allocated = true;
	}
	p_tex->tex_width = tw;
	p_tex->tex_height = th;
	p_tex->gpu_format = fmt;

	// The logical size stays what Godot uploaded; when the image had to be
	// shrunk, UVs still span it because uv_scale uses the stored ratio.
	if (iw != p_tex->width || ih != p_tex->height) {
		p_tex->width = iw;
		p_tex->height = ih;
	}

	uint8_t *dst = (uint8_t *)p_tex->tex.data;
	memset(dst, 0, tw * th * bpp);
	const uint8_t *src = img->ptr();
	for (int y = 0; y < ih; y++) {
		for (int x = 0; x < iw; x++) {
			const uint8_t *s = src + (y * iw + x) * bpp;
			uint8_t *d = dst + image_offset(x, y, tw, th) * bpp;
			if (bpp == 4) {
				d[0] = s[3];
				d[1] = s[2];
				d[2] = s[1];
				d[3] = s[0];
			} else if (bpp == 2) {
				d[0] = s[1];
				d[1] = s[0];
			} else {
				d[0] = s[0];
			}
		}
	}
	C3D_TexFlush(&p_tex->tex);
	C3D_TexSetFilter(&p_tex->tex, GPU_LINEAR, GPU_LINEAR);
	C3D_TexSetWrap(&p_tex->tex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
}

void TextureStorage::texture_2d_initialize(RID p_texture, const Ref<Image> &p_image) {
	Texture tex;
	texture_owner.initialize_rid(p_texture, tex);
	Texture *t = texture_owner.get_or_null(p_texture);
	_texture_upload(t, p_image);
}

void TextureStorage::texture_2d_layered_initialize(RID p_texture, const Vector<Ref<Image>> &p_layers, RSE::TextureLayeredType p_layered_type) {
	// Layered textures aren't supported; keep the first layer usable.
	Texture tex;
	texture_owner.initialize_rid(p_texture, tex);
	if (p_layers.size()) {
		_texture_upload(texture_owner.get_or_null(p_texture), p_layers[0]);
	}
}

void TextureStorage::texture_3d_initialize(RID p_texture, Image::Format, int p_width, int p_height, int p_depth, bool p_mipmaps, const Vector<Ref<Image>> &p_data) {
	Texture tex;
	texture_owner.initialize_rid(p_texture, tex);
}

void TextureStorage::texture_proxy_initialize(RID p_texture, RID p_base) {
	Texture tex;
	tex.proxy_to = p_base;
	texture_owner.initialize_rid(p_texture, tex);
	Texture *base = texture_owner.get_or_null(p_base);
	if (base) {
		base->proxies.insert(p_texture);
	}
}

void TextureStorage::texture_2d_update(RID p_texture, const Ref<Image> &p_image, int p_layer) {
	Texture *t = texture_owner.get_or_null(p_texture);
	ERR_FAIL_NULL(t);
	_texture_upload(t, p_image);
}

void TextureStorage::texture_proxy_update(RID p_proxy, RID p_base) {
	Texture *t = texture_owner.get_or_null(p_proxy);
	ERR_FAIL_NULL(t);
	Texture *old = texture_owner.get_or_null(t->proxy_to);
	if (old) {
		old->proxies.erase(p_proxy);
	}
	t->proxy_to = p_base;
	Texture *base = texture_owner.get_or_null(p_base);
	if (base) {
		base->proxies.insert(p_proxy);
	}
}

void TextureStorage::texture_2d_placeholder_initialize(RID p_texture) {
	Ref<Image> img = Image::create_empty(4, 4, false, Image::FORMAT_RGBA8);
	img->fill(Color(1, 0, 1, 1));
	texture_2d_initialize(p_texture, img);
}

void TextureStorage::texture_2d_layered_placeholder_initialize(RID p_texture, RSE::TextureLayeredType p_layered_type) {
	texture_2d_placeholder_initialize(p_texture);
}

void TextureStorage::texture_3d_placeholder_initialize(RID p_texture) {
	texture_2d_placeholder_initialize(p_texture);
}

Ref<Image> TextureStorage::_read_back(const C3D_Tex &p_tex, GPU_TEXCOLOR p_format, int p_width, int p_height) const {
	int tw = p_tex.width;
	int th = p_tex.height;
	int bpp = p_format == GPU_RGBA8 ? 4 : (p_format == GPU_LA8 ? 2 : (p_format == GPU_L8 ? 1 : 0));
	ERR_FAIL_COND_V(bpp == 0, Ref<Image>());
	Image::Format out_fmt = bpp == 4 ? Image::FORMAT_RGBA8 : (bpp == 2 ? Image::FORMAT_LA8 : Image::FORMAT_L8);
	Vector<uint8_t> data;
	data.resize(p_width * p_height * bpp);
	uint8_t *w = data.ptrw();
	GSPGPU_InvalidateDataCache(p_tex.data, p_tex.size);
	const uint8_t *src = (const uint8_t *)p_tex.data;
	for (int y = 0; y < p_height; y++) {
		for (int x = 0; x < p_width; x++) {
			const uint8_t *s = src + image_offset(x, y, tw, th) * bpp;
			uint8_t *d = w + (y * p_width + x) * bpp;
			if (bpp == 4) {
				d[0] = s[3];
				d[1] = s[2];
				d[2] = s[1];
				d[3] = s[0];
			} else if (bpp == 2) {
				d[0] = s[1];
				d[1] = s[0];
			} else {
				d[0] = s[0];
			}
		}
	}
	return Image::create_from_data(p_width, p_height, false, out_fmt, data);
}

Ref<Image> TextureStorage::texture_2d_get(RID p_texture) const {
	Texture *t = texture_owner.get_or_null(p_texture);
	ERR_FAIL_NULL_V(t, Ref<Image>());
	if (t->proxy_to.is_valid()) {
		return texture_2d_get(t->proxy_to);
	}
	if (t->render_target.is_valid()) {
		RenderTarget *rt = render_target_owner.get_or_null(t->render_target);
		ERR_FAIL_COND_V(!rt || !rt->allocated, Ref<Image>());
		return _read_back(rt->color[0], GPU_RGBA8, rt->size.x, rt->size.y);
	}
	ERR_FAIL_COND_V(!t->allocated, Ref<Image>());
	return _read_back(t->tex, t->gpu_format, t->width, t->height);
}

void TextureStorage::texture_replace(RID p_texture, RID p_by_texture) {
	Texture *t = texture_owner.get_or_null(p_texture);
	Texture *by = texture_owner.get_or_null(p_by_texture);
	ERR_FAIL_COND(!t || !by);
	_texture_release(t);
	// Take over the replacement's image; keep this RID's proxies and path.
	t->width = by->width;
	t->height = by->height;
	t->tex_width = by->tex_width;
	t->tex_height = by->tex_height;
	t->format = by->format;
	t->gpu_format = by->gpu_format;
	t->tex = by->tex;
	t->allocated = by->allocated;
	t->filter = by->filter;
	t->render_target = by->render_target;
	t->proxy_to = by->proxy_to;
	by->allocated = false;
	by->render_target = RID();
	texture_owner.free(p_by_texture);
}

void TextureStorage::texture_set_size_override(RID p_texture, int p_width, int p_height) {
	Texture *t = texture_owner.get_or_null(p_texture);
	ERR_FAIL_NULL(t);
	t->width = p_width;
	t->height = p_height;
}

void TextureStorage::texture_set_path(RID p_texture, const String &p_path) {
	Texture *t = texture_owner.get_or_null(p_texture);
	ERR_FAIL_NULL(t);
	t->path = p_path;
}

String TextureStorage::texture_get_path(RID p_texture) const {
	Texture *t = texture_owner.get_or_null(p_texture);
	ERR_FAIL_NULL_V(t, String());
	return t->path;
}

Image::Format TextureStorage::texture_get_format(RID p_texture) const {
	Texture *t = texture_owner.get_or_null(p_texture);
	ERR_FAIL_NULL_V(t, Image::FORMAT_MAX);
	return t->format;
}

Size2 TextureStorage::texture_size_with_proxy(RID p_proxy) {
	Texture *t = texture_owner.get_or_null(p_proxy);
	ERR_FAIL_NULL_V(t, Size2());
	if (t->proxy_to.is_valid()) {
		Texture *b = texture_owner.get_or_null(t->proxy_to);
		return b ? Size2(b->width, b->height) : Size2();
	}
	return Size2(t->width, t->height);
}

void TextureStorage::texture_debug_usage(List<RenderingServerTypes::TextureInfo> *r_info) {
	for (const RID &rid : texture_owner.get_owned_list()) {
		Texture *t = texture_owner.get_or_null(rid);
		if (!t || !t->allocated || t->render_target.is_valid()) {
			continue;
		}
		RenderingServerTypes::TextureInfo info;
		info.texture = rid;
		info.width = t->width;
		info.height = t->height;
		info.depth = 0;
		info.format = t->format;
		info.bytes = t->tex.size;
		info.path = t->path;
		r_info->push_back(info);
	}
}

/* Render target */

RID TextureStorage::render_target_create() {
	RenderTarget rt;
	RID rid = render_target_owner.make_rid(rt);
	RenderTarget *r = render_target_owner.get_or_null(rid);
	r->self = rid;

	Texture tex;
	tex.render_target = rid;
	r->texture = texture_owner.make_rid(tex);
	return rid;
}

void TextureStorage::_render_target_release(RenderTarget *p_rt) {
	if (p_rt->allocated || p_rt->target[0]) {
		DeadTarget d;
		d.target[0] = p_rt->target[0];
		d.target[1] = p_rt->target[1];
		d.color[0] = p_rt->color[0];
		d.color[1] = p_rt->color[1];
		d.eyes = p_rt->allocated ? (p_rt->target[1] ? 2 : 1) : 0;
		if (frame_active) {
			dead_targets.push_back(d); // Deleted before the next frame.
		} else {
			for (int i = 0; i < 2; i++) {
				if (d.target[i]) {
					C3D_RenderTargetDelete(d.target[i]);
				}
			}
			for (int i = 0; i < d.eyes; i++) {
				C3D_TexDelete(&d.color[i]);
			}
		}
	}
	p_rt->target[0] = nullptr;
	p_rt->target[1] = nullptr;
	p_rt->allocated = false;
	Texture *t = texture_owner.get_or_null(p_rt->texture);
	if (t) {
		t->allocated = false;
	}
}

void TextureStorage::_render_target_allocate(RenderTarget *p_rt) {
	if (frame_active) {
		// Keep drawing into the old buffers this frame; reallocate before the next.
		if (!p_rt->realloc_pending) {
			p_rt->realloc_pending = true;
			pending_realloc.push_back(p_rt->self);
		}
		return;
	}
	_render_target_release(p_rt);
	if (p_rt->size.x <= 0 || p_rt->size.y <= 0) {
		return;
	}
	int w = pot(p_rt->size.x);
	int h = pot(p_rt->size.y);
	int eyes = p_rt->stereo ? 2 : 1;
	for (int i = 0; i < eyes; i++) {
		// Render targets must live in VRAM.
		if (!C3D_TexInitVRAM(&p_rt->color[i], w, h, GPU_RGBA8)) {
			ERR_PRINT(vformat("3DS: out of VRAM for a %dx%d render target.", w, h));
			if (i == 1) {
				C3D_TexDelete(&p_rt->color[0]);
			}
			return;
		}
		C3D_TexSetFilter(&p_rt->color[i], GPU_LINEAR, GPU_LINEAR);
		C3D_TexSetWrap(&p_rt->color[i], GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
	}
	p_rt->target[0] = C3D_RenderTargetCreateFromTex(&p_rt->color[0], GPU_TEXFACE_2D, 0, GPU_RB_DEPTH24_STENCIL8);
	if (p_rt->stereo) {
		// Eyes are drawn one after the other, so they share a depth buffer.
		p_rt->target[1] = C3D_RenderTargetCreateFromTex(&p_rt->color[1], GPU_TEXFACE_2D, 0, C3D_DEPTHTYPE(-1));
		if (p_rt->target[0] && p_rt->target[1]) {
			C3D_FrameBufDepth(&p_rt->target[1]->frameBuf, p_rt->target[0]->frameBuf.depthBuf, GPU_RB_DEPTH24_STENCIL8);
		}
	}
	p_rt->tex_width = w;
	p_rt->tex_height = h;
	p_rt->allocated = p_rt->target[0] != nullptr;

	Texture *t = texture_owner.get_or_null(p_rt->texture);
	if (t) {
		t->tex = p_rt->color[0];
		t->tex_width = w;
		t->tex_height = h;
		t->width = p_rt->size.x;
		t->height = p_rt->size.y;
		t->format = Image::FORMAT_RGBA8;
		t->gpu_format = GPU_RGBA8;
		t->allocated = p_rt->allocated;
	}
}

void TextureStorage::render_target_set_stereo(RID p_rt, bool p_stereo) {
	RenderTarget *rt = render_target_owner.get_or_null(p_rt);
	ERR_FAIL_NULL(rt);
	if (rt->stereo == p_stereo) {
		return;
	}
	rt->stereo = p_stereo;
	_render_target_allocate(rt);
}

bool TextureStorage::render_target_begin(RenderTarget *p_rt, int p_eye) {
	if (!p_rt->allocated) {
		return false;
	}
	int eye = (p_eye == 1 && p_rt->stereo && p_rt->target[1]) ? 1 : 0;
	if (!C3D_FrameDrawOn(p_rt->target[eye])) {
		return false;
	}
	// The viewport sits at the end of the buffer, where UV v = 0 samples.
	C3D_SetViewport(0, 0, p_rt->size.x, p_rt->size.y);
	p_rt->used = true;
	return true;
}

void TextureStorage::render_target_clear(RenderTarget *p_rt, int p_eye, const Color &p_color, bool p_depth_only) {
	if (!p_rt->allocated) {
		return;
	}
	int eye = (p_eye == 1 && p_rt->stereo && p_rt->target[1]) ? 1 : 0;
	// Clears are memory fills queued straight to the GPU, while draws wait in
	// the command list until the frame ends. Submit pending draws first so a
	// clear can't jump ahead of them.
	if (frame_active) {
		C3D_FrameSplit(0);
	}
	C3D_RenderTargetClear(p_rt->target[eye], p_depth_only ? C3D_CLEAR_DEPTH : C3D_CLEAR_ALL, color_to_u32(p_color), 0);
}

void TextureStorage::render_target_free(RID p_rid) {
	RenderTarget *rt = render_target_owner.get_or_null(p_rid);
	ERR_FAIL_NULL(rt);
	_render_target_release(rt);
	Texture *t = texture_owner.get_or_null(rt->texture);
	if (t) {
		t->render_target = RID();
		texture_owner.free(rt->texture);
	}
	render_target_owner.free(p_rid);
}

void TextureStorage::render_target_set_position(RID p_render_target, int p_x, int p_y) {
	RenderTarget *rt = render_target_owner.get_or_null(p_render_target);
	ERR_FAIL_NULL(rt);
	rt->position = Point2i(p_x, p_y);
}

Point2i TextureStorage::render_target_get_position(RID p_render_target) const {
	RenderTarget *rt = render_target_owner.get_or_null(p_render_target);
	ERR_FAIL_NULL_V(rt, Point2i());
	return rt->position;
}

void TextureStorage::render_target_set_size(RID p_render_target, int p_width, int p_height, uint32_t p_view_count) {
	RenderTarget *rt = render_target_owner.get_or_null(p_render_target);
	ERR_FAIL_NULL(rt);
	if (rt->size == Size2i(p_width, p_height) && rt->allocated) {
		return;
	}
	rt->size = Size2i(MIN(p_width, 1024), MIN(p_height, 1024));
	rt->view_count = p_view_count;
	_render_target_allocate(rt);
}

Size2i TextureStorage::render_target_get_size(RID p_render_target) const {
	RenderTarget *rt = render_target_owner.get_or_null(p_render_target);
	ERR_FAIL_NULL_V(rt, Size2i());
	return rt->size;
}

void TextureStorage::render_target_set_transparent(RID p_render_target, bool p_is_transparent) {
	RenderTarget *rt = render_target_owner.get_or_null(p_render_target);
	ERR_FAIL_NULL(rt);
	rt->transparent = p_is_transparent;
}

bool TextureStorage::render_target_get_transparent(RID p_render_target) const {
	RenderTarget *rt = render_target_owner.get_or_null(p_render_target);
	ERR_FAIL_NULL_V(rt, false);
	return rt->transparent;
}

void TextureStorage::render_target_set_direct_to_screen(RID p_render_target, bool p_direct_to_screen) {
	RenderTarget *rt = render_target_owner.get_or_null(p_render_target);
	ERR_FAIL_NULL(rt);
	rt->direct_to_screen = p_direct_to_screen;
}

bool TextureStorage::render_target_get_direct_to_screen(RID p_render_target) const {
	RenderTarget *rt = render_target_owner.get_or_null(p_render_target);
	ERR_FAIL_NULL_V(rt, false);
	return rt->direct_to_screen;
}

bool TextureStorage::render_target_was_used(RID p_render_target) const {
	RenderTarget *rt = render_target_owner.get_or_null(p_render_target);
	ERR_FAIL_NULL_V(rt, false);
	return rt->used;
}

void TextureStorage::render_target_set_as_unused(RID p_render_target) {
	RenderTarget *rt = render_target_owner.get_or_null(p_render_target);
	ERR_FAIL_NULL(rt);
	rt->used = false;
}

void TextureStorage::render_target_request_clear(RID p_render_target, const Color &p_clear_color) {
	RenderTarget *rt = render_target_owner.get_or_null(p_render_target);
	ERR_FAIL_NULL(rt);
	rt->clear_requested = true;
	rt->clear_color = p_clear_color;
}

bool TextureStorage::render_target_is_clear_requested(RID p_render_target) {
	RenderTarget *rt = render_target_owner.get_or_null(p_render_target);
	ERR_FAIL_NULL_V(rt, false);
	return rt->clear_requested;
}

Color TextureStorage::render_target_get_clear_request_color(RID p_render_target) {
	RenderTarget *rt = render_target_owner.get_or_null(p_render_target);
	ERR_FAIL_NULL_V(rt, Color());
	return rt->clear_color;
}

void TextureStorage::render_target_disable_clear_request(RID p_render_target) {
	RenderTarget *rt = render_target_owner.get_or_null(p_render_target);
	ERR_FAIL_NULL(rt);
	rt->clear_requested = false;
}

void TextureStorage::render_target_do_clear_request(RID p_render_target) {
	RenderTarget *rt = render_target_owner.get_or_null(p_render_target);
	ERR_FAIL_NULL(rt);
	if (!rt->clear_requested) {
		return;
	}
	Color c = rt->transparent ? Color(0, 0, 0, 0) : rt->clear_color;
	render_target_clear(rt, 0, c);
	if (rt->stereo) {
		render_target_clear(rt, 1, c);
	}
	rt->clear_requested = false;
}

RID TextureStorage::render_target_get_texture(RID p_render_target) {
	RenderTarget *rt = render_target_owner.get_or_null(p_render_target);
	ERR_FAIL_NULL_V(rt, RID());
	return rt->texture;
}
