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

#include "servers/rendering/shader_language.h"
#include "servers/rendering/shader_types.h"

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
		_translate_custom(p_shader);
		if (p_shader->recipe.ok) {
			s.uses_vertex_color = p_shader->recipe.albedo.vertex_color;
			s.uses_alpha = p_shader->recipe.has_alpha && !s.uses_alpha_scissor;
			s.uses_emission = p_shader->recipe.has_emission;
			s.texture_nearest = p_shader->recipe.texture_nearest;
			s.texture_repeat = p_shader->recipe.texture_repeat;
		} else {
			WARN_PRINT(vformat("3DS: shader %s can't run on the PICA200 (%s); it draws unshaded with its 'albedo'/'color' and 'texture_albedo'/'texture' parameters.", p_shader->path.is_empty() ? String("(embedded)") : p_shader->path, p_shader->recipe.failure));
			s.unshaded = true;
		}
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

void MaterialStorage::shader_set_path_hint(RID p_shader, const String &p_path) {
	Shader *s = shader_owner.get_or_null(p_shader);
	ERR_FAIL_NULL(s);
	s->path = p_path;
}

/* Custom shader translation */

namespace {
using SL = ShaderLanguage;

struct Translator {
	SL::ShaderNode *shader = nullptr;
	String failure;

	bool fail(const String &p_why) {
		if (failure.is_empty()) {
			failure = p_why;
		}
		return false;
	}

	static Color constant_color(const Vector<SL::Scalar> &p_values, SL::DataType p_type) {
		switch (p_type) {
			case SL::TYPE_FLOAT:
				return Color(p_values[0].real, p_values[0].real, p_values[0].real, p_values[0].real);
			case SL::TYPE_INT:
				return Color(p_values[0].sint, p_values[0].sint, p_values[0].sint, p_values[0].sint);
			case SL::TYPE_VEC3:
				return Color(p_values[0].real, p_values[1].real, p_values[2].real, 1.0f);
			case SL::TYPE_VEC4:
				return Color(p_values[0].real, p_values[1].real, p_values[2].real, p_values[3].real);
			default:
				return Color(1, 1, 1, 1);
		}
	}

	bool is_uniform(const StringName &p_name) const { return shader->uniforms.has(p_name); }

	// UV expression: UV, UV * c, UV + c, UV + TIME * c (any order of these).
	bool parse_uv(SL::Node *p_node, ShaderRecipe &r) {
		if (p_node->type == SL::Node::NODE_TYPE_VARIABLE && static_cast<SL::VariableNode *>(p_node)->name == "UV") {
			return true;
		}
		if (p_node->type != SL::Node::NODE_TYPE_OPERATOR) {
			return fail("unsupported texture coordinates");
		}
		SL::OperatorNode *op = static_cast<SL::OperatorNode *>(p_node);
		if (op->arguments.size() != 2) {
			return fail("unsupported texture coordinates");
		}
		SL::Node *a = op->arguments[0];
		SL::Node *b = op->arguments[1];
		if (op->op == SL::OP_MUL && b->type == SL::Node::NODE_TYPE_CONSTANT) {
			Color c = constant_color(b->get_values(), b->get_datatype());
			r.uv_scale *= Vector2(c.r, c.g);
			return parse_uv(a, r);
		}
		if (op->op == SL::OP_ADD) {
			// UV + TIME * k (or k * TIME)
			if (b->type == SL::Node::NODE_TYPE_OPERATOR && static_cast<SL::OperatorNode *>(b)->op == SL::OP_MUL) {
				SL::OperatorNode *m = static_cast<SL::OperatorNode *>(b);
				SL::Node *t = m->arguments[0];
				SL::Node *k = m->arguments[1];
				if (k->type == SL::Node::NODE_TYPE_VARIABLE) {
					SWAP(t, k);
				}
				if (t->type == SL::Node::NODE_TYPE_VARIABLE && static_cast<SL::VariableNode *>(t)->name == "TIME" && k->type == SL::Node::NODE_TYPE_CONSTANT) {
					Color c = constant_color(k->get_values(), k->get_datatype());
					r.uv_scroll += Vector2(c.r, c.g);
					return parse_uv(a, r);
				}
			}
			if (b->type == SL::Node::NODE_TYPE_CONSTANT) {
				Color c = constant_color(b->get_values(), b->get_datatype());
				r.uv_offset += Vector2(c.r, c.g);
				return parse_uv(a, r);
			}
		}
		return fail("unsupported texture coordinates");
	}

