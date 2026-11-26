/**************************************************************************/
/*  etc1_export.cpp                                                       */
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

#include "etc1_export.h"

#include "export_plugin.h"

#include "core/io/file_access.h"
#include "scene/resources/compressed_texture.h"
#include "scene/resources/portable_compressed_texture.h"

#include "modules/modules_enabled.gen.h" // For etcpak.

#ifdef MODULE_ETCPAK_ENABLED
#include <thirdparty/etcpak/ProcessRGB.hpp>
#endif

Ref<Image> EditorExportPlugin3DSTextures::encode_etc1(const Ref<Image> &p_image) {
#ifdef MODULE_ETCPAK_ENABLED
	ERR_FAIL_COND_V(p_image.is_null() || p_image->is_empty(), Ref<Image>());
	Ref<Image> img = p_image->duplicate();
	if (img->is_compressed() && img->decompress() != OK) {
		return Ref<Image>();
	}
	if (img->get_width() < 4 || img->get_height() < 4) {
		return Ref<Image>(); // Not worth a block.
	}
	img->convert(Image::FORMAT_RGBA8);
	// The 3DS only uses mipmaps of power-of-two textures.
	if (img->has_mipmaps() && !(Math::is_power_of_2(img->get_width()) && Math::is_power_of_2(img->get_height()))) {
		img->clear_mipmaps();
	}
	const bool alpha = img->detect_alpha() != Image::ALPHA_NONE;
	const Image::Format target = alpha ? Image::FORMAT_ETC2_RGBA8 : Image::FORMAT_ETC;
	const bool mipmaps = img->has_mipmaps();
	// Like Godot's own etcpak path: the top level is padded to whole blocks.
	const int width = (img->get_width() + 3) & ~3;
	const int height = (img->get_height() + 3) & ~3;

	Vector<uint8_t> out;
	out.resize(Image::get_image_data_size(width, height, target, mipmaps));
	uint8_t *dst = out.ptrw();
	const int levels = (mipmaps ? Image::get_image_required_mipmaps(width, height, target) : 0) + 1;
	const int src_levels = img->get_mipmap_count() + 1;
	const uint8_t *src = img->ptr();

	Vector<uint32_t> color_src, alpha_src;
	Vector<uint64_t> color_blocks, alpha_blocks;
	for (int i = 0; i < levels; i++) {
		int dw, dh;
		int64_t dst_ofs = Image::get_image_mipmap_offset_and_dimensions(width, height, target, i, dw, dh);
		dw = (dw + 3) & ~3;
		dh = (dh + 3) & ~3;
		const uint32_t blocks = dw * dh / 16;

		int64_t src_ofs, src_size;
		int sw, sh;
		img->get_mipmap_offset_size_and_dimensions(MIN(i, src_levels - 1), src_ofs, src_size, sw, sh);
		const uint8_t *s = src + src_ofs;

		// BGRA for etcpak's ETC encoder, alpha in the red byte for its EAC
		// encoder; edges smeared out to whole blocks.
		color_src.resize(dw * dh);
		alpha_src.resize(alpha ? dw * dh : 0);
		uint32_t *c = color_src.ptrw();
		uint32_t *a = alpha_src.ptrw();
		for (int y = 0; y < dh; y++) {
			const uint8_t *row = s + (int64_t)MIN(y, sh - 1) * sw * 4;
			for (int x = 0; x < dw; x++) {
				const uint8_t *px = row + MIN(x, sw - 1) * 4;
				c[y * dw + x] = px[2] | (px[1] << 8) | (px[0] << 16) | ((uint32_t)px[3] << 24);
				if (alpha) {
					a[y * dw + x] = (uint32_t)px[3] << 16;
				}
			}
		}

		color_blocks.resize(blocks);
		CompressEtc1RgbDither(color_src.ptr(), color_blocks.ptrw(), blocks, dw);
		if (alpha) {
			alpha_blocks.resize(blocks);
			CompressEacR(alpha_src.ptr(), alpha_blocks.ptrw(), blocks, dw);
			for (uint32_t b = 0; b < blocks; b++) {
				memcpy(dst + dst_ofs + b * 16, &alpha_blocks[b], 8);
				memcpy(dst + dst_ofs + b * 16 + 8, &color_blocks[b], 8);
			}
		} else {
			memcpy(dst + dst_ofs, color_blocks.ptr(), blocks * 8);
		}
	}
	return Image::create_from_data(width, height, mipmaps, target, out);
#else
	return Ref<Image>();
#endif
}

