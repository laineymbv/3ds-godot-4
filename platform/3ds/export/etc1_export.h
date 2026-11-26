/**************************************************************************/
/*  etc1_export.h                                                         */
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

#include "core/io/image.h"
#include "editor/export/editor_export_plugin.h"

// Re-encodes textures as ETC1 when exporting for the 3DS, so the PICA200
// samples them as they are (4 bits per texel, 8 with alpha) instead of the
// template decompressing BCn to 16/32-bit at load.
//
// Opaque textures become FORMAT_ETC. Textures with alpha become
// FORMAT_ETC2_RGBA8 whose color half only uses ETC1 blocks; the 3DS turns the
// EAC alpha into ETC1A4's 4-bit alpha. Both are ordinary Godot formats, so the
// .pck stays valid data, inside a PortableCompressedTexture2D.
class EditorExportPlugin3DSTextures : public EditorExportPlugin {
	GDCLASS(EditorExportPlugin3DSTextures, EditorExportPlugin);

	int mode = 0;

public:
	enum Mode {
		MODE_DISABLED,
		MODE_VRAM_COMPRESSED, // Only textures imported as VRAM Compressed (3D textures, by default).
		MODE_ALL,
	};

	// Shared with tests. Returns an empty Ref when the image can't be encoded.
	static Ref<Image> encode_etc1(const Ref<Image> &p_image);

	String get_name() const override { return "3DS ETC1 textures"; }
	bool supports_platform(const Ref<EditorExportPlatform> &p_export_platform) const override;
	bool _begin_customize_resources(const Ref<EditorExportPlatform> &p_platform, const Vector<String> &p_features) override;
	Ref<Resource> _customize_resource(const Ref<Resource> &p_resource, const String &p_path) override;
	uint64_t _get_customization_configuration_hash() const override;
};