	// A product of constants, uniforms, COLOR and one texture() lookup.
	bool parse_product(SL::Node *p_node, ShaderProduct &r_p, ShaderRecipe &r) {
		switch (p_node->type) {
			case SL::Node::NODE_TYPE_CONSTANT:
				r_p.constant *= constant_color(p_node->get_values(), p_node->get_datatype());
				return true;
			case SL::Node::NODE_TYPE_VARIABLE: {
				StringName name = static_cast<SL::VariableNode *>(p_node)->name;
				if (name == "COLOR") {
					r_p.vertex_color = true;
					return true;
				}
				if (is_uniform(name)) {
					r_p.uniforms.push_back(name);
					return true;
				}
				return fail(vformat("uses '%s'", name));
			}
			case SL::Node::NODE_TYPE_MEMBER: {
				// Swizzles (.rgb, .a, ...): the channel comes from the target.
				SL::MemberNode *m = static_cast<SL::MemberNode *>(p_node);
				return parse_product(m->owner, r_p, r);
			}
			case SL::Node::NODE_TYPE_OPERATOR: {
				SL::OperatorNode *op = static_cast<SL::OperatorNode *>(p_node);
				if (op->op == SL::OP_MUL && op->arguments.size() == 2) {
					return parse_product(op->arguments[0], r_p, r) && parse_product(op->arguments[1], r_p, r);
				}
				if (op->op == SL::OP_CALL && op->arguments.size() >= 1 && op->arguments[0]->type == SL::Node::NODE_TYPE_VARIABLE) {
					StringName fn = static_cast<SL::VariableNode *>(op->arguments[0])->name;
					if (fn == "texture" && op->arguments.size() == 3 && op->arguments[1]->type == SL::Node::NODE_TYPE_VARIABLE) {
						StringName sampler = static_cast<SL::VariableNode *>(op->arguments[1])->name;
						if (r_p.texture != StringName() && r_p.texture != sampler) {
							return fail("more than one texture");
						}
						r_p.texture = sampler;
						return parse_uv(op->arguments[2], r);
					}
					if (fn == "vec3" || fn == "vec4" || fn == "float") {
						// Constructors of constants or one uniform/value.
						if (op->arguments.size() == 2) {
							return parse_product(op->arguments[1], r_p, r);
						}
						Vector<SL::Scalar> vals;
						for (int i = 1; i < op->arguments.size(); i++) {
							if (op->arguments[i]->type != SL::Node::NODE_TYPE_CONSTANT) {
								return fail("non-constant vector constructor");
							}
							vals.append_array(op->arguments[i]->get_values());
						}
						r_p.constant *= constant_color(vals, fn == "vec4" ? SL::TYPE_VEC4 : SL::TYPE_VEC3);
						return true;
					}
					return fail(vformat("calls %s()", fn));
				}
				return fail("uses an operator other than *");
			}
			default:
				return fail("unsupported expression");
		}
	}

	bool parse_float(SL::Node *p_node, StringName &r_uniform, float &r_const) {
		if (p_node->type == SL::Node::NODE_TYPE_CONSTANT) {
			r_const = constant_color(p_node->get_values(), p_node->get_datatype()).r;
			return true;
		}
		if (p_node->type == SL::Node::NODE_TYPE_VARIABLE && is_uniform(static_cast<SL::VariableNode *>(p_node)->name)) {
			r_uniform = static_cast<SL::VariableNode *>(p_node)->name;
			return true;
		}
		return fail("non-constant roughness/metallic/specular");
	}

	bool run(SL::ShaderNode *p_shader, ShaderRecipe &r) {
		shader = p_shader;
		for (const KeyValue<StringName, SL::ShaderNode::Uniform> &E : shader->uniforms) {
			const SL::ShaderNode::Uniform &u = E.value;
			if (u.default_value.size() >= 1 && u.type != SL::TYPE_SAMPLER2D) {
				Color c = constant_color(u.default_value, u.type);
				r.defaults[E.key] = u.type == SL::TYPE_FLOAT ? Variant(c.r) : Variant(c);
			}
		}
		for (const KeyValue<StringName, SL::ShaderNode::Function> &E : shader->functions) {
			if (E.key == "vertex" && E.value.function && E.value.function->body && !E.value.function->body->statements.is_empty()) {
				return fail("has a vertex() function");
			}
			if (E.key == "light") {
				return fail("has a light() function");
			}
		}
		const SL::ShaderNode::Function *frag = shader->functions.getptr("fragment");
		if (!frag || !frag->function || !frag->function->body) {
			return true; // White.
		}
		for (SL::Node *st : frag->function->body->statements) {
			if (st->type != SL::Node::NODE_TYPE_OPERATOR) {
				return fail("uses statements other than assignments");
			}
			SL::OperatorNode *op = static_cast<SL::OperatorNode *>(st);
			if (op->op != SL::OP_ASSIGN || op->arguments.size() != 2 || op->arguments[0]->type != SL::Node::NODE_TYPE_VARIABLE) {
				return fail("uses statements other than OUTPUT = expression");
			}
			StringName target = static_cast<SL::VariableNode *>(op->arguments[0])->name;
			SL::Node *value = op->arguments[1];
			if (target == "ALBEDO") {
				if (!parse_product(value, r.albedo, r)) {
					return false;
				}
			} else if (target == "ALPHA") {
				r.has_alpha = true;
				if (!parse_product(value, r.alpha, r)) {
					return false;
				}
			} else if (target == "EMISSION") {
				r.has_emission = true;
				if (!parse_product(value, r.emission, r)) {
					return false;
				}
				if (r.emission.texture != StringName() || r.emission.vertex_color) {
					return fail("textured or vertex-colored EMISSION");
				}
			} else if (target == "ROUGHNESS") {
				if (!parse_float(value, r.roughness, r.roughness_const)) {
					return false;
				}
			} else if (target == "METALLIC") {
				if (!parse_float(value, r.metallic, r.metallic_const)) {
					return false;
				}
			} else if (target == "SPECULAR") {
				if (!parse_float(value, r.specular, r.specular_const)) {
					return false;
				}
			} else {
				return fail(vformat("writes %s", target));
			}
		}
		if (r.alpha.texture != StringName() && r.albedo.texture != StringName() && r.alpha.texture != r.albedo.texture) {
			return fail("ALPHA and ALBEDO use different textures");
		}
		return true;
	}
};
} // namespace

