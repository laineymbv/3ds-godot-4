/**************************************************************************/
/*  light_storage_citro3d.cpp                                             */
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

#include "light_storage_citro3d.h"

using namespace Citro3D;

LightStorage::LightStorage() {
	singleton = this;
}

LightStorage::~LightStorage() {
	singleton = nullptr;
}

RID LightStorage::_light_create(RSE::LightType p_type) {
	Light l;
	l.type = p_type;
	l.param[RSE::LIGHT_PARAM_ENERGY] = 1.0;
	l.param[RSE::LIGHT_PARAM_INDIRECT_ENERGY] = 1.0;
	l.param[RSE::LIGHT_PARAM_SPECULAR] = 0.5;
	l.param[RSE::LIGHT_PARAM_RANGE] = 1.0;
	l.param[RSE::LIGHT_PARAM_ATTENUATION] = 1.0;
	l.param[RSE::LIGHT_PARAM_SPOT_ANGLE] = 45;
	l.param[RSE::LIGHT_PARAM_SPOT_ATTENUATION] = 1.0;
	return light_owner.make_rid(l);
}

// The allocate/initialize split exists for threaded servers; lights are
// small, so allocate makes them fully and initialize does nothing.
RID LightStorage::directional_light_allocate() {
	return _light_create(RSE::LIGHT_DIRECTIONAL);
}
void LightStorage::directional_light_initialize(RID p_rid) {}
RID LightStorage::omni_light_allocate() {
	return _light_create(RSE::LIGHT_OMNI);
}
void LightStorage::omni_light_initialize(RID p_rid) {}
RID LightStorage::spot_light_allocate() {
	return _light_create(RSE::LIGHT_SPOT);
}
void LightStorage::spot_light_initialize(RID p_rid) {}
RID LightStorage::area_light_allocate() {
	// Area lights draw as omni lights.
	return _light_create(RSE::LIGHT_OMNI);
}
void LightStorage::area_light_initialize(RID p_rid) {}

void LightStorage::light_free(RID p_rid) {
	Light *l = light_owner.get_or_null(p_rid);
	ERR_FAIL_NULL(l);
	l->dependency.deleted_notify(p_rid);
	light_owner.free(p_rid);
}

void LightStorage::light_set_color(RID p_light, const Color &p_color) {
	Light *l = light_owner.get_or_null(p_light);
	ERR_FAIL_NULL(l);
	l->color = p_color;
}

void LightStorage::light_set_param(RID p_light, RSE::LightParam p_param, float p_value) {
	Light *l = light_owner.get_or_null(p_light);
	ERR_FAIL_NULL(l);
	ERR_FAIL_INDEX(p_param, RSE::LIGHT_PARAM_MAX);
	l->param[p_param] = p_value;
	if (p_param == RSE::LIGHT_PARAM_RANGE || p_param == RSE::LIGHT_PARAM_SPOT_ANGLE) {
		l->version++;
		l->dependency.changed_notify(Dependency::DEPENDENCY_CHANGED_LIGHT);
	}
}

void LightStorage::light_set_shadow(RID p_light, bool p_enabled) {
	Light *l = light_owner.get_or_null(p_light);
	ERR_FAIL_NULL(l);
	l->shadow = p_enabled; // No shadow maps on the PICA200 path yet.
}

void LightStorage::light_set_negative(RID p_light, bool p_enable) {
	Light *l = light_owner.get_or_null(p_light);
	ERR_FAIL_NULL(l);
	l->negative = p_enable;
}

void LightStorage::light_set_cull_mask(RID p_light, uint32_t p_mask) {
	Light *l = light_owner.get_or_null(p_light);
	ERR_FAIL_NULL(l);
	l->cull_mask = p_mask;
	l->version++;
	l->dependency.changed_notify(Dependency::DEPENDENCY_CHANGED_CULL_MASK);
}

bool LightStorage::light_has_shadow(RID p_light) const {
	return false;
}

RSE::LightType LightStorage::light_get_type(RID p_light) const {
	Light *l = light_owner.get_or_null(p_light);
	ERR_FAIL_NULL_V(l, RSE::LIGHT_DIRECTIONAL);
	return l->type;
}

AABB LightStorage::light_get_aabb(RID p_light) const {
	Light *l = light_owner.get_or_null(p_light);
	ERR_FAIL_NULL_V(l, AABB());
	switch (l->type) {
		case RSE::LIGHT_SPOT: {
			float len = l->param[RSE::LIGHT_PARAM_RANGE];
			float size = Math::tan(Math::deg_to_rad(l->param[RSE::LIGHT_PARAM_SPOT_ANGLE])) * len;
			return AABB(Vector3(-size, -size, -len), Vector3(size * 2, size * 2, len));
		}
		case RSE::LIGHT_OMNI: {
			float r = l->param[RSE::LIGHT_PARAM_RANGE];
			return AABB(-Vector3(r, r, r), Vector3(r, r, r) * 2);
		}
		default:
			return AABB();
	}
}

float LightStorage::light_get_param(RID p_light, RSE::LightParam p_param) {
	Light *l = light_owner.get_or_null(p_light);
	ERR_FAIL_NULL_V(l, 0.0);
	ERR_FAIL_INDEX_V(p_param, RSE::LIGHT_PARAM_MAX, 0.0);
	return l->param[p_param];
}

Color LightStorage::light_get_color(RID p_light) {
	Light *l = light_owner.get_or_null(p_light);
	ERR_FAIL_NULL_V(l, Color());
	return l->color;
}

uint64_t LightStorage::light_get_version(RID p_light) const {
	Light *l = light_owner.get_or_null(p_light);
	ERR_FAIL_NULL_V(l, 0);
	return l->version;
}

uint32_t LightStorage::light_get_cull_mask(RID p_light) const {
	Light *l = light_owner.get_or_null(p_light);
	ERR_FAIL_NULL_V(l, 0);
	return l->cull_mask;
}

RID LightStorage::light_instance_create(RID p_light) {
	LightInstance li;
	li.light = p_light;
	return light_instance_owner.make_rid(li);
}

void LightStorage::light_instance_free(RID p_light) {
	light_instance_owner.free(p_light);
}

void LightStorage::light_instance_set_transform(RID p_light_instance, const Transform3D &p_transform) {
	LightInstance *li = light_instance_owner.get_or_null(p_light_instance);
	ERR_FAIL_NULL(li);
	li->transform = p_transform;
}

void LightStorage::light_instance_set_aabb(RID p_light_instance, const AABB &p_aabb) {
	LightInstance *li = light_instance_owner.get_or_null(p_light_instance);
	ERR_FAIL_NULL(li);
	li->aabb = p_aabb;
}
