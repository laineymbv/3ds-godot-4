/**************************************************************************/
/*  utilities_citro3d.cpp                                                 */
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

#include "utilities_citro3d.h"

#include "light_storage_citro3d.h"
#include "material_storage_citro3d.h"
#include "mesh_storage_citro3d.h"
#include "texture_storage_citro3d.h"

using namespace Citro3D;

RSE::InstanceType Utilities::get_base_type(RID p_rid) const {
	if (MeshStorage::get_singleton()->owns_mesh(p_rid)) {
		return RSE::INSTANCE_MESH;
	} else if (MeshStorage::get_singleton()->owns_multimesh(p_rid)) {
		return RSE::INSTANCE_MULTIMESH;
	} else if (LightStorage::get_singleton()->owns_light(p_rid)) {
		return RSE::INSTANCE_LIGHT;
	} else if (LightStorage::get_singleton()->owns_lightmap(p_rid)) {
		return RSE::INSTANCE_LIGHTMAP;
	}
	return RSE::INSTANCE_NONE;
}

bool Utilities::free(RID p_rid) {
	TextureStorage *ts = TextureStorage::get_singleton();
	MeshStorage *ms = MeshStorage::get_singleton();
	MaterialStorage *mat = MaterialStorage::get_singleton();
	LightStorage *ls = LightStorage::get_singleton();
	if (ts->owns_render_target(p_rid)) {
		ts->render_target_free(p_rid);
	} else if (ts->owns_texture(p_rid)) {
		ts->texture_free(p_rid);
	} else if (ts->owns_canvas_texture(p_rid)) {
		ts->canvas_texture_free(p_rid);
	} else if (ms->owns_mesh(p_rid)) {
		ms->mesh_free(p_rid);
	} else if (ms->owns_multimesh(p_rid)) {
		ms->multimesh_free(p_rid);
	} else if (ms->owns_skeleton(p_rid)) {
		ms->skeleton_free(p_rid);
	} else if (mat->owns_shader(p_rid)) {
		mat->shader_free(p_rid);
	} else if (mat->owns_material(p_rid)) {
		mat->material_free(p_rid);
	} else if (ls->owns_light(p_rid)) {
		ls->light_free(p_rid);
	} else if (ls->owns_light_instance(p_rid)) {
		ls->light_instance_free(p_rid);
	} else if (ls->free(p_rid)) {
		// Lightmaps (dummy).
	} else {
		return false;
	}
	return true;
}

void Utilities::base_update_dependency(RID p_base, DependencyTracker *p_instance) {
	if (MeshStorage::get_singleton()->owns_mesh(p_base)) {
		p_instance->update_dependency(&MeshStorage::get_singleton()->get_mesh(p_base)->dependency);
	} else if (MeshStorage::get_singleton()->owns_multimesh(p_base)) {
		MultiMesh *mm = MeshStorage::get_singleton()->get_multimesh(p_base);
		p_instance->update_dependency(&mm->dependency);
		Mesh *m = MeshStorage::get_singleton()->get_mesh(mm->mesh);
		if (m) {
			p_instance->update_dependency(&m->dependency);
		}
	} else if (LightStorage::get_singleton()->owns_light(p_base)) {
		p_instance->update_dependency(&LightStorage::get_singleton()->get_light(p_base)->dependency);
	}
}
