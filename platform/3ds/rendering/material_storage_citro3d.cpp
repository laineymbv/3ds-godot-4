/**************************************************************************/
/*  material_storage_citro3d.cpp                                          */
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

#include "material_storage_citro3d.h"

using namespace Citro3D;

MaterialStorage::MaterialStorage() {
	singleton = this;
}

MaterialStorage::~MaterialStorage() {
	singleton = nullptr;
}

/* Shaders */

RID MaterialStorage::shader_allocate() {
	return shader_owner.allocate_rid();
}

void MaterialStorage::shader_initialize(RID p_rid, bool p_embedded) {
	shader_owner.initialize_rid(p_rid, Shader());
	if (p_embedded) {
		embedded_set.insert(p_rid);
	}
}

void MaterialStorage::shader_free(RID p_rid) {
	Shader *s = shader_owner.get_or_null(p_rid);
	ERR_FAIL_NULL(s);
	s->dependency.deleted_notify(p_rid);
	embedded_set.erase(p_rid);
	shader_owner.free(p_rid);
}

static bool _has_token(const String &p_list, const char *p_token) {
	Vector<String> parts = p_list.split(",");
	for (const String &part : parts) {
		if (part.strip_edges() == p_token) {
			return true;
		}
	}
	return false;
}

void MaterialStorage::_parse_shader(Shader *p_shader) {
	const String &code = p_shader->code;
	Shader::Info &s = p_shader->info;
	s = Shader::Info();

	if (code.contains("shader_type canvas_item")) {
		s.kind = Shader::KIND_CANVAS_ITEM;
		s.cull = RSE::CULL_MODE_DISABLED;
	} else if (code.contains("shader_type spatial")) {
		s.kind = Shader::KIND_SPATIAL;
	} else {
		s.kind = Shader::KIND_OTHER;
	}
	s.generated = code.begins_with("// NOTE: Shader automatically converted from") && (code.contains("StandardMaterial3D") || code.contains("ORMMaterial3D"));
	bool canvas_generated = s.kind == Shader::KIND_CANVAS_ITEM && !code.contains("void fragment") && !code.contains("void vertex");

	// render_mode a, b, c;
	int rm = code.find("render_mode ");
	String modes;
	if (rm >= 0) {
		int end = code.find(";", rm);
		modes = code.substr(rm + 12, end - rm - 12);
	}
	if (_has_token(modes, "blend_add")) {
		s.blend = Shader::BLEND_ADD;
	} else if (_has_token(modes, "blend_sub")) {
		s.blend = Shader::BLEND_SUB;
	} else if (_has_token(modes, "blend_mul")) {
		s.blend = Shader::BLEND_MUL;
	} else if (_has_token(modes, "blend_premul_alpha")) {
		s.blend = Shader::BLEND_PREMUL_ALPHA;
	}
	if (_has_token(modes, "cull_front")) {
		s.cull = RSE::CULL_MODE_FRONT;
	} else if (_has_token(modes, "cull_disabled")) {
		s.cull = RSE::CULL_MODE_DISABLED;
	}
	s.unshaded = _has_token(modes, "unshaded");
	s.depth_draw_never = _has_token(modes, "depth_draw_never");
	s.depth_draw_always = _has_token(modes, "depth_draw_always");
	s.depth_test_disabled = _has_token(modes, "depth_test_disabled");

	s.uses_alpha_scissor = code.contains("ALPHA_SCISSOR_THRESHOLD");
	s.uses_alpha = !s.uses_alpha_scissor && (code.contains("ALPHA *=") || code.contains("ALPHA ="));
	s.uses_vertex_color = code.contains("albedo_tex *= COLOR");
	s.uses_emission = code.contains("EMISSION =");

	// Sampler hints on the albedo texture.
	int tu = code.find("uniform sampler2D texture_albedo");
	if (tu >= 0) {
		String line = code.substr(tu, code.find(";", tu) - tu);
		s.texture_nearest = line.contains("filter_nearest");
		s.texture_repeat = line.contains("repeat_enable");
	}

	if (s.kind == Shader::KIND_SPATIAL && !s.generated) {
		WARN_PRINT_ONCE("3DS: custom spatial shaders aren't supported (the PICA200 has no fragment shaders). They draw unshaded with their 'albedo'/'color' parameter and 'texture_albedo'/'texture' sampler.");
		s.unshaded = true;
	} else if (s.kind == Shader::KIND_CANVAS_ITEM && !canvas_generated) {
		WARN_PRINT_ONCE("3DS: custom canvas_item shaders aren't supported; items draw with the default 2D pipeline.");
	}
}

void MaterialStorage::shader_set_code(RID p_shader, const String &p_code) {
	Shader *s = shader_owner.get_or_null(p_shader);
	ERR_FAIL_NULL(s);
	s->code = p_code;
	_parse_shader(s);
	s->dependency.changed_notify(Dependency::DEPENDENCY_CHANGED_MATERIAL);
}