bool EditorExportPlugin3DSTextures::supports_platform(const Ref<EditorExportPlatform> &p_export_platform) const {
	return Object::cast_to<EditorExportPlatform3DS>(p_export_platform.ptr()) != nullptr;
}

bool EditorExportPlugin3DSTextures::_begin_customize_resources(const Ref<EditorExportPlatform> &p_platform, const Vector<String> &p_features) {
	if (!supports_platform(p_platform)) {
		return false;
	}
	Variant option = get_option("textures/etc1");
	mode = option.get_type() == Variant::INT ? (int)option : MODE_VRAM_COMPRESSED;
#ifndef MODULE_ETCPAK_ENABLED
	if (mode != MODE_DISABLED) {
		WARN_PRINT("3DS export: ETC1 textures need the etcpak module in the editor; exporting textures unchanged.");
	}
	mode = MODE_DISABLED;
#endif
	return mode != MODE_DISABLED;
}

Ref<Resource> EditorExportPlugin3DSTextures::_customize_resource(const Ref<Resource> &p_resource, const String &p_path) {
	Ref<CompressedTexture2D> ct = p_resource;
	if (ct.is_null() || ct->get_load_path().is_empty()) {
		return Ref<Resource>();
	}
	// Read the imported .ctex directly: get_image() needs a renderer, and
	// headless exports have none.
	Ref<FileAccess> f = FileAccess::open(ct->get_load_path(), FileAccess::READ);
	if (f.is_null()) {
		return Ref<Resource>();
	}
	uint8_t magic[4];
	f->get_buffer(magic, 4);
	if (memcmp(magic, "GST2", 4) != 0) {
		return Ref<Resource>();
	}
	f->get_32(); // Version.
	int width = f->get_32();
	int height = f->get_32();
	for (int i = 0; i < 5; i++) {
		f->get_32(); // Data format flags, mipmap limit, reserved.
	}
	Ref<Image> img = CompressedTexture2D::load_image_from_file(f, 0);
	if (img.is_null() || img->is_empty()) {
		return Ref<Resource>();
	}
	if (mode == MODE_VRAM_COMPRESSED && !img->is_compressed()) {
		return Ref<Resource>(); // Lossless/lossy imports (2D, pixel art) stay exact.
	}
	if (img->get_format() == Image::FORMAT_ETC) {
		return Ref<Resource>();
	}
	Ref<Image> etc = encode_etc1(img);
	if (etc.is_null()) {
		return Ref<Resource>();
	}
	Ref<PortableCompressedTexture2D> tex;
	tex.instantiate();
	tex->set_keep_compressed_buffer(true); // Saved into the .pck from this buffer.
	tex->create_from_image(etc, PortableCompressedTexture2D::COMPRESSION_MODE_ETC2);
	// Padding to whole blocks must not change what Godot thinks the size is.
	if (width > 0 && height > 0 && (width != etc->get_width() || height != etc->get_height())) {
		tex->set_size_override(Size2(width, height));
	}
	return tex;
}

uint64_t EditorExportPlugin3DSTextures::_get_customization_configuration_hash() const {
	// Bump the version when the encoding changes, so cached exports redo it.
	const uint64_t version = 1;
	return (version << 8) | (uint64_t)mode;
}
