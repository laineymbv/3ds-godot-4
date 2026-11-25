/**************************************************************************/
/*  nintendo_3ds.cpp                                                      */
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

#include "nintendo_3ds.h"

#include "core/object/class_db.h"
#include "scene/main/canvas_item.h"
#include "scene/resources/material.h"
#include "scene/resources/shader.h"

#ifdef THREEDS_ENABLED
#include "platform/3ds/ctru.h"
#include "platform/3ds/os_3ds.h"
#include "platform/3ds/rendering/rasterizer_citro3d.h"
#endif

void Nintendo3DS::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_3d_slider"), &Nintendo3DS::get_3d_slider);
	ClassDB::bind_method(D_METHOD("is_new_3ds"), &Nintendo3DS::is_new_3ds);
	ClassDB::bind_method(D_METHOD("get_battery_level"), &Nintendo3DS::get_battery_level);
	ClassDB::bind_method(D_METHOD("is_charging"), &Nintendo3DS::is_charging);
	ClassDB::bind_method(D_METHOD("set_stereo_separation", "max_separation"), &Nintendo3DS::set_stereo_separation);
	ClassDB::bind_method(D_METHOD("get_stereo_separation"), &Nintendo3DS::get_stereo_separation);
	ClassDB::bind_method(D_METHOD("set_stereo_convergence", "convergence"), &Nintendo3DS::set_stereo_convergence);
	ClassDB::bind_method(D_METHOD("get_stereo_convergence"), &Nintendo3DS::get_stereo_convergence);
	ClassDB::bind_method(D_METHOD("set_depth", "item", "depth", "recursive"), &Nintendo3DS::set_depth, DEFVAL(true));
	ClassDB::bind_method(D_METHOD("get_depth", "item"), &Nintendo3DS::get_depth);
	ClassDB::bind_method(D_METHOD("save_screens", "dir"), &Nintendo3DS::save_screens);
	ClassDB::bind_method(D_METHOD("get_audio_stats"), &Nintendo3DS::get_audio_stats);
}

static Ref<Shader> _depth_shader() {
	static Ref<Shader> shader;
	if (shader.is_null()) {
		shader.instantiate();
		shader->set_code("shader_type canvas_item;\nuniform float stereo_depth = 0.0;\nvoid fragment() {}\n");
	}
	return shader;
}

// Descendants without a material of their own draw with their parent's, so
// the whole subtree shares one depth.
static void _inherit_material(CanvasItem *p_item) {
	for (int i = 0; i < p_item->get_child_count(); i++) {
		CanvasItem *child = Object::cast_to<CanvasItem>(p_item->get_child(i));
		if (!child) {
			continue;
		}
		if (child->get_material().is_null()) {
			child->set_use_parent_material(true);
		}
		if (child->get_use_parent_material()) {
			_inherit_material(child);
		}
	}
}

void Nintendo3DS::set_depth(CanvasItem *p_item, float p_depth, bool p_recursive) {
	ERR_FAIL_NULL(p_item);
	Ref<ShaderMaterial> mat = p_item->get_material();
	if (mat.is_null() || mat->get_shader() != _depth_shader()) {
		if (p_item->get_material().is_valid() && mat.is_null()) {
			WARN_PRINT(vformat("Nintendo3DS.set_depth: %s already has a different material; it is replaced.", p_item->get_name()));
		}
		mat.instantiate();
		mat->set_shader(_depth_shader());
		p_item->set_material(mat);
	}
	mat->set_shader_parameter("stereo_depth", p_depth);
	if (p_recursive) {
		_inherit_material(p_item);
	}
}

float Nintendo3DS::get_depth(CanvasItem *p_item) const {
	ERR_FAIL_NULL_V(p_item, 0.0f);
	// An item using its parent's material has the parent's depth.
	while (p_item && p_item->get_material().is_null() && p_item->get_use_parent_material()) {
		p_item = Object::cast_to<CanvasItem>(p_item->get_parent());
	}
	Ref<ShaderMaterial> mat;
	if (p_item) {
		mat = p_item->get_material();
	}
	if (mat.is_null() || mat->get_shader() != _depth_shader()) {
		return 0.0f;
	}
	return mat->get_shader_parameter("stereo_depth");
}

bool Nintendo3DS::is_depth_material(const Ref<Material> &p_material) {
	Ref<ShaderMaterial> sm = p_material;
	return sm.is_valid() && sm->get_shader() == _depth_shader();
}

float Nintendo3DS::get_3d_slider() const {
#ifdef THREEDS_ENABLED
	return osGet3DSliderState();
#else
	return 0.0;
#endif
}

bool Nintendo3DS::is_new_3ds() const {
#ifdef THREEDS_ENABLED
	bool is_new = false;
	APT_CheckNew3DS(&is_new);
	return is_new;
#else
	return false;
#endif
}

int Nintendo3DS::get_battery_level() const {
#ifdef THREEDS_ENABLED
	u8 level = 0;
	if (R_SUCCEEDED(ptmuInit())) {
		PTMU_GetBatteryLevel(&level);
		ptmuExit();
	}
	return level; // 0 (empty) .. 5 (full)
#else
	return 5;
#endif
}

bool Nintendo3DS::is_charging() const {
#ifdef THREEDS_ENABLED
	u8 charging = 0;
	if (R_SUCCEEDED(ptmuInit())) {
		PTMU_GetBatteryChargeState(&charging);
		ptmuExit();
	}
	return charging != 0;
#else
	return false;
#endif
}

void Nintendo3DS::set_stereo_separation(float p_max_separation) {
#ifdef THREEDS_ENABLED
	if (RasterizerCitro3D::get_singleton()) {
		RasterizerCitro3D::get_singleton()->stereo_separation_override = p_max_separation;
	}
#endif
}

float Nintendo3DS::get_stereo_separation() const {
#ifdef THREEDS_ENABLED
	if (RasterizerCitro3D::get_singleton()) {
		return RasterizerCitro3D::get_singleton()->get_stereo_separation();
	}
#endif
	return 0.0;
}

void Nintendo3DS::set_stereo_convergence(float p_convergence) {
#ifdef THREEDS_ENABLED
	if (RasterizerCitro3D::get_singleton()) {
		RasterizerCitro3D::get_singleton()->stereo_convergence_override = p_convergence;
	}
#endif
}

float Nintendo3DS::get_stereo_convergence() const {
#ifdef THREEDS_ENABLED
	if (RasterizerCitro3D::get_singleton()) {
		return RasterizerCitro3D::get_singleton()->get_stereo_convergence();
	}
#endif
	return 0.0;
}

void Nintendo3DS::save_screens(const String &p_dir) {
#ifdef THREEDS_ENABLED
	if (RasterizerCitro3D::get_singleton()) {
		RasterizerCitro3D::get_singleton()->request_screen_dump(p_dir);
	}
#endif
}

Dictionary Nintendo3DS::get_audio_stats() const {
	Dictionary d;
#ifdef THREEDS_ENABLED
	AudioDriver3DS *ad = static_cast<OS_3DS *>(OS::get_singleton())->get_audio_driver();
	d["active"] = ad->is_active();
	d["buffers_played"] = (int64_t)ad->get_buffers_played();
	d["last_peak"] = ad->get_last_peak();
#else
	d["active"] = false;
	d["buffers_played"] = 0;
	d["last_peak"] = 0;
#endif
	return d;
}

Nintendo3DS::Nintendo3DS() {
	singleton = this;
}

Nintendo3DS::~Nintendo3DS() {
	singleton = nullptr;
}