String MaterialStorage::shader_get_code(RID p_shader) const {
	Shader *s = shader_owner.get_or_null(p_shader);
	ERR_FAIL_NULL_V(s, String());
	return s->code;
}

/* Materials */

RID MaterialStorage::material_allocate() {
	return material_owner.allocate_rid();
}

void MaterialStorage::material_initialize(RID p_rid) {
	material_owner.initialize_rid(p_rid, Material());
}

void MaterialStorage::material_free(RID p_rid) {
	Material *m = material_owner.get_or_null(p_rid);
	ERR_FAIL_NULL(m);
	m->dependency.deleted_notify(p_rid);
	material_owner.free(p_rid);
}

void MaterialStorage::material_set_render_priority(RID p_material, int priority) {
	Material *m = material_owner.get_or_null(p_material);
	ERR_FAIL_NULL(m);
	m->priority = priority;
}

void MaterialStorage::material_set_shader(RID p_shader_material, RID p_shader) {
	Material *m = material_owner.get_or_null(p_shader_material);
	ERR_FAIL_NULL(m);
	m->shader = p_shader;
	m->dependency.changed_notify(Dependency::DEPENDENCY_CHANGED_MATERIAL);
}

void MaterialStorage::material_set_param(RID p_material, const StringName &p_param, const Variant &p_value) {
	Material *m = material_owner.get_or_null(p_material);
	ERR_FAIL_NULL(m);
	if (p_value.get_type() == Variant::NIL) {
		m->params.erase(p_param);
	} else {
		m->params[p_param] = p_value;
	}
}

Variant MaterialStorage::material_get_param(RID p_material, const StringName &p_param) const {
	Material *m = material_owner.get_or_null(p_material);
	ERR_FAIL_NULL_V(m, Variant());
	const Variant *v = m->params.getptr(p_param);
	return v ? *v : Variant();
}

void MaterialStorage::material_set_next_pass(RID p_material, RID p_next_material) {
	Material *m = material_owner.get_or_null(p_material);
	ERR_FAIL_NULL(m);
	m->next_pass = p_next_material;
	m->dependency.changed_notify(Dependency::DEPENDENCY_CHANGED_MATERIAL);
}

RSE::CullMode MaterialStorage::material_get_cull_mode(RID p_material) const {
	Material *m = material_owner.get_or_null(p_material);
	ERR_FAIL_NULL_V(m, RSE::CULL_MODE_BACK);
	Shader *s = shader_owner.get_or_null(m->shader);
	return s ? s->info.cull : RSE::CULL_MODE_BACK;
}

void MaterialStorage::material_update_dependency(RID p_material, DependencyTracker *p_instance) {
	Material *m = material_owner.get_or_null(p_material);
	if (!m) {
		return;
	}
	p_instance->update_dependency(&m->dependency);
	Shader *s = shader_owner.get_or_null(m->shader);
	if (s) {
		p_instance->update_dependency(&s->dependency);
	}
	if (m->next_pass.is_valid()) {
		material_update_dependency(m->next_pass, p_instance);
	}
}

template <typename T>
static bool _param(const HashMap<StringName, Variant> &p_params, const char *p_name, T &r_value) {
	const Variant *v = p_params.getptr(StringName(p_name));
	if (!v) {
		return false;
	}
	r_value = *v;
	return true;
}

void MaterialStorage::get_state(RID p_material, MaterialState &r_state) const {
	r_state = MaterialState();
	Material *m = material_owner.get_or_null(p_material);
	if (!m) {
		return;
	}
	Shader *s = shader_owner.get_or_null(m->shader);
	r_state.shader = s;
	const HashMap<StringName, Variant> &p = m->params;

	if (!_param(p, "albedo", r_state.albedo)) {
		_param(p, "color", r_state.albedo);
	}
	if (!_param(p, "texture_albedo", r_state.albedo_texture)) {
		_param(p, "texture", r_state.albedo_texture);
	}
	Vector3 v3;
	if (_param(p, "uv1_scale", v3)) {
		r_state.uv_scale = Vector2(v3.x, v3.y);
	}
	if (_param(p, "uv1_offset", v3)) {
		r_state.uv_offset = Vector2(v3.x, v3.y);
	}
	if (s && s->info.uses_emission) {
		Color e;
		float energy = 1.0;
		if (_param(p, "emission", e)) {
			_param(p, "emission_energy", energy);
			r_state.emission = Color(e.r * energy, e.g * energy, e.b * energy);
		}
	}
	_param(p, "roughness", r_state.roughness);
	_param(p, "metallic", r_state.metallic);
	_param(p, "specular", r_state.specular);
	_param(p, "alpha_scissor_threshold", r_state.alpha_scissor);
	r_state.transparent = s && (s->info.uses_alpha || s->info.blend != Shader::BLEND_MIX);
}
