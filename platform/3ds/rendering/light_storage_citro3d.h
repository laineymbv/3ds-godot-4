/**************************************************************************/
/*  light_storage_citro3d.h                                               */
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

#include "servers/rendering/dummy/storage/light_storage.h"
#include "servers/rendering/storage/utilities.h"

namespace Citro3D {

struct Light {
	RSE::LightType type = RSE::LIGHT_DIRECTIONAL;
	Color color = Color(1, 1, 1);
	float param[RSE::LIGHT_PARAM_MAX] = {};
	bool negative = false;
	bool shadow = false;
	uint32_t cull_mask = 0xFFFFFFFF;
	uint64_t version = 0;
	Dependency dependency;
};

struct LightInstance {
	RID light;
	Transform3D transform;
	AABB aabb;
};

class LightStorage : public RendererDummy::LightStorage {
	static inline LightStorage *singleton = nullptr;
	mutable RID_Owner<Light, true> light_owner;
	mutable RID_Owner<LightInstance> light_instance_owner;

	RID _light_create(RSE::LightType p_type);

public:
	static LightStorage *get_singleton() { return singleton; }

	Light *get_light(RID p_rid) const { return light_owner.get_or_null(p_rid); }
	LightInstance *get_light_instance(RID p_rid) const { return light_instance_owner.get_or_null(p_rid); }
	bool owns_light(RID p_rid) const { return light_owner.owns(p_rid); }
	bool owns_light_instance(RID p_rid) const { return light_instance_owner.owns(p_rid); }

	RID directional_light_allocate() override;
	void directional_light_initialize(RID p_rid) override;
	RID omni_light_allocate() override;
	void omni_light_initialize(RID p_rid) override;
	RID spot_light_allocate() override;
	void spot_light_initialize(RID p_rid) override;
	RID area_light_allocate() override;
	void area_light_initialize(RID p_rid) override;
	void light_free(RID p_rid) override;

	void light_set_color(RID p_light, const Color &p_color) override;
	void light_set_param(RID p_light, RSE::LightParam p_param, float p_value) override;
	void light_set_shadow(RID p_light, bool p_enabled) override;
	void light_set_negative(RID p_light, bool p_enable) override;
	void light_set_cull_mask(RID p_light, uint32_t p_mask) override;

	bool light_has_shadow(RID p_light) const override;
	RSE::LightType light_get_type(RID p_light) const override;
	AABB light_get_aabb(RID p_light) const override;
	float light_get_param(RID p_light, RSE::LightParam p_param) override;
	Color light_get_color(RID p_light) override;
	uint64_t light_get_version(RID p_light) const override;
	uint32_t light_get_cull_mask(RID p_light) const override;

	RID light_instance_create(RID p_light) override;
	void light_instance_free(RID p_light) override;
	void light_instance_set_transform(RID p_light_instance, const Transform3D &p_transform) override;
	void light_instance_set_aabb(RID p_light_instance, const AABB &p_aabb) override;

	LightStorage();
	~LightStorage();
};

} // namespace Citro3D
