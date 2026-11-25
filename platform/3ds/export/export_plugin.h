/**************************************************************************/
/*  export_plugin.h                                                       */
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

#include "editor/export/editor_export_platform.h"
#include "scene/resources/texture.h"

class Image;

// Exports a project as a Nintendo 3DS homebrew .3dsx (written directly: the
// template's code plus a new SMDH and a RomFS holding the .pck) or an
// installable .cia (through makerom and bannertool).
class EditorExportPlatform3DS : public EditorExportPlatform {
	GDCLASS(EditorExportPlatform3DS, EditorExportPlatform);

	Ref<Texture2D> logo;
	Ref<Texture2D> run_icon;

	String _get_template(const Ref<EditorExportPreset> &p_preset, bool p_debug, String *r_error) const;
	Ref<Image> _get_icon(const Ref<EditorExportPreset> &p_preset) const;
	String _get_title(const Ref<EditorExportPreset> &p_preset) const;

	Error _export_3dsx(const Ref<EditorExportPreset> &p_preset, const String &p_template, const String &p_pck, const String &p_path);
	Error _export_cia(const Ref<EditorExportPreset> &p_preset, const String &p_template, const String &p_pck, const String &p_path);

public:
	// Shared with tests: pure functions over bytes.
	static Vector<uint8_t> make_smdh(const String &p_title, const String &p_description, const String &p_author, const Ref<Image> &p_icon);
	static Vector<uint8_t> make_romfs_header(const String &p_file_name, uint64_t p_file_size, uint32_t &r_data_offset);

	void get_preset_features(const Ref<EditorExportPreset> &p_preset, List<String> *r_features) const override;
	void get_export_options(List<ExportOption> *r_options) const override;
	String get_name() const override { return "Nintendo 3DS"; }
	String get_os_name() const override { return "3DS"; }
	Ref<Texture2D> get_logo() const override { return logo; }
	Ref<Texture2D> get_run_icon() const override { return run_icon; }
	bool has_valid_export_configuration(const Ref<EditorExportPreset> &p_preset, String &r_error, bool &r_missing_templates, bool p_debug = false) const override;
	bool has_valid_project_configuration(const Ref<EditorExportPreset> &p_preset, String &r_error) const override;
	List<String> get_binary_extensions(const Ref<EditorExportPreset> &p_preset) const override;
	Error export_project(const Ref<EditorExportPreset> &p_preset, bool p_debug, const String &p_path, BitField<EditorExportPlatform::DebugFlags> p_flags = 0, bool p_notify = true) override;
	void get_platform_features(List<String> *r_features) const override;
	void resolve_platform_feature_priorities(const Ref<EditorExportPreset> &p_preset, HashSet<String> &p_features) override {}
	void initialize() override;
};
