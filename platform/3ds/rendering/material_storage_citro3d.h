/**************************************************************************/
/*  material_storage_citro3d.h                                            */
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

#include "servers/rendering/dummy/storage/material_storage.h"
#include "servers/rendering/storage/utilities.h"

namespace Citro3D {

// What a shader's code means for the fixed-function PICA200. Only the
// shaders Godot generates for BaseMaterial3D / CanvasItemMaterial map fully;
// hand-written shaders draw unshaded with their color/texture parameters.
struct Shader {
	enum Kind {
		KIND_SPATIAL,
		KIND_CANVAS_ITEM,
		KIND_OTHER,
	};
	enum Blend {
		BLEND_MIX,
		BLEND_ADD,
		BLEND_SUB,
		BLEND_MUL,
		BLEND_PREMUL_ALPHA,
	};
	struct Info {
		Kind kind = KIND_SPATIAL;
		bool generated = false; // From StandardMaterial3D / ORMMaterial3D.
		Blend blend = BLEND_MIX;
		RSE::CullMode cull = RSE::CULL_MODE_BACK;
		bool unshaded = false;
		bool depth_draw_never = false;
		bool depth_draw_always = false;
		bool depth_test_disabled = false;
		bool uses_alpha = false;
		bool uses_alpha_scissor = false;
		bool uses_vertex_color = false;
		bool uses_emission = false;
		bool texture_nearest = false;
		bool texture_repeat = false;
	};
	String code;
	Info info;
	Dependency dependency;
};

struct Material {
	RID shader;
	RID next_pass;
	int priority = 0;
	HashMap<StringName, Variant> params;
	Dependency dependency;
};

// Flattened render state for one draw.
struct MaterialState {
	const Shader *shader = nullptr;
	Color albedo = Color(1, 1, 1, 1);
	RID albedo_texture;
	Vector2 uv_scale = Vector2(1, 1);
	Vector2 uv_offset;
	Color emission = Color(0, 0, 0);
	float roughness = 1.0;
	float metallic = 0.0;
	float specular = 0.5;
	float alpha_scissor = 0.5;
	bool transparent = false;
};

class MaterialStorage : public RendererDummy::MaterialStorage {
	static inline MaterialStorage *singleton = nullptr;
	mutable RID_Owner<Shader, true> shader_owner;
	mutable RID_Owner<Material, true> material_owner;
	HashSet<RID> embedded_set;

	void _parse_shader(Shader *p_shader);

public:
	static MaterialStorage *get_singleton() { return singleton; }

	Shader *get_shader(RID p_rid) const { return shader_owner.get_or_null(p_rid); }
	Material *get_material(RID p_rid) const { return material_owner.get_or_null(p_rid); }
	bool owns_shader(RID p_rid) { return shader_owner.owns(p_rid); }
	bool owns_material(RID p_rid) { return material_owner.owns(p_rid); }

	// Fills r_state from a material (or defaults when the RID is null).
	void get_state(RID p_material, MaterialState &r_state) const;

	RID shader_allocate() override;
	void shader_initialize(RID p_rid, bool p_embedded) override;
	void shader_free(RID p_rid) override;
	void shader_set_code(RID p_shader, const String &p_code) override;
	String shader_get_code(RID p_shader) const override;
	void get_shader_parameter_list(RID p_shader, List<PropertyInfo> *p_param_list) const override {}
	const HashSet<RID> &shader_embedded_set_get() const override { return embedded_set; }

	RID material_allocate() override;
	void material_initialize(RID p_rid) override;
	void material_free(RID p_rid) override;
	void material_set_render_priority(RID p_material, int priority) override;
	void material_set_shader(RID p_shader_material, RID p_shader) override;
	void material_set_param(RID p_material, const StringName &p_param, const Variant &p_value) override;
	Variant material_get_param(RID p_material, const StringName &p_param) const override;
	void material_set_next_pass(RID p_material, RID p_next_material) override;
	RSE::CullMode material_get_cull_mode(RID p_material) const override;
	void material_get_instance_shader_parameters(RID p_material, List<InstanceShaderParam> *r_parameters) override {}
	void material_update_dependency(RID p_material, DependencyTracker *p_instance) override;

	MaterialStorage();
	~MaterialStorage();
};

} // namespace Citro3D