void MaterialStorage::_translate_custom(Shader *p_shader) {
	ShaderRecipe &r = p_shader->recipe;
	r = ShaderRecipe();
	ShaderLanguage sl;
	ShaderLanguage::ShaderCompileInfo info;
	info.functions = ShaderTypes::get_singleton()->get_functions(RSE::SHADER_SPATIAL);
	info.render_modes = ShaderTypes::get_singleton()->get_modes(RSE::SHADER_SPATIAL);
	info.stencil_modes = ShaderTypes::get_singleton()->get_stencil_modes(RSE::SHADER_SPATIAL);
	info.shader_types = ShaderTypes::get_singleton()->get_types();
	if (sl.compile(p_shader->code, info) != OK) {
		r.failure = "parse error: " + sl.get_error_text();
		return;
	}
	Translator t;
	r.ok = t.run(sl.get_shader(), r);
	r.failure = t.failure;
	StringName tex = r.albedo.texture != StringName() ? r.albedo.texture : r.alpha.texture;
	const ShaderLanguage::ShaderNode::Uniform *u = tex != StringName() ? sl.get_shader()->uniforms.getptr(tex) : nullptr;
	if (u) {
		r.texture_nearest = u->filter == ShaderLanguage::FILTER_NEAREST || u->filter == ShaderLanguage::FILTER_NEAREST_MIPMAP || u->filter == ShaderLanguage::FILTER_NEAREST_MIPMAP_ANISOTROPIC;
		r.texture_repeat = u->repeat != ShaderLanguage::REPEAT_DISABLE;
	}
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

	if (!_param(p, "albedo", r_state.albedo) && !_param(p, "color", r_state.albedo) && s) {
		// Unset: the default written in the shader code.
		if (!_param(s->recipe.defaults, "albedo", r_state.albedo)) {
			_param(s->recipe.defaults, "color", r_state.albedo);
		}
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

	if (s && s->recipe.ok) {
		const ShaderRecipe &rc = s->recipe;
		auto value = [&](const StringName &p_name) -> Variant {
			const Variant *v = p.getptr(p_name);
			if (v) {
				return *v;
			}
			const Variant *d = rc.defaults.getptr(p_name);
			return d ? *d : Variant();
		};
		auto eval = [&](const ShaderProduct &p_prod) -> Color {
			Color c = p_prod.constant;
			for (const StringName &u : p_prod.uniforms) {
				Variant v = value(u);
				switch (v.get_type()) {
					case Variant::COLOR:
						c *= Color(v);
						break;
					case Variant::VECTOR3: {
						Vector3 v3 = v;
						c *= Color(v3.x, v3.y, v3.z, 1.0f);
					} break;
					case Variant::VECTOR4: {
						Vector4 v4 = v;
						c *= Color(v4.x, v4.y, v4.z, v4.w);
					} break;
					case Variant::FLOAT:
					case Variant::INT: {
						float f = v;
						c *= Color(f, f, f, f);
					} break;
					default:
						break;
				}
			}
			return c;
		};
		Color albedo = eval(rc.albedo);
		float alpha = rc.has_alpha ? eval(rc.alpha).r : 1.0f;
		r_state.albedo = Color(albedo.r, albedo.g, albedo.b, alpha);
		StringName tex = rc.albedo.texture != StringName() ? rc.albedo.texture : rc.alpha.texture;
		r_state.albedo_texture = tex != StringName() ? RID(value(tex)) : RID();
		r_state.uv_scale = rc.uv_scale;
		r_state.uv_offset = rc.uv_offset;
		r_state.uv_scroll = rc.uv_scroll;
		r_state.emission = rc.has_emission ? eval(rc.emission) : Color(0, 0, 0);
		auto fval = [&](const StringName &p_u, float p_const, float p_default) -> float {
			if (p_u != StringName()) {
				return value(p_u);
			}
			return p_const >= 0 ? p_const : p_default;
		};
		r_state.roughness = fval(rc.roughness, rc.roughness_const, 1.0f);
		r_state.metallic = fval(rc.metallic, rc.metallic_const, 0.0f);
		r_state.specular = fval(rc.specular, rc.specular_const, 0.5f);
	}
}
